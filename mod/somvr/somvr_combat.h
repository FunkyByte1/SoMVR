/* somvr_combat.h - the first VR combat module (docs/design-vr.md section 3 "assisted physical", docs/re-combat-vr.md):
 * the blade source, the swept blade against the orcs' capsules, swing speed and hit tiers, the per-orc re-hit cooldown
 * and the hit charges, and the "swing toward an orc" test for the AI stimulus. Pure: no globals, no game calls (somvr.c
 * owns the game side: orc enumeration, Combat_DealHit, the stimulus, hit-stop). Tested by tests/test_combat.c.
 *
 * Units: game space (cm, left-handed, +Y up, heading + = turn right: R(yaw)(0,0,1) = (sin yaw, 0, cos yaw)), velocities
 * cm/s, times s (CLOCK_MONOTONIC); the speed thresholds are m/s like the design's.
 */
#ifndef SOMVR_COMBAT_H
#define SOMVR_COMBAT_H

#include <stdint.h>

/* one blade per frame from a provider (SOMVR_BLADE): the sword's segment and its tip velocity */
typedef struct somvr_blade {
    int valid;
    float base[3], tip[3]; /* cm, game space: base = 8 cm behind the grip, tip ~90 cm further (design 3, RT) */
    float vel_tip[3];      /* cm/s: the provider's velocity (controllers: v + w x r from the runtime, never a world
                            * finite difference, so walking or turning with a still sword is no slash: re-xr-input 4.1) */
    double t;              /* s */
    float flat_n[3];       /* the blade's flat facing (unit, world; 0 0 0 = unknown): the grip's +x (the knuckles' side
                            * of the hand; the sword's guard and edges run along the grip's +y, build 15b's pictures) */
    float vel_base[3];     /* cm/s: the base's velocity the same way (build 20; 0 0 0 = unknown: the hand's part of the
                            * swing, the contact point's speed along the blade) */
} somvr_blade;

enum { SC_TIER_NONE = 0, SC_TIER_LIGHT = 1, SC_TIER_HEAVY = 2, SC_TIER_STAB = 3, SC_TIER_N = 4 };

typedef struct {
    float blade_r;             /* cm: the sword's radius (3) */
    float slash_mps, slash_cm; /* a slash: mean tip speed >= 3.0 m/s over the window and >= 15 cm since crossing it */
    float heavy_mps, heavy_cm; /* heavy: >= 6 m/s and >= 40 cm; light = a slash below that */
    float stab_mps, stab_cos;  /* a stab: >= 1.8 m/s along the blade (mean over the window), cos(v, axis) >= 0.8 */
    float window_s;            /* the mean's window: 80 ms */
    float end_frac;            /* the swing ends when the mean drops below end_frac x slash_mps (0.75) */
    float cool_s;              /* per-orc re-hit cooldown 0.25 s (0.12 in a flurry: not built) */
    int charges;               /* 3 hit charges, one back every charge_s (0.25 s); no charge = a glancing tick */
    float charge_s;
    float stim_cm, stim_cos;   /* the AI stimulus: the swing starts within 150 cm of the orc, heading at it */
    int steps;                 /* sub-steps of the frame's sweep (4) */
    float cut_cm;              /* SOMVR_COMBAT_CLASS=cut: a committed cut = a slash (slash_mps) of >= cut_cm since the
                                * swing began (50), the edge leading (below) */
    float edge_max_deg;        /* ... the tip's motion across the blade at most this far off the edge's plane (60; 90 or
                                * no flat known = any) */
    /* SOMVR_COMBAT_SWING=stroke (build 20, docs/re-xr-pipeline.md 79): a cut needs a real swing, Blade & Sorcery's way -
     * the blade's speed where it meets the orc and its travel in one direction into it; wiggles do not cut */
    int stroke;                /* 1: sc_tier_swing's rule; the drop of jumps below (0: build 16's travel since the
                                * crossing, which a flail kept adding to: the user's build 19b session) */
    float rev_cos;             /* a stroke ends where the tip turns back: its velocity this far off the stroke's
                                * direction (cos; -0.3 = past ~107 deg) */
    float cut_mps;             /* a cut's floor: the contact point's speed (12 m/s; build 19b's session: the flail's
                                * contacts 5.9-9.1 m/s at the tip, its real cuts 19-37 at the contact point) */
    float full_mps;            /* a cut's full damage from this speed (20); cut_min_k of it at cut_mps */
    float cut_min_k;           /* 0.6 */
    float tip_max_mps;         /* a sample faster than this at the tip or the base is a tracking jump, not a swing:
                                * dropped (60; 0 = none; the session's 132 m/s "stab") */
} sc_cfg;

