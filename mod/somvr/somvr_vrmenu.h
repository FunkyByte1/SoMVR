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
 *    HANDS_SURFACE; build 19: DODGE, WRAITH_STEP, STEALTH_KILL, CROWD_PACE_MS, COUNTER_ICON, BOW_STRING: the
 *    environment variables of the same name; HUD=on|off,
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
#define SOMVR_VRMENU_VERSION 7u /* 2: STEP, DASH, PHASE (re-xr-pipeline.md 23): the settings 64 bytes; 3: the practice
                                 * page (re-xr-pipeline.md 27): practice, pstate, settings.orc; 168 bytes; 4: the sword
                                 * angle (re-xr-pipeline.md 29): settings.sword_deg, the settings 72 bytes, 176; 5: the
                                 * music volume (re-xr-pipeline.md 34): settings.music, the settings 80 bytes, 184; 6
                                 * (build 17, re-xr-pipeline.md 56): settings.sword_hand, .quiver and 6 spare bytes, the
                                 * settings 88 bytes, 192; 7 (build 19, re-xr-pipeline.md 73; t3/menu18's 56.8): avail
                                 * 64 bits (bits 0..31 kept), everything after it 8 bytes later; the settings 96 bytes
                                 * (the Fight page's six rows at 88..94; pad6 stays spare), 208 */

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
/* build 18 (re-xr-pipeline.md 56.8): the availability word is 64 bits (somvr_vrmenu v7; every avail parameter and
 * variable is uint64_t). Bits 32..63 are macros (an enum constant is an int): SOMVR_VMA_BIT(n). Clearing bits from a
 * 64-bit avail: ~(uint64_t)(...) - a ~ of the 32-bit constants above would clear bits 32..63 too */
#define SOMVR_VMA_BIT(n) (1ull << (n))
/* build 19 (re-xr-pipeline.md 73): the Fight page's rows that build 18 and 19 had as switches only. Each bit = the
 * feature runs in this session (its switch on, its hooks in), so its row is shown and changes it live:
 * SOMVR_DODGE=on with the step's core (A in a fight = the fixed 1.5 m dash, block17): the Dodge (A) row */
#define SOMVR_VMA_DODGE SOMVR_VMA_BIT(32)
#define SOMVR_VMA_WSTEP SOMVR_VMA_BIT(33)   /* SOMVR_WRAITH_STEP=on: the Wraith step row */
#define SOMVR_VMA_STEALTH SOMVR_VMA_BIT(34) /* SOMVR_STEALTH_KILL=on (with SOMVR_COMBAT): the Stealth kill row */
#define SOMVR_VMA_CROWD SOMVR_VMA_BIT(35)   /* SOMVR_FRONT=1 with SOMVR_THREAT (the pace's judge): the Crowd pace row */
#define SOMVR_VMA_CICON SOMVR_VMA_BIT(36)   /* SOMVR_COUNTER_ICON=hide, its two calls hooked: the Counter icon row */
#define SOMVR_VMA_STRING SOMVR_VMA_BIT(37)  /* SOMVR_BOW: the Bow string row (trigger / grip, taken while the bow is away;
                                             * the Controls page's bow lines follow it: somvr_vm_ctl_avail) */
#define SOMVR_VMA_JUMP SOMVR_VMA_BIT(38)    /* build 22: SOMVR_JUMP=on: the Controls page's Jump line */
#define SOMVR_VMA_RIDE SOMVR_VMA_BIT(41)    /* build 26: SOMVR_RIDE=on: the Controls page's Ride lines (39-40 left to the
                                             * helpers of builds 25-26 beside it) */
#define SOMVR_VMA_ARMY SOMVR_VMA_BIT(44)    /* build 27: SOMVR_ARMY=on: the Controls page's Map line names the click's hold
                                             * (re-xr-pipeline.md 110; 42-43 left to the round's other helpers) */
/* build 27 (re-xr-pipeline.md 112, som-b27-hla): every gesture on the Controls page, each line only where its feature
 * runs in this session (bits 47..59; 42..46 left to the other build 27 helpers) */
#define SOMVR_VMA_WALKDIR SOMVR_VMA_BIT(47)  /* the stick's walk pre-rotation runs (SOMVR_INPUT): the Walk toward row */
#define SOMVR_VMA_VAULT SOMVR_VMA_BIT(48)    /* SOMVR_VAULT=on: "Vault: A at the gold ring" */
#define SOMVR_VMA_ABOVE SOMVR_VMA_BIT(49)    /* SOMVR_ABOVE=on with its plunge: "Above: stab down from ledge" */
#define SOMVR_VMA_BRAND SOMVR_VMA_BIT(50)    /* SOMVR_VERBS_BRAND=on: "Brand: L palm on head + LT" */
#define SOMVR_VMA_RALLY SOMVR_VMA_BIT(51)    /* SOMVR_VERBS_RALLY=on: "Rally: L hand high + LT" */
#define SOMVR_VMA_DISMISS SOMVR_VMA_BIT(52)  /* SOMVR_VERBS_DISMISS=on: a Domination's (B) Dismiss by B */
#define SOMVR_VMA_COMMAND SOMVR_VMA_BIT(53)  /* SOMVR_VERBS_COMMAND=on: its (Y) Command by Y */
#define SOMVR_VMA_FAILED SOMVR_VMA_BIT(54)   /* SOMVR_USE_DEATH=on: Mission Failed's A / B */
#define SOMVR_VMA_NEMESIS SOMVR_VMA_BIT(55)  /* SOMVR_USE_SKIP=on: the Nemesis result pages closed by B */
#define SOMVR_VMA_DOWN SOMVR_VMA_BIT(56)     /* SOMVR_CLIMB_DOWN_OVER=on: "Down: grip over the edge" */
#define SOMVR_VMA_OVERHANG SOMVR_VMA_BIT(57) /* SOMVR_CLIMB_MANTLE_OVERHANG=on: "Overhang: A at its gold ring" */
#define SOMVR_VMA_COUNTER SOMVR_VMA_BIT(58)  /* SOMVR_RIDE_COUNTER on (with SOMVR_RIDE): a caragor's leap blocked */
#define SOMVR_VMA_DOMHOLD SOMVR_VMA_BIT(59)  /* SOMVR_RIDE_DOM_HOLD_MS > 0: "Hold tight!" held by the closed hand */
#define SOMVR_VMA_OVERORC SOMVR_VMA_BIT(60)  /* SOMVR_WRAITH_STEP_VAULT_CM > 0: the wraith step aimed at an orc = over it */
#define SOMVR_VMA_STRUGGLE SOMVR_VMA_BIT(61) /* SOMVR_STRUGGLE=thrust: the last chance won by the hand and a stab */
#define SOMVR_VMA_MANTLE SOMVR_VMA_BIT(62)   /* SOMVR_CLIMB_MANTLE (glide|blink): A at a top's gold ring = onto it */
/* build 27 (re-xr-pipeline.md 119, som-b27-perf2): the layer's own bit (never the preload's; 43 = the top of the round's
 * free 42..43): the runtime has XR_FB_display_refresh_rate enabled (SOMVR_XR_REFRESH not off) - the Refresh rate row */
#define SOMVR_VMA_REFRESH SOMVR_VMA_BIT(43)
/* (39-40: never taken by the build 25-26 helpers they were kept for) */
#define SOMVR_VMA_GLYPH SOMVR_VMA_BIT(39)    /* SOMVR_GLYPH=head: an Ithildin glyph collected by the look in the vision */
#define SOMVR_VMA_SHOVE SOMVR_VMA_BIT(40)    /* SOMVR_SHOVE=on: a shield shoved aside by the free left hand */
/* build 27 (re-xr-pipeline.md 114, som-b27-abilities; 45-46 of the 42..46 left to the round's helpers, 44 the army's) */
#define SOMVR_VMA_FLASH SOMVR_VMA_BIT(45)    /* SOMVR_ABIL=on with its flash: "Flash: RT + L palm push out" */
#define SOMVR_VMA_GROUNDK SOMVR_VMA_BIT(46)  /* SOMVR_ABIL=on with its ground finisher: "Downed: stab down into him" */
#define SOMVR_VMA_ABIL2 SOMVR_VMA_BIT(42)    /* build 27c (som-b27-abilities2, docs 118): SOMVR_ABIL_IGNITE and _WHISTLE
                                             * both on: "Ignite", "Whistle" (the quick throw, off, has no line) */
#define SOMVR_VMA_FIGHT_ROWS                                                                                           \
    (SOMVR_VMA_SWORD | SOMVR_VMA_WEIGHT | SOMVR_VMA_SWHAND | SOMVR_VMA_BITE | SOMVR_VMA_ARROWS | SOMVR_VMA_DODGE |     \
     SOMVR_VMA_WSTEP | SOMVR_VMA_STEALTH | SOMVR_VMA_CROWD | SOMVR_VMA_CICON | SOMVR_VMA_STRING)
/* 38..62: free (the next helpers'; take them in order, from 38) */
/* build 21 (re-xr-pipeline.md 82): the layer's own bit (never the preload's; taken from the top): SOMVR_XR_VM_TEST=on -
 * the tab strip is [Settings] [Practice] [Controls] [Test] across the panel (the four fill it: the X box leaves the
 * panel's pages - the card keeps it; the layer clears the bit for the card) and the Test page (SOMVR_VMPG_TEST) */
#define SOMVR_VMA_TESTPAGE SOMVR_VMA_BIT(63)
/* the practice page's requests (somvr_vrmenu_shared.practice; = somvr_practice.h's SOMVR_PRA_*) */
enum { SOMVR_VMPR_NONE = 0, SOMVR_VMPR_GO = 1, SOMVR_VMPR_BACK = 2, SOMVR_VMPR_SPAWN = 3, SOMVR_VMPR_REMOVE = 4,
       /* build 17 (5 = the practice's own status word): the bow's arrows and Focus full, Talion's health full */
       SOMVR_VMPR_ARROWS = 6, SOMVR_VMPR_HEALTH = 7,
       /* build 18: an orc far ahead with its back to Talion (unaware: re-xr-pipeline.md 56.9) */
       SOMVR_VMPR_FAR = 8,
       /* build 19: Spawn group (3-4 orcs of mixed types around Talion) */
       SOMVR_VMPR_GROUP = 9,
       /* build 22: Captain (the save's next Nemesis captain) */
       SOMVR_VMPR_CAPTAIN = 10 };
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
    uint32_t orc;              /* 60: the practice page's orc type 0 infantry, 1 shield, 2 berserker, build 22: 3 caragor,
                                * 4 ghul, 5 graug (no file key) */
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
    uint8_t move;              /* 83: build 27: the walk toward 0 the head, 1 the left hand (SOMVR_MOVE head|hand; needs
                                * SOMVR_VMA_WALKDIR; was pad6[0]: a preload that sets no SOMVR_VMA_WALKDIR ignores it) */
    uint8_t refresh;           /* 84: build 27: the headset's refresh rate asked, Hz (72, 80, 90, 120; 0 = the runtime's
                                * own; SOMVR_XR_REFRESH; the layer's SOMVR_VMA_REFRESH; was pad6[0]: a layer that sets no
                                * SOMVR_VMA_REFRESH ignores it) */
    uint8_t pad6[3];           /* 85: spare (0) for the next rows: a byte each, no new version */
    /* v7 (build 19, re-xr-pipeline.md 73): the Fight page's rows (each needs its SOMVR_VMA_* bit; a key not in the
     * file follows its switch) */
    uint8_t dodge;             /* 88: the A dodge 0 off, 1 on (SOMVR_DODGE; SOMVR_VMA_DODGE) */
    uint8_t wstep;             /* 89: the wraith step (the right stick's aimed blink) 0 off, 1 on (SOMVR_WRAITH_STEP) */
    uint8_t stealth;           /* 90: the VR stealth kill 0 off, 1 on (SOMVR_STEALTH_KILL) */
    uint8_t counter_icon;      /* 91: the game's counter icon 0 hidden, 1 shown (SOMVR_COUNTER_ICON hide|game) */
    uint16_t crowd_ms;         /* 92: the crowd's pace, ms between blows at Talion, 0 = off (SOMVR_CROWD_PACE_MS) */
    uint8_t bow_string;        /* 94: the bow's string held by 0 the grip, 1 the trigger (SOMVR_BOW_STRING) */
    uint8_t pad7;              /* 95: spare (0) */
} somvr_settings;              /* 96 (88 in v6) */

typedef struct somvr_vrmenu_shared {
    uint64_t magic;            /*   0 */
    uint32_t version, size;    /*   8, 12 */
    _Atomic uint32_t active;   /*  16: set last by the preload (release) */
    int32_t pid;               /*  20 */
    uint64_t avail;            /*  24: SOMVR_VMA_* (the preload's; the layer adds FLOOR and drops HUD without its quad);
                                * v7: 64 bits (was 32) */
    uint32_t blink_on_ms;      /*  32: the blink length "Snap blink: On" selects */
    uint32_t pad7;             /*  36 */
    _Atomic uint64_t seq;      /*  40: seqlock over set */
    somvr_settings set;        /*  48 */
    _Atomic uint32_t gen;      /* 136: +1 per change of set (after it) */
    _Atomic uint32_t toggles;  /* 140: preload: long presses of the left menu button */
    _Atomic uint32_t recenters;/* 144: layer: the Recentre button */
    _Atomic uint32_t open;     /* 148: layer: the panel is shown */
    _Atomic uint32_t consume;  /* 152: layer: bit h = hand h's trigger is the panel's */
    _Atomic uint32_t block;    /* 156: preload, every input tick: SOMVR_VMG_* (the gesture's gate; was pad0) */
    /* 160: the layer's counters: frames with the panel, its texture uploads, clicks that changed something, opens */
    _Atomic uint64_t n_frames, n_uploads, n_clicks, n_opens;
    _Atomic uint32_t practice; /* 192: layer: the last practice request (somvr_vm_pr_pack; seq 0 = none yet) */
    _Atomic uint32_t pstate;   /* 204: preload, every H4 frame: SOMVR_VMA_PR_* (bits 6..9: 32 bits are enough) */
} somvr_vrmenu_shared;         /* 208 (v7: avail 64 bits, the settings 96; 192 in v6, 184 in v5, 176 in v4, 168 in v3) */

_Static_assert(sizeof(somvr_settings) == 96 && offsetof(somvr_settings, dodge) == 88 &&
                   offsetof(somvr_settings, crowd_ms) == 92 && offsetof(somvr_settings, bow_string) == 94,
               "somvr_settings size");
_Static_assert(offsetof(somvr_vrmenu_shared, avail) == 24 && offsetof(somvr_vrmenu_shared, blink_on_ms) == 32 &&
                   offsetof(somvr_vrmenu_shared, seq) == 40 && offsetof(somvr_vrmenu_shared, set) == 48 &&
                   offsetof(somvr_vrmenu_shared, gen) == 144 && offsetof(somvr_vrmenu_shared, consume) == 160 &&
                   offsetof(somvr_vrmenu_shared, block) == 164 && offsetof(somvr_vrmenu_shared, n_frames) == 168 &&
                   offsetof(somvr_vrmenu_shared, practice) == 200 && offsetof(somvr_vrmenu_shared, pstate) == 204,
               "somvr_vrmenu_shared offsets");
_Static_assert(sizeof(somvr_vrmenu_shared) == 208 && sizeof(((somvr_vrmenu_shared *)0)->avail) == 8,
               "somvr_vrmenu_shared size");
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
       SOMVR_SK_COMBAT_BITE /* build 17, the bite */,
       /* build 19 (re-xr-pipeline.md 73): the Fight page's new rows */
       SOMVR_SK_DODGE, SOMVR_SK_WRAITH_STEP, SOMVR_SK_STEALTH_KILL, SOMVR_SK_CROWD_PACE_MS, SOMVR_SK_COUNTER_ICON,
       SOMVR_SK_BOW_STRING,
       /* build 27 (re-xr-pipeline.md 112): the walk's direction, HL:A's "Continuous (Head)" / "(Hand)" */
       SOMVR_SK_MOVE,
       /* build 27 (re-xr-pipeline.md 119): the headset's refresh rate (XR_REFRESH=off|0|72..120, SOMVR_XR_REFRESH) */
       SOMVR_SK_XR_REFRESH, SOMVR_SK_N };
