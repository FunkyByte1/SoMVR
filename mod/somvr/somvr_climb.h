/* somvr_climb.h - SOMVR_CLIMB=b: hand-over-hand climbing, a prototype (docs/design-vr.md 4; docs/re-traversal.md 2.5's
 * recipe with the P7/P8 runtime notes; docs/re-xr-pipeline.md 12). Pure: no globals, no game reads, no OpenXR. Tested by
 * tests/test_climb.c; somvr.c's SOMVR_CLIMB region casts the rays, reads the game state, switches Talion's gravity and
 * moves him through the PM hook's one-shot extra move (so the game's collision still clips every move).
 *
 * Once per stereo frame (game thread, H4, after the grips are located for the eye views' time):
 *   grip     each controller's squeeze with a hysteresis; a new press (or one still inside the grab window) casts a ray
 *            from the hand along its palm (and along its fingers; while climbing also straight down: down_assist)
 *            through somvr.c's callback: a surface that holds (somvr_climb_surface_ok) within reach = a hold; none: a
 *            ray from the head to the hand that meets a surface at most inside_cm before the hand (the real hand went
 *            into the virtual wall: build 9's misses) holds there. The rays look near_cm past the reach: a surface
 *            there is a near miss (the logs, the miss cue). At the grab, two more rays: the same one edge_cm higher
 *            (no wall there = an edge hold, the wall's top is near; a ray down onto a top is one) and one down onto
 *            the top (its height). in.nograb (the bow out) spends a press instead: no ray, the grip must open before
 *            it may grab.
 *   cue      (cue = 1) an open hand raised over Talion's root (any open hand while a climb is on) is probed every
 *            cue_s: a surface it could hold within reach = E_NEAR on the way in (somvr.c: a light tick, HL:A's "this
 *            can be grabbed"), again only after cue_off_s out of reach; a press that found nothing while climbing or
 *            near a surface: miss_near (somvr.c: a short double buzz).
 *   climb    while any hand holds: Talion's gravity is off (somvr.c) and the newest hold drives: the body moves by minus
 *            that hand's motion in tracking space, rotated into the world like the rig (hand-over-hand: the hand stays
 *            where it grabbed), at most step_cm a frame (the rest carried, at most carry_cm); a tracking jump is not a
 *            pull. A hold whose grip opens lets go; one without tracking for lost_s lets go.
 *   top      an edge hold with the head above its top by top_cm, or the other hand reaching above it (within 1 m of the
 *            hold): push (the default: the climb goes on 1:1, the hands push the body up and over the edge; once the
 *            head is over_cm past the top hold toward the wall, the end "over": gravity back, Talion stands on it),
 *            release (gravity and walking take over), lift (our own pull-up: up until the head is lift_cm above the
 *            top (measured by the head, so a blocked lift stalls), then lift_fwd_cm over it, then release) or cmd (the
 *            stock LedgePullUp pulse, then release after cmd_s unless the game took over first).
 *   hang     letgo = hang (the default since build 10; the user after build 9: "I want to stay where I am when I let
 *            go. I should need to press a button to fall"): both hands open (or their tracking lost) while climbing =
 *            the phase hang: the climb stays on (gravity off), nothing moves, no timeout; a fresh grip on a wall climbs
 *            on (the same climb), the drop button (somvr.c: A) lets go = the end "drop" (gravity back: the fall). The
 *            input lost while holding hangs too (the grips must open before a new hold). With the ground within
 *            ground_cm under the root (a ray down; not once topped) both hands open end the climb as released
 *            instead (he stands: a hold taken at the foot of a wall never keeps him off the ground). letgo = fall:
 *            the old end.
 *   end      both hands open with letgo = fall (released), the drop button while holding or hanging (drop), the game not
 *            allowing it any more (somvr.c: FSM not Supported, mounted, third person ...; "stand down"), the input lost
 *            (letgo = fall), the timeout max_s (letgo = fall), a lift that stalls or runs out of time. Every end gives
 *            gravity back (somvr.c writes the flag, P8); after any end but a release the grips must open before a new
 *            hold. While the input is frozen (a menu, the pause) nothing changes and the clocks stop.
 */
#ifndef SOMVR_CLIMB_H
#define SOMVR_CLIMB_H

#include <stdint.h>

#include "somvr_pose.h"

