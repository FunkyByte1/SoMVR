/* test_climb.c - unit tests of somvr_climb.c (SOMVR_CLIMB=b, hand-over-hand climbing) against a mock world: a wall
 * face at z = 100 (y 0..300, x -500..500, CanWallClimb for x < 200, plain rock beyond), its top at y = 300 (z 100..200),
 * the player on the -z side. The pieces (the grip hysteresis, the tracking -> world rotation, the clamp), the grab test
 * (palm and finger rays, reach, a hand inside the wall, the window, out of reach = a miss, rock = not climbable, the
 * game not allowing it, the rays counted), the delta integration (straight pulls, the rig's yaw, sideways and away, the
 * per-frame clamp and the carry, the carry cut, a tracking jump, NaN), hand-over-hand (the newest hold drives, a
 * switch with no lost or doubled motion), release (both open, one open, lost tracking), stand-downs (the game, the
 * input, the timeout, from outside) and re-arming, the freeze (a menu: nothing moves, the clocks stop), the edge and the
 * top (an edge hold and its top height, a hold far below the top, release / lift / lift stalled / lift out of time /
 * cmd / cmd taken over by the game), and a whole climb of the wall at 90 Hz. */
#include "../somvr_climb.h"

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
#define NEAR3(v, X, Y, Z, e) (NEAR((v).x, X, e) && NEAR((v).y, Y, e) && NEAR((v).z, Z, e))
static int len3_ok(sv_vec3 v) { /* finite and within one step (8 cm) */
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z) && sqrtf(v.x * v.x + v.y * v.y + v.z * v.z) <= 8.0001f;
}

/* ---- the mock world -------------------------------------------------------------------------------------------- */
#define WALL_Z 100.0f
#define WALL_TOP 300.0f
#define ROCK_X 200.0f
static int g_rays, g_ray_off, g_no_top, g_ground_on; /* g_ground_on: a floor at y 0 (rays down: the hang's ground test) */
static float g_wall_top = WALL_TOP;

static int mock_ray(void *ctx, int hand, sv_vec3 from, sv_vec3 dir, float len, somvr_climb_hit *out) {
    (void)ctx, (void)hand;
    g_rays++;
    if (g_ray_off) return 0;
    float best = 1e30f;
    memset(out, 0, sizeof *out);
    if (dir.z > 1e-6f && from.z <= WALL_Z) { /* the face, from the player's side */
        float t = (WALL_Z - from.z) / dir.z;
        sv_vec3 p = { from.x + dir.x * t, from.y + dir.y * t, WALL_Z };
        if (t >= 0 && t <= len && p.y >= 0 && p.y <= g_wall_top && p.x > -500 && p.x < 500 && t < best)
            best = t, out->hit = 1, out->climbable = p.x < ROCK_X, out->dist = t, out->point = p;
    }
    if (g_ground_on && dir.y < -1e-6f && from.y >= 0 && from.z < WALL_Z) { /* the floor before the wall */
        float t = -from.y / dir.y;
        if (t >= 0 && t <= len && t < best)
            best = t, out->hit = 1, out->climbable = 1, out->dist = t,
            out->point = (sv_vec3){ from.x + dir.x * t, 0, from.z + dir.z * t };
    }
    if (dir.y < -1e-6f && from.y >= g_wall_top && !g_no_top) { /* the top, from above */
        float t = (g_wall_top - from.y) / dir.y;
        sv_vec3 p = { from.x + dir.x * t, g_wall_top, from.z + dir.z * t };
        if (t >= 0 && t <= len && p.z >= WALL_Z && p.z <= WALL_Z + 100 && t < best)
            best = t, out->hit = 1, out->climbable = 1, out->dist = t, out->point = p;
    }
    return 1;
}

/* ---- the player: the rig maps tracking space to the world by the yaw and the body's offset; the body moves by what
 * the climb asks (the "game" applies every request a frame later, like the PM hook) ---------------------------------- */
typedef struct {
    somvr_climb s;
    somvr_climb_cfg c;
    somvr_climb_in in;
    somvr_climb_out o;
    sv_vec3 body;        /* the world offset of the tracking space (moves with Talion) */
    float yaw;
    float scale;         /* the player size (0 = 1): world = body + s R(yaw) p */
    sv_vec3 pending;     /* the last request, applied at the next frame */
    int apply;           /* 1: requests move the body */
    sv_vec3 moved;       /* the sum of the applied moves */
    float head_track_y;  /* the head in tracking space */
    float head_track_z;  /* ... its z (50: 50 cm from the wall at body z 0) */
    int ledge;           /* Talion's capsule at the wall: the root (body + (0, 100, 50)) stays 35 cm off the face (z 65)
                          * while the capsule's foot (100 cm under the root) is more than 40 cm under the top */
    double t;
} rig_t;

static sv_vec3 track_to_world(const rig_t *r, sv_vec3 p) {
    float k = r->scale > 0 ? r->scale : 1.0f;
    sv_vec3 w = somvr_climb_to_world((sv_vec3){ k * p.x, k * p.y, k * p.z }, r->yaw);
    return (sv_vec3){ w.x + r->body.x, w.y + r->body.y, w.z + r->body.z };
}

static void rig_init(rig_t *r) {
    memset(r, 0, sizeof *r);
    somvr_climb_init(&r->s);
    somvr_climb_cfg_default(&r->c);
    r->c.letgo = SOMVR_CLIMB_LETGO_FALL; /* the tests before build 10's hang: both open = released (test_hang: hang) */
    r->c.inside_cm = 0, r->c.cue = 0;    /* ... and before its inside ray and reach cue (test_reach) */
    r->in.live = 1, r->in.allow = 1, r->in.head_ok = 1;
    r->apply = 1;
    r->head_track_y = 170, r->head_track_z = 50;
    r->t = 10.0;
    for (int h = 0; h < 2; h++) {
        r->in.hand[h].valid = 1, r->in.hand[h].buttons = 1;
        r->in.hand[h].p = (sv_vec3){ h ? 20.0f : -20.0f, 150, 85 }; /* 15 cm in front of the wall */
        r->in.hand[h].palm = (sv_vec3){ 0, 0, 1 }, r->in.hand[h].fwd = (sv_vec3){ 0, 1, 0 };
    }
}

/* one frame at 90 Hz: the last request moves the body, then the step */
static void rig_frame(rig_t *r) {
    if (r->apply) {
        sv_vec3 p = r->pending;
        if (r->ledge && r->body.y + p.y < g_wall_top - 40 && r->body.z + p.z + 50 > WALL_Z - 35)
            p.z = fmaxf(0.0f, WALL_Z - 35 - 50 - r->body.z); /* the capsule against the face under the edge */
        r->body.x += p.x, r->body.y += p.y, r->body.z += p.z;
        r->moved.x += p.x, r->moved.y += p.y, r->moved.z += p.z;
    }
    r->pending = (sv_vec3){ 0, 0, 0 };
    r->t += 1.0 / 90;
    r->in.t = r->t;
    r->in.rig_yaw = r->yaw;
    r->in.rig_scale = r->scale;
    r->in.head_y = r->body.y + r->head_track_y;
    r->in.head_x = r->body.x, r->in.head_z = r->body.z + r->head_track_z;
    r->in.root_ok = 1, r->in.root = (sv_vec3){ r->body.x, r->body.y + 100, r->body.z + 50 };
    for (int h = 0; h < 2; h++) {
        r->in.hand[h].pw = track_to_world(r, r->in.hand[h].p); /* palm/fwd: world directions as given */
    }
    somvr_climb_step(&r->s, &r->c, &r->in, mock_ray, NULL, &r->o);
    if (r->o.move) r->pending = r->o.move_w;
}
static void frames(rig_t *r, int n) {
    for (int i = 0; i < n; i++) rig_frame(r);
}
static void grip(rig_t *r, int h, float v) { r->in.hand[h].squeeze = v; }
static void hand_move(rig_t *r, int h, float dx, float dy, float dz) {
    r->in.hand[h].p.x += dx, r->in.hand[h].p.y += dy, r->in.hand[h].p.z += dz;
}

