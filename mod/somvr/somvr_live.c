/* somvr_live.c - the live loop's card, calm gate and folder formats (somvr_live.h; docs/re-xr-pipeline.md 162). */
#include "somvr_live.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the text --------------------------------------------------------------------------------------------------- */
/* one UTF-8 character at *s -> an ASCII byte (0 = none: skipped); *s advanced past it */
static int fold(const unsigned char **s) {
    const unsigned char *p = *s;
    unsigned c = p[0];
    if (c < 0x80) {
        *s = p + 1;
        return c == '\t' ? ' ' : c == '\r' ? 0 : c;
    }
    int n = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
    for (int i = 1; i < n; i++)
        if ((p[i] & 0xc0) != 0x80) n = i; /* a broken sequence: what is there */
    unsigned cp = n == 2 ? (c & 0x1fu) << 6 | (p[1] & 0x3fu)
                  : n == 3 ? (c & 0x0fu) << 12 | (p[1] & 0x3fu) << 6 | (p[2] & 0x3fu) : 0;
    *s = p + n;
    if (cp == 0x2018 || cp == 0x2019 || cp == 0x2032) return '\'';
    if (cp == 0x201c || cp == 0x201d) return '"';
    if (cp == 0x2013 || cp == 0x2014 || cp == 0x2212) return '-';
    if (cp == 0x2026) return '.'; /* (one dot: rare enough) */
    if (cp == 0xb0) return SOMVR_FONT_DEG;
    if (cp == 0xa0) return ' ';
    return '?';
}

int somvr_lv_wrap(const char *text, char out[][SOMVR_LV_COLS + 1], int max_lines, int cols, int *more) {
    if (more) *more = 0;
    if (cols > SOMVR_LV_COLS) cols = SOMVR_LV_COLS;
    if (!text || max_lines <= 0 || cols < 4) return 0;
    /* the folded text first (bounded) */
    char a[SOMVR_LV_TEXT_MAX + 1];
    size_t na = 0;
    const unsigned char *s = (const unsigned char *)text;
    while (*s && na < SOMVR_LV_TEXT_MAX) {
        int ch = fold(&s);
        if (ch) a[na++] = (char)ch;
    }
    int cut = *s != 0;
    a[na] = 0;
    int n = 0, len = 0;
    char *cur = out[0];
    cur[0] = 0;
    size_t i = 0;
    while (i < na) {
        if (a[i] == '\n') { /* a break */
            i++;
            if (n + 1 >= max_lines) { cut = cut || i < na; break; }
            cur = out[++n], cur[0] = 0, len = 0;
            continue;
        }
        if (a[i] == ' ') { i++; continue; }
        size_t j = i;
        while (j < na && a[j] != ' ' && a[j] != '\n') j++;
        int w = (int)(j - i);
        int need = len ? len + 1 + w : w;
        if (need > cols && len) { /* the next line */
            if (n + 1 >= max_lines) { cut = 1; break; }
            cur = out[++n], cur[0] = 0, len = 0;
            continue;
        }
        if (len) cur[len++] = ' ';
        int take = w > cols - len ? cols - len : w; /* a word longer than a line: cut, the rest on the next */
        memcpy(cur + len, a + i, (size_t)take);
        len += take, cur[len] = 0;
        i += (size_t)take;
        if (take < w) {
            if (n + 1 >= max_lines) { cut = 1; break; }
            cur = out[++n], cur[0] = 0, len = 0;
        }
    }
    int used = n + 1;
    if (used == 1 && !out[0][0]) used = 0;
    if (cut && used > 0) {
        char *l = out[used - 1];
        int k = (int)strlen(l);
        if (k > cols - 3) k = cols - 3;
        while (k > 0 && l[k - 1] == ' ') k--;
        memcpy(l + k, "...", 4);
        if (more) *more = 1;
    }
    return used;
}

