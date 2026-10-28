/* somvr_climb.h - SOMVR_CLIMB=b: hand-over-hand climbing, a prototype (docs/design-vr.md 4; docs/re-traversal.md 2.5's
 * recipe with the P7/P8 runtime notes; docs/re-xr-pipeline.md 12). Pure: no globals, no game reads, no OpenXR. Tested by
 * tests/test_climb.c; somvr.c's SOMVR_CLIMB region casts the rays, reads the game state, switches Talion's gravity and
 * moves him through the PM hook's one-shot extra move (so the game's collision still clips every move).
 *
 * Once per stereo frame (game thread, H4, after the grips are located for the eye views' time):
 *   grip     each controller's squeeze with a hysteresis; a new press (or one still inside the grab window) casts a ray
 *            from the hand along its palm (and along its fingers; while climbing also straight down: down_assist)
 *            through somvr.c's callback: a surface that holds (somvr_climb_surface_ok) within reach = a hold; none: a
 *            ray from the head to the hand that meets a surface at most inside_cm before the hand (the real hand went
 *            into the virtual wall: build 9's misses) holds there. The rays look near_cm past the reach: a surface
 *            there is a near miss (the logs, the miss cue). At the grab, two more rays: the same one edge_cm higher
 *            (no wall there = an edge hold, the wall's top is near; a ray down onto a top is one) and one down onto
 *            the top (its height). in.nograb (the bow out) spends a press instead: no ray, the grip must open before
 *            it may grab.
 *   cue      (cue = 1) an open hand raised over Talion's root (any open hand while a climb is on) is probed every
 *            cue_s: a surface it could hold within reach = E_NEAR on the way in (somvr.c: a light tick, HL:A's "this
 *            can be grabbed"), again only after cue_off_s out of reach; a press that found nothing while climbing or
 *            near a surface: miss_near (somvr.c: a short double buzz).
 *   climb    while any hand holds: Talion's gravity is off (somvr.c) and the newest hold drives: the body moves by minus
 *            that hand's motion in tracking space, rotated into the world like the rig (hand-over-hand: the hand stays
 *            where it grabbed), at most step_cm a frame (the rest carried, at most carry_cm); a tracking jump is not a
 *            pull. A hold whose grip opens lets go; one without tracking for lost_s lets go.
 *   top      an edge hold with the head above its top by top_cm, or the other hand reaching above it (within 1 m of the
 *            hold): push (the default: the climb goes on 1:1, the hands push the body up and over the edge; once the
 *            head is over_cm past the top hold toward the wall, the end "over": gravity back, Talion stands on it),
 *            release (gravity and walking take over), lift (our own pull-up: up until the head is lift_cm above the
 *            top (measured by the head, so a blocked lift stalls), then lift_fwd_cm over it, then release) or cmd (the
 *            stock LedgePullUp pulse, then release after cmd_s unless the game took over first).
 *   hang     letgo = hang (the default since build 10; the user after build 9: "I want to stay where I am when I let
 *            go. I should need to press a button to fall"): both hands open (or their tracking lost) while climbing =
 *            the phase hang: the climb stays on (gravity off), nothing moves, no timeout; a fresh grip on a wall climbs
 *            on (the same climb), the drop button (somvr.c: A) lets go = the end "drop" (gravity back: the fall). The
 *            input lost while holding hangs too (the grips must open before a new hold). With the ground within
 *            ground_cm under the root (a ray down; not once topped) both hands open end the climb as released
 *            instead (he stands: a hold taken at the foot of a wall never keeps him off the ground). letgo = fall:
 *            the old end.
 *   fling    (fling = 1, letgo = hang; build 13, the user's "Gorilla tag inspired ... just let me sling myself up";
 *            re-xr-pipeline.md 32.14) the last hold let go (its grip opened) while that hand still moved fast: the body
 *            keeps the hand's speed: minus the driving hand's mean tracking-space velocity over fling_win_s, into the
 *            world as a pull, x fling_gain; only up: the part away from the wall (the hold's toward) and any downward
 *            speed are dropped, the sideways part along the wall x fling_side (at most half the upward part), the part
 *            toward the wall kept (at most the upward part: it carries him over a top); under fling_min upward = the
 *            plain hang; capped at fling_max. The phase fling: our own arc (fling_g), the climb still on (gravity off),
 *            each frame's piece of the parabola asked as a move (the game's collision clips it); the upward part
 *            blocked twice = a ceiling, the sideways part blocked = it stops. Ends: a grip that holds (catch: the hold
 *            drives), the arc's top or fling_s (the apex: the hang where he is), the drop button, a top: the release
 *            hold's found top, push's, or the head's ray along toward losing the wall (a ray down gives the top), the
 *            head top_cm over it = topped: the edge step while rising, over_cm past it = the end "over"; topped at the
 *            arc's top with speed toward the wall left: the glide (that speed alone, level, at most 0.5 s) to over (or
 *            the top under the root within ground_cm: he stands on it); the edge blocking it lifts by the blocked part
 *            (the edge step, at most 50 cm in the glide), then the hang. No fling from the ground (ground_cm), from
 *            lost tracking or while a hand still holds.
 *            Build 18 (fling_up = 1; the user's build 17b session: launches capped at 500 cm/s with up to 324 of it
 *            toward the wall, 21-74 cm arcs, most stopped early as the face held the push into it): up first - the cap
 *            (fling_max) is on the up part alone; mid-face the part toward the wall is kept to fling_tw x the up part and
 *            fling_wall_up of the rest is turned up the face (a pull down and back slings him up the wall, never out);
 *            at a top all of it is kept (the glide over it); sideways x fling_side, at most fling_side_max x up; the
 *            threshold (fling_min) on that up part; in flight a push into the face the face holds is dropped (the
 *            rise goes on along it).
 *   descent  (descent = 1; build 13, re-xr-pipeline.md 40: a forge tower top's edge had no way down) the way over an edge
 *            and down: (1) a press also casts straight down when not climbing (a hand on a top from above) and along
 *            the wall (while climbing the last hold's `toward`; else from the hand toward the head, level: the face
 *            under an edge the hand reached over); (2) the lip: a hold on a top whose edge is within lip_find_cm along
 *            the reach (down rays every 10 cm), or a hold on a face, with the ground beyond it (a ray down from 30 cm
 *            out) at least lip_drop_cm under the hold = a lip: a climb may start from it under low_cm (Talion stands
 *            on the top: the descent), its toward into the top; (3) descending (the climb started from a lip with the
 *            feet on its top) until the head is under the lip's top: no top / push / over, and the lip step: a move
 *            down the top blocks (the capsule still on it) goes out over the edge by the blocked part (lip_ramp x, at most
 *            step_cm a frame; the outward move not applied 3 frames in a row = the game's invisible edge wall: lip_blocked,
 *            no more lip step); then the face, hand over hand down (a hold pushed up lowers him, as any climb); (4) the
 *            drop button: the ground under him (a ray down from blink_out_cm out from the wall, or from beyond the
 *            lip while still on the top) more than blink_cm under his feet = the blink (in.can_blink: the phase blink,
 *            nothing moves, out.blink asks somvr.c for the practice trip's fade and teleport to out.blink_to; the
 *            caller's somvr_climb_stop(END_BLINK) after the teleport, else the hang after blink_wait_s); a lower drop
 *            (or no blink here) = the drop as before (the real fall); no ground within blink_max_cm or water under
 *            him = no drop at all (E_BLINK_NONE: he stays where he is). (5) A on foot (no climb; the flat game's leap off a
 *            tower is A): along the view's heading, down rays every 20 cm up to edge_look_cm find where the floor
 *            ends (more than 30 cm down; a wall ahead stops the look); the ground under it (blink_out_cm further)
 *            more than blink_cm under the feet = the blink there (E_EDGE; a forge tower top: an invisible 'Air' wall
 *            stops Talion ~64 cm ahead of his root and the floor goes on past it, run 010412). (6) the cue: the walk
 *            stopped (in.walk_blocked) at an edge A would blink down from = E_EDGE_CUE once (somvr.c: a double pulse
 *            on the right hand, A's), again after the walk has gone on for 2 s.
 *   mark     (drop_mark = 1, with descent; build 17, re-xr-pipeline.md 55: A blinked Talion down 4-16 m at once, no
 *            shown where, no cancel) every A of (4) and (5) chooses first: A's press finds the landing (dm_find: from
 *            blink_out_cm out from the wall / beyond the lip / past the edge, then every 20 cm to dm_out_cm, the first
 *            spot with room for Talion - nothing 180 cm over it, nothing within 33 cm round him at his knees and chest -
 *            and no enemy within dm_enemy_cm: in.enemy_near) and shows it while A is held (out.dm_show: a ring where he
 *            will stand and a line from the edge's or the feet's height down to it); A let go after dm_min_s = go (the
 *            blink, or from a hang over a short drop the fall, as before); let go sooner = a tap: nothing, the ring
 *            stays to dm_show_s from the press; the move stick past dm_stick while held = cancel (on foot it walks again
 *            from there; in a hang it must come back under 0.3 before the hang's stick acts); a new hold, the climb's
 *            end or start, the root moved dm_move_cm on foot, the game, a menu, the bow, the input = cancel. Climbing,
 *            the landing is found again when the root moves 10 cm (at most every 0.1 s). No safe landing (no ground,
 *            water, an enemy there, no room, a high drop with no blink now) = no ring, E_DM_NO (somvr.c: a soft buzz);
 *            on foot with no edge or a short drop: nothing (as before). The pullover waits while A is held.
 *   overhang (overhang > 0; build 15b, re-xr-pipeline.md 47.2: session 165456 mark 8, stuck under a slab sticking out
 *            ~21 cm over the face, every pull on its top went nowhere) climbing up (not descending), the last request's
 *            upward part not applied (under half) while the driving hold is over the eyes = the capsule under a lip:
 *            the blocked part x overhang goes out from the wall (the hold's toward, level, reversed; at most step_cm a
 *            frame, overhang_cm under one lip until the root has risen OH_RESET_CM past where it began): a pull on the
 *            lip takes him out round its edge and up, as a climber's body swings out under a roof (the player's own
 *            pull, redirected; nothing without it). And a new hold more than OH_RETOP_CM over a top already reached (a
 *            ledge on the way up) forgets that top (E_RETOP): the next one is reached as any (the head over the hold's
 *            top: push, the edge step under it, over) - the old top's edge-step ceiling and its "over" point would
 *            else hold him under the new one (the session: the top 2561.6 found in a fling, the slab's at 2811.6).
 *            Where the game holds him under it all the same (run 174931: the meadow bank's ledge keeps the capsule
 *            from going out over its edge, as descent's `lip blocked`, so the slab's underside stops every pull): the
 *            pulls blocked OH_MANTLE_S in all while a hold with a found top 0..OH_MANTLE_UP_CM over the eyes drives =
 *            the mantle: a floor on that top 40 cm in from its edge (a ray down) with room to stand (a ray up) = the
 *            descent's blink there (in.can_blink: the practice trip's fade and teleport; phase blink, E_MANTLE): the
 *            player's own pull finished by a fade, never a move he did not ask for.
 *   pullover (pullover = 1; build 16, re-xr-pipeline.md 50.2: session 233334 mark 2, still stuck at the lip with the
 *            overhang's automatic blink off) a hand holding a top's edge - the hold's top found by rays down just
 *            over it and into the wall, at most po_up_cm over the eyes and po_down_cm under them (the feet still
 *            under it) - with a landing on
 *            that top (a floor within 30 cm of the top 20..80 cm in from the hold, nearest 40 cm in, room for Talion:
 *            nothing within 33 cm round him at his knees and chest, 180 cm up), and the climb's own 1:1 blocked there
 *            (follow-up: the fade is the fallback, not the norm): the pull's up part (or, with the head at the top,
 *            its part into the top) not applied (under a fifth: not rising, not going in) for po_block_s of pulling
 *            = armed: out.po_show and the
 *            landing (somvr.c shows a ring there: the where, before anything happens); where the 1:1 goes through,
 *            nothing shows and the push over the top goes on as ever. Blocked the same on a top without a landing =
 *            E_PO_NOWAY once a hold (somvr.c: a soft double buzz: no way up here). The when: that hand pulled po_pull_cm
 *            (down, or back toward the body, in tracking space: the player's own arm) while it holds = the blink onto
 *            the ring (in.can_blink: the practice trip's fade; phase blink, E_PO_GO). The cancel: that hand opens
 *            before (E_PO_CANCEL; the climb goes on: hang, fling as ever), or A (the drop as ever). The climb's own
 *            1:1 moves go on while armed (where the game lets him rise, he rises).
 *   stick    (hang_stick_cm > 0; build 16: session 233334, hanging with the feet 28 cm over a ledge the stick did
 *            nothing: held as if in some ledge state) in the hang the move stick tilted past half with the
 *            ground (a ray down) at most hang_stick_cm under the feet = released (gravity back: he stands; the stick
 *            walks from the next tick). Higher up the hang stays (A drops, a grip climbs on).
 *   hug      (hug = 1; build 17: the user's build 16 session, "I keep accidentally pushing myself away from the wall")
 *            while a hold drives, the move's part away from the wall (the hold's toward, level, reversed) is cut once
 *            Talion's root is hug_cm or more from the hold's face (the plane through the hold along toward): a hand
 *            pushed toward the wall never shoves him off it; pulls up, down and along it stay 1:1. Beyond hug_cm (a
 *            hold taken at arm's length, a face that leans back) a part of the pull (hug_in x its length, not past
 *            hug_cm) goes toward the face: the player's own pull redirected, never a move without one; not at a top
 *            (the push over it is the player's) nor descending.
 *   stand    (stand = 1; build 17: the session's marks 3 and 5, topped and let go = left hanging at the top) a floor
 *            under Talion's capsule - a ray down at his root and stand_in_cm toward the top, the floor from
 *            stand_down_cm under his feet to stand_up_cm over them (not water) - while topped: the last hold let go
 *            = released (he stands: gravity back, the stick walks; E_STAND), a push that brings the feet onto it
 *            while holding = over (E_STAND with s->stand_over); in a topped hang with no floor under him but one
 *            within stand_reach_cm toward the top (his capsule's rim on it), the move stick walks him (in.stick_w,
 *            at most stand_walk_cms, its part away from the top dropped; the edge step lifts as a push's) until the
 *            floor is under him: released (E_STAND_WALK on the way). Elsewhere the hang stays as ever.
 *   topout   (topout = 1; build 17: mark 3, the cliff's top-out held - run 052042's trace: the capsule's side at the top's
 *            corner, the whole move stopped with the feet 66 cm under the top, then only the ring) topped with the head
 *            over the top, the push (its up part or its part into the top) not applied (under a fifth of its length)
 *            for thr_block_s = a top-out through (E_TOP_THR_ON): each frame's move goes through the rock 1:1 (out.
 *            through_up and through_lx/lz: somvr.c sets the proxy by it, as through's vertical part), its part away from
 *            the top dropped, none deeper than over_cm + 40 past the top's hold; only while the head's way along the
 *            move is clear (rays from the head and thr_side_cm round it, thr_clear_cm past it: else that frame is
 *            blocked as ever, E_TOP_THR_REFUSED once, and the pullover's ring stays the fallback). Over (E_TOP_THR_OFF):
 *            stand's floor under him (the climb's end: over), out of the rock 2 frames after 10 cm (once in it), no move 0.5 s,
 *            thr_max_cm, the top left, the climb's end.
 *   A mantle (mantle = 1 glide, 2 blink; build 20: the user's build 19b session, hanging over a top with the eyes 110 cm
 *            over it for 16 s, asked for a press of A onto the ledge once the shoulders are over it) at a top - the push's (topped) or the newest hold's own found top - with the eyes am_head_cm or
 *            more over it (at most AM_HI_CM): a landing on the top straight ahead of Talion (from the edge in, as the
 *            pullover's: a floor within 30 cm of the top, room for him) and the way there clear (rays: the head's way up
 *            and in, his capsule's at the knees and the hips over the top) = available: out.am_show and the landing
 *            (somvr.c: the pullover's gold ring there, a light tick when it first shows). A's press (holding, hanging or
 *            in a fling) = every hold let go (the grips must open first) and glide: the root along a curve - up until the
 *            feet are am_clear_cm over the top, then in onto the landing (a quadratic curve through the corner over the
 *            edge) - at am_cms (each frame's move through the rock 1:1 as topout's, the head's way checked; phase
 *            mantle) = the end over (he stands); blocked (the root 25 cm behind the plan 0.25 s) = he stands if the top
 *            is under him, else the hang. blink: the pullover's quick blink onto the landing. At a top (the eyes top_cm
 *            over it) with no mantle (too low, no landing, the way not clear) A does nothing but say why (E_AM_NO: a soft
 *            double buzz); elsewhere A is the drop (mark) as ever. The when is the press, the where the ring (shown before
 *            it), the move short and fixed (no cancel needed once it goes); nothing moves without the press.
 *   end      both hands open with letgo = fall (released), the drop button while holding or hanging (drop), the game not
 *            allowing it any more (somvr.c: FSM not Supported, mounted, third person ...; "stand down"), the input lost
 *            (letgo = fall), the timeout max_s (letgo = fall), a lift that stalls or runs out of time. Every end gives
 *            gravity back (somvr.c writes the flag, P8); after any end but a release the grips must open before a new
 *            hold. While the input is frozen (a menu, the pause) nothing changes and the clocks stop.
 */
