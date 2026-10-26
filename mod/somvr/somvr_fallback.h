/* somvr_fallback.h - SOMVR_FALLBACK=auto: the automatic third-person fallback (docs/design-vr.md 1 "When to fall back to
 * third person, and how"; docs/re-xr-pipeline.md 6.8). Pure: no globals, no game reads, no OpenXR. Tested by
 * tests/test_fallback.c; somvr.c's SOMVR_FALLBACK region does the reads and applies the result.
 *
 * Three stages, once per stereo frame (game thread, H4):
 *   detector    one frame's reads of the game state -> the conditions under which the game owns Talion's body
 *               (cinematic camera, scripted camera, presentation, paired sync action, struggle, mount, attached move,
 *               climb/transition state, death, photo mode, spectator, a state tag of the configured list)
 *   switch      conditions over time -> the presentation wanted (0 first person on the head node, 1 third person on
 *               the game camera): an enter and an exit debounce, a minimum hold in each presentation of at least 1 s
 *               (never more than one toggle per second), a paired action counting only once it lasted sync_s or moved
 *               Talion's root sync_cm (short counters stay first person), no change while the game is paused
 *   transition  the presentation wanted -> per frame the presentation rendered and the fade (0 = clear, 1 = black):
 *               fade out, swap the base while black (at least 2 black frames), fade back in; a change of mind
 *               mid-fade turns the fade around from its current level
 */
#ifndef SOMVR_FALLBACK_H
#define SOMVR_FALLBACK_H

#include <stdint.h>

enum {
    SOMVR_FB_CINE,     /* the cinematic camera manager has an active camera: *(*(shell+0xb0)+0x20) != 0 */
    SOMVR_FB_SCRIPTED, /* a scripted level camera: *(u8 *)(cam+0x390) (cam = P+0x2d0) */
    SOMVR_FB_PRES,     /* a PlayerCameraPresentation runs: *(*(P+0x308)+0x90) != 0 (G16) */
    SOMVR_FB_SYNC,     /* the player's paired sync action has a record: *(*(server Combat+0x188)+0x10) != 0 (G14) */
    SOMVR_FB_STRUGGLE, /* PlayerCombat+0x8d8 (state) or +0x8e0 (sync) holds a struggle */
    SOMVR_FB_MOUNT,    /* mounted: *(P+0x1d8) != 0 */
    SOMVR_FB_ATTACH,   /* an attach-to-object move: *(u8 *)(pm+0x1a6) (pm = P+0x2c8) */
    SOMVR_FB_CLIMB,    /* the FSM's TransitionAnim (9), or a climb tag */
    SOMVR_FB_DEATH,    /* life state 3, or a death tag (P8: PreDeath_Kneeling, PreDeath; life stayed 2) */
    SOMVR_FB_PHOTO,    /* photo mode: *(int *)(*(P+0x318)+0x30) != 3 */
    SOMVR_FB_SPECT,    /* a spectator mode: *(int *)(P+0x4c8) != 0 */
    SOMVR_FB_TAG,      /* one of the other configured state tags (default FinalBlow) */
    SOMVR_FB_FORCED,   /* the trigger word "fallback third" */
    SOMVR_FB_NCOND
};
#define SOMVR_FB_BIT(c) (1u << (c))
#define SOMVR_FB_ALL ((1u << SOMVR_FB_NCOND) - 1u)
extern const char *const somvr_fb_cond_names[SOMVR_FB_NCOND];

/* which reads of somvr_fb_in were made this frame (a field without its bit is ignored) */
enum {
    SOMVR_FBR_LIFE = 1u << 0, SOMVR_FBR_SPECT = 1u << 1, SOMVR_FBR_PHOTO = 1u << 2, SOMVR_FBR_FSM = 1u << 3,
    SOMVR_FBR_CAM = 1u << 4, SOMVR_FBR_CINE = 1u << 5, SOMVR_FBR_PRES = 1u << 6, SOMVR_FBR_SYNC = 1u << 7,
    SOMVR_FBR_STRUGGLE = 1u << 8, SOMVR_FBR_MOUNT = 1u << 9, SOMVR_FBR_ATTACH = 1u << 10, SOMVR_FBR_TAGS = 1u << 11,
    SOMVR_FBR_PAUSE = 1u << 12
};
/* the classes a state tag can belong to (somvr_fb_tags) */
enum { SOMVR_FB_TAGC_DEATH = 1u, SOMVR_FB_TAGC_CLIMB = 2u, SOMVR_FB_TAGC_OTHER = 4u };
#define SOMVR_FB_FSM_TRANSITION 9 /* PlayerStateTransitionAnim (re-traversal.md 1.2) */
#define SOMVR_FB_LIFE_DYING 3     /* P+0x4c4: 1 not in the world, 2 alive, 3 dying/dead */
#define SOMVR_FB_PHOTO_OFF 3      /* PlayerPhotoMode+0x30: 3 = off */

typedef struct {
    uint32_t have;            /* SOMVR_FBR_* */
    int life, spect, photo, fsm, scripted, attach, paused;
    uint64_t cine, pres, sync, mount; /* pointers / records: non-zero = active */
    uint64_t struggle[2];     /* PlayerCombat+0x8d8, +0x8e0 */
    uint32_t tag_classes;     /* SOMVR_FB_TAGC_* of this frame's state tags */
} somvr_fb_in;

/* every condition the reads show (configuration masks are applied by the switch) */
uint32_t somvr_fb_detect(const somvr_fb_in *in);

/* ---- state tag classes: "death=PreDeath*,climb=Climb*,FinalBlow" (class=name; no class = other; a trailing * matches
 * a prefix; "none" = empty) ---- */
