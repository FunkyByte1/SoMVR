/* somvr_objective.c - see somvr_objective.h (SOMVR_OBJECTIVE: the objective marker in the world). Pure. */
#include "somvr_objective.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int somvr_ob_match(const char *pats, const char *name) {
    if (!pats || !name || !name[0]) return 0;
    for (const char *p = pats; *p;) {
        const char *e = strchr(p, ',');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n > 0 && p[n - 1] == '*') {
            if (!strncmp(p, name, n - 1)) return 1;
        } else if (n > 0 && strlen(name) == n && !strncmp(p, name, n)) {
            return 1;
        }
        if (!e) break;
        p = e + 1;
    }
    return 0;
}

void somvr_ob_text(float dist_m, char out[16]) {
    if (!(dist_m >= 0) || dist_m > 99999.0f) {
        snprintf(out, 16, "?");
        return;
    }
    if (dist_m < 999.5f) snprintf(out, 16, "%d m", (int)lroundf(dist_m));
    else snprintf(out, 16, "%.1f km", (double)dist_m / 1000.0);
}

void somvr_ob_cfg_default(somvr_ob_cfg *c) {
    c->size_deg = 4.0f;
    c->dist_m = 12.0f;
    c->near_m = 2.0f;
    c->near_hide_m = 3.0f;
    c->faint_deg = 5.0f;
    c->edge_deg = 5.0f;
    c->in_k = 0.80f;
    c->out_k = 0.90f;
    c->edge_k = 0.72f;
    c->edge_dist_m = 1.0f;
}

/* R(q) v (x y z w, Hamilton) */
static void ob_rot(const float q[4], const float v[3], float out[3]) {
    float tx = 2 * (q[1] * v[2] - q[2] * v[1]), ty = 2 * (q[2] * v[0] - q[0] * v[2]), tz = 2 * (q[0] * v[1] - q[1] * v[0]);
    out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
    out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
    out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}
/* a level billboard at pos whose +Z looks at eye (as somvr_th_face) */
static void ob_face(const float pos[3], const float eye[3], float q[4]) {
    float d[3] = {eye[0] - pos[0], eye[1] - pos[1], eye[2] - pos[2]};
    float n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(n > 1e-6f)) {
        q[0] = q[1] = q[2] = 0, q[3] = 1;
        return;
    }
    float y = d[1] / n, yaw = atan2f(d[0], d[2]), pitch = -asinf(y > 1 ? 1 : y < -1 ? -1 : y);
    float sy = sinf(0.5f * yaw), cy = cosf(0.5f * yaw), sx = sinf(0.5f * pitch), cx = cosf(0.5f * pitch);
    q[0] = cy * sx, q[1] = sy * cx, q[2] = -sy * sx, q[3] = cy * cx;
}

