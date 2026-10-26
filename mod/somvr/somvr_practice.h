/* somvr_practice.h - SOMVR_PRACTICE: the practice area (docs/re-xr-pipeline.md 27). The user's first headset session
 * made them sick and they will not fight in their real save; they asked for "a super simple demo/dev area with no
 * enemies where I can just experiment" and "the ability to spawn in a single orc at a time". Pure: no globals, no game
 * reads, no OpenXR. Tested by tests/test_practice.c; somvr_practice_game.c (included by somvr.c) does the game side:
 *
 *   go      a blink (fade out, the console's Teleport at full black, held until Talion stands at the spot, fade in)
 *           to a fixed quiet spot; the position he left is kept for "back";
 *   in it   Talion cannot die (SetMinHealth), daylight without rain (GameServerShell.AdvanceTimeOfDay from a night,
 *           GameServerShell.ChangeWeather DayNoRain), and about once a second every armed AI within 60 m is removed
 *           (not the player, not his mount, not our own orc, not slaves; never in a scripted moment);
 *   orcs    one at a time, 3.5 m ahead on the ground, facing Talion: infantry, shield or berserker (P9's debug spawn
 *           FUN_029a4830 from our code); a new one removes the previous one; "remove" removes it;
 *   back    the same blink back to the kept position, the light as it was, the min health as it was.
 *
 * This header holds the parts that need no game: the words, the orc types, the trip (blink) state machine and its
 * fade, the keep-clear rule, the scripted-moment gate, the spawn spot and its facing, the night test. */
#ifndef SOMVR_PRACTICE_H
#define SOMVR_PRACTICE_H

#include <stddef.h>
#include <stdint.h>

/* ---- the switch: SOMVR_PRACTICE=0|1|auto ---- */
enum { SOMVR_PR_OFF = 0, SOMVR_PR_ON = 1, SOMVR_PR_AUTO = 2 };
/* "0" "off" "1" "on" "auto" -> *mode; 1 = understood */
int somvr_pr_mode_parse(const char *v, int *mode);
const char *somvr_pr_mode_name(int mode);

/* ---- the orc types (the menu's order) ---- */
enum { SOMVR_PRO_INFANTRY = 0, SOMVR_PRO_SHIELD = 1, SOMVR_PRO_BERSERKER = 2, SOMVR_PRO_N = 3 };
const char *somvr_pr_orc_word(int t);     /* "infantry", "shield", "berserker" ("" outside) */
const char *somvr_pr_orc_label(int t);    /* "Infantry", "Shield", "Berserker" (the menu, the logs) */
/* the AI/Templates names tried in order (P9: Orc_Defender, Orc_Berserker spawned; Orc_Infantry was the captain's own
 * template): "Orc_Infantry", "Orc_Defender", "Orc_Berserker" */
const char *somvr_pr_orc_template(int t);
/* a type word: the words, the labels, "defender", "inf", "berserk", "0".."2" (case-insensitive); -1 = none */
int somvr_pr_orc_parse(const char *w);

/* ---- the requests: the trigger word "practice ..." and the menu's buttons (the same codes as somvr_vrmenu.h's
 * SOMVR_VMPR_*: somvr_practice_game.c asserts it) ---- */
enum { SOMVR_PRA_NONE = 0, SOMVR_PRA_GO = 1, SOMVR_PRA_BACK = 2, SOMVR_PRA_SPAWN = 3, SOMVR_PRA_REMOVE = 4,
       SOMVR_PRA_STATUS = 5, SOMVR_PRA_N = 6 };
const char *somvr_pr_action_name(int a);  /* "go", "back", "spawn", "remove", "status" ("none") */
typedef struct {
    int action;  /* SOMVR_PRA_* */
    int orc;     /* SOMVR_PRO_* for spawn; -1 = the current type */
    int have_at; /* go X Y Z: the spot (cm, the world) for this and later trips (a test run's way to try places) */
    float at[3];
} somvr_pr_req;
/* the text after "practice ": "go [X Y Z]", "back", "spawn [TYPE]", "remove", "status" (blanks around them ignored); a
 * type that is not one, or not three finite numbers = not taken. 1 = taken (*out set) */
int somvr_pr_word(const char *w, somvr_pr_req *out);

/* ---- the trip: the blink there or back ---- */
typedef struct {
    float fade_out_s;   /* the view goes black over this (0.25 s) */
    float fade_in_s;    /* ... and comes back over this (0.35 s) */
    float settle_s;     /* Talion stands at the target this long, still black, before the hold (0.5 s) */
    float hold_s;       /* after the arrival, still black: the light changes happen here (1.0 s) */
    float hold_max_s;   /* ... and longer while the game is busy loading (busy below), at most this after the arrival
                         * (5.5 s; SOMVR_PRACTICE_LOADHOLD: the game's ~1 s pause ~3 s after our ChangeWeather) */
    float arrive_cm;    /* within this of the target in the ground plane (500 cm) ... */
    float fall_cm;      /* ... and not more than this below it (300 cm: below = the ground was not there: again) */
    float timeout_s;    /* no arrival this long after a teleport: again, at most `tries` teleports (3 s) */
    int tries;          /* teleports per trip (3) */
} somvr_pr_tcfg;
void somvr_pr_tcfg_default(somvr_pr_tcfg *c);

