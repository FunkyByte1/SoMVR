/* test_wstep.c - unit tests of somvr_wstep.c, build 18's wraith step (docs/re-xr-pipeline.md 68): the landing from the
 * head's ray in a world of boxes (flat ground near and far, at the feet, a wall's face, over a low rock, through an orc
 * and an orc at the landing, a ledge and a small step down, a platform too high, water, a roof over the spot, out of
 * sight, no answer) and the input's states (the aim, the go after the hold, a flick, the refused ring's let-go, the
 * move stick's cancel and the walk's wait, the cooldown, cannot now, lost, sideways, no world). */
#include "../somvr_wstep.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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

/* ---- a world of boxes ---------------------------------------------------------------------------------------------- */
typedef struct { float lo[3], hi[3]; int wet, chr; } box;
static box W[16];
static int NW, NCALLS, DEAD;
static void wreset(void) { NW = 0, NCALLS = 0, DEAD = 0; }
static void add(float x0, float y0, float z0, float x1, float y1, float z1, int wet, int chr)
{
    W[NW++] = (box){ { x0, y0, z0 }, { x1, y1, z1 }, wet, chr };
}
static void ground(void) { add(-3000, -50, -3000, 3000, 0, 3000, 0, 0); }

static int ray(void *ctx, int hand, sv_vec3 from, sv_vec3 dir, float len, somvr_climb_hit *out)
{
    (void)ctx, (void)hand;
    NCALLS++;
    if (DEAD)
        return 0;
    const float f[3] = { from.x, from.y, from.z }, d[3] = { dir.x, dir.y, dir.z };
    float best = len + 1;
    int bi = -1;
    for (int i = 0; i < NW; i++) {
        float t0 = 0, t1 = len;
        int ok = 1;
        for (int k = 0; k < 3 && ok; k++) {
            if (fabsf(d[k]) < 1e-9f) {
                ok = f[k] >= W[i].lo[k] && f[k] <= W[i].hi[k];
                continue;
            }
            float a = (W[i].lo[k] - f[k]) / d[k], b = (W[i].hi[k] - f[k]) / d[k];
            if (a > b) { float x = a; a = b; b = x; }
            t0 = a > t0 ? a : t0, t1 = b < t1 ? b : t1;
            ok = t0 <= t1;
        }
        if (ok && t0 < best)
            best = t0, bi = i;
    }
    memset(out, 0, sizeof *out);
    if (bi >= 0 && best <= len) {
        out->hit = 1, out->dist = best, out->wet = W[bi].wet, out->chr = W[bi].chr;
        out->point = (sv_vec3){ f[0] + d[0] * best, f[1] + d[1] * best, f[2] + d[2] * best };
    }
    return 1;
}

static const sv_vec3 ROOT = { 0, 102, 0 }, EYE = { 0, 170, 0 };
static sv_vec3 look(float yaw_deg, float pitch_deg) /* yaw 0 = +z, 90 = +x; pitch + = up */
{
    float y = yaw_deg * DEG, p = pitch_deg * DEG;
    return (sv_vec3){ sinf(y) * cosf(p), sinf(p), cosf(y) * cosf(p) };
}
static int find(float yaw, float pitch, const sv_vec3 *orcs, int n, somvr_ws_aim *o)
{
    somvr_ws_cfg c;
    somvr_ws_cfg_default(&c);
    return somvr_ws_find(&c, ray, NULL, EYE, look(yaw, pitch), ROOT, 0, orcs, n, o);
}

