/* test_loco.c - unit tests of somvr_loco.c: SOMVR_LOCO=1's core (docs/design-locomotion.md 3.1-3.2, LO-2) and
 * SOMVR_ANCHOR=root's anchor (3.4, LO-1), against a simulated body at 60-90 Hz. The core: the stick's bands, its world
 * velocity (head- or hand-relative is the heading the caller passes), the leg feed's inverse; who owns a tick (every
 * rule, its names); 1:1 (the stick's velocity in the very first tick, a stop in the same tick, a reversal in one, no
 * slide), the softening option, the cap, the read-back at a wall; the hand-offs (counts, the hand-off speed, a soft
 * resume starting from the body's speed, the stick ruling at once otherwise); the follow merge (one path per tick:
 * into our delta on our ticks, the extra move on the game's; the body moves by stick + follow exactly once); the body
 * yaw rule (the move's direction within the cone, the head behind it, the lazy standing turn with its hysteresis, the
 * rate limit, the game's yaw in fights / aiming / the rotation lock). The anchor: the calibrated eye height, horizontally
 * exact, the vertical low-pass (a step without a step, the ramp lag, no overshoot), exact falls and the landing's capped
 * dip with a continuous velocity, the velocity-continuous hand-offs in both directions (the offset's peak and decay),
 * the display-time extrapolation (a 60-on-72 Hz cadence: the view's positions uniform in display time, the raw root's
 * not; a stop bounded; no display time = none), teleports and gaps re-latched, the spring's closed form. */
#include "../somvr_loco.h"

#include <math.h>
#include <stddef.h>
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

static float hl(sv_vec3 v) { return sqrtf(v.x * v.x + v.z * v.z); }

/* ---- the stick ----------------------------------------------------------------------------------------------------- */
static void test_bands(void)
{
    somvr_loco_cfg c;
    somvr_loco_cfg_default(&c);
    /* P10's LR1: the stock Run plateau 222-232 cm/s (every stick 0.2-1.0), the FreeRun sprint 636-641; no walk band */
    CHECK(c.walk_cms == 150 && c.run_cms == 230 && c.sprint_cms == 640 && c.speed == 1 && c.stick_s == 0,
          "defaults (P10): walk %g run %g sprint %g", c.walk_cms, c.run_cms, c.sprint_cms);
    CHECK(NEAR(somvr_loco_band_speed(&c, 1, 0), 230, 1e-3) && NEAR(somvr_loco_band_speed(&c, 0.5f, 0), 150, 1e-3) &&
              NEAR(somvr_loco_band_speed(&c, 0.75f, 0), 190, 1e-3) && NEAR(somvr_loco_band_speed(&c, 1, 1), 640, 1e-3),
          "P10 bands: full %g, half %g, 0.75 %g, the latch's full %g", somvr_loco_band_speed(&c, 1, 0),
          somvr_loco_band_speed(&c, 0.5f, 0), somvr_loco_band_speed(&c, 0.75f, 0), somvr_loco_band_speed(&c, 1, 1));
    /* the full stick asks the stock Run's own speed (P10: 222-232), and the latch the stock sprint's (636-641), within 4 % */
    CHECK(fabs(somvr_loco_band_speed(&c, 1, 0) / 227.6 - 1) < 0.04 && fabs(somvr_loco_band_speed(&c, 1, 1) / 638.5 - 1) < 0.04,
          "P10: the bands match the stock clips");
    for (int i = 0; i <= 20; i++) { /* the P10 bands: the legs still get the stick itself back at scale 1 */
        float m = i / 20.0f;
        CHECK(NEAR(somvr_loco_leg_mag(&c, somvr_loco_band_speed(&c, m, 0), 0), m, 1e-5) &&
                  NEAR(somvr_loco_leg_mag(&c, somvr_loco_band_speed(&c, m, 1), 1), m, 1e-5),
              "P10 legs round trip at %g", m);
    }
    c.latch = SOMVR_LOCO_LATCH_GAME;
    CHECK(NEAR(somvr_loco_band_speed(&c, 1, 1), 230, 1e-3), "P10 latch=game: the run band");
    c.latch = SOMVR_LOCO_LATCH_OURS;
    /* the band math below on the pre-P10 bands (150 / 500 / 800: round numbers) */
    c.walk_cms = 150, c.run_cms = 500, c.sprint_cms = 800;
    CHECK(somvr_loco_band_speed(&c, 0, 0) == 0 && NEAR(somvr_loco_band_speed(&c, 0.25f, 0), 75, 1e-4) &&
              NEAR(somvr_loco_band_speed(&c, 0.5f, 0), 150, 1e-4) && NEAR(somvr_loco_band_speed(&c, 0.75f, 0), 325, 1e-3) &&
              NEAR(somvr_loco_band_speed(&c, 1, 0), 500, 1e-3) && NEAR(somvr_loco_band_speed(&c, 2, 0), 500, 1e-3),
          "bands: %g %g %g %g", somvr_loco_band_speed(&c, 0.25f, 0), somvr_loco_band_speed(&c, 0.5f, 0),
          somvr_loco_band_speed(&c, 0.75f, 0), somvr_loco_band_speed(&c, 1, 0));
    CHECK(NEAR(somvr_loco_band_speed(&c, 1, 1), 800, 1e-3) && NEAR(somvr_loco_band_speed(&c, 0.5f, 1), 150, 1e-3) &&
              somvr_loco_band_speed(&c, NAN, 0) == 0,
          "the latch's band: %g", somvr_loco_band_speed(&c, 1, 1));
    c.latch = SOMVR_LOCO_LATCH_GAME;
    CHECK(NEAR(somvr_loco_band_speed(&c, 1, 1), 500, 1e-3), "latch=game: the run band (the game carries the sprint)");
    c.latch = SOMVR_LOCO_LATCH_OURS;
    c.speed = 0.8f;
    CHECK(NEAR(somvr_loco_band_speed(&c, 1, 0), 400, 1e-3) && NEAR(somvr_loco_band_speed(&c, 0.5f, 0), 120, 1e-3),
          "SOMVR_LOCO_SPEED 0.8: %g", somvr_loco_band_speed(&c, 1, 0));
    /* the leg feed: the stock band of |v| (without the scale): at 0.8 the full stick's 400 cm/s plays 0.857 */
    CHECK(NEAR(somvr_loco_leg_mag(&c, 400, 0), 0.5 + 0.5 * 250 / 350.0, 1e-5) && NEAR(somvr_loco_leg_mag(&c, 120, 0), 0.4, 1e-5) &&
              somvr_loco_leg_mag(&c, 0, 0) == 0 && somvr_loco_leg_mag(&c, 900, 0) == 1 && somvr_loco_leg_mag(&c, NAN, 0) == 0,
          "legs: %g %g", somvr_loco_leg_mag(&c, 400, 0), somvr_loco_leg_mag(&c, 120, 0));
    c.speed = 1;
    for (int i = 0; i <= 20; i++) { /* at scale 1 the legs get the stick itself back */
        float m = i / 20.0f;
        CHECK(NEAR(somvr_loco_leg_mag(&c, somvr_loco_band_speed(&c, m, 0), 0), m, 1e-5) &&
                  NEAR(somvr_loco_leg_mag(&c, somvr_loco_band_speed(&c, m, 1), 1), m, 1e-5),
              "legs round trip at %g", m);
    }
}