enum { SOMVR_PRT_IDLE = 0, SOMVR_PRT_OUT, SOMVR_PRT_MOVE, SOMVR_PRT_HOLD, SOMVR_PRT_IN };
enum {                    /* what somvr_pr_trip_step asks the game side to do now */
    SOMVR_PRX_NONE = 0,
    SOMVR_PRX_TELEPORT,   /* run the teleport to tr->target now (the view is black) */
    SOMVR_PRX_ARRIVED,    /* once: Talion stands at the target (still black for hold_s) */
    SOMVR_PRX_DONE,       /* once: the fade-in ended (arrived) */
    SOMVR_PRX_FAILED,     /* once: no arrival after `tries` teleports: fading in where he is */
};
typedef struct {
    int phase;            /* SOMVR_PRT_* */
    float target[3];      /* cm, the root as the game keeps it (the console's Teleport x y z) */
    double t0;            /* the phase's start */
    double t_tp;          /* the last teleport */
    double settle_t;      /* standing at the target since (0 = not) */
    int tries;            /* teleports made */
    int arrived, failed;  /* the outcome (kept after the trip) */
    float fade_from;      /* the fade-in starts here (a trip started while one was fading in) */
    int held_busy;        /* the hold went on past hold_s for busy (once per trip: the game side logs it) */
} somvr_pr_trip;
/* a trip to target from now on (a trip in progress is replaced: its fade goes on from where it is) */
void somvr_pr_trip_start(somvr_pr_trip *tr, const somvr_pr_tcfg *c, const float target[3], double t);
/* one frame at t: Talion's root pos (have = 0: not known now), paused = the game is paused (no teleport is asked while
 * paused, the black holds). Returns SOMVR_PRX_* */
int somvr_pr_trip_step(somvr_pr_trip *tr, const somvr_pr_tcfg *c, double t, const float pos[3], int have, int paused);
/* ... the same, busy = the game is loading (the hold after the arrival stays black while busy, up to hold_max_s; a
 * pause alone never holds it: the pause menu must be seen) */
int somvr_pr_trip_step_busy(somvr_pr_trip *tr, const somvr_pr_tcfg *c, double t, const float pos[3], int have, int paused,
                            int busy);
/* the black over the view now: 0 none .. 1 black */
float somvr_pr_trip_fade(const somvr_pr_trip *tr, const somvr_pr_tcfg *c, double t);
const char *somvr_pr_phase_name(int phase);
/* pos within arrive_cm of target in the ground plane, and not more than fall_cm below it */
int somvr_pr_at(const float pos[3], const float target[3], float arrive_cm, float fall_cm);

/* ---- in the area: the keep-clear rule ---- */
enum { SOMVR_PRC_OFF = 0, SOMVR_PRC_ARMED = 1, SOMVR_PRC_ALL = 2 }; /* SOMVR_PRACTICE_CLEAR=0|armed|all */
typedef struct {
    float dist_cm;        /* from Talion's root */
    int ours;             /* our spawned orc */
    int player;           /* the player's own Combat */
    int mount;            /* the player's mount */
    int dead;             /* dead or dying (the game removes it itself) */
    int npc;              /* an NPC with an AI and its HOBJECT (the checked pair) */
    int harmless;         /* somvr_pr_harmless(its character record) */
} somvr_pr_cand;
/* 1 = remove this one now */
int somvr_pr_clear_pick(const somvr_pr_cand *c, int mode, float radius_cm);
/* a character record name that is not an armed enemy: slaves, humans, civilians (case-insensitive substrings
 * "Slave", "Human", "Civilian", "Prisoner") */
int somvr_pr_harmless(const char *record_name);

/* ---- the scripted-moment gate: nothing is removed while one of these is on ---- */
enum {
    SOMVR_PRG_PRES = 1u,      /* a PlayerCameraPresentation runs (a Nemesis showdown, an introduction: design-vr.md) */
    SOMVR_PRG_CINE = 2u,      /* the cinematic camera manager has a camera */
    SOMVR_PRG_SCRIPTED = 4u,  /* a scripted level camera */
    SOMVR_PRG_SYNC = 8u,      /* Talion is in a paired sync action (a grab, a kill, a struggle) */
    SOMVR_PRG_PAUSED = 16u,   /* the game is paused */
    SOMVR_PRG_TRIP = 32u,     /* our own blink is on */
    SOMVR_PRG_UNKNOWN = 64u,  /* the gate's reads are not available (a site did not match): see the CONFIG line */
};
/* the gate's bits as words ("a presentation+the game paused"; "nothing" for 0) */
void somvr_pr_gate_text(uint32_t bits, char *buf, size_t cap);

/* ---- the spawn ---- */
/* the point dist_cm ahead of pos along the heading of fwd (its ground-plane part; none: +Z) */
void somvr_pr_ahead(const float pos[3], const float fwd[3], float dist_cm, float out[3]);
/* the rotation (x, y, z, w; forward +Z) of an orc at `at` facing `face` (the yaw only), as the debug spawn makes it */
void somvr_pr_facing(const float at[3], const float face[3], float q[4]);

/* ---- the light ---- */
/* a TOD name (FUN_025ccd10: "Day", "Night"; P9) that is not daylight: contains "night" (case-insensitive) */
int somvr_pr_is_night(const char *tod_name);
/* a weather record name with rain or a storm (SON_Day03_Rain; "DayNoRain" is dry): the practice changes only these */
int somvr_pr_is_wet(const char *weather);

#endif
