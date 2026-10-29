/* somvr_vrmenu.c - see somvr_vrmenu.h */
#include "somvr_vrmenu.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ==== 2. the settings ============================================================================================ */
void somvr_set_default(somvr_settings *s) {
    *s = (somvr_settings){.turn = 0, .snap_deg = 30, .turn_dps = 120, .vignette = 0, .blink_ms = 0, .seated = 0,
                          .height_cm = 0, .hud = 1, .hud_dist_m = 1.5f, .hand = 0, .view = 0, .haptics = 1.0f,
                          .step = 0, .dash = 0, .phase = 0, .sword_deg = 0, .music = 1.0f, .size = 1.0f,
                          /* build 19: the switches' own defaults (off, the icon shown, no pace, the string on the grip) */
                          .counter_icon = 1};
}

static const char *const g_keys[SOMVR_SK_N] = {"TURN", "SNAP_DEG", "TURN_DPS", "VIGNETTE", "SNAP_BLINK_MS", "SEATED",
                                               "HEIGHT_CM", "HUD", "HUD_DIST_M", "HAND", "VIEW", "HAPTICS",
                                               "STEP", "DASH", "PHASE", "SWORD_ANGLE", "MUSIC", "PLAYER_SIZE", "CROUCH",
                                               "CLIMB_FLING", "HANDS_SURFACE", "SWORD_WEIGHT", "SWORD_WEIGHT_HAND",
                                               "BOW_QUIVER", "COMBAT_BITE",
                                               /* build 19 (re-xr-pipeline.md 73) */
                                               "DODGE", "WRAITH_STEP", "STEALTH_KILL", "CROWD_PACE_MS", "COUNTER_ICON",
                                               "BOW_STRING"};
static const char *const g_envs[SOMVR_SK_N] = {"SOMVR_TURN", "SOMVR_SNAP_DEG", "SOMVR_TURN_DPS", "SOMVR_VIGNETTE",
                                               "SOMVR_SNAP_BLINK_MS", "SOMVR_SEATED", "SOMVR_HEIGHT_CM", NULL, NULL, NULL,
                                               NULL, "SOMVR_HAPTICS", "SOMVR_STEP", "SOMVR_DASH", "SOMVR_PHASE",
                                               "SOMVR_SWORD_ANGLE", "SOMVR_MUSIC",
                                               "SOMVR_PLAYER_SIZE", "SOMVR_CROUCH",
                                               "SOMVR_CLIMB_FLING", "SOMVR_HANDS_SURFACE", "SOMVR_SWORD_WEIGHT",
                                               "SOMVR_SWORD_WEIGHT_HAND", "SOMVR_BOW_QUIVER",
                                               "SOMVR_COMBAT_BITE", "SOMVR_DODGE", "SOMVR_WRAITH_STEP",
                                               "SOMVR_STEALTH_KILL", "SOMVR_CROWD_PACE_MS", "SOMVR_COUNTER_ICON",
                                               "SOMVR_BOW_STRING"};
/* SOMVR_DASH / SOMVR_PHASE's words (somvr_step.h: SOMVR_ST_DASH_* / _PHASE_*) */
static const char *const g_dash_w[] = {"off", "on", "blink"}, *const g_phase_w[] = {"off", "on", "reface", "blink"};
static const char *const g_crouch_w[] = {"button", "duck"}; /* build 13: SOMVR_CROUCH (somvr_crouch.h) */
/* build 16: SOMVR_SWORD_WEIGHT's words (somvr_smooth.h SWG_*; on = light) */
static const char *const g_weight_w[] = {"off", "light", "heavy", "on"};
/* build 17: SOMVR_SWORD_WEIGHT_HAND's words (somvr_smooth_game.c) */
static const char *const g_whand_w[] = {"fixed", "follow"};
/* build 19: SOMVR_COUNTER_ICON's words (0 hidden, 1 shown: the game's), SOMVR_BOW_STRING's (somvr.c g_bo_string_w) */
static const char *const g_cicon_w[] = {"hide", "game"}, *const g_string_w[] = {"grip", "trigger"};

const char *somvr_set_key(int k) { return k >= 0 && k < SOMVR_SK_N ? g_keys[k] : NULL; }
const char *somvr_set_env(int k) { return k >= 0 && k < SOMVR_SK_N ? g_envs[k] : NULL; }
int somvr_set_find(const char *key) {
    if (!strncmp(key, "SOMVR_", 6)) key += 6; /* the switch's own name is taken too */
    for (int k = 0; k < SOMVR_SK_N; k++)
        if (!strcmp(key, g_keys[k])) return k;
    return -1;
}

static int is_blank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

int somvr_set_num(const char *v, double *out) {
    while (is_blank(*v)) v++;
    int neg = *v == '-';
    if (*v == '-' || *v == '+') v++;
    double x = 0, scale = 1;
    int digits = 0;
    for (; *v >= '0' && *v <= '9'; v++, digits++) x = x * 10 + (*v - '0');
    if (*v == '.')
        for (v++; *v >= '0' && *v <= '9'; v++, digits++) scale /= 10, x += (*v - '0') * scale;
    while (is_blank(*v)) v++;
    if (!digits || *v || digits > 15) return 0;
    *out = neg ? -x : x;
    return 1;
}

/* v (blanks around it ignored) is one of the words: its index, else -1 */
static int word_of(const char *v, const char *const *w, int n) {
    while (is_blank(*v)) v++;
    size_t len = strlen(v);
    while (len && is_blank(v[len - 1])) len--;
    for (int i = 0; i < n; i++)
        if (strlen(w[i]) == len && !strncmp(v, w[i], len)) return i;
    return -1;
}
static float clampf(double v, double lo, double hi) { return (float)(v < lo ? lo : v > hi ? hi : v); }

int somvr_set_put(somvr_settings *s, int k, const char *v) {
    static const char *const turn_w[] = {"snap", "smooth"}, *const onoff_w[] = {"0", "1", "off", "on"},
                             *const hand_w[] = {"right", "left"}, *const view_w[] = {"first", "third"};
    double d = 0;
    int w;
    switch (k) {
    case SOMVR_SK_TURN: return (w = word_of(v, turn_w, 2)) < 0 ? 0 : (s->turn = (uint32_t)w, 1);
    case SOMVR_SK_SEATED: return (w = word_of(v, onoff_w, 4)) < 0 ? 0 : (s->seated = (uint32_t)(w & 1), 1);
    case SOMVR_SK_CROUCH: return (w = word_of(v, g_crouch_w, 2)) < 0 ? 0 : (s->crouch = (uint32_t)w, 1);
    case SOMVR_SK_HUD: return (w = word_of(v, onoff_w, 4)) < 0 ? 0 : (s->hud = (uint32_t)(w & 1), 1);
    case SOMVR_SK_HAND: return (w = word_of(v, hand_w, 2)) < 0 ? 0 : (s->hand = (uint32_t)w, 1);
    case SOMVR_SK_VIEW: return (w = word_of(v, view_w, 2)) < 0 ? 0 : (s->view = (uint32_t)w, 1);
    case SOMVR_SK_DASH: return (w = word_of(v, g_dash_w, 3)) < 0 ? 0 : (s->dash = (uint32_t)w, 1);
    case SOMVR_SK_PHASE: return (w = word_of(v, g_phase_w, 4)) < 0 ? 0 : (s->phase = (uint32_t)w, 1);
    case SOMVR_SK_CLIMB_FLING: return (w = word_of(v, onoff_w, 4)) < 0 ? 0 : (s->fling = (uint8_t)(w & 1), 1);
    case SOMVR_SK_HANDS_SURFACE: return (w = word_of(v, onoff_w, 4)) < 0 ? 0 : (s->surface = (uint8_t)(w & 1), 1);
    case SOMVR_SK_SWORD_WEIGHT:
        return (w = word_of(v, g_weight_w, 4)) < 0 ? 0 : (s->sword_weight = (uint8_t)(w == 3 ? 1 : w), 1);
    case SOMVR_SK_SWORD_WEIGHT_HAND: return (w = word_of(v, g_whand_w, 2)) < 0 ? 0 : (s->sword_hand = (uint8_t)w, 1);
    case SOMVR_SK_BOW_QUIVER: return (w = word_of(v, onoff_w, 4)) < 0 ? 0 : (s->quiver = (uint8_t)(w & 1), 1);
    case SOMVR_SK_COMBAT_BITE: return (w = word_of(v, onoff_w, 4)) < 0 ? 0 : (s->bite = (uint8_t)(w & 1), 1);
    /* build 19: the Fight page's rows (the switches' words) */
    case SOMVR_SK_DODGE: return (w = word_of(v, onoff_w, 4)) < 0 ? 0 : (s->dodge = (uint8_t)(w & 1), 1);
    case SOMVR_SK_WRAITH_STEP: return (w = word_of(v, onoff_w, 4)) < 0 ? 0 : (s->wstep = (uint8_t)(w & 1), 1);
    case SOMVR_SK_STEALTH_KILL: return (w = word_of(v, onoff_w, 4)) < 0 ? 0 : (s->stealth = (uint8_t)(w & 1), 1);
    case SOMVR_SK_COUNTER_ICON: return (w = word_of(v, g_cicon_w, 2)) < 0 ? 0 : (s->counter_icon = (uint8_t)w, 1);
    case SOMVR_SK_BOW_STRING: return (w = word_of(v, g_string_w, 2)) < 0 ? 0 : (s->bow_string = (uint8_t)w, 1);
    default: break;
    }
    if (!somvr_set_num(v, &d)) return 0;
    switch (k) {
    case SOMVR_SK_SNAP_DEG: s->snap_deg = clampf(d, 1, 180); return 1;
    case SOMVR_SK_TURN_DPS: s->turn_dps = clampf(d, 10, 360); return 1;
    case SOMVR_SK_VIGNETTE: s->vignette = clampf(d, 0, 1); return 1;
    case SOMVR_SK_SNAP_BLINK_MS: s->blink_ms = (uint32_t)lrint(clampf(d, 0, 1000)); return 1;
    case SOMVR_SK_HEIGHT_CM: s->height_cm = d <= 0 ? 0.0f : clampf(d, 50, 250); return 1;
    case SOMVR_SK_HUD_DIST_M: s->hud_dist_m = clampf(d, 0.3, 10); return 1;
    case SOMVR_SK_HAPTICS: s->haptics = (float)(clampf(d, 0, 150) / 100.0); return 1;
    case SOMVR_SK_STEP: s->step = clampf(d, 0, 1); return 1;
    case SOMVR_SK_SWORD_ANGLE: s->sword_deg = clampf(d, -90, 90); return 1; /* as SOMVR_SWORD_ANGLE */
    case SOMVR_SK_MUSIC: s->music = clampf(d, 0, 1); return 1;
    case SOMVR_SK_PLAYER_SIZE: s->size = (float)(clampf(d, 100, 140) / 100.0); return 1; /* % (somvr_size.h) */
    case SOMVR_SK_CROWD_PACE_MS: s->crowd_ms = (uint16_t)lrint(clampf(d, 0, 3000)); return 1; /* as the switch */
    default: return 0;
    }
}

/* a number with at most 3 decimals, trailing zeros dropped, '.' always (no locale) */
static void fmt_num(double v, char *buf, size_t cap) {
    long long m = llround(fabs(v) * 1000);
    long long ip = m / 1000, fp = m % 1000;
    int n = snprintf(buf, cap, "%s%lld", v < 0 && m ? "-" : "", ip);
    if (fp && n > 0 && (size_t)n + 5 <= cap) {
        char d[5] = {'.', (char)('0' + fp / 100), (char)('0' + fp / 10 % 10), (char)('0' + fp % 10), 0};
        for (int i = 3; i > 0 && d[i] == '0'; i--) d[i] = 0;
        memcpy(buf + n, d, 5);
    }
}

void somvr_set_get(const somvr_settings *s, int k, char *buf, size_t cap) {
    if (!cap) return;
    buf[0] = 0;
    switch (k) {
    case SOMVR_SK_TURN: snprintf(buf, cap, "%s", s->turn ? "smooth" : "snap"); break;
    case SOMVR_SK_SNAP_DEG: fmt_num(s->snap_deg, buf, cap); break;
    case SOMVR_SK_TURN_DPS: fmt_num(s->turn_dps, buf, cap); break;
    case SOMVR_SK_VIGNETTE: fmt_num(s->vignette, buf, cap); break;
    case SOMVR_SK_SNAP_BLINK_MS: snprintf(buf, cap, "%u", s->blink_ms); break;
    case SOMVR_SK_SEATED: snprintf(buf, cap, "%u", s->seated ? 1u : 0u); break;
    case SOMVR_SK_CROUCH: snprintf(buf, cap, "%s", g_crouch_w[s->crouch ? 1 : 0]); break;
    case SOMVR_SK_HEIGHT_CM: fmt_num(s->height_cm, buf, cap); break;
    case SOMVR_SK_HUD: snprintf(buf, cap, "%s", s->hud ? "on" : "off"); break;
    case SOMVR_SK_HUD_DIST_M: fmt_num(s->hud_dist_m, buf, cap); break;
    case SOMVR_SK_HAND: snprintf(buf, cap, "%s", s->hand ? "left" : "right"); break;
    case SOMVR_SK_VIEW: snprintf(buf, cap, "%s", s->view ? "third" : "first"); break;
    case SOMVR_SK_HAPTICS: fmt_num(s->haptics * 100.0, buf, cap); break;
    case SOMVR_SK_STEP: fmt_num(s->step, buf, cap); break;
    case SOMVR_SK_DASH: snprintf(buf, cap, "%s", g_dash_w[s->dash < 3 ? s->dash : 0]); break;
    case SOMVR_SK_PHASE: snprintf(buf, cap, "%s", g_phase_w[s->phase < 4 ? s->phase : 0]); break;
    case SOMVR_SK_SWORD_ANGLE: fmt_num(s->sword_deg, buf, cap); break;
    case SOMVR_SK_MUSIC: fmt_num(s->music, buf, cap); break;
    case SOMVR_SK_PLAYER_SIZE: fmt_num(s->size * 100.0, buf, cap); break;
    case SOMVR_SK_CLIMB_FLING: snprintf(buf, cap, "%s", s->fling ? "on" : "off"); break; /* the switches' words */
    case SOMVR_SK_HANDS_SURFACE: snprintf(buf, cap, "%s", s->surface ? "on" : "off"); break;
    case SOMVR_SK_SWORD_WEIGHT: snprintf(buf, cap, "%s", g_weight_w[s->sword_weight < 3 ? s->sword_weight : 0]); break;
    case SOMVR_SK_SWORD_WEIGHT_HAND: snprintf(buf, cap, "%s", g_whand_w[s->sword_hand != 0]); break; /* build 17 */
    case SOMVR_SK_BOW_QUIVER: snprintf(buf, cap, "%s", s->quiver ? "on" : "off"); break;
    case SOMVR_SK_COMBAT_BITE: snprintf(buf, cap, "%s", s->bite ? "on" : "off"); break; /* build 17, the bite */
    case SOMVR_SK_DODGE: snprintf(buf, cap, "%s", s->dodge ? "on" : "off"); break; /* build 19 */
    case SOMVR_SK_WRAITH_STEP: snprintf(buf, cap, "%s", s->wstep ? "on" : "off"); break;
    case SOMVR_SK_STEALTH_KILL: snprintf(buf, cap, "%s", s->stealth ? "on" : "off"); break;
    case SOMVR_SK_CROWD_PACE_MS: snprintf(buf, cap, "%u", (unsigned)s->crowd_ms); break;
    case SOMVR_SK_COUNTER_ICON: snprintf(buf, cap, "%s", g_cicon_w[s->counter_icon != 0]); break;
    case SOMVR_SK_BOW_STRING: snprintf(buf, cap, "%s", g_string_w[s->bow_string != 0]); break;
    default: break;
    }
}

