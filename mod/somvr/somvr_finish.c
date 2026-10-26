/* somvr_finish.c - VR-native finishers, the pure part (somvr_finish.h; docs/re-xr-pipeline.md 24) */
#include "somvr_finish.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- classes and names ------------------------------------------------------------------------------------------- */
static const char *const g_cls[SF_CLS_N] = { "none", "brutal", "stealth", "ground", "exec", "brand", "drain",
                                             "interrogate" };

const char *sf_class_name(int cls) { return cls >= 0 && cls < SF_CLS_N ? g_cls[cls] : "?"; }

int sf_class_find(const char *name) {
    for (int k = 1; name && k < SF_CLS_N; k++)
        if (!strcmp(name, g_cls[k])) return k;
    return -1;
}

static int lower(int ch) { return ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch; }

int sf_contains(const char *s, const char *pat) {
    if (!s || !pat || !pat[0]) return 0;
    for (; *s; s++) {
        size_t i = 0;
        while (pat[i] && s[i] && lower((unsigned char)s[i]) == lower((unsigned char)pat[i])) i++;
        if (!pat[i]) return 1;
    }
    return 0;
}

int sf_names_set(sf_names *n, int cls, const char *csv) {
    if (cls <= SF_CLS_NONE || cls >= SF_CLS_N) return 0;
    n->n[cls] = 0;
    memset(n->pat[cls], 0, sizeof n->pat[cls]);
    if (!csv) return 0;
    const char *p = csv;
    while (*p && n->n[cls] < SF_PAT_MAX) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        const char *e = p;
        while (*e && *e != ',') e++;
        const char *q = e;
        while (q > p && (q[-1] == ' ' || q[-1] == '\t')) q--;
        size_t l = (size_t)(q - p);
        if (l && !(l == 1 && *p == '-')) {
            if (l > SF_PAT_LEN - 1) l = SF_PAT_LEN - 1;
            memcpy(n->pat[cls][n->n[cls]], p, l);
            n->pat[cls][n->n[cls]][l] = 0;
            n->n[cls]++;
        }
        p = e;
    }
    return n->n[cls];
}

void sf_names_default(sf_names *n) {
    memset(n, 0, sizeof *n);
    sf_names_set(n, SF_CLS_BRUTAL, "StealthTorment,Torment,Brutal");
    sf_names_set(n, SF_CLS_STEALTH, "SneakInstaKill,Sneak_InstantKill,Sneak_IK,StealthKill,InstaKill");
    sf_names_set(n, SF_CLS_GROUND, "GroundExec,Ground_Exec,GroundKill,Ground_Kill,Exec_Ground,Ground");
    sf_names_set(n, SF_CLS_EXEC, "Execution,Execute,Exec");
    sf_names_set(n, SF_CLS_BRAND, "Brand");
    sf_names_set(n, SF_CLS_DRAIN, "Drain");
    sf_names_set(n, SF_CLS_INTERROGATE, "Interrog");
}

static int class_in(const sf_names *n, const char *s) {
    for (int k = 1; s && s[0] && k < SF_CLS_N; k++)
        for (int i = 0; i < n->n[k]; i++)
            if (sf_contains(s, n->pat[k][i])) return k;
    return SF_CLS_NONE;
}

int sf_class_of(const sf_names *n, const char *node, const char *attack) {
    int k = class_in(n, attack);
    return k != SF_CLS_NONE ? k : class_in(n, node);
}

int sf_pick(const int *cls, int n, int want, int alt) {
    for (int i = 0; want != SF_CLS_NONE && i < n; i++)
        if (cls[i] == want) return i;
    for (int i = 0; alt != SF_CLS_NONE && i < n; i++)
        if (cls[i] == alt) return i;
    return -1;
}

/* ---- states ------------------------------------------------------------------------------------------------------ */
int sf_state_kind(const char *name) {
    if (!name || !name[0]) return SF_ST_NONE;
    if (sf_contains(name, "stun")) return SF_ST_STUN;
    if (sf_contains(name, "knockdown") || sf_contains(name, "knockeddown") || sf_contains(name, "kd_") ||
        sf_contains(name, "_kd") || sf_contains(name, "down"))
        return SF_ST_DOWN;
    return SF_ST_OTHER;
}

const char *sf_state_name(int kind) {
    static const char *const s[] = { "none", "stunned", "down", "other" };
    return kind >= 0 && kind <= SF_ST_OTHER ? s[kind] : "?";
}

