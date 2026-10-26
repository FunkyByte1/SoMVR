/* test_vrmenu.c - the VR settings menu's pure part (somvr_font.c, somvr_vrmenu.c): the font (every glyph parses, has
 * ink inside its cell, the metrics: cap height, x-height, baseline, descenders; all glyphs distinct; text width,
 * drawing, clipping, premultiplied colour, BGRA), the ABI's layout, the settings (defaults, keys, values and their
 * ranges, the locale-free number, the file's parse with comments, blanks, CRLF, quotes, bad lines, the last line
 * winning, and the parse/format round trip), the menu (labels, which items are usable, the values shown, every click:
 * steps, ends, wrap, toggles, buttons, disabled items), the layout and hit test (every part's centre back to it,
 * nothing outside), the picture (corners, rows, hover, dimmed items, BGRA, deterministic) and the gesture (the palm's
 * normal, the panel's pose, opening after the hold, re-arming, the menu button, looking away, busy, the hand lost; the
 * gate: no open while the hands are busy, a palm held through it needs a fresh gesture, the tail, the button). */
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
            printf("FAIL line %d: ", __LINE__);                                                                        \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)
#define NEAR(a, b, eps) (fabs((double)(a) - (double)(b)) <= (eps))

static somvr_font F;

/* ---- the font ------------------------------------------------------------------------------------------------------ */
static void test_font(void) {
    int bad = somvr_font_init(&F);
    CHECK(bad == 0 && F.ok, "font art: the first bad glyph %d", bad);
    int box[4], ok = 1;
    CHECK(!somvr_font_ink(&F, ' ', box), "the space has ink");
    for (int c = SOMVR_FONT_FIRST + 1; c <= SOMVR_FONT_LAST; c++) {
        int ink = somvr_font_ink(&F, c, box);
        int inside = ink && box[0] >= 0 && box[2] <= 6 && box[1] >= 1 && box[3] <= 14;
        if (!inside) ok = 0, printf("    glyph %d: ink %d box %d,%d..%d,%d\n", c, ink, box[0], box[1], box[2], box[3]);
    }
    CHECK(ok, "every glyph has ink inside columns 0..6, rows 1..14");
    /* the metrics: capitals and digits rows 2..11 (Q's tail lower); lower case from row 5 (ascenders from 2) to the
     * baseline, the descenders to row 14 */
    int caps = 1, lower = 1;
    for (int c = 'A'; c <= 'Z'; c++) {
        somvr_font_ink(&F, c, box);
        if (box[1] != 2 || box[3] != (c == 'Q' ? 13 : SOMVR_FONT_BASELINE)) caps = 0, printf("    %c: rows %d..%d\n", c, box[1], box[3]);
    }
    for (int c = '0'; c <= '9'; c++) {
        somvr_font_ink(&F, c, box);
        if (box[1] != 2 || box[3] != SOMVR_FONT_BASELINE) caps = 0, printf("    %c: rows %d..%d\n", c, box[1], box[3]);
    }
    CHECK(caps, "capitals and digits: rows 2..11");
    for (const char *p = "acemnorsuvwxz"; *p; p++) {
        somvr_font_ink(&F, *p, box);
        if (box[1] != 5 || box[3] != SOMVR_FONT_BASELINE) lower = 0, printf("    %c: rows %d..%d\n", *p, box[1], box[3]);
    }
    for (const char *p = "bdhkl"; *p; p++) {
        somvr_font_ink(&F, *p, box);
        if (box[1] != 2 || box[3] != SOMVR_FONT_BASELINE) lower = 0, printf("    %c: rows %d..%d\n", *p, box[1], box[3]);
    }
    for (const char *p = "gjpqy"; *p; p++) {
        somvr_font_ink(&F, *p, box);
        if (box[3] != 14) lower = 0, printf("    %c: bottom row %d\n", *p, box[3]);
    }
    CHECK(lower, "lower case: x-height row 5, ascenders row 2, baseline 11, descenders to 14");
    somvr_font_ink(&F, '.', box);
    CHECK(box[3] == SOMVR_FONT_BASELINE && box[1] == 10, "the full stop on the baseline: rows %d..%d", box[1], box[3]);
    int distinct = 1;
    for (int a = SOMVR_FONT_FIRST; a <= SOMVR_FONT_LAST; a++)
        for (int b = a + 1; b <= SOMVR_FONT_LAST; b++)
            if (!memcmp(F.row[a - SOMVR_FONT_FIRST], F.row[b - SOMVR_FONT_FIRST], SOMVR_FONT_H))
                distinct = 0, printf("    glyphs %d and %d are the same\n", a, b);
    CHECK(distinct, "every glyph is distinct");
    CHECK(somvr_font_index(1) == '?' - SOMVR_FONT_FIRST && somvr_font_index(0xff) == '?' - SOMVR_FONT_FIRST &&
              somvr_font_index('A') == 'A' - 32 && somvr_font_row(&F, 'A', 99) == 0 && somvr_font_row(&F, 'A', 2) == 0x10,
          "index / row");
    CHECK(somvr_font_text_w("Snap", 2) == 64 && somvr_font_text_w("", 3) == 0, "text width");
    /* drawing: 'A' at scale 2 at (3, 5) in a 40 x 40 image, then clipped half outside */
    enum { W = 40, H = 40 };
    static uint8_t px[(W * H + 16) * 4];
    memset(px, 0, sizeof px);
    memset(px + W * H * 4, 0xab, 64); /* a guard after the image */
    uint8_t col[4] = {200, 100, 50, 255};
    int x = somvr_font_draw(&F, px, W, H, 3, 5, "A", 2, col, 0);
    const uint8_t *apex = px + 4 * ((5 + 2 * 2) * W + 3 + 3 * 2); /* row 2, column 3 of 'A' */
    CHECK(x == 19 && apex[0] == 200 && apex[1] == 100 && apex[2] == 50 && apex[3] == 255 &&
              px[4 * ((5 + 2 * 2) * W + 3 + 2 * 2) + 3] == 0 && px[4 * ((5 + 2 * 2 + 1) * W + 3 + 3 * 2 + 1) + 3] == 255,
          "'A' at scale 2: pen %d apex %u,%u,%u,%u", x, apex[0], apex[1], apex[2], apex[3]);
    int inked = 0;
    for (int i = 0; i < W * H; i++) inked += px[4 * i + 3] != 0;
    int ainks = 0;
    for (int y = 0; y < 16; y++)
        for (int b = 0; b < 8; b++) ainks += (somvr_font_row(&F, 'A', y) >> (7 - b)) & 1;
    CHECK(inked == 4 * ainks, "'A' at scale 2 inks %d pixels, want 4 x %d", inked, ainks);
    memset(px, 0, W * H * 4);
    somvr_font_draw(&F, px, W, H, -6, 30, "WW", 3, col, 0); /* off the left and the bottom */
    somvr_font_draw(&F, px, W, H, 30, -20, "MM", 3, col, 0); /* off the right and the top */
    int guard = 1;
    for (int i = 0; i < 64; i++) guard &= px[W * H * 4 + i] == 0xab;
    CHECK(guard, "clipped drawing wrote past the image");
    uint8_t half[4] = {200, 100, 50, 128};
    memset(px, 0, W * H * 4);
    somvr_font_draw(&F, px, W, H, 0, 0, "I", 1, half, 1); /* premultiplied, BGRA */
    const uint8_t *p = px + 4 * (3 * W + 3); /* 'I' row 3, column 3 */
    CHECK(p[3] == 128 && p[0] == 25 && p[1] == 50 && p[2] == 100, "premultiplied BGRA %u %u %u %u", p[0], p[1], p[2], p[3]);
}

/* ---- the ABI and the settings ---------------------------------------------------------------------------------- */
static void test_settings(void) {
    somvr_vrmenu_shared m;
    memset(&m, 0, sizeof m);
    CHECK(!somvr_vrmenu_ok(&m) && !somvr_vrmenu_ok(NULL), "a zero object is not ok");
    m.magic = SOMVR_VRMENU_MAGIC, m.version = SOMVR_VRMENU_VERSION, m.size = sizeof m;
    CHECK(!somvr_vrmenu_ok(&m), "ok before active");
    atomic_store(&m.active, 1);
    CHECK(somvr_vrmenu_ok(&m) && !memcmp(&m.magic, "SOMRVMN1", 8), "ok once active; the magic reads SOMRVMN1");
    m.size = 100;
    CHECK(!somvr_vrmenu_ok(&m), "another size");
    m.size = sizeof m;
    somvr_settings s, r;
    somvr_set_default(&s);
    s.snap_deg = 45;
    somvr_vrmenu_publish(&m, &s);
    CHECK(atomic_load(&m.gen) == 1 && somvr_vrmenu_read(&m, &r) && r.snap_deg == 45 && atomic_load(&m.seq) == 2,
          "publish: gen %u seq %llu", atomic_load(&m.gen), (unsigned long long)atomic_load(&m.seq));
    somvr_set_default(&s);
    CHECK(s.turn == 0 && s.snap_deg == 30 && s.turn_dps == 120 && s.vignette == 0 && s.blink_ms == 0 && !s.seated &&
              s.height_cm == 0 && s.hud == 1 && s.hud_dist_m == 1.5f && s.hand == 0 && s.view == 0 && s.haptics == 1.0f,
          "defaults");
    CHECK(!strcmp(somvr_set_key(SOMVR_SK_TURN), "TURN") && !strcmp(somvr_set_key(SOMVR_SK_HAPTICS), "HAPTICS") &&
              !somvr_set_key(SOMVR_SK_N) && !strcmp(somvr_set_env(SOMVR_SK_SNAP_DEG), "SOMVR_SNAP_DEG") &&
              !somvr_set_env(SOMVR_SK_HUD) && !somvr_set_env(SOMVR_SK_VIEW) &&
              !strcmp(somvr_set_env(SOMVR_SK_HAPTICS), "SOMVR_HAPTICS"),
          "key and env names");
    CHECK(somvr_set_find("SNAP_DEG") == SOMVR_SK_SNAP_DEG && somvr_set_find("SOMVR_TURN") == SOMVR_SK_TURN &&
              somvr_set_find("snap_deg") == -1 && somvr_set_find("FOO") == -1,
          "find");
    double d;
    CHECK(somvr_set_num(" 1.5 ", &d) && d == 1.5 && somvr_set_num("-2", &d) && d == -2 && somvr_set_num("7.", &d) &&
              d == 7 && somvr_set_num(".25", &d) && d == 0.25 && !somvr_set_num("1,5", &d) && !somvr_set_num("", &d) &&
              !somvr_set_num("1e3", &d) && !somvr_set_num("-", &d) && !somvr_set_num("3 4", &d),
          "the locale-free number");
    /* values: understood, clamped, refused */
    somvr_settings t = s;
    CHECK(somvr_set_put(&t, SOMVR_SK_TURN, "smooth") && t.turn == 1 && !somvr_set_put(&t, SOMVR_SK_TURN, "fast") &&
              t.turn == 1,
          "TURN");
    CHECK(somvr_set_put(&t, SOMVR_SK_SNAP_DEG, "45") && t.snap_deg == 45 && somvr_set_put(&t, SOMVR_SK_SNAP_DEG, "400") &&
              t.snap_deg == 180 && !somvr_set_put(&t, SOMVR_SK_SNAP_DEG, "x"),
          "SNAP_DEG");
    CHECK(somvr_set_put(&t, SOMVR_SK_TURN_DPS, "5") && t.turn_dps == 10, "TURN_DPS clamped");
    CHECK(somvr_set_put(&t, SOMVR_SK_VIGNETTE, "0.6") && NEAR(t.vignette, 0.6, 1e-6) &&
              somvr_set_put(&t, SOMVR_SK_VIGNETTE, "3") && t.vignette == 1,
          "VIGNETTE");
    CHECK(somvr_set_put(&t, SOMVR_SK_SNAP_BLINK_MS, "140.4") && t.blink_ms == 140, "SNAP_BLINK_MS");
    CHECK(somvr_set_put(&t, SOMVR_SK_SEATED, "on") && t.seated && somvr_set_put(&t, SOMVR_SK_SEATED, "0") && !t.seated &&
              !somvr_set_put(&t, SOMVR_SK_SEATED, "yes"),
          "SEATED");
    CHECK(somvr_set_put(&t, SOMVR_SK_HEIGHT_CM, "0") && t.height_cm == 0 && somvr_set_put(&t, SOMVR_SK_HEIGHT_CM, "20") &&
              t.height_cm == 50 && somvr_set_put(&t, SOMVR_SK_HEIGHT_CM, "125") && t.height_cm == 125,
          "HEIGHT_CM");
    CHECK(somvr_set_put(&t, SOMVR_SK_HUD, "off") && !t.hud && somvr_set_put(&t, SOMVR_SK_HUD_DIST_M, "0.1") &&
              NEAR(t.hud_dist_m, 0.3, 1e-6),
          "HUD, HUD_DIST_M");
    CHECK(somvr_set_put(&t, SOMVR_SK_HAND, "left") && t.hand == 1 && somvr_set_put(&t, SOMVR_SK_VIEW, "third") &&
              t.view == 1 && !somvr_set_put(&t, SOMVR_SK_VIEW, "2"),
          "HAND, VIEW");
    CHECK(somvr_set_put(&t, SOMVR_SK_HAPTICS, "50") && NEAR(t.haptics, 0.5, 1e-6) &&
              somvr_set_put(&t, SOMVR_SK_HAPTICS, "999") && NEAR(t.haptics, 1.5, 1e-6),
          "HAPTICS in percent");
    char v[32];
    somvr_set_get(&t, SOMVR_SK_VIGNETTE, v, sizeof v);
    CHECK(!strcmp(v, "1"), "VIGNETTE 1 -> '%s'", v);
    t.vignette = 0.4f;
    somvr_set_get(&t, SOMVR_SK_VIGNETTE, v, sizeof v);
    CHECK(!strcmp(v, "0.4"), "VIGNETTE 0.4 -> '%s'", v);
    t.hud_dist_m = 1.25f;
    somvr_set_get(&t, SOMVR_SK_HUD_DIST_M, v, sizeof v);
    CHECK(!strcmp(v, "1.25"), "HUD_DIST_M -> '%s'", v);
    somvr_set_get(&t, SOMVR_SK_HAPTICS, v, sizeof v);
    CHECK(!strcmp(v, "150"), "HAPTICS -> '%s'", v);
    somvr_set_get(&t, SOMVR_SK_TURN, v, sizeof v);
    CHECK(!strcmp(v, "smooth"), "TURN -> '%s'", v);
    /* the file */
    somvr_settings p;
    somvr_set_default(&p);
    int nbad = -1;
    char note[128];
    const char *text = "# a comment\n\n  TURN = smooth  \r\nSNAP_DEG=45 # trailing comment\nVIGNETTE=\"0.6\"\n"
                       "HUD='off'\nFOO=1\nSEATED=maybe\nno equals sign\nSNAP_DEG=60\nSOMVR_HAPTICS=75\nHAND=left";
    uint32_t mask = somvr_set_parse(&p, text, &nbad, note, sizeof note);
    CHECK(mask == ((1u << SOMVR_SK_TURN) | (1u << SOMVR_SK_SNAP_DEG) | (1u << SOMVR_SK_VIGNETTE) | (1u << SOMVR_SK_HUD) |
                   (1u << SOMVR_SK_HAPTICS) | (1u << SOMVR_SK_HAND)) &&
              nbad == 3 && !strcmp(note, "line 7: unknown key"),
          "parse: mask %#x bad %d note '%s'", mask, nbad, note);
    CHECK(p.turn == 1 && p.snap_deg == 60 && NEAR(p.vignette, 0.6, 1e-6) && p.hud == 0 && NEAR(p.haptics, 0.75, 1e-6) &&
              p.hand == 1 && !p.seated,
          "parse: the values (the last SNAP_DEG wins, SEATED unchanged)");
    /* the round trip: every key, unusual values */
    somvr_settings a = {.turn = 1, .snap_deg = 22.5f, .turn_dps = 150, .vignette = 0.35f, .blink_ms = 90, .seated = 1,
                        .height_cm = 117.5f, .hud = 0, .hud_dist_m = 2.25f, .hand = 1, .view = 1, .haptics = 0.25f,
                        .size = 1.2f}, b;
    char file[2048];
    size_t n = somvr_set_format(&a, SOMVR_SK_ALL, file, sizeof file);
    somvr_set_default(&b);
    nbad = -1;
    mask = somvr_set_parse(&b, file, &nbad, note, sizeof note);
    CHECK(n == strlen(file) && mask == SOMVR_SK_ALL && nbad == 0 && somvr_set_diff(&a, &b) == 0 &&
              !memcmp(&a, &b, sizeof a),
          "round trip: %zu bytes mask %#x bad %d diff %#x", n, mask, nbad, somvr_set_diff(&a, &b));
    CHECK(strstr(file, "\nSNAP_DEG=22.5\n") && strstr(file, "\nHEIGHT_CM=117.5\n") && strstr(file, "\nVIEW=third\n") &&
              strstr(file, "\nHAPTICS=25\n") && file[0] == '#',
          "the file's lines:\n%s", file);
    n = somvr_set_format(&a, (1u << SOMVR_SK_HUD) | (1u << SOMVR_SK_SNAP_DEG), file, sizeof file);
    CHECK(strstr(file, "SNAP_DEG=22.5\nHUD=off\n") && !strstr(file, "TURN="), "only the masked keys:\n%s", file);
    char tiny[16];
    size_t want = somvr_set_format(&a, SOMVR_SK_ALL, NULL, 0);
    n = somvr_set_format(&a, SOMVR_SK_ALL, tiny, sizeof tiny);
    CHECK(n == want && n > sizeof tiny && strlen(tiny) == sizeof tiny - 1, "a short buffer: %zu (want %zu)", n, want);
    b.snap_deg = 30, b.hud = 1;
    CHECK(somvr_set_diff(&a, &b) == ((1u << SOMVR_SK_SNAP_DEG) | (1u << SOMVR_SK_HUD)), "diff %#x", somvr_set_diff(&a, &b));
}

