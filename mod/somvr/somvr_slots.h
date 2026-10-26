/* somvr_slots.h - SOMVR_SLOTS=1: the body slots (HG-7, design-vr.md plan item 7 and section 3 "Body slots", design-hla.md
 * 2.3; docs/re-xr-pipeline.md 21). Urfael over the main hand's shoulder, Acharn at the off hand's hip (or the lower
 * back): reach there and squeeze to draw, squeeze there again (or let a held grip go there) to put it back, a double
 * squeeze anywhere draws the sword at once; a haptic tick when a hand enters its slot. Pure: no globals, no game reads,
 * no OpenXR. Tested by tests/test_slots.c; somvr.c's SOMVR_SLOTS region reads the game's state (the sword's sheathed
 * flag, the dagger's node), calls the game's own draw and sheathe (FUN_023116f0) and the dagger's re-attach
 * (FUN_0231e2e0), refuses the game's own sheathe of a sword the player holds, plays the haptics and the sounds.
 *
 * Space: the tracking space in the game convention (cm, +Y up, left-handed, +Z ahead at yaw 0; somvr_head_game and
 * somvr_hand_game): a snap or smooth turn, Talion's walk and a climb move nothing here, so a slot stays on the body.
 *
 * Once per stereo frame (game thread, H4, after the bow's step and before the climb's):
 *   torso    the yaw the slots hang on, from the head alone and without lag (HL:A: the slots go with the head): the
 *            head's heading (its forward; looking steeply down or up its up vector blends in, so the heading stays put);
 *            while the main hand is near the sword's slot (radius + approach_cm) the torso keeps its yaw, as long as the head stays
 *            within deadband_deg of it (looking over the shoulder at the sword does not move the slot), and turns with
 *            the head at once past that; away from the slots it is the head's heading. No filter, no time constant.
 *   slots    the sword's: head + R(torso) sword_ofs (x mirrored for a left main hand), radius sword_r_cm; the dagger's:
 *            hip_ofs or back_ofs, dagger_r_cm. A hand is in its slot inside the radius, out past radius + exit_cm.
 *            Only the slot's own hand counts (the main hand at the shoulder, the off hand at the hip).
 *   grips    each squeeze with a hysteresis (grip_on / grip_off). A press inside the slot toggles: the weapon is put
 *            back when it is in the hand (or asked to be), drawn otherwise. A grip that was closed when the hand entered
 *            the slot and opens there puts the weapon back (dropping it in). Hold mode (D14 b): a weapon drawn by a press
 *            goes back when that grip opens anywhere. Quick draw: two presses of the main hand outside its slot within
 *            quick_s draw the sword. A press a slot took is the slot's until the grip opens (nograb: the climb takes no
 *            hold with it).
 *   busy     in.busy[h]: hand h is someone else's (the bow out, a climb on): it presses nothing here. in.away[h]: hand
 *            h must not hold a weapon now (the bow in both hands, a climb hold): its weapon is put back (SUSPEND) and,
 *            with restore, drawn again once the hand is neither away nor busy (RESTORE).
 *   asks     want[k] = the state asked for weapon k (-1 none, 0 put back, 1 in the hand) until the game's state
 *            (in.drawn[k]) reaches it (DONE) or ask_s passes (DENIED). A change of in.drawn without an ask is the
 *            game's own (GAME). The game putting away the sword the player kept in the hand (keep 1 the frame before,
 *            the hand free) asks for it again (REASSERT; at most reassert_max per reassert_s, then INSIST: the game's
 *            state stands): D14's promise against the game's own sheathes somvr.c cannot refuse. With undraw the other
 *            way too: the game drawing the sword the player keeps away (keep 0 the frame before) puts it back (UNDRAW,
 *            the same budget). Without live input or the game's allow: no gestures, the asks dropped, no preference.
 *   keep     keep[k]: what the player chose, for somvr.c's refusal of the game's own draw/sheathe: 1 = keep it in the
 *            hand, 0 = keep it away, -1 = none (an ask is on its way, the hand is busy, not allowed, no weapon).
 *   blade    the sword is in the hand and not being put back: SOMVR_COMBAT's blade may hit (no blade from an empty
 *            hand: the stock melee, which is never pressed, used to leave Urfael on the back while the player swung).
 */
#ifndef SOMVR_SLOTS_H
#define SOMVR_SLOTS_H

#include <stdint.h>

#include "somvr_pose.h"

