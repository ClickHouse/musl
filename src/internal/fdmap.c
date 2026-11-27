#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include "syscall.h"
#include "pthread_impl.h"
#include "fdmap.h"

/* See fdmap.h for the overview.
 *
 * A program-visible descriptor u >= 3 is permute((generation << FD_BITS) | kernel_fd),
 * where permute is a bijection of the 31-bit integers, restricted to [3, 2^31) by cycle
 * walking. The generation of every kernel descriptor is stored in a table and is
 * incremented by close, so a stale u does not decode into the current generation.
 *
 * Kernel descriptors must be below 2^FD_BITS (a million); a syscall that would create a
 * larger one fails with EMFILE. The generation has 31 - FD_BITS = 11 bits, so a stale
 * descriptor is not detected if its kernel descriptor was closed a multiple of 2048 times
 * since then.
 *
 * The child of vfork and posix_spawn shares the memory with the parent, so it must not
 * change the table: the descriptors it closes remain open in the parent. vfork and
 * posix_spawn set fdmap_vfork_child in the thread descriptor, which they share with the
 * suspended parent thread, and reset it in the parent. */

#if FD_RANDOMIZATION

#define FD_BITS 20
#define NUM_KERNEL_FDS (1u << FD_BITS)
#define GEN_MASK ((1u << (31 - FD_BITS)) - 1)
#define MASK31 0x7fffffffu

/* Constants that the headers of this libc do not provide. */
#define FDMAP_F_DUPFD_QUERY 1027
#define FDMAP_FICLONE 0x40049409                     /* _IOW(0x94, 9, int) */
#define FDMAP_FICLONERANGE 0x4020940d                /* _IOW(0x94, 13, struct file_clone_range) */
#define FDMAP_PERF_EVENT_IOC_SET_OUTPUT 0x2405       /* _IO('$', 5) */
#define FDMAP_PERF_FLAG_PID_CGROUP (1ul << 2)
#define FDMAP_IORING_ENTER_REGISTERED_RING (1u << 4)
#define FDMAP_IORING_REGISTER_USE_REGISTERED_RING (1u << 31)
#define FDMAP_IORING_REGISTER_FILES 2
#define FDMAP_IORING_REGISTER_EVENTFD 4
#define FDMAP_IORING_REGISTER_FILES_UPDATE 6
#define FDMAP_IORING_REGISTER_EVENTFD_ASYNC 7
#define FDMAP_IORING_REGISTER_FILES2 13
#define FDMAP_IORING_REGISTER_FILES_UPDATE2 14
#define FDMAP_IORING_REGISTER_RING_FDS 20
#define FDMAP_P_PIDFD 3
#define FDMAP_CLOSE_RANGE_CLOEXEC (1u << 2)
#define FDMAP_LANDLOCK_RULE_PATH_BENEATH 1
#define FDMAP_FSCONFIG_SET_PATH 3
#define FDMAP_FSCONFIG_SET_PATH_EMPTY 4
#define FDMAP_FSCONFIG_SET_FD 5
#define FDMAP_SECCOMP_SET_MODE_FILTER 1
#define FDMAP_SECCOMP_FILTER_FLAG_NEW_LISTENER (1ul << 3)

hidden long __syscall_cp_c(syscall_arg_t, syscall_arg_t, syscall_arg_t, syscall_arg_t,
                           syscall_arg_t, syscall_arg_t, syscall_arg_t);

static volatile unsigned short generations[NUM_KERNEL_FDS];

/* Every step is invertible modulo 2^31: multiplication by an odd constant and xorshift. */
static unsigned permute(unsigned x)
{
	x = (x * 0x2c1b3c6du) & MASK31;
	x ^= x >> 16;
	x = (x * 0x297a2d39u) & MASK31;
	x ^= x >> 13;
	x = (x * 0x1b873593u) & MASK31;
	x ^= x >> 16;
	return x;
}

