/* test_vrhands.c - somvr_vrhands.c: the hand model, its fist on the grip's axis, the skin, the mirror, the wrist's
 * fade, the watch's pose and its debounce, the panel's clearance, the ABI (docs/re-xr-pipeline.md 33). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../somvr_input.h"
#include "../somvr_vrhands.h"

static int g_fail, g_n;
#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        g_n++;                                                                                                         \
        if (!(c)) {                                                                                                    \
            g_fail++;                                                                                                  \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

static somvr_vh_model m;
static somvr_vh_cfg c;
static somvr_vh_vert va[1400], vb[1400];

static float len3(const float a[3]) { return sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }
static void qrot(const float q[4], const float v[3], float o[3]) {
    float t[3] = {2 * (q[1] * v[2] - q[2] * v[1]), 2 * (q[2] * v[0] - q[0] * v[2]), 2 * (q[0] * v[1] - q[1] * v[0])};
    o[0] = v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]);
    o[1] = v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]);
    o[2] = v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]);
}

static void test_model(void) {
    somvr_vh_cfg_default(&c);
    CHECK(somvr_vh_init(&m, &c) == 0 && m.ok, "init: %s", m.why);
    printf("model: %s\n", m.why);
    CHECK(somvr_vh_nverts() > 1000 && somvr_vh_nverts() <= 1400 && somvr_vh_nindices() % 3 == 0, "sizes");
    CHECK(m.tube_r_m > 0.018f && m.tube_r_m < 0.03f, "the fist's joint radius %.4f m (a 3-4 cm grip)", m.tube_r_m);
    float wd = len3(m.wrist_g);
    CHECK(wd > 0.05f && wd < 0.11f, "the wrist %.3f m from the grip", wd);
    CHECK(m.wrist_g[1] > 0.04f, "the wrist toward the elbow (+Y): %.3f", m.wrist_g[1]);
    CHECK(m.arm_g[1] > 0.8f, "the forearm along the grip's +Y (OpenXR's wrist side): %.2f %.2f %.2f", m.arm_g[0], m.arm_g[1],
          m.arm_g[2]);
    CHECK(m.back_g[0] > 0.95f, "the back of the right hand along the grip's +X (\"into the palm\")");
    CHECK(m.palm_reach_m > 0.02f && m.palm_reach_m < 0.06f, "the palm's reach %.3f m", m.palm_reach_m);
    CHECK(m.wrist_r_m > 0.01f && m.wrist_r_m < 0.04f, "the wrist's height %.3f m", m.wrist_r_m);
    CHECK(m.tip_g[1] < -0.05f, "the pointing tip ahead of the fist along -Y: %.3f", m.tip_g[1]);
    int ones = 0, clear = 0, bad = 0;
    for (int v = 0; v < somvr_vh_nverts(); v++) {
        ones += m.fade[v] > 0.999f, clear += m.fade[v] < 0.05f;
        bad += !(m.fade[v] >= 0 && m.fade[v] <= 1);
    }
    CHECK(!bad && ones > somvr_vh_nverts() / 2 && clear > 20, "the fade: %d opaque, %d clear, %d bad", ones, clear, bad);
    somvr_vh_cfg c2 = c;
    CHECK(somvr_vh_parse_cal("1,-2,0.5,10,0,-5", &c2) && c2.cal[1] == -2 && c2.cal[5] == -5, "cal parsed");
    CHECK(!somvr_vh_parse_cal("1,2,3", &c2) && !somvr_vh_parse_cal("1,2,3,4,5,6,7", &c2) &&
              !somvr_vh_parse_cal("11,0,0,0,0,0", &c2) && !somvr_vh_parse_cal("0,0,0,95,0,0", &c2) &&
              !somvr_vh_parse_cal(NULL, &c2) && c2.cal[1] == -2,
          "bad cals refused");
}

/* the fist wraps the grip's -Z axis: every long finger's joints at the tube's radius round it, on both sides */
static void test_fist(void) {
    static const float fist[5] = {1, 1, 1, 1, 1};
    float skin[SOMVR_VH_NJ][12];
    somvr_vh_pose(&m, &c, fist, skin);
    for (int f = 1; f < 5; f++) {
        int base = f == 1 ? SOMVR_VHJ_INDEX : f == 2 ? SOMVR_VHJ_MIDDLE : f == 3 ? SOMVR_VHJ_RING : SOMVR_VHJ_PINKY;
        float amin = 9, amax = -9, rmax = 0, rmin = 1;
        for (int k = 1; k <= 4; k++) {
            float p[3];
            somvr_vh_joint_g(&m, (const float(*)[12])skin, base + k, p);
            float r = sqrtf(p[0] * p[0] + p[1] * p[1]), a = atan2f(p[1], p[0]);
            rmax = fmaxf(rmax, r), rmin = fminf(rmin, r), amin = fminf(amin, a), amax = fmaxf(amax, a);
        }
        CHECK(rmin > 0.012f && rmax < 0.04f, "finger %d round the axis: %.3f..%.3f m", f, rmin, rmax);
        CHECK(amax - amin > 2.0f, "finger %d wraps %.0f deg round the axis", f, (amax - amin) * 57.3f);
    }
    /* the index tip closes toward the palm as the curl rises */
    float last = 1e9f;
    for (int k = 0; k <= 4; k++) {
        float cu[5] = {0, 0.25f * (float)k, 0, 0, 0}, tip[3], kn[3];
        somvr_vh_pose(&m, &c, cu, skin);
        somvr_vh_joint_g(&m, (const float(*)[12])skin, SOMVR_VHJ_INDEX + 4, tip);
        somvr_vh_joint_g(&m, (const float(*)[12])skin, SOMVR_VHJ_INDEX + 1, kn);
        float d[3] = {tip[0] - kn[0], tip[1] - kn[1], tip[2] - kn[2]}, l = len3(d);
        CHECK(l < last + 1e-4f, "the index tip closes in (%.3f after %.3f)", l, last);
        last = l;
    }
}

