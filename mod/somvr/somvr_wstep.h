/* somvr_wstep.h - build 18: the wraith step (SOMVR_WRAITH_STEP; docs/re-xr-pipeline.md 68, handoff/HANDOFF-wraith.md
 * "build 18: the wraith step", HANDOFF-combat-design.md item 7): a short blink the player aims and times - over a low
 * wall, past an orc - by the comfort rule for any blink: a clear when (the let-go), an aimed and shown where (a ring on
 * the ground) and a cancel while choosing. Pure (no globals, no game reads): the landing from the head's ray through
 * the game's ray cast (the climb's somvr_climb_ray_fn) and the input's states. somvr_wstep_game.c (included by
 * somvr.c) reads the game, shows the ring (the objective layer's landing ring: gold = here, red and crossed = not
 * here) and starts the quick blink (the drop mark's). Tested by tests/test_wstep.c.
 *
 * The player: the right stick pushed forward (within 31 deg) and held = aiming. The ring sits where the head's ray meets the ground
 * (a ray that meets an orc goes on through it: the wraith's sight), at most max_cm along the floor from Talion; looking
 * level or up = the farthest along the view's heading. Refused (the red crossed ring, a soft double buzz as it turns):
 * a wall's face (near too), a ledge (more than drop_cm down: the drop is the climb's; or no ground stance_cm round the
 * spot: a lip), more than rise_cm up, water, no room for
 * Talion, an orc there, a spot out of the eyes' sight. Nearer than min_cm (looking at the feet) = no ring at all.
 * Let go (the stick back under `off`) = the blink onto a gold ring, held at least min_s; over a red ring or none, or
 * a flick under min_s, nothing. The move stick past `cancel` while aiming = the cancel (the walk waits until it rests).
 * Units: world cm, +Y up, s. */
#ifndef SOMVR_WSTEP_H
#define SOMVR_WSTEP_H

#include <stdint.h>

#include "somvr_climb.h"

typedef struct {
    float max_cm;   /* SOMVR_WRAITH_STEP_M: the farthest landing along the floor (700) */
    float min_cm;   /* nearer = no step, no ring (120) */
    float rise_cm;  /* the landing at most this over Talion's floor (150) */
    float drop_cm;  /* ... at most this under it (150: deeper is a ledge) */
    float room_cm;  /* nothing this far over the landing (180, the pullover's) */
    float side_cm;  /* nothing this near round it at 100 cm up (30) */
    float orc_cm;   /* no orc's root this near the landing (100) */
    float stance_cm; /* ground within 40 cm under the spot this far round it, 4 ways (35: not a lip) */
    float ray_cm;   /* the head's ray (1500) */
    float on, off;  /* the right stick: forward past `on` starts the aim; under `off` (its radius) is the let-go */
    float cancel;   /* the move stick past this while aiming = the cancel (0.5) */
    float rest;     /* ... the walk waits until it is under this (0.3) */
    float min_s;    /* SOMVR_WRAITH_STEP_HOLD_MS: the hold a let-go needs (0.25) */
    float cool_s;   /* SOMVR_WRAITH_STEP_COOL_MS: after a step (1.0) */
} somvr_ws_cfg;
void somvr_ws_cfg_default(somvr_ws_cfg *c);

/* ---- the landing -------------------------------------------------------------------------------------------------- */
enum { SOMVR_WSA_NONE = 0, SOMVR_WSA_OK = 1, SOMVR_WSA_NO = 2 };
enum { SOMVR_WSW_OK = 0, SOMVR_WSW_NEAR, SOMVR_WSW_WALL, SOMVR_WSW_LEDGE, SOMVR_WSW_HIGH, SOMVR_WSW_WATER, SOMVR_WSW_ROOM,
       SOMVR_WSW_ORC, SOMVR_WSW_SIGHT, SOMVR_WSW_NORAY, SOMVR_WSW_N };