uint32_t somvr_set_parse(somvr_settings *s, const char *text, int *bad, char *note, size_t cap) {
    uint32_t mask = 0;
    int nbad = 0, line = 0;
    if (cap) note[0] = 0;
    for (const char *p = text; p && *p;) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        char l[512];
        line++;
        if (n >= sizeof l) n = sizeof l - 1; /* an overlong line: its start (it will not parse) */
        memcpy(l, p, n), l[n] = 0;
        p = e ? e + 1 : NULL;
        char *h = strchr(l, '#');
        if (h) *h = 0;
        char *a = l, *z = l + strlen(l);
        while (is_blank(*a)) a++;
        while (z > a && is_blank(z[-1])) *--z = 0;
        if (!*a) continue;
        const char *why = NULL;
        char *eq = strchr(a, '=');
        int k = -1;
        if (!eq) {
            why = "no '='";
        } else {
            char *kz = eq;
            while (kz > a && is_blank(kz[-1])) kz--;
            *kz = 0;
            char *v = eq + 1;
            while (is_blank(*v)) v++;
            size_t vl = strlen(v);
            if (vl >= 2 && ((v[0] == '"' && v[vl - 1] == '"') || (v[0] == '\'' && v[vl - 1] == '\''))) v[vl - 1] = 0, v++;
            if ((k = somvr_set_find(a)) < 0) why = "unknown key";
            else if (!somvr_set_put(s, k, v)) why = "a value the key does not take";
        }
        if (why) {
            if (!nbad && cap) snprintf(note, cap, "line %d: %s", line, why);
            nbad++;
        } else {
            mask |= 1u << k;
        }
    }
    if (bad) *bad = nbad;
    return mask;
}

size_t somvr_set_format(const somvr_settings *s, uint32_t mask, char *buf, size_t cap) {
    size_t n = (size_t)snprintf(buf, cap,
                                "# Shadow of Mordor VR settings (SOMVR_SETTINGS), written by the in-game VR menu "
                                "(SOMVR_VRMENU=1).\n# KEY=VALUE; a key here wins over its SOMVR_* switch, a key not "
                                "here follows the switch.\n");
    for (int k = 0; k < SOMVR_SK_N; k++) {
        if (!(mask >> k & 1)) continue;
        char v[32];
        somvr_set_get(s, k, v, sizeof v);
        n += (size_t)snprintf(n < cap ? buf + n : NULL, n < cap ? cap - n : 0, "%s=%s\n", g_keys[k], v);
    }
    return n;
}

uint32_t somvr_set_diff(const somvr_settings *a, const somvr_settings *b) {
    uint32_t d = 0;
    for (int k = 0; k < SOMVR_SK_N; k++) {
        char x[32], y[32];
        somvr_set_get(a, k, x, sizeof x), somvr_set_get(b, k, y, sizeof y);
        if (strcmp(x, y)) d |= 1u << k;
    }
    return d;
}

/* ==== 3. the menu: items and values ============================================================================== */
static const char *const g_labels[SOMVR_VMI_ALL] = {"Turn",     "Snap angle", "Turn speed",   "Vignette",
                                                    "Snap blink", "Seated",   "Seat height",  "HUD",
                                                    "HUD distance", "Main hand", "View",      "Haptics",
                                                    "Assisted step", "Dash",   "Wraith phase",
                                                    "Sword angle", /* v4 */
                                                    "Sword weight", /* build 16 */
                                                    "Sword hand", "Blade bite", "Arrows", /* build 17 */
                                                    "Music", /* v5 */
                                                    "Player size",
                                                    "Crouch", /* build 13 */
                                                    "Climb fling", "Hands at walls",
                                                    /* build 19: the Fight page's new rows */
                                                    "Bow string", "Dodge (A)", "Wraith step", "Stealth kill",
                                                    "Crowd pace", "Counter icon",
                                                    "Recentre view", "X",
                                                    /* v3: the tab, the practice page */
                                                    "Practice", "Go to practice area", "Back to where I was",
                                                    "Orc type", "Spawn an orc", "Remove the orc",
                                                    /* the settings page's bottom row (somvr_session.h) */
                                                    "Report", "Quit game",
                                                    /* build 6: the other two tabs */
                                                    "Settings", "Controls",
                                                    /* build 17: the practice page's refills */
                                                    "Refill arrows + focus", "Refill health",
                                                    /* build 18 */
                                                    "Spawn far orc (unaware)",
                                                    /* build 19: beside Spawn an orc */
                                                    "Spawn group",
                                                    /* build 19: the two page buttons (their arrows drawn too) */
                                                    "Fight settings", "Settings",
                                                    /* build 21: the Test tab, the Test page's buttons */
                                                    "Test", "Prev", "Next"};
const char *somvr_vm_label(int item) { return item >= 0 && item < SOMVR_VMI_ALL ? g_labels[item] : ""; }
static const char *const g_orc_labels[3] = {"Infantry", "Shield", "Berserker"}; /* = somvr_pr_orc_label */

int somvr_vm_enabled(const somvr_settings *s, uint64_t avail, int item) {
    switch (item) {
    case SOMVR_VMI_SNAP: return s->turn == 0;
    case SOMVR_VMI_SPEED: return s->turn == 1;
    case SOMVR_VMI_VIGNETTE:
    case SOMVR_VMI_BLINK: return (avail & SOMVR_VMA_COMFORT) != 0;
    case SOMVR_VMI_HEIGHT: return s->seated && (avail & SOMVR_VMA_FLOOR);
    case SOMVR_VMI_HUD: return (avail & SOMVR_VMA_HUD) != 0;
    case SOMVR_VMI_HUD_DIST: return (avail & SOMVR_VMA_HUD) && s->hud;
    case SOMVR_VMI_VIEW: return (avail & SOMVR_VMA_VIEW) != 0;
    case SOMVR_VMI_STEP:
    case SOMVR_VMI_DASH:
    case SOMVR_VMI_PHASE: return (avail & SOMVR_VMA_MOVES) != 0;
    case SOMVR_VMI_SWORD: return (avail & SOMVR_VMA_SWORD) != 0;
    case SOMVR_VMI_WEIGHT: return (avail & SOMVR_VMA_WEIGHT) != 0;
    case SOMVR_VMI_SWHAND: return (avail & SOMVR_VMA_SWHAND) && s->sword_weight; /* the weight Off: nothing to follow */
    case SOMVR_VMI_BITE: return (avail & SOMVR_VMA_BITE) != 0;
    case SOMVR_VMI_ARROWS: return (avail & SOMVR_VMA_ARROWS) != 0;
    case SOMVR_VMI_MUSIC: return (avail & SOMVR_VMA_MUSIC) != 0;
    case SOMVR_VMI_SIZE: return (avail & SOMVR_VMA_SIZE) != 0;
    case SOMVR_VMI_CROUCH: return (avail & SOMVR_VMA_CROUCH) && !s->seated; /* seated play: the button */
    case SOMVR_VMI_FLING: return (avail & SOMVR_VMA_FLING) != 0;
    case SOMVR_VMI_SURFACE: return (avail & SOMVR_VMA_SURFACE) != 0;
    case SOMVR_VMI_STRING: return (avail & SOMVR_VMA_STRING) != 0; /* build 19: the Fight page's new rows */
    case SOMVR_VMI_DODGE: return (avail & SOMVR_VMA_DODGE) != 0;
    case SOMVR_VMI_WSTEP: return (avail & SOMVR_VMA_WSTEP) != 0;
    case SOMVR_VMI_STEALTH: return (avail & SOMVR_VMA_STEALTH) != 0;
    case SOMVR_VMI_CROWD: return (avail & SOMVR_VMA_CROWD) != 0;
    case SOMVR_VMI_CICON: return (avail & SOMVR_VMA_CICON) != 0;
    case SOMVR_VMI_PG_FIGHT: /* build 19: the page buttons (the Fight page always has the Sword angle row) */
    case SOMVR_VMI_PG_BACK:
    case SOMVR_VMI_TURN:
    case SOMVR_VMI_SEATED:
    case SOMVR_VMI_HAND:
    case SOMVR_VMI_HAPTICS:
    case SOMVR_VMI_RECENTER:
    case SOMVR_VMI_CLOSE:
    case SOMVR_VMI_TAB_SET:
    case SOMVR_VMI_TAB_CTL: return 1;
    case SOMVR_VMI_TAB_TEST: /* build 21 */
    case SOMVR_VMI_TEST_PREV:
    case SOMVR_VMI_TEST_NEXT: return (avail & SOMVR_VMA_TESTPAGE) != 0;
    case SOMVR_VMI_TAB:
    case SOMVR_VMI_PR_ORC: return (avail & SOMVR_VMA_PRACTICE) != 0;
    case SOMVR_VMI_PR_GO: return (avail & SOMVR_VMA_PRACTICE) && !(avail & (SOMVR_VMA_PR_HERE | SOMVR_VMA_PR_BUSY));
    case SOMVR_VMI_PR_BACK:
        return (avail & SOMVR_VMA_PRACTICE) && (avail & SOMVR_VMA_PR_BACK) && !(avail & SOMVR_VMA_PR_BUSY);
    case SOMVR_VMI_PR_SPAWN:
    case SOMVR_VMI_PR_ARROWS: /* build 17: the refills, in the area only (as the spawn) */
    case SOMVR_VMI_PR_HEALTH:
    case SOMVR_VMI_PR_FAR: /* build 18 */
    case SOMVR_VMI_PR_GROUP: /* build 19: the group, as the spawn */
        return (avail & SOMVR_VMA_PRACTICE) && (avail & SOMVR_VMA_PR_HERE) && !(avail & SOMVR_VMA_PR_BUSY);
    case SOMVR_VMI_PR_REMOVE:
        return (avail & SOMVR_VMA_PRACTICE) && (avail & SOMVR_VMA_PR_ORC) && !(avail & SOMVR_VMA_PR_BUSY);
    case SOMVR_VMI_REPORT: return (avail & SOMVR_VMA_MARK) != 0;
    case SOMVR_VMI_QUIT: return (avail & SOMVR_VMA_QUIT) != 0;
    default: return 0;
    }
}

int somvr_vm_tab_page(int item) {
    return item == SOMVR_VMI_TAB_SET    ? SOMVR_VMPG_SETTINGS
           : item == SOMVR_VMI_TAB      ? SOMVR_VMPG_PRACTICE
           : item == SOMVR_VMI_TAB_CTL  ? SOMVR_VMPG_CONTROLS
           : item == SOMVR_VMI_PG_FIGHT ? SOMVR_VMPG_FIGHT    /* build 19: the page buttons */
           : item == SOMVR_VMI_PG_BACK  ? SOMVR_VMPG_SETTINGS
           : item == SOMVR_VMI_TAB_TEST ? SOMVR_VMPG_TEST /* build 21 */
                                        : -1;
}
/* build 19: the Fight page's rows say what they do (two dim lines under the page's rows while one is hovered: HL:A's
 * options explain the one pointed at); 29 cells of 16 px from x 24 */
static const struct { int item; const char *l1, *l2; } g_fight_help[] = {
    {SOMVR_VMI_SWORD, "The blade's angle in the fist", "(+ tips it up, - down)"},
    {SOMVR_VMI_WEIGHT, "How far the blade lags your", "hand in a fast swing"},
    {SOMVR_VMI_SWHAND, "The drawn hands turn with the", "weighted blade, or stay put"},
    {SOMVR_VMI_BITE, "A full cut stops in the body", "for a moment"},
    {SOMVR_VMI_ARROWS, "Quiver: reach behind your", "shoulder. Auto: on the string"},
    {SOMVR_VMI_STRING, "The button that holds the", "bow's string"},
    {SOMVR_VMI_DODGE, "A in a fight: a quick 1.5 m", "dash out of the blow"},
    {SOMVR_VMI_WSTEP, "Right stick forward aims a", "short blink; let go to go"},
    {SOMVR_VMI_STEALTH, "Behind an orc that has not", "seen you, the blade kills it"},
    {SOMVR_VMI_CROWD, "The least time between two", "orcs' blows at you"},
    {SOMVR_VMI_CICON, "The game's flashing icon", "over an orc about to strike"}};
int somvr_vm_help(int item, const char **l1, const char **l2) {
    for (size_t i = 0; i < sizeof g_fight_help / sizeof g_fight_help[0]; i++)
        if (g_fight_help[i].item == item) return *l1 = g_fight_help[i].l1, *l2 = g_fight_help[i].l2, 1;
    *l1 = *l2 = NULL;
    return 0;
}
int somvr_vm_row_page(int item) {
    switch (item) {
    case SOMVR_VMI_SWORD:
    case SOMVR_VMI_WEIGHT:
    case SOMVR_VMI_SWHAND:
    case SOMVR_VMI_BITE:
    case SOMVR_VMI_ARROWS:
    case SOMVR_VMI_STRING:
    case SOMVR_VMI_DODGE:
    case SOMVR_VMI_WSTEP:
    case SOMVR_VMI_STEALTH:
    case SOMVR_VMI_CROWD:
    case SOMVR_VMI_CICON: return SOMVR_VMPG_FIGHT;
    default: return item >= 0 && item < SOMVR_VMI_ROWS ? SOMVR_VMPG_SETTINGS : -1;
    }
}

