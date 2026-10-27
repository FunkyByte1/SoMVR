/* somvr_void_layer.c - never a void (HG-9; docs/re-xr-pipeline.md 36, handoff/HANDOFF-void.md). Included by
 * somvr_vklayer.c after the VR hands' file (it uses X, EV, I, cfg and the frame helpers). X.mx held unless noted.
 *   The gaps (log only; SOMVR_XR_GAPS=off|MS, default 100): a game present on the session's device that comes MS or
 *     more after the one before is logged when it is done - how long, the kinds before and after, what was shown, the
 *     XR frames ended meanwhile (the keep-alive's), the longest time between two xrEndFrame calls, the head sampled
 *     every 100 ms while the game was away - and the session's buckets and its longest gaps (every 10th gap, each gap of
 *     a second or more, the session's end).
 *   The watcher: one thread per session (started with it, joined at its teardown). It never blocks on X.mx (trylock),
 *     so the teardown joins it with X.mx held; a game present raises VD.presenting first, and the watcher leaves X.mx
 *     to it.
 *   The process's exit (build 16, SOMVR_XR_EXIT=on|off, default on; docs/re-xr-pipeline.md 53): the code at the end.
 */

enum { VD_NB = 5, VD_TOP = 3 };
static const int vd_bucket_ms[VD_NB] = {100, 250, 500, 1000, 2000};
#define VD_SAMPLE_NS 100000000LL /* the head is sampled every 100 ms while the game is away */
static struct {
    int gaps_ms, gaps_bad;      /* SOMVR_XR_GAPS: log gaps this long (ms; 0 = off), not understood */
    pthread_t th;
    int th_up;
    _Atomic int stop;
    _Atomic int th_done;        /* the watcher has left its loop (xr_exit's wait when it cannot join) */
    _Atomic int64_t present_ns; /* the game's last present on the session's device (its entry), 0 = none yet */
    _Atomic int presenting;     /* a game present is between its entry and its return */
    int64_t wait_ns;            /* the last xrWaitFrame's return */
    int64_t end_ns;             /* the last xrEndFrame's return */
    int64_t sess_ns;            /* the session's start */
    char kind;                  /* the last present's kind: S stereo, M mono, R reuse, H a hold frame, - nothing */
    uint64_t c_stereo, c_mono, c_reuse, c_hold; /* the counters before this present */
    /* this gap (the watcher fills it while the game is away) */
    int64_t gap_end_max;        /* the longest time between two xrEndFrame calls since the last present */
    uint32_t gap_ka, gap_samples, gap_located;
    float gap_yaw[2], gap_pitch[2], gap_p0[3], gap_move;
    int64_t sample_ns;
    /* the session */
    uint64_t n_present, n_gaps, n_bucket[VD_NB], n_ka;
    int64_t top_ns[VD_TOP];
    double top_at[VD_TOP];
    char top_k[VD_TOP][3];
    int64_t end_gap_max;
} VD;

#include "somvr_void.h"

static struct {
    int ka, ka_bad, ka_ms, ka_ms_bad;           /* SOMVR_XR_KEEPALIVE, _MS */
    int space, space_bad, space_ms, space_ms_bad; /* SOMVR_XR_LOADSPACE, _MS */
    int theatre, theatre_bad;                   /* SOMVR_XR_THEATRE */
    int size_pct, size_bad;                     /* SOMVR_XR_THEATRE_SIZE: the screens' width, % of their distance (build 14) */
    int reseat, reseat_bad;                     /* SOMVR_XR_THEATRE_RESEAT: each new start placed again (build 14) */
    uint64_t th_eyes;                           /* X.n_eyes at the theatre's last mono frame */
    uint64_t th_reseats;
    int video, video_bad;                       /* SOMVR_XR_THEATRE_VIDEO: a video's eyeless layout-B presents = mono */
    uint64_t video_n;
    uint32_t th_kind;                           /* the screen now: SOMVR_SSM_VIDEO / _WINDOW / _LOAD, 0 = the board */
    int loading;                                /* the last mono frame was a load's (the lights on through it) */
    somvr_posef th_pose;
    uint64_t th_n, th_frames;
    _Atomic(VkQueue) q;                         /* the session's queue while the keep-alive runs (the hooks lock it) */
    _Atomic int test_stall_ms;                  /* SOMVR_XR_TEST `stall MS`: the next present sleeps after it */
    /* this stall */
    int64_t stall_ns;                           /* the game's last present before it (0 = no stall) */
    uint64_t stall_frames;
    int stall_space, stall_nothing;
    /* the space */
    int space_up;                               /* the environment shown without a board (the keep-alive put it up) */
    XrSwapchain sc;
    int filled, dead, stat;
    VkFormat fmt;
    XrCompositionLayerQuad floor_q, lights_q;
    XrCompositionLayerProjection proj;          /* the keep-alive's world hold */
    uint64_t placed_stereo;                     /* X.n_stereo when the space was placed (+1: placed) */
    float floor_oy;                             /* LOCAL's origin above the real floor (SOMVR_XR_FLOOR), 0 = unknown */
    float level;                                /* the lights' level */
    XrTime level_t;
    uint64_t n_stalls, n_space, n_lights;
    uint32_t lines;
} KA;

/* the game's in-world loading cover (SOMVR_XR_COVER=on; ./somvr_void.h; the code at the file's end) */
#define CV_NE 16 /* readings in flight at once (one a stereo frame's submit) */
static struct {
    int on, bad, ms, ms_bad;
    int dead;                   /* no readback this session (logged once) */
    somvr_cover_state st;
    dev_st *ds;                 /* the buffer's device */
    VkBuffer buf;
    VkDeviceMemory mem;
    uint8_t *map;               /* CV_NE x SOMVR_COVER_N texels, HOST_COHERENT */
    struct {
        const slot_st *sl;      /* the game slot whose submit copies it (its fence), NULL = free */
        uint64_t seq;
        double t;               /* the submit's time (s) */
        int menu;               /* a game menu's HUD (the cover only when nearly all pure black) */
    } e[CV_NE];
    uint64_t n_sub, seq_read, n_read, n_late, n_menu, n_space_frames, n_hidden_frames;
    uint32_t k;                 /* this load's space frames (its 20th and 100th log the space: tests/void_view.c) */
    int placed;                 /* this load's space is placed (kept while the game's stereo frames go on) */
    int await;                  /* after a mono stretch: the space until a reading (cv_poll) */
    double await_t;             /* the first stereo present of that wait drawn as the space (s; 0: none yet) */
    uint64_t await_n;           /* n_read at the mono present */
    double t_hide, t_space, s_hidden, s_space; /* when each began; their totals (s) */
    uint32_t lines;
} CV;
/* the in-world menus' backdrop (SOMVR_XR_MENU_BACK=on; ./somvr_void.h; the code at the file's end) */
static struct {
    int on, bad, dim_bad;
    float dim;
    XrSwapchain sc;
    int filled, dead, stat;
    float w_m, h_m;             /* the panel the image was filled for */
    float level;
    XrTime level_t;
    somvr_posef pose;           /* the panel: behind the last menu board */
    float size[2];
    XrCompositionLayerQuad q, dq; /* the panel, the world's dim (the fade's ramp at the head) */
    uint32_t has;               /* bit 0 the panel, bit 1 the dim: the last stereo frame's (reuse and holds repeat them) */
    int was_menu;
    uint64_t n_frames, n_opens;
    uint32_t lines;
} MB;

static void ka_config(void); /* (the keep-alive's part, below) */
static void cv_config(void);
static void cv_session(void);
static void cv_summary(void);
static void mb_config(void);
static void mb_session(void);
static void mb_summary(void);
static void mb_destroy(void);
static int cv_line(void);
static void cv_await_end(const char *why, double now);
static int64_t ka_frame(int64_t now);
static void ka_resumed(int64_t t0);
static void vd_space_destroy(void);
static int ka_on(void);
static void xe_config(void); /* the process's exit (the code at the file's end) */
static void xe_session(void);

static int64_t vd_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void vd_config(void) { /* once_init */
    const char *p = getenv("SOMVR_XR_GAPS");
    VD.gaps_ms = 100;
    if (p && *p) {
        char *e = NULL;
        long v = strtol(p, &e, 10);
        if (!strcmp(p, "off") || !strcmp(p, "0")) VD.gaps_ms = 0;
        else if (e && !*e && v >= 20 && v <= 60000) VD.gaps_ms = (int)v;
        else VD.gaps_bad = 1;
    }
    ka_config();
    cv_config(); /* the game's in-world loading cover (SOMVR_XR_COVER) */
    mb_config(); /* the in-world menus' backdrop (SOMVR_XR_MENU_BACK) */
    xe_config(); /* build 16: the process's exit (SOMVR_XR_EXIT) */
}

