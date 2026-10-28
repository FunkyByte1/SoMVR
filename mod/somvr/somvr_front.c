/* somvr_front.c - front-first enemies: the fight circle's VR veto, the damage scale, the AI's camera copy from the head
 * (somvr_front.h; docs/re-xr-pipeline.md 42). Pure. */
#include "somvr_front.h"

#include <math.h>
#include <string.h>

#define FR_FORGET_MS 10000 /* a record not seen as a candidate for this long is freed (no warning running) */

void somvr_front_cfg_default(somvr_front_cfg *c) {
    c->view_deg = 110.f;
    c->warn_s = 1.0f;
    c->ready_s = 2.0f;
    c->quiet_s = 2.0f;
    c->far_cm = 0.f;
    c->veto_score = -50.f;
    c->max_warn = 1;
}

void somvr_front_init(somvr_front *f) { memset(f, 0, sizeof *f); }

const char *somvr_front_verdict_name(int v) {
    static const char *const n[SOMVR_FR_N] = {"game", "in-view", "far", "ready", "wait", "veto", "full", "pace", "pass"};
    return v >= 0 && v < SOMVR_FR_N ? n[v] : "?";
}

static int32_t age_ms(uint32_t now, uint32_t t) { return (int32_t)(now - t); } /* wrap-safe */
static int32_t ms_of(float s) { return s > 0.f ? (int32_t)(s * 1000.f + 0.5f) : 0; }

/* a warning that started is running (or ready) until warn_s + ready_s */
static int live_warning(const somvr_front_orc *r, const somvr_front_cfg *c, uint32_t now) {
    return r->id && r->warning && age_ms(now, r->warn_ms) <= ms_of(c->warn_s) + ms_of(c->ready_s);
}

static somvr_front_orc *record(somvr_front *f, const somvr_front_cfg *c, uint64_t id, uint32_t now) {
    somvr_front_orc *free_r = 0, *old = 0;
    for (int i = 0; i < SOMVR_FRONT_ORCS; i++) {
        somvr_front_orc *r = &f->o[i];
        if (r->id == id) return r;
        if (!r->id) {
            if (!free_r) free_r = r;
        } else if (!live_warning(r, c, now) && age_ms(now, r->seen_ms) > FR_FORGET_MS &&
                   (!old || age_ms(r->seen_ms, old->seen_ms) < 0))
            old = r;
    }
    somvr_front_orc *r = free_r ? free_r : old;
    if (!r) return 0;
    memset(r, 0, sizeof *r);
    r->id = id;
    return r;
}

static somvr_front_orc *find_rw(somvr_front *f, uint64_t id) {
    if (!id) return 0;
    for (int i = 0; i < SOMVR_FRONT_ORCS; i++)
        if (f->o[i].id == id) return &f->o[i];
    return 0;
}

const somvr_front_orc *somvr_front_find(const somvr_front *f, uint64_t id) { return find_rw((somvr_front *)f, id); }

void somvr_front_granted(somvr_front *f, uint64_t id) {
    somvr_front_orc *r = find_rw(f, id);
    if (r && r->warning) {
        r->warning = 0;
        f->used++;
    }
}

int somvr_front_warnings(const somvr_front *f, const somvr_front_cfg *c, uint32_t now_ms) {
    int n = 0;
    for (int i = 0; i < SOMVR_FRONT_ORCS; i++) n += live_warning(&f->o[i], c, now_ms);
    return n;
}

float somvr_front_warn_level(const somvr_front *f, const somvr_front_cfg *c, uint64_t id, uint32_t now_ms) {
    const somvr_front_orc *r = somvr_front_find(f, id);
    if (!r || !live_warning(r, c, now_ms)) return -1.f;
    int32_t w = ms_of(c->warn_s), a = age_ms(now_ms, r->warn_ms);
    if (w <= 0 || a >= w) return 1.f;
    return a <= 0 ? 0.f : (float)a / (float)w;
}