/* the Controls page (build 6, SOMVR_GAMEMOVES=off: A, the bow, the dash, the wraith phase and the finishers do nothing
 * on foot). Keys up to 7 characters, the rest up to 20 (29 cells of 16 pixels on the 512-pixel panel). The minimap's
 * watch (SOMVR_XR_MINIMAP=wrist) is on the panel's wrist; Quit = the Settings page's bottom row, pressed twice.
 * Build 10: the menu button held (VM_LONG_S, 0.6 s) opens the menu even mid-fight, where the palm gesture is held off
 * while an attack comes (NOTES 2026-10-24 16:35); "Orc: Practice > Spawn" left out (the Practice tab says it). */
#ifndef SOMVR_VM_CTL_REPORT /* the bug-mark chord (somvr_session.h: B + Y held half a second and let go); "" = no line */
#define SOMVR_VM_CTL_REPORT "hold B + Y (0.5 s)"
#define SOMVR_VM_CTL_REPORT2 ""
#endif
#ifndef SOMVR_VM_CTL_REPORT2
#define SOMVR_VM_CTL_REPORT2 ""
#endif
/* build 13: a line wants (need) or excludes (unless) avail bits; opt > 0 = optional, dropped in that order (1 first)
 * while more than SOMVR_VM_CTL_FIT lines are wanted. Build 13's all (the fling, the verbs, the descent): 18 lines with
 * Sheathe and Quit dropped ("grip there again" = the same gesture; Quit = the Settings page's bottom button); with
 * the crouch (its own line) Sprint goes too (the left stick's click, as every VR game). Build 14 candidate
 * (SOMVR_VMA_USE, the story drill's round 2): Use names the picks and the missions' start, Menu and Pause share a line,
 * so Sprint stays with the crouch: the story's first objective is timed (1:30) and its hint names the sprint.
 * Build 17 (re-xr-pipeline.md 56): the lines 28 px apart when needed (SOMVR_VM_CTL_DY_MIN: 21 fit); the two hands' hilt
 * and the dagger (SOMVR_TWOHAND, SOMVR_SLOTS: every build since 9, as the Sword's line), the pullover's ring
 * (SOMVR_VMA_PULLOVER), the block; the bow's lines for each string and arrows (the Arrows row: somvr_vm_ctl_avail) */
typedef struct {
    const char *key, *what;
    uint64_t need, unless; /* build 18: 64 bits, as avail */
    int opt;
} vm_ctl_line;
static const vm_ctl_line g_ctl[] = {
    {"Walk", "left stick", 0, 0, 0},
    {"Sprint", "click left stick", 0, 0, 4}, /* build 16: after Parry (opt 3) */
    {"Turn", "R stick, down=crouch", 0, SOMVR_VMA_CROUCH, 0},
    {"Turn", "right stick", SOMVR_VMA_CROUCH, 0, 0},
    {"Crouch", "duck or R stick down", SOMVR_VMA_CROUCH, 0, 0}, /* build 13: SOMVR_CROUCH (the stick toggles it) */
    {"Sword", "grip at R shoulder", 0, 0, 0},
    {"Sheathe", "grip there again", 0, 0, 2},
    {"2 hands", "L grip/LT on hilt", 0, 0, 0}, /* build 17: SOMVR_TWOHAND (the left trigger too: SOMVR_BOW_DRAW=slot) */
    /* build 17: the block in plain words (a headset session found blocking unclear), never dropped (was "Parry: move
     * sword into blow", the first to go after Quit and Sheathe); the red attacks (no block: a dodge) on the practice
     * page */
    {"Block", "sword across a blow", 0, 0, 0},
    {"Dagger", "L grip at L hip", 0, 0, 3},     /* build 17: SOMVR_SLOTS' Acharn (goes after Quit and Sheathe) */
    {"Use", "B (interact)", 0, SOMVR_VMA_USE, 0},
    {"Use", "B (pick, missions)", SOMVR_VMA_USE, 0, 0}, /* build 14: SOMVR_USE's picks and "Start Mission" */
    {"Climb", "grip, pull; fling up", SOMVR_VMA_FLING, 0, 0}, /* build 13: let go mid-pull = the fling */
    {"Ledge", "pull at the ring", SOMVR_VMA_PULLOVER, 0, 0}, /* build 16's pullover: a ring on the top, 25 cm = over */
    {"Drop", "A (on a wall)", 0, SOMVR_VMA_DESCENT, 0},
    {"Drop", "A (wall, edge)", SOMVR_VMA_DESCENT, SOMVR_VMA_DROPMARK, 0}, /* build 13: A at an edge over a 4 m drop */
    {"Drop", "hold A, let go", SOMVR_VMA_DESCENT | SOMVR_VMA_DROPMARK, 0, 0}, /* build 17: held = the landing shown */
    {"Bow", "hold left trigger", 0, SOMVR_VMA_BOWSLOT, 0}, /* build 11 (SOMVR_BOW): Swing/Crouch made room */
    {"Bow", "L grip at L shoulder", SOMVR_VMA_BOWSLOT, 0, 0}, /* build 15b: SOMVR_BOW_DRAW=slot */
    {"Shoot", "R grip, pull, let go", 0, SOMVR_VMA_BOWRT | SOMVR_VMA_QVGRIP | SOMVR_VMA_BOWTRIG, 0},
    {"Shoot", "RT, pull, let go", SOMVR_VMA_BOWTRIG, SOMVR_VMA_BOWRT, 0}, /* build 17: the trigger, Arrows: Auto */
    {"Arrow", "RT at R shoulder", SOMVR_VMA_BOWRT, 0, 0}, /* build 16: SOMVR_BOW_QUIVER=on, the string on the trigger */
    {"Arrow", "R grip at R shoulder", SOMVR_VMA_QVGRIP, 0, 0}, /* build 17: the quiver, the string on the grip */
    {"Shoot", "nock, pull, let go", SOMVR_VMA_BOWRT, 0, 0},
    {"Shoot", "nock, pull, let go", SOMVR_VMA_QVGRIP, 0, 0},
    {"Grab", "L grip at orc's head", SOMVR_VMA_VERBS, 0, 0}, /* build 13: the notes say what a held orc does */
    {"Execute", "R trigger + hard hit", SOMVR_VMA_VERBS, 0, 0}, /* build 12's note; a line with the verbs' notes */
    {"Wraith", "hold Y (vision)", 0, SOMVR_VMA_STRIKE, 0},
    {"Wraith", "hold Y; A = Strike", SOMVR_VMA_STRIKE, 0, 0}, /* build 17: SOMVR_STRIKE_GAZE (the mark says which orc) */
    {"Map", "click right stick", 0, 0, 0},
    {"Minimap", "back of left wrist", 0, 0, 0},
    {"Menu", "L palm / hold button", 0, SOMVR_VMA_USE, 0}, /* build 13: one line (was "left palm to face", "or hold menu button") */
    {"Pause", "tap menu button", 0, SOMVR_VMA_USE, 0},
    {"Menu", "palm/hold; tap=pause", SOMVR_VMA_USE, 0, 0}, /* build 14: the two lines in one (Sprint's room) */
    {"Quit", "Settings > Quit x2", 0, 0, 1},
    {"Report", SOMVR_VM_CTL_REPORT, 0, 0, 0},
    {"", SOMVR_VM_CTL_REPORT2, 0, 0, 0},
};
#define VM_CTL_N (sizeof g_ctl / sizeof g_ctl[0])
/* build 13: the notes name what the rows have no room for (build 12's "Not yet: dash, wraith phase, finishers, Shadow
 * Strike" was stale: the strike's wraith dash and the execution came in build 12; the strike shows its own prompt) */
static const char *const g_ctl_notes[SOMVR_VM_CTL_NOTES] = {"Execute: R trigger + hard hit", ""};
static const char *const g_ctl_notes_verbs[SOMVR_VM_CTL_NOTES] = {"Grabbed: L trigger = drain,", "pull it to you = interrogate"};
/* the lines avail wants: bit k = g_ctl[k] shown */
static uint64_t ctl_mask(uint64_t avail) {
    uint64_t m = 0;
    int n = 0, top = 0;
    for (size_t k = 0; k < VM_CTL_N; k++) {
        const vm_ctl_line *l = &g_ctl[k];
        if (!l->what[0] || (l->need & avail) != l->need || (l->unless & avail)) continue; /* "" = no line */
        m |= 1ull << k, n++;
        if (l->opt > top) top = l->opt;
    }
    for (int r = 1; r <= top && n > SOMVR_VM_CTL_FIT; r++)
        for (size_t k = 0; k < VM_CTL_N && n > SOMVR_VM_CTL_FIT; k++)
            if ((m >> k & 1) && g_ctl[k].opt == r) m &= ~(1ull << k), n--;
    return m;
}
_Static_assert(sizeof g_ctl / sizeof g_ctl[0] <= 64, "the Controls page's lines fit ctl_mask's bits");
uint64_t somvr_vm_ctl_avail(const somvr_settings *s, uint64_t avail) {
    if (avail & SOMVR_VMA_STRING) /* build 19: the Bow string row: the string's button as it says */
        avail = s->bow_string ? avail | SOMVR_VMA_BOWTRIG : avail & ~(uint64_t)SOMVR_VMA_BOWTRIG;
    if (!(avail & SOMVR_VMA_ARROWS)) return avail;
    avail &= ~(uint64_t)(SOMVR_VMA_BOWRT | SOMVR_VMA_QVGRIP);
    if (s->quiver) avail |= (avail & SOMVR_VMA_BOWTRIG) ? SOMVR_VMA_BOWRT : SOMVR_VMA_QVGRIP;
    return avail;
}
int somvr_vm_ctl_dy(void) { return somvr_vm_ctl_dy_av(0); }
int somvr_vm_ctl_dy_av(uint64_t avail) {
    const char *k, *w;
    int n = somvr_vm_controls_av(avail, 0, &k, &w) + SOMVR_VM_CTL_NOTES + 1; /* + the card's close hint */
    int room = SOMVR_VM_TEX_H - 16 - 4 - SOMVR_VM_CTL_Y0;                   /* the panel's bottom margin, the notes' gap */
    int dy = n > 0 ? room / n : SOMVR_VM_CTL_DY;
    return dy > SOMVR_VM_CTL_DY ? SOMVR_VM_CTL_DY : dy < SOMVR_VM_CTL_DY_MIN ? SOMVR_VM_CTL_DY_MIN : dy;
}
const char *somvr_vm_controls_note(int i) { return somvr_vm_controls_note_av(0, i); }
const char *somvr_vm_controls_note_av(uint64_t avail, int i) {
    if (i < 0 || i >= SOMVR_VM_CTL_NOTES) return NULL;
    return (avail & SOMVR_VMA_VERBS) ? g_ctl_notes_verbs[i] : g_ctl_notes[i];
}
int somvr_vm_controls(int i, const char **key, const char **what) { return somvr_vm_controls_av(0, i, key, what); }
int somvr_vm_controls_av(uint64_t avail, int i, const char **key, const char **what) {
    uint64_t m = ctl_mask(avail);
    int n = 0;
    *key = *what = NULL;
    for (size_t k = 0; k < VM_CTL_N; k++) {
        if (!(m >> k & 1)) continue;
        if (n == i) *key = g_ctl[k].key, *what = g_ctl[k].what;
        n++;
    }
    return n;
}

/* ---- build 21 (re-xr-pipeline.md 82): the Test page's text ---------------------------------------------------- */
/* the character the font draws for the bytes at p (left of them): printable ASCII as it is, a tab a space, UTF-8's
 * curly quotes, dashes and the degree sign their look-alikes, anything else outside ASCII '?', a control byte 0
 * (dropped); *adv = the bytes it took */
static char tx_char(const unsigned char *p, size_t left, size_t *adv) {
    *adv = 1;
    if (*p == '\t') return ' ';
    if (*p >= 32 && *p < 127) return (char)*p;
    if (*p < 0x80) return 0;
    size_t n = (*p & 0xE0) == 0xC0 ? 2 : (*p & 0xF0) == 0xE0 ? 3 : (*p & 0xF8) == 0xF0 ? 4 : 1;
    n = n > left ? left : n;
    for (size_t k = 1; k < n; k++)
        if ((p[k] & 0xC0) != 0x80) {
            n = k;
            break;
        }
    *adv = n;
    if (n == 3 && p[0] == 0xE2 && p[1] == 0x80) {
        if (p[2] == 0x98 || p[2] == 0x99) return '\'';
        if (p[2] == 0x9C || p[2] == 0x9D) return '"';
        if (p[2] == 0x93 || p[2] == 0x94) return '-';
    }
    if (n == 2 && p[0] == 0xC2 && p[1] == 0xB0) return (char)SOMVR_FONT_DEG;
    return '?';
}
static void tx_line(somvr_vm_text *t, int kind, const char *s, size_t n) {
    if (t->n >= SOMVR_VM_TEST_LINES - 1) { /* the last line says there is more */
        if (t->n == SOMVR_VM_TEST_LINES - 1) {
            snprintf(t->line[t->n], sizeof t->line[0], "(more in the file)");
            t->kind[t->n++] = SOMVR_VMT_NOTE;
        }
        t->truncated = 1;
        return;
    }
    n = n > SOMVR_VM_TEST_COLS ? SOMVR_VM_TEST_COLS : n;
    memcpy(t->line[t->n], s, n);
    t->line[t->n][n] = 0;
    t->kind[t->n++] = (uint8_t)kind;
}
/* s (n bytes, clean) wrapped by words into lines of kind (an item's: SOMVR_VM_TEST_COLS - 2 wide, its next lines MORE;
 * a paragraph's next TEXT2); a word longer than a line is cut; at most max lines (the title: 2) */
