/* test_live.c - SOMVR_LIVE's pure parts (../somvr_live.h; docs/re-xr-pipeline.md 162): the card's text and picture,
 * the calm gate, the live folder's formats, and the VR menu's Test page patch box (../somvr_vrmenu.h). */
#include "../somvr_live.h"
#include "../somvr_vrmenu.h"

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
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

static somvr_font F;

static void test_wrap(void) {
    char l[8][SOMVR_LV_COLS + 1];
    int more = -1;
    int n = somvr_lv_wrap("Climbing down from a ledge should feel smoother now, try the tower by the camp.", l, 6, 29,
                          &more);
    CHECK(n == 3 && !more, "3 lines: %d (more %d)", n, more);
    int fit = 1;
    for (int i = 0; i < n; i++) fit &= strlen(l[i]) <= 29 && l[i][0] != ' ' && l[i][strlen(l[i]) - 1] != ' ';
    CHECK(fit && !strcmp(l[0], "Climbing down from a ledge"), "by words, within 29: '%s' '%s' '%s'", l[0], l[1], l[2]);
    n = somvr_lv_wrap("one\ntwo\n\nfour", l, 6, 29, &more);
    CHECK(n == 4 && !strcmp(l[1], "two") && !l[2][0] && !strcmp(l[3], "four"), "breaks kept: %d '%s' '%s'", n, l[1],
          l[3]);
    n = somvr_lv_wrap("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa b", l, 6, 29, &more);
    CHECK(n == 2 && strlen(l[0]) == 29 && !strcmp(l[1], "aaaaaaaaaaaaaaa b"), "a long word cut: '%s' '%s'", l[0], l[1]);
    n = somvr_lv_wrap("It\xe2\x80\x99s the \xe2\x80\x9cgrab\xe2\x80\x9d \xe2\x80\x94 90\xc2\xb0 \xe6\x97\xa5", l, 6, 29,
                      &more);
    CHECK(n == 1 && !strcmp(l[0], "It's the \"grab\" - 90\x7f ?"), "folded to ASCII: '%s'", l[0]);
    char big[700];
    for (int i = 0; i < 699; i++) big[i] = i % 6 == 5 ? ' ' : 'x';
    big[699] = 0;
    n = somvr_lv_wrap(big, l, 6, 29, &more);
    CHECK(n == 6 && more && strlen(l[5]) <= 29 && !strcmp(l[5] + strlen(l[5]) - 3, "..."), "cut: %d lines, more %d, '%s'",
          n, more, l[5]);
    CHECK(somvr_lv_wrap("", l, 6, 29, &more) == 0 && somvr_lv_wrap(NULL, l, 6, 29, &more) == 0, "empty: no lines");
}

static void test_card(void) {
    somvr_lv_card c;
    somvr_lv_card_make(&c, SOMVR_LVK_SAY, "From the coordinator", "Try the climb again: the wall-top hang is fixed.",
                       "Tap to close");
    CHECK(c.n == 2 && !strcmp(c.title, "From the coordinator") && !strcmp(c.hint, "Tap to close"), "the card: %d lines",
          c.n);
    /* b37: the spoken line - a message: its text alone; a patch: the title, the whole text (not the card's cut), each
     * break a sentence's end; tabs / control bytes gone; a long text cut at a space */
    CHECK(!strcmp(c.speech, "Try the climb again: the wall-top hang is fixed."), "speech (message): '%s'", c.speech);
    somvr_lv_card_make(&c, SOMVR_LVK_PATCH, "Patch 2 ready", "The block window is shorter\nTry: block a\tswing\x01!\n"
                       "Apply it: menu > Test tab", "Tap to close");
    CHECK(!strcmp(c.speech, "Patch 2 ready. The block window is shorter. Try: block a swing! Apply it: menu > Test tab"),
          "speech (patch): '%s'", c.speech);
    {
        char longt[1400], out[64];
        for (int i = 0; i < 1399; i++) longt[i] = i % 7 == 6 ? ' ' : 'a';
        longt[1399] = 0;
        somvr_lv_card_make(&c, SOMVR_LVK_SAY, "Message 9", longt, "");
        size_t n = strlen(c.speech);
        CHECK(n > 600 && n < SOMVR_LV_SPEECH_MAX && c.speech[n - 1] == 'a' && c.speech[0] == 'a' && c.n == SOMVR_LV_LINES &&
                  c.more, "speech (long): %zu bytes, ends at a word", n);
        size_t m = somvr_lv_speech(SOMVR_LVK_RESULT, "Patch 1 is in", "Try: x\n\n", out, sizeof out);
        CHECK(m == strlen(out) && !strcmp(out, "Patch 1 is in. Try: x."), "speech (result): '%s'", out);
    }
    somvr_lv_card_make(&c, SOMVR_LVK_SAY, "From the coordinator", "Try the climb again: the wall-top hang is fixed.",
                       "Tap to close");
    static uint8_t px[SOMVR_LV_TEX_W * SOMVR_LV_TEX_H * 4];
    somvr_lv_draw(&F, px, 0, &c);
    /* opaque inside (premultiplied: a <= 255 and every channel <= a), clear outside the corners, text lighter */
    const uint8_t *mid = px + ((size_t)(SOMVR_LV_CARD_H / 2) * SOMVR_LV_TEX_W + 400) * 4;
    const uint8_t *corner = px;
    int premul = 1, lit = 0;
    for (size_t i = 0; i < (size_t)SOMVR_LV_TEX_W * SOMVR_LV_TEX_H; i++) {
        const uint8_t *p = px + i * 4;
        premul &= p[0] <= p[3] && p[1] <= p[3] && p[2] <= p[3];
        lit += p[1] > 200 && p[3] == 255 && i < (size_t)SOMVR_LV_TEX_W * SOMVR_LV_CARD_H;
    }
    CHECK(premul && mid[3] >= 235 && corner[3] == 0 && lit > 500, "the card: premultiplied, opaque, clear corners, text "
          "(%d lit)", lit);
    const uint8_t *dot = px + ((size_t)(SOMVR_LV_DOT_Y + 16) * SOMVR_LV_TEX_W + 16) * 4,
                  *beside = px + ((size_t)(SOMVR_LV_DOT_Y + 16) * SOMVR_LV_TEX_W + 40) * 4;
    CHECK(dot[3] == 255 && dot[0] > 200 && beside[3] == 0, "the dot: a disc at (0, %d), nothing beside it", SOMVR_LV_DOT_Y);
    static uint8_t px2[SOMVR_LV_TEX_W * SOMVR_LV_TEX_H * 4];
    somvr_lv_draw(&F, px2, 1, &c);
    CHECK(px2[((size_t)(SOMVR_LV_DOT_Y + 16) * SOMVR_LV_TEX_W + 16) * 4 + 2] == dot[0], "bgra: R and B swapped");
}