void somvr_lv_card_make(somvr_lv_card *c, int kind, const char *title, const char *text, const char *hint) {
    memset(c, 0, sizeof *c);
    c->kind = kind;
    char t[1][SOMVR_LV_COLS + 1];
    int m = 0;
    if (somvr_lv_wrap(title ? title : "", t, 1, SOMVR_LV_COLS, &m) == 1) memcpy(c->title, t[0], sizeof c->title);
    c->n = somvr_lv_wrap(text ? text : "", c->line, SOMVR_LV_LINES, SOMVR_LV_COLS, &c->more);
    if (somvr_lv_wrap(hint ? hint : "", t, 1, SOMVR_LV_COLS, &m) == 1) memcpy(c->hint, t[0], sizeof c->hint);
    somvr_lv_speech(kind, title, text, c->speech, sizeof c->speech);
}

/* b37: the spoken line */
static size_t sp_put(char *out, size_t cap, size_t n, const char *s, size_t len) {
    for (size_t i = 0; i < len && n + 1 < cap; i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch == '\t' || ch == '\r') ch = ' ';
        if (ch < 0x20 && ch != '\n') continue;
        out[n++] = (char)ch;
    }
    return n;
}
size_t somvr_lv_speech(int kind, const char *title, const char *text, char *out, size_t cap) {
    if (!out || !cap) return 0;
    char tmp[SOMVR_LV_SPEECH_MAX * 2];
    size_t n = 0;
    if (title && title[0] && kind != SOMVR_LVK_SAY) {
        n = sp_put(tmp, sizeof tmp, n, title, strlen(title));
        n = sp_put(tmp, sizeof tmp, n, "\n", 1);
    }
    if (text) n = sp_put(tmp, sizeof tmp, n, text, strlen(text));
    tmp[n] = 0;
    /* the breaks: a sentence's end each; runs of spaces folded */
    size_t o = 0;
    for (size_t i = 0; i < n && o + 3 < cap; i++) {
        char ch = tmp[i];
        if (ch == '\n') {
            while (o > 0 && out[o - 1] == ' ') o--;
            if (o > 0 && !strchr(".!?:", out[o - 1])) out[o++] = '.';
            if (o > 0) out[o++] = ' ';
            continue;
        }
        if (ch == ' ' && (o == 0 || out[o - 1] == ' ')) continue;
        out[o++] = ch;
    }
    while (o > 0 && out[o - 1] == ' ') o--;
    if (o + 3 >= cap && o > 0) { /* cut: back to the last space */
        size_t sp = o;
        while (sp > 0 && out[sp - 1] != ' ') sp--;
        if (sp > cap / 2) o = sp - 1;
    }
    out[o] = 0;
    return o;
}

/* ---- the picture ------------------------------------------------------------------------------------------------ */
static void put(uint8_t *px, int bgra, int x, int y, const uint8_t c[4], float cov) {
    if (x < 0 || y < 0 || x >= SOMVR_LV_TEX_W || y >= SOMVR_LV_TEX_H || cov <= 0) return;
    if (cov > 1) cov = 1;
    uint8_t *p = px + ((size_t)y * SOMVR_LV_TEX_W + (size_t)x) * 4;
    float a = c[3] / 255.0f * cov;
    uint8_t r = (uint8_t)lrintf(c[0] * a), g = (uint8_t)lrintf(c[1] * a), b = (uint8_t)lrintf(c[2] * a);
    p[0] = bgra ? b : r, p[1] = g, p[2] = bgra ? r : b, p[3] = (uint8_t)lrintf(a * 255.0f);
}
/* the rounded rectangle's coverage at pixel (x, y) (its centre +0.5): 1 inside, a pixel's soft edge */
static float rrect(float x, float y, float x0, float y0, float x1, float y1, float r) {
    float cx = x < x0 + r ? x0 + r : x > x1 - r ? x1 - r : x, cy = y < y0 + r ? y0 + r : y > y1 - r ? y1 - r : y;
    float d = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy));
    float in = r - d + 0.5f;
    if (x < x0 || x > x1 || y < y0 || y > y1) return 0;
    return in < 0 ? 0 : in > 1 ? 1 : in;
}