static void tx_wrap(somvr_vm_text *t, int kind, const char *s, size_t n, int max) {
    int w = kind == SOMVR_VMT_ITEM ? SOMVR_VM_TEST_COLS - 2 : SOMVR_VM_TEST_COLS, lines = 0;
    size_t i = 0;
    while (i < n && lines < max) {
        while (i < n && s[i] == ' ') i++;
        if (i >= n) break;
        size_t e = i, last = i; /* the end of the last whole word that fits */
        while (e < n && e - i <= (size_t)w) {
            if (s[e] == ' ') last = e;
            e++;
        }
        size_t end = e >= n && e - i <= (size_t)w ? n : last > i ? last : i + (size_t)w;
        int k = lines == 0 ? kind : kind == SOMVR_VMT_ITEM ? SOMVR_VMT_MORE : kind == SOMVR_VMT_TEXT ? SOMVR_VMT_TEXT2 : kind;
        while (end > i && s[end - 1] == ' ') end--;
        tx_line(t, k, s + i, end - i);
        lines++, i = end;
    }
}
int somvr_vm_text_parse(somvr_vm_text *t, const char *text, size_t len) {
    memset(t, 0, sizeof *t);
    if (!text) len = 0;
    if (len > SOMVR_VM_TEST_BYTES) len = SOMVR_VM_TEST_BYTES, t->truncated = 1;
    static const size_t CAP = SOMVR_VM_TEST_BYTES;
    char para[SOMVR_VM_TEST_BYTES + 1], ln[SOMVR_VM_TEST_BYTES + 1];
    size_t pn = 0;
    int pkind = 0, title = 0;
    const unsigned char *u = (const unsigned char *)text;
    for (size_t i = 0; i <= len;) {
        size_t e = i, n = 0, adv;
        while (e < len && u[e] != '\n' && u[e]) e++;
        for (size_t k = i; k < e; k += adv) { /* the line, clean */
            char ch = tx_char(u + k, e - k, &adv);
            if (ch && n < CAP) ln[n++] = ch;
        }
        while (n && ln[n - 1] == ' ') n--;
        size_t b = 0;
        while (b < n && ln[b] == ' ') b++;
        int comment = n > 0 && ln[0] == '#', blank = b >= n, item = !blank && n - b >= 2 &&
                                                                  (ln[b] == '-' || ln[b] == '*') && ln[b + 1] == ' ';
        if (!comment && (blank || item || !title)) { /* the paragraph ends */
            if (pkind) tx_wrap(t, pkind, para, pn, pkind == SOMVR_VMT_TITLE ? 2 : SOMVR_VM_TEST_LINES);
            pkind = 0, pn = 0;
        }
        if (!comment && !blank) {
            const char *s = ln + b + (item ? 2 : 0);
            size_t m = n - b - (item ? 2 : 0);
            if (!title) pkind = SOMVR_VMT_TITLE, title = 1, t->n_title = 0;
            else if (item) pkind = SOMVR_VMT_ITEM;
            else if (!pkind) pkind = SOMVR_VMT_TEXT;
            else if (pn < CAP) para[pn++] = ' '; /* a line under an item or text continues it */
            m = m > CAP - pn ? CAP - pn : m;
            memcpy(para + pn, s, m), pn += m;
            if (pkind == SOMVR_VMT_TITLE) { /* the title: its own line alone */
                tx_wrap(t, SOMVR_VMT_TITLE, para, pn, 2);
                pkind = 0, pn = 0;
            }
        }
        if (e >= len || !u[e]) {
            if (pkind) tx_wrap(t, pkind, para, pn, SOMVR_VM_TEST_LINES);
            break;
        }
        i = e + 1;
    }
    for (int k = 0; k < t->n && t->kind[k] == SOMVR_VMT_TITLE; k++) t->n_title++;
    t->have = t->n_title > 0;
    if (!t->have) { /* no file, or nothing in it */
        t->n = 0;
        tx_line(t, SOMVR_VMT_TITLE, "What to test", 12);
        tx_line(t, SOMVR_VMT_NOTE, "No notes in this build.", 23);
        t->n_title = 1;
    }
    /* the pages: the title on each; the rest from under it to above the Prev / Next row, an item kept whole on a page
     * when it fits one */
    int top = SOMVR_VM_TEST_Y0 + t->n_title * 34 + 8, bottom = SOMVR_VM_TEST_NAV_Y - 8, page = 0, y = top;
    for (int k = 0; k < t->n_title; k++) t->page[k] = 0, t->y[k] = (int16_t)(SOMVR_VM_TEST_Y0 + 34 * k);
    for (int k = t->n_title; k < t->n;) {
        int g = k + 1;
        while (g < t->n && (t->kind[g] == SOMVR_VMT_MORE || t->kind[g] == SOMVR_VMT_TEXT2)) g++;
        int h = (g - k) * SOMVR_VM_TEST_DY, gap = y > top ? SOMVR_VM_TEST_GAP : 0;
        if (y + gap + h > bottom && y > top && h <= bottom - top) page++, y = top, gap = 0;
        y += gap;
        for (int j = k; j < g; j++) {
            if (y + SOMVR_VM_TEST_DY > bottom) page++, y = top;
            t->page[j] = (uint8_t)(page < 255 ? page : 255), t->y[j] = (int16_t)y;
            y += SOMVR_VM_TEST_DY;
        }
        k = g;
    }
    t->pages = page + 1;
    return t->pages;
}
int somvr_vm_test_turn(const somvr_vm_text *t, int page, int dir) {
    int n = t && t->pages > 0 ? t->pages : 1, p = page + (dir < 0 ? -1 : dir > 0 ? 1 : 0);
    return p < 0 ? 0 : p >= n ? n - 1 : p;
}

int somvr_vm_practice_action(int item) {
    switch (item) {
    case SOMVR_VMI_PR_GO: return SOMVR_VMPR_GO;
    case SOMVR_VMI_PR_BACK: return SOMVR_VMPR_BACK;
    case SOMVR_VMI_PR_SPAWN: return SOMVR_VMPR_SPAWN;
    case SOMVR_VMI_PR_REMOVE: return SOMVR_VMPR_REMOVE;
    case SOMVR_VMI_PR_ARROWS: return SOMVR_VMPR_ARROWS; /* build 17 */
    case SOMVR_VMI_PR_HEALTH: return SOMVR_VMPR_HEALTH;
    case SOMVR_VMI_PR_FAR: return SOMVR_VMPR_FAR; /* build 18 */
    case SOMVR_VMI_PR_GROUP: return SOMVR_VMPR_GROUP; /* build 19 */
    default: return SOMVR_VMPR_NONE;
    }
}

static void pct(float v, char *buf, size_t cap) {
    if (v <= 0.0005f) snprintf(buf, cap, "Off");
    else snprintf(buf, cap, "%ld%%", lroundf(v * 100));
}
void somvr_vm_value(const somvr_settings *s, int item, char *buf, size_t cap) {
    char n[24];
    if (!cap) return;
    buf[0] = 0;
    switch (item) {
    case SOMVR_VMI_TURN: snprintf(buf, cap, "%s", s->turn ? "Smooth" : "Snap"); break;
    case SOMVR_VMI_SNAP: fmt_num(s->snap_deg, n, sizeof n), snprintf(buf, cap, "%s\x7f", n); break;
    case SOMVR_VMI_SPEED: fmt_num(s->turn_dps, n, sizeof n), snprintf(buf, cap, "%s\x7f/s", n); break;
    case SOMVR_VMI_VIGNETTE: pct(s->vignette, buf, cap); break;
    case SOMVR_VMI_BLINK: snprintf(buf, cap, "%s", s->blink_ms ? "On" : "Off"); break;
    case SOMVR_VMI_SEATED: snprintf(buf, cap, "%s", s->seated ? "On" : "Off"); break;
    case SOMVR_VMI_HEIGHT:
        if (s->height_cm <= 0) snprintf(buf, cap, "Auto");
        else fmt_num(s->height_cm, n, sizeof n), snprintf(buf, cap, "%s cm", n);
        break;
    case SOMVR_VMI_HUD: snprintf(buf, cap, "%s", s->hud ? "On" : "Off"); break;
    case SOMVR_VMI_HUD_DIST: {
        long cm = lroundf(s->hud_dist_m * 100);
        snprintf(buf, cap, "%ld.%02ld m", cm / 100, cm % 100);
        break;
    }
    case SOMVR_VMI_HAND: snprintf(buf, cap, "%s", s->hand ? "Left" : "Right"); break;
    case SOMVR_VMI_VIEW: snprintf(buf, cap, "%s", s->view ? "Third" : "First"); break;
    case SOMVR_VMI_HAPTICS: pct(s->haptics, buf, cap); break;
    case SOMVR_VMI_STEP: pct(s->step, buf, cap); break;
    case SOMVR_VMI_DASH: {
        static const char *const v[] = {"Off", "On", "Blink"};
        snprintf(buf, cap, "%s", v[s->dash < 3 ? s->dash : 0]);
        break;
    }
    case SOMVR_VMI_PHASE: {
        static const char *const v[] = {"Off", "On", "Re-face", "Blink"};
        snprintf(buf, cap, "%s", v[s->phase < 4 ? s->phase : 0]);
        break;
    }
    case SOMVR_VMI_SWORD: /* "0°", "+15°", "-10°" */
        fmt_num(s->sword_deg, n, sizeof n), snprintf(buf, cap, "%s%s\x7f", s->sword_deg > 0.0005f ? "+" : "", n);
        break;
    case SOMVR_VMI_WEIGHT: {
        static const char *const v[] = {"Off", "Light", "Heavy"};
        snprintf(buf, cap, "%s", v[s->sword_weight < 3 ? s->sword_weight : 0]);
        break;
    }
    case SOMVR_VMI_SWHAND: snprintf(buf, cap, "%s", s->sword_hand ? "Follow" : "Fixed"); break; /* build 17 */
    case SOMVR_VMI_BITE: snprintf(buf, cap, "%s", s->bite ? "On" : "Off"); break; /* build 17, the bite */
    case SOMVR_VMI_ARROWS: snprintf(buf, cap, "%s", s->quiver ? "Quiver" : "Auto"); break;
    case SOMVR_VMI_MUSIC: pct(s->music, buf, cap); break;
    case SOMVR_VMI_SIZE: snprintf(buf, cap, "%ld%%", lroundf(s->size * 100)); break; /* "115%" */
    case SOMVR_VMI_CROUCH: snprintf(buf, cap, "%s", s->crouch ? "Duck" : "Button"); break;
    case SOMVR_VMI_FLING: snprintf(buf, cap, "%s", s->fling ? "On" : "Off"); break;
    case SOMVR_VMI_SURFACE: snprintf(buf, cap, "%s", s->surface ? "On" : "Off"); break;
    case SOMVR_VMI_STRING: snprintf(buf, cap, "%s", s->bow_string ? "Trigger" : "Grip"); break; /* build 19 */
    case SOMVR_VMI_DODGE: snprintf(buf, cap, "%s", s->dodge ? "On" : "Off"); break;
    case SOMVR_VMI_WSTEP: snprintf(buf, cap, "%s", s->wstep ? "On" : "Off"); break;
    case SOMVR_VMI_STEALTH: snprintf(buf, cap, "%s", s->stealth ? "On" : "Off"); break;
    case SOMVR_VMI_CROWD: /* the least time between two blows at Talion: "0.7 s"; 0 = every orc at once */
        if (!s->crowd_ms) snprintf(buf, cap, "Off");
        else fmt_num(s->crowd_ms / 1000.0, n, sizeof n), snprintf(buf, cap, "%s%s s", n, s->crowd_ms % 1000 ? "" : ".0");
        break;
    case SOMVR_VMI_CICON: snprintf(buf, cap, "%s", s->counter_icon ? "Shown" : "Hidden"); break;
    case SOMVR_VMI_PR_ORC: snprintf(buf, cap, "%s", g_orc_labels[s->orc < 3 ? s->orc : 0]); break;
    default: break;
    }
}

/* the next value of list (ascending) from v in direction dir: held at the ends, or wrapped */
static float step(float v, const float *l, int n, int dir, int wrap) {
    const float eps = 1e-3f;
    if (dir > 0) {
        for (int i = 0; i < n; i++)
            if (l[i] > v + eps) return l[i];
        return wrap ? l[0] : (v > l[n - 1] ? v : l[n - 1]);
    }
    for (int i = n - 1; i >= 0; i--)
        if (l[i] < v - eps) return l[i];
    return wrap ? l[n - 1] : (v < l[0] ? v : l[0]);
}
static const float g_snap[] = {15, 30, 45, 60, 90}, g_dps[] = {60, 90, 120, 150, 180, 240, 300},
                   g_vig[] = {0, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f},
                   g_height[] = {0, 100, 105, 110, 115, 120, 125, 130, 135, 140, 145, 150, 155, 160, 165, 170},
                   g_dist[] = {0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 2.5f, 3.0f},
                   g_hap[] = {0, 0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f}, g_stp[] = {0, 0.25f, 0.5f, 0.75f, 1.0f},
                   g_sword[] = {-45, -40, -35, -30, -25, -20, -15, -10, -5, 0, 5, 10, 15, 20, 25, 30, 35, 40, 45},
                   g_size[] = {1.0f, 1.05f, 1.1f, 1.15f, 1.2f, 1.25f, 1.3f, 1.35f, 1.4f},
                   g_crowd[] = {0, 500, 700, 1000, 1500, 2000}; /* build 19: the crowd's pace, ms (0 = off) */
/* a choice of n values: the next in direction dir, held at the ends, or wrapped */
static int cycle(uint32_t *v, uint32_t n, int dir, int wrap) {
    uint32_t c = *v < n ? *v : 0, nv = dir > 0 ? (c + 1 < n ? c + 1 : wrap ? 0 : c) : (c > 0 ? c - 1 : wrap ? n - 1 : 0);
    if (nv == *v) return SOMVR_VMR_NONE;
    *v = nv;
    return SOMVR_VMR_CHANGED;
}
#define STEP(field, list)                                                                                              \
    do {                                                                                                               \
        float nv = step(s->field, list, (int)(sizeof list / sizeof list[0]), dir, wrap);                               \
        if (fabsf(nv - s->field) < 1e-4f) return SOMVR_VMR_NONE;                                                       \
        s->field = nv;                                                                                                 \
        return SOMVR_VMR_CHANGED;                                                                                      \
    } while (0)