static void test_stick_velocity(void)
{
    somvr_loco_cfg c;
    somvr_loco_cfg_default(&c);
    c.walk_cms = 150, c.run_cms = 500, c.sprint_cms = 800; /* round numbers for the geometry (the defaults: test_bands) */
    float f[2] = { 0, 1 }, r[2] = { 1, 0 }, d[2] = { 0.5f, 0.5f }, z[2] = { 0, 0 }, big[2] = { 0, 3 }, nan[2] = { NAN, 1 };
    sv_vec3 v = somvr_loco_stick_velocity(&c, f, 0, 0);
    CHECK(NEAR(v.x, 0, 1e-3) && v.y == 0 && NEAR(v.z, 500, 1e-3), "forward at heading 0: %g %g", v.x, v.z);
    v = somvr_loco_stick_velocity(&c, f, 90 * DEG, 0);
    CHECK(NEAR(v.x, 500, 1e-3) && NEAR(v.z, 0, 1e-3), "forward at heading 90 (the head turned right): %g %g", v.x, v.z);
    v = somvr_loco_stick_velocity(&c, r, 0, 0);
    CHECK(NEAR(v.x, 500, 1e-3) && NEAR(v.z, 0, 1e-3), "right at heading 0: %g %g", v.x, v.z);
    v = somvr_loco_stick_velocity(&c, r, 90 * DEG, 0);
    CHECK(NEAR(v.x, 0, 1e-3) && NEAR(v.z, -500, 1e-3), "right at heading 90: %g %g", v.x, v.z);
    v = somvr_loco_stick_velocity(&c, d, 0, 0); /* |s| 0.707: 150 + 0.414 x 350 = 295 along 45 deg */
    CHECK(NEAR(hl(v), 150 + (2 * sqrtf(0.5f) - 1) * 350, 1e-2) && NEAR(v.x, v.z, 1e-3), "diagonal: %g", hl(v));
    v = somvr_loco_stick_velocity(&c, big, 0, 0);
    CHECK(NEAR(v.z, 500, 1e-3), "clamped to 1: %g", v.z);
    v = somvr_loco_stick_velocity(&c, z, 0, 0);
    sv_vec3 n = somvr_loco_stick_velocity(&c, f, NAN, 0), m = somvr_loco_stick_velocity(&c, nan, 0, 0);
    CHECK(v.x == 0 && v.z == 0 && n.x == 0 && n.z == 0 && NEAR(m.z, 500, 1e-3) && m.x == 0, "rest / NaN heading / NaN x");
}

/* ---- ownership ------------------------------------------------------------------------------------------------------ */
static somvr_loco_own_in own_ok(void)
{
    somvr_loco_own_in in;
    memset(&in, 0, sizeof in);
    in.on = in.live = in.stereo = in.player = in.read_ok = 1;
    in.fsm = 6, in.rootmotion = 1, in.life = 2;
    return in;
}

static void test_why(void)
{
    somvr_loco_cfg c;
    somvr_loco_cfg_default(&c);
    somvr_loco_own_in in = own_ok();
    char b[256];
    CHECK(somvr_loco_why(&in, &c) == 0 && !strcmp(somvr_loco_why_str(0, b, sizeof b), "ours"), "all rules hold: ours");
    in.fsm = 7;
    CHECK(somvr_loco_why(&in, &c) == 0, "FSM 7 (combat between recoils) is ours");
    struct { const char *what; int off; int val; uint32_t want; } rules[] = {
        { "off", offsetof(somvr_loco_own_in, on), 0, SOMVR_LOCO_W_OFF },
        { "input", offsetof(somvr_loco_own_in, live), 0, SOMVR_LOCO_W_INPUT },
        { "stereo", offsetof(somvr_loco_own_in, stereo), 0, SOMVR_LOCO_W_STEREO },
        { "player", offsetof(somvr_loco_own_in, player), 0, SOMVR_LOCO_W_PLAYER },
        { "read", offsetof(somvr_loco_own_in, read_ok), 0, SOMVR_LOCO_W_READ },
        { "fsm 8", offsetof(somvr_loco_own_in, fsm), 8, SOMVR_LOCO_W_FSM },
        { "fsm 9", offsetof(somvr_loco_own_in, fsm), 9, SOMVR_LOCO_W_FSM },
        { "momentum", offsetof(somvr_loco_own_in, rootmotion), 0, SOMVR_LOCO_W_MOMENTUM },
        { "lock", offsetof(somvr_loco_own_in, lock_t), 1, SOMVR_LOCO_W_LOCK },
        { "attack", offsetof(somvr_loco_own_in, attack), 1, SOMVR_LOCO_W_ATTACK },
        { "combat move", offsetof(somvr_loco_own_in, combat_move), 1, SOMVR_LOCO_W_COMBATMOVE },
        { "mount", offsetof(somvr_loco_own_in, mounted), 1, SOMVR_LOCO_W_MOUNT },
        { "attach", offsetof(somvr_loco_own_in, attached), 1, SOMVR_LOCO_W_ATTACH },
        { "dying", offsetof(somvr_loco_own_in, life), 3, SOMVR_LOCO_W_DEAD },
        { "climb", offsetof(somvr_loco_own_in, climbing), 1, SOMVR_LOCO_W_CLIMB },
        { "struggle", offsetof(somvr_loco_own_in, struggle), 1, SOMVR_LOCO_W_STRUGGLE },
        { "sync", offsetof(somvr_loco_own_in, sync), 1, SOMVR_LOCO_W_SYNC },
        { "menu", offsetof(somvr_loco_own_in, menu), 1, SOMVR_LOCO_W_MENU },
        { "fallback", offsetof(somvr_loco_own_in, fallback), 1, SOMVR_LOCO_W_FALLBACK },
    };
    for (size_t i = 0; i < sizeof rules / sizeof rules[0]; i++) {
        somvr_loco_own_in x = own_ok();
        memcpy((char *)&x + rules[i].off, &rules[i].val, sizeof(int));
        uint32_t w = somvr_loco_why(&x, &c);
        CHECK(w == rules[i].want, "rule %s: why %#x want %#x", rules[i].what, w, rules[i].want);
    }
    somvr_loco_own_in x = own_ok();
    x.player = 0, x.fsm = 9, x.lock_t = 1; /* not the player's: its fields are not judged */
    CHECK(somvr_loco_why(&x, &c) == SOMVR_LOCO_W_PLAYER, "another PlayerMovement: only 'player'");
    x = own_ok();
    x.lock_r = 1, x.aiming = 1;
    CHECK(somvr_loco_why(&x, &c) == 0, "the rotation lock and the bow's aim keep the velocity ours");
    x.sprint = 1;
    CHECK(somvr_loco_why(&x, &c) == 0, "the sprint latch with SOMVR_LOCO_LATCH=ours: ours");
    c.latch = SOMVR_LOCO_LATCH_GAME;
    CHECK(somvr_loco_why(&x, &c) == SOMVR_LOCO_W_FREERUN, "latch=game: the stock free-run carries");
    c.latch = SOMVR_LOCO_LATCH_OURS;
    CHECK(!strcmp(somvr_loco_why_str(SOMVR_LOCO_W_FSM | SOMVR_LOCO_W_LOCK, b, sizeof b), "fsm+lock") &&
              !strcmp(somvr_loco_why_name(13), "climb") && !strcmp(somvr_loco_why_name(99), "?"),
          "names: %s", b);
    /* the yaw: the game's in a fight, while aiming, under the rotation lock, with SOMVR_LOCO_YAW=game */
    x = own_ok();
    CHECK(!somvr_loco_yaw_game(&x, &c), "yaw ours on the ground");
    x.fsm = 7;
    CHECK(somvr_loco_yaw_game(&x, &c), "yaw: a fight faces the engaged orc (the game's)");
    x = own_ok(), x.aiming = 1;
    CHECK(somvr_loco_yaw_game(&x, &c), "yaw: the bow's aim");
    x = own_ok(), x.lock_r = 1;
    CHECK(somvr_loco_yaw_game(&x, &c), "yaw: the rotation lock");
    x = own_ok(), c.yaw_mode = SOMVR_LOCO_YAW_GAME;
    CHECK(somvr_loco_yaw_game(&x, &c), "yaw: SOMVR_LOCO_YAW=game");
    /* the follow's route: one path per tick */
    CHECK(somvr_loco_follow_route(1, 1) == SOMVR_LOCO_FOLLOW_DELTA && somvr_loco_follow_route(0, 1) == SOMVR_LOCO_FOLLOW_EXTRA &&
              somvr_loco_follow_route(1, 0) == SOMVR_LOCO_FOLLOW_NONE && somvr_loco_follow_route(0, 0) == SOMVR_LOCO_FOLLOW_NONE,
          "follow routes");
}