/* ---- the menu's model ------------------------------------------------------------------------------------------ */
static void test_model(void) {
    somvr_settings s;
    somvr_set_default(&s);
    const uint32_t ALL = SOMVR_VMA_VIEW | SOMVR_VMA_HUD | SOMVR_VMA_COMFORT | SOMVR_VMA_FLOOR;
    CHECK(!strcmp(somvr_vm_label(SOMVR_VMI_SNAP), "Snap angle") && !strcmp(somvr_vm_label(SOMVR_VMI_RECENTER),
                                                                            "Recentre view") &&
              !strcmp(somvr_vm_label(99), ""),
          "labels");
    int longest = 0;
    for (int i = 0; i < SOMVR_VMI_ROWS; i++) {
        int w = somvr_font_text_w(somvr_vm_label(i), SOMVR_VM_SCALE);
        longest = w > longest ? w : longest;
    }
    CHECK(longest <= 256 - 20 - 8, "the longest label is %d px (fits before the < box)", longest);
    /* usable items */
    CHECK(somvr_vm_enabled(&s, 0, SOMVR_VMI_TURN) && somvr_vm_enabled(&s, 0, SOMVR_VMI_SNAP) &&
              !somvr_vm_enabled(&s, ALL, SOMVR_VMI_SPEED) && !somvr_vm_enabled(&s, 0, SOMVR_VMI_VIGNETTE) &&
              !somvr_vm_enabled(&s, 0, SOMVR_VMI_HUD) && !somvr_vm_enabled(&s, 0, SOMVR_VMI_VIEW) &&
              !somvr_vm_enabled(&s, ALL, SOMVR_VMI_HEIGHT) && somvr_vm_enabled(&s, 0, SOMVR_VMI_HAPTICS) &&
              somvr_vm_enabled(&s, 0, SOMVR_VMI_RECENTER) && !somvr_vm_enabled(&s, ALL, -1),
          "enabled: defaults");
    s.turn = 1, s.seated = 1;
    CHECK(!somvr_vm_enabled(&s, ALL, SOMVR_VMI_SNAP) && somvr_vm_enabled(&s, ALL, SOMVR_VMI_SPEED) &&
              somvr_vm_enabled(&s, ALL, SOMVR_VMI_HEIGHT) && !somvr_vm_enabled(&s, ALL & ~SOMVR_VMA_FLOOR, SOMVR_VMI_HEIGHT),
          "enabled: smooth, seated (the height needs a floor)");
    s.hud = 0;
    CHECK(somvr_vm_enabled(&s, ALL, SOMVR_VMI_HUD) && !somvr_vm_enabled(&s, ALL, SOMVR_VMI_HUD_DIST), "HUD off: no distance");
    /* the values shown */
    somvr_set_default(&s);
    char v[32];
    struct { int item; const char *want; } shown[] = {
        {SOMVR_VMI_TURN, "Snap"}, {SOMVR_VMI_SNAP, "30\x7f"}, {SOMVR_VMI_SPEED, "120\x7f/s"}, {SOMVR_VMI_VIGNETTE, "Off"},
        {SOMVR_VMI_BLINK, "Off"}, {SOMVR_VMI_SEATED, "Off"}, {SOMVR_VMI_HEIGHT, "Auto"}, {SOMVR_VMI_HUD, "On"},
        {SOMVR_VMI_HUD_DIST, "1.50 m"}, {SOMVR_VMI_HAND, "Right"}, {SOMVR_VMI_VIEW, "First"}, {SOMVR_VMI_HAPTICS, "100%"},
        {SOMVR_VMI_RECENTER, ""}};
    int vals = 1;
    for (size_t i = 0; i < sizeof shown / sizeof shown[0]; i++) {
        somvr_vm_value(&s, shown[i].item, v, sizeof v);
        if (strcmp(v, shown[i].want)) vals = 0, printf("    item %d shows '%s', want '%s'\n", shown[i].item, v, shown[i].want);
        if (somvr_font_text_w(v, SOMVR_VM_SCALE) > 448 - 304) vals = 0, printf("    item %d: '%s' too wide\n", shown[i].item, v);
    }
    CHECK(vals, "the default values shown");
    s.vignette = 0.4f, s.height_cm = 125, s.haptics = 0, s.turn = 1, s.hud_dist_m = 0.75f;
    somvr_vm_value(&s, SOMVR_VMI_VIGNETTE, v, sizeof v);
    CHECK(!strcmp(v, "40%"), "vignette 0.4 '%s'", v);
    somvr_vm_value(&s, SOMVR_VMI_HEIGHT, v, sizeof v);
    CHECK(!strcmp(v, "125 cm"), "height '%s'", v);
    somvr_vm_value(&s, SOMVR_VMI_HAPTICS, v, sizeof v);
    CHECK(!strcmp(v, "Off"), "haptics 0 '%s'", v);
    somvr_vm_value(&s, SOMVR_VMI_HUD_DIST, v, sizeof v);
    CHECK(!strcmp(v, "0.75 m"), "distance '%s'", v);
    /* clicks */
    somvr_set_default(&s);
    CHECK(somvr_vm_apply(&s, ALL, 0, SOMVR_VMI_SNAP, SOMVR_VMP_MORE) == SOMVR_VMR_CHANGED && s.snap_deg == 45, "snap >");
    s.snap_deg = 90;
    CHECK(somvr_vm_apply(&s, ALL, 0, SOMVR_VMI_SNAP, SOMVR_VMP_MORE) == SOMVR_VMR_NONE && s.snap_deg == 90, "snap > at 90");
    CHECK(somvr_vm_apply(&s, ALL, 0, SOMVR_VMI_SNAP, SOMVR_VMP_ROW) == SOMVR_VMR_CHANGED && s.snap_deg == 15, "row wraps");
    CHECK(somvr_vm_apply(&s, ALL, 0, SOMVR_VMI_SNAP, SOMVR_VMP_LESS) == SOMVR_VMR_NONE && s.snap_deg == 15, "snap < at 15");
    s.snap_deg = 22.5f; /* a value from the environment, between the steps */
    CHECK(somvr_vm_apply(&s, ALL, 0, SOMVR_VMI_SNAP, SOMVR_VMP_MORE) == 1 && s.snap_deg == 30, "22.5 > = 30");
    s.snap_deg = 22.5f;
    CHECK(somvr_vm_apply(&s, ALL, 0, SOMVR_VMI_SNAP, SOMVR_VMP_LESS) == 1 && s.snap_deg == 15, "22.5 < = 15");
    s.snap_deg = 200; /* above the list: > holds, < steps down */
    CHECK(somvr_vm_apply(&s, ALL, 0, SOMVR_VMI_SNAP, SOMVR_VMP_MORE) == 0 && s.snap_deg == 200 &&
              somvr_vm_apply(&s, ALL, 0, SOMVR_VMI_SNAP, SOMVR_VMP_LESS) == 1 && s.snap_deg == 90,
          "above the list");
    CHECK(somvr_vm_apply(&s, ALL, 0, SOMVR_VMI_SPEED, SOMVR_VMP_MORE) == SOMVR_VMR_NONE && s.turn_dps == 120,
          "turn speed with snap turns: disabled");
    CHECK(somvr_vm_apply(&s, ALL, 0, SOMVR_VMI_TURN, SOMVR_VMP_LESS) == 1 && s.turn == 1 &&
              somvr_vm_apply(&s, ALL, 0, SOMVR_VMI_SPEED, SOMVR_VMP_MORE) == 1 && s.turn_dps == 150 &&
              somvr_vm_apply(&s, ALL, 0, SOMVR_VMI_TURN, SOMVR_VMP_ROW) == 1 && s.turn == 0,
          "turn toggles; the speed steps");
    CHECK(somvr_vm_apply(&s, ALL, 0, SOMVR_VMI_BLINK, SOMVR_VMP_MORE) == 1 && s.blink_ms == SOMVR_SET_BLINK_MS &&
              somvr_vm_apply(&s, ALL, 0, SOMVR_VMI_BLINK, SOMVR_VMP_MORE) == 1 && s.blink_ms == 0 &&
              somvr_vm_apply(&s, ALL, 90, SOMVR_VMI_BLINK, SOMVR_VMP_LESS) == 1 && s.blink_ms == 90,
          "blink: on = the environment's length or 140 ms");
    CHECK(somvr_vm_apply(&s, ALL, 0, SOMVR_VMI_VIGNETTE, SOMVR_VMP_MORE) == 1 && NEAR(s.vignette, 0.1, 1e-6) &&
              somvr_vm_apply(&s, ALL, 0, SOMVR_VMI_VIGNETTE, SOMVR_VMP_LESS) == 1 && s.vignette == 0 &&
              somvr_vm_apply(&s, ALL & ~SOMVR_VMA_COMFORT, 0, SOMVR_VMI_VIGNETTE, SOMVR_VMP_MORE) == 0,
          "vignette steps; without comfort disabled");
    s.seated = 1;
    CHECK(somvr_vm_apply(&s, ALL, 0, SOMVR_VMI_HEIGHT, SOMVR_VMP_MORE) == 1 && s.height_cm == 100 &&
              somvr_vm_apply(&s, ALL, 0, SOMVR_VMI_HEIGHT, SOMVR_VMP_LESS) == 1 && s.height_cm == 0,
          "height: Auto <-> 100");
    CHECK(somvr_vm_apply(&s, ALL, 0, SOMVR_VMI_HUD_DIST, SOMVR_VMP_MORE) == 1 && s.hud_dist_m == 2.0f &&
              somvr_vm_apply(&s, ALL, 0, SOMVR_VMI_HUD, SOMVR_VMP_ROW) == 1 && !s.hud &&
              somvr_vm_apply(&s, ALL, 0, SOMVR_VMI_HUD_DIST, SOMVR_VMP_MORE) == 0,
          "HUD distance, then the HUD off");
    CHECK(somvr_vm_apply(&s, ALL, 0, SOMVR_VMI_HAPTICS, SOMVR_VMP_MORE) == 1 && s.haptics == 1.25f &&
              somvr_vm_apply(&s, ALL, 0, SOMVR_VMI_HAND, SOMVR_VMP_MORE) == 1 && s.hand == 1 &&
              somvr_vm_apply(&s, ALL, 0, SOMVR_VMI_VIEW, SOMVR_VMP_MORE) == 1 && s.view == 1 &&
              somvr_vm_apply(&s, ALL & ~SOMVR_VMA_VIEW, 0, SOMVR_VMI_VIEW, SOMVR_VMP_MORE) == 0 && s.view == 1,
          "haptics, hand, view (without VIEW disabled)");
    CHECK(somvr_vm_apply(&s, 0, 0, SOMVR_VMI_RECENTER, SOMVR_VMP_BUTTON) == SOMVR_VMR_RECENTER &&
              somvr_vm_apply(&s, 0, 0, SOMVR_VMI_CLOSE, SOMVR_VMP_BUTTON) == SOMVR_VMR_CLOSE &&
              somvr_vm_apply(&s, 0, 0, SOMVR_VMI_RECENTER, SOMVR_VMP_ROW) == 0 &&
              somvr_vm_apply(&s, ALL, 0, SOMVR_VMI_TURN, SOMVR_VMP_BUTTON) == 0 &&
              somvr_vm_apply(&s, ALL, 0, SOMVR_VMI_TURN, SOMVR_VMP_NONE) == 0,
          "the buttons; parts an item lacks");
}

/* ---- the layout and the hit test ------------------------------------------------------------------------------- */
static void test_layout(void) {
    int rc[4], inside = 1, back = 1, contained = 1;
    for (int item = 0; item < SOMVR_VMI_N; item++)
        for (int part = SOMVR_VMP_ROW; part <= SOMVR_VMP_BUTTON; part++) {
            if (!somvr_vm_rect(item, part, rc)) continue;
            if (rc[0] < 0 || rc[1] < 0 || rc[2] <= 0 || rc[3] <= 0 || rc[0] + rc[2] > SOMVR_VM_TEX_W ||
                rc[1] + rc[3] > SOMVR_VM_TEX_H)
                inside = 0;
            /* the centre; a row's at its label (its centre is the < box's left edge) */
            float px = part == SOMVR_VMP_ROW ? (float)rc[0] + 20 : (float)rc[0] + 0.5f * (float)rc[2];
            somvr_vm_hit h = somvr_vm_hit_test(px / SOMVR_VM_TEX_W, ((float)rc[1] + 0.5f * (float)rc[3]) / SOMVR_VM_TEX_H);
            if (h.item != item || h.part != part) back = 0, printf("    item %d part %d -> %d %d\n", item, part, h.item, h.part);
            if (part == SOMVR_VMP_LESS || part == SOMVR_VMP_MORE) {
                int r[4];
                somvr_vm_rect(item, SOMVR_VMP_ROW, r);
                if (rc[0] < r[0] || rc[1] < r[1] || rc[0] + rc[2] > r[0] + r[2] || rc[1] + rc[3] > r[1] + r[3]) contained = 0;
            }
        }
    CHECK(inside && back && contained, "rects inside the texture %d, centres hit back %d, boxes in their rows %d", inside,
          back, contained);
    int r0[4], r1[4], rb[4];
    /* every row shown (v5 + the size + build 13's crouch, fling and hands at walls: 21, closer) */
    const uint32_t ALLR = SOMVR_VMA_MOVES | SOMVR_VMA_MUSIC | SOMVR_VMA_SIZE | SOMVR_VMA_CROUCH | SOMVR_VMA_FLING |
                          SOMVR_VMA_SURFACE;
    somvr_vm_rect_av(ALLR, 0, SOMVR_VMP_ROW, r0);
    somvr_vm_rect_av(ALLR, SOMVR_VMI_ROWS - 1, SOMVR_VMP_ROW, r1);
    somvr_vm_rect_av(ALLR, SOMVR_VMI_RECENTER, SOMVR_VMP_BUTTON, rb);
    int overlap = 0;
    for (int i = 0; i + 1 < SOMVR_VMI_ROWS; i++) {
        int a[4], b[4];
        somvr_vm_rect_av(ALLR, i, SOMVR_VMP_ROW, a), somvr_vm_rect_av(ALLR, i + 1, SOMVR_VMP_ROW, b);
        overlap |= a[1] + a[3] > b[1];
    }
    /* all 21 rows (only with SOMVR_VMA_MOVES, which no build since 6 has): 28 px, apart, the boxes 26 */
    CHECK(!overlap && r1[1] + r1[3] <= rb[1] && rb[3] >= 48 && r0[3] >= 28, "rows apart, the button below them, sizes (%d px)",
          r0[3]);
    /* build 13's set (no MOVES: 18 rows): over the font's 32-pixel cell */
    somvr_vm_rect_av(ALLR & ~(uint32_t)SOMVR_VMA_MOVES, 0, SOMVR_VMP_ROW, r0);
    CHECK(r0[3] >= SOMVR_FONT_H * SOMVR_VM_SCALE, "build 13's 18 rows: %d px", r0[3]);
    somvr_vm_rect(0, SOMVR_VMP_ROW, r0);
    CHECK(r0[3] == 38, "the rows every build shows: 38 px");
    /* a box is at least 2 degrees wide seen from 40 cm (the pointer's target) */
    int b[4];
    somvr_vm_rect(0, SOMVR_VMP_LESS, b);
    double wm = b[2] * (double)SOMVR_VM_W_M / SOMVR_VM_TEX_W;
    CHECK(2 * atan(0.5 * wm / 0.4) * 57.29578 > 2.0, "the < box is %.1f deg wide at 40 cm", 2 * atan(0.5 * wm / 0.4) * 57.29578);
    CHECK(fabs((double)SOMVR_VM_W_M / SOMVR_VM_H_M - (double)SOMVR_VM_TEX_W / SOMVR_VM_TEX_H) < 1e-6, "the quad keeps the "
                                                                                                       "texture's aspect");
    somvr_vm_hit h = somvr_vm_hit_test(-0.01f, 0.5f);
    CHECK(h.item == -1, "left of the panel");
    h = somvr_vm_hit_test(0.5f, NAN);
    CHECK(h.item == -1, "NaN");
    h = somvr_vm_hit_test(6.0f / SOMVR_VM_TEX_W, 20.0f / SOMVR_VM_TEX_H);
    CHECK(h.item == -1, "left of the tab strip is no target");
    h = somvr_vm_hit_test(0.1f, 20.0f / SOMVR_VM_TEX_H);
    CHECK(h.item == SOMVR_VMI_TAB_SET && h.part == SOMVR_VMP_BUTTON, "the title bar: the Settings tab (%d)", h.item);
    somvr_vm_rect(SOMVR_VMI_CLOSE, SOMVR_VMP_BUTTON, rc);
    h = somvr_vm_hit_test(((float)rc[0] + 2) / SOMVR_VM_TEX_W, ((float)rc[1] + 2) / SOMVR_VM_TEX_H);
    CHECK(h.item == SOMVR_VMI_CLOSE && h.part == SOMVR_VMP_BUTTON, "the close box's corner");
    somvr_vm_rect(3, SOMVR_VMP_ROW, rc);
    h = somvr_vm_hit_test(380.0f / SOMVR_VM_TEX_W, ((float)rc[1] + 1) / SOMVR_VM_TEX_H);
    CHECK(h.item == 3 && h.part == SOMVR_VMP_ROW, "the value between the boxes: the row (%d %d)", h.item, h.part);
}

/* ---- the picture ------------------------------------------------------------------------------------------------ */
static uint8_t g_px[SOMVR_VM_TEX_W * SOMVR_VM_TEX_H * 4], g_px2[sizeof g_px];
static const uint8_t *pxa(const uint8_t *b, int x, int y) { return b + 4 * ((size_t)y * SOMVR_VM_TEX_W + (size_t)x); }