/* ---- the pieces ------------------------------------------------------------------------------------------------- */
static void test_pieces(void) {
    somvr_climb_cfg c;
    somvr_climb_cfg_default(&c);
    CHECK(c.grip_on == 0.6f && c.grip_off == 0.4f && c.reach_cm == 25 && c.back_cm == 10 && c.step_cm == 8 &&
              c.carry_cm == 20 && c.top_mode == SOMVR_CLIMB_TOP_PUSH && c.rays == 3 && c.max_s == 120 &&
              c.over_cm == 30 && c.down_assist == 1 && c.ramp == 1 && c.low_cm == 50,
          "defaults");
    int d = 0;
    CHECK(somvr_climb_grip(&d, 0.5f, 0.6f, 0.4f) == 0, "0.5 from open stays open");
    CHECK(somvr_climb_grip(&d, 0.61f, 0.6f, 0.4f) == 1, "0.61 closes");
    CHECK(somvr_climb_grip(&d, 0.45f, 0.6f, 0.4f) == 1, "0.45 from closed stays closed");
    CHECK(somvr_climb_grip(&d, 0.39f, 0.6f, 0.4f) == 0, "0.39 opens");
    d = 1;
    CHECK(somvr_climb_grip(&d, NAN, 0.6f, 0.4f) == 0, "NaN opens");
    for (int i = 0; i < 16; i++) {
        float y = -3.0f + 0.4f * (float)i;
        sv_vec3 v = { 1.5f - 0.2f * (float)i, 0.3f * (float)i - 2, 7 - (float)i };
        sv_vec3 a = somvr_climb_to_world(v, y), b = sv_qrot(sv_qyaw(y), v);
        CHECK(NEAR3(a, b.x, b.y, b.z, 1e-5), "to_world = sv_qrot(sv_qyaw) at %g", y);
    }
    sv_vec3 r = somvr_climb_to_world((sv_vec3){ 0, 0, 10 }, 1.5707963f);
    CHECK(NEAR3(r, 10, 0, 0, 1e-4), "yaw +90: forward -> +x (%g %g %g)", r.x, r.y, r.z);
    sv_vec3 k = somvr_climb_clamp((sv_vec3){ 30, 0, 40 }, 10);
    CHECK(NEAR3(k, 6, 0, 8, 1e-5), "clamp 50 -> 10");
    k = somvr_climb_clamp((sv_vec3){ 3, 0, 4 }, 10);
    CHECK(NEAR3(k, 3, 0, 4, 0), "clamp under the limit");
    k = somvr_climb_clamp((sv_vec3){ NAN, 0, 4 }, 10);
    CHECK(NEAR3(k, 0, 0, 0, 0), "clamp NaN");
    CHECK(!strcmp(somvr_climb_end_name(SOMVR_CLIMB_END_LIFT_STALL), "lift-stall") && !strcmp(somvr_climb_end_name(99), "?") &&
              !strcmp(somvr_climb_phase_name(SOMVR_CLIMB_PH_LIFT), "lift"),
          "names");
    /* the surfaces (the game's 63 records, run 053549): walls holds the solid ones whatever CanWallClimb says */
    static const char *const held[] = { "stone", "mossy_stone", "stone_wet", "wood", "WallRun", "Stone_No_WallRun",
                                        "Metal", "gravel", "Grass", "dirt", "default", "rope", "AxeClimb", "Wood_Walkway",
                                        "Snow", "bamboo", "Glass", "metal_cage", "" };
    static const char *const not_held[] = { "Flesh", "Flesh_Player", "Armor", "Armor_Player", "Sky", "Air", "Invisible",
                                            "Invisible_WallRun", "PlayerBlocker", "PlayerBlocker_Dust", "water_shallow",
                                            "water_NurnRiverTop", "Liquid_Generic", "liquid_puddle", "mud", "nurnen_mud",
                                            "foliage", "Ice", "Slippery", "No_WallRunPlusSlip", "No_WallRunPlusSlip_Wood" };
    for (unsigned i = 0; i < sizeof held / sizeof held[0]; i++)
        CHECK(somvr_climb_surface_ok(SOMVR_CLIMB_SURF_WALLS, held[i], 0), "walls holds '%s'", held[i]);
    for (unsigned i = 0; i < sizeof not_held / sizeof not_held[0]; i++)
        CHECK(!somvr_climb_surface_ok(SOMVR_CLIMB_SURF_WALLS, not_held[i], 1), "walls does not hold '%s'", not_held[i]);
    CHECK(somvr_climb_surface_ok(SOMVR_CLIMB_SURF_WALLS, NULL, 0) && !somvr_climb_surface_ok(SOMVR_CLIMB_SURF_GAME, "stone", 0) &&
              somvr_climb_surface_ok(SOMVR_CLIMB_SURF_GAME, "Stone_No_WallRun", 1) &&
              somvr_climb_surface_ok(SOMVR_CLIMB_SURF_GAME, "Flesh", 1) && somvr_climb_surface_ok(SOMVR_CLIMB_SURF_ANY, "Sky", 0) &&
              somvr_climb_surface_ok(SOMVR_CLIMB_SURF_ANY, "water_shallow", 0) &&
              !somvr_climb_surface_ok(SOMVR_CLIMB_SURF_ANY, "FLESH_Player", 1) &&
              somvr_climb_surface_ok(SOMVR_CLIMB_SURF_WALLS, "Stairs", 0) && somvr_climb_surface_ok(SOMVR_CLIMB_SURF_WALLS, "chair", 0),
          "surfaces: NULL, game = CanWallClimb, any = all but flesh, prefixes only for air/ice");
}

/* ---- the grab --------------------------------------------------------------------------------------------------- */
static void test_grab(void) {
    rig_t r;
    rig_init(&r);
    frames(&r, 2);
    CHECK(!r.o.active && r.s.rays == 0, "no press, no ray (%llu)", (unsigned long long)r.s.rays);
    grip(&r, 1, 0.9f);
    int g0 = g_rays;
    rig_frame(&r);
    CHECK(r.o.started && r.o.active && (r.o.events & SOMVR_CLIMB_E_GRAB_R) && r.s.h[1].holding && !r.s.h[0].holding,
          "15 cm from the wall: a hold (events %#x)", r.o.events);
    CHECK(NEAR(r.o.grab_hit[1].dist, 25, 1e-3) && NEAR3(r.s.h[1].hit, 20, 150, WALL_Z, 1e-3) &&
              NEAR3(r.s.h[1].dir, 0, 0, 1, 1e-6) && NEAR3(r.s.h[1].anchor, 20, 150, 85, 1e-3),
          "the hold: dist %g hit %g %g %g", r.o.grab_hit[1].dist, r.s.h[1].hit.x, r.s.h[1].hit.y, r.s.h[1].hit.z);
    CHECK(r.o.grab_rays[1] == 2 && g_rays - g0 == 3 && !r.s.h[1].edge, "palm + fingers + the edge ray: %d rays, edge %d",
          g_rays - g0, r.s.h[1].edge);
    CHECK(r.o.grab_cast[1] == 1 && r.o.grab_cast[0] == 0, "the palm ray (cast 1) gave the hold: %d", r.o.grab_cast[1]);
    rig_frame(&r);
    CHECK(r.o.active && !r.o.started && r.o.rays == 0 && !r.o.move, "holding: no more rays, no motion");
    grip(&r, 1, 0);
    rig_frame(&r);
    CHECK(r.o.ended && r.o.end_why == SOMVR_CLIMB_END_RELEASED && !r.o.active && (r.o.events & SOMVR_CLIMB_E_LET_R),
          "the grip open: released");

    /* out of reach (40 cm): looking for the window, then a miss; a press after reopening grabs once in reach */
    rig_init(&r);
    for (int h = 0; h < 2; h++) r.in.hand[h].p.z = 60;
    grip(&r, 0, 1);
    rig_frame(&r);
    CHECK(!r.o.active && r.s.h[0].looking && r.o.grab_rays[0] == 2, "40 cm: looking");
    frames(&r, 10);
    CHECK(!r.o.active && r.s.h[0].looking, "still in the window after 11 frames");
    int miss = 0;
    for (int i = 0; i < 20; i++) {
        rig_frame(&r);
        miss |= (r.o.events & SOMVR_CLIMB_E_MISS_L) != 0;
    }
    CHECK(miss && !r.s.h[0].looking && !r.s.h[0].armed && r.s.misses == 1, "a miss after 0.25 s, disarmed");
    r.in.hand[0].p.z = 85;
    frames(&r, 3);
    CHECK(!r.o.active, "a held grip after a miss does not grab by moving into reach");
    grip(&r, 0, 0);
    rig_frame(&r);
    grip(&r, 0, 1);
    rig_frame(&r);
    CHECK(r.o.active && r.s.h[0].holding, "reopened and pressed: a hold");

    /* the window: pressed 30 cm off, moved in within 0.1 s */
    rig_init(&r);
    r.in.hand[1].p.z = 62;
    grip(&r, 1, 1);
    frames(&r, 4);
    CHECK(!r.o.active, "30+ cm: no hold yet");
    r.in.hand[1].p.z = 80;
    rig_frame(&r);
    CHECK(r.o.active && r.s.h[1].holding, "moved into reach inside the window: a hold");

    /* a hand 5 cm inside the wall (no physical wall): the ray from 10 cm behind still finds it */
    rig_init(&r);
    r.in.hand[0].p.z = 105;
    grip(&r, 0, 1);
    rig_frame(&r);
    CHECK(r.o.active && NEAR(r.o.grab_hit[0].dist, 5, 1e-3), "inside by 5 cm: a hold at %g", r.o.grab_hit[0].dist);

    /* the palm down, the fingers at the wall: the finger ray finds it; only the palm ray (rays = 1): nothing */
    rig_init(&r);
    r.in.hand[1].palm = (sv_vec3){ 0, -1, 0 }, r.in.hand[1].fwd = (sv_vec3){ 0, 0, 1 };
    grip(&r, 1, 1);
    rig_frame(&r);
    CHECK(r.o.active && NEAR3(r.s.h[1].dir, 0, 0, 1, 1e-6) && r.o.grab_cast[1] == 2, "the finger ray's hold (cast %d)",
          r.o.grab_cast[1]);
    rig_init(&r);
    r.c.rays = SOMVR_CLIMB_RAY_PALM;
    r.in.hand[1].palm = (sv_vec3){ 0, -1, 0 }, r.in.hand[1].fwd = (sv_vec3){ 0, 0, 1 };
    grip(&r, 1, 1);
    rig_frame(&r);
    CHECK(!r.o.active && r.o.grab_rays[1] == 1, "palm only, the palm away: no hold");
    /* both rays hit: the nearer counts (the palm ray at a slant is longer) */
    rig_init(&r);
    r.in.hand[1].palm = (sv_vec3){ 0, 0.6f, 0.8f }, r.in.hand[1].fwd = (sv_vec3){ 0, 0, 1 };
    grip(&r, 1, 1);
    rig_frame(&r);
    CHECK(r.o.active && NEAR3(r.s.h[1].dir, 0, 0, 1, 1e-6) && NEAR(r.s.h[1].hit_dist, 25, 1e-3), "the nearer hit");

    /* rock (not CanWallClimb): no hold, the press stops looking */
    rig_init(&r);
    r.in.hand[1].p.x = 250;
    grip(&r, 1, 1);
    rig_frame(&r);
    CHECK(!r.o.active && (r.o.events & SOMVR_CLIMB_E_NOCLIMB_R) && r.o.grab_hit[1].hit && !r.o.grab_hit[1].climbable &&
              !r.s.h[1].looking && r.o.grab_cast[1] == 1,
          "rock: not climbable (cast %d)", r.o.grab_cast[1]);
    int rays = g_rays;
    frames(&r, 5);
    CHECK(g_rays == rays && !r.o.active, "no more rays after a not-climbable answer");

    /* the game does not allow it (say Falling): no hold while pressed; allowed within the window: a hold */
    rig_init(&r);
    r.in.allow = 0;
    grip(&r, 0, 1);
    frames(&r, 5);
    CHECK(!r.o.active && r.s.rays == 0, "not allowed: no ray, no hold");
    r.in.allow = 1;
    rig_frame(&r);
    CHECK(r.o.active, "allowed inside the window: a hold");
    /* the ray answers nothing (no physics): no hold, then a miss */
    rig_init(&r);
    g_ray_off = 1;
    grip(&r, 0, 1);
    frames(&r, 30);
    CHECK(!r.o.active && r.s.misses == 1, "no ray answer: a miss");
    g_ray_off = 0;
    /* no ray function at all */
    rig_init(&r);
    grip(&r, 0, 1);
    r.in.t = 20;
    somvr_climb_step(&r.s, &r.c, &r.in, NULL, NULL, &r.o);
    CHECK(!r.o.active && r.o.rays == 0, "no ray function: no hold");
    /* a hand without its buttons active, or the input not live: nothing */
    rig_init(&r);
    r.in.hand[0].buttons = 0;
    grip(&r, 0, 1);
    frames(&r, 3);
    CHECK(!r.o.active && !r.s.h[0].down, "buttons inactive: the grip reads open");
    rig_init(&r);
    r.in.live = 0;
    grip(&r, 0, 1);
    frames(&r, 3);
    CHECK(!r.o.active && !r.s.h[0].down, "input not live: the grip reads open");
    /* an untracked hand cannot grab */
    rig_init(&r);
    r.in.hand[0].valid = 0;
    grip(&r, 0, 1);
    frames(&r, 3);
    CHECK(!r.o.active && r.s.rays == 0, "untracked: no grab");
    /* nograb (somvr.c: the bow is out, the hands are on it): a press in reach is spent, no ray; still held after the
     * bow it does not grab; opened and pressed again it does. A hold taken before stays */
    rig_init(&r);
    r.in.nograb = 1;
    grip(&r, 1, 1);
    frames(&r, 3);
    CHECK(!r.o.active && r.s.rays == 0 && !r.s.h[1].armed && !r.s.h[1].looking && r.s.misses == 0,
          "nograb: a press in reach: active %d rays %llu armed %d", r.o.active, (unsigned long long)r.s.rays,
          r.s.h[1].armed);
    r.in.nograb = 0;
    frames(&r, 3);
    CHECK(!r.o.active && r.s.rays == 0, "nograb over, the grip still held: no grab");
    grip(&r, 1, 0);
    rig_frame(&r);
    grip(&r, 1, 1);
    rig_frame(&r);
    CHECK(r.o.active && r.s.h[1].holding, "nograb over, opened and pressed: a hold");
    r.in.nograb = 1;
    grip(&r, 0, 1);
    frames(&r, 2);
    CHECK(r.o.active && r.s.h[1].holding && !r.s.h[0].holding && !r.s.h[0].armed,
          "nograb while holding: the hold kept, the other hand's press spent");
}

