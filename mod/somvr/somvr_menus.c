/* somvr_menus.c - see somvr_menus.h */
#include "somvr_menus.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- small vector helpers ------------------------------------------------------------------------------------------ */
static void qrot(const float q[4], const float v[3], float out[3]) { /* v' = v + 2w (u x v) + 2 u x (u x v) */
    float t[3] = {2 * (q[1] * v[2] - q[2] * v[1]), 2 * (q[2] * v[0] - q[0] * v[2]), 2 * (q[0] * v[1] - q[1] * v[0])};
    out[0] = v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]);
    out[1] = v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]);
    out[2] = v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]);
}
static float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void cross3(const float a[3], const float b[3], float o[3]) {
    float x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0];
    o[0] = x, o[1] = y, o[2] = z;
}
static int norm3(float v[3]) {
    float n = sqrtf(dot3(v, v));
    if (!(n > 1e-6f)) return 0; /* NaN too */
    v[0] /= n, v[1] /= n, v[2] /= n;
    return 1;
}
static int unit_quat(const float q[4]) {
    float n = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    return n > 0.9f && n < 1.1f; /* NaN fails too */
}
/* the rotation whose columns are x, y, z (orthonormal, right-handed) */
static void quat_of_basis(const float x[3], const float y[3], const float z[3], float q[4]) {
    float tr = x[0] + y[1] + z[2];
    if (tr > 0) {
        float s = sqrtf(tr + 1) * 2;
        q[3] = 0.25f * s, q[0] = (y[2] - z[1]) / s, q[1] = (z[0] - x[2]) / s, q[2] = (x[1] - y[0]) / s;
    } else if (x[0] > y[1] && x[0] > z[2]) {
        float s = sqrtf(1 + x[0] - y[1] - z[2]) * 2;
        q[3] = (y[2] - z[1]) / s, q[0] = 0.25f * s, q[1] = (y[0] + x[1]) / s, q[2] = (z[0] + x[2]) / s;
    } else if (y[1] > z[2]) {
        float s = sqrtf(1 + y[1] - x[0] - z[2]) * 2;
        q[3] = (z[0] - x[2]) / s, q[0] = (y[0] + x[1]) / s, q[1] = 0.25f * s, q[2] = (z[1] + y[2]) / s;
    } else {
        float s = sqrtf(1 + z[2] - x[0] - y[1]) * 2;
        q[3] = (x[1] - y[0]) / s, q[0] = (z[0] + x[2]) / s, q[1] = (z[1] + y[2]) / s, q[2] = 0.25f * s;
    }
    float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int i = 0; i < 4; i++) q[i] /= n;
}
static int on_word(const char *v, int *bad, const char *on1, int on_val) {
    if (!v || !*v || !strcmp(v, "off") || !strcmp(v, "0")) return 0;
    if (!strcmp(v, on1) || !strcmp(v, "on") || !strcmp(v, "1")) return on_val;
    if (bad) *bad = 1;
    return 0;
}
/* up to n comma-separated decimals into out (all or nothing), each within [lo[i], hi[i]]; 1 = taken */
static int parse_floats(const char *v, int n, float *out, const float *lo, const float *hi) {
    float t[8];
    const char *p = v;
    for (int i = 0; i < n; i++) {
        char *e;
        double d = strtod(p, &e);
        if (e == p || !(d >= lo[i] && d <= hi[i])) return 0;
        t[i] = (float)d;
        p = e;
        if (i + 1 < n) {
            if (*p != ',') return 0;
            p++;
        }
    }
    if (*p) return 0;
    memcpy(out, t, sizeof(float) * (size_t)n);
    return 1;
}

/* ==== 1. the menu board ========================================================================================== */
int somvr_mn_parse(const char *v, int *bad) { return on_word(v, bad, "front", SOMVR_MN_FRONT); }

void somvr_mn_parse_quad(const char *v, float out[3], int *bad) {
    static const float lo[3] = {0.3f, 0.2f, -1}, hi[3] = {3, 4, 1};
    out[0] = SOMVR_MN_DIST_M, out[1] = SOMVR_MN_WIDTH_M, out[2] = SOMVR_MN_DROP_M;
    if (!v || !*v) return;
    if (!parse_floats(v, 3, out, lo, hi)) {
        out[0] = SOMVR_MN_DIST_M, out[1] = SOMVR_MN_WIDTH_M, out[2] = SOMVR_MN_DROP_M;
        if (bad) *bad = 1;
    }
}

void somvr_mn_board_pose_gaze(const somvr_posef eye[2], float dist, float drop, float down_max_deg, somvr_posef *out) {
    somvr_mn_board_pose_gaze2(eye, dist, drop, down_max_deg, 0.0f, out);
}
void somvr_mn_board_pose_gaze2(const somvr_posef eye[2], float dist, float drop, float down_max_deg, float up_max_deg,
                               somvr_posef *out) {
    static const float fwd0[3] = {0, 0, -1}, up0[3] = {0, 1, 0};
    float f[3], u[3], mid[3];
    qrot(eye[0].q, fwd0, f);
    float hx = f[0], hz = f[2];
    if (hx * hx + hz * hz < 1e-6f) { /* straight up or down: the head's up vector points ahead (down) or back (up) */
        qrot(eye[0].q, up0, u);
        hx = f[1] < 0 ? u[0] : -u[0], hz = f[1] < 0 ? u[2] : -u[2];
    }
    float n = sqrtf(hx * hx + hz * hz);
    if (!(n > 1e-6f)) hx = 0, hz = -1, n = 1;
    hx /= n, hz /= n;
    for (int i = 0; i < 3; i++) mid[i] = 0.5f * (eye[0].p[i] + eye[1].p[i]);
    if (!(dot3(mid, mid) >= 0)) mid[0] = mid[1] = mid[2] = 0; /* NaN */
    /* SOMVR_XR_MENU_PITCH: a head looking down (an artifact at the feet) gets the board down its gaze, at most
     * down_max_deg below the level (0 = level: build 16's) */
    float pb = 0;
    if (down_max_deg > 0 && f[1] < 0 && f[1] == f[1]) {
        pb = asinf(fmaxf(-1.f, f[1]));
        float lim = -down_max_deg * 3.14159265f / 180.f;
        if (pb < lim) pb = lim;
    }
    if (up_max_deg > 0 && f[1] > 0 && f[1] == f[1]) { /* build 19: SOMVR_XR_MENU_PITCH_UP, a head looking up */
        pb = asinf(fminf(1.f, f[1]));
        float lim = up_max_deg * 3.14159265f / 180.f;
        if (pb > lim) pb = lim;
    }
    float ch = cosf(pb), sh = sinf(pb);
    out->p[0] = mid[0] + dist * ch * hx, out->p[1] = mid[1] + dist * sh - drop, out->p[2] = mid[2] + dist * ch * hz;
    float z[3] = {mid[0] - out->p[0], mid[1] - out->p[1], mid[2] - out->p[2]}, y[3], x[3];
    if (!norm3(z)) z[0] = -hx, z[1] = 0, z[2] = -hz;
    float k = dot3(up0, z);
    y[0] = -k * z[0], y[1] = 1 - k * z[1], y[2] = -k * z[2];
    if (!norm3(y)) y[0] = 0, y[1] = 0, y[2] = -1; /* (looking straight along the up axis: not reachable here) */
    cross3(y, z, x);
    quat_of_basis(x, y, z, out->q);
}

