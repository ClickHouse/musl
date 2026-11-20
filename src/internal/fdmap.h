#ifndef _INTERNAL_FDMAP_H
#define _INTERNAL_FDMAP_H

/* File descriptor randomization.
 *
 * The kernel allocates the lowest free descriptor, so a descriptor that is used after
 * it was closed usually refers to some unrelated file opened later by another thread,
 * and the data is silently read from or written to a wrong file. To catch such bugs,
 * the descriptors visible to the program are not the kernel ones: every syscall that
 * goes through musl translates them. The program-visible number is a deterministic
 * pseudorandom permutation of the pair (kernel descriptor, generation), where the
 * generation of a kernel descriptor is incremented when it is closed. A descriptor
 * that was closed and then reused by the kernel gets a different program-visible
 * number, and the stale number is rejected with EBADF instead of reaching the new file.
 * Descriptors 0, 1 and 2 are not translated.
 *
 * The translation is done in __syscall, __syscall_cp and the public syscall function.
 * Code that must use kernel numbers (the child of posix_spawn, the translation itself)
 * uses __syscall_raw. The kind of every syscall that involves descriptors is listed
 * below; for a constant syscall number the check folds at compile time, so syscalls
 * without descriptors have no overhead. */

#include <sys/syscall.h>

#ifndef FD_RANDOMIZATION
#define FD_RANDOMIZATION 1
#endif

#define FDMAP_NONE 0
#define FDMAP_FD0 1          /* a is a descriptor */
#define FDMAP_FD0_RET 2      /* a is a descriptor, returns a new descriptor */
#define FDMAP_RET 3          /* returns a new descriptor */
#define FDMAP_FD0_FD1 4      /* a and b are descriptors */
#define FDMAP_FD0_FD2 5      /* a and c are descriptors */
#define FDMAP_AT0 6          /* a is a directory descriptor for the path b */
#define FDMAP_AT0_RET 7      /* same, returns a new descriptor */
#define FDMAP_AT0_AT2 8      /* a and c are directory descriptors for the paths b and d */
#define FDMAP_AT1 9          /* b is a directory descriptor for the path c */
#define FDMAP_STRICT0_RET 10 /* a is a directory descriptor used even for absolute paths */
#define FDMAP_CLOSE 11
#define FDMAP_DUP2 12
#define FDMAP_FCNTL 13
#define FDMAP_PIPE 14
#define FDMAP_SOCKETPAIR 15
#define FDMAP_MMAP 16
#define FDMAP_POLL 17
#define FDMAP_SELECT 18
#define FDMAP_SENDMSG 19
#define FDMAP_RECVMSG 20
#define FDMAP_SENDMMSG 21
#define FDMAP_RECVMMSG 22
#define FDMAP_IOCTL 23
#define FDMAP_PERF_EVENT_OPEN 24
#define FDMAP_IO_URING_ENTER 25
#define FDMAP_IO_URING_REGISTER 26
#define FDMAP_WAITID 27
#define FDMAP_CLOSE_RANGE 28
#define FDMAP_LANDLOCK_CREATE 29
#define FDMAP_LANDLOCK_ADD_RULE 30
#define FDMAP_FSCONFIG 31
#define FDMAP_SECCOMP 32
#define FDMAP_FANOTIFY_MARK 33
#define FDMAP_PIDFD_GETFD 34