/* ---- the motion ------------------------------------------------------------------------------------------------- */
static void test_motion(void) {
    rig_t r;
    rig_init(&r);
    grip(&r, 1, 1);
    rig_frame(&r);
    sv_vec3 a0 = r.in.hand[1].pw;
    /* the hand pulls down 3 cm a frame for 10 frames: the body rises 30 cm, the hand stays on its hold */
    for (int i = 0; i < 10; i++) {
        hand_move(&r, 1, 0, -3, 0);
        rig_frame(&r);
        CHECK(r.o.move && NEAR3(r.o.move_w, 0, 3, 0, 1e-4), "pull %d: move %g %g %g", i, r.o.move_w.x, r.o.move_w.y,
              r.o.move_w.z);
    }
    rig_frame(&r);
    CHECK(NEAR3(r.moved, 0, 30, 0, 1e-3), "30 cm up: %g %g %g", r.moved.x, r.moved.y, r.moved.z);
    CHECK(NEAR3(r.in.hand[1].pw, a0.x, a0.y, a0.z, 1e-3), "the hand on its hold: %g %g %g vs %g %g %g", r.in.hand[1].pw.x,
          r.in.hand[1].pw.y, r.in.hand[1].pw.z, a0.x, a0.y, a0.z);
    CHECK(!r.o.move, "the hand still: no move");
    /* sideways (shimmy) and away from the wall: the body the other way */
    hand_move(&r, 1, 4, 0, 0);
    rig_frame(&r);
    CHECK(NEAR3(r.o.move_w, -4, 0, 0, 1e-4), "shimmy");
    hand_move(&r, 1, 0, 0, 5);
    rig_frame(&r);
    CHECK(NEAR3(r.o.move_w, 0, 0, -5, 1e-4), "the hand toward the wall: the body away from it (the hand pushes)");
    /* the rig turned 90 deg: tracking +z is world +x; the hand moving +z in tracking pushes the body -x */
    r.yaw = 1.5707963f;
    hand_move(&r, 1, 0, 0, 2);
    rig_frame(&r);
    CHECK(NEAR3(r.o.move_w, -2, 0, 0, 1e-4), "yaw 90: %g %g %g", r.o.move_w.x, r.o.move_w.y, r.o.move_w.z);
    r.yaw = 0;
    rig_frame(&r);
    /* the clamp: 20 cm in one frame = 8 + 8 + 4 */
    hand_move(&r, 1, 0, -20, 0);
    rig_frame(&r);
    CHECK(NEAR3(r.o.move_w, 0, 8, 0, 1e-4), "clamped 8");
    rig_frame(&r);
    CHECK(NEAR3(r.o.move_w, 0, 8, 0, 1e-4), "carried 8");
    rig_frame(&r);
    CHECK(NEAR3(r.o.move_w, 0, 4, 0, 1e-4), "carried 4");
    rig_frame(&r);
    CHECK(!r.o.move, "carry spent");
    /* a tracking jump (30 cm): ignored, the next frame goes on from there */
    sv_vec3 m0 = r.moved;
    hand_move(&r, 1, 0, -30, 0);
    rig_frame(&r);
    CHECK((r.o.events & SOMVR_CLIMB_E_JUMP) && !r.o.move && r.s.jumps == 1, "jump ignored");
    hand_move(&r, 1, 0, -2, 0);
    rig_frame(&r);
    CHECK(NEAR3(r.o.move_w, 0, 2, 0, 1e-4), "after the jump: the next pull counts from the new place");
    rig_frame(&r);
    CHECK(NEAR(r.moved.y - m0.y, 2, 1e-3), "only the 2 cm moved");
    /* NaN: a jump, no NaN move */
    sv_vec3 keep = r.in.hand[1].p;
    r.in.hand[1].p.y = NAN;
    rig_frame(&r);
    CHECK(!r.o.move && (r.o.events & SOMVR_CLIMB_E_JUMP), "NaN: no move");
    r.in.hand[1].p = keep;
    rig_frame(&r);
    rig_frame(&r);
    CHECK(!r.o.move && r.o.active, "back from NaN: still");
    /* the carry cut: jump_cm 100, a 40 cm pull: 8 asked, 32 carried -> cut to 20 */
    r.c.jump_cm = 100;
    m0 = r.moved;
    hand_move(&r, 1, 0, -40, 0);
    rig_frame(&r);
    CHECK((r.o.events & SOMVR_CLIMB_E_CARRY_CUT) && NEAR(r.o.move_w.y, 8, 1e-4) && r.s.cuts == 1, "carry cut");
    frames(&r, 6);
    CHECK(NEAR(r.moved.y - m0.y, 28, 1e-3), "28 of 40 moved: %g", r.moved.y - m0.y);
    /* the untracked driver: no motion; back: re-baselined (the motion while lost does not count) */
    m0 = r.moved;
    r.in.hand[1].valid = 0;
    hand_move(&r, 1, 0, -10, 0);
    frames(&r, 5);
    r.in.hand[1].valid = 1;
    rig_frame(&r);
    hand_move(&r, 1, 0, -1, 0);
    frames(&r, 2);
    CHECK(r.o.active && NEAR(r.moved.y - m0.y, 1, 1e-3), "lost 5 frames: only the 1 cm after it moved (%g)",
          r.moved.y - m0.y);
}

