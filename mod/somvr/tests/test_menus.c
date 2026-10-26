/* test_menus.c - unit tests of somvr_menus.c (build 6, docs/re-xr-pipeline.md 28): the switches, the menu board's pose,
 * the wrist minimap's rect, copy regions, watch pose and facing, the fingertip's state machine (hover, press, release,
 * the guards against accidental presses, the smoothing) */
#include "../somvr_menus.h"

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
#define NEAR(a, b, e) (fabs((double)(a) - (double)(b)) <= (e))

static void qrot(const float q[4], const float v[3], float o[3]) {
    float t[3] = {2 * (q[1] * v[2] - q[2] * v[1]), 2 * (q[2] * v[0] - q[0] * v[2]), 2 * (q[0] * v[1] - q[1] * v[0])};
    o[0] = v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]);
    o[1] = v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]);
    o[2] = v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]);
}
static void qmul(const float a[4], const float b[4], float o[4]) {
    o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}
static void axis_q(float ax, float ay, float az, float deg, float q[4]) {
    float h = 0.5f * deg * 3.14159265f / 180.0f, s = sinf(h);
    q[0] = ax * s, q[1] = ay * s, q[2] = az * s, q[3] = cosf(h);
}
static float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static const float EX[3] = {1, 0, 0}, EY[3] = {0, 1, 0}, EZ[3] = {0, 0, 1};

static void test_switches(void) {
    int bad = 0;
    CHECK(somvr_mn_parse(NULL, &bad) == SOMVR_MN_OFF && somvr_mn_parse("", &bad) == SOMVR_MN_OFF &&
              somvr_mn_parse("front", &bad) == SOMVR_MN_FRONT && somvr_mn_parse("on", &bad) == SOMVR_MN_FRONT &&
              somvr_mn_parse("1", &bad) == SOMVR_MN_FRONT && somvr_mn_parse("off", &bad) == SOMVR_MN_OFF && !bad,
          "SOMVR_XR_MENU words");
    CHECK(somvr_mn_parse("yes", &bad) == SOMVR_MN_OFF && bad, "an unknown word: off, bad");
    float q[3];
    bad = 0;
    somvr_mn_parse_quad(NULL, q, &bad);
    CHECK(q[0] == SOMVR_MN_DIST_M && q[1] == SOMVR_MN_WIDTH_M && q[2] == SOMVR_MN_DROP_M && !bad, "the board's defaults");
    somvr_mn_parse_quad("0.9,1.2,0.05", q, &bad);
    CHECK(NEAR(q[0], 0.9, 1e-6) && NEAR(q[1], 1.2, 1e-6) && NEAR(q[2], 0.05, 1e-6) && !bad, "0.9,1.2,0.05");
    const char *badq[] = {"0.9,1.2", "0.1,1,0", "0.9,1.2,0.05,1", "a,b,c", "0.9;1.2;0.05", "0.9,5,0", "0.9,1,-2"};
    for (int i = 0; i < 7; i++) {
        bad = 0;
        somvr_mn_parse_quad(badq[i], q, &bad);
        CHECK(bad && q[0] == SOMVR_MN_DIST_M && q[1] == SOMVR_MN_WIDTH_M, "bad quad %s", badq[i]);
    }
    /* the defaults: 60 deg or more wide at a fingertip's reach, 18-pixel text at least 0.55 deg */
    float wdeg = 2 * atanf(0.5f * SOMVR_MN_WIDTH_M / SOMVR_MN_DIST_M) * 57.29578f;
    float tdeg = 18.0f / 1080 * 2 * atanf(0.5f * SOMVR_MN_WIDTH_M * 9 / 16 / SOMVR_MN_DIST_M) * 57.29578f;
    CHECK(wdeg >= 58 && wdeg <= 70 && tdeg >= 0.55f && SOMVR_MN_DIST_M <= 0.65f, "the board: %.1f deg wide, text %.2f deg",
          wdeg, tdeg);
    bad = 0;
    CHECK(somvr_mm_parse("wrist", &bad) == SOMVR_MM_WRIST && somvr_mm_parse(NULL, &bad) == SOMVR_MM_OFF && !bad &&
              somvr_mm_parse("hand", &bad) == SOMVR_MM_OFF && bad,
          "SOMVR_XR_MINIMAP words");
    int r[4];
    bad = 0;
    somvr_mm_parse_rect(NULL, r, &bad);
    static const int def[4] = SOMVR_MM_RECT_DEFAULT;
    CHECK(!memcmp(r, def, sizeof def) && !bad, "the rect's default");
    somvr_mm_parse_rect("10,700,350,380", r, &bad);
    CHECK(r[0] == 10 && r[1] == 700 && r[2] == 350 && r[3] == 380 && !bad, "a rect");
    somvr_mm_parse_rect("10,900,350,380", r, &bad);
    CHECK(bad && !memcmp(r, def, sizeof def), "a rect beyond 1080: the default");
    somvr_mm_cfg c;
    somvr_mm_cfg_default(&c);
    bad = 0;
    somvr_mm_parse_quad("0.12,0.07,0.05", &c, &bad);
    CHECK(NEAR(c.size_m, 0.12, 1e-6) && NEAR(c.back_m, 0.07, 1e-6) && NEAR(c.out_m, 0.05, 1e-6) && !bad, "the watch's quad");
    somvr_mm_parse_quad("1,0,0", &c, &bad);
    CHECK(bad && NEAR(c.size_m, 0.12, 1e-6), "a 1 m watch: refused, kept");
    bad = 0;
    CHECK(somvr_tc_parse("on", &bad) == SOMVR_TC_ON && somvr_tc_parse("0", &bad) == SOMVR_TC_OFF && !bad &&
              somvr_tc_parse("poke", &bad) == SOMVR_TC_OFF && bad,
          "SOMVR_XR_TOUCH words");
}