/* ---- configuration ----------------------------------------------------------------------------------------------- */
void sf_cfg_default(sf_cfg *c) {
    memset(c, 0, sizeof *c);
    c->neck_up_cm = 62, c->head_up_cm = 24, c->back_cm = 14, c->back_down_cm = 16, c->torso_up_cm = 18;
    c->exec_streak = 8, c->exec_stunned = 1;
    c->exec_mps = 4.5f, c->exec_cm = 35, c->exec_below_cm = 15, c->exec_above_cm = 12, c->exec_cut_cos = 0.8f;
    c->exec_lead_s = 0.15f;
    c->ground_mps = 1.8f, c->ground_axis_cos = 0.8f, c->ground_down_cos = 0.819f, c->ground_zone_cm = 30;
    c->ground_hold_mps = 1.0f;
    c->stab_mps = 1.5f, c->stab_cos = 0.7f, c->slash_mps = 2.5f, c->stab_zone_cm = 14, c->stab_along = 0.45f;
    c->back_below_cm = 30;
    c->behind_deg = 110;
    c->latch_cm = 25, c->unlatch_cm = 45, c->squeeze_on = 0.7f, c->squeeze_off = 0.4f, c->trigger_on = 0.6f;
    c->hold_s = 0.4f;
    c->cooldown_s = 1.0f, c->claim_s = 4.0f, c->glow_off_s = 0.25f, c->steps = 4;
}

/* ---- geometry ---------------------------------------------------------------------------------------------------- */
#define SF_BLADE_R 3.0f /* the sword's radius (sc_cfg's blade_r) */

void sf_zones_of(const sf_cfg *c, const float root[3], float yaw, const float *head_node, int down, sf_zones *z) {
    float f[3] = { sinf(yaw), 0, cosf(yaw) };
    z->torso[0] = root[0], z->torso[1] = root[1] + c->torso_up_cm, z->torso[2] = root[2];
    if (down) {
        memcpy(z->neck, z->torso, sizeof z->neck), memcpy(z->head, z->torso, sizeof z->head);
        memcpy(z->back, z->torso, sizeof z->back);
        return;
    }
    if (head_node) { /* the Head node is the head's base: the neck a little below it */
        z->neck[0] = head_node[0], z->neck[1] = head_node[1] - 6, z->neck[2] = head_node[2];
    } else {
        z->neck[0] = root[0], z->neck[1] = root[1] + c->neck_up_cm, z->neck[2] = root[2];
    }
    z->head[0] = z->neck[0], z->head[1] = z->neck[1] + c->head_up_cm, z->head[2] = z->neck[2];
    z->back[0] = z->neck[0] - f[0] * c->back_cm, z->back[1] = z->neck[1] - c->back_down_cm;
    z->back[2] = z->neck[2] - f[2] * c->back_cm;
}

float sf_behind_deg(const float orc[3], float yaw, const float player[3]) {
    float dx = player[0] - orc[0], dz = player[2] - orc[2], d = sqrtf(dx * dx + dz * dz);
    if (!(d >= 1.0f)) return -1;
    float cs = (dx * sinf(yaw) + dz * cosf(yaw)) / d;
    if (cs > 1) cs = 1;
    if (cs < -1) cs = -1;
    return acosf(cs) * 57.2957795f;
}

float sf_yaw_of_quat(const float q[4]) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float fx = 2 * (x * z + w * y), fz = 1 - 2 * (x * x + y * y);
    if (fx * fx + fz * fz > 1e-8f) return atan2f(fx, fz);
    float a = 2.0f * atan2f(y, w);
    while (a > 3.14159265f) a -= 6.28318531f;
    while (a < -3.14159265f) a += 6.28318531f;
    return a;
}

/* ---- the judges -------------------------------------------------------------------------------------------------- */
const char *sf_why_name(int why) {
    static const char *const s[SF_WHY_N] = { "ok", "no blade", "not ready", "no swing", "too slow", "too short",
                                             "began before ready", "a poke, not a cut", "missed the zone",
                                             "not pointing down", "not a stab", "not from behind",
                                             "not with the tip", "already inside the zone",
                                             "below the zone", "above the zone" };
    return why >= 0 && why < SF_WHY_N ? s[why] : "?";
}