/* ---- hand over hand --------------------------------------------------------------------------------------------- */
static void test_hand_over_hand(void) {
    rig_t r;
    rig_init(&r);
    grip(&r, 1, 1);
    rig_frame(&r);
    for (int i = 0; i < 10; i++) hand_move(&r, 1, 0, -4, 0), rig_frame(&r);
    /* the left hand reaches up and grabs: it drives from now on (its own motion), the right one's is ignored */
    r.in.hand[0].p.y = r.in.hand[1].p.y + 60;
    rig_frame(&r);
    grip(&r, 0, 1);
    rig_frame(&r);
    CHECK((r.o.events & SOMVR_CLIMB_E_GRAB_L) && r.o.driver == 0 && r.s.h[0].seq > r.s.h[1].seq, "L grabs, drives");
    sv_vec3 m0 = r.moved;
    hand_move(&r, 1, 0, -5, 0); /* the right hand moves: not the driver */
    rig_frame(&r);
    CHECK(!r.o.move, "the older hold does not drive");
    hand_move(&r, 0, 0, -3, 0);
    rig_frame(&r);
    CHECK(NEAR3(r.o.move_w, 0, 3, 0, 1e-4), "the newer one does");
    /* the left lets go: the right drives again from where it is now (its 5 cm earlier never count) */
    grip(&r, 0, 0);
    rig_frame(&r);
    CHECK((r.o.events & SOMVR_CLIMB_E_LET_L) && r.o.active && r.o.driver == 1, "L lets go, R drives");
    hand_move(&r, 1, 0, -2, 0);
    rig_frame(&r);
    CHECK(NEAR3(r.o.move_w, 0, 2, 0, 1e-4), "R's next pull: %g", r.o.move_w.y);
    rig_frame(&r);
    CHECK(NEAR(r.moved.y - m0.y, 5, 1e-3), "3 + 2 moved: %g", r.moved.y - m0.y);
    /* a switch in the middle of a pull: the frame L grabs, L's delta is 0; no motion lost after it */
    m0 = r.moved;
    r.in.hand[0].p.y = r.in.hand[1].p.y + 50;
    for (int i = 0; i < 4; i++) {
        hand_move(&r, 1, 0, -2, 0);
        if (i == 2) grip(&r, 0, 1);
        hand_move(&r, 0, 0, i >= 2 ? -2.0f : 0, 0);
        rig_frame(&r);
    }
    rig_frame(&r);
    CHECK(NEAR(r.moved.y - m0.y, 2 + 2 + 0 + 2, 1e-3), "R 2 + 2, the grab frame 0, then L 2: %g", r.moved.y - m0.y);
    /* both let go: released, gravity back (not active) */
    grip(&r, 0, 0), grip(&r, 1, 0);
    rig_frame(&r);
    CHECK(r.o.ended && r.o.end_why == SOMVR_CLIMB_END_RELEASED && !r.o.active && r.s.h[0].armed && r.s.h[1].armed,
          "both open: released, armed");
    grip(&r, 1, 1);
    r.in.hand[1].p.z = 85;
    rig_frame(&r);
    CHECK(r.o.started, "a new press grabs at once after a release");
    CHECK(r.s.climbs == 2 && r.s.lets >= 3, "counters climbs %llu lets %llu", (unsigned long long)r.s.climbs,
          (unsigned long long)r.s.lets);
    /* lost tracking: 0.2 s keeps the hold, past 0.3 s lets go -> the only hold: released */
    rig_init(&r);
    grip(&r, 0, 1);
    rig_frame(&r);
    r.in.hand[0].valid = 0;
    frames(&r, 18);
    CHECK(r.o.active && r.s.h[0].holding, "0.2 s untracked: still holding");
    int lost = 0, ended = 0;
    for (int i = 0; i < 12; i++) {
        rig_frame(&r);
        lost |= (r.o.events & SOMVR_CLIMB_E_LOST_L) != 0;
        ended |= r.o.ended && r.o.end_why == SOMVR_CLIMB_END_RELEASED;
    }
    CHECK(lost && ended && !r.o.active, "lost after 0.3 s, released");
}

/* ---- stand-downs, timeout, freeze -------------------------------------------------------------------------------- */
static void test_ends(void) {
    rig_t r;
    rig_init(&r);
    grip(&r, 0, 1), grip(&r, 1, 1);
    rig_frame(&r);
    CHECK(r.o.active && r.s.h[0].holding && r.s.h[1].holding, "two holds at once");
    /* the game: FSM no longer Supported (a fall, a transition animation, the game's own climb) */
    r.in.allow = 0;
    rig_frame(&r);
    CHECK(r.o.ended && r.o.end_why == SOMVR_CLIMB_END_GAME && !r.o.active && !r.s.h[0].armed && !r.s.h[1].armed &&
              r.s.standdowns == 1,
          "stand down: game");
    r.in.allow = 1;
    frames(&r, 3);
    CHECK(!r.o.active, "held grips do not grab again by themselves");
    grip(&r, 0, 0), grip(&r, 1, 0);
    rig_frame(&r);
    grip(&r, 0, 1);
    rig_frame(&r);
    CHECK(r.o.active, "reopened: a hold again");
    /* the input */
    r.in.live = 0;
    rig_frame(&r);
    CHECK(r.o.ended && r.o.end_why == SOMVR_CLIMB_END_INPUT && !r.o.active, "stand down: input");
    r.in.live = 1;
    rig_frame(&r);
    CHECK(!r.o.active, "the grip held through the loss does not grab");
    /* the timeout */
    rig_init(&r);
    r.c.max_s = 1;
    grip(&r, 0, 1);
    rig_frame(&r);
    frames(&r, 80);
    CHECK(r.o.active, "0.9 s: on");
    int to = 0;
    for (int i = 0; i < 20; i++) {
        rig_frame(&r);
        to |= r.o.ended && r.o.end_why == SOMVR_CLIMB_END_TIMEOUT;
    }
    CHECK(to && !r.o.active && r.s.timeouts == 1, "timeout");
    /* from outside */
    rig_init(&r);
    grip(&r, 1, 1);
    rig_frame(&r);
    somvr_climb_stop(&r.s, SOMVR_CLIMB_END_OUTSIDE, &r.o);
    CHECK(r.o.ended && r.o.end_why == SOMVR_CLIMB_END_OUTSIDE && !r.s.active && !r.s.h[1].armed && !r.s.h[1].holding,
          "stop from outside");
    rig_frame(&r);
    CHECK(!r.o.active, "a held grip after the stop: nothing");
    somvr_climb_stop(&r.s, SOMVR_CLIMB_END_OUTSIDE, &r.o);
    CHECK(!r.o.ended, "a stop while idle: no end");
    /* the freeze (a menu): nothing moves, nothing lets go, the clock stops */
    rig_init(&r);
    r.c.max_s = 2;
    grip(&r, 1, 1);
    rig_frame(&r);
    frames(&r, 90);
    r.in.frozen = 1;
    sv_vec3 m0 = r.moved;
    for (int i = 0; i < 450; i++) { /* 5 s in the pause menu, the hand wandering, the grip open */
        hand_move(&r, 1, 0, i < 200 ? -0.5f : 0.5f, 0);
        grip(&r, 1, i > 300 ? 0.0f : 1.0f);
        rig_frame(&r);
        CHECK(r.o.active && !r.o.move && !r.o.ended && !r.o.rays, "frozen frame %d", i);
        if (fails > 20) break;
    }
    r.in.frozen = 0;
    grip(&r, 1, 1);
    rig_frame(&r);
    CHECK(r.o.active && !r.o.move && NEAR3(r.moved, m0.x, m0.y, m0.z, 1e-3), "back from the menu: no pull from it");
    frames(&r, 60);
    CHECK(r.o.active, "the timeout's clock stopped in the menu (1.7 s of climbing)");
    frames(&r, 40);
    CHECK(!r.o.active && r.s.last_end == SOMVR_CLIMB_END_TIMEOUT, "and runs out after 2 s of climbing");
}

/* ---- the edge and the top --------------------------------------------------------------------------------------- */
/* hold the top edge with the right hand and pull until the head is above the top */
static void top_setup(rig_t *r, int mode) {
    rig_init(r);
    r->c.top_mode = mode;
    r->head_track_y = 170;
    r->in.hand[1].p.y = 290; /* 10 cm under the top */
    grip(r, 1, 1);
    rig_frame(r);
}
static int pull_to_top(rig_t *r) {
    for (int i = 0; i < 80; i++) {
        hand_move(r, 1, 0, -3, 0);
        rig_frame(r);
        if (r->o.events & SOMVR_CLIMB_E_TOP) return i;
    }
    return -1;
}

