/* somvr_logic.c - see somvr_logic.h */
#include "somvr_logic.h"

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

void somvr_eye_rect(uint32_t w, uint32_t h, int eye, int32_t rect[4]) {
    rect[0] = eye ? (int32_t)w : 0;
    rect[1] = 0;
    rect[2] = (int32_t)w;
    rect[3] = (int32_t)h;
}

void somvr_quad_size(uint32_t w, uint32_t h, float width_m, float out[2]) {
    out[0] = width_m;
    out[1] = w ? width_m * (float)h / (float)w : width_m;
}