void sc_cfg_default(sc_cfg *c);

/* ---- geometry ---------------------------------------------------------------------------------------------------- */

/* closest points of the segments p0-p1 and q0-q1 (Ericson 5.1.9): the distance, *s / *t their parameters (0..1) */
float sc_seg_dist(const float p0[3], const float p1[3], const float q0[3], const float q1[3], float *s, float *t);

typedef struct {
    float u;        /* the sub-step of the frame's sweep where the contact starts: (k / steps), 1 = this frame's pose */
    float along;    /* where on the blade: 0 base .. 1 tip */
    float dist;     /* blade axis to capsule axis, cm (<= blade_r + cap_r) */
    float point[3]; /* the contact point on the capsule's surface (cm), what Combat_DealHit gets as hitPos */
} sc_contact;

/* The blade swept from prev to cur (linear in base and tip, `steps` sub-steps; prev NULL or invalid = cur alone)
 * against the capsule c0-c1 of radius cap_r. 1 = contact at the first touching sub-step. */
int sc_sweep_capsule(const somvr_blade *prev, const somvr_blade *cur, float blade_r, const float c0[3], const float c1[3],
                     float cap_r, int steps, sc_contact *out);

/* The swing heads at the capsule: the tip within c->stim_cm of its surface and the tip's horizontal velocity within
 * acos(c->stim_cos) of the horizontal direction to its axis. *dist (cm to the surface) and *cosv are always set. */
int sc_heading_at(const sc_cfg *c, const somvr_blade *b, const float c0[3], const float c1[3], float cap_r, float *dist,
                  float *cosv);

/* ---- swing speed ------------------------------------------------------------------------------------------------- */

#define SC_RING 64
typedef struct {
    int n, head;
    struct { double t; float speed, axis, base; } s[SC_RING]; /* cm/s: |v_tip|, v_tip along the blade (base -> tip),
                                                                * |v_base| */
    int above;        /* the swing: since the mean crossed slash_mps, until it drops below end_frac x slash_mps */
    float travel;     /* cm the tip travelled since the crossing */
    float mean_mps;   /* the time-weighted mean of |v_tip| over the window, m/s */
    float axis_mps;   /* the same for the speed along the blade */
    float cos_axis;   /* cos(v_tip, blade axis) of the last sample (0 when either is ~0) */
    double last_t;
    /* build 20 (SOMVR_COMBAT_SWING=stroke): the stroke - the tip's motion in one direction, from where it began (the
     * swing's start, a turn back of the tip, or a rest) - measured on every sample whatever c->stroke */
    float base_mps;   /* the time-weighted mean of |v_base| over the window, m/s */
    float reach;      /* cm: the farthest the tip got from where the stroke began (its velocity integrated: the
                       * tracking-space motion, so walking and turning add nothing) */
    float stroke_cm;  /* cm: the tip's path in the stroke */
    float disp[3];    /* the stroke's displacement so far (cm) */
    float dir[3];     /* the stroke's direction: the tip's velocity eased over SC_STROKE_TAU_S */
    int have_dir;
    float rest_s;     /* the tip slower than SC_STROKE_MIN_CMS this long: a rest ends the stroke */
    double stroke_t0; /* when the stroke began (its first sample's t) */
    uint64_t strokes, drops; /* strokes begun; samples dropped as jumps (c->tip_max_mps) */
} sc_speed;
#define SC_STROKE_MIN_CMS 100.0f /* a tip slower than 1 m/s neither steers nor ends a stroke */
#define SC_STROKE_TAU_S 0.025f   /* the stroke direction's ease */
#define SC_STROKE_REST_S 0.08f   /* this long under SC_STROKE_MIN_CMS: a rest */