static void test_draw(void) {
    somvr_vm_view v;
    memset(&v, 0, sizeof v);
    somvr_set_default(&v.set);
    v.avail = SOMVR_VMA_HUD | SOMVR_VMA_COMFORT;
    v.hover.item = -1;
    somvr_vm_draw(&F, g_px, 0, &v);
    CHECK(pxa(g_px, 0, 0)[3] == 0 && pxa(g_px, SOMVR_VM_TEX_W - 1, SOMVR_VM_TEX_H - 1)[3] == 0 &&
              pxa(g_px, 3, 3)[3] == 0,
          "the corners are clear");
    CHECK(pxa(g_px, 256, 320)[3] == 255 && pxa(g_px, 0, 320)[3] > 0 && pxa(g_px, 0, 320)[3] < 255 &&
              pxa(g_px, 5, 2)[3] == 0,
          "opaque inside, a soft edge, the rounded corner");
    int premul = 1;
    for (size_t i = 0; i < SOMVR_VM_TEX_W * SOMVR_VM_TEX_H; i++)
        premul &= g_px[4 * i] <= g_px[4 * i + 3] && g_px[4 * i + 1] <= g_px[4 * i + 3] && g_px[4 * i + 2] <= g_px[4 * i + 3];
    CHECK(premul, "premultiplied");
    /* the label "Turn": its 'T' bar (row 2 of the glyph, columns 1..6) in the label colour, at scale 2 */
    int rc[4];
    somvr_vm_rect(SOMVR_VMI_TURN, SOMVR_VMP_ROW, rc);
    int ty = rc[1] + (rc[3] - 32) / 2;
    const uint8_t *tb = pxa(g_px, 20 + 2 * 2, ty + 2 * 2);
    CHECK(tb[0] == 208 && tb[1] == 213 && tb[2] == 222 && tb[3] == 255, "the 'T' of Turn: %u %u %u", tb[0], tb[1], tb[2]);
    /* Turn speed is dimmed (snap turns): its 'T' in the dim colour */
    somvr_vm_rect(SOMVR_VMI_SPEED, SOMVR_VMP_ROW, rc);
    tb = pxa(g_px, 20 + 2 * 2, rc[1] + (rc[3] - 32) / 2 + 2 * 2);
    CHECK(tb[0] == 96 && tb[1] == 101 && tb[2] == 112, "Turn speed dimmed: %u %u %u", tb[0], tb[1], tb[2]);
    /* hover: the row lit, its > box lit */
    somvr_vm_draw(&F, g_px2, 0, &v);
    CHECK(!memcmp(g_px, g_px2, sizeof g_px), "the same view draws the same picture");
    v.hover = (somvr_vm_hit){SOMVR_VMI_SNAP, SOMVR_VMP_MORE};
    somvr_vm_draw(&F, g_px2, 0, &v);
    int b[4];
    somvr_vm_rect(SOMVR_VMI_SNAP, SOMVR_VMP_ROW, rc);
    somvr_vm_rect(SOMVR_VMI_SNAP, SOMVR_VMP_MORE, b);
    const uint8_t *row0 = pxa(g_px, rc[0] + 2, rc[1] + 2), *row1 = pxa(g_px2, rc[0] + 2, rc[1] + 2);
    const uint8_t *box0 = pxa(g_px, b[0] + 2, b[1] + 2), *box1 = pxa(g_px2, b[0] + 2, b[1] + 2);
    CHECK(row1[0] > row0[0] && box1[2] > box0[2] && box1[2] > row1[2], "hover: row %u -> %u, box %u -> %u", row0[0],
          row1[0], box0[2], box1[2]);
    int outside_same = 1;
    for (int y = 0; y < SOMVR_VM_TEX_H; y++)
        for (int x = 0; x < SOMVR_VM_TEX_W; x++)
            if ((y < rc[1] || y >= rc[1] + rc[3]) && memcmp(pxa(g_px, x, y), pxa(g_px2, x, y), 4)) outside_same = 0;
    CHECK(outside_same, "hovering a row changes only that row");
    /* a hovered disabled row is not lit */
    v.hover = (somvr_vm_hit){SOMVR_VMI_SPEED, SOMVR_VMP_ROW};
    somvr_vm_draw(&F, g_px2, 0, &v);
    CHECK(!memcmp(g_px, g_px2, sizeof g_px), "a disabled row does not light up");
    /* another value: only the value area changes */
    v.hover.item = -1;
    v.set.snap_deg = 45;
    somvr_vm_draw(&F, g_px2, 0, &v);
    int changed = 0, elsewhere = 0;
    somvr_vm_rect(SOMVR_VMI_SNAP, SOMVR_VMP_ROW, rc);
    for (int y = 0; y < SOMVR_VM_TEX_H; y++)
        for (int x = 0; x < SOMVR_VM_TEX_W; x++)
            if (memcmp(pxa(g_px, x, y), pxa(g_px2, x, y), 4)) {
                int in = y >= rc[1] && y < rc[1] + rc[3] && x >= 304 && x < 448;
                changed += in, elsewhere += !in;
            }
    CHECK(changed > 20 && !elsewhere, "45 deg instead of 30: %d pixels in the value, %d elsewhere", changed, elsewhere);
    /* BGRA: R and B swapped, nothing else */
    v.set.snap_deg = 30;
    somvr_vm_draw(&F, g_px2, 1, &v);
    int swapped = 1;
    for (size_t i = 0; i < SOMVR_VM_TEX_W * SOMVR_VM_TEX_H; i++)
        swapped &= g_px2[4 * i] == g_px[4 * i + 2] && g_px2[4 * i + 2] == g_px[4 * i] && g_px2[4 * i + 1] == g_px[4 * i + 1] &&
                   g_px2[4 * i + 3] == g_px[4 * i + 3];
    CHECK(swapped, "BGRA swaps R and B");
}

/* ---- the gesture and the panel's pose --------------------------------------------------------------------------- */
static void qrot(const float q[4], const float v[3], float o[3]) {
    float u[3] = {q[0], q[1], q[2]}, w = q[3];
    float t[3] = {2 * (u[1] * v[2] - u[2] * v[1]), 2 * (u[2] * v[0] - u[0] * v[2]), 2 * (u[0] * v[1] - u[1] * v[0])};
    o[0] = v[0] + w * t[0] + (u[1] * t[2] - u[2] * t[1]);
    o[1] = v[1] + w * t[1] + (u[2] * t[0] - u[0] * t[2]);
    o[2] = v[2] + w * t[2] + (u[0] * t[1] - u[1] * t[0]);
}
static void norm(float v[3]) {
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for (int i = 0; i < 3; i++) v[i] /= l;
}
static void basis_quat(const float x[3], const float y[3], const float z[3], float q[4]) {
    float tr = x[0] + y[1] + z[2];
    if (tr > 0) {
        float s = sqrtf(tr + 1) * 2;
        q[3] = 0.25f * s, q[0] = (y[2] - z[1]) / s, q[1] = (z[0] - x[2]) / s, q[2] = (x[1] - y[0]) / s;
    } else if (x[0] > y[1] && x[0] > z[2]) {
        float s = sqrtf(1 + x[0] - y[1] - z[2]) * 2;
        q[3] = (y[2] - z[1]) / s, q[0] = 0.25f * s, q[1] = (y[0] + x[1]) / s, q[2] = (z[0] + x[2]) / s;
    } else if (y[1] > z[2]) {
        float s = sqrtf(1 + y[1] - x[0] - z[2]) * 2;
        q[3] = (z[0] - x[2]) / s, q[0] = (y[0] + x[1]) / s, q[1] = 0.25f * s, q[2] = (z[1] + y[2]) / s;
    } else {
        float s = sqrtf(1 + z[2] - x[0] - y[1]) * 2;
        q[3] = (x[1] - y[0]) / s, q[0] = (z[0] + x[2]) / s, q[1] = (z[1] + y[2]) / s, q[2] = 0.25f * s;
    }
}
/* a grip of hand h at p whose palm faces along n (the grip's +X for the left hand, -X for the right), its -Z (the
 * fingers' tube) as near to `fwd` as it can be */
static somvr_posef grip_facing(int h, const float p[3], const float n_in[3], const float fwd[3]) {
    float x[3] = {n_in[0], n_in[1], n_in[2]}, z[3], y[3];
    norm(x);
    if (h == SOMVR_HAND_R) x[0] = -x[0], x[1] = -x[1], x[2] = -x[2];
    float d = -(fwd[0] * x[0] + fwd[1] * x[1] + fwd[2] * x[2]);
    for (int i = 0; i < 3; i++) z[i] = -fwd[i] - d * x[i]; /* +Z = -fwd, minus its part along x */
    norm(z);
    y[0] = z[1] * x[2] - z[2] * x[1], y[1] = z[2] * x[0] - z[0] * x[2], y[2] = z[0] * x[1] - z[1] * x[0];
    somvr_posef g;
    basis_quat(x, y, z, g.q);
    memcpy(g.p, p, sizeof g.p);
    return g;
}

static const somvr_posef HEAD = {{0, 0, 0, 1}, {0, 1.6f, 0}}; /* looking along -Z */

/* the left hand 35 cm ahead of the eyes and 25 cm down, its palm at the face */
static somvr_vm_gin gin_facing(double t) {
    somvr_vm_gin in = {.t = t, .head_ok = 1, .head = HEAD, .hand_ok = 1, .hand = SOMVR_HAND_L};
    float p[3] = {-0.05f, 1.35f, -0.35f}, n[3] = {HEAD.p[0] - p[0], HEAD.p[1] - p[1], HEAD.p[2] - p[2]};
    static const float fwd[3] = {1, 0, 0};
    in.grip = grip_facing(SOMVR_HAND_L, p, n, fwd);
    return in;
}

static void test_pose(void) {
    somvr_vm_gcfg c;
    somvr_vm_gcfg_default(&c);
    somvr_posef id = {{0, 0, 0, 1}, {0, 0, 0}}, out;
    float n[3];
    somvr_vm_palm(&id, SOMVR_HAND_L, n);
    CHECK(NEAR(n[0], 1, 1e-6) && NEAR(n[1], 0, 1e-6), "the left palm: +X (%g %g %g)", n[0], n[1], n[2]);
    somvr_vm_palm(&id, SOMVR_HAND_R, n);
    CHECK(NEAR(n[0], -1, 1e-6), "the right palm: -X (%g)", n[0]);
    /* identity grip, left: the panel faces +X, upright, 15 cm up and 5 cm out */
    CHECK(somvr_vm_panel_pose(&id, SOMVR_HAND_L, NULL, &c, &out), "identity pose");
    float z[3], y[3], x[3];
    static const float ex[3] = {1, 0, 0}, ey[3] = {0, 1, 0}, ez[3] = {0, 0, 1};
    qrot(out.q, ez, z), qrot(out.q, ey, y), qrot(out.q, ex, x);
    CHECK(NEAR(z[0], 1, 1e-5) && NEAR(y[1], 1, 1e-5) && NEAR(x[2], -1, 1e-5) && NEAR(out.p[0], 0.05, 1e-6) &&
              NEAR(out.p[1], 0.15, 1e-6) && NEAR(out.p[2], 0, 1e-6),
          "panel: normal %g %g %g up %g %g %g at %g %g %g", z[0], z[1], z[2], y[0], y[1], y[2], out.p[0], out.p[1], out.p[2]);
    /* the facing hand: the panel faces the head, upright, no roll */
    somvr_vm_gin in = gin_facing(0);
    CHECK(somvr_vm_panel_pose(&in.grip, SOMVR_HAND_L, &HEAD, &c, &out), "facing pose");
    qrot(out.q, ez, z), qrot(out.q, ex, x);
    float to[3] = {HEAD.p[0] - in.grip.p[0], HEAD.p[1] - in.grip.p[1], HEAD.p[2] - in.grip.p[2]};
    norm(to);
    CHECK(z[0] * to[0] + z[1] * to[1] + z[2] * to[2] > 0.999f && fabsf(x[1]) < 1e-5f && x[0] > 0.9f,
          "the panel faces the head, its x level and to the right: z %g %g %g x %g %g %g", z[0], z[1], z[2], x[0], x[1], x[2]);
    float qn = out.q[0] * out.q[0] + out.q[1] * out.q[1] + out.q[2] * out.q[2] + out.q[3] * out.q[3];
    CHECK(NEAR(qn, 1, 1e-5), "unit quaternion");
    /* the right hand facing too: the same orientation */
    somvr_posef gr = grip_facing(SOMVR_HAND_R, in.grip.p, (float[3]){to[0], to[1], to[2]}, (float[3]){-1, 0, 0}), o2;
    somvr_vm_panel_pose(&gr, SOMVR_HAND_R, &HEAD, &c, &o2);
    float z2[3];
    qrot(o2.q, ez, z2);
    CHECK(z2[0] * z[0] + z2[1] * z[1] + z2[2] * z[2] > 0.999f, "the right palm: the same panel normal");
    /* the palm facing up: the world's up is along the normal, the head's up is used (here also +Y: then the grip's
     * forward); still a valid, unit, facing pose */
    somvr_posef up = grip_facing(SOMVR_HAND_L, in.grip.p, (float[3]){0, 1, 0}, (float[3]){0, 0, -1});
    CHECK(somvr_vm_panel_pose(&up, SOMVR_HAND_L, &HEAD, &c, &out), "palm up");
    qrot(out.q, ez, z), qrot(out.q, ey, y);
    CHECK(NEAR(z[1], 1, 1e-5) && fabsf(y[1]) < 1e-4f && NEAR(y[0] * y[0] + y[1] * y[1] + y[2] * y[2], 1, 1e-5),
          "palm up: normal %g %g %g up %g %g %g", z[0], z[1], z[2], y[0], y[1], y[2]);
    somvr_posef badq = {{0, 0, 0, 0}, {0, 0, 0}};
    CHECK(!somvr_vm_panel_pose(&badq, SOMVR_HAND_L, NULL, &c, &out), "a zero quaternion");
}

#define DT (1.0 / 90)
static void test_gesture(void) {
    somvr_vm_gcfg c;
    somvr_vm_gcfg_default(&c);
    somvr_vm_gstate g;
    somvr_vm_greset(&g);
    somvr_posef pan;
    int have = 0, ev = 0, f = 0;
    double t = 10;
    /* the palm toward the eyes: opens once held 0.5 s */
    for (; f < 44; f++, t += DT) { /* nothing tracked: nothing happens */
        somvr_vm_gin none = {.t = t};
        ev |= somvr_vm_gesture(&g, &c, &none, &pan, &have);
    }
    CHECK(ev == 0 && !g.open && !have, "untracked: event %d open %d panel %d", ev, g.open, have);
    double t0 = t;
    int opened_at = -1;
    for (int k = 0; k < 60; k++, t += DT) {
        somvr_vm_gin in = gin_facing(t);
        int e = somvr_vm_gesture(&g, &c, &in, &pan, &have);
        if (e > 0 && opened_at < 0) opened_at = k;
        if (e < 0) ev = -1;
    }
    CHECK((opened_at == 45 || opened_at == 46) && g.open && g.why == SOMVR_VMW_GESTURE && have && ev == 0 && g.opens == 1,
          "opened after %d frames (want 45: 0.5 s at 90 Hz), why %s", opened_at, somvr_vm_why(g.why));
    CHECK(t - t0 > 0.5, "time");
    /* still facing: stays open (not re-armed) */
    for (int k = 0; k < 200; k++, t += DT) {
        somvr_vm_gin in = gin_facing(t);
        CHECK(somvr_vm_gesture(&g, &c, &in, &pan, &have) == 0, "kept facing: no toggle at frame %d", k);
        if (!g.open) break;
    }
    CHECK(g.open, "still open");
    /* the palm turned down for 0.3 s while looking at the panel's place: re-armed, not closed (the gaze holds) */
    for (int k = 0; k < 27; k++, t += DT) {
        somvr_vm_gin in = gin_facing(t);
        in.grip = grip_facing(SOMVR_HAND_L, in.grip.p, (float[3]){0, -1, 0}, (float[3]){0, 0, -1});
        CHECK(somvr_vm_gesture(&g, &c, &in, &pan, &have) == 0, "palm down: no toggle");
    }
    CHECK(g.open && g.armed, "re-armed, open %d armed %d", g.open, g.armed);
    /* facing again 0.5 s: closes */
    int closed_at = -1;
    for (int k = 0; k < 60 && closed_at < 0; k++, t += DT) {
        somvr_vm_gin in = gin_facing(t);
        if (somvr_vm_gesture(&g, &c, &in, &pan, &have) < 0) closed_at = k;
    }
    CHECK((closed_at == 45 || closed_at == 46) && !g.open && g.why == SOMVR_VMW_GESTURE, "closed by the gesture after %d frames", closed_at);
    /* busy: the gesture does not close an open panel */
    somvr_vm_greset(&g);
    somvr_vm_gin in = gin_facing(t);
    in.toggles = 1;
    CHECK(somvr_vm_gesture(&g, &c, &in, &pan, &have) == 1 && g.why == SOMVR_VMW_BUTTON && !g.armed,
          "the menu button opens at once");
    for (int k = 0; k < 30; k++, t += DT) { /* the palm away: re-armed */
        in = gin_facing(t);
        in.grip = grip_facing(SOMVR_HAND_L, in.grip.p, (float[3]){0, -1, 0}, (float[3]){0, 0, -1});
        somvr_vm_gesture(&g, &c, &in, &pan, &have);
    }
    int toggled = 0;
    for (int k = 0; k < 120; k++, t += DT) {
        in = gin_facing(t);
        in.busy = 1;
        toggled |= somvr_vm_gesture(&g, &c, &in, &pan, &have) != 0;
    }
    CHECK(!toggled && g.open, "busy (pointing at it): no gesture close");
    in = gin_facing(t), in.toggles = 2;
    CHECK(somvr_vm_gesture(&g, &c, &in, &pan, &have) == 0 && g.open, "two presses in a frame: no change");
    in.toggles = 1;
    CHECK(somvr_vm_gesture(&g, &c, &in, &pan, &have) == -1 && !g.open && g.why == SOMVR_VMW_BUTTON, "the button closes");
    /* looking away: closes after 0.5 s, not while busy */
    in = gin_facing(t), in.toggles = 1;
    somvr_vm_gesture(&g, &c, &in, &pan, &have);
    t += DT;
    somvr_posef away = {{0, sinf(0.5f * 1.6f), 0, cosf(0.5f * 1.6f)}, {0, 1.6f, 0}}; /* turned 92 deg left */
    toggled = 0;
    for (int k = 0; k < 90; k++, t += DT) {
        in = gin_facing(t), in.head = away, in.busy = 1;
        toggled |= somvr_vm_gesture(&g, &c, &in, &pan, &have) != 0;
    }
    CHECK(!toggled && g.open, "busy while looking away: open");
    closed_at = -1;
    for (int k = 0; k < 90 && closed_at < 0; k++, t += DT) {
        in = gin_facing(t), in.head = away;
        if (somvr_vm_gesture(&g, &c, &in, &pan, &have) < 0) closed_at = k;
    }
    CHECK((closed_at == 45 || closed_at == 46) && g.why == SOMVR_VMW_AWAY, "looked away: closed after %d frames (%s)", closed_at,
          somvr_vm_why(g.why));
    /* the hand lost 1 s: closed */
    in = gin_facing(t), in.toggles = 1;
    somvr_vm_gesture(&g, &c, &in, &pan, &have);
    t += DT;
    closed_at = -1;
    for (int k = 0; k < 120 && closed_at < 0; k++, t += DT) {
        in = gin_facing(t), in.hand_ok = 0;
        if (somvr_vm_gesture(&g, &c, &in, &pan, &have) < 0) closed_at = k;
        if (have) closed_at = -2;
    }
    CHECK((closed_at == 90 || closed_at == 91) && g.why == SOMVR_VMW_LOST, "the hand lost: closed after %d frames", closed_at);
    /* no open: the hand 1 m away, the palm not at the eyes, the panel not looked at, no head */
    struct { const char *what; int kind; } no[] = {{"the hand 1 m away", 0}, {"the palm sideways", 1},
                                                   {"not looked at", 2}, {"no head pose", 3}};
    for (size_t i = 0; i < sizeof no / sizeof no[0]; i++) {
        somvr_vm_greset(&g);
        int opened = 0;
        for (int k = 0; k < 120; k++, t += DT) {
            in = gin_facing(t);
            if (no[i].kind == 0) {
                float p[3] = {-0.05f, 1.2f, -0.95f}, n[3] = {HEAD.p[0] - p[0], HEAD.p[1] - p[1], HEAD.p[2] - p[2]};
                in.grip = grip_facing(SOMVR_HAND_L, p, n, (float[3]){1, 0, 0});
            } else if (no[i].kind == 1) {
                in.grip = grip_facing(SOMVR_HAND_L, in.grip.p, (float[3]){1, 0, 0}, (float[3]){0, 0, -1});
            } else if (no[i].kind == 2) {
                in.head = away;
            } else {
                in.head_ok = 0;
            }
            opened |= somvr_vm_gesture(&g, &c, &in, &pan, &have) > 0;
        }
        CHECK(!opened, "%s: opened", no[i].what);
    }
}