/* the head now (the eyes' middle, its yaw and pitch in degrees): 1 = located */
static int vd_head(int64_t now, float *yaw, float *pitch, float p[3]) {
    if (!X.session || !X.space || !X.fs.predictedDisplayTime) return 0;
    XrViewLocateInfo li = {.type = XR_TYPE_VIEW_LOCATE_INFO,
                           .viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                           .displayTime = X.fs.predictedDisplayTime + (VD.wait_ns ? now - VD.wait_ns : 0),
                           .space = X.space};
    XrViewState vs = {.type = XR_TYPE_VIEW_STATE};
    XrView v[2] = {{.type = XR_TYPE_VIEW}, {.type = XR_TYPE_VIEW}};
    uint32_t nv = 0;
    XrResult r = X.xrLocateViews(X.session, &li, &vs, 2, &nv, v);
    if (XR_FAILED(r) || nv != 2 || !(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) return 0;
    const XrQuaternionf *q = &v[0].pose.orientation;
    float fx = -2.0f * (q->w * q->y + q->x * q->z), fy = 2.0f * (q->w * q->x - q->y * q->z),
          fz = -1.0f + 2.0f * (q->x * q->x + q->y * q->y); /* -z (ahead) turned by q */
    *yaw = atan2f(fx, -fz) * 57.29578f;
    *pitch = asinf(fy < -1.0f ? -1.0f : fy > 1.0f ? 1.0f : fy) * 57.29578f;
    for (int i = 0; i < 3; i++)
        p[i] = 0.5f * ((&v[0].pose.position.x)[i] + (&v[1].pose.position.x)[i]);
    return 1;
}

/* the watcher, X.mx held, the game away for a sample period: the head */
static void vd_sample(int64_t now) {
    if (now - VD.sample_ns < VD_SAMPLE_NS) return;
    VD.sample_ns = now;
    VD.gap_samples++;
    float yaw, pitch, p[3];
    if (!vd_head(now, &yaw, &pitch, p)) return;
    if (!VD.gap_located++) {
        VD.gap_yaw[0] = VD.gap_yaw[1] = yaw, VD.gap_pitch[0] = VD.gap_pitch[1] = pitch;
        memcpy(VD.gap_p0, p, sizeof VD.gap_p0);
        return;
    }
    if (yaw < VD.gap_yaw[0]) VD.gap_yaw[0] = yaw;
    if (yaw > VD.gap_yaw[1]) VD.gap_yaw[1] = yaw;
    if (pitch < VD.gap_pitch[0]) VD.gap_pitch[0] = pitch;
    if (pitch > VD.gap_pitch[1]) VD.gap_pitch[1] = pitch;
    float d = sqrtf((p[0] - VD.gap_p0[0]) * (p[0] - VD.gap_p0[0]) + (p[1] - VD.gap_p0[1]) * (p[1] - VD.gap_p0[1]) +
                    (p[2] - VD.gap_p0[2]) * (p[2] - VD.gap_p0[2]));
    if (d > VD.gap_move) VD.gap_move = d;
}

/* The watcher: every 20 ms (4 ms with the keep-alive) a look at the game's last present; away long enough: the head's
 * sample, the keep-alive's frame. A frame's own xrWaitFrame paces it; a runtime whose wait returned in under half a
 * period (the mock) gets the rest of the period slept here, outside X.mx. */
static void *vd_thread(void *arg) {
    (void)arg;
    long sleep_ns = 20000000;
    while (!atomic_load(&VD.stop)) {
        struct timespec sl = {0, sleep_ns};
        nanosleep(&sl, NULL);
        int ka = ka_on();
        sleep_ns = ka ? 4000000 : 20000000;
        int64_t now = vd_now(), p = atomic_load(&VD.present_ns), away = now - p;
        int64_t need = ka && KA.ka_ms * 1000000LL < VD_SAMPLE_NS ? KA.ka_ms * 1000000LL : VD_SAMPLE_NS;
        if (!p || away < need || atomic_load(&VD.presenting)) continue;
        if (pthread_mutex_trylock(&X.mx)) continue;
        int64_t took = -1, period = 0;
        if (!atomic_load(&VD.stop) && X.phase == P_SESSION && X.running && !atomic_load(&VD.presenting) &&
            atomic_load(&VD.present_ns) == p) {
            if (VD.gaps_ms) vd_sample(now);
            if (ka && away >= KA.ka_ms * 1000000LL) took = ka_frame(now), period = (int64_t)X.fs.predictedDisplayPeriod;
        }
        pthread_mutex_unlock(&X.mx);
        if (took >= 0) /* the next frame at once (the wait paced it), or the period's rest */
            sleep_ns = period > 0 && took < period / 2 ? (long)(period - took) : 500000;
    }
    atomic_store(&VD.th_done, 1);
    return NULL;
}

/* the end of xr_session_setup: a new session's gaps; the watcher */
static void vd_start(void) {
    int64_t now = vd_now();
    VD.sess_ns = now, VD.end_ns = 0, VD.wait_ns = 0, VD.kind = '-';
    atomic_store(&VD.present_ns, 0);
    VD.gap_end_max = 0, VD.gap_ka = VD.gap_samples = VD.gap_located = 0, VD.gap_move = 0, VD.sample_ns = 0;
    VD.n_present = VD.n_gaps = VD.n_ka = 0, VD.end_gap_max = 0;
    memset(VD.n_bucket, 0, sizeof VD.n_bucket);
    memset(VD.top_ns, 0, sizeof VD.top_ns);
    if (VD.gaps_bad) lg("xr: gaps: SOMVR_XR_GAPS=%s is not off or 20..60000 (ms): 100", getenv("SOMVR_XR_GAPS"));
    if (KA.ka_bad) lg("xr: keep-alive: SOMVR_XR_KEEPALIVE=%s is not on or off: off", getenv("SOMVR_XR_KEEPALIVE"));
    if (KA.ka_ms_bad) lg("xr: keep-alive: SOMVR_XR_KEEPALIVE_MS=%s is not 50..1000: %d", getenv("SOMVR_XR_KEEPALIVE_MS"),
                         KA.ka_ms);
    if (KA.space_bad) lg("xr: loadspace: SOMVR_XR_LOADSPACE=%s is not on or off: off", getenv("SOMVR_XR_LOADSPACE"));
    if (KA.space_ms_bad) lg("xr: loadspace: SOMVR_XR_LOADSPACE_MS=%s is not 0..10000: %d", getenv("SOMVR_XR_LOADSPACE_MS"),
                            KA.space_ms);
    if (KA.theatre_bad) lg("xr: theatre: SOMVR_XR_THEATRE=%s is not on or off: off", getenv("SOMVR_XR_THEATRE"));
    if (KA.theatre)
        lg("xr: theatre: on: a video's flat frames on a %.0f m screen %.0f m ahead, the cinematic window's on %.0f m at %.0f "
           "m, world-locked at eye height where the head faced when it began (the preload's somvr_session: %s)",
           (double)SOMVR_VOID_THEATRE_M, (double)SOMVR_VOID_THEATRE_M, (double)SOMVR_VOID_WINDOW_M,
           (double)SOMVR_VOID_WINDOW_M, SSN.obj ? "found" : "NOT FOUND (SOMVR_MARK or SOMVR_QUIT makes it): menus as before");
    if (KA.size_bad)
        lg("xr: theatre: SOMVR_XR_THEATRE_SIZE=%s is not 50..200 (%% of the distance): 100", getenv("SOMVR_XR_THEATRE_SIZE"));
    if (KA.theatre)
        lg("xr: theatre: size %d %% (SOMVR_XR_THEATRE_SIZE): a video's screen %.2f m wide %.0f m ahead (%.0f deg), the "
           "window's %.2f m at %.0f m (%.0f deg)%s", KA.size_pct, (double)(SOMVR_VOID_THEATRE_M * (float)KA.size_pct / 100.0f),
           (double)SOMVR_VOID_THEATRE_M, 2.0 * atan(0.5 * KA.size_pct / 100.0) * 57.29578,
           (double)(SOMVR_VOID_WINDOW_M * (float)KA.size_pct / 100.0f), (double)SOMVR_VOID_WINDOW_M,
           2.0 * atan(0.5 * KA.size_pct / 100.0) * 57.29578, KA.size_pct == 100 ? " (build 13)" : "");
    if (KA.reseat_bad)
        lg("xr: theatre: SOMVR_XR_THEATRE_RESEAT=%s is not on or off: off", getenv("SOMVR_XR_THEATRE_RESEAT"));
    if (KA.theatre)
        lg("xr: theatre: reseat %s", KA.reseat ? "on: a screen of the same kind after stereo frames (a new presentation, "
                                                 "video or load) is placed again where the head faces now"
                                               : "off: a screen of the same kind keeps its first place (build 13)");
    if (KA.video_bad)
        lg("xr: theatre: SOMVR_XR_THEATRE_VIDEO=%s is not on or off: off", getenv("SOMVR_XR_THEATRE_VIDEO"));
    lg("xr: theatre: video %s", KA.video ? "on: a present without eye images while the session says a video is a mono "
                                           "frame (the video's image), not the last stereo frame again"
                                         : "off: a video's eyeless layout-B presents reuse the last stereo frame (build 13)");
    KA.th_kind = 0, KA.th_n = KA.th_frames = 0, KA.th_eyes = 0, KA.th_reseats = 0, KA.video_n = 0;
    KA.stall_ns = 0, KA.space_up = 0, KA.placed_stereo = 0, KA.level = 0, KA.level_t = 0;
    KA.n_stalls = KA.n_space = KA.n_lights = 0, KA.lines = 0;
    atomic_store(&KA.q, KA.ka ? X.q : VK_NULL_HANDLE); /* the queue lock's queue */
    if (VD.gaps_ms || KA.ka) {
        atomic_store(&VD.stop, 0);
        if (!VD.th_up) atomic_store(&VD.th_done, 0);
        if (!VD.th_up && pthread_create(&VD.th, NULL, vd_thread, NULL) == 0) VD.th_up = 1;
    }
    xe_session(); /* the process's exit: our exit handler, the runtime's code pinned (once) */
    if (VD.gaps_ms)
        lg("xr: gaps: game presents %d ms or more apart are logged (the head sampled every 100 ms meanwhile%s)",
           VD.gaps_ms, VD.th_up ? "" : ": no watcher thread, no samples");
    else
        lg("xr: gaps: off (SOMVR_XR_GAPS=off)");
    lg("xr: keep-alive: %s; loadspace: %s", !KA.ka ? "off (SOMVR_XR_KEEPALIVE unset or off: a stall sends nothing)"
       : !VD.th_up ? "OFF: no watcher thread" : KA.ka_ms == 100 ? "on: our own frames when the game is away 100 ms "
       "(the game's queue calls under the keep-alive's lock)" : "on: our own frames when the game is away (SOMVR_XR_KEEPALIVE_MS)",
       !KA.space ? "off (SOMVR_XR_LOADSPACE unset or off)" : !EV.on ? "OFF: needs SOMVR_XR_ENV"
       : "on: a floor under the sky, the progress lights while the game is away, a stall in the world goes to the space");
    if (KA.ka && KA.ka_ms != 100) lg("xr: keep-alive: after %d ms (SOMVR_XR_KEEPALIVE_MS)", KA.ka_ms);
    if (KA.space && KA.space_ms != SOMVR_VOID_SPACE_MS)
        lg("xr: loadspace: a stall in the world goes to the space after %d ms (SOMVR_XR_LOADSPACE_MS)", KA.space_ms);
    cv_session();
    mb_session();
}

static void vd_summary(const char *when) {
    char top[256] = "";
    size_t o = 0;
    for (int i = 0; i < VD_TOP && VD.top_ns[i] && o < sizeof top; i++)
        o += (size_t)snprintf(top + o, sizeof top - o, "%s%.0f ms at +%.1f s (%s)", i ? ", " : "",
                              (double)VD.top_ns[i] / 1e6, VD.top_at[i], VD.top_k[i]);
    lg("xr: gaps (%s): %llu game presents in %.1f s; gaps %d..250 ms %llu, 250..500 %llu, 500..1000 %llu, 1..2 s %llu, "
       ">= 2 s %llu; the longest: %s; the longest time between two xrEndFrame calls %.0f ms; keep-alive frames %llu",
       when, (unsigned long long)VD.n_present, (double)(vd_now() - VD.sess_ns) / 1e9, VD.gaps_ms,
       (unsigned long long)VD.n_bucket[0], (unsigned long long)VD.n_bucket[1], (unsigned long long)VD.n_bucket[2],
       (unsigned long long)VD.n_bucket[3], (unsigned long long)VD.n_bucket[4], *top ? top : "none",
       (double)VD.end_gap_max / 1e6, (unsigned long long)VD.n_ka);
}

/* xr_teardown_session's start (X.mx held): the summary; the watcher joined (it never waits for X.mx). From the
 * watcher itself (an XR failure in its own frame): it only leaves. */
static void vd_stop(void) {
    if (VD.gaps_ms && VD.sess_ns) vd_summary("the session's end");
    if (VD.sess_ns && (KA.ka || KA.space))
        lg("xr: keep-alive (the session's end): %llu stall(s) filled, %llu frame(s) of ours; loadspace frames %llu, with "
           "the lights %llu", (unsigned long long)KA.n_stalls, (unsigned long long)VD.n_ka,
           (unsigned long long)KA.n_space, (unsigned long long)KA.n_lights);
    if (VD.sess_ns) cv_summary(), mb_summary();
    VD.sess_ns = 0;
    atomic_store(&KA.q, VK_NULL_HANDLE);
    vd_space_destroy(); /* the space's swapchain: before the session */
    mb_destroy();       /* the backdrop's swapchain: before the session */
    atomic_store(&VD.stop, 1);
    if (!VD.th_up) return;
    VD.th_up = 0;
    if (pthread_equal(pthread_self(), VD.th)) pthread_detach(VD.th);
    else pthread_join(VD.th, NULL);
}

static void vd_waited(void) { VD.wait_ns = vd_now(); }          /* xr_wait_publish */
static void vd_ended(void) {                                    /* xr_end_comp, a frame ended */
    int64_t now = vd_now();
    if (VD.end_ns) {
        int64_t d = now - VD.end_ns;
        if (d > VD.gap_end_max) VD.gap_end_max = d;
        if (d > VD.end_gap_max) VD.end_gap_max = d;
    }
    VD.end_ns = now;
}

/* L_QueuePresentKHR around xr_frame (X.mx held, X.ds == ds): before it the counters, after it the gap's line */
static void vd_before(void) {
    VD.c_stereo = X.n_stereo, VD.c_mono = X.n_mono, VD.c_reuse = X.n_reuse, VD.c_hold = EV.n_hold;
}
static void vd_after(int64_t t0) {
    char k = X.n_stereo != VD.c_stereo ? 'S' : X.n_mono != VD.c_mono ? 'M' : X.n_reuse != VD.c_reuse ? 'R'
           : EV.n_hold != VD.c_hold                                  ? 'H' : '-';
    char was = VD.kind;
    VD.kind = k;
    int64_t prev = atomic_exchange(&VD.present_ns, t0), gap = prev ? t0 - prev : 0;
    VD.n_present++;
    if (VD.gaps_ms && VD.sess_ns && gap >= (int64_t)VD.gaps_ms * 1000000) {
        VD.n_gaps++;
        int b = 0;
        while (b + 1 < VD_NB && gap >= (int64_t)vd_bucket_ms[b + 1] * 1000000) b++;
        VD.n_bucket[b]++;
        double at = (double)(prev - VD.sess_ns) / 1e9;
        for (int i = 0; i < VD_TOP; i++)
            if (gap > VD.top_ns[i]) {
                memmove(VD.top_ns + i + 1, VD.top_ns + i, (VD_TOP - 1 - i) * sizeof VD.top_ns[0]);
                memmove(VD.top_at + i + 1, VD.top_at + i, (VD_TOP - 1 - i) * sizeof VD.top_at[0]);
                memmove(VD.top_k + i + 1, VD.top_k + i, (VD_TOP - 1 - i) * sizeof VD.top_k[0]);
                VD.top_ns[i] = gap, VD.top_at[i] = at, VD.top_k[i][0] = was, VD.top_k[i][1] = k, VD.top_k[i][2] = 0;
                break;
            }
        char head[160] = "no samples";
        if (VD.gap_located)
            snprintf(head, sizeof head, "%u sample(s), %u located, yaw %+.0f..%+.0f deg, pitch %+.0f..%+.0f deg, moved "
                     "%.1f cm", VD.gap_samples, VD.gap_located, VD.gap_yaw[0], VD.gap_yaw[1], VD.gap_pitch[0],
                     VD.gap_pitch[1], 100.0 * VD.gap_move);
        else if (VD.gap_samples)
            snprintf(head, sizeof head, "%u sample(s), none located", VD.gap_samples);
        if (VD.n_gaps <= 400 || gap >= 1000000000LL)
            lg("xr: gap %llu: %.0f ms without a game present (from +%.2f s into the session; %c -> %c; %s shown): %u "
               "keep-alive frame(s) meanwhile, the longest time between two xrEndFrame calls %.0f ms; the head: %s",
               (unsigned long long)VD.n_gaps, (double)gap / 1e6, at, was, k,
               EV.on ? (EV.tr.shown == SOMVR_ENV_SHOW_ENV ? "the environment" : "the world")
                     : X.last_stereo ? "the world" : "a mono frame",
               VD.gap_ka, (double)VD.gap_end_max / 1e6, head);
        if (VD.n_gaps % 10 == 0 || gap >= 1000000000LL) vd_summary("so far");
    }
    ka_resumed(t0);
    VD.gap_end_max = 0, VD.gap_ka = VD.gap_samples = VD.gap_located = 0, VD.gap_move = 0, VD.sample_ns = 0;
}

/* ---- the keep-alive frames and the loading space (SOMVR_XR_KEEPALIVE=on, SOMVR_XR_LOADSPACE=on) --------------------
 *   Keep-alive: when the game has not presented for SOMVR_XR_KEEPALIVE_MS (100), the watcher ends frames of its own at
 *     the runtime's pace (xrWaitFrame) with the shown side's last layers - the world: its last projection (its own eye
 *     poses: the runtime reprojects it, world-locked) over the sky (SOMVR_XR_ENV), its HUD quad, comfort's layers, the
 *     fade at the head; the environment: the sky, the last board (if the swapchain still holds it) - until the game
 *     presents again. No Vulkan work in them (only what is already uploaded), no copy, no acquire. Monado's xrEndFrame
 *     submits on the game's queue: with the keep-alive on, the layer hooks the game's vkQueueSubmit(2), vkQueueBindSparse,
 *     vkQueueWaitIdle, vkDeviceWaitIdle and its vkQueuePresentKHR call with one recursive lock (VkQueue's external
 *     synchronisation); the watcher holds it around its xrEndFrame.
 *   The loading space: the sky gets a floor (a stone disc at the real floor, under the player) and, while the game is
 *     away or a load is on (the preload's somvr_session word: SOMVR_SSM_LOAD on a mono frame; its screen then on a
 *     panel 2.4 m wide 2.4 m ahead, vd_theatre), the progress lights (12 embers low ahead, a slow wave; up over 0.3 s,
 *     down over 1.5 s once it presents and the load is over); a
 *     stall in the world of SOMVR_XR_LOADSPACE_MS (1000) goes through the fade to the space (the world is not held
 *     frozen through a load); the game's next frame fades back as any switch (somvr_env_step). Needs SOMVR_XR_ENV. */

static void ka_config(void) { /* vd_config */
    KA.ka = somvr_void_parse_onoff(getenv("SOMVR_XR_KEEPALIVE"), &KA.ka_bad);
    KA.ka_ms = somvr_void_parse_ms(getenv("SOMVR_XR_KEEPALIVE_MS"), SOMVR_VOID_KEEP_MS, 50, 1000, &KA.ka_ms_bad);
    KA.space = somvr_void_parse_onoff(getenv("SOMVR_XR_LOADSPACE"), &KA.space_bad);
    KA.space_ms = somvr_void_parse_ms(getenv("SOMVR_XR_LOADSPACE_MS"), SOMVR_VOID_SPACE_MS, 0, 10000, &KA.space_ms_bad);
    KA.theatre = somvr_void_parse_onoff(getenv("SOMVR_XR_THEATRE"), &KA.theatre_bad);
    KA.reseat = somvr_void_parse_onoff(getenv("SOMVR_XR_THEATRE_RESEAT"), &KA.reseat_bad);
    KA.video = somvr_void_parse_onoff(getenv("SOMVR_XR_THEATRE_VIDEO"), &KA.video_bad);
    KA.size_bad = 0;
    KA.size_pct = somvr_void_parse_ms(getenv("SOMVR_XR_THEATRE_SIZE"), 100, 50, 200, &KA.size_bad);
}

/* ---- the queue lock: the game's queue calls vs the watcher's xrEndFrame ---- */
static pthread_mutex_t ka_qmx = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
static int ka_qlock(VkQueue q) {
    VkQueue s = atomic_load(&KA.q);
    if (!s || (q && q != s)) return 0;
    pthread_mutex_lock(&ka_qmx);
    return 1;
}
static void ka_qunlock(int l) {
    if (l) pthread_mutex_unlock(&ka_qmx);
}
static dev_st *ka_qdev(VkQueue q) {
    q_st *qi = LIST_FIND(g_queues, q, q);
    return qi ? qi->ds : LIST_FIND(g_devs, key, dkey(q));
}
static VKAPI_ATTR VkResult VKAPI_CALL L_QueueSubmit(VkQueue q, uint32_t n, const VkSubmitInfo *si, VkFence f) {
    dev_st *ds = ka_qdev(q);
    if (!ds || !ds->QueueSubmit) return VK_ERROR_DEVICE_LOST;
    int l = ka_qlock(q);
    VkResult r = ds->QueueSubmit(q, n, si, f);
    ka_qunlock(l);
    return r;
}
static VKAPI_ATTR VkResult VKAPI_CALL L_QueueSubmit2(VkQueue q, uint32_t n, const VkSubmitInfo2 *si, VkFence f) {
    dev_st *ds = ka_qdev(q);
    if (!ds || !ds->QueueSubmit2) return VK_ERROR_DEVICE_LOST;
    int l = ka_qlock(q);
    VkResult r = ds->QueueSubmit2(q, n, si, f);
    ka_qunlock(l);
    return r;
}
static VKAPI_ATTR VkResult VKAPI_CALL L_QueueSubmit2KHR(VkQueue q, uint32_t n, const VkSubmitInfo2 *si, VkFence f) {
    dev_st *ds = ka_qdev(q);
    if (!ds || !ds->QueueSubmit2KHR) return VK_ERROR_DEVICE_LOST;
    int l = ka_qlock(q);
    VkResult r = ds->QueueSubmit2KHR(q, n, si, f);
    ka_qunlock(l);
    return r;
}
static VKAPI_ATTR VkResult VKAPI_CALL L_QueueBindSparse(VkQueue q, uint32_t n, const VkBindSparseInfo *bi, VkFence f) {
    dev_st *ds = ka_qdev(q);
    if (!ds || !ds->QueueBindSparse) return VK_ERROR_DEVICE_LOST;
    int l = ka_qlock(q);
    VkResult r = ds->QueueBindSparse(q, n, bi, f);
    ka_qunlock(l);
    return r;
}
static VKAPI_ATTR VkResult VKAPI_CALL L_QueueWaitIdle(VkQueue q) {
    dev_st *ds = ka_qdev(q);
    if (!ds || !ds->QueueWaitIdle) return VK_ERROR_DEVICE_LOST;
    int l = ka_qlock(q);
    VkResult r = ds->QueueWaitIdle(q);
    ka_qunlock(l);
    return r;
}
static VKAPI_ATTR VkResult VKAPI_CALL L_DeviceWaitIdle(VkDevice dev) { /* every queue of the device */
    dev_st *ds = LIST_FIND(g_devs, key, dkey(dev));
    if (!ds || !ds->DeviceWaitIdle) return VK_ERROR_DEVICE_LOST;
    int l = ka_qlock(NULL);
    VkResult r = ds->DeviceWaitIdle(dev);
    ka_qunlock(l);
    return r;
}
/* dev_hook's table with the keep-alive on (decided at the layer's load, before any device) */
static PFN_vkVoidFunction ka_dev_hook(const char *n) {
    if (!KA.ka) return NULL;
    static const struct {
        const char *n;
        PFN_vkVoidFunction f;
    } t[] = {
        {"vkQueueSubmit", (PFN_vkVoidFunction)L_QueueSubmit},
        {"vkQueueSubmit2", (PFN_vkVoidFunction)L_QueueSubmit2},
        {"vkQueueSubmit2KHR", (PFN_vkVoidFunction)L_QueueSubmit2KHR},
        {"vkQueueBindSparse", (PFN_vkVoidFunction)L_QueueBindSparse},
        {"vkQueueWaitIdle", (PFN_vkVoidFunction)L_QueueWaitIdle},
        {"vkDeviceWaitIdle", (PFN_vkVoidFunction)L_DeviceWaitIdle},
    };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
        if (!strcmp(n, t[i].n)) return t[i].f;
    return NULL;
}

/* SOMVR_XR_TEST `stall MS` (the menus' xt_run, X.mx held): the present thread sleeps MS after its next present, as a
 * load that stops presenting (headless tests) */
static int vd_xt(const char *line) {
    int ms = 0;
    char tail = 0;
    if (sscanf(line, " stall %d %c", &ms, &tail) != 1) return 0;
    if (ms < 1 || ms > 60000) {
        lg("xr: test: stall %d: not 1..60000 ms", ms);
        return 1;
    }
    atomic_store(&KA.test_stall_ms, ms);
    lg("xr: test: stall %d ms after the next present", ms);
    return 1;
}
static void vd_test_stall(void) { /* L_QueuePresentKHR's end: no lock held */
    int ms = atomic_exchange(&KA.test_stall_ms, 0);
    if (ms <= 0) return;
    lg("xr: test: the present thread sleeps %d ms", ms);
    struct timespec sl = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&sl, NULL);
}

