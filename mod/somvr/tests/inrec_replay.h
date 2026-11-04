/* inrec_replay.h - b35 (docs/re-xr-pipeline.md 160.4): the offline replay library - a SOMVR_INPUT_REC recording (a
 * session's <log>.inrec, a mark's marks/<id>/input.inrec, a cut by scripts/inrec.py) in memory, and the player's
 * recorded motion fed into the logic's pure parts with no game at all:
 *   irr_replay_tiers   combat's force tiers: the BLADE stream through sc_speed_push in the game's order, each HIT's tier
 *                      again from the tracker at that point (sc_tier_force / _swing / build 16's), against the tier the
 *                      game dealt
 *   irr_replay_slots   the sword's and the knife's slots (somvr_slots_step): the head and the grips in tracking space,
 *                      the squeezes - the draws and put-backs the player's hands asked for
 *   irr_hand_world     a frame's grip in the world as the rig mapped it (the bow's, the verbs', a test's own)
 * Header only (static functions): a test includes it and links somvr_inrec.c, somvr_combat.c, somvr_slots.c,
 * somvr_hands.c, somvr_eyes.c, somvr_pose.c. */
#ifndef INREC_REPLAY_H
#define INREC_REPLAY_H

#include "../somvr_combat.h"
#include "../somvr_inrec.h"
#include "../somvr_slots.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned char *buf;
    size_t len, first; /* the records start at first (the header's size) */
    somvr_ir_hdr hdr;
    somvr_ir_frame *f; /* the FRAME records, in order */
    int nf;
    int has_cfg;       /* a CFG record of combat's sc_cfg of this build's size */
    sc_cfg cfg;
    int counts[SOMVR_IR_NTYPE];
} irr_rec;

__attribute__((unused)) static void irr_free(irr_rec *r) {
    free(r->buf), free(r->f);
    memset(r, 0, sizeof *r);
}

/* 0 = loaded (buf is taken over: freed by irr_free) */
__attribute__((unused)) static int irr_take(irr_rec *r, unsigned char *buf, size_t len) {
    memset(r, 0, sizeof *r);
    r->buf = buf, r->len = len;
    if (len < sizeof r->hdr)
        return -1;
    memcpy(&r->hdr, buf, sizeof r->hdr);
    if (!somvr_ir_hdr_ok(&r->hdr))
        return -2;
    r->first = r->hdr.hdr_size;
    somvr_ir_rh rh;
    int cap = 0;
    for (size_t o = r->first; somvr_ir_next(buf, len, o, &rh); o += rh.size) {
        if (rh.type < SOMVR_IR_NTYPE)
            r->counts[rh.type]++;
        if (rh.type == SOMVR_IR_FRAME && rh.size >= sizeof(somvr_ir_frame)) {
            if (r->nf == cap) {
                cap = cap ? 2 * cap : 1024;
                somvr_ir_frame *g = realloc(r->f, (size_t)cap * sizeof *g);
                if (!g)
                    return -3;
                r->f = g;
            }
            memcpy(&r->f[r->nf++], buf + o, sizeof(somvr_ir_frame));
        } else if (rh.type == SOMVR_IR_CFG && rh.size >= sizeof(somvr_ir_cfg)) {
            somvr_ir_cfg c;
            memcpy(&c, buf + o, sizeof c);
            if (c.id == SOMVR_IRC_SCCFG && c.len == sizeof(sc_cfg) && sizeof c + c.len <= rh.size && !r->has_cfg)
                memcpy(&r->cfg, buf + o + sizeof c, sizeof r->cfg), r->has_cfg = 1;
        }
    }
    return 0;
}

__attribute__((unused)) static int irr_load_file(irr_rec *r, const char *path) {
    FILE *in = fopen(path, "rb");
    if (!in)
        return -10;
    size_t cap = 1 << 20, n = 0;
    unsigned char *b = malloc(cap);
    size_t k;
    while (b && (k = fread(b + n, 1, cap - n, in)) > 0) {
        n += k;
        if (n == cap) {
            unsigned char *g = realloc(b, cap *= 2);
            if (!g)
                free(b);
            b = g;
        }
    }
    fclose(in);
    if (!b)
        return -11;
    int rc = irr_take(r, b, n);
    if (rc)
        free(b), r->buf = NULL;
    return rc;
}