/* build 27: the key masks are 64 bits (parse, format, diff, the file mask; 32 bits held 31 keys) */
_Static_assert(SOMVR_SK_N <= 63, "the settings keys fit a 64-bit mask");
#define SOMVR_SK_ALL ((1ull << SOMVR_SK_N) - 1ull)
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
uint64_t somvr_set_parse(somvr_settings *s, const char *text, int *bad, char *note, size_t cap);
/* the file: a comment header, then KEY=VALUE for every key in mask (in key order). Returns the length (as snprintf). */
size_t somvr_set_format(const somvr_settings *s, uint64_t mask, char *buf, size_t cap);
uint64_t somvr_set_diff(const somvr_settings *a, const somvr_settings *b); /* bit k: key k's value differs */
/* a locale-free decimal: [+-]digits[.digits]; 1 = the whole text (blanks around it allowed) was one */
int somvr_set_num(const char *v, double *out);

/* ==== 3. the menu ================================================================================================ */
enum { SOMVR_VMI_TURN, SOMVR_VMI_SNAP, SOMVR_VMI_SPEED, SOMVR_VMI_VIGNETTE, SOMVR_VMI_BLINK, SOMVR_VMI_SEATED,
       SOMVR_VMI_HEIGHT, SOMVR_VMI_HUD, SOMVR_VMI_HUD_DIST, SOMVR_VMI_HAND, SOMVR_VMI_VIEW, SOMVR_VMI_HAPTICS,
       SOMVR_VMI_STEP, SOMVR_VMI_DASH, SOMVR_VMI_PHASE, SOMVR_VMI_SWORD /* v4 */, SOMVR_VMI_WEIGHT /* build 16 */,
       SOMVR_VMI_SWHAND /* build 17 */, SOMVR_VMI_BITE, SOMVR_VMI_ARROWS, SOMVR_VMI_MUSIC /* v5 */,
       SOMVR_VMI_SIZE, SOMVR_VMI_CROUCH /* build 13 */, SOMVR_VMI_FLING, SOMVR_VMI_SURFACE,
       /* build 19 (re-xr-pipeline.md 73): on the Fight page (somvr_vm_row_page) */
       SOMVR_VMI_STRING, SOMVR_VMI_DODGE, SOMVR_VMI_WSTEP, SOMVR_VMI_STEALTH, SOMVR_VMI_CROWD, SOMVR_VMI_CICON,
       /* build 27 (re-xr-pipeline.md 112): Walk toward (Head / Hand), shown under the turn's rows (row_slot's order) */
       SOMVR_VMI_MOVE,
       /* build 27 (re-xr-pipeline.md 119): Refresh rate (72 / 80 / 90 / 120 Hz), the Settings page's last row, with the
        * layer's SOMVR_VMA_REFRESH only */
       SOMVR_VMI_REFRESH,
       SOMVR_VMI_RECENTER, SOMVR_VMI_CLOSE, SOMVR_VMI_N };
