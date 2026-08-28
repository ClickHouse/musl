#define _GNU_SOURCE
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include "pthread_impl.h"
#include "libc.h"
#include "syscall.h"
#include "rseq.h"

/* See src/internal/rseq.h for what these describe. All three start out zero;
 * __rseq_size only becomes nonzero once the main thread has actually
 * registered, so a program reading them never sees an area the kernel is not
 * maintaining. */
ptrdiff_t __rseq_offset;
unsigned int __rseq_size;
unsigned int __rseq_flags;

unsigned int __rseq_alloc_len;

/* Feature size to publish in __rseq_size once registration succeeds. */
static unsigned int rseq_feature_size;

static unsigned long auxv_get(unsigned long tag)
{
	size_t *auxv;
	for (auxv = libc.auxv; auxv[0]; auxv += 2)
		if (auxv[0] == tag) return auxv[1];
	return 0;
}

/* Compute the registration length and active feature size from the
 * auxiliary vector, following the same recipe as glibc and librseq:
 * register the original 32 bytes while the kernel's fields fit in them
 * (the kernel maintains every field within 32 bytes for such legacy
 * registrations), otherwise the feature size rounded up to the kernel's
 * required alignment. If the kernel outgrows our fixed capacity or
 * alignment, fall back to the original 32-byte registration, which every
 * kernel accepts, and cap the published feature size at what the
 * registered length covers. */
static void rseq_compute_sizes(void)
{
	unsigned long feature = auxv_get(AT_RSEQ_FEATURE_SIZE);
	unsigned long align = auxv_get(AT_RSEQ_ALIGN);

	if (feature < RSEQ_ORIG_FEATURE_SIZE) feature = RSEQ_ORIG_FEATURE_SIZE;
	if (align < RSEQ_ORIG_SIZE) align = RSEQ_ORIG_SIZE;

	if (feature <= RSEQ_ORIG_SIZE) {
		__rseq_alloc_len = RSEQ_ORIG_SIZE;
	} else {
		unsigned long alloc = feature + align-1 & -align;
		if (align > RSEQ_AREA_ALIGN || alloc > RSEQ_AREA_CAP) {
			__rseq_alloc_len = RSEQ_ORIG_SIZE;
			feature = RSEQ_ORIG_SIZE;
		} else {
			__rseq_alloc_len = alloc;
		}
	}
	rseq_feature_size = feature;
}

void __rseq_register(pthread_t self)
{
	volatile struct k_rseq *area = __rseq_area(self);
	/* __rseq_offset is published from the main thread and is meant to
	 * describe every thread. It only does so while struct pthread keeps the
	 * same alignment everywhere, which is what MIN_TLS_ALIGN buys us. Should
	 * that ever stop holding, leave this thread on the syscall path instead
	 * of letting the program read an address that is not its area. */
	if ((char *)area - (char *)TP_ADJ(self) != __rseq_offset) {
		area->cpu_id = RSEQ_CPU_ID_REGISTRATION_FAILED;
		self->rseq_state = RSEQ_STATE_UNAVAILABLE;
		return;
	}
	memset((void *)area, 0, __rseq_alloc_len);
	area->cpu_id = RSEQ_CPU_ID_UNINITIALIZED;
	/* Anything other than success means this thread has no area of ours to
	 * read: ENOSYS on a pre-4.18 kernel or a seccomp sandbox, and EBUSY if
	 * some other component registered an area of its own first - in which
	 * case the kernel maintains that one and ours would stay zero forever,
	 * silently reporting CPU 0 everywhere. Fall back to the syscall path,
	 * and leave the glibc failure marker in cpu_id: once __rseq_size is
	 * published for the process, external readers of TP + __rseq_offset
	 * have no other way to tell this thread's dead area from CPU 0. */
	if (__syscall(SYS_rseq, area, __rseq_alloc_len, 0, RSEQ_SIG) == 0) {
		self->rseq_state = RSEQ_STATE_REGISTERED;
	} else {
		area->cpu_id = RSEQ_CPU_ID_REGISTRATION_FAILED;
		self->rseq_state = RSEQ_STATE_UNAVAILABLE;
	}
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
	 * fail with EBUSY, leaving the process on the syscall path for good.
	 * Unregistration must pass the length used at registration. */
	if (initial_area) {
		__syscall(SYS_rseq, initial_area, __rseq_alloc_len,
			RSEQ_FLAG_UNREGISTER, RSEQ_SIG);
		initial_area = 0;
		__rseq_size = 0;
	}
	rseq_compute_sizes();
	/* Publish the position before registering, since __rseq_register checks
	 * each thread against it. Every thread places the area at this same
	 * distance from the thread pointer, so one offset covers the process.
	 * __rseq_flags stays 0, as in glibc. */
	__rseq_offset = (char *)__rseq_area(self) - (char *)TP_ADJ(self);
	__rseq_register(self);
	if (self->rseq_state == RSEQ_STATE_REGISTERED) {
		__rseq_size = rseq_feature_size;
		initial_area = __rseq_area(self);
	} else {
		/* Clear both together. A nonzero size with a stale offset is the
		 * worst outcome available: readers take the size as proof the area
		 * is live and then dereference the thread pointer plus garbage. */
		__rseq_offset = 0;
		__rseq_size = 0;
	}
}
