/* test_defence.c - SOMVR_DEFENCE's pure judge (somvr_defence.c): the attack types' rules; the guard (the blow's line
 * from the weapon hand to Talion's body, the blade's distance and crossing angle, a blade along the line, beside it,
 * behind the body; the swing plane: a side swing wants a vertical blade, an overhead one a level blade, a thrust any;
 * the body target's clamping; bad inputs; 300 random cases against a brute-force distance); the parry zone's edges
 * (P8's infantry and archer windows, a tiny window, none, a backwards one); the step over a whole attack at 90 Hz (the
 * parry fires once at the zone's first frame; the game's window byte shut / unknown; each type; parry off; no blade; a
 * late blade = no parry but a block); the verdict at the impact (the grace edge, a parried attack that still lands,
 * block before dodge, the types); the dodge (sideways 29/31 cm, back 39/41 cm, toward the orc, off); two attackers (one
 * blocked, the other not; both parried in one frame = one press); the press (ticks, the cooldown, bad commands); arrows
 * (on the blade, beside it, a step short, going away, passing far, the blade swept across between frames, NaN, off);
 * the haptics per outcome. */
#include "../somvr_combat.h"
#include "../somvr_defence.h"

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

static float frand(float lo, float hi) { return lo + (hi - lo) * (float)rand() / (float)RAND_MAX; }

/* the scene (cm, Y up): Talion's head at the origin's 170 cm, the orc's root 1.5 m ahead (+Z), its weapon hand raised
 * 1 m ahead of the head; the blow's target is the body's top (155), the line rises 17.5 cm over 1 m */
static const float HEAD[3] = { 0, 170, 0 }, ORC[3] = { 0, 100, 150 }, HAND[3] = { 0, 172.5f, 100 };
static const float VB[3] = { 0, 130, 40 }, VT[3] = { 0, 220, 40 };  /* a vertical blade 40 cm ahead */
static const float LB[3] = { -45, 165, 40 }, LT[3] = { 45, 165, 40 }; /* a level blade across, 40 cm ahead */

static void test_types(void)
{
    somvr_df_cfg c;
    somvr_df_cfg_default(&c);
    CHECK(somvr_df_can_parry(0) && !somvr_df_can_parry(1) && !somvr_df_can_parry(2) && somvr_df_can_parry(3) &&
              !somvr_df_can_parry(4) && !somvr_df_can_parry(-1) && !somvr_df_can_parry(9),
          "parry by type (the stock counter's CanParry/CanCounter: 1 and 2 rejected)");
    CHECK(somvr_df_can_block(&c, 0) && !somvr_df_can_block(&c, 1) && somvr_df_can_block(&c, 2) &&
              !somvr_df_can_block(&c, 3) && !somvr_df_can_block(&c, 4) && !somvr_df_can_block(&c, 7),
          "block by type");
    c.red_block = 1;
    CHECK(somvr_df_can_block(&c, 1) && !somvr_df_can_block(&c, 3), "red_block: type 1 blockable, 3 still not");
    CHECK(somvr_df_can_dodge(0) && somvr_df_can_dodge(1) && somvr_df_can_dodge(3) && !somvr_df_can_dodge(4) &&
              !somvr_df_can_dodge(-1),
          "dodge by type");
    CHECK(!strcmp(somvr_df_type_name(0), "normal") && !strcmp(somvr_df_type_name(3), "cannot-block") &&
              !strcmp(somvr_df_type_name(5), "unknown") && !strcmp(somvr_df_outcome_name(SOMVR_DFO_PARRY_BLOCK), "parry-block") &&
              !strcmp(somvr_df_outcome_name(SOMVR_DFO_N), "?"),
          "names");
    somvr_df_cfg_default(&c);
    CHECK(c.way_cm == 20 && c.cross_deg == 35 && c.align_deg == 50 && NEAR(c.parry_s, 0.3, 1e-6) &&
              NEAR(c.grace_s, 0.05, 1e-6) && c.dodge_cm == 30 && c.back_cm == 40 && c.arrow_cm == 12 && c.parry_on &&
              c.block_on && !c.red_block,
          "defaults");
}

/* brute force: the minimum distance between the blade and the blow's line [P0, hand] by sampling both */
static float brute(const float b[3], const float t[3], const float p0[3], const float p1[3])
{
    float best = INFINITY;
    for (int i = 0; i <= 200; i++)
        for (int j = 0; j <= 200; j++) {
            float a = i / 200.0f, s = j / 200.0f, d[3];
            for (int k = 0; k < 3; k++)
                d[k] = (b[k] + (t[k] - b[k]) * a) - (p0[k] + (p1[k] - p0[k]) * s);
            float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (l < best)
                best = l;
        }
    return best;
}

