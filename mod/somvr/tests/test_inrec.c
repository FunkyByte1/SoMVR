/* test_inrec.c - b35 (docs/re-xr-pipeline.md 160): SOMVR_INPUT_REC's format, the replay's sampler and remap, and the
 * offline replay library (tests/inrec_replay.h) on a synthetic recording: a swing's blade and its hit, a knife drawn
 * from the belt. Pure: no game, no files (the recording is built in memory). */
#include "inrec_replay.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail, g_n;
#define EXPECT(c, ...)                                                                                                      \
    do {                                                                                                                    \
        g_n++;                                                                                                              \
        if (!(c)) {                                                                                                         \
            g_fail++;                                                                                                       \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                     \
            printf(__VA_ARGS__);                                                                                            \
            printf("\n");                                                                                                   \
        }                                                                                                                   \
    } while (0)

static float d3(sv_vec3 a, sv_vec3 b) {
    return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}
static float qang(sv_quat a, sv_quat b) { /* the angle between two orientations (deg): of conj(a) b, exact when small */
    double x = (double)a.w * b.x - (double)a.x * b.w - (double)a.y * b.z + (double)a.z * b.y,
           y = (double)a.w * b.y + (double)a.x * b.z - (double)a.y * b.w - (double)a.z * b.x,
           z = (double)a.w * b.z - (double)a.x * b.y + (double)a.y * b.x - (double)a.z * b.w,
           w = (double)a.w * b.w + (double)a.x * b.x + (double)a.y * b.y + (double)a.z * b.z;
    return (float)(2.0 * atan2(sqrt(x * x + y * y + z * z), fabs(w)) * 57.29577951308232);
}

/* a growing buffer of records after a header */
typedef struct {
    unsigned char *b;
    size_t n, cap;
} wbuf;
static void wput(wbuf *w, const void *p, size_t n) {
    if (w->n + n > w->cap) {
        w->cap = (w->n + n) * 2 + 4096;
        w->b = realloc(w->b, w->cap);
    }
    memcpy(w->b + w->n, p, n), w->n += n;
}

static void test_format(void) {
    somvr_ir_hdr h;
    somvr_ir_hdr_init(&h, 12.5, 1700000000, 42, 0);
    EXPECT(somvr_ir_hdr_ok(&h), "a fresh header is ok");
    h.frame_size = 360;
    EXPECT(!somvr_ir_hdr_ok(&h), "another build's frame size is refused");
    /* a hand through put/get */
    somvr_hand hd;
    memset(&hd, 0, sizeof hd);
    hd.valid = 1, hd.tracked = 1, hd.vel_valid = 3, hd.src = SOMVR_HSRC_LOCATED;
    float p[3] = { 0.2f, 1.1f, -0.4f }, q[4] = { 0.1f, 0.2f, 0.3f, 0.927362f };
    memcpy(hd.pose.p, p, sizeof p), memcpy(hd.pose.q, q, sizeof q);
    hd.lin[0] = 3, hd.ang[2] = -7;
    somvr_hand_in in;
    memset(&in, 0, sizeof in);
    in.trigger = 0.75f, in.squeeze = 0.25f, in.stick[1] = -1, in.buttons = SOMVR_BTN_LOWER | SOMVR_BTN_UPPER;
    in.presses[0] = 250, in.active = SOMVR_HACT_POSE | SOMVR_HACT_INPUT, in.aim.q[3] = 1, in.aim.p[1] = 1.5f;
    somvr_ir_hand r;
    somvr_ir_hand_put(&r, &hd, &in, -1);
    somvr_hand h2;
    somvr_hand_in i2;
    somvr_ir_hand_get(&r, 777, &h2, &i2);
    EXPECT(h2.valid && h2.tracked && h2.vel_valid == 3 && h2.time == 777 && !memcmp(h2.pose.p, p, sizeof p) &&
               !memcmp(h2.pose.q, q, sizeof q) && h2.lin[0] == 3 && h2.ang[2] == -7,
           "the grip back as it was");
    EXPECT(i2.trigger == 0.75f && i2.squeeze == 0.25f && i2.stick[1] == -1 && i2.buttons == (SOMVR_BTN_LOWER | SOMVR_BTN_UPPER) &&
               i2.presses[0] == 250 && i2.active == (SOMVR_HACT_POSE | SOMVR_HACT_INPUT) && i2.aim.p[1] == 1.5f,
           "the controls back as they were");
    somvr_ir_hand_put(&r, &hd, NULL, 0.9f);
    EXPECT(r.squeeze == 0.9f && (r.active & SOMVR_HACT_INPUT), "a test squeeze is recorded as a live squeeze");
    /* records in a buffer: a note, a cfg, an unknown type (skipped by its size), a frame */
    unsigned char buf[2048];
    size_t n = 0, k = somvr_ir_note_make(buf, sizeof buf, 1.0, "rec start");
    EXPECT(k == 32 && k % 8 == 0, "a note of 9 chars is 32 bytes (%zu)", k);
    n += k;
    sc_cfg c;
    sc_cfg_default(&c);
    k = somvr_ir_cfg_make(buf + n, sizeof buf - n, SOMVR_IRC_SCCFG, &c, sizeof c);
    EXPECT(k >= sizeof c + 16 && k % 8 == 0, "the cfg record");
    n += k;
    somvr_ir_rh odd = { 99, 24 };
    memcpy(buf + n, &odd, sizeof odd), memset(buf + n + 4, 0x5a, 20), n += 24;
    somvr_ir_frame f;
    memset(&f, 0, sizeof f);
    f.h = (somvr_ir_rh){ SOMVR_IR_FRAME, sizeof f };
    memcpy(buf + n, &f, sizeof f), n += sizeof f;
    int types[4] = { 0 }, nr = 0;
    somvr_ir_rh rh;
    size_t o = 0;
    for (; somvr_ir_next(buf, n, o, &rh); o += rh.size)
        if (nr < 4)
            types[nr++] = rh.type;
    EXPECT(nr == 4 && types[0] == SOMVR_IR_NOTE && types[1] == SOMVR_IR_CFG && types[2] == 99 && types[3] == SOMVR_IR_FRAME &&
               o == n,
           "four records walked, the unknown one skipped by its size (%d)", nr);
    EXPECT(!somvr_ir_next(buf, n - 8, n - sizeof f, &rh), "a cut-off record is not whole");
}