/* ---- a simulated body: vf_11 applies ours or the game's root motion, then a wall at z = wall ----------------------- */
typedef struct {
    sv_vec3 root;
    float yaw;
    int wall;
    float wall_z;
    sv_vec3 game_v;       /* the game's root motion (cm/s) on its ticks */
    double moved_cm;
} body_t;
static somvr_loco g_s;
static somvr_loco_cfg g_c;
static void body_tick(body_t *b, float dt, uint32_t why, sv_vec3 stick_v, float head_yaw, somvr_loco_out *o,
                      const sv_vec3 *follow, int *extra)
{
    somvr_loco_in in;
    memset(&in, 0, sizeof in);
    in.dt = dt, in.why = why, in.stick_ok = 1, in.stick_v = stick_v, in.have_root = 1, in.root = b->root;
    in.yaw = b->yaw, in.head_yaw = head_yaw;
    somvr_loco_step(&g_s, &g_c, &in, o);
    int ex = 0;
    if (follow && !somvr_loco_take_follow(&g_s, o, *follow))
        ex = 1; /* the follow's own extra move (re-m1m2.md 3.2) on the game's ticks */
    if (extra)
        *extra = ex;
    sv_vec3 d = o->owned ? o->delta : (sv_vec3){ b->game_v.x * dt, 0, b->game_v.z * dt };
    if (ex)
        d.x += follow->x, d.z += follow->z;
    sv_vec3 r0 = b->root;
    b->root.x += d.x, b->root.z += d.z;
    if (b->wall && b->root.z > b->wall_z)
        b->root.z = b->wall_z;
    if (o->yaw_ours)
        b->yaw = sv_wrap_pi(b->yaw + o->yaw_step);
    b->moved_cm += sqrtf((b->root.x - r0.x) * (b->root.x - r0.x) + (b->root.z - r0.z) * (b->root.z - r0.z));
}
static void loco_reset(body_t *b)
{
    somvr_loco_init(&g_s);
    somvr_loco_cfg_default(&g_c);
    memset(b, 0, sizeof *b);
    b->root = (sv_vec3){ 500, 100, -50 };
}

static void test_one_to_one(void)
{
    body_t b;
    loco_reset(&b);
    somvr_loco_out o;
    const float dt = 1 / 60.0f;
    sv_vec3 full = { 0, 0, 500 }, zero = { 0, 0, 0 }, back = { 0, 0, -500 };
    for (int i = 0; i < 10; i++) /* at rest */
        body_tick(&b, dt, 0, zero, 0, &o, NULL, NULL);
    CHECK(o.owned && hl(o.v) == 0 && hl(o.delta) == 0 && b.moved_cm == 0, "at rest: nothing moves");
    /* the stick pushed: the run speed in the very first tick (no start clip) */
    sv_vec3 r0 = b.root;
    body_tick(&b, dt, 0, full, 0, &o, NULL, NULL);
    CHECK(NEAR(o.v.z, 500, 1e-3) && NEAR(o.delta.z, 500 * dt, 1e-4) && NEAR(b.root.z - r0.z, 500 * dt, 1e-4) && o.v.y == 0 &&
              o.delta.y == 0,
          "first tick: v %g delta %g moved %g", o.v.z, o.delta.z, b.root.z - r0.z);
    for (int i = 0; i < 59; i++) {
        body_tick(&b, dt, 0, full, 0, &o, NULL, NULL);
        CHECK(NEAR(o.v.z, 500, 1e-3), "running: v %g at tick %d", o.v.z, i);
        CHECK(o.have_applied && NEAR(o.applied.z, o.asked.z, 1e-3) && NEAR(o.asked.z, 500 * dt, 1e-4),
              "read-back = asked in the open: %g of %g", o.applied.z, o.asked.z);
    }
    CHECK(NEAR(b.root.z - r0.z, 500, 0.05), "1 s at full stick = 5 m: %g", b.root.z - r0.z);
    CHECK(NEAR(hl(o.v_hand), 500, 0.05), "the hand-off speed = the body's: %g", hl(o.v_hand));
    /* released: stopped in the same tick, no slide */
    sv_vec3 r1 = b.root;
    body_tick(&b, dt, 0, zero, 0, &o, NULL, NULL);
    CHECK(hl(o.v) == 0 && hl(o.delta) == 0 && b.root.z == r1.z, "the stop tick: v %g moved %g", hl(o.v), b.root.z - r1.z);
    for (int i = 0; i < 30; i++)
        body_tick(&b, dt, 0, zero, 0, &o, NULL, NULL);
    CHECK(b.root.z == r1.z, "no stop slide: %g cm after the release", b.root.z - r1.z);
    /* a reversal in one tick */
    body_tick(&b, dt, 0, full, 0, &o, NULL, NULL);
    body_tick(&b, dt, 0, back, 0, &o, NULL, NULL);
    CHECK(NEAR(o.v.z, -500, 1e-3) && NEAR(o.delta.z, -500 * dt, 1e-4), "reversal: %g", o.v.z);
    /* a half stick: walk speed at once; the bow's cap */
    body_tick(&b, dt, 0, (sv_vec3){ 0, 0, 150 }, 0, &o, NULL, NULL);
    CHECK(NEAR(o.v.z, 150, 1e-3), "walk band: %g", o.v.z);
    somvr_loco_in in;
    memset(&in, 0, sizeof in);
    in.dt = dt, in.stick_ok = 1, in.stick_v = full, in.cap_cms = 150, in.have_root = 1, in.root = b.root;
    somvr_loco_step(&g_s, &g_c, &in, &o);
    CHECK(NEAR(o.v.z, 150, 1e-3), "aim cap: %g", o.v.z);
    /* dt clamped (a hitch never throws him), the step clamped, NaN */
    in.cap_cms = 0, in.dt = 0.5f;
    somvr_loco_step(&g_s, &g_c, &in, &o);
    CHECK(NEAR(o.delta.z, 50, 1e-3), "dt clamp 0.1 s: %g", o.delta.z);
    in.stick_v = (sv_vec3){ 0, 0, 2000 }, in.dt = 0.05f;
    somvr_loco_step(&g_s, &g_c, &in, &o);
    CHECK(NEAR(o.delta.z, 60, 1e-3), "step clamp 60 cm: %g", o.delta.z);
    in.stick_v = (sv_vec3){ NAN, 0, 100 }, in.dt = NAN;
    somvr_loco_step(&g_s, &g_c, &in, &o);
    CHECK(isfinite(o.delta.x) && o.delta.x == 0 && o.delta.z == 0, "NaN: no move");
}