somvr_ob_place somvr_ob_locate(const somvr_ob_cfg *c, const float head[3], const float hq[4], const float fov[4],
                               const float pos[3], int *in_view) {
    somvr_ob_place p;
    memset(&p, 0, sizeof p);
    p.pose[3] = 1;
    float d[3] = {pos[0] - head[0], pos[1] - head[1], pos[2] - head[2]};
    float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(dist > c->near_hide_m) || !isfinite(dist) || !isfinite(hq[0] + hq[1] + hq[2] + hq[3])) {
        *in_view = 0;
        return p;
    }
    float inv[4] = {-hq[0], -hq[1], -hq[2], hq[3]}, l[3];
    ob_rot(inv, d, l); /* the head's frame: x right, y up, -z ahead */
    float fwd = -l[2], cs = fwd / dist;
    p.off_deg = acosf(cs > 1 ? 1 : cs < -1 ? -1 : cs) * 57.29578f;
    p.yaw_deg = atan2f(l[0], fwd) * 57.29578f;
    float tl = tanf(fov[0]), tr = tanf(fov[1]), tu = tanf(fov[2]), td = tanf(fov[3]);
    int in = 0;
    float tx = 0, ty = 0;
    if (fwd > 1e-3f * dist) {
        tx = l[0] / fwd, ty = l[1] / fwd;
        float k = *in_view ? c->out_k : c->in_k;
        in = tx >= tl * k && tx <= tr * k && ty >= td * k && ty <= tu * k;
    }
    *in_view = in;
    if (in) {
        float qd = dist < c->dist_m ? dist : c->dist_m, u[3] = {d[0] / dist, d[1] / dist, d[2] / dist};
        if (qd < c->near_m) qd = c->near_m;
        float side = 2 * qd * tanf(0.5f * c->size_deg * 0.01745329252f), at[3], q[4], up[3];
        static const float yv[3] = {0, 1, 0};
        for (int k = 0; k < 3; k++) at[k] = head[k] + u[k] * qd;
        ob_face(at, head, q);
        ob_rot(q, yv, up);
        float sh = (0.5f - SOMVR_OB_DIAMOND_V) * side; /* the diamond, not the cell's centre, on the direction */
        p.kind = SOMVR_OBK_WORLD;
        p.cell = p.off_deg <= c->faint_deg ? SOMVR_OB_CELL_FAINT : SOMVR_OB_CELL_FULL;
        memcpy(p.pose, q, sizeof q);
        for (int k = 0; k < 3; k++) p.pose[4 + k] = at[k] - up[k] * sh;
        p.size[0] = p.size[1] = side;
        return p;
    }
    /* out of view: the 2D way in the view (ahead: the tangents; behind: the side it is on) */
    float vx = fwd > 1e-3f * dist ? tx : l[0], vy = fwd > 1e-3f * dist ? ty : l[1];
    if (fwd <= 1e-3f * dist && fabsf(vx) < 0.05f * dist) vx = vx < 0 ? -0.05f * dist : 0.05f * dist; /* behind: a side */
    float vn = sqrtf(vx * vx + vy * vy);
    if (!(vn > 1e-9f)) vx = 1, vy = 0, vn = 1;
    float cth = vx / vn, sth = vy / vn, th = atan2f(sth, cth);
    float ax = c->edge_k * (cth >= 0 ? tr : -tl), ay = c->edge_k * (sth >= 0 ? tu : -td);
    float t = 1.0f / sqrtf((cth / ax) * (cth / ax) + (sth / ay) * (sth / ay)); /* the ray at th meets the ellipse */
    /* parallel to the view's plane (a plane parallel to the image projects undistorted: the arrow's angle on the eye
     * is th), rolled by th about the view's axis */
    float ed = c->edge_dist_m, at[3] = {cth * t * ed, sth * t * ed, -ed};
    float q[4] = {0, 0, sinf(0.5f * th), cosf(0.5f * th)};
    p.kind = SOMVR_OBK_EDGE;
    p.cell = SOMVR_OB_CELL_ARROW;
    p.roll_deg = th * 57.29578f;
    memcpy(p.pose, q, sizeof q);
    memcpy(p.pose + 4, at, sizeof at);
    p.size[0] = p.size[1] = 2 * ed * tanf(0.5f * c->edge_deg * 0.01745329252f);
    return p;
}

somvr_ob_place somvr_ob_floor(const float pos[3], float r_m) {
    somvr_ob_place p;
    memset(&p, 0, sizeof p);
    if (!isfinite(pos[0] + pos[1] + pos[2]) || !(r_m > 0.01f) || !(r_m < 5.0f)) return p;
    p.kind = SOMVR_OBK_WORLD, p.cell = SOMVR_OB_CELL_RING;
    p.pose[0] = -0.70710678f, p.pose[3] = 0.70710678f; /* -90 deg about x: the quad's +z (its face) up */
    p.pose[4] = pos[0], p.pose[5] = pos[1] + 0.02f, p.pose[6] = pos[2];
    p.size[0] = p.size[1] = 2 * r_m * (float)SOMVR_OB_CELL / 124.0f; /* the ring's outer edge 62 px from the centre */
    return p;
}

