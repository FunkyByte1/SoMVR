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
                          .counter_icon = 1,
                          /* build 34 (v8): the switches' code defaults (the watch at any angle and 10 cm, no low
                           * health, the heartbeat at 20 %, the force as tuned, the ghost hand, the game's kills off,
                           * the execution by the blow, the shove as tuned, a hold up to 10 s, the knife held while
                           * gripped, the herbs by hand, no fling down, no sword on the wall) */
                          .watch_big = 1, .heartbeat = 1, .force = 1, .ghost = 1, .exec_blow = 1, .shove = 1,
                          .hold_s = 10, .knife_hold = 1, .herbs = 1};
}

static const char *const g_keys[SOMVR_SK_N] = {"TURN", "SNAP_DEG", "TURN_DPS", "VIGNETTE", "SNAP_BLINK_MS", "SEATED",
                                               "HEIGHT_CM", "HUD", "HUD_DIST_M", "HAND", "VIEW", "HAPTICS",
                                               "STEP", "DASH", "PHASE", "SWORD_ANGLE", "MUSIC", "PLAYER_SIZE", "CROUCH",
                                               "CLIMB_FLING", "HANDS_SURFACE", "SWORD_WEIGHT", "SWORD_WEIGHT_HAND",
                                               "BOW_QUIVER", "COMBAT_BITE",
                                               /* build 19 (re-xr-pipeline.md 73) */
                                               "DODGE", "WRAITH_STEP", "STEALTH_KILL", "CROWD_PACE_MS", "COUNTER_ICON",
                                               "BOW_STRING",
                                               /* build 27 (re-xr-pipeline.md 112) */
                                               "MOVE",
                                               /* build 27 (re-xr-pipeline.md 119) */
                                               "XR_REFRESH",
                                               /* build 30 (re-xr-pipeline.md 132) */
                                               "HANDED",
                                               /* build 31 (re-xr-pipeline.md 136) */
                                               "TEST_PAGE",
                                               /* build 33 (re-xr-pipeline.md 142) */
                                               "BOW_TAKE",
                                               /* build 34 (re-xr-pipeline.md 150) */
                                               "XR_WATCH_LOOK", "WATCH_SIZE", "LOW_HEALTH", "HEARTBEAT",
                                               "FORCE_NEEDED", "SWORD_PHYS_GHOST", "KILLS", "KILLS_EXEC",
                                               "SHOVE_PUSH", "VERBS_GRAB_MAX_S", "SLOTS_DAGGER_HOLD", "HERBS",
                                               "CLIMB_FLING_DOWN", "CLIMB_SWORD",
                                               /* b37 live3 (re-xr-pipeline.md 162.12) */
                                               "MENU_PIN"};
static const char *const g_envs[SOMVR_SK_N] = {"SOMVR_TURN", "SOMVR_SNAP_DEG", "SOMVR_TURN_DPS", "SOMVR_VIGNETTE",
                                               "SOMVR_SNAP_BLINK_MS", "SOMVR_SEATED", "SOMVR_HEIGHT_CM", NULL, NULL, NULL,
                                               NULL, "SOMVR_HAPTICS", "SOMVR_STEP", "SOMVR_DASH", "SOMVR_PHASE",
                                               "SOMVR_SWORD_ANGLE", "SOMVR_MUSIC",
                                               "SOMVR_PLAYER_SIZE", "SOMVR_CROUCH",
                                               "SOMVR_CLIMB_FLING", "SOMVR_HANDS_SURFACE", "SOMVR_SWORD_WEIGHT",
                                               "SOMVR_SWORD_WEIGHT_HAND", "SOMVR_BOW_QUIVER",
                                               "SOMVR_COMBAT_BITE", "SOMVR_DODGE", "SOMVR_WRAITH_STEP",
                                               "SOMVR_STEALTH_KILL", "SOMVR_CROWD_PACE_MS", "SOMVR_COUNTER_ICON",
                                               "SOMVR_BOW_STRING", "SOMVR_MOVE", "SOMVR_XR_REFRESH",
                                               "SOMVR_HANDED", NULL, "SOMVR_BOW_TAKE",
                                               /* build 34: the menu's own words have no switch of their name */
                                               "SOMVR_XR_WATCH_LOOK", NULL, NULL, NULL, NULL,
                                               "SOMVR_SWORD_PHYS_GHOST", "SOMVR_KILLS", "SOMVR_KILLS_EXEC", NULL,
                                               "SOMVR_VERBS_GRAB_MAX_S", "SOMVR_SLOTS_DAGGER_HOLD", "SOMVR_HERBS",
                                               "SOMVR_CLIMB_FLING_DOWN", "SOMVR_CLIMB_SWORD", "SOMVR_MENU_PIN"};
/* SOMVR_DASH / SOMVR_PHASE's words (somvr_step.h: SOMVR_ST_DASH_* / _PHASE_*) */
static const char *const g_dash_w[] = {"off", "on", "blink"}, *const g_phase_w[] = {"off", "on", "reface", "blink"};
static const char *const g_crouch_w[] = {"button", "duck"}; /* build 13: SOMVR_CROUCH (somvr_crouch.h) */
/* build 16: SOMVR_SWORD_WEIGHT's words (somvr_smooth.h SWG_*; on = light) */
static const char *const g_weight_w[] = {"off", "light", "heavy", "on"};
/* build 17: SOMVR_SWORD_WEIGHT_HAND's words (somvr_smooth_game.c) */
static const char *const g_whand_w[] = {"fixed", "follow"};
/* build 19: SOMVR_COUNTER_ICON's words (0 hidden, 1 shown: the game's), SOMVR_BOW_STRING's (somvr.c g_bo_string_w) */
static const char *const g_cicon_w[] = {"hide", "game"}, *const g_string_w[] = {"grip", "trigger"};
static const char *const g_take_w[] = {"shoulder", "grip"}; /* build 33: SOMVR_BOW_TAKE */
/* build 27: SOMVR_MOVE's words (somvr.c move_w: the walk toward the head or the left grip) */
static const char *const g_move_w[] = {"head", "hand"};
/* build 34 (re-xr-pipeline.md 150): the v8 keys' words (SOMVR_XR_WATCH_LOOK's, SOMVR_KILLS_EXEC's; the menu's own) */
static const char *const g_look_w[] = {"any", "raise"}, *const g_wsize_w[] = {"small", "large"},
                         *const g_lowhp_w[] = {"off", "edge", "ring"}, *const g_force_w[] = {"low", "normal", "high"},
                         *const g_exec_w[] = {"trigger", "blow"}, *const g_shove_w[] = {"soft", "normal", "hard"};

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
    case SOMVR_SK_BOW_STRING:
        return (w = word_of(v, g_string_w, 2)) < 0 ? 0 : (s->bow_string = (uint8_t)((s->bow_string & 2u) | (unsigned)w), 1);
    case SOMVR_SK_BOW_TAKE: /* build 33: bit 1 */
        return (w = word_of(v, g_take_w, 2)) < 0 ? 0 : (s->bow_string = (uint8_t)((s->bow_string & 1u) | (unsigned)w << 1), 1);
    case SOMVR_SK_MOVE: return (w = word_of(v, g_move_w, 2)) < 0 ? 0 : (s->move = (uint8_t)w, 1); /* build 27 */
    case SOMVR_SK_HANDED: return (w = word_of(v, hand_w, 2)) < 0 ? 0 : (s->handed = (uint8_t)w, 1); /* build 30 */
    /* build 34 (re-xr-pipeline.md 150): the v8 rows (the switches' words; on/off also 1/0) */
    case SOMVR_SK_XR_WATCH_LOOK: return (w = word_of(v, g_look_w, 2)) < 0 ? 0 : (s->watch_raise = (uint8_t)w, 1);
    case SOMVR_SK_WATCH_SIZE: return (w = word_of(v, g_wsize_w, 2)) < 0 ? 0 : (s->watch_big = (uint8_t)w, 1);
    case SOMVR_SK_LOW_HEALTH: return (w = word_of(v, g_lowhp_w, 3)) < 0 ? 0 : (s->low_health = (uint8_t)w, 1);
    case SOMVR_SK_HEARTBEAT: return (w = word_of(v, onoff_w, 4)) < 0 ? 0 : (s->heartbeat = (uint8_t)(w & 1), 1);
    case SOMVR_SK_FORCE_NEEDED: return (w = word_of(v, g_force_w, 3)) < 0 ? 0 : (s->force = (uint8_t)w, 1);
    case SOMVR_SK_SWORD_PHYS_GHOST: return (w = word_of(v, onoff_w, 4)) < 0 ? 0 : (s->ghost = (uint8_t)(w & 1), 1);
    case SOMVR_SK_KILLS: return (w = word_of(v, onoff_w, 4)) < 0 ? 0 : (s->kills = (uint8_t)(w & 1), 1);
    case SOMVR_SK_KILLS_EXEC: return (w = word_of(v, g_exec_w, 2)) < 0 ? 0 : (s->exec_blow = (uint8_t)w, 1);
    case SOMVR_SK_SHOVE_PUSH: return (w = word_of(v, g_shove_w, 3)) < 0 ? 0 : (s->shove = (uint8_t)w, 1);
    case SOMVR_SK_SLOTS_DAGGER_HOLD: return (w = word_of(v, onoff_w, 4)) < 0 ? 0 : (s->knife_hold = (uint8_t)(w & 1), 1);
    case SOMVR_SK_HERBS: return (w = word_of(v, onoff_w, 4)) < 0 ? 0 : (s->herbs = (uint8_t)(w & 1), 1);
    case SOMVR_SK_CLIMB_FLING_DOWN: return (w = word_of(v, onoff_w, 4)) < 0 ? 0 : (s->fling_down = (uint8_t)(w & 1), 1);
    case SOMVR_SK_CLIMB_SWORD: return (w = word_of(v, onoff_w, 4)) < 0 ? 0 : (s->climb_sword = (uint8_t)(w & 1), 1);
    case SOMVR_SK_MENU_PIN: return (w = word_of(v, onoff_w, 4)) < 0 ? 0 : (s->menu_pin = (uint8_t)(w & 1), 1);
    case SOMVR_SK_TEST_PAGE: { /* build 31: P@KK */
        unsigned pg = 0, key = 0;
        char tail = 0;
        if (sscanf(v, "%u@%x%c", &pg, &key, &tail) != 2 || pg > 255 || key > 255) return 0;
        s->test_page = (uint8_t)pg, s->test_key = (uint8_t)key;
        return 1;
    }
    case SOMVR_SK_XR_REFRESH: /* build 27: off / 0 = nothing asked (the runtime's own rate), else Hz 60..144 */
        if (word_of(v, onoff_w + 2, 1) == 0) return s->refresh = 0, 1;
        break;
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
    case SOMVR_SK_XR_REFRESH: s->refresh = d < 0.5 ? 0 : (uint8_t)lrint(clampf(d, 60, 144)); return 1; /* build 27 */
    case SOMVR_SK_VERBS_GRAB_MAX_S: s->hold_s = (uint8_t)lrint(clampf(d, 0, 60)); return 1; /* build 34: as the switch */
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
    case SOMVR_SK_BOW_STRING: snprintf(buf, cap, "%s", g_string_w[s->bow_string & 1u]); break;
    case SOMVR_SK_BOW_TAKE: snprintf(buf, cap, "%s", g_take_w[(s->bow_string >> 1) & 1u]); break; /* build 33 */
    case SOMVR_SK_MOVE: snprintf(buf, cap, "%s", g_move_w[s->move != 0]); break; /* build 27 */
    case SOMVR_SK_HANDED: snprintf(buf, cap, "%s", s->handed ? "left" : "right"); break; /* build 30 */
    case SOMVR_SK_TEST_PAGE: snprintf(buf, cap, "%u@%02x", (unsigned)s->test_page, (unsigned)s->test_key); break;
    case SOMVR_SK_XR_REFRESH: snprintf(buf, cap, "%u", (unsigned)s->refresh); break; /* build 27: Hz, 0 = none asked */
    /* build 34 (re-xr-pipeline.md 150) */
    case SOMVR_SK_XR_WATCH_LOOK: snprintf(buf, cap, "%s", g_look_w[s->watch_raise != 0]); break;
    case SOMVR_SK_WATCH_SIZE: snprintf(buf, cap, "%s", g_wsize_w[s->watch_big != 0]); break;
    case SOMVR_SK_LOW_HEALTH: snprintf(buf, cap, "%s", g_lowhp_w[s->low_health < 3 ? s->low_health : 0]); break;
    case SOMVR_SK_HEARTBEAT: snprintf(buf, cap, "%s", s->heartbeat ? "on" : "off"); break;
    case SOMVR_SK_FORCE_NEEDED: snprintf(buf, cap, "%s", g_force_w[s->force < 3 ? s->force : 1]); break;
    case SOMVR_SK_SWORD_PHYS_GHOST: snprintf(buf, cap, "%s", s->ghost ? "on" : "off"); break;
    case SOMVR_SK_KILLS: snprintf(buf, cap, "%s", s->kills ? "on" : "off"); break;
    case SOMVR_SK_KILLS_EXEC: snprintf(buf, cap, "%s", g_exec_w[s->exec_blow != 0]); break;
    case SOMVR_SK_SHOVE_PUSH: snprintf(buf, cap, "%s", g_shove_w[s->shove < 3 ? s->shove : 1]); break;
    case SOMVR_SK_VERBS_GRAB_MAX_S: snprintf(buf, cap, "%u", (unsigned)s->hold_s); break;
    case SOMVR_SK_SLOTS_DAGGER_HOLD: snprintf(buf, cap, "%s", s->knife_hold ? "on" : "off"); break;
    case SOMVR_SK_HERBS: snprintf(buf, cap, "%s", s->herbs ? "on" : "off"); break;
    case SOMVR_SK_CLIMB_FLING_DOWN: snprintf(buf, cap, "%s", s->fling_down ? "on" : "off"); break;
    case SOMVR_SK_CLIMB_SWORD: snprintf(buf, cap, "%s", s->climb_sword ? "on" : "off"); break;
    case SOMVR_SK_MENU_PIN: snprintf(buf, cap, "%s", s->menu_pin ? "on" : "off"); break; /* b37 live3 */
    default: break;
    }
}

