/* somvr_vrhands.c - our own VR hands, the pure part (somvr_vrhands.h 2; docs/re-xr-pipeline.md 33). No globals. */
#include "somvr_vrhands.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "somvr_handmesh.h"
#include "somvr_input.h" /* SOMVR_HAND_L / _R */

_Static_assert(SOMVR_HM_NJ == SOMVR_VH_NJ, "the mesh's joints");
_Static_assert(SOMVR_HM_NV <= 1400 && SOMVR_HM_NI <= 7000, "the model's per-vertex arrays");

#define DEG 0.01745329252f
/* each finger's first joint and its three bending joints (the thumb bends at its metacarpal too) */
static const int g_base[SOMVR_VH_NF] = {SOMVR_VHJ_THUMB, SOMVR_VHJ_INDEX, SOMVR_VHJ_MIDDLE, SOMVR_VHJ_RING,
                                        SOMVR_VHJ_PINKY};
static int bend_joint(int f, int k) { return f == 0 ? g_base[0] + k : g_base[f] + 1 + k; }
static int tip_joint(int f) { return f == 0 ? g_base[0] + 3 : g_base[f] + 4; }
static int parent_of(int j) { return j == SOMVR_VHJ_WRIST ? -1 : (j == 1 || j == 5 || j == 10 || j == 15 || j == 20) ? 0 : j - 1; }

/* ---- small math: quaternions (x y z w, Hamilton), 3x4 row-major transforms ---------------------------------------- */
static void qmul(const float a[4], const float b[4], float o[4]) {
    float r[4] = {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
                  a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
    memcpy(o, r, sizeof r);
}
static void qrot(const float q[4], const float v[3], float o[3]) {
    float t[3] = {2 * (q[1] * v[2] - q[2] * v[1]), 2 * (q[2] * v[0] - q[0] * v[2]), 2 * (q[0] * v[1] - q[1] * v[0])};
    float r[3] = {v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]), v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]),
                  v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0])};
    memcpy(o, r, sizeof r);
}
static void qconj(const float q[4], float o[4]) { o[0] = -q[0], o[1] = -q[1], o[2] = -q[2], o[3] = q[3]; }
static void qaxis(float ax, float ay, float az, float a, float o[4]) {
    float s = sinf(0.5f * a);
    o[0] = ax * s, o[1] = ay * s, o[2] = az * s, o[3] = cosf(0.5f * a);
}
static void qnorm(float q[4]) {
    float l = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int i = 0; i < 4; i++) q[i] = l > 0 ? q[i] / l : (i == 3);
}
static float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void cross3(const float a[3], const float b[3], float o[3]) {
    float r[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    memcpy(o, r, sizeof r);
}
static float norm3(float v[3]) {
    float l = sqrtf(dot3(v, v));
    if (l > 0)
        for (int i = 0; i < 3; i++) v[i] /= l;
    return l;
}
/* the quaternion of the rotation whose columns are x, y, z (orthonormal, right-handed) */
static void quat_of_basis(const float x[3], const float y[3], const float z[3], float q[4]) {
    float t = x[0] + y[1] + z[2];
    if (t > 0) {
        float s = 0.5f / sqrtf(t + 1);
        q[3] = 0.25f / s, q[0] = (y[2] - z[1]) * s, q[1] = (z[0] - x[2]) * s, q[2] = (x[1] - y[0]) * s;
    } else if (x[0] > y[1] && x[0] > z[2]) {
        float s = 2 * sqrtf(1 + x[0] - y[1] - z[2]);
        q[3] = (y[2] - z[1]) / s, q[0] = 0.25f * s, q[1] = (y[0] + x[1]) / s, q[2] = (z[0] + x[2]) / s;
    } else if (y[1] > z[2]) {
        float s = 2 * sqrtf(1 + y[1] - x[0] - z[2]);
        q[3] = (z[0] - x[2]) / s, q[0] = (y[0] + x[1]) / s, q[1] = 0.25f * s, q[2] = (z[1] + y[2]) / s;
    } else {
        float s = 2 * sqrtf(1 + z[2] - x[0] - y[1]);
        q[3] = (x[1] - y[0]) / s, q[0] = (z[0] + x[2]) / s, q[1] = (z[1] + y[2]) / s, q[2] = 0.25f * s;
    }
    qnorm(q);
}
/* a transform {q, t} (7 floats) as 3x4 row-major */
static void xf_mat(const float x[7], float m[12]) {
    const float *q = x;
    float xx = q[0] * q[0], yy = q[1] * q[1], zz = q[2] * q[2], xy = q[0] * q[1], xz = q[0] * q[2], yz = q[1] * q[2],
          wx = q[3] * q[0], wy = q[3] * q[1], wz = q[3] * q[2];
    m[0] = 1 - 2 * (yy + zz), m[1] = 2 * (xy - wz), m[2] = 2 * (xz + wy), m[3] = x[4];
    m[4] = 2 * (xy + wz), m[5] = 1 - 2 * (xx + zz), m[6] = 2 * (yz - wx), m[7] = x[5];
    m[8] = 2 * (xz - wy), m[9] = 2 * (yz + wx), m[10] = 1 - 2 * (xx + yy), m[11] = x[6];
}
static void xf_mul(const float a[7], const float b[7], float o[7]) { /* a then b in a's frame: a (x) b */
    float t[3], r[7];
    qrot(a, b + 4, t);
    qmul(a, b, r);
    for (int i = 0; i < 3; i++) r[4 + i] = a[4 + i] + t[i];
    qnorm(r);
    memcpy(o, r, sizeof r);
}
static void xf_inv(const float a[7], float o[7]) {
    float q[4], t[3];
    qconj(a, q);
    qrot(q, a + 4, t);
    memcpy(o, q, sizeof q);
    for (int i = 0; i < 3; i++) o[4 + i] = -t[i];
}
static void m34_mul(const float a[12], const float b[12], float o[12]) {
    float r[12];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++)
            r[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j] + (j == 3 ? a[i * 4 + 3] : 0);
    }
    memcpy(o, r, sizeof r);
}
static void m34_point(const float m[12], const float p[3], float o[3]) {
    float r[3];
    for (int i = 0; i < 3; i++) r[i] = m[i * 4] * p[0] + m[i * 4 + 1] * p[1] + m[i * 4 + 2] * p[2] + m[i * 4 + 3];
    memcpy(o, r, sizeof r);
}
static void m34_vec(const float m[12], const float v[3], float o[3]) {
    float r[3];
    for (int i = 0; i < 3; i++) r[i] = m[i * 4] * v[0] + m[i * 4 + 1] * v[1] + m[i * 4 + 2] * v[2];
    memcpy(o, r, sizeof r);
}
static float smooth01(float a, float b, float x) {
    float t = b != a ? (x - a) / (b - a) : (x >= b);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return t * t * (3 - 2 * t);
}

