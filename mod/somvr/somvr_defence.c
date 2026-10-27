/* somvr_defence.c - SOMVR_DEFENCE's pure judge (somvr_defence.h; docs/re-xr-pipeline.md 22). */
#include "somvr_defence.h"
#include "somvr_combat.h" /* sc_seg_dist */

#include <math.h>
#include <string.h>

#define DF_DEG 0.017453292519943295

void somvr_df_cfg_default(somvr_df_cfg *c)
{
	memset(c, 0, sizeof *c);
	c->way_cm = 20, c->front_cm = 10, c->cross_deg = 35, c->align_deg = 50, c->swing_cmps = 100, c->reach_cm = 300;
	c->body_top_cm = 15, c->body_bot_cm = 70;
	c->parry_s = 0.30f, c->lead_s = 0.033f, c->grace_s = 0.05f;
	c->dodge_cm = 30, c->back_cm = 40;
	c->arrow_cm = 12, c->arrow_step_s = 0.025f, c->arrow_near_cm = 150;
	c->parry_on = 1, c->block_on = 1, c->red_block = 0;
	c->contact = 0, c->contact_cm = 10, c->contact_pre_s = 0.25f, c->contact_post_s = 0.08f, c->contact_cross_deg = 15;
	c->red_types = 1u << SOMVR_DF_TYPE_NOPARRY | 1u << SOMVR_DF_TYPE_NOBLOCK;
}

int somvr_df_can_parry(int type) { return type == SOMVR_DF_TYPE_NORMAL || type == SOMVR_DF_TYPE_NOBLOCK; }
int somvr_df_can_block(const somvr_df_cfg *c, int type)
{
	if (type < SOMVR_DF_TYPE_NORMAL || type > SOMVR_DF_TYPE_NOCOUNTER)
		return 0; /* cannot block, ranged, unknown: never */
	if (type == SOMVR_DF_TYPE_NOPARRY && c->red_block)
		return 1;
	return !(c->red_types >> type & 1u);
}
int somvr_df_is_red(const somvr_df_cfg *c, int type) { return somvr_df_can_dodge(type) && !somvr_df_can_block(c, type); }

int somvr_df_red_parse(const char *v, unsigned *bits)
{
	static const char *const w[] = { "normal", "parry", "counter", "block" };
	*bits = 0;
	if (!v)
		return 1;
	const char *p = v;
	for (;;) {
		while (*p == ' ' || *p == ',' || *p == '\t')
			p++;
		if (!*p)
			return 1;
		size_t n = 0;
		while (p[n] && p[n] != ',' && p[n] != ' ' && p[n] != '\t')
			n++;
		int k = n == 4 && !strncmp(p, "none", 4) ? 4 : -1;
		for (int i = 0; i < 4 && k < 0; i++)
			if (strlen(w[i]) == n && !strncmp(p, w[i], n))
				k = i;
		if (k < 0)
			return *bits = 0, 0;
		if (k < 4)
			*bits |= 1u << k;
		p += n;
	}
}
int somvr_df_can_dodge(int type) { return type >= SOMVR_DF_TYPE_NORMAL && type <= SOMVR_DF_TYPE_NOBLOCK; }

const char *somvr_df_type_name(int type)
{
	static const char *const n[] = { "normal", "cannot-parry", "cannot-counter", "cannot-block", "ranged" };
	return type >= 0 && type <= SOMVR_DF_TYPE_RANGED ? n[type] : "unknown";
}
const char *somvr_df_outcome_name(int o)
{
	static const char *const n[SOMVR_DFO_N] = { "hit", "parry", "block", "parry-block", "dodge", "arrow" };
	return o >= 0 && o < SOMVR_DFO_N ? n[o] : "?";
}

static int fin3(const float v[3]) { return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]); }
static float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static float len3(const float a[3]) { return sqrtf(dot3(a, a)); }
static float clamp1(float x) { return x > 1 ? 1 : x < -1 ? -1 : x; }

