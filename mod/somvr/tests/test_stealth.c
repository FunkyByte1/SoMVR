/* test_stealth.c - the VR stealth kill's pure part (somvr_stealth.h): the angles, who may be killed, the contact's
 * judge, the stance switch, the cue's ticks */
#include "../somvr_stealth.h"
#include "../somvr_combat.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_fail, g_n;
#define CHECK(c, ...)                                                                                                      \
    do {                                                                                                                   \
        g_n++;                                                                                                             \
        if (!(c)) {                                                                                                        \
            g_fail++;                                                                                                      \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                    \
            printf(__VA_ARGS__);                                                                                           \
            printf("\n");                                                                                                  \
        }                                                                                                                  \
    } while (0)

static somvr_stl_who who_behind(void) { return (somvr_stl_who){ 0, 1, 20.0f, 175.0f, 90.0f, 0 }; }
static somvr_stl_hit hit_back(void) { return (somvr_stl_hit){ 170.0f, 40.0f, 1, 0.5f, 0.3f, NAN, 0, 0, 0, 0 }; }

static void test_angles(void) {
    float o[3] = { 0, 0, 0 }, front[3] = { 0, 0, 100 }, back[3] = { 0, 0, -100 }, right[3] = { 100, 0, 0 };
    CHECK(fabsf(somvr_stl_off_deg(front, o, 0)) < 0.1f, "in front: 0");
    CHECK(fabsf(fabsf(somvr_stl_off_deg(back, o, 0)) - 180.0f) < 0.1f, "behind: 180");
    CHECK(fabsf(somvr_stl_off_deg(right, o, 0) - 90.0f) < 0.1f, "its right: +90 (%f)", somvr_stl_off_deg(right, o, 0));
    CHECK(fabsf(somvr_stl_off_deg(back, o, 3.14159265f)) < 0.1f, "turned round: the back point is in front");
    CHECK(somvr_stl_off_deg(o, o, 1.0f) == 0.0f, "the same point: 0");
}

static void test_possible(void) {
    somvr_stl_cfg c;
    somvr_stl_defaults(&c);
    somvr_stl_who w = who_behind();
    CHECK(somvr_stl_possible(&c, &w) == SOMVR_STL_KILL, "crouched behind an unaware orc");
    w.aware = 1;
    CHECK(somvr_stl_possible(&c, &w) == SOMVR_STL_AWARE, "aware: a normal fight");
    w.aware = -1;
    CHECK(somvr_stl_possible(&c, &w) == SOMVR_STL_UNKNOWN, "not readable: never a kill");
    w = who_behind(), w.dead = 1;
    CHECK(somvr_stl_possible(&c, &w) == SOMVR_STL_DEAD, "dead");
    w = who_behind(), w.player_off_deg = 100.0f;
    CHECK(somvr_stl_possible(&c, &w) == SOMVR_STL_FRONT, "at its side (100 deg)");
    w.player_off_deg = -115.0f;
    CHECK(somvr_stl_possible(&c, &w) == SOMVR_STL_KILL, "behind on its left (-115)");
    w = who_behind(), w.crouched = 0, w.speed_cms = 60.0f;
    CHECK(somvr_stl_possible(&c, &w) == SOMVR_STL_KILL, "standing but slow (either)");
    w.speed_cms = 200.0f;
    CHECK(somvr_stl_possible(&c, &w) == SOMVR_STL_STANCE, "standing and running");
    w.speed_cms = NAN;
    CHECK(somvr_stl_possible(&c, &w) == SOMVR_STL_STANCE, "standing, no speed");
    c.stance = SOMVR_STL_STANCE_CROUCH, w.speed_cms = 10.0f;
    CHECK(somvr_stl_possible(&c, &w) == SOMVR_STL_STANCE, "crouch only: standing still is not enough");
    w.crouched = 1, w.speed_cms = 300.0f;
    CHECK(somvr_stl_possible(&c, &w) == SOMVR_STL_KILL, "crouch only: a fast crouched rush counts");
    c.stance = SOMVR_STL_STANCE_SLOW;
    CHECK(somvr_stl_possible(&c, &w) == SOMVR_STL_STANCE, "slow only: the crouched rush does not");
    w.speed_cms = 30.0f, w.crouched = 0;
    CHECK(somvr_stl_possible(&c, &w) == SOMVR_STL_KILL, "slow only: slow");
}

