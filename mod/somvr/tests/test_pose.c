/* test_pose.c - unit tests of somvr_pose.c and somvr_eyes.c: the run-1 samples (logs/boot-check-20261018-153551,
 * spike1) and hand-checked cases, plus an end-to-end check that an OpenXR eye's frustum edges land on the edges of the
 * game's projection (FUN_0072b050's formulas with the H3 tangents and centre offset). */
#include "../somvr_eyes.h"

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
#define DEG(x) ((float)(x) * 3.14159265358979f / 180.0f)

static float dot3(sv_vec3 a, sv_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float len3(sv_vec3 a) { return sqrtf(dot3(a, a)); }
static sv_vec3 sub3(sv_vec3 a, sv_vec3 b) { return (sv_vec3){a.x - b.x, a.y - b.y, a.z - b.z}; }
static sv_vec3 vec_of(const float p[3]) { return (sv_vec3){p[0], p[1], p[2]}; }

/* ---- run 1: consecutive moving frames of the benchmark flythrough (H2 pos, quat, next pos) ------------------------ */
static const struct { int frame; float p0[3], q[4], p1[3]; } FLY[] = {
    {5638, {29016.3086f, -2932.56665f, 7497.35205f}, {0.00946754217f, 1.00611605e-05f, 1.49152277e-06f, 0.999956489f}, {29017.3809f, -2931.62085f, 7502.22217f}},
    {5731, {14590.8301f, 454.699066f, 201.317581f}, {0.0811575055f, 0.818338156f, -0.119345389f, 0.556322157f}, {14595.8105f, 453.040497f, 199.129608f}},
    {5781, {14849.0645f, 369.331024f, 90.3945084f}, {0.0808510259f, 0.812246859f, -0.116067201f, 0.565904498f}, {14854.6201f, 367.515717f, 88.0899124f}},
    {5831, {15144.8701f, 273.451965f, -29.6124859f}, {0.0803157538f, 0.806643486f, -0.112832703f, 0.574583173f}, {15151.2969f, 271.387451f, -32.1576653f}},
    {5881, {15472.0244f, 169.461899f, -155.725067f}, {0.079771705f, 0.800952196f, -0.109588854f, 0.583184123f}, {15478.6729f, 167.363998f, -158.24115f}},
    {6042, {16688.8984f, -200.109619f, -577.85614f}, {0.0751158297f, 0.777469277f, -0.0944959298f, 0.617228508f}, {16696.7539f, -202.401962f, -580.35022f}},
    {6183, {17904.3906f, -539.021973f, -927.636108f}, {0.0607648529f, 0.732370615f, -0.0658839941f, 0.674982607f}, {17913.002f, -541.29364f, -929.825134f}},
    {6233, {18360.7715f, -656.238831f, -1036.77271f}, {0.0512267649f, 0.707611263f, -0.0516809598f, 0.702845991f}, {18370.1641f, -658.598083f, -1038.90369f}},
    {6283, {18829.6973f, -768.960144f, -1132.31238f}, {0.0391559675f, 0.679071605f, -0.0364789069f, 0.732119799f}, {18838.793f, -771.052795f, -1133.96545f}},
    {6333, {19308.9609f, -874.018616f, -1208.41064f}, {0.0249578822f, 0.647893012f, -0.0214320328f, 0.761021495f}, {19318.5723f, -875.967529f, -1209.604f}},
};
/* run 1: H2 fov vs H3 tanHalf of the same frame (prect 0,0,1920,1080) */
static const struct { int frame; float fov[2], tan[2]; } FOVTAN[] = {
    {2320, {0.369443715f, 0.209439516f}, {0.186851978f, 0.105104238f}},
    {4720, {0.607536972f, 0.34906584f}, {0.313470185f, 0.176326975f}},
    {5606, {1.26945388f, 0.785398185f}, {0.736379623f, 0.414213568f}},
    {5612, {1.26896799f, 0.785038352f}, {0.736005008f, 0.414002806f}},
    {5613, {1.26760352f, 0.78402859f}, {0.734953761f, 0.413411498f}},
    {5614, {1.26564717f, 0.782581687f}, {0.733448267f, 0.412564665f}},
    {5615, {1.2633158f, 0.780858994f}, {0.731657028f, 0.411557078f}},
    {5616, {1.26060367f, 0.778857231f}, {0.729577124f, 0.410387129f}},
};
/* run 1: spike1 off30 offsets (+30 cm along R(q)(1,0,0)); the vp one moved Talion's face LEFT in the picture */
static const struct { const char *what; float q[4], d[3]; } OFF30[] = {
    {"H2 vp frame 3963 (shot 13)", {-0.0261690747f, 1.19075958e-06f, 9.75832336e-06f, 0.999658108f}, {30, 0.000583429f, -8.6743e-05f}},
    {"H1 view frame 10113", {0.0386965647f, 0.000128077372f, 5.79591733e-06f, 0.999251842f}, {30, 0.000644863f, -0.00766542f}},
};

static void test_run1(void) {
    double sum = 0, worst = 1;
    for (size_t i = 0; i < sizeof FLY / sizeof FLY[0]; i++) {
        sv_vec3 d = sub3(vec_of(FLY[i].p1), vec_of(FLY[i].p0));
        sv_quat q = sv_quat_of(FLY[i].q);
        sv_vec3 f = sv_qrot(q, (sv_vec3){0, 0, 1}), b = sv_qrot(q, (sv_vec3){0, 0, -1});
        sv_vec3 fc = sv_qrot(sv_qconj(q), (sv_vec3){0, 0, 1});
        double c = dot3(f, d) / len3(d);
        sum += c;
        worst = c < worst ? c : worst;
        CHECK(c > 0.9, "frame %d: forward R(q)(0,0,1) vs motion cos %.3f", FLY[i].frame, c);
        CHECK(dot3(b, d) < 0, "frame %d: -Z would be forward", FLY[i].frame);
        if (fabsf(q.y) > 0.3f) /* the conjugate (passive) reading points elsewhere once the camera is yawed */
            CHECK(dot3(fc, d) / len3(d) < 0.5, "frame %d: conjugate also fits (%.3f)", FLY[i].frame, dot3(fc, d) / len3(d));
        float hm = atan2f(d.x, d.z), hq = sv_heading(q);
        CHECK(fabsf(sv_wrap_pi(hm - hq)) < DEG(20), /* the flythrough drifts sideways a little */ "frame %d: heading %.1f vs motion %.1f deg", FLY[i].frame,
              hq * 57.2958f, hm * 57.2958f);
        sv_vec3 up = sv_qrot(q, (sv_vec3){0, 1, 0});
        CHECK(up.y > 0.95f, "frame %d: local +Y is not world up (%.3f)", FLY[i].frame, up.y);
    }
    printf("run1 flythrough: %zu pairs, forward cos mean %.3f, worst %.3f\n", sizeof FLY / sizeof FLY[0],
           sum / (double)(sizeof FLY / sizeof FLY[0]), worst);
    for (size_t i = 0; i < sizeof FOVTAN / sizeof FOVTAN[0]; i++) {
        somvr_fovf f = sv_fov_of_view(FOVTAN[i].fov[0], FOVTAN[i].fov[1]);
        sv_proj p = sv_proj_from_fov(&f);
        CHECK(NEAR(p.tx, FOVTAN[i].tan[0], 2e-6) && NEAR(p.ty, FOVTAN[i].tan[1], 2e-6) && p.cx == 0 && p.cy == 0,
              "frame %d: tan %.9g,%.9g vs H3 %.9g,%.9g", FOVTAN[i].frame, p.tx, p.ty, FOVTAN[i].tan[0], FOVTAN[i].tan[1]);
        CHECK(NEAR(p.tx / p.ty, 1920.0 / 1080.0, 1e-4), "frame %d: aspect %.6f", FOVTAN[i].frame, p.tx / p.ty);
    }
    for (size_t i = 0; i < sizeof OFF30 / sizeof OFF30[0]; i++) {
        sv_vec3 r = sv_qrot(sv_quat_of(OFF30[i].q), (sv_vec3){30, 0, 0});
        CHECK(NEAR(r.x, OFF30[i].d[0], 1e-4) && NEAR(r.y, OFF30[i].d[1], 2e-5) && NEAR(r.z, OFF30[i].d[2], 2e-5),
              "%s: R(q)(30,0,0) = %g,%g,%g, logged %g,%g,%g", OFF30[i].what, r.x, r.y, r.z, OFF30[i].d[0],
              OFF30[i].d[1], OFF30[i].d[2]);
    }
}

/* ---- the mirror ------------------------------------------------------------------------------------------------ */
static void test_mirror(void) {
    unsigned s = 12345;
    for (int k = 0; k < 200; k++) {
        float q[4];
        for (int i = 0; i < 4; i++) q[i] = (float)((s = s * 1103515245u + 12345u) >> 8 & 0xffff) / 32768.0f - 1.0f;
        sv_quat xq = sv_qnormalize(sv_quat_of(q));
        sv_quat_to(xq, q);
        sv_quat gq = sv_xr_to_game_quat(q);
        sv_vec3 vs[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        for (int j = 0; j < 3; j++) { /* R(m(q)) v = M R(q) M v */
            sv_vec3 v = vs[j], mv = {v.x, v.y, -v.z};
            sv_vec3 a = sv_qrot(gq, v), b = sv_qrot(xq, mv);
            b.z = -b.z;
            CHECK(NEAR(a.x, b.x, 1e-5) && NEAR(a.y, b.y, 1e-5) && NEAR(a.z, b.z, 1e-5), "mirror %d axis %d", k, j);
        }
        float back[4];
        sv_game_to_xr_quat(gq, back);
        CHECK(back[0] == q[0] && back[1] == q[1] && back[2] == q[2] && back[3] == q[3], "quat round trip");
    }
    float p[3] = {0.25f, 1.5f, -2.0f}, b[3];
    sv_vec3 g = sv_xr_to_game_pos(p);
    CHECK(g.x == 25 && g.y == 150 && g.z == 200, "pos mirror %g %g %g", g.x, g.y, g.z);
    sv_game_to_xr_pos(g, b);
    CHECK(NEAR(b[0], p[0], 1e-6) && NEAR(b[1], p[1], 1e-6) && NEAR(b[2], p[2], 1e-6), "pos round trip");
    /* yaw: + turns right (toward +X) in the game */
    sv_vec3 f = sv_qrot(sv_qyaw(DEG(90)), (sv_vec3){0, 0, 1});
    CHECK(NEAR(f.x, 1, 1e-6) && NEAR(f.z, 0, 1e-6), "Y(90) forward %g %g", f.x, f.z);
    CHECK(NEAR(sv_heading(sv_qyaw(DEG(-135))), DEG(-135), 1e-5), "heading of Y(-135)");
    CHECK(NEAR(sv_wrap_pi(DEG(190)), DEG(-170), 1e-5) && NEAR(sv_wrap_pi(DEG(-190)), DEG(170), 1e-5), "wrap");
    /* straight down: the twist is used */
    sv_quat down = sv_qmul(sv_qyaw(DEG(30)), (sv_quat){sinf(DEG(45)), 0, 0, cosf(DEG(45))});
    CHECK(NEAR(sv_heading(down), DEG(30), 1e-4), "heading looking straight down %g", sv_heading(down) * 57.2958f);
}

/* ---- projection ------------------------------------------------------------------------------------------------ */
static void test_proj(void) {
    somvr_fovf f = {DEG(-52), DEG(44), DEG(46), DEG(-52)}; /* the doc's illustrative asymmetric eye */
    sv_proj p = sv_proj_from_fov(&f);
    CHECK(NEAR(p.tx, 1.1228, 1e-4) && NEAR(p.cx, -0.1571, 1e-4) && NEAR(p.ty, 1.1577, 1e-4) && NEAR(p.cy, -0.1222, 1e-4),
          "asymmetric tangents %g %g %g %g", p.tx, p.cx, p.ty, p.cy);
    somvr_fovf r = sv_fov_from_proj(p);
    CHECK(NEAR(r.l, f.l, 1e-6) && NEAR(r.r, f.r, 1e-6) && NEAR(r.u, f.u, 1e-6) && NEAR(r.d, f.d, 1e-6), "fov round trip");
    float fx, fy;
    sv_view_fov_cover(&f, &fx, &fy);
    CHECK(NEAR(fx, DEG(104), 1e-5) && NEAR(fy, DEG(104), 1e-5), "cover fov %g %g deg", fx * 57.2958f, fy * 57.2958f);
    somvr_fovf c = sv_fov_cover(&f);
    sv_proj pc = sv_proj_from_fov(&c);
    CHECK(NEAR(pc.tx, p.tx + fabsf(p.cx), 1e-5) && pc.cx == 0, "cover = tx + |cx| (H6 widening)");
    float xf[7] = {1, 2, 3, 0, 0, 0, 1};
    uint32_t h = 2166136261u;
    const unsigned char *b = (const unsigned char *)xf;
    for (int i = 0; i < 28; i++) h = (h ^ b[i]) * 16777619u;
    CHECK(sv_xf_hash(xf) == h, "hash = spike1 FNV-1a");
}

/* ---- the game's projection (FUN_0072b050) applied to a world point seen from a SomView with (tx, ty, cx, cy) --------- */
static void project(const somvr_view *v, sv_proj p, sv_vec3 world, float *xn, float *yn) {
    sv_vec3 d = sub3(world, vec_of(v->pos));
    sv_vec3 l = sv_qrot(sv_qconj(sv_quat_of(v->quat)), d); /* R(q)^T (world - pos) */
    *xn = (l.x / l.z - p.cx) / p.tx;
    *yn = (l.y / l.z - p.cy) / p.ty;
}

/* the same mapping the rig applies to eye positions, for any tracked point */
static sv_vec3 track_to_world(const somvr_rig *r, somvr_base b, const float p_m[3]) {
    sv_vec3 pm = sv_xr_to_game_pos(p_m), rel = sub3(pm, r->p0);
    sv_vec3 d = sv_qrot(sv_qyaw(b.yaw - r->yaw0), rel);
    return (sv_vec3){b.pos.x + d.x, b.pos.y + d.y, b.pos.z + d.z};
}

static somvr_view game_cam(float x, float y, float z, sv_quat q) {
    somvr_view v = {{x, y, z}, {q.x, q.y, q.z, q.w}, 1.26945388f, 0.785398185f, 27.5f, 100000.0f};
    return v;
}

static somvr_posef xr_pose(sv_quat q, float x, float y, float z) { return (somvr_posef){{q.x, q.y, q.z, q.w}, {x, y, z}}; }
static sv_quat xr_yaw(float a) { return (sv_quat){0, sinf(0.5f * a), 0, cosf(0.5f * a)}; }   /* OpenXR: + = left */
static sv_quat xr_pitch(float a) { return (sv_quat){sinf(0.5f * a), 0, 0, cosf(0.5f * a)}; } /* OpenXR: + = up */

static sv_vec3 fwd(const somvr_view *v) { return sv_qrot(sv_quat_of(v->quat), (sv_vec3){0, 0, 1}); }

static void test_eyes(void) {
    somvr_rig_cfg cfg = {.layout = SOMVR_LAYOUT_A, .base = SOMVR_BASE_CAMERA, .yaw = SOMVR_YAW_CAMERA,
                         .snap_rad = DEG(30)};
    somvr_rig rig;
    somvr_fovf fov = {DEG(-45), DEG(45), DEG(45), DEG(-45)};
    somvr_view g = game_cam(100, 200, 300, (sv_quat){0, 0, 0, 1});
    somvr_posef eye[2] = {xr_pose((sv_quat){0, 0, 0, 1}, -0.032f, 0, 0), xr_pose((sv_quat){0, 0, 0, 1}, 0.032f, 0, 0)};
    somvr_eye_out o[2];

    /* (a) identity head at the origin, identity camera: eyes 3.2 cm left/right of the camera, layout A rects */
    somvr_rig_init(&rig);
    somvr_base b = somvr_rig_frame(&rig, &cfg, eye, &g, NULL, 0, 0);
    for (int e = 0; e < 2; e++) somvr_rig_eye(&rig, &cfg, b, &g, &eye[e], &fov, e, &o[e]);
    CHECK(rig.have_ref && rig.recenters == 1, "first frame recentres");
    CHECK(NEAR(o[0].view.pos[0], 96.8, 1e-4) && NEAR(o[1].view.pos[0], 103.2, 1e-4) && o[0].view.pos[1] == 200 &&
              o[0].view.pos[2] == 300,
          "(a) eye x %g %g", o[0].view.pos[0], o[1].view.pos[0]);
    CHECK(o[0].view.quat[3] == 1 && o[0].view.quat[0] == 0, "(a) identity quat");
    CHECK(o[0].rect[0] == 0 && o[0].rect[2] == 0.5f && o[1].rect[0] == 0.5f && o[1].rect[2] == 1 && o[0].slot == 0 &&
              o[1].slot == 0,
          "(a) layout A rects/slots");
    CHECK(NEAR(o[0].view.fov_x, DEG(90), 1e-5) && NEAR(o[0].view.fov_y, DEG(90), 1e-5) && o[0].view.near_z == 27.5f,
          "(a) fov/near kept");
    CHECK(o[0].hash == sv_xf_hash(&o[0].view) && o[0].hash != o[1].hash, "(a) hash");
    sv_vec3 left = sv_qrot(sv_quat_of(o[0].view.quat), (sv_vec3){-1, 0, 0});
    sv_vec3 e01 = sub3(vec_of(o[0].view.pos), vec_of(o[1].view.pos));
    CHECK(dot3(left, e01) > 6.39f, "(a) eye 0 is on the left (R(q)(-1,0,0))");

    /* (b) turn the head 90 deg left in OpenXR: the game view looks along -X (left of +Z) */
    somvr_posef t[2] = {xr_pose(xr_yaw(DEG(90)), 0, 0, 0), xr_pose(xr_yaw(DEG(90)), 0, 0, 0)};
    somvr_rig_eye(&rig, &cfg, b, &g, &t[0], &fov, 0, &o[0]);
    sv_vec3 f = fwd(&o[0].view);
    CHECK(NEAR(f.x, -1, 1e-5) && NEAR(f.z, 0, 1e-5), "(b) head left -> forward %g %g %g", f.x, f.y, f.z);
    /* (c) pitch up 30 deg in OpenXR: the forward axis rises */
    t[0] = xr_pose(xr_pitch(DEG(30)), 0, 0, 0);
    somvr_rig_eye(&rig, &cfg, b, &g, &t[0], &fov, 0, &o[0]);
    f = fwd(&o[0].view);
    CHECK(NEAR(f.y, 0.5, 1e-5) && NEAR(f.z, 0.8660254, 1e-5), "(c) pitch up -> forward %g %g %g", f.x, f.y, f.z);
    /* (d) step 1 m forward (-Z) and 10 cm up: +100 cm along +Z, +10 cm in y */
    t[0] = xr_pose((sv_quat){0, 0, 0, 1}, 0, 0.1f, -1);
    somvr_rig_eye(&rig, &cfg, b, &g, &t[0], &fov, 0, &o[0]);
    CHECK(NEAR(o[0].view.pos[0], 100, 1e-3) && NEAR(o[0].view.pos[1], 210, 1e-3) && NEAR(o[0].view.pos[2], 400, 1e-3),
          "(d) step forward -> %g %g %g", o[0].view.pos[0], o[0].view.pos[1], o[0].view.pos[2]);

    /* (e) the game camera faces +X (heading +90) and pitches 20 deg down: the view follows its yaw only */
    somvr_view g2 = game_cam(0, 0, 0, sv_qmul(sv_qyaw(DEG(90)), (sv_quat){sinf(DEG(10)), 0, 0, cosf(DEG(10))}));
    b = somvr_rig_frame(&rig, &cfg, eye, &g2, NULL, 0, 0);
    CHECK(NEAR(b.yaw, DEG(90), 1e-5), "(e) base yaw %g", b.yaw * 57.2958f);
    somvr_rig_eye(&rig, &cfg, b, &g2, &eye[0], &fov, 0, &o[0]);
    f = fwd(&o[0].view);
    CHECK(NEAR(f.x, 1, 1e-5) && NEAR(f.y, 0, 1e-5), "(e) level view along +X: %g %g %g", f.x, f.y, f.z);
    CHECK(NEAR(o[0].view.pos[2], 3.2, 1e-4) && NEAR(o[0].view.pos[0], 0, 1e-4), "(e) left eye at +Z (%g %g)",
          o[0].view.pos[0], o[0].view.pos[2]);
    t[0] = xr_pose((sv_quat){0, 0, 0, 1}, 0, 0, -1);
    somvr_rig_eye(&rig, &cfg, b, &g2, &t[0], &fov, 0, &o[0]);
    CHECK(NEAR(o[0].view.pos[0], 100, 1e-3) && NEAR(o[0].view.pos[2], 0, 1e-3), "(e) step forward -> +X %g %g",
          o[0].view.pos[0], o[0].view.pos[2]);

    /* (f) recentre with the head 45 deg left of the tracking origin, 0.3 m right, 1.6 m up, 0.2 m ahead */
    /* the head's right axis in OpenXR after 45 deg left: (cos 45, 0, -sin 45); eyes at -/+ 3.2 cm along it */
    somvr_posef h45[2] = {xr_pose(xr_yaw(DEG(45)), 0.3f - 0.0226f, 1.6f, -0.2f + 0.0226f),
                          xr_pose(xr_yaw(DEG(45)), 0.3f + 0.0226f, 1.6f, -0.2f - 0.0226f)};
    b = somvr_rig_frame(&rig, &cfg, h45, &g, NULL, 1, 0);
    CHECK(rig.recenters == 2 && NEAR(rig.yaw0, DEG(-45), 1e-5), "(f) yaw0 %g", rig.yaw0 * 57.2958f);
    for (int e = 0; e < 2; e++) somvr_rig_eye(&rig, &cfg, b, &g, &h45[e], &fov, e, &o[e]);
    float mx = 0.5f * (o[0].view.pos[0] + o[1].view.pos[0]), my = 0.5f * (o[0].view.pos[1] + o[1].view.pos[1]),
          mz = 0.5f * (o[0].view.pos[2] + o[1].view.pos[2]);
    CHECK(NEAR(mx, 100, 1e-3) && NEAR(my, 200, 1e-3) && NEAR(mz, 300, 1e-3), "(f) head on the base %g %g %g", mx, my, mz);
    f = fwd(&o[0].view);
    CHECK(NEAR(f.x, 0, 1e-5) && NEAR(f.z, 1, 1e-5), "(f) looks along the camera heading %g %g", f.x, f.z);
    CHECK(NEAR(o[1].view.pos[0] - o[0].view.pos[0], 6.392, 2e-3), "(f) eyes along the base's right axis");

    /* (g) one snap step: the view turns 30 deg right */
    b = somvr_rig_frame(&rig, &cfg, h45, &g, NULL, 0, 1);
    somvr_rig_eye(&rig, &cfg, b, &g, &h45[0], &fov, 0, &o[0]);
    f = fwd(&o[0].view);
    CHECK(NEAR(atan2f(f.x, f.z), DEG(30), 1e-5), "(g) snap right: heading %g", atan2f(f.x, f.z) * 57.2958f);
    CHECK(rig.snap_steps == 1, "(g) steps");

    /* (h) floor mode keeps the real head height; player base adds eye_cm to the player position */
    somvr_rig_cfg fl = cfg;
    fl.floor = 1;
    fl.base = SOMVR_BASE_PLAYER;
    fl.eye_cm = 5;
    somvr_rig r2;
    somvr_rig_init(&r2);
    float player[3] = {1000, 50, -20};
    b = somvr_rig_frame(&r2, &fl, h45, &g, player, 0, 0);
    somvr_rig_eye(&r2, &fl, b, &g, &h45[0], &fov, 0, &o[0]);
    CHECK(NEAR(o[0].view.pos[1], 50 + 5 + 160, 1e-3), "(h) floor height %g", o[0].view.pos[1]);
    CHECK(NEAR(b.pos.x, 1000, 1e-6) && NEAR(b.pos.z, -20, 1e-6), "(h) player base");

    /* (i) fixed yaw: turning the game camera later does not turn the view */
    somvr_rig_cfg fx = cfg;
    fx.yaw = SOMVR_YAW_FIXED;
    somvr_rig r3;
    somvr_rig_init(&r3);
    b = somvr_rig_frame(&r3, &fx, eye, &g, NULL, 0, 0);
    b = somvr_rig_frame(&r3, &fx, eye, &g2, NULL, 0, 0);
    CHECK(NEAR(b.yaw, 0, 1e-6), "(i) fixed yaw %g", b.yaw);

    /* (j) layout B: full rects, slot = eye */
    somvr_rig_cfg lb = cfg;
    lb.layout = SOMVR_LAYOUT_B;
    for (int e = 0; e < 2; e++) somvr_rig_eye(&rig, &lb, b, &g, &eye[e], &fov, e, &o[e]);
    CHECK(o[0].rect[0] == 0 && o[0].rect[2] == 1 && o[1].rect[0] == 0 && o[1].rect[2] == 1 && o[0].slot == 0 &&
              o[1].slot == 1 && somvr_layout_nslots(SOMVR_LAYOUT_B) == 2 && somvr_layout_nslots(SOMVR_LAYOUT_A) == 1,
          "(j) layout B");
}

/* SOMVR_AUDIO / SOMVR_BASE=head / SOMVR_FOLLOW: the head pose, the head base, the follow's offset, step and shift */
static void test_body(void) {
    somvr_rig_cfg cfg = {.layout = SOMVR_LAYOUT_B, .base = SOMVR_BASE_HEAD, .yaw = SOMVR_YAW_CAMERA, .snap_rad = DEG(30),
                         .eye_cm = 3};
    somvr_rig rig;
    somvr_rig_init(&rig);
    somvr_fovf fov = {DEG(-45), DEG(45), DEG(45), DEG(-45)};
    somvr_view g = game_cam(100, 200, 300, sv_qyaw(DEG(40)));
    float node[3] = {5000, 180, -700};
    /* recentre with the head 20 deg left, 1.7 m up (eyes -/+ 3.2 cm along its right axis) */
    sv_quat q20 = xr_yaw(DEG(20));
    sv_vec3 r20 = sv_qrot(q20, (sv_vec3){0.032f, 0, 0});
    somvr_posef ref[2] = {xr_pose(q20, 0.2f - r20.x, 1.7f, 0.1f - r20.z), xr_pose(q20, 0.2f + r20.x, 1.7f, 0.1f + r20.z)};
    somvr_base b = somvr_rig_frame(&rig, &cfg, ref, &g, node, 1, 0);
    CHECK(NEAR(b.pos.x, 5000, 1e-6) && NEAR(b.pos.y, 183, 1e-6) && NEAR(b.pos.z, -700, 1e-6) && NEAR(b.yaw, DEG(40), 1e-5),
          "(a) head base %g %g %g yaw %g", b.pos.x, b.pos.y, b.pos.z, b.yaw * 57.2958f);
    somvr_base bc = somvr_rig_frame(&rig, &cfg, ref, &g, NULL, 0, 0);
    CHECK(bc.pos.x == 100 && bc.pos.y == 200 && bc.pos.z == 300, "(a) no head node: the camera base");
    /* (b) the head pose = the eyes' centre and their orientation, for a turned, pitched, moved head */
    sv_quat hq = sv_qnormalize(sv_qmul(xr_yaw(DEG(-35)), xr_pitch(DEG(15))));
    sv_vec3 rr = sv_qrot(hq, (sv_vec3){0.032f, 0, 0});
    somvr_posef t[2] = {xr_pose(hq, 0.5f - rr.x, 1.6f - rr.y, -0.3f - rr.z),
                        xr_pose(hq, 0.5f + rr.x, 1.6f + rr.y, -0.3f + rr.z)};
    somvr_eye_out o[2];
    for (int e = 0; e < 2; e++) somvr_rig_eye(&rig, &cfg, b, &g, &t[e], &fov, e, &o[e]);
    sv_vec3 hp;
    sv_quat hqg;
    somvr_rig_head(&rig, b, t, &hp, &hqg);
    float mx = 0.5f * (o[0].view.pos[0] + o[1].view.pos[0]), my = 0.5f * (o[0].view.pos[1] + o[1].view.pos[1]),
          mz = 0.5f * (o[0].view.pos[2] + o[1].view.pos[2]);
    CHECK(NEAR(hp.x, mx, 1e-3) && NEAR(hp.y, my, 1e-3) && NEAR(hp.z, mz, 1e-3), "(b) head %g %g %g vs eyes' centre %g %g %g",
          hp.x, hp.y, hp.z, mx, my, mz);
    sv_vec3 f = sv_qrot(hqg, (sv_vec3){0, 0, 1}), fe = fwd(&o[0].view), u = sv_qrot(hqg, (sv_vec3){0, 1, 0});
    sv_vec3 ue = sv_qrot(sv_quat_of(o[1].view.quat), (sv_vec3){0, 1, 0});
    CHECK(NEAR(f.x, fe.x, 1e-5) && NEAR(f.y, fe.y, 1e-5) && NEAR(f.z, fe.z, 1e-5) && NEAR(u.x, ue.x, 1e-5) &&
              NEAR(u.y, ue.y, 1e-5) && NEAR(u.z, ue.z, 1e-5),
          "(b) head axes vs the eyes' (front %g %g %g / %g %g %g)", f.x, f.y, f.z, fe.x, fe.y, fe.z);
    /* pitch up 15 in OpenXR: front rises by sin 15; heading = the camera's 40 + 35 right + the reference's 20 left */
    CHECK(NEAR(f.y, sinf(DEG(15)), 1e-5) && NEAR(atan2f(f.x, f.z), sv_wrap_pi(DEG(40) + DEG(35) + DEG(20)), 1e-4),
          "(b) front %g %g %g heading %g", f.x, f.y, f.z, atan2f(f.x, f.z) * 57.2958f);
    /* (c) the follow's offset: the head's horizontal offset from the base; a shift by a body move cancels it */
    sv_vec3 off = somvr_rig_offset(&rig, b, t);
    CHECK(off.y == 0 && NEAR(off.x, hp.x - b.pos.x, 1e-3) && NEAR(off.z, hp.z - b.pos.z, 1e-3), "(c) offset %g %g %g", off.x,
          off.y, off.z);
    somvr_rig moved = rig;
    somvr_base b2 = b;
    sv_vec3 a = {off.x * 0.6f, 0, off.z * 0.6f};
    b2.pos.x += a.x, b2.pos.z += a.z; /* the body (the head node) moved by a */
    somvr_rig_shift(&moved, b2, a);
    sv_vec3 hp2;
    somvr_rig_head(&moved, b2, t, &hp2, &hqg);
    CHECK(NEAR(hp2.x, hp.x, 1e-3) && NEAR(hp2.y, hp.y, 1e-3) && NEAR(hp2.z, hp.z, 1e-3),
          "(c) the view moved with the body %g %g %g", hp2.x - hp.x, hp2.y - hp.y, hp2.z - hp.z);
    sv_vec3 off2 = somvr_rig_offset(&moved, b2, t);
    CHECK(NEAR(off2.x, 0.4f * off.x, 1e-3) && NEAR(off2.z, 0.4f * off.z, 1e-3), "(c) offset after the move %g %g", off2.x,
          off2.z);
    /* a turned base (snap / camera yaw) between the request and the shift: still exact for the current base */
    somvr_base b3 = b2;
    b3.yaw = sv_wrap_pi(b2.yaw + DEG(30));
    somvr_rig turned = rig;
    somvr_rig_head(&turned, b3, t, &hp, &hqg);
    b3.pos.x += 7, b3.pos.z -= 4;
    somvr_rig_shift(&turned, b3, (sv_vec3){7, 5, -4}); /* y = 5 is ignored (P0.y stays) */
    somvr_rig_head(&turned, b3, t, &hp2, &hqg);
    CHECK(NEAR(hp2.x, hp.x, 1e-3) && NEAR(hp2.y, hp.y, 1e-3) && NEAR(hp2.z, hp.z, 1e-3), "(c) turned base: moved %g %g %g",
          hp2.x - hp.x, hp2.y - hp.y, hp2.z - hp.z);
    /* (d) the step: the deadzone, the clamp, horizontal only, NaN */
    sv_vec3 s = somvr_follow_step((sv_vec3){5, 30, 0}, 8, 30);
    CHECK(s.x == 0 && s.y == 0 && s.z == 0, "(d) inside the deadzone (y ignored)");
    s = somvr_follow_step((sv_vec3){0, 0, 8}, 8, 30);
    CHECK(s.z == 0, "(d) on the deadzone");
    s = somvr_follow_step((sv_vec3){0, 4, 20}, 8, 30);
    CHECK(s.x == 0 && s.y == 0 && s.z == 20, "(d) 20 cm: %g %g %g", s.x, s.y, s.z);
    s = somvr_follow_step((sv_vec3){30, 0, -40}, 8, 30);
    CHECK(NEAR(s.x, 18, 1e-5) && s.y == 0 && NEAR(s.z, -24, 1e-5), "(d) 50 cm clamped to 30: %g %g", s.x, s.z);
    s = somvr_follow_step((sv_vec3){NAN, 0, 1}, 8, 30);
    CHECK(s.x == 0 && s.z == 0, "(d) NaN");
}

/* An eye with an asymmetric fov, a tilted/turned head, a yawed+pitched game camera and a non-trivial recentre: the
 * four edges of the eye's OpenXR frustum must project onto x_ndc/y_ndc = -1/+1 through the game's SomView and the H3
 * tangents - i.e. the image the game renders is exactly the image OpenXR expects for that pose and fov. */
static void test_end_to_end(void) {
    somvr_rig_cfg cfg = {.layout = SOMVR_LAYOUT_A, .snap_rad = DEG(30)};
    somvr_rig rig;
    somvr_rig_init(&rig);
    somvr_view g = game_cam(5000, -300, 1200, sv_qmul(sv_qyaw(DEG(-70)), (sv_quat){sinf(DEG(-8)), 0, 0, cosf(DEG(-8))}));
    somvr_posef ref[2] = {xr_pose(xr_yaw(DEG(20)), 0.1f, 1.7f, 0.4f), xr_pose(xr_yaw(DEG(20)), 0.16f, 1.7f, 0.4f)};
    somvr_base b = somvr_rig_frame(&rig, &cfg, ref, &g, NULL, 0, 2);
    sv_quat hq = sv_qnormalize(sv_qmul(xr_yaw(DEG(-35)), sv_qmul(xr_pitch(DEG(25)), (sv_quat){0, 0, sinf(DEG(5)), cosf(DEG(5))})));
    somvr_posef eye = xr_pose(hq, 0.3f, 1.55f, 0.1f);
    somvr_fovf fov = {DEG(-52), DEG(44), DEG(46), DEG(-52)};
    somvr_eye_out o;
    somvr_rig_eye(&rig, &cfg, b, &g, &eye, &fov, 0, &o);
    struct { float a, b; const char *what; } edges[] = { /* OpenXR view-space ray (a, b, -1) */
        {tanf(fov.r), 0, "right"}, {tanf(fov.l), 0, "left"}, {0, tanf(fov.u), "top"}, {0, tanf(fov.d), "bottom"},
        {tanf(fov.r), tanf(fov.u), "top-right corner"}, {tanf(fov.l), tanf(fov.d), "bottom-left corner"},
        {0.3f, -0.2f, "inside"},
    };
    sv_proj p0 = {o.proj.tx, o.proj.ty, 0, 0};
    for (size_t i = 0; i < sizeof edges / sizeof edges[0]; i++) {
        /* OpenXR's own NDC for that ray: tangent space mapped linearly, angleLeft -> -1, angleRight -> +1, down -> -1 */
        float wx = 2 * (edges[i].a - tanf(fov.l)) / (tanf(fov.r) - tanf(fov.l)) - 1;
        float wy = 2 * (edges[i].b - tanf(fov.d)) / (tanf(fov.u) - tanf(fov.d)) - 1;
        float xn, yn;
        sv_vec3 dl = {edges[i].a * 3, edges[i].b * 3, -3}; /* 3 m along the ray */
        sv_vec3 dw = sv_qrot(sv_quat_of(eye.q), dl);
        float pw[3] = {eye.p[0] + dw.x, eye.p[1] + dw.y, eye.p[2] + dw.z};
        project(&o.view, o.proj, track_to_world(&rig, b, pw), &xn, &yn);
        CHECK(NEAR(xn, wx, 2e-4) && NEAR(yn, wy, 2e-4), "ray %s -> ndc %g %g, OpenXR %g %g", edges[i].what, xn, yn, wx, wy);
        if (i == 0) { /* without H3's offset the same point misses the edge: the offset matters */
            project(&o.view, p0, track_to_world(&rig, b, pw), &xn, &yn);
            CHECK(fabsf(xn - 1) > 0.05f, "edge without the offset still hits (%g)", xn);
        }
    }
    printf("end to end: eye frustum edges land on the game's NDC edges (asymmetric fov, offset %.4f %.4f)\n", o.proj.cx,
           o.proj.cy);
}

/* FOV overscan (SOMVR_OVERSCAN, re-xr-pipeline.md 7.2): the widened frustum, its margins, and end to end: through the
 * rig and the game's projection the eye's real edges land at NDC +-1/(1+s) (the margin is rendered), the widened
 * edges at +-1 (what the layer submits is what the game rendered). */
static void test_overscan(void) {
    const somvr_fovf q2 = {-0.75049f, 0.78540f, 0.83776f, -0.87266f}; /* WiVRn's Quest 2 left eye (headset look 1) */
    const somvr_fovf doc = {DEG(-52), DEG(44), DEG(46), DEG(-52)};
    const somvr_fovf *eyes[2] = {&q2, &doc};
    float no[] = {0.0f, -0.1f, NAN, -INFINITY};
    for (size_t i = 0; i < sizeof no / sizeof no[0]; i++) {
        somvr_fovf o = sv_fov_overscan(&q2, no[i]);
        CHECK(!memcmp(&o, &q2, sizeof o), "overscan %g changed the fov", no[i]);
    }
    for (int k = 0; k < 2; k++) {
        const somvr_fovf *f = eyes[k];
        sv_proj p = sv_proj_from_fov(f);
        float prev_r = f->r;
        for (int j = 1; j <= 5; j++) {
            float s = 0.05f * (float)j;
            somvr_fovf o = sv_fov_overscan(f, s);
            sv_proj po = sv_proj_from_fov(&o);
            CHECK(NEAR(po.tx, (1 + s) * p.tx, 2e-5) && NEAR(po.ty, (1 + s) * p.ty, 2e-5) && NEAR(po.cx, p.cx, 2e-5) &&
                      NEAR(po.cy, p.cy, 2e-5),
                  "eye %d s %g: tangents %g %g %g %g, want %g %g %g %g", k, s, po.tx, po.ty, po.cx, po.cy, (1 + s) * p.tx,
                  (1 + s) * p.ty, p.cx, p.cy);
            /* the same tangent margin on both sides of each axis: s x the half-extent */
            float ml = tanf(f->l) - tanf(o.l), mr = tanf(o.r) - tanf(f->r);
            float mu = tanf(o.u) - tanf(f->u), md = tanf(f->d) - tanf(o.d);
            CHECK(NEAR(ml, s * p.tx, 2e-5) && NEAR(mr, s * p.tx, 2e-5) && NEAR(mu, s * p.ty, 2e-5) &&
                      NEAR(md, s * p.ty, 2e-5),
                  "eye %d s %g: margins l %g r %g u %g d %g", k, s, ml, mr, mu, md);
            CHECK(o.l < f->l && o.r > f->r && o.u > f->u && o.d < f->d && o.r > prev_r, "eye %d s %g: not wider", k, s);
            prev_r = o.r;
        }
        somvr_fovf cap = sv_fov_overscan(f, 7.0f), half = sv_fov_overscan(f, SOMVR_OVERSCAN_MAX);
        CHECK(!memcmp(&cap, &half, sizeof cap), "eye %d: overscan not capped at %g", k, SOMVR_OVERSCAN_MAX);
    }
    /* the default 0.1 on the Quest 2 eye: 2.6-2.8 degrees of head turn on every side, the covering fov < 179 */
    somvr_fovf o = sv_fov_overscan(&q2, 0.1f);
    float dl = (q2.l - o.l) / DEG(1), dr = (o.r - q2.r) / DEG(1), du = (o.u - q2.u) / DEG(1), dd = (q2.d - o.d) / DEG(1);
    CHECK(dl > 2.5f && dl < 3.0f && dr > 2.5f && dr < 3.0f && du > 2.5f && du < 3.0f && dd > 2.5f && dd < 3.0f,
          "Quest 2 margins %.2f %.2f %.2f %.2f deg", dl, dr, du, dd);
    float fx, fy;
    sv_view_fov_cover(&o, &fx, &fy);
    CHECK(fx < DEG(179) && fy < DEG(179) && fx > 2 * q2.r && fy > 2 * fabsf(q2.d), "covering fov %g %g deg", fx / DEG(1),
          fy / DEG(1));
    printf("overscan 0.1 on the Quest 2 eye: %.4f..%.4f / %.4f..%.4f rad, margins L %.2f R %.2f U %.2f D %.2f deg, "
           "pixels per tangent x %.3f\n", o.l, o.r, o.d, o.u, dl, dr, du, dd, 1 / 1.1f);

    /* end to end, the asymmetric eye of test_end_to_end with s = 0.1 */
    const float s = 0.1f;
    somvr_rig_cfg cfg = {.layout = SOMVR_LAYOUT_B, .snap_rad = DEG(30)};
    somvr_rig rig;
    somvr_rig_init(&rig);
    somvr_view g = game_cam(-800, 40, 2500, sv_qyaw(DEG(35)));
    somvr_posef ref[2] = {xr_pose(xr_yaw(DEG(-10)), -0.03f, 1.6f, 0.0f), xr_pose(xr_yaw(DEG(-10)), 0.034f, 1.6f, 0.0f)};
    somvr_base b = somvr_rig_frame(&rig, &cfg, ref, &g, NULL, 0, 0);
    somvr_posef eye = xr_pose(sv_qnormalize(sv_qmul(xr_yaw(DEG(25)), xr_pitch(DEG(-15)))), 0.05f, 1.62f, -0.2f);
    somvr_fovf os = sv_fov_overscan(&doc, s);
    somvr_eye_out out;
    somvr_rig_eye(&rig, &cfg, b, &g, &eye, &os, 1, &out);
    sv_proj pd = sv_proj_from_fov(&doc);
    CHECK(NEAR(out.proj.tx, (1 + s) * pd.tx, 2e-5) && NEAR(out.proj.cx, pd.cx, 2e-5), "rig: H3 tangents %g %g",
          out.proj.tx, out.proj.cx);
    somvr_fovf cov = sv_fov_cover(&doc);
    CHECK(out.fov_cover.r >= cov.r && out.fov_cover.u >= cov.u && out.view.fov_x >= 2 * cov.r,
          "rig: the covering fov does not contain the eye's real one");
    struct { float a, b, xn, yn; const char *what; } rays[] = { /* OpenXR view-space ray (a, b, -1) */
        {tanf(doc.r), 0, 0, 0, "real right edge"}, {tanf(doc.l), 0, 0, 0, "real left edge"},
        {0, tanf(doc.u), 0, 0, "real top edge"}, {0, tanf(doc.d), 0, 0, "real bottom edge"},
        {tanf(os.r), 0, 0, 0, "widened right edge"}, {tanf(os.l), tanf(os.d), 0, 0, "widened bottom-left corner"},
    };
    for (size_t i = 0; i < sizeof rays / sizeof rays[0]; i++) {
        float wx = 2 * (rays[i].a - tanf(os.l)) / (tanf(os.r) - tanf(os.l)) - 1; /* the submitted fov's own NDC */
        float wy = 2 * (rays[i].b - tanf(os.d)) / (tanf(os.u) - tanf(os.d)) - 1;
        sv_vec3 dl3 = {rays[i].a * 2, rays[i].b * 2, -2};
        sv_vec3 dw = sv_qrot(sv_quat_of(eye.q), dl3);
        float pw[3] = {eye.p[0] + dw.x, eye.p[1] + dw.y, eye.p[2] + dw.z};
        float xn, yn;
        project(&out.view, out.proj, track_to_world(&rig, b, pw), &xn, &yn);
        CHECK(NEAR(xn, wx, 2e-4) && NEAR(yn, wy, 2e-4), "overscan ray %s -> ndc %g %g, submitted fov's %g %g",
              rays[i].what, xn, yn, wx, wy);
        if (i < 4) { /* the real edges sit inside the image, 1/(1+s) of the way out from the frustum's centre */
            float r = i < 2 ? xn : yn, want = (i == 0 || i == 2) ? 1 / (1 + s) : -1 / (1 + s);
            CHECK(NEAR(r, want, 3e-4), "real edge %s at ndc %g, want %g", rays[i].what, r, want);
        }
    }
    printf("overscan end to end: the real edges at ndc +-%.4f, the widened ones at +-1 (s %.2f)\n", 1 / (1 + s), s);
}

int main(void) {
    test_run1();
    test_mirror();
    test_proj();
    test_eyes();
    test_body();
    test_end_to_end();
    test_overscan();
    printf("test_pose: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