#define SOMVR_VMI_ROWS SOMVR_VMI_RECENTER /* the value rows; then the Recentre button and the close box */
/* Build 10: a value row whose feature this build does not have at all is not shown (the rows below close up): the
 * assisted step, the dash and the wraith phase without SOMVR_VMA_MOVES (SOMVR_GAMEMOVES=off: they never start, the
 * user's "I turn on wraith phase, nothing happens"), Music without SOMVR_VMA_MUSIC; build 13: Climb fling without
 * SOMVR_VMA_FLING, Hands at walls without SOMVR_VMA_SURFACE; build 16: Sword weight without SOMVR_VMA_WEIGHT; build 17:
 * Sword hand without SOMVR_VMA_SWHAND, Arrows without SOMVR_VMA_ARROWS (build 13's set: 17 rows of 35 px; build 16's 19
 * of 32; build 17's 21 of 28, the boxes 26; all 24 only with MOVES, which no build since 6 has: 25 px). A row that only
 * does not apply now (the snap angle with smooth turns, the Sword hand with the weight Off ...) stays, dimmed. 1 =
 * shown. */
int somvr_vm_shown(uint64_t avail, int item);
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
       SOMVR_VMI_PR_ARROWS, SOMVR_VMI_PR_HEALTH,
       /* build 18: the practice page's Spawn far orc (unaware) (in the area only, as Spawn) */
       SOMVR_VMI_PR_FAR,
       /* build 19: the practice page's Spawn group (beside Spawn an orc; in the area only, as Spawn) */
       SOMVR_VMI_PR_GROUP,
       /* build 19 (re-xr-pipeline.md 73): the Settings page's last row "Fight settings >" (the Fight page) and the Fight
        * page's last row "< Settings" (back); both page buttons, shown while a Fight row is (SOMVR_VMA_FIGHT_ROWS) */
       SOMVR_VMI_PG_FIGHT, SOMVR_VMI_PG_BACK,
       /* build 21 (re-xr-pipeline.md 82; SOMVR_VMA_TESTPAGE): the Test tab, the Test page's Prev / Next buttons */
       SOMVR_VMI_TAB_TEST, SOMVR_VMI_TEST_PREV, SOMVR_VMI_TEST_NEXT,
       /* build 22 (re-xr-pipeline.md 88, SOMVR_CAPTAIN): the practice page's Captain (beside Remove; in the area only, as
        * Spawn): the save's next Nemesis captain */
       SOMVR_VMI_PR_CAPTAIN,
       /* build 27 (re-xr-pipeline.md 112): the Controls page's Prev / Next (its groups paged; where the Test page's are) */
       SOMVR_VMI_CTL_PREV, SOMVR_VMI_CTL_NEXT, SOMVR_VMI_ALL };
