/* somvr_views.c - see somvr_views.h */
#include "somvr_views.h"

#include <math.h>
#include <openxr/openxr.h>
#include <string.h>

_Static_assert(sizeof(somvr_posef) == sizeof(XrPosef) && offsetof(XrPosef, position) == 16, "somvr_posef = XrPosef");
_Static_assert(sizeof(somvr_fovf) == sizeof(XrFovf) && offsetof(XrFovf, angleUp) == 8, "somvr_fovf = XrFovf");

static int reuse_good(somvr_views_state *st, somvr_views *out) {
    st->fails++;
    st->failures++;
    if (!st->have_good || st->fails > SOMVR_VIEWS_MAX_REUSE) return out->ok = 0;
    memcpy(out->pose, st->good_pose, sizeof out->pose);
    memcpy(out->fov, st->good_fov, sizeof out->fov);
    st->reused++;
    return out->ok = 1;
}

int somvr_views_locate(somvr_shared *sh, somvr_views_state *st, somvr_views *out) {
    memset(out, 0, sizeof *out);
    st->calls++;
    if (!sh || !somvr_xr_enter(sh)) return 0; /* no XR: the caller renders flat */
    somvr_time tm;
    if (!somvr_time_read(sh, &tm) || tm.t <= 0 || !sh->xr.locate_views) {
        somvr_xr_leave(sh);
        return reuse_good(st, out);
    }
    int64_t t = tm.t;
    if (tm.waits == st->last_waits && st->last_time) { /* the layer has not waited since: predict one period on */
        t = st->last_time + (tm.period > 0 ? tm.period : 11111111);
        out->extrapolated = 1;
        st->extrapolated++;
    }
    if (t < st->last_time) t = st->last_time;
    st->last_waits = tm.waits;
    st->last_time = t;
    out->time = t;

    XrViewLocateInfo li = {.type = XR_TYPE_VIEW_LOCATE_INFO,
                           .viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                           .displayTime = t,
                           .space = (XrSpace)(uintptr_t)sh->xr.space};
    XrViewState vs = {.type = XR_TYPE_VIEW_STATE};
    XrView v[2] = {{.type = XR_TYPE_VIEW}, {.type = XR_TYPE_VIEW}};
    uint32_t n = 0;
    XrResult r = ((PFN_xrLocateViews)sh->xr.locate_views)((XrSession)(uintptr_t)sh->xr.session, &li, &vs, 2, &n, v);
    somvr_xr_leave(sh);
    atomic_fetch_add_explicit(&sh->n_locates, 1, memory_order_relaxed);
    out->result = (int)r;
    out->flags = vs.viewStateFlags;
    if (XR_FAILED(r) || n != 2 || !(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
        atomic_fetch_add_explicit(&sh->n_locate_fail, 1, memory_order_relaxed);
        return reuse_good(st, out);
    }
    for (int e = 0; e < 2; e++) {
        memcpy(&out->pose[e], &v[e].pose, sizeof out->pose[e]);
        memcpy(&out->fov[e], &v[e].fov, sizeof out->fov[e]);
    }
    if (!(vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) && st->have_good) {
        for (int e = 0; e < 2; e++) memcpy(out->pose[e].p, st->good_pose[e].p, sizeof out->pose[e].p);
        out->pos_reused = 1;
    }
    memcpy(st->good_pose, out->pose, sizeof st->good_pose);
    memcpy(st->good_fov, out->fov, sizeof st->good_fov);
    st->have_good = 1;
    st->fails = 0;
    st->located++;
    out->fresh = 1;
    return out->ok = 1;
}

int64_t somvr_views_peek_time(somvr_shared *sh, const somvr_views_state *st) {
    somvr_time tm;
    if (!sh || !st || !somvr_time_read(sh, &tm) || tm.t <= 0) return 0;
    int64_t t = tm.t;
    if (tm.waits == st->last_waits && st->last_time) t = st->last_time + (tm.period > 0 ? tm.period : 11111111);
    return t < st->last_time ? st->last_time : t;
}

void somvr_views_fake(const somvr_view *game, int layout, float ipd_m, somvr_views *out) {
    memset(out, 0, sizeof *out);
    float tx = tanf(0.5f * game->fov_x), ty = tanf(0.5f * game->fov_y);
    if (layout == SOMVR_LAYOUT_A) tx *= 0.5f; /* each eye gets half the width at the same height */
    for (int e = 0; e < 2; e++) {
        out->pose[e] = (somvr_posef){{0, 0, 0, 1}, {e ? 0.5f * ipd_m : -0.5f * ipd_m, 0, 0}};
        out->fov[e] = (somvr_fovf){-atanf(tx), atanf(tx), atanf(ty), -atanf(ty)};
    }
    out->ok = out->fresh = 1;
}
