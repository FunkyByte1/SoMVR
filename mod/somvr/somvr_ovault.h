/* somvr_ovault.h - build 31 (som-b31-hands, docs/re-xr-pipeline.md 135.9): the vault over an orc by the hands. Pure (no
 * globals, no game reads; somvr_hands31_game.c feeds it). Tested by tests/test_ovault.c.
 *
 * The open hand closed on top of a facing orc's head (its crown: the palm at most top_cm from the head point, not under
 * it by more than below_cm) holds there as the climb holds a wall: the body moves opposite to the hand's own tracked
 * motion, 1:1 (push down = rise; pull back = forward). Let go with the hand still moving fast = the throw: the body's
 * velocity is minus the hand's mean velocity over the last win_s (the climb fling's rule), kept only up and over the orc
 * (the backward part and any downward part dropped, the sideways part halved), the part over him times over_gain; a
 * throw too weak to carry past him (its arc's landing short of his root + past_cm) is no throw - a plain let-go (the body
 * drops back to the ground where it is). The flight is that arc under g, step by step until the ground takes it. Nothing
 * moves that the hands did not move or throw. */
#ifndef SOMVR_OVAULT_H
#define SOMVR_OVAULT_H
#include "somvr_pose.h"
#include <stdint.h>

typedef struct {
    float top_cm;      /* the palm within this of the orc's head point = on his crown (30: his idle sway) */
    float below_cm;    /* ... and not more than this under the head point (5: the collar under it is the grab's) */
    float grip_on, grip_off;
    float hold_max_s;  /* a hold this long ends (no throw) (5) */
    float break_cm;    /* his body (root) this far along the floor from where it stood at the grip ends the hold (40: he
                        * walked off; his head's idle sway moves it ~30 cm and drags nothing - the hold is a fixed point) */
    float rise_max_cm; /* the body at most this far over where the hold began (90) */
    float win_s;       /* the throw's window (0.1 s) */
    float over_gain;   /* the throw's part over him x this (1.6: Talion's own vault is an acrobat's) */
    float min_up;      /* the throw's upward part at least this (100 cm/s) */
    float min_over;    /* its part over him at least this (120 cm/s) */
    float max_cms;     /* the throw at most this fast (550 cm/s) */
    float side;        /* the sideways part x this (0.5), at most half the part over him */
    float g;           /* 981 cm/s^2 */
    float past_cm;     /* the arc's landing at least this past his root (50) */
    float flight_max_s;/* (1.5 s) */
} somvr_ov_cfg;
void somvr_ov_cfg_default(somvr_ov_cfg *c);

enum { SOMVR_OV_IDLE = 0, SOMVR_OV_HOLD, SOMVR_OV_FLIGHT };
enum {
    SOMVR_OVE_GRAB = 1u,     /* a hand took his crown */
    SOMVR_OVE_LETGO = 2u,    /* the hold ended without a throw (slow, short, held too long, he moved, a hit, not live) */
    SOMVR_OVE_THROW = 4u,    /* the throw: the flight begins (v0, land_cm) */
    SOMVR_OVE_LAND = 8u,     /* the flight met the ground */
    SOMVR_OVE_BLOCKED = 16u, /* a flight step kept less than half of its move along the floor: the rest of it straight down */
    SOMVR_OVE_JOIN = 32u     /* the other hand joined the hold */
};
enum { SOMVR_OVW_NONE = 0, SOMVR_OVW_OPEN, SOMVR_OVW_SHORT, SOMVR_OVW_MAX, SOMVR_OVW_MOVED, SOMVR_OVW_HIT, SOMVR_OVW_LIVE,
       SOMVR_OVW_LANDED, SOMVR_OVW_TIME, SOMVR_OVW_N };
const char *somvr_ov_why_name(int w);

#define SOMVR_OV_HIST 16
typedef struct {
    int valid;
    sv_vec3 p;       /* tracking space (the hand's own motion), cm */
    sv_vec3 pw;      /* the world, cm */
    float squeeze;   /* 0..1 */
} somvr_ov_hand;

typedef struct {
    double t, pose_t;     /* now; the views' display time (the throw's clock; 0 = t) */
    int live;             /* on foot, in the world, nothing else holds the hands or the body */
    float rig_yaw, rig_scale;
    somvr_ov_hand hand[2];
    int free[2];          /* that hand may take his crown (empty, not on the sword, the bow, a wall, an orc) */
    int orc_ok;           /* a vaultable orc (in front, near, alive, on his feet): its head and root */
    sv_vec3 head, orc_root;
    int root_ok;
    sv_vec3 root;         /* Talion's root (his feet), world cm */
    int hurt;             /* a hit on Talion since the last step */
} somvr_ov_in;

typedef struct {
    int phase;               /* SOMVR_OV_* */
    int closed[2];           /* the grips (hysteresis) */
    int holds[2];            /* the hand drives the hold */
    sv_vec3 last_p[2];       /* the driving hands' last tracked points */
    struct { double t; sv_vec3 p; } h[2][SOMVR_OV_HIST];
    int nh[2], hpos[2];
    double t0, t_last;
    sv_vec3 head0, root0, over; /* at the grip: his head, Talion's root; the horizontal unit from Talion to him */
    sv_vec3 orc0;            /* his root at the grip */
    float rise;              /* the body over root0 (cm, the asked moves) */
    sv_vec3 v;               /* the flight's velocity (cm/s) */
    sv_vec3 asked;           /* the flight's last asked step */
    sv_vec3 root_prev;
    int have_prev, stuck, blocked;
    float ground_y;          /* root0's height: the flight lands at or under it */
    uint32_t grabs, throws, letgos, lands, blocks;
} somvr_ov;

typedef struct {
    int events;              /* SOMVR_OVE_* */
    int why;                 /* LETGO / LAND: SOMVR_OVW_* */
    sv_vec3 move;            /* the body's move for the next tick (world cm); 0 = none */
    int grav_off;            /* the proxy's gravity held off (a hold or a flight) */
    int hand;                /* GRAB / JOIN: which */
    sv_vec3 v0;              /* THROW: the body's velocity */
    float land_cm, need_cm;  /* THROW / SHORT: the arc's landing along `over` from the root, and what it needed */
    float throw_up, throw_over, throw_side; /* the hand's throw split (before the rule's cuts), cm/s */
} somvr_ov_out;

void somvr_ov_reset(somvr_ov *s);
void somvr_ov_step(somvr_ov *s, const somvr_ov_cfg *c, const somvr_ov_in *in, somvr_ov_out *o);
/* the palm at pw on his crown (the head point hd) by the rule; 1 = yes */
int somvr_ov_on_crown(const somvr_ov_cfg *c, sv_vec3 pw, sv_vec3 hd);
/* the arc from height h (cm over the ground) at velocity up/over: how far along `over` it lands */
float somvr_ov_land_cm(float up, float over, float h, float g);
#endif
