/* somvr_cues.h - build 37 (som-b37-cues, docs/re-xr-pipeline.md 169): what the hidden flat prompts told the player, the
 * VR way - the pure part (no game, no OpenXR; tested by tests/test_cues.c). The game side is somvr_cues_game.c.
 *
 *   1. The awareness mark over an orc: its level from the game's own AI alert state (the stealth judge's read: the NPC's
 *      state +0x10 -> +0x7c, 0 calm .. 4) and whether it fights Talion, the mark's fade, red's auto-hide (as the flat
 *      game's AutoHideRecognizedIcon*), its size in the room.
 *   2. The mission timer's popups: when its line pops at the gaze (the start, 30 s left, 10 s left to the end).
 *   3. The use cue: which nav-marker records stand for a pick prompt's thing (the herb's pattern for every use). */
#ifndef SOMVR_CUES_H
#define SOMVR_CUES_H

#include <stdint.h>

/* ---- 1. the awareness mark ------------------------------------------------------------------------------------------ */
enum { SOMVR_CU_NONE = 0, SOMVR_CU_WHITE = 1, SOMVR_CU_AMBER = 2, SOMVR_CU_RED = 3 };
#define SOMVR_CU_KINDS 8 /* the alert states mapped (0..7) */
typedef struct {
    int8_t lvl[SOMVR_CU_KINDS]; /* the mark's level for each alert state (SOMVR_CUE_AWARE_KINDS) */
    float in_s, out_s;          /* the fade in / out (s) */
    float red_s;                /* red shows this long, then fades (0 = stays): the flat game auto-hides its red icon */
    float deg, min_m, max_m;    /* the mark's size: deg of the view at its distance, within min..max metres */
} somvr_cu_aw_cfg;
/* the defaults: 0 calm none, 1 white (it noticed something), 2 amber (it looks: 'ReactToSusp'), 3 and 4 red (it has seen
 * Talion: comes at him, fights him), 5..7 none; in 0.25 s, out 0.5 s, red 3 s; 1.6 deg, 0.12..0.6 m */
void somvr_cu_aw_defaults(somvr_cu_aw_cfg *c);
/* "N,N,N,N,N[,N..]" (the level for alert state 0, 1, ... each 0..3) -> c->lvl; 0 = bad (c unchanged) */
int somvr_cu_aw_parse_kinds(const char *v, somvr_cu_aw_cfg *c);
/* an orc's level now: dead or unread (state < 0) none; fighting Talion red; else the state's */
int somvr_cu_aw_level(const somvr_cu_aw_cfg *c, int state, int engaged, int dead);
/* one orc's mark over time */
typedef struct {
    int lvl;        /* the level now (0..3) */
    int shown;      /* the level the mark shows (it keeps its colour while it fades out) */
    float fade;     /* 0..1 */
    double since;   /* when lvl began */
    double red_at;  /* when it last rose to red (0 = never) */
} somvr_cu_aw_mark;
/* the step: the level read now (and whether the orc is in view); returns 1 when the level changed */
int somvr_cu_aw_step(const somvr_cu_aw_cfg *c, somvr_cu_aw_mark *m, int lvl, int visible, double t, float dt);
/* the mark's side (m) at dist_m from the eyes */
float somvr_cu_aw_size(const somvr_cu_aw_cfg *c, float dist_m);

/* ---- 2. the mission timer's popups ---------------------------------------------------------------------------------- */
typedef struct {
    float start_s;  /* the line pops this long after a timer starts (4) */
    float mark_s;   /* ... this long at each mark (4) */
    float at[2];    /* the marks: 30 and 10 s left; under the last it stays up to the end */
} somvr_cu_tm_cfg;
void somvr_cu_tm_defaults(somvr_cu_tm_cfg *c);
enum { SOMVR_CU_TM_NONE = 0, SOMVR_CU_TM_START = 1, SOMVR_CU_TM_MARK = 2, SOMVR_CU_TM_LAST = 3, SOMVR_CU_TM_STOP = 4 };
typedef struct {
    int on;          /* a timer runs */
    float left;      /* its seconds left (the game's) */
    double seen_t;   /* the last tick */
    double pop_until;/* the popup up until (0 = not) */
    int last;        /* under the last mark: up to the end */
    int marks;       /* the marks passed (bits) */
} somvr_cu_tm;
/* a tick of the game's timer (left s at t); returns an event (SOMVR_CU_TM_*: what began now) */
int somvr_cu_tm_tick(const somvr_cu_tm_cfg *c, somvr_cu_tm *s, float left, double t);
/* every frame: no tick for stale_s = the timer is over (SOMVR_CU_TM_STOP once); *pop = the popup is up now */
int somvr_cu_tm_frame(somvr_cu_tm *s, double t, double stale_s, int *pop);

/* ---- 2b. the reach cue (build 37, the user's herb 11-04: "I didn't know how I triggered it") ------------------------ */
/* the glow's level as the nearest free, open hand comes in: 0.3 at reach + 80 cm and beyond, rising to 0.8 at the reach,
 * 1 in reach; the reach ring's cell (1 wide .. 16 tight at the reach) */
float somvr_cu_reach_level(float d_cm, float reach_cm, int in);
int somvr_cu_ring_cell(float d_cm, float reach_cm, int in);

/* ---- 2c. the reach to a thing on the ground (build 40, som-b40-herb, docs/re-xr-pipeline.md 176; the 11-05 session's
 * mark 7: the herb taken by B, never by the hand). The two grips were 8 and 9 cm beside the herb's stem,
 * 79 and 84 cm over its foot, 72 cm under the eyes: the hand reached down over it, not into the grass - and the rule was
 * a 40 cm ball round the plant's top (15 cm over the foot). Now a hand reached down over a thing on the ground counts:
 * within over_cm (25) of its stem, at most up_cm (110) over its foot and at least low_cm (45) under the eyes - the
 * reach of a player who bends a little, not to the floor (HL:A never asks for the floor either). The ball stays. */
typedef struct {
    float reach_cm;  /* the ball round the top (40) */
    float over_cm;   /* the column round the stem (25) */
    float up_cm;     /* ... its height over the foot (110) */
    float low_cm;    /* ... the hand at least this far under the eyes (45) */
    float top_cm;    /* the top over the foot (15) */
} somvr_cu_take_cfg;
void somvr_cu_take_defaults(somvr_cu_take_cfg *c);
/* the hand's distance for the cue and the take (cm): the ball's, or within the column the stem's (horizontal) one
 * scaled so the column's edge = the ball's reach; *in = in reach. palm, foot: world cm (Y up); eye_y: the eyes' height */
float somvr_cu_take_dist(const somvr_cu_take_cfg *c, const float palm[3], const float foot[3], float eye_y, int *in);

/* ---- 3. the use cue's things ---------------------------------------------------------------------------------------- */
/* a pick prompt's string id ('Combat_Activate_*') -> the nav-marker record patterns of its things ("A*,B" as
 * somvr_ob_match; "" = none known: the cue sits where the hand reaches near Talion) */
const char *somvr_cu_use_recs(const char *prompt);
/* a record name against such a pattern list (',' between, a trailing '*' = any rest) */
int somvr_cu_match(const char *pats, const char *name);

#endif
