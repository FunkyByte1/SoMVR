/* somvr_kills.c - SOMVR_KILLS, the pure part (somvr_kills.h; docs/re-xr-pipeline.md 133) */
#include "somvr_kills.h"

#include <math.h>
#include <string.h>

int somvr_kl_side(const float c[3], const float o[3], float yaw, float back_deg, float *off_deg)
{
    float dx = c[0] - o[0], dz = c[2] - o[2], off = 0;
    if (dx * dx + dz * dz > 1e-6f)
        off = remainderf(atan2f(dx, dz) - yaw, 6.2831853f) * 57.29578f;
    if (off_deg)
        *off_deg = off;
    return fabsf(off) > back_deg ? SOMVR_KL_BACK : SOMVR_KL_FRONT;
}

int somvr_kl_kind(const float v[3], const float a[3], float stab_cos)
{
    float vl = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]), al = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (!(vl > 1e-6f))
        return SOMVR_KL_CUT;
    if (al > 1e-6f && (v[0] * a[0] + v[1] * a[1] + v[2] * a[2]) / (vl * al) >= stab_cos)
        return SOMVR_KL_STAB;
    float up = v[1] / vl;
    return up < -0.6f ? SOMVR_KL_OVER : up > 0.6f ? SOMVR_KL_RISE : SOMVR_KL_CUT;
}

int somvr_kl_part(float up_cm, float neck_up_cm, float neck_dist_cm, float neck_r_cm)
{
    if (neck_dist_cm == neck_dist_cm && neck_dist_cm <= neck_r_cm)
        return SOMVR_KL_NECK;
    return up_cm >= neck_up_cm ? SOMVR_KL_NECK : SOMVR_KL_BODY;
}

int somvr_kl_fits(const somvr_kl_row *r, const somvr_kl_blow *b)
{
    return (r->side & b->side) && (r->states & b->state) && (r->parts & b->part) && (r->kinds & b->kind);
}

int somvr_kl_pick(const somvr_kl_row *rows, int n, const somvr_kl_blow *b, unsigned turn, const unsigned char *off)
{
    int idx[64], m = 0;
    for (int i = 0; i < n && m < 64; i++)
        if (!(off && off[i]) && somvr_kl_fits(&rows[i], b))
            idx[m++] = i;
    if (!m) /* side and state kept; any kind, and a body row for any part (a sever row only for the neck) */
        for (int i = 0; i < n && m < 64; i++)
            if (!(off && off[i]) && (rows[i].side & b->side) && (rows[i].states & b->state) &&
                (!rows[i].sever || b->part == SOMVR_KL_NECK) && ((rows[i].parts & SOMVR_KL_BODY) || (rows[i].parts & b->part)))
                idx[m++] = i;
    return m ? idx[turn % (unsigned)m] : -1;
}

int somvr_kl_find(const somvr_kl_row *rows, int n, const char *name)
{
    for (int i = 0; name && i < n; i++)
        if (rows[i].name && !strcmp(rows[i].name, name))
            return i;
    return -1;
}

int somvr_kl_align(const float o[3], float yaw, const float t[3], int side, float min_cm, float max_cm, float max_deg,
                   float *dyaw, float dpos[2])
{
    float dx = t[0] - o[0], dz = t[2] - o[2], d = sqrtf(dx * dx + dz * dz);
    *dyaw = 0, dpos[0] = dpos[1] = 0;
    if (!(d > 1e-3f))
        return 0;
    float want = atan2f(dx, dz); /* the orc faces Talion */
    if (side == SOMVR_KL_BACK)
        want += 3.14159265f; /* its back to him */
    float turn = remainderf(want - yaw, 6.2831853f), maxr = max_deg * 0.01745329f;
    float slide = d < min_cm ? d - min_cm : 0; /* negative: away from Talion */
    if (fabsf(turn) > 2.0f * maxr || fabsf(slide) > 2.0f * max_cm)
        return 0;
    *dyaw = turn > maxr ? maxr : turn < -maxr ? -maxr : turn;
    slide = slide > max_cm ? max_cm : slide < -max_cm ? -max_cm : slide;
    dpos[0] = dx / d * slide, dpos[1] = dz / d * slide;
    return 1;
}

float somvr_kl_ease(double e, double dur)
{
    if (!(dur > 0) || e >= dur)
        return 1.0f;
    if (!(e > 0))
        return 0.0f;
    float x = (float)(e / dur);
    return x * x * (3.0f - 2.0f * x);
}

float somvr_kl_death_at(const somvr_kl_row *r, float lead_s)
{
    if (r->death_s > 0)
        return r->death_s;
    if (r->len_s > 0)
        return r->len_s > lead_s ? r->len_s - lead_s : r->len_s;
    return NAN;
}

const char *somvr_kl_kind_name(int k)
{
    return k == SOMVR_KL_STAB ? "a stab" : k == SOMVR_KL_OVER ? "an overhead cut" : k == SOMVR_KL_RISE ? "a rising cut"
                                                                                                        : "a level cut";
}

const char *somvr_kl_state_name(int s)
{
    return s == SOMVR_KL_UNAWARE ? "unaware" : s == SOMVR_KL_STUNNED ? "stunned" : s == SOMVR_KL_DOWNED ? "downed"
           : s == SOMVR_KL_COUNTER ? "countered mid-attack" : s == SOMVR_KL_HELD ? "held" : "fighting";
}

int somvr_kl_quick_why(const somvr_kl_quick *q, double el, int seen, float moved, float drop, float rise)
{
    el += 1e-6; /* (the times are floats: 0.15 s reached at 0.15) */
    if (q->max_s > 0 && el >= q->max_s)
        return SOMVR_KLQ_LATE;
    if (q->start_s > 0 && el >= q->start_s) {
        if (!seen)
            return SOMVR_KLQ_UNSEEN;
        if (!(moved >= q->start_cm))
            return SOMVR_KLQ_STILL;
    }
    if (q->rise_cm > 0 && seen && rise >= q->rise_cm && !(drop >= q->fall_cm))
        return SOMVR_KLQ_RISING; /* (straightening up before any fall: its own animation, not the half) */
    if (q->fall_s > 0 && el >= q->fall_s && seen && !(drop >= q->fall_cm))
        return SOMVR_KLQ_NOFALL;
    return SOMVR_KLQ_GO;
}

const char *somvr_kl_quick_name(int v)
{
    static const char *const n[SOMVR_KLQ_N] = { "it plays", "its half unseen (no skeleton read)",
                                                "its half not started (the pose still)", "its fall not started",
                                                "the half's time is up", "its head rising (no half: the orc straightening up)" };
    return v >= 0 && v < SOMVR_KLQ_N ? n[v] : "?";
}

static void kl_local(const float d[3], float yaw, float out[3])
{
    float s = sinf(yaw), c = cosf(yaw); /* its forward (s, 0, c), its right (c, 0, -s) */
    out[0] = d[0] * c - d[2] * s, out[1] = d[1], out[2] = d[0] * s + d[2] * c;
}

float somvr_kl_pose_moved(const float (*a)[3], float ya, const float (*b)[3], float yb, int n)
{
    float m = 0;
    for (int i = 0; i < n; i++) {
        float p[3], q[3];
        kl_local(a[i], ya, p), kl_local(b[i], yb, q);
        float d = sqrtf((p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]));
        if (d > m)
            m = d;
    }
    return m;
}