static void test_calm(void) {
    somvr_lv_ccfg c;
    somvr_lv_ccfg_default(&c);
    somvr_lv_calm s;
    memset(&s, 0, sizeof s);
    CHECK(!somvr_lv_calm_step(&s, &c, 10.0, 0), "not calm at the first frame (3 s from the start)");
    CHECK(somvr_lv_calm_step(&s, &c, 13.1, 0), "calm 3 s after the start");
    CHECK(!somvr_lv_calm_step(&s, &c, 14.0, SOMVR_VMG_SWING), "a swing: busy");
    CHECK(!somvr_lv_calm_step(&s, &c, 16.5, 0), "2.5 s after: still busy");
    CHECK(somvr_lv_calm_step(&s, &c, 17.1, 0), "3.1 s after: calm");
    CHECK(!somvr_lv_calm_step(&s, &c, 18.0, SOMVR_VMG_FIGHT), "an attack at Talion lately (FIGHT): busy");
    CHECK(!somvr_lv_calm_step(&s, &c, 19.0, 32u), "1 s after the attack: busy");
    CHECK(somvr_lv_calm_step(&s, &c, 21.1, 32u), "3.1 s after it: calm - the card's own bit is no cause");
    CHECK(s.n_calm == 3 && s.n_busy == 2 && s.why == SOMVR_VMG_FIGHT, "the counts: %llu calm, %llu busy, why %u",
          (unsigned long long)s.n_calm, (unsigned long long)s.n_busy, s.why);
    uint32_t each[] = {SOMVR_VMG_BOW, SOMVR_VMG_CLIMB, SOMVR_VMG_THREAT, SOMVR_VMG_SOFT};
    int all = 1;
    for (int i = 0; i < 4; i++) {
        memset(&s, 0, sizeof s);
        somvr_lv_calm_step(&s, &c, 0, 0);
        all &= !somvr_lv_calm_step(&s, &c, 5, each[i]);
    }
    CHECK(all, "the bow, a climb hold, an attack incoming, the sword hand's swing: each busy");
}