static void test_guard(void)
{
    somvr_df_cfg c;
    somvr_df_cfg_default(&c);
    somvr_df_guard g = somvr_df_guard_eval(&c, VB, VT, HAND, NULL, HEAD);
    CHECK(g.valid && g.in_way && g.aligned && !g.swing_known && g.align_deg == -1 && NEAR(g.dist_cm, 0, 0.01) &&
              NEAR(g.cross_deg, 90 - atan2(17.5, 100) * 57.29578, 0.05) && NEAR(g.target[1], 155, 1e-4) &&
              NEAR(g.target[2], 0, 1e-4) && NEAR(g.point[2], 40, 1e-3) && g.point[1] > 155 && g.point[1] < 170,
          "vertical blade in the line: valid %d way %d aligned %d dist %g cross %g target %g contact y %g", g.valid,
          g.in_way, g.aligned, g.dist_cm, g.cross_deg, g.target[1], g.point[1]);
    /* a blade along the blow (pointing at the orc) stops nothing */
    float ab[3] = { 0, 157, 15 }, at[3] = { 0, 172, 100 };
    g = somvr_df_guard_eval(&c, ab, at, HAND, NULL, HEAD);
    CHECK(g.valid && !g.in_way && g.dist_cm < 2 && g.cross_deg < 2, "along: way %d dist %g cross %g", g.in_way,
          g.dist_cm, g.cross_deg);
    /* the crossing threshold: a blade tilted 34 / 36 deg off the line's direction */
    for (int k = 0; k < 2; k++) {
        float deg = k ? 36.0f : 34.0f, line = (float)atan2(17.5, 100);
        float a = line + deg * 0.01745329f, mid[3] = { 0, 162.3f, 40 };
        float b[3] = { 0, mid[1] - 45 * sinf(a), mid[2] - 45 * cosf(a) }, t[3] = { 0, mid[1] + 45 * sinf(a), mid[2] + 45 * cosf(a) };
        g = somvr_df_guard_eval(&c, b, t, HAND, NULL, HEAD);
        CHECK(g.in_way == k && NEAR(g.cross_deg, deg, 0.05), "cross %g deg: way %d (cross %g, dist %g)", deg, g.in_way,
              g.cross_deg, g.dist_cm);
    }
    /* beside the line: 19 cm in, 21 cm out */
    for (int k = 0; k < 2; k++) {
        float x = k ? 21.0f : 19.0f, b[3] = { x, 130, 40 }, t[3] = { x, 220, 40 };
        g = somvr_df_guard_eval(&c, b, t, HAND, NULL, HEAD);
        CHECK(g.in_way == !k && NEAR(g.dist_cm, x, 0.01), "beside %g cm: way %d dist %g", x, g.in_way, g.dist_cm);
    }
    /* behind the body: not in the way, though it crosses the line's extension */
    float bb[3] = { 0, 120, -25 }, bt[3] = { 0, 210, -25 };
    g = somvr_df_guard_eval(&c, bb, bt, HAND, NULL, HEAD);
    CHECK(g.valid && !g.in_way && g.dist_cm > 30, "behind: way %d dist %g", g.in_way, g.dist_cm);
    /* the swing plane: a side swing (the hand moving along +X) wants the vertical blade, an overhead one (down) the
     * level blade; a thrust (along the line) leaves it unknown; a slow hand too */
    float side[3] = { 300, 0, 0 }, over[3] = { 0, -300, 0 }, slow[3] = { 90, 0, 0 };
    float thrust[3] = { 0, -17.5f * 4, -100 * 4 };
    somvr_df_guard v1 = somvr_df_guard_eval(&c, VB, VT, HAND, side, HEAD), l1 = somvr_df_guard_eval(&c, LB, LT, HAND, side, HEAD);
    somvr_df_guard v2 = somvr_df_guard_eval(&c, VB, VT, HAND, over, HEAD), l2 = somvr_df_guard_eval(&c, LB, LT, HAND, over, HEAD);
    somvr_df_guard v3 = somvr_df_guard_eval(&c, VB, VT, HAND, thrust, HEAD), l3 = somvr_df_guard_eval(&c, LB, LT, HAND, thrust, HEAD);
    somvr_df_guard v4 = somvr_df_guard_eval(&c, VB, VT, HAND, slow, HEAD);
    CHECK(v1.swing_known && v1.aligned && v1.align_deg < 12 && l1.in_way && !l1.aligned && l1.align_deg > 85,
          "side swing: vertical %d (%g deg), level way %d aligned %d (%g deg)", v1.aligned, v1.align_deg, l1.in_way,
          l1.aligned, l1.align_deg);
    CHECK(v2.swing_known && v2.in_way && !v2.aligned && v2.align_deg > 85 && l2.aligned && l2.align_deg < 1,
          "overhead: vertical aligned %d (%g), level %d (%g)", v2.aligned, v2.align_deg, l2.aligned, l2.align_deg);
    CHECK(!v3.swing_known && v3.aligned && l3.aligned && v3.align_deg == -1, "thrust: unknown plane, both aligned");
    CHECK(!v4.swing_known && v4.aligned, "a slow hand: unknown plane");
    /* the align threshold: 49 / 51 deg off the side swing's normal (a vertical blade leaning sideways about Z) */
    for (int k = 0; k < 2; k++) {
        float deg = k ? 51.0f : 49.0f;
        float ang = deg * 0.01745329f, mid[3] = { 0, 162.3f, 40 };
        /* rotate (0,1,0) toward +X by ang: the normal of the side swing is (0, -0.985, 0.172): close enough to Y that
         * the measured angle stays within 1.5 deg of deg; the threshold is checked on the measured value */
        float b[3] = { mid[0] - 45 * sinf(ang), mid[1] - 45 * cosf(ang), mid[2] }, t[3] = { mid[0] + 45 * sinf(ang), mid[1] + 45 * cosf(ang), mid[2] };
        g = somvr_df_guard_eval(&c, b, t, HAND, side, HEAD);
        CHECK(g.in_way && g.swing_known && fabsf(g.align_deg - deg) < 1.5f && g.aligned == (g.align_deg <= 50),
              "align %g: measured %g aligned %d", deg, g.align_deg, g.aligned);
    }
    /* the body target clamps: a low hand aims at the pelvis, a mid one at its own height */
    float low[3] = { 0, 60, 100 }, mid[3] = { 0, 130, 100 };
    g = somvr_df_guard_eval(&c, VB, VT, low, NULL, HEAD);
    CHECK(g.valid && NEAR(g.target[1], 100, 1e-4), "low hand: target y %g", g.target[1]);
    g = somvr_df_guard_eval(&c, VB, VT, mid, NULL, HEAD);
    CHECK(g.valid && NEAR(g.target[1], 130, 1e-4) && g.in_way, "mid hand: target y %g way %d", g.target[1], g.in_way);
    /* reach: a hand 2.9 m from the head is guarded against, one at 3.1 m (an archer's) not */
    float near_h[3] = { 0, 170, 290 }, far_h[3] = { 0, 170, 310 };
    CHECK(somvr_df_guard_eval(&c, VB, VT, near_h, NULL, HEAD).valid && !somvr_df_guard_eval(&c, VB, VT, far_h, NULL, HEAD).valid,
          "reach 300 cm");
    /* bad inputs */
    float nan3[3] = { NAN, 0, 0 }, inbody[3] = { 0.3f, 155, 0 }, b0[3] = { 0, 150, 40 };
    CHECK(!somvr_df_guard_eval(&c, nan3, VT, HAND, NULL, HEAD).valid && !somvr_df_guard_eval(&c, VB, VT, nan3, NULL, HEAD).valid &&
              !somvr_df_guard_eval(&c, VB, VT, HAND, NULL, nan3).valid && !somvr_df_guard_eval(&c, VB, VT, inbody, NULL, HEAD).valid &&
              !somvr_df_guard_eval(&c, b0, b0, HAND, NULL, HEAD).valid && !somvr_df_guard_eval(&c, NULL, VT, HAND, NULL, HEAD).valid,
          "bad inputs: invalid");
    CHECK(somvr_df_guard_eval(&c, VB, VT, HAND, nan3, HEAD).valid && !somvr_df_guard_eval(&c, VB, VT, HAND, nan3, HEAD).swing_known,
          "a NaN velocity: no swing plane, still valid");
    /* random cases against the brute force */
    srand(7);
    int agree = 0;
    for (int i = 0; i < 300; i++) {
        float hd[3] = { frand(-50, 50), frand(150, 190), frand(-50, 50) };
        float hn[3] = { hd[0] + frand(-150, 150), frand(60, 220), hd[2] + frand(-150, 150) };
        float b[3] = { frand(-100, 100), frand(100, 220), frand(-100, 100) };
        float t[3] = { b[0] + frand(-90, 90), b[1] + frand(-90, 90), b[2] + frand(-90, 90) };
        g = somvr_df_guard_eval(&c, b, t, hn, NULL, hd);
        if (!g.valid) {
            agree++;
            continue;
        }
        float top = hd[1] - 15, bot = hd[1] - 70, y = hn[1] > top ? top : hn[1] < bot ? bot : hn[1];
        float T[3] = { hd[0], y, hd[2] }, u[3] = { hn[0] - T[0], hn[1] - T[1], hn[2] - T[2] };
        float L = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]), f = L * 0.5f < 10 ? L * 0.5f : 10;
        float p0[3] = { T[0] + u[0] / L * f, T[1] + u[1] / L * f, T[2] + u[2] / L * f };
        agree += fabsf(brute(b, t, p0, hn) - g.dist_cm) < 0.8f;
    }
    CHECK(agree == 300, "random guards: %d of 300 agree with the brute force", agree);
}

static void test_zone(void)
{
    somvr_df_cfg c;
    somvr_df_cfg_default(&c);
    CHECK(!somvr_df_parry_zone(&c, 0, 0.761f, 0.4605f) && somvr_df_parry_zone(&c, 0, 0.761f, 0.4615f) &&
              somvr_df_parry_zone(&c, 0, 0.761f, 0.7275f) && !somvr_df_parry_zone(&c, 0, 0.761f, 0.7285f) &&
              !somvr_df_parry_zone(&c, 0, 0.761f, 0.79f) && !somvr_df_parry_zone(&c, 0, 0.761f, 0.1f),
          "P8 infantry: the zone 0.461..0.728 s");
    CHECK(!somvr_df_parry_zone(&c, 1.566f, 2.566f, 2.26f) && somvr_df_parry_zone(&c, 1.566f, 2.566f, 2.27f) &&
              somvr_df_parry_zone(&c, 1.566f, 2.566f, 2.53f) && !somvr_df_parry_zone(&c, 1.566f, 2.566f, 2.54f),
          "P8 archer: 2.266..2.533 s");
    CHECK(somvr_df_parry_zone(&c, 0.2f, 0.4f, 0.21f) && !somvr_df_parry_zone(&c, 0.2f, 0.4f, 0.19f),
          "a 0.2 s window: the zone starts at BPW");
    CHECK(!somvr_df_parry_zone(&c, 0.1f, 0.12f, 0.1f) && !somvr_df_parry_zone(&c, 0.1f, 0.12f, 0.11f),
          "a 20 ms window: no zone (the press would come after EPW)");
    CHECK(!somvr_df_parry_zone(&c, -1, -1, 0.5f) && !somvr_df_parry_zone(&c, 0.5f, 0.4f, 0.45f) &&
              !somvr_df_parry_zone(&c, 0, 0.761f, NAN),
          "no window, a backwards one, NaN");
    c.parry_s = 0.1f, c.lead_s = 0;
    CHECK(!somvr_df_parry_zone(&c, 0, 0.761f, 0.65f) && somvr_df_parry_zone(&c, 0, 0.761f, 0.67f) &&
              somvr_df_parry_zone(&c, 0, 0.761f, 0.761f),
          "parry_s 0.1, lead 0: 0.661..0.761");
}

