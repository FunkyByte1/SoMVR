/* somvr_bow.h - SOMVR_BOW=1: the VR bow (docs/re-bow.md, design-vr.md 3 "The bow", re-xr-pipeline.md 13). The bow is in
 * the left hand, the right hand draws the string, the arrow flies from the bow hand along the line from the string hand
 * to the bow hand. Pure: no globals, no game reads, no OpenXR. Tested by tests/test_bow.c; somvr.c's SOMVR_BOW region
 * holds the game's own aim and fire commands (the binding layer), writes the bow ray into the free-aim ray's transform
 * (BOW_FA_CAMXF, re-bow.md 1.4 recipe 1: the game then picks the target and aims its own arrow), plays the haptics and,
 * with SOMVR_ARMS, puts the bow hand's weapon node on the shot.
 *
 * Once per stereo frame (game thread, H4, after the grips are located for the eye views' time and mapped into the world):
 *   enter    the string hand's grip closes (a new press, or one still inside its window) within nock_r_cm of the nock
 *            point (the bow hand's grip + nock_ofs in its frame): aim and draw at once; or the aim button (a press).
 *            While aiming the aim command is held (the game's data-driven bow mode, re-bow.md 1.1).
 *   draw     while the string grip stays closed: the draw = |nock - string hand|; ARMED past min_draw_cm, FULL at
 *            full_draw_cm, a TICK every tick_cm of new draw (the tension haptic).
 *   ray      origin = the nock point; direction = from the string hand to the nock, blended in from the bow hand's own
 *            forward (+Z) over dir_min_cm .. dir_min_cm + dir_blend_cm of draw (a hand at the nock has no direction);
 *            while aimed but not drawn: the bow hand's forward (idle_ray BOW) or none (HEAD: the game's camera ray).
 *   release  the grip opens with the draw (its last closed frame) past min_draw_cm: a shot is asked and the ray latched
 *            (the stock arrow may read the aim point at its animation's SHOOT key). The shot goes (FIRE) once the
 *            game's aim is on (game_aim: the free-aim ray was asked for) and the aim was held aim_min_s (the mode's
 *            HeldDuration): somvr.c fires our own arrow along the latched ray (the default), or a fire command is on
 *            for fire_s (tap) - or, fire_mode HOLD, held from ARMED to the release. With release_now (build 11) it
 *            goes at once whenever bow_ready (the bow is the active item: a hit that knocks the game's aim mode out
 *            does not hold the arrow back). A shot that waits wait_s without the game's aim (or bow_ready) is
 *            DROPPED. follow_s after the shot the latched ray ends (back to AIM).
 *            The grip opening short of min_draw_cm, or the string hand lost for lost_s, is a LETDOWN (no shot).
 *   exit     not drawing and the button not held: the hands farther apart than exit_cm for exit_s ends the aim; any
 *            phase: the input not live, the game not allowing it (a menu, third person, a climb ...), the bow hand lost
 *            for lost_s. A pending shot is dropped by an exit.
 *   quiver   build 16 (cfg.quiver, SOMVR_BOW_QUIVER=on; the string = the right trigger, SOMVR_BOW_STRING=trigger): while
 *            the bow is out, a string press inside the quiver's slot (in.quiver) takes an arrow into the string hand
 *            (TAKE; with in.arrows 0: nothing, EMPTY). Held, the arrow touched to the nock (within nock_r_cm) is nocked
 *            at once (NOCK, no new press); let go before that it is gone (DROP, nothing spent). A release below
 *            min_draw_cm leaves the arrow resting on the string (LETDOWN, arrow REST): a press at the nock draws it
 *            again. A press at the nock with no arrow anywhere: NOARROW. An exit puts any arrow back. The hands apart
 *            end nothing while an arrow is held or the hand is in the quiver.
 *   zone     build 17 (cfg.nock_zone, SOMVR_BOW_NOCK_ZONE=string): "at the nock" is anywhere within zone_r_cm of the
 *            string's middle (the segment zone_half_cm either way of the nock along the bow's limbs, cfg.limb_ax: the
 *            riser and the string's middle third), not more than zone_front_cm past it toward the target; the arrow
 *            snaps onto the string there and the draw counts only the pull back behind the nock along the shot
 *            ((nock - string) . fwd, 0 in front): a hand that nocks 20 cm beside the bow has drawn nothing (the user's
 *            build 16 session: nocks only at 13.7-14.9 cm of the 15, the hand 18 and 22 cm from the grip without one).
 *            cfg.noarrow_take (SOMVR_BOW_NOARROW_CUE=take): a press in the zone with no arrow anywhere and arrows left
 *            takes one and nocks it at once (TAKE | NOCK, the quiver skipped); none left: EMPTY.
 *   grab     build 19 (re-xr-pipeline.md 71; the user's 17b session: every grab was a whole fist, A with the trigger, and
 *            3 of 10 closed just outside the quiver - each a silent MISS, the hand empty at the bow). cfg.quiver_held: a
 *            string press that took nothing keeps looking for the quiver while it stays closed - the hand coming in
 *            takes the arrow (TAKE, or EMPTY), as a held arrow nocks by touching the string. cfg.noarrow_cm: an empty
 *            press that near the nock (the bow's side of the body, never the quiver's) is NOARROW at once.
 *
 * The bow's frame in the bow hand's grip (build 16, somvr_bow_hold_q): the grip's frame in the game convention
 * (somvr_hand_game) has +Z along the fist's tube from the little finger to the thumb (OpenXR's -Z), +X = OpenXR's +X
 * (the left palm's normal, the right hand's back: to the right for both hands held out), +Y = OpenXR's +Y = Z x X: with
 * the arm out and the fist upright, +Z points up and +Y back toward the player. A real bow lies with its limbs along the
 * tube and shoots along -Y (the knuckles' way, along the arm), its string toward the face: HOLD_FIST. Builds 11-15b
 * (HOLD_AUTO) laid the bow's shot on +Z and its limbs on +Y - right for the headless hands' sword pose (the tube level and
 * ahead), flat along the arm for a real bow hold (the user's 10-26 mark 1). cfg.shot_ax carries the shot axis.
 */
