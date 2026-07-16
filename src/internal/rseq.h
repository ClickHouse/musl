#ifndef RSEQ_H
#define RSEQ_H

#include <stdint.h>
#include "pthread_impl.h"

/* Restartable sequences (kernel >= 4.18). Only the kernel-maintained
 * cpu_id field is used, to make sched_getcpu a memory read instead of
 * a syscall (aarch64 has no vDSO getcpu). Critical sections are not
 * used, so rseq_cs stays 0 and the signature value is arbitrary.
 *
 * This is the original 32-byte ABI; newer kernels accept extended
 * sizes but always support 32. The area must be aligned to 32 and
 * remain valid for the lifetime of the thread's registration; it
 * lives inside struct pthread (rseq_area_buf), and is unregistered
 * in pthread_exit before a detached thread unmaps its own stack. */

struct k_rseq {
	uint32_t cpu_id_start;
	uint32_t cpu_id;
	uint64_t rseq_cs;
	uint32_t flags;
	uint32_t pad[3];
};

#define RSEQ_ABI_SIZE 32
#define RSEQ_SIG 0x53053053
#define RSEQ_FLAG_UNREGISTER 1

#define RSEQ_STATE_UNREGISTERED 0
#define RSEQ_STATE_REGISTERED 1
#define RSEQ_STATE_UNAVAILABLE 2

/* 32-aligned pointer into the over-sized buffer; struct pthread itself
 * only guarantees natural alignment. */
static inline volatile struct k_rseq *__rseq_area(pthread_t self)
{
	return (volatile struct k_rseq *)
		(((uintptr_t)self->rseq_area_buf + 31) & ~(uintptr_t)31);
}

#endif
