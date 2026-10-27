/* somvr_vrmenu.h - the in-game VR settings menu (SOMVR_VRMENU=1, docs/re-xr-pipeline.md 15): a small panel on the
 * off hand's wrist (Half-Life: Alyx style) that the main hand's laser pointer operates. C11, no OpenXR/Vulkan. Parts:
 *
 * 1. The ABI: a fifth exported object next to `somvr_shared`, `somvr_input`, `somvr_hud` and `somvr_comfort` (the
 *    smallest compatible addition, as theirs: no other struct changes). The preload (SOMVR_VRMENU=1 with SOMVR_INPUT=1)
 *    fills magic, version, size, pid, avail, the settings it runs with (the environment, then the settings file over
 *    it) and sets `active` (release); otherwise the object stays zero and the layer shows no menu.
 *      set: written under the seqlock `seq`: by the preload before `active`, then by the layer only (a click changed a
 *      value), each change followed by gen + 1 (release). The game thread reads set when gen moves and applies it live;
 *      the preload's writer thread saves it to the settings file.
 *      toggles: the preload (the game thread) counts long presses of the left menu button; each one opens or closes
 *      the panel at the layer's next frame. recenters: the layer counts presses of the panel's Recentre button.
 *      open, consume: the layer, every frame: the panel is shown; bit h = hand h's trigger is the panel's (its ray on
 *      the panel or its click held), so the game side drops that trigger from the pad (as somvr_input.ptr.consume).
 *      block: the preload (the game thread), every input tick: SOMVR_VMG_* = the hands are busy now (the bow out, a
 *      climb hold, a blade swing, an attack incoming); the palm gesture opens nothing meanwhile and needs a fresh
 *      gesture after it (somvr_vm_gesture). It was pad0 (always 0): an older preload never blocks, an older layer
 *      ignores it (offset 108 in version 1, 144 bytes; 124 since v2, 160 bytes, the settings 64).
 *      practice: the layer, at a click on one of the practice page's buttons (SOMVR_PRACTICE, re-xr-pipeline.md 27):
 *      somvr_vm_pr_pack(seq + 1, SOMVR_VMPR_*, the orc type) stored (release); the preload's game thread acts once per
 *      new seq. pstate: the preload, every H4 frame (pr_frame): the practice area's live state (SOMVR_VMA_PR_* bits;
 *      the layer ORs them into avail). Since v3 (168 bytes); the settings' pad1 is the practice page's orc type since v3.
 *
 * 2. The settings (somvr_settings) and their file (SOMVR_SETTINGS=FILE): one KEY=VALUE per line, '#' comments; the
 *    keys are the switches' names without SOMVR_ (TURN=snap|smooth, SNAP_DEG, TURN_DPS, VIGNETTE, SNAP_BLINK_MS,
 *    SEATED, HEIGHT_CM, HAPTICS, STEP, DASH, PHASE, SWORD_ANGLE, MUSIC, PLAYER_SIZE, CROUCH, CLIMB_FLING,
 *    HANDS_SURFACE: the environment variables of the
 *    same name; HUD=on|off,
 *    HUD_DIST_M, HAND=right|left, VIEW=first|third: the menu's own). Parse and format are pure and locale-free.
 *
 * 3. The menu: its items, their values and steps, the panel's layout (a 512 x 768 texture since v2), the hit test of a
 *    pointer uv, the panel's picture (with somvr_font.h), the open/close gesture and the panel's pose on the wrist.
 *
 * Tested by tests/test_vrmenu.c (pure), vklayer/tests/test_layer.c (the layer's menu against the mock runtime) and
 * selftest.c (the game side).
 */
#ifndef SOMVR_VRMENU_H
#define SOMVR_VRMENU_H

#include "somvr_font.h"
#include "somvr_input.h" /* SOMVR_HAND_L / _R; somvr_shared.h */

/* ==== 1. the ABI ================================================================================================= */
#define SOMVR_VRMENU_SYMBOL "somvr_vrmenu"
#define SOMVR_VRMENU_MAGIC 0x314e4d56524d4f53ull /* "SOMRVMN1" */
#define SOMVR_VRMENU_VERSION 6u /* 2: STEP, DASH, PHASE (re-xr-pipeline.md 23): the settings 64 bytes; 3: the practice
                                 * page (re-xr-pipeline.md 27): practice, pstate, settings.orc; 168 bytes; 4: the sword
                                 * angle (re-xr-pipeline.md 29): settings.sword_deg, the settings 72 bytes, 176; 5: the
                                 * music volume (re-xr-pipeline.md 34): settings.music, the settings 80 bytes, 184; 6
                                 * (build 17, re-xr-pipeline.md 56): settings.sword_hand, .quiver and 6 spare bytes, the
                                 * settings 88 bytes, 192 */

