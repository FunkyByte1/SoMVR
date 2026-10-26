/* somvr_slots.c - SOMVR_SLOTS=1: the body slots' geometry and gesture state machine (somvr_slots.h). Pure. */
#include "somvr_slots.h"

#include <math.h>
#include <string.h>

#define SL_PI 3.14159265358979f

void somvr_slots_cfg_default(somvr_slots_cfg *c) {
    memset(c, 0, sizeof *c);
    c->main_hand = 1; /* SOMVR_HAND_R */
    c->sword_ofs[0] = 10, c->sword_ofs[1] = -10, c->sword_ofs[2] = -15;
    c->sword_r_cm = 20;
    c->dagger_at = SOMVR_SLOTS_DAGGER_HIP;
    c->hip_ofs[0] = -18, c->hip_ofs[1] = -62, c->hip_ofs[2] = -5;
    c->back_ofs[0] = -8, c->back_ofs[1] = -55, c->back_ofs[2] = -22;
    c->dagger_r_cm = 15;
    c->exit_cm = 4;
    c->approach_cm[SOMVR_SLOT_SWORD] = 15, c->approach_cm[SOMVR_SLOT_DAGGER] = 0;
    c->deadband_deg = 30;
    c->grip_on = 0.6f, c->grip_off = 0.4f;
    c->quick_s = 0.35f;
    c->hold = 0;
    c->restore = 1;
    c->ask_s = 0.5f;
    c->reassert = 1, c->reassert_max = 2, c->reassert_s = 3.0f;
}

void somvr_slots_init(somvr_slots *s) {
    memset(s, 0, sizeof *s);
    for (int k = 0; k < SOMVR_NSLOT; k++) s->want[k] = -1, s->keep_last[k] = -1;
}

void somvr_slots_reset(somvr_slots *s) {
    for (int h = 0; h < 2; h++) s->down[h] = 0, s->took[h] = 0;
    s->quick_armed = 0;
    for (int k = 0; k < SOMVR_NSLOT; k++) {
        s->inside[k] = s->entered_closed[k] = s->by_hold[k] = s->suspended[k] = 0;
        s->have_last[k] = 0, s->want[k] = -1, s->keep_last[k] = -1, s->ra_n[k] = 0;
    }
}

const char *somvr_slots_name(int k) { return k == SOMVR_SLOT_SWORD ? "sword" : k == SOMVR_SLOT_DAGGER ? "dagger" : "?"; }

int somvr_slots_hand(const somvr_slots_cfg *c, int k) {
    int m = c->main_hand ? 1 : 0;
    return k == SOMVR_SLOT_SWORD ? m : 1 - m;
}

static float wrap_pi(float a) {
    a = fmodf(a + SL_PI, 2 * SL_PI);
    if (a <= 0) a += 2 * SL_PI;
    return a - SL_PI;
}

float somvr_slots_heading(sv_quat q) {
    sv_quat n = sv_qnormalize(q);
    sv_vec3 f = sv_qrot(n, (sv_vec3){0, 0, 1}), u = sv_qrot(n, (sv_vec3){0, 1, 0});
    float h = sqrtf(f.x * f.x + f.z * f.z);
    /* w = 0 up to 60 deg of pitch (|f_xz| >= 0.5), 1 looking straight down or up; the up vector tilts ahead when the
     * head looks down (f.y < 0) and back when it looks up, so -sign(f.y) u points ahead in both */
    float w = h >= 0.5f ? 0 : (0.5f - h) / 0.5f, sg = f.y < 0 ? 1.0f : -1.0f;
    float x = f.x + w * sg * u.x, z = f.z + w * sg * u.z;
    if (!(x * x + z * z > 1e-12f)) return 0;
    return atan2f(x, z);
}

float somvr_slots_torso(float last, float head_yaw, int hold, float deadband) {
    if (!hold || !(deadband > 0)) return wrap_pi(head_yaw);
    float d = wrap_pi(head_yaw - last);
    if (d > deadband) return wrap_pi(head_yaw - deadband);
    if (d < -deadband) return wrap_pi(head_yaw + deadband);
    return wrap_pi(last);
}