static void test_soft_and_wall(void)
{
    body_t b;
    loco_reset(&b);
    g_c.stick_s = 0.12f;
    somvr_loco_out o;
    const float dt = 1 / 90.0f;
    sv_vec3 full = { 0, 0, 500 };
    int n = (int)(0.12f / dt + 0.5f);
    for (int i = 0; i < n; i++)
        body_tick(&b, dt, 0, full, 0, &o, NULL, NULL);
    CHECK(NEAR(o.v.z, 500 * (1 - expf(-n * dt / 0.12f)), 0.5) && o.v.z > 300 && o.v.z < 330,
          "SOMVR_LOCO_STICK_MS 120: %g after %d ticks", o.v.z, n);
    /* a wall: the read-back has the collision's cut, the stick layer still asks its speed */
    loco_reset(&b);
    b.wall = 1, b.wall_z = -45;
    for (int i = 0; i < 6; i++)
        body_tick(&b, dt, 0, full, 0, &o, NULL, NULL);
    CHECK(b.root.z == -45 && o.have_applied && NEAR(o.applied.z, 0, 1e-4) && NEAR(o.asked.z, 500 * dt, 1e-4) &&
              NEAR(o.v.z, 500, 1e-3),
          "the wall: applied %g of %g, v %g", o.applied.z, o.asked.z, o.v.z);
    CHECK(g_s.applied_cm < g_s.asked_cm && g_s.asked_cm > 0, "the wall's totals: %g of %g", g_s.applied_cm, g_s.asked_cm);
}

static void test_handoffs(void)
{
    body_t b;
    loco_reset(&b);
    somvr_loco_out o;
    const float dt = 1 / 60.0f;
    sv_vec3 full = { 0, 0, 500 };
    for (int i = 0; i < 20; i++)
        body_tick(&b, dt, 0, full, 0, &o, NULL, NULL);
    /* ours -> the game's (a lock): the game's root motion passes, our velocity kept as the body's hand-off speed */
    b.game_v = (sv_vec3){ 0, 0, 700 };
    body_tick(&b, dt, SOMVR_LOCO_W_LOCK, full, 0, &o, NULL, NULL);
    CHECK(!o.owned && o.handoff == -1 && o.why_prev == 0 && o.why == SOMVR_LOCO_W_LOCK && hl(o.delta) == 0 &&
              NEAR(o.v_hand.z, 500, 0.05) && g_s.handoffs_out == 1,
          "out: handoff %d v_hand %g", o.handoff, o.v_hand.z);
    for (int i = 0; i < 10; i++)
        body_tick(&b, dt, SOMVR_LOCO_W_LOCK, full, 0, &o, NULL, NULL);
    CHECK(o.handoff == 0 && NEAR(o.v_hand.z, 700, 0.05), "the game carries at 700: v_hand %g", o.v_hand.z);
    /* back: the stick rules at once (instant mode) */
    body_tick(&b, dt, 0, full, 0, &o, NULL, NULL);
    CHECK(o.owned && o.handoff == 1 && NEAR(o.v.z, 500, 1e-3) && g_s.handoffs_in == 2, "in: handoff %d v %g (in %llu)",
          o.handoff, o.v.z, (unsigned long long)g_s.handoffs_in);
    /* the softening option: the resume starts at the body's own speed (velocity-continuous) */
    g_c.stick_s = 0.1f;
    b.game_v = (sv_vec3){ 0, 0, 700 };
    for (int i = 0; i < 5; i++)
        body_tick(&b, dt, SOMVR_LOCO_W_ATTACK, full, 0, &o, NULL, NULL);
    body_tick(&b, dt, 0, full, 0, &o, NULL, NULL);
    float k = 1 - expf(-dt / 0.1f);
    CHECK(o.handoff == 1 && NEAR(o.v.z, 700 + (500 - 700) * k, 0.05), "soft resume from 700: %g", o.v.z);
    /* the game stops him (a lock with no root motion): the hand-off speed goes to 0 */
    g_c.stick_s = 0;
    b.game_v = (sv_vec3){ 0, 0, 0 };
    body_tick(&b, dt, SOMVR_LOCO_W_LOCK, full, 0, &o, NULL, NULL);
    body_tick(&b, dt, SOMVR_LOCO_W_LOCK, full, 0, &o, NULL, NULL);
    CHECK(NEAR(hl(o.v_hand), 0, 1e-3), "stopped by the game: v_hand %g", hl(o.v_hand));
    /* the hook did not run after all: that tick's delta is not read back as ours */
    body_tick(&b, dt, 0, full, 0, &o, NULL, NULL);
    somvr_loco_unapplied(&g_s);
    CHECK(!g_s.owned_last && hl(g_s.asked_last) == 0, "unapplied");
}

static void test_follow_merge(void)
{
    body_t b;
    loco_reset(&b);
    somvr_loco_out o;
    const float dt = 1 / 60.0f;
    sv_vec3 stick = { 0, 0, 300 }, f = { 4, 0, 0 };
    /* 60 ticks: ours, with ten game ticks in the middle; the follow asks 4 cm right every tick */
    double want_x = 0, want_z = 0;
    sv_vec3 r0 = b.root;
    b.game_v = (sv_vec3){ 0, 0, 200 };
    int deltas = 0, extras = 0;
    for (int i = 0; i < 60; i++) {
        uint32_t why = i >= 25 && i < 35 ? SOMVR_LOCO_W_ATTACK : 0;
        int ex;
        body_tick(&b, dt, why, stick, 0, &o, &f, &ex);
        int route = somvr_loco_follow_route(o.owned, 1);
        CHECK(route == (o.owned ? SOMVR_LOCO_FOLLOW_DELTA : SOMVR_LOCO_FOLLOW_EXTRA) && o.follow_taken == o.owned &&
                  ex == !o.owned,
              "tick %d: one path (taken %d extra %d owned %d)", i, o.follow_taken, ex, o.owned);
        deltas += o.follow_taken, extras += ex;
        want_x += 4;
        want_z += (o.owned ? 300 : 200) * dt;
        if (o.owned)
            CHECK(NEAR(o.delta.x, 4, 1e-4) && NEAR(o.delta.z, 300 * dt, 1e-4) && NEAR(o.follow_d.x, 4, 1e-6),
                  "tick %d: delta = v dt + follow (%g %g)", i, o.delta.x, o.delta.z);
    }
    CHECK(deltas == 50 && extras == 10 && g_s.follow_merged == 50 && g_s.follow_extra == 10 && NEAR(g_s.follow_cm, 200, 1e-3),
          "routes: %d in our delta, %d extra", deltas, extras);
    CHECK(NEAR(b.root.x - r0.x, want_x, 1e-2) && NEAR(b.root.z - r0.z, want_z, 1e-2),
          "never a double move: moved %g,%g want %g,%g", b.root.x - r0.x, b.root.z - r0.z, want_x, want_z);
    /* a NaN request is never taken */
    somvr_loco_in in;
    memset(&in, 0, sizeof in);
    in.dt = dt, in.stick_ok = 1;
    somvr_loco_step(&g_s, &g_c, &in, &o);
    CHECK(!somvr_loco_take_follow(&g_s, &o, (sv_vec3){ NAN, 0, 1 }) && !o.follow_taken, "NaN follow");
    /* the follow's request counts in the read-back's ask (the wall check compares the whole move) */
    in.stick_v = stick, in.have_root = 1, in.root = b.root;
    somvr_loco_step(&g_s, &g_c, &in, &o);
    somvr_loco_take_follow(&g_s, &o, f);
    CHECK(NEAR(g_s.asked_last.x, 4, 1e-5) && NEAR(g_s.asked_last.z, 300 * dt, 1e-4), "asked includes the follow");
}