/* SOMVR_XR_FLOOR's one measurement (fl_probe): LOCAL's origin oy m above the real floor */
static void vd_floor(float oy) {
    if (oy > 0.5f && oy < 2.5f) KA.floor_oy = oy;
}

/* one of the sky's layers (the board-less frame's own layer may be one) */
static int vd_is_env(const XrCompositionLayerBaseHeader *l) {
    if (l == (const XrCompositionLayerBaseHeader *)&EV.eq) return 1;
    for (int i = 0; i < 6; i++)
        if (l == (const XrCompositionLayerBaseHeader *)&EV.face[i]) return 1;
    return 0;
}
/* the last mono frame's board is what the XR swapchain's last released image shows */
static int vd_board_ok(void) { return EV.quad_ok && !X.last_stereo && X.sc && EV.quad_nsc == X.n_sc; }
/* env_decide: the environment side can be held (the board, or the loading space the keep-alive put up) */
static int vd_env_holdable(void) { return vd_board_ok() || (KA.space_up && EV.sky_filled); }
/* env_decide's switch to the world: the space has gone */
static void vd_world_shown(void) { KA.space_up = 0, KA.loading = 0; }

/* A static swapchain of ours (w x h, 8-bit RGBA/BGRA, sRGB preferred), filled once from a host buffer on the present
 * thread by fill(px, bgra, srgb, ctx) (as the sky's), its one upload waited. 0 = made (*sc, *stat: a static image, *fmt,
 * *nimg); -1 = not (logged: "xr: WHO: off for this session: ...ALONE") */
