/* test_rcache.c - win/somvr_win_rcache.h (the safe reads' region cache, Linux, build.sh --check): two writers keep
 * re-caching two regions A and B into every slot while two readers ask for an address between them. A reader must
 * never be told it is readable: a slot seen half-updated ([0, hi) while lo is being set, or A's lo with B's hi) would
 * hand somvr_safe_read an unmapped address (a fault, or a guard page touched). Also: forget drops a cached region. */
#include "../somvr_win_rcache.h"
#include <pthread.h>
#include <stdio.h>
#include <time.h>

#define A_LO 0x10000u
#define A_HI 0x20000u
#define B_LO 0x50000u
#define B_HI 0x60000u
#define GAP 0x30000u /* in neither */

static somvr_rcache g_c;
static _Atomic int g_stop;
static _Atomic unsigned long g_false_hits, g_reads, g_puts;

static void *writer(void *p)
{
	(void)p;
	unsigned long n = 0;
	while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
		somvr_rc_put(&g_c, A_LO, A_HI);
		somvr_rc_put(&g_c, B_LO, B_HI);
		n += 2;
	}
	atomic_fetch_add(&g_puts, n);
	return NULL;
}

static void *reader(void *p)
{
	(void)p;
	unsigned long n = 0, bad = 0;
	while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
		bad += somvr_rc_hit(&g_c, GAP, 16);
		n++;
	}
	atomic_fetch_add(&g_reads, n), atomic_fetch_add(&g_false_hits, bad);
	return NULL;
}

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec;
}

int main(void)
{
	int fails = 0;
	/* single-threaded: a cached region hits, the gap and the edges do not, forget drops it */
	somvr_rc_put(&g_c, A_LO, A_HI);
	if (!somvr_rc_hit(&g_c, A_LO, 16) || !somvr_rc_hit(&g_c, A_HI - 16, 16))
		printf("FAIL: a cached region is not hit\n"), fails++;
	if (somvr_rc_hit(&g_c, GAP, 16) || somvr_rc_hit(&g_c, A_HI - 8, 16) || somvr_rc_hit(&g_c, A_LO - 8, 16) ||
	    somvr_rc_hit(&g_c, 0, 16))
		printf("FAIL: an address outside the cached region is hit\n"), fails++;
	if (somvr_rc_hit(&g_c, A_LO, (size_t)0 - A_LO + 1))
		printf("FAIL: a wrapping range is hit\n"), fails++;
	somvr_rc_forget(&g_c);
	if (somvr_rc_hit(&g_c, A_LO, 16))
		printf("FAIL: forget left a cached region\n"), fails++;
	/* concurrent: no false hit while the slots change under the readers */
	pthread_t w[2], r[2];
	for (int i = 0; i < 2; i++)
		pthread_create(&w[i], NULL, writer, NULL), pthread_create(&r[i], NULL, reader, NULL);
	double t0 = now();
	while (now() - t0 < 1.5 && !atomic_load(&g_false_hits)) {
		struct timespec s = { 0, 20000000 };
		nanosleep(&s, NULL);
	}
	atomic_store(&g_stop, 1);
	for (int i = 0; i < 2; i++)
		pthread_join(w[i], NULL), pthread_join(r[i], NULL);
	unsigned long fh = atomic_load(&g_false_hits);
	if (fh) {
		printf("FAIL: %lu of %lu reads called the gap %#x readable while %lu regions were cached (a half-updated slot)\n",
		       fh, atomic_load(&g_reads), GAP, atomic_load(&g_puts));
		fails++;
	}
	printf("test_rcache: %s (%lu reads, %lu puts in %.2f s)\n", fails ? "FAILED" : "ok", atomic_load(&g_reads),
	       atomic_load(&g_puts), now() - t0);
	return fails != 0;
}