static void test_top(void) {
    rig_t r;
    top_setup(&r, SOMVR_CLIMB_TOP_RELEASE);
    CHECK(r.o.active && r.s.h[1].edge && r.s.h[1].top_found && NEAR(r.s.h[1].top_y, WALL_TOP, 1e-3),
          "an edge hold, its top at %g (found %d)", r.s.h[1].top_y, r.s.h[1].top_found);
    CHECK(r.o.rays == 4, "grab rays 2 + edge + top = %d", r.o.rays);
    /* the head at 170 + body: top + 10 = 310 -> 140 cm of pulling */
    int i = pull_to_top(&r);
    CHECK(i >= 0 && r.o.ended && r.o.end_why == SOMVR_CLIMB_END_TOP && !r.o.active && r.s.tops == 1,
          "release at the top after %d frames, head %g", i, r.in.head_y);
    CHECK(r.in.head_y >= WALL_TOP + 10 - 1e-3 && r.in.head_y < WALL_TOP + 10 + 3.1, "the head %g just above top + 10",
          r.in.head_y);
    CHECK(!r.s.h[1].armed, "the grip must open first");
    CHECK(r.o.top_by == 1, "the head's top (%d)", r.o.top_by);
    /* the other hand reaching over the top (the head still under it): the top too; to the side by 1.2 m: not */
    top_setup(&r, SOMVR_CLIMB_TOP_RELEASE);
    r.in.hand[0].p = (sv_vec3){ 120, 315, 85 };
    rig_frame(&r);
    CHECK(r.o.active && !(r.o.events & SOMVR_CLIMB_E_TOP), "a hand over the top 1.4 m to the side: no top");
    r.in.hand[0].p = (sv_vec3){ -20, 309, 85 };
    rig_frame(&r);
    CHECK(r.o.active, "a hand 9 cm over the top: not yet");
    r.in.hand[0].p.y = 311;
    rig_frame(&r);
    CHECK(r.o.ended && (r.o.events & SOMVR_CLIMB_E_TOP) && r.o.top_by == 2 && r.in.head_y < WALL_TOP,
          "the hand over the top: by %d, head %g", r.o.top_by, r.in.head_y);
    /* a hold far below the top: no edge */
    rig_init(&r);
    r.in.hand[0].p.y = 200;
    grip(&r, 0, 1);
    rig_frame(&r);
    CHECK(r.o.active && !r.s.h[0].edge && r.o.rays == 3, "no edge 100 cm under the top (rays %d)", r.o.rays);
    /* an edge without a top surface found (a thin rail: the down ray finds nothing): the bound hit + edge_cm */
    rig_init(&r);
    r.in.hand[0].p.y = 290;
    g_no_top = 1;
    grip(&r, 0, 1);
    rig_frame(&r);
    g_no_top = 0;
    CHECK(r.s.h[0].edge && !r.s.h[0].top_found && NEAR(r.s.h[0].top_y, 290 + 25, 1e-3), "no top surface: the bound %g",
          r.s.h[0].top_y);
    /* a lower wall (top 200): a hold at 190 is an edge, its top 200 */
    rig_init(&r);
    g_wall_top = 200;
    r.in.hand[0].p.y = 190;
    grip(&r, 0, 1);
    rig_frame(&r);
    g_wall_top = WALL_TOP;
    CHECK(r.s.h[0].edge && r.s.h[0].top_found && NEAR(r.s.h[0].top_y, 200, 1e-3), "the lower wall's top %g",
          r.s.h[0].top_y);

    /* lift: up until the head is 185 above the top, then 40 over it (+z), then released */
    top_setup(&r, SOMVR_CLIMB_TOP_LIFT);
    i = pull_to_top(&r);
    CHECK(i >= 0 && r.o.active && r.s.phase == SOMVR_CLIMB_PH_LIFT && r.s.lifts == 1, "lift starts");
    float head0 = r.in.head_y;
    sv_vec3 m0 = r.moved;
    grip(&r, 1, 0); /* letting go during the lift does not stop it */
    int n = 0, done = 0;
    for (; n < 100 && !done; n++) {
        rig_frame(&r);
        done = r.o.ended;
        if (!done) CHECK(r.o.move && len3_ok(r.o.move_w), "lift frame %d asks a move", n);
    }
    CHECK(done && r.o.end_why == SOMVR_CLIMB_END_LIFTED && !r.o.active, "lifted after %d frames (why %s)", n,
          somvr_climb_end_name(r.o.end_why));
    rig_frame(&r);
    float up = r.moved.y - m0.y, fwd = r.moved.z - m0.z;
    CHECK(NEAR(r.in.head_y, WALL_TOP + 185, 0.01) && NEAR(up, WALL_TOP + 185 - head0, 0.01) && NEAR(fwd, 40, 0.01),
          "lifted: head %g up %g (from %g) over %g", r.in.head_y, up, head0, fwd);
    /* lift blocked (the moves not applied: an overhang): a stall after 0.3 s */
    top_setup(&r, SOMVR_CLIMB_TOP_LIFT);
    pull_to_top(&r);
    r.apply = 0;
    int stall = -1;
    for (n = 0; n < 60 && stall < 0; n++) {
        rig_frame(&r);
        if (r.o.ended) stall = n;
    }
    CHECK(stall >= 25 && stall <= 30 && r.o.end_why == SOMVR_CLIMB_END_LIFT_STALL, "stalled after %d frames", stall);
    /* lift out of time: the head rises 0.5 cm a frame only */
    top_setup(&r, SOMVR_CLIMB_TOP_LIFT);
    pull_to_top(&r);
    r.apply = 0;
    for (n = 0; n < 200 && !r.o.ended; n++) {
        r.body.y += 1.2f; /* slow but rising: no stall */
        rig_frame(&r);
    }
    CHECK(r.o.ended && r.o.end_why == SOMVR_CLIMB_END_LIFT_STALL && n >= 134 && n <= 137, "out of time after %d frames", n);
    /* cmd: the pulse once, no motion, released after 0.6 s */
    top_setup(&r, SOMVR_CLIMB_TOP_CMD);
    i = pull_to_top(&r);
    CHECK(i >= 0 && r.o.top_cmd && r.o.active && r.s.phase == SOMVR_CLIMB_PH_CMD && r.s.cmds == 1, "cmd pulse");
    int pulses = 0, moves = 0;
    for (n = 0; n < 100 && !r.o.ended; n++) {
        hand_move(&r, 1, 0, -2, 0);
        rig_frame(&r);
        pulses += r.o.top_cmd, moves += r.o.move;
    }
    CHECK(r.o.ended && r.o.end_why == SOMVR_CLIMB_END_CMD && pulses == 0 && moves == 0 && n >= 53 && n <= 56,
          "cmd: released after %d frames (pulses %d moves %d)", n, pulses, moves);
    /* cmd: the game takes over (FSM 9): stand down at once */
    top_setup(&r, SOMVR_CLIMB_TOP_CMD);
    pull_to_top(&r);
    rig_frame(&r);
    r.in.allow = 0;
    rig_frame(&r);
    CHECK(r.o.ended && r.o.end_why == SOMVR_CLIMB_END_GAME, "the game's own pull-up: stand down");
    /* the head not read and the other hand untracked: no top */
    top_setup(&r, SOMVR_CLIMB_TOP_RELEASE);
    r.in.head_ok = 0, r.in.hand[0].valid = 0;
    CHECK(pull_to_top(&r) < 0 && r.o.active, "no head: no top");
}