void somvr_mn_board_pose(const somvr_posef eye[2], float dist, float drop, somvr_posef *out) {
    somvr_mn_board_pose_gaze(eye, dist, drop, 0.0f, out);
}

/* ==== 2. the wrist minimap ======================================================================================= */
int somvr_mm_parse(const char *v, int *bad) { return on_word(v, bad, "wrist", SOMVR_MM_WRIST); }

void somvr_mm_parse_rect(const char *v, int out[4], int *bad) {
    static const int def[4] = SOMVR_MM_RECT_DEFAULT;
    static const float lo[4] = {0, 0, 16, 16}, hi[4] = {1904, 1064, 1920, 1080};
    memcpy(out, def, sizeof def);
    if (!v || !*v) return;
    float f[4];
    if (!parse_floats(v, 4, f, lo, hi) || f[0] + f[2] > 1920 || f[1] + f[3] > 1080) {
        if (bad) *bad = 1;
        return;
    }
    for (int i = 0; i < 4; i++) out[i] = (int)lroundf(f[i]);
}

int somvr_mm_rect_for(const int r[4], uint32_t w, uint32_t h, int out[4]) {
    if (w < 16 || h < 16 || r[2] <= 0 || r[3] <= 0) return 0;
    float sx = (float)w / 1920.0f, sy = (float)h / 1080.0f;
    int x0 = (int)lroundf((float)r[0] * sx), y0 = (int)lroundf((float)r[1] * sy);
    int x1 = (int)lroundf((float)(r[0] + r[2]) * sx), y1 = (int)lroundf((float)(r[1] + r[3]) * sy);
    x0 = x0 < 0 ? 0 : x0 > (int)w - 1 ? (int)w - 1 : x0, y0 = y0 < 0 ? 0 : y0 > (int)h - 1 ? (int)h - 1 : y0;
    x1 = x1 > (int)w ? (int)w : x1, y1 = y1 > (int)h ? (int)h : y1;
    if (x1 <= x0) x1 = x0 + 1;
    if (y1 <= y0) y1 = y0 + 1;
    out[0] = x0, out[1] = y0, out[2] = x1 - x0, out[3] = y1 - y0;
    return 1;
}

void somvr_mm_cfg_default(somvr_mm_cfg *c) {
    *c = (somvr_mm_cfg){.size_m = 0.10f, .back_m = 0.08f, .out_m = 0.045f, .face_on = 0.40f, .face_off = 0.20f};
}

void somvr_mm_parse_quad(const char *v, somvr_mm_cfg *c, int *bad) {
    static const float lo[3] = {0.03f, -0.2f, -0.1f}, hi[3] = {0.3f, 0.3f, 0.2f};
    if (!v || !*v) return;
    float f[3];
    if (!parse_floats(v, 3, f, lo, hi)) {
        if (bad) *bad = 1;
        return;
    }
    c->size_m = f[0], c->back_m = f[1], c->out_m = f[2];
}

int somvr_mm_pose(const somvr_posef *grip, int hand, const somvr_mm_cfg *c, somvr_posef *out) {
    if (!unit_quat(grip->q)) return 0;
    static const float ex[3] = {1, 0, 0}, ey[3] = {0, 1, 0}, ez[3] = {0, 0, 1};
    float xg[3], yg[3], zg[3], x[3], y[3], z[3];
    qrot(grip->q, ex, xg), qrot(grip->q, ey, yg), qrot(grip->q, ez, zg);
    float s = hand == SOMVR_HAND_L ? -1.0f : 1.0f; /* the back of the hand: the left grip's -X, the right grip's +X */
    for (int i = 0; i < 3; i++) z[i] = s * xg[i], x[i] = s * zg[i], y[i] = -yg[i];
    quat_of_basis(x, y, z, out->q);
    for (int i = 0; i < 3; i++) out->p[i] = grip->p[i] + c->back_m * zg[i] + c->out_m * z[i];
    return 1;
}

int somvr_mm_facing(const somvr_posef *face, const float eye[3], const somvr_mm_cfg *c, int was) {
    static const float ez[3] = {0, 0, 1};
    float n[3], d[3] = {eye[0] - face->p[0], eye[1] - face->p[1], eye[2] - face->p[2]};
    qrot(face->q, ez, n);
    if (!norm3(d)) return 0;
    float k = dot3(n, d);
    return was ? k > c->face_off : k > c->face_on;
}

/* ==== 2b. the HUD's tiles: the wrist HUD and the prompt band ====================================================== */
int somvr_wh_parse(const char *v, int *bad) { return on_word(v, bad, "on", 1); }
int somvr_pb_parse(const char *v, int *bad) { return on_word(v, bad, "on", 1); }

