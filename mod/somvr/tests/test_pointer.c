/* test_pointer.c - the laser pointer's pure part (somvr_pointer.c): the ray of a pose, the ray against a quad (centre,
 * corners = the image's orientation, beside it, behind it, from the back, parallel, too far, a turned quad's round
 * trip), the quad's points, the drawn ray's quad (along the line, facing the eye), the dot's size, a snapshot's rays
 * (aim, the grip fallback, inactive hands), the state machine (on/off hysteresis, the One Euro filter at rest, under
 * jitter and moving, clicks, a drag beside the quad, a drag away from it, a tracking gap, the hand switch, a target
 * change, a press while off, the consume bits, the grip flag, a NaN trigger), the UI pixel mapping, the trigger's
 * owner per press, SOMVR_XR_POINTER and the pointer image. */
#include "../somvr_pointer.h"

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

static const somvr_posef ID = {{0, 0, 0, 1}, {0, 0, 0}};
static const somvr_posef MENU = {{0, 0, 0, 1}, {0, 0, -2}}; /* the layer's mono quad: 2 m ahead, 2 m wide */
static const float MSIZE[2] = {2.0f, 1.125f};               /* a 16:9 image */

static somvr_posef yaw_pose(float deg, float x, float y, float z) {
    float a = 0.5f * deg * 0.01745329252f;
    return (somvr_posef){{0, sinf(a), 0, cosf(a)}, {x, y, z}};
}
/* a pose at o whose -Z points at the target p (yaw then pitch, no roll) */
static somvr_posef aim_at(const float o[3], const float p[3]) {
    float d[3] = {p[0] - o[0], p[1] - o[1], p[2] - o[2]};
    float yaw = atan2f(-d[0], -d[2]), pitch = atan2f(d[1], sqrtf(d[0] * d[0] + d[2] * d[2]));
    float qy[4] = {0, sinf(0.5f * yaw), 0, cosf(0.5f * yaw)}, qx[4] = {sinf(0.5f * pitch), 0, 0, cosf(0.5f * pitch)};
    somvr_posef r = {{qy[3] * qx[0], qy[1] * qx[3], -qy[1] * qx[0], qy[3] * qx[3]}, {o[0], o[1], o[2]}}; /* qy (x) qx */
    return r;
}
static void point_of(const float uv[2], float out[3]) { somvr_ptr_quad_point(&MENU, MSIZE, uv, 0, out); }

