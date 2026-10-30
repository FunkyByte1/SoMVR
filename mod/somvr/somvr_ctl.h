/* somvr_ctl.h - the motion-controller controls (docs/re-xr-input.md 3.2-3.8, design-vr.md 2-3): from one somvr_input
 * snapshot per game tick to the virtual pad and the command holds that somvr.c injects at the binding layer, the turn
 * of the rig (snap or smooth, about the head) and the haptic mixer. Pure: no globals, no game calls (somvr.c owns the
 * snapshot read, the binding layer, the ON/OFF events, pad mode and the rig). Tested by tests/test_ctl.c.
 *
 * Hands: the move hand (left by default) has the locomotion stick, X/Y (lower/upper) and the menu button (always the
 * physical left controller); the sword hand (right) has the turn stick and A/B. SOMVR_HAND=left swaps the roles.
 */
#ifndef SOMVR_CTL_H
#define SOMVR_CTL_H

#include "somvr_input.h"
#include "somvr_pose.h"

/* game command ids (table FUN_025e1450, docs/re-input.md 2.1; somvr_cmds.h has every name) */
enum {
    SC_CMD_FORWARDAXIS = 3, SC_CMD_STRAFEAXIS = 6, SC_CMD_PITCHAXIS = 12, SC_CMD_YAWAXIS = 13, SC_CMD_MENU = 14,
    SC_CMD_CROUCH = 15, SC_CMD_JUMP = 16, SC_CMD_FREERUN = 18, SC_CMD_PITCHACCEL = 22, SC_CMD_YAWACCEL = 23,
    SC_CMD_CENTERVIEW = 32, SC_CMD_ACTIVATE = 34, SC_CMD_PDA = 95, SC_CMD_STRIKE = 118 /* CombatPrimary01 */,
    SC_CMD_COUNTER = 119 /* CombatPrimary02 */, SC_CMD_STUN = 120 /* CombatPrimary03 */, SC_CMD_EVADE = 121,
    SC_CMD_VISION = 243 /* VisionModeToggle */, SC_NCMD = 349
};

/* the virtual Xbox pad: Win32GamePad input ids (re-input.md 1.3); index = id - SC_PAD_BASE */
enum {
    SC_PAD_BASE = 2000, SC_PAD_UP = 0, SC_PAD_DOWN, SC_PAD_LEFT, SC_PAD_RIGHT, SC_PAD_START, SC_PAD_BACK, SC_PAD_L3,
    SC_PAD_R3, SC_PAD_LB, SC_PAD_RB, SC_PAD_A, SC_PAD_B, SC_PAD_X, SC_PAD_Y, SC_PAD_RT, SC_PAD_LT, SC_PAD_LX,
    SC_PAD_LY, SC_PAD_RX, SC_PAD_RY, SC_NPAD
};
#define SC_PAD_AXES ((1u << SC_PAD_LX) | (1u << SC_PAD_LY) | (1u << SC_PAD_RX) | (1u << SC_PAD_RY))
const char *sc_pad_name(int idx); /* "A", "LX", ... ("?" out of range) */

/* where the player is: menu = anything but the world (front end, pause, loads, videos, cinematics' menus) */
enum { SC_CTX_MENU = 0, SC_CTX_WORLD = 1, SC_CTX_STRUGGLE = 2 };
/* the interface state (G17: 3 = in game), the menu state (0 = none), paused, a video; a struggle (PlayerCombat
 * +0x8d8/+0x8e0 set) in the world */
int sc_context(int ifs, int menus, int paused, int video, int struggle);
const char *sc_ctx_name(int ctx);

/* never injected in ctx (re-xr-input.md 3.6): the stock strike/counter/stun (CombatPrimary01-03 = pad X/Y/B), the
 * unknown CombatSecondary01/02 and CombatTertiary slots, the KBM combat, SpecialAttack1-4, Debug*, PhotoMode_*; in the
 * world and in a struggle also the camera axes (12/13/22/23) and CenterView. In those contexts the same records are
 * held at 0/off against polled reads (I1-I3). */
int sc_denied(int cmd, int ctx);