static int ht_rects(const char *v, somvr_ht_set *o) {
    static const float lo[4] = {0, 0, 8, 8}, hi[4] = {1912, 1072, 1920, 1080};
    int n = 0, row = 0;
    const char *p = v;
    while (*p) {
        size_t len = strcspn(p, ";|");
        char one[64];
        float f[4];
        if (n == SOMVR_HT_MAX || len == 0 || len >= sizeof one) return 0;
        memcpy(one, p, len), one[len] = 0;
        if (!parse_floats(one, 4, f, lo, hi) || f[0] + f[2] > 1920 || f[1] + f[3] > 1080) return 0;
        for (int i = 0; i < 4; i++) o->r[n][i] = (int)lroundf(f[i]);
        o->row[n++] = row;
        p += len;
        if (*p) {
            row += *p == '|';
            if (!*++p) return 0; /* a trailing separator */
        }
    }
    return o->n = n;
}
int somvr_ht_parse(const char *v, const char *def, somvr_ht_set *out, int *bad) {
    memset(out, 0, sizeof *out);
    if (v && *v && ht_rects(v, out)) return out->n;
    if (v && *v && bad) *bad = 1;
    memset(out, 0, sizeof *out);
    return ht_rects(def, out);
}
int somvr_ht_scale(const somvr_ht_set *in, uint32_t w, uint32_t h, somvr_ht_set *out) {
    somvr_ht_set t = *in;
    for (int i = 0; i < in->n; i++)
        if (!somvr_mm_rect_for(in->r[i], w, h, t.r[i])) return out->n = 0, 0;
    *out = t;
    return 1;
}

void somvr_pb_cfg_default(somvr_pb_cfg *c) { *c = (somvr_pb_cfg){.dist_m = 1.3f, .line_deg = 0.85f, .down_deg = 15.0f}; }
void somvr_pb_parse_quad(const char *v, somvr_pb_cfg *c, int *bad) {
    static const float lo[3] = {0.4f, 0.3f, -30}, hi[3] = {3, 2, 50};
    if (!v || !*v) return;
    float f[3];
    if (!parse_floats(v, 3, f, lo, hi)) {
        if (bad) *bad = 1;
        return;
    }
    c->dist_m = f[0], c->line_deg = f[1], c->down_deg = f[2];
}
float somvr_pb_width(int w_px, float line_deg, float dist) {
    float half = 0.5f * (float)w_px * line_deg / 18.0f * 3.14159265f / 180.0f;
    if (half > 1.4f) half = 1.4f; /* (80 deg: a sane bound) */
    return 2 * dist * tanf(half);
}

int somvr_hud_layout_make(uint32_t w, uint32_t h, const int *mm, const somvr_ht_set *wt, int mirror,
                          const somvr_ht_set *clear, const somvr_ht_set *bt, somvr_hud_layout *o) {
    memset(o, 0, sizeof *o);
    const int G = SOMVR_HT_GAP, W = (int)w, H = (int)h;
    int nw = mm && wt ? wt->n : 0, nb = bt ? bt->n : 0;
    /* the watch: the column (the tiles top to bottom, left-aligned), a gap, the minimap (mirror: the map first) */
    int colw = 0, colh = 0, ww = 0, wh = 0;
    for (int i = 0; i < nw; i++) colw = wt->r[i][2] > colw ? wt->r[i][2] : colw, colh += wt->r[i][3] + (i ? G : 0);
    if (mm) {
        ww = mm[2] + (nw ? G + colw : 0), wh = mm[3] > colh ? mm[3] : colh;
        if (wh > H) return 0;
        int mx = nw && !mirror ? colw + G : 0, cx = mirror ? mm[2] + G : 0, cy = (wh - colh) / 2;
        int k = o->ntile++;
        memcpy(o->src[k], mm, sizeof o->src[k]);
        o->dst[k][0] = W + mx, o->dst[k][1] = (wh - mm[3]) / 2;
        o->mm[0] = o->dst[k][0], o->mm[1] = o->dst[k][1], o->mm[2] = mm[2], o->mm[3] = mm[3];
        for (int i = 0; i < nw; i++) {
            k = o->ntile++;
            memcpy(o->src[k], wt->r[i], sizeof o->src[k]);
            o->dst[k][0] = W + cx, o->dst[k][1] = cy;
            cy += wt->r[i][3] + G;
        }
        for (int i = 0; nw && clear && i < clear->n; i++) memcpy(o->clear[o->nclear++], clear->r[i], sizeof o->clear[0]);
        o->watch[0] = W, o->watch[1] = 0, o->watch[2] = ww, o->watch[3] = wh;
        o->extra_w = (uint32_t)ww;
    }
    /* the band: its rows top to bottom; a row's one tile centred, two or more spread from its left end to its right */
    if (nb) {
        int nrow = 0, rw[SOMVR_HT_MAX] = {0}, rh[SOMVR_HT_MAX] = {0}, rn[SOMVR_HT_MAX] = {0}, bw = 0, bh = 0;
        for (int i = 0; i < nb; i++) {
            int r = bt->row[i] < 0 ? 0 : bt->row[i] >= SOMVR_HT_MAX ? SOMVR_HT_MAX - 1 : bt->row[i];
            rw[r] += bt->r[i][2] + (rn[r] ? G : 0), rh[r] = bt->r[i][3] > rh[r] ? bt->r[i][3] : rh[r], rn[r]++;
            nrow = r + 1 > nrow ? r + 1 : nrow;
        }
        for (int r = 0; r < nrow; r++) bw = rw[r] > bw ? rw[r] : bw, bh += rh[r] + (rn[r] && bh ? G : 0);
        int bx = W, by = ww ? wh + G : 0;
        if (by + bh > H) bx = W + (ww ? ww + G : 0), by = 0; /* not below the watch: beside it */
        if (bh <= H) {
            int y = by;
            for (int r = 0; r < nrow; r++) {
                if (!rn[r]) continue;
                int sw = rw[r] - G * (rn[r] - 1), j = 0, x = bx;
                for (int i = 0; i < nb; i++) {
                    int ri = bt->row[i] < 0 ? 0 : bt->row[i] >= SOMVR_HT_MAX ? SOMVR_HT_MAX - 1 : bt->row[i];
                    if (ri != r) continue;
                    int k = o->ntile++;
                    memcpy(o->src[k], bt->r[i], sizeof o->src[k]);
                    if (rn[r] == 1) x = bx + (bw - bt->r[i][2]) / 2;
                    else if (j == rn[r] - 1) x = bx + bw - bt->r[i][2]; /* the last one at the right end */
                    o->dst[k][0] = x, o->dst[k][1] = y;
                    if (rn[r] > 1) x += bt->r[i][2] + (bw - sw) / (rn[r] - 1);
                    j++;
                }
                y += rh[r] + G;
            }
            o->band[0] = bx, o->band[1] = by, o->band[2] = bw, o->band[3] = bh;
            uint32_t e = (uint32_t)(bx + bw - W);
            o->extra_w = e > o->extra_w ? e : o->extra_w;
        }
    }
    return o->ntile > 0;
}