/* ---- push (the default top): the climb goes on 1:1 until the head is over the top; the down assist ----------------- */
static void test_push(void) {
    rig_t r;
    top_setup(&r, SOMVR_CLIMB_TOP_PUSH);
    int i = pull_to_top(&r);
    CHECK(i >= 0 && r.o.active && !r.o.ended && r.s.topped && r.s.phase == SOMVR_CLIMB_PH_HOLD && r.s.tops == 1 &&
              r.o.top_by == 1,
          "push: the top after %d frames, still holding", i);
    CHECK(NEAR3(r.s.top_toward, 0, 0, 1, 1e-4) && NEAR(r.s.top_hit.z, WALL_Z, 1e-3), "push: toward the wall (+z)");
    /* on 1:1: 60 cm more up, no second top */
    rig_frame(&r);
    sv_vec3 m0 = r.moved;
    int tops = 0, ends = 0;
    for (int k = 0; k < 20; k++) {
        hand_move(&r, 1, 0, -3, 0);
        rig_frame(&r);
        tops += (r.o.events & SOMVR_CLIMB_E_TOP) != 0, ends += r.o.ended;
    }
    rig_frame(&r);
    CHECK(NEAR(r.moved.y - m0.y, 60, 0.01) && !tops && !ends && r.o.active, "push: up %g (tops %d ends %d)",
          r.moved.y - m0.y, tops, ends);
    /* over: the hand toward the body (-z) moves the body +z 1:1; the head (body z + 50) passes the hold (z 100) + 30 */
    int n;
    for (n = 0; n < 80 && r.o.active; n++) {
        hand_move(&r, 1, 0, 0, -2);
        rig_frame(&r);
    }
    CHECK(r.o.ended && r.o.end_why == SOMVR_CLIMB_END_OVER && !r.o.active && !r.s.topped && !r.s.h[1].armed &&
              r.s.last_end == SOMVR_CLIMB_END_OVER && !strcmp(somvr_climb_end_name(SOMVR_CLIMB_END_OVER), "over"),
          "push: over after %d frames (why %s)", n, somvr_climb_end_name(r.o.end_why));
    CHECK(r.in.head_z >= WALL_Z + 30 - 1e-3 && r.in.head_z <= WALL_Z + 30 + 2.1f, "push: the head %g just past the edge",
          r.in.head_z);
    /* low_cm: standing (the root at 100), a hold 60 cm under the root (y 40) starts no climb (and needs the grip opened
     * again), one 40 cm under it (y 60) does; while climbing a hold at y 40 is taken */
    rig_init(&r);
    r.in.hand[0].p.y = 40;
    grip(&r, 0, 1);
    rig_frame(&r);
    CHECK(!r.o.active && (r.o.events & SOMVR_CLIMB_E_LOW_L) && r.s.lows == 1 && !r.s.h[0].armed, "low: no climb from y 40");
    frames(&r, 30);
    CHECK(!r.o.active && r.s.lows == 1, "low: the held grip does not try again");
    r.in.hand[0].p.y = 60;
    grip(&r, 0, 0);
    rig_frame(&r);
    grip(&r, 0, 1);
    rig_frame(&r);
    CHECK(r.o.active && r.s.h[0].holding, "low: y 60 starts a climb");
    r.in.hand[1].p.y = 40;
    grip(&r, 1, 1);
    rig_frame(&r);
    CHECK(r.s.h[1].holding && r.s.lows == 1, "low: while climbing y 40 is held");
    rig_init(&r);
    r.c.low_cm = 0;
    r.in.hand[0].p.y = 40;
    grip(&r, 0, 1);
    rig_frame(&r);
    CHECK(r.o.active, "low_cm 0: y 40 starts a climb");
    /* the edge step: the capsule against the face until its foot (body y) is 40 cm under the top (260): at the top
     * (head 310 = body 140) the push toward the body is blocked, the blocked part lifts the body 1:1 (2 cm a frame,
     * the hand's speed) until the foot clears, then the push goes over: the head 30 past the face = over */
    top_setup(&r, SOMVR_CLIMB_TOP_PUSH);
    r.ledge = 1;
    i = pull_to_top(&r);
    CHECK(i >= 0 && r.s.topped && NEAR(r.body.y, 140, 3.1) && NEAR(r.body.z, 0, 1e-3), "edge step: topped at %g", r.body.y);
    float y0 = r.body.y;
    int lifted_blocked = 0, lifts = 0;
    for (n = 0; n < 300 && r.o.active; n++) {
        hand_move(&r, 1, 0, 0, -2);
        rig_frame(&r);
        lifts += r.o.ramp > 0;
        if (r.o.ramp > 2.0001f) lifted_blocked++; /* never more than the push (2 cm a frame) */
    }
    CHECK(r.o.ended && r.o.end_why == SOMVR_CLIMB_END_OVER && lifted_blocked == 0 && lifts > 50,
          "edge step: over after %d frames (%d lifting frames, %d over the push)", n, lifts, lifted_blocked);
    CHECK(r.body.y >= 260 - 1e-3 && r.body.y <= 262.1f && NEAR(r.s.ramp_cm, r.body.y - y0, 3.1) && r.body.z >= 80 - 1e-3,
          "edge step: the foot cleared at %g (ramp %g, from %g), z %g", r.body.y, r.s.ramp_cm, y0, r.body.z);
    /* no forward push: no lift; ramp 0: blocked for good; not topped (the face mid-wall): no lift */
    top_setup(&r, SOMVR_CLIMB_TOP_PUSH);
    r.ledge = 1;
    pull_to_top(&r);
    rig_frame(&r); /* the last pull's move */
    y0 = r.body.y;
    frames(&r, 30);
    CHECK(NEAR(r.body.y, y0, 1e-3) && r.s.ramp_cm == 0, "edge step: no push, no lift (%g)", r.body.y - y0);
    r.c.ramp = 0;
    for (n = 0; n < 60; n++) hand_move(&r, 1, 0, 0, -2), rig_frame(&r);
    CHECK(r.o.active && NEAR(r.body.y, y0, 1e-3) && NEAR(r.body.z, 15, 1e-3), "edge step off: blocked at the face (y %g z %g)",
          r.body.y, r.body.z);
    rig_init(&r);
    r.ledge = 1;
    grip(&r, 1, 1);
    rig_frame(&r);
    for (n = 0; n < 30; n++) hand_move(&r, 1, 0, 0, -2), rig_frame(&r);
    CHECK(r.o.active && !r.s.topped && NEAR(r.body.y, 0, 1e-3) && r.s.ramp_cm == 0 && NEAR(r.body.z, 15, 1e-3),
          "mid-wall: a push into the face lifts nothing (y %g z %g)", r.body.y, r.body.z);
    /* the head at the top + lift_cm (185): no more lift (an overhang, not the edge) */
    top_setup(&r, SOMVR_CLIMB_TOP_PUSH);
    r.ledge = 1;
    g_wall_top = 500; /* the capsule never clears (the top's hold stays at 300) */
    pull_to_top(&r);
    for (n = 0; n < 300; n++) hand_move(&r, 1, 0, 0, -2), rig_frame(&r);
    g_wall_top = WALL_TOP;
    CHECK(r.o.active && r.in.head_y >= WALL_TOP + 185 - 1e-3 && r.in.head_y <= WALL_TOP + 185 + 2.1f,
          "the edge step stops at the head %g (top + 185)", r.in.head_y);
    /* both hands open after the top, before over: released (gravity back: the fall) */
    top_setup(&r, SOMVR_CLIMB_TOP_PUSH);
    pull_to_top(&r);
    grip(&r, 1, 0);
    rig_frame(&r);
    CHECK(r.o.ended && r.o.end_why == SOMVR_CLIMB_END_RELEASED && !r.s.topped, "push: let go before over: released");
    /* the down assist: climbing on the face (a hold 50 cm under the top: no edge), the left hand over the top (palm and
     * fingers not at it) grabs the top straight down: an edge hold, toward from the head; pulled down: the top by the
     * head, pushed toward the body: over */
    rig_init(&r);
    r.in.hand[1].p.y = 250;
    grip(&r, 1, 1);
    rig_frame(&r);
    CHECK(r.o.active && r.s.h[1].holding && !r.s.h[1].edge, "down: the face hold (edge %d)", r.s.h[1].edge);
    r.in.hand[0].p = (sv_vec3){ -20, 320, 115 };
    rig_frame(&r);
    grip(&r, 0, 1);
    rig_frame(&r);
    sv_vec3 tw = r.s.h[0].toward;
    CHECK(r.s.h[0].holding && r.s.h[0].edge && r.s.h[0].top_found && NEAR(r.s.h[0].top_y, WALL_TOP, 1e-3) &&
              NEAR3(r.s.h[0].hit, -20, WALL_TOP, 115, 1e-3) && NEAR3(r.s.h[0].dir, 0, -1, 0, 1e-6) &&
              NEAR3(tw, -20 / sqrtf(20 * 20 + 65 * 65), 0, 65 / sqrtf(20 * 20 + 65 * 65), 1e-4),
          "down: the top held (edge %d top %g, toward %g %g)", r.s.h[0].edge, r.s.h[0].top_y, tw.x, tw.z);
    grip(&r, 1, 0);
    r.in.hand[1].p.y = 110; /* the right hand down by the waist (not over the top as the body rises) */
    for (n = 0; n < 80 && !r.s.topped && r.o.active; n++) {
        hand_move(&r, 0, 0, -3, 0);
        rig_frame(&r);
    }
    CHECK(r.s.topped && r.o.active && r.in.head_y >= WALL_TOP + 10 - 1e-3, "down: the top by the head %g after %d frames",
          r.in.head_y, n);
    for (n = 0; n < 120 && r.o.active; n++) {
        hand_move(&r, 0, 0, 0, -2);
        rig_frame(&r);
    }
    float d = (r.in.head_x - (-20)) * tw.x + (r.in.head_z - 115) * tw.z;
    CHECK(r.o.ended && r.o.end_why == SOMVR_CLIMB_END_OVER && d >= 30 - 1e-3 && d < 32.1f, "down: over (%g past) after %d",
          d, n);
    /* standing (no climb): no down ray, the hand over the top misses */
    rig_init(&r);
    r.in.hand[0].p = (sv_vec3){ -20, 320, 115 };
    grip(&r, 0, 1);
    frames(&r, 30);
    CHECK(!r.o.active && r.s.misses == 1, "standing: no down ray (misses %llu)", (unsigned long long)r.s.misses);
    /* the down assist off: climbing, the hand over the top misses */
    rig_init(&r);
    r.c.down_assist = 0;
    r.in.hand[1].p.y = 250;
    grip(&r, 1, 1);
    rig_frame(&r);
    r.in.hand[0].p = (sv_vec3){ -20, 320, 115 };
    grip(&r, 0, 1);
    frames(&r, 30);
    CHECK(r.o.active && !r.s.h[0].holding && r.s.misses == 1, "down assist off: a miss");
    /* lift from a hold on the top: over the top toward it (the ray straight down has no horizontal part) */
    rig_init(&r);
    r.c.top_mode = SOMVR_CLIMB_TOP_LIFT;
    r.in.hand[1].p.y = 250;
    grip(&r, 1, 1);
    rig_frame(&r);
    r.in.hand[0].p = (sv_vec3){ -20, 320, 115 };
    grip(&r, 0, 1);
    rig_frame(&r);
    grip(&r, 1, 0);
    r.in.hand[1].p.y = 110;
    for (n = 0; n < 80 && r.s.phase == SOMVR_CLIMB_PH_HOLD; n++) {
        hand_move(&r, 0, 0, -3, 0);
        rig_frame(&r);
    }
    sv_vec3 l0 = r.moved;
    for (n = 0; n < 200 && r.o.active; n++) rig_frame(&r);
    rig_frame(&r);
    CHECK(r.s.last_end == SOMVR_CLIMB_END_LIFTED && NEAR(r.moved.z - l0.z, 40 * tw.z, 0.05) &&
              NEAR(r.moved.x - l0.x, 40 * tw.x, 0.05),
          "lift from the top hold: over %g %g", r.moved.x - l0.x, r.moved.z - l0.z);
}

/* ---- a whole climb: a 3 m wall, hand over hand at 90 Hz, then the lift ------------------------------------------- */
static void test_sequence(void) {
    rig_t r;
    rig_init(&r);
    r.c.top_mode = SOMVR_CLIMB_TOP_LIFT;
    r.head_track_y = 160;
    r.in.hand[0].p = (sv_vec3){ -20, 150, 85 }, r.in.hand[1].p = (sv_vec3){ 20, 200, 85 };
    int h = 1, reaches = 0, n = 0;
    grip(&r, 1, 1);
    rig_frame(&r);
    while (r.s.phase == SOMVR_CLIMB_PH_HOLD && n < 2000) {
        /* pull the holding hand down to the waist (110) 2 cm a frame; the other reaches up to 200 and grabs; then the
         * first lets go and hangs at 150 */
        while (r.in.hand[h].p.y > 110 && r.s.phase == SOMVR_CLIMB_PH_HOLD) hand_move(&r, h, 0, -2, 0), rig_frame(&r), n++;
        if (r.s.phase != SOMVR_CLIMB_PH_HOLD) break;
        int o = 1 - h;
        r.in.hand[o].p.y = 200;
        rig_frame(&r), n++;
        grip(&r, o, 1);
        rig_frame(&r), n++;
        CHECK(r.s.h[o].holding, "reach %d: the hold at %g", reaches, r.in.hand[o].pw.y);
        grip(&r, h, 0);
        rig_frame(&r), n++;
        r.in.hand[h].p.y = 150;
        h = o;
        reaches++;
    }
    for (int i = 0; i < 200 && r.s.active; i++) rig_frame(&r);
    rig_frame(&r);
    CHECK(!r.s.active && r.s.last_end == SOMVR_CLIMB_END_LIFTED && r.s.tops == 1 && r.s.climbs == 1 && r.s.jumps == 0,
          "the climb: end %s after %d reaches, tops %llu", somvr_climb_end_name(r.s.last_end), reaches,
          (unsigned long long)r.s.tops);
    CHECK(NEAR(r.in.head_y, WALL_TOP + 185, 0.5) && NEAR(r.moved.z, 40, 0.01) && NEAR(r.moved.x, 0, 0.01),
          "on top: head %g, %g over", r.in.head_y, r.moved.z);
    printf("climb: %d reaches, %llu grabs, %llu moves %.0f cm asked, %llu rays, head %.1f\n", reaches,
           (unsigned long long)r.s.grabs, (unsigned long long)r.s.moves, r.s.req_cm, (unsigned long long)r.s.rays,
           r.in.head_y);
}