#ifndef SOMVR_CLIMB_H
#define SOMVR_CLIMB_H

#include <stdint.h>

#include "somvr_pose.h"

enum { SOMVR_CLIMB_TOP_RELEASE = 0, SOMVR_CLIMB_TOP_LIFT = 1, SOMVR_CLIMB_TOP_CMD = 2, SOMVR_CLIMB_TOP_PUSH = 3 };
/* SOMVR_CLIMB_TOP= */
enum { SOMVR_CLIMB_RAY_PALM = 1, SOMVR_CLIMB_RAY_FWD = 2 };                              /* cfg.rays */
enum { SOMVR_CLIMB_PH_NONE = 0, SOMVR_CLIMB_PH_HOLD = 1, SOMVR_CLIMB_PH_LIFT = 2, SOMVR_CLIMB_PH_CMD = 3,
       SOMVR_CLIMB_PH_HANG = 4 /* letgo = hang: no hand holds, Talion stays (gravity off) until a grip or the drop */,
       SOMVR_CLIMB_PH_FLING = 5 /* fling: no hand holds, our own arc up from a fast release (gravity off) */,
       SOMVR_CLIMB_PH_BLINK = 6 /* descent: A over a high drop: still (gravity off) until the blink's teleport */,
       SOMVR_CLIMB_PH_MANTLE = 7 /* A mantle: the glide onto the top (gravity off) */ };