void sc_speed_reset(sc_speed *s);
/* one valid blade sample (times must increase; an older or equal t is ignored): 1 = the mean crossed slash_mps upward
 * on this sample (a swing starts: travel restarts at 0), -1 = the swing ended on it, 0 = neither */
int sc_speed_push(sc_speed *s, const sc_cfg *c, const somvr_blade *b);
/* the tier of a contact now: stab (along the blade), heavy, light, or none (a glancing touch) */
int sc_tier(const sc_cfg *c, const sc_speed *s);
/* the cut's angle off the edge: the tip's velocity across the blade (its part along the blade removed) against the
 * blade's flat: 0 = the edge leads, 90 = the flat slaps; NAN = no flat known or the tip still across the blade */
float sc_edge_deg(const somvr_blade *b);
/* SOMVR_COMBAT_CLASS=cut (build 16): the tier by the cut, not the speed: a stab as sc_tier; a slash that has travelled
 * cut_cm with the edge leading (edge_deg <= edge_max_deg, or NAN) = heavy ("a committed cut"); any other slash = light */
int sc_tier_cut(const sc_cfg *c, const sc_speed *s, float edge_deg);
/* build 20 (SOMVR_COMBAT_SWING=stroke): the contact point's speed (m/s) at `along` (0 base .. 1 tip) of the blade: the
 * window means of the base and the tip, interpolated (a rigid blade's velocity is linear along it) */
float sc_contact_mps(const sc_speed *s, float along);
/* the tier by a real swing (Blade & Sorcery's way): a stab as sc_tier; no swing (below slash_mps) none; a cut (heavy)
 * = the stroke reached cut_cm, the contact point moves at cut_mps or more and the edge leads (as sc_tier_cut); a light
 * hit = the stroke reached slash_cm and the contact point moves at slash_mps or more; else none (a glancing touch). A
 * wiggle - a flail of short strokes, or a contact early in a stroke while the blade is still slow - is no cut. */
int sc_tier_swing(const sc_cfg *c, const sc_speed *s, float edge_deg, float along);
/* the blade came into the orc during this stroke: its touch (sc_touch_age: how long it has touched) began at or after
 * the stroke's start - a blade wiggled inside a body touches from before each new stroke; stab tiers pass */
int sc_stroke_entered(const sc_speed *s, float touch_age, double now);
/* the cut's damage factor by the contact point's speed: cut_min_k at cut_mps rising linearly to 1 at full_mps (1 when
 * full_mps <= cut_mps; NaN = cut_min_k) */
float sc_cut_k(const sc_cfg *c, float contact_mps);
/* SOMVR_FIGHT_SPACE_CM (build 16): an orc's next position xf (cm) kept at least min(space, the distance it had at
 * was) from Talion's root, horizontally (y untouched; never pushed away, only stopped from coming nearer); 1 = changed,
 * *d_want / *d_got its distance asked and given */
int sc_space_hold(const float root[3], const float was[3], float xf[3], float space, float *d_want, float *d_got);
/* the same about two places at once (build 16 round 2: Talion's root and the player's head; head NULL = the root alone):
 * the new position keeps at least min(space, the distance it had) from each, horizontally; when it would not, the
 * nearest of the two radial stops that keeps both is taken, else it stays where it was. 1 = changed; *d_want / *d_got
 * = the nearer of its two distances asked and given */
int sc_space_hold2(const float root[3], const float *head, const float was[3], float xf[3], float space, float *d_want,
                   float *d_got);
