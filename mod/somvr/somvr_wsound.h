/* somvr_wsound.h - build 37 (som-b37-audio, docs/re-xr-pipeline.md 173): where the game's sounds are heard in VR.
 * Header-only and pure (no game reads): somvr_wsound_game.c (the hooks) and tests/test_wsound.c include it.
 *
 * Every position the game gives a Wwise object goes through its sound wrapper's SetPosition (Windows 0x140e29470,
 * Linux 0x8c9b80), which first reworks it for a third-person camera (Windows 0x140e29200, Linux 0x8c89a0; on while
 * the wrapper's +0x14 is set by CSoundMgr slot 28 / Linux 29, SetPlayerPos):
 *   1. a sound farther from the listener A (wrapper +0x08, the listener the game set) than from the player B (+0x18)
 *      is pulled toward A along its own line, to its distance from B (at least 5 cm): heard from the camera's
 *      direction at the player's distance;
 *   2. a sound within 250 cm of A *horizontally* is put on a 250 cm ring around A, at A's height, in its horizontal
 *      direction from A - exactly on A (no direction): A + 250 cm along world +X.
 * Then Wwise's SetPosition, and the RTPC gp_distance = the distance from A after the rework.
 * In VR our listener is the head (SOMVR_AUDIO=head: A = the eye centre). Then step 2's direction for anything near the
 * head's vertical line (Talion's feet and body, his voice, a sound placed at the listener) is a few centimetres of
 * offset: a sound under or over the head is thrown hard left or right as the head leans or turns, one put at the
 * listener sits 2.5 m to the world's east whatever way the player looks. Step 1 is the camera's (the player is the
 * listener in VR).
 *
 * In the Windows sessions the wrapper's +0x14 is off in the world (boots 20261105-011856/020918/021700: no position
 * reworked): the positions reach Wwise as the game gives them. Wwise pans a stereo mix by the left-right part of the
 * direction, so a sound almost straight over or under the head is panned by a few centimetres of offset and swings from
 * ear to ear as the head turns - the music's object at the game's own listener (Talion's root, 77 cm under the eyes),
 * a loop 33 m overhead, the spirit tower's loop 26 m down - and Talion's own sounds sit an arm's length from the ears:
 * the wraith's fire on the sheathed sword (cbt_weap_blueswrd_cntr_lp) 18-39 cm behind the head, hard right one moment,
 * centred the next, and far louder than the flat game's camera (3-4 m off) ever heard it.
 *
 * The VR placement (SOMVR_AUDIO_NEAR=vr), in the head's own frame (its right, up and front: what the panner sees, also
 * with the head pitched - boot 024101: a world-frame rule left the sword's fire at full pan while the head looked
 * down): a sound's left-right part is gone near the head's up-down line and comes back smoothly (s = its horizontal offset / r, r = max(axis_cm, steep x its distance): none
 * below 0.6 r - the body's own sounds, the sword on the back -, a smoothstep up to all of it at r and beyond),
 * its front-back part is kept (at least (1 - s) x half that radius, so it never vanishes into noise): over or under
 * the head = centred, behind = centred, never thrown through the side; a nearer sound than min_cm (the game's own
 * 250 cm "near") is put at min_cm in that direction (the game's ring: at the listener's height); everything else
 * where the game puts it. Such sounds are placed again for the head at each listener update (somvr_wsound_game.c). */
#ifndef SOMVR_WSOUND_H
#define SOMVR_WSOUND_H
#include <math.h>

#define WSD_GAME_MIN_CM 5.0f    /* step 1's least distance (Windows 0x1413ce768, Linux 0x3454a20) */
#define WSD_GAME_RING_CM 250.0f /* step 2's ring (Windows 0x1413c0e60, Linux 0x3454a24) */

struct wsd_cfg {
	float ring_cm;  /* the ring a near sound sits on (the game's 250) */
	float axis_cm;  /* within this horizontal distance of the head's vertical line the direction leans to the front */
	float steep;    /* ... and within steep * the distance (0.5: steeper than 60 deg) */
	float min_cm;   /* a nearer sound is put this far (0 = its own distance) */
};

