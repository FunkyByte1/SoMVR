/* test_step.c - unit tests of somvr_step.c: the assisted step, the dash and the wraith phase (docs/re-xr-pipeline.md
 * 23; design-locomotion.md 3.7-3.8). The raised-cosine profile (ends at rest, the peak 2 D / T, the distance exact at any
 * tick rate, the acceleration bounded); the step's pick (off, no orc, in reach, blocking, the stick away, the gaze's and
 * the tip's cone, the size and the stop distance, the strength, the best orc, NaN); the evade's pick (the phase by the
 * stick or the gaze, its length, the range and the cone, the dash along the stick in a fight, none with the phase off
 * at an orc); the runner (the step clamped by an approaching orc, a game tick ends it; the dash's and the phase's owner
 * mask with the stock evade's hold, held / hold over, the stick's forward part taken over and its sideways part kept,
 * blocked, lost; the phase's peak at the orc's axis and the re-face, the yaw, the look, the blink); LOCO's merge
 * (somvr_loco_take_assist); the owner-bit names; and a whole phase through the LOCO core and the root anchor at 72 Hz
 * (the body moves D exactly, the view's velocity continuous). */
#include "../somvr_loco.h"
#include "../somvr_step.h"

#include <math.h>
#include <stdio.h>
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
#define PI 3.14159265358979

static float hl(sv_vec3 v) { return sqrtf(v.x * v.x + v.z * v.z); }

/* ---- the profile --------------------------------------------------------------------------------------------------- */
static void test_profile(void)
{
    float D = 50, T = 0.18f;
    CHECK(somvr_step_pos(D, T, 0) == 0 && NEAR(somvr_step_pos(D, T, T), D, 1e-4) && NEAR(somvr_step_pos(D, T, T / 2), D / 2, 1e-4),
          "pos at 0, T/2, T: %g %g %g", somvr_step_pos(D, T, 0), somvr_step_pos(D, T, T / 2), somvr_step_pos(D, T, T));
    CHECK(somvr_step_pos(D, T, -1) == 0 && NEAR(somvr_step_pos(D, T, 5), D, 1e-4), "clamped outside");
    CHECK(somvr_step_vel(D, T, 0) == 0 && NEAR(somvr_step_vel(D, T, T), 0, 1e-3) && NEAR(somvr_step_vel(D, T, T / 2), 2 * D / T, 1e-2),
          "vel: 0 at the ends, the peak 2D/T = %g: %g", 2 * D / T, somvr_step_vel(D, T, T / 2));
    CHECK(NEAR(2 * 50 / 0.18, 555.6, 0.1), "the design's 0.5 m in 0.18 s peaks at 5.6 m/s (not 4.4)");
    CHECK(somvr_step_vel(D, T, -0.01f) == 0 && somvr_step_vel(D, T, T + 0.01f) == 0 && somvr_step_vel(D, 0, 0) == 0,
          "no velocity outside");
    CHECK(somvr_step_pos(D, 0, 0) == D && somvr_step_pos(D, 0, -1) == 0, "T 0 = at once");
    CHECK(somvr_step_pos(NAN, T, 0.1f) == 0 && somvr_step_pos(D, T, NAN) == 0 && somvr_step_vel(D, T, NAN) == 0, "NaN");
    /* the sum of the ticks is D at any rate; monotonic; the speed between ticks within the profile's acceleration */
    static const double rates[] = { 60, 72, 90, 120, 37 };
    for (int k = 0; k < 5; k++) {
        double dt = 1 / rates[k], s = 0, vprev = 0, amax = 0;
        int mono = 1;
        float t0 = 0;
        for (int i = 0; t0 < T + 0.05f; i++) {
            float t1 = (float)((i + 1) * dt);
            double ds = somvr_step_pos(D, T, t1) - somvr_step_pos(D, T, t0);
            mono &= ds >= -1e-6;
            s += ds;
            double v = ds / dt;
            amax = fmax(amax, fabs(v - vprev) / dt);
            vprev = v, t0 = t1;
        }
        CHECK(NEAR(s, D, 1e-3) && mono, "%g Hz: the sum %g of %g, monotonic %d", rates[k], s, D, mono);
        CHECK(amax <= 2 * PI * D / (T * T) * 1.05 + 1, "%g Hz: the acceleration %g within 2 pi D / T^2 = %g", rates[k], amax,
              2 * PI * D / (T * T));
    }
    /* irregular ticks (a hitch of 0.05 s in the middle) */
    float ts[] = { 0, 0.011f, 0.025f, 0.075f, 0.09f, 0.13f, 0.2f };
    double s = 0;
    for (int i = 1; i < 7; i++)
        s += somvr_step_pos(D, T, ts[i]) - somvr_step_pos(D, T, ts[i - 1]);
    CHECK(NEAR(s, D, 1e-3), "irregular ticks: %g", s);
}

/* ---- the step's pick ----------------------------------------------------------------------------------------------- */
static somvr_step_orc orc_at(float x, float z, uintptr_t key) { return (somvr_step_orc){ { x, 100, z }, key }; }
static somvr_step_swing_in swing_in(const somvr_step_orc *o, int n)
{
    somvr_step_swing_in in;
    memset(&in, 0, sizeof in);
    in.root = (sv_vec3){ 0, 100, 0 }, in.head_yaw = 0;
    in.tip = (sv_vec3){ 60, 150, 50 }, in.tip_v = (sv_vec3){ -600, 0, 0 }; /* a slash right to left in front */
    in.orc = o, in.n = n;
    return in;
}

