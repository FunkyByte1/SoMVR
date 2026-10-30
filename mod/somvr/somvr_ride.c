/* somvr_ride.c - SOMVR_RIDE's pure parts (somvr_ride.h; docs/re-xr-pipeline.md 105). */
#include "somvr_ride.h"

#include <math.h>
#include <string.h>
#include <strings.h>

const char *const somvr_rd_kind_names[SOMVR_RD_NKIND] = { "caragor", "graug" };

void somvr_rd_cfg_default(somvr_rd_cfg *c)
{
	memset(c, 0, sizeof *c);
	/* build 22's nodes (re-xr-pipeline.md 92.2): a caragor's Saddle 133 cm over its root, a graug's 358; a rider's eyes
	 * about 75 cm over the saddle */
	c->seat[SOMVR_RD_CARAGOR][1] = 208, c->seat[SOMVR_RD_CARAGOR][2] = 10;
	c->seat[SOMVR_RD_GRAUG][1] = 433;
	c->y_s[SOMVR_RD_CARAGOR] = 0.12f, c->y_s[SOMVR_RD_GRAUG] = 0.20f;
	c->off_s = 2.0f, c->off_cmps = 10, c->off_max_cm = 80;
	c->yaw_s = 0.10f, c->yaw_dps = 150, c->jump_rad = 2.0f, c->max_dt = 0.1f;
}

int somvr_rd_kind(const char *rec)
{
	if (!rec)
		return -1;
	if (strcasestr(rec, "Warg"))
		return SOMVR_RD_CARAGOR;
	if (strcasestr(rec, "Troll"))
		return SOMVR_RD_GRAUG;
	return -1;
}

static float wrap_pi(float a)
{
	if (!isfinite(a))
		return 0;
	a = fmodf(a + (float)M_PI, 2.0f * (float)M_PI);
	if (a < 0)
		a += 2.0f * (float)M_PI;
	return a - (float)M_PI;
}

void somvr_rd_yaw_init(somvr_rd_yaw *y) { memset(y, 0, sizeof *y); }

float somvr_rd_yaw_step(somvr_rd_yaw *y, const somvr_rd_cfg *c, float heading, float dt, float *rate_dps)
{
	*rate_dps = 0;
	if (!isfinite(heading))
		return 0;
	float d = y->have ? wrap_pi(heading - y->raw_last) : 0;
	if (!y->have || !(dt > 0) || dt > c->max_dt || fabsf(d) > c->jump_rad) { /* the start, a gap, a jump: no turn */
		y->have = 1, y->raw_last = heading, y->u = y->x = y->view = heading, y->v = 0, y->latches++;
		return 0;
	}
	y->raw_last = heading, y->u += d;
	/* critically damped toward u (held over the frame), exact: e(t) = (e0 + (v0 + w e0) t) e^-wt */
	float w = c->yaw_s > 0 ? 1.0f / c->yaw_s : 0;
	if (w > 0) {
		float e0 = y->x - y->u, v0 = y->v, k = expf(-w * dt);
		float e1 = (e0 + (v0 + w * e0) * dt) * k, v1 = (v0 - w * (v0 + w * e0) * dt) * k;
		y->x = y->u + e1, y->v = v1;
	} else
		y->x = y->u, y->v = 0;
	float step = y->x - y->view, cap = c->yaw_dps > 0 ? c->yaw_dps * 0.017453293f * dt : INFINITY;
	if (fabsf(step) > cap)
		step = copysignf(cap, step), y->capped++;
	y->view += step;
	if (fabsf(y->view) > 1000.0f) { /* keep the unwrapped numbers small (the same shift for all) */
		float s = 2.0f * (float)M_PI * floorf(y->view / (2.0f * (float)M_PI));
		y->view -= s, y->x -= s, y->u -= s;
	}
	*rate_dps = step / dt * 57.29578f;
	return step;
}

void somvr_rd_to_beast(const float p[3], const float beast[3], float heading, float o[3])
{
	float c = cosf(heading), s = sinf(heading), dx = p[0] - beast[0], dz = p[2] - beast[2];
	o[0] = dx * c - dz * s, o[1] = p[1] - beast[1], o[2] = dx * s + dz * c;
}
void somvr_rd_from_beast(const float l[3], const float beast[3], float heading, float o[3])
{
	float c = cosf(heading), s = sinf(heading);
	o[0] = beast[0] + l[0] * c + l[2] * s, o[1] = beast[1] + l[1], o[2] = beast[2] - l[0] * s + l[2] * c;
}

void somvr_rd_seat_init(somvr_rd_seat *s, const somvr_rd_cfg *c, int kind)
{
	memset(s, 0, sizeof *s);
	s->kind = kind >= 0 && kind < SOMVR_RD_NKIND ? kind : SOMVR_RD_CARAGOR;
	memcpy(s->off, c->seat[s->kind], sizeof s->off);
}