static void test_geometry(void) {
    float o[3], d[3], uv[2], t;
    somvr_ptr_ray(&ID, o, d);
    CHECK(NEAR(d[0], 0, 1e-6) && NEAR(d[1], 0, 1e-6) && NEAR(d[2], -1, 1e-6) && o[2] == 0, "identity ray %g %g %g", d[0],
          d[1], d[2]);
    somvr_posef y90 = yaw_pose(90, 1, 2, 3);
    somvr_ptr_ray(&y90, o, d);
    CHECK(NEAR(d[0], -1, 1e-6) && NEAR(d[2], 0, 1e-6) && o[0] == 1 && o[1] == 2 && o[2] == 3, "yaw 90 ray %g %g", d[0],
          d[2]);
    /* the menu quad: centre, the four corners (u right, v down from the top-left) */
    float c[3] = {0, 0, 0}, f[3] = {0, 0, -1};
    CHECK(somvr_ptr_hit_quad(c, f, &MENU, MSIZE, 20, uv, &t) && NEAR(uv[0], 0.5, 1e-6) && NEAR(uv[1], 0.5, 1e-6) &&
              NEAR(t, 2, 1e-6),
          "centre %g %g t %g", uv[0], uv[1], t);
    static const float corners[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    for (int k = 0; k < 4; k++) {
        float p[3];
        point_of(corners[k], p);
        CHECK(NEAR(p[0], corners[k][0] ? 1 : -1, 1e-6) && NEAR(p[1], corners[k][1] ? -0.5625 : 0.5625, 1e-6) &&
                  p[2] == -2,
              "corner %d at %g %g %g", k, p[0], p[1], p[2]);
        float dd[3] = {p[0], p[1], p[2]}, n = sqrtf(dd[0] * dd[0] + dd[1] * dd[1] + dd[2] * dd[2]);
        for (int i = 0; i < 3; i++) dd[i] /= n;
        CHECK(somvr_ptr_hit_quad(c, dd, &MENU, MSIZE, 20, uv, &t) && NEAR(uv[0], corners[k][0], 1e-5) &&
                  NEAR(uv[1], corners[k][1], 1e-5) && NEAR(t, n, 1e-5),
              "corner %d: uv %g %g", k, uv[0], uv[1]);
    }
    float side[3] = {0.8f, 0, -0.6f}; /* beside the quad: a hit of the plane with uv outside */
    CHECK(somvr_ptr_hit_quad(c, side, &MENU, MSIZE, 20, uv, &t) && uv[0] > 1.5f && NEAR(uv[1], 0.5, 1e-6),
          "beside: %g %g", uv[0], uv[1]);
    float back[3] = {0, 0, 1}, along[3] = {1, 0, 0};
    CHECK(!somvr_ptr_hit_quad(c, back, &MENU, MSIZE, 20, uv, &t), "pointing away hits");
    CHECK(!somvr_ptr_hit_quad(c, along, &MENU, MSIZE, 20, uv, &t), "parallel hits");
    float behind[3] = {0, 0, -3};
    CHECK(!somvr_ptr_hit_quad(behind, f, &MENU, MSIZE, 20, uv, &t), "a ray from behind the quad hits");
    CHECK(!somvr_ptr_hit_quad(behind, back, &MENU, MSIZE, 20, uv, &t), "the quad's back side is hit");
    CHECK(!somvr_ptr_hit_quad(c, f, &MENU, MSIZE, 1.5f, uv, &t), "a hit beyond max_t");
    float zero[2] = {0, 1};
    CHECK(!somvr_ptr_hit_quad(c, f, &MENU, zero, 20, uv, &t), "a zero-width quad");
    /* a turned, raised quad (a world-locked HUD at yaw 30): uv -> point -> a ray from the head -> the same uv */
    somvr_posef hud = yaw_pose(30, -0.75f, 1.5f, -1.3f);
    float hs[2] = {1.2f, 0.675f}, head[3] = {0.1f, 1.6f, 0.05f};
    float lifted[3], flat[3], m[2] = {0.5f, 0.5f};
    somvr_ptr_quad_point(&hud, hs, m, 0.01f, lifted);
    somvr_ptr_quad_point(&hud, hs, m, 0, flat);
    CHECK(NEAR(lifted[0] - flat[0], 0.01 * sin(30 * 0.01745329252), 1e-6) &&
              NEAR(lifted[2] - flat[2], 0.01 * cos(30 * 0.01745329252), 1e-6),
          "lift is along the quad's +Z");
    int ok = 1;
    for (int k = 0; k < 25; k++) {
        float want[2] = {0.05f + 0.9f * (float)(k % 5) / 4, 0.05f + 0.9f * (float)(k / 5) / 4}, p[3];
        somvr_ptr_quad_point(&hud, hs, want, 0, p);
        float dd[3] = {p[0] - head[0], p[1] - head[1], p[2] - head[2]}, n = sqrtf(dd[0] * dd[0] + dd[1] * dd[1] + dd[2] * dd[2]);
        for (int i = 0; i < 3; i++) dd[i] /= n;
        ok &= somvr_ptr_hit_quad(head, dd, &hud, hs, 20, uv, &t) && NEAR(uv[0], want[0], 1e-4) && NEAR(uv[1], want[1], 1e-4);
    }
    CHECK(ok, "the turned quad's round trip");
    /* aim_at (the tests' helper) agrees with somvr_ptr_ray */
    float tgt[3] = {0.4f, -0.3f, -2};
    somvr_posef ap = aim_at(head, tgt);
    somvr_ptr_ray(&ap, o, d);
    float e[3] = {tgt[0] - head[0], tgt[1] - head[1], tgt[2] - head[2]}, en = sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
    CHECK(NEAR(d[0], e[0] / en, 1e-5) && NEAR(d[1], e[1] / en, 1e-5) && NEAR(d[2], e[2] / en, 1e-5), "aim_at");

    /* the drawn ray: X along b - a, its face toward the eye, unit orientation */
    float a[3] = {0.2f, -0.3f, -0.4f}, b[3] = {0.1f, 0.05f, -2}, eye[3] = {0, 0, 0};
    somvr_posef rp;
    float rs[2];
    CHECK(somvr_ptr_ray_quad(a, b, eye, 0.002f, &rp, rs), "ray quad");
    float ex[3], ez[3];
    float ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, L = sqrtf(ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2]);
    somvr_posef rq = {{rp.q[0], rp.q[1], rp.q[2], rp.q[3]}, {0, 0, 0}};
    somvr_ptr_quad_point(&rq, (float[2]){2, 2}, (float[2]){1, 0.5f}, 0, ex); /* local (1, 0, 0) */
    somvr_ptr_quad_point(&rq, (float[2]){2, 2}, (float[2]){0.5f, 0.5f}, 1, ez); /* local (0, 0, 1) */
    float qn = rp.q[0] * rp.q[0] + rp.q[1] * rp.q[1] + rp.q[2] * rp.q[2] + rp.q[3] * rp.q[3];
    float te[3] = {eye[0] - rp.p[0], eye[1] - rp.p[1], eye[2] - rp.p[2]};
    CHECK(NEAR(qn, 1, 1e-5) && NEAR(rs[0], L, 1e-6) && rs[1] == 0.002f && NEAR(rp.p[0], 0.15, 1e-6) &&
              NEAR(rp.p[2], -1.2, 1e-6),
          "ray quad pose/size");
    CHECK(NEAR(ex[0], ab[0] / L, 1e-5) && NEAR(ex[1], ab[1] / L, 1e-5) && NEAR(ex[2], ab[2] / L, 1e-5), "ray quad X");
    CHECK(NEAR(ez[0] * ab[0] + ez[1] * ab[1] + ez[2] * ab[2], 0, 1e-5) && ez[0] * te[0] + ez[1] * te[1] + ez[2] * te[2] > 0,
          "ray quad faces the eye");
    CHECK(somvr_ptr_ray_quad(a, b, NULL, 0.002f, &rp, rs), "no eye");
    rq = (somvr_posef){{rp.q[0], rp.q[1], rp.q[2], rp.q[3]}, {0, 0, 0}};
    somvr_ptr_quad_point(&rq, (float[2]){2, 2}, (float[2]){0.5f, 0.5f}, 1, ez);
    CHECK(ez[1] > 0.9f * sqrtf(1 - (ab[1] / L) * (ab[1] / L)), "no eye: faces up (%g)", ez[1]);
    float onl[3] = {0.3f, -0.65f, 1.2f}; /* the eye on the line through a and b */
    onl[0] = a[0] - ab[0], onl[1] = a[1] - ab[1], onl[2] = a[2] - ab[2];
    CHECK(somvr_ptr_ray_quad(a, b, onl, 0.002f, &rp, rs) && isfinite(rp.q[0]) && isfinite(rp.q[3]), "eye on the line");
    float vert_a[3] = {0, 0, 0}, vert_b[3] = {0, 1, 0};
    CHECK(somvr_ptr_ray_quad(vert_a, vert_b, NULL, 0.002f, &rp, rs) && isfinite(rp.q[0]) &&
              NEAR(rp.q[0] * rp.q[0] + rp.q[1] * rp.q[1] + rp.q[2] * rp.q[2] + rp.q[3] * rp.q[3], 1, 1e-5),
          "a vertical ray without an eye");
    CHECK(!somvr_ptr_ray_quad(a, a, eye, 0.002f, &rp, rs), "a zero-length ray is drawn");
    CHECK(NEAR(somvr_ptr_dot_size(2, 0.5f), 4 * tan(0.25 * 0.01745329252), 1e-6) && somvr_ptr_dot_size(0.1f, 0.5f) == 0.004f &&
              somvr_ptr_dot_size(NAN, 0.5f) == 0.004f,
          "dot size");
}