enum { SOMVR_CLIMB_TOP_RELEASE = 0, SOMVR_CLIMB_TOP_LIFT = 1, SOMVR_CLIMB_TOP_CMD = 2, SOMVR_CLIMB_TOP_PUSH = 3 };
/* SOMVR_CLIMB_TOP= */
enum { SOMVR_CLIMB_RAY_PALM = 1, SOMVR_CLIMB_RAY_FWD = 2 };                              /* cfg.rays */
enum { SOMVR_CLIMB_PH_NONE = 0, SOMVR_CLIMB_PH_HOLD = 1, SOMVR_CLIMB_PH_LIFT = 2, SOMVR_CLIMB_PH_CMD = 3,
       SOMVR_CLIMB_PH_HANG = 4 /* letgo = hang: no hand holds, Talion stays (gravity off) until a grip or the drop */ };
enum { SOMVR_CLIMB_LETGO_FALL = 0, SOMVR_CLIMB_LETGO_HANG = 1 }; /* SOMVR_CLIMB_LETGO= */
enum { SOMVR_CLIMB_VIA_NONE = 0, SOMVR_CLIMB_VIA_PALM = 1, SOMVR_CLIMB_VIA_FWD = 2, SOMVR_CLIMB_VIA_DOWN = 3,
       SOMVR_CLIMB_VIA_INSIDE = 4 }; /* which ray found a surface (somvr_climb_via_name) */
enum {                                /* why a climb ended (somvr_climb_out.end_why, somvr_climb.last_end) */
    SOMVR_CLIMB_END_NONE = 0,
    SOMVR_CLIMB_END_RELEASED,         /* both grips opened (or lost their tracking) */
    SOMVR_CLIMB_END_GAME,             /* the game state does not allow it (in.allow = 0): stand down */
    SOMVR_CLIMB_END_INPUT,            /* the controller input is not live */
    SOMVR_CLIMB_END_TIMEOUT,          /* max_s */
    SOMVR_CLIMB_END_TOP,              /* the top reached, top_mode release */
    SOMVR_CLIMB_END_LIFTED,           /* the lift done */
    SOMVR_CLIMB_END_LIFT_STALL,       /* the lift did not raise the head (blocked) or ran out of time */
    SOMVR_CLIMB_END_CMD,              /* the stock pull-up was asked for and the hold waited cmd_s */
    SOMVR_CLIMB_END_OUTSIDE,          /* somvr_climb_stop: flat frames, the player changed, the hook stale */
    SOMVR_CLIMB_END_OVER,             /* push: the head over the top (past the edge by over_cm): standing on it */
    SOMVR_CLIMB_END_DROP,             /* the drop button (somvr.c: A) while holding or hanging: let go of the wall */
    SOMVR_CLIMB_NEND
};
enum {                                /* somvr_climb_out.events: what happened this frame (bit << 1 per hand where _L/_R) */
    SOMVR_CLIMB_E_GRAB_L = 1u << 0, SOMVR_CLIMB_E_GRAB_R = 1u << 1,         /* a hold */
    SOMVR_CLIMB_E_LET_L = 1u << 2, SOMVR_CLIMB_E_LET_R = 1u << 3,           /* a hold let go (grip open) */
    SOMVR_CLIMB_E_LOST_L = 1u << 4, SOMVR_CLIMB_E_LOST_R = 1u << 5,         /* a hold let go (no tracking for lost_s) */
    SOMVR_CLIMB_E_MISS_L = 1u << 6, SOMVR_CLIMB_E_MISS_R = 1u << 7,         /* a press found nothing within its window */
    SOMVR_CLIMB_E_NOCLIMB_L = 1u << 8, SOMVR_CLIMB_E_NOCLIMB_R = 1u << 9,   /* a press found a surface that is not
                                                                             * CanWallClimb (the press stops looking) */
    SOMVR_CLIMB_E_JUMP = 1u << 10,    /* the driving hand jumped more than jump_cm: that frame's motion ignored */
    SOMVR_CLIMB_E_TOP = 1u << 11,     /* the top reached (top_mode acts) */
    SOMVR_CLIMB_E_CARRY_CUT = 1u << 12, /* the carried motion was cut to carry_cm */
    SOMVR_CLIMB_E_LOW_L = 1u << 13, SOMVR_CLIMB_E_LOW_R = 1u << 14,         /* a hold too low to start a climb (low_cm:
                                                                             * the ground under him; the press stops) */
    SOMVR_CLIMB_E_HANG = 1u << 15,    /* letgo = hang: the last hold let go (or the input was lost): hanging */
    SOMVR_CLIMB_E_RESUME = 1u << 16,  /* a new hold while hanging: the climb goes on */
    SOMVR_CLIMB_E_DROP = 1u << 17,    /* the drop button: the climb ended (end_why DROP) */
    SOMVR_CLIMB_E_NEAR_L = 1u << 18, SOMVR_CLIMB_E_NEAR_R = 1u << 19,       /* cue: an open hand came within reach of a
                                                                             * surface it could hold */
};