static void test_formats(void) {
    const char *pt = "id=3\nkind=live\nwhat=the wall-top hang fixed\ntry=climb the tower and let go at the top\n";
    somvr_lv_patch p;
    CHECK(somvr_lv_patch_parse(pt, strlen(pt), &p) && p.id == 3 && p.live && !strcmp(p.what, "the wall-top hang fixed") &&
              !strcmp(p.try_, "climb the tower and let go at the top"), "patch.txt: id %d live %d '%s'", p.id, p.live,
          p.what);
    const char *pr = "id=4\r\nkind=restart\r\nwhy=the layer changed\r\n";
    CHECK(somvr_lv_patch_parse(pr, strlen(pr), &p) && p.id == 4 && !p.live && !strcmp(p.why, "the layer changed"),
          "CRLF, restart: '%s'", p.why);
    CHECK(!somvr_lv_patch_parse("kind=live\n", 10, &p) && !somvr_lv_patch_parse("id=2\n", 5, &p), "no id / no kind: refused");
    const char *st = "seq=7\ncurrent=3\nlast=applied\nid=3\nmsg=gen 2 in 640 ms\n";
    somvr_lv_state s;
    CHECK(somvr_lv_state_parse(st, strlen(st), &s) && s.seq == 7 && s.current == 3 && !strcmp(s.last, "applied") &&
              s.id == 3 && !strcmp(s.msg, "gen 2 in 640 ms"), "state.txt: seq %u current %d", s.seq, s.current);
    int op = 0, id = 0;
    uint32_t seq = 0;
    CHECK(somvr_lv_request_parse("apply 3 12\n", 11, &op, &id, &seq) && op == 1 && id == 3 && seq == 12, "apply 3 12");
    CHECK(somvr_lv_request_parse("undo 3 13", 9, &op, &id, &seq) && op == 2 && seq == 13, "undo 3 13");
    CHECK(!somvr_lv_request_parse("swap 3 1", 8, &op, &id, &seq) && !somvr_lv_request_parse("apply x", 7, &op, &id, &seq),
          "bad requests refused");
    char v[8];
    CHECK(somvr_lv_kv("ab=1\nabc=22\n", 12, "abc", v, sizeof v) && !strcmp(v, "22") &&
              !somvr_lv_kv("ab=1\n", 5, "a", v, sizeof v), "kv: the whole key");
    CHECK(somvr_lv_kv("what=0123456789abc\n", 19, "what", v, sizeof v) && strlen(v) == 7, "kv: cut to the buffer");
    /* results as cards */
    somvr_lv_card c;
    somvr_lv_patch_parse(pt, strlen(pt), &p);
    somvr_lv_state_parse(st, strlen(st), &s);
    CHECK(somvr_lv_result_card(&s, &p, &c) && c.kind == SOMVR_LVK_RESULT && !strcmp(c.title, "Patch 3 is in") &&
              !strncmp(c.line[0], "Try: climb the tower", 20), "applied: '%s' '%s'", c.title, c.line[0]);
    snprintf(s.last, sizeof s.last, "refused"), snprintf(s.msg, sizeof s.msg, "the game was busy");
    CHECK(somvr_lv_result_card(&s, &p, &c) && c.kind == SOMVR_LVK_WARN && !strcmp(c.line[0], "the game was busy"),
          "refused: '%s'", c.title);
    snprintf(s.last, sizeof s.last, "undone"), s.current = 0;
    CHECK(somvr_lv_result_card(&s, &p, &c) && !strcmp(c.title, "Patch 3 undone") && strstr(c.line[0], "session's own"),
          "undone: '%s' / '%s'", c.line[0], c.line[1]);
    snprintf(s.last, sizeof s.last, "stub");
    CHECK(somvr_lv_result_card(&s, &p, &c) && !strcmp(c.title, "Patch 3: after a restart"), "stub: '%s'", c.title);
    {   /* b37: a refused Undo (the patch still the live one) says so, not "not applied" */
        somvr_lv_state u = s;
        snprintf(u.last, sizeof u.last, "refused");
        u.id = 3, u.current = 3;
        CHECK(somvr_lv_result_card(&u, &p, &c) && !strcmp(c.title, "Patch 3 still in") && c.kind == SOMVR_LVK_WARN,
              "refused undo: '%s'", c.title);
        u.current = 0;
        CHECK(somvr_lv_result_card(&u, &p, &c) && !strcmp(c.title, "Patch 3 not applied"), "refused apply: '%s'", c.title);
    }
    snprintf(s.last, sizeof s.last, "none");
    CHECK(!somvr_lv_result_card(&s, &p, &c), "no card for an unknown result");
}

