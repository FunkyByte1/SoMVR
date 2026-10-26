/* somvr_env.c - SoM loading env, the pure part (./somvr_env.h, docs/re-xr-pipeline.md 14): the switch, the procedural
 * Mordor-dusk sky and its image, the box of the quad fallback, the world mode's dim quads, the transitions. */
#include "somvr_env.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI_F 3.14159265358979f

/* ---- the switch ---------------------------------------------------------------------------------------------------- */
int somvr_env_parse(const char *v, int *bad) {
    *bad = 0;
    if (!v || !*v || !strcmp(v, "off") || !strcmp(v, "0")) return SOMVR_ENV_OFF;
    if (!strcmp(v, "on") || !strcmp(v, "1") || !strcmp(v, "sky")) return SOMVR_ENV_ON;
    if (!strcmp(v, "world")) return SOMVR_ENV_WORLD;
    *bad = 1;
    return SOMVR_ENV_OFF;
}

float somvr_env_parse_dim(const char *v, int *bad) {
    *bad = 0;
    if (!v || !*v) return SOMVR_ENV_DIM_DEFAULT;
    char *end = NULL;
    float f = strtof(v, &end);
    if (end == v || *end || !(f >= 0.0f && f <= 1.0f)) { /* NaN and inf fail the range too */
        *bad = 1;
        return SOMVR_ENV_DIM_DEFAULT;
    }
    return f;
}

/* ---- the sky ------------------------------------------------------------------------------------------------------- */
/* the palette (sRGB-encoded): dusk over Mordor, dark and warm; nothing brighter than ~0.4 */
static const float ZEN[3] = {0.024f, 0.018f, 0.023f};  /* the zenith: purple-black */
static const float HOR[3] = {0.200f, 0.082f, 0.052f};  /* the sky at the horizon: dull red-brown */
static const float KDEC[3] = {0.34f, 0.20f, 0.26f};    /* the gradient's decay with elevation (rad): red stays longest */
static const float GLOW[3] = {0.080f, 0.026f, 0.008f}; /* the faint band along the horizon */
#define GLOW_K 0.06f                                   /* its decay above the horizon (rad) */
static const float DOOM[3] = {0.200f, 0.060f, 0.015f}; /* the sky over Orodruin */
#define DOOM_A 1.75f                                   /* its azimuth (rad): 100 deg to the right of ahead */
#define DOOM_SA 0.06f                                  /* its glow's width in azimuth */
#define DOOM_E 0.11f                                   /* ... centred at this elevation (just over the cone) */
#define DOOM_SE 0.06f                                  /* ... and this width in elevation */
static const float MNT[3] = {0.034f, 0.024f, 0.023f};  /* the ridges' body */
static const float RIM[3] = {0.075f, 0.038f, 0.028f};  /* just under the ridge line (the glow behind it) */
static const float GH[3] = {0.110f, 0.062f, 0.046f};   /* the ground's haze at the horizon */
static const float GN[3] = {0.026f, 0.020f, 0.018f};   /* the ground straight down */
#define GROUND_K 0.10f                                 /* the haze's decay below the horizon (rad) */
#define EDGE 0.003f                                    /* the soft width of the ridge line (rad, half an EQ texel) */

static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static float smooth01(float x) {
    x = clamp01(x);
    return x * x * (3 - 2 * x);
}

typedef struct { float ridge, glow_w, doom_a; } env_col; /* what depends on the azimuth alone */
typedef struct { float sky[3], glow, doom_e, ground[3]; } env_row; /* ... on the elevation alone */

static env_col col_terms(float a) {
    env_col c;
    float da = remainderf(a - DOOM_A, 2 * PI_F);
    /* the ridges: integer frequencies, so the line closes on itself behind the player */
    float h = 0.035f + 0.018f * sinf(3 * a + 0.7f) + 0.011f * sinf(7 * a + 2.1f) + 0.006f * sinf(13 * a + 4.0f) +
              0.003f * sinf(29 * a + 1.3f); /* 0.3 .. 4.2 deg */
    if (!(h > 0.006f)) h = 0.006f;
    float x = fabsf(da) / 0.15f; /* Orodruin's cone, 5.7 deg tall, a notch at its top */
    if (x < 1) {
        float q = da / 0.015f, hd = 0.10f * powf(1 - x, 1.25f) * (1 - 0.10f * expf(-q * q));
        if (hd > h) h = hd;
    }
    c.ridge = h;
    c.glow_w = 0.35f + 0.65f * expf(-da * da / (2 * 0.6f * 0.6f)); /* the band is warmer toward Orodruin */
    c.doom_a = expf(-da * da / (2 * DOOM_SA * DOOM_SA));
    return c;
}