static void test_yaw(void)
{
    somvr_loco_cfg c;
    somvr_loco_cfg_default(&c);
    int turning = 0;
    float tgt, dt = 1 / 60.0f, lim = 720 * DEG * dt;
    /* moving 45 deg right of the head: face the move, at most 12 deg a tick */
    float s = somvr_loco_yaw(&c, 0, 0, (sv_vec3){ 353.6f, 0, 353.6f }, dt, &turning, &tgt);
    CHECK(NEAR(tgt, 45 * DEG, 1e-4) && NEAR(s, lim, 1e-5), "move at 45: target %g step %g", tgt / DEG, s / DEG);
    s = somvr_loco_yaw(&c, 40 * DEG, 0, (sv_vec3){ 353.6f, 0, 353.6f }, dt, &turning, &tgt);
    CHECK(NEAR(s, 5 * DEG, 1e-4), "the last 5 deg: %g", s / DEG);
    /* moving straight back (180 from the head): he faces the head */
    s = somvr_loco_yaw(&c, 30 * DEG, 0, (sv_vec3){ 0, 0, -500 }, dt, &turning, &tgt);
    CHECK(NEAR(tgt, 0, 1e-5) && NEAR(s, -lim, 1e-5), "moving back: target %g step %g", tgt / DEG, s / DEG);
    /* 99 deg from the head: still the move's; 101: the head's */
    s = somvr_loco_yaw(&c, 0, 0, (sv_vec3){ 500 * sinf(99 * DEG), 0, 500 * cosf(99 * DEG) }, dt, &turning, &tgt);
    CHECK(NEAR(tgt, 99 * DEG, 1e-4), "99 deg: %g", tgt / DEG);
    s = somvr_loco_yaw(&c, 0, 0, (sv_vec3){ 500 * sinf(101 * DEG), 0, 500 * cosf(101 * DEG) }, dt, &turning, &tgt);
    CHECK(NEAR(tgt, 0, 1e-5) && s == 0, "101 deg: the head's (%g)", tgt / DEG);
    /* standing: 40 deg off, no turn; 60 deg: turn until within 3 deg, then stop; the hysteresis */
    turning = 0;
    s = somvr_loco_yaw(&c, 0, 40 * DEG, (sv_vec3){ 0, 0, 0 }, dt, &turning, &tgt);
    CHECK(s == 0 && !turning, "standing 40 deg: lazy (no turn)");
    float yaw = 0;
    int ticks = 0;
    s = somvr_loco_yaw(&c, yaw, 60 * DEG, (sv_vec3){ 0, 0, 0 }, dt, &turning, &tgt);
    CHECK(turning && NEAR(s, lim, 1e-5), "standing 60 deg: turning");
    while (turning && ticks < 100) {
        yaw += s;
        s = somvr_loco_yaw(&c, yaw, 60 * DEG, (sv_vec3){ 0, 0, 0 }, dt, &turning, &tgt);
        ticks++;
    }
    CHECK(!turning && fabsf(sv_wrap_pi(60 * DEG - yaw)) <= 3 * DEG + 1e-4 && ticks <= 6, "turned to the head in %d ticks",
          ticks);
    s = somvr_loco_yaw(&c, 15 * DEG, 60 * DEG, (sv_vec3){ 0, 0, 0 }, dt, &turning, &tgt);
    CHECK(s == 0, "45 deg after the turn: within the hysteresis");
    s = somvr_loco_yaw(&c, 0, 179 * DEG, (sv_vec3){ 0, 0, 0 }, dt, &turning, &tgt);
    CHECK(NEAR(s, lim, 1e-5), "the short way round");
    s = somvr_loco_yaw(&c, NAN, 0, (sv_vec3){ 0, 0, 500 }, dt, &turning, &tgt);
    CHECK(s == 0, "NaN yaw: no step");
    /* in the core: ours on the ground, the game's (no step) when yaw_game */
    body_t b;
    loco_reset(&b);
    somvr_loco_out o;
    body_tick(&b, dt, 0, (sv_vec3){ 500, 0, 0 }, 0, &o, NULL, NULL);
    CHECK(o.yaw_ours && NEAR(o.yaw_step, lim, 1e-5) && NEAR(o.yaw_target, 90 * DEG, 1e-4), "core yaw: %g", o.yaw_step / DEG);
    for (int i = 0; i < 10; i++)
        body_tick(&b, dt, 0, (sv_vec3){ 500, 0, 0 }, 0, &o, NULL, NULL);
    CHECK(NEAR(b.yaw, 90 * DEG, 1e-4), "faced the move in 8 ticks: %g", b.yaw / DEG);
    somvr_loco_in in;
    memset(&in, 0, sizeof in);
    in.dt = dt, in.stick_ok = 1, in.stick_v = (sv_vec3){ 0, 0, 500 }, in.yaw = b.yaw, in.yaw_game = 1;
    somvr_loco_step(&g_s, &g_c, &in, &o);
    CHECK(o.owned && !o.yaw_ours && o.yaw_step == 0, "yaw_game: the game's rotation");
}

/* the yaw guard (SOMVR_LOCO_YAW_GUARD): a rail that holds the body's yaw at 0 between the ticks (build 9's wall-top bug:
 * 720 deg/s of our standing turn for 31 s) */
static int guard_ticks(int rail, sv_vec3 v, float head_yaw, int n, int *starts, float *yaw_end)
{
    body_t b;
    loco_reset(&b);
    somvr_loco_out o;
    const float dt = 1 / 90.0f;
    float post = 0;
    int ours = 0, have = 0;
    *starts = 0;
    for (int i = 0; i < n; i++) {
        somvr_loco_in in;
        memset(&in, 0, sizeof in);
        in.dt = dt, in.stick_ok = 1, in.stick_v = v, in.have_root = 1, in.root = b.root;
        in.yaw = b.yaw, in.head_yaw = head_yaw;
        in.have_btw = have, in.btw_yaw = sv_wrap_pi(b.yaw - post);
        somvr_loco_step(&g_s, &g_c, &in, &o);
        ours += o.yaw_ours && o.yaw_step != 0;
        *starts += o.guard_start;
        if (o.yaw_ours)
            b.yaw = sv_wrap_pi(b.yaw + o.yaw_step);
        post = b.yaw, have = 1; /* vf_11's transform: our step on */
        if (rail)
            b.yaw = 0; /* the rail turns him back before the next tick */
    }
    *yaw_end = b.yaw;
    return ours;
}

static void test_yaw_guard(void)
{
    somvr_loco_cfg c;
    somvr_loco_cfg_default(&c);
    CHECK(c.guard_ticks == 10 && c.guard_s == 1.0f, "guard defaults %d %g", c.guard_ticks, c.guard_s);
    int st;
    float ye;
    /* standing, the head 130 deg off, on the rail: 10 ticks of our turn, then 1 s the game's, then 10 again */
    int ours = guard_ticks(1, (sv_vec3){ 0, 0, 0 }, 130 * DEG, 180, &st, &ye);
    CHECK(st == 2 && ours >= 20 && ours <= 24 && g_s.guards == 2 && g_s.guard_ticks >= 150,
          "rail: %d of 180 ticks turned (guards %d, guard ticks %llu)", ours, st, (unsigned long long)g_s.guard_ticks);
    /* off the rail: the standing turn completes, no guard */
    ours = guard_ticks(0, (sv_vec3){ 0, 0, 0 }, 130 * DEG, 180, &st, &ye);
    CHECK(st == 0 && ours <= 17 && fabsf(sv_wrap_pi(ye - 130 * DEG)) <= 3 * DEG + 1e-4, "ground: turned in %d ticks", ours);
    /* walking (the stock turn may answer ours): no guard */
    ours = guard_ticks(1, (sv_vec3){ 500, 0, 0 }, 0, 180, &st, &ye);
    CHECK(st == 0 && ours == 180, "walking on the rail: no guard (%d)", ours);
    /* off */
    loco_reset(&(body_t){ 0 });
    g_c.guard_ticks = 0;
    body_t b;
    memset(&b, 0, sizeof b);
    somvr_loco_out o;
    int any = 0;
    for (int i = 0; i < 60; i++) {
        somvr_loco_in in;
        memset(&in, 0, sizeof in);
        in.dt = 1 / 90.0f, in.stick_ok = 1, in.have_root = 1, in.head_yaw = 130 * DEG, in.have_btw = i > 0;
        in.btw_yaw = i > 0 ? -o.yaw_step : 0;
        somvr_loco_step(&g_s, &g_c, &in, &o);
        any |= o.guard_start || o.yaw_guard;
    }
    CHECK(!any, "guard_ticks 0: never");
}

