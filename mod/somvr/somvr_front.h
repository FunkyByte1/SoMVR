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
    /* build 19 (the crowd's pace): set by the game side while another orc's turn holds for longer than a warning: no
     * new warning starts (a warned orc could not be granted before its ready time ran out); held = the pass ends that
     * would have started one */
    int hold_warn;
    uint64_t held;
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
    /* build 19 (SOMVR_CROWD_PACE_MS, the crowd's pace below): the game side's own verdicts after the judge's (never
     * returned by somvr_front_judge) */
    SOMVR_FR_PACE,     /* accepted, but another orc's turn holds (its wind-up) or the gap after the last blow runs: vetoed */
    SOMVR_FR_PASS,     /* accepted, but another candidate of the same pass was kept (one grant a pass): vetoed */
    SOMVR_FR_N
};
/* "game", "in-view", "far", "ready", "wait", "veto", "full", "pace", "pass" */
const char *somvr_front_verdict_name(int v);

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
 * best-scored vetoed orc that is not quiet; none while hold_warn is set (build 19). Returns that orc (0 = none);
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

/* ---- build 19: the crowd's pace (SOMVR_CROWD_PACE_MS; handoff/HANDOFF-combat-design.md item 5, docs/re-xr-pipeline.md
 * 70) ----
 * One orc winds up at Talion at a time and its blow lands at least gap_s after the last one: from a grant at Talion
 * (FR_GRANT) the turn is that orc's until its attack's impact (THREAT's track: its start + the impact key), or until
 * start_s passed without a keyed start (its keyless moves in between are not blows: the turn holds), or the attack
 * ended early (a hit cancelled it). While a turn holds, every candidate is vetoed (the game's fight circle keeps it circling, its own
 * wait). After an impact a new grant may come from impact + gap_s - lead (lead = the shortest grant -> impact: lead_s,
 * or a shorter one seen; 0.05 s to the start + 0.80 s = 0.85 s headless, so a gap up to it costs nothing). An attack
 * that starts with no grant the pace saw (or while another turn holds: an overlap) is a turn too, and counted.
 * Time = our clock (s; the THREAT tracks' t0 follow the orc's animation timer). Pure. */
typedef struct {
    float gap_s;   /* impacts at Talion at least this far apart (0.7; SOMVR_CROWD_PACE_MS) */
    float lead_s;  /* the shortest grant -> impact assumed (0.85; SOMVR_CROWD_LEAD_MS) */
    float start_s; /* a grant whose blow has not started by then lets the turn go (2.5; FRONT's FF_HOLD_S) */
    float swing_s; /* a pending grant's blow lands at least this long after now (its start -> impact, 0.8) */
    /* build 19 (the attack from behind, re-xr-pipeline.md 70.7): a berserker whose blow hit Talion answers a cut of his
     * in the next ~2.7 s (while he recoils: its headbutt sync or its bash; no grant). Such an orc's turn (marked with
     * somvr_pace_may_answer) does not end at its blow's end (its impact, or cut short by our cut: somvr_pace_end): it
     * waits verdict_s for the blow's verdict (DEFENCE; the game side also holds it while Talion still recoils from its
     * last blow that hit), then holds
     * on for answer_s after a hit (somvr_pace_verdict; the game side lets it go when Talion's recoil ends), so its answer
     * starts inside its own turn, never while another orc winds up. 0 = off (SOMVR_CROWD_ANSWER_MS) */
    float answer_s;
    float verdict_s; /* 0.6 (DEFENCE's verdict of a fast red blow comes after build 18's hold, 0.4 s) */
} somvr_pace_cfg;
void somvr_pace_cfg_default(somvr_pace_cfg *c);

#define SOMVR_PACE_TURNS 8
typedef struct {
    uint64_t id;     /* the orc (0 = free) */
    double grant_t;  /* its grant (0 = an attack the pace saw no grant for) */
    double start_t;  /* its blow's start (0 = not yet) */
    double impact_t; /* its blow's impact (0 = not started yet) */
    int answer;      /* it may answer a cut after its blow hit Talion (a berserker; somvr_pace_may_answer) */
    double hold_t;   /* > 0: an answer hold, no blow of its own yet: the turn stays its until then */
    double hold0_t;  /* the hold's start (its blow's impact) */
    int hit;         /* the hold follows a blow that hit Talion (the verdict; 0 = still waiting for it) */
} somvr_pace_turn;
typedef struct {
    somvr_pace_turn turn[SOMVR_PACE_TURNS]; /* the turns now: granted or winding up, not landed */
    double last_impact_t;  /* the latest blow that landed (0 = none yet) */
    uint64_t last_id;
    float lead_seen;       /* the shortest grant -> impact seen (0 = none yet) */
    double min_gap, max_gap, gap_sum;
    uint64_t grants, starts, unannounced, overlaps, moves, let_go, cancelled, impacts, gaps, short_gaps, full;
    /* the answer holds: waited for a verdict, held after a hit, a blow started in one (its answer), released early
     * (somvr_pace_release: Talion's recoil over), ran out (answer_s or verdict_s), ended by a verdict that was no hit */
    uint64_t holds_wait, holds, answers, released, expired, no_hit;
} somvr_pace;
void somvr_pace_init(somvr_pace *p);