int somvr_slots_ofs(const somvr_slots_cfg *c, int k, sv_vec3 *ofs) {
    const float *o;
    if (k == SOMVR_SLOT_SWORD)
        o = c->sword_ofs;
    else if (k == SOMVR_SLOT_DAGGER && c->dagger_at == SOMVR_SLOTS_DAGGER_HIP)
        o = c->hip_ofs;
    else if (k == SOMVR_SLOT_DAGGER && c->dagger_at == SOMVR_SLOTS_DAGGER_BACK)
        o = c->back_ofs;
    else {
        *ofs = (sv_vec3){0, 0, 0};
        return 0;
    }
    float mx = c->main_hand ? 1.0f : -1.0f;
    *ofs = (sv_vec3){mx * o[0], o[1], o[2]};
    return 1;
}

float somvr_slots_radius(const somvr_slots_cfg *c, int k) { return k == SOMVR_SLOT_SWORD ? c->sword_r_cm : c->dagger_r_cm; }

sv_vec3 somvr_slots_pos(const somvr_slots_cfg *c, int k, sv_vec3 head, float torso_yaw) {
    sv_vec3 o;
    somvr_slots_ofs(c, k, &o);
    sv_vec3 r = sv_qrot(sv_qyaw(torso_yaw), o);
    return (sv_vec3){head.x + r.x, head.y + r.y, head.z + r.z};
}

static float dist3(sv_vec3 a, sv_vec3 b) {
    float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return sqrtf(x * x + y * y + z * z);
}

static int finite3(sv_vec3 v) { return isfinite(v.x) && isfinite(v.y) && isfinite(v.z); }

static void ask(somvr_slots *s, somvr_slots_out *o, int k, int want, uint32_t why, double t) {
    s->want[k] = want, s->want_t[k] = t;
    o->ev[k] |= (want ? SOMVR_SL_E_DRAW : SOMVR_SL_E_PUT) | why;
    if (want) s->draws[k]++;
    else s->puts[k]++;
}