enum { SOMVR_CLIMB_AM_OK = 0, SOMVR_CLIMB_AM_LOW = 1, SOMVR_CLIMB_AM_NOFLOOR = 2, SOMVR_CLIMB_AM_NOROOM = 3,
       SOMVR_CLIMB_AM_NOWAY = 4, SOMVR_CLIMB_AM_NOBLINK = 5, SOMVR_CLIMB_AM_HIGH = 6 }; /* A mantle: why not (am_why) */
enum { SOMVR_CLIMB_AME_NONE = 0, SOMVR_CLIMB_AME_DONE = 1, SOMVR_CLIMB_AME_BLOCKED = 2, SOMVR_CLIMB_AME_HANG = 3,
       SOMVR_CLIMB_AME_GAME = 4 }; /* A mantle: how the last glide ended (am_end) */
const char *somvr_climb_am_name(int why); /* SOMVR_CLIMB_AM_* */
enum { SOMVR_CLIMB_FL_NONE = 0, SOMVR_CLIMB_FL_APEX = 1, SOMVR_CLIMB_FL_BLOCKED = 2, SOMVR_CLIMB_FL_TIME = 3,
       SOMVR_CLIMB_FL_CATCH = 4, SOMVR_CLIMB_FL_OVER = 5, SOMVR_CLIMB_FL_EDGE = 6 }; /* how a flight ended (fl_why) */
enum { SOMVR_CLIMB_LETGO_FALL = 0, SOMVR_CLIMB_LETGO_HANG = 1 }; /* SOMVR_CLIMB_LETGO= */
enum { SOMVR_CLIMB_VIA_NONE = 0, SOMVR_CLIMB_VIA_PALM = 1, SOMVR_CLIMB_VIA_FWD = 2, SOMVR_CLIMB_VIA_DOWN = 3,
       SOMVR_CLIMB_VIA_INSIDE = 4, SOMVR_CLIMB_VIA_WALL = 5 }; /* which ray found a surface (somvr_climb_via_name) */
enum { SOMVR_CLIMB_BL_NONE = 0, SOMVR_CLIMB_BL_SHORT = 1, SOMVR_CLIMB_BL_NOGROUND = 2, SOMVR_CLIMB_BL_WET = 3,
       SOMVR_CLIMB_BL_CANNOT = 4, SOMVR_CLIMB_BL_NOEDGE = 5,
       SOMVR_CLIMB_BL_ENEMY = 6, SOMVR_CLIMB_BL_NOROOM = 7 }; /* descent: why A was no blink (bl_why); 6, 7: drop_mark's */
enum { SOMVR_CLIMB_DM_NONE = 0, SOMVR_CLIMB_DM_BLINK = 1, SOMVR_CLIMB_DM_FALL = 2 }; /* drop_mark: what the let-go does */
enum { SOMVR_CLIMB_DMC_NONE = 0, SOMVR_CLIMB_DMC_STICK = 1, SOMVR_CLIMB_DMC_GRAB = 2, SOMVR_CLIMB_DMC_END = 3,
       SOMVR_CLIMB_DMC_MOVED = 4, SOMVR_CLIMB_DMC_GAME = 5 }; /* drop_mark: why a choice was cancelled (dm_cancel_why) */
enum {                                /* why a climb ended (somvr_climb_out.end_why, somvr_climb.last_end) */
    SOMVR_CLIMB_END_NONE = 0,
    SOMVR_CLIMB_END_RELEASED,         /* both grips opened (or lost their tracking) */
    SOMVR_CLIMB_END_GAME,             /* the game state does not allow it (in.allow = 0): stand down */
    SOMVR_CLIMB_END_INPUT,            /* the controller input is not live */
    SOMVR_CLIMB_END_TIMEOUT,          /* max_s */
    SOMVR_CLIMB_END_TOP,              /* the top reached, top_mode release */
    SOMVR_CLIMB_END_LIFTED,           /* the lift done */
    SOMVR_CLIMB_END_LIFT_STALL,       /* the lift did not raise the head (blocked) or ran out of time */
    SOMVR_CLIMB_END_CMD,              /* the stock pull-up was asked for and the hold waited cmd_s */
    SOMVR_CLIMB_END_OUTSIDE,          /* somvr_climb_stop: flat frames, the player changed, the hook stale */
    SOMVR_CLIMB_END_OVER,             /* push: the head over the top (past the edge by over_cm): standing on it */
    SOMVR_CLIMB_END_DROP,             /* the drop button (somvr.c: A) while holding or hanging: let go of the wall */
    SOMVR_CLIMB_END_BLINK,            /* descent: the blink's teleport put him on the ground (somvr_climb_stop) */
    SOMVR_CLIMB_NEND
};
enum {                                /* somvr_climb_out.events: what happened this frame (bit << 1 per hand where _L/_R) */
    SOMVR_CLIMB_E_GRAB_L = 1u << 0, SOMVR_CLIMB_E_GRAB_R = 1u << 1,         /* a hold */
    SOMVR_CLIMB_E_LET_L = 1u << 2, SOMVR_CLIMB_E_LET_R = 1u << 3,           /* a hold let go (grip open) */
    SOMVR_CLIMB_E_LOST_L = 1u << 4, SOMVR_CLIMB_E_LOST_R = 1u << 5,         /* a hold let go (no tracking for lost_s) */
    SOMVR_CLIMB_E_MISS_L = 1u << 6, SOMVR_CLIMB_E_MISS_R = 1u << 7,         /* a press found nothing within its window */
    SOMVR_CLIMB_E_NOCLIMB_L = 1u << 8, SOMVR_CLIMB_E_NOCLIMB_R = 1u << 9,   /* a press found a surface that is not
                                                                             * CanWallClimb (the press stops looking) */
    SOMVR_CLIMB_E_JUMP = 1u << 10,    /* the driving hand jumped more than jump_cm: that frame's motion ignored */
    SOMVR_CLIMB_E_TOP = 1u << 11,     /* the top reached (top_mode acts) */
    SOMVR_CLIMB_E_CARRY_CUT = 1u << 12, /* the carried motion was cut to carry_cm */
    SOMVR_CLIMB_E_LOW_L = 1u << 13, SOMVR_CLIMB_E_LOW_R = 1u << 14,         /* a hold too low to start a climb (low_cm:
                                                                             * the ground under him; the press stops) */
    SOMVR_CLIMB_E_HANG = 1u << 15,    /* letgo = hang: the last hold let go (or the input was lost): hanging */
    SOMVR_CLIMB_E_RESUME = 1u << 16,  /* a new hold while hanging: the climb goes on */
    SOMVR_CLIMB_E_DROP = 1u << 17,    /* the drop button: the climb ended (end_why DROP) */
    SOMVR_CLIMB_E_NEAR_L = 1u << 18, SOMVR_CLIMB_E_NEAR_R = 1u << 19,       /* cue: an open hand came within reach of a
                                                                             * surface it could hold */
    SOMVR_CLIMB_E_FLING = 1u << 20,   /* fling: a launch (somvr_climb.fl_* has its numbers) */
    SOMVR_CLIMB_E_FLING_NONE = 1u << 21, /* fling: the last hold let go, no fling (fl_none says why): the hang */
    SOMVR_CLIMB_E_CATCH = 1u << 22,   /* fling: a hold during the flight (with E_GRAB) */
    SOMVR_CLIMB_E_APEX = 1u << 23,    /* fling: the flight's end without a hold: hanging (with E_HANG; fl_why) */
    SOMVR_CLIMB_E_FLING_TOP = 1u << 24, /* fling: the eyes cleared a top in flight (topped: push) */
    SOMVR_CLIMB_E_GLIDE = 1u << 25,   /* fling: topped at the arc's top: level on toward the top (fl_glide) */
    SOMVR_CLIMB_E_LIP_L = 1u << 26, SOMVR_CLIMB_E_LIP_R = 1u << 27,         /* descent: a hold on a lip (with E_GRAB) */
    SOMVR_CLIMB_E_BLINK = 1u << 28,   /* descent: A over a high drop: the blink asked (out.blink_to) */
    SOMVR_CLIMB_E_BLINK_NONE = 1u << 29, /* descent: A, no blink (bl_why): the drop, or nothing (no ground, water) */
    SOMVR_CLIMB_E_DESCENT = 1u << 30, /* descent: the climb started from a lip on its top (descending) */
    SOMVR_CLIMB_E_EDGE = 1u << 31,    /* descent: A on foot (no climb): the edge ahead (with E_BLINK or E_BLINK_NONE) */
};
enum { /* somvr_climb_out.events2 */
    SOMVR_CLIMB_E_LIP_BLOCKED = 1u << 0, /* descent: the lip step's outward move not applied 3 frames: the game's edge wall */
    SOMVR_CLIMB_E_EDGE_CUE = 1u << 1, /* descent (cue): the walk stopped at an edge A would blink down from */
    SOMVR_CLIMB_E_RETOP = 1u << 2,    /* overhang: a hold far over the top reached: that top forgotten (a new one ahead) */
    SOMVR_CLIMB_E_OVERHANG = 1u << 3, /* overhang: the first overhang step under this lip (an up move blocked) */
    SOMVR_CLIMB_E_MANTLE = 1u << 4,   /* overhang: the pull held under a top it cannot rise past: the blink onto it */
    SOMVR_CLIMB_E_PO_ARM = 1u << 5,   /* pullover: a hand on a top's edge with a landing: armed (the ring shown) */
    SOMVR_CLIMB_E_PO_GO = 1u << 6,    /* pullover: the pull done: the blink onto the landing asked (out.blink_to) */
    SOMVR_CLIMB_E_PO_CANCEL = 1u << 7, /* pullover: the armed hand let go (or the top out of range) before the pull */
    SOMVR_CLIMB_E_PO_NONE = 1u << 8,  /* pullover: a new hold on a top's edge without a landing (s->po_why) */
    SOMVR_CLIMB_E_STICK_DROP = 1u << 9, /* hang_stick_cm: the stick in a low hang: released (s->stick_h: the feet's height) */
    SOMVR_CLIMB_E_PO_NOWAY = 1u << 10, /* pullover: pulls blocked on a top with no landing (once a hold): no way up here */
    SOMVR_CLIMB_E_THR_ON = 1u << 11,  /* through: a pull blocked under a hold over the eyes: its up part goes through */
    SOMVR_CLIMB_E_THR_OFF = 1u << 12, /* through: over (s->thr_why: SOMVR_CLIMB_THR_*) */
    SOMVR_CLIMB_E_THR_REFUSED = 1u << 13, /* through: the head's way up not clear (once a climb through): blocked as ever */
    SOMVR_CLIMB_E_STAND = 1u << 20,   /* stand: a floor under him at a top: released (let go) or over (holding) */
    SOMVR_CLIMB_E_STAND_WALK = 1u << 21, /* stand: the stick in a topped hang walks him onto the top (the first frame) */
    SOMVR_CLIMB_E_TOP_THR_ON = 1u << 22, /* topout: the push at a top held: it goes through 1:1 */
    SOMVR_CLIMB_E_TOP_THR_OFF = 1u << 23, /* topout: over (s->to_why: SOMVR_CLIMB_THR_*) */
    SOMVR_CLIMB_E_TOP_THR_REFUSED = 1u << 24, /* topout: the head's way along the move not clear (once a top-out) */
    SOMVR_CLIMB_E_DM_SHOW = 1u << 14, /* drop_mark: A's press: the landing shown (s->dm_kind: the blink or the fall) */
    SOMVR_CLIMB_E_DM_GO = 1u << 15,   /* drop_mark: A let go: the blink (out.blink) or the fall (E_DROP, the end DROP) */
    SOMVR_CLIMB_E_DM_CANCEL = 1u << 16, /* drop_mark: the choice cancelled (s->dm_cancel_why) */
    SOMVR_CLIMB_E_DM_TAP = 1u << 17,  /* drop_mark: A let go under dm_min_s: nothing (the ring stays to dm_show_s) */
    SOMVR_CLIMB_E_DM_NO = 1u << 18,   /* drop_mark: no safe landing (s->bl_why): no ring, nothing moves (a soft buzz) */
    SOMVR_CLIMB_E_DM_MOVE = 1u << 19, /* drop_mark: climbing, the landing found again (moved, gone or back) */
    SOMVR_CLIMB_E_AM_SHOW = 1u << 25, /* A mantle: available (the landing shown; out.am_land) - its first frame */
    SOMVR_CLIMB_E_AM_GO = 1u << 26,   /* A mantle: A's press: the glide (phase mantle) or the blink (out.blink) */
    SOMVR_CLIMB_E_AM_NO = 1u << 27,   /* A mantle: A's press at a top with no mantle (s->am_why): nothing moves */
    SOMVR_CLIMB_E_AM_END = 1u << 28,  /* A mantle: the glide's end (s->am_end: over, or blocked: stood, or the hang) */
};
/* through's ends (s->thr_why) */
enum { SOMVR_CLIMB_THR_NONE = 0, SOMVR_CLIMB_THR_CLEAR = 1, SOMVR_CLIMB_THR_IDLE = 2, SOMVR_CLIMB_THR_MAX = 3,
       SOMVR_CLIMB_THR_END = 4, SOMVR_CLIMB_THR_NOREAD = 5 };