/* the panel's pages; build 19: SOMVR_VMPG_FIGHT = the Settings tab's second page (the sword, the bow, the fight: its
 * tab lit as the Settings page's) */
enum { SOMVR_VMPG_SETTINGS = 0, SOMVR_VMPG_PRACTICE = 1, SOMVR_VMPG_CONTROLS = 2, SOMVR_VMPG_FIGHT = 3,
       SOMVR_VMPG_TEST = 4 /* build 21: what to test in this build (its what-to-test.txt; text only, paged) */ };
/* the page a tab or a page button opens (SOMVR_VMPG_*), -1 = not one */
int somvr_vm_tab_page(int item);
/* build 19: the page a value row is on: SOMVR_VMPG_FIGHT for the sword's, the bow's and the fight's rows (Sword angle,
 * Sword weight, Sword hand, Blade bite, Arrows, Bow string, Dodge, Wraith step, Stealth kill, Crowd pace, Counter icon),
 * SOMVR_VMPG_SETTINGS for the rest; -1 = not a value row */
int somvr_vm_row_page(int item);
/* build 19: a Fight page row's two lines of help (shown under the page's rows while the row is hovered); 0 = none */
int somvr_vm_help(int item, const char **l1, const char **l2);
/* The Controls page (build 6; build 27, re-xr-pipeline.md 112: every VR control and gesture this build has, in groups -
 * Moving, Climbing, The sword, Orcs, Bow and wraith, Beasts, Menus and help - each line only where its feature runs
 * (avail), paged with Prev / Next, a group kept whole on a page when it fits one). Line i (every group's, in page order;
 * no titles): its key ("Walk") and what does it ("left stick"); returns the number of lines (i out of range: NULL,
 * NULL). The bug-mark chord is SOMVR_VM_CTL_REPORT. */