typedef struct {
    float grip_on, grip_off; /* the squeeze's hysteresis (0.6 / 0.4, as the menus' LB/RB) */
    float reach_cm;          /* a hold: a climbable surface within this far ahead of the hand (25, design-vr.md 4) */
    float back_cm;           /* each grab ray starts this far behind the hand (10): a hand a little inside the wall */
    int rays;                /* SOMVR_CLIMB_RAY_* bits: along the palm, along the fingers (both) */
    float window_s;          /* a press out of reach keeps looking for a hold this long (0.25) */
    float step_cm;           /* the most Talion moves in one frame (8: 7.2 m/s at 90 Hz) */
    float carry_cm;          /* the most clamped motion carried into the next frames (20) */
    float jump_cm;           /* a held hand moving more than this in one frame is a tracking glitch, not a pull (25) */
    float lost_s;            /* a holding hand without tracking this long lets go (0.3) */
    float max_s;             /* a climb ends after this long (120) */
    float edge_cm;           /* the top test at a grab: the same ray this much higher finds no wall = an edge hold (25) */
    float edge_far_cm;       /* ... or finds it this much farther than the hold's surface (15) */
    float top_cm;            /* an edge hold's top is reached when the head is this far above it (10) */
    int top_mode;            /* SOMVR_CLIMB_TOP_* (push) */
    float over_cm;           /* push: the head this far past the top hold's point, toward the wall (its `toward`) =
                              * over the top: the climb ends, gravity back, Talion stands on it (30) */
    int down_assist;         /* while climbing, a press also looks straight down from the hand: a hand over a wall's
                              * top grabs the top whatever the hand's angle (1) */
    float low_cm;            /* a new climb starts only from a hold at most this far under Talion's root (his pelvis,
                              * ~100 cm over his feet): no climb from the ground he stands on (a hand reaching down,
                              * run 061702); holds while climbing are not limited; 0 = off (50) */
    float ramp;              /* push: the edge step: the part of a forward push (toward the wall) the game's collision
                              * did not apply lifts the body by ramp x that part (1: one to one; 0 off), at most
                              * step_cm a frame, while the head is under the top + lift_cm: Talion's capsule reaches
                              * ~100 cm under his root and steps over an edge only from 40 cm under it (run 060734) */
    float lift_cm;           /* lift: the head ends this far above the top (185: standing on it) */
    float lift_fwd_cm;       /* lift: then this far over the top, horizontally along the hold's ray (40) */
    float lift_s;            /* lift: at most this long (1.5) */
    float lift_still_s;      /* lift: the head not rising for this long while going up = blocked (0.3) */
    float cmd_s;             /* cmd: how long the hold waits after the stock pull-up pulse (0.6) */
    int letgo;               /* SOMVR_CLIMB_LETGO_*: both hands open while climbing = hang (default) or fall */
    float ground_cm;         /* hang: the ground this close under the root (a ray down; the root is ~102 cm over his
                              * feet) = he stands: both hands open end the climb as released, no hang (a hold taken
                              * from the ground and let go never holds him in the air: 125; 0 off) */
    float inside_cm;         /* the hand inside a wall: the head-to-hand ray meets a surface at most this far before the
                              * hand = a hold there (30; 0 off). Build 9: 19 of 40 grabs had the hand 0-10 cm in the
                              * wall (the ray's start behind it), the misses deeper */
    float near_cm;           /* the grab rays look this much past the reach: a surface there is a near miss (35) */
    int cue;                 /* the reach cue and the miss cue (1) */
    float cue_s;             /* each hand is probed this often (1/30 s) */
    float cue_off_s;         /* out of reach this long before the next E_NEAR (0.3) */
    float cue_raise_cm;      /* not climbing: only a hand at least this far over the root (the pelvis) is probed (10) */
} somvr_climb_cfg;
void somvr_climb_cfg_default(somvr_climb_cfg *c);

