/* somvr_session.h - SOMVR_MARK (bug marks) and SOMVR_QUIT (the VR menu's Quit game): the player's way to note a problem
 * without taking the headset off, and to end the session cleanly (docs/re-xr-pipeline.md 28). The user, after headset
 * session 2: "You should add a screenshot+voice recording bug/feature reporting feature like I have in HoF2" and "I
 * couldn't figure out how to close the game. I had to top and kill the process."
 *
 *   a mark   the chord (B + Y held together half a second and let go; a short buzz at half a second says it counts),
 *            the VR menu's Report button, or the trigger word `mark [NOTE]`: somvr.so makes <marks>/<HHMMSS>_<n>/ and
 *            writes state.txt (the place, Talion, the head and hands, the context, the settings, the frame rate), asks
 *            the layer for both eyes (eyes.ppm, written on a thread of its own), then writes `want`; boot_check.sh serves
 *            it (the flat picture, the log tails, voice.wav from WiVRn's microphone in a separate process, the JPEGs).
 *            Two buzzes confirm it: one at once, one when the eyes are written (or after 2 s).
 *   Quit     the VR menu's Quit game, pressed twice (the second press within 5 s confirms: the button reads Confirm),
 *            or the trigger word `quitgame`: somvr.so writes <run>/quit.request and boot_check.sh stops the game the
 *            way its `quit` does (the cleanup: the game, gamescope, the save check). Not the game's own quit: that
 *            needs its menus (the dialog clicks of runs 13 and 14 never confirmed) and in these sessions the save is
 *            a copy anyway (SOMVR_SAVEDIR). Without a harness (no SOM_BOOTCHECK_RUN) Quit is not offered.
 *
 * Parts: 1. the ABI: a seventh exported object next to the others (no other struct changes); 2. the chord's state
 * machine (pure: held bits and press counters in, the game's view of B and Y out, events); 3. small text helpers.
 * Tested by tests/test_session.c (pure), vklayer/tests (the layer's dump and menu buttons) and selftest.c. */
#ifndef SOMVR_SESSION_H
#define SOMVR_SESSION_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* ==== 1. the ABI ================================================================================================= */
#define SOMVR_SESSION_SYMBOL "somvr_session"
#define SOMVR_SESSION_MAGIC 0x31534553524d4f53ull /* "SOMRSES1" */
#define SOMVR_SESSION_VERSION 1u
#define SOMVR_SS_DIR_MAX 448

enum {                     /* somvr_session_shared.avail */
    SOMVR_SSA_MARK = 1u,   /* SOMVR_MARK: the menu's Report button works, the layer dumps the eyes on request */
    SOMVR_SSA_QUIT = 2u,   /* SOMVR_QUIT with a harness: the menu's Quit game works */
};

enum {                     /* somvr_session_shared.mono */
    SOMVR_SSM_MENU = 0,    /* menus, loading screens, anything else */
    SOMVR_SSM_VIDEO = 1,   /* a video state (the UI composite shows a pre-rendered video: the story cutscenes) */
    SOMVR_SSM_WINDOW = 2,  /* SOMVR_BODYMOMENTS' window (in-engine cinematics, captain presentations: flat frames) */
    SOMVR_SSM_LOAD = 3,    /* interface state 3, no menu, paused: a load on flat frames (the pause menu and the map share
                            * the state but render stereo: the layer reads it on mono frames only) */
};

typedef struct somvr_session_shared {
    uint64_t magic;               /*   0 */
    uint32_t version, size;       /*   8, 12 */
    _Atomic uint32_t active;      /*  16: set last by the preload (release) */
    int32_t pid;                  /*  20 */
    uint32_t avail;               /*  24: SOMVR_SSA_* (the preload, once) */
    uint32_t pad0;                /*  28 */
    _Atomic uint32_t report_req;  /*  32: layer: +1 per click on Report */
    _Atomic uint32_t quit_req;    /*  36: layer: +1 per confirmed Quit game */
    _Atomic uint32_t dump_req;    /*  40: preload: +1 per mark that wants the eyes, after dump_dir (release) */
    _Atomic uint32_t dump_done;   /*  44: layer: = the dump_req it served, once eyes.ppm is written or given up */
    _Atomic uint32_t dump_fail;   /*  48: layer: dumps it could not make (no frame copy, a write error) */
    _Atomic uint32_t marks;       /*  52: preload: marks made */
    _Atomic uint32_t quitting;    /*  56: preload: 1 = the harness was asked to stop the game */
    _Atomic uint32_t mono;        /*  60: preload: what flat (mono) frames show now, SOMVR_SSM_* (every input tick; the
                                   *      layer's theatre, never a void; 0 from an older preload: menus as before) */
    char dump_dir[SOMVR_SS_DIR_MAX]; /* 64: the mark's folder (eyes.ppm goes there), written before dump_req moves */
} somvr_session_shared;           /* 512 */