/* ---- the anchor ----------------------------------------------------------------------------------------------------- */
static somvr_anchor g_a;
static somvr_anchor_cfg g_ac;
static somvr_anchor_in ain(double t, sv_vec3 root)
{
    somvr_anchor_in in;
    memset(&in, 0, sizeof in);
    in.t = t, in.root = root, in.now_ns = (int64_t)(t * 1e9);
    return in;
}

static void test_anchor_basic(void)
{
    somvr_anchor_init(&g_a);
    somvr_anchor_cfg_default(&g_ac);
    g_ac.y_s = 0.08f; /* the numbers below are the 80 ms filter's (the default is 150 ms: test_anchor_y150) */
    {
        somvr_anchor_cfg d;
        somvr_anchor_cfg_default(&d);
        CHECK(NEAR(d.eye_cm, 76.6, 1e-4) && d.y_s == 0.15f && d.lead_s == 0, "anchor defaults (y %g s)", (double)d.y_s);
    }
    somvr_anchor_out o;
    sv_vec3 r = { 500, 100, -50 };
    somvr_anchor_in in = ain(10, r);
    somvr_anchor_step(&g_a, &g_ac, &in, &o);
    CHECK(o.latched == 1 && NEAR(o.pos.x, 500, 1e-4) && NEAR(o.pos.y, 176.6, 1e-4) && NEAR(o.pos.z, -50, 1e-4),
          "first frame: %g %g %g", o.pos.x, o.pos.y, o.pos.z);
    /* standing: the eye point, exactly; the eyes 180.6 cm above World_Ground (104 below the root) */
    for (int i = 1; i <= 30; i++) {
        in = ain(10 + i / 90.0, r);
        somvr_anchor_step(&g_a, &g_ac, &in, &o);
    }
    CHECK(!o.latched && NEAR(o.pos.y - (r.y - 104), 180.6, 1e-3) && NEAR(o.pos.x, 500, 1e-4) && hl(o.vel) < 1e-3,
          "standing: eyes %g above the ground", o.pos.y - (r.y - 104));
    /* walking 300 cm/s with no hand-off: horizontally exact (no lag, no smoothing of our motion) */
    for (int i = 31; i <= 120; i++) {
        r.z += 300 / 90.0f;
        in = ain(10 + i / 90.0, r);
        in.own = 1, in.v_own = (sv_vec3){ 0, 0, 300 };
        somvr_anchor_step(&g_a, &g_ac, &in, &o);
        CHECK(NEAR(o.pos.z, r.z, 1e-3) && NEAR(o.pos.x, r.x, 1e-3), "exact horizontally: %g vs %g", o.pos.z, r.z);
    }
    CHECK(NEAR(o.vel.z, 300, 0.5), "its velocity: %g", o.vel.z);
    /* a stop: exact at once (the player's stop, no overshoot) */
    in = ain(10 + 121 / 90.0, r);
    somvr_anchor_step(&g_a, &g_ac, &in, &o);
    CHECK(NEAR(o.pos.z, r.z, 1e-3) && NEAR(o.vel.z, 0, 1e-2), "stop: %g", o.vel.z);
    /* a teleport: re-latched, no smoothing */
    r.z += 300;
    in = ain(10 + 122 / 90.0, r);
    somvr_anchor_step(&g_a, &g_ac, &in, &o);
    CHECK(o.latched == 2 && NEAR(o.pos.z, r.z, 1e-3) && g_a.teleports == 1, "teleport: latched %d", o.latched);
    /* a gap (a pause, third person): re-latched */
    in = ain(12, r);
    somvr_anchor_step(&g_a, &g_ac, &in, &o);
    CHECK(o.latched == 1, "gap: latched %d", o.latched);
}

static void test_anchor_vertical(void)
{
    somvr_anchor_init(&g_a);
    somvr_anchor_cfg_default(&g_ac);
    g_ac.y_s = 0.08f; /* the numbers below are the 80 ms filter's (the default is 150 ms: test_anchor_y150) */
    somvr_anchor_out o;
    sv_vec3 r = { 0, 100, 0 };
    double t = 20;
    somvr_anchor_in in = ain(t, r);
    somvr_anchor_step(&g_a, &g_ac, &in, &o);
    for (int i = 0; i < 10; i++) {
        t += 1 / 90.0;
        in = ain(t, r);
        somvr_anchor_step(&g_a, &g_ac, &in, &o);
    }
    /* a 30 cm step up in one tick: no step in the view, a smooth rise without overshoot, velocity continuous */
    r.y += 30;
    float prev = o.pos.y, vprev = 0, maxdv = 0, maxv = 0;
    int mono = 1;
    for (int i = 0; i < 45; i++) {
        t += 1 / 90.0;
        in = ain(t, r);
        somvr_anchor_step(&g_a, &g_ac, &in, &o);
        mono &= o.pos.y >= prev - 1e-4f && o.pos.y <= 206.6f + 1e-3f;
        maxdv = fmaxf(maxdv, fabsf(o.vel.y - vprev));
        maxv = fmaxf(maxv, o.vel.y);
        if (i == 0)
            CHECK(o.pos.y - prev < 6, "step: the first frame rises only %g of 30 cm", o.pos.y - prev);
        prev = o.pos.y, vprev = o.vel.y;
    }
    CHECK(mono && NEAR(o.pos.y, 206.6, 0.5) && maxv < 30 * 37.5f * 0.2707f + 10, "step: monotonic, settled at %g (peak %g cm/s)",
          o.pos.y, maxv);
    /* stairs: a ramp at 100 cm/s up -> the ramp lag = y_s x v = 8 cm */
    for (int i = 0; i < 90; i++) {
        t += 1 / 90.0;
        r.y += 100 / 90.0f;
        in = ain(t, r);
        somvr_anchor_step(&g_a, &g_ac, &in, &o);
    }
    CHECK(NEAR(o.y_lag, -(8 - 100 / 90.0 / 2), 0.4) && NEAR(o.vel.y, 100, 2), "stairs: lag %g cm, %g cm/s", o.y_lag, o.vel.y);
    for (int i = 0; i < 60; i++) {
        t += 1 / 90.0;
        in = ain(t, r);
        somvr_anchor_step(&g_a, &g_ac, &in, &o);
    }
    /* a jump/fall (exact): the root goes up then falls 2 m under -2000 cm/s^2; the view exact after the blend */
    float vy = 400, y0 = r.y, worst = 0, maxjump = 0, vlast = o.vel.y;
    int n = 0;
    while (r.y > y0 - 200 || n < 5) {
        t += 1 / 90.0;
        vy -= 2000 / 90.0f;
        r.y += vy / 90.0f;
        in = ain(t, r);
        in.exact_y = 1;
        somvr_anchor_step(&g_a, &g_ac, &in, &o);
        if (n > 40)
            worst = fmaxf(worst, fabsf(o.pos.y - (r.y + 76.6f)));
        maxjump = fmaxf(maxjump, fabsf(o.vel.y - vlast));
        vlast = o.vel.y;
        n++;
    }
    CHECK(worst < 0.5f && o.exact_y, "the fall: exact after the blend (worst %g cm)", worst);
    CHECK(maxjump < 150, "the fall: velocity continuous (max change %g cm/s a frame)", maxjump);
    /* the landing at ~ -760 cm/s: the view dips below the eye height (capped at 30 cm), comes back, no velocity step */
    float ground = r.y, dip = 0;
    vlast = o.vel.y, maxjump = 0;
    for (int i = 0; i < 60; i++) {
        t += 1 / 90.0;
        in = ain(t, r);
        somvr_anchor_step(&g_a, &g_ac, &in, &o);
        dip = fminf(dip, o.pos.y - (ground + 76.6f));
        if (i < 1)
            maxjump = fmaxf(maxjump, fabsf(o.vel.y - vlast));
        vlast = o.vel.y;
    }
    CHECK(dip < -2 && dip >= -30.001f && NEAR(o.pos.y, ground + 76.6, 0.5) && maxjump < 100,
          "landing: dip %g cm, settled %g, the landing frame's velocity change %g", dip, o.pos.y - ground, maxjump);
    CHECK(g_a.y_switches >= 2, "switches %llu", (unsigned long long)g_a.y_switches);
}

