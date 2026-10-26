/* somvr_logic.c - see somvr_logic.h */
#include "somvr_logic.h"

#include <math.h>
#include <openxr/openxr.h>
#include <string.h>

int somvr_extl_init(somvr_extlist *l, const char *const *names, uint32_t n) {
    l->count = 0;
    l->used = 0;
    if (n > SOMVR_EXTL_MAX) return -1;
    for (uint32_t i = 0; i < n; i++)
        if (names[i]) l->names[l->count++] = names[i];
    return 0;
}

int somvr_extl_has(const somvr_extlist *l, const char *name) {
    for (uint32_t i = 0; i < l->count; i++)
        if (strcmp(l->names[i], name) == 0) return 1;
    return 0;
}

int somvr_extl_add_spaced(somvr_extlist *l, const char *spaced) {
    int added = 0;
    const char *p = spaced ? spaced : "";
    for (;;) {
        while (*p == ' ') p++;
        if (!*p) break;
        size_t len = strcspn(p, " ");
        if (l->used + len + 1 > sizeof l->buf) return -1;
        char *dst = l->buf + l->used;
        memcpy(dst, p, len);
        dst[len] = 0;
        p += len;
        if (somvr_extl_has(l, dst)) continue; /* a duplicate: the copy is overwritten by the next name */
        if (l->count >= SOMVR_EXTL_MAX) return -1;
        l->used += len + 1;
        l->names[l->count++] = dst;
        added++;
    }
    return added;
}

const char *somvr_first_unsupported(const char *spaced, const VkExtensionProperties *props, uint32_t n, char *out,
                                    size_t outsz) {
    const char *p = spaced ? spaced : "";
    for (;;) {
        while (*p == ' ') p++;
        if (!*p) return NULL;
        size_t len = strcspn(p, " ");
        int found = 0;
        for (uint32_t i = 0; i < n && !found; i++)
            found = strlen(props[i].extensionName) == len && memcmp(props[i].extensionName, p, len) == 0;
        if (!found) {
            size_t c = len < outsz - 1 ? len : outsz - 1;
            memcpy(out, p, c);
            out[c] = 0;
            return out;
        }
        p += len;
    }
}

typedef struct { int ok, bgra, srgb; } fmtinfo;
static fmtinfo fmt_info(int64_t f) {
    fmtinfo r = {0, 0, 0};
    switch (f) {
    case VK_FORMAT_B8G8R8A8_UNORM: r = (fmtinfo){1, 1, 0}; break;
    case VK_FORMAT_B8G8R8A8_SRGB: r = (fmtinfo){1, 1, 1}; break;
    case VK_FORMAT_R8G8B8A8_UNORM: r = (fmtinfo){1, 0, 0}; break;
    case VK_FORMAT_R8G8B8A8_SRGB: r = (fmtinfo){1, 0, 1}; break;
    default: break;
    }
    return r;
}

somvr_xfer somvr_pick_format(VkFormat game, const int64_t *offered, uint32_t n, VkFormat *out, int *gamma_ok) {
    fmtinfo g = fmt_info(game);
    int best = -1;
    somvr_xfer xfer = SOMVR_XFER_NONE;
    if (!g.ok) return SOMVR_XFER_NONE;
    for (uint32_t i = 0; i < n; i++) {
        fmtinfo o = fmt_info(offered[i]);
        if (!o.ok) continue;
        int copy = o.bgra == g.bgra;
        /* copy keeps the bytes: right only into an _SRGB image. A blit converts: right only from an _SRGB source
         * (decoded to linear, then stored linear or re-encoded, both read back correctly by the runtime). */
        int gamma = copy ? o.srgb : g.srgb;
        int score = gamma * 2 + copy;
        if (score > best) { /* ties: the runtime's order (its preferred format first) */
            best = score;
            *out = (VkFormat)offered[i];
            *gamma_ok = gamma;
            xfer = copy ? SOMVR_XFER_COPY : SOMVR_XFER_BLIT;
        }
    }
    return xfer;
}