/* one ray's answer (somvr.c: the G21 cast with the player's own body skipped, the hit surface's CanWallClimb) */
typedef struct {
    int hit;        /* something other than Talion within the length */
    int climbable;  /* its surface record says CanWallClimb */
    float dist;     /* cm from the ray's start */
    sv_vec3 point;  /* the hit, world cm */
} somvr_climb_hit;
/* the ray callback: from (world cm), dir (unit), len (cm) -> *out; returns 1 if the cast ran (0: no answer, as no hit) */
typedef int (*somvr_climb_ray_fn)(void *ctx, int hand, sv_vec3 from, sv_vec3 dir, float len, somvr_climb_hit *out);

typedef struct {
    int valid;          /* the grip pose is usable this frame */
    sv_vec3 p;          /* the grip in tracking space, game convention (somvr_hand_game), cm */
    sv_vec3 pw;         /* the same in the world (somvr_rig_point), cm */
    sv_vec3 palm, fwd;  /* unit world directions: out of the palm, along the fingers */
    float squeeze;      /* 0..1 */
    int buttons;        /* the hand's button actions are active (SOMVR_HACT_INPUT) */
} somvr_climb_hand;

typedef struct {
    double t;           /* s */
    int live;           /* the controller input is live */
    int frozen;         /* live, but a menu or the pause: nothing changes, the clocks stop */
    int allow;          /* the game allows a climb now (somvr.c: FSM Supported, not mounted, first person, the objects) */
    int nograb;         /* no new hold now (somvr.c: the hands are on the bow): a press is spent, the grip must open
                         * before it may grab; holds already taken are kept */
    float rig_yaw;      /* psi - yaw0: tracking space -> world, as somvr_rig_vec */
    float rig_scale;    /* the player size s (somvr_rig_scale; 0 = 1): a hold carries s x the hand's tracked move */
    int head_ok;        /* head_y, head_x, head_z are this frame's head node */
    float head_y;       /* the frame's base y (the head node), cm */
    float head_x, head_z; /* ... its world x and z (push: over the top) */
    int root_ok;        /* root is Talion's root this frame (push's edge step: what the last move did) */
    sv_vec3 root;       /* world cm */
    int drop;           /* the drop button is held (somvr.c: A on the right controller); its press while a climb is on
                         * (holding or hanging) ends it: the fall */
    somvr_climb_hand hand[2];
} somvr_climb_in;

typedef struct {
    int down;           /* the squeeze's logical state (hysteresis) */
    int armed;          /* seen open since the last forced end: a press may grab */
    int looking;        /* a press inside its window, no hold yet */
    double press_t;
    int holding;
    uint32_t seq;       /* grab order: the newest hold drives */
    int have_last;
    sv_vec3 last_p;     /* tracking space at the last frame it was valid */
    double lost_t;      /* when its tracking was lost (< 0: tracked) */
    sv_vec3 anchor;     /* the hand at the grab, world cm */
    sv_vec3 hit;        /* the hold's surface point */
    sv_vec3 dir;        /* the ray that found it (world, unit) */
    float hit_dist;
    int edge;           /* an edge hold: the wall ends within edge_cm above */
    float top_y;        /* its top, world y cm (edge holds) */
    int top_found;      /* top_y from the down ray (else the bound hit.y + edge_cm) */
    sv_vec3 toward;     /* horizontal unit toward the wall: the ray's (a ray down onto a top: the head's to the hold,
                         * else the fingers'): where "over the top" is (push, lift) */
    int via;            /* SOMVR_CLIMB_VIA_*: the ray of the hold */
    float inside_by;    /* via inside: how far the hand was past the surface (cm) */
    float near_past;    /* this press's nearest surface past the reach (cm; < 0 none) */
    int near_via;       /* ... its ray */
    int cue_in;         /* cue: within reach at the last probe */
    double cue_t, cue_out_t; /* the next probe; when it last went out of reach */
} somvr_climb_hold;