/* one attack at 90 Hz from its start to t_end: the blade given by bl(ts) (0 = none), the hand still at HAND */
typedef int (*blade_fn)(float ts, float b[3], float t[3]);
static int bl_vert(float ts, float b[3], float t[3]) { (void)ts; memcpy(b, VB, 12), memcpy(t, VT, 12); return 1; }
static int bl_late(float ts, float b[3], float t[3]) { return ts >= 0.75f ? bl_vert(ts, b, t) : 0; }
static int bl_away(float ts, float b[3], float t[3])
{
    (void)ts;
    float x[3] = { 60, 130, 40 }, y[3] = { 60, 220, 40 };
    memcpy(b, x, 12), memcpy(t, y, 12);
    return 1;
}
/* in the way from 0.5 s (inside the zone) to 0.70 s, then 60 cm aside until the impact */
static int bl_leaves(float ts, float b[3], float t[3]) { return ts >= 0.5f && ts < 0.70f ? bl_vert(ts, b, t) : bl_away(ts, b, t); }
struct run {
    int fires, fire_frame, parried;
    float fire_ts;
    double t_end;
};
static struct run run_attack(const somvr_df_cfg *c, somvr_df_track *tr, double t0, int window, int type, blade_fn bl,
                             float bpw, float epw, float until, const float head_at_end[3])
{
    struct run r = { 0, -1, 0, -1, 0 };
    somvr_df_track_init(tr);
    for (int f = 0;; f++) {
        float ts = f / 90.0f;
        if (ts > until + 1e-6f)
            break;
        somvr_df_in in;
        memset(&in, 0, sizeof in);
        in.ts = ts, in.bpw = bpw, in.epw = epw, in.window = window, in.type = type;
        in.blade = bl ? bl(ts, in.base, in.tip) : 0;
        memcpy(in.hand, HAND, 12), memcpy(in.orc, ORC, 12);
        float k = until > 0 ? ts / until : 0;
        for (int i = 0; i < 3; i++)
            in.head[i] = head_at_end ? HEAD[i] + (head_at_end[i] - HEAD[i]) * k : HEAD[i];
        if (somvr_df_step(c, tr, t0 + ts, &in)) {
            r.fires++;
            if (r.fire_frame < 0)
                r.fire_frame = f, r.fire_ts = ts;
        }
        r.t_end = t0 + ts;
    }
    r.parried = tr->parried;
    return r;
}

static void test_step_verdict(void)
{
    somvr_df_cfg c;
    somvr_df_cfg_default(&c);
    somvr_df_track tr;
    /* the blade in the way from the start: the parry fires once, at the zone's first frame (42/90 = 0.4667 s) */
    struct run r = run_attack(&c, &tr, 100, 1, 0, bl_vert, 0, 0.761f, 0.8f, NULL);
    CHECK(r.fires == 1 && r.fire_frame == 42 && NEAR(r.fire_ts, 42 / 90.0, 1e-6) && tr.parried &&
              NEAR(tr.parry_t, 100 + 42 / 90.0, 1e-6) && tr.started && NEAR(tr.t0, 100, 1e-9),
          "held guard: fires %d at frame %d (%g s)", r.fires, r.fire_frame, r.fire_ts);
    CHECK(somvr_df_verdict(&c, &tr, 100.8, 0) == SOMVR_DFO_PARRY_BLOCK,
          "the counter did not take: the impact is stopped (parry-block)");
    CHECK(somvr_df_verdict(&c, &tr, 100.8, 3) == SOMVR_DFO_HIT, "type 3 parried (counterable, not blockable): lands");
    /* the game's window byte: shut = no parry; unknown = the keys decide */
    r = run_attack(&c, &tr, 100, 0, 0, bl_vert, 0, 0.761f, 0.8f, NULL);
    CHECK(r.fires == 0 && !tr.parried && somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_BLOCK,
          "window byte 0: no parry (%d), a block at the impact", r.fires);
    r = run_attack(&c, &tr, 100, -1, 0, bl_vert, 0, 0.761f, 0.8f, NULL);
    CHECK(r.fires == 1 && r.fire_frame == 42, "window unknown: the keys (%d at %d)", r.fires, r.fire_frame);
    /* the types */
    int want[5] = { 1, 0, 0, 1, 0 };
    for (int ty = 0; ty < 5; ty++) {
        r = run_attack(&c, &tr, 100, 1, ty, bl_vert, 0, 0.761f, 0.8f, NULL);
        int v = somvr_df_verdict(&c, &tr, r.t_end, ty);
        int wv = ty == 0 ? SOMVR_DFO_PARRY_BLOCK : ty == 2 ? SOMVR_DFO_BLOCK : SOMVR_DFO_HIT;
        CHECK(r.fires == want[ty] && v == wv, "type %d: fires %d verdict %s (want %d, %s)", ty, r.fires,
              somvr_df_outcome_name(v), want[ty], somvr_df_outcome_name(wv));
    }
    /* no window = no parry, still a block */
    r = run_attack(&c, &tr, 100, -1, 0, bl_vert, -1, -1, 0.8f, NULL);
    CHECK(r.fires == 0 && somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_BLOCK, "no window: block only");
    /* parry off, block off */
    c.parry_on = 0;
    r = run_attack(&c, &tr, 100, 1, 0, bl_vert, 0, 0.761f, 0.8f, NULL);
    CHECK(r.fires == 0 && somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_BLOCK, "parry off: a block");
    c.block_on = 0;
    CHECK(somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_HIT, "block off too: the hit lands");
    somvr_df_cfg_default(&c);
    /* no blade */
    r = run_attack(&c, &tr, 100, 1, 0, NULL, 0, 0.761f, 0.8f, NULL);
    CHECK(r.fires == 0 && tr.way_t < 0 && !tr.g.valid && somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_HIT,
          "no blade: nothing");
    /* the blade arrives after the zone (0.75 s): no parry; at the impact (0.8) a block */
    r = run_attack(&c, &tr, 100, 1, 0, bl_late, 0, 0.761f, 0.8f, NULL);
    CHECK(r.fires == 0 && NEAR(tr.way_t, r.t_end, 1e-9) && somvr_df_verdict(&c, &tr, r.t_end + 0.016, 0) == SOMVR_DFO_BLOCK,
          "late blade: no parry, block at the impact");
    /* the grace edge: the last frame in the way 50 ms before the impact still blocks, 51 ms not */
    double wt = tr.way_t;
    CHECK(somvr_df_verdict(&c, &tr, wt + 0.05, 0) == SOMVR_DFO_BLOCK && somvr_df_verdict(&c, &tr, wt + 0.051, 0) == SOMVR_DFO_HIT,
          "grace 50 ms");
    /* the blade 60 cm aside all along: nothing */
    r = run_attack(&c, &tr, 100, 1, 0, bl_away, 0, 0.761f, 0.8f, NULL);
    CHECK(r.fires == 0 && tr.way_t < 0 && somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_HIT,
          "the blade aside all along: the hit lands");
    /* in the way inside the zone, then away 0.1 s before the impact: parried at 0.5 s, so stopped at the impact; with
     * the parry off the same blade is too early for a block (its last frame in the way 0.1 s before the impact) */
    r = run_attack(&c, &tr, 100, 1, 0, bl_leaves, 0, 0.761f, 0.8f, NULL);
    CHECK(r.fires == 1 && r.fire_frame == 45 && somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_PARRY_BLOCK,
          "in the way 0.5..0.7 s: parried at frame %d, stopped", r.fire_frame);
    c.parry_on = 0;
    r = run_attack(&c, &tr, 100, 1, 0, bl_leaves, 0, 0.761f, 0.8f, NULL);
    CHECK(r.fires == 0 && tr.way_t > 0 && r.t_end - tr.way_t > 0.09 && somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_HIT,
          "parry off, the blade gone 0.1 s before the impact: lands");
    c.parry_on = 1;
    CHECK(somvr_df_verdict(&c, NULL, 1, 0) == SOMVR_DFO_HIT, "no track: hit");
    somvr_df_track fresh;
    somvr_df_track_init(&fresh);
    CHECK(somvr_df_verdict(&c, &fresh, 1, 0) == SOMVR_DFO_HIT && fresh.way_t < 0 && fresh.dodge_t < 0, "a fresh track: hit");
}

