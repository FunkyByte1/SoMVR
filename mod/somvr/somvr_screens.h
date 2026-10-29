/* somvr_screens.h - build 22 (som-b22-screens; docs/re-xr-pipeline.md 90): death, the Nemesis screens and the game's
 * menus in VR. Pure: no globals, no game reads, no OpenXR. Tested by tests/test_screens.c; somvr_screens_game.c does
 * the reads and applies the results.
 *
 * 1. The death in first person (SOMVR_BM_DEATH=first). Build 21 (SOMVR_BODYMOMENTS): the death greys the view out and
 *    holds it, then the game's death presentation (`Player_Death`: its camera looking down at Talion's body, the killer
 *    walking up and his taunt) went to the window - the game camera on the calm space's screen - and the Nemesis screens
 *    after it stayed there because the window never changes while the game is paused. With this the death is the
 *    player's own to the end: while the death condition holds and the game runs, no window (the view stays where Talion
 *    fell, the world greyed; the killer's taunt plays in the world around it, his name and line on the HUD); the game's
 *    pause after it (the Nemesis screens: the killer at the head of the army, the Duel, the board) opens the window then
 *    - the theatre's screen - although the game is paused. B on the board = the game's respawn presentation in the same
 *    window, its end back to the world through the layer's fade. A death hold that ends without the window (no Nemesis
 *    screens: a fall, a beast) re-seats with a slow blink - the view fades to black, waits there, fades in at the place
 *    the game put Talion - never the quick blink of a counter.
 * 2. The pointer's Mouse1 on the Nemesis screens (SOMVR_POINTER_MOUSE1). The laser and the fingertip drive the game's
 *    mouse (CInterfaceMgr's move/down/up: GFx's clips), which selects menu buttons; the Nemesis hierarchy layer
 *    (`CUIHierarchyLayer::vf_10`) picks a captain only on the input command Mouse1 (100) at the mouse's stored point
 *    (a KBM mouse's click sends both). With this the pointer's press also holds the command Mouse1 while the top menu
 *    layer is that layer (nemesis) or in every game menu (all). */
#ifndef SOMVR_SCREENS_H
#define SOMVR_SCREENS_H

#include <stdint.h>

/* ==== 1. the death ==================================================================================================== */
enum { SOMVR_SD_IDLE, SOMVR_SD_DYING, SOMVR_SD_NEMESIS };
extern const char *const somvr_sd_phase_names[3];

typedef struct {
    int on;                                       /* SOMVR_BM_DEATH=first */
    float blink_out_s, blink_black_s, blink_in_s; /* a death hold's release without the window (0.35, 0.6, 0.6) */
} somvr_sd_cfg;
void somvr_sd_cfg_default(somvr_sd_cfg *c);
/* "first" / "game" (= off: build 21) [",OUT_MS,BLACK_MS,IN_MS"]; unset = off. 0 = understood, -1 = not (off) */
int somvr_sd_parse(const char *v, somvr_sd_cfg *c);

typedef struct {
    int phase;
    double t0;           /* the phase's start */
    int had_win;         /* this frame asked for the window (before the death's rule) */
    uint64_t deaths, nemesis, respawns, recovered, dropped_frames, paused_opens;
} somvr_sd;
void somvr_sd_init(somvr_sd *d);

typedef struct {
    int win, win_cond;   /* the window wanted this frame (after the rule) and its condition */
    int step_paused;     /* the paused flag for somvr_bm_step: 0 while the death opens the window in the pause */
    int slow_blink;      /* a release now re-seats with the cfg's slow blink */
    int changed;         /* the phase changed this frame */
    int dropped;         /* a window asked for was dropped this frame (the death is the player's own) */
} somvr_sd_out;
/* one H4 frame: death = the moment conditions hold the death (a death name or the dead flag); win / win_cond = what the
 * body moments' policy asks for; paused = the game timer's pause; bm_win = the window in force now (the step's state
 * before this frame); bm_frozen = the view held now or its re-seat blink running (the slow blink to its end); death_cond = the condition index to name the window by. */
void somvr_sd_step(somvr_sd *d, const somvr_sd_cfg *c, int death, int win, int win_cond, int paused, int bm_win,
                   int bm_frozen, int death_cond, double t, somvr_sd_out *o);

/* ==== 2. the pointer's Mouse1 ========================================================================================= */
enum { SOMVR_PM1_OFF, SOMVR_PM1_NEMESIS, SOMVR_PM1_ALL };
extern const char *const somvr_pm1_names[3];
/* "off" | "nemesis" | "all" (also "0" = off, "1"/"on" = nemesis); unset = off; *bad = 1 for anything else (off) */
int somvr_pm1_parse(const char *v, int *bad);
/* the command's hold this tick: mode, the pointer's click held (down sent, up not yet), a game menu up, the top menu
 * layer is the Nemesis hierarchy -> 1 = hold Mouse1 */
int somvr_pm1_want(int mode, int click_held, int in_menu, int top_is_nemesis);

/* the player UI's menu stack (CPlayerUIStateMachine state 5's CUIMenuManagerBase): the top layer's index from the count
 * at +0x1e8 (1..8) -> 0..7, -1 = none */
int somvr_scr_top_index(uint32_t count);

#endif