/* SOMVR_COMBAT_STAGGER_CM (build 17): an orc's step during its reel (its reaction to our hit or parry) from was to xf
 * (cm), kept within cap of anchor (where the hit found it), horizontally: the step's part away from the anchor passes
 * 1:1 up to half the cap, then eased by (cap - d) / (cap / 2) (a reel that slows to a stop just under the cap, never
 * dead), the sideways part as it is, never past max(cap, the distance it had); a step that takes it nearer to talion
 * (Talion's root; NULL = none) passes as it is (FIGHT_SPACE's rule holds it). 1 = changed, *cut = cm taken off */
int sc_reel_cap(const float anchor[3], const float *talion, const float was[3], float xf[3], float cap, float *cut);
/* SOMVR_FIGHT_BODY_CM (build 19b, docs/re-xr-pipeline.md 74): how far (horizontally, cm) an orc's root has to stand
 * from the eyes for each of its n body points (bone[i], world cm, read with the root at root) to be at least body cm
 * from the eyes (3D), the body carried rigidly with the root straight away from the eyes. 0 = none needed (every point
 * out already); else the root's distance now + the largest point's need (at most max_cm more); *worst = that point's
 * index (-1 none) */
float sc_body_need(const float eye[3], const float root[3], const float (*bone)[3], int n, float body, float max_cm,
                   int *worst);
/* the hook's step for it: the orc's next position xf (cm) kept at least need from the eyes, horizontally (y untouched),
 * eased out - at most push cm farther than the larger of where it was and where its own step put it, never nearer than
 * its own step. 1 = changed, *pushed = cm added to its own step's distance */
int sc_body_push(const float eye[3], const float was[3], float xf[3], float need, float push, float *pushed);
const char *sc_tier_name(int tier);
/* build 17 (docs/re-xr-pipeline.md 59): what a landed hit was, for its feedback (the bite, the sound, the sparks): a cut
 * (the heavy tier: SOMVR_COMBAT_CLASS=cut's committed cut, =speed's fast long one), a stab, a flat slap (light with the
 * flat leading: edge_deg over edge_max_deg) or a tap (any other light hit: a short one, the edge's angle unknown) */
enum { SC_KIND_NONE = 0, SC_KIND_CUT = 1, SC_KIND_SLAP = 2, SC_KIND_TAP = 3, SC_KIND_STAB = 4, SC_KIND_N = 5 };
int sc_hit_kind(int tier, float edge_deg, float edge_max_deg);
const char *sc_kind_name(int kind); /* "cut", "slap", "tap", "stab", "none" */
/* the bite: a cut that landed - not one dealt as a glance off a raised shield, not one the orc blocked */
int sc_kind_bites(int kind, int glance, int blocked);

/* ---- re-hit cooldown and hit charges ----------------------------------------------------------------------------- */

#define SC_COOL_N 32
typedef struct { uintptr_t key[SC_COOL_N]; double t[SC_COOL_N]; } sc_cool; /* key = the orc's Combat*, 0 = free */

void sc_cool_reset(sc_cool *c);
int sc_cool_ready(const sc_cool *c, uintptr_t key, double now, float cool_s); /* 1 = no mark on key in the last cool_s */
void sc_cool_mark(sc_cool *c, uintptr_t key, double now); /* key's slot, else a free/the oldest one */

typedef struct { float n; double t; } sc_charges; /* n charges (fractional while one grows back) as of time t */

void sc_charges_init(sc_charges *ch, const sc_cfg *c, double now);
float sc_charges_now(const sc_charges *ch, const sc_cfg *c, double now);
int sc_charges_take(sc_charges *ch, const sc_cfg *c, double now); /* 1 = a whole charge was there and is used */

/* ---- the blade's touch on an orc (the glancing feel) ------------------------------------------------------------- */
/* A blade resting in an orc's capsule touches it every frame (20-90 contacts a second). The touch is one: a glancing
 * tick when it starts (no contact with that orc in the last gap_s) and at most a weak rub while it lasts (one every
 * rub_s, only while the tip moves at rub_mps or more: a blade scraping along the orc, not one held still in it).
 * Boot 1 of build 7 (boot-check-20261024-031532): 59 glance ticks, about 4 a second while the blade rested (the
 * re-hit cooldown marked by each glance gave the rhythm). */