enum {                         /* somvr_vrmenu_shared.block: why the palm gesture opens nothing now */
    SOMVR_VMG_BOW = 1u,        /* SOMVR_BOW: the bow is out (nocked, drawn or held) */
    SOMVR_VMG_CLIMB = 2u,      /* SOMVR_CLIMB: a hand holds in our climb */
    SOMVR_VMG_SWING = 4u,      /* SOMVR_COMBAT: a blade swing is on (the sword's speed above the slash speed) */
    SOMVR_VMG_THREAT = 8u,     /* SOMVR_THREAT: an attack at Talion before its impact, or an arrow inbound */
    SOMVR_VMG_ALL = 15u,
};

enum {                         /* somvr_vrmenu_shared.avail: what the game side can change live */
    SOMVR_VMA_VIEW = 1u,       /* first/third person: SOMVR_FALLBACK=auto on the head base (its forced third person) */
    SOMVR_VMA_HUD = 2u,        /* the HUD quad exists (SOMVR_HUD=quad; the layer also needs its somvr_hud) */
    SOMVR_VMA_COMFORT = 4u,    /* somvr_comfort is active: the vignette and the snap blink follow the menu */
    SOMVR_VMA_FLOOR = 8u,      /* the layer: a STAGE space (a floor): the seated height is used */
    SOMVR_VMA_MOVES = 16u,     /* the assisted step, the dash, the wraith phase run (the LOCO core is on) */
    SOMVR_VMA_PRACTICE = 32u,  /* SOMVR_PRACTICE runs: the practice page's tab */
    /* the practice area's live state (somvr_vrmenu_shared.pstate, every H4 frame; never in avail itself) */
    SOMVR_VMA_PR_HERE = 64u,   /* Talion is in the practice area */
    SOMVR_VMA_PR_BACK = 128u,  /* a place to go back to is kept */
    SOMVR_VMA_PR_ORC = 256u,   /* our orc is out */
    SOMVR_VMA_PR_BUSY = 512u,  /* a blink there or back is on */
    SOMVR_VMA_PR_LIVE = 960u,  /* the four above */
    /* the layer's own, from the preload's somvr_session (somvr_session.h; never in somvr_vrmenu_shared.avail) */
    SOMVR_VMA_MARK = 1024u,    /* SOMVR_MARK: the Report button (a bug mark) */
    SOMVR_VMA_QUIT = 2048u,    /* SOMVR_QUIT with a harness: the Quit game button */
    SOMVR_VMA_SWORD = 4096u,   /* v4: SOMVR_ARMS: the sword in the hand, its angle (SOMVR_SWORD_ANGLE) live */
    SOMVR_VMA_MUSIC = 8192u,   /* v5: SOMVR_MUSIC: the game's music volume (its options_music RTPC) follows the menu */
    SOMVR_VMA_SIZE = 16384u,   /* the player size (SOMVR_PLAYER_SIZE, re-xr-pipeline.md 35): first person on the head */
    SOMVR_VMA_CROUCH = 32768u, /* build 13: SOMVR_CROUCH set (re-xr-pipeline.md 41): the Crouch row (duck / button) */
    /* build 13: the preload's switches that change the Controls page's lines (somvr_vm_controls_av) and two rows */
    SOMVR_VMA_VERBS = 65536u,    /* SOMVR_VERBS=on: the grab's line and notes */
    SOMVR_VMA_DESCENT = 131072u, /* SOMVR_CLIMB_DESCENT=on: "Drop: A (wall, edge)" */
    SOMVR_VMA_SURFACE = 262144u, /* SOMVR_HANDS_SURFACE=on running: the Hands at walls row */
    SOMVR_VMA_FLING = 524288u,   /* SOMVR_CLIMB_FLING=on with the hang: "Climb: grip, pull; fling up", the Climb fling row */
    /* build 14 candidate (the story drill's round 2): SOMVR_USE=1: "Use: B (pick, missions)"; Menu and Pause one line,
     * so Sprint stays with the crouch (the timed objectives want the left stick's click) */
    SOMVR_VMA_USE = 1048576u,
    /* build 15b: SOMVR_BOW_DRAW=slot: "Bow: L grip at L shoulder" (the left trigger is a grab) */
    SOMVR_VMA_BOWSLOT = 2097152u,
    /* build 16: SOMVR_ARMS (the sword in the hand): the Sword weight row (SOMVR_SWORD_WEIGHT live) */
    SOMVR_VMA_WEIGHT = 4194304u,
    /* build 16: SOMVR_BOW_QUIVER=on with the string on the right trigger: "Arrow: RT at R shoulder", "Shoot: nock, pull,
     * let go" (Parry's line goes first for the room: moving the sword into a blow needs no words) */
    SOMVR_VMA_BOWRT = 8388608u,
    /* build 17 (re-xr-pipeline.md 56): SOMVR_ARMS with the layer's hands (SOMVR_VRHANDS): the Sword hand row
     * (SOMVR_SWORD_WEIGHT_HAND live; enabled while the Sword weight is not Off) */
    SOMVR_VMA_SWHAND = 16777216u,
    /* build 17: SOMVR_BOW: the Arrows row (SOMVR_BOW_QUIVER live, taken while the bow is away); with it the Controls
     * page's bow lines follow the row (somvr_vm_ctl_avail: BOWRT and QVGRIP from settings.quiver and BOWTRIG) */
    SOMVR_VMA_ARROWS = 33554432u,
    /* build 17: SOMVR_BOW_STRING=trigger (the string on the right trigger): "Shoot: RT, pull, let go" without the
     * quiver */
    SOMVR_VMA_BOWTRIG = 67108864u,
    /* build 17: the quiver with the string on the grip (SOMVR_BOW_STRING=grip): "Arrow: R grip at R shoulder" */
    SOMVR_VMA_QVGRIP = 134217728u,
    /* build 17: SOMVR_CLIMB_PULLOVER=on (re-xr-pipeline.md 50): "Ledge: pull at the ring" */
    SOMVR_VMA_PULLOVER = 268435456u,
    /* build 17: SOMVR_CLIMB_DESCENT_MARK=on: "Drop: hold A, let go" (A held shows the landing, let go goes) */
    SOMVR_VMA_DROPMARK = 536870912u,
    /* build 17 (the bite17 helper, re-xr-pipeline.md 59): SOMVR_ARMS and SOMVR_COMBAT: the Blade bite row
     * (SOMVR_COMBAT_BITE live) */
    SOMVR_VMA_BITE = 1073741824u,
    /* build 17 (the strike17 helper): SOMVR_STRIKE_GAZE=on: the Wraith line says A strikes the orc looked at ("hold Y;
     * A = Strike"; the same line count: the mark in the world shows the button too) */
    SOMVR_VMA_STRIKE = 2147483648u, /* 1<<31: the coordinator's (descent 1<<29, bite 1<<30) */
};
/* the practice page's requests (somvr_vrmenu_shared.practice; = somvr_practice.h's SOMVR_PRA_*) */
enum { SOMVR_VMPR_NONE = 0, SOMVR_VMPR_GO = 1, SOMVR_VMPR_BACK = 2, SOMVR_VMPR_SPAWN = 3, SOMVR_VMPR_REMOVE = 4,
       /* build 17 (5 = the practice's own status word): the bow's arrows and Focus full, Talion's health full */
       SOMVR_VMPR_ARROWS = 6, SOMVR_VMPR_HEALTH = 7 };
