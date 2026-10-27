/* test_body.c - unit tests of somvr_body.c on run 3's skeleton and pose (tests/run3_skeleton.h): the hands' keep set
 * (both hands' subtrees against a walk up every node's parents, the body's wrist side: the left clavicle and arm above
 * the wrist on the left wrist, everything else on the right one; broken chains and skeletons), the skin edit (the kept
 * hands bit for bit, the body and a hidden hand at the side's wrist with a zero rotation, NaN, 1 km below, "show", a
 * non-finite wrist, a set that does not fit), the words, the sword angle (the tilted calibration lays the same blade axis
 * on somvr_blade_controller's pitched blade), and the two-handed sword (no grab without the sword, outside the hilt, or
 * with the hand busy; the grab, the blade on the line from the left grip to the right one, the left hand on the hilt at
 * its clamped distance with its fist along the blade, the ease, the release and the ease back, pulled apart, a lost
 * hand, the hands together, continuity at 90 Hz). */
#include "../somvr_body.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "run3_skeleton.h"

static int fails, checks;
#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        checks++;                                                                                                      \
        if (!(c)) {                                                                                                    \
            fails++;                                                                                                   \
            printf("FAIL line %d: ", __LINE__);                                                                        \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)
#define NEAR(a, b, eps) (fabs((double)(a) - (double)(b)) <= (eps))
#define DEG 0.01745329252f

static void load_pose(sa_node *p) {
    for (int i = 0; i < RUN3_N; i++) {
        const float *r = RUN3_POSE[i];
        sa_node_set(&p[i], (sa_xf){{r[0], r[1], r[2]}, {r[3], r[4], r[5], r[6]}});
    }
}

static int under(int i, int j) { /* i is j or below it (walking up the parents) */
    for (int k = i, n = 0; k != 0xff && n <= RUN3_N; k = RUN3_PARENT[k], n++)
        if (k == j) return 1;
    return 0;
}