static int vd_static_image(dev_st *ds, const char *who, const char *alone, uint32_t w, uint32_t h,
                           void (*fill)(uint8_t *px, int bgra, int srgb, void *ctx), void *ctx, XrSwapchain *sc, int *stat,
                           VkFormat *fmt, uint32_t *nimg) {
    const char *what = "the device lacks the upload's functions";
    if (!ds->CmdCopyBufferToImage || !ds->CreateBuffer || !ds->DestroyBuffer || !ds->GetBufferMemoryRequirements ||
        !ds->AllocateMemory || !ds->FreeMemory || !ds->BindBufferMemory || !ds->MapMemory || !ds->UnmapMemory ||
        !ds->is->GetPhysicalDeviceMemoryProperties || !ds->pool) {
        lg("xr: %s: off for this session: %s%s", who, what, alone);
        return -1;
    }
    int64_t fmts[64];
    uint32_t n = 0;
    XrResult r = X.xrEnumerateSwapchainFormats(X.session, 64, &n, fmts);
    static const VkFormat pref[4] = {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM,
                                     VK_FORMAT_B8G8R8A8_UNORM};
    VkFormat f = VK_FORMAT_UNDEFINED;
    for (int k = 0; XR_SUCCEEDED(r) && k < 4 && f == VK_FORMAT_UNDEFINED; k++)
        for (uint32_t i = 0; i < n && i < 64; i++)
            if (fmts[i] == pref[k]) f = pref[k];
    if (f == VK_FORMAT_UNDEFINED) {
        lg("xr: %s: off for this session: no 8-bit RGBA/BGRA swapchain format (%s)%s", who, xr_str(r), alone);
        return -1;
    }
    int bgra = f == VK_FORMAT_B8G8R8A8_SRGB || f == VK_FORMAT_B8G8R8A8_UNORM;
    int srgb = f == VK_FORMAT_R8G8B8A8_SRGB || f == VK_FORMAT_B8G8R8A8_SRGB;
    XrSwapchainCreateInfo ci = {.type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
                                .createFlags = XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT,
                                .usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
                                .format = f, .sampleCount = 1, .width = w, .height = h, .faceCount = 1, .arraySize = 1,
                                .mipCount = 1};
    *stat = 1;
    if (XR_FAILED(r = X.xrCreateSwapchain(X.session, &ci, sc))) {
        ci.createFlags = 0, *stat = 0;
        r = X.xrCreateSwapchain(X.session, &ci, sc);
    }
    if (XR_FAILED(r)) {
        *sc = XR_NULL_HANDLE;
        lg("xr: %s: off for this session: xrCreateSwapchain(%ux%u format %d): %s%s", who, w, h, f, xr_str(r), alone);
        return -1;
    }
    XrSwapchainImageVulkanKHR im[MAX_XR_IMAGES];
    for (int i = 0; i < MAX_XR_IMAGES; i++) im[i] = (XrSwapchainImageVulkanKHR){.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR};
    n = 0;
    uint32_t xi = 0;
    XrSwapchainImageAcquireInfo ai = {.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrSwapchainImageWaitInfo wti = {.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, .timeout = XR_IMAGE_TIMEOUT_NS};
    r = X.xrEnumerateSwapchainImages(*sc, MAX_XR_IMAGES, &n, (XrSwapchainImageBaseHeader *)im);
    if (XR_SUCCEEDED(r) && n) r = X.xrAcquireSwapchainImage(*sc, &ai, &xi);
    if (XR_SUCCEEDED(r) && n && xi < n) r = X.xrWaitSwapchainImage(*sc, &wti);
    if (XR_FAILED(r) || !n || xi >= n || r != XR_SUCCESS) {
        lg("xr: %s: off for this session: its image (%s, %u images, image %u)%s", who, xr_str(r), n, xi, alone);
        X.xrDestroySwapchain(*sc), *sc = XR_NULL_HANDLE;
        return -1;
    }
    VkDeviceSize sz = (VkDeviceSize)w * h * 4;
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sz,
                              .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    void *map = NULL;
    int type = -1, in_flight = 0;
    what = "vkCreateBuffer";
    VkResult vr = ds->CreateBuffer(ds->dev, &bci, NULL, &buf);
    if (vr != VK_SUCCESS) buf = VK_NULL_HANDLE;
    VkMemoryRequirements mr = {0};
    if (vr == VK_SUCCESS) {
        VkPhysicalDeviceMemoryProperties mp = {0};
        ds->GetBufferMemoryRequirements(ds->dev, buf, &mr);
        ds->is->GetPhysicalDeviceMemoryProperties(ds->pd, &mp);
        what = "no HOST_VISIBLE|HOST_COHERENT memory type for the upload";
        if ((type = somvr_pick_host_memory(&mp, mr.memoryTypeBits)) < 0) vr = VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (vr == VK_SUCCESS) {
        VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
                                    .memoryTypeIndex = (uint32_t)type};
        what = "vkAllocateMemory";
        if ((vr = ds->AllocateMemory(ds->dev, &mai, NULL, &mem)) != VK_SUCCESS) mem = VK_NULL_HANDLE;
    }
    if (vr == VK_SUCCESS) what = "vkBindBufferMemory", vr = ds->BindBufferMemory(ds->dev, buf, mem, 0);
    if (vr == VK_SUCCESS && (what = "vkMapMemory", vr = ds->MapMemory(ds->dev, mem, 0, VK_WHOLE_SIZE, 0, &map)) != VK_SUCCESS)
        map = NULL;
    if (vr == VK_SUCCESS) {
        fill(map, bgra, srgb, ctx); /* HOST_COHERENT: visible to the device at the submit */
        VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                           .commandPool = ds->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                           .commandBufferCount = 1};
        what = "vkAllocateCommandBuffers";
        if ((vr = ds->AllocateCommandBuffers(ds->dev, &cai, &cmd)) != VK_SUCCESS) cmd = VK_NULL_HANDLE;
    }
    if (vr == VK_SUCCESS) {
        what = "the command buffer's dispatch";
        if (ds->set_loader_data) vr = ds->set_loader_data(ds->dev, cmd);
        else *(void **)cmd = dkey(ds->dev);
    }
    if (vr == VK_SUCCESS) {
        VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        what = "vkCreateFence";
        if ((vr = ds->CreateFence(ds->dev, &fci, NULL, &fence)) != VK_SUCCESS) fence = VK_NULL_HANDLE;
    }
    if (vr == VK_SUCCESS) {
        VkCommandBufferBeginInfo cbi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        what = "vkBeginCommandBuffer";
        vr = ds->BeginCommandBuffer(cmd, &cbi);
    }
    if (vr == VK_SUCCESS) {
        VkImageMemoryBarrier b;
        img_barrier(&b, im[xi].image, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL,
                               1, &b);
        VkBufferImageCopy rg = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {w, h, 1}};
        ds->CmdCopyBufferToImage(cmd, buf, im[xi].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
        img_barrier(&b, im[xi].image, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                               NULL, 1, &b);
        what = "vkEndCommandBuffer";
        vr = ds->EndCommandBuffer(cmd);
    }
    if (vr == VK_SUCCESS) {
        VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
        what = "vkQueueSubmit";
        if ((vr = ds->QueueSubmit(X.q, 1, &si, fence)) == VK_SUCCESS) in_flight = 1;
    }
    if (vr == VK_SUCCESS) {
        what = "vkWaitForFences";
        if ((vr = ds->WaitForFences(ds->dev, 1, &fence, VK_TRUE, FENCE_TIMEOUT_NS)) == VK_SUCCESS) in_flight = 0;
    }
    if (!in_flight) { /* a copy still running (the wait failed) keeps its buffer, command buffer and fence: a leak */
        if (cmd) ds->FreeCommandBuffers(ds->dev, ds->pool, 1, &cmd);
        if (fence) ds->DestroyFence(ds->dev, fence, NULL);
        if (map) ds->UnmapMemory(ds->dev, mem);
        if (buf) ds->DestroyBuffer(ds->dev, buf, NULL);
        if (mem) ds->FreeMemory(ds->dev, mem, NULL);
    }
    XrSwapchainImageReleaseInfo ri = {.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if (vr != VK_SUCCESS || XR_FAILED(r = X.xrReleaseSwapchainImage(*sc, &ri))) {
        lg("xr: %s: off for this session: the upload: %s: %d (%s)%s", who, what, vr, xr_str(r), alone);
        if (vr == VK_SUCCESS || !in_flight) X.xrDestroySwapchain(*sc), *sc = XR_NULL_HANDLE;
        return -1;
    }
    *fmt = f, *nimg = n;
    return 0;
}
static void vd_space_px(uint8_t *px, int bgra, int srgb, void *ctx) {
    (void)ctx;
    somvr_void_image(px, bgra, srgb);
}
/* the floor and lights' image (vd_static_image); 0 = ready, -1 = the loading space is off for the session (logged
 * once) */
static int vd_space_fill(dev_st *ds) {
    if (KA.filled) return 0;
    if (KA.dead) return -1;
    KA.dead = 1; /* until filled */
    const uint32_t w = SOMVR_VOID_IMG_W, h = SOMVR_VOID_IMG_H;
    uint32_t n = 0;
    VkFormat f = VK_FORMAT_UNDEFINED;
    if (vd_static_image(ds, "loadspace", " (the sky alone)", w, h, vd_space_px, NULL, &KA.sc, &KA.stat, &f, &n)) return -1;
    KA.filled = 1, KA.dead = 0, KA.fmt = f;
    int32_t rc[4];
    somvr_void_floor_rect(rc);
    KA.floor_q = (XrCompositionLayerQuad){.type = XR_TYPE_COMPOSITION_LAYER_QUAD,
                                          .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
                                          .space = X.space, .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
                                          .subImage = {KA.sc, {{rc[0], rc[1]}, {rc[2], rc[3]}}, 0},
                                          .pose = {{0, 0, 0, 1}, {0, 0, 0}},
                                          .size = {SOMVR_VOID_FLOOR_M, SOMVR_VOID_FLOOR_M}};
    KA.lights_q = KA.floor_q;
    KA.lights_q.size = (XrExtent2Df){SOMVR_VOID_LIGHTS_W_M, SOMVR_VOID_LIGHTS_H_M};
    lg("xr: loadspace: image %ux%u format %d (%s swapchain, %u image(s)) filled once: the floor disc (%.0f m) and the "
       "progress lights (%d embers, %d wave frames x %d levels)", w, h, f, KA.stat ? "a static" : "an ordinary", n,
       (double)SOMVR_VOID_FLOOR_M, SOMVR_VOID_NLIGHTS, SOMVR_VOID_WAVES, SOMVR_VOID_LEVELS);
    return 0;
}
static void vd_space_destroy(void) { /* xr_teardown_session: before the session */
    if (KA.sc) X.xrDestroySwapchain(KA.sc);
    KA.sc = XR_NULL_HANDLE;
    KA.filled = KA.dead = 0, KA.space_up = 0, KA.placed_stereo = 0, KA.level = 0, KA.level_t = 0;
}

/* the space's place: the floor under the head, the lights ahead along its yaw (once per stretch away from the world) */
static void vd_space_place(void) {
    if (KA.placed_stereo == X.n_stereo + 1 || !env_head()) return;
    float h[3], q[4];
    for (int i = 0; i < 3; i++) h[i] = 0.5f * (EV.head[0].p[i] + EV.head[1].p[i]);
    memcpy(q, EV.head[0].q, sizeof q);
    float fy = X.space_type == XR_REFERENCE_SPACE_TYPE_STAGE ? 0.0f : -(KA.floor_oy > 0 ? KA.floor_oy : SOMVR_VOID_EYE_M);
    somvr_posef fp, lp;
    somvr_void_floor_pose(h, fy, &fp);
    somvr_void_lights_pose(h, somvr_void_yaw(q), fy, &lp);
    memcpy(&KA.floor_q.pose, &fp, sizeof fp);
    memcpy(&KA.lights_q.pose, &lp, sizeof lp);
    KA.placed_stereo = X.n_stereo + 1;
    if (KA.lines++ < 40)
        lg("xr: loadspace: placed: the floor at %+.2f m (%s) under %+.2f,%+.2f, the lights at %+.2f,%+.2f,%+.2f facing "
           "yaw %+.0f deg", fy, X.space_type == XR_REFERENCE_SPACE_TYPE_STAGE ? "STAGE's"
                                : KA.floor_oy > 0 ? "the real floor, SOMVR_XR_FLOOR" : "1.6 m below LOCAL's origin",
           h[0], h[2], lp.p[0], lp.p[1], lp.p[2], somvr_void_yaw(q) * 57.29578f);
}

/* The loading space's layers after the environment's n (mono, hold and keep-alive frames): the floor, the lights while
 * the game is away (stalled) or fading after it; may_fill: the present thread (the first fill). A frame without a
 * board gets the top layer as its own (xr_end_try ends a frame without one empty). */
static void vd_space_add(dev_st *ds, xr_comp *c, uint32_t n, int stalled, int may_fill) {
    if (KA.space && EV.on && n && (KA.filled || (may_fill && vd_space_fill(ds) == 0))) {
        vd_space_place();
        XrTime t = X.fs.predictedDisplayTime;
        float dt = KA.level_t && t > KA.level_t ? (float)((double)(t - KA.level_t) * 1e-9) : 0.0f;
        KA.level_t = t;
        KA.level = somvr_void_level_step(KA.level, stalled || KA.loading || (CV.on && CV.st.space),
                                         dt); /* the game away, a load, or the game's in-world cover */
        if (n < XL_ENV_N) c->l[XL_ENV0 + n++] = (const XrCompositionLayerBaseHeader *)&KA.floor_q;
        int k = somvr_void_level_index(KA.level);
        if (k && n < XL_ENV_N) {
            int32_t rc[4];
            somvr_void_lights_rect(somvr_void_wave((double)t * 1e-9), k, rc);
            KA.lights_q.subImage.imageRect = (XrRect2Di){{rc[0], rc[1]}, {rc[2], rc[3]}};
            c->l[XL_ENV0 + n++] = (const XrCompositionLayerBaseHeader *)&KA.lights_q;
            KA.n_lights++;
        }
        KA.n_space++;
        if ((KA.n_space <= 2 || KA.n_space % 600 == 0 || (stalled && KA.stall_frames == 0) || cv_line()) && KA.lines++ < 400 &&
            env_head()) { /* what a picture of it needs (tests/void_view.c) */
            float h[3], q[4];
            for (int i = 0; i < 3; i++) h[i] = 0.5f * (EV.head[0].p[i] + EV.head[1].p[i]);
            memcpy(q, EV.head[0].q, sizeof q);
            float fx = -2.0f * (q[3] * q[1] + q[0] * q[2]), fy = 2.0f * (q[3] * q[0] - q[1] * q[2]);
            const XrCompositionLayerQuad *b = /* the board: the frame's own quad when it is not one of the space's */
                c->l[XL_OWN] && c->l[XL_OWN]->type == XR_TYPE_COMPOSITION_LAYER_QUAD &&
                        c->l[XL_OWN] != (const XrCompositionLayerBaseHeader *)&KA.floor_q &&
                        c->l[XL_OWN] != (const XrCompositionLayerBaseHeader *)&KA.lights_q && !vd_is_env(c->l[XL_OWN])
                    ? (const XrCompositionLayerQuad *)c->l[XL_OWN] : NULL;
            lg("xr: loadspace frame %llu: the eyes at %+.3f,%+.3f,%+.3f yaw %+.1f pitch %+.1f deg; the lights level %.2f wave "
               "%d%s; the board %s%+.3f,%+.3f,%+.3f q %+.4f,%+.4f,%+.4f,%+.4f size %.3f x %.3f", (unsigned long long)KA.n_space,
               h[0], h[1], h[2], somvr_void_yaw(q) * 57.29578f, asinf(fy < -1 ? -1 : fy > 1 ? 1 : fy) * 57.29578f, KA.level,
               somvr_void_wave((double)t * 1e-9), stalled ? " (the game away)" : "", b ? "at " : "none, at ", b ? b->pose.position.x : 0,
               b ? b->pose.position.y : 0, b ? b->pose.position.z : 0, b ? b->pose.orientation.x : 0,
               b ? b->pose.orientation.y : 0, b ? b->pose.orientation.z : 0, b ? b->pose.orientation.w : 1,
               b ? b->size.width : 0, b ? b->size.height : 0);
            (void)fx;
        }
    }
    if (!c->l[XL_OWN]) /* no board: the top environment layer is the frame's own */
        for (int i = XL_OWN - 1; i >= XL_ENV0; i--)
            if (c->l[i]) {
                c->l[XL_OWN] = c->l[i], c->l[i] = NULL;
                break;
            }
}

/* the keep-alive's side: the shown one, or (SOMVR_XR_LOADSPACE) the space after SOMVR_XR_LOADSPACE_MS in the world,
 * through the fade (somvr_env_step on this frame's time); 1 = the world, 0 = the environment, -1 = nothing to show */
static int ka_side(int64_t away) {
    if (!EV.on) return X.last_stereo ? 1 : -1;
    int shown = EV.tr.started ? EV.tr.shown : X.last_stereo ? SOMVR_ENV_SHOW_WORLD : SOMVR_ENV_SHOW_ENV;
    int want = shown;
    if (KA.space && away >= (int64_t)KA.space_ms * 1000000 && (KA.filled || !KA.dead) && EV.sky_filled)
        want = SOMVR_ENV_SHOW_ENV;
    int can_hold = shown == SOMVR_ENV_SHOW_WORLD ? X.last_stereo : vd_board_ok() || EV.sky_filled;
    if (!can_hold) return -1;
    int sw = 0;
    somvr_env_step(&EV.tr, want, can_hold, X.fs.predictedDisplayTime, &sw);
    EV.level = somvr_env_level(&EV.tr);
    if (sw && EV.tr.switches) {
        if (EV.lines < 200)
            lg("xr: env: %s -> %s after %u hold frame(s) (switch %llu, the keep-alive: the game away %.0f ms)",
               EV.tr.shown == SOMVR_ENV_SHOW_ENV ? "world" : "environment",
               EV.tr.shown == SOMVR_ENV_SHOW_ENV ? "the loading space" : "world", EV.tr.holds,
               (unsigned long long)EV.tr.switches, (double)away / 1e6);
        EV.lines++;
        if (EV.tr.shown == SOMVR_ENV_SHOW_ENV) KA.space_up = !vd_board_ok(), KA.stall_space = 1;
        else env_world_free(), KA.space_up = 0;
    }
    return EV.tr.shown == SOMVR_ENV_SHOW_WORLD;
}

/* one keep-alive frame (the watcher, X.mx held, the game away): begin, the shown side's layers, end (under the queue
 * lock), then the next wait. Returns how long that wait took (ns), -1 = no frame. */
static int64_t ka_frame(int64_t now) {
    dev_st *ds = X.ds;
    if (!ds || !X.sc || X.frame_open || X.img_acquired) return -1;
    if (xr_poll() || X.phase != P_SESSION || !X.running) return -1;
    int64_t since = atomic_load(&VD.present_ns);
    if (!KA.stall_ns) {
        KA.stall_ns = since, KA.stall_frames = 0, KA.stall_space = KA.stall_nothing = 0;
        KA.n_stalls++;
    }
    if (!X.waited && xr_wait_publish()) return -1;
    int side = ka_side(now - KA.stall_ns);
    if (side < 0) {
        if (!KA.stall_nothing++ && KA.lines++ < 40)
            lg("xr: keep-alive: nothing to hold (%s): the runtime keeps the last frame", !EV.on ? "a mono frame without "
               "SOMVR_XR_ENV" : "no frame shown yet");
        return -1;
    }
    if (xr_begin()) return -1;
    xr_comp c;
    memset(&c, 0, sizeof c);
    if (side) { /* the world held: its projection (the runtime reprojects it) over the sky, its HUD, comfort's layers */
        c.l[XL_OWN] = proj_layer(&KA.proj, X.last_pv);
        if (EV.on && EV.sky_filled) env_scene(ds, &c);
        if (X.last_hud && X.hud_sc) c.l[XL_HUD] = (const XrCompositionLayerBaseHeader *)&X.last_quad;
        mb_layers(&c); /* SOMVR_XR_MENU_BACK: a menu board's backdrop */
        uint32_t level = env_level(X.last_fade);
        if (level && X.fade_on && X.fade_filled) fade_frame(ds, level, env_head() ? EV.head : X.last_fade_eye, fade_red(level));
        cf_reuse_layers(&c);
    } else { /* the environment: the sky, the board if it is still there, the space, the lights */
        if (vd_board_ok()) c.l[XL_OWN] = (const XrCompositionLayerBaseHeader *)&EV.last_quad;
        uint32_t n = EV.sky_filled ? env_scene(ds, &c) : 0;
        vd_space_add(ds, &c, n, 1, 0);
        if (X.fade_filled) env_fade(ds);
    }
    vd_hands(ds, &c, 1); /* our hands, live (SOMVR_XR_LOADSPACE with SOMVR_VRHANDS) */
    fade_comp(&c);
    int l = ka_qlock(NULL);
    xr_end_comp(&c);
    ka_qunlock(l);
    KA.stall_frames++, VD.n_ka++, VD.gap_ka++;
    if (KA.stall_frames == 1 && KA.lines++ < 60)
        lg("xr: keep-alive: the game away %.0f ms: our own frames (%s; frame %llu of ours)", (double)(now - since) / 1e6,
           side ? "the world held over the sky" : vd_board_ok() ? "the board in the environment" : "the loading space",
           (unsigned long long)VD.n_ka);
    if (X.phase != P_SESSION || !X.running) return -1;
    int64_t t0 = vd_now();
    if (xr_wait_publish()) return -1;
    return vd_now() - t0;
}

/* vd_after: the game presented again after a stall the keep-alive filled */
static void ka_resumed(int64_t t0) {
    if (!KA.stall_ns) return;
    if (KA.n_stalls <= 200 || KA.stall_space)
        lg("xr: keep-alive: the game presented again after %.0f ms away: %llu frame(s) of ours%s (stall %llu)",
           (double)(t0 - KA.stall_ns) / 1e6, (unsigned long long)KA.stall_frames,
           KA.stall_space ? ", the loading space shown" : "", (unsigned long long)KA.n_stalls);
    KA.stall_ns = 0;
}
static int ka_on(void) { return KA.ka; }

/* Our hands in the loading space (SOMVR_XR_LOADSPACE=on with SOMVR_VRHANDS=1): every mono frame and keep-alive frame
 * gets them from the layer's own input - the controllers' grips (SOMVR_XR_TEST's hands in a headless test), the
 * index from the trigger, the other three from the grip, the thumb down when it touches - at the eyes located for the
 * frame, over the board and the space (XL_HANDS). The game publishes no hands outside first person. watcher = 1: the
 * keep-alive's frame (the drawing's submit and its image under the queue lock). */
static void vd_hands(dev_st *ds, xr_comp *c, int watcher) {
    if (!KA.space || !EV.on || !VH.on || !I.on || !X.session || (watcher && !VH.ready)) return;
    XrViewLocateInfo li = {.type = XR_TYPE_VIEW_LOCATE_INFO,
                           .viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                           .displayTime = X.fs.predictedDisplayTime, .space = X.space};
    XrViewState vs = {.type = XR_TYPE_VIEW_STATE};
    XrView v[2] = {{.type = XR_TYPE_VIEW}, {.type = XR_TYPE_VIEW}};
    uint32_t nv = 0;
    const XrViewStateFlags need = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
    XrResult r = X.xrLocateViews(X.session, &li, &vs, 2, &nv, v);
    if (XR_FAILED(r) || nv != 2 || (vs.viewStateFlags & need) != need) return;
    somvr_vrhands_frame e;
    memset(&e, 0, sizeof e);
    e.flags = SOMVR_VHF_HIDE;
    for (int h = 0; h < 2; h++) {
        const somvr_hand_in *hi = &I.st.hand[h];
        if ((hi->grip_flags & 3u) != 3u) continue; /* orientation and position valid */
        e.grip[h] = hi->grip;
        e.flags |= h ? SOMVR_VHF_R : SOMVR_VHF_L;
        float sq = hi->squeeze < 0 ? 0 : hi->squeeze > 1 ? 1 : hi->squeeze, tr = hi->trigger < 0 ? 0 : hi->trigger > 1 ? 1
                                                                                                                : hi->trigger;
        e.curl[h][0] = hi->buttons & SOMVR_BTN_THUMB_TOUCH ? 0.6f : 0.15f;
        e.curl[h][1] = 0.1f + 0.9f * tr;
        e.curl[h][2] = e.curl[h][3] = e.curl[h][4] = 0.15f + 0.85f * sq;
    }
    if (!(e.flags & (SOMVR_VHF_L | SOMVR_VHF_R))) return;
    somvr_rec rec;
    memset(&rec, 0, sizeof rec);
    rec.display_time = X.fs.predictedDisplayTime;
    XrCompositionLayerProjectionView pv[2];
    for (int k = 0; k < 2; k++) {
        memcpy(&rec.eye[k].pose, &v[k].pose, sizeof rec.eye[k].pose);
        pv[k] = (XrCompositionLayerProjectionView){.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW, .pose = v[k].pose,
                                                   .fov = v[k].fov};
    }
    int l = watcher ? ka_qlock(NULL) : 0;
    vh_frame_e(ds, X.q, &rec, pv, c, NULL, &e);
    ka_qunlock(l);
}

/* The theatre (SOMVR_XR_THEATRE=on; design-vr.md 6): xr_frame's mono quad after the board's placement. While the
 * preload's somvr_session says a video (the story cutscenes' UI composite) or SOMVR_BODYMOMENTS' window (in-engine
 * cinematics, captain presentations) is on the flat frames, they go on a big screen in the loading space instead of the
 * board: 5 m wide 5 m ahead (a video) or 3 m at 3 m (the window), at eye height, world-locked where the head faced when
 * it began (a new start places it again). With SOMVR_XR_LOADSPACE a loading screen (the preload's SOMVR_SSM_LOAD on a
 * mono frame) goes on a calm panel 2.4 m wide 2.4 m ahead and keeps the progress lights on through the load; menus keep
 * the board. */
static void vd_theatre(XrCompositionLayerQuad *q, uint32_t w, uint32_t h) {
    KA.loading = 0;
    if (!SSN.obj || !w || !h) return;
    uint32_t k = atomic_load_explicit(&SSN.obj->mono, memory_order_relaxed);
    int on = (k == SOMVR_SSM_VIDEO || k == SOMVR_SSM_WINDOW) ? KA.theatre : k == SOMVR_SSM_LOAD ? KA.space && EV.on : 0;
    if (!on) {
        KA.th_kind = 0;
        return;
    }
    KA.loading = k == SOMVR_SSM_LOAD; /* SOMVR_XR_LOADSPACE: the lights through the load, its panel */
    if (KA.reseat && KA.th_kind && X.n_eyes != KA.th_eyes) { /* stereo frames since its last mono frame: a new start */
        KA.th_kind = 0, KA.th_reseats++;                         /* (build 13 kept the first place: run 081246) */
    }
    KA.th_eyes = X.n_eyes;
    float m = k == SOMVR_SSM_VIDEO ? SOMVR_VOID_THEATRE_M : k == SOMVR_SSM_WINDOW ? SOMVR_VOID_WINDOW_M : SOMVR_VOID_LOAD_M;
    /* SOMVR_XR_THEATRE_SIZE (build 14 candidate, the story drill's round 2): a video's and the window's screen wider
     * than its distance (a captain's line was 0.6 deg on 3 m at 3 m); the load panel stays */
    float wm = k == SOMVR_SSM_LOAD ? m : m * (float)(KA.size_pct > 0 ? KA.size_pct : 100) / 100.0f;
    if (k != KA.th_kind) {
        somvr_posef eye[2];
        if (!env_head()) return;
        eye[0] = EV.head[0], eye[1] = EV.head[1];
        somvr_mn_board_pose(eye, m, 0.0f, &KA.th_pose);
        KA.th_kind = k, KA.th_n++;
        if (KA.th_n <= 50)
            lg("xr: theatre %llu: %s on a %.1f m screen at %+.2f,%+.2f,%+.2f (%.1f m ahead at eye height)%s",
               (unsigned long long)KA.th_n, k == SOMVR_SSM_VIDEO ? "a video" : k == SOMVR_SSM_WINDOW ? "the cinematic window"
                                                                                                     : "a loading screen", (double)wm,
               KA.th_pose.p[0], KA.th_pose.p[1], KA.th_pose.p[2], (double)m,
               KA.th_reseats ? " (reseat: placed again after stereo frames)" : "");
        KA.th_reseats = 0;
    }
    float sz[2];
    somvr_quad_size(w, h, wm, sz);
    memcpy(&q->pose, &KA.th_pose, sizeof q->pose);
    q->size.width = sz[0], q->size.height = sz[1];
    KA.th_frames++;
}

/* SOMVR_XR_THEATRE_VIDEO=on (build 14; the story drill, run 20261026-083012): the story mission's video (56 s,
 * `video=1 ctx=menu`) came as layout-B presents whose tag names no eye images, so somvr_submit_decide made them reuse
 * frames - the last stereo frame again for the whole video (`reuse 600`): a frozen world, the video only heard. With
 * it such a present, while the preload's session says a video, is a mono frame: the presented image (the video) on the
 * theatre's 5 m screen (SOMVR_XR_THEATRE) or the menu board. xr_frame's decision, after somvr_submit_decide. */
static int vd_video_mono(int have_tag, int have_rec) {
    if (!KA.video || !have_tag || have_rec || !SSN.obj) return 0;
    if (atomic_load_explicit(&SSN.obj->mono, memory_order_relaxed) != SOMVR_SSM_VIDEO) return 0;
    if (KA.video_n++ % 600 == 0)
        lg("xr: theatre: a video's present without eye images: a mono frame, the video's image (%llu so far)",
           (unsigned long long)KA.video_n);
    return 1;
}

/* ==== The game's in-world loading cover (SOMVR_XR_COVER=on; docs/re-xr-pipeline.md 36.7, handoff/HANDOFF-void.md
 * round 2). After a fast travel (4-6 s) and a far respawn (40-60 s) the game covers its UI surface with opaque black
 * and a small spinner while the world goes on (`ctx=world paused=0`): build 12 showed it as the HUD - a black slab with
 * holes 1.5 m ahead, the world around it. Each stereo frame's submit copies a 16 x 9 grid of the HUD image's texels
 * into a host buffer (eyes_submit -> cv_cmds); the reading is taken when that submit is done (cv_poll: its fence at
 * the next presents; or when its slot comes round). Most of the grid dark = the HUD quad, the band and the wrist
 * minimap hidden (never a head-locked black); opaque black held SOMVR_XR_COVER_MS = a load: env_decide goes to the
 * environment as at a mono frame - the world held darkening, then the loading space with the progress lights (the
 * spinner's meaning) and our hands - while the game's stereo frames go on being copied under it (the readings go
 * on); the cover lifting = back to the world through the fade. A game menu or a video is never a cover. ==== */
static void cv_config(void) { /* vd_config */
    CV.on = somvr_void_parse_onoff(getenv("SOMVR_XR_COVER"), &CV.bad);
    CV.ms = somvr_void_parse_ms(getenv("SOMVR_XR_COVER_MS"), SOMVR_COVER_MS, 0, 5000, &CV.ms_bad);
}
static void cv_session(void) { /* vd_start */
    if (CV.bad) lg("xr: cover: SOMVR_XR_COVER=%s is not on or off: off", getenv("SOMVR_XR_COVER"));
    if (CV.ms_bad) lg("xr: cover: SOMVR_XR_COVER_MS=%s is not 0..5000: %d", getenv("SOMVR_XR_COVER_MS"), CV.ms);
    somvr_cover_reset(&CV.st);
    CV.st.n_hide = CV.st.n_space = 0;
    CV.dead = 0, CV.lines = 0, CV.await = 0, CV.await_t = 0;
    CV.n_sub = CV.seq_read = CV.n_read = CV.n_late = CV.n_menu = CV.n_space_frames = CV.n_hidden_frames = 0;
    CV.s_hidden = CV.s_space = 0;
    memset(CV.e, 0, sizeof CV.e);
    if (CV.on)
        lg("xr: cover: on: the HUD image's %d x %d grid read each stereo frame; the HUD, the band and the wrist minimap "
           "hidden while most of it is dark; the game's black cover held %d ms is a load: %s", SOMVR_COVER_NX,
           SOMVR_COVER_NY, CV.ms, !EV.on ? "NO SPACE (needs SOMVR_XR_ENV): the world without the HUD"
           : KA.space ? "the loading space with the progress lights, back through the fade when it lifts"
                      : "the sky (the floor and the lights need SOMVR_XR_LOADSPACE), back through the fade when it lifts");
}
static void cv_summary(void) { /* vd_stop */
    if (!CV.on) return;
    lg("xr: cover (the session's end): %llu reading(s) (%llu late, %llu menu/video frames), the HUD hidden %u time(s) "
       "(%.1f s, %llu frames), %u load(s) in the space (%.1f s, %llu frames)", (unsigned long long)CV.n_read,
       (unsigned long long)CV.n_late, (unsigned long long)CV.n_menu, CV.st.n_hide, CV.s_hidden,
       (unsigned long long)CV.n_hidden_frames, CV.st.n_space, CV.s_space, (unsigned long long)CV.n_space_frames);
}
/* the buffer and its memory (no copy in flight: after sc_wait_all, or never submitted) */
static void cv_free(dev_st *ds) {
    if (ds && CV.map) ds->UnmapMemory(ds->dev, CV.mem);
    if (ds && CV.buf) ds->DestroyBuffer(ds->dev, CV.buf, NULL);
    if (ds && CV.mem) ds->FreeMemory(ds->dev, CV.mem, NULL);
    CV.map = NULL, CV.buf = VK_NULL_HANDLE, CV.mem = VK_NULL_HANDLE, CV.ds = NULL;
    memset(CV.e, 0, sizeof CV.e);
}
static void cv_destroy(void) { cv_free(CV.ds); } /* xr_teardown_session, after sc_wait_all */
/* the readback buffer (made at the first reading of the session); 0 = ready */
static int cv_buf(dev_st *ds) {
    if (CV.map) return CV.ds == ds ? 0 : -1;
    if (CV.dead) return -1;
    CV.dead = 1;
    const char *what = "the device lacks the readback's functions";
    VkDeviceSize sz = (VkDeviceSize)CV_NE * SOMVR_COVER_N * 4;
    if (ds->CmdCopyImageToBuffer && ds->CreateBuffer && ds->DestroyBuffer && ds->GetBufferMemoryRequirements &&
        ds->AllocateMemory && ds->FreeMemory && ds->BindBufferMemory && ds->MapMemory && ds->UnmapMemory &&
        ds->is->GetPhysicalDeviceMemoryProperties) {
        VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sz,
                                  .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
        what = "vkCreateBuffer";
        if (ds->CreateBuffer(ds->dev, &bci, NULL, &CV.buf) != VK_SUCCESS) CV.buf = VK_NULL_HANDLE;
        else {
            VkMemoryRequirements mr;
            VkPhysicalDeviceMemoryProperties mp;
            ds->GetBufferMemoryRequirements(ds->dev, CV.buf, &mr);
            ds->is->GetPhysicalDeviceMemoryProperties(ds->pd, &mp);
            int t = somvr_pick_host_memory(&mp, mr.memoryTypeBits);
            VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
                                       .memoryTypeIndex = (uint32_t)t};
            void *m = NULL;
            what = "no HOST_VISIBLE|HOST_COHERENT memory type";
            if (t >= 0 && (what = "vkAllocateMemory", ds->AllocateMemory(ds->dev, &ai, NULL, &CV.mem)) != VK_SUCCESS)
                CV.mem = VK_NULL_HANDLE;
            else if (t >= 0 && (what = "vkBindBufferMemory", ds->BindBufferMemory(ds->dev, CV.buf, CV.mem, 0)) == VK_SUCCESS &&
                     (what = "vkMapMemory", ds->MapMemory(ds->dev, CV.mem, 0, VK_WHOLE_SIZE, 0, &m)) == VK_SUCCESS) {
                CV.map = m, CV.ds = ds, CV.dead = 0;
                memset(m, 0, (size_t)sz);
                return 0;
            }
        }
    }
    lg("xr: cover: no readback this session: %s (the HUD as before)", what);
    cv_free(ds);
    return -1;
}
/* 4-byte texels with alpha in the 4th byte */
static int cv_fmt_ok(VkFormat f) {
    return f == VK_FORMAT_R8G8B8A8_UNORM || f == VK_FORMAT_R8G8B8A8_SRGB || f == VK_FORMAT_B8G8R8A8_UNORM ||
           f == VK_FORMAT_B8G8R8A8_SRGB || f == VK_FORMAT_A8B8G8R8_UNORM_PACK32 || f == VK_FORMAT_A8B8G8R8_SRGB_PACK32;
}
static int cv_k_armed = -1; /* the entry cv_cmds recorded into this submit (cv_submitted confirms or frees it) */
/* the hide/space changes of a reading (or of its age) at t: their lines, the time totals */
static void cv_changed(int ch, double t) {
    if (!ch) return;
    if ((ch & SOMVR_COVER_CH_HIDE) && CV.st.hide) {
        CV.t_hide = t;
        if (CV.lines++ < 200)
            lg("xr: cover %u: the HUD hidden (the grid %.2f dark, %.2f opaque black; stereo frame %llu)", CV.st.n_hide,
               (double)CV.st.dark, (double)CV.st.black, (unsigned long long)X.n_stereo);
    }
    if ((ch & SOMVR_COVER_CH_SPACE) && CV.st.space) {
        CV.t_space = t, CV.k = 0;
        if (CV.lines++ < 200)
            lg("xr: cover %u: the game's cover held %.2f s (%.2f opaque black): a load - %s", CV.st.n_space,
               CV.st.t_black >= 0 ? t - CV.st.t_black : 0.0, (double)CV.st.black,
               !EV.on ? "no environment (SOMVR_XR_ENV off): the world without the HUD"
               : KA.space ? "the loading space with the progress lights" : "the sky");
    }
    if ((ch & SOMVR_COVER_CH_SPACE) && !CV.st.space) {
        CV.s_space += t - CV.t_space;
        if (CV.lines++ < 200)
            lg("xr: cover %u: %s (%.2f opaque black, %.2f dark) after %.1f s: back to the world through the fade",
               CV.st.n_space, CV.st.t_last >= 0 && t - CV.st.t_last >= SOMVR_COVER_STALE_S - 1e-3 ? "no reading for 1 s"
               : "lifted", (double)CV.st.black, (double)CV.st.dark, t - CV.t_space);
    }
    if ((ch & SOMVR_COVER_CH_HIDE) && !CV.st.hide) {
        CV.s_hidden += t - CV.t_hide;
        if (CV.lines++ < 200)
            lg("xr: cover %u: the HUD shown again (%.2f dark) after %.1f s hidden", CV.st.n_hide, (double)CV.st.dark,
               t - CV.t_hide);
    }
}
/* entry i's submit is done: its reading (an older one than the last taken is dropped) */
static void cv_take(int i) {
    uint64_t seq = CV.e[i].seq;
    double t = CV.e[i].t;
    CV.e[i].sl = NULL;
    if (!CV.map) return;
    if (seq <= CV.seq_read) {
        CV.n_late++;
        return;
    }
    CV.seq_read = seq, CV.n_read++;
    float b, d;
    const uint8_t *px = CV.map + (size_t)i * SOMVR_COVER_N * 4;
    somvr_cover_measure(px, SOMVR_COVER_N, &b, &d);
    int menu = CV.e[i].menu && somvr_cover_pure(px, SOMVR_COVER_N) < SOMVR_COVER_MENU_PURE; /* a menu, not the cover */
    CV.n_menu += menu;
    cv_changed(somvr_cover_step(&CV.st, t, b, d, menu, (float)CV.ms * 1e-3f), t);
    if (CV.await && menu) cv_await_end("a game menu", t);
    if (CV.await && CV.st.space) cv_await_end("the game's cover: its load", t);
    else if (CV.await && !CV.st.hide) cv_await_end("no cover", t);
    if (CV.n_read <= 2 || CV.n_read % 600 == 0)
        lg("xr: cover: reading %llu: %.2f opaque black, %.2f dark (hide %d, load %d; %llu late)", (unsigned long long)CV.n_read,
           (double)b, (double)d, CV.st.hide, CV.st.space, (unsigned long long)CV.n_late);
}
/* slot_begin (its fence waited) and sc_free_slots: the readings that slot's last submit carried; idle = 0: dropped */
static void cv_slot_done(const slot_st *sl, int idle) {
    for (int i = 0; i < CV_NE; i++)
        if (CV.e[i].sl == sl) {
            if (idle) cv_take(i);
            else CV.e[i].sl = NULL;
        }
}
/* eyes_submit, after the HUD's copy (the HUD image in TRANSFER_SRC_OPTIMAL): the grid into a free entry */
static void cv_cmds(dev_st *ds, VkCommandBuffer cmd, const slot_st *sl, const hud_st *hd) {
    cv_k_armed = -1;
    if (!CV.on || !hd || (hd->kind & 0xffu) != SOMVR_HUDK_WORLD) return; /* (a video: cv_kind) */
    if (!cv_fmt_ok(hd->fmt) || hd->w < SOMVR_COVER_NX || hd->h < SOMVR_COVER_NY) {
        if (!CV.dead) lg("xr: cover: no readback this session: the HUD image %ux%u format %d (not 8-bit RGBA/BGRA)", hd->w,
                         hd->h, hd->fmt);
        CV.dead = 1;
        return;
    }
    if (cv_buf(ds)) return;
    int k = -1;
    for (int i = 0; i < CV_NE && k < 0; i++)
        if (!CV.e[i].sl) k = i;
    if (k < 0) return; /* all in flight: no reading this frame */
    VkBufferImageCopy r[SOMVR_COVER_N];
    for (int i = 0; i < SOMVR_COVER_N; i++) {
        int32_t x, y;
        somvr_cover_point(i, hd->w, hd->h, &x, &y);
        r[i] = (VkBufferImageCopy){.bufferOffset = ((VkDeviceSize)k * SOMVR_COVER_N + (VkDeviceSize)i) * 4,
                                   .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageOffset = {x, y, 0},
                                   .imageExtent = {1, 1, 1}};
    }
    ds->CmdCopyImageToBuffer(cmd, hd->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, CV.buf, SOMVR_COVER_N, r);
    VkMemoryBarrier hb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                          .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
    ds->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, NULL, 0, NULL);
    CV.e[k].sl = sl, CV.e[k].seq = ++CV.n_sub, CV.e[k].t = (double)vd_now() * 1e-9;
    CV.e[k].menu = (hd->kind & SOMVR_HUDF_MENU) != 0;
    cv_k_armed = k;
}
/* eyes_submit's end: its submit went (ok) or not (the entry is free again) */
static void cv_submitted(int ok) {
    if (cv_k_armed >= 0 && !ok) CV.e[cv_k_armed].sl = NULL;
    cv_k_armed = -1;
}
/* the wait after a mono stretch is over (why) */
static void cv_await_end(const char *why, double now) {
    CV.await = 0;
    if (CV.lines++ < 200)
        lg("xr: cover: the game's first stereo frames after a mono stretch: %s after %.2f s in the space", why,
           CV.await_t > 0 ? now - CV.await_t : 0.0);
}
/* xr_frame, every present before env_decide (mono: a mono present): the readings whose submits are done (their
 * fences), their age. After a mono stretch (a load's screen, the Nemesis window, a video) the game's first stereo frames
 * stay in the environment - the loading space over them, the frames copied and read under it - until a reading says no
 * cover (or the cover's load takes over; at most SOMVR_COVER_AWAIT_S, 0.3 s without a reading): a load that ends in
 * the game's cover never flashes the world between two spaces. */