static inline uint32_t somvr_vm_pr_pack(uint32_t seq, int action, int orc) {
    return seq << 8 | ((uint32_t)orc & 15u) << 4 | ((uint32_t)action & 15u);
}
static inline uint32_t somvr_vm_pr_seq(uint32_t w) { return w >> 8; }
static inline int somvr_vm_pr_action(uint32_t w) { return (int)(w & 15u); }
static inline int somvr_vm_pr_orc(uint32_t w) { return (int)(w >> 4 & 15u); }

typedef struct {
    uint32_t turn;             /*  0: 0 snap, 1 smooth (SOMVR_TURN) */
    float snap_deg;            /*  4: one snap step (SOMVR_SNAP_DEG) */
    float turn_dps;            /*  8: smooth turn speed (SOMVR_TURN_DPS) */
    float vignette;            /* 12: 0..1 (SOMVR_VIGNETTE) */
    uint32_t blink_ms;         /* 16: the snap blink, 0 = off (SOMVR_SNAP_BLINK_MS) */
    uint32_t seated;           /* 20: SOMVR_SEATED */
    float height_cm;           /* 24: the seated eye height above the floor, 0 = the head's at the recentre */
    uint32_t hud;              /* 28: 1 = the HUD quad shown */
    float hud_dist_m;          /* 32: the HUD quad's distance (its width and drop scale with it) */
    uint32_t hand;             /* 36: the main hand: 0 right, 1 left (the panel on the other wrist; it points) */
    uint32_t view;             /* 40: 0 first person, 1 third person */
    float haptics;             /* 44: strength 0..1.5 (SOMVR_HAPTICS / 100) */
    float step;                /* 48: the assisted step's strength 0..1, 0 = off (SOMVR_STEP) */
    uint32_t dash;             /* 52: 0 off, 1 on, 2 blink (SOMVR_DASH) */
    uint32_t phase;            /* 56: 0 off, 1 on, 2 on + re-face, 3 blink (SOMVR_PHASE) */
    uint32_t orc;              /* 60: the practice page's orc type 0 infantry, 1 shield, 2 berserker (no file key) */
    float sword_deg;           /* 64: v4: the sword's angle in the fist, deg (SOMVR_SWORD_ANGLE; + = the blade up toward
                                * the knuckles), -45..45 in the menu */
    float size;                /* 68: the player size, 1 = 100 % (SOMVR_PLAYER_SIZE in %: 100..140; re-xr-pipeline.md 35);
                                * was pad2 (0): a preload that sets no SOMVR_VMA_SIZE ignores it */
    float music;               /* 72: v5: the game's music at this share of its own volume setting, 0..1 (SOMVR_MUSIC;
                                * 1 = as the game sets it) */
    uint8_t crouch;            /* 76: build 13: 0 button, 1 duck (SOMVR_CROUCH; re-xr-pipeline.md 41); was pad3 (0): a
                                * preload that sets no SOMVR_VMA_CROUCH ignores it */
    uint8_t fling;             /* 77: build 13: the climb's fling 0 off, 1 on (SOMVR_CLIMB_FLING; needs SOMVR_VMA_FLING) */
    uint8_t surface;           /* 78: build 13: the hands stop at surfaces 0 off, 1 on (SOMVR_HANDS_SURFACE; needs
                                * SOMVR_VMA_SURFACE) */
    uint8_t sword_weight;      /* 79: build 16: the sword's weight 0 off, 1 light, 2 heavy (SOMVR_SWORD_WEIGHT; needs
                                * SOMVR_VMA_WEIGHT); was pad3 (0): a preload that sets no SOMVR_VMA_WEIGHT ignores it */
    uint8_t sword_hand;        /* 80: v6: the drawn hands with the weighted blade 0 fixed, 1 follow
                                * (SOMVR_SWORD_WEIGHT_HAND; needs SOMVR_VMA_SWHAND) */
    uint8_t quiver;            /* 81: v6: the bow's arrows 0 on the string by themselves, 1 from the quiver behind the
                                * string hand's shoulder (SOMVR_BOW_QUIVER; needs SOMVR_VMA_ARROWS) */
    uint8_t bite;              /* 82: v6 (the bite17 helper): a landed cut's bite 0 off, 1 on (SOMVR_COMBAT_BITE; needs
                                * SOMVR_VMA_BITE; was pad6[0]: a preload that sets no SOMVR_VMA_BITE ignores it) */
    uint8_t pad6[5];           /* 83: spare (0) for the next rows: a byte each, no new version */
} somvr_settings;              /* 88 */

