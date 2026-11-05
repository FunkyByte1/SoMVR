/* somvr_struggle.h - build 41 (som-b41-struggle, docs/re-xr-pipeline.md 180; GAME-STUDY item 1.4): one VR rule for every
 * struggle (the game's QTEs). Pure: no globals, no game reads (somvr_struggle_game.c feeds it). Tested by
 * tests/test_struggle.c.
 *
 * The game's five struggle classes and what each one counts (static, both builds; docs 180.1):
 *   CStrugglePressCount     presses of its command (+0x2f0) up to +0x2f4 (made +0x2f5)          -> repeated thrusts
 *   CStruggleQuickTime      one press of its command (+0xd0) while its window is open (+0xd4)    -> one thrust
 *   CStruggleHold           its command (+0xfc) held for +0x100 s inside its window              -> a hand held closed
 *   CStruggleInputDirection MenuAxisX/Y (or the menu's digital four) pushed from rest within +0xac
 *                           rad of +0xa8 (0 right, pi/2 up, pi left, 3 pi/2 down); a wrong way fails
 *                           it when +0x204                                                       -> a swipe or a lean
 *   CStruggleAim            its command (+0x58c) with the HUD cursor inside its ring; the ring
 *                           sits on a body node named by the struggle's UI data                 -> the blade laid on the
 *                                                                                                  outline there, a thrust
 * Only the struggle's own input is ever sent (its command, its axes), or its own completion flag (+0xa5) for the aim -
 * never a fake button sequence, never a press the struggle would count as wrong. */
#ifndef SOMVR_STRUGGLE_H
#define SOMVR_STRUGGLE_H

#include <stdint.h>

enum { SOMVR_SG_NONE = 0, SOMVR_SG_PRESS, SOMVR_SG_QT, SOMVR_SG_HOLD, SOMVR_SG_DIR, SOMVR_SG_AIM, SOMVR_SG_OTHER,
       SOMVR_SG_N };

/* the act the player answers a struggle with (somvr_sg_act) */
enum { SOMVR_SGA_NONE = 0, /* not ours (a class not known) */
       SOMVR_SGA_THRUSTS,  /* PressCount: each hard thrust or pull of either hand one press (a tick as the game counts it) */
       SOMVR_SGA_THRUST,   /* QuickTime: one hard thrust while its window is open */
       SOMVR_SGA_HOLD,     /* Hold: a hand closed (fresh, after the struggle began) and kept closed */
       SOMVR_SGA_SWIPE,    /* InputDirection: a swipe of either hand or a lean of the head the shown way */
       SOMVR_SGA_STAB,     /* Aim with its node known: the blade laid on the outline at the node, then a thrust */
       SOMVR_SGA_STEER,    /* Aim, no node: build 27's hand-steered cursor, a thrust inside the ring presses */
       SOMVR_SGA_GRIP,     /* a beast's domination (SOMVR_RIDE's): the hand that took it kept closed */
       SOMVR_SGA_N };
int somvr_sg_act(int kind, int beast_domination, int anchored);
const char *somvr_sg_act_name(int act); /* "repeated thrusts" ... (logs) */

/* ---- InputDirection ---- */
/* the game's own test (CStruggleInputDirection's update, L 0xe14990 / W 0x1405acbd0): the input's angle a = acos(x/|v|),
 * negative when y < 0, from |v| >= 0.5; d = |target - a|, less 2 pi once if over it; won when d < tol */
float somvr_sg_dir_angle(float x, float y, int *ok);
int somvr_sg_dir_game(float target, float tol, float a);
/* the axes to send for the target (unit, x right, y up): the target's own direction - or, where the game's fold misses
 * its exact angle (down, 3 pi/2: |3 pi/2 - (-pi/2)| = 2 pi is not over 2 pi), nudged by up to a twentieth of a radian
 * inside the tolerance. 1 = the game takes it; 0 = no vector inside (tolerance 0: the record has none) */
int somvr_sg_dir_send(float target, float tol, float v[2]);
/* a gesture's way (unit or not, x right, y up) within cone_deg of the target angle; *off_deg its angle off */
int somvr_sg_dir_match(const float g[2], float target, float cone_deg, float *off_deg);
const char *somvr_sg_dir_word(float target); /* "right" "up" "left" "down" (the nearest), for logs */

/* the swipe: one hand's place in the head's frame (cm right, cm up) at t (s). A swipe = the newest sample against the
 * one 80..250 ms older: a speed in that plane over min_mps and a way over min_cm; then re-armed when the hand slows under
 * a third of min_mps. 1 = a swipe now: dir (unit), *mps */
typedef struct {
    float min_mps, min_cm;
} somvr_sg_swipe_cfg;
#define SOMVR_SG_HIST 16
typedef struct {
    float t[SOMVR_SG_HIST], x[SOMVR_SG_HIST], y[SOMVR_SG_HIST];
    int n, pos, armed;
} somvr_sg_swipe;
void somvr_sg_swipe_defaults(somvr_sg_swipe_cfg *c);
void somvr_sg_swipe_init(somvr_sg_swipe *s);
int somvr_sg_swipe_step(somvr_sg_swipe *s, const somvr_sg_swipe_cfg *c, float t, float x, float y, float dir[2],
                        float *mps);
/* the lean: the head's offset from where it was at the struggle's start (cm right, cm up); 1 = past lean_cm: dir (unit) */
int somvr_sg_lean(float dx, float dy, float lean_cm, float dir[2]);

/* ---- Hold ---- */
/* the hold's hands: a hand closed when the struggle began must open before it counts (the sword hand, the hand that
 * still holds a beast): only a fresh close holds. 1 = the struggle's command on this tick */
typedef struct {
    int armed[2], on, hand;
    float held; /* s held so far (counts only while the game's window is open: in_window) */
} somvr_sg_hold;
void somvr_sg_hold_init(somvr_sg_hold *h, const int closed_at_start[2]);
int somvr_sg_hold_step(somvr_sg_hold *h, const int closed[2], int in_window, float dt);

/* the objective layer's timer ring (SOMVR_OB_TIMER_STEPS steps; steps + 1 = the full bright ring): the steps left */
unsigned somvr_sg_steps_hold(float held_s, float need_s, int steps);  /* need reached: steps + 1 */
unsigned somvr_sg_steps_count(int made, int need, int steps);          /* need reached: steps + 1 */

/* ---- Aim ---- */
/* the stab on the outline: the blade's point within align_cm of the outline's point and its axis within align_deg of
 * the outline's way = laid (for laid_s after it last was); laid and the point moving along the way at min_mps or more =
 * the stab. Returns SOMVR_SGS_* */
typedef struct {
    float align_cm, align_deg, min_mps, laid_s;
} somvr_sg_stab_cfg;
typedef struct {
    float laid_until;
    int laid, n_laid;
} somvr_sg_stab;
enum { SOMVR_SGS_NONE = 0, SOMVR_SGS_LAID = 1, SOMVR_SGS_STILL = 2, SOMVR_SGS_STAB = 3 };
void somvr_sg_stab_defaults(somvr_sg_stab_cfg *c);
void somvr_sg_stab_init(somvr_sg_stab *s);
int somvr_sg_stab_step(somvr_sg_stab *s, const somvr_sg_stab_cfg *c, float t, const float tip[3], const float axis[3],
                       const float vel[3], const float otip[3], const float odir[3], float *d_cm, float *ang_deg,
                       float *along_mps);

#endif