void somvr_rd_seat_step(somvr_rd_seat *s, const somvr_rd_cfg *c, const float beast[3], float heading, const float *head,
			float dt, float out[3])
{
	if (head && isfinite(head[0] + head[1] + head[2]) && c->off_s > 0) {
		float l[3], dd = 0, dr = 0;
		somvr_rd_to_beast(head, beast, heading, l);
		for (int i = 0; i < 3; i++) /* the first sample: anywhere on the beast (5 m of its root); then near the mean */
			dd += (l[i] - s->off[i]) * (l[i] - s->off[i]), dr += l[i] * l[i];
		if (!(s->have ? sqrtf(dd) <= c->off_max_cm : sqrtf(dr) <= 500)) {
			s->rejected++;
		} else if (!s->have) { /* the first sample on the seat (the view is held or black then) */
			memcpy(s->off, l, sizeof s->off), s->have = 1, s->samples++;
		} else if (dt > 0) {
			float k = 1.0f - expf(-(dt < c->max_dt ? dt : c->max_dt) / c->off_s), lim = c->off_cmps * dt, m[3], ml = 0;
			for (int i = 0; i < 3; i++)
				m[i] = (l[i] - s->off[i]) * k, ml += m[i] * m[i];
			ml = sqrtf(ml);
			float f = ml > lim && ml > 0 ? lim / ml : 1;
			for (int i = 0; i < 3; i++)
				s->off[i] += m[i] * f;
			s->samples++;
		}
	}
	somvr_rd_from_beast(s->off, beast, heading, out);
}

int somvr_rd_reached(float dx, float dz, float heading, float *cm, float *deg)
{
	float h = hypotf(dx, dz), fx = sinf(heading), fz = cosf(heading);
	*cm = h, *deg = h > 1 ? acosf(fmaxf(-1, fminf(1, (dx * fx + dz * fz) / h))) * 57.29578f : 180;
	return isfinite(h) && h >= 30 && *deg <= 75;
}

const char *const somvr_rd_strug_names[SOMVR_RDS_N] = { "none", "a hold", "a press count", "a quick-time press",
							 "an aim", "a stick direction" };

int somvr_rd_strug_kind(const char *rtti)
{
	if (!rtti)
		return SOMVR_RDS_NONE;
	return strstr(rtti, "CStruggleHold") ? SOMVR_RDS_HOLD : strstr(rtti, "CStrugglePressCount") ? SOMVR_RDS_COUNT :
	       strstr(rtti, "CStruggleQuickTime") ? SOMVR_RDS_QT : strstr(rtti, "CStruggleAim") ? SOMVR_RDS_AIM :
	       strstr(rtti, "CStruggleInputDirection") ? SOMVR_RDS_DIR : SOMVR_RDS_NONE;
}

void somvr_rd_strug_init(somvr_rd_strug *s, int kind, float win_s)
{
	memset(s, 0, sizeof *s), s->kind = kind, s->win_s = win_s;
}

int somvr_rd_strug_step(somvr_rd_strug *s, int hold, float dt)
{
	s->t += dt > 0 ? dt : 0;
	if (s->t < SOMVR_RD_STRUG_GAP_S) /* let go: the struggle's press must be a fresh one */
		return 0;
	int fresh = hold && !s->was;
	s->was = hold;
	switch (s->kind) {
	case SOMVR_RDS_HOLD:
		s->presses += fresh;
		return hold;
	case SOMVR_RDS_COUNT:
	case SOMVR_RDS_QT:
		if (fresh)
			s->pulse = SOMVR_RD_STRUG_PULSE_S, s->presses++;
		else if (s->pulse > 0)
			s->pulse -= dt;
		return s->pulse > 0;
	case SOMVR_RDS_AIM:
	case SOMVR_RDS_DIR:
		s->held = hold ? s->held + (dt > 0 ? dt : 0) : 0; /* let go: the beast shakes free, start again */
		if (s->win_s > 0 && s->held >= s->win_s && !s->won)
			s->won = 1;
		return 0;
	default:
		return 0;
	}
}

void somvr_rd_btn_init(somvr_rd_btn *b) { memset(b, 0, sizeof *b); }

uint32_t somvr_rd_btn_step(somvr_rd_btn *b, const somvr_rd_btn_cfg *c, int a, int bb, float rt, int click, float stick)
{
	float v = isfinite(rt) ? rt : 0;
	b->trig_dn = b->trig_dn ? v > c->trig_off : v > c->trig_on;
	if (!b->armed) { /* nothing fires until every button has been seen up */
		if (!a && !bb && !b->trig_dn && !click)
			b->armed = 1;
		return b->last = 0;
	}
	if (click)
		b->charge = 1;
	else if (b->charge && !(stick > 0.2f))
		b->charge = 0;
	uint32_t o = (a ? SOMVR_RDB_JUMP : 0u) | (b->trig_dn ? SOMVR_RDB_BITE : 0u) | (bb ? SOMVR_RDB_OFF : 0u) |
		     (b->charge ? SOMVR_RDB_CHARGE : 0u);
	return b->last = o;
}
