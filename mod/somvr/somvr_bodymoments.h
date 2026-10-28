/* somvr_bodymoments.h - SOMVR_BODYMOMENTS=1: first person through the body moments (docs/design-hla.md HG-2 and 2.9,
 * docs/re-xr-pipeline.md 20). The user's decision is "VR-native": the view is never dragged. When the game owns
 * Talion's body (a sync, a struggle, a knockdown, the death) the view stays where the player is and the orc and
 * Talion's body animate in front of it or around it; only states we do not know go to the window (the calm-space
 * quad), never to the game camera. Pure: no globals, no game reads, no OpenXR. Tested by tests/test_bodymoments.c;
 * somvr.c's SOMVR_BODYMOMENTS region does the reads (SOMVR_FALLBACK's detector, 6.8, plus Talion's current attack, his
 * combat state, his dead flag, the state tags and the sync record by name) and applies the result.
 *
 * Stages, once per H4 frame (stereo or flat) and once per stereo frame's eye 0:
 *   names    a database record's name -> classes (knockdown, death, hold, window, first) by glob patterns per source
 *            (the attack record, the combat state record, a state tag, the sync record)
 *   conds    the fallback's conditions + the classes -> the moment conditions (the fallback's 13, knockdown, hold,
 *            window); the dead flag is death; our own climb exempts climb and attach
 *   policy   each condition -> an action, in rising priority: first (nothing: the live anchor), hold (the anchor
 *            frozen), dim (frozen and dimmed: a knockdown), grey (frozen and greyed out: the death), window (the
 *            frame rendered flat: the layer shows it as the menu quad in the calm space)
 *   step     the actions over time: a hold starts in the frame it is seen (the body's first move never reaches the
 *            view) and ends after a short debounce (chained syncs stay one hold); the window has an enter and exit
 *            debounce and a minimum time (never more than one change per second); the dim level ramps; the release
 *            re-seats the view: within seat_cm by a smooth ease (3rd order, velocity continuous), farther by a blink
 *            (fade out, the cut on a black frame, fade in). No change while the game is paused (the blink and the
 *            ramps finish).
 *   base     (eye 0) the frame's base: the frozen point while held, the ease, the cut; the base yaw kept the player's
 *            (the game camera's own heading changes, e.g. combat and kill cameras, absorbed instead of turning the
 *            view; the player's snap steps pass)
 */
#ifndef SOMVR_BODYMOMENTS_H
#define SOMVR_BODYMOMENTS_H

#include <stdint.h>

#include "somvr_fallback.h"

/* ---- conditions: the fallback's SOMVR_FB_* (0..12), then ours ---- */
enum {
    SOMVR_BMC_KNOCKDOWN = SOMVR_FB_NCOND, /* a knockdown name (the attack record, the combat state or a tag) */
    SOMVR_BMC_HOLD,                       /* a name of the class hold (none by default: grabs, drains if the RT asks) */
    SOMVR_BMC_WINDOW,                     /* a name of the class window (none by default) */
    SOMVR_BMC_N
};
#define SOMVR_BMC_BIT(c) (1u << (c))
#define SOMVR_BMC_ALL ((1u << SOMVR_BMC_N) - 1u)
extern const char *const somvr_bm_cond_names[SOMVR_BMC_N];

/* ---- actions, in rising priority ---- */
enum { SOMVR_BMA_FIRST, SOMVR_BMA_HOLD, SOMVR_BMA_DIM, SOMVR_BMA_GREY, SOMVR_BMA_WINDOW, SOMVR_BMA_N };
extern const char *const somvr_bm_act_names[SOMVR_BMA_N];
typedef struct { uint8_t act[SOMVR_BMC_N]; } somvr_bm_policy;
/* cine, pres, mount, attach, photo, spect, forced, window: window; sync, struggle, hold: hold; knockdown: dim; death:
 * grey; scripted, climb, tag (FinalBlow, the kill cam): first */
void somvr_bm_policy_default(somvr_bm_policy *p);
/* "cond=action,..." over the defaults ("mount=first,scripted=window"; "default"/"" = the defaults); 0 = every item
 * understood, -1 = at least one was not (skipped, the rest kept) */
int somvr_bm_policy_parse(const char *spec, somvr_bm_policy *p);
/* the conditions' actions: returns the highest hold-type action (FIRST..GREY) and its condition (*hold_cond, -1 for
 * FIRST); *win = some condition asks for the window, *win_cond = the first such */
int somvr_bm_actions(const somvr_bm_policy *p, uint32_t conds, int *hold_cond, int *win, int *win_cond);

