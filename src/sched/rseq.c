#define _GNU_SOURCE
#include <errno.h>
#include <stddef.h>
#include "pthread_impl.h"
#include "syscall.h"
#include "rseq.h"

/* See src/internal/rseq.h for what these describe. All three start out zero;
 * __rseq_size only becomes nonzero once the main thread has actually
 * registered, so a program reading them never sees an area the kernel is not
 * maintaining. */
ptrdiff_t __rseq_offset;
unsigned int __rseq_size;
unsigned int __rseq_flags;

void __rseq_register(pthread_t self)
{
	volatile struct k_rseq *area = __rseq_area(self);
	/* __rseq_offset is published from the main thread and is meant to
	 * describe every thread. It only does so while struct pthread keeps the
	 * same alignment everywhere, which is what MIN_TLS_ALIGN buys us. Should
	 * that ever stop holding, leave this thread on the syscall path instead
	 * of letting the program read an address that is not its area. */
	if ((char *)area - (char *)TP_ADJ(self) != __rseq_offset) {
		self->rseq_state = RSEQ_STATE_UNAVAILABLE;
		return;
	}
	area->rseq_cs = 0;
	area->flags = 0;
	/* Anything other than success means this thread has no area of ours to
	 * read: ENOSYS on a pre-4.18 kernel or a seccomp sandbox, and EBUSY if
	 * some other component registered an area of its own first - in which
	 * case the kernel maintains that one and ours would stay zero forever,
	 * silently reporting CPU 0 everywhere. Fall back to the syscall path. */
	if (__syscall(SYS_rseq, area, RSEQ_ABI_SIZE, 0, RSEQ_SIG) == 0)
		self->rseq_state = RSEQ_STATE_REGISTERED;
	else
		self->rseq_state = RSEQ_STATE_UNAVAILABLE;
}

/* What the initial thread has registered, if anything. The dynamic linker sets
 * TLS up twice - once on a bootstrap builtin_tls, then again on the real block -
 * so __init_tp, and with it __rseq_init, can run a second time with struct
 * pthread at a new address. */
static volatile struct k_rseq *initial_area;

void __rseq_init(pthread_t self)
{
	/* Drop any earlier registration first. Without this the kernel would keep
	 * updating the abandoned bootstrap area and registering the new one would
	 * fail with EBUSY, leaving the process on the syscall path for good. */
	if (initial_area) {
		__syscall(SYS_rseq, initial_area, RSEQ_ABI_SIZE,
			RSEQ_FLAG_UNREGISTER, RSEQ_SIG);
		initial_area = 0;
		__rseq_size = 0;
	}
	/* Publish the position before registering, since __rseq_register checks
	 * each thread against it. Every thread places the area at this same
	 * distance from the thread pointer, so one offset covers the process.
	 * __rseq_flags stays 0, as in glibc. */
	__rseq_offset = (char *)__rseq_area(self) - (char *)TP_ADJ(self);
	__rseq_register(self);
	if (self->rseq_state == RSEQ_STATE_REGISTERED) {
		__rseq_size = RSEQ_ABI_SIZE;
		initial_area = __rseq_area(self);
	} else {
		/* Clear both together. A nonzero size with a stale offset is the
		 * worst outcome available: readers take the size as proof the area
		 * is live and then dereference the thread pointer plus garbage. */
		__rseq_offset = 0;
		__rseq_size = 0;
	}
}