static void cv_poll(dev_st *ds, int mono) {
    if (!CV.on) return;
    if (ds->GetFenceStatus)
        for (int i = 0; i < CV_NE; i++)
            if (CV.e[i].sl && ds->GetFenceStatus(ds->dev, CV.e[i].sl->fence) == VK_SUCCESS) cv_take(i);
    double now = (double)vd_now() * 1e-9;
    cv_changed(somvr_cover_age(&CV.st, now), now);
    if (mono) {
        CV.await = EV.on && X.hud_on && vm_hud_shown(), CV.await_t = 0, CV.await_n = CV.n_read;
        CV.placed = 1, CV.k = 0; /* the stretch's own place (the mono frames placed the space): kept through the wait */
    } else if (CV.await && CV.await_t > 0) { /* (from the first game frame drawn as the space: holds read nothing) */
        if (CV.n_read == CV.await_n && now - CV.await_t >= 0.3) cv_await_end("no reading", now);
        else if (now - CV.await_t >= SOMVR_COVER_AWAIT_S) cv_await_end("the HUD dark but no cover", now);
    }
    CV.n_hidden_frames += CV.st.hide;
}
/* xr_frame: a stereo present's HUD entry kind; a video is never the cover (all off at once; a game menu: its reading) */
static void cv_kind(uint32_t kind) {
    if (!CV.on || (kind & 0xffu) == SOMVR_HUDK_WORLD) return;
    double now = (double)vd_now() * 1e-9;
    CV.n_menu++;
    cv_changed(somvr_cover_step(&CV.st, now, 0, 0, 1, (float)CV.ms * 1e-3f), now);
    if (CV.await) cv_await_end("a video", now);
}
/* env_decide's switch to the environment for the cover's load: the space is up without a board (the holds back) */
static void cv_space_up(void) { KA.space_up = 1, CV.placed = 0, CV.k = 0; } /* (placed at its first frame) */
/* the HUD quad, the band and the wrist minimap are not shown (the cover) */
static int cv_hud_hidden(void) { return CV.on && CV.st.hide; }
/* env_decide: a stereo or reuse present wants the environment (the cover's load) */
static int cv_space_want(void) { return CV.on && EV.on && (CV.st.space || CV.await); }
/* xr_frame: this stereo or reuse present is drawn while env_decide shows the environment = the cover's load */
static int cv_space_shown(void) { return EV.on && EV.tr.started && EV.tr.shown == SOMVR_ENV_SHOW_ENV; }
/* ... its layers: the loading space - the sky, the floor, the lights (on through the load), our hands - and the fade */
static int cv_line(void) { return CV.on && CV.st.space && (CV.k == 20 || CV.k == 100); }
static void cv_space_comp(dev_st *ds, xr_comp *c) {
    CV.k++;
    if (CV.await && CV.await_t <= 0) CV.await_t = (double)vd_now() * 1e-9;
    if (CV.placed) /* placed once a load: the game's stereo frames go on under it (vd_space_place: once a stretch) */
        KA.placed_stereo = X.n_stereo + 1;
    vd_space_add(ds, c, env_scene(ds, c), 0, 1);
    KA.space_up = 1; /* without a board: env_decide holds it back to the world */
    if (!CV.placed && KA.placed_stereo == X.n_stereo + 1 && (CV.placed = 1, CV.lines++ < 200)) { /* (void_view's placed=) */
        float h[3];
        for (int i = 0; i < 3; i++) h[i] = 0.5f * (EV.head[0].p[i] + EV.head[1].p[i]);
        lg("xr: cover %u: the loading space placed for the load: the head at %+.3f,%+.3f,%+.3f yaw %+.1f deg, the floor "
           "at %+.2f m, the lights at %+.2f,%+.2f,%+.2f", CV.st.n_space, h[0], h[1], h[2],
           somvr_void_yaw(EV.head[0].q) * 57.29578f, KA.floor_q.pose.position.y, KA.lights_q.pose.position.x,
           KA.lights_q.pose.position.y, KA.lights_q.pose.position.z);
    }
    vd_hands(ds, c, 0);
    env_fade(ds);
    CV.n_space_frames++;
    if (mn_dump_frame && CV.lines++ < 200) /* (xr_compose.py would draw the world: tests/void_view.c draws this) */
        lg("xr: cover: dump frame %llu is the loading space's (its eye images are the world under it)",
           (unsigned long long)mn_dump_frame);
    if (CV.n_space_frames <= 2 || CV.n_space_frames % 600 == 0)
        lg("xr: cover: frame %llu of the loading space over the game's stereo frames (%u load(s) so far, the lights "
           "%.2f)", (unsigned long long)CV.n_space_frames, CV.st.n_space, (double)KA.level);
}