static void test_swing_pick(void)
{
    somvr_step_cfg c;
    somvr_step_cfg_default(&c);
    CHECK(c.step == 0 && c.dash == SOMVR_ST_DASH_OFF && c.phase == SOMVR_ST_PHASE_OFF, "all off by default");
    somvr_step_orc o[4] = { orc_at(0, 170, 1) };
    somvr_step_swing_in in = swing_in(o, 1);
    somvr_step_pick p;
    CHECK(somvr_step_swing_pick(&c, &in, &p) == SOMVR_STW_OFF && p.d_cm == 0 && p.target == -1, "off: %d", p.why);
    c.step = 1;
    CHECK(somvr_step_swing_pick(&c, &in, &p) == SOMVR_STW_OK && p.target == 0 && NEAR(p.d_cm, 50, 1e-4) && NEAR(p.dist_cm, 170, 1e-3) &&
              NEAR(p.dir.z, 1, 1e-5),
          "an orc 1.7 m ahead: the full 50 cm: %d %g", p.why, p.d_cm);
    o[0] = orc_at(0, 150, 1);
    CHECK(somvr_step_swing_pick(&c, &in, &p) == SOMVR_STW_OK && NEAR(p.d_cm, 30, 1e-4), "1.5 m: 30 cm (to the reach): %g", p.d_cm);
    o[0] = orc_at(0, 119, 1);
    CHECK(somvr_step_swing_pick(&c, &in, &p) == SOMVR_STW_IN_REACH && p.target == 0 && p.d_cm == 0, "in reach: %d", p.why);
    o[0] = orc_at(0, 201, 1);
    CHECK(somvr_step_swing_pick(&c, &in, &p) == SOMVR_STW_NO_ORC, "beyond reach + 80: %d", p.why);
    o[0] = orc_at(0, 124, 1);
    CHECK(somvr_step_swing_pick(&c, &in, &p) == SOMVR_STW_SHORT, "4 cm: too short: %d %g", p.why, p.d_cm);
    c.step = 0.5f, o[0] = orc_at(0, 190, 1);
    CHECK(somvr_step_swing_pick(&c, &in, &p) == SOMVR_STW_OK && NEAR(p.d_cm, 25, 1e-4), "strength 0.5: 25 cm: %g", p.d_cm);
    c.step = 1;
    /* the stop distance: a closer reach setting may not take him within 70 cm of the axis */
    c.reach_cm = 60, o[0] = orc_at(0, 100, 1);
    CHECK(somvr_step_swing_pick(&c, &in, &p) == SOMVR_STW_OK && NEAR(p.d_cm, 30, 1e-4), "the stop distance: %g", p.d_cm);
    c.reach_cm = 120;
    /* the gaze's cone: 34 deg yes, 36 deg no (the tip's travel elsewhere) */
    in.tip_v = (sv_vec3){ 0, 0, -600 }; /* travelling back: no tip cone */
    float r = 170;
    o[0] = orc_at(r * sinf(34 * DEG), r * cosf(34 * DEG), 1);
    CHECK(somvr_step_swing_pick(&c, &in, &p) == SOMVR_STW_OK, "34 deg off the gaze: %d", p.why);
    o[0] = orc_at(r * sinf(36 * DEG), r * cosf(36 * DEG), 1);
    CHECK(somvr_step_swing_pick(&c, &in, &p) == SOMVR_STW_NO_ORC, "36 deg off the gaze: %d", p.why);
    /* ... but the tip travelling at it (a backhand toward the right) picks it */
    in.tip = (sv_vec3){ 0, 150, 60 }, in.tip_v = (sv_vec3){ 500, 0, 300 };
    CHECK(somvr_step_swing_pick(&c, &in, &p) == SOMVR_STW_OK && p.cos_tip > c.cone_cos && p.cos_gaze < c.cone_cos,
          "the tip's travel: %d cos tip %g gaze %g", p.why, p.cos_tip, p.cos_gaze);
    in = swing_in(o, 1);
    o[0] = orc_at(0, 170, 1);
    /* blocking; the stick away; the stick sideways is fine; the stick at rest is fine */
    in.blocking = 1;
    CHECK(somvr_step_swing_pick(&c, &in, &p) == SOMVR_STW_BLOCK, "blocking: %d", p.why);
    in.blocking = 0;
    in.stick_v = (sv_vec3){ 0, 0, -300 };
    CHECK(somvr_step_swing_pick(&c, &in, &p) == SOMVR_STW_STICK_AWAY && p.target == 0, "the stick back: %d", p.why);
    in.stick_v = (sv_vec3){ 300, 0, 0 };
    CHECK(somvr_step_swing_pick(&c, &in, &p) == SOMVR_STW_OK, "the stick sideways: %d", p.why);
    in.stick_v = (sv_vec3){ 0, 0, -20 };
    CHECK(somvr_step_swing_pick(&c, &in, &p) == SOMVR_STW_OK, "a stick below 30 cm/s points nowhere: %d", p.why);
    in.stick_v = (sv_vec3){ 0, 0, 0 };
    /* two orcs: the better cosine wins, then the nearer */
    o[0] = orc_at(60, 160, 1), o[1] = orc_at(0, 190, 2);
    in.n = 2;
    CHECK(somvr_step_swing_pick(&c, &in, &p) == SOMVR_STW_OK && p.target == 1, "the one looked at: %d", p.target);
    o[0] = orc_at(0, 150, 1), o[1] = orc_at(0, 190, 2);
    CHECK(somvr_step_swing_pick(&c, &in, &p) == SOMVR_STW_OK && p.target == 0 && NEAR(p.d_cm, 30, 1e-4), "the nearer: %d",
          p.target);
    /* NaN: an orc skipped, a bad root refused */
    o[0] = orc_at(NAN, 150, 1);
    CHECK(somvr_step_swing_pick(&c, &in, &p) == SOMVR_STW_OK && p.target == 1, "a NaN orc skipped: %d", p.target);
    in.root.x = NAN;
    CHECK(somvr_step_swing_pick(&c, &in, &p) == SOMVR_STW_BAD, "a NaN root: %d", p.why);
    CHECK(!strcmp(somvr_step_why_name(SOMVR_STW_IN_REACH), "in reach") && !strcmp(somvr_step_why_name(99), "?"), "names");
}