somvr_df_guard somvr_df_guard_eval(const somvr_df_cfg *c, const float base[3], const float tip[3], const float hand[3],
				   const float hand_v[3], const float head[3])
{
	somvr_df_guard g;
	memset(&g, 0, sizeof g);
	g.align_deg = -1;
	if (!base || !tip || !hand || !head || !fin3(base) || !fin3(tip) || !fin3(hand) || !fin3(head))
		return g;
	float hh[3] = { hand[0] - head[0], hand[1] - head[1], hand[2] - head[2] };
	if (!(len3(hh) <= c->reach_cm)) /* not a melee blow at Talion (an archer's hand, a thrower's) */
		return g;
	/* the blow's target: the point of Talion's body (a vertical segment below the head) nearest the weapon hand */
	float top = head[1] - c->body_top_cm, bot = head[1] - c->body_bot_cm, y = hand[1];
	float T[3] = { head[0], y > top ? top : y < bot ? bot : y, head[2] };
	float u[3] = { hand[0] - T[0], hand[1] - T[1], hand[2] - T[2] }, L = len3(u);
	float bd[3] = { tip[0] - base[0], tip[1] - base[1], tip[2] - base[2] }, bl = len3(bd);
	if (!(L >= 1) || !(bl >= 1))
		return g;
	for (int i = 0; i < 3; i++)
		u[i] /= L, bd[i] /= bl;
	float front = c->front_cm < 0.5f * L ? c->front_cm : 0.5f * L;
	float p0[3] = { T[0] + u[0] * front, T[1] + u[1] * front, T[2] + u[2] * front }, sb = 0, sl = 0;
	g.dist_cm = sc_seg_dist(base, tip, p0, hand, &sb, &sl);
	g.cross_deg = (float)(acos(fabs((double)clamp1(dot3(bd, u)))) / DF_DEG);
	g.in_way = g.dist_cm <= c->way_cm && g.cross_deg >= c->cross_deg;
	for (int i = 0; i < 3; i++)
		g.point[i] = base[i] + bd[i] * bl * sb, g.target[i] = T[i];
	if (hand_v && fin3(hand_v)) {
		float sp = len3(hand_v);
		float n[3] = { hand_v[1] * u[2] - hand_v[2] * u[1], hand_v[2] * u[0] - hand_v[0] * u[2],
			       hand_v[0] * u[1] - hand_v[1] * u[0] };
		float nl = len3(n);
		if (sp >= c->swing_cmps && nl >= 0.3f * sp) { /* the swing is not along the line (a thrust) */
			for (int i = 0; i < 3; i++)
				n[i] /= nl;
			g.swing_known = 1;
			g.align_deg = (float)(acos(fabs((double)clamp1(dot3(bd, n)))) / DF_DEG);
		}
	}
	g.aligned = g.in_way && (!g.swing_known || g.align_deg <= c->align_deg);
	g.valid = 1;
	return g;
}

int somvr_df_parry_zone(const somvr_df_cfg *c, float bpw, float epw, float ts)
{
	if (!(bpw >= 0) || !(epw > bpw) || !isfinite(ts))
		return 0;
	float lo = epw - c->parry_s > bpw ? epw - c->parry_s : bpw;
	return ts >= lo && ts <= epw - c->lead_s;
}

void somvr_df_track_init(somvr_df_track *tr)
{
	memset(tr, 0, sizeof *tr);
	tr->way_t = tr->dodge_t = tr->parry_t = -1;
	tr->g.align_deg = -1;
	tr->contact_t = -1, tr->best_cm = INFINITY, tr->best_ts = -1, tr->drawn_best_cm = INFINITY;
}

int somvr_df_contact_window(const somvr_df_cfg *c, float impact, float ts)
{
	return impact >= 0 && isfinite(ts) && ts >= impact - c->contact_pre_s && ts <= impact + c->contact_post_s;
}