const char *somvr_ws_why_name(int why); /* "ok", "at the feet", "a wall", "a ledge", ... */

typedef struct {
    int kind;        /* SOMVR_WSA_* */
    int why;         /* SOMVR_WSW_* */
    sv_vec3 land;    /* OK: the ground point (the ring); NO: the refused spot (on the ground under it when there is one) */
    float dist_cm;   /* from the root along the floor */
    float rise_cm;   /* land.y - the floor */
    int far;         /* the head's ray went past max_cm (or met nothing): the farthest along the view's heading */
    int through;     /* orcs the head's ray went through */
    int rays;        /* casts this find */
} somvr_ws_aim;

/* eye/gaze: the head's ray (world cm, gaze ~unit); root: Talion's root, floor_y: the ground under him; orcs: their
 * roots. Returns the kind. A ray with no answer = NONE (NORAY). */
int somvr_ws_find(const somvr_ws_cfg *c, somvr_climb_ray_fn ray, void *ctx, sv_vec3 eye, sv_vec3 gaze, sv_vec3 root,
                  float floor_y, const sv_vec3 *orcs, int n_orcs, somvr_ws_aim *o);

/* ---- the input ---------------------------------------------------------------------------------------------------- */
enum { SOMVR_WSP_IDLE = 0, SOMVR_WSP_AIM = 1, SOMVR_WSP_REST = 2 /* waits for the right stick to rest */ };
enum { SOMVR_WSE_AIM = 1u,      /* the aim began (the ring from now) */
       SOMVR_WSE_GO = 2u,       /* let go over a gold ring: the blink onto s->aim.land */
       SOMVR_WSE_NOGO = 4u,     /* let go over a red ring, none, or a flick: nothing */
       SOMVR_WSE_CANCEL = 8u,   /* the move stick: nothing */
       SOMVR_WSE_LOST = 16u,    /* the step could no longer go (a climb, a menu, a trip, the bow ...): nothing */
       SOMVR_WSE_REFUSED = 32u, /* pushed while it cannot (s->refused: 1 the cooldown, 2 cannot now) */
       SOMVR_WSE_TURN_NO = 64u, /* somvr_ws_aimed: the ring turned to a refusal */
       SOMVR_WSE_TURN_OK = 128u };
typedef struct {
    double t;
    int world;      /* the input live, no menu */
    int can;        /* the step can go now (on the ground, no climb, no bow, no finisher, no trip, the root and the head) */
    float rx, ry;   /* the right stick */
    float move;     /* the move stick's deflection */
} somvr_ws_in;
typedef struct {
    int phase;      /* SOMVR_WSP_* */
    double t0;      /* the aim's start */
    double next_ok; /* the cooldown's end */
    int walk_wait;  /* after the cancel: the walk waits until the move stick rests */
    int have_aim;   /* s->aim is this aim's */
    int refused;    /* the last REFUSED: 1 the cooldown, 2 cannot now */
    float held_s;   /* the last let-go's hold */
    somvr_ws_aim aim; /* the newest landing (set by somvr_ws_aimed while aiming) */
    uint64_t n_aim, n_go, n_nogo, n_tap, n_cancel, n_lost, n_cool, n_cannot, n_turn_no;
} somvr_ws_state;
void somvr_ws_init(somvr_ws_state *s);
/* one frame: the phase's changes from the input; returns SOMVR_WSE_* */
uint32_t somvr_ws_tick(somvr_ws_state *s, const somvr_ws_cfg *c, const somvr_ws_in *in);
/* while aiming: the newest landing; returns TURN_NO / TURN_OK when its kind changed to a refusal / to a landing */
uint32_t somvr_ws_aimed(somvr_ws_state *s, const somvr_ws_aim *a);
/* the walk is the player's now (0 while aiming and while the cancel's push lasts) */
int somvr_ws_walk_ok(const somvr_ws_state *s);

#endif