static void test_in_from(void) {
    somvr_input_state st;
    memset(&st, 0, sizeof st);
    somvr_hand_in *R = &st.hand[1], *L = &st.hand[0];
    R->aim = yaw_pose(10, 0.2f, -0.3f, -0.4f), R->grip = yaw_pose(-5, 0.21f, -0.31f, -0.41f);
    R->aim_flags = R->grip_flags = 0xf, R->active = SOMVR_HACT_POSE | SOMVR_HACT_INPUT, R->trigger = 0.7f;
    L->aim = yaw_pose(0, -0.2f, -0.3f, -0.4f), L->grip = yaw_pose(20, -0.2f, -0.3f, -0.4f);
    L->aim_flags = 0, L->grip_flags = 3, L->active = SOMVR_HACT_POSE, L->trigger = 0.9f; /* input inactive */
    somvr_ptr_in in;
    somvr_ptr_in_from(&st, &in);
    CHECK(in.ok[1] && !in.grip[1] && !memcmp(&in.pose[1], &R->aim, sizeof R->aim) && in.trigger[1] == 0.7f, "aim");
    CHECK(in.ok[0] && in.grip[0] && !memcmp(&in.pose[0], &L->grip, sizeof L->grip) && in.trigger[0] == 0,
          "grip fallback / trigger with the input inactive");
    L->grip_flags = 1; /* orientation only */
    R->active = SOMVR_HACT_INPUT;
    somvr_ptr_in_from(&st, &in);
    CHECK(!in.ok[0] && !in.ok[1] && in.trigger[1] == 0.7f, "no pose: no ray (L %d R %d)", in.ok[0], in.ok[1]);
    R->active = SOMVR_HACT_POSE, R->aim.q[0] = NAN;
    somvr_ptr_in_from(&st, &in);
    CHECK(in.ok[1] && in.grip[1], "a NaN aim falls back to the grip");
}

/* ---- the state machine -------------------------------------------------------------------------------------------- */
static somvr_ptr_state S;
static somvr_ptr_cfg C;
static somvr_ptr_in I;
static somvr_ptr_out O;
static int64_t T;
static const float HEAD_R[3] = {0.2f, -0.25f, -0.3f}, HEAD_L[3] = {-0.2f, -0.25f, -0.3f};

