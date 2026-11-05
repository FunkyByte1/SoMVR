/* replay_input.c - b35 (docs/re-xr-pipeline.md 160.4): a SOMVR_INPUT_REC recording replayed offline into the logic's
 * pure parts (tests/inrec_replay.h), so a fix can be checked against the player's own motion with no game at all.
 * Not part of build.sh --check (a tool on a recording; test_inrec.c checks the library):
 *   gcc -O2 -I. -I../../third_party/OpenXR-SDK-1.1.61/include -o build/tests/replay_input tests/replay_input.c somvr_inrec.c somvr_combat.c somvr_slots.c somvr_bow.c \
 *       somvr_smooth.c somvr_hands.c somvr_eyes.c somvr_pose.c -lm
 *   build/tests/replay_input FILE info                 the header, the records, the span, the marks, the hits
 *   build/tests/replay_input FILE tiers [T0 T1]        each landed hit's tier again from the recorded blade (the
 *                                                     recording's own combat config: the same build gives the same)
 *   build/tests/replay_input FILE slots [T0 T1]        the sword's and the knife's draws the recorded hands ask for
 *   build/tests/replay_input FILE bow [T0 T1]          build 37 (re-xr-pipeline.md 174; link somvr_bow.c too): every
 *                                                     drawn frame of the left-hand bow (the string hand 20 cm back
 *                                                     or more) as builds 16-36 show it and as SOMVR_BOW_FIT does: the
 *                                                     shown bow's cant, its turn onto the draw, where the arrow passes
 *                                                     the riser (the grip's X across the riser's depth) and its angle
 *                                                     off the bow's shot; per cant band and per draw
 *   build/tests/replay_input FILE hangbow [T0 T1]      build 40 (re-xr-pipeline.md 175.1): the bow hand against its
 *                                                     shoulder slot through SOMVR_BOW_HANG_REACH's rule - the frames
 *                                                     and spans it would keep a hands-free hang (and why)
 *   build/tests/replay_input FILE grip [T0 T1]         the right grip through build 31's weighted body (swp) and the
 *                                                     blade into the swing tracker: each stroke's tier and energy by
 *                                                     the config (a recording without BLADE records: a converted
 *                                                     SWORDREC, scripts/inrec.py from-swordrec)
 * Options after the mode: --cfg FROM.inrec (combat's config from another recording: the current build's rules on an
 * old session's motion), --along A (the contact's place on the blade for `grip`, default 0.7).
 * T0 T1: the game clock (somvr.log's t=; a mark's state.txt line 2). */
#include "../somvr_bow.h"
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

/* build 37: the bow's arrow against its riser in the player's own draws (see the header's `bow`). The grip frame: X across
 * the bow (the arrow's side), Y back toward the archer (the shot along -Y), Z up the limbs; the riser's depth taken as
 * Y -5..+3 cm (the grip node at 0), its middle the limbs' line X 0.1 (the tip nodes, boot 015515) */
#define BW_X0 0.1f
typedef struct { int n, cross, near1; float tilt_max, tilt_sum, turn_sum, xmin_min, off_sum, off_max; } bw_acc;
static void bw_add(bw_acc *a, float tilt, float xmin, float turn, float off) {
    a->n++, a->tilt_sum += tilt, a->turn_sum += turn, a->off_sum += off;
    a->off_max = off > a->off_max ? off : a->off_max;
    a->tilt_max = tilt > a->tilt_max ? tilt : a->tilt_max;
    if (a->n == 1 || xmin < a->xmin_min) a->xmin_min = xmin;
    a->cross += xmin <= 0.0f, a->near1 += xmin < 1.0f;
}
/* the arrow from string through the shown bow (p, qs)'s nock: its angle off the shot (deg) and the least distance (cm)
 * from the riser's middle toward its own side across the riser's depth (<= 0: through the riser's middle) */
