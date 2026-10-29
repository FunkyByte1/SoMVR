/* somvr_jump.h - build 22: Talion jumps and vaults, the player's way (SOMVR_JUMP, SOMVR_VAULT; docs/re-xr-pipeline.md
 * 91, design-locomotion.md 3.6 LO-8 and the vault of 3.5; handoff/HANDOFF-loco.md). Pure: no globals, no game reads
 * (somvr_jump_game.c reads the game, casts the rays through the climb's somvr_climb_ray_fn, rides LOCO's delta).
 * Tested by tests/test_jump.c.
 *
 * Since build 8 (SOMVR_GAMEMOVES=off) A moved nothing on foot: the game's jump, vault and free-run carried the view by
 * the animation. Here the moves are ours, on LOCO's ticks (the root delta, design-locomotion.md 2.6) with the proxy's
 * gravity flag off for their length (the climb's way: root-motion mode then takes the delta's y as this tick's velocity):
 *   - the jump: A when nothing else claims it = a fixed take-off speed (jump_cm's rise at the game's gravity), the
 *     horizontal the stick's own 1:1 velocity all the way (LOCO's: a run's or the sprint latch's speed carries, a stick
 *     let go stops in the air as on the ground), the vertical a ballistic arc at the game's gravity (pc+0x58, -20 m/s^2)
 *     computed in closed form per tick; a head that meets a ceiling starts the fall there; the landing is soft: the fall's
 *     speed is braked to 0 over the last soft_cm over the floor under him (the view never dips under the standing
 *     height), then a 2 cm step down finds the floor (blocked = landed);
 *   - the vault: walking, running or standing at an obstacle between v_min_cm and v_max_cm over the feet (a waist-high
 *     wall, a rock, a fence) within reach: its landing is shown (the climb's gold ring: past the far side, or 45 cm onto
 *     a deep top) and A = over it: the jump's rise to just the top + v_clear_cm (the take-off for that height), held
 *     level while the body crosses the top, then the jump's fall and soft landing on the ring - the vertical speed
 *     continuous all the way (it is 0 at the apex where the level glide starts and ends); the horizontal speed fixed
 *     (the body's own when faster than v_cms, slower when the obstacle is near so the rise clears its near edge); short
 *     and fixed (no cancel needed), the stick ignored until the landing.
 * Both are the player's (L1: started by the press), the same every time (L6), and position-only (no yaw, no pitch, no
 * animation in the view). The game's collision clips every step: a blocked arc just falls and lands where it is.
 * Units: world cm, cm/s, s; game convention (+Y up). */
#ifndef SOMVR_JUMP_H
#define SOMVR_JUMP_H

#include <stdint.h>

#include "somvr_climb.h"

typedef struct {
    int jump;              /* SOMVR_JUMP: A's jump (0 off) */
    int vault;             /* SOMVR_VAULT: the ring at a low obstacle, A over it (0 off) */
    float jump_cm;         /* SOMVR_JUMP_CM: the feet's rise at the game's gravity (60) */
    float g;               /* the gravity when the game's is not read (2000 cm/s^2: the player record's -20 m/s^2) */
    float soft_cm;         /* the landing: the fall braked to 0 over this much over the floor (12) */
    float settle_cm;       /* ... then a step this far down finds the floor (2) */
    float air_max_s;       /* longer than this in the air: the game's own fall (gravity back) (3) */
    float drop_max_cm;     /* ... or the feet this far under the take-off's floor (400: the descent's blink height) */
    float moving_cms;      /* the body at least this fast at the press = moving: the jump goes at once (100) */
    float foot_cm;         /* the root over the feet (102) */
    float ground_cm;       /* a take-off needs the floor within this under the feet (25) */
    /* the vault */
    float v_min_cm;        /* an obstacle's top at least this over the feet (45: the capsule steps 40) */
    float v_max_cm;        /* SOMVR_VAULT_MAX_CM: ... at most this (120: waist high) */
    float v_clear_cm;      /* the feet over the top as they cross it (12) */
    float v_reach_cm;      /* its face within this of the root, standing (100) ... */
    float v_reach_s;       /* ... + the body's speed x this (0.4) ... */
    float v_reach_max_cm;  /* ... at most this (260) */
    float v_depth_cm;      /* a top deeper than this: onto it (100) */
    float v_in_cm;         /* onto a top: the landing this far in from its edge (45) */
    float v_past_cm;       /* over: the landing this far past the far edge (55: the capsule's 35 + 20) */
    float v_drop_cm;       /* over: the far floor at most this under the start's (150) */
    float v_cms;           /* SOMVR_VAULT_CMS: the least horizontal speed (300); the body's own up to v_cms_max */
    float v_cms_max;       /* (700) */
    float v_cms_min;       /* a higher, slower arc when the obstacle is near, down to this (120) */
    float v_rise_max_cm;   /* the feet's apex at most this over the start's floor (190) */
    float radius_cm;       /* Talion's capsule (35) */
    float room_cm;         /* the landing: nothing this far over it (180) */
    float side_cm;         /* ... nothing this near round it at 50 and 140 cm up (33) */
    int v_ring_stop;       /* SOMVR_VAULT_RING_STOP (build 24): the fixed horizontal ends at the ring's distance - a far floor
                              lower than the plan's (a downhill) falls straight down onto it, not past it (1) */
} somvr_jp_cfg;
void somvr_jp_cfg_default(somvr_jp_cfg *c);

