/* somvr_bodymoments.c - see somvr_bodymoments.h */
#include "somvr_bodymoments.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

#include "somvr_loco.h" /* somvr_loco_spring3: the same 3rd-order ease as the anchor's hand-offs */

const char *const somvr_bm_cond_names[SOMVR_BMC_N] = { "cine",  "scripted", "pres",  "sync",  "struggle", "mount",
                                                       "attach", "climb",   "death", "photo", "spect",    "tag",
                                                       "forced", "knockdown", "hold", "window" };
const char *const somvr_bm_act_names[SOMVR_BMA_N] = { "first", "hold", "dim", "grey", "window" };
const char *const somvr_bm_blink_names[4] = { "idle", "out", "black", "in" };
const char *const somvr_bm_yaw_names[3] = { "player", "moments", "camera" };

void somvr_bm_policy_default(somvr_bm_policy *p) {
    static const uint8_t d[SOMVR_BMC_N] = {
        [SOMVR_FB_CINE] = SOMVR_BMA_WINDOW,   [SOMVR_FB_SCRIPTED] = SOMVR_BMA_FIRST, [SOMVR_FB_PRES] = SOMVR_BMA_WINDOW,
        [SOMVR_FB_SYNC] = SOMVR_BMA_HOLD,     [SOMVR_FB_STRUGGLE] = SOMVR_BMA_HOLD,  [SOMVR_FB_MOUNT] = SOMVR_BMA_WINDOW,
        [SOMVR_FB_ATTACH] = SOMVR_BMA_WINDOW, [SOMVR_FB_CLIMB] = SOMVR_BMA_FIRST,    [SOMVR_FB_DEATH] = SOMVR_BMA_GREY,
        [SOMVR_FB_PHOTO] = SOMVR_BMA_WINDOW,  [SOMVR_FB_SPECT] = SOMVR_BMA_WINDOW,   [SOMVR_FB_TAG] = SOMVR_BMA_FIRST,
        [SOMVR_FB_FORCED] = SOMVR_BMA_WINDOW, [SOMVR_BMC_KNOCKDOWN] = SOMVR_BMA_DIM, [SOMVR_BMC_HOLD] = SOMVR_BMA_HOLD,
        [SOMVR_BMC_WINDOW] = SOMVR_BMA_WINDOW };
    memcpy(p->act, d, sizeof p->act);
}

/* the next comma-separated item of *p (blanks around it dropped) into out; 0 = no more items; *too_long when cut */
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

static int find_word(const char *w, size_t n, const char *const *words, int nw) {
    for (int i = 0; i < nw; i++)
        if (strlen(words[i]) == n && !strncmp(w, words[i], n)) return i;
    return -1;
}

int somvr_bm_policy_parse(const char *spec, somvr_bm_policy *p) {
    somvr_bm_policy_default(p);
    if (!spec || !spec[0] || !strcmp(spec, "default")) return 0;
    int rc = 0, tl;
    char it[48];
    while (next_item(&spec, it, sizeof it, &tl)) {
        const char *eq = strchr(it, '=');
        int c = eq && !tl ? find_word(it, (size_t)(eq - it), somvr_bm_cond_names, SOMVR_BMC_N) : -1;
        int a = c >= 0 ? find_word(eq + 1, strlen(eq + 1), somvr_bm_act_names, SOMVR_BMA_N) : -1;
        if (a < 0) {
            rc = -1;
            continue;
        }
        p->act[c] = (uint8_t)a;
    }
    return rc;
}

int somvr_bm_actions(const somvr_bm_policy *p, uint32_t conds, int *hold_cond, int *win, int *win_cond) {
    int best = SOMVR_BMA_FIRST, bc = -1, w = 0, wc = -1;
    for (int k = 0; k < SOMVR_BMC_N; k++) {
        if (!(conds >> k & 1)) continue;
        int a = p->act[k];
        if (a == SOMVR_BMA_WINDOW) {
            if (!w) w = 1, wc = k;
        } else if (a > best) {
            best = a, bc = k;
        }
    }
    if (hold_cond) *hold_cond = bc;
    if (win) *win = w;
    if (win_cond) *win_cond = wc;
    return best;
}

