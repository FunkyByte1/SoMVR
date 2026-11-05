/* test_cues.c - build 37 (som-b37-cues, docs/re-xr-pipeline.md 169): the pure part of the cues for what the hidden
 * prompts said (somvr_cues.h) and the threat texture's new rows (somvr_threat.h: the awareness marks, the captain's arc) */
#include "../somvr_cues.h"
#include "../somvr_threat.h"
#include "../somvr_menus.h"

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

static void test_levels(void) {
    somvr_cu_aw_cfg c;
    somvr_cu_aw_defaults(&c);
    CHECK(somvr_cu_aw_level(&c, 0, 0, 0) == SOMVR_CU_NONE, "a calm orc: no mark");
    CHECK(somvr_cu_aw_level(&c, 1, 0, 0) == SOMVR_CU_WHITE, "state 1: white");
    CHECK(somvr_cu_aw_level(&c, 2, 0, 0) == SOMVR_CU_AMBER && somvr_cu_aw_level(&c, 4, 0, 0) == SOMVR_CU_RED,
          "state 2 amber (it looks), 4 red (it fights: boot 205033's whole fight)");
    CHECK(somvr_cu_aw_level(&c, 3, 0, 0) == SOMVR_CU_RED && somvr_cu_aw_level(&c, 0, 1, 0) == SOMVR_CU_RED,
          "state 3 or fighting Talion: red");
    CHECK(somvr_cu_aw_level(&c, 3, 1, 1) == SOMVR_CU_NONE && somvr_cu_aw_level(&c, -1, 0, 0) == SOMVR_CU_NONE &&
              somvr_cu_aw_level(&c, 9, 0, 0) == SOMVR_CU_NONE,
          "never on a dead orc; an unread state none");
    CHECK(somvr_cu_aw_parse_kinds("0,2,2,3,1", &c) && c.lvl[1] == 2 && c.lvl[4] == 1 && c.lvl[5] == 0, "the kinds parsed");
    CHECK(!somvr_cu_aw_parse_kinds("0,4", &c) && !somvr_cu_aw_parse_kinds("0,1,", &c) && !somvr_cu_aw_parse_kinds("x", &c) &&
              !somvr_cu_aw_parse_kinds("0,1,2,3,0,0,0,0,0", &c) && c.lvl[1] == 2,
          "bad kinds refused, the table kept");
}

static void test_fade(void) {
    somvr_cu_aw_cfg c;
    somvr_cu_aw_defaults(&c);
    somvr_cu_aw_mark m = {0};
    double t = 100;
    float dt = 1.0f / 90;
    CHECK(somvr_cu_aw_step(&c, &m, SOMVR_CU_WHITE, 1, t, dt) == 1 && m.shown == SOMVR_CU_WHITE && m.fade > 0 && m.fade < 0.1f,
          "white comes in (fade %.3f)", (double)m.fade);
    for (int i = 0; i < 30; i++) t += dt, somvr_cu_aw_step(&c, &m, SOMVR_CU_WHITE, 1, t, dt);
    CHECK(m.fade == 1.0f, "in after 0.25 s (fade %.3f)", (double)m.fade);
    somvr_cu_aw_step(&c, &m, SOMVR_CU_AMBER, 1, t += dt, dt);
    CHECK(m.shown == SOMVR_CU_AMBER && m.fade == 1.0f, "white -> amber at once");
    somvr_cu_aw_step(&c, &m, SOMVR_CU_RED, 1, t += dt, dt);
    CHECK(m.shown == SOMVR_CU_RED, "amber -> red");
    for (int i = 0; i < 2 * 90; i++) t += dt, somvr_cu_aw_step(&c, &m, SOMVR_CU_RED, 1, t, dt);
    CHECK(m.shown == SOMVR_CU_RED && m.fade == 1.0f, "red shows for its 3 s");
    for (int i = 0; i < 2 * 90; i++) t += dt, somvr_cu_aw_step(&c, &m, SOMVR_CU_RED, 1, t, dt);
    CHECK(m.fade == 0 && m.shown == SOMVR_CU_NONE, "then red is hidden (the flat game's auto-hide; fade %.2f)", (double)m.fade);
    somvr_cu_aw_step(&c, &m, SOMVR_CU_AMBER, 1, t += dt, dt);
    for (int i = 0; i < 40; i++) t += dt, somvr_cu_aw_step(&c, &m, SOMVR_CU_AMBER, 1, t, dt);
    somvr_cu_aw_step(&c, &m, SOMVR_CU_RED, 1, t += dt, dt);
    CHECK(m.shown == SOMVR_CU_RED && m.fade == 1.0f, "red again after amber: shown again");
    for (int i = 0; i < 20; i++) t += dt, somvr_cu_aw_step(&c, &m, SOMVR_CU_NONE, 1, t, dt);
    CHECK(m.shown == SOMVR_CU_RED && m.fade > 0.5f && m.fade < 0.65f, "calm again: red fades out in its colour (%.2f)",
          (double)m.fade);
    for (int i = 0; i < 40; i++) t += dt, somvr_cu_aw_step(&c, &m, SOMVR_CU_NONE, 1, t, dt);
    CHECK(m.fade == 0 && m.shown == SOMVR_CU_NONE, "out after 0.5 s");
    somvr_cu_aw_mark v = {0};
    for (int i = 0; i < 40; i++) somvr_cu_aw_step(&c, &v, SOMVR_CU_AMBER, 0, t += dt, dt);
    CHECK(v.fade == 0 && v.lvl == SOMVR_CU_AMBER, "out of view: no mark (the level kept)");
    CHECK(fabsf(somvr_cu_aw_size(&c, 10) - 0.279f) < 0.01f && somvr_cu_aw_size(&c, 1) == c.min_m &&
              somvr_cu_aw_size(&c, 100) == c.max_m,
          "the size: 1.6 deg at 10 m, clamped near and far (%.3f)", (double)somvr_cu_aw_size(&c, 10));
}