/* the jump's kinds and its ends */
enum { SOMVR_JPK_NONE = 0, SOMVR_JPK_JUMP = 1, SOMVR_JPK_VAULT = 2 };
enum {
    SOMVR_JPE_NONE = 0,
    SOMVR_JPE_LANDED,      /* the 2 cm step down was blocked: on the floor */
    SOMVR_JPE_LOST,        /* LOCO lost the tick (a sync, the FSM, a menu ...): gravity back, the game's */
    SOMVR_JPE_CLIMB,       /* a grip caught a hold: the climb's (it keeps gravity off) */
    SOMVR_JPE_FALL,        /* too long or too deep in the air: the game's own fall (gravity back) */
    SOMVR_JPE_STOP,        /* stopped from outside (the switch, the input lost, a blink) */
    SOMVR_JPE_N
};
const char *somvr_jp_kind_name(int k); /* "none", "jump", "vault" */
const char *somvr_jp_end_name(int e);  /* "-", "landed", "lost", "caught by the climb", "the game's fall", "stopped" */

/* the take-off speed for a rise of h cm at gravity g: sqrt(2 g h) */
float somvr_jp_takeoff(float h, float g);
/* the closed-form vertical step over dt from vy at gravity g (cm), and the speed after */
float somvr_jp_arc_dy(float vy, float g, float dt);

/* ---- the arc's runner, once per LOCO tick --------------------------------------------------------------------------- */
typedef struct {
    int kind;              /* SOMVR_JPK_* (NONE = on the ground) */
    float t;               /* time in the air (s) */
    float vy;              /* our vertical velocity (cm/s, + up) */
    float g;               /* this jump's gravity (cm/s^2) */
    sv_vec3 start;         /* the root at the take-off */
    float floor0;          /* the take-off's floor (the feet's) */
    sv_vec3 vh;            /* the vault: the fixed horizontal velocity (cm/s) */
    sv_vec3 land;          /* the vault: the planned landing (the floor point) */
    float hold_t;          /* the vault: the apex is held (level, over the top) until this time in the air (0 none) */
    float miss_floor;      /* a floor the settle went through (the rays' floor there is ignored from then on) */
    int holding;           /* the vault holds the apex now */
    float asked_dy;        /* the last tick's asked vertical step (the read-back's) */
    sv_vec3 asked_h;       /* ... and the vault's horizontal step */
    sv_vec3 root_last;     /* the root at the last tick */
    int have_root;
    float apex_y;          /* the highest root of this jump */
    float brake_v;         /* the landing's brake: the speed entering it (0 = not braking) */
    float brake_a;         /* ... its deceleration */
    int settling;          /* the brake reached the floor: the step down that finds it */
    int settle_step;       /* the last tick's step was that step */
    int ceiling;           /* a ceiling turned the rise into the fall */
    int h_blocked;         /* the vault: ticks its horizontal step was blocked */
    int h_done;            /* the vault: at the ring's distance (v_ring_stop): no horizontal until the landing */
    float v_land;          /* the fall's speed when the brake started (or at a hard stop) */
    int end;               /* SOMVR_JPE_* of the last end */
    /* totals */
    uint64_t n_jump, n_vault, n_landed, n_lost, n_climb, n_fall, n_stop, n_ceiling, n_brakes, n_settle_miss, ticks, n_ring_stop;
    float max_rise_cm, max_air_s, max_land_cms;
} somvr_jp;
void somvr_jp_init(somvr_jp *s);

typedef struct {
    float dt;              /* the tick (s) */
    int owned;             /* LOCO owns the tick (else the arc is lost) */
    int climbing;          /* the climb holds (a grip caught a hold) */
    int have_root;
    sv_vec3 root;          /* Talion's root before this tick */
    int floor_ok;          /* a floor under the capsule (rays down) */
    float floor_y;         /* ... the highest one (the feet's height on it) */
} somvr_jp_in;
typedef struct {
    int active;            /* the arc runs this tick: the gravity flag off, dy in the delta */
    float dy;              /* the vertical step of this tick (cm) */
    int fixed_h;           /* the vault: the horizontal step is dh (the stick's replaced) */
    sv_vec3 dh;
    float vy;              /* the vertical velocity after it */
    int ended;             /* the arc ended at this tick's start (end = s->end); gravity back unless CLIMB */
    int braking;           /* the landing's brake this tick */
    int ceiling;           /* a ceiling was met (the read-back) */
} somvr_jp_out;
/* starts a jump (vy0 up) or a vault (vy0, the fixed horizontal velocity vh, the landing, the apex held until hold_t)
 * from the root on the floor */