int somvr_hud_split(uint32_t w, uint32_t h, const somvr_hud_layout *l, int32_t out[][6], int cap) {
    const int W = (int)w, H = (int)h, nt = l->ntile + l->nclear;
    int hole[SOMVR_HL_MAX + SOMVR_HT_MAX][4]; /* left out in place: the tiles and the clear rects */
    for (int i = 0; i < nt; i++) memcpy(hole[i], i < l->ntile ? l->src[i] : l->clear[i - l->ntile], sizeof hole[i]);
    int ys[2 + 2 * (SOMVR_HL_MAX + SOMVR_HT_MAX)], ny = 0, n = 0;
    ys[ny++] = 0, ys[ny++] = H;
    for (int i = 0; i < nt; i++) ys[ny++] = hole[i][1], ys[ny++] = hole[i][1] + hole[i][3];
    for (int i = 1; i < ny; i++) /* sorted, unique */
        for (int j = i; j > 0 && ys[j] < ys[j - 1]; j--) {
            int t = ys[j];
            ys[j] = ys[j - 1], ys[j - 1] = t;
        }
    int m = 0;
    for (int i = 0; i < ny; i++)
        if (ys[i] >= 0 && ys[i] <= H && (!m || ys[i] != ys[m - 1])) ys[m++] = ys[i];
    for (int b = 0; b + 1 < m; b++) {
        int y0 = ys[b], y1 = ys[b + 1], x = 0;
        while (x < W) { /* the next gap: from x up to the nearest tile starting at or after it in this band */
            int blocked = 1;
            while (blocked) { /* skip the tiles covering x */
                blocked = 0;
                for (int i = 0; i < nt; i++) {
                    const int *s = hole[i];
                    if (s[1] <= y0 && s[1] + s[3] >= y1 && s[0] <= x && s[0] + s[2] > x) x = s[0] + s[2], blocked = 1;
                }
            }
            if (x >= W) break;
            int x1 = W;
            for (int i = 0; i < nt; i++) {
                const int *s = hole[i];
                if (s[1] <= y0 && s[1] + s[3] >= y1 && s[0] > x && s[0] < x1) x1 = s[0];
            }
            int k = -1; /* the same columns right above it: grow that one */
            for (int j = 0; j < n; j++)
                if (out[j][0] == x && out[j][4] == x1 - x && out[j][1] + out[j][5] == y0 && out[j][0] == out[j][2] &&
                    out[j][1] == out[j][3])
                    k = j;
            if (k >= 0) {
                out[k][5] += y1 - y0;
            } else {
                if (n == cap) return 0;
                int32_t *o = out[n++];
                o[0] = o[2] = x, o[1] = o[3] = y0, o[4] = x1 - x, o[5] = y1 - y0;
            }
            x = x1;
        }
    }
    for (int i = 0; i < l->ntile; i++) {
        if (n == cap) return 0;
        int32_t *o = out[n++];
        o[0] = l->src[i][0], o[1] = l->src[i][1], o[2] = l->dst[i][0], o[3] = l->dst[i][1], o[4] = l->src[i][2],
        o[5] = l->src[i][3];
    }
    return n;
}

/* ==== 3. touch ===================================================================================================== */
int somvr_tc_parse(const char *v, int *bad) { return on_word(v, bad, "on", SOMVR_TC_ON); }

void somvr_tc_cfg_default(somvr_tc_cfg *c) {
    *c = (somvr_tc_cfg){.reach_m = 0.05f,
                        .hover_m = 0.03f,
                        .from_m = 0.02f,
                        .press_m = 0.005f,
                        .release_m = 0.015f,
                        .behind_m = 0.08f,
                        .window_s = 0.4f,
                        .dwell_s = 0.06f,
                        .refractory_s = 0.25f,
                        .min_in_mps = 0.05f,
                        .max_tan_mps = 0.4f,
                        .margin = 0.01f,
                        .min_cutoff = 1.0f,
                        .beta = 30.0f,
                        .d_cutoff = 1.0f};
}

void somvr_tc_tip(const somvr_posef *aim, float reach_m, float out[3]) {
    static const float fwd[3] = {0, 0, -1};
    float d[3];
    qrot(aim->q, fwd, d);
    for (int i = 0; i < 3; i++) out[i] = aim->p[i] + reach_m * d[i];
}

void somvr_tc_reset(somvr_tc_state *s) {
    memset(s, 0, sizeof *s);
    s->hand = -1;
    for (int h = 0; h < 2; h++) s->h[h].front_t = s->h[h].cross_t = -1;
}

static float oe_alpha(float cutoff, float dt) {
    float tau = 1.0f / (2.0f * 3.14159265f * cutoff);
    return 1.0f / (1.0f + tau / dt);
}
/* the hand back to IDLE; a held press ends (edges) */
static void tc_idle(somvr_tc_state *s, int h, uint32_t *edges) {
    somvr_tc_hand *a = &s->h[h];
    if (a->state == SOMVR_TCS_DOWN) *edges |= SOMVR_TCE_CANCEL | SOMVR_TCE_RELEASE, s->ups++, s->cancels++;
    if (a->state != SOMVR_TCS_IDLE) *edges |= SOMVR_TCE_LEAVE;
    a->state = SOMVR_TCS_IDLE;
    a->have_f = 0;
}

