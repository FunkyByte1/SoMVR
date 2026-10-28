/* somvr_wstep.c - build 18: the wraith step (somvr_wstep.h; docs/re-xr-pipeline.md 68). Pure: no globals. */
#include "somvr_wstep.h"

#include <math.h>
#include <string.h>

void somvr_ws_cfg_default(somvr_ws_cfg *c)
{
    memset(c, 0, sizeof *c);
    c->max_cm = 700, c->min_cm = 120, c->rise_cm = 150, c->drop_cm = 150, c->room_cm = 180, c->side_cm = 30;
    c->orc_cm = 100, c->ray_cm = 1500, c->stance_cm = 35;
    c->on = 0.7f, c->off = 0.3f, c->cancel = 0.5f, c->rest = 0.3f, c->min_s = 0.25f, c->cool_s = 1.0f;
}

const char *somvr_ws_why_name(int why)
{
    static const char *const n[] = { "ok", "at the feet", "a wall", "a ledge", "too high", "water", "no room for Talion",
                                     "an orc there", "out of sight", "no answer from the cast" };
    return why >= 0 && why < SOMVR_WSW_N ? n[why] : "?";
}

static sv_vec3 v_add(sv_vec3 a, sv_vec3 b) { return (sv_vec3){ a.x + b.x, a.y + b.y, a.z + b.z }; }
static sv_vec3 v_mul(sv_vec3 a, float k) { return (sv_vec3){ a.x * k, a.y * k, a.z * k }; }
static float v_len(sv_vec3 a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }

/* a cast that goes on through orcs (their bodies: hit.chr); 1 = it ran, *h the first other hit (or none) */
static int ws_ray_through(somvr_climb_ray_fn ray, void *ctx, sv_vec3 from, sv_vec3 dir, float len, somvr_climb_hit *h,
                          int *through, int *rays)
{
    float s = 0;
    for (int k = 0; k < 4; k++) {
        memset(h, 0, sizeof *h);
        if (!ray(ctx, -1, v_add(from, v_mul(dir, s)), dir, len - s, h))
            return 0;
        (*rays)++;
        if (!h->hit || !h->chr) {
            if (h->hit)
                h->dist += s;
            return 1;
        }
        (*through)++;
        s += h->dist + 60.0f; /* past the orc's body */
        if (s >= len)
            break;
    }
    memset(h, 0, sizeof *h);
    return 1;
}

static int ws_set(somvr_ws_aim *o, int kind, int why, sv_vec3 at, sv_vec3 root, float floor_y)
{
    o->kind = kind, o->why = why, o->land = at;
    o->dist_cm = hypotf(at.x - root.x, at.z - root.z), o->rise_cm = at.y - floor_y;
    return kind;
}

