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
    /* build 37 (docs/re-xr-pipeline.md 171, SOMVR_FINISHER_OUT=hand): the way out follows the hand - the blade back along
     * the wound's axis by out_gain x the hand's pull past out_break_cm (a little resistance: the first cm and a tenth) */
    float out_gain, out_break_cm;
    int back_free; /* build 37: on the way in a pull back draws the blade back with the hand (1:1), never stuck (0 = build
                    * 33: a pull back on the way in did nothing) */
    int follow;    /* build 37 (the 22:09 session, mark 221801_7): the way in follows the hand 1:1 along the line - the
                    * blade's rate = the hand's speed along the way (up to v_max) x the line's accuracy, nothing under
                    * min_v (a resting hand's drift); 0 = build 33's speed match (1 m/s top, a 3 m/s thrust penalised) */
    float min_v;   /* (follow) the least speed along the way that moves the blade, either way (cm/s) */
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

/* build 37 (re-xr-pipeline.md 171): the way out by the hand. retreat_cm = the hand's motion back along the wound's axis
 * since the deepest point (cm; + = out, - = further in): the blade's place = the deepest less out_gain x (retreat -
 * out_break_cm), never deeper than the deepest nor past the start; returns p (1..2) */
float somvr_fn_out_p(const somvr_fn_cfg *c, float retreat_cm);
/* the same from a turn short of the deepest: s_deep = the share of the line where the hand turned back (1 = the
 * deepest; the turn counts once the point is FN_TURN_CM (6 cm) in - SOMVR_FINISHER_FEEL=b37's kill there) */
float somvr_fn_out_p2(const somvr_fn_cfg *c, float retreat_cm, float s_deep);
/* the snap's pull and the release's blend: 0..1 eased (smoothstep), s the share of the time gone */
float somvr_fn_ease(float s);

/* build 37: why the execution's ready finds no orc to finish (the hint, SOMVR_FINISHER_HINT): the nearest candidate's
 * distance from Talion (cm) and his reach (cm), the cosine of its bearing off the head's forward and the least it may
 * be, how far its facing is off Talion (deg) and the most it may be, whether the outline's grip is in an arm's reach.
 * The first that fails, in the order the player fixes them: too far, its back to Talion, not looked at, out of the
 * arm's reach (SOMVR_FN_WHY_OK = it qualifies) */
enum { SOMVR_FN_WHY_OK = 0, SOMVR_FN_WHY_FAR, SOMVR_FN_WHY_BACK, SOMVR_FN_WHY_VIEW, SOMVR_FN_WHY_REACH, SOMVR_FN_WHY_NONE,
       SOMVR_FN_WHY_NEAR, SOMVR_FN_WHY_N };
int somvr_fn_why(float d_cm, float reach_cm, float view_cos, float view_cos_min, float face_deg, float face_max_deg,
                 int reachable);
const char *somvr_fn_why_text(int why); /* the reason in words ("get within reach" ...) */

/* build 37 (re-xr-pipeline.md 171.6; the user's 22:37 marks: the hand starts within a foot of the body, the stab the
 * arm's full extension out and all the way back): where the stab starts. In the head's yaw frame (cm: x right, y up,
 * z ahead of the eyes): the sword-side shoulder `body`, the orc's neck. The grip goes within grip_r of the body (never a
 * reach across), the blade (blade cm from the grip to the point) along the line to the neck with its point s short of
 * it (s_min..s_max); the hand's travel s + depth from there must end within reach of the body (the arm's extension).
 * Of the starts that do, the one nearest s_pref with the fullest extension; the grip not behind ahead_min nor over
 * up_max or under up_min. 1 = placed: g (the grip), dir (unit, the way in), *s (the point's gap), *ext (the hand's end
 * from the body); 0 = none: *why SOMVR_FN_WHY_REACH (the neck too far: closer) or SOMVR_FN_WHY_NEAR (too near) */
typedef struct {
    float body[3];
    float grip_r, blade, depth, s_min, s_max, s_pref, reach, ahead_min, up_max, up_min;
} somvr_fn_place_cfg;
void somvr_fn_place_defaults(somvr_fn_place_cfg *c);
int somvr_fn_place(const somvr_fn_place_cfg *c, const float neck[3], float g[3], float dir[3], float *s, float *ext,
                   int *why);

#endif
