/* test_zprobe.c - build 41 (som-b41-climb4): somvr_zprobe.h - the ABI's round trips, the projection (hd_eye_matrix's
 * mapping: OpenXR's fov, y down) and the verdict (the 11-04 mark 222445_15's undrawn top against a real one) */
#include "../somvr_zprobe.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_n, g_fail;
#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        g_n++;                                                                                                         \
        if (!(c)) {                                                                                                    \
            g_fail++;                                                                                                  \
            printf("FAIL line %d: ", __LINE__);                                                                        \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)
#define NEAR(a, b, e) (fabsf((float)(a) - (float)(b)) <= (e))

static somvr_zprobe_shared g_z;

static void test_abi(void) {
    CHECK(!somvr_zprobe_ok(&g_z), "an empty object is not ok");
    somvr_zprobe_init(&g_z, 42);
    CHECK(somvr_zprobe_ok(&g_z) && g_z.pid == 42 && g_z.size == 872, "init: ok, pid, size %u", g_z.size);
    float p[3][3] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
    somvr_zprobe_put(&g_z, 100, 7, 3, (const float(*)[3])p);
    somvr_zprobe_req r;
    memset(&r, 0, sizeof r);
    CHECK(somvr_zprobe_get(&g_z, 100, &r) && r.n == 3 && r.id == 7 && r.p[2][1] == 8 &&
              atomic_load(&r.seq) == 100,
          "put/get: frame 100's request (n %u id %u)", r.n, r.id);
    CHECK(!somvr_zprobe_get(&g_z, 100 + SOMVR_NREC, &r) && !somvr_zprobe_get(&g_z, 101, &r) && !somvr_zprobe_get(&g_z, 0, &r),
          "get: another frame's slot, a frame not written, frame 0: none");
    somvr_zprobe_put(&g_z, 100 + SOMVR_NREC, 8, 0, NULL);
    CHECK(!somvr_zprobe_get(&g_z, 100, &r) && !somvr_zprobe_get(&g_z, 100 + SOMVR_NREC, &r),
          "overwritten: the old frame gone; a request of no points is none");
    somvr_zprobe_ans a, b;
    memset(&a, 0, sizeof a), memset(&b, 0, sizeof b);
    CHECK(!somvr_zprobe_read(&g_z, &b), "read: no answer yet");
    a.frame = 99, a.id = 7, a.n = 2, a.pt[1][1].state = SOMVR_ZP_READ, a.pt[1][1].near_z = 3.5f;
    somvr_zprobe_answer(&g_z, &a);
    CHECK(somvr_zprobe_read(&g_z, &b) && b.frame == 99 && b.id == 7 && b.n == 2 && b.pt[1][1].near_z == 3.5f &&
              atomic_load(&g_z.n_ans) == 1,
          "answer/read: the frame %llu id %u", (unsigned long long)b.frame, b.id);
    atomic_store(&g_z.ans.seq, 3); /* being written */
    CHECK(!somvr_zprobe_read(&g_z, &b), "read: a torn answer refused");
    atomic_store(&g_z.ans.seq, 2);
}

static void test_project(void) {
    /* an eye at (0, 1.6, 0) looking down -Z, the Quest 2's fov (about 52 deg out, 49 in, 47 up, 54 down) */
    const float q[4] = {0, 0, 0, 1}, p[3] = {0, 1.6f, 0};
    const float fov[4] = {-0.907f, 0.855f, 0.820f, -0.942f};
    float px, py, z;
    float ahead[3] = {0, 1.6f, -2};
    int in = somvr_zp_project(q, p, fov, 2000, 2200, ahead, &px, &py, &z);
    float u0 = (0 - tanf(fov[0])) / (tanf(fov[1]) - tanf(fov[0])), v0 = tanf(fov[2]) / (tanf(fov[2]) - tanf(fov[3]));
    CHECK(in && NEAR(z, 2, 1e-4) && NEAR(px, u0 * 2000, 0.01) && NEAR(py, v0 * 2200, 0.01),
          "straight ahead 2 m: in %d, z %.3f, pixel %.1f %.1f (want %.1f %.1f)", in, z, px, py, u0 * 2000, v0 * 2200);
    float up[3] = {0, 2.6f, -2}, right[3] = {1, 1.6f, -2};
    float pyu, pxr, d1, d2;
    somvr_zp_project(q, p, fov, 2000, 2200, up, &d1, &pyu, &z);
    somvr_zp_project(q, p, fov, 2000, 2200, right, &pxr, &d2, &z);
    CHECK(pyu < py && pxr > px, "y down, x right: up %.0f < %.0f, right %.0f > %.0f", pyu, py, pxr, px);
    float behind[3] = {0, 1.6f, 2}, aside[3] = {5, 1.6f, -1};
    CHECK(!somvr_zp_project(q, p, fov, 2000, 2200, behind, &px, &py, &z), "behind the eye: not in the view");
    CHECK(!somvr_zp_project(q, p, fov, 2000, 2200, aside, &px, &py, &z) && px > 2000, "far to the right: outside (%.0f)", px);
    /* the eye turned 90 deg left (yaw +90 about +Y): a point at -X is ahead */
    const float ql[4] = {0, sinf(0.7853982f), 0, cosf(0.7853982f)};
    float left[3] = {-3, 1.6f, 0};
    in = somvr_zp_project(ql, p, fov, 2000, 2200, left, &px, &py, &z);
    CHECK(in && NEAR(z, 3, 1e-3) && NEAR(px, u0 * 2000, 0.05), "turned left: the point at -X ahead (z %.3f, x %.1f)", z, px);
    /* looking down 45 deg at a top's landing 1 m ahead and 1 m under the eye */
    const float qd[4] = {sinf(-0.3926991f), 0, 0, cosf(-0.3926991f)};
    float land[3] = {0, 0.6f, -1};
    in = somvr_zp_project(qd, p, fov, 2000, 2200, land, &px, &py, &z);
    CHECK(in && NEAR(z, sqrtf(2.0f), 1e-3) && NEAR(py, v0 * 2200, 0.05), "looking down 45 deg: the landing on the axis (z %.3f)",
          z);
}