typedef struct somvr_vrmenu_shared {
    uint64_t magic;            /*   0 */
    uint32_t version, size;    /*   8, 12 */
    _Atomic uint32_t active;   /*  16: set last by the preload (release) */
    int32_t pid;               /*  20 */
    uint32_t avail;            /*  24: SOMVR_VMA_* (the preload's; the layer adds FLOOR and drops HUD without its quad) */
    uint32_t blink_on_ms;      /*  28: the blink length "Snap blink: On" selects */
    _Atomic uint64_t seq;      /*  32: seqlock over set */
    somvr_settings set;        /*  40 */
    _Atomic uint32_t gen;      /* 128: +1 per change of set (after it) */
    _Atomic uint32_t toggles;  /* 132: preload: long presses of the left menu button */
    _Atomic uint32_t recenters;/* 136: layer: the Recentre button */
    _Atomic uint32_t open;     /* 140: layer: the panel is shown */
    _Atomic uint32_t consume;  /* 144: layer: bit h = hand h's trigger is the panel's */
    _Atomic uint32_t block;    /* 148: preload, every input tick: SOMVR_VMG_* (the gesture's gate; was pad0) */
    /* 152: the layer's counters: frames with the panel, its texture uploads, clicks that changed something, opens */
    _Atomic uint64_t n_frames, n_uploads, n_clicks, n_opens;
    _Atomic uint32_t practice; /* 184: layer: the last practice request (somvr_vm_pr_pack; seq 0 = none yet) */
    _Atomic uint32_t pstate;   /* 188: preload, every H4 frame: SOMVR_VMA_PR_* */
} somvr_vrmenu_shared;         /* 192 (v6; 184 in v5, 176 in v4, 168 in v3: the settings grew by 8 each time) */

_Static_assert(sizeof(somvr_settings) == 88, "somvr_settings size");
_Static_assert(offsetof(somvr_vrmenu_shared, avail) == 24 && offsetof(somvr_vrmenu_shared, seq) == 32 &&
                   offsetof(somvr_vrmenu_shared, set) == 40 && offsetof(somvr_vrmenu_shared, gen) == 128 &&
                   offsetof(somvr_vrmenu_shared, consume) == 144 && offsetof(somvr_vrmenu_shared, block) == 148 &&
                   offsetof(somvr_vrmenu_shared, n_frames) == 152 && offsetof(somvr_vrmenu_shared, practice) == 184 &&
                   offsetof(somvr_vrmenu_shared, pstate) == 188,
               "somvr_vrmenu_shared offsets");
_Static_assert(sizeof(somvr_vrmenu_shared) == 192, "somvr_vrmenu_shared size");
_Static_assert(sizeof(somvr_shared) == 2664 && SOMVR_SHARED_VERSION == 2, "somvr_shared stays v2");

static inline int somvr_vrmenu_ok(const somvr_vrmenu_shared *m) {
    return m && m->magic == SOMVR_VRMENU_MAGIC && m->version == SOMVR_VRMENU_VERSION &&
           m->size == (uint32_t)sizeof(somvr_vrmenu_shared) &&
           atomic_load_explicit(&((somvr_vrmenu_shared *)m)->active, memory_order_acquire) == 1;
}
/* the writer (the preload before active, the layer after): the values, then gen + 1 */
static inline void somvr_vrmenu_publish(somvr_vrmenu_shared *m, const somvr_settings *s) {
    somvr_seq_wbegin(&m->seq);
    memcpy((void *)&m->set, s, sizeof *s);
    somvr_seq_wend(&m->seq);
    atomic_fetch_add_explicit(&m->gen, 1, memory_order_release);
}
/* a reader: a consistent copy; 1 = read */
static inline int somvr_vrmenu_read(somvr_vrmenu_shared *m, somvr_settings *out) {
    return somvr_seq_read(&m->seq, out, &m->set, sizeof *out);
}

