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
    somvr_objective_frame e = {.n = 1, .flags = SOMVR_OBF_HIDDEN, .pos = {1, 2, 3}, .dist_m = 42, .id = 7, .top = {4, 5, 6}}, g;
    somvr_objective_put(&o, 123, &e);
    CHECK(somvr_objective_get(&o, 123, &g) && g.n == 1 && g.pos[2] == 3 && g.dist_m == 42 && g.id == 7 &&
              g.flags == SOMVR_OBF_HIDDEN && atomic_load(&g.seq) == 123 && atomic_load(&o.last) == 123 && g.top[0] == 4 &&
              g.top[2] == 6,
          "the entry round trip (build 17: the line's top too)");
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
            int gx = x % SOMVR_OB_STRIDE, inside = 0;
            for (int c = 0; c < SOMVR_OB_CELLS && !inside; c++) { /* build 17: the strike's cells are wide */
                int32_t cr[4];
                somvr_ob_rect(c, cr);
                inside = x >= cr[0] && x < cr[0] + cr[2] && y >= cr[1] && y < cr[1] + cr[3];
            }
            if (!inside && p[3]) guard++;
            for (int k = 0; k < 3; k++) prem &= p[k] <= p[3];
            if (x < SOMVR_OB_STRIDE && y < SOMVR_OB_STRIDE) { /* (build 21: the second row is the timer's) */
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
    { /* build 21: the timer's cells - the bright white part of the timer ring grows with the steps left, all at ARMED */
        int bright[SOMVR_OB_TIMER_ARMED + 1], mono = 1;
        for (int k = 0; k <= SOMVR_OB_TIMER_ARMED; k++) {
            int32_t tr[4];
            somvr_ob_rect(SOMVR_OB_CELL_TIMER0 + k, tr);
            bright[k] = 0;
            for (int y = tr[1]; y < tr[1] + tr[3]; y++)
                for (int x = tr[0]; x < tr[0] + tr[2]; x++) {
                    const uint8_t *p = px + 4 * (y * SOMVR_OB_IMG_W + x);
                    bright[k] += p[3] >= 240 && p[0] >= 235 && p[1] >= 235 && p[2] >= 235;
                }
            if (k && k <= SOMVR_OB_TIMER_STEPS && bright[k] <= bright[k - 1]) mono = 0;
        }
        int32_t t0[4];
        somvr_ob_rect(SOMVR_OB_CELL_TIMER0, t0);
        CHECK(t0[1] == SOMVR_OB_STRIDE + 1 && t0[0] == 1 && bright[0] < 20 && mono && bright[SOMVR_OB_TIMER_STEPS] > 1200 &&
                  bright[SOMVR_OB_TIMER_ARMED] > bright[SOMVR_OB_TIMER_STEPS] &&
                  abs(bright[6] - bright[SOMVR_OB_TIMER_STEPS] / 2) < bright[SOMVR_OB_TIMER_STEPS] / 10,
              "the timer's cells (the second row): white %d (empty), %d (half), %d (full), %d (armed), growing %d", bright[0],
              bright[6], bright[SOMVR_OB_TIMER_STEPS], bright[SOMVR_OB_TIMER_ARMED], mono);
    }
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
    CHECK(r[0] == rx && r[2] == SOMVR_OB_CELL && SOMVR_OB_IMG_W == 17 * SOMVR_OB_STRIDE, "the ring's rect"); /* 21: 17 */
    /* build 18: cell 8, the refused landing: a square after the strike's three wide cells; its band red, the bar across
     * its middle red, beside the bar faint, the corner empty */
    /* build 39: the third row - the edge's glow (a square: its core opaque, its sides faint, its ends faded) and the two
     * A labels (wide: the blue button's middle opaque, the plate's corner empty) */
    somvr_ob_rect(SOMVR_OB_CELL_GLOW, r);
    CHECK(r[0] == 1 && r[1] == 2 * SOMVR_OB_STRIDE + 1 && r[2] == SOMVR_OB_CELL && SOMVR_OB_IMG_H == 3 * SOMVR_OB_STRIDE,
          "the glow's rect %d %d %d", r[0], r[1], r[2]);
    {
        const uint8_t *gc = px + 4 * ((r[1] + 64) * SOMVR_OB_IMG_W + r[0] + 64), *gs = px + 4 * ((r[1] + 64) * SOMVR_OB_IMG_W + r[0] + 120),
                      *ge = px + 4 * ((r[1] + 1) * SOMVR_OB_IMG_W + r[0] + 64);
        CHECK(gc[3] > 240 && gs[3] < 30 && ge[3] < 40, "the glow's cell: core %d, side %d, end %d", gc[3], gs[3], ge[3]);
    }
    for (int c = SOMVR_OB_CELL_A_DOWN; c <= SOMVR_OB_CELL_A_UP; c++) {
        somvr_ob_rect(c, r);
        CHECK(r[1] == 2 * SOMVR_OB_STRIDE + 1 && r[2] == SOMVR_OB_WIDE * SOMVR_OB_STRIDE - 2 && r[0] + r[2] < SOMVR_OB_IMG_W,
              "the A label %d's rect %d %d %d", c, r[0], r[1], r[2]);
        int on = 0;
        for (int x = 0; x < r[2] / 2; x++) { /* the blue button somewhere along the middle row of the left half */
            const uint8_t *q = px + 4 * ((r[1] + 64) * SOMVR_OB_IMG_W + r[0] + x);
            on += q[3] > 240 && q[2] > 200;
        }
        const uint8_t *cn = px + 4 * ((r[1] + 2) * SOMVR_OB_IMG_W + r[0] + 2);
        CHECK(on > 20 && cn[3] == 0, "the A label %d: button texels %d, corner %d", c, on, cn[3]);
    }
    float w_hd[3] = {0, 1.6f, 0}, w_e0[3] = {-0.3f, 0.2f, -1.2f}, w_e1[3] = {0.3f, 0.2f, -1.2f}, w_at[3] = {0, 0.28f, -1.2f};
    somvr_ob_place gp = somvr_ob_glow(w_e0, w_e1, w_hd), lp = somvr_ob_a_label(w_hd, w_at, SOMVR_OB_A_DOWN);
    CHECK(gp.kind == SOMVR_OBK_WORLD && gp.cell == SOMVR_OB_CELL_GLOW && fabsf(gp.size[1] - 0.6f) < 0.01f &&
              fabsf(gp.size[0] - SOMVR_OB_GLOW_M) < 1e-4f && fabsf(gp.pose[4]) < 1e-4f,
          "the glow's quad: kind %d cell %d size %.3f x %.3f at x %.3f", gp.kind, gp.cell, gp.size[0], gp.size[1], gp.pose[4]);
    CHECK(lp.kind == SOMVR_OBK_WORLD && lp.cell == SOMVR_OB_CELL_A_DOWN && lp.size[1] >= SOMVR_OB_A_LABEL_MIN_M &&
              lp.size[0] > 2.5f * lp.size[1] && fabsf(lp.pose[5] - (w_at[1] + 0.5f * lp.size[1])) < 1e-4f &&
              somvr_ob_a_label(w_hd, w_at, 7).kind == SOMVR_OBK_NONE,
          "the A label's quad: kind %d cell %d %.3f x %.3f, its middle at y %.3f", lp.kind, lp.cell, lp.size[0], lp.size[1], lp.pose[5]);
    { /* build 39: the frame's edge fields through the shared ring (put, get): the glow's ends and the A word */
        static somvr_objective_shared sh;
        somvr_objective_frame fi, fo;
        memset(&fi, 0, sizeof fi), memset(&fo, 0, sizeof fo);
        fi.n = 1, fi.flags = SOMVR_OBF_EDGE, fi.a_word = SOMVR_OB_A_DOWN;
        fi.edge0[0] = -0.3f, fi.edge0[1] = 0.2f, fi.edge0[2] = -1.2f, fi.edge1[0] = 0.3f, fi.edge1[1] = 0.2f, fi.edge1[2] = -1.2f;
        somvr_objective_put(&sh, 77, &fi);
        int got = somvr_objective_get(&sh, 77, &fo);
        CHECK(got && fo.flags == SOMVR_OBF_EDGE && fo.a_word == SOMVR_OB_A_DOWN && fo.edge0[0] == -0.3f && fo.edge1[0] == 0.3f &&
                  fo.edge1[2] == -1.2f,
              "the edge fields through put/get: a_word %u, edge0.x %g, edge1.x %g", fo.a_word, fo.edge0[0], fo.edge1[0]);
    }
    somvr_ob_rect(SOMVR_OB_CELL_RING_NO, r);
    int nx = 1 + 14 * SOMVR_OB_STRIDE;
    CHECK(r[0] == nx && r[1] == 1 && r[2] == SOMVR_OB_CELL && r[3] == SOMVR_OB_CELL && r[0] + r[2] < SOMVR_OB_IMG_W,
          "the refused ring's rect %d %d %d %d", r[0], r[1], r[2], r[3]);
    const uint8_t *nb = px + 4 * ((1 + 64) * SOMVR_OB_IMG_W + nx + 64 + 55), *nm = px + 4 * ((1 + 64) * SOMVR_OB_IMG_W + nx + 64),
                  *ns = px + 4 * ((1 + 64 - 20) * SOMVR_OB_IMG_W + nx + 64 + 20), *nf = px + 4 * ((1 + 64 + 25) * SOMVR_OB_IMG_W + nx + 64 + 25),
                  *nc = px + 4 * ((1 + 4) * SOMVR_OB_IMG_W + nx + 4);
    CHECK(nb[3] > 200 && nb[0] > 190 && nb[1] < 60 && nm[3] > 200 && nm[0] > 190 && ns[3] > 10 && ns[3] < 40 && nf[3] > 200 &&
              nc[3] == 0,
          "the refused ring's cell: band %d (r %d g %d), middle %d, beside the bar %d, on the bar %d, corner %d", nb[3], nb[0],
          nb[1], nm[3], ns[3], nf[3], nc[3]);
    /* build 17: cell 4, the drop's line: a gold middle, dark edges, nothing at the cell's sides; faded in at its top */
    int lx = 1 + 4 * SOMVR_OB_STRIDE;
    const uint8_t *mid = px + 4 * ((1 + 64) * SOMVR_OB_IMG_W + lx + 64), *rim = px + 4 * ((1 + 64) * SOMVR_OB_IMG_W + lx + 64 + 45),
                  *side = px + 4 * ((1 + 64) * SOMVR_OB_IMG_W + lx + 64 + 60), *top = px + 4 * ((1 + 0) * SOMVR_OB_IMG_W + lx + 64);
    CHECK(mid[3] > 155 && mid[3] < 180 && mid[0] > 155 && rim[3] > 75 && rim[3] < 100 && rim[0] < 20 && side[3] == 0 &&
              top[3] < 10,
          "the line's cell: middle %d, rim %d, side %d, top %d", mid[3], rim[3], side[3], top[3]);
    somvr_ob_rect(SOMVR_OB_CELL_LINE, r);
    CHECK(r[0] == lx && r[2] == SOMVR_OB_CELL, "the line's rect");
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

/* v rotated by the quaternion q (x y z w) */
static void qrot(const float q[4], const float v[3], float o[3]) {
    float t[3] = {2 * (q[1] * v[2] - q[2] * v[1]), 2 * (q[2] * v[0] - q[0] * v[2]), 2 * (q[0] * v[1] - q[1] * v[0])};
    o[0] = v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]);
    o[1] = v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]);
    o[2] = v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]);
}
static int near3f(const float a[3], float x, float y, float z, float e) {
    return fabsf(a[0] - x) < e && fabsf(a[1] - y) < e && fabsf(a[2] - z) < e;
}