/* ---- v2's rows: the assisted step, the dash, the wraith phase (somvr_step.h, re-xr-pipeline.md 23) ----------------- */
static void test_moves(void) {
    somvr_settings s;
    somvr_set_default(&s);
    CHECK(s.step == 0 && s.dash == 0 && s.phase == 0, "all three off by default");
    CHECK(!strcmp(somvr_set_key(SOMVR_SK_STEP), "STEP") && !strcmp(somvr_set_env(SOMVR_SK_DASH), "SOMVR_DASH") &&
              !strcmp(somvr_set_env(SOMVR_SK_PHASE), "SOMVR_PHASE") && somvr_set_find("SOMVR_PHASE") == SOMVR_SK_PHASE &&
              somvr_set_find("STEP") == SOMVR_SK_STEP,
          "the keys and their switches");
    char v[32];
    CHECK(somvr_set_put(&s, SOMVR_SK_STEP, " 0.5 ") && NEAR(s.step, 0.5, 1e-6) && somvr_set_put(&s, SOMVR_SK_STEP, "3") &&
              s.step == 1 && somvr_set_put(&s, SOMVR_SK_STEP, "-1") && s.step == 0 && !somvr_set_put(&s, SOMVR_SK_STEP, "on"),
          "STEP: a number clamped to 0..1");
    CHECK(somvr_set_put(&s, SOMVR_SK_DASH, "blink") && s.dash == 2 && !somvr_set_put(&s, SOMVR_SK_DASH, "2") && s.dash == 2 &&
              somvr_set_put(&s, SOMVR_SK_PHASE, "reface") && s.phase == 2 && !somvr_set_put(&s, SOMVR_SK_PHASE, "x"),
          "DASH, PHASE: their words");
    somvr_set_get(&s, SOMVR_SK_PHASE, v, sizeof v);
    CHECK(!strcmp(v, "reface"), "PHASE as the file writes it: %s", v);
    s.step = 0.75f;
    char file[2048];
    somvr_set_format(&s, (1u << SOMVR_SK_STEP) | (1u << SOMVR_SK_DASH) | (1u << SOMVR_SK_PHASE), file, sizeof file);
    CHECK(strstr(file, "STEP=0.75\nDASH=blink\nPHASE=reface\n") != NULL, "the file: %s", file);
    somvr_settings b;
    somvr_set_default(&b);
    int bad = 0;
    uint32_t m = somvr_set_parse(&b, file, &bad, NULL, 0);
    CHECK(m == ((1u << SOMVR_SK_STEP) | (1u << SOMVR_SK_DASH) | (1u << SOMVR_SK_PHASE)) && !bad && !somvr_set_diff(&s, &b),
          "the round trip: mask %#x bad %d diff %#x", m, bad, somvr_set_diff(&s, &b));
    /* the rows: dimmed without the LOCO core, the values, the clicks */
    somvr_set_default(&s);
    CHECK(!somvr_vm_enabled(&s, 0, SOMVR_VMI_STEP) && !somvr_vm_enabled(&s, SOMVR_VMA_COMFORT, SOMVR_VMI_PHASE) &&
              somvr_vm_enabled(&s, SOMVR_VMA_MOVES, SOMVR_VMI_DASH) &&
              somvr_vm_apply(&s, 0, 0, SOMVR_VMI_STEP, SOMVR_VMP_MORE) == SOMVR_VMR_NONE && s.step == 0,
          "enabled with SOMVR_VMA_MOVES only");
    CHECK(!strcmp(somvr_vm_label(SOMVR_VMI_STEP), "Assisted step") && !strcmp(somvr_vm_label(SOMVR_VMI_PHASE), "Wraith phase"),
          "labels");
    somvr_vm_value(&s, SOMVR_VMI_STEP, v, sizeof v);
    CHECK(!strcmp(v, "Off"), "the step at 0: %s", v);
    const uint32_t A = SOMVR_VMA_MOVES;
    CHECK(somvr_vm_apply(&s, A, 0, SOMVR_VMI_STEP, SOMVR_VMP_MORE) == SOMVR_VMR_CHANGED && NEAR(s.step, 0.25, 1e-6), "step +");
    somvr_vm_value(&s, SOMVR_VMI_STEP, v, sizeof v);
    CHECK(!strcmp(v, "25%"), "25%%: %s", v);
    for (int i = 0; i < 6; i++)
        somvr_vm_apply(&s, A, 0, SOMVR_VMI_STEP, SOMVR_VMP_MORE);
    CHECK(s.step == 1 && somvr_vm_apply(&s, A, 0, SOMVR_VMI_STEP, SOMVR_VMP_MORE) == SOMVR_VMR_NONE, "held at 100%%");
    CHECK(somvr_vm_apply(&s, A, 0, SOMVR_VMI_STEP, SOMVR_VMP_ROW) == SOMVR_VMR_CHANGED && s.step == 0, "the row wraps to Off");
    CHECK(somvr_vm_apply(&s, A, 0, SOMVR_VMI_DASH, SOMVR_VMP_LESS) == SOMVR_VMR_NONE && s.dash == 0 &&
              somvr_vm_apply(&s, A, 0, SOMVR_VMI_DASH, SOMVR_VMP_MORE) == SOMVR_VMR_CHANGED && s.dash == 1 &&
              somvr_vm_apply(&s, A, 0, SOMVR_VMI_DASH, SOMVR_VMP_MORE) == SOMVR_VMR_CHANGED && s.dash == 2 &&
              somvr_vm_apply(&s, A, 0, SOMVR_VMI_DASH, SOMVR_VMP_MORE) == SOMVR_VMR_NONE &&
              somvr_vm_apply(&s, A, 0, SOMVR_VMI_DASH, SOMVR_VMP_ROW) == SOMVR_VMR_CHANGED && s.dash == 0,
          "the dash: Off < On < Blink, held, the row wraps: %u", s.dash);
    static const char *const ph[] = {"Off", "On", "Re-face", "Blink"};
    for (uint32_t i = 0; i < 4; i++) {
        s.phase = i;
        somvr_vm_value(&s, SOMVR_VMI_PHASE, v, sizeof v);
        CHECK(!strcmp(v, ph[i]) && somvr_font_text_w(v, SOMVR_VM_SCALE) <= 448 - 304, "the phase %u: %s", i, v);
    }
    CHECK(somvr_vm_apply(&s, A, 0, SOMVR_VMI_PHASE, SOMVR_VMP_ROW) == SOMVR_VMR_CHANGED && s.phase == 0, "the phase wraps");
    s.dash = 7;
    somvr_vm_value(&s, SOMVR_VMI_DASH, v, sizeof v);
    somvr_set_get(&s, SOMVR_SK_DASH, file, 32);
    CHECK(!strcmp(v, "Off") && !strcmp(file, "off"), "a bad value shows Off: %s %s", v, file);
    /* the rows fit the 768 panel: the Recentre button below the rows, inside the texture */
    int rc[4], rr[4];
    CHECK(somvr_vm_rect_av(A, SOMVR_VMI_PHASE, SOMVR_VMP_ROW, rc) && somvr_vm_rect(SOMVR_VMI_RECENTER, SOMVR_VMP_BUTTON, rr) &&
              rc[1] + rc[3] <= rr[1] && rr[1] + rr[3] <= SOMVR_VM_TEX_H - 20,
          "the layout: the phase row ends at %d, the button %d..%d of %d", rc[1] + rc[3], rr[1], rr[1] + rr[3], SOMVR_VM_TEX_H);
}

/* ---- v4: the sword angle (re-xr-pipeline.md 29): the key, the row, its steps, the 16-row layout ------------------- */
static void test_sword(void) {
    CHECK(SOMVR_VRMENU_VERSION == 5 && sizeof(somvr_settings) == 80 && offsetof(somvr_settings, sword_deg) == 64 &&
              sizeof(somvr_vrmenu_shared) == 184 && offsetof(somvr_vrmenu_shared, gen) == 120 &&
              offsetof(somvr_vrmenu_shared, block) == 140 && offsetof(somvr_vrmenu_shared, n_frames) == 144,
          "the v4 fields in the v5 ABI");
    somvr_settings s;
    somvr_set_default(&s);
    char v[32], file[2048];
    CHECK(s.sword_deg == 0 && !strcmp(somvr_set_key(SOMVR_SK_SWORD_ANGLE), "SWORD_ANGLE") &&
              !strcmp(somvr_set_env(SOMVR_SK_SWORD_ANGLE), "SOMVR_SWORD_ANGLE") &&
              somvr_set_find("SOMVR_SWORD_ANGLE") == SOMVR_SK_SWORD_ANGLE && somvr_set_find("SWORD_ANGLE") == SOMVR_SK_SWORD_ANGLE,
          "the key, its switch, the default 0");
    CHECK(somvr_set_put(&s, SOMVR_SK_SWORD_ANGLE, " -12.5 ") && NEAR(s.sword_deg, -12.5, 1e-6) &&
              somvr_set_put(&s, SOMVR_SK_SWORD_ANGLE, "120") && s.sword_deg == 90 &&
              somvr_set_put(&s, SOMVR_SK_SWORD_ANGLE, "-100") && s.sword_deg == -90 &&
              !somvr_set_put(&s, SOMVR_SK_SWORD_ANGLE, "up"),
          "SWORD_ANGLE: a number clamped to -90..90 (as SOMVR_SWORD_ANGLE)");
    s.sword_deg = 15;
    somvr_set_get(&s, SOMVR_SK_SWORD_ANGLE, v, sizeof v);
    somvr_set_format(&s, 1u << SOMVR_SK_SWORD_ANGLE, file, sizeof file);
    somvr_settings b;
    somvr_set_default(&b);
    int bad = 0;
    uint32_t m = somvr_set_parse(&b, file, &bad, NULL, 0);
    CHECK(!strcmp(v, "15") && strstr(file, "SWORD_ANGLE=15\n") && m == 1u << SOMVR_SK_SWORD_ANGLE && !bad &&
              b.sword_deg == 15 && somvr_set_diff(&s, &b) == 0,
          "the file round trip: %s / %s", v, file);
    b.sword_deg = -5;
    CHECK(somvr_set_diff(&s, &b) == 1u << SOMVR_SK_SWORD_ANGLE, "the diff sees it");
    /* the row: dimmed without SOMVR_VMA_SWORD; the values; 5 deg steps from -45 to 45, held at the ends; the row wraps */
    const uint32_t A = SOMVR_VMA_SWORD;
    somvr_set_default(&s);
    CHECK(!strcmp(somvr_vm_label(SOMVR_VMI_SWORD), "Sword angle") && !somvr_vm_enabled(&s, SOMVR_VMA_MOVES, SOMVR_VMI_SWORD) &&
              somvr_vm_enabled(&s, A, SOMVR_VMI_SWORD) &&
              somvr_vm_apply(&s, 0, 0, SOMVR_VMI_SWORD, SOMVR_VMP_MORE) == SOMVR_VMR_NONE && s.sword_deg == 0,
          "the row needs SOMVR_VMA_SWORD");
    static const struct { float d; const char *t; } vals[] = {{0, "0\x7f"}, {5, "+5\x7f"}, {-10, "-10\x7f"}, {45, "+45\x7f"},
                                                               {-45, "-45\x7f"}, {12.5f, "+12.5\x7f"}};
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        s.sword_deg = vals[i].d;
        somvr_vm_value(&s, SOMVR_VMI_SWORD, v, sizeof v);
        CHECK(!strcmp(v, vals[i].t) && somvr_font_text_w(v, SOMVR_VM_SCALE) <= 448 - 304, "%g deg shows %s", vals[i].d, v);
    }
    s.sword_deg = 0;
    CHECK(somvr_vm_apply(&s, A, 0, SOMVR_VMI_SWORD, SOMVR_VMP_MORE) == SOMVR_VMR_CHANGED && s.sword_deg == 5 &&
              somvr_vm_apply(&s, A, 0, SOMVR_VMI_SWORD, SOMVR_VMP_LESS) == SOMVR_VMR_CHANGED && s.sword_deg == 0 &&
              somvr_vm_apply(&s, A, 0, SOMVR_VMI_SWORD, SOMVR_VMP_LESS) == SOMVR_VMR_CHANGED && s.sword_deg == -5,
          "5 deg steps: %g", s.sword_deg);
    int n = 0;
    while (somvr_vm_apply(&s, A, 0, SOMVR_VMI_SWORD, SOMVR_VMP_LESS) == SOMVR_VMR_CHANGED && n < 40) n++;
    CHECK(s.sword_deg == -45 && n == 8, "held at -45 after %d more", n);
    CHECK(somvr_vm_apply(&s, A, 0, SOMVR_VMI_SWORD, SOMVR_VMP_ROW) == SOMVR_VMR_CHANGED && s.sword_deg == -40, "the row: next");
    s.sword_deg = 45;
    CHECK(somvr_vm_apply(&s, A, 0, SOMVR_VMI_SWORD, SOMVR_VMP_MORE) == SOMVR_VMR_NONE &&
              somvr_vm_apply(&s, A, 0, SOMVR_VMI_SWORD, SOMVR_VMP_ROW) == SOMVR_VMR_CHANGED && s.sword_deg == -45,
          "held at +45, the row wraps to -45");
    s.sword_deg = 12.5f; /* a file's odd value: the next step from it */
    CHECK(somvr_vm_apply(&s, A, 0, SOMVR_VMI_SWORD, SOMVR_VMP_MORE) == SOMVR_VMR_CHANGED && s.sword_deg == 15 &&
              (s.sword_deg = 12.5f, somvr_vm_apply(&s, A, 0, SOMVR_VMI_SWORD, SOMVR_VMP_LESS) == SOMVR_VMR_CHANGED) &&
              s.sword_deg == 10,
          "from 12.5: up 15, down 10");
    /* every row shown (v5 + the player size + build 13's crouch, fling and hands at walls: 21): the sword row below the
     * phase row, the bottom buttons below them and inside the texture */
    const uint32_t ALLR = SOMVR_VMA_MOVES | SOMVR_VMA_SWORD | SOMVR_VMA_MUSIC | SOMVR_VMA_SIZE | SOMVR_VMA_CROUCH |
                          SOMVR_VMA_FLING | SOMVR_VMA_SURFACE;
    int rp[4], rs[4], rr[4], rq[4], lb[4], mb[4];
    memset(rp, 0, sizeof rp), memset(rs, 0, sizeof rs), memset(rr, 0, sizeof rr), memset(rq, 0, sizeof rq);
    memset(lb, 0, sizeof lb), memset(mb, 0, sizeof mb);
    CHECK(SOMVR_VMI_ROWS == 21 && SOMVR_VMI_SWORD == SOMVR_VMI_ROWS - 6 && somvr_vm_rect_av(ALLR, SOMVR_VMI_PHASE, SOMVR_VMP_ROW, rp) &&
              somvr_vm_rect_av(ALLR, SOMVR_VMI_SWORD, SOMVR_VMP_ROW, rs) &&
              somvr_vm_rect_av(ALLR, SOMVR_VMI_RECENTER, SOMVR_VMP_BUTTON, rr) &&
              somvr_vm_rect_av(ALLR, SOMVR_VMI_QUIT, SOMVR_VMP_BUTTON, rq) && somvr_vm_rect_av(ALLR, SOMVR_VMI_SWORD, SOMVR_VMP_LESS, lb) &&
              somvr_vm_rect_av(ALLR, SOMVR_VMI_SWORD, SOMVR_VMP_MORE, mb) && rp[1] + rp[3] <= rs[1] && rs[1] + rs[3] <= rr[1] &&
              rq[1] + rq[3] <= SOMVR_VM_TEX_H - 28 && lb[3] == rs[3] - 2 && mb[3] == lb[3] && lb[1] >= rs[1] &&
              lb[1] + lb[3] <= rs[1] + rs[3],
          "the layout: phase %d..%d, sword %d..%d, the buttons %d..%d of %d", rp[1], rp[1] + rp[3], rs[1], rs[1] + rs[3],
          rr[1], rr[1] + rr[3], SOMVR_VM_TEX_H);
    somvr_vm_hit h = somvr_vm_hit_test_av(ALLR, SOMVR_VMPG_SETTINGS, (mb[0] + 0.5f * mb[2]) / SOMVR_VM_TEX_W,
                                          (mb[1] + 0.5f * mb[3]) / SOMVR_VM_TEX_H);
    CHECK(h.item == SOMVR_VMI_SWORD && h.part == SOMVR_VMP_MORE, "its > box hits back (%d %d)", h.item, h.part);
}

