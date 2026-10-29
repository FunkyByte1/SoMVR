/* somvr_stealth.h - the VR stealth kill's pure part (SOMVR_STEALTH_KILL; docs/re-xr-pipeline.md 67).
 *
 * Behind an unaware orc (its back arc; the game's own awareness, read from its NPC: the AIIsAware predicate's function
 * FUN_0229bbc0, re-xr-pipeline.md 67.1), crouched (the game's stealth crouch) or slow, the player's blade that enters the
 * orc's back or neck with a committed stab or cut kills it at once and silently. Everything else is the ordinary hit.
 * The game side is somvr_stealth_game.c; this file judges and keeps the cue's state (no game reads). */
#ifndef SOMVR_STEALTH_H
#define SOMVR_STEALTH_H
#include <stdint.h>

enum { SOMVR_STL_STANCE_CROUCH = 0, SOMVR_STL_STANCE_SLOW = 1, SOMVR_STL_STANCE_EITHER = 2 };

typedef struct {
    float behind_deg;   /* the player within the orc's back arc: |his angle off its facing| >= this (110 = 70 deg either side) */
    float back_deg;     /* the contact on the orc's back half: |the contact's angle off its facing| >= this (90) */
    float slow_cms;     /* "slow": the player's ground speed under this (90 cm/s) */
    int stance;         /* SOMVR_STL_STANCE_*: crouched, slow, or either (either) */
    float low_cm;       /* the contact at least this high over the orc's root (its pelvis: combat's capsule is the root
                         * +-90 cm): its back and neck, not its legs (-10) */
    float high_cm;      /* ... and at most this (100: over an infantry's head is air) */
    float stab_mps;     /* a stab below the hit rule still counts: along the blade at least this fast (0.8 m/s) */
    float stab_cos;     /* ... the motion this close to the blade's axis (0.7) */
    float near_cm;      /* the cue: the player within this of the orc (250 cm) */
    /* build 21 (SOMVR_STEALTH_KILL_THRUST; docs/re-xr-pipeline.md 84): the kill is the player's own thrust */
    int thrust;          /* 1: the rule below; 0: build 20c's (a hit by combat's rule or a stab on the first contact) */
    float thrust_mps;    /* a thrust: the blade's point driven along the blade at least this fast (1.5 m/s) */
    float thrust_cos;    /* ... the motion within acos(this) of the blade's axis, the point leading (0.866 = 30 deg) */
    float depth_cm;      /* ... and the point at least this deep in the body (15 cm along the blade) */
    float grab_depth_cm; /* held from behind (the left hand): a point-first stab this deep kills (10 cm) */
    float grab_mps;      /* ... at least this fast along the blade (0.5 m/s) */
    float side_deg;      /* the thrust's way in: its angle off the orc's facing at least this (60: back, neck or side) */
    float grace_s;       /* a touch alerts the orc this long after it unless a kill comes first (0.3 s) */
    /* build 22 (SOMVR_STEALTH_KILL_CARRIED; docs/re-xr-pipeline.md 93.4): a blade the walk carried into its body is no
     * touch - only the hand's own motion into the body makes one */
    int carried;         /* 1: the rule; 0: build 21's (any blade in its body that is no kill is a touch) */
    float own_cm;        /* ... the hand's own push into the body under this since the blade went in = carried (4 cm) */
} somvr_stl_cfg;

typedef struct {
    int aware;            /* the game's: 1 aware (alert, its senses filling, or it fights Talion), 0 unaware, -1 unknown */
    int crouched;         /* the game's stealth crouch held (or read back) */
    float speed_cms;      /* the player's ground speed */
    float player_off_deg; /* the player's angle off the orc's facing, from its root (0 = in front, 180 = straight behind) */
    float dist_cm;        /* the player to the orc (the ground plane) */
    int dead;             /* the orc is dead already */
} somvr_stl_who;

