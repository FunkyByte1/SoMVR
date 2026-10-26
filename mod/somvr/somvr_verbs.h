/* somvr_verbs.h - SOMVR_VERBS: the game's Nemesis verbs in VR (build 13 candidate; docs/re-xr-pipeline.md 39). Pure: no
 * globals, no game reads (somvr_verbs_game.c feeds it). Tested by tests/test_verbs.c.
 *
 * What the game does (runs 20261025-214207 ... -231228; re-xr-pipeline.md 39.1): the player's root combat nodes and their
 * predicates' Input records name the buttons. The grab = CombatSecondary02 (123) pressed (PC_Grab_Attempt,
 * _Stealth), held (PC_Grab_Attempt_Held), let go (Grab_OnRelease: PC_Grab_Release_Root); while grabbed CombatPrimary03
 * (B, 120) held = the wraith hand's hold (Grab_HOG_Into), CombatPrimary04 (A, 121) = the interrogation (Grab_Subdue:
 * PC_Interrogation_Into), CombatPrimary01 (X, 118) = the grab's flurry (PC_GrabFlurry_Into). Without a grab, B pressed at
 * an orc = PC_HoG_Standing_Start: let go within 0.2 s = the stun (PL_St_*), held = the hold (PC_HoG_Standing_Hold_*
 * ~3 s to _Complete: the drain; let go before = _Out). From behind an unaware orc X = the silent stealth kill, B = the
 * stealth brand (or torment) once unlocked.
 *
 * The VR controls (the HL:A way: the hand does it):
 *   - the grab: the empty left hand on an orc's head or collar (the palm within reach_cm of its head, or the arm reached
 *     out at it: cone_deg, out_cm, far_cm) and a new squeeze = 123 held while the grip stays closed; opened = let go;
 *   - the wraith hand: the left trigger while the hand holds an orc, or pressed with the open hand on an orc's head = B
 *     held while the trigger is (a tap: the stun; held: the drain, or the brand where the game has it);
 *   - the interrogation: while holding, the orc pulled to the face (the left hand within ask_cm of the eyes for ask_s,
 *     after it was at least ask_cm + 10 cm away) = A tapped once;
 *   - the kill (the grab's flurry) and the stealth kill: the sword's own contact (somvr_verbs_game.c, the game's
 *     selection asked first).
 * A fist moved onto an orc holds nothing (the squeeze must start at the head); the trigger is the bow's everywhere else. */
#ifndef SOMVR_VERBS_H
#define SOMVR_VERBS_H

typedef struct {
    float reach_cm;    /* the palm this close to an orc's head point = on its head (45: run 001308's misses at 42-44) */
    float cone_deg;    /* ... or the arm reached out at it: the eyes -> palm line within this of the eyes -> head (25) */
    float out_cm;      /* ... the palm at least this far out from the eyes (35) */
    float far_cm;      /* ... the head at most this far from the eyes (170: an orc in melee stands ~1 m off) */
    float grip_on;     /* the squeeze closes the hand at >= this (0.7) */
    float grip_off;    /* ... and opens it below this (0.35) */
    float trig_on;     /* the trigger held at >= this (0.6) */
    float trig_off;    /* ... let go below this (0.4) */
    float ask_cm;      /* the interrogation: the palm this close to the eyes (35) ... */
    float ask_s;       /* ... this long (0.25 s) */
    float hold_max_s;  /* B let go after this even with the trigger still held (6 s: the drain takes ~3) */
    float cool_s;      /* no new grab or wraith press this long after one ended (0.4 s) */
} somvr_vb_cfg;

void somvr_vb_cfg_default(somvr_vb_cfg *c);

#define SOMVR_VB_ORCS 8

typedef struct {
    double t;
    int live;          /* in the world with the left hand free (not on the bow, a wall, a weapon or the VR menu) */
    float squeeze;     /* 0..1 (NaN = open) */
    float trigger;     /* 0..1 (NaN = let go) */
    int have_palm;
    float palm[3];     /* the left palm, world cm (game space) */
    int have_eye;
    float eye[3];      /* the eyes (the reach-out cone, the interrogation's pull), world cm */
    int norc;
    float head[SOMVR_VB_ORCS][3]; /* the near orcs' head points, world cm */
    int game_done;     /* the game's hold ended on its own (completed or out: Talion's record) */
} somvr_vb_hand_in;

