/* somvr_vrmenu.c - see somvr_vrmenu.h */
#include "somvr_vrmenu.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ==== 2. the settings ============================================================================================ */
void somvr_set_default(somvr_settings *s) {
    *s = (somvr_settings){.turn = 0, .snap_deg = 30, .turn_dps = 120, .vignette = 0, .blink_ms = 0, .seated = 0,
                          .height_cm = 0, .hud = 1, .hud_dist_m = 1.5f, .hand = 0, .view = 0, .haptics = 1.0f,
                          .step = 0, .dash = 0, .phase = 0, .sword_deg = 0, .music = 1.0f, .size = 1.0f};
}

static const char *const g_keys[SOMVR_SK_N] = {"TURN", "SNAP_DEG", "TURN_DPS", "VIGNETTE", "SNAP_BLINK_MS", "SEATED",
                                               "HEIGHT_CM", "HUD", "HUD_DIST_M", "HAND", "VIEW", "HAPTICS",
                                               "STEP", "DASH", "PHASE", "SWORD_ANGLE", "MUSIC", "PLAYER_SIZE"};
static const char *const g_envs[SOMVR_SK_N] = {"SOMVR_TURN", "SOMVR_SNAP_DEG", "SOMVR_TURN_DPS", "SOMVR_VIGNETTE",
                                               "SOMVR_SNAP_BLINK_MS", "SOMVR_SEATED", "SOMVR_HEIGHT_CM", NULL, NULL, NULL,
                                               NULL, "SOMVR_HAPTICS", "SOMVR_STEP", "SOMVR_DASH", "SOMVR_PHASE",
                                               "SOMVR_SWORD_ANGLE", "SOMVR_MUSIC",
                                               "SOMVR_PLAYER_SIZE"};
/* SOMVR_DASH / SOMVR_PHASE's words (somvr_step.h: SOMVR_ST_DASH_* / _PHASE_*) */
static const char *const g_dash_w[] = {"off", "on", "blink"}, *const g_phase_w[] = {"off", "on", "reface", "blink"};

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
    case SOMVR_SK_HUD: return (w = word_of(v, onoff_w, 4)) < 0 ? 0 : (s->hud = (uint32_t)(w & 1), 1);
    case SOMVR_SK_HAND: return (w = word_of(v, hand_w, 2)) < 0 ? 0 : (s->hand = (uint32_t)w, 1);
    case SOMVR_SK_VIEW: return (w = word_of(v, view_w, 2)) < 0 ? 0 : (s->view = (uint32_t)w, 1);
    case SOMVR_SK_DASH: return (w = word_of(v, g_dash_w, 3)) < 0 ? 0 : (s->dash = (uint32_t)w, 1);
    case SOMVR_SK_PHASE: return (w = word_of(v, g_phase_w, 4)) < 0 ? 0 : (s->phase = (uint32_t)w, 1);
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
                                                    "Music", /* v5 */
                                                    "Player size",
                                                    "Recentre view", "X",
                                                    /* v3: the tab, the practice page */
                                                    "Practice", "Go to practice area", "Back to where I was",
                                                    "Orc type", "Spawn an orc", "Remove the orc",
                                                    /* the settings page's bottom row (somvr_session.h) */
                                                    "Report", "Quit game",
                                                    /* build 6: the other two tabs */
                                                    "Settings", "Controls"};
const char *somvr_vm_label(int item) { return item >= 0 && item < SOMVR_VMI_ALL ? g_labels[item] : ""; }
static const char *const g_orc_labels[3] = {"Infantry", "Shield", "Berserker"}; /* = somvr_pr_orc_label */

