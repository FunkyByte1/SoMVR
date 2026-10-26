/* somvr_practice.c - SOMVR_PRACTICE's pure part (somvr_practice.h; docs/re-xr-pipeline.md 27) */
#include "somvr_practice.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c; }
static int blank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

/* the n characters at a equal b (case-insensitive), b's length = n */
static int ci_eq(const char *a, size_t n, const char *b)
{
    if (strlen(b) != n)
        return 0;
    for (size_t i = 0; i < n; i++)
        if (lower((unsigned char)a[i]) != lower((unsigned char)b[i]))
            return 0;
    return 1;
}

/* s contains sub (case-insensitive) */
static int ci_has(const char *s, const char *sub)
{
    size_t n = strlen(sub);
    for (; s && *s; s++)
        if (strlen(s) >= n && ci_eq(s, n, sub))
            return 1;
    return 0;
}

/* the next blank-separated token of *s: its start and length (0 = none); *s moves past it */
static size_t token(const char **s, const char **tok)
{
    const char *p = *s;
    while (blank(*p))
        p++;
    *tok = p;
    while (*p && !blank(*p))
        p++;
    *s = p;
    return (size_t)(p - *tok);
}

int somvr_pr_mode_parse(const char *v, int *mode)
{
    static const struct { const char *w; int m; } t[] = { { "0", SOMVR_PR_OFF }, { "off", SOMVR_PR_OFF },
                                                          { "1", SOMVR_PR_ON },  { "on", SOMVR_PR_ON },
                                                          { "auto", SOMVR_PR_AUTO } };
    const char *tok, *s = v ? v : "";
    size_t n = token(&s, &tok);
    const char *rest;
    if (!n || token(&s, &rest))
        return 0;
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
        if (ci_eq(tok, n, t[i].w)) {
            *mode = t[i].m;
            return 1;
        }
    return 0;
}

const char *somvr_pr_mode_name(int mode)
{
    return mode == SOMVR_PR_ON ? "on" : mode == SOMVR_PR_AUTO ? "auto" : "off";
}

static const char *const g_orc_w[SOMVR_PRO_N] = { "infantry", "shield", "berserker" };
static const char *const g_orc_l[SOMVR_PRO_N] = { "Infantry", "Shield", "Berserker" };
static const char *const g_orc_t[SOMVR_PRO_N] = { "Orc_Infantry", "Orc_Defender", "Orc_Berserker" };

const char *somvr_pr_orc_word(int t) { return t >= 0 && t < SOMVR_PRO_N ? g_orc_w[t] : ""; }
const char *somvr_pr_orc_label(int t) { return t >= 0 && t < SOMVR_PRO_N ? g_orc_l[t] : ""; }
const char *somvr_pr_orc_template(int t) { return t >= 0 && t < SOMVR_PRO_N ? g_orc_t[t] : ""; }

static int orc_tok(const char *w, size_t n)
{
    static const struct { const char *w; int t; } syn[] = {
        { "infantry", SOMVR_PRO_INFANTRY }, { "inf", SOMVR_PRO_INFANTRY },   { "plain", SOMVR_PRO_INFANTRY },
        { "0", SOMVR_PRO_INFANTRY },        { "shield", SOMVR_PRO_SHIELD },  { "defender", SOMVR_PRO_SHIELD },
        { "1", SOMVR_PRO_SHIELD },          { "berserker", SOMVR_PRO_BERSERKER }, { "berserk", SOMVR_PRO_BERSERKER },
        { "2", SOMVR_PRO_BERSERKER },
    };
    for (size_t i = 0; i < sizeof syn / sizeof syn[0]; i++)
        if (ci_eq(w, n, syn[i].w))
            return syn[i].t;
    return -1;
}

int somvr_pr_orc_parse(const char *w)
{
    const char *tok, *s = w ? w : "", *rest;
    size_t n = token(&s, &tok);
    return n && !token(&s, &rest) ? orc_tok(tok, n) : -1;
}

const char *somvr_pr_action_name(int a)
{
    static const char *const n[SOMVR_PRA_N] = { "none", "go", "back", "spawn", "remove", "status" };
    return a >= 0 && a < SOMVR_PRA_N ? n[a] : "?";
}