/* frame f's grip of physical hand h in the world (game convention, cm) as the frame's rig mapped it */
__attribute__((unused)) static int irr_hand_world(const somvr_ir_frame *f, int h, sv_vec3 *pw, sv_quat *qw, sv_vec3 *vw) {
    if (!f->hand[h].valid)
        return 0;
    somvr_hand hd;
    somvr_ir_hand_get(&f->hand[h], f->xr_time, &hd, NULL);
    sv_vec3 p, v;
    sv_quat q;
    somvr_hand_game(&hd, &p, &q, &v, NULL);
    somvr_rig rg;
    somvr_rig_init(&rg);
    rg.have_ref = 1, rg.p0 = (sv_vec3){ f->rig_p0[0], f->rig_p0[1], f->rig_p0[2] }, rg.yaw0 = f->rig_yaw0, rg.scale = f->scale;
    somvr_base b = { { f->base[0], f->base[1], f->base[2] }, f->base_yaw };
    if (pw)
        *pw = somvr_rig_point(&rg, b, p);
    if (qw)
        *qw = somvr_rig_quat(&rg, b, q);
    if (vw)
        *vw = somvr_rig_vec(&rg, b, v);
    return 1;
}

/* ---- combat's tiers ------------------------------------------------------------------------------------------------ */

typedef struct {
    int hits, same, differ, blades, pushed, resets;
    int rec[SC_TIER_N], rep[SC_TIER_N];
    float max_dj; /* the largest |J again - J recorded| over the hits with a recorded energy */
} irr_tiers;

/* the tier at a contact as combat_frame's rule has it (somvr.c: force, then stroke, then build 16's) */
__attribute__((unused)) static int irr_tier_of(const sc_cfg *c, const sc_speed *s, float edge, float along) {
    return c->force ? sc_tier_force(c, s, edge, along) : c->stroke ? sc_tier_swing(c, s, edge, along) : sc_tier(c, s);
}

/* every BLADE and HIT record with t in [t0, t1] (t1 <= t0: all), in the file's order; cfg NULL = the recording's own
 * (its CFG record), else the given one (a what-if). Lines to out (may be NULL). 0 = run, -1 = no config. */
__attribute__((unused)) static int irr_replay_tiers(const irr_rec *r, const sc_cfg *cfg, double t0, double t1, irr_tiers *res, FILE *out) {
    memset(res, 0, sizeof *res);
    if (!cfg && !r->has_cfg)
        return -1;
    sc_cfg own = r->cfg; /* the recording's own rules: each CFG record from where it is (a change at run time) */
    const sc_cfg *c = cfg ? cfg : &own;
    sc_speed sp;
    sc_speed_reset(&sp);
    somvr_ir_rh rh;
    for (size_t o = r->first; somvr_ir_next(r->buf, r->len, o, &rh); o += rh.size) {
        if (!cfg && rh.type == SOMVR_IR_CFG && rh.size >= sizeof(somvr_ir_cfg)) {
            somvr_ir_cfg cr;
            memcpy(&cr, r->buf + o, sizeof cr);
            if (cr.id == SOMVR_IRC_SCCFG && cr.len == sizeof(sc_cfg) && sizeof cr + cr.len <= rh.size)
                memcpy(&own, r->buf + o + sizeof cr, sizeof own);
        } else if (rh.type == SOMVR_IR_BLADE && rh.size >= sizeof(somvr_ir_blade)) {
            somvr_ir_blade b;
            memcpy(&b, r->buf + o, sizeof b);
            if (!(b.flags & SOMVR_IRB_VALID)) {
                sc_speed_reset(&sp), res->resets++;
                continue;
            }
            res->blades++;
            if (!(b.flags & SOMVR_IRB_PUSHED))
                continue;
            somvr_blade bl;
            memset(&bl, 0, sizeof bl);
            bl.valid = 1, bl.t = b.t;
            memcpy(bl.base, b.base, sizeof bl.base), memcpy(bl.tip, b.tip, sizeof bl.tip);
            memcpy(bl.vel_tip, b.vel_tip, sizeof bl.vel_tip), memcpy(bl.vel_base, b.vel_base, sizeof bl.vel_base);
            memcpy(bl.flat_n, b.flat_n, sizeof bl.flat_n);
            sc_speed_push(&sp, c, &bl), res->pushed++;
        } else if (rh.type == SOMVR_IR_HIT && rh.size >= sizeof(somvr_ir_hit)) {
            somvr_ir_hit h;
            memcpy(&h, r->buf + o, sizeof h);
            if (t1 > t0 && (h.t < t0 || h.t > t1))
                continue;
            int tier = irr_tier_of(c, &sp, h.edge_deg, h.along);
            float j = NAN;
            if (c->force && (tier == SC_TIER_LIGHT || tier == SC_TIER_HEAVY)) {
                float js = sp.axis_mps >= c->stab_mps ? sc_stab_j(c, &sp) * sc_axis_share(&sp) : 0;
                j = fmaxf(sc_blow_j(c, &sp, h.along), js);
            } else if (c->force && tier == SC_TIER_STAB) {
                j = sc_stab_j(c, &sp);
            }
            res->hits++;
            if (h.tier >= 0 && h.tier < SC_TIER_N)
                res->rec[h.tier]++;
            res->rep[tier]++;
            tier == h.tier ? res->same++ : res->differ++;
            if (isfinite(h.blow_j) && isfinite(j) && fabsf(j - h.blow_j) > res->max_dj)
                res->max_dj = fabsf(j - h.blow_j);
            if (out)
                fprintf(out, "  %s t=%.3f orc %08x: the game %s (%.1f J, %.1f m/s at %.2f, edge %.0f deg) - again %s (%.1f J, "
                             "the contact %.1f m/s, the swing's mean %.1f m/s, the stroke %.0f cm)%s\n",
                        h.kind == SOMVR_IRH_CONTACT ? "contact" : "hit", h.t, h.orc, sc_tier_name(h.tier), (double)h.blow_j, (double)h.contact_mps, (double)h.along,
                        (double)h.edge_deg, sc_tier_name(tier), (double)j, (double)sc_contact_mps(&sp, h.along),
                        (double)sp.mean_mps, (double)sp.stroke_cm, tier == h.tier ? "" : "  <- DIFFERS");
        }
    }
    return 0;
}