somvr_session_action somvr_on_session_state(int state, int running) {
    switch (state) {
    case XR_SESSION_STATE_READY: return running ? SOMVR_SA_NONE : SOMVR_SA_BEGIN;
    case XR_SESSION_STATE_STOPPING: return running ? SOMVR_SA_END : SOMVR_SA_NONE;
    case XR_SESSION_STATE_LOSS_PENDING:
    case XR_SESSION_STATE_EXITING: return SOMVR_SA_LOST;
    default: return SOMVR_SA_NONE;
    }
}

int somvr_parse_mode(const char *v) {
    if (!v || !*v || !strcmp(v, "1") || !strcmp(v, "auto")) return SOMVR_MODE_AUTO;
    if (!strcmp(v, "0") || !strcmp(v, "off")) return SOMVR_MODE_OFF;
    if (!strcmp(v, "force")) return SOMVR_MODE_FORCE;
    return SOMVR_MODE_OFF; /* unknown value: the safe choice */
}

int somvr_parse_layer(const char *v) { return v && !strcmp(v, "proj") ? SOMVR_LAYER_PROJ : SOMVR_LAYER_QUAD; }

int somvr_parse_space(const char *v) { return v && !strcmp(v, "stage") ? 1 : 0; }

int somvr_parse_dump(const char *v, uint64_t at[SOMVR_DUMP_MAX]) {
    int n = 0;
    if (!v || !*v) return 0;
    for (const char *p = v;; p++) {
        uint64_t mono = *p == 'm' ? SOMVR_DUMP_MONO : *p == 's' ? SOMVR_DUMP_SC : 0;
        if (mono) p++;
        const char *d = p;
        uint64_t x = 0;
        for (; *p >= '0' && *p <= '9'; p++) x = x * 10 + (uint64_t)(*p - '0');
        if (p == d || p - d > 12 || x == 0 || n == SOMVR_DUMP_MAX) return -1;
        at[n++] = x | mono;
        if (!*p) return n;
        if (*p != ',') return -1;
    }
}

int somvr_dump_order(VkFormat f, int *srgb) {
    fmtinfo i = fmt_info(f);
    *srgb = i.srgb;
    return i.ok ? i.bgra : -1;
}

void somvr_dump_row(const uint8_t *src, uint8_t *dst, uint32_t n, int bgra) {
    for (uint32_t i = 0; i < n; i++, src += 4, dst += 3) {
        dst[0] = src[bgra ? 2 : 0];
        dst[1] = src[1];
        dst[2] = src[bgra ? 0 : 2];
    }
}

