/* test_wsound.c - somvr_wsound.h (build 37, SOMVR_AUDIO_NEAR, docs/re-xr-pipeline.md 173): the game's third-person
 * rework of a sound's position replayed as the exe does it, and the VR placement against it. Each "game:" check is
 * the bug the VR placement fixes (it holds for the game's rule), each "vr:" check the fix. */
#include <stdio.h>
#include <math.h>
#include "../somvr_wsound.h"

static int g_fail, g_n;
#define CHECK(c, ...) do { g_n++; if (!(c)) { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const struct wsd_cfg C = { 250.0f, 50.0f, 0.5f, 0.0f };
static float bearing_of(const float A[3], const float F[3], const float U[3], const float P[3])
{
	float b, e, d;
	wsd_bearing(A, F, U, P, &b, &e, &d);
	return b;
}
static float dist_of(const float A[3], const float P[3])
{
	return sqrtf((P[0] - A[0]) * (P[0] - A[0]) + (P[1] - A[1]) * (P[1] - A[1]) + (P[2] - A[2]) * (P[2] - A[2]));
}
static void head_yaw(float deg, float F[3], float U[3])
{
	float r = deg * 3.14159265f / 180.0f;
	F[0] = sinf(r), F[1] = 0, F[2] = cosf(r), U[0] = 0, U[1] = 1, U[2] = 0;
}

