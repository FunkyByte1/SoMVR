/* test_threat.c - SOMVR_THREAT's pure part (somvr_threat.c): the ABI object (layout, entries by frame, a torn or
 * overwritten entry), a threat's direction in the head's frame (ahead, beside, behind, above; a turned head), in view
 * or not (the 110 deg cone), its side; the key times from Combat+0xc8.. (P8's infantry and archer tables, none), the lead;
 * the glow's level (0 at the start, the peak inside the parry window, held to the impact, out after it; an archer's
 * base before the window; no window; no impact) and the edge's; the cue selection (glow in view, the edge out of view
 * with one announcement - its pulse and sound, late when the timing is short - and one urgent pulse, none after the
 * impact, in -> out -> in -> out, the sound's minimum lead), the engaged orc's near cue, the parry tick, the projectile's
 * closest approach (at the head, the chest, a miss, passed, too far, no speed) and its pick; the world -> XR mapping
 * against the real rig (somvr_eyes.c); the glow's size; the layer's billboard and edge bands; the texture and its cells. */
#include "../somvr_eyes.h"
#include "../somvr_threat.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        checks++;                                                                                                      \
        if (!(c)) {                                                                                                    \
            fails++;                                                                                                   \
            printf("FAIL line %d: ", __LINE__);                                                                        \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)
#define NEAR(a, b, eps) (fabs((double)(a) - (double)(b)) <= (eps))
#define DEG 0.01745329252f

static float frand(float lo, float hi) { return lo + (hi - lo) * (float)rand() / (float)RAND_MAX; }
static const uint16_t KT_INF[10] = {0xffff, 266, 800, 0xffff, 0xffff, 0xffff, 0, 761, 0xffff, 100}; /* P8 melee */
static const uint16_t KT_ARCH[10] = {0xffff, 0xffff, 2633, 0xffff, 0xffff, 0xffff, 1566, 2566, 2600, 0xffff};
static const uint16_t KT_NONE[10] = {0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff};

/* ---- the ABI ---------------------------------------------------------------------------------------------------- */
static somvr_threat_shared g_obj;
static void test_abi(void) {
    CHECK(sizeof g_obj == 896 && sizeof(somvr_threat_frame) == 104 && offsetof(somvr_threat_shared, f) == 40,
          "layout %zu %zu", sizeof g_obj, sizeof(somvr_threat_frame));
    CHECK(!somvr_threat_ok(&g_obj) && !somvr_threat_ok(NULL), "a zero object is not ok");
    g_obj.magic = SOMVR_THREAT_MAGIC, g_obj.version = SOMVR_THREAT_VERSION, g_obj.size = sizeof g_obj;
    CHECK(!somvr_threat_ok(&g_obj), "not active yet");
    atomic_store(&g_obj.active, 1);
    CHECK(somvr_threat_ok(&g_obj), "active");
    g_obj.version = 2;
    CHECK(!somvr_threat_ok(&g_obj), "another version");
    g_obj.version = SOMVR_THREAT_VERSION;
    somvr_threat_frame in, out;
    memset(&in, 0, sizeof in);
    in.nglow = 2, in.flags = SOMVR_THF_WINDOW, in.edge[0] = 0.25f, in.edge[1] = 0.5f, in.threats = 3;
    in.hurt[0] = 200, in.hurt[1] = 90;
    in.g[0] = (somvr_th_glow){{1, 2, 3}, 0.3f, 0.9f, SOMVR_THK_MELEE};
    in.g[1] = (somvr_th_glow){{-1, 1.5f, -2}, 0.2f, 0.4f, SOMVR_THK_PROJ};
    somvr_threat_put(&g_obj, 77, &in);
    CHECK(somvr_threat_get(&g_obj, 77, &out) && out.nglow == 2 && out.flags == SOMVR_THF_WINDOW && out.edge[0] == 0.25f &&
              out.edge[1] == 0.5f && out.threats == 3 && out.g[0].pos[2] == 3 && out.g[1].kind == SOMVR_THK_PROJ &&
              out.hurt[0] == 200 && out.hurt[1] == 90 &&
              atomic_load(&out.seq) == 77 && atomic_load(&g_obj.last) == 77 && atomic_load(&g_obj.n_frames) == 1,
          "put/get");
    CHECK(!somvr_threat_get(&g_obj, 78, &out) && !somvr_threat_get(&g_obj, 0, &out) &&
              !somvr_threat_get(&g_obj, 77 + SOMVR_NREC, &out),
          "another frame's slot");
    somvr_threat_put(&g_obj, 77 + SOMVR_NREC, &in);
    CHECK(!somvr_threat_get(&g_obj, 77, &out) && somvr_threat_get(&g_obj, 77 + SOMVR_NREC, &out), "overwritten");
    atomic_store(&g_obj.f[(77 + SOMVR_NREC) % SOMVR_NREC].seq, 0); /* being written */
    CHECK(!somvr_threat_get(&g_obj, 77 + SOMVR_NREC, &out), "a torn entry");
    in.nglow = 9;
    somvr_threat_put(&g_obj, 5, &in);
    CHECK(somvr_threat_get(&g_obj, 5, &out) && out.nglow == SOMVR_TH_GLOWS, "nglow clamped: %u", out.nglow);
}