/* ---- the configuration ----------------------------------------------------------------------------------------- */
void somvr_vh_cfg_default(somvr_vh_cfg *c) {
    /* deg per joint, knuckle first: a relaxed open hand (never a flat board) and a fist round a 3.4 cm grip */
    static const float open[SOMVR_VH_NF][3] = {{4, 6, 6}, {6, 8, 5}, {8, 11, 6}, {10, 13, 7}, {12, 15, 8}};
    static const float fist[SOMVR_VH_NF][3] = {{20, 50, 50}, {75, 90, 50}, {80, 95, 50}, {85, 95, 50}, {90, 95, 50}};
    memset(c, 0, sizeof *c);
    memcpy(c->open, open, sizeof open);
    memcpy(c->fist, fist, sizeof fist);
    c->thumb_opp_deg = 45; /* the thumb across the index and middle (a hammer grip) */
    c->fade_from_m = 0.015f, c->fade_to_m = 0.008f; /* clear before the wrist's cap (tilted: 0.9..2.2 cm past the joint) */
}

int somvr_vh_parse_cal(const char *v, somvr_vh_cfg *c) {
    if (!v) return 0;
    float f[6];
    const char *p = v;
    for (int i = 0; i < 6; i++) {
        char *e;
        f[i] = strtof(p, &e);
        if (e == p || !isfinite(f[i]) || fabsf(f[i]) > (i < 3 ? 10.0f : 90.0f)) return 0;
        p = e;
        if (i < 5) {
            if (*p != ',') return 0;
            p++;
        }
    }
    if (*p) return 0;
    memcpy(c->cal, f, sizeof f);
    return 1;
}