uint64_t somvr_front_end_pass(somvr_front *f, const somvr_front_cfg *c, uint32_t now_ms) {
    for (int i = 0; i < SOMVR_FRONT_ORCS; i++) { /* expiry: unused warnings, quiet times, forgotten orcs */
        somvr_front_orc *r = &f->o[i];
        if (!r->id) continue;
        if (r->warning && !live_warning(r, c, now_ms)) {
            r->warning = 0;
            r->quiet = 1;
            r->quiet_until = now_ms + (uint32_t)ms_of(c->quiet_s);
            f->expired++;
        }
        if (r->quiet && age_ms(now_ms, r->quiet_until) >= 0) r->quiet = 0;
        if (!r->warning && age_ms(now_ms, r->seen_ms) > FR_FORGET_MS) memset(r, 0, sizeof *r);
    }
    if (!f->open) return 0;
    somvr_front_orc *best = 0, *best_prio = 0;
    for (int i = 0; i < SOMVR_FRONT_ORCS; i++) {
        somvr_front_orc *r = &f->o[i];
        if (!r->id || !r->want || r->warning) continue;
        if (r->prio && (!best_prio || r->score > best_prio->score)) best_prio = r;
        if (!r->quiet && (!best || r->score > best->score)) best = r;
    }
    if ((best_prio || best) && f->hold_warn) /* build 19: another orc's turn holds longer than a warning */
        best = 0, f->held++;
    else if (best_prio) /* it ends every pass before anyone is granted: warn it now */
        best = best_prio, f->prio_warnings++;
    else if (f->any_in_view || somvr_front_warnings(f, c, now_ms) >= c->max_warn)
        best = 0;
    uint64_t started = 0;
    if (best) {
        best->warning = 1;
        best->quiet = 0;
        best->warn_ms = now_ms;
        f->warnings++;
        started = best->id;
    }
    for (int i = 0; i < SOMVR_FRONT_ORCS; i++) f->o[i].want = f->o[i].prio = 0;
    f->open = 0;
    return started;
}

int somvr_front_judge(somvr_front *f, const somvr_front_cfg *c, uintptr_t pass, uint32_t now_ms, uint64_t id,
                      float off_deg, float dist_cm, int prio, float *score, uint64_t *started) {
    uint64_t s = 0;
    if (f->open && (f->key != pass || f->pass_ms != now_ms)) s = somvr_front_end_pass(f, c, now_ms);
    if (started) *started = s;
    if (!f->open) {
        f->open = 1;
        f->key = pass;
        f->pass_ms = now_ms;
        f->any_in_view = 0;
        f->passes++;
    }
    f->judged++;
    float sc = *score;
    somvr_front_orc *r = find_rw(f, id);
    if (!(sc >= 0.f)) { /* the game's own rejection (or a NaN): unchanged */
        f->game_rejects++;
        if (r) r->seen_ms = now_ms, r->score = sc;
        return SOMVR_FR_GAME;
    }
    if (!(off_deg > c->view_deg * 0.5f)) { /* in view (NaN: unknown = in view) */
        f->in_view++;
        f->any_in_view = 1;
        if (r) r->seen_ms = now_ms, r->score = sc, r->off_deg = off_deg, r->dist_cm = dist_cm;
        return SOMVR_FR_IN_VIEW;
    }
    if (c->far_cm > 0.f && dist_cm > c->far_cm) {
        f->far++;
        return SOMVR_FR_FAR;
    }
    if (!r && !(r = id ? record(f, c, id, now_ms) : 0)) { /* no id or no record free */
        f->full++;
        return SOMVR_FR_FULL;
    }
    r->seen_ms = now_ms;
    r->score = sc;
    r->off_deg = off_deg;
    r->dist_cm = dist_cm;
    if (r->warning) {
        int32_t a = age_ms(now_ms, r->warn_ms);
        if (a >= ms_of(c->warn_s) && a <= ms_of(c->warn_s) + ms_of(c->ready_s)) {
            f->ready++;
            return SOMVR_FR_READY;
        }
        if (a >= 0 && a < ms_of(c->warn_s)) {
            f->waits++;
            *score = c->veto_score;
            return SOMVR_FR_WAIT;
        }
        r->warning = 0; /* ran out unused (the pass end would do it too): quiet */
        r->quiet = 1;
        r->quiet_until = now_ms + (uint32_t)ms_of(c->quiet_s);
        f->expired++;
    }
    if (r->quiet && age_ms(now_ms, r->quiet_until) >= 0) r->quiet = 0;
    r->prio = prio != 0;
    r->want = !r->quiet || r->prio; /* a prio orc is warned even while quiet */
    f->vetoes++;
    *score = c->veto_score;
    return SOMVR_FR_VETO;
}

float somvr_front_damage(float amount, float scale, float keep_above) {
    if (!(scale >= 0.f && scale <= 1.f)) scale = 1.f;
    if (!(amount > 0.f)) return amount;
    if (keep_above > 0.f && amount >= keep_above) return amount;
    return amount * scale;
}

