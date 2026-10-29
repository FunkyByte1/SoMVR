/* somvr_jump.c - build 22: the jump and the vault (somvr_jump.h; docs/re-xr-pipeline.md 91). Pure: no globals. */
#include "somvr_jump.h"

#include <math.h>
#include <string.h>

void somvr_jp_cfg_default(somvr_jp_cfg *c)
{
    memset(c, 0, sizeof *c);
    c->jump_cm = 60, c->g = 2000, c->soft_cm = 12, c->settle_cm = 2, c->air_max_s = 3, c->drop_max_cm = 1500;
    c->moving_cms = 100, c->foot_cm = 102, c->ground_cm = 25;
    c->v_min_cm = 45, c->v_max_cm = 120, c->v_clear_cm = 12, c->v_reach_cm = 100, c->v_reach_s = 0.4f;
    c->v_reach_max_cm = 260, c->v_depth_cm = 100, c->v_in_cm = 45, c->v_past_cm = 55, c->v_drop_cm = 150;
    c->v_cms = 300, c->v_cms_max = 700, c->v_cms_min = 120, c->v_rise_max_cm = 190, c->radius_cm = 35;
    c->v_ring_stop = 1;
    c->room_cm = 180, c->side_cm = 33;
}

const char *somvr_jp_kind_name(int k)
{
    static const char *const n[] = { "none", "jump", "vault" };
    return k >= 0 && k <= SOMVR_JPK_VAULT ? n[k] : "?";
}

const char *somvr_jp_end_name(int e)
{
    static const char *const n[] = { "-", "landed", "lost (LOCO gave the tick to the game)", "caught by the climb",
                                     "the game's fall", "stopped" };
    return e >= 0 && e < SOMVR_JPE_N ? n[e] : "?";
}

const char *somvr_jp_vwhy_name(int why)
{
    static const char *const n[] = { "ok", "nothing ahead", "a step (he walks over it)", "too high (the climb's)",
                                     "no top over the face", "no landing past it", "no room for Talion at the landing",
                                     "the way over is not clear", "no arc clears it", "no answer from a cast",
                                     "a slope (no edge)" };
    return why >= 0 && why < SOMVR_JPV_N ? n[why] : "?";
}

