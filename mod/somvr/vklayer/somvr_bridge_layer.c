/* somvr_bridge_layer.c - the game-side objects under Proton (docs/re-winport.md 3.1, 7.1; ../somvr_xrloc.h): included
 * by somvr_vklayer.c after its headers.
 *
 * On the Linux build the LD_PRELOAD library's objects are found with dlsym(RTLD_DEFAULT). Under Proton the game-side
 * code is a Windows DLL (not in the ELF scope): it writes their addresses into the file SOMVR_BRIDGE names (the same
 * process; Wine copies the Unix environment, so the DLL sees the variable too). somvr_obj() reads that file (its
 * "pid" must be ours: never another process's stale file) and falls back to dlsym. br_pe() = the objects came from a
 * PE bridge: the layer then stores the DLL's locate stand-ins instead of the runtime's functions and runs the 1 ms
 * locate thread (somvr_xrloc_layer.c), so no PE code ever calls a Linux function. */
#include "../somvr_xrloc.h"
#include <sys/uio.h>
#ifdef _WIN32
#ifndef _WINDOWS_ /* (the kernel32 two, without windows.h in the layer's one big file) */
__declspec(dllimport) void *__stdcall GetModuleHandleA(const char *name);
__declspec(dllimport) void *__stdcall GetProcAddress(void *module, const char *name);
typedef void *HMODULE;
#endif
/* W1: the layer is a Windows DLL in the game's process: the proxy DLL's somvr_win_bridge(name) answers (no file) */
static const void *(*br_win_fn(void))(const char *) {
    HMODULE m = GetModuleHandleA("xinput9_1_0.dll");
    return m ? (const void *(*)(const char *))(void *)GetProcAddress(m, "somvr_win_bridge") : NULL;
}
#endif

static void xl_start(void); /* somvr_xrloc_layer.c: the locate thread (session running, a PE bridge) */
static void xl_stop(void);

#define BR_MAX 24
static struct {
    pthread_mutex_t mx;
    int state;          /* 0 = not read, 1 = read and valid, -1 = no file / not ours (read again later) */
    int n;
    char name[BR_MAX][32];
    uint64_t addr[BR_MAX];
    long pid;
    uint64_t nonce;
    char why[200];
    int logged;
} BR = {.mx = PTHREAD_MUTEX_INITIALIZER};

/* X held: parse the file once it is there (the DLL writes it before the game's own code runs) */
static void br_read_locked(void) {
    const char *p = getenv(SOMVR_BRIDGE_ENV);
    if (!p || !p[0]) {
        BR.state = -1, snprintf(BR.why, sizeof BR.why, "%s unset", SOMVR_BRIDGE_ENV);
        return;
    }
    FILE *f = fopen(p, "re");
    if (!f) {
        BR.state = -1, snprintf(BR.why, sizeof BR.why, "%s: %s", p, strerror(errno));
        return;
    }
    char line[256];
    int head = 0, end = 0;
    BR.n = 0, BR.pid = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strcmp(line, SOMVR_BRIDGE_HEAD)) head = 1;
        else if (!strcmp(line, "end")) end = 1;
        else if (!strncmp(line, "pid ", 4)) BR.pid = strtol(line + 4, NULL, 10);
        else if (!strncmp(line, "nonce ", 6)) BR.nonce = strtoull(line + 6, NULL, 16);
        else {
            char nm[32];
            unsigned long long a;
            if (BR.n < BR_MAX && sscanf(line, "%31s %llx", nm, &a) == 2 && a) {
                snprintf(BR.name[BR.n], sizeof BR.name[0], "%s", nm);
                BR.addr[BR.n++] = (uint64_t)a;
            }
        }
    }
    fclose(f);
    /* ours iff somvr_xrloc at its address is readable here (process_vm_readv on ourselves never faults) and holds the
     * file's nonce */
    uint64_t xa = 0;
    for (int i = 0; i < BR.n; i++)
        if (!strcmp(BR.name[i], SOMVR_XRLOC_SYMBOL)) xa = BR.addr[i];
    somvr_xrloc xl;
    struct iovec lo = {&xl, sizeof xl}, re = {(void *)(uintptr_t)xa, sizeof xl};
    int readable = xa && process_vm_readv(getpid(), &lo, 1, &re, 1, 0) == (ssize_t)sizeof xl;
    if (!head || !end) BR.state = -1, snprintf(BR.why, sizeof BR.why, "%s: not a complete bridge file", p);
    else if (!readable || !somvr_xrloc_ok(&xl) || !BR.nonce || xl.nonce != BR.nonce)
        BR.state = -1, snprintf(BR.why, sizeof BR.why, "%s: not this process's (somvr_xrloc %#llx %s)", p,
                                (unsigned long long)xa, !readable ? "not mapped here" : "without the file's nonce");
    else BR.state = 1, snprintf(BR.why, sizeof BR.why, "%s: %d objects of the PE DLL (nonce %#llx)", p, BR.n,
                                (unsigned long long)BR.nonce);
}

static int br_pe(void) {
#ifdef _WIN32 /* W1: the bridge is the proxy's export */
    const void *(*f)(const char *) = br_win_fn();
    if (!BR.logged++) lg("bridge: %s", f ? "W1: the proxy DLL's somvr_win_bridge" : "W1: no xinput9_1_0.dll with somvr_win_bridge");
    return f && somvr_xrloc_ok((const somvr_xrloc *)f(SOMVR_XRLOC_SYMBOL));
#endif
    pthread_mutex_lock(&BR.mx);
    if (BR.state <= 0) br_read_locked();
    int ok = BR.state == 1;
    if (!BR.logged || (ok && BR.logged < 2)) {
        lg("bridge: %s", BR.why);
        BR.logged = ok ? 2 : 1;
    }
    pthread_mutex_unlock(&BR.mx);
    return ok;
}

static void *br_get(const char *name) {
#ifdef _WIN32
    const void *(*f)(const char *) = br_win_fn();
    return f ? (void *)f(name) : NULL;
#endif
    void *r = NULL;
    pthread_mutex_lock(&BR.mx);
    if (BR.state == 1)
        for (int i = 0; i < BR.n; i++)
            if (!strcmp(BR.name[i], name)) r = (void *)(uintptr_t)BR.addr[i];
    pthread_mutex_unlock(&BR.mx);
    return r;
}

/* the idle layout of one of the PE DLL's own images (somvr_xrloc v2), -1 = not one (or no PE bridge): img_barrier */
static somvr_xrloc *_Atomic g_xl_img;
static int64_t xl_idle_layout(uint64_t img) {
    somvr_xrloc *x = atomic_load_explicit(&g_xl_img, memory_order_acquire);
    return x ? somvr_xrloc_layout(x, img) : -1;
}


/* the game-side object `name`: the PE bridge's, else the LD_PRELOAD library's */
static void *somvr_obj(const char *name) {
#ifdef _WIN32
    const void *(*f)(const char *) = br_win_fn();
    if (!f) return NULL;
    somvr_xrloc *x = (somvr_xrloc *)f(SOMVR_XRLOC_SYMBOL);
    if (somvr_xrloc_ok(x)) atomic_store_explicit(&g_xl_img, x, memory_order_release);
    return (void *)f(name);
#else
    if (getenv(SOMVR_BRIDGE_ENV) && br_pe()) {
        somvr_xrloc *x = (somvr_xrloc *)br_get(SOMVR_XRLOC_SYMBOL);
        if (somvr_xrloc_ok(x)) atomic_store_explicit(&g_xl_img, x, memory_order_release);
        return br_get(name);
    }
    return dlsym(RTLD_DEFAULT, name);
#endif
}