static void test_dodge(void)
{
    somvr_df_cfg c;
    somvr_df_cfg_default(&c);
    somvr_df_track tr;
    float side31[3] = { 31, 170, 0 }, side29[3] = { -29, 170, 0 }, back41[3] = { 0, 170, -41 }, back39[3] = { 0, 170, -39 };
    float fwd[3] = { 0, 170, 60 }, diag[3] = { 23, 170, -23 };
    struct run r = run_attack(&c, &tr, 50, 1, 0, NULL, 0, 0.761f, 0.8f, side31);
    CHECK(somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_DODGE && NEAR(tr.lateral_cm, 31, 1e-3),
          "sideways 31 cm: dodge (lateral %g)", tr.lateral_cm);
    r = run_attack(&c, &tr, 50, 1, 0, NULL, 0, 0.761f, 0.8f, side29);
    CHECK(somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_HIT && NEAR(tr.lateral_cm, 29, 1e-3), "29 cm: hit");
    r = run_attack(&c, &tr, 50, 1, 0, NULL, 0, 0.761f, 0.8f, back41);
    CHECK(somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_DODGE && NEAR(tr.back_cm, 41, 1e-3), "back 41: dodge (%g)",
          tr.back_cm);
    r = run_attack(&c, &tr, 50, 1, 0, NULL, 0, 0.761f, 0.8f, back39);
    CHECK(somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_HIT, "back 39: hit");
    r = run_attack(&c, &tr, 50, 1, 0, NULL, 0, 0.761f, 0.8f, fwd);
    CHECK(somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_HIT && tr.back_cm < 0, "toward the orc: hit");
    r = run_attack(&c, &tr, 50, 1, 0, NULL, 0, 0.761f, 0.8f, diag);
    CHECK(somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_HIT, "23 cm aside and 23 cm back: neither threshold");
    /* types: a red attack (1) and an unblockable (3) are dodged, a ranged one not */
    r = run_attack(&c, &tr, 50, 1, 1, bl_vert, 0, 0.761f, 0.8f, side31);
    CHECK(r.fires == 0 && somvr_df_verdict(&c, &tr, r.t_end, 1) == SOMVR_DFO_DODGE, "red: the blade does nothing, the dodge does");
    CHECK(somvr_df_verdict(&c, &tr, r.t_end, 4) == SOMVR_DFO_HIT, "ranged: no dodge here");
    /* block before dodge */
    r = run_attack(&c, &tr, 50, 0, 0, bl_vert, 0, 0.761f, 0.8f, side31);
    CHECK(somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_BLOCK, "in the way and out of the path: a block");
    /* the dodge's grace: out of the path, then back in 0.1 s before the impact */
    somvr_df_track_init(&tr);
    somvr_df_in in;
    memset(&in, 0, sizeof in);
    in.window = 1, in.bpw = 0, in.epw = 0.761f;
    memcpy(in.hand, HAND, 12), memcpy(in.orc, ORC, 12), memcpy(in.head, HEAD, 12);
    somvr_df_step(&c, &tr, 10.0, &in);
    in.head[0] = 35;
    somvr_df_step(&c, &tr, 10.6, &in);
    in.head[0] = 5;
    somvr_df_step(&c, &tr, 10.7, &in);
    CHECK(somvr_df_verdict(&c, &tr, 10.7, 0) == SOMVR_DFO_HIT && NEAR(tr.dodge_t, 10.6, 1e-9), "back in the path: hit");
    /* off */
    c.dodge_cm = 0, c.back_cm = 0;
    r = run_attack(&c, &tr, 50, 1, 0, NULL, 0, 0.761f, 0.8f, side31);
    CHECK(somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_HIT && tr.dodge_t < 0, "dodges off");
    /* the game's own move of Talion (a knockback 85 cm back, boot-check-20261024-020824) carried into the start: no
     * dodge; the same move by the player (moved 0): a dodge; the player's 35 cm aside on top of the game's move: a dodge */
    somvr_df_cfg_default(&c);
    for (int k = 0; k < 3; k++) {
        somvr_df_track_init(&tr);
        memset(&in, 0, sizeof in);
        in.window = 1, in.bpw = 0, in.epw = 0.761f;
        memcpy(in.hand, HAND, 12), memcpy(in.orc, ORC, 12), memcpy(in.head, HEAD, 12);
        somvr_df_step(&c, &tr, 10.0, &in);
        for (int f = 1; f <= 72; f++) { /* 0.8 s at 90 Hz: 85 cm straight back over frames 10..40 */
            float step = f >= 10 && f < 40 ? -85.0f / 30 : 0;
            in.head[2] += step;
            in.moved[2] = k == 1 ? 0 : step;
            in.head[0] = k == 2 && f >= 40 ? HEAD[0] + 35 : HEAD[0];
            somvr_df_step(&c, &tr, 10.0 + f / 90.0, &in);
        }
        int v = somvr_df_verdict(&c, &tr, 10.8, 0);
        CHECK(k == 0 ? v == SOMVR_DFO_HIT && fabsf(tr.back_cm) < 0.01f && tr.lateral_cm < 0.01f
                     : v == SOMVR_DFO_DODGE && (k == 1 ? NEAR(tr.back_cm, 85, 0.01) : NEAR(tr.lateral_cm, 35, 0.01)),
              "%s: verdict %d back %.2f lateral %.2f", k == 0 ? "the game's knockback: no dodge" : k == 1 ? "the player's 85 cm back: a dodge" : "the player's 35 cm aside after the game's knockback: a dodge",
              v, tr.back_cm, tr.lateral_cm);
    }
    /* the orc's own step away during its attack (an infantry's 'Front_2_LL', 80 cm): no dodge; the player's 41 cm back
     * with the orc stepping in 50 cm: a dodge (measured from where the orc stood at the start) */
    for (int k = 0; k < 2; k++) {
        somvr_df_track_init(&tr);
        memset(&in, 0, sizeof in);
        in.window = 1, in.bpw = 0, in.epw = 0.761f;
        memcpy(in.hand, HAND, 12), memcpy(in.orc, ORC, 12), memcpy(in.head, HEAD, 12);
        somvr_df_step(&c, &tr, 10.0, &in);
        for (int f = 1; f <= 72; f++) {
            in.orc[2] = ORC[2] + (k == 0 ? 80.0f : -50.0f) * (f < 40 ? f / 40.0f : 1);
            in.head[2] = HEAD[2] - (k == 1 ? 41.0f * (f < 40 ? f / 40.0f : 1) : 0);
            somvr_df_step(&c, &tr, 10.0 + f / 90.0, &in);
        }
        int v = somvr_df_verdict(&c, &tr, 10.8, 0);
        CHECK(k == 0 ? v == SOMVR_DFO_HIT && fabsf(tr.back_cm) < 0.01f : v == SOMVR_DFO_DODGE && NEAR(tr.back_cm, 41, 0.01),
              "%s: verdict %d back %.2f", k == 0 ? "the orc stepping away 80 cm: no dodge" : "41 cm back, the orc stepping in: a dodge",
              v, tr.back_cm);
    }
    /* a head on top of the orc at the start: a side axis still exists */
    somvr_df_cfg_default(&c);
    somvr_df_track_init(&tr);
    memcpy(in.head, ORC, 12), memcpy(in.orc, ORC, 12);
    somvr_df_step(&c, &tr, 1, &in);
    CHECK(NEAR(tr.side0[0], 1, 1e-6) && tr.dist0 < 1, "degenerate start: side axis X");
}