/* the game's rework (both steps, the exe's float order), in place */
static inline void wsd_game_rework(const float A[3], const float B[3], float E[3])
{
	float ax = A[0] - E[0], ay = A[1] - E[1], az = A[2] - E[2];
	float da = sqrtf(ax * ax + ay * ay + az * az);
	float bx = B[0] - E[0], by = B[1] - E[1], bz = B[2] - E[2];
	float r = sqrtf(bx * bx + by * by + bz * bz);
	if (WSD_GAME_MIN_CM > r)
		r = WSD_GAME_MIN_CM;
	if (da > r) {
		float vx = E[0] - A[0], vy = E[1] - A[1], vz = E[2] - A[2], l = sqrtf(vx * vx + vy * vy + vz * vz);
		if (l > 0.0f)
			vx *= r / l, vy *= r / l, vz *= r / l;
		else
			vx = r;
		E[0] = A[0] + vx, E[1] = A[1] + vy, E[2] = A[2] + vz;
	}
	float hx = E[0] - A[0], hz = E[2] - A[2], h = sqrtf(hx * hx + hz * hz);
	if (WSD_GAME_RING_CM > h) {
		if (h > 0.0f)
			hx *= WSD_GAME_RING_CM / h, hz *= WSD_GAME_RING_CM / h;
		else
			hx = WSD_GAME_RING_CM;
		E[0] = A[0] + hx, E[1] = A[1], E[2] = A[2] + hz;
	}
}

/* the head's forward on the ground (unit x, z) from its front F and up U: F's own part, and when the head looks
 * steeply down (or up) its up axis's part, which then points the way the face is turned: never zero for a real pose */
static inline void wsd_fwd_h(const float F[3], const float U[3], float *fx, float *fz)
{
	float x = F[0] - F[1] * U[0], z = F[2] - F[1] * U[2], l = sqrtf(x * x + z * z);
	if (!(l > 1e-4f)) {
		*fx = 0.0f, *fz = 1.0f;
		return;
	}
	*fx = x / l, *fz = z / l;
}

/* 1 = E is near the vertical line through the head at A: its horizontal bearing is noise (the lean applies) */
static inline int wsd_on_axis(const struct wsd_cfg *c, const float A[3], const float F[3], const float U[3],
			      const float E[3]);

/* the head's frame: right = U x F (Wwise is left-handed, +Y up), up U, front F; v's parts along them */
static inline void wsd_head_parts(const float F[3], const float U[3], const float v[3], float *x, float *y, float *z)
{
	float R[3] = { U[1] * F[2] - U[2] * F[1], U[2] * F[0] - U[0] * F[2], U[0] * F[1] - U[1] * F[0] };
	*x = v[0] * R[0] + v[1] * R[1] + v[2] * R[2];
	*y = v[0] * U[0] + v[1] * U[1] + v[2] * U[2];
	*z = v[0] * F[0] + v[1] * F[1] + v[2] * F[2];
}

/* 1 = E is near the head's own up-down line (A, along U): its left-right part (what a stereo mix pans by) is noise */
static inline int wsd_on_axis(const struct wsd_cfg *c, const float A[3], const float F[3], const float U[3],
			      const float E[3])
{
	float v[3] = { E[0] - A[0], E[1] - A[1], E[2] - A[2] }, x, y, z;
	wsd_head_parts(F, U, v, &x, &y, &z);
	float d = sqrtf(x * x + y * y + z * z), h = sqrtf(x * x + z * z);
	float hn = c->steep * d > c->axis_cm ? c->steep * d : c->axis_cm;
	return h < hn;
}

/* the VR placement of a sound at E for a listener at A whose head looks along F (up U), in the head's own frame: the
 * left-right part gone near the head's up-down line (none within 0.6 r, a smoothstep to all of it at r = max(axis_cm,
 * steep x the distance)), the front-back part kept (at least (1 - s) r / 2 when it is smaller: never noise), the
 * up-down part kept; then at its own distance, min_cm at least - or with ring (the game's step 2 on for it) ring_cm at
 * least, at the listener's height. out may be E. Returns 1 if it was moved */
