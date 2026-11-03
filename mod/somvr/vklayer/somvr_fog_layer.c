/* somvr_fog_layer.c - build 31 (docs/re-xr-pipeline.md 137): the game's shader modules as the layer sees them
 * (included by somvr_vklayer.c after the test head's file). The game's D3D11 shaders reach Vulkan as SPIR-V made at
 * runtime (IndirectX's DXBC -> GLSL translator, then glslang); vkCreateShaderModule is where the layer sees them.
 * SOMVR_XR_FOG=radial|game (code default game; the build 31 env: radial): the fog overlay's two pixel shaders (the
 * opaque pass and the OIT pass, known by their code's FNV-1a 64 and size) are created with the fog's path length the
 * distance along the eye's ray instead of the view's depth (./somvr_fogpatch.h): the same fog wherever the head points.
 * A module of that size whose hash or pattern does not match is the game's (logged). Lines: `xr: fog: ..`. */
#include "somvr_fogpatch.h"
static const struct {
    uint64_t hash;
    uint32_t size, call;
    const char *what;
} g_fog_sites[] = {
    {0x5bb4420b4b219149ULL, 18776, 332, "the fog overlay"},
    {0x3b25070a5c940c06ULL, 24116, 620, "the fog overlay's OIT pass (the fog on each transparent layer)"},
};
static struct {
    int on;
    uint64_t patched, failed, other;
} FOG;
static void fog_config(void) {
    const char *v = getenv("SOMVR_XR_FOG");
    FOG.on = v && !strcmp(v, "radial");
    if (v && *v && strcmp(v, "radial") && strcmp(v, "game"))
        lg("WARN xr: fog: SOMVR_XR_FOG=%s is not radial|game: game", v);
    lg("xr: fog: SOMVR_XR_FOG=%s: %s", FOG.on ? "radial" : "game",
       FOG.on ? "the fog overlay's shaders by the distance along the eye's ray (the same fog wherever the head points)"
              : "the game's fog by the view's depth (it thins at the view's edges and turns with the head)");
}
/* SOMVR_XR_SPV_DUMP=DIR[,all] (headless tests only; unset = nothing installed): every module whose SPIR-V carries the
 * word "Fog" - or every module with `,all` (the game's carry no names: the DXBC registers r0.., cb0.. only; build 31
 * found the fog overlay among 1300 modules with scripts/spv_find.py, read it with scripts/spv_dec.py) - is written to
 * DIR/<fnv64 of its code>.spv with an `xr: spv:` line; `xr: spv: census` lines count the modules seen, the ones
 * carrying any k_* name and the ones written. Never in a player's build. */
static struct {
    int on, all;
    char dir[512];
    pthread_mutex_t mx;
    uint64_t seen, named, written, bytes;
    uint64_t done[256];
    uint32_t ndone;
} SPV = {.mx = PTHREAD_MUTEX_INITIALIZER};

static uint64_t spv_fnv(const unsigned char *p, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 1099511628211ULL;
    return h;
}
static int spv_has(const unsigned char *p, size_t n, const char *w) {
    size_t k = strlen(w);
    for (size_t i = 0; i + k <= n; i++)
        if (p[i] == (unsigned char)w[0] && !memcmp(p + i, w, k)) return 1;
    return 0;
}

static void spv_config(void) {
    fog_config();
    const char *d = getenv("SOMVR_XR_SPV_DUMP");
    if (!d || !*d) return;
    snprintf(SPV.dir, sizeof SPV.dir, "%s", d);
    char *c = strchr(SPV.dir, ',');
    if (c) {
        SPV.all = !strcmp(c + 1, "all");
        *c = 0;
    }
    mkdir(SPV.dir, 0755);
    SPV.on = 1;
    lg("xr: spv: SOMVR_XR_SPV_DUMP=%s: the game's shader modules %s written to %s (a headless test)", d,
       SPV.all ? "(all)" : "naming the fog (\"Fog\")", SPV.dir);
}

