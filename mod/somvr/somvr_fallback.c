/* somvr_fallback.c - see somvr_fallback.h */
#include "somvr_fallback.h"

#include <math.h>
#include <string.h>

const char *const somvr_fb_cond_names[SOMVR_FB_NCOND] = { "cine",  "scripted", "pres",  "sync",  "struggle",
                                                          "mount", "attach",   "climb", "death", "photo",
                                                          "spect", "tag",      "forced" };
const char *const somvr_fb_phase_names[4] = { "idle", "out", "black", "in" };

uint32_t somvr_fb_detect(const somvr_fb_in *in) {
    uint32_t c = 0, h = in->have, tg = (h & SOMVR_FBR_TAGS) ? in->tag_classes : 0;
    if ((h & SOMVR_FBR_CINE) && in->cine) c |= SOMVR_FB_BIT(SOMVR_FB_CINE);
    if ((h & SOMVR_FBR_CAM) && in->scripted) c |= SOMVR_FB_BIT(SOMVR_FB_SCRIPTED);
    if ((h & SOMVR_FBR_PRES) && in->pres) c |= SOMVR_FB_BIT(SOMVR_FB_PRES);
    if ((h & SOMVR_FBR_SYNC) && in->sync) c |= SOMVR_FB_BIT(SOMVR_FB_SYNC);
    if ((h & SOMVR_FBR_STRUGGLE) && (in->struggle[0] || in->struggle[1])) c |= SOMVR_FB_BIT(SOMVR_FB_STRUGGLE);
    if ((h & SOMVR_FBR_MOUNT) && in->mount) c |= SOMVR_FB_BIT(SOMVR_FB_MOUNT);
    if ((h & SOMVR_FBR_ATTACH) && in->attach) c |= SOMVR_FB_BIT(SOMVR_FB_ATTACH);
    if (((h & SOMVR_FBR_FSM) && in->fsm == SOMVR_FB_FSM_TRANSITION) || (tg & SOMVR_FB_TAGC_CLIMB))
        c |= SOMVR_FB_BIT(SOMVR_FB_CLIMB);
    if (((h & SOMVR_FBR_LIFE) && in->life == SOMVR_FB_LIFE_DYING) || (tg & SOMVR_FB_TAGC_DEATH))
        c |= SOMVR_FB_BIT(SOMVR_FB_DEATH);
    if ((h & SOMVR_FBR_PHOTO) && in->photo != SOMVR_FB_PHOTO_OFF) c |= SOMVR_FB_BIT(SOMVR_FB_PHOTO);
    if ((h & SOMVR_FBR_SPECT) && in->spect != 0) c |= SOMVR_FB_BIT(SOMVR_FB_SPECT);
    if (tg & SOMVR_FB_TAGC_OTHER) c |= SOMVR_FB_BIT(SOMVR_FB_TAG);
    return c;
}

/* the next comma-separated item of *p (spaces around it dropped) into out; 0 = no more items */
static int next_item(const char **p, char *out, size_t cap, int *too_long) {
    const char *s = *p;
    while (*s == ' ' || *s == ',') s++;
    if (!*s) return *p = s, 0;
    const char *e = s;
    while (*e && *e != ',') e++;
    const char *b = e;
    while (b > s && b[-1] == ' ') b--;
    size_t n = (size_t)(b - s);
    *too_long = n >= cap;
    if (n >= cap) n = cap - 1;
    memcpy(out, s, n);
    out[n] = 0;
    *p = e;
    return 1;
}

int somvr_fb_tags_parse(const char *spec, somvr_fb_tags *t) {
    memset(t, 0, sizeof *t);
    if (!spec || !strcmp(spec, "none")) return 0;
    int rc = 0, tl;
    char it[SOMVR_FB_TAGLEN + 16];
    while (next_item(&spec, it, sizeof it, &tl)) {
        uint32_t cls = SOMVR_FB_TAGC_OTHER;
        const char *nm = it, *eq = strchr(it, '=');
        if (eq) {
            size_t k = (size_t)(eq - it);
            if (k == 5 && !strncmp(it, "death", 5)) cls = SOMVR_FB_TAGC_DEATH;
            else if (k == 5 && !strncmp(it, "climb", 5)) cls = SOMVR_FB_TAGC_CLIMB;
            else if ((k == 3 && !strncmp(it, "tag", 3)) || (k == 5 && !strncmp(it, "other", 5))) cls = SOMVR_FB_TAGC_OTHER;
            else cls = 0;
            nm = eq + 1;
        }
        size_t n = strlen(nm);
        int prefix = n > 0 && nm[n - 1] == '*';
        if (prefix) n--;
        if (!cls || tl || n == 0 || n >= SOMVR_FB_TAGLEN || t->n >= SOMVR_FB_MAXTAGS) {
            rc = -1;
            continue;
        }
        memcpy(t->name[t->n], nm, n);
        t->name[t->n][n] = 0;
        t->prefix[t->n] = (uint8_t)prefix;
        t->cls[t->n] = cls;
        t->n++;
    }
    return rc;
}