int somvr_vm_enabled(const somvr_settings *s, uint32_t avail, int item) {
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
    case SOMVR_VMI_MUSIC: return (avail & SOMVR_VMA_MUSIC) != 0;
    case SOMVR_VMI_SIZE: return (avail & SOMVR_VMA_SIZE) != 0;
    case SOMVR_VMI_TURN:
    case SOMVR_VMI_SEATED:
    case SOMVR_VMI_HAND:
    case SOMVR_VMI_HAPTICS:
    case SOMVR_VMI_RECENTER:
    case SOMVR_VMI_CLOSE:
    case SOMVR_VMI_TAB_SET:
    case SOMVR_VMI_TAB_CTL: return 1;
    case SOMVR_VMI_TAB:
    case SOMVR_VMI_PR_ORC: return (avail & SOMVR_VMA_PRACTICE) != 0;
    case SOMVR_VMI_PR_GO: return (avail & SOMVR_VMA_PRACTICE) && !(avail & (SOMVR_VMA_PR_HERE | SOMVR_VMA_PR_BUSY));
    case SOMVR_VMI_PR_BACK:
        return (avail & SOMVR_VMA_PRACTICE) && (avail & SOMVR_VMA_PR_BACK) && !(avail & SOMVR_VMA_PR_BUSY);
    case SOMVR_VMI_PR_SPAWN:
        return (avail & SOMVR_VMA_PRACTICE) && (avail & SOMVR_VMA_PR_HERE) && !(avail & SOMVR_VMA_PR_BUSY);
    case SOMVR_VMI_PR_REMOVE:
        return (avail & SOMVR_VMA_PRACTICE) && (avail & SOMVR_VMA_PR_ORC) && !(avail & SOMVR_VMA_PR_BUSY);
    case SOMVR_VMI_REPORT: return (avail & SOMVR_VMA_MARK) != 0;
    case SOMVR_VMI_QUIT: return (avail & SOMVR_VMA_QUIT) != 0;
    default: return 0;
    }
}