/* the left stick's radial dead zone with rescale: v (r - dead) / ((1 - dead) r), r clamped to 1 */
void sc_deadzone(const float in[2], float dead, float out[2]);
/* the stick rotated by d (rad, + = to the right): forward (0, 1) -> (sin d, cos d) */
void sc_rotate(float v[2], float d);
/* a turn of the rig by dpsi about the head (re-xr-input.md 3.4): the recentre point P0 moves so the head keeps its
 * world position, P0' = h - R(Y(-dpsi)) (h - P0), x and z only (h, P0: mirrored tracking space, cm) */
sv_vec3 sc_turn_p0(sv_vec3 p0, sv_vec3 head, float dpsi);

typedef struct {
    float dead;                  /* stick dead zone, radial (0.15) */
    float snap_on, snap_off;     /* snap: |x| > 0.7 turns once, |x| < 0.3 re-arms */
    int smooth;                  /* 1 = smooth turn instead of snap */
    float smooth_rate;           /* rad/s at full deflection (SOMVR_TURN_DPS, 120 deg/s) */
    float smooth_ease;           /* s to full rate (0.1) */
    float sprint_r, sprint_s;    /* the sprint latch ends when the move stick stays under 0.2 for 0.3 s */
    float crouch_on, crouch_off; /* the turn stick down: y < -0.7 = one Crouch pulse, re-armed above -0.3 */
    float grip_on, grip_off;     /* menus: the squeeze as LB/RB, > 0.6 on, < 0.4 off */
    float trig_on, trig_off;     /* menus: the trigger as LT/RT's digital state */
    float flick_on, flick_off;   /* menus: a move-stick flick past 0.7 = a d-pad edge, back under 0.5 = none */
    float rep0, rep;             /* d-pad repeat: after 0.4 s, then every 0.12 s */
    float stall_s;               /* the snapshot's sync_seq still for 4 ticks and this long = input lost (0.05) */
    int stall_ticks;
    int left_handed;             /* 1: the sword in the left hand, the move stick on the right */
    int game_moves;              /* 1 (default): A = the stock A in the world, the sprint latch = FreeRun; 0
                                    (SOMVR_GAMEMOVES=off): A nothing in the world, the latch no FreeRun (the game's
                                    jump, vault, ledge pull-up, evade and free-run never carry the view) */
    /* build 27 (SOMVR_ARMY, re-xr-pipeline.md 110): the right stick's click in the world. 0 (default) = the map (PDA
     * held while the click is). > 0: a click let go within army_s = the map (PDA for one tick at the release); held
     * army_s = the game's "Hold [>]" (the pad's START held: army_cmd for army_ms, once per press) - Sauron's Army /
     * the warchief's target; the left menu button's hold stays the VR panel's */
    float army_s, army_ms;
    int army_cmd;
} sc_ctl_cfg;
void sc_ctl_cfg_default(sc_ctl_cfg *c);

/* why a tick is not live (sc_live) */
enum { SC_LIVE_OK = 0, SC_LIVE_NOREAD, SC_LIVE_NOSYNC, SC_LIVE_NOTRUNNING, SC_LIVE_UNFOCUSED, SC_LIVE_STALLED };
const char *sc_live_name(int why);

typedef struct {
    /* liveness */
    uint64_t seq;          /* the last sync_seq seen */
    int same_ticks;        /* ticks since it last changed */
    double same_t;         /* when it last changed */
    /* buttons: per hand, the layer's press counters baselined, the logical state, armed = seen up since a loss */
    int have;
    uint8_t presses[2][SOMVR_NBTN];
    uint32_t down[2], armed[2], again[2];
    /* analog-derived digital states (menus) and edges */
    int grip_dn[2], trig_dn[2];
    int snap_armed, crouch_armed;
    float ease_t;
    int sprint;
    float sprint_low;
    int flick_dir;         /* 0 none, 1 + SC_PAD_UP..SC_PAD_RIGHT */
    double flick_next;
    double last_t;
    int was_live;
    int r3_dn, r3_long;    /* SOMVR_ARMY: the right stick's click down (in the world), its hold given */
    double r3_t0, army_until;
} sc_ctl;
void sc_ctl_reset(sc_ctl *s); /* everything up and disarmed; the counters are re-baselined at the next live tick */

