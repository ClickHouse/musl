#ifndef RSEQ_H
#define RSEQ_H

#include <stdint.h>
#include <stddef.h>
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
 * in pthread_exit before a detached thread unmaps its own stack.
 *
 * Registration is eager, as in glibc: the main thread registers in
 * __init_tp and every other thread at its clone entry point, before
 * any user code runs. Eager registration is what makes it possible to
 * describe the area to the program with the glibc __rseq_offset /
 * __rseq_size / __rseq_flags ABI below, since the area then sits at a
 * fixed distance from the thread pointer in every thread. */

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

/* glibc-compatible cpu_id sentinels. The kernel only ever stores
 * nonnegative CPU numbers, so external readers of the __rseq_* ABI use
 * these to tell a live area from one whose registration failed. */
#define RSEQ_CPU_ID_UNINITIALIZED ((uint32_t)-1)
#define RSEQ_CPU_ID_REGISTRATION_FAILED ((uint32_t)-2)

#define RSEQ_STATE_UNREGISTERED 0
#define RSEQ_STATE_REGISTERED 1
#define RSEQ_STATE_UNAVAILABLE 2

/* 32-aligned pointer into the over-sized buffer; struct pthread itself
 * only guarantees natural alignment. MIN_TLS_ALIGN is at least 32 (see
 * __init_tls.c), so libc.tls_align is too, and every struct pthread ends
 * up at the same residue modulo 32. The rounding below therefore skips
 * the same number of bytes in every thread, which is what lets
 * __rseq_offset describe all of them at once. */
static inline volatile struct k_rseq *__rseq_area(pthread_t self)
{
	return (volatile struct k_rseq *)
		(((uintptr_t)self->rseq_area_buf + 31) & ~(uintptr_t)31);
}

/* glibc-compatible description of the area, letting a program read cpu_id
 * straight off the thread pointer instead of calling sched_getcpu.
 * __rseq_size is 0 until the main thread registers successfully, and stays
 * 0 on a kernel or sandbox without rseq; it doubles as the flag that keeps
 * pthread_create from attempting a registration that cannot succeed. A
 * thread whose own registration fails after the globals are published
 * carries RSEQ_CPU_ID_REGISTRATION_FAILED in its cpu_id field, as in
 * glibc, so readers never mistake its dead area for a thread on CPU 0. */
extern ptrdiff_t __rseq_offset;
extern unsigned int __rseq_size;
extern unsigned int __rseq_flags;

hidden void __rseq_init(pthread_t self);
hidden void __rseq_register(pthread_t self);

#endif
