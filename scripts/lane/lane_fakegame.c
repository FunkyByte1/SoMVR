/* lane_fakegame.c OUT SECONDS [HZ] - a stand-in session for the lane's closed loop (som-b35-lane, docs 163): every
 * frame (HZ, default 90) a fixed piece of memory-bound work (a dependent walk through a 24 MB buffer: L3- and
 * latency-bound, as a game's render thread is), then a sleep to the next tick; OUT gets SOMVR_PERF_RAW's lines
 * (`T INTERVAL WORK`, CLOCK_MONOTONIC) in batches every 50 ms. For lane_watch.py's live check, never a measurement of
 * the game. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static volatile uint32_t sink;
static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
	if (argc < 3)
		return 2;
	FILE *o = fopen(argv[1], "w");
	double secs = atof(argv[2]), hz = argc > 3 ? atof(argv[3]) : 90;
	if (!o)
		return 1;
	size_t n = (24u << 20) / sizeof(uint32_t);
	uint32_t *b = malloc(n * sizeof *b);
	for (size_t i = 0; i < n; i++)
		b[i] = (uint32_t)i;
	for (size_t i = n - 1; i > 0; i--) { /* one random cycle: a dependent walk */
		size_t j = (size_t)(((uint64_t)rand() << 16 ^ (uint64_t)rand()) % i);
		uint32_t t = b[i];
		b[i] = b[j], b[j] = t;
	}
	fprintf(o, "# somvr perf raw 1: T INTERVAL_MS WORK_MS (clock monotonic, lane_fakegame)\n");
	double t0 = now(), next = t0, last = 0, flush = t0;
	uint32_t p = 0;
	while (now() - t0 < secs) {
		double w0 = now();
		for (int k = 0; k < 80000; k++)
			p = b[p];
		sink = p;
		double w1 = now();
		if (last > 0)
			fprintf(o, "%.6f %.3f %.3f\n", w1, (w1 - last) * 1e3, (w1 - w0) * 1e3);
		last = w1;
		if (w1 - flush >= 0.05)
			fflush(o), flush = w1;
		next += 1.0 / hz;
		double s = next - now();
		if (s > 0) {
			struct timespec ts = { (time_t)s, (long)((s - (double)(time_t)s) * 1e9) };
			nanosleep(&ts, NULL);
		}
	}
	fclose(o);
	return 0;
}