static float vlen3(sv_vec3 a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }
static sv_vec3 vsub3(sv_vec3 a, sv_vec3 b) { return (sv_vec3){a.x - b.x, a.y - b.y, a.z - b.z}; }
static float vdot3(sv_vec3 a, sv_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float qang(sv_quat a, sv_quat b) { /* the angle between two rotations */
    float d = fabsf(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    return 2 * acosf(d > 1 ? 1 : d);
}

static sa_chain g_ch[2];

static void test_keep(void) {
    char why[300];
    for (int s = 0; s < 2; s++)
        CHECK(sa_chain_build(&g_ch[s], s, RUN3_PARENT, RUN3_HASH, RUN3_N, why, sizeof why) == 0, "chain %d: %s", s, why);
    sb_keep k;
    int rc = sb_keep_build(&k, g_ch, RUN3_PARENT, RUN3_N, why, sizeof why);
    CHECK(rc == 0 && k.n == RUN3_N && k.wrist[0] == 90 && k.wrist[1] == 112, "run 3: rc %d wrists %d %d (%s)", rc, k.wrist[0],
          k.wrist[1], why);
    CHECK(k.nhand[0] == 1 + g_ch[0].nsub[2] && k.nhand[1] == 1 + g_ch[1].nsub[2] && k.nbody == RUN3_N - k.nhand[0] - k.nhand[1],
          "counts L %d R %d body %d (sub %d %d)", k.nhand[0], k.nhand[1], k.nbody, g_ch[0].nsub[2], g_ch[1].nsub[2]);
    int same = 1, side = 1;
    for (int i = 0; i < RUN3_N; i++) {
        int want = under(i, 90) ? SB_HAND_L : under(i, 112) ? SB_HAND_R : SB_BODY;
        same &= k.kind[i] == want;
        if (want == SB_BODY) side &= k.aside[i] == (under(i, g_ch[0].p) ? 0 : 1);
        else side &= k.aside[i] == want - 1;
    }
    CHECK(same, "the hands' subtrees = the nodes below L_Hand 90 / R_Hand 112");
    CHECK(side, "the body's wrist sides: the left clavicle %d's subtree on the left wrist", g_ch[0].p);
    CHECK(k.kind[g_ch[1].wpn] == SB_HAND_R && k.kind[g_ch[0].wpn] == SB_HAND_L && k.kind[g_ch[1].e] == SB_BODY &&
              k.kind[g_ch[1].tf] == SB_BODY && k.aside[g_ch[0].e] == 0 && k.aside[g_ch[0].s] == 0 && k.aside[g_ch[1].e] == 1 &&
              k.aside[28] == 1 && k.aside[0] == 1,
          "the weapon nodes kept, the forearms and their twist nodes not; sides");
    CHECK(!strncmp(why, "hands L_Hand 90 (17 nodes) R_Hand 112 (17 nodes)", 47), "summary '%s'", why);
    CHECK(k.stub[0][0] == 91 && k.stub[0][1] == 88 && k.stub[1][0] == 113 && k.stub[1][1] == 110,
          "the stubs: L_Forearm %d L_Arml %d R_Forearm %d R_Arml %d", k.stub[0][0], k.stub[0][1], k.stub[1][0], k.stub[1][1]);
    /* broken */
    sa_chain bad[2];
    memcpy(bad, g_ch, sizeof bad);
    bad[1].w = 100; /* not a child of R_Arml */
    CHECK(sb_keep_build(&k, bad, RUN3_PARENT, RUN3_N, why, sizeof why) == -1 && strstr(why, "right chain"), "a wrong wrist: %s",
          why);
    memcpy(bad, g_ch, sizeof bad);
    bad[0] = g_ch[1];
    CHECK(sb_keep_build(&k, bad, RUN3_PARENT, RUN3_N, why, sizeof why) == -1, "the right chain twice: %s", why);
    CHECK(sb_keep_build(&k, g_ch, RUN3_PARENT, 1, why, sizeof why) == -1, "one node");
    uint8_t par[RUN3_N];
    memcpy(par, RUN3_PARENT, sizeof par);
    par[50] = 60;
    CHECK(sb_keep_build(&k, g_ch, par, RUN3_N, why, sizeof why) == -1 && strstr(why, "does not precede"), "order: %s", why);
    CHECK(sb_keep_build(&k, g_ch, RUN3_PARENT, 100, why, sizeof why) == -1, "a chain beyond n: %s", why);
}

static void test_skin(void) {
    char why[300];
    sb_keep k;
    sb_keep_build(&k, g_ch, RUN3_PARENT, RUN3_N, why, sizeof why);
    static sa_node model[RUN3_N], skin[RUN3_N], orig[RUN3_N];
    load_pose(model);
    for (int i = 0; i < RUN3_N; i++) { /* some skin: the pose turned and moved (any rigid values) */
        sa_xf x = sa_node_xf(&model[i]);
        x.p.x += 3 * i, x.q = sv_qnormalize(sv_qmul(x.q, (sv_quat){0.1f, 0.2f, 0, 1}));
        sa_node_set(&orig[i], x);
    }
    for (unsigned show = 0; show < 4; show++) {
        memcpy(skin, orig, sizeof skin);
        int n = sb_skin_edit(skin, model, RUN3_N, &k, show, SB_HIDE_ZERO), ok = 1, cnt = 0;
        for (int i = 0; i < RUN3_N; i++) {
            int kept = k.kind[i] != SB_BODY && (show >> (k.kind[i] - 1) & 1);
            if (kept) {
                ok &= !memcmp(&skin[i], &orig[i], sizeof skin[i]);
                continue;
            }
            cnt++;
            int s = k.kind[i] != SB_BODY ? k.kind[i] - 1 : k.aside[i];
            const float *w = model[k.wrist[s]].pos;
            ok &= skin[i].pos[0] == w[0] && skin[i].pos[1] == w[1] && skin[i].pos[2] == w[2] && skin[i].pad == 0 &&
                  skin[i].q[0] == 0 && skin[i].q[1] == 0 && skin[i].q[2] == 0 && skin[i].q[3] == 0;
        }
        CHECK(ok && n == cnt, "zero, show %u: %d written (want %d), the kept hands bit for bit, the rest at the wrist", show, n,
              cnt);
    }
    memcpy(skin, orig, sizeof skin);
    int n = sb_skin_edit(skin, model, RUN3_N, &k, 3, SB_HIDE_NAN), ok = 1;
    for (int i = 0; i < RUN3_N; i++)
        ok &= k.kind[i] != SB_BODY ? !memcmp(&skin[i], &orig[i], sizeof skin[i])
                                   : isnan(skin[i].pos[0]) && isnan(skin[i].q[3]) && skin[i].pad == 0;
    CHECK(ok && n == k.nbody, "nan: %d written", n);
    memcpy(skin, orig, sizeof skin);
    n = sb_skin_edit(skin, model, RUN3_N, &k, 3, SB_HIDE_FAR), ok = 1;
    for (int i = 0; i < RUN3_N; i++)
        if (k.kind[i] == SB_BODY) {
            const float *w = model[k.wrist[k.aside[i]]].pos;
            ok &= skin[i].pos[0] == w[0] && NEAR(skin[i].pos[1], w[1] - SB_FAR_CM, 0.01) && skin[i].q[3] == 1 &&
                  skin[i].q[0] == 0;
        }
    CHECK(ok && n == k.nbody, "far: %d written", n);
    /* line: skin = model x bind^-1 with a bind pose of its own; the shader's v' = p + v + 2w (u x v) + 2 u x (u x v) of
     * a hidden node puts every bind-space vertex on the line through both posed wrists, a wrist's own bind point on
     * that wrist */
    static sa_xf bind[RUN3_N];
    for (int i = 0; i < RUN3_N; i++) {
        sa_xf m = sa_node_xf(&model[i]);
        bind[i] = (sa_xf){{m.p.x * 1.3f + 5, m.p.y - 7, m.p.z * 0.8f}, sv_qnormalize(sv_qmul(m.q, (sv_quat){0.3f, -0.1f, 0.2f, 1}))};
        sa_node_set(&skin[i], sa_xf_mul(m, sa_xf_inv(bind[i])));
    }
    memcpy(orig, skin, sizeof orig);
    for (unsigned show = 0; show < 4; show += 3) {
        memcpy(skin, orig, sizeof skin);
        n = sb_skin_edit(skin, model, RUN3_N, &k, show, SB_HIDE_LINE), ok = 1;
        sv_vec3 wl = {model[k.wrist[0]].pos[0], model[k.wrist[0]].pos[1], model[k.wrist[0]].pos[2]};
        sv_vec3 wr = {model[k.wrist[1]].pos[0], model[k.wrist[1]].pos[1], model[k.wrist[1]].pos[2]};
        sv_vec3 e = {wr.x - wl.x, wr.y - wl.y, wr.z - wl.z};
        float el = sqrtf(e.x * e.x + e.y * e.y + e.z * e.z), worst = 0, worst_w = 0;
        e = (sv_vec3){e.x / el, e.y / el, e.z / el};
        int cnt = 0;
        for (int i = 0; i < RUN3_N; i++) {
            int kept = k.kind[i] != SB_BODY && (show >> (k.kind[i] - 1) & 1);
            if (kept) {
                ok &= !memcmp(&skin[i], &orig[i], sizeof skin[i]);
                continue;
            }
            cnt++;
            sv_vec3 u = {skin[i].q[0], skin[i].q[1], skin[i].q[2]};
            float w = skin[i].q[3];
            int s = k.kind[i] != SB_BODY ? k.kind[i] - 1 : k.aside[i];
            for (int t = 0; t < 4; t++) {
                sv_vec3 v = t < 3 ? bind[(i * 7 + t * 31) % RUN3_N].p : bind[k.wrist[s]].p;
                sv_vec3 c1 = {u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
                sv_vec3 a = {c1.x + w * v.x, c1.y + w * v.y, c1.z + w * v.z}; /* u x v + w v */
                sv_vec3 c2 = {u.y * a.z - u.z * a.y, u.z * a.x - u.x * a.z, u.x * a.y - u.y * a.x};
                sv_vec3 r = {skin[i].pos[0] + v.x + 2 * c2.x, skin[i].pos[1] + v.y + 2 * c2.y, skin[i].pos[2] + v.z + 2 * c2.z};
                sv_vec3 d = {r.x - wl.x, r.y - wl.y, r.z - wl.z};
                float along = d.x * e.x + d.y * e.y + d.z * e.z;
                sv_vec3 off = {d.x - along * e.x, d.y - along * e.y, d.z - along * e.z};
                worst = fmaxf(worst, sqrtf(off.x * off.x + off.y * off.y + off.z * off.z));
                if (t == 3) {
                    const float *wp = model[k.wrist[s]].pos;
                    worst_w = fmaxf(worst_w, fabsf(r.x - wp[0]) + fabsf(r.y - wp[1]) + fabsf(r.z - wp[2]));
                }
            }
        }
        CHECK(ok && n == cnt && worst < 1e-2f && worst_w < 1e-2f,
              "line, show %u: %d written (want %d), the kept hands bit for bit, off the wrists' line %.5f cm at most, a "
              "wrist's bind point %.5f cm from its posed wrist", show, n, cnt, worst, worst_w);
    }
    /* cuff / forearm (round 3): a shown hand's stub nodes carry its wrist's skin transform bit for bit (their vertices move
     * with the hand as in the bind pose); every other hidden vertex on the line through both stubs' ends, each end
     * node's bind point carried by its hand onto its end (a hidden hand's side: its wrist, as the line) */
    for (int m = SB_HIDE_CUFF; m <= SB_HIDE_FOREARM; m++)
        for (unsigned show = 0; show < 4; show++) {
            memcpy(skin, orig, sizeof skin);
            n = sb_skin_edit(skin, model, RUN3_N, &k, show, m), ok = 1;
            int end[2], cnt = 0, stubok = 1;
            sv_vec3 P[2];
            for (int s2 = 0; s2 < 2; s2++) {
                end[s2] = (show >> s2 & 1) ? k.stub[s2][m - SB_HIDE_CUFF] : k.wrist[s2];
                P[s2] = sa_xf_mul(sa_node_xf(&orig[k.wrist[s2]]), (sa_xf){bind[end[s2]].p, {0, 0, 0, 1}}).p;
            }
            sv_vec3 e = vsub3(P[1], P[0]);
            float el = vlen3(e), worst = 0, worst_end = 0;
            e = (sv_vec3){e.x / el, e.y / el, e.z / el};
            for (int i = 0; i < RUN3_N; i++) {
                int kept = k.kind[i] != SB_BODY && (show >> (k.kind[i] - 1) & 1);
                if (kept) {
                    ok &= !memcmp(&skin[i], &orig[i], sizeof skin[i]);
                    continue;
                }
                cnt++;
                int s2 = k.kind[i] != SB_BODY ? k.kind[i] - 1 : k.aside[i];
                int stub = k.kind[i] == SB_BODY && (show >> s2 & 1) &&
                           (i == k.stub[s2][0] || (m == SB_HIDE_FOREARM && i == k.stub[s2][1]));
                if (stub) {
                    stubok &= !memcmp(skin[i].q, orig[k.wrist[s2]].q, sizeof skin[i].q) &&
                              !memcmp(skin[i].pos, orig[k.wrist[s2]].pos, sizeof skin[i].pos) && skin[i].pad == 0;
                    continue;
                }
                sv_vec3 u = {skin[i].q[0], skin[i].q[1], skin[i].q[2]};
                float w = skin[i].q[3];
                for (int t = 0; t < 4; t++) {
                    sv_vec3 v = t < 3 ? bind[(i * 7 + t * 31) % RUN3_N].p : bind[end[s2]].p;
                    sv_vec3 c1 = {u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
                    sv_vec3 a = {c1.x + w * v.x, c1.y + w * v.y, c1.z + w * v.z};
                    sv_vec3 c2 = {u.y * a.z - u.z * a.y, u.z * a.x - u.x * a.z, u.x * a.y - u.y * a.x};
                    sv_vec3 r = {skin[i].pos[0] + v.x + 2 * c2.x, skin[i].pos[1] + v.y + 2 * c2.y,
                                 skin[i].pos[2] + v.z + 2 * c2.z};
                    sv_vec3 d = vsub3(r, P[0]);
                    float along = vdot3(d, e);
                    worst = fmaxf(worst, vlen3((sv_vec3){d.x - along * e.x, d.y - along * e.y, d.z - along * e.z}));
                    if (t == 3) worst_end = fmaxf(worst_end, vlen3(vsub3(r, P[s2])));
                }
            }
            CHECK(ok && stubok && n == cnt && worst < 1e-2f && worst_end < 1e-2f,
                  "%s, show %u: %d written (want %d), hands kept %d, the stubs the wrists' skin %d, off the ends' line %.5f "
                  "cm, an end's bind point %.5f cm from its end", sb_hide_name(m), show, n, cnt, ok, stubok, worst,
                  worst_end);
        }
    /* cuffik: the twist node keeps its rotation, moved so that it carries the bind wrist onto the posed wrist (here the
     * pose's own wrist: a skin whose twist node is 6 cm off, as a stretched arm's); the line through the moved ends */
    static sa_node m3[RUN3_N];
    for (unsigned show = 1; show < 4; show += 2) {
        memcpy(skin, orig, sizeof skin), memcpy(m3, model, sizeof m3);
        /* the forearms' twist nodes left behind by a stretch: their pose and palette moved alike (skin = model x bind^-1) */
        skin[113].pos[0] += 6, m3[113].pos[0] += 6, skin[91].pos[1] -= 4, m3[91].pos[1] -= 4;
        sa_node pre[RUN3_N];
        memcpy(pre, skin, sizeof pre);
        n = sb_skin_edit(skin, m3, RUN3_N, &k, show, SB_HIDE_CUFFIK), ok = 1;
        float gapw = 0, rot = 0, worst = 0, wend = 0;
        sv_vec3 P[2];
        for (int s2 = 0; s2 < 2; s2++) {
            int on = show >> s2 & 1, t = k.stub[s2][0];
            sa_xf tf = sa_node_xf(&skin[t]);
            if (on) {
                gapw = fmaxf(gapw, vlen3(vsub3(sa_xf_mul(tf, (sa_xf){bind[k.wrist[s2]].p, {0, 0, 0, 1}}).p,
                                               (sv_vec3){model[k.wrist[s2]].pos[0], model[k.wrist[s2]].pos[1],
                                                         model[k.wrist[s2]].pos[2]})));
                rot = fmaxf(rot, fabsf(skin[t].q[0] - pre[t].q[0]) + fabsf(skin[t].q[3] - pre[t].q[3]));
                P[s2] = sa_xf_mul(tf, (sa_xf){bind[t].p, {0, 0, 0, 1}}).p;
            } else {
                P[s2] = (sv_vec3){model[k.wrist[s2]].pos[0], model[k.wrist[s2]].pos[1], model[k.wrist[s2]].pos[2]};
            }
        }
        sv_vec3 e = vsub3(P[1], P[0]);
        float el = vlen3(e);
        e = (sv_vec3){e.x / el, e.y / el, e.z / el};
        for (int i = 0; i < RUN3_N; i++) {
            if (k.kind[i] != SB_BODY && (show >> (k.kind[i] - 1) & 1)) {
                ok &= !memcmp(&skin[i], &orig[i], sizeof skin[i]);
                continue;
            }
            int s2 = k.kind[i] != SB_BODY ? k.kind[i] - 1 : k.aside[i];
            if (k.kind[i] == SB_BODY && (show >> s2 & 1) && i == k.stub[s2][0]) continue;
            sv_vec3 u = {skin[i].q[0], skin[i].q[1], skin[i].q[2]};
            float w = skin[i].q[3];
            int end = (show >> s2 & 1) ? k.stub[s2][0] : k.wrist[s2];
            for (int t = 0; t < 2; t++) {
                sv_vec3 v = t ? bind[end].p : bind[(i * 5 + 11) % RUN3_N].p;
                sv_vec3 c1 = {u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
                sv_vec3 a = {c1.x + w * v.x, c1.y + w * v.y, c1.z + w * v.z};
                sv_vec3 c2 = {u.y * a.z - u.z * a.y, u.z * a.x - u.x * a.z, u.x * a.y - u.y * a.x};
                sv_vec3 r = {skin[i].pos[0] + v.x + 2 * c2.x, skin[i].pos[1] + v.y + 2 * c2.y, skin[i].pos[2] + v.z + 2 * c2.z};
                sv_vec3 d = vsub3(r, P[0]);
                float along = vdot3(d, e);
                worst = fmaxf(worst, vlen3((sv_vec3){d.x - along * e.x, d.y - along * e.y, d.z - along * e.z}));
                if (t) wend = fmaxf(wend, vlen3(vsub3(r, P[s2])));
            }
        }
        CHECK(ok && gapw < 1e-2f && rot == 0 && worst < 1e-2f && wend < 1e-2f,
              "cuffik, show %u: hands kept %d, the twist node carries the bind wrist onto the wrist (%.4f cm), its rotation "
              "kept, the line through the ends %.4f / %.4f cm", show, ok, gapw, worst, wend);
    }
    /* an end node still holding our own line edit (|q| 0.707: a palette the game did not rewrite) gives no stub: that
     * side's forearm on the line, its anchor the wrist */
    memcpy(skin, orig, sizeof skin);
    skin[113].q[0] = 0.5f, skin[113].q[1] = 0.5f, skin[113].q[2] = 0, skin[113].q[3] = 0;
    n = sb_skin_edit(skin, model, RUN3_N, &k, 3, SB_HIDE_CUFF);
    CHECK(skin[113].q[3] == 0 && memcmp(skin[113].q, orig[112].q, sizeof skin[113].q) &&
              !memcmp(skin[91].q, orig[90].q, sizeof skin[91].q),
          "a stale end: no right stub (q %g,%g,%g,%g), the left one kept", skin[113].q[0], skin[113].q[1], skin[113].q[2],
          skin[113].q[3]);
    /* the cuff's ends: half way along the forearm from the wrist (the twist node), the forearm's at the elbow: carried by
     * the hand straight back (run 3's pose and this bind: the stub's length = the bind distance wrist -> end) */
    {
        sv_vec3 wb = bind[112].p, tb = bind[113].p, eb = bind[110].p;
        CHECK(NEAR(vlen3(vsub3(tb, wb)), 0.5f * vlen3(vsub3(eb, wb)), 3.0f), "the twist node half way: %.1f of %.1f cm",
              vlen3(vsub3(tb, wb)), vlen3(vsub3(eb, wb)));
    }
    memcpy(skin, orig, sizeof skin);
    CHECK(sb_skin_edit(skin, model, RUN3_N, &k, 0, SB_HIDE_SHOW) == 0 && !memcmp(skin, orig, sizeof skin), "show: nothing");
    CHECK(sb_skin_edit(skin, model, RUN3_N - 1, &k, 0, SB_HIDE_ZERO) == 0 && !memcmp(skin, orig, sizeof skin),
          "another node count: nothing");
    CHECK(sb_skin_edit(skin, model, RUN3_N, &k, 0, SB_HIDE_N) == 0 && sb_skin_edit(skin, NULL, RUN3_N, &k, 0, 0) == 0 &&
              !memcmp(skin, orig, sizeof skin),
          "a bad mode, no model: nothing");
    sa_node m2[RUN3_N];
    memcpy(m2, model, sizeof m2);
    m2[112].pos[1] = NAN;
    CHECK(sb_skin_edit(skin, m2, RUN3_N, &k, 0, SB_HIDE_ZERO) == 0 && !memcmp(skin, orig, sizeof skin), "a NaN wrist: nothing");
    CHECK(sb_hide_parse("zero") == SB_HIDE_ZERO && sb_hide_parse("nan") == SB_HIDE_NAN && sb_hide_parse("far") == SB_HIDE_FAR &&
              sb_hide_parse("show") == SB_HIDE_SHOW && sb_hide_parse("line") == SB_HIDE_LINE && sb_hide_parse("Zero") == -1 &&
              sb_hide_parse("") == -1 && !strcmp(sb_hide_name(SB_HIDE_LINE), "line") &&
              sb_hide_parse(NULL) == -1 && !strcmp(sb_hide_name(SB_HIDE_NAN), "nan") && !strcmp(sb_hide_name(9), "?") &&
              sb_hide_parse("cuff") == SB_HIDE_CUFF && sb_hide_parse("forearm") == SB_HIDE_FOREARM &&
              sb_hide_parse("cuffik") == SB_HIDE_CUFFIK &&
              !strcmp(sb_hide_name(SB_HIDE_CUFF), "cuff"),
          "the words");
}

static void test_angle(void) {
    /* a calibration whose blade axis b lands on the grip's +z */
    float v[6] = {1, 2, 3, 90, 0, 0};
    sa_xf c = sa_xf_euler(v);
    sv_vec3 b = sv_qrot(sv_qconj(c.q), (sv_vec3){0, 0, 1}); /* the blade axis in the weapon node */
    for (float a = -60; a <= 60; a += 15) {
        sa_xf ca = sb_cal_angle(c, a * DEG);
        sv_vec3 d = sv_qrot(ca.q, b), want = sb_blade_dir((sv_quat){0, 0, 0, 1}, a * DEG);
        sc_blade_cfg bc;
        sc_blade_cfg_default(&bc);
        bc.pitch_rad = a * DEG, bc.back_cm = 0, bc.len_cm = 100;
        sc_grip g = {1, {0, 0, 0}, {0, 0, 0, 1}, 0, {0, 0, 0}, {0, 0, 0}, 1.0};
        sc_rigxf rig = {{0, 0, 0}, {0, 0, 0}, 0, 0};
        sc_blade_hist h = {0, {0, 0, 0}, {0, 0, 0}, 0};
        somvr_blade bl;
        somvr_blade_controller(&bc, &g, &rig, 1.0, &h, &bl);
        sv_vec3 cd = {bl.tip[0] / 100, bl.tip[1] / 100, bl.tip[2] / 100};
        CHECK(vlen3(vsub3(d, want)) < 1e-5 && vlen3(vsub3(cd, want)) < 1e-4 && NEAR(want.y, sinf(a * DEG), 1e-6),
              "angle %g: drawn %.4f,%.4f,%.4f combat %.4f,%.4f,%.4f want %.4f,%.4f,%.4f", a, d.x, d.y, d.z, cd.x, cd.y, cd.z,
              want.x, want.y, want.z);
        CHECK(vlen3(vsub3(ca.p, c.p)) < 1e-5 || a != 0, "angle 0 keeps the offset");
    }
    sa_xf c0 = sb_cal_angle(c, 0);
    CHECK(qang(c0.q, c.q) < 1e-5 && vlen3(vsub3(c0.p, c.p)) < 1e-5, "angle 0 = the calibration");
    /* SOMVR_SWORD_GRIP_CM: the node slid d along the blade at any angle (the combat blade's tip moves the same d) */
    for (float a = -45; a <= 45; a += 45) {
        sa_xf g = sb_cal_angle(sb_cal_grip(c, 8), a * DEG), n = sb_cal_angle(c, a * DEG);
        sv_vec3 dv = vsub3(g.p, n.p), want = sb_blade_dir((sv_quat){0, 0, 0, 1}, a * DEG);
        CHECK(vlen3(vsub3(dv, (sv_vec3){8 * want.x, 8 * want.y, 8 * want.z})) < 1e-4 && qang(g.q, n.q) < 1e-5,
              "grip 8 cm at angle %g: moved %.3f,%.3f,%.3f", a, dv.x, dv.y, dv.z);
    }
    sa_xf g0 = sb_cal_grip(c, 0);
    CHECK(vlen3(vsub3(g0.p, c.p)) < 1e-6 && qang(g0.q, c.q) < 1e-6, "grip 0 = the calibration (build 15)");
}

/* ---- two hands ---- */
static sb_tw_cfg g_tc;
static sb_tw_in tw_in(sv_vec3 pl, sv_vec3 pr, float sq) {
    sb_tw_in in;
    memset(&in, 0, sizeof in);
    in.sword = in.allow = 1;
    in.valid[0] = in.valid[1] = 1;
    in.g[0] = (sa_xf){pl, {0, 0, 0, 1}}, in.g[1] = (sa_xf){pr, {0, 0, 0, 1}};
    in.squeeze_l = sq, in.dt = 1.0f / 90;
    return in;
}

static void test_twohand(void) {
    sb_tw_cfg_default(&g_tc);
    sb_tw_state s;
    sb_tw_out o;
    sv_vec3 pr = {20, -30, 40}, pl_on = {20, -30, 28}, pl_off = {40, -30, 28};
    /* the blade of an identity grip is +z: the hilt is behind the right grip toward -z */
    memset(&s, 0, sizeof s);
    sb_tw_in in = tw_in(pl_on, pr, 1);
    in.sword = 0;
    sb_tw_step(&s, &g_tc, &in, &o);
    CHECK(!s.active && o.w == 0 && !o.ev, "no sword: no grab");
    in = tw_in(pl_on, pr, 0), sb_tw_step(&s, &g_tc, &in, &o);
    in = tw_in(pl_on, pr, 1), in.allow = 0, sb_tw_step(&s, &g_tc, &in, &o);
    CHECK(!s.active && o.w == 0, "the hand busy: no grab");
    memset(&s, 0, sizeof s);
    in = tw_in(pl_off, pr, 1), sb_tw_step(&s, &g_tc, &in, &o);
    CHECK(!s.active && (o.ev & SB_TW_E_MISS) && s.misses == 1, "a squeeze 20 cm off the hilt: a miss");
    in = tw_in(pl_on, pr, 1), sb_tw_step(&s, &g_tc, &in, &o);
    CHECK(!s.active, "a held squeeze moved onto the hilt does not grab (a press does)");
    memset(&s, 0, sizeof s);
    in = tw_in(pl_on, pr, 0.7f), sb_tw_step(&s, &g_tc, &in, &o);
    CHECK(s.active && (o.ev & SB_TW_E_GRAB) && o.w > 0 && o.w < 0.1f, "a press on the hilt grabs (w %.3f)", o.w);
    /* ease in over blend_s, the left hand onto the hilt */
    float last_w = o.w, max_dw = 0;
    for (int i = 0; i < 20; i++) {
        in = tw_in(pl_on, pr, 0.5f), sb_tw_step(&s, &g_tc, &in, &o); /* 0.5 > squeeze_off: held */
        max_dw = fmaxf(max_dw, o.w - last_w), last_w = o.w;
    }
    CHECK(s.active && o.w == 1 && max_dw < 0.2f, "eased in: w %.2f, step at most %.3f", o.w, max_dw);
    sv_vec3 d = sb_blade_dir(o.g[1].q, 0);
    CHECK(vlen3(vsub3(d, (sv_vec3){0, 0, 1})) < 1e-4 && vlen3(vsub3(o.g[1].p, pr)) < 1e-5,
          "left straight behind: the blade stays +z (%.3f,%.3f,%.3f), the right grip in place", d.x, d.y, d.z);
    CHECK(NEAR(o.hold_cm, 12, 1e-3) && vlen3(vsub3(o.g[0].p, pl_on)) < 1e-3, "the left hand on the hilt 12 cm behind: %.2f",
          o.hold_cm);
    /* the left grip moves down and back: the blade turns onto the line from the left grip to the right one */
    sv_vec3 pl2 = {20, -45, 30};
    in = tw_in(pl2, pr, 0.5f), sb_tw_step(&s, &g_tc, &in, &o);
    sv_vec3 want = vsub3(pr, pl2);
    float L = vlen3(want);
    want = (sv_vec3){want.x / L, want.y / L, want.z / L};
    d = sb_blade_dir(o.g[1].q, 0);
    sv_vec3 hd = vsub3(o.g[0].p, pr);
    float hl = vlen3(hd);
    CHECK(vlen3(vsub3(d, want)) < 1e-4, "the blade on the two-hand line: %.3f,%.3f,%.3f want %.3f,%.3f,%.3f", d.x, d.y, d.z,
          want.x, want.y, want.z);
    CHECK(NEAR(hl, L, 1e-2) && vdot3(hd, d) < -0.999f * hl && NEAR(o.hold_cm, L, 1e-3), "the left hand on the line %.2f cm behind",
          hl);
    sv_vec3 fl = sb_blade_dir(o.g[0].q, 0);
    CHECK(vlen3(vsub3(fl, d)) < 1e-4, "the left fist along the blade");
    /* the roll of the right grip kept: its x axis stays normal to the blade, its twist about it unchanged */
    sv_quat q0 = {0, 0, sinf(0.3f), cosf(0.3f)}; /* rolled 34 deg about the blade */
    in = tw_in(pl2, pr, 0.5f), in.g[1].q = q0, sb_tw_step(&s, &g_tc, &in, &o);
    d = sb_blade_dir(o.g[1].q, 0);
    CHECK(vlen3(vsub3(d, want)) < 1e-4, "rolled: the blade still on the line");
    /* with a sword angle: the blade line (0, sin a, cos a) of the right grip on the two-hand line */
    in = tw_in(pl2, pr, 0.5f), in.angle_rad = 30 * DEG, sb_tw_step(&s, &g_tc, &in, &o);
    d = sb_blade_dir(o.g[1].q, 30 * DEG);
    CHECK(vlen3(vsub3(d, want)) < 1e-4, "angle 30: the blade on the line (%.3f,%.3f,%.3f)", d.x, d.y, d.z);
    /* far apart along the line: clamped to the hilt */
    sv_vec3 pl3 = {20, -30, 5};
    in = tw_in(pl3, pr, 0.5f), sb_tw_step(&s, &g_tc, &in, &o);
    CHECK(s.active && NEAR(o.hold_cm, g_tc.hold_hi_cm, 1e-4) && NEAR(vlen3(vsub3(o.g[0].p, pr)), g_tc.hold_hi_cm, 1e-2),
          "35 cm apart: the left hand held at %.1f cm", o.hold_cm);
    /* together: the last direction */
    in = tw_in((sv_vec3){20, -45, 30}, pr, 0.5f), sb_tw_step(&s, &g_tc, &in, &o);
    sv_vec3 dl = sb_blade_dir(o.g[1].q, 0);
    in = tw_in((sv_vec3){20.5f, -30.5f, 39}, pr, 0.5f), sb_tw_step(&s, &g_tc, &in, &o);
    d = sb_blade_dir(o.g[1].q, 0);
    CHECK(vlen3(vsub3(d, dl)) < 1e-4 && NEAR(o.hold_cm, g_tc.hold_lo_cm, 1e-4), "the hands 1.2 cm apart: the last direction kept");
    /* release: eased back to the right hand's own blade */
    in = tw_in((sv_vec3){20, -45, 30}, pr, 0.5f), sb_tw_step(&s, &g_tc, &in, &o);
    in = tw_in((sv_vec3){20, -45, 30}, pr, 0.2f), sb_tw_step(&s, &g_tc, &in, &o);
    CHECK(!s.active && (o.ev & SB_TW_E_RELEASE) && o.w > 0.8f, "let go: released, easing out (w %.2f)", o.w);
    float prev = vdot3(sb_blade_dir(o.g[1].q, 0), (sv_vec3){0, 0, 1}), max_jump = 0;
    for (int i = 0; i < 20; i++) {
        in = tw_in((sv_vec3){20, -45, 30}, pr, 0), sb_tw_step(&s, &g_tc, &in, &o);
        float c = vdot3(sb_blade_dir(o.g[1].q, 0), (sv_vec3){0, 0, 1});
        max_jump = fmaxf(max_jump, fabsf(acosf(fminf(1, c)) - acosf(fminf(1, prev)))), prev = c;
    }
    CHECK(o.w == 0 && qang(o.g[1].q, (sv_quat){0, 0, 0, 1}) < 1e-5 && vlen3(vsub3(o.g[0].p, (sv_vec3){20, -45, 30})) < 1e-5 &&
              max_jump < 0.2f,
          "eased out: the grips back, the blade at most %.3f rad per frame", max_jump);
    /* pulled apart */
    memset(&s, 0, sizeof s);
    in = tw_in(pl_on, pr, 1), sb_tw_step(&s, &g_tc, &in, &o);
    in = tw_in((sv_vec3){20, -30, -10}, pr, 1), sb_tw_step(&s, &g_tc, &in, &o);
    CHECK(!s.active && (o.ev & SB_TW_E_BREAK) && s.breaks == 1, "50 cm apart: one hand again");
    /* a lost hand, the sword put away */
    memset(&s, 0, sizeof s);
    in = tw_in(pl_on, pr, 1), sb_tw_step(&s, &g_tc, &in, &o);
    in = tw_in(pl_on, pr, 1), in.valid[0] = 0, sb_tw_step(&s, &g_tc, &in, &o);
    CHECK(!s.active && (o.ev & SB_TW_E_LOST) && o.w == 0 && !memcmp(&o.g[1], &in.g[1], sizeof o.g[1]),
          "the left hand lost: one hand, the grips as given");
    memset(&s, 0, sizeof s);
    in = tw_in(pl_on, pr, 1), sb_tw_step(&s, &g_tc, &in, &o);
    in = tw_in(pl_on, pr, 1), in.sword = 0, sb_tw_step(&s, &g_tc, &in, &o);
    CHECK(!s.active && (o.ev & SB_TW_E_LOST), "the sword put away: one hand");
    /* NaN in, nothing out of it */
    memset(&s, 0, sizeof s);
    in = tw_in(pl_on, pr, 1), sb_tw_step(&s, &g_tc, &in, &o);
    in = tw_in((sv_vec3){NAN, 0, 0}, pr, 1), sb_tw_step(&s, &g_tc, &in, &o);
    CHECK(!s.active && isfinite(o.g[1].q.w) && isfinite(o.g[1].p.x), "a NaN grip: released, finite output");
    /* a circle with the left hand at 90 Hz: the right grip turns smoothly */
    memset(&s, 0, sizeof s);
    in = tw_in(pl_on, pr, 1), sb_tw_step(&s, &g_tc, &in, &o);
    sv_quat lq = o.g[1].q;
    float maxa = 0;
    for (int i = 1; i < 400; i++) {
        float t = i / 90.0f;
        sv_vec3 p = {20 + 8 * sinf(2 * t), -30 + 8 * cosf(2 * t) - 8, 28};
        in = tw_in(p, pr, 1), sb_tw_step(&s, &g_tc, &in, &o);
        if (i > 15) maxa = fmaxf(maxa, qang(o.g[1].q, lq));
        lq = o.g[1].q;
    }
    CHECK(s.active && maxa < 0.05f, "circling: at most %.4f rad per frame", maxa);
}

int main(void) {
    test_keep();
    test_skin();
    test_angle();
    test_twohand();
    printf("test_body: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