/* the board: dist ahead at the head's yaw, drop below, facing the eyes */
static void board_check(const somvr_posef eye[2], float yaw_deg, const char *what) {
    somvr_posef b;
    somvr_mn_board_pose(eye, 0.6f, 0.1f, &b);
    float mid[3], ahead[3] = {-sinf(yaw_deg * 3.14159265f / 180), 0, -cosf(yaw_deg * 3.14159265f / 180)};
    for (int i = 0; i < 3; i++) mid[i] = 0.5f * (eye[0].p[i] + eye[1].p[i]);
    float n[3], up[3], x[3], to[3];
    qrot(b.q, EZ, n), qrot(b.q, EY, up), qrot(b.q, EX, x);
    for (int i = 0; i < 3; i++) to[i] = mid[i] - b.p[i];
    float tl = sqrtf(dot3(to, to));
    for (int i = 0; i < 3; i++) to[i] /= tl;
    CHECK(NEAR(b.p[0], mid[0] + 0.6f * ahead[0], 1e-4) && NEAR(b.p[1], mid[1] - 0.1f, 1e-4) &&
              NEAR(b.p[2], mid[2] + 0.6f * ahead[2], 1e-4),
          "%s: the centre %.3f,%.3f,%.3f", what, b.p[0], b.p[1], b.p[2]);
    CHECK(dot3(n, to) > 0.9999f && fabsf(x[1]) < 1e-4f && up[1] > 0.98f, "%s: faces the eyes (%.4f), level x %.4f, up %.3f",
          what, dot3(n, to), x[1], up[1]);
}
static void test_board(void) {
    somvr_posef eye[2] = {{{0, 0, 0, 1}, {-0.032f, 1.6f, 0}}, {{0, 0, 0, 1}, {0.032f, 1.6f, 0}}};
    board_check(eye, 0, "ahead");
    float q[4];
    axis_q(0, 1, 0, 90, q); /* turned left */
    memcpy(eye[0].q, q, sizeof q), memcpy(eye[1].q, q, sizeof q);
    board_check(eye, 90, "turned left");
    axis_q(0, 1, 0, 180, q);
    memcpy(eye[0].q, q, sizeof q), memcpy(eye[1].q, q, sizeof q);
    eye[0].p[2] = eye[1].p[2] = 2.0f; /* behind the origin, turned around (session 2's case) */
    board_check(eye, 180, "turned around, 2 m behind the origin");
    float yaw[4], pitch[4];
    axis_q(0, 1, 0, -30, yaw), axis_q(1, 0, 0, -50, pitch); /* looking down 50 deg, 30 deg right */
    qmul(yaw, pitch, q);
    memcpy(eye[0].q, q, sizeof q), memcpy(eye[1].q, q, sizeof q);
    board_check(eye, -30, "looking down, right");
    axis_q(1, 0, 0, -90, pitch); /* straight down: the head's up decides (ahead) */
    memcpy(eye[0].q, pitch, sizeof pitch), memcpy(eye[1].q, pitch, sizeof pitch);
    eye[0].p[2] = eye[1].p[2] = 0;
    board_check(eye, 0, "straight down");
    float nanq[4] = {NAN, 0, 0, 1};
    memcpy(eye[0].q, nanq, sizeof nanq);
    somvr_posef b;
    somvr_mn_board_pose(eye, 0.6f, 0.1f, &b);
    CHECK(NEAR(b.p[2], -0.6, 1e-4) && b.q[3] == b.q[3], "a NaN head: straight ahead (%.3f)", b.p[2]);
}

/* somvr_hud_split's regions for layout l: every pixel of the w x h image outside the tiles copied in place exactly once,
 * every tile's pixel to its place exactly once, nothing else written (the extra area's gaps stay clear), no region
 * empty or outside; returns the count (-1 = wrong) */
static int split_check(uint32_t w, uint32_t h, const somvr_hud_layout *l, int cap) {
    enum { MW = 640, MH = 220 };
    static int src_of[MH][MW]; /* the source pixel copied there (y * MW + x), -1 none, -2 twice */
    int32_t g[SOMVR_HUD_SPLIT_MAX][6];
    int n = somvr_hud_split(w, h, l, g, cap);
    if (n <= 0 || w + l->extra_w > MW || h > MH) return -1;
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < MW; x++) src_of[y][x] = -1;
    for (int i = 0; i < n; i++) {
        if (g[i][4] <= 0 || g[i][5] <= 0 || g[i][0] < 0 || g[i][1] < 0 || g[i][0] + g[i][4] > (int)w ||
            g[i][1] + g[i][5] > (int)h || g[i][2] + g[i][4] > (int)(w + l->extra_w) || g[i][3] + g[i][5] > (int)h)
            return -1;
        for (int y = 0; y < g[i][5]; y++)
            for (int x = 0; x < g[i][4]; x++) {
                int *d = &src_of[g[i][3] + y][g[i][2] + x];
                *d = *d == -1 ? (g[i][1] + y) * MW + g[i][0] + x : -2;
            }
    }
    for (int y = 0; y < (int)h; y++)
        for (int x = 0; x < (int)(w + l->extra_w); x++) {
            int want = -1; /* what belongs here */
            if (x < (int)w) {
                int in = 0;
                for (int t = 0; t < l->ntile + l->nclear; t++) {
                    const int *q = t < l->ntile ? l->src[t] : l->clear[t - l->ntile];
                    in |= x >= q[0] && x < q[0] + q[2] && y >= q[1] && y < q[1] + q[3];
                }
                if (!in) want = y * MW + x;
            }
            for (int t = 0; t < l->ntile; t++)
                if (x >= l->dst[t][0] && x < l->dst[t][0] + l->src[t][2] && y >= l->dst[t][1] && y < l->dst[t][1] + l->src[t][3])
                    want = (l->src[t][1] + y - l->dst[t][1]) * MW + l->src[t][0] + x - l->dst[t][0];
            if (src_of[y][x] != want) return -1;
        }
    return n;
}