/* ---- posing ---------------------------------------------------------------------------------------------------- */
/* each joint's posed model-space transform for curls (0..1) */
static void pose_world(const somvr_vh_model *m, const somvr_vh_cfg *c, const float curl[SOMVR_VH_NF],
                       float world[SOMVR_VH_NJ][7]) {
    float bend[SOMVR_VH_NJ] = {0}, opp = 0;
    for (int f = 0; f < SOMVR_VH_NF; f++) {
        float k = curl[f] < 0 ? 0 : curl[f] > 1 ? 1 : curl[f];
        if (!isfinite(k)) k = 0;
        for (int j = 0; j < 3; j++) bend[bend_joint(f, j)] = (c->open[f][j] + k * (c->fist[f][j] - c->open[f][j])) * DEG;
        if (f == 0) opp = k * c->thumb_opp_deg * DEG;
    }
    for (int j = 0; j < SOMVR_VH_NJ; j++) {
        float loc[7], r[4], ro[4];
        memcpy(loc, m->local[j], sizeof loc);
        /* a bend turns the joint's -Z (toward the tip) toward its -Y (the palm's side): about its +X, negative */
        qaxis(1, 0, 0, -bend[j], r);
        if (j == SOMVR_VHJ_THUMB && opp != 0) { /* the thumb's metacarpal across the palm first */
            qaxis(0, 1, 0, opp, ro);
            qmul(ro, r, r);
        }
        qmul(loc, r, loc);
        int p = parent_of(j);
        if (p < 0)
            memcpy(world[j], loc, sizeof loc);
        else
            xf_mul(world[p], loc, world[j]);
    }
}

void somvr_vh_pose(const somvr_vh_model *m, const somvr_vh_cfg *c, const float curl[SOMVR_VH_NF],
                   float skin[SOMVR_VH_NJ][12]) {
    float world[SOMVR_VH_NJ][7];
    pose_world(m, c, curl, world);
    for (int j = 0; j < SOMVR_VH_NJ; j++) {
        float w[12];
        xf_mat(world[j], w);
        m34_mul(w, m->ibind[j], skin[j]);
    }
}

/* the right grip's frame from model space: p_g = gq p + gp */
static void to_grip(const somvr_vh_model *m, const float p[3], float o[3]) {
    float t[3];
    qrot(m->gq, p, t);
    for (int i = 0; i < 3; i++) o[i] = t[i] + m->gp[i];
}

void somvr_vh_joint_g(const somvr_vh_model *m, const float skin[SOMVR_VH_NJ][12], int j, float out[3]) {
    float p[3];
    m34_point(skin[j], somvr_hm_rest[j] + 4, p);
    to_grip(m, p, out);
}

/* a circle through the points (least squares, in their plane): its centre and radius; 0 = degenerate */
static int fit_circle(const float pts[][3], int n, float c[3], float *r) {
    float e1[3], e2[3], nrm[3];
    for (int i = 0; i < 3; i++) e1[i] = pts[n - 1][i] - pts[0][i], e2[i] = pts[n / 2][i] - pts[0][i];
    cross3(e1, e2, nrm);
    if (norm3(nrm) < 1e-9f || norm3(e1) < 1e-6f) return 0;
    cross3(nrm, e1, e2);
    norm3(e2);
    /* (x - a)^2 + (y - b)^2 = r^2 -> 2ax + 2by + k = x^2 + y^2 */
    double A[3][3] = {{0}}, B[3] = {0};
    for (int k = 0; k < n; k++) {
        float d[3] = {pts[k][0] - pts[0][0], pts[k][1] - pts[0][1], pts[k][2] - pts[0][2]};
        double x = dot3(d, e1), y = dot3(d, e2), row[3] = {2 * x, 2 * y, 1}, rhs = x * x + y * y;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) A[i][j] += row[i] * row[j];
            B[i] += row[i] * rhs;
        }
    }
    /* Cramer */
    double det = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) +
                 A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
    if (fabs(det) < 1e-18) return 0;
    double s[3];
    for (int col = 0; col < 3; col++) {
        double M[3][3];
        memcpy(M, A, sizeof M);
        for (int i = 0; i < 3; i++) M[i][col] = B[i];
        s[col] = (M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) +
                  M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0])) / det;
    }
    double rr = s[2] + s[0] * s[0] + s[1] * s[1];
    if (!(rr > 0)) return 0;
    for (int i = 0; i < 3; i++) c[i] = pts[0][i] + (float)s[0] * e1[i] + (float)s[1] * e2[i];
    *r = (float)sqrt(rr);
    return 1;
}

