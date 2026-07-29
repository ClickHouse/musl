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
	 * current across migrations, so this is a plain memory read. Every
	 * thread registers at its entry point before running any user code
	 * (see rseq.c), so there is nothing to set up here, and no way for a
	 * signal handler to observe a half-registered area. */
	if (self->rseq_state == RSEQ_STATE_REGISTERED) {
		r = (int)__rseq_area(self)->cpu_id;
		if (r >= 0) return r;
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