static void test_minimap(void) {
    static const int def[4] = SOMVR_MM_RECT_DEFAULT;
    int r[4];
    CHECK(somvr_mm_rect_for(def, 1920, 1080, r) && !memcmp(r, def, sizeof def), "1080p: as given");
    CHECK(somvr_mm_rect_for(def, 1280, 720, r) && r[0] == 19 && r[1] == 513 && r[2] == 200 && r[3] == 207,
          "720p: %d,%d %dx%d", r[0], r[1], r[2], r[3]);
    CHECK(!somvr_mm_rect_for(def, 8, 8, r), "a tiny image: none");
    /* build 6's case through somvr_hud_split: every pixel but the minimap once in place, the minimap into the column */
    int mm[4] = {3, 70, 30, 31};
    somvr_hud_layout l;
    CHECK(somvr_hud_layout_make(192, 108, mm, NULL, 0, NULL, NULL, &l) && l.ntile == 1 && l.extra_w == 30 &&
              l.dst[0][0] == 192 && l.dst[0][1] == 0,
          "the minimap alone: %d tiles, extra %u, at %d,%d", l.ntile, l.extra_w, l.dst[0][0], l.dst[0][1]);
    CHECK(split_check(192, 108, &l, 5) == 5, "the minimap alone: 4 rects and its copy");
    int corner[4] = {0, 108 - 31, 30, 31}; /* the default case: the bottom-left corner: only above and right */
    somvr_hud_layout_make(192, 108, corner, NULL, 0, NULL, NULL, &l);
    CHECK(split_check(192, 108, &l, 5) == 3, "the corner minimap: 2 rects and the column");
    /* the watch: the left hand held level, fingers ahead (-Z), palm to the right (+X) */
    somvr_mm_cfg c;
    somvr_mm_cfg_default(&c);
    somvr_posef grip = {{0, 0, 0, 1}, {-0.2f, 1.1f, -0.3f}}, f;
    CHECK(somvr_mm_pose(&grip, SOMVR_HAND_L, &c, &f), "the left watch");
    float n3[3], x3[3], y3[3];
    qrot(f.q, EZ, n3), qrot(f.q, EX, x3), qrot(f.q, EY, y3);
    CHECK(NEAR(n3[0], -1, 1e-4) && NEAR(x3[2], -1, 1e-4) && NEAR(y3[1], -1, 1e-4), "left: out of the back of the hand "
          "(%.2f), right edge to the fingers (%.2f), top away from the thumb (%.2f)", n3[0], x3[2], y3[1]);
    CHECK(NEAR(f.p[0], -0.2 - c.out_m, 1e-4) && NEAR(f.p[1], 1.1, 1e-4) && NEAR(f.p[2], -0.3 + c.back_m, 1e-4),
          "left: behind the grip, off the back of the hand (%.3f,%.3f,%.3f)", f.p[0], f.p[1], f.p[2]);
    CHECK(somvr_mm_pose(&grip, SOMVR_HAND_R, &c, &f), "the right watch");
    qrot(f.q, EZ, n3), qrot(f.q, EX, x3), qrot(f.q, EY, y3);
    CHECK(NEAR(n3[0], 1, 1e-4) && NEAR(x3[2], 1, 1e-4) && NEAR(y3[1], -1, 1e-4), "right: mirrored (%.2f %.2f %.2f)", n3[0],
          x3[2], y3[1]);
    /* looked at like a watch: the left forearm across the chest (fingers to the right, palm down) in front of the
     * eyes: the face up toward them, its top away from the body, its right edge to the right */
    float yawq[4], rollq[4], q[4];
    axis_q(0, 1, 0, -90, yawq);  /* fingers (-Z) to the right (+X) */
    axis_q(0, 0, 1, -90, rollq); /* palm (+X) down */
    qmul(yawq, rollq, q);
    somvr_posef g2 = {{q[0], q[1], q[2], q[3]}, {-0.05f, 1.25f, -0.35f}};
    somvr_mm_pose(&g2, SOMVR_HAND_L, &c, &f);
    qrot(f.q, EZ, n3), qrot(f.q, EX, x3), qrot(f.q, EY, y3);
    float eye[3] = {0, 1.6f, 0};
    CHECK(n3[1] > 0.99f && x3[0] > 0.99f && y3[2] < -0.99f, "the watch check: face up %.2f, right edge right %.2f, top "
          "away %.2f", n3[1], x3[0], y3[2]);
    CHECK(somvr_mm_facing(&f, eye, &c, 0), "seen from the eyes");
    somvr_mm_pose(&grip, SOMVR_HAND_L, &c, &f); /* the hand held level: the face to the left, the eyes above-right */
    float eye2[3] = {0.2f, 1.6f, 0.2f};
    CHECK(!somvr_mm_facing(&f, eye2, &c, 0) && !somvr_mm_facing(&f, eye2, &c, 1), "held level, seen from the right: away");
    /* the hysteresis: 60 deg on (cos 0.5 > 0.4); 70 deg (0.34) stays on only if it was on */
    somvr_posef up = {{0, 0, 0, 1}, {0, 0, 0}};
    float e60[3] = {sinf(60 * 3.14159265f / 180), 0, cosf(60 * 3.14159265f / 180)};
    float e70[3] = {sinf(70 * 3.14159265f / 180), 0, cosf(70 * 3.14159265f / 180)};
    float e85[3] = {sinf(85 * 3.14159265f / 180), 0, cosf(85 * 3.14159265f / 180)};
    CHECK(somvr_mm_facing(&up, e60, &c, 0) && !somvr_mm_facing(&up, e70, &c, 0) && somvr_mm_facing(&up, e70, &c, 1) &&
              !somvr_mm_facing(&up, e85, &c, 1),
          "facing: on at 60, 70 only if shown, off at 85 deg");
    somvr_posef badg = {{NAN, 0, 0, 1}, {0, 0, 0}};
    CHECK(!somvr_mm_pose(&badg, SOMVR_HAND_L, &c, &f), "a NaN grip: no watch");
}