int main(void)
{
	/* the head 170 cm up, looking along +Z (Wwise: X right, Y up, Z ahead); Talion's root (the game's player) under it */
	const float A[3] = { 1000, 170, 2000 }, B[3] = { 1000, 0, 2000 };
	float F[3], U[3], E[3], o[3];
	head_yaw(0, F, U);

	/* wsd_bearing: right is +X, ahead +Z, up +Y */
	E[0] = A[0] + 100, E[1] = A[1], E[2] = A[2];
	CHECK(fabsf(bearing_of(A, F, U, E) - 90) < 0.01f, "bearing right %f", bearing_of(A, F, U, E));
	E[0] = A[0], E[2] = A[2] + 100;
	CHECK(fabsf(bearing_of(A, F, U, E)) < 0.01f, "bearing ahead %f", bearing_of(A, F, U, E));
	E[0] = A[0] - 100, E[2] = A[2];
	CHECK(fabsf(bearing_of(A, F, U, E) + 90) < 0.01f, "bearing left %f", bearing_of(A, F, U, E));

	/* game: the rework, by the exe's two steps - a sound at the feet is pulled to 5 cm of the player's distance
	 * along its line, then put on the 250 cm ring at the head's height; exactly under the head: world +X */
	E[0] = B[0], E[1] = B[1], E[2] = B[2];
	wsd_game_rework(A, B, E);
	CHECK(fabsf(E[0] - (A[0] + 250)) < 0.01f && fabsf(E[1] - A[1]) < 0.01f && fabsf(E[2] - A[2]) < 0.01f,
	      "game: the feet straight under the head -> %f %f %f (want A + 250 x)", E[0] - A[0], E[1] - A[1], E[2] - A[2]);
	/* game: 3 cm to the left or the right of the head's vertical line: hard left / hard right */
	float L[3] = { B[0] - 3, B[1], B[2] }, R[3] = { B[0] + 3, B[1], B[2] };
	wsd_game_rework(A, B, L), wsd_game_rework(A, B, R);
	CHECK(bearing_of(A, F, U, L) < -89 && bearing_of(A, F, U, R) > 89, "game: feet 3 cm left %f / right %f (the throw)",
	      bearing_of(A, F, U, L), bearing_of(A, F, U, R));
	/* game: a sound put at the listener: wherever the head looks, 250 cm toward world +X - its bearing goes all
	 * the way round as the head turns */
	float minb = 999, maxb = -999;
	for (int y = 0; y < 360; y += 15) {
		head_yaw((float)y, F, U);
		E[0] = A[0], E[1] = A[1], E[2] = A[2];
		wsd_game_rework(A, B, E);
		float b = bearing_of(A, F, U, E);
		minb = b < minb ? b : minb, maxb = b > maxb ? b : maxb;
	}
	CHECK(minb < -150 && maxb > 150, "game: the listener's own sound swings %f .. %f as the head turns", minb, maxb);

	/* vr: the same three - centred, steady */
	head_yaw(0, F, U);
	L[0] = B[0] - 3, L[1] = B[1], L[2] = B[2], R[0] = B[0] + 3, R[1] = B[1], R[2] = B[2];
	wsd_vr_place(&C, A, F, U, L, L, 1), wsd_vr_place(&C, A, F, U, R, R, 1);
	CHECK(fabsf(bearing_of(A, F, U, L)) < 5 && fabsf(bearing_of(A, F, U, R)) < 5, "vr: feet 3 cm left %f / right %f",
	      bearing_of(A, F, U, L), bearing_of(A, F, U, R));
	CHECK(fabsf(dist_of(A, L) - 250) < 0.1f && fabsf(L[1] - A[1]) < 0.01f, "vr: the feet on the ring %f at the head's "
	      "height %f", dist_of(A, L), L[1] - A[1]);
	minb = 999, maxb = -999;
	for (int y = 0; y < 360; y += 15) {
		head_yaw((float)y, F, U);
		E[0] = A[0], E[1] = A[1], E[2] = A[2];
		int nr = wsd_vr_place(&C, A, F, U, E, o, 1);
		float b = bearing_of(A, F, U, o);
		minb = b < minb ? b : minb, maxb = b > maxb ? b : maxb;
		CHECK(nr == 1 && fabsf(dist_of(A, o) - 250) < 0.1f, "vr: the listener's own sound near, on the ring (%f)",
		      dist_of(A, o));
	}
	CHECK(minb > -0.5f && maxb < 0.5f, "vr: the listener's own sound stays ahead %f .. %f", minb, maxb);

	/* vr: clearly to one side - the true direction, the game's ring (as loud as the game) */
	head_yaw(0, F, U);
	E[0] = A[0] + 100, E[1] = A[1] - 20, E[2] = A[2];
	float G[3] = { E[0], E[1], E[2] };
	wsd_game_rework(A, B, G);
	wsd_vr_place(&C, A, F, U, E, o, 1);
	CHECK(fabsf(bearing_of(A, F, U, o) - 90) < 0.5f && fabsf(dist_of(A, o) - 250) < 0.1f,
	      "vr: 1 m to the right -> bearing %f at %f", bearing_of(A, F, U, o), dist_of(A, o));
	CHECK(fabsf(bearing_of(A, F, U, G) - bearing_of(A, F, U, o)) < 0.5f, "vr: as the game's %f / %f",
	      bearing_of(A, F, U, G), bearing_of(A, F, U, o));
	/* the sword in the hand, 45 cm ahead-right: its own direction */
	E[0] = A[0] + 32, E[1] = A[1] - 40, E[2] = A[2] + 32;
	wsd_vr_place(&C, A, F, U, E, o, 1);
	CHECK(bearing_of(A, F, U, o) > 20 && bearing_of(A, F, U, o) < 46, "vr: the hand ahead-right -> %f",
	      bearing_of(A, F, U, o));

	/* vr: far sounds where they are; a steep one leans to the front and keeps its distance */
	E[0] = A[0] + 1000, E[1] = A[1] + 300, E[2] = A[2] - 400;
	wsd_vr_place(&C, A, F, U, E, o, 1);
	CHECK(fabsf(o[0] - E[0]) < 0.01f && fabsf(o[1] - E[1]) < 0.01f && fabsf(o[2] - E[2]) < 0.01f,
	      "vr: a far sound moved %f %f %f", o[0] - E[0], o[1] - E[1], o[2] - E[2]);
	E[0] = A[0] - 40, E[1] = A[1] + 1500, E[2] = A[2];
	wsd_vr_place(&C, A, F, U, E, o, 1);
	CHECK(fabsf(dist_of(A, o) - dist_of(A, E)) < 0.5f && fabsf(bearing_of(A, F, U, o)) < 30,
	      "vr: 15 m overhead, 40 cm left: %f cm, bearing %f", dist_of(A, o), bearing_of(A, F, U, o));
	E[0] = A[0] + 3000, E[1] = A[1] + 2000, E[2] = A[2]; /* 34 deg up: not steep */
	wsd_vr_place(&C, A, F, U, E, o, 1);
	CHECK(fabsf(o[0] - E[0]) < 0.01f && fabsf(o[1] - E[1]) < 0.01f, "vr: 34 deg up moved");

	/* vr: no jump - a sound walking out from under the head to 1.5 m right at foot level turns smoothly */
	float last = 0;
	int mono_ok = 1, jump = 0;
	for (int x = 0; x <= 150; x += 2) {
		E[0] = A[0] + (float)x, E[1] = B[1], E[2] = A[2];
		wsd_vr_place(&C, A, F, U, E, o, 1);
		float b = bearing_of(A, F, U, o);
		if (b < last - 0.01f)
			mono_ok = 0;
		if (b - last > 12)
			jump = 1;
		last = b;
	}
	CHECK(mono_ok && !jump && last > 85, "vr: out from under the head: monotonic %d, a jump %d, at 1.5 m %f", mono_ok, jump, last);

	/* vr: the head looking 80 deg down (and up): ahead is still the way the face is turned */
	float p = 80 * 3.14159265f / 180;
	float Fd[3] = { 0, -sinf(p), cosf(p) }, Ud[3] = { 0, cosf(p), sinf(p) };
	E[0] = A[0], E[1] = A[1], E[2] = A[2];
	wsd_vr_place(&C, A, Fd, Ud, E, o, 1);
	CHECK(fabsf(bearing_of(A, Fd, Ud, o)) < 0.5f && fabsf(dist_of(A, o) - 250) < 0.1f, "vr: looking down, the listener's sound at bearing %f, %f cm", bearing_of(A, Fd, Ud, o), dist_of(A, o));
	float Fu[3] = { 0, sinf(p), cosf(p) }, Uu[3] = { 0, cosf(p), -sinf(p) };
	wsd_vr_place(&C, A, Fu, Uu, E, o, 1);
	CHECK(fabsf(bearing_of(A, Fu, Uu, o)) < 0.5f, "vr: looking up, bearing %f", bearing_of(A, Fu, Uu, o));

	/* vr: a NaN stays the game's input; axis 0 = the true direction off the line, ahead only on it */
	E[0] = NAN, E[1] = 0, E[2] = 0;
	wsd_vr_place(&C, A, F, U, E, o, 1);
	CHECK(isnan(o[0]), "vr: NaN kept");
	struct wsd_cfg c0 = { 250, 0, 0, 0 };
	E[0] = A[0], E[1] = A[1], E[2] = A[2];
	wsd_vr_place(&c0, A, F, U, E, o, 1);
	CHECK(fabsf(o[2] - A[2] - 250) < 0.1f, "vr: axis 0, on A -> ahead %f", o[2] - A[2]);

	/* the game's step 2 off (the Windows world): distances kept. The music's object at Talion's root, 77 cm under the
	 * eyes and 1 cm off the line: raw, its bearing follows that centimetre (any value as the head turns); placed for
	 * the head at each turn, ahead, under the eyes and at 77 cm */
	float M[3] = { A[0] + 1, A[1] - 77, A[2] - 0.5f };
	CHECK(wsd_on_axis(&C, A, F, U, M), "off: the music under the head not on the axis");
	minb = 999, maxb = -999;
	float rminb = 999, rmaxb = -999;
	for (int y = 0; y < 360; y += 15) {
		head_yaw((float)y, F, U);
		float b0 = bearing_of(A, F, U, M);
		rminb = b0 < rminb ? b0 : rminb, rmaxb = b0 > rmaxb ? b0 : rmaxb;
		int nr = wsd_vr_place(&C, A, F, U, M, o, 0);
		float b = fabsf(sinf(bearing_of(A, F, U, o) / 57.29578f)); /* the pan: ahead or behind are both the middle */
		minb = b < minb ? b : minb, maxb = b > maxb ? b : maxb;
		CHECK(nr == 1 && fabsf(dist_of(A, o) - dist_of(A, M)) < 0.1f && o[1] < A[1], "off: the music at %f cm, under %d",
		      dist_of(A, o), o[1] < A[1]);
	}
	CHECK(rminb < -150 && rmaxb > 150, "off, raw: the music's bearing %f .. %f (the swing)", rminb, rmaxb);
	CHECK(maxb < 0.05f, "off, placed: the music's pan up to %f", maxb);
	/* a loop 33 m straight overhead: placed ahead, its distance kept; one 40 m away at 15 deg up: not on the axis,
	 * untouched; 1 m to the right at ear height: its true place (no ring) */
	float O[3] = { A[0] + 0.3f, A[1] + 3300, A[2] + 0.2f };
	head_yaw(70, F, U);
	wsd_vr_place(&C, A, F, U, O, o, 0);
	CHECK(wsd_on_axis(&C, A, F, U, O) && fabsf(bearing_of(A, F, U, o)) < 0.5f && fabsf(dist_of(A, o) - dist_of(A, O)) < 1,
	      "off: 33 m overhead -> bearing %f at %f", bearing_of(A, F, U, o), dist_of(A, o));
	float T[3] = { A[0] + 3860, A[1] + 1035, A[2] };
	CHECK(!wsd_on_axis(&C, A, F, U, T), "off: 40 m at 15 deg on the axis");
	wsd_vr_place(&C, A, F, U, T, o, 0);
	CHECK(fabsf(o[0] - T[0]) < 0.01f && fabsf(o[1] - T[1]) < 0.01f && fabsf(o[2] - T[2]) < 0.01f, "off: 15 deg up moved");
	float S[3] = { A[0] + 100, A[1], A[2] };
	wsd_vr_place(&C, A, F, U, S, o, 0);
	CHECK(fabsf(o[0] - S[0]) < 0.01f && fabsf(o[2] - S[2]) < 0.01f, "off: 1 m to the right moved (%f %f)", o[0] - S[0], o[2] - S[2]);

	/* the wraith's fire on the sword on Talion's back (boot 021700: 18-39 cm from the head, behind, 15-45 deg down): the
	 * head turned -60..+60 against the body. Raw its left-right pan (sin of the bearing: what a stereo mix follows)
	 * reaches the side; placed (the least distance 250) it stays near the middle, 250 cm off */
	struct wsd_cfg cm = { 250, 50, 0.5f, 250 };
	float SW[3] = { A[0] + 8, A[1] - 15, A[2] - 25 };
	float rawmax = 0, ourmax = 0;
	for (int y = -60; y <= 60; y += 5) {
		head_yaw((float)y, F, U);
		float pr = sinf(bearing_of(A, F, U, SW) / 57.29578f);
		wsd_vr_place(&cm, A, F, U, SW, o, 0);
		float po = sinf(bearing_of(A, F, U, o) / 57.29578f);
		rawmax = fabsf(pr) > rawmax ? fabsf(pr) : rawmax, ourmax = fabsf(po) > ourmax ? fabsf(po) : ourmax;
		CHECK(fabsf(dist_of(A, o) - 250) < 0.1f, "the sword's fire at %f cm", dist_of(A, o));
	}
	CHECK(rawmax > 0.85f && ourmax < 0.3f, "the sword's fire: the pan raw up to %f, placed up to %f", rawmax, ourmax);
	/* the same with the head pitched (boot 024101: looking 60 down / 40 up the world-frame rule left it at full pan) */
	float rawp = 0, ourp = 0;
	for (int pdeg = -60; pdeg <= 40; pdeg += 20)
		for (int y = -60; y <= 60; y += 10) {
			float pr2 = pdeg * 3.14159265f / 180, yr = y * 3.14159265f / 180;
			float Fp[3] = { sinf(yr) * cosf(pr2), sinf(pr2), cosf(yr) * cosf(pr2) };
			float Up[3] = { -sinf(yr) * sinf(pr2), cosf(pr2), -cosf(yr) * sinf(pr2) };
			float a1 = fabsf(sinf(bearing_of(A, Fp, Up, SW) / 57.29578f));
			wsd_vr_place(&cm, A, Fp, Up, SW, o, 0);
			float a2 = fabsf(sinf(bearing_of(A, Fp, Up, o) / 57.29578f));
			rawp = a1 > rawp ? a1 : rawp, ourp = a2 > ourp ? a2 : ourp;
		}
	CHECK(rawp > 0.9f && ourp < 0.3f, "the sword's fire, the head pitched -60..40: the pan raw up to %f, placed up to %f", rawp, ourp);
	CHECK(wsd_needs_place(&cm, A, F, U, SW, 0) && !wsd_needs_place(&cm, A, F, U, T, 0), "needs_place: the sword's fire / 40 m away");
	/* behind and to the side but off the axis (2 m behind-right at ear height): untouched by the axis rule, kept at 2.5 m
	 * in its true direction */
	float BR[3] = { A[0] + 120, A[1], A[2] - 160 };
	head_yaw(0, F, U);
	wsd_vr_place(&cm, A, F, U, BR, o, 0);
	CHECK(fabsf(bearing_of(A, F, U, o) - bearing_of(A, F, U, BR)) < 0.5f && fabsf(dist_of(A, o) - 250) < 0.1f,
	      "2 m behind-right: bearing %f (raw %f) at %f", bearing_of(A, F, U, o), bearing_of(A, F, U, BR), dist_of(A, o));

	printf("test_wsound: %d checks, %d failed\n", g_n, g_fail);
	return g_fail != 0;
}
