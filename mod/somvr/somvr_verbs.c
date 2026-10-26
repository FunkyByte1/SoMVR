/* somvr_verbs.c - SOMVR_VERBS: the left hand on an orc - the grab, the wraith hand, the interrogation's pull (pure;
 * somvr_verbs.h) */
#include "somvr_verbs.h"

#include <math.h>
#include <string.h>

void somvr_vb_cfg_default(somvr_vb_cfg *c)
{
    c->reach_cm = 45, c->cone_deg = 25, c->out_cm = 35, c->far_cm = 170;
    c->grip_on = 0.7f, c->grip_off = 0.35f, c->trig_on = 0.6f, c->trig_off = 0.4f;
    c->ask_cm = 35, c->ask_s = 0.25f, c->hold_max_s = 6, c->cool_s = 0.4f;
}

const char *somvr_vb_event_name(int e)
{
    static const char *const n[SOMVR_VBE_N] = { "none", "grab", "release", "lost", "miss", "wraith", "wraith end", "done",
                                                "ask" };
    return e >= 0 && e < SOMVR_VBE_N ? n[e] : "?";
}

void somvr_vb_hand_reset(somvr_vb_hand *s)
{
    memset(s, 0, sizeof *s);
    s->orc = -1, s->t_end = -1e9, s->near_t = -1;
}

int somvr_vb_trigger_taken(const somvr_vb_hand *s) { return s->phase != SOMVR_VBH_IDLE; }

static float d3(const float a[3], const float b[3])
{
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrtf(x * x + y * y + z * z);
}

static int fin3(const float v[3]) { return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]); }

/* the head the palm is on (or reached out at): its index, -1 = none; *nearest the nearest head (any distance) */
static int pick_head(const somvr_vb_cfg *c, const somvr_vb_hand_in *in, float *dist, float *cone, int *by_cone,
                     int *nearest, float *nd)
{
    float rd[3] = { 0, 0, 0 }, rl = 0;
    int eye = in->have_eye && fin3(in->eye);
    if (eye) {
        for (int k = 0; k < 3; k++)
            rd[k] = in->palm[k] - in->eye[k];
        rl = sqrtf(rd[0] * rd[0] + rd[1] * rd[1] + rd[2] * rd[2]);
        eye = rl >= c->out_cm;
    }
    int best = -1;
    float score = INFINITY;
    *nearest = -1, *nd = INFINITY, *dist = INFINITY, *cone = -1, *by_cone = 0;
    for (int i = 0; i < in->norc && i < SOMVR_VB_ORCS; i++) {
        if (!fin3(in->head[i]))
            continue;
        float d = d3(in->palm, in->head[i]), a = -1, sc = INFINITY;
        if (d < *nd)
            *nd = d, *nearest = i;
        if (eye) {
            float hd[3] = { in->head[i][0] - in->eye[0], in->head[i][1] - in->eye[1], in->head[i][2] - in->eye[2] };
            float hl = sqrtf(hd[0] * hd[0] + hd[1] * hd[1] + hd[2] * hd[2]);
            if (hl > 1 && hl <= c->far_cm) {
                float cs = (rd[0] * hd[0] + rd[1] * hd[1] + rd[2] * hd[2]) / (rl * hl);
                a = acosf(cs > 1 ? 1 : cs < -1 ? -1 : cs) * 57.29578f;
            }
        }
        if (d <= c->reach_cm)
            sc = d;
        else if (a >= 0 && a <= c->cone_deg)
            sc = c->reach_cm + a;
        if (sc < score)
            score = sc, best = i, *dist = d, *cone = a, *by_cone = !(d <= c->reach_cm);
    }
    return best;
}

/* B (the wraith hand) for this step: held while the trigger is, until the game's hold ends or hold_max */
static void wraith_b(somvr_vb_hand *s, const somvr_vb_cfg *c, const somvr_vb_hand_in *in, somvr_vb_hand_out *o)
{
    if (s->b && (in->game_done || in->t - s->tb >= c->hold_max_s)) {
        s->b = 0, s->b_done = 1;
        o->event = SOMVR_VBE_DONE, o->held_s = (float)(in->t - s->tb);
    }
    o->b = s->b;
}

static void to_idle(somvr_vb_hand *s, double t)
{
    s->phase = SOMVR_VBH_IDLE, s->b = s->b_done = 0, s->orc = -1, s->t_end = t;
}