static void test_judge(void) {
    somvr_stl_cfg c;
    somvr_stl_defaults(&c);
    somvr_stl_who w = who_behind();
    somvr_stl_hit h = hit_back();
    CHECK(somvr_stl_judge(&c, &w, &h) == SOMVR_STL_KILL, "a light hit into the back");
    h.tier = 0;
    CHECK(somvr_stl_judge(&c, &w, &h) == SOMVR_STL_WEAK, "a slow touch");
    h.axis_mps = 1.0f, h.cos_axis = 0.9f;
    CHECK(somvr_stl_judge(&c, &w, &h) == SOMVR_STL_KILL, "a slow stab along the blade");
    h.cos_axis = -0.9f;
    CHECK(somvr_stl_judge(&c, &w, &h) == SOMVR_STL_KILL, "the stab's sign does not matter (the reverse grip)");
    h.cos_axis = 0.5f;
    CHECK(somvr_stl_judge(&c, &w, &h) == SOMVR_STL_WEAK, "a slow push across the blade");
    h = hit_back(), h.contact_off_deg = 40.0f;
    CHECK(somvr_stl_judge(&c, &w, &h) == SOMVR_STL_SIDE, "reached round into its chest");
    h = hit_back(), h.contact_up_cm = -40.0f;
    CHECK(somvr_stl_judge(&c, &w, &h) == SOMVR_STL_LOW, "its legs");
    h.contact_up_cm = 120.0f;
    CHECK(somvr_stl_judge(&c, &w, &h) == SOMVR_STL_LOW, "over its head");
    h = hit_back(), h.tier = 2, w.aware = 1;
    CHECK(somvr_stl_judge(&c, &w, &h) == SOMVR_STL_AWARE, "a heavy hit into an aware orc's back: the ordinary hit");
    w = who_behind(), w.player_off_deg = 10.0f, h.contact_off_deg = 170.0f;
    CHECK(somvr_stl_judge(&c, &w, &h) == SOMVR_STL_FRONT, "from the front, reaching round: no");
    CHECK(somvr_stl_stance_of("crouch") == SOMVR_STL_STANCE_CROUCH && somvr_stl_stance_of("either") == 2 &&
          somvr_stl_stance_of("x") == -1 && somvr_stl_stance_of(NULL) == -1, "stance words");
    CHECK(!strcmp(somvr_stl_stance_name(1), "slow") && !strcmp(somvr_stl_why(SOMVR_STL_KILL), "the stealth kill") &&
          !strcmp(somvr_stl_why(99), "?"), "names");
}

/* build 20c: the sword's tier here - build 16's rule's, or where that sees nothing the stroke's. The replay: the
 * scripted stab's first contact at 20 frames a second (runs 20261028-164235/164926: "tier none, along 1.78 m/s cos
 * 0.50", combat's "light (build 16's rule: none) - the stroke 32 cm, the contact point 3.9 m/s ... travel since the
 * crossing 0 cm"): the blade along +z still, then 2 and 4 m/s at 60 deg off its axis; the window's mean crosses the
 * slash speed on the contact's sample, so build 16's rule has no travel yet - a touch to the judge, while the stroke's
 * rule deals a light hit that wakes the unaware orc (build 20b) */