static inline __attribute__((always_inline)) int __fdmap_kind(long n)
{
#if FD_RANDOMIZATION
	switch (n) {
	/* Descriptor in the first argument. */
	case SYS_read: case SYS_write: case SYS_readv: case SYS_writev:
	case SYS_pread64: case SYS_pwrite64: case SYS_preadv: case SYS_pwritev:
#ifdef SYS_preadv2
	case SYS_preadv2: case SYS_pwritev2:
#endif
	case SYS_lseek: case SYS_fstatfs: case SYS_ftruncate: case SYS_fallocate:
	case SYS_fsync: case SYS_fdatasync: case SYS_syncfs: case SYS_flock:
#ifdef SYS_fstat
	case SYS_fstat:
#endif
#ifdef SYS_fadvise64
	case SYS_fadvise64:
#endif
#ifdef SYS_sync_file_range
	case SYS_sync_file_range:
#endif
	case SYS_readahead: case SYS_fchmod: case SYS_fchown: case SYS_fchdir:
	case SYS_getdents64:
#ifdef SYS_getdents
	case SYS_getdents:
#endif
	case SYS_fgetxattr: case SYS_fsetxattr: case SYS_flistxattr: case SYS_fremovexattr:
	case SYS_vmsplice:
	case SYS_connect: case SYS_bind: case SYS_listen: case SYS_sendto: case SYS_recvfrom:
	case SYS_shutdown: case SYS_getsockname: case SYS_getpeername:
	case SYS_setsockopt: case SYS_getsockopt:
#ifdef SYS_epoll_wait
	case SYS_epoll_wait:
#endif
	case SYS_epoll_pwait:
#ifdef SYS_epoll_pwait2
	case SYS_epoll_pwait2:
#endif
	case SYS_timerfd_settime: case SYS_timerfd_gettime:
	case SYS_inotify_add_watch: case SYS_inotify_rm_watch:
	case SYS_setns:
	case SYS_mq_timedsend: case SYS_mq_timedreceive: case SYS_mq_notify: case SYS_mq_getsetattr:
#ifdef SYS_pidfd_send_signal
	case SYS_pidfd_send_signal:
#endif
#ifdef SYS_process_madvise
	case SYS_process_madvise:
#endif
#ifdef SYS_process_mrelease
	case SYS_process_mrelease:
#endif
#ifdef SYS_landlock_restrict_self
	case SYS_landlock_restrict_self:
#endif
#ifdef SYS_quotactl_fd
	case SYS_quotactl_fd:
#endif
#ifdef SYS_cachestat
	case SYS_cachestat:
#endif
		return FDMAP_FD0;

	case SYS_accept4:
#ifdef SYS_accept
	case SYS_accept:
#endif
	case SYS_dup:
	case SYS_signalfd4:
#ifdef SYS_signalfd
	case SYS_signalfd:
#endif
	case SYS_open_by_handle_at:
#ifdef SYS_fsmount
	case SYS_fsmount:
#endif
		return FDMAP_FD0_RET;

	case SYS_socket: case SYS_eventfd2: case SYS_epoll_create1: case SYS_timerfd_create:
	case SYS_inotify_init1: case SYS_fanotify_init: case SYS_memfd_create:
	case SYS_userfaultfd: case SYS_mq_open:
#ifdef SYS_open
	case SYS_open:
#endif
#ifdef SYS_creat
	case SYS_creat:
#endif
#ifdef SYS_eventfd
	case SYS_eventfd:
#endif
#ifdef SYS_epoll_create
	case SYS_epoll_create:
#endif
#ifdef SYS_inotify_init
	case SYS_inotify_init:
#endif
#ifdef SYS_io_uring_setup
	case SYS_io_uring_setup:
#endif
#ifdef SYS_pidfd_open
	case SYS_pidfd_open:
#endif
#ifdef SYS_fsopen
	case SYS_fsopen:
#endif
#ifdef SYS_memfd_secret
	case SYS_memfd_secret:
#endif
		return FDMAP_RET;

#ifdef SYS_sendfile
	case SYS_sendfile:
#endif
	case SYS_tee:
		return FDMAP_FD0_FD1;

	case SYS_splice: case SYS_copy_file_range: case SYS_epoll_ctl:
		return FDMAP_FD0_FD2;

	case SYS_mkdirat: case SYS_mknodat: case SYS_unlinkat: case SYS_readlinkat:
	case SYS_fchownat: case SYS_fchmodat: case SYS_faccessat: case SYS_utimensat:
	case SYS_name_to_handle_at: case SYS_execveat: case SYS_statx:
#ifdef SYS_newfstatat
	case SYS_newfstatat:
#endif
#ifdef SYS_fstatat
	case SYS_fstatat:
#endif
#ifdef SYS_futimesat
	case SYS_futimesat:
#endif
#ifdef SYS_faccessat2
	case SYS_faccessat2:
#endif
#ifdef SYS_fchmodat2
	case SYS_fchmodat2:
#endif
#ifdef SYS_mount_setattr
	case SYS_mount_setattr:
#endif
#ifdef SYS_setxattrat
	case SYS_setxattrat: case SYS_getxattrat: case SYS_listxattrat: case SYS_removexattrat:
#endif
#ifdef SYS_file_getattr
	case SYS_file_getattr: case SYS_file_setattr:
#endif
		return FDMAP_AT0;

	case SYS_openat:
#ifdef SYS_open_tree
	case SYS_open_tree: case SYS_fspick:
#endif
#ifdef SYS_open_tree_attr
	case SYS_open_tree_attr:
#endif
		return FDMAP_AT0_RET;

	case SYS_renameat2: case SYS_linkat:
#ifdef SYS_renameat
	case SYS_renameat:
#endif
#ifdef SYS_move_mount
	case SYS_move_mount:
#endif
		return FDMAP_AT0_AT2;

	case SYS_symlinkat:
		return FDMAP_AT1;

#ifdef SYS_openat2
	case SYS_openat2:
		return FDMAP_STRICT0_RET;
#endif

	case SYS_close: return FDMAP_CLOSE;
#ifdef SYS_dup2
	case SYS_dup2:
#endif
	case SYS_dup3: return FDMAP_DUP2;
	case SYS_fcntl: return FDMAP_FCNTL;
#ifdef SYS_pipe
	case SYS_pipe:
#endif
	case SYS_pipe2: return FDMAP_PIPE;
	case SYS_socketpair: return FDMAP_SOCKETPAIR;
	case SYS_mmap: return FDMAP_MMAP;
#ifdef SYS_poll
	case SYS_poll:
#endif
	case SYS_ppoll: return FDMAP_POLL;
#ifdef SYS_select
	case SYS_select:
#endif
	case SYS_pselect6: return FDMAP_SELECT;
	case SYS_sendmsg: return FDMAP_SENDMSG;
	case SYS_recvmsg: return FDMAP_RECVMSG;
	case SYS_sendmmsg: return FDMAP_SENDMMSG;
	case SYS_recvmmsg: return FDMAP_RECVMMSG;
	case SYS_ioctl: return FDMAP_IOCTL;
	case SYS_perf_event_open: return FDMAP_PERF_EVENT_OPEN;
#ifdef SYS_io_uring_enter
	case SYS_io_uring_enter: return FDMAP_IO_URING_ENTER;
	case SYS_io_uring_register: return FDMAP_IO_URING_REGISTER;
#endif
	case SYS_waitid: return FDMAP_WAITID;
#ifdef SYS_close_range
	case SYS_close_range: return FDMAP_CLOSE_RANGE;
#endif
#ifdef SYS_landlock_create_ruleset
	case SYS_landlock_create_ruleset: return FDMAP_LANDLOCK_CREATE;
	case SYS_landlock_add_rule: return FDMAP_LANDLOCK_ADD_RULE;
#endif
#ifdef SYS_fsconfig
	case SYS_fsconfig: return FDMAP_FSCONFIG;
#endif
	case SYS_seccomp: return FDMAP_SECCOMP;
	case SYS_fanotify_mark: return FDMAP_FANOTIFY_MARK;
#ifdef SYS_pidfd_getfd
	case SYS_pidfd_getfd: return FDMAP_PIDFD_GETFD;
#endif
	}
#endif
	return FDMAP_NONE;
}

/* Performs the syscall n of the given kind with the descriptors translated. If cp is
 * nonzero, the syscall is a cancellation point. */
hidden long __fdmap_syscall(long n, long a, long b, long c, long d, long e, long f, int cp);

/* Translation of a single descriptor: returns the kernel descriptor, or -1 if fd is not
 * a currently valid descriptor. Negative values and 0, 1, 2 are returned unchanged. */
hidden int __fdmap_kernel(int fd);

/* Converts a descriptor just created by the kernel, or a negative error code, to the
 * program-visible descriptor. */
hidden long __fdmap_new_fd(long r);

#endif