static unsigned unpermute(unsigned x)
{
	x ^= x >> 16;
	x = (x * 0x56ed309bu) & MASK31;
	x ^= (x >> 13) ^ (x >> 26);
	x = (x * 0x0cf0b109u) & MASK31;
	x ^= x >> 16;
	x = (x * 0x64ea2d65u) & MASK31;
	return x;
}

static int encode(unsigned kfd, unsigned gen)
{
	unsigned x = (gen << FD_BITS) | kfd;
	do x = permute(x); while (x < 3);
	return x;
}

/* Returns the kernel descriptor (below 3 if u is not an encoding of any) and its generation. */
static int decode(int u, unsigned *gen)
{
	unsigned x = u;
	do x = unpermute(x); while (x < 3);
	*gen = x >> FD_BITS;
	return x & (NUM_KERNEL_FDS - 1);
}

static unsigned generation(int kfd)
{
	return __atomic_load_n(&generations[kfd], __ATOMIC_RELAXED);
}

static int in_vfork_child(void)
{
	return __pthread_self()->fdmap_vfork_child;
}

hidden int __fdmap_kernel(int fd)
{
	unsigned gen;
	int kfd;
	if (fd < 3) return fd;
	kfd = decode(fd, &gen);
	if (kfd < 3 || generation(kfd) != gen) return -1;
	return kfd;
}

static long raw(long n, long a, long b, long c, long d, long e, long f, int cp)
{
	if (cp) return __syscall_cp_c(n, a, b, c, d, e, f);
	return __syscall_raw(n, a, b, c, d, e, f);
}

/* Converts a descriptor just created by the kernel (or an error) to the program-visible one. */
hidden long __fdmap_new_fd(long r)
{
	if (r < 3) return r;
	if (r >= NUM_KERNEL_FDS) {
		__syscall_raw(SYS_close, r);
		return -EMFILE;
	}
	return encode(r, generation(r));
}

/* Translates a descriptor argument in place. */
static int arg(long *fd)
{
	int u = *fd, kfd;
	if (u < 3) return 0;
	kfd = __fdmap_kernel(u);
	if (kfd < 0) return -EBADF;
	*fd = kfd;
	return 0;
}

/* Translates a directory descriptor argument for the path. */
static int at_arg(long *fd, long path)
{
	int u = *fd, kfd;
	if (u < 3) return 0;
	kfd = __fdmap_kernel(u);
	if (kfd >= 0) {
		*fd = kfd;
		return 0;
	}
	/* The kernel ignores the directory descriptor of an absolute path. */
	if (path && *(const char *)path == '/') {
		*fd = AT_FDCWD;
		return 0;
	}
	return -EBADF;
}

#define ARG(x) do { int _r = arg(&(x)); if (_r) return _r; } while (0)
#define AT_ARG(x, p) do { int _r = at_arg(&(x), (p)); if (_r) return _r; } while (0)

/* Storage for the original values of descriptors that are translated in place in the
 * caller's memory and restored after the syscall. Large arrays go to an anonymous mapping:
 * the translation must work in signal handlers and must not depend on malloc. */
struct saved {
	int *fds;
	size_t size;
	int local[128];
};