/* ---- the player size (re-xr-pipeline.md 35): the key in %, the row at the end of the settings, 100 .. 140 by 5 ---- */
static void test_size(void) {
    somvr_settings s, b;
    char v[32], file[512];
    somvr_set_default(&s);
    CHECK(s.size == 1.0f && offsetof(somvr_settings, size) == 68 && sizeof(somvr_settings) == 80 &&
              !strcmp(somvr_set_key(SOMVR_SK_PLAYER_SIZE), "PLAYER_SIZE") &&
              !strcmp(somvr_set_env(SOMVR_SK_PLAYER_SIZE), "SOMVR_PLAYER_SIZE") &&
              somvr_set_find("SOMVR_PLAYER_SIZE") == SOMVR_SK_PLAYER_SIZE && SOMVR_SK_PLAYER_SIZE == SOMVR_SK_N - 4,
          "the default 100 %%, the old pad's place, the key and its switch");
    CHECK(somvr_set_put(&s, SOMVR_SK_PLAYER_SIZE, " 115 ") && NEAR(s.size, 1.15, 1e-6) &&
              somvr_set_put(&s, SOMVR_SK_PLAYER_SIZE, "300") && NEAR(s.size, 1.4, 1e-6) &&
              somvr_set_put(&s, SOMVR_SK_PLAYER_SIZE, "50") && NEAR(s.size, 1.0, 1e-6) &&
              !somvr_set_put(&s, SOMVR_SK_PLAYER_SIZE, "big"),
          "put: percent, clamped to 100 .. 140, words refused (%.3f)", s.size);
    s.size = 1.25f;
    somvr_set_get(&s, SOMVR_SK_PLAYER_SIZE, v, sizeof v);
    somvr_set_format(&s, 1u << SOMVR_SK_PLAYER_SIZE, file, sizeof file);
    somvr_set_default(&b);
    int bad = 0;
    char note[64];
    uint32_t m = somvr_set_parse(&b, file, &bad, note, sizeof note);
    CHECK(!strcmp(v, "125") && strstr(file, "PLAYER_SIZE=125\n") && m == 1u << SOMVR_SK_PLAYER_SIZE && !bad &&
              NEAR(b.size, 1.25, 1e-6) && somvr_set_diff(&s, &b) == 0,
          "the file round trip: '%s'", v);
    somvr_set_default(&b);
    CHECK(somvr_set_diff(&s, &b) == 1u << SOMVR_SK_PLAYER_SIZE, "the diff sees it");
    /* the row: last of the settings, enabled with SOMVR_VMA_SIZE only, "100%" .. "140%", held at the ends, the row
     * wraps */
    const uint32_t A = SOMVR_VMA_SIZE;
    somvr_set_default(&s);
    CHECK(SOMVR_VMI_SIZE == SOMVR_VMI_ROWS - 4 && !strcmp(somvr_vm_label(SOMVR_VMI_SIZE), "Player size") &&
              !somvr_vm_enabled(&s, SOMVR_VMA_SWORD, SOMVR_VMI_SIZE) && somvr_vm_enabled(&s, A, SOMVR_VMI_SIZE) &&
              somvr_vm_apply(&s, 0, 0, SOMVR_VMI_SIZE, SOMVR_VMP_MORE) == SOMVR_VMR_NONE && s.size == 1.0f,
          "the row, its switch");
    somvr_vm_value(&s, SOMVR_VMI_SIZE, v, sizeof v);
    CHECK(!strcmp(v, "100%") && somvr_vm_apply(&s, A, 0, SOMVR_VMI_SIZE, SOMVR_VMP_LESS) == SOMVR_VMR_NONE,
          "100%% shown ('%s'), held at the bottom", v);
    int n = 0;
    while (somvr_vm_apply(&s, A, 0, SOMVR_VMI_SIZE, SOMVR_VMP_MORE) == SOMVR_VMR_CHANGED && n < 20) n++;
    somvr_vm_value(&s, SOMVR_VMI_SIZE, v, sizeof v);
    CHECK(n == 8 && NEAR(s.size, 1.4, 1e-6) && !strcmp(v, "140%"), "8 steps of 5 %% up to '%s' (%d)", v, n);
    CHECK(somvr_vm_apply(&s, A, 0, SOMVR_VMI_SIZE, SOMVR_VMP_ROW) == SOMVR_VMR_CHANGED && NEAR(s.size, 1.0, 1e-6) &&
              somvr_vm_apply(&s, A, 0, SOMVR_VMI_SIZE, SOMVR_VMP_LESS) == SOMVR_VMR_NONE &&
              somvr_vm_apply(&s, A, 0, SOMVR_VMI_SIZE, SOMVR_VMP_MORE) == SOMVR_VMR_CHANGED && NEAR(s.size, 1.05, 1e-6),
          "the row wraps to 100 %%, then up 105");
    s.size = 1.12f; /* a file's odd value: the next step from it */
    CHECK(somvr_vm_apply(&s, A, 0, SOMVR_VMI_SIZE, SOMVR_VMP_MORE) == SOMVR_VMR_CHANGED && NEAR(s.size, 1.15, 1e-6),
          "from 112: up 115");
    int rs[4], rz[4], rr[4], rq[4], mb[4];
    const uint32_t AV = SOMVR_VMA_SIZE; /* the row is shown only with it (as Music with SOMVR_VMA_MUSIC) */
    CHECK(!somvr_vm_shown(0, SOMVR_VMI_SIZE) && somvr_vm_shown(AV, SOMVR_VMI_SIZE), "the row shown with SOMVR_VMA_SIZE only");
    CHECK(somvr_vm_rect_av(AV, SOMVR_VMI_SWORD, SOMVR_VMP_ROW, rs) && somvr_vm_rect_av(AV, SOMVR_VMI_SIZE, SOMVR_VMP_ROW, rz) &&
              somvr_vm_rect_av(AV, SOMVR_VMI_RECENTER, SOMVR_VMP_BUTTON, rr) && somvr_vm_rect_av(AV, SOMVR_VMI_QUIT, SOMVR_VMP_BUTTON, rq) &&
              somvr_vm_rect_av(AV, SOMVR_VMI_SIZE, SOMVR_VMP_MORE, mb) && rs[1] + rs[3] <= rz[1] && rz[1] + rz[3] <= rr[1] &&
              rq[1] + rq[3] <= SOMVR_VM_TEX_H - 20 && rz[3] >= 36,
          "the layout: sword %d..%d, size %d..%d, the buttons %d..%d of %d", rs[1], rs[1] + rs[3], rz[1], rz[1] + rz[3],
          rr[1], rq[1] + rq[3], SOMVR_VM_TEX_H);
    somvr_vm_hit h = somvr_vm_hit_test_av(AV, SOMVR_VMPG_SETTINGS, (mb[0] + 0.5f * mb[2]) / SOMVR_VM_TEX_W,
                                          (mb[1] + 0.5f * mb[3]) / SOMVR_VM_TEX_H);
    CHECK(h.item == SOMVR_VMI_SIZE && h.part == SOMVR_VMP_MORE, "its > box hits back (%d %d)", h.item, h.part);
}

/* ---- build 13: the Climb fling and Hands at walls rows: keys = the switches (off|on), the last two rows, shown with
 * SOMVR_VMA_FLING / SOMVR_VMA_SURFACE; the bytes after the crouch's ---------------------------------------------- */
static void test_feel(void) {
    somvr_settings s, b;
    char v[32], f[32], file[512], note[64];
    int bad = 0;
    somvr_set_default(&s);
    CHECK(s.fling == 0 && s.surface == 0 && offsetof(somvr_settings, fling) == 77 &&
              offsetof(somvr_settings, surface) == 78 && sizeof(somvr_settings) == 80 &&
              !strcmp(somvr_set_key(SOMVR_SK_CLIMB_FLING), "CLIMB_FLING") &&
              !strcmp(somvr_set_env(SOMVR_SK_CLIMB_FLING), "SOMVR_CLIMB_FLING") &&
              !strcmp(somvr_set_key(SOMVR_SK_HANDS_SURFACE), "HANDS_SURFACE") &&
              somvr_set_find("SOMVR_HANDS_SURFACE") == SOMVR_SK_HANDS_SURFACE && SOMVR_SK_HANDS_SURFACE == SOMVR_SK_N - 1 &&
              SOMVR_SK_CLIMB_FLING == SOMVR_SK_N - 2,
          "the defaults off, the bytes 77 and 78, the keys and their switches");
    CHECK(somvr_set_put(&s, SOMVR_SK_CLIMB_FLING, " on ") && s.fling == 1 && somvr_set_put(&s, SOMVR_SK_HANDS_SURFACE, "1") &&
              s.surface == 1 && somvr_set_put(&s, SOMVR_SK_HANDS_SURFACE, "off") && s.surface == 0 &&
              !somvr_set_put(&s, SOMVR_SK_CLIMB_FLING, "fast") && s.fling == 1,
          "put: the switches' words");
    s.surface = 1;
    somvr_set_get(&s, SOMVR_SK_CLIMB_FLING, v, sizeof v), somvr_set_get(&s, SOMVR_SK_HANDS_SURFACE, f, sizeof f);
    somvr_set_format(&s, (1u << SOMVR_SK_CLIMB_FLING) | (1u << SOMVR_SK_HANDS_SURFACE), file, sizeof file);
    somvr_set_default(&b);
    uint32_t m = somvr_set_parse(&b, file, &bad, note, sizeof note);
    CHECK(!strcmp(v, "on") && !strcmp(f, "on") && strstr(file, "CLIMB_FLING=on\n") && strstr(file, "HANDS_SURFACE=on\n") &&
              m == ((1u << SOMVR_SK_CLIMB_FLING) | (1u << SOMVR_SK_HANDS_SURFACE)) && !bad && b.fling == 1 && b.surface == 1 &&
              somvr_set_diff(&s, &b) == 0 && b.crouch == 0,
          "the file round trip");
    somvr_set_default(&s);
    CHECK(SOMVR_VMI_FLING == SOMVR_VMI_ROWS - 2 && SOMVR_VMI_SURFACE == SOMVR_VMI_ROWS - 1 &&
              !strcmp(somvr_vm_label(SOMVR_VMI_FLING), "Climb fling") &&
              !strcmp(somvr_vm_label(SOMVR_VMI_SURFACE), "Hands at walls") && !somvr_vm_shown(0, SOMVR_VMI_FLING) &&
              !somvr_vm_shown(SOMVR_VMA_SURFACE, SOMVR_VMI_FLING) && somvr_vm_shown(SOMVR_VMA_FLING, SOMVR_VMI_FLING) &&
              !somvr_vm_shown(SOMVR_VMA_FLING, SOMVR_VMI_SURFACE) && somvr_vm_shown(SOMVR_VMA_SURFACE, SOMVR_VMI_SURFACE) &&
              somvr_vm_enabled(&s, SOMVR_VMA_FLING, SOMVR_VMI_FLING) && !somvr_vm_enabled(&s, 0, SOMVR_VMI_SURFACE),
          "the last two rows, shown and enabled with their bits only");
    s.fling = 1;
    somvr_vm_value(&s, SOMVR_VMI_FLING, v, sizeof v), somvr_vm_value(&s, SOMVR_VMI_SURFACE, f, sizeof f);
    CHECK(!strcmp(v, "On") && !strcmp(f, "Off") &&
              somvr_vm_apply(&s, SOMVR_VMA_FLING, 0, SOMVR_VMI_FLING, SOMVR_VMP_LESS) == SOMVR_VMR_CHANGED && s.fling == 0 &&
              somvr_vm_apply(&s, 0, 0, SOMVR_VMI_SURFACE, SOMVR_VMP_ROW) == SOMVR_VMR_NONE && s.surface == 0 &&
              somvr_vm_apply(&s, SOMVR_VMA_SURFACE, 0, SOMVR_VMI_SURFACE, SOMVR_VMP_ROW) == SOMVR_VMR_CHANGED &&
              s.surface == 1,
          "the values, a press toggles (not without the bit)");
    /* build 13's rows on the page: the crouch, the fling, the hands at walls the last three, apart, above Recentre */
    const uint32_t B13 = SOMVR_VMA_MUSIC | SOMVR_VMA_SIZE | SOMVR_VMA_SWORD | SOMVR_VMA_CROUCH | SOMVR_VMA_FLING |
                         SOMVR_VMA_SURFACE | SOMVR_VMA_COMFORT | SOMVR_VMA_HUD | SOMVR_VMA_VIEW;
    int rc[4], rf[4], rh[4], rr[4];
    memset(rc, 0, sizeof rc), memset(rf, 0, sizeof rf), memset(rh, 0, sizeof rh), memset(rr, 0, sizeof rr);
    CHECK(somvr_vm_rect_av(B13, SOMVR_VMI_CROUCH, SOMVR_VMP_ROW, rc) && somvr_vm_rect_av(B13, SOMVR_VMI_FLING, SOMVR_VMP_ROW, rf) &&
              somvr_vm_rect_av(B13, SOMVR_VMI_SURFACE, SOMVR_VMP_ROW, rh) &&
              somvr_vm_rect_av(B13, SOMVR_VMI_RECENTER, SOMVR_VMP_BUTTON, rr) && rc[1] + rc[3] <= rf[1] &&
              rf[1] + rf[3] <= rh[1] && rh[1] + rh[3] <= rr[1] && rh[3] >= SOMVR_FONT_H * SOMVR_VM_SCALE,
          "build 13's page: crouch %d, fling %d, hands %d (%d px), Recentre %d", rc[1], rf[1], rh[1], rh[3], rr[1]);
    somvr_vm_hit h = somvr_vm_hit_test_av(B13, SOMVR_VMPG_SETTINGS, (rh[0] + 20.0f) / SOMVR_VM_TEX_W,
                                          (rh[1] + 0.5f * rh[3]) / SOMVR_VM_TEX_H);
    CHECK(h.item == SOMVR_VMI_SURFACE && h.part == SOMVR_VMP_ROW, "the hands row hits back (%d %d)", h.item, h.part);
}

