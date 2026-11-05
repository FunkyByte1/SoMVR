/* somvr_live_layer.c - SOMVR_LIVE=on (docs/re-xr-pipeline.md 162, ../somvr_live.h): the live loop's card on the off
 * hand's palm and the VR menu's patch box. Included by somvr_vklayer.c after the menus' file; X.mx held in the frame
 * functions (the present thread), the folder read and written by LV's own thread.
 *
 * SOMVR_LIVE=on with SOMVR_LIVE_DIR=DIR (scripts/live_session.sh's folder; the session sets both) and the VR menu:
 *   - the thread (every 0.5 s): DIR/say/<n>.txt (scripts/live_say.sh) -> a message card; DIR/patches/ready and its
 *     patches/<id>/patch.txt (scripts/live_patch.sh) -> a "Patch N ready" card and the Test page's patch box;
 *     DIR/patches/state.txt (the game side's answer to a request) -> a result card, the box again. It writes the
 *     player's acts: say/<n>.shown and .read, patches/request.txt ("apply ID SEQ" / "undo ID SEQ").
 *   - the frame (vm_frame; b37 live3, re-xr-pipeline.md 162.10): a card waits -> a small amber dot on the BACK of the
 *     off hand (only while the back faces the eyes); at a calm moment (somvr_lv_calm: 3 s without the bow, a climb hold,
 *     a swing, an attack incoming or an attack at Talion lately - SOMVR_VMG_FIGHT) with its spoken line ready the card
 *     goes out BY ITSELF, world-locked beside the off hand (somvr_lv_card_place: placed once, still after), with a NUDGE
 *     in that hand and the line said; it stays 8 s (and while its line is said; 30 s at most) or until tapped (the main
 *     hand's fingertip, SOMVR_XR_TOUCH; without touch its trigger pointed at the card): CLICK, the next card 1 s later.
 *     A fight or the menu puts it away (out again, placed anew, at the next calm moment). The VR menu keeps its palm
 *     gesture (build 35-37 showed the card on the raised palm and held the menu's gesture meanwhile: the player kept
 *     opening the menu instead). The card and the dot use the menu's quad slots (XL_VM0) - never both with the panel.
 *   - the patch box's Apply patch / Undo patch (vm_click) -> patches/request.txt; nothing swaps without that press.
 * Logged: "xr: live: ..." (setup, each card's arrival, shown, read; each press). Any failure: no card for the session,
 * logged once; the frames go on. */
#include "../somvr_live.h"
#include <sys/stat.h>

#define LV_Q 16
typedef struct {
    char key[24];        /* "say:5", "patch:3", "state:7" */
    int say;             /* say/<n> (0: not a message) */
    somvr_lv_card card;
    int voiced;          /* b37: its spoken line is ready (say/<key>.wav), or none is coming: the nudge may come */
    int spoken;          /* b37 live3: its line said (say/<key>.spoken) or none coming: the card may go after 8 s */
    double t_in;         /* b37: its arrival (the thread's monotonic clock) */
} lv_item;
static double lv_mono(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}
#define LV_VOICE_WAIT_S 8.0 /* b37: the nudge waits at most this long for the card's spoken line */
static struct {
    int on;
    char dir[512];
    somvr_font font;
    somvr_lv_ccfg ccfg;
    somvr_lv_calm calm;
    /* the swapchain (one picture: the front card and the dot) */
    XrSwapchain sc;
    uint32_t nimg;
    VkImage img[MAX_XR_IMAGES];
    int bgra;
    dev_st *ds;
    VkBuffer buf;
    VkDeviceMemory mem;
    void *map;
    VkCommandBuffer cmd;
    VkFence fence;
    int pending, released;
    char drawn_key[24];  /* the card in the released picture */
    /* the thread's (mx): the cards waiting, the box, the requests to write */
    pthread_mutex_t mx;
    _Atomic int stop, running;
    lv_item q[LV_Q];
    int nq;
    somvr_vm_live box;   /* the Test page's patch box (the frame copies it when its gen moves) */
    int ready_id, ready_live, current, last_req_id;
    uint32_t state_seq, req_seq, req_written;
    char req[64];        /* the request to write ("" = none) */
    int say_next;        /* the next say/<n> to look for */
    char marks[8][40];   /* say/<n>.shown / .read to write (b37: also <key>.calm, the card's first calm moment) */
    int nmarks;
    /* b37 (re-xr-pipeline.md 162.8): each card's spoken line, written as say/<key>.line when it arrives (the daemon
     * synthesises it at once) - the thread's own (lv_push runs only in lv_poll) */
    char sp_key[8][24];
    char sp_text[8][SOMVR_LV_SPEECH_MAX];
    int nsp;
    int tts;             /* b37: SOMVR_LIVE_TTS=on - a card's nudge waits for its spoken line (LV_VOICE_WAIT_S) */
    /* the frame's */
    uint32_t box_gen;
    int showing;         /* 1 card, 2 dot, 0 nothing (this frame) */
    int in_vm0;          /* this frame's XL_VM0 is ours */
    int nudged_for;      /* the card nudged for (its q[0] key's hash) */
    double shown_t;
    char shown_key[24];
    /* b37 live3 (162.10): the card out by itself, world-locked beside the off hand - since out_t (this showing), its
     * place; the next one not before next_t; the times it was put away for a fight */
    int out;
    double out_t, next_t;
    somvr_posef out_pose;
    uint64_t n_out, n_timeout, n_away, n_dot_hidden;
    int dot_on;                   /* this frame's dot (its own slot XL_LDOT, over our hands) */
    XrCompositionLayerQuad dot_q;
    somvr_tc_state tc;
    somvr_tc_out tco;
    int trig_was;
    XrCompositionLayerQuad quad;
    somvr_posef card_pose;
    uint64_t n_cards, n_shown, n_read, n_dot_frames, n_card_frames, n_held, n_press, n_uploads;
    uint32_t lines;
} LV = {.mx = PTHREAD_MUTEX_INITIALIZER};