/* one eye's reading of point i: the point at z, the nearest drawn at nz (0 = the sky) */
static void rd(somvr_zprobe_ans *a, int e, int i, float z, float nz) {
    a->pt[e][i].state = SOMVR_ZP_READ, a->pt[e][i].z = z, a->pt[e][i].near_z = nz, a->pt[e][i].far_z = nz;
}

static void test_verdict(void) {
    somvr_zprobe_ans a;
    int r, d;
    float g;
    /* mark 222445_15: the eyes 46 cm over the hull's top, the landing ~1 m off; behind it the ground 6.8 m down */
    memset(&a, 0, sizeof a), a.n = 5;
    for (int i = 0; i < 5; i++) rd(&a, 0, i, 1.0f + 0.05f * (float)i, 9.0f), rd(&a, 1, i, 1.0f + 0.05f * (float)i, 9.1f);
    int v = somvr_zp_verdict(&a, 0.35f, 3, &r, &d, &g);
    CHECK(v == SOMVR_ZP_UNDRAWN && r == 5 && d == 0 && g > 7.5f, "the hull's top: undrawn (read %d, drawn %d, gap %.2f)", r, d, g);
    /* the sky beyond it: the same */
    memset(&a, 0, sizeof a), a.n = 5;
    for (int i = 0; i < 5; i++) rd(&a, 0, i, 1.2f, 0);
    CHECK(somvr_zp_verdict(&a, 0.35f, 3, &r, &d, &g) == SOMVR_ZP_UNDRAWN && g > 1e8f, "the sky behind: undrawn");
    /* a real top: the drawn surface at the point (+-3 cm), or grass in front of it */
    memset(&a, 0, sizeof a), a.n = 5;
    for (int i = 0; i < 5; i++) rd(&a, 0, i, 1.0f, i == 2 ? 0.96f : 1.03f);
    CHECK(somvr_zp_verdict(&a, 0.35f, 3, &r, &d, &g) == SOMVR_ZP_DRAWN && d == 5, "a drawn top: drawn (%d)", d);
    /* one point drawn (the centre), the four round it past a narrow top's edges: drawn */
    memset(&a, 0, sizeof a), a.n = 5;
    for (int i = 0; i < 5; i++) rd(&a, 1, i, 1.0f, i == 0 ? 1.1f : 8.0f);
    CHECK(somvr_zp_verdict(&a, 0.35f, 3, &r, &d, &g) == SOMVR_ZP_DRAWN && d == 1, "a narrow top: drawn by its centre");
    /* a top seen from under its drawn lip: the face in front of every point */
    memset(&a, 0, sizeof a), a.n = 5;
    for (int i = 0; i < 5; i++) rd(&a, 0, i, 1.4f, 0.6f);
    CHECK(somvr_zp_verdict(&a, 0.35f, 3, &r, &d, &g) == SOMVR_ZP_DRAWN, "from under a drawn lip: drawn (the face in front)");
    /* drawn 30 cm past the point (a collision top over the drawn rock, seen at a slant): within the margin */
    memset(&a, 0, sizeof a), a.n = 5;
    for (int i = 0; i < 5; i++) rd(&a, 0, i, 1.0f, 1.3f);
    CHECK(somvr_zp_verdict(&a, 0.35f, 3, &r, &d, &g) == SOMVR_ZP_DRAWN, "drawn 30 cm past it: drawn");
    /* out of the view, or only two points read: unknown */
    memset(&a, 0, sizeof a), a.n = 5;
    for (int i = 0; i < 5; i++) a.pt[0][i].state = a.pt[1][i].state = SOMVR_ZP_OUT;
    CHECK(somvr_zp_verdict(&a, 0.35f, 3, &r, &d, &g) == SOMVR_ZP_UNKNOWN && r == 0, "out of the view: unknown");
    rd(&a, 0, 0, 1.0f, 9.0f), rd(&a, 1, 1, 1.0f, 9.0f);
    CHECK(somvr_zp_verdict(&a, 0.35f, 3, &r, &d, &g) == SOMVR_ZP_UNKNOWN && r == 2, "two read: unknown (%d)", r);
    rd(&a, 1, 4, 1.0f, 9.0f);
    CHECK(somvr_zp_verdict(&a, 0.35f, 3, &r, &d, &g) == SOMVR_ZP_UNDRAWN && r == 3, "three read, none drawn: undrawn");
    /* one eye drawn, the other not (a thin post in one eye's line): drawn */
    memset(&a, 0, sizeof a), a.n = 3;
    for (int i = 0; i < 3; i++) rd(&a, 0, i, 1.0f, 9.0f), rd(&a, 1, i, 1.0f, i == 1 ? 0.9f : 9.0f);
    CHECK(somvr_zp_verdict(&a, 0.35f, 3, &r, &d, &g) == SOMVR_ZP_DRAWN && d == 1, "one eye drawn: drawn");
}

int main(void) {
    test_abi();
    test_project();
    test_verdict();
    printf("test_zprobe: %d checks, %d failures\n", g_n, g_fail);
    return g_fail != 0;
}
