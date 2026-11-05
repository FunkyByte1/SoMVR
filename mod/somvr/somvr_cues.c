/* somvr_cues.c - build 37 (som-b37-cues): the pure part of the cues for what the hidden prompts said (somvr_cues.h) */
#include "somvr_cues.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void somvr_cu_aw_defaults(somvr_cu_aw_cfg *c) {
    /* (boot 20261104-205033: a calm orc facing Talion at 10.6 m went 0 -> 1, 1.6 s later 2 with its
     * 'ReactToSusp_FromBase_Unsheathe' (it looks), then 4 as it came and fought - 4 the whole fight; its attack target is
     * Talion only during each blow) */
    static const int8_t def[SOMVR_CU_KINDS] = {SOMVR_CU_NONE, SOMVR_CU_WHITE, SOMVR_CU_AMBER, SOMVR_CU_RED,
                                               SOMVR_CU_RED, SOMVR_CU_NONE, SOMVR_CU_NONE, SOMVR_CU_NONE};
    memcpy(c->lvl, def, sizeof def);
    c->in_s = 0.25f, c->out_s = 0.5f, c->red_s = 3.0f;
    c->deg = 1.6f, c->min_m = 0.12f, c->max_m = 0.6f;
}

int somvr_cu_aw_parse_kinds(const char *v, somvr_cu_aw_cfg *c) {
    int8_t l[SOMVR_CU_KINDS] = {0};
    int n = 0;
    const char *p = v;
    if (!v || !*v) return 0;
    while (*p) {
        if (*p < '0' || *p > '3' || n == SOMVR_CU_KINDS) return 0;
        l[n++] = (int8_t)(*p++ - '0');
        if (*p == ',') {
            p++;
            if (!*p) return 0;
        } else if (*p) {
            return 0;
        }
    }
    memcpy(c->lvl, l, sizeof l);
    return 1;
}

int somvr_cu_aw_level(const somvr_cu_aw_cfg *c, int state, int engaged, int dead) {
    if (dead) return SOMVR_CU_NONE;
    if (engaged) return SOMVR_CU_RED;
    if (state < 0 || state >= SOMVR_CU_KINDS) return SOMVR_CU_NONE;
    return c->lvl[state];
}

int somvr_cu_aw_step(const somvr_cu_aw_cfg *c, somvr_cu_aw_mark *m, int lvl, int visible, double t, float dt) {
    int changed = lvl != m->lvl;
    if (changed) {
        if (lvl == SOMVR_CU_RED && m->lvl != SOMVR_CU_RED) m->red_at = t;
        m->lvl = lvl, m->since = t;
    }
    int want = lvl > SOMVR_CU_NONE && visible;
    if (want && lvl == SOMVR_CU_RED && c->red_s > 0 && t - m->red_at > c->red_s) want = 0; /* red's auto-hide */
    if (want) m->shown = lvl; /* (fading out it keeps the colour it had) */
    float k = !(dt > 0) ? 0 : want ? (c->in_s > 0 ? dt / c->in_s : 1) : (c->out_s > 0 ? -dt / c->out_s : -1);
    m->fade += k;
    if (m->fade > 1) m->fade = 1;
    if (!(m->fade > 0)) m->fade = 0, m->shown = want ? m->shown : SOMVR_CU_NONE;
    return changed;
}

float somvr_cu_aw_size(const somvr_cu_aw_cfg *c, float dist_m) {
    float s = 2.0f * dist_m * tanf(0.5f * c->deg * 0.017453293f);
    return !(s > c->min_m) ? c->min_m : s > c->max_m ? c->max_m : s;
}

void somvr_cu_tm_defaults(somvr_cu_tm_cfg *c) {
    c->start_s = 4, c->mark_s = 4, c->at[0] = 30, c->at[1] = 10;
}

int somvr_cu_tm_tick(const somvr_cu_tm_cfg *c, somvr_cu_tm *s, float left, double t) {
    int ev = SOMVR_CU_TM_NONE;
    if (!(left >= 0)) return ev;
    if (!s->on || left > s->left + 1.5f) { /* a new timer (or one set again, longer) */
        s->on = 1, s->marks = 0, s->last = 0, s->pop_until = t + c->start_s, ev = SOMVR_CU_TM_START;
        for (int i = 0; i < 2; i++) /* marks already passed at its start do not pop again */
            if (left <= c->at[i]) s->marks |= 1 << i;
        if (left <= c->at[1]) s->last = 1;
    }
    for (int i = 0; i < 2; i++)
        if (!(s->marks & 1 << i) && left <= c->at[i]) {
            s->marks |= 1 << i;
            if (i == 1) s->last = 1, ev = SOMVR_CU_TM_LAST;
            else s->pop_until = t + c->mark_s, ev = SOMVR_CU_TM_MARK;
        }
    s->left = left, s->seen_t = t;
    return ev;
}

int somvr_cu_tm_frame(somvr_cu_tm *s, double t, double stale_s, int *pop) {
    int ev = SOMVR_CU_TM_NONE;
    if (s->on && t - s->seen_t > stale_s) s->on = 0, s->last = 0, s->pop_until = 0, ev = SOMVR_CU_TM_STOP;
    *pop = s->on && (s->last || t < s->pop_until);
    return ev;
}

float somvr_cu_reach_level(float d_cm, float reach_cm, int in) {
    if (in) return 1.0f;
    float p = 1.0f - (d_cm - reach_cm) / 80.0f;
    p = !(p > 0) ? 0 : p > 1 ? 1 : p;
    return 0.3f + 0.5f * p;
}

int somvr_cu_ring_cell(float d_cm, float reach_cm, int in) {
    if (in) return 16;
    float p = 1.0f - (d_cm - reach_cm) / 80.0f;
    p = !(p > 0) ? 0 : p > 1 ? 1 : p;
    return 1 + (int)lrintf(14.0f * p);
}

int somvr_cu_match(const char *pats, const char *name) {
    if (!pats || !name || !*name) return 0;
    const char *p = pats;
    while (*p) {
        const char *e = strchr(p, ',');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n && p[n - 1] == '*') {
            if (!strncmp(name, p, n - 1)) return 1;
        } else if (n && strlen(name) == n && !strncmp(name, p, n)) {
            return 1;
        }
        if (!e) break;
        p = e + 1;
    }
    return 0;
}

const char *somvr_cu_use_recs(const char *prompt) {
    static const struct { const char *prompt, *recs; } T[] = {
        /* artifacts, Ithildin, Torvin's journals, a captain's rune (on his body: no marker known - the hand's reach) */
        {"Combat_Activate_Collectable", "Collectible_Artifact,Collectible_Glyph,Collectible_Intel,DLC1_Collectible_*,Rune*,"
                                        "Collectible_Rune*"},
        {"Combat_Activate_SpiritStone", "RessurrectionStone,FT_*"}, /* a forge tower's stone ("(B) Reforge"): an unsynced tower's marker */
        {"Combat_Activate_Ammo", "Ammo"},        /* an arrow bundle */
        {"Combat_Activate_Flower", "Collectible_Flower*"},
    };
    if (!prompt) return "";
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++)
        if (!strcmp(prompt, T[i].prompt)) return T[i].recs;
    /* the rest (grog to poison, a cage, a slave's bonds, ...): the camp's own things, the nearest within reach */
    return "Explosive,LockedGate,HangingBait,RessurrectionStone,Collectible_*";
}