int somvr_vm_apply(somvr_settings *s, uint64_t avail, uint32_t blink_on_ms, int item, int part) {
    if (!somvr_vm_enabled(s, avail, item)) return SOMVR_VMR_NONE;
    if (item == SOMVR_VMI_RECENTER) return part == SOMVR_VMP_BUTTON ? SOMVR_VMR_RECENTER : SOMVR_VMR_NONE;
    if (item == SOMVR_VMI_CLOSE) return part == SOMVR_VMP_BUTTON ? SOMVR_VMR_CLOSE : SOMVR_VMR_NONE;
    if (somvr_vm_tab_page(item) >= 0) return part == SOMVR_VMP_BUTTON ? SOMVR_VMR_PAGE : SOMVR_VMR_NONE;
    if (item == SOMVR_VMI_REPORT) return part == SOMVR_VMP_BUTTON ? SOMVR_VMR_REPORT : SOMVR_VMR_NONE;
    if (item == SOMVR_VMI_QUIT) return part == SOMVR_VMP_BUTTON ? SOMVR_VMR_QUIT : SOMVR_VMR_NONE;
    if (item == SOMVR_VMI_TEST_PREV || item == SOMVR_VMI_TEST_NEXT) /* build 21 */
        return part == SOMVR_VMP_BUTTON ? SOMVR_VMR_SCROLL : SOMVR_VMR_NONE;
    if (somvr_vm_practice_action(item)) return part == SOMVR_VMP_BUTTON ? SOMVR_VMR_PRACTICE : SOMVR_VMR_NONE;
    if (part != SOMVR_VMP_ROW && part != SOMVR_VMP_LESS && part != SOMVR_VMP_MORE) return SOMVR_VMR_NONE;
    int dir = part == SOMVR_VMP_LESS ? -1 : 1, wrap = part == SOMVR_VMP_ROW;
    switch (item) {
    case SOMVR_VMI_TURN: s->turn = !s->turn; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_BLINK: s->blink_ms = s->blink_ms ? 0 : (blink_on_ms ? blink_on_ms : SOMVR_SET_BLINK_MS); return 1;
    case SOMVR_VMI_SEATED: s->seated = !s->seated; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_CROUCH: s->crouch = !s->crouch; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_HUD: s->hud = !s->hud; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_HAND: s->hand = !s->hand; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_VIEW: s->view = !s->view; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_FLING: s->fling = !s->fling; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_SURFACE: s->surface = !s->surface; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_SWHAND: s->sword_hand = !s->sword_hand; return SOMVR_VMR_CHANGED; /* build 17 */
    case SOMVR_VMI_BITE: s->bite = !s->bite; return SOMVR_VMR_CHANGED; /* build 17, the bite: < > flip it too */
    case SOMVR_VMI_ARROWS: s->quiver = !s->quiver; return SOMVR_VMR_CHANGED;
    /* build 19: the Fight page's toggles (< > flip them too) */
    case SOMVR_VMI_STRING: s->bow_string = !s->bow_string; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_DODGE: s->dodge = !s->dodge; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_WSTEP: s->wstep = !s->wstep; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_STEALTH: s->stealth = !s->stealth; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_CICON: s->counter_icon = !s->counter_icon; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_CROWD: {
        float nv = step((float)s->crowd_ms, g_crowd, (int)(sizeof g_crowd / sizeof g_crowd[0]), dir, wrap);
        uint16_t ms = (uint16_t)lrintf(nv);
        if (ms == s->crowd_ms) return SOMVR_VMR_NONE;
        s->crowd_ms = ms;
        return SOMVR_VMR_CHANGED;
    }
    case SOMVR_VMI_SNAP: STEP(snap_deg, g_snap);
    case SOMVR_VMI_SPEED: STEP(turn_dps, g_dps);
    case SOMVR_VMI_VIGNETTE: STEP(vignette, g_vig);
    case SOMVR_VMI_HEIGHT: STEP(height_cm, g_height);
    case SOMVR_VMI_HUD_DIST: STEP(hud_dist_m, g_dist);
    case SOMVR_VMI_HAPTICS: STEP(haptics, g_hap);
    case SOMVR_VMI_STEP: STEP(step, g_stp);
    case SOMVR_VMI_SWORD: STEP(sword_deg, g_sword);
    case SOMVR_VMI_MUSIC: STEP(music, g_vig); /* Off, 10 .. 100 % */
    case SOMVR_VMI_SIZE: STEP(size, g_size);
    case SOMVR_VMI_DASH: return cycle(&s->dash, 3, dir, wrap);
    case SOMVR_VMI_PHASE: return cycle(&s->phase, 4, dir, wrap);
    case SOMVR_VMI_PR_ORC: return cycle(&s->orc, 3, dir, wrap);
    case SOMVR_VMI_WEIGHT: {
        uint32_t w = s->sword_weight;
        int r = cycle(&w, 3, dir, wrap);
        s->sword_weight = (uint8_t)w;
        return r;
    }
    default: return SOMVR_VMR_NONE;
    }
}

/* ---- the layout --------------------------------------------------------------------------------------------------- */
#define ROW_Y0 64    /* the first row's top */
#define ROW_H 38      /* v4: 16 rows (40 px for 15 before); v5: closer when more than 16 rows are shown (row_h) */
#define ROW_FIT 16    /* rows of ROW_H above the bottom row of buttons */
#define LESS_X 256   /* the < box */
#define MORE_X 448   /* the > box */
#define BOX_W 48
#define BOX_H 34
#define VALUE_X0 (LESS_X + BOX_W) /* the value is centred between the boxes */
#define RECENTER_Y (ROW_Y0 + ROW_FIT * ROW_H + 12) /* the bottom row: where 16 rows end, however many are shown */
/* the practice page: the state lines at 70 and 104, then the buttons and the orc type row; build 17: the two refills
 * under Spawn / Remove (the buttons 50 px, were 56), then the hints and the defence lines */
#define PR_GO_Y 136   /* build 18: the far orc under Spawn, the buttons 48 px (50 in build 17) */
#define PR_BACK_Y 190
#define PR_ORC_Y 246
#define PR_SPAWN_Y 292
#define PR_FAR_Y 346
#define PR_REMOVE_Y 400
#define PR_ARROWS_Y 460
#define PR_HEALTH_Y 514
#define PR_BTN_H 48
#define PR_HINT_Y 574
/* build 19: the Spawn row split - Spawn an orc (40..260) | Spawn group (272..472) */
#define PR_SPAWN_W 220
#define PR_GROUP_X 272
#define PR_GROUP_W 200

/* build 21 (SOMVR_VMA_TESTPAGE): the tab strip's four tabs - the three 134 wide (the labels' 128 and 3 a side), the
 * Test tab 80, 3 apart from x 10 to 501 */
#define TAB4_W 134
#define TAB4_X3 (10 + 3 * (TAB4_W + 3))
#define TAB4_W3 80

int somvr_vm_shown(uint64_t avail, int item) {
    switch (item) {
    case SOMVR_VMI_STEP:
    case SOMVR_VMI_DASH:
    case SOMVR_VMI_PHASE: return (avail & SOMVR_VMA_MOVES) != 0;
    case SOMVR_VMI_MUSIC: return (avail & SOMVR_VMA_MUSIC) != 0;
    case SOMVR_VMI_SIZE: return (avail & SOMVR_VMA_SIZE) != 0; /* the player size: first person on the head */
    case SOMVR_VMI_CROUCH: return (avail & SOMVR_VMA_CROUCH) != 0; /* build 13: SOMVR_CROUCH set */
    case SOMVR_VMI_FLING: return (avail & SOMVR_VMA_FLING) != 0;     /* build 13: the climb's fling */
    case SOMVR_VMI_SURFACE: return (avail & SOMVR_VMA_SURFACE) != 0; /* build 13: hands stop at surfaces */
    case SOMVR_VMI_WEIGHT: return (avail & SOMVR_VMA_WEIGHT) != 0;   /* build 16: the sword's weight */
    case SOMVR_VMI_SWHAND: return (avail & SOMVR_VMA_SWHAND) != 0;   /* build 17: the drawn hands with it */
    case SOMVR_VMI_BITE: return (avail & SOMVR_VMA_BITE) != 0;       /* build 17: a landed cut's bite */
    case SOMVR_VMI_ARROWS: return (avail & SOMVR_VMA_ARROWS) != 0;   /* build 17: the quiver or arrows on the string */
    /* build 19: the Fight page's rows of build 18's and 19's switches, each with its feature in this session */
    case SOMVR_VMI_STRING: return (avail & SOMVR_VMA_STRING) != 0;
    case SOMVR_VMI_DODGE: return (avail & SOMVR_VMA_DODGE) != 0;
    case SOMVR_VMI_WSTEP: return (avail & SOMVR_VMA_WSTEP) != 0;
    case SOMVR_VMI_STEALTH: return (avail & SOMVR_VMA_STEALTH) != 0;
    case SOMVR_VMI_CROWD: return (avail & SOMVR_VMA_CROWD) != 0;
    case SOMVR_VMI_CICON: return (avail & SOMVR_VMA_CICON) != 0;
    default: return item >= 0 && item < SOMVR_VMI_ALL;
    }
}
/* the value row item's slot among the shown rows of its page (-1 = not shown), the slots there (build 19: its rows and
 * the page button under them: SOMVR_VMI_PG_FIGHT on the Settings page, SOMVR_VMI_PG_BACK on the Fight page; the
 * button's slot = the rows') and the rows' height */
static int row_slot(uint64_t avail, int item, int *n_shown) {
    int page = item == SOMVR_VMI_PG_FIGHT ? SOMVR_VMPG_SETTINGS
               : item == SOMVR_VMI_PG_BACK ? SOMVR_VMPG_FIGHT
                                           : somvr_vm_row_page(item);
    int slot = -1, n = 0;
    for (int i = 0; i < SOMVR_VMI_ROWS; i++)
        if (somvr_vm_row_page(i) == page && somvr_vm_shown(avail, i)) {
            if (i == item) slot = n;
            n++;
        }
    if (item == SOMVR_VMI_PG_FIGHT || item == SOMVR_VMI_PG_BACK) slot = n;
    if (n_shown) *n_shown = n + 1;
    return slot;
}
static int row_h(int n) { return n <= ROW_FIT ? ROW_H : ROW_FIT * ROW_H / n; }

int somvr_vm_rect(int item, int part, int rc[4]) { return somvr_vm_rect_av(0, item, part, rc); }
int somvr_vm_rect_av(uint64_t avail, int item, int part, int rc[4]) {
    rc[0] = rc[1] = rc[2] = rc[3] = 0;
    if (item == SOMVR_VMI_PG_FIGHT || item == SOMVR_VMI_PG_BACK) { /* build 19: the page button in its own slot */
        if (part != SOMVR_VMP_BUTTON) return 0;
        int n = 0, slot = row_slot(avail, item, &n), h = row_h(n);
        rc[0] = 16, rc[1] = ROW_Y0 + slot * h + 2, rc[2] = SOMVR_VM_TEX_W - 32, rc[3] = h - 4;
        return 1;
    }
    int t4 = (avail & SOMVR_VMA_TESTPAGE) != 0; /* build 21: the four tabs, no X on the panel, the Test page's row */
    if (item == SOMVR_VMI_TAB_TEST || item == SOMVR_VMI_TEST_PREV || item == SOMVR_VMI_TEST_NEXT || (t4 && item == SOMVR_VMI_CLOSE)) {
        if (!t4 || part != SOMVR_VMP_BUTTON || item == SOMVR_VMI_CLOSE) return 0;
        if (item == SOMVR_VMI_TAB_TEST) rc[0] = TAB4_X3, rc[1] = 10, rc[2] = TAB4_W3, rc[3] = 44;
        else rc[0] = item == SOMVR_VMI_TEST_PREV ? 16 : 344, rc[1] = SOMVR_VM_TEST_NAV_Y, rc[2] = 152, rc[3] = 56;
        return 1;
    }
    if (item == SOMVR_VMI_CLOSE || item == SOMVR_VMI_RECENTER || somvr_vm_tab_page(item) >= 0 ||
        item == SOMVR_VMI_REPORT || item == SOMVR_VMI_QUIT || somvr_vm_practice_action(item)) {
        static const int by[] = {PR_GO_Y, PR_BACK_Y, 0, PR_SPAWN_Y, PR_REMOVE_Y};
        int pa = somvr_vm_practice_action(item);
        if (part != SOMVR_VMP_BUTTON) return 0;
        if (item == SOMVR_VMI_CLOSE) rc[0] = 452, rc[1] = 10, rc[2] = 48, rc[3] = 44;
        else if (somvr_vm_tab_page(item) >= 0 && t4) /* build 21: four tabs across: 10, 147, 284 (134 wide), 421 (80) */
            rc[0] = 10 + (TAB4_W + 3) * somvr_vm_tab_page(item), rc[1] = 10, rc[2] = TAB4_W, rc[3] = 44;
        else if (somvr_vm_tab_page(item) >= 0) /* the tab strip: Settings, Practice, Controls */
            rc[0] = 12 + 144 * somvr_vm_tab_page(item), rc[1] = 10, rc[2] = 140, rc[3] = 44;
        /* the settings page's bottom row: Recentre | Report | Quit game (the last two: somvr_session.h) */
        /* three equal buttons from the rows' labels to the > boxes' right edge (16 .. 496), 12 apart */
        else if (item == SOMVR_VMI_RECENTER) rc[0] = 16, rc[1] = RECENTER_Y, rc[2] = 152, rc[3] = 56;
        else if (item == SOMVR_VMI_REPORT) rc[0] = 180, rc[1] = RECENTER_Y, rc[2] = 152, rc[3] = 56;
        else if (item == SOMVR_VMI_QUIT) rc[0] = 344, rc[1] = RECENTER_Y, rc[2] = 152, rc[3] = 56;
        else if (pa == SOMVR_VMPR_GROUP) rc[0] = PR_GROUP_X, rc[1] = PR_SPAWN_Y, rc[2] = PR_GROUP_W, rc[3] = PR_BTN_H;
        else
            rc[0] = 40, rc[1] = pa == SOMVR_VMPR_ARROWS ? PR_ARROWS_Y
                                : pa == SOMVR_VMPR_HEALTH ? PR_HEALTH_Y
                                : pa == SOMVR_VMPR_FAR    ? PR_FAR_Y
                                                          : by[item - SOMVR_VMI_PR_GO],
            rc[2] = pa == SOMVR_VMPR_SPAWN ? PR_SPAWN_W : SOMVR_VM_TEX_W - 80, rc[3] = PR_BTN_H;
        return 1;
    }
    if (item != SOMVR_VMI_PR_ORC && (item < 0 || item >= SOMVR_VMI_ROWS)) return 0;
    int n = 0, slot = item == SOMVR_VMI_PR_ORC ? 0 : row_slot(avail, item, &n);
    if (slot < 0) return 0; /* not shown */
    int h = item == SOMVR_VMI_PR_ORC ? ROW_H : row_h(n), y = item == SOMVR_VMI_PR_ORC ? PR_ORC_Y : ROW_Y0 + slot * h;
    int bh = BOX_H < h - 2 ? BOX_H : h - 2;
    switch (part) {
    case SOMVR_VMP_ROW: rc[0] = 8, rc[1] = y, rc[2] = SOMVR_VM_TEX_W - 16, rc[3] = h; return 1;
    case SOMVR_VMP_LESS: rc[0] = LESS_X, rc[1] = y + (h - bh) / 2, rc[2] = BOX_W, rc[3] = bh; return 1;
    case SOMVR_VMP_MORE: rc[0] = MORE_X, rc[1] = y + (h - bh) / 2, rc[2] = BOX_W, rc[3] = bh; return 1;
    default: return 0;
    }
}

