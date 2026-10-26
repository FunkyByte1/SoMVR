/* somvr_comfort.c - see somvr_comfort.h (the pure part: no globals, no OpenXR/Vulkan; built into somvr.so and the
 * layer) */
#include "somvr_comfort.h"

#include <math.h>
#include <string.h>

#define CF_PI 3.14159265358979f
#define CF_DEG 57.2957795f

static float wrap_pi(float a) {
    a = fmodf(a + CF_PI, 2 * CF_PI);
    if (a <= 0) a += 2 * CF_PI;
    return a - CF_PI;
}
static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static float smooth01(float e0, float e1, float x) {
    if (!(e1 > e0)) return x >= e1 ? 1.0f : 0.0f;
    float t = clampf((x - e0) / (e1 - e0), 0, 1);
    return t * t * (3 - 2 * t);
}
static int finite3(const float v[3]) { return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]); }

/* ---- the rig's artificial motion ------------------------------------------------------------------------------ */
void somvr_cf_yaw_rot(float a, const float v[3], float out[3]) {
    float c = cosf(a), s = sinf(a), x = v[0], y = v[1], z = v[2];
    out[0] = c * x + s * z;
    out[1] = y;
    out[2] = -s * x + c * z;
}

void somvr_cf_map_apply(const somvr_cf_map *m, const float h[3], float out[3]) {
    float k = m->s >= 0.5f && m->s <= 2.0f ? m->s : 1.0f;
    float d[3] = {k * (h[0] - m->p0[0]), k * (h[1] - m->p0[1]), k * (h[2] - m->p0[2])}, r[3];
    somvr_cf_yaw_rot(m->a, d, r);
    for (int i = 0; i < 3; i++) out[i] = m->b[i] + r[i];
}

void somvr_cf_meas_reset(somvr_cf_meas *s) { memset(s, 0, sizeof *s); }

uint32_t somvr_cf_measure(somvr_cf_meas *s, const somvr_cf_map *m, const float h[3], double t, float snap_rad,
                          int rig_ev, float *lin_mps, float *ang_dps, float *dt_s) {
    int recentred = (rig_ev & SOMVR_CF_EV_RECENTER) != 0, jumped = (rig_ev & SOMVR_CF_EV_JUMP) != 0;
    s->frames++;
    *dt_s = 0;
    int ok = finite3(m->b) && finite3(m->p0) && isfinite(m->a) && finite3(h) && isfinite(t) && isfinite(snap_rad);
    if (!ok) { /* never into the state: the next good frame measures from the last good one */
        s->jumps++, s->held++;
        *lin_mps = s->lin, *ang_dps = s->ang;
        return SOMVR_CFF_JUMP | SOMVR_CFF_HELD;
    }
    if (!s->have || t - s->t > SOMVR_CF_GAP_S || t < s->t) { /* the first frame, after a gap, or a clock change */
        uint32_t f = s->have ? SOMVR_CFF_GAP : SOMVR_CFF_FIRST;
        s->gaps += s->have;
        s->have = 1, s->last = *m, s->t = t, s->lin = s->ang = 0;
        *lin_mps = *ang_dps = 0;
        return f | (recentred ? SOMVR_CFF_RECENTER : 0u) | (snap_rad != 0 ? SOMVR_CFF_SNAP : 0u) |
               (jumped ? SOMVR_CFF_JUMP : 0u);
    }
    double dt = t - s->t;
    if (dt < SOMVR_CF_MIN_DT_S) { /* the same time again: keep the frame before as the reference */
        if (recentred || snap_rad != 0 || jumped) s->last = *m; /* ... unless the rig jumped in it (not motion) */
        s->held++;
        *lin_mps = s->lin, *ang_dps = s->ang;
        return SOMVR_CFF_HELD | (recentred ? SOMVR_CFF_RECENTER : 0u) | (snap_rad != 0 ? SOMVR_CFF_SNAP : 0u) |
               (jumped ? SOMVR_CFF_JUMP : 0u);
    }
    *dt_s = (float)dt;
    float w1[3], w0[3];
    somvr_cf_map_apply(m, h, w1);
    somvr_cf_map_apply(&s->last, h, w0); /* the same head through last frame's mapping: only the rig's change is left */
    float dx = w1[0] - w0[0], dy = w1[1] - w0[1], dz = w1[2] - w0[2];
    float dcm = sqrtf(dx * dx + dy * dy + dz * dz);
    float lin = (float)(dcm * 0.01 / dt), ang = (float)(fabsf(wrap_pi(m->a - s->last.a - snap_rad)) * CF_DEG / dt);
    uint32_t flags = (recentred ? SOMVR_CFF_RECENTER : 0u) | (snap_rad != 0 ? SOMVR_CFF_SNAP : 0u) |
                     (jumped ? SOMVR_CFF_JUMP : 0u); /* a base swap: not motion, whatever its size */
    if (!flags && (dcm > SOMVR_CF_JUMP_CM || lin > SOMVR_CF_JUMP_MPS || ang > SOMVR_CF_JUMP_DPS)) flags = SOMVR_CFF_JUMP;
    s->last = *m, s->t = t;
    if (flags) { /* not motion: the last counted speeds stay */
        s->held++;
        s->jumps += (flags & SOMVR_CFF_JUMP) != 0;
        *lin_mps = s->lin, *ang_dps = s->ang;
        return flags | SOMVR_CFF_HELD;
    }
    s->counted++;
    s->lin = lin, s->ang = ang;
    *lin_mps = lin, *ang_dps = ang;
    return SOMVR_CFF_COUNTED;
}