float somvr_df_sweep(const float b0[3], const float t0[3], const float b1[3], const float t1[3], const float o0[3],
		     const float p0[3], const float o1[3], const float p1[3], int have_prev, int steps, float *cross_deg,
		     float point[3])
{
	float best = INFINITY;
	*cross_deg = 0;
	if (!fin3(b1) || !fin3(t1) || !fin3(o1) || !fin3(p1))
		return best;
	if (!have_prev || !fin3(b0) || !fin3(t0) || !fin3(o0) || !fin3(p0))
		steps = 0;
	int n = steps > 0 ? steps : 0;
	for (int k = 0; k <= n; k++) { /* n = 0: the current poses alone */
		float u = n ? (float)k / (float)n : 1, bb[3], bt[3], oo[3], op[3];
		for (int i = 0; i < 3; i++) {
			bb[i] = n ? b0[i] + (b1[i] - b0[i]) * u : b1[i], bt[i] = n ? t0[i] + (t1[i] - t0[i]) * u : t1[i];
			oo[i] = n ? o0[i] + (o1[i] - o0[i]) * u : o1[i], op[i] = n ? p0[i] + (p1[i] - p0[i]) * u : p1[i];
		}
		float sb = 0, so = 0, d = sc_seg_dist(bb, bt, oo, op, &sb, &so);
		if (!(d < best))
			continue;
		best = d;
		float da[3] = { bt[0] - bb[0], bt[1] - bb[1], bt[2] - bb[2] }, db[3] = { op[0] - oo[0], op[1] - oo[1], op[2] - oo[2] };
		float la = len3(da), lb = len3(db);
		*cross_deg = la > 0 && lb > 0 ? (float)(acos(fabs((double)clamp1(dot3(da, db) / (la * lb)))) / DF_DEG) : 0;
		for (int i = 0; i < 3; i++)
			point[i] = bb[i] + da[i] * sb;
	}
	return best;
}

