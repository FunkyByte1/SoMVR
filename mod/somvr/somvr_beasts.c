/* somvr_beasts.c - see somvr_beasts.h (pure functions, no globals) */
#include "somvr_beasts.h"

#include <math.h>
#include <string.h>
#include <strings.h>

static int has_ci(const char *s, const char *w)
{
    size_t n = strlen(w);
    for (; s && *s; s++)
        if (!strncasecmp(s, w, n))
            return 1;
    return 0;
}

int somvr_bz_kind(const char *name)
{
    if (!name || !name[0])
        return SOMVR_BZ_NONE;
    if (has_ci(name, "Warg"))
        return SOMVR_BZ_CARAGOR;
    if (has_ci(name, "Goblin"))
        return SOMVR_BZ_GHUL;
    if (has_ci(name, "Troll"))
        return SOMVR_BZ_GRAUG;
    return SOMVR_BZ_NONE;
}

const char *somvr_bz_kind_name(int kind)
{
    static const char *const n[SOMVR_BZ_N] = { "none", "caragor", "ghul", "graug" };
    return kind >= 0 && kind < SOMVR_BZ_N ? n[kind] : "?";
}

void somvr_bz_shape_default(int kind, somvr_bz_shape *s)
{
    memset(s, 0, sizeof *s);
    switch (kind) {
    case SOMVR_BZ_CARAGOR: /* a wolf the size of a horse, low and long */
        s->r_head = 22, s->r_body = 40, s->r_tail = 14, s->snout = 30;
        s->fb_up = 0, s->fb_back = 110, s->fb_front = 120, s->fb_r = 45;
        break;
    case SOMVR_BZ_GHUL: /* a crouched, man-high climber */
        s->r_head = 18, s->r_body = 26, s->r_tail = 0, s->snout = 0;
        s->fb_up = 0, s->fb_back = 25, s->fb_front = 25, s->fb_r = 32;
        break;
    case SOMVR_BZ_GRAUG: /* a giant: the blade reaches its legs and belly */
        s->r_head = 45, s->r_body = 80, s->r_tail = 0, s->snout = 0;
        s->fb_up = 100, s->fb_back = 90, s->fb_front = 90, s->fb_r = 95;
        break;
    default: break;
    }
}

static void put(somvr_bz_cap *c, const float a[3], const float b[3], float r, int part)
{
    memcpy(c->c0, a, sizeof c->c0), memcpy(c->c1, b, sizeof c->c1);
    c->r = r, c->part = part;
}

int somvr_bz_capsules(const somvr_bz_shape *s, const float pos[SOMVR_BZP_N][3], const int have[SOMVR_BZP_N],
                      const float root[3], const float fwd[3], somvr_bz_cap out[SOMVR_BZ_CAPS])
{
    int idx[SOMVR_BZP_N], n = 0, k = 0;
    for (int p = 0; p < SOMVR_BZP_N; p++)
        if (have && have[p] && isfinite(pos[p][0]) && isfinite(pos[p][1]) && isfinite(pos[p][2]))
            idx[n++] = p;
    if (n < 2) {
        float f[3] = { fwd ? fwd[0] : 0, 0, fwd ? fwd[2] : 1 }, l = sqrtf(f[0] * f[0] + f[2] * f[2]);
        if (!(l > 1e-3f) || !(s->fb_r > 0))
            return 0;
        f[0] /= l, f[2] /= l;
        float a[3] = { root[0] - f[0] * s->fb_back, root[1] + s->fb_up, root[2] - f[2] * s->fb_back };
        float b[3] = { root[0] + f[0] * s->fb_front, root[1] + s->fb_up, root[2] + f[2] * s->fb_front };
        put(&out[0], a, b, s->fb_r, -2);
        return 1;
    }
    /* the snout: past the head along the line from the next part back (the neck, else whatever follows) */
    if (idx[0] == SOMVR_BZP_HEAD && s->snout > 0) {
        const float *h = pos[SOMVR_BZP_HEAD], *nk = pos[idx[1]];
        float d[3] = { h[0] - nk[0], h[1] - nk[1], h[2] - nk[2] }, l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (l > 1e-3f) {
            float tip[3] = { h[0] + d[0] / l * s->snout, h[1] + d[1] / l * s->snout, h[2] + d[2] / l * s->snout };
            put(&out[k++], h, tip, s->r_head, -1);
        }
    }
    for (int i = 0; i + 1 < n && k < SOMVR_BZ_CAPS; i++) {
        int a = idx[i], b = idx[i + 1];
        float r = a == SOMVR_BZP_HEAD ? s->r_head : b == SOMVR_BZP_TAIL ? s->r_tail : s->r_body;
        if (r > 0)
            put(&out[k++], pos[a], pos[b], r, a);
    }
    return k;
}

float somvr_bz_limb_r(int kind, int limb)
{
    if (limb < 0 || limb >= SOMVR_BZL_N)
        return 0;
    if (kind == SOMVR_BZ_GRAUG) /* thighs and shins as thick as an orc; the arms its fists swing down */
        return limb <= SOMVR_BZL_L_THIGH ? 50.0f : limb <= SOMVR_BZL_L_SHIN ? 38.0f : 32.0f;
    return 0;
}

int somvr_bz_limb(int n, somvr_bz_cap out[SOMVR_BZ_CAPS_ALL], const float a[3], const float b[3], float r, int limb)
{
    if (n >= SOMVR_BZ_CAPS_ALL || !(r > 0))
        return n;
    for (int i = 0; i < 3; i++)
        if (!isfinite(a[i]) || !isfinite(b[i]))
            return n;
    put(&out[n], a, b, r, -10 - limb);
    return n + 1;
}

int somvr_bz_blows(int kind, const somvr_bz_shape *s, const float head[3], const float neck[3], int have_jaws,
                   const float arm[2][3], const float hand[2][3], const int have_fist[2], float base[2][3], float tip[2][3])
{
    int n = 0;
    if (kind == SOMVR_BZ_CARAGOR && have_jaws) {
        float d[3] = { head[0] - neck[0], head[1] - neck[1], head[2] - neck[2] };
        float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (l > 1e-3f && isfinite(l)) {
            float k = (s ? s->snout : 30.0f) / l;
            for (int i = 0; i < 3; i++)
                base[n][i] = neck[i], tip[n][i] = head[i] + d[i] * k;
            n++;
        }
    } else if (kind == SOMVR_BZ_GRAUG) {
        for (int f = 0; f < 2; f++)
            if (have_fist && have_fist[f] && isfinite(arm[f][0] + arm[f][1] + arm[f][2] + hand[f][0] + hand[f][1] + hand[f][2]))
                memcpy(base[n], arm[f], sizeof base[n]), memcpy(tip[n], hand[f], sizeof tip[n]), n++;
    }
    return n;
}