/* ==== The in-world menus' backdrop (SOMVR_XR_MENU_BACK=on; docs/re-xr-pipeline.md 36.8, handoff/HANDOFF-void.md
 * round 2). The pause menu, the map and their pages are stereo frames: SOMVR_XR_MENU=front's board 0.6 m ahead over
 * the live world, only a little darkened by the game - over a forge tower's white marble its text was unreadable (the
 * drill's finding 4). While a game menu shows on the board: a dark panel just behind it (the board's size and 6 cm more
 * on each side, its rim soft over 5 cm, rounded corners; ./somvr_void.h) and under that the world dimmed (the fade's
 * black ramp, head-locked as the fade: SOMVR_XR_MENU_BACK_DIM, 0.4), both rising over 0.12 s at the open and falling
 * over 0.15 s after the close (the panel where the board was). Slots XL_MBD (the dim) and XL_MBK (the panel), over the
 * world and its cues, under the board, the pointer, the wrist panel and our hands: touch and the laser as before. ==== */
static void mb_config(void) { /* vd_config */
    MB.on = somvr_void_parse_onoff(getenv("SOMVR_XR_MENU_BACK"), &MB.bad);
    MB.dim = somvr_back_parse_dim(getenv("SOMVR_XR_MENU_BACK_DIM"), SOMVR_BACK_DIM, &MB.dim_bad);
}
static void mb_session(void) { /* vd_start */
    if (MB.bad) lg("xr: menu backdrop: SOMVR_XR_MENU_BACK=%s is not on or off: off", getenv("SOMVR_XR_MENU_BACK"));
    if (MB.dim_bad)
        lg("xr: menu backdrop: SOMVR_XR_MENU_BACK_DIM=%s is not 0..0.9: %.2f", getenv("SOMVR_XR_MENU_BACK_DIM"),
           (double)MB.dim);
    MB.level = 0, MB.level_t = 0, MB.has = 0, MB.was_menu = 0, MB.n_frames = MB.n_opens = 0, MB.lines = 0;
    if (MB.on)
        lg("xr: menu backdrop: on: a dark panel %.0f cm beyond the menu board on each side (its rim soft over %.0f cm, "
           "alpha %.2f), the world dimmed %.0f %% behind it, while a game menu shows on the board over the world%s",
           100.0 * SOMVR_BACK_MARGIN_M, 100.0 * SOMVR_BACK_SOFT_M, (double)SOMVR_BACK_ALPHA, 100.0 * MB.dim,
           MN.menu ? "" : " - NEVER SHOWN: needs SOMVR_XR_MENU=front");
}
static void mb_summary(void) { /* vd_stop */
    if (MB.on) lg("xr: menu backdrop (the session's end): %llu menu open(s), %llu frame(s) with it", (unsigned long long)MB.n_opens,
                  (unsigned long long)MB.n_frames);
}
static void mb_destroy(void) { /* vd_stop: before the session */
    if (MB.sc) X.xrDestroySwapchain(MB.sc);
    MB.sc = XR_NULL_HANDLE;
    MB.filled = MB.dead = 0, MB.has = 0, MB.level = 0, MB.level_t = 0, MB.was_menu = 0;
}
/* xr_end_comp: the runtime rejected a frame with the backdrop's layers (ended again without them) */
static void mb_off(const char *why, const char *res) {
    if (MB.on) lg("xr: menu backdrop: %s: %s; off for this session", why, res);
    MB.on = 0, MB.has = 0;
}
static void mb_px(uint8_t *px, int bgra, int srgb, void *ctx) {
    const float *sz = ctx;
    somvr_back_image(px, bgra, srgb, sz[0], sz[1]);
}
/* the panel's image for its size (once a session: the board keeps its size); 0 = ready */
static int mb_fill(dev_st *ds) {
    if (MB.filled) return 0;
    if (MB.dead) return -1;
    MB.dead = 1;
    VkFormat f = VK_FORMAT_UNDEFINED;
    uint32_t n = 0;
    if (vd_static_image(ds, "menu backdrop", " (the board as before)", SOMVR_BACK_IMG_W, SOMVR_BACK_IMG_H, mb_px, MB.size,
                        &MB.sc, &MB.stat, &f, &n))
        return -1;
    MB.filled = 1, MB.dead = 0, MB.w_m = MB.size[0], MB.h_m = MB.size[1];
    lg("xr: menu backdrop: image %ux%u format %d (%s swapchain, %u image(s)) filled once: %d levels of a %.3f x %.3f m "
       "panel", SOMVR_BACK_IMG_W, SOMVR_BACK_IMG_H, f, MB.stat ? "a static" : "an ordinary", n, SOMVR_BACK_LEVELS,
       (double)MB.w_m, (double)MB.h_m);
    return 0;
}
/* a whole stereo frame, after hud_quad (X.last_quad: this frame's HUD quad): menu = it is shown on the board as a game
 * menu; eye = the frame's eyes (the dim's pose). The level steps; the panel and the dim for this frame and its reuse. */