int somvr_pick_host_memory(const VkPhysicalDeviceMemoryProperties *mp, uint32_t bits) {
    const VkMemoryPropertyFlags need = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    int best = -1, best_score = -1;
    for (uint32_t i = 0; i < mp->memoryTypeCount && i < VK_MAX_MEMORY_TYPES; i++) {
        VkMemoryPropertyFlags f = mp->memoryTypes[i].propertyFlags;
        if (!(bits >> i & 1u) || (f & need) != need) continue;
        int score = (f & VK_MEMORY_PROPERTY_HOST_CACHED_BIT ? 2 : 0) + !(f & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (score > best_score) best = (int)i, best_score = score;
    }
    return best;
}

void somvr_eye_rect(uint32_t w, uint32_t h, int slot, int32_t rect[4]) {
    rect[0] = slot > 0 ? slot * (int32_t)w : 0;
    rect[1] = 0;
    rect[2] = (int32_t)w;
    rect[3] = (int32_t)h;
}

static int32_t px(float f, uint32_t size) {
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    return (int32_t)(f * (float)size + 0.5f);
}

void somvr_eye_pixel_rect(uint32_t w, uint32_t h, uint32_t slot, const float rect[4], int32_t out[4]) {
    int32_t x0 = px(rect[0], w), y0 = px(rect[1], h), x1 = px(rect[2], w), y1 = px(rect[3], h);
    out[0] = (int32_t)(slot * w) + x0;
    out[1] = y0;
    out[2] = x1 > x0 ? x1 - x0 : 0;
    out[3] = y1 > y0 ? y1 - y0 : 0;
}

uint32_t somvr_guard_cols(uint32_t sc_w, uint32_t w, uint32_t slot) {
    uint64_t end = (uint64_t)(slot + 1u) * w;
    if (!w || end >= sc_w) return 0;
    return sc_w - end < SOMVR_GUARD_COLS ? (uint32_t)(sc_w - end) : SOMVR_GUARD_COLS;
}

/* ---- prediction lead ------------------------------------------------------------------------------------------- */
int somvr_parse_predict(const char *v, int64_t *fixed_ns, int *bad) {
    *fixed_ns = 0;
    *bad = 0;
    if (!v || !*v || !strcmp(v, "auto")) return SOMVR_PREDICT_AUTO;
    if (!strcmp(v, "off") || !strcmp(v, "0")) return SOMVR_PREDICT_OFF;
    int64_t ns = 0, unit = 1000000; /* digits[.digits] ms, by hand: the game's locale may not use '.' */
    const char *p = v;
    for (; *p >= '0' && *p <= '9' && ns <= SOMVR_LEAD_MAX_NS; p++) ns = ns * 10 + (*p - '0') * unit;
    if (p == v || (*p && *p != '.')) return *bad = 1, SOMVR_PREDICT_AUTO;
    if (*p == '.' && !(p[1] >= '0' && p[1] <= '9')) return *bad = 1, SOMVR_PREDICT_AUTO;
    if (*p == '.')
        for (p++; *p >= '0' && *p <= '9'; p++) ns += (unit /= 10) * (*p - '0');
    if (*p || !(ns > 0 && ns <= SOMVR_LEAD_MAX_NS)) return *bad = 1, SOMVR_PREDICT_AUTO;
    *fixed_ns = ns;
    return SOMVR_PREDICT_FIXED;
}

void somvr_lead_init(somvr_lead *l) { memset(l, 0, sizeof *l); }

int64_t somvr_lead_publish(somvr_lead *l, int mode, int64_t fixed_ns, int64_t t_wait) {
    int64_t add = mode == SOMVR_PREDICT_AUTO ? l->lead : mode == SOMVR_PREDICT_FIXED ? fixed_ns : 0;
    int64_t pub = t_wait + add;
    l->ring[l->npub % SOMVR_LEAD_RING].pub = pub;
    l->ring[l->npub % SOMVR_LEAD_RING].wait = t_wait;
    l->npub++;
    return pub;
}

int somvr_lead_measure(somvr_lead *l, int64_t display_time, int64_t t_frame, int64_t *raw) {
    *raw = 0;
    uint32_t n = l->npub < SOMVR_LEAD_RING ? l->npub : SOMVR_LEAD_RING;
    for (uint32_t k = 1; display_time > 0 && k <= n; k++) { /* newest first */
        uint32_t i = (l->npub - k) % SOMVR_LEAD_RING;
        if (l->ring[i].pub != display_time) continue;
        *raw = t_frame - l->ring[i].wait;
        if (*raw < 0 || *raw > SOMVR_LEAD_MAX_NS) return l->rejected++, -1;
        l->win[l->nwin++ % SOMVR_LEAD_WIN] = *raw;
        uint32_t m = l->nwin < SOMVR_LEAD_WIN ? l->nwin : SOMVR_LEAD_WIN;
        int64_t s[SOMVR_LEAD_WIN];
        memcpy(s, l->win, sizeof s);
        for (uint32_t a = 1; a < m; a++) /* insertion sort of at most 9 */
            for (uint32_t j = a; j > 0 && s[j - 1] > s[j]; j--) {
                int64_t t = s[j];
                s[j] = s[j - 1], s[j - 1] = t;
            }
        l->lead = s[m / 2];
        l->matched++;
        return 1;
    }
    l->missed++;
    return 0;
}

somvr_submit_kind somvr_submit_decide(int have_tag, uint64_t tag_frame, int have_rec, int last_stereo) {
    if (!have_tag || !tag_frame) return SOMVR_SUBMIT_MONO;  /* no preload, untagged, or a flat frame */
    if (have_rec) return SOMVR_SUBMIT_STEREO;
    return last_stereo ? SOMVR_SUBMIT_REUSE : SOMVR_SUBMIT_MONO; /* frame id mismatch: the last stereo frame again */
}

somvr_fovf somvr_submit_fov(const somvr_eye_rec *e, uint32_t applied, int eye) {
    return (applied >> eye & 1u) ? e->fov : e->fov_cover;
}

void somvr_quad_size(uint32_t w, uint32_t h, float width_m, float out[2]) {
    out[0] = width_m;
    out[1] = w ? width_m * (float)h / (float)w : width_m;
}

/* ---- the HUD quad ------------------------------------------------------------------------------------------------ */
int somvr_parse_hud(const char *v, int *bad) {
    *bad = 0;
    if (!v || !*v || !strcmp(v, "world")) return SOMVR_HUD_WORLD;
    if (!strcmp(v, "head")) return SOMVR_HUD_HEAD;
    if (!strcmp(v, "off") || !strcmp(v, "0")) return SOMVR_HUD_OFF;
    return *bad = 1, SOMVR_HUD_WORLD;
}

int somvr_parse_hud_alpha(const char *v, int *bad) {
    *bad = 0;
    if (!v || !*v || !strcmp(v, "straight")) return SOMVR_HUD_STRAIGHT;
    if (!strcmp(v, "premul")) return SOMVR_HUD_PREMUL;
    return *bad = 1, SOMVR_HUD_STRAIGHT;
}

/* [-]digits[.digits] by hand (the game's locale may not use '.'); NULL = not a number */
static const char *parse_metres(const char *p, float *out) {
    int neg = *p == '-';
    p += neg;
    const char *d = p;
    double x = 0, unit = 1;
    for (; *p >= '0' && *p <= '9' && p - d < 6; p++) x = x * 10 + (*p - '0');
    if (p == d || (*p >= '0' && *p <= '9')) return NULL;
    if (*p == '.') {
        if (!(p[1] >= '0' && p[1] <= '9')) return NULL;
        for (p++; *p >= '0' && *p <= '9'; p++) x += (unit /= 10) * (*p - '0');
    }
    *out = (float)(neg ? -x : x);
    return p;
}

void somvr_parse_hud_quad(const char *v, float out[3], int *bad) {
    static const float lo[3] = {0.3f, 0.1f, -2.0f}, hi[3] = {10.0f, 10.0f, 2.0f};
    float q[3];
    *bad = 0;
    out[0] = SOMVR_HUD_DIST_M, out[1] = SOMVR_HUD_WIDTH_M, out[2] = SOMVR_HUD_DROP_M;
    if (!v || !*v) return;
    const char *p = v;
    for (int i = 0; i < 3; i++) {
        if (!(p = parse_metres(p, &q[i])) || !(q[i] >= lo[i] && q[i] <= hi[i]) || *p != (i < 2 ? ',' : '\0')) {
            *bad = 1;
            return;
        }
        p += i < 2;
    }
    memcpy(out, q, sizeof q);
}

/* v rotated by the unit quaternion (x y z w) */
static void qrot(const float q[4], const float v[3], float out[3]) {
    float t[3] = {2 * (q[1] * v[2] - q[2] * v[1]), 2 * (q[2] * v[0] - q[0] * v[2]), 2 * (q[0] * v[1] - q[1] * v[0])};
    out[0] = v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]);
    out[1] = v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]);
    out[2] = v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]);
}