/* ---- touch ---- */
static somvr_tc_cfg TC;
static somvr_tc_state TS;
static somvr_tc_out TO;
static double TT;
static const somvr_posef QP = {{0, 0, 0, 1}, {0, 1.5f, -0.6f}}; /* a board facing +Z (the eyes at z 0) */
static const float QS[2] = {0.72f, 0.405f};
/* one frame at 90 Hz: hand h's tip at uv (u, v), depth d (m in front) */
static uint32_t tc_frame(int h, float u, float v, float d, int ok) {
    somvr_tc_in in;
    memset(&in, 0, sizeof in);
    TT += 1.0 / 90;
    in.t = TT;
    in.ok[h] = ok;
    in.tip[h][0] = QP.p[0] + (u - 0.5f) * QS[0], in.tip[h][1] = QP.p[1] + (0.5f - v) * QS[1], in.tip[h][2] = QP.p[2] + d;
    in.quad = 7;
    in.quad_pose = QP;
    in.quad_size[0] = QS[0], in.quad_size[1] = QS[1];
    somvr_tc_update(&TS, &TC, &in, &TO);
    return TO.edges;
}
/* the finger from `from` to `to` (depth, m) at `mps`, at uv; the edges OR'ed */
static uint32_t tc_move(int h, float u, float v, float from, float to, float mps) {
    uint32_t e = 0;
    float step = mps / 90 * (to < from ? -1 : 1);
    for (float d = from; (step < 0 ? d > to : d < to); d += step) e |= tc_frame(h, u, v, d, 1);
    e |= tc_frame(h, u, v, to, 1);
    return e;
}
static void tc_start(void) {
    somvr_tc_cfg_default(&TC);
    somvr_tc_reset(&TS);
    TT = 100;
}
static void test_touch(void) {
    tc_start();
    float tip[3];
    somvr_posef aim = {{0, 0, 0, 1}, {0.1f, 1.2f, -0.3f}};
    somvr_tc_tip(&aim, 0.05f, tip);
    CHECK(NEAR(tip[0], 0.1, 1e-6) && NEAR(tip[2], -0.35, 1e-6), "the tip: 5 cm along the aim's -Z");
    /* a poke: from 8 cm to 6 mm through at 0.3 m/s, then back */
    uint32_t e = tc_move(1, 0.3f, 0.6f, 0.08f, 0.02f, 0.3f);
    CHECK((e & SOMVR_TCE_HOVER) && !(e & SOMVR_TCE_PRESS) && TO.on && TO.hand == 1 && NEAR(TO.uv[0], 0.3, 0.01) &&
              NEAR(TO.uv[1], 0.6, 0.01),
          "hover in front (edges 0x%x, uv %.3f,%.3f)", e, TO.uv[0], TO.uv[1]);
    e = tc_move(1, 0.3f, 0.6f, 0.02f, -0.007f, 0.3f);
    CHECK((e & SOMVR_TCE_PRESS) && TO.down && TS.downs == 1, "the press through the surface (0x%x)", e);
    e = tc_move(1, 0.3f, 0.6f, -0.007f, -0.03f, 0.1f);
    CHECK(!(e & (SOMVR_TCE_PRESS | SOMVR_TCE_RELEASE)) && TO.down && TS.downs == 1, "deeper: still one press, held");
    e = tc_move(1, 0.35f, 0.6f, -0.03f, 0.0f, 0.3f); /* a drag back up */
    CHECK(TO.down && TO.uv[0] > 0.31f, "a drag follows (uv %.3f)", TO.uv[0]);
    e = tc_move(1, 0.35f, 0.6f, 0.0f, 0.05f, 0.3f);
    CHECK((e & SOMVR_TCE_RELEASE) && !TO.down && TS.ups == 1, "the release at 1.5 cm (0x%x)", e);
    /* a second poke right away: the refractory (0.25 s) and the front approach (2 cm) */
    e = tc_move(1, 0.35f, 0.6f, 0.05f, -0.007f, 0.6f);
    CHECK(!(e & SOMVR_TCE_PRESS) && TS.downs == 1, "within the refractory: no press");
    for (int i = 0; i < 30; i++) tc_frame(1, 0.35f, 0.6f, 0.05f, 1);
    e = tc_move(1, 0.35f, 0.6f, 0.05f, -0.007f, 0.3f);
    CHECK((e & SOMVR_TCE_PRESS) && TS.downs == 2, "after it: the second press");
    tc_move(1, 0.35f, 0.6f, -0.007f, 0.06f, 0.3f);
    /* a slow push: no press; a swipe through: no press */
    for (int i = 0; i < 40; i++) tc_frame(1, 0.5f, 0.5f, 0.06f, 1);
    tc_move(1, 0.5f, 0.5f, 0.06f, 0.02f, 0.3f);
    e = tc_move(1, 0.5f, 0.5f, 0.02f, -0.01f, 0.02f);
    CHECK(!(e & SOMVR_TCE_PRESS) && TS.downs == 2, "a slow push (2 cm/s): no press");
    tc_move(1, 0.5f, 0.5f, -0.01f, 0.06f, 0.3f);
    for (int i = 0; i < 40; i++) tc_frame(1, 0.5f, 0.5f, 0.06f, 1);
    tc_move(1, 0.2f, 0.5f, 0.06f, 0.015f, 0.3f);
    e = 0;
    for (int i = 0; i < 12; i++) e |= tc_frame(1, 0.2f + 0.05f * (float)i, 0.5f, 0.015f - 0.002f * (float)i, 1);
    CHECK(!(e & SOMVR_TCE_PRESS) && TS.downs == 2, "a swipe across (1.6 m/s): no press (0x%x)", e);
    /* ... the finger left resting 1 cm behind the surface after the swipe, then pushed on (run 162428: it changed a
     * setting): no press - a press must cross the surface within window_s */
    for (int i = 0; i < 90; i++) tc_frame(1, 0.75f, 0.5f, -0.01f, 1);
    e = tc_move(1, 0.7f, 0.55f, -0.01f, -0.04f, 0.1f);
    CHECK(!(e & SOMVR_TCE_PRESS) && TS.downs == 2, "resting behind, then pushed deeper: no press (0x%x)", e);
    tc_move(1, 0.8f, 0.5f, -0.01f, 0.06f, 0.3f);
    /* no front approach: the finger appears behind the surface and comes forward: nothing */
    tc_start();
    for (int i = 0; i < 5; i++) tc_frame(1, 0.5f, 0.5f, -0.05f, 1);
    e = tc_move(1, 0.5f, 0.5f, -0.05f, 0.01f, 0.3f);
    CHECK(!(e & (SOMVR_TCE_HOVER | SOMVR_TCE_PRESS)) && !TO.on, "from behind: no hover, no press (0x%x)", e);
    /* the dwell: a fast jab from 2.5 cm (hover for less than 60 ms) */
    tc_start();
    for (int i = 0; i < 5; i++) tc_frame(0, 0.5f, 0.5f, 0.1f, 1);
    e = 0;
    float ds[] = {0.025f, 0.01f, -0.006f, -0.01f};
    for (int i = 0; i < 4; i++) e |= tc_frame(0, 0.5f, 0.5f, ds[i], 1);
    CHECK((e & SOMVR_TCE_HOVER) && !(e & SOMVR_TCE_PRESS), "a jab faster than the dwell: no press (0x%x)", e);
    /* outside the rect: nothing; a hand not pointing: nothing, a held press cancelled */
    tc_start();
    e = tc_move(0, 1.2f, 0.5f, 0.08f, -0.01f, 0.3f);
    CHECK(!e && !TO.on, "beside the board: nothing");
    tc_move(0, 0.5f, 0.5f, 0.08f, 0.02f, 0.3f);
    tc_move(0, 0.5f, 0.5f, 0.02f, -0.007f, 0.3f);
    CHECK(TO.down, "pressed");
    e = tc_frame(0, 0.5f, 0.5f, -0.007f, 0);
    CHECK((e & SOMVR_TCE_CANCEL) && (e & SOMVR_TCE_RELEASE) && !TO.on && TS.ups == TS.downs, "the finger curled: cancel "
          "(0x%x)", e);
    /* two hands: the second cannot press while the first holds */
    tc_start();
    tc_move(0, 0.3f, 0.5f, 0.08f, 0.02f, 0.3f), tc_move(0, 0.3f, 0.5f, 0.02f, -0.007f, 0.3f);
    somvr_tc_in in;
    uint32_t ee = 0;
    for (int k = 0; k < 40; k++) {
        memset(&in, 0, sizeof in);
        TT += 1.0 / 90, in.t = TT, in.ok[0] = in.ok[1] = 1;
        float d1 = 0.08f - 0.0034f * (float)k;
        in.tip[0][0] = QP.p[0] + (0.3f - 0.5f) * QS[0], in.tip[0][1] = QP.p[1], in.tip[0][2] = QP.p[2] - 0.007f;
        in.tip[1][0] = QP.p[0] + (0.7f - 0.5f) * QS[0], in.tip[1][1] = QP.p[1], in.tip[1][2] = QP.p[2] + d1;
        in.quad = 7, in.quad_pose = QP, in.quad_size[0] = QS[0], in.quad_size[1] = QS[1];
        somvr_tc_update(&TS, &TC, &in, &TO);
        ee |= TO.edges;
    }
    CHECK(TS.downs == 1 && TO.hand == 0 && TO.down && (ee & SOMVR_TCE_HOVER), "the other hand pressing: no second press (downs %u, hand %d)",
          TS.downs, TO.hand);
    /* another target: a held press ends */
    memset(&in, 0, sizeof in);
    TT += 1.0 / 90, in.t = TT, in.ok[0] = 1, in.quad = 8, in.quad_pose = QP, in.quad_size[0] = QS[0], in.quad_size[1] = QS[1];
    somvr_tc_update(&TS, &TC, &in, &TO);
    CHECK((TO.edges & SOMVR_TCE_CANCEL) && TS.ups == TS.downs, "another target: cancelled");
    /* the smoothing: +-1 mm jitter while hovering moves the point by much less */
    tc_start();
    tc_move(1, 0.5f, 0.5f, 0.08f, 0.015f, 0.3f);
    float lo = 1, hi = 0;
    for (int i = 0; i < 90; i++) {
        float j = (i & 1 ? 1 : -1) * 0.001f / QS[0];
        tc_frame(1, 0.5f + j, 0.5f, 0.015f, 1);
        if (i > 30) lo = fminf(lo, TO.uv[0]), hi = fmaxf(hi, TO.uv[0]);
    }
    CHECK((hi - lo) * QS[0] < 0.0004f, "jitter +-1 mm -> %.2f mm", (hi - lo) * QS[0] * 1000);
    /* ... while a deliberate 10 cm move at 0.5 m/s lags under 1 cm at its end */
    e = 0;
    for (int i = 0; i <= 18; i++) tc_frame(1, 0.5f + (float)i * 0.5f / 90 / QS[0], 0.5f, 0.015f, 1);
    float want = 0.5f + 18 * 0.5f / 90 / QS[0];
    CHECK(fabsf(TO.uv[0] - want) * QS[0] < 0.01f, "a 0.5 m/s move lags %.1f mm", fabsf(TO.uv[0] - want) * QS[0] * 1000);
    /* NaN tip: idle */
    memset(&in, 0, sizeof in);
    TT += 1.0 / 90, in.t = TT, in.ok[1] = 1, in.tip[1][0] = NAN, in.quad = 7, in.quad_pose = QP;
    in.quad_size[0] = QS[0], in.quad_size[1] = QS[1];
    somvr_tc_update(&TS, &TC, &in, &TO);
    CHECK(!TO.on, "a NaN tip: nothing");
}