static void test_tier(void) {
    CHECK(somvr_stl_tier(SC_TIER_NONE, SC_TIER_NONE) == SC_TIER_NONE && somvr_stl_tier(SC_TIER_STAB, SC_TIER_NONE) == SC_TIER_STAB &&
              somvr_stl_tier(SC_TIER_NONE, SC_TIER_LIGHT) == SC_TIER_LIGHT &&
              somvr_stl_tier(SC_TIER_LIGHT, SC_TIER_HEAVY) == SC_TIER_LIGHT,
          "the tier: build 16's when it has one, else the stroke's");
    sc_cfg c;
    sc_cfg_default(&c);
    c.stroke = 1, c.tip_max_mps = 60; /* build 20's SOMVR_COMBAT_SWING=stroke */
    sc_speed s;
    sc_speed_reset(&s);
    somvr_blade b;
    memset(&b, 0, sizeof b);
    b.valid = 1, b.base[2] = 0, b.tip[2] = 100;
    for (int i = 0; i <= 4; i++) /* still for 0.2 s */
        b.t = 0.05 * i, sc_speed_push(&s, &c, &b);
    const float mps[2] = { 2.0f, 4.0f }, sn = 0.8660254f, cs = 0.5f;
    for (int k = 0; k < 2; k++) {
        float v[3] = { 100 * mps[k] * sn, 0, 100 * mps[k] * cs };
        for (int i = 0; i < 3; i++)
            b.base[i] += v[i] * 0.05f, b.tip[i] += v[i] * 0.05f, b.vel_tip[i] = v[i], b.vel_base[i] = v[i];
        b.t += 0.05, sc_speed_push(&s, &c, &b);
    }
    int t16 = sc_tier_cut(&c, &s, NAN), tsw = sc_tier_swing(&c, &s, NAN, 1.0f);
    CHECK(t16 == SC_TIER_NONE && sc_tier(&c, &s) == SC_TIER_NONE && tsw == SC_TIER_LIGHT && sc_stroke_entered(&s, 0, b.t),
          "the replay: build 16's none (travel %.1f), the stroke's light (reach %.1f cm, %.2f m/s; got %d %d)", s.travel,
          s.reach, s.mean_mps, t16, tsw);
    CHECK(fabsf(s.cos_axis - 0.5f) < 0.01f && s.axis_mps > 0.8f && s.axis_mps < c.stab_mps, "along %.2f m/s cos %.2f",
          s.axis_mps, s.cos_axis);
    somvr_stl_cfg sc;
    somvr_stl_defaults(&sc);
    somvr_stl_who w = who_behind();
    somvr_stl_hit h = { 170.0f, 65.0f, t16, s.axis_mps, s.cos_axis, NAN, 0, 0, 0, 0 };
    CHECK(somvr_stl_judge(&sc, &w, &h) == SOMVR_STL_WEAK, "build 20b: build 16's tier alone - a touch (the light hit woke it)");
    h.tier = somvr_stl_tier(t16, tsw);
    CHECK(somvr_stl_judge(&sc, &w, &h) == SOMVR_STL_KILL, "build 20c: the stroke's light hit - the stealth kill");
    /* build 21: that first contact (the point 3 cm in, sideways) is a touch - no damage, nothing woken; the kill comes as
     * the point goes on in */
    sc.thrust = 1, h.depth_cm = 3.0f, h.tier_cut = tsw;
    CHECK(somvr_stl_judge(&sc, &w, &h) == SOMVR_STL_TOUCH, "build 21: the sideways first contact - a touch, not the ordinary hit");
    sc.thrust = 0;
    /* SOMVR_COMBAT_SWING=off: combat's tier is build 16's - nothing changes (that contact a glance, as build 19b's) */
    CHECK(somvr_stl_tier(t16, t16) == t16, "swing off: build 16's alone");
}

static void test_cue(void) {
    somvr_stl_cue q;
    memset(&q, 0, sizeof q);
    CHECK(!somvr_stl_cue_step(&q, 0, 0, 0.0, 1.5), "nothing");
    CHECK(somvr_stl_cue_step(&q, 0x100, 1, 1.0, 1.5), "possible: one tick");
    CHECK(!somvr_stl_cue_step(&q, 0x100, 1, 1.1, 1.5) && !somvr_stl_cue_step(&q, 0x100, 1, 3.0, 1.5), "held: quiet");
    CHECK(!somvr_stl_cue_step(&q, 0, 0, 3.1, 1.5), "gone");
    CHECK(!somvr_stl_cue_step(&q, 0x100, 1, 3.5, 1.5), "back after 0.4 s: no tick (a flicker at the arc's edge)");
    CHECK(!somvr_stl_cue_step(&q, 0, 0, 3.6, 1.5) && somvr_stl_cue_step(&q, 0x100, 1, 5.2, 1.5),
          "back after 1.6 s: a tick");
    CHECK(somvr_stl_cue_step(&q, 0x200, 1, 5.3, 1.5), "another orc: a tick");
    CHECK(q.ticks == 3 && q.orc == 0x200, "3 ticks (%llu)", (unsigned long long)q.ticks);
}

