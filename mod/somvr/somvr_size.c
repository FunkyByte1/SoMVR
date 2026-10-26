/* somvr_size.c - see somvr_size.h */
#include "somvr_size.h"

#include <math.h>
#include <string.h>

float somvr_size_of_pct(double pct) {
    if (!(pct >= SOMVR_SIZE_MIN_PCT)) pct = SOMVR_SIZE_MIN_PCT; /* NaN too */
    if (pct > SOMVR_SIZE_MAX_PCT) pct = SOMVR_SIZE_MAX_PCT;
    return (float)(pct / 100.0);
}

float somvr_size_lift(float s, float eye_h_cm) {
    float l = (s - 1.0f) * eye_h_cm;
    return isfinite(l) ? l : 0.0f;
}

void somvr_size_cfg_default(somvr_size_cfg *c) { c->out_s = SOMVR_SIZE_OUT_S, c->in_s = SOMVR_SIZE_IN_S; }

void somvr_size_init(somvr_size *z, float s) {
    memset(z, 0, sizeof *z);
    z->cur = z->want = z->from = somvr_size_of_pct(s * 100.0);
    somvr_blink_reset(&z->blink);
}

int somvr_size_ask(somvr_size *z, float s) {
    float w = somvr_size_of_pct(s * 100.0);
    int news = fabsf(w - z->want) > 1e-4f; /* (the same again, or what is shown with nothing pending: nothing new) */
    z->want = w;
    z->asks += news;
    return news;
}

float somvr_size_frame(somvr_size *z, const somvr_size_cfg *c, int hold, double t) {
    z->frames++;
    z->landed = 0;
    int pending = fabsf(z->want - z->cur) > 1e-4f;
    int want = pending && !hold;
    z->held_frames += (uint64_t)(pending && hold);
    float a = 0;
    int go = somvr_blink_frame(&z->blink, c->out_s, c->in_s, want, t, &a);
    if (go && want) {
        z->from = z->cur;
        z->cur = z->want;
        z->landed = 1;
        z->lands++;
    }
    z->alpha = a;
    return a;
}

int somvr_size_steps(const float *h, int n, float min_cm, float *out, int *idx, int cap) {
    int k = 0;
    for (int i = 0; i + 1 < n; i++) {
        float d = h[i + 1] - h[i];
        if (!isfinite(d) || !(fabsf(d) >= min_cm)) continue;
        if (k < cap) out[k] = d, idx[k] = i;
        k++;
    }
    return k;
}