static void test_skin(void) {
    static const float half[5] = {0.3f, 0.5f, 0.5f, 0.6f, 0.7f};
    float skin[SOMVR_VH_NJ][12];
    somvr_vh_pose(&m, &c, half, skin);
    somvr_posef g = {{0, 0, 0, 1}, {0, 0, 0}};
    somvr_vh_skin(&m, (const float(*)[12])skin, SOMVR_HAND_R, &g, va);
    somvr_vh_skin(&m, (const float(*)[12])skin, SOMVR_HAND_L, &g, vb);
    int bad = 0, mirr = 0;
    for (int v = 0; v < somvr_vh_nverts(); v++) {
        bad += !(fabsf(len3(va[v].n) - 1) < 1e-3f) || !isfinite(va[v].p[0] + va[v].p[1] + va[v].p[2]);
        mirr += fabsf(vb[v].p[0] + va[v].p[0]) > 1e-5f || fabsf(vb[v].p[1] - va[v].p[1]) > 1e-5f ||
                fabsf(vb[v].p[2] - va[v].p[2]) > 1e-5f || fabsf(vb[v].n[0] + va[v].n[0]) > 1e-4f ||
                fabsf(vb[v].n[1] - va[v].n[1]) > 1e-4f;
    }
    CHECK(!bad, "%d vertices with a bad normal or position", bad);
    CHECK(!mirr, "%d left vertices not the right ones mirrored (x)", mirr);
    /* a moved, turned grip moves the hand rigidly: the distance between two vertices kept */
    somvr_posef g2 = {{0.2f, -0.3f, 0.1f, 0}, {0.4f, 1.2f, -0.7f}};
    float l = sqrtf(0.04f + 0.09f + 0.01f);
    g2.q[3] = sqrtf(1 - l * l);
    somvr_vh_skin(&m, (const float(*)[12])skin, SOMVR_HAND_R, &g2, vb);
    float d1[3], d2[3];
    for (int i = 0; i < 3; i++) d1[i] = va[10].p[i] - va[900].p[i], d2[i] = vb[10].p[i] - vb[900].p[i];
    CHECK(fabsf(len3(d1) - len3(d2)) < 1e-5f, "rigid: %.5f vs %.5f", len3(d1), len3(d2));
    float o[3], z[3] = {0, 0, 0};
    somvr_vh_grip_point(&g2, SOMVR_HAND_R, z, o);
    CHECK(fabsf(o[0] - 0.4f) < 1e-6f && fabsf(o[2] + 0.7f) < 1e-6f, "the grip's origin");
    /* the triangles: the mirror's reversed */
    const uint16_t *ir = somvr_vh_indices(&m, 0), *il = somvr_vh_indices(&m, 1);
    CHECK(ir[0] == il[0] && ir[1] == il[2] && ir[2] == il[1], "the left hand's winding reversed");
}