int somvr_vm_controls(int i, const char **key, const char **what); /* = _av(0, ...) */
int somvr_vm_controls_av(uint64_t avail, int i, const char **key, const char **what);
enum { SOMVR_VMC_NONE = 0, SOMVR_VMC_GROUP = 1, SOMVR_VMC_LINE = 2 };
const char *somvr_vm_ctl_group(int g);       /* group g's title (NULL outside) */
int somvr_vm_ctl_pages(uint64_t avail);      /* the pages (1 ..) */
/* entry i of page (0 ..): SOMVR_VMC_GROUP (key = its title, what NULL) or SOMVR_VMC_LINE; *y = its top (texels);
 * SOMVR_VMC_NONE past the page's last */
int somvr_vm_ctl_entry(uint64_t avail, int page, int i, const char **key, const char **what, int *y);
int somvr_vm_ctl_turn(uint64_t avail, int page, int dir); /* the page after Prev (-1) / Next (+1), clamped */
/* the arrival card's lines (the first steps: walk, turn, crouch, the sword, the block, the climb, use, the menu), then
 * the card says where the rest is; returns their number */
int somvr_vm_ctl_card(uint64_t avail, int i, const char **key, const char **what);
#define SOMVR_VM_CTL_Y0 68      /* the first entry's top */
#define SOMVR_VM_CTL_DY 28      /* an entry's height (the font's ink 28 at scale 2: lines never touch; builds 17-26's too) */
#define SOMVR_VM_CTL_GAP 6      /* more before a group's title (not at a page's top) */
#define SOMVR_VM_CTL_BOTTOM (SOMVR_VM_TEST_NAV_Y - 4) /* the entries end above the Prev / Next row */
#define SOMVR_VM_CTL_KEY_W 8    /* a key's characters at most */
#define SOMVR_VM_CTL_WHAT_X (20 + 16 * (SOMVR_VM_CTL_KEY_W + 1)) /* the what column (16 px a character) */
#define SOMVR_VM_CTL_WHAT_W 20  /* its characters at most (to x 484) */
#define SOMVR_VM_CARD_DY 34     /* the card's lines */
/* build 17: the avail the Controls page shows for the settings s: with SOMVR_VMA_ARROWS (the Arrows row) the quiver's
 * lines follow s->quiver (SOMVR_VMA_BOWRT with the string on RT, SOMVR_VMA_QVGRIP on the grip; neither: the string's own
 * Shoot line); build 27: the Dodge, Wraith step and Stealth kill rows Off take their lines away; otherwise avail
 * (somvr_vm_draw draws the page and the card with it) */