static void test_anchor_handoff(void)
{
    somvr_anchor_init(&g_a);
    somvr_anchor_cfg_default(&g_ac);
    g_ac.y_s = 0.08f; /* the numbers below are the 80 ms filter's (the default is 150 ms: test_anchor_y150) */
    somvr_anchor_out o;
    sv_vec3 r = { 0, 100, 0 };
    double t = 30;
    const double dt = 1 / 90.0;
    somvr_anchor_in in = ain(t, r);
    somvr_anchor_step(&g_a, &g_ac, &in, &o);
    for (int i = 0; i < 30; i++) { /* ours at 500 cm/s */
        t += dt, r.z += 500 * (float)dt;
        in = ain(t, r);
        in.own = 1, in.v_own = (sv_vec3){ 0, 0, 500 };
        somvr_anchor_step(&g_a, &g_ac, &in, &o);
    }
    float v0 = o.vel.z;
    /* the game takes him (a lock): the root stops dead. The view: velocity-continuous, a small ease, back exact */
    float emax = 0, vjump = 0, vl = v0;
    for (int i = 0; i < 40; i++) {
        t += dt;
        in = ain(t, r);
        in.handoff = i == 0;
        somvr_anchor_step(&g_a, &g_ac, &in, &o);
        emax = fmaxf(emax, o.pos.z - r.z);
        vjump = fmaxf(vjump, fabsf(o.vel.z - vl));
        vl = o.vel.z;
        if (i == 0)
            CHECK(o.blended && NEAR(o.vel.z, v0, 100), "hand-off frame: %g cm/s (was %g)", o.vel.z, v0);
    }
    CHECK(vjump < 0.80f * 500 * 25 / 90 + 2 && emax > 10 && emax < 20 && NEAR(o.pos.z, r.z, 0.5),
          "ours -> the game's: max velocity change %g cm/s a frame (the 3rd order bound 0.8 dv w dt), ease %g cm, back to %g", vjump, emax,
          o.pos.z - r.z);
    for (int i = 0; i < 30; i++) {
        t += dt;
        in = ain(t, r);
        somvr_anchor_step(&g_a, &g_ac, &in, &o);
    }
    CHECK(NEAR(o.pos.z, r.z, 0.01), "exact again: %g", o.pos.z - r.z);
    /* the game's vault at 700 cm/s, then ours at 500 (the stick): eases from 700 to 500 */
    for (int i = 0; i < 20; i++) {
        t += dt, r.z += 700 * (float)dt;
        in = ain(t, r);
        somvr_anchor_step(&g_a, &g_ac, &in, &o);
    }
    vl = o.vel.z, vjump = 0;
    for (int i = 0; i < 40; i++) {
        t += dt, r.z += 500 * (float)dt;
        in = ain(t, r);
        in.own = 1, in.v_own = (sv_vec3){ 0, 0, 500 }, in.handoff = i == 0;
        somvr_anchor_step(&g_a, &g_ac, &in, &o);
        vjump = fmaxf(vjump, fabsf(o.vel.z - vl));
        vl = o.vel.z;
    }
    CHECK(vjump < 60 && NEAR(o.pos.z, r.z, 0.5) && NEAR(o.vel.z, 500, 5), "the game's -> ours: max change %g, now %g cm/s",
          vjump, o.vel.z);
    /* the same without the hand-off flag: the body's own velocity step reaches the view (only hand-offs are eased) */
    somvr_anchor_init(&g_a);
    in = ain(t, r);
    somvr_anchor_step(&g_a, &g_ac, &in, &o);
    for (int i = 0; i < 10; i++) {
        t += dt, r.z += 500 * (float)dt;
        in = ain(t, r);
        somvr_anchor_step(&g_a, &g_ac, &in, &o);
    }
    t += dt;
    in = ain(t, r);
    somvr_anchor_step(&g_a, &g_ac, &in, &o);
    CHECK(NEAR(o.pos.z, r.z, 1e-3) && !o.blended, "no hand-off: exact");
    CHECK(g_a.e_cm_max == 0 && g_a.blends == 0, "no offsets without a hand-off");
}