/* no pose of the fingers reaches out of the palm farther than palm_reach (the panel's clearance) */
static void test_reach(void) {
    float worst = 0;
    static const float lv[3] = {0, 0.5f, 1};
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++)
            for (int d = 0; d < 3; d++)
                for (int e = 0; e < 3; e++) {
                    float cu[5] = {lv[a], lv[b], lv[d], 0.5f * (lv[d] + lv[e]), lv[e]}, skin[SOMVR_VH_NJ][12];
                    somvr_vh_pose(&m, &c, cu, skin);
                    somvr_posef g = {{0, 0, 0, 1}, {0, 0, 0}};
                    somvr_vh_skin(&m, (const float(*)[12])skin, SOMVR_HAND_R, &g, va);
                    for (int v = 0; v < somvr_vh_nverts(); v++) worst = fmaxf(worst, -va[v].p[0]);
                }
    CHECK(worst <= m.palm_reach_m + 0.002f, "a pose reaches %.4f m out of the palm (the model says %.4f)", worst,
          m.palm_reach_m);
}

static void test_watch(void) {
    somvr_vh_watch_cfg wc;
    somvr_vh_watch_cfg_default(&wc);
    somvr_posef g = {{0, 0, 0, 1}, {0, 0, 0}}, f;
    static const float ex[3] = {1, 0, 0}, ey[3] = {0, 1, 0}, ez[3] = {0, 0, 1};
    for (int h = 0; h < 2; h++) {
        CHECK(somvr_vh_watch_pose(&m, &wc, &g, h, &f), "watch pose %d", h);
        float n[3], up[3], rt[3];
        qrot(f.q, ez, n), qrot(f.q, ey, up), qrot(f.q, ex, rt);
        float s = h == SOMVR_HAND_L ? -1 : 1;
        CHECK(s * n[0] > 0.95f, "hand %d: the face up the back of the wrist: %.2f %.2f %.2f", h, n[0], n[1], n[2]);
        CHECK(up[2] > 0.8f, "hand %d: its top away from the thumb (the grip's +Z): %.2f %.2f %.2f", h, up[0], up[1], up[2]);
        CHECK(s * rt[1] > 0.8f, "hand %d: its right toward the %s: %.2f", h, h ? "elbow" : "fingers", rt[1]);
        float q2 = f.q[0] * f.q[0] + f.q[1] * f.q[1] + f.q[2] * f.q[2] + f.q[3] * f.q[3];
        CHECK(fabsf(q2 - 1) < 1e-4f, "a unit quaternion");
        float d[3] = {f.p[0] - s * m.wrist_g[0], f.p[1] - m.wrist_g[1], f.p[2] - m.wrist_g[2]};
        CHECK(len3(d) < 0.08f && s * d[0] > 0.01f, "hand %d: on the back of the wrist (%.3f %.3f %.3f from the joint)", h,
              d[0], d[1], d[2]);
    }
    /* a turned grip turns the watch with it: the face stays on the wrist's back */
    somvr_posef g2 = {{0, 0.7071068f, 0, 0.7071068f}, {1, 1, 1}}, f2;
    somvr_vh_watch_pose(&m, &wc, &g2, SOMVR_HAND_L, &f2);
    somvr_vh_watch_pose(&m, &wc, &g, SOMVR_HAND_L, &f);
    float a[3], b[3];
    qrot(g2.q, f.p, a);
    for (int i = 0; i < 3; i++) b[i] = f2.p[i] - (a[i] + g2.p[i]);
    CHECK(len3(b) < 1e-5f, "the watch rides the grip (%.6f)", len3(b));
    /* the debounce: no flash at the edge, shown after hold_on facing frames, hidden after hold_off away */
    somvr_vh_face_cfg fc;
    somvr_vh_face_cfg_default(&fc);
    somvr_vh_face st = {0, 0};
    somvr_posef face = {{0, 0, 0, 1}, {0, 0, 0}};
    float eye_on[3] = {0, 0, 1}, eye_off[3] = {1, 0, -0.2f};
    int flashes = 0;
    for (int k = 0; k < 40; k++) flashes += somvr_vh_face_step(&st, &fc, &face, k & 1 ? eye_on : eye_off);
    CHECK(flashes == 0, "alternating frames never show it (%d)", flashes);
    st.run = 0; /* (the alternation left one facing frame counted) */
    int shown_at = -1;
    for (int k = 0; k < 10 && shown_at < 0; k++)
        if (somvr_vh_face_step(&st, &fc, &face, eye_on)) shown_at = k;
    CHECK(shown_at == fc.hold_on - 1, "shown after %d facing frames (at %d)", fc.hold_on, shown_at);
    int hidden_at = -1;
    for (int k = 0; k < 20 && hidden_at < 0; k++) {
        float *e = k % 3 == 2 ? eye_on : eye_off; /* a facing frame now and then restarts the count */
        if (!somvr_vh_face_step(&st, &fc, &face, e)) hidden_at = k;
    }
    CHECK(hidden_at < 0, "a facing frame every third keeps it (hidden at %d)", hidden_at);
    st.run = 0;
    for (int k = 0; k < 20 && hidden_at < 0; k++)
        if (!somvr_vh_face_step(&st, &fc, &face, eye_off)) hidden_at = k;
    CHECK(hidden_at == fc.hold_off - 1, "hidden after %d frames away (at %d)", fc.hold_off, hidden_at);
}