int somvr_bm_glob(const char *pat, const char *s) {
    const char *star = NULL, *ss = NULL;
    while (*s) {
        if (*pat == '*') {
            star = ++pat, ss = s;
            continue;
        }
        if (*pat && (*pat == '?' || tolower((unsigned char)*pat) == tolower((unsigned char)*s))) {
            pat++, s++;
            continue;
        }
        if (!star) return 0;
        pat = star, s = ++ss; /* the last star takes one more character */
    }
    while (*pat == '*') pat++;
    return !*pat;
}

static const char *const g_cls_words[5] = { "knockdown", "death", "hold", "window", "first" };
static const char *const g_src_words[5] = { "atk", "state", "tag", "sync", "any" };

static int names_add(const char *spec, somvr_bm_names *t) {
    int rc = 0, tl;
    char it[SOMVR_BM_PATLEN + 32];
    while (next_item(&spec, it, sizeof it, &tl)) {
        const char *eq = strchr(it, '=');
        int c = eq && !tl ? find_word(it, (size_t)(eq - it), g_cls_words, 5) : -1;
        const char *pat = eq ? eq + 1 : "", *col = strchr(pat, ':');
        uint32_t src = SOMVR_BMS_ALL;
        if (c >= 0 && col) {
            int s = find_word(pat, (size_t)(col - pat), g_src_words, 5);
            if (s < 0) c = -1;
            else src = s == 4 ? SOMVR_BMS_ALL : 1u << s;
            pat = col + 1;
        }
        size_t n = strlen(pat);
        if (c < 0 || n == 0 || n >= SOMVR_BM_PATLEN || t->n >= SOMVR_BM_MAXNAMES) {
            rc = -1;
            continue;
        }
        t->cls[t->n] = (uint8_t)(1u << c);
        t->src[t->n] = (uint8_t)src;
        memcpy(t->pat[t->n], pat, n + 1);
        t->n++;
    }
    return rc;
}

int somvr_bm_names_parse(const char *spec, somvr_bm_names *t) {
    memset(t, 0, sizeof *t);
    if (spec && !strcmp(spec, "none")) return 0;
    if (!spec || !spec[0] || !strcmp(spec, "default")) return names_add(SOMVR_BM_NAMES_DEFAULT, t);
    if (spec[0] == '+') {
        int rc = names_add(SOMVR_BM_NAMES_DEFAULT, t);
        return names_add(spec + 1, t) | rc;
    }
    return names_add(spec, t);
}

uint32_t somvr_bm_names_class(const somvr_bm_names *t, uint32_t src, const char *name) {
    uint32_t c = 0;
    if (!name || !name[0]) return 0;
    for (int i = 0; i < t->n; i++)
        if ((t->src[i] & src) && !(c & t->cls[i]) && somvr_bm_glob(t->pat[i], name)) c |= t->cls[i];
    return c;
}

uint32_t somvr_bm_conds(const somvr_bm_in *in) {
    uint32_t c = in->fb & ((1u << SOMVR_FB_NCOND) - 1u);
    if ((in->names & SOMVR_BMN_DEATH) || in->dead) c |= SOMVR_BMC_BIT(SOMVR_FB_DEATH);
    if (in->names & SOMVR_BMN_KNOCKDOWN) c |= SOMVR_BMC_BIT(SOMVR_BMC_KNOCKDOWN);
    if (in->names & SOMVR_BMN_HOLD) c |= SOMVR_BMC_BIT(SOMVR_BMC_HOLD);
    if (in->names & SOMVR_BMN_WINDOW) c |= SOMVR_BMC_BIT(SOMVR_BMC_WINDOW);
    if (in->names & SOMVR_BMN_FIRST) c &= ~SOMVR_BMC_BIT(SOMVR_FB_SYNC); /* a sync record that stays live */
    if (in->our_climb) c &= ~(SOMVR_BMC_BIT(SOMVR_FB_CLIMB) | SOMVR_BMC_BIT(SOMVR_FB_ATTACH));
    return c;
}