/* SOMVR_XR_TEST's lines and poses (the headless test hands) */
static void test_xt(void) {
    somvr_xt_cmd c;
    CHECK(somvr_xt_parse("dump", &c) == 1 && c.op == SOMVR_XT_DUMP && somvr_xt_parse("# step 2", &c) == 0 &&
              somvr_xt_parse("", &c) == 0 && somvr_xt_parse("  \n", &c) == 0,
          "dump, comments, blanks");
    CHECK(somvr_xt_parse("hand R touch panel 0.93 0.042 -1.0 s 0.15", &c) == 1 && c.op == SOMVR_XT_TOUCH &&
              c.hand == SOMVR_HAND_R && c.what == SOMVR_XTS_PANEL && NEAR(c.v[0], 0.93, 1e-6) && NEAR(c.v[2], -1, 1e-6) &&
              NEAR(c.sec, 0.15, 1e-6),
          "a touch line");
    CHECK(somvr_xt_parse("hand L head 5 -16 32 0 -90 -130 s 0.4\n", &c) == 1 && c.op == SOMVR_XT_HEAD &&
              c.hand == SOMVR_HAND_L && NEAR(c.v[5], -130, 1e-6) && NEAR(c.sec, 0.4, 1e-6) &&
              somvr_xt_parse("hand L head 1 2 3", &c) == 1 && c.v[3] == 0 && c.sec == 0,
          "head lines (3 or 6 numbers)");
    CHECK(somvr_xt_parse("btn L menu 1", &c) == 1 && c.op == SOMVR_XT_BTN && c.what == SOMVR_XTB_MENU &&
              somvr_xt_parse("btn R trigger 0.3", &c) == 1 && c.what == SOMVR_XTB_TRIGGER &&
              somvr_xt_parse("stick R 1 0", &c) == 1 && c.op == SOMVR_XT_STICK && c.v[0] == 1 &&
              somvr_xt_parse("hand R off", &c) == 1 && c.op == SOMVR_XT_OFF,
          "buttons, sticks, off");
    CHECK(somvr_xt_parse("hand X off", &c) < 0 && somvr_xt_parse("hand R touch nope 1 2 3", &c) < 0 &&
              somvr_xt_parse("hand R head 1 2", &c) < 0 && somvr_xt_parse("hand R head 1 2 3 4", &c) < 0 &&
              somvr_xt_parse("hand R head 1 2 3 s 99", &c) < 0 && somvr_xt_parse("btn R z 1", &c) < 0 &&
              somvr_xt_parse("stick R 2 0", &c) < 0 && somvr_xt_parse("hop", &c) < 0,
          "bad lines");
    /* the head's yaw frame: x right, y up, z ahead; turned 90 deg left, x right = -Z of the space */
    somvr_posef h = {{0, 0, 0, 1}, {0, 1.6f, 0}}, o;
    float cm[3] = {10, -20, 30}, d0[3] = {0, 0, 0}, f[3];
    somvr_xt_head_pose(&h, cm, d0, &o);
    CHECK(NEAR(o.p[0], 0.1, 1e-5) && NEAR(o.p[1], 1.4, 1e-5) && NEAR(o.p[2], -0.3, 1e-5) && NEAR(o.q[3], 1, 1e-5),
          "ahead of a head looking along -Z");
    axis_q(0, 1, 0, 90, h.q);
    somvr_xt_head_pose(&h, cm, d0, &o);
    qrot(o.q, (const float[3]){0, 0, -1}, f);
    CHECK(NEAR(o.p[0], -0.3, 1e-5) && NEAR(o.p[2], -0.1, 1e-5) && NEAR(f[0], -1, 1e-5), "a head turned left");
    /* the watch pose of the drive script: the back of the left wrist toward the eyes, shown */
    h.q[0] = h.q[1] = h.q[2] = 0, h.q[3] = 1;
    float cw[3] = {5, -16, 32}, dw[3] = {0, -90, -130}, eye[3] = {0, 1.6f, 0};
    somvr_xt_head_pose(&h, cw, dw, &o);
    somvr_mm_cfg mc;
    somvr_mm_cfg_default(&mc);
    somvr_posef face;
    CHECK(somvr_mm_pose(&o, SOMVR_HAND_L, &mc, &face) && somvr_mm_facing(&face, eye, &mc, 0), "the watch faces the eyes");
    /* touch: the fingertip at the quad's top right corner, 2 cm in front, the aim into it */
    somvr_posef quad = {{0, 0, 0, 1}, {0, 1, -1}};
    float sz[2] = {0.2f, 0.3f}, tip[3];
    somvr_xt_touch_pose(&quad, sz, 1, 0, 0.02f, 0.05f, &o);
    somvr_tc_tip(&o, 0.05f, tip);
    CHECK(NEAR(tip[0], 0.1, 1e-5) && NEAR(tip[1], 1.15, 1e-5) && NEAR(tip[2], -0.98, 1e-5), "the fingertip on the quad");
    /* the lerp: halfway, the ends, the short way round */
    somvr_posef a = {{0, 0, 0, 1}, {0, 0, 0}}, b = {{0, 0, 0, -1}, {2, 0, 0}};
    somvr_xt_lerp(&a, &b, 0.5f, &o);
    CHECK(NEAR(o.p[0], 1, 1e-6) && NEAR(fabs(o.q[3]), 1, 1e-6), "the lerp: halfway, -q = q");
    somvr_xt_lerp(&a, &b, 7, &o);
    CHECK(NEAR(o.p[0], 2, 1e-6), "the lerp clamped");
}

