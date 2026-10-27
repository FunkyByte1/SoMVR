/* somvr_gaze.h - SOMVR_STRIKE_GAZE (build 17; docs/re-xr-pipeline.md 63, handoff/HANDOFF-wraith.md "build 17"): the
 * Shadow Strike by gaze. In the wraith vision (Y held) the orc the head looks at is marked; A = the game's own Shadow
 * Strike at it (the build-12 wraith dash carries the view); looking away or letting go of Y before A = nothing.
 * Pure: no globals, no game reads (somvr_strike_game.c feeds it). Tested by tests/test_gaze.c.
 *
 * Units: world cm, seconds, degrees. The head's ray: its position and forward (unit) in the game's world.
 *
 * 1. The pick (somvr_gz_pick): each frame, the orcs near Talion (their root = the pelvis, the body a segment from
 *    root - seg_lo to root + seg_hi, radius_cm wide) - an orc's angle is the angle between the head's ray and the
 *    body's nearest point, less its half-width's angle (looking at its shoulder counts). Candidates: in range
 *    (near_cm .. range_cm), in sight (los: the game side's ray; -1 = unknown counts as seen), within cone_deg.
 *    - nothing marked: the best (smallest angle) candidate marked once it has been the best for dwell_s (a sweep
 *      across a crowd marks nothing until the head rests);
 *    - marked: kept while within keep_deg (> cone_deg: the hysteresis), in range (+ range_keep_cm) and in sight; out
 *      of those for lose_s = unmarked (a blink of a rock or a glance away keeps it). Another candidate takes the
 *      mark only when it is better by switch_deg and has been the best for dwell_s while the marked one is out of
 *      cone_deg (a glance at a neighbour does not move the mark);
 *    - off (Y let go, the vision gone, a menu, climbing, the bow out) = unmarked at once (the cancel).
 * 2. The press (somvr_gz_press, somvr_gz_tick): A with a mark and 2+ arrows (or the count unknown) = committed: the
 *    mark is locked to that orc (the head may move), the game's aim command is held and its free-aim ray pointed at
 *    the orc (the game side) until the game's Shadow Strike prompt is up, then the strike command for press_s; the
 *    aim is held hold_s more (the strike starts from the aim mode). No prompt within aim_max_s = refused (the denied
 *    buzz; nothing moves). A without a mark = nothing; with a mark and under 2 arrows = refused at once.
 */
#ifndef SOMVR_GAZE_H
#define SOMVR_GAZE_H

#include <stdint.h>

typedef struct {
    float cone_deg;       /* a new mark within this of the head's ray (6) */
    float keep_deg;       /* the mark kept within this (12) */
    float switch_deg;     /* another orc takes the mark only this much nearer the ray (3) */
    float dwell_s;        /* the best this long before it is marked (0.12) */
    float lose_s;         /* the marked one out of keep / sight / range this long = unmarked (0.25) */
    float range_cm;       /* farther = no new mark (the game's strike range, measured: 2500) */
    float range_keep_cm;  /* kept to range_cm + this (200) */
    float near_cm;        /* nearer = no new mark (150) */
    float radius_cm;      /* an orc's half-width (35) */
    float seg_lo_cm, seg_hi_cm; /* its body below / above the root (70, 80) */
    float chest_cm;       /* the aim point (and the mark's centre) above the root (40) */
    /* the press */
    float aim_max_s;      /* the prompt must come within this of the press (0.8) */
    float press_s;        /* the strike command held this long (0.15) */
    float hold_s;         /* the aim held this long after the strike command began (0.45) */
    int arrows_min;       /* the strike's cost (2: the game takes two Elf-shot arrows) */
} somvr_gz_cfg;
void somvr_gz_cfg_default(somvr_gz_cfg *c);

typedef struct {
    uint64_t id;          /* the orc (its object; 0 = none) */
    float root[3];        /* its root (the pelvis), world cm */
    int los;              /* 1 in sight, 0 blocked, -1 unknown (counts as seen) */
} somvr_gz_orc;

