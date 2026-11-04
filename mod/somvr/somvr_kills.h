/* somvr_kills.h - SOMVR_KILLS=on|off: kills the game's way in VR (build 31, docs/re-xr-pipeline.md 133). The orc plays
 * its half of the game's own paired kill (a sync record's TargetAttack, the AI_Bip_PC_* families); Talion's half is the
 * player's real blade and is never started. Pure: no globals, no game reads (somvr_kills_game.c feeds it). Tested by
 * tests/test_kills.c.
 *
 * The blow picks the kill: from the contact, which side of the orc the blade came from (its front or back), the blow's
 * kind (a stab along the blade; a cut: level, overhead, rising), the part (the neck and head, or the body) and the orc's
 * state (fighting, unaware, stunned, downed, countered mid-attack). The table (somvr_kills_game.c, the catalogue's
 * numbers in docs 133.2) lists each victim record with what it shows; the pick takes the rows that fit, in turn. */
#ifndef SOMVR_KILLS_H
#define SOMVR_KILLS_H

enum { SOMVR_KL_FRONT = 1, SOMVR_KL_BACK = 2 };                          /* side (a mask in a row) */
enum { SOMVR_KL_STAB = 1, SOMVR_KL_CUT = 2, SOMVR_KL_OVER = 4, SOMVR_KL_RISE = 8 }; /* kind (a mask in a row) */
enum { SOMVR_KL_BODY = 1, SOMVR_KL_NECK = 2 };                            /* part (a mask in a row) */
enum { SOMVR_KL_FIGHT = 1, SOMVR_KL_UNAWARE = 2, SOMVR_KL_STUNNED = 4, SOMVR_KL_DOWNED = 8, SOMVR_KL_COUNTER = 16,
       SOMVR_KL_HELD = 32 /* held by the left hand (the grab) */ };
#define SOMVR_KL_ANYKIND 15
#define SOMVR_KL_ANYPART 3

typedef struct {
    const char *name;  /* the victim record (AI_Bip_PC_*): the orc's half */
    const char *src;   /* the player's record whose sync pairs it (PC_Bip_*; the death's DealHit record), NULL = the caller's */
    unsigned char side, kinds, parts, states;
    unsigned char sever; /* 1 = the head comes off (a _Sever row: the game's sever by the cut) */
    float death_s;       /* the death's moment after the start (s; <= 0: the animation's end) */
    float len_s;         /* the animation's length as measured (s; <= 0: not measured) */
    unsigned char slow;  /* build 36: 1 = its fall comes late or the orc stands or walks in it (the game's keys, the runs:
                          * docs 164) - SOMVR_KILLS_QUICK leaves it out */
} somvr_kl_row;

typedef struct {
    int side;  /* SOMVR_KL_FRONT / _BACK */
    int kind;  /* one SOMVR_KL_STAB / _CUT / _OVER / _RISE */
    int part;  /* SOMVR_KL_BODY / _NECK */
    int state; /* one SOMVR_KL_FIGHT / _UNAWARE / _STUNNED / _DOWNED / _COUNTER */
} somvr_kl_blow;

/* the side the contact came from: the angle of the contact about the orc's root off its facing (game space: heading yaw
 * = atan2(x, z)); more than back_deg off = its back. *off_deg (may be NULL) = the angle, -180..180 */
int somvr_kl_side(const float contact[3], const float orc[3], float orc_yaw, float back_deg, float *off_deg);

/* the blow's kind from the blade's tip velocity (cm/s or m/s, any unit) and the blade's axis (base -> tip, any length):
 * a stab when the motion runs along the blade, the point leading (cos >= stab_cos), else a cut - overhead when it falls
 * steeply (down more than 0.6 of it), rising when it climbs as steeply, else level */
int somvr_kl_kind(const float vel[3], const float axis[3], float stab_cos);

/* the part: the contact at or above neck_up_cm over the orc's root (or within neck_r_cm of its neck node, when known) =
 * the neck and head */
int somvr_kl_part(float contact_up_cm, float neck_up_cm, float neck_dist_cm, float neck_r_cm);

/* a row fits the blow: its side, state and part masks hold the blow's and its kinds the blow's kind */
int somvr_kl_fits(const somvr_kl_row *r, const somvr_kl_blow *b);

/* the row for the blow, in turn among the best fits (off[i] != 0: row i not used - not in the database, failed to
 * play): the rows that fit fully; else, keeping side and state, any part and kind (a neck cut without a sever row takes a
 * body row, never the reverse: a sever row needs the neck); else -1 */
int somvr_kl_pick(const somvr_kl_row *rows, int n, const somvr_kl_blow *b, unsigned turn, const unsigned char *off);

/* the row named name (exactly), or -1 */
int somvr_kl_find(const somvr_kl_row *rows, int n, const char *name);

/* the placement (the orc to the player, never the player to the orc): the turn that puts the orc's front (side FRONT:
 * it faces Talion) or its back (BACK: its back to him) toward Talion's root, and the slide along the line between them
 * that keeps the roots at least min_cm apart. Clamped to max_deg and max_cm; 0 = needed more than twice either (too far
 * off: no alignment), 1 = *dyaw (rad) and *dpos (cm, x and z) set */
int somvr_kl_align(const float orc[3], float orc_yaw, const float talion[3], int side, float min_cm, float max_cm,
                   float max_deg, float *dyaw, float dpos[2]);

/* the share of the alignment applied by `elapsed` of `dur` seconds (smooth: 0 at the start, 1 at the end and after) */
float somvr_kl_ease(double elapsed, double dur);

/* the death's moment for a row: death_s when > 0; else the measured length less lead_s (when measured); else NAN (the
 * animation's end decides) */
float somvr_kl_death_at(const somvr_kl_row *r, float lead_s);

/* build 36 (SOMVR_KILLS_QUICK, docs/re-xr-pipeline.md 164): a playing half's checks, on the orc's own clock (el, s from
 * its start; a hit-stop's freeze not counted) - by start_s its pose moved start_cm against its root in its own frame, by
 * fall_s its head fall_cm lower, the death by max_s at the latest; seen = its skeleton read. 0 (GO) = it plays on, else
 * why its death is now. A value <= 0 turns that check off */
typedef struct {
    float start_s, start_cm, fall_s, fall_cm, max_s;
    float rise_cm; /* its head this much over the blow's before its fall: the orc straightening up, no half (boots
                    * 20261103-230240/232418: F3, F9, B2 on orcs in a recoil +11..15 cm by 0.15-0.2 s, none played; the
                    * playing F4 and F5 rose 8-10 cm first) */
} somvr_kl_quick;
enum { SOMVR_KLQ_GO = 0, SOMVR_KLQ_UNSEEN, SOMVR_KLQ_STILL, SOMVR_KLQ_NOFALL, SOMVR_KLQ_LATE, SOMVR_KLQ_RISING, SOMVR_KLQ_N };
/* rise_cm = the most its head has been over the blow's height so far */
int somvr_kl_quick_why(const somvr_kl_quick *q, double el, int seen, float moved_cm, float drop_cm, float rise_cm);
const char *somvr_kl_quick_name(int v);
/* the most any of n nodes moved against the root in the orc's own frame between two poses (each node less the root,
 * world cm; the frame turned by its heading yaw, atan2(x, z)) - a turn or a slide of the whole orc is no motion */
float somvr_kl_pose_moved(const float (*a)[3], float yaw_a, const float (*b)[3], float yaw_b, int n);

const char *somvr_kl_kind_name(int kind);
const char *somvr_kl_state_name(int state);

#endif