typedef struct {
    somvr_climb_hold h[2];
    int active;         /* a climb: gravity off */
    int phase;          /* SOMVR_CLIMB_PH_* */
    double t0, phase_t, last_t;
    uint32_t seq;
    int driver;         /* the hand whose motion moves Talion (-1: none) */
    sv_vec3 carry;
    /* lift */
    float lift_y;       /* the head's target height */
    int lift_up_done;
    float fwd_left;
    sv_vec3 lift_dir;
    float lift_best;    /* the highest head seen while lifting */
    double lift_rise_t; /* when it last rose by 1 cm */
    int last_end;       /* SOMVR_CLIMB_END_* of the last climb */
    /* push: the top reached, the climb goes on 1:1 until the head is over it */
    int topped;
    sv_vec3 top_hit, top_toward;
    float top_y;        /* the top hold's top (the edge step's ceiling: top_y + lift_cm for the head) */
    sv_vec3 last_req;   /* the last frame's request (0: none) and the root then (the edge step) */
    sv_vec3 last_root;
    int have_root;
    double ramp_cm;     /* the edge step's lift, in total */
    int drop_down;      /* the drop button's state at the last frame (a press = its rising edge) */
    double hang_t;      /* when the hang began */
    /* totals */
    uint64_t climbs, grabs, lets, lost, misses, noclimb, tops, lifts, cmds, standdowns, timeouts, jumps, cuts, rays,
        frames, moves, overs, lows, hangs, resumes, drops, insides, nears, cue_rays;
    double req_cm;      /* the length of every move asked for */
} somvr_climb;

typedef struct {
    int active;             /* gravity off for Talion after this frame */
    int started, ended;     /* edges this frame */
    int end_why;            /* SOMVR_CLIMB_END_* when ended */
    int move;               /* move_w is a request for the next player update */
    sv_vec3 move_w;         /* world cm */
    uint32_t events;        /* SOMVR_CLIMB_E_* */
    int top_cmd;            /* cmd mode: pulse the stock LedgePullUp now */
    int top_by;             /* with SOMVR_CLIMB_E_TOP: 1 the head over the top, 2 the other hand reaching over it */
    float ramp;             /* the edge step's lift in this frame's request (cm) */
    int rays;               /* rays cast this frame */
    int grab_rays[2];       /* rays cast for each hand's grab test */
    somvr_climb_hit grab_hit[2]; /* each hand's grab ray answer this frame (logs; hit = 0 when none cast) */
    int grab_cast[2];       /* the frame's cast (1 = its first ray) that gave grab_hit[h] (0: none): the caller's ray log */
    int miss_near[2];       /* with SOMVR_CLIMB_E_MISS: the press was made while climbing or near a surface (the cue) */
    int driver;             /* the driving hand (-1 none) */
} somvr_climb_out;

void somvr_climb_init(somvr_climb *s);
/* one stereo frame; ray may be NULL (no grab can be found then) */
void somvr_climb_step(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_ray_fn ray,
                      void *ctx, somvr_climb_out *out);
/* the end from outside (why: SOMVR_CLIMB_END_*): every hold released, the grips must open before a new one */
void somvr_climb_stop(somvr_climb *s, int why, somvr_climb_out *out);
const char *somvr_climb_end_name(int why);
const char *somvr_climb_via_name(int via);
const char *somvr_climb_phase_name(int phase);

/* the pieces (tested on their own) */
int somvr_climb_grip(int *down, float squeeze, float on, float off); /* the hysteresis; returns the new state */
sv_vec3 somvr_climb_to_world(sv_vec3 d_track, float rig_yaw);       /* R(Y(rig_yaw)) d, as somvr_rig_vec */
sv_vec3 somvr_climb_clamp(sv_vec3 v, float max_len);                /* v shortened to max_len (NaN: 0) */

/* which surfaces hold a hand (SOMVR_CLIMB_SURF=walls|game|any; somvr.c's ray callback answers hit.climbable with it).
 * walls (default): every solid surface record, the game's CanWallClimb ignored: not a character's (Flesh*, Armor*),
 * not sky, air, invisible ones or player blockers, not liquids (water*, liquid*), mud, foliage, ice or slippery ones
 * (*Slip*); game: CanWallClimb only (in this game's data it marks the 17 No_WallRun / AxeClimb / Air / blocker records,
 * not plain 'stone', 'wood' or 'WallRun': runs 052758 and 053549, re-xr-pipeline.md 32); any: all but Flesh* (tests).
 * name: the record's name ("" = unnamed: walls holds it); can: the game's CanWallClimb */
enum { SOMVR_CLIMB_SURF_WALLS = 0, SOMVR_CLIMB_SURF_GAME = 1, SOMVR_CLIMB_SURF_ANY = 2 };
int somvr_climb_surface_ok(int mode, const char *name, int can);

#endif