static const uint8_t C_BG[4] = {16, 19, 24, 240}, C_TITLE_SAY[4] = {255, 200, 90, 255},
                     C_TITLE_PATCH[4] = {120, 220, 255, 255}, C_TITLE_RES[4] = {140, 235, 140, 255},
                     C_TITLE_WARN[4] = {255, 130, 110, 255}, C_TEXT[4] = {235, 235, 230, 255},
                     C_DIM[4] = {150, 150, 150, 255}, C_DOT[4] = {255, 185, 60, 255};

static const uint8_t *kind_col(int k) {
    return k == SOMVR_LVK_PATCH ? C_TITLE_PATCH : k == SOMVR_LVK_RESULT ? C_TITLE_RES
                                              : k == SOMVR_LVK_WARN ? C_TITLE_WARN : C_TITLE_SAY;
}

void somvr_lv_draw(const somvr_font *f, uint8_t *px, int bgra, const somvr_lv_card *c) {
    memset(px, 0, (size_t)SOMVR_LV_TEX_W * SOMVR_LV_TEX_H * 4);
    const uint8_t *fc = kind_col(c ? c->kind : 0);
    float x0 = 1, y0 = 1, x1 = SOMVR_LV_TEX_W - 1, y1 = SOMVR_LV_CARD_H - 1, R = 18, B = 3;
    for (int y = 0; y < SOMVR_LV_CARD_H; y++)
        for (int x = 0; x < SOMVR_LV_TEX_W; x++) {
            float px_ = x + 0.5f, py = y + 0.5f;
            float o = rrect(px_, py, x0, y0, x1, y1, R);
            if (o <= 0) continue;
            float i = rrect(px_, py, x0 + B, y0 + B, x1 - B, y1 - B, R - B);
            uint8_t col[4];
            for (int k = 0; k < 4; k++) col[k] = (uint8_t)lrintf(C_BG[k] * i + fc[k] * (1 - i));
            col[3] = (uint8_t)lrintf(C_BG[3] * i + 255 * (1 - i));
            put(px, bgra, x, y, col, o);
        }
    if (c && f) {
        int y = 16;
        somvr_font_draw(f, px, SOMVR_LV_TEX_W, SOMVR_LV_TEX_H, 20, y, c->title, 2, fc, bgra);
        y += 34;
        for (int k = 0; k < SOMVR_LV_TEX_W - 40; k++) put(px, bgra, 20 + k, y, fc, 0.6f); /* a rule under the title */
        y += 10;
        for (int i = 0; i < c->n && i < SOMVR_LV_LINES; i++, y += 30)
            somvr_font_draw(f, px, SOMVR_LV_TEX_W, SOMVR_LV_TEX_H, 20, y, c->line[i], 2, C_TEXT, bgra);
        if (c->hint[0])
            somvr_font_draw(f, px, SOMVR_LV_TEX_W, SOMVR_LV_TEX_H, 20, SOMVR_LV_CARD_H - 44, c->hint, 2, C_DIM, bgra);
    }
    /* the dot: a soft amber disc with a light core */
    float r = SOMVR_LV_DOT_PX / 2.0f - 1.5f, cx = SOMVR_LV_DOT_PX / 2.0f, cy = SOMVR_LV_DOT_Y + SOMVR_LV_DOT_PX / 2.0f;
    for (int y = SOMVR_LV_DOT_Y; y < SOMVR_LV_DOT_Y + SOMVR_LV_DOT_PX; y++)
        for (int x = 0; x < SOMVR_LV_DOT_PX; x++) {
            float d = sqrtf((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
            float cov = r - d + 0.5f;
            if (cov <= 0) continue;
            uint8_t col[4] = {C_DOT[0], C_DOT[1], C_DOT[2], 255};
            float core = d < r * 0.45f ? 1 : 0;
            for (int k = 0; k < 3; k++) col[k] = (uint8_t)lrintf(col[k] + (255 - col[k]) * 0.6f * core);
            put(px, bgra, x, y, col, cov);
        }
}

/* ---- b37 live3: the card's place and the dot's ------------------------------------------------------------------ */
static void lv_qrot(const float q[4], const float v[3], float out[3]) { /* v' = v + 2w (u x v) + 2 u x (u x v) */
    float u[3] = {q[0], q[1], q[2]}, w = q[3];
    float t[3] = {2 * (u[1] * v[2] - u[2] * v[1]), 2 * (u[2] * v[0] - u[0] * v[2]), 2 * (u[0] * v[1] - u[1] * v[0])};
    out[0] = v[0] + w * t[0] + (u[1] * t[2] - u[2] * t[1]);
    out[1] = v[1] + w * t[1] + (u[2] * t[0] - u[0] * t[2]);
    out[2] = v[2] + w * t[2] + (u[0] * t[1] - u[1] * t[0]);
}
static int lv_unit(float v[3]) {
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!(l > 1e-6f) || !isfinite(l)) return 0;
    v[0] /= l, v[1] /= l, v[2] /= l;
    return 1;
}
static int lv_qok(const float q[4]) {
    float n = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    return isfinite(n) && n > 0.9f && n < 1.1f;
}
/* the rotation whose matrix has the orthonormal right-handed columns x, y, z */
static void lv_qbasis(const float x[3], const float y[3], const float z[3], float q[4]) {
    float tr = x[0] + y[1] + z[2];
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
    float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int i = 0; i < 4; i++) q[i] /= n;
}
/* a pose at p facing along z (its +Z toward the viewer), upright as near the world's +Y as z allows (else hint) */
static void lv_facing(const float p[3], const float zin[3], const float hint[3], somvr_posef *out) {
    float z[3] = {zin[0], zin[1], zin[2]}, x[3], y[3];
    lv_unit(z);
    static const float up[3] = {0, 1, 0};
    const float *u = fabsf(z[1]) < 0.9f ? up : hint;
    x[0] = u[1] * z[2] - u[2] * z[1], x[1] = u[2] * z[0] - u[0] * z[2], x[2] = u[0] * z[1] - u[1] * z[0];
    lv_unit(x);
    y[0] = z[1] * x[2] - z[2] * x[1], y[1] = z[2] * x[0] - z[0] * x[2], y[2] = z[0] * x[1] - z[1] * x[0];
    lv_qbasis(x, y, z, out->q);
    memcpy(out->p, p, sizeof out->p);
}
int somvr_lv_card_place(const somvr_posef *head, const somvr_posef *grip, int hand, somvr_posef *out) {
    if (!lv_qok(head->q) || !isfinite(grip->p[0] + grip->p[1] + grip->p[2] + head->p[0] + head->p[1] + head->p[2]))
        return 0;
    static const float f0[3] = {0, 0, -1};
    float f[3];
    lv_qrot(head->q, f0, f);
    f[1] = 0;
    if (!lv_unit(f)) f[0] = 0, f[1] = 0, f[2] = -1; /* (looking straight up or down: XR's -Z) */
    float r[3] = {-f[2], 0, f[0]}; /* the head's right on the level */
    float inner = hand == 0 ? 1.0f : -1.0f;
    float c[3];
    for (int i = 0; i < 3; i++) c[i] = grip->p[i] + inner * 0.18f * r[i];
    float d[2] = {c[0] - head->p[0], c[2] - head->p[2]};
    float dh = sqrtf(d[0] * d[0] + d[1] * d[1]);
    float side = d[0] * r[0] + d[1] * r[2], ahead = d[0] * f[0] + d[1] * f[2];
    float ang = dh > 0.05f ? atan2f(side, ahead) : 0.0f, lim = 45.0f * 3.14159265f / 180.0f;
    if (ang > lim) ang = lim;
    if (ang < -lim) ang = -lim;
    if (dh < 0.45f) dh = 0.45f;
    if (dh > 0.65f) dh = 0.65f;
    float dir[3] = {f[0] * cosf(ang) + r[0] * sinf(ang), 0, f[2] * cosf(ang) + r[2] * sinf(ang)};
    float p[3] = {head->p[0] + dir[0] * dh, grip->p[1] + 0.10f, head->p[2] + dir[2] * dh};
    if (p[1] > head->p[1] - 0.12f) p[1] = head->p[1] - 0.12f;
    if (p[1] < head->p[1] - 0.35f) p[1] = head->p[1] - 0.35f;
    float z[3] = {head->p[0] - p[0], head->p[1] - p[1], head->p[2] - p[2]};
    lv_facing(p, z, f, out);
    return 1;
}
int somvr_lv_dot_place(const somvr_posef *grip, int hand, const somvr_posef *head, somvr_posef *out) {
    if (!lv_qok(grip->q)) return 0;
    static const float x0[3] = {1, 0, 0}, f0[3] = {0, 0, -1};
    float n[3], zf[3];
    lv_qrot(grip->q, x0, n); /* out of the left palm, into the right one */
    lv_qrot(grip->q, f0, zf);
    float back[3], p[3];
    for (int i = 0; i < 3; i++) back[i] = (hand == 0 ? -1.0f : 1.0f) * n[i];
    for (int i = 0; i < 3; i++) p[i] = grip->p[i] + 0.04f * back[i] + 0.01f * zf[i];
    lv_facing(p, back, zf, out);
    if (!head || !lv_qok(head->q)) return 0;
    float v[3] = {head->p[0] - p[0], head->p[1] - p[1], head->p[2] - p[2]};
    if (!lv_unit(v)) return 0;
    return back[0] * v[0] + back[1] * v[1] + back[2] * v[2] > 0.15f;
}