uint64_t somvr_set_parse(somvr_settings *s, const char *text, int *bad, char *note, size_t cap) {
    uint64_t mask = 0; /* build 27: 64 bits (32 keys) */
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
            mask |= 1ull << k;
        }
    }
    if (bad) *bad = nbad;
    return mask;
}

size_t somvr_set_format(const somvr_settings *s, uint64_t mask, char *buf, size_t cap) {
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

uint64_t somvr_set_diff(const somvr_settings *a, const somvr_settings *b) {
    uint64_t d = 0;
    for (int k = 0; k < SOMVR_SK_N; k++) {
        char x[32], y[32];
        somvr_set_get(a, k, x, sizeof x), somvr_set_get(b, k, y, sizeof y);
        if (strcmp(x, y)) d |= 1ull << k;
    }
    return d;
}

/* ==== 3. the menu: items and values ============================================================================== */
static const char *const g_labels[SOMVR_VMI_ALL] = {"Turn",     "Snap angle", "Turn speed",   "Vignette",
                                                    "Snap blink", "Seated",   "Seat height",  "HUD",
                                                    "HUD distance", "Pointer hand", "View",   "Haptics",
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
                                                    /* build 27: the walk's direction (Settings page, by the turn) */
                                                    "Walk toward",
                                                    /* build 27 (re-xr-pipeline.md 119): the Settings page's last row */
                                                    "Refresh rate",
                                                    /* build 30 (re-xr-pipeline.md 132): above Pointer hand */
                                                    "Handedness",
                                                    /* build 33 (re-xr-pipeline.md 142): under Bow string */
                                                    "Bow from",
                                                    /* build 34 (re-xr-pipeline.md 150): the v8 rows */
                                                    "Watch", "Watch size", "Low health", "Heartbeat",
                                                    "Force needed", "Ghost hand", "Kill moves", "Execute",
                                                    "Shove push", "Grab time", "Knife grip", "Herbs",
                                                    "Fling down", "Climb sword",
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
                                                    "Test", "Prev", "Next",
                                                    /* build 22: beside Remove the orc */
                                                    "Captain",
                                                    /* build 27: the Controls page's Prev / Next */
                                                    "Prev", "Next",
                                                    /* build 31: beside Refill health */
                                                    "Place orc",
                                                    /* build 34: the Settings tab's groups */
                                                    "Comfort", "Display", "Climbing", "Sword", "Fight", "Bow",
                                                    /* build 36: the practice page's last button */
                                                    "Charge execution",
                                                    /* build 37: beside it */
                                                    "New herb",
                                                    /* build 35: the Test page's patch box (SOMVR_LIVE) */
                                                    "Apply patch", "Undo patch",
                                                    /* b37 live3 */ "Pin in front", "Close"};
const char *somvr_vm_label(int item) { return item >= 0 && item < SOMVR_VMI_ALL ? g_labels[item] : ""; }
static const char *const g_orc_labels[6] = {"Infantry", "Shield", "Berserker", /* = somvr_pr_orc_label */
                                            "Caragor", "Ghul", "Graug"}; /* build 22: the beasts */

/* build 35 (re-xr-pipeline.md 162): the Test page's patch box, the layer's (off: the page as before) */
static somvr_vm_live g_vml;
void somvr_vm_live_set(const somvr_vm_live *l) {
    if (l) g_vml = *l;
    else memset(&g_vml, 0, sizeof g_vml);
}
const somvr_vm_live *somvr_vm_live_get(void) { return &g_vml; }

int somvr_vm_enabled(const somvr_settings *s, uint64_t avail, int item) {
    switch (item) {
    case SOMVR_VMI_PIN:
    case SOMVR_VMI_PINX: return 1; /* b37 live3: the bottom bar, always */
    case SOMVR_VMI_LIVE_APPLY: return g_vml.on && g_vml.can_apply;
    case SOMVR_VMI_LIVE_UNDO: return g_vml.on && g_vml.can_undo;
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
    case SOMVR_VMI_BOWTAKE: return (avail & SOMVR_VMA_STRING) != 0; /* build 33: with the bow (Bow string's bit) */
    case SOMVR_VMI_DODGE: return (avail & SOMVR_VMA_DODGE) != 0;
    case SOMVR_VMI_WSTEP: return (avail & SOMVR_VMA_WSTEP) != 0;
    case SOMVR_VMI_STEALTH: return (avail & SOMVR_VMA_STEALTH) != 0;
    case SOMVR_VMI_CROWD: return (avail & SOMVR_VMA_CROWD) != 0;
    case SOMVR_VMI_CICON: return (avail & SOMVR_VMA_CICON) != 0;
    case SOMVR_VMI_MOVE: return (avail & SOMVR_VMA_WALKDIR) != 0; /* build 27 */
    case SOMVR_VMI_REFRESH: return (avail & SOMVR_VMA_REFRESH) != 0; /* build 27: the layer's own */
    /* build 34 (re-xr-pipeline.md 150): the v8 rows by s->av8 (the preload's at its start); a few only matter with
     * another row's value (dimmed then, as the snap angle with smooth turns) */
    case SOMVR_VMI_WATCH:
    case SOMVR_VMI_WATCH_SIZE: return (s->av8 & SOMVR_VMA8_WATCH) != 0;
    case SOMVR_VMI_LOWHP: return (s->av8 & SOMVR_VMA8_HURT) != 0;
    case SOMVR_VMI_BEAT: return (s->av8 & SOMVR_VMA8_HURT) && s->low_health == 2; /* the ring's heartbeat */
    case SOMVR_VMI_FORCE: return (s->av8 & SOMVR_VMA8_FORCE) != 0;
    case SOMVR_VMI_GHOST: return (s->av8 & SOMVR_VMA8_GHOST) != 0;
    case SOMVR_VMI_KILLS: return (s->av8 & SOMVR_VMA8_KILLS) != 0;
    case SOMVR_VMI_EXEC: return (s->av8 & SOMVR_VMA8_KILLS) && s->kills; /* the blow's execution is the kills' */
    case SOMVR_VMI_SHOVE: return (s->av8 & SOMVR_VMA8_SHOVE) != 0;
    case SOMVR_VMI_HOLD: return (s->av8 & SOMVR_VMA8_HOLD) != 0;
    case SOMVR_VMI_KNIFE: return (s->av8 & SOMVR_VMA8_KNIFE) != 0;
    case SOMVR_VMI_HERBS: return (s->av8 & SOMVR_VMA8_HERBS) != 0;
    case SOMVR_VMI_FLING_DOWN: return (s->av8 & SOMVR_VMA8_FLINGDN) && s->fling; /* a fling's own way down */
    case SOMVR_VMI_CLIMB_SWORD: return (s->av8 & SOMVR_VMA8_CLIMB) != 0;
    case SOMVR_VMI_SUB_COMFORT: /* build 34: the Settings tab's groups */
    case SOMVR_VMI_SUB_DISPLAY:
    case SOMVR_VMI_SUB_CLIMB:
    case SOMVR_VMI_SUB_SWORD:
    case SOMVR_VMI_SUB_FIGHT:
    case SOMVR_VMI_SUB_BOW: return 1;
    case SOMVR_VMI_CTL_PREV: /* build 27: the Controls page's pages */
    case SOMVR_VMI_CTL_NEXT:
    case SOMVR_VMI_PG_FIGHT: /* build 19: the page buttons (the Fight page always has the Sword angle row) */
    case SOMVR_VMI_PG_BACK:
    case SOMVR_VMI_TURN:
    case SOMVR_VMI_SEATED:
    case SOMVR_VMI_HANDED: /* build 30: always (the controllers' roles: every build with input) */
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
    case SOMVR_VMI_PR_CAPTAIN: /* build 22: the captain, as the spawn */
    case SOMVR_VMI_PR_PLACE: /* build 31: the aimed orc, as the spawn */
    case SOMVR_VMI_PR_CHARGE: /* build 36: the execution's charge, as the refills */
    case SOMVR_VMI_PR_HERB:   /* build 37: New herb, as the refills */
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
           /* build 34: the Settings tab's groups */
           : item == SOMVR_VMI_SUB_COMFORT ? SOMVR_VMPG_SETTINGS
           : item == SOMVR_VMI_SUB_DISPLAY ? SOMVR_VMPG_DISPLAY
           : item == SOMVR_VMI_SUB_CLIMB   ? SOMVR_VMPG_CLIMB
           : item == SOMVR_VMI_SUB_SWORD   ? SOMVR_VMPG_SWORD
           : item == SOMVR_VMI_SUB_FIGHT   ? SOMVR_VMPG_FIGHT
           : item == SOMVR_VMI_SUB_BOW     ? SOMVR_VMPG_BOW
                                           : -1;
}
int somvr_vm_settings_page(int page) {
    return page == SOMVR_VMPG_SETTINGS || page == SOMVR_VMPG_FIGHT ||
           (page >= SOMVR_VMPG_DISPLAY && page <= SOMVR_VMPG_CLIMB);
}
const char *somvr_vm_page_name(int page) {
    switch (page) {
    case SOMVR_VMPG_PRACTICE: return "the practice page";
    case SOMVR_VMPG_CONTROLS: return "the controls page";
    case SOMVR_VMPG_FIGHT: return "the fight page";
    case SOMVR_VMPG_TEST: return "the test page";
    case SOMVR_VMPG_DISPLAY: return "the display page";
    case SOMVR_VMPG_SWORD: return "the sword page";
    case SOMVR_VMPG_BOW: return "the bow page";
    case SOMVR_VMPG_CLIMB: return "the climbing page";
    default: return "the settings page"; /* the Comfort group (build 19's words) */
    }
}
/* build 19: the Fight page's rows say what they do (two dim lines under the page's rows while one is hovered: HL:A's
 * options explain the one pointed at); 29 cells of 16 px from x 24. Build 34: any group's row (the room permitting) */
static const struct { int item; const char *l1, *l2; } g_fight_help[] = {
    {SOMVR_VMI_SWORD, "The blade's angle in the fist", "(+ tips it up, - down)"},
    {SOMVR_VMI_WEIGHT, "How far the blade lags your", "hand in a fast swing"},
    {SOMVR_VMI_SWHAND, "The drawn hands turn with the", "weighted blade, or stay put"},
    {SOMVR_VMI_BITE, "A cut slows in the body; a", "stab stays in until pulled"}, /* build 34: build 31's flesh */
    {SOMVR_VMI_ARROWS, "Quiver: reach behind your", "shoulder. Auto: on the string"},
    {SOMVR_VMI_STRING, "The button that holds the", "bow's string"},
    {SOMVR_VMI_BOWTAKE, "Grip: the left grip anywhere", "takes the bow out"}, /* build 33 */
    {SOMVR_VMI_DODGE, "A in a fight: a quick 1.5 m", "dash out of the blow"},
    {SOMVR_VMI_WSTEP, "Right stick forward aims a", "short blink; let go to go"},
    {SOMVR_VMI_STEALTH, "Behind an orc that has not", "seen you, the blade kills it"},
    {SOMVR_VMI_CROWD, "The least time between two", "orcs' blows at you"},
    {SOMVR_VMI_CICON, "The game's flashing icon", "over an orc about to strike"},
    /* build 34 (re-xr-pipeline.md 150): the v8 rows, the Climbing group's two older ones */
    {SOMVR_VMI_WATCH, "Raised: only while you turn", "your wrist to read it"},
    {SOMVR_VMI_WATCH_SIZE, "The watch's face on your", "wrist: 6 cm or 10 cm wide"},
    {SOMVR_VMI_LOWHP, "Under 30 % health: a red", "ring, or a faint red edge"},
    {SOMVR_VMI_BEAT, "Under 20 % health your", "hands feel a heartbeat"},
    {SOMVR_VMI_FORCE, "How hard a swing must be to", "hit. Low suits seated play"},
    {SOMVR_VMI_GHOST, "A faint hand at your real", "hand while the blade lags"},
    {SOMVR_VMI_KILLS, "Orcs die in the game's own", "kill moves, not a ragdoll"},
    {SOMVR_VMI_EXEC, "A glowing orc: execute it", "by a heavy swing or trigger"},
    {SOMVR_VMI_SHOVE, "How fast a push of the free", "hand must be to shove"},
    {SOMVR_VMI_HOLD, "How long a grabbed orc stays", "held while you grip"},
    {SOMVR_VMI_KNIFE, "Hold: let go, it goes back", "on the belt. Toggle: it stays"},
    {SOMVR_VMI_HERBS, "Hand: grip a glowing herb,", "bring it to your mouth"},
    {SOMVR_VMI_FLING, "Let go of the wall fast and", "fly on the way you threw"},
    {SOMVR_VMI_FLING_DOWN, "A fast let-go with the hand", "rising flings you down"},
    {SOMVR_VMI_CLIMB_SWORD, "The free hand may draw the", "sword while you climb"},
    {SOMVR_VMI_SURFACE, "Your hands stop at walls", "instead of going through"}};
int somvr_vm_help(int item, const char **l1, const char **l2) {
    for (size_t i = 0; i < sizeof g_fight_help / sizeof g_fight_help[0]; i++)
        if (g_fight_help[i].item == item) return *l1 = g_fight_help[i].l1, *l2 = g_fight_help[i].l2, 1;
    *l1 = *l2 = NULL;
    return 0;
}
/* build 34 (re-xr-pipeline.md 150): the Settings tab's groups' rows, top to bottom (-1 ends a group) */
static const int8_t g_group_rows[SOMVR_VM_SUBS][18] = {
    /* Comfort (SOMVR_VMPG_SETTINGS): Pointer hand stays hidden (Handedness sets it; somvr_vm_shown) */
    {SOMVR_VMI_TURN, SOMVR_VMI_SNAP, SOMVR_VMI_SPEED, SOMVR_VMI_VIGNETTE, SOMVR_VMI_BLINK, SOMVR_VMI_MOVE,
     SOMVR_VMI_SEATED, SOMVR_VMI_HEIGHT, SOMVR_VMI_CROUCH, SOMVR_VMI_SIZE, SOMVR_VMI_HANDED, SOMVR_VMI_HAND,
     SOMVR_VMI_HAPTICS, SOMVR_VMI_STEP, SOMVR_VMI_DASH, SOMVR_VMI_PHASE, -1},
    /* Display */
    {SOMVR_VMI_VIEW, SOMVR_VMI_HUD, SOMVR_VMI_HUD_DIST, SOMVR_VMI_WATCH, SOMVR_VMI_WATCH_SIZE, SOMVR_VMI_LOWHP,
     SOMVR_VMI_BEAT, SOMVR_VMI_MUSIC, SOMVR_VMI_REFRESH, -1},
    /* Climbing */
    {SOMVR_VMI_FLING, SOMVR_VMI_FLING_DOWN, SOMVR_VMI_CLIMB_SWORD, SOMVR_VMI_SURFACE, -1},
    /* Sword */
    {SOMVR_VMI_SWORD, SOMVR_VMI_WEIGHT, SOMVR_VMI_SWHAND, SOMVR_VMI_FORCE, SOMVR_VMI_BITE, SOMVR_VMI_GHOST,
     SOMVR_VMI_KILLS, SOMVR_VMI_EXEC, -1},
    /* Fight (SOMVR_VMPG_FIGHT: the orcs) */
    {SOMVR_VMI_DODGE, SOMVR_VMI_WSTEP, SOMVR_VMI_STEALTH, SOMVR_VMI_CROWD, SOMVR_VMI_CICON, SOMVR_VMI_SHOVE,
     SOMVR_VMI_HOLD, SOMVR_VMI_KNIFE, SOMVR_VMI_HERBS, -1},
    /* Bow (build 33's Bow from under Bow string) */
    {SOMVR_VMI_ARROWS, SOMVR_VMI_STRING, SOMVR_VMI_BOWTAKE, -1}};
static const int g_group_page[SOMVR_VM_SUBS] = {SOMVR_VMPG_SETTINGS, SOMVR_VMPG_DISPLAY, SOMVR_VMPG_CLIMB,
                                                SOMVR_VMPG_SWORD, SOMVR_VMPG_FIGHT, SOMVR_VMPG_BOW};
static int group_of_page(int page) {
    for (int g = 0; g < SOMVR_VM_SUBS; g++)
        if (g_group_page[g] == page) return g;
    return -1;
}
int somvr_vm_row_page(int item) {
    if (item < 0 || item >= SOMVR_VMI_ROWS) return -1;
    for (int g = 0; g < SOMVR_VM_SUBS; g++)
        for (int j = 0; g_group_rows[g][j] >= 0; j++)
            if (g_group_rows[g][j] == item) return g_group_page[g];
    return -1; /* (every value row is in a group: test_vrmenu) */
}

/* the Controls page. Build 6: one page of key / what lines (SOMVR_GAMEMOVES=off: A, the bow, the dash, the wraith phase
 * and the finishers did nothing on foot); builds 10-26 grew it to 21 lines with optional ones dropped for room (Quit,
 * Sheathe, Sprint). Build 27 (re-xr-pipeline.md 112, som-b27-hla): every VR control and gesture the build has, in
 * groups a player can find (Moving, Climbing, The sword, Orcs, Bow and wraith, Beasts, Menus and help), paged with
 * Prev / Next; each line only where its feature runs in this session (need: all of these avail bits; unless: none of
 * these) - no line for a thing that does not work, no working thing without a line (tests/test_vrmenu.c holds the
 * build 26-27 headset set to that). Keys up to SOMVR_VM_CTL_KEY_W characters, the rest up to SOMVR_VM_CTL_WHAT_W (16
 * pixels each on the 512-pixel panel). card = one of the arrival card's first steps (the card: those, then where the
 * rest is). Quit = the Settings page's bottom row, pressed twice; the bug-mark chord is SOMVR_VM_CTL_REPORT. */
#ifndef SOMVR_VM_CTL_REPORT /* the bug-mark chord (somvr_session.h: B + Y held half a second and let go); "" = no line */
#define SOMVR_VM_CTL_REPORT "hold B + Y (0.5 s)"
#endif
enum { CG_MOVE, CG_CLIMB, CG_SWORD, CG_ORCS, CG_BOW, CG_BEAST, CG_MENU, CG_N };
static const char *const g_ctl_group[CG_N] = {"Moving",         "Climbing", "The sword",     "Orcs",
                                              "Bow and wraith", "Beasts",   "Menus and help"};
typedef struct {
    int group;
    const char *key, *what;
    uint64_t need, unless; /* build 18: 64 bits, as avail */
    int card;              /* build 27: on the arrival card */
} vm_ctl_line;
static const vm_ctl_line g_ctl[] = {
    /* ---- Moving: the stick's walk toward the head or the left hand (the Settings page's Walk toward row) */
    {CG_MOVE, "Walk", "left stick", 0, 0, 1},
    {CG_MOVE, "Sprint", "click left stick", 0, 0, 0},
    {CG_MOVE, "Turn", "R stick, down=crouch", 0, SOMVR_VMA_CROUCH, 1},
    {CG_MOVE, "Turn", "right stick", SOMVR_VMA_CROUCH, 0, 1},
    {CG_MOVE, "Crouch", "duck or R stick down", SOMVR_VMA_CROUCH, 0, 1}, /* build 13: SOMVR_CROUCH (the stick toggles it) */
    /* build 22: SOMVR_JUMP (moving: at once; standing: where no edge claims A) */
    {CG_MOVE, "Jump", "A on foot", SOMVR_VMA_JUMP, 0, 0},
    {CG_MOVE, "Vault", "A at the gold ring", SOMVR_VMA_VAULT, 0, 0}, /* build 24: SOMVR_VAULT (a 45-120 cm obstacle) */
    /* build 18: SOMVR_WRAITH_STEP (the Fight page's row On): the stick forward aims the ring, the let-go goes, the walk
     * cancels */
    {CG_MOVE, "Blink", "R stick fwd, let go", SOMVR_VMA_WSTEP, 0, 0},
    {CG_MOVE, "Map", "click right stick", 0, SOMVR_VMA_ARMY, 0},
    {CG_MOVE, "Map", "R click, hold=Army", SOMVR_VMA_ARMY, 0, 0}, /* build 27: SOMVR_ARMY (the game's "Hold [>]") */
    {CG_MOVE, "Minimap", "back of left wrist", 0, 0, 0},
    {CG_MOVE, "Use", "B (interact)", 0, SOMVR_VMA_USE, 1},
    {CG_MOVE, "Use", "B (pick, missions)", SOMVR_VMA_USE, 0, 1}, /* build 14: SOMVR_USE's picks and "Start Mission" */
    {CG_MOVE, "Herb", "grip, fist to mouth", SOMVR_VMA_CTL_HERBS, 0, 0}, /* build 33-34: SOMVR_HERBS (ctl_feat) */
    /* ---- Climbing */
    {CG_CLIMB, "Climb", "grip a wall, pull", 0, SOMVR_VMA_FLING, 1},
    {CG_CLIMB, "Climb", "grip; fling up/down", SOMVR_VMA_FLING, 0, 1}, /* build 13: let go mid-pull = the fling (build 31: SOMVR_CLIMB_FLING_DOWN, a hard pull up then let go) */
    {CG_CLIMB, "Ledge", "pull at the ring", SOMVR_VMA_PULLOVER, 0, 0},  /* build 16's pullover: a ring on the top */
    /* build 20: SOMVR_CLIMB_MANTLE (holding a top with the eyes over it: the gold ring where he will stand, A = onto it) */
    {CG_CLIMB, "Onto top", "A at the gold ring", SOMVR_VMA_MANTLE, 0, 0},
    /* build 22: SOMVR_CLIMB_MANTLE_OVERHANG (a lip held from below: the ring on its top, A = the glide up onto it) */
    {CG_CLIMB, "Overhang", "A at its gold ring", SOMVR_VMA_OVERHANG, 0, 0},
    /* build 21-22: SOMVR_CLIMB_DOWN_OVER (on a wall top a hand reached down over the edge and closed: the glide over) */
    {CG_CLIMB, "Down", "grip over the edge", SOMVR_VMA_DOWN, 0, 0},
    {CG_CLIMB, "Sword", "free hand: shoulder", 0, 0, 0}, /* build 31: SOMVR_CLIMB_SWORD (the hand off the wall draws) */
    {CG_CLIMB, "Drop", "A (on a wall)", 0, SOMVR_VMA_DESCENT, 0},
    {CG_CLIMB, "Drop", "A (wall, edge)", SOMVR_VMA_DESCENT, SOMVR_VMA_DROPMARK, 0}, /* build 13: A at an edge, 4 m */
    {CG_CLIMB, "Drop", "hold A, let go", SOMVR_VMA_DESCENT | SOMVR_VMA_DROPMARK, 0, 0}, /* build 17: the landing shown */
    /* ---- The sword */
    {CG_SWORD, "Sword", "grip at R shoulder", 0, 0, 1},
    {CG_SWORD, "Sheathe", "grip there again", 0, 0, 0},
    {CG_SWORD, "2 hands", "L grip/LT on hilt", 0, 0, 0}, /* build 17: SOMVR_TWOHAND (the left trigger too) */
    {CG_SWORD, "Block", "sword across a blow", 0, 0, 1}, /* build 17: the block in plain words */
    {CG_SWORD, "Dodge", "A in a fight", SOMVR_VMA_DODGE, 0, 0}, /* build 17-19: SOMVR_DODGE (the row On): 1.5 m out */
    /* build 17: SOMVR_SLOTS' Acharn; build 33: held while gripped (SOMVR_SLOTS_DAGGER_HOLD); build 36: either hip, the
     * hand on that side (SOMVR_SLOTS_KNIFE_HIPS=both: the sword's hand when it holds nothing) */
    {CG_SWORD, "Knife", "hip, hold grip", 0, 0, 0},
    /* build 19: SOMVR_SHOVE - a quick push of the free left hand into a raised shield opens it for a second */
    {CG_SWORD, "Shove", "push hard at an orc", SOMVR_VMA_SHOVE, 0, 0}, /* build 33: 2.5 m/s (3.5 at a grab-cued orc) */ /* build 31: SOMVR_SHOVE_ANY, 2-3 m (SHOVE_FAR) */
    /* build 18-21: SOMVR_STEALTH_KILL (the row On): behind an unaware orc, a hard thrust point first */
    {CG_SWORD, "Stealth", "stab his back hard", SOMVR_VMA_STEALTH, 0, 0},
    /* build 25: SOMVR_ABOVE: crouched on a ledge over an orc (a faint blue glow), a fast plunge down at him */
    {CG_SWORD, "Above", "stab down from ledge", SOMVR_VMA_ABOVE, 0, 0},
    /* build 27 (the combat helper, re-xr-pipeline.md 108): the wraith step aimed at an orc in reach = over it */
    {CG_SWORD, "Over orc", "blink aimed at him", SOMVR_VMA_WSTEP | SOMVR_VMA_OVERORC, 0, 0},
    /* build 27 (108.6, SOMVR_STRUGGLE): Talion's last chance - a hand moves the cursor into the ring, a hard stab */
    {CG_SWORD, "Struggle", "hand to ring, stab", SOMVR_VMA_STRUGGLE, 0, 0},
    /* build 27 (114, SOMVR_ABIL): a downed orc the game lets Talion finish (its blue glow) - the stab down into him */
    {CG_SWORD, "Downed", "stab down into him", SOMVR_VMA_GROUNDK, 0, 0},
    /* build 27d (121, SOMVR_ABIL_BRUTAL): the stealth stab with the left trigger held = the game's brutalize */
    {CG_SWORD, "Brutal", "stealth stab + LT", SOMVR_VMA_STEALTH | SOMVR_VMA_ABIL2, 0, 0},
    /* ---- Orcs (SOMVR_VERBS: the left hand; the trigger while holding: a tap stuns, held drains) */
    {CG_ORCS, "Grab", "L grip at blue glow", SOMVR_VMA_VERBS, 0, 0}, /* build 31: SOMVR_VERBS_CUE_NEAR (the faint glow first) */
    {CG_ORCS, "Stun", "held: tap L trigger", SOMVR_VMA_VERBS, 0, 0},
    {CG_ORCS, "Drain", "held: LT again", SOMVR_VMA_VERBS, 0, 0}, /* build 31: SOMVR_VERBS_FIST_S (a trigger with the grab is the fist) */
    {CG_ORCS, "Question", "held: pull to face", SOMVR_VMA_VERBS, 0, 0}, /* the interrogation: the orc to the eyes */
    {CG_ORCS, "Kill", "held: any sword hit", SOMVR_VMA_VERBS, 0, 0}, /* build 31: SOMVR_HOLD31 */
    {CG_ORCS, "Let go", "open hand or push", SOMVR_VMA_VERBS, 0, 0}, /* build 31: SOMVR_HOLD31 (the push = a shove) */
    {CG_ORCS, "Execute", "glowing: hard hit", SOMVR_VMA_VERBS, 0, 0}, /* build 12: with a full hit streak; build 31: SOMVR_KILLS_EXEC=blow (no trigger) */
    {CG_ORCS, "Brand", "L palm on head + LT", SOMVR_VMA_BRAND, 0, 0}, /* build 23-24: SOMVR_VERBS_BRAND (where offered) */
    {CG_ORCS, "Rally", "L hand high + LT", SOMVR_VMA_RALLY, 0, 0}, /* build 25: SOMVR_VERBS_RALLY: the branded attack */
    {CG_ORCS, "Whistle", "duck, hand at mouth", SOMVR_VMA_ABIL2, 0, 0}, /* build 27c (118, SOMVR_ABIL_WHISTLE): crouched */
    /* build 26: SOMVR_VERBS_DISMISS / _COMMAND: a Domination's (B) Dismiss and (Y) Command by the Touch's B and Y */
    {CG_ORCS, "Dominate", "B dismiss, Y command", SOMVR_VMA_DISMISS | SOMVR_VMA_COMMAND, 0, 0},
    {CG_ORCS, "Dominate", "B dismiss", SOMVR_VMA_DISMISS, SOMVR_VMA_COMMAND, 0},
    {CG_ORCS, "Dominate", "Y command", SOMVR_VMA_COMMAND, SOMVR_VMA_DISMISS, 0},
    /* ---- Bow and wraith */
    {CG_BOW, "Bow", "hold left trigger", 0, SOMVR_VMA_BOWSLOT, 0}, /* build 11 (SOMVR_BOW) */
    {CG_BOW, "Bow", "L grip at L shoulder", SOMVR_VMA_BOWSLOT, 0, 0}, /* build 15b: SOMVR_BOW_DRAW=slot */
    /* build 36 (re-xr-pipeline.md 167.1; the 11-03 Windows session: the trigger and A pressed together to draw, a jump):
     * the button that draws on a line of its own, then the shot (A is no jump while drawing: SOMVR_BOW_A_JUMP) */
    {CG_BOW, "Draw", "R grip at string", 0, SOMVR_VMA_BOWRT | SOMVR_VMA_QVGRIP | SOMVR_VMA_BOWTRIG, 0},
    {CG_BOW, "Draw", "RT at the string", SOMVR_VMA_BOWTRIG, SOMVR_VMA_BOWRT, 0}, /* build 17: the trigger, Arrows: Auto */
    {CG_BOW, "Shoot", "pull back, let go", 0, SOMVR_VMA_BOWRT | SOMVR_VMA_QVGRIP, 0},
    {CG_BOW, "Arrow", "RT at R shoulder", SOMVR_VMA_BOWRT, 0, 0}, /* build 16: the quiver, the string on the trigger */
    {CG_BOW, "Arrow", "R grip at R shoulder", SOMVR_VMA_QVGRIP, 0, 0}, /* build 17: the quiver, the string on the grip */
    {CG_BOW, "Shoot", "nock, pull, let go", SOMVR_VMA_BOWRT, 0, 0},
    {CG_BOW, "Shoot", "nock, pull, let go", SOMVR_VMA_QVGRIP, 0, 0},
    {CG_BOW, "Wraith", "hold Y (vision)", 0, SOMVR_VMA_STRIKE, 0},
    {CG_BOW, "Wraith", "hold Y; A = Strike", SOMVR_VMA_STRIKE, 0, 0}, /* build 17: SOMVR_STRIKE_GAZE (the marked orc) */
    /* build 18: SOMVR_GLYPH=head - near an Ithildin glyph, the vision held and the glow looked at = collected */
    {CG_BOW, "Glyph", "hold Y, look at it", SOMVR_VMA_GLYPH, 0, 0},
    /* build 27 (114, SOMVR_ABIL): the wraith flash where the game offers it (the skill, the streak; the left palm's glow)
     * - the right trigger held, the open left hand thrust out at an orc */
    {CG_BOW, "Flash", "RT + L palm push out", SOMVR_VMA_FLASH, 0, 0},
    /* build 27c (118, SOMVR_ABIL_IGNITE): the ignite shot where the game offers it - B while an arrow is drawn (its tip
     * glows), the release shoots it */
    {CG_BOW, "Ignite", "B while drawn", SOMVR_VMA_ABIL2, 0, 0},
    {CG_BOW, "Distract", "X while drawn", SOMVR_VMA_ABIL2, 0, 0}, /* build 27d (121, SOMVR_ABIL_DISTRACT) */
    /* ---- Beasts (build 26-27, SOMVR_RIDE): a caragor's leap blocked = stunned; the left hand reached out at it and
     * closed (or B) at the game's offer mounts it; "Hold tight!" = that hand kept closed (or B held); riding: the left
     * stick steers, A leaps, the right trigger bites, B gets off */
    {CG_BEAST, "Caragor", "sword across a leap", SOMVR_VMA_RIDE | SOMVR_VMA_COUNTER, 0, 0},
    {CG_BEAST, "Mount", "L grip stunned beast", SOMVR_VMA_RIDE, 0, 0},
    {CG_BEAST, "Hold on", "keep the grip closed", SOMVR_VMA_RIDE | SOMVR_VMA_DOMHOLD, 0, 0},
    {CG_BEAST, "Steer", "left stick", SOMVR_VMA_RIDE, 0, 0},
    {CG_BEAST, "Riding", "A leap RT bite B off", SOMVR_VMA_RIDE, 0, 0},
    /* build 27c (som-b27-ride3, re-xr-pipeline.md 122): the beast's sprint - A held (the stock pad's A: the game's own),
     * or the left stick's click (latched while the stick stays pushed, as on foot) */
    {CG_BEAST, "Sprint", "hold A / click stick", SOMVR_VMA_RIDE, 0, 0},
    /* ---- Menus and help */
    /* build 13: the wrist panel by the palm gesture (the left palm turned to the eyes and looked at) or the left menu
     * button held; build 27: the gesture in a first-time player's words, the button's tap and hold on one line */
    {CG_MENU, "Menu", "look at left palm", 0, 0, 1},
    {CG_MENU, "Pause", "tap menu, hold=Menu", 0, 0, 0},
    {CG_MENU, "Screens", "point + trigger/tap", 0, 0, 0}, /* the game's menus: the laser's press or a fingertip */
    {CG_MENU, "Failed", "A retry, B abandon", SOMVR_VMA_FAILED, 0, 0}, /* build 25: SOMVR_USE_DEATH */
    {CG_MENU, "Nemesis", "B closes the pages", SOMVR_VMA_NEMESIS, 0, 0}, /* build 25: SOMVR_USE_SKIP */
    {CG_MENU, "Recentre", "Settings > Recentre", 0, 0, 0},
    {CG_MENU, "Report", SOMVR_VM_CTL_REPORT, 0, 0, 0},
    {CG_MENU, "Quit", "Settings > Quit x2", SOMVR_VMA_QUIT, 0, 0}, /* build 34: only where quitting works (SOMVR_QUIT) */
};
#define VM_CTL_N (sizeof g_ctl / sizeof g_ctl[0])
_Static_assert(sizeof g_ctl / sizeof g_ctl[0] <= 128, "the Controls page's lines");
static int ctl_on(uint64_t avail, size_t k) {
    const vm_ctl_line *l = &g_ctl[k];
    return l->what[0] && (l->need & avail) == l->need && !(l->unless & avail); /* "" = no line */
}
/* build 30 (re-xr-pipeline.md 132): the left-handed mirror's words, both ways, whole words only */
static const char *const g_mirror_w[][2] = {{"left", "right"}, {"Left", "Right"}, {"L", "R"}, {"LT", "RT"},
                                            {"LB", "RB"}, {"A", "X"}, {"B", "Y"}};
static int is_word_c(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }
void somvr_vm_mirror_text(const char *in, char *out, size_t cap) {
    if (!cap) return;
    size_t n = 0;
    for (const char *p = in ? in : ""; *p;) {
        if (!is_word_c(*p)) {
            if (n + 1 < cap) out[n++] = *p;
            p++;
            continue;
        }
        const char *q = p, *w = NULL;
        while (is_word_c(*q)) q++;
        size_t len = (size_t)(q - p);
        for (size_t k = 0; !w && k < sizeof g_mirror_w / sizeof g_mirror_w[0]; k++)
            for (int e = 0; e < 2 && !w; e++)
                if (strlen(g_mirror_w[k][e]) == len && !strncmp(p, g_mirror_w[k][e], len)) w = g_mirror_w[k][!e];
        size_t wl = w ? strlen(w) : len;
        for (size_t i = 0; i < wl && n + 1 < cap; i++) out[n++] = w ? w[i] : p[i];
        p = q;
    }
    out[n] = 0;
}
int somvr_vm_ctl_mirrored(const somvr_settings *s, const char *key) {
    if (key && (!strcmp(key, "Menu") || !strcmp(key, "Minimap"))) /* the panel's wrist: the Pointer hand's other one */
        return s->hand != 0;
    return s->handed_on != 0;
}

uint64_t somvr_vm_ctl_avail_f(const somvr_settings *s, uint64_t avail, uint32_t feat) {
    uint64_t a = somvr_vm_ctl_avail(s, avail & ~(uint64_t)SOMVR_VMA_PR_LIVE);
    return a | ((feat & SOMVR_VMF_HERBS) ? SOMVR_VMA_CTL_HERBS : 0u);
}
uint64_t somvr_vm_ctl_avail(const somvr_settings *s, uint64_t avail) {
    if (avail & SOMVR_VMA_STRING) /* build 19: the Bow string row: the string's button as it says */
        avail = (s->bow_string & 1u) ? avail | SOMVR_VMA_BOWTRIG : avail & ~(uint64_t)SOMVR_VMA_BOWTRIG;
    /* build 27: a Fight page row set Off takes its line away (the dodge, the wraith step, the stealth kill) */
    if (!s->dodge) avail &= ~(uint64_t)SOMVR_VMA_DODGE;
    if (!s->wstep) avail &= ~(uint64_t)SOMVR_VMA_WSTEP;
    if (!s->stealth) avail &= ~(uint64_t)SOMVR_VMA_STEALTH;
    if (!(avail & SOMVR_VMA_ARROWS)) return avail;
    avail &= ~(uint64_t)(SOMVR_VMA_BOWRT | SOMVR_VMA_QVGRIP);
    if (s->quiver) avail |= (avail & SOMVR_VMA_BOWTRIG) ? SOMVR_VMA_BOWRT : SOMVR_VMA_QVGRIP;
    return avail;
}
int somvr_vm_controls(int i, const char **key, const char **what) { return somvr_vm_controls_av(0, i, key, what); }
int somvr_vm_controls_av(uint64_t avail, int i, const char **key, const char **what) {
    int n = 0;
    *key = *what = NULL;
    for (int g = 0; g < CG_N; g++)
        for (size_t k = 0; k < VM_CTL_N; k++) {
            if (g_ctl[k].group != g || !ctl_on(avail, k)) continue;
            if (n == i) *key = g_ctl[k].key, *what = g_ctl[k].what;
            n++;
        }
    return n;
}
const char *somvr_vm_ctl_group(int g) { return g >= 0 && g < CG_N ? g_ctl_group[g] : NULL; }
/* the layout: the groups in order, each a title and its lines; a group that does not fit the rest of a page starts the
 * next (one longer than a page goes on over it, its title again). Calls fn for every entry; returns the pages. */
typedef void (*ctl_fn)(void *u, int page, int kind, int group, const char *key, const char *what, int y);
static int ctl_layout(uint64_t avail, ctl_fn fn, void *u) {
    int page = 0, y = SOMVR_VM_CTL_Y0, any = 0;
    for (int g = 0; g < CG_N; g++) {
        int n = 0;
        for (size_t k = 0; k < VM_CTL_N; k++) n += g_ctl[k].group == g && ctl_on(avail, k);
        if (!n) continue;
        int top = y == SOMVR_VM_CTL_Y0, need = (top ? 0 : SOMVR_VM_CTL_GAP) + (n + 1) * SOMVR_VM_CTL_DY;
        int whole = (n + 1) * SOMVR_VM_CTL_DY <= SOMVR_VM_CTL_BOTTOM - SOMVR_VM_CTL_Y0;
        if (!top && (y + need > SOMVR_VM_CTL_BOTTOM) && (whole || y + SOMVR_VM_CTL_GAP + 2 * SOMVR_VM_CTL_DY > SOMVR_VM_CTL_BOTTOM))
            page++, y = SOMVR_VM_CTL_Y0, top = 1;
        if (!top) y += SOMVR_VM_CTL_GAP;
        if (fn) fn(u, page, SOMVR_VMC_GROUP, g, g_ctl_group[g], NULL, y);
        y += SOMVR_VM_CTL_DY, any = 1;
        for (size_t k = 0; k < VM_CTL_N; k++) {
            if (g_ctl[k].group != g || !ctl_on(avail, k)) continue;
            if (y + SOMVR_VM_CTL_DY > SOMVR_VM_CTL_BOTTOM) { /* a group longer than a page: on over, its title again */
                page++, y = SOMVR_VM_CTL_Y0;
                if (fn) fn(u, page, SOMVR_VMC_GROUP, g, g_ctl_group[g], NULL, y);
                y += SOMVR_VM_CTL_DY;
            }
            if (fn) fn(u, page, SOMVR_VMC_LINE, g, g_ctl[k].key, g_ctl[k].what, y);
            y += SOMVR_VM_CTL_DY;
        }
    }
    return any ? page + 1 : 1;
}
int somvr_vm_ctl_pages(uint64_t avail) { return ctl_layout(avail, NULL, NULL); }
typedef struct { int page, i, n, kind, group, y; const char *key, *what; } ctl_find;
static void ctl_find_fn(void *u, int page, int kind, int group, const char *key, const char *what, int y) {
    ctl_find *f = u;
    if (page != f->page) return;
    if (f->n++ == f->i) f->kind = kind, f->group = group, f->key = key, f->what = what, f->y = y;
}
int somvr_vm_ctl_entry(uint64_t avail, int page, int i, const char **key, const char **what, int *y) {
    ctl_find f = {page, i, 0, SOMVR_VMC_NONE, -1, 0, NULL, NULL};
    ctl_layout(avail, ctl_find_fn, &f);
    if (key) *key = f.key;
    if (what) *what = f.what;
    if (y) *y = f.y;
    return f.kind;
}
int somvr_vm_ctl_turn(uint64_t avail, int page, int dir) {
    int pages = somvr_vm_ctl_pages(avail), p = page + (dir > 0 ? 1 : dir < 0 ? -1 : 0);
    return p < 0 ? 0 : p >= pages ? pages - 1 : p;
}
int somvr_vm_ctl_card(uint64_t avail, int i, const char **key, const char **what) {
    int n = 0;
    *key = *what = NULL;
    for (size_t k = 0; k < VM_CTL_N; k++) {
        if (!g_ctl[k].card || !ctl_on(avail, k)) continue;
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
    int top = SOMVR_VM_TEST_Y0 + t->n_title * 34 + 8, page = 0, y = top;
    int bottom = g_vml.on ? SOMVR_VM_LIVE_Y - 14 : SOMVR_VM_TEST_NAV_Y - 8; /* build 35: above the patch box */
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
    case SOMVR_VMI_PR_CAPTAIN: return SOMVR_VMPR_CAPTAIN; /* build 22 */
    case SOMVR_VMI_PR_PLACE: return SOMVR_VMPR_PLACE; /* build 31 */
    case SOMVR_VMI_PR_CHARGE: return SOMVR_VMPR_CHARGE; /* build 36 */
    case SOMVR_VMI_PR_HERB: return SOMVR_VMPR_HERB;     /* build 37 */
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
    case SOMVR_VMI_HANDED: snprintf(buf, cap, "%s", s->handed ? "Left" : "Right"); break; /* build 30 */
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
    case SOMVR_VMI_STRING: snprintf(buf, cap, "%s", (s->bow_string & 1u) ? "Trigger" : "Grip"); break; /* build 19 */
    case SOMVR_VMI_BOWTAKE: snprintf(buf, cap, "%s", (s->bow_string & 2u) ? "Grip" : "Shoulder"); break; /* build 33 */
    case SOMVR_VMI_DODGE: snprintf(buf, cap, "%s", s->dodge ? "On" : "Off"); break;
    case SOMVR_VMI_WSTEP: snprintf(buf, cap, "%s", s->wstep ? "On" : "Off"); break;
    case SOMVR_VMI_STEALTH: snprintf(buf, cap, "%s", s->stealth ? "On" : "Off"); break;
    case SOMVR_VMI_CROWD: /* the least time between two blows at Talion: "0.7 s"; 0 = every orc at once */
        if (!s->crowd_ms) snprintf(buf, cap, "Off");
        else fmt_num(s->crowd_ms / 1000.0, n, sizeof n), snprintf(buf, cap, "%s%s s", n, s->crowd_ms % 1000 ? "" : ".0");
        break;
    case SOMVR_VMI_CICON: snprintf(buf, cap, "%s", s->counter_icon ? "Shown" : "Hidden"); break;
    case SOMVR_VMI_MOVE: snprintf(buf, cap, "%s", s->move ? "Hand" : "Head"); break; /* build 27 */
    case SOMVR_VMI_REFRESH: /* build 27: the rate asked; 0 = the headset's own (nothing asked) */
        if (!s->refresh) snprintf(buf, cap, "Headset");
        else snprintf(buf, cap, "%u Hz", (unsigned)s->refresh);
        break;
    case SOMVR_VMI_PR_ORC: snprintf(buf, cap, "%s", g_orc_labels[s->orc < 6 ? s->orc : 0]); break;
    /* build 34 (re-xr-pipeline.md 150): the v8 rows */
    case SOMVR_VMI_WATCH: snprintf(buf, cap, "%s", s->watch_raise ? "Raised" : "Always"); break;
    case SOMVR_VMI_WATCH_SIZE: snprintf(buf, cap, "%s", s->watch_big ? "Large" : "Small"); break;
    case SOMVR_VMI_LOWHP: {
        static const char *const v[] = {"Off", "Edge", "Ring"};
        snprintf(buf, cap, "%s", v[s->low_health < 3 ? s->low_health : 0]);
        break;
    }
    case SOMVR_VMI_BEAT: snprintf(buf, cap, "%s", s->heartbeat ? "On" : "Off"); break;
    case SOMVR_VMI_FORCE: {
        static const char *const v[] = {"Low", "Normal", "High"};
        snprintf(buf, cap, "%s", v[s->force < 3 ? s->force : 1]);
        break;
    }
    case SOMVR_VMI_GHOST: snprintf(buf, cap, "%s", s->ghost ? "On" : "Off"); break;
    case SOMVR_VMI_KILLS: snprintf(buf, cap, "%s", s->kills ? "On" : "Off"); break;
    case SOMVR_VMI_EXEC: snprintf(buf, cap, "%s", s->exec_blow ? "Swing" : "Trigger"); break;
    case SOMVR_VMI_SHOVE: {
        static const char *const v[] = {"Soft", "Normal", "Hard"};
        snprintf(buf, cap, "%s", v[s->shove < 3 ? s->shove : 1]);
        break;
    }
    case SOMVR_VMI_HOLD: /* 0 = the game's own end of the grab (the switch's; the row steps 5 .. 30 s) */
        if (!s->hold_s) snprintf(buf, cap, "Game");
        else snprintf(buf, cap, "%u s", (unsigned)s->hold_s);
        break;
    case SOMVR_VMI_KNIFE: snprintf(buf, cap, "%s", s->knife_hold ? "Hold" : "Toggle"); break;
    case SOMVR_VMI_HERBS: snprintf(buf, cap, "%s", s->herbs ? "Hand" : "Button"); break;
    case SOMVR_VMI_FLING_DOWN: snprintf(buf, cap, "%s", s->fling_down ? "On" : "Off"); break;
    case SOMVR_VMI_CLIMB_SWORD: snprintf(buf, cap, "%s", s->climb_sword ? "On" : "Off"); break;
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
                   g_crowd[] = {0, 500, 700, 1000, 1500, 2000}, /* build 19: the crowd's pace, ms (0 = off) */
                   g_refresh[] = {72, 80, 90, 120}, /* build 27: the Quest 2's rates (Hz) */
                   g_hold[] = {5, 10, 20, 30}; /* build 34: a grabbed orc held up to (s) */
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
    if (item == SOMVR_VMI_CLOSE || item == SOMVR_VMI_PINX) return part == SOMVR_VMP_BUTTON ? SOMVR_VMR_CLOSE : SOMVR_VMR_NONE;
    if (item == SOMVR_VMI_PIN) { /* b37 live3: the panel's place - the layer moves it (settings.menu_pin) */
        if (part != SOMVR_VMP_BUTTON) return SOMVR_VMR_NONE;
        s->menu_pin = !s->menu_pin;
        return SOMVR_VMR_CHANGED;
    }
    if (somvr_vm_tab_page(item) >= 0) return part == SOMVR_VMP_BUTTON ? SOMVR_VMR_PAGE : SOMVR_VMR_NONE;
    if (item == SOMVR_VMI_REPORT) return part == SOMVR_VMP_BUTTON ? SOMVR_VMR_REPORT : SOMVR_VMR_NONE;
    if (item == SOMVR_VMI_QUIT) return part == SOMVR_VMP_BUTTON ? SOMVR_VMR_QUIT : SOMVR_VMR_NONE;
    if (item == SOMVR_VMI_TEST_PREV || item == SOMVR_VMI_TEST_NEXT || item == SOMVR_VMI_CTL_PREV ||
        item == SOMVR_VMI_CTL_NEXT) /* build 21; build 27: the Controls page's */
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
    /* build 30: the sword hand; the pointer goes with it (the main hand points, the off hand holds the panel, HL:A) -
     * the panel moves now, the controls swap at the next start */
    case SOMVR_VMI_HANDED: s->handed = !s->handed, s->hand = s->handed; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_VIEW: s->view = !s->view; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_FLING: s->fling = !s->fling; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_SURFACE: s->surface = !s->surface; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_SWHAND: s->sword_hand = !s->sword_hand; return SOMVR_VMR_CHANGED; /* build 17 */
    case SOMVR_VMI_BITE: s->bite = !s->bite; return SOMVR_VMR_CHANGED; /* build 17, the bite: < > flip it too */
    case SOMVR_VMI_ARROWS: s->quiver = !s->quiver; return SOMVR_VMR_CHANGED;
    /* build 19: the Fight page's toggles (< > flip them too) */
    case SOMVR_VMI_STRING: s->bow_string ^= 1u; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_BOWTAKE: s->bow_string ^= 2u; return SOMVR_VMR_CHANGED; /* build 33 */
    case SOMVR_VMI_DODGE: s->dodge = !s->dodge; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_WSTEP: s->wstep = !s->wstep; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_STEALTH: s->stealth = !s->stealth; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_CICON: s->counter_icon = !s->counter_icon; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_MOVE: s->move = !s->move; return SOMVR_VMR_CHANGED; /* build 27 */
    case SOMVR_VMI_REFRESH: { /* build 27: 72 / 80 / 90 / 120 Hz (from the headset's own 0: > 72, < 120) */
        float v = s->refresh ? (float)s->refresh : dir > 0 ? 0.0f : 1000.0f;
        float nv = step(v, g_refresh, (int)(sizeof g_refresh / sizeof g_refresh[0]), dir, wrap);
        uint8_t hz = nv > 0 && nv < 1000 ? (uint8_t)lrintf(nv) : s->refresh;
        if (hz == s->refresh) return SOMVR_VMR_NONE;
        s->refresh = hz;
        return SOMVR_VMR_CHANGED;
    }
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
    case SOMVR_VMI_PR_ORC: return cycle(&s->orc, 6, dir, wrap);
    case SOMVR_VMI_WEIGHT: {
        uint32_t w = s->sword_weight;
        int r = cycle(&w, 3, dir, wrap);
        s->sword_weight = (uint8_t)w;
        return r;
    }
    /* build 34 (re-xr-pipeline.md 150): the v8 rows - toggles (< > flip them too), three-way choices, the hold's s */
    case SOMVR_VMI_WATCH: s->watch_raise = !s->watch_raise; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_WATCH_SIZE: s->watch_big = !s->watch_big; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_BEAT: s->heartbeat = !s->heartbeat; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_GHOST: s->ghost = !s->ghost; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_KILLS: s->kills = !s->kills; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_EXEC: s->exec_blow = !s->exec_blow; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_KNIFE: s->knife_hold = !s->knife_hold; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_HERBS: s->herbs = !s->herbs; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_FLING_DOWN: s->fling_down = !s->fling_down; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_CLIMB_SWORD: s->climb_sword = !s->climb_sword; return SOMVR_VMR_CHANGED;
    case SOMVR_VMI_LOWHP:
    case SOMVR_VMI_FORCE:
    case SOMVR_VMI_SHOVE: {
        uint8_t *f = item == SOMVR_VMI_LOWHP ? &s->low_health : item == SOMVR_VMI_FORCE ? &s->force : &s->shove;
        uint32_t w = *f < 3 ? *f : 1;
        int r = cycle(&w, 3, dir, wrap);
        *f = (uint8_t)w;
        return r;
    }
    case SOMVR_VMI_HOLD: {
        float nv = step((float)s->hold_s, g_hold, (int)(sizeof g_hold / sizeof g_hold[0]), dir, wrap);
        uint8_t v = (uint8_t)lrintf(nv);
        if (v == s->hold_s) return SOMVR_VMR_NONE;
        s->hold_s = v;
        return SOMVR_VMR_CHANGED;
    }
    default: return SOMVR_VMR_NONE;
    }
}

/* ---- the layout --------------------------------------------------------------------------------------------------- */
#define ROW_Y0 64    /* the first row's top (builds 1-33; build 34: SET_Y0, under the groups' buttons) */
#define ROW_H 38      /* v4: 16 rows (40 px for 15 before); v5: closer when more than 16 rows are shown (row_h) */
#define ROW_FIT 13    /* rows of ROW_H above the bottom row of buttons (16 from ROW_Y0 before build 34) */
/* build 34 (re-xr-pipeline.md 150): the Settings tab's six groups - two rows of three buttons (152 x 36, 8 apart) under
 * the tab strip, a line under them, the group's rows from SET_Y0 */
#define SUB_Y0 66
#define SUB_H 36
#define SUB_DY 40
#define SUB_W 152
#define SUB_DX 160
#define SUB_LINE_Y 147
#define SET_Y0 152
#define LESS_X 256   /* the < box */
#define MORE_X 448   /* the > box */
#define BOX_W 48
#define BOX_H 34
#define VALUE_X0 (LESS_X + BOX_W) /* the value is centred between the boxes */
#define RECENTER_Y (ROW_Y0 + 16 * ROW_H + 12) /* the bottom row: where 16 rows ended (684), however many are shown */
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
/* build 36: Charge execution under Refill health / Place orc, the hints a row lower (their last ink ends at 764 of 768) */
#define PR_CHARGE_Y 568
#define PR_CHARGE_W 272 /* build 37: "Charge execution" (256 px) and its margins; New herb right of it */
#define PR_HINT_Y 628
/* build 19: the Spawn row split - Spawn an orc (40..260) | Spawn group (272..472) */
#define PR_SPAWN_W 220
#define PR_GROUP_X 272
#define PR_GROUP_W 200
/* build 22: the Remove row split - Remove the orc (40..288) | Captain (300..472) */
#define PR_REMOVE_W 248
#define PR_CAPTAIN_X 300
#define PR_CAPTAIN_W 172
/* build 31: the Refill health row split - Refill health (40..264) | Place orc (276..472) */
#define PR_HEALTH_W 224
#define PR_PLACE_X 276
#define PR_PLACE_W 196

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
    case SOMVR_VMI_BOWTAKE: return (avail & SOMVR_VMA_STRING) != 0; /* build 33 */
    case SOMVR_VMI_DODGE: return (avail & SOMVR_VMA_DODGE) != 0;
    case SOMVR_VMI_WSTEP: return (avail & SOMVR_VMA_WSTEP) != 0;
    case SOMVR_VMI_STEALTH: return (avail & SOMVR_VMA_STEALTH) != 0;
    case SOMVR_VMI_CROWD: return (avail & SOMVR_VMA_CROWD) != 0;
    case SOMVR_VMI_CICON: return (avail & SOMVR_VMA_CICON) != 0;
    case SOMVR_VMI_MOVE: return (avail & SOMVR_VMA_WALKDIR) != 0; /* build 27: the walk's pre-rotation runs */
    case SOMVR_VMI_REFRESH: return (avail & SOMVR_VMA_REFRESH) != 0; /* build 27: the runtime's rates (the layer) */
    /* build 30 (re-xr-pipeline.md 132): Handedness in Pointer hand's place - it sets the pointer too (the main hand
     * points, HL:A); the HAND key stays the file's (the pointer alone) */
    case SOMVR_VMI_HAND: return 0;
    default: return item >= 0 && item < SOMVR_VMI_ALL;
    }
}
/* the value row item's slot among the shown rows of its group's page (-1 = not shown) and the rows shown there (build 34:
 * the group's order, g_group_rows; no page button under them any more) */
