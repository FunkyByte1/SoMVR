/* somvr_views.h - the XR views provider (game thread): xrLocateViews for the time the layer published (its predicted
 * display time + the pipeline lead, used verbatim: the layer recognises its own values to measure that lead), through
 * the session/space/function the layer published in somvr_shared (docs/re-xr-pipeline.md sections 4.3, 4.4, 4.7, 7.1).
 * Tested against a mock runtime by tests/test_views.c.
 */
#ifndef SOMVR_VIEWS_H
#define SOMVR_VIEWS_H

#include "somvr_pose.h"
#include "somvr_shared.h"

#define SOMVR_VIEWS_MAX_REUSE 30 /* failed locates in a row that may reuse the last good views */

typedef struct {
    int ok;            /* two usable views */
    int fresh;         /* located by this call (else reused) */
    int extrapolated;  /* no new xrWaitFrame since the last call: time = last + period */
    int pos_reused;    /* orientation fresh, positions from the last good views (position not valid) */
    int64_t time;      /* the XrTime asked for */
    uint64_t flags;    /* XrViewStateFlags */
    int result;        /* XrResult of the call (0 = not called) */
    somvr_posef pose[2];
    somvr_fovf fov[2];
} somvr_views;

typedef struct {
    uint64_t last_waits;
    int64_t last_time;
    int have_good;
    somvr_posef good_pose[2];
    somvr_fovf good_fov[2];
    int fails;                    /* consecutive */
    uint64_t calls, located, failures, reused, extrapolated;
} somvr_views_state;

/* 1 = out->ok. Never calls the runtime unless xr.state is RUNNING (somvr_xr_enter). */
int somvr_views_locate(somvr_shared *sh, somvr_views_state *st, somvr_views *out);

/* The XrTime the next somvr_views_locate would ask for, now (the same pick: the published time when the layer has
 * waited since, else the last time + one period; never earlier than the last), without locating or changing st; 0 = no
 * published time. SOMVR_ARMS locates the grips with it at phase 2, before H4 locates the eyes for the same time. */
int64_t somvr_views_peek_time(somvr_shared *sh, const somvr_views_state *st);

/* Synthetic views without a runtime (SOMVR_VIEWS=fake, the selftest, desktop checks): identity head at the tracking
 * origin, eyes at -/+ ipd/2 along X, a symmetric fov from the game's SomView (layout A: the half width). */
void somvr_views_fake(const somvr_view *game, int layout, float ipd_m, somvr_views *out);

#endif