void somvr_vb_hand_step(somvr_vb_hand *s, const somvr_vb_cfg *c, const somvr_vb_hand_in *in, somvr_vb_hand_out *o)
{
    memset(o, 0, sizeof *o);
    o->orc = -1, o->dist_cm = INFINITY, o->cone_deg = -1;
    float sq = isfinite(in->squeeze) ? in->squeeze : 0, tr = isfinite(in->trigger) ? in->trigger : 0;
    int was = s->closed, twas = s->trig;
    s->closed = was ? sq > c->grip_off : sq >= c->grip_on;
    s->trig = twas ? tr > c->trig_off : tr >= c->trig_on;
    int edge = s->closed && !was, tedge = s->trig && !twas;
    if (s->phase != SOMVR_VBH_IDLE && !in->live) {
        o->event = SOMVR_VBE_LOST, o->held_s = (float)(in->t - s->t0);
        to_idle(s, in->t);
        return;
    }
    if (s->phase == SOMVR_VBH_GRAB) {
        if (!s->closed) {
            o->event = SOMVR_VBE_RELEASE, o->held_s = (float)(in->t - s->t0);
            to_idle(s, in->t);
            return;
        }
        o->grab = 1, o->orc = s->orc;
        if (!s->trig)
            s->b = s->b_done = 0;
        else if (tedge && !s->b_done)
            s->b = 1, s->tb = in->t, o->event = SOMVR_VBE_WRAITH;
        wraith_b(s, c, in, o);
        /* the pull to the face: the interrogation, once per grab */
        if (in->have_eye && in->have_palm && fin3(in->eye) && fin3(in->palm) && !s->asked) {
            float de = d3(in->palm, in->eye);
            if (de >= c->ask_cm + 10) {
                s->far_seen = 1, s->near_t = -1;
            } else if (de <= c->ask_cm && s->far_seen) {
                if (s->near_t < 0)
                    s->near_t = in->t;
                if (in->t - s->near_t >= c->ask_s && !o->event)
                    s->asked = 1, o->ask = 1, o->event = SOMVR_VBE_ASK;
            } else if (de > c->ask_cm) {
                s->near_t = -1;
            }
        }
        return;
    }
    if (s->phase == SOMVR_VBH_WRAITH) {
        if (!s->trig) {
            o->event = SOMVR_VBE_WRAITH_END, o->held_s = (float)(in->t - s->tb);
            to_idle(s, in->t);
            return;
        }
        o->orc = s->orc;
        wraith_b(s, c, in, o);
        return;
    }
    /* idle: a new squeeze = the grab; a new trigger press with the open hand = the wraith hand */
    if ((!edge && !tedge) || !in->live || !in->have_palm || !fin3(in->palm))
        return;
    int nearest, by_cone;
    float nd, dist, cone;
    int best = pick_head(c, in, &dist, &cone, &by_cone, &nearest, &nd);
    o->edge = edge;
    o->orc = best >= 0 ? best : nearest, o->dist_cm = best >= 0 ? dist : nd, o->cone_deg = best >= 0 ? cone : -1;
    o->by_cone = best >= 0 && by_cone;
    if (in->t - s->t_end < c->cool_s)
        return;
    if (edge && best >= 0) {
        o->event = SOMVR_VBE_GRAB, o->grab = 1;
        s->phase = SOMVR_VBH_GRAB, s->orc = best, s->t0 = in->t, s->asked = 0, s->near_t = -1;
        s->far_seen = in->have_eye && fin3(in->eye) && d3(in->palm, in->eye) >= c->ask_cm + 10;
        s->b = 0, s->b_done = s->trig; /* both at once: the grab first, the trigger counts from its next press */
    } else if (tedge && !s->closed && best >= 0) {
        o->event = SOMVR_VBE_WRAITH, o->b = 1;
        s->phase = SOMVR_VBH_WRAITH, s->orc = best, s->t0 = s->tb = in->t, s->b = 1, s->b_done = 0;
    } else if (edge && nearest >= 0 && nd <= 2 * c->reach_cm) {
        o->event = SOMVR_VBE_MISS;
    }
}

static int has(const char *s, const char *sub) { return s && strstr(s, sub) != NULL; }

int somvr_vb_attack_kind(const char *n)
{
    if (!n || !n[0])
        return SOMVR_VBK_NONE;
    if (has(n, "_HoG_") || has(n, "_HOG_") || !strncmp(n, "PC_HoG", 6)) /* the grab's drain: PC_Grab_HOG_Into, _Kill, _Out */
        return has(n, "Complete") || has(n, "_Out") || has(n, "_Kill") ? SOMVR_VBK_HOG_END : SOMVR_VBK_HOG;
    if (!strncmp(n, "PL_St_", 6))
        return SOMVR_VBK_STUN;
    if (has(n, "Interrogat") || has(n, "Intrgtn")) /* PC_Bip_Sync_Intrgtn_Into_NoBrand, _Idle_NoBrand */
        return SOMVR_VBK_ASK;
    if (!strncmp(n, "PC_Grab", 7))
        return SOMVR_VBK_GRAB;
    if (has(n, "SneakInstaKill") || has(n, "StealthTorment") || has(n, "StealthBrand"))
        return SOMVR_VBK_STEALTH;
    return SOMVR_VBK_NONE;
}

const char *somvr_vb_kind_name(int k)
{
    static const char *const n[SOMVR_VBK_N] = { "-", "the hold (HoG)", "the hold's end", "the stun", "a grab", "a stealth kill",
                                                "the interrogation" };
    return k >= 0 && k < SOMVR_VBK_N ? n[k] : "?";
}