static void aim_uv(int h, float u, float v) {
    float p[3], uv[2] = {u, v};
    point_of(uv, p);
    I.pose[h] = aim_at(h ? HEAD_R : HEAD_L, p), I.ok[h] = 1, I.grip[h] = 0;
}
static void step(void) {
    T += 11111111; /* 90 Hz */
    I.time = T;
    somvr_ptr_update(&S, &C, &I, &O);
}
static void setup(void) {
    somvr_ptr_cfg_default(&C);
    somvr_ptr_reset(&S);
    memset(&I, 0, sizeof I);
    I.quad = SOMVR_PTRQ_MENU, I.quad_pose = MENU, I.quad_size[0] = MSIZE[0], I.quad_size[1] = MSIZE[1];
    T = 1000000000;
}

static void test_state(void) {
    setup();
    CHECK(S.hand == SOMVR_HAND_R, "the right hand points at the start");
    step();
    CHECK(O.flags == 0 && O.consume == 0 && O.edges == 0, "no rays: flags %#x", O.flags);
    aim_uv(1, 0.5f, 0.5f);
    step();
    CHECK((O.flags & (SOMVR_PTRF_ON | SOMVR_PTRF_AIM)) == (SOMVR_PTRF_ON | SOMVR_PTRF_AIM) && (O.edges & SOMVR_PTRE_ENTER) &&
              O.hand == 1 && NEAR(O.uv[0], 0.5, 1e-4) && NEAR(O.uv[1], 0.5, 1e-4) && O.consume == 2u,
          "on: flags %#x edges %#x uv %g %g consume %#x", O.flags, O.edges, O.uv[0], O.uv[1], O.consume);
    float hit[3];
    point_of((float[2]){0.5f, 0.5f}, hit);
    CHECK(NEAR(O.hit[2], -2, 1e-5) && NEAR(O.hit[0], 0, 1e-4) &&
              NEAR(O.dist, sqrtf(0.04f + 0.0625f + 2.89f), 1e-3) && NEAR(O.origin[0], 0.2, 1e-6),
          "hit %g %g %g dist %g", O.hit[0], O.hit[1], O.hit[2], O.dist);
    /* hysteresis */
    for (int k = 0; k < 60; k++) aim_uv(1, 0.99f, 0.5f), step();
    CHECK(O.flags & SOMVR_PTRF_ON, "0.99 is on");
    aim_uv(1, 1.015f, 0.5f), step();
    CHECK((O.flags & SOMVR_PTRF_ON) && O.uv[0] <= 1.0f, "within the leave margin: on, clamped (%g)", O.uv[0]);
    aim_uv(1, 1.05f, 0.5f), step();
    CHECK(!(O.flags & SOMVR_PTRF_ON) && (O.edges & SOMVR_PTRE_LEAVE) && O.consume == 0, "1.05: off (edges %#x consume %#x)",
          O.edges, O.consume);
    aim_uv(1, 1.01f, 0.5f), step();
    CHECK(!(O.flags & SOMVR_PTRF_ON) && O.consume == 2u, "1.01 from off: stays off (enter is inside [0,1]); consumed");
    aim_uv(1, 0.98f, 0.5f), step();
    CHECK((O.flags & SOMVR_PTRF_ON) && (O.edges & SOMVR_PTRE_ENTER) && NEAR(O.uv[0], 0.98, 1e-4),
          "back inside: on at the raw point (%g)", O.uv[0]);

    /* the filter: at rest a step moves gradually and settles; jitter is damped; a fast sweep lags little */
    for (int k = 0; k < 90; k++) aim_uv(1, 0.5f, 0.5f), step();
    aim_uv(1, 0.52f, 0.5f), step();
    float first = O.uv[0];
    for (int k = 0; k < 90; k++) aim_uv(1, 0.52f, 0.5f), step();
    CHECK(first > 0.5f && first < 0.515f && NEAR(O.uv[0], 0.52, 0.001), "a 0.02 step: first %g, after 1 s %g", first,
          O.uv[0]);
    float worst = 0;
    for (int k = 0; k < 180; k++) {
        aim_uv(1, 0.52f + (k & 1 ? 0.002f : -0.002f), 0.5f + (k & 2 ? 0.002f : -0.002f));
        step();
        if (k > 30) worst = fmaxf(worst, fmaxf(fabsf(O.uv[0] - 0.52f), fabsf(O.uv[1] - 0.5f)));
    }
    CHECK(worst < 0.001f, "jitter +-0.002 -> +-%g", worst);
    float lag = 0;
    for (int k = 0; k <= 30; k++) { /* 0.2 -> 0.8 in 1/3 s: 1.8 uv/s */
        float u = 0.2f + 0.02f * (float)k;
        aim_uv(1, u, 0.5f), step();
        if (k > 10) lag = fmaxf(lag, u - O.uv[0]);
    }
    CHECK(lag < 0.03f && lag > 0, "a 1.8 uv/s sweep lags %g", lag);
    for (int k = 0; k < 90; k++) aim_uv(1, 0.5f, 0.5f), step();

    /* a click */
    I.trigger[1] = 0.7f, step();
    CHECK((O.edges & SOMVR_PTRE_PRESS) && (O.flags & SOMVR_PTRF_DOWN) && S.downs == 1 && S.ups == 0, "press: edges %#x",
          O.edges);
    I.trigger[1] = 0.5f, step();
    CHECK((O.flags & SOMVR_PTRF_DOWN) && !O.edges, "between the thresholds: held");
    I.trigger[1] = 0.3f, step();
    CHECK((O.edges & SOMVR_PTRE_RELEASE) && !(O.edges & SOMVR_PTRE_CANCEL) && !(O.flags & SOMVR_PTRF_DOWN) && S.ups == 1,
          "release: edges %#x", O.edges);
    /* a drag beside the quad: on with uv clamped, then released there: off the next frame */
    I.trigger[1] = 0.8f, step();
    for (int k = 0; k < 20; k++) aim_uv(1, 0.5f + 0.05f * (float)k, 0.5f), step();
    CHECK((O.flags & (SOMVR_PTRF_ON | SOMVR_PTRF_DOWN)) == (SOMVR_PTRF_ON | SOMVR_PTRF_DOWN) && O.uv[0] == 1.0f &&
              S.downs == 2,
          "drag to u 1.45: flags %#x uv %g", O.flags, O.uv[0]);
    I.trigger[1] = 0, step();
    CHECK((O.edges & SOMVR_PTRE_RELEASE) && S.ups == 2, "released beside");
    step();
    CHECK(!(O.flags & SOMVR_PTRF_ON) && (O.edges & SOMVR_PTRE_LEAVE), "then off");
    /* a drag away from the plane: cancelled */
    aim_uv(1, 0.5f, 0.5f), step();
    I.trigger[1] = 0.8f, step();
    float back[3] = {HEAD_R[0], HEAD_R[1], 3};
    I.pose[1] = aim_at(HEAD_R, back), step();
    CHECK((O.edges & (SOMVR_PTRE_RELEASE | SOMVR_PTRE_CANCEL)) == (SOMVR_PTRE_RELEASE | SOMVR_PTRE_CANCEL) &&
              !(O.flags & SOMVR_PTRF_ON) && S.ups == S.downs,
          "dragged away: edges %#x", O.edges);
    I.trigger[1] = 0, step();
    CHECK(!O.edges, "no second release");
    /* a tracking gap while held: kept for lose_frames, then cancelled */
    aim_uv(1, 0.3f, 0.6f), step();
    I.trigger[1] = 0.9f, step();
    I.ok[1] = 0;
    for (int k = 0; k < C.lose_frames; k++) step();
    CHECK((O.flags & (SOMVR_PTRF_ON | SOMVR_PTRF_DOWN)) == (SOMVR_PTRF_ON | SOMVR_PTRF_DOWN) && !(O.flags & SOMVR_PTRF_AIM) &&
              NEAR(O.uv[0], 0.3, 1e-3),
          "a gap of %d frames: flags %#x", C.lose_frames, O.flags);
    step();
    CHECK((O.edges & SOMVR_PTRE_CANCEL) && !(O.flags & SOMVR_PTRF_ON), "a longer gap: cancelled (edges %#x)", O.edges);
    I.trigger[1] = 0;
    aim_uv(1, 0.5f, 0.5f), step();
    CHECK(O.flags & SOMVR_PTRF_ON, "back on");

    /* the hand switch: the left trigger while the right points (and holds a click) */
    I.trigger[1] = 0.9f, step();
    uint32_t downs = S.downs;
    aim_uv(0, 0.25f, 0.25f);
    I.trigger[0] = 0.9f, step();
    CHECK(O.hand == 0 && (O.edges & SOMVR_PTRE_SWITCH) && (O.edges & SOMVR_PTRE_CANCEL) && (O.edges & SOMVR_PTRE_PRESS) &&
              S.downs == downs + 1 && S.ups == downs && NEAR(O.uv[0], 0.25, 1e-4) && (O.consume & 1u),
          "switch to L: hand %d edges %#x uv %g", O.hand, O.edges, O.uv[0]);
    I.trigger[1] = 0, step();
    CHECK(O.hand == 0 && (O.flags & SOMVR_PTRF_DOWN) && !(O.edges & SOMVR_PTRE_RELEASE), "R's release is not L's");
    I.trigger[0] = 0, step();
    CHECK(O.edges & SOMVR_PTRE_RELEASE, "L's release");
    I.trigger[0] = 0.9f, I.trigger[1] = 0.9f, step();
    CHECK(O.hand == 0 && !(O.edges & SOMVR_PTRE_SWITCH) && (O.edges & SOMVR_PTRE_PRESS), "both at once: the pointing hand keeps it");
    I.trigger[0] = I.trigger[1] = 0, step();
    /* the right hand's press with its ray off the quad: it points now, but no click */
    float away[3] = {3, 0, -1};
    I.pose[1] = aim_at(HEAD_R, away);
    I.trigger[1] = 0.9f, step();
    CHECK(O.hand == 1 && (O.edges & SOMVR_PTRE_SWITCH) && !(O.edges & SOMVR_PTRE_PRESS) && !(O.flags & SOMVR_PTRF_ON) &&
              !(O.consume & 2u),
          "R pulled off the quad: switch, no click (edges %#x)", O.edges);
    aim_uv(1, 0.5f, 0.5f), step();
    CHECK((O.flags & SOMVR_PTRF_ON) && !(O.flags & SOMVR_PTRF_DOWN) && !(O.edges & SOMVR_PTRE_PRESS),
          "moved on while held: on, but no click");
    I.trigger[1] = 0, step();
    CHECK(!(O.edges & SOMVR_PTRE_RELEASE), "no release without a press");
    CHECK(O.consume == 3u, "both rays on the quad: consume %#x", O.consume);
    /* a target change while held: cancelled */
    I.trigger[1] = 0.9f, step();
    I.quad = SOMVR_PTRQ_NONE, step();
    CHECK((O.edges & SOMVR_PTRE_CANCEL) && !(O.flags & SOMVR_PTRF_ON) && O.consume == 0 && S.ups == S.downs,
          "no target: cancelled, nothing consumed (edges %#x)", O.edges);
    I.quad = SOMVR_PTRQ_MENU, I.trigger[1] = 0, step();
    CHECK((O.flags & SOMVR_PTRF_ON) && (O.edges & SOMVR_PTRE_ENTER), "the target back: on again");
    /* the grip fallback flag, a NaN trigger */
    I.grip[1] = 1, step();
    CHECK(O.flags & SOMVR_PTRF_GRIP, "grip flag");
    I.grip[1] = 0, I.trigger[1] = NAN, step();
    CHECK(!(O.flags & SOMVR_PTRF_DOWN) && !(O.edges & SOMVR_PTRE_PRESS), "a NaN trigger is up");
    CHECK(S.downs == S.ups, "every click ended: %u downs %u ups", S.downs, S.ups);
}