static uint32_t lv_hash(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
    return h ? h : 1;
}
static int lv_read(const char *path, char *buf, size_t cap) { /* the file into buf (0-terminated); its length or -1 */
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = 0;
    return (int)n;
}
static int lv_write(const char *path, const char *s) { /* atomically (PATH.tmp, renamed); 0 = written */
    char tmp[1300];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    int ok = fputs(s, f) >= 0;
    ok &= fclose(f) == 0;
    return ok && rename(tmp, path) == 0 ? 0 : -1;
}
static void lv_wall(char *out, size_t cap) {
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(out, cap, "%Y-%m-%d %H:%M:%S", &tm);
}
static void lv_push(const char *key, int say, const somvr_lv_card *c) { /* LV.mx held */
    if (LV.nq >= LV_Q) { /* the oldest goes (a flood of messages: the newest matter) */
        memmove(&LV.q[0], &LV.q[1], sizeof LV.q[0] * (LV_Q - 1));
        LV.nq--;
    }
    lv_item *it = &LV.q[LV.nq++];
    snprintf(it->key, sizeof it->key, "%s", key);
    it->say = say, it->card = *c;
    it->voiced = !LV.tts || !c->speech[0], it->t_in = lv_mono();
    it->spoken = it->voiced; /* (no line coming: nothing to wait for) */
    LV.n_cards++;
    if (c->speech[0] && LV.nsp < 8) { /* b37: its spoken line for the daemon (say/<key>.line) */
        snprintf(LV.sp_key[LV.nsp], sizeof LV.sp_key[0], "%s", key);
        snprintf(LV.sp_text[LV.nsp], sizeof LV.sp_text[0], "%s", c->speech);
        LV.nsp++;
    }
}
/* b37: a card's key as a file name part ("say:3" -> "say-3") */
static void lv_fkey(const char *key, char *out, size_t cap) {
    snprintf(out, cap, "%s", key);
    for (char *q = out; *q; q++)
        if (*q == ':' || *q == '/') *q = '-';
}
static void lv_box_patch(const somvr_lv_patch *p) { /* LV.mx held: the box from the patch waiting and the state */
    somvr_vm_live b = LV.box;
    b.on = 1;
    if (p && p->id) {
        if (LV.current == p->id) snprintf(b.line[0], sizeof b.line[0], "Patch %d is in", p->id);
        else if (p->live) snprintf(b.line[0], sizeof b.line[0], "Patch %d ready", p->id);
        else snprintf(b.line[0], sizeof b.line[0], "Patch %d: restart", p->id);
        char w[2][SOMVR_LV_COLS + 1];
        int more = 0;
        if (somvr_lv_wrap(p->what, w, 1, SOMVR_VM_TEST_COLS, &more) == 1) snprintf(b.line[1], sizeof b.line[1], "%s", w[0]);
        char t[200];
        snprintf(t, sizeof t, "Try: %s", p->try_);
        if (somvr_lv_wrap(p->try_[0] ? t : "", w, 1, SOMVR_VM_TEST_COLS, &more) == 1)
            snprintf(b.line[2], sizeof b.line[2], "%s", w[0]);
        else b.line[2][0] = 0;
    } else if (!b.line[0][0]) {
        snprintf(b.line[0], sizeof b.line[0], "No patch waiting");
    }
    b.can_apply = LV.ready_id > 0 && LV.ready_live && LV.ready_id != LV.current;
    b.can_undo = LV.current > 0;
    if (memcmp(&b, &LV.box, sizeof b)) b.gen = LV.box.gen + 1, LV.box = b;
}

