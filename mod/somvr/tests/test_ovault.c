/* test_ovault.c - the vault over an orc by the hands (somvr_ovault.h): the crown's rule, the 1:1 hold, the throw's rule,
 * the flight */
#include "../somvr_ovault.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_fail, g_n;
#define CHECK(c, ...)                                                                                                      \
    do {                                                                                                                   \
        g_n++;                                                                                                             \
        if (!(c)) {                                                                                                        \
            g_fail++;                                                                                                      \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                    \
            printf(__VA_ARGS__);                                                                                           \
            printf("\n");                                                                                                  \
        }                                                                                                                  \
    } while (0)

static somvr_ov_cfg C;

/* Talion's root at (0, 0, 0) facing +z; the orc's root at (0, 0, 80), its head point at (0, 190, 80); the left hand at
 * the tracking point p (rig yaw 0, scale 1: the world = the tracking space shifted by the body's moves) */
static void in_at(somvr_ov_in *in, double t, float sq, sv_vec3 p, sv_vec3 root)
{
    memset(in, 0, sizeof *in);
    in->t = t, in->live = 1, in->rig_scale = 1;
    in->hand[0].valid = 1, in->hand[0].p = p, in->hand[0].squeeze = sq;
    in->hand[0].pw = (sv_vec3){ p.x + root.x, p.y + root.y, p.z + root.z };
    in->free[0] = 1;
    in->orc_ok = 1, in->head = (sv_vec3){ 0, 190, 80 }, in->orc_root = (sv_vec3){ 0, 0, 80 };
    in->root_ok = 1, in->root = root;
}

static void test_crown(void)
{
    sv_vec3 hd = { 0, 190, 80 };
    CHECK(somvr_ov_on_crown(&C, (sv_vec3){ 0, 196, 76 }, hd), "on his crown");
    CHECK(!somvr_ov_on_crown(&C, (sv_vec3){ 0, 178, 72 }, hd), "the collar (12 under the head point): the grab's, not the vault's");
    CHECK(!somvr_ov_on_crown(&C, (sv_vec3){ 0, 200, 50 }, hd), "30 cm off: no");
}

/* the grab, a push down 30 cm in 0.3 s (the body rises 1:1), then a throw down and back */
static void test_hold_throw(void)
{
    somvr_ov s;
    somvr_ov_in in;
    somvr_ov_out o;
    somvr_ov_reset(&s);
    sv_vec3 root = { 0, 0, 0 };
    in_at(&in, 1.0, 0, (sv_vec3){ 0, 195, 76 }, root);
    somvr_ov_step(&s, &C, &in, &o);
    CHECK(s.phase == SOMVR_OV_IDLE && !o.events, "the open hand on his crown: nothing");
    in_at(&in, 1.011, 0.9f, (sv_vec3){ 0, 195, 76 }, root);
    somvr_ov_step(&s, &C, &in, &o);
    CHECK(s.phase == SOMVR_OV_HOLD && (o.events & SOMVR_OVE_GRAB) && o.hand == 0 && o.grav_off, "the squeeze on his crown: the hold");
    float rise = 0;
    for (int k = 1; k <= 9; k++) { /* down 30 cm at 100 cm/s: the body up 1:1 */
        in_at(&in, 1.011 + 0.0333 * k, 0.9f, (sv_vec3){ 0, 195 - 3.333f * k, 76 }, root);
        somvr_ov_step(&s, &C, &in, &o);
        rise += o.move.y, root.y += o.move.y;
    }
    CHECK(fabsf(rise - 30) < 0.1f && s.phase == SOMVR_OV_HOLD, "the push down 30 cm: the body up 30 (%.2f)", rise);
    /* the throw: down at 300 cm/s and back at 200 cm/s for 0.1 s, let go */
    sv_vec3 p = { 0, 165, 76 };
    for (int k = 1; k <= 4; k++) {
        p.y -= 10, p.z -= 6.67f;
        in_at(&in, 1.311 + 0.0333 * k, 0.9f, p, root);
        somvr_ov_step(&s, &C, &in, &o);
        root.x += o.move.x, root.y += o.move.y, root.z += o.move.z;
    }
    in_at(&in, 1.311 + 0.0333 * 5, 0.1f, p, root);
    somvr_ov_step(&s, &C, &in, &o);
    CHECK((o.events & SOMVR_OVE_THROW) && s.phase == SOMVR_OV_FLIGHT && o.v0.y > 250 && o.v0.z > 280 && o.land_cm >= o.need_cm,
          "the throw: up %.0f over %.0f, lands %.0f (needs %.0f)", o.v0.y, o.v0.z, o.land_cm, o.need_cm);
    /* the flight: the game applies every step; then the ground stops the fall */
    sv_vec3 r = root;
    int land = 0, k;
    float top = r.y;
    for (k = 1; k <= 150 && !land; k++) {
        in_at(&in, 1.5 + 0.0111 * k, 0, (sv_vec3){ 0, 150, 60 }, r);
        somvr_ov_step(&s, &C, &in, &o);
        land = (o.events & SOMVR_OVE_LAND) != 0;
        r.x += o.move.x, r.z += o.move.z, r.y += o.move.y;
        if (r.y < 0)
            r.y = 0; /* the ground */
        top = r.y > top ? r.y : top;
    }
    CHECK(land && r.z > 130 && top > 60 && s.phase == SOMVR_OV_IDLE, "the flight lands past him (z %.0f, its top %.0f, %d steps)",
          r.z, top, k);
}