/* ==== 2. the settings ============================================================================================ */
enum { SOMVR_SK_TURN, SOMVR_SK_SNAP_DEG, SOMVR_SK_TURN_DPS, SOMVR_SK_VIGNETTE, SOMVR_SK_SNAP_BLINK_MS, SOMVR_SK_SEATED,
       SOMVR_SK_HEIGHT_CM, SOMVR_SK_HUD, SOMVR_SK_HUD_DIST_M, SOMVR_SK_HAND, SOMVR_SK_VIEW, SOMVR_SK_HAPTICS,
       SOMVR_SK_STEP, SOMVR_SK_DASH, SOMVR_SK_PHASE, SOMVR_SK_SWORD_ANGLE /* v4 */, SOMVR_SK_MUSIC /* v5 */,
       SOMVR_SK_PLAYER_SIZE, SOMVR_SK_CROUCH /* build 13 */, SOMVR_SK_CLIMB_FLING, SOMVR_SK_HANDS_SURFACE,
       SOMVR_SK_SWORD_WEIGHT /* build 16 */, SOMVR_SK_SWORD_WEIGHT_HAND /* build 17 */, SOMVR_SK_BOW_QUIVER,
       SOMVR_SK_COMBAT_BITE /* build 17, the bite */, SOMVR_SK_N };
#define SOMVR_SK_ALL ((1u << SOMVR_SK_N) - 1u)
#define SOMVR_SET_BLINK_MS 140u /* "Snap blink: On" without a SOMVR_SNAP_BLINK_MS: HoF2's quick blink (50 + 90 ms) */

void somvr_set_default(somvr_settings *s);  /* the switches' defaults (snap 30, 120 deg/s, no vignette ... 100 %, the
                                             * music as the game sets it) */
const char *somvr_set_key(int k);           /* "TURN" ... (NULL outside) */
const char *somvr_set_env(int k);           /* the environment variable key k overrides ("SOMVR_TURN"), NULL = none */
int somvr_set_find(const char *key);        /* key -> k, -1 = unknown */
/* key k from the text v (blanks around it ignored): 1 = understood and stored (numbers clamped to the key's range),
 * 0 = not understood (s unchanged) */
int somvr_set_put(somvr_settings *s, int k, const char *v);
/* key k's value as the file writes it (and as the environment takes it: snap|smooth, 30, 0.4, 1 ...) */
void somvr_set_get(const somvr_settings *s, int k, char *buf, size_t cap);
/* Lines of KEY=VALUE ('#' starts a comment; blanks, a CR and one pair of quotes round the value dropped; the last good
 * line of a key wins). Returns the mask of the keys taken; *bad = lines that were not empty and not taken (an unknown
 * key, a bad value, no '='); note: the first such line's reason. */
uint32_t somvr_set_parse(somvr_settings *s, const char *text, int *bad, char *note, size_t cap);
/* the file: a comment header, then KEY=VALUE for every key in mask (in key order). Returns the length (as snprintf). */
size_t somvr_set_format(const somvr_settings *s, uint32_t mask, char *buf, size_t cap);
uint32_t somvr_set_diff(const somvr_settings *a, const somvr_settings *b); /* bit k: key k's value differs */
/* a locale-free decimal: [+-]digits[.digits]; 1 = the whole text (blanks around it allowed) was one */
int somvr_set_num(const char *v, double *out);

/* ==== 3. the menu ================================================================================================ */
enum { SOMVR_VMI_TURN, SOMVR_VMI_SNAP, SOMVR_VMI_SPEED, SOMVR_VMI_VIGNETTE, SOMVR_VMI_BLINK, SOMVR_VMI_SEATED,
       SOMVR_VMI_HEIGHT, SOMVR_VMI_HUD, SOMVR_VMI_HUD_DIST, SOMVR_VMI_HAND, SOMVR_VMI_VIEW, SOMVR_VMI_HAPTICS,
       SOMVR_VMI_STEP, SOMVR_VMI_DASH, SOMVR_VMI_PHASE, SOMVR_VMI_SWORD /* v4 */, SOMVR_VMI_WEIGHT /* build 16 */,
       SOMVR_VMI_SWHAND /* build 17 */, SOMVR_VMI_BITE, SOMVR_VMI_ARROWS, SOMVR_VMI_MUSIC /* v5 */,
       SOMVR_VMI_SIZE, SOMVR_VMI_CROUCH /* build 13 */, SOMVR_VMI_FLING, SOMVR_VMI_SURFACE, SOMVR_VMI_RECENTER,
       SOMVR_VMI_CLOSE, SOMVR_VMI_N };