/* ---- the thread: the folder every 0.5 s --------------------------------------------------------------------- */
static void lv_poll(void) {
    char path[1200], buf[2048];
    /* the messages, in order */
    for (int k = 0; k < 8; k++) {
        snprintf(path, sizeof path, "%s/say/%d.txt", LV.dir, LV.say_next);
        int n = lv_read(path, buf, SOMVR_LV_TEXT_MAX + 1);
        if (n < 0) break;
        char title[32], key[24];
        snprintf(title, sizeof title, "Message %d", LV.say_next);
        snprintf(key, sizeof key, "say:%d", LV.say_next);
        somvr_lv_card c;
        somvr_lv_card_make(&c, SOMVR_LVK_SAY, title, buf, "Tap to close");
        char qp[1200];
        struct stat qs;
        snprintf(qp, sizeof qp, "%s/say/%d.quiet", LV.dir, LV.say_next);
        if (stat(qp, &qs) == 0) c.speech[0] = 0; /* b37 live3: a quiet card (a page after the first): no line coming */
        pthread_mutex_lock(&LV.mx);
        lv_push(key, LV.say_next, &c);
        pthread_mutex_unlock(&LV.mx);
        lg("xr: live: message %d arrived (%d bytes, %d lines%s): '%.80s'", LV.say_next, n, c.n, c.more ? ", cut" : "", buf);
        LV.say_next++;
    }
    /* the patch waiting */
    somvr_lv_patch p;
    memset(&p, 0, sizeof p);
    snprintf(path, sizeof path, "%s/patches/ready", LV.dir);
    int rid = lv_read(path, buf, 32) > 0 ? atoi(buf) : 0;
    if (rid > 0) {
        snprintf(path, sizeof path, "%s/patches/%d/patch.txt", LV.dir, rid);
        int n = lv_read(path, buf, sizeof buf);
        if (n <= 0 || !somvr_lv_patch_parse(buf, (size_t)n, &p) || p.id != rid) rid = 0, memset(&p, 0, sizeof p);
    }
    pthread_mutex_lock(&LV.mx);
    if (rid > 0 && rid != LV.ready_id) {
        LV.ready_id = rid, LV.ready_live = p.live;
        char title[40], text[500], key[24];
        if (p.live) {
            snprintf(title, sizeof title, "Patch %d ready", rid);
            snprintf(text, sizeof text, "%s\n%s%s\nApply it: menu > Test tab", p.what, p.try_[0] ? "Try: " : "", p.try_);
        } else {
            snprintf(title, sizeof title, "Patch %d: after a restart", rid);
            snprintf(text, sizeof text, "%s\n%s", p.what, p.why[0] ? p.why : "It changes the layer or the host.");
        }
        snprintf(key, sizeof key, "patch:%d", rid);
        somvr_lv_card c;
        somvr_lv_card_make(&c, SOMVR_LVK_PATCH, title, text, "Tap to close");
        if (c.more && p.live) { /* b37 live3: too long for one card (the 22:13 session: cut at the bottom) - two pages,
                                  * what changed / what to try; the voice says only "Patch N ready: what changed" */
            somvr_lv_card c2;
            char t1[40], t2[40], key2[24], sp[200];
            snprintf(t1, sizeof t1, "Patch %d ready (1 of 2)", rid);
            snprintf(t2, sizeof t2, "Patch %d (2 of 2)", rid);
            somvr_lv_card_make(&c, SOMVR_LVK_PATCH, t1, p.what, "Tap for page 2");
            snprintf(text, sizeof text, "%s%s", p.try_[0] ? "Try: " : "", p.try_);
            somvr_lv_card_make(&c2, SOMVR_LVK_PATCH, t2, text, "Apply: menu > Test tab");
            snprintf(sp, sizeof sp, "Patch %d ready: %s", rid, p.what);
            somvr_lv_speech(SOMVR_LVK_SAY, NULL, sp, c.speech, sizeof c.speech);
            c2.speech[0] = 0; /* (quiet: the first page's line is the patch's) */
            snprintf(key2, sizeof key2, "patch:%d:2", rid);
            lv_push(key, 0, &c);
            lv_push(key2, 0, &c2);
            lg("xr: live: patch %d's card in 2 pages (%d + %d lines)", rid, c.n, c2.n);
        } else {
            lv_push(key, 0, &c);
        }
        lg("xr: live: patch %d staged (%s): '%.80s' / try '%.60s'", rid, p.live ? "live" : "after a restart", p.what, p.try_);
    }
    if (rid > 0 || !LV.box.on || !LV.box.line[0][0]) lv_box_patch(rid > 0 ? &p : NULL);
    pthread_mutex_unlock(&LV.mx);
    /* the game side's answer */
    snprintf(path, sizeof path, "%s/patches/state.txt", LV.dir);
    int n = lv_read(path, buf, sizeof buf);
    somvr_lv_state s;
    if (n > 0 && somvr_lv_state_parse(buf, (size_t)n, &s) && s.seq != LV.state_seq) {
        pthread_mutex_lock(&LV.mx);
        LV.state_seq = s.seq, LV.current = s.current;
        somvr_lv_card c;
        char key[24];
        snprintf(key, sizeof key, "state:%u", s.seq);
        if (somvr_lv_result_card(&s, rid == s.id ? &p : NULL, &c)) lv_push(key, 0, &c);
        if (LV.req_written == s.seq) LV.req[0] = 0; /* answered */
        lv_box_patch(rid > 0 ? &p : NULL);
        pthread_mutex_unlock(&LV.mx);
        lg("xr: live: the game's answer %u: %s patch %d (now %d live): %.100s", s.seq, s.last, s.id, s.current, s.msg);
    }
    /* b37: the cards whose spoken line is now ready (or will not come) */
    if (LV.tts) {
        double now = lv_mono();
        pthread_mutex_lock(&LV.mx);
        for (int i = 0; i < LV.nq; i++) {
            lv_item *it = &LV.q[i];
            if (it->voiced) continue;
            char fk[24];
            struct stat sb;
            lv_fkey(it->key, fk, sizeof fk);
            snprintf(path, sizeof path, "%s/say/%s.wav", LV.dir, fk);
            int have = stat(path, &sb) == 0;
            if (have || now - it->t_in > LV_VOICE_WAIT_S) {
                it->voiced = 1;
                if (!have) it->spoken = 1; /* b37 live3: no line: nothing to wait for */
                if (LV.lines++ < 300)
                    lg("xr: live: card %s: %s %.1f s after it arrived", it->key, have ? "its spoken line ready" :
                       "no spoken line yet (the nudge goes without it)", now - it->t_in);
            }
        }
        pthread_mutex_unlock(&LV.mx);
    }
    /* b37 live3: the front card's line said (say/<key>.spoken): the card may go after its 8 s */
    if (LV.tts) {
        pthread_mutex_lock(&LV.mx);
        if (LV.nq > 0 && !LV.q[0].spoken) {
            char fk[24];
            struct stat sb;
            lv_fkey(LV.q[0].key, fk, sizeof fk);
            snprintf(path, sizeof path, "%s/say/%s.spoken", LV.dir, fk);
            if (stat(path, &sb) == 0) LV.q[0].spoken = 1;
        }
        pthread_mutex_unlock(&LV.mx);
    }
    /* the player's acts */
    char req[64] = "", marks[8][40];
    int nm = 0;
    uint32_t rseq = 0;
    pthread_mutex_lock(&LV.mx);
    if (LV.req[0] && LV.req_written != LV.req_seq) snprintf(req, sizeof req, "%s", LV.req), rseq = LV.req_seq;
    nm = LV.nmarks;
    memcpy(marks, LV.marks, sizeof marks);
    LV.nmarks = 0;
    pthread_mutex_unlock(&LV.mx);
    if (req[0]) {
        snprintf(path, sizeof path, "%s/patches/request.txt", LV.dir);
        int ok = lv_write(path, req) == 0;
        if (ok) LV.req_written = rseq;
        lg("xr: live: the request '%.40s' %s", req, ok ? "written for the game" : "NOT written (the folder?)");
    }
    for (int i = 0; i < nm; i++) {
        char w[40];
        lv_wall(w, sizeof w);
        snprintf(path, sizeof path, "%s/say/%.39s", LV.dir, marks[i]);
        strcat(w, "\n");
        lv_write(path, w);
    }
    for (int i = 0; i < LV.nsp; i++) { /* b37: the cards' spoken lines (the thread's own: no lock) */
        char fk[24], line[SOMVR_LV_SPEECH_MAX + 2];
        lv_fkey(LV.sp_key[i], fk, sizeof fk);
        snprintf(path, sizeof path, "%s/say/%s.line", LV.dir, fk);
        snprintf(line, sizeof line, "%s\n", LV.sp_text[i]);
        if (lv_write(path, line) && LV.lines++ < 300) lg("xr: live: say/%s.line NOT written", fk);
    }
    LV.nsp = 0;
}
static void *lv_main(void *arg) {
    (void)arg;
    while (!atomic_load(&LV.stop)) {
        lv_poll();
        struct timespec ts = {0, 200 * 1000 * 1000}; /* b37 live3: 0.5 -> 0.2 s (the voice and the card sooner) */
        nanosleep(&ts, NULL);
    }
    atomic_store(&LV.running, 0);
    return NULL;
}