static void test_find(void)
{
    somvr_ws_aim a;
    /* flat ground: where the head's ray meets it (170 cm / tan 20 deg = 467 cm), 15 cm short of it */
    wreset(), ground();
    CHECK(find(0, -20, NULL, 0, &a) == SOMVR_WSA_OK && NEAR(a.land.z, 467 - 15, 2) && NEAR(a.land.y, 0, 0.1) && !a.far &&
              NEAR(a.dist_cm, 452, 2), "flat: kind %d why %d at %.1f,%.1f,%.1f far %d", a.kind, a.why, a.land.x, a.land.y,
          a.land.z, a.far);
    CHECK(a.rays <= 14, "flat: %d casts", a.rays);
    CHECK(find(90, -20, NULL, 0, &a) == SOMVR_WSA_OK && NEAR(a.land.x, 452, 2) && NEAR(a.land.z, 0, 0.5), "flat +x: %.1f %.1f",
          a.land.x, a.land.z);
    /* level or up: the farthest along the view's heading */
    CHECK(find(0, 0, NULL, 0, &a) == SOMVR_WSA_OK && a.far && NEAR(a.land.z, 700, 0.5) && NEAR(a.dist_cm, 700, 0.5),
          "level: kind %d far %d z %.1f", a.kind, a.far, a.land.z);
    CHECK(find(0, 30, NULL, 0, &a) == SOMVR_WSA_OK && a.far && NEAR(a.land.z, 700, 0.5), "up: kind %d far %d", a.kind, a.far);
    CHECK(find(0, -13, NULL, 0, &a) == SOMVR_WSA_OK && a.far, "-13 deg: past 700 cm: the farthest (far %d)", a.far);
    /* at the feet: no ring at all */
    CHECK(find(0, -60, NULL, 0, &a) == SOMVR_WSA_NONE && a.why == SOMVR_WSW_NEAR, "feet: kind %d why %d", a.kind, a.why);
    CHECK(find(0, -89.9f, NULL, 0, &a) == SOMVR_WSA_NONE && a.why == SOMVR_WSW_NEAR, "straight down: kind %d why %d", a.kind,
          a.why);
    /* a wall's face (z 500..520, 300 high): refused, the red ring at its foot */
    wreset(), ground(), add(-300, 0, 500, 300, 300, 520, 0, 0);
    CHECK(find(0, -10, NULL, 0, &a) == SOMVR_WSA_NO && a.why == SOMVR_WSW_WALL && NEAR(a.land.y, 0, 0.1) && a.land.z < 500,
          "wall: kind %d why %d at z %.1f y %.1f", a.kind, a.why, a.land.z, a.land.y);
    CHECK(find(0, 0, NULL, 0, &a) == SOMVR_WSA_NO && a.why == SOMVR_WSW_WALL, "wall, level: kind %d why %d", a.kind, a.why);
    CHECK(find(0, -20, NULL, 0, &a) == SOMVR_WSA_OK && NEAR(a.land.z, 452, 2), "before the wall: kind %d why %d z %.1f",
          a.kind, a.why, a.land.z);
    /* over a low rock (40 cm, z 200..240): the ray passes over it, the landing beyond */
    wreset(), ground(), add(-100, 0, 200, 100, 40, 240, 0, 0);
    CHECK(find(0, -20, NULL, 0, &a) == SOMVR_WSA_OK && a.land.z > 240, "over the rock: kind %d why %d z %.1f", a.kind, a.why,
          a.land.z);
    /* looking at the rock's own face low down: in front of it (its face is under 30 cm over the ground there) */
    CHECK(find(0, -40, NULL, 0, &a) == SOMVR_WSA_OK && a.land.z < 200 && NEAR(a.land.y, 0, 0.1),
          "the rock's foot: kind %d why %d z %.1f", a.kind, a.why, a.land.z);
    /* through an orc in the way (z 260..340, 200 tall): the wraith's sight goes on through it */
    wreset(), ground(), add(-40, 0, 260, 40, 200, 340, 0, 1);
    CHECK(find(0, -20, NULL, 0, &a) == SOMVR_WSA_OK && a.through >= 1 && NEAR(a.land.z, 452, 2), "through the orc: kind %d "
          "why %d through %d z %.1f", a.kind, a.why, a.through, a.land.z);
    /* an orc at the landing: refused */
    sv_vec3 orc = { 0, 0, 470 };
    wreset(), ground(), add(-40, 0, 430, 40, 200, 510, 0, 1);
    CHECK(find(0, -20, &orc, 1, &a) == SOMVR_WSA_NO && a.why == SOMVR_WSW_ORC, "the orc there: kind %d why %d", a.kind, a.why);
    orc.z = 470; /* its body over the spot, not in the list (out of PlayerCombat's bands): refused by the body */
    CHECK(find(0, -20, NULL, 0, &a) == SOMVR_WSA_NO && a.why == SOMVR_WSW_ORC, "the orc's body there: kind %d why %d",
          a.kind, a.why);
    orc.z = 650;
    wreset(), ground(), add(-40, 0, 610, 40, 200, 690, 0, 1);
    CHECK(find(0, -20, &orc, 1, &a) == SOMVR_WSA_OK, "an orc 2 m past it: kind %d why %d", a.kind, a.why);
    /* a ledge: the ground ends at z 400, 4 m down past it */
    wreset(), add(-3000, -50, -3000, 3000, 0, 400, 0, 0), add(-3000, -450, 400, 3000, -400, 3000, 0, 0);
    CHECK(find(0, -10, NULL, 0, &a) == SOMVR_WSA_NO && a.why == SOMVR_WSW_LEDGE, "ledge: kind %d why %d", a.kind, a.why);
    CHECK(find(0, -20, NULL, 0, &a) == SOMVR_WSA_NO && a.why == SOMVR_WSW_LEDGE, "ledge -20: kind %d why %d", a.kind, a.why);
    CHECK(find(0, -30, NULL, 0, &a) == SOMVR_WSA_OK && a.land.z < 400, "short of the ledge: kind %d why %d z %.1f", a.kind,
          a.why, a.land.z);
    /* a lip: ground 7 m out only as a 40 cm wide spur over the drop (looking level: the farthest is on it) */
    wreset(), add(-3000, -50, -3000, 3000, 0, 400, 0, 0), add(-3000, -450, 400, 3000, -400, 3000, 0, 0),
        add(-20, -50, 400, 20, 0, 3000, 0, 0);
    CHECK(find(0, 0, NULL, 0, &a) == SOMVR_WSA_NO && a.why == SOMVR_WSW_LEDGE && NEAR(a.land.y, 0, 0.1),
          "the spur: kind %d why %d y %.1f", a.kind, a.why, a.land.y);
    /* a wall right ahead (50 cm), looked at level: refused, not "at the feet" */
    wreset(), ground(), add(-300, 0, 50, 300, 300, 70, 0, 0);
    CHECK(find(0, 0, NULL, 0, &a) == SOMVR_WSA_NO && a.why == SOMVR_WSW_WALL, "a wall 50 cm ahead: kind %d why %d", a.kind,
          a.why);
    CHECK(find(0, -80, NULL, 0, &a) == SOMVR_WSA_NONE && a.why == SOMVR_WSW_NEAR, "the feet before it: kind %d why %d", a.kind,
          a.why);
    /* a small step down (1 m): fine */
    wreset(), add(-3000, -50, -3000, 3000, 0, 400, 0, 0), add(-3000, -150, 400, 3000, -100, 3000, 0, 0);
    CHECK(find(0, -10, NULL, 0, &a) == SOMVR_WSA_OK && NEAR(a.land.y, -100, 0.1) && NEAR(a.rise_cm, -100, 0.1),
          "1 m down: kind %d why %d y %.1f", a.kind, a.why, a.land.y);
    /* a platform 2 m high from z 300: its face = a wall; looking over it = too high */
    wreset(), ground(), add(-3000, 0, 300, 3000, 200, 3000, 0, 0);
    CHECK(find(0, -10, NULL, 0, &a) == SOMVR_WSA_NO && a.why == SOMVR_WSW_WALL, "platform face: kind %d why %d", a.kind, a.why);
    CHECK(find(0, 10, NULL, 0, &a) == SOMVR_WSA_NO && a.why == SOMVR_WSW_HIGH && NEAR(a.rise_cm, 200, 0.1),
          "platform top: kind %d why %d rise %.1f", a.kind, a.why, a.rise_cm);
    /* 1 m up onto it: fine */
    wreset(), ground(), add(-3000, 0, 300, 3000, 100, 3000, 0, 0);
    CHECK(find(0, -2, NULL, 0, &a) == SOMVR_WSA_OK && NEAR(a.land.y, 100, 0.1), "1 m up: kind %d why %d y %.1f", a.kind,
          a.why, a.land.y);
    /* water */
    wreset(), add(-3000, -50, -3000, 3000, 0, 3000, 1, 0);
    CHECK(find(0, -20, NULL, 0, &a) == SOMVR_WSA_NO && a.why == SOMVR_WSW_WATER, "water: kind %d why %d", a.kind, a.why);
    /* a roof 1.5 m over the spot, the ray under it */
    wreset(), ground(), add(-200, 150, 400, 200, 160, 520, 0, 0);
    CHECK(find(0, -20, NULL, 0, &a) == SOMVR_WSA_NO && a.why == SOMVR_WSW_ROOM, "roof: kind %d why %d", a.kind, a.why);
    /* a post right beside the spot */
    wreset(), ground(), add(470, 0, 440, 480, 300, 470, 0, 0);
    CHECK(find(0, -20, NULL, 0, &a) == SOMVR_WSA_OK, "a post 18 m aside: kind %d why %d", a.kind, a.why);
    wreset(), ground(), add(10, 0, 440, 30, 300, 470, 0, 0);
    CHECK(find(0, -20, NULL, 0, &a) == SOMVR_WSA_NO && a.why == SOMVR_WSW_ROOM, "a post 10 cm aside: kind %d why %d", a.kind,
          a.why);
    /* out of sight: the farthest (looking level) behind a low wall the head's ray passes over */
    wreset(), ground(), add(-300, 0, 650, 300, 125, 660, 0, 0);
    CHECK(find(0, 0, NULL, 0, &a) == SOMVR_WSA_NO && a.why == SOMVR_WSW_SIGHT, "hidden: kind %d why %d", a.kind, a.why);
    /* no answer from the cast */
    wreset(), ground(), DEAD = 1;
    CHECK(find(0, -20, NULL, 0, &a) == SOMVR_WSA_NONE && a.why == SOMVR_WSW_NORAY, "dead cast: kind %d why %d", a.kind, a.why);
    /* NaN */
    wreset(), ground();
    somvr_ws_cfg c;
    somvr_ws_cfg_default(&c);
    CHECK(somvr_ws_find(&c, ray, NULL, EYE, (sv_vec3){ NAN, 0, 1 }, ROOT, 0, NULL, 0, &a) == SOMVR_WSA_NONE && NCALLS == 0,
          "NaN gaze: kind %d calls %d", a.kind, NCALLS);
}