static void test_anchor_extrapolation(void)
{
    somvr_anchor_init(&g_a);
    somvr_anchor_cfg_default(&g_ac);
    g_ac.y_s = 0.08f; /* the numbers below are the 80 ms filter's (the default is 150 ms: test_anchor_y150) */
    somvr_anchor_out o;
    /* 60 fps game frames on a 72 Hz display: frames shown 2, 2, 2, 2, 3 periods apart ... (the 5:6 cadence); each frame's
     * root is at its sim time (60 fps), its display time on the 72 Hz grid. Ours at 500 cm/s */
    const double ft = 1 / 60.0, per = 1 / 72.0;
    sv_vec3 r = { 0, 100, 0 };
    double t = 40, disp = 40.05;
    int64_t base = 123456789000LL; /* the XrTime clock's own offset: never needed */
    float err_raw_min = 1e9f, err_raw_max = -1e9f, err_min = 1e9f, err_max = -1e9f;
    for (int i = 0; i < 400; i++) {
        t += ft, r.z += 500 * (float)ft;
        while (disp < t + 0.05 - 1e-9) /* the first display slot at least 50 ms after the sim time */
            disp += per;
        somvr_anchor_in in = ain(t, r);
        in.own = 1, in.v_own = (sv_vec3){ 0, 0, 500 };
        in.display_ns = base + (int64_t)(disp * 1e9);
        somvr_anchor_step(&g_a, &g_ac, &in, &o);
        if (i > 150) { /* after the mean settled: the view vs a uniform motion in display time */
            double raw = r.z - 500 * (disp - 40), view = o.pos.z - 500 * (disp - 40);
            err_raw_min = fminf(err_raw_min, (float)raw), err_raw_max = fmaxf(err_raw_max, (float)raw);
            err_min = fminf(err_min, (float)view), err_max = fmaxf(err_max, (float)view);
        }
    }
    CHECK(err_raw_max - err_raw_min > 5 && err_max - err_min < 0.6f,
          "60-on-72: the raw root's spread in display time %.2f cm, the view's %.2f cm", err_raw_max - err_raw_min,
          err_max - err_min);
    CHECK(g_a.jit_max > 0.005f && g_a.jit_min < -0.003f && g_a.x_cm_max < 10, "jitter %g..%g s, x max %g cm", g_a.jit_min,
          g_a.jit_max, g_a.x_cm_max);
    /* a stop: the extrapolation decays with the velocity's acceleration limit, never more than |v| x the jitter */
    sv_vec3 r1 = r;
    float worst = 0;
    for (int i = 0; i < 30; i++) {
        t += ft;
        while (disp < t + 0.05 - 1e-9)
            disp += per;
        somvr_anchor_in in = ain(t, r1);
        in.own = 1, in.display_ns = base + (int64_t)(disp * 1e9);
        somvr_anchor_step(&g_a, &g_ac, &in, &o);
        worst = fmaxf(worst, fabsf(o.pos.z - r1.z));
    }
    CHECK(worst <= 500 * 0.025f + 1e-3 && NEAR(o.pos.z, r1.z, 1e-3), "stop: at most %g cm off, then exact", worst);
    /* no display time: no extrapolation; a constant lead (the option) leads by v x lead */
    somvr_anchor_init(&g_a);
    g_ac.lead_s = 0.02f;
    for (int i = 0; i < 20; i++) {
        t += ft, r.z += 500 * (float)ft;
        somvr_anchor_in in = ain(t, r);
        in.own = 1, in.v_own = (sv_vec3){ 0, 0, 500 };
        somvr_anchor_step(&g_a, &g_ac, &in, &o);
    }
    CHECK(NEAR(o.pos.z - r.z, 10, 0.05) && NEAR(o.x.z, 10, 0.05), "lead 20 ms at 5 m/s: %g cm", o.pos.z - r.z);
    g_ac.lead_s = 0;
    somvr_anchor_init(&g_a);
    for (int i = 0; i < 20; i++) {
        t += ft, r.z += 500 * (float)ft;
        somvr_anchor_in in = ain(t, r);
        in.own = 1, in.v_own = (sv_vec3){ 0, 0, 500 };
        somvr_anchor_step(&g_a, &g_ac, &in, &o);
    }
    CHECK(o.pos.z == r.z && o.x.z == 0, "no display time: none");
    /* the game's frames: the measured velocity's median (a single follow step of 30 cm is not extrapolated) */
    somvr_anchor_init(&g_a);
    float xworst = 0;
    disp = t + 0.05;
    for (int i = 0; i < 200; i++) {
        t += ft;
        while (disp < t + 0.05 - 1e-9)
            disp += per;
        if (i == 150)
            r.x += 30; /* one follow step */
        somvr_anchor_in in = ain(t, r);
        in.display_ns = base + (int64_t)(disp * 1e9);
        somvr_anchor_step(&g_a, &g_ac, &in, &o);
        if (i >= 150)
            xworst = fmaxf(xworst, hl(o.x));
    }
    CHECK(xworst < 0.01f, "a single follow step is not extrapolated: %g cm", xworst);
    /* the clock jumps (a new session): re-latched, no jitter */
    somvr_anchor_in in = ain(t + ft, r);
    in.display_ns = base + 5000000000LL + (int64_t)(disp * 1e9);
    somvr_anchor_step(&g_a, &g_ac, &in, &o);
    CHECK(o.jitter_s == 0, "the clock jumped: jitter %g", o.jitter_s);
}

static void test_spring(void)
{
    /* the closed form: 100 steps of 1 ms = one of 100 ms; against a fine explicit integration of the ODE */
    float x = 10, v = 3, a = -40, w = 25;
    for (int i = 0; i < 100; i++)
        somvr_loco_spring3(&x, &v, &a, w, 0.001f);
    float x2 = 10, v2 = 3, a2 = -40;
    somvr_loco_spring3(&x2, &v2, &a2, w, 0.1f);
    double xe = 10, ve = 3, ae = -40;
    for (int i = 0; i < 100000; i++) { /* x''' = -3w x'' - 3w^2 x' - w^3 x */
        double j = -3 * w * ae - 3 * w * w * ve - (double)w * w * w * xe, h = 1e-6;
        xe += ve * h, ve += ae * h, ae += j * h;
    }
    CHECK(NEAR(x, x2, 1e-4) && NEAR(v, v2, 1e-2) && NEAR(a, a2, 0.5) && NEAR(x2, xe, 2e-3) && NEAR(v2, ve, 0.05) &&
              NEAR(a2, ae, 1),
          "spring3: %g %g %g vs %g %g %g", x2, v2, a2, xe, ve, ae);
    /* a velocity kick of 500: the peak 0.839 dv / w, the acceleration within 0.80 dv w, no overshoot back past 0 */
    float e = 0, ev = 500, ea = 0, peak = 0, amax = 0, low = 0;
    for (int i = 0; i < 1000; i++) {
        somvr_loco_spring3(&e, &ev, &ea, 25, 0.0005f);
        peak = fmaxf(peak, e), amax = fmaxf(amax, fabsf(ea)), low = fminf(low, e);
    }
    CHECK(NEAR(peak, 0.8394 * 500 / 25, 0.05) && amax <= 0.7995f * 500 * 25 + 5 && low >= -1e-4f && fabsf(e) < 0.05f,
          "kick: peak %g (want %g) |a| max %g (bound %g) low %g", peak, 0.8394 * 500 / 25, amax, 0.7995 * 500 * 25, low);
    /* a step of 30 (x 30 toward 0): monotonic, no overshoot */
    float s = 30, sv = 0, sa = 0, prev = 30;
    int mono = 1;
    for (int i = 0; i < 400; i++) {
        somvr_loco_spring3(&s, &sv, &sa, 37.5f, 0.001f);
        mono &= s <= prev + 1e-5f && s >= -1e-4f;
        prev = s;
    }
    CHECK(mono && s < 0.01f, "step: monotonic to %g", s);
    float n = NAN, m = 1, k = 2;
    somvr_loco_spring3(&n, &m, &k, 25, 0.01f);
    CHECK(n == 0 && m == 0 && k == 0, "NaN -> 0");
}

/* the default 150 ms vertical filter (build 6 comfort): a crest at a run (20 % up, then 20 % down at 230 cm/s) and a 3 cm
 * bump at 90 Hz: about half the 80 ms filter's peak vertical acceleration; the ramp lag y_s x v (6.9 cm at 46 cm/s) */
static double y_run(float y_s, double *lag_max)
{
    somvr_anchor a;
    somvr_anchor_cfg c;
    somvr_anchor_init(&a);
    somvr_anchor_cfg_default(&c);
    c.y_s = y_s;
    sv_vec3 r = { 0, 100, 0 };
    double t = 50, amax = 0, vprev = 0;
    *lag_max = 0;
    for (int i = 0; i < 6 * 90; i++) {
        float sl = i < 90 ? 0 : i < 270 ? 0.2f : i < 450 ? -0.2f : 0;
        t += 1 / 90.0;
        r.x += 230 / 90.0f, r.y += sl * 230 / 90.0f + (i == 500 ? 3 : 0);
        somvr_anchor_in in = ain(t, r);
        somvr_anchor_out o;
        somvr_anchor_step(&a, &c, &in, &o);
        if (i > 2)
            amax = fmax(amax, fabs((o.vel.y - vprev) * 90));
        vprev = o.vel.y;
        *lag_max = fmax(*lag_max, fabs(o.y_lag));
    }
    return amax;
}
static void test_anchor_y150(void)
{
    double lag80, lag150, a80 = y_run(0.08f, &lag80), a150 = y_run(0.15f, &lag150);
    CHECK(a150 < 0.6 * a80 && a150 < 600 && lag150 < 7.5 && lag150 > lag80,
          "150 ms: peak vertical accel %.0f cm/s^2 (80 ms: %.0f), lag max %.1f cm (80 ms: %.1f)", a150, a80, lag150, lag80);
}

int main(void)
{
    test_anchor_y150();
    test_bands();
    test_stick_velocity();
    test_why();
    test_one_to_one();
    test_soft_and_wall();
    test_handoffs();
    test_follow_merge();
    test_yaw();
    test_yaw_guard();
    test_anchor_basic();
    test_anchor_vertical();
    test_anchor_handoff();
    test_anchor_extrapolation();
    test_spring();
    printf("test_loco: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