enum { SOMVR_VBH_IDLE = 0, SOMVR_VBH_GRAB, SOMVR_VBH_WRAITH };
enum {
    SOMVR_VBE_NONE = 0,
    SOMVR_VBE_GRAB,       /* a new squeeze on an orc's head (or reached out at it): 123 held from now */
    SOMVR_VBE_RELEASE,    /* the grip opened: 123 let go (the game's release) */
    SOMVR_VBE_LOST,       /* not live any more (a menu, ...): everything let go */
    SOMVR_VBE_MISS,       /* a new squeeze near an orc's head but not on it (within 2 x reach): nothing */
    SOMVR_VBE_WRAITH,     /* the trigger: B held from now (holding an orc, or the open hand on a head) */
    SOMVR_VBE_WRAITH_END, /* the trigger let go: B let go */
    SOMVR_VBE_DONE,       /* the game's hold ended (or hold_max): B let go, the trigger still held */
    SOMVR_VBE_ASK,        /* the held orc pulled to the face: A tapped */
    SOMVR_VBE_N
};
const char *somvr_vb_event_name(int e);

typedef struct {
    int phase;         /* SOMVR_VBH_* */
    int closed;        /* the grip's state (hysteresis) */
    int trig;          /* the trigger's state (hysteresis) */
    int orc;           /* the held orc's index in the last GRAB's (or WRAITH's) input */
    int b;             /* B held now */
    int b_done;        /* the game's hold ended: no B until the trigger is let go */
    int asked, far_seen; /* the interrogation: done for this grab; the palm was away from the face since the grab */
    double t0;         /* the grab's (or the wraith press's) time */
    double tb;         /* B's start */
    double near_t;     /* since when the palm is near the face (-1 = not) */
    double t_end;      /* the last hold's end (the cooldown) */
} somvr_vb_hand;

typedef struct {
    int grab;          /* hold 123 this tick */
    int b;             /* hold B (120) this tick */
    int ask;           /* tap A (121) now (one step) */
    int event;         /* SOMVR_VBE_* this step */
    int orc;           /* GRAB / MISS / WRAITH: the orc's index */
    float dist_cm;     /* GRAB / MISS / WRAITH: the palm's distance to its head */
    float cone_deg;    /* GRAB / WRAITH: the reach line's angle off the head (-1 = no eyes or not out) */
    int by_cone;       /* GRAB / WRAITH: by the reach-out cone (not on the head) */
    int edge;          /* a new squeeze this step while idle (whatever came of it; dist_cm then the nearest head's) */
    float held_s;      /* RELEASE / LOST / WRAITH_END / DONE: how long it was held */
} somvr_vb_hand_out;

void somvr_vb_hand_reset(somvr_vb_hand *s);
void somvr_vb_hand_step(somvr_vb_hand *s, const somvr_vb_cfg *c, const somvr_vb_hand_in *in, somvr_vb_hand_out *o);
/* the left trigger is the verbs' (not the bow's) now: a grab held, or the wraith press running */
int somvr_vb_trigger_taken(const somvr_vb_hand *s);

/* Talion's attack record by name (the logs, the hold's end): */
enum {
    SOMVR_VBK_NONE = 0,
    SOMVR_VBK_HOG,      /* PC_HoG_* (the start and the hold) */
    SOMVR_VBK_HOG_END,  /* PC_HoG_*Complete* / PC_HoG_*_Out* (the hold ended) */
    SOMVR_VBK_STUN,     /* PL_St_* (the stun) */
    SOMVR_VBK_GRAB,     /* PC_Grab_* */
    SOMVR_VBK_STEALTH,  /* *SneakInstaKill*, *StealthTorment*, *StealthBrand* */
    SOMVR_VBK_ASK,      /* *Interrogation* */
    SOMVR_VBK_N
};
int somvr_vb_attack_kind(const char *name);
const char *somvr_vb_kind_name(int k);

#endif
