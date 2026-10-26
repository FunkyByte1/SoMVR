/* somvr_front.h - front-first enemies (the HL:A gap HG-5: docs/design-hla.md 2.10, design-vr.md "Front-first, the VR
 * rule"; the RE and the build 14 design: docs/re-xr-pipeline.md 42). The fight circle (AICombatMgr, FUN_028d7b50)
 * scores every orc that may attack Talion (AICombatMgr_ScoreAttacker: entry+0x20c), sorts them and grants the best
 * ones; the first negative score ends the grants. The VR rule judges each candidate after the game's score:
 *   - in view (inside the view_deg cone around the head) or the game's own rejection: the score stays;
 *   - out of view: vetoed (veto_score, negative) until a warning has run for warn_s (the cue: a positional sound at the
 *     orc, the edge glow on that side, a haptic tick on that side's hand); then it may be granted for ready_s; a
 *     warning that ran out unused leaves the orc quiet for quiet_s (no warning spam);
 *   - the warning starts at the end of a pass (one target's slot, one AI tick) for the best-scored vetoed orc, and only
 *     when no orc in view was accepted in that pass (front first) and fewer than max_warn run;
 *   - except an orc the game sorts first whatever its score (prio: entry+0x23a set and its +0x224 timer run out; the
 *     comparator FUN_028ca050): vetoed, it would end every pass before anyone is granted, so it is warned at once (in
 *     view orcs, max_warn and its quiet time do not hold it back) and the slot waits at most warn_s for it.
 * So an orc behind the player attacks only after at least warn_s of warning, plus the AI's own grant -> start time,
 * plus the attack's 0.8 s to the impact. Time = the fight circle's own clock (ms, *(*(0x463e290)+8)+0x14).
 * Also the two small pure parts of build 14's other switches: the damage scale on hits at Talion and the AI's camera
 * copy from the head (CPlayerObj+0xb94..+0xbd0).
 * Pure: no globals, no game reads. Tested by tests/test_front.c; not hooked into the game yet (build 14). */
#ifndef SOMVR_FRONT_H
#define SOMVR_FRONT_H

#include <stdint.h>

typedef struct {
    float view_deg;   /* the in-view cone, full angle (110, = SOMVR_THREAT_VIEW_DEG) */
    float warn_s;     /* an out-of-view orc may be granted after its warning ran this long (1.0) */
    float ready_s;    /* ... within this long after that (2.0); later it needs a new warning */
    float quiet_s;    /* a warning that ran out unused: no new one for that orc for this long (2.0) */
    float far_cm;     /* out-of-view orcs farther than this are not vetoed (0 = every distance) */
    float veto_score; /* the score a veto writes (-50: negative = never granted; the game's own rejections are -2..-100) */
    int max_warn;     /* warnings running at a time (1) */
} somvr_front_cfg;
void somvr_front_cfg_default(somvr_front_cfg *c);

#define SOMVR_FRONT_ORCS 16
typedef struct {
    uint64_t id;          /* the orc (its HOBJECT); 0 = a free record */
    uint32_t seen_ms;     /* the last pass it was a candidate in */
    uint32_t warn_ms;     /* its warning's start (when warning) */
    uint32_t quiet_until; /* no new warning before this (when quiet) */
    float score;          /* the game's score in the last pass */
    float off_deg, dist_cm;
    uint8_t warning;      /* a warning was started (it may still be running or ready; end_pass expires it) */
    uint8_t quiet;        /* its last warning ran out unused */
    uint8_t want;         /* the open pass: out of view, accepted by the game, vetoed without a warning */
    uint8_t prio;         /* the open pass: it was a prio candidate (sorted first by the game) */
} somvr_front_orc;

typedef struct {
    somvr_front_orc o[SOMVR_FRONT_ORCS];
    int open;          /* a pass is open */
    uintptr_t key;     /* the open pass: the target entry's slot */
    uint32_t pass_ms;  /* its tick */
    int any_in_view;   /* an orc in view was accepted in it */
    uint64_t passes, judged, in_view, far, ready, waits, vetoes, game_rejects, warnings, prio_warnings, expired, used,
        full;
} somvr_front;
void somvr_front_init(somvr_front *f);