#define SC_TOUCH_N 16
#define SC_TOUCH_GAP_S 0.12f /* a gap this long ends a touch (2 frames at 20 Hz, 10 at 90 Hz) */
#define SC_TOUCH_RUB_S 0.12f /* a rub at most this often */
#define SC_TOUCH_RUB_MPS 0.5f
typedef struct { uintptr_t key[SC_TOUCH_N]; double t0[SC_TOUCH_N], last[SC_TOUCH_N], rub[SC_TOUCH_N]; } sc_touch;
enum { SC_TOUCH_ON = 0, SC_TOUCH_START = 1, SC_TOUCH_RUB = 2 };
void sc_touch_reset(sc_touch *tc);
/* one contact with key (an orc's Combat*, 0 = none: always START) at now, the tip's speed (m/s): START = a new touch,
 * RUB = it lasts and a rub is due (marked), ON = it lasts, nothing due. A full table forgets the oldest touch */
int sc_touch_step(sc_touch *tc, uintptr_t key, double now, float mps, float gap_s, float rub_s, float rub_mps);
/* how long key's touch has lasted at now (0 = no touch going on) */
float sc_touch_age(const sc_touch *tc, uintptr_t key, double now, float gap_s);

/* ---- blade providers --------------------------------------------------------------------------------------------- */

/* SOMVR_BLADE=fake (tests, desktop): a scripted pair of horizontal swings in front of a head facing `yaw`, in a
 * SC_FAKE_PERIOD cycle from t0: held at the right (+75 deg), a heavy swing right -> left (9 m/s at the tip) from 0.3 s,
 * held at the left, a light one back (4.5 m/s) from 1.4 s; each ramps its angular speed up and down over 50 ms. The
 * blade is level, 30 cm below the head (the shoulder), base 45 cm and tip 135 cm from that pivot. */
#define SC_FAKE_PERIOD 2.4
#define SC_FAKE_HEAVY_T0 0.3
#define SC_FAKE_LIGHT_T0 1.4
#define SC_FAKE_HEAVY_MPS 9.0f
#define SC_FAKE_LIGHT_MPS 4.5f
#define SC_FAKE_HALF_DEG 75.0f
#define SC_FAKE_RAMP_S 0.05f
#define SC_FAKE_DROP_CM 30.0f
#define SC_FAKE_BASE_CM 45.0f
#define SC_FAKE_TIP_CM 135.0f
void somvr_blade_fake(double t, double t0, const float head[3], float yaw, somvr_blade *out);
/* the fake's swing angle (rad, + = right of forward) and its rate at time t - t0 (tests) */
float somvr_blade_fake_angle(double dt, float *rate);

/* SOMVR_BLADE=controller (docs/re-xr-input.md 4.1-4.2): the sword hand's grip, located at the frame's eye views'
 * display time (somvr_hands.c), in mirrored tracking space (game convention: cm, left-handed; the tracking origin's
 * frame, before the rig), and the frame's rig, the same transform the eyes got. */
typedef struct {
    int valid;              /* orientation and position */
    float pos[3], quat[4];  /* m(p) cm; m(q) x y z w */
    int vel_valid;          /* the runtime's linear and angular velocity, both valid */
    float vel[3], avel[3];  /* m(v) cm/s; (-wx, -wy, wz) rad/s */
    double xr_t;            /* the locate's XrTime, s */
} sc_grip;
/* world = base + s R(Y(yaw)) (p - p0), yaw = psi - yaw0; s = the player size (somvr_rig_scale; 0 = 1, sc_rig_scale) */
typedef struct { float base[3], p0[3], yaw, s; } sc_rigxf;
float sc_rig_scale(const sc_rigxf *rig); /* rig->s when it is a size (0.5 .. 2), else 1 */
/* the grip -> blade offset (design-vr.md 3: the blade from 8 cm behind the grip to a ~90 cm tip; RT: the sword
 * model's nodes and a shot decide the real values). The blade runs along the grip's +Z after the mirror (OpenXR's grip
 * -Z: through the curled fingers toward the thumb, the way a held sword points) */