int somvr_ws_find(const somvr_ws_cfg *c, somvr_climb_ray_fn ray, void *ctx, sv_vec3 eye, sv_vec3 gaze, sv_vec3 root,
                  float floor_y, const sv_vec3 *orcs, int n_orcs, somvr_ws_aim *o)
{
    memset(o, 0, sizeof *o);
    o->land = root;
    float gl = v_len(gaze);
    if (!(gl > 0.5f) || !isfinite(gl + eye.x + eye.y + eye.z + root.x + root.y + root.z + floor_y))
        return o->why = SOMVR_WSW_NORAY, SOMVR_WSA_NONE;
    sv_vec3 g = v_mul(gaze, 1.0f / gl);
    float hl = hypotf(g.x, g.z);
    if (hl < 0.05f) /* straight down (or up): at the feet */
        return o->why = SOMVR_WSW_NEAR, SOMVR_WSA_NONE;
    sv_vec3 hd = { g.x / hl, 0, g.z / hl };
    somvr_climb_hit h;
    /* 1. where the head's ray meets something other than an orc */
    if (!ws_ray_through(ray, ctx, eye, g, c->ray_cm, &h, &o->through, &o->rays))
        return o->why = SOMVR_WSW_NORAY, SOMVR_WSA_NONE;
    int far = !h.hit || hypotf(h.point.x - root.x, h.point.z - root.z) > c->max_cm;
    sv_vec3 q;      /* the down ray's top */
    float top_y;    /* what was looked at (a wall's face is far over the ground under it) */
    if (far) {
        q = (sv_vec3){ root.x + hd.x * c->max_cm, floor_y + c->rise_cm + 60.0f, root.z + hd.z * c->max_cm };
        top_y = -1e30f;
        o->far = 1;
    } else {
        q = (sv_vec3){ h.point.x - hd.x * 15.0f, h.point.y + 40.0f, h.point.z - hd.z * 15.0f };
        top_y = h.point.y;
    }
    /* 2. the ground under it */
    float down = q.y - (floor_y - c->drop_cm) + 40.0f;
    if (down < 80.0f)
        down = 80.0f;
    memset(&h, 0, sizeof h);
    if (!ray(ctx, -1, q, (sv_vec3){ 0, -1, 0 }, down, &h))
        return o->why = SOMVR_WSW_NORAY, SOMVR_WSA_NONE;
    o->rays++;
    if (h.hit && h.chr) /* an orc's body over the spot */
        return ws_set(o, SOMVR_WSA_NO, SOMVR_WSW_ORC, (sv_vec3){ q.x, far ? floor_y : top_y, q.z }, root, floor_y);
    if (!h.hit) /* nothing under it within drop_cm of the floor: a drop */
        return ws_set(o, SOMVR_WSA_NO, SOMVR_WSW_LEDGE, (sv_vec3){ q.x, floor_y, q.z }, root, floor_y);
    sv_vec3 gp = h.point;
    if (top_y > gp.y + 30.0f) /* the ray met a face standing over the ground in front of it (near too: a wall ahead) */
        return ws_set(o, SOMVR_WSA_NO, SOMVR_WSW_WALL, gp, root, floor_y);
    if (hypotf(gp.x - root.x, gp.z - root.z) < c->min_cm)
        return ws_set(o, SOMVR_WSA_NONE, SOMVR_WSW_NEAR, gp, root, floor_y);
    if (h.wet)
        return ws_set(o, SOMVR_WSA_NO, SOMVR_WSW_WATER, gp, root, floor_y);
    if (gp.y - floor_y > c->rise_cm)
        return ws_set(o, SOMVR_WSA_NO, SOMVR_WSW_HIGH, gp, root, floor_y);
    if (floor_y - gp.y > c->drop_cm)
        return ws_set(o, SOMVR_WSA_NO, SOMVR_WSW_LEDGE, gp, root, floor_y);
    /* 3. room for Talion: nothing over the spot, nothing close round it at the chest */
    memset(&h, 0, sizeof h);
    if (!ray(ctx, -1, (sv_vec3){ gp.x, gp.y + 5.0f, gp.z }, (sv_vec3){ 0, 1, 0 }, c->room_cm, &h))
        return o->why = SOMVR_WSW_NORAY, SOMVR_WSA_NONE;
    o->rays++;
    if (h.hit && !h.chr)
        return ws_set(o, SOMVR_WSA_NO, SOMVR_WSW_ROOM, gp, root, floor_y);
    const sv_vec3 sd[4] = { hd, { -hd.x, 0, -hd.z }, { hd.z, 0, -hd.x }, { -hd.z, 0, hd.x } };
    for (int k = 0; k < 4; k++) {
        memset(&h, 0, sizeof h);
        if (!ray(ctx, -1, (sv_vec3){ gp.x, gp.y + 100.0f, gp.z }, sd[k], c->side_cm, &h))
            return o->why = SOMVR_WSW_NORAY, SOMVR_WSA_NONE;
        o->rays++;
        if (h.hit && !h.chr)
            return ws_set(o, SOMVR_WSA_NO, SOMVR_WSW_ROOM, gp, root, floor_y);
    }
    /* 3b. ground round the spot (a stance, not a lip: the run-184609 plateau's edge gave one frame of ground 7 m out) */
    for (int k = 0; k < 4; k++) {
        sv_vec3 f = { gp.x + sd[k].x * c->stance_cm, gp.y + 50.0f, gp.z + sd[k].z * c->stance_cm };
        memset(&h, 0, sizeof h);
        if (!ray(ctx, -1, f, (sv_vec3){ 0, -1, 0 }, 90.0f, &h))
            return o->why = SOMVR_WSW_NORAY, SOMVR_WSA_NONE;
        o->rays++;
        if (!h.hit) /* (an orc's body beside it is no lip: an orc there is refused below) */
            return ws_set(o, SOMVR_WSA_NO, SOMVR_WSW_LEDGE, gp, root, floor_y);
    }
    /* 4. no orc standing there */
    for (int i = 0; i < n_orcs; i++)
        if (hypotf(orcs[i].x - gp.x, orcs[i].z - gp.z) < c->orc_cm && fabsf(orcs[i].y - gp.y) < 250.0f)
            return ws_set(o, SOMVR_WSA_NO, SOMVR_WSW_ORC, gp, root, floor_y);
    /* 5. in the eyes' sight (standing there: 100 cm over the ground; orcs are seen through) */
    sv_vec3 st = { gp.x, gp.y + 100.0f, gp.z }, d = { st.x - eye.x, st.y - eye.y, st.z - eye.z };
    float dl = v_len(d);
    if (dl > 40.0f) {
        int th = 0;
        if (!ws_ray_through(ray, ctx, eye, v_mul(d, 1.0f / dl), dl - 30.0f, &h, &th, &o->rays))
            return o->why = SOMVR_WSW_NORAY, SOMVR_WSA_NONE;
        if (h.hit)
            return ws_set(o, SOMVR_WSA_NO, SOMVR_WSW_SIGHT, gp, root, floor_y);
    }
    return ws_set(o, SOMVR_WSA_OK, SOMVR_WSW_OK, gp, root, floor_y);
}

