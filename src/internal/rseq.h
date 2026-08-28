#ifndef RSEQ_H
#define RSEQ_H

#include <stdint.h>
#include <stddef.h>
#include "pthread_impl.h"

/* Restartable sequences (kernel >= 4.18). The libc itself only reads
 * the kernel-maintained cpu_id field, to make sched_getcpu a memory
 * read instead of a syscall (aarch64 has no vDSO getcpu). The area is
 * also the process-wide rseq ABI: external consumers (e.g. librseq)
 * discover it through __rseq_offset / __rseq_size / __rseq_flags and
 * run rseq critical sections against it, which is why registration
 * uses the standard per-arch abort signature below - the kernel
 * delivers SIGSEGV on an abort whose signature differs from the
 * registered one.
 *
 * Sizing follows the extensible ABI (kernel >= 6.3). The auxiliary
 * vector describes the kernel's fields: AT_RSEQ_FEATURE_SIZE is the
 * size of the maintained field area (20 before extensions, 28 with
 * node_id/mm_cid) and AT_RSEQ_ALIGN the required allocation alignment
 * (the feature size rounded up to a power of two). While the features
 * fit in the original 32 bytes we register with length 32, which every
 * kernel accepts and for which the kernel still maintains every field
 * that fits - node_id and mm_cid included. Once the feature size
 * outgrows 32 bytes, registration grows with it up to RSEQ_AREA_CAP;
 * past the capacity (or past RSEQ_AREA_ALIGN alignment) it falls back
 * to the original 32-byte ABI. The area must remain valid for the
 * lifetime of the thread's registration; it lives inside struct
 * pthread (rseq_area_buf), and is unregistered in pthread_exit before
 * a detached thread unmaps its own stack.
 *
 * Registration is eager, as in glibc: the main thread registers in
 * __init_tp and every other thread at its clone entry point, before
 * any user code runs. Eager registration is what makes it possible to
 * describe the area to the program with the glibc __rseq_offset /
 * __rseq_size / __rseq_flags ABI below, since the area then sits at a
 * fixed distance from the thread pointer in every thread. */

/* The original 32-byte layout. The libc only touches these fields;
 * kernel-maintained extension fields (node_id, mm_cid, ...) live in
 * the same allocation past this struct's tail padding and are read by
 * external consumers through their own headers. */
struct k_rseq {
	uint32_t cpu_id_start;
	uint32_t cpu_id;
	uint64_t rseq_cs;
	uint32_t flags;
	uint32_t pad[3];
};

#define RSEQ_ORIG_SIZE 32
#define RSEQ_ORIG_FEATURE_SIZE 20

/* Capacity and alignment of the in-pthread allocation, giving headroom
 * for kernel feature growth without changing struct pthread again. The
 * kernel requires extended registrations to be aligned to AT_RSEQ_ALIGN,
 * so MIN_TLS_ALIGN (see __init_tls.c) must be >= RSEQ_AREA_ALIGN and
 * rseq_area_buf must hold RSEQ_AREA_CAP + RSEQ_AREA_ALIGN - 1 bytes. */
#define RSEQ_AREA_CAP 64
#define RSEQ_AREA_ALIGN 64

#ifndef AT_RSEQ_FEATURE_SIZE
#define AT_RSEQ_FEATURE_SIZE 27
#endif
#ifndef AT_RSEQ_ALIGN
#define AT_RSEQ_ALIGN 28
#endif

/* Per-arch abort signature, matching glibc and librseq, so rseq critical
 * sections assembled against their headers can run on an area we registered.
 * On aarch64 the value doubles as a trapping instruction (BRK #0x45E0)
 * placed right before each abort handler. */
#if defined(__x86_64__)
#define RSEQ_SIG 0x53053053
#elif defined(__aarch64__)
#define RSEQ_SIG 0xd428bc00
#else
#error "no rseq abort signature defined for this architecture"
#endif

#define RSEQ_FLAG_UNREGISTER 1

/* glibc-compatible cpu_id sentinels. The kernel only ever stores
 * nonnegative CPU numbers, so external readers of the __rseq_* ABI use
 * these to tell a live area from one whose registration failed. */
#define RSEQ_CPU_ID_UNINITIALIZED ((uint32_t)-1)
#define RSEQ_CPU_ID_REGISTRATION_FAILED ((uint32_t)-2)

#define RSEQ_STATE_UNREGISTERED 0
#define RSEQ_STATE_REGISTERED 1
#define RSEQ_STATE_UNAVAILABLE 2

/* RSEQ_AREA_ALIGN-aligned pointer into the over-sized buffer; struct
 * pthread itself only guarantees natural alignment. MIN_TLS_ALIGN is at
 * least RSEQ_AREA_ALIGN (see __init_tls.c), so libc.tls_align is too, and
 * every struct pthread ends up at the same residue modulo RSEQ_AREA_ALIGN.
 * The rounding below therefore skips the same number of bytes in every
 * thread, which is what lets __rseq_offset describe all of them at once. */
static inline volatile struct k_rseq *__rseq_area(pthread_t self)
{
	return (volatile struct k_rseq *)
		(((uintptr_t)self->rseq_area_buf + (RSEQ_AREA_ALIGN-1))
			& ~(uintptr_t)(RSEQ_AREA_ALIGN-1));
}

/* glibc-compatible description of the area, letting a program read cpu_id
 * straight off the thread pointer instead of calling sched_getcpu.
 * __rseq_size carries the active feature size - how many bytes of fields
 * the kernel maintains - as in glibc >= 2.41, not the registered length
 * (librseq reads it verbatim to decide feature availability, e.g. mm_cid
 * needs >= 28). It is 0 until the main thread registers successfully, and
 * stays 0 on a kernel or sandbox without rseq; it doubles as the flag that
 * keeps pthread_create from attempting a registration that cannot succeed.
 * A thread whose own registration fails after the globals are published
 * carries RSEQ_CPU_ID_REGISTRATION_FAILED in its cpu_id field, as in
 * glibc, so readers never mistake its dead area for a thread on CPU 0. */
extern ptrdiff_t __rseq_offset;
extern unsigned int __rseq_size;
extern unsigned int __rseq_flags;

/* Length passed to the registration syscall, computed from the auxiliary
 * vector in __rseq_init. Unregistration must pass the same length - the
 * kernel rejects a mismatch. */
hidden extern unsigned int __rseq_alloc_len;

hidden void __rseq_init(pthread_t self);
hidden void __rseq_register(pthread_t self);

#endif