/* ---- the evade's pick ---------------------------------------------------------------------------------------------- */
static void test_evade_pick(void)
{
    somvr_step_cfg c;
    somvr_step_cfg_default(&c);
    somvr_step_orc o[3] = { orc_at(0, 200, 7) };
    somvr_step_evade_in in;
    memset(&in, 0, sizeof in);
    in.root = (sv_vec3){ 0, 100, 0 }, in.head_yaw = 0, in.fight = 1, in.orc = o, in.n = 1;
    somvr_step_evade e;
    CHECK(somvr_step_evade_pick(&c, &in, &e) == SOMVR_STW_OFF && e.kind == SOMVR_ST_NONE, "both off: %d", e.why);
    c.phase = SOMVR_ST_PHASE_ON;
    /* the stick at rest: the gaze asks; an orc 2 m ahead -> through it to 1.1 m beyond */
    CHECK(somvr_step_evade_pick(&c, &in, &e) == SOMVR_STW_OK && e.kind == SOMVR_ST_PHASE && e.target == 0 && NEAR(e.D, 310, 1e-3) &&
              NEAR(e.T, 0.4, 1e-6) && NEAR(e.land.z, 310, 1e-3) && NEAR(e.land.y, 100, 1e-6) && NEAR(e.dist_cm, 200, 1e-3),
          "the phase by the gaze: %d D %g", e.kind, e.D);
    in.fight = 0;
    CHECK(somvr_step_evade_pick(&c, &in, &e) == SOMVR_STW_NO_FIGHT && e.kind == SOMVR_ST_NONE && e.target == 0,
          "no phase out of a fight (the stock jump): %d", e.why);
    in.fight = 1;
    /* the path through the orc's axis even when it is off the gaze within the cone */
    o[0] = orc_at(100, 200, 7);
    CHECK(somvr_step_evade_pick(&c, &in, &e) == SOMVR_STW_OK && e.kind == SOMVR_ST_PHASE &&
              NEAR(e.dir.x, 100 / sqrt(50000.0), 1e-5) && NEAR(e.D, sqrt(50000.0) + 110, 1e-3),
          "through the axis: dir %g,%g D %g", e.dir.x, e.dir.z, e.D);
    /* the length's clamps */
    o[0] = orc_at(0, 20, 7);
    CHECK(somvr_step_evade_pick(&c, &in, &e) == SOMVR_STW_OK && NEAR(e.D, 150, 1e-3), "at least 150: %g", e.D);
    c.phase_behind_cm = 300;
    o[0] = orc_at(0, 290, 7);
    CHECK(somvr_step_evade_pick(&c, &in, &e) == SOMVR_STW_OK && NEAR(e.D, 450, 1e-3), "at most 450: %g", e.D);
    c.phase_behind_cm = 110;
    o[0] = orc_at(0, 301, 7);
    CHECK(somvr_step_evade_pick(&c, &in, &e) == SOMVR_STW_OFF && e.kind == SOMVR_ST_NONE, "out of range, dash off: %d", e.why);
    /* the stick decides the direction: right at an orc on the right; the gaze's orc ahead ignored */
    o[0] = orc_at(0, 200, 7), o[1] = orc_at(250, 0, 8);
    in.n = 2;
    in.stick_dir = (sv_vec3){ 1, 0, 0 }, in.stick_mag = 0.8f;
    CHECK(somvr_step_evade_pick(&c, &in, &e) == SOMVR_STW_OK && e.kind == SOMVR_ST_PHASE && e.target == 1 && NEAR(e.dir.x, 1, 1e-5),
          "the stick's orc: %d", e.target);
    /* a light stick (below 0.5): the gaze */
    in.stick_mag = 0.3f;
    CHECK(somvr_step_evade_pick(&c, &in, &e) == SOMVR_STW_OK && e.target == 0, "a light stick: the gaze's orc: %d", e.target);
    /* the stick away from every orc: the dash (when on) in a fight */
    in.stick_dir = (sv_vec3){ -1, 0, 0 }, in.stick_mag = 1;
    CHECK(somvr_step_evade_pick(&c, &in, &e) == SOMVR_STW_OFF && e.kind == SOMVR_ST_NONE, "the dash off: %d", e.why);
    c.dash = SOMVR_ST_DASH_ON;
    CHECK(somvr_step_evade_pick(&c, &in, &e) == SOMVR_STW_OK && e.kind == SOMVR_ST_DASH && NEAR(e.dir.x, -1, 1e-5) &&
              NEAR(e.D, 200, 1e-4) && NEAR(e.T, 0.3, 1e-6) && NEAR(e.land.x, -200, 1e-3),
          "the dash left: %d D %g", e.kind, e.D);
    in.fight = 0;
    CHECK(somvr_step_evade_pick(&c, &in, &e) == SOMVR_STW_NO_FIGHT, "no dash outside a fight: %d", e.why);
    in.fight = 1, in.stick_mag = 0.4f;
    in.n = 0;
    CHECK(somvr_step_evade_pick(&c, &in, &e) == SOMVR_STW_NO_STICK, "a light stick, nothing ahead: no dash: %d", e.why);
    /* the phase off and the stick at an orc: no dash into it (the stock A) */
    in.n = 2, c.phase = SOMVR_ST_PHASE_OFF, in.stick_dir = (sv_vec3){ 0, 0, 1 }, in.stick_mag = 1;
    CHECK(somvr_step_evade_pick(&c, &in, &e) == SOMVR_STW_OFF && e.kind == SOMVR_ST_NONE && e.target == 0, "no dash into an orc: %d",
          e.kind);
    in.root.z = NAN;
    CHECK(somvr_step_evade_pick(&c, &in, &e) == SOMVR_STW_BAD, "NaN: %d", e.why);
}