/* ---- the snap blink --------------------------------------------------------------------------------------------- */
void somvr_blink_times(uint32_t ms, float *out_s, float *in_s) {
    float t = (float)(ms > 2000 ? 2000 : ms) * 0.001f;
    *out_s = t / 3, *in_s = t - t / 3;
}
void somvr_blink_reset(somvr_blink *b) { memset(b, 0, sizeof *b); }

int somvr_blink_frame(somvr_blink *b, float out_s, float in_s, int want, double t, float *alpha) {
    double fdt = b->t_last > 0 && t > b->t_last ? t - b->t_last : 0;
    if (fdt > 0.05) fdt = 0.05; /* a hitch does not count as one frame */
    b->t_last = t;
    if (!(out_s > 0) && !(in_s > 0)) { /* off */
        b->phase = 0, b->alpha = 0, *alpha = 0;
        return want;
    }
    if (want && b->phase != 1) { /* a new turn: fade out from the black now (already one frame dark) */
        b->blinks++;
        b->t0 = t - fdt - (double)b->alpha * out_s;
        b->phase = 1;
    }
    if (b->phase == 1 && !want) { /* nothing held any more (a recentre took it): fade in from the last frame's black */
        b->phase = 2;
        b->t0 = t - fdt - (1.0 - (double)b->alpha) * in_s;
    }
    if (b->phase == 1) {
        float a = out_s > 0 ? (float)((t - b->t0) / out_s) : 1.0f;
        if (!(a < 1.0f)) { /* black: the turn lands on this frame */
            b->phase = 2, b->t0 = t, b->alpha = 1.0f, *alpha = 1.0f;
            return 1;
        }
        b->held_frames++;
        b->alpha = clampf(a, 0, 1), *alpha = b->alpha;
        return 0;
    }
    if (b->phase == 2) {
        float a = in_s > 0 ? 1.0f - (float)((t - b->t0) / in_s) : 0.0f;
        if (!(a > 0)) b->phase = 0, a = 0;
        b->alpha = clampf(a, 0, 1), *alpha = b->alpha;
        return 0;
    }
    b->alpha = 0, *alpha = 0;
    return 0;
}