/* ---- the swapchain ---------------------------------------------------------------------------------------- */
static void lv_free(void) {
    dev_st *ds = LV.ds;
    if (ds && LV.pending && LV.fence && ds->WaitForFences(ds->dev, 1, &LV.fence, VK_TRUE, FENCE_TIMEOUT_NS) == VK_SUCCESS)
        LV.pending = 0;
    if (ds && !LV.pending) {
        if (LV.cmd) ds->FreeCommandBuffers(ds->dev, ds->pool, 1, &LV.cmd);
        if (LV.fence) ds->DestroyFence(ds->dev, LV.fence, NULL);
        if (LV.map) ds->UnmapMemory(ds->dev, LV.mem);
        if (LV.buf) ds->DestroyBuffer(ds->dev, LV.buf, NULL);
        if (LV.mem) ds->FreeMemory(ds->dev, LV.mem, NULL);
    }
    LV.cmd = VK_NULL_HANDLE, LV.fence = VK_NULL_HANDLE, LV.map = NULL, LV.buf = VK_NULL_HANDLE;
    LV.mem = VK_NULL_HANDLE, LV.pending = 0, LV.ds = NULL;
    if (LV.sc) X.xrDestroySwapchain(LV.sc);
    LV.sc = XR_NULL_HANDLE, LV.nimg = 0, LV.released = 0, LV.drawn_key[0] = 0;
}
static void lv_destroy(void) { /* before the session (the thread ends by itself: LV.stop) */
    lv_free();
    LV.showing = 0, LV.in_vm0 = 0;
}
__attribute__((format(printf, 1, 2))) static void lv_off(const char *fmt, ...) {
    char why[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, sizeof why, fmt, ap);
    va_end(ap);
    if (LV.on) lg("xr: live: off for this session: %s (the frames go on without the card)", why);
    LV.on = 0;
    atomic_store(&LV.stop, 1);
    lv_free();
    somvr_vm_live_set(NULL);
}
static int lv_make(dev_st *ds) {
    if (LV.sc) return 0;
    if (!ds->CmdCopyBufferToImage || !ds->CreateBuffer || !ds->AllocateMemory || !ds->MapMemory || !ds->ResetFences ||
        !ds->ResetCommandBuffer || !ds->is->GetPhysicalDeviceMemoryProperties || !ds->pool)
        return lv_off("the driver lacks a buffer entry point"), -1;
    int64_t fmts[64];
    uint32_t n = 0;
    XrResult r = X.xrEnumerateSwapchainFormats(X.session, 64, &n, fmts);
    if (XR_FAILED(r)) return lv_off("xrEnumerateSwapchainFormats: %s", xr_str(r)), -1;
    static const VkFormat pref[4] = {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM,
                                     VK_FORMAT_B8G8R8A8_UNORM};
    VkFormat f = VK_FORMAT_UNDEFINED;
    for (int k = 0; k < 4 && f == VK_FORMAT_UNDEFINED; k++)
        for (uint32_t i = 0; i < n && i < 64; i++)
            if (fmts[i] == pref[k]) f = pref[k];
    if (f == VK_FORMAT_UNDEFINED) return lv_off("no 8-bit RGBA/BGRA swapchain format"), -1;
    XrSwapchainCreateInfo ci = {.type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
                                .usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
                                .format = f, .sampleCount = 1, .width = SOMVR_LV_TEX_W, .height = SOMVR_LV_TEX_H,
                                .faceCount = 1, .arraySize = 1, .mipCount = 1};
    if (XR_FAILED(r = X.xrCreateSwapchain(X.session, &ci, &LV.sc))) {
        LV.sc = XR_NULL_HANDLE;
        return lv_off("xrCreateSwapchain(%dx%d): %s", SOMVR_LV_TEX_W, SOMVR_LV_TEX_H, xr_str(r)), -1;
    }
    XrSwapchainImageVulkanKHR im[MAX_XR_IMAGES];
    for (int i = 0; i < MAX_XR_IMAGES; i++) im[i] = (XrSwapchainImageVulkanKHR){.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR};
    n = 0;
    r = X.xrEnumerateSwapchainImages(LV.sc, MAX_XR_IMAGES, &n, (XrSwapchainImageBaseHeader *)im);
    if (XR_FAILED(r) || n == 0) return lv_off("xrEnumerateSwapchainImages: %s, %u images", xr_str(r), n), -1;
    for (uint32_t i = 0; i < n; i++) LV.img[i] = im[i].image;
    LV.nimg = n, LV.bgra = f == VK_FORMAT_B8G8R8A8_SRGB || f == VK_FORMAT_B8G8R8A8_UNORM;
    LV.ds = ds;
    VkDeviceSize sz = (VkDeviceSize)SOMVR_LV_TEX_W * SOMVR_LV_TEX_H * 4;
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sz,
                              .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    const char *what = "vkCreateBuffer";
    VkResult vr = ds->CreateBuffer(ds->dev, &bci, NULL, &LV.buf);
    if (vr != VK_SUCCESS) LV.buf = VK_NULL_HANDLE;
    VkMemoryRequirements mr = {0};
    int type = -1;
    if (vr == VK_SUCCESS) {
        VkPhysicalDeviceMemoryProperties mp = {0};
        ds->GetBufferMemoryRequirements(ds->dev, LV.buf, &mr);
        ds->is->GetPhysicalDeviceMemoryProperties(ds->pd, &mp);
        what = "no HOST_VISIBLE|HOST_COHERENT memory";
        if ((type = somvr_pick_host_memory(&mp, mr.memoryTypeBits)) < 0) vr = VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (vr == VK_SUCCESS) {
        VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
                                    .memoryTypeIndex = (uint32_t)type};
        what = "vkAllocateMemory";
        if ((vr = ds->AllocateMemory(ds->dev, &mai, NULL, &LV.mem)) != VK_SUCCESS) LV.mem = VK_NULL_HANDLE;
    }
    if (vr == VK_SUCCESS) what = "vkBindBufferMemory", vr = ds->BindBufferMemory(ds->dev, LV.buf, LV.mem, 0);
    if (vr == VK_SUCCESS && (what = "vkMapMemory", vr = ds->MapMemory(ds->dev, LV.mem, 0, VK_WHOLE_SIZE, 0, &LV.map)) != VK_SUCCESS)
        LV.map = NULL;
    if (vr == VK_SUCCESS) {
        VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                           .commandPool = ds->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                           .commandBufferCount = 1};
        what = "vkAllocateCommandBuffers";
        if ((vr = ds->AllocateCommandBuffers(ds->dev, &cai, &LV.cmd)) != VK_SUCCESS) LV.cmd = VK_NULL_HANDLE;
    }
    if (vr == VK_SUCCESS) {
        what = "the command buffer's dispatch";
        if (ds->set_loader_data) vr = ds->set_loader_data(ds->dev, LV.cmd);
        else *(void **)LV.cmd = dkey(ds->dev);
    }
    if (vr == VK_SUCCESS) {
        VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        what = "vkCreateFence";
        if ((vr = ds->CreateFence(ds->dev, &fci, NULL, &LV.fence)) != VK_SUCCESS) LV.fence = VK_NULL_HANDLE;
    }
    if (vr != VK_SUCCESS) return lv_off("the staging buffer: %s: %d", what, vr), -1;
    lg("xr: live: swapchain %dx%d format %d (%u images), a %u KB staging buffer (the picture uploaded when the card "
       "changes)", SOMVR_LV_TEX_W, SOMVR_LV_TEX_H, f, n, (unsigned)(sz >> 10));
    return 0;
}
static int lv_upload(dev_st *ds, const somvr_lv_card *card, const char *key) {
    if (lv_make(ds)) return -1;
    VkResult vr;
    if (LV.pending) {
        if ((vr = ds->WaitForFences(ds->dev, 1, &LV.fence, VK_TRUE, FENCE_TIMEOUT_NS)) != VK_SUCCESS)
            return lv_off("vkWaitForFences: %d", vr), -1;
        LV.pending = 0;
    }
    if ((vr = ds->ResetFences(ds->dev, 1, &LV.fence)) != VK_SUCCESS) return lv_off("vkResetFences: %d", vr), -1;
    somvr_lv_draw(&LV.font, (uint8_t *)LV.map, LV.bgra, card);
    uint32_t xi = 0;
    XrSwapchainImageAcquireInfo ai = {.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrSwapchainImageWaitInfo wti = {.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, .timeout = XR_IMAGE_TIMEOUT_NS};
    XrResult r;
    if (XR_FAILED(r = X.xrAcquireSwapchainImage(LV.sc, &ai, &xi)) || xi >= LV.nimg)
        return lv_off("xrAcquireSwapchainImage: %s, image %u of %u", xr_str(r), xi, LV.nimg), -1;
    if ((r = xr_wait_image(LV.sc, &wti, "live")) != XR_SUCCESS) return lv_off("xrWaitSwapchainImage: %s", xr_str(r)), -1;
    const char *what = "vkResetCommandBuffer";
    if ((vr = ds->ResetCommandBuffer(LV.cmd, 0)) == VK_SUCCESS) {
        VkCommandBufferBeginInfo cbi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        what = "vkBeginCommandBuffer";
        vr = ds->BeginCommandBuffer(LV.cmd, &cbi);
    }
    if (vr == VK_SUCCESS) {
        VkImageMemoryBarrier b;
        img_barrier(&b, LV.img[xi], 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        ds->CmdPipelineBarrier(LV.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0,
                               NULL, 1, &b);
        VkBufferImageCopy rg = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                .imageExtent = {SOMVR_LV_TEX_W, SOMVR_LV_TEX_H, 1}};
        ds->CmdCopyBufferToImage(LV.cmd, LV.buf, LV.img[xi], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
        img_barrier(&b, LV.img[xi], VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        ds->CmdPipelineBarrier(LV.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                               NULL, 1, &b);
        what = "vkEndCommandBuffer";
        vr = ds->EndCommandBuffer(LV.cmd);
    }
    if (vr == VK_SUCCESS) {
        VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &LV.cmd};
        what = "vkQueueSubmit";
        if ((vr = ds->QueueSubmit(X.q, 1, &si, LV.fence)) == VK_SUCCESS) LV.pending = 1;
    }
    if (vr != VK_SUCCESS) return lv_off("the picture's upload: %s: %d", what, vr), -1;
    XrSwapchainImageReleaseInfo ri = {.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if (XR_FAILED(r = X.xrReleaseSwapchainImage(LV.sc, &ri))) return lv_off("xrReleaseSwapchainImage: %s", xr_str(r)), -1;
    LV.released = 1, LV.n_uploads++;
    snprintf(LV.drawn_key, sizeof LV.drawn_key, "%s", key);
    return 0;
}

/* xr_session_setup, before vm_setup (the Test page's text is laid out above the box) */
static void lv_setup(void) {
    lv_destroy();
    const char *e = getenv("SOMVR_LIVE"), *d = getenv("SOMVR_LIVE_DIR");
    int want = e && (!strcmp(e, "on") || !strcmp(e, "1"));
    if (LV.on) return; /* a second session in one run: the thread and the folder go on */
    if (!want) {
        if (e && *e && strcmp(e, "off") && strcmp(e, "0")) lg("xr: live: SOMVR_LIVE=%s is not on or off: off", e);
        return;
    }
    struct stat st;
    if (!d || !*d || strlen(d) >= sizeof LV.dir - 40 || stat(d, &st) != 0 || !S_ISDIR(st.st_mode)) {
        lg("xr: live: SOMVR_LIVE=on without a usable SOMVR_LIVE_DIR ('%s'): no live card", d ? d : "");
        return;
    }
    snprintf(LV.dir, sizeof LV.dir, "%s", d);
    somvr_font_init(&LV.font);
    somvr_lv_ccfg_default(&LV.ccfg);
    const char *cs = getenv("SOMVR_LIVE_CALM_S");
    if (cs && *cs) {
        double v = atof(cs);
        if (v >= 0.5 && v <= 30) LV.ccfg.calm_s = v;
    }
    memset(&LV.calm, 0, sizeof LV.calm);
    const char *tt = getenv("SOMVR_LIVE_TTS");
    LV.tts = tt && (!strcmp(tt, "on") || !strcmp(tt, "1"));
    LV.say_next = 1, LV.nq = 0, LV.ready_id = 0, LV.current = 0, LV.state_seq = 0, LV.req[0] = 0, LV.nsp = 0;
    LV.box = (somvr_vm_live){.on = 1, .gen = 1};
    snprintf(LV.box.line[0], sizeof LV.box.line[0], "No patch waiting");
    somvr_vm_live_set(&LV.box);
    LV.box_gen = LV.box.gen;
    somvr_tc_reset(&LV.tc);
    atomic_store(&LV.stop, 0);
    pthread_t th;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    int err = pthread_create(&th, &at, lv_main, NULL);
    pthread_attr_destroy(&at);
    if (err) {
        lg("xr: live: pthread_create: %s: no live card", strerror(err));
        somvr_vm_live_set(NULL);
        return;
    }
    atomic_store(&LV.running, 1);
    LV.on = 1;
    { /* b37 live3: what this layer does with the folder (scripts/live.py reads it: a long patch card is paged here) */
        char cp[600];
        snprintf(cp, sizeof cp, "%s/layer.caps", LV.dir);
        lv_write(cp, "pages card-out dot-hand\n");
    }
    lg("xr: live: on - the folder %s every 0.2 s; a card out by itself beside the off hand at a calm moment (%.1f s without the bow, a "
       "climb hold, a swing or an attack), the dot meanwhile; the Test page's patch box; spoken lines %s", LV.dir,
       LV.ccfg.calm_s, LV.tts ? "on (the nudge waits up to 8 s for each card's)" : "off");
}

/* vm_frame, before the gesture: a card waits and the moment is calm -> the palm gesture shows it (the menu's held) */
static int lv_pre(const somvr_vm_gin *gi) {
    if (!LV.on) return 0;
    uint64_t nb0 = LV.calm.n_busy, nc0 = LV.calm.n_calm;
    int calm = somvr_lv_calm_step(&LV.calm, &LV.ccfg, gi->t, gi->block);
    if ((LV.calm.n_busy != nb0 || LV.calm.n_calm != nc0) && LV.lines++ < 300) {
        char w[96];
        somvr_vm_gate_text(LV.calm.why & SOMVR_VMG_ALL, w, sizeof w);
        lg("xr: live: %s (%s%s; %llu calm, %llu busy so far)", calm ? "calm: a card may show" : "busy: a card waits",
           calm ? "since " : "", calm ? (LV.calm.why & SOMVR_VMG_FIGHT ? "an attack at Talion" : w)
                                      : (LV.calm.why & SOMVR_VMG_FIGHT ? "an attack at Talion lately" : w),
           (unsigned long long)LV.calm.n_calm, (unsigned long long)LV.calm.n_busy);
    }
    int waiting = 0;
    if (pthread_mutex_trylock(&LV.mx) == 0) {
        waiting = LV.nq > 0;
        if (LV.box.gen != LV.box_gen) somvr_vm_live_set(&LV.box), LV.box_gen = LV.box.gen;
        pthread_mutex_unlock(&LV.mx);
    } else {
        waiting = LV.showing != 0;
    }
    /* b37 live3: the menu keeps its palm gesture - the card goes out by itself (lv_post), never on the palm */
    return waiting && calm && !VM.gs.open;
}

/* the dot's quad (the dot: SOMVR_LV_DOT_M; the card: its picture) */
static XrCompositionLayerQuad lv_quad(const somvr_posef *p, int dot) {
    XrCompositionLayerQuad q = {.type = XR_TYPE_COMPOSITION_LAYER_QUAD,
                                .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
                                .space = X.space,
                                .eyeVisibility = XR_EYE_VISIBILITY_BOTH};
    q.subImage.swapchain = LV.sc;
    if (dot) q.subImage.imageRect = (XrRect2Di){{0, SOMVR_LV_DOT_Y}, {SOMVR_LV_DOT_PX, SOMVR_LV_DOT_PX}},
             q.size = (XrExtent2Df){SOMVR_LV_DOT_M, SOMVR_LV_DOT_M};
    else q.subImage.imageRect = (XrRect2Di){{0, 0}, {SOMVR_LV_TEX_W, SOMVR_LV_CARD_H}},
         q.size = (XrExtent2Df){SOMVR_LV_CARD_W_M, SOMVR_LV_CARD_H_M};
    memcpy(&q.pose, p, sizeof q.pose);
    return q;
}

/* the front card closed (tapped: read; or its time over): the next one after SOMVR_LV_GAP_S */
static void lv_close(const lv_item *front, double t, int tapped) {
    pthread_mutex_lock(&LV.mx);
    if (LV.nq > 0 && !strcmp(LV.q[0].key, front->key)) {
        memmove(&LV.q[0], &LV.q[1], sizeof LV.q[0] * (size_t)(LV.nq - 1));
        LV.nq--;
    }
    if (front->say && LV.nmarks < 8) snprintf(LV.marks[LV.nmarks++], sizeof LV.marks[0], "%d.read", front->say);
    pthread_mutex_unlock(&LV.mx);
    LV.n_read++;
    if (!tapped) LV.n_timeout++;
    lg("xr: live: card %s %s (out %.1f s); %llu read in all", front->key, tapped ? (MN.touch ? "tapped" : "the trigger on it")
       : "gone by itself (its time over)", t - LV.out_t, (unsigned long long)LV.n_read);
    LV.out = 0, LV.shown_key[0] = 0, LV.nudged_for = 0, LV.next_t = t + SOMVR_LV_GAP_S;
    somvr_tc_reset(&LV.tc), memset(&LV.tco, 0, sizeof LV.tco);
}

/* vm_frame, after the panel: (b37 live3, 162.10) a card waiting - the dot on the BACK of the off hand (only while the
 * back faces the eyes); at a calm moment with its line ready the card goes out by itself, world-locked beside the off
 * hand (somvr_lv_card_place, placed once), with the nudge and its spoken line; it stays SOMVR_LV_SHOW_S (and while its
 * line is said; SOMVR_LV_SHOW_MAX_S at most) or until tapped; a fight (not calm) or the menu puts it away (out again,
 * placed anew, at the next calm moment). Into VM.q[0] when the menu shows nothing; returns the trigger bits to
 * consume (the main hand's while it points at the card without touch) */
static uint32_t lv_post(dev_st *ds, const somvr_vm_gin *gi, int hold, int main) {
    LV.showing = 0, LV.in_vm0 = 0, LV.dot_on = 0;
    if (!LV.on || VM.nq > 0 || VM.gs.open) {
        if (LV.tco.on) somvr_tc_reset(&LV.tc), memset(&LV.tco, 0, sizeof LV.tco);
        if (LV.out && LV.on && LV.lines++ < 300) lg("xr: live: the card put away: the menu is open (out again at the next calm moment)");
        LV.out = 0;
        return 0;
    }
    lv_item front;
    int have = 0;
    if (pthread_mutex_trylock(&LV.mx) == 0) {
        if (LV.nq > 0) front = LV.q[0], have = 1;
        pthread_mutex_unlock(&LV.mx);
    } else if (LV.drawn_key[0]) { /* the thread busy this frame: the last picture again */
        return 0;
    }
    if (!have) {
        LV.out = 0;
        return 0;
    }
    if (strcmp(front.key, LV.drawn_key) && lv_upload(ds, &front.card, front.key)) return 0;
    if (!LV.released) return 0;
    double t = gi->t;
    if (LV.out && !hold) { /* a fight (or the calm gone): away now, out again at the next calm moment */
        LV.out = 0, LV.n_away++;
        if (LV.lines++ < 300) lg("xr: live: card %s put away: not calm (out again at the next calm moment)", front.key);
    }
    if (!LV.out && hold && front.voiced && t >= LV.next_t && gi->hand_ok && gi->head_ok &&
        somvr_lv_card_place(&gi->head, &gi->grip, gi->hand, &LV.out_pose)) {
        LV.out = 1, LV.out_t = t, LV.n_out++;
        uint32_t h = lv_hash(front.key);
        if (LV.nudged_for != (int)h) { /* its first time out: the nudge, and its spoken line now (say/<key>.calm) */
            LV.nudged_for = (int)h;
            vm_haptic(gi->hand, SOMVR_HX_READY);
            char fk[24];
            lv_fkey(front.key, fk, sizeof fk);
            if (pthread_mutex_lock(&LV.mx) == 0) {
                if (LV.nmarks < 8) snprintf(LV.marks[LV.nmarks++], sizeof LV.marks[0], "%.24s.calm", fk);
                if (front.say && LV.nmarks < 8) snprintf(LV.marks[LV.nmarks++], sizeof LV.marks[0], "%d.shown", front.say);
                pthread_mutex_unlock(&LV.mx);
            }
            LV.shown_t = t, LV.n_shown++;
            snprintf(LV.shown_key, sizeof LV.shown_key, "%s", front.key);
        }
        float hd[3];
        for (int i = 0; i < 3; i++) hd[i] = LV.out_pose.p[i] - gi->head.p[i];
        lg("xr: live: card %s out by itself beside the %s hand at %+.3f,%+.3f,%+.3f (%.2f m from the head, %.0f cm below "
           "the eyes; the hand at %+.3f,%+.3f,%+.3f; calm %.1f s; '%s')", front.key, gi->hand == SOMVR_HAND_L ? "left" : "right",
           LV.out_pose.p[0], LV.out_pose.p[1], LV.out_pose.p[2], sqrtf(hd[0] * hd[0] + hd[1] * hd[1] + hd[2] * hd[2]),
           -hd[1] * 100.0f, gi->grip.p[0], gi->grip.p[1], gi->grip.p[2], t - LV.calm.busy_t, front.card.title);
    }
    if (!LV.out) { /* waiting: the dot on the back of the off hand, only while that faces the eyes */
        somvr_posef dp;
        if (gi->hand_ok && somvr_lv_dot_place(&gi->grip, gi->hand, gi->head_ok ? &gi->head : NULL, &dp)) {
            LV.dot_q = lv_quad(&dp, 1); /* its own slot over our hands (XL_VM0 is under them: hidden on the hand) */
            LV.dot_on = 1, LV.showing = 2, LV.n_dot_frames++;
        } else {
            LV.n_dot_hidden++;
        }
        if (LV.tco.on) somvr_tc_reset(&LV.tc), memset(&LV.tco, 0, sizeof LV.tco);
        return 0;
    }
    /* out: the card where it was placed, until tapped or its time is over */
    double shown = t - LV.out_t;
    if ((shown >= SOMVR_LV_SHOW_S && front.spoken) || shown >= SOMVR_LV_SHOW_MAX_S) {
        lv_close(&front, t, 0);
        return 0;
    }
    somvr_posef pose = LV.out_pose;
    LV.quad = lv_quad(&pose, 0);
    VM.q[0] = LV.quad, VM.nq = 1;
    LV.in_vm0 = 1, LV.showing = 1, LV.n_card_frames++, LV.card_pose = pose;
    /* the tap: the main hand's fingertip (touch), else its trigger pointed at the card */
    float sz[2] = {SOMVR_LV_CARD_W_M, SOMVR_LV_CARD_H_M};
    int press = 0;
    uint32_t consume = 0;
    if (MN.touch) {
        somvr_tc_in ti;
        mn_tc_in(&ti, 1u << main, 2u, &pose, sz);
        somvr_tc_update(&LV.tc, &MN.tcc, &ti, &LV.tco);
        press = (LV.tco.edges & SOMVR_TCE_PRESS) != 0;
        if (LV.tco.on && LV.tco.edges && LV.lines++ < 200)
            lg("xr: live: touch edges 0x%x uv %.3f,%.3f depth %+.3f m", LV.tco.edges, LV.tco.uv[0], LV.tco.uv[1], LV.tco.depth);
    } else if (I.on) {
        const somvr_hand_in *hp = &I.st.hand[main];
        float o[3], d[3], uv[2], tt;
        somvr_ptr_ray(&hp->aim, o, d);
        int on = (hp->aim_flags & 3u) == 3u && somvr_ptr_hit_quad(o, d, &pose, sz, 1.5f, uv, &tt) && uv[0] >= 0 &&
                 uv[0] <= 1 && uv[1] >= 0 && uv[1] <= 1;
        int trig = hp->trigger > 0.7f;
        press = on && trig && !LV.trig_was;
        LV.trig_was = trig;
        if (on) consume = 1u << main;
    }
    if (press) {
        LV.n_press++;
        vm_haptic(main, SOMVR_HX_CLICK);
        lv_close(&front, t, 1);
        return consume;
    }
    if (LV.n_card_frames <= 2 || LV.n_card_frames % 600 == 0)
        lg("xr: live: card frame %llu: %s at %+.3f,%+.3f,%+.3f (dot frames %llu, hidden %llu; out %llu, away %llu)",
           (unsigned long long)LV.n_card_frames, front.key, pose.p[0], pose.p[1], pose.p[2],
           (unsigned long long)LV.n_dot_frames, (unsigned long long)LV.n_dot_hidden, (unsigned long long)LV.n_out,
           (unsigned long long)LV.n_away);
    return consume;
}

/* vm_click: the patch box's buttons (enabled only) -> the request for the game side */
static int lv_click(int item) {
    if (item != SOMVR_VMI_LIVE_APPLY && item != SOMVR_VMI_LIVE_UNDO) return 0;
    if (!LV.on || !somvr_vm_enabled(&VM.set, 0, item)) {
        vm_haptic(VM.main_hand, SOMVR_HX_NO);
        lg("xr: live: %s pressed while it is off (nothing to %s)", somvr_vm_label(item),
           item == SOMVR_VMI_LIVE_APPLY ? "apply" : "undo");
        return 1;
    }
    pthread_mutex_lock(&LV.mx);
    int id = item == SOMVR_VMI_LIVE_APPLY ? LV.ready_id : LV.current;
    LV.req_seq = LV.req_seq > LV.state_seq ? LV.req_seq + 1 : LV.state_seq + 1;
    snprintf(LV.req, sizeof LV.req, "%s %d %u\n", item == SOMVR_VMI_LIVE_APPLY ? "apply" : "undo", id, LV.req_seq);
    /* the box says so at once; the game's answer replaces it */
    snprintf(LV.box.line[0], sizeof LV.box.line[0], "%s patch %d ...", item == SOMVR_VMI_LIVE_APPLY ? "Applying" : "Undoing", id);
    LV.box.can_apply = LV.box.can_undo = 0, LV.box.gen++;
    uint32_t seq = LV.req_seq;
    pthread_mutex_unlock(&LV.mx);
    LV.n_press++;
    vm_haptic(VM.main_hand, SOMVR_HX_CLICK);
    lg("xr: live: %s pressed: request %u (%s patch %d)", somvr_vm_label(item), seq,
       item == SOMVR_VMI_LIVE_APPLY ? "apply" : "undo", id);
    return 1;
}

/* xt_quad (SOMVR_XR_TEST's `touch live`): the card's pose while it is shown (this frame or the last) */
static int lv_card_pose(somvr_posef *pose, float size[2]) {
    if (!LV.on || LV.showing != 1) return 0;
    *pose = LV.card_pose, size[0] = SOMVR_LV_CARD_W_M, size[1] = SOMVR_LV_CARD_H_M;
    return 1;
}

/* mn_dump_layers: this frame's XL_VM0 is ours - 1 the card, 2 the dot (0: the menu's or none) */
static int lv_kind(void) { return LV.on && LV.in_vm0 ? LV.showing : 0; }

/* a dumped frame with our quad in XL_VM0 (mn_dump_layers): the picture as <base>.liveN.ppm + .alpha.pgm */
static void lv_dump(uint64_t frame) {
    if (!LV.on || !LV.map || !LV.released) return;
    char path[sizeof D.base + 48], apath[sizeof D.base + 48];
    snprintf(path, sizeof path, "%s.live%llu.ppm", D.base, (unsigned long long)frame);
    snprintf(apath, sizeof apath, "%s.live%llu.alpha.pgm", D.base, (unsigned long long)frame);
    FILE *f = fopen(path, "we"), *a = fopen(apath, "we");
    int ok = f && a && fprintf(f, "P6\n%d %d\n255\n", SOMVR_LV_TEX_W, SOMVR_LV_TEX_H) > 0 &&
             fprintf(a, "P5\n%d %d\n255\n", SOMVR_LV_TEX_W, SOMVR_LV_TEX_H) > 0;
    const uint8_t *px = (const uint8_t *)LV.map;
    for (int i = 0; ok && i < SOMVR_LV_TEX_W * SOMVR_LV_TEX_H; i++) {
        const uint8_t *s = px + 4 * i;
        uint8_t rgb[3] = {s[LV.bgra ? 2 : 0], s[1], s[LV.bgra ? 0 : 2]};
        ok = fwrite(rgb, 1, 3, f) == 3 && fputc(s[3], a) != EOF;
    }
    if (f) ok &= fclose(f) == 0;
    if (a) ok &= fclose(a) == 0;
    lg("xr: dump live frame %llu -> %s + .alpha.pgm (%dx%d, the %s, '%s')%s", (unsigned long long)frame, path,
       SOMVR_LV_TEX_W, SOMVR_LV_TEX_H, LV.showing == 1 ? "card" : "dot", LV.drawn_key, ok ? "" : ": write failed");
}