int somvr_pr_word(const char *w, somvr_pr_req *out)
{
    static const struct { const char *w; int a; } t[] = {
        { "go", SOMVR_PRA_GO },         { "there", SOMVR_PRA_GO },       { "back", SOMVR_PRA_BACK },
        { "return", SOMVR_PRA_BACK },   { "spawn", SOMVR_PRA_SPAWN },    { "remove", SOMVR_PRA_REMOVE },
        { "despawn", SOMVR_PRA_REMOVE }, { "status", SOMVR_PRA_STATUS },
    };
    const char *tok, *s = w ? w : "", *arg, *rest;
    size_t n = token(&s, &tok);
    int a = SOMVR_PRA_NONE;
    for (size_t i = 0; i < sizeof t / sizeof t[0] && n; i++)
        if (ci_eq(tok, n, t[i].w))
            a = t[i].a;
    if (a == SOMVR_PRA_NONE)
        return 0;
    size_t na = token(&s, &arg);
    int orc = -1;
    out->have_at = 0;
    /* go X Y Z; status ground X Y Z (a test word, the collect drill: the floor under a place before a trip there) */
    int ground = na && a == SOMVR_PRA_STATUS && ci_eq(arg, na, "ground");
    if (na && (a == SOMVR_PRA_GO || ground)) {
        float v[3];
        if (ground && !(na = token(&s, &arg)))
            return 0;
        for (int i = 0; i < 3; i++) {
            char buf[32], *e;
            if (i && !(na = token(&s, &arg)))
                return 0;
            if (na >= sizeof buf)
                return 0;
            memcpy(buf, arg, na), buf[na] = 0;
            double d = strtod(buf, &e);
            if (e == buf || *e || !(d > -1e7 && d < 1e7))
                return 0;
            v[i] = (float)d;
        }
        if (token(&s, &rest))
            return 0;
        out->action = a, out->orc = ground ? 2 : -1, out->have_at = 1;
        memcpy(out->at, v, sizeof v);
        return 1;
    }
    if (na) {
        if (a == SOMVR_PRA_STATUS && ci_eq(arg, na, "nav"))
            orc = 1; /* "status nav": the game's nav markers too (a test word, the story drill's round 2) */
        else if (a == SOMVR_PRA_STATUS && ci_eq(arg, na, "look"))
            orc = 3; /* "status look": what the game's look-at triggers read of the players (the collect drill) */
        else if (a != SOMVR_PRA_SPAWN || (orc = orc_tok(arg, na)) < 0)
            return 0; /* only spawn takes a type, status "nav" */
    }
    if (token(&s, &rest))
        return 0;
    out->action = a, out->orc = orc;
    return 1;
}

/* ---- the trip ---- */
void somvr_pr_tcfg_default(somvr_pr_tcfg *c)
{
    c->fade_out_s = 0.25f, c->fade_in_s = 0.35f, c->settle_s = 0.5f, c->hold_s = 1.0f, c->hold_max_s = 5.5f;
    c->arrive_cm = 500, c->fall_cm = 300, c->timeout_s = 3.0f, c->tries = 3;
}

const char *somvr_pr_phase_name(int phase)
{
    static const char *const n[] = { "idle", "fading out", "moving", "holding", "fading in" };
    return phase >= SOMVR_PRT_IDLE && phase <= SOMVR_PRT_IN ? n[phase] : "?";
}

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static float part(double dt, float len) { return len > 0 ? clamp01((float)(dt / len)) : 1.0f; }

float somvr_pr_trip_fade(const somvr_pr_trip *tr, const somvr_pr_tcfg *c, double t)
{
    switch (tr->phase) {
    case SOMVR_PRT_OUT: return part(t - tr->t0, c->fade_out_s);
    case SOMVR_PRT_MOVE:
    case SOMVR_PRT_HOLD: return 1.0f;
    case SOMVR_PRT_IN: return tr->fade_from * (1.0f - part(t - tr->t0, c->fade_in_s));
    default: return 0.0f;
    }
}

void somvr_pr_trip_start(somvr_pr_trip *tr, const somvr_pr_tcfg *c, const float target[3], double t)
{
    float f = somvr_pr_trip_fade(tr, c, t); /* a trip fading in: on from its black */
    memset(tr, 0, sizeof *tr);
    tr->phase = SOMVR_PRT_OUT;
    tr->t0 = t - (double)f * (double)c->fade_out_s;
    memcpy(tr->target, target, sizeof tr->target);
}

int somvr_pr_at(const float pos[3], const float target[3], float arrive_cm, float fall_cm)
{
    float dx = pos[0] - target[0], dz = pos[2] - target[2];
    return sqrtf(dx * dx + dz * dz) <= arrive_cm && pos[1] >= target[1] - fall_cm;
}

#define PR_RETRY_MIN_S 0.5 /* a fall below the target asks for the teleport again at most this often */

int somvr_pr_trip_step(somvr_pr_trip *tr, const somvr_pr_tcfg *c, double t, const float pos[3], int have, int paused)
{
    return somvr_pr_trip_step_busy(tr, c, t, pos, have, paused, 0);
}