void somvr_tc_update(somvr_tc_state *s, const somvr_tc_cfg *c, const somvr_tc_in *in, somvr_tc_out *o) {
    memset(o, 0, sizeof *o);
    o->hand = -1;
    float local[2][3] = {{0}};
    for (int h = 0; h < 2; h++) {
        somvr_tc_hand *a = &s->h[h];
        uint32_t e = 0;
        if (a->quad != in->quad) { /* another target: start over */
            tc_idle(s, h, &e);
            a->front_t = -1;
            a->quad = in->quad;
        }
        if (!in->ok[h] || in->quad == 0 || !unit_quat(in->quad_pose.q) || !(in->quad_size[0] > 0) ||
            !(in->quad_size[1] > 0)) {
            tc_idle(s, h, &e);
            a->front_t = -1;
            o->edges |= e;
            continue;
        }
        /* the tip in the quad's frame: R^T (tip - p) */
        float d[3] = {in->tip[h][0] - in->quad_pose.p[0], in->tip[h][1] - in->quad_pose.p[1],
                      in->tip[h][2] - in->quad_pose.p[2]};
        float qi[4] = {-in->quad_pose.q[0], -in->quad_pose.q[1], -in->quad_pose.q[2], in->quad_pose.q[3]};
        qrot(qi, d, local[h]);
        float depth = local[h][2];
        float u = local[h][0] / in->quad_size[0] + 0.5f, v = 0.5f - local[h][1] / in->quad_size[1];
        int in_rect = u >= -c->margin && u <= 1 + c->margin && v >= -c->margin && v <= 1 + c->margin;
        if (!(depth == depth)) { /* NaN */
            tc_idle(s, h, &e);
            o->edges |= e;
            continue;
        }
        double dt = a->t > 0 && in->t > a->t ? in->t - a->t : 1.0 / 90;
        float vin = a->state != SOMVR_TCS_IDLE || a->have_f ? (a->depth - depth) / (float)dt : 0; /* toward it: + */
        if (depth >= c->from_m) a->front_t = in->t;
        if (depth > 0) a->cross_t = in->t;
        /* the point on the surface (metres from the quad's centre, x right, y up), One Euro while targeted */
        float px = local[h][0], py = local[h][1], tan = 0;
        if (a->state != SOMVR_TCS_IDLE && a->have_f && dt > 0 && dt < 0.25) {
            float fdt = (float)dt, dx = (px - a->f[0]) / fdt, dy = (py - a->f[1]) / fdt;
            float ad = oe_alpha(c->d_cutoff, fdt);
            a->df[0] += ad * (dx - a->df[0]), a->df[1] += ad * (dy - a->df[1]);
            float sp = sqrtf(a->df[0] * a->df[0] + a->df[1] * a->df[1]);
            float ax = oe_alpha(c->min_cutoff + c->beta * sp, fdt);
            a->f[0] += ax * (px - a->f[0]), a->f[1] += ax * (py - a->f[1]);
            tan = sp;
        } else {
            a->f[0] = px, a->f[1] = py, a->df[0] = a->df[1] = 0, a->have_f = 1;
        }
        float fu = a->f[0] / in->quad_size[0] + 0.5f, fv = 0.5f - a->f[1] / in->quad_size[1];
        a->uv[0] = fu < 0 ? 0 : fu > 1 ? 1 : fu, a->uv[1] = fv < 0 ? 0 : fv > 1 ? 1 : fv;
        o->near_[h] = in_rect && depth < 0.10f && depth > -c->behind_m;
        int other_down = s->h[1 - h].state == SOMVR_TCS_DOWN;
        switch (a->state) {
        case SOMVR_TCS_IDLE:
            if (in_rect && depth > 0 && depth < c->hover_m && a->front_t >= 0 && in->t - a->front_t <= c->window_s) {
                a->state = SOMVR_TCS_HOVER, a->hover_t = in->t, e |= SOMVR_TCE_HOVER;
                a->f[0] = px, a->f[1] = py, a->df[0] = a->df[1] = 0, a->have_f = 1;
                a->uv[0] = u < 0 ? 0 : u > 1 ? 1 : u, a->uv[1] = v < 0 ? 0 : v > 1 ? 1 : v;
            } else {
                a->have_f = 0;
            }
            break;
        case SOMVR_TCS_HOVER:
            if (!in_rect || depth > c->hover_m + 0.01f || depth < -c->behind_m) {
                tc_idle(s, h, &e);
            } else if (depth <= -c->press_m && in->t - a->hover_t >= c->dwell_s && vin >= c->min_in_mps &&
                       tan <= c->max_tan_mps && in->t >= a->free_t && !other_down && a->cross_t >= 0 &&
                       in->t - a->cross_t <= c->window_s) {
                a->state = SOMVR_TCS_DOWN, e |= SOMVR_TCE_PRESS, s->downs++;
            }
            break;
        case SOMVR_TCS_DOWN:
            if (depth < -c->behind_m) {
                tc_idle(s, h, &e);
            } else if (depth >= c->release_m) {
                a->state = SOMVR_TCS_HOVER, a->hover_t = in->t, a->free_t = in->t + c->refractory_s;
                e |= SOMVR_TCE_RELEASE, s->ups++;
            }
            break;
        }
        a->depth = depth;
        a->t = in->t;
        o->edges |= e;
    }
    /* the touching hand: the pressing one, else the hoverer nearer the surface */
    int pick = -1;
    for (int h = 0; h < 2; h++)
        if (s->h[h].state == SOMVR_TCS_DOWN) pick = h;
    if (pick < 0)
        for (int h = 0; h < 2; h++)
            if (s->h[h].state == SOMVR_TCS_HOVER && (pick < 0 || s->h[h].depth < s->h[pick].depth)) pick = h;
    s->hand = pick;
    if (pick >= 0) {
        const somvr_tc_hand *a = &s->h[pick];
        o->hand = pick, o->on = 1, o->down = a->state == SOMVR_TCS_DOWN;
        o->uv[0] = a->uv[0], o->uv[1] = a->uv[1], o->depth = a->depth;
        memcpy(o->tip, in->tip[pick], sizeof o->tip);
        /* the smoothed point back on the surface */
        float lp[3] = {(a->uv[0] - 0.5f) * in->quad_size[0], (0.5f - a->uv[1]) * in->quad_size[1], 0}, w[3];
        qrot(in->quad_pose.q, lp, w);
        for (int i = 0; i < 3; i++) o->surf[i] = in->quad_pose.p[i] + w[i];
    }
}

/* ==== 4. test hands (SOMVR_XR_TEST) ================================================================================== */
static void qmul(const float a[4], const float b[4], float o[4]) {
    float x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    float y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    float z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    float w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    o[0] = x, o[1] = y, o[2] = z, o[3] = w;
}
static void qaxis(int axis, float deg, float o[4]) {
    float h = 0.5f * deg * 0.017453293f;
    o[0] = o[1] = o[2] = 0, o[axis] = sinf(h), o[3] = cosf(h);
}
static int xt_word(const char **p, char *w) { /* the next word (up to 15 characters) */
    int n = 0;
    if (sscanf(*p, "%15s%n", w, &n) != 1) return 0;
    *p += n;
    return 1;
}