void somvr_slots_step(somvr_slots *s, const somvr_slots_cfg *c, const somvr_slots_in *in, somvr_slots_out *o) {
    memset(o, 0, sizeof *o);
    s->frames++;
    double t = in->t;
    int press[2] = {0, 0}, release[2] = {0, 0}, taken[SOMVR_NSLOT] = {0, 0}, given[SOMVR_NSLOT] = {0, 0};
    /* the grips (no live input: open) */
    for (int h = 0; h < 2; h++) {
        float v = in->live && isfinite(in->squeeze[h]) ? in->squeeze[h] : 0;
        int nd = s->down[h] ? v > c->grip_off : v > c->grip_on;
        press[h] = nd && !s->down[h], release[h] = !nd && s->down[h];
        s->down[h] = nd;
    }
    /* the game's state: an ask reached, or a change of its own */
    for (int k = 0; k < SOMVR_NSLOT; k++) {
        int d = in->have[k] && in->drawn[k];
        if (s->have_last[k] && d != s->last_drawn[k]) {
            if (s->want[k] == d)
                o->ev[k] |= SOMVR_SL_E_DONE, s->done[k]++, s->want[k] = -1;
            else if (s->want[k] < 0)
                o->ev[k] |= SOMVR_SL_E_GAME, s->game[k]++,
                    taken[k] = k == SOMVR_SLOT_SWORD && !d && s->keep_last[k] == 1,
                    given[k] = k == SOMVR_SLOT_SWORD && d && s->keep_last[k] == 0;
        }
        if (s->want[k] >= 0 && d == s->want[k]) /* asked for what already is */
            o->ev[k] |= SOMVR_SL_E_DONE, s->done[k]++, s->want[k] = -1;
        s->last_drawn[k] = d, s->have_last[k] = 1;
    }
    /* the torso and the slots */
    int head = in->head_ok && finite3(in->head) && isfinite(in->head_q.w);
    o->head_yaw = head ? somvr_slots_heading(in->head_q) : s->torso;
    int near = 0;
    for (int k = 0; head && s->torso_ok && k < SOMVR_NSLOT; k++) {
        sv_vec3 ofs;
        int h = somvr_slots_hand(c, k);
        if (!(c->approach_cm[k] > 0) || !in->have[k] || !somvr_slots_ofs(c, k, &ofs) || !in->valid[h] || !finite3(in->p[h]))
            continue;
        if (dist3(in->p[h], somvr_slots_pos(c, k, in->head, s->torso)) < somvr_slots_radius(c, k) + c->approach_cm[k])
            near = 1;
    }
    if (head) {
        s->torso = s->torso_ok ? somvr_slots_torso(s->torso, o->head_yaw, near, c->deadband_deg * (SL_PI / 180)) : o->head_yaw;
        s->torso_ok = 1;
    }
    o->torso_yaw = s->torso, o->torso_held = near && head;
    s->held_frames += (uint64_t)o->torso_held;
    int act = in->live && in->allow;
    for (int k = 0; k < SOMVR_NSLOT; k++) {
        sv_vec3 ofs;
        int h = somvr_slots_hand(c, k), on = in->have[k] && somvr_slots_ofs(c, k, &ofs) && s->torso_ok;
        o->dist[k] = -1;
        if (on) o->slot[k] = somvr_slots_pos(c, k, in->head, s->torso);
        int was = s->inside[k];
        if (!on || !head || !in->valid[h] || !finite3(in->p[h])) {
            s->inside[k] = 0;
        } else {
            float d = dist3(in->p[h], o->slot[k]), r = somvr_slots_radius(c, k);
            o->dist[k] = d;
            s->inside[k] = was ? d <= r + c->exit_cm : d <= r;
        }
        if (s->inside[k] && !was) {
            s->entered_closed[k] = s->down[h] && !press[h];
            if (act && !in->busy[h]) o->ev[k] |= SOMVR_SL_E_ENTER, s->enters[k]++;
        } else if (!s->inside[k] && was) {
            s->entered_closed[k] = 0;
            if (act && !in->busy[h]) o->ev[k] |= SOMVR_SL_E_LEAVE;
        }
        o->inside[k] = s->inside[k];
    }
    if (!act) { /* no gestures, no asks, no preference; a suspended weapon stays suspended (the restore waits) */
        for (int h = 0; h < 2; h++) s->took[h] = 0;
        for (int k = 0; k < SOMVR_NSLOT; k++)
            s->want[k] = -1, s->by_hold[k] = 0, o->want[k] = -1, o->keep[k] = -1, s->keep_last[k] = -1;
        s->quick_armed = 0;
        o->blade = in->have[SOMVR_SLOT_SWORD] && in->drawn[SOMVR_SLOT_SWORD];
        return;
    }
    /* the hands needed elsewhere: the weapon back (suspended); then, free again, back to the hand */
    for (int k = 0; k < SOMVR_NSLOT; k++) {
        int h = somvr_slots_hand(c, k), d = in->have[k] && in->drawn[k];
        if (!in->have[k]) {
            s->suspended[k] = 0;
            continue;
        }
        if (in->away[h] && (d || s->want[k] == 1) && s->want[k] != 0) {
            ask(s, o, k, 0, SOMVR_SL_E_SUSPEND, t);
            s->suspended[k] = 1, s->suspends[k]++, s->by_hold[k] = 0;
        } else if (!in->away[h] && !in->busy[h] && s->suspended[k]) {
            s->suspended[k] = 0;
            if (c->restore && !d && s->want[k] != 1) ask(s, o, k, 1, SOMVR_SL_E_RESTORE, t), s->restores[k]++;
        }
    }
    /* the game put away what the player keeps in the hand: back to the hand, a few times; with undraw, the game drew
     * what the player keeps away: back to the slot, on the same budget */
    for (int k = 0; k < SOMVR_NSLOT; k++) {
        int h = somvr_slots_hand(c, k), re = taken[k] && c->reassert, un = given[k] && c->undraw;
        if (!(re || un) || !in->have[k] || in->busy[h] || in->away[h] || s->want[k] >= 0) continue;
        if (t - s->ra_t0[k] > c->reassert_s || s->ra_n[k] == 0) s->ra_t0[k] = t, s->ra_n[k] = 0;
        if (s->ra_n[k] < c->reassert_max) {
            s->ra_n[k]++;
            if (re)
                s->reasserts[k]++, ask(s, o, k, 1, SOMVR_SL_E_REASSERT, t);
            else
                s->undraws[k]++, ask(s, o, k, 0, SOMVR_SL_E_UNDRAW, t);
        } else {
            o->ev[k] |= SOMVR_SL_E_INSIST, s->insists[k]++;
        }
    }
    /* the gestures */
    for (int k = 0; k < SOMVR_NSLOT; k++) {
        int h = somvr_slots_hand(c, k);
        if (!in->have[k] || in->busy[h] || in->away[h] || !in->valid[h]) continue;
        int in_hand = in->drawn[k] ? s->want[k] != 0 : s->want[k] == 1; /* where it is or is going */
        if (press[h] && s->inside[k]) {
            s->took[h] = 1;
            if (in_hand)
                ask(s, o, k, 0, 0, t), s->by_hold[k] = 0;
            else
                ask(s, o, k, 1, 0, t), s->by_hold[k] = c->hold;
            s->suspended[k] = 0;
        } else if (release[h] && s->inside[k] && s->entered_closed[k] && in_hand) {
            ask(s, o, k, 0, SOMVR_SL_E_DROPIN, t), s->dropins[k]++, s->by_hold[k] = 0;
        } else if (release[h] && s->by_hold[k] && in_hand) {
            ask(s, o, k, 0, SOMVR_SL_E_HOLDEND, t), s->holdends[k]++, s->by_hold[k] = 0;
        }
    }
    { /* the quick draw: two presses of the main hand outside its slot within quick_s */
        int k = SOMVR_SLOT_SWORD, h = somvr_slots_hand(c, k);
        int ok = c->quick_s > 0 && in->have[k] && !in->busy[h] && !in->away[h] && in->valid[h];
        if (!ok) {
            s->quick_armed = 0;
        } else if (press[h] && !s->took[h] && !s->inside[k]) {
            int in_hand = in->drawn[k] ? s->want[k] != 0 : s->want[k] == 1;
            if (!in_hand && s->quick_armed && t - s->quick_t <= c->quick_s) {
                s->took[h] = 1, s->quick_armed = 0, s->quick++;
                ask(s, o, k, 1, SOMVR_SL_E_QUICK, t), s->by_hold[k] = c->hold, s->suspended[k] = 0;
            } else {
                s->quick_armed = !in_hand, s->quick_t = t;
            }
        }
    }
    for (int h = 0; h < 2; h++) {
        if (!s->down[h]) s->took[h] = 0;
        o->nograb |= s->took[h];
    }
    for (int k = 0; k < SOMVR_NSLOT; k++) {
        if (s->inside[k] && !s->down[somvr_slots_hand(c, k)]) s->entered_closed[k] = 0;
        int d = in->have[k] && in->drawn[k];
        if (s->want[k] >= 0 && t - s->want_t[k] > c->ask_s) {
            o->ev[k] |= SOMVR_SL_E_DENIED, s->denied[k]++, s->want[k] = -1, s->by_hold[k] = 0;
            if (!d) s->suspended[k] = 0; /* a refused put-back: nothing to bring back */
        }
        int h = somvr_slots_hand(c, k);
        o->want[k] = s->want[k];
        o->keep[k] = !in->have[k] || s->want[k] >= 0 || in->busy[h] || in->away[h] ? -1 : d;
        s->keep_last[k] = o->keep[k];
    }
    o->blade = in->have[SOMVR_SLOT_SWORD] && in->drawn[SOMVR_SLOT_SWORD] && s->want[SOMVR_SLOT_SWORD] != 0;
}