/* ---- build 8: the HUD's tiles (the wrist HUD, the prompt band) ---- */
static somvr_ht_set set_of(int n, const int r[][4], const int *row) {
    somvr_ht_set t;
    memset(&t, 0, sizeof t);
    t.n = n;
    for (int i = 0; i < n; i++) memcpy(t.r[i], r[i], sizeof t.r[i]), t.row[i] = row ? row[i] : i;
    return t;
}
static void test_tiles(void) {
    int bad = 0;
    somvr_ht_set t;
    CHECK(somvr_wh_parse("on", &bad) == 1 && somvr_pb_parse("1", &bad) == 1 && !somvr_wh_parse(NULL, &bad) &&
              !somvr_pb_parse("off", &bad) && !bad,
          "the two switches");
    CHECK(!somvr_wh_parse("left", &bad) && bad == 1, "a bad SOMVR_XR_WRIST_HUD");
    bad = 0;
    CHECK(somvr_ht_parse(NULL, SOMVR_WH_RECTS_DEFAULT, &t, &bad) == 2 && t.r[0][0] == 0 && t.r[0][1] == 214 &&
              t.r[0][2] == 260 && t.r[0][3] == 96 && t.r[1][0] == 1540 && t.r[1][3] == 104 && t.row[1] == 0 && !bad,
          "the wrist's default rects: the streak %d,%d %dx%d, the target panel %d,..", t.r[0][0], t.r[0][1], t.r[0][2],
          t.r[0][3], t.r[1][0]);
    CHECK(somvr_ht_parse(NULL, SOMVR_WH_CLEAR_DEFAULT, &t, &bad) == 1 && t.r[0][0] == 1400 && t.r[0][2] == 140 && !bad,
          "the wrist's default clear");
    CHECK(somvr_ht_parse("", SOMVR_PB_RECTS_DEFAULT, &t, &bad) == 4 && t.r[0][0] == 640 && t.r[0][2] == 640 &&
              t.r[1][0] == 440 && t.r[1][2] == 1040 && t.r[2][1] == 390 && t.r[3][0] == 1120 && t.r[3][2] == 800 && t.r[3][3] == 220 &&
              t.row[0] == 0 && t.row[1] == 1 && t.row[2] == 2 && t.row[3] == 2 && !bad,
          "the band's default rects and rows (%d %d %d %d)", t.row[0], t.row[1], t.row[2], t.row[3]);
    CHECK(somvr_ht_parse("10,20,30,40", SOMVR_PB_RECTS_DEFAULT, &t, &bad) == 1 && t.r[0][0] == 10 && t.r[0][3] == 40 &&
              !bad,
          "one rect");
    CHECK(somvr_ht_parse("1,2,30,40|5,6,70,80;9,9,9,9", SOMVR_PB_RECTS_DEFAULT, &t, &bad) == 3 && t.r[1][0] == 5 &&
              t.r[1][3] == 80 && t.row[0] == 0 && t.row[1] == 1 && t.row[2] == 1 && !bad,
          "two rows");
    static const char *bads[] = {"1,2,3", "0,0,8,8;", ";0,0,8,8", "0,0,8,8|", "0,0,8,8||0,0,8,8", "1900,0,100,10",
                                 "0,0,4,40", "a,b,c,d", "0,0,8,8;0,0,8,8;0,0,8,8;0,0,8,8;0,0,8,8;0,0,8,8;0,0,8,8",
                                 "0,1075,10,10"};
    for (size_t i = 0; i < sizeof bads / sizeof bads[0]; i++) {
        bad = 0;
        int n = somvr_ht_parse(bads[i], SOMVR_WH_RECTS_DEFAULT, &t, &bad);
        CHECK(n == 2 && bad == 1 && t.r[0][1] == 214, "'%s': not the default and bad (%d, %d)", bads[i], n, bad);
    }
    somvr_ht_set sc;
    somvr_ht_parse(NULL, SOMVR_PB_RECTS_DEFAULT, &t, NULL);
    CHECK(somvr_ht_scale(&t, 960, 540, &sc) && sc.n == 4 && sc.r[1][0] == 220 && sc.r[1][2] == 520 && sc.row[3] == 2,
          "scaled to 960x540: %d,%d", sc.r[1][0], sc.r[1][2]);
    CHECK(!somvr_ht_scale(&t, 8, 8, &sc) && sc.n == 0, "a tiny image: none");
    somvr_pb_cfg c;
    somvr_pb_cfg_default(&c);
    CHECK(c.dist_m == 1.3f && c.line_deg == 0.85f && c.down_deg == 15.0f, "the band's defaults");
    bad = 0;
    somvr_pb_parse_quad("1.0,1.1,20", &c, &bad);
    CHECK(c.dist_m == 1.0f && NEAR(c.line_deg, 1.1, 1e-6) && c.down_deg == 20.0f && !bad, "SOMVR_XR_PROMPTS_QUAD");
    somvr_pb_parse_quad("5,1,1", &c, &bad);
    CHECK(bad && c.dist_m == 1.0f, "a bad SOMVR_XR_PROMPTS_QUAD: unchanged");
    /* 1040 px at 0.85 deg per 18 px = 49.1 deg: 1.19 m wide at 1.3 m; 18 px subtend 0.85 deg on average, more in the
     * middle (0.91), less at the ends (0.75) */
    float wm = somvr_pb_width(1040, 0.85f, 1.3f);
    CHECK(NEAR(wm, 2 * 1.3 * tan(0.5 * 1040 * 0.85 / 18 * 3.14159265 / 180), 1e-4) && NEAR(wm, 1.188, 2e-3), "band width %.4f", wm);
    float mid = 2 * atanf(0.5f * 18 * wm / 1040 / 1.3f) * 57.29578f;
    float end = (atanf(0.5f * wm / 1.3f) - atanf((0.5f * wm - 18 * wm / 1040) / 1.3f)) * 57.29578f;
    CHECK(mid >= 0.85f && mid <= 0.93f && end > 0.72f && end < 0.78f, "18 px in the middle: %.3f deg, at the end %.3f", mid, end);
    /* the layouts (a small image: the default rects scaled by 0.1) */
    int mm[4] = {3, 77, 30, 31}, wr[2][4] = {{0, 21, 26, 10}, {154, 1, 38, 10}}, cr[1][4] = {{140, 1, 14, 10}};
    int br[4][4] = {{64, 1, 64, 11}, {44, 60, 104, 18}, {3, 39, 29, 19}, {140, 86, 52, 22}}, brow[4] = {0, 1, 2, 2};
    somvr_ht_set wt = set_of(2, (const int(*)[4])wr, NULL), ct = set_of(1, (const int(*)[4])cr, NULL);
    somvr_ht_set bt = set_of(4, (const int(*)[4])br, brow), bt2 = set_of(3, (const int(*)[4])(br + 1), brow + 1);
    somvr_hud_layout l;
    CHECK(somvr_hud_layout_make(192, 108, mm, &wt, 0, &ct, NULL, &l) && l.ntile == 3 && l.nclear == 1, "the wrist HUD");
    CHECK(l.mm[0] == 192 + 38 + SOMVR_HT_GAP && l.mm[1] == 0 && l.dst[1][0] == 192 && l.dst[2][0] == 192 &&
              l.dst[2][1] == l.dst[1][1] + 10 + SOMVR_HT_GAP && l.watch[0] == 192 && l.watch[2] == 38 + SOMVR_HT_GAP + 30 &&
              l.watch[3] == 31 && l.extra_w == 76,
          "left wrist: the column (%d,%d / %d,%d) left of the map (%d,%d), watch %dx%d, extra %u", l.dst[1][0], l.dst[1][1],
          l.dst[2][0], l.dst[2][1], l.mm[0], l.mm[1], l.watch[2], l.watch[3], l.extra_w);
    CHECK(l.dst[1][1] == (31 - (10 + SOMVR_HT_GAP + 10)) / 2, "the column centred beside the map (%d)", l.dst[1][1]);
    CHECK(split_check(192, 108, &l, SOMVR_HUD_SPLIT_MAX) > 0, "the wrist HUD's split, its clear rect left out");
    CHECK(somvr_hud_layout_make(192, 108, mm, &wt, 1, &ct, NULL, &l) && l.mm[0] == 192 &&
              l.dst[1][0] == 192 + 30 + SOMVR_HT_GAP && l.extra_w == 76 && split_check(192, 108, &l, SOMVR_HUD_SPLIT_MAX) > 0,
          "right wrist (mirror): the map first, the column right of it (%d), the same width", l.dst[1][0]);
    CHECK(somvr_hud_layout_make(192, 108, NULL, &wt, 0, &ct, NULL, &l) == 0 && l.ntile == 0 && l.nclear == 0 &&
              l.extra_w == 0,
          "no minimap: no column, nothing cleared");
    CHECK(somvr_hud_layout_make(192, 108, mm, NULL, 0, &ct, NULL, &l) && l.nclear == 0, "no column: nothing cleared");
    CHECK(somvr_hud_layout_make(192, 108, mm, &wt, 0, &ct, &bt, &l) && l.ntile == 7, "the wrist HUD and the band");
    int bw = 104, bh = 11 + SOMVR_HT_GAP + 18 + SOMVR_HT_GAP + 22, by = 31 + SOMVR_HT_GAP;
    CHECK(l.band[0] == 192 && l.band[1] == by && l.band[2] == bw && l.band[3] == bh && l.extra_w == 104,
          "the band below the watch: %d,%d %dx%d, extra %u", l.band[0], l.band[1], l.band[2], l.band[3], l.extra_w);
    CHECK(l.dst[3][0] == 192 + (104 - 64) / 2 && l.dst[3][1] == by && l.dst[4][0] == 192 &&
              l.dst[4][1] == by + 11 + SOMVR_HT_GAP && l.dst[5][0] == 192 && l.dst[5][1] == l.dst[4][1] + 18 + SOMVR_HT_GAP &&
              l.dst[6][0] == 192 + 104 - 52 && l.dst[6][1] == l.dst[5][1],
          "the band: the banner centred on top (%d,%d), the hints (%d,%d), XP below left (%d,%d), the prompts right (%d,%d)",
          l.dst[3][0], l.dst[3][1], l.dst[4][0], l.dst[4][1], l.dst[5][0], l.dst[5][1], l.dst[6][0], l.dst[6][1]);
    int n = split_check(192, 108, &l, SOMVR_HUD_SPLIT_MAX);
    CHECK(n > 7 && n <= SOMVR_HUD_SPLIT_MAX, "all seven tiles' split and a clear: %d regions", n);
    int32_t g[SOMVR_HUD_SPLIT_MAX][6];
    CHECK(somvr_hud_split(192, 108, &l, g, 7) == 0, "a cap too small: none");
    int r3[3][4] = {{0, 0, 20, 10}, {30, 0, 20, 12}, {60, 0, 20, 10}}, row3[3] = {0, 0, 0};
    somvr_ht_set t3 = set_of(3, (const int(*)[4])r3, row3), t1 = set_of(1, (const int(*)[4])br, NULL);
    bt2.r[0][2] = 80; /* a wide top row: three tiles under it spread over its width */
    int r4[4][4] = {{0, 50, 80, 10}, {0, 0, 20, 10}, {30, 0, 20, 12}, {60, 0, 20, 10}}, row4[4] = {0, 1, 1, 1};
    somvr_ht_set t4 = set_of(4, (const int(*)[4])r4, row4);
    CHECK(somvr_hud_layout_make(192, 108, NULL, NULL, 0, NULL, &t3, &l) && l.band[2] == 76 && l.band[3] == 12 &&
              l.dst[0][0] == 192 && l.dst[1][0] == 192 + 28 && l.dst[2][0] == 192 + 56,
          "one row of three: packed %d %d %d, %dx%d", l.dst[0][0], l.dst[1][0], l.dst[2][0], l.band[2], l.band[3]);
    CHECK(somvr_hud_layout_make(192, 108, NULL, NULL, 0, NULL, &t4, &l) && l.band[2] == 80 &&
              l.dst[1][0] == 192 && l.dst[2][0] == 192 + 30 && l.dst[3][0] == 192 + 60 && l.dst[1][1] == 18 &&
              split_check(192, 108, &l, SOMVR_HUD_SPLIT_MAX) > 0,
          "three under a wider row: spread %d %d %d at y %d", l.dst[1][0], l.dst[2][0], l.dst[3][0], l.dst[1][1]);
    int mm0[4] = {3, 0, 30, 31};
    CHECK(somvr_hud_layout_make(192, 24, mm0, NULL, 0, NULL, &t1, &l) == 0 && l.ntile == 0,
          "the watch taller than the image: nothing");
    int mm2[4] = {3, 20, 30, 31}, b2[3][4] = {{44, 10, 104, 18}, {40, 30, 29, 19}, {140, 40, 52, 18}}, b2row[3] = {0, 1, 1};
    somvr_ht_set bt3 = set_of(3, (const int(*)[4])b2, b2row);
    CHECK(somvr_hud_layout_make(192, 60, mm2, NULL, 0, NULL, &bt3, &l) && l.band[0] == 192 + 30 + SOMVR_HT_GAP &&
              l.band[1] == 0 && l.extra_w == 30 + SOMVR_HT_GAP + 104 && split_check(192, 60, &l, SOMVR_HUD_SPLIT_MAX) > 0,
          "a low image: the band beside the watch (%d,%d), extra %u", l.band[0], l.band[1], l.extra_w);
    CHECK(somvr_hud_layout_make(192, 108, NULL, NULL, 0, NULL, &t1, &l) && l.band[0] == 192 && l.band[1] == 0 &&
              l.band[2] == 64 && l.band[3] == 11 && split_check(192, 108, &l, SOMVR_HUD_SPLIT_MAX) > 0,
          "the band alone, one tile: at the extra area's top");
    (void)bt2;
    /* the real defaults on a 1920x1080 HUD: the watch 688 x 310, the band 1098 x 526 below it, extra 1098 */
    somvr_ht_set w1, c1, b1;
    int dm[4] = SOMVR_MM_RECT_DEFAULT;
    somvr_ht_parse(NULL, SOMVR_WH_RECTS_DEFAULT, &w1, NULL);
    somvr_ht_parse(NULL, SOMVR_WH_CLEAR_DEFAULT, &c1, NULL);
    somvr_ht_parse(NULL, SOMVR_PB_RECTS_DEFAULT, &b1, NULL);
    CHECK(somvr_hud_layout_make(1920, 1080, dm, &w1, 0, &c1, &b1, &l) && l.watch[2] == 380 + SOMVR_HT_GAP + 300 &&
              l.watch[3] == 310 && l.band[1] == 318 && l.band[2] == 290 + SOMVR_HT_GAP + 800 &&
              l.band[3] == 110 + SOMVR_HT_GAP + 180 + SOMVR_HT_GAP + 220 && l.extra_w == 1098 && l.nclear == 1,
          "1080p defaults: watch %dx%d, band at %d %dx%d, extra %u", l.watch[2], l.watch[3], l.band[1], l.band[2],
          l.band[3], l.extra_w);
    n = somvr_hud_split(1920, 1080, &l, g, SOMVR_HUD_SPLIT_MAX);
    CHECK(n > 8 && n < 60, "1080p defaults: %d regions", n);
}

int main(void) {
    test_switches();
    test_board();
    test_minimap();
    test_tiles();
    test_touch();
    test_xt();
    printf("test_menus: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