#define SOMVR_VMI_ROWS SOMVR_VMI_RECENTER /* the value rows; then the Recentre button and the close box */
/* Build 10: a value row whose feature this build does not have at all is not shown (the rows below close up): the
 * assisted step, the dash and the wraith phase without SOMVR_VMA_MOVES (SOMVR_GAMEMOVES=off: they never start, the
 * user's "I turn on wraith phase, nothing happens"), Music without SOMVR_VMA_MUSIC; build 13: Climb fling without
 * SOMVR_VMA_FLING, Hands at walls without SOMVR_VMA_SURFACE; build 16: Sword weight without SOMVR_VMA_WEIGHT; build 17:
 * Sword hand without SOMVR_VMA_SWHAND, Arrows without SOMVR_VMA_ARROWS (build 13's set: 17 rows of 35 px; build 16's 19
 * of 32; build 17's 21 of 28, the boxes 26; all 24 only with MOVES, which no build since 6 has: 25 px). A row that only
 * does not apply now (the snap angle with smooth turns, the Sword hand with the weight Off ...) stays, dimmed. 1 =
 * shown. */
int somvr_vm_shown(uint32_t avail, int item);
/* since v3: the page tab (the title bar: "Practice" on the settings page, "Settings" on the practice page) and the
 * practice page's items (SOMVR_VMI_N stays the settings page's count) */
enum { SOMVR_VMI_TAB = SOMVR_VMI_N, SOMVR_VMI_PR_GO, SOMVR_VMI_PR_BACK, SOMVR_VMI_PR_ORC, SOMVR_VMI_PR_SPAWN,
       SOMVR_VMI_PR_REMOVE,
       /* the settings page's bottom row next to Recentre (somvr_session.h: SOMVR_MARK, SOMVR_QUIT) */
       SOMVR_VMI_REPORT, SOMVR_VMI_QUIT,
       /* build 6 (re-xr-pipeline.md 28): the title bar is a tab strip [Settings] [Practice] [Controls]: SOMVR_VMI_TAB
        * is the Practice tab (only with SOMVR_VMA_PRACTICE), these two the others */
       SOMVR_VMI_TAB_SET, SOMVR_VMI_TAB_CTL,
       /* build 17: the practice page's Refill arrows + focus and Refill health (in the area only, as Spawn) */
       SOMVR_VMI_PR_ARROWS, SOMVR_VMI_PR_HEALTH, SOMVR_VMI_ALL };
enum { SOMVR_VMPG_SETTINGS = 0, SOMVR_VMPG_PRACTICE = 1, SOMVR_VMPG_CONTROLS = 2 }; /* the panel's pages */
/* the page a tab opens (SOMVR_VMPG_*), -1 = not a tab */
int somvr_vm_tab_page(int item);
/* The Controls page's lines (build 6): line i's key ("Walk") and what does it ("left stick"); returns the number of
 * lines (i out of range: NULL, NULL). Only what works in this build; the bug-mark chord is SOMVR_VM_CTL_REPORT. */
int somvr_vm_controls(int i, const char **key, const char **what); /* = _av(0, ...) */
/* build 13: the lines for the preload's avail (SOMVR_VMA_FLING, _VERBS, _DESCENT pick their lines); when more lines
 * than SOMVR_VM_CTL_FIT are wanted the optional ones go (Quit first: the Settings page's own button, then Sheathe) */
int somvr_vm_controls_av(uint32_t avail, int i, const char **key, const char **what);
/* ... drawn from y SOMVR_VM_CTL_Y0, one every somvr_vm_ctl_dy() pixels (SOMVR_VM_CTL_DY, closer when the lines, the
 * notes and the card's hint would not fit the panel; never under SOMVR_VM_CTL_DY_MIN); then SOMVR_VM_CTL_NOTES dim
 * lines (what is not in this build) and on the card its close hint */
#define SOMVR_VM_CTL_Y0 68
#define SOMVR_VM_CTL_DY 34
/* build 17: the closest lines: 28 px (was the font's 32-pixel cell; the ink the lines use is its rows 1..14 - an
 * apostrophe's top, a g's tail: 28 px at scale 2, so 28-pixel lines never touch; the Settings page's rows are 28 too) */
#define SOMVR_VM_CTL_DY_MIN 28
int somvr_vm_ctl_dy(void);                 /* = _av(0) */
int somvr_vm_ctl_dy_av(uint32_t avail);
#define SOMVR_VM_CTL_NOTES 2
/* the lines that fit with the notes and the card's hint at SOMVR_VM_CTL_DY_MIN (21 on the 768-pixel panel; 18 at the
 * font's 32-pixel cell before build 17) */
#define SOMVR_VM_CTL_FIT ((SOMVR_VM_TEX_H - 16 - 4 - SOMVR_VM_CTL_Y0) / SOMVR_VM_CTL_DY_MIN - SOMVR_VM_CTL_NOTES - 1)
const char *somvr_vm_controls_note(int i); /* the dim lines (i out of range: NULL); = _av(0, i) */
const char *somvr_vm_controls_note_av(uint32_t avail, int i); /* with SOMVR_VMA_VERBS: what a held orc does */
/* build 17: the avail the Controls page shows for the settings s: with SOMVR_VMA_ARROWS (the Arrows row) the quiver's
 * lines follow s->quiver (SOMVR_VMA_BOWRT with the string on RT, SOMVR_VMA_QVGRIP on the grip; neither: the string's own
 * Shoot line); otherwise avail (somvr_vm_draw draws the page and the card with it) */