int somvr_df_step(const somvr_df_cfg *c, somvr_df_track *tr, double t, const somvr_df_in *in)
{
	if (!tr->started) {
		somvr_df_track_init(tr);
		tr->started = 1, tr->t0 = t;
		memcpy(tr->head0, in->head, sizeof tr->head0), memcpy(tr->orc0, in->orc, sizeof tr->orc0);
		float dx = in->head[0] - in->orc[0], dz = in->head[2] - in->orc[2], n = sqrtf(dx * dx + dz * dz);
		tr->dist0 = isfinite(n) ? n : 0;
		if (n >= 1 && isfinite(n))
			tr->side0[0] = -dz / n, tr->side0[2] = dx / n;
		else
			tr->side0[0] = 1;
	} else if (fin3(in->moved) && (in->moved[0] != 0 || in->moved[2] != 0)) {
		/* the game moved Talion (boot-check-20261024-020824: a knockback 85 cm back judged a dodge): the start goes
		 * with him, its distance from the orc by as much as his */
		float hx = in->head[0] - tr->orc0[0], hz = in->head[2] - tr->orc0[2];
		float px = hx - in->moved[0], pz = hz - in->moved[2], d = sqrtf(hx * hx + hz * hz) - sqrtf(px * px + pz * pz);
		for (int i = 0; i < 3; i++)
			tr->head0[i] += in->moved[i];
		tr->moved_cm += hypotf(in->moved[0], in->moved[2]);
		if (isfinite(d))
			tr->dist0 += d;
	}
	/* the weapon hand's velocity: the difference over the last step, smoothed; none across a gap of 0.1 s */
	double dt = t - tr->hand_t;
	if (tr->have_hand && dt > 0.004 && fin3(in->hand)) {
		if (dt <= 0.1) {
			for (int i = 0; i < 3; i++) {
				float v = (float)((in->hand[i] - tr->hand[i]) / dt);
				tr->hand_v[i] = tr->have_v ? 0.5f * (tr->hand_v[i] + v) : v;
			}
			tr->have_v = 1;
		} else
			tr->have_v = 0;
	}
	if (fin3(in->hand) && (!tr->have_hand || dt > 0.004))
		memcpy(tr->hand, in->hand, sizeof tr->hand), tr->hand_t = t, tr->have_hand = 1;
	if (in->blade)
		tr->g = somvr_df_guard_eval(c, in->base, in->tip, in->hand, tr->have_v ? tr->hand_v : NULL, in->head);
	else {
		memset(&tr->g, 0, sizeof tr->g);
		tr->g.align_deg = -1;
	}
	if (tr->g.valid && tr->g.in_way)
		tr->way_t = t;
	/* the dodge: the head's displacement since the start, across the orc's direction then and away from where the orc
	 * stood then (boot-check-20261024-024023: an infantry's 'Front_2_LL' steps the orc back 80 cm: 19 false dodges) */
	float dh[3] = { in->head[0] - tr->head0[0], 0, in->head[2] - tr->head0[2] };
	float hx = in->head[0] - tr->orc0[0], hz = in->head[2] - tr->orc0[2];
	tr->lateral_cm = fabsf(dh[0] * tr->side0[0] + dh[2] * tr->side0[2]);
	tr->back_cm = sqrtf(hx * hx + hz * hz) - tr->dist0;
	if ((c->dodge_cm > 0 && tr->lateral_cm >= c->dodge_cm) || (c->back_cm > 0 && tr->back_cm >= c->back_cm))
		tr->dodge_t = t;
	if (c->contact) { /* build 10: the blades meet in the impact window, or nothing (no parry at the wind-up) */
		int fire = 0, win = somvr_df_contact_window(c, in->impact, in->ts);
		int prev = tr->have_prev && tr->prev_norc == in->norc;
		if (in->blade) { /* build 11: how far the guard moved since the attack began: the grip in the tracking space
				   * (a snap turn moved the blade 20 cm against the head: boot-check-20261025-004916), else the
				   * blade's middle against the head */
			float m[3];
			for (int i = 0; i < 3; i++)
				m[i] = in->have_grip ? in->grip[i] : 0.5f * (in->base[i] + in->tip[i]) - in->head[i];
			if (!tr->have_m0)
				memcpy(tr->m0, m, sizeof m), tr->have_m0 = 1;
			float dm = sqrtf((m[0] - tr->m0[0]) * (m[0] - tr->m0[0]) + (m[1] - tr->m0[1]) * (m[1] - tr->m0[1]) +
					 (m[2] - tr->m0[2]) * (m[2] - tr->m0[2]));
			if (dm > tr->blade_cm)
				tr->blade_cm = dm;
			if (c->move_blade && in->have_tip_xr && fin3(in->tip_xr)) { /* build 17: the tip's move counts too */
				if (!tr->have_t0)
					memcpy(tr->tip0, in->tip_xr, sizeof tr->tip0), tr->have_t0 = 1;
				float dx = in->tip_xr[0] - tr->tip0[0], dy = in->tip_xr[1] - tr->tip0[1], dz = in->tip_xr[2] - tr->tip0[2];
				float dtip = sqrtf(dx * dx + dy * dy + dz * dz);
				if (dtip > tr->blade_cm)
					tr->blade_cm = dtip;
			}
		}
		int hand = c->contact_hand && in->have_hand_blade && fin3(in->hb) && fin3(in->ht);
		int hprev = hand && tr->have_hprev && tr->prev_norc == in->norc;
		for (int k = 0; win && in->blade && k < in->norc && k < 2 && tr->contact_t < 0; k++) {
			float cross = 0, pt[3] = { 0, 0, 0 };
			float d = somvr_df_sweep(tr->pb, tr->pt, in->base, in->tip, tr->pob[k], tr->pot[k], in->ob[k], in->ot[k], prev,
						 SOMVR_DF_SWEEP_STEPS, &cross, pt);
			int by = 1;
			if (d < tr->drawn_best_cm)
				tr->drawn_best_cm = d;
			if (hand) { /* build 17: the blade where the hand holds it: the nearer of the two (the drawn one first) */
				float hc = 0, hp[3] = { 0, 0, 0 };
				float hd = somvr_df_sweep(tr->phb, tr->pht, in->hb, in->ht, tr->pob[k], tr->pot[k], in->ob[k], in->ot[k],
							  hprev && prev, SOMVR_DF_SWEEP_STEPS, &hc, hp);
				if (hd < d && !(d <= c->contact_cm && cross >= c->contact_cross_deg))
					d = hd, cross = hc, memcpy(pt, hp, sizeof pt), by = 2;
			}
			if (d < tr->best_cm)
				tr->best_cm = d, tr->best_ts = in->ts, tr->best_cross = cross;
			int met = d <= c->contact_cm && cross >= c->contact_cross_deg && somvr_df_can_block(c, in->type) && c->block_on;
			if (met && c->contact_move_cm > 0 && tr->blade_cm < c->contact_move_cm) { /* a still guard: through it */
				if (!tr->still)
					tr->still = 1, tr->still_cm = d;
				met = 0;
			}
			if (met) {
				tr->met_by = by;
				tr->contact_t = t, tr->contact_cm = d, tr->contact_cross = cross, tr->contact_ts = in->ts;
				memcpy(tr->contact_pt, pt, sizeof pt);
				fire = 2;
			}
		}
		tr->windowed += win;
		tr->have_hprev = hand && in->norc > 0;
		if (tr->have_hprev)
			memcpy(tr->phb, in->hb, sizeof tr->phb), memcpy(tr->pht, in->ht, sizeof tr->pht);
		tr->have_prev = in->blade && in->norc > 0;
		if (tr->have_prev) {
			memcpy(tr->pb, in->base, sizeof tr->pb), memcpy(tr->pt, in->tip, sizeof tr->pt);
			memcpy(tr->pob, in->ob, sizeof tr->pob), memcpy(tr->pot, in->ot, sizeof tr->pot), tr->prev_norc = in->norc;
		}
		return fire;
	}
	if (!tr->parried && c->parry_on && in->blade && tr->g.valid && tr->g.aligned && somvr_df_can_parry(in->type) &&
	    in->window != 0 && somvr_df_parry_zone(c, in->bpw, in->epw, in->ts)) {
		tr->parried = 1, tr->parry_t = t;
		return 1;
	}
	return 0;
}