/* ---- seated height --------------------------------------------------------------------------------------------- */
float somvr_seat_ref(somvr_seat *s, const somvr_seat_cfg *c, float head_y, int floor, const char **why) {
    if (floor && c->has_height) {
        *why = "fixed (SOMVR_HEIGHT_CM above the floor)";
        s->have = 1, s->y = c->height_cm, s->latched++;
        return c->height_cm;
    }
    if (floor) {
        if (isfinite(head_y) && head_y >= SOMVR_SEAT_MIN_CM && head_y <= SOMVR_SEAT_MAX_CM) {
            *why = "the head now (above the floor)";
            s->have = 1, s->y = head_y, s->latched++;
            return head_y;
        }
        s->rejected++;
        *why = s->have ? "not a seated head: the last seated height kept" : "not a seated head: the default height";
        return s->have ? s->y : SOMVR_SEAT_DEFAULT_CM;
    }
    if (!isfinite(head_y)) {
        s->rejected++;
        *why = "no head: the last height kept";
        return s->have ? s->y : 0;
    }
    *why = c->has_height ? "the head now (no floor in this space: SOMVR_HEIGHT_CM not used)" : "the head now (no floor)";
    s->have = 1, s->y = head_y, s->latched++;
    return head_y;
}

/* ---- the vignette ------------------------------------------------------------------------------------------------ */
void somvr_vig_cfg_default(somvr_vig_cfg *c) {
    /* HoF2's walk vignette ramps 0.25 -> 1.5 m/s and its turn vignette to 90 deg/s; here the rig's speed, which also
     * carries jogs and sprints: nothing below a drift (0.3 m/s), full at a jog (3 m/s) */
    c->lin_lo = 0.3f, c->lin_hi = 3.0f;
    c->ang_lo = 10.0f, c->ang_hi = 90.0f;
    /* HL:A-like (build 6): the tunnel comes in over ~0.25 s and goes over ~0.35 s (the 0.05 s low-pass keeps a jittery
     * speed from shimmering the ring); build 5's 0.08 / 0.30 s (HoF2's) lingered about a second after a stop */
    c->tau_f = 0.05f, c->tau_in = 0.05f, c->tau_out = 0.10f;
    c->min_deg = 20.0f;   /* HoF2: 16 deg at strength 1; a little wider here (the ring's soft edge reaches inward less) */
    c->margin_deg = 2.0f;
    c->dist_m = 5.0f;     /* HoF2: "far: no disparity" (a ring close to the eyes looked different to each eye) */
    c->fit = SOMVR_VIG_FIT_CORNER;
}

void somvr_vig_cfg_fit(somvr_vig_cfg *c, int fit) {
    if (fit != SOMVR_VIG_FIT_LENS) {
        c->fit = SOMVR_VIG_FIT_CORNER;
        return;
    }
    c->fit = SOMVR_VIG_FIT_LENS;
    /* the stick's walk is ~2.3 m/s, its slowest useful push ~0.5: full from 2 m/s (HoF2's walk vignette: full at 1.5);
     * the menu's slowest smooth turn is 60 deg/s: full there */
    c->lin_lo = 0.2f, c->lin_hi = 2.0f;
    c->ang_lo = 8.0f, c->ang_hi = 60.0f;
    c->min_deg = 15.0f; /* 100 %: a porthole (the quad still covers a Quest 2's view down to ~13.5 deg) */
}

float somvr_vig_target(const somvr_vig_cfg *c, float lin_mps, float ang_dps) {
    float a = isfinite(lin_mps) ? smooth01(c->lin_lo, c->lin_hi, lin_mps) : 0.0f;
    float b = isfinite(ang_dps) ? smooth01(c->ang_lo, c->ang_hi, ang_dps) : 0.0f;
    return a > b ? a : b;
}