/* the player size (re-xr-pipeline.md 35): at 130 % a 3 cm tracked pull moves the body 3.9 cm, the hand stays on its
 * hold in the world (the rig maps it x 1.3) */
static void test_size(void) {
    rig_t r;
    rig_init(&r);
    r.scale = 1.3f;
    r.in.hand[1].p.z = 85 / 1.3f; /* 15 cm in front of the wall in the world */
    grip(&r, 1, 1);
    rig_frame(&r);
    CHECK(r.s.h[1].holding, "a hold at 130 %%");
    sv_vec3 a0 = r.in.hand[1].pw;
    for (int i = 0; i < 10; i++) {
        hand_move(&r, 1, 0, -3, 0);
        rig_frame(&r);
        CHECK(r.o.move && NEAR3(r.o.move_w, 0, 3.9, 0, 1e-4), "pull %d at 130 %%: move %g %g %g", i, r.o.move_w.x,
              r.o.move_w.y, r.o.move_w.z);
    }
    rig_frame(&r);
    CHECK(NEAR3(r.moved, 0, 39, 0, 1e-3), "39 cm up: %g %g %g", r.moved.x, r.moved.y, r.moved.z);
    CHECK(NEAR3(r.in.hand[1].pw, a0.x, a0.y, a0.z, 1e-3), "the hand on its hold: %g %g %g vs %g %g %g", r.in.hand[1].pw.x,
          r.in.hand[1].pw.y, r.in.hand[1].pw.z, a0.x, a0.y, a0.z);
    r.scale = 7; /* not a size: 1 */
    hand_move(&r, 1, 0, -2, 0);
    rig_frame(&r);
    CHECK(NEAR3(r.o.move_w, 0, 2, 0, 1e-4), "a bad size is 1: %g", r.o.move_w.y);
}

/* ---- letgo = hang (build 10's default): both hands open = Talion stays, a new grip climbs on, A drops ----------- */
static void test_hang(void) {
    somvr_climb_cfg dc;
    somvr_climb_cfg_default(&dc);
    CHECK(dc.letgo == SOMVR_CLIMB_LETGO_HANG, "the default: hang");
    rig_t r;
    rig_init(&r);
    r.c.letgo = SOMVR_CLIMB_LETGO_HANG;
    r.c.max_s = 2;
    r.in.drop = 1;
    rig_frame(&r);
    r.in.drop = 0;
    rig_frame(&r);
    CHECK(!r.o.active && !r.o.ended && r.s.drops == 0, "A on foot: nothing");
    grip(&r, 1, 1);
    rig_frame(&r);
    for (int i = 0; i < 10; i++) hand_move(&r, 1, 0, -3, 0), rig_frame(&r);
    rig_frame(&r);
    sv_vec3 m0 = r.moved;
    CHECK(NEAR(m0.y, 30, 1e-3), "pulled 30: %g", m0.y);
    grip(&r, 1, 0);
    rig_frame(&r);
    CHECK(!r.o.ended && r.o.active && r.s.phase == SOMVR_CLIMB_PH_HANG && (r.o.events & SOMVR_CLIMB_E_HANG) &&
              (r.o.events & SOMVR_CLIMB_E_LET_R) && r.s.hangs == 1 && r.s.h[1].armed && r.o.driver == -1,
          "both open: hang, armed");
    /* 5 s hanging, the hands wandering (no hold): nothing moves, no timeout (max_s 2) */
    for (int i = 0; i < 450; i++) {
        hand_move(&r, 0, 0, i < 200 ? 0.5f : -0.5f, 0), hand_move(&r, 1, 0.3f, 0, 0);
        rig_frame(&r);
        CHECK(r.o.active && !r.o.move && !r.o.ended && !(r.o.events & SOMVR_CLIMB_E_HANG), "hang frame %d", i);
        if (fails > 20) break;
    }
    CHECK(NEAR3(r.moved, m0.x, m0.y, m0.z, 1e-4) && r.s.timeouts == 0 && r.s.hangs == 1, "5 s hanging: not moved, no timeout");
    /* a grip in the air (80 cm from the wall) is a miss; the hang goes on */
    r.in.hand[0].p = (sv_vec3){ -20, 150, 20 };
    grip(&r, 0, 1);
    frames(&r, 30);
    CHECK(r.o.active && r.s.phase == SOMVR_CLIMB_PH_HANG && r.s.misses == 1, "a miss while hanging: still hanging");
    grip(&r, 0, 0);
    rig_frame(&r);
    /* a fresh grip on the wall: the same climb goes on (no new start), its pull moves */
    r.in.hand[0].p = (sv_vec3){ -20, 170, 85 };
    grip(&r, 0, 1);
    rig_frame(&r);
    CHECK((r.o.events & SOMVR_CLIMB_E_RESUME) && (r.o.events & SOMVR_CLIMB_E_GRAB_L) && !r.o.started &&
              r.s.phase == SOMVR_CLIMB_PH_HOLD && r.s.climbs == 1 && r.s.resumes == 1 && r.o.driver == 0,
          "a grip on the wall: resume");
    hand_move(&r, 0, 0, -4, 0);
    rig_frame(&r);
    CHECK(NEAR3(r.o.move_w, 0, 4, 0, 1e-4), "the resumed hold pulls: %g", r.o.move_w.y);
    /* A while holding: the drop (the end, disarmed: the squeezed grip does not hold again) */
    r.in.drop = 1;
    rig_frame(&r);
    CHECK(r.o.ended && r.o.end_why == SOMVR_CLIMB_END_DROP && (r.o.events & SOMVR_CLIMB_E_DROP) && !r.o.active &&
              !r.s.h[0].armed && r.s.drops == 1 && !strcmp(somvr_climb_end_name(SOMVR_CLIMB_END_DROP), "drop"),
          "A while holding: drop");
    r.in.drop = 0;
    frames(&r, 3);
    CHECK(!r.o.active, "the grip still squeezed after the drop: no new hold");
    grip(&r, 0, 0);
    rig_frame(&r);
    /* hang, then A: the drop */
    grip(&r, 0, 1);
    rig_frame(&r);
    CHECK(r.o.started, "a new climb");
    grip(&r, 0, 0);
    rig_frame(&r);
    CHECK(r.s.phase == SOMVR_CLIMB_PH_HANG && r.o.active && !strcmp(somvr_climb_phase_name(r.s.phase), "hang"), "hang again");
    r.in.drop = 1;
    rig_frame(&r);
    CHECK(r.o.ended && r.o.end_why == SOMVR_CLIMB_END_DROP && !r.o.active && r.s.drops == 2, "A while hanging: drop");
    rig_frame(&r); /* the grips seen open after the drop (armed again) */
    /* A held since before the climb: no drop (a press is its rising edge); pressed again: the drop */
    grip(&r, 0, 1);
    rig_frame(&r);
    CHECK(r.o.started, "a climb with A held");
    frames(&r, 5);
    CHECK(r.o.active, "A held since before: no drop");
    r.in.drop = 0;
    rig_frame(&r);
    r.in.drop = 1;
    rig_frame(&r);
    CHECK(r.o.ended && r.o.end_why == SOMVR_CLIMB_END_DROP && r.s.drops == 3, "pressed again: drop");
    /* A in a menu (frozen): no drop, nor when the world comes back with it held */
    r.in.drop = 0;
    grip(&r, 0, 0);
    rig_frame(&r);
    rig_frame(&r);
    grip(&r, 0, 1);
    rig_frame(&r);
    grip(&r, 0, 0);
    rig_frame(&r);
    CHECK(r.s.phase == SOMVR_CLIMB_PH_HANG && r.o.active, "hanging");
    r.in.frozen = 1, r.in.drop = 1;
    frames(&r, 10);
    r.in.frozen = 0;
    frames(&r, 5);
    CHECK(r.o.active && r.s.phase == SOMVR_CLIMB_PH_HANG && r.s.drops == 3, "the menu's A: no drop");
    r.in.drop = 0;
    rig_frame(&r);
    /* the input lost while holding: hang (the grips must open first), no end */
    r.in.hand[1].p = (sv_vec3){ 20, 160, 85 };
    grip(&r, 1, 1);
    rig_frame(&r);
    CHECK(r.s.h[1].holding && r.s.phase == SOMVR_CLIMB_PH_HOLD, "holding");
    sv_vec3 m1 = r.moved;
    r.in.live = 0;
    frames(&r, 30);
    CHECK(r.o.active && !r.o.ended && r.s.phase == SOMVR_CLIMB_PH_HANG && !r.s.h[1].holding && !r.s.h[1].armed &&
              r.s.standdowns == 0 && NEAR3(r.moved, m1.x, m1.y, m1.z, 1e-4),
          "input lost: hang");
    r.in.live = 1;
    frames(&r, 3);
    CHECK(r.o.active && !r.s.h[1].holding, "back, the grip still squeezed: no hold");
    grip(&r, 1, 0);
    rig_frame(&r);
    grip(&r, 1, 1);
    rig_frame(&r);
    CHECK(r.s.h[1].holding && r.s.phase == SOMVR_CLIMB_PH_HOLD, "opened and squeezed: holds again");
    /* the game stands a hang down (FSM not Supported, a mount, third person): the fall */
    grip(&r, 1, 0);
    rig_frame(&r);
    CHECK(r.s.phase == SOMVR_CLIMB_PH_HANG, "hanging");
    r.in.allow = 0;
    rig_frame(&r);
    CHECK(r.o.ended && r.o.end_why == SOMVR_CLIMB_END_GAME && !r.o.active, "the game stands a hang down");
    /* on the ground (the floor 100 cm under the root, within ground_cm 125): both open = released, no hang; 30 cm up: hang */
    rig_init(&r);
    r.c.letgo = SOMVR_CLIMB_LETGO_HANG;
    CHECK(r.c.ground_cm == 125, "ground_cm %g", r.c.ground_cm);
    g_ground_on = 1;
    grip(&r, 1, 1);
    rig_frame(&r);
    grip(&r, 1, 0);
    rig_frame(&r);
    CHECK(r.o.ended && r.o.end_why == SOMVR_CLIMB_END_RELEASED && !r.o.active && r.s.hangs == 0, "on the ground: released");
    grip(&r, 1, 1);
    rig_frame(&r);
    for (int i = 0; i < 10; i++) hand_move(&r, 1, 0, -3, 0), rig_frame(&r);
    rig_frame(&r);
    grip(&r, 1, 0);
    rig_frame(&r);
    CHECK(r.o.active && r.s.phase == SOMVR_CLIMB_PH_HANG, "30 cm up (the floor 130 under the root): hang");
    g_ground_on = 0;
    /* the lost tracking of the only hold hangs too */
    rig_init(&r);
    r.c.letgo = SOMVR_CLIMB_LETGO_HANG;
    grip(&r, 0, 1);
    rig_frame(&r);
    r.in.hand[0].valid = 0;
    frames(&r, 40);
    CHECK(r.o.active && r.s.phase == SOMVR_CLIMB_PH_HANG && r.s.lost == 1 && !r.o.ended, "untracked 0.44 s: hang");
    /* push: let go after the top = hang, topped kept; a new hold over the top pushes on to over */
    rig_init(&r);
    r.c.letgo = SOMVR_CLIMB_LETGO_HANG;
    r.c.top_mode = SOMVR_CLIMB_TOP_PUSH;
    r.in.hand[1].p.y = 290;
    grip(&r, 1, 1);
    rig_frame(&r);
    for (int i = 0; i < 80 && !r.s.topped; i++) hand_move(&r, 1, 0, -3, 0), rig_frame(&r);
    CHECK(r.s.topped, "topped");
    grip(&r, 1, 0);
    frames(&r, 20);
    CHECK(r.o.active && r.s.phase == SOMVR_CLIMB_PH_HANG && r.s.topped, "let go after the top: hang, topped");
}