/* ---- direction, view, side ------------------------------------------------------------------------------------- */
static somvr_th_dir dir_of(const float head[3], const float q[4], float x, float y, float z) {
    float p[3] = {x, y, z}, l[3];
    somvr_th_local(head, q, p, l);
    return somvr_th_direction(l);
}
static void test_direction(void) {
    somvr_th_cfg c;
    somvr_th_cfg_default(&c);
    const float h[3] = {100, 170, -50}, id[4] = {0, 0, 0, 1};
    somvr_th_dir d = dir_of(h, id, 100, 170, 150);
    CHECK(NEAR(d.yaw_deg, 0, 1e-4) && NEAR(d.pitch_deg, 0, 1e-4) && NEAR(d.off_deg, 0, 1e-3) && NEAR(d.dist_cm, 200, 1e-3) &&
              somvr_th_in_view(&c, d),
          "ahead: yaw %g pitch %g off %g dist %g", d.yaw_deg, d.pitch_deg, d.off_deg, d.dist_cm);
    d = dir_of(h, id, 300, 170, -50);
    CHECK(NEAR(d.yaw_deg, 90, 1e-3) && NEAR(d.off_deg, 90, 1e-3) && !somvr_th_in_view(&c, d) && somvr_th_side(d) == SOMVR_TH_RIGHT,
          "right: yaw %g off %g side %d", d.yaw_deg, d.off_deg, somvr_th_side(d));
    d = dir_of(h, id, -100, 170, -50);
    CHECK(NEAR(d.yaw_deg, -90, 1e-3) && somvr_th_side(d) == SOMVR_TH_LEFT, "left: yaw %g", d.yaw_deg);
    d = dir_of(h, id, 100, 170, -250);
    CHECK(NEAR(fabsf(d.yaw_deg), 180, 1e-3) && NEAR(d.off_deg, 180, 1e-3) && somvr_th_side(d) == SOMVR_TH_BOTH,
          "behind: yaw %g off %g side %d", d.yaw_deg, d.off_deg, somvr_th_side(d));
    d = dir_of(h, id, 100, 370, -50);
    CHECK(NEAR(d.pitch_deg, 90, 1e-3) && NEAR(d.off_deg, 90, 1e-3) && !somvr_th_in_view(&c, d), "above: pitch %g off %g",
          d.pitch_deg, d.off_deg);
    /* the 110 deg cone, the full angle (SOMVR_THREAT_VIEW=full): 54 deg in, 56 out, in any direction */
    c.view_full = 1;
    for (int k = 0; k < 8; k++) {
        float a = (float)k * 45 * DEG;
        for (int s = 0; s < 2; s++) {
            float off = (s ? 56 : 54) * DEG, l[3] = {sinf(off) * cosf(a), sinf(off) * sinf(a), cosf(off)};
            somvr_th_dir e = somvr_th_direction(l);
            CHECK(somvr_th_in_view(&c, e) == !s, "cone at %d deg around, %s: off %g", k * 45, s ? "56" : "54", e.off_deg);
        }
    }
    /* the bearing (the default): |yaw| 54 in, 56 out at any pitch up to 85 deg; the pitch alone never puts it out of
     * view; more than 85 deg above/below: the full angle (no yaw there) */
    c.view_full = 0;
    for (int k = 0; k < 7; k++) {
        float pitch = (float)(k * 28 - 84) * DEG; /* -84 .. +84 */
        for (int s = 0; s < 4; s++) {
            float yaw = (s == 0 ? 54 : s == 1 ? -54 : s == 2 ? 56 : -56) * DEG;
            float l[3] = {cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw)};
            somvr_th_dir e = somvr_th_direction(l);
            CHECK(somvr_th_in_view(&c, e) == (s < 2) && NEAR(somvr_th_view_off(&c, e), fabsf(e.yaw_deg), 1e-4),
                  "bearing: yaw %g pitch %g: in %d", e.yaw_deg, e.pitch_deg, somvr_th_in_view(&c, e));
        }
    }
    float up[3] = {0.02f, 1, 0.01f}; /* 88.7 deg up, yaw 63: off 89 = out (the full angle) */
    d = somvr_th_direction(up);
    CHECK(!somvr_th_in_view(&c, d) && NEAR(somvr_th_view_off(&c, d), d.off_deg, 1e-4), "bearing, straight up: off %g",
          somvr_th_view_off(&c, d));
    float up2[3] = {0.0f, 1, 0.02f}; /* yaw 0, 88.9 up: out all the same */
    d = somvr_th_direction(up2);
    CHECK(!somvr_th_in_view(&c, d), "bearing, straight up ahead: off %g", somvr_th_view_off(&c, d));
    /* an orc at Talion's feet straight ahead (its root 40 cm ahead, the eyes 170 cm above it: 77 deg down; its hand
     * 100 cm up: 60 deg down): in view by the bearing, out by the full angle (round 1's LATE edge warnings); behind
     * him: out by both */
    const float eye[3] = {0, 170, 0};
    somvr_th_dir feet = dir_of(eye, id, 5, 0, 40), hand = dir_of(eye, id, 5, 100, 40), back = dir_of(eye, id, 5, 100, -60);
    CHECK(somvr_th_in_view(&c, feet) && somvr_th_in_view(&c, hand) && !somvr_th_in_view(&c, back) && feet.pitch_deg < -75 &&
              hand.pitch_deg < -55,
          "bearing: the feet %g/%g, the hand %g/%g in view, behind %g out", feet.yaw_deg, feet.pitch_deg, hand.yaw_deg,
          hand.pitch_deg, back.yaw_deg);
    c.view_full = 1;
    CHECK(!somvr_th_in_view(&c, feet) && !somvr_th_in_view(&c, hand) && !somvr_th_in_view(&c, back),
          "full: the feet, the hand and behind out of view");
    c.view_full = 0;
    /* the head turned 90 deg right (game: sv_qyaw(+a) turns +Z toward +X): the point at +X is ahead */
    float q[4] = {0, sinf(45 * DEG), 0, cosf(45 * DEG)};
    d = dir_of(h, q, 300, 170, -50);
    CHECK(NEAR(d.off_deg, 0, 1e-3) && somvr_th_in_view(&c, d), "turned head: off %g", d.off_deg);
    d = dir_of(h, q, 100, 170, 150); /* the world's +Z is now on the left */
    CHECK(NEAR(d.yaw_deg, -90, 1e-3) && somvr_th_side(d) == SOMVR_TH_LEFT, "turned head, left: yaw %g", d.yaw_deg);
    float qn[4] = {0, 2 * sinf(45 * DEG), 0, 2 * cosf(45 * DEG)}; /* not normalised */
    d = dir_of(h, qn, 300, 170, -50);
    CHECK(NEAR(d.off_deg, 0, 1e-3), "a scaled quaternion: off %g", d.off_deg);
    /* sides */
    static const float yaws[] = {30, -30, 171, -171, 173, -173, 0};
    static const int sides[] = {SOMVR_TH_RIGHT, SOMVR_TH_LEFT, SOMVR_TH_RIGHT, SOMVR_TH_LEFT, SOMVR_TH_BOTH, SOMVR_TH_BOTH,
                                SOMVR_TH_RIGHT};
    for (int i = 0; i < 7; i++) {
        somvr_th_dir e = {yaws[i], 0, fabsf(yaws[i]), 100};
        CHECK(somvr_th_side(e) == sides[i], "side of yaw %g: %d", yaws[i], somvr_th_side(e));
    }
    somvr_th_dir bad = {NAN, NAN, NAN, NAN};
    CHECK(!somvr_th_in_view(&c, bad) && somvr_th_side(bad) == 0, "NaN: not in view, no side");
    CHECK(!strcmp(somvr_th_side_name(SOMVR_TH_BOTH), "LR") && !strcmp(somvr_th_side_name(0), "-"), "side names");
}