#define VIG_CUT 0.002f /* below this the amount is 0 (the ring sits outside the view there anyway) */
float somvr_vig_step(const somvr_vig_cfg *c, somvr_vig_state *s, float target, float dt_s) {
    if (!isfinite(target)) target = 0;
    target = clampf(target, 0, 1);
    if (!(dt_s > 0) || !isfinite(dt_s)) return s->amount;
    if (dt_s > 1.0f) dt_s = 1.0f;
    float kf = c->tau_f > 0 ? 1.0f - expf(-dt_s / c->tau_f) : 1.0f;
    s->f += (target - s->f) * kf;
    float tau = s->f > s->amount ? c->tau_in : c->tau_out;
    float k = tau > 0 ? 1.0f - expf(-dt_s / tau) : 1.0f;
    s->amount += (s->f - s->amount) * k;
    if (s->amount < VIG_CUT && s->f < VIG_CUT) s->amount = 0;
    if (s->f < 1e-5f) s->f = 0;
    return s->amount;
}

void somvr_vig_snap(somvr_vig_state *s, float strength) {
    if (!isfinite(strength) || !(strength > 0)) return;
    strength = clampf(strength, 0, 1);
    if (s->f < strength) s->f = strength;
    if (s->amount < strength) s->amount = strength;
}

void somvr_vig_extent(const float fov[2][4], float *cover_tan, float *corner_rad) {
    float cov = 0, cor = 0;
    for (int e = 0; e < 2; e++) {
        float tl = tanf(clampf(fov[e][0], -1.5f, 1.5f)), tr = tanf(clampf(fov[e][1], -1.5f, 1.5f));
        float tu = tanf(clampf(fov[e][2], -1.5f, 1.5f)), td = tanf(clampf(fov[e][3], -1.5f, 1.5f));
        float xs[2] = {tl, tr}, ys[2] = {tu, td};
        for (int i = 0; i < 2; i++) {
            if (fabsf(xs[i]) > cov) cov = fabsf(xs[i]);
            if (fabsf(ys[i]) > cov) cov = fabsf(ys[i]);
            for (int j = 0; j < 2; j++) {
                float a = atanf(sqrtf(xs[i] * xs[i] + ys[j] * ys[j]));
                if (a > cor) cor = a;
            }
        }
    }
    if (!(cov > 0.1f) || !isfinite(cov)) cov = 1.5f; /* no usable fov: about a Quest 2's */
    if (!(cor > 0.1f) || !isfinite(cor)) cor = 62.0f / CF_DEG;
    *cover_tan = cov, *corner_rad = cor;
}

float somvr_vig_edge(const float fov[2][4]) {
    float m = 0;
    for (int e = 0; e < 2; e++)
        for (int i = 0; i < 4; i++) {
            float a = fabsf(isfinite(fov[e][i]) ? fov[e][i] : 0);
            if (a > 0.1f && (m == 0 || a < m)) m = a;
        }
    return m > 0 ? clampf(m, 0.1f, 1.5f) : 46.0f / CF_DEG; /* no usable fov: about a Quest 2's */
}

somvr_vig_ring somvr_vig_geom(const somvr_vig_cfg *c, float amount, float cover_tan, float corner_rad) {
    somvr_vig_ring g;
    amount = isfinite(amount) ? clampf(amount, 0, 1) : 0;
    float open = clampf(corner_rad + c->margin_deg / CF_DEG, 30.0f / CF_DEG, 85.0f / CF_DEG);
    float closed = clampf(c->min_deg / CF_DEG, 5.0f / CF_DEG, open);
    g.cover_tan = 1.1f * cover_tan;
    /* the lens: the first part of the amount carries the disc's edge in from the corners to the lens's rim fast (the
     * rim darkens at once and smoothly: the ring's alpha there starts at 0), the rest closes it inside the picture */
    float k = c->fit == SOMVR_VIG_FIT_LENS ? sqrtf(amount) : amount;
    g.clear_rad = open - (open - closed) * k;
    g.half_tan = tanf(g.clear_rad) / SOMVR_VIG_R0;
    g.clamped = 0;
    if (g.half_tan < g.cover_tan) { /* the quad must reach past the view's edges */
        g.half_tan = g.cover_tan;
        g.clear_rad = atanf(SOMVR_VIG_R0 * g.half_tan);
        g.clamped = 1;
    }
    return g;
}

