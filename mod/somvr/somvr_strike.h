/* somvr_strike.h - SOMVR_BOW_STRIKE=dash|blink (build 12; design-vr.md 3 "The bow" - the Shadow Strike and the wraith dash,
 * design-locomotion.md 3.9 LO-9; docs/re-bow.md 14): X with the bow out sends the game's own Shadow Strike
 * (CombatPrimary04 121 in the aim mode); the game carries Talion to the orc, the view follows as the wraith dash.
 * Pure: no globals, no game reads (somvr_strike_game.c feeds it). Tested by tests/test_strike.c.
 *
 * Units: world cm, seconds. The view point is the anchor's output (SOMVR_ANCHOR=root: Talion's root + the eye height),
 * the base the rig maps the tracked head from; position only: nothing here turns the view.
 *
 * A press (X) arms the dash for window_s. The stock strike (run 064438): 0.16 s after the press the game lunges Talion
 * 1.6 m toward the orc in 0.2 s (PC_WraithDash_KD_Start, up to 14 m/s), at 0.46 s it teleports him 7.8 m to the orc
 * (the sync PC_WraithDash_KD_Bip_Standing_F, a knockdown, 3.6 s). So: an armed frame whose view point moved faster
 * than start_cms (the lunge) holds the view where it was (HOLD: the game's lunge never carries the view); a frame that
 * moved more than start_cm (the teleport: no continuous motion moves that far in one frame), or hold_max_s of holding,
 * starts the dash: the view runs from A0 (the held point, or the frame before's) to the target P(t) (the anchor's
 * output now) as V = A0 + (P - A0) f(tau), f the eased constant-speed profile over dash_s: the speed rises over ease_s
 * as a half cosine (no step in velocity), stays constant, falls the same way; P still after the jump = a straight line
 * at constant speed with eased ends. Nothing moves the view while armed and still (the stick walks as ever).
 * tunnel: amount x the speed's share (0 at the ends, full at constant speed). The blink: V stays at A0 while the white
 * rises over blink_in_s, jumps to P under full white, the white falls over blink_out_s.
 */
#ifndef SOMVR_STRIKE_H
#define SOMVR_STRIKE_H

#include <stdint.h>

#include "somvr_pose.h"

enum { SOMVR_BSK_BLOCK = 0, SOMVR_BSK_GAME = 1, SOMVR_BSK_DASH = 2, SOMVR_BSK_BLINK = 3, SOMVR_BSK_NMODE = 4 };
enum { SOMVR_BSK_IDLE = 0, SOMVR_BSK_ARMED = 1, SOMVR_BSK_MOVE = 2, SOMVR_BSK_HOLD = 3 };

typedef struct {
    int mode;           /* SOMVR_BSK_*: block (default: no button starts it), game (nothing of ours sends it), dash, blink */
    float press_s;      /* 121 held this long per press (0.15) */
    float window_s;     /* the move must start within this of the press (2.0) */
    float start_cm;     /* a frame's view move over this is a teleport: the dash (100) */
    float start_cms;    /* a move faster than this (under start_cm a frame) is the game's lunge: the view held (600
                           cm/s: the aim walks at most 150) */
    float hold_max_s;   /* a hold with no teleport dashes to where he is after this (0.8) */
    float dash_s;       /* the dash's duration (0.30) */
    float ease_s;       /* each end's ease (0.06) */
    float tunnel;       /* the wraith tunnel's amount at full speed (0.4: the vignette ring) */
    float blink_in_s, blink_out_s; /* the white blink: 0.03 up, 0.07 down (0.1 s) */
} somvr_sk_cfg;
void somvr_sk_cfg_default(somvr_sk_cfg *c);
const char *somvr_sk_mode_name(int mode);  /* "block", "game", "dash", "blink" */
int somvr_sk_mode_parse(const char *s);    /* -1 = not a mode */

/* the eased constant-speed profile over T with ends e (e <= T/2): the share of the way at tau (0..1, clamped) and the
 * speed's share of the constant speed (0..1) */
float somvr_sk_profile(float tau, float T, float e);
float somvr_sk_speed(float tau, float T, float e);
/* the constant speed for a way of D cm (cm/s) */
float somvr_sk_vmax(float D, float T, float e);

typedef struct {
    int phase;           /* SOMVR_BSK_IDLE / ARMED / HOLD / MOVE */
    double press_t, t0;  /* the press, the move's (or the hold's) start */
    sv_vec3 held;        /* HOLD: the view point kept */
    sv_vec3 vlast;       /* the view point given last frame */
    int have_vlast;
    sv_vec3 a0;          /* the view point the move starts from */
    sv_vec3 last;        /* the last frame's target (the anchor's output) */
    double last_t;
    int have_last;
    float dist0;         /* |P - A0| at the start (cm) */
    float jumped;        /* the start frame's move (cm) */
    int jumped_once;     /* blink: the view has jumped to P (at t_jump, under full white) */
    double t_jump;
    int carried;         /* this press: frames the game carried him (fast, continuous) */
    uint64_t presses, moves, timeouts, frames, carried_frames;
} somvr_sk;
void somvr_sk_reset(somvr_sk *s);

typedef struct {
    sv_vec3 view;        /* the view point to use this frame */
    int active;          /* MOVE: view differs from the target */
    int started, ended, timed_out; /* this frame */
    int carried;         /* this frame: a fast continuous move (the hold began or goes on) */
    int held;            /* this frame: the view held (HOLD) */
    float f, speed;      /* the profile's share and speed share */
    float tunnel, blink; /* 0..1 */
} somvr_sk_out;

/* a press at t (X with the bow out): arms the dash (a press while moving is ignored) */
void somvr_sk_press(somvr_sk *s, double t);
/* one stereo frame at t: p = the anchor's output (the target). Dash and blink only; other modes pass p through. */
void somvr_sk_step(somvr_sk *s, const somvr_sk_cfg *c, double t, sv_vec3 p, somvr_sk_out *o);

#endif