/* ---- names: "class=source:pattern,..." ---- */
enum { SOMVR_BMS_ATK = 1u, SOMVR_BMS_STATE = 2u, SOMVR_BMS_TAG = 4u, SOMVR_BMS_SYNC = 8u, SOMVR_BMS_ALL = 15u };
enum { SOMVR_BMN_KNOCKDOWN = 1u, SOMVR_BMN_DEATH = 2u, SOMVR_BMN_HOLD = 4u, SOMVR_BMN_WINDOW = 8u, SOMVR_BMN_FIRST = 16u };
#define SOMVR_BM_MAXNAMES 24
#define SOMVR_BM_PATLEN 48
typedef struct {
    int n;
    uint8_t cls[SOMVR_BM_MAXNAMES]; /* one SOMVR_BMN_* */
    uint8_t src[SOMVR_BM_MAXNAMES]; /* SOMVR_BMS_* bits */
    char pat[SOMVR_BM_MAXNAMES][SOMVR_BM_PATLEN];
} somvr_bm_names;
/* P8's death went through the attack records PC_RecoilAny_PreDeath, PC_PreDeathLoop, PC_DeathLoop (with the tags
 * PreDeath_Kneeling, PreDeath: SOMVR_FALLBACK_TAGS's death class). The knockdown names are not seen at runtime yet
 * (RT): Talion's recoil records start PC_Recoil_ (P7: PC_Recoil_Light_R2L_F, PC_Recoil_Medium_D2U_B, ...); his counters
 * that knock an orc down are PC_Bip_Sync_Cntr_Knockdown_* (P9), so the attack patterns keep to PC_Recoil*. */
#define SOMVR_BM_NAMES_DEFAULT                                                                                          \
    "knockdown=atk:PC_Recoil*Knockdown*,knockdown=atk:PC_Recoil*KnockedDown*,knockdown=atk:PC_Recoil*GetUp*,"          \
    "knockdown=atk:PC_GetUp*,knockdown=state:*Knockdown*,knockdown=state:*KnockedDown*,knockdown=tag:*Knockdown*,"      \
    "knockdown=tag:*KnockedDown*,death=atk:PC_*PreDeath*,death=atk:PC_*DeathLoop*"
/* "class=source:pattern" items, comma separated; class knockdown|death|hold|window|first, source atk|state|tag|sync|any
 * (no "source:" = any), the pattern a case-insensitive glob (* any run, ? one character); "none" = empty, "" or
 * "default" = SOMVR_BM_NAMES_DEFAULT; a leading "+" adds to the defaults. 0 = every item understood, -1 = not (the rest
 * kept); at most SOMVR_BM_MAXNAMES */
int somvr_bm_names_parse(const char *spec, somvr_bm_names *t);
/* the classes (SOMVR_BMN_* bits) of one name from a source (SOMVR_BMS_*); 0 = none */
uint32_t somvr_bm_names_class(const somvr_bm_names *t, uint32_t src, const char *name);
/* case-insensitive glob: '*' any run (also empty), '?' one character; 1 = match */
int somvr_bm_glob(const char *pat, const char *s);

/* ---- the frame's conditions ---- */
typedef struct {
    uint32_t fb;          /* somvr_fb_detect's bits (+ SOMVR_FB_BIT(SOMVR_FB_FORCED)) */
    uint32_t names;       /* SOMVR_BMN_* of this frame's names; SOMVR_BMN_FIRST only from the sync record */
    int dead;             /* Talion's server Combat+0x288 (set first thing by the death FUN_026f61f0) */
    int our_climb;        /* SOMVR_CLIMB holds: climb and attach are the player's own */
    int our_grab;         /* SOMVR_VERBS: the player's left hand holds an orc (the game's grab rides on the mount and
                           * attach states, its interrogation is a presentation): not the window, a hold (the anchor
                           * frozen) for as long as the grab */
} somvr_bm_in;
uint32_t somvr_bm_conds(const somvr_bm_in *in);

/* ---- the step and the base ---- */
typedef struct {
    float hold_exit_s;            /* a hold ends after this long without it (0.25; chained syncs stay one hold) */
    float win_in_s, win_out_s;    /* the window's debounce (0.10 / 0.40) */
    float win_hold_s;             /* minimum time in or out of the window (1.0, at least 1.0, enforced) */
    float dim, grey;              /* the knockdown's and the death's fade level, 0..1 (0.5, 0.8) */
    int red;                      /* the knockdown's dim in dark red (the layer's red ramp; 1), else black */
    float dim_in_s, grey_in_s;    /* the ramps up (0.15, 1.0: the death's 1 s grey-out) */
    float dim_out_s;              /* the ramp down from the highest level (0.35) */
    float seat_cm;                /* the release: within this an ease, farther a blink (40; 0 = always a blink) */
    float ease_s;                 /* the ease's 1/omega (0.05: 15 cm settle to 0.5 mm in 0.5 s, peak 0.27 x cm / ease_s
                                     cm/s; 3rd order from rest, no overshoot) */
    float blink_out_s, blink_black_s, blink_in_s; /* the re-seat blink (0.05, 0.02 + 2 frames, 0.07) */
    float gap_s;                  /* frames further apart re-latch (0.5: a cinematic or a load ran without H4) */
} somvr_bm_cfg;
void somvr_bm_cfg_default(somvr_bm_cfg *c);