static void test_two(void)
{
    somvr_df_cfg c;
    somvr_df_cfg_default(&c);
    /* A ahead (the scene's), B on the left (its hand 1 m to the left of the head): the blade before A only */
    float handB[3] = { -100, 172.5f, 0 }, orcB[3] = { -150, 100, 0 };
    somvr_df_track a, b;
    somvr_df_track_init(&a), somvr_df_track_init(&b);
    somvr_df_press p;
    memset(&p, 0, sizeof p);
    int presses = 0;
    for (int f = 0; f <= 72; f++) {
        float ts = f / 90.0f;
        somvr_df_in ia, ib;
        memset(&ia, 0, sizeof ia);
        ia.ts = ts, ia.bpw = 0, ia.epw = 0.761f, ia.window = 1, ia.blade = 1;
        memcpy(ia.base, VB, 12), memcpy(ia.tip, VT, 12), memcpy(ia.hand, HAND, 12), memcpy(ia.orc, ORC, 12);
        memcpy(ia.head, HEAD, 12);
        ib = ia;
        memcpy(ib.hand, handB, 12), memcpy(ib.orc, orcB, 12);
        ib.bpw = -1, ib.epw = -1; /* B: no parry window (a cannot-counter swing, say) */
        if (somvr_df_step(&c, &a, 10 + ts, &ia))
            presses += somvr_df_press_start(&p, 119, 3, 10 + ts, 0.25f);
        if (somvr_df_step(&c, &b, 10 + ts, &ib))
            presses += somvr_df_press_start(&p, 119, 3, 10 + ts, 0.25f);
    }
    CHECK(a.parried && !b.parried && presses == 1 && b.g.valid && !b.g.in_way && b.g.dist_cm > 40 &&
              somvr_df_verdict(&c, &a, 10.8, 0) == SOMVR_DFO_PARRY_BLOCK && somvr_df_verdict(&c, &b, 10.8, 0) == SOMVR_DFO_HIT,
          "two attackers, the blade before A: A parried, B lands (B dist %g)", b.g.dist_cm);
    /* both in the way: a blade on the diagonal crossing both lines; both parried in the same frame = one press */
    float db[3] = { -60, 168, -10 }, dt[3] = { 20, 158, 70 };
    somvr_df_track_init(&a), somvr_df_track_init(&b);
    memset(&p, 0, sizeof p);
    presses = 0;
    int fa = -1, fb = -1;
    for (int f = 0; f <= 72; f++) {
        float ts = f / 90.0f;
        somvr_df_in ia;
        memset(&ia, 0, sizeof ia);
        ia.ts = ts, ia.bpw = 0, ia.epw = 0.761f, ia.window = 1, ia.blade = 1;
        memcpy(ia.base, db, 12), memcpy(ia.tip, dt, 12), memcpy(ia.hand, HAND, 12), memcpy(ia.orc, ORC, 12);
        memcpy(ia.head, HEAD, 12);
        somvr_df_in ib = ia;
        memcpy(ib.hand, handB, 12), memcpy(ib.orc, orcB, 12);
        if (somvr_df_step(&c, &a, 20 + ts, &ia))
            fa = f, presses += somvr_df_press_start(&p, 119, 3, 20 + ts, 0.25f);
        if (somvr_df_step(&c, &b, 20 + ts, &ib))
            fb = f, presses += somvr_df_press_start(&p, 119, 3, 20 + ts, 0.25f);
    }
    CHECK(a.parried && b.parried && fa == 42 && fb == 42 && presses == 1 && p.presses == 1 && p.refused == 1,
          "both parried in frame %d/%d: presses %d (refused %llu); A way %d (%g cm) B way %d (%g cm)", fa, fb, presses,
          (unsigned long long)p.refused, a.g.in_way, a.g.dist_cm, b.g.in_way, b.g.dist_cm);
    /* B 0.3 s later than A: a second press after the cooldown */
    memset(&p, 0, sizeof p);
    CHECK(somvr_df_press_start(&p, 119, 3, 5.0, 0.25f) && somvr_df_press_tick(&p, 5.01, 0.15f) == 119 &&
              somvr_df_press_tick(&p, 5.02, 0.15f) == 119 && somvr_df_press_tick(&p, 5.03, 0.15f) == 119 &&
              somvr_df_press_tick(&p, 5.04, 0.15f) == 0 && !somvr_df_press_start(&p, 119, 3, 5.2, 0.25f) &&
              somvr_df_press_start(&p, 144, 2, 5.3, 0.25f) && somvr_df_press_tick(&p, 5.31, 0.15f) == 144 &&
              somvr_df_press_tick(&p, 5.32, 0.15f) == 144 && somvr_df_press_tick(&p, 5.33, 0.15f) == 0 && p.presses == 2 &&
              p.refused == 1 && p.stale == 0,
          "press: 3 ticks, the cooldown, a second press");
    CHECK(!somvr_df_press_start(&p, 0, 3, 9, 0.25f) && !somvr_df_press_start(&p, 119, 0, 9, 0.25f) &&
              somvr_df_press_tick(&p, 9, 0.15f) == 0,
          "press: no command, no ticks");
    /* the input was not live for 0.2 s after the parry: the press is dropped, not sent late; the edge 0.15 s */
    memset(&p, 0, sizeof p);
    CHECK(somvr_df_press_start(&p, 119, 3, 7.0, 0.25f) && somvr_df_press_tick(&p, 7.2, 0.15f) == 0 && p.stale == 1 &&
              p.left == 0 && somvr_df_press_tick(&p, 7.21, 0.15f) == 0,
          "press: stale after 0.2 s");
    memset(&p, 0, sizeof p);
    CHECK(somvr_df_press_start(&p, 119, 2, 7.0, 0.25f) && somvr_df_press_tick(&p, 7.15, 0.15f) == 119 &&
              somvr_df_press_tick(&p, 7.1501, 0.15f) == 0 && p.stale == 1,
          "press: the 0.15 s edge (its first tick in time, the second late)");
    memset(&p, 0, sizeof p);
    CHECK(somvr_df_press_start(&p, 119, 3, 1, 0) && !somvr_df_press_start(&p, 119, 3, 1, 0) && p.refused == 1,
          "press: not while one is held");
}

static void test_arrow(void)
{
    somvr_df_cfg c;
    somvr_df_cfg_default(&c);
    float d = 0, pt[3] = { 0, 0, 0 }, dir[3] = { 0, 0, -1 }, b[3] = { 0, 120, 40 }, t[3] = { 0, 210, 40 };
    float p100[3] = { 0, 170, 100 }, p200[3] = { 0, 170, 200 }, p59[3] = { 0, 170, 59 };
    CHECK(somvr_df_arrow(&c, p100, dir, 2800, HEAD, b, t, NULL, NULL, 0, &d, pt) && d < 1e-3 && NEAR(pt[1], 170, 1e-3) &&
              NEAR(pt[2], 40, 1e-3),
          "on the blade: dist %g at %g %g", d, pt[1], pt[2]);
    CHECK(!somvr_df_arrow(&c, p200, dir, 2800, HEAD, b, t, NULL, NULL, 0, &d, pt) && NEAR(d, 90, 1e-3),
          "a step short (70 cm from 2 m): %g", d);
    CHECK(somvr_df_arrow(&c, p59, dir, 2800, HEAD, b, t, NULL, NULL, 0, &d, pt), "already close");
    /* beside: 11 in, 13 out */
    for (int k = 0; k < 2; k++) {
        float x = k ? 13.0f : 11.0f, bb[3] = { x, 120, 40 }, tt[3] = { x, 210, 40 };
        CHECK(somvr_df_arrow(&c, p100, dir, 2800, HEAD, bb, tt, NULL, NULL, 0, &d, pt) == !k && NEAR(d, x, 1e-3),
              "beside %g: dist %g", x, d);
    }
    /* going away; passing 2 m off the head */
    float away[3] = { 0, 0, 1 }, far[3] = { 200, 170, 100 };
    float fb[3] = { 200, 120, 40 }, ft[3] = { 200, 210, 40 };
    CHECK(!somvr_df_arrow(&c, p100, away, 2800, HEAD, b, t, NULL, NULL, 0, &d, pt) &&
              !somvr_df_arrow(&c, far, dir, 2800, HEAD, fb, ft, NULL, NULL, 0, &d, pt),
          "away / far: not ours");
    /* the blade swept across the path between two frames (x -30 -> +30): the midpoint catches it */
    float b0[3] = { -30, 120, 40 }, t0[3] = { -30, 210, 40 }, b1[3] = { 30, 120, 40 }, t1[3] = { 30, 210, 40 };
    CHECK(somvr_df_arrow(&c, p100, dir, 2800, HEAD, b1, t1, b0, t0, 1, &d, pt) && d < 1e-3 &&
              !somvr_df_arrow(&c, p100, dir, 2800, HEAD, b1, t1, b0, t0, 0, &d, pt),
          "a swept blade: the midpoint with the last frame, not without it");
    /* a scaled direction is normalised; NaN, no speed (a 1 cm step), off */
    float dir2[3] = { 0, 0, -3 }, nan3[3] = { NAN, 0, 0 };
    CHECK(somvr_df_arrow(&c, p100, dir2, 2800, HEAD, b, t, NULL, NULL, 0, &d, pt), "scaled direction");
    CHECK(!somvr_df_arrow(&c, nan3, dir, 2800, HEAD, b, t, NULL, NULL, 0, &d, pt) &&
              !somvr_df_arrow(&c, p100, dir, NAN, HEAD, b, t, NULL, NULL, 0, &d, pt) &&
              !somvr_df_arrow(&c, p100, dir, 0, HEAD, b, t, NULL, NULL, 0, &d, pt),
          "NaN, no speed (1 cm step from 1 m: short)");
    float zero[3] = { 0, 0, 0 };
    CHECK(!somvr_df_arrow(&c, p100, zero, 2800, HEAD, b, t, NULL, NULL, 0, &d, pt), "no direction");
    c.arrow_cm = 0;
    CHECK(!somvr_df_arrow(&c, p100, dir, 2800, HEAD, b, t, NULL, NULL, 0, &d, pt), "arrows off");
    /* a spear at 30 m/s from 20 m at the chest: frames of 1/60 s until it reaches the blade (no frame skips it) */
    somvr_df_cfg_default(&c);
    float s[3] = { 0, 150, 2000 }, chest_b[3] = { -40, 150, 40 }, chest_t[3] = { 40, 150, 40 };
    int blocked_at = -1;
    for (int f = 0; f < 120 && blocked_at < 0; f++) {
        if (somvr_df_arrow(&c, s, dir, 3000, HEAD, chest_b, chest_t, NULL, NULL, 0, &d, pt))
            blocked_at = f;
        s[2] -= 3000.0f / 60;
    }
    CHECK(blocked_at > 0 && s[2] > 40 - 1e-3, "a spear stepping 50 cm a frame: blocked before it passes (frame %d, at z %g)",
          blocked_at, s[2]);
}

