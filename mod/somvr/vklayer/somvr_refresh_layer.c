/* somvr_refresh_layer.c - SOMVR_XR_REFRESH: the headset's refresh rate for the session (build 27, som-b27-perf2,
 * docs/re-xr-pipeline.md 119). Included by somvr_vklayer.c (X, VM, lg, xr_str).
 *   The render thread does 10-13 ms of work a stereo frame (re-xr-pipeline.md 115): over 90 Hz's 11.1 ms in the heavy
 *   places, a dropped frame = judder. A steady lower rate beats a 90 that drops. XR_FB_display_refresh_rate (WiVRn and
 *   Monado have it) is the session's own: the runtime's config is never touched; the next app gets its own rate.
 *   xrCreateInstance: the extension enabled when the runtime offers it (not with SOMVR_XR_REFRESH=off).
 *   The session running: the rates the runtime lists and its current rate logged, then the rate asked: the highest
 *     listed rate at or under it (none under it: the lowest listed; none listed: the rate itself).
 *   Every present: the VR menu's Refresh rate row (somvr_settings.refresh, its XR_REFRESH key in the settings file,
 *     the preload's SOMVR_XR_REFRESH under it) asked again when it changes - live. No menu: the switch's rate.
 *   XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB: logged (from -> to). A request's failure: logged, not again
 *     for that rate (the next change asks).
 * SOMVR_XR_REFRESH: off = the extension never enabled (build 26's session); 0 (the code default) = enabled, nothing
 *   asked (the runtime's own rate; the menu's row may ask); 60..144 = that rate asked at the session's start.
 * Lines: `xr: refresh: ...`. Tests: vklayer/tests/test_layer.c `refresh*` (the mock runtime's MOCKXR_REFRESH). */

static struct {
    int off, bad;            /* SOMVR_XR_REFRESH=off; not understood */
    uint32_t env_hz;         /* its rate, 0 = nothing asked */
    int offered, enabled;    /* the runtime lists the extension; the instance has it (and its functions) */
    PFN_xrEnumerateDisplayRefreshRatesFB enumerate;
    PFN_xrGetDisplayRefreshRateFB get;
    PFN_xrRequestDisplayRefreshRateFB request;
    float rates[16];
    uint32_t n;
    uint32_t asked;          /* the rate (Hz) last acted on this session, 0 = none yet */
    float now;               /* the display's rate (xrGetDisplayRefreshRateFB, the events) */
    uint32_t events, requests, fails;
} RR;

