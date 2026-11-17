#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include "syscall.h"
#include "atomic.h"
#include "pthread_impl.h"
#include "rseq.h"

#ifdef VDSO_GETCPU_SYM
/* Not checked by CFI: the target is in the vDSO, and its address is
 * read-only (see vdso.c). */
no_cfi_icall
static long vdso_getcpu(void *f, unsigned *cpu, unsigned *node, void *unused)
{
	return ((long (*)(unsigned *, unsigned *, void *))f)(cpu, node, unused);
}
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
	void *f = __vdso_funcs->getcpu;
	if (f) {
		r = vdso_getcpu(f, &cpu, 0, 0);
		if (!r) return cpu;
		if (r != -ENOSYS) return __syscall_ret(r);
	}
#endif

	r = __syscall(SYS_getcpu, &cpu, 0, 0);
	if (!r) return cpu;
	return __syscall_ret(r);
}