static void mkframe(somvr_ir_frame *f, int64_t t, float x) {
    memset(f, 0, sizeof *f);
    f->h = (somvr_ir_rh){ SOMVR_IR_FRAME, sizeof *f };
    f->xr_time = t, f->t = (double)t * 1e-9;
    for (int e = 0; e < 2; e++)
        f->eye_p[e][0] = x + (e ? 0.032f : -0.032f), f->eye_p[e][1] = 1.6f, f->eye_q[e][3] = 1;
    for (int h = 0; h < 2; h++) {
        f->hand[h].valid = 1, f->hand[h].p[0] = x, f->hand[h].q[3] = 1, f->hand[h].aim_q[3] = 1;
        f->hand[h].trigger = x, f->hand[h].presses[0] = (uint8_t)(x * 10);
    }
}

static void test_sample(void) {
    somvr_ir_frame f[3], s;
    mkframe(&f[0], 1000000000, 0), mkframe(&f[1], 1011000000, 1), mkframe(&f[2], 1022000000, 2);
    float a = 0.7071068f;
    f[1].hand[1].q[1] = a, f[1].hand[1].q[3] = a; /* 90 deg about +Y */
    EXPECT(somvr_ir_find(f, 3, 999) == -1 && somvr_ir_find(f, 3, 1000000000) == 0 && somvr_ir_find(f, 3, 1015000000) == 1 &&
               somvr_ir_find(f, 3, 2000000000) == 2,
           "find");
    uint32_t fl;
    somvr_ir_sample(f, 3, 1005500000, &s, &fl);
    EXPECT(fl == 1 && fabsf(s.hand[0].p[0] - 0.5f) < 1e-5f && fabsf(s.eye_p[1][0] - 0.532f) < 1e-5f &&
               fabsf(s.hand[0].trigger - 0.5f) < 1e-5f && s.hand[0].presses[0] == 0 && s.xr_time == 1005500000,
           "half way: positions and the trigger halved, the presses the earlier frame's (fl %u, x %.4f)", fl,
           (double)s.hand[0].p[0]);
    sv_quat qh = { s.hand[1].q[0], s.hand[1].q[1], s.hand[1].q[2], s.hand[1].q[3] };
    EXPECT(fabsf(qang(qh, (sv_quat){ 0, 0, 0, 1 }) - 45) < 0.5f, "the orientation half way: 45 deg (%.2f)",
           (double)qang(qh, (sv_quat){ 0, 0, 0, 1 }));
    somvr_ir_sample(f, 3, 3000000000, &s, &fl);
    EXPECT(fl == 2 && s.hand[0].p[0] == 2, "past the end: the last frame held");
    somvr_ir_sample(f, 3, 5, &s, &fl);
    EXPECT(fl == 0 && s.hand[0].p[0] == 0, "before the start: the first held");
    EXPECT(somvr_ir_press(17, 200, 203) == 20 && somvr_ir_press(250, 254, 2) == 254, "the press counters rebased mod 256");
}