const char *somvr_climb_thr_name(int why);
enum { SOMVR_CLIMB_PO_OK = 0, SOMVR_CLIMB_PO_NOTOP = 1, SOMVR_CLIMB_PO_NOFLOOR = 2, SOMVR_CLIMB_PO_NOROOM = 3 }; /* po_why */

typedef struct {
    float grip_on, grip_off; /* the squeeze's hysteresis (0.6 / 0.4, as the menus' LB/RB) */
    float reach_cm;          /* a hold: a climbable surface within this far ahead of the hand (25, design-vr.md 4) */
    float back_cm;           /* each grab ray starts this far behind the hand (10): a hand a little inside the wall */
    int rays;                /* SOMVR_CLIMB_RAY_* bits: along the palm, along the fingers (both) */
    float window_s;          /* a press out of reach keeps looking for a hold this long (0.25) */
    float step_cm;           /* the most Talion moves in one frame (8: 7.2 m/s at 90 Hz) */
    float carry_cm;          /* the most clamped motion carried into the next frames (20) */
    float jump_cm;           /* a held hand moving more than this in one frame is a tracking glitch, not a pull (25) */
    float lost_s;            /* a holding hand without tracking this long lets go (0.3) */
    float max_s;             /* a climb ends after this long (120) */
    float edge_cm;           /* the top test at a grab: the same ray this much higher finds no wall = an edge hold (25) */
    float edge_far_cm;       /* ... or finds it this much farther than the hold's surface (15) */
    float top_cm;            /* an edge hold's top is reached when the head is this far above it (10) */
    int top_mode;            /* SOMVR_CLIMB_TOP_* (push) */
    float over_cm;           /* push: the head this far past the top hold's point, toward the wall (its `toward`) =
                              * over the top: the climb ends, gravity back, Talion stands on it (30) */
    int down_assist;         /* while climbing, a press also looks straight down from the hand: a hand over a wall's
                              * top grabs the top whatever the hand's angle (1) */
    float low_cm;            /* a new climb starts only from a hold at most this far under Talion's root (his pelvis,
                              * ~100 cm over his feet): no climb from the ground he stands on (a hand reaching down,
                              * run 061702); holds while climbing are not limited; 0 = off (50) */
    float ramp;              /* push: the edge step: the part of a forward push (toward the wall) the game's collision
                              * did not apply lifts the body by ramp x that part (1: one to one; 0 off), at most
                              * step_cm a frame, while the head is under the top + lift_cm: Talion's capsule reaches
                              * ~100 cm under his root and steps over an edge only from 40 cm under it (run 060734) */
    float lift_cm;           /* lift: the head ends this far above the top (185: standing on it) */
    float lift_fwd_cm;       /* lift: then this far over the top, horizontally along the hold's ray (40) */
    float lift_s;            /* lift: at most this long (1.5) */
    float lift_still_s;      /* lift: the head not rising for this long while going up = blocked (0.3) */
    float cmd_s;             /* cmd: how long the hold waits after the stock pull-up pulse (0.6) */
    int letgo;               /* SOMVR_CLIMB_LETGO_*: both hands open while climbing = hang (default) or fall */
    float ground_cm;         /* hang: the ground this close under the root (a ray down; the root is ~102 cm over his
                              * feet) = he stands: both hands open end the climb as released, no hang (a hold taken
                              * from the ground and let go never holds him in the air: 125; 0 off) */
    float inside_cm;         /* the hand inside a wall: the head-to-hand ray meets a surface at most this far before the
                              * hand = a hold there (30; 0 off). Build 9: 19 of 40 grabs had the hand 0-10 cm in the
                              * wall (the ray's start behind it), the misses deeper */
    float near_cm;           /* the grab rays look this much past the reach: a surface there is a near miss (35) */
    int cue;                 /* the reach cue and the miss cue (1) */
    float cue_s;             /* each hand is probed this often (1/30 s) */
    float cue_off_s;         /* out of reach this long before the next E_NEAR (0.3) */
    float cue_raise_cm;      /* not climbing: only a hand at least this far over the root (the pelvis) is probed (10) */
    int fling;               /* the fling (needs letgo = hang): the last hold let go while the hand moves fast (0 off) */
    float fling_min;         /* the least upward speed of a fling, cm/s; under it the hang (150: an 11 cm rise) */
    float fling_gain;        /* the release velocity x this (1) */
    float fling_max;         /* the flight's speed cap, cm/s (500: 1.27 m up at fling_g 981) */
    float fling_side;        /* the sideways part along the wall x this (0.5; at most half the upward part) */
    float fling_g;           /* the flight's gravity, cm/s^2 (981) */
    float fling_win_s;       /* the release velocity: the mean over this long (0.1; at least 0.04 s of samples) */
    float fling_s;           /* the longest flight (1.5) */
    int descent;             /* the way over an edge and down: the lip, the lip step, the blink (0 off) */
    float lip_drop_cm;       /* a lip: the ground beyond it at least this far under the hold (150) */
    float lip_find_cm;       /* a hold on a top: its edge searched this far along the reach (60) */
    float foot_cm;           /* Talion's root over his feet (102, run 060734) */
    float lip_ramp;          /* the lip step: the blocked downward part x this goes out over the edge (2: the capsule
                              * clears an edge it stands ~40 cm from in ~40 cm of the hand's push) */
    float blink_cm;          /* the drop button: the feet more than this over the ground = the blink (400; 0: never) */
    float blink_max_cm;      /* ... the ground searched this far under the feet (8000); none = no drop */
    float blink_out_cm;      /* ... from this far out from the wall (40) */
    float blink_wait_s;      /* the blink's teleport not come this long = the hang (3) */
    float edge_look_cm;      /* A on foot: an edge looked for this far ahead along the view (400; 0: never) */
    float overhang;          /* the overhang step: an up move blocked under a hold over the head x this goes out from
                              * the wall (0 off: build 15's; 1 on); with it a hold far over a reached top forgets it */
    float overhang_cm;       /* ... at most this far out under one lip (40: a slab 21 cm out, the capsule ~40 round) */
    int oh_mantle;           /* ... and the mantle (the automatic blink onto a top after 0.4 s of blocked pulls; 1 =
                              * SOMVR_CLIMB_OVERHANG=on, build 15b's); 0 = =step (build 16: the out step and the retop
                              * only; the way onto a top is the player's pullover) */
    int pullover;            /* build 16: a hand on a top's edge shows the landing, a pull takes him there (0 off) */
    float po_pull_cm;        /* ... the pull (25) */
    float po_up_cm;          /* ... the top at most this over the eyes (120) */
    float po_down_cm;        /* ... and at most this under them (150: the feet still under the top) */
    float po_block_s;        /* ... and the 1:1 blocked this long while pulling (0.3) */
    float hang_stick_cm;     /* build 16: hanging with the feet at most this over the ground (a ray down), the move
                              * stick tilted past half = the climb ends (released: he stands, the stick walks); 0 off */
    int face_toward;         /* build 16 round 2: a hold found by a ray at a slant to its face (over 35 deg off the
                              * face's level normal: the palm or the fingers along the wall) takes the face's normal as
                              * its toward, and its edge test runs along it (1; 0 = the ray's own, as before) */
    int through;             /* build 16 round 2 (SOMVR_CLIMB_THROUGH, 0 off): a pull's up part blocked thr_block_s
                              * under the driving hold over the eyes (the capsule under a lip it cannot pass: the user's
                              * slab) goes up through the rock as the stock wall run's does (somvr.c: the proxy set up by
                              * it, no collision), 1:1 with the hand, while the capsule is in the rock; never with the
                              * head's way up not clear (rays from the head and round it, thr_clear_cm past the move) */
    float thr_block_s;       /* ... the pull blocked this long first (0.15) */
    float thr_clear_cm;      /* ... the head's way clear this far past its new place (15) */
    float thr_side_cm;       /* ... and so round the head (the eyes, the near plane: 10) */
    float thr_max_cm;        /* ... at most this far in one (300: the capsule's height and a slab) */
    float thr_look_cm;       /* ... and none starts with rock this close over the head (60) */
    int drop_mark;           /* build 17 (SOMVR_CLIMB_DESCENT_MARK, with descent; 0 off): A held = the landing shown, A
                              * let go = go, the stick = cancel (see "mark" above) */
    float dm_min_s;          /* ... A held at least this long before a let-go goes (0.3) */
    float dm_show_s;         /* ... a tap's ring shown this long from its press (0.6) */
    float dm_stick;          /* ... the move stick past this while A is held = cancel (0.5) */
    float dm_move_cm;        /* ... on foot: the root moved this far from the press = cancel (30) */
    float dm_out_cm;         /* ... the landing looked for out to this from the wall or the edge (100) */
    float dm_enemy_cm;       /* ... an enemy within this of Talion's root at the landing = no landing (120) */
    int thr_top;             /* ... also at a top with the head over it: the edge step's lift (the push into the top)
                              * blocked = through, so the top-out goes 1:1 where the capsule's foot is caught under
                              * the edge (the user's cliff top, run 043737: the feet 46 cm under it; 1) */
    int hug;                 /* build 17 (SOMVR_CLIMB_HUG, 0 off): no move away from the wall past hug_cm (see hug) */
    float hug_cm;            /* ... the root's axis this far from the hold's face at most (50: the capsule's 35 + 15) */
    float hug_in;            /* ... beyond it this part of a pull's length goes toward the face (0.35; 0 = the cut only) */
    int stand;               /* build 17 (SOMVR_CLIMB_STAND, 0 off): a floor under him at a top = he stands (see stand) */
    float stand_in_cm;       /* ... the second ray down this far toward the top from the root (15: the capsule's foot on
                              * an edge 15 cm in tilts 25 deg: he stands, further out he would slide off) */
    float stand_up_cm;       /* ... the floor at most this over the feet (15) */
    float stand_down_cm;     /* ... and at most this under them (40) */
    float stand_reach_cm;    /* ... the stick walks him when a floor is within this toward the top (35: the capsule) */
    float stand_walk_cms;    /* ... at most this fast (150 cm/s, the stick at full tilt) */
    int topout;              /* build 17 (SOMVR_CLIMB_TOPOUT=through, 0 off): a push held at a top goes through 1:1 */
    int fling_up;            /* build 18 (SOMVR_CLIMB_FLING_UP, 0 off): the fling up first (see fling) */
    float fling_tw;          /* ... mid-face the part toward the wall kept at most this x the up part (0.25) */
    float fling_wall_up;     /* ... and this part of the rest turned up the face (0.5) */
    float fling_side_max;    /* ... the sideways part at most this x the up part (0.6; build 13's rule: 0.5) */
    int mantle;              /* build 20 (SOMVR_CLIMB_MANTLE=glide|blink, 0 off): at a top, A = onto it (see A mantle) */
    float am_head_cm;        /* ... the eyes at least this over the top (25: the shoulders at its edge) */
    float am_cms;            /* ... the glide's speed (350 cm/s) */
    float am_clear_cm;       /* ... the feet this far over the top when the curve crosses its edge (10) */
} somvr_climb_cfg;
void somvr_climb_cfg_default(somvr_climb_cfg *c);