typedef struct {
    float back_cm, len_cm;  /* the base back_cm behind the grip point, the tip len_cm ahead of it (8, 100: the sword
                             * model's sword_tip node, round 2's measure) */
    float ofs_cm[3];        /* the grip point in the grip's frame (+x right, +y up, +z the blade), cm (0 0 0) */
    float pitch_rad;        /* the blade turned up (+) or down about the grip's +x (0) */
    int vel_runtime;        /* 1: the tip's velocity v + w x r from the runtime when it gives both (else deltas);
                             * 0 (default): tracking-space deltas of the tip between two locates */
} sc_blade_cfg;
typedef struct { int have; float tip[3], vel[3]; double xr_t; float base[3], vbase[3]; } sc_blade_hist; /* the last
                                                                                * tip (and base), tracking space */
void sc_blade_cfg_default(sc_blade_cfg *c);
/* The blade in the world: base/tip = rig(grip point -/+ along the blade), t = the combat clock; the blade's lengths
 * and offset are world cm (the sword model's): in tracking space they are divided by the rig's size s, so a bigger
 * player holds the same sword (docs/re-xr-pipeline.md 35). vel_tip = s R(Y(yaw))
 * of the tip's tracking-space velocity: a finite difference of the tip in tracking space (XrTime dt, at most 0.1 s;
 * the same time again = the last velocity), or v + w x r from the runtime; never a world difference, so walking,
 * snap turns and recentring with a still sword are no slash (re-xr-input.md 4.1). An invalid grip: no blade (0) and
 * the history is dropped. Returns out->valid. */
int somvr_blade_controller(const sc_blade_cfg *c, const sc_grip *g, const sc_rigxf *rig, double t, sc_blade_hist *h,
                           somvr_blade *out);

/* ---- orc reactions to a swing (SOMVR_COMBAT_REACT; docs/re-combat-spawn.md 2) ------------------------------------ */
/* At a player attack's start, Combat_StartAttack rolls the target orc's AIBlockTable (its character combat record's
 * Combat.AIBlockTable: Entries of CombatAIBlockChance {Category, BlockChance}) against the attack's categories; on a
 * win FUN_029ac890 tells the orc's AI it is being attacked, which may make it block or dodge. For a VR swing: the same
 * roll for every orc the swing heads at (sc_heading_at), once per swing per orc, in the swing's first
 * SC_REACT_WINDOW_S. The game side (somvr.c) reads the table and makes the calls. */

/* one AIBlockTable entry: its Category record (0 = none: every attack) and BlockChance (default 0.5) */
typedef struct { uintptr_t cat; float chance; } sc_block_entry;
#define SC_BLOCK_DEFAULT 0.5f

/* the chance Combat_StartAttack rolls for an attack with categories cats[0..ncat) (re-combat-spawn.md 2.1 step 3,
 * 0x26ecd22): the first entry whose Category is none or one of cats (*ent = its index); Entries that are not records
 * (records = 0): 0.5 (*ent = -1); no entry, or none matching: -1 = no roll (*ent = -1) */
float sc_block_chance(const sc_block_entry *e, int n, int records, const uintptr_t *cats, int ncat, int *ent);
/* the game's comparison (0x26ecd5a: notify only when chance > roll; no roll = no notify) */
int sc_block_won(float chance, float roll);