static env_row row_terms(float e) {
    env_row r;
    float up = e > 0 ? e : 0, down = e < 0 ? -e : 0, de = e - DOOM_E;
    for (int k = 0; k < 3; k++) {
        r.sky[k] = ZEN[k] + (HOR[k] - ZEN[k]) * expf(-up / KDEC[k]);
        r.ground[k] = GN[k] + (GH[k] - GN[k]) * expf(-down / GROUND_K);
    }
    r.glow = expf(-up / GLOW_K);
    r.doom_e = expf(-de * de / (2 * DOOM_SE * DOOM_SE));
    return r;
}

static void mix_terms(float e, const env_col *c, const env_row *r, float out[3]) {
    float depth = c->ridge - e;                    /* > 0: inside the ridge */
    float mm = smooth01(depth / (2 * EDGE) + 0.5f); /* 0 above the line, 1 under it, soft over +-EDGE */
    float rim = smooth01(depth / 0.010f);          /* 0 at the line, 1 deeper */
    float foot = smooth01(e / 0.008f);             /* the ridges' foot fades into the ground's haze */
    float gnd = smooth01(-e / (2 * EDGE) + 0.5f);  /* 1 below the horizon */
    for (int k = 0; k < 3; k++) {
        float sky = r->sky[k] + GLOW[k] * r->glow * c->glow_w + DOOM[k] * r->doom_e * c->doom_a;
        float m = RIM[k] + (MNT[k] - RIM[k]) * rim;
        m = GH[k] + (m - GH[k]) * foot;
        float above = sky + (m - sky) * mm;
        out[k] = clamp01(above + (r->ground[k] - above) * gnd);
    }
}

void somvr_env_sky_ea(float e, float a, float rgb[3]) {
    if (!isfinite(e)) e = 0;
    if (!isfinite(a)) a = 0;
    if (e > 0.5f * PI_F) e = 0.5f * PI_F;
    if (e < -0.5f * PI_F) e = -0.5f * PI_F;
    env_col c = col_terms(a);
    env_row r = row_terms(e);
    mix_terms(e, &c, &r, rgb);
}

void somvr_env_sky(const float d[3], float rgb[3]) {
    float n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    float x = 0, y = 0, z = -1;
    if (n > 1e-9f && isfinite(n)) x = d[0] / n, y = d[1] / n, z = d[2] / n;
    somvr_env_sky_ea(asinf(clamp01(0.5f * (y + 1)) * 2 - 1), atan2f(x, -z), rgb);
}

/* ---- the image ----------------------------------------------------------------------------------------------------- */
static float dither(uint32_t x, uint32_t y) { /* a fixed per-texel value in [0, 1) */
    uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u;
    h ^= h >> 15, h *= 0x2c1b3c6du, h ^= h >> 12, h *= 0x297a2d39u, h ^= h >> 15;
    return (float)(h >> 8) * (1.0f / 16777216.0f);
}
static float srgb_to_linear(float c) { return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }
static void put(uint8_t *p, const float rgb[3], float d, int bgra, int srgb) {
    for (int k = 0; k < 3; k++) {
        float v = srgb ? rgb[k] : srgb_to_linear(rgb[k]);
        int b = (int)floorf(v * 255.0f + d);
        p[bgra ? 2 - k : k] = (uint8_t)(b < 0 ? 0 : b > 255 ? 255 : b);
    }
    p[3] = 255;
}

void somvr_env_sky_rect(int kind, int i, int32_t rc[4]) {
    if (kind == SOMVR_ENV_KIND_EQ) {
        rc[0] = rc[1] = 0, rc[2] = SOMVR_ENV_EQ_W, rc[3] = SOMVR_ENV_EQ_H;
        return;
    }
    if (i < 0 || i > 5) i = 0;
    rc[0] = (i % 3) * SOMVR_ENV_TILE + 1, rc[1] = (i / 3) * SOMVR_ENV_TILE + 1;
    rc[2] = rc[3] = SOMVR_ENV_TILE - 2;
}