static void test_game_side(void) {
    int px[2];
    CHECK(somvr_ptr_pixel((float[2]){0, 0}, 1920, 1080, 0, 0, px) && px[0] == 0 && px[1] == 0, "0,0");
    CHECK(somvr_ptr_pixel((float[2]){1, 1}, 1920, 1080, 0, 0, px) && px[0] == 1919 && px[1] == 1079, "1,1 -> %d %d", px[0],
          px[1]);
    CHECK(somvr_ptr_pixel((float[2]){0.5f, 0.5f}, 1920, 1080, 7, -3, px) && px[0] == 967 && px[1] == 537, "centre + offset");
    CHECK(somvr_ptr_pixel((float[2]){0.25f / 1920, 0.9999f}, 1920, 1080, 0, 0, px) && px[0] == 0 && px[1] == 1079,
          "rounding down");
    CHECK(somvr_ptr_pixel((float[2]){NAN, -2}, 1920, 1080, 0, 0, px) && px[0] == 0 && px[1] == 0, "NaN / below 0");
    CHECK(!somvr_ptr_pixel((float[2]){0.5f, 0.5f}, 0, 1080, 0, 0, px), "no UI size");
    somvr_ptr_route r = {{0, 0}, {0, 0}};
    CHECK(!somvr_ptr_route_tick(&r, 1, 0, 1), "up: never the pointer's");
    CHECK(somvr_ptr_route_tick(&r, 1, 1, 1) && somvr_ptr_route_tick(&r, 1, 1, 0), "pressed on the quad: the pointer's while held");
    CHECK(!somvr_ptr_route_tick(&r, 1, 0, 1), "released");
    CHECK(!somvr_ptr_route_tick(&r, 0, 1, 0) && !somvr_ptr_route_tick(&r, 0, 1, 1), "pressed off the quad: the pad's while held");
    CHECK(!somvr_ptr_route_tick(&r, 0, 0, 1) && somvr_ptr_route_tick(&r, 0, 1, 1), "the next press decides anew");
    CHECK(!somvr_ptr_route_tick(&r, 2, 1, 1), "a bad hand");
    int bad = 0;
    CHECK(somvr_ptr_parse_mode(NULL, &bad) == SOMVR_PTRM_ON && somvr_ptr_parse_mode("", &bad) == SOMVR_PTRM_ON &&
              somvr_ptr_parse_mode("on", &bad) == SOMVR_PTRM_ON && somvr_ptr_parse_mode("dot", &bad) == SOMVR_PTRM_DOT &&
              somvr_ptr_parse_mode("off", &bad) == SOMVR_PTRM_OFF && somvr_ptr_parse_mode("0", &bad) == SOMVR_PTRM_OFF && !bad,
          "SOMVR_XR_POINTER");
    CHECK(somvr_ptr_parse_mode("laser", &bad) == SOMVR_PTRM_ON && bad, "a bad value");
}