#ifndef SOMVR_BOW_H
#define SOMVR_BOW_H

#include <stdint.h>

#include "somvr_pose.h"

enum { SOMVR_BOW_PH_IDLE = 0, SOMVR_BOW_PH_AIM = 1, SOMVR_BOW_PH_DRAW = 2, SOMVR_BOW_PH_FOLLOW = 3 };
enum { SOMVR_BOW_FIRE_TAP = 0, SOMVR_BOW_FIRE_HOLD = 1 }; /* SOMVR_BOW_FIRE= */
enum { SOMVR_BOW_IDLE_BOW = 0, SOMVR_BOW_IDLE_HEAD = 1 }; /* SOMVR_BOW_IDLE_RAY= */
enum {                                /* why an aim ended (somvr_bow_out.end_why, somvr_bow.last_end) */
    SOMVR_BOW_END_NONE = 0,
    SOMVR_BOW_END_APART,              /* the hands separated (not drawing, the button not held) */
    SOMVR_BOW_END_GAME,               /* the game does not allow the bow now (in.allow = 0) */
    SOMVR_BOW_END_INPUT,              /* the controller input is not live */
    SOMVR_BOW_END_LOST,               /* the bow hand without tracking for lost_s */
    SOMVR_BOW_END_OUTSIDE,            /* somvr_bow_stop: no step for frames, the player changed ... */
    SOMVR_BOW_END_SLOT,               /* somvr_bow_stop (build 15b, SOMVR_BOW_DRAW=slot): put back at its slot */
    SOMVR_BOW_NEND
};
enum {                                /* somvr_bow_out.events: what happened this frame */
    SOMVR_BOW_E_ENTER = 1u << 0,      /* the aim began (by the draw or the button: somvr_bow.by_button) */
    SOMVR_BOW_E_NOCK = 1u << 1,       /* the string grabbed at the nock: a draw began */
    SOMVR_BOW_E_ARMED = 1u << 2,      /* the draw passed min_draw_cm */
    SOMVR_BOW_E_FULL = 1u << 3,       /* the draw reached full_draw_cm */
    SOMVR_BOW_E_TICK = 1u << 4,       /* another tick_cm of draw (the tension haptic) */
    SOMVR_BOW_E_RELEASE = 1u << 5,    /* the string let go past min_draw_cm: a shot asked, the ray latched */
    SOMVR_BOW_E_FIRE = 1u << 6,       /* the fire command goes on now (tap; hold mode: the hold ends = the shot) */
    SOMVR_BOW_E_HOLD = 1u << 7,       /* hold mode: the fire command is held from now (armed, the game's aim on) */
    SOMVR_BOW_E_LETDOWN = 1u << 8,    /* the string let go short of min_draw_cm, or its hand lost: no shot */
    SOMVR_BOW_E_DROPPED = 1u << 9,    /* a shot waited wait_s without the game's aim, or an exit came first */
    SOMVR_BOW_E_EXIT = 1u << 10,      /* the aim ended (end_why) */
    SOMVR_BOW_E_MISS = 1u << 11,      /* while aiming: a string press whose window ran out away from the nock */
    SOMVR_BOW_E_TAKE = 1u << 12,      /* quiver: an arrow taken into the string hand */
    SOMVR_BOW_E_EMPTY = 1u << 13,     /* quiver: a press in it with no arrows (the game's count 0): nothing taken */
    SOMVR_BOW_E_DROP = 1u << 14,      /* quiver: the string let go with the arrow not nocked: the arrow is gone */
    SOMVR_BOW_E_NOARROW = 1u << 15,   /* quiver: a string press at the nock with no arrow in the hand or on the string */
};
enum { SOMVR_BOW_HOLD_CAL = 0, SOMVR_BOW_HOLD_AUTO = 1, SOMVR_BOW_HOLD_FIST = 2 }; /* SOMVR_BOW_HOLD= */
enum {                                /* somvr_bow.arrow / somvr_bow_out.arrow: where the arrow is */
    SOMVR_BOW_ARROW_NONE = 0,
    SOMVR_BOW_ARROW_HAND = 1,         /* in the string hand (quiver: taken, not nocked) */
    SOMVR_BOW_ARROW_STRING = 2,       /* nocked, the string hand on it (drawing) */
    SOMVR_BOW_ARROW_REST = 3          /* quiver: on the string, the hand let go below min_draw_cm */
};