static void mb_frame(dev_st *ds, int menu, const somvr_posef eye[2]) {
    MB.has = 0;
    if (!MB.on || !MN.menu) return;
    XrTime t = X.fs.predictedDisplayTime;
    float dt = MB.level_t && t > MB.level_t ? (float)((double)(t - MB.level_t) * 1e-9) : 0.0f;
    MB.level_t = t;
    if (menu) {
        somvr_posef b;
        memcpy(&b, &X.last_quad.pose, sizeof b);
        float bs[2] = {X.last_quad.size.width, X.last_quad.size.height};
        somvr_back_place(&b, bs, &MB.pose, MB.size);
        if (!MB.was_menu && ++MB.n_opens <= 50)
            lg("xr: menu backdrop %llu: a game menu on the board: the panel %.3f x %.3f m at %+.3f,%+.3f,%+.3f, the world "
               "dimmed %.0f %%", (unsigned long long)MB.n_opens, (double)MB.size[0], (double)MB.size[1], MB.pose.p[0],
               MB.pose.p[1], MB.pose.p[2], 100.0 * MB.dim);
    }
    MB.was_menu = menu;
    MB.level = somvr_back_level_step(MB.level, menu, dt);
    int k = somvr_back_index(MB.level);
    if (!k || mb_fill(ds)) return;
    int32_t rc[4];
    somvr_back_rect(k, rc);
    MB.q = (XrCompositionLayerQuad){.type = XR_TYPE_COMPOSITION_LAYER_QUAD,
                                    .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT, .space = X.space,
                                    .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
                                    .subImage = {MB.sc, {{rc[0], rc[1]}, {rc[2], rc[3]}}, 0},
                                    .size = {MB.size[0], MB.size[1]}};
    memcpy(&MB.q.pose, &MB.pose, sizeof MB.q.pose);
    MB.has = 1;
    int d = somvr_fade_index((uint32_t)lrintf(255.0f * MB.dim * MB.level));
    if (d && X.fade_on && !fade_fill(ds)) {
        somvr_posef p;
        somvr_fade_pose(eye, &p);
        somvr_fade_rect(d, rc);
        MB.dq = (XrCompositionLayerQuad){.type = XR_TYPE_COMPOSITION_LAYER_QUAD,
                                         .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
                                         .space = X.space, .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
                                         .subImage = {X.fade_sc, {{rc[0], rc[1]}, {rc[2], rc[3]}}, 0},
                                         .size = {SOMVR_FADE_SIZE_M, SOMVR_FADE_SIZE_M}};
        memcpy(&MB.dq.pose, &p, sizeof MB.dq.pose);
        MB.has |= 2;
    }
    MB.n_frames++;
    if (MB.n_frames <= 3 || MB.n_frames % 600 == 0)
        lg("xr: menu backdrop frame %llu: level %.2f (cell %d of %d), the panel %.3f x %.3f m at %+.3f,%+.3f,%+.3f; the "
           "world dimmed: %s", (unsigned long long)MB.n_frames, (double)MB.level, k, SOMVR_BACK_LEVELS, (double)MB.size[0],
           (double)MB.size[1], MB.pose.p[0], MB.pose.p[1], MB.pose.p[2], MB.has & 2 ? "the fade's ramp" : "no (the fade off)");
}
/* the last stereo frame's panel and dim into a composition (that frame, its reuse, the world's hold frames) */
static void mb_layers(xr_comp *c) {
    if (!MB.on) return;
    if (MB.has & 2) c->l[XL_MBD] = (const XrCompositionLayerBaseHeader *)&MB.dq;
    if (MB.has & 1) c->l[XL_MBK] = (const XrCompositionLayerBaseHeader *)&MB.q;
}