typedef struct {
    float contact_off_deg; /* the contact's angle off the orc's facing */
    float contact_up_cm;   /* the contact's height over the orc's root (its pelvis) */
    int tier;              /* the hit rule's tier: 0 none (below it), 1 light, 2 heavy */
    float axis_mps;        /* the blade's speed along itself */
    float cos_axis;        /* the cosine of the motion against the blade */
    /* build 21 (cfg.thrust): the body, not combat's hit capsule */
    float depth_cm;        /* the blade in the body: cm from where it went in to the point (< 0 or NAN: not in it) */
    int neck;              /* it went in at the neck */
    int tier_cut;          /* SOMVR_COMBAT_SWING=stroke's own tier (2 = a committed full-speed cut) */
    int held;              /* held from behind by the left hand (SOMVR_VERBS' grab) */
    int carried;           /* build 22: the hand's own push into the body since it went in under cfg.own_cm (the walk
                            * carried it in: somvr_stl_own_push) */
} somvr_stl_hit;

enum {
    SOMVR_STL_KILL = 0,   /* the stealth kill */
    SOMVR_STL_DEAD,       /* already dead */
    SOMVR_STL_UNKNOWN,    /* the awareness could not be read: the ordinary hit */
    SOMVR_STL_AWARE,      /* it has noticed Talion: the ordinary hit (a normal fight) */
    SOMVR_STL_FRONT,      /* the player is not behind it */
    SOMVR_STL_STANCE,     /* neither crouched nor slow (as the stance asks) */
    SOMVR_STL_SIDE,       /* the contact is on its front half */
    SOMVR_STL_LOW,        /* the contact is below its back (the legs) or over its head */
    SOMVR_STL_WEAK,       /* neither the hit rule nor a stab: a touch */
    SOMVR_STL_TOUCH,      /* build 21: in the body, but no thrust (a touch, a glance, a wiggle, a slow push): no damage;
                           * the orc is alerted after cfg.grace_s unless a kill comes first */
    SOMVR_STL_AIR,        /* build 21: combat's hit capsule, not the body: nothing */
    SOMVR_STL_CARRIED,    /* build 22 (cfg.carried): in the body, carried in by the walk, the hand not pushing it in: no
                           * damage, no alert (a touch is the hand's own motion) */
    SOMVR_STL_N
};

void somvr_stl_defaults(somvr_stl_cfg *c);
const char *somvr_stl_why(int verdict);
const char *somvr_stl_stance_name(int stance);
int somvr_stl_stance_of(const char *word); /* -1 = not a stance word */

/* the angle (deg, -180..180) of point p off the facing yaw (sv_heading: atan2(x, z)) of an orc at o */
float somvr_stl_off_deg(const float p[3], const float o[3], float yaw);

/* the orc as a stealth kill's target now (no contact): KILL = possible (the cue), else why not (DEAD..STANCE) */
int somvr_stl_possible(const somvr_stl_cfg *c, const somvr_stl_who *w);
/* the contact: KILL or why the ordinary hit */
int somvr_stl_judge(const somvr_stl_cfg *c, const somvr_stl_who *w, const somvr_stl_hit *h);
/* build 20c: the tier a sword's contact is judged by here (and by the verbs): build 16's rule's (tier16), or where
 * that sees nothing SOMVR_COMBAT_SWING=stroke's (tier_swing) - a contact the ordinary hit would deal is never a
 * touch to the stealth kill (docs/re-xr-pipeline.md 81) */
int somvr_stl_tier(int tier16, int tier_swing);

/* build 21: what killed (a KILL verdict): build 20c's hit or stab, the thrust, the neck cut, the stab into a held orc */
enum { SOMVR_STL_BY_HIT = 0, SOMVR_STL_BY_THRUST, SOMVR_STL_BY_NECK, SOMVR_STL_BY_GRAB };
int somvr_stl_kill_by(const somvr_stl_cfg *c, const somvr_stl_hit *h);
const char *somvr_stl_by_name(int by);

/* build 21: the blade (base -> tip, cm) against the body - a chain of n nodes (the pelvis, torso, neck, head) with a
 * radius per link (n - 1): the length of blade from the first point of it inside the body (from the base) to the tip,
 * -1 when no point is inside. entry = that point, *link = the link it went in at (either may be NULL) */