static void test_haptic(void)
{
    somvr_df_haptic h = somvr_df_haptic_of(SOMVR_DFO_PARRY, 1, 1);
    CHECK(NEAR(h.amp[1], 0.9, 1e-6) && NEAR(h.dur_s[1], 0.06, 1e-6) && NEAR(h.amp[0], 0.35, 1e-6) && NEAR(h.dur_s[0], 0.04, 1e-6),
          "parry: the sword hand (right) 0.9, the other 0.35");
    h = somvr_df_haptic_of(SOMVR_DFO_PARRY, 0, 0.5f);
    CHECK(NEAR(h.amp[0], 0.45, 1e-6) && NEAR(h.amp[1], 0.175, 1e-6), "left-handed, half strength");
    h = somvr_df_haptic_of(SOMVR_DFO_BLOCK, 1, 1);
    CHECK(NEAR(h.amp[1], 0.75, 1e-6) && NEAR(h.dur_s[1], 0.09, 1e-6) && h.amp[0] == 0, "block: a clank in the sword hand");
    somvr_df_haptic pb = somvr_df_haptic_of(SOMVR_DFO_PARRY_BLOCK, 1, 1);
    CHECK(!memcmp(&pb, &h, sizeof h), "parry-block feels like a block");
    h = somvr_df_haptic_of(SOMVR_DFO_DODGE, 1, 1);
    CHECK(NEAR(h.amp[0], 0.25, 1e-6) && NEAR(h.amp[1], 0.25, 1e-6) && NEAR(h.dur_s[0], 0.12, 1e-6), "dodge: both hands");
    h = somvr_df_haptic_of(SOMVR_DFO_ARROW, 1, 2);
    CHECK(NEAR(h.amp[1], 0.6, 1e-6) && h.amp[0] == 0, "arrow: scale clamped to 1");
    h = somvr_df_haptic_of(SOMVR_DFO_HIT, 1, 1);
    somvr_df_haptic z = somvr_df_haptic_of(SOMVR_DFO_BLOCK, 1, 0), bad = somvr_df_haptic_of(99, 1, 1),
                    nn = somvr_df_haptic_of(SOMVR_DFO_BLOCK, 1, NAN);
    CHECK(h.amp[0] == 0 && h.amp[1] == 0 && z.amp[1] == 0 && bad.amp[1] == 0 && nn.amp[1] == 0, "none: hit, scale 0, bad, NaN");
}

/* SOMVR_DEFENCE_CLANK: the switch's words, the recoil's permission (FUN_0280fe60's two lists) and the interrupter, the
 * clank's pulses */
static void test_clank(void)
{
    unsigned b = 99;
    char s[64];
    CHECK(somvr_df_clank_parse(NULL, &b) && b == 0 && somvr_df_clank_parse("", &b) && b == 0 &&
              somvr_df_clank_parse("off", &b) && b == 0 && somvr_df_clank_parse("0", &b) && b == 0,
          "none: NULL, empty, off, 0");
    CHECK(somvr_df_clank_parse("on", &b) && b == SOMVR_DFC_ALL && somvr_df_clank_parse("ALL", &b) && b == SOMVR_DFC_ALL,
          "on / all = every part");
    CHECK(somvr_df_clank_parse(" recoil, Stop ,haptic", &b) && b == (SOMVR_DFC_RECOIL | SOMVR_DFC_STOP | SOMVR_DFC_HAPTIC) &&
              !strcmp(somvr_df_clank_str(b, s, sizeof s), "recoil,stop,haptic"),
          "a list with blanks and capitals: %#x '%s'", b, s);
    CHECK(somvr_df_clank_parse("late,fx", &b) && b == (SOMVR_DFC_LATE | SOMVR_DFC_FX) &&
              !strcmp(somvr_df_clank_str(SOMVR_DFC_ALL, s, sizeof s), "recoil,late,stop,haptic,fx") &&
              !strcmp(somvr_df_clank_str(0, s, sizeof s), "off"),
          "late,fx; every part's words; none = off");
    CHECK(!somvr_df_clank_parse("stop,spark", &b) && b == 0 && !somvr_df_clank_parse("averyveryverylongwordthatisnotapart", &b),
          "a word that is not a part: not understood, nothing on");
    CHECK(!strcmp(somvr_df_clank_str(SOMVR_DFC_ALL, s, 12), "recoil,late"), "a short buffer: whole words only ('%s')", s);

    /* build 7's infantry strike: CanInterrupt 0, the light record's category not among the exceptions (refused);
     * a counter-like record with category 0xC2 that is */
    const uintptr_t light[2] = { 0xA1, 0xA2 }, heavy[1] = { 0xB1 }, ctr[2] = { 0xC1, 0xC2 };
    somvr_df_irules r;
    memset(&r, 0, sizeof r);
    CHECK(somvr_df_may_interrupt(&r, light, 2) && somvr_df_may_interrupt(NULL, light, 2), "unknown rules: the game decides");
    r.known = 1, r.can = 0, r.nexc = 2, r.exc[0] = 0xC2, r.exc[1] = 0xD9;
    CHECK(!somvr_df_may_interrupt(&r, light, 2) && !somvr_df_may_interrupt(&r, heavy, 1) && somvr_df_may_interrupt(&r, ctr, 2) &&
              !somvr_df_may_interrupt(&r, NULL, 0),
          "CanInterrupt 0: only a category of CannotInterruptExceptions");
    const uintptr_t *cc[3] = { light, heavy, ctr };
    const int cn[3] = { 2, 1, 2 };
    CHECK(somvr_df_pick_interrupter(&r, cc, cn, 3) == 2 && somvr_df_pick_interrupter(&r, cc, cn, 2) == -1,
          "the interrupter: the counter-like record; none among light and heavy");
    r.can = 1, r.nexc = 1, r.exc[0] = 0xA2;
    CHECK(!somvr_df_may_interrupt(&r, light, 2) && somvr_df_may_interrupt(&r, heavy, 1) &&
              somvr_df_pick_interrupter(&r, cc, cn, 3) == 1,
          "CanInterrupt 1: any attack but one with a category of CanInterruptExceptions (the light one here)");
    r.nexc = 0;
    CHECK(somvr_df_pick_interrupter(&r, cc, cn, 3) == 0, "CanInterrupt 1 with no exceptions: the light record itself");
    const uintptr_t zero[1] = { 0 };
    r.can = 0, r.nexc = 1, r.exc[0] = 0;
    CHECK(!somvr_df_may_interrupt(&r, zero, 1), "a 0 category never matches");

    somvr_df_clank_haptic h = somvr_df_clank_haptic_of(1);
    CHECK(NEAR(h.crack_amp, 1, 1e-6) && NEAR(h.crack_s, 0.05, 1e-6) && NEAR(h.ring_amp, 0.6, 1e-6) && NEAR(h.ring_s, 0.25, 1e-6) &&
              NEAR(h.knock_amp, 0.45, 1e-6) && NEAR(h.knock_s, 0.06, 1e-6),
          "the clank: a crack 1.0 / 50 ms and a ring 0.6 over 250 ms in the sword hand, a knock 0.45 / 60 ms");
    somvr_df_clank_haptic hh = somvr_df_clank_haptic_of(0.5f), z = somvr_df_clank_haptic_of(0), n = somvr_df_clank_haptic_of(NAN),
                          big = somvr_df_clank_haptic_of(3);
    CHECK(NEAR(hh.crack_amp, 0.5, 1e-6) && NEAR(hh.ring_amp, 0.3, 1e-6) && NEAR(hh.crack_s, 0.05, 1e-6) && z.crack_amp == 0 &&
              n.ring_amp == 0 && NEAR(big.crack_amp, 1, 1e-6),
          "the clank scaled: half, 0, NaN, clamped at 1");
    printf("clank: the parts' words; the recoil's permission by the orc's attack (CanInterrupt and its exception lists), "
           "the interrupter; a crack, a ring and a knock\n");
}

