/* replay_input.c - b35 (docs/re-xr-pipeline.md 160.4): a SOMVR_INPUT_REC recording replayed offline into the logic's
 * pure parts (tests/inrec_replay.h), so a fix can be checked against the player's own motion with no game at all.
 * Not part of build.sh --check (a tool on a recording; test_inrec.c checks the library):
 *   gcc -O2 -I. -I../../third_party/OpenXR-SDK-1.1.61/include -o build/tests/replay_input tests/replay_input.c somvr_inrec.c somvr_combat.c somvr_slots.c \
 *       somvr_smooth.c somvr_hands.c somvr_eyes.c somvr_pose.c -lm
 *   build/tests/replay_input FILE info                 the header, the records, the span, the marks, the hits
 *   build/tests/replay_input FILE tiers [T0 T1]        each landed hit's tier again from the recorded blade (the
 *                                                     recording's own combat config: the same build gives the same)
 *   build/tests/replay_input FILE slots [T0 T1]        the sword's and the knife's draws the recorded hands ask for
 *   build/tests/replay_input FILE grip [T0 T1]         the right grip through build 31's weighted body (swp) and the
 *                                                     blade into the swing tracker: each stroke's tier and energy by
 *                                                     the config (a recording without BLADE records: a converted
 *                                                     SWORDREC, scripts/inrec.py from-swordrec)
 * Options after the mode: --cfg FROM.inrec (combat's config from another recording: the current build's rules on an
 * old session's motion), --along A (the contact's place on the blade for `grip`, default 0.7).
 * T0 T1: the game clock (somvr.log's t=; a mark's state.txt line 2). */
#include "../somvr_smooth.h"
#include "inrec_replay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const g_tn[SOMVR_IR_NTYPE] = { "?", "frames", "states", "blades", "hits", "marks", "cfgs", "notes", "replay" };

static void info(const irr_rec *r) {
    printf("%s v%u, %u-byte frames, the writer built %.16s, pid %d, started t=%.3f%s\n", r->hdr.magic, r->hdr.version,
           r->hdr.frame_size, r->hdr.build, r->hdr.pid, r->hdr.t0,
           r->hdr.flags & 1 ? " (a bug mark's copy)" : r->hdr.flags & 2 ? " (a cut)" : "");
    for (int k = 1; k < SOMVR_IR_NTYPE; k++)
        printf("  %-7s %d\n", g_tn[k], r->counts[k]);
    if (r->nf)
        printf("  frames t=%.3f..%.3f (%.1f s, %.1f a second), XrTime %lld..%lld\n", r->f[0].t, r->f[r->nf - 1].t,
               r->f[r->nf - 1].t - r->f[0].t, r->nf / fmax(1e-3, r->f[r->nf - 1].t - r->f[0].t),
               (long long)r->f[0].xr_time, (long long)r->f[r->nf - 1].xr_time);
    printf("  combat config: %s\n", r->has_cfg ? "recorded" : "none");
    somvr_ir_rh rh;
    for (size_t o = r->first; somvr_ir_next(r->buf, r->len, o, &rh); o += rh.size) {
        if (rh.type == SOMVR_IR_MARK && rh.size >= sizeof(somvr_ir_mark)) {
            somvr_ir_mark m;
            memcpy(&m, r->buf + o, sizeof m);
            printf("  mark #%u t=%.3f %.32s\n", m.n, m.t, m.name);
        } else if (rh.type == SOMVR_IR_HIT && rh.size >= sizeof(somvr_ir_hit)) {
            somvr_ir_hit h;
            memcpy(&h, r->buf + o, sizeof h);
            printf("  %s t=%.3f orc %08x %s %.1f J %.1f m/s along %.2f\n", h.kind ? "contact" : "hit", h.t, h.orc, sc_tier_name(h.tier), (double)h.blow_j,
                   (double)h.contact_mps, (double)h.along);
        } else if (rh.type == SOMVR_IR_NOTE && rh.size > sizeof(somvr_ir_note)) {
            somvr_ir_note n;
            memcpy(&n, r->buf + o, sizeof n);
            printf("  note t=%.3f %.*s\n", n.t, (int)(rh.size - sizeof n), (const char *)r->buf + o + sizeof n);
        }
    }
}