uint32_t somvr_vm_ctl_avail(const somvr_settings *s, uint32_t avail);
enum { SOMVR_VMP_NONE, SOMVR_VMP_ROW, SOMVR_VMP_LESS, SOMVR_VMP_MORE, SOMVR_VMP_BUTTON };
typedef struct { int item, part; } somvr_vm_hit; /* item -1 = none */

const char *somvr_vm_label(int item);
/* the item can be used now (its feature exists: avail; and it matters: the snap angle with snap turns ...) */
int somvr_vm_enabled(const somvr_settings *s, uint32_t avail, int item);
/* the value as the panel shows it ("Snap", "45\x7f", "40%", "Auto" ...); "" for the buttons */
void somvr_vm_value(const somvr_settings *s, int item, char *buf, size_t cap);
enum { SOMVR_VMR_NONE = 0, SOMVR_VMR_CHANGED = 1, SOMVR_VMR_RECENTER = 2, SOMVR_VMR_CLOSE = 3, SOMVR_VMR_PAGE = 4,
       SOMVR_VMR_PRACTICE = 5,
       SOMVR_VMR_REPORT = 6,   /* the Report button: a bug mark (the layer counts somvr_session.report_req) */
       SOMVR_VMR_QUIT = 7 };   /* the Quit game button: the layer arms it, a second press within 5 s confirms */
/* A click on (item, part): LESS / MORE step the value down / up (held at the ends; a toggle flips), ROW the next value
 * (wrapping), the buttons act (the tab: PAGE, the other page; a practice button: PRACTICE, its request is
 * somvr_vm_practice_action(item)). A disabled item does nothing. Returns SOMVR_VMR_*. */
int somvr_vm_apply(somvr_settings *s, uint32_t avail, uint32_t blink_on_ms, int item, int part);
/* a practice page button's request (SOMVR_VMPR_*; 0 = not one) */
int somvr_vm_practice_action(int item);

/* the panel's texture and its rects (pixels: x right, y down = the quad's uv x 512 / x 768) */
#define SOMVR_VM_TEX_W 512
#define SOMVR_VM_TEX_H 768   /* 15 rows since v2 (the step, the dash, the phase): 640 before; 16 rows of 38 px since
                              * v4 (the sword angle), 40 px before; 17 of 36 px with the player size */
#define SOMVR_VM_SCALE 2     /* the font's scale: 16 x 32 pixel cells */
#define SOMVR_VM_W_M 0.20f   /* the quad: 20 x 30 cm (2560 texels per metre: about 18 per degree at 40 cm) */
#define SOMVR_VM_H_M 0.30f
/* {x, y, w, h} of (item, part) with the rows avail shows (somvr_vm_shown: the shown rows from the top, 38 px each, closer
 * when more than 16 are shown; the bottom row of buttons stays where it is); 0 = the part does not exist (the buttons
 * have only BUTTON, the rows ROW/LESS/MORE; a row not shown has none) */
int somvr_vm_rect_av(uint32_t avail, int item, int part, int rc[4]);
int somvr_vm_rect(int item, int part, int rc[4]); /* = somvr_vm_rect_av(0, ...): the rows every build shows */
/* the item and part under the pointer at uv (0..1, v down) on page SOMVR_VMPG_* (the tab and the close box on all) with
 * the rows avail shows; item -1 outside every part */
somvr_vm_hit somvr_vm_hit_test_av(uint32_t avail, int page, float u, float v);
somvr_vm_hit somvr_vm_hit_test(float u, float v);                /* = _av(0, SOMVR_VMPG_SETTINGS, u, v) */
somvr_vm_hit somvr_vm_hit_test_page(int page, float u, float v); /* = _av(0, page, u, v) */

typedef struct {
    somvr_settings set;
    uint32_t avail;
    somvr_vm_hit hover;  /* item -1 = none */
    int page;            /* SOMVR_VMPG_* (since v3) */
    int quit_armed;      /* the Quit game button was pressed once: it reads Confirm (red) */
    int card;            /* build 6: the Controls page shown as the arrival card in front of the player (no tabs, the
                          * title and a close hint instead) */
} somvr_vm_view;
/* The panel's picture: SOMVR_VM_TEX_W x _H RGBA8 (bgra: B and R swapped), premultiplied, the sRGB bytes; opaque inside
 * its rounded corners. A title, one row per item (label, value, the < > buttons), the Recentre button, the close box;
 * disabled items dimmed, the hovered row and part lit. The practice page (v->page): its state (avail's SOMVR_VMA_PR_*),
 * the Go / Back buttons, the orc type row, the Spawn / Remove buttons (build 17: the two refills, the defence lines);
 * the tab on both pages while SOMVR_VMA_PRACTICE. */
void somvr_vm_draw(const somvr_font *f, uint8_t *px, int bgra, const somvr_vm_view *v);