/* ---- build 10: the contact block (SOMVR_DEFENCE_BLOCK=contact) --------------------------------------------------------
 * The orc's weapon swept level at chest height (150 cm) about a pivot 1.2 m ahead: its base 45 cm and its tip 110 cm
 * from the pivot along d(phi) = (sin phi, 0, -cos phi); held raised at phi = 110 deg until 0.45 s (the wind-up), then
 * swung to -70 deg by 0.85 s: it passes x = 0 (in front of Talion) at 0.694 s; the impact at 0.70 s. */
static void orc_weapon(float ts, float ob[3], float ot[3])
{
    float ph = ts < 0.45f ? 110 : ts > 0.85f ? -70 : 110 - 180 * (ts - 0.45f) / 0.4f, r = ph * 0.0174533f;
    float d[3] = { sinf(r), 0, -cosf(r) }, pv[3] = { 0, 150, 120 };
    for (int i = 0; i < 3; i++)
        ob[i] = pv[i] + 45 * d[i], ot[i] = pv[i] + 110 * d[i];
}
/* our blade by ts: k 0 the vertical guard 40 cm ahead; 1 the same 50 cm higher (over the swing); 2 in the path only
 * until 0.4 s, then lowered under the swing; 3 along the orc's blade (parallel at the crossing); 4 none; 5 swung into the
 * path late (from the side, arriving at 0.6 s) */
static int our_blade(int k, float ts, float b[3], float t[3])
{
    static const float V0[3] = { 0, 130, 40 }, V1[3] = { 0, 220, 40 };
    if (k == 4)
        return 0;
    memcpy(b, V0, sizeof V0), memcpy(t, V1, sizeof V1);
    if (k == 1)
        b[1] += 50, t[1] += 50;
    if (k == 2 && ts > 0.4f)
        b[0] = t[0] = 30, b[1] = 20, t[1] = 100; /* lowered: 50 cm under the swing */
    if (k == 3)
        b[0] = 0, b[1] = 150, b[2] = 5, t[0] = 0, t[1] = 150, t[2] = 95;
    if (k == 5) {
        float u = ts < 0.45f ? 0 : ts > 0.6f ? 1 : (ts - 0.45f) / 0.15f;
        b[0] = t[0] = 70 * (1 - u);
    }
    return 1;
}
struct crun { int fires, contact_frames; double t_end; float fire_ts; };
static struct crun contact_attack(const somvr_df_cfg *c, somvr_df_track *tr, int k, int type, float hz, float dodge_x)
{
    struct crun r = { 0, 0, 0, -1 };
    somvr_df_track_init(tr);
    double t0 = 50;
    for (int f = 0;; f++) {
        float ts = (float)f / hz;
        if (ts > 0.72f)
            break;
        somvr_df_in in;
        memset(&in, 0, sizeof in);
        in.ts = ts, in.bpw = 0, in.epw = 0.66f, in.window = 1, in.type = type, in.impact = 0.70f;
        in.blade = our_blade(k, ts, in.base, in.tip);
        orc_weapon(ts, in.ob[0], in.ot[0]);
        in.norc = 1;
        memcpy(in.hand, in.ob[0], sizeof in.hand), memcpy(in.orc, ORC, sizeof in.orc);
        memcpy(in.head, HEAD, sizeof in.head);
        in.head[0] += dodge_x * (ts > 0.5f ? 1 : 0);
        double t = t0 + ts;
        int v = somvr_df_step(c, tr, t, &in);
        if (v == 2) {
            r.fires++;
            if (r.fire_ts < 0)
                r.fire_ts = ts;
        }
        r.contact_frames += v == 1; /* a parry (must not fire in contact mode) */
        r.t_end = t;
    }
    return r;
}
static void test_contact(void)
{
    somvr_df_cfg c;
    somvr_df_cfg_default(&c);
    CHECK(!c.contact && c.contact_cm == 10 && NEAR(c.contact_pre_s, 0.25, 1e-6) && NEAR(c.contact_post_s, 0.08, 1e-6) &&
              c.contact_cross_deg == 15 && c.contact_move_cm == 0,
          "contact defaults (off)");
    c.contact = 1;
    CHECK(!somvr_df_contact_window(&c, 0.7f, 0.44f) && somvr_df_contact_window(&c, 0.7f, 0.46f) &&
              somvr_df_contact_window(&c, 0.7f, 0.78f) && !somvr_df_contact_window(&c, 0.7f, 0.79f) &&
              !somvr_df_contact_window(&c, -1, 0.5f) && !somvr_df_contact_window(&c, 0.7f, NAN),
          "the window: 0.45..0.78 s around a 0.70 s impact; none without an impact");
    /* the sweep: crossing blades; a blade that jumps through ours between two frames; parallel ones */
    float cr = 0, pt[3];
    float b0[3] = { 0, 130, 40 }, t0[3] = { 0, 220, 40 }, o0[3] = { -40, 150, 0 }, p0[3] = { -40, 150, 80 };
    float o1[3] = { 40, 150, 0 }, p1[3] = { 40, 150, 80 };
    float d1 = somvr_df_sweep(b0, t0, b0, t0, o0, p0, o1, p1, 1, SOMVR_DF_SWEEP_STEPS, &cr, pt);
    float d0 = somvr_df_sweep(b0, t0, b0, t0, o0, p0, o1, p1, 0, SOMVR_DF_SWEEP_STEPS, &cr, pt);
    CHECK(d1 < 0.01f && NEAR(cr, 90, 0.1) && NEAR(pt[1], 150, 0.1) && d0 > 39,
          "the sweep catches a blade passing through ours between frames (%g cm, %g deg; the poses alone %g cm)", d1, cr, d0);
    float q0[3] = { 0, 130, 45 }, q1[3] = { 0, 220, 45 };
    float dp = somvr_df_sweep(b0, t0, b0, t0, q0, q1, q0, q1, 1, SOMVR_DF_SWEEP_STEPS, &cr, pt);
    CHECK(NEAR(dp, 5, 0.01) && cr < 0.1f, "parallel: %g cm, %g deg", dp, cr);
    float nanv[3] = { NAN, 0, 0 };
    CHECK(isinf(somvr_df_sweep(b0, t0, nanv, t0, o0, p0, o1, p1, 1, 6, &cr, pt)), "NaN: no approach");
    /* 1. the guard held in the swing's path: the blades meet at its crossing (0.694 s, inside the window); no parry at
     * the wind-up; the verdict: a block */
    somvr_df_track tr;
    struct crun r = contact_attack(&c, &tr, 0, 0, 90, 0);
    CHECK(r.fires == 1 && r.contact_frames == 0 && r.fire_ts > 0.68f && r.fire_ts < 0.70f && tr.contact_t > 0 &&
              tr.contact_cm <= c.contact_cm && tr.contact_cross > 60 && NEAR(tr.contact_pt[1], 150, 2) &&
              somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_BLOCK,
          "held in the path: met once at %.3f s (%.1f cm, %.0f deg), verdict %s", r.fire_ts, tr.contact_cm, tr.contact_cross,
          somvr_df_outcome_name(somvr_df_verdict(&c, &tr, r.t_end, 0)));
    /* the same at the headless 20 Hz: the sweep between frames */
    r = contact_attack(&c, &tr, 0, 0, 20, 0);
    CHECK(r.fires == 1 && r.fire_ts >= 0.65f && r.fire_ts <= 0.72f && somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_BLOCK,
          "20 Hz: met at %.3f s", r.fire_ts);
    /* 2. held out in the wrong place (50 cm higher, over the swing): the hit lands; the near miss is logged */
    r = contact_attack(&c, &tr, 1, 0, 90, 0);
    CHECK(r.fires == 0 && tr.contact_t < 0 && tr.best_cm > 25 && tr.best_cm < 35 &&
              somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_HIT,
          "the wrong place: no contact (closest %.1f cm), the hit lands", tr.best_cm);
    /* 3. too early: in the path during the wind-up, gone before the swing: the hit lands */
    r = contact_attack(&c, &tr, 2, 0, 90, 0);
    CHECK(r.fires == 0 && r.contact_frames == 0 && somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_HIT,
          "too early: the hit lands");
    /* 4. along the orc's blade (no crossing): no block */
    r = contact_attack(&c, &tr, 3, 0, 90, 0);
    CHECK(r.fires == 0 && somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_HIT, "parallel: no block (closest %.1f cm %.0f deg)",
          tr.best_cm, tr.best_cross);
    /* 5. no blade: the hit lands */
    r = contact_attack(&c, &tr, 4, 0, 90, 0);
    CHECK(r.fires == 0 && somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_HIT, "no blade: the hit lands");
    /* 6. swung into the path in time (from the side, there by 0.6 s): a block */
    r = contact_attack(&c, &tr, 5, 0, 90, 0);
    CHECK(r.fires == 1 && somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_BLOCK, "swung in in time: a block at %.3f s",
          r.fire_ts);
    /* 7. the types: a cannot-block strike (3) is never blocked; cannot-counter (2) is */
    r = contact_attack(&c, &tr, 0, 3, 90, 0);
    CHECK(r.fires == 0 && somvr_df_verdict(&c, &tr, r.t_end, 3) == SOMVR_DFO_HIT, "type 3: no block");
    r = contact_attack(&c, &tr, 0, 2, 90, 0);
    CHECK(r.fires == 1 && somvr_df_verdict(&c, &tr, r.t_end, 2) == SOMVR_DFO_BLOCK, "type 2: a block");
    /* 8. a dodge still counts: no blade, the head 35 cm aside from 0.5 s */
    r = contact_attack(&c, &tr, 4, 0, 90, 35);
    CHECK(somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_DODGE, "a dodge (no blade): %s",
          somvr_df_outcome_name(somvr_df_verdict(&c, &tr, r.t_end, 0)));
    /* build 11: SOMVR_DEFENCE_CONTACT_MOVE_CM 15: the still guard in the path lets the blow through (logged as still);
     * the guard swung in from the side (70 cm) still blocks */
    c.contact_move_cm = 15;
    r = contact_attack(&c, &tr, 0, 0, 90, 0);
    CHECK(r.fires == 0 && tr.still == 1 && tr.still_cm <= c.contact_cm && tr.blade_cm < 1 &&
              somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_HIT,
          "move 15: the still guard met (%.1f cm) but moved %.1f cm: the hit lands", tr.still_cm, tr.blade_cm);
    r = contact_attack(&c, &tr, 5, 0, 90, 0);
    CHECK(r.fires == 1 && !tr.still && tr.blade_cm > 60 && somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_BLOCK,
          "move 15: swung in (%.0f cm): a block", tr.blade_cm);
    c.contact_move_cm = 0;
    /* 9. block off: nothing blocks */
    c.block_on = 0;
    r = contact_attack(&c, &tr, 0, 0, 90, 0);
    CHECK(r.fires == 0 && somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_HIT, "block off: the hit lands");
    c.block_on = 1;
    /* 10. build 9's guard (contact off) with the same held blade: the parry at the wind-up (the old way) */
    c.contact = 0;
    r = contact_attack(&c, &tr, 0, 0, 90, 0);
    CHECK(r.contact_frames == 1 && r.fires == 0 && tr.parry_t > 0 && tr.parry_t - 50 < 0.5,
          "contact off: build 9's parry in the wind-up at %.3f s", tr.parry_t - 50);
}

