/* somvr_testhead_layer.c - SOMVR_XR_TEST `head X Y Z [s SEC]` (headless tests only; docs/re-xr-pipeline.md 41;
 * included by somvr_vklayer.c after the void's file). The tracked head moved X, Y, Z cm along the tracking space's
 * axes (Y up) from where the runtime has it, eased there in SEC seconds (default 0: at once): a duck (`head 0 -30 0
 * s 0.4`), a stand (`head 0 0 0 s 0.4`), a bob. Everything that locates the views sees it: the game's eyes (somvr_shared
 * xr.locate_views), the layer's own head (the HUD, the menus, the test hands' head frame). Without SOMVR_XR_TEST nothing
 * here is installed (xrLocateViews stays the runtime's). */

static struct {
    pthread_mutex_t mx;     /* a leaf: the game thread's locate and the test line's change */
    PFN_xrLocateViews real; /* the runtime's */
    float from[3], to[3];   /* cm */
    double t0, t1;          /* the move: from at t0, to at t1 (the display time, s) */
    uint64_t lines;
} TQ = {.mx = PTHREAD_MUTEX_INITIALIZER};

static void th_cur(double t, float o[3]) { /* TQ.mx held */
    double span = TQ.t1 - TQ.t0, k = span > 0 ? (t - TQ.t0) / span : 1.0;
    k = k < 0 ? 0 : k > 1 ? 1 : k;
    k = k * k * (3 - 2 * k); /* smoothstep: no speed step at either end */
    for (int i = 0; i < 3; i++) o[i] = TQ.from[i] + (float)k * (TQ.to[i] - TQ.from[i]);
}

static XRAPI_ATTR XrResult XRAPI_CALL th_locate_views(XrSession s, const XrViewLocateInfo *li, XrViewState *vs,
                                                       uint32_t cap, uint32_t *n, XrView *v) {
    XrResult r = TQ.real(s, li, vs, cap, n, v);
    if (XR_FAILED(r) || !v || !n || !li) return r;
    float o[3];
    pthread_mutex_lock(&TQ.mx);
    th_cur((double)li->displayTime * 1e-9, o);
    pthread_mutex_unlock(&TQ.mx);
    for (uint32_t i = 0; i < *n && i < cap; i++) {
        v[i].pose.position.x += 0.01f * o[0];
        v[i].pose.position.y += 0.01f * o[1];
        v[i].pose.position.z += 0.01f * o[2];
    }
    return r;
}

/* after the XR functions are loaded (each instance): the views through th_locate_views when SOMVR_XR_TEST is set */
static void th_wrap(void) {
    const char *p = getenv("SOMVR_XR_TEST");
    if (!p || !*p || !X.xrLocateViews) return;
    if (X.xrLocateViews != th_locate_views) TQ.real = X.xrLocateViews;
    X.xrLocateViews = th_locate_views;
}

/* the menus' xt_run (X.mx held): 1 = the line was a `head` line */
static int th_xt(const char *line) {
    float c[3], sec = 0;
    int n = 0;
    if (sscanf(line, " head %f %f %f%n", &c[0], &c[1], &c[2], &n) != 3) return 0;
    const char *p = line + n;
    char tail = 0;
    if (sscanf(p, " s %f %c", &sec, &tail) == 1 || sscanf(p, " %c", &tail) != 1) {
        int ok = TQ.real && fabsf(c[0]) <= 200 && fabsf(c[1]) <= 200 && fabsf(c[2]) <= 200 && sec >= 0 && sec <= 30;
        if (ok) {
            double t = (double)X.fs.predictedDisplayTime * 1e-9;
            pthread_mutex_lock(&TQ.mx);
            th_cur(t, TQ.from);
            memcpy(TQ.to, c, sizeof TQ.to);
            TQ.t0 = t, TQ.t1 = t + sec;
            pthread_mutex_unlock(&TQ.mx);
            TQ.lines++;
        }
        lg("xr: test: head %+.1f %+.1f %+.1f cm in %.2f s: %s (head line %llu)", c[0], c[1], c[2], sec,
           ok ? "the views move there" : TQ.real ? "not -200..200 cm, 0..30 s" : "no views yet",
           (unsigned long long)TQ.lines);
        return 1;
    }
    lg("xr: test: head: not `head X Y Z [s SEC]`");
    return 1;
}