void somvr_front_camcopy_fill(somvr_front_camcopy *out, const float head[3], const float q[4], float fov_h_deg,
                              float fov_v_deg) {
    float n = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    float u[4] = {0.f, 0.f, 0.f, 1.f};
    if (n > 1e-12f && isfinite(n)) {
        float k = 1.f / sqrtf(n);
        for (int i = 0; i < 4; i++) u[i] = q[i] * k;
    }
    for (int i = 0; i < 3; i++) out->pos[i] = out->pos2[i] = head[i];
    for (int i = 0; i < 4; i++) out->quat[i] = out->quat2[i] = u[i];
    float fv[2] = {fov_h_deg, fov_v_deg};
    for (int i = 0; i < 2; i++) {
        float d = isfinite(fv[i]) ? fv[i] : 110.f;
        d = d < 20.f ? 20.f : d > 170.f ? 170.f : d;
        out->fov[i] = d * (float)(M_PI / 180.0);
    }
}

float somvr_front_cam_dot(const somvr_front_camcopy *cc, const float p[3]) {
    float d[3] = {p[0] - cc->pos[0], p[1] - cc->pos[1], p[2] - cc->pos[2]};
    float l2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    if (!(l2 > 0.1f)) return 0.f; /* the game's own guard (DAT_03360530 = 0.1) */
    const float *q = cc->quat; /* R(q)(0,0,1), as at 0x28cea93.. */
    float fx = 2.f * (q[0] * q[2] + q[1] * q[3]);
    float fy = 2.f * (q[1] * q[2] - q[0] * q[3]);
    float fz = 1.f - 2.f * (q[0] * q[0] + q[1] * q[1]);
    return (fx * d[0] + fy * d[1] + fz * d[2]) / sqrtf(l2);
}

/* ---- build 19: the crowd's pace ---- */
void somvr_pace_cfg_default(somvr_pace_cfg *c) {
    c->gap_s = 0.7f;
    c->lead_s = 0.85f;
    c->start_s = 2.5f;
    c->swing_s = 0.8f;
    c->answer_s = 0.f;
    c->verdict_s = 0.6f;
}

void somvr_pace_init(somvr_pace *p) { memset(p, 0, sizeof *p); }

float somvr_pace_lead(const somvr_pace *p, const somvr_pace_cfg *c) {
    float l = c->lead_s > 0.f ? c->lead_s : 0.f;
    return p->lead_seen > 0.f && p->lead_seen < l ? p->lead_seen : l;
}

static int pace_live(const somvr_pace_turn *u) { return u->id != 0; }

/* the latest a held turn's blow lands (a pending grant's: its blow cannot land before t + swing_s) */
static double pace_turn_end(const somvr_pace_turn *u, const somvr_pace_cfg *c, double t) {
    if (u->impact_t > 0) return u->impact_t;
    if (u->hold_t > 0) return u->hold_t; /* an answer hold: its end (its answer, if it comes, holds on to its impact) */
    double e = t + (c->swing_s > 0.f ? c->swing_s : 0.f);
    double g = u->grant_t + (c->swing_s > 0.f ? c->swing_s : 0.f);
    return e > g ? e : g;
}

double somvr_pace_free_t(const somvr_pace *p, const somvr_pace_cfg *c, double t) {
    double after = c->gap_s - somvr_pace_lead(p, c), f = t;
    if (after < 0) after = 0;
    int held = 0;
    for (int i = 0; i < SOMVR_PACE_TURNS; i++) {
        const somvr_pace_turn *u = &p->turn[i];
        if (!pace_live(u)) continue;
        double e = pace_turn_end(u, c, t) + after;
        if (!held || e > f) f = e;
        held = 1;
    }
    if (!held && p->last_impact_t > 0 && p->last_impact_t + after > f) f = p->last_impact_t + after;
    return f;
}

static int pace_others(const somvr_pace *p, uint64_t id) {
    for (int i = 0; i < SOMVR_PACE_TURNS; i++)
        if (pace_live(&p->turn[i]) && p->turn[i].id != id) return 1;
    return 0;
}

int somvr_pace_may(const somvr_pace *p, const somvr_pace_cfg *c, uint64_t id, double t, double *free_t) {
    double f = somvr_pace_free_t(p, c, t);
    int held = 0, own = 0;
    for (int i = 0; i < SOMVR_PACE_TURNS; i++)
        if (pace_live(&p->turn[i])) held = 1, own |= p->turn[i].id == id;
    if (free_t) *free_t = f;
    if (held) /* the orc whose turn it is keeps its score (a veto must not take back its grant); everyone else waits */
        return own && !pace_others(p, id);
    return !(f > t);
}

