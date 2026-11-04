/* lane_wivrn.c OUT SECONDS [HZ] [MBIT] [WORK_MS] - a stand-in for WiVRn's CPU side beside a headless game (som-b37-lane2,
 * docs/re-xr-pipeline.md 163.8): the headless boots have Monado's null compositor but no WiVRn server, whose CPU work in a
 * real session is the encoder's per-frame submit and read-back of the bitstream and the stream's packets over the
 * network (the encode itself runs on the GPU's VCN). Every frame (HZ, default 90): the "encoder" thread reads the frame's
 * bitstream (MBIT/HZ bits, default 100 Mbit/s) out of a 4 MB ring and copies it into packets of 1400 bytes with a
 * checksum each (FEC-like work) - as many passes as take WORK_MS (default 2.5 ms: ~25 % of a core with the rest, an
 * estimate of the server's encoder, compositor and audio threads together; the passes counted once at the start and then
 * fixed, so a slowed tick shows as more work) - then hands them to the "network" thread, which sends them as UDP datagrams to a socket
 * of its own on 127.0.0.1 that a third thread drains (the kernel's network path both ways). OUT gets the encoder tick's
 * own `T INTERVAL_MS WORK_MS` lines (SOMVR_PERF_RAW's format, CLOCK_MONOTONIC_RAW: the Windows build's clock under Wine) - whether the lane delays it. No GPU, no
 * sound, nothing leaves the PC (loopback only). An estimate of WiVRn's CPU side, not a measurement of it. */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define PKT 1400
static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static volatile int g_stop;
static int g_tx, g_rx;
static struct sockaddr_in g_to;
static uint8_t *g_pk;     /* the packets of the current frame */
static size_t g_npk;
static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static unsigned g_gen;
static volatile uint32_t g_sink;

/* one pass: the frame's bitstream out of the ring into its packets, a checksum each */
static size_t pass(uint8_t *rb, size_t ring, size_t off, size_t npk)
{
	for (size_t i = 0; i < npk; i++) {
		uint8_t *p = g_pk + i * PKT;
		if (off + PKT > ring)
			off = 0;
		memcpy(p, rb + off, PKT);
		uint32_t s = 0;
		for (int k = 0; k < PKT; k += 4) {
			uint32_t v;
			memcpy(&v, p + k, sizeof v);
			s = (s << 1 | s >> 31) ^ v;
		}
		memcpy(p, &s, sizeof s);
		off += PKT;
	}
	return off;
}

static void *drain(void *u)
{
	(void)u;
	uint8_t b[2048];
	uint32_t s = 0;
	while (!g_stop) {
		ssize_t n = recv(g_rx, b, sizeof b, 0);
		if (n > 0)
			s += b[0] + b[n - 1];
	}
	g_sink = s;
	return NULL;
}

static void *net(void *u)
{
	(void)u;
	unsigned seen = 0;
	while (!g_stop) {
		pthread_mutex_lock(&g_mx);
		while (g_gen == seen && !g_stop)
			pthread_cond_wait(&g_cv, &g_mx);
		seen = g_gen;
		size_t n = g_npk;
		pthread_mutex_unlock(&g_mx);
		for (size_t i = 0; i < n && !g_stop; i++)
			sendto(g_tx, g_pk + i * PKT, PKT, 0, (struct sockaddr *)&g_to, sizeof g_to);
	}
	return NULL;
}

int main(int argc, char **argv)
{
	if (argc < 3)
		return 2;
	FILE *o = fopen(argv[1], "w");
	double secs = atof(argv[2]), hz = argc > 3 ? atof(argv[3]) : 90, mbit = argc > 4 ? atof(argv[4]) : 100;
	double work_ms = argc > 5 ? atof(argv[5]) : 2.5;
	if (!o || hz <= 0 || mbit <= 0)
		return 1;
	size_t ring = 4u << 20, fbytes = (size_t)(mbit * 1e6 / 8 / hz);
	size_t npk = (fbytes + PKT - 1) / PKT;
	uint8_t *rb = malloc(ring);
	g_pk = malloc(npk * PKT);
	if (!rb || !g_pk)
		return 1;
	for (size_t i = 0; i < ring; i++)
		rb[i] = (uint8_t)(i * 2654435761u >> 13);
	g_rx = socket(AF_INET, SOCK_DGRAM, 0);
	g_tx = socket(AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = 0, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
	socklen_t al = sizeof a;
	int rcv = 4 << 20;
	setsockopt(g_rx, SOL_SOCKET, SO_RCVBUF, &rcv, sizeof rcv);
	struct timeval tv = { 0, 200000 };
	setsockopt(g_rx, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
	if (g_rx < 0 || g_tx < 0 || bind(g_rx, (struct sockaddr *)&a, sizeof a) || getsockname(g_rx, (struct sockaddr *)&a, &al))
		return 1;
	g_to = a;
	pthread_t td, tn;
	pthread_create(&td, NULL, drain, NULL);
	pthread_create(&tn, NULL, net, NULL);
	/* the passes a frame: one pass's time over 0.3 s, then WORK_MS of them (at least one) */
	size_t off = 0;
	int passes = 1;
	{
		double c0 = now();
		long n = 0;
		while (now() - c0 < 0.3) {
			off = pass(rb, ring, off, npk);
			n++;
		}
		double per = (now() - c0) / (double)n * 1e3;
		passes = (int)(work_ms / per + 0.5);
		if (passes < 1)
			passes = 1;
	}
	fprintf(o, "# lane_wivrn 1: T INTERVAL_MS WORK_MS (clock monotonic_raw; %.0f Hz, %.0f Mbit/s, %zu packets a frame, %d passes)\n",
		hz, mbit, npk, passes);
	double t0 = now(), next = t0, last = 0, flush = t0;
	while (now() - t0 < secs) {
		double w0 = now();
		pthread_mutex_lock(&g_mx);
		for (int q = 0; q < passes; q++)
			off = pass(rb, ring, off, npk);
		g_npk = npk;
		g_gen++;
		pthread_cond_signal(&g_cv);
		pthread_mutex_unlock(&g_mx);
		double w1 = now();
		if (last > 0)
			fprintf(o, "%.6f %.3f %.3f\n", w1, (w1 - last) * 1e3, (w1 - w0) * 1e3);
		last = w1;
		if (w1 - flush >= 0.5)
			fflush(o), flush = w1;
		next += 1.0 / hz;
		double s = next - now();
		if (s > 0) {
			struct timespec ts = { (time_t)s, (long)((s - (double)(time_t)s) * 1e9) };
			nanosleep(&ts, NULL);
		} else if (s < -0.5)
			next = now();
	}
	g_stop = 1;
	pthread_mutex_lock(&g_mx);
	pthread_cond_signal(&g_cv);
	pthread_mutex_unlock(&g_mx);
	pthread_join(tn, NULL);
	pthread_join(td, NULL);
	fclose(o);
	return 0;
}
