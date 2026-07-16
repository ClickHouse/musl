#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include "syscall.h"
#include "atomic.h"
#include "pthread_impl.h"
#include "rseq.h"

#ifdef VDSO_GETCPU_SYM

static void *volatile vdso_func;

typedef long (*getcpu_f)(unsigned *, unsigned *, void *);

static long getcpu_init(unsigned *cpu, unsigned *node, void *unused)
{
	void *p = __vdsosym(VDSO_GETCPU_VER, VDSO_GETCPU_SYM);
	getcpu_f f = (getcpu_f)p;
	a_cas_p(&vdso_func, (void *)getcpu_init, p);
	return f ? f(cpu, node, unused) : -ENOSYS;
}

static void *volatile vdso_func = (void *)getcpu_init;

#endif

int sched_getcpu(void)
{
	int r;
	unsigned cpu;
	pthread_t self = __pthread_self();

	/* Fast path: the kernel keeps cpu_id in the registered rseq area
	 * current across migrations, making this a plain memory read.
	 * Registration is lazy, on the first call of each thread; the
	 * signal handler reentrancy cases are all benign: a nested call
	 * either also registers (the outer one then gets -EBUSY, treated
	 * as registered) or reads a live area. Only rseq_cs and flags are
	 * written before registering - both must be 0, and writing them
	 * never disturbs a concurrently live registration since this
	 * implementation keeps them 0 forever. */
	if (self->rseq_state == RSEQ_STATE_REGISTERED) {
		r = (int)__rseq_area(self)->cpu_id;
		if (r >= 0) return r;
	} else if (self->rseq_state == RSEQ_STATE_UNREGISTERED) {
		volatile struct k_rseq *area = __rseq_area(self);
		area->rseq_cs = 0;
		area->flags = 0;
		r = __syscall(SYS_rseq, area, RSEQ_ABI_SIZE, 0, RSEQ_SIG);
		if (r == 0 || r == -EBUSY) {
			self->rseq_state = RSEQ_STATE_REGISTERED;
			r = (int)area->cpu_id;
			if (r >= 0) return r;
		} else {
			self->rseq_state = RSEQ_STATE_UNAVAILABLE;
		}
	}

#ifdef VDSO_GETCPU_SYM
	getcpu_f f = (getcpu_f)vdso_func;
	if (f) {
		r = f(&cpu, 0, 0);
		if (!r) return cpu;
		if (r != -ENOSYS) return __syscall_ret(r);
	}
#endif

	r = __syscall(SYS_getcpu, &cpu, 0, 0);
	if (!r) return cpu;
	return __syscall_ret(r);
}
