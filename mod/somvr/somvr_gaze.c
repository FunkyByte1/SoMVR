/* somvr_gaze.c - the Shadow Strike by gaze, the pure part (somvr_gaze.h) */
#include "somvr_gaze.h"

#include <math.h>
#include <string.h>

void somvr_gz_cfg_default(somvr_gz_cfg *c) {
    memset(c, 0, sizeof *c);
    c->cone_deg = 6, c->keep_deg = 12, c->switch_deg = 3, c->dwell_s = 0.12f, c->lose_s = 0.25f;
    c->range_cm = 2500, c->range_keep_cm = 200, c->near_cm = 150, c->radius_cm = 35;
    c->seg_lo_cm = 70, c->seg_hi_cm = 80, c->chest_cm = 40;
    c->aim_max_s = 0.8f, c->press_s = 0.15f, c->hold_s = 0.45f, c->arrows_min = 2;
}

const char *somvr_gz_why_name(int why) {
    static const char *const n[] = {"-", "off (Y let go, the vision gone, a menu, the bow, a climb)", "looked away",
                                    "out of range", "out of sight", "gone (dead or out of the list)", "struck"};
    return why >= 0 && why < (int)(sizeof n / sizeof n[0]) ? n[why] : "?";
}

void somvr_gz_reset(somvr_gz *g) { memset(g, 0, sizeof *g); }

/* the cosine of the angle between the unit fwd and p - head; its length */
static float gz_cos(const float head[3], const float fwd[3], const float p[3], float *len) {
    float v[3] = {p[0] - head[0], p[1] - head[1], p[2] - head[2]};
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    *len = l;
    return l > 1e-3f ? (v[0] * fwd[0] + v[1] * fwd[1] + v[2] * fwd[2]) / l : -1;
}

float somvr_gz_angle(const somvr_gz_cfg *c, const float head[3], const float fwd[3], const float root[3], float *dist_cm) {
    float aim[3] = {root[0], root[1] + c->chest_cm, root[2]}, l;
    gz_cos(head, fwd, aim, &l);
    if (dist_cm) *dist_cm = l;
    if (!isfinite(l)) return 180;
    /* the body's point nearest the ray in angle: the angle along a straight segment is unimodal (golden section) */
    float lo = root[1] - c->seg_lo_cm, hi = root[1] + c->seg_hi_cm, a = lo, b = hi;
    const float r = 0.6180340f;
    for (int i = 0; i < 24; i++) {
        float y1 = b - r * (b - a), y2 = a + r * (b - a), l1, l2;
        float p1[3] = {root[0], y1, root[2]}, p2[3] = {root[0], y2, root[2]};
        if (gz_cos(head, fwd, p1, &l1) > gz_cos(head, fwd, p2, &l2)) b = y2;
        else a = y1;
    }
    float p[3] = {root[0], 0.5f * (a + b), root[2]}, lp, cs = gz_cos(head, fwd, p, &lp);
    if (!(cs > 0)) return 180; /* beside or behind the head */
    float deg = acosf(cs > 1 ? 1 : cs) * 57.29578f - atanf(c->radius_cm / (lp > 1 ? lp : 1)) * 57.29578f;
    return deg > 0 ? deg : 0;
}

static void gz_unmark(somvr_gz *g, int why, somvr_gz_out *o) {
    if (!g->mark) return;
    g->mark = 0, g->out_t = 0, g->out_why = 0, g->unmarks++;
    o->ev |= SOMVR_GZE_UNMARK, o->why = why;
}

static void gz_set(somvr_gz *g, const somvr_gz_cfg *c, const float root[3], float deg, float dist) {
    g->aim[0] = root[0], g->aim[1] = root[1] + c->chest_cm, g->aim[2] = root[2], g->deg = deg, g->dist_cm = dist;
}