/* one roll per swing per orc: the orcs this swing has rolled for, its start and its roll window */
#define SC_REACT_N 16
#define SC_REACT_WINDOW_S 0.25
typedef struct { int on, n; double t0; uintptr_t key[SC_REACT_N]; } sc_react;
void sc_react_reset(sc_react *r);
void sc_react_start(sc_react *r, double t); /* a swing starts (sc_speed_push returned 1): nobody has rolled yet */
void sc_react_end(sc_react *r);             /* the swing ended (-1): no more rolls until the next start */
int sc_react_open(const sc_react *r, double t); /* 1 = a swing is on and t is inside its window */
/* 1 = key (an orc's Combat*) rolls now and is marked; 0 = no swing, the window is over, key 0, already rolled in this
 * swing, or SC_REACT_N orcs have */
int sc_react_take(sc_react *r, uintptr_t key, double t);

/* Orcs with no AIBlockTable (P9, runtime-probes.md: AI_Defender and AI_Berserker have none, so the game never rolls
 * for them and neither does the copy above). For them the notify is our own call FUN_029ac890 with a chance per
 * archetype (the character combat record), made only while the orc is out of its own attack: CAIGoalCounterMelee's
 * relevance (vf_03, 0x29d6c23..0x29d6c7b) ignores the fact while the orc's Combat has a current attack (+0x4d0) whose
 * cancel-window bytes +0xe0/+0xe1 are both 0 (P9: the two `incoming` calls inside its attack did nothing, the one
 * outside it raised the defender's block). Still one call per swing per orc (sc_react). */
enum { SC_ARCH_DEFENDER = 0, SC_ARCH_BERSERKER = 1, SC_ARCH_OTHER = 2, SC_ARCH_N = 3 };
#define SC_REACT_DEFENDER 0.6f  /* SOMVR_COMBAT_REACT_DEFENDER */
#define SC_REACT_BERSERKER 0.4f /* SOMVR_COMBAT_REACT_BERSERKER */
#define SC_REACT_OTHER 0.5f     /* SOMVR_COMBAT_REACT_OTHER: any other record without a table (the game's default 0.5) */
typedef struct { float chance[SC_ARCH_N]; } sc_react_cfg; /* 0..1 each; 0 = never */
void sc_react_cfg_default(sc_react_cfg *c);
/* the archetype of a character combat record's name: "AI_Defender" / "AI_Berserker" (also followed by '_' and more),
 * anything else (NULL, "") other */
int sc_archetype(const char *record);
const char *sc_archetype_name(int arch);
/* the counter goal's gate (vf_03): 1 = the orc is in its own attack (an attack record, both cancel bytes 0) */
int sc_in_own_attack(uintptr_t attack, uint8_t cancel0, uint8_t cancel1);
/* the chance of the no-table path: the archetype's; -1 = no call (in its own attack, a chance <= 0 or NaN, an
 * unknown archetype). The comparison is sc_block_won's (chance > roll) */
float sc_react_direct_chance(const sc_react_cfg *c, int arch, int in_attack);

/* SOMVR_HANDS_TEST's scripted swing (`hands swing`, headless tests only): a grip moved from pose a to pose b over a
 * swing's time. Poses: x right, y up, z ahead (cm) of the head's yaw frame and a quaternion x y z w in that frame (the
 * test grips' frame, somvr_body_game.c). sc_swing_ease: the minimum-jerk timing 10u^3 - 15u^4 + 6u^5 (0 at 0, 1 at 1,
 * no speed at either end, the peak 1.875 x the mean at u = 0.5; clamped outside 0..1, NaN = 0). sc_euler_quat: deg
 * about X, then Y, then Z (q = qx qy qz: sa_xf_euler's order). sc_swing_pose: the pose at u (0..1 of the time): the
 * eased fraction k; the position's direction from the pivot slerped and its distance lerped (an arc about the pivot,
 * the shoulder); a plain line when either end is within 1 cm of the pivot or the two directions are within 1 deg of
 * opposite; the orientation slerped along the shorter way. 0 = a bad input (NaN, a zero quaternion) */
float sc_swing_ease(float u);
void sc_euler_quat(const float deg[3], float q[4]);
int sc_swing_pose(const float pa[3], const float qa[4], const float pb[3], const float qb[4], const float pivot[3],
                  float u, float pos[3], float q[4]);

#endif