/* the watch read as a player reads one: the left forearm across the chest (the elbow left, the fingers right), the
 * back of the wrist up and tilted 40 deg toward the eyes (SOMVR_XR_TEST's `hand L head -5 -22 30 -140 0 90`): its face
 * toward the eyes, its text's right to the viewer's right, its top away from the viewer and up */
static void qaxis4(int ax, float deg, float q[4]) {
    float a = deg * 0.00872665f;
    q[0] = q[1] = q[2] = 0, q[ax] = sinf(a), q[3] = cosf(a);
}
static void qmul4(const float a[4], const float b[4], float o[4]) {
    float r[4] = {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
                  a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
    memcpy(o, r, sizeof r);
}
static void test_watch_read(void) {
    float qx[4], qy[4], qz[4], t[4];
    qaxis4(0, -140, qx), qaxis4(1, 0, qy), qaxis4(2, 90, qz);
    qmul4(qx, qy, t);
    somvr_posef g = {{0, 0, 0, 1}, {-0.05f, -0.22f, -0.30f}}, f; /* the eyes at the origin, looking down -Z */
    qmul4(t, qz, g.q);
    somvr_vh_watch_cfg wc;
    somvr_vh_watch_cfg_default(&wc);
    CHECK(somvr_vh_watch_pose(&m, &wc, &g, SOMVR_HAND_L, &f), "the reading pose's watch");
    static const float ex[3] = {1, 0, 0}, ey[3] = {0, 1, 0}, ez[3] = {0, 0, 1};
    float x[3], y[3], z[3], toeye[3] = {-f.p[0], -f.p[1], -f.p[2]};
    qrot(f.q, ex, x), qrot(f.q, ey, y), qrot(f.q, ez, z);
    float l = len3(toeye), k = (z[0] * toeye[0] + z[1] * toeye[1] + z[2] * toeye[2]) / l;
    CHECK(k > 0.8f, "its face toward the eyes (cos %.2f)", k);
    CHECK(x[0] > 0.8f, "its text's right to the viewer's right (%.2f %.2f %.2f)", x[0], x[1], x[2]);
    CHECK(y[1] - y[2] > 0.8f, "its top up and away (%.2f %.2f %.2f)", y[0], y[1], y[2]);
    float arm[3], wr[3];
    somvr_vh_grip_point(&g, SOMVR_HAND_L, m.wrist_g, wr);
    for (int i = 0; i < 3; i++) arm[i] = f.p[i] - wr[i];
    CHECK(arm[0] < 0, "toward the elbow (left) from the wrist (%.3f)", arm[0]);
}

/* touch: the grip made for a point puts the drawn tip there, the index along the direction */
static void test_touch(void) {
    float pt[3] = {0.1f, 1.2f, -0.4f}, dir[3] = {0, -0.3f, -0.954f}, up[3] = {0, 1, 0};
    for (int h = 0; h < 2; h++) {
        somvr_posef g;
        CHECK(somvr_vh_touch_grip(&m, pt, dir, up, h, &g), "touch grip %d", h);
        float tip[3], d[3];
        somvr_vh_tip(&m, &g, h, tip);
        for (int i = 0; i < 3; i++) d[i] = tip[i] - pt[i];
        CHECK(len3(d) < 1e-5f, "hand %d: the tip on the point (%.6f)", h, len3(d));
        float a[3] = {(h ? 1 : -1) * m.tipdir_g[0], m.tipdir_g[1], m.tipdir_g[2]}, w[3];
        qrot(g.q, a, w);
        float k = (w[0] * dir[0] + w[1] * dir[1] + w[2] * dir[2]) / len3(dir);
        CHECK(k > 0.999f, "hand %d: the index along the direction (%.4f)", h, k);
    }
    float bad[3] = {0, 1, 0};
    somvr_posef g;
    CHECK(!somvr_vh_touch_grip(&m, pt, bad, up, 1, &g), "a direction along up has no frame");
}

static somvr_vrhands_shared sh;
static void test_abi(void) {
    somvr_vrhands_frame e, o;
    memset(&e, 0, sizeof e);
    e.flags = SOMVR_VHF_L | SOMVR_VHF_HIDE, e.time = 1234, e.grip[0].p[1] = -0.3f, e.curl[1][4] = 0.75f;
    e.depth_k = 0.06f; /* v2 */
    e.ghost[1].p[2] = -0.45f, e.ghost[1].q[3] = 1, e.ghost_a[1] = 0.3f; /* v3 */
    e.arrow.p[0] = 0.12f, e.arrow.q[3] = 1, e.arrow_len = 0.72f, e.arrow_glow = 0.6f; /* v4 */
    CHECK(!somvr_vrhands_ok(&sh), "inactive");
    sh.magic = SOMVR_VRHANDS_MAGIC, sh.version = SOMVR_VRHANDS_VERSION, sh.size = sizeof sh;
    atomic_store(&sh.active, 1);
    CHECK(somvr_vrhands_ok(&sh), "active");
    somvr_vrhands_put(&sh, 77, &e);
    CHECK(somvr_vrhands_get(&sh, 77, &o) && o.flags == e.flags && o.time == 1234 && o.grip[0].p[1] == -0.3f &&
              o.curl[1][4] == 0.75f && o.depth_k == 0.06f && atomic_load(&o.seq) == 77 && o.ghost[1].p[2] == -0.45f &&
              o.ghost[1].q[3] == 1 && o.ghost_a[1] == 0.3f && o.ghost_a[0] == 0 && o.arrow.p[0] == 0.12f &&
              o.arrow.q[3] == 1 && o.arrow_len == 0.72f && o.arrow_glow == 0.6f,
          "round trip (v2: depth_k, v3: the ghost, v4: the arrow)");
    CHECK(!somvr_vrhands_get(&sh, 77 + SOMVR_NREC, &o) && !somvr_vrhands_get(&sh, 0, &o), "another frame's slot");
    somvr_vrhands_put(&sh, 77 + SOMVR_NREC, &e);
    CHECK(!somvr_vrhands_get(&sh, 77, &o), "overwritten");
    /* v2: depth_k set after the put (the eyes' H1), only into the slot of that frame */
    e.depth_k = 0;
    somvr_vrhands_put(&sh, 90, &e);
    somvr_vrhands_set_depth_k(&sh, 90, 0.052f);
    somvr_vrhands_set_depth_k(&sh, 90 + SOMVR_NREC, 0.9f); /* another frame's: nothing */
    CHECK(somvr_vrhands_get(&sh, 90, &o) && o.depth_k == 0.052f && o.flags == e.flags && o.curl[1][4] == 0.75f,
          "depth_k set after the put (%.3f)", o.depth_k);
    /* v2: the depth textures' info round trip, the frames they hold */
    somvr_eyes_info in = {3, 2015, 2217, 19, {0x1111, 0x2222}}, out;
    somvr_vrhands_depth_publish(&sh, &in);
    CHECK(somvr_vrhands_depth_read(&sh, &out) && !memcmp(&in, &out, sizeof in) && (atomic_load(&sh.dseq) & 1) == 0,
          "the depth info round trip");
    CHECK(sh.version == 4 && offsetof(somvr_vrhands_shared, dinfo) == offsetof(somvr_vrhands_shared, dseq) + 8 &&
              offsetof(somvr_vrhands_frame, ghost) == 120 && offsetof(somvr_vrhands_frame, ghost_a) == 176 &&
              offsetof(somvr_vrhands_frame, arrow) == 184 && offsetof(somvr_vrhands_frame, arrow_glow) == 216,
          "v4 layout (v3 + the arrow)");
}

/* build 16: the arrow's mesh (vrhands v4): along the pose's -Z from its nock end, its length, both windings */
static void test_arrow(void) {
    static somvr_vh_vert v[SOMVR_VH_ARROW_NV];
    const uint16_t *ix = somvr_vh_arrow_indices();
    int bad = 0;
    for (int i = 0; i < SOMVR_VH_ARROW_NI; i++) bad += ix[i] >= SOMVR_VH_ARROW_NV;
    int half = SOMVR_VH_ARROW_NI / 2, rev = 1;
    for (int i = 0; i < half; i += 3) rev &= ix[half + i] == ix[i] && ix[half + i + 1] == ix[i + 2] && ix[half + i + 2] == ix[i + 1];
    CHECK(!bad && rev, "arrow indices: in range (%d bad), the second half reversed (%d)", bad, rev);
    /* turned 90 deg about +Y: its -Z along -X; at (1, 1.5, -2) */
    somvr_posef p = {{0, 0.70710678f, 0, 0.70710678f}, {1, 1.5f, -2}};
    somvr_vh_arrow(&p, 0.72f, v);
    float mn[3] = {1e9f, 1e9f, 1e9f}, mx[3] = {-1e9f, -1e9f, -1e9f};
    int unit = 1;
    for (int i = 0; i < SOMVR_VH_ARROW_NV; i++) {
        for (int k = 0; k < 3; k++) mn[k] = fminf(mn[k], v[i].p[k]), mx[k] = fmaxf(mx[k], v[i].p[k]);
        float l = sqrtf(v[i].n[0] * v[i].n[0] + v[i].n[1] * v[i].n[1] + v[i].n[2] * v[i].n[2]);
        unit &= fabsf(l - 1) < 1e-4f && v[i].a == 1;
    }
    CHECK(fabsf(mx[0] - 1.002f) < 1e-3f && fabsf(mn[0] - (1 - 0.72f)) < 1e-4f && mx[1] - mn[1] < 0.05f &&
              mx[2] - mn[2] < 0.05f && unit,
          "arrow: from the nock along -X 0.72 m (x %.3f..%.3f), thin (y %.3f z %.3f), unit normals %d", mn[0], mx[0],
          mx[1] - mn[1], mx[2] - mn[2], unit);
    somvr_vh_arrow(&p, NAN, v);
    float m0 = 1e9f;
    for (int i = 0; i < SOMVR_VH_ARROW_NV; i++) m0 = fminf(m0, v[i].p[0]);
    CHECK(fabsf(m0 - 0.8f) < 1e-4f, "arrow: a NaN length reads 0.2 m (%.3f)", m0);
    /* the fade near the eyes: the eyes 5 cm behind the nock end (0.72 m along -X from x 1): the nock clear, the head opaque */
    somvr_vh_arrow(&p, 0.72f, v);
    float e0[3] = {1.05f, 1.5f, -2.03f}, e1[3] = {1.05f, 1.5f, -1.97f};
    somvr_vh_arrow_fade(v, SOMVR_VH_ARROW_NV, e0, e1, 0.06f, 0.16f);
    float a_nock = v[24].a, a_head = v[33].a; /* the cap's centre, the head's tip */
    CHECK(a_nock == 0 && a_head == 1, "arrow fade: nock %.2f head %.2f", a_nock, a_head);
}

/* build 11: the depth switch's words and the world's fade (the shader's rule) */
static void test_depth(void) {
    CHECK(somvr_vh_depth_word(NULL) == SOMVR_VH_DEPTH_DEFAULT && somvr_vh_depth_word("") == SOMVR_VH_DEPTH_DEFAULT &&
              somvr_vh_depth_word("world") == 1 && somvr_vh_depth_word("on") == 1 && somvr_vh_depth_word("off") == 0 &&
              somvr_vh_depth_word("0") == 0 && somvr_vh_depth_word("walls") == -1,
          "SOMVR_XR_HANDS_DEPTH's words");
    const float soft = 0.015f, bias = 0.0075f, zw = 0.40f;
    CHECK(somvr_vh_depth_fade(0.30f, zw, soft, bias) == 1.0f && somvr_vh_depth_fade(zw, zw, soft, bias) == 1.0f,
          "in front of the surface and on it: drawn");
    CHECK(somvr_vh_depth_fade(zw + 0.015f, zw, soft, bias) == 0.0f && somvr_vh_depth_fade(1.0f, zw, soft, bias) == 0.0f,
          "1.5 cm into it and beyond: hidden");
    float mid = somvr_vh_depth_fade(zw + 0.0075f, zw, soft, bias);
    CHECK(fabsf(mid - 0.5f) < 1e-4f, "the edge's middle 0.75 cm behind the surface: half (%.4f)", mid);
    float prev = 2;
    int mono = 1;
    for (int i = 0; i <= 40; i++) {
        float f = somvr_vh_depth_fade(zw - 0.005f + 0.0005f * (float)i, zw, soft, bias);
        mono &= f <= prev + 1e-6f, prev = f;
    }
    CHECK(mono, "the fade only falls with the depth");
    /* depth_k: reversed Z with an infinite far plane, depth = near / z; 6 cm near at 100 % and 115 % (the world 1/s) */
    float near_cm = 6, k1 = near_cm / 100.0f, k115 = near_cm / (100.0f * 1.15f);
    float z_cm = 75, d = near_cm / z_cm;
    CHECK(fabsf(k1 / d - 0.75f) < 1e-5f && fabsf(k115 / d - 0.75f / 1.15f) < 1e-5f,
          "depth_k / depth = the view distance in XR metres (%.4f, %.4f)", k1 / d, k115 / d);
}

int main(void) {
    test_model();
    test_fist();
    test_skin();
    test_reach();
    test_watch();
    test_watch_read();
    test_touch();
    test_abi();
    test_arrow();
    test_depth();
    printf("test_vrhands: %d checks, %d failures\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
