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
    uint64_t mask = somvr_set_parse(&p, text, &nbad, note, sizeof note);
    CHECK(mask == ((1u << SOMVR_SK_TURN) | (1u << SOMVR_SK_SNAP_DEG) | (1u << SOMVR_SK_VIGNETTE) | (1u << SOMVR_SK_HUD) |
                   (1u << SOMVR_SK_HAPTICS) | (1u << SOMVR_SK_HAND)) &&
              nbad == 3 && !strcmp(note, "line 7: unknown key"),
          "parse: mask %#llx bad %d note '%s'", (unsigned long long)mask, nbad, note);
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
          "round trip: %zu bytes mask %#llx bad %d diff %#llx", n, (unsigned long long)mask, nbad,
          (unsigned long long)somvr_set_diff(&a, &b));
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
    CHECK(somvr_set_diff(&a, &b) == ((1u << SOMVR_SK_SNAP_DEG) | (1u << SOMVR_SK_HUD)), "diff %#llx",
          (unsigned long long)somvr_set_diff(&a, &b));
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
/* build 19: the page an item is on (the Fight page's rows and its back button; the rest on the Settings page) */
static int page_of(int item) {
    if (item == SOMVR_VMI_PG_BACK) return SOMVR_VMPG_FIGHT;
    int p = somvr_vm_row_page(item);
    return p < 0 ? SOMVR_VMPG_SETTINGS : p;
}
static void test_layout(void) {
    int rc[4], inside = 1, back = 1, contained = 1;
    for (int item = 0; item < SOMVR_VMI_ALL; item++)
        for (int part = SOMVR_VMP_ROW; part <= SOMVR_VMP_BUTTON; part++) {
            if (item >= SOMVR_VMI_N && item != SOMVR_VMI_PG_FIGHT && item != SOMVR_VMI_PG_BACK) continue;
            if (!somvr_vm_rect(item, part, rc)) continue;
            if (rc[0] < 0 || rc[1] < 0 || rc[2] <= 0 || rc[3] <= 0 || rc[0] + rc[2] > SOMVR_VM_TEX_W ||
                rc[1] + rc[3] > SOMVR_VM_TEX_H)
                inside = 0;
            /* the centre; a row's at its label (its centre is the < box's left edge) */
            float px = part == SOMVR_VMP_ROW ? (float)rc[0] + 20 : (float)rc[0] + 0.5f * (float)rc[2];
            somvr_vm_hit h = somvr_vm_hit_test_av(0, page_of(item), px / SOMVR_VM_TEX_W,
                                                  ((float)rc[1] + 0.5f * (float)rc[3]) / SOMVR_VM_TEX_H);
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
    /* build 19 (re-xr-pipeline.md 73): the rows on two pages - the Settings page (the view, the comfort, the body) and
     * the Fight page (the sword, the bow, the fight: somvr_vm_row_page), each with its page button in the slot under its
     * rows (Fight settings > / < Settings), the bottom row under both. Every row of every build: apart, the button under
     * them, the bottom row below; a page's rows as close as its count makes them (row_h) */
    const uint64_t ALLR = SOMVR_VMA_MOVES | SOMVR_VMA_MUSIC | SOMVR_VMA_SIZE | SOMVR_VMA_CROUCH | SOMVR_VMA_FLING |
                          SOMVR_VMA_SURFACE | SOMVR_VMA_FIGHT_ROWS;
    int overlap = 0, below = 1, minh = 999, n_on[2] = {0, 0};
    for (int pg = 0; pg < 2; pg++) {
        int want = pg ? SOMVR_VMPG_FIGHT : SOMVR_VMPG_SETTINGS, prev[4] = {0, 0, 0, 0}, have = 0;
        for (int i = 0; i < SOMVR_VMI_ROWS; i++) {
            int a[4];
            if (somvr_vm_row_page(i) != want || !somvr_vm_rect_av(ALLR, i, SOMVR_VMP_ROW, a)) continue;
            if (have) overlap |= prev[1] + prev[3] > a[1];
            memcpy(prev, a, sizeof a), have = 1, n_on[pg]++;
            minh = a[3] < minh ? a[3] : minh;
        }
        somvr_vm_rect_av(ALLR, pg ? SOMVR_VMI_PG_BACK : SOMVR_VMI_PG_FIGHT, SOMVR_VMP_BUTTON, r1);
        overlap |= prev[1] + prev[3] > r1[1];
        somvr_vm_rect_av(ALLR, SOMVR_VMI_RECENTER, SOMVR_VMP_BUTTON, rb);
        below &= r1[1] + r1[3] <= rb[1];
    }
    /* all rows (the moves only with SOMVR_VMA_MOVES, which no build since 6 has): the Settings page 20 + its button =
     * 28 px, the Fight page 11 + its button = 38 px */
    CHECK(!overlap && below && rb[3] >= 48 && minh == 28 && n_on[0] == 20 && n_on[1] == 11,
          "rows apart, the page buttons below them, the bottom row below (min %d px, %d + %d rows)", minh, n_on[0], n_on[1]);
    /* build 19's own set (build 18's 21 rows: no moves, no Sword hand - SOMVR_SWORD_HAND_LOCK - + the six): the Settings
     * page 17 rows + its button = 33 px (over the font's 32-pixel cell; build 18's one page: 21 rows of 28), the Fight
     * page 10 + its button = 38 */
    const uint64_t B19 = ALLR & ~(uint64_t)(SOMVR_VMA_MOVES | SOMVR_VMA_SWHAND);
    somvr_vm_rect_av(B19, SOMVR_VMI_TURN, SOMVR_VMP_ROW, r0), somvr_vm_rect_av(B19, SOMVR_VMI_SWORD, SOMVR_VMP_ROW, r1);
    CHECK(r0[3] == 33 && r0[3] >= SOMVR_FONT_H * SOMVR_VM_SCALE && r1[3] == 38 && r0[1] == r1[1] && r0[1] == 64,
          "build 19: the Settings page's rows %d px, the Fight page's %d px, both from %d", r0[3], r1[3], r0[1]);
    somvr_vm_rect_av(B19, SOMVR_VMI_SURFACE, SOMVR_VMP_ROW, r0), somvr_vm_rect_av(B19, SOMVR_VMI_PG_FIGHT, SOMVR_VMP_BUTTON, r1);
    CHECK(r0[1] == 64 + 16 * 33 && r1[1] == 64 + 17 * 33 + 2 && r1[3] == 29 && r1[0] == 16 && r1[2] == 480,
          "build 19: Hands at walls the Settings page's last row (%d), Fight settings > under it (%d, %d px)", r0[1], r1[1],
          r1[3]);
    somvr_vm_rect_av(B19, SOMVR_VMI_CICON, SOMVR_VMP_ROW, r0), somvr_vm_rect_av(B19, SOMVR_VMI_PG_BACK, SOMVR_VMP_BUTTON, r1);
    CHECK(r0[1] == 64 + 9 * 38 && r1[1] == 64 + 10 * 38 + 2 && r1[3] == 34,
          "build 19: Counter icon the Fight page's last row (%d), < Settings under it (%d)", r0[1], r1[1]);
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
    uint64_t m = somvr_set_parse(&b, file, &bad, NULL, 0);
    CHECK(m == ((1u << SOMVR_SK_STEP) | (1u << SOMVR_SK_DASH) | (1u << SOMVR_SK_PHASE)) && !bad && !somvr_set_diff(&s, &b),
          "the round trip: mask %#llx bad %d diff %#llx", (unsigned long long)m, bad,
          (unsigned long long)somvr_set_diff(&s, &b));
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
    CHECK(SOMVR_VRMENU_VERSION == 7 && sizeof(somvr_settings) == 96 && offsetof(somvr_settings, sword_deg) == 64 &&
              sizeof(somvr_vrmenu_shared) == 208 && offsetof(somvr_vrmenu_shared, gen) == 144 &&
              offsetof(somvr_vrmenu_shared, block) == 164 && offsetof(somvr_vrmenu_shared, n_frames) == 168,
          "the v4 fields in the v7 ABI");
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
    /* every row shown (v5 + the player size + build 13's crouch, fling and hands at walls): build 19: the sword row the
     * Fight page's first (at the top, where the Settings page's Turn is), the phase row on the Settings page; the
     * bottom buttons below both and inside the texture */
    const uint32_t ALLR = SOMVR_VMA_MOVES | SOMVR_VMA_SWORD | SOMVR_VMA_MUSIC | SOMVR_VMA_SIZE | SOMVR_VMA_CROUCH |
                          SOMVR_VMA_FLING | SOMVR_VMA_SURFACE;
    int rp[4], rs[4], rr[4], rq[4], lb[4], mb[4];
    memset(rp, 0, sizeof rp), memset(rs, 0, sizeof rs), memset(rr, 0, sizeof rr), memset(rq, 0, sizeof rq);
    memset(lb, 0, sizeof lb), memset(mb, 0, sizeof mb);
    CHECK(SOMVR_VMI_ROWS == 33 && SOMVR_VMI_MOVE == 31 && SOMVR_VMI_REFRESH == 32 && SOMVR_VMI_SWORD == SOMVR_VMI_MOVE - 16 && somvr_vm_rect_av(ALLR, SOMVR_VMI_PHASE, SOMVR_VMP_ROW, rp) &&
              somvr_vm_rect_av(ALLR, SOMVR_VMI_SWORD, SOMVR_VMP_ROW, rs) &&
              somvr_vm_rect_av(ALLR, SOMVR_VMI_RECENTER, SOMVR_VMP_BUTTON, rr) &&
              somvr_vm_rect_av(ALLR, SOMVR_VMI_QUIT, SOMVR_VMP_BUTTON, rq) && somvr_vm_rect_av(ALLR, SOMVR_VMI_SWORD, SOMVR_VMP_LESS, lb) &&
              somvr_vm_rect_av(ALLR, SOMVR_VMI_SWORD, SOMVR_VMP_MORE, mb) && rs[1] == 64 && rp[1] + rp[3] <= rr[1] &&
              rs[1] + rs[3] <= rr[1] &&
              rq[1] + rq[3] <= SOMVR_VM_TEX_H - 28 && lb[3] == (rs[3] - 2 < 34 ? rs[3] - 2 : 34) && mb[3] == lb[3] &&
              lb[1] >= rs[1] &&
              lb[1] + lb[3] <= rs[1] + rs[3],
          "the layout: phase %d..%d, sword %d..%d, the buttons %d..%d of %d", rp[1], rp[1] + rp[3], rs[1], rs[1] + rs[3],
          rr[1], rr[1] + rr[3], SOMVR_VM_TEX_H);
    somvr_vm_hit h = somvr_vm_hit_test_av(ALLR, SOMVR_VMPG_FIGHT, (mb[0] + 0.5f * mb[2]) / SOMVR_VM_TEX_W,
                                          (mb[1] + 0.5f * mb[3]) / SOMVR_VM_TEX_H),
                 hs = somvr_vm_hit_test_av(ALLR, SOMVR_VMPG_SETTINGS, (mb[0] + 0.5f * mb[2]) / SOMVR_VM_TEX_W,
                                           (mb[1] + 0.5f * mb[3]) / SOMVR_VM_TEX_H);
    CHECK(h.item == SOMVR_VMI_SWORD && h.part == SOMVR_VMP_MORE && hs.item == SOMVR_VMI_TURN,
          "its > box hits back on the Fight page (%d %d), the Settings page's Turn there (%d)", h.item, h.part, hs.item);
}

/* ---- the player size (re-xr-pipeline.md 35): the key in %, the row at the end of the settings, 100 .. 140 by 5 ---- */
static void test_size(void) {
    somvr_settings s, b;
    char v[32], file[512];
    somvr_set_default(&s);
    CHECK(s.size == 1.0f && offsetof(somvr_settings, size) == 68 && sizeof(somvr_settings) == 96 &&
              !strcmp(somvr_set_key(SOMVR_SK_PLAYER_SIZE), "PLAYER_SIZE") &&
              !strcmp(somvr_set_env(SOMVR_SK_PLAYER_SIZE), "SOMVR_PLAYER_SIZE") &&
              somvr_set_find("SOMVR_PLAYER_SIZE") == SOMVR_SK_PLAYER_SIZE && SOMVR_SK_PLAYER_SIZE == SOMVR_SK_MOVE - 14,
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
    CHECK(SOMVR_VMI_SIZE == SOMVR_VMI_MOVE - 10 && !strcmp(somvr_vm_label(SOMVR_VMI_SIZE), "Player size") &&
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
              offsetof(somvr_settings, surface) == 78 && sizeof(somvr_settings) == 96 &&
              !strcmp(somvr_set_key(SOMVR_SK_CLIMB_FLING), "CLIMB_FLING") &&
              !strcmp(somvr_set_env(SOMVR_SK_CLIMB_FLING), "SOMVR_CLIMB_FLING") &&
              !strcmp(somvr_set_key(SOMVR_SK_HANDS_SURFACE), "HANDS_SURFACE") &&
              somvr_set_find("SOMVR_HANDS_SURFACE") == SOMVR_SK_HANDS_SURFACE && SOMVR_SK_HANDS_SURFACE == SOMVR_SK_MOVE - 11 &&
              SOMVR_SK_CLIMB_FLING == SOMVR_SK_MOVE - 12,
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
    CHECK(SOMVR_VMI_FLING == SOMVR_VMI_MOVE - 8 && SOMVR_VMI_SURFACE == SOMVR_VMI_MOVE - 7 &&
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

/* ---- build 16: the Sword weight row (re-xr-pipeline.md 52): the key = the switch (off|light|heavy, on = light), the
 * old pad's byte 79, the row right below Sword angle, shown and enabled with SOMVR_VMA_WEIGHT, Off / Light / Heavy -- */
static void test_weight_row(void) {
    somvr_settings s, b;
    char v[32], file[512], note[64];
    int bad = 0;
    somvr_set_default(&s);
    CHECK(s.sword_weight == 0 && offsetof(somvr_settings, sword_weight) == 79 && sizeof(somvr_settings) == 96 &&
              !strcmp(somvr_set_key(SOMVR_SK_SWORD_WEIGHT), "SWORD_WEIGHT") &&
              !strcmp(somvr_set_env(SOMVR_SK_SWORD_WEIGHT), "SOMVR_SWORD_WEIGHT") &&
              somvr_set_find("SOMVR_SWORD_WEIGHT") == SOMVR_SK_SWORD_WEIGHT && SOMVR_SK_SWORD_WEIGHT == SOMVR_SK_MOVE - 10,
          "the default off, the old pad's byte, the key and its switch");
    CHECK(somvr_set_put(&s, SOMVR_SK_SWORD_WEIGHT, " heavy ") && s.sword_weight == 2 &&
              somvr_set_put(&s, SOMVR_SK_SWORD_WEIGHT, "on") && s.sword_weight == 1 &&
              somvr_set_put(&s, SOMVR_SK_SWORD_WEIGHT, "off") && s.sword_weight == 0 &&
              somvr_set_put(&s, SOMVR_SK_SWORD_WEIGHT, "light") && s.sword_weight == 1 &&
              !somvr_set_put(&s, SOMVR_SK_SWORD_WEIGHT, "2") && !somvr_set_put(&s, SOMVR_SK_SWORD_WEIGHT, "trail") &&
              s.sword_weight == 1,
          "put: the switch's words (%u)", s.sword_weight);
    s.sword_weight = 2;
    somvr_set_get(&s, SOMVR_SK_SWORD_WEIGHT, v, sizeof v);
    somvr_set_format(&s, 1u << SOMVR_SK_SWORD_WEIGHT, file, sizeof file);
    somvr_set_default(&b);
    uint32_t m = somvr_set_parse(&b, file, &bad, note, sizeof note);
    CHECK(!strcmp(v, "heavy") && strstr(file, "SWORD_WEIGHT=heavy\n") && m == 1u << SOMVR_SK_SWORD_WEIGHT && !bad &&
              b.sword_weight == 2 && somvr_set_diff(&s, &b) == 0 && b.surface == 0,
          "the file round trip (%s)", v);
    b.sword_weight = 0;
    CHECK(somvr_set_diff(&s, &b) == 1u << SOMVR_SK_SWORD_WEIGHT, "the diff sees it");
    somvr_set_default(&s);
    const uint32_t A = SOMVR_VMA_WEIGHT;
    CHECK(SOMVR_VMI_WEIGHT == SOMVR_VMI_SWORD + 1 && !strcmp(somvr_vm_label(SOMVR_VMI_WEIGHT), "Sword weight") &&
              !somvr_vm_shown(0, SOMVR_VMI_WEIGHT) && !somvr_vm_shown(SOMVR_VMA_SWORD, SOMVR_VMI_WEIGHT) &&
              somvr_vm_shown(A, SOMVR_VMI_WEIGHT) && somvr_vm_enabled(&s, A, SOMVR_VMI_WEIGHT) &&
              !somvr_vm_enabled(&s, SOMVR_VMA_SWORD, SOMVR_VMI_WEIGHT) &&
              somvr_vm_apply(&s, 0, 0, SOMVR_VMI_WEIGHT, SOMVR_VMP_MORE) == SOMVR_VMR_NONE && s.sword_weight == 0,
          "the row below Sword angle, shown and enabled with its bit only");
    const char *want[] = {"Off", "Light", "Heavy"};
    int vals = 1;
    for (int i = 0; i < 3; i++) {
        s.sword_weight = (uint8_t)i;
        somvr_vm_value(&s, SOMVR_VMI_WEIGHT, v, sizeof v);
        vals &= !strcmp(v, want[i]);
    }
    CHECK(vals, "the values Off / Light / Heavy");
    s.sword_weight = 0;
    CHECK(somvr_vm_apply(&s, A, 0, SOMVR_VMI_WEIGHT, SOMVR_VMP_MORE) == SOMVR_VMR_CHANGED && s.sword_weight == 1 &&
              somvr_vm_apply(&s, A, 0, SOMVR_VMI_WEIGHT, SOMVR_VMP_MORE) == SOMVR_VMR_CHANGED && s.sword_weight == 2 &&
              somvr_vm_apply(&s, A, 0, SOMVR_VMI_WEIGHT, SOMVR_VMP_MORE) == SOMVR_VMR_NONE && s.sword_weight == 2 &&
              somvr_vm_apply(&s, A, 0, SOMVR_VMI_WEIGHT, SOMVR_VMP_ROW) == SOMVR_VMR_CHANGED && s.sword_weight == 0 &&
              somvr_vm_apply(&s, A, 0, SOMVR_VMI_WEIGHT, SOMVR_VMP_LESS) == SOMVR_VMR_NONE && s.sword_weight == 0,
          "> steps up, held at Heavy; a press wraps to Off; < held at Off (%u)", s.sword_weight);
    /* build 16's set: the weight row right below the angle (build 19: both on the Fight page, Music on the Settings
     * page), the < > boxes in it, its > box hits back there */
    const uint32_t B16 = SOMVR_VMA_MUSIC | SOMVR_VMA_SIZE | SOMVR_VMA_SWORD | SOMVR_VMA_CROUCH | SOMVR_VMA_FLING |
                         SOMVR_VMA_SURFACE | SOMVR_VMA_COMFORT | SOMVR_VMA_HUD | SOMVR_VMA_VIEW | SOMVR_VMA_WEIGHT;
    int ra[4], rw[4], rm[4], mb[4];
    memset(ra, 0, sizeof ra), memset(rw, 0, sizeof rw), memset(rm, 0, sizeof rm), memset(mb, 0, sizeof mb);
    CHECK(somvr_vm_rect_av(B16, SOMVR_VMI_SWORD, SOMVR_VMP_ROW, ra) && somvr_vm_rect_av(B16, SOMVR_VMI_WEIGHT, SOMVR_VMP_ROW, rw) &&
              somvr_vm_rect_av(B16, SOMVR_VMI_MUSIC, SOMVR_VMP_ROW, rm) &&
              somvr_vm_rect_av(B16, SOMVR_VMI_WEIGHT, SOMVR_VMP_MORE, mb) && ra[1] + ra[3] == rw[1] && ra[1] == 64 &&
              rw[3] >= SOMVR_FONT_H * SOMVR_VM_SCALE && mb[1] >= rw[1] && mb[1] + mb[3] <= rw[1] + rw[3] &&
              rm[1] == 64 + 12 * rm[3],
          "build 16's page: angle %d, weight %d (%d px), music %d", ra[1], rw[1], rw[3], rm[1]);
    somvr_vm_hit h = somvr_vm_hit_test_av(B16, SOMVR_VMPG_FIGHT, (mb[0] + 0.5f * mb[2]) / SOMVR_VM_TEX_W,
                                          (mb[1] + 0.5f * mb[3]) / SOMVR_VM_TEX_H);
    CHECK(h.item == SOMVR_VMI_WEIGHT && h.part == SOMVR_VMP_MORE, "its > box hits back (%d %d)", h.item, h.part);
    /* without the bit the rows close up: the Fight page's back button right below the angle */
    somvr_vm_rect_av(B16 & ~(uint32_t)SOMVR_VMA_WEIGHT, SOMVR_VMI_PG_BACK, SOMVR_VMP_BUTTON, rm);
    somvr_vm_rect_av(B16 & ~(uint32_t)SOMVR_VMA_WEIGHT, SOMVR_VMI_SWORD, SOMVR_VMP_ROW, ra);
    CHECK(rm[1] - (ra[1] + ra[3]) < ra[3], "without the bit: no gap (angle %d+%d, the back button %d)", ra[1], ra[3],
          rm[1]);
}

/* ---- build 17, the bite17 helper: the Blade bite row (re-xr-pipeline.md 59): the key, its byte (v6's spare 82), on|off,
 * right below Sword hand; bit 1 << 30 ---------------------------------------------------------------------------- */
static void test_bite_row(void) {
    somvr_settings s, b;
    char v[32], file[512], note[64];
    int bad = 0;
    somvr_set_default(&s);
    CHECK(s.bite == 0 && offsetof(somvr_settings, bite) == 82 && sizeof(somvr_settings) == 96 &&
              SOMVR_VMA_BITE == 1u << 30 && !strcmp(somvr_set_key(SOMVR_SK_COMBAT_BITE), "COMBAT_BITE") &&
              !strcmp(somvr_set_env(SOMVR_SK_COMBAT_BITE), "SOMVR_COMBAT_BITE") &&
              somvr_set_find("SOMVR_COMBAT_BITE") == SOMVR_SK_COMBAT_BITE && SOMVR_SK_COMBAT_BITE == SOMVR_SK_MOVE - 7,
          "the default off, the spare byte 82, bit 30, the key and its switch");
    CHECK(somvr_set_put(&s, SOMVR_SK_COMBAT_BITE, " on ") && s.bite == 1 && somvr_set_put(&s, SOMVR_SK_COMBAT_BITE, "0") &&
              s.bite == 0 && somvr_set_put(&s, SOMVR_SK_COMBAT_BITE, "1") && s.bite == 1 &&
              !somvr_set_put(&s, SOMVR_SK_COMBAT_BITE, "hard") && s.bite == 1,
          "put: on|off|1|0 (%u)", s.bite);
    somvr_set_get(&s, SOMVR_SK_COMBAT_BITE, v, sizeof v);
    somvr_set_format(&s, 1u << SOMVR_SK_COMBAT_BITE, file, sizeof file);
    somvr_set_default(&b);
    uint32_t m = somvr_set_parse(&b, file, &bad, note, sizeof note);
    CHECK(!strcmp(v, "on") && strstr(file, "COMBAT_BITE=on\n") && m == 1u << SOMVR_SK_COMBAT_BITE && !bad && b.bite == 1 &&
              somvr_set_diff(&s, &b) == 0 && b.quiver == 0 && b.sword_hand == 0,
          "the file round trip (%s)", v);
    b.bite = 0;
    CHECK(somvr_set_diff(&s, &b) == 1u << SOMVR_SK_COMBAT_BITE, "the diff sees it");
    somvr_set_default(&s);
    const uint32_t A = SOMVR_VMA_BITE;
    CHECK(SOMVR_VMI_BITE == SOMVR_VMI_SWHAND + 1 && !strcmp(somvr_vm_label(SOMVR_VMI_BITE), "Blade bite") &&
              !somvr_vm_shown(0, SOMVR_VMI_BITE) && !somvr_vm_shown(SOMVR_VMA_WEIGHT, SOMVR_VMI_BITE) &&
              somvr_vm_shown(A, SOMVR_VMI_BITE) && somvr_vm_enabled(&s, A, SOMVR_VMI_BITE) &&
              !somvr_vm_enabled(&s, SOMVR_VMA_WEIGHT, SOMVR_VMI_BITE) &&
              somvr_vm_apply(&s, 0, 0, SOMVR_VMI_BITE, SOMVR_VMP_MORE) == SOMVR_VMR_NONE && s.bite == 0,
          "the row below Sword hand, shown and enabled with its bit only");
    somvr_vm_value(&s, SOMVR_VMI_BITE, v, sizeof v);
    int off = !strcmp(v, "Off");
    CHECK(somvr_vm_apply(&s, A, 0, SOMVR_VMI_BITE, SOMVR_VMP_ROW) == SOMVR_VMR_CHANGED && s.bite == 1 &&
              (somvr_vm_value(&s, SOMVR_VMI_BITE, v, sizeof v), !strcmp(v, "On")) && off &&
              somvr_vm_apply(&s, A, 0, SOMVR_VMI_BITE, SOMVR_VMP_MORE) == SOMVR_VMR_CHANGED && s.bite == 0 &&
              somvr_vm_apply(&s, A, 0, SOMVR_VMI_BITE, SOMVR_VMP_LESS) == SOMVR_VMR_CHANGED && s.bite == 1,
          "Off / On: a press, < and > flip it (%u)", s.bite);
    /* build 17's set: the bite between the sword hand and the arrows (build 19: on the Fight page, 38 px; 27 on build
     * 17's one page); its > box hits back there */
    const uint32_t B17 = SOMVR_VMA_MUSIC | SOMVR_VMA_SIZE | SOMVR_VMA_SWORD | SOMVR_VMA_CROUCH | SOMVR_VMA_FLING |
                         SOMVR_VMA_SURFACE | SOMVR_VMA_COMFORT | SOMVR_VMA_HUD | SOMVR_VMA_VIEW | SOMVR_VMA_WEIGHT |
                         SOMVR_VMA_SWHAND | SOMVR_VMA_ARROWS | A;
    int rw[4], rb[4], ra[4], mb[4];
    memset(rw, 0, sizeof rw), memset(rb, 0, sizeof rb), memset(ra, 0, sizeof ra), memset(mb, 0, sizeof mb);
    CHECK(somvr_vm_rect_av(B17, SOMVR_VMI_SWHAND, SOMVR_VMP_ROW, rw) && somvr_vm_rect_av(B17, SOMVR_VMI_BITE, SOMVR_VMP_ROW, rb) &&
              somvr_vm_rect_av(B17, SOMVR_VMI_ARROWS, SOMVR_VMP_ROW, ra) &&
              somvr_vm_rect_av(B17, SOMVR_VMI_BITE, SOMVR_VMP_MORE, mb) && rw[1] + rw[3] <= rb[1] && rb[1] + rb[3] <= ra[1] &&
              rb[3] == 38 && mb[1] >= rb[1] && mb[1] + mb[3] <= rb[1] + rb[3],
          "build 17's set: sword hand %d, bite %d (%d px), arrows %d", rw[1], rb[1], rb[3], ra[1]);
    somvr_vm_hit h = somvr_vm_hit_test_av(B17, SOMVR_VMPG_FIGHT, (mb[0] + 0.5f * mb[2]) / SOMVR_VM_TEX_W,
                                          (mb[1] + 0.5f * mb[3]) / SOMVR_VM_TEX_H);
    CHECK(h.item == SOMVR_VMI_BITE && h.part == SOMVR_VMP_MORE, "its > box hits back (%d %d)", h.item, h.part);
}

/* ---- build 13: the Crouch row (re-xr-pipeline.md 41): the key, button|duck, the last row, shown with SOMVR_CROUCH --- */
static void test_crouch(void) {
    somvr_settings s, b;
    char v[32], file[512];
    somvr_set_default(&s);
    CHECK(s.crouch == 0 && offsetof(somvr_settings, crouch) == 76 && sizeof(somvr_settings) == 96 &&
              !strcmp(somvr_set_key(SOMVR_SK_CROUCH), "CROUCH") && !strcmp(somvr_set_env(SOMVR_SK_CROUCH), "SOMVR_CROUCH") &&
              somvr_set_find("CROUCH") == SOMVR_SK_CROUCH && SOMVR_SK_CROUCH == SOMVR_SK_MOVE - 13,
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
    CHECK(SOMVR_VMI_CROUCH == SOMVR_VMI_MOVE - 9 && !strcmp(somvr_vm_label(SOMVR_VMI_CROUCH), "Crouch") &&
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
    CHECK(SOMVR_VRMENU_VERSION == 7 && sizeof(somvr_vrmenu_shared) == 208 &&
              offsetof(somvr_vrmenu_shared, practice) == 200 && offsetof(somvr_vrmenu_shared, pstate) == 204 &&
              offsetof(somvr_settings, orc) == 60,
          "the v3 ABI's fields (v4, v5, v6, v7: 8 bytes later each)");
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
              somvr_vm_apply(&s, P, 0, SOMVR_VMI_PR_ORC, SOMVR_VMP_MORE) == 1 && s.orc == 3 && /* build 22: the beasts */
              (somvr_vm_value(&s, SOMVR_VMI_PR_ORC, v, sizeof v), !strcmp(v, "Caragor")) &&
              somvr_vm_apply(&s, P, 0, SOMVR_VMI_PR_ORC, SOMVR_VMP_MORE) == 1 && s.orc == 4 &&
              somvr_vm_apply(&s, P, 0, SOMVR_VMI_PR_ORC, SOMVR_VMP_MORE) == 1 && s.orc == 5 &&
              (somvr_vm_value(&s, SOMVR_VMI_PR_ORC, v, sizeof v), !strcmp(v, "Graug")) &&
              somvr_vm_apply(&s, P, 0, SOMVR_VMI_PR_ORC, SOMVR_VMP_MORE) == 0 && s.orc == 5 &&
              somvr_vm_apply(&s, P, 0, SOMVR_VMI_PR_ORC, SOMVR_VMP_ROW) == 1 && s.orc == 0 &&
              somvr_vm_apply(&s, 0, 0, SOMVR_VMI_PR_ORC, SOMVR_VMP_MORE) == 0,
          "orc: berserker, the beasts, graug held, wraps, needs practice");
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

/* build 6 (re-xr-pipeline.md 28): the tab strip and the Controls page (and the arrival card's picture); build 27
 * (re-xr-pipeline.md 112): every gesture in groups, paged, each line only with its feature */
static int ctl_has(uint64_t avail, const char *key, const char *what) { /* the line (what NULL: any) is on the page */
    const char *k, *w;
    int n = somvr_vm_controls_av(avail, 0, &k, &w), c = 0;
    for (int i = 0; i < n; i++) {
        somvr_vm_controls_av(avail, i, &k, &w);
        c += !strcmp(k, key) && (!what || !strcmp(w, what));
    }
    return c;
}
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
    /* the hit test: each tab on every page; on the Controls page only the tabs, the close box and (build 27) Prev /
     * Next */
    int tabs[3] = {SOMVR_VMI_TAB_SET, SOMVR_VMI_TAB, SOMVR_VMI_TAB_CTL}, all = 1, rc[4];
    for (int pg = 0; pg < 3; pg++)
        for (int i = 0; i < 3; i++) {
            somvr_vm_rect(tabs[i], SOMVR_VMP_BUTTON, rc);
            somvr_vm_hit h = somvr_vm_hit_test_page(pg, (rc[0] + 0.5f * rc[2]) / SOMVR_VM_TEX_W, (rc[1] + 0.5f * rc[3]) / SOMVR_VM_TEX_H);
            all &= h.item == tabs[i] && h.part == SOMVR_VMP_BUTTON;
        }
    CHECK(all, "every tab hit on every page");
    int only = 1, prev = 0, next = 0;
    for (int y = 0; y < SOMVR_VM_TEX_H; y += 4)
        for (int x = 0; x < SOMVR_VM_TEX_W; x += 4) {
            somvr_vm_hit h = somvr_vm_hit_test_page(SOMVR_VMPG_CONTROLS, (x + 0.5f) / SOMVR_VM_TEX_W, (y + 0.5f) / SOMVR_VM_TEX_H);
            prev |= h.item == SOMVR_VMI_CTL_PREV, next |= h.item == SOMVR_VMI_CTL_NEXT;
            if (h.item >= 0 && h.item != SOMVR_VMI_CLOSE && h.item != SOMVR_VMI_CTL_PREV && h.item != SOMVR_VMI_CTL_NEXT &&
                somvr_vm_tab_page(h.item) < 0)
                only = 0;
        }
    somvr_vm_rect(SOMVR_VMI_CTL_NEXT, SOMVR_VMP_BUTTON, rc);
    CHECK(only && prev && next && rc[1] == SOMVR_VM_TEST_NAV_Y && rc[0] == 344 &&
              somvr_vm_apply(&s, 0, 0, SOMVR_VMI_CTL_NEXT, SOMVR_VMP_BUTTON) == SOMVR_VMR_SCROLL &&
              somvr_vm_apply(&s, 0, 0, SOMVR_VMI_CTL_PREV, SOMVR_VMP_BUTTON) == SOMVR_VMR_SCROLL &&
              !strcmp(somvr_vm_label(SOMVR_VMI_CTL_PREV), "Prev") && somvr_vm_hit_test_page(SOMVR_VMPG_SETTINGS,
              (rc[0] + 0.5f * rc[2]) / SOMVR_VM_TEX_W, (rc[1] + 0.5f * rc[3]) / SOMVR_VM_TEX_H).item != SOMVR_VMI_CTL_NEXT,
          "the Controls page: the tabs, X and its Prev / Next (with or without the Test tab) - not on the Settings page");
    /* the headset's build 26-27 set (scripts/headset-build26.env: the session 20261030-071307's 0x7ff67ff027, the
     * layer's Report / Quit / Test, the ride, build 27's bits) with the switches' settings (the dodge, the wraith step,
     * the stealth kill on; the string on the trigger, arrows on the string) */
    const uint64_t NEWB = SOMVR_VMA_WALKDIR | SOMVR_VMA_VAULT | SOMVR_VMA_ABOVE | SOMVR_VMA_BRAND | SOMVR_VMA_RALLY |
                          SOMVR_VMA_DISMISS | SOMVR_VMA_COMMAND | SOMVR_VMA_FAILED | SOMVR_VMA_NEMESIS | SOMVR_VMA_DOWN |
                          SOMVR_VMA_OVERHANG | SOMVR_VMA_COUNTER | SOMVR_VMA_DOMHOLD | SOMVR_VMA_OVERORC |
                          SOMVR_VMA_STRUGGLE | SOMVR_VMA_MANTLE | SOMVR_VMA_GLYPH | SOMVR_VMA_SHOVE | SOMVR_VMA_FLASH |
                          SOMVR_VMA_GROUNDK | /* (FLASH, GROUNDK: the abilities helper's, re-xr-pipeline.md 114) */
                          SOMVR_VMA_ABIL2;    /* (ABIL2: the abilities2 helper's, re-xr-pipeline.md 118) */
    const uint64_t H26 = 0x7ff67ff027ull | SOMVR_VMA_MARK | SOMVR_VMA_QUIT | SOMVR_VMA_TESTPAGE | SOMVR_VMA_RIDE |
                         SOMVR_VMA_ARMY | NEWB; /* (ARMY: the story2 helper's, re-xr-pipeline.md 110) */
    somvr_settings hs;
    somvr_set_default(&hs);
    hs.dodge = hs.wstep = hs.stealth = 1, hs.bow_string = 1, hs.quiver = 0;
    uint64_t ca = somvr_vm_ctl_avail(&hs, H26);
    static const struct { const char *k, *w; } want[] = {
        {"Walk", "left stick"}, {"Sprint", "click left stick"}, {"Turn", "right stick"},
        {"Crouch", "duck or R stick down"}, {"Jump", "A on foot"}, {"Vault", "A at the gold ring"},
        {"Blink", "R stick fwd, let go"}, {"Use", "B (pick, missions)"},
        {"Climb", "grip, pull; fling up"}, {"Ledge", "pull at the ring"}, {"Onto top", "A at the gold ring"}, {"Overhang", "A at its gold ring"},
        {"Down", "grip over the edge"}, {"Drop", "hold A, let go"},
        {"Sword", "grip at R shoulder"}, {"Sheathe", "grip there again"}, {"2 hands", "L grip/LT on hilt"},
        {"Block", "sword across a blow"}, {"Dodge", "A in a fight"}, {"Dagger", "L grip at L hip"}, {"Shove", "L hand push a shield"},
        {"Stealth", "stab his back hard"}, {"Above", "stab down from ledge"}, {"Over orc", "blink aimed at him"},
        {"Struggle", "hand to ring, stab"}, {"Downed", "stab down into him"},
        {"Grab", "L grip on orc's head"}, {"Stun", "held: tap L trigger"}, {"Drain", "held: hold L trigger"},
        {"Question", "held: pull to face"}, {"Execute", "R trigger + hard hit"}, {"Brand", "L palm on head + LT"},
        {"Rally", "L hand high + LT"}, {"Whistle", "duck, hand at mouth"}, {"Dominate", "B dismiss, Y command"},
        {"Bow", "L grip at L shoulder"}, {"Shoot", "RT, pull, let go"}, {"Wraith", "hold Y; A = Strike"}, {"Glyph", "hold Y, look at it"},
        {"Flash", "RT + L palm push out"}, {"Ignite", "B while drawn"},
        {"Caragor", "sword across a leap"}, {"Mount", "L grip stunned beast"}, {"Hold on", "keep the grip closed"},
        {"Steer", "left stick"}, {"Riding", "A leap RT bite B off"},
        {"Menu", "look at left palm"}, {"Pause", "tap menu, hold=Menu"}, {"Screens", "point + trigger/tap"},
        {"Map", "R click, hold=Army"},
        {"Minimap", "back of left wrist"}, {"Failed", "A retry, B abandon"}, {"Nemesis", "B closes the pages"},
        {"Recentre", "Settings > Recentre"}, {"Report", "hold B + Y (0.5 s)"}, {"Quit", "Settings > Quit x2"}};
    const int NW = (int)(sizeof want / sizeof want[0]);
    const char *k, *w;
    int n = somvr_vm_controls_av(ca, 0, &k, &w), fit = 1, missing = -1;
    for (int i = 0; i < n; i++) {
        somvr_vm_controls_av(ca, i, &k, &w);
        fit &= k && w && *k && *w && strlen(k) <= SOMVR_VM_CTL_KEY_W && strlen(w) <= SOMVR_VM_CTL_WHAT_W;
    }
    for (int j = 0; j < NW && missing < 0; j++)
        if (ctl_has(ca, want[j].k, want[j].w) != 1) missing = j;
    somvr_vm_controls_av(ca, n, &k, &w);
    CHECK(n == NW && fit && missing < 0 && !k && !w && !ctl_has(ca, "Arrow", NULL),
          "build 26-27's headset: %d lines (want %d), each once and fitting (%d); the first missing: %s %s", n, NW, fit,
          missing < 0 ? "none" : want[missing].k, missing < 0 ? "" : want[missing].w);
    /* each build 27 bit alone adds its own line(s) and nothing else; none of them without it */
    static const struct { uint64_t bit; const char *k; int lines; } one[] = {
        {SOMVR_VMA_VAULT, "Vault", 1},     {SOMVR_VMA_ABOVE, "Above", 1},        {SOMVR_VMA_BRAND, "Brand", 1},
        {SOMVR_VMA_RALLY, "Rally", 1},     {SOMVR_VMA_DISMISS, "Dominate", 1},   {SOMVR_VMA_COMMAND, "Dominate", 1},
        {SOMVR_VMA_FAILED, "Failed", 1},   {SOMVR_VMA_NEMESIS, "Nemesis", 1},    {SOMVR_VMA_DOWN, "Down", 1},
        {SOMVR_VMA_OVERHANG, "Overhang", 1}, {SOMVR_VMA_RIDE, "Mount", 3},       {SOMVR_VMA_JUMP, "Jump", 1},
        {SOMVR_VMA_WALKDIR, NULL, 0},      {SOMVR_VMA_COUNTER, NULL, 0},         {SOMVR_VMA_DOMHOLD, NULL, 0},
        {SOMVR_VMA_OVERORC, NULL, 0},      {SOMVR_VMA_STRUGGLE, "Struggle", 1}, {SOMVR_VMA_MANTLE, "Onto top", 1},
        {SOMVR_VMA_GLYPH, "Glyph", 1},     {SOMVR_VMA_SHOVE, "Shove", 1},       {SOMVR_VMA_FLASH, "Flash", 1},
        {SOMVR_VMA_GROUNDK, "Downed", 1}, {SOMVR_VMA_ABIL2, "Ignite", 2}};
    int n0 = somvr_vm_controls_av(0, 0, &k, &w), ok1 = 1, bad1 = -1;
    for (size_t j = 0; j < sizeof one / sizeof one[0]; j++) {
        int nj = somvr_vm_controls_av(one[j].bit, 0, &k, &w);
        int good = nj == n0 + one[j].lines && (!one[j].k || (ctl_has(one[j].bit, one[j].k, NULL) && !ctl_has(0, one[j].k, NULL)));
        if (!good && bad1 < 0) bad1 = (int)j;
        ok1 &= good;
    }
    CHECK(ok1 && ctl_has(SOMVR_VMA_RIDE | SOMVR_VMA_COUNTER, "Caragor", NULL) && !ctl_has(SOMVR_VMA_COUNTER, "Caragor", NULL) &&
              ctl_has(SOMVR_VMA_RIDE | SOMVR_VMA_DOMHOLD, "Hold on", NULL) && !ctl_has(SOMVR_VMA_RIDE, "Hold on", NULL) &&
              ctl_has(SOMVR_VMA_DISMISS, "Dominate", "B dismiss") && ctl_has(SOMVR_VMA_COMMAND, "Dominate", "Y command") &&
              ctl_has(SOMVR_VMA_ARMY, "Map", "R click, hold=Army") && ctl_has(0, "Map", "click right stick") &&
              somvr_vm_controls_av(SOMVR_VMA_ARMY, 0, &k, &w) == n0,
          "each gesture's bit: its line only with it (the first wrong: %d); the counter and the hold only with the ride", bad1);
    /* build 6's base without any switch: the walk, the sword, the menu with pause, quit, the report, the minimap, the
     * wall's drop, the bow on the trigger; no grab, no A in a fight, no dash */
    CHECK(ctl_has(0, "Walk", NULL) && ctl_has(0, "Sword", NULL) && ctl_has(0, "Menu", "look at left palm") &&
              ctl_has(0, "Pause", "tap menu, hold=Menu") && ctl_has(0, "Quit", NULL) && ctl_has(0, "Report", NULL) &&
              ctl_has(0, "Minimap", NULL) && ctl_has(0, "Drop", "A (on a wall)") && ctl_has(0, "Bow", "hold left trigger") &&
              ctl_has(0, "Shoot", "R grip, pull, let go") && ctl_has(0, "Turn", "R stick, down=crouch") &&
              !ctl_has(0, "Grab", NULL) && !ctl_has(0, "Dodge", NULL) && !ctl_has(0, "Stealth", NULL) &&
              !ctl_has(0, "Blink", NULL) && !ctl_has(0, "Execute", NULL),
          "no switch: build 6's lines (%d)", n0);
    /* a Fight row Off takes its line (the dodge, the wraith step, the stealth kill) */
    somvr_settings off = hs;
    off.dodge = off.wstep = off.stealth = 0;
    uint64_t co = somvr_vm_ctl_avail(&off, H26);
    CHECK(!ctl_has(co, "Dodge", NULL) && !ctl_has(co, "Blink", NULL) && !ctl_has(co, "Stealth", NULL) &&
              !ctl_has(co, "Over orc", NULL) && somvr_vm_controls_av(co, 0, &k, &w) == n - 4 &&
              ctl_has(SOMVR_VMA_WSTEP | SOMVR_VMA_OVERORC, "Over orc", NULL) && !ctl_has(SOMVR_VMA_WSTEP, "Over orc", NULL),
          "the Fight rows Off: their lines go (the vault over an orc with the wraith step's)");
    /* the pages: titles and lines inside the room, apart, in order; each line once; each group whole on a page */
    int pages = somvr_vm_ctl_pages(ca), lines = 0, inside = 1, order = 1, whole = 1, titles = 0, seen_g[16] = {0};
    for (int pg = 0; pg < pages; pg++) {
        int last = -1000, kind, y;
        const char *grp = NULL;
        for (int i = 0; (kind = somvr_vm_ctl_entry(ca, pg, i, &k, &w, &y)) != SOMVR_VMC_NONE; i++) {
            inside &= y >= SOMVR_VM_CTL_Y0 && y + SOMVR_VM_CTL_DY <= SOMVR_VM_CTL_BOTTOM;
            order &= y >= last + SOMVR_VM_CTL_DY, last = y;
            if (kind == SOMVR_VMC_GROUP) {
                titles++, grp = k;
                for (int g = 0; somvr_vm_ctl_group(g); g++)
                    if (!strcmp(somvr_vm_ctl_group(g), k)) whole &= !seen_g[g]++;
            } else {
                lines++, order &= grp != NULL;
            }
        }
    }
    int p0 = somvr_vm_ctl_entry(ca, 0, 0, &k, &w, NULL);
    CHECK(pages >= 2 && pages <= 4 && lines == n && inside && order && whole && titles == 7 && p0 == SOMVR_VMC_GROUP &&
              !strcmp(k, "Moving") && somvr_vm_ctl_entry(ca, pages, 0, &k, &w, NULL) == SOMVR_VMC_NONE &&
              somvr_vm_ctl_turn(ca, 0, -1) == 0 && somvr_vm_ctl_turn(ca, 0, 1) == 1 &&
              somvr_vm_ctl_turn(ca, pages - 1, 1) == pages - 1 && somvr_vm_ctl_pages(0) >= 1,
          "the headset's pages: %d, %d lines and %d titles inside the room (%d), apart (%d), each group whole (%d)", pages,
          lines, titles, inside, order, whole);
    /* the card: the first steps, what a first-time player needs before anything else */
    int nc = somvr_vm_ctl_card(ca, 0, &k, &w), cw = 0, cs = 0, cm = 0;
    for (int i = 0; i < nc; i++) {
        somvr_vm_ctl_card(ca, i, &k, &w);
        cw |= !strcmp(k, "Walk"), cs |= !strcmp(k, "Sword"), cm |= !strcmp(k, "Menu");
    }
    CHECK(nc >= 6 && nc <= 10 && cw && cs && cm && SOMVR_VM_CTL_Y0 + 4 + nc * SOMVR_VM_CARD_DY + 12 + 2 * SOMVR_VM_CARD_DY + 12 +
              3 * SOMVR_VM_CARD_DY + 12 + SOMVR_VM_CTL_DY <= SOMVR_VM_TEX_H - 16,
          "the card: %d first steps (walk %d sword %d menu %d), the pointer to the rest and the close hint fit", nc, cw, cs, cm);
    /* the pictures: the Controls page, its second page, the card (no tabs: a title; the close hint) */
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
    /* Prev dimmed on the first page, Next lit; the second page another picture with Prev lit */
    int pv[4], nx[4];
    somvr_vm_rect(SOMVR_VMI_CTL_PREV, SOMVR_VMP_BUTTON, pv), somvr_vm_rect(SOMVR_VMI_CTL_NEXT, SOMVR_VMP_BUTTON, nx);
    vw.set = hs, vw.avail = H26, vw.page = SOMVR_VMPG_CONTROLS, vw.ctl_page = 0;
    somvr_vm_draw(&F, g_px, 0, &vw);
    uint8_t p1[4], n1[4];
    memcpy(p1, pxa(g_px, pv[0] + 3, pv[1] + 3), 4), memcpy(n1, pxa(g_px, nx[0] + 3, nx[1] + 3), 4);
    vw.ctl_page = 1;
    somvr_vm_draw(&F, g_px2, 0, &vw);
    const uint8_t *p2 = pxa(g_px2, pv[0] + 3, pv[1] + 3);
    CHECK(memcmp(g_px, g_px2, sizeof g_px) != 0 && memcmp(p1, n1, 4) != 0 && !memcmp(p2, n1, 4),
          "page 1: Prev dimmed (%u %u %u), Next lit (%u %u %u); page 2 another picture, its Prev lit", p1[0], p1[1], p1[2],
          n1[0], n1[1], n1[2]);
    vw.ctl_page = 0, vw.card = 1;
    somvr_vm_draw(&F, g_px2, 0, &vw);
    bt = pxa(g_px2, t0[0] + 2, t0[1] + 2);
    int diff_low = 0;
    for (int y = SOMVR_VM_TEST_NAV_Y; y < SOMVR_VM_TEX_H - 16; y++) /* the card has no Prev / Next row */
        for (int x = 0; x < SOMVR_VM_TEX_W; x++) diff_low += memcmp(pxa(g_px, x, y), pxa(g_px2, x, y), 4) != 0;
    CHECK(bt[0] == 22 && bt[1] == 26 && bt[2] == 33 && diff_low > 200, "the card: no tab buttons (%u %u %u), no page row "
          "(%d pixels differ)", bt[0], bt[1], bt[2], diff_low);
    /* build 27: at the practice area's arrival the card says the way back to the game (Menu > Practice > Back) */
    vw.avail = H26 & ~(uint64_t)SOMVR_VMA_PRACTICE;
    somvr_vm_draw(&F, g_px, 0, &vw);
    int diff_pr = 0;
    for (int y = 400; y < SOMVR_VM_TEX_H - 16; y++)
        for (int x = 0; x < SOMVR_VM_TEX_W; x++) diff_pr += memcmp(pxa(g_px, x, y), pxa(g_px2, x, y), 4) != 0;
    CHECK((H26 & SOMVR_VMA_PRACTICE) && diff_pr > 500, "the card with the practice area: the way back drawn (%d pixels)",
          diff_pr);
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
    /* build 10's menu (no MOVES, with MUSIC): build 19: 13 rows of 38 px, Music right below the haptics (the Sword
     * angle the Fight page's first) */
    const uint32_t B10 = SOMVR_VMA_SWORD | M;
    CHECK(somvr_vm_rect_av(B10, SOMVR_VMI_HAPTICS, SOMVR_VMP_ROW, rh) && somvr_vm_rect_av(B10, SOMVR_VMI_SWORD, SOMVR_VMP_ROW, rs) &&
              somvr_vm_rect_av(B10, SOMVR_VMI_MUSIC, SOMVR_VMP_ROW, rm) &&
              somvr_vm_rect_av(B10, SOMVR_VMI_RECENTER, SOMVR_VMP_BUTTON, rr) && rh[3] == 38 && rs[1] == 64 &&
              rm[1] == rh[1] + 38 && rm[1] + rm[3] <= rr[1] && rm[1] == 64 + 12 * 38,
          "build 10's rows: haptics %d, sword %d, music %d..%d, buttons %d", rh[1], rs[1], rm[1], rm[1] + rm[3], rr[1]);
    /* the bottom row stays put whatever is shown */
    int r0[4];
    somvr_vm_rect_av(SOMVR_VMA_MOVES | B10, SOMVR_VMI_RECENTER, SOMVR_VMP_BUTTON, r0);
    CHECK(r0[1] == rr[1] && rr[1] == 64 + 16 * 38 + 12, "the bottom row at %d / %d", r0[1], rr[1]);
    /* the hit test follows the shown rows: the music row's > box with B10 is the music, with 0 the page button that
     * closes up into its slot (Fight settings >: build 19) */
    somvr_vm_rect_av(B10, SOMVR_VMI_MUSIC, SOMVR_VMP_MORE, rc);
    float u = (rc[0] + 0.5f * rc[2]) / SOMVR_VM_TEX_W, vv = (rc[1] + 0.5f * rc[3]) / SOMVR_VM_TEX_H;
    somvr_vm_hit h1 = somvr_vm_hit_test_av(B10, SOMVR_VMPG_SETTINGS, u, vv), h0 = somvr_vm_hit_test_av(0, SOMVR_VMPG_SETTINGS, u, vv);
    CHECK(h1.item == SOMVR_VMI_MUSIC && h1.part == SOMVR_VMP_MORE && h0.item == SOMVR_VMI_PG_FIGHT, "hits: %d/%d, without %d", h1.item,
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

/* ---- build 17 (re-xr-pipeline.md 56): the Sword hand and Arrows rows (v6's bytes 80 and 81), their keys, the bits;
 * the Controls page's lines for them (somvr_vm_ctl_avail), the block, the two hands, the dagger, the ledge's ring;
 * the practice page's defence lines -------------------------------------------------------------------------------- */
static void test_build17(void) {
    somvr_settings s, b;
    char v[32], file[1024];
    somvr_set_default(&s);
    CHECK(SOMVR_VRMENU_VERSION == 7 && sizeof(somvr_settings) == 96 && offsetof(somvr_settings, sword_hand) == 80 &&
              offsetof(somvr_settings, quiver) == 81 && offsetof(somvr_settings, bite) == 82 &&
              offsetof(somvr_settings, pad6) == 85 && s.sword_hand == 0 && s.quiver == 0 && !s.bite && !s.pad6[0] &&
              !s.pad6[2] && offsetof(somvr_settings, move) == 83 && !s.move && /* build 27: MOVE took pad6[0] */
              offsetof(somvr_settings, refresh) == 84 && !s.refresh, /* build 27 (perf2): XR_REFRESH the next */
          "v6: the two bytes after the weight, 5 spare, the defaults 0");
    CHECK(!strcmp(somvr_set_key(SOMVR_SK_SWORD_WEIGHT_HAND), "SWORD_WEIGHT_HAND") &&
              !strcmp(somvr_set_env(SOMVR_SK_SWORD_WEIGHT_HAND), "SOMVR_SWORD_WEIGHT_HAND") &&
              !strcmp(somvr_set_key(SOMVR_SK_BOW_QUIVER), "BOW_QUIVER") &&
              !strcmp(somvr_set_env(SOMVR_SK_BOW_QUIVER), "SOMVR_BOW_QUIVER") &&
              somvr_set_find("SOMVR_BOW_QUIVER") == SOMVR_SK_BOW_QUIVER &&
              somvr_set_find("SWORD_WEIGHT_HAND") == SOMVR_SK_SWORD_WEIGHT_HAND && SOMVR_SK_BOW_QUIVER == SOMVR_SK_MOVE - 8,
          "the keys, their switches, the last two");
    CHECK(somvr_set_put(&s, SOMVR_SK_SWORD_WEIGHT_HAND, " follow ") && s.sword_hand == 1 &&
              somvr_set_put(&s, SOMVR_SK_SWORD_WEIGHT_HAND, "fixed") && s.sword_hand == 0 &&
              !somvr_set_put(&s, SOMVR_SK_SWORD_WEIGHT_HAND, "1") && !somvr_set_put(&s, SOMVR_SK_SWORD_WEIGHT_HAND, "on") &&
              somvr_set_put(&s, SOMVR_SK_BOW_QUIVER, "on") && s.quiver == 1 && somvr_set_put(&s, SOMVR_SK_BOW_QUIVER, "0") &&
              s.quiver == 0 && somvr_set_put(&s, SOMVR_SK_BOW_QUIVER, "1") && s.quiver == 1 &&
              !somvr_set_put(&s, SOMVR_SK_BOW_QUIVER, "auto") && s.quiver == 1 && s.sword_hand == 0,
          "put: the switches' words (%u %u)", s.sword_hand, s.quiver);
    s.sword_hand = 1;
    somvr_set_get(&s, SOMVR_SK_SWORD_WEIGHT_HAND, v, sizeof v);
    CHECK(!strcmp(v, "follow"), "get: %s", v);
    somvr_set_format(&s, (1u << SOMVR_SK_SWORD_WEIGHT_HAND) | (1u << SOMVR_SK_BOW_QUIVER), file, sizeof file);
    somvr_set_default(&b);
    int bad = 0;
    char note[96] = "";
    uint32_t m = somvr_set_parse(&b, file, &bad, note, sizeof note);
    CHECK(strstr(file, "SWORD_WEIGHT_HAND=follow\n") && strstr(file, "BOW_QUIVER=on\n") &&
              m == ((1u << SOMVR_SK_SWORD_WEIGHT_HAND) | (1u << SOMVR_SK_BOW_QUIVER)) && !bad && b.sword_hand == 1 &&
              b.quiver == 1 && somvr_set_diff(&s, &b) == 0,
          "the file round trip (%#x, %d bad)", m, bad);
    b.quiver = 0;
    CHECK(somvr_set_diff(&s, &b) == 1u << SOMVR_SK_BOW_QUIVER, "the diff sees the quiver");
    /* the rows: right below Sword weight, shown with their bits; the hand dimmed with the weight Off */
    const uint32_t HA = SOMVR_VMA_WEIGHT | SOMVR_VMA_SWHAND | SOMVR_VMA_ARROWS;
    somvr_set_default(&s);
    s.sword_weight = 2;
    CHECK(SOMVR_VMI_SWHAND == SOMVR_VMI_WEIGHT + 1 && SOMVR_VMI_ARROWS == SOMVR_VMI_WEIGHT + 3 &&
              !strcmp(somvr_vm_label(SOMVR_VMI_SWHAND), "Sword hand") && !strcmp(somvr_vm_label(SOMVR_VMI_ARROWS), "Arrows") &&
              !somvr_vm_shown(SOMVR_VMA_WEIGHT, SOMVR_VMI_SWHAND) && !somvr_vm_shown(SOMVR_VMA_BOWRT, SOMVR_VMI_ARROWS) &&
              somvr_vm_shown(HA, SOMVR_VMI_SWHAND) && somvr_vm_shown(HA, SOMVR_VMI_ARROWS) &&
              somvr_vm_enabled(&s, HA, SOMVR_VMI_SWHAND) && somvr_vm_enabled(&s, HA, SOMVR_VMI_ARROWS) &&
              !somvr_vm_enabled(&s, SOMVR_VMA_WEIGHT, SOMVR_VMI_SWHAND) && !somvr_vm_enabled(&s, 0, SOMVR_VMI_ARROWS),
          "the rows' bits");
    somvr_vm_value(&s, SOMVR_VMI_SWHAND, v, sizeof v);
    int vf = !strcmp(v, "Fixed");
    somvr_vm_value(&s, SOMVR_VMI_ARROWS, v, sizeof v);
    int va = !strcmp(v, "Auto");
    CHECK(somvr_vm_apply(&s, HA, 0, SOMVR_VMI_SWHAND, SOMVR_VMP_MORE) == SOMVR_VMR_CHANGED && s.sword_hand == 1 &&
              somvr_vm_apply(&s, HA, 0, SOMVR_VMI_ARROWS, SOMVR_VMP_ROW) == SOMVR_VMR_CHANGED && s.quiver == 1 &&
              vf && va,
          "a click flips them (hand %u quiver %u)", s.sword_hand, s.quiver);
    somvr_vm_value(&s, SOMVR_VMI_SWHAND, v, sizeof v);
    vf = !strcmp(v, "Follow");
    somvr_vm_value(&s, SOMVR_VMI_ARROWS, v, sizeof v);
    va = !strcmp(v, "Quiver");
    s.sword_weight = 0;
    CHECK(vf && va && !somvr_vm_enabled(&s, HA, SOMVR_VMI_SWHAND) &&
              somvr_vm_apply(&s, HA, 0, SOMVR_VMI_SWHAND, SOMVR_VMP_LESS) == SOMVR_VMR_NONE && s.sword_hand == 1 &&
              somvr_vm_enabled(&s, HA, SOMVR_VMI_ARROWS) &&
              somvr_vm_apply(&s, 0, 0, SOMVR_VMI_ARROWS, SOMVR_VMP_LESS) == SOMVR_VMR_NONE && s.quiver == 1,
          "Follow / Quiver; the weight Off: the hand dimmed and unclicked; no bit: nothing");
    /* build 17's set (build 16's 19 rows + these two: 21 of 28 px on build 17's one page): build 19: the order weight,
     * hand, arrows on the Fight page (38 px), Music on the Settings page; the boxes in their rows, a > box hits back,
     * the button row below */
    const uint32_t B17 = SOMVR_VMA_MUSIC | SOMVR_VMA_SIZE | SOMVR_VMA_SWORD | SOMVR_VMA_CROUCH | SOMVR_VMA_FLING |
                         SOMVR_VMA_SURFACE | SOMVR_VMA_COMFORT | SOMVR_VMA_HUD | SOMVR_VMA_VIEW | HA;
    int rw[4], rh[4], ra[4], rm[4], mb[4], rl[4], rb[4];
    CHECK(somvr_vm_rect_av(B17, SOMVR_VMI_WEIGHT, SOMVR_VMP_ROW, rw) && somvr_vm_rect_av(B17, SOMVR_VMI_SWHAND, SOMVR_VMP_ROW, rh) &&
              somvr_vm_rect_av(B17, SOMVR_VMI_ARROWS, SOMVR_VMP_ROW, ra) &&
              somvr_vm_rect_av(B17, SOMVR_VMI_MUSIC, SOMVR_VMP_ROW, rm) &&
              somvr_vm_rect_av(B17, SOMVR_VMI_ARROWS, SOMVR_VMP_MORE, mb) &&
              somvr_vm_rect_av(B17, SOMVR_VMI_SURFACE, SOMVR_VMP_ROW, rl) &&
              somvr_vm_rect_av(B17, SOMVR_VMI_RECENTER, SOMVR_VMP_BUTTON, rb) && rw[1] + rw[3] == rh[1] &&
              rh[1] + rh[3] == ra[1] && rm[1] == 64 + 12 * rm[3] && rh[3] == 38 && mb[1] >= ra[1] &&
              mb[1] + mb[3] <= ra[1] + ra[3] && rl[1] + rl[3] <= rb[1],
          "build 17's set: weight %d, hand %d, arrows %d (%d px), music %d; the last row ends %d, the buttons %d", rw[1],
          rh[1], ra[1], ra[3], rm[1], rl[1] + rl[3], rb[1]);
    somvr_vm_hit h = somvr_vm_hit_test_av(B17, SOMVR_VMPG_FIGHT, (mb[0] + 0.5f * mb[2]) / SOMVR_VM_TEX_W,
                                          (mb[1] + 0.5f * mb[3]) / SOMVR_VM_TEX_H);
    CHECK(h.item == SOMVR_VMI_ARROWS && h.part == SOMVR_VMP_MORE, "the Arrows > box hits back (%d %d)", h.item, h.part);
    /* the Controls page with build 17's switches: the quiver's lines follow the row */
    const uint32_t C17 = SOMVR_VMA_FLING | SOMVR_VMA_VERBS | SOMVR_VMA_DESCENT | SOMVR_VMA_MARK | SOMVR_VMA_QUIT |
                         SOMVR_VMA_CROUCH | SOMVR_VMA_USE | SOMVR_VMA_BOWSLOT | SOMVR_VMA_BOWRT | SOMVR_VMA_ARROWS |
                         SOMVR_VMA_BOWTRIG | SOMVR_VMA_PULLOVER;
    struct { int quiver; uint32_t base; const char *arrow, *shoot; int dagger; } cs[] = {
        {1, C17, "RT at R shoulder", "nock, pull, let go", 0},
        {0, C17, NULL, "RT, pull, let go", 1},
        {1, C17 & ~(uint32_t)(SOMVR_VMA_BOWTRIG | SOMVR_VMA_BOWRT), "R grip at R shoulder", "nock, pull, let go", 0},
        {0, C17 & ~(uint32_t)(SOMVR_VMA_BOWTRIG | SOMVR_VMA_BOWRT), NULL, "R grip, pull, let go", 1},
    };
    for (size_t c = 0; c < sizeof cs / sizeof cs[0]; c++) {
        somvr_set_default(&s);
        s.quiver = (uint8_t)cs[c].quiver;
        uint64_t ca = somvr_vm_ctl_avail(&s, cs[c].base);
        const char *k, *w;
        int n = somvr_vm_controls_av(ca, 0, &k, &w), fit = 1, arrows = 0, arrow_ok = !cs[c].arrow, shoots = 0, shoot_ok = 0;
        int block = 0, two = 0, dagger = 0, ledge = 0, sprint = 0, sheathe = 0, quit = 0, parry = 0;
        for (int i = 0; i < n; i++) {
            somvr_vm_controls_av(ca, i, &k, &w);
            fit &= k && w && strlen(k) <= SOMVR_VM_CTL_KEY_W && strlen(w) <= SOMVR_VM_CTL_WHAT_W && *w;
            if (!strcmp(k, "Arrow")) arrows++, arrow_ok = cs[c].arrow && !strcmp(w, cs[c].arrow);
            if (!strcmp(k, "Shoot")) shoots++, shoot_ok = !strcmp(w, cs[c].shoot);
            block |= !strcmp(k, "Block") && !strcmp(w, "sword across a blow");
            two |= !strcmp(k, "2 hands") && strstr(w, "L grip") == w && strstr(w, "hilt");
            dagger |= !strcmp(k, "Dagger") && !strcmp(w, "L grip at L hip");
            ledge |= !strcmp(k, "Ledge") && strstr(w, "ring");
            sprint |= !strcmp(k, "Sprint"), sheathe |= !strcmp(k, "Sheathe"), quit |= !strcmp(k, "Quit");
            parry |= !strcmp(k, "Parry");
        }
        /* (build 27: every line on its group's page - nothing dropped for room, the dagger, Sheathe and Quit too) */
        CHECK(fit && arrows == (cs[c].arrow != NULL) && arrow_ok && shoots == 1 && shoot_ok && block && two && ledge &&
                  sprint && dagger && sheathe && quit && !parry,
              "build 17's controls, quiver %d %s: %d lines, arrow %d shoot %d block %d two %d ledge %d sprint %d "
              "dagger %d sheathe %d quit %d",
              cs[c].quiver, (cs[c].base & SOMVR_VMA_BOWTRIG) ? "RT" : "grip", n, arrow_ok, shoot_ok, block, two, ledge,
              sprint, dagger, sheathe, quit);
    }
    { /* build 17, the strike17 helper: SOMVR_STRIKE_GAZE's Wraith line instead of the plain one, the count unchanged */
        somvr_set_default(&s);
        const char *k, *w;
        uint64_t ca = somvr_vm_ctl_avail(&s, C17), cg = somvr_vm_ctl_avail(&s, C17 | SOMVR_VMA_STRIKE);
        int n0 = somvr_vm_controls_av(ca, 0, &k, &w), ng = somvr_vm_controls_av(cg, 0, &k, &w), wr = 0, wr_old = 0, spr = 0;
        for (int i = 0; i < ng; i++) {
            somvr_vm_controls_av(cg, i, &k, &w);
            wr += !strcmp(k, "Wraith") && !strcmp(w, "hold Y; A = Strike") && strlen(w) <= 20;
            wr_old += !strcmp(k, "Wraith") && !strcmp(w, "hold Y (vision)"), spr |= !strcmp(k, "Sprint");
        }
        CHECK(ng == n0 && wr == 1 && !wr_old && spr && (cg & SOMVR_VMA_STRIKE),
              "the gaze strike's Wraith line: %d lines (%d without), new %d old %d, sprint %d", ng, n0, wr, wr_old, spr);
    }
    s.quiver = 1;
    CHECK(somvr_vm_ctl_avail(&s, SOMVR_VMA_BOWRT) == SOMVR_VMA_BOWRT &&
              somvr_vm_ctl_avail(&s, C17 & ~(uint32_t)SOMVR_VMA_ARROWS) == (C17 & ~(uint32_t)SOMVR_VMA_ARROWS),
          "without the Arrows row: the preload's bits as they are");
    /* the picture follows the row: the Controls page and the card with the quiver on and off differ */
    somvr_vm_view vw;
    memset(&vw, 0, sizeof vw);
    somvr_set_default(&vw.set);
    vw.avail = C17, vw.hover.item = -1, vw.page = SOMVR_VMPG_CONTROLS;
    int differ = 0; /* build 27: the bow's group on its own page */
    for (int pg = 0; pg < 4; pg++) {
        vw.ctl_page = pg, vw.set.quiver = 1;
        somvr_vm_draw(&F, g_px, 0, &vw);
        vw.set.quiver = 0;
        somvr_vm_draw(&F, g_px2, 0, &vw);
        differ |= memcmp(g_px, g_px2, sizeof g_px) != 0;
    }
    CHECK(differ, "the Controls page follows the Arrows row");
    /* the practice page's two defence lines (labels' colour) under its hints, inside the panel */
    vw.avail = SOMVR_VMA_PRACTICE, vw.page = SOMVR_VMPG_PRACTICE;
    somvr_vm_draw(&F, g_px, 0, &vw);
    int lit[2] = {0, 0};
    for (int l = 0; l < 2; l++)
        for (int y = 646 + 32 * l; y < 646 + 32 * l + 32; y++) /* (build 18: 574 + 72, + 104) */
            for (int x = 24; x < SOMVR_VM_TEX_W - 16; x++) {
                const uint8_t *p = pxa(g_px, x, y);
                lit[l] += p[0] == 208 && p[1] == 213 && p[2] == 222;
            }
    CHECK(lit[0] > 200 && lit[1] > 200, "the practice page's block and red lines drawn (%d, %d px)", lit[0], lit[1]);
    /* the practice page's refills (the user's ask): in the area only (as Spawn), their requests, below Remove and
     * above the hints, apart, inside the panel, their labels inside them, a centre hits back */
    const uint32_t P = SOMVR_VMA_PRACTICE, HERE = P | SOMVR_VMA_PR_HERE;
    int refills[2] = {SOMVR_VMI_PR_ARROWS, SOMVR_VMI_PR_HEALTH}, want[2] = {SOMVR_VMPR_ARROWS, SOMVR_VMPR_HEALTH};
    int rr[4], rp[4], prev[4];
    somvr_vm_rect(SOMVR_VMI_PR_REMOVE, SOMVR_VMP_BUTTON, prev);
    for (int i = 0; i < 2; i++) {
        int it = refills[i];
        somvr_set_default(&s);
        CHECK(!somvr_vm_enabled(&s, P, it) && somvr_vm_enabled(&s, HERE, it) &&
                  !somvr_vm_enabled(&s, HERE | SOMVR_VMA_PR_BUSY, it) && !somvr_vm_enabled(&s, SOMVR_VMA_PR_HERE, it) &&
                  somvr_vm_apply(&s, P, 0, it, SOMVR_VMP_BUTTON) == SOMVR_VMR_NONE &&
                  somvr_vm_apply(&s, HERE, 0, it, SOMVR_VMP_BUTTON) == SOMVR_VMR_PRACTICE &&
                  somvr_vm_apply(&s, HERE, 0, it, SOMVR_VMP_ROW) == SOMVR_VMR_NONE &&
                  somvr_vm_practice_action(it) == want[i],
              "%s: in the area only, request %d", somvr_vm_label(it), somvr_vm_practice_action(it));
        CHECK(somvr_vm_rect(it, SOMVR_VMP_BUTTON, rr) && !somvr_vm_rect(it, SOMVR_VMP_ROW, rp) &&
                  rr[1] >= prev[1] + prev[3] && rr[1] + rr[3] <= 574 && rr[3] >= 48 &&
                  somvr_font_text_w(somvr_vm_label(it), SOMVR_VM_SCALE) <= rr[2] - 16,
              "%s: %d..%d under %d, its label %d of %d px", somvr_vm_label(it), rr[1], rr[1] + rr[3], prev[1] + prev[3],
              somvr_font_text_w(somvr_vm_label(it), SOMVR_VM_SCALE), rr[2]);
        h = somvr_vm_hit_test_page(SOMVR_VMPG_PRACTICE, (rr[0] + 0.5f * rr[2]) / SOMVR_VM_TEX_W,
                                   (rr[1] + 0.5f * rr[3]) / SOMVR_VM_TEX_H);
        CHECK(h.item == it && h.part == SOMVR_VMP_BUTTON, "%s hits back (%d)", somvr_vm_label(it), h.item);
        h = somvr_vm_hit_test_page(SOMVR_VMPG_SETTINGS, (rr[0] + 0.5f * rr[2]) / SOMVR_VM_TEX_W,
                                   (rr[1] + 0.5f * rr[3]) / SOMVR_VM_TEX_H);
        CHECK(h.item != it, "%s not on the Settings page", somvr_vm_label(it));
        memcpy(prev, rr, sizeof prev);
    }
    /* the request word carries them (4 bits: the practice's own 5 = status in between) */
    uint32_t pw = somvr_vm_pr_pack(9, SOMVR_VMPR_HEALTH, 0);
    CHECK(somvr_vm_pr_action(pw) == SOMVR_VMPR_HEALTH && somvr_vm_pr_seq(pw) == 9 &&
              somvr_vm_pr_action(somvr_vm_pr_pack(1, SOMVR_VMPR_ARROWS, 2)) == SOMVR_VMPR_ARROWS,
          "the request word: arrows %d, health %d", SOMVR_VMPR_ARROWS, SOMVR_VMPR_HEALTH);
    /* in the area: the two buttons lit (drawn as buttons, not dimmed) */
    vw.avail = HERE;
    somvr_vm_draw(&F, g_px, 0, &vw);
    somvr_vm_rect(SOMVR_VMI_PR_HEALTH, SOMVR_VMP_BUTTON, rr);
    const uint8_t *bp = pxa(g_px, rr[0] + 3, rr[1] + 3);
    CHECK(bp[0] == 52 && bp[1] == 61 && bp[2] == 76, "Refill health a live button in the area (%u %u %u)", bp[0], bp[1], bp[2]);
}

/* ---- build 18 (re-xr-pipeline.md 56.9): the practice page's Spawn far orc (unaware): in the area only (as Spawn),
 * request 8 with the orc type, right under Spawn and above Remove, its label inside it, it hits back; the page's last
 * line still inside the panel ------------------------------------------------------------------------------------ */
static void test_far_orc(void) {
    const uint32_t P = SOMVR_VMA_PRACTICE, HERE = P | SOMVR_VMA_PR_HERE;
    somvr_settings s;
    somvr_set_default(&s);
    s.orc = 2;
    int rf[4], rs[4], rm[4];
    CHECK(SOMVR_VMPR_FAR == 8 && somvr_vm_practice_action(SOMVR_VMI_PR_FAR) == SOMVR_VMPR_FAR &&
              !strcmp(somvr_vm_label(SOMVR_VMI_PR_FAR), "Spawn far orc (unaware)") &&
              !somvr_vm_enabled(&s, P, SOMVR_VMI_PR_FAR) && somvr_vm_enabled(&s, HERE, SOMVR_VMI_PR_FAR) &&
              !somvr_vm_enabled(&s, HERE | SOMVR_VMA_PR_BUSY, SOMVR_VMI_PR_FAR) &&
              somvr_vm_apply(&s, HERE, 0, SOMVR_VMI_PR_FAR, SOMVR_VMP_BUTTON) == SOMVR_VMR_PRACTICE &&
              somvr_vm_apply(&s, P, 0, SOMVR_VMI_PR_FAR, SOMVR_VMP_BUTTON) == SOMVR_VMR_NONE &&
              somvr_vm_pr_orc(somvr_vm_pr_pack(4, SOMVR_VMPR_FAR, (int)s.orc)) == 2,
          "Spawn far orc: in the area only, request 8 with the type");
    CHECK(somvr_vm_rect(SOMVR_VMI_PR_FAR, SOMVR_VMP_BUTTON, rf) && somvr_vm_rect(SOMVR_VMI_PR_SPAWN, SOMVR_VMP_BUTTON, rs) &&
              somvr_vm_rect(SOMVR_VMI_PR_REMOVE, SOMVR_VMP_BUTTON, rm) && rf[1] >= rs[1] + rs[3] &&
              rf[1] + rf[3] <= rm[1] && rf[3] >= 48 &&
              somvr_font_text_w(somvr_vm_label(SOMVR_VMI_PR_FAR), SOMVR_VM_SCALE) <= rf[2] - 16,
          "its place: %d..%d between Spawn's end %d and Remove %d", rf[1], rf[1] + rf[3], rs[1] + rs[3], rm[1]);
    somvr_vm_hit h = somvr_vm_hit_test_page(SOMVR_VMPG_PRACTICE, (rf[0] + 0.5f * rf[2]) / SOMVR_VM_TEX_W,
                                            (rf[1] + 0.5f * rf[3]) / SOMVR_VM_TEX_H);
    CHECK(h.item == SOMVR_VMI_PR_FAR && h.part == SOMVR_VMP_BUTTON, "it hits back (%d)", h.item);
    /* every practice button apart, top to bottom, the last line's ink above the panel's bottom edge */
    int ids[] = {SOMVR_VMI_PR_GO, SOMVR_VMI_PR_BACK, SOMVR_VMI_PR_ORC, SOMVR_VMI_PR_SPAWN, SOMVR_VMI_PR_FAR,
                 SOMVR_VMI_PR_REMOVE, SOMVR_VMI_PR_ARROWS, SOMVR_VMI_PR_HEALTH}, apart = 1, a[4], b[4];
    for (int i = 0; i + 1 < 8; i++) {
        somvr_vm_rect(ids[i], ids[i] == SOMVR_VMI_PR_ORC ? SOMVR_VMP_ROW : SOMVR_VMP_BUTTON, a);
        somvr_vm_rect(ids[i + 1], ids[i + 1] == SOMVR_VMI_PR_ORC ? SOMVR_VMP_ROW : SOMVR_VMP_BUTTON, b);
        apart &= a[1] + a[3] <= b[1];
    }
    CHECK(apart && b[1] + b[3] <= 574 && 574 + 104 + 30 <= SOMVR_VM_TEX_H - 16, "the page's buttons apart, the hints after");
}

/* build 19: the practice page's Spawn group - beside Spawn an orc (the same row, apart, both labels inside), in the area
 * only (as Spawn), its request 9 through the request word, a centre hits back, drawn live in the area */
static void test_build19(void) {
    somvr_settings s;
    somvr_set_default(&s);
    const uint32_t P = SOMVR_VMA_PRACTICE, HERE = P | SOMVR_VMA_PR_HERE;
    const int it = SOMVR_VMI_PR_GROUP;
    CHECK(!somvr_vm_enabled(&s, P, it) && somvr_vm_enabled(&s, HERE, it) &&
              !somvr_vm_enabled(&s, HERE | SOMVR_VMA_PR_BUSY, it) && !somvr_vm_enabled(&s, SOMVR_VMA_PR_HERE, it) &&
              somvr_vm_apply(&s, P, 0, it, SOMVR_VMP_BUTTON) == SOMVR_VMR_NONE &&
              somvr_vm_apply(&s, HERE, 0, it, SOMVR_VMP_BUTTON) == SOMVR_VMR_PRACTICE &&
              somvr_vm_practice_action(it) == SOMVR_VMPR_GROUP && SOMVR_VMPR_GROUP == 9 &&
              !strcmp(somvr_vm_label(it), "Spawn group"),
          "Spawn group: in the area only, request %d", somvr_vm_practice_action(it));
    int g[4], sp[4], rm[4], rp[4];
    CHECK(somvr_vm_rect(it, SOMVR_VMP_BUTTON, g) && somvr_vm_rect(SOMVR_VMI_PR_SPAWN, SOMVR_VMP_BUTTON, sp) &&
              somvr_vm_rect(SOMVR_VMI_PR_REMOVE, SOMVR_VMP_BUTTON, rm) && !somvr_vm_rect(it, SOMVR_VMP_ROW, rp) &&
              g[1] == sp[1] && g[3] == sp[3] && sp[0] + sp[2] + 8 <= g[0] && g[0] + g[2] <= SOMVR_VM_TEX_W - 40 &&
              g[1] + g[3] <= rm[1] && sp[0] == 40 &&
              somvr_font_text_w(somvr_vm_label(it), SOMVR_VM_SCALE) <= g[2] - 16 &&
              somvr_font_text_w(somvr_vm_label(SOMVR_VMI_PR_SPAWN), SOMVR_VM_SCALE) <= sp[2] - 16,
          "Spawn an orc %d..%d | Spawn group %d..%d at y %d, above Remove %d", sp[0], sp[0] + sp[2], g[0], g[0] + g[2],
          g[1], rm[1]);
    somvr_vm_hit h = somvr_vm_hit_test_page(SOMVR_VMPG_PRACTICE, (g[0] + 0.5f * g[2]) / SOMVR_VM_TEX_W,
                                            (g[1] + 0.5f * g[3]) / SOMVR_VM_TEX_H);
    somvr_vm_hit h2 = somvr_vm_hit_test_page(SOMVR_VMPG_PRACTICE, (sp[0] + 0.5f * sp[2]) / SOMVR_VM_TEX_W,
                                             (sp[1] + 0.5f * sp[3]) / SOMVR_VM_TEX_H);
    somvr_vm_hit h3 = somvr_vm_hit_test_page(SOMVR_VMPG_SETTINGS, (g[0] + 0.5f * g[2]) / SOMVR_VM_TEX_W,
                                             (g[1] + 0.5f * g[3]) / SOMVR_VM_TEX_H);
    CHECK(h.item == it && h.part == SOMVR_VMP_BUTTON && h2.item == SOMVR_VMI_PR_SPAWN && h3.item != it,
          "the two hit back (%d, %d), not on the Settings page (%d)", h.item, h2.item, h3.item);
    uint32_t pw = somvr_vm_pr_pack(12, SOMVR_VMPR_GROUP, 1);
    CHECK(somvr_vm_pr_action(pw) == SOMVR_VMPR_GROUP && somvr_vm_pr_seq(pw) == 12 && somvr_vm_pr_orc(pw) == 1,
          "the request word carries 9");
    somvr_vm_view vw;
    memset(&vw, 0, sizeof vw);
    somvr_set_default(&vw.set);
    vw.page = SOMVR_VMPG_PRACTICE, vw.hover.item = -1, vw.avail = HERE;
    somvr_vm_draw(&F, g_px, 0, &vw);
    const uint8_t *bp = pxa(g_px, g[0] + 3, g[1] + 3), *gap = pxa(g_px, sp[0] + sp[2] + 4, sp[1] + 3);
    CHECK(bp[0] == 52 && bp[1] == 61 && bp[2] == 76 && !(gap[0] == 52 && gap[1] == 61 && gap[2] == 76),
          "Spawn group a live button in the area, the gap between the two not (%u %u %u)", bp[0], bp[1], bp[2]);
    vw.avail = P;
    somvr_vm_draw(&F, g_px, 0, &vw);
    bp = pxa(g_px, g[0] + 3, g[1] + 3);
    CHECK(bp[0] == 32 && bp[1] == 37 && bp[2] == 45, "dimmed out of the area (%u %u %u)", bp[0], bp[1], bp[2]);
}

/* build 22 (re-xr-pipeline.md 88): the practice page's Captain - beside Remove the orc (the same row, apart, both labels
 * inside), in the area only (as Spawn), its request 10 through the request word, a centre hits back, drawn live */
static void test_build22_captain(void) {
    somvr_settings s;
    somvr_set_default(&s);
    const uint32_t P = SOMVR_VMA_PRACTICE, HERE = P | SOMVR_VMA_PR_HERE;
    const int it = SOMVR_VMI_PR_CAPTAIN;
    CHECK(!somvr_vm_enabled(&s, P, it) && somvr_vm_enabled(&s, HERE, it) &&
              !somvr_vm_enabled(&s, HERE | SOMVR_VMA_PR_BUSY, it) &&
              somvr_vm_apply(&s, P, 0, it, SOMVR_VMP_BUTTON) == SOMVR_VMR_NONE &&
              somvr_vm_apply(&s, HERE, 0, it, SOMVR_VMP_BUTTON) == SOMVR_VMR_PRACTICE &&
              somvr_vm_practice_action(it) == SOMVR_VMPR_CAPTAIN && SOMVR_VMPR_CAPTAIN == 10 &&
              !strcmp(somvr_vm_label(it), "Captain"),
          "Captain: in the area only, request %d", somvr_vm_practice_action(it));
    int c[4], rm[4], ar[4], rp[4];
    CHECK(somvr_vm_rect(it, SOMVR_VMP_BUTTON, c) && somvr_vm_rect(SOMVR_VMI_PR_REMOVE, SOMVR_VMP_BUTTON, rm) &&
              somvr_vm_rect(SOMVR_VMI_PR_ARROWS, SOMVR_VMP_BUTTON, ar) && !somvr_vm_rect(it, SOMVR_VMP_ROW, rp) &&
              c[1] == rm[1] && c[3] == rm[3] && rm[0] + rm[2] + 8 <= c[0] && c[0] + c[2] <= SOMVR_VM_TEX_W - 40 &&
              c[1] + c[3] <= ar[1] && rm[0] == 40 &&
              somvr_font_text_w(somvr_vm_label(it), SOMVR_VM_SCALE) <= c[2] - 16 &&
              somvr_font_text_w(somvr_vm_label(SOMVR_VMI_PR_REMOVE), SOMVR_VM_SCALE) <= rm[2] - 16,
          "Remove the orc %d..%d | Captain %d..%d at y %d, above the refills %d", rm[0], rm[0] + rm[2], c[0], c[0] + c[2],
          c[1], ar[1]);
    somvr_vm_hit h = somvr_vm_hit_test_page(SOMVR_VMPG_PRACTICE, (c[0] + 0.5f * c[2]) / SOMVR_VM_TEX_W,
                                            (c[1] + 0.5f * c[3]) / SOMVR_VM_TEX_H);
    somvr_vm_hit h2 = somvr_vm_hit_test_page(SOMVR_VMPG_PRACTICE, (rm[0] + 0.5f * rm[2]) / SOMVR_VM_TEX_W,
                                             (rm[1] + 0.5f * rm[3]) / SOMVR_VM_TEX_H);
    CHECK(h.item == it && h.part == SOMVR_VMP_BUTTON && h2.item == SOMVR_VMI_PR_REMOVE, "the two hit back (%d, %d)", h.item,
          h2.item);
    uint32_t pw = somvr_vm_pr_pack(13, SOMVR_VMPR_CAPTAIN, 0);
    CHECK(somvr_vm_pr_action(pw) == SOMVR_VMPR_CAPTAIN && somvr_vm_pr_seq(pw) == 13, "the request word carries 10");
    somvr_vm_view vw;
    memset(&vw, 0, sizeof vw);
    somvr_set_default(&vw.set);
    vw.page = SOMVR_VMPG_PRACTICE, vw.hover.item = -1, vw.avail = HERE;
    somvr_vm_draw(&F, g_px, 0, &vw);
    const uint8_t *bp = pxa(g_px, c[0] + 3, c[1] + 3), *gap = pxa(g_px, rm[0] + rm[2] + 4, rm[1] + 3);
    CHECK(bp[0] == 52 && bp[1] == 61 && bp[2] == 76 && !(gap[0] == 52 && gap[1] == 61 && gap[2] == 76),
          "Captain a live button in the area, the gap not (%u %u %u)", bp[0], bp[1], bp[2]);
}

/* ---- build 19 (re-xr-pipeline.md 73; t3/menu18's 56.8 ported): the availability word is 64 bits and the Fight page
 * (somvr_vrmenu v7): the layout; the free bits 38..63 change nothing; bits 32..37 only the Fight page; the six rows'
 * keys, values, steps; the page buttons; the Controls page as build 18's (no new line); a v6 menu's settings file read
 * by v7 unchanged --------------------------------------------------------------------------------------------------- */
static void test_fight19(void) {
    CHECK(SOMVR_VRMENU_VERSION == 7 && sizeof(((somvr_vrmenu_shared *)0)->avail) == 8 &&
              offsetof(somvr_vrmenu_shared, avail) == 24 && offsetof(somvr_vrmenu_shared, blink_on_ms) == 32 &&
              offsetof(somvr_vrmenu_shared, set) == 48 && sizeof(somvr_vrmenu_shared) == 208 &&
              sizeof(somvr_settings) == 96 && offsetof(somvr_settings, pad6) == 85 &&
              offsetof(somvr_settings, dodge) == 88 && offsetof(somvr_settings, wstep) == 89 &&
              offsetof(somvr_settings, stealth) == 90 && offsetof(somvr_settings, counter_icon) == 91 &&
              offsetof(somvr_settings, crowd_ms) == 92 && offsetof(somvr_settings, bow_string) == 94 &&
              SOMVR_VMA_DODGE == (1ull << 32) && SOMVR_VMA_WSTEP == (1ull << 33) && SOMVR_VMA_STEALTH == (1ull << 34) &&
              SOMVR_VMA_CROWD == (1ull << 35) && SOMVR_VMA_CICON == (1ull << 36) && SOMVR_VMA_STRING == (1ull << 37) &&
              sizeof(((somvr_vm_view *)0)->avail) == 8 && SOMVR_SK_N == 33 && SOMVR_SK_ALL == 0x1ffffffffull /* build 27 */,
          "v7: avail 64 bits at 24, the settings 96 (the six rows at 88..94, pad6 spare), 208; bits 32..37; 33 keys (build 27)");
    /* build 17b's preload avail (the session 174757: 0xf7fff027) with the layer's Report / Quit; build 18 hides the
     * Sword hand (SOMVR_SWORD_HAND_LOCK) */
    const uint64_t B18 = (0xf7fff027ull | SOMVR_VMA_MARK | SOMVR_VMA_QUIT) & ~(uint64_t)SOMVR_VMA_SWHAND,
                   NEW = SOMVR_VMA_DODGE | SOMVR_VMA_WSTEP | SOMVR_VMA_STEALTH | SOMVR_VMA_CROWD | SOMVR_VMA_CICON |
                         SOMVR_VMA_STRING,
                   B19 = B18 | NEW,
                   /* 63: build 21's (the layer's); 47: build 27's Walk toward row (its own test: test_walk_toward) */
                   HI = (~0ull << 38) & ~(uint64_t)(SOMVR_VMA_TESTPAGE | SOMVR_VMA_WALKDIR | SOMVR_VMA_REFRESH);
    /* (43: build 27's Refresh rate row, the layer's: test_refresh27) */
    somvr_settings s;
    somvr_set_default(&s);
    s.sword_weight = 2, s.quiver = 1, s.bow_string = 1;
    /* the free bits change no row, rect, hit or click; the new bits change no Settings-page row, rect or hit */
    int same = 1, same_set = 1;
    for (int item = 0; item < SOMVR_VMI_ALL; item++) {
        int a[4], b[4], c[4];
        for (int part = SOMVR_VMP_ROW; part <= SOMVR_VMP_BUTTON; part++) {
            int ka = somvr_vm_rect_av(B19, item, part, a), kb = somvr_vm_rect_av(B19 | HI, item, part, b);
            same &= ka == kb && (!ka || !memcmp(a, b, sizeof a));
            if (somvr_vm_row_page(item) == SOMVR_VMPG_SETTINGS || item == SOMVR_VMI_PG_FIGHT) {
                int kc = somvr_vm_rect_av(B18, item, part, c);
                same_set &= ka == kc && (!ka || !memcmp(a, c, sizeof a));
            }
        }
        same &= somvr_vm_shown(B19, item) == somvr_vm_shown(B19 | HI, item) &&
                somvr_vm_enabled(&s, B19, item) == somvr_vm_enabled(&s, B19 | HI, item);
    }
    for (float v = 0.02f; v < 1.0f; v += 0.04f)
        for (float u = 0.05f; u < 1.0f; u += 0.1f)
            for (int pg = 0; pg < 4; pg++) {
                somvr_vm_hit h0 = somvr_vm_hit_test_av(B19, pg, u, v), h1 = somvr_vm_hit_test_av(B19 | HI, pg, u, v),
                             h2 = somvr_vm_hit_test_av(B18, pg, u, v);
                same &= h0.item == h1.item && h0.part == h1.part;
                if (pg != SOMVR_VMPG_FIGHT) same_set &= h0.item == h2.item && h0.part == h2.part;
            }
    somvr_settings s1 = s, s2 = s;
    same &= somvr_vm_apply(&s1, B19, 0, SOMVR_VMI_ARROWS, SOMVR_VMP_ROW) ==
                somvr_vm_apply(&s2, B19 | HI, 0, SOMVR_VMI_ARROWS, SOMVR_VMP_ROW) &&
            !memcmp(&s1, &s2, sizeof s1);
    CHECK(same && same_set, "bits 38..62 change nothing on the rows (%d); bits 32..37 only the Fight page (%d)", same,
          same_set);
    /* the Controls page: build 18's lines whatever the new bits (no new line); the Bow string row's string decides the
     * bow's lines as BOWTRIG did (the trigger: the same as build 18's) */
    const char *k, *w, *k2, *w2;
    uint64_t c18 = somvr_vm_ctl_avail(&s, B18), c19 = somvr_vm_ctl_avail(&s, B19);
    int n18 = somvr_vm_controls_av(c18, 0, &k, &w), n19 = somvr_vm_controls_av(c19, 0, &k, &w), lines_same = n18 == n19;
    for (int i = 0; i < n18 && lines_same; i++) {
        somvr_vm_controls_av(c18, i, &k, &w), somvr_vm_controls_av(c19, i, &k2, &w2);
        lines_same &= !strcmp(k, k2) && !strcmp(w, w2) && strcmp(k, "Dodge");
    }
    CHECK(lines_same, "the Controls page: build 18's %d lines with the new bits (%d)", n18, n19);
    somvr_settings g = s;
    g.bow_string = 0;
    uint64_t cg = somvr_vm_ctl_avail(&g, B19), cgq = (g.quiver = 0, somvr_vm_ctl_avail(&g, B19));
    CHECK((c19 & SOMVR_VMA_BOWTRIG) && (c19 & SOMVR_VMA_BOWRT) && !(cg & SOMVR_VMA_BOWTRIG) && (cg & SOMVR_VMA_QVGRIP) &&
              !(cg & SOMVR_VMA_BOWRT) && !(cgq & (SOMVR_VMA_BOWTRIG | SOMVR_VMA_QVGRIP | SOMVR_VMA_BOWRT)) &&
              (somvr_vm_ctl_avail(&g, B18) & SOMVR_VMA_BOWTRIG) && (somvr_vm_ctl_avail(&s, B19 | HI) & HI) == HI,
          "the bow's Controls lines follow the Bow string row (trigger: BOWRT, grip: QVGRIP); without its bit the start's");
    /* the six keys: the switches' names and words, the file round trip, the diff */
    static const struct { int k; const char *key, *env, *val; } K[] = {
        {SOMVR_SK_DODGE, "DODGE", "SOMVR_DODGE", "on"},
        {SOMVR_SK_WRAITH_STEP, "WRAITH_STEP", "SOMVR_WRAITH_STEP", "on"},
        {SOMVR_SK_STEALTH_KILL, "STEALTH_KILL", "SOMVR_STEALTH_KILL", "on"},
        {SOMVR_SK_CROWD_PACE_MS, "CROWD_PACE_MS", "SOMVR_CROWD_PACE_MS", "700"},
        {SOMVR_SK_COUNTER_ICON, "COUNTER_ICON", "SOMVR_COUNTER_ICON", "hide"},
        {SOMVR_SK_BOW_STRING, "BOW_STRING", "SOMVR_BOW_STRING", "trigger"}};
    somvr_settings d, b;
    somvr_set_default(&d);
    int keys = d.dodge == 0 && d.wstep == 0 && d.stealth == 0 && d.crowd_ms == 0 && d.counter_icon == 1 &&
               d.bow_string == 0;
    uint32_t mask = 0;
    b = d;
    for (size_t i = 0; i < sizeof K / sizeof K[0]; i++) {
        char v[32];
        keys &= !strcmp(somvr_set_key(K[i].k), K[i].key) && !strcmp(somvr_set_env(K[i].k), K[i].env) &&
                somvr_set_find(K[i].env) == K[i].k && somvr_set_put(&b, K[i].k, K[i].val);
        somvr_set_get(&b, K[i].k, v, sizeof v);
        keys &= !strcmp(v, K[i].val);
        mask |= 1u << K[i].k;
    }
    CHECK(keys && SOMVR_SK_BOW_STRING == SOMVR_SK_MOVE - 1 && b.dodge && b.wstep && b.stealth && b.crowd_ms == 700 &&
              !b.counter_icon && b.bow_string,
          "the six keys: the switches' names and words (the defaults: off, no pace, the icon shown, the grip)");
    char file[2048], note[64];
    int bad = 0;
    somvr_set_format(&b, mask, file, sizeof file);
    somvr_settings r = d;
    uint32_t m = somvr_set_parse(&r, file, &bad, note, sizeof note);
    CHECK(m == mask && !bad && somvr_set_diff(&b, &r) == 0 && somvr_set_diff(&d, &b) == mask &&
              strstr(file, "CROWD_PACE_MS=700\n") && strstr(file, "COUNTER_ICON=hide\n") && strstr(file, "BOW_STRING=trigger\n"),
          "the file round trip, the diff");
    r = d;
    CHECK(!somvr_set_put(&r, SOMVR_SK_STEALTH_KILL, "log") && !somvr_set_put(&r, SOMVR_SK_COUNTER_ICON, "on") &&
              !somvr_set_put(&r, SOMVR_SK_BOW_STRING, "squeeze") && somvr_set_put(&r, SOMVR_SK_CROWD_PACE_MS, "9999") &&
              r.crowd_ms == 3000 && somvr_set_put(&r, SOMVR_SK_CROWD_PACE_MS, " 725 ") && r.crowd_ms == 725 &&
              !somvr_set_put(&r, SOMVR_SK_CROWD_PACE_MS, "fast"),
          "put: bad words refused, the pace clamped to 0..3000 as the switch (%u)", r.crowd_ms);
    /* the rows: on the Fight page, shown and enabled with their bits only; the values; < > and a press */
    static const struct { int item; uint64_t bit; const char *label; } R[] = {
        {SOMVR_VMI_STRING, SOMVR_VMA_STRING, "Bow string"}, {SOMVR_VMI_DODGE, SOMVR_VMA_DODGE, "Dodge (A)"},
        {SOMVR_VMI_WSTEP, SOMVR_VMA_WSTEP, "Wraith step"}, {SOMVR_VMI_STEALTH, SOMVR_VMA_STEALTH, "Stealth kill"},
        {SOMVR_VMI_CROWD, SOMVR_VMA_CROWD, "Crowd pace"}, {SOMVR_VMI_CICON, SOMVR_VMA_CICON, "Counter icon"}};
    int rows = 1;
    for (size_t i = 0; i < sizeof R / sizeof R[0]; i++) {
        rows &= somvr_vm_row_page(R[i].item) == SOMVR_VMPG_FIGHT && !strcmp(somvr_vm_label(R[i].item), R[i].label) &&
                !somvr_vm_shown(B18, R[i].item) && somvr_vm_shown(R[i].bit, R[i].item) &&
                !somvr_vm_enabled(&d, NEW & ~R[i].bit, R[i].item) && somvr_vm_enabled(&d, R[i].bit, R[i].item) &&
                somvr_font_text_w(R[i].label, SOMVR_VM_SCALE) <= 256 - 20;
        if (!rows) printf("    row %d (%s)\n", R[i].item, R[i].label);
    }
    CHECK(rows, "the six rows: the Fight page's, shown and enabled with their bits only, the labels fit");
    somvr_settings t = d;
    char v[32], v2[32];
    int ok = 1;
    static const struct { int item; const char *off, *on; } T[] = {
        {SOMVR_VMI_STRING, "Grip", "Trigger"}, {SOMVR_VMI_DODGE, "Off", "On"}, {SOMVR_VMI_WSTEP, "Off", "On"},
        {SOMVR_VMI_STEALTH, "Off", "On"}};
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        somvr_vm_value(&t, T[i].item, v, sizeof v);
        ok &= !strcmp(v, T[i].off) && somvr_vm_apply(&t, NEW, 0, T[i].item, SOMVR_VMP_ROW) == SOMVR_VMR_CHANGED;
        somvr_vm_value(&t, T[i].item, v2, sizeof v2);
        ok &= !strcmp(v2, T[i].on) && somvr_vm_apply(&t, NEW, 0, T[i].item, SOMVR_VMP_LESS) == SOMVR_VMR_CHANGED &&
              somvr_vm_apply(&t, NEW, 0, T[i].item, SOMVR_VMP_MORE) == SOMVR_VMR_CHANGED &&
              somvr_vm_apply(&t, 0, 0, T[i].item, SOMVR_VMP_ROW) == SOMVR_VMR_NONE;
        if (!ok) printf("    toggle %d: %s %s\n", T[i].item, v, v2);
    }
    somvr_vm_value(&t, SOMVR_VMI_CICON, v, sizeof v);
    ok &= !strcmp(v, "Shown") && somvr_vm_apply(&t, NEW, 0, SOMVR_VMI_CICON, SOMVR_VMP_MORE) == SOMVR_VMR_CHANGED &&
          (somvr_vm_value(&t, SOMVR_VMI_CICON, v, sizeof v), !strcmp(v, "Hidden"));
    CHECK(ok && t.bow_string && t.dodge && t.wstep && t.stealth && !t.counter_icon,
          "the toggles: Grip / Trigger, Off / On, Shown / Hidden; a press, < and > flip; no bit: nothing");
    /* the crowd's pace: Off, 0.5, 0.7, 1.0, 1.5, 2.0 s, held at the ends, a press wraps; an odd file value steps from it */
    static const char *const pv[] = {"Off", "0.5 s", "0.7 s", "1.0 s", "1.5 s", "2.0 s"};
    t.crowd_ms = 0;
    int pace = 1;
    for (int i = 0; i < 6; i++) {
        somvr_vm_value(&t, SOMVR_VMI_CROWD, v, sizeof v);
        pace &= !strcmp(v, pv[i]) && somvr_font_text_w(v, SOMVR_VM_SCALE) <= 448 - 304;
        if (!pace) printf("    pace %d: '%s'\n", i, v);
        int r2 = somvr_vm_apply(&t, NEW, 0, SOMVR_VMI_CROWD, SOMVR_VMP_MORE);
        pace &= i < 5 ? r2 == SOMVR_VMR_CHANGED : r2 == SOMVR_VMR_NONE;
    }
    pace &= t.crowd_ms == 2000 && somvr_vm_apply(&t, NEW, 0, SOMVR_VMI_CROWD, SOMVR_VMP_ROW) == SOMVR_VMR_CHANGED &&
            t.crowd_ms == 0 && somvr_vm_apply(&t, NEW, 0, SOMVR_VMI_CROWD, SOMVR_VMP_LESS) == SOMVR_VMR_NONE;
    t.crowd_ms = 800;
    somvr_vm_value(&t, SOMVR_VMI_CROWD, v, sizeof v);
    pace &= !strcmp(v, "0.8 s") && somvr_vm_apply(&t, NEW, 0, SOMVR_VMI_CROWD, SOMVR_VMP_MORE) == SOMVR_VMR_CHANGED &&
            t.crowd_ms == 1000 && (t.crowd_ms = 800, somvr_vm_apply(&t, NEW, 0, SOMVR_VMI_CROWD, SOMVR_VMP_LESS)) &&
            t.crowd_ms == 700 && (t.crowd_ms = 725, somvr_vm_value(&t, SOMVR_VMI_CROWD, v, sizeof v), !strcmp(v, "0.725 s"));
    CHECK(pace, "the pace's steps, held, wrapped; from 800: up 1000, down 700; 725 shown '%s'", v);
    /* the page buttons: Fight settings > under the Settings page's rows, < Settings under the Fight page's; they open
     * their page (the layer's PAGE), every page's the other's rect is no hit */
    int rf[4], rk[4];
    somvr_vm_rect_av(B19, SOMVR_VMI_PG_FIGHT, SOMVR_VMP_BUTTON, rf), somvr_vm_rect_av(B19, SOMVR_VMI_PG_BACK, SOMVR_VMP_BUTTON, rk);
    float fu = (rf[0] + 0.5f * rf[2]) / SOMVR_VM_TEX_W, fv = (rf[1] + 0.5f * rf[3]) / SOMVR_VM_TEX_H;
    float ku = (rk[0] + 0.5f * rk[2]) / SOMVR_VM_TEX_W, kv = (rk[1] + 0.5f * rk[3]) / SOMVR_VM_TEX_H;
    somvr_vm_hit hf = somvr_vm_hit_test_av(B19, SOMVR_VMPG_SETTINGS, fu, fv),
                 hk = somvr_vm_hit_test_av(B19, SOMVR_VMPG_FIGHT, ku, kv),
                 hx = somvr_vm_hit_test_av(B19, SOMVR_VMPG_FIGHT, fu, fv),
                 hc = somvr_vm_hit_test_av(B19, SOMVR_VMPG_CONTROLS, fu, fv);
    t = d;
    CHECK(hf.item == SOMVR_VMI_PG_FIGHT && hk.item == SOMVR_VMI_PG_BACK && hx.item != SOMVR_VMI_PG_FIGHT && hc.item == -1 &&
              somvr_vm_tab_page(SOMVR_VMI_PG_FIGHT) == SOMVR_VMPG_FIGHT &&
              somvr_vm_tab_page(SOMVR_VMI_PG_BACK) == SOMVR_VMPG_SETTINGS &&
              somvr_vm_apply(&t, 0, 0, SOMVR_VMI_PG_FIGHT, SOMVR_VMP_BUTTON) == SOMVR_VMR_PAGE &&
              somvr_vm_apply(&t, 0, 0, SOMVR_VMI_PG_BACK, SOMVR_VMP_BUTTON) == SOMVR_VMR_PAGE && rf[1] > rk[1],
          "the page buttons: at %d / %d, hit on their pages only (%d %d %d %d), PAGE", rf[1], rk[1], hf.item, hk.item, hx.item,
          hc.item);
    /* every Fight row's boxes hit back on the Fight page, no Fight row is hit on the Settings page */
    int back = 1;
    for (int item = 0; item < SOMVR_VMI_ROWS; item++) {
        int b2[4];
        if (somvr_vm_row_page(item) != SOMVR_VMPG_FIGHT || !somvr_vm_rect_av(B19, item, SOMVR_VMP_MORE, b2)) continue;
        float u = (b2[0] + 0.5f * b2[2]) / SOMVR_VM_TEX_W, vv = (b2[1] + 0.5f * b2[3]) / SOMVR_VM_TEX_H;
        somvr_vm_hit h1 = somvr_vm_hit_test_av(B19, SOMVR_VMPG_FIGHT, u, vv), h2 = somvr_vm_hit_test_av(B19, SOMVR_VMPG_SETTINGS, u, vv);
        back &= h1.item == item && h1.part == SOMVR_VMP_MORE && somvr_vm_row_page(h2.item) != SOMVR_VMPG_FIGHT;
    }
    CHECK(back, "the Fight rows' > boxes hit back on the Fight page only");
    /* every Fight row has two lines of help that fit (29 cells from x 24); none elsewhere; drawn under the back button
     * while its row is hovered, above the bottom row */
    int help = 1;
    for (int item = 0; item < SOMVR_VMI_ALL; item++) {
        const char *h1, *h2;
        int has = somvr_vm_help(item, &h1, &h2);
        help &= has == (somvr_vm_row_page(item) == SOMVR_VMPG_FIGHT);
        if (has) help &= somvr_font_text_w(h1, SOMVR_VM_SCALE) <= 29 * 16 && somvr_font_text_w(h2, SOMVR_VM_SCALE) <= 29 * 16;
        if (!help) { printf("    help %d\n", item); break; }
    }
    CHECK(help, "the Fight rows' help: two lines each that fit, none elsewhere");
    /* the pictures: the Settings page the same with and without the new bits (its rows; the page button under them);
     * the Fight page differs (its six rows); the Settings tab lit on both */
    somvr_vm_view vw;
    memset(&vw, 0, sizeof vw);
    vw.set = d, vw.hover.item = -1, vw.page = SOMVR_VMPG_SETTINGS, vw.avail = B19;
    somvr_vm_draw(&F, g_px, 0, &vw);
    vw.avail = B18;
    somvr_vm_draw(&F, g_px2, 0, &vw);
    int set_same = !memcmp(g_px, g_px2, sizeof g_px);
    int tab[4];
    somvr_vm_rect(SOMVR_VMI_TAB_SET, SOMVR_VMP_BUTTON, tab);
    const uint8_t *ul = pxa(g_px, tab[0] + tab[2] / 2, tab[1] + tab[3] - 3);
    int lit_set = ul[0] == 232 && ul[1] == 192 && ul[2] == 112;
    vw.page = SOMVR_VMPG_FIGHT;
    somvr_vm_draw(&F, g_px2, 0, &vw);
    vw.avail = B19;
    somvr_vm_draw(&F, g_px, 0, &vw);
    int fight_differs = memcmp(g_px, g_px2, sizeof g_px) != 0;
    ul = pxa(g_px, tab[0] + tab[2] / 2, tab[1] + tab[3] - 3);
    int lit_fight = ul[0] == 232 && ul[1] == 192 && ul[2] == 112;
    CHECK(set_same && fight_differs && lit_set && lit_fight,
          "the pictures: Settings the same (%d), Fight differs (%d), the Settings tab lit on both (%d %d)", set_same,
          fight_differs, lit_set, lit_fight);
    /* a v6 menu's settings file (build 17b's, as its menu wrote it) read by v7: the same keys, the same values, no bad
     * line; written back with its keys: the same lines; the new keys follow their switches until changed */
    static const char v6file[] =
        "# Shadow of Mordor VR settings (SOMVR_SETTINGS), written by the in-game VR menu (SOMVR_VRMENU=1).\n"
        "# KEY=VALUE; a key here wins over its SOMVR_* switch, a key not here follows the switch.\n"
        "TURN=smooth\nTURN_DPS=90\nVIGNETTE=0.5\nSNAP_BLINK_MS=140\nHAPTICS=100\nPHASE=on\nCOMBAT_BITE=on\n";
    somvr_settings env = d;
    env.wstep = env.dodge = env.stealth = 1, env.crowd_ms = 700, env.counter_icon = 0, env.bow_string = 1; /* build 19's */
    somvr_settings u6 = env;
    m = somvr_set_parse(&u6, v6file, &bad, note, sizeof note);
    uint32_t want = 1u << SOMVR_SK_TURN | 1u << SOMVR_SK_TURN_DPS | 1u << SOMVR_SK_VIGNETTE | 1u << SOMVR_SK_SNAP_BLINK_MS |
                    1u << SOMVR_SK_HAPTICS | 1u << SOMVR_SK_PHASE | 1u << SOMVR_SK_COMBAT_BITE;
    somvr_set_format(&u6, m, file, sizeof file);
    CHECK(m == want && !bad && u6.turn == 1 && u6.turn_dps == 90 && NEAR(u6.vignette, 0.5, 1e-6) && u6.blink_ms == 140 &&
              NEAR(u6.haptics, 1.0, 1e-6) && u6.phase == 1 && u6.bite == 1 && !strcmp(file, v6file) &&
              (somvr_set_diff(&env, &u6) & ~want) == 0 && u6.wstep && u6.crowd_ms == 700 && !u6.counter_icon &&
              u6.bow_string,
          "a v6 settings file: its 7 keys and values taken, written back the same; the new keys follow the switches");
    t = u6;
    somvr_vm_apply(&t, NEW, 0, SOMVR_VMI_CROWD, SOMVR_VMP_MORE);
    uint32_t keys2 = m | somvr_set_diff(&u6, &t);
    somvr_set_format(&t, keys2, file, sizeof file);
    CHECK(keys2 == (want | 1u << SOMVR_SK_CROWD_PACE_MS) && !strncmp(file, v6file, sizeof v6file - 1) &&
              !strcmp(file + sizeof v6file - 1, "CROWD_PACE_MS=1000\n"),
          "a change on the Fight page adds its one line after the old ones: '%s'", file + sizeof v6file - 1);
}

/* ---- build 20: the panel held steady, the pointer's lock (docs/re-xr-pipeline.md 80) ------------------------------ */
#include "hand_rec20.h"
static void t20_qmul(const float a[4], const float b[4], float o[4]) {
    float x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    float z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    o[0] = x, o[1] = y, o[2] = z, o[3] = w;
}
static void t20_qaxis(int axis, float deg, float o[4]) {
    float h = 0.5f * deg * 0.017453293f;
    o[0] = o[1] = o[2] = 0, o[axis] = sinf(h), o[3] = cosf(h);
}
/* a shaking hand: per axis six sines (0.6 to 9.7 Hz), at most cm and deg (as somvr_xt_shake) */
static void t20_shake(somvr_posef *p, float cm, float deg, double t) {
    static const float hz[6] = {0.6f, 1.1f, 1.9f, 5.3f, 7.9f, 9.7f}, wt[6] = {0.25f, 0.2f, 0.15f, 0.15f, 0.15f, 0.1f};
    float o[6];
    for (int a = 0; a < 6; a++) {
        double sum = 0;
        for (int k = 0; k < 6; k++) sum += wt[k] * sin(6.2831853 * hz[k] * t + 0.7 * a + 1.3 * k);
        o[a] = (float)sum * (a < 3 ? 0.01f * cm : deg);
    }
    for (int a = 0; a < 3; a++) {
        float r[4], q[4];
        t20_qaxis(a, o[3 + a], r), t20_qmul(p->q, r, q);
        memcpy(p->q, q, sizeof q);
        p->p[a] += o[a];
    }
}
static float t20_deg(const float a[4], const float b[4]) {
    float d = fabsf(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]);
    return 2 * acosf(d < 1 ? d : 1) * 57.29578f;
}
static float t20_dist(const float a[3], const float b[3]) {
    return sqrtf((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}
/* the corners' speed (rms, m/s) of a run of poses */
typedef struct { float c[4][3]; int have; double sq; int n; float most; } t20_jit;
static void t20_jit_add(t20_jit *j, const somvr_posef *p, double dt) {
    static const float size[2] = {SOMVR_VM_W_M, SOMVR_VM_H_M};
    float c[4][3];
    somvr_vm_corners(p, size, c);
    if (j->have)
        for (int k = 0; k < 4; k++) {
            float d = t20_dist(c[k], j->c[k]);
            j->sq += 0.25 * (d / dt) * (d / dt), j->most = d > j->most ? d : j->most;
        }
    j->n += j->have;
    memcpy(j->c, c, sizeof c), j->have = 1;
}
static double t20_rms(const t20_jit *j) { return j->n ? sqrt(j->sq / j->n) : 0; }

static void test_steady20(void) {
    somvr_vm_stcfg c, d;
    somvr_vm_stcfg_default(&c);
    CHECK(NEAR(c.hold_m, 0.015, 1e-6) && NEAR(c.hold_deg, 3, 1e-6) && NEAR(c.settle_s, 0.35, 1e-6), "the defaults");
    int bad = 0;
    CHECK(somvr_vm_stcfg_parse(NULL, &d, &bad) == 0 && somvr_vm_stcfg_parse("off", &d, &bad) == 0 &&
              somvr_vm_stcfg_parse("0", &d, &bad) == 0 && !bad, "unset/off/0: off");
    CHECK(somvr_vm_stcfg_parse("on", &d, &bad) == 1 && !bad && NEAR(d.hold_m, 0.015, 1e-6), "on: the defaults");
    CHECK(somvr_vm_stcfg_parse("2,4,500", &d, &bad) == 1 && !bad && NEAR(d.hold_m, 0.02, 1e-6) && NEAR(d.hold_deg, 4, 1e-6) &&
              NEAR(d.settle_s, 0.5, 1e-6), "CM,DEG,MS");
    const char *bads[] = {"yes", "2", "2,4", "0,4,500", "2,30,500", "2,4,5000", "2,4,500x", "-1,4,5", "2,4,", "2,4,5,6"};
    for (int i = 0; i < 10; i++) {
        bad = 0;
        CHECK(somvr_vm_stcfg_parse(bads[i], &d, &bad) == 0 && bad, "'%s' is not understood: off", bads[i]);
    }
    somvr_vm_gcfg g;
    somvr_vm_gcfg_default(&g);
    somvr_vm_gin in = gin_facing(0);
    /* 1. a shaking hand held up to read (0.5 cm, 1.5 deg, 10 s at 90 Hz): build 19's panel (the hand's pose) against
     * the steady one */
    somvr_vm_steady st;
    somvr_vm_steady_reset(&st);
    t20_jit jr = {0}, js = {0};
    float most_off = 0, most_deg = 0;
    for (int f = 0; f < 900; f++) {
        double t = f / 90.0;
        somvr_posef grip = in.grip, raw, sh;
        t20_shake(&grip, 0.5f, 1.5f, t);
        somvr_vm_panel_pose(&grip, SOMVR_HAND_L, &HEAD, &g, &raw);
        somvr_vm_steady_step(&st, &c, &raw, t, &sh);
        t20_jit_add(&jr, &raw, 1 / 90.0), t20_jit_add(&js, &sh, 1 / 90.0);
        float o = t20_dist(raw.p, sh.p), a = t20_deg(raw.q, sh.q);
        most_off = o > most_off ? o : most_off, most_deg = a > most_deg ? a : most_deg;
    }
    printf("test_vrmenu: build 20 steady, a shaking hand (0.5 cm, 1.5 deg): the corners %.1f mm/s rms (build 19's: %.1f), "
           "most %.2f mm a frame (%.2f); %.2f cm, %.2f deg off the hand at most\n", 1000 * t20_rms(&js),
           1000 * t20_rms(&jr), 1000 * js.most, 1000 * jr.most, 100 * most_off, most_deg);
    CHECK(t20_rms(&jr) > 0.03, "build 19's panel shakes with the hand: %.1f mm/s", 1000 * t20_rms(&jr));
    CHECK(t20_rms(&js) < 0.1 * t20_rms(&jr) && t20_rms(&js) < 0.005,
          "the steady panel holds still: %.2f mm/s rms (build 19's %.1f)", 1000 * t20_rms(&js), 1000 * t20_rms(&jr));
    CHECK(most_off <= c.hold_m + 1e-4f && most_deg <= c.hold_deg + 0.05f, "never more than the hold off the hand: %.2f cm "
          "%.2f deg", 100 * most_off, most_deg);
    /* 2. a real move: 30 cm to the right in 0.3 s - at once, never more than the hold behind; then it settles */
    somvr_posef base = in.grip, raw, sh;
    float lag = 0;
    for (int f = 0; f <= 270; f++) { /* 3 s: 1 s still, the move, then 1.7 s still */
        double t = 10 + f / 90.0;
        float k = f < 90 ? 0 : f > 117 ? 1 : (float)(f - 90) / 27.0f;
        somvr_posef grip = base;
        grip.p[0] += 0.3f * k;
        somvr_vm_panel_pose(&grip, SOMVR_HAND_L, &HEAD, &g, &raw);
        somvr_vm_steady_step(&st, &c, &raw, t, &sh);
        float o = t20_dist(raw.p, sh.p);
        if (f >= 90 && f <= 117) lag = o > lag ? o : lag;
    }
    CHECK(lag <= c.hold_m + 1e-4f && lag > 0.5f * c.hold_m, "the move: followed at once, %.2f cm behind at most", 100 * lag);
    CHECK(t20_dist(raw.p, sh.p) < 0.006f, "1.7 s after the move it has eased to the hand: %.2f cm off",
          100 * t20_dist(raw.p, sh.p));
    /* 3. a turn: 60 deg about the up in 0.5 s - never more than the hold behind */
    float turn = 0;
    for (int f = 0; f <= 90; f++) {
        double t = 20 + f / 90.0;
        float r[4], k = f > 45 ? 1 : (float)f / 45.0f;
        somvr_posef grip = base;
        t20_qaxis(1, 60 * k, r), t20_qmul(r, base.q, grip.q);
        somvr_vm_panel_pose(&grip, SOMVR_HAND_L, NULL, &g, &raw);
        somvr_vm_steady_step(&st, &c, &raw, t, &sh);
        float a = t20_deg(raw.q, sh.q);
        turn = a > turn ? a : turn;
    }
    CHECK(turn <= c.hold_deg + 0.05f && turn > 0.5f * c.hold_deg, "the turn: %.2f deg behind at most", turn);
    /* 4. a gap over 0.25 s: at the hand's pose again; a bad pose: the last one stays */
    raw.p[1] += 0.01f;
    somvr_vm_steady_step(&st, &c, &raw, 21.5, &sh);
    CHECK(t20_dist(raw.p, sh.p) == 0 && t20_deg(raw.q, sh.q) < 1e-3f && st.restarts == 3, "after a gap (the turn began after one too): the hand's pose "
          "(restarts %llu)", (unsigned long long)st.restarts);
    somvr_posef nan_p = raw, kept;
    nan_p.p[0] = NAN;
    somvr_vm_steady_step(&st, &c, &nan_p, 21.51, &kept);
    CHECK(kept.p[0] == sh.p[0] && isfinite(kept.p[0]), "a NaN pose: the last one");
    /* 5. the recorded hand (hand_rec20.h): the panel's hand turned by a real hand's 3 s held still */
    somvr_vm_steady_reset(&st);
    t20_jit rr = {0}, rs = {0};
    int nrec = (int)(sizeof g_hand_rec20 / sizeof g_hand_rec20[0]);
    const float *q0 = &g_hand_rec20[0][1];
    float q0c[4] = {-q0[0], -q0[1], -q0[2], q0[3]};
    for (int i = 0; i < nrec; i++) {
        float dq[4], qi[4] = {g_hand_rec20[i][1], g_hand_rec20[i][2], g_hand_rec20[i][3], g_hand_rec20[i][4]};
        t20_qmul(q0c, qi, dq); /* the turn since the first sample, in the grip's own frame */
        somvr_posef grip = base;
        t20_qmul(base.q, dq, grip.q);
        double t = 30 + g_hand_rec20[i][0], dt = i ? g_hand_rec20[i][0] - g_hand_rec20[i - 1][0] : 1 / 90.0;
        somvr_vm_panel_pose(&grip, SOMVR_HAND_L, &HEAD, &g, &raw);
        somvr_vm_steady_step(&st, &c, &raw, t, &sh);
        t20_jit_add(&rr, &raw, dt), t20_jit_add(&rs, &sh, dt);
    }
    printf("test_vrmenu: build 20 steady, the recorded hand (%d samples): the corners %.1f mm/s rms (build 19's: %.1f), "
           "most %.2f mm a frame (%.2f)\n", nrec, 1000 * t20_rms(&rs), 1000 * t20_rms(&rr), 1000 * rs.most, 1000 * rr.most);
    CHECK(t20_rms(&rs) < 0.5 * t20_rms(&rr), "the recorded hand: the steady panel moves less than half as much: %.1f mm/s "
          "(build 19's %.1f)", 1000 * t20_rms(&rs), 1000 * t20_rms(&rr));
}

/* the uv of the texel point (x, y) */
static void t20_uv(float x, float y, float uv[2]) { uv[0] = x / SOMVR_VM_TEX_W, uv[1] = y / SOMVR_VM_TEX_H; }
static int t20_is(somvr_vm_hit h, int item, int part) { return h.item == item && h.part == part; }
static void test_snap20(void) {
    somvr_vm_snapcfg c;
    somvr_vm_snapcfg_default(&c);
    CHECK(NEAR(c.hold_deg, 1.0, 1e-6) && NEAR(c.magnet_deg, 2.0, 1e-6) && NEAR(c.min_m, 0.003, 1e-6), "the defaults");
    somvr_settings set;
    somvr_set_default(&set);
    somvr_vm_snap sn;
    somvr_vm_snap_reset(&sn);
    CHECK(sn.lock.item == -1 && sn.raw.item == -1, "reset: no lock");
    const float D = 0.3f, W = SOMVR_VM_W_M; /* the pointing hand 30 cm from the panel: the hold 13.4 texels, the magnet 26.8 */
    int r0[4], r1[4], mo[4];
    CHECK(somvr_vm_rect_av(0, SOMVR_VMI_TURN, SOMVR_VMP_ROW, r0) && somvr_vm_rect_av(0, SOMVR_VMI_SNAP, SOMVR_VMP_ROW, r1) &&
              somvr_vm_rect_av(0, SOMVR_VMI_TURN, SOMVR_VMP_MORE, mo), "the rects");
    float uv[2], dot[2], cy = r0[1] + 0.5f * r0[3];
    /* 1. the point on the Turn row's label: it locks */
    t20_uv(100, cy, uv);
    somvr_vm_hit h = somvr_vm_snap_step(&sn, &c, &set, 0, SOMVR_VMPG_SETTINGS, 0, uv, D, W, dot);
    CHECK(t20_is(h, SOMVR_VMI_TURN, SOMVR_VMP_ROW) && sn.changes == 1, "the Turn row locks (%d/%d)", h.item, h.part);
    /* 2. a shake across the row's edge (+-12 texels about its bottom edge, 90 frames): the bare hit flips, the lock holds */
    uint32_t c0 = sn.changes, r0c = sn.raw_changes;
    int held = 1, inside = 1;
    for (int f = 0; f < 90; f++) {
        float y = (float)(r0[1] + r0[3]) + 12.0f * sinf(f * 0.9f);
        t20_uv(100 + 5 * cosf(f * 1.7f), y, uv);
        h = somvr_vm_snap_step(&sn, &c, &set, 0, SOMVR_VMPG_SETTINGS, 0, uv, D, W, dot);
        held &= t20_is(h, SOMVR_VMI_TURN, SOMVR_VMP_ROW);
        float dy = dot[1] * SOMVR_VM_TEX_H;
        inside &= dy >= r0[1] && dy <= r0[1] + r0[3];
    }
    CHECK(held && sn.changes == c0, "a shake across the edge: the lock holds (%u changes)", sn.changes - c0);
    CHECK(sn.raw_changes - r0c >= 20, "... where the bare hit (build 19's hover) flips %u times", sn.raw_changes - r0c);
    CHECK(inside, "the dot stays on the locked row");
    /* 3. the next row's middle: it locks (a real move) */
    t20_uv(100, r1[1] + 0.5f * r1[3], uv);
    h = somvr_vm_snap_step(&sn, &c, &set, 0, SOMVR_VMPG_SETTINGS, 0, uv, D, W, dot);
    CHECK(t20_is(h, SOMVR_VMI_SNAP, SOMVR_VMP_ROW), "the next row's middle: it locks (%d/%d)", h.item, h.part);
    /* 4. the row and its > box (the small target first): 3 texels into the box it takes over from the row at once; 10
     * texels back out onto the row the box holds; the box's middle */
    t20_uv(100, cy, uv), somvr_vm_snap_step(&sn, &c, &set, 0, SOMVR_VMPG_SETTINGS, 0, uv, D, W, dot);
    t20_uv(mo[0] + 3, mo[1] + 0.5f * mo[3], uv);
    h = somvr_vm_snap_step(&sn, &c, &set, 0, SOMVR_VMPG_SETTINGS, 0, uv, D, W, dot);
    CHECK(t20_is(h, SOMVR_VMI_TURN, SOMVR_VMP_MORE), "3 texels into the > box: the box at once (%d/%d)", h.item, h.part);
    t20_uv(mo[0] - 10, mo[1] + 0.5f * mo[3], uv);
    h = somvr_vm_snap_step(&sn, &c, &set, 0, SOMVR_VMPG_SETTINGS, 0, uv, D, W, dot);
    CHECK(t20_is(h, SOMVR_VMI_TURN, SOMVR_VMP_MORE), "10 texels back on the row: the box holds (%d/%d)", h.item, h.part);
    t20_uv(mo[0] - 20, mo[1] + 0.5f * mo[3], uv);
    h = somvr_vm_snap_step(&sn, &c, &set, 0, SOMVR_VMPG_SETTINGS, 0, uv, D, W, dot);
    CHECK(t20_is(h, SOMVR_VMI_TURN, SOMVR_VMP_ROW), "20 texels back on the row: the row (%d/%d)", h.item, h.part);
    t20_uv(mo[0] + 0.5f * mo[2], mo[1] + 0.5f * mo[3], uv);
    h = somvr_vm_snap_step(&sn, &c, &set, 0, SOMVR_VMPG_SETTINGS, 0, uv, D, W, dot);
    CHECK(t20_is(h, SOMVR_VMI_TURN, SOMVR_VMP_MORE), "the box's middle: the > locks (%d/%d)", h.item, h.part);
    /* ... and holds through a shake of 10 texels about it */
    held = 1;
    for (int f = 0; f < 60; f++) {
        t20_uv(mo[0] + 0.5f * mo[2] + 10 * sinf(f * 1.1f), mo[1] + 0.5f * mo[3] + 10 * cosf(f * 0.7f), uv);
        h = somvr_vm_snap_step(&sn, &c, &set, 0, SOMVR_VMPG_SETTINGS, 0, uv, D, W, dot);
        held &= t20_is(h, SOMVR_VMI_TURN, SOMVR_VMP_MORE);
    }
    CHECK(held, "the > box holds through a 10-texel shake");
    /* 5. a gap between the bottom buttons (Recentre | Report, 12 texels apart): the nearest locks, the dot on it */
    int rb[4], pb[4];
    somvr_vm_rect_av(0, SOMVR_VMI_RECENTER, SOMVR_VMP_BUTTON, rb), somvr_vm_rect_av(0, SOMVR_VMI_REPORT, SOMVR_VMP_BUTTON, pb);
    somvr_vm_snap_reset(&sn);
    t20_uv(rb[0] + rb[2] + 3, rb[1] + 0.5f * rb[3], uv);
    CHECK(somvr_vm_hit_test_av(0, SOMVR_VMPG_SETTINGS, uv[0], uv[1]).item < 0, "the gap: no bare hit");
    h = somvr_vm_snap_step(&sn, &c, &set, 0, SOMVR_VMPG_SETTINGS, 0, uv, D, W, dot);
    float dx = dot[0] * SOMVR_VM_TEX_W;
    CHECK(t20_is(h, SOMVR_VMI_RECENTER, SOMVR_VMP_BUTTON) && dx >= rb[0] && dx <= rb[0] + rb[2],
          "the gap: the nearest button (Recentre) locks, the dot on it (%d/%d, x %.1f)", h.item, h.part, dx);
    /* 6. far from any part (the Controls page's text, the magnet 26.8 texels): nothing */
    somvr_vm_snap_reset(&sn);
    t20_uv(256, 400, uv);
    h = somvr_vm_snap_step(&sn, &c, &set, 0, SOMVR_VMPG_CONTROLS, 0, uv, D, W, dot);
    CHECK(h.item < 0 && NEAR(dot[0], uv[0], 1e-6), "the Controls page's text: no lock, the dot at the point");
    /* 7. a disabled part never pulls: beside the right edge of a disabled row only an enabled one locks */
    int dis = -1;
    for (int i = 0; i < SOMVR_VMI_ROWS && dis < 0; i++)
        if (somvr_vm_row_page(i) == SOMVR_VMPG_SETTINGS && somvr_vm_rect_av(0, i, SOMVR_VMP_ROW, r1) &&
            !somvr_vm_enabled(&set, 0, i))
            dis = i;
    if (dis >= 0) {
        somvr_vm_snap_reset(&sn);
        t20_uv(r1[0] + r1[2] + 2, r1[1] + 0.5f * r1[3], uv);
        h = somvr_vm_snap_step(&sn, &c, &set, 0, SOMVR_VMPG_SETTINGS, 0, uv, D, W, dot);
        CHECK(h.item != dis && (h.item < 0 || somvr_vm_enabled(&set, 0, h.item)),
              "beside the disabled %s: it does not pull (%d)", somvr_vm_label(dis), h.item);
    }
    CHECK(dis >= 0, "a disabled row on the default settings (a smooth turn's Snap angle)");
    /* 8. the card: its close box alone */
    int cb[4];
    somvr_vm_rect_av(0, SOMVR_VMI_CLOSE, SOMVR_VMP_BUTTON, cb);
    somvr_vm_snap_reset(&sn);
    t20_uv(100, cy, uv);
    h = somvr_vm_snap_step(&sn, &c, &set, 0, SOMVR_VMPG_SETTINGS, 1, uv, D, W * 2, dot);
    CHECK(h.item < 0, "the card: a row is nothing (%d)", h.item);
    t20_uv(cb[0] - 6, cb[1] + 0.5f * cb[3], uv);
    h = somvr_vm_snap_step(&sn, &c, &set, 0, SOMVR_VMPG_SETTINGS, 1, uv, D, W * 2, dot);
    CHECK(t20_is(h, SOMVR_VMI_CLOSE, SOMVR_VMP_BUTTON), "the card: beside its X, the X locks (%d)", h.item);
    /* 9. NaN: nothing */
    uv[0] = NAN, uv[1] = 0.5f;
    h = somvr_vm_snap_step(&sn, &c, &set, 0, SOMVR_VMPG_SETTINGS, 0, uv, D, W, dot);
    CHECK(h.item < 0, "a NaN point: no lock");
    /* 10. the picture: the locked part outlined (the ray's colour), not without the lock */
    static somvr_font font;
    static uint8_t a[SOMVR_VM_TEX_W * SOMVR_VM_TEX_H * 4], b[SOMVR_VM_TEX_W * SOMVR_VM_TEX_H * 4];
    if (somvr_font_init(&font) == 0) {
        somvr_vm_view v;
        memset(&v, 0, sizeof v);
        v.set = set, v.page = SOMVR_VMPG_SETTINGS, v.hover = (somvr_vm_hit){SOMVR_VMI_TURN, SOMVR_VMP_MORE};
        somvr_vm_draw(&font, a, 0, &v);
        v.lock = 1;
        somvr_vm_draw(&font, b, 0, &v);
        const uint8_t *p = b + 4 * ((size_t)(mo[1] + 0) * SOMVR_VM_TEX_W + (size_t)(mo[0] + mo[2] / 2));
        const uint8_t *q = a + 4 * ((size_t)(mo[1] + 0) * SOMVR_VM_TEX_W + (size_t)(mo[0] + mo[2] / 2));
        CHECK(p[0] == 150 && p[1] == 220 && p[2] == 255 && !(q[0] == 150 && q[1] == 220),
              "the locked > box outlined (%u %u %u; without the lock %u %u %u)", p[0], p[1], p[2], q[0], q[1], q[2]);
        size_t diff = 0;
        for (size_t i = 0; i < sizeof a; i++) diff += a[i] != b[i];
        CHECK(diff > 0 && diff < 4 * 600, "only the outline differs (%zu bytes)", diff);
    }
}

/* build 21 (re-xr-pipeline.md 82): the Test tab and page - a page saying what to test in the new build (the 20c
 * session's ask). The text: the title, items (wrapped by words, their next lines under their text), plain
 * text, comments and quotes; none = the title and a note; long = pages, an item whole on one. The layout with
 * SOMVR_VMA_TESTPAGE: four tabs across (no X on the pages; the card's X as build 20), the Test page's Prev / Next; the
 * picture: the title on every page, the lines inside the panel, Prev dim on the first page */
static void test_test21(void) {
    static somvr_vm_text t;
    const uint64_t T = SOMVR_VMA_TESTPAGE;
    static const char txt[] = "# a comment: not shown\n"
                              "Build 21 - what to test\n"
                              "\n"
                              "- The bow: the sight is small and white, blue on an orc.\n"
                              "  This line continues the bow item and wraps at a word boundary.\n"
                              "- Voice notes: talk as long as you like\n"
                              "# another comment\n"
                              "* a star item with \xe2\x80\x9cquotes\xe2\x80\x9d and an \xe2\x80\x93 dash\r\n"
                              "\n"
                              "Plain text here.\n"
                              "- Averyveryveryveryveryverylongwordthatcannotfit at all";
    int pages = somvr_vm_text_parse(&t, txt, sizeof txt - 1);
    int ok = t.have && t.n_title == 1 && !strcmp(t.line[0], "Build 21 - what to test") && pages == 1 && !t.truncated;
    int items = 0, more = 0, texts = 0, fit = 1, comment = 0, quotes = 0, cut = 0;
    for (int k = 0; k < t.n; k++) {
        size_t n = strlen(t.line[k]);
        items += t.kind[k] == SOMVR_VMT_ITEM, more += t.kind[k] == SOMVR_VMT_MORE, texts += t.kind[k] == SOMVR_VMT_TEXT;
        fit &= n <= (size_t)((t.kind[k] == SOMVR_VMT_ITEM || t.kind[k] == SOMVR_VMT_MORE) ? SOMVR_VM_TEST_COLS - 2
                                                                                          : SOMVR_VM_TEST_COLS) &&
               n > 0 && t.line[k][0] != ' ' && t.line[k][n - 1] != ' ';
        comment |= strstr(t.line[k], "comment") != NULL;
        quotes |= strstr(t.line[k], "\"quotes\"") != NULL;
        cut |= !strcmp(t.line[k], "Averyveryveryveryveryverylo");
    }
    CHECK(ok && items == 4 && more >= 3 && texts == 1 && fit && !comment && quotes && cut,
          "the text: the title, %d items, %d next lines, %d text, all within the panel's columns (%d), no comment (%d), "
          "the quotes plain (%d), a long word cut (%d)", items, more, texts, fit, comment, quotes, cut);
    CHECK(!strcmp(t.line[1], "The bow: the sight is small") && t.kind[1] == SOMVR_VMT_ITEM && t.kind[2] == SOMVR_VMT_MORE &&
              !strncmp(t.line[2], "and white, blue on an orc.", 26),
          "an item wraps by words, its next line under it: '%s' / '%s'", t.line[1], t.line[2]);
    /* nothing: the title and a note, never a failure */
    somvr_vm_text_parse(&t, NULL, 0);
    CHECK(!t.have && t.n == 2 && t.pages == 1 && !strcmp(t.line[0], "What to test") && t.kind[1] == SOMVR_VMT_NOTE,
          "no file: '%s' + '%s'", t.line[0], t.line[1]);
    somvr_vm_text_parse(&t, "# only comments\n\n   \n", 22);
    CHECK(!t.have && t.n == 2, "only comments: the note");
    /* long: pages; every line between the title and the Prev / Next row; an item whole on one page */
    static char big[SOMVR_VM_TEST_BYTES + 200];
    size_t bn = (size_t)snprintf(big, sizeof big, "Build 99 - what to test\n");
    for (int i = 0; i < 30; i++)
        bn += (size_t)snprintf(big + bn, sizeof big - bn, "- Item %d: something to try in the headset, said in a few "
                                                          "words.\n", i);
    pages = somvr_vm_text_parse(&t, big, bn);
    int inside = 1, whole = 1, order = 1, last_page = 0;
    for (int k = t.n_title; k < t.n; k++) {
        inside &= t.y[k] >= SOMVR_VM_TEST_Y0 + 34 * t.n_title && t.y[k] + SOMVR_VM_TEST_DY <= SOMVR_VM_TEST_NAV_Y - 8;
        if (t.kind[k] == SOMVR_VMT_MORE) whole &= t.page[k] == t.page[k - 1];
        order &= t.page[k] >= last_page && t.page[k] <= last_page + 1, last_page = t.page[k];
    }
    CHECK(pages >= 3 && t.pages == pages && last_page == pages - 1 && inside && whole && order && !t.truncated,
          "30 items: %d pages, the lines inside (%d), items whole (%d), in order (%d)", pages, inside, whole, order);
    CHECK(somvr_vm_test_turn(&t, 0, -1) == 0 && somvr_vm_test_turn(&t, 0, 1) == 1 &&
              somvr_vm_test_turn(&t, pages - 1, 1) == pages - 1 && somvr_vm_test_turn(NULL, 3, 1) == 0,
          "Prev / Next: clamped to the pages");
    while (bn < SOMVR_VM_TEST_BYTES + 100) big[bn++] = 'x';
    somvr_vm_text_parse(&t, big, bn);
    CHECK(t.truncated && t.n <= SOMVR_VM_TEST_LINES && t.kind[t.n - 1] == SOMVR_VMT_NOTE, "more than the room: cut, the "
                                                                                         "last line says so (%d lines)", t.n);
    /* the layout: four tabs across with the bit, no X on the pages; build 20's without it */
    int r[4][4], x[4], ok4 = 1;
    static const int tabs[4] = {SOMVR_VMI_TAB_SET, SOMVR_VMI_TAB, SOMVR_VMI_TAB_CTL, SOMVR_VMI_TAB_TEST};
    for (int i = 0; i < 4; i++) {
        ok4 &= somvr_vm_rect_av(T, tabs[i], SOMVR_VMP_BUTTON, r[i]) && r[i][1] + r[i][3] <= 58 &&
               r[i][2] >= somvr_font_text_w(somvr_vm_label(tabs[i]), SOMVR_VM_SCALE) + 6 &&
               (i == 0 ? r[i][0] >= 8 : r[i][0] > r[i - 1][0] + r[i - 1][2]) && r[i][0] + r[i][2] <= 502;
        x[i] = r[i][0];
    }
    CHECK(ok4 && !somvr_vm_rect_av(T, SOMVR_VMI_CLOSE, SOMVR_VMP_BUTTON, r[0]) &&
              somvr_vm_rect_av(0, SOMVR_VMI_CLOSE, SOMVR_VMP_BUTTON, r[1]) &&
              !somvr_vm_rect_av(0, SOMVR_VMI_TAB_TEST, SOMVR_VMP_BUTTON, r[2]) && !somvr_vm_enabled(NULL, 0, SOMVR_VMI_TAB_TEST),
          "four tabs across (%d %d %d %d), no X with them; without the bit build 20's X, no Test tab", x[0], x[1], x[2], x[3]);
    somvr_settings s;
    somvr_set_default(&s);
    int all = 1, only = 1;
    for (int pg = 0; pg <= SOMVR_VMPG_TEST; pg++)
        for (int i = 0; i < 4; i++) {
            somvr_vm_rect_av(T, tabs[i], SOMVR_VMP_BUTTON, r[0]);
            somvr_vm_hit h = somvr_vm_hit_test_av(T | SOMVR_VMA_PRACTICE, pg, (r[0][0] + 0.5f * r[0][2]) / SOMVR_VM_TEX_W,
                                                  (r[0][1] + 0.5f * r[0][3]) / SOMVR_VM_TEX_H);
            all &= h.item == tabs[i];
        }
    for (int yy = 0; yy < SOMVR_VM_TEX_H; yy += 4)
        for (int xx = 0; xx < SOMVR_VM_TEX_W; xx += 4) {
            somvr_vm_hit h = somvr_vm_hit_test_av(T, SOMVR_VMPG_TEST, (xx + 0.5f) / SOMVR_VM_TEX_W, (yy + 0.5f) / SOMVR_VM_TEX_H);
            if (h.item >= 0 && somvr_vm_tab_page(h.item) < 0 && h.item != SOMVR_VMI_TEST_PREV && h.item != SOMVR_VMI_TEST_NEXT)
                only = 0;
        }
    CHECK(all && only, "every tab on every page (%d); the Test page: only the tabs, Prev and Next (%d)", all, only);
    CHECK(somvr_vm_tab_page(SOMVR_VMI_TAB_TEST) == SOMVR_VMPG_TEST &&
              somvr_vm_apply(&s, T, 0, SOMVR_VMI_TAB_TEST, SOMVR_VMP_BUTTON) == SOMVR_VMR_PAGE &&
              somvr_vm_apply(&s, T, 0, SOMVR_VMI_TEST_NEXT, SOMVR_VMP_BUTTON) == SOMVR_VMR_SCROLL &&
              somvr_vm_apply(&s, T, 0, SOMVR_VMI_TEST_PREV, SOMVR_VMP_BUTTON) == SOMVR_VMR_SCROLL &&
              somvr_vm_apply(&s, 0, 0, SOMVR_VMI_TEST_NEXT, SOMVR_VMP_BUTTON) == SOMVR_VMR_NONE,
          "the Test tab opens its page; Prev / Next scroll (only with the bit)");
    /* the picture: page 1 and 2 of the long text - the title on both, Prev dim on 1, the lines differ, nothing past
     * the panel's border; the card with the bit: build 20's (its X drawn) */
    somvr_font f;
    somvr_font_init(&f);
    somvr_vm_text_parse(&t, big, 600);
    static uint8_t p1[SOMVR_VM_TEX_W * SOMVR_VM_TEX_H * 4], p2[SOMVR_VM_TEX_W * SOMVR_VM_TEX_H * 4];
    somvr_vm_view v;
    memset(&v, 0, sizeof v);
    v.set = s, v.avail = T, v.hover.item = -1, v.page = SOMVR_VMPG_TEST, v.test = &t;
    somvr_vm_draw(&f, p1, 0, &v);
    v.test_page = 1;
    somvr_vm_draw(&f, p2, 0, &v);
    int title1 = 0, title2 = 0, diff_body = 0, prev_dim = 1, next_lit = 0;
    for (int yy = SOMVR_VM_TEST_Y0; yy < SOMVR_VM_TEST_Y0 + 32; yy++)
        for (int xx = 20; xx < 400; xx++) {
            const uint8_t *a = p1 + 4 * (yy * SOMVR_VM_TEX_W + xx), *b = p2 + 4 * (yy * SOMVR_VM_TEX_W + xx);
            title1 += a[0] == 232 && a[1] == 192, title2 += b[0] == 232 && b[1] == 192;
        }
    for (int yy = 110; yy < SOMVR_VM_TEST_NAV_Y - 8; yy++)
        for (int xx = 20; xx < 490; xx++) diff_body += memcmp(p1 + 4 * (yy * SOMVR_VM_TEX_W + xx), p2 + 4 * (yy * SOMVR_VM_TEX_W + xx), 4) != 0;
    somvr_vm_rect_av(T, SOMVR_VMI_TEST_PREV, SOMVR_VMP_BUTTON, r[0]), somvr_vm_rect_av(T, SOMVR_VMI_TEST_NEXT, SOMVR_VMP_BUTTON, r[1]);
    const uint8_t *pp = p1 + 4 * ((r[0][1] + 2) * SOMVR_VM_TEX_W + r[0][0] + 2), *np = p1 + 4 * ((r[1][1] + 2) * SOMVR_VM_TEX_W + r[1][0] + 2);
    prev_dim = pp[0] == 32 && pp[1] == 37, next_lit = np[0] == 52 && np[1] == 61;
    CHECK(title1 > 100 && title1 == title2 && diff_body > 500 && prev_dim && next_lit,
          "the picture: the title on pages 1 and 2 (%d %d texels), their lines differ (%d), Prev dim (%d), Next lit (%d)",
          title1, title2, diff_body, prev_dim, next_lit);
    v.card = 1, v.page = SOMVR_VMPG_CONTROLS;
    somvr_vm_draw(&f, p2, 0, &v);
    v.avail = 0;
    somvr_vm_draw(&f, p1, 0, &v);
    CHECK(!memcmp(p1, p2, sizeof p1), "the card with the bit: build 20's card (its X)");
}

/* ---- build 27 (re-xr-pipeline.md 112): Walk toward (HL:A's "Continuous (Head)" / "(Hand)") - the MOVE key as
 * SOMVR_MOVE, the row under the turn's rows with SOMVR_VMA_WALKDIR only, live; Pointer hand says what the row does ---- */
static void test_walk_toward(void) {
    somvr_settings s, b;
    char v[32], file[1024], note[64];
    int bad = 0;
    somvr_set_default(&s);
    CHECK(s.move == 0 && offsetof(somvr_settings, move) == 83 && sizeof(somvr_settings) == 96 &&
              !strcmp(somvr_set_key(SOMVR_SK_MOVE), "MOVE") && !strcmp(somvr_set_env(SOMVR_SK_MOVE), "SOMVR_MOVE") &&
              somvr_set_find("SOMVR_MOVE") == SOMVR_SK_MOVE && SOMVR_SK_MOVE == 31 && SOMVR_SK_N == 33,
          "MOVE: the default head, the old pad's byte 83, the key and its switch; the 32nd key");
    CHECK(somvr_set_put(&s, SOMVR_SK_MOVE, " hand ") && s.move == 1 && (somvr_set_get(&s, SOMVR_SK_MOVE, v, sizeof v), !strcmp(v, "hand")) &&
              somvr_set_put(&s, SOMVR_SK_MOVE, "head") && s.move == 0 && !somvr_set_put(&s, SOMVR_SK_MOVE, "1") &&
              !somvr_set_put(&s, SOMVR_SK_MOVE, "left") && s.move == 0,
          "MOVE takes SOMVR_MOVE's words (head|hand) only");
    /* the 64-bit masks: the 32nd key's bit round trips with the others (32 bits held 31) */
    s.move = 1, s.snap_deg = 45;
    somvr_set_format(&s, 1ull << SOMVR_SK_MOVE | 1ull << SOMVR_SK_SNAP_DEG, file, sizeof file);
    somvr_set_default(&b);
    uint64_t m = somvr_set_parse(&b, file, &bad, note, sizeof note);
    CHECK(strstr(file, "SNAP_DEG=45\nMOVE=hand\n") && m == (1ull << SOMVR_SK_MOVE | 1ull << SOMVR_SK_SNAP_DEG) && !bad &&
              b.move == 1 && somvr_set_diff(&s, &b) == 0 && (m >> 31 & 1) &&
              somvr_set_diff(&s, &(somvr_settings){0}) >> SOMVR_SK_MOVE & 1,
          "the file: MOVE=hand read back, bit 31 in the mask and the diff (mask %#llx)", (unsigned long long)m);
    /* the user's own file (the eight keys it pins) reads as before: MOVE not in it follows the switch */
    const char *user = "TURN=smooth\nTURN_DPS=90\nVIGNETTE=0.3\nSNAP_BLINK_MS=140\nVIEW=first\nHAPTICS=100\nPHASE=off\n"
                       "COMBAT_BITE=on\n";
    somvr_set_default(&b);
    m = somvr_set_parse(&b, user, &bad, note, sizeof note);
    CHECK(!bad && m == (1ull << SOMVR_SK_TURN | 1ull << SOMVR_SK_TURN_DPS | 1ull << SOMVR_SK_VIGNETTE |
                        1ull << SOMVR_SK_SNAP_BLINK_MS | 1ull << SOMVR_SK_VIEW | 1ull << SOMVR_SK_HAPTICS |
                        1ull << SOMVR_SK_PHASE | 1ull << SOMVR_SK_COMBAT_BITE) &&
              b.turn == 1 && b.turn_dps == 90 && b.bite == 1 && b.move == 0,
          "the user's eight pinned keys read as before; MOVE follows its switch");
    /* the row: shown and enabled with SOMVR_VMA_WALKDIR only; Head / Hand; a click flips it */
    const uint64_t A = SOMVR_VMA_COMFORT | SOMVR_VMA_HUD | SOMVR_VMA_VIEW | SOMVR_VMA_MUSIC | SOMVR_VMA_SIZE |
                       SOMVR_VMA_CROUCH | SOMVR_VMA_FLING | SOMVR_VMA_SURFACE | SOMVR_VMA_SWORD; /* the headset's Settings rows */
    somvr_set_default(&s);
    int rb[4], rm[4], rs[4], rr[4], r0[4], rq[4];
    somvr_vm_value(&s, SOMVR_VMI_MOVE, v, sizeof v);
    int shown = somvr_vm_shown(A | SOMVR_VMA_WALKDIR, SOMVR_VMI_MOVE) && !somvr_vm_shown(A, SOMVR_VMI_MOVE) &&
                somvr_vm_enabled(&s, A | SOMVR_VMA_WALKDIR, SOMVR_VMI_MOVE) && !somvr_vm_enabled(&s, A, SOMVR_VMI_MOVE) &&
                !somvr_vm_rect_av(A, SOMVR_VMI_MOVE, SOMVR_VMP_ROW, r0);
    CHECK(shown && !strcmp(v, "Head") && !strcmp(somvr_vm_label(SOMVR_VMI_MOVE), "Walk toward") &&
              somvr_vm_row_page(SOMVR_VMI_MOVE) == SOMVR_VMPG_SETTINGS &&
              somvr_vm_apply(&s, A | SOMVR_VMA_WALKDIR, 0, SOMVR_VMI_MOVE, SOMVR_VMP_MORE) == SOMVR_VMR_CHANGED && s.move == 1 &&
              (somvr_vm_value(&s, SOMVR_VMI_MOVE, v, sizeof v), !strcmp(v, "Hand")) &&
              somvr_vm_apply(&s, A | SOMVR_VMA_WALKDIR, 0, SOMVR_VMI_MOVE, SOMVR_VMP_ROW) == SOMVR_VMR_CHANGED && s.move == 0 &&
              somvr_vm_apply(&s, A, 0, SOMVR_VMI_MOVE, SOMVR_VMP_ROW) == SOMVR_VMR_NONE && s.move == 0,
          "Walk toward: with its bit only, Head / Hand, a click flips it; without the bit nothing");
    /* its place: right under Snap blink, Seated under it; the bottom buttons still below the last row */
    const uint64_t AW = A | SOMVR_VMA_WALKDIR;
    CHECK(somvr_vm_rect_av(AW, SOMVR_VMI_BLINK, SOMVR_VMP_ROW, rb) && somvr_vm_rect_av(AW, SOMVR_VMI_MOVE, SOMVR_VMP_ROW, rm) &&
              somvr_vm_rect_av(AW, SOMVR_VMI_SEATED, SOMVR_VMP_ROW, rs) &&
              somvr_vm_rect_av(AW, SOMVR_VMI_PG_FIGHT, SOMVR_VMP_BUTTON, rq) &&
              somvr_vm_rect_av(AW, SOMVR_VMI_RECENTER, SOMVR_VMP_BUTTON, rr) && rm[1] == rb[1] + rb[3] &&
              rs[1] == rm[1] + rm[3] && rq[1] + rq[3] <= rr[1] && rm[3] >= 28,
          "under Snap blink (%d) at %d, Seated after (%d); rows %d px; the page button ends %d, the buttons at %d", rb[1],
          rm[1], rs[1], rm[3], rq[1] + rq[3], rr[1]);
    somvr_vm_hit h = somvr_vm_hit_test_av(AW, SOMVR_VMPG_SETTINGS, (rm[0] + 40.5f) / SOMVR_VM_TEX_W,
                                          (rm[1] + 0.5f * rm[3]) / SOMVR_VM_TEX_H);
    CHECK(h.item == SOMVR_VMI_MOVE && h.part == SOMVR_VMP_ROW, "its row hits (%d %d)", h.item, h.part);
    /* Pointer hand: the row moves the panel's wrist and the pointing hand only (the controls do not swap) */
    CHECK(!strcmp(somvr_vm_label(SOMVR_VMI_HAND), "Pointer hand"), "Main hand is Pointer hand (what it does)");
}


/* ---- build 27 (re-xr-pipeline.md 119, som-b27-perf2): the Refresh rate row (XR_REFRESH, SOMVR_XR_REFRESH; the layer's
 * SOMVR_VMA_REFRESH): the key, the file, the steps 72 / 80 / 90 / 120 Hz, the row last on the Settings page ---------- */
static void test_refresh27(void) {
    somvr_settings s, b;
    char v[32], file[1024], note[64];
    int bad = 0;
    somvr_set_default(&s);
    CHECK(s.refresh == 0 && offsetof(somvr_settings, refresh) == 84 && sizeof(somvr_settings) == 96 &&
              !strcmp(somvr_set_key(SOMVR_SK_XR_REFRESH), "XR_REFRESH") &&
              !strcmp(somvr_set_env(SOMVR_SK_XR_REFRESH), "SOMVR_XR_REFRESH") &&
              somvr_set_find("SOMVR_XR_REFRESH") == SOMVR_SK_XR_REFRESH && SOMVR_SK_XR_REFRESH == 32 &&
              SOMVR_VMA_REFRESH == (1ull << 43),
          "XR_REFRESH: the default 0 (nothing asked), byte 84, the key and its switch; the 33rd key; bit 43");
    CHECK(somvr_set_put(&s, SOMVR_SK_XR_REFRESH, " 80 ") && s.refresh == 80 &&
              (somvr_set_get(&s, SOMVR_SK_XR_REFRESH, v, sizeof v), !strcmp(v, "80")) &&
              somvr_set_put(&s, SOMVR_SK_XR_REFRESH, "off") && s.refresh == 0 &&
              somvr_set_put(&s, SOMVR_SK_XR_REFRESH, "45") && s.refresh == 60 &&
              somvr_set_put(&s, SOMVR_SK_XR_REFRESH, "200") && s.refresh == 144 &&
              somvr_set_put(&s, SOMVR_SK_XR_REFRESH, "0") && s.refresh == 0 &&
              !somvr_set_put(&s, SOMVR_SK_XR_REFRESH, "fast") && s.refresh == 0,
          "XR_REFRESH's values: Hz 60..144, off / 0 = nothing asked, words refused");
    s.refresh = 90;
    somvr_set_format(&s, 1ull << SOMVR_SK_XR_REFRESH, file, sizeof file);
    somvr_set_default(&b);
    uint64_t m = somvr_set_parse(&b, file, &bad, note, sizeof note);
    CHECK(strstr(file, "\nXR_REFRESH=90\n") && m == 1ull << SOMVR_SK_XR_REFRESH && !bad && b.refresh == 90 &&
              somvr_set_diff(&s, &b) == 0,
          "the file: XR_REFRESH=90 round trip");
    /* the row: shown and enabled with SOMVR_VMA_REFRESH only; 72 / 80 / 90 / 120 Hz, held at the ends by < >, the row's
     * click wraps; from the headset's own (0): > 72, < 120 */
    const uint64_t A = SOMVR_VMA_COMFORT | SOMVR_VMA_HUD | SOMVR_VMA_VIEW | SOMVR_VMA_MUSIC | SOMVR_VMA_SIZE |
                       SOMVR_VMA_CROUCH | SOMVR_VMA_FLING | SOMVR_VMA_SURFACE | SOMVR_VMA_SWORD | SOMVR_VMA_WALKDIR,
                   AR = A | SOMVR_VMA_REFRESH;
    int r0[4];
    somvr_set_default(&s);
    s.refresh = 80;
    somvr_vm_value(&s, SOMVR_VMI_REFRESH, v, sizeof v);
    int shown = somvr_vm_shown(AR, SOMVR_VMI_REFRESH) && !somvr_vm_shown(A, SOMVR_VMI_REFRESH) &&
                somvr_vm_enabled(&s, AR, SOMVR_VMI_REFRESH) && !somvr_vm_enabled(&s, A, SOMVR_VMI_REFRESH) &&
                !somvr_vm_rect_av(A, SOMVR_VMI_REFRESH, SOMVR_VMP_ROW, r0);
    CHECK(shown && !strcmp(v, "80 Hz") && !strcmp(somvr_vm_label(SOMVR_VMI_REFRESH), "Refresh rate") &&
              somvr_vm_row_page(SOMVR_VMI_REFRESH) == SOMVR_VMPG_SETTINGS,
          "Refresh rate: with its bit only, \"80 Hz\" (%s)", v);
    int seq = somvr_vm_apply(&s, AR, 0, SOMVR_VMI_REFRESH, SOMVR_VMP_MORE) == SOMVR_VMR_CHANGED && s.refresh == 90 &&
              somvr_vm_apply(&s, AR, 0, SOMVR_VMI_REFRESH, SOMVR_VMP_MORE) == SOMVR_VMR_CHANGED && s.refresh == 120 &&
              somvr_vm_apply(&s, AR, 0, SOMVR_VMI_REFRESH, SOMVR_VMP_MORE) == SOMVR_VMR_NONE && s.refresh == 120 &&
              somvr_vm_apply(&s, AR, 0, SOMVR_VMI_REFRESH, SOMVR_VMP_ROW) == SOMVR_VMR_CHANGED && s.refresh == 72 &&
              somvr_vm_apply(&s, AR, 0, SOMVR_VMI_REFRESH, SOMVR_VMP_LESS) == SOMVR_VMR_NONE && s.refresh == 72 &&
              somvr_vm_apply(&s, AR, 0, SOMVR_VMI_REFRESH, SOMVR_VMP_MORE) == SOMVR_VMR_CHANGED && s.refresh == 80 &&
              somvr_vm_apply(&s, A, 0, SOMVR_VMI_REFRESH, SOMVR_VMP_MORE) == SOMVR_VMR_NONE && s.refresh == 80;
    s.refresh = 0;
    somvr_vm_value(&s, SOMVR_VMI_REFRESH, v, sizeof v);
    seq &= !strcmp(v, "Headset");
    seq &= somvr_vm_apply(&s, AR, 0, SOMVR_VMI_REFRESH, SOMVR_VMP_MORE) == SOMVR_VMR_CHANGED && s.refresh == 72;
    s.refresh = 0;
    seq &= somvr_vm_apply(&s, AR, 0, SOMVR_VMI_REFRESH, SOMVR_VMP_LESS) == SOMVR_VMR_CHANGED && s.refresh == 120;
    CHECK(seq, "the steps: 80 > 90 > 120 > held, the row wraps to 72, < held at 72; from Headset > 72, < 120");
    /* its place: the Settings page's last row (after Hands at walls), the page button and the bottom buttons below */
    int rf[4], rl[4], rq[4], rr[4];
    CHECK(somvr_vm_rect_av(AR, SOMVR_VMI_SURFACE, SOMVR_VMP_ROW, rl) &&
              somvr_vm_rect_av(AR, SOMVR_VMI_REFRESH, SOMVR_VMP_ROW, rf) &&
              somvr_vm_rect_av(AR, SOMVR_VMI_PG_FIGHT, SOMVR_VMP_BUTTON, rq) &&
              somvr_vm_rect_av(AR, SOMVR_VMI_RECENTER, SOMVR_VMP_BUTTON, rr) && rf[1] == rl[1] + rl[3] &&
              rq[1] >= rf[1] + rf[3] && rq[1] + rq[3] <= rr[1] && rf[3] >= 28,
          "after Hands at walls (%d) at %d, %d px; the page button %d..%d; the buttons at %d", rl[1], rf[1], rf[3], rq[1],
          rq[1] + rq[3], rr[1]);
    somvr_vm_hit h = somvr_vm_hit_test_av(AR, SOMVR_VMPG_SETTINGS, (rf[0] + 40.5f) / SOMVR_VM_TEX_W,
                                          (rf[1] + 0.5f * rf[3]) / SOMVR_VM_TEX_H);
    CHECK(h.item == SOMVR_VMI_REFRESH && h.part == SOMVR_VMP_ROW, "its row hits (%d %d)", h.item, h.part);
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
    test_weight_row();
    test_bite_row();
    test_session_buttons();
    test_controls();
    test_walk_toward(); /* build 27 */
    test_refresh27(); /* build 27 (perf2) */
    test_music_rows();
    test_build17();
    test_far_orc();
    test_build19();
    test_build22_captain();
    test_fight19();
    test_steady20();
    test_snap20();
    test_test21();
    printf("test_vrmenu: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