/* ---- key times, levels ------------------------------------------------------------------------------------------ */
static void test_keys(void) {
    somvr_th_keys k = somvr_th_keys_of(KT_INF);
    CHECK(NEAR(k.impact, 0.8, 1e-6) && NEAR(k.bpw, 0, 1e-6) && NEAR(k.epw, 0.761, 1e-6) && NEAR(k.bcol, 0.266, 1e-6) &&
              !k.ranged,
          "infantry: impact %g bpw %g epw %g bcol %g ranged %d", k.impact, k.bpw, k.epw, k.bcol, k.ranged);
    CHECK(NEAR(somvr_th_lead(&k, 0.3f), 0.5, 1e-6) && somvr_th_window(&k, 0) && somvr_th_window(&k, 0.761f) &&
              !somvr_th_window(&k, 0.78f),
          "lead and window");
    k = somvr_th_keys_of(KT_ARCH);
    CHECK(NEAR(k.impact, 2.633, 1e-6) && NEAR(k.bpw, 1.566, 1e-6) && NEAR(k.epw, 2.566, 1e-6) && k.bcol < 0 && k.ranged &&
              !somvr_th_window(&k, 1.0f) && somvr_th_window(&k, 2.0f),
          "archer: impact %g bpw %g epw %g", k.impact, k.bpw, k.epw);
    k = somvr_th_keys_of(KT_NONE);
    CHECK(k.impact < 0 && k.bpw < 0 && k.epw < 0 && !k.ranged && isnan(somvr_th_lead(&k, 0)) && !somvr_th_window(&k, 0),
          "none");
    uint16_t bad[10];
    memcpy(bad, KT_INF, sizeof bad);
    bad[SOMVR_TH_KT_EPW] = 0; /* EPW before BPW: no window */
    k = somvr_th_keys_of(bad);
    CHECK(k.bpw < 0 && k.epw < 0 && k.impact > 0, "a backwards window is none");
}

static void test_levels(void) {
    somvr_th_cfg c;
    somvr_th_cfg_default(&c);
    somvr_th_keys k = somvr_th_keys_of(KT_INF);
    /* build 17: the red level: 0 before the start, full by 0.12 s (no wait for the window), a pulse 0.7..1 at 4 Hz to the
     * impact, out after it */
    {
        float lo = 2, hi = -1;
        for (int i = 15; i < 70; i++) {
            float l = somvr_th_red_level(&c, &k, 0.01f * (float)i, 4.0f);
            lo = l < lo ? l : lo, hi = l > hi ? l : hi;
        }
        CHECK(somvr_th_red_level(&c, &k, -0.1f, 4) == 0 && somvr_th_red_level(&c, &k, 0, 4) == 0 &&
                  somvr_th_red_level(&c, &k, 0.125f, 0) > 0.999f && lo > 0.69f && lo < 0.75f && hi > 0.95f &&
                  somvr_th_red_level(&c, &k, k.impact + c.fade_out_s + 0.01f, 4) == 0 &&
                  somvr_th_red_level(&c, &k, NAN, 4) == 0,
              "the red level: pulse %.2f..%.2f (0.7..1), out after the impact", lo, hi);
    }
    /* the infantry: 0 at the start, rising, the peak (1) inside the window, held to the impact, out 0.25 s after */
    float prev = -1, tpeak = -1;
    int mono = 1;
    for (int i = 0; i <= 80; i++) {
        float t = 0.01f * (float)i, l = somvr_th_glow_level(&c, &k, t);
        mono &= l >= prev - 1e-6f;
        if (tpeak < 0 && l > 0.999f) tpeak = t;
        prev = l;
    }
    CHECK(somvr_th_glow_level(&c, &k, 0) == 0 && mono && tpeak > 0 && tpeak <= k.epw && NEAR(prev, 1, 1e-6),
          "infantry glow: start %g monotonic %d peak at %g (window to %g) at the impact %g",
          somvr_th_glow_level(&c, &k, 0), mono, tpeak, k.epw, prev);
    CHECK(somvr_th_glow_level(&c, &k, 0.05f) > 0 && somvr_th_glow_level(&c, &k, 0.05f) < c.base,
          "the fade-in: %g", somvr_th_glow_level(&c, &k, 0.05f));
    CHECK(NEAR(somvr_th_glow_level(&c, &k, 0.925f), 0.5, 1e-4) && somvr_th_glow_level(&c, &k, 1.06f) == 0 &&
              !somvr_th_expired(&c, &k, 1.04f) && somvr_th_expired(&c, &k, 1.06f),
          "after the impact: %g %g", somvr_th_glow_level(&c, &k, 0.925f), somvr_th_glow_level(&c, &k, 1.06f));
    /* the archer: the base before the window, the peak in it */
    k = somvr_th_keys_of(KT_ARCH);
    CHECK(NEAR(somvr_th_glow_level(&c, &k, 1.0f), c.base, 1e-6) && NEAR(somvr_th_glow_level(&c, &k, 1.566f), c.base, 1e-6) &&
              somvr_th_glow_level(&c, &k, 2.0f) > c.base && NEAR(somvr_th_glow_level(&c, &k, 2.4f), 1, 1e-6),
          "archer glow: %g %g %g", somvr_th_glow_level(&c, &k, 1.0f), somvr_th_glow_level(&c, &k, 2.0f),
          somvr_th_glow_level(&c, &k, 2.4f));
    /* no window: from the base to 1 at the impact; no impact: the base */
    uint16_t nw[10];
    memcpy(nw, KT_INF, sizeof nw);
    nw[SOMVR_TH_KT_BPW] = nw[SOMVR_TH_KT_EPW] = 0xffff;
    k = somvr_th_keys_of(nw);
    CHECK(NEAR(somvr_th_glow_level(&c, &k, 0.4f), c.base + (1 - c.base) * 0.5f, 1e-4) &&
              NEAR(somvr_th_glow_level(&c, &k, 0.8f), 1, 1e-6),
          "no window: %g %g", somvr_th_glow_level(&c, &k, 0.4f), somvr_th_glow_level(&c, &k, 0.8f));
    k = somvr_th_keys_of(KT_NONE);
    CHECK(NEAR(somvr_th_glow_level(&c, &k, 2.0f), c.base, 1e-6) && !somvr_th_expired(&c, &k, 3.9f) &&
              somvr_th_expired(&c, &k, 4.1f) && somvr_th_glow_level(&c, &k, -1) == 0 && somvr_th_glow_level(&c, &k, NAN) == 0,
          "no impact: %g", somvr_th_glow_level(&c, &k, 2.0f));
    /* the edge: its base early, rising to 1 at the impact, out after */
    k = somvr_th_keys_of(KT_ARCH);
    prev = -1, mono = 1;
    for (int i = 15; i <= 263; i++) {
        float l = somvr_th_edge_level(&c, &k, 0.01f * (float)i);
        mono &= l >= prev - 1e-6f, prev = l;
    }
    CHECK(NEAR(somvr_th_edge_level(&c, &k, 0.5f), c.edge_base, 1e-6) && mono && NEAR(prev, 1, 1e-4) &&
              somvr_th_edge_level(&c, &k, 2.633f + 0.26f) == 0 && somvr_th_edge_level(&c, &k, 0) == 0,
          "edge: %g ... %g monotonic %d", somvr_th_edge_level(&c, &k, 0.5f), prev, mono);
}