void somvr_hud_world_pose(const somvr_posef eye[2], float dist, float drop, somvr_posef *out) {
    static const float fwd0[3] = {0, 0, -1}, up0[3] = {0, 1, 0};
    float f[3], u[3];
    qrot(eye[0].q, fwd0, f);
    float hx = f[0], hz = f[2];
    if (hx * hx + hz * hz < 1e-6f) { /* straight up or down: the head's up vector points ahead (down) or back (up) */
        qrot(eye[0].q, up0, u);
        hx = f[1] < 0 ? u[0] : -u[0], hz = f[1] < 0 ? u[2] : -u[2];
    }
    float n = sqrtf(hx * hx + hz * hz);
    if (!(n > 1e-6f)) hx = 0, hz = -1, n = 1; /* no direction at all (NaN): straight ahead in the space */
    hx /= n, hz /= n;
    float yaw = atan2f(-hx, -hz); /* the rotation about +Y that turns -Z into (hx, 0, hz) */
    *out = (somvr_posef){{0, sinf(0.5f * yaw), 0, cosf(0.5f * yaw)},
                         {0.5f * (eye[0].p[0] + eye[1].p[0]) + dist * hx, 0.5f * (eye[0].p[1] + eye[1].p[1]) - drop,
                          0.5f * (eye[0].p[2] + eye[1].p[2]) + dist * hz}};
}