#define SOMVR_FB_MAXTAGS 16
#define SOMVR_FB_TAGLEN 48
typedef struct {
    int n;
    uint32_t cls[SOMVR_FB_MAXTAGS];
    uint8_t prefix[SOMVR_FB_MAXTAGS];
    char name[SOMVR_FB_MAXTAGS][SOMVR_FB_TAGLEN];
} somvr_fb_tags;
#define SOMVR_FB_TAGS_DEFAULT "death=PreDeath*,FinalBlow"
/* 0 = every item understood; -1 = at least one was not (it is skipped, the others kept) */
int somvr_fb_tags_parse(const char *spec, somvr_fb_tags *t);
uint32_t somvr_fb_tag_class(const somvr_fb_tags *t, const char *name); /* SOMVR_FB_TAGC_* bits, 0 = none */

/* ---- conditions by name: "cine,sync,death" / "all" / "none" -> bits; -1 = an unknown name (the rest kept) ---- */
int somvr_fb_cond_parse(const char *spec, uint32_t *bits);

/* ---- the switch ---- */
typedef struct {
    uint32_t enabled;         /* the conditions that count (SOMVR_FB_FORCED always does) */
    float enter_s, exit_s;    /* the wish must hold this long before a change (default 0.10 / 0.40) */
    float hold_s;             /* minimum time in each presentation (default 1.5; at least 1.0, enforced) */
    float sync_s, sync_cm;    /* a paired action counts once it lasted sync_s or moved the root sync_cm (default 0.6, 75;
                                 0, 0: at once) */
} somvr_fb_sw_cfg;
typedef struct {
    int want;                 /* the presentation wanted: 0 first person, 1 third person */
    int started;
    double since;             /* when `want` last changed (the start counts as long ago) */
    double cand;              /* since when the wish differs from `want` (< 0: it does not) */
    uint32_t raw, cond;       /* this frame: the detected conditions; the ones that count */
    int sync_on, sync_ok;     /* a paired action runs; it counts */
    double sync_t0;
    float sync_p0[3], sync_cm;/* the root at its start, its largest travel so far (cm) */
    int sync_have_p0;
    uint64_t switches;
} somvr_fb_sw;
void somvr_fb_sw_cfg_default(somvr_fb_sw_cfg *c);
void somvr_fb_sw_init(somvr_fb_sw *s);
/* one frame at t (s): raw = somvr_fb_detect's bits (+ SOMVR_FB_FORCED); root = Talion's root (cm) or NULL; hold = the
 * game is paused (no change; the debounce starts again afterwards). Returns 1 when `want` changed this frame. */
int somvr_fb_sw_step(somvr_fb_sw *s, const somvr_fb_sw_cfg *c, uint32_t raw, const float *root, int hold, double t);

/* ---- the transition ---- */
typedef struct { float out_s, black_s, in_s; } somvr_fb_fade_cfg; /* default 0.15 / 0.06 / 0.20; all 0 = a plain cut */
enum { SOMVR_FBT_IDLE, SOMVR_FBT_OUT, SOMVR_FBT_BLACK, SOMVR_FBT_IN };
extern const char *const somvr_fb_phase_names[4];
typedef struct {
    int phase, shown;         /* shown: the presentation rendered (0 first person, 1 third person) */
    double t0;                /* the phase's start (OUT/IN: shifted so that a turn-around keeps the level) */
    int black_frames;         /* frames rendered black since the last swap */
    float alpha;
    uint64_t swaps;
} somvr_fb_tr;
typedef struct {
    float alpha;              /* this frame's fade: 0 clear .. 1 black */
    int shown;                /* the presentation to render this frame */
    int swapped;              /* the presentation changed with this frame (it is black, unless a plain cut) */
    int phase_changed;
} somvr_fb_tr_out;
void somvr_fb_fade_cfg_default(somvr_fb_fade_cfg *c);
void somvr_fb_tr_init(somvr_fb_tr *tr, int shown);
void somvr_fb_tr_step(somvr_fb_tr *tr, const somvr_fb_fade_cfg *c, int want, double t, somvr_fb_tr_out *o);
/* the record's fade level (somvr_shared.h SOMVR_REC_FADE_*): 0..255, any alpha > 0 at least 1 */
uint32_t somvr_fb_fade_level(float alpha);

/* A sync record that outlives Talion's part in it (G14's +0x10 is cleared only when both participants have blended out,
 * FUN_02252f20; run logs/boot-check-20261024-020824: after the stock counter's 'PC_Bip_Sync_Cntr_Stun_F2' the record
 * stayed 87 s while Talion recoiled from the orc's hits, the body moments held the view and the combat found no orc).
 * The record counts while Talion's current attack is the one it started with (Combat+0x4d0; the first non-zero one
 * within 0.5 s of the start when it began with none); it goes stale once his attack has been another for stale_s, or
 * after max_s in all (0 = no cap), and stays stale until the record changes, clears, or his attack is the starting one
 * again (the same sync started anew). Without an attack read (have_atk 0), or no attack at its start, only max_s applies. */
typedef struct {
    uintptr_t rec, atk0;   /* the record followed, Talion's attack at its start */
    double t0, off_t;      /* its start; since when his attack is another (-1 = it is the start's) */
    int stale;
    uint64_t stales;       /* records that went stale (logs) */
} somvr_fb_sync_age;
#define SOMVR_FB_SYNC_STALE_S 1.0f
#define SOMVR_FB_SYNC_MAX_S 15.0f
/* 1 = the record counts now (rec set and not stale); 0 = none, or stale */
int somvr_fb_sync_live(somvr_fb_sync_age *a, uintptr_t rec, int have_atk, uintptr_t atk, double t, float stale_s, float max_s);

#endif