/* ---- the cue selection ------------------------------------------------------------------------------------------ */
static void test_select(void) {
    somvr_th_cfg c;
    somvr_th_cfg_default(&c);
    somvr_th_keys k = somvr_th_keys_of(KT_INF);
    somvr_th_dir ahead = {10, 0, 10, 150}, behind = {-150, 0, 150, 150}, right = {80, 0, 80, 150};
    somvr_th_state s;
    memset(&s, 0, sizeof s);
    /* in view: the glow, no pulse, no sound */
    somvr_th_pick p = somvr_th_select(&c, ahead, &k, 0.4f, &s);
    CHECK(p.cue == SOMVR_THC_GLOW && p.haptic == SOMVR_THH_NONE && !p.sound && !s.announced &&
              NEAR(p.level, c.glow * somvr_th_glow_level(&c, &k, 0.4f), 1e-6) && NEAR(p.lead, 0.4, 1e-6),
          "in view: cue %d haptic %d level %g", p.cue, p.haptic, p.level);
    /* out of view from the start: the announcement once (pulse + sound; late: 0.8 s of lead), then the urgent pulse */
    memset(&s, 0, sizeof s);
    int ann = 0, urg = 0, snd = 0, late = 0, after = 0;
    for (int i = 0; i <= 110; i++) {
        float t = 0.01f * (float)i;
        p = somvr_th_select(&c, behind, &k, t, &s);
        ann += p.haptic == SOMVR_THH_ANNOUNCE, urg += p.haptic == SOMVR_THH_URGENT, snd += p.sound, late += p.late;
        if (t > k.impact && p.haptic) after++;
        if (i == 0)
            CHECK(p.cue == SOMVR_THC_EDGE && p.side == SOMVR_TH_LEFT && p.haptic == SOMVR_THH_ANNOUNCE && p.sound && p.late,
                  "behind at the start: cue %d side %d haptic %d sound %d late %d", p.cue, p.side, p.haptic, p.sound, p.late);
        if (p.haptic == SOMVR_THH_URGENT) CHECK(NEAR(p.lead, 0.35, 0.011), "the urgent pulse at lead %g", p.lead);
        if (i == 106) CHECK(p.cue == SOMVR_THC_NONE, "expired: cue %d", p.cue);
    }
    CHECK(ann == 1 && urg == 1 && snd == 1 && late == 1 && !after, "pulses: announce %d urgent %d sound %d late %d after %d",
          ann, urg, snd, late, after);
    /* in view, then out of view (announced when it leaves), back in (glow), out again (no second announcement) */
    k = somvr_th_keys_of(KT_ARCH);
    memset(&s, 0, sizeof s);
    p = somvr_th_select(&c, ahead, &k, 0.5f, &s);
    CHECK(p.cue == SOMVR_THC_GLOW && !p.haptic, "archer in view");
    p = somvr_th_select(&c, right, &k, 1.0f, &s);
    CHECK(p.cue == SOMVR_THC_EDGE && p.side == SOMVR_TH_RIGHT && p.haptic == SOMVR_THH_ANNOUNCE && p.sound && !p.late &&
              NEAR(p.level, c.edge * c.edge_base, 1e-6),
          "archer leaves the view: cue %d side %d haptic %d sound %d late %d level %g", p.cue, p.side, p.haptic, p.sound,
          p.late, p.level);
    p = somvr_th_select(&c, ahead, &k, 1.5f, &s);
    CHECK(p.cue == SOMVR_THC_GLOW && !p.haptic, "archer back in view");
    p = somvr_th_select(&c, right, &k, 2.0f, &s);
    CHECK(p.cue == SOMVR_THC_EDGE && !p.haptic && !p.sound, "archer out again: haptic %d", p.haptic);
    p = somvr_th_select(&c, right, &k, 2.4f, &s);
    CHECK(p.haptic == SOMVR_THH_URGENT, "archer urgent: %d", p.haptic);
    /* the first frame out of view inside the urgent time: the announcement only; with too little lead: no sound */
    memset(&s, 0, sizeof s);
    k = somvr_th_keys_of(KT_INF);
    p = somvr_th_select(&c, behind, &k, 0.6f, &s);
    somvr_th_pick p2 = somvr_th_select(&c, behind, &k, 0.7f, &s);
    CHECK(p.haptic == SOMVR_THH_ANNOUNCE && !p.sound && p.late && p2.haptic == SOMVR_THH_NONE,
          "late out of view: haptic %d sound %d then %d", p.haptic, p.sound, p2.haptic);
    /* out of view only after the impact: nothing announced */
    memset(&s, 0, sizeof s);
    p = somvr_th_select(&c, behind, &k, 0.85f, &s);
    CHECK(p.cue == SOMVR_THC_EDGE && !p.haptic && !s.announced && p.level > 0, "after the impact: haptic %d", p.haptic);
    /* strength 0: no light, the pulse still */
    c.edge = 0;
    memset(&s, 0, sizeof s);
    p = somvr_th_select(&c, behind, &k, 0.1f, &s);
    CHECK(p.level == 0 && p.haptic == SOMVR_THH_ANNOUNCE, "edge strength 0: level %g haptic %d", p.level, p.haptic);
    /* a keyless attack (all ten keys none: the orcs' moves between blows, run 092140): no cue, no pulse, no sound - in
     * view or behind; SOMVR_THREAT_KEYLESS=1: round 1's (the base glow; behind the edge, the pulse, the sound) */
    c.edge = 0.8f;
    k = somvr_th_keys_of(KT_NONE);
    somvr_th_keys kb = somvr_th_keys_of(KT_INF), ka = somvr_th_keys_of(KT_ARCH);
    uint16_t bc[10];
    memset(bc, 0xff, sizeof bc);
    bc[SOMVR_TH_KT_BCOL] = 300;
    somvr_th_keys kc = somvr_th_keys_of(bc);
    CHECK(somvr_th_keyless(&k) && !somvr_th_keyless(&kb) && !somvr_th_keyless(&ka) && !somvr_th_keyless(&kc),
          "keyless: none %d, infantry %d, archer %d, BCOL only %d", somvr_th_keyless(&k), somvr_th_keyless(&kb),
          somvr_th_keyless(&ka), somvr_th_keyless(&kc));
    int quiet = 1;
    memset(&s, 0, sizeof s);
    for (int i = 0; i < 30; i++) {
        float t = i * 0.05f;
        somvr_th_pick pb = somvr_th_select(&c, behind, &k, t, &s), pa = somvr_th_select(&c, ahead, &k, t, &s);
        quiet &= pb.cue == SOMVR_THC_NONE && !pb.haptic && !pb.sound && pa.cue == SOMVR_THC_NONE && pa.level == 0;
    }
    CHECK(quiet && !s.announced, "keyless: no cue behind or ahead, no pulse, no sound");
    c.keyless = 1;
    memset(&s, 0, sizeof s);
    p = somvr_th_select(&c, behind, &k, 0.1f, &s);
    CHECK(p.cue == SOMVR_THC_EDGE && p.haptic == SOMVR_THH_ANNOUNCE && p.sound, "keyless=1: behind announced (%d %d)",
          p.cue, p.haptic);
    c.keyless = 0;
    CHECK(!strcmp(somvr_th_cue_name(SOMVR_THC_PROJ_EDGE), "proj-edge") && !strcmp(somvr_th_haptic_name(SOMVR_THH_URGENT), "urgent"),
          "names");
}