int somvr_pr_trip_step_busy(somvr_pr_trip *tr, const somvr_pr_tcfg *c, double t, const float pos[3], int have, int paused,
                            int busy)
{
    switch (tr->phase) {
    case SOMVR_PRT_OUT:
        if (t - tr->t0 < c->fade_out_s || paused)
            return SOMVR_PRX_NONE;
        tr->phase = SOMVR_PRT_MOVE, tr->t0 = tr->t_tp = t, tr->tries = 1, tr->settle_t = -1;
        return SOMVR_PRX_TELEPORT;
    case SOMVR_PRT_MOVE: {
        if (have && somvr_pr_at(pos, tr->target, c->arrive_cm, c->fall_cm)) {
            if (tr->settle_t < 0)
                tr->settle_t = t;
            if (t - tr->settle_t < c->settle_s)
                return SOMVR_PRX_NONE;
            tr->phase = SOMVR_PRT_HOLD, tr->t0 = t, tr->arrived = 1;
            return SOMVR_PRX_ARRIVED;
        }
        tr->settle_t = -1;
        float dx = have ? pos[0] - tr->target[0] : 0, dz = have ? pos[2] - tr->target[2] : 0;
        int fell = have && sqrtf(dx * dx + dz * dz) <= c->arrive_cm && pos[1] < tr->target[1] - c->fall_cm &&
                   t - tr->t_tp >= PR_RETRY_MIN_S;
        if (paused || !(fell || t - tr->t_tp >= c->timeout_s))
            return SOMVR_PRX_NONE;
        if (tr->tries < c->tries) {
            tr->tries++, tr->t_tp = t;
            return SOMVR_PRX_TELEPORT;
        }
        tr->phase = SOMVR_PRT_IN, tr->t0 = t, tr->fade_from = 1, tr->failed = 1;
        return SOMVR_PRX_FAILED;
    }
    case SOMVR_PRT_HOLD:
        if (t - tr->t0 < c->hold_s)
            return SOMVR_PRX_NONE;
        if (busy && t - tr->t0 < (c->hold_max_s > c->hold_s ? c->hold_max_s : c->hold_s)) {
            tr->held_busy = 1;
            return SOMVR_PRX_NONE;
        }
        tr->phase = SOMVR_PRT_IN, tr->t0 = t, tr->fade_from = 1;
        return SOMVR_PRX_NONE;
    case SOMVR_PRT_IN:
        if (t - tr->t0 < c->fade_in_s)
            return SOMVR_PRX_NONE;
        tr->phase = SOMVR_PRT_IDLE;
        return tr->arrived ? SOMVR_PRX_DONE : SOMVR_PRX_NONE;
    default: return SOMVR_PRX_NONE;
    }
}

/* ---- keep clear ---- */
int somvr_pr_harmless(const char *name)
{
    static const char *const k[] = { "slave", "human", "civilian", "prisoner" };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        if (ci_has(name, k[i]))
            return 1;
    return 0;
}

int somvr_pr_clear_pick(const somvr_pr_cand *c, int mode, float radius_cm)
{
    if (mode == SOMVR_PRC_OFF || !c->npc || c->ours || c->player || c->mount || c->dead)
        return 0;
    if (!(c->dist_cm <= radius_cm)) /* NaN too */
        return 0;
    return mode == SOMVR_PRC_ALL || !c->harmless;
}

void somvr_pr_gate_text(uint32_t bits, char *buf, size_t cap)
{
    static const char *const w[] = { "a presentation", "a cinematic camera", "a scripted camera", "a sync",
                                     "the game paused", "our blink", "the gate unread" };
    size_t o = 0;
    if (!cap)
        return;
    buf[0] = 0;
    for (size_t i = 0; i < sizeof w / sizeof w[0]; i++)
        if (bits >> i & 1u) {
            int k = snprintf(buf + o, cap - o, "%s%s", o ? "+" : "", w[i]);
            if (k < 0 || (size_t)k >= cap - o)
                return;
            o += (size_t)k;
        }
    if (!o)
        snprintf(buf, cap, "nothing");
}

/* ---- the spawn ---- */
void somvr_pr_ahead(const float pos[3], const float fwd[3], float dist_cm, float out[3])
{
    float fx = fwd[0], fz = fwd[2], l = sqrtf(fx * fx + fz * fz);
    if (!(l > 1e-3f))
        fx = 0, fz = 1, l = 1;
    out[0] = pos[0] + fx / l * dist_cm, out[1] = pos[1], out[2] = pos[2] + fz / l * dist_cm;
}

void somvr_pr_facing(const float at[3], const float face[3], float q[4])
{
    float dx = face[0] - at[0], dz = face[2] - at[2];
    float ang = dx * dx + dz * dz > 1e-6f ? atan2f(dx, dz) : 0.0f;
    q[0] = 0, q[1] = sinf(ang * 0.5f), q[2] = 0, q[3] = cosf(ang * 0.5f);
}

int somvr_pr_is_night(const char *tod_name) { return ci_has(tod_name, "night"); }

int somvr_pr_is_wet(const char *weather)
{
    return !ci_has(weather, "norain") && (ci_has(weather, "rain") || ci_has(weather, "storm"));
}