static void test_standoff(void) {
    const float orc[3] = { 0, 0, 0 }, r = 118.0f, hz = 8.0f, dt = 0.05f;
    float out[3], p[3] = { 0, 0, -300 }, d[3] = { 0, 0, 7.0f };
    CHECK(!somvr_stl_standoff(p, orc, r, dt, hz, d, out) && out[2] == 0, "far: the step free");
    /* walk straight at it at 140 cm/s (20 Hz): stops short of r, never inside, smoothly */
    float last = 1e9f;
    int cut = 0, mono = 1;
    for (int i = 0; i < 200; i++) {
        float step[3] = { 0, 0, 7.0f };
        if (somvr_stl_standoff(p, orc, r, dt, hz, step, out))
            cut++, step[0] += out[0], step[2] += out[2];
        p[2] += step[2];
        mono &= step[2] <= last + 1e-4f;
        if (p[2] > -r - 20)
            last = step[2];
    }
    CHECK(cut > 0 && -p[2] >= r - 1e-3f && -p[2] < r + 1.0f, "walked in: stopped at %.2f (r %.0f)", -p[2], r);
    CHECK(mono, "the steps only shrink near the edge");
    /* sideways at the edge: free */
    float q[3] = { 0, 0, -r }, side[3] = { 7.0f, 0, 0 };
    CHECK(!somvr_stl_standoff(q, orc, r, dt, hz, side, out), "sideways along the edge: free");
    float back[3] = { 0, 0, -7.0f };
    CHECK(!somvr_stl_standoff(q, orc, r, dt, hz, back, out), "back: free");
    /* inside already (the orc stepped back into him): no push, no step further in */
    float in[3] = { 0, 0, -90 }, fw[3] = { 0, 0, 5.0f };
    CHECK(somvr_stl_standoff(in, orc, r, dt, hz, fw, out) && fabsf(out[2] + 5.0f) < 1e-4f, "inside: the inward step cut to 0");
    CHECK(!somvr_stl_standoff(in, orc, r, dt, hz, back, out) && out[2] == 0, "inside: back out free, no push");
    /* a diagonal at the edge keeps its sideways part */
    float diag[3] = { 5.0f, 0, 5.0f };
    CHECK(somvr_stl_standoff(q, orc, r, dt, hz, diag, out) && fabsf(diag[0] + out[0] - 5.0f) < 1e-4f &&
          fabsf(diag[2] + out[2]) < 1e-4f, "diagonal: the inward half cut, the sideways half kept");
    /* frame-rate independent: 90 Hz stops at the same place */
    float p2[3] = { 0, 0, -300 };
    for (int i = 0; i < 900; i++) {
        float step[3] = { 0, 0, 140.0f / 90.0f };
        if (somvr_stl_standoff(p2, orc, r, 1.0f / 90.0f, hz, step, out))
            step[2] += out[2];
        p2[2] += step[2];
    }
    CHECK(-p2[2] >= r - 1e-3f && -p2[2] < r + 1.0f, "90 Hz: stopped at %.2f", -p2[2]);
    float z[3] = { 0, 0, 0 };
    CHECK(!somvr_stl_standoff(z, orc, r, dt, hz, d, out) && !somvr_stl_standoff(p, orc, 0, dt, hz, d, out), "degenerate");
}