static float bw_ang(sv_vec3 a, sv_vec3 b) {
    float c = (a.x * b.x + a.y * b.y + a.z * b.z) / sqrtf((a.x * a.x + a.y * a.y + a.z * a.z) * (b.x * b.x + b.y * b.y + b.z * b.z));
    return acosf(c > 1 ? 1 : c < -1 ? -1 : c) * 57.29578f;
}
static void bw_pass(const somvr_bow_cfg *c, sv_vec3 p, sv_quat qs, sv_vec3 string, float *tilt, float *xmin, sv_vec3 *dir) {
    sv_vec3 n = somvr_bow_nock(c, p, qs), d = somvr_bow_ray_dir(c, qs, n, string, NULL);
    *dir = d;
    sv_quat iq = sv_qconj(qs);
    sv_vec3 dl = sv_qrot(iq, d), nl = sv_qrot(iq, (sv_vec3){ n.x - p.x, n.y - p.y, n.z - p.z });
    float sh = -dl.y; /* along the shot (-Y) */
    *tilt = acosf(sh > 1 ? 1 : sh < -1 ? -1 : sh) * 57.29578f;
    float sd = nl.x - BW_X0 < 0 ? -1.0f : 1.0f, m = 1e9f;
    for (float y = -5.0f; y <= 3.01f; y += 0.5f) {
        float x = fabsf(dl.y) > 1e-4f ? nl.x + (y - nl.y) * dl.x / dl.y : nl.x;
        float v = sd * (x - BW_X0);
        m = v < m ? v : m;
    }
    *xmin = m;
}
/* the XR pose p (m), q relative to the head (eye midpoint, its levelled yaw) as the layer's test word reads it (somvr_menus.c
 * somvr_xt_head_pose: cm right / up / ahead, then R = Rx(a) Ry(b) Rz(c) after the head's yaw, deg) */