int somvr_xt_parse(const char *line, somvr_xt_cmd *c) {
    memset(c, 0, sizeof *c);
    char w0[16] = "", w1[16] = "", w2[16] = "";
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p || *p == '#' || *p == '\n' || *p == '\r' || !xt_word(&p, w0)) return 0;
    if (!strcmp(w0, "dump")) { /* build 21: "dump [K]", K = 1..8 stereo frames ahead (default 1) */
        int k = 0;
        c->v[0] = 1;
        if (sscanf(p, "%f%n", &c->v[0], &k) == 1 && !(c->v[0] >= 1 && c->v[0] <= 8 && c->v[0] == (float)(int)c->v[0]))
            return -1;
        return c->op = SOMVR_XT_DUMP, 1;
    }
    if (!strcmp(w0, "handsize")) { /* build 17 */
        int k = 0;
        if (sscanf(p, "%f%n", &c->v[0], &k) != 1 || !(c->v[0] >= 0.8f && c->v[0] <= 1.5f)) return -1;
        p += k;
        c->v[1] = -1; /* build 18: an optional grip diameter (cm: 0, or 2..5; none = kept) */
        if (sscanf(p, "%f%n", &c->v[1], &k) == 1) {
            if (!(c->v[1] == 0 || (c->v[1] >= 2 && c->v[1] <= 5))) return -1;
            p += k;
        }
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        return *p ? -1 : (c->op = SOMVR_XT_SIZE, 1);
    }
    if (!strcmp(w0, "popups")) { /* build 21 */
        if (!xt_word(&p, w1) || (strcmp(w1, "band") && strcmp(w1, "gaze"))) return -1;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        c->v[0] = w1[0] == 'g' ? SOMVR_PP_GAZE : SOMVR_PP_BAND;
        return *p ? -1 : (c->op = SOMVR_XT_POPUPS, 1);
    }
    if (!strcmp(w0, "lumlog")) { /* build 17 */
        int k = 0;
        if (sscanf(p, "%f%n", &c->v[0], &k) != 1 || !(c->v[0] >= 1 && c->v[0] <= 400)) return -1;
        p += k;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        return *p ? -1 : (c->op = SOMVR_XT_LUMLOG, 1);
    }
    if (!xt_word(&p, w1) || (strcmp(w1, "L") && strcmp(w1, "R"))) return -1;
    c->hand = w1[0] == 'L' ? SOMVR_HAND_L : SOMVR_HAND_R;
    int n = 0;
    if (!strcmp(w0, "hand")) {
        if (!xt_word(&p, w2)) return -1;
        if (!strcmp(w2, "off")) return c->op = SOMVR_XT_OFF, 1;
        int nv = 0, most = 6;
        if (!strcmp(w2, "head")) {
            c->op = SOMVR_XT_HEAD;
        } else if (!strcmp(w2, "shake")) { /* build 20 */
            int k = 0;
            if (sscanf(p, "%f %f%n", &c->v[0], &c->v[1], &k) != 2 || !(c->v[0] >= 0 && c->v[0] <= 5) ||
                !(c->v[1] >= 0 && c->v[1] <= 10))
                return -1;
            p += k;
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
            return *p ? -1 : (c->op = SOMVR_XT_SHAKE, 1);
        } else if (!strcmp(w2, "touch") || !strcmp(w2, "aim")) { /* build 20: aim */
            char s[16] = "";
            c->op = !strcmp(w2, "aim") ? SOMVR_XT_AIM : SOMVR_XT_TOUCH, most = 3;
            if (!xt_word(&p, s)) return -1;
            c->what = !strcmp(s, "board") ? SOMVR_XTS_BOARD : !strcmp(s, "panel") ? SOMVR_XTS_PANEL
                      : !strcmp(s, "watch") ? SOMVR_XTS_WATCH : -1;
            if (c->what < 0) return -1;
        } else {
            return -1;
        }
        while (nv < most && sscanf(p, "%f%n", &c->v[nv], &n) == 1) p += n, nv++;
        if (nv != 3 && nv != most) return -1;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == 's' && (p[1] == ' ' || p[1] == '\t')) {
            if (sscanf(p + 1, "%f%n", &c->sec, &n) != 1 || !(c->sec >= 0 && c->sec <= 30)) return -1;
            p += 1 + n;
        }
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        return *p ? -1 : 1;
    }
    if (!strcmp(w0, "btn")) {
        static const char *const names[] = {"menu", "a", "b", "stick", "trigger", "squeeze"};
        if (!xt_word(&p, w2) || sscanf(p, "%f", &c->v[0]) != 1) return -1;
        c->what = -1;
        for (int i = 0; i < 6; i++)
            if (!strcmp(w2, names[i])) c->what = i;
        if (c->what < 0 || !(c->v[0] >= 0 && c->v[0] <= 1)) return -1;
        return c->op = SOMVR_XT_BTN, 1;
    }
    if (!strcmp(w0, "stick")) {
        if (sscanf(p, "%f %f", &c->v[0], &c->v[1]) != 2 || !(fabsf(c->v[0]) <= 1 && fabsf(c->v[1]) <= 1)) return -1;
        return c->op = SOMVR_XT_STICK, 1;
    }
    return -1;
}

void somvr_xt_head_pose(const somvr_posef *head, const float cm[3], const float deg[3], somvr_posef *out) {
    static const float fwd0[3] = {0, 0, -1};
    float f[3];
    qrot(head->q, fwd0, f);
    float hx = f[0], hz = f[2], n = sqrtf(hx * hx + hz * hz);
    if (!(n > 1e-6f)) hx = 0, hz = -1, n = 1;
    hx /= n, hz /= n;
    /* x along the levelled forward's right (-hz, 0, hx), y up, z along the forward */
    out->p[0] = head->p[0] + 0.01f * (-cm[0] * hz + cm[2] * hx);
    out->p[1] = head->p[1] + 0.01f * cm[1];
    out->p[2] = head->p[2] + 0.01f * (cm[0] * hx + cm[2] * hz);
    float yq[4], a[4], t[4];
    qaxis(1, atan2f(-hx, -hz) * 57.29578f, yq); /* R(Y(a)) (0,0,-1) = (-sin a, 0, -cos a) */
    qaxis(0, deg[0], a), qmul(yq, a, t);
    qaxis(1, deg[1], a), qmul(t, a, yq);
    qaxis(2, deg[2], a), qmul(yq, a, out->q);
}