/* one ray's answer (somvr.c: the G21 cast with the player's own body skipped, the hit surface's CanWallClimb) */
typedef struct {
    int hit;        /* something other than Talion within the length */
    int climbable;  /* its surface record says CanWallClimb */
    float dist;     /* cm from the ray's start */
    sv_vec3 point;  /* the hit, world cm */
    int wet;        /* descent: a water / liquid surface (somvr_climb_surface_wet) */
    int chr;        /* build 18: a character's body (an orc; somvr.c's cl_char_hit): the wraith step's ray goes on through */
} somvr_climb_hit;
/* the ray callback: from (world cm), dir (unit), len (cm) -> *out; returns 1 if the cast ran (0: no answer, as no hit) */
typedef int (*somvr_climb_ray_fn)(void *ctx, int hand, sv_vec3 from, sv_vec3 dir, float len, somvr_climb_hit *out);

typedef struct {
    int valid;          /* the grip pose is usable this frame */
    sv_vec3 p;          /* the grip in tracking space, game convention (somvr_hand_game), cm */
    sv_vec3 pw;         /* the same in the world (somvr_rig_point), cm */
    sv_vec3 palm, fwd;  /* unit world directions: out of the palm, along the fingers */
    float squeeze;      /* 0..1 */
    int buttons;        /* the hand's button actions are active (SOMVR_HACT_INPUT) */
} somvr_climb_hand;

typedef struct {
    double t;           /* s */
    double pose_t;      /* s: the time the hands were located for (somvr.c: the views' display time; > 0), the fling's
                         * release velocity's clock (a stalled frame, e.g. a dump, does not stretch it); 0 = t */
    int live;           /* the controller input is live */
    int frozen;         /* live, but a menu or the pause: nothing changes, the clocks stop */
    int allow;          /* the game allows a climb now (somvr.c: FSM Supported, not mounted, first person, the objects) */
    int nograb;         /* no new hold now (somvr.c: the hands are on the bow): a press is spent, the grip must open
                         * before it may grab; holds already taken are kept */
    float rig_yaw;      /* psi - yaw0: tracking space -> world, as somvr_rig_vec */
    float rig_scale;    /* the player size s (somvr_rig_scale; 0 = 1): a hold carries s x the hand's tracked move */
    int head_ok;        /* head_y, head_x, head_z are this frame's head node */
    float head_y;       /* the frame's base y (the head node), cm */
    float head_x, head_z; /* ... its world x and z (push: over the top) */
    int root_ok;        /* root is Talion's root this frame (push's edge step: what the last move did) */
    sv_vec3 root;       /* world cm */
    int rock_ok;        /* through: rock_cm is the proxy's deepest contact now (somvr.c: its manifold) */
    float rock_cm;      /* ... cm (< 0: inside a surface; under -1: in the rock) */
    int drop;           /* the drop button is held (somvr.c: A on the right controller); its press while a climb is on
                         * (holding or hanging) ends it: the fall */
    int can_blink;      /* descent: the blink can start now (somvr.c: the practice's trip is free) */
    sv_vec3 head_fwd;   /* descent: the view's heading (horizontal unit; 0 = none): A on foot looks for an edge along it */
    int walk_blocked;   /* descent (cue): the stick asks a walk the game does not apply (somvr.c: LOCO's last 0.5 s) */
    float stick;        /* the move stick's tilt 0..1 as the player holds it (somvr.c, before the climb holds it off) */
    sv_vec3 stick_w;    /* stand: the move stick in the world (level, x its tilt 0..1, the view's heading; 0 = none) */
    /* drop_mark: an enemy within r_cm of p (Talion's root at a landing, world cm): 1 = one there (NULL: never) */
    int (*enemy_near)(void *ctx, sv_vec3 p, float r_cm);
    void *enemy_ctx;
    somvr_climb_hand hand[2];
} somvr_climb_in;