static void test_near_parry(void) {
    somvr_th_cfg c;
    somvr_th_cfg_default(&c);
    somvr_th_dir d = {-120, 0, 120, 100};
    somvr_th_pick p = somvr_th_near(&c, d);
    CHECK(p.cue == SOMVR_THC_NEAR && p.side == SOMVR_TH_LEFT && NEAR(p.level, c.edge * c.near_level, 1e-6) && !p.haptic,
          "near at 1 m: cue %d side %d level %g", p.cue, p.side, p.level);
    d.dist_cm = 175;
    p = somvr_th_near(&c, d);
    CHECK(p.cue == SOMVR_THC_NEAR && NEAR(p.level, 0.5 * c.edge * c.near_level, 1e-4), "near at 1.75 m: %g", p.level);
    d.dist_cm = 260;
    CHECK(somvr_th_near(&c, d).cue == SOMVR_THC_NONE, "near beyond 2.5 m");
    d.dist_cm = 100, d.yaw_deg = -30, d.off_deg = 30;
    CHECK(somvr_th_near(&c, d).cue == SOMVR_THC_NONE, "near in view");
    c.near_m = 0, d.yaw_deg = -120, d.off_deg = 120;
    CHECK(somvr_th_near(&c, d).cue == SOMVR_THC_NONE, "near off");
    /* the parry tick: once, when the window opens */
    somvr_th_keys k = somvr_th_keys_of(KT_ARCH);
    somvr_th_state s;
    memset(&s, 0, sizeof s);
    int n = 0;
    float at = -1;
    for (int i = 0; i < 300; i++) {
        float t = 0.01f * (float)i;
        if (somvr_th_parry_tick(&k, t, &s)) n++, at = t;
    }
    CHECK(n == 1 && NEAR(at, 1.57, 1e-4), "archer tick: %d at %g", n, at);
    k = somvr_th_keys_of(KT_INF);
    memset(&s, 0, sizeof s);
    CHECK(somvr_th_parry_tick(&k, 0, &s) && !somvr_th_parry_tick(&k, 0.1f, &s), "infantry tick at the start (BPW 0)");
    k = somvr_th_keys_of(KT_NONE);
    memset(&s, 0, sizeof s);
    CHECK(!somvr_th_parry_tick(&k, 0.5f, &s), "no window, no tick");
}