static void test_timer(void) {
    somvr_cu_tm_cfg c;
    somvr_cu_tm_defaults(&c);
    somvr_cu_tm s = {0};
    int pop = 0, ev[8] = {0};
    double t = 50;
    /* the Queen of the Shore's 1:30: one tick a second */
    for (int k = 0; k <= 92; k++, t += 1.0) {
        float left = 90.0f - (float)k;
        int e = left >= 0 ? somvr_cu_tm_tick(&c, &s, left, t) : 0;
        ev[e]++;
        int f = somvr_cu_tm_frame(&s, t + 0.5, 2.5, &pop);
        ev[f]++;
        if (k == 2) CHECK(pop, "the start pops (2 s in)");
        if (k == 6) CHECK(!pop, "back to the watch 6 s in");
        if (k == 61) CHECK(pop, "29 s left: popped (the 30 s mark)");
        if (k == 66) CHECK(!pop, "24 s left: back");
        if (k == 79) CHECK(!pop, "11 s left: back");
        if (k == 81) CHECK(pop, "9 s left: up to the end");
        if (k == 90) CHECK(pop, "0 s left: up");
    }
    CHECK(ev[SOMVR_CU_TM_START] == 1 && ev[SOMVR_CU_TM_MARK] == 1 && ev[SOMVR_CU_TM_LAST] == 1,
          "one start, one 30 s mark, one 10 s mark (%d %d %d)", ev[1], ev[2], ev[3]);
    for (int k = 0; k < 4; k++) somvr_cu_tm_frame(&s, t + k, 2.5, &pop);
    CHECK(!s.on && !pop, "no tick for 2.5 s: over, no popup");
    /* a 20 s dash (The One Truth): starts under 30 s - the start pops, 30 s does not, 10 s does */
    memset(&s, 0, sizeof s), memset(ev, 0, sizeof ev);
    for (int k = 0; k <= 20; k++, t += 1.0) ev[somvr_cu_tm_tick(&c, &s, 20.0f - (float)k, t)]++;
    CHECK(ev[SOMVR_CU_TM_START] == 1 && ev[SOMVR_CU_TM_MARK] == 0 && ev[SOMVR_CU_TM_LAST] == 1, "a 20 s timer: start and 10 s");
    /* a timer set again (a checkpoint adds time) starts again */
    CHECK(somvr_cu_tm_tick(&c, &s, 60.0f, t + 1) == SOMVR_CU_TM_START && !s.last, "a longer timer: a new start");
    CHECK(somvr_cu_tm_tick(&c, &s, NAN, t + 2) == SOMVR_CU_TM_NONE && s.left == 60.0f, "a NaN tick ignored");
}