void somvr_xt_touch_pose(const somvr_posef *quad, const float size[2], float u, float v, float front_m, float reach_m,
                         somvr_posef *out) {
    float lp[3] = {(u - 0.5f) * size[0], (0.5f - v) * size[1], front_m + reach_m}, w[3];
    qrot(quad->q, lp, w); /* the aim reach_m behind the fingertip, along the quad's +Z */
    for (int i = 0; i < 3; i++) out->p[i] = quad->p[i] + w[i];
    memcpy(out->q, quad->q, sizeof out->q); /* the aim's -Z = the quad's -Z: into it */
}

void somvr_xt_shake(const somvr_posef *in, float cm, float deg, double t, int hand, somvr_posef *out) {
    static const float hz[6] = {0.6f, 1.1f, 1.9f, 5.3f, 7.9f, 9.7f}, wt[6] = {0.25f, 0.2f, 0.15f, 0.15f, 0.15f, 0.1f};
    float o[6]; /* x y z (m), then about x y z (deg) */
    for (int a = 0; a < 6; a++) {
        double sum = 0;
        for (int k = 0; k < 6; k++) { /* a fixed phase per (hand, axis, sine) */
            double ph = 6.2831853 * (double)((unsigned)(hand * 7919 + a * 104729 + k * 1299709) % 997u) / 997.0;
            sum += wt[k] * sin(6.2831853 * hz[k] * t + ph);
        }
        o[a] = (float)sum * (a < 3 ? 0.01f * cm : deg);
    }
    float q[4] = {in->q[0], in->q[1], in->q[2], in->q[3]}, r[4], t4[4];
    for (int a = 0; a < 3; a++) {
        qaxis(a, o[3 + a], r);
        qmul(q, r, t4);
        memcpy(q, t4, sizeof q);
    }
    memcpy(out->q, q, sizeof out->q);
    for (int i = 0; i < 3; i++) out->p[i] = in->p[i] + o[i];
}

void somvr_xt_lerp(const somvr_posef *a, const somvr_posef *b, float k, somvr_posef *out) {
    k = k < 0 ? 0 : k > 1 ? 1 : k;
    float d = a->q[0] * b->q[0] + a->q[1] * b->q[1] + a->q[2] * b->q[2] + a->q[3] * b->q[3], s = d < 0 ? -1.0f : 1.0f;
    float q[4], n = 0;
    for (int i = 0; i < 4; i++) q[i] = (1 - k) * a->q[i] + k * s * b->q[i], n += q[i] * q[i];
    n = sqrtf(n);
    for (int i = 0; i < 4; i++) out->q[i] = n > 1e-6f ? q[i] / n : b->q[i];
    for (int i = 0; i < 3; i++) out->p[i] = (1 - k) * a->p[i] + k * b->p[i];
}