/* ---- build 27: the vault over an orc (SOMVR_WRAITH_STEP_VAULT_CM; docs/re-xr-pipeline.md 108) ------------------------ */
static int vfind(float vault_cm, float yaw, float pitch, const sv_vec3 *orcs, int n, somvr_ws_aim *o)
{
    somvr_ws_cfg c;
    somvr_ws_cfg_default(&c);
    c.vault_cm = vault_cm;
    return somvr_ws_find(&c, ray, NULL, EYE, look(yaw, pitch), ROOT, 0, orcs, n, o);
}
static void test_vault(void)
{
    somvr_ws_aim a;
    /* an orc 2 m ahead (its body z 170..230, 200 tall), looked at in the chest: the old step refuses (its body over the
     * spot: "an orc there") or flies 7 m past it (level); the vault puts the ring 120 cm behind its root */
    sv_vec3 orc = { 0, 0, 200 };
    wreset(), ground(), add(-30, 0, 170, 30, 200, 230, 0, 1);
    CHECK(vfind(0, 0, -35, &orc, 1, &a) == SOMVR_WSA_NO && a.why == SOMVR_WSW_ORC && !a.vault, "off: the orc's body: kind %d "
          "why %d vault %d", a.kind, a.why, a.vault);
    CHECK(vfind(0, 0, 0, &orc, 1, &a) == SOMVR_WSA_OK && a.far && !a.vault, "off, level: 7 m past it (far %d)", a.far);
    CHECK(vfind(350, 0, -25, &orc, 1, &a) == SOMVR_WSA_OK && a.vault && a.orc == 0 && NEAR(a.land.z, 320, 1) &&
              NEAR(a.land.x, 0, 1) && NEAR(a.land.y, 0, 0.1) && NEAR(a.orc_cm, 200, 1),
          "the vault, the chest: kind %d why %d vault %d at %.1f,%.1f,%.1f", a.kind, a.why, a.vault, a.land.x, a.land.y, a.land.z);
    CHECK(vfind(350, 0, 0, &orc, 1, &a) == SOMVR_WSA_OK && a.vault && NEAR(a.land.z, 320, 1) && !a.far,
          "the vault, level at its head: kind %d vault %d z %.1f far %d", a.kind, a.vault, a.land.z, a.far);
    /* an orc ahead-right: the ring on the line from Talion through its root */
    sv_vec3 o2 = { 150, 0, 150 };
    wreset(), ground(), add(120, 0, 120, 180, 200, 180, 0, 1);
    float yaw = 45, p2 = -atan2f(170 - 100, 212) / DEG;
    CHECK(vfind(350, yaw, p2, &o2, 1, &a) == SOMVR_WSA_OK && a.vault && NEAR(a.land.x, 150 + 84.85f, 2) &&
              NEAR(a.land.z, 150 + 84.85f, 2), "ahead-right: kind %d why %d vault %d at %.1f,%.1f", a.kind, a.why, a.vault,
          a.land.x, a.land.z);
    /* too far (4 m > 350): no vault - as before (its body: refused) */
    sv_vec3 o3 = { 0, 0, 400 };
    wreset(), ground(), add(-30, 0, 370, 30, 200, 430, 0, 1);
    CHECK(vfind(350, 0, -14, &o3, 1, &a) != SOMVR_WSA_OK || !a.vault, "4 m: no vault (kind %d vault %d)", a.kind, a.vault);
    CHECK(!a.vault, "4 m: not the vault's");
    /* the head's ray through its body to ground 4.5 m out: still the vault (the head on an orc = over it) */
    wreset(), ground(), add(-30, 0, 170, 30, 200, 230, 0, 1);
    CHECK(vfind(350, 0, -20, &orc, 1, &a) == SOMVR_WSA_OK && a.vault && NEAR(a.land.z, 320, 1), "through it: kind %d vault "
          "%d z %.1f", a.kind, a.vault, a.land.z);
    CHECK(vfind(350, 0, -35, &orc, 1, &a) == SOMVR_WSA_OK && a.vault && NEAR(a.land.z, 320, 1), "at its knees: kind %d vault "
          "%d z %.1f", a.kind, a.vault, a.land.z);
    /* looking beside it (15 deg left) at the ground beyond: that aimed spot, not the vault */
    CHECK(vfind(350, -15, -20, &orc, 1, &a) == SOMVR_WSA_OK && !a.vault && a.through == 0, "beside it: kind %d vault %d",
          a.kind, a.vault);
    /* a wall at its back (z 260..280): the vault refused (a wall), the ring red */
    wreset(), ground(), add(-30, 0, 170, 30, 200, 230, 0, 1), add(-300, 0, 260, 300, 300, 280, 0, 0);
    CHECK(vfind(350, 0, -25, &orc, 1, &a) == SOMVR_WSA_NO && a.vault && a.why == SOMVR_WSW_WALL, "a wall at its back: kind "
          "%d why %d vault %d", a.kind, a.why, a.vault);
    /* a low fence at its back (z 260..270, 100 high: the eyes see over it): refused by the waist ray */
    wreset(), ground(), add(-30, 0, 170, 30, 200, 230, 0, 1), add(-300, 0, 260, 300, 105, 270, 0, 0);
    CHECK(vfind(350, 0, -25, &orc, 1, &a) == SOMVR_WSA_NO && a.vault && a.why == SOMVR_WSW_WALL, "a fence at its back: kind "
          "%d why %d", a.kind, a.why);
    /* a second orc standing on the landing: refused (an orc there) */
    sv_vec3 two[2] = { { 0, 0, 200 }, { 0, 0, 330 } };
    wreset(), ground(), add(-30, 0, 170, 30, 200, 230, 0, 1), add(-30, 0, 300, 30, 200, 360, 0, 1);
    CHECK(vfind(350, 0, -25, two, 2, &a) == SOMVR_WSA_NO && a.vault && a.orc == 0 && a.why == SOMVR_WSW_ORC,
          "an orc on the landing: kind %d why %d vault %d orc %d", a.kind, a.why, a.vault, a.orc);
    /* a ledge at its back (the ground ends at z 250): refused */
    wreset(), add(-3000, -50, -3000, 3000, 0, 250, 0, 0), add(-30, 0, 170, 30, 200, 230, 0, 1);
    CHECK(vfind(350, 0, -25, &orc, 1, &a) == SOMVR_WSA_NO && a.vault && a.why == SOMVR_WSW_LEDGE, "a ledge at its back: kind "
          "%d why %d", a.kind, a.why);
    /* a body the ray meets with no orc of the list near it (not PlayerCombat's): as with the vault off */
    wreset(), ground(), add(-30, 0, 170, 30, 200, 230, 0, 1);
    somvr_ws_aim b;
    int k0 = vfind(0, 0, -35, NULL, 0, &b), k1 = vfind(350, 0, -35, NULL, 0, &a);
    CHECK(k0 == k1 && !a.vault && NEAR(a.land.z, b.land.z, 0.01), "unlisted body: kind %d/%d vault %d z %.1f/%.1f", k0, k1,
          a.vault, b.land.z, a.land.z);
}