void somvr_jp_start(somvr_jp *s, int kind, sv_vec3 root, float floor_y, float vy0, float g, sv_vec3 vh, sv_vec3 land,
                    float hold_t);
void somvr_jp_step(somvr_jp *s, const somvr_jp_cfg *c, const somvr_jp_in *in, somvr_jp_out *o);
void somvr_jp_stop(somvr_jp *s, int end); /* from outside (no step this tick) */

/* ---- the vault's plan ------------------------------------------------------------------------------------------------ */
enum {
    SOMVR_JPV_OK = 0, SOMVR_JPV_NONE,     /* nothing ahead within reach */
    SOMVR_JPV_LOW,                        /* a step (under v_min_cm): he walks over it */
    SOMVR_JPV_HIGH,                       /* over v_max_cm: the climb's (or the jump's) */
    SOMVR_JPV_NOTOP,                      /* no top found over the face (a slope, an overhang) */
    SOMVR_JPV_NOFLOOR,                    /* no landing past it (a ledge, water, deeper than v_drop_cm) */
    SOMVR_JPV_NOROOM,                     /* no room for Talion at the landing */
    SOMVR_JPV_NOWAY,                      /* the way over is not clear (the head, the body) */
    SOMVR_JPV_NOARC,                      /* no arc within the speeds and the apex clears the top */
    SOMVR_JPV_NOANSWER,                   /* a cast gave no answer */
    SOMVR_JPV_SLOPE,                      /* rising ground, no edge: the top not at the face, or the floor rising before it */
    SOMVR_JPV_N
};
const char *somvr_jp_vwhy_name(int why);
enum { SOMVR_JPO_OVER = 1, SOMVR_JPO_ONTO = 2 };
typedef struct {
    int why;               /* SOMVR_JPV_* */
    int kind;              /* SOMVR_JPO_OVER / _ONTO */
    float face_cm;         /* the face along dir from the root's axis */
    float top_y;           /* the obstacle's top (the highest point of its span) */
    float top_cm;          /* ... over the start's floor */
    float depth_cm;        /* the top's depth along dir (over: to the far edge) */
    sv_vec3 land;          /* the landing: the floor point the ring shows */
    float land_y;          /* the landing root's height (the floor + foot_cm + 1) */
    sv_vec3 vh;            /* the arc's horizontal velocity */
    float vy0;             /* ... its take-off speed */
    float T;               /* ... its time to the landing */
    float apex_cm;         /* the feet's apex over the start's floor (the held height: the top + v_clear_cm) */
    float g;               /* ... the gravity it was planned for */
    float feet0;           /* ... the feet over the start's floor at the take-off */
    float t_up, t_hold;    /* ... the apex's time, the end of the level glide over the top */
    float clear_cm;        /* the least clearance of the feet over the top's samples */
    int rays;
} somvr_jp_vplan;
/* the obstacle along dir (unit, horizontal) from the root on the floor floor_y at the body's speed speed: its face (rays
 * forward at the knees .. the waist), its top (rays down every 10 cm), the landing (past the far edge or onto a deep
 * top) with room, the arc (vh from max(speed, v_cms) down to v_cms_min until it clears) and its way (the feet and the
 * head along it). 1 = a vault (*p), else 0 with p->why. g: this jump's gravity. */
int somvr_jp_vault_find(const somvr_jp_cfg *c, somvr_climb_ray_fn ray, void *ctx, sv_vec3 root, float floor_y,
                        sv_vec3 dir, float speed, float g, somvr_jp_vplan *p);
/* the feet's height over the start's floor at horizontal distance u along the plan (the rise, the level glide, the fall) */
float somvr_jp_vault_feet(const somvr_jp_cfg *c, const somvr_jp_vplan *p, float u);

/* ---- whose A's press it is, on foot (after the strike's, the dodge's) ------------------------------------------------ */
enum { SOMVR_JPA_NONE = 0, SOMVR_JPA_VAULT, SOMVR_JPA_JUMP, SOMVR_JPA_CLIMB_FIRST, SOMVR_JPA_CLIMB, SOMVR_JPA_N };
typedef struct {
    int jump, vault;       /* the switches */
    int climbing;          /* a climb holds or hangs: the climb's (the mantle, the drop) */
    int ring;              /* the vault's ring is shown */
    float speed;           /* the body's horizontal speed (cm/s) */
    int can;               /* on the ground and free to jump (no blink, no dodge, no sync, not crouched ...) */
    int descent;           /* the climb's A on foot (the descent's edge) is on */
} somvr_jp_a_in;
/* the vault with its ring shown; else moving = the jump at once; else standing = the climb's edge first (a drop's ring
 * there), the jump when it finds no edge (SOMVR_JPA_CLIMB_FIRST); a climb = the climb's; the jump off = the climb's */
int somvr_jp_a_pick(const somvr_jp_cfg *c, const somvr_jp_a_in *in);
const char *somvr_jp_a_name(int a); /* "nobody", "the vault", "the jump", "the climb, then the jump", "the climb" */

#endif
