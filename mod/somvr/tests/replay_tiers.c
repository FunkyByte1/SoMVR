/* replay_tiers.c - build 31b (docs/re-xr-pipeline.md 134.7): the hit tiers on the user's own recorded swings
 * (SOMVR_SWORD_REC's <log>.swordrec) through three blades - the hand's (1:1), build 16-30's heavy trail (swg) and build
 * 31's weighted body (swp) - each through combat's own speed tracker and SOMVR_COMBAT_SWING=stroke's tier
 * (somvr_combat.c sc_speed_push / sc_tier_swing) as a contact at `along` would get it, frame by frame. Segmented by the
 * hand's strokes; each stroke classed by the hand: committed (the fist travels CMT_CM or more: the arm swings), a flick
 * (the fist under FLICK_CM while the hand's own blade reaches the cut speed: the wrist alone), other.
 * Not part of build.sh --check (an offline tool on a recording):
 *   gcc -O2 -o build/tests/replay_tiers tests/replay_tiers.c somvr_combat.c somvr_smooth.c -lm
 *   build/tests/replay_tiers FILE [CUT_MPS [ALONG [MODE]]]   MODE: blade (the drawn blade's contact speed, build 30) or
 *                                                             hand (the hand's own swing's, the tier's speed only) */
#include "../somvr_combat.h"
#include "../somvr_smooth.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CMT_CM 30.0f
#define FLICK_CM 12.0f
#define DEG 0.01745329f

