/* somvr_ride.h - SOMVR_RIDE (build 26, som-b26-ride; docs/re-xr-pipeline.md 105, design-vr.md 5, design-hla.md SH-2):
 * riding a caragor or a graug in first person, Seat mode. Pure: no globals, no game reads (somvr_ride_game.c feeds it).
 * Tested by tests/test_ride.c.
 *
 * The seat: the beast's own root (its object position: no gait in it - the gait is its skeleton's) plus the rider's eye
 * offset in the beast's frame (right, up, ahead), never the rider's head node itself (his head bobs, leans and turns
 * with the beast's stride). The offset starts at the kind's measured seat and follows Talion's head node only slowly (a
 * moving mean over off_s, at most off_cmps a second), so a stride's bob never reaches it. The caller puts the seat
 * through the root anchor (somvr_loco.h: horizontally exact, the vertical 3rd-order filter, extrapolated to the display
 * time) with the kind's y_s. The base has a position and a yaw only: no pitch or roll ever reaches the view.
 *
 * The yaw, car-style: the beast's heading unwrapped, low-passed (critically damped 2nd order, omega = 1 / yaw_s, exact
 * over any dt), and the view's own heading chasing it at most yaw_dps; each frame's step is the turn the rig adds about
 * the head (as the smooth turn does). A heading jump beyond jump_rad in one frame (a respawn, a new beast) re-latches:
 * no turn.
 *
 * The buttons while riding (somvr_ride_game.c maps them to the commands that reach the beast, re-xr-pipeline.md 109,
 * 122): A, the right trigger (the bite), B (off), the left stick's click latched while the stick is pushed (the
 * sprint); the left stick steers in the beast's frame.
 */
#ifndef SOMVR_RIDE_H
#define SOMVR_RIDE_H

#include <stdint.h>

enum { SOMVR_RD_CARAGOR = 0, SOMVR_RD_GRAUG = 1, SOMVR_RD_NKIND };
extern const char *const somvr_rd_kind_names[SOMVR_RD_NKIND];

typedef struct {
    float seat[SOMVR_RD_NKIND][3]; /* the eyes from the beast's root: right, up, ahead (cm) before any measure */
    float y_s[SOMVR_RD_NKIND];     /* the vertical filter's ramp lag (0.12 caragor, 0.20 graug) */
    float off_s;                   /* the offset's moving mean (2.0 s; 0 = the kind's seat only, never measured) */
    float off_cmps;                /* ... at most this many cm a second (10) */
    float off_max_cm;              /* a head node farther than this from the mean is not the rider's (80; the first
                                    * one: within 5 m of the beast's root - he sits on it) */
    float yaw_s;                   /* the beast's heading's low-pass (0.10 s) */
    float yaw_dps;                 /* the view's turn at most (150 deg/s; 0 = no cap) */
    float jump_rad;                /* a heading step beyond this in one frame re-latches (2.0) */
    float max_dt;                  /* a frame's dt at most (0.1 s; a longer gap re-latches) */
} somvr_rd_cfg;
void somvr_rd_cfg_default(somvr_rd_cfg *c);

/* the beast's kind from its character record's name ('Warg_Brown' a caragor, 'Troll_Cave' a graug); -1 = neither */
int somvr_rd_kind(const char *rec);

/* ---- the yaw follower ---- */
typedef struct {
    int have;
    float raw_last;   /* the raw heading last frame (rad) */
    float u;          /* the heading unwrapped */
    float x, v;       /* the low-passed heading and its rate */
    float view;       /* the view's own heading (chases x at most yaw_dps) */
    uint64_t latches, capped;
} somvr_rd_yaw;
void somvr_rd_yaw_init(somvr_rd_yaw *y);
/* one frame: the beast's heading (rad), dt (s) -> the turn to add to the view this frame (rad, + = right as the game's
 * heading); *rate_dps = its rate */
float somvr_rd_yaw_step(somvr_rd_yaw *y, const somvr_rd_cfg *c, float heading, float dt, float *rate_dps);

/* ---- the seat ---- */
typedef struct {
    int have, kind;
    float off[3];       /* right, up, ahead (cm) */
    uint64_t samples, rejected;
} somvr_rd_seat;
void somvr_rd_seat_init(somvr_rd_seat *s, const somvr_rd_cfg *c, int kind);
/* one frame: the beast's root (cm), its heading (rad), Talion's head node (NULL = none this frame), dt -> the seat
 * point (cm, the eyes before the vertical filter) */
void somvr_rd_seat_step(somvr_rd_seat *s, const somvr_rd_cfg *c, const float beast[3], float heading, const float *head,
                        float dt, float out[3]);
/* a world point -> the beast's frame (right, up, ahead) and back */
void somvr_rd_to_beast(const float p[3], const float beast[3], float heading, float o[3]);
void somvr_rd_from_beast(const float l[3], const float beast[3], float heading, float o[3]);

/* ---- the mount (the game's offer on 123 at a stunned caragor) ---- */
/* the left palm from the eyes (dx, dz, cm, world) and the head's heading -> 1 = reached out: 30 cm or more across the
 * ground from the eyes and at most 75 deg off the view's heading (*cm, *deg: the measure) */
int somvr_rd_reached(float dx, float dz, float heading, float *cm, float *deg);