typedef struct {
    int bow_hand;            /* SOMVR_HAND_L (0) or _R (1); the string hand is the other */
    float nock_ofs[3];       /* the nock point in the bow hand's grip frame, cm (0, 0, 0: the grip) */
    float nock_r_cm;         /* a draw starts with the string hand within this of the nock (15) */
    float grip_on, grip_off; /* the string's hysteresis (0.6 / 0.4, as the climb's) */
    float window_s;          /* a press away from the nock keeps looking for it this long (0.25) */
    float min_draw_cm;       /* a release shoots only from this draw on (20) */
    float full_draw_cm;      /* tension 1 (55) */
    float dir_min_cm;        /* the string's direction starts counting at this draw (5) ... */
    float dir_blend_cm;      /* ... and has the whole weight this much later (10) */
    float tick_cm;           /* a tension tick every this much new draw (4) */
    float exit_cm;           /* not drawing: hands farther apart than this ... (40) */
    float exit_s;            /* ... for this long end the aim (0.8: time to nock the next arrow) */
    float lost_s;            /* a hand without tracking this long counts as lost (0.3) */
    float aim_min_s;         /* the fire command waits until the aim command was held this long (0.25) */
    float fire_s;            /* the tap's length (0.1) */
    float wait_s;            /* a shot waits at most this long for the game's aim (0.6) */
    float follow_s;          /* the latched ray lasts this long after the shot (0.4) */
    int fire_mode;           /* SOMVR_BOW_FIRE_* (TAP) */
    int idle_ray;            /* SOMVR_BOW_IDLE_* (BOW) */
    int release_now;         /* build 11 (SOMVR_BOW_RELEASE=now): the shot goes at the release whenever the bow is ready
                                (bow_ready), the game's aim on or not; else (0) it waits for the game's aim as before */
    int idle_nock;           /* the string grip at the nock enters from idle (1, the default); build 15b's
                                SOMVR_BOW_DRAW=slot: 0, the bow comes only from its slot (the button) */
    float shot_ax[3];        /* build 16: the bow's shot axis in the bow hand's grip frame (unit; (0, 0, 1) = builds
                                11-15b; HOLD_FIST: (0, -1, 0), somvr_bow_hold_q's +Z) */
    int quiver;              /* build 16: arrows come from the quiver (see "quiver" above); 0 = any press at the nock */
    int nock_zone;           /* build 17: 0 = within nock_r_cm of the nock point (build 16), 1 = the string's zone ("zone") */
    float zone_r_cm;         /* the zone: this far from the string's middle segment (25) ... */
    float zone_half_cm;      /* ... which runs this far either way of the nock along the limbs (12) ... */
    float zone_front_cm;     /* ... and at most this far past the nock toward the target (5) */
    float limb_ax[3];        /* the bow's upper limb in the bow hand's grip frame (unit; (0, 1, 0) = builds 11-15b;
                                HOLD_FIST: (0, 0, 1), somvr_bow_hold_q's +Y) */
    int noarrow_take;        /* build 17: a press in the zone with no arrow takes one and nocks it (see "zone") */
    int quiver_held;         /* build 19 (SOMVR_BOW_QUIVER_GRAB=held): the string held since a press that took nothing
                                takes an arrow when the hand comes into the quiver (see "grab") */
    float noarrow_cm;        /* build 19 (SOMVR_BOW_NOARROW_CM): an empty press this near the nock (outside the zone too)
                                is told as NOARROW at once, not a silent MISS (0: the zone only, build 18) */
    float zone_back_cm;      /* build 19 (SOMVR_BOW_STRING_REST=ours: the string's rest line this far behind the nock):
                                the zone's core reaches back to it - touching the string seen at rest nocks (0: build 17) */
} somvr_bow_cfg;
void somvr_bow_cfg_default(somvr_bow_cfg *c);

