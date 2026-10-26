/* test_surface.c - the hands and the sword stopped at surfaces, the pure part (somvr_surface.h): the push, the planes,
 * the contact and its tick, the blade and its clank, the void, the spring, the ghost, the exceptions, the surfaces */
#include "../somvr_surface.h"

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
#define NEAR(a, b, e) (fabs((double)(a) - (double)(b)) <= (e))
#define V(x, y, z) ((sv_vec3){ (x), (y), (z) })

/* the wall: z = 0, the player on z < 0 */
static const somvr_sf_plane WALL = { { 0, 0, 0 }, { 0, 0, -1 } };
static const somvr_sf_plane SIDE = { { 0, 0, 0 }, { -1, 0, 0 } };
static const somvr_sf_plane GROUND = { { 0, 0, 0 }, { 0, 1, 0 } };

static void test_push(void) {
    float r[1] = { 3.5f }, d;
    int w;
    sv_vec3 s[1] = { V(0, 0, 2) };
    sv_vec3 T = somvr_sf_push(&WALL, 1, s, r, 1, &d, &w);
    CHECK(NEAR(T.x, 0, 1e-5) && NEAR(T.z, -5.5, 1e-4) && NEAR(d, 5.5, 1e-4) && w == 0, "one wall: %g %g %g (%g, %d)", T.x,
          T.y, T.z, d, w);
    s[0] = V(0, 0, -10);
    T = somvr_sf_push(&WALL, 1, s, r, 1, &d, &w);
    CHECK(T.x == 0 && T.y == 0 && T.z == 0 && d == 0 && w == -1, "out of it: no push");
    somvr_sf_plane two[2] = { WALL, SIDE };
    float r1[1] = { 1 };
    s[0] = V(1, 0, 1);
    T = somvr_sf_push(two, 2, s, r1, 1, &d, &w);
    CHECK(NEAR(T.x, -2, 1e-4) && NEAR(T.z, -2, 1e-4), "a corner: %g %g %g", T.x, T.y, T.z);
    /* the hand rigid: the palm out, a finger tip deeper moves it all */
    sv_vec3 h[2] = { V(0, 0, -5), V(0, 0, 3) };
    float rh[2] = { 3.5f, 1.0f };
    T = somvr_sf_push(&WALL, 1, h, rh, 2, &d, &w);
    CHECK(NEAR(T.z, -4, 1e-4), "the finger tip pushes the hand: %g", T.z);
    float r0[2] = { 0, 0 };
    T = somvr_sf_push(&WALL, 1, h, r0, 2, &d, &w);
    CHECK(T.z == 0, "radius 0: not a sample");
}

static void test_plane(void) {
    somvr_sf_plane p;
    CHECK(somvr_sf_plane_of(V(0, 0, -50), V(0, 0, 0), V(0, 0, 1), &p) && NEAR(p.n.z, -1, 1e-6),
          "the normal turned toward the ray's start: %g", p.n.z);
    CHECK(somvr_sf_plane_of(V(0, 0, -50), V(0, 0, 0), V(0, 0, 0), &p) && NEAR(p.n.z, -1, 1e-6),
          "no normal: back along the ray");
    CHECK(somvr_sf_plane_of(V(0, 100, 0), V(0, 0, 0), V(0, 2, 0.1f), &p) && NEAR(p.n.y, 1, 1e-6),
          "an out-of-range normal: back along the ray: %g", p.n.y);
    CHECK(!somvr_sf_plane_of(V(0, 0, 0), V(0, 0, 0), V(0, 0, 0), &p), "nothing to make a plane of");
    CHECK(somvr_sf_plane_of(V(0, 100, 0), V(0, 0, 0), V(0, 0.98f, 0.2f), &p) &&
              NEAR(sqrtf(p.n.x * p.n.x + p.n.y * p.n.y + p.n.z * p.n.z), 1, 1e-5) && NEAR(p.p.y, 0, 0),
          "the normal made unit");
}

static somvr_sf_in hand_at(sv_vec3 palm, int npl, const somvr_sf_plane *pl) {
    somvr_sf_in in;
    memset(&in, 0, sizeof in);
    in.free = 1;
    in.s[0] = palm, in.r[0] = 3.5f;
    in.s[1] = V(palm.x, palm.y, palm.z + 8), in.r[1] = 1.0f; /* a finger tip 8 cm ahead (+z) */
    for (int k = 0; k < npl; k++) in.pl[k] = pl[k];
    in.npl = npl;
    return in;
}