static int in_rect(const int rc[4], float x, float y) {
    return x >= (float)rc[0] && x < (float)(rc[0] + rc[2]) && y >= (float)rc[1] && y < (float)(rc[1] + rc[3]);
}
/* the buttons a page has (the hit test's order: -1 ends the list) */
static const int g_page_buttons[5][14] = {
    {SOMVR_VMI_RECENTER, SOMVR_VMI_REPORT, SOMVR_VMI_QUIT, SOMVR_VMI_CLOSE, SOMVR_VMI_TAB, SOMVR_VMI_TAB_SET,
     SOMVR_VMI_TAB_CTL, SOMVR_VMI_TAB_TEST, SOMVR_VMI_PG_FIGHT, -1},
    {SOMVR_VMI_CLOSE, SOMVR_VMI_TAB, SOMVR_VMI_TAB_SET, SOMVR_VMI_TAB_CTL, SOMVR_VMI_TAB_TEST, SOMVR_VMI_PR_GO,
     SOMVR_VMI_PR_BACK, SOMVR_VMI_PR_SPAWN, SOMVR_VMI_PR_REMOVE, SOMVR_VMI_PR_ARROWS, SOMVR_VMI_PR_HEALTH,
     SOMVR_VMI_PR_FAR, SOMVR_VMI_PR_GROUP, -1},
    {SOMVR_VMI_CLOSE, SOMVR_VMI_TAB, SOMVR_VMI_TAB_SET, SOMVR_VMI_TAB_CTL, SOMVR_VMI_TAB_TEST, -1},
    /* build 19: the Fight page = the Settings page's bottom row, the tabs, its back button */
    {SOMVR_VMI_RECENTER, SOMVR_VMI_REPORT, SOMVR_VMI_QUIT, SOMVR_VMI_CLOSE, SOMVR_VMI_TAB, SOMVR_VMI_TAB_SET,
     SOMVR_VMI_TAB_CTL, SOMVR_VMI_TAB_TEST, SOMVR_VMI_PG_BACK, -1},
    /* build 21: the Test page = the tabs, its Prev / Next (the X: none with SOMVR_VMA_TESTPAGE - its rect is 0) */
    {SOMVR_VMI_TAB, SOMVR_VMI_TAB_SET, SOMVR_VMI_TAB_CTL, SOMVR_VMI_TAB_TEST, SOMVR_VMI_TEST_PREV, SOMVR_VMI_TEST_NEXT,
     -1}};
static const int *page_buttons(int page) {
    return g_page_buttons[page == SOMVR_VMPG_PRACTICE   ? 1
                          : page == SOMVR_VMPG_CONTROLS ? 2
                          : page == SOMVR_VMPG_FIGHT    ? 3
                          : page == SOMVR_VMPG_TEST     ? 4
                                                        : 0];
}
somvr_vm_hit somvr_vm_hit_test(float u, float v) { return somvr_vm_hit_test_av(0, SOMVR_VMPG_SETTINGS, u, v); }
somvr_vm_hit somvr_vm_hit_test_page(int page, float u, float v) { return somvr_vm_hit_test_av(0, page, u, v); }
somvr_vm_hit somvr_vm_hit_test_av(uint64_t avail, int page, float u, float v) {
    somvr_vm_hit h = {-1, SOMVR_VMP_NONE};
    if (!(u >= 0 && u <= 1 && v >= 0 && v <= 1)) return h; /* NaN too */
    float x = u * SOMVR_VM_TEX_W, y = v * SOMVR_VM_TEX_H;
    int rc[4];
    const int *bl = page_buttons(page);
    for (int i = 0; i < 14 && bl[i] >= 0; i++)
        if (somvr_vm_rect_av(avail, bl[i], SOMVR_VMP_BUTTON, rc) && in_rect(rc, x, y))
            return (somvr_vm_hit){bl[i], SOMVR_VMP_BUTTON};
    if (page == SOMVR_VMPG_CONTROLS || page == SOMVR_VMPG_TEST) return h; /* text only */
    int r0 = page == SOMVR_VMPG_PRACTICE ? SOMVR_VMI_PR_ORC : 0, r1 = page == SOMVR_VMPG_PRACTICE ? r0 + 1 : SOMVR_VMI_ROWS;
    for (int item = r0; item < r1; item++) {
        if (page != SOMVR_VMPG_PRACTICE && somvr_vm_row_page(item) != (page == SOMVR_VMPG_FIGHT ? SOMVR_VMPG_FIGHT
                                                                                                : SOMVR_VMPG_SETTINGS))
            continue; /* build 19: the other page's row */
        if (!somvr_vm_rect_av(avail, item, SOMVR_VMP_ROW, rc) || !in_rect(rc, x, y)) continue;
        for (int part = SOMVR_VMP_LESS; part <= SOMVR_VMP_MORE; part++)
            if (somvr_vm_rect_av(avail, item, part, rc) && in_rect(rc, x, y)) return (somvr_vm_hit){item, part};
        return (somvr_vm_hit){item, SOMVR_VMP_ROW};
    }
    return h;
}

/* ---- the picture -------------------------------------------------------------------------------------------------- */
typedef struct { uint8_t *px; int bgra; } canvas;
/* the opaque pixel of rgb, as stored */
static void opaque(const canvas *c, const uint8_t rgb[3], uint8_t out[4]) {
    out[0] = c->bgra ? rgb[2] : rgb[0], out[1] = rgb[1], out[2] = c->bgra ? rgb[0] : rgb[2], out[3] = 255;
}
static void put_px(const canvas *c, int x, int y, const uint8_t rgb[3], float a) {
    if (x < 0 || y < 0 || x >= SOMVR_VM_TEX_W || y >= SOMVR_VM_TEX_H) return;
    uint8_t *p = c->px + 4 * ((size_t)y * SOMVR_VM_TEX_W + (size_t)x);
    if (a >= 1) { /* opaque: the same bytes as the rounding below gives at a = 1 */
        opaque(c, rgb, p);
        return;
    }
    a = a < 0 ? 0 : a;
    uint8_t r = (uint8_t)lroundf(rgb[0] * a), g = (uint8_t)lroundf(rgb[1] * a), b = (uint8_t)lroundf(rgb[2] * a);
    p[0] = c->bgra ? b : r, p[1] = g, p[2] = c->bgra ? r : b, p[3] = (uint8_t)lroundf(a * 255);
}
static void fill(const canvas *c, int x, int y, int w, int h, const uint8_t rgb[3]) {
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > SOMVR_VM_TEX_W ? SOMVR_VM_TEX_W : x + w, y1 = y + h > SOMVR_VM_TEX_H ? SOMVR_VM_TEX_H : y + h;
    uint8_t q[4];
    opaque(c, rgb, q);
    for (int j = y0; j < y1; j++) {
        uint8_t *p = c->px + 4 * ((size_t)j * SOMVR_VM_TEX_W + (size_t)x0);
        for (int i = x0; i < x1; i++, p += 4) memcpy(p, q, 4);
    }
}
static void fill_rc(const canvas *c, const int rc[4], const uint8_t rgb[3]) { fill(c, rc[0], rc[1], rc[2], rc[3], rgb); }
static void text_at(const somvr_font *f, const canvas *c, int x, int y, const char *s, const uint8_t rgb[3]) {
    uint8_t col[4] = {rgb[0], rgb[1], rgb[2], 255};
    somvr_font_draw(f, c->px, SOMVR_VM_TEX_W, SOMVR_VM_TEX_H, x, y, s, SOMVR_VM_SCALE, col, c->bgra);
}
static void text_mid(const somvr_font *f, const canvas *c, int x0, int x1, int y, const char *s, const uint8_t rgb[3]) {
    text_at(f, c, x0 + (x1 - x0 - somvr_font_text_w(s, SOMVR_VM_SCALE)) / 2, y, s, rgb);
}

static const uint8_t C_BG[3] = {22, 26, 33}, C_BORDER[3] = {70, 84, 104}, C_TITLE[3] = {232, 192, 112},
                     C_LINE[3] = {40, 47, 58}, C_ROW_HOV[3] = {38, 46, 59}, C_LABEL[3] = {208, 213, 222},
                     C_VALUE[3] = {255, 255, 255}, C_DIM[3] = {96, 101, 112}, C_BOX[3] = {52, 61, 76},
                     C_BOX_HOV[3] = {92, 116, 150}, C_BOX_DIM[3] = {32, 37, 45}, C_WARN[3] = {168, 52, 40},
                     C_WARN_HOV[3] = {208, 72, 56}, C_HELP[3] = {150, 156, 168}, /* build 19: the Fight rows' help */
                     C_LOCK[3] = {150, 220, 255}; /* build 20: the locked part's outline (the ray's colour) */
/* build 20: a w-pixel frame just inside rc */
static void outline(const canvas *c, const int rc[4], int w, const uint8_t rgb[3]) {
    fill(c, rc[0], rc[1], rc[2], w, rgb), fill(c, rc[0], rc[1] + rc[3] - w, rc[2], w, rgb);
    fill(c, rc[0], rc[1], w, rc[3], rgb), fill(c, rc[0] + rc[2] - w, rc[1], w, rc[3], rgb);
}

/* a value row: its label, the < > boxes, its value (dimmed when disabled, lit when hovered); line = the separator */
static void draw_row(const somvr_font *f, const canvas *c, const somvr_vm_view *v, int item, int line) {
    int rc[4];
    int on = somvr_vm_enabled(&v->set, v->avail, item), hov = on && v->hover.item == item;
    if (!somvr_vm_rect_av(v->avail, item, SOMVR_VMP_ROW, rc)) return; /* not shown in this build */
    if (hov) fill_rc(c, rc, C_ROW_HOV);
    if (line) fill(c, rc[0] + 8, rc[1] + rc[3] - 1, rc[2] - 16, 1, C_LINE);
    int ty = rc[1] + (rc[3] - SOMVR_FONT_H * SOMVR_VM_SCALE) / 2;
    ty = ty < rc[1] ? rc[1] : ty;
    text_at(f, c, 20, ty, somvr_vm_label(item), on ? C_LABEL : C_DIM);
    for (int part = SOMVR_VMP_LESS; part <= SOMVR_VMP_MORE; part++) {
        int b[4];
        somvr_vm_rect_av(v->avail, item, part, b);
        fill_rc(c, b, !on ? C_BOX_DIM : hov && v->hover.part == part ? C_BOX_HOV : C_BOX);
        if (v->lock && hov && v->hover.part == part) outline(c, b, 3, C_LOCK); /* build 20 */
        char g[2] = {(char)(part == SOMVR_VMP_LESS ? SOMVR_FONT_LEFT : SOMVR_FONT_RIGHT), 0};
        text_mid(f, c, b[0], b[0] + b[2], ty, g, on ? C_VALUE : C_DIM);
    }
    char val[32];
    somvr_vm_value(&v->set, item, val, sizeof val);
    text_mid(f, c, VALUE_X0, MORE_X, ty, val, on ? C_VALUE : C_DIM);
    if (v->lock && hov && v->hover.part == SOMVR_VMP_ROW) outline(c, rc, 3, C_LOCK); /* build 20: the row locked */
}
/* a button: lit when hovered, dimmed when disabled; label NULL = its own */
static void draw_button(const somvr_font *f, const canvas *c, const somvr_vm_view *v, int item, const char *label) {
    int rc[4], on = somvr_vm_enabled(&v->set, v->avail, item), hov = v->hover.item == item;
    int warn = on && item == SOMVR_VMI_QUIT && v->quit_armed; /* pressed once: red, "Confirm" */
    if (!somvr_vm_rect_av(v->avail, item, SOMVR_VMP_BUTTON, rc)) return; /* build 21: the X with the four tabs */
    fill_rc(c, rc, !on ? C_BOX_DIM : warn ? (hov ? C_WARN_HOV : C_WARN) : hov ? C_BOX_HOV : C_BOX);
    if (v->lock && hov && on) outline(c, rc, 3, C_LOCK); /* build 20: the button locked */
    text_mid(f, c, rc[0], rc[0] + rc[2], rc[1] + (rc[3] - SOMVR_FONT_H * SOMVR_VM_SCALE) / 2,
             label ? label : somvr_vm_label(item), on ? C_VALUE : C_DIM);
}

/* build 21: the Test page - the title on every page, the page's lines (an item's bullet a small square in the title's
 * colour), the Prev / Next row (dimmed at the ends) with "page / pages" between them */
static void draw_test(const somvr_font *f, const canvas *c, const somvr_vm_view *v) {
    const somvr_vm_text *t = v->test;
    int pages = t && t->pages > 0 ? t->pages : 1, pg = v->test_page < 0 ? 0 : v->test_page >= pages ? pages - 1 : v->test_page;
    if (!t) {
        text_at(f, c, 20, SOMVR_VM_TEST_Y0, "What to test", C_TITLE);
        text_at(f, c, 20, SOMVR_VM_TEST_Y0 + 42, "No notes in this build.", C_DIM);
    }
    for (int k = 0; t && k < t->n; k++) {
        int kind = t->kind[k], y = t->y[k], x = 20;
        if (kind != SOMVR_VMT_TITLE && t->page[k] != pg) continue;
        const uint8_t *col = kind == SOMVR_VMT_TITLE ? C_TITLE
                             : kind == SOMVR_VMT_NOTE ? C_DIM
                             : kind == SOMVR_VMT_TEXT || kind == SOMVR_VMT_TEXT2 ? C_LABEL
                                                                                  : C_VALUE;
        if (kind == SOMVR_VMT_ITEM) fill(c, 24, y + 11, 8, 8, C_TITLE); /* the bullet, at the capitals' middle */
        if (kind == SOMVR_VMT_ITEM || kind == SOMVR_VMT_MORE) x = 52;
        text_at(f, c, x, y, t->line[k], col);
    }
    for (int i = 0; i < 2; i++) {
        int item = i ? SOMVR_VMI_TEST_NEXT : SOMVR_VMI_TEST_PREV, rc[4];
        if (!somvr_vm_rect_av(v->avail, item, SOMVR_VMP_BUTTON, rc)) continue;
        int on = i ? pg < pages - 1 : pg > 0, hov = on && v->hover.item == item;
        fill_rc(c, rc, !on ? C_BOX_DIM : hov ? C_BOX_HOV : C_BOX);
        if (v->lock && hov) outline(c, rc, 3, C_LOCK);
        static const char prev[] = {(char)SOMVR_FONT_LEFT, ' ', 'P', 'r', 'e', 'v', 0},
                          next[] = {'N', 'e', 'x', 't', ' ', (char)SOMVR_FONT_RIGHT, 0};
        text_mid(f, c, rc[0], rc[0] + rc[2], rc[1] + (rc[3] - SOMVR_FONT_H * SOMVR_VM_SCALE) / 2, i ? next : prev,
                 on ? C_VALUE : C_DIM);
    }
    char num[24];
    snprintf(num, sizeof num, "%d / %d", pg + 1, pages);
    text_mid(f, c, 168, 344, SOMVR_VM_TEST_NAV_Y + (56 - SOMVR_FONT_H * SOMVR_VM_SCALE) / 2, num, C_LABEL);
}

