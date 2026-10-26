/* somvr_void.c - never a void (HG-9): the keep-alive's and the loading space's pure parts (somvr_void.h). */
#include "somvr_void.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

int somvr_void_parse_onoff(const char *v, int *bad) {
    if (!v || !*v || !strcmp(v, "off") || !strcmp(v, "0")) return 0;
    if (!strcmp(v, "on") || !strcmp(v, "1")) return 1;
    if (bad) *bad = 1;
    return 0;
}

int somvr_void_parse_ms(const char *v, int def, int lo, int hi, int *bad) {
    if (!v || !*v) return def;
    char *e = NULL;
    long n = strtol(v, &e, 10);
    if (e && !*e && e != v && n >= lo && n <= hi) return (int)n;
    if (bad) *bad = 1;
    return def;
}

/* ---- the image ----------------------------------------------------------------------------------------------------- */
static float clamp01(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }
static float smooth(float a, float b, float x) {
    float t = clamp01((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}
static uint32_t hash2(int32_t x, int32_t y) {
    uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u;
    h ^= h >> 13, h *= 0x5bd1e995u, h ^= h >> 15;
    return h;
}
static float hashf(int32_t x, int32_t y) { return (float)(hash2(x, y) & 0xffffu) / 65535.0f; }

/* The floor: flagstones 0.6 x 0.45 m in running bond with soft dark joints, each stone a little lighter or darker, a
 * fine grain, a little darker away from the centre (a pool of light); solid to 1.8 m from the centre, gone at 3 m (it
 * fades into the sky's ground). Linear values: about sRGB
 * 52 42 36 for an average stone (a touch lighter than the sky's ground below it, so it reads as a surface). */
void somvr_void_floor_texel(float u, float v, float out[4]) {
    float x = (u - 0.5f) * SOMVR_VOID_FLOOR_M, z = (v - 0.5f) * SOMVR_VOID_FLOOR_M; /* metres; v down = toward the player */
    float d = sqrtf(x * x + z * z);
    float a = 1.0f - smooth(1.8f, 3.0f, d);
    const float sw = 0.6f, sh = 0.45f, joint = 0.018f;
    float rz = z / sh;
    int row = (int)floorf(rz);
    float rx = x / sw + (row & 1 ? 0.5f : 0.0f);
    int col = (int)floorf(rx);
    float fx = (rx - (float)col) * sw, fz = (rz - (float)row) * sh; /* metres into the stone */
    float edge = fminf(fminf(fx, sw - fx), fminf(fz, sh - fz));
    float j = smooth(0.0f, joint, edge); /* 0 in the joint, 1 on the stone */
    float stone = 0.85f + 0.3f * hashf(col, row);
    float grain = 0.94f + 0.12f * hashf((int32_t)floorf(x * 160.0f), (int32_t)floorf(z * 160.0f) + 7919);
    float pool = 1.0f - 0.4f * smooth(0.4f, 2.6f, d); /* a soft pool of light around the player */
    float k = (0.45f + 0.55f * j) * stone * grain * pool;
    float lin[3] = {0.034f * k, 0.024f * k, 0.018f * k};
    for (int i = 0; i < 3; i++) out[i] = lin[i] * a;
    out[3] = a;
}

/* the arc: light i's centre in the frame (u, v) - 12 across, the ends a little higher (farther back on the ground) */
static void light_uv(int i, float *u, float *v) {
    *u = 0.06f + 0.88f * (float)i / (float)(SOMVR_VOID_NLIGHTS - 1);
    float s = (*u - 0.5f) / 0.44f;
    *v = 0.60f - 0.20f * s * s;
}

float somvr_void_light_on(int wave, int i) {
    float p = (float)wave / (float)SOMVR_VOID_WAVES * (float)(SOMVR_VOID_NLIGHTS + 4) - 2.0f; /* the crest */
    float k = ((float)i - p) / 1.3f;
    return expf(-k * k);
}

/* light i's glow at (u, v) of a frame (its full brightness) */
static float light_glow(int i, float u, float v) {
    float lu, lv;
    light_uv(i, &lu, &lv);
    float dx = (u - lu) * SOMVR_VOID_LIGHTS_W_M, dy = (v - lv) * SOMVR_VOID_LIGHTS_H_M; /* metres */
    float d2 = dx * dx + dy * dy;
    return expf(-d2 / (0.016f * 0.016f)) + 0.35f * expf(-d2 / (0.045f * 0.045f));
}
static float light_b(int wave, int level, int i) {
    return (float)level / (float)SOMVR_VOID_LEVELS * (0.22f + 0.78f * somvr_void_light_on(wave, i));
}
static void light_rgba(float sum, float out[4]) {
    static const float ember[3] = {1.0f, 0.42f, 0.11f};
    for (int c = 0; c < 3; c++) out[c] = clamp01(ember[c] * sum);
    out[3] = 0.5f * clamp01(sum); /* premultiplied with a low alpha: the glow adds light to what is behind it */
}
void somvr_void_light_texel(int wave, int level, float u, float v, float out[4]) {
    float sum = 0.0f;
    for (int i = 0; i < SOMVR_VOID_NLIGHTS; i++) sum += light_b(wave, level, i) * light_glow(i, u, v);
    light_rgba(sum, out);
}

/* linear -> a byte; sRGB through a 4096-step table (powf per channel was most of the image's time) */
static uint8_t enc(float lin, int srgb) {
    static uint8_t lut[4097];
    static int have;
    lin = clamp01(lin);
    if (!srgb) return (uint8_t)lrintf(lin * 255.0f);
    if (!have) {
        for (int i = 0; i <= 4096; i++) {
            float l = (float)i / 4096.0f, e = l <= 0.0031308f ? 12.92f * l : 1.055f * powf(l, 1.0f / 2.4f) - 0.055f;
            lut[i] = (uint8_t)lrintf(clamp01(e) * 255.0f);
        }
        have = 1;
    }
    return lut[(int)lrintf(lin * 4096.0f)];
}
static void put(uint8_t *p, const float c[4], int bgra, int srgb) {
    p[bgra ? 2 : 0] = enc(c[0], srgb), p[1] = enc(c[1], srgb), p[bgra ? 0 : 2] = enc(c[2], srgb);
    p[3] = (uint8_t)lrintf(clamp01(c[3]) * 255.0f);
}

void somvr_void_floor_rect(int32_t rc[4]) {
    rc[0] = 1, rc[1] = 1, rc[2] = SOMVR_VOID_FLOOR_PX - 2, rc[3] = SOMVR_VOID_FLOOR_PX - 2;
}
void somvr_void_lights_rect(int wave, int level, int32_t rc[4]) {
    if (wave < 0 || wave >= SOMVR_VOID_WAVES) wave = 0;
    if (level < 1) level = 1;
    if (level > SOMVR_VOID_LEVELS) level = SOMVR_VOID_LEVELS;
    int k = (level - 1) * SOMVR_VOID_WAVES + wave;
    rc[0] = (k % SOMVR_VOID_COLS) * SOMVR_VOID_LIGHT_W + 1;
    rc[1] = SOMVR_VOID_FLOOR_PX + (k / SOMVR_VOID_COLS) * SOMVR_VOID_LIGHT_H + 1;
    rc[2] = SOMVR_VOID_LIGHT_W - 2, rc[3] = SOMVR_VOID_LIGHT_H - 2;
}

void somvr_void_image(uint8_t *px, int bgra, int srgb) {
    memset(px, 0, (size_t)SOMVR_VOID_IMG_W * SOMVR_VOID_IMG_H * 4); /* premultiplied transparent black */
    float c[4];
    for (int y = 0; y < SOMVR_VOID_FLOOR_PX; y++)
        for (int x = 0; x < SOMVR_VOID_FLOOR_PX; x++) {
            somvr_void_floor_texel(((float)x + 0.5f) / SOMVR_VOID_FLOOR_PX, ((float)y + 0.5f) / SOMVR_VOID_FLOOR_PX, c);
            put(px + ((size_t)y * SOMVR_VOID_IMG_W + (size_t)x) * 4, c, bgra, srgb);
        }
    /* the lights' frames: each light's glow once per texel (somvr_void_light_texel's sum, 0.5 s per image at -O2 when
     * done per texel and frame: a hitch at the session's first menu frame), the frames summed from them */
    enum { LW = SOMVR_VOID_LIGHT_W - 2, LH = SOMVR_VOID_LIGHT_H - 2 };
    static float g[SOMVR_VOID_NLIGHTS][LH][LW];
    for (int i = 0; i < SOMVR_VOID_NLIGHTS; i++)
        for (int y = 0; y < LH; y++)
            for (int x = 0; x < LW; x++) g[i][y][x] = light_glow(i, ((float)x + 0.5f) / LW, ((float)y + 0.5f) / LH);
    for (int level = 1; level <= SOMVR_VOID_LEVELS; level++)
        for (int wave = 0; wave < SOMVR_VOID_WAVES; wave++) {
            int32_t rc[4];
            somvr_void_lights_rect(wave, level, rc);
            float b[SOMVR_VOID_NLIGHTS];
            for (int i = 0; i < SOMVR_VOID_NLIGHTS; i++) b[i] = light_b(wave, level, i);
            for (int y = 0; y < LH; y++) /* the cell's border stays transparent (the guard) */
                for (int x = 0; x < LW; x++) {
                    float sum = 0.0f;
                    for (int i = 0; i < SOMVR_VOID_NLIGHTS; i++) sum += b[i] * g[i][y][x];
                    light_rgba(sum, c);
                    put(px + ((size_t)(rc[1] + y) * SOMVR_VOID_IMG_W + (size_t)(rc[0] + x)) * 4, c, bgra, srgb);
                }
        }
}

/* ---- time, level, places ------------------------------------------------------------------------------------------ */
int somvr_void_wave(double t) {
    if (!isfinite(t)) return 0;
    double f = fmod(t, (double)SOMVR_VOID_WAVE_S);
    if (f < 0) f += SOMVR_VOID_WAVE_S;
    int w = (int)floor(f / SOMVR_VOID_WAVE_S * SOMVR_VOID_WAVES);
    return w < 0 ? 0 : w >= SOMVR_VOID_WAVES ? SOMVR_VOID_WAVES - 1 : w;
}

float somvr_void_level_step(float level, int on, float dt) {
    if (!isfinite(level)) level = 0.0f;
    if (!(dt > 0.0f)) return clamp01(level);
    if (dt > 0.1f) dt = 0.1f; /* a hitch does not jump */
    return clamp01(on ? level + dt / SOMVR_VOID_RISE_S : level - dt / SOMVR_VOID_FALL_S);
}

int somvr_void_level_index(float level) {
    if (!(level > 0.0f)) return 0;
    int k = (int)ceilf(level * SOMVR_VOID_LEVELS - 1e-4f);
    return k < 1 ? 1 : k > SOMVR_VOID_LEVELS ? SOMVR_VOID_LEVELS : k;
}

static void quat_from_basis(const float x[3], const float y[3], const float z[3], float q[4]) {
    float m00 = x[0], m11 = y[1], m22 = z[2], tr = m00 + m11 + m22; /* columns x, y, z */
    if (tr > 0.0f) {
        float s = sqrtf(tr + 1.0f) * 2.0f;
        q[3] = 0.25f * s, q[0] = (y[2] - z[1]) / s, q[1] = (z[0] - x[2]) / s, q[2] = (x[1] - y[0]) / s;
    } else if (m00 > m11 && m00 > m22) {
        float s = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
        q[3] = (y[2] - z[1]) / s, q[0] = 0.25f * s, q[1] = (y[0] + x[1]) / s, q[2] = (z[0] + x[2]) / s;
    } else if (m11 > m22) {
        float s = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
        q[3] = (z[0] - x[2]) / s, q[0] = (y[0] + x[1]) / s, q[1] = 0.25f * s, q[2] = (z[1] + y[2]) / s;
    } else {
        float s = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
        q[3] = (x[1] - y[0]) / s, q[0] = (z[0] + x[2]) / s, q[1] = (z[1] + y[2]) / s, q[2] = 0.25f * s;
    }
}

void somvr_void_floor_pose(const float head[3], float floor_y, somvr_posef *out) {
    out->q[0] = -0.70710678f, out->q[1] = 0.0f, out->q[2] = 0.0f, out->q[3] = 0.70710678f; /* +Z up, the top ahead */
    out->p[0] = head[0], out->p[1] = floor_y, out->p[2] = head[2];
}

void somvr_void_lights_pose(const float head[3], float yaw, float floor_y, somvr_posef *out) {
    if (!isfinite(yaw)) yaw = 0.0f;
    float s = sinf(yaw), c = cosf(yaw);
    float p[3] = {head[0] + SOMVR_VOID_LIGHTS_AHEAD_M * s, floor_y + SOMVR_VOID_LIGHTS_LIFT_M,
                  head[2] - SOMVR_VOID_LIGHTS_AHEAD_M * c};
    float n[3] = {head[0] - p[0], head[1] - p[1], head[2] - p[2]};
    float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (!(l > 1e-4f)) n[0] = -s, n[1] = 0.0f, n[2] = c, l = 1.0f;
    for (int i = 0; i < 3; i++) n[i] /= l;
    float r[3] = {c, 0.0f, s};
    float y[3] = {n[1] * r[2] - n[2] * r[1], n[2] * r[0] - n[0] * r[2], n[0] * r[1] - n[1] * r[0]}; /* n x r */
    quat_from_basis(r, y, n, out->q);
    memcpy(out->p, p, sizeof p);
}

float somvr_void_yaw(const float q[4]) {
    float fx = -2.0f * (q[3] * q[1] + q[0] * q[2]), fz = -1.0f + 2.0f * (q[0] * q[0] + q[1] * q[1]);
    return atan2f(fx, -fz);
}

/* ---- the in-world loading cover ------------------------------------------------------------------------------------ */
void somvr_cover_point(int k, uint32_t w, uint32_t h, int32_t *x, int32_t *y) {
    if (k < 0 || k >= SOMVR_COVER_N) k = 0;
    int i = k % SOMVR_COVER_NX, j = k / SOMVR_COVER_NX;
    *x = (int32_t)(((uint64_t)(2 * i + 1) * w) / (2 * SOMVR_COVER_NX));
    *y = (int32_t)(((uint64_t)(2 * j + 1) * h) / (2 * SOMVR_COVER_NY));
}

void somvr_cover_measure(const uint8_t *px, int n, float *black, float *dark) {
    int nb = 0, nd = 0;
    for (int i = 0; i < n; i++) {
        const uint8_t *p = px + 4 * i;
        if (p[0] > 40 || p[1] > 40 || p[2] > 40) continue;
        nb += p[3] >= 200, nd += p[3] >= 40;
    }
    *black = n > 0 ? (float)nb / (float)n : 0.0f;
    *dark = n > 0 ? (float)nd / (float)n : 0.0f;
}

float somvr_cover_pure(const uint8_t *px, int n) {
    int np = 0;
    for (int i = 0; i < n; i++) {
        const uint8_t *p = px + 4 * i;
        np += p[0] <= 8 && p[1] <= 8 && p[2] <= 8 && p[3] >= 250;
    }
    return n > 0 ? (float)np / (float)n : 0.0f;
}

void somvr_cover_reset(somvr_cover_state *s) {
    uint32_t nh = s->n_hide, ns = s->n_space;
    memset(s, 0, sizeof *s);
    s->t_black = s->t_last = -1.0;
    s->n_hide = nh, s->n_space = ns;
}

static int cover_set(somvr_cover_state *s, int hide, int space) {
    int ch = (hide != s->hide ? SOMVR_COVER_CH_HIDE : 0) | (space != s->space ? SOMVR_COVER_CH_SPACE : 0);
    s->n_hide += hide && !s->hide, s->n_space += space && !s->space;
    s->hide = hide, s->space = space;
    return ch;
}

int somvr_cover_step(somvr_cover_state *s, double t, float black, float dark, int menu, float on_s) {
    s->t_last = t, s->black = black, s->dark = dark;
    if (menu) {
        s->t_black = -1.0;
        return cover_set(s, 0, 0);
    }
    if (black >= SOMVR_COVER_BLACK_ON) {
        if (s->t_black < 0) s->t_black = t;
    } else if (black < SOMVR_COVER_LIFT) {
        s->t_black = -1.0;
    }
    int hide = s->hide ? dark >= SOMVR_COVER_DARK_OFF || black >= SOMVR_COVER_LIFT
                       : dark >= SOMVR_COVER_DARK_ON || black >= SOMVR_COVER_BLACK_ON;
    int space = s->space ? black >= SOMVR_COVER_LIFT : s->t_black >= 0 && t - s->t_black >= (double)on_s - 1e-6;
    return cover_set(s, hide || space, space);
}

int somvr_cover_age(somvr_cover_state *s, double t) {
    if (s->t_last < 0 || !(t - s->t_last >= SOMVR_COVER_STALE_S) || (!s->hide && !s->space)) return 0;
    s->t_black = -1.0;
    return cover_set(s, 0, 0);
}

/* ---- the in-world menus' backdrop ---------------------------------------------------------------------------------- */
float somvr_back_alpha(float x, float y, float w_m, float h_m) {
    float r = SOMVR_BACK_RADIUS_M, hw = 0.5f * w_m, hh = 0.5f * h_m;
    if (r > hw) r = hw;
    if (r > hh) r = hh;
    float qx = fabsf(x) - (hw - r), qy = fabsf(y) - (hh - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0, in = qx > qy ? qx : qy;
    float d = sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - r; /* < 0 inside the rounded rect */
    return SOMVR_BACK_ALPHA * smooth(0.0f, SOMVR_BACK_SOFT_M, -d);
}

void somvr_back_image(uint8_t *px, int bgra, int srgb, float w_m, float h_m) {
    static const float col[3] = {0.010f, 0.011f, 0.014f}; /* linear: about sRGB 25 26 30, a cool dark slate */
    for (int y = 0; y < SOMVR_BACK_IMG_H; y++)
        for (int x = 0; x < SOMVR_BACK_IMG_W; x++) {
            int k = x / SOMVR_BACK_CELL_W, cx = x % SOMVR_BACK_CELL_W;
            float u = ((float)cx - 0.5f) / (float)(SOMVR_BACK_CELL_W - 2), v = ((float)y - 0.5f) / (float)(SOMVR_BACK_CELL_H - 2);
            float a = (k + 1) * somvr_back_alpha((u - 0.5f) * w_m, (v - 0.5f) * h_m, w_m, h_m) / SOMVR_BACK_LEVELS;
            if (cx == 0 || cx == SOMVR_BACK_CELL_W - 1 || y == 0 || y == SOMVR_BACK_CELL_H - 1) a = 0.0f; /* the guard */
            float c[4] = {col[0] * a, col[1] * a, col[2] * a, a};
            put(px + ((size_t)y * SOMVR_BACK_IMG_W + (size_t)x) * 4, c, bgra, srgb);
        }
}

void somvr_back_rect(int k, int32_t rc[4]) {
    if (k < 1) k = 1;
    if (k > SOMVR_BACK_LEVELS) k = SOMVR_BACK_LEVELS;
    rc[0] = (k - 1) * SOMVR_BACK_CELL_W + 1, rc[1] = 1, rc[2] = SOMVR_BACK_CELL_W - 2, rc[3] = SOMVR_BACK_CELL_H - 2;
}

int somvr_back_index(float level) {
    if (!(level > 0.0f)) return 0;
    int k = (int)ceilf(level * SOMVR_BACK_LEVELS - 1e-4f);
    return k < 1 ? 1 : k > SOMVR_BACK_LEVELS ? SOMVR_BACK_LEVELS : k;
}

float somvr_back_level_step(float level, int on, float dt) {
    if (!isfinite(level)) level = 0.0f;
    if (!(dt > 0.0f)) return clamp01(level);
    if (dt > 0.1f) dt = 0.1f;
    return clamp01(on ? level + dt / SOMVR_BACK_RISE_S : level - dt / SOMVR_BACK_FALL_S);
}

void somvr_back_place(const somvr_posef *board, const float bsize[2], somvr_posef *out, float size[2]) {
    const float *q = board->q; /* the board's +Z (its normal toward the eyes) in the space */
    float z[3] = {2.0f * (q[0] * q[2] + q[3] * q[1]), 2.0f * (q[1] * q[2] - q[3] * q[0]),
                  1.0f - 2.0f * (q[0] * q[0] + q[1] * q[1])};
    *out = *board;
    for (int i = 0; i < 3; i++) out->p[i] = board->p[i] - SOMVR_BACK_BEHIND_M * z[i];
    size[0] = bsize[0] + 2.0f * SOMVR_BACK_MARGIN_M, size[1] = bsize[1] + 2.0f * SOMVR_BACK_MARGIN_M;
}

float somvr_back_parse_dim(const char *v, float def, int *bad) {
    if (!v || !*v) return def;
    char *e = NULL;
    float f = strtof(v, &e);
    if (e && !*e && e != v && f >= 0.0f && f <= 0.9f) return f;
    if (bad) *bad = 1;
    return def;
}