static void test_reach(void) {
    CHECK(somvr_cu_reach_level(200, 40, 0) == 0.3f && fabsf(somvr_cu_reach_level(80, 40, 0) - 0.55f) < 1e-4f &&
              fabsf(somvr_cu_reach_level(40, 40, 0) - 0.8f) < 1e-4f && somvr_cu_reach_level(30, 40, 1) == 1.0f &&
              somvr_cu_reach_level(NAN, 40, 0) == 0.3f,
          "the glow brightens as the hand comes in");
    CHECK(somvr_cu_ring_cell(200, 40, 0) == 1 && somvr_cu_ring_cell(80, 40, 0) == 8 && somvr_cu_ring_cell(41, 40, 0) == 15 &&
              somvr_cu_ring_cell(30, 40, 1) == 16,
          "the ring tightens as the hand comes in (%d %d %d)", somvr_cu_ring_cell(200, 40, 0), somvr_cu_ring_cell(80, 40, 0),
          somvr_cu_ring_cell(41, 40, 0));
}

static void test_use(void) {
    CHECK(somvr_cu_match(somvr_cu_use_recs("Combat_Activate_SpiritStone"), "FT_BG") &&
              somvr_cu_match(somvr_cu_use_recs("Combat_Activate_SpiritStone"), "RessurrectionStone"),
          "the forge tower's stone: an unsynced tower's RessurrectionStone, FT_*");
    CHECK(somvr_cu_match(somvr_cu_use_recs("Combat_Activate_Collectable"), "Collectible_Artifact") &&
              somvr_cu_match(somvr_cu_use_recs("Combat_Activate_Collectable"), "Collectible_Glyph") &&
              !somvr_cu_match(somvr_cu_use_recs("Combat_Activate_Collectable"), "Collectible_Flower_G"),
          "a collectable: artifacts and Ithildin, not herbs");
    CHECK(somvr_cu_match(somvr_cu_use_recs("Combat_Activate_Ammo"), "Ammo") &&
              !somvr_cu_match(somvr_cu_use_recs("Combat_Activate_Ammo"), "Ammo2"),
          "arrows: the bundle, exactly");
    CHECK(somvr_cu_match(somvr_cu_use_recs("Combat_Activate_Poison"), "Explosive") &&
              somvr_cu_match(somvr_cu_use_recs("Combat_Activate_Free"), "LockedGate"),
          "an unknown use: the camp's things");
    CHECK(!somvr_cu_match("", "FT_BG") && !somvr_cu_match("FT_*", "") && somvr_cu_match("A,B*", "Bx") &&
              !somvr_cu_match("A,B*", "AB"),
          "the matcher");
}