uint64_t somvr_vm_ctl_avail(const somvr_settings *s, uint64_t avail);
enum { SOMVR_VMP_NONE, SOMVR_VMP_ROW, SOMVR_VMP_LESS, SOMVR_VMP_MORE, SOMVR_VMP_BUTTON };
typedef struct { int item, part; } somvr_vm_hit; /* item -1 = none */

const char *somvr_vm_label(int item);
/* the item can be used now (its feature exists: avail; and it matters: the snap angle with snap turns ...) */
int somvr_vm_enabled(const somvr_settings *s, uint64_t avail, int item);
/* the value as the panel shows it ("Snap", "45\x7f", "40%", "Auto" ...); "" for the buttons */
void somvr_vm_value(const somvr_settings *s, int item, char *buf, size_t cap);
enum { SOMVR_VMR_NONE = 0, SOMVR_VMR_CHANGED = 1, SOMVR_VMR_RECENTER = 2, SOMVR_VMR_CLOSE = 3, SOMVR_VMR_PAGE = 4,
       SOMVR_VMR_PRACTICE = 5,
       SOMVR_VMR_REPORT = 6,   /* the Report button: a bug mark (the layer counts somvr_session.report_req) */
       SOMVR_VMR_QUIT = 7,     /* the Quit game button: the layer arms it, a second press within 5 s confirms */
       SOMVR_VMR_SCROLL = 8 }; /* build 21: the Test page's Prev / Next: the layer turns its page (somvr_vm_test_turn) */