int somvr_df_verdict(const somvr_df_cfg *c, const somvr_df_track *tr, double t, int type)
{
	if (!tr || !tr->started)
		return SOMVR_DFO_HIT;
	if (c->contact) { /* build 10: a block only after the blades met; else a dodge, or the hit */
		if (tr->contact_t >= 0 && somvr_df_can_block(c, type))
			return SOMVR_DFO_BLOCK;
		if (somvr_df_can_dodge(type) && tr->dodge_t >= 0 && t - tr->dodge_t <= c->grace_s + 1e-9)
			return SOMVR_DFO_DODGE;
		return SOMVR_DFO_HIT;
	}
	if (tr->parried && somvr_df_can_block(c, type)) /* the blade was in the way inside the window */
		return SOMVR_DFO_PARRY_BLOCK;
	if (c->block_on && somvr_df_can_block(c, type) && tr->way_t >= 0 && t - tr->way_t <= c->grace_s + 1e-9)
		return SOMVR_DFO_BLOCK;
	if (somvr_df_can_dodge(type) && tr->dodge_t >= 0 && t - tr->dodge_t <= c->grace_s + 1e-9)
		return SOMVR_DFO_DODGE;
	return SOMVR_DFO_HIT;
}

int somvr_df_press_start(somvr_df_press *p, int cmd, int ticks, double t, float cool_s)
{
	if (cmd <= 0 || ticks <= 0)
		return 0;
	if (p->left > 0 || t < p->next_ok) {
		p->refused++;
		return 0;
	}
	p->cmd = cmd, p->left = ticks, p->t0 = t, p->next_ok = t + cool_s, p->presses++;
	return 1;
}
int somvr_df_press_tick(somvr_df_press *p, double t, float max_s)
{
	if (p->left <= 0)
		return 0;
	if (!(t - p->t0 <= max_s)) { /* too late for the window it was for */
		p->left = 0, p->stale++;
		return 0;
	}
	p->left--;
	return p->cmd;
}

