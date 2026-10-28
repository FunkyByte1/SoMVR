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
static float g_top_depth = 100; /* the top's depth from the face (build 16's pullover: a shallow top has no landing) */
static int g_wet, g_top_far, g_desc_all, g_deep; /* g_deep: a floor 50 m down (A's ground with descent) */ /* descent: the floor is water; the top goes on to z 1000; rig_init: descent on */

static float g_slab_y0, g_slab_y1, g_slab_z; /* > 0: a slab out from the face: y0..y1 high, from z slab_z to the face */
static float g_roof_y, g_roof_z0, g_roof_z1; /* > 0: a roof at y roof_y over z roof_z0..roof_z1 (rays up from under it) */
static int mock_ray(void *ctx, int hand, sv_vec3 from, sv_vec3 dir, float len, somvr_climb_hit *out) {
    (void)ctx, (void)hand;
    g_rays++;
    if (g_ray_off) return 0;
    float best = 1e30f;
    memset(out, 0, sizeof *out);
    if (g_roof_y > 0 && dir.y > 1e-6f && from.y < g_roof_y && from.z >= g_roof_z0 && from.z <= g_roof_z1) {
        float t = (g_roof_y - from.y) / dir.y;
        if (t <= len)
            best = t, out->hit = 1, out->climbable = 1, out->dist = t,
            out->point = (sv_vec3){ from.x + dir.x * t, g_roof_y, from.z + dir.z * t };
    }
    if (g_slab_y1 > g_slab_y0) { /* the slab: a box (x -500..500), entered from outside */
        float lo[3] = { -500, g_slab_y0, g_slab_z }, hi[3] = { 500, g_slab_y1, WALL_Z }, o3[3] = { from.x, from.y, from.z },
              d3[3] = { dir.x, dir.y, dir.z }, t0 = 0, t1 = len;
        int ok = 1;
        for (int a = 0; a < 3 && ok; a++) {
            if (fabsf(d3[a]) < 1e-6f) {
                if (o3[a] < lo[a] || o3[a] > hi[a]) ok = 0;
                continue;
            }
            float ta = (lo[a] - o3[a]) / d3[a], tb = (hi[a] - o3[a]) / d3[a];
            if (ta > tb) { float x = ta; ta = tb, tb = x; }
            t0 = fmaxf(t0, ta), t1 = fminf(t1, tb);
            if (t0 > t1) ok = 0;
        }
        if (ok && t0 > 0 && t0 <= len)
            best = t0, out->hit = 1, out->climbable = 1, out->dist = t0,
            out->point = (sv_vec3){ from.x + dir.x * t0, from.y + dir.y * t0, from.z + dir.z * t0 };
    }
    if (dir.z > 1e-6f && from.z <= WALL_Z) { /* the face, from the player's side */
        float t = (WALL_Z - from.z) / dir.z;
        if (out->hit && t >= best) t = -1; /* the slab before it */
        sv_vec3 p = { from.x + dir.x * t, from.y + dir.y * t, WALL_Z };
        if (t >= 0 && t <= len && p.y >= 0 && p.y <= g_wall_top && p.x > -500 && p.x < 500 && t < best)
            best = t, out->hit = 1, out->climbable = p.x < ROCK_X, out->dist = t, out->point = p;
    }
    if (g_ground_on && dir.y < -1e-6f && from.y >= 0 && from.z < WALL_Z) { /* the floor before the wall */
        float t = -from.y / dir.y;
        if (t >= 0 && t <= len && t < best)
            best = t, out->hit = 1, out->climbable = 1, out->dist = t, out->wet = g_wet,
            out->point = (sv_vec3){ from.x + dir.x * t, 0, from.z + dir.z * t };
    }
    if (g_deep && dir.y < -1e-6f) { /* the ground far below (descent's A finds ground: the drop as before) */
        float t = (from.y + 5000) / -dir.y;
        if (t >= 0 && t <= len && t < best)
            best = t, out->hit = 1, out->climbable = 1, out->dist = t, out->wet = 0,
            out->point = (sv_vec3){ from.x + dir.x * t, -5000, from.z + dir.z * t };
    }
    if (dir.y < -1e-6f && from.y >= g_wall_top && !g_no_top) { /* the top, from above */
        float t = (g_wall_top - from.y) / dir.y;
        sv_vec3 p = { from.x + dir.x * t, g_wall_top, from.z + dir.z * t };
        if (t >= 0 && t <= len && p.z >= WALL_Z && p.z <= WALL_Z + (g_top_far ? 900 : g_top_depth) && t < best)
            best = t, out->hit = 1, out->climbable = 1, out->dist = t, out->point = p, out->wet = 0;
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
    float pending_thr;   /* ... its through part (cm; taken out of pending.y) */
    float pending_thx, pending_thz; /* ... its level through part (topout; taken out of pending.x/z) */
    int corner;          /* topout: the top's corner holds every move while the capsule is at the face (root z over
                          * WALL_Z - 35) with the feet (body y) more than 5 cm under the top (run 052042's stall) */
    int apply;           /* 1: requests move the body */
    sv_vec3 moved;       /* the sum of the applied moves */
    float head_track_y;  /* the head in tracking space */
    float head_track_z;  /* ... its z (50: 50 cm from the wall at body z 0) */
    int ledge;           /* Talion's capsule at the wall: the root (body + (0, 100, 50)) stays 35 cm off the face (z 65)
                          * while the capsule's foot (100 cm under the root) is more than 40 cm under the top */
    float ceil_y;        /* > 0: an overhang: the head (body + head_track_y) never goes over it */
    float ceil_reach;    /* > 0: ... only while the root's z (body z + 50) is over this (a lip: out from it, free) */
    float side_x;        /* > 0: a side wall: the body's x never goes over it */
    int edge_stop;       /* descent: the game keeps a standing Talion (body y on the top) 45 cm off its edge (root z) */
    int top_rest;        /* descent: the capsule (35 cm about the root) rests on the top while it overlaps it (root z
                          * over WALL_Z - 35), and under the top stays off the face (root z at most WALL_Z - 35) */
    int foot_lip;        /* through (thr_top): a lip under the top catches the capsule's foot (body y) 46 cm under it */
    int slab;            /* through: the slab (g_slab_*) holds the capsule's top (the head + 20) under it while the root
                          * (body z + 50) is within 35 cm of its front; the request's through part passes; in.rock_cm
                          * -10 while the capsule (the head + 20 down to the feet) overlaps it so, else +5 */
    double t;
    double dt;           /* > 0: the frame's time (default 1/90 s) */
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
    r->c.descent = g_desc_all;
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
        if (r->side_x > 0 && r->body.x + p.x > r->side_x) p.x = fmaxf(0.0f, r->side_x - r->body.x); /* the side wall */
        if (r->edge_stop && r->body.y + p.y < g_wall_top + 1.0f && r->body.z + 50 + p.z < WALL_Z + 45)
            p.z = fmaxf(p.z, fminf(WALL_Z + 45 - 50 - r->body.z, 0.0f)); /* no closer to the edge (z 145) */
        if (r->top_rest && r->body.y + p.y < g_wall_top - 0.01f) {
            if (r->body.y >= g_wall_top - 0.01f && r->body.z + 50 + p.z > WALL_Z - 35)
                p.y = g_wall_top - r->body.y; /* on the top: the capsule still over it */
            else if (r->body.z + 50 + p.z > WALL_Z - 35)
                p.z = WALL_Z - 35 - 50 - r->body.z; /* under it: against the face */
        }
        if (r->ceil_y > 0 && r->body.y + p.y + r->head_track_y > r->ceil_y &&
            !(r->ceil_reach > 0 && r->body.z + p.z + 50 <= r->ceil_reach))
            p.y = fmaxf(0.0f, r->ceil_y - r->head_track_y - r->body.y); /* the overhang stops the head */
        if (r->slab && r->body.z + 50 > g_slab_z - 35) { /* the capsule (head + 20 to the feet) under or in the slab */
            float top = r->body.y + r->head_track_y + 20, gap = g_slab_y0 - top;
            if (top <= g_slab_y0 + 0.01f && p.y > gap) p.y = fmaxf(gap, 0.0f); /* under it: up to its underside */
            else if (top > g_slab_y0 + 0.01f && r->body.y < g_slab_y1 && p.y > 0) p.y = 0; /* in it: held */
        }
        if (r->foot_lip && r->body.y < g_wall_top - 10 && r->body.y + p.y > g_wall_top - 46 && p.y > 0)
            p.y = fmaxf(0.0f, g_wall_top - 46 - r->body.y); /* the foot caught under the lip */
        if (r->corner && r->body.z + 50 > WALL_Z - 35 - 0.01f && r->body.y < g_wall_top - 5)
            p = (sv_vec3){ 0, 0, 0 }; /* the corner: nothing moves */
        p.y += r->pending_thr; /* the through part: past any collision */
        p.x += r->pending_thx, p.z += r->pending_thz;
        r->body.x += p.x, r->body.y += p.y, r->body.z += p.z;
        r->moved.x += p.x, r->moved.y += p.y, r->moved.z += p.z;
    }
    r->pending = (sv_vec3){ 0, 0, 0 }, r->pending_thr = 0, r->pending_thx = r->pending_thz = 0;
    r->t += r->dt > 0 ? r->dt : 1.0 / 90;
    if (r->slab) {
        float top = r->body.y + r->head_track_y + 20, feet = r->body.y;
        int in = r->body.z + 50 > g_slab_z - 35 && top > g_slab_y0 + 0.5f && feet < g_slab_y1;
        r->in.rock_ok = 1, r->in.rock_cm = in ? -10.0f : 5.0f;
    }
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
    if (r->o.move) {
        r->pending = r->o.move_w, r->pending_thr = r->o.through_up, r->pending.y -= r->o.through_up;
        r->pending_thx = r->o.through_lx, r->pending_thz = r->o.through_lz;
        r->pending.x -= r->o.through_lx, r->pending.z -= r->o.through_lz;
    }
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
    CHECK(!r.o.active && r.s.misses == 1, "standing: no down ray (misses %llu; descent too: a raised hand)",
          (unsigned long long)r.s.misses);
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

/* ---- build 13's fling (re-xr-pipeline.md 32.14) ------------------------------------------------------------------- */
/* a climb with the right hand (hang, fling on), then n frames of the hand moving (dx dy dz) a frame and one more with
 * the grip open while it still moves (the release) */
static void fl_setup(rig_t *r) {
    rig_init(r);
    r->c.letgo = SOMVR_CLIMB_LETGO_HANG, r->c.fling = 1;
    grip(r, 1, 1);
    rig_frame(r);
}
static void fl_throw(rig_t *r, int h, float dx, float dy, float dz, int n) {
    for (int i = 0; i < n; i++) hand_move(r, h, dx, dy, dz), rig_frame(r);
    hand_move(r, h, dx, dy, dz);
    grip(r, h, 0);
    rig_frame(r);
}
/* frames until the flight ends (at most n); the frames flown */
static int fl_fly(rig_t *r, int n) {
    int i = 0;
    while (i < n && r->s.phase == SOMVR_CLIMB_PH_FLING && r->o.active) rig_frame(r), i++;
    return i;
}

static void test_fling(void) {
    somvr_climb_cfg dc;
    somvr_climb_cfg_default(&dc);
    CHECK(!dc.fling && dc.fling_min == 150 && dc.fling_gain == 1 && dc.fling_max == 500 && dc.fling_side == 0.5f &&
              dc.fling_g == 981 && NEAR(dc.fling_win_s, 0.1, 1e-6) && NEAR(dc.fling_s, 1.5, 1e-6),
          "the defaults: off, 150 cm/s, x1, 500 cm/s, side 0.5, 981, 100 ms, 1.5 s");
    /* the rule alone: only up; the side damped and at most half the up; away dropped; toward kept up to the up; cap */
    somvr_climb s;
    somvr_climb_init(&s);
    sv_vec3 v, tw = { 0, 0, 1 };
    CHECK(somvr_climb_fling_rule(&s, &dc, (sv_vec3){ 0, 300, 0 }, tw, &v) == 1 && NEAR3(v, 0, 300, 0, 1e-3), "rule: straight up");
    CHECK(somvr_climb_fling_rule(&s, &dc, (sv_vec3){ 100, 300, -200 }, tw, &v) == 1 && NEAR3(v, 50, 300, 0, 1e-3) &&
              NEAR(s.fl_out, 200, 1e-3) && NEAR(s.fl_side, 50, 1e-3),
          "rule: away dropped, side x0.5: %g %g %g", v.x, v.y, v.z);
    CHECK(somvr_climb_fling_rule(&s, &dc, (sv_vec3){ 400, 200, 0 }, tw, &v) == 1 && NEAR3(v, 100, 200, 0, 1e-3),
          "rule: side at most half the up: %g", v.x);
    CHECK(somvr_climb_fling_rule(&s, &dc, (sv_vec3){ 0, 200, 300 }, tw, &v) == 1 && NEAR3(v, 0, 200, 200, 1e-3) &&
              NEAR(s.fl_into, 300, 1e-3),
          "rule: toward kept up to the up: %g", v.z);
    CHECK(somvr_climb_fling_rule(&s, &dc, (sv_vec3){ 0, 800, 0 }, tw, &v) == 1 && NEAR3(v, 0, 500, 0, 1e-3) && s.fl_capped,
          "rule: capped at 500");
    CHECK(somvr_climb_fling_rule(&s, &dc, (sv_vec3){ 0, 149, 0 }, tw, &v) == 0 && NEAR3(v, 0, 0, 0, 0) && NEAR(s.fl_up, 149, 1e-3),
          "rule: under 150: none");
    CHECK(somvr_climb_fling_rule(&s, &dc, (sv_vec3){ 0, -400, -300 }, tw, &v) == 0 && NEAR(s.fl_out, 300, 1e-3),
          "rule: down and away: none");
    CHECK(somvr_climb_fling_rule(&s, &dc, (sv_vec3){ 300, 300, 0 }, (sv_vec3){ 0, 0, 0 }, &v) == 1 &&
              NEAR3(v, 0, 300, 0, 1e-3),
          "rule: no wall direction: only the up");
    CHECK(somvr_climb_fling_rule(&s, &dc, (sv_vec3){ NAN, 300, 0 }, tw, &v) == 0, "rule: NaN: none");
    CHECK(!strcmp(somvr_climb_phase_name(SOMVR_CLIMB_PH_FLING), "fling") &&
              !strcmp(somvr_climb_fl_name(SOMVR_CLIMB_FL_APEX), "the arc's top") && !strcmp(somvr_climb_fl_name(9), "?"),
          "the names");

    /* off (the default): a fast release hangs as build 10 */
    rig_t r;
    rig_init(&r);
    r.c.letgo = SOMVR_CLIMB_LETGO_HANG;
    grip(&r, 1, 1);
    rig_frame(&r);
    fl_throw(&r, 1, 0, -4, 0, 10);
    sv_vec3 m0 = r.moved;
    frames(&r, 60);
    CHECK(r.s.phase == SOMVR_CLIMB_PH_HANG && r.s.flings == 0 && NEAR(r.moved.y, m0.y, 1e-3) && r.s.fl_nones == 0,
          "off: a fast release hangs (%g)", r.moved.y - m0.y);

    /* on: 360 cm/s up at the release: the arc rises v^2/2g = 66 cm, slowing, then hangs at its apex; 2 s more: still */
    fl_setup(&r);
    fl_throw(&r, 1, 0, -4, 0, 10);
    CHECK((r.o.events & SOMVR_CLIMB_E_FLING) && (r.o.events & SOMVR_CLIMB_E_LET_R) && r.s.phase == SOMVR_CLIMB_PH_FLING &&
              r.s.flings == 1 && NEAR(r.s.fl_v0.y, 360, 0.5) && NEAR(r.s.fl_v0.x, 0, 1e-3) && NEAR(r.s.fl_v0.z, 0, 1e-3) &&
              r.s.fl_hand == 1 && r.s.fl_n >= 9 && r.s.fl_span >= 0.09f && r.s.fl_span <= 0.125f && r.o.move,
          "launch: v0 %g (n %d, span %g)", r.s.fl_v0.y, r.s.fl_n, r.s.fl_span);
    float y0 = r.body.y; /* the release frame moves nothing (let go first); its flight piece lands next frame */
    float vlast = 1e9f;
    int slower = 1, nf = 0;
    while (r.s.phase == SOMVR_CLIMB_PH_FLING && nf < 200) {
        rig_frame(&r), nf++;
        float vy = r.o.move ? r.o.move_w.y * 90 : 0;
        if (r.s.phase == SOMVR_CLIMB_PH_FLING && vy > vlast + 1e-3f) slower = 0;
        vlast = vy;
        CHECK(!r.o.move || (r.o.move_w.y >= 0 && r.o.move_w.y <= 360.0f / 90 + 1e-3f), "flight frame %d: %g", nf, r.o.move_w.y);
    }
    rig_frame(&r); /* the apex piece lands */
    CHECK(r.s.phase == SOMVR_CLIMB_PH_HANG && r.s.fl_why == SOMVR_CLIMB_FL_APEX && r.s.apexes == 1 && slower &&
              nf >= 30 && nf <= 36 && NEAR(r.body.y - y0, 360.0 * 360.0 / (2 * 981), 0.8),
          "the arc: %d frames, rose %g (v^2/2g %g), apex hang", nf, r.body.y - y0, 360.0 * 360.0 / (2 * 981));
    m0 = r.moved;
    frames(&r, 180);
    CHECK(r.o.active && r.s.phase == SOMVR_CLIMB_PH_HANG && NEAR3(r.moved, m0.x, m0.y, m0.z, 1e-4), "2 s at the apex: no fall");
    r.in.drop = 1;
    rig_frame(&r);
    CHECK(r.o.ended && r.o.end_why == SOMVR_CLIMB_END_DROP, "A at the apex: the drop");
    r.in.drop = 0;

    /* a slow release (90 cm/s): today's hang, the none event; a pull that stopped 150 ms before the release: the hang */
    fl_setup(&r);
    fl_throw(&r, 1, 0, -1, 0, 20);
    m0 = r.moved;
    frames(&r, 30);
    CHECK(r.s.phase == SOMVR_CLIMB_PH_HANG && r.s.flings == 0 && r.s.fl_nones == 1 && NEAR(r.s.fl_up, 90, 0.5) &&
              NEAR(r.moved.y, m0.y, 1e-3),
          "slow: hang (up %g)", r.s.fl_up);
    fl_setup(&r);
    for (int i = 0; i < 10; i++) hand_move(&r, 1, 0, -4, 0), rig_frame(&r);
    frames(&r, 14);
    fl_throw(&r, 1, 0, 0, 0, 0);
    CHECK(r.s.phase == SOMVR_CLIMB_PH_HANG && r.s.flings == 0 && r.s.fl_nones == 0 && NEAR(r.s.fl_up, 0, 1e-3),
          "stopped, then let go: hang (up %g)", r.s.fl_up);
    /* away from the wall (the hand pushed toward it): no fling; down (the hand raised): none; up and away: up only */
    fl_setup(&r);
    fl_throw(&r, 1, 0, 0, 4, 10);
    m0 = r.moved;
    frames(&r, 30);
    CHECK(r.s.phase == SOMVR_CLIMB_PH_HANG && r.s.flings == 0 && r.s.fl_nones == 1 && NEAR(r.s.fl_out, 360, 0.5) &&
              NEAR3(r.moved, m0.x, m0.y, m0.z, 1e-4),
          "away: none (out %g), not moved", r.s.fl_out);
    fl_setup(&r);
    fl_throw(&r, 1, 0, 4, 0, 10);
    m0 = r.moved;
    frames(&r, 30);
    CHECK(r.s.phase == SOMVR_CLIMB_PH_HANG && r.s.flings == 0 && NEAR(r.s.fl_up, -360, 0.5) && NEAR3(r.moved, m0.x, m0.y, m0.z, 1e-4),
          "down: none, not moved");
    fl_setup(&r);
    fl_throw(&r, 1, 0, -4, 3, 10);
    float z0 = r.body.z;
    CHECK(r.s.phase == SOMVR_CLIMB_PH_FLING && NEAR(r.s.fl_out, 270, 0.5) && NEAR3(r.s.fl_v0, 0, 360, 0, 0.5),
          "up and away: the away part dropped (out %g)", r.s.fl_out);
    fl_fly(&r, 200);
    rig_frame(&r);
    CHECK(r.s.phase == SOMVR_CLIMB_PH_HANG && NEAR(r.body.z, z0, 1e-3), "up and away: flew straight up (z %g from %g)", r.body.z, z0);
    /* sideways: x0.5, at most half the up */
    fl_setup(&r);
    fl_throw(&r, 1, -4, -4, 0, 10);
    CHECK(NEAR3(r.s.fl_v0, 180, 360, 0, 0.5), "sideways: %g", r.s.fl_v0.x);
    float x0 = r.body.x;
    nf = fl_fly(&r, 200);
    rig_frame(&r);
    CHECK(r.s.phase == SOMVR_CLIMB_PH_HANG && NEAR(r.body.x - x0, 180.0 * 360 / 981, 2.5), "sideways: drifted %g (%d frames)",
          r.body.x - x0, nf);
    /* a side wall 10 cm to the side: the sideways part blocked stops it, the arc goes on up to the same apex */
    fl_setup(&r);
    r.side_x = 10;
    fl_throw(&r, 1, -4, -4, 0, 10);
    y0 = r.body.y;
    nf = fl_fly(&r, 200);
    rig_frame(&r);
    CHECK(r.s.phase == SOMVR_CLIMB_PH_HANG && r.s.fl_why == SOMVR_CLIMB_FL_APEX && NEAR(r.body.x, 10, 1e-3) &&
              NEAR(r.body.y - y0, 360.0 * 360 / 981 / 2, 1.0),
          "side wall: x %g, rose %g (%d frames)", r.body.x, r.body.y - y0, nf);
    r.side_x = 0;
    /* the catch: 10 frames into the flight the left hand squeezes at the wall: it holds, the flight stops at once */
    fl_setup(&r);
    fl_throw(&r, 1, 0, -4, 0, 10);
    frames(&r, 10);
    CHECK(r.s.phase == SOMVR_CLIMB_PH_FLING, "flying");
    r.in.hand[0].p = (sv_vec3){ -20, 150, 85 };
    grip(&r, 0, 1);
    rig_frame(&r);
    CHECK((r.o.events & SOMVR_CLIMB_E_CATCH) && (r.o.events & SOMVR_CLIMB_E_GRAB_L) && r.s.phase == SOMVR_CLIMB_PH_HOLD &&
              r.s.catches == 1 && r.s.fl_why == SOMVR_CLIMB_FL_CATCH && !r.o.move && r.o.driver == 0,
          "catch: held, no flight move");
    rig_frame(&r);
    m0 = r.moved;
    frames(&r, 20);
    CHECK(NEAR3(r.moved, m0.x, m0.y, m0.z, 1e-4) && r.s.phase == SOMVR_CLIMB_PH_HOLD, "after the catch: still (the hand still)");
    hand_move(&r, 0, 0, -3, 0);
    rig_frame(&r);
    CHECK(NEAR3(r.o.move_w, 0, 3, 0, 1e-4), "after the catch: the hold pulls");
    /* the other hand still holding: no fling; it lets go fast after: the fling (from its own drive) */
    fl_setup(&r);
    r.in.hand[0].p = (sv_vec3){ -20, 140, 85 };
    grip(&r, 0, 1);
    rig_frame(&r);
    fl_throw(&r, 0, 0, -4, 0, 10); /* the newest (L) drives and lets go: R still holds */
    CHECK(r.s.flings == 0 && r.s.phase == SOMVR_CLIMB_PH_HOLD && r.o.active, "one hand still holds: no fling");
    rig_frame(&r);
    fl_throw(&r, 1, 0, -4, 0, 10);
    CHECK(r.s.flings == 1 && r.s.phase == SOMVR_CLIMB_PH_FLING && r.s.fl_hand == 1 && NEAR(r.s.fl_v0.y, 360, 0.5),
          "then R's fast release: the fling (%g)", r.s.fl_v0.y);
    /* A in flight: the drop */
    frames(&r, 5);
    r.in.drop = 1;
    rig_frame(&r);
    CHECK(r.o.ended && r.o.end_why == SOMVR_CLIMB_END_DROP && r.s.drops == 1, "A in flight: drop");
    r.in.drop = 0;
    /* an overhang 30 cm over the head: blocked above (twice) = the apex hang there */
    fl_setup(&r);
    r.ceil_y = 170 + 40 + 30;
    fl_throw(&r, 1, 0, -4, 0, 10);
    nf = fl_fly(&r, 200);
    CHECK(r.s.phase == SOMVR_CLIMB_PH_HANG && r.s.fl_why == SOMVR_CLIMB_FL_BLOCKED && r.s.fl_blocks == 1 && nf < 20 &&
              NEAR(r.in.head_y, r.ceil_y, 1e-3),
          "overhang: blocked after %d frames, the head at %g", nf, r.in.head_y);
    m0 = r.moved;
    frames(&r, 30);
    CHECK(NEAR3(r.moved, m0.x, m0.y, m0.z, 1e-4), "overhang: hanging still");
    r.ceil_y = 0;
    /* the menu in flight: nothing moves, the clocks stop; after it the arc goes on to the same apex */
    fl_setup(&r);
    fl_throw(&r, 1, 0, -4, 0, 10);
    y0 = r.body.y;
    frames(&r, 8);
    r.in.frozen = 1;
    rig_frame(&r); /* the last flight piece lands */
    m0 = r.moved;
    frames(&r, 200);
    CHECK(r.s.phase == SOMVR_CLIMB_PH_FLING && NEAR3(r.moved, m0.x, m0.y, m0.z, 1e-3), "the menu: the flight frozen");
    r.in.frozen = 0;
    fl_fly(&r, 200);
    rig_frame(&r);
    CHECK(r.s.phase == SOMVR_CLIMB_PH_HANG && r.s.fl_why == SOMVR_CLIMB_FL_APEX && NEAR(r.body.y - y0, 66.06, 1.0),
          "after the menu: the same apex (%g)", r.body.y - y0);
    /* the input lost in flight: the hang (disarmed) */
    fl_setup(&r);
    fl_throw(&r, 1, 0, -4, 0, 10);
    frames(&r, 3);
    r.in.live = 0;
    rig_frame(&r);
    CHECK(r.o.active && r.s.phase == SOMVR_CLIMB_PH_HANG && !r.o.move, "input lost in flight: hang");
    r.in.live = 1;
    /* the ground under the root at the release: released (no fling); letgo = fall: released (the fall) */
    fl_setup(&r);
    g_ground_on = 1;
    r.body.y = -30; /* the root 70 cm over the floor */
    fl_throw(&r, 1, 0, -4, 0, 4);
    g_ground_on = 0;
    CHECK(r.o.ended && r.o.end_why == SOMVR_CLIMB_END_RELEASED && r.s.flings == 0, "on the ground: released, no fling");
    fl_setup(&r);
    r.c.letgo = SOMVR_CLIMB_LETGO_FALL;
    fl_throw(&r, 1, 0, -4, 0, 10);
    CHECK(r.o.ended && r.o.end_why == SOMVR_CLIMB_END_RELEASED && r.s.flings == 0, "letgo fall: released, no fling");
    /* the player size: 1.2 x the hand's speed (as the pull); the gain */
    fl_setup(&r);
    r.scale = 1.2f;
    fl_throw(&r, 1, 0, -3, 0, 10);
    CHECK(NEAR(r.s.fl_v0.y, 1.2 * 270, 0.5), "size 1.2: %g", r.s.fl_v0.y);
    fl_setup(&r);
    r.c.fling_gain = 1.3f;
    fl_throw(&r, 1, 0, -3, 0, 10);
    CHECK(NEAR(r.s.fl_v0.y, 1.3 * 270, 0.5), "gain 1.3: %g", r.s.fl_v0.y);
    /* too short a drive (2 frames: 22 ms): no velocity, the hang */
    fl_setup(&r);
    fl_throw(&r, 1, 0, -4, 0, 1);
    CHECK(r.s.flings == 0 && r.s.phase == SOMVR_CLIMB_PH_HANG, "a 22 ms drive: hang");
    /* the hands' located time is the clock: a stalled frame (a dump, 200 ms of wall time) before the release does not
     * stretch the window; without the located time the stall leaves no sample within the window: no velocity */
    fl_setup(&r);
    for (int i = 0; i < 10; i++) {
        r.in.pose_t = 500 + i / 90.0;
        hand_move(&r, 1, 0, -4, 0), rig_frame(&r);
    }
    r.t += 0.2; /* the stall: the wall clock jumps, the next located time is one frame on */
    r.in.pose_t = 500 + 10 / 90.0;
    fl_throw(&r, 1, 0, -4, 0, 0);
    CHECK(r.s.flings == 1 && NEAR(r.s.fl_v0.y, 360, 0.5), "a stall before the release, the located time: %g", r.s.fl_v0.y);
    r.in.pose_t = 0;
    fl_setup(&r);
    for (int i = 0; i < 10; i++) hand_move(&r, 1, 0, -4, 0), rig_frame(&r);
    r.t += 0.2;
    fl_throw(&r, 1, 0, -4, 0, 0);
    CHECK(r.s.flings == 0 && r.s.fl_none == -1 && (r.o.events & SOMVR_CLIMB_E_FLING_NONE), "a stall, the wall clock: no velocity");
    /* a tracking jump in the release frame: the ring alone (the jump not counted) */
    fl_setup(&r);
    for (int i = 0; i < 10; i++) hand_move(&r, 1, 0, -4, 0), rig_frame(&r);
    hand_move(&r, 1, 0, -60, 0);
    grip(&r, 1, 0);
    rig_frame(&r);
    CHECK(r.s.flings == 1 && NEAR(r.s.fl_v0.y, 360, 0.5), "a jump at the release: the ring's %g", r.s.fl_v0.y);

    /* the top in flight: the face's top at 300; the hold at 210 (no edge), the head 270 at the release; 450 up and 225
     * toward the wall: the head's ray loses the face at 300, the top found by the ray down (300), topped; the capsule
     * blocked by the face (ledge) until its foot clears 260: the edge step lifts by the blocked part; then over */
    fl_setup(&r);
    r.ledge = 1;
    r.c.over_cm = 10;
    r.body.y = 60;
    rig_frame(&r);
    fl_throw(&r, 1, 0, -5, -2.5f, 10);
    CHECK(r.s.phase == SOMVR_CLIMB_PH_FLING && NEAR3(r.s.fl_v0, 0, 447.2, 223.6, 0.5) && r.s.fl_capped &&
              r.s.fl_wall_d > 0 && !r.s.fl_edge,
          "top: launch %g %g %g, the wall %g", r.s.fl_v0.x, r.s.fl_v0.y, r.s.fl_v0.z, r.s.fl_wall_d);
    int topped_at = -1, ramp_frames = 0;
    for (nf = 0; nf < 200 && r.o.active && r.s.phase == SOMVR_CLIMB_PH_FLING; nf++) {
        rig_frame(&r);
        if ((r.o.events & SOMVR_CLIMB_E_FLING_TOP) && topped_at < 0) topped_at = nf;
        ramp_frames += r.o.ramp > 0;
    }
    CHECK(topped_at >= 0 && r.s.fl_top_by == 2 && NEAR(r.s.top_y, 300, 1e-3) && ramp_frames > 0 && r.o.ended &&
              r.o.end_why == SOMVR_CLIMB_END_OVER && r.s.fl_why == SOMVR_CLIMB_FL_OVER && r.s.fl_overs == 1 &&
              r.body.y >= 260 - 1e-3 && r.s.fl_glides == 1,
          "top: topped at frame %d (by %d, top %g), %d edge-step frames, %s after %d frames, body %g %g, glides %llu",
          topped_at, r.s.fl_top_by, r.s.top_y, ramp_frames, somvr_climb_end_name(r.o.end_why), nf, r.body.y, r.body.z,
          (unsigned long long)r.s.fl_glides);
    printf("fling top: topped at frame %d, %d edge-step frames (%.1f cm), over after %d frames, body y %.1f z %.1f\n", topped_at,
           ramp_frames, r.s.ramp_cm, nf, r.body.y, r.body.z);
    r.ledge = 0;
    /* a weak fling under the top: the apex hang, topped (no ground test); a hand on the top and a push go on as today */
    fl_setup(&r);
    r.ledge = 1;
    r.body.y = 60;
    rig_frame(&r);
    fl_throw(&r, 1, 0, -2.5f, -1, 8);
    nf = fl_fly(&r, 200);
    CHECK(r.s.phase == SOMVR_CLIMB_PH_HANG && r.o.active &&
              (r.s.fl_why == SOMVR_CLIMB_FL_APEX || r.s.fl_why == SOMVR_CLIMB_FL_EDGE),
          "a weak fling at the top: the hang (%s, topped %d, body %g %g)", somvr_climb_fl_name(r.s.fl_why), r.s.topped, r.body.y,
          r.body.z);
    printf("fling weak: %s after %d frames, topped %d, body y %.1f z %.1f\n", somvr_climb_fl_name(r.s.fl_why), nf, r.s.topped,
           r.body.y, r.body.z);
    /* the glide blocked at the edge: topped (the eyes over the top) but the capsule's foot still under the step (the
     * mock's ledge at 330 here): the forward part blocked = the hang */
    fl_setup(&r);
    r.ledge = 1;
    r.body.y = 60;
    rig_frame(&r);
    g_wall_top = 300;
    fl_throw(&r, 1, 0, -3.5f, -2, 10);
    nf = fl_fly(&r, 200);
    CHECK(r.s.phase == SOMVR_CLIMB_PH_HANG && r.o.active && r.s.topped && r.body.y < 260 &&
              r.s.fl_why == SOMVR_CLIMB_FL_EDGE && r.s.fl_glides == 1 && NEAR(r.s.fl_glide_lift, r.s.fl_glide_cap, 1e-3) &&
              r.s.fl_glide_cap >= 50 && r.s.fl_glide_cap <= 80,
          "glide blocked: %s (glides %llu, lifted %g, body %g %g)", somvr_climb_fl_name(r.s.fl_why),
          (unsigned long long)r.s.fl_glides, r.s.fl_glide_lift, r.body.y, r.body.z);
    /* the same 40 cm higher: the glide's edge step lifts the capsule's foot over the edge (under 50 cm), then over */
    fl_setup(&r);
    r.ledge = 1;
    r.body.y = 100;
    rig_frame(&r);
    fl_throw(&r, 1, 0, -3.5f, -2, 10);
    nf = fl_fly(&r, 200);
    CHECK(r.o.ended && r.o.end_why == SOMVR_CLIMB_END_OVER && r.s.fl_glides == 1 && r.s.fl_glide_lift > 1 &&
              r.s.fl_glide_lift < 50 && r.body.y >= 260 - 1e-3 && r.s.fl_landed && r.body.z + 50 >= WALL_Z,
          "glide over the edge: %s (lifted %g, body %g %g)", somvr_climb_end_name(r.o.end_why), r.s.fl_glide_lift, r.body.y,
          r.body.z);
    printf("fling glide over the edge: %s, glides %llu, body y %.1f z %.1f\n", somvr_climb_fl_name(r.s.fl_why),
           (unsigned long long)r.s.fl_glides, r.body.y, r.body.z);
    /* the same flown at 10 fps (a headless dump's slow frames, runs 030713 / 031623): the edge step's cap scales with
     * the frame (step_cm per 50 ms), so the rise and the glide lift as at 20 Hz: over, not the edge's hang */
    for (int k = 0; k < 4; k++) {
        int fps = k == 0 ? 20 : 10;
        fl_setup(&r);
        r.ledge = 1;
        r.body.y = k < 2 ? 100 : k == 2 ? 80 : 70;
        rig_frame(&r);
        fl_throw(&r, 1, 0, -3.5f, -2, 10);
        r.dt = 1.0 / fps;
        nf = fl_fly(&r, 60);
        CHECK(r.o.ended && r.o.end_why == SOMVR_CLIMB_END_OVER && r.s.fl_glides == 1 && r.body.y >= 260 - 1e-3,
              "glide over the edge at %d fps from %d: %s / %s after %d frames (lifted %g of %g, ramp %g, body %g %g)", fps,
              k < 2 ? 100 : k == 2 ? 80 : 70,
              somvr_climb_end_name(r.o.end_why), somvr_climb_fl_name(r.s.fl_why), nf, r.s.fl_glide_lift, r.s.fl_glide_cap,
              r.s.ramp_cm,
              r.body.y, r.body.z);
        printf("fling glide over the edge at %d fps from %d: %s after %d frames, lifted %.1f of %.1f, body y %.1f z %.1f\n",
               fps, k < 2 ? 100 : k == 2 ? 80 : 70, somvr_climb_fl_name(r.s.fl_why), nf, r.s.fl_glide_lift, r.s.fl_glide_cap,
               r.body.y, r.body.z);
        r.dt = 0;
    }
    r.ledge = 0;
    /* the release hold's found top (an edge hold 10 cm under the top): topped by it once the head is 10 cm over it */
    fl_setup(&r);
    grip(&r, 1, 0);
    rig_frame(&r);
    r.body.y = 60;
    r.in.hand[1].p = (sv_vec3){ 20, 225, 85 };
    rig_frame(&r);
    grip(&r, 1, 1);
    rig_frame(&r);
    CHECK(r.s.h[1].holding && r.s.h[1].edge && r.s.h[1].top_found, "an edge hold");
    fl_throw(&r, 1, 0, -4, 0, 10);
    CHECK(r.s.phase == SOMVR_CLIMB_PH_FLING && r.s.fl_edge && NEAR(r.s.fl_top_y, 300, 1e-3) && r.s.fl_wall_d < 0,
          "the edge hold's top kept (%g)", r.s.fl_top_y);
    for (nf = 0; nf < 200 && r.s.phase == SOMVR_CLIMB_PH_FLING; nf++) rig_frame(&r);
    CHECK(r.s.fl_tops == 1 && r.s.fl_top_by == 1 && r.s.topped, "topped by the hold's top");
}

/* ---- descent (re-xr-pipeline.md 40): Talion on the wall's top (feet at the top, the root at z 140), facing out (-z);
 * the hands' palms and fingers point away from the face (only the down and wall rays find it) ---- */
static void desc_setup(rig_t *r, float top) {
    g_wall_top = top, g_ground_on = 1, g_wet = 0, g_top_far = 0;
    rig_init(r);
    r->c.descent = 1, r->c.letgo = SOMVR_CLIMB_LETGO_HANG, r->c.foot_cm = 100, r->c.inside_cm = 30;
    r->top_rest = 1;
    r->body = (sv_vec3){ 0, top, 90 };
    r->in.can_blink = 1;
    for (int h = 0; h < 2; h++) {
        r->in.hand[h].p = (sv_vec3){ h ? 25.0f : -25.0f, 120, 60 };
        r->in.hand[h].palm = (sv_vec3){ h ? -1.0f : 1.0f, 0, 0 }, r->in.hand[h].fwd = (sv_vec3){ 0, 0, -1 };
    }
    rig_frame(r);
}
/* the right hand over the top 10 cm before the edge, 15 cm up, squeezed: the lip (1 = held) */
static int desc_lip(rig_t *r) {
    r->in.hand[1].p = (sv_vec3){ 20, 15, 20 }; /* world (20, top + 15, 110) */
    rig_frame(r);
    grip(r, 1, 1);
    rig_frame(r);
    return r->s.h[1].holding;
}
/* over the edge: the held hand pulled in 60 cm (the body out), then pushed up (n frames of 3 cm) */
static void desc_over(rig_t *r, int n) {
    for (int i = 0; i < 20; i++) hand_move(r, 1, 0, 0, 3), rig_frame(r);
    for (int i = 0; i < n; i++) hand_move(r, 1, 0, 3, 0), rig_frame(r);
}
/* one hold down the face: hand h 20 cm out from the face, 90 cm under the head; grip, the other opens, pushed up 75 cm */
static int desc_hold(rig_t *r, int h, int n) {
    sv_vec3 w = { h ? 20.0f : -20.0f, fminf(r->in.head_y - 90, g_wall_top - 20), 80 }; /* the face: under the top */
    r->in.hand[h].p = (sv_vec3){ w.x - r->body.x, w.y - r->body.y, w.z - r->body.z };
    rig_frame(r);
    grip(r, h, 1);
    rig_frame(r);
    if (!r->s.h[h].holding) return 0;
    grip(r, 1 - h, 0);
    rig_frame(r);
    for (int i = 0; i < n; i++) hand_move(r, h, 0, 3, 0), rig_frame(r);
    return 1;
}
static void press_a(rig_t *r) {
    r->in.drop = 1;
    rig_frame(r);
    somvr_climb_out o = r->o; /* the press's frame */
    r->in.drop = 0;
    rig_frame(r);
    if (!(r->o.events & SOMVR_CLIMB_E_BLINK)) r->o.events |= o.events; /* (its events kept for the checks) */
    r->o.blink |= o.blink, r->o.ended |= o.ended;
    if (o.blink) r->o.blink_to = o.blink_to;
}

static void test_descent(void) {
    rig_t r;
    /* the lip from above: a hold on the top (the down ray), its edge 20 cm along the reach, the ground 300 cm under */
    desc_setup(&r, WALL_TOP);
    CHECK(desc_lip(&r) && r.s.active && r.s.descending && (r.o.events & SOMVR_CLIMB_E_DESCENT) &&
              (r.o.events & SOMVR_CLIMB_E_LIP_R) && r.s.h[1].via == SOMVR_CLIMB_VIA_DOWN && r.s.lows == 0,
          "the lip from above: holding %d, descending %d, via %d, lows %llu", r.s.h[1].holding, r.s.descending,
          r.s.h[1].via, (unsigned long long)r.s.lows);
    CHECK(r.s.h[1].lip && NEAR(r.s.lip_top_y, WALL_TOP, 0.01) && NEAR3(r.s.lip_out, 0, 0, -1, 0.01) &&
              NEAR(r.s.lip_edge.z, 100, 6) && NEAR(r.s.h[1].lip_drop, 300, 6) && NEAR3(r.s.h[1].toward, 0, 0, 1, 0.01),
          "the lip: top %g, out (%g %g %g) (the face's own, not the diagonal reach), edge z %g, drop %g",
          r.s.lip_top_y, r.s.lip_out.x, r.s.lip_out.y, r.s.lip_out.z, r.s.lip_edge.z, r.s.h[1].lip_drop);
    CHECK(r.s.tops == 0 && !r.s.topped, "descending: no top (the head over the lip's top) %llu", (unsigned long long)r.s.tops);
    /* both open on the top: released (he stands) */
    grip(&r, 1, 0);
    rig_frame(&r);
    CHECK(!r.s.active && r.s.last_end == SOMVR_CLIMB_END_RELEASED && NEAR(r.body.y, WALL_TOP, 0.01), "let go on the top: %s",
          somvr_climb_end_name(r.s.last_end));
    /* again: over the edge (out 60, then down: the lip step, then the face) */
    desc_setup(&r, WALL_TOP);
    desc_lip(&r);
    float z0 = r.body.z;
    desc_over(&r, 20);
    CHECK(r.s.active && r.s.lip_steps > 0 && r.s.lip_cm > 0 && r.body.y < WALL_TOP - 30 && r.body.z + 50 <= WALL_Z - 35 + 0.01f,
          "over the edge: lip steps %llu (%.1f cm), body y %g z %g (moved out %g)", (unsigned long long)r.s.lip_steps,
          r.s.lip_cm, r.body.y, r.body.z, z0 - r.body.z);
    CHECK(r.s.tops == 0 && r.s.overs == 0, "no top, no over while going down");
    /* hand over hand down the face (the wall ray: palms and fingers point away) */
    int k, h = 0;
    for (k = 0; k < 4; k++, h = 1 - h)
        if (!desc_hold(&r, h, 15)) break;
    CHECK(k == 4 && r.s.h[1 - h].via == SOMVR_CLIMB_VIA_WALL && !r.s.descending && r.s.tops == 0 && r.s.jumps == 0,
          "four holds down the face (via %d), descending %d, tops %llu, body y %g", r.s.h[1 - h].via, r.s.descending,
          (unsigned long long)r.s.tops, r.body.y);
    float y1 = r.body.y;
    CHECK(y1 < WALL_TOP - 200, "down the face: body y %g", y1);
    /* both open: the hang; A: a short drop (the feet under blink_cm over the ground): the real fall */
    grip(&r, 0, 0), grip(&r, 1, 0);
    frames(&r, 2);
    y1 = r.body.y;
    frames(&r, 10);
    CHECK(r.s.active && r.s.phase == SOMVR_CLIMB_PH_HANG && NEAR(r.body.y, y1, 0.01), "the hang (y %g)", r.body.y);
    press_a(&r);
    CHECK(!r.s.active && r.s.last_end == SOMVR_CLIMB_END_DROP && r.s.bl_why == SOMVR_CLIMB_BL_SHORT && NEAR(r.s.bl_h, y1, 1) &&
              (r.o.events & SOMVR_CLIMB_E_BLINK_NONE) && !r.o.blink,
          "A, %g cm over the ground: %s", r.s.bl_h, somvr_climb_bl_name(r.s.bl_why));

    /* a tall wall (9 m): A in the hang = the blink, nothing moves until the teleport (somvr_climb_stop) */
    desc_setup(&r, 900);
    desc_lip(&r);
    desc_over(&r, 40);
    for (k = 0, h = 0; k < 3; k++, h = 1 - h) desc_hold(&r, h, 25);
    grip(&r, 0, 0), grip(&r, 1, 0);
    frames(&r, 3);
    sv_vec3 b0 = r.body, root = r.in.root;
    press_a(&r);
    CHECK(r.s.active && r.s.phase == SOMVR_CLIMB_PH_BLINK && r.o.blink && (r.o.events & SOMVR_CLIMB_E_BLINK) &&
              NEAR3(r.o.blink_to, root.x, 105, root.z - 40, 1) && NEAR(r.s.bl_h, root.y - 100, 1),
          "the blink: phase %s, to (%g %g %g), %g cm", somvr_climb_phase_name(r.s.phase), r.o.blink_to.x, r.o.blink_to.y,
          r.o.blink_to.z, r.s.bl_h);
    grip(&r, 0, 1); /* a grip during the blink: nothing */
    frames(&r, 30);
    CHECK(r.s.phase == SOMVR_CLIMB_PH_BLINK && NEAR3(r.body, b0.x, b0.y, b0.z, 0.01) && !r.s.h[0].holding && !r.o.blink,
          "still during the blink");
    somvr_climb_stop(&r.s, SOMVR_CLIMB_END_BLINK, &r.o);
    CHECK(!r.s.active && r.s.last_end == SOMVR_CLIMB_END_BLINK && r.o.ended, "the teleport: the end %s",
          somvr_climb_end_name(r.s.last_end));
    /* the teleport never comes: the hang after blink_wait_s */
    desc_setup(&r, 900);
    desc_lip(&r);
    desc_over(&r, 40);
    grip(&r, 1, 0);
    frames(&r, 2);
    press_a(&r);
    CHECK(r.s.phase == SOMVR_CLIMB_PH_BLINK, "blink (wait)");
    frames(&r, 300);
    CHECK(r.s.active && r.s.phase == SOMVR_CLIMB_PH_HANG && r.s.bl_waits == 1, "no teleport in 3 s: the hang (%s)",
          somvr_climb_phase_name(r.s.phase));
    /* A on the top with the lip held: from beyond the edge */
    desc_setup(&r, 900);
    desc_lip(&r);
    press_a(&r);
    CHECK(r.s.phase == SOMVR_CLIMB_PH_BLINK && NEAR(r.s.bl_h, 900, 1) && r.o.blink_to.z < WALL_Z - 30 &&
              NEAR(r.o.blink_to.y, 105, 1),
          "A on the top: the blink from beyond the edge (%g cm, to z %g)", r.s.bl_h, r.o.blink_to.z);
    /* no ground, water: no drop at all; no blink now: the drop */
    desc_setup(&r, 900);
    desc_lip(&r);
    desc_over(&r, 40);
    grip(&r, 1, 0);
    frames(&r, 2);
    g_ground_on = 0;
    press_a(&r);
    CHECK(r.s.active && r.s.phase == SOMVR_CLIMB_PH_HANG && r.s.bl_why == SOMVR_CLIMB_BL_NOGROUND &&
              (r.o.events & SOMVR_CLIMB_E_BLINK_NONE) && !r.o.ended,
          "no ground: he stays (%s)", somvr_climb_bl_name(r.s.bl_why));
    g_ground_on = 1, g_wet = 1;
    press_a(&r);
    CHECK(r.s.active && r.s.bl_why == SOMVR_CLIMB_BL_WET && !r.o.ended, "water: he stays");
    g_wet = 0, r.in.can_blink = 0;
    press_a(&r);
    CHECK(!r.s.active && r.s.last_end == SOMVR_CLIMB_END_DROP && r.s.bl_why == SOMVR_CLIMB_BL_CANNOT,
          "no blink now: the drop (%s)", somvr_climb_bl_name(r.s.bl_why));
    /* descent off: the hand over the edge finds nothing (the drill's H: palms and fingers away, no down ray standing) */
    desc_setup(&r, 900);
    r.c.descent = 0;
    CHECK(!desc_lip(&r) && !r.s.active && r.s.lips == 0, "descent off: no hold over the edge");
    frames(&r, 30);
    CHECK(r.s.misses == 1, "... a miss (%llu)", (unsigned long long)r.s.misses);

    /* a top reached from below (standing at the wall's foot, a raised hand over its top): no down ray, as without descent
     * (run 022824: the far side made it a lip facing back and `over` at once; run 024500: the climb started on the top) */
    desc_setup(&r, WALL_TOP);
    r.body = (sv_vec3){ 0, 0, 0 }, r.top_rest = 0;
    r.in.hand[0].p = (sv_vec3){ -20, 310, 110 }, r.in.hand[1].p = (sv_vec3){ 20, 320, 60 };
    rig_frame(&r);
    grip(&r, 0, 1);
    frames(&r, 5);
    CHECK(r.s.h[0].via != SOMVR_CLIMB_VIA_DOWN && !r.s.h[0].lip && r.s.overs == 0 && r.s.lips == 0,
          "a top from below: not the raised hand's down ray (via %d: the inside ray, as without descent)", r.s.h[0].via);
    /* the floor, not a lip: the top goes on along the reach (no edge within 60 cm): the low rule (no climb) */
    desc_setup(&r, WALL_TOP);
    g_top_far = 1;
    r.in.hand[1].p = (sv_vec3){ 20, 15, 80 }; /* world z 170: 30 cm past the head, the top goes on */
    rig_frame(&r);
    grip(&r, 1, 1);
    rig_frame(&r);
    CHECK(!r.s.active && r.s.lows == 1 && !r.s.h[1].lip, "the floor: low (%llu), no lip", (unsigned long long)r.s.lows);
    g_top_far = 0;
    /* the lip from below: the hand out past the edge, 20 cm under the top; the wall ray (toward the head) finds the face */
    desc_setup(&r, WALL_TOP);
    r.in.hand[1].p = (sv_vec3){ 0, -20, -10 }; /* world (0, 280, 80) */
    rig_frame(&r);
    grip(&r, 1, 1);
    rig_frame(&r);
    CHECK(r.s.active && r.s.descending && r.s.h[1].via == SOMVR_CLIMB_VIA_WALL && r.s.h[1].lip &&
              NEAR(r.s.lip_top_y, WALL_TOP, 0.5) && NEAR3(r.s.lip_out, 0, 0, -1, 0.01),
          "the lip from below: via %d, top %g", r.s.h[1].via, r.s.lip_top_y);
    for (int i = 0; i < 50; i++) hand_move(&r, 1, 0, 3, 0), rig_frame(&r);
    CHECK(r.body.y < WALL_TOP - 60 && r.s.lip_steps > 0, "pushed up: down past the lip (y %g, lip steps %llu)", r.body.y,
          (unsigned long long)r.s.lip_steps);
    /* back up from the face: a hold under the top climbs up and over it (the top's own rules once the head is under) */
    desc_setup(&r, WALL_TOP);
    desc_lip(&r);
    desc_over(&r, 50);
    for (k = 0, h = 0; k < 2; k++, h = 1 - h) desc_hold(&r, h, 25);
    CHECK(!r.s.descending && r.in.head_y < WALL_TOP, "under the top (head %g)", r.in.head_y);
    /* climb up again: the holds 20 cm from the face, the hand pulled down; then a hand on the top pushes over */
    r.c.top_mode = SOMVR_CLIMB_TOP_PUSH;
    int over = 0;
    for (k = 0; k < 12 && !over; k++, h = 1 - h) {
        sv_vec3 w = { h ? 20.0f : -20.0f, fminf(r.in.head_y + 30, WALL_TOP - 5), 80 };
        r.in.hand[h].p = (sv_vec3){ w.x - r.body.x, w.y - r.body.y, w.z - r.body.z };
        rig_frame(&r);
        grip(&r, h, 1);
        rig_frame(&r);
        grip(&r, 1 - h, 0);
        rig_frame(&r);
        for (int i = 0; i < 25 && r.s.active; i++) hand_move(&r, h, 0, -3, -1.5f), rig_frame(&r);
        over = !r.s.active && r.s.last_end == SOMVR_CLIMB_END_OVER;
    }
    CHECK(over && r.s.tops >= 1 && r.body.y >= WALL_TOP - 0.01f, "climbed back over the top (%s, tops %llu, y %g)",
          somvr_climb_end_name(r.s.last_end), (unsigned long long)r.s.tops, r.body.y);

    /* the cue: an open low hand at the lip ticks; over the far floor not */
    desc_setup(&r, WALL_TOP);
    r.c.cue = 1;
    r.in.hand[1].p = (sv_vec3){ 20, 15, 20 };
    frames(&r, 5);
    CHECK(r.s.nears == 1, "the cue at the lip (%llu)", (unsigned long long)r.s.nears);
    desc_setup(&r, WALL_TOP);
    r.c.cue = 1, g_top_far = 1;
    r.in.hand[1].p = (sv_vec3){ 20, 15, 80 };
    frames(&r, 5);
    CHECK(r.s.nears == 0, "no cue over the floor (%llu)", (unsigned long long)r.s.nears);
    /* the game's edge wall (a standing Talion kept 45 cm off the edge): the lip step's move out does not go through 3
     * frames = lip blocked: no more lip step (nothing moves; A is the way down) */
    desc_setup(&r, WALL_TOP);
    r.edge_stop = 1;
    r.body.z = 100; /* the root at z 150 */
    rig_frame(&r);
    r.in.hand[1].p = (sv_vec3){ 20, 15, 5 }; /* world z 105, over the top at its edge */
    rig_frame(&r);
    grip(&r, 1, 1);
    rig_frame(&r);
    int blocked = 0;
    for (int i = 0; i < 30; i++) {
        hand_move(&r, 1, 0, 3, 0), rig_frame(&r);
        blocked += (r.o.events2 & SOMVR_CLIMB_E_LIP_BLOCKED) != 0;
    }
    CHECK(r.s.descending && r.s.lip_blocked && blocked == 1 && r.s.lip_blocks == 1 && r.s.lip_steps <= 4 &&
              NEAR(r.body.y, WALL_TOP, 0.01) && r.body.z + 50 >= WALL_Z + 45 - 0.01f,
          "the edge wall: lip blocked (%d, steps %llu), y %g, root z %g", blocked, (unsigned long long)r.s.lip_steps,
          r.body.y, r.body.z + 50);
    r.c.blink_cm = 200; /* (the 3 m mock top) */
    r.in.drop = 1;
    rig_frame(&r);
    r.in.drop = 0;
    CHECK(r.s.phase == SOMVR_CLIMB_PH_BLINK && r.o.blink && NEAR(r.s.bl_h, WALL_TOP, 1), "... A: the blink from the lip (%g)",
          r.s.bl_h);
    g_top_far = 0, g_wall_top = WALL_TOP, g_ground_on = 0;
}

/* A on foot (descent): the edge ahead along the view's heading, the blink to the ground under it */
static void test_edge(void) {
    rig_t r;
    desc_setup(&r, 900);
    r.in.head_fwd = (sv_vec3){ 0, 0, -1 }; /* facing out over the edge (z 100; the root at z 140) */
    uint64_t r0 = r.s.rays;
    press_a(&r);
    CHECK(r.o.blink && (r.o.events & SOMVR_CLIMB_E_EDGE) && (r.o.events & SOMVR_CLIMB_E_BLINK) && !r.s.active &&
              r.s.edge_blinks == 1 && NEAR(r.s.bl_h, 900, 0.5) && NEAR3(r.o.blink_to, 0, 105, 140 - 60 - 40, 0.5),
          "A at the edge: the blink (%g cm, to %g %g %g, %llu rays)", r.s.bl_h, r.o.blink_to.x, r.o.blink_to.y,
          r.o.blink_to.z, (unsigned long long)(r.s.rays - r0));
    CHECK(NEAR3(r.body, 0, 900, 90, 0.01), "nothing moves (the trip does)");
    /* a lower top (300): short: nothing; facing into the top (the ground beyond it not there): no ground, a buzz */
    desc_setup(&r, WALL_TOP);
    r.in.head_fwd = (sv_vec3){ 0, 0, -1 };
    press_a(&r);
    CHECK(!r.o.blink && r.s.bl_why == SOMVR_CLIMB_BL_SHORT && (r.o.events & SOMVR_CLIMB_E_BLINK_NONE) && !r.s.active,
          "a 3 m edge: %s", somvr_climb_bl_name(r.s.bl_why));
    r.in.head_fwd = (sv_vec3){ 0, 0, 1 };
    press_a(&r);
    CHECK(!r.o.blink && r.s.bl_why == SOMVR_CLIMB_BL_NOGROUND, "the far side (no floor there): %s",
          somvr_climb_bl_name(r.s.bl_why));
    /* no heading, no blink now, the bow out (nograb), descent off */
    desc_setup(&r, 900);
    press_a(&r);
    CHECK(!r.o.blink && r.s.bl_why == SOMVR_CLIMB_BL_NOEDGE, "no heading: %s", somvr_climb_bl_name(r.s.bl_why));
    r.in.head_fwd = (sv_vec3){ 0, 0, -1 }, r.in.can_blink = 0;
    press_a(&r);
    CHECK(!r.o.blink && r.s.bl_why == SOMVR_CLIMB_BL_CANNOT && !r.s.active, "no blink now: nothing");
    r.in.can_blink = 1, r.in.nograb = 1;
    r0 = r.s.rays;
    press_a(&r);
    CHECK(!r.o.blink && r.s.rays == r0, "the bow out: A is the bow's");
    r.in.nograb = 0, r.c.descent = 0;
    press_a(&r);
    CHECK(!r.o.blink && r.s.rays == r0 && !(r.o.events & SOMVR_CLIMB_E_EDGE), "descent off: A on foot does nothing");
    /* the edge cue: the walk stopped at the edge = once E_EDGE_CUE (the counts of A untouched); again after 2 s of walking */
    desc_setup(&r, 900);
    r.c.cue = 1, r.in.head_fwd = (sv_vec3){ 0, 0, -1 }, r.in.walk_blocked = 1;
    int cues = 0;
    for (int i = 0; i < 90; i++) rig_frame(&r), cues += (r.o.events2 & SOMVR_CLIMB_E_EDGE_CUE) != 0;
    CHECK(cues == 1 && r.s.edge_cues == 1 && NEAR(r.s.cue_h, 900, 1) && !r.o.blink && r.s.blinks == 0 && r.s.bl_noedge == 0,
          "the edge cue once (%d, %g cm)", cues, r.s.cue_h);
    r.in.walk_blocked = 0;
    frames(&r, 200);
    r.in.walk_blocked = 1;
    for (int i = 0; i < 10; i++) rig_frame(&r), cues += (r.o.events2 & SOMVR_CLIMB_E_EDGE_CUE) != 0;
    CHECK(cues == 2, "the cue again after walking on (%d)", cues);
    r.in.head_fwd = (sv_vec3){ 0, 0, 1 }; /* the far side: no ground under it: no cue */
    r.in.walk_blocked = 0;
    frames(&r, 200);
    r.in.walk_blocked = 1;
    for (int i = 0; i < 10; i++) rig_frame(&r), cues += (r.o.events2 & SOMVR_CLIMB_E_EDGE_CUE) != 0;
    CHECK(cues == 2 && r.s.bl_noground == 0, "no cue without ground (%d)", cues);
    /* the floor goes on past the look (4 m): no edge */
    desc_setup(&r, WALL_TOP);
    g_top_far = 1;
    r.in.head_fwd = (sv_vec3){ 0, 0, 1 };
    press_a(&r);
    CHECK(!r.o.blink && r.s.bl_why == SOMVR_CLIMB_BL_NOEDGE && r.s.bl_noedge == 1, "no edge within 4 m: %s",
          somvr_climb_bl_name(r.s.bl_why));
    g_top_far = 0, g_wall_top = WALL_TOP, g_ground_on = 0;
}

/* build 15b's overhang: a pull under a lip (the head stopped 5 cm up while the root is within 10 cm of the wall's 50)
 * goes out round it with SOMVR_CLIMB_OVERHANG, stays blocked without; a hold far over a reached top forgets it */
static void test_overhang(void) {
    for (int on = 0; on < 2; on++) {
        rig_t r;
        rig_init(&r);
        r.c.overhang = on ? 1.0f : 0.0f;
        r.ceil_y = 175, r.ceil_reach = 40;          /* the lip over the head, out to the root's z 40 */
        r.in.hand[1].p = (sv_vec3){ 20, 200, 85 }; /* the hold 30 cm over the eyes */
        grip(&r, 1, 1);
        frames(&r, 2);
        CHECK(r.s.active && r.s.h[1].holding, "overhang %d: the hold", on);
        for (int i = 0; i < 20; i++) hand_move(&r, 1, 0, -2, 0), rig_frame(&r); /* the pull: 40 cm down in 20 frames */
        frames(&r, 3);
        if (!on)
            CHECK(r.moved.y <= 5.001f && NEAR(r.moved.z, 0, 1e-4) && r.s.oh_steps == 0,
                  "overhang off: blocked (up %g, out %g)", r.moved.y, -r.moved.z);
        else
            CHECK(r.moved.y > 25 && r.moved.z <= -10 && r.moved.z >= -20.001f && r.s.oh_lips == 1 &&
                      r.s.oh_cm >= 10 && r.s.oh_cm <= 20.001f,
                  "overhang on: out %g (steps %llu) and up %g", -r.moved.z, (unsigned long long)r.s.oh_steps, r.moved.y);
        printf("overhang %s: the pull 40 cm: up %.1f, out %.1f (%llu steps)\n", on ? "on" : "off", r.moved.y, -r.moved.z,
               (unsigned long long)r.s.oh_steps);
        /* no pull, nothing: the hand still */
        sv_vec3 m0 = r.moved;
        frames(&r, 30);
        CHECK(NEAR3(r.moved, m0.x, m0.y, m0.z, 1e-4), "overhang %d: still without a pull", on);
        /* a hold 50+ cm over a top already reached forgets it (on only) */
        r.s.topped = 1, r.s.top_y = r.s.h[1].hit.y - 60;
        r.in.hand[0].p = (sv_vec3){ -20, 190, 85 };
        grip(&r, 0, 1);
        frames(&r, 2);
        CHECK(r.s.h[0].holding && r.s.topped == !on && r.s.retops == (uint64_t)on, "overhang %d: retop %d (topped %d)", on,
              (int)r.s.retops, r.s.topped);
    }
    /* the mantle: held under the top (the ceiling at the head everywhere: the game's edge keeps him in), the pull
     * blocked 0.4 s = the blink onto the top, 40 cm in from the edge; without the blink (can_blink 0) nothing */
    for (int cb = 0; cb < 2; cb++) {
        rig_t q;
        rig_init(&q);
        q.c.overhang = 1, q.c.overhang_cm = 0.001f; /* (no out step: the edge keeps him) */
        q.in.can_blink = cb;
        q.body.y = 60, q.ceil_y = 235;              /* the head at 230, the top (300) 70 cm over it */
        q.in.hand[1].p = (sv_vec3){ 20, 235, 85 }; /* the hold 5 cm under the top */
        grip(&q, 1, 1);
        frames(&q, 2);
        CHECK(q.s.active && q.s.h[1].holding && q.s.h[1].edge && q.s.h[1].top_found, "mantle %d: the edge hold", cb);
        int f = 0, blink = 0;
        for (; f < 60 && !blink; f++) hand_move(&q, 1, 0, -2, 0), rig_frame(&q), blink = q.o.blink;
        if (cb)
            CHECK(blink && q.s.mantles == 1 && q.s.phase == SOMVR_CLIMB_PH_BLINK && (q.o.events2 & SOMVR_CLIMB_E_MANTLE) &&
                      NEAR(q.o.blink_to.y, 300 + 102 + 5, 1e-3) && NEAR(q.o.blink_to.z, 140, 1.0) && f >= 36 && f <= 42,
                  "mantle: after %d frames, to %g %g %g", f, q.o.blink_to.x, q.o.blink_to.y, q.o.blink_to.z);
        else
            CHECK(!blink && q.s.mantles == 0 && q.s.phase == SOMVR_CLIMB_PH_HOLD, "no blink: no mantle (%d)", blink);
        if (cb) printf("mantle: the blink after %d frames of the pull (0.4 s at 90 Hz)\n", f);
    }
    { /* build 16: SOMVR_CLIMB_OVERHANG=step: the same pull, no mantle */
        rig_t q;
        rig_init(&q);
        q.c.overhang = 1, q.c.overhang_cm = 0.001f, q.c.oh_mantle = 0;
        q.in.can_blink = 1;
        q.body.y = 60, q.ceil_y = 235;
        q.in.hand[1].p = (sv_vec3){ 20, 235, 85 };
        grip(&q, 1, 1);
        frames(&q, 2);
        int blink = 0;
        for (int f = 0; f < 60 && !blink; f++) hand_move(&q, 1, 0, -2, 0), rig_frame(&q), blink = q.o.blink;
        CHECK(!blink && q.s.mantles == 0 && q.s.phase == SOMVR_CLIMB_PH_HOLD, "overhang step: no mantle (%d)", blink);
    }
    /* a hold under the eyes: a blocked up move is no lip (the capsule's top against something else) */
    rig_t r;
    rig_init(&r);
    r.c.overhang = 1;
    r.ceil_y = 175, r.ceil_reach = 40;
    r.in.hand[1].p = (sv_vec3){ 20, 150, 85 };
    grip(&r, 1, 1);
    frames(&r, 2);
    for (int i = 0; i < 20; i++) hand_move(&r, 1, 0, -2, 0), rig_frame(&r);
    CHECK(NEAR(r.moved.z, 0, 1e-4) && r.s.oh_steps == 0, "a hold under the eyes: no overhang step (out %g)", -r.moved.z);
}

/* SOMVR_CLIMB_PULLOVER (build 16): a hold just under the top (the mock's 300, 100 cm deep): armed with the landing 40
 * cm in shown; a short pull nothing; the hand opened = cancelled; again, the pull of 25 cm = the blink onto it; off,
 * out of range, no blink, a shallow top (no landing) */
static void test_pullover(void) {
    /* blocked at the lip (the head never past 232, the top 300 with room 40 cm in): nothing on the grab, nothing
     * without a pull; armed after 0.3 s of the blocked pull (the ring); 20 cm more: shown, no blink; the hand opened:
     * cancelled; gripped again and blocked again: armed again; 25 cm from there = the blink (no blink now: nothing) */
    for (int cb = 0; cb < 2; cb++) {
        rig_t r;
        rig_init(&r);
        r.c.pullover = 1, r.in.can_blink = cb, r.c.letgo = SOMVR_CLIMB_LETGO_HANG;
        r.body.y = 60, r.ceil_y = 232;
        r.in.hand[1].p = (sv_vec3){ 20, 235, 85 }; /* the hold 5 cm under the top */
        grip(&r, 1, 1);
        rig_frame(&r);
        CHECK(r.s.active && r.s.h[1].holding && !r.o.po_show && r.s.po_arms == 0, "pullover %d: nothing on the grab", cb);
        frames(&r, 40);
        CHECK(!r.o.po_show && r.s.po_arms == 0, "pullover %d: no pull: nothing", cb);
        int f = 0, armed = 0;
        for (; f < 80 && !armed; f++) hand_move(&r, 1, 0, -1, 0), rig_frame(&r), armed = (r.o.events2 & SOMVR_CLIMB_E_PO_ARM) != 0;
        CHECK(armed && f >= 26 && f <= 34 && r.o.po_show && r.o.po_hand == 1 && NEAR(r.o.po_land.y, 300, 1e-3) &&
                  NEAR(r.o.po_land.z, 140, 1e-3) && r.s.po_arms == 1,
              "pullover %d: armed after the pull blocked 0.3 s (%d frames; land %g %g %g)", cb, f, r.o.po_land.x,
              r.o.po_land.y, r.o.po_land.z);
        for (int i = 0; i < 20; i++) hand_move(&r, 1, 0, -1, 0), rig_frame(&r);
        CHECK(r.o.po_show && !r.o.blink && NEAR(r.o.po_pull, 0.8, 1e-3), "pullover %d: 20 cm more: shown, %.2f, no blink", cb,
              r.o.po_pull);
        grip(&r, 1, 0);
        rig_frame(&r);
        CHECK(!r.o.po_show && (r.o.events2 & SOMVR_CLIMB_E_PO_CANCEL) && r.s.po_cancels == 1 && !r.o.blink,
              "pullover %d: the hand opened: cancelled, no ring", cb);
        frames(&r, 2);
        grip(&r, 1, 1);
        rig_frame(&r);
        CHECK(!r.o.po_show, "pullover %d: gripped again: not yet (the block again first)", cb);
        for (f = 0, armed = 0; f < 80 && !armed; f++)
            hand_move(&r, 1, 0, -1, 0), rig_frame(&r), armed = (r.o.events2 & SOMVR_CLIMB_E_PO_ARM) != 0;
        CHECK(armed && r.s.po_arms == 2, "pullover %d: blocked again: armed again (%d frames)", cb, f);
        int blink = 0;
        for (f = 0; f < 40 && !blink; f++) hand_move(&r, 1, 0, -2, 0), rig_frame(&r), blink = r.o.blink;
        if (cb)
            CHECK(blink && f == 13 && (r.o.events2 & SOMVR_CLIMB_E_PO_GO) && r.s.po_goes == 1 &&
                      r.s.phase == SOMVR_CLIMB_PH_BLINK && NEAR(r.o.blink_to.y, 300 + 102 + 5, 1e-3) &&
                      NEAR(r.o.blink_to.z, 140, 1e-3) && !r.o.po_show && !r.s.h[1].holding,
                  "pullover: the pull of 26 cm (%d frames) = the blink to %g %g %g", f, r.o.blink_to.x, r.o.blink_to.y,
                  r.o.blink_to.z);
        else
            CHECK(!blink && r.o.po_show && r.s.po_goes == 0 && r.s.phase == SOMVR_CLIMB_PH_HOLD,
                  "pullover: no blink now: armed, shown, nothing (%d)", blink);
    }
    { /* the 1:1 goes through (no ceiling): the same pull rises 1:1, never a ring */
        rig_t r;
        rig_init(&r);
        r.c.pullover = 1, r.in.can_blink = 1;
        r.body.y = 60;
        r.in.hand[1].p = (sv_vec3){ 20, 235, 85 };
        grip(&r, 1, 1);
        uint32_t ev = 0;
        for (int i = 0; i < 60; i++) hand_move(&r, 1, 0, -1, 0), rig_frame(&r), ev |= r.o.events2;
        CHECK(r.moved.y > 55 && r.s.po_arms == 0 && !(ev & (SOMVR_CLIMB_E_PO_ARM | SOMVR_CLIMB_E_PO_NOWAY)) && !r.o.po_show,
              "pullover: the 1:1 rises (%g cm): no ring", r.moved.y);
    }
    { /* blocked on a top with no landing (15 cm deep): no way up here, once */
        rig_t r;
        rig_init(&r);
        r.c.pullover = 1, r.in.can_blink = 1;
        r.body.y = 60, r.ceil_y = 232;
        g_top_depth = 15;
        r.in.hand[1].p = (sv_vec3){ 20, 235, 85 };
        grip(&r, 1, 1);
        int nw = 0;
        for (int i = 0; i < 80; i++) {
            hand_move(&r, 1, 0, -1, 0), rig_frame(&r);
            nw += (r.o.events2 & SOMVR_CLIMB_E_PO_NOWAY) != 0;
        }
        CHECK(nw == 1 && r.s.po_noways == 1 && r.s.po_arms == 0 && !r.o.po_show && !r.o.blink,
              "pullover: blocked, no landing: no way up here once (%d, arms %llu)", nw, (unsigned long long)r.s.po_arms);
        g_top_depth = 100;
    }
    /* off; a top 130 cm over the eyes (out of range); a top 15 cm deep (no landing: E_PO_NONE once); no top at all */
    for (int k = 0; k < 4; k++) {
        rig_t r;
        rig_init(&r);
        r.c.pullover = k != 0, r.in.can_blink = 1;
        r.body.y = k == 1 ? 0 : 60, r.ceil_y = k == 1 ? 172 : 232;
        if (k == 1) r.c.po_up_cm = 100; /* the top 130 over the eyes (170): past 100 */
        if (k == 2) g_top_depth = 15;
        if (k == 3) g_no_top = 1;
        r.in.hand[1].p = (sv_vec3){ 20, k == 1 ? 295 : 235, 85 };
        grip(&r, 1, 1);
        rig_frame(&r);
        uint32_t ev = r.o.events2;
        for (int i = 0; i < 20; i++) hand_move(&r, 1, 0, -2, 0), rig_frame(&r), ev |= r.o.events2;
        CHECK(r.s.h[1].holding && !r.o.po_show && r.s.po_arms == 0 && !r.o.blink && r.s.po_goes == 0 &&
                  (k == 2 ? (ev & SOMVR_CLIMB_E_PO_NONE) && r.s.po_why == SOMVR_CLIMB_PO_NOFLOOR && r.s.po_nones == 1
                          : !(ev & SOMVR_CLIMB_E_PO_NONE)),
              "pullover case %d: nothing (arms %llu, nones %llu, why %d)", k, (unsigned long long)r.s.po_arms,
              (unsigned long long)r.s.po_nones, r.s.po_why);
        g_top_depth = 100, g_no_top = 0;
    }
}

/* hang_stick_cm (build 16): hanging with the feet 30 cm over the floor, the stick = released; at 90 cm over it, the
 * stick does nothing (the hang stays); off: nothing */
static void test_hang_stick(void) {
    for (int k = 0; k < 3; k++) {
        rig_t r;
        rig_init(&r);
        r.c.letgo = SOMVR_CLIMB_LETGO_HANG, r.c.ground_cm = 0, r.c.hang_stick_cm = k == 2 ? 0 : 60;
        g_ground_on = 1;
        r.in.root_ok = 1;
        r.body.y = k == 1 ? 90 : 30; /* the root = body + 102: the feet 30 (90) cm over the floor */
        r.in.hand[1].p = (sv_vec3){ 20, 200, 85 };
        grip(&r, 1, 1);
        frames(&r, 2);
        grip(&r, 1, 0);
        frames(&r, 2);
        CHECK(r.s.active && r.s.phase == SOMVR_CLIMB_PH_HANG, "hang stick %d: hanging (%d)", k, r.s.phase);
        frames(&r, 5);
        CHECK(r.s.active && r.s.phase == SOMVR_CLIMB_PH_HANG && r.s.stick_drops == 0, "hang stick %d: no stick: hangs", k);
        r.in.stick = 0.8f;
        rig_frame(&r);
        uint32_t ev = r.o.events2;
        rig_frame(&r);
        if (k == 0)
            CHECK(!r.s.active && (ev & SOMVR_CLIMB_E_STICK_DROP) && r.s.stick_drops == 1 && NEAR(r.s.stick_h, 28, 0.5) && /* the rig's root is 100 over the body */
                      r.s.last_end == SOMVR_CLIMB_END_RELEASED,
                  "hang stick: the feet 30 cm up, the stick: released (h %g, active %d)", r.s.stick_h, r.s.active);
        else
            CHECK(r.s.active && r.s.phase == SOMVR_CLIMB_PH_HANG && r.s.stick_drops == 0,
                  "hang stick %d: %s: the hang stays", k, k == 1 ? "90 cm up" : "off");
        g_ground_on = 0;
    }
}

/* SOMVR_CLIMB_UNPIN (build 16): blocked ticks, the hold, inside a surface, the guards, the step's size */
static void test_unpin(void) {
    somvr_unpin_cfg c;
    somvr_unpin_cfg_default(&c);
    somvr_unpin s;
    somvr_unpin_init(&s);
    somvr_unpin_out o;
    somvr_unpin_in in;
    memset(&in, 0, sizeof in);
    in.asked = (sv_vec3){ 3, 0.5f, 0 }, in.applied = (sv_vec3){ 0.1f, 0.5f, 0 }, in.clear_cm = -1;
    in.nc = 2;
    in.c[0] = (somvr_unpin_contact){ { 1, 0, 0 }, -6 };  /* a wall at -x, 6 cm into the capsule */
    in.c[1] = (somvr_unpin_contact){ { 0, 1, 0 }, 0.3f }; /* the floor */
    in.t = 10.0;
    somvr_unpin_step(&s, &c, &in, &o);
    CHECK(o.why == SOMVR_UNPIN_WAIT && s.t0 == 10.0, "unpin: the first blocked tick waits (%d)", o.why);
    in.t = 10.1;
    somvr_unpin_step(&s, &c, &in, &o);
    CHECK(o.why == SOMVR_UNPIN_WAIT, "unpin: 0.1 s: still waiting (%d)", o.why);
    in.t = 10.16;
    somvr_unpin_step(&s, &c, &in, &o);
    CHECK(o.why == SOMVR_UNPIN_RAYS && NEAR(o.dir.x, 1, 1e-4f) && NEAR(o.deepest, -6, 1e-4f), "unpin: 0.16 s: the rays (%d)",
          o.why);
    in.clear_cm = 1e6f;
    somvr_unpin_step(&s, &c, &in, &o);
    CHECK(o.why == SOMVR_UNPIN_STEP && NEAR(o.d.x, 3, 1e-4f) && o.d.y == 0 && o.d.z == 0 && o.first && s.steps == 1 &&
              s.episodes == 1,
          "unpin: the step = the asked move, level (%d %g)", o.why, o.d.x);
    in.clear_cm = -1, in.t = 10.18;
    somvr_unpin_step(&s, &c, &in, &o);
    in.clear_cm = 1e6f;
    somvr_unpin_step(&s, &c, &in, &o);
    CHECK(o.why == SOMVR_UNPIN_STEP && !o.first && s.steps == 2 && s.episodes == 1, "unpin: the next tick steps on (%d)",
          o.why);
    in.asked = (sv_vec3){ 20, 0, 0 };
    somvr_unpin_step(&s, &c, &in, &o);
    CHECK(o.why == SOMVR_UNPIN_STEP && NEAR(o.d.x, c.step_cm, 1e-4f), "unpin: at most step_cm a tick (%g)", o.d.x);
    in.clear_cm = c.radius_cm + c.step_cm - 1;
    somvr_unpin_step(&s, &c, &in, &o);
    CHECK(o.why == SOMVR_UNPIN_WALL && s.walls == 1, "unpin: a wall within the radius + the move: none (%d)", o.why);
    in.asked = (sv_vec3){ -3, 0, 0 }, in.clear_cm = -1;
    somvr_unpin_step(&s, &c, &in, &o);
    CHECK(o.why == SOMVR_UNPIN_INTO && s.into == 1, "unpin: into the wall he is in: none (%d)", o.why);
    in.asked = (sv_vec3){ 0, 0, 3 };
    somvr_unpin_step(&s, &c, &in, &o);
    CHECK(o.why == SOMVR_UNPIN_RAYS, "unpin: along that wall: allowed (%d)", o.why);
    in.asked = (sv_vec3){ -3, 0, 3 }, in.clear_cm = 1e6f; /* half into the wall: slides along it */
    somvr_unpin_step(&s, &c, &in, &o);
    CHECK(o.why == SOMVR_UNPIN_STEP && NEAR(o.d.x, 0, 1e-4) && NEAR(o.d.z, 3, 1e-4) && o.drift == 0,
          "unpin: half into the wall: the part along it (no dt: no drift) (%g %g)", o.d.x, o.d.z);
    in.dt = 1.0f / 90; /* at 90 Hz: the drift out of the rock, 4 cm/s = 0.044 cm a tick */
    somvr_unpin_step(&s, &c, &in, &o);
    CHECK(o.why == SOMVR_UNPIN_STEP && NEAR(o.drift, 4.0f / 90, 1e-5) && NEAR(o.d.x, 4.0f / 90, 1e-5) && NEAR(o.d.z, 3, 1e-4),
          "unpin: the drift out at most 4 cm/s (%g, d %g %g)", o.drift, o.d.x, o.d.z);
    in.dt = 0;
    in.asked = (sv_vec3){ -3, 0, 0.5f }, in.clear_cm = -1; /* nearly all into it: none */
    somvr_unpin_step(&s, &c, &in, &o);
    CHECK(o.why == SOMVR_UNPIN_INTO, "unpin: nearly all into the wall: none (%d)", o.why);
    in.asked = (sv_vec3){ 0, 0, 3 };
    in.c[0].d = 0.5f; /* touching, not inside: a wall ahead or the game's edge rule */
    in.asked = (sv_vec3){ 3, 0, 0 };
    somvr_unpin_step(&s, &c, &in, &o);
    CHECK(o.why == SOMVR_UNPIN_NOTIN && s.notin == 1, "unpin: blocked, not inside: none (%d)", o.why);
    in.c[0].d = -6;
    in.applied = (sv_vec3){ 1.0f, 0, 0 }; /* a third of it done: not blocked */
    somvr_unpin_step(&s, &c, &in, &o);
    CHECK(o.why == SOMVR_UNPIN_FREE && s.t0 < 0, "unpin: the body moved: free, the block forgotten (%d)", o.why);
    in.applied = (sv_vec3){ 0, 0, 0 }, in.asked = (sv_vec3){ 0.1f, 5, 0 }, in.t = 11;
    somvr_unpin_step(&s, &c, &in, &o);
    CHECK(o.why == SOMVR_UNPIN_FREE, "unpin: only up asked (a climb's pull): free (%d)", o.why);
    in.asked = (sv_vec3){ 3, 0, 0 }, in.c[1].n = (sv_vec3){ 0, -1, 0 }, in.c[1].d = -3, in.c[0].d = 0.5f; /* a ceiling */
    for (int i = 0; i < 3; i++) in.t = 12 + 0.1 * i, in.clear_cm = -1, somvr_unpin_step(&s, &c, &in, &o);
    CHECK(o.why == SOMVR_UNPIN_RAYS, "unpin: in a ceiling, moving level: allowed (%d)", o.why);
    in.c[1].n = (sv_vec3){ 0, 1, 0 }, in.c[1].d = -3, in.clear_cm = -1, in.t = 12.3; /* sunk 3 cm into the floor only */
    somvr_unpin_step(&s, &c, &in, &o);
    CHECK(o.why == SOMVR_UNPIN_NOTIN, "unpin: only the floor inside: not a pin (%d)", o.why);
    CHECK(!strcmp(somvr_unpin_why_name(SOMVR_UNPIN_INTO), "into a surface"), "unpin: the names");
}

/* build 16 round 2 (SOMVR_CLIMB_FACE_TOWARD): a hold taken with the palm along the face (run 032503's second top: the
 * palm ray 0.48 0 -0.88 on a face whose normal was ~ -0.8 0 -0.6) took the ray as its toward, and `over` came at the
 * top with the feet 150 cm under it. Here: the right hand 55 cm to the side, 2 cm off the face, the palm turned in
 * toward the head (-0.99 0 0.14): the palm ray finds the face 24 cm along; the hold 10 cm under the top */
static int face_run(rig_t *r, int face, int *over_at) {
    rig_init(r);
    r->c.face_toward = face;
    r->in.hand[0].p = (sv_vec3){ -20, 100, 60 }; /* the left hand down by the side */
    r->in.hand[1].p = (sv_vec3){ 55, 290, 98 };
    r->in.hand[1].palm = (sv_vec3){ -0.99f, 0, 0.14f }, r->in.hand[1].fwd = (sv_vec3){ 0, 1, 0 };
    grip(r, 1, 1);
    rig_frame(r);
    if (!r->s.h[1].holding) return 0;
    *over_at = -1;
    for (int i = 0; i < 80 && r->o.active; i++) { /* pulled down 2 cm a frame: the body up 160 cm (the head 330) */
        hand_move(r, 1, 0, -2, 0);
        rig_frame(r);
        if (r->o.ended && r->o.end_why == SOMVR_CLIMB_END_OVER) *over_at = i;
    }
    return 1;
}
static void test_face_toward(void) {
    rig_t r;
    int over = -1;
    /* as build 16 round 1: the palm's toward, over at the top with the feet (body y) ~150 cm under it */
    CHECK(face_run(&r, 0, &over) && r.s.h[1].via == SOMVR_CLIMB_VIA_PALM && !r.s.h[1].face && r.s.h[1].toward.x < -0.9f &&
              over >= 0 && r.body.y < 170,
          "face off: the palm's toward %g %g, over at %d with the feet at %g", r.s.h[1].toward.x, r.s.h[1].toward.z, over,
          r.body.y);
    /* the fix: the face's normal (+z) is the toward; the head over the top is no `over`; the push into it is */
    int rays0 = g_rays;
    CHECK(face_run(&r, 1, &over) && r.s.h[1].via == SOMVR_CLIMB_VIA_PALM && r.s.h[1].face && r.s.faces == 1 &&
              NEAR3(r.s.h[1].toward, 0, 0, 1, 1e-3) && r.s.h[1].edge && NEAR(r.s.h[1].top_y, 300, 1e-3) && over < 0 &&
              r.o.active && r.s.topped && r.in.head_y > 320,
          "face on: toward %g %g %g (face %d), edge %d top %g, over %d, active %d topped %d, the head %g (rays %d)",
          r.s.h[1].toward.x, r.s.h[1].toward.y, r.s.h[1].toward.z, r.s.h[1].face, r.s.h[1].edge, r.s.h[1].top_y, over,
          r.o.active, r.s.topped, r.in.head_y, g_rays - rays0);
    int n;
    for (n = 0; n < 80 && r.o.active; n++) {
        hand_move(&r, 1, 0, 0, -2);
        rig_frame(&r);
    }
    CHECK(r.o.ended && r.o.end_why == SOMVR_CLIMB_END_OVER && r.in.head_z >= WALL_Z + 30 - 1e-3 &&
              r.in.head_z <= WALL_Z + 30 + 2.1f && r.body.y > 159,
          "face on: over after the push in (%d frames, the head z %g, the feet %g)", n, r.in.head_z, r.body.y);
    /* the palm into the face (the common grab): no face rays, the ray's toward as ever */
    rig_init(&r);
    r.in.hand[1].p.y = 290;
    grip(&r, 1, 1);
    rig_frame(&r);
    CHECK(r.s.h[1].holding && !r.s.h[1].face && r.s.faces == 0 && NEAR3(r.s.h[1].toward, 0, 0, 1, 1e-4),
          "face: a palm into the face keeps its ray's toward");
    /* the palm along the face 10 cm under a face that goes on up (no top within reach): the old edge test's raised ray
     * along the face found no wall (a false edge); along the normal it finds the face: no edge */
    g_wall_top = 600;
    face_run(&r, 0, &over);
    int edge_old = r.s.h[1].edge;
    face_run(&r, 1, &over);
    CHECK(r.s.h[1].face && !r.s.h[1].edge && over < 0, "face: no false edge on a face that goes on (old %d, new %d)",
          edge_old, r.s.h[1].edge);
    g_wall_top = WALL_TOP;
}

/* build 16 round 2 (SOMVR_CLIMB_THROUGH): the user's slab - a box 60 cm high out from the face, the capsule's top held
 * under it; a hold on its front over the eyes, pulled hand over hand. Off: held (as round 1); on: blocked 0.15 s,
 * then each frame's up part through it 1:1 while the capsule is in it; a slab over the head (its front 5 cm past the
 * head) = refused, blocked as ever; the hold under the eyes = never */
static int thr_run(rig_t *r, int through, float slab_z, int pulls, float *top_y) {
    rig_init(r);
    r->c.through = through, r->c.cue = 0, r->c.letgo = SOMVR_CLIMB_LETGO_HANG;
    g_slab_y0 = 230, g_slab_y1 = 290, g_slab_z = slab_z, r->slab = 1, g_wall_top = 900; /* the face goes on up */
    r->in.hand[1].p = (sv_vec3){ 20, 250, slab_z - 4 }; /* the right hand on the slab's front, 80 cm over the eyes */
    r->in.hand[1].palm = (sv_vec3){ 0, 0, 1 }, r->in.hand[1].fwd = (sv_vec3){ 0, 1, 0 };
    grip(r, 1, 1);
    rig_frame(r);
    if (!r->s.h[1].holding) return 0;
    *top_y = -1;
    for (int k = 0; k < pulls && r->o.active; k++) { /* each pull 60 cm down in 30 frames, the other hand over it */
        int h = k & 1 ? 0 : 1, o = 1 - h;
        for (int i = 0; i < 30; i++) hand_move(r, h, 0, -2, 0), rig_frame(r);
        float wy = r->body.y + r->head_track_y + 60, wz = wy <= g_slab_y1 ? slab_z - 4 : WALL_Z - 4; /* the slab, the face */
        r->in.hand[o].p = (sv_vec3){ o ? 20.0f : -20.0f, wy - r->body.y, wz - r->body.z };
        r->in.hand[o].palm = (sv_vec3){ 0, 0, 1 };
        grip(r, o, 0), rig_frame(r);
        grip(r, o, 1), rig_frame(r);
        if (!r->s.h[o].holding) break;
        grip(r, h, 0), rig_frame(r);
        r->in.hand[h].p.y = 100;
    }
    *top_y = r->body.y + r->head_track_y + 20;
    return 1;
}
static void test_through(void) {
    rig_t r;
    float top;
    /* off: the capsule's top held under the slab (230): the head at 210, every pull lost */
    CHECK(thr_run(&r, 0, 65, 3, &top) && NEAR(top, 230, 0.6f) && r.s.thr_ons == 0 && r.o.active,
          "through off: held under the slab, the top %g", top);
    /* on: through it (60 cm of slab + the capsule: 250 cm): 6 pulls of 60 cm; the first rises 40 cm to the slab, the
     * second is blocked 0.15 s (27 cm lost), then all 1:1 (40 + 33 + 4 x 60 = 313) */
    CHECK(thr_run(&r, 1, 65, 6, &top) && r.s.thr_ons == 1 && r.s.thr_all_cm > 230 && top > 500 &&
              r.s.thr_refused == 0,
          "through on: the top %g (through %llu, %.0f cm, refused %llu)", top, (unsigned long long)r.s.thr_ons,
          r.s.thr_all_cm, (unsigned long long)r.s.thr_refused);
    CHECK(r.body.y > 290 && NEAR(r.moved.y, 313, 3), "through on: the body up %g", r.moved.y);
    /* out of it: over (out of the rock), the climb goes on 1:1 */
    CHECK(!r.s.thr_on && r.s.thr_why == SOMVR_CLIMB_THR_CLEAR && r.o.active, "through: over out of the rock (why %d %s)",
          r.s.thr_why, somvr_climb_thr_name(r.s.thr_why));
    /* a slab over the head (its front at z 45, the head at 50): the head's way up not clear: refused, held */
    CHECK(thr_run(&r, 1, 45, 2, &top) && NEAR(top, 230, 0.6f) && r.s.thr_ons == 0 && r.s.thr_refused >= 1 &&
              r.s.thr_all_cm == 0,
          "through: the slab over the head: refused %llu, the top %g", (unsigned long long)r.s.thr_refused, top);
    /* the hold under the eyes (a hold at the chest, pulled: the capsule's top held): never through */
    rig_init(&r);
    r.c.through = 1, r.c.cue = 0, r.slab = 1, g_slab_y0 = 170 + 20 + 5, g_slab_y1 = 260, g_slab_z = 60;
    r.in.hand[1].p = (sv_vec3){ 20, 140, 90 };
    grip(&r, 1, 1), rig_frame(&r);
    for (int i = 0; i < 30; i++) hand_move(&r, 1, 0, -2, 0), rig_frame(&r);
    CHECK(r.s.h[1].holding && r.s.thr_ons == 0 && r.moved.y < 6, "through: a hold under the eyes: never (up %g)", r.moved.y);
    g_slab_y0 = g_slab_y1 = g_slab_z = 0, g_wall_top = WALL_TOP;
    /* thr_top: at a top, the push into it lifts him (the edge step) but the capsule's foot is caught 46 cm under the
     * edge (the user's cliff top): off = stalled there; on = the lift through it, then over 1:1 */
    for (int on = 0; on < 2; on++) {
        top_setup(&r, SOMVR_CLIMB_TOP_PUSH);
        r.ledge = 1, r.foot_lip = 1, r.c.through = on, r.in.rock_ok = 1, r.in.rock_cm = 5;
        int i = pull_to_top(&r), n;
        for (n = 0; n < 300 && r.o.active; n++) hand_move(&r, 1, 0, 0, -2), rig_frame(&r);
        if (!on)
            CHECK(i >= 0 && r.s.topped && r.o.active && NEAR(r.body.y, 254, 0.5) && r.s.thr_ons == 0,
                  "through top off: stalled with the foot at %g", r.body.y);
        else
            CHECK(i >= 0 && r.o.ended && r.o.end_why == SOMVR_CLIMB_END_OVER && r.body.y >= 259 && r.s.thr_ons >= 1 &&
                      r.s.thr_all_cm > 4 && r.s.thr_all_cm < 20,
                  "through top on: over (foot %g, through %llu, %.1f cm)", r.body.y, (unsigned long long)r.s.thr_ons,
                  r.s.thr_all_cm);
    }
    r.foot_lip = 0;
}

/* SOMVR_CLIMB_HUG (build 17): a hand pushed toward the wall never shoves him off it; up and along 1:1; beyond hug_cm a
 * part of the pull goes toward the face, not at a top */
static void test_hug(void) {
    for (int on = 0; on < 2; on++) {
        rig_t r;
        rig_init(&r);
        r.c.hug = on;
        grip(&r, 1, 1);
        frames(&r, 2);
        CHECK(r.s.h[1].holding && NEAR3(r.s.h[1].toward, 0, 0, 1, 1e-4), "hug %d: the hold, toward the wall", on);
        for (int i = 0; i < 15; i++) { /* the hand 30 cm toward the wall (into it) */
            hand_move(&r, 1, 0, 0, 2);
            rig_frame(&r);
        }
        frames(&r, 2);
        if (on)
            CHECK(NEAR(r.body.z, 0, 0.01) && r.s.hug_cuts > 0 && NEAR(r.s.hug_cut_cm, 30, 0.5) && r.s.hug_ok &&
                      NEAR(r.s.hug_d, 50, 0.01),
                  "hug on: the push toward the wall cut (body z %g, cut %g cm, d %g)", r.body.z, r.s.hug_cut_cm, r.s.hug_d);
        else
            CHECK(NEAR(r.body.z, -30, 0.01) && r.s.hug_cuts == 0, "hug off: pushed 30 cm off the wall (body z %g)", r.body.z);
        sv_vec3 b0 = r.body;
        for (int i = 0; i < 10; i++) { /* up 40 and 30 to the side: 1:1 */
            hand_move(&r, 1, 3, -4, 0);
            rig_frame(&r);
        }
        frames(&r, 2);
        CHECK(NEAR(r.body.y - b0.y, 40, 0.01) && NEAR(r.body.x - b0.x, -30, 0.01) && NEAR(r.body.z - b0.z, 0, 0.01),
              "hug %d: up and along 1:1 (%g %g %g)", on, r.body.x - b0.x, r.body.y - b0.y, r.body.z - b0.z);
        /* a pull toward the wall (the hand back) goes in: 10 cm, as ever */
        b0 = r.body;
        for (int i = 0; i < 5; i++) {
            hand_move(&r, 1, 0, 0, -2);
            rig_frame(&r);
        }
        frames(&r, 2);
        CHECK(NEAR(r.body.z - b0.z, on ? 10 : 10, 0.01), "hug %d: toward the wall 1:1 (%g)", on, r.body.z - b0.z);
    }
    /* beyond hug_cm (pushed 30 cm off with hug off, then on): a pull up takes him toward the face by 0.35 of it */
    rig_t r;
    rig_init(&r);
    grip(&r, 1, 1);
    frames(&r, 2);
    for (int i = 0; i < 15; i++) {
        hand_move(&r, 1, 0, 0, 2);
        rig_frame(&r);
    }
    frames(&r, 2);
    r.c.hug = 1;
    float z0 = r.body.z, y0 = r.body.y;
    for (int i = 0; i < 10; i++) {
        hand_move(&r, 1, 0, -4, 0);
        rig_frame(&r);
    }
    frames(&r, 2);
    CHECK(NEAR(r.body.y - y0, 40, 0.01) && NEAR(r.body.z - z0, 14, 0.6) && r.s.hug_ins > 0 && NEAR(r.s.hug_max_d, 80, 0.5),
          "hug: beyond 50 cm a pull up goes 0.35 toward the face (up %g, in %g, max d %g)", r.body.y - y0, r.body.z - z0,
          r.s.hug_max_d);
    for (int i = 0; i < 60; i++) { /* never past hug_cm: d stops at 50 */
        hand_move(&r, 1, 0, i < 30 ? -4 : 4, 0);
        rig_frame(&r);
    }
    frames(&r, 2);
    CHECK(NEAR(r.body.z, 0, 0.6) && NEAR(r.s.hug_d, 50, 0.6), "hug: in to 50 cm, no further (body z %g, d %g)", r.body.z,
          r.s.hug_d);
    /* at a top: no redirect (the push over it is the player's) */
    rig_init(&r);
    grip(&r, 1, 1);
    frames(&r, 2);
    for (int i = 0; i < 15; i++) {
        hand_move(&r, 1, 0, 0, 2);
        rig_frame(&r);
    }
    frames(&r, 2);
    r.c.hug = 1, r.s.topped = 1, r.s.top_y = 1000, r.s.top_toward = (sv_vec3){ 0, 0, 1 };
    z0 = r.body.z;
    for (int i = 0; i < 10; i++) {
        hand_move(&r, 1, 0, -4, 0);
        rig_frame(&r);
    }
    frames(&r, 2);
    CHECK(NEAR(r.body.z, z0, 0.01) && r.s.hug_ins == 0, "hug: topped, no redirect (dz %g)", r.body.z - z0);
}

/* SOMVR_CLIMB_STAND (build 17): topped with the feet at the top and the axis near its edge: let go = he stands; the push
 * that brings his feet onto it = over; a topped hang with his rim on the top: the stick walks him on; real hangs stay */
static void stand_at(rig_t *r, float body_y, float body_z, int stand) {
    top_setup(r, SOMVR_CLIMB_TOP_PUSH);
    r->c.letgo = SOMVR_CLIMB_LETGO_HANG, r->c.stand = 0, r->c.over_cm = 200; /* the head's over test out of the way */
    pull_to_top(r);
    frames(r, 2);
    for (float left = body_y - r->body.y; left > 0.001f; left -= 3) { /* up 1:1 (the hand pushed down) */
        hand_move(r, 1, 0, -fminf(3.0f, left), 0);
        rig_frame(r);
    }
    for (float left = body_z - r->body.z; left > 0.001f; left -= 2) { /* toward the top (the hand back) */
        hand_move(r, 1, 0, 0, -fminf(2.0f, left));
        rig_frame(r);
    }
    frames(r, 2);
    r->c.stand = stand;
}
static void test_stand(void) {
    rig_t r;
    /* the feet at the top (body y 300 = the feet 298), the axis 3 cm over the top (root z 103): let go */
    for (int on = 0; on < 2; on++) {
        stand_at(&r, 300, 53, on);
        CHECK(r.s.active && r.s.topped && NEAR(r.body.y, 300, 0.01) && NEAR(r.body.z, 53, 0.01), "stand %d: at the edge (%g %g)",
              on, r.body.y, r.body.z);
        grip(&r, 1, 0);
        rig_frame(&r);
        uint32_t ev = r.o.events2;
        rig_frame(&r);
        if (on)
            CHECK(!r.s.active && (ev & SOMVR_CLIMB_E_STAND) && r.s.last_end == SOMVR_CLIMB_END_RELEASED && r.s.stand_at == 0 &&
                      !r.s.stand_over && NEAR(r.s.stand_h, -2, 0.01) && r.s.stands == 1,
                  "stand on: let go at the top = released (h %g at %d, active %d)", r.s.stand_h, r.s.stand_at, r.s.active);
        else
            CHECK(r.s.active && r.s.phase == SOMVR_CLIMB_PH_HANG && r.s.stands == 0, "stand off: the hang at the top (build 16)");
    }
    /* mark 5 itself: the axis 8 cm out past the edge (root z 92): let go = the hang (his capsule would slide off), the
     * stick walks him on until the axis is over the top = released */
    stand_at(&r, 300, 42, 1);
    grip(&r, 1, 0);
    frames(&r, 3);
    CHECK(r.s.active && r.s.phase == SOMVR_CLIMB_PH_HANG && r.s.stands == 0, "stand: mark 5 (8 cm out): let go = the hang");
    r.in.stick = 1, r.in.stick_w = (sv_vec3){ 0, 0, 1 };
    for (int n5 = 0; n5 < 30 && r.s.active; n5++) rig_frame(&r);
    CHECK(!r.s.active && r.s.last_end == SOMVR_CLIMB_END_RELEASED && r.s.stand_walks == 1 && r.body.z + 50 >= 100 &&
              r.body.z + 50 < 103.5f, "stand: mark 5: the stick walked him on (root z %g)", r.body.z + 50);
    r.in.stick = 0, r.in.stick_w = (sv_vec3){ 0, 0, 0 };
    /* holding: the push toward the top brings the root over the top (the ray at his axis on it) = over */
    stand_at(&r, 300, 20, 1);
    int n;
    for (n = 0; n < 40 && r.s.active; n++) {
        hand_move(&r, 1, 0, 0, -1);
        rig_frame(&r);
    }
    CHECK(!r.s.active && r.s.last_end == SOMVR_CLIMB_END_OVER && r.s.stand_over && r.s.stand_overs == 1 &&
              NEAR(r.body.z + 50, 100, 1.01),
          "stand: the push onto the top = over at root z %g (frames %d)", r.body.z + 50, n);
    CHECK(r.body.z + 50 >= 100 - 1.01f, "stand: over only with the axis over the top (root z %g)", r.body.z + 50);
    /* a topped hang with the axis 20 cm out (root z 80): let go = the hang; the stick toward the top walks him on */
    stand_at(&r, 300, 30, 1);
    grip(&r, 1, 0);
    frames(&r, 3);
    CHECK(r.s.active && r.s.phase == SOMVR_CLIMB_PH_HANG && r.s.stands == 0, "stand: 20 cm out, let go = the hang");
    r.in.stick = 1, r.in.stick_w = (sv_vec3){ 0.6f, 0, 0.8f }; /* forward-right: the part along the edge walks too */
    int walks = 0;
    for (n = 0; n < 40 && r.s.active; n++) {
        rig_frame(&r);
        walks += (r.o.events2 & SOMVR_CLIMB_E_STAND_WALK) != 0;
    }
    CHECK(!r.s.active && r.s.last_end == SOMVR_CLIMB_END_RELEASED && walks == 1 && r.s.stand_walks == 1 &&
              r.s.stand_walk_cm > 4 && NEAR(r.body.z + 50, 100, 1.5) && r.body.x > 2,
          "stand: the stick walked him onto the top (root z %g x %g, %g cm, frames %d)", r.body.z + 50, r.body.x,
          r.s.stand_walk_cm, n);
    /* the stick away from the top: nothing outward */
    stand_at(&r, 300, 30, 1);
    grip(&r, 1, 0);
    frames(&r, 3);
    r.in.stick = 1, r.in.stick_w = (sv_vec3){ 0, 0, -1 };
    float z0 = r.body.z;
    frames(&r, 20);
    CHECK(r.s.active && NEAR(r.body.z, z0, 0.01), "stand: the stick away from the top moves nothing (dz %g)", r.body.z - z0);
    /* a real wall hang at a top: the feet 60 cm under it: let go = the hang, the stick does nothing */
    stand_at(&r, 240, 42, 1);
    grip(&r, 1, 0);
    frames(&r, 3);
    r.in.stick = 1, r.in.stick_w = (sv_vec3){ 0, 0, 1 };
    z0 = r.body.z;
    frames(&r, 20);
    CHECK(r.s.active && r.s.phase == SOMVR_CLIMB_PH_HANG && r.s.stands == 0 && NEAR(r.body.z, z0, 0.01),
          "stand: the feet 60 cm under the top: the hang stays (%d)", r.s.phase);
}

/* SOMVR_CLIMB_TOPOUT (build 17): the push held at a top's corner goes through 1:1, then stand's over */
static void test_topout(void) {
    for (int on = 0; on < 2; on++) {
        rig_t r;
        top_setup(&r, SOMVR_CLIMB_TOP_PUSH);
        r.c.topout = on, r.c.stand = 1, r.c.over_cm = 200;
        r.ledge = 1;
        pull_to_top(&r);
        frames(&r, 2);
        for (float left = 234 - r.body.y; left > 0.001f; left -= 3) { /* up 1:1: the feet 66 cm under the top */
            hand_move(&r, 1, 0, -fminf(3.0f, left), 0);
            rig_frame(&r);
        }
        for (int i = 0; i < 10; i++) { /* to the face (the ledge holds the root 35 cm off it) */
            hand_move(&r, 1, 0, 0, -2);
            rig_frame(&r);
        }
        frames(&r, 2);
        CHECK(r.s.topped && r.body.y >= 234 && r.body.y < 240 && NEAR(r.body.z + 50, 65, 0.01),
              "topout %d: at the corner (%g %g; the edge step lifted a little)", on, r.body.y, r.body.z + 50);
        r.corner = 1;
        float y0 = r.body.y;
        int n, ons = 0;
        for (n = 0; n < 120 && r.s.active; n++) { /* the push: down and back */
            hand_move(&r, 1, 0, -2, -2);
            rig_frame(&r);
            ons += (r.o.events2 & SOMVR_CLIMB_E_TOP_THR_ON) != 0;
        }
        if (on)
            CHECK(!r.s.active && r.s.last_end == SOMVR_CLIMB_END_OVER && r.s.stand_over && ons == 1 && r.s.to_ons == 1 &&
                      r.body.y >= 297 - 2.01f && r.body.y <= 300 && r.body.z + 50 > 100 && r.s.to_all_cm > 50,
                  "topout on: through the corner onto the top (body y %g, root z %g, %g cm, frames %d)", r.body.y,
                  r.body.z + 50, r.s.to_all_cm, n);
        else
            CHECK(r.s.active && NEAR(r.body.y, y0, 0.01) && NEAR(r.body.z + 50, 65, 0.01) && r.s.to_ons == 0,
                  "topout off: held at the corner (%g %g)", r.body.y, r.body.z + 50);
    }
}

/* build 17: a ledge's top passed on the way up is forgotten with stand (or topout) on: the real top is reached anew */
static void test_retop17(void) {
    for (int on = 0; on < 2; on++) {
        rig_t r;
        float wall0 = g_wall_top;
        g_wall_top = 150; /* a low ledge first */
        rig_init(&r);
        r.c.stand = on;
        r.in.hand[1].p.y = 140;
        grip(&r, 1, 1);
        rig_frame(&r);
        for (int i = 0; i < 60 && !r.s.topped; i++) {
            hand_move(&r, 1, 0, -2, 0);
            rig_frame(&r);
        }
        CHECK(r.s.topped && NEAR(r.s.top_y, 150, 0.01), "retop17 %d: topped at the ledge (%g)", on, r.s.top_y);
        g_wall_top = 300; /* the face goes on: a hold 60 cm over the ledge's top */
        r.in.hand[0].p = (sv_vec3){ -20, 210 - r.body.y, 85 };
        grip(&r, 0, 1);
        frames(&r, 2);
        CHECK(r.s.h[0].holding, "retop17 %d: the hold over the ledge", on);
        if (on)
            CHECK(!r.s.topped && r.s.retops == 1, "retop17 on: the ledge's top forgotten (topped %d)", r.s.topped);
        else
            CHECK(r.s.topped && NEAR(r.s.top_y, 150, 0.01), "retop17 off: still the ledge's top (build 16)");
        if (on) { /* hand over hand to the real top: topped there */
            grip(&r, 1, 0);
            frames(&r, 2);
            for (int i = 0; i < 40; i++) {
                hand_move(&r, 0, 0, -2, 0);
                rig_frame(&r);
            }
            r.in.hand[1].p = (sv_vec3){ 20, 290 - r.body.y, 85 };
            grip(&r, 1, 1);
            frames(&r, 2);
            for (int i = 0; i < 80 && !r.s.topped; i++) {
                hand_move(&r, 1, 0, -2, 0);
                rig_frame(&r);
            }
            CHECK(r.s.topped && NEAR(r.s.top_y, 300, 0.01), "retop17 on: topped at the real top (%g)", r.s.top_y);
        }
        g_wall_top = wall0;
    }
}

/* SOMVR_HEAD_ROCK (build 17): the fade from the head's gap, black, back to Talion's eyes once, the fade in */
static void test_headrock(void) {
    somvr_headrock_cfg c;
    somvr_headrock_cfg_default(&c);
    somvr_headrock s;
    somvr_headrock_init(&s);
    somvr_headrock_out o;
    double t = 100;
    float dt = 1.0f / 90;
    int first = 0, backs = 0, ended = 0, i;
    for (i = 0; i < 10; i++, t += dt) somvr_headrock_step(&s, &c, t, dt, 1, 50, &o);
    CHECK(o.level == 0 && s.episodes == 0, "headrock: clear (%g)", o.level);
    for (i = 0; i < 12; i++, t += dt) { /* the head 3 cm into the rock: black within 0.12 s */
        somvr_headrock_step(&s, &c, t, dt, 1, -3, &o);
        first += o.first;
    }
    CHECK(o.level == 1 && first == 1 && s.episodes == 1 && !o.back, "headrock: black in 12 frames (%g, first %d)", o.level, first);
    for (i = 0; i < 60; i++, t += dt) {
        somvr_headrock_step(&s, &c, t, dt, 1, -3, &o);
        backs += o.back;
        if (o.back) CHECK(s.black_t >= 0 && t - s.black_t >= c.back_s - 1e-6, "headrock: back after %g s of black", t - s.black_t);
    }
    CHECK(backs == 1 && s.backs == 1, "headrock: back to Talion's eyes once (%d)", backs);
    for (i = 0; i < 25; i++, t += dt) {
        somvr_headrock_step(&s, &c, t, dt, 1, 60, &o);
        ended += o.ended;
    }
    CHECK(o.level == 0 && ended == 1 && NEAR(s.min_gap, -3, 1e-4), "headrock: the fade in (%g, ended %d)", o.level, ended);
    for (i = 0; i < 30; i++, t += dt) somvr_headrock_step(&s, &c, t, dt, 1, 3, &o);
    CHECK(NEAR(o.level, 0.5, 1e-3) && s.episodes == 2 && !s.backed, "headrock: 3 cm from a surface: half (%g)", o.level);
    for (i = 0; i < 40; i++, t += dt) somvr_headrock_step(&s, &c, t, dt, 0, 0, &o);
    CHECK(o.level == 0, "headrock: no answer: the fade eases out (%g)", o.level);
    c.back_s = 0;
    somvr_headrock_init(&s);
    for (i = 0; i < 200; i++, t += dt) {
        somvr_headrock_step(&s, &c, t, dt, 1, -5, &o);
        CHECK(!o.back, "headrock: back_s 0: never back");
    }
}

/* ---- build 18's head tether (re-xr-pipeline.md 69): the head off the body past where the follow can take him ---- */
static void test_tether(void) {
    somvr_tether_cfg c;
    somvr_tether_cfg_default(&c);
    somvr_tether s;
    somvr_tether_init(&s);
    somvr_tether_out o;
    double t = 100;
    float dt = 1.0f / 90;
    int first = 0, backs = 0, held = 0, ended = 0, i;
    for (i = 0; i < 90; i++, t += dt) somvr_tether_step(&s, &c, t, dt, 1, 20 + 0.3f * (float)i, &o); /* 20..47 cm */
    CHECK(o.level == 0 && s.episodes == 0, "tether: within 50 cm: nothing (%g)", o.level);
    for (i = 0; i < 30; i++, t += dt) { /* 70 cm: half, eased in over 0.15 s */
        somvr_tether_step(&s, &c, t, dt, 1, 70, &o);
        first += o.first;
        if (i == 2) CHECK(o.level > 0 && o.level < 0.5f, "tether: eased, not a jump (%g)", o.level);
    }
    CHECK(NEAR(o.level, 0.5, 1e-3) && first == 1 && s.episodes == 1 && !o.back, "tether: 70 cm = half (%g)", o.level);
    for (i = 0; i < 90 && !o.held; i++, t += dt) somvr_tether_step(&s, &c, t, dt, 1, 70, &o);
    CHECK(o.held && s.holds == 1 && NEAR(t - s.over_t, c.hold_s, 2 * dt), "tether: held %g s = to black", t - s.over_t);
    for (i = 0; i < 60; i++, t += dt) {
        somvr_tether_step(&s, &c, t, dt, 1, 70, &o);
        backs += o.back, held += o.held;
        if (o.back) CHECK(s.black_t >= 0 && t - s.black_t >= c.back_s - 1e-6, "tether: back after %g s of black", t - s.black_t);
    }
    CHECK(backs == 1 && held == 0 && o.level == 1, "tether: black, back to Talion's eyes once (%d)", backs);
    for (i = 0; i < 40; i++, t += dt) { /* re-latched: the head at the body again */
        somvr_tether_step(&s, &c, t, dt, 1, 2, &o);
        ended += o.ended;
    }
    CHECK(o.level == 0 && ended == 1 && NEAR(s.max_cm, 70, 1e-4), "tether: faded in (%g, ended %d)", o.level, ended);
    for (i = 0; i < 25; i++, t += dt) somvr_tether_step(&s, &c, t, dt, 1, 95, &o); /* stepped off an edge: past black */
    CHECK(o.level == 1 && s.episodes == 2, "tether: past 90 cm = black at once (%g)", o.level);
    backs = 0;
    for (i = 0; i < 30; i++, t += dt) backs += (somvr_tether_step(&s, &c, t, dt, 1, 95, &o), o.back);
    CHECK(backs == 1, "tether: back after 0.3 s of black (%d)", backs);
    for (i = 0; i < 40; i++, t += dt) somvr_tether_step(&s, &c, t, dt, 0, 300, &o);
    CHECK(o.level == 0 && s.backs == 2, "tether: not measured (a climb): the fade eases out (%g)", o.level);
    for (i = 0; i < 300; i++, t += dt) somvr_tether_step(&s, &c, t, dt, 1, 49.9f, &o);
    CHECK(o.level == 0 && s.holds == 1 && s.episodes == 2, "tether: 49.9 cm for 3 s: nothing (%g)", o.level);
    for (i = 0; i < 300; i++, t += dt) somvr_tether_step(&s, &c, t, dt, 1, NAN, &o);
    CHECK(o.level == 0, "tether: NaN: nothing");
    c.hold_s = 0, c.back_s = 0;
    somvr_tether_init(&s);
    for (i = 0; i < 300; i++, t += dt) {
        somvr_tether_step(&s, &c, t, dt, 1, 60, &o);
        CHECK(!o.back && !o.held, "tether: hold 0, back 0: never black by time, never back");
    }
    CHECK(NEAR(o.level, 0.25, 1e-3), "tether: hold 0: the distance's fade only (%g)", o.level);
}

/* ---- build 17's drop mark (re-xr-pipeline.md 55): A held = the landing shown, let go = go, the stick = cancel ---- */
static int g_enemy; /* the mock's enemy_near answer */
static sv_vec3 g_enemy_q;
static int mock_enemy(void *ctx, sv_vec3 p, float r_cm) {
    (void)ctx, (void)r_cm;
    g_enemy_q = p;
    return g_enemy;
}
typedef struct {
    uint32_t ev, ev2;
    int blink, shows, ended;
    sv_vec3 to;
} dm_acc;
/* n frames with A held (a = 1) or not, the outputs gathered */
static void dm_run(rig_t *r, int a, int n, dm_acc *k) {
    r->in.drop = a;
    for (int i = 0; i < n; i++) {
        rig_frame(r);
        k->ev |= r->o.events, k->ev2 |= r->o.events2, k->shows += r->o.dm_show, k->ended |= r->o.ended;
        if (r->o.blink) k->blink++, k->to = r->o.blink_to;
    }
}
static void dm_setup(rig_t *r, float top) {
    desc_setup(r, top);
    r->c.drop_mark = 1;
    r->in.enemy_near = mock_enemy, g_enemy = 0;
    r->in.head_fwd = (sv_vec3){ 0, 0, -1 }; /* facing out over the edge (z 100; the root at z 140) */
}
/* down the face of a top-high wall and both hands open: the hang (1 = hanging) */
static int dm_hang(rig_t *r, float top) {
    dm_setup(r, top);
    r->in.head_fwd = (sv_vec3){ 0, 0, 0 };
    desc_lip(r);
    int tall = top > 500; /* the 3 m wall: test_descent's way down (the feet under 1 m over the ground) */
    desc_over(r, tall ? 40 : 20);
    for (int k = 0, h = 0; k < (tall ? 3 : 4); k++, h = 1 - h) desc_hold(r, h, tall ? 25 : 15);
    grip(r, 0, 0), grip(r, 1, 0);
    frames(r, 3);
    return r->s.active && r->s.phase == SOMVR_CLIMB_PH_HANG;
}

static void test_drop_mark(void) {
    rig_t r;
    dm_acc k;
    float wall0 = g_wall_top;
    int ground0 = g_ground_on;
    /* on foot at a 9 m edge: the press shows the ring (40 cm past the edge, on the ground) and the line from the top's
     * height; held: nothing moves; let go after 0.3 s: the blink there */
    dm_setup(&r, 900);
    memset(&k, 0, sizeof k);
    dm_run(&r, 1, 1, &k);
    CHECK((k.ev2 & SOMVR_CLIMB_E_DM_SHOW) && r.o.dm_show && !k.blink && r.s.dm == 1 && r.s.dm_kind == SOMVR_CLIMB_DM_BLINK &&
              NEAR3(r.o.dm_land, 0, 0, 40, 0.5) && NEAR3(r.o.dm_top, 0, 900, 40, 0.5) && NEAR(r.s.bl_h, 900, 0.5),
          "the press: the ring at (%g %g %g), the line from %g", r.o.dm_land.x, r.o.dm_land.y, r.o.dm_land.z, r.o.dm_top.y);
    dm_run(&r, 1, 44, &k);
    CHECK(!k.blink && k.shows == 45 && NEAR(r.o.dm_held, 1, 1e-3) && NEAR3(r.body, 0, 900, 90, 0.01),
          "held 0.5 s: shown every frame (%d), nothing moves", k.shows);
    memset(&k, 0, sizeof k);
    dm_run(&r, 0, 1, &k);
    CHECK(k.blink == 1 && (k.ev2 & SOMVR_CLIMB_E_DM_GO) && (k.ev & SOMVR_CLIMB_E_BLINK) && (k.ev & SOMVR_CLIMB_E_EDGE) &&
              NEAR3(k.to, 0, 105, 40, 0.5) && !r.o.dm_show && r.s.dm == 0 && r.s.dm_goes == 1 && r.s.edge_blinks == 1,
          "let go: the blink to (%g %g %g)", k.to.x, k.to.y, k.to.z);
    /* a tap (under 0.3 s): nothing; the ring stays to 0.6 s from the press, then goes */
    dm_setup(&r, 900);
    memset(&k, 0, sizeof k);
    dm_run(&r, 1, 10, &k);
    dm_run(&r, 0, 1, &k);
    CHECK(!k.blink && (k.ev2 & SOMVR_CLIMB_E_DM_TAP) && r.o.dm_show && r.s.dm == 2 && r.o.dm_held == 0 && r.s.dm_taps == 1,
          "a tap: nothing, the ring still shown");
    dm_run(&r, 0, 50, &k);
    CHECK(!k.blink && !r.o.dm_show && r.s.dm == 0 && k.shows >= 52 && k.shows <= 56,
          "... gone 0.6 s after the press (%d frames shown)", k.shows);
    /* the stick while A is held: cancel; held on and let go: nothing */
    dm_setup(&r, 900);
    memset(&k, 0, sizeof k);
    dm_run(&r, 1, 10, &k);
    r.in.stick = 0.8f;
    dm_run(&r, 1, 1, &k);
    CHECK((k.ev2 & SOMVR_CLIMB_E_DM_CANCEL) && r.s.dm_cancel_why == SOMVR_CLIMB_DMC_STICK && !r.o.dm_show && r.s.dm == 0 &&
              r.s.dm_stick_wait,
          "the stick: cancelled (the stick waits to come back: somvr.c holds the walk off)");
    dm_run(&r, 1, 3, &k);
    CHECK(r.s.dm_stick_wait, "... held on: still waiting");
    r.in.stick = 0;
    dm_run(&r, 1, 1, &k);
    CHECK(!r.s.dm_stick_wait, "... back: the stick walks again");
    dm_run(&r, 1, 40, &k);
    dm_run(&r, 0, 5, &k);
    CHECK(!k.blink && !r.o.dm_show && r.s.dm_goes == 0, "... A held on and let go: nothing");
    /* the stick under half: no cancel */
    dm_setup(&r, 900);
    memset(&k, 0, sizeof k);
    r.in.stick = 0.3f;
    dm_run(&r, 1, 40, &k);
    r.in.stick = 0;
    dm_run(&r, 0, 1, &k);
    CHECK(k.blink == 1 && !(k.ev2 & SOMVR_CLIMB_E_DM_CANCEL), "the stick at 0.3: no cancel, the go");
    /* moved 40 cm on foot (an orc's push): cancel; a menu: cancel; the bow out: cancel */
    dm_setup(&r, 900);
    memset(&k, 0, sizeof k);
    dm_run(&r, 1, 5, &k);
    r.body.x += 40;
    dm_run(&r, 1, 1, &k);
    CHECK((k.ev2 & SOMVR_CLIMB_E_DM_CANCEL) && r.s.dm_cancel_why == SOMVR_CLIMB_DMC_MOVED && !r.o.dm_show, "moved: cancelled");
    dm_setup(&r, 900);
    memset(&k, 0, sizeof k);
    dm_run(&r, 1, 5, &k);
    r.in.frozen = 1;
    dm_run(&r, 1, 1, &k);
    r.in.frozen = 0;
    dm_run(&r, 1, 40, &k);
    dm_run(&r, 0, 2, &k);
    CHECK((k.ev2 & SOMVR_CLIMB_E_DM_CANCEL) && r.s.dm_cancel_why == SOMVR_CLIMB_DMC_GAME && !k.blink && !r.o.dm_show,
          "a menu: cancelled (A held through it does nothing after)");
    dm_setup(&r, 900);
    memset(&k, 0, sizeof k);
    dm_run(&r, 1, 5, &k);
    r.in.nograb = 1;
    dm_run(&r, 1, 1, &k);
    CHECK((k.ev2 & SOMVR_CLIMB_E_DM_CANCEL) && r.s.dm_cancel_why == SOMVR_CLIMB_DMC_GAME, "the bow out: cancelled");
    /* no safe landing on foot: no ground beyond, water, an orc there, no room: no ring, E_DM_NO, held and let go: nothing */
    struct { const char *what; int wet, enemy, far; float roof0, roof1; int why; } nos[] = {
        { "no ground", 0, 0, 1, 0, 0, SOMVR_CLIMB_BL_NOGROUND }, { "water", 1, 0, 0, 0, 0, SOMVR_CLIMB_BL_WET },
        { "an orc", 0, 1, 0, 0, 0, SOMVR_CLIMB_BL_ENEMY }, { "no room", 0, 0, 0, -200, 90, SOMVR_CLIMB_BL_NOROOM } };
    for (unsigned i = 0; i < sizeof nos / sizeof nos[0]; i++) {
        dm_setup(&r, 900);
        g_wet = nos[i].wet, g_enemy = nos[i].enemy;
        if (nos[i].far) r.in.head_fwd = (sv_vec3){ 0, 0, 1 }, r.body.z = 50; /* facing over the top's far side */
        g_roof_y = nos[i].roof1 > nos[i].roof0 ? 150.0f : 0.0f, g_roof_z0 = nos[i].roof0, g_roof_z1 = nos[i].roof1;
        memset(&k, 0, sizeof k);
        dm_run(&r, 1, 40, &k);
        dm_run(&r, 0, 3, &k);
        CHECK((k.ev2 & SOMVR_CLIMB_E_DM_NO) && !(k.ev2 & SOMVR_CLIMB_E_DM_SHOW) && !k.shows && !k.blink && r.s.bl_why == nos[i].why &&
                  r.s.dm == 0,
              "%s: no ring, the no (%s)", nos[i].what, somvr_climb_bl_name(r.s.bl_why));
    }
    CHECK(NEAR3(g_enemy_q, 0, 105, 40, 0.5), "the orc looked for at Talion's root at the landing (%g %g %g)", g_enemy_q.x,
          g_enemy_q.y, g_enemy_q.z);
    g_wet = 0, g_enemy = 0, g_roof_y = 0;
    /* a roof over the first spots: the ring further out (z 20: 60 cm past the edge's spot) */
    dm_setup(&r, 900);
    g_roof_y = 150, g_roof_z0 = 30, g_roof_z1 = 90;
    memset(&k, 0, sizeof k);
    dm_run(&r, 1, 30, &k);
    dm_run(&r, 0, 1, &k);
    CHECK(k.blink && NEAR3(k.to, 0, 105, 20, 0.5) && NEAR(r.s.dm_out, 20, 0.01), "a roof over the first spot: the ring 20 cm "
          "further out (%g %g %g)", k.to.x, k.to.y, k.to.z);
    g_roof_y = 0;
    /* a short edge (3 m) or none ahead on foot: nothing at all (no ring, no buzz), as before */
    dm_setup(&r, WALL_TOP);
    memset(&k, 0, sizeof k);
    dm_run(&r, 1, 40, &k);
    dm_run(&r, 0, 1, &k);
    CHECK(!k.ev2 && !k.shows && !k.blink && (k.ev & SOMVR_CLIMB_E_BLINK_NONE) && r.s.bl_why == SOMVR_CLIMB_BL_SHORT,
          "a 3 m edge: nothing (%s)", somvr_climb_bl_name(r.s.bl_why));
    dm_setup(&r, 900);
    r.in.head_fwd = (sv_vec3){ 0, 0, 0 };
    memset(&k, 0, sizeof k);
    dm_run(&r, 1, 40, &k);
    dm_run(&r, 0, 1, &k);
    CHECK(!k.ev2 && !k.shows && !k.blink && r.s.bl_why == SOMVR_CLIMB_BL_NOEDGE, "no edge ahead: nothing");

    /* hanging on a 9 m wall: A held = the ring 40 cm out from the wall, the line from the feet; let go = the blink */
    CHECK(dm_hang(&r, 900), "the hang on the 9 m wall");
    sv_vec3 root = r.in.root, b0 = r.body;
    memset(&k, 0, sizeof k);
    dm_run(&r, 1, 1, &k);
    CHECK((k.ev2 & SOMVR_CLIMB_E_DM_SHOW) && r.s.dm_climb && r.s.dm_kind == SOMVR_CLIMB_DM_BLINK &&
              NEAR3(r.o.dm_land, root.x, 0, root.z - 40, 0.5) && NEAR(r.o.dm_top.y, root.y - 100, 0.5) && r.s.active &&
              r.s.phase == SOMVR_CLIMB_PH_HANG,
          "hanging: the ring at (%g %g %g), the line from %g", r.o.dm_land.x, r.o.dm_land.y, r.o.dm_land.z, r.o.dm_top.y);
    dm_run(&r, 1, 35, &k);
    dm_run(&r, 0, 1, &k);
    CHECK(k.blink == 1 && NEAR3(k.to, root.x, 105, root.z - 40, 0.5) && r.s.phase == SOMVR_CLIMB_PH_BLINK &&
              NEAR3(r.body, b0.x, b0.y, b0.z, 0.01) && !(k.ev & SOMVR_CLIMB_E_EDGE),
          "let go: the blink (to %g %g %g), still until the teleport", k.to.x, k.to.y, k.to.z);
    somvr_climb_stop(&r.s, SOMVR_CLIMB_END_BLINK, &r.o);
    /* hanging: a tap does nothing (he hangs on); a new hold cancels; the landing follows him; no blink now: the no */
    CHECK(dm_hang(&r, 900), "the hang again");
    memset(&k, 0, sizeof k);
    dm_run(&r, 1, 5, &k);
    dm_run(&r, 0, 30, &k);
    CHECK(!k.blink && !k.ended && (k.ev2 & SOMVR_CLIMB_E_DM_TAP) && r.s.active && r.s.phase == SOMVR_CLIMB_PH_HANG,
          "hanging: a tap: nothing");
    root = r.in.root;
    memset(&k, 0, sizeof k);
    dm_run(&r, 1, 5, &k);
    r.body.x += 20; /* (the hang moved by something: the landing found again) */
    dm_run(&r, 1, 10, &k);
    CHECK((k.ev2 & SOMVR_CLIMB_E_DM_MOVE) && NEAR(r.o.dm_land.x, root.x + 20, 0.5) && r.o.dm_show, "the landing follows (x %g)",
          r.o.dm_land.x);
    r.in.hand[0].p = (sv_vec3){ -20, r.in.head_y - 90 - r.body.y, 80 - r.body.z }; /* a hold on the face */
    grip(&r, 0, 1);
    dm_run(&r, 1, 2, &k);
    CHECK((k.ev2 & SOMVR_CLIMB_E_DM_CANCEL) && r.s.dm_cancel_why == SOMVR_CLIMB_DMC_GRAB && r.s.h[0].holding && !r.o.dm_show,
          "a new hold: cancelled (%d)", r.s.dm_cancel_why);
    grip(&r, 0, 0);
    dm_run(&r, 0, 3, &k);
    r.in.can_blink = 0;
    memset(&k, 0, sizeof k);
    dm_run(&r, 1, 40, &k);
    dm_run(&r, 0, 3, &k);
    CHECK((k.ev2 & SOMVR_CLIMB_E_DM_NO) && !k.shows && !k.blink && !k.ended && r.s.active && r.s.bl_why == SOMVR_CLIMB_BL_CANNOT,
          "no blink now over 9 m: the no, he stays (not the fall)");
    r.in.can_blink = 1;
    /* hanging over a short drop (the 3 m wall): A held = the ring under him, let go = the fall; the stick cancels and the
     * hang's stick waits for the stick to come back */
    CHECK(dm_hang(&r, WALL_TOP), "the hang on the 3 m wall");
    root = r.in.root;
    memset(&k, 0, sizeof k);
    dm_run(&r, 1, 1, &k);
    CHECK((k.ev2 & SOMVR_CLIMB_E_DM_SHOW) && r.s.dm_kind == SOMVR_CLIMB_DM_FALL && NEAR3(r.o.dm_land, root.x, 0, root.z, 0.5) &&
              NEAR(r.o.dm_top.y, root.y - 100, 0.5),
          "a short drop: the ring under him (%g %g %g)", r.o.dm_land.x, r.o.dm_land.y, r.o.dm_land.z);
    dm_run(&r, 1, 30, &k);
    CHECK(r.s.active && !k.ended, "held: he hangs on");
    dm_run(&r, 0, 1, &k);
    CHECK(k.ended && !r.s.active && r.s.last_end == SOMVR_CLIMB_END_DROP && (k.ev & SOMVR_CLIMB_E_DROP) && !k.blink &&
              r.s.dm_falls == 1,
          "let go: the fall (%s)", somvr_climb_end_name(r.s.last_end));
    CHECK(dm_hang(&r, WALL_TOP), "the hang on the 3 m wall again");
    r.c.hang_stick_cm = 400; /* (the stick would stand him here) */
    memset(&k, 0, sizeof k);
    dm_run(&r, 1, 5, &k);
    r.in.stick = 0.8f;
    dm_run(&r, 1, 10, &k);
    CHECK((k.ev2 & SOMVR_CLIMB_E_DM_CANCEL) && !(k.ev2 & SOMVR_CLIMB_E_STICK_DROP) && r.s.active, "the stick: cancelled, he hangs");
    dm_run(&r, 0, 10, &k);
    CHECK(!(k.ev2 & SOMVR_CLIMB_E_STICK_DROP) && r.s.active && r.s.dm_stick_wait, "... the stick held on: still the hang");
    r.in.stick = 0;
    dm_run(&r, 0, 2, &k);
    r.in.stick = 0.8f;
    dm_run(&r, 0, 2, &k);
    CHECK((k.ev2 & SOMVR_CLIMB_E_STICK_DROP) && !r.s.active, "... back and pushed again: the hang's stick (he stands)");
    r.in.stick = 0;
    /* on the top with the lip held (descending): from beyond the edge */
    dm_setup(&r, 900);
    r.in.head_fwd = (sv_vec3){ 0, 0, 0 };
    desc_lip(&r);
    memset(&k, 0, sizeof k);
    dm_run(&r, 1, 30, &k);
    dm_run(&r, 0, 1, &k);
    CHECK(k.blink && NEAR(k.to.y, 105, 0.5) && k.to.z < WALL_Z - 30 && NEAR(r.s.bl_h, 900, 1), "the lip from the top: the blink "
          "from beyond the edge (%g cm, to z %g)", r.s.bl_h, k.to.z);
    /* drop mark off: A at once, as before */
    dm_setup(&r, 900);
    r.c.drop_mark = 0;
    memset(&k, 0, sizeof k);
    dm_run(&r, 1, 1, &k);
    CHECK(k.blink == 1 && !k.ev2 && !k.shows, "off: the blink on the press, as build 16");
    g_wall_top = wall0, g_ground_on = ground0;
}

/* build 18: SOMVR_CLIMB_FLING_UP - the cap on the up part alone, the push into the wall mostly turned up it, the
 * threshold on that part; in flight a push into the face the face holds dropped (the rise goes on) */
static void test_fling_up(void) {
    rig_t r;
    rig_init(&r);
    r.c.fling = 1, r.c.fling_up = 1, r.c.fling_max = 650, r.c.fling_min = 110, r.c.fling_side = 0.7f;
    sv_vec3 v, tw = { 0, 0, 1 };
    /* the session's strongest: the hand 922 cm/s down and back = the body up 700, toward the wall 597 */
    int f = somvr_climb_fling_rule(&r.s, &r.c, (sv_vec3){ 0, 700, 597 }, tw, &v);
    CHECK(f == 1 && NEAR(v.y, 650, 1e-3) && NEAR(v.z, 175, 1e-3) && NEAR(r.s.fl_wallup, 211, 1e-3) && r.s.fl_capped,
          "fling up: strong: up %g (capped), toward %g, turned up %g", v.y, v.z, r.s.fl_wallup);
    float h = v.y * v.y / (2 * 981);
    CHECK(h > 200 && h < 220, "fling up: the strong arc %g cm (build 13: 74)", h);
    /* a medium one: up 300, toward 250 -> 300 + (250 - 75) / 2 = 387.5 */
    f = somvr_climb_fling_rule(&r.s, &r.c, (sv_vec3){ 0, 300, 250 }, tw, &v);
    CHECK(f == 1 && NEAR(v.y, 387.5, 1e-3) && NEAR(v.z, 75, 1e-3) && !r.s.fl_capped, "fling up: medium up %g toward %g", v.y,
          v.z);
    /* slow: up 60, toward 80 -> 60 + 32.5 = 92.5 under 110: the hang */
    f = somvr_climb_fling_rule(&r.s, &r.c, (sv_vec3){ 0, 60, 80 }, tw, &v);
    CHECK(f == 0 && NEAR(r.s.fl_up, 92.5, 1e-3), "fling up: a slow let-go hangs (up %g)", r.s.fl_up);
    /* away from the wall: dropped, nothing turned */
    f = somvr_climb_fling_rule(&r.s, &r.c, (sv_vec3){ 0, 300, -300 }, tw, &v);
    CHECK(f == 1 && NEAR(v.y, 300, 1e-3) && NEAR(v.z, 0, 1e-3) && NEAR(r.s.fl_out, 300, 1e-3) && r.s.fl_wallup == 0,
          "fling up: never away (up %g, z %g)", v.y, v.z);
    /* sideways x 0.7, at most 0.6 of the up */
    f = somvr_climb_fling_rule(&r.s, &r.c, (sv_vec3){ 400, 300, 0 }, tw, &v);
    CHECK(f == 1 && NEAR(v.x, 180, 1e-3) && NEAR(v.y, 300, 1e-3), "fling up: sideways %g (0.6 x 300)", v.x);
    /* at a top: the push into it kept (the glide), nothing turned */
    r.s.topped = 1;
    f = somvr_climb_fling_rule(&r.s, &r.c, (sv_vec3){ 0, 300, 250 }, tw, &v);
    CHECK(f == 1 && NEAR(v.y, 300, 1e-3) && NEAR(v.z, 250, 1e-3) && r.s.fl_wallup == 0, "fling up: at a top all kept (%g %g)",
          v.y, v.z);
    /* off: build 13's (the cap on the whole) */
    r.s.topped = 0, r.c.fling_up = 0, r.c.fling_max = 500;
    f = somvr_climb_fling_rule(&r.s, &r.c, (sv_vec3){ 0, 700, 597 }, tw, &v);
    CHECK(f == 1 && v.y < 400 && r.s.fl_capped, "fling up off: build 13's split (up %g)", v.y);
    /* a flight at the face: the capsule against it (the mock's ledge holds the root 35 cm off it): the push into it
     * dropped once held, the rise to the arc's top */
    for (int on = 0; on < 2; on++) {
        fl_setup(&r);
        r.c.fling_up = on, r.c.fling_max = 650, r.c.fling_min = 110, r.ledge = 1;
        for (int i = 0; i < 8; i++) hand_move(&r, 1, 0, 0, -2), rig_frame(&r); /* to the face */
        float y0 = r.body.y;
        fl_throw(&r, 1, 0, -5, -4, 10); /* down and back 450 x 360 cm/s at 90 Hz */
        int n = fl_fly(&r, 200);
        if (on)
            CHECK(r.s.fl_why == SOMVR_CLIMB_FL_APEX && r.body.y - y0 > 100 && r.s.fl_into_cut >= 1 && NEAR(r.body.z + 50, 65, 0.6),
                  "fling up: at the face up %g cm to the arc's top (%d frames, the push into it dropped %llu), never out (root z %g)",
                  r.body.y - y0, n, (unsigned long long)r.s.fl_into_cut, r.body.z + 50);
        else
            printf("fling up off at the face: rose %.1f cm (why %d)\n", r.body.y - y0, r.s.fl_why);
    }
}

/* build 20: the A mantle - topped at the wall's top (300), the eyes `over` cm over it (the feet under it), then (hang)
 * both hands open: the topped hang of the user's build 19b session (mark 2: the eyes 110 cm over the top for 16 s) */
static void am_setup(rig_t *r, int mode, float over, int hang) {
    top_setup(r, SOMVR_CLIMB_TOP_PUSH);
    r->c.letgo = SOMVR_CLIMB_LETGO_HANG, r->c.over_cm = 200, r->c.stand = 1, r->c.descent = 1, r->c.drop_mark = 1;
    r->c.mantle = mode;
    r->in.can_blink = 1, r->in.enemy_near = mock_enemy, g_enemy = 0;
    pull_to_top(r);
    frames(r, 2);
    for (float left = 300 + over - r->head_track_y - r->body.y; left > 0.001f; left -= 3) { /* up 1:1 */
        hand_move(r, 1, 0, -fminf(3.0f, left), 0);
        rig_frame(r);
    }
    if (hang) grip(r, 1, 0);
    frames(r, 3);
}
typedef struct {
    int frames, shows, ended, blink, show_ev, go_ev, no_ev, end_ev;
    float max_step, cross_feet; /* the largest move a frame; the feet over the top at the first frame past the face */
    sv_vec3 to;
} am_acc;
/* A pressed (one frame) and n frames after it */
static void am_run(rig_t *r, int a, int n, am_acc *k) {
    memset(k, 0, sizeof *k);
    k->cross_feet = -1e9f;
    for (int i = 0; i < n + a; i++) {
        sv_vec3 b0 = r->body;
        r->in.drop = a && i == 0;
        rig_frame(r);
        float st = sqrtf((r->body.x - b0.x) * (r->body.x - b0.x) + (r->body.y - b0.y) * (r->body.y - b0.y) +
                         (r->body.z - b0.z) * (r->body.z - b0.z));
        k->max_step = fmaxf(k->max_step, st);
        if (r->body.z + 50 > WALL_Z && b0.z + 50 <= WALL_Z) k->cross_feet = r->body.y - WALL_TOP;
        k->frames++, k->shows += r->o.am_show, k->ended |= r->o.ended;
        k->show_ev += (r->o.events2 & SOMVR_CLIMB_E_AM_SHOW) != 0, k->go_ev += (r->o.events2 & SOMVR_CLIMB_E_AM_GO) != 0;
        k->no_ev += (r->o.events2 & SOMVR_CLIMB_E_AM_NO) != 0, k->end_ev += (r->o.events2 & SOMVR_CLIMB_E_AM_END) != 0;
        if (r->o.blink) k->blink++, k->to = r->o.blink_to;
        if (k->ended && i > 2) break;
    }
    r->in.drop = 0;
}
static void test_mantle20(void) {
    rig_t r;
    am_acc k;
    /* off (build 19b): hanging with the eyes 110 cm over the top, A is the drop mark's (a tap: nothing moves); nothing
     * else gets him on: still hanging under the top after 2 s */
    am_setup(&r, 0, 110, 1);
    sv_vec3 b0 = r.body;
    CHECK(r.s.active && r.s.phase == SOMVR_CLIMB_PH_HANG && r.s.topped && NEAR(r.body.y + 170, 410, 0.5) &&
              NEAR(r.body.z + 50, 50, 0.01) && !r.o.am_show,
          "mantle off: the topped hang, the eyes 110 cm over the top (body %g %g, phase %d)", r.body.y, r.body.z, r.s.phase);
    am_run(&r, 1, 180, &k);
    CHECK(r.s.active && r.s.phase == SOMVR_CLIMB_PH_HANG && NEAR3(r.body, b0.x, b0.y, b0.z, 0.01) && !k.go_ev && !k.shows &&
              r.s.dm_presses == 1 && r.s.am_presses == 0,
          "mantle off: A = the drop mark's tap, he stays hanging (body %g %g %g)", r.body.x, r.body.y, r.body.z);
    /* glide: the ring shows while A would mantle; A = the glide up and in onto the landing, he stands */
    am_setup(&r, 1, 110, 1);
    CHECK(r.s.am_ok && r.o.am_show && NEAR3(r.o.am_land, 0, 300, 140, 0.5) && r.s.am_shows == 1,
          "mantle glide: the landing shown 40 cm in from the edge (%g %g %g; shows %llu)", r.o.am_land.x, r.o.am_land.y,
          r.o.am_land.z, (unsigned long long)r.s.am_shows);
    am_run(&r, 0, 30, &k);
    CHECK(k.shows == 30 && !k.show_ev && r.s.am_shows == 1 && r.s.active, "mantle glide: shown every frame, one show (%d)",
          k.shows);
    b0 = r.body;
    am_run(&r, 1, 120, &k);
    CHECK(k.go_ev == 1 && k.end_ev == 1 && !r.s.active && r.s.last_end == SOMVR_CLIMB_END_OVER &&
              r.s.am_end == SOMVR_CLIMB_AME_DONE && NEAR(r.body.y, 303, 1.0) && NEAR(r.body.z + 50, 140, 1.0) &&
              NEAR(r.body.x, 0, 0.01) && r.s.am_dones == 1 && r.s.dm_presses == 0,
          "mantle glide: A = onto the top: he stands at the landing (body %g, root z %g; end %d)", r.body.y, r.body.z + 50,
          r.s.am_end);
    float L = r.s.am_len[SOMVR_CLIMB_AM_N];
    CHECK(k.max_step <= 350.0f / 90 + 0.5f && k.cross_feet >= 10 - 2 && L > 120 && L < 160 &&
              k.frames <= (int)(L / 350 * 90) + 6,
          "mantle glide: smooth (at most %.2f cm a frame), the feet %.1f cm over the top at the edge, %.0f cm in %d frames",
          k.max_step, k.cross_feet, L, k.frames);
    printf("mantle glide from the eyes 110 cm over the top, 50 cm off the face: %.0f cm in %d frames (%.2f s), the feet %.1f "
           "cm over the edge\n", L, k.frames, k.frames / 90.0, k.cross_feet);
    /* ... from a hold on the top's edge (holding): the same; the closed grip takes no new hold after it */
    am_setup(&r, 1, 60, 0);
    CHECK(r.s.active && r.s.phase == SOMVR_CLIMB_PH_HOLD && r.o.am_show, "mantle glide: holding, the eyes 60 over: shown");
    am_run(&r, 1, 120, &k);
    frames(&r, 30);
    CHECK(k.go_ev == 1 && !r.s.active && r.s.am_dones == 1 && NEAR(r.body.y, 303, 1.0) && NEAR(r.body.z + 50, 140, 1.0) &&
              r.s.climbs == 1,
          "mantle glide: from a hold, onto the top; the closed grip no new climb (body %g %g, climbs %llu)", r.body.y,
          r.body.z + 50, (unsigned long long)r.s.climbs);
    /* too low (the eyes 15 cm over the top: at it, under am_head_cm): A says why, nothing moves, no drop */
    am_setup(&r, 1, 15, 1);
    b0 = r.body;
    CHECK(!r.o.am_show && r.s.phase == SOMVR_CLIMB_PH_HANG, "mantle glide: the eyes 15 over: no ring");
    am_run(&r, 1, 60, &k);
    CHECK(k.no_ev == 1 && !k.go_ev && r.s.am_why == SOMVR_CLIMB_AM_LOW && r.s.active && NEAR3(r.body, b0.x, b0.y, b0.z, 0.01) &&
              r.s.dm_presses == 0 && r.s.am_lows == 1,
          "mantle glide: too low: E_AM_NO (why %d), he stays, no drop mark", r.s.am_why);
    /* a roof 90 cm over the top: no room on it: no ring, A says why */
    g_roof_y = 390, g_roof_z0 = 100, g_roof_z1 = 300;
    am_setup(&r, 1, 60, 1);
    CHECK(!r.o.am_show && r.s.active, "mantle glide: a roof over the top: no ring");
    am_run(&r, 1, 30, &k);
    CHECK(k.no_ev == 1 && r.s.am_why == SOMVR_CLIMB_AM_NOROOM && r.s.active && r.s.am_nofloors == 1,
          "mantle glide: no room: E_AM_NO (why %d)", r.s.am_why);
    g_roof_y = 0;
    /* not at a top (mid-face, the eyes 130 cm under the top): A is the drop mark's as ever */
    rig_init(&r);
    r.c.letgo = SOMVR_CLIMB_LETGO_HANG, r.c.descent = 1, r.c.drop_mark = 1, r.c.mantle = 1;
    r.in.can_blink = 1, r.in.enemy_near = mock_enemy;
    grip(&r, 1, 1);
    frames(&r, 3);
    for (int i = 0; i < 10; i++) hand_move(&r, 1, 0, -3, 0), rig_frame(&r);
    grip(&r, 1, 0);
    frames(&r, 3);
    CHECK(r.s.active && r.s.phase == SOMVR_CLIMB_PH_HANG && !r.s.topped && !r.o.am_show, "mantle glide: a hang mid-face");
    am_run(&r, 1, 2, &k);
    CHECK(!k.no_ev && !k.go_ev && r.s.am_presses == 0 && r.s.dm_presses == 1 && r.s.active,
          "mantle glide: under the top: A = the drop mark (dm %d, presses %llu)", r.s.dm, (unsigned long long)r.s.dm_presses);
    /* blocked (the game takes none of the moves): 0.25 s behind = the hang (no floor under him) */
    am_setup(&r, 1, 110, 1);
    b0 = r.body;
    r.apply = 0;
    am_run(&r, 1, 60, &k);
    CHECK(k.end_ev == 1 && r.s.active && r.s.phase == SOMVR_CLIMB_PH_HANG && r.s.am_end == SOMVR_CLIMB_AME_HANG &&
              r.s.am_blocks == 1,
          "mantle glide: blocked: the hang (end %d, phase %d)", r.s.am_end, r.s.phase);
    /* a menu half way: the glide waits (nothing moves), then goes on to the landing */
    am_setup(&r, 1, 110, 1);
    am_run(&r, 1, 15, &k);
    b0 = r.body;
    r.in.frozen = 1;
    frames(&r, 40);
    CHECK(r.s.phase == SOMVR_CLIMB_PH_MANTLE && sqrtf((r.body.y - b0.y) * (r.body.y - b0.y) + (r.body.z - b0.z) * (r.body.z - b0.z)) < 4.5f,
          "mantle glide: a menu: it waits (moved %g)", r.body.y - b0.y);
    r.in.frozen = 0;
    am_run(&r, 0, 120, &k);
    CHECK(!r.s.active && r.s.am_end == SOMVR_CLIMB_AME_DONE && NEAR(r.body.y, 303, 1.0) && NEAR(r.body.z + 50, 140, 1.0),
          "mantle glide: after the menu, on the top (%g %g)", r.body.y, r.body.z + 50);
    /* blink: A = the pullover's quick blink onto the landing */
    am_setup(&r, 2, 110, 1);
    am_run(&r, 1, 3, &k);
    CHECK(k.go_ev == 1 && k.blink == 1 && NEAR3(k.to, 0, 300 + 102 + 5, 140, 0.5) && r.s.phase == SOMVR_CLIMB_PH_BLINK &&
              r.s.am_blinks == 1,
          "mantle blink: the blink onto the landing (%g %g %g)", k.to.x, k.to.y, k.to.z);
    r.in.can_blink = 0;
    am_setup(&r, 2, 110, 1);
    r.in.can_blink = 0;
    am_run(&r, 1, 3, &k);
    CHECK(k.no_ev == 1 && !k.blink && r.s.am_why == SOMVR_CLIMB_AM_NOBLINK && r.s.phase == SOMVR_CLIMB_PH_HANG,
          "mantle blink: no blink now: E_AM_NO, the hang");
}

int main(void) {
    test_mantle20();
    test_through();
    test_face_toward();
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
    test_fling();
    test_descent();
    test_edge();
    test_overhang();
    test_unpin();
    test_pullover();
    test_hang_stick();
    test_hug();
    test_stand();
    test_headrock();
    test_tether();
    test_topout();
    test_retop17();
    test_fling_up();
    test_drop_mark();
    g_desc_all = 1, g_deep = 1; /* the climbs up with descent on: the same answers (the ground 50 m down for A) */
    test_push();
    test_sequence();
    test_hang();
    test_fling();
    g_desc_all = 0, g_deep = 0;
    printf("test_climb: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