int somvr_vh_init(somvr_vh_model *m, const somvr_vh_cfg *c) {
    memset(m, 0, sizeof *m);
    /* the hierarchy: each joint's rest transform in its parent's (the mesh's are model space) */
    for (int j = 0; j < SOMVR_VH_NJ; j++) {
        float rest[7], inv[7];
        memcpy(rest, somvr_hm_rest[j], sizeof rest);
        qnorm(rest);
        int p = parent_of(j);
        if (p < 0) {
            memcpy(m->local[j], rest, sizeof rest);
        } else {
            float pr[7];
            memcpy(pr, somvr_hm_rest[p], sizeof pr);
            qnorm(pr);
            xf_inv(pr, inv);
            xf_mul(inv, rest, m->local[j]);
        }
        xf_inv(rest, inv);
        xf_mat(inv, m->ibind[j]);
    }
    /* the fist's tube: a circle through each long finger's knuckle, joints and tip, the axis through their centres */
    static const float one[SOMVR_VH_NF] = {1, 1, 1, 1, 1};
    float world[SOMVR_VH_NJ][7], cen[4][3], rad[4];
    pose_world(m, c, one, world);
    for (int f = 1; f < SOMVR_VH_NF; f++) {
        float pts[4][3];
        for (int k = 0; k < 4; k++) memcpy(pts[k], world[g_base[f] + 1 + k] + 4, sizeof pts[k]);
        if (!fit_circle(pts, 4, cen[f - 1], &rad[f - 1])) {
            snprintf(m->why, sizeof m->why, "the %s finger's fist is not a curve", f == 1 ? "index" : f == 2 ? "middle" : f == 3 ? "ring" : "pinky");
            return -1;
        }
    }
    float d[3], o[3], kn[3];
    for (int i = 0; i < 3; i++) {
        d[i] = (cen[0][i] + cen[1][i]) - (cen[2][i] + cen[3][i]); /* pinky -> index: toward the thumb = the grip's -Z */
        o[i] = 0.25f * (cen[0][i] + cen[1][i] + cen[2][i] + cen[3][i]);
        kn[i] = world[SOMVR_VHJ_INDEX + 1][4 + i] - world[SOMVR_VHJ_PINKY + 1][4 + i];
    }
    if (norm3(d) < 1e-4f || norm3(kn) < 1e-4f) {
        snprintf(m->why, sizeof m->why, "no fist axis");
        return -1;
    }
    m->tube_r_m = 0.25f * (rad[0] + rad[1] + rad[2] + rad[3]);
    m->tube_tilt_deg = acosf(fminf(1, fmaxf(-1, dot3(d, kn)))) / DEG;
    /* the grip's frame in model space: Z = -d, X = the back of the hand (+X: "into the palm" for a right hand) */
    float X[3] = {1, 0, 0}, Y[3], Z[3] = {-d[0], -d[1], -d[2]};
    float k = dot3(X, Z);
    for (int i = 0; i < 3; i++) X[i] -= k * Z[i];
    if (norm3(X) < 0.5f) {
        snprintf(m->why, sizeof m->why, "the fist's axis runs along the back of the hand");
        return -1;
    }
    cross3(Z, X, Y);
    float rq[4], rt[3], tube[7], cal[7], cq[4], t1[4], t2[4];
    quat_of_basis(X, Y, Z, rq); /* model <- grip */
    qconj(rq, rq);               /* grip <- model */
    qrot(rq, o, rt);
    memcpy(tube, rq, sizeof rq);
    for (int i = 0; i < 3; i++) tube[4 + i] = -rt[i];
    /* the calibration (SOMVR_XR_HANDS_CAL) in the grip's frame: rotate (x, then y, then z), then move */
    qaxis(1, 0, 0, c->cal[3] * DEG, t1), qaxis(0, 1, 0, c->cal[4] * DEG, t2);
    qmul(t2, t1, cq);
    qaxis(0, 0, 1, c->cal[5] * DEG, t1);
    qmul(t1, cq, cq);
    memcpy(cal, cq, sizeof cq);
    for (int i = 0; i < 3; i++) cal[4 + i] = 0.01f * c->cal[i];
    float g[7];
    xf_mul(cal, tube, g);
    memcpy(m->gq, g, sizeof m->gq);
    memcpy(m->gp, g + 4, sizeof m->gp);
    /* the wrist at rest: its place, the forearm (+Y in model space: the wrist joint's +Z) and the back (+X) */
    static const float ey[3] = {0, 1, 0}, ex[3] = {1, 0, 0};
    float wr[3];
    memcpy(wr, somvr_hm_rest[SOMVR_VHJ_WRIST] + 4, sizeof wr);
    to_grip(m, wr, m->wrist_g);
    qrot(m->gq, ey, m->arm_g);
    qrot(m->gq, ex, m->back_g);
    /* the wrist's height (the back of the wrist within 1.5 cm of the joint along the arm) and the fade */
    m->wrist_r_m = 0;
    for (int v = 0; v < SOMVR_HM_NV; v++) {
        float dv[3] = {somvr_hm_pos[v][0] - wr[0], somvr_hm_pos[v][1] - wr[1], somvr_hm_pos[v][2] - wr[2]};
        float s = dot3(dv, ey), b = dot3(dv, ex);
        if (fabsf(s) < 0.015f && b > m->wrist_r_m) m->wrist_r_m = b;
        m->fade[v] = 1 - smooth01(-c->fade_from_m, c->fade_to_m, s);
    }
    /* how far out of the palm any pose reaches (the right palm's outward normal = the grip's -X), from the origin: the
     * thumb, the index, the middle with the ring and the pinky each at 0, 1/2, 1 (81 poses); then the pointing tip */
    static const float point[SOMVR_VH_NF] = {1, 0, 1, 1, 1}, lv[3] = {0, 0.5f, 1};
    m->palm_reach_m = 0;
    for (int p = 0; p <= 81; p++) {
        float skin[SOMVR_VH_NJ][12], cu[SOMVR_VH_NF] = {lv[p % 3], lv[p / 3 % 3], lv[p / 9 % 3], lv[p / 9 % 3], lv[p / 27 % 3]};
        somvr_vh_pose(m, c, p == 81 ? point : cu, skin);
        for (int v = 0; v < SOMVR_HM_NV; v++) {
            float q[3] = {0, 0, 0}, pg[3];
            for (int i = 0; i < 4; i++) {
                float w = somvr_hm_weight[v][i], t[3];
                if (w <= 0) continue;
                m34_point(skin[somvr_hm_joint[v][i]], somvr_hm_pos[v], t);
                for (int a = 0; a < 3; a++) q[a] += w * t[a];
            }
            to_grip(m, q, pg);
            if (-pg[0] > m->palm_reach_m) m->palm_reach_m = -pg[0];
        }
        if (p == 81) {
            float k1[3];
            somvr_vh_joint_g(m, (const float(*)[12])skin, tip_joint(1), m->tip_g);
            somvr_vh_joint_g(m, (const float(*)[12])skin, SOMVR_VHJ_INDEX + 1, k1);
            for (int i = 0; i < 3; i++) m->tipdir_g[i] = m->tip_g[i] - k1[i];
            norm3(m->tipdir_g);
        }
    }
    for (int t = 0; t < SOMVR_HM_NI / 3; t++) { /* the mirrored hand's triangles: the winding reversed */
        m->idx_mirror[3 * t] = somvr_hm_idx[3 * t];
        m->idx_mirror[3 * t + 1] = somvr_hm_idx[3 * t + 2];
        m->idx_mirror[3 * t + 2] = somvr_hm_idx[3 * t + 1];
    }
    snprintf(m->why, sizeof m->why,
             "%d vertices, %d triangles; the fist's tube %.1f cm across (joint radius %.1f cm, %.0f deg off the knuckles), "
             "the wrist %.1f cm behind the grip, the palm's reach %.1f cm, the pointing tip at %.1f,%.1f,%.1f cm",
             SOMVR_HM_NV, SOMVR_HM_NI / 3, 200 * (m->tube_r_m - 0.0085f), 100 * m->tube_r_m, m->tube_tilt_deg,
             100 * sqrtf(dot3(m->wrist_g, m->wrist_g)), 100 * m->palm_reach_m, 100 * m->tip_g[0], 100 * m->tip_g[1],
             100 * m->tip_g[2]);
    m->ok = 1;
    return 0;
}