typedef struct {
    double t;                /* s */
    int live;                /* the controller input is live and the player is in the world (no menu) */
    int allow;               /* the game allows the bow now (somvr.c: first person, no climb, the player objects) */
    int game_aim;            /* the game's free aim is on (BOW_FA_CAMXF asked for the ray within the last frames) */
    int valid[2];            /* each hand's pose is usable this frame */
    sv_vec3 p[2];            /* the grips in the world, cm (game convention) */
    sv_quat q[2];            /* their world orientations */
    float string;            /* the string hand's squeeze (or trigger) 0..1 */
    int button;              /* the aim button is held (0 without one) */
    int bow_ready;           /* the bow is the game's active item (with release_now: our shot needs no aim mode) */
    int quiver;              /* cfg.quiver: the string hand is inside the quiver's slot */
    int arrows;              /* cfg.quiver: the game's arrows (-1 = unknown: an arrow is given, the shot's gate decides) */
} somvr_bow_in;

typedef struct {
    int phase;               /* SOMVR_BOW_PH_* */
    int by_button;           /* this aim began with the button */
    int down;                /* the string's logical state (hysteresis) */
    int looking;             /* a string press inside its window, no nock yet */
    double press_t;
    int button_was;
    double aim_t0, phase_t, apart_t;
    double lost_t[2];        /* when each hand's tracking was lost (< 0: tracked) */
    int have[2];
    sv_vec3 p[2];            /* the last valid grips (world) */
    sv_quat q[2];
    float draw_cm;           /* the draw of the last frame the string was held */
    float draw_max, tension;
    int armed, full, ticks;
    int shot;                /* a shot asked, the fire command not sent yet */
    double shot_t;
    int fire_on;             /* hold mode: the fire command held */
    double fire_t0, fire_until;
    uint32_t fire_seq;       /* +1 per FIRE: the input tick sends every one at least once */
    int have_ray;
    sv_vec3 origin, dir;     /* the last ray */
    sv_vec3 lat_o, lat_d;    /* the release's ray (FOLLOW) */
    int last_end;            /* SOMVR_BOW_END_* of the last aim */
    /* totals */
    uint64_t aims, by_btn, draws, shots, fired, dropped, letdowns, misses, exits[SOMVR_BOW_NEND], frames, ray_frames;
    float last_draw;         /* the draw of the last release */
    double last_wait;        /* how long the last fired shot waited for the game */
    int arrow;               /* SOMVR_BOW_ARROW_* */
    uint64_t takes, empties, drops, noarrows;
    int qheld;               /* build 19: the string held since a press that took nothing (cfg.quiver_held) */
    uint64_t held_takes;     /* ... arrows taken that way (the hand came into the quiver after the press) */
} somvr_bow;

typedef struct {
    int phase;
    int aim;                 /* hold the aim command */
    int fire;                /* the fire command is on at the step's time (the input tick: somvr_bow_cmds) */
    int ray;                 /* origin/dir replace the free-aim ray */
    sv_vec3 origin, dir;     /* world cm, unit */
    float draw_cm, tension;  /* now (0 when not drawing) */
    float nock_dist;         /* the string hand to the nock (cm; -1 = a hand unknown) */
    float zone_dist;         /* the string hand to the nock's zone's core (nock_zone: the string's middle segment; else
                                = nock_dist; -1 = a hand unknown; past zone_front_cm in front: 999) */
    uint32_t events;         /* SOMVR_BOW_E_* */
    int end_why;             /* with SOMVR_BOW_E_EXIT */
    double wait_s;           /* with SOMVR_BOW_E_FIRE: how long the shot waited for the game */
    int arrow;               /* SOMVR_BOW_ARROW_* after the step */
} somvr_bow_out;

