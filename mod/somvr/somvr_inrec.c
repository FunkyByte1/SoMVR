/* somvr_inrec.c - SOMVR_INPUT_REC's format and the replay's pure parts (somvr_inrec.h; docs/re-xr-pipeline.md 160) */
#include "somvr_inrec.h"

#include <math.h>
#include <string.h>

void somvr_ir_hdr_init(somvr_ir_hdr *h, double t0, int64_t unix_t0, int32_t pid, uint32_t flags) {
    memset(h, 0, sizeof *h);
    memcpy(h->magic, SOMVR_IR_MAGIC, 8);
    h->version = SOMVR_IR_VERSION, h->hdr_size = (uint32_t)sizeof *h;
    h->t0 = t0, h->unix_t0 = unix_t0, h->pid = pid, h->flags = flags;
    h->frame_size = (uint32_t)sizeof(somvr_ir_frame);
    const char *d = __DATE__;
    for (size_t i = 0; i < sizeof h->build - 1 && d[i]; i++)
        h->build[i] = d[i];
}

int somvr_ir_hdr_ok(const somvr_ir_hdr *h) {
    return h && !memcmp(h->magic, SOMVR_IR_MAGIC, 8) && h->version >= 1 && h->hdr_size >= sizeof *h &&
           h->frame_size == sizeof(somvr_ir_frame);
}

int somvr_ir_next(const void *buf, size_t len, size_t off, somvr_ir_rh *rh) {
    if (!buf || off + sizeof *rh > len)
        return 0;
    memcpy(rh, (const char *)buf + off, sizeof *rh);
    return rh->size >= sizeof *rh && rh->size % 8 == 0 && off + rh->size <= len;
}

void somvr_ir_hand_put(somvr_ir_hand *o, const somvr_hand *h, const somvr_hand_in *in, float squeeze) {
    memset(o, 0, sizeof *o);
    o->q[3] = o->aim_q[3] = 1;
    if (h && h->valid) {
        memcpy(o->p, h->pose.p, sizeof o->p), memcpy(o->q, h->pose.q, sizeof o->q);
        memcpy(o->lin, h->lin, sizeof o->lin), memcpy(o->ang, h->ang, sizeof o->ang);
        o->valid = 1, o->tracked = h->tracked ? 1 : 0, o->vel_valid = (uint8_t)(h->vel_valid & 3), o->src = (uint8_t)h->src;
    }
    if (in) {
        memcpy(o->aim_p, in->aim.p, sizeof o->aim_p), memcpy(o->aim_q, in->aim.q, sizeof o->aim_q);
        o->trigger = in->trigger, o->squeeze = in->squeeze, o->stick[0] = in->stick[0], o->stick[1] = in->stick[1];
        o->buttons = in->buttons;
        o->grip_flags = in->grip_flags, o->aim_flags = in->aim_flags, o->vel_flags = in->vel_flags, o->active = in->active;
        memcpy(o->presses, in->presses, sizeof o->presses);
    }
    if (squeeze >= 0)
        o->squeeze = squeeze, o->active |= SOMVR_HACT_INPUT;
}

void somvr_ir_hand_get(const somvr_ir_hand *o, int64_t time, somvr_hand *h, somvr_hand_in *in) {
    if (h) {
        memset(h, 0, sizeof *h);
        h->valid = o->valid, h->tracked = o->tracked, h->vel_valid = o->vel_valid, h->src = o->src;
        h->flags = o->valid ? (o->tracked ? 0xfu : 0x3u) : 0u;
        h->time = time;
        memcpy(h->pose.p, o->p, sizeof o->p), memcpy(h->pose.q, o->q, sizeof o->q);
        memcpy(h->lin, o->lin, sizeof o->lin), memcpy(h->ang, o->ang, sizeof o->ang);
    }
    if (in) {
        memset(in, 0, sizeof *in);
        memcpy(in->grip.p, o->p, sizeof o->p), memcpy(in->grip.q, o->q, sizeof o->q);
        memcpy(in->aim.p, o->aim_p, sizeof o->aim_p), memcpy(in->aim.q, o->aim_q, sizeof o->aim_q);
        memcpy(in->lin_vel, o->lin, sizeof o->lin), memcpy(in->ang_vel, o->ang, sizeof o->ang);
        in->trigger = o->trigger, in->squeeze = o->squeeze, in->stick[0] = o->stick[0], in->stick[1] = o->stick[1];
        in->buttons = o->buttons;
        in->grip_flags = o->grip_flags, in->aim_flags = o->aim_flags, in->vel_flags = o->vel_flags, in->active = o->active;
        memcpy(in->presses, o->presses, sizeof in->presses);
    }
}