/* ---- the input ----------------------------------------------------------------------------------------------------- */
static somvr_ws_cfg C;
static somvr_ws_state S;
static uint32_t tk(double t, float ry, float move, int can)
{
    somvr_ws_in in = { .t = t, .world = 1, .can = can, .rx = 0, .ry = ry, .move = move };
    return somvr_ws_tick(&S, &C, &in);
}
static somvr_ws_aim aim(int kind)
{
    somvr_ws_aim a;
    memset(&a, 0, sizeof a);
    a.kind = kind, a.land = (sv_vec3){ 1, 2, 3 };
    return a;
}

static void test_input(void)
{
    somvr_ws_cfg_default(&C);
    somvr_ws_init(&S);
    somvr_ws_aim ok = aim(SOMVR_WSA_OK), no = aim(SOMVR_WSA_NO), none = aim(SOMVR_WSA_NONE);
    /* the aim, the ring, the go after the hold */
    CHECK(tk(10.0, 0.5f, 0, 1) == 0 && S.phase == SOMVR_WSP_IDLE, "under `on`: nothing");
    CHECK(tk(10.1, 0.9f, 0, 1) == SOMVR_WSE_AIM && S.phase == SOMVR_WSP_AIM && !somvr_ws_walk_ok(&S), "the aim");
    CHECK(somvr_ws_aimed(&S, &ok) == SOMVR_WSE_TURN_OK && S.have_aim, "the ring");
    CHECK(somvr_ws_aimed(&S, &ok) == 0, "the same ring: no event");
    CHECK(tk(10.3, 0.6f, 0.2f, 1) == 0 && S.phase == SOMVR_WSP_AIM, "held under `on` but over `off`, a small walk: aiming");
    CHECK(tk(10.5, 0.1f, 0, 1) == SOMVR_WSE_GO && S.phase == SOMVR_WSP_IDLE && NEAR(S.held_s, 0.4, 1e-6) &&
              S.aim.land.z == 3 && somvr_ws_walk_ok(&S), "let go: the go (held %.2f)", S.held_s);
    /* the cooldown */
    CHECK(tk(11.0, 0.9f, 0, 1) == SOMVR_WSE_REFUSED && S.refused == 1 && S.phase == SOMVR_WSP_REST, "the cooldown");
    CHECK(tk(11.6, 0.9f, 0, 1) == 0 && S.phase == SOMVR_WSP_REST, "still forward after a refusal: waits for the rest");
    CHECK(tk(11.7, 0, 0, 1) == 0 && S.phase == SOMVR_WSP_IDLE, "rested");
    /* a refused ring: let go = nothing */
    CHECK(tk(12.0, 0.9f, 0, 1) == SOMVR_WSE_AIM, "aim 2");
    CHECK(somvr_ws_aimed(&S, &ok) == SOMVR_WSE_TURN_OK && somvr_ws_aimed(&S, &no) == SOMVR_WSE_TURN_NO && S.n_turn_no == 1,
          "ok -> no: the turn");
    CHECK(tk(12.6, 0, 0, 1) == SOMVR_WSE_NOGO && S.n_go == 1, "let go over the red ring: nothing");
    /* no ring (at the feet): nothing */
    CHECK(tk(13.0, 0.9f, 0, 1) == SOMVR_WSE_AIM && somvr_ws_aimed(&S, &none) == 0, "aim 3, no ring");
    CHECK(tk(13.6, 0, 0, 1) == SOMVR_WSE_NOGO, "let go with no ring: nothing");
    /* no find yet: nothing */
    CHECK(tk(14.0, 0.9f, 0, 1) == SOMVR_WSE_AIM && tk(14.5, 0, 0, 1) == SOMVR_WSE_NOGO, "no find: nothing");
    /* a flick */
    CHECK(tk(15.0, 0.9f, 0, 1) == SOMVR_WSE_AIM && somvr_ws_aimed(&S, &ok) && tk(15.1, 0, 0, 1) == SOMVR_WSE_NOGO &&
              S.n_tap == 1, "a flick under 0.25 s: nothing (taps %llu)", (unsigned long long)S.n_tap);
    /* the cancel: the move stick past half; the walk waits for its rest; the right stick let go later: nothing */
    CHECK(tk(16.0, 0.9f, 0, 1) == SOMVR_WSE_AIM && somvr_ws_aimed(&S, &ok), "aim 6");
    CHECK(tk(16.4, 0.9f, 0.8f, 1) == SOMVR_WSE_CANCEL && S.phase == SOMVR_WSP_REST && !somvr_ws_walk_ok(&S), "the cancel");
    CHECK(tk(16.5, 0.9f, 0.6f, 1) == 0 && !somvr_ws_walk_ok(&S), "the walk waits");
    CHECK(tk(16.6, 0, 0.6f, 1) == 0 && S.phase == SOMVR_WSP_IDLE && !somvr_ws_walk_ok(&S), "the right stick let go: nothing");
    CHECK(tk(16.7, 0, 0.1f, 1) == 0 && somvr_ws_walk_ok(&S), "the move stick rests: the walk again");
    CHECK(S.n_go == 1 && S.n_cancel == 1, "one go, one cancel");
    /* cannot now (a climb, the bow ...) */
    CHECK(tk(17.0, 0.9f, 0, 0) == SOMVR_WSE_REFUSED && S.refused == 2, "cannot now: refused");
    CHECK(tk(17.1, 0, 0, 1) == 0 && S.phase == SOMVR_WSP_IDLE, "rested");
    /* lost while aiming */
    CHECK(tk(18.0, 0.9f, 0, 1) == SOMVR_WSE_AIM && somvr_ws_aimed(&S, &ok) && tk(18.3, 0.9f, 0, 0) == SOMVR_WSE_LOST &&
              S.phase == SOMVR_WSP_REST, "lost");
    CHECK(tk(18.4, 0, 0, 1) == 0 && S.n_go == 1, "let go after lost: nothing");
    /* sideways is the turn, not the aim; no world, no aim */
    somvr_ws_in in = { .t = 19, .world = 1, .can = 1, .rx = 0.9f, .ry = 0.75f, .move = 0 };
    CHECK(somvr_ws_tick(&S, &C, &in) == 0 && S.phase == SOMVR_WSP_IDLE, "mostly sideways: nothing");
    in.rx = 0.6f, in.ry = 0.8f; /* 37 deg off forward: a turn's diagonal, not the aim */
    CHECK(somvr_ws_tick(&S, &C, &in) == 0 && S.phase == SOMVR_WSP_IDLE, "a diagonal: nothing");
    in.rx = 0.4f, in.ry = 0.8f; /* 27 deg: the aim */
    CHECK(somvr_ws_tick(&S, &C, &in) == SOMVR_WSE_AIM, "27 deg off forward: the aim");
    in.rx = 0, in.ry = 0;
    CHECK(somvr_ws_tick(&S, &C, &in) == SOMVR_WSE_NOGO, "let go at once: nothing");
    in.rx = 0, in.ry = 0.9f, in.world = 0;
    CHECK(somvr_ws_tick(&S, &C, &in) == 0 && S.phase == SOMVR_WSP_IDLE, "a menu: nothing");
    /* the second go after the cooldown */
    CHECK(tk(20.0, 0.9f, 0, 1) == SOMVR_WSE_AIM && somvr_ws_aimed(&S, &ok) && tk(20.4, 0, 0, 1) == SOMVR_WSE_GO && S.n_go == 2,
          "the second go");
    CHECK(somvr_ws_aimed(&S, &no) == 0, "aimed while idle: nothing");
}

int main(void)
{
    test_find();
    test_vault();
    test_input();
    printf("test_wstep: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