#define SOMVR_CLIMB_VH_N 16 /* fling: the ring's samples (0.18 s at 90 Hz) */
typedef struct {
    int down;           /* the squeeze's logical state (hysteresis) */
    int armed;          /* seen open since the last forced end: a press may grab */
    int looking;        /* a press inside its window, no hold yet */
    double press_t;
    int holding;
    uint32_t seq;       /* grab order: the newest hold drives */
    int have_last;
    sv_vec3 last_p;     /* tracking space at the last frame it was valid */
    double lost_t;      /* when its tracking was lost (< 0: tracked) */
    sv_vec3 anchor;     /* the hand at the grab, world cm */
    sv_vec3 hit;        /* the hold's surface point */
    sv_vec3 dir;        /* the ray that found it (world, unit) */
    float hit_dist;
    int edge;           /* an edge hold: the wall ends within edge_cm above */
    float top_y;        /* its top, world y cm (edge holds) */
    int top_found;      /* top_y from the down ray (else the bound hit.y + edge_cm) */
    sv_vec3 toward;     /* horizontal unit toward the wall: the ray's (a ray down onto a top: the head's to the hold,
                         * else the fingers'): where "over the top" is (push, lift) */
    int face;           /* face_toward: toward is the face's normal (the hold's ray was at a slant to the face) */
    sv_vec3 ray_tw;     /* ... the ray's own toward then (logs) */
    int via;            /* SOMVR_CLIMB_VIA_*: the ray of the hold */
    float inside_by;    /* via inside: how far the hand was past the surface (cm) */
    float near_past;    /* this press's nearest surface past the reach (cm; < 0 none) */
    int near_via;       /* ... its ray */
    int cue_in;         /* cue: within reach at the last probe */
    double cue_t, cue_out_t; /* the next probe; when it last went out of reach */
    sv_vec3 vh_p[SOMVR_CLIMB_VH_N]; /* fling: the hand's tracking-space positions while it drives (a ring) ... */
    double vh_t[SOMVR_CLIMB_VH_N];  /* ... and their times */
    int vh_n, vh_i;     /* samples kept, the next slot */
    int lip;            /* descent: the hold is a lip (lip_*: its top, a point on its edge, out = away from the wall,
                         * the ground beyond it this far under the hold) */
    float lip_top_y, lip_drop;
    sv_vec3 lip_edge, lip_out;
    uint32_t po_seq;    /* pullover: the hold (seq) its landing was looked for */
    int po_ok, po_why;  /* ... found (SOMVR_CLIMB_PO_*) */
    int po_has_top;     /* ... a top found (po_top_y), with or without a landing */
    int po_noway;       /* ... E_PO_NOWAY given for this hold */
    float po_top_y;     /* ... its top, the landing on it (the floor point), into the top (level unit) */
    sv_vec3 po_land, po_in;
} somvr_climb_hold;

typedef struct {
    somvr_climb_hold h[2];
    int active;         /* a climb: gravity off */
    int phase;          /* SOMVR_CLIMB_PH_* */
    double t0, phase_t, last_t;
    uint32_t seq;
    int driver;         /* the hand whose motion moves Talion (-1: none) */
    sv_vec3 carry;
    /* lift */
    float lift_y;       /* the head's target height */
    int lift_up_done;
    float fwd_left;
    sv_vec3 lift_dir;
    float lift_best;    /* the highest head seen while lifting */
    double lift_rise_t; /* when it last rose by 1 cm */
    int last_end;       /* SOMVR_CLIMB_END_* of the last climb */
    /* push: the top reached, the climb goes on 1:1 until the head is over it */
    int topped;
    sv_vec3 top_hit, top_toward;
    float top_y;        /* the top hold's top (the edge step's ceiling: top_y + lift_cm for the head) */
    sv_vec3 last_req;   /* the last frame's request (0: none) and the root then (the edge step) */
    float last_ramp;    /* ... the edge step's lift in it (fling: the arc's own part is the rest) */
    sv_vec3 last_root;
    int have_root;
    double ramp_cm;     /* the edge step's lift, in total */
    int drop_down;      /* the drop button's state at the last frame (a press = its rising edge) */
    double hang_t;      /* when the hang began */
    /* fling: the launch's numbers (logs) and the flight */
    int fl_hand;        /* the hand that launched */
    sv_vec3 fl_hand_v;  /* its mean velocity, tracking space cm/s */
    float fl_span;      /* ... over this long (s), fl_n samples */
    int fl_n;
    sv_vec3 fl_v0;      /* the body's velocity at the release (world cm/s, x gain) */
    float fl_up, fl_side, fl_into, fl_out; /* its parts: up, sideways along the wall, toward the wall, away (dropped) */
    int fl_capped;      /* fling_max cut it */
    sv_vec3 fl_v;       /* the flight's velocity now (world cm/s) */
    double fl_t0;       /* the launch */
    int fl_frames, fl_blocked;
    int fl_glide;       /* topped at the arc's top, still going toward the wall: level on (since fl_glide_t) */
    double fl_glide_t;
    float fl_glide_lift; /* the glide's edge step so far (cm) */
    float fl_glide_cap;  /* ... at most this (FL_GLIDE_LIFT_CM, or the capsule's foot's gap under the top + 10 cm) */
    double fl_glide_run; /* the glide's time not blocked by the edge (s; FL_GLIDE_S of it = out of time) */
    int fl_landed;      /* the last fling's over: the top under the root in the glide (else the head past the hold) */
    sv_vec3 fl_root0;   /* the root at the launch (root_ok then: fl_root_ok) */
    int fl_root_ok;
    sv_vec3 fl_toward;  /* the release hold's toward (unit horizontal into the wall, or 0) */
    int fl_edge;        /* the release hold's found top: fl_top_y, its surface point fl_hit */
    float fl_top_y;
    sv_vec3 fl_hit;
    float fl_wall_d;    /* the head's ray along toward: the wall this far at the launch (< 0: no ray, not looking) */
    int fl_top_by;      /* E_FLING_TOP: 1 the release hold's top, 2 the head's ray */
    int fl_why;         /* SOMVR_CLIMB_FL_* of the last flight */
    int fl_none;        /* E_FLING_NONE: 0 too slow up (fl_up ...), -1 no velocity (fl_n samples, under 40 ms), -2 not the
                         * driving hand's release */
    /* descent */
    int descending;     /* the climb started from a lip with the feet on its top: until the head is under lip_top_y */
    float lip_top_y;    /* that lip's top, a point on its edge, away from the wall */
    sv_vec3 lip_edge, lip_out;
    sv_vec3 wall_tw;    /* the wall the climb is on: the last face or lip hold's toward (0: none) */
    int bl_why;         /* SOMVR_CLIMB_BL_* of the last A with descent */
    float bl_h;         /* ... the feet over the ground (cm; < 0: none found) */
    sv_vec3 bl_from, bl_ground, bl_to; /* ... the ray down's start, the ground it found, the blink's target (the root) */
    double lip_cm;      /* the lip step, in total */
    int lip_blocked, lip_block_n; /* the game keeps him from going over this lip (its edge wall): no lip step */
    uint64_t lip_blocks;
    int edge_cued;      /* the edge cue given for this stop (again after the walk went on edge_rearm_s) */
    double walk_free_t; /* since when the walk is not stopped (< 0: stopped now) */
    double edge_cue_t;  /* the last cue's probe */
    float cue_h;        /* the last cue's drop (cm) */
    uint64_t edge_cues;
    uint64_t lips, descents, lip_steps, blinks, bl_short, bl_noground, bl_wet, bl_cannot, bl_waits, edge_blinks, bl_noedge;
    /* overhang */
    float oh_cm;        /* the overhang step under this lip so far (0: none; past it by OH_RESET_CM: 0 again) */
    float oh_y0;        /* the root's height at this lip's first step */
    double oh_all_cm;   /* the overhang step, in total */
    uint64_t oh_steps, oh_lips, retops, mantles, mantle_nofloor;
    float oh_block_s;   /* the pulls' time blocked under a known top (the mantle at OH_MANTLE_S) */
    sv_vec3 mt_top;     /* the last mantle's top point (the hold's) and its landing floor */
    sv_vec3 mt_floor;
    /* pullover */
    int po_hand;        /* the armed hand (-1: none) */
    uint32_t po_aseq;   /* ... its hold */
    sv_vec3 po_grab_p;  /* ... the hand at the arm (tracking space) */
    float po_pull;      /* ... the pull so far (cm) */
    int po_why;         /* the last E_PO_NONE's SOMVR_CLIMB_PO_* */
    float po_block_s;   /* the 1:1 blocked so far under the candidate hold (po_bseq) while pulling */
    uint32_t po_bseq;
    uint64_t po_arms, po_goes, po_cancels, po_nones, po_noways;
    float po_dbg[8];    /* pullover: the last landing search (logs): the top, floors seen, the highest floor, blocked
                         * over them, room rays hit in / out / to the sides */
    /* through */
    int thr_on;         /* a climb through: this frame's vertical part goes through the rock */
    float thr_block;    /* the pull blocked under a hold over the eyes so far (s) */
    float thr_idle;     /* ... no vertical move so far (s) */
    float thr_cm;       /* ... this one's way through (cm) */
    float thr_y0;       /* ... the root's height at its start */
    int thr_clear;      /* ... frames out of the rock in a row */
    int thr_refused_once; /* ... E_THR_REFUSED given */
    int thr_why;        /* the last one's end (SOMVR_CLIMB_THR_*) */
    double thr_all_cm;
    uint64_t thr_ons, thr_frames, thr_refused;
    float stick_h;      /* hang_stick_cm: the feet over the ground at the last stick drop (cm) */
    uint64_t stick_drops;
    /* hug */
    int hug_ok;         /* hug_d is this frame's (a driving hold with a wall direction) */
    float hug_d;        /* the root's axis to the driving hold's face (cm, along its toward) */
    float hug_max_d;    /* ... the largest while a hold drove (this climb; logs) */
    double hug_cut_cm, hug_in_cm; /* the moves away from the wall cut, the pulls redirected toward it (totals, cm) */
    uint64_t hug_cuts, hug_ins;  /* ... frames */
    /* stand */
    float stand_h;      /* the last E_STAND: the feet over the floor (cm; < 0: under it) */
    int stand_at;       /* ... the ray that found it: 0 at the root, 1 stand_in_cm toward the top */
    int stand_over;     /* ... 1 = over (holding), 0 = released (let go, or the stick's walk) */
    int stand_walking;  /* the stick walks him in a topped hang (since the first E_STAND_WALK of this hang) */
    double stand_walk_cm;
    uint64_t stands, stand_overs, stand_walks;
    /* topout */
    int to_on;          /* a top-out through: this frame's move goes through the rock */
    float to_block, to_idle, to_cm; /* ... the push held so far (s), no move so far (s), this one's way (cm) */
    int to_clear, to_refused_once, to_why, to_in; /* ... to_in: the capsule was in the rock during this one */
    float to_top_y;     /* ... the top it is at (the push's, or the driving hold's own) */
    double to_all_cm;
    uint64_t to_ons, to_frames, to_refused;
    float fl_wallup;    /* fling_up: the last launch's push into the wall turned up (cm/s) */
    uint64_t fl_into_cut; /* fling_up: flights whose push into the face was dropped once the face took it */
    /* drop mark (build 17) */
    int dm;             /* 0 none, 1 A held (choosing), 2 a tap's ring until dm_t0 + dm_show_s */
    int dm_climb;       /* the choice began while climbing (else on foot) */
    int dm_kind;        /* SOMVR_CLIMB_DM_*: what the let-go does now (NONE: nothing) */
    int dm_cancel_why;  /* the last cancel's SOMVR_CLIMB_DMC_* */
    int dm_stick_wait;  /* a stick cancel: the stick must come back under 0.3 before the hang's stick acts */
    double dm_t0;       /* the press */
    double dm_test_t;   /* climbing: the landing's last test */
    sv_vec3 dm_land;    /* the ring: the floor point where he will stand (or land: the fall) */
    sv_vec3 dm_top;     /* the line's top: over the ring at the edge's (or the feet's) height */
    sv_vec3 dm_to;      /* the blink's target (Talion's root) */
    sv_vec3 dm_root;    /* the root at the press (on foot) or at the last test (climbing) */
    float dm_out;       /* the landing's distance past the first spot looked at (cm: 0, 20, ...) */
    uint32_t dm_seq;    /* the newest hold's seq at the press (a newer one = cancel) */
    uint64_t dm_presses, dm_shows, dm_goes, dm_falls, dm_cancels, dm_taps, dm_nos, dm_tests, dm_enemies, dm_norooms;
    /* A mantle (build 20) */
    int am_ok;          /* available now: the landing (am_land) shown */
    int am_why;         /* the last search's SOMVR_CLIMB_AM_* (the last A at a top without a mantle: its why) */
    double am_test_t;   /* the last search (again when the root moved 5 cm, at most every 0.1 s) */
    sv_vec3 am_test_root;
    float am_top_y;     /* the top, its edge straight ahead of Talion, into the top (level unit), the landing (floor) */
    sv_vec3 am_edge, am_in, am_land;
#define SOMVR_CLIMB_AM_N 16 /* the glide's curve: this many pieces */
    sv_vec3 am_pt[SOMVR_CLIMB_AM_N + 1]; /* ... its points (the root: at the press .. at the landing) */
    float am_len[SOMVR_CLIMB_AM_N + 1];  /* ... the length along it to each point */
    double am_t0;       /* the press */
    float am_lag_s;     /* the root behind the plan so long */
    int am_end;         /* SOMVR_CLIMB_AME_*: how the last glide ended */
    float am_dbg[6];    /* the last search (logs): the eyes over the top, floors seen, room rays hit, way rays hit */
    uint64_t am_shows, am_presses, am_goes, am_blinks, am_nos, am_lows, am_nofloors, am_noways, am_dones, am_blocks,
        am_frames;
    double am_cm;       /* the glides' way in all */
    /* totals */
    uint64_t climbs, grabs, lets, lost, misses, noclimb, tops, lifts, cmds, standdowns, timeouts, jumps, cuts, rays,
        frames, moves, overs, lows, hangs, resumes, drops, insides, nears, cue_rays, flings, fl_nones, catches, apexes,
        fl_blocks, fl_overs, fl_tops, fl_glides, faces;
    double req_cm;      /* the length of every move asked for */
} somvr_climb;