/* build 17: the drop's line: along top - bot, its face toward the head (square to the line), w wide */
static void test_line(void) {
    const float ex[3] = {1, 0, 0}, ey[3] = {0, 1, 0}, ez[3] = {0, 0, 1};
    float top[3] = {0, 0, -1}, bot[3] = {0, -5, -1}, head[3] = {0, 1.6f, 0}, a[3], b[3], c[3];
    somvr_ob_place p = somvr_ob_line(top, bot, head, 0.04f);
    qrot(p.pose, ex, a), qrot(p.pose, ey, b), qrot(p.pose, ez, c);
    CHECK(p.kind == SOMVR_OBK_WORLD && p.cell == SOMVR_OB_CELL_LINE && near3f(p.pose + 4, 0, -2.5f, -1, 1e-5f) &&
              fabsf(p.size[0] - 0.04f) < 1e-6f && fabsf(p.size[1] - 5) < 1e-5f && near3f(b, 0, 1, 0, 1e-5f) &&
              near3f(c, 0, 0, 1, 1e-5f) && near3f(a, 1, 0, 0, 1e-5f),
          "the line ahead: up along it, facing the head (+z %g %g %g)", c[0], c[1], c[2]);
    head[0] = 2, head[1] = 0, head[2] = -1; /* the head to its side */
    p = somvr_ob_line(top, bot, head, 0.04f);
    qrot(p.pose, ey, b), qrot(p.pose, ez, c);
    CHECK(near3f(b, 0, 1, 0, 1e-5f) && near3f(c, 1, 0, 0, 1e-5f), "the head at its side: facing it (+z %g %g %g)", c[0], c[1],
          c[2]);
    float top2[3] = {1, 0, 0}, bot2[3] = {0, -2, 0}; /* a slanted line, seen from above (looking down on it from the edge) */
    head[0] = 0, head[1] = 1.7f, head[2] = 0;
    p = somvr_ob_line(top2, bot2, head, 0.04f);
    qrot(p.pose, ey, b), qrot(p.pose, ez, c);
    float l = sqrtf(5.0f), d = c[0] * b[0] + c[1] * b[1] + c[2] * b[2];
    float hm[3] = {head[0] - p.pose[4], head[1] - p.pose[5], head[2] - p.pose[6]}, toward = c[0] * hm[0] + c[1] * hm[1] + c[2] * hm[2];
    CHECK(near3f(b, 1 / l, 2 / l, 0, 1e-5f) && fabsf(d) < 1e-5f && toward > 0 && fabsf(p.size[1] - l) < 1e-5f,
          "slanted: along it (%g %g %g), +z square to it and toward the head", b[0], b[1], b[2]);
    head[0] = 0, head[1] = 3, head[2] = -1; /* the head on its axis (straight over it) */
    p = somvr_ob_line(top, bot, head, 0.04f);
    qrot(p.pose, ey, b), qrot(p.pose, ez, c);
    CHECK(p.kind == SOMVR_OBK_WORLD && near3f(b, 0, 1, 0, 1e-5f) && fabsf(c[1]) < 1e-5f &&
              fabsf(c[0] * c[0] + c[2] * c[2] - 1) < 1e-4f,
          "on its axis: any square face (+z %g %g %g)", c[0], c[1], c[2]);
    CHECK(somvr_ob_line(top, top, head, 0.04f).kind == SOMVR_OBK_NONE, "no length: none");
    top[1] = NAN;
    CHECK(somvr_ob_line(top, bot, head, 0.04f).kind == SOMVR_OBK_NONE, "NaN: none");
}