enum {
    SOMVR_FR_GAME = 0, /* the game's own rejection (a negative score): unchanged */
    SOMVR_FR_IN_VIEW,  /* in view: unchanged */
    SOMVR_FR_FAR,      /* out of view beyond far_cm: unchanged */
    SOMVR_FR_READY,    /* out of view, warned at least warn_s ago: unchanged (it may be granted now) */
    SOMVR_FR_WAIT,     /* out of view, its warning runs: vetoed */
    SOMVR_FR_VETO,     /* out of view, no warning (yet, or quiet): vetoed */
    SOMVR_FR_FULL,     /* no record free: unchanged (fail open) */
    SOMVR_FR_N
};
const char *somvr_front_verdict_name(int v); /* "game", "in-view", "far", "ready", "wait", "veto", "full" */

/* One candidate as the game scored it (right after AICombatMgr_ScoreAttacker). pass = the slot's address (a pass =
 * one slot in one tick); now_ms = the fight circle's clock; id = the orc; off_deg = its angle off the head's view
 * direction (somvr_th_direction's off_deg; NaN = unknown: in view), dist_cm = the head's distance to it; prio = the
 * game sorts it first (entry+0x23a and *(uint32*)(entry+0x224) < now: FUN_02390cb0's test). A new pass
 * (another key or a later tick) ends the open one first (somvr_front_end_pass; a warning started there is reported by
 * *started, else 0; started may be NULL). *score is rewritten to veto_score for WAIT and VETO. Returns the verdict. */
int somvr_front_judge(somvr_front *f, const somvr_front_cfg *c, uintptr_t pass, uint32_t now_ms, uint64_t id,
                      float off_deg, float dist_cm, int prio, float *score, uint64_t *started);

/* End the open pass (also once a frame from the game thread, so a warning starts within a frame): expire warnings that
 * ran past warn_s + ready_s (the orc goes quiet for quiet_s); then start one warning: for the best-scored vetoed prio
 * orc if there is one; else, when no orc in view was accepted in the pass and fewer than max_warn run, for the
 * best-scored vetoed orc that is not quiet. Returns that orc (0 = none);
 * its record (somvr_front_find) has the direction for the cue. Without an open pass only the expiry runs. */
uint64_t somvr_front_end_pass(somvr_front *f, const somvr_front_cfg *c, uint32_t now_ms);

/* The game granted the orc (FR_GRANT: FUN_028cd4b0, its attack streak of entry+0x22c attacks): a warning it had is
 * used up, so its next grant from out of view needs a new one (no quiet time: it may be warned at the next pass). */
void somvr_front_granted(somvr_front *f, uint64_t id);

const somvr_front_orc *somvr_front_find(const somvr_front *f, uint64_t id);
int somvr_front_warnings(const somvr_front *f, const somvr_front_cfg *c, uint32_t now_ms); /* running now */
/* the warning's progress for the cue: 0 at its start .. 1 at warn_s (it may be granted from then on); -1 = none */
float somvr_front_warn_level(const somvr_front *f, const somvr_front_cfg *c, uint64_t id, uint32_t now_ms);

/* The damage scale (SOMVR_FRONT_DAMAGE, D10): a hit at Talion of amount > 0 and under keep_above is scaled (scale
 * 0..1; NaN or out of range = 1); heals (< 0), 0 and amounts from keep_above on (scripted kills) stay. */
float somvr_front_damage(float amount, float scale, float keep_above);

/* The AI's camera copy from the head (SOMVR_FRONT_CAMCOPY=head): the 0x40 bytes FUN_027aa490 writes at
 * CPlayerObj+0xb94: pos, quat (x y z w), the same pose again at +0xbb0, the FOVs (full angles, radians) at +0xbcc. */
typedef struct {
    float pos[3];  /* +0xb94 */
    float quat[4]; /* +0xba0 (vf_59 returns pos + quat) */
    float pos2[3]; /* +0xbb0 (FUN_029a8130's frustum test reads this copy) */
    float quat2[4];/* +0xbbc */
    float fov[2];  /* +0xbcc, +0xbd0 */
} somvr_front_camcopy;
_Static_assert(sizeof(somvr_front_camcopy) == 0x40, "CPlayerObj+0xb94..+0xbd3");
/* head = the head's world position (cm), q = its orientation (game convention, x y z w; normalised here, a zero or
 * NaN quaternion = identity); fov_h/v in degrees (clamped 20..170). */
void somvr_front_camcopy_fill(somvr_front_camcopy *out, const float head[3], const float q[4], float fov_h_deg,
                              float fov_v_deg);
/* The camera term's input as the game computes it (AICombatMgr_ScoreAttacker 0x28ce506..): the cosine between the
 * copy's forward R(q)(0,0,1) and the direction from its position to p (0 when p is at the position). */
float somvr_front_cam_dot(const somvr_front_camcopy *cc, const float p[3]);

#endif