void somvr_bow_init(somvr_bow *s);
void somvr_bow_step(somvr_bow *s, const somvr_bow_cfg *c, const somvr_bow_in *in, somvr_bow_out *out);
/* the end from outside (why: SOMVR_BOW_END_*): the aim off, a pending shot dropped, the hold released */
void somvr_bow_stop(somvr_bow *s, int why, double t, somvr_bow_out *out);
/* the input tick's commands at time t: aim while a phase is on; fire inside the tap's window or while held, and for
 * every new FIRE at least once (*seen = the last fire_seq sent) */
void somvr_bow_cmds(const somvr_bow *s, double t, uint32_t *seen, int *aim, int *fire);
const char *somvr_bow_phase_name(int phase);
const char *somvr_bow_end_name(int why);

/* ---- the commands and our own shot (P9, re-bow.md 9) ------------------------------------------------------------ */
/* The aim is the game's PC_WraithRanged mode: CombatSecondary03 124, held, HeldDuration 0.2 s (P9 wmdump). The fire is
 * not mode data: 125 CombatSecondary04 enters PC_Stealth (the dagger), so it is never sent by default. Default fire =
 * our own shot through the game's FUN_02332c50 with the arrow counter decremented (re-bow.md 2.3 B); SOMVR_BOW_FIRE_CMD
 * (unset: 0) sends a command instead, for when the real one is known. */
#define SOMVR_BOW_AIM_CMD_DEFAULT 124 /* CombatSecondary03 */
#define SOMVR_BOW_FIRE_CMD_DEFAULT 0  /* none: our own shot */
#define SOMVR_BOW_STEALTH_CMD 125     /* CombatSecondary04 = PC_Stealth's entry (P9): not a fire command */

enum {                                /* somvr_bow_shot_check: the first gate that fails */
    SOMVR_BOW_SHOT_OK = 0,
    SOMVR_BOW_SHOT_SITES,             /* a fire site did not match at start */
    SOMVR_BOW_SHOT_NO_AIM,            /* the game's free aim (the bow mode) is not on */
    SOMVR_BOW_SHOT_COMBAT,            /* the player's server Combat, its owner or the inventory did not check out */
    SOMVR_BOW_SHOT_NO_ITEM,           /* no active item (the slot, the item, its record) */
    SOMVR_BOW_SHOT_NOT_RANGED,        /* the active item has no RangedWeaponData: FUN_02332c50 would not shoot */
    SOMVR_BOW_SHOT_ARROWS_UNKNOWN,    /* the arrow counter not readable (BOW_AMMO*) */
    SOMVR_BOW_SHOT_NO_ARROWS,         /* 0 arrows (and not unlimited) */
    SOMVR_BOW_SHOT_RAY,               /* no finite ray */
    SOMVR_BOW_NSHOT
};
typedef struct {
    int sites, game_aim, combat, item, ranged; /* each 1 = that gate passes */
    int arrows;                       /* the arrow count, -1 = unknown */
    int unlimited;
    sv_vec3 origin, dir;              /* the shot's ray (world cm; dir unit) */
    int any_aim;                      /* release_now: the game's aim is not a gate (the arrow flies along the ray) */
} somvr_bow_shot_in;
int somvr_bow_shot_check(const somvr_bow_shot_in *in); /* SOMVR_BOW_SHOT_* */
const char *somvr_bow_shot_why(int r);
/* the arrow counter's change after a shot: -1, or 0 when unlimited (the game shows Infinity then) */
int somvr_bow_shot_cost(const somvr_bow_shot_in *in);
/* the shot's fire transform (LTRigidTransform: pos xyz, quat xyzw): pos = origin, +Z = dir, no roll (somvr_bow_look) */
void somvr_bow_fire_xf(sv_vec3 origin, sv_vec3 dir, float yaw_hint, float xf[7]);