void somvr_vm_draw(const somvr_font *f, uint8_t *px, int bgra, const somvr_vm_view *v0) {
    canvas c = {px, bgra};
    somvr_vm_view vv = *v0; /* build 21: the card has no tabs - build 20's (its X) */
    if (vv.card) vv.avail &= ~(uint64_t)SOMVR_VMA_TESTPAGE;
    const somvr_vm_view *v = &vv;
    memset(px, 0, (size_t)SOMVR_VM_TEX_W * SOMVR_VM_TEX_H * 4);
    /* the panel: a rounded rectangle (radius 20, soft by a pixel), a 3-pixel border. Inside the margin E from every
     * edge and R + E from the top and bottom every pixel is the background, opaque (its distance > R - E > B there):
     * filled at once; the distance is evaluated only in the band around the edges (a redraw in well under 1 ms) */
    const float R = 20, B = 3, hw = 0.5f * SOMVR_VM_TEX_W, hh = 0.5f * SOMVR_VM_TEX_H;
    enum { E = 8, RE = 28 }; /* E > B + 1, RE = R + E */
    fill(&c, E, RE, SOMVR_VM_TEX_W - 2 * E, SOMVR_VM_TEX_H - 2 * RE, C_BG);
    for (int y = 0; y < SOMVR_VM_TEX_H; y++)
        for (int x = 0; x < SOMVR_VM_TEX_W; x++) { /* the rounded rectangle's signed distance (negative inside) */
            if (y >= RE && y < SOMVR_VM_TEX_H - RE && x == E) x = SOMVR_VM_TEX_W - E; /* (filled above) */
            float qx = fabsf((float)x + 0.5f - hw) - (hw - R), qy = fabsf((float)y + 0.5f - hh) - (hh - R);
            float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
            float in = -(sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0) - R);
            if (in <= 0) continue;
            put_px(&c, x, y, in < B ? C_BORDER : C_BG, in < 1 ? in : 1);
        }
    int pr = v->page == SOMVR_VMPG_PRACTICE, ctl = v->page == SOMVR_VMPG_CONTROLS, fight = v->page == SOMVR_VMPG_FIGHT;
    if (v->card) { /* the arrival card: a title instead of the tabs */
        text_at(f, &c, 24, 14, "CONTROLS", C_TITLE);
    } else { /* the tab strip: the page's own tab lit as the title (underlined), the others as buttons */
        static const int tabs[4] = {SOMVR_VMI_TAB_SET, SOMVR_VMI_TAB, SOMVR_VMI_TAB_CTL, SOMVR_VMI_TAB_TEST};
        for (int i = 0; i < 4; i++) {
            if (!somvr_vm_enabled(&v->set, v->avail, tabs[i])) continue;
            int rc[4];
            somvr_vm_rect_av(v->avail, tabs[i], SOMVR_VMP_BUTTON, rc); /* build 21: four with SOMVR_VMA_TESTPAGE */
            if (somvr_vm_tab_page(tabs[i]) == (fight ? SOMVR_VMPG_SETTINGS : v->page)) { /* (the Fight page: Settings') */
                fill(&c, rc[0] + 8, rc[1] + rc[3] - 4, rc[2] - 16, 3, C_TITLE);
                text_mid(f, &c, rc[0], rc[0] + rc[2], rc[1] + (rc[3] - SOMVR_FONT_H * SOMVR_VM_SCALE) / 2,
                         somvr_vm_label(tabs[i]), C_TITLE);
                if (v->lock && v->hover.item == tabs[i]) outline(&c, rc, 3, C_LOCK); /* build 20 */
            } else {
                draw_button(f, &c, v, tabs[i], NULL);
            }
        }
    }
    fill(&c, 16, 58, SOMVR_VM_TEX_W - 32, 2, C_BORDER);
    if (ctl) { /* the controls: key, what does it; what is not in this build; on the card how to close it */
        const char *k, *w;
        uint64_t ca = somvr_vm_ctl_avail(&v->set, v->avail); /* build 17: the bow's lines as the Arrows row says */
        int y = SOMVR_VM_CTL_Y0, n = somvr_vm_controls_av(ca, 0, &k, &w), dy = somvr_vm_ctl_dy_av(ca);
        for (int i = 0; i < n; i++, y += dy) {
            somvr_vm_controls_av(ca, i, &k, &w);
            text_at(f, &c, 20, y, k, C_TITLE);
            text_at(f, &c, 20 + 16 * 8, y, w, C_VALUE);
        }
        y += 4;
        for (int i = 0; i < SOMVR_VM_CTL_NOTES; i++, y += dy)
            text_at(f, &c, 20, y, somvr_vm_controls_note_av(ca, i), C_DIM);
        if (v->card) text_at(f, &c, 20, y, "Close: X box or A", C_LABEL);
    } else if (v->page == SOMVR_VMPG_TEST) { /* build 21: what to test in this build */
        draw_test(f, &c, v);
    } else if (!pr) { /* the Settings page or (build 19) the Fight page: their rows, the page button under them */
        int pg = fight ? SOMVR_VMPG_FIGHT : SOMVR_VMPG_SETTINGS;
        for (int item = 0; item < SOMVR_VMI_ROWS; item++)
            if (somvr_vm_row_page(item) == pg) draw_row(f, &c, v, item, 1); /* (the button under the last) */
        static const char go[] = {'F', 'i', 'g', 'h', 't', ' ', 's', 'e', 't', 't', 'i', 'n', 'g', 's', ' ', ' ',
                                  (char)SOMVR_FONT_RIGHT, 0},
                          back[] = {(char)SOMVR_FONT_LEFT, ' ', ' ', 'S', 'e', 't', 't', 'i', 'n', 'g', 's', 0};
        draw_button(f, &c, v, fight ? SOMVR_VMI_PG_BACK : SOMVR_VMI_PG_FIGHT, fight ? back : go);
        const char *h1, *h2;
        int bb[4];
        if (fight && somvr_vm_help(v->hover.item, &h1, &h2) &&
            somvr_vm_rect_av(v->avail, SOMVR_VMI_PG_BACK, SOMVR_VMP_BUTTON, bb) &&
            bb[1] + bb[3] + 14 + 2 * 30 <= RECENTER_Y) { /* the hovered row's two lines (the room under the page's) */
            text_at(f, &c, 24, bb[1] + bb[3] + 14, h1, C_HELP);
            text_at(f, &c, 24, bb[1] + bb[3] + 14 + 30, h2, C_HELP);
        }
        draw_button(f, &c, v, SOMVR_VMI_RECENTER, "Recentre");
        draw_button(f, &c, v, SOMVR_VMI_REPORT, NULL);
        draw_button(f, &c, v, SOMVR_VMI_QUIT, v->quit_armed ? "Confirm" : "Quit");
    } else { /* the state, the buttons, the orc type, a hint */
        uint64_t a = v->avail;
        text_at(f, &c, 24, 70,
                (a & SOMVR_VMA_PR_BUSY)   ? "On the way ..."
                : (a & SOMVR_VMA_PR_HERE) ? "In the practice area"
                                          : "In your world",
                C_LABEL);
        text_at(f, &c, 24, 104, (a & SOMVR_VMA_PR_ORC) ? "An orc is out" : "No orc out", C_DIM);
        draw_button(f, &c, v, SOMVR_VMI_PR_GO, NULL);
        draw_button(f, &c, v, SOMVR_VMI_PR_BACK, NULL);
        draw_row(f, &c, v, SOMVR_VMI_PR_ORC, 0);
        draw_button(f, &c, v, SOMVR_VMI_PR_SPAWN, NULL);
        draw_button(f, &c, v, SOMVR_VMI_PR_FAR, NULL); /* build 18 */
        draw_button(f, &c, v, SOMVR_VMI_PR_GROUP, NULL); /* build 19 */
        draw_button(f, &c, v, SOMVR_VMI_PR_REMOVE, NULL);
        draw_button(f, &c, v, SOMVR_VMI_PR_ARROWS, NULL); /* build 17 */
        draw_button(f, &c, v, SOMVR_VMI_PR_HEALTH, NULL);
        text_at(f, &c, 24, PR_HINT_Y, "No enemies here.", C_DIM);
        text_at(f, &c, 24, PR_HINT_Y + 32, "Talion cannot die here.", C_DIM);
        /* build 17: how to defend against the orc (somvr_defence.h: the blade in the blow's path; the red warning's
         * attacks - cannot parry / block - only dodged: the head 30 cm aside or 40 cm back) */
        text_at(f, &c, 24, PR_HINT_Y + 72, "Block: sword across a blow.", C_LABEL);
        text_at(f, &c, 24, PR_HINT_Y + 104, "Red: can't block, step aside.", C_LABEL);
    }
    draw_button(f, &c, v, SOMVR_VMI_CLOSE, NULL);
}

/* ---- the gesture and the panel's pose ------------------------------------------------------------------------------ */
void somvr_vm_gcfg_default(somvr_vm_gcfg *c) {
    *c = (somvr_vm_gcfg){.face_on = 0.70710678f, .face_off = 0.5f, .look_on = 0.76604444f, .look_off = 0.5f,
                         .reach_m = 0.8f, .hold_s = 0.5f, .rearm_s = 0.25f, .away_s = 0.5f, .lost_s = 1.0f,
                         .up_m = 0.15f, .out_m = 0.05f, .gate_s = 0.3f};
}

static void qrot(const float q[4], const float v[3], float out[3]) { /* v' = v + 2w (u x v) + 2 u x (u x v) */
    float u[3] = {q[0], q[1], q[2]}, w = q[3];
    float t[3] = {2 * (u[1] * v[2] - u[2] * v[1]), 2 * (u[2] * v[0] - u[0] * v[2]), 2 * (u[0] * v[1] - u[1] * v[0])};
    float x[3] = {v[0] + w * t[0] + (u[1] * t[2] - u[2] * t[1]), v[1] + w * t[1] + (u[2] * t[0] - u[0] * t[2]),
                  v[2] + w * t[2] + (u[0] * t[1] - u[1] * t[0])};
    memcpy(out, x, sizeof x);
}
static float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static int unit_quat(const float q[4]) {
    float n = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    return isfinite(n) && n > 0.9f && n < 1.1f;
}
/* the unit quaternion of the rotation whose matrix has the orthonormal right-handed columns x, y, z */
static void quat_of_basis(const float x[3], const float y[3], const float z[3], float q[4]) {
    float tr = x[0] + y[1] + z[2];
    if (tr > 0) {
        float s = sqrtf(tr + 1.0f) * 2;
        q[3] = 0.25f * s, q[0] = (y[2] - z[1]) / s, q[1] = (z[0] - x[2]) / s, q[2] = (x[1] - y[0]) / s;
    } else if (x[0] > y[1] && x[0] > z[2]) {
        float s = sqrtf(1.0f + x[0] - y[1] - z[2]) * 2;
        q[3] = (y[2] - z[1]) / s, q[0] = 0.25f * s, q[1] = (y[0] + x[1]) / s, q[2] = (z[0] + x[2]) / s;
    } else if (y[1] > z[2]) {
        float s = sqrtf(1.0f + y[1] - x[0] - z[2]) * 2;
        q[3] = (z[0] - x[2]) / s, q[0] = (y[0] + x[1]) / s, q[1] = 0.25f * s, q[2] = (z[1] + y[2]) / s;
    } else {
        float s = sqrtf(1.0f + z[2] - x[0] - y[1]) * 2;
        q[3] = (x[1] - y[0]) / s, q[0] = (z[0] + x[2]) / s, q[1] = (z[1] + y[2]) / s, q[2] = 0.25f * s;
    }
    float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int i = 0; i < 4; i++) q[i] /= n;
}

void somvr_vm_palm(const somvr_posef *grip, int hand, float n[3]) {
    static const float px[3] = {1, 0, 0};
    qrot(grip->q, px, n);
    float l = sqrtf(dot3(n, n));
    float s = (hand == SOMVR_HAND_R ? -1.0f : 1.0f) / (l > 0 ? l : 1);
    n[0] *= s, n[1] *= s, n[2] *= s;
}

int somvr_vm_panel_pose(const somvr_posef *grip, int hand, const somvr_posef *head, const somvr_vm_gcfg *c,
                        somvr_posef *out) {
    if (!unit_quat(grip->q) || !isfinite(grip->p[0] + grip->p[1] + grip->p[2])) return 0;
    float z[3], y[3], x[3];
    somvr_vm_palm(grip, hand, z);
    static const float up[3] = {0, 1, 0}, hup0[3] = {0, 1, 0}, fwd0[3] = {0, 0, -1};
    float refs[3][3];
    int nr = 0;
    memcpy(refs[nr++], up, sizeof up);
    if (head && unit_quat(head->q)) qrot(head->q, hup0, refs[nr++]);
    qrot(grip->q, fwd0, refs[nr++]); /* the last resort: never parallel to the palm's normal */
    for (int k = 0; k < nr; k++) {
        float p = dot3(refs[k], z);
        for (int i = 0; i < 3; i++) y[i] = refs[k][i] - p * z[i];
        float l = sqrtf(dot3(y, y));
        if (l > 0.2f || k == nr - 1) {
            for (int i = 0; i < 3; i++) y[i] /= l > 0 ? l : 1;
            break;
        }
    }
    x[0] = y[1] * z[2] - y[2] * z[1], x[1] = y[2] * z[0] - y[0] * z[2], x[2] = y[0] * z[1] - y[1] * z[0];
    quat_of_basis(x, y, z, out->q);
    for (int i = 0; i < 3; i++) out->p[i] = grip->p[i] + c->up_m * y[i] + c->out_m * z[i];
    return 1;
}

static const char *const g_why[] = {"none", "the palm gesture", "the menu button", "looked away", "the hand lost",
                                    "its close box", "off", "the wrist turned over (the watch)",
                                    "the bow came out in its hand"};
const char *somvr_vm_why(int why) { return why >= 0 && why <= SOMVR_VMW_BOW ? g_why[why] : "?"; }

void somvr_vm_greset(somvr_vm_gstate *g) {
    memset(g, 0, sizeof *g);
    g->armed = 1;
    g->away_t = g->nf_t = g->lost_t = g->gate_t = -1;
}

void somvr_vm_gate_text(uint32_t bits, char *buf, size_t cap) {
    static const char *const w[] = {"the bow", "a climb hold", "a blade swing", "an attack incoming"};
    size_t n = 0;
    if (!cap) return;
    buf[0] = 0;
    for (int i = 0; i < 4; i++)
        if (bits >> i & 1u) {
            int k = snprintf(buf + n, cap - n, "%s%s", n ? "+" : "", w[i]);
            if (k < 0 || (size_t)k >= cap - n) return;
            n += (size_t)k;
        }
    if (!n) snprintf(buf, cap, "nothing");
}