/* the orc's angle off the head's ray (deg, its half-width taken off; 0 = the ray meets its body) and its distance
 * (cm, the head to its aim point); behind the head = 180 */
float somvr_gz_angle(const somvr_gz_cfg *c, const float head[3], const float fwd[3], const float root[3], float *dist_cm);

enum { SOMVR_GZP_OFF = 0, SOMVR_GZP_AIM = 1, SOMVR_GZP_SENT = 2 }; /* the press's phase */
enum {                          /* events (out.ev bits) */
    SOMVR_GZE_MARK = 1u,        /* an orc marked (none before) */
    SOMVR_GZE_SWITCH = 2u,      /* the mark moved to another orc */
    SOMVR_GZE_UNMARK = 4u,      /* the mark gone (out.why) */
    SOMVR_GZE_PRESS = 8u,       /* A with a mark and the arrows: committed (the aim begins) */
    SOMVR_GZE_NOMARK = 16u,     /* A without a mark: nothing */
    SOMVR_GZE_NOARROWS = 32u,   /* A with a mark, under arrows_min: refused (the buzz) */
    SOMVR_GZE_SEND = 64u,       /* the prompt up: the strike command begins */
    SOMVR_GZE_REFUSED = 128u,   /* no prompt within aim_max_s: refused (the buzz) */
    SOMVR_GZE_DONE = 256u       /* the press's aim and command over */
};
enum { SOMVR_GZW_NONE = 0, SOMVR_GZW_OFF, SOMVR_GZW_AWAY, SOMVR_GZW_RANGE, SOMVR_GZW_SIGHT, SOMVR_GZW_GONE, SOMVR_GZW_STRUCK };
const char *somvr_gz_why_name(int why);

typedef struct {
    /* the pick */
    uint64_t mark;        /* the marked orc, 0 = none */
    uint64_t cand;        /* the best candidate other than the mark, and since when */
    double cand_t;
    double out_t;         /* the marked one out of keep / sight / range since (0 = in) */
    int out_why;
    float aim[3];         /* the marked orc's aim point (root + chest), world cm */
    float deg, dist_cm;   /* its angle and distance this frame */
    double mark_t;        /* marked since */
    /* the press */
    int phase;            /* SOMVR_GZP_* */
    double press_t, send_t;
    uint64_t target;      /* the press's orc (locked) */
    float target_aim[3];  /* its aim point (updated while it is seen) */
    /* counters */
    uint64_t marks, switches, unmarks, presses, nomarks, noarrows, sends, refused, frames_marked;
} somvr_gz;
void somvr_gz_reset(somvr_gz *g);

typedef struct {
    uint32_t ev;          /* SOMVR_GZE_* this step */
    int why;              /* SOMVR_GZE_UNMARK's reason */
    uint64_t mark;        /* the mark now (the locked target while pressed) */
    float aim[3];         /* its aim point */
    float deg, dist_cm;
    int aim_cmd, strike_cmd; /* the tick: hold the aim command / the strike command now */
} somvr_gz_out;

/* one frame's pick at t: on = Y held in the world with the vision on, on foot, nothing else owning the hands; while
 * a press runs the mark stays on its target (updated from orcs when it is there; gone from them = kept as last seen) */
void somvr_gz_pick(somvr_gz *g, const somvr_gz_cfg *c, double t, int on, const float head[3], const float fwd[3],
                   const somvr_gz_orc *orcs, int n, somvr_gz_out *o);
/* A's press at t with arrows (-1 = unknown): SOMVR_GZE_PRESS | NOMARK | NOARROWS (a press while one runs: 0) */
uint32_t somvr_gz_press(somvr_gz *g, const somvr_gz_cfg *c, double t, int arrows);
/* one input tick at t: prompt = the game's Shadow Strike prompt up now; struck = the strike's move began (the dash's
 * hold or start): the aim's end. Sets o->aim_cmd / o->strike_cmd, SEND / REFUSED / DONE */
void somvr_gz_tick(somvr_gz *g, const somvr_gz_cfg *c, double t, int prompt, int struck, somvr_gz_out *o);

#endif
