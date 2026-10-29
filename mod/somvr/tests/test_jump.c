/* test_jump.c - unit tests of somvr_jump.c, build 22's jump and vault (docs/re-xr-pipeline.md 91): the arc in closed form
 * (the rise, the air time, the per-tick step at 90 and 20 Hz), the soft landing (no step under the floor, the stop spread
 * over ticks, the settle), a ceiling, a landing on a higher floor and off a ledge, a floor the rays missed, the game's
 * fall past the depth, lost / caught by the climb; the vault's plan in a world of boxes (a waist-high wall, a fence, a
 * deep top, too high, a step, too far and in reach at a run, a roof over the landing, a ledge behind, no answer) and its
 * arc run through a box proxy (the feet over the wall, down on the ring); A's owner. */
#include "../somvr_jump.h"

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
static float hlen_(sv_vec3 a) { return sqrtf(a.x * a.x + a.z * a.z); }

/* ---- a world of boxes (test_wstep.c's) ------------------------------------------------------------------------------ */
typedef struct { float lo[3], hi[3]; int wet, chr; } box;
static box W[16];
static int NW, DEAD;
static void wreset(void) { NW = 0, DEAD = 0; }
static void add(float x0, float y0, float z0, float x1, float y1, float z1)
{
    W[NW++] = (box){ { x0, y0, z0 }, { x1, y1, z1 }, 0, 0 };
}
static void ground(void) { add(-3000, -50, -3000, 3000, 0, 3000); }

static int ray(void *ctx, int hand, sv_vec3 from, sv_vec3 dir, float len, somvr_climb_hit *out)
{
    (void)ctx, (void)hand;
    if (DEAD)
        return 0;
    const float f[3] = { from.x, from.y, from.z }, d[3] = { dir.x, dir.y, dir.z };
    float best = len + 1;
    int bi = -1;
    for (int i = 0; i < NW; i++) {
        float t0 = 0, t1 = len;
        int ok = 1;
        for (int k = 0; k < 3 && ok; k++) {
            if (fabsf(d[k]) < 1e-9f) {
                ok = f[k] >= W[i].lo[k] && f[k] <= W[i].hi[k];
                continue;
            }
            float a = (W[i].lo[k] - f[k]) / d[k], b = (W[i].hi[k] - f[k]) / d[k];
            if (a > b) { float x = a; a = b; b = x; }
            t0 = a > t0 ? a : t0, t1 = b < t1 ? b : t1;
            ok = t0 <= t1;
        }
        if (ok && t0 < best)
            best = t0, bi = i;
    }
    memset(out, 0, sizeof *out);
    if (bi >= 0 && best <= len) {
        out->hit = 1, out->dist = best, out->wet = W[bi].wet, out->chr = W[bi].chr;
        out->point = (sv_vec3){ f[0] + d[0] * best, f[1] + d[1] * best, f[2] + d[2] * best };
    }
    return 1;
}

/* the floor under a point (the highest box top under it, at most 300 cm over y) */
static float floor_at(float x, float z, float y)
{
    somvr_climb_hit h;
    if (ray(NULL, -1, (sv_vec3){ x, y + 300, z }, (sv_vec3){ 0, -1, 0 }, 5000, &h) && h.hit)
        return h.point.y;
    return -1e9f;
}

/* ---- a box proxy: Talion as a column of points from the feet up 195 cm, radius r (4 points round + the axis);
 * a step moves it as far as no point enters a box (bisection), vertical and horizontal separately ----------------- */