static void rr_config(void) {
    const char *e = getenv("SOMVR_XR_REFRESH");
    RR.off = 0, RR.bad = 0, RR.env_hz = 0;
    if (!e || !*e) return;
    char *end = NULL;
    double d = strtod(e, &end);
    if (!strcmp(e, "off")) RR.off = 1;
    else if (end && end != e && !*end && d >= 0 && (d < 0.5 || (d >= 60 && d <= 144))) RR.env_hz = (uint32_t)lrint(d);
    else RR.bad = 1;
    if (RR.bad)
        lg("WARN xr: refresh: SOMVR_XR_REFRESH=%s not understood (off, 0, or a rate 60..144 Hz): nothing asked", e);
}
/* the helper thread, with the runtime's instance extensions */
static void rr_offered(const XrExtensionProperties *props, uint32_t n) {
    RR.offered = 0;
    for (uint32_t i = 0; props && i < n; i++)
        RR.offered |= !strcmp(props[i].extensionName, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
}
static int rr_want_ext(void) { return RR.offered && !RR.off; }
/* the helper thread, after xrCreateInstance (enabled = the extension was in it) */
static void rr_loaded(int enabled) {
    RR.enabled = 0;
    if (!enabled) {
        lg("xr: refresh: %s", RR.off       ? "SOMVR_XR_REFRESH=off: XR_FB_display_refresh_rate not enabled (the runtime's rate)"
                              : !RR.offered ? "the runtime does not offer XR_FB_display_refresh_rate: its own rate"
                                            : "XR_FB_display_refresh_rate refused by xrCreateInstance: the runtime's rate");
        return;
    }
    if (XR_FAILED(X.gipa(X.inst, "xrEnumerateDisplayRefreshRatesFB", (PFN_xrVoidFunction *)&RR.enumerate)) ||
        XR_FAILED(X.gipa(X.inst, "xrGetDisplayRefreshRateFB", (PFN_xrVoidFunction *)&RR.get)) ||
        XR_FAILED(X.gipa(X.inst, "xrRequestDisplayRefreshRateFB", (PFN_xrVoidFunction *)&RR.request)) ||
        !RR.enumerate || !RR.get || !RR.request) {
        lg("xr: refresh: XR_FB_display_refresh_rate enabled but its functions are missing: the runtime's rate");
        return;
    }
    RR.enabled = 1;
    lg("xr: refresh: XR_FB_display_refresh_rate enabled; SOMVR_XR_REFRESH=%u: %s", RR.env_hz,
       RR.env_hz ? "asked at the session's start (the VR menu's Refresh rate over it)" : "nothing asked unless the VR menu does");
}
/* the VR menu's row is shown (the layer's own avail bit) */
static int rr_menu(void) { return RR.enabled; }

static uint32_t rr_want(void) { return VM.obj ? VM.set.refresh : RR.env_hz; }

/* the listed rate for hz: the highest at or under it, else the lowest; none listed: hz */
static float rr_pick(uint32_t hz) {
    float best = 0, lo = 0;
    for (uint32_t i = 0; i < RR.n; i++) {
        float r = RR.rates[i];
        if (r <= (float)hz + 0.5f && r > best) best = r;
        if (!(lo > 0) || r < lo) lo = r;
    }
    return best > 0 ? best : lo > 0 ? lo : (float)hz;
}
static void rr_list(char *buf, size_t cap) {
    size_t u = 0;
    buf[0] = 0;
    for (uint32_t i = 0; i < RR.n && u + 1 < cap; i++)
        u += (size_t)snprintf(buf + u, cap - u, "%s%.1f", i ? ", " : "", (double)RR.rates[i]);
    if (!RR.n) snprintf(buf, cap, "none");
}
static void rr_ask(uint32_t hz, const char *why) {
    if (!RR.enabled || !X.running || !hz || hz == RR.asked) return;
    RR.asked = hz;
    float r = rr_pick(hz), before = 0;
    char l[160];
    rr_list(l, sizeof l);
    XrResult g = RR.get(X.session, &before);
    XrResult q = RR.request(X.session, r);
    RR.requests++;
    if (XR_FAILED(q)) RR.fails++;
    float after = 0;
    XrResult g2 = RR.get(X.session, &after);
    if (XR_SUCCEEDED(g2)) RR.now = after;
    lg("xr: refresh: %u Hz asked (%s): %.1f Hz requested (listed: %s): %s (%d); the display %.1f Hz before, %.1f after",
       hz, why, (double)r, l, xr_str(q), q, XR_SUCCEEDED(g) ? (double)before : -1.0, XR_SUCCEEDED(g2) ? (double)after : -1.0);
}
/* the session running (after xrBeginSession) */
static void rr_begin(void) {
    if (!RR.enabled) return;
    RR.asked = 0, RR.n = 0;
    uint32_t n = 0;
    XrResult r = RR.enumerate(X.session, 0, &n, NULL);
    if (XR_SUCCEEDED(r) && n) {
        if (n > 16) n = 16;
        r = RR.enumerate(X.session, n, &n, RR.rates);
        RR.n = XR_SUCCEEDED(r) ? (n < 16 ? n : 16) : 0;
    }
    float now = 0;
    XrResult g = RR.get(X.session, &now);
    if (XR_SUCCEEDED(g)) RR.now = now;
    char l[160];
    rr_list(l, sizeof l);
    lg("xr: refresh: the runtime lists %s Hz (%s); the display %.1f Hz (%s)", l, xr_str(r), XR_SUCCEEDED(g) ? (double)now : -1.0,
       xr_str(g));
    rr_ask(rr_want(), VM.obj ? "the session's start, the VR menu's setting" : "the session's start, SOMVR_XR_REFRESH");
}
static void rr_end(void) { RR.asked = 0; }
/* every present while the session runs: the VR menu's row changed */
static void rr_frame(void) {
    if (RR.enabled && X.running && VM.obj && VM.set.refresh && VM.set.refresh != RR.asked) rr_ask(VM.set.refresh, "the VR menu");
}
/* xrPollEvent's XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB; 1 = it was one */
static int rr_event(const XrEventDataBuffer *ev) {
    if (ev->type != XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB) return 0;
    const XrEventDataDisplayRefreshRateChangedFB *c = (const XrEventDataDisplayRefreshRateChangedFB *)ev;
    RR.events++;
    RR.now = c->toDisplayRefreshRate;
    lg("xr: refresh: the display's rate changed %.1f -> %.1f Hz (event %u)", (double)c->fromDisplayRefreshRate,
       (double)c->toDisplayRefreshRate, RR.events);
    return 1;
}