static VKAPI_ATTR VkResult VKAPI_CALL L_CreateShaderModule(VkDevice dev, const VkShaderModuleCreateInfo *ci,
                                                           const VkAllocationCallbacks *alloc, VkShaderModule *out) {
    dev_st *ds = LIST_FIND(g_devs, key, dkey(dev));
    if (!ds) return VK_ERROR_INITIALIZATION_FAILED;
    if (SPV.on && ci && ci->pCode && ci->codeSize >= 20) {
        const unsigned char *p = (const unsigned char *)ci->pCode;
        size_t n = ci->codeSize;
        int named = spv_has(p, n, "k_"), fog = spv_has(p, n, "Fog");
        uint64_t h = spv_fnv(p, n);
        int write = 0;
        pthread_mutex_lock(&SPV.mx);
        SPV.seen++, SPV.named += named != 0;
        if (SPV.all || fog) {
            write = 1;
            for (uint32_t i = 0; i < SPV.ndone; i++)
                if (SPV.done[i] == h) write = 0;
            if (write && SPV.ndone < 256) SPV.done[SPV.ndone++] = h;
            else if (write && !SPV.all) write = 0;
        }
        uint64_t seen = SPV.seen;
        pthread_mutex_unlock(&SPV.mx);
        if (write) {
            char path[600];
            snprintf(path, sizeof path, "%s/%016llx.spv", SPV.dir, (unsigned long long)h);
            FILE *f = fopen(path, "wb");
            if (f) {
                fwrite(p, 1, n, f);
                fclose(f);
                pthread_mutex_lock(&SPV.mx);
                SPV.written++, SPV.bytes += n;
                pthread_mutex_unlock(&SPV.mx);
                if (!SPV.all || SPV.written <= 40)
                    lg("xr: spv: module %llu (%zu bytes, %s) -> %s", (unsigned long long)seen, n,
                       fog ? "names the fog" : named ? "named" : "no k_ names", path);
            }
        }
        if (seen % 500 == 0)
            lg("xr: spv: census: %llu modules seen, %llu carry k_ names, %llu written (%.1f MB)",
               (unsigned long long)SPV.seen, (unsigned long long)SPV.named, (unsigned long long)SPV.written,
               (double)SPV.bytes / 1e6);
    }
    if (FOG.on && ci && ci->pCode && !(ci->codeSize & 3)) {
        for (size_t s = 0; s < sizeof g_fog_sites / sizeof g_fog_sites[0]; s++) {
            if (ci->codeSize != g_fog_sites[s].size) continue;
            uint64_t h = spv_fnv((const unsigned char *)ci->pCode, ci->codeSize);
            if (h != g_fog_sites[s].hash) {
                FOG.other++;
                continue;
            }
            size_t nw = 0;
            const char *why = NULL;
            uint32_t *pw = somvr_fogpatch(ci->pCode, ci->codeSize / 4, g_fog_sites[s].call, &nw, &why);
            if (!pw) {
                FOG.failed++;
                lg("xr: fog: module %016llx (%s): NOT changed - %s (the game's fog)", (unsigned long long)h,
                   g_fog_sites[s].what, why ? why : "?");
                break;
            }
            VkShaderModuleCreateInfo c2 = *ci;
            c2.pCode = pw, c2.codeSize = nw * 4;
            VkResult r = ds->CreateShaderModule(dev, &c2, alloc, out);
            free(pw);
            FOG.patched++;
            lg("xr: fog: module %016llx (%s, %zu bytes): created with the fog by the distance along the eye's ray (+%zu "
               "words): %d (%llu so far)", (unsigned long long)h, g_fog_sites[s].what, ci->codeSize, nw - ci->codeSize / 4,
               r, (unsigned long long)FOG.patched);
            return r;
        }
    }
    return ds->CreateShaderModule(dev, ci, alloc, out);
}

static PFN_vkVoidFunction spv_dev_hook(const char *n) {
    if (!SPV.on && !FOG.on) return NULL;
    if (!strcmp(n, "vkCreateShaderModule")) return (PFN_vkVoidFunction)L_CreateShaderModule;
    return NULL;
}
