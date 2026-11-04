/* somvr_win_rcache.h - the safe reads' cache of readable regions (win/somvr_win_compat.c), on its own for the Linux
 * test (win/tests/test_rcache.c, build.sh --check).
 *
 * Each slot is [lo, hi) under its own sequence number (a seqlock per slot): a reader takes a slot only from a pair read
 * between two equal even numbers; a writer claims the slot by moving its number to odd (compare-exchange: a slot another
 * writer holds is left alone - that region is simply not cached this time) and releases it at the next even one.
 * (b34 review: the earlier lo=0, hi, lo stores let a reader see [0, hi) - or one region's lo with another's hi - and
 * call an unmapped address readable: 0.15 % of the test's reads.) */
#ifndef SOMVR_WIN_RCACHE_H
#define SOMVR_WIN_RCACHE_H
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#define SOMVR_RC_N 64
typedef struct {
	struct {
		_Atomic uint64_t seq; /* odd: being written */
		_Atomic uintptr_t lo, hi;
	} s[SOMVR_RC_N];
	_Atomic unsigned next;
} somvr_rcache;

/* 1 = [a, a + n) lies in a cached region */
static inline int somvr_rc_hit(somvr_rcache *c, uintptr_t a, size_t n)
{
	if (a + n < a)
		return 0;
	for (int i = 0; i < SOMVR_RC_N; i++) {
		uint64_t s1 = atomic_load_explicit(&c->s[i].seq, memory_order_acquire);
		if (s1 & 1)
			continue;
		uintptr_t lo = atomic_load_explicit(&c->s[i].lo, memory_order_relaxed),
			  hi = atomic_load_explicit(&c->s[i].hi, memory_order_relaxed);
		atomic_thread_fence(memory_order_acquire);
		if (atomic_load_explicit(&c->s[i].seq, memory_order_relaxed) != s1)
			continue; /* changed under us: not this one */
		if (lo < hi && a >= lo && a + n <= hi)
			return 1;
	}
	return 0;
}

/* slot k claimed (odd) for writing: 1, its even number in *s */
static inline int somvr_rc_claim(somvr_rcache *c, unsigned k, uint64_t *s)
{
	*s = atomic_load_explicit(&c->s[k].seq, memory_order_relaxed);
	if ((*s & 1) || !atomic_compare_exchange_strong_explicit(&c->s[k].seq, s, *s + 1, memory_order_relaxed,
								  memory_order_relaxed))
		return 0;
	atomic_thread_fence(memory_order_release); /* the odd number before the new pair */
	return 1;
}

static inline void somvr_rc_set(somvr_rcache *c, unsigned k, uint64_t s, uintptr_t lo, uintptr_t hi)
{
	atomic_store_explicit(&c->s[k].lo, lo, memory_order_relaxed);
	atomic_store_explicit(&c->s[k].hi, hi, memory_order_relaxed);
	atomic_store_explicit(&c->s[k].seq, s + 2, memory_order_release);
}

/* cache [lo, hi) (a VirtualQuery region) */
static inline void somvr_rc_put(somvr_rcache *c, uintptr_t lo, uintptr_t hi)
{
	unsigned k = atomic_fetch_add_explicit(&c->next, 1, memory_order_relaxed) % SOMVR_RC_N;
	uint64_t s;
	if (somvr_rc_claim(c, k, &s))
		somvr_rc_set(c, k, s, lo, hi);
}

/* a fault in a cached region: every slot dropped (one a writer holds right now gets that writer's fresh region) */
static inline void somvr_rc_forget(somvr_rcache *c)
{
	for (unsigned k = 0; k < SOMVR_RC_N; k++) {
		uint64_t s;
		if (somvr_rc_claim(c, k, &s))
			somvr_rc_set(c, k, s, 0, 0);
	}
}
#endif