static void bw_word(const float hp[3], const float hq[4], const float p[3], const float q[4], float out[6]) {
    float f[3], fw[3] = { 0, 0, -1 };
    qrot(hq, fw, f);
    float n = sqrtf(f[0] * f[0] + f[2] * f[2]), hx = f[0] / n, hz = f[2] / n;
    float d[3] = { 100 * (p[0] - hp[0]), 100 * (p[1] - hp[1]), 100 * (p[2] - hp[2]) };
    out[0] = -hz * d[0] + hx * d[2], out[1] = d[1], out[2] = hx * d[0] + hz * d[2];
    float a = atan2f(-hx, -hz), ya[4] = { 0, -sinf(0.5f * a), 0, cosf(0.5f * a) }; /* the yaw's inverse */
    float l[4] = { ya[3] * q[0] + ya[0] * q[3] + ya[1] * q[2] - ya[2] * q[1], ya[3] * q[1] - ya[0] * q[2] + ya[1] * q[3] + ya[2] * q[0],
                   ya[3] * q[2] + ya[0] * q[1] - ya[1] * q[0] + ya[2] * q[3], ya[3] * q[3] - ya[0] * q[0] - ya[1] * q[1] - ya[2] * q[2] };
    float x = l[0], y = l[1], z = l[2], w = l[3];
    float r00 = 1 - 2 * (y * y + z * z), r01 = 2 * (x * y - z * w), r02 = 2 * (x * z + y * w);
    float r12 = 2 * (y * z - x * w), r22 = 1 - 2 * (x * x + y * y);
    out[4] = asinf(r02 > 1 ? 1 : r02 < -1 ? -1 : r02) * 57.29578f;
    out[3] = atan2f(-r12, r22) * 57.29578f, out[5] = atan2f(-r01, r00) * 57.29578f;
}
static int g_bw_poses, g_eye_n, g_eye_agree, g_eye_far, g_eye_agree_far;
static void bow(const irr_rec *r, double t0, double t1) {
    somvr_bow_cfg c;
    somvr_bow_cfg_default(&c);
    c.bow_hand = SOMVR_HAND_L, c.shot_ax[0] = 0, c.shot_ax[1] = -1, c.shot_ax[2] = 0;
    c.limb_ax[0] = 0, c.limb_ax[1] = 0, c.limb_ax[2] = 1;
    c.nock_ofs[0] = -1.1f, c.nock_ofs[1] = 1.7f, c.nock_ofs[2] = 4.0f; /* the session's measured nock (BOW nock) */
    c.side_auto = 1;
    somvr_bow_qf fo = { 0 }, fn = { 0 };
    enum { NB = 12 };
    bw_acc ob[NB], nb[NB], ot = { 0 }, nt = { 0 };
    memset(ob, 0, sizeof ob), memset(nb, 0, sizeof nb);
    int draws = 0, in_draw = 0, side = 0, flips = 0, d_frames = 0, d_cross = 0, side_n[2] = { 0, 0 };
    double d_t0 = 0;
    float d_cant_lo = 0, d_cant_hi = 0, d_turn = 0;
    /* the bow out: the STATE records' flag (4 Hz) */
    int ns = 0, cap = 0, si = 0;
    double *st = NULL;
    uint32_t *sf = NULL;
    somvr_ir_rh rh;
    for (size_t o = r->first; somvr_ir_next(r->buf, r->len, o, &rh); o += rh.size)
        if (rh.type == SOMVR_IR_STATE && rh.size >= sizeof(somvr_ir_state)) {
            somvr_ir_state x;
            memcpy(&x, r->buf + o, sizeof x);
            if (ns == cap) {
                cap = cap ? 2 * cap : 1024;
                st = realloc(st, (size_t)cap * sizeof *st), sf = realloc(sf, (size_t)cap * sizeof *sf);
                if (!st || !sf)
                    return;
            }
            st[ns] = x.t, sf[ns++] = x.flags;
        }
    for (int i = 0; i < r->nf; i++) {
        const somvr_ir_frame *f = &r->f[i];
        while (si + 1 < ns && st[si + 1] <= f->t)
            si++;
        int bow_out = ns && st[si] <= f->t && (sf[si] & SOMVR_IRS_BOW);
        if ((t1 > t0 && (f->t < t0 || f->t > t1)) || !f->hand[0].valid || !f->hand[1].valid)
            continue;
        somvr_hand hb, hs;
        somvr_ir_hand_get(&f->hand[0], f->xr_time, &hb, NULL);
        somvr_ir_hand_get(&f->hand[1], f->xr_time, &hs, NULL);
        sv_vec3 p, ps;
        sv_quat q, qx;
        somvr_hand_game(&hb, &p, &q, NULL, NULL);
        somvr_hand_game(&hs, &ps, &qx, NULL, NULL);
        c.fit = 0;
        sv_vec3 n0 = somvr_bow_nock(&c, p, q);
        float back = somvr_bow_pull(&c, q, n0, ps), dist = sqrtf((n0.x - ps.x) * (n0.x - ps.x) + (n0.y - ps.y) * (n0.y - ps.y) +
                                                                  (n0.z - ps.z) * (n0.z - ps.z));
        int drawn = bow_out && back >= 20.0f && dist < 110.0f;
        if (!drawn) {
            if (in_draw && d_frames >= 9)
                printf("  draw %d t=%.2f..%.2f: %d frames, cant %.0f..%.0f deg, the turn onto the draw up to %.0f deg, the "
                       "arrow through the riser's middle in %d (builds 16-36)\n", draws, d_t0, r->f[i - 1].t, d_frames,
                       (double)d_cant_lo, (double)d_cant_hi, (double)d_turn, d_cross);
            in_draw = 0, fo.have = fn.have = 0, side = 0;
            continue;
        }
        if (!in_draw)
            in_draw = 1, draws++, d_t0 = f->t, d_frames = d_cross = 0, d_cant_lo = 1e9f, d_cant_hi = -1e9f, d_turn = 0;
        d_frames++;
        /* builds 16-36: turned onto the line through the unturned nock, then the 4 Hz filter (the arm's) */
        sv_quat qo = somvr_bow_fit(&c, p, q, ps, NULL, NULL), qos = somvr_bow_qfilter(&fo, qo, f->t, 4.0f, 1.0f);
        sv_vec3 fq = somvr_bow_fwd(&c, q), fo2 = somvr_bow_fwd(&c, qo);
        float ct = fq.x * fo2.x + fq.y * fo2.y + fq.z * fo2.z, turn = acosf(ct > 1 ? 1 : ct < -1 ? -1 : ct) * 57.29578f;
        float tilt_o, xmin_o, tilt_n, xmin_n;
        sv_vec3 ad_o, ad_n, rd_o, rd_n;
        bw_pass(&c, p, qos, ps, &tilt_o, &xmin_o, &ad_o);
        rd_o = somvr_bow_ray_dir(&c, q, n0, ps, NULL); /* builds 16-36's shot: through the unturned nock */
        /* SOMVR_BOW_FIT=on with the side by the cant */
        c.fit = 1;
        sv_vec3 dn;
        sv_quat qn = somvr_bow_fit(&c, p, q, ps, NULL, &dn);
        side = somvr_bow_side_pick(&c, qn, dn, side);
        side_n[side > 0]++;
        { /* the eyes' side of the shown bow's plane (where the riser cannot hide the arrow) against the cant's pick */
            float emx[3] = { 0.5f * (f->eye_p[0][0] + f->eye_p[1][0]), 0.5f * (f->eye_p[0][1] + f->eye_p[1][1]),
                             0.5f * (f->eye_p[0][2] + f->eye_p[1][2]) };
            sv_vec3 em = sv_xr_to_game_pos(emx);
            sv_vec3 nk = somvr_bow_nock(&c, p, qn), xa = sv_qrot(qn, (sv_vec3){ 1, 0, 0 });
            float de = (em.x - nk.x) * xa.x + (em.y - nk.y) * xa.y + (em.z - nk.z) * xa.z;
            g_eye_n++, g_eye_agree += (de > 0) == (side > 0), g_eye_far += fabsf(de) > 3.0f;
            g_eye_agree_far += fabsf(de) > 3.0f && (de > 0) == (side > 0);
        }
        float n0x = c.nock_ofs[0];
        c.nock_ofs[0] = BW_X0 + (float)side * fabsf(n0x - BW_X0);
        qn = somvr_bow_fit(&c, p, q, ps, NULL, &rd_n);
        sv_quat qns = somvr_bow_qfilter(&fn, qn, f->t, 4.0f, 1.0f);
        bw_pass(&c, p, qns, ps, &tilt_n, &xmin_n, &ad_n);
        float off_o = bw_ang(rd_o, ad_o), off_n = bw_ang(rd_n, ad_n);
        c.nock_ofs[0] = n0x;
        float cant = somvr_bow_cant_deg(&c, qos, somvr_bow_fwd(&c, qos));
        if (!isfinite(cant))
            continue;
        int b = (int)floorf((cant + 180.0f) / 30.0f);
        b = b < 0 ? 0 : b >= NB ? NB - 1 : b;
        bw_add(&ob[b], tilt_o, xmin_o, turn, off_o), bw_add(&nb[b], tilt_n, xmin_n, turn, off_n);
        bw_add(&ot, tilt_o, xmin_o, turn, off_o), bw_add(&nt, tilt_n, xmin_n, turn, off_n);
        d_cross += xmin_o <= 0.0f;
        if (g_bw_poses && d_frames > 20) { /* the layer's words for this frame: a held draw (20 frames in) at a new cant */
            static const float want[] = { -150, -120, -90, -60, -30, 0, 30, 60, 90, 120 };
            static int done[10];
            for (int k = 0; k < 10; k++)
                if (!done[k] && fabsf(cant - want[k]) < 4.0f) {
                    float hp[3], wl[6], wr[6], fw[3] = { 0, 0, -1 }, fv[3];
                    for (int j = 0; j < 3; j++) hp[j] = 0.5f * (f->eye_p[0][j] + f->eye_p[1][j]);
                    bw_word(hp, f->eye_q[0], f->hand[0].p, f->hand[0].q, wl);
                    bw_word(hp, f->eye_q[0], f->hand[1].p, f->hand[1].q, wr);
                    if (!(wl[2] > 40 && wr[2] < 25 && wl[2] - wr[2] > 40)) /* a held draw: the bow ahead, the string back */
                        continue;
                    done[k] = 1;
                    qrot(f->eye_q[0], fw, fv);
                    printf("POSE cant %.0f t=%.3f side %+d: hand L head %.1f %.1f %.1f %.1f %.1f %.1f | hand R head %.1f %.1f %.1f "
                           "%.1f %.1f %.1f | head pitch %.0f\n", (double)cant, f->t, side, (double)wl[0], (double)wl[1], (double)wl[2],
                           (double)wl[3], (double)wl[4], (double)wl[5], (double)wr[0], (double)wr[1], (double)wr[2], (double)wr[3],
                           (double)wr[4], (double)wr[5], (double)(asinf(fv[1]) * 57.29578f));
                }
        }
        d_cant_lo = cant < d_cant_lo ? cant : d_cant_lo, d_cant_hi = cant > d_cant_hi ? cant : d_cant_hi;
        d_turn = turn > d_turn ? turn : d_turn;
        (void)flips;
    }
    printf("bow: %d draws, %d drawn frames (the string hand 20+ cm back); the arrow on the grip's -X %d, +X %d frames (fit, by "
           "the cant)\n", draws, ot.n, side_n[0], side_n[1]);
    printf("  (tilt: the arrow off the shown bow's shot; through: frames with the arrow across the riser's middle; <1cm: within\n"
           "  1 cm of it; least: the nearest, cm; shot: the shot's ray off the arrow seen, deg avg/max)\n");
    printf("  cant (deg)  frames  turn(avg)  | builds 16-36: tilt avg/max  through  <1cm  least  shot avg/max | fit: tilt avg/max  through  <1cm  least  shot avg/max\n");
    for (int b = 0; b < NB; b++) {
        if (!ob[b].n)
            continue;
        printf("  %4d..%4d  %6d  %8.1f   |  %5.1f/%5.1f  %7d  %5d  %5.1f  %4.1f/%4.1f  |  %5.1f/%5.1f  %6d  %5d  %5.1f  %4.1f/%4.1f\n",
               b * 30 - 180, b * 30 - 150, ob[b].n, (double)(ob[b].turn_sum / ob[b].n), (double)(ob[b].tilt_sum / ob[b].n),
               (double)ob[b].tilt_max, ob[b].cross, ob[b].near1, (double)ob[b].xmin_min, (double)(ob[b].off_sum / ob[b].n),
               (double)ob[b].off_max, (double)(nb[b].tilt_sum / nb[b].n), (double)nb[b].tilt_max, nb[b].cross, nb[b].near1,
               (double)nb[b].xmin_min, (double)(nb[b].off_sum / nb[b].n), (double)nb[b].off_max);
    }
    if (ot.n)
        printf("  all         %6d  %8.1f   |  %5.1f/%5.1f  %7d  %5d  %5.1f  %4.1f/%4.1f  |  %5.1f/%5.1f  %6d  %5d  %5.1f  %4.1f/%4.1f\n",
               ot.n, (double)(ot.turn_sum / ot.n), (double)(ot.tilt_sum / ot.n), (double)ot.tilt_max, ot.cross, ot.near1,
               (double)ot.xmin_min, (double)(ot.off_sum / ot.n), (double)ot.off_max, (double)(nt.tilt_sum / nt.n),
               (double)nt.tilt_max, nt.cross, nt.near1, (double)nt.xmin_min, (double)(nt.off_sum / nt.n), (double)nt.off_max);
    if (g_eye_n)
        printf("  the eyes' side of the bow's plane = the cant's side in %d of %d drawn frames; the eyes over 3 cm off the plane in "
               "%d, the same side in %d of those\n", g_eye_agree, g_eye_n, g_eye_far, g_eye_agree_far);
    free(st), free(sf);
}