typedef struct {
    int active;             /* gravity off for Talion after this frame */
    int started, ended;     /* edges this frame */
    int end_why;            /* SOMVR_CLIMB_END_* when ended */
    int move;               /* move_w is a request for the next player update */
    sv_vec3 move_w;         /* world cm */
    uint32_t events;        /* SOMVR_CLIMB_E_* */
    uint32_t events2;       /* SOMVR_CLIMB_E_LIP_BLOCKED (the second word) */
    int top_cmd;            /* cmd mode: pulse the stock LedgePullUp now */
    int top_by;             /* with SOMVR_CLIMB_E_TOP: 1 the head over the top, 2 the other hand reaching over it */
    float ramp;             /* the edge step's lift in this frame's request (cm) */
    int rays;               /* rays cast this frame */
    int grab_rays[2];       /* rays cast for each hand's grab test */
    somvr_climb_hit grab_hit[2]; /* each hand's grab ray answer this frame (logs; hit = 0 when none cast) */
    int grab_cast[2];       /* the frame's cast (1 = its first ray) that gave grab_hit[h] (0: none): the caller's ray log */
    int miss_near[2];       /* with SOMVR_CLIMB_E_MISS: the press was made while climbing or near a surface (the cue) */
    int driver;             /* the driving hand (-1 none) */
    float lip;              /* descent: the lip step's move out in this frame's request (cm) */
    float overhang;         /* the overhang step's move out from the wall in this frame's request (cm) */
    int blink;              /* descent: start the blink to blink_to (Talion's root at the landing) now */
    sv_vec3 blink_to;
    int po_show;            /* pullover: armed: show the landing (po_land: the floor point, world cm) */
    sv_vec3 po_land;
    float po_pull;          /* ... the pull so far, 0..1 of po_pull_cm */
    int po_hand;            /* ... the armed hand */
    float through_up;       /* through: this request's vertical part (cm) goes through the rock (0: none) */
    float hug_cut;          /* hug: this frame's move away from the wall cut (cm) */
    float hug_in;           /* hug: this frame's pull redirected toward the face (cm) */
    float stand_walk;       /* stand: this frame's walk onto the top (cm) */
    float through_lx, through_lz; /* topout: this request's level part through the rock (cm; with through_up) */
    int am_show;            /* A mantle: available: show the landing (am_land: the floor point, world cm) */
    sv_vec3 am_land;
    int dm_show;            /* drop_mark: show the ring at dm_land and the line from dm_top down to it (world cm) */
    sv_vec3 dm_land, dm_top;
    float dm_held;          /* ... A held so far, 0..1 of dm_min_s (1: a let-go goes; 0 for a tap's ring) */
} somvr_climb_out;

void somvr_climb_init(somvr_climb *s);
/* one stereo frame; ray may be NULL (no grab can be found then) */
void somvr_climb_step(somvr_climb *s, const somvr_climb_cfg *c, const somvr_climb_in *in, somvr_climb_ray_fn ray,
                      void *ctx, somvr_climb_out *out);
/* the end from outside (why: SOMVR_CLIMB_END_*): every hold released, the grips must open before a new one */
void somvr_climb_stop(somvr_climb *s, int why, somvr_climb_out *out);
const char *somvr_climb_end_name(int why);
const char *somvr_climb_via_name(int via);
const char *somvr_climb_phase_name(int phase);
const char *somvr_climb_fl_name(int why); /* SOMVR_CLIMB_FL_* */
const char *somvr_climb_bl_name(int why); /* SOMVR_CLIMB_BL_* */

/* the pieces (tested on their own) */
int somvr_climb_grip(int *down, float squeeze, float on, float off); /* the hysteresis; returns the new state */
sv_vec3 somvr_climb_to_world(sv_vec3 d_track, float rig_yaw);       /* R(Y(rig_yaw)) d, as somvr_rig_vec */
sv_vec3 somvr_climb_clamp(sv_vec3 v, float max_len);                /* v shortened to max_len (NaN: 0) */
/* the fling's velocity rule: the body's release velocity vw (world cm/s, x gain already) against toward (unit
 * horizontal into the wall, or 0): only up (see fling above); 1 = a fling (*v0, the parts in s->fl_up/side/into/out,
 * fl_capped), 0 = under fling_min (the parts filled all the same) */
int somvr_climb_fling_rule(somvr_climb *s, const somvr_climb_cfg *c, sv_vec3 vw, sv_vec3 toward, sv_vec3 *v0);

/* which surfaces hold a hand (SOMVR_CLIMB_SURF=walls|game|any; somvr.c's ray callback answers hit.climbable with it).
 * walls (default): every solid surface record, the game's CanWallClimb ignored: not a character's (Flesh*, Armor*),
 * not sky, air, invisible ones or player blockers, not liquids (water*, liquid*), mud, foliage, ice or slippery ones
 * (*Slip*); game: CanWallClimb only (in this game's data it marks the 17 No_WallRun / AxeClimb / Air / blocker records,
 * not plain 'stone', 'wood' or 'WallRun': runs 052758 and 053549, re-xr-pipeline.md 32); any: all but Flesh* (tests).
 * name: the record's name ("" = unnamed: walls holds it); can: the game's CanWallClimb */