int somvr_df_arrow(const somvr_df_cfg *c, const float pos[3], const float dir[3], float speed, const float head[3],
		   const float b1[3], const float t1[3], const float b0[3], const float t0[3], int have_prev,
		   float *dist_cm, float point[3])
{
	*dist_cm = INFINITY;
	if (!(c->arrow_cm > 0) || !fin3(pos) || !fin3(dir) || !fin3(head) || !fin3(b1) || !fin3(t1) || !isfinite(speed))
		return 0;
	float dl = len3(dir);
	if (!(dl > 0.5f))
		return 0;
	float d[3] = { dir[0] / dl, dir[1] / dl, dir[2] / dl }, w[3] = { head[0] - pos[0], head[1] - pos[1], head[2] - pos[2] };
	float along = dot3(w, d);
	float off[3] = { w[0] - d[0] * along, w[1] - d[1] * along, w[2] - d[2] * along };
	if (!(along > 0) || len3(off) > c->arrow_near_cm) /* going away, or passing far from the head */
		return 0;
	float step = speed * c->arrow_step_s;
	if (!(step >= 1))
		step = 1;
	float q[3] = { pos[0] + d[0] * step, pos[1] + d[1] * step, pos[2] + d[2] * step };
	const float *bs[3] = { b1, NULL, NULL }, *ts[3] = { t1, NULL, NULL };
	float mb[3], mt[3];
	int nb = 1;
	if (have_prev && b0 && t0 && fin3(b0) && fin3(t0)) {
		for (int i = 0; i < 3; i++)
			mb[i] = 0.5f * (b0[i] + b1[i]), mt[i] = 0.5f * (t0[i] + t1[i]);
		bs[1] = b0, ts[1] = t0, bs[2] = mb, ts[2] = mt, nb = 3;
	}
	int hit = 0;
	for (int k = 0; k < nb; k++) {
		float sb, sa, dd = sc_seg_dist(bs[k], ts[k], pos, q, &sb, &sa);
		if (dd < *dist_cm) {
			*dist_cm = dd;
			for (int i = 0; i < 3; i++)
				point[i] = bs[k][i] + (ts[k][i] - bs[k][i]) * sb;
		}
		hit |= dd <= c->arrow_cm;
	}
	return hit;
}

somvr_df_haptic somvr_df_haptic_of(int outcome, int sword, float scale)
{
	/* {sword amp, sword s, off amp, off s}: the parry a sharp crack in the sword hand and a knock in the other, the
	 * block a longer clank, the dodge a soft sweep in both, the arrow a short tick */
	static const float tab[SOMVR_DFO_N][4] = {
		{ 0, 0, 0, 0 }, { 0.9f, 0.06f, 0.35f, 0.04f }, { 0.75f, 0.09f, 0, 0 }, { 0.75f, 0.09f, 0, 0 },
		{ 0.25f, 0.12f, 0.25f, 0.12f }, { 0.6f, 0.04f, 0, 0 },
	};
	somvr_df_haptic h;
	memset(&h, 0, sizeof h);
	if (outcome < 0 || outcome >= SOMVR_DFO_N || !(scale > 0))
		return h;
	float k = scale > 1 ? 1 : scale;
	int s = sword ? 1 : 0;
	h.amp[s] = tab[outcome][0] * k, h.dur_s[s] = tab[outcome][1];
	h.amp[1 - s] = tab[outcome][2] * k, h.dur_s[1 - s] = tab[outcome][3];
	return h;
}

