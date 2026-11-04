/* somvr_finisher.h - SOMVR_FINISHER=on|off: the finisher as the player's own guided motion (build 33, docs/re-xr-pipeline.md
 * 140), first the single neck stab. Pure: no globals, no game reads (somvr_finisher_game.c feeds it). Tested by
 * tests/test_finisher.c.
 *
 * When the execution is ready and an orc stands in front, the world goes slow and a blue outline of the sword shows the
 * finisher's start: its point short of the orc's neck, pointing into it. The drawn sword lined up with it (generous: the
 * point within align_cm, the blade within align_deg) snaps onto it and from then on rides the finisher's path - into the
 * neck and back out - at a speed the player's hand earns: v_max x how close the hand's speed along the path's way is to
 * v_ref (its own share below it, full to 1.6 v_ref, 1.6 v_ref over it past that, no less than 0.4) x how well its
 * motion follows the way (the cosine,
 * from 1 at the way to 0 at acc_min). Progress p runs
 * 0..1 in, 1..2 out; the blade's point is at start + dir x (start_cm + depth_cm) x s(p), s = p in, 2 - p out. */
#ifndef SOMVR_FINISHER_H
#define SOMVR_FINISHER_H

typedef struct {
    float align_cm, align_deg;  /* the snap: the point within this of the outline's, the blade within this angle */
    float start_cm, depth_cm;   /* the outline's point this far short of the neck; the stab this far into it */
    float v_max, v_ref;         /* the guided blade's top speed (cm/s); the hand's speed along the way for it (cm/s) */
    float acc_min;              /* the motion's cosine to the way under which it earns nothing */
} somvr_fn_cfg;
void somvr_fn_defaults(somvr_fn_cfg *c);

/* 1 = the drawn blade lines up with the outline: its point (tip) within align_cm of the outline's, its axis within
 * align_deg of the outline's direction (both unit or any length: normalised) */
int somvr_fn_aligned(const somvr_fn_cfg *c, const float tip[3], const float axis[3], const float otip[3],
                     const float odir[3], float *d_cm, float *ang_deg);

/* the step: the hand's velocity (cm/s), the path's way (unit; into the orc), p (0..2), dt (s); returns the new p and
 * *rate (cm/s of the guided blade) and *match (0..1: speed x accuracy). The way is dir while p < 1, -dir after */
float somvr_fn_step(const somvr_fn_cfg *c, const float hand_v[3], const float dir[3], float p, float dt, float *rate,
                    float *match);

/* the guided blade's point at progress p: start + dir x (start_cm + depth_cm) x s(p) */
void somvr_fn_point(const somvr_fn_cfg *c, const float start[3], const float dir[3], float p, float out[3]);
/* the depth past the neck's surface at p (cm; < 0 = short of it) */
float somvr_fn_depth(const somvr_fn_cfg *c, float p);

#endif