/* build 19b: the brake (SOMVR_STEALTH_KILL_STANDOFF_DECEL): a steady deceleration to rest on the edge */
static void test_brake(void) {
    const float orc[3] = { 0, 0, 0 }, r = 118.0f, a = 250.0f;
    float out[3];
    for (int pass = 0; pass < 3; pass++) {
        const float hzs[3] = { 20, 72, 90 }, v0s[3] = { 150, 150, 40 };
        float dt = 1.0f / hzs[pass], v0 = v0s[pass], p[3] = { 0, 0, -300 }, start = -1, last = 1e9f, prev_v = v0;
        float dv_max = 0, t = 0, t0 = -1, t1 = -1;
        int mono = 1, inside = 0;
        for (int i = 0; i < 2000; i++, t += dt) {
            float step[3] = { 0, 0, v0 * dt };
            if (somvr_stl_standoff_brake(p, orc, r, dt, a, step, out)) {
                if (start < 0)
                    start = -p[2] - r, t0 = t;
                step[0] += out[0], step[2] += out[2];
            }
            p[2] += step[2];
            inside |= -p[2] < r - 1e-3f;
            mono &= step[2] <= last + 1e-4f;
            last = step[2];
            float v = step[2] / dt;
            if (start >= 0 && prev_v > 10.0f && prev_v - v > dv_max) /* (under 10 cm/s: the last tick to rest) */
                dv_max = prev_v - v;
            if (start >= 0 && t1 < 0 && v < 1.0f)
                t1 = t;
            prev_v = v;
        }
        float want = v0 * v0 / (2 * a), decel = dv_max / dt;
        CHECK(!inside && -p[2] < r + 0.05f, "%.0f Hz %.0f cm/s: at rest on the edge (%.3f)", hzs[pass], v0, -p[2]);
        CHECK(mono, "%.0f Hz: the steps only shrink", hzs[pass]);
        CHECK(start > want - v0 * dt - 1 && start < want + 1, "%.0f Hz %.0f cm/s: the brake starts %.1f cm out (v^2/2a %.1f)",
              hzs[pass], v0, start, want);
        /* the deceleration never above ~1.5 x the asked (the discrete steps), the time ~v0/a */
        CHECK(decel < (hzs[pass] < 50 ? 2.0f : 1.3f) * a, "%.0f Hz: the largest deceleration over 10 cm/s %.0f cm/s^2 (asked "
              "%.0f)", hzs[pass], decel, a);
        CHECK(t1 - t0 > 0.6f * v0 / a && t1 - t0 < 1.4f * v0 / a + 0.1f, "%.0f Hz: to rest in %.2f s (v0/a %.2f)",
              hzs[pass], t1 - t0, v0 / a);
    }
    /* the old ease's onset for the record: 12 m/s^2 at once (8 x 150) against the brake's 2.5 */
    float q[3] = { 0, 0, -r }, side[3] = { 7.0f, 0, 0 }, back[3] = { 0, 0, -7.0f };
    CHECK(!somvr_stl_standoff_brake(q, orc, r, 0.05f, a, side, out), "sideways along the edge: free");
    CHECK(!somvr_stl_standoff_brake(q, orc, r, 0.05f, a, back, out), "back: free");
    float in[3] = { 0, 0, -90 }, fw[3] = { 0, 0, 5.0f };
    CHECK(somvr_stl_standoff_brake(in, orc, r, 0.05f, a, fw, out) && fabsf(out[2] + 5.0f) < 1e-4f, "inside: no step in");
    CHECK(!somvr_stl_standoff_brake(in, orc, r, 0.05f, a, back, out) && out[2] == 0, "inside: back out free, no push");
    float diag[3] = { 5.0f, 0, 5.0f };
    CHECK(somvr_stl_standoff_brake(q, orc, r, 0.05f, a, diag, out) && fabsf(diag[0] + out[0] - 5.0f) < 1e-4f &&
          fabsf(diag[2] + out[2]) < 1e-4f, "diagonal at the edge: the inward half cut, the sideways half kept");
    CHECK(!somvr_stl_standoff_brake(q, orc, r, 0.05f, 0, fw, out), "decel 0: nothing (the old ease's)");
}

