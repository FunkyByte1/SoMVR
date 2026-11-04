/* somvr_haptics.h - build 34: the mod's one haptics language (docs/re-xr-pipeline.md 153). Every pulse the mod gives is
 * a word of this table; a feature names the word (and, for a graded word, how strong: k 0..1), never an amplitude.
 * Pure and header-only (no globals, no game reads, no OpenXR): the preload (somvr.c's hx_play, the defence module) and
 * the layer (the VR menu's ticks) include it; tests/test_haptics.c checks the table, the rules and the scaling.
 *
 * The vocabulary (Half-Life: Alyx's and Blade & Sorcery's): what a pulse means is in its shape, how much in its class.
 *   TICK    a light short tick            something is here: in reach, under the pointer, a slot, a ring shown
 *   CLICK   a firm short click            it took: a grab, a hold, a draw, a press, a nock, a latch, a bite
 *   NO      two (three) soft clicks       no / refused / not here (three: none left)
 *   NUDGE   a soft long push              ready (an action waits for the hand) / going (a move asked for starts)
 *   THUMP   a strong short thump          an impact: a hit, a block, a stab, a shove, the bow's kick; graded by
 *                                         force; metal rings on (a ramp down after the crack)
 *   KILL    the strongest, a long tail    a kill lands: a finisher, an execution, a stealth kill
 *   RUMBLE  a soft steady buzz            continuous contact: the blade moving in flesh, a scrape, a strain
 *   ALERT   patterns of their own         danger or Talion's state: an attack coming, the red warning, the heartbeat
 * Rules (tests/test_haptics.c): each word inside its class's band; no word but an alert reaches the weakest impact;
 * a kill is the most an impact gives; a weapon's words reach the other hand when the sword is in both (`two`); the
 * player's strength (HAPTICS, 0..150 %) scales every word alike and keeps their order (no flat clip above 100 %).
 * Frequency: every pulse leaves it to the runtime (XR_FREQUENCY_UNSPECIFIED; the Quest 2 Touch plays its own). */
#ifndef SOMVR_HAPTICS_H
#define SOMVR_HAPTICS_H
#include <math.h>
#include <stdint.h>

#include "somvr_input.h" /* SOMVR_HAP_* (the pulse's source in the logs) */

enum { SOMVR_HXC_TICK, SOMVR_HXC_CLICK, SOMVR_HXC_NO, SOMVR_HXC_NUDGE, SOMVR_HXC_THUMP, SOMVR_HXC_KILL,
       SOMVR_HXC_RUMBLE, SOMVR_HXC_ALERT, SOMVR_HXC_N };