float somvr_vig_profile(float r) { return smooth01(SOMVR_VIG_R0, SOMVR_VIG_R0 * SOMVR_VIG_K, r); }

float somvr_vig_dark(const somvr_vig_ring *g, float a) {
    if (!isfinite(a) || !(g->half_tan > 0)) return 0;
    a = clampf(fabsf(a), 0, 1.55f);
    return somvr_vig_profile(tanf(a) / g->half_tan);
}

static uint8_t a8(float a) { return (uint8_t)lrintf(clampf(a, 0, 1) * 255.0f); }

void somvr_vig_image(uint8_t *px, int bgra) {
    (void)bgra; /* black: the colour bytes are 0 in either order; the alpha is byte 3 in both */
    memset(px, 0, (size_t)SOMVR_VIG_IMG_W * SOMVR_VIG_IMG_H * 4);
    const float h = 0.5f * SOMVR_VIG_RING_PX;
    for (int y = 0; y < SOMVR_VIG_IMG_H; y++)
        for (int x = 0; x < SOMVR_VIG_IMG_W; x++) {
            float a;
            if (y < SOMVR_VIG_RING_PX) {
                float u = (x + 0.5f - h) / h, v = (y + 0.5f - h) / h;
                a = somvr_vig_profile(sqrtf(u * u + v * v));
            } else if (y < SOMVR_VIG_CELL_Y) {
                a = 1; /* the guard rows: the ring rect's bottom edge filters against opaque texels */
            } else {
                a = (float)(x / 4) / (SOMVR_VIG_CELLS - 1);
            }
            px[4 * ((size_t)y * SOMVR_VIG_IMG_W + x) + 3] = a8(a);
        }
}

/* build 12, the wraith tunnel (SOMVR_CFF_WRAITH): the same alphas, the colour the wraith's white-blue premultiplied by
 * them in linear light (the swapchain is sRGB; a quad's colour is premultiplied unless flagged otherwise) */
static uint8_t srgb8(float lin) {
    lin = clampf(lin, 0, 1);
    float e = lin <= 0.0031308f ? 12.92f * lin : 1.055f * powf(lin, 1 / 2.4f) - 0.055f;
    return (uint8_t)lrintf(clampf(e, 0, 1) * 255.0f);
}
void somvr_vig_image_wraith(uint8_t *px, int bgra) {
    somvr_vig_image(px, bgra);
    for (size_t i = 0; i < (size_t)SOMVR_VIG_IMG_W * SOMVR_VIG_IMG_H; i++) {
        float a = px[4 * i + 3] / 255.0f;
        uint8_t r = srgb8(SOMVR_VIG_WRAITH_R * a), g = srgb8(SOMVR_VIG_WRAITH_G * a), b = srgb8(SOMVR_VIG_WRAITH_B * a);
        px[4 * i + 0] = bgra ? b : r, px[4 * i + 1] = g, px[4 * i + 2] = bgra ? r : b;
    }
}

void somvr_vig_rect_ring(int32_t rc[4]) { rc[0] = 0, rc[1] = 0, rc[2] = SOMVR_VIG_RING_PX, rc[3] = SOMVR_VIG_RING_PX; }

int somvr_vig_cell(float alpha) {
    if (!isfinite(alpha)) return 0;
    return (int)lrintf(clampf(alpha, 0, 1) * (SOMVR_VIG_CELLS - 1));
}

void somvr_vig_rect_cell(int cell, int32_t rc[4]) {
    cell = cell < 0 ? 0 : cell >= SOMVR_VIG_CELLS ? SOMVR_VIG_CELLS - 1 : cell;
    rc[0] = 4 * cell + 1, rc[1] = SOMVR_VIG_CELL_Y + 1, rc[2] = 2, rc[3] = 6;
}