/* build 21 (docs 84): the kill is a thrust - the point leading, driven in at a real speed, 15 cm deep */
static void test_thrust(void) {
    somvr_stl_cfg c;
    somvr_stl_defaults(&c);
    somvr_stl_who w = who_behind();
    /* the user's build 20c session, 20261028-193826, t=974943.487 (the mark 14 right after): the tip at its back, point
     * first at 2.72 m/s (cos 0.97, the stab tier) - killed on the first contact (the session's mark 14) */
    somvr_stl_hit u = { 151.0f, 9.0f, SC_TIER_STAB, 2.72f, 0.97f, 2.0f, 0, SC_TIER_NONE, 0, 0 };
    CHECK(somvr_stl_judge(&c, &w, &u) == SOMVR_STL_KILL, "build 20c's rule: the touch killed (the session's)");
    c.thrust = 1;
    CHECK(somvr_stl_judge(&c, &w, &u) == SOMVR_STL_TOUCH, "build 21: a touch 2 cm in - no damage (%d)", somvr_stl_judge(&c, &w, &u));
    somvr_stl_hit k = u;
    k.depth_cm = 18.0f;
    CHECK(somvr_stl_judge(&c, &w, &k) == SOMVR_STL_KILL && somvr_stl_kill_by(&c, &k) == SOMVR_STL_BY_THRUST, "the thrust 18 cm in kills");
    k.depth_cm = 14.0f;
    CHECK(somvr_stl_judge(&c, &w, &k) == SOMVR_STL_TOUCH, "14 cm: not yet");
    k.depth_cm = 30.0f, k.axis_mps = 0.9f;
    CHECK(somvr_stl_judge(&c, &w, &k) == SOMVR_STL_TOUCH, "a slow push 30 cm in: no");
    k.axis_mps = 3.0f, k.cos_axis = 0.80f;
    CHECK(somvr_stl_judge(&c, &w, &k) == SOMVR_STL_TOUCH, "37 deg off the blade: not point-first");
    k.cos_axis = -0.97f;
    CHECK(somvr_stl_judge(&c, &w, &k) == SOMVR_STL_TOUCH, "drawn back out: no");
    k.cos_axis = 0.9f, k.tier = SC_TIER_HEAVY;
    CHECK(somvr_stl_judge(&c, &w, &k) == SOMVR_STL_KILL, "point-first at 3 m/s, 30 cm in");
    k.contact_off_deg = 40.0f;
    CHECK(somvr_stl_judge(&c, &w, &k) == SOMVR_STL_SIDE, "in its front: no");
    k.contact_off_deg = 75.0f;
    CHECK(somvr_stl_judge(&c, &w, &k) == SOMVR_STL_KILL, "its side from behind: yes");
    k.depth_cm = -1.0f;
    CHECK(somvr_stl_judge(&c, &w, &k) == SOMVR_STL_AIR, "the hit capsule only: nothing");
    k.depth_cm = NAN;
    CHECK(somvr_stl_judge(&c, &w, &k) == SOMVR_STL_AIR, "not measured: nothing");
    /* a full-speed cut to the neck from behind (the stroke's heavy tier), not to the back */
    somvr_stl_hit n = { 170.0f, 70.0f, SC_TIER_HEAVY, 0.5f, 0.1f, 25.0f, 1, SC_TIER_HEAVY, 0, 0 };
    CHECK(somvr_stl_judge(&c, &w, &n) == SOMVR_STL_KILL && somvr_stl_kill_by(&c, &n) == SOMVR_STL_BY_NECK, "the neck cut");
    n.tier_cut = SC_TIER_LIGHT;
    CHECK(somvr_stl_judge(&c, &w, &n) == SOMVR_STL_TOUCH, "a light cut to the neck: no");
    n.tier_cut = SC_TIER_STAB;
    CHECK(somvr_stl_judge(&c, &w, &n) == SOMVR_STL_TOUCH, "the stab tier is no cut");
    n.tier_cut = SC_TIER_HEAVY, n.neck = 0;
    CHECK(somvr_stl_judge(&c, &w, &n) == SOMVR_STL_TOUCH, "a full cut to the back: no");
    /* held from behind: a point-first stab 10 cm in */
    somvr_stl_hit g = { 180.0f, 40.0f, SC_TIER_NONE, 0.6f, 0.9f, 11.0f, 0, SC_TIER_NONE, 1, 0 };
    CHECK(somvr_stl_judge(&c, &w, &g) == SOMVR_STL_KILL && somvr_stl_kill_by(&c, &g) == SOMVR_STL_BY_GRAB, "held: the stab");
    g.depth_cm = 8.0f;
    CHECK(somvr_stl_judge(&c, &w, &g) == SOMVR_STL_TOUCH, "held: 8 cm, no");
    g.depth_cm = 20.0f, g.cos_axis = 0.5f;
    CHECK(somvr_stl_judge(&c, &w, &g) == SOMVR_STL_TOUCH, "held: sideways, no");
    /* who: unchanged - aware, in front, standing fast: the ordinary hit */
    somvr_stl_who a = w;
    a.aware = 1;
    CHECK(somvr_stl_judge(&c, &a, &k) == SOMVR_STL_AWARE, "aware: the ordinary hit");
    a = w, a.player_off_deg = 60.0f;
    CHECK(somvr_stl_judge(&c, &a, &k) == SOMVR_STL_FRONT, "in front: the ordinary hit");
    CHECK(SOMVR_STL_CARRIED + 1 == SOMVR_STL_N && strcmp(somvr_stl_why(SOMVR_STL_AIR), "?") &&
          strcmp(somvr_stl_why(SOMVR_STL_CARRIED), "?"), "the verdicts' names");
}

/* build 22 (SOMVR_STEALTH_KILL_CARRIED, docs 93.4): a blade held forward on the walk up reaches past the stand-off into
 * its back - the walk carried it in, the hand did not push: no touch. Only the hand's own motion makes one */