/* the remap keeps the recorded poses' place to the rig: the live rig maps the moved pose where the recorded rig mapped
 * the recorded one (relative to each base), for any P0, yaw0 and base heading; kappa latches the base heading */
static void test_remap(void) {
    srand(7);
    float worst = 0, worst_q = 0, worst_v = 0, ident = 0;
    for (int it = 0; it < 200; it++) {
        float r[12];
        for (int i = 0; i < 12; i++)
            r[i] = (float)rand() / (float)RAND_MAX * 2 - 1;
        somvr_rig src, live;
        somvr_rig_init(&src), somvr_rig_init(&live);
        src.have_ref = live.have_ref = 1;
        src.p0 = (sv_vec3){ 50 * r[0], 160 + 10 * r[1], 50 * r[2] }, src.yaw0 = 3 * r[3];
        live.p0 = (sv_vec3){ 80 * r[4], 150 + 10 * r[5], 80 * r[6] }, live.yaw0 = 3 * r[7];
        somvr_base bs = { { 1000, 50, -300 }, 3 * r[8] }, bl = { { -40, 7, 22 }, 3 * r[9] };
        float kappa = sv_wrap_pi(bs.yaw - bl.yaw);
        somvr_ir_map m;
        float p0s[3] = { src.p0.x, src.p0.y, src.p0.z }, p0l[3] = { live.p0.x, live.p0.y, live.p0.z };
        somvr_ir_map_make(&m, p0s, src.yaw0, p0l, live.yaw0, kappa);
        float p[3] = { 0.4f * r[10], 1.2f + 0.3f * r[11], -0.3f * r[0] }, q[4] = { 0.2f * r[1], 0.5f * r[2], 0.1f * r[3], 0 }, op[3],
              oq[4];
        q[3] = sqrtf(1 - q[0] * q[0] - q[1] * q[1] - q[2] * q[2]);
        somvr_ir_map_pose(&m, p, q, op, oq);
        sv_vec3 ws = somvr_rig_point(&src, bs, sv_xr_to_game_pos(p)), wl = somvr_rig_point(&live, bl, sv_xr_to_game_pos(op));
        sv_vec3 rs = { ws.x - bs.pos.x, ws.y - bs.pos.y, ws.z - bs.pos.z }, rl = { wl.x - bl.pos.x, wl.y - bl.pos.y, wl.z - bl.pos.z };
        worst = fmaxf(worst, d3(rs, rl));
        sv_quat qs = somvr_rig_quat(&src, bs, sv_xr_to_game_quat(q)), ql = somvr_rig_quat(&live, bl, sv_xr_to_game_quat(oq));
        worst_q = fmaxf(worst_q, qang(qs, ql));
        float v[3] = { 2 * r[4], r[5], -3 * r[6] }, ov[3];
        somvr_ir_map_vec(&m, v, ov);
        somvr_hand ha, hb;
        memset(&ha, 0, sizeof ha), memset(&hb, 0, sizeof hb);
        memcpy(ha.lin, v, sizeof v), memcpy(hb.lin, ov, sizeof ov);
        sv_vec3 va, vb;
        somvr_hand_game(&ha, NULL, NULL, &va, NULL), somvr_hand_game(&hb, NULL, NULL, &vb, NULL);
        worst_v = fmaxf(worst_v, d3(somvr_rig_vec(&src, bs, va), somvr_rig_vec(&live, bl, vb)));
        /* the test can tell: without the remap (the recorded XR pose as it is) the place is off */
        sv_vec3 wi = somvr_rig_point(&live, bl, sv_xr_to_game_pos(p));
        ident = fmaxf(ident, d3(rs, (sv_vec3){ wi.x - bl.pos.x, wi.y - bl.pos.y, wi.z - bl.pos.z }));
    }
    EXPECT(worst < 0.01f, "the remapped point keeps its place to the base (worst %.4f cm)", (double)worst);
    EXPECT(worst_q < 0.01f, "... and its orientation (worst %.5f deg)", (double)worst_q);
    EXPECT(worst_v < 0.01f, "... and a velocity's direction (worst %.4f cm/s)", (double)worst_v);
    EXPECT(ident > 50, "without the remap the place is off (%.0f cm): the check can fail", (double)ident);
}