/* build 17: the Shadow Strike's mark (SOMVR_OBF_STRIKE): the three wide cells, their pictures, the placement */
static void test_strike_mark(void) {
    static uint8_t px[SOMVR_OB_IMG_W * SOMVR_OB_IMG_H * 4];
    somvr_font f;
    somvr_font_init(&f);
    somvr_ob_image(px, &f, "12 m", 0);
    int32_t r[3][4], sq[4];
    somvr_ob_rect(SOMVR_OB_CELL_LINE, sq); /* the last square cell (the drop's line, build 17) */
    for (int i = 0; i < 3; i++) somvr_ob_rect(SOMVR_OB_CELL_STRIKE + i, r[i]);
    CHECK(r[0][0] == 5 * SOMVR_OB_STRIDE + 1 && r[0][2] == 3 * SOMVR_OB_STRIDE - 2 && r[0][3] == SOMVR_OB_CELL &&
              r[1][0] == r[0][0] + 3 * SOMVR_OB_STRIDE && r[2][0] == r[1][0] + 3 * SOMVR_OB_STRIDE &&
              r[2][0] + r[2][2] == SOMVR_OB_IMG_W - 1 - 3 * SOMVR_OB_STRIDE && sq[0] + sq[2] < r[0][0], /* build 18: cell 8 after them; 21: 9, 10 */
          "the strike's cells: 388 px wide, side by side after the line's, inside the image (%d %d %d)", r[0][0], r[1][0], r[2][0]);
#define PX(c, x, y) (px + 4 * ((size_t)(r[c][1] + (y)) * SOMVR_OB_IMG_W + (size_t)(r[c][0] + (x))))
    /* the ring's band (5 px inside its outer edge, at its right), its centre, the A button, the text */
    const uint8_t *band0 = PX(0, 64 + 49, 64), *mid0 = PX(0, 64, 64), *band1 = PX(1, 64 + 49, 64), *mid2 = PX(2, 64, 64),
                  *btn = PX(0, 134 + 22, 50), *btn1 = PX(1, 134 + 22, 50), *corner = PX(0, 2, 2);
    CHECK(band0[3] == 255 && band0[2] == 255 && band0[0] > 140 && band0[0] < 170, "ready: the band wraith-blue (%d %d %d %d)",
          band0[0], band0[1], band0[2], band0[3]);
    CHECK(mid0[3] > 20 && mid0[3] < 40 && corner[3] == 0, "ready: the ring's inside faint (%d), the corner clear", mid0[3]);
    CHECK(band1[3] > 170 && band1[3] < 200 && abs((int)band1[0] - (int)band1[2]) < 15, "no arrows: the band grey (%d %d %d %d)",
          band1[0], band1[1], band1[2], band1[3]);
    CHECK(mid2[3] > 65 && mid2[3] < 90, "pressed: the ring filled (%d)", mid2[3]);
    int blue1 = 0;
    for (int y = 0; y < SOMVR_OB_CELL; y++)
        for (int x = 128; x < r[1][2]; x++) blue1 += PX(1, x, y)[2] == 255 && PX(1, x, y)[0] == 158;
    CHECK(btn[3] == 255 && btn[2] == 255 && btn[0] == 158 && blue1 == 0 && btn1 != NULL, "the A button in ready (%d %d %d %d), none in no arrows (%d)",
          btn[0], btn[1], btn[2], btn[3], blue1);
    int white = 0, red = 0, dark_a = 0;
    for (int y = 0; y < SOMVR_OB_CELL; y++)
        for (int x = 128; x < r[0][2]; x++) {
            const uint8_t *a = PX(0, x, y), *b = PX(1, x, y);
            white += a[0] == 255 && a[1] == 255 && a[2] == 255 && a[3] == 255;
            red += b[0] == 255 && b[1] == 130 && b[2] == 110;
        }
    for (int y = 40; y < 90; y++)
        for (int x = 140; x < 170; x++) dark_a += PX(0, x, y)[2] < 60 && PX(0, x, y)[3] == 255;
    CHECK(white > 400 && red > 300 && dark_a > 40, "the label: white text %d, red text %d, the letter on the button %d", white,
          red, dark_a);
#undef PX
    /* the placement: the ring's centre on the orc's chest, the quad facing the head, its size from the ring */
    float head[3] = {0, 1.7f, 0}, pos[3] = {3.0f, 1.4f, -8.0f};
    float ring = somvr_ob_strike_ring(8.5f, 1.0f, 7.0f);
    CHECK(fabsf(ring - 2 * 8.5f * tanf(3.5f * 0.0174533f)) < 1e-4f && somvr_ob_strike_ring(3.0f, 1.0f, 7.0f) == 1.0f,
          "the ring: 7 deg of the view (%.3f m at 8.5 m), at least 1 m", ring);
    somvr_ob_place p = somvr_ob_strike(head, pos, ring, SOMVR_OB_CELL_STRIKE);
    const float *q = p.pose;
    float xx = 1 - 2 * (q[1] * q[1] + q[2] * q[2]), xy = 2 * (q[0] * q[1] + q[3] * q[2]), xz = 2 * (q[0] * q[2] - q[3] * q[1]);
    float zx = 2 * (q[0] * q[2] + q[3] * q[1]), zy = 2 * (q[1] * q[2] - q[3] * q[0]), zz = 1 - 2 * (q[0] * q[0] + q[1] * q[1]);
    float h = ring * 128 / 108.0f, w = h * 388 / 128.0f, sh = 0.5f * w - 0.5f * h;
    float cx = p.pose[4] - xx * sh, cy = p.pose[5] - xy * sh, cz = p.pose[6] - xz * sh;
    float to[3] = {head[0] - pos[0], head[1] - pos[1], head[2] - pos[2]}, tl = sqrtf(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
    CHECK(p.kind == SOMVR_OBK_WORLD && p.cell == SOMVR_OB_CELL_STRIKE && fabsf(cx - pos[0]) < 1e-4f &&
              fabsf(cy - pos[1]) < 1e-4f && fabsf(cz - pos[2]) < 1e-4f,
          "the ring's centre on the chest (%.3f %.3f %.3f)", cx, cy, cz);
    CHECK(fabsf(zx - to[0] / tl) < 1e-3f && fabsf(zy - to[1] / tl) < 1e-3f && fabsf(zz - to[2] / tl) < 1e-3f && fabsf(xy) < 1e-5f,
          "facing the head, level (+z %.3f %.3f %.3f, +x up %.5f)", zx, zy, zz, xy);
    CHECK(fabsf(p.size[1] - h) < 1e-5f && fabsf(p.size[0] - w) < 1e-4f, "the size: %.3f x %.3f m", p.size[0], p.size[1]);
    CHECK(somvr_ob_strike(head, pos, ring, SOMVR_OB_CELL_RING).kind == SOMVR_OBK_NONE &&
              somvr_ob_strike(head, head, ring, SOMVR_OB_CELL_STRIKE).kind == SOMVR_OBK_NONE &&
              somvr_ob_strike(head, pos, NAN, SOMVR_OB_CELL_STRIKE).kind == SOMVR_OBK_NONE,
          "no mark: another cell, at the head, NaN");
}

/* build 41 (SOMVR_OBF_STRUGGLE, re-xr-pipeline.md 180): a struggle's cue - the timer ring's cell at its node facing the
 * head, the arrow turned to its way as the eye sees it */
static void test_struggle_cue(void) {
    float head[3] = {0, 1.7f, 0}, pos[3] = {0.2f, 1.4f, -0.9f};
    somvr_ob_place p = somvr_ob_struggle(head, pos, 0.22f, 6, 0);
    const float *q = p.pose;
    float zx = 2 * (q[0] * q[2] + q[3] * q[1]), zy = 2 * (q[1] * q[2] - q[3] * q[0]), zz = 1 - 2 * (q[0] * q[0] + q[1] * q[1]);
    float to[3] = {head[0] - pos[0], head[1] - pos[1], head[2] - pos[2]}, tl = sqrtf(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
    CHECK(p.kind == SOMVR_OBK_WORLD && p.cell == SOMVR_OB_CELL_TIMER0 + 6 && fabsf(p.pose[4] - pos[0]) < 1e-5f &&
              fabsf(p.pose[6] - pos[2]) < 1e-5f && fabsf(p.size[0] - 0.22f) < 1e-6f && p.size[0] == p.size[1],
          "the ring with 6 steps left on the node, 0.22 m square (cell %d)", p.cell);
    CHECK(fabsf(zx - to[0] / tl) < 1e-3f && fabsf(zy - to[1] / tl) < 1e-3f && fabsf(zz - to[2] / tl) < 1e-3f,
          "facing the head (+z %.3f %.3f %.3f)", zx, zy, zz);
    CHECK(somvr_ob_struggle(head, pos, 0.22f, SOMVR_OB_TIMER_ARMED, 0).cell == SOMVR_OB_CELL_TIMER0 + SOMVR_OB_TIMER_ARMED,
          "the bright ring (done)");
    /* the arrow straight ahead of a level head: roll 0 = its +x to the right, 90 = up, 180 = left, 270 = down */
    float ahead[3] = {0, 1.7f, -1.0f};
    const float rw[4][3] = {{1, 0, 0}, {0, 1, 0}, {-1, 0, 0}, {0, -1, 0}};
    for (int k = 0; k < 4; k++) {
        somvr_ob_place a = somvr_ob_struggle(head, ahead, 0.22f, SOMVR_OB_SG_ARROW, 90.0f * (float)k);
        const float *r = a.pose;
        float xx = 1 - 2 * (r[1] * r[1] + r[2] * r[2]), xy = 2 * (r[0] * r[1] + r[3] * r[2]), xz = 2 * (r[0] * r[2] - r[3] * r[1]);
        CHECK(a.kind == SOMVR_OBK_WORLD && a.cell == SOMVR_OB_CELL_ARROW && fabsf(xx - rw[k][0]) < 1e-4f &&
                  fabsf(xy - rw[k][1]) < 1e-4f && fabsf(xz - rw[k][2]) < 1e-4f,
              "the arrow at %d deg points %.0f %.0f %.0f (its +x %.3f %.3f %.3f)", 90 * k, rw[k][0], rw[k][1], rw[k][2], xx, xy, xz);
    }
    CHECK(somvr_ob_struggle(head, pos, 0.22f, SOMVR_OB_TIMER_ARMED + 1, 0).kind == SOMVR_OBK_NONE &&
              somvr_ob_struggle(head, head, 0.22f, 3, 0).kind == SOMVR_OBK_NONE &&
              somvr_ob_struggle(head, pos, NAN, 3, 0).kind == SOMVR_OBK_NONE,
          "nothing: an id past the bright ring, at the head, NaN");
}

/* build 40 (SOMVR_OBF_READOUT, re-xr-pipeline.md 171.7): Practice > Finisher test's readout - the lines from an entry,
 * the cell after build 39's A labels in the third row, the card and its ink in it, standing on its spot facing the head */
static void test_readout(void) {
    somvr_objective_frame e;
    memset(&e, 0, sizeof e);
    char l1[SOMVR_OB_READOUT_COLS + 1], l2[SOMVR_OB_READOUT_COLS + 1];
    e.id = 3u | 1u << 8, e.dist_m = 9.6f, e.top[0] = 2.44f, e.top[1] = 22, e.top[2] = 0.35f;
    somvr_ob_readout_text(&e, l1, l2);
    CHECK(!strcmp(l1, "Try 3  snap 10 cm  in 2.4 m/s") && !strcmp(l2, "Depth 22 cm  KILL  out 0.35 s"), "readout, a kill: '%s' / '%s'",
          l1, l2);
    e.id = 12u | (unsigned)SOMVR_OB_RO_BACK << 9, e.dist_m = 7, e.top[0] = 0.9f, e.top[1] = 4, e.top[2] = -1;
    somvr_ob_readout_text(&e, l1, l2);
    CHECK(!strcmp(l2, "Depth 4 cm  no kill (drawn back)") && strlen(l1) <= SOMVR_OB_READOUT_COLS, "readout, drawn back: '%s' / '%s'",
          l1, l2);
    e.dist_m = -1;
    somvr_ob_readout_text(&e, l1, l2);
    CHECK(!strcmp(l1, "Try 12  no snap"), "readout, no snap: '%s'", l1);
    /* build 41: the finisher named (bits 12..15), a cut's line - its arc's share and the time to its end */
    e.id = 2u | 1u << 8 | 2u << 12, e.dist_m = 8.2f, e.top[0] = 3.1f, e.top[1] = 100, e.top[2] = 0.42f;
    somvr_ob_readout_text(&e, l1, l2);
    CHECK(!strcmp(l1, "Behead 2  snap 8 cm  in 3.1 m/s") && !strcmp(l2, "Cut 100%  KILL  end 0.42 s"),
          "readout, a beheading: '%s' / '%s'", l1, l2);
    e.id = 7u | (unsigned)SOMVR_OB_RO_STOP << 9 | 7u << 12, e.top[1] = 14;
    somvr_ob_readout_text(&e, l1, l2);
    CHECK(!strcmp(l1, "Counter 7  snap 8 cm  in 3.1 m/s") && !strcmp(l2, "Depth 14 cm  no kill (stopped)") &&
              strlen(l1) <= SOMVR_OB_READOUT_COLS, "readout, the counter kill stopped: '%s' / '%s'", l1, l2);
    int32_t r[4], a[4];
    somvr_ob_rect(SOMVR_OB_CELL_READOUT, r);
    somvr_ob_rect(SOMVR_OB_CELL_A_UP, a);
    CHECK(r[1] == a[1] && r[0] >= a[0] + a[2] + 2 && r[0] + r[2] <= SOMVR_OB_IMG_W - 1 && r[1] + r[3] <= SOMVR_OB_IMG_H - 1 &&
              r[2] >= SOMVR_OB_READOUT_COLS * 16 + 12,
          "the readout's cell after the A labels in the third row, inside (%d,%d %dx%d)", r[0], r[1], r[2], r[3]);
    static uint8_t px[SOMVR_OB_IMG_W * SOMVR_OB_IMG_H * 4];
    somvr_font f;
    CHECK(somvr_font_init(&f) == 0, "the font");
    somvr_ob_image(px, &f, "5 m", 0);
    int before = 0;
    for (int y = r[1]; y < r[1] + r[3]; y++)
        for (int x = r[0]; x < r[0] + r[2]; x++) before += px[4 * ((size_t)y * SOMVR_OB_IMG_W + (size_t)x) + 3] != 0;
    somvr_ob_readout_draw(px, &f, "Try 3  snap 10 cm  in 2.4 m/s", "Depth 22 cm  KILL  out 0.35 s", 0);
    int card = 0, white = 0, outside = 0;
    for (int y = 0; y < SOMVR_OB_IMG_H; y++)
        for (int x = 0; x < SOMVR_OB_IMG_W; x++) {
            const uint8_t *p = px + 4 * ((size_t)y * SOMVR_OB_IMG_W + (size_t)x);
            int in = x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3];
            if (in) card += p[3] > 100, white += p[0] == 255 && p[1] == 255 && p[2] == 255;
        }
    somvr_ob_readout_draw(px, &f, "", "", 0);
    for (int y = r[1]; y < r[1] + r[3]; y++)
        for (int x = r[0]; x < r[0] + r[2]; x++) outside += px[4 * ((size_t)y * SOMVR_OB_IMG_W + (size_t)x) + 3] != 0;
    CHECK(before == 0 && card > r[2] * r[3] / 2 && white > 300 && outside == 0,
          "readout drawn: clear before (%d), the card (%d px), the white ink (%d), cleared again (%d)", before, card, white, outside);
    float head[3] = {0, 1.7f, 0}, pos[3] = {0.2f, 2.1f, -1.4f};
    somvr_ob_place p = somvr_ob_readout(head, pos);
    float dd = sqrtf(0.04f + 0.16f + 1.96f), h = 2 * dd * tanf(0.5f * SOMVR_OB_READOUT_DEG * 0.01745329f);
    CHECK(p.kind == SOMVR_OBK_WORLD && p.cell == SOMVR_OB_CELL_READOUT && fabsf(p.size[1] - h) < 1e-4f &&
              fabsf(p.size[0] / p.size[1] - (float)r[2] / 128.0f) < 1e-3f && p.pose[4] == pos[0] && p.pose[6] == pos[2],
          "readout placed on its spot, %.3f x %.3f m (%.1f deg high at %.2f m)", p.size[0], p.size[1], SOMVR_OB_READOUT_DEG, dd);
}

/* build 37 (SOMVR_OBF_FINISH, re-xr-pipeline.md 171): the finisher's hint - a wide cell in the second row's room after
 * the timer's squares (the texture's size unchanged), the strike's blue ring and the label "FACE TO FACE / IN REACH"
 * (white ink), no A button; placed as the strike's chest ring */
static void test_finish_cell(void) {
    int32_t r[4], t[4];
    somvr_ob_rect(SOMVR_OB_CELL_FINISH, r);
    somvr_ob_rect(SOMVR_OB_CELL_TIMER0 + SOMVR_OB_TIMER_ARMED, t);
    CHECK(r[1] == t[1] && r[0] >= t[0] + t[2] + 2 && r[0] + r[2] <= SOMVR_OB_IMG_W - 1 && r[1] + r[3] <= SOMVR_OB_IMG_H - 1 &&
              r[2] == SOMVR_OB_WIDE * SOMVR_OB_STRIDE - 2 && r[3] == SOMVR_OB_CELL,
          "the finisher's cell after the last timer square, inside the texture (%d,%d %dx%d of %dx%d)", r[0], r[1], r[2], r[3],
          SOMVR_OB_IMG_W, SOMVR_OB_IMG_H);
    static uint8_t px[SOMVR_OB_IMG_W * SOMVR_OB_IMG_H * 4];
    somvr_font f;
    CHECK(somvr_font_init(&f) == 0, "the font");
    memset(px, 0, sizeof px);
    somvr_ob_image(px, &f, "5 m", 0);
    int blue = 0, white = 0, button = 0;
    for (int y = r[1]; y < r[1] + r[3]; y++)
        for (int x = r[0]; x < r[0] + r[2]; x++) {
            const uint8_t *p = px + 4 * ((size_t)y * SOMVR_OB_IMG_W + (size_t)x);
            if (x < r[0] + SOMVR_OB_CELL && p[3] > 200 && p[2] > 200 && p[0] < 200) blue++;          /* the ring's band */
            if (x >= r[0] + SOMVR_OB_CELL && p[0] == 255 && p[1] == 255 && p[2] == 255) white++; /* the label's ink */
            if (x >= r[0] + SOMVR_OB_CELL && x < r[0] + SOMVR_OB_CELL + 50 && p[3] > 200 && p[2] > 200 && p[0] < 200)
                button++; /* (the strike's A button would be blue here) */
        }
    CHECK(blue > 2000 && white > 400 && button == 0, "the ring (%d blue px), the label (%d white px), no A (%d)", blue, white,
          button);
    float head[3] = {0, 1.7f, 0}, pos[3] = {0.4f, 1.3f, -1.8f};
    float ring = somvr_ob_strike_ring(1.85f, 0.7f, 8.0f);
    somvr_ob_place p = somvr_ob_strike(head, pos, ring, SOMVR_OB_CELL_FINISH);
    CHECK(p.kind == SOMVR_OBK_WORLD && p.cell == SOMVR_OB_CELL_FINISH && fabsf(p.size[1] - ring * 128 / 108.0f) < 1e-4f,
          "placed as the strike's chest ring (the ring %.2f m, the quad %.2f x %.2f)", ring, p.size[0], p.size[1]);
    CHECK(SOMVR_OBF_FINISH > SOMVR_OBF_TIMER && (SOMVR_OBF_FINISH & (SOMVR_OBF_FINISH - 1)) == 0, "its flag a bit of its own");
}

/* build 18: the strike's ground symbol - the ring square and the label part of a strike cell, the ring flat on the
 * ground point, the label standing beside it facing the head, readable at 20 m */
static void test_strike_ground(void) {
    int32_t r[4], r0[4], r1[4];
    somvr_ob_rect(SOMVR_OB_CELL_STRIKE_NO, r);
    somvr_ob_strike_rect(SOMVR_OB_CELL_STRIKE_NO, 0, r0), somvr_ob_strike_rect(SOMVR_OB_CELL_STRIKE_NO, 1, r1);
    CHECK(r0[0] == r[0] && r0[2] == SOMVR_OB_CELL && r0[3] == SOMVR_OB_CELL && r1[0] == r[0] + SOMVR_OB_CELL + 2 &&
              r1[2] == SOMVR_OB_STRIKE_LABEL_PX && r1[0] + r1[2] <= r[0] + r[2],
          "the parts: the ring square at %d, the label at %d (%d wide) inside the cell", r0[0], r1[0], r1[2]);
    float g[3] = {1.0f, -1.6f, -10.0f}, head[3] = {0, 0, 0};
    float near_head[3] = {1.0f, 0.0f, -6.0f}; /* 4 m away, 1.6 m over it: seen at 22 deg: tilted 2 deg */
    float up_head[3] = {1.0f, 0.4f, -8.0f};   /* 2 m away, 2 m over it: seen at 45 deg: flat */
    somvr_ob_place f = somvr_ob_strike_floor(up_head, g, 0.45f, SOMVR_OB_CELL_STRIKE);
    const float *q = f.pose;
    float zy = 2 * (q[1] * q[2] - q[3] * q[0]);
    CHECK(f.kind == SOMVR_OBK_WORLD && fabsf(zy - 1) < 1e-5f && fabsf(f.pose[5] - (-1.58f)) < 1e-5f && f.pose[4] == 1.0f &&
              fabsf(f.size[0] - 0.9f * 128 / 108) < 1e-4f && f.off_deg == 0,
          "seen from above: the ring flat 2 cm over the ground, %.3f m square (its outer edge 0.45 m from the centre)", f.size[0]);
    /* far: tilted toward the head so it is seen at the minimum angle; the face's normal leans to the head, level right */
    float far_heads[2][3] = {{1.0f, 0.0f, 10.0f}, {1.0f, 0.0f, -6.0f}};
    for (int i = 0; i < 2; i++) {
        const float *hh = i ? near_head : far_heads[0];
        f = somvr_ob_strike_floor(hh, g, 0.45f, SOMVR_OB_CELL_STRIKE);
        q = f.pose;
        float zx = 2 * (q[0] * q[2] + q[3] * q[1]), zy2 = 2 * (q[1] * q[2] - q[3] * q[0]), zz = 1 - 2 * (q[0] * q[0] + q[1] * q[1]);
        float xy = 2 * (q[0] * q[1] + q[3] * q[2]);
        float v[3] = {hh[0] - g[0], hh[1] - g[1] - 0.02f, hh[2] - g[2]}, vl = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        float seen = asinf((zx * v[0] + zy2 * v[1] + zz * v[2]) / vl) * 57.29578f; /* the angle between the ray and the plane */
        float elev = atan2f(hh[1] - g[1], sqrtf((hh[0] - g[0]) * (hh[0] - g[0]) + (hh[2] - g[2]) * (hh[2] - g[2]))) * 57.29578f;
        CHECK(fabsf(seen - SOMVR_OB_STRIKE_MIN_VIEW_DEG) < 0.3f && fabsf(f.off_deg - (SOMVR_OB_STRIKE_MIN_VIEW_DEG - elev)) < 0.05f &&
                  fabsf(xy) < 1e-4f && zy2 > 0.9f,
              "%s: tilted %.1f deg, seen at %.1f deg (flat: %.1f), its right edge level", i ? "4 m away" : "20 m away",
              f.off_deg, seen, elev);
    }
    float dist[3] = {5, 10, 20};
    for (int i = 0; i < 3; i++) {
        float gp[3] = {0, -1.6f, -dist[i]};
        somvr_ob_place l = somvr_ob_strike_label(head, gp, 0.45f, SOMVR_OB_CELL_STRIKE);
        float d = sqrtf(l.pose[4] * l.pose[4] + l.pose[5] * l.pose[5] + l.pose[6] * l.pose[6]);
        float deg = 2 * atanf(0.5f * l.size[1] / d) * 57.29578f, xl = l.pose[4] - 0.5f * l.size[0];
        CHECK(l.kind == SOMVR_OBK_WORLD && l.pose[4] > 0.45f && xl > 0.45f && xl < 0.6f &&
                  fabsf(l.pose[5] - (-1.6f + 0.05f + 0.5f * l.size[1])) < 1e-4f && deg >= 2.4f &&
                  fabsf(l.size[0] / l.size[1] - 156.0f / 128.0f) < 1e-4f,
              "at %.0f m: the label right of the ring (its left edge %.2f m from the centre), its foot on the ground, %.2f deg high",
              (double)dist[i], xl, deg);
    }
    CHECK(somvr_ob_strike_floor(head, g, NAN, SOMVR_OB_CELL_STRIKE).kind == SOMVR_OBK_NONE &&
              somvr_ob_strike_label(head, head, 0.45f, SOMVR_OB_CELL_STRIKE).kind == SOMVR_OBK_NONE &&
              somvr_ob_strike_floor(head, g, 0.45f, SOMVR_OB_CELL_RING).kind == SOMVR_OBK_NONE,
          "none: NaN, at the head, another cell");
}

/* build 20: the bow's aim ring (SOMVR_OBF_AIM) - centred on the point, facing the head, deg of the view across at any
 * distance; the gold ring's cell on the world, the strike's ring square on an orc */
static void test_aim_ring(void) {
    float head[3] = {0, 1.7f, 0};
    for (int k = 0; k < 2; k++) {
        float pos[3] = {k ? 0.5f : 4.0f, k ? 1.5f : 1.0f, k ? -2.0f : -20.0f};
        float d = sqrtf((pos[0] - head[0]) * (pos[0] - head[0]) + (pos[1] - head[1]) * (pos[1] - head[1]) +
                        (pos[2] - head[2]) * (pos[2] - head[2]));
        somvr_ob_place p = somvr_ob_aim(head, pos, 2.2f, k, 0);
        float ring = 2 * d * tanf(1.1f * 0.0174533f), want = ring * 128 / (k ? 108.0f : 124.0f);
        const float *q = p.pose;
        float zx = 2 * (q[0] * q[2] + q[3] * q[1]), zy = 2 * (q[1] * q[2] - q[3] * q[0]), zz = 1 - 2 * (q[0] * q[0] + q[1] * q[1]);
        CHECK(p.kind == SOMVR_OBK_WORLD && p.cell == (k ? SOMVR_OB_CELL_STRIKE : SOMVR_OB_CELL_RING) &&
                  fabsf(p.pose[4] - pos[0]) < 1e-5f && fabsf(p.pose[5] - pos[1]) < 1e-5f && fabsf(p.pose[6] - pos[2]) < 1e-5f,
              "aim ring %s: on the point", k ? "(an orc, near)" : "(the world, far)");
        CHECK(fabsf(p.size[0] - want) < 1e-4f && p.size[0] == p.size[1], "aim ring: 2.2 deg at %.1f m = the quad %.3f m (%.3f)",
              d, p.size[0], want);
        CHECK(fabsf(zx - (head[0] - pos[0]) / d) < 1e-3f && fabsf(zy - (head[1] - pos[1]) / d) < 1e-3f &&
                  fabsf(zz - (head[2] - pos[2]) / d) < 1e-3f, "aim ring: facing the head");
    }
    CHECK(somvr_ob_aim(head, head, 2.2f, 0, 0).kind == SOMVR_OBK_NONE &&
              somvr_ob_aim(head, (float[]){0, 1, -5}, NAN, 0, 0).kind == SOMVR_OBK_NONE,
          "aim ring: none at the head or NaN");
}

/* build 21 (re-xr-pipeline.md 82): the bow's sight (SOMVR_OBF_AIM_SIGHT) - the 20c session: build 20's ring too big,
 * its gold out of place. Its own cells: the ring's outer edge deg of the view across (as build 20's),
 * the quad SOMVR_OB_AIM_PX / SOMVR_OB_AIM_RING_PX of that; on the world no gold or yellow texel, soft white at about
 * 80 %, a faint dark edge outside and inside the band, a dot at the centre, the band thin (under 0.15 of the ring's
 * radius); on an orc the same in the strike's blue; nothing outside its square */
static void test_aim_sight(void) {
    float head[3] = {0, 1.7f, 0};
    for (int k = 0; k < 2; k++) {
        float pos[3] = {k ? 0.5f : 4.0f, k ? 1.5f : 1.0f, k ? -2.0f : -20.0f};
        float d = sqrtf((pos[0] - head[0]) * (pos[0] - head[0]) + (pos[1] - head[1]) * (pos[1] - head[1]) +
                        (pos[2] - head[2]) * (pos[2] - head[2]));
        somvr_ob_place p = somvr_ob_aim(head, pos, 1.5f, k, 1);
        float ring = 2 * d * tanf(0.75f * 0.0174533f), want = ring * SOMVR_OB_AIM_PX / (float)SOMVR_OB_AIM_RING_PX;
        CHECK(p.kind == SOMVR_OBK_WORLD && p.cell == (k ? SOMVR_OB_CELL_AIM_HIT : SOMVR_OB_CELL_AIM) &&
                  fabsf(p.pose[4] - pos[0]) < 1e-5f && fabsf(p.pose[5] - pos[1]) < 1e-5f && fabsf(p.pose[6] - pos[2]) < 1e-5f,
              "sight %s: its cell, on the point", k ? "(an orc)" : "(the world)");
        CHECK(fabsf(p.size[0] - want) < 1e-4f && p.size[0] == p.size[1], "sight: 1.5 deg at %.1f m = the quad %.4f m (%.4f)",
              d, p.size[0], want);
    }
    int32_t r[4], rn[4], rh[4];
    somvr_ob_rect(SOMVR_OB_CELL_AIM, r), somvr_ob_rect(SOMVR_OB_CELL_RING_NO, rn), somvr_ob_rect(SOMVR_OB_CELL_AIM_HIT, rh);
    CHECK(r[2] == SOMVR_OB_AIM_PX && r[3] == SOMVR_OB_AIM_PX && r[0] >= rn[0] + rn[2] + 1 && rh[0] >= r[0] + SOMVR_OB_CELL &&
              rh[0] + SOMVR_OB_CELL <= SOMVR_OB_IMG_W && r[1] == 1,
          "sight cells: %d,%d %dx%d and %d after the refused ring's (%d), inside the texture (%d)", r[0], r[1], r[2], r[3],
          rh[0], rn[0], SOMVR_OB_IMG_W);
    uint8_t *px = calloc((size_t)SOMVR_OB_IMG_W * SOMVR_OB_IMG_H, 4);
    somvr_ob_image(px, NULL, "", 0);
    for (int k = 0; k < 2; k++) {
        int32_t c[4];
        somvr_ob_rect(k ? SOMVR_OB_CELL_AIM_HIT : SOMVR_OB_CELL_AIM, c);
        int gold = 0, outside = 0, band = 0, edge_out = 0, edge_in = 0, dot = 0, hole = 0, blue = 0;
        float amax = 0, rmax = 0, rmin = 99;
        for (int y = c[1] - 1; y < c[1] + SOMVR_OB_CELL; y++)
            for (int x = c[0] - 1; x < c[0] + SOMVR_OB_CELL; x++) {
                const uint8_t *t = px + 4 * ((size_t)y * SOMVR_OB_IMG_W + (size_t)x);
                int in = x >= c[0] && y >= c[1] && x < c[0] + c[2] && y < c[1] + c[3];
                if (!in) {
                    outside += t[3] != 0;
                    continue;
                }
                float a = t[3] / 255.0f, R = a > 0 ? t[0] / a : 0, G = a > 0 ? t[1] / a : 0, B = a > 0 ? t[2] / a : 0;
                float dx = x - c[0] + 0.5f - 0.5f * SOMVR_OB_AIM_PX, dy = y - c[1] + 0.5f - 0.5f * SOMVR_OB_AIM_PX;
                float rr = sqrtf(dx * dx + dy * dy);
                if (a > 0.1f && R > B + 30 && G > B + 20) gold++; /* yellow / gold: red and green well over blue */
                if (a > 0.1f && B > R + 40) blue++;
                if (a > 0.02f && rr > 6) rmax = rr > rmax ? rr : rmax, rmin = rr < rmin ? rr : rmin;
                if (rr > 15.5f && rr < 16.9f && a > 0.7f && a < 0.95f && R > 120 && G > 150 && B > 150) band++; /* the band */
                if (rr > 18.0f && rr < 18.8f && a > 0.25f && R < 40 && G < 40 && B < 40) edge_out++;
                if (rr > 13.8f && rr < 14.5f && a > 0.25f && R < 40 && G < 40 && B < 40) edge_in++;
                if (rr < 1.0f && a > 0.7f && G > 150) dot++;
                if (rr > 4 && rr < 12 && t[3]) hole++;
                amax = a > amax ? a : amax;
            }
        CHECK(!outside && !hole && dot && band > 40 && edge_out > 40 && edge_in > 30,
              "sight %s: a band (%d), a dark edge outside (%d) and inside (%d), the dot (%d), clear inside (%d) and outside "
              "its square (%d)", k ? "blue" : "white", band, edge_out, edge_in, dot, hole, outside);
        CHECK(rmax <= 0.5f * SOMVR_OB_AIM_RING_PX + 0.8f && rmin >= 0.5f * SOMVR_OB_AIM_RING_PX * 0.68f &&
                  (rmax - rmin) < 0.36f * 0.5f * SOMVR_OB_AIM_RING_PX && amax < 0.95f,
              "sight: thin (the ring %.1f .. %.1f of %.1f), never opaque (%.2f)", rmin, rmax, 0.5f * SOMVR_OB_AIM_RING_PX, amax);
        CHECK(k ? blue > 40 && !gold : !gold && !blue, "sight %s: no gold texel (%d), blue %d", k ? "on an orc" : "on the world",
              gold, blue);
    }
    /* build 20's world ring (ring20) is the gold one - the check above fails on it */
    int32_t c3[4];
    somvr_ob_rect(somvr_ob_aim(head, (float[]){0, 1, -5}, 3.0f, 0, 0).cell, c3);
    int gold20 = 0;
    for (int y = c3[1]; y < c3[1] + c3[3]; y++)
        for (int x = c3[0]; x < c3[0] + c3[2]; x++) {
            const uint8_t *t = px + 4 * ((size_t)y * SOMVR_OB_IMG_W + (size_t)x);
            float a = t[3] / 255.0f;
            gold20 += a > 0.1f && t[0] / a > t[2] / a + 30 && t[1] / a > t[2] / a + 20;
        }
    CHECK(gold20 > 100, "ring20 (build 20's world ring) is gold: %d texels", gold20);
    free(px);
}

int main(void) {
    test_aim_ring();
    test_aim_sight();
    test_strike_ground();
    test_strike_mark();
    test_readout();      /* build 40 */
    test_struggle_cue(); /* build 41 */
    test_finish_cell(); /* build 37 */
    test_match_text();
    test_line();
    test_ahead();
    test_edges();
    test_abi();
    test_image();
    printf("test_objective: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