/* the words (the table's rows, in this order) */
enum {
    SOMVR_HX_HOVER,    /* TICK: the pointer on a menu row or a quad */
    SOMVR_HX_TICK,     /* TICK: in reach of a hold, a slot, the quiver, the string; a ring shown; a cue; crouched */
    SOMVR_HX_CLICK,    /* CLICK: a press took (a menu, a command), a put-back, a light "done" */
    SOMVR_HX_TAKE,     /* CLICK: the hand took hold (a climb hold, an orc, an arrow, a herb, the sword out, a latch) */
    SOMVR_HX_EAT,      /* CLICK x3: the herb's bites at the mouth */
    SOMVR_HX_MARK,     /* CLICK, both hands: a bug mark taken / its pictures saved */
    SOMVR_HX_NO,       /* NO: two soft clicks - refused, missed, not here */
    SOMVR_HX_EMPTY,    /* NO x3: none left (the quiver) */
    SOMVR_HX_READY,    /* NUDGE: an action waits for the hand (a finisher's glow, a stealth kill, the knife, A = down) */
    SOMVR_HX_GO,       /* NUDGE: a move asked for starts (a dash, a dodge, a phase, a fling, a jump, a wraith step) */
    SOMVR_HX_QUIT,     /* NUDGE, both hands: the game quits */
    SOMVR_HX_HIT,      /* THUMP: the blade into a body (k: light 0, stab 0.5, heavy 1); a stab going in */
    SOMVR_HX_HIT_WEIGHT, /* THUMP: the same with SOMVR_SWORD_WEIGHT: full, and the weight's tail */
    SOMVR_HX_TEAR,     /* THUMP: the blade comes out of a body (k: through 0, drawn out 0.2, torn free 1) */
    SOMVR_HX_CLANK,    /* THUMP: the blade on metal or stone (k: the blade's speed): the crack and its ring */
    SOMVR_HX_BLOCK,    /* THUMP: an orc's blow on the blade (k: block 0.5, parry 1): crack, ring, the free hand braces */
    SOMVR_HX_PARRY,    /* THUMP: a parry's own beat in the free hand */
    SOMVR_HX_THUMP,    /* THUMP: a body impact by the hand (k: force): a shove, a throw, a catch, a press, the bow's kick */
    SOMVR_HX_POWER,    /* THUMP: an ability or a verb fires (the game's power moment the hand set off) */
    SOMVR_HX_HURT,     /* THUMP, both hands: Talion is hit (k: the damage) */
    SOMVR_HX_KILL,     /* KILL: a kill lands (k: a kill's own end 0 .. the killing blow 1) */
    SOMVR_HX_FLESH,    /* RUMBLE: the blade moving in flesh (k: its speed) */
    SOMVR_HX_RUB,      /* RUMBLE: the blade rubs a body, too slow to cut */
    SOMVR_HX_TOUCH,    /* RUMBLE: a hand or the blade touches a surface or a body (k: the speed) */
    SOMVR_HX_STRAIN,   /* RUMBLE: a held effort (k: how hard): the string's tension, the blade's drag, a bucking beast */
    SOMVR_HX_GAME,     /* RUMBLE: the game's own rumble, its share (the mixer's continuous level, never a pulse) */
    SOMVR_HX_ANNOUNCE, /* ALERT: an attack is coming on that side */
    SOMVR_HX_URGENT,   /* ALERT: it lands soon - a quick double on that side */
    SOMVR_HX_RED,      /* ALERT, both hands: an unblockable - three strong, 130 ms apart */
    SOMVR_HX_RED_LAST, /* ALERT, both hands: the unblockable lands now - a strong double */
    SOMVR_HX_STRUGGLE, /* ALERT, both hands: the last chance begins */
    SOMVR_HX_HEART,    /* ALERT, both hands: low health - the heartbeat's lub and its softer dub 0.25 s later */
    SOMVR_HX_N
};

/* a class's band: the level (k at 0 and 1, the gain applied), one pulse's length (s), the pulses, the tail */
typedef struct {
    const char *name;
    float lo, hi;           /* every level of its words inside [lo, hi] (hi: also the cap a feature's gain meets) */
    float dur_lo, dur_hi;   /* each pulse */
    int n_lo, n_hi;         /* pulses */
    float tail_max;         /* the tail's start as a share of the level (0: none in this class) */
    float total_max;        /* the whole word's length (s), its tail included */
} somvr_hx_band;

typedef struct {
    const char *name;       /* the logs' and the docs' word */
    uint8_t cls;            /* SOMVR_HXC_* */
    uint8_t n;              /* pulses (1..3) */
    uint8_t src;            /* SOMVR_HAP_UI / _COMBAT (the logs) */
    uint8_t oth_tail;       /* 1: the other hand gets the tail too (its share of it) */
    float lo, hi;           /* the level at k = 0 / k = 1 (a fixed word: lo = hi) */
    float dur, gap;         /* each pulse; between pulses (s) */
    float decay;            /* each next pulse's level x this (1: all alike) */
    float tail, tail_s;     /* after the first pulse a ramp from tail x level down to 0 over tail_s (0: none) */
    float two;              /* the other hand's share while the sword is in both (a weapon's words; 0: never) */
    float both;             /* the other hand's share always (the body's words; 0: never) */
} somvr_hx_word;