/* ---- the runner ---------------------------------------------------------------------------------------------------- */
typedef struct { somvr_step_run r; sv_vec3 root; } sim;
static somvr_step_tick_out sim_tick(sim *s, const somvr_step_cfg *c, float dt, int owned, int attack, sv_vec3 stick_d, float body_k,
                                    const sv_vec3 *orc)
{
    somvr_step_tick_in in;
    memset(&in, 0, sizeof in);
    in.dt = dt, in.owned = owned, in.attack = attack, in.have_root = 1, in.root = s->root, in.stick_d = stick_d;
    if (orc)
        in.have_orc = 1, in.orc = *orc;
    somvr_step_tick_out o;
    somvr_step_tick(&s->r, c, &in, &o);
    if (owned) { /* the body: the stick's delta + ours, times body_k (1 = free, 0 = blocked) */
        s->root.x += (stick_d.x + o.d.x) * body_k, s->root.z += (stick_d.z + o.d.z) * body_k;
    }
    return o;
}

static void test_runner_step(void)
{
    somvr_step_cfg c;
    somvr_step_cfg_default(&c);
    c.step = 1;
    sim s;
    memset(&s, 0, sizeof s);
    somvr_step_run_init(&s.r);
    sv_vec3 z = { 0, 0, 0 }, orc = { 0, 100, 170 };
    CHECK(somvr_step_mask(&s.r, &c, SOMVR_LOCO_W_ATTACK, 1) == 0 && !somvr_step_yaw_ours(&s.r, &c, 0), "idle: no mask");
    somvr_step_tick_out o = sim_tick(&s, &c, 1 / 90.0f, 1, 0, z, 1, &orc);
    CHECK(!o.active && !o.ended, "idle: nothing");
    somvr_step_start(&s.r, &c, SOMVR_ST_STEP, 0, 0x1234, 50, c.step_s, (sv_vec3){ 0, 0, 1 }, s.root, 170);
    CHECK(s.r.kind == SOMVR_ST_STEP && s.r.n[SOMVR_ST_STEP] == 1 && somvr_step_mask(&s.r, &c, SOMVR_LOCO_W_ATTACK, 1) == 0,
          "the step overrides no owner");
    float vmax = 0, vlast = 0;
    int ticks = 0, ended = 0;
    for (; ticks < 40 && !ended; ticks++) {
        o = sim_tick(&s, &c, 1 / 90.0f, 1, 0, z, 1, &orc);
        float v = hl(o.d) * 90;
        vmax = fmaxf(vmax, v), vlast = v;
        ended = o.ended;
    }
    CHECK(ended == SOMVR_STE_DONE && NEAR(s.root.z, 50, 1e-3) && ticks == 17 && vlast < 40 && NEAR(vmax, 555.6, 15),
          "90 Hz: done after %d ticks, moved %g, v max %g (5.6 m/s), the last tick %g cm/s", ticks, s.root.z, vmax, vlast);
    CHECK(s.r.kind == SOMVR_ST_NONE && s.r.ends[SOMVR_STE_DONE] == 1 && NEAR(s.r.cm[SOMVR_ST_STEP], 50, 1e-3), "the totals");
    /* the orc steps in meanwhile: the step stops 70 cm from its axis */
    s.root = z;
    somvr_step_start(&s.r, &c, SOMVR_ST_STEP, 0, 0x1234, 50, c.step_s, (sv_vec3){ 0, 0, 1 }, s.root, 170);
    ended = 0;
    int clamped = 0;
    for (int i = 0; i < 40 && !ended; i++) {
        orc.z -= 3; /* 2.7 m/s toward Talion */
        float gap = orc.z - s.root.z;
        o = sim_tick(&s, &c, 1 / 90.0f, 1, 0, z, 1, &orc);
        clamped |= o.clamped;
        ended = o.ended;
        CHECK(o.d.z <= fmaxf(gap - 70, 0) + 1e-4, "the step never takes him within 70 cm: %g of the gap %g", o.d.z, gap);
    }
    CHECK(clamped && (ended == SOMVR_STE_CLOSE || ended == SOMVR_STE_DONE) && s.root.z < 50, "clamped: end %d at %g", ended, s.root.z);
    /* the stick toward the orc counts against the stop distance */
    s.root = z, orc = (sv_vec3){ 0, 100, 170 };
    somvr_step_start(&s.r, &c, SOMVR_ST_STEP, 0, 0x1234, 50, c.step_s, (sv_vec3){ 0, 0, 1 }, s.root, 170);
    ended = 0;
    for (int i = 0; i < 40 && !ended; i++) {
        float gap = orc.z - s.root.z;
        o = sim_tick(&s, &c, 1 / 90.0f, 1, 0, (sv_vec3){ 0, 0, 500 / 90.0f }, 1, &orc);
        ended = o.ended;
        CHECK(o.d.z <= fmaxf(gap - 70 - 500 / 90.0f, 0) + 1e-4, "the stick's part counted: the step %g of the gap %g", o.d.z, gap);
    }
    CHECK(ended, "ended %d", ended);
    /* a game tick ends the step (a stagger: the game's) */
    s.root = z, orc = (sv_vec3){ 0, 100, 170 };
    somvr_step_start(&s.r, &c, SOMVR_ST_STEP, 0, 0x1234, 50, c.step_s, (sv_vec3){ 0, 0, 1 }, s.root, 170);
    sim_tick(&s, &c, 1 / 90.0f, 1, 0, z, 1, &orc);
    o = sim_tick(&s, &c, 1 / 90.0f, 0, 0, z, 1, &orc);
    CHECK(o.ended == SOMVR_STE_LOST && s.r.kind == SOMVR_ST_NONE, "lost: %d", o.ended);
    /* stop: the off switch */
    somvr_step_start(&s.r, &c, SOMVR_ST_STEP, 0, 0x1234, 50, c.step_s, (sv_vec3){ 0, 0, 1 }, s.root, 170);
    CHECK(somvr_step_stop(&s.r, SOMVR_STE_OFF) == SOMVR_ST_STEP && s.r.kind == SOMVR_ST_NONE && s.r.ends[SOMVR_STE_OFF] == 1, "stop");
    /* refused starts */
    somvr_step_start(&s.r, &c, SOMVR_ST_STEP, 0, 0, 0, c.step_s, (sv_vec3){ 0, 0, 1 }, s.root, 0);
    somvr_step_start(&s.r, &c, SOMVR_ST_STEP, 0, 0, 10, c.step_s, (sv_vec3){ 0, 0, 0 }, s.root, 0);
    somvr_step_start(&s.r, &c, 7, 0, 0, 10, c.step_s, (sv_vec3){ 0, 0, 1 }, s.root, 0);
    CHECK(s.r.kind == SOMVR_ST_NONE, "no move from D 0, no direction, a bad kind");
    CHECK(!strcmp(somvr_step_kind_name(SOMVR_ST_PHASE), "phase") && !strcmp(somvr_step_end_name(SOMVR_STE_DONE), "done"), "names");
}