void somvr_env_dim_rect(int kind, int32_t rc[4]) {
    rc[0] = 1, rc[1] = (int32_t)somvr_env_sky_h(kind) + 2, rc[2] = 2, rc[3] = 2;
}

void somvr_env_image(uint8_t *px, int kind, int bgra, int srgb, float dim) {
    const uint32_t w = somvr_env_img_w(kind), sh = somvr_env_sky_h(kind);
    float rgb[3];
    if (kind == SOMVR_ENV_KIND_EQ) {
        env_col cols[SOMVR_ENV_EQ_W]; /* the azimuth's part once per column (12 KB of stack) */
        for (uint32_t x = 0; x < w; x++) cols[x] = col_terms(((float)x + 0.5f) / (float)w * 2 * PI_F - PI_F);
        for (uint32_t y = 0; y < sh; y++) {
            float e = (0.5f - ((float)y + 0.5f) / (float)sh) * PI_F;
            env_row r = row_terms(e);
            for (uint32_t x = 0; x < w; x++) {
                mix_terms(e, &cols[x], &r, rgb);
                put(px + 4 * ((size_t)y * w + x), rgb, dither(x, y), bgra, srgb);
            }
        }
    } else {
        const int T = SOMVR_ENV_TILE;
        for (int i = 0; i < 6; i++) {
            somvr_env_face f;
            somvr_env_face_get(i, &f);
            uint32_t x0 = (uint32_t)((i % 3) * T), y0 = (uint32_t)((i / 3) * T);
            for (int ty = 0; ty < T; ty++)
                for (int tx = 0; tx < T; tx++) { /* the guard texels (0, T - 1) continue the face past its edge */
                    float s = ((float)(tx - 1) + 0.5f) / (float)(T - 2) * 2 - 1;
                    float t = 1 - ((float)(ty - 1) + 0.5f) / (float)(T - 2) * 2, p[3];
                    somvr_env_face_point(&f, s, t, p);
                    somvr_env_sky(p, rgb);
                    uint32_t x = x0 + (uint32_t)tx, y = y0 + (uint32_t)ty;
                    put(px + 4 * ((size_t)y * w + x), rgb, dither(x, y), bgra, srgb);
                }
        }
    }
    memcpy(px + 4 * (size_t)sh * w, px + 4 * (size_t)(sh - 1) * w, 4 * (size_t)w); /* the guard row */
    float a = clamp01(1 - (isfinite(dim) ? dim : SOMVR_ENV_DIM_DEFAULT));
    uint8_t ab = (uint8_t)lrintf(a * 255.0f);
    for (uint32_t y = sh + 1; y < sh + SOMVR_ENV_STRIP; y++)
        for (uint32_t x = 0; x < w; x++) {
            uint8_t *p = px + 4 * ((size_t)y * w + x);
            p[0] = p[1] = p[2] = 0, p[3] = ab; /* black, premultiplied */
        }
}

/* ---- the geometry -------------------------------------------------------------------------------------------------- */
void somvr_env_face_get(int i, somvr_env_face *f) {
    const float R = SOMVR_ENV_RADIUS_M, E = SOMVR_ENV_EYE_M, s = 0.70710678f, yc = 0.5f * (R - E);
    static const float X[3] = {1, 0, 0}, Y[3] = {0, 1, 0}, Z[3] = {0, 0, 1};
    memset(f, 0, sizeof *f);
    float c[3] = {0, 0, 0}, r[3], u[3], n[3], q[4] = {0, 0, 0, 1};
    memcpy(r, X, sizeof r), memcpy(u, Y, sizeof u), memcpy(n, Z, sizeof n);
    f->hw = R, f->hh = 0.5f * (R + E); /* the walls: from the floor up to the ceiling */
    switch (i) {
    default: /* 0: ahead (-z), as is */
        c[1] = yc, c[2] = -R;
        break;
    case 1: /* right (+x): -90 deg about Y */
        c[0] = R, c[1] = yc;
        r[0] = 0, r[2] = 1, n[0] = -1, n[2] = 0;
        q[1] = -s, q[3] = s;
        break;
    case 2: /* behind (+z): 180 deg about Y */
        c[1] = yc, c[2] = R;
        r[0] = -1, n[2] = -1;
        q[1] = 1, q[3] = 0;
        break;
    case 3: /* left (-x): +90 deg about Y */
        c[0] = -R, c[1] = yc;
        r[0] = 0, r[2] = -1, n[0] = 1, n[2] = 0;
        q[1] = s, q[3] = s;
        break;
    case 4: /* the ceiling: +90 deg about X (it faces down; its top toward +z) */
        c[1] = R;
        u[1] = 0, u[2] = 1, n[1] = -1, n[2] = 0;
        q[0] = s, q[3] = s;
        f->hh = R;
        break;
    case 5: /* the floor: -90 deg about X (it faces up; its top ahead, -z) */
        c[1] = -E;
        u[1] = 0, u[2] = -1, n[1] = 1, n[2] = 0;
        q[0] = -s, q[3] = s;
        f->hh = R;
        break;
    }
    memcpy(f->c, c, sizeof c), memcpy(f->r, r, sizeof r), memcpy(f->u, u, sizeof u), memcpy(f->n, n, sizeof n);
    memcpy(f->pose.q, q, sizeof q), memcpy(f->pose.p, c, sizeof c);
}