enum { SOMVR_SLOT_SWORD = 0, SOMVR_SLOT_DAGGER = 1, SOMVR_NSLOT = 2 };
enum { SOMVR_SLOTS_DAGGER_HIP = 0, SOMVR_SLOTS_DAGGER_BACK = 1, SOMVR_SLOTS_DAGGER_OFF = 2 }; /* SOMVR_SLOTS_DAGGER= */
enum {                                /* somvr_slots_out.ev[k]: what happened to slot k this frame */
    SOMVR_SL_E_ENTER = 1u << 0,       /* its hand entered it (the tick) */
    SOMVR_SL_E_LEAVE = 1u << 1,       /* its hand left it */
    SOMVR_SL_E_DRAW = 1u << 2,        /* a draw asked (a press in the slot, the quick draw, a restore) */
    SOMVR_SL_E_PUT = 1u << 3,         /* a put-back asked (a press or a release in the slot, hold mode, a suspend) */
    SOMVR_SL_E_QUICK = 1u << 4,       /* with DRAW: the quick draw (the sword) */
    SOMVR_SL_E_DROPIN = 1u << 5,      /* with PUT: a held grip let go in the slot */
    SOMVR_SL_E_HOLDEND = 1u << 6,     /* with PUT: hold mode, the grip opened */
    SOMVR_SL_E_SUSPEND = 1u << 7,     /* with PUT: the hand is needed elsewhere (the bow, a climb hold) */
    SOMVR_SL_E_RESTORE = 1u << 8,     /* with DRAW: back after the bow or the climb */
    SOMVR_SL_E_DONE = 1u << 9,        /* the game's state reached the ask */
    SOMVR_SL_E_DENIED = 1u << 10,     /* an ask not reached within ask_s (dropped) */
    SOMVR_SL_E_GAME = 1u << 11,       /* the state changed without an ask (the game's own draw or sheathe) */
    SOMVR_SL_E_REASSERT = 1u << 12,   /* with DRAW: the game put away the sword the player keeps in the hand: back */
    SOMVR_SL_E_INSIST = 1u << 13,     /* with GAME: ... once more within reassert_s after reassert_max: accepted */
    SOMVR_SL_E_UNDRAW = 1u << 14,     /* with PUT: the game drew the sword the player keeps away (undraw): back */
};

typedef struct {
    int main_hand;          /* the sword's hand (SOMVR_HAND_R = 1); the dagger is the other's. A left main hand mirrors x */
    float sword_ofs[3];     /* the shoulder slot from the head in the torso's frame for a right main hand, cm: x right,
                             * y up, z ahead (10, -10, -15: behind the head over the right shoulder, design-vr.md 3) */
    float sword_r_cm;       /* 20 */
    int dagger_at;          /* SOMVR_SLOTS_DAGGER_* (HIP) */
    float hip_ofs[3];       /* the off hand's hip for a right main hand (-18, -62, -5) */
    float back_ofs[3];      /* the lower back, a little to the off hand's side (-8, -55, -22) */
    float dagger_r_cm;      /* 15 */
    float exit_cm;          /* a hand leaves a slot this far outside its radius (4) */
    float approach_cm[SOMVR_NSLOT]; /* the torso keeps its yaw while slot k's hand is within its radius + this (the
                             * sword 15; the dagger 0 = never: the off hand hangs near the hip, and looking down at
                             * the hip turns the head little) ... */
    float deadband_deg;     /* ... up to this far from the head's heading (30) */
    float grip_on, grip_off; /* the squeeze's hysteresis (0.6 / 0.4, as the climb's and the bow's) */
    float quick_s;          /* the quick draw's double squeeze, at most this apart (0.35; 0 = no quick draw) */
    int hold;               /* D14: 0 = a drawn weapon stays in the hand (a), 1 = only while its grip is held (b) */
    int restore;            /* a weapon put back for the bow or a climb comes back to the hand after it (1) */
    float ask_s;            /* an ask the game's state does not reach in this long is DENIED (0.5) */
    int reassert;           /* the game putting away the sword the player keeps in the hand gets it drawn again (1) ...
                             * (the dagger's state is ours: only a load takes it, and then it stays at its slot) */
    int reassert_max;       /* ... at most this often (2) ... */
    float reassert_s;       /* ... within this long (3 s); past that the game's state is accepted (INSIST) */
    int undraw;             /* build 10 (SOMVR_SLOTS_GAMEDRAW=refuse): the game drawing the sword the player keeps away
                             * (a combat start's stance, headset session 3) gets it put back, on the same budget
                             * (reassert_max per reassert_s, then INSIST); 0 = the game's draw is kept (the default) */
} somvr_slots_cfg;
void somvr_slots_cfg_default(somvr_slots_cfg *c);