static void test_runner_dash(void)
{
    somvr_step_cfg c;
    somvr_step_cfg_default(&c);
    c.dash = SOMVR_ST_DASH_ON;
    sim s;
    memset(&s, 0, sizeof s);
    somvr_step_run_init(&s.r);
    sv_vec3 z = { 0, 0, 0 };
    uint32_t own = SOMVR_LOCO_W_ATTACK;
    somvr_step_start(&s.r, &c, SOMVR_ST_DASH, -1, 0, 200, 0.3f, (sv_vec3){ -1, 0, 0 }, s.root, 0);
    CHECK(somvr_step_mask(&s.r, &c, own, 0) == own && !somvr_step_yaw_ours(&s.r, &c, 1), "the dash owns the roll's ticks; the yaw "
          "stays as LOCO has it");
    /* the stock roll (an attack) from the second tick to 0.5 s; the stick held left at the run speed: its part along the
     * path is the dash's, so the dash is 200 cm whatever the stick does; the stick forward meanwhile steers (+z) */
    int ended = 0, ticks = 0;
    float t = 0;
    for (; ticks < 200 && !ended; ticks++) {
        int attack = ticks >= 1 && t < 0.5f;
        if (!(somvr_step_mask(&s.r, &c, own, attack) & own) && attack)
            break; /* the game would own: not expected while the hold runs */
        sv_vec3 st = { -500 / 72.0f, 0, 100 / 72.0f };
        somvr_step_tick_out o = sim_tick(&s, &c, 1 / 72.0f, 1, attack, st, 1, NULL);
        t += 1 / 72.0f;
        ended = o.ended;
    }
    CHECK(ended == SOMVR_STE_HELD && t >= 0.5f && t < 0.52f, "held to the roll's end: %d at %g s", ended, t);
    CHECK(NEAR(s.root.x, -200 - 500 * (t - 0.3), 6) && NEAR(s.root.z, 100 * t, 0.5),
          "the dash 200 cm along, the stick after it; sideways the stick's own: %g, %g (t %g)", s.root.x, s.root.z, t);
    CHECK(s.r.kind == SOMVR_ST_NONE && somvr_step_mask(&s.r, &c, own, 1) == 0, "over: no mask");
    /* the roll outlasting the hold: hold over, then the game's */
    s.root = z;
    somvr_step_start(&s.r, &c, SOMVR_ST_DASH, -1, 0, 200, 0.3f, (sv_vec3){ -1, 0, 0 }, s.root, 0);
    ended = 0;
    t = 0;
    for (int i = 0; i < 200 && !ended; i++) {
        somvr_step_tick_out o = sim_tick(&s, &c, 1 / 72.0f, 1, 1, z, 1, NULL);
        t += 1 / 72.0f;
        ended = o.ended;
    }
    CHECK(ended == SOMVR_STE_HOLD_OVER && NEAR(t, 0.9, 0.02) && NEAR(s.root.x, -200, 1e-2), "hold over at %g s: %d, %g", t, ended,
          s.root.x);
    /* no stock roll at all: done at T */
    s.root = z;
    somvr_step_start(&s.r, &c, SOMVR_ST_DASH, -1, 0, 200, 0.3f, (sv_vec3){ -1, 0, 0 }, s.root, 0);
    ended = 0;
    t = 0;
    for (int i = 0; i < 200 && !ended; i++) {
        somvr_step_tick_out o = sim_tick(&s, &c, 1 / 72.0f, 1, 0, z, 1, NULL);
        t += 1 / 72.0f;
        ended = o.ended;
    }
    CHECK(ended == SOMVR_STE_DONE && NEAR(t, 0.3, 0.015) && NEAR(s.root.x, -200, 1e-2), "no roll: done at %g: %d", t, ended);
    /* a wall: the body does not move -> blocked after 15 cm asked */
    s.root = z;
    somvr_step_start(&s.r, &c, SOMVR_ST_DASH, -1, 0, 200, 0.3f, (sv_vec3){ -1, 0, 0 }, s.root, 0);
    ended = 0;
    float asked = 0;
    for (int i = 0; i < 200 && !ended; i++) {
        somvr_step_tick_out o = sim_tick(&s, &c, 1 / 72.0f, 1, 1, z, 0, NULL);
        asked += hl(o.d);
        ended = o.ended;
    }
    CHECK(ended == SOMVR_STE_BLOCKED && asked >= 15 && asked < 40, "blocked after %g cm asked: %d", asked, ended);
    /* the game owning its ticks (a combat move): lost after 0.1 s; one game tick does not end it */
    s.root = z;
    somvr_step_start(&s.r, &c, SOMVR_ST_DASH, -1, 0, 200, 0.3f, (sv_vec3){ -1, 0, 0 }, s.root, 0);
    somvr_step_tick_out o = sim_tick(&s, &c, 1 / 72.0f, 0, 1, z, 1, NULL);
    CHECK(!o.ended && s.r.kind == SOMVR_ST_DASH && s.r.game_ticks == 1, "one game tick: on");
    for (int i = 0; i < 4 && !o.ended; i++)
        o = sim_tick(&s, &c, 1 / 72.0f, 1, 1, z, 1, NULL);
    for (int i = 0; i < 10 && !o.ended; i++)
        o = sim_tick(&s, &c, 1 / 72.0f, 0, 1, z, 1, NULL);
    CHECK(o.ended == SOMVR_STE_LOST && s.r.game_ticks >= 8, "lost after 0.1 s of the game's: %d (%llu)", o.ended,
          (unsigned long long)s.r.game_ticks);
    /* the blink option: black within 50 ms, back over 80 ms after the motion */
    c.dash = SOMVR_ST_DASH_BLINK;
    somvr_step_start(&s.r, &c, SOMVR_ST_DASH, -1, 0, 200, 0.3f, (sv_vec3){ -1, 0, 0 }, s.root, 0);
    CHECK(somvr_step_blink(&s.r, &c) == 0, "the blink at 0: %g", somvr_step_blink(&s.r, &c));
    s.r.t = 0.025f;
    CHECK(NEAR(somvr_step_blink(&s.r, &c), 0.5, 1e-4), "half black at 25 ms: %g", somvr_step_blink(&s.r, &c));
    s.r.t = 0.2f;
    CHECK(somvr_step_blink(&s.r, &c) == 1 && !somvr_step_look(&s.r), "black in the motion, no wraith look for the dash");
    s.r.t = 0.34f;
    CHECK(NEAR(somvr_step_blink(&s.r, &c), 0.5, 1e-3), "half way back: %g", somvr_step_blink(&s.r, &c));
    s.r.t = 0.4f;
    CHECK(somvr_step_blink(&s.r, &c) == 0, "clear");
    c.dash = SOMVR_ST_DASH_ON;
    s.r.t = 0.2f;
    CHECK(somvr_step_blink(&s.r, &c) == 0, "no blink with dash=on");
    somvr_step_stop(&s.r, SOMVR_STE_OFF);
}