_Static_assert(offsetof(somvr_session_shared, avail) == 24 && offsetof(somvr_session_shared, report_req) == 32 &&
                   offsetof(somvr_session_shared, dump_req) == 40 && offsetof(somvr_session_shared, marks) == 52 &&
                   offsetof(somvr_session_shared, dump_dir) == 64,
               "somvr_session_shared offsets");
_Static_assert(sizeof(somvr_session_shared) == 512, "somvr_session_shared size");

static inline int somvr_session_ok(const somvr_session_shared *s) {
    return s && s->magic == SOMVR_SESSION_MAGIC && s->version == SOMVR_SESSION_VERSION &&
           s->size == (uint32_t)sizeof(somvr_session_shared) &&
           atomic_load_explicit(&((somvr_session_shared *)s)->active, memory_order_acquire) == 1;
}

/* ==== 2. the chord: B + Y (the upper buttons: Y = the left hand's, B = the right hand's) ======================== */
typedef struct {
    float window_s; /* 0.12 s: a lone B or Y waits this long for the other one before the game gets it (HoF2: 0.1 s) */
    float join_s;   /* 0.40 s: a button the game has had for at most this long still makes a chord with the other */
    float hold_s;   /* 0.50 s: held together this long and let go = a mark (the short buzz at this point) */
    float max_s;    /* 5 s: held together longer = nothing (a controller lying on its buttons) */
} somvr_chord_cfg;
void somvr_chord_cfg_default(somvr_chord_cfg *c);

enum { SOMVR_CHB_Y = 0, SOMVR_CHB_B = 1 };
enum { SOMVR_CHS_UP = 0, SOMVR_CHS_WAIT = 1, SOMVR_CHS_PASS = 2 };
enum { SOMVR_CHE_NONE = 0, SOMVR_CHE_ARMED = 1, SOMVR_CHE_MARK = 2, SOMVR_CHE_SHORT = 3, SOMVR_CHE_LONG = 4 };
typedef struct {
    int held[2];          /* the button is down now ([SOMVR_CHB_Y], [SOMVR_CHB_B]) */
    uint8_t presses[2];   /* the layer's press counters (somvr_hand_in.presses[1]; they count presses between ticks) */
    int act[2];           /* the hand's input is live (SOMVR_HACT_INPUT); not: that button is up, nothing pending */
} somvr_chord_in;
typedef struct {
    int held[2];          /* what the game side sees: down now */
    uint8_t presses[2];   /* ... and its press counters (a held-back press arrives as a new press, or a tap) */
    int ev;               /* SOMVR_CHE_*: ARMED (the chord held hold_s), MARK (let go after hold_s), SHORT / LONG */
    float held_s;         /* with MARK / SHORT / LONG: how long both were held together */
} somvr_chord_out;
typedef struct {
    int st[2];            /* SOMVR_CHS_* per button (outside a chord) */
    double t_dn[2];       /* its press (WAIT, PASS) */
    int chord;            /* both down together: both held back until both are up */
    int armed, ended;     /* the chord: held hold_s; one of them let go (the rest is swallowed until both are up) */
    double t_chord;
    uint8_t in_p[2], out_p[2];
    int have;             /* the counters' baseline is taken */
    uint32_t n_marks, n_short, n_long, n_held, n_passed; /* counts: marks, too short, too long, presses held back, passed */
} somvr_chord;
void somvr_chord_reset(somvr_chord *c);
/* one input tick at time t (seconds): the game side's view of B and Y in *out, an event in out->ev */
void somvr_chord_tick(somvr_chord *c, const somvr_chord_cfg *cfg, const somvr_chord_in *in, double t,
                      somvr_chord_out *out);

/* ==== 3. text ===================================================================================================== */
/* a mark's folder name: "HHMMSS_N" from the local time's hours, minutes, seconds and the mark's number */
void somvr_mark_name(int h, int m, int s, uint32_t n, char *buf, size_t cap);
/* the trigger word after "mark": "" or " NOTE..." -> the note (printable ASCII, blanks squeezed, at most cap - 1
 * bytes); 1 = a mark word ("mark", "mark NOTE"), 0 = not one (e.g. "marker") */
int somvr_mark_word(const char *w, char *note, size_t cap);
/* a path under cap bytes, no newline: 1 = usable for a mark folder / the quit file */
int somvr_ss_path_ok(const char *p, size_t cap);

#endif