int somvr_vm_tab_page(int item) {
    return item == SOMVR_VMI_TAB_SET ? SOMVR_VMPG_SETTINGS
           : item == SOMVR_VMI_TAB   ? SOMVR_VMPG_PRACTICE
           : item == SOMVR_VMI_TAB_CTL ? SOMVR_VMPG_CONTROLS
                                       : -1;
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
static const char *const g_ctl[][2] = {
    {"Walk", "left stick"},         {"Sprint", "click left stick"},  {"Turn", "R stick, down=crouch"},
    {"Sword", "grip at R shoulder"}, {"Sheathe", "grip there again"}, {"Parry", "move sword into blow"},
    {"Use", "B (interact)"},        {"Drop", "A (on a wall)"},
    {"Bow", "hold left trigger"},   {"Shoot", "R grip, pull, let go"}, /* build 11 (SOMVR_BOW): Swing/Crouch made room */
    {"Wraith", "hold Y (vision)"},  {"Map", "click right stick"},   {"Minimap", "back of left wrist"},
    {"Menu", "left palm to face"},  {"", "or hold menu button"},    {"Pause", "tap menu button"},
    {"Quit", "Settings > Quit x2"}, {"Report", SOMVR_VM_CTL_REPORT}, {"", SOMVR_VM_CTL_REPORT2},
};
static const char *const g_ctl_notes[SOMVR_VM_CTL_NOTES] = {"Not yet: dash, wraith phase,", "finishers, Shadow Strike"};
int somvr_vm_ctl_dy(void) {
    const char *k, *w;
    int n = somvr_vm_controls(0, &k, &w) + SOMVR_VM_CTL_NOTES + 1; /* + the card's close hint */
    int room = SOMVR_VM_TEX_H - 16 - 4 - SOMVR_VM_CTL_Y0;         /* the panel's bottom margin, the notes' gap */
    int dy = n > 0 ? room / n : SOMVR_VM_CTL_DY;
    return dy > SOMVR_VM_CTL_DY ? SOMVR_VM_CTL_DY : dy < SOMVR_FONT_H * SOMVR_VM_SCALE ? SOMVR_FONT_H * SOMVR_VM_SCALE : dy;
}
const char *somvr_vm_controls_note(int i) { return i >= 0 && i < SOMVR_VM_CTL_NOTES ? g_ctl_notes[i] : NULL; }
int somvr_vm_controls(int i, const char **key, const char **what) {
    int n = 0;
    *key = *what = NULL;
    for (size_t k = 0; k < sizeof g_ctl / sizeof g_ctl[0]; k++) {
        if (!g_ctl[k][1][0]) continue; /* an empty placeholder: no line */
        if (n == i) *key = g_ctl[k][0], *what = g_ctl[k][1];
        n++;
    }
    return n;
}

int somvr_vm_practice_action(int item) {
    switch (item) {
    case SOMVR_VMI_PR_GO: return SOMVR_VMPR_GO;
    case SOMVR_VMI_PR_BACK: return SOMVR_VMPR_BACK;
    case SOMVR_VMI_PR_SPAWN: return SOMVR_VMPR_SPAWN;
    case SOMVR_VMI_PR_REMOVE: return SOMVR_VMPR_REMOVE;
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
    case SOMVR_VMI_MUSIC: pct(s->music, buf, cap); break;
    case SOMVR_VMI_SIZE: snprintf(buf, cap, "%ld%%", lroundf(s->size * 100)); break; /* "115%" */
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
                   g_size[] = {1.0f, 1.05f, 1.1f, 1.15f, 1.2f, 1.25f, 1.3f, 1.35f, 1.4f};
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

int somvr_vm_apply(somvr_settings *s, uint32_t avail, uint32_t blink_on_ms, int item, int part) {
    if (!somvr_vm_enabled(s, avail, item)) return SOMVR_VMR_NONE;
    if (item == SOMVR_VMI_RECENTER) return part == SOMVR_VMP_BUTTON ? SOMVR_VMR_RECENTER : SOMVR_VMR_NONE;
    if (item == SOMVR_VMI_CLOSE) return part == SOMVR_VMP_BUTTON ? SOMVR_VMR_CLOSE : SOMVR_VMR_NONE;
    if (somvr_vm_tab_page(item) >= 0) return part == SOMVR_VMP_BUTTON ? SOMVR_VMR_PAGE : SOMVR_VMR_NONE;
    if (item == SOMVR_VMI_REPORT) return part == SOMVR_VMP_BUTTON ? SOMVR_VMR_REPORT : SOMVR_VMR_NONE;
    if (item == SOMVR_VMI_QUIT) return part == SOMVR_VMP_BUTTON ? SOMVR_VMR_QUIT : SOMVR_VMR_NONE;
    if (somvr_vm_practice_action(item)) return part == SOMVR_VMP_BUTTON ? SOMVR_VMR_PRACTICE : SOMVR_VMR_NONE;
    if (part != SOMVR_VMP_ROW && part != SOMVR_VMP_LESS && part != SOMVR_VMP_MORE) return SOMVR_VMR_NONE;
    int dir = part == SOMVR_VMP_LESS ? -1 : 1, wrap = part == SOMVR_VMP_ROW;
    switch (item) {
    case SOMVR_VMI_TURN: s->turn = !s->turn; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_BLINK: s->blink_ms = s->blink_ms ? 0 : (blink_on_ms ? blink_on_ms : SOMVR_SET_BLINK_MS); return 1;
    case SOMVR_VMI_SEATED: s->seated = !s->seated; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_HUD: s->hud = !s->hud; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_HAND: s->hand = !s->hand; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_VIEW: s->view = !s->view; return SOMVR_VMR_CHANGED;
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
/* the practice page: the state lines at 70 and 104, then the buttons and the orc type row */
#define PR_GO_Y 140
#define PR_BACK_Y 206
#define PR_ORC_Y 280
#define PR_SPAWN_Y 336
#define PR_REMOVE_Y 402
#define PR_HINT_Y 486

int somvr_vm_shown(uint32_t avail, int item) {
    switch (item) {
    case SOMVR_VMI_STEP:
    case SOMVR_VMI_DASH:
    case SOMVR_VMI_PHASE: return (avail & SOMVR_VMA_MOVES) != 0;
    case SOMVR_VMI_MUSIC: return (avail & SOMVR_VMA_MUSIC) != 0;
    case SOMVR_VMI_SIZE: return (avail & SOMVR_VMA_SIZE) != 0; /* the player size: first person on the head */
    default: return item >= 0 && item < SOMVR_VMI_ALL;
    }
}
/* the value row item's slot among the shown rows (-1 = not shown), the count shown and the rows' height */
static int row_slot(uint32_t avail, int item, int *n_shown) {
    int slot = -1, n = 0;
    for (int i = 0; i < SOMVR_VMI_ROWS; i++)
        if (somvr_vm_shown(avail, i)) {
            if (i == item) slot = n;
            n++;
        }
    if (n_shown) *n_shown = n;
    return slot;
}
static int row_h(int n) { return n <= ROW_FIT ? ROW_H : ROW_FIT * ROW_H / n; }

int somvr_vm_rect(int item, int part, int rc[4]) { return somvr_vm_rect_av(0, item, part, rc); }
int somvr_vm_rect_av(uint32_t avail, int item, int part, int rc[4]) {
    rc[0] = rc[1] = rc[2] = rc[3] = 0;
    if (item == SOMVR_VMI_CLOSE || item == SOMVR_VMI_RECENTER || somvr_vm_tab_page(item) >= 0 ||
        item == SOMVR_VMI_REPORT || item == SOMVR_VMI_QUIT || somvr_vm_practice_action(item)) {
        static const int by[] = {PR_GO_Y, PR_BACK_Y, 0, PR_SPAWN_Y, PR_REMOVE_Y};
        if (part != SOMVR_VMP_BUTTON) return 0;
        if (item == SOMVR_VMI_CLOSE) rc[0] = 452, rc[1] = 10, rc[2] = 48, rc[3] = 44;
        else if (somvr_vm_tab_page(item) >= 0) /* the tab strip: Settings, Practice, Controls */
            rc[0] = 12 + 144 * somvr_vm_tab_page(item), rc[1] = 10, rc[2] = 140, rc[3] = 44;
        /* the settings page's bottom row: Recentre | Report | Quit game (the last two: somvr_session.h) */
        /* three equal buttons from the rows' labels to the > boxes' right edge (16 .. 496), 12 apart */
        else if (item == SOMVR_VMI_RECENTER) rc[0] = 16, rc[1] = RECENTER_Y, rc[2] = 152, rc[3] = 56;
        else if (item == SOMVR_VMI_REPORT) rc[0] = 180, rc[1] = RECENTER_Y, rc[2] = 152, rc[3] = 56;
        else if (item == SOMVR_VMI_QUIT) rc[0] = 344, rc[1] = RECENTER_Y, rc[2] = 152, rc[3] = 56;
        else rc[0] = 40, rc[1] = by[item - SOMVR_VMI_PR_GO], rc[2] = SOMVR_VM_TEX_W - 80, rc[3] = 56;
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
somvr_vm_hit somvr_vm_hit_test(float u, float v) { return somvr_vm_hit_test_av(0, SOMVR_VMPG_SETTINGS, u, v); }
somvr_vm_hit somvr_vm_hit_test_page(int page, float u, float v) { return somvr_vm_hit_test_av(0, page, u, v); }
somvr_vm_hit somvr_vm_hit_test_av(uint32_t avail, int page, float u, float v) {
    somvr_vm_hit h = {-1, SOMVR_VMP_NONE};
    if (!(u >= 0 && u <= 1 && v >= 0 && v <= 1)) return h; /* NaN too */
    float x = u * SOMVR_VM_TEX_W, y = v * SOMVR_VM_TEX_H;
    int rc[4];
    static const int buttons[3][9] = {
        {SOMVR_VMI_RECENTER, SOMVR_VMI_REPORT, SOMVR_VMI_QUIT, SOMVR_VMI_CLOSE, SOMVR_VMI_TAB, SOMVR_VMI_TAB_SET,
         SOMVR_VMI_TAB_CTL, -1},
        {SOMVR_VMI_CLOSE, SOMVR_VMI_TAB, SOMVR_VMI_TAB_SET, SOMVR_VMI_TAB_CTL, SOMVR_VMI_PR_GO, SOMVR_VMI_PR_BACK,
         SOMVR_VMI_PR_SPAWN, SOMVR_VMI_PR_REMOVE, -1},
        {SOMVR_VMI_CLOSE, SOMVR_VMI_TAB, SOMVR_VMI_TAB_SET, SOMVR_VMI_TAB_CTL, -1}};
    const int *bl = buttons[page == SOMVR_VMPG_PRACTICE ? 1 : page == SOMVR_VMPG_CONTROLS ? 2 : 0];
    for (int i = 0; i < 9 && bl[i] >= 0; i++)
        if (somvr_vm_rect(bl[i], SOMVR_VMP_BUTTON, rc) && in_rect(rc, x, y)) return (somvr_vm_hit){bl[i], SOMVR_VMP_BUTTON};
    if (page == SOMVR_VMPG_CONTROLS) return h; /* text only */
    int r0 = page == SOMVR_VMPG_PRACTICE ? SOMVR_VMI_PR_ORC : 0, r1 = page == SOMVR_VMPG_PRACTICE ? r0 + 1 : SOMVR_VMI_ROWS;
    for (int item = r0; item < r1; item++) {
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
                     C_WARN_HOV[3] = {208, 72, 56};

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
        char g[2] = {(char)(part == SOMVR_VMP_LESS ? SOMVR_FONT_LEFT : SOMVR_FONT_RIGHT), 0};
        text_mid(f, c, b[0], b[0] + b[2], ty, g, on ? C_VALUE : C_DIM);
    }
    char val[32];
    somvr_vm_value(&v->set, item, val, sizeof val);
    text_mid(f, c, VALUE_X0, MORE_X, ty, val, on ? C_VALUE : C_DIM);
}
/* a button: lit when hovered, dimmed when disabled; label NULL = its own */
static void draw_button(const somvr_font *f, const canvas *c, const somvr_vm_view *v, int item, const char *label) {
    int rc[4], on = somvr_vm_enabled(&v->set, v->avail, item), hov = v->hover.item == item;
    int warn = on && item == SOMVR_VMI_QUIT && v->quit_armed; /* pressed once: red, "Confirm" */
    somvr_vm_rect(item, SOMVR_VMP_BUTTON, rc);
    fill_rc(c, rc, !on ? C_BOX_DIM : warn ? (hov ? C_WARN_HOV : C_WARN) : hov ? C_BOX_HOV : C_BOX);
    text_mid(f, c, rc[0], rc[0] + rc[2], rc[1] + (rc[3] - SOMVR_FONT_H * SOMVR_VM_SCALE) / 2,
             label ? label : somvr_vm_label(item), on ? C_VALUE : C_DIM);
}

void somvr_vm_draw(const somvr_font *f, uint8_t *px, int bgra, const somvr_vm_view *v) {
    canvas c = {px, bgra};
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
    int pr = v->page == SOMVR_VMPG_PRACTICE, ctl = v->page == SOMVR_VMPG_CONTROLS;
    if (v->card) { /* the arrival card: a title instead of the tabs */
        text_at(f, &c, 24, 14, "CONTROLS", C_TITLE);
    } else { /* the tab strip: the page's own tab lit as the title (underlined), the others as buttons */
        static const int tabs[3] = {SOMVR_VMI_TAB_SET, SOMVR_VMI_TAB, SOMVR_VMI_TAB_CTL};
        for (int i = 0; i < 3; i++) {
            if (!somvr_vm_enabled(&v->set, v->avail, tabs[i])) continue;
            int rc[4];
            somvr_vm_rect(tabs[i], SOMVR_VMP_BUTTON, rc);
            if (somvr_vm_tab_page(tabs[i]) == v->page) {
                fill(&c, rc[0] + 8, rc[1] + rc[3] - 4, rc[2] - 16, 3, C_TITLE);
                text_mid(f, &c, rc[0], rc[0] + rc[2], rc[1] + (rc[3] - SOMVR_FONT_H * SOMVR_VM_SCALE) / 2,
                         somvr_vm_label(tabs[i]), C_TITLE);
            } else {
                draw_button(f, &c, v, tabs[i], NULL);
            }
        }
    }
    fill(&c, 16, 58, SOMVR_VM_TEX_W - 32, 2, C_BORDER);
    if (ctl) { /* the controls: key, what does it; what is not in this build; on the card how to close it */
        const char *k, *w;
        int y = SOMVR_VM_CTL_Y0, n = somvr_vm_controls(0, &k, &w), dy = somvr_vm_ctl_dy();
        for (int i = 0; i < n; i++, y += dy) {
            somvr_vm_controls(i, &k, &w);
            text_at(f, &c, 20, y, k, C_TITLE);
            text_at(f, &c, 20 + 16 * 8, y, w, C_VALUE);
        }
        y += 4;
        for (int i = 0; i < SOMVR_VM_CTL_NOTES; i++, y += dy)
            text_at(f, &c, 20, y, somvr_vm_controls_note(i), C_DIM);
        if (v->card) text_at(f, &c, 20, y, "Close: X box or A", C_LABEL);
    } else if (!pr) {
        int last = -1; /* the separator under every shown row but the last */
        for (int item = 0; item < SOMVR_VMI_ROWS; item++)
            if (somvr_vm_shown(v->avail, item)) last = item;
        for (int item = 0; item < SOMVR_VMI_ROWS; item++) draw_row(f, &c, v, item, item != last);
        draw_button(f, &c, v, SOMVR_VMI_RECENTER, "Recentre");
        draw_button(f, &c, v, SOMVR_VMI_REPORT, NULL);
        draw_button(f, &c, v, SOMVR_VMI_QUIT, v->quit_armed ? "Confirm" : "Quit");
    } else { /* the state, the buttons, the orc type, a hint */
        uint32_t a = v->avail;
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
        draw_button(f, &c, v, SOMVR_VMI_PR_REMOVE, NULL);
        text_at(f, &c, 24, PR_HINT_Y, "No enemies here.", C_DIM);
        text_at(f, &c, 24, PR_HINT_Y + 34, "Talion cannot die here.", C_DIM);
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
                                    "its close box", "off", "the wrist turned over (the watch)"};
const char *somvr_vm_why(int why) { return why >= 0 && why <= SOMVR_VMW_TURNED ? g_why[why] : "?"; }

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