static void test_proj(void) {
    somvr_th_cfg c;
    somvr_th_cfg_default(&c);
    const float head[3] = {0, 170, 0}, chest[3] = {0, 130, 0};
    float pos[3] = {0, 170, 2000}, dir[3] = {0, 0, -1};
    somvr_th_proj r = somvr_th_proj_eval(&c, pos, dir, 2800, head, chest);
    CHECK(r.inbound && r.at_head && NEAR(r.tca_s, 2000.0 / 2800, 1e-5) && NEAR(r.miss_cm, 0, 1e-3),
          "at the head from 20 m: inbound %d tca %g miss %g", r.inbound, r.tca_s, r.miss_cm);
    pos[1] = 120; /* at the stomach: the chest is nearer */
    r = somvr_th_proj_eval(&c, pos, dir, 2800, head, chest);
    CHECK(r.inbound && !r.at_head && NEAR(r.miss_cm, 10, 1e-3), "at the chest: at_head %d miss %g", r.at_head, r.miss_cm);
    pos[0] = 100, pos[1] = 170; /* 1 m to the side */
    r = somvr_th_proj_eval(&c, pos, dir, 2800, head, chest);
    CHECK(!r.inbound && NEAR(r.miss_cm, 100, 1e-3), "a miss: %g", r.miss_cm);
    pos[0] = 0, pos[2] = -200; /* already past */
    r = somvr_th_proj_eval(&c, pos, dir, 2800, head, chest);
    CHECK(!r.inbound && isnan(r.tca_s), "passed: inbound %d", r.inbound);
    pos[2] = 6000; /* 60 m: more than 1.5 s */
    r = somvr_th_proj_eval(&c, pos, dir, 2800, head, chest);
    CHECK(!r.inbound && r.tca_s > c.proj_t_s, "too far: tca %g", r.tca_s);
    pos[2] = 2000;
    r = somvr_th_proj_eval(&c, pos, dir, 0, head, chest);
    float z[3] = {0, 0, 0};
    somvr_th_proj r2 = somvr_th_proj_eval(&c, pos, z, 2800, head, chest);
    CHECK(!r.inbound && !r2.inbound, "no speed, no direction");
    float d2[3] = {0, 0, -2}; /* a direction not of unit length */
    r = somvr_th_proj_eval(&c, pos, d2, 2800, head, chest);
    CHECK(r.inbound && NEAR(r.tca_s, 2000.0 / 2800, 1e-5), "scaled direction: tca %g", r.tca_s);
    /* its pick: in view the glow, out of view the edge; one pulse */
    int ann = 0;
    somvr_th_dir in = {5, 0, 5, 2000}, out = {120, 0, 120, 2000};
    somvr_th_pick p = somvr_th_proj_pick(&c, in, &r, &ann);
    somvr_th_pick q = somvr_th_proj_pick(&c, out, &r, &ann);
    CHECK(p.cue == SOMVR_THC_PROJ_GLOW && p.haptic == SOMVR_THH_ANNOUNCE && p.side == SOMVR_TH_RIGHT && ann == 1 &&
              q.cue == SOMVR_THC_PROJ_EDGE && q.side == SOMVR_TH_RIGHT && !q.haptic && q.level > 0.6f * c.edge &&
              q.level < c.edge,
          "proj pick: %d/%d side %d, %d/%d level %g", p.cue, p.haptic, p.side, q.cue, q.haptic, q.level);
    r.inbound = 0;
    CHECK(somvr_th_proj_pick(&c, out, &r, &ann).cue == SOMVR_THC_NONE, "not inbound: no cue");
}

/* ---- the mapping, the quads, the texture ------------------------------------------------------------------------- */
static void test_map(void) {
    /* a point at XR p through the real rig (somvr_rig_eye maps an eye pose like any tracked point) back to p */
    srand(7);
    float worst = 0;
    for (int i = 0; i < 200; i++) {
        somvr_rig r;
        somvr_rig_init(&r);
        r.have_ref = 1, r.p0 = (sv_vec3){frand(-50, 50), frand(100, 180), frand(-50, 50)}, r.yaw0 = frand(-3, 3);
        somvr_rig_cfg cfg = {.layout = SOMVR_LAYOUT_B, .base = SOMVR_BASE_HEAD};
        somvr_base b = {{frand(-5e4f, 5e4f), frand(-1e3f, 1e3f), frand(-5e4f, 5e4f)}, frand(-3.1f, 3.1f)};
        somvr_view game = {{0, 0, 0}, {0, 0, 0, 1}, 1.5f, 1.2f, 7, 1e5f};
        somvr_posef xr = {{0, 0, 0, 1}, {frand(-2, 2), frand(0, 2), frand(-2, 2)}};
        somvr_fovf fov = {-0.8f, 0.8f, 0.8f, -0.8f};
        somvr_eye_out o;
        somvr_rig_eye(&r, &cfg, b, &game, &xr, &fov, 0, &o);
        somvr_th_map m = {{b.pos.x, b.pos.y, b.pos.z}, b.yaw - r.yaw0, {r.p0.x, r.p0.y, r.p0.z}, 0};
        float back[3];
        somvr_th_to_xr(&m, o.view.pos, back);
        for (int j = 0; j < 3; j++) worst = fmaxf(worst, fabsf(back[j] - xr.p[j]));
    }
    CHECK(worst < 5e-3f, "world -> XR against the rig: worst %g m", worst); /* float precision at 500 m from the origin */
    somvr_th_cfg c;
    somvr_th_cfg_default(&c);
    CHECK(NEAR(somvr_th_glow_size(&c, 200), 2 * 2 * tan(4.5 * DEG), 1e-5) && somvr_th_glow_size(&c, 10) == c.glow_min_m &&
              somvr_th_glow_size(&c, 1e5f) == c.glow_max_m && somvr_th_glow_size(&c, NAN) == c.glow_min_m,
          "glow size %g", somvr_th_glow_size(&c, 200));
}