/* ---- the parry's clank ------------------------------------------------------------------------------------------- */

static const char *const g_dfc_w[] = { "recoil", "late", "stop", "haptic", "fx" };

int somvr_df_clank_parse(const char *v, unsigned *bits)
{
	*bits = 0;
	if (!v)
		return 1;
	char w[32];
	const char *p = v;
	for (;;) {
		while (*p == ' ' || *p == '\t' || *p == ',')
			p++;
		if (!*p)
			break;
		size_t n = 0;
		while (p[n] && p[n] != ',' && p[n] != ' ' && p[n] != '\t')
			n++;
		if (n >= sizeof w)
			return *bits = 0, 0;
		for (size_t i = 0; i < n; i++)
			w[i] = (char)(p[i] >= 'A' && p[i] <= 'Z' ? p[i] - 'A' + 'a' : p[i]);
		w[n] = 0;
		p += n;
		unsigned b = 0;
		if (!strcmp(w, "on") || !strcmp(w, "all") || !strcmp(w, "1"))
			b = SOMVR_DFC_ALL;
		else if (!strcmp(w, "off") || !strcmp(w, "0") || !strcmp(w, "none"))
			b = 0;
		else {
			for (unsigned k = 0; k < sizeof g_dfc_w / sizeof g_dfc_w[0] && !b; k++)
				if (!strcmp(w, g_dfc_w[k]))
					b = 1u << k;
			if (!b)
				return *bits = 0, 0;
		}
		*bits |= b;
	}
	return 1;
}

const char *somvr_df_clank_str(unsigned bits, char *out, unsigned cap)
{
	unsigned o = 0;
	if (!cap)
		return out;
	out[0] = 0;
	for (unsigned k = 0; k < sizeof g_dfc_w / sizeof g_dfc_w[0]; k++) {
		if (!(bits & (1u << k)))
			continue;
		size_t n = strlen(g_dfc_w[k]);
		if (o + (o ? 1 : 0) + n + 1 > cap)
			break;
		if (o)
			out[o++] = ',';
		memcpy(out + o, g_dfc_w[k], n + 1);
		o += (unsigned)n;
	}
	if (!o && cap >= 4)
		memcpy(out, "off", 4);
	return out;
}

int somvr_df_may_interrupt(const somvr_df_irules *r, const uintptr_t *cats, int n)
{
	if (!r || !r->known)
		return 1;
	int in = 0;
	for (int i = 0; i < n && !in && cats; i++)
		for (int k = 0; k < r->nexc && !in; k++)
			in = cats[i] && cats[i] == r->exc[k];
	return r->can ? !in : in;
}

int somvr_df_pick_interrupter(const somvr_df_irules *r, const uintptr_t *const *cand_cats, const int *cand_n, int n)
{
	for (int i = 0; i < n; i++)
		if (cand_cats && cand_n && somvr_df_may_interrupt(r, cand_cats[i], cand_n[i]))
			return i;
	return -1;
}

somvr_df_clank_haptic somvr_df_clank_haptic_of(float scale)
{
	somvr_df_clank_haptic h;
	memset(&h, 0, sizeof h);
	if (!(scale > 0))
		return h;
	float k = scale > 1 ? 1 : scale;
	/* the sword hand: a full crack for 50 ms, then the blade's ring from 0.6 down to 0 over 0.25 s (the mixer's ramp);
	 * the other hand a knock (the parry's table gives 0.9 / 60 ms and 0.35 / 40 ms) */
	h.crack_amp = 1.0f * k, h.crack_s = 0.05f, h.ring_amp = 0.6f * k, h.ring_s = 0.25f;
	h.knock_amp = 0.45f * k, h.knock_s = 0.06f;
	return h;
}