static int sweep(const sf_cfg *c, const somvr_blade *prev, const somvr_blade *cur, const float c0[3],
                 const float c1[3], float r, int zone, sf_hit *out) {
    sc_contact ct;
    if (!sc_sweep_capsule(prev, cur, SF_BLADE_R, c0, c1, r, c->steps > 0 ? c->steps : 4, &ct)) return 0;
    float s, t;
    if (prev && prev->valid && sc_seg_dist(prev->base, prev->tip, c0, c1, &s, &t) <= SF_BLADE_R + r)
        return -1; /* the previous pose touched already: inside the zone, not an entry */
    out->u = ct.u, out->along = ct.along, out->dist = ct.dist, out->zone = zone;
    memcpy(out->point, ct.point, sizeof out->point);
    return 1;
}

int sf_judge_exec_at(const sf_cfg *c, const sc_speed *sp, double swing_t0, const float point[3], const sf_zones *z,
                     int ready, double ready_since) {
    if (!ready) return SF_WHY_NOTREADY;
    if (!sp->above || swing_t0 < 0) return SF_WHY_NOSWING;
    if (swing_t0 < ready_since - c->exec_lead_s) return SF_WHY_EARLY;
    if (!(point[1] >= z->neck[1] - c->exec_below_cm)) return SF_WHY_LOW;
    if (!(point[1] <= z->head[1] + c->exec_above_cm)) return SF_WHY_HIGH;
    if (!(sp->mean_mps >= c->exec_mps)) return SF_WHY_SLOW;
    if (!(sp->travel >= c->exec_cm)) return SF_WHY_SHORT;
    if (!(fabsf(sp->cos_axis) <= c->exec_cut_cos)) return SF_WHY_POKE;
    return SF_WHY_OK;
}

static float blade_down_cos(const somvr_blade *b) {
    float a[3] = { b->tip[0] - b->base[0], b->tip[1] - b->base[1], b->tip[2] - b->base[2] };
    float l = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    return l > 1e-3f ? -a[1] / l : 0;
}

int sf_judge_ground(const sf_cfg *c, const sc_speed *sp, const somvr_blade *prev, const somvr_blade *cur,
                    const sf_zones *z, int down, sf_hit *out) {
    if (!cur || !cur->valid) return SF_WHY_NOBLADE;
    if (!down) return SF_WHY_NOTREADY;
    sf_hit h;
    int e = sweep(c, prev, cur, z->torso, z->torso, c->ground_zone_cm, 2, &h);
    if (e <= 0) return e < 0 ? SF_WHY_INSIDE : SF_WHY_MISS;
    if (!(h.along >= 0.5f)) return SF_WHY_TIP;
    if (!(blade_down_cos(cur) >= c->ground_down_cos)) return SF_WHY_UP;
    if (!(sp->axis_mps >= c->ground_mps) || !(sp->cos_axis >= c->ground_axis_cos)) return SF_WHY_NOTSTAB;
    if (out) *out = h;
    return SF_WHY_OK;
}

int sf_ground_hold(const sf_cfg *c, const sc_speed *sp, const somvr_blade *b) {
    return b && b->valid && blade_down_cos(b) >= c->ground_down_cos && sp->axis_mps >= c->ground_hold_mps;
}

int sf_judge_back_at(const sf_cfg *c, const sc_speed *sp, const float point[3], float along, const sf_zones *z,
                     float behind) {
    if (!(behind >= c->behind_deg)) return SF_WHY_FRONT;
    if (!(along >= c->stab_along)) return SF_WHY_TIP;
    if (!(point[1] >= z->back[1] - c->back_below_cm)) return SF_WHY_LOW;
    int stab = sp->axis_mps >= c->stab_mps && sp->cos_axis >= c->stab_cos;
    if (!stab && !(sp->mean_mps >= c->slash_mps)) return SF_WHY_NOTSTAB;
    return SF_WHY_OK;
}

int sf_judge_stab(const sf_cfg *c, const sc_speed *sp, const somvr_blade *prev, const somvr_blade *cur,
                  const sf_zones *z, float behind, sf_hit *out) {
    if (!cur || !cur->valid) return SF_WHY_NOBLADE;
    if (!(behind >= c->behind_deg)) return SF_WHY_FRONT;
    sf_hit h;
    int e = sweep(c, prev, cur, z->neck, z->head, c->stab_zone_cm, 0, &h);
    if (e == 0) e = sweep(c, prev, cur, z->back, z->back, c->stab_zone_cm, 1, &h);
    if (e <= 0) return e < 0 ? SF_WHY_INSIDE : SF_WHY_MISS;
    if (!(h.along >= c->stab_along)) return SF_WHY_TIP;
    int stab = sp->axis_mps >= c->stab_mps && sp->cos_axis >= c->stab_cos;
    if (!stab && !(sp->mean_mps >= c->slash_mps)) return SF_WHY_NOTSTAB;
    if (out) *out = h;
    return SF_WHY_OK;
}

