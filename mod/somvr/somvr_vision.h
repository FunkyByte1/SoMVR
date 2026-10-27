/* somvr_vision.h - SOMVR_VISION: the Y button -> the wraith vision (re-xr-pipeline.md 49). The game's vision is a
 * toggle (PC_WraithPing: a tap of CombatSecondary01 122; its flag PlayerVisionMode+0x48), so Y gives a wish (on or off)
 * and taps bring the flag to it:
 *   hold: Y's press with the vision off (or going off, or wished off) = on, ours; Y not held while the vision is ours =
 *         off (the level, not the release's edge: a release never seen cannot leave it on); a press with the vision on
 *         (the game's own, a phase's) = off.
 *   tap:  a press = the other state.
 * A tap's change is waited for wait_s (the game turns it ~50 ms after the tap), the next tap starts settle_s after the
 * flag's last change at the earliest: the game keeps the vision on at least ~1 s (an off tap 0.06, 0.40 and 0.75 s after
 * the on is dropped, one 1.05 s after it lands: runs 233334, 002021) and off at least 1 s (an on tap then is held back
 * and lands 1.0 s after the off: run 002021) - a tap in that second would land late or not at all, and a retry on top
 * of a held-back one could toggle twice. A wish not reached is tapped again (every wait_s) up to tries times, then
 * given up (logged; the next press tries again). A tap is
 * tap_ticks input ticks of the command, two taps are a tick apart (the game sees two presses). The flag unread (-1):
 * blind, the wish's edge taps once as build 15b did. A change nobody tapped for (a phase's look, the game's own) is no
 * wish of ours: it stays until Y.
 * Pure: no globals, no game reads. Tested by tests/test_vision.c; the game side is somvr_gameopts_game.c. */
#ifndef SOMVR_VISION_H
#define SOMVR_VISION_H

#include <stdint.h>

enum { SOMVR_VIS_NONE = 0, SOMVR_VIS_HOLD = 1, SOMVR_VIS_TAP = 2 };

/* one tick's events (bits of somvr_vis_out.ev) */
enum {
    SOMVR_VIE_PRESS_ON = 1u,   /* Y pressed: on wished */
    SOMVR_VIE_PRESS_OFF = 2u,  /* Y pressed with the vision on (or going on): off wished */
    SOMVR_VIE_RELEASE = 4u,    /* hold: Y not held while the vision is ours: off wished */
    SOMVR_VIE_TAP = 8u,        /* a tap starts (out.tries = its number for this wish, out.to = the state it asks) */
    SOMVR_VIE_LANDED = 16u,    /* the flag turned while our tap waited (out.ms after the tap's start) */
    SOMVR_VIE_GAME = 32u,      /* the flag turned with no tap of ours waiting (the game's own, a phase's) */
    SOMVR_VIE_LATE = 64u,      /* a tap's change not seen within wait_s */
    SOMVR_VIE_GIVEUP = 128u,   /* the wish not reached after tries taps: given up */
    SOMVR_VIE_DONE = 256u,     /* the wish reached */
    SOMVR_VIE_BLIND = 512u,    /* the flag unread: the wish's edge tapped once, not followed */
};

typedef struct {
    float wait_s;   /* a tap's change waited for this long (0.3 s) */
    float settle_s; /* a tap at the earliest this long after the flag's last change (1.05 s: the game's ~1 s dwell) */
    int tries;      /* taps for one wish at most (6) */
    int tap_ticks;  /* a tap's length in input ticks (2: a toggle; 0.85 s held would be PC_Radiance) */
} somvr_vis_cfg;
void somvr_vis_cfg_default(somvr_vis_cfg *c);

typedef struct {
    int mode;          /* SOMVR_VIS_* */
    int y;             /* Y held (in the world) last tick */
    int want;          /* the wish: -1 none, 0 off, 1 on */
    int ours;          /* hold: the vision is on (or asked on) by Y */
    int tap, gap;      /* ticks of the tap left; ticks without the command before the next tap */
    int tries;         /* taps for this wish */
    int pending, to;   /* a tap waits for its change; the state it asked */
    int flag;          /* the flag last tick (-1 unread) */
    double tap_t;      /* the last tap's start */
    double want_t;     /* the wish's start */
    double flag_t;     /* the flag's last change (0 = none seen) */
    uint64_t n_press, n_taps, n_retry, n_landed, n_game, n_late, n_giveup, n_blind;
} somvr_vis;
void somvr_vis_init(somvr_vis *v, int mode);

typedef struct {
    int tap;      /* this tick: the toggle command (only in the world) */
    uint32_t ev;  /* SOMVR_VIE_* */
    int flag;     /* the flag read */
    int want;     /* the wish after this tick */
    int to;       /* SOMVR_VIE_TAP: the state the tap asks */
    int tries;    /* SOMVR_VIE_TAP / GIVEUP: the taps for this wish */
    float ms;     /* SOMVR_VIE_LANDED: since the tap's start */
    float done_ms; /* SOMVR_VIE_DONE: since the wish */
} somvr_vis_out;
/* One input tick at t (s): world = the player is in the world (the tap is sent only there); y = Y held; flag = the
 * vision's flag (1 on, 0 off, -1 unread) */
void somvr_vis_tick(somvr_vis *v, const somvr_vis_cfg *c, int world, int y, int flag, double t, somvr_vis_out *o);
const char *somvr_vis_mode_name(int mode);

#endif