static void test_carried(void) {
    float node[4][3] = { { 0, 0, 0 }, { 0, 45, 0 }, { 0, 70, 5 }, { 0, 85, 8 } }, r[3] = { 17, 16, 9 };
    float vz[3] = { 0, 0, 0 }, vin[3] = { 0, 0, 200 }, vout[3] = { 0, 0, -200 };
    /* the blade level at 30 cm, its point 10 cm behind the spine's axis (7 cm in), Talion's walk having carried it there:
     * the hand's own velocity 0 - its own push 0, whatever the world moved */
    float b[3] = { 0, 30, -100 }, t[3] = { 0, 30, -10 };
    CHECK(fabsf(somvr_stl_own_push(b, t, vz, vz, 0.05f, node, r, 4)) < 1e-3f, "a still hand: no own push");
    float p = somvr_stl_own_push(b, t, vz, vin, 0.05f, node, r, 4);
    CHECK(fabsf(p - 7.0f) < 0.6f, "the point pushed 10 cm by the hand from outside to 7 cm in: 7 cm its own (%f)", p);
    float t2[3] = { 0, 30, 10 };
    p = somvr_stl_own_push(b, t2, vz, vin, 0.05f, node, r, 4);
    CHECK(fabsf(p - 10.0f) < 0.6f, "deeper in: the 10 cm the hand pushed (%f)", p);
    p = somvr_stl_own_push(b, t2, vz, vout, 0.05f, node, r, 4);
    CHECK(p < -9.0f, "drawn back by the hand: negative (%f)", p);
    CHECK(somvr_stl_own_push(b, t2, vz, vin, 0, node, r, 4) == 0, "no span: 0");
    somvr_stl_cfg c;
    somvr_stl_defaults(&c);
    c.thrust = 1;
    CHECK(!c.carried && fabsf(c.own_cm - 4.0f) < 1e-6f, "the code's default off (build 21's), 4 cm");
    somvr_stl_who w = who_behind();
    /* run 204131's stand-off: the point 22 cm in at its back, the hand still (the tracking-space speed ~0) */
    somvr_stl_hit k = { 170.0f, 30.0f, SC_TIER_NONE, 0.02f, 0.1f, 22.0f, 0, SC_TIER_NONE, 0, 1 };
    CHECK(somvr_stl_judge(&c, &w, &k) == SOMVR_STL_TOUCH, "build 21: the carried blade is a touch (the alert)");
    c.carried = 1;
    CHECK(somvr_stl_judge(&c, &w, &k) == SOMVR_STL_CARRIED, "build 22: carried - no touch (%d)", somvr_stl_judge(&c, &w, &k));
    somvr_stl_hit sd = k;
    sd.contact_off_deg = 40.0f;
    CHECK(somvr_stl_judge(&c, &w, &sd) == SOMVR_STL_CARRIED, "carried into its side: no touch either");
    sd.contact_off_deg = 170.0f, sd.contact_up_cm = 120.0f;
    CHECK(somvr_stl_judge(&c, &w, &sd) == SOMVR_STL_CARRIED, "carried in over its back: no touch");
    somvr_stl_hit m = k;
    m.carried = 0;
    CHECK(somvr_stl_judge(&c, &w, &m) == SOMVR_STL_TOUCH, "the hand pushed 4+ cm itself: a touch");
    m = k, m.axis_mps = 3.1f, m.cos_axis = 0.97f, m.depth_cm = 38.0f;
    CHECK(somvr_stl_judge(&c, &w, &m) == SOMVR_STL_KILL, "then the hand's thrust kills (its first frames still 'carried')");
    m.carried = 0;
    CHECK(somvr_stl_judge(&c, &w, &m) == SOMVR_STL_KILL && somvr_stl_kill_by(&c, &m) == SOMVR_STL_BY_THRUST, "the thrust");
    somvr_stl_hit g = { 180.0f, 40.0f, SC_TIER_NONE, 0.0f, 0.1f, 20.0f, 0, SC_TIER_NONE, 1, 1 };
    CHECK(somvr_stl_judge(&c, &w, &g) == SOMVR_STL_TOUCH, "held from behind: a held orc is never alerted (as build 21)");
    somvr_stl_who a = w;
    a.aware = 1;
    CHECK(somvr_stl_judge(&c, &a, &k) == SOMVR_STL_AWARE, "aware: the ordinary hit (the fight's rules)");
}