static void test_contact(void) {
    somvr_sf_cfg c;
    somvr_sf_cfg_default(&c);
    somvr_sf_hand h;
    somvr_sf_hand_reset(&h);
    somvr_sf_out o;
    double t = 10;
    const float dt = 1.0f / 90;
    /* the hand comes toward the wall at 1 m/s: no contact while the finger tip is short of it */
    somvr_sf_in in = hand_at(V(0, 0, -12), 1, &WALL);
    somvr_sf_step(&h, &c, &in, t, 0, &o);
    CHECK(o.ev == 0 && o.off.z == 0 && !h.contact, "short of the wall: 0x%x %g", o.ev, o.off.z);
    in = hand_at(V(0, 0, -8.0f), 1, &WALL);
    t += dt;
    somvr_sf_step(&h, &c, &in, t, dt, &o); /* the tip 0 cm: inside its radius by 1 */
    CHECK((o.ev & SOMVR_SF_EV_CONTACT) && NEAR(o.off.z, -1, 1e-4) && o.tick > 0.15f && o.speed > 300,
          "the contact: 0x%x off %g tick %g speed %g", o.ev, o.off.z, o.tick, o.speed);
    /* pushed on, 6 cm deeper over the next frames: the shown hand stays at the wall, the ghost comes in */
    int on = 0;
    for (int k = 1; k <= 12; k++) {
        in = hand_at(V(0, 0, -8.0f + 0.5f * (float)k), 1, &WALL);
        t += dt;
        somvr_sf_step(&h, &c, &in, t, dt, &o);
        on |= (o.ev & SOMVR_SF_EV_GHOST_ON) != 0;
        CHECK(NEAR(in.s[1].z + o.off.z, -1, 1e-4) && o.tick == 0 && !(o.ev & SOMVR_SF_EV_CONTACT),
              "frame %d: the tip held at -1: %g", k, in.s[1].z + o.off.z);
    }
    CHECK(NEAR(o.off.z, -7, 1e-4) && on && o.alpha > 0 && o.alpha < c.ghost_a, "pushed 7: off %g, ghost %g", o.off.z,
          o.alpha);
    for (int k = 0; k < 30; k++) {
        t += dt;
        somvr_sf_step(&h, &c, &in, t, dt, &o);
    }
    /* sep 7 cm: (7 - 4) / 8 = 0.375, smoothstep 0.3164 */
    CHECK(NEAR(o.alpha, c.ghost_a * 0.31640625f, 1e-4), "the ghost at 7 cm: %g", o.alpha);
    /* back out 1:1 (0.5 cm a frame: no ease), the release, the ghost fades */
    int rel = 0, ease = 0, off = 0;
    for (int k = 1; k <= 30; k++) {
        in = hand_at(V(0, 0, -2.0f - 0.5f * (float)k), 1, &WALL);
        t += dt;
        somvr_sf_step(&h, &c, &in, t, dt, &o);
        rel |= (o.ev & SOMVR_SF_EV_RELEASE) != 0, ease |= (o.ev & SOMVR_SF_EV_EASE) != 0;
        off |= (o.ev & SOMVR_SF_EV_GHOST_OFF) != 0;
        if (o.ev & SOMVR_SF_EV_RELEASE)
            CHECK(o.held_s > 0.4f && NEAR(o.depth_max, 7, 1e-3), "the release: %g s, deepest %g", o.held_s, o.depth_max);
    }
    CHECK(rel && !ease && off && o.off.z == 0 && o.alpha == 0 && !h.contact, "out: release %d ease %d ghost off %d", rel,
          ease, off);
    /* in again at once: no second tick within tick_gap_s? (the gap is long past: a tick) */
    in = hand_at(V(0, 0, -6), 1, &WALL);
    t += dt;
    somvr_sf_step(&h, &c, &in, t, dt, &o);
    CHECK((o.ev & SOMVR_SF_EV_CONTACT) && o.tick > 0, "a new contact ticks");
    in = hand_at(V(0, 0, -20), 1, &WALL);
    t += dt;
    somvr_sf_step(&h, &c, &in, t, dt, &o); /* out by 11 cm in one frame: the jump eases */
    CHECK((o.ev & SOMVR_SF_EV_RELEASE) && (o.ev & SOMVR_SF_EV_EASE) && NEAR(o.off.z, -3 * expf(-dt / c.spring_s), 1e-4),
          "a jump of 3 cm eases: 0x%x %g", o.ev, o.off.z);
    in = hand_at(V(0, 0, -8), 1, &WALL);
    t += dt;
    somvr_sf_step(&h, &c, &in, t, dt, &o);
    CHECK((o.ev & SOMVR_SF_EV_CONTACT) && o.tick == 0, "within the tick gap: no tick (%g)", o.tick);
}