static somvr_pace_turn *pace_slot(somvr_pace *p, uint64_t id, int pending_only) {
    for (int i = 0; i < SOMVR_PACE_TURNS; i++) {
        somvr_pace_turn *u = &p->turn[i];
        if (u->id == id && id && (!pending_only || !(u->start_t > 0))) return u;
    }
    return 0;
}

static somvr_pace_turn *pace_free_slot(somvr_pace *p) {
    for (int i = 0; i < SOMVR_PACE_TURNS; i++)
        if (!p->turn[i].id) return &p->turn[i];
    return 0;
}


/* a turn's blow is over (landed, met or cancelled at t): a marked orc's turn becomes its answer hold, else it ends */
static void pace_finish(somvr_pace *p, const somvr_pace_cfg *c, somvr_pace_turn *u, double at) {
    if (u->answer && c->answer_s > 0.f && u->hit >= 0) {
        int hit = u->hit;
        uint64_t id = u->id;
        memset(u, 0, sizeof *u);
        u->id = id, u->answer = 1, u->hit = hit, u->hold0_t = at;
        u->hold_t = at + (double)(hit > 0 ? c->answer_s : c->verdict_s);
        if (hit > 0) p->holds++;
        else p->holds_wait++;
        return;
    }
    p->no_hit += u->answer && c->answer_s > 0.f && u->hit < 0;
    memset(u, 0, sizeof *u);
}

static void pace_land(somvr_pace *p, const somvr_pace_cfg *c, somvr_pace_turn *u, double at, double *gap) {
    double g = -1;
    if (p->last_impact_t > 0) {
        g = at - p->last_impact_t;
        if (g < 0) g = 0; /* landed out of order (a cancelled one counted at its end): together */
        if (!p->gaps || g < p->min_gap) p->min_gap = g;
        if (!p->gaps || g > p->max_gap) p->max_gap = g;
        p->gap_sum += g;
        p->gaps++;
        p->short_gaps += g < c->gap_s - 1e-6;
    }
    if (u->grant_t > 0) {
        float l = (float)(at - u->grant_t);
        if (l > 0.f && (!(p->lead_seen > 0.f) || l < p->lead_seen)) p->lead_seen = l;
    }
    if (at > p->last_impact_t) p->last_impact_t = at, p->last_id = u->id;
    p->impacts++;
    if (gap) *gap = g;
}

int somvr_pace_update(somvr_pace *p, const somvr_pace_cfg *c, double t, somvr_pace_turn *out, double *gaps, int cap) {
    int n = 0;
    for (;;) { /* the landed blows in impact order */
        somvr_pace_turn *first = 0;
        for (int i = 0; i < SOMVR_PACE_TURNS; i++) {
            somvr_pace_turn *u = &p->turn[i];
            if (pace_live(u) && u->impact_t > 0 && u->impact_t <= t && (!first || u->impact_t < first->impact_t))
                first = u;
        }
        if (!first) break;
        double g, at = first->impact_t;
        pace_land(p, c, first, at, &g);
        if (n < cap) {
            if (out) out[n] = *first;
            if (gaps) gaps[n] = g;
        }
        n++;
        pace_finish(p, c, first, at); /* an answer hold (a marked orc): the turn stays its own */
    }
    for (int i = 0; i < SOMVR_PACE_TURNS; i++) { /* grants that never started a blow; answer holds run out */
        somvr_pace_turn *u = &p->turn[i];
        if (!pace_live(u) || u->start_t > 0) continue;
        if (u->hold_t > 0) {
            if (t >= u->hold_t) {
                p->expired++;
                memset(u, 0, sizeof *u);
            }
        } else if (t - u->grant_t > (double)c->start_s) {
            p->let_go++;
            memset(u, 0, sizeof *u);
        }
    }
    return n;
}

int somvr_pace_grant(somvr_pace *p, const somvr_pace_cfg *c, uint64_t id, double t) {
    (void)c;
    if (!id) return 0;
    int over = pace_others(p, id);
    p->grants++;
    p->overlaps += over;
    somvr_pace_turn *u = pace_slot(p, id, 1);
    if (!u) u = pace_free_slot(p);
    if (!u) {
        p->full++;
        return over;
    }
    memset(u, 0, sizeof *u);
    u->id = id, u->grant_t = t;
    return over;
}