static void test_body(void) {
    /* an orc standing at the origin, its back toward -z: the pelvis, the torso, the neck, the head */
    float node[4][3] = { { 0, 0, 0 }, { 0, 45, 0 }, { 0, 70, 5 }, { 0, 85, 8 } }, r[3] = { 17, 16, 9 }, e[3];
    int link = -1;
    float b0[3] = { 0, 40, -150 }, t0[3] = { 0, 40, -60 };
    CHECK(somvr_stl_body_depth(b0, t0, node, r, 4, e, &link) < 0, "the point 60 cm behind its axis: not in");
    float t1[3] = { 0, 40, -10 };
    float d = somvr_stl_body_depth(b0, t1, node, r, 4, e, &link);
    CHECK(fabsf(d - 7.0f) < 0.6f && link == 0 && fabsf(e[2] + 17.0f) < 0.6f, "10 cm behind the axis: 7 cm in (%f, link %d, entry z %f)",
          d, link, e[2]);
    float t2[3] = { 0, 40, 30 };
    d = somvr_stl_body_depth(b0, t2, node, r, 4, e, &link);
    CHECK(fabsf(d - 47.0f) < 0.6f, "through it: from where it went in (%f)", d);
    float b3[3] = { 0, 88, -100 }, t3[3] = { 0, 88, 10 };
    d = somvr_stl_body_depth(b3, t3, node, r, 4, e, &link);
    CHECK(d > 0 && link == 2, "at neck height: the neck's link (%d, %f)", link, d);
    float b4[3] = { -100, 40, -10 }, t4[3] = { -5, 40, -10 };
    CHECK(somvr_stl_body_depth(b4, t4, node, r, 4, e, &link) >= 0, "from its side: in");
    float b5[3] = { 0, 140, -100 }, t5[3] = { 0, 140, 50 };
    CHECK(somvr_stl_body_depth(b5, t5, node, r, 4, e, &link) < 0, "over its head: air");
    CHECK(somvr_stl_body_depth(b0, t1, node, r, 1, e, &link) < 0, "no links: nothing");
}

static void test_stick(void) {
    somvr_stl_stick s;
    somvr_stl_stick_cfg c = { 25.0f, 12.0f };
    float E[3] = { 0, 100, 0 }, a[3] = { 0, 0, 2.0f }, G[3], Gs[3];
    somvr_stl_stick_start(&s, E, a, 90.0f);
    CHECK(s.on && fabsf(s.a[2] - 1.0f) < 1e-6f, "started, the axis unit");
    /* the grip 60 cm behind the wound on the axis: the point 30 cm in */
    G[0] = 0, G[1] = 100, G[2] = -60;
    CHECK(somvr_stl_stick_step(&s, &c, E, G, Gs) == SOMVR_STL_STK_HOLD && fabsf(s.depth - 30.0f) < 1e-3f && s.off < 1e-3f,
          "on the axis: held 30 cm in");
    /* sideways 15 cm: the blade stays, the hand 15 cm off it */
    G[0] = 15;
    CHECK(somvr_stl_stick_step(&s, &c, E, G, Gs) == SOMVR_STL_STK_HOLD && fabsf(Gs[0]) < 1e-3f && fabsf(s.off - 15.0f) < 1e-3f,
          "sideways: the blade pinned, the offset shown");
    /* pushed deeper than the guard allows: the blade stops at 78 cm, the hand's overshoot is the offset */
    G[0] = 0, G[2] = 0;
    CHECK(somvr_stl_stick_step(&s, &c, E, G, Gs) == SOMVR_STL_STK_HOLD && fabsf(s.depth - 78.0f) < 1e-3f &&
          fabsf(s.off - 12.0f) < 1e-3f && fabsf(s.peak - 78.0f) < 1e-3f, "the guard at the wound");
    /* the body slumps: the wound 20 cm lower, the hand still: held; 30 cm: it fell off the blade */
    float E2[3] = { 0, 80, 0 };
    G[2] = -50;
    CHECK(somvr_stl_stick_step(&s, &c, E2, G, Gs) == SOMVR_STL_STK_HOLD && fabsf(Gs[1] - 80.0f) < 1e-3f, "the wound moves: the blade with it");
    float E3[3] = { 0, 70, 0 };
    CHECK(somvr_stl_stick_step(&s, &c, E3, G, Gs) == SOMVR_STL_STK_TEAR && !s.on, "30 cm off: free");
    /* drawn back past the wound: out */
    somvr_stl_stick_start(&s, E, a, 90.0f);
    G[0] = 0, G[1] = 100, G[2] = -60;
    somvr_stl_stick_step(&s, &c, E, G, Gs);
    G[2] = -91;
    CHECK(somvr_stl_stick_step(&s, &c, E, G, Gs) == SOMVR_STL_STK_OUT && !s.on && fabsf(s.peak - 30.0f) < 1e-3f, "pulled out");
    CHECK(somvr_stl_stick_step(&s, &c, E, G, Gs) == SOMVR_STL_STK_OFF, "off stays off");
}

int main(void) {
    test_standoff();
    test_brake();
    test_angles();
    test_possible();
    test_judge();
    test_tier();
    test_cue();
    test_thrust();
    test_carried();
    test_body();
    test_stick();
    printf("test_stealth: %d checks, %d failures\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