/* ==== 2c. build 21: the popups where the player looks (SOMVR_XR_POPUPS=gaze) ======================================= */
int somvr_pp_parse(const char *v, int *bad) {
    if (bad) *bad = 0;
    if (!v || !*v || !strcmp(v, "band") || !strcmp(v, "off") || !strcmp(v, "0")) return SOMVR_PP_BAND;
    if (!strcmp(v, "gaze") || !strcmp(v, "on") || !strcmp(v, "1")) return SOMVR_PP_GAZE;
    if (bad) *bad = 1;
    return SOMVR_PP_BAND;
}
void somvr_pp_cfg_default(somvr_pp_cfg *c) {
    *c = (somvr_pp_cfg){.down_deg = 13.0f, .turn_deg = 40.0f, .ease_s = 0.35f, .hold_s = 0.3f, .pitch_min_deg = -50.0f,
                        .pitch_max_deg = 10.0f, .gap_deg = 1.0f};
}
void somvr_pp_parse_geom(const char *v, somvr_pp_cfg *c, int *bad) {
    if (bad) *bad = 0;
    if (!v || !*v) return;
    static const float lo[3] = {0, 10, 0}, hi[3] = {40, 120, 2000};
    float f[3];
    if (!parse_floats(v, 3, f, lo, hi)) {
        if (bad) *bad = 1;
        return;
    }
    c->down_deg = f[0], c->turn_deg = f[1], c->ease_s = f[2] * 1e-3f;
}
int somvr_pp_measure(const uint8_t *px, int w, int nrow, int step, int bb[4]) {
    int n = 0, x0 = w, x1 = 0, y0 = nrow, y1 = -1;
    for (int j = 0; px && j < nrow; j++)
        for (int x = 0; x < w; x++)
            if (px[((size_t)j * (size_t)w + (size_t)x) * 4 + 3] >= SOMVR_PP_ALPHA) {
                n++;
                if (x < x0) x0 = x;
                if (x + 1 > x1) x1 = x + 1;
                if (j < y0) y0 = j;
                if (j > y1) y1 = j;
            }
    if (n < SOMVR_PP_MIN_PX) {
        bb[0] = bb[1] = bb[2] = bb[3] = 0;
        return 0;
    }
    bb[0] = x0, bb[2] = x1, bb[1] = y0 * step, bb[3] = (y1 + 1) * step;
    return 1;
}
void somvr_pp_reset(somvr_pp_state *s) {
    memset(s, 0, sizeof *s);
    for (int i = 0; i < SOMVR_PP_MAX; i++) s->t[i].gone_t = -1, s->t[i].under = -1;
}
void somvr_pp_gaze(const somvr_posef eye[2], float *yaw, float *pitch, float mid[3]) {
    static const float fwd0[3] = {0, 0, -1};
    float f[3];
    qrot(eye[0].q, fwd0, f);
    if (!norm3(f)) f[0] = 0, f[1] = 0, f[2] = -1;
    *yaw = atan2f(-f[0], -f[2]), *pitch = asinf(fmaxf(-1.f, fminf(1.f, f[1])));
    for (int i = 0; i < 3; i++) mid[i] = 0.5f * (eye[0].p[i] + eye[1].p[i]);
    if (!(dot3(mid, mid) >= 0)) mid[0] = mid[1] = mid[2] = 0; /* NaN */
}
static float pp_wrap(float a) { /* to -pi..pi */
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}
/* do tiles a and b (their content's angular boxes, a placed at yaw/pitch) overlap? */
static int pp_overlap(const somvr_pp_tile *a, float yaw, float pitch, const somvr_pp_tile *b, float gap) {
    float dy = fabsf(pp_wrap(yaw - b->yaw)), dp = fabsf(pitch - (b->pitch - b->dpitch));
    return dy < 0.5f * (a->wrad + b->wrad) + gap && dp < 0.5f * (a->hrad + b->hrad) + gap;
}
uint32_t somvr_pp_step(somvr_pp_state *s, const somvr_pp_cfg *c, double t, const somvr_posef eye[2], float dist, int n,
                       const somvr_pp_read *rd, const float (*sz)[2], const int (*px)[2]) {
    const float D = 0.017453293f;
    float gy, gp, mid[3];
    somvr_pp_gaze(eye, &gy, &gp, mid);
    float ap = gp - c->down_deg * D; /* a new popup's centre: down_deg under the gaze, kept in the pitch limits */
    ap = fminf(c->pitch_max_deg * D, fmaxf(c->pitch_min_deg * D, ap));
    if (!(dist > 0.05f)) dist = 0.05f;
    uint32_t ev = 0;
    if (n > SOMVR_PP_MAX) n = SOMVR_PP_MAX;
    for (int i = 0; i < n; i++) {
        somvr_pp_tile *tl = &s->t[i];
        if (rd[i].present) {
            tl->gone_t = -1;
            if (!tl->shown) { /* it came up: in front of the gaze */
                const int *bb = rd[i].bb;
                float kx = px[i][0] > 0 ? sz[i][0] / (float)px[i][0] : 0, ky = px[i][1] > 0 ? sz[i][1] / (float)px[i][1] : 0;
                tl->off[0] = (0.5f * (float)(bb[0] + bb[2]) - 0.5f * (float)px[i][0]) * kx;
                tl->off[1] = -(0.5f * (float)(bb[1] + bb[3]) - 0.5f * (float)px[i][1]) * ky;
                tl->wrad = 2 * atanf(0.5f * (float)(bb[2] - bb[0] + 4) * kx / dist);
                tl->hrad = 2 * atanf(0.5f * (float)(bb[3] - bb[1] + 4) * ky / dist);
                tl->yaw = gy, tl->pitch = ap, tl->dpitch = 0, tl->under = -1;
                for (int k = 0, moved = 1; moved && k < 2 * SOMVR_PP_MAX; k++) { /* under any shown one it would cover */
                    moved = 0;
                    for (int j = 0; j < n && !moved; j++) {
                        const somvr_pp_tile *o = &s->t[j];
                        if (j == i || !o->shown || !pp_overlap(tl, tl->yaw, tl->pitch - tl->dpitch, o, c->gap_deg * D))
                            continue;
                        tl->yaw = o->yaw, tl->pitch = o->pitch, tl->under = j, moved = 1;
                        tl->dpitch = o->dpitch + 0.5f * (o->hrad + tl->hrad) + c->gap_deg * D;
                    }
                }
                memcpy(tl->eye, mid, sizeof tl->eye);
                tl->shown = 1, tl->easing = 0, tl->shown_t = t, tl->n_placed++;
                ev |= 1u << i;
            } else if (!tl->easing && fabsf(pp_wrap(gy - tl->yaw)) > c->turn_deg * D) { /* turned away: to the new front */
                float from[5] = {tl->yaw, tl->pitch, tl->eye[0], tl->eye[1], tl->eye[2]};
                float to[5] = {gy, ap, mid[0], mid[1], mid[2]};
                memcpy(tl->e_from, from, sizeof from), memcpy(tl->e_to, to, sizeof to);
                tl->easing = 1, tl->e_t0 = t, tl->n_eased++;
                ev |= 1u << (8 + i);
            }
        } else if (tl->shown) {
            if (tl->gone_t < 0) tl->gone_t = t;
            else if (t - tl->gone_t >= c->hold_s) {
                tl->shown = 0, tl->easing = 0, tl->gone_t = -1;
                ev |= 1u << (16 + i);
            }
        }
        if (tl->shown && tl->easing) {
            /* the new front follows the head while it eases: it lands where the head is at the ease's end (run
             * 20261028-215803: a head still turning landed it 27 deg short of the front) */
            tl->e_to[0] = gy, tl->e_to[1] = ap;
            memcpy(tl->e_to + 2, mid, sizeof mid);
            double k = c->ease_s > 0 ? (t - tl->e_t0) / c->ease_s : 1.0;
            k = k < 0 ? 0 : k > 1 ? 1 : k;
            float e = (float)(k * k * (3 - 2 * k));
            tl->yaw = pp_wrap(tl->e_from[0] + e * pp_wrap(tl->e_to[0] - tl->e_from[0]));
            tl->pitch = tl->e_from[1] + e * (tl->e_to[1] - tl->e_from[1]);
            for (int a = 0; a < 3; a++) tl->eye[a] = tl->e_from[2 + a] + e * (tl->e_to[2 + a] - tl->e_from[2 + a]);
            if (k >= 1) tl->easing = 0;
        }
    }
    return ev;
}
void somvr_pp_pose(const somvr_pp_tile *tl, float dist, somvr_posef *out) {
    float yaw = tl->yaw, pitch = tl->pitch - tl->dpitch, qy[4], qx[4];
    qy[0] = 0, qy[1] = sinf(0.5f * yaw), qy[2] = 0, qy[3] = cosf(0.5f * yaw);
    qx[0] = sinf(0.5f * pitch), qx[1] = 0, qx[2] = 0, qx[3] = cosf(0.5f * pitch);
    qmul(qy, qx, out->q);
    static const float fwd0[3] = {0, 0, -1}, x0[3] = {1, 0, 0}, y0[3] = {0, 1, 0};
    float f[3], r[3], u[3];
    qrot(out->q, fwd0, f), qrot(out->q, x0, r), qrot(out->q, y0, u);
    for (int i = 0; i < 3; i++) out->p[i] = tl->eye[i] + dist * f[i] - tl->off[0] * r[i] - tl->off[1] * u[i];
}
