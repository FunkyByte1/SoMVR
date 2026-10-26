/* somvr_body.c - see somvr_body.h */
#include "somvr_body.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static sv_vec3 v3(float x, float y, float z) { return (sv_vec3){x, y, z}; }
static sv_vec3 vadd(sv_vec3 a, sv_vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static sv_vec3 vsub(sv_vec3 a, sv_vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static sv_vec3 vmul(sv_vec3 a, float k) { return v3(a.x * k, a.y * k, a.z * k); }
static float vdot(sv_vec3 a, sv_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float vlen(sv_vec3 a) { return sqrtf(vdot(a, a)); }
static sv_vec3 vcross(sv_vec3 a, sv_vec3 b) {
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static int vfinite(sv_vec3 a) { return isfinite(a.x) && isfinite(a.y) && isfinite(a.z); }
static int qfinite(sv_quat q) { return isfinite(q.x) && isfinite(q.y) && isfinite(q.z) && isfinite(q.w); }

/* the shortest rotation taking the unit vector u to the unit vector v (antiparallel: 180 deg about a normal of u) */
static sv_quat qarc(sv_vec3 u, sv_vec3 v) {
    float c = vdot(u, v);
    if (c < -0.999999f) {
        sv_vec3 a = vcross(u, fabsf(u.x) < 0.9f ? v3(1, 0, 0) : v3(0, 1, 0));
        float l = vlen(a);
        return (sv_quat){a.x / l, a.y / l, a.z / l, 0};
    }
    sv_vec3 a = vcross(u, v);
    return sv_qnormalize((sv_quat){a.x, a.y, a.z, 1.0f + c});
}

static sv_quat qslerp(sv_quat a, sv_quat b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) b = (sv_quat){-b.x, -b.y, -b.z, -b.w}, d = -d;
    if (d > 0.9995f)
        return sv_qnormalize((sv_quat){a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                                       a.w + (b.w - a.w) * t});
    float th = acosf(d), s = sinf(th), ka = sinf((1 - t) * th) / s, kb = sinf(t * th) / s;
    return sv_qnormalize((sv_quat){ka * a.x + kb * b.x, ka * a.y + kb * b.y, ka * a.z + kb * b.z, ka * a.w + kb * b.w});
}

/* ---- 1. hands only -------------------------------------------------------------------------------------------------- */

int sb_keep_build(sb_keep *k, const sa_chain ch[2], const uint8_t *parent, int n, char *why, size_t cap) {
    memset(k, 0, sizeof *k);
    k->wrist[0] = k->wrist[1] = -1;
    if (!ch || !parent || n < 2 || n > SA_MAXN) return snprintf(why, cap, "bad skeleton (%d nodes)", n), -1;
    if (parent[0] != 0xff) return snprintf(why, cap, "node 0 has a parent (%u)", parent[0]), -1;
    for (int i = 1; i < n; i++)
        if (parent[i] >= i) return snprintf(why, cap, "node %d's parent %u does not precede it", i, parent[i]), -1;
    for (int s = 0; s < 2; s++) {
        const sa_chain *c = &ch[s];
        if (c->side != s || c->w < 1 || c->w >= n || c->e < 1 || c->s < 1 || c->p < 0 || c->p >= n || parent[c->w] != c->e ||
            parent[c->e] != c->s)
            return snprintf(why, cap, "the %s chain does not fit this skeleton", s ? "right" : "left"), -1;
        k->wrist[s] = c->w;
        k->stub[s][0] = c->tf >= 0 && c->tf < n && parent[c->tf] == c->e ? c->tf : -1;
        k->stub[s][1] = c->e;
    }
    if (k->wrist[0] == k->wrist[1]) return snprintf(why, cap, "both wrists are node %d", k->wrist[0]), -1;
    uint8_t hand[SA_MAXN], larm[SA_MAXN];
    memset(hand, 0, sizeof hand), memset(larm, 0, sizeof larm);
    hand[k->wrist[0]] = SB_HAND_L, hand[k->wrist[1]] = SB_HAND_R;
    larm[ch[0].p] = 1; /* the left clavicle and everything below it */
    for (int i = 1; i < n; i++) { /* one forward pass: parents first */
        if (!hand[i]) hand[i] = hand[parent[i]];
        if (!larm[i]) larm[i] = larm[parent[i]];
    }
    for (int i = 0; i < n; i++) {
        k->kind[i] = hand[i];
        k->aside[i] = hand[i] ? (uint8_t)(hand[i] - 1) : larm[i] ? 0 : 1;
        if (hand[i]) k->nhand[hand[i] - 1]++;
        else k->nbody++;
    }
    if (k->kind[0] != SB_BODY) return snprintf(why, cap, "the root is in a hand's subtree"), -1;
    k->n = n;
    snprintf(why, cap, "hands L_Hand %d (%d nodes) R_Hand %d (%d nodes), the body %d nodes (left arm side from the clavicle "
             "%d)", k->wrist[0], k->nhand[0], k->wrist[1], k->nhand[1], k->nbody, ch[0].p);
    return 0;
}

int sb_skin_edit(sa_node *skin, const sa_node *model, int n, const sb_keep *k, unsigned show, int mode) {
    if (!skin || !model || !k || k->n != n || n < 2 || mode < 0 || mode >= SB_HIDE_N) return 0;
    if (mode == SB_HIDE_SHOW) return 0;
    int stubs = mode == SB_HIDE_CUFF || mode == SB_HIDE_CUFFIK ? 1 : mode == SB_HIDE_FOREARM ? 2 : 0;
    int line = mode == SB_HIDE_LINE || stubs, ik = mode == SB_HIDE_CUFFIK;
    float at[2][3];
    for (int s = 0; s < 2; s++) {
        const float *p = model[k->wrist[s]].pos;
        if (!isfinite(p[0]) || !isfinite(p[1]) || !isfinite(p[2])) return 0;
        memcpy(at[s], p, sizeof at[s]);
    }
    /* SB_HIDE_LINE: the line through both posed wrists (direction e); each side's wrist in the bind pose from the
     * game's own skin transform of it (skin = model x bind^-1, so bind = skin^-1 x model). With a stub: the line
     * through both stubs' ends instead, each the end node's bind point carried by its hand's skin transform. */
    sv_vec3 e = v3(0, 1, 0), base[2] = {v3(0, 0, 0), v3(0, 0, 0)}, ab[2], ap[2], gap[2] = {v3(0, 0, 0), v3(0, 0, 0)};
    int stub_on[2] = {0, 0};
    if (line) {
        for (int s = 0; s < 2; s++) {
            const sa_node *g = &skin[k->wrist[s]];
            sv_quat q = {g->q[0], g->q[1], g->q[2], g->q[3]};
            float qn = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
            int qok = qfinite(q) && qn > 0.5f && qn < 2.0f;
            sv_vec3 wb = v3(0, 0, 0), wp = v3(at[s][0], at[s][1], at[s][2]), gp = v3(g->pos[0], g->pos[1], g->pos[2]);
            if (qok) {
                q = (sv_quat){q.x / qn, q.y / qn, q.z / qn, q.w / qn};
                wb = sv_qrot(sv_qconj(q), vsub(wp, gp));
            }
            if (!vfinite(wb)) wb = v3(0, 0, 0);
            ab[s] = wb, ap[s] = wp;
            int a = stubs ? k->stub[s][stubs - 1] : -1;
            if (a >= 0 && a < n && qok && (show >> s & 1) && vfinite(gp)) { /* the end node's bind point, carried */
                const sa_node *ga = &skin[a];
                sv_quat qa = {ga->q[0], ga->q[1], ga->q[2], ga->q[3]};
                float qan = sqrtf(qa.x * qa.x + qa.y * qa.y + qa.z * qa.z + qa.w * qa.w);
                const float *mp = model[a].pos;
                /* the game's entry is a unit rotation (it writes the palette every frame: run 020044's node 0 before
                 * each edit); |q| 0.707 would be our own line edit: no stub from that */
                if (qfinite(qa) && qan > 0.9f && qan < 1.1f && isfinite(mp[0]) && isfinite(mp[1]) && isfinite(mp[2])) {
                    qa = (sv_quat){qa.x / qan, qa.y / qan, qa.z / qan, qa.w / qan};
                    sv_vec3 gap0 = v3(ga->pos[0], ga->pos[1], ga->pos[2]);
                    sv_vec3 b = sv_qrot(sv_qconj(qa), vsub(v3(mp[0], mp[1], mp[2]), gap0));
                    sv_vec3 p = vadd(gp, sv_qrot(q, b));
                    if (ik) { /* its own transform; the gap between where it carries the bind wrist and the posed wrist */
                        gap[s] = vsub(wp, vadd(gap0, sv_qrot(qa, wb)));
                        p = vadd(v3(mp[0], mp[1], mp[2]), gap[s]);
                    }
                    if (vfinite(b) && vfinite(p) && vfinite(gap[s])) ab[s] = b, ap[s] = p, stub_on[s] = 1;
                }
            }
        }
        sv_vec3 d = vsub(ap[1], ap[0]);
        float L = vlen(d);
        if (L > 1.0f && isfinite(L)) e = vmul(d, 1.0f / L);
        for (int s = 0; s < 2; s++) base[s] = vsub(ap[s], vmul(e, vdot(e, ab[s]))); /* pos + e e^T v = ap + e (e . (v - ab)) */
    }
    sa_node hand_skin[2] = {skin[k->wrist[0]], skin[k->wrist[1]]};
    int w = 0;
    for (int i = 0; i < n; i++) {
        int kind = k->kind[i];
        if (kind != SB_BODY && (show >> (kind - 1) & 1)) continue;
        sa_node *d = &skin[i];
        int side = kind != SB_BODY ? kind - 1 : k->aside[i];
        const float *a = at[side];
        if (stub_on[side] && kind == SB_BODY && (i == k->stub[side][0] || (stubs == 2 && i == k->stub[side][1]))) {
            if (ik) /* its own rotation, moved onto the hand */
                d->pos[0] += gap[side].x, d->pos[1] += gap[side].y, d->pos[2] += gap[side].z;
            else
                *d = hand_skin[side]; /* rigid with the hand */
            d->pad = 0;
            w++;
            continue;
        }
        if (line) {
            /* q = (e / sqrt 2, 0): the shader's rotation 1 - 2|u|^2 + 2 u u^T (+ 2 w [u]x) turns into the projection e e^T,
             * so every hidden vertex lands on that one line and their triangles have no area */
            d->pos[0] = base[side].x, d->pos[1] = base[side].y, d->pos[2] = base[side].z;
            d->q[0] = e.x * 0.70710678f, d->q[1] = e.y * 0.70710678f, d->q[2] = e.z * 0.70710678f, d->q[3] = 0;
        } else if (mode == SB_HIDE_NAN) {
            d->pos[0] = d->pos[1] = d->pos[2] = d->q[0] = d->q[1] = d->q[2] = d->q[3] = NAN;
        } else if (mode == SB_HIDE_FAR) {
            d->pos[0] = a[0], d->pos[1] = a[1] - SB_FAR_CM, d->pos[2] = a[2];
            d->q[0] = d->q[1] = d->q[2] = 0, d->q[3] = 1;
        } else {
            d->pos[0] = a[0], d->pos[1] = a[1], d->pos[2] = a[2];
            d->q[0] = d->q[1] = d->q[2] = d->q[3] = 0;
        }
        d->pad = 0;
        w++;
    }
    return w;
}

static const char *const g_hide_names[SB_HIDE_N] = {"zero", "nan", "far", "show", "line", "cuff", "forearm", "cuffik"};
const char *sb_hide_name(int mode) { return mode >= 0 && mode < SB_HIDE_N ? g_hide_names[mode] : "?"; }
int sb_hide_parse(const char *s) {
    for (int i = 0; s && i < SB_HIDE_N; i++)
        if (!strcmp(s, g_hide_names[i])) return i;
    return -1;
}

/* ---- 2. the grip ---------------------------------------------------------------------------------------------------- */

sv_vec3 sb_blade_dir(sv_quat q, float a) { return sv_qrot(q, v3(0, sinf(a), cosf(a))); }

sa_xf sb_cal_angle(sa_xf cal, float a) {
    sa_xf r = {v3(0, 0, 0), (sv_quat){sinf(-0.5f * a), 0, 0, cosf(-0.5f * a)}};
    return sa_xf_mul(r, cal);
}

/* ---- 3. two hands --------------------------------------------------------------------------------------------------- */

void sb_tw_cfg_default(sb_tw_cfg *c) {
    c->grab_r_cm = 12, c->hilt_lo_cm = 3, c->hilt_hi_cm = 28, c->hold_lo_cm = 8, c->hold_hi_cm = 24, c->break_cm = 45;
    c->squeeze_on = 0.6f, c->squeeze_off = 0.35f, c->blend_s = 0.1f, c->hold_d_cm = 4;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

void sb_tw_step(sb_tw_state *s, const sb_tw_cfg *c, const sb_tw_in *in, sb_tw_out *o) {
    memset(o, 0, sizeof *o);
    o->g[0] = in->g[0], o->g[1] = in->g[1];
    float dt = isfinite(in->dt) ? clampf(in->dt, 0, 0.1f) : 0;
    int both = in->valid[0] && in->valid[1] && vfinite(in->g[0].p) && vfinite(in->g[1].p) && qfinite(in->g[0].q) &&
               qfinite(in->g[1].q);
    float sq = isfinite(in->squeeze_l) ? in->squeeze_l : 0;
    int was = s->sq;
    s->sq = was ? sq > c->squeeze_off : sq >= c->squeeze_on;
    int press = s->sq && !was;
    sv_vec3 pR = in->g[1].p, pL = in->g[0].p, bl = sb_blade_dir(in->g[1].q, in->angle_rad);
    sv_vec3 v = vsub(pR, pL);
    float dist = vlen(v);
    o->dist_cm = both ? dist : 0;
    if (s->active && (!in->sword || !in->allow || !both)) {
        s->active = 0, s->lost++, o->ev |= SB_TW_E_LOST;
    } else if (s->active && !s->sq) {
        s->active = 0, s->releases++, o->ev |= SB_TW_E_RELEASE;
    } else if (s->active && dist > c->break_cm) {
        s->active = 0, s->breaks++, o->ev |= SB_TW_E_BREAK;
    } else if (!s->active && press && in->sword && in->allow && both) {
        float t = clampf(vdot(v, bl), c->hilt_lo_cm, c->hilt_hi_cm); /* the hilt point nearest the left grip */
        sv_vec3 h = vsub(pR, vmul(bl, t));
        if (vlen(vsub(pL, h)) <= c->grab_r_cm) {
            s->active = 1, s->grabs++, o->ev |= SB_TW_E_GRAB;
            s->d = bl, s->have_d = 1;
        } else {
            s->misses++, o->ev |= SB_TW_E_MISS;
        }
    }
    float step = c->blend_s > 0 ? dt / c->blend_s : 1;
    s->ramp = clampf(s->ramp + (s->active ? step : -step), 0, 1);
    if (!both) s->ramp = s->active ? s->ramp : 0;
    float w = s->ramp * s->ramp * (3 - 2 * s->ramp);
    o->w = w;
    if (w <= 0 || !both) {
        if (!s->active) s->have_d = 0;
        return;
    }
    sv_vec3 d = dist > c->hold_d_cm ? vmul(v, 1 / dist) : s->have_d ? s->d : bl;
    if (!vfinite(d)) d = bl;
    if (s->active) s->d = d, s->have_d = 1;
    else if (s->have_d) d = s->d; /* easing out: from the last two-hand direction back to the hand's own */
    sv_quat qR = sv_qnormalize(sv_qmul(qarc(bl, d), in->g[1].q));
    o->g[1].q = qslerp(in->g[1].q, qR, w);
    sv_vec3 d2 = sb_blade_dir(o->g[1].q, in->angle_rad);
    float hold = clampf(dist, c->hold_lo_cm, c->hold_hi_cm);
    o->hold_cm = hold;
    sv_vec3 pH = vsub(pR, vmul(d2, hold));
    /* the left fist's own axis (its weapon node's calibration carries no sword angle) onto the hilt line */
    sv_quat qL = sv_qnormalize(sv_qmul(qarc(sb_blade_dir(in->g[0].q, 0), d2), in->g[0].q));
    o->g[0].p = vadd(pL, vmul(vsub(pH, pL), w));
    o->g[0].q = qslerp(in->g[0].q, qL, w);
}