int somvr_vh_nverts(void) { return SOMVR_HM_NV; }
int somvr_vh_nindices(void) { return SOMVR_HM_NI; }
const uint16_t *somvr_vh_indices(const somvr_vh_model *m, int mirror) { return mirror ? m->idx_mirror : somvr_hm_idx; }

void somvr_vh_grip_point(const somvr_posef *grip, int side, const float pg[3], float out[3]) {
    float p[3] = {side == SOMVR_HAND_L ? -pg[0] : pg[0], pg[1], pg[2]}, t[3];
    qrot(grip->q, p, t);
    for (int i = 0; i < 3; i++) out[i] = grip->p[i] + t[i];
}

void somvr_vh_tip(const somvr_vh_model *m, const somvr_posef *grip, int side, float out[3]) {
    somvr_vh_grip_point(grip, side, m->tip_g, out);
}

int somvr_vh_touch_grip(const somvr_vh_model *m, const float point[3], const float dir[3], const float up[3], int side,
                        somvr_posef *grip) {
    /* grip space: a = the index's direction, b = the thumb's side (-Z) square to it; world: A = dir, B = up square to it */
    float s = side == SOMVR_HAND_L ? -1.0f : 1.0f;
    float a[3] = {s * m->tipdir_g[0], m->tipdir_g[1], m->tipdir_g[2]}, b[3] = {0, 0, -1}, c[3];
    float A[3] = {dir[0], dir[1], dir[2]}, B[3] = {up[0], up[1], up[2]}, C[3];
    if (norm3(a) < 0.5f || norm3(A) < 0.5f) return 0;
    float k = dot3(a, b), K = dot3(A, B);
    for (int i = 0; i < 3; i++) b[i] -= k * a[i], B[i] -= K * A[i];
    if (norm3(b) < 0.1f || norm3(B) < 0.1f) return 0;
    cross3(a, b, c), cross3(A, B, C);
    /* R = [A B C] [a b c]^T */
    float x[3], y[3], z[3];
    for (int i = 0; i < 3; i++) {
        x[i] = A[i] * a[0] + B[i] * b[0] + C[i] * c[0];
        y[i] = A[i] * a[1] + B[i] * b[1] + C[i] * c[1];
        z[i] = A[i] * a[2] + B[i] * b[2] + C[i] * c[2];
    }
    quat_of_basis(x, y, z, grip->q);
    float tip[3] = {s * m->tip_g[0], m->tip_g[1], m->tip_g[2]}, t[3];
    qrot(grip->q, tip, t);
    for (int i = 0; i < 3; i++) grip->p[i] = point[i] - t[i];
    return 1;
}