const char *somvr_jp_a_name(int a)
{
    static const char *const n[] = { "nobody", "the vault", "the jump", "the climb, then the jump", "the climb" };
    return a >= 0 && a < SOMVR_JPA_N ? n[a] : "?";
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float hlen(sv_vec3 a) { return sqrtf(a.x * a.x + a.z * a.z); }
static sv_vec3 v_add(sv_vec3 a, sv_vec3 b) { return (sv_vec3){ a.x + b.x, a.y + b.y, a.z + b.z }; }
static sv_vec3 v_mul(sv_vec3 a, float k) { return (sv_vec3){ a.x * k, a.y * k, a.z * k }; }
static sv_vec3 v_sub(sv_vec3 a, sv_vec3 b) { return (sv_vec3){ a.x - b.x, a.y - b.y, a.z - b.z }; }
static float v_len(sv_vec3 a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }

float somvr_jp_takeoff(float h, float g) { return h > 0 && g > 0 ? sqrtf(2 * g * h) : 0; }
float somvr_jp_arc_dy(float vy, float g, float dt) { return vy * dt - 0.5f * g * dt * dt; }

/* ---- the runner ----------------------------------------------------------------------------------------------------- */
void somvr_jp_init(somvr_jp *s) { memset(s, 0, sizeof *s); }

void somvr_jp_start(somvr_jp *s, int kind, sv_vec3 root, float floor_y, float vy0, float g, sv_vec3 vh, sv_vec3 land,
                    float hold_t)
{
    s->hold_t = kind == SOMVR_JPK_VAULT && hold_t > 0 ? hold_t : 0, s->miss_floor = 1e9f, s->holding = 0;
    s->kind = kind, s->t = 0, s->vy = vy0, s->g = g > 0 ? g : 2000, s->start = root, s->floor0 = floor_y;
    s->vh = kind == SOMVR_JPK_VAULT ? (sv_vec3){ vh.x, 0, vh.z } : (sv_vec3){ 0, 0, 0 };
    s->land = land, s->asked_dy = 0, s->asked_h = (sv_vec3){ 0, 0, 0 }, s->root_last = root, s->have_root = 0;
    s->apex_y = root.y, s->brake_v = 0, s->brake_a = 0, s->settling = 0, s->settle_step = 0, s->ceiling = 0, s->h_blocked = 0;
    s->h_done = 0;
    s->v_land = 0, s->end = SOMVR_JPE_NONE;
    if (kind == SOMVR_JPK_VAULT)
        s->n_vault++;
    else
        s->n_jump++;
}

static void jp_end(somvr_jp *s, int end, somvr_jp_out *o)
{
    s->end = end, s->kind = SOMVR_JPK_NONE;
    s->n_landed += end == SOMVR_JPE_LANDED, s->n_lost += end == SOMVR_JPE_LOST, s->n_climb += end == SOMVR_JPE_CLIMB;
    s->n_fall += end == SOMVR_JPE_FALL, s->n_stop += end == SOMVR_JPE_STOP;
    s->max_air_s = fmaxf(s->max_air_s, s->t);
    s->max_rise_cm = fmaxf(s->max_rise_cm, s->apex_y - s->start.y);
    if (o)
        o->ended = 1;
}

void somvr_jp_stop(somvr_jp *s, int end)
{
    if (s->kind != SOMVR_JPK_NONE)
        jp_end(s, end, NULL);
}

void somvr_jp_step(somvr_jp *s, const somvr_jp_cfg *c, const somvr_jp_in *in, somvr_jp_out *o)
{
    memset(o, 0, sizeof *o);
    if (s->kind == SOMVR_JPK_NONE)
        return;
    s->ticks++;
    if (in->climbing) { /* a grip caught a hold: the climb's from here (its gravity, its motion) */
        jp_end(s, SOMVR_JPE_CLIMB, o);
        return;
    }
    if (!in->owned) {
        jp_end(s, SOMVR_JPE_LOST, o);
        return;
    }
    float dt = isfinite(in->dt) ? clampf(in->dt, 0, 0.1f) : 0;
    /* the read-back: what the last tick's step really did */
    if (in->have_root && s->have_root) {
        float ady = in->root.y - s->root_last.y;
        if (s->asked_dy < -0.3f && (ady > 0.5f * s->asked_dy || (s->settle_step && ady > s->asked_dy + 0.3f))) {
            /* the step down blocked: the floor (the settle step: any of it blocked - the proxy's contact sits a cm or two
             * under the rays' floor, run 001108) */
            if (!(s->brake_v > 0) && !s->settling)
                s->v_land = -s->vy; /* a hard stop (the rays missed this floor) */
            s->max_land_cms = fmaxf(s->max_land_cms, s->settling ? 0 : -s->vy);
            jp_end(s, SOMVR_JPE_LANDED, o);
            return;
        }
        if (s->asked_dy > 0.3f && ady < 0.5f * s->asked_dy && s->vy > 0) { /* the rise blocked: a ceiling */
            s->vy = 0, s->ceiling = 1, s->n_ceiling++;
            o->ceiling = 1;
        }
        if (s->settle_step && s->asked_dy < -0.3f) { /* the step down went on: the floor is lower than the rays said */
            s->settling = 0, s->brake_v = 0, s->vy = 0, s->n_settle_miss++;
            s->miss_floor = fminf(s->miss_floor, in->floor_ok ? in->floor_y : s->miss_floor); /* ignored from now */
        }
        float ah = hlen(s->asked_h);
        if (s->kind == SOMVR_JPK_VAULT && ah > 0.5f) { /* the vault's own horizontal step blocked twice: straight down */
            float done = ((in->root.x - s->root_last.x) * s->asked_h.x + (in->root.z - s->root_last.z) * s->asked_h.z) / ah;
            s->h_blocked = done < 0.5f * ah ? s->h_blocked + 1 : 0;
            if (s->h_blocked >= 2)
                s->vh = (sv_vec3){ 0, 0, 0 };
        }
    }
    if (in->have_root) {
        s->root_last = in->root, s->have_root = 1;
        s->apex_y = fmaxf(s->apex_y, in->root.y);
    }
    s->t += dt;
    if (s->t > c->air_max_s || (in->have_root && in->root.y - c->foot_cm < s->floor0 - c->drop_max_cm)) {
        jp_end(s, SOMVR_JPE_FALL, o);
        return;
    }
    float g = s->g, dy;
    s->settle_step = s->settling;
    if (s->settling) { /* the brake reached the floor the rays found: a small step down finds it */
        dy = -c->settle_cm, s->vy = 0;
    } else {
        float dyf = somvr_jp_arc_dy(s->vy, g, dt), vyf = s->vy - g * dt;
        if (s->hold_t > 0 && s->t - dt < s->hold_t && (vyf <= 0 || s->holding)) { /* the vault: the apex held level */
            if (!s->holding) /* this tick reaches the apex: the rest of the rise, then level */
                dyf = s->vy > 0 ? s->vy * s->vy / (2 * g) : 0, s->holding = 1;
            else if (s->t > s->hold_t) /* the hold ends inside this tick: its fall's part */
                dyf = -0.5f * g * (s->t - s->hold_t) * (s->t - s->hold_t);
            else
                dyf = 0;
            vyf = s->t > s->hold_t ? -g * (s->t - s->hold_t) : 0;
        }
        int fl_ok = in->floor_ok && in->floor_y < s->miss_floor - 1.0f; /* not the floor the settle went through */
        float d = in->have_root && fl_ok ? in->root.y - c->foot_cm - in->floor_y : 1e9f; /* the feet over it */
        float v = -s->vy;                                                                         /* down, now */
        if (!(s->brake_v > 0) && vyf < 0 && d < 1e8f && d > 0.5f && d + dyf < c->soft_cm && v > 0 &&
            v * v >= 2.0f * g * fmaxf(d, 0.1f)) /* the brake starts where it needs more than gravity to stop on the floor */
            s->brake_v = v, s->brake_a = v * v / (2.0f * fmaxf(d, 0.1f)), s->v_land = v, s->n_brakes++;
        if (s->brake_v > 0) {
            float a = s->brake_a, step = v * dt - 0.5f * a * dt * dt, v1 = v - a * dt;
            if (d < 1e8f && (v1 <= 0 || step >= d - 0.05f)) /* arrived: on the floor's height */
                step = fmaxf(d, 0), v1 = 0, s->settling = 1;
            dy = -fmaxf(step, 0), s->vy = -fmaxf(v1, 0);
            o->braking = 1;
        } else {
            dy = dyf, s->vy = vyf;
        }
    }
    s->asked_dy = dy;
    o->active = 1, o->dy = dy, o->vy = s->vy;
    if (s->kind == SOMVR_JPK_VAULT && (s->vh.x != 0 || s->vh.z != 0)) {
        sv_vec3 dh = { s->vh.x * dt, 0, s->vh.z * dt };
        float vl = hlen(s->vh);
        if (c->v_ring_stop && in->have_root && vl > 1.0f) {
            /* build 24 (boot 20261029-141005: the far floor ran downhill, the settle missed it and the fixed horizontal
             * went on 0.25 s - 55 cm past the ring): never past the ring's distance; the rest of the fall straight down */
            float L = hlen(v_sub(s->land, s->start)), st = vl * dt;
            float left = L - ((in->root.x - s->start.x) * s->vh.x + (in->root.z - s->start.z) * s->vh.z) / vl;
            if (s->h_done || left <= 0.5f) {
                if (!s->h_done)
                    s->n_ring_stop++;
                s->h_done = 1, dh = (sv_vec3){ 0, 0, 0 };
            } else if (left < st) {
                dh = v_mul(dh, left / st);
            }
        }
        o->fixed_h = 1, o->dh = dh; /* (at the ring: no horizontal, the stick still ignored until the landing) */
        s->asked_h = dh;
    } else {
        s->asked_h = (sv_vec3){ 0, 0, 0 };
    }
}

/* ---- the vault's plan ------------------------------------------------------------------------------------------------ */
#define JV_SAMPLES 16 /* the top's down rays, 10 cm apart */
#define JV_WAY 8      /* the arc's pieces for the way's rays */
static int jv_cast(somvr_climb_ray_fn ray, void *ctx, sv_vec3 from, sv_vec3 dir, float len, somvr_climb_hit *h, int *rays)
{
    memset(h, 0, sizeof *h);
    (*rays)++;
    if (!ray(ctx, -1, from, dir, len, h))
        return 0;
    if (h->hit && h->chr) /* an orc's body is no obstacle to vault (the phase is the fights') */
        h->hit = 0;
    return 1;
}

float somvr_jp_vault_feet(const somvr_jp_cfg *c, const somvr_jp_vplan *p, float u)
{
    (void)c;
    float vh = hlen(p->vh);
    if (!(vh > 1))
        return p->feet0;
    float t = u / vh, g = p->g;
    if (t <= p->t_up)
        return p->feet0 + p->vy0 * t - 0.5f * g * t * t;
    if (t <= p->t_hold)
        return p->apex_cm;
    return p->apex_cm - 0.5f * g * (t - p->t_hold) * (t - p->t_hold);
}

/* the profile for the horizontal speed vh: the rise to the apex H (feet over F0), held level until the body is past
 * hold_u (the top's far zone), then the fall; 1 = the feet clear every top sample (us[k], ys[k] over F0) by v_clear_cm at
 * u and by v_clear_cm - 7 at u +- 0.6 radius (the capsule's round foot) */
/* the time from the end of the level glide to the floor h cm under the apex: the free fall to soft_cm over it, then the
 * brake's constant deceleration to 0 (somvr_jp_step's soft landing) */
static float jv_fall_t(const somvr_jp_cfg *c, float g, float h)
{
    if (!(h > 0))
        return 0;
    if (h <= c->soft_cm) /* all in the brake's zone: free to half of it, braked the other half */
        return 2 * sqrtf(h / g);
    float tf = sqrtf(2 * (h - c->soft_cm) / g), ve = g * tf;
    return tf + 2 * c->soft_cm / ve;
}

static int jv_arc(const somvr_jp_cfg *c, somvr_jp_vplan *p, sv_vec3 dir, float vh, float H, float hold_u, const float *us,
                  const float *ys, int n)
{
    p->vh = (sv_vec3){ dir.x * vh, 0, dir.z * vh }, p->apex_cm = H;
    p->vy0 = H > p->feet0 ? sqrtf(2 * p->g * (H - p->feet0)) : 0, p->t_up = p->vy0 / p->g;
    p->t_hold = fmaxf(p->t_up, hold_u / vh);
    float r6 = 0.6f * c->radius_cm, least = 1e9f;
    for (int k = 0; k < n; k++) {
        float e = somvr_jp_vault_feet(c, p, us[k]) - ys[k];
        least = fminf(least, e);
        if (e < c->v_clear_cm - 0.01f)
            return 0;
        for (int j = -1; j <= 1; j += 2) {
            float u = us[k] + j * r6;
            if (u > 0 && somvr_jp_vault_feet(c, p, u) - ys[k] < c->v_clear_cm - 7.0f)
                return 0;
        }
    }
    p->clear_cm = least;
    return 1;
}

int somvr_jp_vault_find(const somvr_jp_cfg *c, somvr_climb_ray_fn ray, void *ctx, sv_vec3 root, float floor_y,
                        sv_vec3 dir, float speed, float g, somvr_jp_vplan *p)
{
    memset(p, 0, sizeof *p);
    p->g = g > 0 ? g : c->g;
    dir.y = 0;
    float dl = hlen(dir);
    if (!(dl > 0.3f) || !isfinite(root.x + root.y + root.z + floor_y))
        return p->why = SOMVR_JPV_NONE, 0;
    dir = v_mul(dir, 1 / dl);
    float F0 = floor_y, reach = clampf(c->v_reach_cm + fmaxf(speed, 0) * c->v_reach_s, c->v_reach_cm, c->v_reach_max_cm);
    p->feet0 = root.y - c->foot_cm - F0;
    somvr_climb_hit h;
    /* the face: forward from the axis at the knees .. the waist */
    float face = 1e9f;
    static const float hs[] = { 25, 50, 75, 100 };
    for (unsigned i = 0; i < sizeof hs / sizeof hs[0]; i++) {
        if (hs[i] > c->v_max_cm + 5)
            break;
        if (!jv_cast(ray, ctx, (sv_vec3){ root.x, F0 + hs[i], root.z }, dir, reach, &h, &p->rays))
            return p->why = SOMVR_JPV_NOANSWER, 0;
        if (h.hit)
            face = fminf(face, h.dist);
    }
    if (!(face <= reach))
        return p->why = SOMVR_JPV_NONE, 0;
    p->face_cm = face;
    /* a wall over the highest top a vault takes, right behind the face = too high (the climb's, the jump's) */
    if (!jv_cast(ray, ctx, (sv_vec3){ root.x, F0 + c->v_max_cm + 35, root.z }, dir, face + 30, &h, &p->rays))
        return p->why = SOMVR_JPV_NOANSWER, 0;
    if (h.hit)
        return p->why = SOMVR_JPV_HIGH, 0;
    /* the top: down rays every 10 cm from 3 cm past the face, from over the highest top; the span = while the floor
     * is over half the least top (the far side's floor ends it) */
    float y_from = F0 + c->v_max_cm + 30, us[JV_SAMPLES], ys[JV_SAMPLES], top = -1e9f, far_u = -1, span_end = -1;
    int n = 0, ns = (int)((c->v_depth_cm + 10) / 10) + 1;
    if (ns > JV_SAMPLES)
        ns = JV_SAMPLES;
    for (int k = 0; k < ns; k++) {
        float u = face + 3 + 10.0f * k;
        sv_vec3 f = v_add(root, v_mul(dir, u));
        f.y = y_from;
        if (!jv_cast(ray, ctx, f, (sv_vec3){ 0, -1, 0 }, y_from - (F0 - c->v_drop_cm - 20), &h, &p->rays))
            return p->why = SOMVR_JPV_NOANSWER, 0;
        float y = h.hit ? h.point.y - F0 : -1e9f;
        if (k == 0 && !(y >= 0.5f * c->v_min_cm)) /* the face's top is low (a slope's foot, a step) */
            return p->why = y >= -30 ? SOMVR_JPV_LOW : SOMVR_JPV_NOTOP, 0;
        if (y < 0.5f * c->v_min_cm || h.wet) { /* past the far edge */
            far_u = u, span_end = u - 5;
            break;
        }
        us[n] = u, ys[n] = y, n++;
        top = fmaxf(top, y);
    }
    p->top_cm = top, p->top_y = F0 + top;
    if (top < c->v_min_cm)
        return p->why = SOMVR_JPV_LOW, 0;
    if (top > c->v_max_cm)
        return p->why = SOMVR_JPV_HIGH, 0;
    /* an obstacle has an edge: its top reached at the face (the first sample within 15 cm of it) and the floor just
     * before the face still the start's (run 032104: the meadow's rising slopes passed as 45-62 cm "tops onto") */
    if (ys[0] < top - 15.0f)
        return p->why = SOMVR_JPV_SLOPE, 0;
    {
        sv_vec3 f = v_add(root, v_mul(dir, fmaxf(face - 8.0f, 0)));
        f.y = F0 + 40.0f;
        if (!jv_cast(ray, ctx, f, (sv_vec3){ 0, -1, 0 }, 80.0f, &h, &p->rays))
            return p->why = SOMVR_JPV_NOANSWER, 0;
        if (h.hit && h.point.y > F0 + 15.0f)
            return p->why = SOMVR_JPV_SLOPE, 0;
    }
    /* the arc: the body's speed (at least v_cms), slower down to v_cms_min until its rise clears the near edge; the apex
     * the top + v_clear_cm, held level until past the far zone (over) or the near zone (onto), then the fall */
    float H = top + c->v_clear_cm, r6 = 0.6f * c->radius_cm, u_far = far_u >= 0 ? span_end : face + c->v_depth_cm;
    if (H > c->v_rise_max_cm)
        return p->why = SOMVR_JPV_HIGH, 0;
    int ne = n;
    if (far_u < 0) { /* onto: only the near zone needs the clearance; he comes down onto the rest */
        p->kind = SOMVR_JPO_ONTO, p->depth_cm = 10.0f * ns;
        for (ne = 0; ne < n && us[ne] <= face + r6 + 3;)
            ne++;
        if (ne < 1)
            ne = 1;
    } else {
        p->kind = SOMVR_JPO_OVER, p->depth_cm = span_end - face;
    }
    float hold_u = far_u >= 0 ? span_end + r6 : us[ne - 1] + r6, vh = clampf(speed, c->v_cms, c->v_cms_max), y2 = 0;
    int ok = 0;
    for (; vh >= c->v_cms_min - 0.5f; vh *= 0.88f)
        if ((ok = jv_arc(c, p, dir, vh, H, hold_u, us, ys, ne)))
            break;
    if (!ok)
        return p->why = SOMVR_JPV_NOARC, 0;
    sv_vec3 land = root;
    if (far_u < 0) { /* onto: the floor on the top where the fall from the apex comes down */
        y2 = top;
        float L = vh * (p->t_hold + jv_fall_t(c, p->g, H - y2));
        int k = (int)((L - face - 3) / 10 + 0.5f);
        k = k < 0 ? 0 : k >= n ? n - 1 : k;
        if (L > u_far)
            return p->why = SOMVR_JPV_NOFLOOR, 0;
        land = v_add(root, v_mul(dir, L));
        land.y = F0 + ys[k];
    } else { /* over: the far floor where the fall comes down (the fall's time from it: found again when it differs) */
        int found = 0;
        for (int it = 0; it < 3 && !found; it++) {
            float L = vh * (p->t_hold + jv_fall_t(c, p->g, H - y2));
            sv_vec3 f = v_add(root, v_mul(dir, L));
            f.y = F0 + top + 40;
            if (!jv_cast(ray, ctx, f, (sv_vec3){ 0, -1, 0 }, top + 40 + c->v_drop_cm + 20, &h, &p->rays))
                return p->why = SOMVR_JPV_NOANSWER, 0;
            if (!h.hit || h.wet || h.point.y < F0 - c->v_drop_cm || h.point.y > F0 + top - 20)
                return p->why = SOMVR_JPV_NOFLOOR, 0;
            land = h.point;
            found = fabsf(h.point.y - F0 - y2) <= 8.0f;
            y2 = h.point.y - F0;
        }
        if (!found)
            return p->why = SOMVR_JPV_NOFLOOR, 0;
    }
    float L = hlen(v_sub(land, root));
    p->T = p->t_hold + jv_fall_t(c, p->g, H - y2);
    /* room at the landing: nothing room_cm over it, nothing side_cm round it at 50 and 140 cm */
    if (!jv_cast(ray, ctx, v_add(land, (sv_vec3){ 0, 10, 0 }), (sv_vec3){ 0, 1, 0 }, c->room_cm, &h, &p->rays))
        return p->why = SOMVR_JPV_NOANSWER, 0;
    if (h.hit)
        return p->why = SOMVR_JPV_NOROOM, 0;
    sv_vec3 side = { -dir.z, 0, dir.x };
    for (int hh = 0; hh < 2; hh++) {
        sv_vec3 at = v_add(land, (sv_vec3){ 0, hh ? 140.0f : 50.0f, 0 });
        const sv_vec3 ds[4] = { dir, v_mul(dir, -1), side, v_mul(side, -1) };
        for (int j = 0; j < 4; j++) {
            if (!jv_cast(ray, ctx, at, ds[j], c->side_cm, &h, &p->rays))
                return p->why = SOMVR_JPV_NOANSWER, 0;
            if (h.hit)
                return p->why = SOMVR_JPV_NOROOM, 0;
        }
    }
    p->land = land, p->land_y = land.y + c->foot_cm + 1;
    /* the way: the feet + 15 and the eyes (the root + 77) along the arc's pieces */
    for (int i = 0; i < JV_WAY; i++) {
        float u0 = L * i / JV_WAY, u1 = L * (i + 1) / JV_WAY;
        for (int k = 0; k < 2; k++) {
            float up = k ? c->foot_cm + 77 : 15;
            sv_vec3 a = v_add(root, v_mul(dir, u0)), b = v_add(root, v_mul(dir, u1));
            a.y = F0 + somvr_jp_vault_feet(c, p, u0) + up, b.y = F0 + somvr_jp_vault_feet(c, p, u1) + up;
            sv_vec3 d = v_sub(b, a);
            float len = v_len(d);
            if (!(len > 0.5f))
                continue;
            if (!jv_cast(ray, ctx, a, v_mul(d, 1 / len), len, &h, &p->rays))
                return p->why = SOMVR_JPV_NOANSWER, 0;
            if (h.hit)
                return p->why = SOMVR_JPV_NOWAY, 0;
        }
    }
    return p->why = SOMVR_JPV_OK, 1;
}

/* ---- A's press ------------------------------------------------------------------------------------------------------- */
int somvr_jp_a_pick(const somvr_jp_cfg *c, const somvr_jp_a_in *in)
{
    if (in->climbing)
        return SOMVR_JPA_CLIMB;
    if (in->vault && c->vault && in->ring && in->can)
        return SOMVR_JPA_VAULT;
    if (!in->jump || !c->jump || !in->can)
        return SOMVR_JPA_CLIMB;
    if (in->speed >= c->moving_cms)
        return SOMVR_JPA_JUMP;
    return in->descent ? SOMVR_JPA_CLIMB_FIRST : SOMVR_JPA_JUMP;
}