static void test_image(void) {
    static uint8_t px[SOMVR_PTR_IMG_W * SOMVR_PTR_IMG_H * 4], bx[sizeof px];
    somvr_ptr_image(px, 0);
    somvr_ptr_image(bx, 1);
#define PX(b, x, y) (&(b)[4 * ((y) * SOMVR_PTR_IMG_W + (x))])
    const uint8_t *c = PX(px, 16, 16), *k = PX(px, 0, 0), *ring = PX(px, 16 + 11, 16), *ray = PX(px, 63, 16);
    CHECK(c[0] == 255 && c[1] == 255 && c[2] == 255 && c[3] == 255, "dot centre %u %u %u %u", c[0], c[1], c[2], c[3]);
    CHECK(!k[0] && !k[3] && !PX(px, 31, 16)[3] && !PX(px, 16, 0)[3] && !PX(px, 16, 31)[3], "the dot's edges are clear");
    CHECK(ring[3] == 255 && ring[0] < 80, "the dark ring %u a %u", ring[0], ring[3]);
    CHECK(ray[3] > 150 && PX(px, 33, 16)[3] < 60 && !PX(px, 48, 0)[3] && !PX(px, 48, 31)[3] && PX(px, 32, 16)[3] > 0,
          "ray: bright end %u, dim end %u", ray[3], PX(px, 33, 16)[3]);
    int premul = 1, swapped = 1;
    for (uint32_t i = 0; i < SOMVR_PTR_IMG_W * SOMVR_PTR_IMG_H; i++) {
        premul &= px[4 * i] <= px[4 * i + 3] && px[4 * i + 1] <= px[4 * i + 3] && px[4 * i + 2] <= px[4 * i + 3];
        swapped &= bx[4 * i] == px[4 * i + 2] && bx[4 * i + 2] == px[4 * i] && bx[4 * i + 1] == px[4 * i + 1] &&
                   bx[4 * i + 3] == px[4 * i + 3];
    }
    CHECK(premul && swapped, "premultiplied %d, bgra swapped %d", premul, swapped);
    int32_t r[4];
    somvr_ptr_rect(0, r);
    CHECK(r[0] == 0 && r[1] == 0 && r[2] == 32 && r[3] == 32, "dot rect");
    somvr_ptr_rect(1, r);
    CHECK(r[0] == 32 && r[1] == 0 && r[2] == 32 && r[3] == 32, "ray rect");
}