void somvr_vh_skin(const somvr_vh_model *m, const float skin[SOMVR_VH_NJ][12], int side, const somvr_posef *grip,
                   somvr_vh_vert *out) {
    /* model -> right grip (gq, gp) -> the side's mirror (x) -> XR (the grip pose): one 3x4 per joint */
    float g[12], gm[7], gx[12], s[12];
    memcpy(gm, m->gq, sizeof m->gq);
    memcpy(gm + 4, m->gp, sizeof m->gp);
    xf_mat(gm, g);
    if (side == SOMVR_HAND_L)
        for (int j = 0; j < 4; j++) g[j] = -g[j];
    float gp[7];
    memcpy(gp, grip->q, sizeof grip->q);
    memcpy(gp + 4, grip->p, sizeof grip->p);
    qnorm(gp);
    xf_mat(gp, gx);
    m34_mul(gx, g, s);
    float js[SOMVR_VH_NJ][12];
    for (int j = 0; j < SOMVR_VH_NJ; j++) m34_mul(s, skin[j], js[j]);
    for (int v = 0; v < SOMVR_HM_NV; v++) {
        float p[3] = {0, 0, 0}, n[3] = {0, 0, 0};
        for (int i = 0; i < 4; i++) {
            float w = somvr_hm_weight[v][i], t[3], u[3];
            if (w <= 0) continue;
            const float *M = js[somvr_hm_joint[v][i]];
            m34_point(M, somvr_hm_pos[v], t);
            m34_vec(M, somvr_hm_nrm[v], u);
            for (int a = 0; a < 3; a++) p[a] += w * t[a], n[a] += w * u[a];
        }
        norm3(n); /* (a mirror's normals too: its linear part is orthogonal, its inverse transpose itself) */
        memcpy(out[v].p, p, sizeof p);
        memcpy(out[v].n, n, sizeof n);
        out[v].a = m->fade[v];
    }
}

/* ---- what hangs on the hand ------------------------------------------------------------------------------------- */
void somvr_vh_watch_cfg_default(somvr_vh_watch_cfg *c) { c->back_m = 0.015f, c->gap_m = 0.008f; }