/* ---- build 13: the Crouch row (re-xr-pipeline.md 41): the key, button|duck, the last row, shown with SOMVR_CROUCH --- */
static void test_crouch(void) {
    somvr_settings s, b;
    char v[32], file[512];
    somvr_set_default(&s);
    CHECK(s.crouch == 0 && offsetof(somvr_settings, crouch) == 76 && sizeof(somvr_settings) == 80 &&
              !strcmp(somvr_set_key(SOMVR_SK_CROUCH), "CROUCH") && !strcmp(somvr_set_env(SOMVR_SK_CROUCH), "SOMVR_CROUCH") &&
              somvr_set_find("CROUCH") == SOMVR_SK_CROUCH && SOMVR_SK_CROUCH == SOMVR_SK_N - 3,
          "the default button, the old pad's place, the key and its switch");
    CHECK(somvr_set_put(&s, SOMVR_SK_CROUCH, " duck ") && s.crouch == 1 && somvr_set_put(&s, SOMVR_SK_CROUCH, "button") &&
              s.crouch == 0 && !somvr_set_put(&s, SOMVR_SK_CROUCH, "1") && !somvr_set_put(&s, SOMVR_SK_CROUCH, "on"),
          "put: button|duck only");
    s.crouch = 1;
    somvr_set_get(&s, SOMVR_SK_CROUCH, v, sizeof v);
    somvr_set_format(&s, 1u << SOMVR_SK_CROUCH, file, sizeof file);
    somvr_set_default(&b);
    int bad = 0;
    char note[64];
    uint32_t m = somvr_set_parse(&b, file, &bad, note, sizeof note);
    CHECK(!strcmp(v, "duck") && strstr(file, "CROUCH=duck\n") && m == 1u << SOMVR_SK_CROUCH && !bad && b.crouch == 1 &&
              somvr_set_diff(&s, &b) == 0,
          "the file round trip: '%s'", v);
    somvr_set_default(&b);
    CHECK(somvr_set_diff(&s, &b) == 1u << SOMVR_SK_CROUCH, "the diff sees it");
    const uint32_t A = SOMVR_VMA_CROUCH;
    somvr_set_default(&s);
    CHECK(SOMVR_VMI_CROUCH == SOMVR_VMI_ROWS - 3 && !strcmp(somvr_vm_label(SOMVR_VMI_CROUCH), "Crouch") &&
              !somvr_vm_shown(0, SOMVR_VMI_CROUCH) && somvr_vm_shown(A, SOMVR_VMI_CROUCH) &&
              !somvr_vm_enabled(&s, SOMVR_VMA_SIZE, SOMVR_VMI_CROUCH) && somvr_vm_enabled(&s, A, SOMVR_VMI_CROUCH),
          "the row after the size, shown and enabled with SOMVR_VMA_CROUCH only");
    somvr_vm_value(&s, SOMVR_VMI_CROUCH, v, sizeof v);
    CHECK(!strcmp(v, "Button") && somvr_vm_apply(&s, A, 0, SOMVR_VMI_CROUCH, SOMVR_VMP_ROW) == SOMVR_VMR_CHANGED &&
              s.crouch == 1,
          "'Button' shown, the row: Duck");
    somvr_vm_value(&s, SOMVR_VMI_CROUCH, v, sizeof v);
    CHECK(!strcmp(v, "Duck") && somvr_vm_apply(&s, A, 0, SOMVR_VMI_CROUCH, SOMVR_VMP_MORE) == SOMVR_VMR_CHANGED &&
              s.crouch == 0,
          "'Duck' shown, > back to Button");
    s.seated = 1;
    CHECK(!somvr_vm_enabled(&s, A, SOMVR_VMI_CROUCH), "seated play: dimmed (the button's)");
    /* the layout with every row this build shows (no moves): the crouch row under the size, above the buttons */
    const uint32_t AV = SOMVR_VMA_COMFORT | SOMVR_VMA_HUD | SOMVR_VMA_VIEW | SOMVR_VMA_SWORD | SOMVR_VMA_MUSIC |
                        SOMVR_VMA_SIZE | SOMVR_VMA_CROUCH;
    int rz[4], rc[4], rr[4], mb[4];
    CHECK(somvr_vm_rect_av(AV, SOMVR_VMI_SIZE, SOMVR_VMP_ROW, rz) && somvr_vm_rect_av(AV, SOMVR_VMI_CROUCH, SOMVR_VMP_ROW, rc) &&
              somvr_vm_rect_av(AV, SOMVR_VMI_RECENTER, SOMVR_VMP_BUTTON, rr) &&
              somvr_vm_rect_av(AV, SOMVR_VMI_CROUCH, SOMVR_VMP_MORE, mb) && rz[1] + rz[3] <= rc[1] &&
              rc[1] + rc[3] <= rr[1] && rc[3] >= 36,
          "the layout: size %d..%d, crouch %d..%d, the buttons from %d", rz[1], rz[1] + rz[3], rc[1], rc[1] + rc[3], rr[1]);
    somvr_vm_hit h = somvr_vm_hit_test_av(AV, SOMVR_VMPG_SETTINGS, (mb[0] + 0.5f * mb[2]) / SOMVR_VM_TEX_W,
                                          (mb[1] + 0.5f * mb[3]) / SOMVR_VM_TEX_H);
    CHECK(h.item == SOMVR_VMI_CROUCH && h.part == SOMVR_VMP_MORE, "its > box hits back (%d %d)", h.item, h.part);
}

/* ---- v3: the practice page (re-xr-pipeline.md 27): the tab, its buttons and the orc type, the request word -------- */
static void test_practice(void) {
    CHECK(SOMVR_VRMENU_VERSION == 5 && sizeof(somvr_vrmenu_shared) == 184 &&
              offsetof(somvr_vrmenu_shared, practice) == 176 && offsetof(somvr_vrmenu_shared, pstate) == 180 &&
              offsetof(somvr_settings, orc) == 60,
          "the v3 ABI's fields (v4, v5: 8 bytes later each)");
    uint32_t w = somvr_vm_pr_pack(5, SOMVR_VMPR_SPAWN, 2);
    CHECK(somvr_vm_pr_seq(w) == 5 && somvr_vm_pr_action(w) == SOMVR_VMPR_SPAWN && somvr_vm_pr_orc(w) == 2 &&
              somvr_vm_pr_seq(somvr_vm_pr_pack(0xffffff, SOMVR_VMPR_GO, 0)) == 0xffffff,
          "the request word %#x", w);
    somvr_settings s;
    somvr_set_default(&s);
    CHECK(s.orc == 0, "the default orc: infantry");
    const uint32_t P = SOMVR_VMA_PRACTICE, OUT = P | SOMVR_VMA_PR_BACK, HERE = P | SOMVR_VMA_PR_HERE | SOMVR_VMA_PR_BACK;
    /* enabled: the tab only with practice; go out of the area, spawn in it, back with a place, remove with an orc */
    CHECK(!somvr_vm_enabled(&s, 0, SOMVR_VMI_TAB) && somvr_vm_enabled(&s, P, SOMVR_VMI_TAB) &&
              somvr_vm_enabled(&s, P, SOMVR_VMI_PR_GO) && !somvr_vm_enabled(&s, P, SOMVR_VMI_PR_BACK) &&
              !somvr_vm_enabled(&s, P, SOMVR_VMI_PR_SPAWN) && !somvr_vm_enabled(&s, P, SOMVR_VMI_PR_REMOVE) &&
              somvr_vm_enabled(&s, P, SOMVR_VMI_PR_ORC) && !somvr_vm_enabled(&s, SOMVR_VMA_PR_LIVE, SOMVR_VMI_PR_ORC),
          "enabled: out, nothing kept");
    CHECK(!somvr_vm_enabled(&s, HERE, SOMVR_VMI_PR_GO) && somvr_vm_enabled(&s, HERE, SOMVR_VMI_PR_BACK) &&
              somvr_vm_enabled(&s, HERE, SOMVR_VMI_PR_SPAWN) && !somvr_vm_enabled(&s, HERE, SOMVR_VMI_PR_REMOVE) &&
              somvr_vm_enabled(&s, HERE | SOMVR_VMA_PR_ORC, SOMVR_VMI_PR_REMOVE) && somvr_vm_enabled(&s, OUT, SOMVR_VMI_PR_GO),
          "enabled: in the area");
    for (int i = SOMVR_VMI_PR_GO; i <= SOMVR_VMI_PR_REMOVE; i++)
        if (i != SOMVR_VMI_PR_ORC)
            CHECK(!somvr_vm_enabled(&s, HERE | SOMVR_VMA_PR_ORC | SOMVR_VMA_PR_BUSY, i), "busy: %s off", somvr_vm_label(i));
    /* clicks */
    CHECK(somvr_vm_apply(&s, P, 0, SOMVR_VMI_TAB, SOMVR_VMP_BUTTON) == SOMVR_VMR_PAGE &&
              somvr_vm_apply(&s, 0, 0, SOMVR_VMI_TAB, SOMVR_VMP_BUTTON) == SOMVR_VMR_NONE &&
              somvr_vm_apply(&s, P, 0, SOMVR_VMI_TAB, SOMVR_VMP_ROW) == SOMVR_VMR_NONE,
          "the tab");
    CHECK(somvr_vm_apply(&s, P, 0, SOMVR_VMI_PR_GO, SOMVR_VMP_BUTTON) == SOMVR_VMR_PRACTICE &&
              somvr_vm_apply(&s, HERE, 0, SOMVR_VMI_PR_GO, SOMVR_VMP_BUTTON) == SOMVR_VMR_NONE &&
              somvr_vm_apply(&s, HERE, 0, SOMVR_VMI_PR_SPAWN, SOMVR_VMP_BUTTON) == SOMVR_VMR_PRACTICE &&
              somvr_vm_apply(&s, HERE | SOMVR_VMA_PR_ORC, 0, SOMVR_VMI_PR_REMOVE, SOMVR_VMP_BUTTON) == SOMVR_VMR_PRACTICE &&
              somvr_vm_apply(&s, HERE, 0, SOMVR_VMI_PR_BACK, SOMVR_VMP_BUTTON) == SOMVR_VMR_PRACTICE,
          "the buttons");
    CHECK(somvr_vm_practice_action(SOMVR_VMI_PR_GO) == SOMVR_VMPR_GO &&
              somvr_vm_practice_action(SOMVR_VMI_PR_BACK) == SOMVR_VMPR_BACK &&
              somvr_vm_practice_action(SOMVR_VMI_PR_SPAWN) == SOMVR_VMPR_SPAWN &&
              somvr_vm_practice_action(SOMVR_VMI_PR_REMOVE) == SOMVR_VMPR_REMOVE &&
              !somvr_vm_practice_action(SOMVR_VMI_PR_ORC) && !somvr_vm_practice_action(SOMVR_VMI_TURN),
          "the requests");
    char v[32];
    CHECK(somvr_vm_apply(&s, P, 0, SOMVR_VMI_PR_ORC, SOMVR_VMP_MORE) == SOMVR_VMR_CHANGED && s.orc == 1 &&
              (somvr_vm_value(&s, SOMVR_VMI_PR_ORC, v, sizeof v), !strcmp(v, "Shield")),
          "orc > shield: %s", v);
    CHECK(somvr_vm_apply(&s, P, 0, SOMVR_VMI_PR_ORC, SOMVR_VMP_MORE) == 1 && s.orc == 2 &&
              somvr_vm_apply(&s, P, 0, SOMVR_VMI_PR_ORC, SOMVR_VMP_MORE) == 0 && s.orc == 2 &&
              somvr_vm_apply(&s, P, 0, SOMVR_VMI_PR_ORC, SOMVR_VMP_ROW) == 1 && s.orc == 0 &&
              somvr_vm_apply(&s, 0, 0, SOMVR_VMI_PR_ORC, SOMVR_VMP_MORE) == 0,
          "orc: berserker, held, wraps, needs practice");
    s.orc = 9;
    somvr_vm_value(&s, SOMVR_VMI_PR_ORC, v, sizeof v);
    CHECK(!strcmp(v, "Infantry"), "a bad orc shows Infantry: %s", v);
    CHECK(somvr_set_diff(&(somvr_settings){0}, &(somvr_settings){.orc = 2}) == 0, "the orc is no file key");
    /* the layout: every practice item's rect inside, its centre hits it back on the practice page, never on the
     * settings page; the settings page's rows are not on the practice page */
    int rc[4], inside = 1, back = 1, other = 1;
    for (int item = SOMVR_VMI_TAB; item <= SOMVR_VMI_PR_REMOVE; item++) /* (the practice page's; Report, Quit: below) */
        for (int part = SOMVR_VMP_ROW; part <= SOMVR_VMP_BUTTON; part++) {
            if (!somvr_vm_rect(item, part, rc)) continue;
            if (rc[0] < 0 || rc[1] < 0 || rc[0] + rc[2] > SOMVR_VM_TEX_W || rc[1] + rc[3] > SOMVR_VM_TEX_H) inside = 0;
            float px = part == SOMVR_VMP_ROW ? (float)rc[0] + 20 : (float)rc[0] + 0.5f * (float)rc[2];
            float u = px / SOMVR_VM_TEX_W, vv = ((float)rc[1] + 0.5f * (float)rc[3]) / SOMVR_VM_TEX_H;
            somvr_vm_hit h = somvr_vm_hit_test_page(SOMVR_VMPG_PRACTICE, u, vv), h0 = somvr_vm_hit_test(u, vv);
            if (h.item != item || h.part != part) back = 0, printf("    item %d part %d -> %d %d\n", item, part, h.item, h.part);
            if (somvr_vm_tab_page(item) < 0 && h0.item == item) other = 0; /* (the tabs are on every page) */
        }
    somvr_vm_rect(SOMVR_VMI_TAB, SOMVR_VMP_BUTTON, rc);
    somvr_vm_hit ht = somvr_vm_hit_test((rc[0] + 5.0f) / SOMVR_VM_TEX_W, (rc[1] + 5.0f) / SOMVR_VM_TEX_H);
    CHECK(inside && back && other && ht.item == SOMVR_VMI_TAB, "practice rects inside %d, hit back %d, not on the "
                                                               "settings page %d, the tab on it %d", inside, back, other,
          ht.item);
    somvr_vm_rect(SOMVR_VMI_TURN, SOMVR_VMP_ROW, rc);
    CHECK(somvr_vm_hit_test_page(SOMVR_VMPG_PRACTICE, 0.1f, (rc[1] + 20.0f) / SOMVR_VM_TEX_H).item == -1 &&
              somvr_vm_hit_test_page(SOMVR_VMPG_PRACTICE, 6.0f / SOMVR_VM_TEX_W, 20.0f / SOMVR_VM_TEX_H).item == -1,
          "the practice page: its state line and the title are no target");
    int a[4], b[4];
    int ov = 0, ids[] = {SOMVR_VMI_PR_GO, SOMVR_VMI_PR_BACK, SOMVR_VMI_PR_ORC, SOMVR_VMI_PR_SPAWN, SOMVR_VMI_PR_REMOVE};
    for (int i = 0; i + 1 < 5; i++) {
        somvr_vm_rect(ids[i], ids[i] == SOMVR_VMI_PR_ORC ? SOMVR_VMP_ROW : SOMVR_VMP_BUTTON, a);
        somvr_vm_rect(ids[i + 1], ids[i + 1] == SOMVR_VMI_PR_ORC ? SOMVR_VMP_ROW : SOMVR_VMP_BUTTON, b);
        ov |= a[1] + a[3] > b[1];
    }
    int t0[4], t1[4], t2[4];
    somvr_vm_rect(SOMVR_VMI_TAB_SET, SOMVR_VMP_BUTTON, t0), somvr_vm_rect(SOMVR_VMI_TAB, SOMVR_VMP_BUTTON, t1);
    somvr_vm_rect(SOMVR_VMI_TAB_CTL, SOMVR_VMP_BUTTON, t2), somvr_vm_rect(SOMVR_VMI_CLOSE, SOMVR_VMP_BUTTON, b);
    CHECK(!ov && t0[0] >= 8 && t0[0] + t0[2] < t1[0] && t1[0] + t1[2] < t2[0] && t2[0] + t2[2] < b[0] &&
              t1[2] >= somvr_font_text_w("Settings", SOMVR_VM_SCALE) + 8 && t0[1] + t0[3] <= 58,
          "practice items apart; the tabs Settings, Practice, Controls in a row before X, above the line");
    int longest = 0;
    for (int i = SOMVR_VMI_TAB; i < SOMVR_VMI_ALL; i++) {
        int lw = somvr_font_text_w(somvr_vm_label(i), SOMVR_VM_SCALE);
        if (i != SOMVR_VMI_PR_ORC && i != SOMVR_VMI_TAB) longest = lw > longest ? lw : longest;
    }
    CHECK(longest <= SOMVR_VM_TEX_W - 80 - 16 && somvr_font_text_w("Settings", SOMVR_VM_SCALE) <= 140 - 8,
          "the labels fit their buttons (%d px)", longest);
    /* the pictures: the settings page without practice is the v2 picture (no tab); with it the tab is drawn there and
     * nowhere else; the practice page differs, its disabled buttons dimmed */
    somvr_vm_view vw;
    memset(&vw, 0, sizeof vw);
    somvr_set_default(&vw.set);
    vw.avail = SOMVR_VMA_HUD | SOMVR_VMA_COMFORT;
    vw.hover.item = -1;
    somvr_vm_draw(&F, g_px, 0, &vw);
    vw.avail |= P;
    somvr_vm_draw(&F, g_px2, 0, &vw);
    somvr_vm_rect(SOMVR_VMI_TAB, SOMVR_VMP_BUTTON, rc);
    int in_tab = 0, outside = 0;
    for (int y = 0; y < SOMVR_VM_TEX_H; y++)
        for (int x = 0; x < SOMVR_VM_TEX_W; x++)
            if (memcmp(pxa(g_px, x, y), pxa(g_px2, x, y), 4)) {
                int in = x >= rc[0] && x < rc[0] + rc[2] && y >= rc[1] && y < rc[1] + rc[3];
                in_tab += in, outside += !in;
            }
    CHECK(in_tab > 1000 && !outside, "the tab: %d pixels in it, %d elsewhere", in_tab, outside);
    vw.page = SOMVR_VMPG_PRACTICE;
    somvr_vm_draw(&F, g_px, 0, &vw);
    CHECK(memcmp(g_px, g_px2, sizeof g_px) != 0, "the practice page is another picture");
    somvr_vm_rect(SOMVR_VMI_PR_GO, SOMVR_VMP_BUTTON, a), somvr_vm_rect(SOMVR_VMI_PR_SPAWN, SOMVR_VMP_BUTTON, b);
    const uint8_t *go = pxa(g_px, a[0] + 2, a[1] + 2), *sp = pxa(g_px, b[0] + 2, b[1] + 2);
    CHECK(go[0] == 52 && go[1] == 61 && go[2] == 76 && sp[0] == 32 && sp[1] == 37 && sp[2] == 45,
          "Go lit (%u %u %u), Spawn dimmed out of the area (%u %u %u)", go[0], go[1], go[2], sp[0], sp[1], sp[2]);
    vw.avail = HERE | SOMVR_VMA_HUD;
    vw.hover = (somvr_vm_hit){SOMVR_VMI_PR_SPAWN, SOMVR_VMP_BUTTON};
    somvr_vm_draw(&F, g_px2, 0, &vw);
    sp = pxa(g_px2, b[0] + 2, b[1] + 2), go = pxa(g_px2, a[0] + 2, a[1] + 2);
    CHECK(sp[2] == 150 && go[0] == 32, "in the area: Spawn hovered (%u), Go dimmed (%u)", sp[2], go[0]);
}