void somvr_gz_pick(somvr_gz *g, const somvr_gz_cfg *c, double t, int on, const float head[3], const float fwd[3],
                   const somvr_gz_orc *orcs, int n, somvr_gz_out *o) {
    memset(o, 0, sizeof *o);
    if (g->phase != SOMVR_GZP_OFF) { /* a press runs: the mark is its target, wherever the head is */
        g->mark = g->target, g->cand = 0;
        for (int i = 0; i < n; i++)
            if (orcs[i].id == g->target) {
                float dist, deg = somvr_gz_angle(c, head, fwd, orcs[i].root, &dist);
                gz_set(g, c, orcs[i].root, deg, dist);
                memcpy(g->target_aim, g->aim, sizeof g->aim);
            }
        goto out;
    }
    if (!on) {
        gz_unmark(g, SOMVR_GZW_OFF, o);
        g->cand = 0;
        goto out;
    }
    int best = -1, mi = -1;
    float bdeg = 1e9f, mdeg = 180, mdist = 0;
    for (int i = 0; i < n; i++) {
        if (!orcs[i].id) continue;
        float dist, deg = somvr_gz_angle(c, head, fwd, orcs[i].root, &dist);
        int seen = orcs[i].los != 0;
        if (orcs[i].id == g->mark) mi = i, mdeg = deg, mdist = dist;
        if (seen && dist >= c->near_cm && dist <= c->range_cm && deg <= c->cone_deg && deg < bdeg) best = i, bdeg = deg;
    }
    if (g->mark && mi < 0) gz_unmark(g, SOMVR_GZW_GONE, o);
    if (g->mark) {
        const somvr_gz_orc *m = &orcs[mi];
        int why = mdeg > c->keep_deg ? SOMVR_GZW_AWAY
                  : mdist > c->range_cm + c->range_keep_cm || mdist < 0.5f * c->near_cm ? SOMVR_GZW_RANGE
                  : m->los == 0 ? SOMVR_GZW_SIGHT
                                : SOMVR_GZW_NONE;
        if (why) {
            if (!g->out_t) g->out_t = t > 0 ? t : 1e-9, g->out_why = why;
            else g->out_why = why;
            if (t - g->out_t >= c->lose_s) {
                gz_unmark(g, why, o);
                mi = -1;
            }
        } else {
            g->out_t = 0, g->out_why = 0;
        }
        if (g->mark) gz_set(g, c, m->root, mdeg, mdist);
    }
    if (g->mark) { /* another takes it only clearly better, while the marked one is out of the cone, for dwell_s */
        int better = best >= 0 && orcs[best].id != g->mark && (mdeg > c->cone_deg || g->out_t) &&
                     bdeg + c->switch_deg <= mdeg;
        if (!better) {
            g->cand = 0;
        } else if (g->cand != orcs[best].id) {
            g->cand = orcs[best].id, g->cand_t = t;
        } else if (t - g->cand_t >= c->dwell_s) {
            float dist;
            float deg = somvr_gz_angle(c, head, fwd, orcs[best].root, &dist);
            g->mark = orcs[best].id, g->cand = 0, g->out_t = 0, g->out_why = 0, g->mark_t = t, g->switches++;
            gz_set(g, c, orcs[best].root, deg, dist);
            o->ev |= SOMVR_GZE_SWITCH;
        }
    } else if (best < 0) {
        g->cand = 0;
    } else if (g->cand != orcs[best].id) {
        g->cand = orcs[best].id, g->cand_t = t;
    } else if (t - g->cand_t >= c->dwell_s) {
        float dist;
        float deg = somvr_gz_angle(c, head, fwd, orcs[best].root, &dist);
        g->mark = orcs[best].id, g->cand = 0, g->out_t = 0, g->out_why = 0, g->mark_t = t, g->marks++;
        gz_set(g, c, orcs[best].root, deg, dist);
        o->ev |= SOMVR_GZE_MARK;
    }
out:
    g->frames_marked += g->mark != 0;
    o->mark = g->mark;
    memcpy(o->aim, g->aim, sizeof o->aim);
    o->deg = g->deg, o->dist_cm = g->dist_cm;
}

uint32_t somvr_gz_press(somvr_gz *g, const somvr_gz_cfg *c, double t, int arrows) {
    if (g->phase != SOMVR_GZP_OFF) return 0;
    if (!g->mark) {
        g->nomarks++;
        return SOMVR_GZE_NOMARK;
    }
    if (arrows >= 0 && arrows < c->arrows_min) {
        g->noarrows++;
        return SOMVR_GZE_NOARROWS;
    }
    g->phase = SOMVR_GZP_AIM, g->press_t = t, g->send_t = 0, g->target = g->mark, g->presses++;
    memcpy(g->target_aim, g->aim, sizeof g->aim);
    return SOMVR_GZE_PRESS;
}

void somvr_gz_tick(somvr_gz *g, const somvr_gz_cfg *c, double t, int prompt, int struck, somvr_gz_out *o) {
    o->ev = 0, o->why = 0, o->aim_cmd = o->strike_cmd = 0;
    if (g->phase == SOMVR_GZP_AIM) {
        if (prompt) {
            g->phase = SOMVR_GZP_SENT, g->send_t = t, g->sends++;
            o->ev |= SOMVR_GZE_SEND;
        } else if (t - g->press_t >= c->aim_max_s) {
            g->phase = SOMVR_GZP_OFF, g->refused++;
            o->ev |= SOMVR_GZE_REFUSED | SOMVR_GZE_DONE;
            g->target = 0;
            return;
        } else {
            o->aim_cmd = 1;
            return;
        }
    }
    if (g->phase == SOMVR_GZP_SENT) {
        double k = t - g->send_t;
        o->strike_cmd = k < c->press_s;
        o->aim_cmd = k < c->hold_s && !(struck && !o->strike_cmd);
        if (!o->aim_cmd && !o->strike_cmd) {
            g->phase = SOMVR_GZP_OFF, g->target = 0;
            g->mark = 0, g->out_t = 0, g->cand = 0; /* struck: no mark until the head rests on an orc again */
            o->ev |= SOMVR_GZE_DONE | SOMVR_GZE_UNMARK, o->why = SOMVR_GZW_STRUCK;
        }
    }
}