/* ---- the glow ---------------------------------------------------------------------------------------------------- */
void sf_glow_reset(sf_glow *g) { memset(g, 0, sizeof *g); }

int sf_glow_step(sf_glow *g, const sf_cfg *c, int ready, int force_off, double t) {
    if (ready && !force_off) g->last = t;
    if (!g->on) {
        if (!ready || force_off) return 0;
        g->on = 1, g->since = t, g->ons++;
        return 1;
    }
    if (force_off || (!ready && t - g->last >= c->glow_off_s)) {
        g->on = 0;
        return -1;
    }
    return 0;
}

/* ---- the latch --------------------------------------------------------------------------------------------------- */
const char *sf_lev_name(int ev) {
    static const char *const s[] = { "none", "latch", "drain", "interrogate", "release", "lost" };
    return ev >= 0 && ev <= SF_LEV_LOST ? s[ev] : "?";
}

void sf_latch_reset(sf_latch *l) {
    memset(l, 0, sizeof *l);
    l->trig_t0 = l->btn_t0 = -1; /* armed 0: a grip held since before the first frame must open once first */
}

static float dist3f(const float a[3], const float b[3]) {
    float d[3] = { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
    return sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

int sf_latch_step(sf_latch *l, const sf_cfg *c, const sf_latch_in *in) {
    int grip = in->valid && in->squeeze >= c->squeeze_on, open = !in->valid || in->squeeze < c->squeeze_off;
    if (!l->held) {
        if (open) l->armed = 1;
        if (!grip || !l->armed) return SF_LEV_NONE;
        l->armed = 0; /* this squeeze is spent: it latches here or never (HL:A: a closed hand moved onto a thing holds
                       * nothing) */
        if (!in->key || !(dist3f(in->hand, in->neck) <= c->latch_cm)) return SF_LEV_NONE;
        l->held = 1, l->key = in->key, l->t0 = in->t, l->trig_t0 = l->btn_t0 = -1, l->fired = 0;
        l->latches++;
        return SF_LEV_LATCH;
    }
    if (open) {
        l->held = 0, l->key = 0, l->releases++;
        return SF_LEV_RELEASE;
    }
    if (in->held_key_alive != l->key || !(dist3f(in->hand, in->held_neck) <= c->unlatch_cm)) {
        l->held = 0, l->key = 0, l->losses++;
        return SF_LEV_LOST;
    }
    if (in->trigger >= c->trigger_on) {
        if (l->trig_t0 < 0) l->trig_t0 = in->t;
    } else {
        l->trig_t0 = -1;
    }
    if (in->upper) {
        if (l->btn_t0 < 0) l->btn_t0 = in->t;
    } else {
        l->btn_t0 = -1;
    }
    if (l->fired) return SF_LEV_NONE;
    if (l->trig_t0 >= 0 && in->t - l->trig_t0 >= c->hold_s) {
        l->fired = 1, l->drains++;
        return SF_LEV_DRAIN;
    }
    if (l->btn_t0 >= 0 && in->t - l->btn_t0 >= c->hold_s) {
        l->fired = 1, l->interrogations++;
        return SF_LEV_INTERROGATE;
    }
    return SF_LEV_NONE;
}

/* ---- the moment -------------------------------------------------------------------------------------------------- */
void sf_moment_reset(sf_moment *m) {
    memset(m, 0, sizeof *m);
    m->last_t = -1e9;
}

int sf_moment_cool(const sf_moment *m, const sf_cfg *c, double t) { return t - m->last_t >= c->cooldown_s; }

void sf_moment_start(sf_moment *m, const sf_cfg *c, uintptr_t key, int cls, double t) {
    m->key = key, m->cls = cls, m->t0 = t, m->until = t + c->claim_s, m->last_t = t;
}

uintptr_t sf_moment_claim(sf_moment *m, double t, int end) {
    if (m->key && (end || t >= m->until)) m->key = 0;
    return m->key;
}
