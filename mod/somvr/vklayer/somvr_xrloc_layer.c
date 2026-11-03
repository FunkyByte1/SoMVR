/* somvr_xrloc_layer.c - the locate thread for the PE DLL (../somvr_xrloc.h, docs/re-winport.md 3.1): included by
 * somvr_vklayer.c. Runs only with a PE bridge, from the session's RUNNING (after somvr_shared and somvr_input are
 * published) to xr_unpublish (joined there). Every 1 ms: the newest predicted display time (somvr_shared's tseq block)
 * t0 and t1 = t0 + one display period; xrLocateViews and the four action spaces (with velocities) at both; published
 * under somvr_xrloc.seq. Every call is inside the provider guard (somvr_xr_enter), like the game thread's on Linux. */

static struct {
    pthread_t th;
    int on;
    _Atomic int stop;
    somvr_xrloc *x;
    uint64_t ticks_logged;
} XL;

static void xl_one(somvr_xrloc *x, int64_t t, somvr_xrloc_sample *s) {
    memset(s, 0, sizeof *s);
    s->time = t;
    XrViewLocateInfo li = {.type = XR_TYPE_VIEW_LOCATE_INFO,
                           .viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                           .displayTime = t,
                           .space = (XrSpace)(uintptr_t)x->base_space};
    XrViewState vs = {.type = XR_TYPE_VIEW_STATE};
    XrView v[2] = {{.type = XR_TYPE_VIEW}, {.type = XR_TYPE_VIEW}};
    uint32_t n = 0;
    XrResult r = X.xrLocateViews(X.session, &li, &vs, 2, &n, v);
    s->result = (int32_t)r, s->view_flags = (uint32_t)vs.viewStateFlags, s->n = n;
    if (XR_FAILED(r)) atomic_fetch_add_explicit(&x->views_fail, 1, memory_order_relaxed);
    for (uint32_t e = 0; e < n && e < 2; e++) {
        memcpy(&s->pose[e], &v[e].pose, sizeof s->pose[e]);
        memcpy(&s->fov[e], &v[e].fov, sizeof s->fov[e]);
    }
    for (int k = 0; k < SOMVR_XRLOC_NSPACE; k++) {
        somvr_xrloc_space *o = &s->space[k];
        if (!x->space[k]) {
            o->result = XR_ERROR_HANDLE_INVALID;
            continue;
        }
        XrSpaceVelocity vel = {.type = XR_TYPE_SPACE_VELOCITY};
        XrSpaceLocation l = {.type = XR_TYPE_SPACE_LOCATION, .next = &vel};
        r = X.xrLocateSpace((XrSpace)(uintptr_t)x->space[k], (XrSpace)(uintptr_t)x->base_space, t, &l);
        o->result = (int32_t)r;
        if (XR_FAILED(r)) {
            atomic_fetch_add_explicit(&x->space_fail, 1, memory_order_relaxed);
            continue;
        }
        o->flags = (uint32_t)l.locationFlags, o->vflags = (uint32_t)vel.velocityFlags;
        memcpy(&o->pose, &l.pose, sizeof o->pose);
        memcpy(o->lin, &vel.linearVelocity, 12);
        memcpy(o->ang, &vel.angularVelocity, 12);
    }
}

static void *xl_main(void *arg) {
    somvr_xrloc *x = arg;
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    atomic_store_explicit(&x->running, 1, memory_order_release);
    while (!atomic_load_explicit(&XL.stop, memory_order_acquire)) {
        somvr_time tm;
        if (X.sh && somvr_xr_enter(X.sh)) {
            if (somvr_time_read(X.sh, &tm) && tm.t > 0) {
                somvr_xrloc_sample s[SOMVR_XRLOC_NSAMPLE];
                xl_one(x, tm.t, &s[0]);
                xl_one(x, tm.t + (tm.period > 0 ? tm.period : 11111111), &s[1]);
                somvr_seq_wbegin(&x->seq);
                memcpy((void *)x->sample, s, sizeof s);
                somvr_seq_wend(&x->seq);
            }
            somvr_xr_leave(X.sh);
        }
        atomic_fetch_add_explicit(&x->ticks, 1, memory_order_relaxed);
        next.tv_nsec += 1000000;
        if (next.tv_nsec >= 1000000000) next.tv_sec++, next.tv_nsec -= 1000000000;
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }
    atomic_store_explicit(&x->running, 0, memory_order_release);
    return NULL;
}

/* SOMVR_SA_BEGIN, after somvr_shared's state = RUNNING and in_begin (the action spaces published) */
static void xl_start(void) {
    if (XL.on || !br_pe()) return;
    somvr_xrloc *x = (somvr_xrloc *)br_get(SOMVR_XRLOC_SYMBOL);
    if (!somvr_xrloc_ok(x)) {
        lg("xr: locate thread: no valid somvr_xrloc in the bridge (%p): the DLL's views fail (flat)", (void *)x);
        return;
    }
    x->session = (uint64_t)(uintptr_t)X.session, x->base_space = X.sh ? X.sh->xr.space : 0;
    for (int h = 0; h < 2; h++) {
        x->space[h] = I.in ? I.in->xr.grip_space[h] : 0;
        x->space[2 + h] = I.in ? I.in->xr.aim_space[h] : 0;
    }
    x->period_us = 1000;
    atomic_store(&XL.stop, 0);
    XL.x = x;
    if (pthread_create(&XL.th, NULL, xl_main, x) != 0) {
        lg("xr: locate thread: pthread_create failed: %s", strerror(errno));
        return;
    }
    pthread_setname_np(XL.th, "somvr-xrloc");
    XL.on = 1;
    lg("xr: locate thread on (1 ms): views + grip/aim L/R at the predicted time and one period later into somvr_xrloc "
       "%p (spaces %#llx %#llx %#llx %#llx)", (void *)x, (unsigned long long)x->space[0], (unsigned long long)x->space[1],
       (unsigned long long)x->space[2], (unsigned long long)x->space[3]);
}

static void xl_stop(void) {
    if (!XL.on) return;
    atomic_store(&XL.stop, 1);
    pthread_join(XL.th, NULL);
    XL.on = 0;
    somvr_xrloc *x = XL.x;
    lg("xr: locate thread off: %llu ticks, views failed %llu, spaces failed %llu; the DLL answered %llu views, %llu "
       "spaces (%llu stale or none, %llu unknown spaces)", (unsigned long long)atomic_load(&x->ticks),
       (unsigned long long)atomic_load(&x->views_fail), (unsigned long long)atomic_load(&x->space_fail),
       (unsigned long long)atomic_load(&x->n_views), (unsigned long long)atomic_load(&x->n_spaces),
       (unsigned long long)atomic_load(&x->n_stale), (unsigned long long)atomic_load(&x->n_nospace));
}