void somvr_ws_init(somvr_ws_state *s) { memset(s, 0, sizeof *s); }

uint32_t somvr_ws_tick(somvr_ws_state *s, const somvr_ws_cfg *c, const somvr_ws_in *in)
{
    uint32_t ev = 0;
    float r = hypotf(in->rx, in->ry);
    int fwd = in->world && in->ry >= c->on && fabsf(in->rx) <= 0.6f * in->ry, rest = !(r >= c->off); /* within 31 deg */
    if (s->walk_wait && !(in->move >= c->rest))
        s->walk_wait = 0;
    switch (s->phase) {
    case SOMVR_WSP_IDLE:
        if (!fwd)
            break;
        if (!in->can) {
            s->refused = 2, s->n_cannot++, s->phase = SOMVR_WSP_REST;
            return SOMVR_WSE_REFUSED;
        }
        if (in->t < s->next_ok) {
            s->refused = 1, s->n_cool++, s->phase = SOMVR_WSP_REST;
            return SOMVR_WSE_REFUSED;
        }
        s->phase = SOMVR_WSP_AIM, s->t0 = in->t, s->have_aim = 0, s->n_aim++;
        memset(&s->aim, 0, sizeof s->aim);
        ev |= SOMVR_WSE_AIM;
        break;
    case SOMVR_WSP_AIM:
        if (!in->world || !in->can) {
            s->phase = rest ? SOMVR_WSP_IDLE : SOMVR_WSP_REST, s->n_lost++, s->have_aim = 0;
            return SOMVR_WSE_LOST;
        }
        if (in->move >= c->cancel) {
            s->phase = rest ? SOMVR_WSP_IDLE : SOMVR_WSP_REST, s->walk_wait = 1, s->n_cancel++, s->have_aim = 0;
            return SOMVR_WSE_CANCEL;
        }
        if (rest) {
            s->held_s = (float)(in->t - s->t0);
            s->phase = SOMVR_WSP_IDLE;
            if (s->held_s >= c->min_s && s->have_aim && s->aim.kind == SOMVR_WSA_OK) {
                s->next_ok = in->t + c->cool_s, s->n_go++;
                return SOMVR_WSE_GO;
            }
            s->n_nogo++, s->n_tap += s->held_s < c->min_s, s->have_aim = 0;
            return SOMVR_WSE_NOGO;
        }
        break;
    case SOMVR_WSP_REST:
        if (rest)
            s->phase = SOMVR_WSP_IDLE;
        break;
    }
    return ev;
}

uint32_t somvr_ws_aimed(somvr_ws_state *s, const somvr_ws_aim *a)
{
    if (s->phase != SOMVR_WSP_AIM)
        return 0;
    int was = s->have_aim ? s->aim.kind : SOMVR_WSA_NONE;
    s->aim = *a, s->have_aim = 1;
    if (a->kind == was)
        return 0;
    if (a->kind == SOMVR_WSA_OK)
        return SOMVR_WSE_TURN_OK;
    if (a->kind == SOMVR_WSA_NO)
        return s->n_turn_no++, SOMVR_WSE_TURN_NO;
    return 0;
}

int somvr_ws_walk_ok(const somvr_ws_state *s) { return s->phase != SOMVR_WSP_AIM && !s->walk_wait; }