static int saved_init(struct saved *s, size_t n)
{
	s->fds = s->local;
	s->size = 0;
	if (n <= sizeof s->local / sizeof s->local[0]) return 0;
	long p = __syscall_raw(SYS_mmap, 0, n * sizeof(int), PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if ((unsigned long)p > -4096UL) return p;
	s->fds = (int *)p;
	s->size = n * sizeof(int);
	return 0;
}

static void saved_free(struct saved *s)
{
	if (s->size) __syscall_raw(SYS_munmap, s->fds, s->size);
}

/* Translates n descriptors at fds in place, saving the originals to save. Negative
 * descriptors (sparse entries of io_uring tables) are kept. On failure, restores. */
static int translate_array(int *fds, size_t n, int *save)
{
	size_t i;
	for (i = 0; i < n; i++) {
		int kfd = __fdmap_kernel(save[i] = fds[i]);
		if (fds[i] >= 3 && kfd < 0) {
			while (i--) fds[i] = save[i];
			return -EBADF;
		}
		fds[i] = kfd;
	}
	return 0;
}

static void restore_array(int *fds, size_t n, const int *save)
{
	size_t i;
	for (i = 0; i < n; i++) fds[i] = save[i];
}

static long translated_array_syscall(int *fds, size_t n, long nr, long a, long b, long c, long d, long e, long f)
{
	struct saved s;
	long r = saved_init(&s, n);
	if (r) return r;
	r = translate_array(fds, n, s.fds);
	if (!r) {
		r = __syscall_raw(nr, a, b, c, d, e, f);
		restore_array(fds, n, s.fds);
	}
	saved_free(&s);
	return r;
}

static long do_close(long a, int cp)
{
	int u = a, kfd;
	unsigned short gen;
	unsigned g;
	if (u < 3) return raw(SYS_close, a, 0, 0, 0, 0, 0, cp);
	kfd = decode(u, &g);
	if (kfd < 3) return -EBADF;
	gen = g;
	if (in_vfork_child()) {
		if (generation(kfd) != gen) return -EBADF;
	} else {
		/* Only one of concurrent closes of the same descriptor reaches the kernel: the
		 * others would close an unrelated descriptor that reused the number. */
		if (!__atomic_compare_exchange_n(&generations[kfd], &gen, (gen + 1) & GEN_MASK,
		                                 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
			return -EBADF;
	}
	return raw(SYS_close, kfd, 0, 0, 0, 0, 0, cp);
}

/* dup2 and dup3. The target is any number chosen by the program: it is the encoding of
 * some kernel descriptor and generation, and the kernel descriptor is made a copy of old
 * with that generation. */
static long do_dup2(long n, long a, long b, long c)
{
	int target = b, kfd = b;
	unsigned gen = 0;
	long r;
	ARG(a);
	if (target >= 3) {
		kfd = decode(target, &gen);
		if (kfd < 3) return -EBADF;
		/* old and target would be the same kernel descriptor with different generations. */
		if (kfd == a && generation(kfd) != gen) return -EMFILE;
	}
	r = __syscall_raw(n, a, kfd, c);
	if (r < 0) return r;
	if (target >= 3 && !in_vfork_child())
		__atomic_store_n(&generations[kfd], gen, __ATOMIC_RELAXED);
	return target;
}

static long do_fcntl(long a, long b, long c, int cp)
{
	ARG(a);
	if (b == F_DUPFD || b == F_DUPFD_CLOEXEC) return __fdmap_new_fd(raw(SYS_fcntl, a, b, c, 0, 0, 0, cp));
	if (b == FDMAP_F_DUPFD_QUERY) ARG(c);
	return raw(SYS_fcntl, a, b, c, 0, 0, 0, cp);
}

static long new_fd_pair(long r, int *fds)
{
	long r0, r1;
	if (r) return r;
	r0 = __fdmap_new_fd(fds[0]);
	r1 = __fdmap_new_fd(fds[1]);
	if (r0 < 0 || r1 < 0) {
		if (r0 >= 0) __syscall_raw(SYS_close, fds[0]);
		if (r1 >= 0) __syscall_raw(SYS_close, fds[1]);
		return -EMFILE;
	}
	fds[0] = r0;
	fds[1] = r1;
	return 0;
}

static long do_poll(long n, long a, long b, long c, long d, long e, int cp)
{
	struct pollfd *fds = (struct pollfd *)a;
	size_t nfds = b, i, invalid = 0;
	struct timespec zero = {0};
	struct saved s;
	long r = saved_init(&s, nfds);
	if (r) return r;
	for (i = 0; i < nfds; i++) {
		int u = s.fds[i] = fds[i].fd;
		if (u >= 3) {
			int kfd = __fdmap_kernel(u);
			/* The kernel ignores negative descriptors; the entry is reported as POLLNVAL below. */
			if (kfd < 0) invalid++;
			fds[i].fd = kfd;
		}
	}
	/* The kernel returns immediately when some descriptor is not open. */
	if (invalid) c = n == SYS_ppoll ? (long)&zero : 0;
	r = raw(n, a, b, c, d, e, 0, cp);
	for (i = 0; i < nfds; i++) {
		if (s.fds[i] >= 3 && fds[i].fd < 0) fds[i].revents = POLLNVAL;
		fds[i].fd = s.fds[i];
	}
	saved_free(&s);
	if (r >= 0) r += invalid;
	return r;
}

/* fd_set is a bitmap of kernel descriptors (see sys/select.h): the sets are passed as is.
 * The number of descriptors given by the program is the largest program-visible
 * descriptor plus one, which says nothing about the kernel ones. */
static long do_select(long n, long a, long b, long c, long d, long e, long f, int cp)
{
	if (a > 3) a = FD_SETSIZE;
	return raw(n, a, b, c, d, e, f, cp);
}

static size_t cmsg_count_fds(struct msghdr *m)
{
	struct cmsghdr *cm;
	size_t n = 0;
	if (!m->msg_control) return 0;
	for (cm = CMSG_FIRSTHDR(m); cm; cm = CMSG_NXTHDR(m, cm))
		if (cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SCM_RIGHTS)
			n += (cm->cmsg_len - CMSG_LEN(0)) / sizeof(int);
	return n;
}

/* Calls fn on every array of descriptors in SCM_RIGHTS messages. */
static long cmsg_for_each(struct msghdr *m, long (*fn)(int *, size_t, void *), void *arg)
{
	struct cmsghdr *cm;
	long r;
	if (!m->msg_control) return 0;
	for (cm = CMSG_FIRSTHDR(m); cm; cm = CMSG_NXTHDR(m, cm))
		if (cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SCM_RIGHTS)
			if ((r = fn((int *)CMSG_DATA(cm), (cm->cmsg_len - CMSG_LEN(0)) / sizeof(int), arg)))
				return r;
	return 0;
}

struct cmsg_cursor {
	int *save;
	size_t pos, limit;
};

static long cmsg_translate(int *fds, size_t n, void *arg)
{
	struct cmsg_cursor *c = arg;
	long r = translate_array(fds, n, c->save + c->pos);
	if (r) return r;
	c->pos += n;
	return 0;
}

static long cmsg_restore(int *fds, size_t n, void *arg)
{
	struct cmsg_cursor *c = arg;
	if (c->pos >= c->limit) return 1;
	restore_array(fds, n, c->save + c->pos);
	c->pos += n;
	return 0;
}

static long cmsg_received(int *fds, size_t n, void *arg)
{
	size_t i;
	for (i = 0; i < n; i++) {
		long r = __fdmap_new_fd(fds[i]);
		fds[i] = r < 0 ? -1 : r;
	}
	return 0;
}

/* Calls fn on the descriptors of every message of sendmsg (one msghdr) or sendmmsg. */
static long msgs_for_each(long n, long b, size_t count, long (*fn)(int *, size_t, void *), void *arg)
{
	size_t i;
	long r;
	if (n == SYS_sendmsg) return cmsg_for_each((struct msghdr *)b, fn, arg);
	for (i = 0; i < count; i++)
		if ((r = cmsg_for_each(&((struct mmsghdr *)b)[i].msg_hdr, fn, arg))) return r;
	return 0;
}

static long msgs_count_fds(long n, long b, size_t count)
{
	size_t i, total = 0;
	if (n == SYS_sendmsg) return cmsg_count_fds((struct msghdr *)b);
	for (i = 0; i < count; i++) total += cmsg_count_fds(&((struct mmsghdr *)b)[i].msg_hdr);
	return total;
}

/* sendmsg and sendmmsg: the descriptors in SCM_RIGHTS are translated in place. */
static long do_sendmsg(long n, long a, long b, long c, long d, int cp)
{
	size_t total;
	struct saved s;
	struct cmsg_cursor cur;
	long r;
	ARG(a);
	total = msgs_count_fds(n, b, c);
	if (!total) return raw(n, a, b, c, d, 0, 0, cp);
	if ((r = saved_init(&s, total))) return r;
	cur.save = s.fds;
	cur.pos = 0;
	r = msgs_for_each(n, b, c, cmsg_translate, &cur);
	if (!r) r = raw(n, a, b, c, d, 0, 0, cp);
	/* On a failure, translate_array has restored the array it failed on. */
	cur.limit = cur.pos;
	cur.pos = 0;
	msgs_for_each(n, b, c, cmsg_restore, &cur);
	saved_free(&s);
	return r;
}

static long do_ioctl(long a, long b, long c)
{
	ARG(a);
	if (b == FDMAP_FICLONE || b == FDMAP_PERF_EVENT_IOC_SET_OUTPUT) ARG(c);
	if (b == FDMAP_FICLONERANGE) {
		struct { long long src_fd; unsigned long long src_offset, src_length, dest_offset; } range;
		long src;
		memcpy(&range, (void *)c, sizeof range);
		src = range.src_fd;
		ARG(src);
		range.src_fd = src;
		return __syscall_raw(SYS_ioctl, a, b, &range);
	}
	return __syscall_raw(SYS_ioctl, a, b, c);
}

static long do_io_uring_register(long a, long b, long c, long d)
{
	unsigned op = b;
	if (!(op & FDMAP_IORING_REGISTER_USE_REGISTERED_RING)) ARG(a);
	switch (op & ~FDMAP_IORING_REGISTER_USE_REGISTERED_RING) {
	case FDMAP_IORING_REGISTER_FILES:
		return translated_array_syscall((int *)c, d, SYS_io_uring_register, a, b, c, d, 0, 0);
	case FDMAP_IORING_REGISTER_EVENTFD:
	case FDMAP_IORING_REGISTER_EVENTFD_ASYNC:
		return translated_array_syscall((int *)c, 1, SYS_io_uring_register, a, b, c, d, 0, 0);
	case FDMAP_IORING_REGISTER_FILES_UPDATE: {
		struct { unsigned offset, resv; unsigned long long fds; } *u = (void *)c;
		return translated_array_syscall((int *)(unsigned long)u->fds, d, SYS_io_uring_register, a, b, c, d, 0, 0);
	}
	case FDMAP_IORING_REGISTER_FILES2: {
		struct { unsigned nr, flags; unsigned long long resv2, data, tags; } *rr = (void *)c;
		return translated_array_syscall((int *)(unsigned long)rr->data, rr->nr, SYS_io_uring_register, a, b, c, d, 0, 0);
	}
	case FDMAP_IORING_REGISTER_FILES_UPDATE2: {
		struct { unsigned offset, resv; unsigned long long data, tags; unsigned nr, resv2; } *u = (void *)c;
		return translated_array_syscall((int *)(unsigned long)u->data, u->nr, SYS_io_uring_register, a, b, c, d, 0, 0);
	}
	case FDMAP_IORING_REGISTER_RING_FDS: {
		/* An array of struct io_uring_rsrc_update with the ring descriptor in data. */
		struct upd { unsigned offset, resv; unsigned long long data; } *u = (void *)c, copy[16];
		size_t i, n = d;
		long r;
		if (n > sizeof copy / sizeof copy[0]) return -EINVAL; /* IO_RINGFD_REG_MAX */
		for (i = 0; i < n; i++) {
			long fd = (int)u[i].data;
			copy[i] = u[i];
			ARG(fd);
			copy[i].data = fd;
		}
		r = __syscall_raw(SYS_io_uring_register, a, b, copy, d);
		/* The kernel writes the registered indexes back. */
		for (i = 0; i < n; i++) u[i].offset = copy[i].offset;
		return r;
	}
	}
	return __syscall_raw(SYS_io_uring_register, a, b, c, d);
}

static long do_close_range(long a, long b, long c)
{
	unsigned first = a, last = b;
	int kfd;
	if (last < 3) return __syscall_raw(SYS_close_range, a, b, c);
	/* The program-visible descriptors >= 3 are exactly the kernel ones >= 3. */
	if (first <= 3 && last >= MASK31) return __syscall_raw(SYS_close_range, a, b, c);
	if (first != last) return -EINVAL;
	/* A single descriptor. */
	if ((int)first < 0 || (kfd = __fdmap_kernel(first)) < 0) return 0;
	if (c & FDMAP_CLOSE_RANGE_CLOEXEC) return __syscall_raw(SYS_close_range, kfd, kfd, c);
	return do_close(first, 0);
}

hidden long __fdmap_syscall(long n, long a, long b, long c, long d, long e, long f, int cp)
{
	switch (__fdmap_kind(n)) {
	case FDMAP_FD0:
		ARG(a);
		break;
	case FDMAP_FD0_RET:
		ARG(a);
		return __fdmap_new_fd(raw(n, a, b, c, d, e, f, cp));
	case FDMAP_RET:
		return __fdmap_new_fd(raw(n, a, b, c, d, e, f, cp));
	case FDMAP_FD0_FD1:
		ARG(a);
		ARG(b);
		break;
	case FDMAP_FD0_FD2:
		ARG(a);
		ARG(c);
		break;
	case FDMAP_AT0:
		AT_ARG(a, b);
		break;
	case FDMAP_AT0_RET:
		AT_ARG(a, b);
		return __fdmap_new_fd(raw(n, a, b, c, d, e, f, cp));
	case FDMAP_AT0_AT2:
		AT_ARG(a, b);
		AT_ARG(c, d);
		break;
	case FDMAP_AT1:
		AT_ARG(b, c);
		break;
	case FDMAP_STRICT0_RET:
		ARG(a);
		return __fdmap_new_fd(raw(n, a, b, c, d, e, f, cp));
	case FDMAP_CLOSE:
		return do_close(a, cp);
	case FDMAP_DUP2:
		return do_dup2(n, a, b, c);
	case FDMAP_FCNTL:
		return do_fcntl(a, b, c, cp);
	case FDMAP_PIPE:
		return new_fd_pair(raw(n, a, b, c, d, e, f, cp), (int *)a);
	case FDMAP_SOCKETPAIR:
		return new_fd_pair(raw(n, a, b, c, d, e, f, cp), (int *)d);
	case FDMAP_MMAP:
		if (!(d & MAP_ANONYMOUS)) ARG(e);
		break;
	case FDMAP_POLL:
		return do_poll(n, a, b, c, d, e, cp);
	case FDMAP_SELECT:
		return do_select(n, a, b, c, d, e, f, cp);
	case FDMAP_SENDMSG:
	case FDMAP_SENDMMSG:
		return do_sendmsg(n, a, b, c, d, cp);
	case FDMAP_RECVMSG: {
		long r;
		ARG(a);
		r = raw(n, a, b, c, d, e, f, cp);
		if (r >= 0) cmsg_for_each((struct msghdr *)b, cmsg_received, 0);
		return r;
	}
	case FDMAP_RECVMMSG: {
		long r, i;
		ARG(a);
		r = raw(n, a, b, c, d, e, f, cp);
		for (i = 0; i < r; i++) cmsg_for_each(&((struct mmsghdr *)b)[i].msg_hdr, cmsg_received, 0);
		return r;
	}
	case FDMAP_IOCTL:
		return do_ioctl(a, b, c);
	case FDMAP_PERF_EVENT_OPEN:
		ARG(d);
		if (e & FDMAP_PERF_FLAG_PID_CGROUP) ARG(b);
		return __fdmap_new_fd(raw(n, a, b, c, d, e, f, cp));
#ifdef SYS_io_uring_enter
	case FDMAP_IO_URING_ENTER:
		if (!(d & FDMAP_IORING_ENTER_REGISTERED_RING)) ARG(a);
		break;
	case FDMAP_IO_URING_REGISTER:
		return do_io_uring_register(a, b, c, d);
#endif
	case FDMAP_WAITID:
		if (a == FDMAP_P_PIDFD) ARG(b);
		break;
#ifdef SYS_close_range
	case FDMAP_CLOSE_RANGE:
		return do_close_range(a, b, c);
#endif
	case FDMAP_LANDLOCK_CREATE: {
		/* With flags, returns the ABI version or errata instead of a descriptor. */
		long r = raw(n, a, b, c, d, e, f, cp);
		return c ? r : __fdmap_new_fd(r);
	}
#ifdef SYS_landlock_add_rule
	case FDMAP_LANDLOCK_ADD_RULE:
		ARG(a);
		if (b == FDMAP_LANDLOCK_RULE_PATH_BENEATH) {
			struct { unsigned long long allowed_access; int parent_fd; } __attribute__((packed)) attr;
			long parent;
			memcpy(&attr, (void *)c, sizeof attr);
			parent = attr.parent_fd;
			ARG(parent);
			attr.parent_fd = parent;
			return __syscall_raw(n, a, b, &attr, d);
		}
		break;
#endif
	case FDMAP_FSCONFIG:
		ARG(a);
		if (b == FDMAP_FSCONFIG_SET_FD) ARG(e);
		else if (b == FDMAP_FSCONFIG_SET_PATH || b == FDMAP_FSCONFIG_SET_PATH_EMPTY) AT_ARG(e, d);
		break;
	case FDMAP_SECCOMP: {
		long r = raw(n, a, b, c, d, e, f, cp);
		if (a == FDMAP_SECCOMP_SET_MODE_FILTER && (b & FDMAP_SECCOMP_FILTER_FLAG_NEW_LISTENER))
			return __fdmap_new_fd(r);
		return r;
	}
	case FDMAP_FANOTIFY_MARK:
		ARG(a);
		AT_ARG(d, e);
		break;
	case FDMAP_PIDFD_GETFD:
		/* The second argument is a descriptor of the other process. */
		ARG(a);
		return __fdmap_new_fd(raw(n, a, b, c, d, e, f, cp));
	}
	return raw(n, a, b, c, d, e, f, cp);
}

int __musl_fd_to_kernel(int fd)
{
	return __fdmap_kernel(fd);
}

int __musl_fd_from_kernel(int kfd)
{
	if (kfd < 3) return kfd;
	if (kfd >= NUM_KERNEL_FDS) {
		errno = EBADF;
		return -1;
	}
	return encode(kfd, generation(kfd));
}

hidden long __fdmap_vfork_ret(unsigned long r)
{
	__pthread_self()->fdmap_vfork_child = !r;
	return __syscall_ret(r);
}

#else

hidden long __syscall_cp_c(syscall_arg_t, syscall_arg_t, syscall_arg_t, syscall_arg_t,
                           syscall_arg_t, syscall_arg_t, syscall_arg_t);

hidden int __fdmap_kernel(int fd)
{
	return fd;
}

hidden long __fdmap_new_fd(long r)
{
	return r;
}

hidden long __fdmap_syscall(long n, long a, long b, long c, long d, long e, long f, int cp)
{
	if (cp) return __syscall_cp_c(n, a, b, c, d, e, f);
	return __syscall_raw(n, a, b, c, d, e, f);
}

int __musl_fd_to_kernel(int fd)
{
	return fd;
}

int __musl_fd_from_kernel(int kfd)
{
	return kfd;
}

hidden long __fdmap_vfork_ret(unsigned long r)
{
	return __syscall_ret(r);
}

#endif