int somvr_vh_watch_pose(const somvr_vh_model *m, const somvr_vh_watch_cfg *c, const somvr_posef *grip, int side,
                        somvr_posef *out) {
    if (!m->ok || !isfinite(grip->q[0] + grip->q[1] + grip->q[2] + grip->q[3] + grip->p[0] + grip->p[1] + grip->p[2]))
        return 0;
    /* the right hand's face in its grip frame: z = the back of the wrist, x = toward the elbow, y = z x x (away from
     * the thumb); the left's mirrored with x turned round (toward the fingers) so it stays a rotation */
    float z[3], x[3], y[3], pc[3];
    memcpy(z, m->back_g, sizeof z);
    memcpy(x, m->arm_g, sizeof x);
    float k = dot3(x, z);
    for (int i = 0; i < 3; i++) x[i] -= k * z[i];
    if (norm3(x) < 0.5f || norm3(z) < 0.5f) return 0;
    cross3(z, x, y);
    for (int i = 0; i < 3; i++) pc[i] = m->wrist_g[i] + c->back_m * m->arm_g[i] + (m->wrist_r_m + c->gap_m) * z[i];
    if (side == SOMVR_HAND_L) {
        x[0] = -x[0], y[0] = -y[0], z[0] = -z[0]; /* the mirror */
        for (int i = 0; i < 3; i++) x[i] = -x[i];
    }
    float q[4], gq[4];
    quat_of_basis(x, y, z, q);
    memcpy(gq, grip->q, sizeof gq);
    qnorm(gq);
    qmul(gq, q, out->q);
    qnorm(out->q);
    somvr_vh_grip_point(grip, side, pc, out->p);
    return 1;
}

/* a face on the wrist is seen whenever it turns toward the eye at all (a real watch): it comes and goes edge-on, where
 * it covers next to nothing (the old 66 deg rule popped a 10 cm face in at a third of its size) */
void somvr_vh_face_cfg_default(somvr_vh_face_cfg *c) { c->face_on = 0.15f, c->face_off = 0.08f, c->hold_on = 2, c->hold_off = 3; }

int somvr_vh_face_step(somvr_vh_face *s, const somvr_vh_face_cfg *c, const somvr_posef *face, const float eye[3]) {
    static const float ez[3] = {0, 0, 1};
    float n[3], d[3] = {eye[0] - face->p[0], eye[1] - face->p[1], eye[2] - face->p[2]};
    qrot(face->q, ez, n);
    float k = norm3(d) > 0 ? dot3(n, d) : -1;
    if (!isfinite(k)) k = -1;
    if (s->shown) {
        s->run = k > c->face_off ? 0 : s->run + 1;
        if (s->run >= c->hold_off) s->shown = 0, s->run = 0;
    } else {
        s->run = k > c->face_on ? s->run + 1 : 0;
        if (s->run >= c->hold_on) s->shown = 1, s->run = 0;
    }
    return s->shown;
}

/* ==== build 16: the arrow (vrhands v4) ============================================================================ */
#define AR_R 0.0045f    /* the shaft's radius (m) */
#define AR_HEAD 0.05f   /* the head's length */
#define AR_HEAD_W 0.011f /* its half width */
static const uint16_t g_arrow_one[SOMVR_VH_ARROW_NI / 2] = {
    /* the shaft: 6 quads (4 vertices each from 0) */
    0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7, 8, 9, 10, 8, 10, 11, 12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22,
    20, 22, 23,
    /* the nock's cap: the centre 24, the rim 25..30 */
    24, 25, 26, 24, 26, 27, 24, 27, 28, 24, 28, 29, 24, 29, 30, 24, 30, 25,
    /* the head: 4 faces (31..42), its back (43..46) */
    31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 43, 45, 46,
    /* the vanes: 3 quads (47..58) */
    47, 48, 49, 47, 49, 50, 51, 52, 53, 51, 53, 54, 55, 56, 57, 55, 57, 58};
static uint16_t g_arrow_idx[SOMVR_VH_ARROW_NI];
const uint16_t *somvr_vh_arrow_indices(void) {
    if (g_arrow_idx[1] == 0) { /* once: each triangle, then the same reversed */
        int n = SOMVR_VH_ARROW_NI / 2;
        for (int i = 0; i < n; i += 3) {
            g_arrow_idx[i] = g_arrow_one[i], g_arrow_idx[i + 1] = g_arrow_one[i + 1], g_arrow_idx[i + 2] = g_arrow_one[i + 2];
            g_arrow_idx[n + i] = g_arrow_one[i], g_arrow_idx[n + i + 1] = g_arrow_one[i + 2], g_arrow_idx[n + i + 2] = g_arrow_one[i + 1];
        }
    }
    return g_arrow_idx;
}

static void ar_put(somvr_vh_vert *v, const float q[4], const float o[3], float x, float y, float z, float nx, float ny,
                   float nz) {
    float lp[3] = {x, y, z}, ln[3] = {nx, ny, nz}, w[3];
    qrot(q, lp, w);
    for (int i = 0; i < 3; i++) v->p[i] = o[i] + w[i];
    qrot(q, ln, v->n);
    v->a = 1;
}