void somvr_env_face_point(const somvr_env_face *f, float s, float t, float out[3]) {
    for (int k = 0; k < 3; k++) out[k] = f->c[k] + s * f->hw * f->r[k] + t * f->hh * f->u[k];
}

static void qrot(const float q[4], const float v[3], float out[3]) { /* q v q^-1, q unit */
    float tx = 2 * (q[1] * v[2] - q[2] * v[1]), ty = 2 * (q[2] * v[0] - q[0] * v[2]), tz = 2 * (q[0] * v[1] - q[1] * v[0]);
    out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
    out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
    out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}

void somvr_env_dim_quad(const somvr_posef *eye, const somvr_fovf *fov, float dist, float margin, somvr_posef *out,
                        float size[2]) {
    const float lim = 1.5f; /* tangents of a sane fov: below 86 deg each way */
    float a[4] = {fov->l, fov->r, fov->u, fov->d}, t[4];
    for (int k = 0; k < 4; k++) t[k] = tanf(isfinite(a[k]) ? (a[k] < -lim ? -lim : a[k] > lim ? lim : a[k]) : 0);
    const float *q0 = eye->q;
    float n = sqrtf(q0[0] * q0[0] + q0[1] * q0[1] + q0[2] * q0[2] + q0[3] * q0[3]), q[4] = {0, 0, 0, 1};
    if (n > 1e-6f && isfinite(n)) q[0] = q0[0] / n, q[1] = q0[1] / n, q[2] = q0[2] / n, q[3] = q0[3] / n;
    float local[3] = {0.5f * (t[0] + t[1]) * dist, 0.5f * (t[2] + t[3]) * dist, -dist}, w[3];
    qrot(q, local, w);
    memcpy(out->q, q, sizeof q);
    for (int k = 0; k < 3; k++) out->p[k] = eye->p[k] + w[k];
    size[0] = (t[1] - t[0]) * dist * margin;
    size[1] = (t[2] - t[3]) * dist * margin;
}

/* ---- the transitions ----------------------------------------------------------------------------------------------- */
int somvr_env_step(somvr_env_trans *s, int want, int can_hold, int64_t t_ns, int *switched) {
    *switched = 0;
    if (!s->started) { /* the session's first frame: its side, from black */
        s->started = 1, s->shown = want, s->black = 1, s->t_last = t_ns, s->holds = 0;
        *switched = 1;
        return SOMVR_ENVT_DRAW;
    }
    float dt = t_ns > s->t_last ? (float)((double)(t_ns - s->t_last) * 1e-9) : 0.0f;
    if (dt > SOMVR_ENV_DT_MAX_S) dt = SOMVR_ENV_DT_MAX_S;
    s->t_last = t_ns;
    if (want == s->shown) { /* fading in (or in) */
        s->holds = 0;
        s->black -= dt / SOMVR_ENV_IN_S;
        if (!(s->black > 0)) s->black = 0;
        return SOMVR_ENVT_DRAW;
    }
    if (can_hold) { /* the other side: the shown one again, darker */
        s->black += dt / SOMVR_ENV_OUT_S;
        if (s->black < 1 - 1e-4f) return s->holds++, SOMVR_ENVT_HOLD;
    }
    s->shown = want, s->black = 1, s->switches++; /* black: the new side from here */
    *switched = 1;
    return SOMVR_ENVT_DRAW;
}

uint32_t somvr_env_level(const somvr_env_trans *s) { return (uint32_t)lrintf(clamp01(s->black) * 255.0f); }
