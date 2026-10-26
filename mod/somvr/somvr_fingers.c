/* somvr_fingers.c - see somvr_fingers.h */
#include "somvr_fingers.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* the right hand's idle (run 3, logs/boot-check-20261019-190441 nodes-2.tsv: Talion standing, the sword sheathed):
 * each joint's rotation in its parent, thumb .. pinky, knuckle first */
const fg_pose fg_pose_open = {{
    {{0.250643f, 0.176888f, -0.345742f, 0.886772f}, {-0.074934f, -0.013991f, 0.170039f, 0.982492f},
     {0.017915f, -0.001375f, 0.078079f, 0.996789f}},
    {{-0.683154f, 0.148703f, 0.088397f, 0.709489f}, {0.000049f, 0.000001f, 0.260527f, 0.965464f},
     {-0.000005f, -0.000003f, 0.061816f, 0.998094f}},
    {{-0.720278f, 0.152552f, 0.181565f, 0.651904f}, {0.000000f, -0.000125f, 0.355177f, 0.934807f},
     {0.000002f, 0.000002f, 0.043515f, 0.999058f}},
    {{-0.747657f, 0.149619f, 0.186916f, 0.619436f}, {0.000003f, -0.000127f, 0.418243f, 0.908348f},
     {-0.000002f, -0.000005f, 0.200434f, 0.979715f}},
    {{-0.784165f, 0.163652f, 0.239411f, 0.548635f}, {0.000006f, -0.000057f, 0.482428f, 0.875949f},
     {-0.000001f, 0.000001f, 0.169675f, 0.985508f}},
}};

/* the closed fist: the knuckles turned on about their own +Z (the idle's curl axis) by 70..85 deg, the middle joints
 * 100 deg and the tips 65 deg about +Z, the thumb's joints 15/35/45 deg more */
const fg_pose fg_pose_fist = {{
    {{0.271588f, 0.142659f, -0.227037f, 0.924314f}, {-0.075673f, 0.009190f, 0.457610f, 0.885888f},
     {0.016025f, -0.008126f, 0.453591f, 0.891033f}},
    {{-0.474314f, 0.513651f, 0.479357f, 0.530477f}, {0.000000f, 0.000000f, 0.766044f, 0.642788f},
     {0.000000f, 0.000000f, 0.537300f, 0.843391f}},
    {{-0.478567f, 0.559505f, 0.540899f, 0.406660f}, {0.000000f, 0.000000f, 0.766044f, 0.642788f},
     {0.000000f, 0.000000f, 0.537300f, 0.843391f}},
    {{-0.476565f, 0.595200f, 0.541352f, 0.354368f}, {0.000000f, 0.000000f, 0.766044f, 0.642788f},
     {0.000000f, 0.000000f, 0.537300f, 0.843391f}},
    {{-0.467586f, 0.650431f, 0.547165f, 0.242753f}, {0.000000f, 0.000000f, 0.766044f, 0.642788f},
     {0.000000f, 0.000000f, 0.537300f, 0.843391f}},
}};

static const char *const g_fname[FG_NF] = {"thumb", "index", "middle", "ring", "pinky"};
const char *fg_finger_name(int finger) { return finger >= 0 && finger < FG_NF ? g_fname[finger] : "?"; }

void fg_cfg_default(fg_cfg *c) {
    memset(c, 0, sizeof *c);
    c->tau_s = 0.03f;
    c->blend_s = 0.1f;
    c->index_touch = 0.2f;
    c->thumb_rest = 0.6f;
}

static float clamp01(float v) { return v > 0 ? (v < 1 ? v : 1) : 0; } /* NaN: 0 */