somvr_ob_place somvr_ob_line(const float top[3], const float bot[3], const float head[3], float w_m) {
    somvr_ob_place p;
    memset(&p, 0, sizeof p);
    float a[3] = {top[0] - bot[0], top[1] - bot[1], top[2] - bot[2]};
    float len = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (!isfinite(len + head[0] + head[1] + head[2]) || !(len >= 0.05f) || !(w_m > 0)) return p;
    float y[3] = {a[0] / len, a[1] / len, a[2] / len}, m[3], z[3];
    for (int k = 0; k < 3; k++) m[k] = 0.5f * (top[k] + bot[k]), z[k] = head[k] - m[k];
    float d = z[0] * y[0] + z[1] * y[1] + z[2] * y[2]; /* toward the head, square to the line */
    for (int k = 0; k < 3; k++) z[k] -= d * y[k];
    float zn = sqrtf(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
    if (!(zn > 1e-4f)) { /* the head on the line's axis: any square direction */
        float t[3] = {fabsf(y[0]) < 0.9f ? 1.0f : 0.0f, fabsf(y[0]) < 0.9f ? 0.0f : 1.0f, 0};
        float e = t[0] * y[0] + t[1] * y[1];
        for (int k = 0; k < 3; k++) z[k] = t[k] - e * y[k];
        zn = sqrtf(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
    }
    for (int k = 0; k < 3; k++) z[k] /= zn;
    float x[3] = {y[1] * z[2] - y[2] * z[1], y[2] * z[0] - y[0] * z[2], y[0] * z[1] - y[1] * z[0]};
    /* the rotation with columns x y z as a quaternion */
    float tr = x[0] + y[1] + z[2], q[4];
    if (tr > 0) {
        float s = sqrtf(tr + 1.0f) * 2;
        q[3] = 0.25f * s, q[0] = (y[2] - z[1]) / s, q[1] = (z[0] - x[2]) / s, q[2] = (x[1] - y[0]) / s;
    } else if (x[0] > y[1] && x[0] > z[2]) {
        float s = sqrtf(1.0f + x[0] - y[1] - z[2]) * 2;
        q[3] = (y[2] - z[1]) / s, q[0] = 0.25f * s, q[1] = (y[0] + x[1]) / s, q[2] = (z[0] + x[2]) / s;
    } else if (y[1] > z[2]) {
        float s = sqrtf(1.0f + y[1] - x[0] - z[2]) * 2;
        q[3] = (z[0] - x[2]) / s, q[0] = (y[0] + x[1]) / s, q[1] = 0.25f * s, q[2] = (z[1] + y[2]) / s;
    } else {
        float s = sqrtf(1.0f + z[2] - x[0] - y[1]) * 2;
        q[3] = (x[1] - y[0]) / s, q[0] = (z[0] + x[2]) / s, q[1] = (z[1] + y[2]) / s, q[2] = 0.25f * s;
    }
    p.kind = SOMVR_OBK_WORLD, p.cell = SOMVR_OB_CELL_LINE;
    memcpy(p.pose, q, sizeof q);
    memcpy(p.pose + 4, m, sizeof m);
    p.size[0] = w_m, p.size[1] = len;
    return p;
}

float somvr_ob_strike_ring(float dist_m, float min_m, float deg) {
    float r = 2 * dist_m * tanf(0.5f * deg * 0.01745329252f);
    return isfinite(r) && r > min_m ? r : min_m;
}

somvr_ob_place somvr_ob_strike(const float head[3], const float pos[3], float ring_m, int cell) {
    somvr_ob_place p;
    memset(&p, 0, sizeof p);
    p.pose[3] = 1;
    float d[3] = {pos[0] - head[0], pos[1] - head[1], pos[2] - head[2]};
    float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!isfinite(dist + ring_m) || !(dist > 0.3f) || !(ring_m > 0.05f) || !(ring_m < 20.0f) || cell < SOMVR_OB_CELL_STRIKE ||
        cell >= SOMVR_OB_CELLS)
        return p;
    float q[4], right[3];
    static const float xv[3] = {1, 0, 0};
    ob_face(pos, head, q);
    ob_rot(q, xv, right);
    float h = ring_m * (float)SOMVR_OB_CELL / (float)SOMVR_OB_STRIKE_RING_PX;
    float w = h * (float)(SOMVR_OB_WIDE * SOMVR_OB_STRIDE - 2) / (float)SOMVR_OB_CELL;
    float sh = 0.5f * w - 0.5f * h; /* the ring's centre (its square's middle) on pos: the quad's middle right of it */
    p.kind = SOMVR_OBK_WORLD, p.cell = cell;
    memcpy(p.pose, q, sizeof q);
    for (int k = 0; k < 3; k++) p.pose[4 + k] = pos[k] + right[k] * sh;
    p.size[0] = w, p.size[1] = h;
    p.yaw_deg = atan2f(d[0], -d[2]) * 57.29578f; /* logs: its bearing in the reference space */
    return p;
}

void somvr_ob_rect(int cell, int32_t r[4]) {
    if (cell < 0) cell = 0;
    if (cell >= SOMVR_OB_CELLS) cell = SOMVR_OB_CELLS - 1;
    if (cell >= SOMVR_OB_SQUARE_CELLS) { /* the strike's wide cells */
        int k = SOMVR_OB_SQUARE_CELLS + (cell - SOMVR_OB_SQUARE_CELLS) * SOMVR_OB_WIDE;
        r[0] = k * SOMVR_OB_STRIDE + 1, r[1] = 1, r[2] = SOMVR_OB_WIDE * SOMVR_OB_STRIDE - 2, r[3] = SOMVR_OB_CELL;
        return;
    }
    r[0] = cell * SOMVR_OB_STRIDE + 1, r[1] = 1, r[2] = SOMVR_OB_CELL, r[3] = SOMVR_OB_CELL;
}

/* one premultiplied texel (straight colour c, alpha a 0..1) over what is there (src-over) */
static void ob_px(uint8_t *px, int x, int y, const float c[3], float a, int bgra) {
    if (x < 0 || y < 0 || x >= SOMVR_OB_IMG_W || y >= SOMVR_OB_IMG_H || !(a > 0)) return;
    uint8_t *p = px + 4 * ((size_t)y * SOMVR_OB_IMG_W + (size_t)x);
    float s[4] = {c[bgra ? 2 : 0] * a, c[1] * a, c[bgra ? 0 : 2] * a, a};
    for (int k = 0; k < 4; k++) {
        float v = s[k] * 255.0f + p[k] * (1 - a);
        p[k] = (uint8_t)(v > 255 ? 255 : v < 0 ? 0 : v + 0.5f);
    }
}
static float ob_edge(float x0, float y0, float x1, float y1, float x, float y) {
    return (x1 - x0) * (y - y0) - (y1 - y0) * (x - x0);
}
static int ob_in_tri(const float t[6], float x, float y) {
    float a = ob_edge(t[0], t[1], t[2], t[3], x, y), b = ob_edge(t[2], t[3], t[4], t[5], x, y),
          c = ob_edge(t[4], t[5], t[0], t[1], x, y);
    return (a >= 0 && b >= 0 && c >= 0) || (a <= 0 && b <= 0 && c <= 0);
}

/* text at x0, y0 (its cell's top left) at scale sc in colour ink with a dark rim of 2 px (the font draws opaque texels:
 * the rim first, then the ink) */
static void ob_text_rim(uint8_t *px, const somvr_font *f, int x0, int y0, const char *s, int sc, const uint8_t ink[4], int bgra) {
    const uint8_t rim[4] = {13, 10, 5, 210};
    for (int oy = -2; oy <= 2; oy += 2)
        for (int ox = -2; ox <= 2; ox += 2)
            if (ox || oy) somvr_font_draw(f, px, SOMVR_OB_IMG_W, SOMVR_OB_IMG_H - 1, x0 + ox, y0 + oy, s, sc, rim, bgra);
    somvr_font_draw(f, px, SOMVR_OB_IMG_W, SOMVR_OB_IMG_H - 1, x0, y0, s, sc, ink, bgra);
}

/* build 17: a strike cell (4 ready, 5 no arrows, 6 pressed): the ring in its left square, the label right of it */
static void ob_strike_cell(uint8_t *px, const somvr_font *f, int cell, int bgra) {
    static const float blue[3] = {0.62f, 0.85f, 1.00f}, grey[3] = {0.60f, 0.60f, 0.64f}, dark[3] = {0.03f, 0.04f, 0.07f};
    int32_t r[4];
    somvr_ob_rect(cell, r);
    int no = cell == SOMVR_OB_CELL_STRIKE_NO, go = cell == SOMVR_OB_CELL_STRIKE_GO;
    const float *rc = no ? grey : blue;
    float ro = 0.5f * SOMVR_OB_STRIKE_RING_PX, c0 = 0.5f * SOMVR_OB_CELL, band = go ? 11 : 8;
    for (int y = 0; y < SOMVR_OB_CELL; y++) /* the ring: a dark rim, the band, a dark rim; pressed: wider, a faint fill */
        for (int x = 0; x < SOMVR_OB_CELL; x++) {
            float dx = x + 0.5f - c0, dy = y + 0.5f - c0, d = sqrtf(dx * dx + dy * dy);
            int X = r[0] + x, Y = r[1] + y;
            if (d <= ro && d > ro - 2) ob_px(px, X, Y, dark, 0.7f, bgra);
            else if (d <= ro - 2 && d > ro - band) ob_px(px, X, Y, rc, no ? 0.75f : 1.0f, bgra);
            else if (d <= ro - band && d > ro - band - 2) ob_px(px, X, Y, dark, 0.6f, bgra);
            else if (go && d <= ro - band - 2) ob_px(px, X, Y, blue, 0.18f, bgra);
        }
    int lx = r[0] + SOMVR_OB_CELL + 6; /* the label */
    if (!no) { /* the Touch's A: a round button with its letter */
        float bx = (float)lx + 22.0f, by = (float)r[1] + 0.5f * SOMVR_OB_CELL, br = 21;
        for (int y = (int)(by - br - 3); y <= (int)(by + br + 3); y++)
            for (int x = (int)(bx - br - 3); x <= (int)(bx + br + 3); x++) {
                float dx = (float)x + 0.5f - bx, dy = (float)y + 0.5f - by, d = sqrtf(dx * dx + dy * dy);
                if (d <= br + 2 && d > br) ob_px(px, x, y, dark, 0.8f, bgra);
                else if (d <= br) ob_px(px, x, y, blue, 1.0f, bgra);
            }
        const uint8_t ink[4] = {10, 14, 26, 255};
        if (f) somvr_font_draw(f, px, SOMVR_OB_IMG_W, SOMVR_OB_IMG_H - 1, (int)bx - 7, (int)by - 13, "A", 2, ink, bgra);
        lx += 52;
    }
    if (!f) return;
    const uint8_t white[4] = {255, 255, 255, 255}, red[4] = {255, 130, 110, 255};
    int y0 = r[1] + (SOMVR_OB_CELL - 2 * 2 * SOMVR_FONT_H) / 2 - 2; /* two lines at scale 2, a 4 px gap */
    ob_text_rim(px, f, lx, y0, no ? "NO" : "SHADOW", 2, no ? red : white, bgra);
    ob_text_rim(px, f, lx, y0 + 2 * SOMVR_FONT_H + 4, no ? "ARROWS" : "STRIKE", 2, no ? red : white, bgra);
}

void somvr_ob_image(uint8_t *px, const somvr_font *f, const char *text, int bgra) {
    static const float gold[3] = {1.00f, 0.80f, 0.30f}, dark[3] = {0.05f, 0.04f, 0.02f};
    memset(px, 0, (size_t)SOMVR_OB_IMG_W * SOMVR_OB_IMG_H * 4);
    int cx = SOMVR_OB_CELL / 2, cy = (int)lroundf(SOMVR_OB_DIAMOND_V * SOMVR_OB_CELL);
    for (int y = 0; y < SOMVR_OB_CELL; y++) /* cell 0: the diamond (L1 rings: the rim, the gold band, the dim inside, the dot) */
        for (int x = 0; x < SOMVR_OB_CELL; x++) {
            int s = abs(x - cx) + abs(y - cy), X = 1 + x, Y = 1 + y;
            if (s <= 29) ob_px(px, X, Y, dark, s <= 26 ? 0.0f : 0.70f, bgra);
            if (s <= 26 && s > 19) ob_px(px, X, Y, gold, 1.0f, bgra);
            else if (s <= 19 && s > 8) ob_px(px, X, Y, dark, 0.30f, bgra);
            else if (s <= 8) ob_px(px, X, Y, gold, 1.0f, bgra);
        }
    if (f && text && text[0]) { /* the distance: white, a dark rim of 2 px, centred under the diamond */
        int sc = 2, w = somvr_font_text_w(text, sc), x0 = 1 + (SOMVR_OB_CELL - w) / 2, y0 = 1 + cy + 34;
        /* the font draws opaque texels: the rim first (8 offsets), then the white */
        const uint8_t rim[4] = {13, 10, 5, 210}, ink[4] = {255, 255, 255, 255};
        for (int oy = -2; oy <= 2; oy += 2)
            for (int ox = -2; ox <= 2; ox += 2)
                if (ox || oy) somvr_font_draw(f, px, SOMVR_OB_IMG_W, SOMVR_OB_IMG_H - 1, x0 + ox, y0 + oy, text, sc, rim, bgra);
        somvr_font_draw(f, px, SOMVR_OB_IMG_W, SOMVR_OB_IMG_H - 1, x0, y0, text, sc, ink, bgra);
    }
    /* the font may not reach past the cell (x0 >= 1, the text at most 8 glyphs = 128 px): cell 1 = cell 0 faint */
    for (int y = 1; y <= SOMVR_OB_CELL; y++)
        for (int x = 1; x <= SOMVR_OB_CELL; x++) {
            const uint8_t *s = px + 4 * ((size_t)y * SOMVR_OB_IMG_W + (size_t)x);
            uint8_t *d = px + 4 * ((size_t)y * SOMVR_OB_IMG_W + (size_t)(x + SOMVR_OB_STRIDE));
            for (int k = 0; k < 4; k++) d[k] = (uint8_t)(s[k] * SOMVR_OB_FAINT + 0.5f);
        }
    /* cell 2: the arrow, pointing right (a dark rim 3 px, gold inside) */
    float big[6] = {116, 64, 34, 18, 34, 110}, in[6] = {108, 64, 39, 26, 39, 102};
    for (int y = 0; y < SOMVR_OB_CELL; y++)
        for (int x = 0; x < SOMVR_OB_CELL; x++) {
            float fx = x + 0.5f, fy = y + 0.5f;
            int X = 1 + 2 * SOMVR_OB_STRIDE + x, Y = 1 + y;
            if (ob_in_tri(in, fx, fy)) ob_px(px, X, Y, gold, 1.0f, bgra);
            else if (ob_in_tri(big, fx, fy)) ob_px(px, X, Y, dark, 0.75f, bgra);
        }
    /* cell 3: the landing ring (a dark rim, the gold band, a faint gold disc, the dot), seen from above */
    for (int y = 0; y < SOMVR_OB_CELL; y++)
        for (int x = 0; x < SOMVR_OB_CELL; x++) {
            float dx = x + 0.5f - SOMVR_OB_CELL / 2.0f, dy = y + 0.5f - SOMVR_OB_CELL / 2.0f, d = sqrtf(dx * dx + dy * dy);
            int X = 1 + 3 * SOMVR_OB_STRIDE + x, Y = 1 + y;
            if (d <= 62 && d > 59) ob_px(px, X, Y, dark, 0.6f, bgra);
            else if (d <= 59 && d > 52) ob_px(px, X, Y, gold, 0.85f, bgra);
            else if (d <= 52 && d > 50) ob_px(px, X, Y, dark, 0.5f, bgra);
            else if (d <= 50 && d > 6) ob_px(px, X, Y, gold, 0.12f, bgra);
            else if (d <= 6) ob_px(px, X, Y, gold, 0.85f, bgra);
        }
    /* cell 4: the drop's line (build 17): across, a gold band in the middle and dark edges; down, faded in over its top
     * 15 % (it starts at the edge's height out of nothing; full at the ring) */
    for (int y = 0; y < SOMVR_OB_CELL; y++)
        for (int x = 0; x < SOMVR_OB_CELL; x++) {
            float t = fabsf(x + 0.5f - SOMVR_OB_CELL / 2.0f) / (SOMVR_OB_CELL / 2.0f), v = (y + 0.5f) / (0.15f * SOMVR_OB_CELL);
            float k = v < 1 ? v : 1.0f;
            int X = 1 + SOMVR_OB_CELL_LINE * SOMVR_OB_STRIDE + x, Y = 1 + y;
            if (t <= 0.5f) ob_px(px, X, Y, gold, 0.65f * k, bgra);
            else if (t <= 0.85f) ob_px(px, X, Y, dark, 0.35f * k, bgra);
        }
    for (int c = SOMVR_OB_CELL_STRIKE; c < SOMVR_OB_CELLS; c++) ob_strike_cell(px, f, c, bgra);
}