/* ---- the calm gate ---------------------------------------------------------------------------------------------- */
void somvr_lv_ccfg_default(somvr_lv_ccfg *c) {
    c->calm_s = 3.0;
    c->mask = 1u | 2u | 4u | 8u | 16u | 64u; /* bow, climb, swing, threat, soft swing, fight (not the card's 32) */
}
int somvr_lv_calm_step(somvr_lv_calm *s, const somvr_lv_ccfg *c, double t, uint32_t block) {
    if (!s->init) s->init = 1, s->busy_t = t, s->calm = 0;
    uint32_t b = block & c->mask;
    if (b) s->busy_t = t, s->why = b;
    int calm = !b && t - s->busy_t >= c->calm_s;
    if (calm != s->calm) {
        if (calm) s->n_calm++;
        else s->n_busy++;
        s->calm = calm;
    }
    return calm;
}

/* ---- the folder's formats --------------------------------------------------------------------------------------- */
int somvr_lv_kv(const char *txt, size_t n, const char *key, char *out, size_t cap) {
    size_t kl = strlen(key), i = 0;
    if (cap) out[0] = 0;
    while (i < n) {
        size_t e = i;
        while (e < n && txt[e] != '\n') e++;
        if (e - i > kl && !memcmp(txt + i, key, kl) && txt[i + kl] == '=') {
            size_t v = i + kl + 1, len = e - v;
            while (len && (txt[v + len - 1] == '\r' || txt[v + len - 1] == ' ')) len--;
            if (cap) {
                if (len >= cap) len = cap - 1;
                memcpy(out, txt + v, len), out[len] = 0;
            }
            return 1;
        }
        i = e + 1;
    }
    return 0;
}
int somvr_lv_patch_parse(const char *txt, size_t n, somvr_lv_patch *p) {
    memset(p, 0, sizeof *p);
    char v[32];
    if (!somvr_lv_kv(txt, n, "id", v, sizeof v) || (p->id = atoi(v)) <= 0) return 0;
    if (!somvr_lv_kv(txt, n, "kind", v, sizeof v)) return 0;
    p->live = !strcmp(v, "live");
    somvr_lv_kv(txt, n, "what", p->what, sizeof p->what);
    somvr_lv_kv(txt, n, "try", p->try_, sizeof p->try_);
    somvr_lv_kv(txt, n, "why", p->why, sizeof p->why);
    return 1;
}
int somvr_lv_state_parse(const char *txt, size_t n, somvr_lv_state *s) {
    memset(s, 0, sizeof *s);
    char v[32];
    if (!somvr_lv_kv(txt, n, "seq", v, sizeof v)) return 0;
    s->seq = (uint32_t)strtoul(v, NULL, 10);
    if (somvr_lv_kv(txt, n, "current", v, sizeof v)) s->current = atoi(v);
    if (somvr_lv_kv(txt, n, "id", v, sizeof v)) s->id = atoi(v);
    somvr_lv_kv(txt, n, "last", s->last, sizeof s->last);
    somvr_lv_kv(txt, n, "msg", s->msg, sizeof s->msg);
    return 1;
}
int somvr_lv_request_parse(const char *txt, size_t n, int *op, int *id, uint32_t *seq) {
    char b[96];
    if (n >= sizeof b) n = sizeof b - 1;
    memcpy(b, txt, n), b[n] = 0;
    char w[16];
    int i = 0;
    unsigned long sq = 0;
    if (sscanf(b, "%15s %d %lu", w, &i, &sq) != 3 || i < 0) return 0;
    *op = !strcmp(w, "apply") ? 1 : !strcmp(w, "undo") ? 2 : 0;
    if (!*op) return 0;
    *id = i, *seq = (uint32_t)sq;
    return 1;
}
int somvr_lv_result_card(const somvr_lv_state *s, const somvr_lv_patch *p, somvr_lv_card *c) {
    char title[64], text[400];
    int kind = SOMVR_LVK_RESULT;
    const char *try_ = p && p->id == s->id ? p->try_ : "";
    if (!strcmp(s->last, "applied")) {
        snprintf(title, sizeof title, "Patch %d is in", s->id);
        snprintf(text, sizeof text, "%s%s%s", try_[0] ? "Try: " : "", try_, s->msg[0] && !try_[0] ? s->msg : "");
    } else if (!strcmp(s->last, "start")) { /* b40 live4: staged before the launch, the session's module from its start */
        snprintf(title, sizeof title, "Patch %d applied at start", s->id);
        snprintf(text, sizeof text, "%s%s%s", try_[0] ? "Try: " : "", try_,
                 try_[0] ? "" : "It is in since the game started. Undo: menu > Test tab.");
    } else if (!strcmp(s->last, "undone")) {
        snprintf(title, sizeof title, "Patch %d undone", s->id);
        snprintf(text, sizeof text, "%s", s->current ? "Back to the patch before it." : "Back to this session's own build.");
    } else if (!strcmp(s->last, "refused") || !strcmp(s->last, "bad")) {
        kind = SOMVR_LVK_WARN;
        /* b37: refused while that patch is still the live one = its Undo (Apply is never offered for the live one) */
        if (s->current == s->id && s->id > 0) snprintf(title, sizeof title, "Patch %d still in", s->id);
        else snprintf(title, sizeof title, "Patch %d not applied", s->id);
        snprintf(text, sizeof text, "%s", s->msg[0] ? s->msg : "Try again at a calm moment.");
    } else if (!strcmp(s->last, "stub") || !strcmp(s->last, "restart")) {
        kind = SOMVR_LVK_WARN;
        snprintf(title, sizeof title, "Patch %d: after a restart", s->id);
        snprintf(text, sizeof text, "%s", s->msg[0] ? s->msg : "It cannot be swapped in while the game runs.");
    } else {
        return 0;
    }
    somvr_lv_card_make(c, kind, title, text, "Tap to close");
    return 1;
}