/* 1 = live: a consistent read, at least one sync, the session running, the snapshot FOCUSED and SYNCED and its
 * sync_seq advanced within the last stall_ticks ticks or stall_s seconds. *why = SC_LIVE_* */
int sc_live(sc_ctl *s, const sc_ctl_cfg *c, const somvr_input_state *st, int read_ok, int running, double t, int *why);

#define SC_OUT_CMDS 16
typedef struct {
    int ctx, live;
    float move_raw[2], move[2];  /* the move stick raw / after the dead zone and the rotation (world, struggle) */
    float pad[SC_NPAD];          /* the virtual pad: axes -1..1 (after the dead zone), buttons 0/1, triggers 0..1 */
    uint32_t pad_on;             /* bit i: pad input i pressed (buttons/triggers) or deflected (axes, |v| > 0) */
    int ncmd;                    /* command-level holds this tick (absent = off) */
    int cmd[SC_OUT_CMDS];
    int snap;                    /* -1, 0, +1 rig steps this tick */
    float turn;                  /* rad this tick (smooth turn; + = right) */
    int sprint;                  /* the sprint latch (FreeRun with game_moves) */
    uint32_t edges;              /* SC_E_*: what started this tick (logs) */
    uint32_t raw;                /* bit SC_PAD_A/B/X/Y/LB/RB: that button held now, in any context (SOMVR_USE: the
                                  * quest cards' pad buttons while a presentation is up in the world) */
} sc_tick_out;
enum { SC_E_SNAP = 1u, SC_E_CROUCH = 2u, SC_E_SPRINT = 4u, SC_E_DPAD = 8u, SC_E_TAP = 16u, SC_E_REPRESS = 32u,
       SC_E_MAP = 64u /* SOMVR_ARMY: the click's map */, SC_E_ARMY = 128u /* SOMVR_ARMY: its hold */ };

/* One tick: st = the snapshot (NULL or live = 0: everything off and the state reset), ctx, dpsi = the stick's
 * pre-rotation (the head's world heading minus the game camera's, re-xr-input.md 3.3), t = now (s). The per-button
 * rules (3.1): a hold lasts at least one tick; a press counter that moved while the button reads up gives one tick
 * down; a release and a new press between two ticks give one tick up, then down; after a loss a button counts only
 * once it has been seen up. A hand without SOMVR_HACT_INPUT gives nothing (its buttons up and disarmed). */
void sc_tick(sc_ctl *s, const sc_ctl_cfg *c, const somvr_input_state *st, int live, int ctx, float dpsi, double t,
             sc_tick_out *out);

/* ---- haptics (re-xr-input.md 3.7): a mixer per hand ---------------------------------------------------------------- */
#define SC_HAP_FX 4
typedef struct { double t0; float dur, amp; int ramp; uint32_t source; } sc_hap_fx; /* ramp: 1 = linear down to 0 */
typedef struct {
    sc_hap_fx fx[SC_HAP_FX];
    float rumble;          /* the game's rumble share for this hand, 0..1 (set per tick) */
    float last;            /* the amplitude last posted (0 = stopped / nothing running) */
    int ticks;             /* ticks since the last post */
    double last_end;       /* a flat effect posted runs on its own until here */
} sc_hap;
void sc_hap_reset(sc_hap *h);
void sc_hap_add(sc_hap *h, double t, float amp, float dur, int ramp, uint32_t source); /* replaces the weakest */
/* the same for a pulse starting at t0 >= now (one still to come keeps its slot against later adds) */
void sc_hap_add_at(sc_hap *h, double now, double t0, float amp, float dur, int ramp, uint32_t source);
/* the level now: max(the effects, the rumble) x scale, clamped to 1; *src its source (SOMVR_HAP_*) */
float sc_hap_level(const sc_hap *h, double t, float scale, uint32_t *src, float *remaining, int *continuous);
/* one tick: 1 = post *out (somvr_haptic_post): a level that moved by more than 0.02 (a flat effect: its whole
 * remaining time; a ramp or the rumble: 6 periods, refreshed every 4th tick), 0 after something ran = a stop */
int sc_hap_tick(sc_hap *h, double t, float scale, float period_s, somvr_haptic_req *out);

#endif