/* ---- the geometry (tested on their own) ------------------------------------------------------------------------- */
/* the nock point: p + R(q) nock_ofs */
sv_vec3 somvr_bow_nock(const somvr_bow_cfg *c, sv_vec3 p, sv_quat q);
/* build 17: is the string hand at the nock? point (nock_zone 0): within nock_r_cm of the nock; zone: within zone_r_cm of
 * the segment nock -/+ zone_half_cm along R(q) limb_ax (build 19: swept zone_back_cm back along the shot to the string's
 * rest line) and not more than zone_front_cm in front of the nock along the
 * shot. *zd (may be NULL) = the distance tested against the radius (999 in front of the zone) */
int somvr_bow_at_nock(const somvr_bow_cfg *c, sv_vec3 bow_p, sv_quat bow_q, sv_vec3 string, float *zd);
/* build 17: the draw with the zone: the pull back behind the nock along the bow's shot, (nock - string) . fwd, at least 0 */
float somvr_bow_pull(const somvr_bow_cfg *c, sv_quat bow_q, sv_vec3 nock, sv_vec3 string);
/* the bow's forward (the shot) for the bow hand's grip q: R(q) shot_ax (unit; a zero shot_ax reads (0, 0, 1)) */
sv_vec3 somvr_bow_fwd(const somvr_bow_cfg *c, sv_quat q);
/* the bow model's rotation in the bow hand's grip frame (its +Z the shot, +Y the upper limb, -Z toward the string).
 * CAL, AUTO: identity (builds 11-15b: the shot on the grip's +Z, the limbs on +Y). FIST: the limbs along the tube (+Z),
 * the shot along -Y; then tilt_deg about the grip's +X (+ = the upper limb toward the target) and cant_deg about the
 * shot (+ = the upper limb toward the string hand's side: right for a left-hand bow) */
sv_quat somvr_bow_hold_q(int hold, int bow_hand, float tilt_deg, float cant_deg);
/* the shot's direction from the string hand to the nock (unit; *len = the draw, cm); a zero draw: +Z, len 0 */
sv_vec3 somvr_bow_draw_dir(sv_vec3 nock, sv_vec3 string, float *len);
/* the string direction's weight 0..1 at this draw (smoothstep over dir_min_cm .. + dir_blend_cm) */
float somvr_bow_dir_weight(const somvr_bow_cfg *c, float draw_cm);
/* build 16: the draw along the bow's shot axis, |(nock - string) . fwd| (cm; 0: the hand beside the nock). The string
 * direction's weight counts this, not |nock - string|: a hand that nocks 10 cm beside the nock has drawn nothing (boot
 * 003313: the bow turned flat at the nock by the alignment). Either way along the axis (a bow held backwards: the
 * string's direction still wins, as before) */
float somvr_bow_back(const somvr_bow_cfg *c, sv_quat bow_q, sv_vec3 nock, sv_vec3 string);
/* the ray's direction: the bow's forward R(q) shot_ax turned toward the draw direction by the weight of the draw back
 * along it (somvr_bow_back; unit) */
sv_vec3 somvr_bow_ray_dir(const somvr_bow_cfg *c, sv_quat bow_q, sv_vec3 nock, sv_vec3 string, float *draw);
/* a rotation with R (0,0,1) = dir and R (0,1,0) in the vertical plane through dir (yaw then pitch, no roll); a
 * vertical dir keeps the heading `yaw_hint` (rad). The free-aim transform's quaternion */
sv_quat somvr_bow_look(sv_vec3 dir, float yaw_hint);
/* q turned by the shortest arc that takes R(q)(0,0,1) onto dir, by the share w (0 = q, 1 = the whole arc) */
sv_quat somvr_bow_align(sv_quat q, sv_vec3 dir, float w);
/* the same for the axis ax of q's frame (build 16: the bow's shot axis, cfg.shot_ax) */
sv_quat somvr_bow_align_ax(sv_quat q, sv_vec3 ax, sv_vec3 dir, float w);
const char *somvr_bow_arrow_name(int arrow);
/* build 18 (SOMVR_BOW_STRING_DRAW): our own string on the layer this frame? mode 0 off, 1 on (always while the bow is
 * out: at rest between the limb ends, drawn to the string hand), 2 drawn (only while an arrow is on the string and the
 * hand holds it - SOMVR_BOW_PH_DRAW: the game's own string at rest, so never two at once there) */
enum { SOMVR_BOW_SDRAW_OFF = 0, SOMVR_BOW_SDRAW_ON = 1, SOMVR_BOW_SDRAW_DRAWN = 2 };
int somvr_bow_string_shown(int mode, int phase);

#endif
