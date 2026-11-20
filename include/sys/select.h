#ifndef _SYS_SELECT_H
#define _SYS_SELECT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_size_t
#define __NEED_time_t
#define __NEED_suseconds_t
#define __NEED_struct_timeval
#define __NEED_struct_timespec
#define __NEED_sigset_t

#include <bits/alltypes.h>

#define FD_SETSIZE 1024

typedef unsigned long fd_mask;

typedef struct {
	unsigned long fds_bits[FD_SETSIZE / 8 / sizeof(long)];
} fd_set;

/* The file descriptors visible to the program are a pseudorandom encoding of the
 * kernel descriptors (see src/internal/fdmap.h), so fd_set is a bitmap of kernel
 * descriptors: the macros translate the descriptor, and select passes the sets to the
 * kernel as is. Like without the translation, only kernel descriptors below FD_SETSIZE
 * fit; other descriptors are not added and are never reported as set. */
int __musl_fd_to_kernel(int);

static __inline int __fd_set_index(int __d)
{
	int __k = __musl_fd_to_kernel(__d);
	return (unsigned)__k < FD_SETSIZE ? __k : -1;
}

static __inline void __fd_set_add(fd_set *__s, int __d)
{
	int __k = __fd_set_index(__d);
	if (__k >= 0) __s->fds_bits[__k/(8*sizeof(long))] |= 1UL<<(__k%(8*sizeof(long)));
}

static __inline void __fd_set_remove(fd_set *__s, int __d)
{
	int __k = __fd_set_index(__d);
	if (__k >= 0) __s->fds_bits[__k/(8*sizeof(long))] &= ~(1UL<<(__k%(8*sizeof(long))));
}

static __inline int __fd_set_contains(const fd_set *__s, int __d)
{
	int __k = __fd_set_index(__d);
	return __k >= 0 && !!(__s->fds_bits[__k/(8*sizeof(long))] & (1UL<<(__k%(8*sizeof(long)))));
}

#define FD_ZERO(s) do { int __i; unsigned long *__b=(s)->fds_bits; for(__i=sizeof (fd_set)/sizeof (long); __i; __i--) *__b++=0; } while(0)
#define FD_SET(d, s)   __fd_set_add((s), (d))
#define FD_CLR(d, s)   __fd_set_remove((s), (d))
#define FD_ISSET(d, s) __fd_set_contains((s), (d))

int select (int, fd_set *__restrict, fd_set *__restrict, fd_set *__restrict, struct timeval *__restrict);
int pselect (int, fd_set *__restrict, fd_set *__restrict, fd_set *__restrict, const struct timespec *__restrict, const sigset_t *__restrict);

#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
#define NFDBITS (8*(int)sizeof(long))
#endif

#if _REDIR_TIME64
__REDIR(select, __select_time64);
__REDIR(pselect, __pselect_time64);
#endif

#ifdef __cplusplus
}
#endif
#endif