static inline const somvr_hx_band *somvr_hx_band_of(int cls)
{
    static const somvr_hx_band B[SOMVR_HXC_N] = {
        /*            name      lo     hi    dur_lo  dur_hi n_lo n_hi tail  total */
        [SOMVR_HXC_TICK] = { "tick", 0.10f, 0.25f, 0.008f, 0.025f, 1, 1, 0, 0.025f },
        [SOMVR_HXC_CLICK] = { "click", 0.30f, 0.50f, 0.015f, 0.045f, 1, 3, 0, 0.45f },
        [SOMVR_HXC_NO] = { "no", 0.20f, 0.35f, 0.015f, 0.035f, 2, 3, 0, 0.25f },
        [SOMVR_HXC_NUDGE] = { "nudge", 0.25f, 0.45f, 0.040f, 0.130f, 1, 1, 0, 0.13f },
        [SOMVR_HXC_THUMP] = { "thump", 0.55f, 1.00f, 0.030f, 0.120f, 1, 1, 0.6f, 0.33f },
        [SOMVR_HXC_KILL] = { "kill", 0.90f, 1.00f, 0.080f, 0.120f, 1, 1, 0.6f, 0.38f },
        [SOMVR_HXC_RUMBLE] = { "rumble", 0.08f, 0.50f, 0.000f, 0.120f, 1, 1, 0, 0.12f },
        [SOMVR_HXC_ALERT] = { "alert", 0.30f, 1.00f, 0.030f, 0.080f, 1, 3, 0, 0.40f },
    };
    return cls >= 0 && cls < SOMVR_HXC_N ? &B[cls] : &B[SOMVR_HXC_TICK];
}