/* ---- the domination's struggle (build 27; re-xr-pipeline.md 109) ----
 * After the mount's grab the game runs a struggle on Talion (PlayerCombat+0x8d8/+0x8e0, an ICombatStruggle): its class
 * says what it counts - CStruggleHold a command held (OnCommandOn/Off; the press must come after the struggle began:
 * its delegate is registered then, a hold carried over from the grab is never seen), CStrugglePressCount a command
 * pressed N times, CStruggleQuickTime one press in a window, CStruggleAim / CStruggleInputDirection a stick. The VR
 * answer: the grip that took the beast holds on to it - the left hand kept closed (or B held) is the struggle's own
 * command held (Hold) or pressed anew at each fresh close (PressCount, QuickTime); the first SOMVR_RD_STRUG_GAP_S of a
 * new struggle the command is let go, so its press is a fresh one. The caragor's domination is an aim (boot
 * 20261030-032747: '12CStruggleAim' - a cursor kept on a moving target by the pad's stick for its 5 s, its UI not in
 * the eyes): held on for win_s without letting go = won (the caller then completes it as the game's own does). */
enum { SOMVR_RDS_NONE = 0, SOMVR_RDS_HOLD, SOMVR_RDS_COUNT, SOMVR_RDS_QT, SOMVR_RDS_AIM, SOMVR_RDS_DIR, SOMVR_RDS_N };
extern const char *const somvr_rd_strug_names[SOMVR_RDS_N];
#define SOMVR_RD_STRUG_GAP_S 0.1f
#define SOMVR_RD_STRUG_PULSE_S 0.08f
/* the struggle's class from its RTTI name ('13CStruggleHold', ...); SOMVR_RDS_NONE = not a struggle we know */
int somvr_rd_strug_kind(const char *rtti);
typedef struct {
    int kind, was;
    float t, pulse;   /* since the struggle began; a press's time left (PressCount, QuickTime) */
    float win_s;      /* an aim or a stick direction: held on this long = won (0 = never) */
    float held;       /* held on without letting go (s) */
    int won;
    uint64_t presses; /* fresh presses sent */
} somvr_rd_strug;
void somvr_rd_strug_init(somvr_rd_strug *s, int kind, float win_s);
/* one input tick: the grip holds on (the left hand closed or B held), dt -> 1 = the struggle's command on now (an aim:
 * never; s->won once held on win_s) */
int somvr_rd_strug_step(somvr_rd_strug *s, int hold, float dt);

/* ---- the buttons ---- */
typedef struct {
    float trig_on, trig_off; /* the right trigger's bite (0.6 / 0.4) */
} somvr_rd_btn_cfg;
enum { SOMVR_RDB_JUMP = 1u, SOMVR_RDB_BITE = 2u, SOMVR_RDB_OFF = 4u, SOMVR_RDB_CHARGE = 8u };
typedef struct {
    int trig_dn, charge, armed;
    uint32_t last;
} somvr_rd_btn;
void somvr_rd_btn_init(somvr_rd_btn *b);
/* one input tick: A, B held, the right trigger (0..1, NaN = none), the left stick's click held, the stick's length;
 * armed = every button seen up since the seat began (a press carried over from on foot fires nothing) -> SOMVR_RDB_*
 * held now */
uint32_t somvr_rd_btn_step(somvr_rd_btn *b, const somvr_rd_btn_cfg *c, int a, int bb, float rt, int click, float stick);

/* ---- the beast's speed (build 27c, som-b27-ride3; re-xr-pipeline.md 122) ----
 * The ground speed of the beast's root: the path its samples ran (horizontal) over the newest SOMVR_RD_SPD_WIN_S of
 * them, so a 20 Hz headless frame and a 90 Hz headset frame give the same number. A gap longer than SOMVR_RD_SPD_GAP_S
 * (a window, a load) or a step beyond SOMVR_RD_SPD_JUMP_CM (a teleport, a checkpoint) starts again (no speed until a
 * window is full again). top, dist_m and time_s count from somvr_rd_speed_mark (a run, a ride). */
#define SOMVR_RD_SPD_N 256
#define SOMVR_RD_SPD_WIN_S 1.0
#define SOMVR_RD_SPD_GAP_S 0.5
#define SOMVR_RD_SPD_JUMP_CM 2500.0f
typedef struct {
    int n, head;
    double t[SOMVR_RD_SPD_N], cum[SOMVR_RD_SPD_N]; /* the sample's time; the path (cm) up to it since the reset */
    float x[SOMVR_RD_SPD_N], z[SOMVR_RD_SPD_N];
    float mps;            /* the newest full window's speed (m/s; NAN while none) */
    float top;            /* the best window since the mark (m/s) */
    double dist_m, time_s; /* the path and the time measured since the mark (gaps not counted) */
    uint64_t resets;
} somvr_rd_speed;
void somvr_rd_speed_init(somvr_rd_speed *s);
void somvr_rd_speed_mark(somvr_rd_speed *s); /* top, dist_m, time_s from now on */
/* one sample: the beast's root (cm; [0] and [2] the ground), its time (s) -> the speed now (m/s, NAN = none yet) */
float somvr_rd_speed_step(somvr_rd_speed *s, const float pos[3], double t);

#endif