static void test_runner_phase(void)
{
    somvr_step_cfg c;
    somvr_step_cfg_default(&c);
    c.phase = SOMVR_ST_PHASE_REFACE;
    sim s;
    memset(&s, 0, sizeof s);
    somvr_step_run_init(&s.r);
    sv_vec3 z = { 0, 0, 0 };
    uint32_t own = SOMVR_LOCO_W_ATTACK | SOMVR_LOCO_W_FSM;
    somvr_step_start(&s.r, &c, SOMVR_ST_PHASE, 0, 0x99, 310, 0.4f, (sv_vec3){ 0, 0, 2 }, s.root, 200);
    CHECK(s.r.dir.z == 1 && somvr_step_mask(&s.r, &c, own, 0) == own && somvr_step_yaw_ours(&s.r, &c, 0) && somvr_step_look(&s.r),
          "the phase: the mask, the yaw ours, the look");
    int peaks = 0, peak_tick = -1, ended = 0, i = 0;
    float vprev = 0, dvmax = 0;
    for (; i < 200 && !ended; i++) {
        somvr_step_tick_out o = sim_tick(&s, &c, 1 / 72.0f, 1, i < 40, z, 1, NULL);
        float v = o.d.z * 72;
        dvmax = fmaxf(dvmax, fabsf(v - vprev));
        vprev = v;
        if (o.peak)
            peaks++, peak_tick = i;
        ended = o.ended;
    }
    CHECK(peaks == 1 && s.r.reface && ended == SOMVR_STE_HELD && NEAR(s.root.z, 310, 1e-2),
          "one peak (tick %d) with the re-face due, held, through to %g", peak_tick, s.root.z);
    CHECK(dvmax <= 2 * PI * 310 / (0.4 * 0.4) / 72 * 1.05, "the velocity changes at most %g cm/s a tick: %g",
          2 * PI * 310 / (0.16) / 72, dvmax);
    /* the peak's tick: where the profile passes 200 cm (u ~ 0.59 of 0.4 s = 17 ticks at 72 Hz) */
    CHECK(peak_tick >= 15 && peak_tick <= 18, "the peak at tick %d", peak_tick);
    c.phase = SOMVR_ST_PHASE_ON;
    somvr_step_start(&s.r, &c, SOMVR_ST_PHASE, 0, 0x99, 310, 0.4f, (sv_vec3){ 0, 0, 1 }, s.root, 200);
    for (i = 0; i < 40; i++)
        sim_tick(&s, &c, 1 / 72.0f, 1, 1, z, 1, NULL);
    CHECK(!s.r.reface && s.r.peaked, "phase=on: no re-face");
    CHECK(!somvr_step_look(&s.r) && somvr_step_yaw_ours(&s.r, &c, 1) && !somvr_step_yaw_ours(&s.r, &c, 0),
          "after the motion: no look; the yaw ours while the vault holds");
    CHECK(somvr_step_blink(&s.r, &c) == 0, "no blink with phase=on");
    somvr_step_stop(&s.r, SOMVR_STE_OFF);
}