/* ---- OpenXR input ---------------------------------------------------------------------------------------------- */
int somvr_parse_input(const char *v) {
    if (!v || !*v || !strcmp(v, "1") || !strcmp(v, "on")) return SOMVR_INPUT_ON;
    if (!strcmp(v, "log")) return SOMVR_INPUT_LOG;
    return SOMVR_INPUT_OFF; /* "0", "off" and anything unknown */
}

#define LR 3u
const somvr_action_def somvr_actions[SOMVR_NACT] = {
    [SOMVR_ACT_GRIP] = {"grip_pose", "Hand grip pose", XR_ACTION_TYPE_POSE_INPUT, LR, 0},
    [SOMVR_ACT_AIM] = {"aim_pose", "Hand aim pose", XR_ACTION_TYPE_POSE_INPUT, LR, 0},
    [SOMVR_ACT_TRIGGER] = {"trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT, LR, 0},
    [SOMVR_ACT_SQUEEZE] = {"squeeze", "Grip squeeze", XR_ACTION_TYPE_FLOAT_INPUT, LR, 0},
    [SOMVR_ACT_STICK] = {"stick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT, LR, 0},
    [SOMVR_ACT_STICK_CLICK] = {"stick_click", "Thumbstick click", XR_ACTION_TYPE_BOOLEAN_INPUT, LR, SOMVR_BTN_STICK},
    [SOMVR_ACT_LOWER] = {"lower", "Lower face button (A/X)", XR_ACTION_TYPE_BOOLEAN_INPUT, LR, SOMVR_BTN_LOWER},
    [SOMVR_ACT_UPPER] = {"upper", "Upper face button (B/Y)", XR_ACTION_TYPE_BOOLEAN_INPUT, LR, SOMVR_BTN_UPPER},
    [SOMVR_ACT_MENU] = {"menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT, 1u << SOMVR_HAND_L, SOMVR_BTN_MENU},
    [SOMVR_ACT_TRIGGER_TOUCH] = {"trigger_touch", "Finger on the trigger", XR_ACTION_TYPE_BOOLEAN_INPUT, LR,
                                 SOMVR_BTN_TRIGGER_TOUCH},
    [SOMVR_ACT_THUMB_TOUCH] = {"thumb_touch", "Thumb resting", XR_ACTION_TYPE_BOOLEAN_INPUT, LR, SOMVR_BTN_THUMB_TOUCH},
    [SOMVR_ACT_HAPTIC] = {"haptic", "Vibration", XR_ACTION_TYPE_VIBRATION_OUTPUT, LR, 0},
};

#define L SOMVR_HAND_L
#define R SOMVR_HAND_R
#define BOTH(a, p) {SOMVR_ACT_##a, L, p}, {SOMVR_ACT_##a, R, p}
static const somvr_binding_def b_touch[] = {
    BOTH(GRIP, "input/grip/pose"), BOTH(AIM, "input/aim/pose"), BOTH(TRIGGER, "input/trigger/value"),
    BOTH(SQUEEZE, "input/squeeze/value"), BOTH(STICK, "input/thumbstick"), BOTH(STICK_CLICK, "input/thumbstick/click"),
    {SOMVR_ACT_LOWER, L, "input/x/click"}, {SOMVR_ACT_LOWER, R, "input/a/click"},
    {SOMVR_ACT_UPPER, L, "input/y/click"}, {SOMVR_ACT_UPPER, R, "input/b/click"},
    {SOMVR_ACT_MENU, L, "input/menu/click"}, BOTH(TRIGGER_TOUCH, "input/trigger/touch"),
    {SOMVR_ACT_THUMB_TOUCH, L, "input/x/touch"}, {SOMVR_ACT_THUMB_TOUCH, L, "input/y/touch"},
    {SOMVR_ACT_THUMB_TOUCH, L, "input/thumbstick/touch"}, {SOMVR_ACT_THUMB_TOUCH, L, "input/thumbrest/touch"},
    {SOMVR_ACT_THUMB_TOUCH, R, "input/a/touch"}, {SOMVR_ACT_THUMB_TOUCH, R, "input/b/touch"},
    {SOMVR_ACT_THUMB_TOUCH, R, "input/thumbstick/touch"}, {SOMVR_ACT_THUMB_TOUCH, R, "input/thumbrest/touch"},
    BOTH(HAPTIC, "output/haptic"),
};
static const somvr_binding_def b_index[] = {
    BOTH(GRIP, "input/grip/pose"), BOTH(AIM, "input/aim/pose"), BOTH(TRIGGER, "input/trigger/value"),
    BOTH(SQUEEZE, "input/squeeze/value"), BOTH(STICK, "input/thumbstick"), BOTH(STICK_CLICK, "input/thumbstick/click"),
    BOTH(LOWER, "input/a/click"), BOTH(UPPER, "input/b/click"), {SOMVR_ACT_MENU, L, "input/system/click"},
    BOTH(TRIGGER_TOUCH, "input/trigger/touch"), BOTH(THUMB_TOUCH, "input/a/touch"), BOTH(THUMB_TOUCH, "input/b/touch"),
    BOTH(THUMB_TOUCH, "input/thumbstick/touch"), BOTH(THUMB_TOUCH, "input/trackpad/touch"),
    BOTH(HAPTIC, "output/haptic"),
};
static const somvr_binding_def b_simple[] = {
    BOTH(GRIP, "input/grip/pose"), BOTH(AIM, "input/aim/pose"), BOTH(TRIGGER, "input/select/click"),
    {SOMVR_ACT_UPPER, R, "input/menu/click"}, {SOMVR_ACT_MENU, L, "input/menu/click"}, BOTH(HAPTIC, "output/haptic"),
};
#undef BOTH
#undef L
#undef R
#define NB(a) (uint32_t)(sizeof a / sizeof a[0])
const somvr_profile_def somvr_profiles[SOMVR_NPROF] = {
    {SOMVR_PROF_TOUCH, "touch", "/interaction_profiles/oculus/touch_controller", b_touch, NB(b_touch)},
    {SOMVR_PROF_INDEX, "index", "/interaction_profiles/valve/index_controller", b_index, NB(b_index)},
    {SOMVR_PROF_SIMPLE, "simple", "/interaction_profiles/khr/simple_controller", b_simple, NB(b_simple)},
};
#undef NB
#undef LR

int somvr_prof_from_path(const char *path) {
    if (!path || !*path) return SOMVR_PROF_NONE;
    for (int i = 0; i < SOMVR_NPROF; i++)
        if (!strcmp(path, somvr_profiles[i].path)) return somvr_profiles[i].prof;
    return SOMVR_PROF_OTHER;
}

uint32_t somvr_in_merge(uint8_t presses[SOMVR_NBTN], uint32_t prev, uint32_t now) {
    uint32_t rise = now & ~prev;
    for (int b = 0; b < SOMVR_NBTN; b++)
        if (rise >> b & 1u) presses[b] = (uint8_t)(presses[b] + 1u);
    return now;
}

somvr_hap_decision somvr_hap_decide(const somvr_haptic_req *req, float *amp, float *freq, int64_t *dur_ns) {
    float a = req->amplitude, f = req->frequency;
    if (!(a > 0.0f)) return *amp = 0, *freq = 0, *dur_ns = 0, SOMVR_HAPD_STOP; /* 0, negative, NaN */
    *amp = a > 1.0f ? 1.0f : a;
    *freq = f > 0.0f && f < 1e6f ? f : (float)XR_FREQUENCY_UNSPECIFIED;
    *dur_ns = req->duration_ns > 0 ? req->duration_ns : (int64_t)XR_MIN_HAPTIC_DURATION;
    return SOMVR_HAPD_APPLY;
}