static void qrot(const float q[4], const float v[3], float o[3]) {
    float u[3] = { q[0], q[1], q[2] }, w = q[3];
    float c1[3] = { u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0] };
    float c2[3] = { u[1] * c1[2] - u[2] * c1[1], u[2] * c1[0] - u[0] * c1[2], u[0] * c1[1] - u[1] * c1[0] };
    for (int i = 0; i < 3; i++)
        o[i] = v[i] + 2 * w * c1[i] + 2 * c2[i];
}

/* the right grip's blade through swp (build 31's weighted body, heavy) into the tracker: each stroke's best tier */
static void grip(const irr_rec *r, const sc_cfg *c, double t0, double t1, float along) {
    swp_cfg pc;
    swp_defaults(&pc, SWG_HEAVY);
    swp_state ps;
    swp_reset(&ps);
    sc_speed sp;
    sc_speed_reset(&sp);
    somvr_blade prev;
    int have = 0, best = SC_TIER_NONE, open = 0, n[SC_TIER_N] = { 0 };
    float bj = 0, bv = 0;
    double tprev = 0, sb = 0;
    unsigned strokes0 = 0;
    printf("grip: the right grip through the weighted body (swp heavy) into the tracker (%s, contact at %.2f)\n",
           c->force ? "the force rule" : c->stroke ? "the stroke rule" : "build 16's rule", (double)along);
    for (int i = 0; i < r->nf; i++) {
        const somvr_ir_frame *f = &r->f[i];
        const somvr_ir_hand *g = &f->hand[(f->flags & SOMVR_IRF_LEFTY) ? 0 : 1];
        if ((t1 > t0 && (f->t < t0 || f->t > t1)) || !g->valid)
            continue;
        /* the grip in the game's convention (cm); the blade along the grip's +Z (game) as somvr_blade_controller */
        somvr_hand hd;
        somvr_ir_hand_get(g, f->xr_time, &hd, NULL);
        sv_vec3 p;
        sv_quat q;
        somvr_hand_game(&hd, &p, &q, NULL, NULL);
        float pm[3] = { p.x * 0.01f, p.y * 0.01f, p.z * 0.01f }, qq[4] = { q.x, q.y, q.z, q.w }, ax[3] = { 0, 0, 1 }, op[3], oq[4];
        float dt = have ? (float)(f->t - tprev) : 0;
        swp_step(&ps, &pc, f->t, pm, qq, ax, dt, 0, 0, NULL, op, oq);
        somvr_blade b;
        memset(&b, 0, sizeof b);
        float d[3], x[3] = { 1, 0, 0 }, nn[3];
        qrot(oq, ax, d), qrot(oq, x, nn);
        for (int k = 0; k < 3; k++)
            b.base[k] = op[k] * 100 - c->blow_back_cm * d[k], b.tip[k] = op[k] * 100 + c->blow_len_cm * d[k], b.flat_n[k] = nn[k];
        b.t = f->t, b.valid = 1;
        double bdt = have ? f->t - prev.t : 0;
        if (have && !(bdt < 0.1))
            sc_speed_reset(&sp);
        if (have && bdt > 0.002 && bdt < 0.1)
            for (int k = 0; k < 3; k++)
                b.vel_tip[k] = (float)((b.tip[k] - prev.tip[k]) / bdt), b.vel_base[k] = (float)((b.base[k] - prev.base[k]) / bdt);
        sc_speed_push(&sp, c, &b);
        prev = b, have = 1, tprev = f->t;
        float edge = sc_edge_deg(&b);
        int tier = irr_tier_of(c, &sp, edge, along);
        float j = sc_blow_j(c, &sp, along), v = sc_contact_mps(&sp, along);
        if (sp.strokes != strokes0) { /* a new stroke: the last one's verdict */
            if (open && (bj > 1 || best != SC_TIER_NONE))
                printf("  stroke t=%.3f: %-5s (its peak %.0f J, the contact %.1f m/s)\n", sb, sc_tier_name(best), (double)bj, (double)bv),
                    n[best]++;
            strokes0 = sp.strokes, open = 1, best = SC_TIER_NONE, bj = bv = 0, sb = f->t;
        }
        int rk = tier == SC_TIER_HEAVY ? 3 : tier == SC_TIER_LIGHT ? 2 : tier == SC_TIER_STAB ? 1 : 0,
            rb = best == SC_TIER_HEAVY ? 3 : best == SC_TIER_LIGHT ? 2 : best == SC_TIER_STAB ? 1 : 0;
        if (rk > rb)
            best = tier;
        if (j > bj)
            bj = j;
        if (v > bv)
            bv = v;
    }
    if (open && (bj > 1 || best != SC_TIER_NONE))
        printf("  stroke t=%.3f: %-5s (its peak %.0f J, the contact %.1f m/s)\n", sb, sc_tier_name(best), (double)bj, (double)bv),
            n[best]++;
    printf("grip: strokes heavy %d light %d stab %d none %d (the force rule: a hit from %.0f J, a cut from %.0f J)\n",
           n[SC_TIER_HEAVY], n[SC_TIER_LIGHT], n[SC_TIER_STAB], n[SC_TIER_NONE], (double)c->blow_min_j, (double)c->blow_heavy_j);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "replay_input FILE info|tiers|slots|grip [T0 T1] [--cfg FROM.inrec] [--along A]\n");
        return 2;
    }
    irr_rec r, rc;
    int e = irr_load_file(&r, argv[1]);
    if (e) {
        fprintf(stderr, "%s: not a recording of this build (%d)\n", argv[1], e);
        return 1;
    }
    double t0 = 0, t1 = 0;
    float along = 0.7f;
    const sc_cfg *cfg = NULL;
    int ai = 3;
    if (argc > 4 && argv[3][0] != '-')
        t0 = atof(argv[3]), t1 = atof(argv[4]), ai = 5;
    for (; ai < argc; ai++) {
        if (!strcmp(argv[ai], "--cfg") && ai + 1 < argc) {
            if (irr_load_file(&rc, argv[++ai]) || !rc.has_cfg) {
                fprintf(stderr, "%s: no combat config\n", argv[ai]);
                return 1;
            }
            cfg = &rc.cfg;
        } else if (!strcmp(argv[ai], "--along") && ai + 1 < argc) {
            along = (float)atof(argv[++ai]);
        }
    }
    const char *m = argv[2];
    if (!strcmp(m, "info")) {
        info(&r);
    } else if (!strcmp(m, "tiers")) {
        irr_tiers t;
        if (irr_replay_tiers(&r, cfg, t0, t1, &t, stdout)) {
            fprintf(stderr, "no combat config recorded (give --cfg)\n");
            return 1;
        }
        printf("tiers: %d hits, %d the same again, %d differ (the game: heavy %d light %d stab %d; again: heavy %d light %d stab "
               "%d none %d); %d blades (%d pushed), %d resets; the energy again within %.4f J\n",
               t.hits, t.same, t.differ, t.rec[SC_TIER_HEAVY], t.rec[SC_TIER_LIGHT], t.rec[SC_TIER_STAB], t.rep[SC_TIER_HEAVY],
               t.rep[SC_TIER_LIGHT], t.rep[SC_TIER_STAB], t.rep[SC_TIER_NONE], t.blades, t.pushed, t.resets, (double)t.max_dj);
        return t.differ && !cfg ? 3 : 0;
    } else if (!strcmp(m, "slots")) {
        somvr_slots_cfg sc;
        somvr_slots_cfg_default(&sc);
        irr_slots s;
        irr_replay_slots(&r, &sc, t0, t1, &s, stdout);
        printf("slots: %d frames; the sword drawn %d put back %d; the knife drawn %d put back %d\n", s.frames,
               s.draws[SOMVR_SLOT_SWORD], s.puts[SOMVR_SLOT_SWORD], s.draws[SOMVR_SLOT_DAGGER], s.puts[SOMVR_SLOT_DAGGER]);
    } else if (!strcmp(m, "grip")) {
        sc_cfg dflt;
        sc_cfg_default(&dflt);
        dflt.force = 1, dflt.stroke = 1, dflt.tip_max_mps = 60;
        grip(&r, cfg ? cfg : r.has_cfg ? &r.cfg : &dflt, t0, t1, along);
    } else {
        fprintf(stderr, "unknown mode %s\n", m);
        return 2;
    }
    return 0;
}