uint32_t somvr_fb_tag_class(const somvr_fb_tags *t, const char *name) {
    uint32_t c = 0;
    if (!name || !name[0]) return 0;
    for (int i = 0; i < t->n; i++) {
        size_t n = strlen(t->name[i]);
        if (t->prefix[i] ? !strncmp(name, t->name[i], n) : !strcmp(name, t->name[i])) c |= t->cls[i];
    }
    return c;
}

int somvr_fb_cond_parse(const char *spec, uint32_t *bits) {
    if (!spec || !strcmp(spec, "all")) return *bits = SOMVR_FB_ALL, 0;
    *bits = 0;
    if (!strcmp(spec, "none")) return 0;
    int rc = 0, tl;
    char it[32];
    while (next_item(&spec, it, sizeof it, &tl)) {
        int k = -1;
        for (int i = 0; i < SOMVR_FB_NCOND && k < 0 && !tl; i++)
            if (!strcmp(it, somvr_fb_cond_names[i])) k = i;
        if (k < 0) rc = -1;
        else *bits |= SOMVR_FB_BIT(k);
    }
    return rc;
}

void somvr_fb_sw_cfg_default(somvr_fb_sw_cfg *c) {
    c->enabled = SOMVR_FB_ALL;
    c->enter_s = 0.10f, c->exit_s = 0.40f, c->hold_s = 1.5f;
    c->sync_s = 0.6f, c->sync_cm = 75.0f;
}

void somvr_fb_sw_init(somvr_fb_sw *s) {
    memset(s, 0, sizeof *s);
    s->cand = -1;
}

int somvr_fb_sw_step(somvr_fb_sw *s, const somvr_fb_sw_cfg *c, uint32_t raw, const float *root, int hold, double t) {
    double hold_s = c->hold_s < 1.0f ? 1.0 : (double)c->hold_s; /* never more than one toggle per second */
    if (!s->started) s->started = 1, s->since = t - hold_s, s->cand = -1;
    /* a paired action counts once it has lasted sync_s or carried Talion sync_cm (the design's "long, large motion") */
    int sync = (raw & SOMVR_FB_BIT(SOMVR_FB_SYNC)) != 0;
    if (sync && !s->sync_on) {
        s->sync_on = 1, s->sync_t0 = t, s->sync_cm = 0, s->sync_have_p0 = 0;
        s->sync_ok = !(c->sync_s > 0) && !(c->sync_cm > 0);
    } else if (!sync) {
        s->sync_on = s->sync_ok = 0;
    }
    if (s->sync_on && root) {
        if (!s->sync_have_p0) memcpy(s->sync_p0, root, sizeof s->sync_p0), s->sync_have_p0 = 1;
        float dx = root[0] - s->sync_p0[0], dy = root[1] - s->sync_p0[1], dz = root[2] - s->sync_p0[2];
        float d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d > s->sync_cm) s->sync_cm = d; /* NaN never wins */
    }
    if (s->sync_on && !s->sync_ok &&
        ((c->sync_s > 0 && t - s->sync_t0 >= c->sync_s) || (c->sync_cm > 0 && s->sync_cm >= c->sync_cm)))
        s->sync_ok = 1;
    uint32_t cond = raw & (c->enabled | SOMVR_FB_BIT(SOMVR_FB_FORCED));
    if (!s->sync_ok) cond &= ~SOMVR_FB_BIT(SOMVR_FB_SYNC);
    s->raw = raw, s->cond = cond;
    int wish = cond != 0;
    if (hold || wish == s->want) {
        s->cand = -1;
        return 0;
    }
    if (s->cand < 0) s->cand = t;
    double deb = wish ? c->enter_s : c->exit_s;
    if (t - s->cand + 1e-9 < deb || t - s->since + 1e-9 < hold_s) return 0;
    s->want = wish, s->since = t, s->cand = -1, s->switches++;
    return 1;
}

void somvr_fb_fade_cfg_default(somvr_fb_fade_cfg *c) { c->out_s = 0.15f, c->black_s = 0.06f, c->in_s = 0.20f; }

void somvr_fb_tr_init(somvr_fb_tr *tr, int shown) {
    memset(tr, 0, sizeof *tr);
    tr->shown = shown;
}