/* the VR menu's patch box: off = the Test page as before; on = its two buttons, enabled by the state, the text above */
static void test_menu_box(void) {
    uint64_t av = SOMVR_VMA_TESTPAGE;
    int rc[4];
    somvr_vm_live_set(NULL);
    CHECK(!somvr_vm_rect_av(av, SOMVR_VMI_LIVE_APPLY, SOMVR_VMP_BUTTON, rc) &&
              !somvr_vm_rect_av(av, SOMVR_VMI_LIVE_UNDO, SOMVR_VMP_BUTTON, rc), "off: no rects");
    int none = 1;
    for (int y = 0; y < SOMVR_VM_TEX_H; y += 4)
        for (int x = 0; x < SOMVR_VM_TEX_W; x += 4) {
            somvr_vm_hit h = somvr_vm_hit_test_av(av, SOMVR_VMPG_TEST, (x + 0.5f) / SOMVR_VM_TEX_W, (y + 0.5f) / SOMVR_VM_TEX_H);
            none &= h.item != SOMVR_VMI_LIVE_APPLY && h.item != SOMVR_VMI_LIVE_UNDO;
        }
    CHECK(none, "off: never hit");
    static const char txt[] = "What to test in build 35\n- one\n- two\n- three\n- four\n- five\n- six\n- seven\n- eight\n"
                              "- nine\n- ten\n- eleven\n- twelve\n- thirteen\n- fourteen\n- fifteen\n- sixteen\n";
    static somvr_vm_text t0, t1;
    somvr_vm_text_parse(&t0, txt, sizeof txt - 1);
    somvr_vm_live L;
    memset(&L, 0, sizeof L);
    L.on = 1, L.can_apply = 1, L.gen = 1;
    snprintf(L.line[0], sizeof L.line[0], "Patch 3 ready");
    snprintf(L.line[1], sizeof L.line[1], "the wall-top hang fixed");
    snprintf(L.line[2], sizeof L.line[2], "Try: climb the tower");
    somvr_vm_live_set(&L);
    somvr_vm_text_parse(&t1, txt, sizeof txt - 1);
    int below = 1;
    for (int k = 0; k < t1.n; k++) below &= t1.y[k] + SOMVR_VM_TEST_DY <= SOMVR_VM_LIVE_Y - 14;
    CHECK(below && t1.pages >= t0.pages, "on: the text ends above the box (%d pages, %d before)", t1.pages, t0.pages);
    int a[4], u[4];
    CHECK(somvr_vm_rect_av(av, SOMVR_VMI_LIVE_APPLY, SOMVR_VMP_BUTTON, a) &&
              somvr_vm_rect_av(av, SOMVR_VMI_LIVE_UNDO, SOMVR_VMP_BUTTON, u) && a[1] + a[3] < SOMVR_VM_TEST_NAV_Y &&
              a[0] + a[2] < u[0] && u[0] + u[2] <= SOMVR_VM_TEX_W - 16 && a[1] > SOMVR_VM_LIVE_Y + 2 * 30,
          "on: two buttons side by side under the lines, above Prev / Next");
    somvr_vm_hit h = somvr_vm_hit_test_av(av, SOMVR_VMPG_TEST, (a[0] + 10.0f) / SOMVR_VM_TEX_W, (a[1] + 10.0f) / SOMVR_VM_TEX_H);
    somvr_vm_hit h2 = somvr_vm_hit_test_av(av, SOMVR_VMPG_SETTINGS, (a[0] + 10.0f) / SOMVR_VM_TEX_W, (a[1] + 10.0f) / SOMVR_VM_TEX_H);
    CHECK(h.item == SOMVR_VMI_LIVE_APPLY && h2.item != SOMVR_VMI_LIVE_APPLY, "hit on the Test page only");
    somvr_settings s;
    memset(&s, 0, sizeof s);
    CHECK(somvr_vm_enabled(&s, av, SOMVR_VMI_LIVE_APPLY) && !somvr_vm_enabled(&s, av, SOMVR_VMI_LIVE_UNDO),
          "enabled as can_apply / can_undo");
    CHECK(somvr_font_text_w(somvr_vm_label(SOMVR_VMI_LIVE_APPLY), SOMVR_VM_SCALE) <= a[2] - 16 &&
              !strcmp(somvr_vm_label(SOMVR_VMI_LIVE_UNDO), "Undo patch"), "the labels fit");
    /* the picture: the box drawn (differs from off), the panel otherwise the same above it */
    static uint8_t p0[SOMVR_VM_TEX_W * SOMVR_VM_TEX_H * 4], p1[SOMVR_VM_TEX_W * SOMVR_VM_TEX_H * 4];
    somvr_vm_view v;
    memset(&v, 0, sizeof v);
    v.avail = av, v.page = SOMVR_VMPG_TEST, v.test = &t1, v.hover = (somvr_vm_hit){-1, 0};
    somvr_vm_draw(&F, p1, 0, &v);
    somvr_vm_live_set(NULL);
    somvr_vm_draw(&F, p0, 0, &v);
    size_t row = (size_t)SOMVR_VM_TEX_W * 4;
    CHECK(memcmp(p0 + row * SOMVR_VM_LIVE_BTN_Y, p1 + row * SOMVR_VM_LIVE_BTN_Y, row * 56) &&
              !memcmp(p0 + row * 64, p1 + row * 64, row * 60), "the box drawn; the tabs the same");
    somvr_vm_live_set(NULL);
}

int main(void) {
    CHECK(somvr_font_init(&F) == 0, "the font");
    test_wrap();
    test_card();
    test_calm();
    test_formats();
    test_menu_box();
    printf("test_live: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