void somvr_bm_cfg_default(somvr_bm_cfg *c) {
    memset(c, 0, sizeof *c);
    c->hold_exit_s = 0.25f;
    c->win_in_s = 0.10f, c->win_out_s = 0.40f, c->win_hold_s = 1.0f;
    c->dim = 0.5f, c->grey = 0.8f, c->red = 1;
    c->dim_in_s = 0.15f, c->grey_in_s = 1.0f, c->dim_out_s = 0.35f;
    c->seat_cm = 40;
    c->ease_s = 0.05f;
    c->blink_out_s = 0.05f, c->blink_black_s = 0.02f, c->blink_in_s = 0.07f;
    c->gap_s = 0.5f;
}

void somvr_bm_init(somvr_bm *s) {
    memset(s, 0, sizeof *s);
    s->act_cond = s->win_cond = -1;
    s->low_since = s->win_cand = -1;
}

static float dist3f(const float *a, const float *b) {
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

void somvr_bm_step(somvr_bm *s, const somvr_bm_cfg *c, int hold_act, int hold_cond, int win, int win_cond, int paused,
                   double t, somvr_bm_out *o) {
    memset(o, 0, sizeof *o);
    double win_hold = c->win_hold_s < 1.0f ? 1.0 : (double)c->win_hold_s; /* never more than one change per second */
    if (hold_act < SOMVR_BMA_FIRST || hold_act > SOMVR_BMA_GREY) hold_act = SOMVR_BMA_FIRST;
    if (!s->started) s->started = 1, s->t_last = t, s->win_since = t - win_hold;
    double dt = t - s->t_last;
    int gap = !(dt <= c->gap_s); /* also NaN */
    if (!(dt > 0) || gap) dt = 0;
    if (dt > 0.1) dt = 0.1;
    s->t_last = t;
    s->frames++;
    int act0 = s->act, win0 = s->win, blink0 = s->blink;
    if (gap && s->frames > 1) { /* H4 did not run for a while (a cinematic, a load): no smoothing across */
        s->low_since = s->win_cand = -1;
        if (hold_act == SOMVR_BMA_FIRST && (s->frozen || s->ease || s->blink)) {
            s->act = SOMVR_BMA_FIRST, s->act_cond = -1;
            s->frozen = 0, s->ease = s->ease_pending = 0, s->blink = SOMVR_BMB_IDLE, s->blink_alpha = 0;
            s->cut_pending = 1, o->release = 3;
        }
    }
    if (!paused) {
        /* the hold-type action: a rise at once, a fall after hold_exit_s */
        if (hold_act > s->act) {
            s->act = hold_act, s->act_cond = hold_cond, s->low_since = -1;
        } else if (hold_act < s->act) {
            if (s->low_since < 0) s->low_since = t;
            if (t - s->low_since + 1e-6 >= c->hold_exit_s) s->act = hold_act, s->act_cond = hold_cond, s->low_since = -1;
        } else {
            s->low_since = -1, s->act_cond = hold_cond;
        }
        /* the window: debounced in and out, at least win_hold in each */
        if (!!win != s->win) {
            if (s->win_cand < 0) s->win_cand = t;
            double deb = win ? c->win_in_s : c->win_out_s;
            if (t - s->win_cand + 1e-6 >= deb && t - s->win_since + 1e-6 >= win_hold) {
                s->win = !!win, s->win_since = t, s->win_cand = -1;
                if (s->win) s->windows++, s->win_cond = win_cond;
                else s->cut_pending = 1; /* the first stereo frame after it: the live base (the layer fades it in) */
            }
        } else {
            s->win_cand = -1;
            if (win) s->win_cond = win_cond;
        }
    }
    /* the freeze */
    if (s->act >= SOMVR_BMA_HOLD) {
        if (!s->frozen) {
            s->frozen = 1, s->holds++;
            const float *f = s->have_out ? s->out : s->L; /* the view stops where it is */
            memcpy(s->F, f, sizeof s->F); /* a cut still pending (after the window) re-latches it at the live base */
            if (!s->have_out && !s->have_L) s->cut_pending = 1; /* no base seen yet: held where the first one is */
            s->ease = s->ease_pending = 0;
        }
        if (s->blink == SOMVR_BMB_OUT) /* held again before the black: back in from this level, still frozen */
            s->blink = SOMVR_BMB_IN, s->blink_t0 = c->blink_in_s > 0 ? t - (1.0 - s->blink_alpha) * c->blink_in_s : t;
        if (s->act > act0 && s->act == SOMVR_BMA_DIM) s->dims++;
        if (s->act > act0 && s->act == SOMVR_BMA_GREY) s->greys++;
    } else if (s->frozen && s->blink == SOMVR_BMB_IDLE) { /* the release: re-seat the view on the live base */
        float d = s->have_L ? dist3f(s->L, s->F) : 0;
        s->last_seat_cm = d;
        if (s->win) { /* rendered flat now: the next stereo frame comes back through the layer's fade */
            s->frozen = 0, s->cut_pending = 1, o->release = 3;
        } else if (c->seat_cm > 0 && d <= c->seat_cm && c->ease_s > 0) {
            s->frozen = 0, s->ease_pending = 1, s->eases++, o->release = 1;
        } else {
            s->blink = SOMVR_BMB_OUT, s->blink_t0 = t, s->blinks++, o->release = 2;
        }
    }
    /* the blink */
    for (int step = 0; step < 3; step++) {
        if (s->blink == SOMVR_BMB_OUT) {
            float a = c->blink_out_s > 0 ? (float)((t - s->blink_t0) / c->blink_out_s) : 1.0f;
            if (a < 1.0f) {
                s->blink_alpha = a > 0 ? a : 0;
                break;
            }
            s->blink_alpha = 1, s->blink = SOMVR_BMB_BLACK, s->blink_t0 = t, s->blink_frames = 1;
            if (s->frozen) s->frozen = 0, s->cut_pending = 1; /* the cut on this black frame */
            break;
        }
        if (s->blink == SOMVR_BMB_BLACK) {
            s->blink_alpha = 1;
            if (s->blink_frames >= 3 && t - s->blink_t0 + 1e-6 >= c->blink_black_s) {
                s->blink = SOMVR_BMB_IN, s->blink_t0 = t;
                continue;
            }
            s->blink_frames++;
            break;
        }
        if (s->blink == SOMVR_BMB_IN) {
            float a = c->blink_in_s > 0 ? 1.0f - (float)((t - s->blink_t0) / c->blink_in_s) : 0.0f;
            if (a > 0) {
                s->blink_alpha = a < 1 ? a : 1;
                break;
            }
            s->blink_alpha = 0, s->blink = SOMVR_BMB_IDLE;
        }
        break;
    }
    /* the dim / grey level */
    float target = s->act == SOMVR_BMA_DIM ? c->dim : s->act == SOMVR_BMA_GREY ? c->grey : 0;
    if (!(target >= 0)) target = 0;
    if (target > 1) target = 1;
    float in_s = s->act == SOMVR_BMA_GREY ? c->grey_in_s : c->dim_in_s, top = c->dim > c->grey ? c->dim : c->grey;
    float up = in_s > 0 ? target / in_s : 1e9f, down = c->dim_out_s > 0 ? (top > 0.01f ? top : 0.01f) / c->dim_out_s : 1e9f;
    if (target > 0) s->level_red = s->act == SOMVR_BMA_DIM; /* a decay keeps its colour */
    if (gap) s->level = target;
    else if (s->level < target) s->level = fminf(target, s->level + up * (float)dt);
    else if (s->level > target) s->level = fmaxf(target, s->level - down * (float)dt);
    if (!(s->level >= 0)) s->level = 0;
    o->act = s->act, o->act_cond = s->act_cond;
    o->window = s->win, o->win_cond = s->win_cond;
    o->frozen = s->frozen;
    o->fade = s->level > s->blink_alpha ? s->level : s->blink_alpha;
    o->body_anim = s->act == SOMVR_BMA_DIM || s->act == SOMVR_BMA_GREY;
    o->red = c->red && s->level_red && s->level > 0 && s->level >= s->blink_alpha;
    o->act_changed = s->act != act0, o->win_changed = s->win != win0, o->blink_changed = s->blink != blink0;
}

static float wrap_pi(float a) {
    if (!isfinite(a)) return 0;
    a = fmodf(a + 3.14159265f, 6.28318531f);
    if (a < 0) a += 6.28318531f;
    return a - 3.14159265f;
}

void somvr_bm_base(somvr_bm *s, const somvr_bm_cfg *c, const float live[3], float live_yaw, int32_t snaps, float snap_rad,
                   int yaw_mode, double t, float pos[3], float *yaw, int *cut) {
    int ok = isfinite(live[0]) && isfinite(live[1]) && isfinite(live[2]);
    double dt = s->have_L ? t - s->tL : 0;
    int gap = !(dt <= c->gap_s);
    *cut = 0;
    if (ok) {
        if (s->have_L && dt > 1e-4 && !gap)
            for (int k = 0; k < 3; k++) s->vL[k] = (float)((live[k] - s->L[k]) / dt);
        else
            s->vL[0] = s->vL[1] = s->vL[2] = 0;
        memcpy(s->L, live, sizeof s->L), s->have_L = 1, s->tL = t;
    }
    float dts = dt > 0 && !gap ? (float)(dt < 0.1 ? dt : 0.1) : 0;
    const float *base = ok ? live : s->have_out ? s->out : live;
    float out[3];
    if (s->cut_pending) {
        s->cut_pending = 0, s->ease = s->ease_pending = 0;
        memcpy(out, base, sizeof out);
        if (s->frozen) memcpy(s->F, out, sizeof s->F); /* held across the window: from here */
        *cut = s->have_out, s->cuts += (uint64_t)s->have_out;
    } else if (s->frozen) {
        memcpy(out, s->F, sizeof out);
    } else if (s->ease_pending && ok) { /* from the held point, the view's velocity 0 (continuous with the hold) */
        s->ease_pending = 0, s->ease = 1;
        for (int k = 0; k < 3; k++) s->e[k] = s->F[k] - live[k], s->ev[k] = -s->vL[k], s->ea[k] = 0;
        for (int k = 0; k < 3; k++) out[k] = live[k] + s->e[k];
    } else if (s->ease && ok) {
        float w = c->ease_s > 0 ? 1.0f / c->ease_s : 0;
        for (int k = 0; k < 3; k++) somvr_loco_spring3(&s->e[k], &s->ev[k], &s->ea[k], w, dts);
        float m = 0, mv = 0;
        for (int k = 0; k < 3; k++) {
            out[k] = live[k] + s->e[k];
            m = fmaxf(m, fabsf(s->e[k])), mv = fmaxf(mv, fabsf(s->ev[k]));
        }
        if (w <= 0 || (m < 0.05f && mv < 1.0f)) { /* settled (the last 0.5 mm go with it: below a frame's motion) */
            s->ease = 0;
            memcpy(out, live, sizeof out);
        }
    } else {
        memcpy(out, base, sizeof out);
    }
    memcpy(pos, out, sizeof out);
    memcpy(s->out, out, sizeof out), s->have_out = 1;
    *yaw = somvr_bm_yaw(s, live_yaw, snaps, snap_rad, yaw_mode);
}


void somvr_bm_recenter(somvr_bm *s) { s->yaw_off = 0; }

float somvr_bm_yaw(somvr_bm *s, float live_yaw, int32_t snaps, float snap_rad, int yaw_mode) {
    /* the rig's heading minus the player's own snap steps = the game camera's part; absorbed while the mode says so,
     * so the view never turns with the game camera (its combat, sync and kill cameras) */
    float y = isfinite(live_yaw) ? live_yaw : s->yaw_last;
    if (!s->have_yaw) s->have_yaw = 1, s->yaw_last = y, s->snaps_last = snaps, s->yaw_off = 0;
    float dcam = wrap_pi(wrap_pi(y - s->yaw_last) - (float)(snaps - s->snaps_last) * snap_rad);
    int absorb = yaw_mode == SOMVR_BMY_PLAYER ||
                 (yaw_mode == SOMVR_BMY_MOMENTS && (s->act != SOMVR_BMA_FIRST || s->frozen || s->ease || s->blink));
    if (absorb && isfinite(dcam)) s->yaw_off = wrap_pi(s->yaw_off - dcam), s->yaw_absorbed += fabsf(dcam);
    s->yaw_last = y, s->snaps_last = snaps;
    return yaw_mode == SOMVR_BMY_CAMERA ? live_yaw : wrap_pi(y + s->yaw_off);
}