static float qdot4(sv_quat a, sv_quat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
static int qfin(sv_quat q) { return isfinite(q.x) && isfinite(q.y) && isfinite(q.z) && isfinite(q.w); }
/* a node the game could have written: finite, not absurdly far, a quaternion near unit length */
static int node_ok(const sa_node *n) {
    float qq = n->q[0] * n->q[0] + n->q[1] * n->q[1] + n->q[2] * n->q[2] + n->q[3] * n->q[3];
    return isfinite(n->pos[0]) && isfinite(n->pos[1]) && isfinite(n->pos[2]) && fabsf(n->pos[0]) < 1e7f &&
           fabsf(n->pos[1]) < 1e7f && fabsf(n->pos[2]) < 1e7f && qq > 0.25f && qq < 4.0f;
}

static sv_quat slerp(sv_quat a, sv_quat b, float t) {
    float d = qdot4(a, b);
    if (d < 0) b = (sv_quat){-b.x, -b.y, -b.z, -b.w}, d = -d;
    if (d > 0.9995f)
        return sv_qnormalize((sv_quat){a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                                       a.w + (b.w - a.w) * t});
    float th = acosf(d), s = sinf(th), ka = sinf((1 - t) * th) / s, kb = sinf(t * th) / s;
    return sv_qnormalize((sv_quat){ka * a.x + kb * b.x, ka * a.y + kb * b.y, ka * a.z + kb * b.z, ka * a.w + kb * b.w});
}

int fg_hand_build(fg_hand *f, int side, int hand, const uint8_t *parent, const uint32_t *hash, int n, char *why,
                  size_t cap) {
    static const uint32_t H[2][FG_NF] = {
        {FG_H_L_THUMB1, FG_H_L_INDEX1, FG_H_L_MIDDLE1, FG_H_L_RING1, FG_H_L_PINKY1},
        {FG_H_R_THUMB1, FG_H_R_INDEX1, FG_H_R_MIDDLE1, FG_H_R_RING1, FG_H_R_PINKY1},
    };
    memset(f, 0, sizeof *f);
    f->side = side, f->hand = -1;
    for (int a = 0; a < FG_NF; a++)
        for (int k = 0; k < FG_NJ; k++) f->j[a][k] = -1;
    if (side != SA_L && side != SA_R) return snprintf(why, cap, "bad side %d", side), -1;
    if (!parent || !hash || n < 2 || n > SA_MAXN || hand < 1 || hand >= n)
        return snprintf(why, cap, "bad skeleton (%d nodes, hand %d)", n, hand), -1;
    const char *sn = side == SA_R ? "R" : "L";
    for (int a = 0; a < FG_NF; a++)
        for (int k = 0; k < FG_NJ; k++) {
            uint32_t h = H[side][a] + (uint32_t)k;
            int at = -1;
            for (int i = 0; i < n; i++)
                if (hash[i] == h) {
                    if (at >= 0) return snprintf(why, cap, "two nodes hash like %s %s %d", sn, g_fname[a], k + 1), -1;
                    at = i;
                }
            if (at < 0) return snprintf(why, cap, "no %s %s joint %d (%#x)", sn, g_fname[a], k + 1, h), -1;
            int want = k ? f->j[a][k - 1] : hand;
            if (parent[at] != want)
                return snprintf(why, cap, "%s %s joint %d (node %d) hangs below %u, not %d", sn, g_fname[a], k + 1, at,
                                parent[at], want), -1;
            f->j[a][k] = at;
        }
    for (int i = 1; i < n; i++) /* a finger joint's only child is the next joint */
        for (int a = 0; a < FG_NF; a++)
            for (int k = 0; k < FG_NJ; k++)
                if (parent[i] == f->j[a][k] && (k == FG_NJ - 1 || i != f->j[a][k + 1]))
                    return snprintf(why, cap, "node %d hangs below %s %s joint %d: not supported", i, sn, g_fname[a],
                                    k + 1), -1;
    f->hand = hand;
    snprintf(why, cap, "%s hand %d: thumb %d/%d/%d index %d/%d/%d middle %d/%d/%d ring %d/%d/%d pinky %d/%d/%d", sn, hand,
             f->j[0][0], f->j[0][1], f->j[0][2], f->j[1][0], f->j[1][1], f->j[1][2], f->j[2][0], f->j[2][1], f->j[2][2],
             f->j[3][0], f->j[3][1], f->j[3][2], f->j[4][0], f->j[4][1], f->j[4][2]);
    return 0;
}

void fg_targets(const fg_cfg *c, const fg_in *in, float t[FG_NF]) {
    if (in->hold == FG_HOLD_FIST) {
        for (int a = 0; a < FG_NF; a++) t[a] = 1;
        return;
    }
    float sq = clamp01(in->squeeze), ix = clamp01(in->trigger);
    if (in->trigger_touch && ix < c->index_touch) ix = clamp01(c->index_touch);
    t[FG_MIDDLE] = t[FG_RING] = t[FG_PINKY] = sq;
    t[FG_INDEX] = ix;
    if (!in->touch_ok) t[FG_THUMB] = sq;
    else if (!in->thumb_touch) t[FG_THUMB] = 0;
    else {
        float r = clamp01(c->thumb_rest), m = sq > ix ? sq : ix;
        t[FG_THUMB] = r + (1 - r) * m;
    }
}

float fg_step(fg_state *s, const fg_cfg *c, const fg_in *in, float dt) {
    if (!(dt > 0)) dt = 0;
    if (dt > 0.1f) dt = 0.1f;
    fg_targets(c, in, s->t);
    int game = in->hold == FG_HOLD_GAME;
    if (!s->have) {
        memcpy(s->c, s->t, sizeof s->c);
        s->ramp = game ? 0 : 1;
        s->have = 1;
    } else {
        float k = c->tau_s > 1e-4f ? 1.0f - expf(-dt / c->tau_s) : 1.0f;
        for (int a = 0; a < FG_NF; a++) {
            s->c[a] += (s->t[a] - s->c[a]) * k;
            if (!isfinite(s->c[a])) s->c[a] = s->t[a];
            s->c[a] = clamp01(s->c[a]);
        }
        float kb = c->blend_s > 1e-3f ? dt / c->blend_s : 1.0f;
        float r = s->ramp + (game ? -kb : kb);
        s->ramp = clamp01(r);
    }
    return s->ramp * s->ramp * (3.0f - 2.0f * s->ramp);
}

int fg_local(const sa_node *pose, const fg_hand *f, int finger, int k, sa_xf *out) {
    if (!pose || !f || f->hand < 0 || finger < 0 || finger >= FG_NF || k < 0 || k >= FG_NJ) return 0;
    int p = k ? f->j[finger][k - 1] : f->hand, j = f->j[finger][k];
    sa_xf P = sa_node_xf(&pose[p]), J = sa_node_xf(&pose[j]);
    P.q = sv_qnormalize(P.q), J.q = sv_qnormalize(J.q);
    *out = sa_xf_mul(sa_xf_inv(P), J);
    return qfin(out->q) && isfinite(out->p.x) && isfinite(out->p.y) && isfinite(out->p.z);
}

int fg_apply(sa_node *pose, int n, const fg_hand *f, const fg_pose *open, const fg_pose *fist, const float curl[FG_NF],
             float w) {
    if (!pose || !f || !open || !fist || !curl || f->hand < 0 || f->hand >= n || !(w > 0)) return 0;
    if (w > 1) w = 1;
    if (!node_ok(&pose[f->hand])) return 0;
    sa_xf loc[FG_NF][FG_NJ];
    for (int a = 0; a < FG_NF; a++)
        for (int k = 0; k < FG_NJ; k++)
            if (f->j[a][k] < 0 || f->j[a][k] >= n || !node_ok(&pose[f->j[a][k]]) || !fg_local(pose, f, a, k, &loc[a][k]))
                return 0;
    sa_xf H = sa_node_xf(&pose[f->hand]);
    H.q = sv_qnormalize(H.q);
    for (int a = 0; a < FG_NF; a++) {
        float c = clamp01(curl[a]);
        sa_xf P = H;
        for (int k = 0; k < FG_NJ; k++) {
            sv_quat tq = slerp(open->q[a][k], fist->q[a][k], c);
            sa_xf L = {loc[a][k].p, slerp(sv_qnormalize(loc[a][k].q), tq, w)};
            sa_xf J = sa_xf_mul(P, L);
            sa_node_set(&pose[f->j[a][k]], J);
            P = J;
        }
    }
    return 1;
}