/* a synthetic recording: a 0.5 s horizontal cut whose blade crosses an orc (one HIT the way combat_frame deals it),
 * then a knife drawn from the left hip (the hand at the hip, the squeeze, out to the front) */
static void build_rec(wbuf *w, const sc_cfg *c, int *tier_dealt) {
    somvr_ir_hdr hd;
    somvr_ir_hdr_init(&hd, 100.0, 0, 1, 0);
    wput(w, &hd, sizeof hd);
    unsigned char rec[512];
    size_t k = somvr_ir_cfg_make(rec, sizeof rec, SOMVR_IRC_SCCFG, c, sizeof *c);
    wput(w, rec, k);
    sc_speed sp;
    sc_speed_reset(&sp);
    *tier_dealt = -1;
    double t = 100.0;
    for (int i = 0; i < 30; i++, t += 1.0 / 90) { /* the cut: the arm swings the blade 3 rad in 0.2 s about the shoulder */
        float u = (float)i / 18.0f, ang = u > 1 ? 3.0f : 3.0f * (u * u * (3 - 2 * u)),
              dang = u > 1 ? 0 : 3.0f * 6 * u * (1 - u) * 90.0f / 18.0f; /* rad, rad/s */
        float px = 30 * cosf(ang - 1.5f), pz = 30 * sinf(ang - 1.5f) + 30, dx[2] = { cosf(ang - 1.5f), sinf(ang - 1.5f) };
        somvr_ir_blade b;
        memset(&b, 0, sizeof b);
        b.h = (somvr_ir_rh){ SOMVR_IR_BLADE, sizeof b };
        b.t = t, b.flags = SOMVR_IRB_VALID | SOMVR_IRB_PUSHED;
        b.base[0] = px - 8 * dx[0], b.base[1] = 120, b.base[2] = pz - 8 * dx[1];
        b.tip[0] = px + 100 * dx[0], b.tip[1] = 120, b.tip[2] = pz + 100 * dx[1];
        /* v = w x r about the shoulder (0, 120, 30), the turn about +Y in the x-z plane: (-w rz, 0, w rx) */
        b.vel_base[0] = -dang * (b.base[2] - 30), b.vel_base[2] = dang * b.base[0];
        b.vel_tip[0] = -dang * (b.tip[2] - 30), b.vel_tip[2] = dang * b.tip[0];
        b.flat_n[1] = 1;
        wput(w, &b, sizeof b);
        somvr_blade bl;
        memset(&bl, 0, sizeof bl);
        bl.valid = 1, bl.t = b.t;
        memcpy(bl.base, b.base, 12), memcpy(bl.tip, b.tip, 12), memcpy(bl.vel_tip, b.vel_tip, 12),
            memcpy(bl.vel_base, b.vel_base, 12), memcpy(bl.flat_n, b.flat_n, 12);
        sc_speed_push(&sp, c, &bl);
        if (i == 10) { /* the contact, as the game deals it: the tier from the tracker now */
            float along = 0.7f, edge = sc_edge_deg(&bl);
            int tier = c->force ? sc_tier_force(c, &sp, edge, along) : sc_tier_swing(c, &sp, edge, along);
            somvr_ir_hit h = { { SOMVR_IR_HIT, sizeof h }, 0x1234, t, tier, along, edge, sc_blow_j(c, &sp, along),
                               sc_contact_mps(&sp, along), 1, (uint32_t)i, SOMVR_IRH_DEALT };
            wput(w, &h, sizeof h);
            *tier_dealt = tier;
        }
    }
    somvr_ir_blade none;
    memset(&none, 0, sizeof none);
    none.h = (somvr_ir_rh){ SOMVR_IR_BLADE, sizeof none }, none.t = t;
    wput(w, &none, sizeof none); /* the sword away: the tracker reset */
    /* the knife: the left hand from the front down to the left hip (game convention: x right, -z behind), the squeeze
     * there, then out to the front with it held; the head still, looking ahead (OpenXR: -Z forward) */
    for (int i = 0; i < 180; i++, t += 1.0 / 90) {
        somvr_ir_frame f;
        memset(&f, 0, sizeof f);
        f.h = (somvr_ir_rh){ SOMVR_IR_FRAME, sizeof f };
        f.t = t, f.xr_time = (int64_t)(t * 1e9), f.scale = 1, f.flags = SOMVR_IRF_SNAP | SOMVR_IRF_STEREO;
        for (int e = 0; e < 2; e++)
            f.eye_p[e][0] = e ? 0.032f : -0.032f, f.eye_p[e][1] = 1.65f, f.eye_q[e][3] = 1;
        float u = i < 60 ? (float)i / 60 : i < 100 ? 1 : 1 - (float)(i - 100) / 80; /* to the hip, hold, back out */
        /* the hip slot for a right main hand: (-18, -62, -5) cm from the head in the torso's frame (game convention) */
        float gx = -25 + 7 * u, gy = 165 - 30 - 32 * u, gz = 35 - 40 * u;
        for (int h = 0; h < 2; h++) {
            somvr_ir_hand *o = &f.hand[h];
            o->valid = 1, o->tracked = 1, o->vel_valid = 3, o->src = SOMVR_HSRC_LOCATED, o->q[3] = 1, o->aim_q[3] = 1;
            o->active = SOMVR_HACT_POSE | SOMVR_HACT_INPUT, o->grip_flags = o->aim_flags = 0xf;
            if (h == 0)
                o->p[0] = gx * 0.01f, o->p[1] = gy * 0.01f, o->p[2] = -gz * 0.01f, o->squeeze = i >= 70 ? 1.0f : 0.0f;
            else
                o->p[0] = 0.25f, o->p[1] = 1.2f, o->p[2] = -0.35f;
        }
        wput(w, &f, sizeof f);
    }
}