static void test_rules(void)
{
    somvr_ov s;
    somvr_ov_in in;
    somvr_ov_out o;
    sv_vec3 root = { 0, 0, 0 };
    /* a slow let-go: no throw */
    somvr_ov_reset(&s);
    in_at(&in, 1.0, 0, (sv_vec3){ 0, 195, 76 }, root), somvr_ov_step(&s, &C, &in, &o);
    in_at(&in, 1.01, 0.9f, (sv_vec3){ 0, 195, 76 }, root), somvr_ov_step(&s, &C, &in, &o);
    for (int k = 1; k <= 6; k++)
        in_at(&in, 1.01 + 0.0333 * k, 0.9f, (sv_vec3){ 0, 195 - 1.0f * k, 76 }, root), somvr_ov_step(&s, &C, &in, &o);
    in_at(&in, 1.3, 0.1f, (sv_vec3){ 0, 189, 76 }, root), somvr_ov_step(&s, &C, &in, &o);
    CHECK((o.events & SOMVR_OVE_LETGO) && o.why == SOMVR_OVW_OPEN && s.phase == SOMVR_OV_IDLE && !o.grav_off,
          "a slow let-go: no throw (%d)", o.why);
    /* thrown backward (the hand pushed away from him): the part over him dropped - no throw */
    somvr_ov_reset(&s);
    in_at(&in, 2.0, 0, (sv_vec3){ 0, 195, 76 }, root), somvr_ov_step(&s, &C, &in, &o);
    in_at(&in, 2.01, 0.9f, (sv_vec3){ 0, 195, 76 }, root), somvr_ov_step(&s, &C, &in, &o);
    sv_vec3 p = { 0, 195, 76 };
    for (int k = 1; k <= 4; k++)
        p.y -= 10, p.z += 8, in_at(&in, 2.01 + 0.0333 * k, 0.9f, p, root), somvr_ov_step(&s, &C, &in, &o);
    in_at(&in, 2.2, 0.1f, p, root), somvr_ov_step(&s, &C, &in, &o);
    CHECK((o.events & SOMVR_OVE_LETGO) && !(o.events & SOMVR_OVE_THROW) && o.throw_over < 0,
          "thrown backward: no throw (over %.0f)", o.throw_over);
    /* a throw too weak to clear him: short */
    somvr_ov_reset(&s);
    in_at(&in, 3.0, 0, (sv_vec3){ 0, 195, 76 }, root), somvr_ov_step(&s, &C, &in, &o);
    in_at(&in, 3.01, 0.9f, (sv_vec3){ 0, 195, 76 }, root), somvr_ov_step(&s, &C, &in, &o);
    p = (sv_vec3){ 0, 195, 76 };
    for (int k = 1; k <= 4; k++)
        p.y -= 6, p.z -= 3, in_at(&in, 3.01 + 0.0333 * k, 0.9f, p, root), somvr_ov_step(&s, &C, &in, &o);
    in_at(&in, 3.2, 0.1f, p, root), somvr_ov_step(&s, &C, &in, &o);
    CHECK((o.events & SOMVR_OVE_LETGO) && o.why == SOMVR_OVW_SHORT && o.land_cm < o.need_cm,
          "a weak throw: short (lands %.0f, needs %.0f)", o.land_cm, o.need_cm);
    /* he walks 45 cm while held: let go (no drag); his head's sway alone is no let-go */
    somvr_ov_reset(&s);
    in_at(&in, 4.0, 0, (sv_vec3){ 0, 195, 76 }, root), somvr_ov_step(&s, &C, &in, &o);
    in_at(&in, 4.01, 0.9f, (sv_vec3){ 0, 195, 76 }, root), somvr_ov_step(&s, &C, &in, &o);
    in_at(&in, 4.03, 0.9f, (sv_vec3){ 0, 195, 76 }, root);
    in.head.x += 30;
    somvr_ov_step(&s, &C, &in, &o);
    CHECK(s.phase == SOMVR_OV_HOLD && !(o.events & SOMVR_OVE_LETGO), "his head swayed 30 cm: still held");
    in_at(&in, 4.05, 0.9f, (sv_vec3){ 0, 195, 76 }, root);
    in.orc_root.z += 45;
    somvr_ov_step(&s, &C, &in, &o);
    CHECK((o.events & SOMVR_OVE_LETGO) && o.why == SOMVR_OVW_MOVED && o.move.z == 0, "he walked away: let go, no drag");
    /* the collar is no vault: nothing */
    somvr_ov_reset(&s);
    in_at(&in, 5.0, 0, (sv_vec3){ 0, 178, 72 }, root), somvr_ov_step(&s, &C, &in, &o);
    in_at(&in, 5.01, 0.9f, (sv_vec3){ 0, 178, 72 }, root), somvr_ov_step(&s, &C, &in, &o);
    CHECK(s.phase == SOMVR_OV_IDLE && !o.events, "a squeeze at his collar: not the vault's");
    /* a blocked flight: the floor's part kept nothing twice -> straight down */
    somvr_ov_reset(&s);
    s.phase = SOMVR_OV_FLIGHT, s.v = (sv_vec3){ 0, 200, 300 }, s.t0 = 6.0, s.t_last = 6.0;
    sv_vec3 r = { 0, 30, 0 };
    int blocked = 0;
    for (int k = 1; k <= 4; k++) {
        in_at(&in, 6.0 + 0.0111 * k, 0, (sv_vec3){ 0, 150, 60 }, r);
        somvr_ov_step(&s, &C, &in, &o);
        blocked |= (o.events & SOMVR_OVE_BLOCKED) != 0;
        r.y += o.move.y; /* (z never moves: he is in the way) */
    }
    CHECK(blocked && s.v.z == 0, "blocked: the rest straight down (%d %.0f)", blocked, s.v.z);
}

int main(void)
{
    somvr_ov_cfg_default(&C);
    test_crown();
    test_hold_throw();
    test_rules();
    printf("test_ovault: %d checks, %d failures\n", g_n, g_fail);
    return g_fail != 0;
}