void somvr_fb_tr_step(somvr_fb_tr *tr, const somvr_fb_fade_cfg *c, int want, double t, somvr_fb_tr_out *o) {
    int prev = tr->phase;
    float a = tr->alpha;
    o->swapped = 0;
    if (!(c->out_s > 0) && !(c->black_s > 0) && !(c->in_s > 0)) { /* a plain cut */
        if (want != tr->shown) tr->shown = want, tr->swaps++, o->swapped = 1;
        tr->phase = SOMVR_FBT_IDLE, a = 0;
    } else {
        for (int step = 0; step < 4; step++) { /* at most two phase changes per frame */
            if (tr->phase == SOMVR_FBT_IDLE) {
                if (want == tr->shown) {
                    a = 0;
                    break;
                }
                tr->phase = SOMVR_FBT_OUT, tr->t0 = t, a = 0;
                continue;
            }
            if (tr->phase == SOMVR_FBT_OUT) {
                if (want == tr->shown) { /* changed its mind before black: back in from this level */
                    tr->phase = SOMVR_FBT_IN, tr->t0 = c->in_s > 0 ? t - (1.0 - a) * c->in_s : t;
                    continue;
                }
                a = c->out_s > 0 ? (float)((t - tr->t0) / c->out_s) : 1.0f;
                if (a < 1.0f) {
                    a = a > 0 ? a : 0;
                    break;
                }
                a = 1.0f; /* black: the base changes with this frame */
                tr->shown = want, tr->swaps++, o->swapped = 1;
                tr->phase = SOMVR_FBT_BLACK, tr->t0 = t, tr->black_frames = 1;
                break;
            }
            if (tr->phase == SOMVR_FBT_BLACK) {
                a = 1.0f;
                if (want != tr->shown) { /* still black: swap again, the black starts over */
                    tr->shown = want, tr->swaps++, o->swapped = 1;
                    tr->t0 = t, tr->black_frames = 1;
                    break;
                }
                if (tr->black_frames >= 2 && t - tr->t0 >= c->black_s) {
                    tr->phase = SOMVR_FBT_IN, tr->t0 = t;
                    continue;
                }
                tr->black_frames++;
                break;
            }
            /* IN */
            if (want != tr->shown) { /* changed its mind while coming back: out again from this level */
                tr->phase = SOMVR_FBT_OUT, tr->t0 = c->out_s > 0 ? t - a * c->out_s : t;
                continue;
            }
            a = c->in_s > 0 ? 1.0f - (float)((t - tr->t0) / c->in_s) : 0.0f;
            if (a > 0) {
                a = a < 1.0f ? a : 1.0f;
                break;
            }
            a = 0, tr->phase = SOMVR_FBT_IDLE;
            break;
        }
    }
    if (!(a >= 0)) a = 0; /* NaN */
    tr->alpha = a;
    o->alpha = a;
    o->shown = tr->shown;
    o->phase_changed = tr->phase != prev;
}

uint32_t somvr_fb_fade_level(float alpha) {
    if (!(alpha > 0)) return 0;
    if (alpha >= 1.0f) return 255;
    uint32_t l = (uint32_t)(alpha * 255.0f + 0.5f);
    return l ? l : 1;
}

/* ---- a sync record that outlived Talion's part in it ---------------------------------------------------------------- */

int somvr_fb_sync_live(somvr_fb_sync_age *a, uintptr_t rec, int have_atk, uintptr_t atk, double t, float stale_s, float max_s) {
    if (!rec) {
        a->rec = a->atk0 = 0, a->stale = 0, a->off_t = -1;
        return 0;
    }
    if (rec != a->rec) { /* a new record: its start */
        a->rec = rec, a->atk0 = have_atk ? atk : 0, a->t0 = t, a->off_t = -1, a->stale = 0;
        return 1;
    }
    if (have_atk && !a->atk0 && atk && t - a->t0 < 0.5) a->atk0 = atk; /* began before his attack showed */
    if (a->stale) {
        if (!(have_atk && atk && atk == a->atk0)) return 0;
        a->t0 = t, a->off_t = -1, a->stale = 0; /* the starting attack again: the same sync started anew */
        return 1;
    }
    if (have_atk && a->atk0) { /* (no starting attack known: the cap alone) */
        if (atk == a->atk0) a->off_t = -1;
        else if (a->off_t < 0) a->off_t = t;
        if (a->off_t >= 0 && t - a->off_t >= stale_s) a->stale = 1;
    }
    if (max_s > 0 && t - a->t0 >= max_s) a->stale = 1;
    if (a->stale) a->stales++;
    return !a->stale;
}