static void test_spring(void) {
    somvr_sf_cfg c;
    somvr_sf_cfg_default(&c);
    somvr_sf_hand h;
    somvr_sf_hand_reset(&h);
    somvr_sf_out o;
    double t = 0;
    const float dt = 1.0f / 90;
    somvr_sf_in in = hand_at(V(0, 0, 10), 1, &WALL); /* 10 cm into the wall: the tip 18 in, off -19 */
    somvr_sf_step(&h, &c, &in, t, dt, &o);
    CHECK(NEAR(o.off.z, -19, 1e-3), "deep: %g", o.off.z);
    /* the surface lost (the ray found nothing): the hand eases to the controller, never in one jump */
    in = hand_at(V(0, 0, 10), 0, NULL);
    float last = o.off.z;
    int n = 0;
    while (o.off.z != 0 && n < 100) {
        t += dt;
        somvr_sf_step(&h, &c, &in, t, dt, &o);
        CHECK(o.off.z >= last - 1e-5f && (o.off.z == 0 || (o.ev & SOMVR_SF_EV_EASE)), "frame %d: %g", n, o.off.z);
        last = o.off.z, n++;
    }
    CHECK(n >= 5 && n <= 30, "eased back in %d frames", n);
    /* not free (a climb hold, the bow, the menu): nothing clamps, the offset eases back the same way */
    somvr_sf_hand_reset(&h);
    in = hand_at(V(0, 0, 10), 1, &WALL);
    somvr_sf_step(&h, &c, &in, t, dt, &o);
    in.free = 0;
    t += dt;
    somvr_sf_step(&h, &c, &in, t, dt, &o);
    CHECK((o.ev & SOMVR_SF_EV_RELEASE) && (o.ev & SOMVR_SF_EV_EASE) && o.off.z < 0 && o.off.z > -19 && o.depth == 0,
          "an exception: 0x%x %g", o.ev, o.off.z);
}

static void test_blade(void) {
    somvr_sf_cfg c;
    somvr_sf_cfg_default(&c);
    somvr_sf_hand h;
    somvr_sf_hand_reset(&h);
    somvr_sf_out o;
    double t = 5;
    const float dt = 1.0f / 90;
    /* the sword level, pointing at the wall from 120 cm: nothing; then a thrust at 3 m/s: the tip stops at the wall */
    somvr_sf_in in = hand_at(V(0, 0, -120), 0, NULL);
    in.blade = 1, in.base = V(0, 0, -128), in.tip = V(0, 0, -20), in.bpl[0] = WALL, in.nbpl = 1;
    somvr_sf_step(&h, &c, &in, t, 0, &o);
    CHECK(o.ev == 0 && o.off.z == 0, "short: 0x%x", o.ev);
    in.s[0] = V(0, 0, -96), in.s[1] = V(0, 0, -88), in.base = V(0, 0, -104), in.tip = V(0, 0, 4);
    t += dt;
    somvr_sf_step(&h, &c, &in, t, dt, &o);
    CHECK((o.ev & SOMVR_SF_EV_BLADE) && NEAR(o.off.z, -5, 1e-4) && o.clank > 0.9f && o.fx && o.bspeed > 2000,
          "the thrust: 0x%x off %g clank %g fx %d speed %g", o.ev, o.off.z, o.clank, o.fx, o.bspeed);
    /* on: the hand stays back with it, 20 cm in: still held (max 25) */
    in.s[0] = V(0, 0, -81), in.s[1] = V(0, 0, -73), in.base = V(0, 0, -89), in.tip = V(0, 0, 19);
    t += dt;
    somvr_sf_step(&h, &c, &in, t, dt, &o);
    CHECK(NEAR(o.off.z, -20, 1e-4) && h.blade && !(o.ev & SOMVR_SF_EV_BLADE) && o.clank == 0, "held: %g", o.off.z);
    /* 30 cm in: the void (the blade goes in), the hand eases back onto the controller */
    in.s[0] = V(0, 0, -71), in.s[1] = V(0, 0, -63), in.base = V(0, 0, -79), in.tip = V(0, 0, 29);
    t += dt;
    somvr_sf_step(&h, &c, &in, t, dt, &o);
    CHECK((o.ev & SOMVR_SF_EV_VOID) && (o.ev & SOMVR_SF_EV_BLADE_OFF) && (o.ev & SOMVR_SF_EV_EASE) && h.vd &&
              o.off.z < 0 && o.off.z > -20 && NEAR(o.bdepth, 30, 1e-3),
          "the void: 0x%x off %g need %g", o.ev, o.off.z, o.bdepth);
    /* a slow touch: a tick, no clank */
    somvr_sf_hand_reset(&h);
    in.s[0] = V(0, 0, -101), in.s[1] = V(0, 0, -93), in.base = V(0, 0, -109), in.tip = V(0, 0, -1.5f);
    somvr_sf_step(&h, &c, &in, t, dt, &o);
    in.s[0] = V(0, 0, -100.5f), in.s[1] = V(0, 0, -92.5f), in.base = V(0, 0, -108.5f), in.tip = V(0, 0, -0.5f);
    t += 1;
    somvr_sf_step(&h, &c, &in, t, 0.1f, &o);
    CHECK((o.ev & SOMVR_SF_EV_BLADE) && o.clank == 0 && o.tick > 0 && o.bspeed < 60, "slow: clank %g tick %g speed %g",
          o.clank, o.tick, o.bspeed);
    /* the sword pointing down into the ground: the hand lifted by the tip's depth; a palm on the ground meanwhile */
    somvr_sf_hand_reset(&h);
    in = hand_at(V(0, 90, 0), 1, &GROUND);
    in.blade = 1, in.base = V(0, 98, 0), in.tip = V(0, -10, 0), in.bpl[0] = GROUND, in.nbpl = 1;
    somvr_sf_step(&h, &c, &in, t, 0, &o);
    CHECK(NEAR(o.off.y, 11, 1e-4) && o.off.x == 0 && o.off.z == 0, "the ground lifts the sword: %g", o.off.y);
    c.blade_max_cm = 0;
    somvr_sf_hand_reset(&h);
    somvr_sf_step(&h, &c, &in, t, 0, &o);
    CHECK(o.off.y == 0 && !h.blade, "_BLADE_MAX_CM=0: the blade never stops");
}

