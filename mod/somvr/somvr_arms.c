/* somvr_arms.c - see somvr_arms.h */
#include "somvr_arms.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define SA_PI 3.14159265358979323846f
#define SA_DEG 0.01745329252f

/* ---- vectors and quaternions --------------------------------------------------------------------------------------- */

static sv_vec3 v3(float x, float y, float z) { return (sv_vec3){x, y, z}; }
static sv_vec3 vadd(sv_vec3 a, sv_vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static sv_vec3 vsub(sv_vec3 a, sv_vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static sv_vec3 vmul(sv_vec3 a, float k) { return v3(a.x * k, a.y * k, a.z * k); }
static float vdot(sv_vec3 a, sv_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static sv_vec3 vcross(sv_vec3 a, sv_vec3 b) {
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static float vlen(sv_vec3 a) { return sqrtf(vdot(a, a)); }
static sv_vec3 vnorm(sv_vec3 a) {
    float l = vlen(a);
    return l > 1e-12f ? vmul(a, 1.0f / l) : v3(0, 0, 0);
}
static sv_vec3 vperp(sv_vec3 a, sv_vec3 unit) { return vsub(a, vmul(unit, vdot(a, unit))); } /* a without its part along unit */
static int vfinite(sv_vec3 a) { return isfinite(a.x) && isfinite(a.y) && isfinite(a.z); }
static int qfinite(sv_quat q) { return isfinite(q.x) && isfinite(q.y) && isfinite(q.z) && isfinite(q.w); }
static float qdot(sv_quat a, sv_quat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

static sv_quat qaxis(sv_vec3 unit, float ang) {
    float s = sinf(0.5f * ang);
    return (sv_quat){unit.x * s, unit.y * s, unit.z * s, cosf(0.5f * ang)};
}

/* the shortest rotation taking the unit vector u to the unit vector v (antiparallel: 180 deg about an axis normal to u) */
static sv_quat qarc(sv_vec3 u, sv_vec3 v) {
    float c = vdot(u, v);
    if (c < -0.999999f) {
        sv_vec3 a = vcross(u, fabsf(u.x) < 0.9f ? v3(1, 0, 0) : v3(0, 1, 0));
        a = vnorm(a);
        return (sv_quat){a.x, a.y, a.z, 0};
    }
    sv_vec3 a = vcross(u, v);
    return sv_qnormalize((sv_quat){a.x, a.y, a.z, 1.0f + c});
}

static sv_quat qslerp(sv_quat a, sv_quat b, float t) {
    float d = qdot(a, b);
    if (d < 0) b = (sv_quat){-b.x, -b.y, -b.z, -b.w}, d = -d;
    if (d > 0.9995f) /* nearly the same: a normalised lerp */
        return sv_qnormalize((sv_quat){a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                                       a.w + (b.w - a.w) * t});
    float th = acosf(d), s = sinf(th), ka = sinf((1 - t) * th) / s, kb = sinf(t * th) / s;
    return sv_qnormalize((sv_quat){ka * a.x + kb * b.x, ka * a.y + kb * b.y, ka * a.z + kb * b.z, ka * a.w + kb * b.w});
}

/* the twist of the rotation q about the unit axis a (swing-twist, q = swing (x) twist), in (-pi, pi] */
static float qtwist(sv_quat q, sv_vec3 a) {
    float p = q.x * a.x + q.y * a.y + q.z * a.z;
    if (fabsf(p) < 1e-9f && fabsf(q.w) < 1e-9f) return 0; /* a 180 deg swing: no twist defined */
    return sv_wrap_pi(2.0f * atan2f(p, q.w));
}

/* the signed angle from the unit vector a to the unit vector b about the unit axis n (both normal to n) */
static float vangle_about(sv_vec3 a, sv_vec3 b, sv_vec3 n) { return atan2f(vdot(n, vcross(a, b)), vdot(a, b)); }

sa_xf sa_xf_mul(sa_xf a, sa_xf b) { return (sa_xf){vadd(a.p, sv_qrot(a.q, b.p)), sv_qnormalize(sv_qmul(a.q, b.q))}; }

sa_xf sa_xf_inv(sa_xf a) {
    sv_quat c = sv_qconj(sv_qnormalize(a.q));
    return (sa_xf){vmul(sv_qrot(c, a.p), -1.0f), c};
}

sa_xf sa_node_xf(const sa_node *n) { return (sa_xf){v3(n->pos[0], n->pos[1], n->pos[2]), sv_quat_of(n->q)}; }

void sa_node_set(sa_node *n, sa_xf x) {
    n->pos[0] = x.p.x, n->pos[1] = x.p.y, n->pos[2] = x.p.z, n->pad = 0;
    sv_quat_to(sv_qnormalize(x.q), n->q);
}

sa_xf sa_xf_euler(const float v[6]) {
    sv_quat q = sv_qmul(sv_qmul(qaxis(v3(1, 0, 0), v[3] * SA_DEG), qaxis(v3(0, 1, 0), v[4] * SA_DEG)),
                        qaxis(v3(0, 0, 1), v[5] * SA_DEG));
    return (sa_xf){v3(v[0], v[1], v[2]), sv_qnormalize(q)};
}

/* ---- the chains ---------------------------------------------------------------------------------------------------- */

static int find_hash(const uint32_t *hash, int n, uint32_t h, int *dup) {
    int at = -1;
    for (int i = 0; i < n; i++)
        if (hash[i] == h) {
            if (at >= 0) *dup = 1;
            else at = i;
        }
    return at;
}

/* the nodes below j (parents precede children: one forward pass); -1 = more than cap */
static int subtree(const uint8_t *parent, int n, int j, int16_t *out, int cap) {
    uint8_t in[SA_MAXN];
    int k = 0;
    memset(in, 0, sizeof in);
    in[j] = 1;
    for (int i = j + 1; i < n; i++) {
        if (!in[parent[i]]) continue;
        in[i] = 1;
        if (k >= cap) return -1;
        out[k++] = (int16_t)i;
    }
    return k;
}

static int in_list(const int16_t *l, int n, int j) {
    for (int i = 0; i < n; i++)
        if (l[i] == j) return 1;
    return 0;
}

int sa_chain_build(sa_chain *c, int side, const uint8_t *parent, const uint32_t *hash, int n, char *why, size_t cap) {
    static const uint32_t H[2][6] = {
        {SA_H_L_ARMU, SA_H_L_ARML, SA_H_L_HAND, SA_H_L_BICEP, SA_H_L_FOREARM, SA_H_L_WEAPON},
        {SA_H_R_ARMU, SA_H_R_ARML, SA_H_R_HAND, SA_H_R_BICEP, SA_H_R_FOREARM, SA_H_R_WEAPON},
    };
    static const char *const nm[2][6] = {{"L_Armu", "L_Arml", "L_Hand", "L_Bicep", "L_Forearm", "L_Weapon"},
                                         {"R_Armu", "R_Arml", "R_Hand", "R_Bicep", "R_Forearm", "R_Weapon"}};
    memset(c, 0, sizeof *c);
    c->side = side;
    c->p = c->s = c->e = c->w = c->tu = c->tf = c->wpn = -1;
    if (side != SA_L && side != SA_R) return snprintf(why, cap, "bad side %d", side), -1;
    if (!parent || !hash || n < 2 || n > SA_MAXN) return snprintf(why, cap, "bad skeleton (%d nodes)", n), -1;
    for (int i = 1; i < n; i++)
        if (parent[i] >= i) return snprintf(why, cap, "node %d's parent %u does not precede it", i, parent[i]), -1;
    int idx[6], dup = 0;
    for (int k = 0; k < 6; k++) {
        idx[k] = find_hash(hash, n, H[side][k], &dup);
        if (dup) return snprintf(why, cap, "two nodes hash like %s (%#x)", nm[side][k], H[side][k]), -1;
        if (k < 3 && idx[k] < 1) return snprintf(why, cap, "no %s (%#x) in the skeleton", nm[side][k], H[side][k]), -1;
    }
    c->s = idx[0], c->e = idx[1], c->w = idx[2], c->p = parent[c->s];
    if (parent[c->e] != c->s || parent[c->w] != c->e)
        return snprintf(why, cap, "%s %d -> %s %d -> %s %d is not a parent chain (parents %u, %u)", nm[side][0], c->s,
                        nm[side][1], c->e, nm[side][2], c->w, parent[c->e], parent[c->w]), -1;
    for (int k = 0; k < 3; k++) {
        int r = subtree(parent, n, k == 0 ? c->s : k == 1 ? c->e : c->w, c->sub[k], SA_SUB);
        if (r < 0) return snprintf(why, cap, "more than %d nodes below %s", SA_SUB, nm[side][k]), -1;
        c->nsub[k] = r;
    }
    c->tu = idx[3] >= 0 && parent[idx[3]] == c->s ? idx[3] : -1;
    c->tf = idx[4] >= 0 && parent[idx[4]] == c->e ? idx[4] : -1;
    c->wpn = idx[5] >= 0 && in_list(c->sub[2], c->nsub[2], idx[5]) ? idx[5] : -1;
    int tw[2] = {c->tu, c->tf};
    for (int k = 0; k < 2; k++) {
        c->ntw[k] = 0;
        if (tw[k] < 0) continue;
        int r = subtree(parent, n, tw[k], c->twsub[k], SA_TWSUB);
        if (r < 0) return snprintf(why, cap, "more than %d nodes below %s", SA_TWSUB, nm[side][3 + k]), -1;
        c->ntw[k] = r;
    }
    c->lo = c->s, c->hi = c->s;
    for (int i = 0; i < c->nsub[0]; i++) c->hi = c->sub[0][i] > c->hi ? c->sub[0][i] : c->hi;
    snprintf(why, cap, "%s %d (in %d) -> %s %d -> %s %d, twist %d/%d, weapon %d, %d/%d/%d below", nm[side][0], c->s, c->p,
             nm[side][1], c->e, nm[side][2], c->w, c->tu, c->tf, c->wpn, c->nsub[0], c->nsub[1], c->nsub[2]);
    return 0;
}

/* ---- the solver ---------------------------------------------------------------------------------------------------- */

void sa_cfg_default(sa_cfg *c) {
    memset(c, 0, sizeof *c);
    c->eps_cm = 0.5f, c->reach_cm = 1.0f;
    c->pole[0] = 0.4f, c->pole[1] = -1.0f, c->pole[2] = -0.3f;
    c->pole_anim = 0.25f;
    c->pole_dps = 720.0f;
    c->twist_upper = 0.5f, c->twist_fore = 0.5f;
    c->blend_s = 0.15f;
}

float sa_weight_step(sa_arm_state *st, const sa_cfg *c, int valid, float dt) {
    if (!(dt > 0)) dt = 0;
    if (dt > 0.1f) dt = 0.1f;
    float k = c->blend_s > 1e-3f ? dt / c->blend_s : 1.0f;
    float r = st->ramp + (valid ? k : -k);
    st->ramp = r < 0 ? 0 : r > 1 ? 1 : r;
    if (!(st->ramp > 0)) st->have_m = st->have_tw = 0;
    return st->ramp * st->ramp * (3.0f - 2.0f * st->ramp);
}

sa_xf sa_hand_target(const sa_node *pose, const sa_chain *c, sa_xf t_wpn) {
    if (!pose || !c || c->wpn < 0 || c->w < 0) return t_wpn;
    return sa_hand_target_lw(t_wpn, sa_weapon_local(pose, c));
}

sa_xf sa_weapon_local(const sa_node *pose, const sa_chain *c) {
    if (!pose || !c || c->wpn < 0 || c->w < 0) return (sa_xf){{0, 0, 0}, {0, 0, 0, 1}};
    sa_xf W = sa_node_xf(&pose[c->w]), P = sa_node_xf(&pose[c->wpn]);
    W.q = sv_qnormalize(W.q), P.q = sv_qnormalize(P.q);
    sa_xf lw = sa_xf_mul(sa_xf_inv(W), P);
    lw.q = sv_qnormalize(lw.q);
    return lw;
}

sa_xf sa_hand_target_lw(sa_xf t_wpn, sa_xf lw) { return sa_xf_mul(t_wpn, sa_xf_inv(lw)); }

int sa_weapon_set(sa_node *pose, const sa_chain *c, sa_xf lw) {
    if (!pose || !c || c->wpn < 0 || c->w < 0) return 0;
    sa_xf W = sa_node_xf(&pose[c->w]);
    W.q = sv_qnormalize(W.q);
    sa_node_set(&pose[c->wpn], sa_xf_mul(W, lw));
    return 1;
}

float sa_xf_dist(sa_xf a, sa_xf b, float *deg) {
    float d = vlen(vsub(a.p, b.p));
    if (deg) {
        float c = fabsf(qdot(sv_qnormalize(a.q), sv_qnormalize(b.q)));
        *deg = 2.0f * acosf(c > 1 ? 1 : c) / SA_DEG;
    }
    return d;
}

void sa_lw_reset(sa_lw_hold *h) { memset(h, 0, sizeof *h); }

int sa_lw_step(sa_lw_hold *h, sa_xf lw, int may_capture, int frames) {
    float dg = 0;
    if (!vfinite(lw.p) || !qfinite(lw.q)) {
        h->have_last = 0, h->steady = 0;
        return 0;
    }
    if (h->have_last && sa_xf_dist(lw, h->last, &dg) <= SA_LW_STEADY_CM && dg <= SA_LW_STEADY_DEG)
        h->steady++;
    else
        h->steady = 0;
    h->last = lw, h->have_last = 1;
    if (h->have || !may_capture || h->steady < frames) return 0;
    h->ref = lw, h->have = 1;
    return 1;
}

/* turn node j and the listed nodes below it by d about pivot */
static void turn(sa_node *pose, int j, const int16_t *sub, int nsub, sv_vec3 pivot, sv_quat d) {
    for (int k = -1; k < nsub; k++) {
        sa_node *nd = &pose[k < 0 ? j : sub[k]];
        sv_vec3 p = vadd(pivot, sv_qrot(d, vsub(v3(nd->pos[0], nd->pos[1], nd->pos[2]), pivot)));
        sa_node_set(nd, (sa_xf){p, sv_qmul(d, sv_quat_of(nd->q))});
    }
}

static void shift(sa_node *pose, int j, const int16_t *sub, int nsub, sv_vec3 d) {
    for (int k = -1; k < nsub; k++) {
        sa_node *nd = &pose[k < 0 ? j : sub[k]];
        nd->pos[0] += d.x, nd->pos[1] += d.y, nd->pos[2] += d.z;
    }
}

static int node_ok(const sa_node *n) {
    float qq = n->q[0] * n->q[0] + n->q[1] * n->q[1] + n->q[2] * n->q[2] + n->q[3] * n->q[3];
    return isfinite(n->pos[0]) && isfinite(n->pos[1]) && isfinite(n->pos[2]) && fabsf(n->pos[0]) < 1e7f &&
           fabsf(n->pos[1]) < 1e7f && fabsf(n->pos[2]) < 1e7f && qq > 0.25f && qq < 4.0f;
}

int sa_solve(sa_node *pose, int n, const sa_chain *c, const sa_cfg *cfg, sa_xf T, float w, float dt, sa_arm_state *st,
             sa_report *rep) {
    memset(rep, 0, sizeof *rep);
    rep->weight = w;
    if (!pose || !c || !cfg || !st || c->s < 1 || c->p < 0 || c->e <= c->s || c->w <= c->e || c->hi >= n)
        return rep->why = "no chain", 0;
    if (!(w > 0)) return rep->why = "weight 0", 0;
    if (w > 1) w = 1;
    if (!(dt > 0)) dt = 0;
    if (dt > 0.1f) dt = 0.1f;
    float tq = qdot(T.q, T.q);
    if (!vfinite(T.p) || !qfinite(T.q) || !(tq > 0.25f && tq < 4.0f) || fabsf(T.p.x) > 1e5f || fabsf(T.p.y) > 1e5f ||
        fabsf(T.p.z) > 1e5f)
        return rep->why = "the target is not finite", 0;
    T.q = sv_qnormalize(T.q);
    if (!node_ok(&pose[c->p]) || !node_ok(&pose[c->s]) || !node_ok(&pose[c->e]) || !node_ok(&pose[c->w]))
        return rep->why = "the animated arm is not finite", 0;

    sa_node keep[1 + SA_SUB];
    keep[0] = pose[c->s];
    for (int k = 0; k < c->nsub[0]; k++) keep[1 + k] = pose[c->sub[0][k]];

    sa_xf P = sa_node_xf(&pose[c->p]), S = sa_node_xf(&pose[c->s]), E = sa_node_xf(&pose[c->e]),
          W = sa_node_xf(&pose[c->w]);
    S.q = sv_qnormalize(S.q), E.q = sv_qnormalize(E.q), W.q = sv_qnormalize(W.q), P.q = sv_qnormalize(P.q);
    float a = vlen(vsub(E.p, S.p)), b = vlen(vsub(W.p, E.p));
    if (!(a > 1.0f && b > 1.0f)) return rep->why = "bone lengths under 1 cm", 0;

    /* the reach: S -> the hand target, clamped */
    sv_vec3 d = vsub(T.p, S.p);
    float L0 = vlen(d);
    sv_vec3 dh = L0 > 1e-3f ? vmul(d, 1.0f / L0) : vnorm(vsub(W.p, S.p));
    if (!(vlen(dh) > 0.5f)) dh = v3(0, -1, 0);
    float lo = fabsf(a - b) + cfg->eps_cm, hi = a + b - cfg->eps_cm;
    if (lo > hi) lo = hi = 0.5f * (lo + hi);
    float L = L0;
    rep->clamp = SA_CLAMP_NONE;
    if (L > hi) L = hi, rep->clamp = SA_CLAMP_FAR;
    if (L < lo) L = lo, rep->clamp = SA_CLAMP_NEAR;
    sv_vec3 tc = vadd(S.p, vmul(dh, L));
    rep->reach = L0;

    /* the elbow's direction m (normal to dh): the hint, a share of the animated elbow, rate-limited */
    float sx = c->side == SA_R ? 1.0f : -1.0f;
    sv_vec3 hint = vnorm(v3(sx * cfg->pole[0], cfg->pole[1], cfg->pole[2]));
    if (!(vlen(hint) > 0.5f)) hint = v3(0, -1, 0);
    float ka = cfg->pole_anim < 0 ? 0 : cfg->pole_anim > 1 ? 1 : cfg->pole_anim;
    sv_vec3 hv = vadd(vmul(vperp(hint, dh), 1 - ka), vmul(vperp(vmul(vsub(E.p, S.p), 1.0f / a), dh), ka));
    sv_vec3 mt;
    if (vlen(hv) > 1e-4f) mt = vnorm(hv);
    else if (st->have_m && vlen(vperp(st->m, dh)) > 1e-4f) mt = vnorm(vperp(st->m, dh));
    else mt = vnorm(vperp(fabsf(dh.y) < 0.9f ? v3(0, -1, 0) : v3(0, 0, -1), dh));
    sv_vec3 m = mt;
    if (st->have_m) {
        sv_vec3 mp = vperp(st->m, dh);
        if (vlen(mp) > 1e-4f) {
            mp = vnorm(mp);
            float th = vangle_about(mp, mt, dh), lim = cfg->pole_dps * SA_DEG * dt;
            if (cfg->pole_dps > 0 && fabsf(th) > lim) {
                m = vnorm(vperp(sv_qrot(qaxis(dh, th > 0 ? lim : -lim), mp), dh));
                if (!(vlen(m) > 0.5f)) m = mt;
            }
        }
    }
    st->m = m, st->have_m = 1;

    /* the elbow by the law of cosines */
    float cs = (a * a + L * L - b * b) / (2.0f * a * L);
    cs = cs < -1 ? -1 : cs > 1 ? 1 : cs;
    float sn = sqrtf(fmaxf(0.0f, 1.0f - cs * cs));
    sv_vec3 ep = vadd(S.p, vmul(vadd(vmul(dh, cs), vmul(m, sn)), a));

    /* S: the swing onto the new upper arm, then the roll that makes its hinge the bend axis k = m x dh */
    sv_vec3 u = vmul(vsub(E.p, S.p), 1.0f / a), u2 = vmul(vsub(ep, S.p), 1.0f / a), k = vnorm(vcross(m, dh));
    sv_quat dS = qarc(u, u2);
    sv_vec3 h1 = vperp(sv_qrot(sv_qmul(dS, S.q), v3(SA_HINGE_X, SA_HINGE_Y, SA_HINGE_Z)), u2);
    if (vlen(h1) > 1e-4f) dS = sv_qnormalize(sv_qmul(qaxis(u2, vangle_about(vnorm(h1), k, u2)), dS));
    sv_quat qS1 = sv_qnormalize(sv_qmul(dS, S.q));
    /* E: the forearm onto the target */
    sv_vec3 pE1 = vadd(S.p, sv_qrot(dS, vsub(E.p, S.p))), f1 = sv_qrot(dS, vsub(W.p, E.p)), f2 = vsub(tc, pE1);
    sv_quat dE = vlen(f2) > 1e-4f ? qarc(vnorm(f1), vnorm(f2)) : (sv_quat){0, 0, 0, 1};
    sv_quat qE1 = sv_qnormalize(sv_qmul(dE, sv_qmul(dS, E.q))), qW1 = T.q;

    /* the weight: S in model space, E and W as local rotations (the bones stay rigid in between) */
    sv_quat lE0 = sv_qmul(sv_qconj(S.q), E.q), lE1 = sv_qmul(sv_qconj(qS1), qE1);
    sv_quat lW0 = sv_qmul(sv_qconj(E.q), W.q), lW1 = sv_qmul(sv_qconj(qE1), qW1);
    sv_quat qSw = qslerp(S.q, qS1, w), qEw = sv_qnormalize(sv_qmul(qSw, qslerp(lE0, lE1, w)));
    sv_quat qWw = sv_qnormalize(sv_qmul(qEw, qslerp(lW0, lW1, w)));

    turn(pose, c->s, c->sub[0], c->nsub[0], S.p, sv_qnormalize(sv_qmul(qSw, sv_qconj(S.q))));
    sa_xf E2 = sa_node_xf(&pose[c->e]);
    turn(pose, c->e, c->sub[1], c->nsub[1], E2.p, sv_qnormalize(sv_qmul(qEw, sv_qconj(sv_qnormalize(E2.q)))));
    sa_xf W2 = sa_node_xf(&pose[c->w]);
    turn(pose, c->w, c->sub[2], c->nsub[2], W2.p, sv_qnormalize(sv_qmul(qWw, sv_qconj(sv_qnormalize(W2.q)))));

    /* the twist nodes: the upper arm's keeps twist_upper of S's roll change against the clavicle, the forearm's takes
     * twist_fore of the hand's roll change against the forearm (both measured about the bone, swing-twist) */
    sa_xf S3 = sa_node_xf(&pose[c->s]), E3 = sa_node_xf(&pose[c->e]), W3 = sa_node_xf(&pose[c->w]);
    sv_vec3 aS = vnorm(sv_qrot(sv_qconj(S.q), vsub(E.p, S.p))), aE = vnorm(sv_qrot(sv_qconj(E.q), vsub(W.p, E.p)));
    float raw[2][2] = {{qtwist(sv_qmul(sv_qconj(P.q), S.q), aS), qtwist(sv_qmul(sv_qconj(P.q), sv_qnormalize(S3.q)), aS)},
                       {qtwist(sv_qmul(sv_qconj(E.q), W.q), aE),
                        qtwist(sv_qmul(sv_qconj(sv_qnormalize(E3.q)), sv_qnormalize(W3.q)), aE)}};
    float dtw[2];
    for (int r = 0; r < 2; r++) { /* continuous in time: each roll unwrapped from its last value, the change capped */
        for (int j = 0; j < 2; j++) {
            float v = !st->have_tw ? (j ? raw[r][0] + sv_wrap_pi(raw[r][1] - raw[r][0]) : raw[r][0])
                                   : st->tw[r][j] + sv_wrap_pi(raw[r][j] - st->tw[r][j]);
            st->tw[r][j] = fmaxf(-1e4f, fminf(1e4f, v));
        }
        dtw[r] = fmaxf(-SA_TW_MAX, fminf(SA_TW_MAX, st->tw[r][1] - st->tw[r][0]));
    }
    st->have_tw = 1;
    float tu = dtw[0], tf = dtw[1];
    if (c->tu >= 0 && cfg->twist_upper != 1)
        turn(pose, c->tu, c->twsub[0], c->ntw[0], S3.p, qaxis(vnorm(vsub(E3.p, S3.p)), (cfg->twist_upper - 1) * tu));
    if (c->tf >= 0 && cfg->twist_fore != 0)
        turn(pose, c->tf, c->twsub[1], c->ntw[1], E3.p, qaxis(vnorm(vsub(W3.p, E3.p)), cfg->twist_fore * tf));

    int ok = node_ok(&pose[c->s]);
    for (int i = 0; ok && i < c->nsub[0]; i++) ok = node_ok(&pose[c->sub[0][i]]);
    if (!ok) { /* never leave a broken arm: back to the animation's */
        pose[c->s] = keep[0];
        for (int i = 0; i < c->nsub[0]; i++) pose[c->sub[0][i]] = keep[1 + i];
        st->have_m = st->have_tw = 0;
        return rep->why = "NaN in the result (the pose is left as it was)", 0;
    }
    sv_vec3 pw = v3(pose[c->w].pos[0], pose[c->w].pos[1], pose[c->w].pos[2]);
    sv_vec3 uf = vnorm(vsub(E3.p, S3.p)), ff = vnorm(vsub(pw, E3.p));
    if (cfg->stretch && rep->clamp != SA_CLAMP_NONE) { /* the body unseen: the hand onto its target, the forearm long */
        sv_vec3 dw = vmul(vsub(T.p, pw), w);
        shift(pose, c->w, c->sub[2], c->nsub[2], dw);
        pw = vadd(pw, dw);
        rep->stretch_cm = vlen(dw);
    }
    rep->solved = 1;
    rep->residual = vlen(vsub(T.p, pw));
    rep->reached = rep->residual < cfg->reach_cm;
    rep->bend_deg = acosf(fmaxf(-1.0f, fminf(1.0f, vdot(uf, ff)))) / SA_DEG;
    rep->twist_u_deg = tu / SA_DEG, rep->twist_f_deg = tf / SA_DEG;
    rep->elbow[0] = E3.p.x, rep->elbow[1] = E3.p.y, rep->elbow[2] = E3.p.z;
    rep->hand[0] = pw.x, rep->hand[1] = pw.y, rep->hand[2] = pw.z;
    return 1;
}

/* ---- the grip <-> model space --------------------------------------------------------------------------------------- */

sa_xf sa_grip_to_model(const sc_rigxf *rig, sv_vec3 shift, sa_xf X, sa_xf grip) {
    sv_quat y = sv_qyaw(rig->yaw);
    float k = sc_rig_scale(rig);
    sv_vec3 d = sv_qrot(y, v3(k * (grip.p.x - rig->p0[0]), k * (grip.p.y - rig->p0[1]), k * (grip.p.z - rig->p0[2])));
    sa_xf world = {v3(rig->base[0] + shift.x + d.x, rig->base[1] + shift.y + d.y, rig->base[2] + shift.z + d.z),
                   sv_qnormalize(sv_qmul(y, grip.q))};
    return sa_xf_mul(sa_xf_inv(X), world);
}

sa_xf sa_world_to_track(const sc_rigxf *rig, sa_xf w) {
    sv_quat y = sv_qyaw(-rig->yaw);
    float k = 1.0f / sc_rig_scale(rig);
    sv_vec3 d = sv_qrot(y, v3(k * (w.p.x - rig->base[0]), k * (w.p.y - rig->base[1]), k * (w.p.z - rig->base[2])));
    return (sa_xf){v3(rig->p0[0] + d.x, rig->p0[1] + d.y, rig->p0[2] + d.z), sv_qnormalize(sv_qmul(y, w.q))};
}

sa_xf sa_bone_grip(const sc_rigxf *rig, sa_xf X, sa_xf wpn_model, sa_xf cal) {
    return sa_world_to_track(rig, sa_xf_mul(sa_xf_mul(X, wpn_model), sa_xf_inv(cal)));
}
