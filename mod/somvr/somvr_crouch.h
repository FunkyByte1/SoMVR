/* somvr_crouch.h - crouch by ducking (SOMVR_CROUCH=duck; the HL:A gap HG-11: docs/design-hla.md 2.14, design-vr.md
 * "Crouch, stealth, seated play", re-xr-pipeline.md 41). The player ducks for real: the head held depth_cm below the
 * standing height for on_s = Talion crouches (the game's stealth crouch); back above standing - up_cm for off_s = he
 * stands (the gap between the two is the hysteresis; a crouch lasts at least hold_s, so nothing flickers). Small bobs,
 * a nod, a look at the floor stay above the line. The standing height is the head's at every recentre (the first
 * stereo frame, the headset's recentre, the trigger word) and follows a taller stance (the head rise_cm over it for
 * rise_s: a recentre made sitting or bent). Seated play keeps the button (the right stick down) and never ducks.
 * The view: the eyes are the headset's (SOMVR_BASE=head, SOMVR_ANCHOR=root), so a duck lowers the view by the real
 * duck and nothing else; Talion's crouch animation moves his body under it, never the view.
 * The game's crouch is a hold (somvr_crouch_hold_step): held while ducked, or toggled by the right stick down.
 * Pure: no globals, no game reads. Tested by tests/test_crouch.c; the game side is somvr_crouch_game.c. */
#ifndef SOMVR_CROUCH_H
#define SOMVR_CROUCH_H

#include <stdint.h>

typedef struct {
    float depth_cm; /* the duck: the head this far below the standing height (28) */
    float up_cm;    /* the stand: the head back above standing - up_cm (18; < depth_cm: the hysteresis) */
    float on_s;     /* held below that long = crouch (0.15 s) */
    float off_s;    /* held above that long = stand (0.2 s) */
    float hold_s;   /* a crouch lasts at least this long (0.5 s) */
    float rise_cm;  /* the standing height follows a taller stance: the head over it by rise_cm (5) ... */
    float rise_s;   /* ... for rise_s (1.0 s) = the new standing height (the lowest head of that time) */
    float jump_cm;  /* the head moved more than this between two samples: a tracking jump, the timers restart (40) */
} somvr_crouch_cfg;
void somvr_crouch_cfg_default(somvr_crouch_cfg *c);

typedef struct {
    int have;          /* a standing height */
    float stand;       /* the standing head height (the tracking space, cm, y up) */
    float y, last_y;   /* the last sample */
    int have_y;
    int ducked;        /* 1: the duck asks for the crouch */
    double t_below;    /* the head below the duck line since (0 = not) */
    double t_above;    /* ducked: the head above the stand line since (0 = not) */
    double t_on;       /* the last duck's start */
    double t_rise;     /* the head over the standing height since (0 = not) */
    float rise_min;    /* the lowest head since t_rise */
    uint64_t ons, offs, rises, stands, jumps;
} somvr_crouch;
void somvr_crouch_init(somvr_crouch *s);
/* the standing height measured: the head's y at a recentre (standing play) */
void somvr_crouch_stand(somvr_crouch *s, float y_cm);
/* One sample at t (s): the head's y (cm, the same space as the standing height); ok = the head is tracked; armed = the
 * duck is on (SOMVR_CROUCH=duck, not seated). Returns the edge: +1 = the duck starts (crouch), -1 = it ends (stand;
 * also once when the duck is disarmed while ducked), 0 = none. */
int somvr_crouch_step(somvr_crouch *s, const somvr_crouch_cfg *c, double t, float y_cm, int ok, int armed);
/* how far the head is below the standing height now (cm; 0 without one) */
float somvr_crouch_depth(const somvr_crouch *s);

/* ---- the hold: what the game's stealth crouch gets ------------------------------------------------------------------ */
/* The game's crouch is a hold (re-xr-pipeline.md 41.1, run 031239): CombatSecondary04 (125, pad RT) held = PC_Stealth,
 * Talion low with the dagger while it is held, up within 0.3 s of the release; the Crouch command (15) does nothing.
 * The duck holds it while ducked; the right stick down (the button: seated play, or anyone) toggles it. One rule set:
 *   - a duck's start: the duck holds (a button crouch before it is dropped into it);
 *   - a duck's end: up (a button crouch too);
 *   - the button while held: up, and the duck stays up until its next start; the button while up: held (latched). */
typedef struct {
    int latched;       /* the button's crouch */
    int duck_off;      /* the button stood a duck up: the duck holds nothing until its next start */
    uint64_t toggles;
} somvr_crouch_hold;
void somvr_crouch_hold_init(somvr_crouch_hold *h);
/* One input tick: edge = the duck's (somvr_crouch_step, 0 none), button = the right stick's pulse this tick (1),
 * ducked = the duck's state. Returns 1 = the stealth command held this tick. */
int somvr_crouch_hold_step(somvr_crouch_hold *h, int edge, int button, int ducked);

#endif