static void qrot(const float q[4], const float v[3], float o[3]) {
    float u[3] = {q[0], q[1], q[2]}, w = q[3];
    float c1[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
    float c2[3] = {u[1] * c1[2] - u[2] * c1[1], u[2] * c1[0] - u[0] * c1[2], u[0] * c1[1] - u[1] * c1[0]};
    for (int i = 0; i < 3; i++) o[i] = v[i] + 2 * w * c1[i] + 2 * c2[i];
}
static void qmul(const float a[4], const float b[4], float o[4]) {
    float r[4] = {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
                  a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
    memcpy(o, r, sizeof r);
}

static int cmpf(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}
enum { B_HAND, B_OLD, B_NEW, B_N };
static const char *const g_bn[B_N] = {"the hand 1:1", "build 30 heavy (swg)", "build 31 body (swp)"};
typedef struct {
    sc_speed sp;
    somvr_blade prev;
    int have;
} trk;

/* the blade from the grip (cm) and the sword's orientation; velocities by finite difference (the game's bone blade) */
static void blade_of(const float p[3], const float q[4], const float ax[3], double t, const somvr_blade *prev,
                     somvr_blade *b) {
    memset(b, 0, sizeof *b);
    float d[3], n[3], x[3] = {1, 0, 0};
    qrot(q, ax, d), qrot(q, x, n);
    for (int i = 0; i < 3; i++) b->base[i] = p[i] - 8.0f * d[i], b->tip[i] = p[i] + 100.0f * d[i], b->flat_n[i] = n[i];
    b->t = t, b->valid = 1;
    double dt = prev && prev->valid ? t - prev->t : 0;
    if (dt > 0.002 && dt < 0.1)
        for (int i = 0; i < 3; i++)
            b->vel_tip[i] = (float)((b->tip[i] - prev->tip[i]) / dt), b->vel_base[i] = (float)((b->base[i] - prev->base[i]) / dt);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "replay_tiers FILE [CUT_MPS [ALONG [hand|blade]]]\n"); return 2; }
    float cut = argc > 2 ? (float)atof(argv[2]) : 12.0f, along = argc > 3 ? (float)atof(argv[3]) : 0.7f;
    int handmode = argc > 4 && !strcmp(argv[4], "hand");
    float arm = argc > 5 ? (float)atof(argv[5]) : 0;
    FILE *f = fopen(argv[1], "r");
    if (!f) { perror(argv[1]); return 1; }
    sc_cfg c;
    sc_cfg_default(&c);
    c.stroke = 1, c.cut_mps = cut, c.tip_max_mps = 30.0f, c.arm_cm = arm; /* (CB_SWING_JUMP_MPS's order) */
    swg_cfg gc;
    swg_defaults(&gc, SWG_HEAVY);
    swg_state gs;
    swg_reset(&gs);
    swp_cfg pc;
    swp_defaults(&pc, SWG_HEAVY);
    swp_state ps;
    swp_reset(&ps);
    trk tk[B_N];
    memset(tk, 0, sizeof tk);
    /* per hand stroke: the best tier of each blade inside it (and 150 ms after), the peak contact speed, the fist's travel */
    long strokes[3][B_N][SC_TIER_N], nst[3] = {0, 0, 0};
    memset(strokes, 0, sizeof strokes);
    double vsum[3][B_N] = {{0}};
    int cur_best[B_N], open = 0;
    float cur_v[B_N], fist_cm = 0, hand_v = 0, pp[3] = {0, 0, 0};
    double end_t = 0;
    unsigned hand_strokes0 = 0;
    char line[512];
    double tprev = 0;
    float qprev[4] = {0, 0, 0, 1};
    long frames = 0;
    static float fr[2][4096], fv[2][4096];
    int nfr[2] = {0, 0};
    float br_max = 0, bv_max = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        double t;
        float q[4], a, off, p[3];
        int two, lv;
        if (sscanf(line, "%lf %f %f %f %f %d %d %f %f %f %f %f", &t, &q[0], &q[1], &q[2], &q[3], &two, &lv, &a, &off, &p[0], &p[1],
                   &p[2]) != 12) continue;
        float ax[3] = {0, sinf(a * DEG), cosf(a * DEG)}, dt = frames ? (float)(t - tprev) : 0;
        /* the three swords' orientations (and the body's place) this frame */
        float qs[B_N][4], ps_[B_N][3];
        memcpy(qs[B_HAND], q, 16), memcpy(ps_[B_HAND], p, 12);
        float rot[3], e[4];
        if (!swg_step(&gs, &gc, q, ax, dt > 0 ? dt : 0.0111f, two, rot)) memset(rot, 0, 12);
        swg_qexp(rot, e), qmul(e, q, qs[B_OLD]), memcpy(ps_[B_OLD], p, 12);
        float pm[3] = {p[0] * 0.01f, p[1] * 0.01f, p[2] * 0.01f}, op[3], oq[4];
        swp_step(&ps, &pc, t, pm, q, ax, dt, two, 0, NULL, op, oq);
        memcpy(qs[B_NEW], oq, 16);
        for (int i = 0; i < 3; i++) ps_[B_NEW][i] = op[i] * 100.0f;
        int tier[B_N];
        float cv[B_N];
        for (int k = 0; k < B_N; k++) {
            somvr_blade b;
            blade_of(ps_[k], qs[k], ax, t, tk[k].have ? &tk[k].prev : NULL, &b);
            if (tk[k].have && !(t - tk[k].prev.t < 0.1)) sc_speed_reset(&tk[k].sp);
            sc_speed_push(&tk[k].sp, &c, &b);
            tk[k].prev = b, tk[k].have = 1;
            float edge = sc_edge_deg(&b);
            /* hand mode: the tier from the hand's tracker (speed, reach), the edge the drawn blade's */
            const sc_speed *s = handmode && k != B_HAND ? &tk[B_HAND].sp : &tk[k].sp;
            tier[k] = sc_tier_swing(&c, s, edge, along);
            cv[k] = sc_contact_mps(s, along);
        }
        (void)qprev;
        /* the hand's strokes: a new one each time the hand's tracker starts one */
        unsigned hs = tk[B_HAND].sp.strokes;
        if (hs != hand_strokes0 || (open && t > end_t + 0.6)) {
            if (open) { /* close the last: class it */
                int cl = fist_cm >= CMT_CM ? 0 : (fist_cm < FLICK_CM && hand_v >= 12.0f) ? 1 : 2;
                if (cl < 2 && nfr[cl] < 4096) fr[cl][nfr[cl]] = br_max, fv[cl][nfr[cl]++] = bv_max;
                nst[cl]++;
                for (int k = 0; k < B_N; k++) strokes[cl][k][cur_best[k]]++, vsum[cl][k] += cur_v[k];
            }
            open = hs != hand_strokes0;
            hand_strokes0 = hs, fist_cm = 0, hand_v = 0, br_max = bv_max = 0, memcpy(pp, p, 12);
            for (int k = 0; k < B_N; k++) cur_best[k] = SC_TIER_NONE, cur_v[k] = 0;
        }
        if (open) {
            float d[3] = {p[0] - pp[0], p[1] - pp[1], p[2] - pp[2]};
            fist_cm += sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            memcpy(pp, p, 12);
            if (tk[B_HAND].sp.mean_mps >= c.slash_mps) end_t = t;
            if (cv[B_HAND] > hand_v) hand_v = cv[B_HAND];
            if (tk[B_NEW].sp.base_reach > br_max) br_max = tk[B_NEW].sp.base_reach;
            if (tk[B_NEW].sp.base_mps > bv_max) bv_max = tk[B_NEW].sp.base_mps;
            for (int k = 0; k < B_N; k++) {
                int r = tier[k] == SC_TIER_HEAVY ? 3 : tier[k] == SC_TIER_LIGHT ? 2 : tier[k] == SC_TIER_STAB ? 1 : 0;
                int rb = cur_best[k] == SC_TIER_HEAVY ? 3 : cur_best[k] == SC_TIER_LIGHT ? 2 : cur_best[k] == SC_TIER_STAB ? 1 : 0;
                if (r > rb) cur_best[k] = tier[k];
                if (tk[k].sp.above && cv[k] > cur_v[k]) cur_v[k] = cv[k];
            }
        }
        tprev = t, memcpy(qprev, q, 16), frames++;
    }
    fclose(f);
    static const char *const cn[3] = {"committed (the fist 30+ cm)", "wrist flick (the fist <12 cm, the hand at the cut speed)",
                                      "other"};
    printf("replay_tiers %s: %ld frames; cut %.1f m/s at along %.2f, the fist %.0f cm; tier by the %s\n", argv[1], frames,
           (double)cut, (double)along, (double)arm, handmode ? "hand's swing" : "drawn blade");
    for (int cl = 0; cl < 3; cl++) {
        printf("  %s: %ld strokes\n", cn[cl], nst[cl]);
        for (int k = 0; k < B_N; k++) {
            long *s = strokes[cl][k];
            printf("    %-22s heavy %5ld  light %5ld  stab %4ld  none %5ld   (the contact's peak mean %.1f m/s)\n", g_bn[k],
                   s[SC_TIER_HEAVY], s[SC_TIER_LIGHT], s[SC_TIER_STAB], s[SC_TIER_NONE], nst[cl] ? vsum[cl][k] / nst[cl] : 0.0);
        }
    }
    for (int cl = 0; cl < 2; cl++) {
        qsort(fr[cl], (size_t)nfr[cl], sizeof(float), cmpf), qsort(fv[cl], (size_t)nfr[cl], sizeof(float), cmpf);
        int n = nfr[cl];
        printf("  %s: build 31's fist (the blade's base) in the stroke - its reach p10 %.0f p25 %.0f p50 %.0f p75 %.0f cm; its "
               "peak speed p10 %.1f p25 %.1f p50 %.1f p75 %.1f m/s\n", cl ? "flicks" : "committed", n ? fr[cl][n / 10] : 0,
               n ? fr[cl][n / 4] : 0, n ? fr[cl][n / 2] : 0, n ? fr[cl][n * 3 / 4] : 0, n ? fv[cl][n / 10] : 0, n ? fv[cl][n / 4] : 0,
               n ? fv[cl][n / 2] : 0, n ? fv[cl][n * 3 / 4] : 0);
    }
    return 0;
}
