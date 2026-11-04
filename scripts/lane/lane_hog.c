/* lane_hog.c THREADS SECONDS - the lane's deliberate overload (som-b35-lane, docs/re-xr-pipeline.md 163): each thread
 * streams a triad over its own three 256 MB arrays (a[i] = b[i] + s * c[i]), far past the 32 MB L3: the memory bandwidth
 * and the shared L3 go to the lane - the cost a game feels from a heavy lane job. Prints its GB/s every second. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define N (32u << 20) /* doubles: 256 MB an array */

static double secs, t0;
static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
static volatile unsigned long long bytes[64];

static void *run(void *arg)
{
	long k = (long)arg;
	double *a = malloc(N * sizeof *a), *b = malloc(N * sizeof *b), *c = malloc(N * sizeof *c);
	if (!a || !b || !c)
		return NULL;
	for (size_t i = 0; i < N; i++)
		a[i] = 0, b[i] = (double)i, c[i] = 1.0;
	while (now() - t0 < secs) {
		for (size_t i = 0; i < N; i++)
			a[i] = b[i] + 1.5 * c[i];
		bytes[k] += 3ull * N * sizeof(double);
	}
	return NULL;
}

int main(int argc, char **argv)
{
	int n = argc > 1 ? atoi(argv[1]) : 4;
	secs = argc > 2 ? atof(argv[2]) : 30;
	if (n < 1 || n > 64)
		n = 4;
	t0 = now();
	struct timespec raw;
	clock_gettime(CLOCK_MONOTONIC_RAW, &raw);
	printf("lane_hog: start mono=%.6f raw=%.6f\n", t0, (double)raw.tv_sec + (double)raw.tv_nsec * 1e-9), fflush(stdout);
	pthread_t th[64];
	for (long k = 0; k < n; k++)
		pthread_create(&th[k], NULL, run, (void *)k);
	unsigned long long last = 0;
	for (double t = 1; t <= secs + 0.5; t += 1) {
		struct timespec ts = { 1, 0 };
		nanosleep(&ts, NULL);
		unsigned long long s = 0;
		for (int k = 0; k < n; k++)
			s += bytes[k];
		printf("lane_hog: %d threads, %.0f s: %.1f GB/s\n", n, t, (double)(s - last) / 1e9), fflush(stdout);
		last = s;
	}
	for (int k = 0; k < n; k++)
		pthread_join(th[k], NULL);
	return 0;
}