static inline int wsd_vr_place(const struct wsd_cfg *c, const float A[3], const float F[3], const float U[3],
			       const float E[3], float out[3], int ring)
{
	float v[3] = { E[0] - A[0], E[1] - A[1], E[2] - A[2] }, x, y, z;
	float d = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (!isfinite(d)) { /* a NaN position: the game's as it was */
		out[0] = E[0], out[1] = E[1], out[2] = E[2];
		return 0;
	}
	wsd_head_parts(F, U, v, &x, &y, &z);
	float h = sqrtf(x * x + z * z);
	float hn = c->steep * d > c->axis_cm ? c->steep * d : c->axis_cm;
	float sv = hn > 0.0f && h < hn ? h / hn : 1.0f, m = (1.0f - sv) * hn * 0.5f;
	float t = (sv - 0.6f) / 0.4f, k = t <= 0.0f ? 0.0f : t >= 1.0f ? 1.0f : t * t * (3.0f - 2.0f * t);
	float x2 = x * k, z2 = z >= m || z <= -m ? z : (z >= 0.0f ? m : -m);
	float R[3] = { U[1] * F[2] - U[2] * F[1], U[2] * F[0] - U[0] * F[2], U[0] * F[1] - U[1] * F[0] };
	float n[3];
	for (int i = 0; i < 3; i++)
		n[i] = x2 * R[i] + y * U[i] + z2 * F[i];
	float dist = d;
	if (ring && d < c->ring_cm)
		dist = c->ring_cm, n[1] = 0.0f;
	else if (d < c->min_cm)
		dist = c->min_cm;
	float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
	if (!(l > 0.0f)) /* exactly on A with no axis zone: ahead */
		n[0] = F[0], n[1] = F[1], n[2] = F[2], l = sqrtf(F[0] * F[0] + F[1] * F[1] + F[2] * F[2]);
	if (!(l > 0.0f)) {
		out[0] = E[0], out[1] = E[1], out[2] = E[2];
		return 0;
	}
	out[0] = A[0] + n[0] * dist / l, out[1] = A[1] + n[1] * dist / l, out[2] = A[2] + n[2] * dist / l;
	return sv < 1.0f || dist != d;
}

/* 1 = the placement moves E (near the head's up-down line, or nearer than the least distance) */
static inline int wsd_needs_place(const struct wsd_cfg *c, const float A[3], const float F[3], const float U[3],
				  const float E[3], int ring)
{
	float vx = E[0] - A[0], vy = E[1] - A[1], vz = E[2] - A[2];
	float d = sqrtf(vx * vx + vy * vy + vz * vz);
	return wsd_on_axis(c, A, F, U, E) || d < (ring ? c->ring_cm : c->min_cm);
}

/* where P is for a head at A looking along F (up U): the bearing (deg, + = right, 0 = ahead; the angle Wwise pans by),
 * the elevation (deg, + = up), the distance (cm). Wwise's frame is left-handed, +Y up: right = U x F */
static inline void wsd_bearing(const float A[3], const float F[3], const float U[3], const float P[3], float *bear,
			      float *elev, float *dist)
{
	float v[3] = { P[0] - A[0], P[1] - A[1], P[2] - A[2] };
	float R[3] = { U[1] * F[2] - U[2] * F[1], U[2] * F[0] - U[0] * F[2], U[0] * F[1] - U[1] * F[0] };
	float x = v[0] * R[0] + v[1] * R[1] + v[2] * R[2], y = v[0] * U[0] + v[1] * U[1] + v[2] * U[2],
	      z = v[0] * F[0] + v[1] * F[1] + v[2] * F[2];
	*dist = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	*bear = atan2f(x, z) * 57.29578f;
	*elev = atan2f(y, sqrtf(x * x + z * z)) * 57.29578f;
}

#endif