/* the lead in use: lead_s, or the shortest grant -> impact seen if shorter */
float somvr_pace_lead(const somvr_pace *p, const somvr_pace_cfg *c);
/* the turns: the blows whose impact is at or before t land (in impact order; each one's gap to the one before is
 * counted: min, max, under gap_s); grants older than start_s with no start are let go. The landed ones are copied to
 * out (up to cap) with their gap (s, -1 = the first blow) in gaps; returns how many landed. */
int somvr_pace_update(somvr_pace *p, const somvr_pace_cfg *c, double t, somvr_pace_turn *out, double *gaps, int cap);
/* may orc id be granted now (keep its score)? 1 = yes: nothing holds and the gap ran, or the only turn held is its own
 * (a veto must not take back its grant); 0 = another orc's turn holds or the gap runs (*free_t = the earliest time a
 * new grant may be: the held turn's impact or its earliest one, + gap - lead; NULL allowed) */
int somvr_pace_may(const somvr_pace *p, const somvr_pace_cfg *c, uint64_t id, double t, double *free_t);
/* the earliest time a new grant may come (t when free now; an answer hold: its end) */
double somvr_pace_free_t(const somvr_pace *p, const somvr_pace_cfg *c, double t);
/* the game granted orc id at t: its turn (a turn of id not started yet is renewed). Returns 1 when another orc's turn
 * held already (an overlap: granted past the pace) */
int somvr_pace_grant(somvr_pace *p, const somvr_pace_cfg *c, uint64_t id, double t);
/* orc id's attack at Talion started at t0: keyless = all its key times none, or impact_s (its impact key) missing or
 * under SOMVR_PACE_MIN_WIND_S (run 213948: 7 body-collision records with only a BCOL key, one with its impact at 0 s
 * right after a parried blow) = a move, not a blow: counted, nothing else; else its impact at t0 + impact_s. Returns
 * SOMVR_PACE_* bits */
#define SOMVR_PACE_MIN_WIND_S 0.1f
enum { SOMVR_PACE_GRANTED = 1, SOMVR_PACE_UNANNOUNCED = 2, SOMVR_PACE_OVERLAP = 4, SOMVR_PACE_MOVE = 8,
       SOMVR_PACE_ANSWER = 16 /* the blow started in its own answer hold */ };
int somvr_pace_start(somvr_pace *p, const somvr_pace_cfg *c, uint64_t id, double t0, float impact_s, int keyless);
/* orc id's attack ended at t before its impact (a hit or a stagger cancelled it, or a parry met it): within near_s
 * before the impact it counts as landed now (the blow met the blade; *gap as somvr_pace_update's, -1 = the first or
 * not landed), earlier as cancelled; its turn ends (a marked orc's: its answer hold). Returns 1 = landed, 0 = cancelled,
 * -1 = no started turn of id */
int somvr_pace_end(somvr_pace *p, const somvr_pace_cfg *c, uint64_t id, double t, float near_s, double *gap);
/* one pass (one slot's scoring in one AI tick) grants at most one: a newly accepted candidate against the one the
 * pass kept so far: 1 = the new one is kept (the old one vetoed), 0 = the new one is vetoed. A prio candidate (the
 * game sorts it first whatever its score) beats a plain one; else the higher score; a tie keeps the first. */
int somvr_pace_pass_wins(int new_prio, float new_score, int kept_prio, float kept_score);
/* build 19, the answer hold (cfg answer_s > 0): orc id's live turn may answer (a berserker; set at its grant or its
 * blow's start). Its blow's landing (somvr_pace_update) then turns the turn into a hold of verdict_s instead of freeing
 * it. Returns 1 = marked */
int somvr_pace_may_answer(somvr_pace *p, const somvr_pace_cfg *c, uint64_t id);
/* the verdict of orc id's landed blow at t: hit = it hit Talion: the hold runs on to t + answer_s; else it ends now.
 * Returns 1 = a hold of id held on, 0 = ended (no hit), -1 = no waiting hold of id */
int somvr_pace_verdict(somvr_pace *p, const somvr_pace_cfg *c, uint64_t id, double t, int hit);
/* orc id's answer hold ends now (Talion's recoil over: no answer any more). Returns 1 = there was one */
int somvr_pace_release(somvr_pace *p, uint64_t id);
/* orc id's answer hold now: 0 = none, 1 = waiting for its verdict, 2 = held after a hit (*since = its start) */
int somvr_pace_holding(const somvr_pace *p, uint64_t id, double *since);

#endif