/* build 20: the held point (SOMVR_XR_PTR_HOLD) - a shaking aim held still, a real sweep followed at once, the parse */
static void hold_run(const somvr_ptr_cfg *c, float shake_deg, float sweep_deg, float *spread_uv, float *lag_deg,
                     float *end_uv) {
    somvr_ptr_state s;
    somvr_ptr_reset(&s);
    somvr_ptr_in in;
    memset(&in, 0, sizeof in);
    in.quad = SOMVR_PTRQ_MENU, in.quad_pose = MENU, in.quad_size[0] = MSIZE[0], in.quad_size[1] = MSIZE[1];
    in.ok[SOMVR_HAND_R] = 1;
    float lo[2] = {9, 9}, hi[2] = {-9, -9}, lag = 0, o0[3] = {0, 0, 0};
    for (int f = 0; f < 360; f++) { /* 4 s at 90 Hz: 2 s shaking at rest, a 0.5 s sweep, 1.5 s still after it */
        double t = f / 90.0;
        float k = f < 180 ? 0 : f > 225 ? 1 : (float)(f - 180) / 45.0f, sh = f < 180 ? shake_deg : 0;
        float yaw = sweep_deg * k + sh * (0.6f * sinf(6.2831853f * 7.9f * (float)t) + 0.4f * sinf(6.2831853f * 1.1f * (float)t));
        float pit = sh * (0.5f * sinf(6.2831853f * 9.7f * (float)t + 1) + 0.5f * sinf(6.2831853f * 0.6f * (float)t + 2));
        float a = 0.5f * yaw * 0.01745329f, b = 0.5f * pit * 0.01745329f;
        float qy[4] = {0, sinf(a), 0, cosf(a)}, qx[4] = {sinf(b), 0, 0, cosf(b)};
        in.pose[SOMVR_HAND_R] = (somvr_posef){{qy[3] * qx[0], qy[1] * qx[3], -qy[1] * qx[0], qy[3] * qx[3]}, {0, 0, 0}};
        in.time = (int64_t)(t * 1e9);
        somvr_ptr_out o;
        somvr_ptr_update(&s, c, &in, &o);
        if (f >= 60 && f < 180)
            for (int i = 0; i < 2; i++) lo[i] = fminf(lo[i], o.uv[i]), hi[i] = fmaxf(hi[i], o.uv[i]);
        if (f >= 180 && f <= 225) { /* the angle between the drawn point and the ray */
            float d[3] = {o.hit[0] - o0[0], o.hit[1] - o0[1], o.hit[2] - o0[2]}, r[3];
            float rq[4] = {in.pose[SOMVR_HAND_R].q[0], in.pose[SOMVR_HAND_R].q[1], in.pose[SOMVR_HAND_R].q[2],
                           in.pose[SOMVR_HAND_R].q[3]};
            somvr_posef rp = {{rq[0], rq[1], rq[2], rq[3]}, {0, 0, 0}};
            float oo[3];
            somvr_ptr_ray(&rp, oo, r);
            float n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), cs = (d[0] * r[0] + d[1] * r[1] + d[2] * r[2]) / n;
            float ang = acosf(cs < 1 ? cs : 1) * 57.29578f;
            lag = ang > lag ? ang : lag;
        }
        if (f == 359) end_uv[0] = o.uv[0], end_uv[1] = o.uv[1];
    }
    *spread_uv = fmaxf(hi[0] - lo[0], hi[1] - lo[1]), *lag_deg = lag;
}
static void test_hold20(void) {
    somvr_ptr_cfg c, h;
    somvr_ptr_cfg_default(&c);
    CHECK(c.hold_deg == 0, "the default: no hold (build 19's point)");
    int bad = 0;
    somvr_ptr_parse_hold("on", &h, &bad);
    CHECK(!bad && NEAR(h.hold_deg, 0.6, 1e-6) && NEAR(h.settle_s, 0.3, 1e-6), "on: 0.6 deg, 300 ms");
    somvr_ptr_parse_hold("1.2,500", &h, &bad);
    CHECK(!bad && NEAR(h.hold_deg, 1.2, 1e-6) && NEAR(h.settle_s, 0.5, 1e-6), "DEG,MS");
    somvr_ptr_parse_hold("0.8", &h, &bad);
    CHECK(!bad && NEAR(h.hold_deg, 0.8, 1e-6) && NEAR(h.settle_s, 0.3, 1e-6), "DEG");
    somvr_ptr_parse_hold("off", &h, &bad);
    CHECK(!bad && h.hold_deg == 0, "off");
    const char *bads[] = {"yes", "4", "0.5,", "0.5,3000", "-1", "0.5x"};
    for (int i = 0; i < 6; i++) {
        bad = 0;
        somvr_ptr_parse_hold(bads[i], &h, &bad);
        CHECK(bad && h.hold_deg == 0, "'%s': not understood, off", bads[i]);
    }
    h = c; /* (the parse sets the hold's two fields only) */
    somvr_ptr_parse_hold("on", &h, &bad);
    float s0, s1, l0, l1, e0[2], e1[2];
    hold_run(&c, 0.5f, 10, &s0, &l0, e0);
    hold_run(&h, 0.5f, 10, &s1, &l1, e1);
    printf("test_pointer: build 20 hold: a 0.5-deg shake at rest moves the point over %.4f uv (build 19's: %.4f); the "
           "sweep %.2f deg behind the ray at most (%.2f)\n", s1, s0, l1, l0);
    CHECK(s0 > 0.008f, "build 19's point shakes with the aim: %.4f uv", s0);
    CHECK(s1 < 0.4f * s0, "the held point: %.4f uv (build 19's %.4f)", s1, s0);
    CHECK(l1 <= l0 + 0.65f, "a real sweep followed at once: %.2f deg behind (build 19's filter %.2f)", l1, l0);
    float ex = e1[0] - e0[0], ey = e1[1] - e0[1];
    CHECK(sqrtf(ex * ex + ey * ey) * MSIZE[0] / 2.0f * 57.29578f < 0.35f, "1.5 s after the sweep: within 0.35 deg of "
          "build 19's point (%.3f deg)", sqrtf(ex * ex + ey * ey) * MSIZE[0] / 2.0f * 57.29578f);
}

int main(void) {
    test_geometry();
    test_hold20();
    test_in_from();
    test_state();
    test_game_side();
    test_image();
    printf("test_pointer: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
