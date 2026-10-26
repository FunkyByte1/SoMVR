/* void_view.c - never a void (HG-9): the loading space as a player sees it, both eyes side by side (a PPM), from the
 * layer's own pure code: SOMVR_XR_ENV's sky (somvr_env_sky), the floor and the progress lights (somvr_void.c, placed by
 * somvr_void_floor_pose / _lights_pose as the layer places them) and optionally the game's board (a PPM, e.g. a mono
 * dump, at the pose the layer logged). Blending as xr_compose.py: in the stored sRGB bytes, premultiplied. Not part of
 * --check; build: gcc -O2 -I../../../../third_party/... (see handoff/HANDOFF-void.md) -o void_view void_view.c
 * ../somvr_env.c ../somvr_void.c -lm
 *   void_view OUT.ppm [yaw=DEG] [pitch=DEG] [head=X,Y,Z] [placed=X,Y,Z,YAWDEG] [floor=Y] [level=0..1] [t=SECONDS]
 *            [board=PPM,X,Y,Z,YAWDEG,W,H] [fov=DEG] [px=N]
 * Defaults: the head at LOCAL's origin looking ahead, the space placed there (yaw 0), the floor 1.6 m below, the lights
 * at full level, t = 0, no board, 100 deg square fov per eye (about a Quest 2's), 640 px per eye. */
#include "../somvr_env.h"
#include "../somvr_void.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    somvr_posef pose;
    float w, h;
    int kind; /* 0 floor, 1 lights, 2 board */
} quad;

static uint8_t *g_board;
static int g_bw, g_bh;
static float g_level = 1.0f;
static int g_wave;

static void qrot(const float q[4], const float v[3], float o[3]) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float c1[3] = {y * v[2] - z * v[1], z * v[0] - x * v[2], x * v[1] - y * v[0]};
    float c2[3] = {y * c1[2] - z * c1[1], z * c1[0] - x * c1[2], x * c1[1] - y * c1[0]};
    for (int i = 0; i < 3; i++) o[i] = v[i] + 2.0f * (w * c1[i] + c2[i]);
}
static void qinv_rot(const float q[4], const float v[3], float o[3]) {
    float qi[4] = {-q[0], -q[1], -q[2], q[3]};
    qrot(qi, v, o);
}
static void yaw_pitch_q(float yaw, float pitch, float q[4]) { /* yaw + right (about -y), then pitch + up (about +x) */
    float qy[4] = {0, sinf(-0.5f * yaw), 0, cosf(-0.5f * yaw)}, qp[4] = {sinf(0.5f * pitch), 0, 0, cosf(0.5f * pitch)};
    q[0] = qy[3] * qp[0] + qy[0] * qp[3] + qy[1] * qp[2] - qy[2] * qp[1];
    q[1] = qy[3] * qp[1] - qy[0] * qp[2] + qy[1] * qp[3] + qy[2] * qp[0];
    q[2] = qy[3] * qp[2] + qy[0] * qp[1] - qy[1] * qp[0] + qy[2] * qp[3];
    q[3] = qy[3] * qp[3] - qy[0] * qp[0] - qy[1] * qp[1] - qy[2] * qp[2];
}
static float enc(float lin) {
    lin = lin < 0 ? 0 : lin > 1 ? 1 : lin;
    return lin <= 0.0031308f ? 12.92f * lin : 1.055f * powf(lin, 1.0f / 2.4f) - 0.055f;
}
static float dec(float s) { return s <= 0.04045f ? s / 12.92f : powf((s + 0.055f) / 1.055f, 2.4f); }

/* the quad's premultiplied colour (stored sRGB bytes / 255) at (u, v) */
static void quad_texel(const quad *q, float u, float v, float out[4]) {
    float lin[4];
    if (q->kind == 0) somvr_void_floor_texel(u, v, lin);
    else if (q->kind == 1) {
        int k = somvr_void_level_index(g_level);
        if (!k) {
            memset(out, 0, 4 * sizeof(float));
            return;
        }
        somvr_void_light_texel(g_wave, k, u, v, lin);
    } else {
        int x = (int)(u * g_bw), y = (int)(v * g_bh);
        x = x < 0 ? 0 : x >= g_bw ? g_bw - 1 : x, y = y < 0 ? 0 : y >= g_bh ? g_bh - 1 : y;
        const uint8_t *p = g_board + ((size_t)y * g_bw + x) * 3;
        out[0] = p[0] / 255.0f, out[1] = p[1] / 255.0f, out[2] = p[2] / 255.0f, out[3] = 1;
        return;
    }
    for (int i = 0; i < 3; i++) out[i] = enc(lin[i]); /* the layer stores sRGB of the premultiplied linear value */
    out[3] = lin[3];
}
static int quad_hit(const quad *q, const float o[3], const float d[3], float *t, float *u, float *v) {
    float lo[3] = {o[0] - q->pose.p[0], o[1] - q->pose.p[1], o[2] - q->pose.p[2]}, a[3], b[3];
    qinv_rot(q->pose.q, lo, a);
    qinv_rot(q->pose.q, d, b);
    if (fabsf(b[2]) < 1e-6f) return 0;
    *t = -a[2] / b[2];
    if (*t <= 0) return 0;
    float x = a[0] + *t * b[0], y = a[1] + *t * b[1];
    if (fabsf(x) > 0.5f * q->w || fabsf(y) > 0.5f * q->h) return 0;
    *u = x / q->w + 0.5f, *v = 0.5f - y / q->h;
    return 1;
}