/* ---- the sword's and the knife's slots ----------------------------------------------------------------------------- */

typedef struct {
    int frames;
    int draws[SOMVR_NSLOT], puts[SOMVR_NSLOT], enters[SOMVR_NSLOT];
    double first_draw_t[SOMVR_NSLOT];
} irr_slots;

/* the frames in [t0, t1] (t1 <= t0: all) through somvr_slots_step with cfg (the hands as the roles: a left-handed
 * recording swapped like the game does), the game's drawn state followed from each ask at once (an idealised game:
 * every ask is done) */
__attribute__((unused)) static void irr_replay_slots(const irr_rec *r, const somvr_slots_cfg *cfg, double t0, double t1, irr_slots *res, FILE *out) {
    memset(res, 0, sizeof *res);
    somvr_slots s;
    somvr_slots_init(&s);
    int drawn[SOMVR_NSLOT] = { 0, 0 };
    for (int i = 0; i < r->nf; i++) {
        const somvr_ir_frame *f = &r->f[i];
        if (t1 > t0 && (f->t < t0 || f->t > t1))
            continue;
        somvr_slots_in in;
        memset(&in, 0, sizeof in);
        in.t = f->t, in.live = 1, in.allow = 1, in.have[0] = in.have[1] = 1;
        memcpy(in.drawn, drawn, sizeof in.drawn);
        somvr_posef e[2];
        for (int k = 0; k < 2; k++)
            memcpy(e[k].p, f->eye_p[k], sizeof e[k].p), memcpy(e[k].q, f->eye_q[k], sizeof e[k].q);
        somvr_head_game(e, &in.head, &in.head_q);
        in.head_ok = 1;
        int lefty = (f->flags & SOMVR_IRF_LEFTY) != 0;
        for (int h = 0; h < 2; h++) {
            const somvr_ir_hand *ph = &f->hand[h ^ lefty]; /* the role's controller */
            in.valid[h] = ph->valid;
            if (ph->valid) {
                somvr_hand hd;
                somvr_ir_hand_get(ph, f->xr_time, &hd, NULL);
                somvr_hand_game(&hd, &in.p[h], NULL, NULL, NULL);
            }
            in.squeeze[h] = (ph->active & SOMVR_HACT_INPUT) ? ph->squeeze : 0;
        }
        somvr_slots_out o;
        somvr_slots_step(&s, cfg, &in, &o);
        res->frames++;
        for (int k = 0; k < SOMVR_NSLOT; k++) {
            if (o.ev[k] & SOMVR_SL_E_ENTER)
                res->enters[k]++;
            if (o.ev[k] & SOMVR_SL_E_DRAW) {
                if (!res->draws[k])
                    res->first_draw_t[k] = f->t;
                res->draws[k]++, drawn[k] = 1;
            }
            if (o.ev[k] & SOMVR_SL_E_PUT)
                res->puts[k]++, drawn[k] = 0;
            if (out && (o.ev[k] & (SOMVR_SL_E_DRAW | SOMVR_SL_E_PUT)))
                fprintf(out, "  slots t=%.3f the %s %s (the hand %.0f cm from its slot)\n", f->t, k ? "knife" : "sword",
                        o.ev[k] & SOMVR_SL_E_DRAW ? "drawn" : "put back", (double)o.dist[k]);
        }
    }
}

#endif