/* ---- the gesture and the panel's pose (the layer, once per frame; XR space, metres, XrPosef layout) -------------- */
typedef struct {
    float face_on, face_off; /* cos: the palm faces the eyes within 45 deg (on), no longer beyond 60 deg (off) */
    float look_on;           /* cos: the gesture counts only with the panel within 40 deg of the gaze */
    float look_off;          /* cos: beyond 60 deg of the gaze the panel is looked away from */
    float reach_m;           /* the hand within 0.8 m of the head */
    float hold_s;            /* the gesture held 0.5 s toggles the panel */
    float rearm_s;           /* ... once the palm has been turned away 0.25 s since the last toggle */
    float away_s;            /* looked away 0.5 s: closed */
    float lost_s;            /* the panel's hand untracked 1 s: closed */
    float up_m, out_m;       /* the panel's centre from the grip: 15 cm along the panel's up, 5 cm off the palm */
    float gate_s;            /* the gate (gin.block) holds this long after its cause ends (0.3 s) */
} somvr_vm_gcfg;
void somvr_vm_gcfg_default(somvr_vm_gcfg *c);

/* the palm's normal (unit, out of the palm) of hand h's grip pose: OpenXR's grip +X points out of the left palm and
 * into the right one */
void somvr_vm_palm(const somvr_posef *grip, int hand, float n[3]);
/* The panel on hand h: facing along the palm's normal, upright (its up = the world's +Y without its part along the
 * normal; the head's up when the palm faces up or down; head may be NULL), centred up_m along its up and out_m along the
 * normal from the grip. 0 = no valid pose (a bad grip quaternion). */
int somvr_vm_panel_pose(const somvr_posef *grip, int hand, const somvr_posef *head, const somvr_vm_gcfg *c,
                        somvr_posef *out);

enum { SOMVR_VMW_NONE, SOMVR_VMW_GESTURE, SOMVR_VMW_BUTTON, SOMVR_VMW_AWAY, SOMVR_VMW_LOST, SOMVR_VMW_CLOSE_BOX,
       /* SOMVR_VMW_OFF, then build 6: the panel's wrist turned over to show the watch (SOMVR_XR_MINIMAP=wrist) */
       SOMVR_VMW_OFF, SOMVR_VMW_TURNED };
const char *somvr_vm_why(int why);
typedef struct {
    int open;
    int armed;          /* the gesture may toggle (the palm was turned away since the last toggle) */
    int facing;         /* the palm faces the eyes (hysteresis) and the panel is looked at */
    double since;       /* facing since (s) */
    double away_t;      /* open: the panel out of the gaze since (-1 = in it) */
    double nf_t;        /* not facing since (-1 = facing) */
    double lost_t;      /* open: the hand untracked since (-1 = tracked) */
    uint64_t opens, closes;
    int why;            /* the last toggle's SOMVR_VMW_* */
    /* the gate: while gin.block is set and gate_s after it the closed panel's gesture is disarmed (a palm held through
     * it needs to turn away rearm_s and back: a fresh gesture) */
    double gate_t;      /* the last frame with gin.block set (-1 = never) */
    uint32_t gate_why;  /* the SOMVR_VMG_* bits of the gate's episode (OR'ed; cleared when it ends) */
    int gated;          /* this frame the gate held the closed panel's gesture */
    int held;           /* the palm faces through the gate now (counted once per episode in held_off) */
    uint64_t gate_frames, held_off; /* frames gated; gate episodes the palm faced through (opens refused) */
} somvr_vm_gstate;
void somvr_vm_greset(somvr_vm_gstate *g);
typedef struct {
    double t;           /* s, monotonic (the frames' display times) */
    int head_ok;        /* the head pose is valid */
    somvr_posef head;
    int hand_ok;        /* the panel hand's grip pose is valid (orientation and position) */
    somvr_posef grip;
    int hand;           /* the panel's hand (SOMVR_HAND_L unless the main hand is the left) */
    uint32_t toggles;   /* long presses of the menu button since the last frame */
    int busy;           /* the pointer is on the panel or holds a click: no gesture close, no look-away close */
    uint32_t block;     /* SOMVR_VMG_*: the hands are busy (somvr_vrmenu_shared.block): the gesture opens nothing */
} somvr_vm_gin;
/* One frame: the menu button's toggles, the palm gesture (palm toward the eyes, the hand within reach, the panel looked
 * at, held hold_s, armed), looking away (away_s, not busy), the hand lost (lost_s). Returns +1 opened, -1 closed, 0
 * neither (g->why the reason); *panel = this frame's panel pose when one could be made (*have_panel).
 * The gate: while in->block is set and gate_s after it, a closed panel's gesture is disarmed every frame, so it opens
 * nothing then and a palm held through it opens nothing when it ends (it must turn away rearm_s and face again hold_s:
 * a fresh gesture). The menu button still toggles (a deliberate press), an open panel's gesture still closes it. */
int somvr_vm_gesture(somvr_vm_gstate *g, const somvr_vm_gcfg *c, const somvr_vm_gin *in, somvr_posef *panel,
                     int *have_panel);
/* the gate's bits as words ("the bow+a climb hold", "nothing" for 0) */
void somvr_vm_gate_text(uint32_t bits, char *buf, size_t cap);

#endif