/* build 40 (SOMVR_BOW_HANG_REACH, re-xr-pipeline.md 175.1): the left (bow) hand against its shoulder slot (the game's
 * slot SOMVR_BOW_SLOT_CM 12,-10,-15 mirrored to its side, on the head's heading - the game's torso yaw when it has one),
 * frame by frame through somvr_bow_reach_step: how many frames the rule would have kept a hands-free hang and why, the
 * spans, the nearest the hand came. A stuck player's top-hang (mark 223841_10) must stay unkept: it still lets go. */
static void hangbow(const irr_rec *r, double t0, double t1, float slot_r) {
    somvr_bow_reach_cfg c;
    somvr_bow_reach_defaults(&c, slot_r);
    somvr_bow_reach_st s;
    memset(&s, 0, sizeof s);
    int n = 0, why_n[SOMVR_BRCH_GRACE + 1] = {0}, last = -1, sq = 0, presses = 0;
    double span0 = 0, tl = 0;
    float dmin = 1e9f, dmax = 0;
    for (int i = 0; i < r->nf; i++) {
        const somvr_ir_frame *f = &r->f[i];
        if (t1 > t0 && (f->t < t0 || f->t > t1)) continue;
        somvr_posef e[2];
        for (int k = 0; k < 2; k++) memcpy(e[k].p, f->eye_p[k], sizeof e[k].p), memcpy(e[k].q, f->eye_q[k], sizeof e[k].q);
        sv_vec3 hp;
        sv_quat hq;
        somvr_head_game(e, &hp, &hq);
        int lefty = (f->flags & SOMVR_IRF_LEFTY) != 0;
        const somvr_ir_hand *ph = &f->hand[0 ^ lefty]; /* the bow hand's controller (the left role) */
        float d = NAN, ahead = 0;
        if (ph->valid) {
            somvr_hand hd;
            sv_vec3 p;
            somvr_ir_hand_get(ph, f->xr_time, &hd, NULL);
            somvr_hand_game(&hd, &p, NULL, NULL, NULL);
            float yaw = somvr_slots_heading(hq);
            sv_vec3 o = sv_qrot(sv_qyaw(yaw), (sv_vec3){-12, -10, -15});
            sv_vec3 dv = {p.x - hp.x - o.x, p.y - hp.y - o.y, p.z - hp.z - o.z};
            d = sqrtf(dv.x * dv.x + dv.y * dv.y + dv.z * dv.z);
            ahead = sv_qrot(sv_qconj(sv_qyaw(yaw)), dv).z - 15; /* (ahead of the head: the slot is 15 cm behind it) */
            dmin = fminf(dmin, d), dmax = fmaxf(dmax, d);
        }
        float v = (ph->active & SOMVR_HACT_INPUT) ? ph->squeeze : 0;
        int was = sq;
        sq = was ? v > 0.4f : v > 0.6f;
        presses += sq && !was;
        int w = somvr_bow_reach_step(&s, &c, ph->valid, d, ahead, sq && !was, f->t);
        why_n[w]++, n++;
        if (w != last) {
            if (last > 0) printf("  t=%.3f .. %.3f (%.2f s): %s\n", span0, tl, tl - span0, somvr_bow_reach_name(last));
            span0 = f->t, last = w;
        }
        tl = f->t;
    }
    if (last > 0) printf("  t=%.3f .. %.3f (%.2f s): %s\n", span0, tl, tl - span0, somvr_bow_reach_name(last));
    printf("hangbow: %d frames: none %d, at the slot %d, reaching %d, unseen %d, the grace %d; the bow hand %.0f..%.0f cm from "
           "its slot, its grip closed %d times\n", n, why_n[0], why_n[SOMVR_BRCH_AT], why_n[SOMVR_BRCH_TOWARD],
           why_n[SOMVR_BRCH_UNSEEN], why_n[SOMVR_BRCH_GRACE], (double)dmin, (double)dmax, presses);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "replay_input FILE info|tiers|slots|grip|bow|hangbow [T0 T1] [--cfg FROM.inrec] [--along A]\n");
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
        } else if (!strcmp(argv[ai], "--poses")) {
            g_bw_poses = 1;
        } else if (!strcmp(argv[ai], "--along") && ai + 1 < argc) {
            along = (float)atof(argv[++ai]);
        }
    }
    const char *m = argv[2];
    if (!strcmp(m, "info")) {
        info(&r);
    } else if (!strcmp(m, "bow")) {
        bow(&r, t0, t1);
    } else if (!strcmp(m, "hangbow")) {
        hangbow(&r, t0, t1, 20);
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