float somvr_stl_body_depth(const float base[3], const float tip[3], const float (*node)[3], const float *r, int n,
                           float entry[3], int *link);

/* build 22: the depth the hand's own motion added this frame (cm, < 0 drawn out): the body depth of the blade now less
 * that of the blade where the hand alone would have left it - moved back by its own velocity (vb, vt: the base's and
 * the tip's tracking-space velocity, cm/s: the walk and the turns are not in it) over dt s. Outside = 0 deep */
float somvr_stl_own_push(const float base[3], const float tip[3], const float vb[3], const float vt[3], float dt,
                         const float (*node)[3], const float *r, int n);

/* build 21 (SOMVR_STEALTH_KILL_STICK): the blade stays in the wound. Pinned at the wound E along the axis a (unit, into
 * the body) it took in; the hand's grip G slides it in and out along that axis (the guard stays guard_cm out of the
 * wound); sideways the hand leaves it (the offset is shown: the blade lags the hand) and past tear_cm it tears free;
 * drawn back past the wound it comes out. E follows the body (the caller moves it) */
typedef struct {
    float tear_cm;  /* 25 */
    float guard_cm; /* 12 */
} somvr_stl_stick_cfg;
typedef struct {
    int on;
    float E[3], a[3], len; /* the wound, the axis, the grip to the point */
    float depth, peak;     /* the point's depth now, the deepest */
    float off;             /* the hand's grip off the pinned grip (cm) */
} somvr_stl_stick;
enum { SOMVR_STL_STK_OFF = 0, SOMVR_STL_STK_HOLD, SOMVR_STL_STK_OUT, SOMVR_STL_STK_TEAR };
void somvr_stl_stick_start(somvr_stl_stick *s, const float E[3], const float a[3], float len);
/* one frame: E the wound now, G the hand's grip; Gs = the pinned grip (HOLD); OUT / TEAR end it (s->on = 0) */
int somvr_stl_stick_step(somvr_stl_stick *s, const somvr_stl_stick_cfg *c, const float E[3], const float G[3], float Gs[3]);

/* the cue: one soft tick when an orc becomes a possible stealth kill (the nearest one), again only after it was not
 * possible for rearm_s or another orc took its place; returns 1 = tick now */
typedef struct {
    uintptr_t orc;      /* the orc the last tick was for */
    double off_t;       /* when it stopped being possible (0 = possible now) */
    int on;             /* possible at the last step */
    uint64_t ticks;
} somvr_stl_cue;
int somvr_stl_cue_step(somvr_stl_cue *q, uintptr_t orc, int possible, double t, double rearm_s);

/* the stand-off (SOMVR_STEALTH_KILL_STANDOFF_CM; docs/re-xr-pipeline.md 72): crouched in an unaware orc's back half,
 * Talion's move d (cm, this tick, the ground plane) from p is kept outside a circle of radius r around the orc at c -
 * the game notices him inside ~108 cm root to root (72.1). Only the part toward the orc is cut, eased over the last
 * ~20 cm (the step toward the edge at most room x (1 - e^(-dt x ease_hz)): a smooth stop at any frame rate); sideways
 * and back are free; never a push (already inside: no step further in). out = the correction to add to d (0 if none);
 * returns 1 when it cut something */
int somvr_stl_standoff(const float p[3], const float c[3], float r, float dt, float ease_hz, const float d[3],
                       float out[3]);
/* build 19b (SOMVR_STEALTH_KILL_STANDOFF_DECEL; docs 72.6): the same circle with a brake instead of the ease: the step
 * toward the edge at most sqrt(2 x decel x room) x dt (decel cm/s^2) and never past the edge - a steady deceleration
 * that ends at rest on the edge (a 150 cm/s walk starts braking 45 cm out at 250 cm/s^2 and stops 0.6 s later; a slow
 * walk only at the very end). Sideways, back, inside: as somvr_stl_standoff. Returns 1 when it cut something */
int somvr_stl_standoff_brake(const float p[3], const float c[3], float r, float dt, float decel, const float d[3],
                             float out[3]);
#endif