int somvr_vm_gesture(somvr_vm_gstate *g, const somvr_vm_gcfg *c, const somvr_vm_gin *in, somvr_posef *panel,
                     int *have_panel) {
    double t = in->t;
    int have = in->hand_ok && somvr_vm_panel_pose(&in->grip, in->hand, in->head_ok ? &in->head : NULL, c, panel);
    *have_panel = have;
    int face = 0, gaze = 0;
    float look = -2;
    if (have && in->head_ok && unit_quat(in->head.q)) {
        float n[3], v[3], w[3], fwd[3];
        static const float f0[3] = {0, 0, -1};
        somvr_vm_palm(&in->grip, in->hand, n);
        for (int i = 0; i < 3; i++) v[i] = in->head.p[i] - in->grip.p[i], w[i] = panel->p[i] - in->head.p[i];
        float dv = sqrtf(dot3(v, v)), dw = sqrtf(dot3(w, w));
        qrot(in->head.q, f0, fwd);
        float cf = dv > 0.05f ? dot3(n, v) / dv : -2;
        look = dw > 0.01f ? dot3(fwd, w) / dw : -2;
        gaze = 1;
        face = dv <= c->reach_m && (g->facing ? cf > c->face_off && look > c->look_off
                                              : cf > c->face_on && look > c->look_on);
    }
    if ((face && !g->facing) || (g->open && in->busy)) g->since = t; /* the hold counts while not pointing at it */
    if (face) g->nf_t = -1;
    else if (g->nf_t < 0) g->nf_t = t;
    g->facing = face;
    if (!g->armed && !face && t - g->nf_t >= c->rearm_s) g->armed = 1;
    /* the gate: the hands are busy (the bow, a climb, a swing, an attack incoming) and gate_s after it - a closed
     * panel's gesture is disarmed every frame, so a palm held through it must turn away and face again */
    uint32_t blk = in->block & SOMVR_VMG_ALL;
    if (blk) g->gate_t = t;
    int gate = blk || (g->gate_t >= 0 && t - g->gate_t < c->gate_s);
    g->gate_why = gate ? g->gate_why | blk : 0;
    g->gated = gate && !g->open;
    if (g->gated) {
        g->armed = 0;
        g->gate_frames++;
        if (face && !g->held) g->held = 1, g->held_off++;
    }
    if (!face) g->held = 0;
    int ev = 0;
    if (in->toggles & 1u) { /* the menu button (an even count: open and closed again) */
        g->open = !g->open, g->why = SOMVR_VMW_BUTTON, g->armed = 0;
        ev = g->open ? 1 : -1;
    } else if (g->armed && face && t - g->since >= c->hold_s && !(g->open && in->busy)) {
        g->open = !g->open, g->why = SOMVR_VMW_GESTURE, g->armed = 0;
        ev = g->open ? 1 : -1;
    }
    if (ev > 0) g->away_t = g->lost_t = -1;
    if (g->open && !ev) {
        if (gaze && look <= c->look_off && !in->busy) {
            if (g->away_t < 0) g->away_t = t;
            else if (t - g->away_t >= c->away_s) g->open = 0, g->why = SOMVR_VMW_AWAY, ev = -1;
        } else {
            g->away_t = -1;
        }
        if (!ev && !in->hand_ok) {
            if (g->lost_t < 0) g->lost_t = t;
            else if (t - g->lost_t >= c->lost_s) g->open = 0, g->why = SOMVR_VMW_LOST, ev = -1;
        } else if (in->hand_ok) {
            g->lost_t = -1;
        }
    }
    g->opens += ev > 0, g->closes += ev < 0;
    return ev;
}

/* ---- build 20: the pointer's lock on the panel's parts (SOMVR_XR_PTR_SNAP) ------------------------------------- */
void somvr_vm_snapcfg_default(somvr_vm_snapcfg *c) {
    *c = (somvr_vm_snapcfg){.hold_deg = 1.0f, .magnet_deg = 2.0f, .min_m = 0.003f};
}
void somvr_vm_snap_reset(somvr_vm_snap *s) {
    memset(s, 0, sizeof *s);
    s->lock = s->raw = (somvr_vm_hit){-1, SOMVR_VMP_NONE};
}
/* the parts a pointer can press on page (the hit test's: the buttons, then each shown row's < > and the row itself;
 * the card: its close box alone) */
static int snap_parts(uint64_t avail, int page, int card, somvr_vm_hit *out, int cap) {
    int n = 0, rc[4];
    if (card) {
        out[n++] = (somvr_vm_hit){SOMVR_VMI_CLOSE, SOMVR_VMP_BUTTON};
        return n;
    }
    const int *bl = page_buttons(page);
    for (int i = 0; i < 14 && bl[i] >= 0 && n < cap; i++)
        if (somvr_vm_rect_av(avail, bl[i], SOMVR_VMP_BUTTON, rc)) out[n++] = (somvr_vm_hit){bl[i], SOMVR_VMP_BUTTON};
    if (page == SOMVR_VMPG_CONTROLS || page == SOMVR_VMPG_TEST) return n;
    int r0 = page == SOMVR_VMPG_PRACTICE ? SOMVR_VMI_PR_ORC : 0, r1 = page == SOMVR_VMPG_PRACTICE ? r0 + 1 : SOMVR_VMI_ROWS;
    for (int item = r0; item < r1; item++) {
        if (page != SOMVR_VMPG_PRACTICE &&
            somvr_vm_row_page(item) != (page == SOMVR_VMPG_FIGHT ? SOMVR_VMPG_FIGHT : SOMVR_VMPG_SETTINGS))
            continue;
        if (!somvr_vm_rect_av(avail, item, SOMVR_VMP_ROW, rc)) continue;
        for (int part = SOMVR_VMP_ROW; part <= SOMVR_VMP_MORE && n < cap; part++) out[n++] = (somvr_vm_hit){item, part};
    }
    return n;
}
/* texels from (x, y) to the part's own area: its rect (0 inside); a row's own area leaves out its < > boxes (inside one:
 * none - the small target first: the point on a box takes the box from its row at once, the box then holds) */
static float own_dist(uint64_t avail, somvr_vm_hit p, float x, float y) {
    int rc[4];
    if (!somvr_vm_rect_av(avail, p.item, p.part, rc)) return 1e9f;
    float dx = fmaxf(fmaxf((float)rc[0] - x, x - (float)(rc[0] + rc[2])), 0);
    float dy = fmaxf(fmaxf((float)rc[1] - y, y - (float)(rc[1] + rc[3])), 0);
    float d = sqrtf(dx * dx + dy * dy);
    if (p.part == SOMVR_VMP_ROW && d == 0)
        for (int part = SOMVR_VMP_LESS; part <= SOMVR_VMP_MORE; part++)
            if (somvr_vm_rect_av(avail, p.item, part, rc) && in_rect(rc, x, y)) d = 1e9f;
    return d;
}
somvr_vm_hit somvr_vm_snap_step(somvr_vm_snap *s, const somvr_vm_snapcfg *c, const somvr_settings *set, uint64_t avail,
                                int page, int card, const float uv[2], float dist, float quad_w_m, float dot[2]) {
    dot[0] = uv[0], dot[1] = uv[1];
    somvr_vm_hit none = {-1, SOMVR_VMP_NONE}, raw = somvr_vm_hit_test_av(avail, card ? SOMVR_VMPG_CONTROLS : page, uv[0], uv[1]);
    if (card && raw.item != SOMVR_VMI_CLOSE) raw = none; /* the card: its X alone */
    if (raw.item != s->raw.item || raw.part != s->raw.part) s->raw_changes++, s->raw = raw;
    float x = uv[0] * SOMVR_VM_TEX_W, y = uv[1] * SOMVR_VM_TEX_H;
    float tpm = quad_w_m > 0 ? SOMVR_VM_TEX_W / quad_w_m : SOMVR_VM_TEX_W / SOMVR_VM_W_M, dd = dist > 0 ? dist : 0;
    float hold = fmaxf(c->min_m, dd * tanf(c->hold_deg * 0.01745329252f)) * tpm;
    float mag = fmaxf(c->min_m, dd * tanf(c->magnet_deg * 0.01745329252f)) * tpm;
    somvr_vm_hit parts[64], lock = none;
    int n = snap_parts(avail, page, card, parts, 64);
    if (!(x == x && y == y)) n = 0; /* NaN: nothing */
    for (int i = 0; i < n && s->lock.item >= 0; i++) /* the lock holds while the point stays near its own area */
        if (parts[i].item == s->lock.item && parts[i].part == s->lock.part && own_dist(avail, parts[i], x, y) <= hold)
            lock = s->lock;
    if (lock.item < 0 && n && raw.item >= 0) lock = raw; /* the part under the point */
    if (lock.item < 0) { /* on none: the nearest one that can be used, within the magnet's reach */
        float best = mag;
        for (int i = 0; i < n; i++) {
            float d = own_dist(avail, parts[i], x, y);
            if (d <= best && somvr_vm_enabled(set, avail, parts[i].item)) best = d, lock = parts[i];
        }
    }
    if (lock.item != s->lock.item || lock.part != s->lock.part) s->changes++, s->lock = lock;
    int rc[4];
    if (lock.item >= 0 && somvr_vm_rect_av(avail, lock.item, lock.part, rc)) { /* the dot kept on the locked part */
        float ix = fminf(4.0f, 0.5f * (float)rc[2]), iy = fminf(4.0f, 0.5f * (float)rc[3]);
        float cx = fminf(fmaxf(x, (float)rc[0] + ix), (float)(rc[0] + rc[2]) - ix);
        float cy = fminf(fmaxf(y, (float)rc[1] + iy), (float)(rc[1] + rc[3]) - iy);
        dot[0] = cx / SOMVR_VM_TEX_W, dot[1] = cy / SOMVR_VM_TEX_H;
    }
    return lock;
}

/* ---- build 20: the panel held steady (SOMVR_XR_PANEL_STEADY) --------------------------------------------------- */
void somvr_vm_stcfg_default(somvr_vm_stcfg *c) {
    *c = (somvr_vm_stcfg){.hold_m = 0.015f, .hold_deg = 3.0f, .settle_s = 0.35f};
}
int somvr_vm_stcfg_parse(const char *v, somvr_vm_stcfg *c, int *bad) {
    somvr_vm_stcfg_default(c);
    if (!v || !*v || !strcmp(v, "off") || !strcmp(v, "0")) return 0;
    if (!strcmp(v, "on") || !strcmp(v, "1")) return 1;
    float f[3] = {-1, -1, -1};
    const char *q = v;
    int n = 0;
    for (;;) { /* three numbers, a comma between each two, nothing else */
        char *e = NULL;
        float x = strtof(q, &e);
        if (e == q || n == 3) break;
        f[n++] = x, q = e;
        if (*q != ',') break;
        q++;
    }
    float cm = f[0], deg = f[1], ms = f[2];
    if (n != 3 || *q || !(cm > 0 && cm <= 10) || !(deg > 0 && deg <= 20) || !(ms >= 0 && ms <= 3000)) {
        *bad = 1;
        return 0;
    }
    c->hold_m = cm * 0.01f, c->hold_deg = deg, c->settle_s = ms * 0.001f;
    return 1;
}
void somvr_vm_steady_reset(somvr_vm_steady *s) { memset(s, 0, sizeof *s); }
static float qangle(const float a[4], const float b[4]) { /* rad between two unit quaternions' rotations */
    float d = fabsf(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]);
    return 2.0f * acosf(d < 1 ? d : 1);
}
static void qslerp(const float a[4], const float b[4], float k, float o[4]) { /* the short way */
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3], sg = d < 0 ? -1.0f : 1.0f, wa, wb;
    d = fabsf(d);
    if (d > 0.9995f) {
        wa = 1 - k, wb = k;
    } else {
        float th = acosf(d), sn = sinf(th);
        wa = sinf((1 - k) * th) / sn, wb = sinf(k * th) / sn;
    }
    float q[4], n = 0;
    for (int i = 0; i < 4; i++) q[i] = wa * a[i] + sg * wb * b[i], n += q[i] * q[i];
    n = sqrtf(n);
    for (int i = 0; i < 4; i++) o[i] = n > 1e-6f ? q[i] / n : b[i];
}
void somvr_vm_steady_step(somvr_vm_steady *s, const somvr_vm_stcfg *c, const somvr_posef *raw, double t,
                          somvr_posef *out) {
    if (!unit_quat(raw->q) || !isfinite(raw->p[0] + raw->p[1] + raw->p[2])) { /* a bad pose: the last one stays */
        *out = s->have ? s->p : *raw;
        return;
    }
    double dt = s->have ? t - s->t : -1;
    if (!s->have || !(dt >= 0 && dt <= 0.25)) { /* the start, or a gap: at the hand's pose */
        s->p = *raw, s->have = 1, s->t = t, s->restarts++;
        *out = *raw;
        return;
    }
    s->frames++;
    if (dt > 0) {
        s->t = t;
        float r = c->hold_m > 1e-4f ? c->hold_m : 1e-4f, d[3], dm = 0;
        for (int i = 0; i < 3; i++) d[i] = raw->p[i] - s->p.p[i], dm += d[i] * d[i];
        dm = sqrtf(dm);
        int moved = 0;
        if (dm > r) { /* beyond the radius: follows by the excess, 1:1 */
            float k = (dm - r) / dm;
            for (int i = 0; i < 3; i++) s->p.p[i] += k * d[i];
            dm = r, moved = 1;
        }
        if (c->settle_s > 0) { /* inside: eases toward the hand, (offset / radius)^2 of the edge's rate */
            float w = dm / r, a = 1.0f - expf(-(float)dt * w * w / c->settle_s);
            for (int i = 0; i < 3; i++) s->p.p[i] += a * (raw->p[i] - s->p.p[i]);
        }
        float ar = (c->hold_deg > 0.01f ? c->hold_deg : 0.01f) * 0.01745329252f, ang = qangle(s->p.q, raw->q);
        if (ang > ar) {
            qslerp(s->p.q, raw->q, (ang - ar) / ang, s->p.q);
            ang = ar, moved = 1;
        }
        if (c->settle_s > 0) {
            float w = ang / ar, a = 1.0f - expf(-(float)dt * w * w / c->settle_s);
            qslerp(s->p.q, raw->q, a, s->p.q);
        }
        s->follows += moved;
    }
    *out = s->p;
}
void somvr_vm_corners(const somvr_posef *p, const float size[2], float out[4][3]) {
    for (int k = 0; k < 4; k++) {
        float l[3] = {(k & 1 ? 0.5f : -0.5f) * size[0], (k & 2 ? -0.5f : 0.5f) * size[1], 0}, w[3];
        qrot(p->q, l, w);
        for (int i = 0; i < 3; i++) out[k][i] = p->p[i] + w[i];
    }
}