/* THE TABLE: every word, its class, levels (HAPTICS 100 %), pulse, pattern, tail, the other hand's shares */
static inline const somvr_hx_word *somvr_hx_word_of(int w)
{
#define HXW(id, nm, c, src_, n_, lo_, hi_, dur_, gap_, dec_, tl_, tls_, two_, both_, ot_)                                  \
    [id] = { nm, SOMVR_HXC_##c, n_, SOMVR_HAP_##src_, ot_, lo_, hi_, dur_, gap_, dec_, tl_, tls_, two_, both_ }
    static const somvr_hx_word T[SOMVR_HX_N] = {
        /*  id                 name          class  src     n  lo     hi     dur     gap    decay tail  tail_s two   both oth_tail */
        HXW(SOMVR_HX_HOVER, "hover", TICK, UI, 1, 0.12f, 0.12f, 0.010f, 0, 1, 0, 0, 0, 0, 0),
        HXW(SOMVR_HX_TICK, "tick", TICK, UI, 1, 0.20f, 0.20f, 0.016f, 0, 1, 0, 0, 0, 0, 0),
        HXW(SOMVR_HX_CLICK, "click", CLICK, UI, 1, 0.32f, 0.32f, 0.018f, 0, 1, 0, 0, 0, 0, 0),
        HXW(SOMVR_HX_TAKE, "take", CLICK, COMBAT, 1, 0.45f, 0.45f, 0.030f, 0, 1, 0, 0, 0, 0, 0),
        HXW(SOMVR_HX_EAT, "eat", CLICK, UI, 3, 0.45f, 0.45f, 0.040f, 0.080f, 0.88f, 0, 0, 0, 0, 0),
        HXW(SOMVR_HX_MARK, "mark", CLICK, UI, 1, 0.45f, 0.45f, 0.040f, 0, 1, 0, 0, 0, 1, 0),
        HXW(SOMVR_HX_NO, "no", NO, UI, 2, 0.28f, 0.28f, 0.025f, 0.060f, 1, 0, 0, 0, 0, 0),
        HXW(SOMVR_HX_EMPTY, "empty", NO, COMBAT, 3, 0.32f, 0.32f, 0.025f, 0.050f, 1, 0, 0, 0, 0, 0),
        HXW(SOMVR_HX_READY, "ready", NUDGE, COMBAT, 1, 0.38f, 0.38f, 0.060f, 0, 1, 0, 0, 0, 0, 0),
        HXW(SOMVR_HX_GO, "go", NUDGE, COMBAT, 1, 0.30f, 0.30f, 0.045f, 0, 1, 0, 0, 0, 0, 0),
        HXW(SOMVR_HX_QUIT, "quit", NUDGE, UI, 1, 0.45f, 0.45f, 0.120f, 0, 1, 0, 0, 0, 1, 0),
        HXW(SOMVR_HX_HIT, "hit", THUMP, COMBAT, 1, 0.80f, 1.00f, 0.090f, 0, 1, 0, 0, 0.6f, 0, 0),
        HXW(SOMVR_HX_HIT_WEIGHT, "hit-weight", THUMP, COMBAT, 1, 1.00f, 1.00f, 0.090f, 0, 1, 0.5f, 0.160f, 0.6f, 0, 1),
        HXW(SOMVR_HX_TEAR, "tear", THUMP, COMBAT, 1, 0.55f, 0.85f, 0.060f, 0, 1, 0, 0, 0.6f, 0, 0),
        HXW(SOMVR_HX_CLANK, "clank", THUMP, COMBAT, 1, 0.55f, 0.90f, 0.040f, 0, 1, 0.5f, 0.150f, 0.6f, 0, 0),
        HXW(SOMVR_HX_BLOCK, "block", THUMP, COMBAT, 1, 0.75f, 1.00f, 0.050f, 0, 1, 0.6f, 0.250f, 0, 0.45f, 0),
        HXW(SOMVR_HX_PARRY, "parry", THUMP, COMBAT, 1, 0.90f, 0.90f, 0.090f, 0, 1, 0, 0, 0, 0, 0),
        HXW(SOMVR_HX_THUMP, "thump", THUMP, COMBAT, 1, 0.55f, 1.00f, 0.080f, 0, 1, 0, 0, 0, 0, 0),
        HXW(SOMVR_HX_POWER, "power", THUMP, COMBAT, 1, 0.70f, 0.70f, 0.090f, 0, 1, 0, 0, 0, 0, 0),
        HXW(SOMVR_HX_HURT, "hurt", THUMP, COMBAT, 1, 0.80f, 1.00f, 0.090f, 0, 1, 0.5f, 0.200f, 0, 1, 1),
        HXW(SOMVR_HX_KILL, "kill", KILL, COMBAT, 1, 0.90f, 1.00f, 0.100f, 0, 1, 0.5f, 0.250f, 0.6f, 0, 1),
        HXW(SOMVR_HX_FLESH, "flesh", RUMBLE, COMBAT, 1, 0.12f, 0.50f, 0.040f, 0, 1, 0, 0, 0.6f, 0, 0),
        HXW(SOMVR_HX_RUB, "rub", RUMBLE, COMBAT, 1, 0.12f, 0.12f, 0.100f, 0, 1, 0, 0, 0.6f, 0, 0),
        HXW(SOMVR_HX_TOUCH, "touch", RUMBLE, COMBAT, 1, 0.15f, 0.50f, 0.025f, 0, 1, 0, 0, 0.6f, 0, 0),
        HXW(SOMVR_HX_STRAIN, "strain", RUMBLE, COMBAT, 1, 0.12f, 0.45f, 0.060f, 0, 1, 0, 0, 0.6f, 0, 0),
        HXW(SOMVR_HX_GAME, "game", RUMBLE, RUMBLE, 1, 0.50f, 0.50f, 0, 0, 1, 0, 0, 0, 0, 0),
        HXW(SOMVR_HX_ANNOUNCE, "announce", ALERT, COMBAT, 1, 0.50f, 0.50f, 0.080f, 0, 1, 0, 0, 0, 0, 0),
        HXW(SOMVR_HX_URGENT, "urgent", ALERT, COMBAT, 2, 0.80f, 0.80f, 0.035f, 0.040f, 1, 0, 0, 0, 0, 0),
        HXW(SOMVR_HX_RED, "red", ALERT, COMBAT, 3, 1.00f, 1.00f, 0.070f, 0.060f, 1, 0, 0, 0, 1, 0),
        HXW(SOMVR_HX_RED_LAST, "red-last", ALERT, COMBAT, 2, 1.00f, 1.00f, 0.060f, 0.060f, 1, 0, 0, 0, 1, 0),
        HXW(SOMVR_HX_STRUGGLE, "struggle", ALERT, COMBAT, 2, 0.80f, 0.80f, 0.060f, 0.080f, 1, 0, 0, 0, 1, 0),
        HXW(SOMVR_HX_HEART, "heart", ALERT, COMBAT, 2, 0.45f, 0.45f, 0.050f, 0.200f, 0.667f, 0, 0, 0, 1, 0),
    };
#undef HXW
    return w >= 0 && w < SOMVR_HX_N ? &T[w] : &T[SOMVR_HX_TICK];
}

static inline const char *somvr_hx_name(int w) { return w >= 0 && w < SOMVR_HX_N ? somvr_hx_word_of(w)->name : "?"; }

/* the player's strength s (HAPTICS / 100, 0..1.5) on a level a (0..1): up to 100 % plainly a x s; above it a x s /
 * (1 + (s - 1) a) - the weak words gain the most, 1 stays 1, and no two levels meet at the top (a x s clipped at 1 made
 * every impact from 0.67 up the same at 150 %). Monotone in a and in s; NaN or <= 0: 0. */
static inline float somvr_hx_scale(float a, float s)
{
    if (!(a > 0) || !(s > 0)) return 0;
    if (a > 1) a = 1;
    if (s > 1.5f) s = 1.5f;
    float r = s <= 1 ? a * s : a * s / (1 + (s - 1) * a);
    return r > 1 ? 1 : r;
}

/* word w's level for k (0..1; NaN: 0) and a feature's gain (its own switch, 1 = the table; <= 0 or NaN: 0), at most its
 * class's top: no gain lifts a word out of its class */
static inline float somvr_hx_level(int w, float k, float gain)
{
    const somvr_hx_word *d = somvr_hx_word_of(w);
    if (!(gain > 0)) return 0;
    if (!(k > 0)) k = 0;
    if (k > 1) k = 1;
    float a = (d->lo + (d->hi - d->lo) * k) * gain, top = somvr_hx_band_of(d->cls)->hi;
    return a > top ? top : a;
}

/* a pulse for the mixer (sc_hap_add_at): on `hand`, t s from now, amp, dur s, ramp 1 = linear down to 0 */
typedef struct { int hand; float t, amp, dur; int ramp; uint32_t src; } somvr_hx_shot;
#define SOMVR_HX_SHOTS 8

/* word w on hand h as pulses: its n pulses (each decay x the last, gap apart), its tail after the first, and the other
 * hand's share (both: always; two: when `two` - the sword in both hands and h its hand) of the pulses (and of the tail
 * with oth_tail). Returns how many (0: a level of 0, or the GAME word - the mixer's rumble, never a pulse). */
static inline int somvr_hx_shots(int w, float k, float gain, int h, int two, somvr_hx_shot out[SOMVR_HX_SHOTS])
{
    const somvr_hx_word *d = somvr_hx_word_of(w);
    float a = somvr_hx_level(w, k, gain);
    if (!(a > 0) || !(d->dur > 0) || w < 0 || w >= SOMVR_HX_N) return 0;
    float oth = d->both > 0 ? d->both : two && d->two > 0 ? d->two : 0;
    int n = 0;
    for (int side = 0; side < (oth > 0 ? 2 : 1); side++) {
        float s = side ? oth : 1, l = a * s;
        int hand = side ? 1 - (h & 1) : h & 1;
        for (int i = 0; i < d->n && i < 3; i++, l *= d->decay)
            out[n++] = (somvr_hx_shot){ hand, (float)i * (d->dur + d->gap), l, d->dur, 0, d->src };
        if (d->tail > 0 && d->tail_s > 0 && (!side || d->oth_tail))
            out[n++] = (somvr_hx_shot){ hand, d->dur, a * s * d->tail, d->tail_s, 1, d->src };
    }
    return n;
}

/* the word's length (s): its pulses, gaps and tail */
static inline float somvr_hx_total(int w)
{
    const somvr_hx_word *d = somvr_hx_word_of(w);
    float p = (float)d->n * d->dur + (float)(d->n - 1) * d->gap, t = d->tail > 0 ? d->dur + d->tail_s : 0;
    return p > t ? p : t;
}
#endif
