/* somvr_session.c - SOMVR_MARK's chord and the session's text helpers (somvr_session.h). Pure: no globals, no game. */
#include "somvr_session.h"

#include <stdio.h>
#include <string.h>

void somvr_chord_cfg_default(somvr_chord_cfg *c) {
    c->window_s = 0.12f;
    c->join_s = 0.40f;
    c->hold_s = 0.50f;
    c->max_s = 5.0f;
}

void somvr_chord_reset(somvr_chord *c) { memset(c, 0, sizeof *c); }

void somvr_chord_tick(somvr_chord *c, const somvr_chord_cfg *cfg, const somvr_chord_in *in, double t,
                      somvr_chord_out *o) {
    memset(o, 0, sizeof *o);
    if (!c->have) { /* the counters' baseline: presses before the first tick never arrive */
        for (int b = 0; b < 2; b++) c->in_p[b] = c->out_p[b] = in->presses[b];
        c->have = 1;
    }
    int np[2], hd[2], dn[2];
    for (int b = 0; b < 2; b++) {
        np[b] = (uint8_t)(in->presses[b] - c->in_p[b]);
        c->in_p[b] = in->presses[b];
        if (!in->act[b]) np[b] = 0;
        hd[b] = in->act[b] && in->held[b];
        dn[b] = hd[b] || np[b] > 0; /* down now, or pressed (and let go) since the last tick */
    }
    if (c->chord) { /* both are ours until both are up */
        if (!c->ended) {
            float d = (float)(t - c->t_chord);
            if (hd[0] && hd[1]) {
                if (!c->armed && d >= cfg->hold_s && d <= cfg->max_s) c->armed = 1, o->ev = SOMVR_CHE_ARMED;
            } else {
                c->ended = 1, o->held_s = d;
                if (d >= cfg->hold_s && d <= cfg->max_s) o->ev = SOMVR_CHE_MARK, c->n_marks++;
                else if (d > cfg->max_s) o->ev = SOMVR_CHE_LONG, c->n_long++;
                else o->ev = SOMVR_CHE_SHORT, c->n_short++;
            }
        }
        if (c->ended && !hd[0] && !hd[1]) c->chord = 0, c->st[0] = c->st[1] = SOMVR_CHS_UP;
        o->presses[0] = c->out_p[0], o->presses[1] = c->out_p[1];
        return;
    }
    if (dn[0] && dn[1]) { /* both down: a chord, unless one of them has been the game's for a while */
        int ok = 1;
        for (int b = 0; b < 2; b++)
            if (c->st[b] == SOMVR_CHS_PASS && t - c->t_dn[b] > cfg->join_s) ok = 0;
        if (ok) {
            for (int b = 0; b < 2; b++) c->n_held += c->st[b] != SOMVR_CHS_PASS; /* (a passed one: the game sees it go up) */
            c->chord = 1, c->ended = 0, c->armed = 0, c->t_chord = t;
            if (!hd[0] || !hd[1]) { /* both pressed within one tick but one is up already: too short */
                c->ended = 1, o->ev = SOMVR_CHE_SHORT, c->n_short++;
                if (!hd[0] && !hd[1]) c->chord = 0, c->st[0] = c->st[1] = SOMVR_CHS_UP;
            }
            o->presses[0] = c->out_p[0], o->presses[1] = c->out_p[1];
            return;
        }
    }
    for (int b = 0; b < 2; b++) {
        if (c->st[b] == SOMVR_CHS_UP && dn[b]) c->st[b] = SOMVR_CHS_WAIT, c->t_dn[b] = t; /* held back for window_s */
        if (c->st[b] == SOMVR_CHS_WAIT) {
            if (t - c->t_dn[b] >= cfg->window_s) { /* no chord: the game gets the press now (a tap if it is up again) */
                c->out_p[b]++, c->n_passed++;
                c->st[b] = hd[b] ? SOMVR_CHS_PASS : SOMVR_CHS_UP;
                o->held[b] = hd[b];
            }
            continue;
        }
        if (c->st[b] == SOMVR_CHS_PASS) {
            c->out_p[b] = (uint8_t)(c->out_p[b] + np[b]); /* a release and press between two ticks */
            o->held[b] = hd[b];
            if (!hd[b]) c->st[b] = SOMVR_CHS_UP;
        }
    }
    o->presses[0] = c->out_p[0], o->presses[1] = c->out_p[1];
}

void somvr_mark_name(int h, int m, int s, uint32_t n, char *buf, size_t cap) {
    snprintf(buf, cap, "%02d%02d%02d_%u", h, m, s, n);
}

int somvr_mark_word(const char *w, char *note, size_t cap) {
    if (cap) note[0] = 0;
    if (strncmp(w, "mark", 4) || (w[4] && w[4] != ' ' && w[4] != '\t' && w[4] != '\n' && w[4] != '\r')) return 0;
    size_t u = 0;
    int blank = 1; /* no leading blanks */
    for (const char *p = w + 4; *p && cap && u + 1 < cap; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            if (!blank) note[u++] = ' ', blank = 1;
        } else if (ch >= 0x21 && ch <= 0x7e) {
            note[u++] = (char)ch, blank = 0;
        }
    }
    while (u && note[u - 1] == ' ') u--;
    if (cap) note[u] = 0;
    return 1;
}

int somvr_ss_path_ok(const char *p, size_t cap) {
    size_t n = p ? strlen(p) : 0;
    return n > 0 && n + 40 < cap && !strchr(p, '\n') && p[0] == '/';
}