/* the gate (gin.block: the bow, a climb hold, a swing, an attack incoming): no open while it is on and gate_s after it;
 * a palm held through it needs a fresh gesture; the button still toggles; an open panel is left alone */
static somvr_vm_gin gin_down(double t) { /* the palm turned down (not facing) */
    somvr_vm_gin in = gin_facing(t);
    in.grip = grip_facing(SOMVR_HAND_L, in.grip.p, (float[3]){0, -1, 0}, (float[3]){0, 0, -1});
    return in;
}
/* n frames at 90 Hz, the palm facing or down, the gate's bits: the first event (its frame in *first_ev, -1 = none) */
static int run_frames(somvr_vm_gstate *g, const somvr_vm_gcfg *c, double *t, int n, int facing, uint32_t block,
                      int *first_ev) {
    somvr_posef pan;
    int have = 0, any = 0;
    *first_ev = -1;
    for (int k = 0; k < n; k++, *t += DT) {
        somvr_vm_gin in = facing ? gin_facing(*t) : gin_down(*t);
        in.block = block;
        int e = somvr_vm_gesture(g, c, &in, &pan, &have);
        if (e && *first_ev < 0) *first_ev = k, any = e;
    }
    return any;
}
static void test_gate(void) {
    somvr_vm_gcfg c;
    somvr_vm_gcfg_default(&c);
    CHECK(NEAR(c.gate_s, 0.3, 1e-6), "gate_s default %g", c.gate_s);
    somvr_vm_gstate g;
    somvr_posef pan;
    int have = 0, at = -1;
    double t = 50;
    char buf[96];
    somvr_vm_gate_text(0, buf, sizeof buf);
    CHECK(!strcmp(buf, "nothing"), "gate text 0: %s", buf);
    somvr_vm_gate_text(SOMVR_VMG_BOW | SOMVR_VMG_SWING, buf, sizeof buf);
    CHECK(!strcmp(buf, "the bow+a blade swing"), "gate text: %s", buf);
    somvr_vm_gate_text(SOMVR_VMG_ALL, buf, sizeof buf);
    CHECK(!strcmp(buf, "the bow+a climb hold+a blade swing+an attack incoming"), "gate text all: %s", buf);
    somvr_vm_gate_text(SOMVR_VMG_ALL, buf, 12);
    CHECK(strlen(buf) < 12, "gate text truncated: %s", buf);
    /* 1. each cause alone: the palm toward the eyes 2 s opens nothing; the frames counted, one refused open */
    const uint32_t bits[] = {SOMVR_VMG_BOW, SOMVR_VMG_CLIMB, SOMVR_VMG_SWING, SOMVR_VMG_THREAT};
    for (int i = 0; i < 4; i++) {
        somvr_vm_greset(&g);
        int e = run_frames(&g, &c, &t, 180, 1, bits[i], &at);
        CHECK(e == 0 && !g.open && g.gated && !g.armed && g.gate_frames == 180 && g.held_off == 1 && g.gate_why == bits[i],
              "cause %#x: event %d open %d gated %d frames %llu held_off %llu why %#x", bits[i], e, g.open, g.gated,
              (unsigned long long)g.gate_frames, (unsigned long long)g.held_off, g.gate_why);
    }
    /* unknown bits are no cause */
    somvr_vm_greset(&g);
    int e = run_frames(&g, &c, &t, 60, 1, 0x30u, &at);
    CHECK(e == 1 && (at == 45 || at == 46) && g.gate_frames == 0, "bits 0x30: opened at %d (event %d)", at, e);
    /* 2. held through: the bow put away while the palm still faces -> stays closed (2 s) */
    somvr_vm_greset(&g);
    run_frames(&g, &c, &t, 90, 1, SOMVR_VMG_BOW, &at);
    e = run_frames(&g, &c, &t, 180, 1, 0, &at);
    CHECK(e == 0 && !g.open && !g.gated && g.gate_why == 0 && g.held_off == 1, "held through the gate: opened (event %d at %d)", e,
          at);
    /* 3. ... then a fresh gesture: the palm away 0.3 s, toward the eyes again: open after hold_s */
    run_frames(&g, &c, &t, 27, 0, 0, &at);
    CHECK(g.armed, "re-armed after the palm turned away");
    e = run_frames(&g, &c, &t, 60, 1, 0, &at);
    CHECK(e == 1 && (at == 45 || at == 46) && g.open && g.why == SOMVR_VMW_GESTURE, "the fresh gesture: event %d at %d", e, at);
    /* 4. the palm away through the gate, facing right at its end: the 0.3 s tail still holds it (no open within 2 s);
     * facing only after the tail opens after hold_s */
    somvr_vm_greset(&g);
    run_frames(&g, &c, &t, 90, 0, SOMVR_VMG_CLIMB, &at);
    e = run_frames(&g, &c, &t, 180, 1, 0, &at);
    CHECK(e == 0 && !g.open && g.held_off == 1, "facing inside the tail: opened (event %d at %d)", e, at);
    somvr_vm_greset(&g);
    run_frames(&g, &c, &t, 90, 0, SOMVR_VMG_CLIMB, &at);
    run_frames(&g, &c, &t, 26, 0, 0, &at); /* 0.29 s: still in the tail */
    CHECK(g.gated && !g.armed, "the tail: gated %d armed %d", g.gated, g.armed);
    run_frames(&g, &c, &t, 2, 0, 0, &at); /* past 0.3 s: re-armed at once (the palm was away long enough) */
    CHECK(!g.gated && g.armed && g.gate_why == 0, "after the tail: gated %d armed %d why %#x", g.gated, g.armed, g.gate_why);
    e = run_frames(&g, &c, &t, 60, 1, 0, &at);
    CHECK(e == 1 && (at == 45 || at == 46), "after the tail: the gesture opens at %d (event %d)", at, e);
    /* 5. the gate during a hold (0.3 s into it) for 0.1 s: no open then, none while the palm stays */
    somvr_vm_greset(&g);
    run_frames(&g, &c, &t, 27, 1, 0, &at);
    e = run_frames(&g, &c, &t, 9, 1, SOMVR_VMG_SWING, &at);
    e |= run_frames(&g, &c, &t, 180, 1, 0, &at);
    CHECK(e == 0 && !g.open && g.held_off == 1, "a swing in the hold: opened (event %d)", e);
    /* 6. two causes in one episode: OR'ed; cleared once it ends */
    somvr_vm_greset(&g);
    run_frames(&g, &c, &t, 10, 0, SOMVR_VMG_BOW, &at);
    run_frames(&g, &c, &t, 10, 0, SOMVR_VMG_THREAT, &at);
    CHECK(g.gate_why == (SOMVR_VMG_BOW | SOMVR_VMG_THREAT) && g.held_off == 0, "why %#x held_off %llu", g.gate_why,
          (unsigned long long)g.held_off);
    run_frames(&g, &c, &t, 30, 0, 0, &at);
    CHECK(g.gate_why == 0 && !g.gated, "after: why %#x", g.gate_why);
    /* 7. the menu button opens while gated; the open panel is left alone (no gate frames), its gesture closes it */
    somvr_vm_greset(&g);
    somvr_vm_gin in = gin_down(t);
    in.block = SOMVR_VMG_BOW, in.toggles = 1;
    CHECK(somvr_vm_gesture(&g, &c, &in, &pan, &have) == 1 && g.open && g.why == SOMVR_VMW_BUTTON,
          "the button while gated: open %d gated %d", g.open, g.gated);
    t += DT;
    uint64_t gf = g.gate_frames;
    run_frames(&g, &c, &t, 27, 0, SOMVR_VMG_BOW, &at); /* the palm away: re-armed (open: not gated) */
    CHECK(g.open && g.armed && g.gate_frames == gf, "open under the gate: open %d armed %d frames +%llu", g.open, g.armed,
          (unsigned long long)(g.gate_frames - gf));
    e = run_frames(&g, &c, &t, 60, 1, SOMVR_VMG_BOW, &at);
    CHECK(e == -1 && !g.open && g.why == SOMVR_VMW_GESTURE, "the gesture closes under the gate: event %d", e);
    e = run_frames(&g, &c, &t, 120, 1, SOMVR_VMG_BOW, &at);
    CHECK(e == 0 && !g.open && g.gated, "closed under the gate: not reopened (event %d)", e);
    /* 8. gate_s 0: only the frames with the bits */
    somvr_vm_gcfg c0 = c;
    c0.gate_s = 0;
    somvr_vm_greset(&g);
    run_frames(&g, &c0, &t, 30, 0, SOMVR_VMG_BOW, &at);
    run_frames(&g, &c0, &t, 1, 0, 0, &at);
    CHECK(!g.gated && g.gate_why == 0, "gate_s 0: gated %d after the bits", g.gated);
}

/* the bottom row's Report and Quit game (somvr_session.h, re-xr-pipeline.md 28): rects, hits, availability, the
 * results, the armed Quit drawn red as Confirm */
static void test_session_buttons(void) {
    int rc[4], rr[4], rq[4];
    CHECK(somvr_vm_rect(SOMVR_VMI_RECENTER, SOMVR_VMP_BUTTON, rc) && somvr_vm_rect(SOMVR_VMI_REPORT, SOMVR_VMP_BUTTON, rr) &&
              somvr_vm_rect(SOMVR_VMI_QUIT, SOMVR_VMP_BUTTON, rq) && !somvr_vm_rect(SOMVR_VMI_QUIT, SOMVR_VMP_ROW, rq + 0),
          "three buttons, no rows");
    somvr_vm_rect(SOMVR_VMI_QUIT, SOMVR_VMP_BUTTON, rq);
    CHECK(rc[1] == rr[1] && rr[1] == rq[1] && rc[0] + rc[2] < rr[0] && rr[0] + rr[2] < rq[0] &&
              rq[0] + rq[2] <= SOMVR_VM_TEX_W - 16 && rc[0] >= 16 && rc[2] == rr[2] && rr[2] == rq[2] &&
              rr[0] - (rc[0] + rc[2]) == rq[0] - (rr[0] + rr[2]) && rq[1] + rq[3] <= SOMVR_VM_TEX_H - 28,
          "one row of equal buttons, evenly apart, inside the panel");
    CHECK(somvr_font_text_w("Recentre", SOMVR_VM_SCALE) <= rc[2] - 8 && somvr_font_text_w("Report", SOMVR_VM_SCALE) <= rr[2] - 8 &&
              somvr_font_text_w("Confirm", SOMVR_VM_SCALE) <= rq[2] - 8,
          "the labels fit");
    int items[3] = {SOMVR_VMI_RECENTER, SOMVR_VMI_REPORT, SOMVR_VMI_QUIT}, ok = 1;
    for (int i = 0; i < 3; i++) {
        int r[4];
        somvr_vm_rect(items[i], SOMVR_VMP_BUTTON, r);
        float u = (r[0] + 0.5f * r[2]) / SOMVR_VM_TEX_W, v = (r[1] + 0.5f * r[3]) / SOMVR_VM_TEX_H;
        somvr_vm_hit h = somvr_vm_hit_test(u, v), hp = somvr_vm_hit_test_page(SOMVR_VMPG_PRACTICE, u, v);
        ok &= h.item == items[i] && h.part == SOMVR_VMP_BUTTON && hp.item != items[i];
    }
    CHECK(ok, "each hits back on the settings page only");
    somvr_settings s;
    somvr_set_default(&s);
    CHECK(!somvr_vm_enabled(&s, 0, SOMVR_VMI_REPORT) && !somvr_vm_enabled(&s, SOMVR_VMA_MARK, SOMVR_VMI_QUIT) &&
              somvr_vm_enabled(&s, SOMVR_VMA_MARK, SOMVR_VMI_REPORT) && somvr_vm_enabled(&s, SOMVR_VMA_QUIT, SOMVR_VMI_QUIT),
          "available with the session's bits");
    somvr_settings s0 = s;
    CHECK(somvr_vm_apply(&s, 0, 140, SOMVR_VMI_REPORT, SOMVR_VMP_BUTTON) == SOMVR_VMR_NONE &&
              somvr_vm_apply(&s, SOMVR_VMA_MARK, 140, SOMVR_VMI_REPORT, SOMVR_VMP_BUTTON) == SOMVR_VMR_REPORT &&
              somvr_vm_apply(&s, SOMVR_VMA_QUIT, 140, SOMVR_VMI_QUIT, SOMVR_VMP_BUTTON) == SOMVR_VMR_QUIT &&
              somvr_vm_apply(&s, SOMVR_VMA_QUIT, 140, SOMVR_VMI_QUIT, SOMVR_VMP_ROW) == SOMVR_VMR_NONE &&
              !memcmp(&s, &s0, sizeof s),
          "the results; no setting changes");
    CHECK(!strcmp(somvr_vm_label(SOMVR_VMI_REPORT), "Report") && !strcmp(somvr_vm_label(SOMVR_VMI_QUIT), "Quit game"),
          "labels");
    somvr_vm_view v;
    memset(&v, 0, sizeof v);
    v.set = s, v.avail = SOMVR_VMA_MARK | SOMVR_VMA_QUIT, v.hover.item = -1;
    somvr_vm_draw(&F, g_px, 0, &v);
    v.quit_armed = 1;
    somvr_vm_draw(&F, g_px2, 0, &v);
    const uint8_t *a = pxa(g_px, rq[0] + 3, rq[1] + 3), *b = pxa(g_px2, rq[0] + 3, rq[1] + 3);
    const uint8_t *ra = pxa(g_px, rr[0] + 3, rr[1] + 3), *rb = pxa(g_px2, rr[0] + 3, rr[1] + 3);
    CHECK(b[0] > 150 && b[1] < 80 && a[0] < 100 && !memcmp(ra, rb, 4), "armed: Quit red (%u %u %u), Report as it was",
          b[0], b[1], b[2]);
    v.avail = 0, v.quit_armed = 0;
    somvr_vm_draw(&F, g_px2, 0, &v);
    const uint8_t *d = pxa(g_px2, rr[0] + 3, rr[1] + 3);
    CHECK(d[0] == 32 && d[1] == 37 && d[2] == 45, "without the session: both dimmed (%u %u %u)", d[0], d[1], d[2]);
}