int somvr_pace_start(somvr_pace *p, const somvr_pace_cfg *c, uint64_t id, double t0, float impact_s, int keyless) {
    (void)c;
    if (!id) return 0;
    somvr_pace_turn *u = pace_slot(p, id, 1);
    if (keyless || !(impact_s >= SOMVR_PACE_MIN_WIND_S)) { /* a move, not a blow (no impact key: a body-collision
                                                            * move; one at 0 s: no wind-up): counted; a granted orc's
                                                            * turn holds on for its blow (or start_s) */
        p->moves++;
        return SOMVR_PACE_MOVE | (u ? (u->hold_t > 0 ? SOMVR_PACE_ANSWER : SOMVR_PACE_GRANTED) : 0);
    }
    int r = 0;
    if (pace_others(p, id)) r |= SOMVR_PACE_OVERLAP, p->overlaps++;
    if (u && u->hold_t > 0) { /* its answer, in its own held turn */
        r |= SOMVR_PACE_ANSWER;
        p->answers++;
        u->hold_t = 0, u->hit = 0;
    } else if (u) {
        r |= SOMVR_PACE_GRANTED;
    } else {
        somvr_pace_turn *old = pace_slot(p, id, 0); /* its previous blow still winding up: replaced by this one */
        u = old ? old : pace_free_slot(p);
        r |= SOMVR_PACE_UNANNOUNCED;
        p->unannounced++;
        if (!u) {
            p->full++;
            return r;
        }
        memset(u, 0, sizeof *u);
        u->id = id;
    }
    p->starts++;
    u->start_t = t0 > 0 ? t0 : 1e-9;
    u->impact_t = u->start_t + (double)impact_s;
    return r;
}

int somvr_pace_end(somvr_pace *p, const somvr_pace_cfg *c, uint64_t id, double t, float near_s, double *gap) {
    somvr_pace_turn *u = 0;
    for (int i = 0; i < SOMVR_PACE_TURNS && !u; i++)
        if (p->turn[i].id == id && id && p->turn[i].start_t > 0) u = &p->turn[i];
    if (!u) return -1;
    int landed = t >= u->impact_t - (double)near_s;
    if (gap) *gap = -1;
    if (landed)
        pace_land(p, c, u, t < u->impact_t ? t : u->impact_t, gap);
    else
        p->cancelled++;
    pace_finish(p, c, u, t); /* a marked orc's blow cut short (our cut met or stopped it): its answer hold */
    return landed;
}

int somvr_pace_may_answer(somvr_pace *p, const somvr_pace_cfg *c, uint64_t id) {
    if (!(c->answer_s > 0.f) || !id) return 0;
    for (int i = 0; i < SOMVR_PACE_TURNS; i++)
        if (p->turn[i].id == id) {
            p->turn[i].answer = 1;
            return 1;
        }
    return 0;
}

int somvr_pace_verdict(somvr_pace *p, const somvr_pace_cfg *c, uint64_t id, double t, int hit) {
    for (int i = 0; i < SOMVR_PACE_TURNS; i++) {
        somvr_pace_turn *u = &p->turn[i];
        if (u->id != id || !id || !u->answer) continue;
        if (u->start_t > 0) { /* the verdict before its landing (the same frame): the landing decides with it */
            u->hit = hit ? 1 : -1;
            return hit ? 1 : 0;
        }
        if (!(u->hold_t > 0)) continue;
        if (u->hit > 0) return 1; /* held already */
        if (hit) {
            u->hit = 1, u->hold_t = t + (double)c->answer_s;
            p->holds++;
            return 1;
        }
        p->no_hit++;
        memset(u, 0, sizeof *u);
        return 0;
    }
    return -1;
}

int somvr_pace_release(somvr_pace *p, uint64_t id) {
    for (int i = 0; i < SOMVR_PACE_TURNS; i++) {
        somvr_pace_turn *u = &p->turn[i];
        if (u->id == id && id && u->hold_t > 0 && !(u->start_t > 0)) {
            p->released++;
            memset(u, 0, sizeof *u);
            return 1;
        }
    }
    return 0;
}

int somvr_pace_holding(const somvr_pace *p, uint64_t id, double *since) {
    for (int i = 0; i < SOMVR_PACE_TURNS; i++) {
        const somvr_pace_turn *u = &p->turn[i];
        if (u->id == id && id && u->hold_t > 0 && !(u->start_t > 0)) {
            if (since) *since = u->hold0_t;
            return u->hit > 0 ? 2 : 1;
        }
    }
    return 0;
}

int somvr_pace_pass_wins(int new_prio, float new_score, int kept_prio, float kept_score) {
    if (!!new_prio != !!kept_prio) return new_prio != 0;
    return new_score > kept_score;
}