/* ---- LOCO's merge and the names ------------------------------------------------------------------------------------ */
static void test_merge(void)
{
    somvr_loco_cfg lc;
    somvr_loco_cfg_default(&lc);
    somvr_loco s;
    somvr_loco_init(&s);
    somvr_loco_in in;
    memset(&in, 0, sizeof in);
    in.dt = 1 / 60.0f, in.stick_ok = 1, in.stick_v = (sv_vec3){ 0, 0, 300 }, in.have_root = 1, in.yaw_game = 1;
    somvr_loco_out o;
    somvr_loco_step(&s, &lc, &in, &o);
    CHECK(o.owned && NEAR(o.delta.z, 5, 1e-4), "the stick's 5 cm");
    CHECK(somvr_loco_take_assist(&s, &o, (sv_vec3){ 2, 7, 10 }, in.dt) == 1 && NEAR(o.delta.x, 2, 1e-5) && NEAR(o.delta.z, 15, 1e-4) &&
              o.delta.y == 0 && NEAR(o.v.z, 900, 1e-2) && NEAR(o.v.x, 120, 1e-2),
          "merged: delta %g,%g,%g v %g,%g", o.delta.x, o.delta.y, o.delta.z, o.v.x, o.v.z);
    CHECK(somvr_loco_take_assist(&s, &o, (sv_vec3){ NAN, 0, 1 }, in.dt) == 0 && NEAR(o.delta.z, 15, 1e-4), "NaN refused");
    in.root = (sv_vec3){ 2, 0, 15 };
    somvr_loco_step(&s, &lc, &in, &o);
    CHECK(o.have_applied && NEAR(o.asked.z, 15, 1e-4) && NEAR(o.asked.x, 2, 1e-5), "the read-back's asked has it: %g", o.asked.z);
    in.why = SOMVR_LOCO_W_ATTACK;
    somvr_loco_step(&s, &lc, &in, &o);
    CHECK(!o.owned && somvr_loco_take_assist(&s, &o, (sv_vec3){ 0, 0, 10 }, in.dt) == 0 && o.delta.z == 0, "the game's tick: nothing");
    int bad = 0;
    CHECK(somvr_step_own_bits("attack", &bad) == SOMVR_LOCO_W_ATTACK && !bad, "attack");
    CHECK(somvr_step_own_bits("attack,fsm+combatmove", &bad) == (SOMVR_LOCO_W_ATTACK | SOMVR_LOCO_W_FSM | SOMVR_LOCO_W_COMBATMOVE) &&
              !bad,
          "three");
    CHECK(somvr_step_own_bits("attack,bogus", &bad) == SOMVR_LOCO_W_ATTACK && bad == 1, "a bad word counted");
    CHECK(somvr_step_own_bits("", &bad) == 0 && somvr_step_own_bits(NULL, NULL) == 0, "none");
}