/* ---- the process's exit (build 16, SOMVR_XR_EXIT=on|off, default on) ------------------------------------------------
 *   The game quits with exit() - its menus' Quit game, and its SIGTERM handler (boot_check's stop, the VR menu's Quit) -
 *   without vkDestroyDevice: the process's exit handlers run while the watcher still ends frames, and the OpenXR
 *   loader's static destructor unloads the runtime's library. Build 15b: the watcher's next xrWaitFrame jumped into
 *   unmapped code (SIGSEGV in xr_wait_publish), Feral's crash reporter and its message box kept the game alive until the
 *   harness killed it (the user's Stop, or SIGKILL 30 s after a SIGTERM).
 *   xr_exit, an exit handler registered at the first session's start - after the loader's and the runtime's own, so it
 *   runs before them - takes X.mx (2 s at most), stops and joins the watcher, stops the preload's xrLocateViews
 *   (xr.state DEAD, its calls in flight waited for) and leaves every hook pass-through (X.ds NULL, phase dead). Nothing is
 *   destroyed (the game did not destroy its device; the runtime's objects end with the process, as build 9's clean
 *   exit). The runtime's library is pinned too (one more dlopen reference from its xrWaitFrame's address): a late call
 *   cannot jump into unmapped code. It logs "xr: exit: " (boot_check: the game is exiting). */
static struct {
    int on, bad, registered, pinned;
    char pin[320];
    _Atomic int exiting;
} XE;

static void xe_config(void) { /* vd_config */
    const char *p = getenv("SOMVR_XR_EXIT");
    XE.on = 1, XE.bad = 0;
    if (p && *p) {
        if (!strcmp(p, "off") || !strcmp(p, "0")) XE.on = 0;
        else if (strcmp(p, "on") && strcmp(p, "1")) XE.bad = 1;
    }
}

/* the runtime's library: one more reference (never released), unless its functions are the loader's own */
static void xe_pin(void) {
    Dl_info rt, ld;
    void *a = NULL, *g = NULL;
    memcpy(&a, &X.xrWaitFrame, sizeof a);
    memcpy(&g, &X.gipa, sizeof g);
    if (!a || !dladdr(a, &rt) || !rt.dli_fname) {
        snprintf(XE.pin, sizeof XE.pin, "not pinned (xrWaitFrame's library not found)");
    } else if (g && dladdr(g, &ld) && ld.dli_fbase == rt.dli_fbase) {
        snprintf(XE.pin, sizeof XE.pin, "xrWaitFrame is the loader's own code (%s; never unloaded)", rt.dli_fname);
    } else {
        void *h = dlopen(rt.dli_fname, RTLD_NOW | RTLD_NOLOAD);
        if (h) snprintf(XE.pin, sizeof XE.pin, "%s pinned (never unmapped while the process lives)", rt.dli_fname);
        else snprintf(XE.pin, sizeof XE.pin, "%s NOT pinned: %s", rt.dli_fname, dlerror());
    }
}

static void xr_exit(void) {
    if (atomic_exchange(&XE.exiting, 1)) return;
    int64_t t0 = vd_now();
    struct timespec dl;
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_sec += 2;
    int locked = pthread_mutex_timedlock(&X.mx, &dl) == 0; /* a frame in flight ends first */
    atomic_store(&VD.stop, 1);
    const char *w = "none running";
    if (VD.th_up && pthread_equal(pthread_self(), VD.th)) {
        w = "this thread (left as it is)";
    } else if (VD.th_up && locked) { /* it never waits for X.mx: at most its sleep */
        clock_gettime(CLOCK_REALTIME, &dl);
        dl.tv_sec += 1;
        if (pthread_timedjoin_np(VD.th, NULL, &dl) == 0) VD.th_up = 0, w = "stopped and joined";
        else w = "STILL RUNNING after 1 s (not joined)";
    } else if (VD.th_up) {
        for (int i = 0; i < 100 && !atomic_load(&VD.th_done); i++) {
            struct timespec sl = {0, 10000000};
            nanosleep(&sl, NULL);
        }
        w = atomic_load(&VD.th_done) ? "stopped (not joined: X.mx busy)" : "STILL RUNNING (X.mx busy)";
    }
    atomic_store(&KA.q, VK_NULL_HANDLE); /* the game's queue calls without the keep-alive's lock */
    int quiet = !X.sh || somvr_xr_stop(X.sh, SOMVR_XRS_DEAD, 1000);
    int was = X.phase;
    if (locked) {
        X.ds = NULL; /* every hook passes through: no frame, no new session, no teardown */
        X.phase = P_DEAD;
    }
    lg("xr: exit: the game is exiting (an exit handler, before the runtime is unloaded): the watcher %s; the preload's "
       "xrLocateViews %s; %s (phase %s, frames %llu); nothing destroyed (the process ends); the runtime: %s; %.0f ms",
       w, !X.sh ? "(no preload)" : quiet ? "stopped" : "STILL IN A CALL after 1 s",
       locked ? "the hooks pass through from now on" : "X.MX BUSY 2 s: the hooks left as they were", phase_name[was],
       (unsigned long long)X.frames, XE.pinned ? XE.pin : "not pinned", (double)(vd_now() - t0) / 1e6);
    if (locked) pthread_mutex_unlock(&X.mx);
}

/* vd_start (X.mx held): the first session's start registers xr_exit (after the loader and the runtime registered theirs:
 * it runs first) and pins the runtime */
static void xe_session(void) {
    if (XE.bad) lg("xr: exit: SOMVR_XR_EXIT=%s is not on or off: on", getenv("SOMVR_XR_EXIT"));
    if (!XE.on) {
        lg("xr: exit: off (SOMVR_XR_EXIT=off: build 15b - the watcher may call the runtime after it is unloaded)");
        return;
    }
    if (!XE.pinned) xe_pin(), XE.pinned = 1;
    if (!XE.registered) XE.registered = atexit(xr_exit) == 0 ? 1 : -1;
    lg("xr: exit: on: %s; the runtime: %s", XE.registered > 0 ? "at the game's exit our threads stop before the runtime "
       "is unloaded, the hooks pass through" : "atexit FAILED (no exit handler)", XE.pin);
}