static int row_slot(uint64_t avail, int item, int *n_shown) {
    int g = group_of_page(somvr_vm_row_page(item)), slot = -1, n = 0;
    for (int j = 0; g >= 0 && g_group_rows[g][j] >= 0; j++) {
        int i = g_group_rows[g][j];
        if (!somvr_vm_shown(avail, i)) continue;
        if (i == item) slot = n;
        n++;
    }
    if (n_shown) *n_shown = n;
    return slot;
}
static int row_h(int n) { return n <= ROW_FIT ? ROW_H : ROW_FIT * ROW_H / n; }

int somvr_vm_rect(int item, int part, int rc[4]) { return somvr_vm_rect_av(0, item, part, rc); }
int somvr_vm_rect_av(uint64_t avail, int item, int part, int rc[4]) {
    if (item == SOMVR_VMI_LIVE_APPLY || item == SOMVR_VMI_LIVE_UNDO) { /* build 35: the patch box's (on the Test page) */
        if (!g_vml.on || part != SOMVR_VMP_BUTTON) return 0;
        rc[0] = item == SOMVR_VMI_LIVE_APPLY ? 16 : 264, rc[1] = SOMVR_VM_LIVE_BTN_Y, rc[2] = 232, rc[3] = 56;
        return 1;
    }
    rc[0] = rc[1] = rc[2] = rc[3] = 0;
    if (item == SOMVR_VMI_PG_FIGHT || item == SOMVR_VMI_PG_BACK) return 0; /* build 34: the groups' buttons instead */
    if (item >= SOMVR_VMI_SUB_COMFORT && item <= SOMVR_VMI_SUB_BOW) { /* build 34: two rows of three */
        if (part != SOMVR_VMP_BUTTON) return 0;
        int i = item - SOMVR_VMI_SUB_COMFORT;
        rc[0] = 16 + (i % 3) * SUB_DX, rc[1] = SUB_Y0 + (i / 3) * SUB_DY, rc[2] = SUB_W, rc[3] = SUB_H;
        return 1;
    }
    if (item == SOMVR_VMI_CTL_PREV || item == SOMVR_VMI_CTL_NEXT) { /* build 27: the Controls page's, the Test row's place */
        if (part != SOMVR_VMP_BUTTON) return 0;
        rc[0] = item == SOMVR_VMI_CTL_PREV ? 16 : 344, rc[1] = SOMVR_VM_TEST_NAV_Y, rc[2] = 152, rc[3] = 56;
        return 1;
    }
    int t4 = (avail & SOMVR_VMA_TESTPAGE) != 0; /* build 21: the four tabs, no X on the panel, the Test page's row */
    if (item == SOMVR_VMI_TAB_TEST || item == SOMVR_VMI_TEST_PREV || item == SOMVR_VMI_TEST_NEXT || (t4 && item == SOMVR_VMI_CLOSE)) {
        if (!t4 || part != SOMVR_VMP_BUTTON || item == SOMVR_VMI_CLOSE) return 0;
        if (item == SOMVR_VMI_TAB_TEST) rc[0] = TAB4_X3, rc[1] = 10, rc[2] = TAB4_W3, rc[3] = 44;
        else rc[0] = item == SOMVR_VMI_TEST_PREV ? 16 : 344, rc[1] = SOMVR_VM_TEST_NAV_Y, rc[2] = 152, rc[3] = 56;
        return 1;
    }
    if (item == SOMVR_VMI_PIN || item == SOMVR_VMI_PINX) { /* b37 live3: the bottom bar, every page */
        if (part != SOMVR_VMP_BUTTON) return 0;
        if (item == SOMVR_VMI_PIN) rc[0] = 16, rc[1] = SOMVR_VM_PIN_Y, rc[2] = 312, rc[3] = 40;
        else rc[0] = 340, rc[1] = SOMVR_VM_PIN_Y, rc[2] = 156, rc[3] = 40;
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
        else if (pa == SOMVR_VMPR_CAPTAIN) rc[0] = PR_CAPTAIN_X, rc[1] = PR_REMOVE_Y, rc[2] = PR_CAPTAIN_W, rc[3] = PR_BTN_H;
        else if (pa == SOMVR_VMPR_PLACE) /* build 31: the Refill health row split, as the Spawn row */
            rc[0] = PR_PLACE_X, rc[1] = PR_HEALTH_Y, rc[2] = PR_PLACE_W, rc[3] = PR_BTN_H;
        else if (pa == SOMVR_VMPR_CHARGE) /* build 36: a row of its own, the width of the refills'; build 37: its left part */
            rc[0] = 40, rc[1] = PR_CHARGE_Y, rc[2] = PR_CHARGE_W, rc[3] = PR_BTN_H;
        else if (pa == SOMVR_VMPR_HERB) /* build 37: New herb, the row's right part */
            rc[0] = 40 + PR_CHARGE_W + 12, rc[1] = PR_CHARGE_Y, rc[2] = SOMVR_VM_TEX_W - 80 - PR_CHARGE_W - 12, rc[3] = PR_BTN_H;
        else
            rc[0] = 40, rc[1] = pa == SOMVR_VMPR_ARROWS ? PR_ARROWS_Y
                                : pa == SOMVR_VMPR_HEALTH ? PR_HEALTH_Y
                                : pa == SOMVR_VMPR_FAR    ? PR_FAR_Y
                                                          : by[item - SOMVR_VMI_PR_GO],
            rc[2] = pa == SOMVR_VMPR_SPAWN ? PR_SPAWN_W : pa == SOMVR_VMPR_HEALTH ? PR_HEALTH_W
                    : pa == SOMVR_VMPR_REMOVE ? PR_REMOVE_W : SOMVR_VM_TEX_W - 80,
            rc[3] = PR_BTN_H;
        return 1;
    }
    if (item != SOMVR_VMI_PR_ORC && (item < 0 || item >= SOMVR_VMI_ROWS)) return 0;
    int n = 0, slot = item == SOMVR_VMI_PR_ORC ? 0 : row_slot(avail, item, &n);
    if (slot < 0) return 0; /* not shown */
    int h = item == SOMVR_VMI_PR_ORC ? ROW_H : row_h(n), y = item == SOMVR_VMI_PR_ORC ? PR_ORC_Y : SET_Y0 + slot * h;
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
static const int g_page_buttons[5][20] = { /* (build 36: 17 - Charge execution; b37: New herb + live3's Pin / X: 20) */
    /* the Settings tab's groups (build 34: their six buttons; build 19's page buttons gone) */
    {SOMVR_VMI_RECENTER, SOMVR_VMI_REPORT, SOMVR_VMI_QUIT, SOMVR_VMI_CLOSE, SOMVR_VMI_TAB, SOMVR_VMI_TAB_SET,
     SOMVR_VMI_TAB_CTL, SOMVR_VMI_TAB_TEST, SOMVR_VMI_SUB_COMFORT, SOMVR_VMI_SUB_DISPLAY, SOMVR_VMI_SUB_CLIMB,
     SOMVR_VMI_SUB_SWORD, SOMVR_VMI_SUB_FIGHT, SOMVR_VMI_SUB_BOW, SOMVR_VMI_PIN, SOMVR_VMI_PINX, -1},
    {SOMVR_VMI_CLOSE, SOMVR_VMI_TAB, SOMVR_VMI_TAB_SET, SOMVR_VMI_TAB_CTL, SOMVR_VMI_TAB_TEST, SOMVR_VMI_PR_GO,
     SOMVR_VMI_PR_BACK, SOMVR_VMI_PR_SPAWN, SOMVR_VMI_PR_REMOVE, SOMVR_VMI_PR_ARROWS, SOMVR_VMI_PR_HEALTH,
     SOMVR_VMI_PR_FAR, SOMVR_VMI_PR_GROUP, SOMVR_VMI_PR_CAPTAIN, SOMVR_VMI_PR_PLACE, SOMVR_VMI_PR_CHARGE,
     SOMVR_VMI_PR_HERB, SOMVR_VMI_PIN, SOMVR_VMI_PINX, -1},
    /* the Controls page: the tabs, X, build 27: its Prev / Next */
    {SOMVR_VMI_CLOSE, SOMVR_VMI_TAB, SOMVR_VMI_TAB_SET, SOMVR_VMI_TAB_CTL, SOMVR_VMI_TAB_TEST, SOMVR_VMI_CTL_PREV,
     SOMVR_VMI_CTL_NEXT, SOMVR_VMI_PIN, SOMVR_VMI_PINX, -1},
    /* (build 19's Fight page with its back button: build 34's groups share the first list) */
    {-1},
    /* build 21: the Test page = the tabs, its Prev / Next (the X: none with SOMVR_VMA_TESTPAGE - its rect is 0) */
    {SOMVR_VMI_TAB, SOMVR_VMI_TAB_SET, SOMVR_VMI_TAB_CTL, SOMVR_VMI_TAB_TEST, SOMVR_VMI_TEST_PREV, SOMVR_VMI_TEST_NEXT,
     SOMVR_VMI_LIVE_APPLY, SOMVR_VMI_LIVE_UNDO /* build 35: a rect only with the patch box */, SOMVR_VMI_PIN,
     SOMVR_VMI_PINX, -1}};
static const int *page_buttons(int page) {
    return g_page_buttons[page == SOMVR_VMPG_PRACTICE   ? 1
                          : page == SOMVR_VMPG_CONTROLS ? 2
                          : page == SOMVR_VMPG_TEST     ? 4
                                                        : 0]; /* build 34: every settings group */
}
somvr_vm_hit somvr_vm_hit_test(float u, float v) { return somvr_vm_hit_test_av(0, SOMVR_VMPG_SETTINGS, u, v); }
somvr_vm_hit somvr_vm_hit_test_page(int page, float u, float v) { return somvr_vm_hit_test_av(0, page, u, v); }
somvr_vm_hit somvr_vm_hit_test_av(uint64_t avail, int page, float u, float v) {
    somvr_vm_hit h = {-1, SOMVR_VMP_NONE};
    if (!(u >= 0 && u <= 1 && v >= 0 && v <= 1)) return h; /* NaN too */
    float x = u * SOMVR_VM_TEX_W, y = v * SOMVR_VM_TEX_H;
    int rc[4];
    const int *bl = page_buttons(page);
    for (int i = 0; i < 19 && bl[i] >= 0; i++) /* (build 36: the practice page's 16; b37: 19 with New herb, Pin, X) */
        if (somvr_vm_rect_av(avail, bl[i], SOMVR_VMP_BUTTON, rc) && in_rect(rc, x, y))
            return (somvr_vm_hit){bl[i], SOMVR_VMP_BUTTON};
    if (page == SOMVR_VMPG_CONTROLS || page == SOMVR_VMPG_TEST) return h; /* text only */
    int r0 = page == SOMVR_VMPG_PRACTICE ? SOMVR_VMI_PR_ORC : 0, r1 = page == SOMVR_VMPG_PRACTICE ? r0 + 1 : SOMVR_VMI_ROWS;
    for (int item = r0; item < r1; item++) {
        if (page != SOMVR_VMPG_PRACTICE && somvr_vm_row_page(item) != page)
            continue; /* build 19: the other page's row; build 34: another group's */
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
    const char *lab = somvr_vm_label(item);
    char ml[32];
    if (item == SOMVR_VMI_HANDED && v->set.handed != v->set.handed_on) /* build 30: changed, not applied yet */
        lab = "At next start";
    else if (v->set.handed_on) /* build 30: "Dodge (X)" */
        somvr_vm_mirror_text(lab, ml, sizeof ml), lab = ml;
    text_at(f, c, 20, ty, lab, on ? C_LABEL : C_DIM);
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
    if (!g_vml.on) return;
    /* build 35 (re-xr-pipeline.md 162): the patch box - a rule, its three lines, Apply patch | Undo patch */
    fill(c, 16, SOMVR_VM_LIVE_Y - 8, SOMVR_VM_TEX_W - 32, 2, C_TITLE);
    for (int i = 0; i < 3; i++)
        if (g_vml.line[i][0])
            text_at(f, c, 20, SOMVR_VM_LIVE_Y + 30 * i, g_vml.line[i], i == 0 ? C_TITLE : i == 1 ? C_VALUE : C_LABEL);
    for (int i = 0; i < 2; i++) {
        int item = i ? SOMVR_VMI_LIVE_UNDO : SOMVR_VMI_LIVE_APPLY, rc[4];
        if (!somvr_vm_rect_av(v->avail, item, SOMVR_VMP_BUTTON, rc)) continue;
        int on = i ? g_vml.can_undo : g_vml.can_apply, hov = on && v->hover.item == item;
        fill_rc(c, rc, !on ? C_BOX_DIM : hov ? C_BOX_HOV : C_BOX);
        if (v->lock && hov) outline(c, rc, 3, C_LOCK);
        text_mid(f, c, rc[0], rc[0] + rc[2], rc[1] + (rc[3] - SOMVR_FONT_H * SOMVR_VM_SCALE) / 2, somvr_vm_label(item),
                 on ? C_VALUE : C_DIM);
    }
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
    int pr = v->page == SOMVR_VMPG_PRACTICE, ctl = v->page == SOMVR_VMPG_CONTROLS;
    int grp = somvr_vm_settings_page(v->page); /* build 34: one of the Settings tab's groups */
    if (v->card) { /* the arrival card: a title instead of the tabs */
        text_at(f, &c, 24, 14, "CONTROLS", C_TITLE);
    } else { /* the tab strip: the page's own tab lit as the title (underlined), the others as buttons */
        static const int tabs[4] = {SOMVR_VMI_TAB_SET, SOMVR_VMI_TAB, SOMVR_VMI_TAB_CTL, SOMVR_VMI_TAB_TEST};
        for (int i = 0; i < 4; i++) {
            if (!somvr_vm_enabled(&v->set, v->avail, tabs[i])) continue;
            int rc[4];
            somvr_vm_rect_av(v->avail, tabs[i], SOMVR_VMP_BUTTON, rc); /* build 21: four with SOMVR_VMA_TESTPAGE */
            if (somvr_vm_tab_page(tabs[i]) == (grp ? SOMVR_VMPG_SETTINGS : v->page)) { /* (a group: Settings') */
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
    if (ctl && v->card) { /* the arrival card (build 27): the first steps, where the rest is, how to close it */
        const char *k, *w;
        uint64_t ca = somvr_vm_ctl_avail_f(&v->set, v->avail, v->ctl_feat);
        int y = SOMVR_VM_CTL_Y0 + 4, n = somvr_vm_ctl_card(ca, 0, &k, &w);
        for (int i = 0; i < n; i++, y += SOMVR_VM_CARD_DY) {
            somvr_vm_ctl_card(ca, i, &k, &w);
            char mk[32], mw[48]; /* build 30: the left-handed mirror's words */
            if (somvr_vm_ctl_mirrored(&v->set, k))
                somvr_vm_mirror_text(k, mk, sizeof mk), somvr_vm_mirror_text(w, mw, sizeof mw), k = mk, w = mw;
            text_at(f, &c, 20, y, k, C_TITLE);
            text_at(f, &c, SOMVR_VM_CTL_WHAT_X, y, w, C_VALUE);
        }
        y += 12;
        text_at(f, &c, 20, y, "Everything else:", C_LABEL);
        text_at(f, &c, 20, y + SOMVR_VM_CARD_DY, "Menu > Controls tab", C_LABEL);
        y += 2 * SOMVR_VM_CARD_DY + 12;
        if (v->avail & SOMVR_VMA_PRACTICE) { /* the card comes at the practice area's arrival: the way to the game */
            text_at(f, &c, 20, y, "This is the practice area.", C_LABEL);
            text_at(f, &c, 20, y + SOMVR_VM_CARD_DY, "Your game: Menu > Practice", C_LABEL);
            text_at(f, &c, 20, y + 2 * SOMVR_VM_CARD_DY, "> Back to where I was", C_LABEL);
            y += 3 * SOMVR_VM_CARD_DY + 12;
        }
        text_at(f, &c, 20, y, v->set.handed_on ? "Close: X box or X button" : "Close: X box or A", C_DIM);
    } else if (ctl) { /* build 27: the groups' titles and lines, this page's; Prev / Next with "page / pages" */
        const char *k, *w;
        uint64_t ca = somvr_vm_ctl_avail_f(&v->set, v->avail, v->ctl_feat); /* build 17: the bow's lines as the Arrows row says */
        int pages = somvr_vm_ctl_pages(ca), pg = v->ctl_page < 0 ? 0 : v->ctl_page >= pages ? pages - 1 : v->ctl_page, y;
        for (int i = 0, kind; (kind = somvr_vm_ctl_entry(ca, pg, i, &k, &w, &y)) != SOMVR_VMC_NONE; i++) {
            if (kind == SOMVR_VMC_GROUP) { /* the title, a thin line under it */
                text_at(f, &c, 20, y, k, C_TITLE);
                fill(&c, 20, y + SOMVR_VM_CTL_DY - 3, SOMVR_VM_TEX_W - 40, 1, C_LINE);
                continue;
            }
            char mk[32], mw[48]; /* build 30: the left-handed mirror's words */
            if (somvr_vm_ctl_mirrored(&v->set, k))
                somvr_vm_mirror_text(k, mk, sizeof mk), somvr_vm_mirror_text(w, mw, sizeof mw), k = mk, w = mw;
            text_at(f, &c, 20, y, k, C_LABEL);
            text_at(f, &c, SOMVR_VM_CTL_WHAT_X, y, w, C_VALUE);
        }
        for (int i = 0; i < 2; i++) {
            int item = i ? SOMVR_VMI_CTL_NEXT : SOMVR_VMI_CTL_PREV, rc[4];
            if (!somvr_vm_rect_av(v->avail, item, SOMVR_VMP_BUTTON, rc)) continue;
            int on = i ? pg < pages - 1 : pg > 0, hov = on && v->hover.item == item;
            fill_rc(&c, rc, !on ? C_BOX_DIM : hov ? C_BOX_HOV : C_BOX);
            if (v->lock && hov) outline(&c, rc, 3, C_LOCK);
            static const char prev[] = {(char)SOMVR_FONT_LEFT, ' ', 'P', 'r', 'e', 'v', 0},
                              next[] = {'N', 'e', 'x', 't', ' ', (char)SOMVR_FONT_RIGHT, 0};
            text_mid(f, &c, rc[0], rc[0] + rc[2], rc[1] + (rc[3] - SOMVR_FONT_H * SOMVR_VM_SCALE) / 2, i ? next : prev,
                     on ? C_VALUE : C_DIM);
        }
        char num[32];
        snprintf(num, sizeof num, "%d / %d", pg + 1, pages);
        text_mid(f, &c, 168, 344, SOMVR_VM_TEST_NAV_Y + (56 - SOMVR_FONT_H * SOMVR_VM_SCALE) / 2, num, C_LABEL);
    } else if (v->page == SOMVR_VMPG_TEST) { /* build 21: what to test in this build */
        draw_test(f, &c, v);
    } else if (!pr) { /* the Settings tab's groups (build 34): the six buttons (this one lit), its rows, the help */
        for (int i = 0; i < SOMVR_VM_SUBS; i++) {
            int item = SOMVR_VMI_SUB_COMFORT + i, rc[4];
            if (!somvr_vm_rect_av(v->avail, item, SOMVR_VMP_BUTTON, rc)) continue;
            if (somvr_vm_tab_page(item) == v->page) { /* the group shown: its name lit and underlined, as a tab */
                fill_rc(&c, rc, C_ROW_HOV);
                fill(&c, rc[0] + 8, rc[1] + rc[3] - 4, rc[2] - 16, 3, C_TITLE);
                text_mid(f, &c, rc[0], rc[0] + rc[2], rc[1] + (rc[3] - SOMVR_FONT_H * SOMVR_VM_SCALE) / 2,
                         somvr_vm_label(item), C_TITLE);
                if (v->lock && v->hover.item == item) outline(&c, rc, 3, C_LOCK);
            } else {
                draw_button(f, &c, v, item, NULL);
            }
        }
        fill(&c, 16, SUB_LINE_Y, SOMVR_VM_TEX_W - 32, 1, C_LINE);
        int n = 0, last = -1;
        for (int item = 0; item < SOMVR_VMI_ROWS; item++)
            if (somvr_vm_row_page(item) == v->page) {
                draw_row(f, &c, v, item, 1);
                int rc[4];
                if (somvr_vm_rect_av(v->avail, item, SOMVR_VMP_ROW, rc) && rc[1] + rc[3] > last) last = rc[1] + rc[3];
                n++;
            }
        const char *h1, *h2;
        if (n && last > 0 && somvr_vm_row_page(v->hover.item) == v->page && somvr_vm_help(v->hover.item, &h1, &h2) &&
            last + 14 + 2 * 30 <= RECENTER_Y) { /* the hovered row's two lines (the room under the group's rows) */
            char m1[48], m2[48]; /* build 30: the left-handed mirror's words */
            if (v->set.handed_on)
                somvr_vm_mirror_text(h1, m1, sizeof m1), somvr_vm_mirror_text(h2, m2, sizeof m2), h1 = m1, h2 = m2;
            text_at(f, &c, 24, last + 14, h1, C_HELP);
            text_at(f, &c, 24, last + 14 + 30, h2, C_HELP);
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
        draw_button(f, &c, v, SOMVR_VMI_PR_CAPTAIN, NULL); /* build 22 */
        draw_button(f, &c, v, SOMVR_VMI_PR_ARROWS, NULL); /* build 17 */
        draw_button(f, &c, v, SOMVR_VMI_PR_HEALTH, NULL);
        draw_button(f, &c, v, SOMVR_VMI_PR_PLACE, NULL); /* build 31 */
        draw_button(f, &c, v, SOMVR_VMI_PR_CHARGE, NULL); /* build 36 */
        draw_button(f, &c, v, SOMVR_VMI_PR_HERB, NULL);   /* build 37 */
        text_at(f, &c, 24, PR_HINT_Y, "No enemies here.", C_DIM);
        text_at(f, &c, 24, PR_HINT_Y + 32, "Only a captain can kill you.", C_DIM); /* build 22 (SOMVR_CAPTAIN) */
        /* build 17: how to defend against the orc (somvr_defence.h: the blade in the blow's path; the red warning's
         * attacks - cannot parry / block - only dodged: the head 30 cm aside or 40 cm back) */
        text_at(f, &c, 24, PR_HINT_Y + 72, "Block: sword across a blow.", C_LABEL);
        text_at(f, &c, 24, PR_HINT_Y + 104, "Red: can't block, step aside.", C_LABEL);
    }
    draw_button(f, &c, v, SOMVR_VMI_CLOSE, NULL);
    if (!v->card) { /* b37 live3: the bottom bar - where the panel is, and its close */
        draw_button(f, &c, v, SOMVR_VMI_PIN, v->pinned ? "Back to the wrist" : "Pin in front");
        draw_button(f, &c, v, SOMVR_VMI_PINX, "X  Close");
    }
}

/* ---- the gesture and the panel's pose ------------------------------------------------------------------------------ */
void somvr_vm_gcfg_default(somvr_vm_gcfg *c) {
    *c = (somvr_vm_gcfg){.face_on = 0.70710678f, .face_off = 0.5f, .look_on = 0.76604444f, .look_off = 0.5f,
                         .reach_m = 0.8f, .hold_s = 0.5f, .rearm_s = 0.25f, .away_s = 0.5f, .lost_s = 1.0f,
                         .up_m = 0.15f, .out_m = 0.05f, .gate_s = 0.3f, .use_m = 0.25f, .use_s = 0.5f,
                         .drop_cos = -0.2f, .drop_s = 0.5f};
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

int somvr_vm_pin_pose(const somvr_posef *head, float dist_m, float drop_m, somvr_posef *out) {
    if (!unit_quat(head->q) || !isfinite(head->p[0] + head->p[1] + head->p[2])) return 0;
    static const float f0[3] = {0, 0, -1}, up[3] = {0, 1, 0};
    float f[3];
    qrot(head->q, f0, f);
    f[1] = 0;
    float l = sqrtf(dot3(f, f));
    if (l < 1e-3f) f[0] = 0, f[2] = -1, l = 1; /* (straight up or down: XR's -Z) */
    f[0] /= l, f[2] /= l;
    float p[3] = {head->p[0] + dist_m * f[0], head->p[1] - drop_m, head->p[2] + dist_m * f[2]};
    float z[3] = {head->p[0] - p[0], head->p[1] - p[1], head->p[2] - p[2]}, x[3], y[3];
    l = sqrtf(dot3(z, z));
    for (int i = 0; i < 3; i++) z[i] /= l;
    x[0] = up[1] * z[2] - up[2] * z[1], x[1] = up[2] * z[0] - up[0] * z[2], x[2] = up[0] * z[1] - up[1] * z[0];
    l = sqrtf(dot3(x, x));
    for (int i = 0; i < 3; i++) x[i] /= l;
    y[0] = z[1] * x[2] - z[2] * x[1], y[1] = z[2] * x[0] - z[0] * x[2], y[2] = z[0] * x[1] - z[1] * x[0];
    quat_of_basis(x, y, z, out->q);
    memcpy(out->p, p, sizeof p);
    return 1;
}

static const char *const g_why[] = {"none", "the palm gesture", "the menu button", "looked away", "the hand lost",
                                    "its close box", "off", "the wrist turned over (the watch)",
                                    "the bow came out in its hand", "the palm dropped"};
const char *somvr_vm_why(int why) { return why >= 0 && why <= SOMVR_VMW_DROPPED ? g_why[why] : "?"; }

void somvr_vm_greset(somvr_vm_gstate *g) {
    memset(g, 0, sizeof *g);
    g->armed = 1;
    g->away_t = g->nf_t = g->lost_t = g->gate_t = g->soft_t = g->use_t = g->drop_t = -1;
}

uint8_t somvr_vm_test_key(const char *text, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; text && i < n; i++) h = (h ^ (uint8_t)text[i]) * 16777619u;
    uint8_t k = (uint8_t)(h ^ h >> 8 ^ h >> 16 ^ h >> 24);
    return k ? k : 1;
}

void somvr_vm_gate_text(uint32_t bits, char *buf, size_t cap) {
    static const char *const w[] = {"the bow", "a climb hold", "a blade swing", "an attack incoming",
                                    "the sword hand's swing"};
    size_t n = 0;
    if (!cap) return;
    buf[0] = 0;
    for (int i = 0; i < 5; i++)
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
    float look = -2, cf = 2;
    if (have && in->head_ok && unit_quat(in->head.q)) {
        float n[3], v[3], w[3], fwd[3];
        static const float f0[3] = {0, 0, -1};
        somvr_vm_palm(&in->grip, in->hand, n);
        for (int i = 0; i < 3; i++) v[i] = in->head.p[i] - in->grip.p[i], w[i] = panel->p[i] - in->head.p[i];
        float dv = sqrtf(dot3(v, v)), dw = sqrtf(dot3(w, w));
        qrot(in->head.q, f0, fwd);
        cf = dv > 0.05f ? dot3(n, v) / dv : 2; /* (too close to say: not dropped) */
        look = dw > 0.01f ? dot3(fwd, w) / dw : -2;
        gaze = 1;
        face = dv <= c->reach_m && (g->facing ? cf > c->face_off && look > c->look_off
                                              : cf > c->face_on && look > c->look_on);
    }
    /* b37 live3: in use - the pointing hand at the open panel (the layer's using), or its ray on it (busy) */
    if (g->open && (in->using || in->busy)) g->use_t = t;
    g->in_use = g->open && g->use_t >= 0 && t - g->use_t < c->use_s;
    if (g->open && cf < c->drop_cos) {
        if (g->drop_t < 0) g->drop_t = t;
    } else {
        g->drop_t = -1;
    }
    if ((face && !g->facing) || (g->open && in->busy)) g->since = t; /* the hold counts while not pointing at it */
    if (face) g->nf_t = -1;
    else if (g->nf_t < 0) g->nf_t = t;
    g->facing = face;
    if (!g->armed && !face && t - g->nf_t >= c->rearm_s) g->armed = 1;
    /* the gate: the hands are busy (the bow, a climb, a swing, an attack incoming) and gate_s after it - a closed
     * panel's gesture is disarmed every frame, so a palm held through it must turn away and face again */
    uint32_t blk = in->block & SOMVR_VMG_ALL, hard = blk & SOMVR_VMG_HARD, soft = blk & SOMVR_VMG_SOFT;
    if (hard) g->gate_t = t;
    if (soft) g->soft_t = t;
    int gate = hard || (g->gate_t >= 0 && t - g->gate_t < c->gate_s);
    int sgate = soft || (g->soft_t >= 0 && t - g->soft_t < c->gate_s); /* build 31: waits, no disarm */
    g->gate_why = gate || sgate ? g->gate_why | blk : 0;
    g->gated = gate && !g->open;
    g->soft_wait = sgate && !gate && !g->open;
    if (g->gated) {
        g->armed = 0;
        g->gate_frames++;
        if (face && !g->held) g->held = 1, g->held_off++;
    }
    if (g->soft_wait && face && g->armed && !g->soft_held) g->soft_held = 1, g->soft_waits++;
    if (!face || !sgate) g->soft_held = 0;
    if (!face) g->held = 0;
    int ev = 0;
    if (in->toggles & 1u) { /* the menu button (an even count: open and closed again) */
        g->open = !g->open, g->why = SOMVR_VMW_BUTTON, g->armed = 0;
        ev = g->open ? 1 : -1;
    } else if (g->armed && face && t - g->since >= c->hold_s && !(g->open && in->busy) && !g->soft_wait) {
        if (g->open && g->in_use) { /* b37 live3: no toggle-close while it is used (re-armed by a small wrist turn) */
            g->armed = 0, g->use_held++;
        } else {
            g->open = !g->open, g->why = SOMVR_VMW_GESTURE, g->armed = 0;
            ev = g->open ? 1 : -1;
        }
    } else if (g->open && g->drop_t >= 0 && t - g->drop_t >= c->drop_s) { /* b37 live3: the palm down: the close */
        g->open = 0, g->why = SOMVR_VMW_DROPPED, g->armed = 0, ev = -1;
    }
    if (ev > 0) g->away_t = g->lost_t = g->use_t = g->drop_t = -1, g->in_use = 0, g->use_held = 0;
    if (g->open && !ev) {
        if (gaze && look <= c->look_off && !in->busy && !g->in_use) {
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
    for (int i = 0; i < 18 && bl[i] >= 0 && n < cap; i++) /* (build 36: 16; b37 live3: 18) */
        if (somvr_vm_rect_av(avail, bl[i], SOMVR_VMP_BUTTON, rc)) out[n++] = (somvr_vm_hit){bl[i], SOMVR_VMP_BUTTON};
    if (page == SOMVR_VMPG_CONTROLS || page == SOMVR_VMPG_TEST) return n;
    int r0 = page == SOMVR_VMPG_PRACTICE ? SOMVR_VMI_PR_ORC : 0, r1 = page == SOMVR_VMPG_PRACTICE ? r0 + 1 : SOMVR_VMI_ROWS;
    for (int item = r0; item < r1; item++) {
        if (page != SOMVR_VMPG_PRACTICE && somvr_vm_row_page(item) != page) /* build 34: the group's own rows */
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