static uint8_t *read_ppm(const char *path, int *w, int *h) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    int mx = 0;
    if (fscanf(f, "P6 %d %d %d", w, h, &mx) != 3 || mx != 255 || *w <= 0 || *h <= 0) return fclose(f), NULL;
    fgetc(f);
    uint8_t *p = malloc((size_t)*w * *h * 3);
    if (p && fread(p, 1, (size_t)*w * *h * 3, f) != (size_t)*w * *h * 3) free(p), p = NULL;
    fclose(f);
    return p;
}

int main(int argc, char **argv) {
    if (argc < 2) return fprintf(stderr, "usage: void_view OUT.ppm [key=value ...]\n"), 2;
    float yaw = 0, pitch = 0, head[3] = {0, 0, 0}, placed[4] = {0, 0, 0, 0}, floor_y = -SOMVR_VOID_EYE_M, fov = 100;
    float tsec = 0;
    int px = 640, nq = 2;
    quad qs[3];
    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        if (!strncmp(a, "yaw=", 4)) yaw = strtof(a + 4, NULL);
        else if (!strncmp(a, "pitch=", 6)) pitch = strtof(a + 6, NULL);
        else if (!strncmp(a, "head=", 5)) sscanf(a + 5, "%f,%f,%f", &head[0], &head[1], &head[2]);
        else if (!strncmp(a, "placed=", 7)) sscanf(a + 7, "%f,%f,%f,%f", &placed[0], &placed[1], &placed[2], &placed[3]);
        else if (!strncmp(a, "floor=", 6)) floor_y = strtof(a + 6, NULL);
        else if (!strncmp(a, "level=", 6)) g_level = strtof(a + 6, NULL);
        else if (!strncmp(a, "t=", 2)) tsec = strtof(a + 2, NULL);
        else if (!strncmp(a, "fov=", 4)) fov = strtof(a + 4, NULL);
        else if (!strncmp(a, "px=", 3)) px = atoi(a + 3);
        else if (!strncmp(a, "board=", 6)) {
            char path[1024];
            float bx, by, bz, byaw, bw, bh;
            if (sscanf(a + 6, "%1023[^,],%f,%f,%f,%f,%f,%f", path, &bx, &by, &bz, &byaw, &bw, &bh) != 7 ||
                !(g_board = read_ppm(path, &g_bw, &g_bh)))
                return fprintf(stderr, "board: %s unreadable\n", a + 6), 1;
            quad *b = &qs[2];
            yaw_pitch_q(byaw * 0.01745329f, 0, b->pose.q);
            b->pose.p[0] = bx, b->pose.p[1] = by, b->pose.p[2] = bz, b->w = bw, b->h = bh, b->kind = 2;
            nq = 3;
        } else
            return fprintf(stderr, "unknown %s\n", a), 2;
    }
    g_wave = somvr_void_wave(tsec);
    somvr_void_floor_pose(placed, floor_y, &qs[0].pose);
    qs[0].w = qs[0].h = SOMVR_VOID_FLOOR_M, qs[0].kind = 0;
    somvr_void_lights_pose(placed, placed[3] * 0.01745329f, floor_y, &qs[1].pose);
    qs[1].w = SOMVR_VOID_LIGHTS_W_M, qs[1].h = SOMVR_VOID_LIGHTS_H_M, qs[1].kind = 1;
    float hq[4];
    yaw_pitch_q(yaw * 0.01745329f, pitch * 0.01745329f, hq);
    uint8_t *img = malloc((size_t)2 * px * px * 3);
    float tn = tanf(0.5f * fov * 0.01745329f);
    for (int e = 0; e < 2; e++) {
        float off[3] = {e ? 0.032f : -0.032f, 0, 0}, eo[3];
        qrot(hq, off, eo);
        float o[3] = {head[0] + eo[0], head[1] + eo[1], head[2] + eo[2]};
        for (int y = 0; y < px; y++)
            for (int x = 0; x < px; x++) {
                float vd[3] = {(2.0f * (x + 0.5f) / px - 1.0f) * tn, (1.0f - 2.0f * (y + 0.5f) / px) * tn, -1.0f}, d[3];
                float l = sqrtf(vd[0] * vd[0] + vd[1] * vd[1] + 1.0f);
                for (int i = 0; i < 3; i++) vd[i] /= l;
                qrot(hq, vd, d);
                float sky[3], c[3];
                somvr_env_sky(d, sky); /* sRGB values (somvr_env.h: nothing brighter than 0.6) */
                for (int i = 0; i < 3; i++) c[i] = sky[i];
                for (int k = 0; k < nq; k++) { /* the layers in the layer's order: floor, lights, board */
                    float t, u, v, tx[4];
                    if (!quad_hit(&qs[k], o, d, &t, &u, &v)) continue;
                    quad_texel(&qs[k], u, v, tx);
                    for (int i = 0; i < 3; i++) c[i] = tx[i] + c[i] * (1.0f - tx[3]);
                }
                uint8_t *p = img + ((size_t)y * 2 * px + (size_t)e * px + x) * 3;
                for (int i = 0; i < 3; i++) p[i] = (uint8_t)lrintf(fminf(fmaxf(c[i], 0.0f), 1.0f) * 255.0f);
            }
    }
    (void)dec;
    FILE *f = fopen(argv[1], "wb");
    if (!f) return perror(argv[1]), 1;
    fprintf(f, "P6 %d %d 255\n", 2 * px, px);
    fwrite(img, 1, (size_t)2 * px * px * 3, f);
    fclose(f);
    printf("void_view: %s (%dx%d), yaw %.0f pitch %.0f, level %.2f wave %d%s\n", argv[1], 2 * px, px, yaw, pitch,
           g_level, g_wave, nq == 3 ? ", a board" : "");
    return 0;
}