/* build 6 (re-xr-pipeline.md 28): the tab strip and the Controls page (and the arrival card's picture) */
static void test_controls(void) {
    somvr_settings s;
    somvr_set_default(&s);
    const uint32_t P = SOMVR_VMA_PRACTICE;
    CHECK(somvr_vm_tab_page(SOMVR_VMI_TAB_SET) == SOMVR_VMPG_SETTINGS && somvr_vm_tab_page(SOMVR_VMI_TAB) == SOMVR_VMPG_PRACTICE &&
              somvr_vm_tab_page(SOMVR_VMI_TAB_CTL) == SOMVR_VMPG_CONTROLS && somvr_vm_tab_page(SOMVR_VMI_CLOSE) == -1 &&
              somvr_vm_tab_page(SOMVR_VMI_TURN) == -1 && somvr_vm_tab_page(-1) == -1,
          "the tabs' pages");
    CHECK(somvr_vm_apply(&s, 0, 0, SOMVR_VMI_TAB_CTL, SOMVR_VMP_BUTTON) == SOMVR_VMR_PAGE &&
              somvr_vm_apply(&s, 0, 0, SOMVR_VMI_TAB_SET, SOMVR_VMP_BUTTON) == SOMVR_VMR_PAGE &&
              somvr_vm_apply(&s, 0, 0, SOMVR_VMI_TAB, SOMVR_VMP_BUTTON) == SOMVR_VMR_NONE &&
              somvr_vm_apply(&s, P, 0, SOMVR_VMI_TAB, SOMVR_VMP_BUTTON) == SOMVR_VMR_PAGE &&
              somvr_vm_apply(&s, 0, 0, SOMVR_VMI_TAB_CTL, SOMVR_VMP_ROW) == SOMVR_VMR_NONE,
          "the tabs: Settings and Controls always, Practice with SOMVR_PRACTICE");
    CHECK(!strcmp(somvr_vm_label(SOMVR_VMI_TAB_SET), "Settings") && !strcmp(somvr_vm_label(SOMVR_VMI_TAB_CTL), "Controls"),
          "the tab labels");
    /* the hit test: each tab on every page; on the Controls page only the tabs and the close box */
    int tabs[3] = {SOMVR_VMI_TAB_SET, SOMVR_VMI_TAB, SOMVR_VMI_TAB_CTL}, all = 1, rc[4];
    for (int pg = 0; pg < 3; pg++)
        for (int i = 0; i < 3; i++) {
            somvr_vm_rect(tabs[i], SOMVR_VMP_BUTTON, rc);
            somvr_vm_hit h = somvr_vm_hit_test_page(pg, (rc[0] + 0.5f * rc[2]) / SOMVR_VM_TEX_W, (rc[1] + 0.5f * rc[3]) / SOMVR_VM_TEX_H);
            all &= h.item == tabs[i] && h.part == SOMVR_VMP_BUTTON;
        }
    CHECK(all, "every tab hit on every page");
    int only = 1;
    for (int y = 0; y < SOMVR_VM_TEX_H; y += 4)
        for (int x = 0; x < SOMVR_VM_TEX_W; x += 4) {
            somvr_vm_hit h = somvr_vm_hit_test_page(SOMVR_VMPG_CONTROLS, (x + 0.5f) / SOMVR_VM_TEX_W, (y + 0.5f) / SOMVR_VM_TEX_H);
            if (h.item >= 0 && h.item != SOMVR_VMI_CLOSE && somvr_vm_tab_page(h.item) < 0) only = 0;
        }
    CHECK(only, "the Controls page: nothing but the tabs and X to click");
    /* the lines: every key and text fits its column, the walk, the sword, the menu (build 10: the palm or the menu
     * button held, which opens it mid-fight too) and quit are there, nothing for A but the climb's drop (build 10: A
     * lets go of a wall) or the dash (off in this build); build 10 dropped "Orc: Practice > Spawn"; build 11: the bow
     * (the left trigger; the right grip at the bow, pull, let go) */
    const char *k, *w;
    int n = somvr_vm_controls(0, &k, &w), fit = 1, walk = 0, sword = 0, menu = 0, orc = 0, quit = 0, off = 0, rep = 0, mm = 0,
        drop = 0;
    int held = 0, bow = 0, shoot = 0;
    for (int i = 0; i < n; i++) {
        somvr_vm_controls(i, &k, &w);
        fit &= k && w && strlen(k) <= 7 && strlen(w) <= 20 && *w;
        held |= !strcmp(k, "Menu") && strstr(w, "palm") && strstr(w, "hold"); /* build 13: one line */
        walk |= !strcmp(k, "Walk"), sword |= !strcmp(k, "Sword"), menu |= !strcmp(k, "Menu"), orc |= !strcmp(k, "Orc");
        quit |= !strcmp(k, "Quit") && strstr(w, "Settings") != NULL; /* the bottom row of the Settings page */
        mm |= !strcmp(k, "Minimap") && strstr(w, "wrist") != NULL;
        rep |= !strcmp(k, "Report") && strstr(w, "B + Y") != NULL; /* somvr_session.h's chord */
        drop |= !strcmp(k, "Drop") && !strcmp(w, "A (on a wall)");
        bow |= !strcmp(k, "Bow") && !strcmp(w, "hold left trigger");
        shoot |= !strcmp(k, "Shoot") && strstr(w, "R grip") == w && strstr(w, "let go") != NULL;
        off |= (strstr(w, "A ") == w && strcmp(k, "Drop")) || strstr(w, "dash") != NULL;
    }
    somvr_vm_controls(n, &k, &w);
    CHECK(n >= 14 && fit && walk && sword && menu && held && !orc && quit && rep && mm && drop && bow && shoot && !off && !k &&
              !w,
          "the controls: %d lines (held %d, drop %d, bow %d, shoot %d)", n, held, drop, bow, shoot);
    int dy = somvr_vm_ctl_dy();
    CHECK(SOMVR_VM_CTL_Y0 + dy * (n + SOMVR_VM_CTL_NOTES + 1) + 4 <= SOMVR_VM_TEX_H - 12 &&
              dy >= SOMVR_FONT_H * SOMVR_VM_SCALE && dy <= SOMVR_VM_CTL_DY && somvr_vm_controls_note(0) &&
              !somvr_vm_controls_note(SOMVR_VM_CTL_NOTES),
          "the lines, the notes and the card's hint fit the panel (%d px apart)", dy);
    /* the notes: the execution (build 13; no stale "Not yet"; the second empty without the verbs) */
    CHECK(!strstr(somvr_vm_controls_note(0), "Not yet") && !strstr(somvr_vm_controls_note(1), "Not yet") &&
              strstr(somvr_vm_controls_note(0), "Execute") && !*somvr_vm_controls_note(1) &&
              strlen(somvr_vm_controls_note(0)) <= 29 && strlen(somvr_vm_controls_note(1)) <= 29,
          "the notes: '%s' '%s'", somvr_vm_controls_note(0), somvr_vm_controls_note(1));
    /* build 13's switches (the fling, the verbs, the descent; the Report and Quit buttons): 18 lines, the climb's fling,
     * the drop at an edge, the grab and the execution as lines, the held orc's drain and interrogation in the notes;
     * Quit and Sheathe dropped for room (the Settings page's own Quit button; the same gesture), Sprint kept */
    {
        uint32_t a13 = SOMVR_VMA_FLING | SOMVR_VMA_VERBS | SOMVR_VMA_DESCENT | SOMVR_VMA_MARK | SOMVR_VMA_QUIT;
        int n13 = somvr_vm_controls_av(a13, 0, &k, &w), climb = 0, edge = 0, grab = 0, exe = 0, q13 = 0, sh = 0, spr = 0,
            fit13 = 1, old_drop = 0;
        for (int i = 0; i < n13; i++) {
            somvr_vm_controls_av(a13, i, &k, &w);
            fit13 &= k && w && strlen(k) <= 7 && strlen(w) <= 20 && *w;
            climb |= !strcmp(k, "Climb") && strstr(w, "fling") != NULL;
            edge |= !strcmp(k, "Drop") && !strcmp(w, "A (wall, edge)");
            old_drop |= !strcmp(w, "A (on a wall)");
            grab |= !strcmp(k, "Grab") && strstr(w, "L grip") == w;
            exe |= !strcmp(k, "Execute"), q13 |= !strcmp(k, "Quit"), sh |= !strcmp(k, "Sheathe");
            spr |= !strcmp(k, "Sprint");
        }
        int dy13 = somvr_vm_ctl_dy_av(a13);
        const char *n0 = somvr_vm_controls_note_av(a13, 0), *n1 = somvr_vm_controls_note_av(a13, 1);
        CHECK(n13 == SOMVR_VM_CTL_FIT && n13 == 18 && fit13 && climb && edge && !old_drop && grab && exe && !q13 && !sh &&
                  spr && dy13 >= SOMVR_FONT_H * SOMVR_VM_SCALE &&
                  SOMVR_VM_CTL_Y0 + dy13 * (n13 + SOMVR_VM_CTL_NOTES + 1) + 4 <= SOMVR_VM_TEX_H - 12 && strstr(n0, "drain") &&
                  strstr(n1, "interrogate") && strlen(n0) <= 29 && strlen(n1) <= 29,
              "build 13's controls: %d lines at %d px (climb %d edge %d grab %d execute %d quit %d sheathe %d), notes '%s' '%s'",
              n13, dy13, climb, edge, grab, exe, q13, sh, n0, n1);
        /* one switch alone: its own line, the rest as build 12 (the drop at an edge only with the descent) */
        somvr_vm_controls_av(SOMVR_VMA_DESCENT, 7, &k, &w);
        CHECK(!strcmp(k, "Drop") && !strcmp(w, "A (wall, edge)") &&
                  somvr_vm_controls_av(SOMVR_VMA_DESCENT, 0, &k, &w) == n,
              "the descent alone: line 8 '%s: %s', %d lines", k, w, somvr_vm_controls_av(SOMVR_VMA_DESCENT, 0, &k, &w));
    }
    /* the pictures: the Controls page, the card (no tabs: a title; the close hint) */
    somvr_vm_view vw;
    memset(&vw, 0, sizeof vw);
    somvr_set_default(&vw.set);
    vw.avail = P;
    vw.hover.item = -1;
    vw.page = SOMVR_VMPG_CONTROLS;
    somvr_vm_draw(&F, g_px, 0, &vw);
    vw.page = SOMVR_VMPG_SETTINGS;
    somvr_vm_draw(&F, g_px2, 0, &vw);
    CHECK(memcmp(g_px, g_px2, sizeof g_px) != 0, "the Controls page is another picture");
    /* the lit tab: its underline in the title colour; the Settings tab a button there */
    somvr_vm_rect(SOMVR_VMI_TAB_CTL, SOMVR_VMP_BUTTON, rc);
    const uint8_t *ul = pxa(g_px, rc[0] + 20, rc[1] + rc[3] - 3);
    int t0[4];
    somvr_vm_rect(SOMVR_VMI_TAB_SET, SOMVR_VMP_BUTTON, t0);
    const uint8_t *bt = pxa(g_px, t0[0] + 2, t0[1] + 2);
    CHECK(ul[0] == 232 && ul[1] == 192 && ul[2] == 112 && bt[0] == 52 && bt[1] == 61 && bt[2] == 76,
          "Controls lit (%u %u %u), Settings a button (%u %u %u)", ul[0], ul[1], ul[2], bt[0], bt[1], bt[2]);
    vw.page = SOMVR_VMPG_CONTROLS;
    vw.card = 1;
    somvr_vm_draw(&F, g_px2, 0, &vw);
    bt = pxa(g_px2, t0[0] + 2, t0[1] + 2);
    int diff_low = 0;
    for (int y = SOMVR_VM_CTL_Y0 + dy * (n + SOMVR_VM_CTL_NOTES) - 2; y < SOMVR_VM_TEX_H; y++) /* the hint's row */
        for (int x = 0; x < SOMVR_VM_TEX_W; x++) diff_low += memcmp(pxa(g_px, x, y), pxa(g_px2, x, y), 4) != 0;
    CHECK(bt[0] == 22 && bt[1] == 26 && bt[2] == 33 && diff_low > 200, "the card: no tab buttons (%u %u %u), the close "
          "hint drawn (%d pixels)", bt[0], bt[1], bt[2], diff_low);
}

/* ---- v5 (build 10, re-xr-pipeline.md 34): the Music row, the rows a build does not show -------------------------- */
static void test_music_rows(void) {
    somvr_settings s, b;
    somvr_set_default(&s);
    char v[32], file[2048];
    CHECK(s.music == 1.0f && offsetof(somvr_settings, music) == 72 && !strcmp(somvr_set_key(SOMVR_SK_MUSIC), "MUSIC") &&
              !strcmp(somvr_set_env(SOMVR_SK_MUSIC), "SOMVR_MUSIC") && somvr_set_find("SOMVR_MUSIC") == SOMVR_SK_MUSIC,
          "MUSIC: the key, its switch, the default (the game's own volume)");
    CHECK(somvr_set_put(&s, SOMVR_SK_MUSIC, "0.3") && NEAR(s.music, 0.3, 1e-6) && somvr_set_put(&s, SOMVR_SK_MUSIC, "4") &&
              s.music == 1 && somvr_set_put(&s, SOMVR_SK_MUSIC, "-1") && s.music == 0 && !somvr_set_put(&s, SOMVR_SK_MUSIC, "loud"),
          "MUSIC: a share clamped to 0..1");
    s.music = 0.3f;
    somvr_set_format(&s, 1u << SOMVR_SK_MUSIC, file, sizeof file);
    somvr_set_default(&b);
    int bad = 0;
    CHECK(strstr(file, "MUSIC=0.3\n") && somvr_set_parse(&b, file, &bad, NULL, 0) == 1u << SOMVR_SK_MUSIC && !bad &&
              !somvr_set_diff(&s, &b),
          "the file's round trip: %s", file);
    /* the row: only with SOMVR_VMA_MUSIC; Off, 10 .. 100 %; the row wraps */
    const uint32_t M = SOMVR_VMA_MUSIC;
    CHECK(!somvr_vm_enabled(&s, 0, SOMVR_VMI_MUSIC) && somvr_vm_enabled(&s, M, SOMVR_VMI_MUSIC) &&
              !strcmp(somvr_vm_label(SOMVR_VMI_MUSIC), "Music") &&
              somvr_vm_apply(&s, 0, 0, SOMVR_VMI_MUSIC, SOMVR_VMP_MORE) == SOMVR_VMR_NONE && NEAR(s.music, 0.3, 1e-6),
          "the Music row needs SOMVR_VMA_MUSIC");
    somvr_vm_value(&s, SOMVR_VMI_MUSIC, v, sizeof v);
    CHECK(!strcmp(v, "30%"), "30 %%: %s", v);
    CHECK(somvr_vm_apply(&s, M, 0, SOMVR_VMI_MUSIC, SOMVR_VMP_LESS) == SOMVR_VMR_CHANGED && NEAR(s.music, 0.2, 1e-6) &&
              somvr_vm_apply(&s, M, 0, SOMVR_VMI_MUSIC, SOMVR_VMP_LESS) == SOMVR_VMR_CHANGED &&
              somvr_vm_apply(&s, M, 0, SOMVR_VMI_MUSIC, SOMVR_VMP_LESS) == SOMVR_VMR_CHANGED && s.music == 0 &&
              somvr_vm_apply(&s, M, 0, SOMVR_VMI_MUSIC, SOMVR_VMP_LESS) == SOMVR_VMR_NONE,
          "down to Off, held there: %g", s.music);
    somvr_vm_value(&s, SOMVR_VMI_MUSIC, v, sizeof v);
    CHECK(!strcmp(v, "Off"), "0 %%: %s", v);
    CHECK(somvr_vm_apply(&s, M, 0, SOMVR_VMI_MUSIC, SOMVR_VMP_ROW) == SOMVR_VMR_CHANGED && NEAR(s.music, 0.1, 1e-6), "the row: 10 %%");
    s.music = 1;
    somvr_vm_value(&s, SOMVR_VMI_MUSIC, v, sizeof v);
    CHECK(!strcmp(v, "100%") && somvr_vm_apply(&s, M, 0, SOMVR_VMI_MUSIC, SOMVR_VMP_MORE) == SOMVR_VMR_NONE &&
              somvr_vm_apply(&s, M, 0, SOMVR_VMI_MUSIC, SOMVR_VMP_ROW) == SOMVR_VMR_CHANGED && s.music == 0,
          "100 %%, held; the row wraps to Off");
    /* the rows a build does not show: the step, the dash and the phase without MOVES (SOMVR_GAMEMOVES=off), Music
     * without MUSIC; a row that does not apply now (the snap angle with smooth turns) is still shown */
    CHECK(!somvr_vm_shown(0, SOMVR_VMI_STEP) && !somvr_vm_shown(0, SOMVR_VMI_DASH) && !somvr_vm_shown(0, SOMVR_VMI_PHASE) &&
              !somvr_vm_shown(0, SOMVR_VMI_MUSIC) && somvr_vm_shown(SOMVR_VMA_MOVES, SOMVR_VMI_PHASE) &&
              somvr_vm_shown(M, SOMVR_VMI_MUSIC) && somvr_vm_shown(0, SOMVR_VMI_SNAP) && somvr_vm_shown(0, SOMVR_VMI_SWORD) &&
              somvr_vm_shown(0, SOMVR_VMI_RECENTER) && !somvr_vm_shown(0, -1) && !somvr_vm_shown(0, SOMVR_VMI_ALL),
          "shown");
    int rc[4], rh[4], rs[4], rm[4], rr[4];
    CHECK(!somvr_vm_rect(SOMVR_VMI_PHASE, SOMVR_VMP_ROW, rc) && !somvr_vm_rect_av(SOMVR_VMA_MOVES, SOMVR_VMI_MUSIC, SOMVR_VMP_MORE, rc),
          "a row not shown has no rect");
    /* build 10's menu (no MOVES, with MUSIC): 14 rows of 38 px, the sword right below the haptics, Music last */
    const uint32_t B10 = SOMVR_VMA_SWORD | M;
    CHECK(somvr_vm_rect_av(B10, SOMVR_VMI_HAPTICS, SOMVR_VMP_ROW, rh) && somvr_vm_rect_av(B10, SOMVR_VMI_SWORD, SOMVR_VMP_ROW, rs) &&
              somvr_vm_rect_av(B10, SOMVR_VMI_MUSIC, SOMVR_VMP_ROW, rm) &&
              somvr_vm_rect_av(B10, SOMVR_VMI_RECENTER, SOMVR_VMP_BUTTON, rr) && rh[3] == 38 && rs[1] == rh[1] + 38 &&
              rm[1] == rs[1] + 38 && rm[1] + rm[3] <= rr[1] && rm[1] == 64 + 13 * 38,
          "build 10's rows: haptics %d, sword %d, music %d..%d, buttons %d", rh[1], rs[1], rm[1], rm[1] + rm[3], rr[1]);
    /* the bottom row stays put whatever is shown */
    int r0[4];
    somvr_vm_rect_av(SOMVR_VMA_MOVES | B10, SOMVR_VMI_RECENTER, SOMVR_VMP_BUTTON, r0);
    CHECK(r0[1] == rr[1] && rr[1] == 64 + 16 * 38 + 12, "the bottom row at %d / %d", r0[1], rr[1]);
    /* the hit test follows the shown rows: the music row's > box with B10 is the music, with 0 nothing (no row there:
     * 13 rows end above it) */
    somvr_vm_rect_av(B10, SOMVR_VMI_MUSIC, SOMVR_VMP_MORE, rc);
    float u = (rc[0] + 0.5f * rc[2]) / SOMVR_VM_TEX_W, vv = (rc[1] + 0.5f * rc[3]) / SOMVR_VM_TEX_H;
    somvr_vm_hit h1 = somvr_vm_hit_test_av(B10, SOMVR_VMPG_SETTINGS, u, vv), h0 = somvr_vm_hit_test_av(0, SOMVR_VMPG_SETTINGS, u, vv);
    CHECK(h1.item == SOMVR_VMI_MUSIC && h1.part == SOMVR_VMP_MORE && h0.item == -1, "hits: %d/%d, without %d", h1.item,
          h1.part, h0.item);
    /* the pictures: build 10's draws no phase row, the MOVES one does; Music's row drawn only with MUSIC */
    somvr_vm_view vw;
    memset(&vw, 0, sizeof vw);
    somvr_set_default(&vw.set);
    vw.hover.item = -1;
    vw.avail = B10;
    somvr_vm_draw(&F, g_px, 0, &vw);
    vw.avail = B10 | SOMVR_VMA_MOVES;
    somvr_vm_draw(&F, g_px2, 0, &vw);
    CHECK(memcmp(g_px, g_px2, sizeof g_px) != 0, "the MOVES rows change the picture");
    /* below the last shown row (build 10: 14 rows end at 596) and above the buttons the panel is plain background */
    int plain = 1;
    for (int y = 64 + 14 * 38 + 2; y < rr[1] - 2; y++)
        for (int x = 20; x < SOMVR_VM_TEX_W - 20; x++) {
            const uint8_t *p = pxa(g_px, x, y);
            plain &= p[0] == 22 && p[1] == 26 && p[2] == 33;
        }
    CHECK(plain, "build 10: nothing drawn between the last row and the buttons");
}

int main(void) {
    test_font();
    test_settings();
    test_model();
    test_moves();
    test_layout();
    test_draw();
    test_pose();
    test_gesture();
    test_gate();
    test_practice();
    test_sword();
    test_size();
    test_feel();
    test_crouch();
    test_session_buttons();
    test_controls();
    test_music_rows();
    printf("test_vrmenu: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
