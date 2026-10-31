/* somvr_abil.h - build 27 (som-b27-abilities; docs/re-xr-pipeline.md 114): the abilities Talion gains, by the VR
 * player's hands - the pure parts (no game memory): the wraith hand's push (the wraith flash) and the sword's stab
 * down into a downed orc (the ground finisher). somvr_abil_game.c asks the game whether it offers the ability for an
 * orc and starts the game's own attack; these judge the hands. */
#ifndef SOMVR_ABIL_H
#define SOMVR_ABIL_H

typedef struct {
    /* the push (the wraith flash): the right trigger held (the wraith charge, as for the execution), the open left
     * hand thrust out at an orc */
    float push_cms;     /* the palm's speed (the body's frame, the newest 80-160 ms) at least this (cm/s) */
    float push_out_deg; /* its way within this of the eyes -> palm line (out from the body, not a pull or a sweep) */
    float push_aim_deg; /* its way (level) within this of the line from the palm to the orc */
    float push_out_cm;  /* the palm at least this far from the eyes at the push (the arm going out) */
    float open_max;     /* the grip under this = the hand open (a fist is the grab's) */
    float trig_on, trig_off; /* the right trigger's hysteresis (the wraith charge) */
    /* the stab (the ground finisher): the sword hand's way down, point first, the tip at the downed orc's body */
    float stab_cms;     /* the hand at least this fast (cm/s) */
    float stab_down_cms; /* ... this much of it down */
    float stab_point_deg; /* the blade within this of the hand's way (point first) */
    float stab_vert_deg;  /* the blade within this of straight down */
    float stab_reach_cm;  /* the tip within this of the body's point (70: a lying orc's torso and thighs about his pelvis;
                           * run 20261030-124054's second stab at the body 63 cm off it was refused at 55) */
} somvr_ab_cfg;

void somvr_ab_cfg_default(somvr_ab_cfg *c);

/* the push's measures (for the lines); why "" = a push */
typedef struct {
    float speed, out_deg, aim_deg, out_cm;
    const char *why;
} somvr_ab_push;

/* v: the palm's velocity (cm/s, world axes, the body's motion taken out); eye, palm, orc: world cm (orc: its chest or
 * root - only the level direction is used); squeeze 0..1 (NaN = open); charge: the right trigger held. 1 = the push. */
int somvr_ab_push_judge(const somvr_ab_cfg *c, const float v[3], const float eye[3], const float palm[3],
                        const float orc[3], float squeeze, int charge, somvr_ab_push *o);

/* the stab's measures; why "" = a stab into him */
typedef struct {
    float speed, down, point_deg, vert_deg, reach_cm;
    const char *why;
} somvr_ab_stab;

/* v: the sword hand's velocity (cm/s, world axes, the body's motion taken out); axis: the blade base -> tip; tip: the
 * tip (world cm); body: the downed orc's body point (world cm). 1 = a stab into him. */
int somvr_ab_stab_judge(const somvr_ab_cfg *c, const float v[3], const float axis[3], const float tip[3],
                        const float body[3], somvr_ab_stab *o);

/* the right trigger's state with hysteresis (was: the state so far) */
int somvr_ab_trigger(const somvr_ab_cfg *c, int was, float v);

/* ---- build 27c (som-b27-abilities2; docs/re-xr-pipeline.md 118): the dagger's quick throw by a real throw, the
 * whistle by the hand at the mouth ---- */
typedef struct {
    float throw_cms;       /* the hand at least this fast in the throw (cm/s, the body's frame; the newest 80-160 ms) */
    float throw_fwd_deg;   /* its level way within this of the head's level forward (thrown ahead, not across) */
    float throw_up_deg;    /* its way at most this far up ... */
    float throw_down_deg;  /* ... and at most this far down */
    float throw_low_cm;    /* overhand: the hand at most this far under the eyes at the throw (the shoulders' line; an
                            * underhand toss lets go lower: headless boot 20261030-145019's at 28 cm, +29 deg up) */
    float throw_win_s;     /* the grip opening counts this long after the fast move (the release follows the peak) */
    float aim_level_deg;   /* an orc within this (level) of the throw's line ... */
    float aim_pitch_deg;   /* ... and this up or down of it (a thrown dagger arcs: the hand's way is above the line) */
    float aim_min_cm, aim_max_cm; /* the orc this far from the hand */
    float mouth_cm;        /* the whistle: the grip within this of the mouth ... */
    float mouth_s;         /* ... held this long */
    float mouth_out_cm;    /* the hand away from the mouth past mouth_cm + this before the next whistle */
} somvr_ab2_cfg;
void somvr_ab2_cfg_default(somvr_ab2_cfg *c);

typedef struct {
    float speed, fwd_deg, pitch_deg, low_cm;
    const char *why;
} somvr_ab_throw;

/* v: the hand's velocity in the throw (cm/s, world axes, the body's motion taken out); eye: the eyes; fwd: the head's
 * forward; hand: the grip at the throw (world cm). 1 = an overhand throw ahead. */
int somvr_ab_throw_judge(const somvr_ab2_cfg *c, const float v[3], const float eye[3], const float fwd[3],
                         const float hand[3], somvr_ab_throw *o);

/* an orc's point against the throw's line from the hand: level deg off (+ = right of the line), pitch deg (+ = the line
 * above him), distance cm; 1 = inside the cone and the range */
int somvr_ab_throw_aim(const somvr_ab2_cfg *c, const float hand[3], const float v[3], const float orc[3], float *level_deg,
                       float *pitch_deg, float *dist_cm);

/* the mouth: 9 cm under the eyes and 7 cm ahead of them (the head's level forward); the grip's distance to it (cm) */
float somvr_ab_mouth_dist(const float eye[3], const float fwd[3], const float hand[3]);

#endif
