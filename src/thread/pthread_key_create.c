#include "pthread_impl.h"
#include "fork_impl.h"

volatile size_t __pthread_tsd_size = sizeof(void *) * PTHREAD_KEYS_MAX;
void *__pthread_tsd_main[PTHREAD_KEYS_MAX] = { 0 };

static void (*keys[PTHREAD_KEYS_MAX])(void *);

static pthread_rwlock_t key_lock = PTHREAD_RWLOCK_INITIALIZER;

static pthread_key_t next_key;

static void nodtor(void *dummy)
{
}

static void dummy_0(void)
{
}

weak_alias(dummy_0, __tl_lock);
weak_alias(dummy_0, __tl_unlock);

void __pthread_key_atfork(int who)
{
	if (who<0) __pthread_rwlock_rdlock(&key_lock);
	else if (!who) __pthread_rwlock_unlock(&key_lock);
	else key_lock = (pthread_rwlock_t)PTHREAD_RWLOCK_INITIALIZER;
}

int __pthread_key_create(pthread_key_t *k, void (*dtor)(void *))
{
	pthread_t self = __pthread_self();

	/* This can only happen in the main thread before
	 * pthread_create has been called. */
	if (!self->tsd) self->tsd = __pthread_tsd_main;

	/* Purely a sentinel value since null means slot is free. */
	if (!dtor) dtor = nodtor;

	__pthread_rwlock_wrlock(&key_lock);
	pthread_key_t j = next_key;
	do {
		if (!keys[j]) {
			keys[next_key = *k = j] = dtor;
			__pthread_rwlock_unlock(&key_lock);
			return 0;
		}
	} while ((j=(j+1)%PTHREAD_KEYS_MAX) != next_key);

	__pthread_rwlock_unlock(&key_lock);
	return EAGAIN;
}

int __pthread_key_delete(pthread_key_t k)
{
	sigset_t set;
	pthread_t self = __pthread_self(), td=self;

	__block_app_sigs(&set);
	__pthread_rwlock_wrlock(&key_lock);

	__tl_lock();
	do td->tsd[k] = 0;
	while ((td=td->next)!=self);
	__tl_unlock();

	keys[k] = 0;

	__pthread_rwlock_unlock(&key_lock);
	__restore_sigs(&set);

	return 0;
}

void __pthread_tsd_run_dtors()
{
	pthread_t self = __pthread_self();
	int i, j;
	for (j=0; self->tsd_used && j<PTHREAD_DESTRUCTOR_ITERATIONS; j++) {
		__pthread_rwlock_rdlock(&key_lock);
		self->tsd_used = 0;
		for (i=0; i<PTHREAD_KEYS_MAX; i++) {
			void *val = self->tsd[i];
			void (*dtor)(void *) = keys[i];
			self->tsd[i] = 0;
			if (val && dtor && dtor != nodtor) {
				__pthread_rwlock_unlock(&key_lock);
				dtor(val);
				__pthread_rwlock_rdlock(&key_lock);
			}
		}
		__pthread_rwlock_unlock(&key_lock);
	}
}

/* Expose the calling thread's TSD array (the pthread_setspecific slots) for
 * sanitizers. LeakSanitizer scans each thread's TLS range for pointers into
 * the heap; on glibc that range is extended to cover the TCB, where the
 * specifics live inline (pthread::specific_1stblock). On musl the slots are
 * a separate array placed at the top of the thread mapping, above the static
 * TLS block, so without this hook an allocation referenced only through
 * pthread_setspecific - for example libc++'s per-thread __thread_struct of
 * any thread still running at exit - is reported as a leak.
 *
 * Deliberately placed in this translation unit: the sanitizer runtime only
 * references the function weakly, which does not extract archive members on
 * its own, and any program that can have TSD-referenced allocations extracts
 * this object anyway through pthread_key_create.
 *
 * Not a musl-upstream interface; used by the ClickHouse compiler-rt build
 * (GetTls in sanitizer_linux_libcdep.cpp). */
void __pthread_current_tsd_range(void **begin, void **end)
{
	pthread_t self = __pthread_self();
	void **tsd = self->tsd;
	if (!tsd) tsd = __pthread_tsd_main;
	*begin = (void *)tsd;
	*end = (void *)((char *)tsd + __pthread_tsd_size);
}

/* Bounds of the calling thread's struct pthread. The sanitizer runtime resets
 * the shadow memory of a starting thread's stack and static TLS block, but
 * musl allocates the stack, the TLS image and the tsd array in one mapping
 * with internal __mmap/__munmap/__unmapself calls that the runtime does not
 * see, so a mapping recycled for a new thread keeps the previous thread's
 * shadow for everything outside those two ranges. On TLS_ABOVE_TP targets
 * (aarch64) struct pthread sits just below the thread pointer, outside the
 * static TLS block, and its errno_val then shows up as a data race between
 * the exited and the new thread (seen with TSan: Rust std reading errno after
 * a tokio blocking-pool thread was replaced). Exposing the descriptor range
 * lets GetTls include it, the same way ThreadDescriptorSize() does for glibc.
 *
 * Same placement rationale and status as __pthread_current_tsd_range. */
void __pthread_current_thread_range(void **begin, void **end)
{
	pthread_t self = __pthread_self();
	*begin = (void *)self;
	*end = (void *)(self + 1);
}

weak_alias(__pthread_key_create, pthread_key_create);
weak_alias(__pthread_key_delete, pthread_key_delete);