static float FLOOR_BIAS = 0; /* the rays see the floor this much higher than the proxy's contact (run 001108: ~1.6 cm) */
static float PR = 20; /* the proxy's radius in the tests (the capsule's round foot is 35; the column is conservative) */
static int inside(sv_vec3 feet)
{
    static const float off[5][2] = { { 0, 0 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    for (int i = 0; i < NW; i++)
        for (int k = 0; k < 5; k++) {
            float x = feet.x + off[k][0] * PR, z = feet.z + off[k][1] * PR;
            if (x > W[i].lo[0] && x < W[i].hi[0] && z > W[i].lo[2] && z < W[i].hi[2] && feet.y + 195 > W[i].lo[1] &&
                feet.y < W[i].hi[1] - 0.01f)
                return 1;
        }
    return 0;
}
static sv_vec3 move(sv_vec3 feet, sv_vec3 d)
{
    sv_vec3 parts[2] = { { d.x, 0, d.z }, { 0, d.y, 0 } };
    for (int p = 0; p < 2; p++) {
        sv_vec3 to = { feet.x + parts[p].x, feet.y + parts[p].y, feet.z + parts[p].z };
        if (!inside(to)) {
            feet = to;
            continue;
        }
        float lo = 0, hi = 1;
        for (int it = 0; it < 30; it++) {
            float m = 0.5f * (lo + hi);
            sv_vec3 q = { feet.x + parts[p].x * m, feet.y + parts[p].y * m, feet.z + parts[p].z * m };
            if (inside(q))
                hi = m;
            else
                lo = m;
        }
        feet = (sv_vec3){ feet.x + parts[p].x * lo, feet.y + parts[p].y * lo, feet.z + parts[p].z * lo };
    }
    return feet;
}

/* run a jump or a vault at hz until it ends; the feet's path and the per-tick numbers */
typedef struct {
    int ticks, end, braked;
    float apex, air_s, max_dy, max_dv, last_dy, min_clear_wall, land_x, land_z, feet_end, min_feet;
} run_out;
static run_out run(const somvr_jp_cfg *c, somvr_jp *s, sv_vec3 feet, float hz, sv_vec3 hstick, int max_ticks,
                   const float *wall /* z0 z1 top: the feet over it while crossing (or NULL) */)
{
    run_out r;
    memset(&r, 0, sizeof r);
    r.apex = -1e9f, r.min_clear_wall = 1e9f, r.min_feet = 1e9f;
    float dt = 1.0f / hz, vy_prev = s->vy, f0 = feet.y;
    for (int k = 0; k < max_ticks && s->kind != SOMVR_JPK_NONE; k++) {
        somvr_jp_in in = { .dt = dt, .owned = 1, .have_root = 1, .root = { feet.x, feet.y + c->foot_cm, feet.z } };
        in.floor_y = -1e9f;
        static const float off[5][2] = { { 0, 0 }, { 25, 0 }, { -25, 0 }, { 0, 25 }, { 0, -25 } };
        for (int i = 0; i < 5; i++) { /* the glue's five rays down */
            float fy = floor_at(feet.x + off[i][0], feet.z + off[i][1], feet.y);
            if (fy > in.floor_y)
                in.floor_y = fy;
        }
        in.floor_ok = in.floor_y > -1e8f;
        in.floor_y += FLOOR_BIAS;
        somvr_jp_out o;
        somvr_jp_step(s, c, &in, &o);
        r.ticks++;
        if (getenv("JDBG"))
            printf("    t %.3f feet %.2f,%.2f,%.2f floor %.2f dy %.2f vy %.1f brake %.0f settle %d hold %d\n", s->t, feet.x,
                   feet.y, feet.z, in.floor_y, o.dy, o.vy, s->brake_v, s->settling, s->holding);
        if (o.ended)
            break;
        sv_vec3 d = o.fixed_h ? (sv_vec3){ o.dh.x, o.dy, o.dh.z } : (sv_vec3){ hstick.x * dt, o.dy, hstick.z * dt };
        r.max_dy = fmaxf(r.max_dy, fabsf(o.dy));
        r.max_dv = fmaxf(r.max_dv, fabsf(o.vy - vy_prev));
        vy_prev = o.vy, r.last_dy = o.dy, r.braked |= o.braking;
        feet = move(feet, d);
        r.apex = fmaxf(r.apex, feet.y - f0), r.air_s = s->t, r.min_feet = fminf(r.min_feet, feet.y);
        if (wall && feet.z > wall[0] - 0.6f * c->radius_cm && feet.z < wall[1] + 0.6f * c->radius_cm)
            r.min_clear_wall = fminf(r.min_clear_wall, feet.y - wall[2]);
    }
    r.end = s->end, r.land_x = feet.x, r.land_z = feet.z, r.feet_end = feet.y;
    return r;
}

static void test_arc(void)
{
    somvr_jp_cfg c;
    somvr_jp_cfg_default(&c);
    CHECK(NEAR(somvr_jp_takeoff(60, 2000), 489.9, 0.1), "take-off for 60 cm at 2000: %.1f", somvr_jp_takeoff(60, 2000));
    CHECK(NEAR(somvr_jp_arc_dy(490, 2000, 0.1f), 39.0, 0.01), "arc piece");
    float hz[2] = { 90, 20 };
    for (int i = 0; i < 2; i++) { /* a standing jump on flat ground */
        wreset(), ground();
        somvr_jp s;
        somvr_jp_init(&s);
        float v0 = somvr_jp_takeoff(c.jump_cm, 2000);
        somvr_jp_start(&s, SOMVR_JPK_JUMP, (sv_vec3){ 0, 102, 0 }, 0, v0, 2000, (sv_vec3){ 0, 0, 0 }, (sv_vec3){ 0, 0, 0 }, 0);
        run_out r = run(&c, &s, (sv_vec3){ 0, 0, 0 }, hz[i], (sv_vec3){ 0, 0, 0 }, 400, NULL);
        printf("  jump at %.0f Hz: apex %.1f cm, %.3f s in the air, %d ticks, the largest step %.2f cm, the largest change of "
               "the vertical speed in a tick %.0f cm/s, the last step %.2f cm, end %s, the feet %.2f\n", hz[i], r.apex,
               r.air_s, r.ticks, r.max_dy, r.max_dv, r.last_dy, somvr_jp_end_name(r.end), r.feet_end);
        CHECK(r.end == SOMVR_JPE_LANDED, "%.0f Hz: landed (%s)", hz[i], somvr_jp_end_name(r.end));
        CHECK(NEAR(r.apex, c.jump_cm, i ? 6 : 1.5), "%.0f Hz: the rise %.1f ~ %.0f", hz[i], r.apex, c.jump_cm);
        CHECK(r.air_s > 0.45f && r.air_s < (i ? 0.75f : 0.6f), "%.0f Hz: the air time %.3f (the arc's 0.49 + the brake)",
              hz[i], r.air_s);
        CHECK(r.max_dy <= v0 / hz[i] + 0.5f, "%.0f Hz: no step over v0 x dt (%.2f)", hz[i], r.max_dy);
        CHECK(r.min_feet >= -0.01f && NEAR(r.feet_end, 0, 0.01), "%.0f Hz: never under the floor (%.3f), on it (%.3f)", hz[i],
              r.min_feet, r.feet_end);
        CHECK(r.braked && s.n_brakes == 1, "%.0f Hz: the soft landing braked", hz[i]);
        if (i == 0) /* the stop spread over ticks: no tick's change over a third of v0 (gravity's own is 22 cm/s) */
            CHECK(r.max_dv <= 0.3f * v0, "90 Hz: the brake's largest change %.0f cm/s <= %.0f", r.max_dv, 0.3f * v0);
    }
    { /* the proxy's contact 1.6 cm under the rays' floor: the settle step partly blocked = landed (no free fall) */
        wreset(), ground();
        somvr_jp s;
        somvr_jp_init(&s);
        FLOOR_BIAS = 1.6f;
        somvr_jp_start(&s, SOMVR_JPK_JUMP, (sv_vec3){ 0, 102, 0 }, 1.6f, somvr_jp_takeoff(60, 2000), 2000,
                       (sv_vec3){ 0, 0, 0 }, (sv_vec3){ 0, 0, 0 }, 0);
        run_out r = run(&c, &s, (sv_vec3){ 0, 0, 0 }, 20, (sv_vec3){ 0, 0, 0 }, 400, NULL);
        FLOOR_BIAS = 0;
        CHECK(r.end == SOMVR_JPE_LANDED && s.n_settle_miss == 0 && s.max_land_cms < 1, "the contact under the rays' floor: "
              "landed (%s) at the settle (misses %llu, the landing %.0f cm/s)", somvr_jp_end_name(r.end),
              (unsigned long long)s.n_settle_miss, s.max_land_cms);
    }
    { /* a ceiling 30 cm over the head */
        wreset(), ground();
        add(-500, 225, -500, 500, 260, 500);
        somvr_jp s;
        somvr_jp_init(&s);
        somvr_jp_start(&s, SOMVR_JPK_JUMP, (sv_vec3){ 0, 102, 0 }, 0, somvr_jp_takeoff(60, 2000), 2000,
                       (sv_vec3){ 0, 0, 0 }, (sv_vec3){ 0, 0, 0 }, 0);
        run_out r = run(&c, &s, (sv_vec3){ 0, 0, 0 }, 90, (sv_vec3){ 0, 0, 0 }, 400, NULL);
        CHECK(s.n_ceiling == 1 && r.end == SOMVR_JPE_LANDED && r.apex <= 30.1f && r.apex > 28,
              "ceiling: met once (%llu), the rise %.1f <= 30, landed (%s)", (unsigned long long)s.n_ceiling, r.apex,
              somvr_jp_end_name(r.end));
    }
    { /* a running jump onto a step 40 cm up (it starts 60 cm ahead), and off a 3 m ledge */
        wreset(), ground();
        add(-500, 0, 60, 500, 40, 600);
        somvr_jp s;
        somvr_jp_init(&s);
        somvr_jp_start(&s, SOMVR_JPK_JUMP, (sv_vec3){ 0, 102, 0 }, 0, somvr_jp_takeoff(60, 2000), 2000,
                       (sv_vec3){ 0, 0, 0 }, (sv_vec3){ 0, 0, 0 }, 0);
        run_out r = run(&c, &s, (sv_vec3){ 0, 0, 0 }, 90, (sv_vec3){ 0, 0, 230 }, 400, NULL);
        CHECK(r.end == SOMVR_JPE_LANDED && NEAR(r.feet_end, 40, 0.05) && r.land_z > 60,
              "onto the step: landed %s at %.2f (z %.0f)", somvr_jp_end_name(r.end), r.feet_end, r.land_z);
        wreset(), ground();
        add(-500, -350, 40, 500, -300, 3000); /* the floor 3 m down past z 40 */
        W[0].hi[2] = 40;
        somvr_jp_init(&s);
        somvr_jp_start(&s, SOMVR_JPK_JUMP, (sv_vec3){ 0, 102, 0 }, 0, somvr_jp_takeoff(60, 2000), 2000,
                       (sv_vec3){ 0, 0, 0 }, (sv_vec3){ 0, 0, 0 }, 0);
        r = run(&c, &s, (sv_vec3){ 0, 0, 0 }, 90, (sv_vec3){ 0, 0, 640 }, 400, NULL);
        CHECK(r.end == SOMVR_JPE_LANDED && NEAR(r.feet_end, -300, 0.05) && r.braked,
              "off the ledge: landed %s at %.2f, braked %d (from %.0f cm/s)", somvr_jp_end_name(r.end), r.feet_end, r.braked,
              s.v_land);
        printf("  off a 3 m ledge at 640 cm/s: %.2f s in the air, landed %.0f cm on, the brake from %.0f cm/s\n", r.air_s,
               r.land_z, s.v_land);
    }
    { /* the rays see a floor at 0, the real one is 10 cm lower (a hole under the axis the five rays miss) */
        wreset();
        add(-3000, -60, -3000, 3000, -10, 3000);
        add(-3000, 0, -3000, 3000, 1, -15), add(-3000, 0, 15, 3000, 1, 3000), add(-3000, 0, -15, -15, 1, 15),
            add(15, 0, -15, 3000, 1, 15); /* a thin lid with a 30 cm hole round the axis: the rays at 25 cm see it */
        somvr_jp s;
        somvr_jp_init(&s);
        PR = 10;
        somvr_jp_start(&s, SOMVR_JPK_JUMP, (sv_vec3){ 0, 102, 0 }, 0, somvr_jp_takeoff(60, 2000), 2000,
                       (sv_vec3){ 0, 0, 0 }, (sv_vec3){ 0, 0, 0 }, 0);
        run_out r = run(&c, &s, (sv_vec3){ 0, 1, 0 }, 90, (sv_vec3){ 0, 0, 0 }, 400, NULL);
        PR = 20;
        CHECK(r.end == SOMVR_JPE_LANDED && NEAR(r.feet_end, -10, 0.05) && s.n_settle_miss == 1,
              "a floor the rays missed: the settle went on (%llu), landed %s at %.2f", (unsigned long long)s.n_settle_miss,
              somvr_jp_end_name(r.end), r.feet_end);
    }
    { /* a jump off into a deep pit: the game's own fall past drop_max_cm; lost; caught */
        wreset();
        somvr_jp s;
        somvr_jp_init(&s);
        somvr_jp_start(&s, SOMVR_JPK_JUMP, (sv_vec3){ 0, 102, 0 }, 0, somvr_jp_takeoff(60, 2000), 2000,
                       (sv_vec3){ 0, 0, 0 }, (sv_vec3){ 0, 0, 0 }, 0);
        run_out r = run(&c, &s, (sv_vec3){ 0, 0, 0 }, 90, (sv_vec3){ 0, 0, 0 }, 400, NULL);
        CHECK(r.end == SOMVR_JPE_FALL && r.min_feet < -c.drop_max_cm + 30, "no floor: the game's fall (%s) at %.0f",
              somvr_jp_end_name(r.end), r.min_feet);
        ground();
        somvr_jp_start(&s, SOMVR_JPK_JUMP, (sv_vec3){ 0, 102, 0 }, 0, 400, 2000, (sv_vec3){ 0, 0, 0 }, (sv_vec3){ 0, 0, 0 }, 0);
        somvr_jp_in in = { .dt = 1 / 90.0f, .owned = 1, .have_root = 1, .root = { 0, 102, 0 }, .floor_ok = 1 };
        somvr_jp_out o;
        somvr_jp_step(&s, &c, &in, &o);
        CHECK(o.active && o.dy > 0, "the first tick rises %.2f", o.dy);
        in.owned = 0;
        somvr_jp_step(&s, &c, &in, &o);
        CHECK(o.ended && s.end == SOMVR_JPE_LOST && s.kind == SOMVR_JPK_NONE, "LOCO lost the tick: lost");
        somvr_jp_start(&s, SOMVR_JPK_JUMP, (sv_vec3){ 0, 102, 0 }, 0, 400, 2000, (sv_vec3){ 0, 0, 0 }, (sv_vec3){ 0, 0, 0 }, 0);
        in.owned = 1, in.climbing = 1;
        somvr_jp_step(&s, &c, &in, &o);
        CHECK(o.ended && s.end == SOMVR_JPE_CLIMB && !o.active, "a grip caught a hold: the climb's, no step");
        somvr_jp_start(&s, SOMVR_JPK_JUMP, (sv_vec3){ 0, 102, 0 }, 0, 400, 2000, (sv_vec3){ 0, 0, 0 }, (sv_vec3){ 0, 0, 0 }, 0);
        somvr_jp_stop(&s, SOMVR_JPE_STOP);
        CHECK(s.kind == SOMVR_JPK_NONE && s.end == SOMVR_JPE_STOP && s.n_stop == 1, "stopped from outside");
    }
}

static int vfind(const somvr_jp_cfg *c, float speed, somvr_jp_vplan *p)
{
    return somvr_jp_vault_find(c, ray, NULL, (sv_vec3){ 0, 102, 0 }, 0, (sv_vec3){ 0, 0, 1 }, speed, 2000, p);
}

static void test_vault(void)
{
    somvr_jp_cfg c;
    somvr_jp_cfg_default(&c);
    somvr_jp_vplan p;
    float hz[2] = { 90, 20 };
    for (int i = 0; i < 2; i++) { /* a waist-high wall (100 cm, 40 deep) 80 cm ahead, standing and at a run */
        for (int run_ = 0; run_ < 2; run_++) {
            wreset(), ground();
            add(-300, 0, 80, 300, 100, 120);
            float speed = run_ ? 230 : 0;
            int ok = vfind(&c, speed, &p);
            CHECK(ok && p.kind == SOMVR_JPO_OVER && NEAR(p.top_cm, 100, 0.5) && NEAR(p.face_cm, 80, 0.5),
                  "wall: over (%s), the top %.1f, the face %.1f", somvr_jp_vwhy_name(p.why), p.top_cm, p.face_cm);
            CHECK(p.land.z > 120 + 35 && p.land.z < 120 + 160 && NEAR(p.land.y, 0, 0.5), "wall: the landing at z %.0f y %.0f",
                  p.land.z, p.land.y);
            CHECK(p.clear_cm >= c.v_clear_cm && p.apex_cm <= c.v_rise_max_cm, "wall: clear %.1f, apex %.1f", p.clear_cm,
                  p.apex_cm);
            if (!ok)
                continue;
            if (i == 0)
                printf("  the wall (100 cm) at %s: %.0f cm/s over %.0f cm in %.2f s, the take-off %.0f cm/s, the feet's apex "
                       "%.0f cm, the least clearance %.1f cm, %d casts\n", run_ ? "a run" : "a stand", hlen_(p.vh), p.land.z,
                       p.T, p.vy0, p.apex_cm, p.clear_cm, p.rays);
            somvr_jp s;
            somvr_jp_init(&s);
            somvr_jp_start(&s, SOMVR_JPK_VAULT, (sv_vec3){ 0, 102, 0 }, 0, p.vy0, 2000, p.vh, p.land, p.t_hold);
            const float wall[3] = { 80, 120, 100 };
            PR = 20;
            run_out r = run(&c, &s, (sv_vec3){ 0, 0, 0 }, hz[i], (sv_vec3){ 0, 0, 0 }, 600, wall);
            CHECK(r.end == SOMVR_JPE_LANDED && NEAR(r.feet_end, 0, 0.05) && NEAR(r.land_z, p.land.z, i ? 25 : 6),
                  "%.0f Hz %s: over the wall onto the ring: %s at z %.1f (the ring %.1f), the feet %.2f", hz[i],
                  run_ ? "run" : "stand", somvr_jp_end_name(r.end), r.land_z, p.land.z, r.feet_end);
            CHECK(r.min_clear_wall >= 0, "%.0f Hz: the feet over the wall's top all the way (%.1f)", hz[i], r.min_clear_wall);
            float vmax = fmaxf(fabsf(p.vy0), fabsf(p.vy0 - 2000 * p.T)); /* the arc's fastest vertical speed */
            CHECK(r.max_dy <= vmax / hz[i] + 0.5f, "%.0f Hz: the vertical steps %.1f <= %.1f", hz[i], r.max_dy,
                  vmax / hz[i] + 0.5f);
        }
    }
    /* a fence: 100 cm, 5 cm deep */
    wreset(), ground();
    add(-300, 0, 100, 300, 100, 105);
    CHECK(vfind(&c, 0, &p) && p.kind == SOMVR_JPO_OVER && p.depth_cm < 15, "fence: over (%s), depth %.0f",
          somvr_jp_vwhy_name(p.why), p.depth_cm);
    /* a deep top (a platform 100 high from z 80 on): onto it, 45 in */
    wreset(), ground();
    add(-300, 0, 80, 300, 100, 600);
    CHECK(vfind(&c, 0, &p) && p.kind == SOMVR_JPO_ONTO && NEAR(p.land.y, 100, 0.5) && p.land.z > 120 && p.land.z < 200,
          "deep top: onto it (%s) at z %.0f y %.0f", somvr_jp_vwhy_name(p.why), p.land.z, p.land.y);
    if (p.why == SOMVR_JPV_OK) {
        somvr_jp s;
        somvr_jp_init(&s);
        somvr_jp_start(&s, SOMVR_JPK_VAULT, (sv_vec3){ 0, 102, 0 }, 0, p.vy0, 2000, p.vh, p.land, p.t_hold);
        run_out r = run(&c, &s, (sv_vec3){ 0, 0, 0 }, 90, (sv_vec3){ 0, 0, 0 }, 600, NULL);
        CHECK(r.end == SOMVR_JPE_LANDED && NEAR(r.feet_end, 100, 0.05) && NEAR(r.land_z, p.land.z, 4),
              "onto the top: %s at z %.1f y %.2f", somvr_jp_end_name(r.end), r.land_z, r.feet_end);
    }
    /* too high, a step, too far standing but in reach at a sprint */
    wreset(), ground();
    add(-300, 0, 80, 300, 135, 120);
    CHECK(!vfind(&c, 0, &p) && p.why == SOMVR_JPV_HIGH, "135 cm: too high (%s)", somvr_jp_vwhy_name(p.why));
    wreset(), ground();
    add(-300, 0, 80, 300, 30, 120);
    CHECK(!vfind(&c, 0, &p) && p.why == SOMVR_JPV_LOW, "30 cm: a step (%s)", somvr_jp_vwhy_name(p.why));
    wreset(), ground();
    add(-300, 0, 220, 300, 100, 260);
    CHECK(!vfind(&c, 0, &p) && p.why == SOMVR_JPV_NONE, "2.2 m ahead standing: nothing (%s)", somvr_jp_vwhy_name(p.why));
    CHECK(vfind(&c, 640, &p) && p.kind == SOMVR_JPO_OVER, "2.2 m ahead at the sprint: over (%s), %.0f cm/s, apex %.0f",
          somvr_jp_vwhy_name(p.why), hlen_(p.vh), p.apex_cm);
    /* a roof over the landing; a ledge behind the wall; a high wall right behind it; no answer */
    wreset(), ground();
    add(-300, 0, 80, 300, 100, 120);
    add(-300, 150, 140, 300, 200, 240);
    CHECK(!vfind(&c, 0, &p) && (p.why == SOMVR_JPV_NOROOM || p.why == SOMVR_JPV_NOWAY), "a roof over the landing (%s)",
          somvr_jp_vwhy_name(p.why));
    wreset();
    add(-3000, -50, -3000, 3000, 0, 120);
    add(-300, 0, 80, 300, 100, 120);
    add(-3000, -400, 120, 3000, -350, 3000);
    CHECK(!vfind(&c, 0, &p) && p.why == SOMVR_JPV_NOFLOOR, "a 3.5 m ledge behind: no landing (%s)", somvr_jp_vwhy_name(p.why));
    wreset(), ground();
    add(-300, 0, 80, 300, 100, 120);
    add(-300, 0, 120, 300, 300, 140);
    CHECK(!vfind(&c, 0, &p) && p.why == SOMVR_JPV_HIGH, "a high wall right behind: too high (%s)", somvr_jp_vwhy_name(p.why));
    wreset(), ground();
    add(-300, 0, 80, 300, 100, 120);
    DEAD = 1;
    CHECK(!vfind(&c, 0, &p) && p.why == SOMVR_JPV_NOANSWER, "no answer (%s)", somvr_jp_vwhy_name(p.why));
    DEAD = 0;
    /* rising ground (a ramp 0.5 m per 1 m from 80 cm on, steps of 10 cm): no vault (run 032104's meadow slopes) */
    wreset(), ground();
    for (int k = 0; k < 12; k++)
        add(-300, 0, 80 + 10.0f * k, 300, 5.0f * (k + 1), 3000);
    CHECK(!vfind(&c, 0, &p) && (p.why == SOMVR_JPV_SLOPE || p.why == SOMVR_JPV_LOW || p.why == SOMVR_JPV_NONE),
          "a ramp: no vault (%s, the top %.0f)", somvr_jp_vwhy_name(p.why), p.top_cm);
    CHECK(!vfind(&c, 230, &p) && p.why != SOMVR_JPV_OK, "a ramp at a run: no vault (%s)", somvr_jp_vwhy_name(p.why));
    /* an orc's body is no obstacle */
    wreset(), ground();
    add(-30, 0, 80, 30, 200, 120);
    W[1].chr = 1;
    CHECK(!vfind(&c, 0, &p) && p.why == SOMVR_JPV_NONE, "an orc ahead: no vault (%s)", somvr_jp_vwhy_name(p.why));
}

static void test_a(void)
{
    somvr_jp_cfg c;
    somvr_jp_cfg_default(&c);
    c.jump = c.vault = 1;
    somvr_jp_a_in in = { .jump = 1, .vault = 1, .can = 1, .descent = 1 };
    CHECK(somvr_jp_a_pick(&c, &in) == SOMVR_JPA_CLIMB_FIRST, "standing: the climb's edge first");
    in.speed = 230;
    CHECK(somvr_jp_a_pick(&c, &in) == SOMVR_JPA_JUMP, "running: the jump at once");
    in.ring = 1;
    CHECK(somvr_jp_a_pick(&c, &in) == SOMVR_JPA_VAULT, "the ring shown: the vault");
    in.speed = 0;
    CHECK(somvr_jp_a_pick(&c, &in) == SOMVR_JPA_VAULT, "the ring shown standing: the vault");
    in.ring = 0, in.descent = 0;
    CHECK(somvr_jp_a_pick(&c, &in) == SOMVR_JPA_JUMP, "standing, no descent: the jump");
    in.climbing = 1;
    CHECK(somvr_jp_a_pick(&c, &in) == SOMVR_JPA_CLIMB, "a climb: the climb's");
    in.climbing = 0, in.can = 0, in.ring = 1;
    CHECK(somvr_jp_a_pick(&c, &in) == SOMVR_JPA_CLIMB, "cannot (a blink, crouched ...): the climb's as before");
    in.can = 1, in.ring = 0, in.jump = 0;
    CHECK(somvr_jp_a_pick(&c, &in) == SOMVR_JPA_CLIMB, "the jump off: the climb's as before");
    c.jump = 0, in.jump = 1, in.ring = 1;
    CHECK(somvr_jp_a_pick(&c, &in) == SOMVR_JPA_VAULT, "the vault alone");
}

int main(void)
{
    test_arc();
    test_vault();
    test_a();
    printf("test_jump: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
