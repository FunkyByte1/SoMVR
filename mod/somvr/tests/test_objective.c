/* test_objective.c - the objective marker (docs/re-xr-pipeline.md 44): somvr_objective.c - the record patterns, the
 * distance text, the placement (ahead: world-locked over the objective's direction with the diamond, not the cell's
 * centre, on it, the quad's distance clamped, faint near the view's centre; out of view: the edge arrow on the view's
 * ellipse, parallel to the view's plane, pointing the way - right, left, up, behind on the nearer side -, the hysteresis, a head turn moving it from
 * the world to the edge; too near: none), the ABI's entry round trip, and the texture (the cells, the guards, the faint
 * copy, the arrow pointing right). */
#include "../somvr_objective.h"

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

static const float FOV[4] = {-0.90f, 0.85f, 0.80f, -0.85f}; /* rad: about a Quest 2's views together */

static void rot(const float q[4], const float v[3], float o[3]) {
    float tx = 2 * (q[1] * v[2] - q[2] * v[1]), ty = 2 * (q[2] * v[0] - q[0] * v[2]), tz = 2 * (q[0] * v[1] - q[1] * v[0]);
    o[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
    o[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
    o[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}
static void yawq(float deg, float q[4]) { /* a head turned deg to the left (about +Y) */
    float a = deg * 0.01745329f;
    q[0] = 0, q[1] = sinf(0.5f * a), q[2] = 0, q[3] = cosf(0.5f * a);
}

static void test_match_text(void) {
    CHECK(somvr_ob_match("Objective*", "Objective") && somvr_ob_match("Objective*", "Objective_Squiller"), "prefix");
    CHECK(!somvr_ob_match("Objective*", "goldenM") && !somvr_ob_match("Objective*", "") && !somvr_ob_match("", "X"),
          "no match");
    CHECK(somvr_ob_match("goldenM,Objective", "Objective") && !somvr_ob_match("goldenM,Objective", "Objective_S") &&
              somvr_ob_match("A,goldenM", "goldenM") && !somvr_ob_match("golden", "goldenM"),
          "exact names in a list");
    CHECK(somvr_ob_match("x,,Ob*", "Obj") && somvr_ob_match("*", "anything"), "empty entries, a lone star");
    char t[16];
    somvr_ob_text(7.4f, t);
    CHECK(!strcmp(t, "7 m"), "%s", t);
    somvr_ob_text(234.6f, t);
    CHECK(!strcmp(t, "235 m"), "%s", t);
    somvr_ob_text(1234.0f, t);
    CHECK(!strcmp(t, "1.2 km"), "%s", t);
    somvr_ob_text(-1, t);
    CHECK(!strcmp(t, "?"), "%s", t);
}

static void test_ahead(void) {
    somvr_ob_cfg c;
    somvr_ob_cfg_default(&c);
    float head[3] = {1, 1.6f, 2}, q[4] = {0, 0, 0, 1}, pos[3] = {1 + 30, 1.6f, 2 - 200}; /* 30 m right, 200 ahead */
    int in = 0;
    somvr_ob_place p = somvr_ob_locate(&c, head, q, FOV, pos, &in);
    CHECK(p.kind == SOMVR_OBK_WORLD && in == 1 && p.cell == SOMVR_OB_CELL_FULL, "ahead: the world marker (%d)", p.kind);
    float side = 2 * 12 * tanf(0.5f * 4 * 0.01745329f);
    CHECK(fabsf(p.size[0] - side) < 1e-4f && p.size[0] == p.size[1], "the size at the clamped 12 m: %f", p.size[0]);
    /* the diamond's centre (the quad's centre + up x (0.5 - V) x side) on the head -> objective line, 12 m out */
    float up[3], yv[3] = {0, 1, 0}, dc[3];
    rot(p.pose, yv, up);
    for (int k = 0; k < 3; k++) dc[k] = p.pose[4 + k] + up[k] * (0.5f - SOMVR_OB_DIAMOND_V) * p.size[0] - head[k];
    float n = sqrtf(dc[0] * dc[0] + dc[1] * dc[1] + dc[2] * dc[2]), dd = sqrtf(30 * 30 + 200 * 200);
    CHECK(fabsf(n - 12) < 1e-3f && fabsf(dc[0] / n - 30 / dd) < 1e-4f && fabsf(dc[2] / n + 200 / dd) < 1e-4f,
          "the diamond on the direction: %f %f %f", dc[0] / n, dc[1] / n, dc[2] / n);
    float zv[3] = {0, 0, 1}, nz[3];
    rot(p.pose, zv, nz); /* the quad faces the head */
    CHECK(nz[0] * dc[0] + nz[1] * dc[1] + nz[2] * dc[2] < -0.99f * n, "it faces the head");
    CHECK(fabsf(p.yaw_deg - atan2f(30, 200) * 57.29578f) < 0.01f && p.off_deg > 8.5f && p.off_deg < 8.6f, "yaw %f off %f",
          p.yaw_deg, p.off_deg);
    /* near: at its own distance; straight ahead: faint; too near: none */
    float pn[3] = {head[0], head[1], head[2] - 6};
    p = somvr_ob_locate(&c, head, q, FOV, pn, &in);
    CHECK(p.kind == SOMVR_OBK_WORLD && p.cell == SOMVR_OB_CELL_FAINT &&
              fabsf(p.size[0] - 2 * 6 * tanf(0.5f * 4 * 0.01745329f)) < 1e-4f,
          "6 m ahead: faint at 6 m (%d %f)", p.cell, p.size[0]);
    float pc[3] = {head[0], head[1], head[2] - 2.5f};
    p = somvr_ob_locate(&c, head, q, FOV, pc, &in);
    CHECK(p.kind == SOMVR_OBK_NONE && in == 0, "2.5 m: none");
    /* the head turned 30 deg left: the objective 30 m right of 200 m ahead is now 38.5 deg right of the view: still
     * inside 0.8 of the right tangent (tan 0.85 = 1.14: 0.91 >= 0.796)? no: out of view -> the edge arrow, pointing right */
    float ql[4];
    yawq(30, ql);
    in = 1;
    p = somvr_ob_locate(&c, head, ql, FOV, pos, &in);
    CHECK(p.kind == SOMVR_OBK_WORLD && in == 1, "38.5 deg right: kept in view by the hysteresis (tan 0.795 <= 0.9 x 1.14)");
    in = 0;
    p = somvr_ob_locate(&c, head, ql, FOV, pos, &in);
    CHECK(p.kind == SOMVR_OBK_WORLD, "38.5 deg right: tan 0.795 <= 0.8 x 1.14 = 0.912: in view");
    yawq(45, ql);
    p = somvr_ob_locate(&c, head, ql, FOV, pos, &in);
    CHECK(p.kind == SOMVR_OBK_EDGE && in == 0 && fabsf(p.roll_deg) < 0.5f && p.pose[4] > 0.3f && fabsf(p.pose[5]) < 1e-3f &&
              fabsf(p.pose[6] + 1) < 1e-4f,
          "53.5 deg right: the arrow at the right edge pointing right (roll %f at %f %f %f)", p.roll_deg, p.pose[4],
          p.pose[5], p.pose[6]);
    CHECK(fabsf(p.pose[4] - 0.72f * tanf(0.85f)) < 1e-3f, "on the ellipse at 0.72 of the right tangent: %f", p.pose[4]);
}

static void test_edges(void) {
    somvr_ob_cfg c;
    somvr_ob_cfg_default(&c);
    float head[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1};
    int in = 0;
    float left[3] = {-100, 0, -20};
    somvr_ob_place p = somvr_ob_locate(&c, head, q, FOV, left, &in);
    CHECK(p.kind == SOMVR_OBK_EDGE && fabsf(fabsf(p.roll_deg) - 180) < 0.5f && p.pose[4] < -0.3f,
          "left: the arrow at the left edge pointing left (roll %f)", p.roll_deg);
    CHECK(fabsf(p.pose[4] + 0.72f * tanf(0.90f)) < 1e-3f, "the left tangent: %f", p.pose[4]);
    float upp[3] = {0, 100, -20};
    p = somvr_ob_locate(&c, head, q, FOV, upp, &in);
    CHECK(p.kind == SOMVR_OBK_EDGE && fabsf(p.roll_deg - 90) < 0.5f && p.pose[5] > 0.3f, "above: pointing up (%f)", p.roll_deg);
    float behind[3] = {0.5f, 0, 50}, behindl[3] = {-0.5f, 0, 50}, dead[3] = {0, 0, 50};
    p = somvr_ob_locate(&c, head, q, FOV, behind, &in);
    CHECK(p.kind == SOMVR_OBK_EDGE && fabsf(p.roll_deg) < 0.5f, "behind, a little right: the right edge (%f)", p.roll_deg);
    p = somvr_ob_locate(&c, head, q, FOV, behindl, &in);
    CHECK(p.kind == SOMVR_OBK_EDGE && fabsf(fabsf(p.roll_deg) - 180) < 0.5f, "behind, a little left: the left edge (%f)",
          p.roll_deg);
    p = somvr_ob_locate(&c, head, q, FOV, dead, &in);
    CHECK(p.kind == SOMVR_OBK_EDGE && fabsf(p.roll_deg) < 0.5f, "straight behind: the right edge (%f)", p.roll_deg);
    /* the arrow faces the eye and its +x points along the way */
    float diag[3] = {100, 100, -10};
    p = somvr_ob_locate(&c, head, q, FOV, diag, &in);
    float xv[3] = {1, 0, 0}, ax[3];
    rot(p.pose, xv, ax);
    CHECK(p.kind == SOMVR_OBK_EDGE && fabsf(p.roll_deg - 45) < 0.5f && fabsf(ax[0] - 0.7071f) < 1e-3f &&
              fabsf(ax[1] - 0.7071f) < 1e-3f && fabsf(ax[2]) < 1e-4f,
          "up-right: its +x up-right (%f %f %f, roll %f)", ax[0], ax[1], ax[2], p.roll_deg);
    CHECK(fabsf(p.size[0] - 2 * tanf(0.5f * 5 * 0.01745329f)) < 1e-4f && fabsf(p.pose[6] + 1) < 1e-5f,
          "the arrow 5 deg at 1 m, parallel to the view's plane");
    float bad[4] = {NAN, 0, 0, 1};
    p = somvr_ob_locate(&c, head, bad, FOV, diag, &in);
    CHECK(p.kind == SOMVR_OBK_NONE, "a NaN head: none");
}

static void test_abi(void) {
    static somvr_objective_shared o;
    memset(&o, 0, sizeof o);
    CHECK(!somvr_objective_ok(&o), "zero: not ok");
    o.magic = SOMVR_OBJECTIVE_MAGIC, o.version = SOMVR_OBJECTIVE_VERSION, o.size = sizeof o;
    atomic_store(&o.active, 1);
    CHECK(somvr_objective_ok(&o) && !memcmp(&o.magic, "SOMROBJ1", 8), "ok, the magic spells SOMROBJ1");
    somvr_objective_frame e = {.n = 1, .flags = SOMVR_OBF_HIDDEN, .pos = {1, 2, 3}, .dist_m = 42, .id = 7}, g;
    somvr_objective_put(&o, 123, &e);
    CHECK(somvr_objective_get(&o, 123, &g) && g.n == 1 && g.pos[2] == 3 && g.dist_m == 42 && g.id == 7 &&
              g.flags == SOMVR_OBF_HIDDEN && atomic_load(&g.seq) == 123 && atomic_load(&o.last) == 123,
          "the entry round trip");
    CHECK(!somvr_objective_get(&o, 123 + SOMVR_NREC, &g) && !somvr_objective_get(&o, 0, &g), "another frame: none");
}

static void test_image(void) {
    static uint8_t px[SOMVR_OB_IMG_W * SOMVR_OB_IMG_H * 4];
    somvr_font f;
    CHECK(somvr_font_init(&f) == 0, "the font");
    somvr_ob_image(px, &f, "234 m", 0);
    int32_t r[4];
    somvr_ob_rect(SOMVR_OB_CELL_ARROW, r);
    CHECK(r[0] == 2 * SOMVR_OB_STRIDE + 1 && r[1] == 1 && r[2] == SOMVR_OB_CELL && r[3] == SOMVR_OB_CELL, "the arrow's rect");
    int guard = 0, full = 0, faint_ok = 1, text = 0, arrow_r = -1, arrow_l = 9999, prem = 1;
    for (int y = 0; y < SOMVR_OB_IMG_H; y++)
        for (int x = 0; x < SOMVR_OB_IMG_W; x++) {
            const uint8_t *p = px + 4 * (y * SOMVR_OB_IMG_W + x);
            int gx = x % SOMVR_OB_STRIDE;
            if ((gx == 0 || gx == SOMVR_OB_STRIDE - 1 || y == 0 || y == SOMVR_OB_IMG_H - 1) && p[3]) guard++;
            for (int k = 0; k < 3; k++) prem &= p[k] <= p[3];
            if (x < SOMVR_OB_STRIDE) {
                full += p[3] > 0;
                if (y > 75 && p[0] == 255 && p[1] == 255 && p[2] == 255) text++;
                const uint8_t *fp = p + 4 * SOMVR_OB_STRIDE;
                if (gx && abs((int)fp[3] - (int)(p[3] * SOMVR_OB_FAINT + 0.5f)) > 1) faint_ok = 0;
            } else if (x >= 2 * SOMVR_OB_STRIDE && x < 3 * SOMVR_OB_STRIDE && p[3] && y == 65) {
                arrow_r = x > arrow_r ? x : arrow_r, arrow_l = x < arrow_l ? x : arrow_l;
            }
        }
    CHECK(guard == 0 && prem, "the guards empty (%d), premultiplied", guard);
    CHECK(full > 1500 && text > 100 && faint_ok, "cell 0 drawn (%d), the white text (%d), cell 1 = 0.4 x cell 0", full, text);
    int ax = 2 * SOMVR_OB_STRIDE + 1;
    CHECK(arrow_r - ax > 105 && arrow_l - ax < 40, "the arrow across the cell at its middle row: %d..%d", arrow_l - ax,
          arrow_r - ax);
    const uint8_t *dc = px + 4 * ((1 + 40) * SOMVR_OB_IMG_W + 1 + 64); /* the diamond's dot: gold, opaque */
    CHECK(dc[3] == 255 && dc[0] == 255 && dc[1] > 190 && dc[2] < 100, "the diamond's centre gold (%d %d %d %d)", dc[0], dc[1],
          dc[2], dc[3]);
    /* build 16: cell 3, the landing ring: its band gold (0.85), its inside faint, its centre gold, its corner empty */
    int rx = 1 + 3 * SOMVR_OB_STRIDE;
    const uint8_t *band = px + 4 * ((1 + 64) * SOMVR_OB_IMG_W + rx + 64 + 53), *fill = px + 4 * ((1 + 64) * SOMVR_OB_IMG_W + rx + 64 + 25),
                  *dot = px + 4 * ((1 + 64) * SOMVR_OB_IMG_W + rx + 64), *corner = px + 4 * ((1 + 4) * SOMVR_OB_IMG_W + rx + 4);
    CHECK(band[3] > 200 && band[0] > 200 && fill[3] > 20 && fill[3] < 60 && dot[3] > 200 && corner[3] == 0,
          "the ring's cell: band %d, fill %d, dot %d, corner %d", band[3], fill[3], dot[3], corner[3]);
    somvr_ob_rect(SOMVR_OB_CELL_RING, r);
    CHECK(r[0] == rx && r[2] == SOMVR_OB_CELL && SOMVR_OB_IMG_W == 4 * SOMVR_OB_STRIDE, "the ring's rect");
    float at[3] = {1.0f, -1.6f, -0.5f};
    somvr_ob_place p = somvr_ob_floor(at, 0.35f);
    float q[4] = {p.pose[0], p.pose[1], p.pose[2], p.pose[3]};
    /* the quad's +z rotated by q: up (the face toward the sky) */
    float zx = 2 * (q[0] * q[2] + q[3] * q[1]), zy = 2 * (q[1] * q[2] - q[3] * q[0]), zz = 1 - 2 * (q[0] * q[0] + q[1] * q[1]);
    CHECK(p.kind == SOMVR_OBK_WORLD && p.cell == SOMVR_OB_CELL_RING && fabsf(zx) < 1e-5f && fabsf(zy - 1) < 1e-5f &&
              fabsf(zz) < 1e-5f && fabsf(p.pose[5] - (-1.58f)) < 1e-5f && p.pose[4] == 1.0f && p.pose[6] == -0.5f &&
              fabsf(p.size[0] - 0.7f * 128 / 124) < 1e-4f,
          "the landing ring flat on the floor (+z -> %g %g %g, y %g, size %g)", zx, zy, zz, p.pose[5], p.size[0]);
    at[1] = NAN;
    CHECK(somvr_ob_floor(at, 0.35f).kind == SOMVR_OBK_NONE, "no ring at NaN");
    somvr_ob_image(px, &f, "234 m", 1);
    CHECK(dc[2] == 255 && dc[0] < 100, "bgra swaps B and R");
}

int main(void) {
    test_match_text();
    test_ahead();
    test_edges();
    test_abi();
    test_image();
    printf("test_objective: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