static void test_rows(void) {
    static uint8_t px[SOMVR_TH_IMG_W * SOMVR_TH_IMG_H * 4];
    somvr_th_image(px, 0);
    CHECK(somvr_th_glow_row(SOMVR_THK_MARK) == SOMVR_TH_ROW_MARK_W &&
              somvr_th_glow_row(SOMVR_THK_MARK | SOMVR_THK_MARK_AMBER) == SOMVR_TH_ROW_MARK_A &&
              somvr_th_glow_row(SOMVR_THK_MARK | SOMVR_THK_MARK_RED) == SOMVR_TH_ROW_MARK_R &&
              somvr_th_glow_row(SOMVR_THK_MARK | SOMVR_THK_ARC) == SOMVR_TH_ROW_ARC &&
              somvr_th_glow_row(SOMVR_THK_BLUE) == SOMVR_TH_ROW_BLUE && somvr_th_glow_row(0) == SOMVR_TH_ROW_GLOW,
          "the rows by kind");
    CHECK(somvr_th_glow_cell(SOMVR_THK_MARK | SOMVR_THK_ARC | 5u << SOMVR_THK_ARC_SHIFT, 0.1f) == 5 &&
              somvr_th_glow_cell(SOMVR_THK_MARK | SOMVR_THK_ARC | 99u << SOMVR_THK_ARC_SHIFT, 0.1f) == SOMVR_TH_LEVELS &&
              somvr_th_glow_cell(SOMVR_THK_MARK | SOMVR_THK_ARC, 1.0f) == 0 &&
              somvr_th_glow_cell(SOMVR_THK_MARK, 1.0f) == SOMVR_TH_LEVELS,
          "the arc's cell is its health steps; a mark's its level");
    int32_t rc[4];
    /* the mark: its middle opaque in its colour, its point down, the corners clear */
    for (int row = SOMVR_TH_ROW_MARK_W; row <= SOMVR_TH_ROW_MARK_R; row++) {
        somvr_th_rect(row, SOMVR_TH_LEVELS, rc);
        const uint8_t *mid = px + 4 * ((size_t)(rc[1] + rc[3] * 45 / 100) * SOMVR_TH_IMG_W + (size_t)(rc[0] + rc[2] / 2));
        const uint8_t *tip = px + 4 * ((size_t)(rc[1] + rc[3] * 80 / 100) * SOMVR_TH_IMG_W + (size_t)(rc[0] + rc[2] / 2));
        const uint8_t *topc = px + 4 * ((size_t)(rc[1] + rc[3] * 22 / 100) * SOMVR_TH_IMG_W + (size_t)(rc[0] + 3));
        const uint8_t *low = px + 4 * ((size_t)(rc[1] + rc[3] * 80 / 100) * SOMVR_TH_IMG_W + (size_t)(rc[0] + 3));
        CHECK(mid[3] > 200 && tip[3] > 100 && topc[3] == 0 && low[3] == 0 && rc[1] + rc[3] <= SOMVR_TH_IMG_H - 1,
              "row %d: the triangle (mid a %d, tip %d, corners %d %d)", row, mid[3], tip[3], topc[3], low[3]);
        if (row == SOMVR_TH_ROW_MARK_W) CHECK(mid[0] > 200 && mid[1] > 200 && mid[2] > 200, "white");
        if (row == SOMVR_TH_ROW_MARK_A) CHECK(mid[0] > 200 && mid[1] > 120 && mid[1] < 180 && mid[2] < 40, "amber");
        if (row == SOMVR_TH_ROW_MARK_R) CHECK(mid[0] > 200 && mid[1] < 30, "red");
    }
    somvr_th_rect(SOMVR_TH_ROW_MARK_W, SOMVR_TH_LEVELS / 2, rc);
    const uint8_t *half = px + 4 * ((size_t)(rc[1] + rc[3] * 45 / 100) * SOMVR_TH_IMG_W + (size_t)(rc[0] + rc[2] / 2));
    CHECK(half[3] > 90 && half[3] < 130, "a half cell: half its alpha (%d)", half[3]);
    /* the arc: cell 8 lit on its left half, dark on its right */
    somvr_th_rect(SOMVR_TH_ROW_ARC, SOMVR_TH_LEVELS / 2, rc);
    int lit_l = 0, lit_r = 0, any_r = 0;
    for (int j = 0; j < rc[3]; j++)
        for (int i = 0; i < rc[2]; i++) {
            const uint8_t *q = px + 4 * ((size_t)(rc[1] + j) * SOMVR_TH_IMG_W + (size_t)(rc[0] + i));
            if (q[3] > 150 && q[0] > 150) *(i < rc[2] / 2 ? &lit_l : &lit_r) += 1;
            if (i > rc[2] / 2 + 2 && q[3] > 0) any_r++;
        }
    CHECK(lit_l > 20 && lit_r == 0 && any_r > 10, "the arc half lit from the left (%d lit left, %d right, %d track)", lit_l,
          lit_r, any_r);
    /* build 37: the reach ring - wide at cell 1, tight at 16; the heal band green */
    int32_t r1[4], r16[4], rh2[4];
    somvr_th_rect(SOMVR_TH_ROW_RING, 1, r1), somvr_th_rect(SOMVR_TH_ROW_RING, SOMVR_TH_LEVELS, r16);
    somvr_th_rect(SOMVR_TH_ROW_HEAL, SOMVR_TH_LEVELS, rh2);
    const uint8_t *w1 = px + 4 * ((size_t)(r1[1] + 16) * SOMVR_TH_IMG_W + (size_t)(r1[0] + 1)),
                  *w16 = px + 4 * ((size_t)(r16[1] + 16) * SOMVR_TH_IMG_W + (size_t)(r16[0] + 11)),
                  *c16 = px + 4 * ((size_t)(r16[1] + 16) * SOMVR_TH_IMG_W + (size_t)(r16[0] + 16)),
                  *hb = px + 4 * ((size_t)(rh2[1] + 16) * SOMVR_TH_IMG_W + (size_t)(rh2[0] + 0));
    CHECK(w1[3] > 100 && w16[3] > 100 && c16[3] == 0 && somvr_th_glow_row(SOMVR_THK_RING) == SOMVR_TH_ROW_RING &&
              somvr_th_glow_cell(SOMVR_THK_RING | 7u << SOMVR_THK_ARC_SHIFT, 0.1f) == 7,
          "the reach ring: wide at 1 (a %d at x 1), tight at 16 (a %d at x 11), its middle clear (%d)", w1[3], w16[3], c16[3]);
    CHECK(hb[1] > hb[0] && hb[1] > hb[2] && hb[3] > 50, "the heal band green (%d %d %d a %d)", hb[0], hb[1], hb[2], hb[3]);
    int prem = 1;
    for (size_t i = (size_t)SOMVR_TH_IMG_W * SOMVR_TH_ROW_MARK_W * SOMVR_TH_STRIDE; i < (size_t)SOMVR_TH_IMG_W * SOMVR_TH_IMG_H; i++)
        prem &= px[4 * i + 3] >= px[4 * i] && px[4 * i + 3] >= px[4 * i + 1] && px[4 * i + 3] >= px[4 * i + 2];
    CHECK(prem, "the marks and the arc premultiplied (each colour at most the alpha)");
}