static void test_cfg(void) {
    somvr_sf_cfg c;
    somvr_sf_cfg_default(&c);
    CHECK(somvr_sf_parse_ghost("0.5", &c) && c.ghost_a == 0.5f && !somvr_sf_parse_ghost("2", &c) &&
              !somvr_sf_parse_ghost("x", &c) && !somvr_sf_parse_ghost("", &c) && c.ghost_a == 0.5f,
          "the ghost's switch");
    CHECK(somvr_sf_parse_blade_max("0", &c) && c.blade_max_cm == 0 && somvr_sf_parse_blade_max("40", &c) &&
              !somvr_sf_parse_blade_max("-1", &c) && c.blade_max_cm == 40,
          "the blade's switch");
    c.ghost_a = 0;
    somvr_sf_hand h;
    somvr_sf_hand_reset(&h);
    somvr_sf_out o;
    somvr_sf_in in = hand_at(V(0, 0, 10), 1, &WALL);
    somvr_sf_step(&h, &c, &in, 0, 0.1f, &o);
    CHECK(o.alpha == 0 && !(o.ev & SOMVR_SF_EV_GHOST_ON) && o.off.z < -10, "no ghost at 0");
}

static void test_solid(void) {
    CHECK(somvr_sf_solid("stone") && somvr_sf_solid("Grass") && somvr_sf_solid("gravel") && somvr_sf_solid("Mud") &&
              somvr_sf_solid("wood"),
          "solid");
    CHECK(!somvr_sf_solid("") && !somvr_sf_solid(NULL), "no record (an orc's capsule): passed");
    CHECK(!somvr_sf_solid("Flesh_Uruk") && !somvr_sf_solid("ArmorMetal") && !somvr_sf_solid("Foliage") &&
              !somvr_sf_solid("Water_Shallow") && !somvr_sf_solid("Sky") && !somvr_sf_solid("Invisible") &&
              !somvr_sf_solid("PlayerBlocker") && !somvr_sf_solid("MudPuddle") && !somvr_sf_solid("cloth"),
          "not solid");
}

int main(void) {
    test_push();
    test_plane();
    test_contact();
    test_spring();
    test_blade();
    test_cfg();
    test_solid();
    printf("test_surface: %d/%d passed\n", g_n - g_fail, g_n);
    return g_fail ? 1 : 0;
}