void somvr_vh_arrow(const somvr_posef *pose, float len_m, somvr_vh_vert *out) {
    float q[4] = {pose->q[0], pose->q[1], pose->q[2], pose->q[3]}, o[3] = {pose->p[0], pose->p[1], pose->p[2]};
    qnorm(q);
    if (!(len_m >= 0.2f)) len_m = 0.2f; /* (NaN too) */
    if (len_m > 1.5f) len_m = 1.5f;
    float ls = len_m - AR_HEAD; /* the shaft's end: the head's back */
    somvr_vh_vert *v = out;
    for (int k = 0; k < 6; k++) { /* the shaft's sides, flat */
        float a0 = (float)k * 1.0471976f, a1 = (float)(k + 1) * 1.0471976f, am = 0.5f * (a0 + a1);
        float c0 = cosf(a0) * AR_R, s0 = sinf(a0) * AR_R, c1 = cosf(a1) * AR_R, s1 = sinf(a1) * AR_R;
        ar_put(v++, q, o, c0, s0, 0, cosf(am), sinf(am), 0);
        ar_put(v++, q, o, c0, s0, -ls, cosf(am), sinf(am), 0);
        ar_put(v++, q, o, c1, s1, -ls, cosf(am), sinf(am), 0);
        ar_put(v++, q, o, c1, s1, 0, cosf(am), sinf(am), 0);
    }
    ar_put(v++, q, o, 0, 0, 0.002f, 0, 0, 1); /* the nock's cap, a little proud */
    for (int k = 0; k < 6; k++) {
        float a = (float)k * 1.0471976f;
        ar_put(v++, q, o, cosf(a) * AR_R, sinf(a) * AR_R, 0, 0, 0, 1);
    }
    float b[4][3]; /* the head's back: a diamond */
    for (int k = 0; k < 4; k++) {
        float a = 0.7853982f + (float)k * 1.5707963f;
        b[k][0] = cosf(a) * AR_HEAD_W, b[k][1] = sinf(a) * AR_HEAD_W, b[k][2] = -ls;
    }
    float tip[3] = {0, 0, -len_m};
    for (int k = 0; k < 4; k++) {
        const float *p0 = b[k], *p1 = b[(k + 1) & 3];
        float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]}, e2[3] = {tip[0] - p0[0], tip[1] - p0[1], tip[2] - p0[2]};
        float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (n[0] * (p0[0] + p1[0]) + n[1] * (p0[1] + p1[1]) < 0) l = -l; /* outward */
        ar_put(v++, q, o, p0[0], p0[1], p0[2], n[0] / l, n[1] / l, n[2] / l);
        ar_put(v++, q, o, p1[0], p1[1], p1[2], n[0] / l, n[1] / l, n[2] / l);
        ar_put(v++, q, o, tip[0], tip[1], tip[2], n[0] / l, n[1] / l, n[2] / l);
    }
    for (int k = 0; k < 4; k++) ar_put(v++, q, o, b[k][0], b[k][1], b[k][2], 0, 0, 1);
    for (int k = 0; k < 3; k++) { /* the vanes, 120 deg apart, the cock vane along +Y */
        float a = 1.5707963f + (float)k * 2.0943951f, c = cosf(a), s = sinf(a);
        ar_put(v++, q, o, c * AR_R, s * AR_R, -0.025f, -s, c, 0);
        ar_put(v++, q, o, c * (AR_R + 0.015f), s * (AR_R + 0.015f), -0.035f, -s, c, 0);
        ar_put(v++, q, o, c * (AR_R + 0.010f), s * (AR_R + 0.010f), -0.11f, -s, c, 0);
        ar_put(v++, q, o, c * AR_R, s * AR_R, -0.125f, -s, c, 0);
    }
}

void somvr_vh_arrow_fade(somvr_vh_vert *v, int n, const float eye0[3], const float eye1[3], float near_m, float far_m) {
    for (int i = 0; i < n; i++) {
        float d0 = 0, d1 = 0;
        for (int k = 0; k < 3; k++)
            d0 += (v[i].p[k] - eye0[k]) * (v[i].p[k] - eye0[k]), d1 += (v[i].p[k] - eye1[k]) * (v[i].p[k] - eye1[k]);
        float d = sqrtf(d0 < d1 ? d0 : d1), x = far_m > near_m ? (d - near_m) / (far_m - near_m) : (float)(d >= near_m);
        x = x < 0 ? 0 : x > 1 ? 1 : x; /* (NaN: 0) */
        v[i].a = x * x * (3 - 2 * x);
    }
}