typedef struct {
    double t;               /* s */
    int live;               /* the controller input is live */
    int allow;              /* the slots may act (somvr.c: the world, first person, the player's inventory and items) */
    int have[SOMVR_NSLOT];  /* weapon k exists and can be moved (its sites and item checked out) */
    int drawn[SOMVR_NSLOT]; /* weapon k is in its hand now (the game's state) */
    int busy[2];            /* hand h presses nothing here (the bow out, a climb on) */
    int away[2];            /* hand h must not hold a weapon now (the bow, a climb hold) */
    int head_ok;
    sv_vec3 head;           /* the head, tracking space (game convention, cm) */
    sv_quat head_q;
    int valid[2];           /* hand h's grip pose is usable */
    sv_vec3 p[2];           /* the grips, the same space */
    float squeeze[2];       /* 0..1 */
} somvr_slots_in;

typedef struct {
    int down[2];            /* each grip's logical state (hysteresis) */
    int took[2];            /* the current press belongs to a slot (until the grip opens) */
    int quick_armed;        /* the main hand's last press was outside its slot ... */
    double quick_t;         /* ... at this time */
    int inside[SOMVR_NSLOT];
    int entered_closed[SOMVR_NSLOT]; /* the slot's hand entered with its grip closed and has kept it closed */
    int want[SOMVR_NSLOT];  /* -1 none, 0 put back, 1 in the hand */
    double want_t[SOMVR_NSLOT];
    int by_hold[SOMVR_NSLOT]; /* hold mode: drawn by a press that is still held */
    int suspended[SOMVR_NSLOT];
    int last_drawn[SOMVR_NSLOT], have_last[SOMVR_NSLOT];
    int keep_last[SOMVR_NSLOT]; /* the last step's keep */
    int ra_n[SOMVR_NSLOT];  /* re-asserts in the window since ra_t0 */
    double ra_t0[SOMVR_NSLOT];
    float torso;            /* rad, tracking space */
    int torso_ok;
    /* totals */
    uint64_t frames, enters[SOMVR_NSLOT], draws[SOMVR_NSLOT], puts[SOMVR_NSLOT], quick, dropins[SOMVR_NSLOT],
        holdends[SOMVR_NSLOT], suspends[SOMVR_NSLOT], restores[SOMVR_NSLOT], done[SOMVR_NSLOT], denied[SOMVR_NSLOT],
        game[SOMVR_NSLOT], reasserts[SOMVR_NSLOT], insists[SOMVR_NSLOT], undraws[SOMVR_NSLOT], held_frames;
} somvr_slots;

typedef struct {
    uint32_t ev[SOMVR_NSLOT];  /* SOMVR_SL_E_* */
    int want[SOMVR_NSLOT];     /* the ask now: -1 none, 0 put back, 1 in the hand */
    int keep[SOMVR_NSLOT];     /* 1 keep it in the hand, 0 keep it away, -1 no preference */
    int blade;                 /* the sword is in the hand and not being put back */
    int nograb;                /* a press is a slot's: the climb takes no new hold now */
    int inside[SOMVR_NSLOT];
    float dist[SOMVR_NSLOT];   /* the slot's hand to the slot, cm (-1: unknown) */
    sv_vec3 slot[SOMVR_NSLOT]; /* the slots, tracking space */
    float head_yaw, torso_yaw; /* rad */
    int torso_held;            /* the approach rule kept the torso's yaw this frame */
} somvr_slots_out;

void somvr_slots_init(somvr_slots *s);
/* a new start (somvr.c: another inventory, or no step for a while: a load, a cutscene): the asks, the grips, keep,
 * suspends and the last state forgotten (no re-assert or restore across it); the torso and the totals kept */
void somvr_slots_reset(somvr_slots *s);
void somvr_slots_step(somvr_slots *s, const somvr_slots_cfg *c, const somvr_slots_in *in, somvr_slots_out *out);
const char *somvr_slots_name(int k); /* "sword" / "dagger" */

/* ---- the geometry (tested on its own) --------------------------------------------------------------------------- */
int somvr_slots_hand(const somvr_slots_cfg *c, int k); /* the hand of slot k: the main hand's for the sword */
/* the head's heading (rad, +Y up, + = right): its forward's; past 60 deg of pitch its up vector blends in (the side it
 * tilts to), so looking steeply down or up keeps the heading; any roll leaves it alone below 60 deg */
float somvr_slots_heading(sv_quat head_q);
/* the torso's yaw from the last one and the head's heading: hold = keep it, within deadband (rad) of the head */
float somvr_slots_torso(float last, float head_yaw, int hold, float deadband);
/* slot k's offset from the head in the torso's frame (cm), mirrored for a left main hand; 0 = the slot is off */
int somvr_slots_ofs(const somvr_slots_cfg *c, int k, sv_vec3 *ofs);
float somvr_slots_radius(const somvr_slots_cfg *c, int k);
/* slot k in tracking space: head + R(yaw) ofs */
sv_vec3 somvr_slots_pos(const somvr_slots_cfg *c, int k, sv_vec3 head, float torso_yaw);

#endif