static void test_offline(void) {
    sc_cfg c;
    sc_cfg_default(&c);
    c.stroke = 1, c.force = 1, c.tip_max_mps = 60;
    wbuf w = { 0 };
    int dealt;
    build_rec(&w, &c, &dealt);
    irr_rec r;
    int rc = irr_take(&r, w.b, w.n);
    EXPECT(rc == 0 && r.has_cfg && r.nf == 180 && r.counts[SOMVR_IR_BLADE] == 31 && r.counts[SOMVR_IR_HIT] == 1,
           "the recording read back (rc %d, cfg %d, %d frames, %d blades, %d hits)", rc, r.has_cfg, r.nf,
           r.counts[SOMVR_IR_BLADE], r.counts[SOMVR_IR_HIT]);
    EXPECT(dealt == SC_TIER_HEAVY, "the synthetic cut is a cut (%s)", sc_tier_name(dealt));
    irr_tiers t;
    irr_replay_tiers(&r, NULL, 0, 0, &t, NULL);
    EXPECT(t.hits == 1 && t.same == 1 && t.rep[dealt] == 1 && t.pushed == 30 && t.resets == 1 && t.max_dj < 1e-4f,
           "the hit's tier again from the recorded blade: the same (%d of %d, %s, dJ %.6f)", t.same, t.hits,
           sc_tier_name(dealt), (double)t.max_dj);
    sc_cfg hard = r.cfg;
    hard.blow_heavy_j = 1e6f, hard.blow_min_j = 1e5f;
    irr_replay_tiers(&r, &hard, 0, 0, &t, NULL);
    EXPECT(t.hits == 1 && t.differ == 1, "a what-if config gives another tier: the replay is the config's, not a copy");
    /* the knife from the hip by the recorded left hand */
    somvr_slots_cfg sc;
    somvr_slots_cfg_default(&sc);
    irr_slots s;
    irr_replay_slots(&r, &sc, 0, 0, &s, NULL);
    EXPECT(s.frames == 180 && s.draws[SOMVR_SLOT_DAGGER] == 1 && s.draws[SOMVR_SLOT_SWORD] == 0,
           "the recorded hand at the hip with the squeeze draws the knife (draws %d, sword %d, enters %d)",
           s.draws[SOMVR_SLOT_DAGGER], s.draws[SOMVR_SLOT_SWORD], s.enters[SOMVR_SLOT_DAGGER]);
    sv_vec3 pw = { 0, 0, 0 };
    EXPECT(irr_hand_world(&r.f[0], 1, &pw, NULL, NULL) && fabsf(pw.x - 25) < 0.01f && fabsf(pw.z - 35) < 0.01f,
           "the right grip in the world (an identity rig: x 25, z 35 cm; %.2f %.2f)", (double)pw.x, (double)pw.z);
    irr_free(&r);
}

int main(void) {
    test_format();
    test_sample();
    test_remap();
    test_offline();
    printf("test_inrec: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