/* A click on (item, part): LESS / MORE step the value down / up (held at the ends; a toggle flips), ROW the next value
 * (wrapping), the buttons act (the tab: PAGE, the other page; a practice button: PRACTICE, its request is
 * somvr_vm_practice_action(item)). A disabled item does nothing. Returns SOMVR_VMR_*. */
int somvr_vm_apply(somvr_settings *s, uint64_t avail, uint32_t blink_on_ms, int item, int part);
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
int somvr_vm_rect_av(uint64_t avail, int item, int part, int rc[4]);
int somvr_vm_rect(int item, int part, int rc[4]); /* = somvr_vm_rect_av(0, ...): the rows every build shows */
/* the item and part under the pointer at uv (0..1, v down) on page SOMVR_VMPG_* (the tab and the close box on all) with
 * the rows avail shows; item -1 outside every part */
somvr_vm_hit somvr_vm_hit_test_av(uint64_t avail, int page, float u, float v);
somvr_vm_hit somvr_vm_hit_test(float u, float v);                /* = _av(0, SOMVR_VMPG_SETTINGS, u, v) */
somvr_vm_hit somvr_vm_hit_test_page(int page, float u, float v); /* = _av(0, page, u, v) */

/* ---- build 21 (re-xr-pipeline.md 82): the Test page's text ------------------------------------------------------
 * The build's mod/somvr/assets/what-to-test.txt (the layer reads it once a session, next to its library: ../../assets
 * from vklayer/build; the freeze archives mod/, so each frozen build carries its own). Lines starting with # are
 * comments; the first other non-blank line is the title (the page's title line, on every page); "- " (or "* ") starts
 * an item (a bullet); a line under an item without a blank line between continues it; other lines are plain text; a
 * blank line ends an item. Wrapped by words to the panel (SOMVR_VM_TEST_COLS, an item's text SOMVR_VM_TEST_COLS - 2
 * after its bullet), paged to the room above the Prev / Next row (an item kept whole on a page when it fits one).
 * No file (or nothing in it): the title "What to test" and a line saying so - never a failure. */
#define SOMVR_VM_TEST_BYTES 8192  /* the most of the file read */
#define SOMVR_VM_TEST_LINES 160   /* wrapped lines kept (more: the last says "...") */
#define SOMVR_VM_TEST_COLS 29     /* characters a line (16 px each from x 20) */
#define SOMVR_VM_TEST_Y0 70       /* the title's first line */
#define SOMVR_VM_TEST_DY 30       /* a line's height (the font's ink 28 at scale 2: lines never touch) */
#define SOMVR_VM_TEST_GAP 8       /* more before an item */
#define SOMVR_VM_TEST_NAV_Y 684   /* the Prev / Next row (56 high, its page number between them; the Recentre row's) */
/* a line's kind: the title's, an item's first (its bullet) and next lines (indented under its text), a plain
 * paragraph's first and next, a dim note (no file; "more in the file") */
enum { SOMVR_VMT_TITLE = 1, SOMVR_VMT_ITEM = 2, SOMVR_VMT_MORE = 3, SOMVR_VMT_TEXT = 4, SOMVR_VMT_TEXT2 = 5,
       SOMVR_VMT_NOTE = 6 };
typedef struct {
    int have;                 /* 1: from a file with something in it */
    int n, pages, n_title;    /* lines (the title's first), pages (1..), the title's lines (1..2, on every page) */
    int truncated;            /* more than SOMVR_VM_TEST_LINES lines or SOMVR_VM_TEST_BYTES: cut */
    char line[SOMVR_VM_TEST_LINES][SOMVR_VM_TEST_COLS + 1];
    uint8_t kind[SOMVR_VM_TEST_LINES]; /* SOMVR_VMT_* */
    uint8_t page[SOMVR_VM_TEST_LINES]; /* the page a line is on (the title's: 0, drawn on every page) */
    int16_t y[SOMVR_VM_TEST_LINES];    /* its top (texels) */
} somvr_vm_text;
/* text (len bytes, need not end in 0; NULL = no file) -> t; returns t->pages */
int somvr_vm_text_parse(somvr_vm_text *t, const char *text, size_t len);
/* the page after Prev (dir -1) or Next (+1) from page (clamped to 0 .. pages - 1) */
int somvr_vm_test_turn(const somvr_vm_text *t, int page, int dir);

