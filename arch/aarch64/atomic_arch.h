#ifdef __ARM_FEATURE_ATOMICS

/* The LSE atomics of ARMv8.1. Under contention a load/store-exclusive loop keeps
 * failing and retrying (each store-exclusive of one core cancels the reservations
 * of the others), while a single LSE instruction is executed at the cache. */

#define a_cas a_cas
static inline int a_cas(volatile int *p, int t, int s)
{
	int old = t;
	__asm__ __volatile__ ("casal %w0,%w2,%1" : "+r"(old), "+Q"(*p) : "r"(s) : "memory");
	/* A failed casal has only the acquire semantics, while all atomics of musl are full
	 * barriers, as the load/store-exclusive version below is. */
	if (old != t) __asm__ __volatile__ ("dmb ish" : : : "memory");
	return old;
}

#define a_cas_p a_cas_p
static inline void *a_cas_p(volatile void *p, void *t, void *s)
{
	void *old = t;
	__asm__ __volatile__ ("casal %0,%2,%1" : "+r"(old), "+Q"(*(void *volatile *)p) : "r"(s) : "memory");
	if (old != t) __asm__ __volatile__ ("dmb ish" : : : "memory");
	return old;
}

#define a_swap a_swap
static inline int a_swap(volatile int *p, int v)
{
	int old;
	__asm__ __volatile__ ("swpal %w2,%w0,%1" : "=r"(old), "+Q"(*p) : "r"(v) : "memory");
	return old;
}

#define a_fetch_add a_fetch_add
static inline int a_fetch_add(volatile int *p, int v)
{
	int old;
	__asm__ __volatile__ ("ldaddal %w2,%w0,%1" : "=r"(old), "+Q"(*p) : "r"(v) : "memory");
	return old;
}

#define a_fetch_and a_fetch_and
static inline int a_fetch_and(volatile int *p, int v)
{
	int old;
	/* ldclr clears the bits that are set in the operand. */
	__asm__ __volatile__ ("ldclral %w2,%w0,%1" : "=r"(old), "+Q"(*p) : "r"(~v) : "memory");
	return old;
}

#define a_fetch_or a_fetch_or
static inline int a_fetch_or(volatile int *p, int v)
{
	int old;
	__asm__ __volatile__ ("ldsetal %w2,%w0,%1" : "=r"(old), "+Q"(*p) : "r"(v) : "memory");
	return old;
}

#define a_and_64 a_and_64
static inline void a_and_64(volatile uint64_t *p, uint64_t v)
{
	uint64_t old;
	__asm__ __volatile__ ("ldclral %2,%0,%1" : "=r"(old), "+Q"(*p) : "r"(~v) : "memory");
}

#define a_or_64 a_or_64
static inline void a_or_64(volatile uint64_t *p, uint64_t v)
{
	uint64_t old;
	__asm__ __volatile__ ("ldsetal %2,%0,%1" : "=r"(old), "+Q"(*p) : "r"(v) : "memory");
}

#define a_barrier a_barrier
static inline void a_barrier()
{
	__asm__ __volatile__ ("dmb ish" : : : "memory");
}

#else

#define a_ll a_ll
static inline int a_ll(volatile int *p)
{
	int v;
	__asm__ __volatile__ ("ldaxr %w0,%1" : "=r"(v) : "Q"(*p));
	return v;
}

#define a_sc a_sc
static inline int a_sc(volatile int *p, int v)
{
	int r;
	__asm__ __volatile__ ("stlxr %w0,%w2,%1" : "=&r"(r), "=Q"(*p) : "r"(v) : "memory");
	return !r;
}

#define a_barrier a_barrier
static inline void a_barrier()
{
	__asm__ __volatile__ ("dmb ish" : : : "memory");
}

#define a_cas a_cas
static inline int a_cas(volatile int *p, int t, int s)
{
	int old;
	do {
		old = a_ll(p);
		if (old != t) {
			a_barrier();
			break;
		}
	} while (!a_sc(p, s));
	return old;
}

#define a_ll_p a_ll_p
static inline void *a_ll_p(volatile void *p)
{
	void *v;
	__asm__ __volatile__ ("ldaxr %0, %1" : "=r"(v) : "Q"(*(void *volatile *)p));
	return v;
}

#define a_sc_p a_sc_p
static inline int a_sc_p(volatile int *p, void *v)
{
	int r;
	__asm__ __volatile__ ("stlxr %w0,%2,%1" : "=&r"(r), "=Q"(*(void *volatile *)p) : "r"(v) : "memory");
	return !r;
}

#define a_cas_p a_cas_p
static inline void *a_cas_p(volatile void *p, void *t, void *s)
{
	void *old;
	do {
		old = a_ll_p(p);
		if (old != t) {
			a_barrier();
			break;
		}
	} while (!a_sc_p(p, s));
	return old;
}

#endif

#define a_ctz_64 a_ctz_64
static inline int a_ctz_64(uint64_t x)
{
	__asm__(
		"	rbit %0, %1\n"
		"	clz %0, %0\n"
		: "=r"(x) : "r"(x));
	return x;
}

#define a_clz_64 a_clz_64
static inline int a_clz_64(uint64_t x)
{
	__asm__("clz %0, %1" : "=r"(x) : "r"(x));
	return x;
}