enum { SOMVR_BMB_IDLE, SOMVR_BMB_OUT, SOMVR_BMB_BLACK, SOMVR_BMB_IN };
extern const char *const somvr_bm_blink_names[4];
enum { SOMVR_BMY_PLAYER, SOMVR_BMY_MOMENTS, SOMVR_BMY_CAMERA };
extern const char *const somvr_bm_yaw_names[3];

typedef struct {
    int started;
    double t_last;
    /* the hold-type action in force (FIRST..GREY) and the condition that set it */
    int act, act_cond;
    double low_since;             /* the raw action has been below act since (< 0: not) */
    /* the window */
    int win, win_cond;
    double win_since, win_cand;
    /* the dim / grey level */
    float level;
    int level_red;                /* the level is (or decays from) the knockdown's dim */
    /* the seat */
    int frozen;
    float F[3];                   /* the held base (cm) */
    int blink, blink_frames;
    double blink_t0;
    float blink_alpha;
    int cut_pending;              /* the next base goes live by a cut (the blink's black, a gap, after the window) */
    int ease_pending, ease;
    float e[3], ev[3], ea[3];     /* the ease's offset from the live base and its derivatives */
    float L[3], vL[3];            /* the live base last seen, its velocity (cm/s) */
    int have_L;
    double tL;
    float out[3];                 /* the last base given */
    int have_out;
    float last_seat_cm;           /* the distance of the last release */
    /* the yaw */
    int have_yaw;
    float yaw_off, yaw_last;      /* the offset added to the rig's yaw; the rig's yaw last frame */
    int32_t snaps_last;
    float yaw_absorbed;           /* radians of the game camera's turns absorbed so far (|sum|) */
    /* counts */
    uint64_t frames, holds, windows, blinks, eases, cuts, dims, greys;
} somvr_bm;

typedef struct {
    int act, act_cond;            /* the hold-type action in force this frame */
    int window, win_cond;         /* render this frame flat (the window) */
    int frozen;                   /* the base is held this frame */
    float fade;                   /* the record's fade, 0..1: max(the dim level, the blink) */
    int body_anim;                /* dim / grey: the arms to the animation, no blade hits */
    int red;                      /* the fade is the knockdown's dim, shown red (SOMVR_REC_FADE_RED) */
    int act_changed, win_changed, blink_changed;
    int release;                  /* 0, or how this frame's release re-seats: 1 ease, 2 blink, 3 cut (window, gap) */
} somvr_bm_out;

void somvr_bm_init(somvr_bm *s);
/* one H4 frame at t (s): hold_act / hold_cond / win / win_cond = somvr_bm_actions of this frame's conditions;
 * paused = the game timer is paused (no change; the blink and the ramps go on) */
void somvr_bm_step(somvr_bm *s, const somvr_bm_cfg *c, int hold_act, int hold_cond, int win, int win_cond, int paused,
                   double t, somvr_bm_out *o);
/* eye 0 of a stereo frame at t: the rig's live base (cm) and yaw (rad), its snap steps and snap_rad, the yaw mode ->
 * the base to render (pos, *yaw); *cut = 1: the base jumped this frame (a re-seat cut: not motion) */
void somvr_bm_base(somvr_bm *s, const somvr_bm_cfg *c, const float live[3], float live_yaw, int32_t snaps, float snap_rad,
                   int yaw_mode, double t, float pos[3], float *yaw, int *cut);
/* the yaw part alone (somvr_bm_base calls it; a frame on the camera base, no head node, calls it directly): the rig's
 * yaw with the game camera's own turns absorbed (PLAYER always, MOMENTS while a moment runs, CAMERA never) */
float somvr_bm_yaw(somvr_bm *s, float live_yaw, int32_t snaps, float snap_rad, int yaw_mode);
/* a recentre (the player's request): the absorbed offset goes, so the view faces the game's forward again as it does
 * without body moments (the recentre's own jump; the next frames absorb from there) */
void somvr_bm_recenter(somvr_bm *s);

/* build 20 (SOMVR_BM_GRAB_FREE_S; docs/re-xr-pipeline.md 77): the player's own grab (somvr_bm_in.our_grab) holds the
 * view only while the game holds Talion. Once the player's own locomotion has owned his ticks for free_s under such a
 * hold (the game's grab, its interrogation and its kill no longer move him), our_grab stops counting until it ends: the
 * hold lets go, the view follows him again - no state of ours can strand the player (the user's build 19b session: an
 * interrogation's end kept the view on the held point for 30 s while the stick walked Talion away). 0 = never. */
typedef struct {
    double since;   /* the player's locomotion has owned Talion since (< 0: not under the grab) */
    int latched;    /* the grab no longer holds the view (until our_grab ends) */
    uint64_t frees; /* how often */
} somvr_bm_free;
void somvr_bm_free_init(somvr_bm_free *f);
/* one H4 frame at t: grab = our_grab, loco_ours = the player's locomotion owns Talion this tick -> 1 = our_grab does not
 * count this frame */
int somvr_bm_free_step(somvr_bm_free *f, int grab, int loco_ours, double t, float free_s);

#endif