typedef struct {
    somvr_settings set;
    uint64_t avail;      /* v7: 64 bits */
    somvr_vm_hit hover;  /* item -1 = none */
    int page;            /* SOMVR_VMPG_* (since v3) */
    int quit_armed;      /* the Quit game button was pressed once: it reads Confirm (red) */
    int card;            /* build 6: the Controls page shown as the arrival card in front of the player (no tabs, the
                          * title and a close hint instead) */
    int lock;            /* build 20: hover is the pointer's lock (SOMVR_XR_PTR_SNAP): the part outlined as well */
    const somvr_vm_text *test; /* build 21: the Test page's text (NULL: the page says there is none) */
    int test_page;             /* ... the page shown (0 ..) */
    int ctl_page;              /* build 27: the Controls page's page shown (0 ..) */
} somvr_vm_view;
/* The panel's picture: SOMVR_VM_TEX_W x _H RGBA8 (bgra: B and R swapped), premultiplied, the sRGB bytes; opaque inside
 * its rounded corners. A title, one row per item (label, value, the < > buttons), the Recentre button, the close box;
 * disabled items dimmed, the hovered row and part lit. The practice page (v->page): its state (avail's SOMVR_VMA_PR_*),
 * the Go / Back buttons, the orc type row, the Spawn / Remove buttons (build 17: the two refills, the defence lines;
 * build 19: Spawn group beside Spawn an orc);
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
       SOMVR_VMW_OFF, SOMVR_VMW_TURNED,
       /* build 20 (the layer's SOMVR_XR_VRMENU_BOW=close): the bow came out in the panel's hand */
       SOMVR_VMW_BOW };
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

/* ---- build 20: menus that hold still and a pointer that locks on (docs/re-xr-pipeline.md 80) --------------------- */
/* The pointer's lock on the panel's parts (SOMVR_XR_PTR_SNAP, the layer; not with a fingertip's touch): the part under
 * the pointer's point locks; a locked part stays locked while the point stays within hold_deg (seen from the hand:
 * dist x tan, at least min_m) of its own area, so a shake does not change it (a row's own area leaves out its < >
 * boxes: the point on a box takes it from the row at once - the small target first - and the box then holds); on no
 * part the nearest one that can be used within magnet_deg locks. The press goes to the locked part; the dot is drawn on
 * it (the point kept inside it). */
typedef struct {
    float hold_deg;     /* 1.0 */
    float magnet_deg;   /* 2.0 */
    float min_m;        /* 0.003 */
} somvr_vm_snapcfg;
void somvr_vm_snapcfg_default(somvr_vm_snapcfg *c);
typedef struct {
    somvr_vm_hit lock;  /* the locked part (item -1 = none) */
    somvr_vm_hit raw;   /* the bare hit test's part (build 19's hover) */
    uint32_t changes, raw_changes; /* how often each changed */
} somvr_vm_snap;
void somvr_vm_snap_reset(somvr_vm_snap *s);
/* One frame with the pointer on the panel: uv its point, dist m from the hand, the quad quad_w_m wide; page (card: the
 * card's close box alone); returns the locked part, dot = the uv to draw the dot at. */
somvr_vm_hit somvr_vm_snap_step(somvr_vm_snap *s, const somvr_vm_snapcfg *c, const somvr_settings *set, uint64_t avail,
                                int page, int card, const float uv[2], float dist, float quad_w_m, float dot[2]);

/* The panel held steady (SOMVR_XR_PANEL_STEADY, the layer): the panel shown stays put while the hand's panel pose stays
 * within hold_m and hold_deg of it, follows by the excess beyond (1:1: a real move at once, never more than that
 * behind), and inside eases toward the hand, the faster the farther ((offset / radius)^2 of the rate at the edge, 1 /
 * settle_s: a tremor's small offsets barely move it, a held-still offset settles). Restarted at the hand's pose at the
 * start and after a gap over 0.25 s. */
typedef struct {
    float hold_m;       /* 0.015 */
    float hold_deg;     /* 3 */
    float settle_s;     /* 0.35 */
} somvr_vm_stcfg;
void somvr_vm_stcfg_default(somvr_vm_stcfg *c);
/* SOMVR_XR_PANEL_STEADY: unset/off/0 = 0 (build 19's panel on the hand); on/1 = 1 with the defaults; "CM,DEG,MS" = 1
 * with those (0 < CM <= 10, 0 < DEG <= 20, 0 <= MS <= 3000); anything else 0 and *bad = 1 */
int somvr_vm_stcfg_parse(const char *v, somvr_vm_stcfg *c, int *bad);
typedef struct {
    int have;
    somvr_posef p;      /* the pose shown */
    double t;
    uint64_t frames, follows, restarts;
} somvr_vm_steady;
void somvr_vm_steady_reset(somvr_vm_steady *s);
/* one frame: raw = the hand's panel pose at t (s); out = the pose to show */
void somvr_vm_steady_step(somvr_vm_steady *s, const somvr_vm_stcfg *c, const somvr_posef *raw, double t,
                          somvr_posef *out);
/* the quad's corners (top left, top right, bottom left, bottom right) */
void somvr_vm_corners(const somvr_posef *p, const float size[2], float out[4][3]);

#endif