/* ---- build 10's reach: the hand inside the wall (inside_cm), the near miss (near_cm), the reach cue (cue) ---------- */
static void test_reach(void) {
    somvr_climb_cfg dc;
    somvr_climb_cfg_default(&dc);
    CHECK(dc.inside_cm == 30 && dc.near_cm == 35 && dc.cue == 1 && NEAR(dc.cue_s, 1.0 / 30, 1e-6) && dc.cue_off_s == 0.3f &&
              dc.cue_raise_cm == 10,
          "the reach defaults");
    rig_t r;
    /* the hand 15 cm in the wall (z 115; the palm ray starts at 105: inside, no hit): inside_cm 0 = a miss */
    rig_init(&r);
    r.in.hand[1].p.z = 115;
    grip(&r, 1, 1);
    frames(&r, 30);
    CHECK(!r.o.active && r.s.misses == 1, "15 cm in, no inside ray: a miss");
    /* inside_cm 30: the head's ray meets the face first: a hold there, 15-16 cm before the hand */
    rig_init(&r);
    r.c.inside_cm = 30;
    r.in.hand[1].p.z = 115;
    grip(&r, 1, 1);
    rig_frame(&r);
    CHECK(r.o.active && r.s.h[1].holding && r.s.h[1].via == SOMVR_CLIMB_VIA_INSIDE && r.s.insides == 1 &&
              r.s.h[1].inside_by > 15 && r.s.h[1].inside_by < 17 && NEAR(r.s.h[1].hit.z, WALL_Z, 1e-3) &&
              r.s.h[1].toward.z > 0.9f && !strcmp(somvr_climb_via_name(SOMVR_CLIMB_VIA_INSIDE), "inside"),
          "15 cm in: held via inside (by %g, hit z %g)", r.s.h[1].inside_by, r.s.h[1].hit.z);
    hand_move(&r, 1, 0, -4, 0);
    rig_frame(&r);
    CHECK(NEAR3(r.o.move_w, 0, 4, 0, 1e-4), "an inside hold pulls as any: %g", r.o.move_w.y);
    /* 40 cm in: past inside_cm: a miss */
    rig_init(&r);
    r.c.inside_cm = 30;
    r.in.hand[1].p.z = 140;
    grip(&r, 1, 1);
    frames(&r, 30);
    CHECK(!r.o.active && r.s.misses == 1 && r.s.insides == 0, "40 cm in: a miss");
    /* a hand in front of the wall: the palm ray holds (the inside ray not cast) */
    rig_init(&r);
    r.c.inside_cm = 30;
    grip(&r, 1, 1);
    rig_frame(&r);
    CHECK(r.s.h[1].holding && r.s.h[1].via == SOMVR_CLIMB_VIA_PALM && r.o.grab_rays[1] == 2, "in front: palm (%d rays)",
          r.o.grab_rays[1]);
    /* the near miss: the hand 50 cm from the wall: the palm ray finds it 25 cm past the reach */
    rig_init(&r);
    r.in.hand[1].p.z = 50;
    grip(&r, 1, 1);
    int near = 0;
    for (int i = 0; i < 30; i++) {
        rig_frame(&r);
        if (r.o.events & SOMVR_CLIMB_E_MISS_R) near = r.o.miss_near[1];
    }
    CHECK(r.s.misses == 1 && near && NEAR(r.s.h[1].near_past, 25, 1e-3) && r.s.h[1].near_via == SOMVR_CLIMB_VIA_PALM,
          "50 cm off: a near miss %g past (via %d)", r.s.h[1].near_past, r.s.h[1].near_via);
    grip(&r, 1, 0);
    rig_frame(&r);
    /* nothing near (the hand 1.5 m off): a miss, not near (no cue) */
    r.in.hand[1].p.z = -50;
    grip(&r, 1, 1);
    near = 1;
    for (int i = 0; i < 30; i++) {
        rig_frame(&r);
        if (r.o.events & SOMVR_CLIMB_E_MISS_R) near = r.o.miss_near[1];
    }
    CHECK(r.s.misses == 2 && !near && r.s.h[1].near_past < 0, "1.5 m off: a miss, nothing near");
    /* the reach cue: an open raised hand 15 cm from the wall: E_NEAR once */
    rig_init(&r);
    r.c.cue = 1;
    int nears = 0, rays0 = 0;
    for (int i = 0; i < 45; i++) {
        rig_frame(&r);
        nears += (r.o.events & SOMVR_CLIMB_E_NEAR_R) != 0;
        if (i == 0) rays0 = (int)r.s.rays;
    }
    CHECK(nears == 1 && (r.s.nears == 2) && r.s.cue_rays > 0 && rays0 > 0, "an open hand at the wall: one cue (R %d, all %llu)",
          nears, (unsigned long long)r.s.nears); /* both hands start 15 cm from the wall */
    CHECK(r.s.rays <= 2 * 2 * 16, "the cue's rays: ~30 Hz a hand (%llu in 0.5 s)", (unsigned long long)r.s.rays);
    /* away (out of reach) 0.1 s and back: no new cue; away 0.5 s and back: a cue */
    r.in.hand[1].p.z = 20;
    frames(&r, 9);
    r.in.hand[1].p.z = 85;
    nears = 0;
    for (int i = 0; i < 20; i++) rig_frame(&r), nears += (r.o.events & SOMVR_CLIMB_E_NEAR_R) != 0;
    CHECK(nears == 0, "away 0.1 s and back: no cue");
    r.in.hand[1].p.z = 20;
    frames(&r, 45);
    r.in.hand[1].p.z = 85;
    nears = 0;
    for (int i = 0; i < 20; i++) rig_frame(&r), nears += (r.o.events & SOMVR_CLIMB_E_NEAR_R) != 0;
    CHECK(nears == 1, "away 0.5 s and back: a cue (%d)", nears);
    /* a lowered hand (under the root + 10 while standing) is not probed; while climbing it is */
    r.in.hand[1].p.z = 20;
    frames(&r, 45);
    r.in.hand[1].p = (sv_vec3){ 20, 105, 85 };
    nears = 0;
    for (int i = 0; i < 20; i++) rig_frame(&r), nears += (r.o.events & SOMVR_CLIMB_E_NEAR_R) != 0;
    CHECK(nears == 0, "a lowered hand: no cue");
    /* a hold, then let go at the wall: no cue (it never left); squeezed or holding: no probe */
    r.in.hand[1].p = (sv_vec3){ 20, 150, 85 };
    r.in.hand[0].p = (sv_vec3){ -20, 150, 20 };
    frames(&r, 45);
    grip(&r, 1, 1);
    rig_frame(&r);
    CHECK(r.s.h[1].holding, "a hold");
    uint64_t cr = r.s.cue_rays;
    r.in.hand[0].valid = 0; /* the other hand untracked: not probed */
    frames(&r, 10);
    CHECK(r.s.cue_rays == cr, "holding: no cue rays (%llu)", (unsigned long long)(r.s.cue_rays - cr));
    r.in.hand[0].valid = 1;
    frames(&r, 10);
    CHECK(r.s.cue_rays > cr, "climbing: the other open hand is probed at any height");
    grip(&r, 1, 0);
    nears = 0;
    for (int i = 0; i < 30; i++) rig_frame(&r), nears += (r.o.events & SOMVR_CLIMB_E_NEAR_R) != 0;
    CHECK(nears == 0, "let go at the wall: no cue");
    /* cue off: no probes */
    rig_init(&r);
    frames(&r, 30);
    CHECK(r.s.cue_rays == 0 && r.s.nears == 0, "cue 0: nothing");
}

int main(void) {
    test_pieces();
    test_size();
    test_push();
    test_grab();
    test_motion();
    test_hand_over_hand();
    test_ends();
    test_top();
    test_sequence();
    test_hang();
    test_reach();
    printf("test_climb: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