/* ---- a whole phase through the LOCO core and the anchor at 72 Hz ------------------------------------------------ */
static void test_phase_through_loco(void)
{
    somvr_loco_cfg lc;
    somvr_loco_cfg_default(&lc);
    somvr_anchor_cfg ac;
    somvr_anchor_cfg_default(&ac);
    somvr_step_cfg c;
    somvr_step_cfg_default(&c);
    c.phase = SOMVR_ST_PHASE_ON;
    somvr_loco s;
    somvr_loco_init(&s);
    somvr_anchor a;
    somvr_anchor_init(&a);
    somvr_step_run r;
    somvr_step_run_init(&r);
    sv_vec3 root = { 0, 100, 0 };
    double t = 0;
    float dt = 1 / 72.0f, vprev = 0, dvmax = 0, vmax = 0;
    somvr_step_orc orc = orc_at(0, 200, 0x55);
    int started = 0, attack_from = 2, attack_to = 50, ended = 0;
    for (int i = 0; i < 160; i++, t += dt) {
        if (i == 10) { /* the A press: the pick, then the move */
            somvr_step_evade_in ei;
            memset(&ei, 0, sizeof ei);
            ei.root = root, ei.head_yaw = 0, ei.fight = 1, ei.orc = &orc, ei.n = 1;
            somvr_step_evade e;
            CHECK(somvr_step_evade_pick(&c, &ei, &e) == SOMVR_STW_OK && e.kind == SOMVR_ST_PHASE, "the pick");
            somvr_step_start(&r, &c, e.kind, e.target, orc.key, e.D, e.T, e.dir, root, e.dist_cm);
            started = i;
        }
        int attack = started && i >= started + attack_from && i < started + attack_to;
        somvr_loco_in in;
        memset(&in, 0, sizeof in);
        in.dt = dt, in.stick_ok = 1, in.have_root = 1, in.root = root, in.yaw_game = 1;
        in.why = (attack ? SOMVR_LOCO_W_ATTACK : 0) & ~somvr_step_mask(&r, &c, SOMVR_LOCO_W_ATTACK, attack);
        somvr_loco_out o;
        somvr_loco_step(&s, &lc, &in, &o);
        somvr_step_tick_in ti;
        memset(&ti, 0, sizeof ti);
        ti.dt = dt, ti.owned = o.owned, ti.attack = attack, ti.have_root = 1, ti.root = root, ti.stick_d = o.delta;
        somvr_step_tick_out so;
        somvr_step_tick(&r, &c, &ti, &so);
        if (so.active)
            somvr_loco_take_assist(&s, &o, so.d, dt);
        if (so.ended)
            ended = so.ended;
        if (o.owned)
            root.x += o.delta.x, root.z += o.delta.z;
        else
            root.z += 3; /* the stock vault's own root motion (not expected while ours) */
        somvr_anchor_in ain;
        memset(&ain, 0, sizeof ain);
        ain.t = t, ain.root = root, ain.own = o.owned, ain.v_own = o.v;
        somvr_anchor_out ao;
        somvr_anchor_step(&a, &ac, &ain, &ao);
        if (i > 2) {
            dvmax = fmaxf(dvmax, fabsf(ao.vel.z - vprev));
            vmax = fmaxf(vmax, ao.vel.z);
        }
        vprev = ao.vel.z;
    }
    CHECK(ended == SOMVR_STE_HELD && NEAR(root.z, 310, 1e-2) && NEAR(root.x, 0, 1e-4) && s.handoffs_out == 0,
          "the body through to %g (end %d), no hand-off to the vault's root motion (%llu)", root.z, ended,
          (unsigned long long)s.handoffs_out);
    CHECK(NEAR(vmax, 2 * 310 / 0.4, 15) && dvmax < 2 * PI * 310 / 0.16 / 72 * 1.1,
          "the view: the peak %g cm/s (2D/T %g), the largest change a frame %g", vmax, 2 * 310 / 0.4, dvmax);
}

int main(void)
{
    test_profile();
    test_swing_pick();
    test_evade_pick();
    test_runner_step();
    test_runner_dash();
    test_runner_phase();
    test_merge();
    test_phase_through_loco();
    printf("test_step: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