enum { SOMVR_CLIMB_SURF_WALLS = 0, SOMVR_CLIMB_SURF_GAME = 1, SOMVR_CLIMB_SURF_ANY = 2 };
int somvr_climb_surface_ok(int mode, const char *name, int can);
/* descent: a landing that is water (water*, liquid*, puddle; case-insensitive) */
int somvr_climb_surface_wet(const char *name);
/* descent: an invisible blocker the hands and the rays reach through ('Air', Invisible*, PlayerBlocker*): somvr.c's ray
 * steps past it (a forge tower top's edge wall, run 010412) */
int somvr_climb_surface_clear(const char *name);

/* ==== SOMVR_CLIMB_UNPIN (build 16, docs/re-xr-pipeline.md 50.1) ================================================
 * Build 15b's session (233334 marks 2/3): Talion's capsule in the meadow bank's rock at a corner of its ledge (the
 * proxy's contacts inside the stone); the game's character proxy then applies no horizontal move at all - every walk
 * and every pull undone, only the height free. The unpin, per player tick (somvr.c's PM hook; on foot and climbing):
 * the last tick's move asked (the transform vf_11 left - the root before it) against the move the body made, level;
 * blocked = at least min_cm asked and under a fifth done. Blocked for hold_s in a row while a proxy contact is at least
 * pen_cm inside a wall or a ceiling (not a floor: its normal under 0.7 up) (the game's own contacts: a wall ahead, an invisible edge wall or an edge keeper never are)
 * = pinned: the asked move (at most step_cm a tick, the player's own speed: the stick's or the pull's) is given to the
 * proxy directly, its part into any touching wall (a contact within touch_cm whose normal it opposes) taken out (he
 * slides along it; nothing left = none) plus a drift out of the rock he is in (its deepest wall's normal, at most
 * drift_cms: a few cm a second, never felt as a push), none where the caller's rays along it find a wall within
 * radius_cm + the move. Nothing without a move asked. */
typedef struct {
    float hold_s;    /* blocked this long in a row (0.15) */
    float min_cm;    /* a tick asked at least this far, level (0.2) */
    float frac;      /* ... and the body did under this part of it (0.2) */
    float pen_cm;    /* a contact at least this deep in a surface = in the rock (1) */
    float touch_cm;  /* a contact nearer than this guards the move's direction (2) */
    float step_cm;   /* the most a tick (8) */
    float radius_cm; /* the capsule's radius: the caller's rays must find no wall this far + the move (35) */
    float drift_cms; /* the drift out of the rock while a move is asked, at most this a second (4 cm/s) */
} somvr_unpin_cfg;
void somvr_unpin_cfg_default(somvr_unpin_cfg *c);
#define SOMVR_UNPIN_MAXC 16
typedef struct {
    sv_vec3 n; /* the surface's normal toward Talion (unit) */
    float d;   /* the distance (cm; < 0 inside the surface) */
} somvr_unpin_contact;
typedef struct {
    double t;
    float dt;                /* the tick's time (s; the drift's cap) */
    sv_vec3 asked, applied;  /* the last tick: asked (cm, vf_11's transform - the root before it) and done (cm) */
    int nc;                  /* the proxy's contacts (at most SOMVR_UNPIN_MAXC) */
    somvr_unpin_contact c[SOMVR_UNPIN_MAXC];
    float clear_cm;          /* the caller's rays along the move: the nearest wall (cm; large = none; < 0 = not cast) */
} somvr_unpin_in;
enum { SOMVR_UNPIN_FREE = 0, SOMVR_UNPIN_WAIT = 1, SOMVR_UNPIN_STEP = 2, SOMVR_UNPIN_NOTIN = 3, SOMVR_UNPIN_INTO = 4,
       SOMVR_UNPIN_WALL = 5, SOMVR_UNPIN_RAYS = 6 }; /* somvr_unpin_out.why */
typedef struct {
    double t0;     /* blocked since (< 0: not blocked) */
    int pinned;    /* steps given since the block began */
    uint64_t blocked_ticks, steps, notin, into, walls, episodes;
    double step_cm, drift_cm; /* the steps' length, their drift out of the rock (totals) */
} somvr_unpin;
typedef struct {
    int why;       /* SOMVR_UNPIN_*: STEP = move the proxy by d; RAYS = cast the rays along dir, then call again */
    sv_vec3 d;     /* STEP: the move (level, cm) */
    sv_vec3 dir;   /* the asked move's direction (level unit) */
    float deepest; /* the deepest contact (cm, < 0 inside) */
    int first;     /* STEP: the first of this block (a log line) */
    float drift;   /* STEP: its drift out of the rock (cm), in d */
} somvr_unpin_out;
void somvr_unpin_init(somvr_unpin *s);
/* one tick; in->clear_cm < 0 and everything else allowing a step = RAYS (cast, set clear_cm, call again: no time
 * passes on the second call); a tick without a block resets the block */
void somvr_unpin_step(somvr_unpin *s, const somvr_unpin_cfg *c, const somvr_unpin_in *in, somvr_unpin_out *o);
const char *somvr_unpin_why_name(int why);

/* ==== SOMVR_HEAD_ROCK (build 17, docs/re-xr-pipeline.md 60.3) =================================================
 * The user's build 16 session, mark 4: the view inside a rock face. Real steps move only the view (never Talion), so
 * over a session the tracked head drifted up to ~1 m from his body (the room's offset); his capsule stood 55 cm out
 * from the face, the view 24 cm inside it. Per stereo frame somvr.c casts from Talion's eyes (the rig's base, where
 * his capsule keeps them out of the rock) to the tracked head and on past it, and from the head along the view (the
 * near plane): gap = the head's clearance (cm; < 0 = past a surface). The view fades to black as the gap closes from
 * near_cm to full_cm (out_s to black, in_s back); black for back_s with the head still in = back: somvr.c re-latches
 * the rig's origin so the view is at Talion's eyes again (under the black), then it fades in. */
typedef struct {
    float near_cm;  /* the fade starts with the head this close to a surface (8: the near plane is 6) */
    float full_cm;  /* ... black with it this far past it (-2) */
    float out_s;    /* to black in this long (0.12) */
    float in_s;     /* back in this long (0.25) */
    float back_s;   /* black this long = back to Talion's eyes (0.5; 0 = never) */
} somvr_headrock_cfg;
void somvr_headrock_cfg_default(somvr_headrock_cfg *c);
typedef struct {
    float level;    /* the fade now, 0..1 */
    double black_t; /* black since (< 0: not black) */
    int backed;     /* back given in this black (again only after the level fell under 0.5) */
    int in;         /* the gap under near_cm (an episode; first = its first frame) */
    uint64_t frames, faded, black, episodes, backs;
    float min_gap;  /* the smallest gap of this episode (logs) */
} somvr_headrock;
void somvr_headrock_init(somvr_headrock *s);
typedef struct {
    float level;    /* the fade for this frame, 0..1 */
    int back;       /* re-latch now (the view back to Talion's eyes) */
    int first;      /* an episode's first frame (the gap under near_cm) */
    int ended;      /* an episode ended (the level back to 0) */
} somvr_headrock_out;
/* one frame: t (s), ok = the rays answered (0: no change of the target, the level eases out), gap (cm) */
void somvr_headrock_step(somvr_headrock *s, const somvr_headrock_cfg *c, double t, float dt, int ok, float gap,
                         somvr_headrock_out *o);

/* ==== SOMVR_HEAD_TETHER (build 18, docs/re-xr-pipeline.md 69) ===============================================
 * The tracked head's level offset from the rig's origin (somvr_rig_offset: what SOMVR_FOLLOW asks Talion's body to
 * close, 20 cm) when the follow cannot take him there - an edge, a wall, a blocked path: past start_cm the view fades
 * with the distance, black at black_cm; past start_cm for hold_s = to black too; black for back_s with the head still
 * past start_cm = back: somvr.c re-latches the rig's origin (the view at Talion's eyes, under the black), then it fades
 * in. HL:A's head-through-a-wall fade, for the head off the body. */
typedef struct {
    float start_cm; /* the fade starts this far from the body (50: the follow keeps 20) */
    float black_cm; /* ... black this far (90) */
    float hold_s;   /* past start_cm this long = to black (1.0; 0 = never) */
    float out_s;    /* to black in this long (0.15) */
    float in_s;     /* back in this long (0.3) */
    float back_s;   /* black this long = back to Talion's eyes (0.3; 0 = never) */
} somvr_tether_cfg;
void somvr_tether_cfg_default(somvr_tether_cfg *c);
typedef struct {
    float level;    /* the fade now, 0..1 */
    double over_t;  /* past start_cm since (< 0: not) */
    double black_t; /* black since (< 0: not black) */
    int backed;     /* back given in this black (again only after the level fell under 0.5) */
    int in;         /* an episode: past start_cm, until the level is back to 0 */
    int held;       /* this episode went to black by hold_s */
    uint64_t frames, faded, black, episodes, backs, holds;
    float max_cm;   /* the episode's farthest (logs) */
} somvr_tether;
void somvr_tether_init(somvr_tether *s);
typedef struct {
    float level;    /* the fade for this frame, 0..1 */
    int back;       /* re-latch now (the view back to Talion's eyes) */
    int first;      /* an episode's first frame */
    int held;       /* hold_s passed: to black now */
    int ended;      /* an episode ended (the level back to 0) */
} somvr_tether_out;
/* one frame: ok = measured (0: climbing, a hold, a menu, ...: the level eases out), off_cm = the head's offset */
void somvr_tether_step(somvr_tether *s, const somvr_tether_cfg *c, double t, float dt, int ok, float off_cm,
                       somvr_tether_out *o);

#endif