static void qrot_xr(const float q[4], const float v[3], float o[3]) {
    float tx = 2 * (q[1] * v[2] - q[2] * v[1]), ty = 2 * (q[2] * v[0] - q[0] * v[2]), tz = 2 * (q[0] * v[1] - q[1] * v[0]);
    o[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
    o[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
    o[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}
static void test_quads(void) {
    srand(11);
    float worst = 0, roll = 0, norm = 0;
    for (int i = 0; i < 300; i++) {
        float pos[3] = {frand(-5, 5), frand(-1, 3), frand(-5, 5)}, eye[3] = {frand(-1, 1), frand(1, 2), frand(-1, 1)}, q[4];
        somvr_th_face(pos, eye, q);
        float z[3] = {0, 0, 1}, x[3] = {1, 0, 0}, fz[3], fx[3], d[3] = {eye[0] - pos[0], eye[1] - pos[1], eye[2] - pos[2]};
        float n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        qrot_xr(q, z, fz), qrot_xr(q, x, fx);
        for (int j = 0; j < 3; j++) worst = fmaxf(worst, fabsf(fz[j] - d[j] / n));
        roll = fmaxf(roll, fabsf(fx[1]));
        norm = fmaxf(norm, fabsf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3] - 1));
    }
    CHECK(worst < 1e-4f && roll < 1e-5f && norm < 1e-5f, "billboard: +Z to the eye %g, roll %g, norm %g", worst, roll, norm);
    float q[4], p0[3] = {1, 1, 1};
    somvr_th_face(p0, p0, q);
    CHECK(q[3] == 1 && q[0] == 0, "billboard at the eye: identity");
    /* the edge bands on the mock's views (eye 0: -0.90..0.70, up 0.80, down -0.95; eye 1 mirrored) */
    const float fov[2][4] = {{-0.90f, 0.70f, 0.80f, -0.95f}, {-0.70f, 0.90f, 0.80f, -0.95f}};
    float pl[7], sl[2], pr[7], sr[2];
    somvr_th_edge_quad(fov, SOMVR_TH_LEFT, 1.0f, pl, sl);
    somvr_th_edge_quad(fov, SOMVR_TH_RIGHT, 1.0f, pr, sr);
    float t = tanf(0.9f), inner = t * (1 - SOMVR_TH_EDGE_IN), outer = t * (1 + SOMVR_TH_EDGE_OUT);
    CHECK(pl[3] == 1 && pl[2] == 0 && NEAR(pl[4], -0.5 * (inner + outer), 1e-5) && NEAR(pl[6], -1, 1e-6) &&
              NEAR(sl[0], outer - inner, 1e-5) && NEAR(pl[5], 0.5 * (tan(0.8) - tan(0.95)), 1e-5) &&
              NEAR(sl[1], 1.1 * (tan(0.8) + tan(0.95)), 1e-5),
          "left band: q %g %g x %g y %g z %g size %g x %g", pl[2], pl[3], pl[4], pl[5], pl[6], sl[0], sl[1]);
    CHECK(pr[2] == 1 && pr[3] == 0 && NEAR(pr[4], -pl[4], 1e-5) && NEAR(sr[0], sl[0], 1e-5) && NEAR(sr[1], sl[1], 1e-5),
          "right band: the left one mirrored: q %g %g x %g", pr[2], pr[3], pr[4]);
    somvr_th_edge_quad(fov, SOMVR_TH_LEFT, 2.0f, pl, sl);
    CHECK(NEAR(pl[6], -2, 1e-6) && NEAR(sl[0], 2 * (outer - inner), 1e-5), "the band scales with its distance");
}

static void test_image(void) {
    static uint8_t px[SOMVR_TH_IMG_W * SOMVR_TH_IMG_H * 4], bg[SOMVR_TH_IMG_W * SOMVR_TH_IMG_H * 4];
    somvr_th_image(px, 0);
    somvr_th_image(bg, 1);
#define PX(b, x, y) (&(b)[4 * ((size_t)(y) * SOMVR_TH_IMG_W + (size_t)(x))])
    int32_t rc[4], rc2[4];
    somvr_th_rect(SOMVR_TH_ROW_GLOW, SOMVR_TH_LEVELS, rc);
    const uint8_t *c = PX(px, rc[0] + 16, rc[1] + 16), *cb = PX(bg, rc[0] + 16, rc[1] + 16);
    CHECK(c[0] > 240 && c[1] > 125 && c[1] < 145 && c[2] > 30 && c[2] < 42 && c[3] > 80 && c[3] < 92 && cb[0] == c[2] &&
              cb[2] == c[0] && cb[1] == c[1] && cb[3] == c[3],
          "the glow's centre: %u %u %u %u (bgra %u %u %u %u)", c[0], c[1], c[2], c[3], cb[0], cb[1], cb[2], cb[3]);
    CHECK(PX(px, rc[0], rc[1])[3] == 0 && PX(px, rc[0] + 31, rc[1] + 31)[0] == 0 && PX(px, rc[0] - 1, rc[1] + 16)[0] == 0 &&
              PX(px, rc[0] + 16, rc[1] - 1)[3] == 0,
          "the glow's corners and guard are clear");
    int mono = 1, prem = 1;
    for (int k = 2; k <= SOMVR_TH_LEVELS; k++) {
        somvr_th_rect(SOMVR_TH_ROW_GLOW, k - 1, rc2);
        somvr_th_rect(SOMVR_TH_ROW_GLOW, k, rc);
        mono &= PX(px, rc[0] + 16, rc[1] + 16)[0] > PX(px, rc2[0] + 16, rc2[1] + 16)[0];
    }
    for (size_t i = 0; i < (size_t)SOMVR_TH_IMG_W * SOMVR_TH_IMG_H; i++) prem &= px[4 * i + 3] <= px[4 * i];
    CHECK(mono && prem, "levels rise %d, alpha below the colour (mostly additive) %d", mono, prem);
    somvr_th_rect(SOMVR_TH_ROW_EDGE, SOMVR_TH_LEVELS, rc);
    CHECK(PX(px, rc[0], rc[1] + 16)[0] > 200 && PX(px, rc[0] + 31, rc[1] + 16)[0] == 0 && PX(px, rc[0], rc[1])[0] < 40 &&
              PX(px, rc[0] + 8, rc[1] + 16)[0] < PX(px, rc[0], rc[1] + 16)[0],
          "the edge cell: bright outer column %u, inner %u, top %u", PX(px, rc[0], rc[1] + 16)[0],
          PX(px, rc[0] + 31, rc[1] + 16)[0], PX(px, rc[0], rc[1])[0]);
    /* build 10: the hurt row: the edge band's shape in red, a stronger alpha */
    somvr_th_rect(SOMVR_TH_ROW_HURT, SOMVR_TH_LEVELS, rc);
    const uint8_t *h = PX(px, rc[0], rc[1] + 16), *hb = PX(bg, rc[0], rc[1] + 16);
    CHECK(rc[1] == 2 * SOMVR_TH_STRIDE + 1 && h[0] > 240 && h[1] < 16 && h[2] < 10 && h[3] > 180 && hb[2] == h[0] &&
              PX(px, rc[0] + 31, rc[1] + 16)[3] == 0 && PX(px, rc[0] + 8, rc[1] + 16)[0] < h[0],
          "the hurt cell: red outer column %u %u %u a %u", h[0], h[1], h[2], h[3]);
    /* build 17: the red row: the radial glow's shape in an alarm red, alpha 0.7; the kinds' rows */
    somvr_th_rect(SOMVR_TH_ROW_RED, SOMVR_TH_LEVELS, rc);
    const uint8_t *rd = PX(px, rc[0] + 16, rc[1] + 16), *rg = PX(px, rc[0], rc[1]);
    CHECK(rc[1] == 3 * SOMVR_TH_STRIDE + 1 && rd[0] > 230 && rd[1] < 20 && rd[2] < 12 && rd[3] > 150 && rg[3] == 0 &&
              rd[3] > PX(px, rc[0] + 16, rc[1] + 16 - 3 * SOMVR_TH_STRIDE)[3], /* stronger than the warm glow's */
          "the red cell: centre %u %u %u a %u, corner a %u", rd[0], rd[1], rd[2], rd[3], rg[3]);
    CHECK(somvr_th_glow_row(SOMVR_THK_MELEE) == SOMVR_TH_ROW_GLOW && somvr_th_glow_row(SOMVR_THK_MELEE | SOMVR_THK_RED) ==
              SOMVR_TH_ROW_RED && somvr_th_glow_row(SOMVR_THK_PROJ) == SOMVR_TH_ROW_GLOW, "the glow rows by kind");
    int hb_hc = 0;
    CHECK(somvr_th_hurt_side(0, 0.5f, &hb_hc) == 0 && hb_hc == 8, "no hurt: the band");
    CHECK(somvr_th_hurt_side(255, 0.5f, &hb_hc) == 1 && hb_hc == 16, "a full hurt over a band: red");
    CHECK(somvr_th_hurt_side(64, 0.9f, &hb_hc) == 0 && hb_hc == 14, "a fading hurt under a strong band: the band");
    CHECK(somvr_th_hurt_side(128, 0, &hb_hc) == 1 && hb_hc == 8, "a hurt alone: red");
    CHECK(somvr_th_hurt_side(2, 0, &hb_hc) == 0 && hb_hc == 0, "a tiny hurt: nothing");
    /* cells, rects */
    CHECK(somvr_th_cell(0) == 0 && somvr_th_cell(NAN) == 0 && somvr_th_cell(-1) == 0 && somvr_th_cell(0.02f) == 0 &&
              somvr_th_cell(0.04f) == 1 && somvr_th_cell(0.5f) == 8 && somvr_th_cell(1) == 16 && somvr_th_cell(3) == 16,
          "cell picks");
    int inside = 1, apart = 1;
    for (int row = 0; row < 4; row++)
        for (int k = 1; k <= SOMVR_TH_LEVELS; k++) {
            somvr_th_rect(row, k, rc);
            inside &= rc[0] >= 1 && rc[1] >= 1 && rc[0] + rc[2] <= SOMVR_TH_IMG_W - 1 && rc[1] + rc[3] <= SOMVR_TH_IMG_H - 1 &&
                      rc[2] == SOMVR_TH_CELL && rc[3] == SOMVR_TH_CELL;
            if (k > 1) {
                somvr_th_rect(row, k - 1, rc2);
                apart &= rc[0] - (rc2[0] + rc2[2]) == 2;
            }
        }
    CHECK(inside && apart, "rects inside %d, 2 px apart %d", inside, apart);
    CHECK(somvr_th_glow_profile(0) == 1 && somvr_th_glow_profile(1) == 0 && somvr_th_glow_profile(NAN) == 0 &&
              somvr_th_edge_profile(0, 0.5f) == 1 && somvr_th_edge_profile(1, 0.5f) == 0,
          "profiles");
#undef PX
}

int main(void) {
    test_abi();
    test_direction();
    test_keys();
    test_levels();
    test_select();
    test_near_parry();
    test_proj();
    test_map();
    test_quads();
    test_image();
    printf("test_threat: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