size_t somvr_ir_note_make(void *buf, size_t cap, double t, const char *text) {
    size_t n = strlen(text ? text : ""), sz = (sizeof(somvr_ir_note) + n + 1 + 7) & ~(size_t)7;
    if (sz > cap || sz > 0xfff8)
        return 0;
    somvr_ir_note h = { { SOMVR_IR_NOTE, (uint16_t)sz }, 0, t };
    memset(buf, 0, sz);
    memcpy(buf, &h, sizeof h);
    if (n)
        memcpy((char *)buf + sizeof h, text, n);
    return sz;
}

size_t somvr_ir_cfg_make(void *buf, size_t cap, uint32_t id, const void *blob, uint32_t len) {
    size_t sz = (sizeof(somvr_ir_cfg) + (size_t)len + 7) & ~(size_t)7;
    if (sz > cap || sz > 0xfff8)
        return 0;
    somvr_ir_cfg h = { { SOMVR_IR_CFG, (uint16_t)sz }, id, len, 0 };
    memset(buf, 0, sz);
    memcpy(buf, &h, sizeof h);
    if (len)
        memcpy((char *)buf + sizeof h, blob, len);
    return sz;
}

int somvr_ir_find(const somvr_ir_frame *f, int n, int64_t t) {
    if (n <= 0 || t < f[0].xr_time)
        return -1;
    int lo = 0, hi = n - 1; /* f[lo].xr_time <= t */
    while (lo < hi) {
        int mid = lo + (hi - lo + 1) / 2;
        if (f[mid].xr_time <= t)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

static void ir_lerp3(const float a[3], const float b[3], float u, float o[3]) {
    for (int i = 0; i < 3; i++)
        o[i] = a[i] + u * (b[i] - a[i]);
}
static void ir_nlerp(const float a[4], const float b[4], float u, float o[4]) {
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3], s = d < 0 ? -1.0f : 1.0f, r[4], n = 0;
    for (int i = 0; i < 4; i++)
        r[i] = a[i] + u * (s * b[i] - a[i]), n += r[i] * r[i];
    n = n > 1e-12f ? 1.0f / sqrtf(n) : 0;
    if (n == 0) {
        memcpy(o, a, 4 * sizeof *o);
        return;
    }
    for (int i = 0; i < 4; i++)
        o[i] = r[i] * n;
}

void somvr_ir_sample(const somvr_ir_frame *f, int n, int64_t t, somvr_ir_frame *out, uint32_t *flags_out) {
    uint32_t fl = 0;
    if (n <= 0) {
        memset(out, 0, sizeof *out);
        if (flags_out)
            *flags_out = 2;
        return;
    }
    int i = somvr_ir_find(f, n, t);
    if (i < 0)
        i = 0;
    *out = f[i];
    if (i + 1 >= n || t <= f[i].xr_time) {
        if (i + 1 >= n && t > f[i].xr_time)
            fl |= 2;
        if (flags_out)
            *flags_out = fl;
        return;
    }
    const somvr_ir_frame *a = &f[i], *b = &f[i + 1];
    int64_t span = b->xr_time - a->xr_time;
    float u = span > 0 ? (float)((double)(t - a->xr_time) / (double)span) : 0;
    u = u < 0 ? 0 : u > 1 ? 1 : u;
    fl |= 1;
    out->xr_time = t;
    out->t = a->t + (double)u * (b->t - a->t);
    for (int e = 0; e < 2; e++)
        ir_lerp3(a->eye_p[e], b->eye_p[e], u, out->eye_p[e]), ir_nlerp(a->eye_q[e], b->eye_q[e], u, out->eye_q[e]);
    for (int h = 0; h < 2; h++) {
        const somvr_ir_hand *ha = &a->hand[h], *hb = &b->hand[h];
        somvr_ir_hand *o = &out->hand[h];
        if (ha->valid && hb->valid) {
            ir_lerp3(ha->p, hb->p, u, o->p), ir_nlerp(ha->q, hb->q, u, o->q);
            ir_lerp3(ha->lin, hb->lin, u, o->lin), ir_lerp3(ha->ang, hb->ang, u, o->ang);
        }
        ir_lerp3(ha->aim_p, hb->aim_p, u, o->aim_p), ir_nlerp(ha->aim_q, hb->aim_q, u, o->aim_q);
        o->trigger = ha->trigger + u * (hb->trigger - ha->trigger);
        o->squeeze = ha->squeeze + u * (hb->squeeze - ha->squeeze);
        o->stick[0] = ha->stick[0] + u * (hb->stick[0] - ha->stick[0]);
        o->stick[1] = ha->stick[1] + u * (hb->stick[1] - ha->stick[1]);
    }
    if (flags_out)
        *flags_out = fl;
}

/* ---- the remap ---------------------------------------------------------------------------------------------------- */

void somvr_ir_map_make(somvr_ir_map *m, const float p0_src[3], float yaw0_src, const float p0_live[3], float yaw0_live,
                       float kappa) {
    memcpy(m->p0_src, p0_src, sizeof m->p0_src), memcpy(m->p0_live, p0_live, sizeof m->p0_live);
    m->kappa = kappa;
    m->rot = sv_wrap_pi(yaw0_live - yaw0_src + kappa);
}

void somvr_ir_map_pose(const somvr_ir_map *m, const float p[3], const float q[4], float op[3], float oq[4]) {
    sv_vec3 g = sv_xr_to_game_pos(p);
    sv_quat r = sv_qyaw(m->rot);
    sv_vec3 d = sv_qrot(r, (sv_vec3){ g.x - m->p0_src[0], g.y - m->p0_src[1], g.z - m->p0_src[2] });
    sv_game_to_xr_pos((sv_vec3){ m->p0_live[0] + d.x, m->p0_live[1] + d.y, m->p0_live[2] + d.z }, op);
    sv_quat gq = sv_xr_to_game_quat(q);
    sv_game_to_xr_quat(sv_qnormalize(sv_qmul(r, gq)), oq);
}

/* in XR space the game's turn by rot about +Y is a turn by -rot (M R_y(a) M = R_y(-a)); polar and axial vectors alike
 * (a proper rotation) */
void somvr_ir_map_vec(const somvr_ir_map *m, const float v[3], float o[3]) {
    sv_vec3 r = sv_qrot(sv_qyaw(-m->rot), (sv_vec3){ v[0], v[1], v[2] });
    o[0] = r.x, o[1] = r.y, o[2] = r.z;
}

void somvr_ir_map_frame(const somvr_ir_map *m, somvr_ir_frame *f) {
    for (int e = 0; e < 2; e++)
        somvr_ir_map_pose(m, f->eye_p[e], f->eye_q[e], f->eye_p[e], f->eye_q[e]);
    for (int h = 0; h < 2; h++) {
        somvr_ir_hand *o = &f->hand[h];
        if (o->valid) {
            somvr_ir_map_pose(m, o->p, o->q, o->p, o->q);
            somvr_ir_map_vec(m, o->lin, o->lin), somvr_ir_map_vec(m, o->ang, o->ang);
        }
        if (o->aim_flags & 3)
            somvr_ir_map_pose(m, o->aim_p, o->aim_q, o->aim_p, o->aim_q);
    }
}