/* the layer's part (somvr_menus.c): the timer's rect cut out of a band copy, the watch rim's spans */
static void test_layer_parts(void) {
    int32_t out[4][6];
    const int32_t r[6] = {440, 10, 2000, 400, 1040, 290}; /* the band's top tile: src 440,10 -> dst 2000,400 */
    const int hole[4] = {850, 12, 230, 95};               /* the timer */
    int n = somvr_rect_cut(r, hole, out), area = 0, ok = 1;
    for (int i = 0; i < n; i++) {
        area += out[i][4] * out[i][5];
        ok &= out[i][2] - out[i][0] == 1560 && out[i][3] - out[i][1] == 390; /* the same src -> dst offset */
        ok &= !(out[i][0] < 850 + 230 && out[i][0] + out[i][4] > 850 && out[i][1] < 12 + 95 && out[i][1] + out[i][5] > 12);
    }
    CHECK(n == 4 && area == 1040 * 290 - 230 * 95 && ok, "the timer cut out: 4 pieces, its area gone (%d, %d)", n, area);
    const int far[4] = {0, 600, 50, 50};
    CHECK(somvr_rect_cut(r, far, out) == 1 && !memcmp(out[0], r, sizeof r), "no overlap: unchanged");
    const int edge[4] = {400, 0, 100, 400}; /* over its left end: only the right part stays */
    CHECK(somvr_rect_cut(r, edge, out) == 1 && out[0][0] == 500 && out[0][4] == 980 && out[0][2] == 2060, "a cut at its end");
    int32_t sp[1024][3];
    int k = somvr_ring_spans(100, 50, 300, 8, sp, 1024), inside = 1, rows = 0;
    for (int i = 0; i < k; i++) {
        for (int x = sp[i][0]; x < sp[i][0] + sp[i][2]; x++) {
            float dx = (float)x + 0.5f - 250, dy = (float)sp[i][1] + 0.5f - 200, d = sqrtf(dx * dx + dy * dy);
            inside &= d <= 151.5f && d >= 140.0f;
        }
        rows += i == 0 || sp[i][1] != sp[i - 1][1];
    }
    CHECK(k > 500 && inside && rows >= 296, "the rim: a ring 8 px wide round the map (%d spans, %d rows)", k, rows);
}

int main(void) {
    test_layer_parts();
    test_levels();
    test_fade();
    test_timer();
    test_use();
    test_reach();
    test_rows();
    printf("test_cues: %d checks, %d failures\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