/* build 17: the drawn blade k_drawn, the hand's k_hand (-1 = none), the grip still, the drawn tip in the tracking space
 * moving tip_cm over the attack (a wrist turn) */
static struct crun contact_attack17(const somvr_df_cfg *c, somvr_df_track *tr, int k_drawn, int k_hand, int type, float tip_cm)
{
    struct crun r = { 0, 0, 0, -1 };
    somvr_df_track_init(tr);
    for (int f = 0;; f++) {
        float ts = (float)f / 90;
        if (ts > 0.72f)
            break;
        somvr_df_in in;
        memset(&in, 0, sizeof in);
        in.ts = ts, in.bpw = 0, in.epw = 0.66f, in.window = 1, in.type = type, in.impact = 0.70f;
        in.blade = our_blade(k_drawn, ts, in.base, in.tip);
        in.have_hand_blade = k_hand >= 0 && our_blade(k_hand, ts, in.hb, in.ht);
        orc_weapon(ts, in.ob[0], in.ot[0]);
        in.norc = 1;
        memcpy(in.hand, in.ob[0], sizeof in.hand), memcpy(in.orc, ORC, sizeof in.orc), memcpy(in.head, HEAD, sizeof in.head);
        in.grip[0] = 10, in.grip[1] = 120, in.grip[2] = 30, in.have_grip = 1;
        in.tip_xr[0] = 10, in.tip_xr[1] = 120 + tip_cm * ts / 0.72f, in.tip_xr[2] = 120, in.have_tip_xr = 1;
        double t = 50 + ts;
        if (somvr_df_step(c, tr, t, &in) == 2 && r.fires++ == 0)
            r.fire_ts = ts;
        r.t_end = t;
    }
    return r;
}
static void test_build17(void)
{
    somvr_df_cfg c;
    somvr_df_cfg_default(&c);
    c.contact = 1;
    unsigned b = 0;
    CHECK(c.red_types == 0xa && !c.contact_hand && !c.move_blade, "build 17 defaults: red parry,block; the drawn blade; grip");
    CHECK(somvr_df_red_parse("parry,counter,block", &b) && b == 0xe && somvr_df_red_parse(" none ", &b) && b == 0 &&
              somvr_df_red_parse("counter", &b) && b == 4 && !somvr_df_red_parse("parry,sword", &b) && b == 0 &&
              somvr_df_red_parse(NULL, &b) && b == 0,
          "the red types' words");
    CHECK(!somvr_df_is_red(&c, 0) && somvr_df_is_red(&c, 1) && !somvr_df_is_red(&c, 2) && somvr_df_is_red(&c, 3) &&
              !somvr_df_is_red(&c, 4) && !somvr_df_is_red(&c, -1),
          "red by type (defaults): 1 and 3; never ranged");
    c.red_types = 0xe;
    CHECK(somvr_df_is_red(&c, 2) && !somvr_df_can_block(&c, 2) && somvr_df_can_block(&c, 0), "red counter: type 2 dodged");
    c.red_types = 0;
    CHECK(somvr_df_can_block(&c, 1) && !somvr_df_can_block(&c, 3) && somvr_df_is_red(&c, 3),
          "no red bits: 1 blockable, 3 never");
    c.red_types = 0xe;
    somvr_df_track tr;
    struct crun r = contact_attack17(&c, &tr, 0, -1, 2, 0);
    CHECK(r.fires == 0 && somvr_df_verdict(&c, &tr, r.t_end, 2) == SOMVR_DFO_HIT, "red counter: the held guard does not block");
    c.red_types = 0xa;
    /* the drawn blade trails 50 cm over the swing (k 1), the hand's is in the path (k 0) */
    r = contact_attack17(&c, &tr, 1, 0, 0, 0);
    CHECK(r.fires == 0 && tr.best_cm > 25, "drawn only: the trailing blade misses (%.1f cm)", tr.best_cm);
    c.contact_hand = 1;
    r = contact_attack17(&c, &tr, 1, 0, 0, 0);
    CHECK(r.fires == 1 && tr.met_by == 2 && tr.drawn_best_cm > 25 && somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_BLOCK,
          "both: the hand's blade meets (by %d; the drawn %.1f cm): a block", tr.met_by, tr.drawn_best_cm);
    r = contact_attack17(&c, &tr, 0, 1, 0, 0);
    CHECK(r.fires == 1 && tr.met_by == 1, "both: the drawn blade in the path meets first (by %d)", tr.met_by);
    r = contact_attack17(&c, &tr, 1, -1, 0, 0);
    CHECK(r.fires == 0, "both, no hand blade read: the drawn blade alone");
    /* the move rule on the blade: the grip still, the tip turned 30 cm into the path */
    c.contact_move_cm = 15, c.contact_hand = 0;
    r = contact_attack17(&c, &tr, 0, -1, 0, 30);
    CHECK(r.fires == 0 && tr.still && tr.blade_cm < 1, "move from the grip: the wrist turn is a still guard (%.1f cm)", tr.blade_cm);
    c.move_blade = 1;
    r = contact_attack17(&c, &tr, 0, -1, 0, 30);
    CHECK(r.fires == 1 && tr.blade_cm > 15 && somvr_df_verdict(&c, &tr, r.t_end, 0) == SOMVR_DFO_BLOCK,
          "move from the blade: the tip moved %.1f cm: a block", tr.blade_cm);
    r = contact_attack17(&c, &tr, 0, -1, 0, 5);
    CHECK(r.fires == 0 && tr.still, "move from the blade: a still tip (5 cm) is still");
}

int main(void)
{
    test_types();
    test_guard();
    test_zone();
    test_step_verdict();
    test_dodge();
    test_two();
    test_arrow();
    test_haptic();
    test_clank();
    test_contact();
    test_build17();
    printf("test_defence: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
