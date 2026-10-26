/*
 * somvr_probe.c - Shadow of Mordor VR: runtime probes for the scarce test boots (docs/runtime-probes.md).
 * An LD_PRELOAD library for the native Linux (Feral) build: bin/ShadowOfMordor, non-PIE ELF at 0x400000, GNU
 * build-id 87ec5d6fd94afd47a24d5da7c9b0c2b74b30c6e9. It loads NEXT TO mod/spike1 (LD_PRELOAD=spike1.so:probe.so) and
 * never installs the save guard: spike1 owns WBOnlineService slots 219-222 (two libraries patching them would conflict).
 *
 * Usage (the game process only):
 *   SOMVR_PROBE_LOG=FILE     required. Unset or empty = the library logs nothing and changes nothing.
 *   SOMVR_PROBE_CMD=FILE     command file: every new version of the file (new inode, mtime or size; the harness writes
 *                            it with tmp + rename) is read once; each line holds one or more probe words, ';'-separated.
 *   SOMVR_PROBE_OUT=DIR      dumps (bindings, attacks, nodes TSVs); default: the log's directory + /probe-out
 *   SOMVR_PROBE_AUTO=ITEMS   commands run without the harness: "EVENT[+SECS] WORDS|EVENT[+SECS] WORDS|...",
 *                            EVENT = start | menu (front end for 3 s) | world (in the world, not paused, 3 s)
 *   SOMVR_PROBE_SKIP=A,B     hook names not to install (e.g. P_UI105,P_DEATH), for bisecting a crash
 *   SOMVR_PROBE_NOGUARD_OK=1 allow the active words without spike1's save guard in place (default: refused)
 *
 * Words (docs/runtime-probes.md has the question each one answers). Read-only:
 *   state on|off      the state-change logger (default on): interface/menu state, pause, video, player, life, mount,
 *                     cinematic, scripted camera, photo mode, input mode bytes, state tags (names)
 *   bindings          PlayerInput's 349 command records -> OUT/bindings-SEQ.tsv (4 bound inputs each, scale, active)
 *   pieces            the player model's piece names (count, index, name; the head/hood/hair candidates)
 *   nodes             the player skeleton from the model data (index, parent, name, stored hash vs the hash of the
 *                     name, world transform) plus the socket table -> OUT/nodes-SEQ.tsv; the head bone = the parent
 *                     node of socket SocketHead (fallback: candidate names through GetNode); logs the eye offset
 *                     from it to the gameplay camera and picks the node that pose/fpcam use
 *   pose N            N frames of head-node samples: before/after FUN_00a51210 (animation update), after the camera
 *                     post (FUN_00a35340) and at the renderer's visibility request right after H1 (vf_107)
 *   orcs              Combat objects near the player (targeting bands, hooks), sorted by distance
 *   attacks           every Combat/Attack record name -> OUT/attacks.tsv
 *   ui                UI / 3D surface sizes and textures, then 120 calls of CRendererHandle::vf_105 (who queues the
 *                     Scaleform display, target, layer mask)
 *   pmcount [SECS]    PlayerMovement::vf_11 (the root-motion slot, hook P_PMROOT) calls per probe tick for SECS
 *                     (default 5, max 60): histogram, the PlayerMovement objects and threads seen (re-m1m2.md X2)
 *   mark TEXT         a MARK line (sync with the harness)          sleep MS    delay the following words
 * Active (refused unless spike1's save guard is in place, see SOMVR_PROBE_NOGUARD_OK):
 *   cmd ID|NAME... [ms=N]  commands through PlayerInput: ON (FUN_00aab660), held as "on" for every polled query
 *                     (CWBInput vf_25), OFF (FUN_00aab5d0) after N ms (default 120; `ms=` because a bare number
 *                     would be ambiguous with a command id)
 *   move X Y [MS]     analog move stick (StrafeAxis 6 = X right, ForwardAxis 3 = Y forward) for MS (default 1000)
 *   look YAW PITCH [MS]  camera axes (YawAxis 13 / YawAccel 23, PitchAxis 12 / PitchAccel 22)
 *   pad SPEC... [MS]  a virtual pad at the binding layer: SPEC = A B X Y LB RB LT RT BACK START L3 R3 UP DOWN LEFT RIGHT
 *                     or AXIS=V (LX LY RX RY LT RT); presses every command bound to that pad input
 *   release           release every virtual input now
 *   padmode on|off    force pad mode (FUN_00a0eeb0, auto-switch back to KBM off) / back to KBM
 *   dealhit [NAME|last|auto] [kill]   one hit on the nearest live orc through Combat_DealHit (FUN_026d4ff0)
 *   hidehead [SUB...] / showhead      SetPieceHidden on pieces whose name contains SUB (default head hair hood helm
 *                     face eye mouth teeth beard brow lash; '*' = every piece) / undo
 *   fpcam on [fwd=CM] [up=CM] [pitch=DEG] / fpcam off   gameplay camera at the head node (PlayerBody::vf_28 after the
 *                     camera post-process, cam+0x484 reset)
 *   near CM | near off               the near-plane override 0x46b65e4
 *   autocounter CMD|off [FRAMES]     press CMD for FRAMES (default 3) when a nearby orc's parry window opens (+0xe9)
 *   exec CMDLINE      the engine's console executor (CWBEngine::vf_32 0x8bd8b0 = (*(void***)eng)[32](eng, s), eng =
 *                     *0x466afa8; re-m1m2.md G22) on this game-thread tick; the rest of the probe word is the string.
 *                     `\;` = a ';' for the game (the probe's own ';' ends the word; or one exec per word). @x @y @z
 *                     (optionally +N/-N, e.g. @x+100) = the player root's coordinate, @out = the OUT directory.
 *                     At most 400 characters and 48 tokens after the expansion. Logs the console find of every
 *                     command name (unknown names become silent pending variables), a variable's +0x38 before and
 *                     after, the return value, the time, absolute-path arguments (exists?) and the root for 3 ticks
 *   pmmove DX DY DZ   one-shot extra move through PlayerMovement (P+0x2c8): +0x428 = {DX,DY,DZ} cm in Talion's frame
 *                     (x right, y up, z forward; each |D| <= 200), then the pending byte +0x444 = 1; vf_11 consumes it.
 *                     Refused unless the fields are at rest (0 0 0 / quat 0 0 0 1 / 0); logs them before and after,
 *                     the vf_11 call that consumed it and the root for the next 3 ticks
 * P7 words (docs/runtime-probes.md P7; re-combat-vr.md, re-combat-items.md, re-hud.md). Read-only:
 *   timers            the object timers (HOBJECT+0x68) of the player and the nearest orcs vs the simulation timer, the
 *                     game timer and each object's default timer
 *   pick [SUB...]     the target orc for @ox/@oy/@oz, swingevent and hitstop: the nearest live AI character (its record
 *                     name containing a SUB; default: not a slave, human or animal)
 *   stimlog on|off    every AI stimulus: FUN_026c7180's callers (start, STIMULUS key, stop, the probe) and its two
 *                     RegisterStimulus calls (record, source, target, persistent, the id)
 *   aiwatch on|off    the orcs' counter goal (vf_07) and block/dodge actions: context-check results (changes) and starts
 *   keytimes on|off   at each attack start the key times Combat+0xc8.., the tracker, rate, KeyLookup and SampleNode's
 *                     R_Hand at the impact; at the attacker's APPLYDAMAGE the measured time and the live hand
 *   projlog on|off    live projectiles (CProjectile::vf_13): spawn data, flight near the player, the end; their impacts
 *   fxlog on|off      every CLIENTFX_CREATESTRUCT init (19 call sites): FX file, flags, parent; `off` = the name table
 *   soundlog on|off   every AK PostEvent through the game's two wrappers (event name, object); `off` = the name table
 *   weapons           the player's 8 weapon slots: record, active/shown bytes, model entries with their node names
 *   syncinfo NAME|PREFIX*...  AnimationData.Sync and every field of the Sync record, AttackPostData.AOEOnHit, the stimuli
 *   attacks full      attacks.tsv plus OUT/attacks-full.tsv (stimuli, IsBlock, projectiles, DodgeChance, Sync, AOE)
 *   navdump           the nav markers: record, shown bytes, world position, icon attributes -> OUT/nav-SEQ.tsv
 *   prompts [watch on|off]  the live entries of the three HUD prompt diamonds (action, StringId, IconId, category)
 *   uiscene           CUISceneMgr's state, then the next 20 UI scene views (P_UISCENE; the first 3 of a boot anyway)
 *   res               the render sizes: ScreenWidth/Height cvars, the device's back buffer/UI/3D sizes and textures, the
 *                     3D and UI rects, rt3D, P_RES3D's last height (re-resolution.md 1.5)
 *   trav [SECS]       the traversal state for SECS (default 20): FSM P+0x7f0, PlayerMovement +0x1a2/+0x1a6/+0x1f8/+0x200,
 *                     the proxy's gravity, FUN_00a36db0 (supported), the root and head height; on change and every 0.5 s
 *                     (the STATE line also carries fsm=)
 * Active (P7):
 *   swingevent NAME|last|auto [MS] [cur]  FUN_026c7180(pc, AttackData(rec), hOrc) toward the picked/nearest orc; `cur`
 *                     = pc+0x4d0/+0x55c/+0x4c0 hold the swing for MS (default 400); its persistent stimulus is removed
 *                     after MS; the target's AI reactions are counted for 2 s
 *   hitstop MS [rate] [scale=S] | hitstop off   hold the last dealhit target / picked / nearest orc for MS (<= 3000):
 *                     its own timer (SetObjectTimer, num 0 or 1/S-th) or every tracker at rate 0, then restored
 *   projblock [M] | projreflect [M] [own] | proj off   the next projectile within M m (default 1.5) of the player:
 *                     +0x228 = 1 (removed) / turned at its shooter (homing, spline, target cleared; own: +0x58 = us)
 *   weaponfx NAME|last [loop] | weaponfx stop   the WEAPONFX key's recipe on the active weapon's model object
 *   dagger hand|back [slot=N]  the dagger's inactive node -> L_Weapon and FUN_0231e2e0 (re-attach), then back
 *   sound NAME|last [hand]  CSoundMgr::vf_13 (2D) or our own Wwise object at the right hand
 *   restall on|off    P_RES3D passes the ScreenHeight cvar as the 3D surface's height at the next mode set (then
 *                     `exec SetResolution 1832 1920`): the tall eye size of re-resolution.md 1.4; only at the front end
 *   piecehide N|off   SetPieceHidden on piece N alone (the last one shown again): one shot per piece finds the head
 *   exec also expands @ox @oy @oz (the picked orc) and @cout (C:\Temp: Feral's VFS puts it at
 *                     ${AppPreferences}/VFS/Temp, inside RUN); a C: argument's host file is checked and copied to OUT
 * P8 words (docs/runtime-probes.md P8; re-arms-ik.md 6.2, re-ui-pointer.md 7, re-traversal.md 2.6/3.6, re-resolution.md
 * 2.4). Read-only:
 *   attached          the player model's attached children (*(sm+0x90) entries, one level down too): child object, node
 *                     or socket, offset, its model, pieces and the hidden-piece mask chain (model, renderable, node)
 *   wnodes [SLOT]     the weapon model's own skeleton and sockets (OUT/wnodes-SEQ.tsv) and its attach entry
 *   ikwatch [SECS] | ikwatch off   the engine's arm IK on Talion: chain setups (P_IK_SETUP_A/_B), every 10th update of
 *                     an active slot (P_IK_CHAIN), IK_LHand/IK_RHand/L_Hand/R_Hand in model space every 0.5 s
 *   ikcb [SECS] | ikcb off   node callbacks added/removed on Talion (IEngineModel slots 38/39: type, node, owner)
 *   mouselog on|off   the mouse path: system events (P_LP_SYS_*: gates, states, pad mode), CUIView (P_LP_VIEW_*: which view,
 *                     result), GFx HandleEvent (P_LP_GFX_EVENT), the pad guard's answers (P_LP_GUARD_*)
 *   mstate            im+0x3210.. (the stored mouse), the UI size, m_GameUI's view offset and its movie's mouse, focus bit,
 *                     cursor count, the menu / player-UI states, pad mode
 *   hitscan           the top menu layer's components hit-tested at the last `pointer` (to-stage + the clip's hitTest)
 *   surf [M]          two rays of M m (default 2): from the head along Talion's forward, from the camera along its view:
 *                     hit, object, surface record, CanWallClimb/IsSlippery
 *   mount [SECS]      while mounted: MountInfo, the beast, the rider's attachment (on change and every 0.5 s)
 *   conlog on|off     the console's output as CONOUT lines (exec logs its own call's output anyway)
 *   piecehide all     (active) every piece of hChar's model hidden at once; piecehide/attachhide log the mask chain then
 *                     and 2 ticks later (PIECEMASK)
 * Active (P8):
 *   attachhide ID [node] | attachhide off   one attached child hidden, with the children attached to it: their pieces
 *                     (those the game shows), or their render nodes' skip byte +0x40
 *   armpose rot DEG | armpose ik X Y Z | armpose off   the right arm in the phase-2 pose (P_TRK_EVAL, the player's tracker
 *                     only): the R_Hand subtree turned about the forearm, or a two-bone IK to (X Y Z) cm in Talion's model
 *                     space; once a second the next update's R_Hand and the sword vs object x pose'[node] x offset
 *                     (P_ATTACH_LISTEN)
 *   aggro [N] [loose] [SUB...] | aggro off   SetCombatScriptedTarget(npc, Talion, sticky) for the picked orc and the N-1
 *                     nearest others (pick's filter, or SUB); AGGROWATCH for 40 s; off = ClearCombatScriptedTarget
 *   minhealth V | minhealth off   Talion's min health CCharacter+0x9b4 (SetMinHealth): damage cannot take him below V
 *   pointer X Y | pointer u=U v=V | pointer off   the UI mouse moved through CInterfaceMgr FUN_00a0fa20 (UI pixels)
 *   click [BTN] [FRAMES]   FUN_00a0f760 down at the pointer, FUN_00a0f8c0 up FRAMES ticks later (default 1, 2)
 *   padguard off | padguard on   the menu items' pad guard answers 0 (the items act in pad mode) / as the game
 *   mcmd CMD          a command ON now, OFF next tick (100 = Mouse1: the Nemesis screen's pick)
 *   grav off | grav on   PlayerMovement::vf_06 answers 0 (no gravity from the next step) / as the game
 *   hitstop ... onbpw (P7's word) armed: the next counter prompt at Talion starts the hold on that orc; the hold reads the
 *                     playing tracker (FindTracker of Combat+0x17c)
 * P9 words (docs/runtime-probes.md P9; re-combat-spawn.md 1-3, re-bow.md 6). Read-only:
 *   blocktable [N] [ATTACK|last]  the picked orc's (and N-1 others') AIBlockTable: its character combat record (as the
 *                     roll reads it, FUN_026ff330), the Entries' Category and BlockChance; with ATTACK its categories and
 *                     the chance an attack start with it rolls
 *   aiblockwatch on|off  the roll path in Combat_StartAttack at an NPC: the gate (P_AIB_GATE), the record (P_AIB_REC), the
 *                     roll and its chance (P_AIB_ROLL), the incoming-attack fact (P_AIB_NOTIFY), CAIGoalCounterMelee's
 *                     relevance on change (P_GOAL_REL), the damage context's shield test (P_SHIELD); counts at on/off
 *   templates [SUB]   the AI/Templates records and the archetype (AI_Defender ...) each references -> OUT/templates.tsv
 *   tod / weathers    the weather state (G->vtbl+0xa8: Record, GDBIndex, TOD) / the Weather/Definition names
 *   wmdump            every player weapon mode with its components (Predicate with the command FUN_00e24570 reads, Data,
 *                     SlowMo, FreeAim, VisionMode, UIMode) -> OUT/wmodes-SEQ.tsv, and the player's WeaponModeSetup
 * Active (P9):
 *   spawn WHAT[,WHAT] [CM] [LEVEL]  the server's debug spawn FUN_029a4830 from our code: a template by name, else the best
 *                     one referencing archetype WHAT (AI_Defender ...), else the best name containing it; CM (default 500)
 *                     ahead of Talion on the ground (a ray down), facing him; the debug spawn's post steps (NPC vtbl+0x288,
 *                     the level); the new orc is picked
 *   spawncaptain [CM] the current faction target (`exec Faction.SetTarget ROW COL` first) with its template, bound to it
 *   despawn [all]     the last (every) spawned orc removed (the object system's deferred remove; no guard needed)
 *   incoming [roll] [ATTACK|last]  FUN_029ac890(Talion, the picked orc): the orc is told it is being attacked (step 1b of
 *                     re-combat-spawn.md 2.6); `roll` = only if our roll from its table with ATTACK's categories wins
 *                     (alias aiattacked)
 *   blockroll force | blockroll off   P_AIB_ROLL returns its low bound: every attack start that has a table entry notifies
 *   wmhold TOKEN[,TOKEN] [MS]   press the entry command of the first weapon mode a token finds (a name substring, or
 *                     `freeaim` = one with a FreeAim component) for MS (default 3000)
 *   aimray YAW PITCH [hand] [SECS] | aimray off   the free-aim ray (P_BOW_CAMXF, FUN_00a5b8b0's transform) turned by YAW
 *                     deg right and PITCH deg up, from R_Hand with `hand`, for SECS (default 6); AIMRAY lines twice a second
 * P8 fixes in P9: dealhit takes the picked (or spawned) orc first; `dealhit auto` skips the player's PC_Recoil_* records.
 * P10 words (docs/runtime-probes.md P10; design-locomotion.md 2, 4.2). Read-only:
 *   speed [SECS]      per tick for SECS (default 4, max 60): the root's horizontal and vertical speed, the solver's velocity
 *                     pm+0xe0, the mode pm+0x1a2, FSM, the graph's RawMoveMag/Game_Velocity/Game_SmoothedVelocity/DeltaYaw
 *                     (FUN_0208dd90 + FUN_0208de50), the probe's stick, the game's root delta (P_ROOTDELTA) and loco's,
 *                     the state tags -> OUT/speed-SEQ.tsv; SPEED lines every 0.5 s and on a tag change; the end: the
 *                     plateau, the time to 90 %, the stop time and distance after the stick's release
 *   pmrec             the player record pm+0x448 through Client.PlayerMovement's 30 descriptors (*0x4c254e0: the reader's
 *                     fast path and the hash search), the caches pm+0x32c/+0x450/+0x454, the proxy's gravity pc+0x58, the
 *                     console find of PlayerMovement.ATTR, Client.PlayerMovement.ATTR and ATTR
 *   heading clear [M] | open | wall [M] | back | fwd | cam | orc | YAW   the probe's heading (a world yaw) for walk, loco,
 *                     fling and npcpush: 16 rays at 40 and 100 cm with ground checks; clear = a free direction (>= M m,
 *                     default 15) nearest Talion's facing, open = the freest, wall = the nearest obstacle within M m
 *                     (default 10); or Talion's facing, the camera's, toward the picked orc, +180, a yaw in degrees
 *   vaultlog [SECS]   for SECS (default 4, max 30) per tick: the player's sync action (Combat+0x188: +0x10 record, +0x1f8
 *                     target), attack, FSM and root, the partner orc's position, yaw and attack, the movement Combat's
 *                     +0x56c (the attack's EnableCollision, somvr's SOMVR_PHASE) -> OUT/vault-SEQ.tsv; VAULT lines on change
 *                     and per episode (duration, displacement, path, largest step, yaw change, EnableCollision 0 seen)
 * Active (P10):
 *   walk MAG [MS] | walk off   the stock stick (StrafeAxis/ForwardAxis) along the heading at MAG (0..1) for MS (default
 *                     3000, max 15000), re-aimed through the camera's yaw every tick (a mirrored x is found once and flipped)
 *   loco const VX VZ SECS [world] | loco stick SECS [V] | loco off   P_ROOTDELTA replaces the game's root translation in
 *                     FSM 6/7 (not mounted, attached or in a sync) by v*dt: const = VX right / VZ forward of the heading
 *                     (Talion's facing without one; `world` = world axes) in cm/s; stick = the probe's held stick through
 *                     the camera's yaw at V (default 500) cm/s; LOCO lines (asked vs the root, locks, skips), then the stop
 *   fling VX VY VZ [world]   momentum mode (FUN_020c6ff0(pm, 0)), pm+0xe0 = the launch velocity (cm/s; x right, y up,
 *                     z forward of the heading), gravity on; the arc, FSM path and landing per tick -> OUT/fling-SEQ.tsv;
 *                     root-motion mode back after the landing if the graph did not do it
 *   animrate R [SECS] [all] | animrate off   Talion's main tracker (0xff; all = every playing one) at rate R (0.05..3) for
 *                     SECS (default 3, max 20), set again if the game writes it, then restored; the root's speed during it
 *                     vs the second before
 *   npcpush [CM] [V]  spawn AI_Infantry CM (default 150) ahead along the heading; 1.2 s later loco at it at V (default 250)
 *                     cm/s to 1 m past it, then a one-shot push (pm+0x428) of up to 2 m at it: the closest approach
 *   pmrec set ATTR V | pmrec restore   exec ATTR's console name (the first the find knows) with V; restore = the old
 *                     values; pmrec base V | pmrec base off   pm+0x32c (BaseSpeedMultiplier's cache) = V, then back
 * P9 fix in P10: wmhold takes an exact mode name first, then a substring; `freeaim` = a mode whose FreeAim element is a
 * record (P9: every mode has one, mostly null, so `freeaim` took mode 0, PC_Stealth).
 *
 * The words run on the probe's game-thread tick (after CWBInput::vf_04, hook P_INPUT), one ';'-separated word at a
 * time; dealhit, swingevent, hitstop, dagger, weaponfx, aggro, minhealth, spawn, spawncaptain, despawn, incoming and
 * npcpush wait for the next server tick (after CGameServerShell::vf_10).
 * Hooks (call-site rel32 / vtable slot patches, never prologues; each site is byte-checked first and skipped on any
 * mismatch; disjoint from spike1's and somvr's sites, checked by build.sh). The game was built with GCC 8.3 (IPA-RA),
 * so call-site hooks enter through asm wrappers that save and restore every call-clobbered register: CALLWRAP (post
 * hooks: the C side calls the original) restores all but rax; PREWRAP (log-only pre hooks) restores everything and
 * jumps to the original, so stack arguments and return values are untouched. Vtable slots take plain C functions (the
 * calls are indirect, so the caller assumes nothing about the callee's registers).
 * Inert unless /proc/self/exe is named ShadowOfMordor, the cmdline has no --type= (CEF helpers), the main
 * executable's in-memory build-id note matches and SOMVR_PROBE_LOG is set; then it removes itself from LD_PRELOAD.
 * Every read of game memory along a pointer chain goes through process_vm_readv (a bad pointer gives 0, not a fault).
 * Logs are rate-limited per category (the first 60 lines, then 8 per second; SUPPRESSED counts every second).
 * Build: ./build.sh   Offline check: ./build.sh --check
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <locale.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#include "somvr_probe_cmds.h"

#define SOM_BUILDID  "87ec5d6fd94afd47a24d5da7c9b0c2b74b30c6e9"
#define SOM_EXE_NAME "ShadowOfMordor"
#define NCMD 349

/* ------------------------------------------------------------------------------------------------------------ */
/* Sites                                                                                                          */

enum { K_CALL = 1, K_VSLOT = 2, K_ENTRY = 3 };
struct site_spec {
	const char *name;
	int kind;
	uintptr_t addr;           /* K_CALL: the E8 byte; K_VSLOT: the 8-byte slot; K_ENTRY: the function */
	uintptr_t ctx;            /* start of the bytes checked (contains addr) */
	const char *ctx_hex;      /* expected bytes from ctx on */
	uintptr_t target;         /* expected call target / slot value / the function itself */
	const char *prologue_hex; /* expected first bytes of the target */
};
#define SITE(n, k, a, c, h, t, p) { n, k, a, c, h, t, p }

enum { HK_INPUT, HK_VALUE, HK_VALUE2, HK_ISON, HK_SERVER, HK_VIS, HK_UI105, HK_ANIM_A, HK_ANIM_B, HK_CAMPOST_A,
       HK_CAMPOST_B, HK_ATK_OBJ, HK_ATK_POS, HK_ATK_NOPOS, HK_DEALHIT, HK_APPLYDMG, HK_COUNTER, HK_HITEV, HK_DEATH,
       HK_PMROOT,
       /* P7 */
       HK_STIM_START, HK_STIM_KEY, HK_STIM_STOP, HK_REGSTIM, HK_REGSTIM_P, HK_AI_GOAL, HK_AI_BLOCK_OK, HK_AI_BLOCK_GO,
       HK_AI_DODGE_OK, HK_AI_DODGE_GO, HK_KEYTIMES, HK_PJ_UPDATE, HK_PJ_DAMAGE, HK_SND_POST, HK_SND_POST_CB, HK_UISCENE,
       HK_FX0, HK_FX_LAST = HK_FX0 + 18, HK_RES3D,
       /* P8 */
       HK_IK_SETUP_A, HK_IK_SETUP_B, HK_IK_CHAIN, HK_IEM_ADDCB, HK_IEM_REMCB, HK_TRK_EVAL, HK_ATTACH_LISTEN,
       HK_LP_GUARD_HOVER, HK_LP_GUARD_CLICK, HK_LP_SYS_DOWN, HK_LP_SYS_UP, HK_LP_SYS_MOVE, HK_LP_VIEW_DOWN, HK_LP_VIEW_UP,
       HK_LP_VIEW_MOVE, HK_LP_GFX_EVENT, HK_GRAV,
       /* P9 */
       HK_AIB_GATE, HK_AIB_REC, HK_AIB_ROLL, HK_AIB_NOTIFY, HK_GOAL_REL, HK_SHIELD, HK_BOW_CAMXF,
       /* P10 */
       HK_ROOTDELTA, HK_N };
#define HK_FX_N (HK_FX_LAST - HK_FX0 + 1) /* the 19 calls of the CLIENTFX_CREATESTRUCT init FUN_00a09e70 */

/* SITES-BEGIN (hooks; generated by gen_sites.py from the binary; one SITE per line, order = the HK_ enum) */
static const struct site_spec g_hook_sites[HK_N] = {
SITE("P_INPUT", K_VSLOT, 0x34798c0, 0x34798c0, "b0 23 90 00 00 00 00 00", 0x9023b0, "80 bf 3c 02 00 00 00 75 07 c3 66 0f 1f 44 00"),
SITE("P_VALUE", K_VSLOT, 0x3479950, 0x3479950, "90 35 90 00 00 00 00 00", 0x903590, "41 54 55 48 89 fd 53 48 89 f3 48 83 ec 10 48"),
SITE("P_VALUE2", K_VSLOT, 0x3479958, 0x3479958, "90 39 90 00 00 00 00 00", 0x903990, "41 54 55 48 89 fd 53 48 89 f3 48 83 ec 10 48"),
SITE("P_ISON", K_VSLOT, 0x3479968, 0x3479968, "e0 11 90 00 00 00 00 00", 0x9011e0, "48 85 f6 0f 84 57 02 00 00 41 54 55 53 48 8b"),
SITE("P_SERVER", K_VSLOT, 0x3987690, 0x3987690, "50 7c 77 02 00 00 00 00", 0x2777c50, "53 48 89 fb 48 8b 3d 4d 33 ef 01 48 8b 07 ff"),
SITE("P_VIS", K_VSLOT, 0x335cd90, 0x335cd90, "70 2e 70 00 00 00 00 00", 0x702e70, "48 8b 3d 31 81 f6 03 41 54 49 89 f4 55 53 48"),
SITE("P_UI105", K_VSLOT, 0x335cd80, 0x335cd80, "b0 51 70 00 00 00 00 00", 0x7051b0, "41 54 41 89 f4 55 89 d5 53 e8 02 87 02 00 bf"),
SITE("P_ANIM_A", K_CALL, 0xa41a55, 0xa41a45, "03 00 00 83 78 30 03 75 0c 48 8b bb d8 02 00 00 e8 b6 f7 00 00", 0xa51210, "53 48 89 fb c6 87 e0 05 00 00 00 48 8b bf b8"),
SITE("P_ANIM_B", K_CALL, 0xa41363, 0xa41353, "03 00 00 83 78 30 03 75 0c 48 8b bb d8 02 00 00 e8 a8 fe 00 00", 0xa51210, "53 48 89 fb c6 87 e0 05 00 00 00 48 8b bf b8"),
SITE("P_CAMPOST_A", K_CALL, 0xa41ab7, 0xa41aa7, "00 48 8b bb f0 02 00 00 e8 0c 20 09 00 48 89 df e8 84 38 ff ff", 0xa35340, "41 54 66 0f ef d2 55 53 48 89 fb 48 83 c4 80"),
SITE("P_CAMPOST_B", K_CALL, 0xa413c5, 0xa413b5, "00 48 8b bb f0 02 00 00 e8 fe 26 09 00 48 89 df e8 76 3f ff ff", 0xa35340, "41 54 66 0f ef d2 55 53 48 89 fb 48 83 c4 80"),
SITE("P_ATK_OBJ", K_CALL, 0xa8cb19, 0xa8cb09, "ea 4c 89 ee 4c 89 f7 41 51 44 8b 8b d8 04 00 00 e8 72 12 c6 01", 0x26edd90, "4c 89 87 70 01 00 00 44 0f b6 44 24 08 44 89"),
SITE("P_ATK_POS", K_CALL, 0xa8d02b, 0xa8d01b, "58 04 00 00 4c 89 e1 48 89 ea 4c 89 ee 4c 89 f7 e8 10 0d c6 01", 0x26edd40, "f3 41 0f 10 08 f3 0f 11 8f 50 01 00 00 f3 41"),
SITE("P_ATK_NOPOS", K_CALL, 0xa8d04d, 0xa8d03d, "60 45 89 c8 4c 89 e1 48 89 ea 4c 89 ee 4c 89 f7 e8 5e 09 c6 01", 0x26ed9b0, "48 c7 87 50 01 00 00 00 00 00 00 45 0f b6 c0"),
SITE("P_DEALHIT", K_CALL, 0x26e5468, 0x26e5458, "7e 84 24 a8 00 00 00 f3 0f 10 8c 24 b0 00 00 00 e8 83 fb fe ff", 0x26d4ff0, "41 57 49 89 f7 41 56 41 89 ce 41 55 41 54 55"),
SITE("P_APPLYDMG", K_CALL, 0x26e5af5, 0x26e5ae5, "54 49 89 d4 55 48 89 f5 53 48 89 fb 48 83 ec 50 e8 a6 ee ff ff", 0x26e49a0, "41 57 41 56 41 55 41 54 55 53 48 81 ec 28 01"),
SITE("P_COUNTER", K_CALL, 0x26fc32b, 0x26fc31b, "66 39 c2 0f 83 bc 09 00 00 29 d0 89 c6 48 89 df e8 60 9c fe ff", 0x26e5f90, "48 83 bf 80 00 00 00 00 0f 84 32 02 00 00 41"),
SITE("P_HITEV", K_CALL, 0x2819a9a, 0x2819a8a, "8b b5 38 ff ff ff 48 83 c4 40 4c 89 e6 4c 89 f7 e8 e1 8a ed ff", 0x26f2580, "41 57 41 56 41 55 41 54 49 89 fc 55 48 89 f5"),
SITE("P_DEATH", K_CALL, 0x281985d, 0x281984d, "e8 be ac eb ff 31 c9 48 89 da 48 89 c6 48 89 ef e8 8e c9 ed ff", 0x26f61f0, "80 bf 88 02 00 00 00 0f 85 f3 0e 00 00 55 48"),
SITE("P_PMROOT", K_VSLOT, 0x34fc838, 0x34fc838, "70 5e ab 00 00 00 00 00", 0xab5e70, "55 48 89 fd 66 0f ef db 53 48 89 f3 48 83 ec"),
SITE("P_STIM_START", K_CALL, 0x26ed119, 0x26ed109, "fd ff e9 3a eb ff ff 4c 89 e2 4c 89 ee 48 89 df e8 62 a0 fd ff", 0x26c7180, "41 57 41 56 41 55 49 89 f5 41 54 55 48 89 d5"),
SITE("P_STIM_KEY", K_CALL, 0x26fd1d7, 0x26fd1c7, "41 39 fe 48 8b 93 c0 04 00 00 48 89 df 48 89 c6 e8 a4 9f fc ff", 0x26c7180, "41 57 41 56 41 55 49 89 f5 41 54 55 48 89 d5"),
SITE("P_STIM_STOP", K_CALL, 0x26eed28, 0x26eed18, "48 83 7b 70 00 74 0e 4c 89 ea 48 89 ee 48 89 df e8 53 84 fd ff", 0x26c7180, "41 57 41 56 41 55 49 89 f5 41 54 55 48 89 d5"),
SITE("P_REGSTIM", K_CALL, 0x26c747f, 0x26c746f, "00 48 8b 3d 69 8c 58 02 48 8d b4 24 80 00 00 00 e8 9c 73 27 00", 0x293e820, "80 7f 24 00 75 4f 80 3d 63 88 30 02 00 74 51"),
SITE("P_REGSTIM_P", K_CALL, 0x26c76bf, 0x26c76af, "00 48 8b 3d 29 8a 58 02 48 8d b4 24 80 00 00 00 e8 5c 71 27 00", 0x293e820, "80 7f 24 00 75 4f 80 3d 63 88 30 02 00 74 51"),
SITE("P_AI_GOAL", K_VSLOT, 0x3a4e0f8, 0x3a4e0f8, "80 6d 9d 02 00 00 00 00", 0x29d6d80, "53 48 89 fb e8 07 c5 ff ff 48 8b 7b 08 e8 5e"),
SITE("P_AI_BLOCK_OK", K_VSLOT, 0x3a3d828, 0x3a3d828, "10 55 9c 02 00 00 00 00", 0x29c5510, "41 54 49 89 fc 55 53 48 83 ec 70 48 8b 6f 08"),
SITE("P_AI_BLOCK_GO", K_VSLOT, 0x3a3d7e8, 0x3a3d7e8, "d0 61 9c 02 00 00 00 00", 0x29c61d0, "53 48 89 fb 48 83 ec 20 e8 a3 7a ff ff 48 8b"),
SITE("P_AI_DODGE_OK", K_VSLOT, 0x3a3e428, 0x3a3e428, "90 65 9c 02 00 00 00 00", 0x29c6590, "41 57 41 56 41 55 41 54 55 53 48 81 ec 98 01"),
SITE("P_AI_DODGE_GO", K_VSLOT, 0x3a3e3e8, 0x3a3e3e8, "90 7e 9c 02 00 00 00 00", 0x29c7e90, "41 57 41 56 45 31 f6 41 55 45 31 ed 41 54 49"),
SITE("P_KEYTIMES", K_CALL, 0x26eb77a, 0x26eb76a, "8b b7 ff 48 8b b3 a0 04 00 00 4c 89 ff 48 89 ea e8 11 7e b7 ff", 0x2263590, "41 57 41 56 41 55 41 54 49 89 fc 55 48 89 f5"),
SITE("P_PJ_UPDATE", K_VSLOT, 0x3998748, 0x3998748, "20 5d 7c 02 00 00 00 00", 0x27c5d20, "55 53 48 89 fb 48 83 ec 08 f3 0f 10 05 ef ec"),
SITE("P_PJ_DAMAGE", K_CALL, 0x27bf8ba, 0x27bf8aa, "48 8b 54 24 70 4c 8b 8c 24 b0 00 00 00 48 89 de e8 51 95 f0 ff", 0x26c8e10, "41 57 49 89 f7 41 56 41 55 4d 89 cd 41 54 49"),
SITE("P_SND_POST", K_CALL, 0x8c9865, 0x8c9855, "f0 48 89 d7 45 31 c9 6a 00 45 31 c0 31 c9 31 d2 e8 06 e0 b9 00", 0x1467870, "41 57 4d 89 c7 41 56 49 89 ce 41 55 41 89 d5"),
SITE("P_SND_POST_CB", K_CALL, 0x8c988d, 0x8c987d, "00 45 31 c9 b9 a0 8b 8c 00 ba 05 00 00 00 6a 00 e8 de df b9 00", 0x1467870, "41 57 4d 89 c7 41 56 49 89 ce 41 55 41 89 d5"),
SITE("P_UISCENE", K_CALL, 0xc1ee09, 0xc1edf9, "8b 07 ff 90 d8 00 00 00 48 8d 74 24 50 48 89 c7 e8 42 37 dd ff", 0x9f2550, "55 48 89 fd 53 48 89 f3 48 83 ec 08 48 8b 07"),
SITE("P_FXI_A07FE1", K_CALL, 0xa07fe1, 0xa07fd1, "44 24 10 31 c9 48 89 c2 48 8d 7c 24 30 4c 89 ce e8 8a 1e 00 00", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_A08081", K_CALL, 0xa08081, 0xa08071, "44 24 10 31 c9 48 89 c2 48 8d 7c 24 30 4c 89 ce e8 ea 1d 00 00", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_E1E6D9", K_CALL, 0xe1e6d9, 0xe1e6c9, "49 89 e8 b9 01 00 00 00 4c 89 e6 48 8d 7c 24 10 e8 92 b7 be ff", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_E22DF2", K_CALL, 0xe22df2, 0xe22de2, "00 00 4c 89 e6 4c 8d b4 24 f0 01 00 00 4c 89 f7 e8 79 70 be ff", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_26E5E34", K_CALL, 0x26e5e34, 0x26e5e24, "00 00 4c 89 ee 4d 89 f0 31 c9 48 89 c2 48 89 e7 e8 37 40 32 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_26E674B", K_CALL, 0x26e674b, 0x26e673b, "00 4c 89 f6 48 8d 7c 24 10 4c 8b 42 08 48 89 c2 e8 20 37 32 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_26E6DE8", K_CALL, 0x26e6de8, 0x26e6dd8, "4c 8b 44 24 10 4c 89 fe 44 89 e1 48 8d 7c 24 30 e8 83 30 32 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_26EDEAB", K_CALL, 0x26edeab, 0x26ede9b, "67 fe 4c 89 e6 31 c9 48 89 ea 49 89 c0 48 89 e7 e8 c0 bf 31 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_26EEECA", K_CALL, 0x26eeeca, 0x26eeeba, "24 90 01 00 00 4c 89 ce 31 c9 48 89 c2 4c 89 e7 e8 a1 af 31 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_26F34F1", K_CALL, 0x26f34f1, 0x26f34e1, "48 8b 54 24 08 48 89 c6 48 89 ef 4c 8b 01 31 c9 e8 7a 69 31 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_26F3659", K_CALL, 0x26f3659, 0x26f3649, "48 8b 54 24 08 31 c9 49 89 c0 48 89 de 48 89 ef e8 12 68 31 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_26FC738", K_CALL, 0x26fc738, 0x26fc728, "4c 8d 6c 24 40 48 89 c2 4d 89 e0 31 c9 4c 89 ef e8 33 d7 30 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_26FCBD7", K_CALL, 0x26fcbd7, 0x26fcbc7, "8b 44 24 08 31 c9 4c 89 ef 48 8b 34 24 48 89 c2 e8 94 d2 30 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_273C276", K_CALL, 0x273c276, 0x273c266, "f8 ff 31 c9 4c 89 e2 4c 89 f6 49 89 c0 48 89 ef e8 f5 db 2c fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_273C4FB", K_CALL, 0x273c4fb, 0x273c4eb, "24 40 4c 89 f6 49 89 c0 31 c9 48 89 ef 4c 89 e2 e8 70 d9 2c fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_27BD11B", K_CALL, 0x27bd11b, 0x27bd10b, "00 4c 89 e7 31 c9 4c 89 fe 4c 8b 42 08 48 89 c2 e8 50 cd 24 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_27BD205", K_CALL, 0x27bd205, 0x27bd1f5, "74 40 4c 89 e7 49 89 d8 31 c9 48 89 c2 4c 89 f6 e8 66 cc 24 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_27BD30E", K_CALL, 0x27bd30e, 0x27bd2fe, "5a fe 49 89 c0 4c 89 e7 31 c9 4c 89 ea 48 89 de e8 5d cb 24 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_27BDF60", K_CALL, 0x27bdf60, 0x27bdf50, "49 89 c0 48 8d 7c 24 60 31 c9 4c 89 ea 4c 89 e6 e8 0b bf 24 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_RES3D", K_CALL, 0x741ceb, 0x741cdb, "c0 f3 44 0f 5e c7 f3 44 0f 58 c1 f3 49 0f 2c c8 e8 f0 df ff ff", 0x73fce0, "44 8b 15 6d 03 ee 03 66 0f ef c0 48 c7 46 3c"),
SITE("P_IK_SETUP_A", K_CALL, 0x21b875e, 0x21b874e, "08 4d 89 6c 24 68 48 8b 53 50 4c 89 e7 4c 89 ee e8 9d d1 fc ff", 0x2185900, "41 55 41 54 55 53 48 89 fb 48 89 d7 48 83 ec"),
SITE("P_IK_SETUP_B", K_CALL, 0x21b8c8a, 0x21b8c7a, "48 08 4c 89 63 68 48 8b 55 50 4c 89 e6 48 89 df e8 71 cc fc ff", 0x2185900, "41 55 41 54 55 53 48 89 fb 48 89 d7 48 83 ec"),
SITE("P_IK_CHAIN", K_CALL, 0x20a51d4, 0x20a51c4, "8d ab d8 34 00 00 4c 8d a3 38 39 00 00 48 89 ef e8 b7 03 0e 00", 0x2185590, "8b 87 0c 01 00 00 85 c0 0f 84 4a 01 00 00 53"),
SITE("P_IEM_ADDCB", K_VSLOT, 0x3484260, 0x3484260, "70 ad 91 00 00 00 00 00", 0x91ad70, "48 85 f6 74 6b 41 55 48 89 f7 41 89 d5 41 54"),
SITE("P_IEM_REMCB", K_VSLOT, 0x3484268, 0x3484268, "f0 ad 91 00 00 00 00 00", 0x91adf0, "48 85 f6 74 5b 41 55 48 89 f7 41 89 d5 41 54"),
SITE("P_TRK_EVAL", K_VSLOT, 0x38df398, 0x38df398, "b0 99 38 02 00 00 00 00", 0x23899b0, "48 8b 97 08 08 00 00 c6 87 48 01 00 00 00 48"),
SITE("P_ATTACH_LISTEN", K_VSLOT, 0x38d8958, 0x38d8958, "60 a6 35 02 00 00 00 00", 0x235a660, "48 83 bf 80 00 00 00 00 0f 84 9a 06 00 00 41"),
SITE("P_LP_GUARD_HOVER", K_CALL, 0xb4e5d8, 0xb4e5c8, "48 89 d3 e8 a0 4f e9 ff 48 85 c0 74 0c 48 89 c7 e8 a3 a8 ee ff", 0xa38e80, "48 83 ec 08 48 8b 05 d5 06 c7 03 0f b6 b7 31"),
SITE("P_LP_GUARD_CLICK", K_CALL, 0xb4e62c, 0xb4e61c, "af b5 03 e8 4c 4f e9 ff 48 85 c0 74 0c 48 89 c7 e8 4f a8 ee ff", 0xa38e80, "48 83 ec 08 48 8b 05 d5 06 c7 03 0f b6 b7 31"),
SITE("P_LP_SYS_DOWN", K_VSLOT, 0x34e4230, 0x34e4230, "20 41 9f 00 00 00 00 00", 0x9f4120, "80 3d 9d 54 cb 03 00 74 06 80 7f 10 00 74 06"),
SITE("P_LP_SYS_UP", K_VSLOT, 0x34e4238, 0x34e4238, "c0 41 9f 00 00 00 00 00", 0x9f41c0, "80 3d fd 53 cb 03 00 74 06 80 7f 10 00 74 06"),
SITE("P_LP_SYS_MOVE", K_VSLOT, 0x34e4240, 0x34e4240, "60 42 9f 00 00 00 00 00", 0x9f4260, "80 3d 5d 53 cb 03 00 74 06 80 7f 10 00 74 06"),
SITE("P_LP_VIEW_DOWN", K_VSLOT, 0x3482248, 0x3482248, "d0 b6 90 00 00 00 00 00", 0x90b6d0, "53 48 63 f6 66 0f ef c0 66 0f ef c9 48 89 fb"),
SITE("P_LP_VIEW_UP", K_VSLOT, 0x3482250, 0x3482250, "70 b7 90 00 00 00 00 00", 0x90b770, "53 48 63 f6 66 0f ef c0 66 0f ef c9 48 89 fb"),
SITE("P_LP_VIEW_MOVE", K_VSLOT, 0x3482258, 0x3482258, "10 b8 90 00 00 00 00 00", 0x90b810, "48 83 ec 28 66 2b 57 2c 66 0f ef c0 66 2b 77"),
SITE("P_LP_GFX_EVENT", K_VSLOT, 0x3763ec0, 0x3763ec0, "d0 a3 24 01 00 00 00 00", 0x124a3d0, "41 54 55 48 89 fd 53 48 89 f3 48 83 ec 20 48"),
SITE("P_GRAV", K_VSLOT, 0x34fc810, 0x34fc810, "a0 58 ab 00 00 00 00 00", 0xab58a0, "41 54 55 53 48 8b 5f 50 48 85 db 0f 84 7f 00"),
SITE("P_AIB_GATE", K_CALL, 0x26eb818, 0x26eb808, "7c 24 38 74 1b 4d 85 ff 74 16 48 89 ee 4c 89 f7 e8 d3 f6 ba ff", 0x229aef0, "48 85 ff 0f 84 57 01 00 00 41 54 48 89 f0 55"),
SITE("P_AIB_REC", K_CALL, 0x26ecbc7, 0x26ecbb7, "00 00 0f 85 69 ec ff ff 49 8b 47 10 48 8b 78 70 e8 64 27 01 00", 0x26ff330, "48 8b 35 09 7f 52 02 83 7e 48 01 7e 2b 48 85"),
SITE("P_AIB_ROLL", K_CALL, 0x26ecd50, 0x26ecd40, "0f 10 0d 29 7a c5 00 66 0f ef c0 f3 0f 11 14 24 e8 eb 66 58 fe", 0xc73440, "48 83 ec 18 f3 0f 11 44 24 0c f3 0f 11 4c 24"),
SITE("P_AIB_NOTIFY", K_CALL, 0x26ecd6a, 0x26ecd5a, "0f 2f d0 0f 86 c5 ea ff ff 48 8b 7b 70 4c 89 fe e8 21 fb 2b 00", 0x29ac890, "41 55 49 89 f5 41 54 55 53 48 83 ec 38 48 8b"),
SITE("P_GOAL_REL", K_VSLOT, 0x3a4e0d8, 0x3a4e0d8, "90 6b 9d 02 00 00 00 00", 0x29d6b90, "41 54 55 53 48 8b 6f 08 48 89 fb 48 8b 85 20"),
SITE("P_SHIELD", K_CALL, 0x28177c4, 0x28177b4, "41 0f 10 87 bc 01 00 00 48 8b 74 24 50 48 89 ef e8 67 67 09 00", 0x28adf30, "f3 0f 10 8f a8 00 00 00 0f 2f 0d 49 a7 a9 00"),
SITE("P_BOW_CAMXF", K_CALL, 0xa9a1d0, 0xa9a1c0, "00 48 8b 03 48 8d 74 24 40 48 8b b8 d0 02 00 00 e8 db 16 fc ff", 0xa5b8b0, "55 48 89 fd 53 48 89 f3 48 83 ec 28 e8 6f 8e"),
SITE("P_ROOTDELTA", K_CALL, 0xab5ea9, 0xab5e99, "44 24 28 00 00 80 3f 48 c7 44 24 20 00 00 00 00 e8 f2 b4 f9 ff", 0xa513a0, "55 48 89 fd 53 48 89 f3 48 83 ec 18 48 8b 47"),
};
/* SITES-END */

enum { FN_CMD_ON, FN_CMD_OFF, FN_CMD_ACTIVE, FN_INPUT_MODE, FN_DEALHIT, FN_ATK_BYNAME, FN_MODELOF, FN_GETNODE,
       FN_NODEXF, FN_HASHINDEX, FN_GETSOCKET, FN_NUMPIECES, FN_GETPIECE, FN_PIECENAME, FN_HIDEPIECE, FN_VIDEOCOVER,
       FN_BODYSETCAM, FN_CAMRESET, FN_LOCALPLAYER,
       /* P7 */
       FN_STIM, FN_ATTACKDATA, FN_STIM_REMOVE, FN_TM_CREATE, FN_TM_ATTACH, FN_TM_DETACH, FN_TM_SETOBJ, FN_TM_GETOBJ,
       FN_TM_RELEASE, FN_GETCURANIM, FN_GETANIMTIME, FN_GETANIMRATE, FN_SETANIMRATE, FN_KEYLOOKUP, FN_SAMPLENODE,
       FN_FINDTRACKER, FN_FX_CSINIT, FN_FX_CREATE, FN_FX_CSFREE, FN_FX_RELEASE, FN_ITEM_REATTACH, FN_ITEM_VISIBLE,
       FN_SND_REGOBJ, FN_SND_SETPOS, FN_SND_POST, FN_NAV_POS, FN_SUPPORTED,
       /* P8 */
       FN_AI_SCRIPTTGT, FN_AI_SCRIPTCLR, FN_IM_MOVE, FN_IM_DOWN, FN_IM_UP, FN_VIEW_MSTATE, FN_VIEW_TOSTAGE, FN_CLIP_VIEW,
       FN_CLIP_HITTEST, FN_SURF_FROM_RAY,
       /* P9 */
       FN_SPAWN_AI, FN_TMPL_BYNAME, FN_ZONE_AT, FN_SETLEVEL, FN_FM_BIND, FN_FM_ACTIVATE, FN_FACTION_TGT, FN_FM_TEMPLATE,
       FN_CHAR_COMBATREC, FN_AI_FACT, FN_TOD_NAME, FN_WM_CMDID,
       /* P10 */
       FN_GRAPH_VAR, FN_GRAPH_GET, FN_VELMODE, FN_N };

/* SITES-BEGIN (functions the probe calls: checked like K_ENTRY sites, never patched; order = the FN_ enum) */
static const struct site_spec g_fn_sites[FN_N] = {
SITE("F_CMD_ON", K_ENTRY, 0xaab660, 0xaab660, "80 7f 78 00 75 0a c3 66 0f 1f 84 00 00 00 00 00", 0xaab660, "80 7f 78 00 75 0a c3 66 0f 1f 84 00 00 00 00"),
SITE("F_CMD_OFF", K_ENTRY, 0xaab5d0, 0xaab5d0, "80 7f 78 00 75 0a c3 66 0f 1f 84 00 00 00 00 00", 0xaab5d0, "80 7f 78 00 75 0a c3 66 0f 1f 84 00 00 00 00"),
SITE("F_CMD_ACTIVE", K_ENTRY, 0xaab900, 0xaab900, "89 f2 48 81 fa 5c 01 00 00 77 1a 48 c1 ea 06 89", 0xaab900, "89 f2 48 81 fa 5c 01 00 00 77 1a 48 c1 ea 06"),
SITE("F_INPUT_MODE", K_ENTRY, 0xa0eeb0, 0xa0eeb0, "41 54 49 89 fc 55 48 89 f5 53 48 8b 7e 08 0f b6", 0xa0eeb0, "41 54 49 89 fc 55 48 89 f5 53 48 8b 7e 08 0f"),
SITE("F_DEALHIT", K_ENTRY, 0x26d4ff0, 0x26d4ff0, "41 57 49 89 f7 41 56 41 89 ce 41 55 41 54 55 48", 0x26d4ff0, "41 57 49 89 f7 41 56 41 89 ce 41 55 41 54 55"),
SITE("F_ATK_BYNAME", K_ENTRY, 0x2166790, 0x2166790, "0f b6 07 31 d2 84 c0 74 21 0f 1f 80 00 00 00 00", 0x2166790, "0f b6 07 31 d2 84 c0 74 21 0f 1f 80 00 00 00"),
SITE("F_MODELOF", K_ENTRY, 0x8f3c20, 0x8f3c20, "48 8b 7f 38 48 85 ff 74 0f 48 8b 07 48 8b 40 20", 0x8f3c20, "48 8b 7f 38 48 85 ff 74 0f 48 8b 07 48 8b 40"),
SITE("F_GETNODE", K_ENTRY, 0x91a750, 0x91a750, "41 54 55 48 89 cd 53 48 85 f6 74 34 48 89 f7 48", 0x91a750, "41 54 55 48 89 cd 53 48 85 f6 74 34 48 89 f7"),
SITE("F_NODEXF", K_ENTRY, 0x91c360, 0x91c360, "48 85 f6 0f 84 97 00 00 00 41 55 48 89 f7 49 89", 0x91c360, "48 85 f6 0f 84 97 00 00 00 41 55 48 89 f7 49"),
SITE("F_HASHINDEX", K_ENTRY, 0x91ac20, 0x91ac20, "48 85 f6 74 3b 53 48 89 f7 89 d3 e8 f0 8f fd ff", 0x91ac20, "48 85 f6 74 3b 53 48 89 f7 89 d3 e8 f0 8f fd"),
SITE("F_GETSOCKET", K_ENTRY, 0x91b410, 0x91b410, "48 85 f6 74 4b 55 48 89 f7 48 89 cd 53 48 89 d3", 0x91b410, "48 85 f6 74 4b 55 48 89 f7 48 89 cd 53 48 89"),
SITE("F_NUMPIECES", K_ENTRY, 0x91ae60, 0x91ae60, "48 85 f6 74 3b 53 48 89 f7 48 89 d3 e8 af 8d fd", 0x91ae60, "48 85 f6 74 3b 53 48 89 f7 48 89 d3 e8 af 8d"),
SITE("F_GETPIECE", K_ENTRY, 0x91aeb0, 0x91aeb0, "c7 01 ff ff ff ff 48 85 f6 74 4d 55 48 89 f7 48", 0x91aeb0, "c7 01 ff ff ff ff 48 85 f6 74 4d 55 48 89 f7"),
SITE("F_PIECENAME", K_ENTRY, 0x91b510, 0x91b510, "48 85 f6 74 4b 41 54 48 89 f7 49 89 cc 55 44 89", 0x91b510, "48 85 f6 74 4b 41 54 48 89 f7 49 89 cc 55 44"),
SITE("F_HIDEPIECE", K_ENTRY, 0x91b310, 0x91b310, "48 85 f6 74 4b 55 48 89 f7 89 d5 53 0f b6 d9 48", 0x91b310, "48 85 f6 74 4b 55 48 89 f7 89 d5 53 0f b6 d9"),
SITE("F_VIDEOCOVER", K_ENTRY, 0xa10d30, 0xa10d30, "48 81 c7 f0 2a 00 00 e9 f4 3e 21 00 0f 1f 40 00", 0xa10d30, "48 81 c7 f0 2a 00 00 e9 f4 3e 21 00 0f 1f 40"),
SITE("F_BODYSETCAM", K_ENTRY, 0xa4bef0, 0xa4bef0, "48 8b 4f 08 f3 0f 10 06 48 89 f2 f3 0f 10 4e 04", 0xa4bef0, "48 8b 4f 08 f3 0f 10 06 48 89 f2 f3 0f 10 4e"),
SITE("F_CAMRESET", K_ENTRY, 0xa5afb0, 0xa5afb0, "48 8b 87 4c 04 00 00 f3 0f 10 87 54 04 00 00 48", 0xa5afb0, "48 8b 87 4c 04 00 00 f3 0f 10 87 54 04 00 00"),
SITE("F_LOCALPLAYER", K_ENTRY, 0x9e3570, 0x9e3570, "53 48 8d 9f a0 00 00 00 48 8d b7 80 00 00 00 48", 0x9e3570, "53 48 8d 9f a0 00 00 00 48 8d b7 80 00 00 00"),
SITE("F_STIM", K_ENTRY, 0x26c7180, 0x26c7180, "41 57 41 56 41 55 49 89 f5 41 54 55 48 89 d5 53", 0x26c7180, "41 57 41 56 41 55 49 89 f5 41 54 55 48 89 d5"),
SITE("F_ATTACKDATA", K_ENTRY, 0xa91300, 0xa91300, "48 8b 35 b9 10 11 04 83 7e 48 01 7e 33 48 85 ff", 0xa91300, "48 8b 35 b9 10 11 04 83 7e 48 01 7e 33 48 85"),
SITE("F_STIM_REMOVE", K_ENTRY, 0x293e150, 0x293e150, "83 fe ff 74 29 80 7f 24 00 75 23 48 8b 47 10 48", 0x293e150, "83 fe ff 74 29 80 7f 24 00 75 23 48 8b 47 10"),
SITE("F_TIMER_CREATE", K_ENTRY, 0x8f8fc0, 0x8f8fc0, "53 48 8b 05 18 1f d7 03 48 89 fb 48 8d b8 70 03", 0x8f8fc0, "53 48 8b 05 18 1f d7 03 48 89 fb 48 8d b8 70"),
SITE("F_TIMER_ATTACH", K_ENTRY, 0x8f8f60, 0x8f8f60, "48 8b 3e 48 85 ff 74 18 48 8b 32 48 85 f6 74 10", 0x8f8f60, "48 8b 3e 48 85 ff 74 18 48 8b 32 48 85 f6 74"),
SITE("F_TIMER_DETACH", K_ENTRY, 0x8f8f90, 0x8f8f90, "48 8b 3e 48 85 ff 74 18 48 83 ec 08 e8 ef cf 07", 0x8f8f90, "48 8b 3e 48 85 ff 74 18 48 83 ec 08 e8 ef cf"),
SITE("F_TIMER_SETOBJ", K_ENTRY, 0x8f9050, 0x8f9050, "48 85 f6 0f 84 4f 01 00 00 53 48 89 f3 48 83 ec", 0x8f9050, "48 85 f6 0f 84 4f 01 00 00 53 48 89 f3 48 83"),
SITE("F_TIMER_GETOBJ", K_ENTRY, 0x8f8ec0, 0x8f8ec0, "48 89 f8 48 85 d2 74 28 48 8b 52 68 48 89 17 48", 0x8f8ec0, "48 89 f8 48 85 d2 74 28 48 8b 52 68 48 89 17"),
SITE("F_TIMER_RELEASE", K_ENTRY, 0x9ec850, 0x9ec850, "48 8b 3f 48 85 ff 74 18 8b 47 08 8d 50 ff 85 c0", 0x9ec850, "48 8b 3f 48 85 ff 74 18 8b 47 08 8d 50 ff 85"),
SITE("F_GETCURANIM", K_ENTRY, 0x91ba10, 0x91ba10, "48 85 f6 74 4b 55 48 89 f7 48 89 cd 53 0f b6 da", 0x91ba10, "48 85 f6 74 4b 55 48 89 f7 48 89 cd 53 0f b6"),
SITE("F_GETANIMTIME", K_ENTRY, 0x91a520, 0x91a520, "55 48 89 cd 53 48 83 ec 08 48 85 f6 74 32 48 89", 0x91a520, "55 48 89 cd 53 48 83 ec 08 48 85 f6 74 32 48"),
SITE("F_GETANIMRATE", K_ENTRY, 0x91a580, 0x91a580, "55 48 89 cd 53 48 83 ec 08 48 85 f6 74 32 48 89", 0x91a580, "55 48 89 cd 53 48 83 ec 08 48 85 f6 74 32 48"),
SITE("F_SETANIMRATE", K_ENTRY, 0x91bd90, 0x91bd90, "48 85 f6 74 4b 53 48 89 f7 0f b6 da 48 83 ec 10", 0x91bd90, "48 85 f6 74 4b 53 48 89 f7 0f b6 da 48 83 ec"),
SITE("F_KEYLOOKUP", K_ENTRY, 0x91b220, 0x91b220, "41 57 41 56 41 55 41 54 55 53 48 83 ec 08 4c 8b", 0x91b220, "41 57 41 56 41 55 41 54 55 53 48 83 ec 08 4c"),
SITE("F_SAMPLENODE", K_ENTRY, 0x91c410, 0x91c410, "41 56 41 55 41 54 55 53 4c 89 cb 44 8b 74 24 30", 0x91c410, "41 56 41 55 41 54 55 53 4c 89 cb 44 8b 74 24"),
SITE("F_FINDTRACKER", K_ENTRY, 0xc73800, 0xc73800, "41 56 49 89 f6 41 55 41 89 d5 41 54 55 48 89 fd", 0xc73800, "41 56 49 89 f6 41 55 41 89 d5 41 54 55 48 89"),
SITE("F_FX_CSINIT", K_ENTRY, 0xa09e70, 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89 cd", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("F_FX_CREATE", K_ENTRY, 0xd8c7f0, 0xd8c7f0, "80 3d da aa 92 03 00 75 07 31 c0 c3 0f 1f 40 00", 0xd8c7f0, "80 3d da aa 92 03 00 75 07 31 c0 c3 0f 1f 40"),
SITE("F_FX_CSFREE", K_ENTRY, 0x9ec750, 0x9ec750, "55 53 48 89 fb 48 83 ec 08 80 bf ac 01 00 00 00", 0x9ec750, "55 53 48 89 fb 48 83 ec 08 80 bf ac 01 00 00"),
SITE("F_FX_RELEASE", K_ENTRY, 0x2aebfd0, 0x2aebfd0, "48 8b 07 48 85 c0 74 50 53 48 89 fb 48 89 c7 e8", 0x2aebfd0, "48 8b 07 48 85 c0 74 50 53 48 89 fb 48 89 c7"),
SITE("F_ITEM_REATTACH", K_ENTRY, 0x231e2e0, 0x231e2e0, "41 55 49 89 fd 41 54 55 53 48 83 ec 08 0f b6 9f", 0x231e2e0, "41 55 49 89 fd 41 54 55 53 48 83 ec 08 0f b6"),
SITE("F_ITEM_VISIBLE", K_ENTRY, 0x231e700, 0x231e700, "41 57 41 56 41 55 41 54 41 89 f4 55 53 48 89 fb", 0x231e700, "41 57 41 56 41 55 41 54 41 89 f4 55 53 48 89"),
SITE("F_SND_REGOBJ", K_ENTRY, 0x8c97c0, 0x8c97c0, "48 89 d0 48 83 ec 08 89 f7 ba 01 00 00 00 48 89", 0x8c97c0, "48 89 d0 48 83 ec 08 89 f7 ba 01 00 00 00 48"),
SITE("F_SND_SETPOS", K_ENTRY, 0x8c9b80, 0x8c9b80, "41 54 41 89 f4 55 53 48 89 fb 48 83 ec 30 48 8b", 0x8c9b80, "41 54 41 89 f4 55 53 48 89 fb 48 83 ec 30 48"),
SITE("F_SND_POST", K_ENTRY, 0x8c9840, 0x8c9840, "48 83 ec 08 89 f0 83 fe ff 48 c7 c6 ff ff ff ff", 0x8c9840, "48 83 ec 08 89 f0 83 fe ff 48 c7 c6 ff ff ff"),
SITE("F_NAV_POS", K_ENTRY, 0xebc700, 0xebc700, "48 8b 87 50 02 00 00 48 85 c0 74 0f 48 89 c7 48", 0xebc700, "48 8b 87 50 02 00 00 48 85 c0 74 0f 48 89 c7"),
SITE("F_SUPPORTED", K_ENTRY, 0xa36db0, 0xa36db0, "48 8b 87 20 04 00 00 48 85 c0 74 24 48 8b 40 10", 0xa36db0, "48 8b 87 20 04 00 00 48 85 c0 74 24 48 8b 40"),
SITE("F_AI_SCRIPTTGT", K_ENTRY, 0x296f870, 0x296f870, "48 85 f6 0f 84 b7 00 00 00 41 54 41 89 d4 55 48", 0x296f870, "48 85 f6 0f 84 b7 00 00 00 41 54 41 89 d4 55"),
SITE("F_AI_SCRIPTCLR", K_ENTRY, 0x296f940, 0x296f940, "48 8b 87 78 0c 00 00 48 8b 90 60 0b 00 00 48 85", 0x296f940, "48 8b 87 78 0c 00 00 48 8b 90 60 0b 00 00 48"),
SITE("F_IM_MOVE", K_ENTRY, 0xa0fa20, 0xa0fa20, "41 54 66 0f ef c9 49 89 d4 66 0f ef c0 55 89 f5", 0xa0fa20, "41 54 66 0f ef c9 49 89 d4 66 0f ef c0 55 89"),
SITE("F_IM_DOWN", K_ENTRY, 0xa0f760, 0xa0f760, "41 55 41 89 f5 41 54 41 89 d4 55 48 89 cd 53 48", 0xa0f760, "41 55 41 89 f5 41 54 41 89 d4 55 48 89 cd 53"),
SITE("F_IM_UP", K_ENTRY, 0xa0f8c0, 0xa0f8c0, "41 55 41 89 f5 41 54 41 89 d4 55 48 89 cd 53 48", 0xa0f8c0, "41 55 41 89 f5 41 54 41 89 d4 55 48 89 cd 53"),
SITE("F_VIEW_MSTATE", K_ENTRY, 0x90b960, 0x90b960, "48 83 ec 18 48 8b 7f 18 31 f6 48 8d 4c 24 0c 4c", 0x90b960, "48 83 ec 18 48 8b 7f 18 31 f6 48 8d 4c 24 0c"),
SITE("F_VIEW_TOSTAGE", K_ENTRY, 0xc83ba0, 0xc83ba0, "55 53 48 83 ec 28 48 85 ff 74 18 48 8b 07 89 d5", 0xc83ba0, "55 53 48 83 ec 28 48 85 ff 74 18 48 8b 07 89"),
SITE("F_CLIP_VIEW", K_ENTRY, 0x9783b0, 0x9783b0, "48 8b 47 28 48 8b 78 08 48 8b 07 ff a0 40 01 00", 0x9783b0, "48 8b 47 28 48 8b 78 08 48 8b 07 ff a0 40 01"),
SITE("F_CLIP_HITTEST", K_ENTRY, 0x979db0, 0x979db0, "55 48 89 f8 66 0f ef c0 41 b9 04 00 00 00 f2 0f", 0x979db0, "55 48 89 f8 66 0f ef c0 41 b9 04 00 00 00 f2"),
SITE("F_SURF_FROM_RAY", K_ENTRY, 0x2ae5d90, 0x2ae5d90, "48 8b 47 20 48 85 c0 74 45 0f b7 48 5a 89 ce 85", 0x2ae5d90, "48 8b 47 20 48 85 c0 74 45 0f b7 48 5a 89 ce"),
SITE("F_SPAWN_AI", K_ENTRY, 0x29a4830, 0x29a4830, "48 85 ff 0f 84 b7 07 00 00 41 57 48 89 f8 45 89", 0x29a4830, "48 85 ff 0f 84 b7 07 00 00 41 57 48 89 f8 45"),
SITE("F_TMPL_BYNAME", K_ENTRY, 0x2153910, 0x2153910, "0f b6 07 31 d2 84 c0 74 21 0f 1f 80 00 00 00 00", 0x2153910, "0f b6 07 31 d2 84 c0 74 21 0f 1f 80 00 00 00"),
SITE("F_ZONE_AT", K_ENTRY, 0x22a2a80, 0x22a2a80, "41 57 41 56 41 55 41 54 49 89 d4 55 53 48 89 fb", 0x22a2a80, "41 57 41 56 41 55 41 54 49 89 d4 55 53 48 89"),
SITE("F_SETLEVEL", K_ENTRY, 0x26ee7c0, 0x26ee7c0, "53 ba 01 00 00 00 48 89 fb e8 82 f7 ff ff 48 89", 0x26ee7c0, "53 ba 01 00 00 00 48 89 fb e8 82 f7 ff ff 48"),
SITE("F_FM_BIND", K_ENTRY, 0x2975eb0, 0x2975eb0, "41 56 41 55 49 89 f5 41 54 49 89 fc 55 53 89 d3", 0x2975eb0, "41 56 41 55 49 89 f5 41 54 49 89 fc 55 53 89"),
SITE("F_FM_ACTIVATE", K_ENTRY, 0x22cdad0, 0x22cdad0, "41 55 41 54 41 89 f4 55 48 89 fd 53 48 83 ec 18", 0x22cdad0, "41 55 41 54 41 89 f4 55 48 89 fd 53 48 83 ec"),
SITE("F_FACTION_TARGET", K_ENTRY, 0x23d3f80, 0x23d3f80, "48 83 ec 08 48 8b b7 e8 00 00 00 e8 a0 49 00 00", 0x23d3f80, "48 83 ec 08 48 8b b7 e8 00 00 00 e8 a0 49 00"),
SITE("F_FM_TEMPLATE", K_ENTRY, 0x22d71b0, 0x22d71b0, "48 8b 77 60 48 8b 7f 58 e9 e3 39 03 00 90 66 90", 0x22d71b0, "48 8b 77 60 48 8b 7f 58 e9 e3 39 03 00 90 66"),
SITE("F_CHAR_COMBATREC", K_ENTRY, 0x26ff330, 0x26ff330, "48 8b 35 09 7f 52 02 83 7e 48 01 7e 2b 48 85 ff", 0x26ff330, "48 8b 35 09 7f 52 02 83 7e 48 01 7e 2b 48 85"),
SITE("F_AI_FACT", K_ENTRY, 0x29ac890, 0x29ac890, "41 55 49 89 f5 41 54 55 53 48 83 ec 38 48 8b 6f", 0x29ac890, "41 55 49 89 f5 41 54 55 53 48 83 ec 38 48 8b"),
SITE("F_TOD_NAME", K_ENTRY, 0x25ccd10, 0x25ccd10, "53 0f b6 05 78 8e 66 02 89 fb 84 c0 74 22 39 1d", 0x25ccd10, "53 0f b6 05 78 8e 66 02 89 fb 84 c0 74 22 39"),
SITE("F_WM_CMDID", K_ENTRY, 0xe24570, 0xe24570, "55 53 48 89 fb 48 83 ec 18 48 8b 05 20 f0 d8 03", 0xe24570, "55 53 48 89 fb 48 83 ec 18 48 8b 05 20 f0 d8"),
SITE("F_GRAPH_VAR", K_ENTRY, 0x208dd90, 0x208dd90, "0f b6 06 31 d2 84 c0 74 21 0f 1f 80 00 00 00 00", 0x208dd90, "0f b6 06 31 d2 84 c0 74 21 0f 1f 80 00 00 00"),
SITE("F_GRAPH_GET", K_ENTRY, 0x208de50, 0x208de50, "48 85 f6 74 7b 48 8b 46 18 66 0f ef c0 48 39 87", 0x208de50, "48 85 f6 74 7b 48 8b 46 18 66 0f ef c0 48 39"),
SITE("F_VELMODE", K_ENTRY, 0x20c6ff0, 0x20c6ff0, "80 bf a2 01 00 00 00 74 05 40 84 f6 74 12 40 88", 0x20c6ff0, "80 bf a2 01 00 00 00 74 05 40 84 f6 74 12 40"),
};
/* SITES-END */

enum { RO_RUNSTRING, RO_SERVERGATE, RO_EXEC, RO_REPO_FIND, RO_OBJSYS_MH, RO_SND_GLOBAL, RO_PHYS_RAY, RO_NPC_SETZONE,
       RO_OBJSYS_REMOVE, RO_N };

/* SITES-BEGIN (the console and more slots: read and called through, never patched; G22_RUNSTRING, _SERVERGATE and
 * _EXEC as re-m1m2.md section 9 prints them; OBJSYS_MODELHANDLE = the object system's vtbl+0x118 (weaponfx),
 * SND_POSTGLOBAL = CSoundMgr::vf_13 (sound), PHYS_RAYCAST = IEnginePhysics::vf_64 (surf, spawn); P9: NPC_SETZONE = the
 * NPC's vtbl+0x288 (spawn's post step), OBJSYS_REMOVE = the object system's vtbl+0x58 (despawn); order = the RO_ enum) */
static const struct site_spec g_ro_sites[RO_N] = {
SITE("G22_RUNSTRING", K_VSLOT, 0x344d650, 0x344d640, "20 d3 8b 00 00 00 00 00 30 d3 8b 00 00 00 00 00 b0 d8 8b 00 00 00 00 00 80 d8 8b 00 00 00 00 00 40 d3 8b 00 00 00 00 00", 0x8bd8b0, "53 48 89 f3 e8 77 f8 ff ff 48 89 de 48 89 c7"),
SITE("G22_SERVERGATE", K_VSLOT, 0x344d7c8, 0x344d7c0, "40 e0 8b 00 00 00 00 00 f0 d4 8b 00 00 00 00 00 f0 d9 8b 00 00 00 00 00", 0x8bd4f0, "48 8b 05 e9 d9 da 03 48 83 b8 58 0c 00 00 00"),
SITE("G22_EXEC", K_ENTRY, 0x8b9ac0, 0x8b9ac0, "41 57 41 56 41 55 41 54 55 53 48 81 ec 28 1a 00", 0x8b9ac0, "41 57 41 56 41 55 41 54 55 53 48 81 ec 28 1a"),
SITE("G22_REPO_FIND", K_VSLOT, 0x3910d00, 0x3910d00, "60 5e 45 02 00 00 00 00", 0x2455e60, "41 57 41 56 41 55 41 54 55 31 ed 53 48 83 ec"),
SITE("OBJSYS_MODELHANDLE", K_VSLOT, 0x3453170, 0x3453170, "e0 6a 8c 00 00 00 00 00", 0x8c6ae0, "48 85 f6 74 0b 48 8b 86 e0 00 00 00 c3 0f 1f"),
SITE("SND_POSTGLOBAL", K_VSLOT, 0x346da80, 0x346da80, "30 d7 8e 00 00 00 00 00", 0x8ed730, "80 7f 08 01 75 22 48 85 f6 74 1d 80 3e 00 74"),
SITE("PHYS_RAYCAST", K_VSLOT, 0x3472458, 0x3472458, "d0 4f 8f 00 00 00 00 00", 0x8f4fd0, "48 8b 7f 08 48 85 ff 74 07 e9 02 96 fe ff 66"),
SITE("NPC_SETZONE", K_VSLOT, 0x3a2b658, 0x3a2b658, "b0 39 96 02 00 00 00 00", 0x29639b0, "53 48 89 fb 48 83 ec 10 48 8b 47 10 48 8b 50"),
SITE("OBJSYS_REMOVE", K_VSLOT, 0x34530b0, 0x34530b0, "60 6f 8c 00 00 00 00 00", 0x8c6f60, "48 85 f6 74 1b 48 83 ec 08 48 8b 7f 08 e8 66"),
};
/* SITES-END */

/* ------------------------------------------------------------------------------------------------------------ */
/* Game addresses (the selftest points them at stand-ins) and offsets (docs: re-input, re-combat, re-firstperson,  */
/* re-ui-effects, re-stereo; each one read in the disassembly for this probe)                                     */

static struct game {
	uintptr_t shell_pp;     /* CGameClientShell* holder (FUN_009e8e00 writes it) */
	uintptr_t model_pp;     /* IEngineModel* */
	uintptr_t atkcat_pp;    /* "Combat/Attack" category (FUN_02166700) */
	uintptr_t pause_pp;     /* game timer: *(*(this)+8)+0x34 = paused (FUN_009e4770) */
	uintptr_t allow_pad;    /* byte: auto-switch KBM -> pad allowed (FUN_00a13790) */
	uintptr_t allow_kbm;    /* byte: auto-switch pad -> KBM allowed */
	uintptr_t near_ovr;     /* float near-plane override (re-view-render.md 2) */
	uintptr_t device;       /* the IdxD3D11 device object: +0x90/0x94 UI size, +0xd8/0xdc 3D size, +0x1b8/+0x1c0 tex */
	uintptr_t hash_table;   /* the 256-byte node-name hash table (re-animation.md 2.3) */
	uintptr_t guard_slot;   /* WBOnlineService slot 219: spike1's save guard replaces it */
	uintptr_t guard_orig;   /* its original value */
	uintptr_t vt_combat, vt_npc, vt_playerobj; /* vtables, to validate pointers before use */
	uintptr_t ra_vis_player; /* return address of the player view's vf_107 call (0x9ef039 in FUN_009ee170); 0 = any */
	uintptr_t eng_pp;       /* IWBEngine* holder (re-m1m2.md 2.2): vtable slot 32 = RunConsoleString */
	uintptr_t srv_pp;       /* CWBEngine::vf_79's global: *(*this + 0xc58) != 0 = the local server is up (G22_SERVERGATE) */
	uintptr_t repo;         /* the static CConsoleRepository (vtable vt_repo; slot 8 = find(name) -> program) */
	uintptr_t vt_repo, vt_userprog, vt_pm; /* CConsoleRepository, CConsoleUserProgram, PlayerMovement vtables */
	uintptr_t fn[FN_N];
	/* P7 (re-combat-vr.md, re-combat-items.md, re-hud.md) */
	uintptr_t timer_pp, vt_timer, vt_etimer; /* IEngineTimer* holder (CWBTimer vtable), CEngineTimer vtable */
	uintptr_t stim_mgr_pp, stim_off;    /* CAIStimulusMgr* holder; byte: no stimuli from the player (re-combat-vr 1.2) */
	uintptr_t sch_attack, sch_attackdata, sch_anim, sch_sync, sch_post, sch_aidata, sch_nav; /* DB schema holders */
	uintptr_t vt_proj;                  /* CProjectile */
	uintptr_t objsys_pp, vt_objsys;     /* the object system (vtbl+0x118 = model handle of an object) */
	uintptr_t snd_pp, vt_snd, snd_wrap; /* CSoundMgr* holder and vtable, the Wwise wrapper object */
	uintptr_t nav_head;                 /* the CNavMarkerInstance list (sentinel; first = +8, next = node+8) */
	uintptr_t vt_diamond_pad, vt_diamond_kb, vt_uiscene; /* CUIPromptDiamondGamePad/Keyboard, CUISceneMgr */
	uintptr_t fx_enabled;               /* byte: ClientFX on (FUN_00d8c7f0 creates nothing without it) */
	uintptr_t cvar_sw, cvar_sh, aspect_lock; /* ScreenWidth/ScreenHeight cvars (u32), AspectRatioLock (re-resolution.md) */
	/* P8 (re-arms-ik.md, re-ui-pointer.md, re-resolution.md 2, re-traversal.md, the aggro of runtime-probes.md P8) */
	uintptr_t vt_sm, vt_smrend, vt_rhandle, vt_rnode, vt_attach, vt_tracker; /* CSimpleModel, its renderable, the
	                                                  render handle and node, CSimpleModelAttachNode, ...TrackerNode */
	uintptr_t vt_uiview, vt_movie, vt_clip, vt_menusm, vt_puism, vt_sysh; /* CUIView, GFx MovieImpl, CUIMovieClip, the
	                                                  menu / player-UI state machines, CGameSystemEventHandler */
	uintptr_t sys_gate;                 /* byte: the system-event window-active gate (re-ui-pointer.md M2) */
	uintptr_t phys_pp, vt_phys, sch_surface; /* IEnginePhysics* holder and vtable; the Surface.Surface schema holder */
	uintptr_t text_lo, text_hi;         /* the executable's code segment (a function pointer read from game data must be
	                                       inside it before it is called) */
	/* P9 (re-combat-spawn.md, re-bow.md) */
	uintptr_t tmplcat_pp, wxcat_pp, wmcat_pp; /* AI/Templates, Weather/Definition, Combat/PlayerWeaponModes/Modes */
	uintptr_t gsvc_pp;                  /* the shared game services (vtbl+0x78 faction manager, +0xa8 weather state) */
	uintptr_t spawn_level;              /* u32: the debug spawner's level for non-members (FUN_027b4560) */
	uintptr_t class_npc;                /* the NPC class in an HOBJECT's class list */
	uintptr_t sch_ccr, sch_btbl, sch_bent; /* character combat record, Combat.AIBlockTable, CombatAIBlockChance */
	uintptr_t sch_wm, sch_wmpred, sch_wmdata, sch_wmslowmo, sch_wmfreeaim, sch_wmvision, sch_wmui, sch_wmsetup; /* the
	                                       Combat.PlayerWeaponMode schema and its components' (re-bow.md 1.1) */
	/* P10 (design-locomotion.md 2.2) */
	uintptr_t sch_pm, sch_pm_n;         /* Client.PlayerMovement: the descriptor array's holder (_INIT_1317: 0x4c25520)
	                                       and its u32 count (30) */
} G = {
	.shell_pp = 0x46a9560, .model_pp = 0x46a9230, .atkcat_pp = 0x4b568d0, .pause_pp = 0x463e290,
	.allow_pad = 0x46b0d60, .allow_kbm = 0x46b0d61, .near_ovr = 0x46b65e4, .device = 0x461fa40,
	.hash_table = 0x38d7480,
	.guard_slot = 0x3482a78, .guard_orig = 0x919970,
	.vt_combat = 0x396bb40, .vt_npc = 0x3a2b3d0, .vt_playerobj = 0x39959a0, .ra_vis_player = 0x9ef03f,
	.eng_pp = 0x466afa8, .srv_pp = 0x466aee0, .repo = 0x4b5e920,
	.vt_repo = 0x3910cc0, .vt_userprog = 0x336b4b0, .vt_pm = 0x34fc7e0,
	.fn = { 0xaab660, 0xaab5d0, 0xaab900, 0xa0eeb0, 0x26d4ff0, 0x2166790, 0x8f3c20, 0x91a750, 0x91c360, 0x91ac20,
		0x91b410, 0x91ae60, 0x91aeb0, 0x91b510, 0x91b310, 0xa10d30, 0xa4bef0, 0xa5afb0, 0x9e3570,
		/* P7 */
		0x26c7180, 0xa91300, 0x293e150, 0x8f8fc0, 0x8f8f60, 0x8f8f90, 0x8f9050, 0x8f8ec0, 0x9ec850, 0x91ba10,
		0x91a520, 0x91a580, 0x91bd90, 0x91b220, 0x91c410, 0xc73800, 0xa09e70, 0xd8c7f0, 0x9ec750, 0x2aebfd0,
		0x231e2e0, 0x231e700, 0x8c97c0, 0x8c9b80, 0x8c9840, 0xebc700, 0xa36db0,
		/* P8 */
		0x296f870, 0x296f940, 0xa0fa20, 0xa0f760, 0xa0f8c0, 0x90b960, 0xc83ba0, 0x9783b0, 0x979db0, 0x2ae5d90,
		/* P9 */
		0x29a4830, 0x2153910, 0x22a2a80, 0x26ee7c0, 0x2975eb0, 0x22cdad0, 0x23d3f80, 0x22d71b0, 0x26ff330, 0x29ac890,
		0x25ccd10, 0xe24570,
		/* P10 */
		0x208dd90, 0x208de50, 0x20c6ff0 },
	.timer_pp = 0x466c718, .vt_timer = 0x3473cd0, .vt_etimer = 0x34a4e08, .stim_mgr_pp = 0x4c500e0, .stim_off = 0x4c47090,
	.sch_attack = 0x4ba23c0, .sch_attackdata = 0x4ba7280, .sch_anim = 0x4ba8640, .sch_sync = 0x4baa4a0,
	.sch_post = 0x4ba6580, .sch_aidata = 0x4ba60a0, .sch_nav = 0x4b66f20, .vt_proj = 0x39986e0, .objsys_pp = 0x466afa0,
	.vt_objsys = 0x3453058, .snd_pp = 0x466c618, .vt_snd = 0x346da18, .snd_wrap = 0x466df00, .nav_head = 0x46b9200,
	.vt_diamond_pad = 0x353fb78, .vt_diamond_kb = 0x353fbb8, .vt_uiscene = 0x3542998, .fx_enabled = 0x46b72d1,
	.cvar_sw = 0x466ae18, .cvar_sh = 0x466add8, .aspect_lock = 0x461e2dc,
	.vt_sm = 0x38d82c8, .vt_smrend = 0x38de478, .vt_rhandle = 0x335b438, .vt_rnode = 0x335eab0, .vt_attach = 0x38d8948,
	.vt_tracker = 0x38df380, .vt_uiview = 0x3482040, .vt_movie = 0x3763da0, .vt_clip = 0x34adab0, .vt_menusm = 0x34ea140,
	.vt_puism = 0x3502498, .vt_sysh = 0x34e41f8, .sys_gate = 0x46a95c4, .phys_pp = 0x466c758, .vt_phys = 0x3472258,
	.sch_surface = 0x4c1cd00, .text_lo = 0x411000, .text_hi = 0x3340b7d,
	.tmplcat_pp = 0x4b56fb0, .wxcat_pp = 0x4b56300, .wmcat_pp = 0x4b567d0, .gsvc_pp = 0x46b7310, .spawn_level = 0x4c471cc,
	.class_npc = 0x4c52780, .sch_ccr = 0x4ba0ca0, .sch_btbl = 0x4ba6460, .sch_bent = 0x4ba64e0, .sch_wm = 0x4bb2320,
	.sch_wmpred = 0x4bb35a0, .sch_wmdata = 0x4bb33e0, .sch_wmslowmo = 0x4bb3040, .sch_wmfreeaim = 0x4bb2ca0,
	.sch_wmvision = 0x4bb3120, .sch_wmui = 0x4bb2f60, .sch_wmsetup = 0x4bb21e0,
	.sch_pm = 0x4c254e0, .sch_pm_n = 0x4c254e8,
};
static int g_fn_ok[FN_N];
static const struct site_spec *g_ro; /* g_ro_sites (the selftest: stand-ins) */
static int g_ro_ok[RO_N];

/* CGameClientShell */
#define SH_LOCALS     0x10    /* {int current (6 = none); vector<entry*> begin +0x18, end +0x20} */
#define SH_CINE       0xb0    /* cinematic camera manager; +0x20 = the active camera */
#define SH_UIRECT     0x80dc8 /* InterfaceWidth/Height rect */
#define SH_3DRECT     0x80df0
#define SH_IM         0x80ff8 /* CInterfaceMgr* */
#define SH_PROFILES   0x81060 /* FUN_00cb2430: +0x168 valid, profile = +0x8 */
/* local player entry (FUN_00c40c80): +0x0 index byte, +0x8 profile, +0x10 PlayerInput (embedded) */
#define EN_PROFILE    0x08
#define EN_PI         0x10
#define PROF_PAD      0x238   /* byte "using gamepad" (FUN_00cbe650) */
/* PlayerInput */
#define PI_ENTRIES    0x60    /* vector of 0x30-byte entries indexed by command id: +0x00 CWBInput record, +0x29 swallow */
#define PI_ENABLED    0x78
#define PI_ACTIVESET  0x130   /* bitset<349> */
#define PI_INDEX      0x160
#define PIE_SIZE      0x30
/* CWBInput command record (0x40): +0x08 callback, +0x10 user data (= the PlayerInput), +0x18 context, +0x20..0x2c
 * four input ids (-1 none), +0x30..0x3c four value-record indices; context +0x10 value records (0x14 bytes: scale,
 * threshold, .., +0x10 on), +0x2c pad index */
#define REC_USER      0x10
#define REC_CTX       0x18
#define REC_INPUTS    0x20
#define REC_VALIDX    0x30
#define CTX_VALRECS   0x10
#define VALREC_SIZE   0x14
/* client Player */
#define P_MOUNT       0x1d8
#define P_PM          0x2c8   /* PlayerMovement (re-m1m2.md 3) */
#define P_CAM         0x2d0
#define P_BODY        0x2d8
#define P_COMBAT      0x2e8
#define P_PHOTO       0x318   /* +0x30 int, 3 = off */
#define P_LIFE        0x4c4   /* 1 not in world, 2 alive, 3 dying/dead */
#define P_SPECT       0x4c8
/* PlayerBody / animation manager / camera */
#define PM_OWNER      0x218   /* the Player* (vf_11 reads its +0x2d8 PlayerBody) */
#define PM_XPOS       0x428   /* float[3] one-shot extra translation, Talion's frame (x right, y up, z forward), cm */
#define PM_XROT       0x434   /* float[4] its rotation x y z w (w at +0x440) */
#define PM_PENDING    0x444   /* byte: consumed and reset by the next vf_11 (re-m1m2.md 3.2; the game never sets it) */
#define BODY_OBJ      0x1b8
#define BODY_MGR      0x3b8
#define BODY_TAGS     0x3c0   /* StateTagMgr (embedded): +0x158 current set index, sets of 0xa8 at +0x8, count +0xa0 */
#define MGR_CHAR      0x560
#define CAM_OBJ       0x338
#define CAM_SCRIPTED  0x390
#define CAM_POS       0x44c
#define CAM_ROT       0x458
#define CAM_EXTRA     0x484
/* PlayerCombat */
#define PC_BANDS      0x2f0   /* three vectors (0x18 apart) of 0x38-byte candidate entries, Combat* at +0x30 */
#define PCB_SIZE      0x38
#define PC_SERVER     0xa08   /* the player's server Combat* (PlayerCombat::vf_03) */
/* Combat */
#define C_OWNER       0x70    /* CCharacter* (+0x178 bit 0 = dead) */
#define C_AI          0x80
#define C_PLAYER      0x88
#define C_WIN_HIT     0xe3
#define C_WIN_PARRY   0xe9
#define C_HITDIR      0x15c
#define C_HITONCE     0x27a
#define C_DEAD        0x288
#define C_HOBJ        0x4a0
#define C_TARGET      0x4c0
#define C_CHARREC     0x4c8
#define C_ATTACK      0x4d0
#define CH_FLAGS      0x178
#define OBJ_POS       0x08
#define OBJ_ROT       0x14
/* database category / record */
#define CAT_COUNT     0x28
#define CAT_RECS      0x38
#define DBREC_SIZE    0x30
/* P7: Combat (re-combat-vr.md 1.2, 3.1), characters, object timers (2.2), projectiles (re-combat-items.md 1.2),
 * inventory (3.1/3.2), nav markers (re-hud.md 3.2), prompts (4.3), the UI scene (2.1) */
#define C_WS          0xb0    /* window state; the key times (ushort ms from the animation start) at Combat+0xc8.. */
#define C_KT          0xc8    /* EAL BCOL APPLYDAMAGE STARTFACING STOPFACING BEFACING BPW EPW SHOOT STIMULUS */
#define C_STIMID      0x168   /* the id of this Combat's persistent stimulus (0 = none) */
#define C_ANIM        0x17c   /* the current attack's animation index (-1 = none) */
#define C_SYNC1       0x188   /* a sync action (with +0x190) */
#define C_SYNC2       0x190
#define C_VARIATION   0x55c   /* MultiHitRecords index + 1 of the current attack */
#define CH_COMBAT     0x6d0   /* CCharacter -> its Combat* */
#define CH_INV        0x910   /* CCharacter -> its Inventory* (Character_GetInventory) */
#define OBJ_TIMER     0x68    /* HOBJECT -> its IEngineTimer timer (HTIMER) */
#define OBJ_WORLD     0xe0    /* HOBJECT -> world; its +0xc088 = the default object timer */
#define WORLD_DEFTIMER 0xc088
#define TM_REF        0x08
#define TM_FRAME      0x0c    /* u32 frame ms; +0x10 float frame s, +0x14 u32 total ms, +0x18 double total s */
#define TM_TOTAL      0x14
#define TM_TOTALS     0x18
#define TM_NUM        0x20
#define TM_DEN        0x24
#define TM_SCALE      0x28
#define TM_PAUSED     0x34
#define TM_CARRY      0x78
#define SRV_SIMTIMER  0x380   /* *(*0x466aee0 + 0x380) = the simulation timer */
#define PJ_SHOOTER    0x58
#define PJ_TARGET     0x140
#define PJ_RANGED     0x188
#define PJ_ATTACK     0x190
#define PJ_POS        0x1b8
#define PJ_ROT        0x1c4   /* quaternion x y z w: the flight direction = R(q) +Z */
#define PJ_SPEED      0x210
#define PJ_TRAVEL     0x21c
#define PJ_RANGE      0x220
#define PJ_GRAVW      0x224
#define PJ_DONE       0x228   /* vf_13 removes the projectile at its next call */
#define PJ_IMMEDIATE  0x22b
#define PJ_HOMING     0x22c
#define PJ_GRAVITY    0x22d
#define PJ_SPLINE     0x22e
#define PJ_CRIT       0x22f
#define PJ_SIZE       0x230
#define INV_SLOT0     0x30    /* 8 weapon slots of 0x38 bytes, InventoryItem* at +0 */
#define INV_SLOTSZ    0x38
#define INV_ACTIVE    0x280
#define IT_ENTRIES    0x28    /* vector of 0x48-byte model entries */
#define IT_ESIZE      0x48
#define IE_OBJ        0x20
#define IE_ACTNODE    0x30    /* node hash while the item is active */
#define IE_INACTNODE  0x34    /* node hash while inactive */
#define IE_SOCKET     0x43
#define IT_REC        0x78
#define IT_FX         0x88    /* LoadedFX handles (vector) */
#define IT_ACTIVE     0xa2
#define IT_SHOWN      0xa4
#define IT_VISEN      0xa5
#define NAV_NODE      0x80    /* CNavMarkerInstance: list node (+0x88 next) */
#define NM_SHOWN1     0x45
#define NM_SHOWN2     0x6d
#define NM_TGT2       0x210
#define NM_TGT        0x230
#define NM_PARENT     0x250
#define NM_REC        0x298   /* the NavMarkerData record */
#define NM_NODE       0x2d0
#define NM_ACTIVE     0x2e0
#define NM_ENABLED    0x2e1
#define NM_PADICON    0x2ea
#define P_UI          0x2e0   /* Player -> CPlayerUI; its +0xb0 + 0x16e0 = the HUD (FUN_00ac29d0) */
#define HUD_PROMPTS   0x60    /* Standard, Quicktime, Keyboard CUIHUDPrompts* at +0x60/+0x68/+0x70 */
#define PR_DIAMOND    0x1d8
#define IM_UISCENE    0x2ec0  /* CUISceneMgr, embedded in CInterfaceMgr */
#define IM_FADE       0x2ad8  /* the screen fade (re-hud.md 1.4, U10) */
#define SH_FXMGR      0x80c78 /* CGameClientShell -> the ClientFX manager (create at +0x30) */
#define SH_RT3D       0x80e00 /* CGameClientShell -> rt3D (RenderTarget: +0x8 flags, +0xc/+0x10 size) */
#define P_FSM         0x7f0   /* Player: the FSM state id byte (+0x7f1 the previous; re-traversal.md 1.2) */
#define PM_PHYS       0x50    /* PlayerMovement -> CPhysicsCharacter (+0x58 gravity float, +0x60 gravity on) */
/* P10: the movement solver and the player record (design-locomotion.md 2.1-2.4, 2.7; asm 0xab5e70, 0x20c6ff0, 0xab8700) */
#define PM_ATK        0x38    /* PlayerMovement: != 0 with a current attack = the animation locks are skipped (vf_11) */
#define PM_VEL        0xe0    /* float[3] the solver's velocity, cm/s, world (FSM 8 stores it at its start) */
#define PM_VELMODE    0x1a2   /* u8: 1 root-motion mode, 0 momentum (FUN_020c6ff0; to 0 it zeroes +0x104..+0x10c) */
#define PM_ATTACHED   0x1a6   /* u8: an attach-to-object move (FUN_020cac10) */
#define PM_BASESPEED  0x32c   /* float: BaseSpeedMultiplier as FUN_00ab8700 caches it (no reader found) */
#define PM_REC        0x448   /* the player record of Client.PlayerMovement (FUN_00ab8700) */
#define PM_YAWTIME    0x450   /* float s: PlayerYawInterpolateTime (default 0.4) */
#define PM_FALLMIN    0x454   /* float cm: FallMinHeight (default 100) */
#define PCH_GRAVITY   0x58    /* CPhysicsCharacter: float gravity (P7: -20 in the world); +0x60 u8 gravity on */
#define PCH_GRAVON    0x60
#define BODY_PLAYER   0x08    /* PlayerBody -> its Player (FUN_00a513a0: +8 -> +0x2e8 PlayerCombat) */
#define MGR_LOCK_T    0x62a   /* CAnimationMgr: the animation owns the translation (vf_30: vf_11 zeroes it) */
#define MGR_LOCK_R    0x629   /* ... the rotation (vf_31) */
#define MGR_GRAPHDEF  0x3258  /* CAnimationMgr: the graph definition FUN_0208dd90 searches (+0x38 variables, +0x28 count) */
#define SYNC_REC      0x10    /* CCombatSyncAction (Combat+0x188): the active record (0 = none), +0x1f8 the target Combat */
#define SYNC_TARGET   0x1f8
/* P8: a character's model (re-animation.md 4, re-arms-ik.md 2.2), the hidden-piece mask chain (re-resolution.md 2.1),
 * the AI's scripted combat target (FUN_0296f870, read by CAITargetSelectCombatScripted::vf_07), the character's min
 * health (FUN_0269da10, read by CCharacter::vf_37), the UI mouse path (re-ui-pointer.md 1), mounts (re-traversal.md 3) */
#define SM_ATTACH     0x90    /* CSimpleModel -> CSimpleModelAttachNode* */
#define SM_TRACKER    0x178   /* CSimpleModel -> CSimpleModelTrackerNode* (the pose buffer's owner) */
#define SM_DIRTY      0x68
#define SM_MASK       0x6c    /* u32[2]: bit n = piece n hidden (game thread) */
#define SM_REND       0xb0    /* -> CSimpleModelRenderable: +0x40 dirty, +0x44 mask copy, +0x50 the render handle */
#define REND_DIRTY    0x40
#define REND_MASK     0x44
#define REND_HANDLE   0x50
#define HANDLE_NODE   0x10    /* the handle embeds the render-thread CRenderNodeMeshSkeletal here */
#define RNODE_SKIP    0x40    /* node byte: nonzero = skipped by every draw-list builder (re-resolution.md 2.2) */
#define RNODE_MASK    0x130
#define AT_ENTRIES    0x48    /* attach node: vector of 0x68-byte entries */
#define AT_ESIZE      0x68
#define AE_CHILD      0x18    /* entry: the child HOBJECT; +0x20 node index, +0x24 hash, +0x28 the effective offset
                               * (pos[3] quat[4]), +0x44 the user offset, +0x60 flag, +0x61 resolved */
#define AE_INDEX      0x20
#define AE_HASH       0x24
#define AE_OFFSET     0x28
#define AE_USER       0x44
#define TRK_POSE      0x58    /* tracker -> pose buffer: +0x40 node transforms (0x20 each: pos[3], pad, quat[4]), +0x48 count */
#define PB_XF         0x40
#define PB_COUNT      0x48
#define MGR_CHAINS    0x34d8  /* CAnimationMgr: 4 IK chain slots of 0x118 bytes */
#define CHAIN_SIZE    0x118
#define NPC_AI        0xc78   /* NPC -> its AI: +0xb50..+0xb60 the scripted target's WBObjRef (+0xb60 = the HOBJECT) */
#define AI_SCRIPTTGT  0xb60
#define AI_STICKY     0xb68   /* 0 = cleared by the AI itself (FUN_0296c0f1's path), 1 = kept */
#define GB_HOBJ       0x08    /* GameBase (CCharacter, NPC) -> its HOBJECT */
#define CH_HEALTH     0x164   /* CCharacter: float health */
#define CH_MINHEALTH  0x9b4   /* CCharacter: float min health (SetMinHealth) */
#define P_PLINDEX     0x431   /* Player: the local player index the mouse path passes */
#define P_MOUNTINFO   0x1b8   /* Player: MountInfo (embedded): +0x48 RawMoveMag .. +0x68 camera quat */
#define P_MOUNTSTATE  0x230   /* Player: the pending mount state name (0x80 chars) */
#define IM_MOUSE      0x3210  /* CInterfaceMgr: int x, y (pixels), float u, v (/ iw, ih) */
#define IM_GAMEUI     0x2708  /* +8 -> +0x50 = m_GameUI's CUIView */
#define IM_MSGBOX     0x2a50  /* m_MessageBoxUI: +8 -> +0x50 = its CUIView (FUN_00cb52e0, as for m_GameUI) */
#define VIEW_MOVIE    0x18    /* CUIView -> GFx MovieImpl: +0x52d0 bit 18 focus, +0x23d8 cursor count */
#define VIEW_OFS      0x28    /* CUIView: int x, y offset subtracted by the forwarders */
#define MV_FOCUS      0x52d0
#define MV_CURSORS    0x23d8
#define MSM_MAINMENU  0x158   /* the menu SM: menu state 0xf's manager (CUIMainMenuMenuManager) */
#define MSM_MENUC     0x1788  /* menu state 0xc's manager */
#define PUISM_MGR     0x10    /* the player-UI SM (*(CPlayerUI+0xb0)): state 5's manager */
#define MGR_LAYERS    0x198   /* CUIMenuManagerBase: layers[8]; +0x1e8 count, +0x1ec the override layer is on */
#define MGR_NLAYERS   0x1e8
#define MGR_OVERRIDE  0x1ec
#define LAYER_FOCUS   0x64c   /* CUIMenuLayer: the focused index; +0x650..+0x658 the components (CUIMenuComponent*) */
#define LAYER_COMPS   0x650
#define COMP_CLIP     0x28    /* component -> its CUIMovieClip */

/* ------------------------------------------------------------------------------------------------------------ */
/* Register frame and asm wrappers                                                                               */

struct regs {
	uint64_t rax, rcx, rdx, rsi, rdi, r8, r9, r10, r11, ra; /* ra = the game's return address */
	float xmm[16][4];
};
_Static_assert(offsetof(struct regs, ra) == 0x48, "regs.ra");
_Static_assert(offsetof(struct regs, xmm) == 0x50, "regs.xmm");
_Static_assert(sizeof(struct regs) == 0x150, "regs size");

#define WSAVEX(i) "  movdqu %xmm" #i ", 0x50+" #i "*16(%rsp)\n"
#define WLOADX(i) "  movdqu 0x50+" #i "*16(%rsp), %xmm" #i "\n"
#define WPROLOGUE(name)                                                                                        \
	".text\n.p2align 4\n.globl " name "\n.hidden " name "\n.type " name ",@function\n" name ":\n"          \
	"  push %rbp\n  mov %rsp, %rbp\n  sub $0x160, %rsp\n  and $-16, %rsp\n"                                  \
	"  mov %rax, 0x00(%rsp)\n  mov %rcx, 0x08(%rsp)\n  mov %rdx, 0x10(%rsp)\n  mov %rsi, 0x18(%rsp)\n"        \
	"  mov %rdi, 0x20(%rsp)\n  mov %r8, 0x28(%rsp)\n  mov %r9, 0x30(%rsp)\n  mov %r10, 0x38(%rsp)\n"         \
	"  mov %r11, 0x40(%rsp)\n  mov 8(%rbp), %rax\n  mov %rax, 0x48(%rsp)\n"                                  \
	WSAVEX(0) WSAVEX(1) WSAVEX(2) WSAVEX(3) WSAVEX(4) WSAVEX(5) WSAVEX(6) WSAVEX(7)                       \
	WSAVEX(8) WSAVEX(9) WSAVEX(10) WSAVEX(11) WSAVEX(12) WSAVEX(13) WSAVEX(14) WSAVEX(15)
#define WRESTORE                                                                                               \
	WLOADX(0) WLOADX(1) WLOADX(2) WLOADX(3) WLOADX(4) WLOADX(5) WLOADX(6) WLOADX(7)                       \
	WLOADX(8) WLOADX(9) WLOADX(10) WLOADX(11) WLOADX(12) WLOADX(13) WLOADX(14) WLOADX(15)                 \
	"  mov 0x08(%rsp), %rcx\n  mov 0x10(%rsp), %rdx\n  mov 0x18(%rsp), %rsi\n  mov 0x20(%rsp), %rdi\n"        \
	"  mov 0x28(%rsp), %r8\n  mov 0x30(%rsp), %r9\n  mov 0x38(%rsp), %r10\n  mov 0x40(%rsp), %r11\n"         \
	"  mov 0x00(%rsp), %rax\n"
/* post hook: the C function calls the original itself and leaves its return value in r->rax */
#define CALLWRAP(name, cfn) \
	__asm__(WPROLOGUE(name) "  mov %rsp, %rdi\n  cld\n  call " cfn "\n" WRESTORE "  leave\n  ret\n.size " name ", .-" name "\n")
/* pre hook (log only): everything restored, then a jump to the original with the caller's stack as it was */
#define PREWRAP(name, cfn, off) \
	__asm__(WPROLOGUE(name) "  mov %rsp, %rdi\n  cld\n  call " cfn "\n" WRESTORE "  leave\n"                  \
		"  jmp *somvr_probe_orig+" off "(%rip)\n.size " name ", .-" name "\n")

__attribute__((used)) uintptr_t somvr_probe_orig[HK_N]; /* PREWRAP targets (hidden: -fvisibility=hidden) */

#define DECL_W(x) void somvr_pw_##x(void); void somvr_pc_##x(struct regs *r);
DECL_W(anim_a) DECL_W(anim_b) DECL_W(campost_a) DECL_W(campost_b) DECL_W(atk_obj) DECL_W(atk_pos) DECL_W(atk_nopos)
DECL_W(dealhit) DECL_W(applydmg) DECL_W(counter) DECL_W(hitev) DECL_W(death)
CALLWRAP("somvr_pw_anim_a", "somvr_pc_anim_a");
CALLWRAP("somvr_pw_anim_b", "somvr_pc_anim_b");
CALLWRAP("somvr_pw_campost_a", "somvr_pc_campost_a");
CALLWRAP("somvr_pw_campost_b", "somvr_pc_campost_b");
/* offsets = 8 * HK_ index */
PREWRAP("somvr_pw_atk_obj", "somvr_pc_atk_obj", "88");
PREWRAP("somvr_pw_atk_pos", "somvr_pc_atk_pos", "96");
PREWRAP("somvr_pw_atk_nopos", "somvr_pc_atk_nopos", "104");
PREWRAP("somvr_pw_dealhit", "somvr_pc_dealhit", "112");
PREWRAP("somvr_pw_applydmg", "somvr_pc_applydmg", "120");
PREWRAP("somvr_pw_counter", "somvr_pc_counter", "128");
PREWRAP("somvr_pw_hitev", "somvr_pc_hitev", "136");
PREWRAP("somvr_pw_death", "somvr_pc_death", "144");
_Static_assert(HK_ATK_OBJ * 8 == 88 && HK_DEATH * 8 == 144, "PREWRAP offsets follow the HK_ enum");
/* P7: CALLWRAP for the stimulus calls (the C side calls the original with its 3 or 2 register arguments), PREWRAP for
 * the log-only sites (P_PJ_DAMAGE and the two AK PostEvent calls pass arguments on the stack: PREWRAP leaves it as
 * it was); the 19 FX init sites share one C function (it finds its site by the return address) */
DECL_W(stim_start) DECL_W(stim_key) DECL_W(stim_stop) DECL_W(regstim) DECL_W(regstim_p) DECL_W(keytimes)
DECL_W(pjdamage) DECL_W(snd) DECL_W(uiscene) void somvr_pw_snd_cb(void);
void somvr_pc_fxi(struct regs *r);
void somvr_pw_fx0(void), somvr_pw_fx1(void), somvr_pw_fx2(void), somvr_pw_fx3(void), somvr_pw_fx4(void), somvr_pw_fx5(void),
     somvr_pw_fx6(void), somvr_pw_fx7(void), somvr_pw_fx8(void), somvr_pw_fx9(void), somvr_pw_fx10(void), somvr_pw_fx11(void),
     somvr_pw_fx12(void), somvr_pw_fx13(void), somvr_pw_fx14(void), somvr_pw_fx15(void), somvr_pw_fx16(void),
     somvr_pw_fx17(void), somvr_pw_fx18(void);
CALLWRAP("somvr_pw_stim_start", "somvr_pc_stim_start");
CALLWRAP("somvr_pw_stim_key", "somvr_pc_stim_key");
CALLWRAP("somvr_pw_stim_stop", "somvr_pc_stim_stop");
CALLWRAP("somvr_pw_regstim", "somvr_pc_regstim");
CALLWRAP("somvr_pw_regstim_p", "somvr_pc_regstim_p");
DECL_W(res3d)
CALLWRAP("somvr_pw_res3d", "somvr_pc_res3d");
PREWRAP("somvr_pw_keytimes", "somvr_pc_keytimes", "240");
PREWRAP("somvr_pw_pjdamage", "somvr_pc_pjdamage", "256");
PREWRAP("somvr_pw_snd", "somvr_pc_snd", "264");
PREWRAP("somvr_pw_snd_cb", "somvr_pc_snd", "272");
PREWRAP("somvr_pw_uiscene", "somvr_pc_uiscene", "280");
PREWRAP("somvr_pw_fx0", "somvr_pc_fxi", "288");
PREWRAP("somvr_pw_fx1", "somvr_pc_fxi", "296");
PREWRAP("somvr_pw_fx2", "somvr_pc_fxi", "304");
PREWRAP("somvr_pw_fx3", "somvr_pc_fxi", "312");
PREWRAP("somvr_pw_fx4", "somvr_pc_fxi", "320");
PREWRAP("somvr_pw_fx5", "somvr_pc_fxi", "328");
PREWRAP("somvr_pw_fx6", "somvr_pc_fxi", "336");
PREWRAP("somvr_pw_fx7", "somvr_pc_fxi", "344");
PREWRAP("somvr_pw_fx8", "somvr_pc_fxi", "352");
PREWRAP("somvr_pw_fx9", "somvr_pc_fxi", "360");
PREWRAP("somvr_pw_fx10", "somvr_pc_fxi", "368");
PREWRAP("somvr_pw_fx11", "somvr_pc_fxi", "376");
PREWRAP("somvr_pw_fx12", "somvr_pc_fxi", "384");
PREWRAP("somvr_pw_fx13", "somvr_pc_fxi", "392");
PREWRAP("somvr_pw_fx14", "somvr_pc_fxi", "400");
PREWRAP("somvr_pw_fx15", "somvr_pc_fxi", "408");
PREWRAP("somvr_pw_fx16", "somvr_pc_fxi", "416");
PREWRAP("somvr_pw_fx17", "somvr_pc_fxi", "424");
PREWRAP("somvr_pw_fx18", "somvr_pc_fxi", "432");
_Static_assert(HK_KEYTIMES * 8 == 240 && HK_PJ_DAMAGE * 8 == 256 && HK_SND_POST * 8 == 264 && HK_SND_POST_CB * 8 == 272 &&
	       HK_UISCENE * 8 == 280 && HK_FX0 * 8 == 288 && HK_FX_LAST * 8 == 432 && HK_RES3D == 55,
	       "P7 PREWRAP offsets follow the HK_ enum");
/* P8: CALLWRAP for the IK chain setup/update calls and the two pad-guard calls (the C side calls the original);
 * PREWRAP (log only) for the IEngineModel callback slots: AddNodeCallback takes a seventh argument on the stack */
DECL_W(iksetup_a) DECL_W(iksetup_b) DECL_W(ikchain) DECL_W(guard_hover) DECL_W(guard_click) DECL_W(addcb) DECL_W(remcb)
CALLWRAP("somvr_pw_iksetup_a", "somvr_pc_iksetup_a");
CALLWRAP("somvr_pw_iksetup_b", "somvr_pc_iksetup_b");
CALLWRAP("somvr_pw_ikchain", "somvr_pc_ikchain");
CALLWRAP("somvr_pw_guard_hover", "somvr_pc_guard_hover");
CALLWRAP("somvr_pw_guard_click", "somvr_pc_guard_click");
PREWRAP("somvr_pw_addcb", "somvr_pc_addcb", "472");
PREWRAP("somvr_pw_remcb", "somvr_pc_remcb", "480");
_Static_assert(HK_IK_SETUP_A == 56 && HK_IEM_ADDCB * 8 == 472 && HK_IEM_REMCB * 8 == 480 && HK_GRAV == 72,
	       "P8 PREWRAP offsets follow the HK_ enum");
/* P9: CALLWRAP for the roll path's four calls (the roll passes floats: the C side calls the original with xmm0/xmm1
 * and writes the result into the saved xmm0) and the free-aim camera call; PREWRAP (log only) for the shield test */
DECL_W(aib_gate) DECL_W(aib_rec) DECL_W(aib_roll) DECL_W(aib_notify) DECL_W(shield) DECL_W(bowcamxf)
CALLWRAP("somvr_pw_aib_gate", "somvr_pc_aib_gate");
CALLWRAP("somvr_pw_aib_rec", "somvr_pc_aib_rec");
CALLWRAP("somvr_pw_aib_roll", "somvr_pc_aib_roll");
CALLWRAP("somvr_pw_aib_notify", "somvr_pc_aib_notify");
CALLWRAP("somvr_pw_bowcamxf", "somvr_pc_bowcamxf");
PREWRAP("somvr_pw_shield", "somvr_pc_shield", "624");
_Static_assert(HK_AIB_GATE == 73 && HK_SHIELD * 8 == 624, "P9 PREWRAP offsets follow the HK_ enum");
/* P10: CALLWRAP for vf_11's root-delta call (FUN_00a513a0(body, &delta): the C side calls it, then reads/replaces the
 * delta record it filled; rbp/rbx (pm/xf in vf_11) and xmm3 (vf_11's zero) come back as they were) */
DECL_W(rootdelta)
CALLWRAP("somvr_pw_rootdelta", "somvr_pc_rootdelta");
_Static_assert(HK_ROOTDELTA == 80 && HK_N == 81, "P10 hooks follow the HK_ enum");

/* ------------------------------------------------------------------------------------------------------------ */
/* State                                                                                                          */

struct hook {
	const char *name;
	int installed;
	uintptr_t orig, site; /* site = the patched call/slot address (the shared FX/sound C functions find theirs) */
	_Atomic uint64_t calls;
};
static struct hook g_h[HK_N];

static char g_log_path[1024], g_cmd_path[1024], g_out_dir[1024];
static int g_fd = -1, g_active;
static pid_t g_pid;
static locale_t g_cloc;
static _Atomic int g_stop;
static int g_guard_ok = -1, g_noguard_ok; /* -1 = not checked yet */

#define LOGBUF (1u << 20)
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;       /* hooks: held for one memcpy */
static pthread_mutex_t g_flush_mu = PTHREAD_MUTEX_INITIALIZER; /* flushers only */
static char g_buf[2][LOGBUF];
static size_t g_len;
static unsigned g_cur;
static _Atomic uint64_t g_drops;

/* rate limiter per log category */
#define RL_FIRST 60
#define RL_PER_SEC 8
struct rl {
	const char *name;
	_Atomic uint32_t total, sec_count, suppressed;
	_Atomic int64_t sec;
};
enum { RL_STATE, RL_VIN, RL_ATK, RL_DEALHIT, RL_APPLY, RL_COUNTER, RL_HITEV, RL_DEATH, RL_UI, RL_VIS, RL_FP, RL_AUTOC,
       RL_POSE, RL_STIM, RL_AI, RL_KEYT, RL_PROJ, RL_FX, RL_SND, RL_UISC, RL_HSTOP, RL_PROMPT,
       /* P8 */
       RL_MOUSE, RL_IK, RL_ARM, RL_AGGRO, RL_CONOUT, RL_TRAV,
       /* P9 */
       RL_AIB, RL_AIM,
       /* P10 */
       RL_LOCO, RL_SPEED, RL_VAULT, RL_N };
static struct rl g_rl[RL_N] = { { .name = "state" }, { .name = "vin" }, { .name = "atk" }, { .name = "dealhit" },
				{ .name = "applydmg" }, { .name = "counter" }, { .name = "hitev" }, { .name = "death" },
				{ .name = "ui105" }, { .name = "vis" }, { .name = "fpcam" }, { .name = "autocounter" },
				{ .name = "pose" }, { .name = "stim" }, { .name = "ai" }, { .name = "keytimes" },
				{ .name = "proj" }, { .name = "fx" }, { .name = "sound" }, { .name = "uiscene" },
				{ .name = "hitstop" }, { .name = "prompts" }, { .name = "mouse" }, { .name = "ik" },
				{ .name = "armpose" }, { .name = "aggro" }, { .name = "conout" }, { .name = "trav" },
				{ .name = "aiblock" }, { .name = "aimray" }, { .name = "loco" }, { .name = "speed" },
				{ .name = "vault" } };

static double mono(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Logging (spike1's double buffer; the writer thread flushes)                                                    */

static void log_raw(const char *s, size_t n)
{
	pthread_mutex_lock(&g_mu);
	if (g_len + n <= LOGBUF) {
		memcpy(g_buf[g_cur & 1] + g_len, s, n);
		g_len += n;
	} else {
		atomic_fetch_add_explicit(&g_drops, 1, memory_order_relaxed);
	}
	pthread_mutex_unlock(&g_mu);
}

__attribute__((format(printf, 1, 2))) static void logf_(const char *fmt, ...)
{
	char line[2048];
	va_list ap;
	locale_t old = g_cloc ? uselocale(g_cloc) : (locale_t)0;
	va_start(ap, fmt);
	int n = vsnprintf(line, sizeof line - 1, fmt, ap);
	va_end(ap);
	if (old)
		uselocale(old);
	if (n <= 0)
		return;
	if ((size_t)n > sizeof line - 2)
		n = (int)sizeof line - 2;
	if (line[n - 1] != '\n')
		line[n++] = '\n';
	log_raw(line, (size_t)n);
}

static void flush_log(void)
{
	if (g_fd < 0)
		return;
	pthread_mutex_lock(&g_flush_mu);
	pthread_mutex_lock(&g_mu);
	unsigned c = g_cur & 1;
	size_t n = g_len;
	g_cur ^= 1u;
	g_len = 0;
	pthread_mutex_unlock(&g_mu);
	const char *p = g_buf[c & 1];
	while (n > 0) {
		ssize_t w = write(g_fd, p, n);
		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0)
			break;
		p += w;
		n -= (size_t)w;
	}
	pthread_mutex_unlock(&g_flush_mu);
}

static int rl_ok(int cat)
{
	struct rl *r = &g_rl[cat];
	uint32_t n = atomic_fetch_add_explicit(&r->total, 1, memory_order_relaxed) + 1;
	if (n <= RL_FIRST)
		return 1;
	int64_t s = (int64_t)mono();
	if (atomic_load_explicit(&r->sec, memory_order_relaxed) != s) {
		atomic_store(&r->sec, s);
		atomic_store(&r->sec_count, 0);
	}
	if (atomic_fetch_add_explicit(&r->sec_count, 1, memory_order_relaxed) < RL_PER_SEC)
		return 1;
	atomic_fetch_add_explicit(&r->suppressed, 1, memory_order_relaxed);
	return 0;
}

/* the first call of a hook: its thread and the time (re-input.md 9.3 / re-animation.md 9.2 want the thread map) */
static void first_call(int hk, uint64_t n)
{
	if (n != 1)
		return;
	char thr[32] = { 0 };
	prctl(PR_GET_NAME, thr, 0, 0, 0);
	logf_("FIRST %s tid=%d thr=%s t=%.6f", g_h[hk].name, (int)syscall(SYS_gettid), thr, mono());
}

static int my_tid(void)
{
	static __thread int tid;
	if (!tid)
		tid = (int)syscall(SYS_gettid);
	return tid;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Safe reads of game memory: a bad pointer gives an error, never a fault                                         */

static int rd(uintptr_t a, void *out, size_t n)
{
	if (a < 0x10000 || a > 0x7fffffffe000ull || n == 0 || n > 0x100000)
		return -1;
	struct iovec l = { out, n }, r = { (void *)a, n };
	return process_vm_readv(g_pid, &l, 1, &r, 1, 0) == (ssize_t)n ? 0 : -1;
}
static uint64_t rq(uintptr_t a) { uint64_t v = 0; return rd(a, &v, 8) ? 0 : v; }
static uint32_t r32(uintptr_t a) { uint32_t v = 0; return rd(a, &v, 4) ? 0 : v; }
static uint8_t rb(uintptr_t a) { uint8_t v = 0; return rd(a, &v, 1) ? 0 : v; }
static float rf(uintptr_t a) { float v = 0; return rd(a, &v, 4) ? 0.0f : v; }
static int rvec(uintptr_t a, float *v, int n) { if (rd(a, v, (size_t)n * 4)) { memset(v, 0, (size_t)n * 4); return -1; } return 0; }

/* NUL-terminated string at a (read in 16-byte steps, so a string ending near an unmapped page still reads) */
static const char *rstr(uintptr_t a, char *out, size_t cap)
{
	size_t o = 0;
	out[0] = 0;
	if (!a)
		return out;
	while (o + 1 < cap) {
		char ch[16];
		size_t step = 16 - ((a + o) & 15);
		if (step > cap - 1 - o)
			step = cap - 1 - o;
		if (rd(a + o, ch, step)) {
			if (rd(a + o, ch, 1))
				break;
			step = 1;
		}
		for (size_t i = 0; i < step; i++) {
			out[o] = ch[i];
			if (!ch[i])
				return out;
			o++;
		}
	}
	out[o] = 0;
	return out;
}

/* DBRecord_GetName (FUN_025aa530): flags byte +1 bit 1, name pointer +0x20 */
static const char *rec_name(uintptr_t rec, char *out, size_t cap)
{
	out[0] = 0;
	if (!rec || !(rb(rec + 1) & 2))
		return out;
	return rstr(rq(rec + 0x20), out, cap);
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Patching (spike1's patcher; K_ENTRY specs are checked only)                                                    */

static int hexval(int c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

static int parse_hex(const char *s, unsigned char *out, int cap)
{
	int n = 0;
	while (*s) {
		if (*s == ' ') {
			s++;
			continue;
		}
		int hi = hexval(s[0]), lo = hi < 0 ? -1 : hexval(s[1]);
		if (n >= cap || lo < 0)
			return -1;
		out[n++] = (unsigned char)(hi * 16 + lo);
		s += 2;
	}
	return n;
}

static const char *parse_ulx(const char *s, unsigned long *v)
{
	unsigned long x = 0;
	int d, any = 0;
	while ((d = hexval(*s)) >= 0) {
		x = x * 16 + (unsigned long)d;
		s++;
		any = 1;
	}
	*v = x;
	return any ? s : NULL;
}

static void hex_of(const unsigned char *p, int n, char *out, size_t cap)
{
	size_t o = 0;
	for (int i = 0; i < n && o + 4 < cap; i++)
		o += (size_t)snprintf(out + o, cap - o, i ? " %02x" : "%02x", p[i]);
	out[o] = 0;
}

static int page_prot(uintptr_t addr)
{
	FILE *f = fopen("/proc/self/maps", "re");
	if (!f)
		return -1;
	char line[512];
	int prot = -1;
	while (fgets(line, sizeof line, f)) {
		unsigned long lo, hi;
		const char *q = parse_ulx(line, &lo), *perm;
		if (!q || *q != '-' || !(q = parse_ulx(q + 1, &hi)) || *q != ' ' || strlen(q) < 5)
			continue;
		perm = q + 1;
		if (addr >= lo && addr < hi) {
			prot = (perm[0] == 'r' ? PROT_READ : 0) | (perm[1] == 'w' ? PROT_WRITE : 0) | (perm[2] == 'x' ? PROT_EXEC : 0);
			break;
		}
	}
	fclose(f);
	return prot;
}

static int range_readable(uintptr_t a, size_t n)
{
	for (uintptr_t p = a & ~(uintptr_t)4095; p < a + n; p += 4096) {
		int pr = page_prot(p);
		if (pr < 0 || !(pr & PROT_READ))
			return 0;
	}
	return 1;
}

static int write_code(uintptr_t dst, const void *src, size_t n, char *err, size_t errcap)
{
	uintptr_t p0 = dst & ~(uintptr_t)4095, p1 = (dst + n - 1) & ~(uintptr_t)4095;
	int prot[2];
	prot[0] = page_prot(p0);
	prot[1] = p1 != p0 ? page_prot(p1) : prot[0];
	if (prot[0] < 0 || prot[1] < 0) {
		snprintf(err, errcap, "page not mapped");
		return -1;
	}
	for (int i = 0; i < (p1 != p0 ? 2 : 1); i++)
		if (mprotect((void *)(i ? p1 : p0), 4096, prot[i] | PROT_READ | PROT_WRITE) != 0) {
			snprintf(err, errcap, "mprotect rw: %s", strerror(errno));
			if (i)
				mprotect((void *)p0, 4096, prot[0]);
			return -1;
		}
	memcpy((void *)dst, src, n);
	__builtin___clear_cache((char *)dst, (char *)dst + n);
	int rc = 0;
	for (int i = 0; i < (p1 != p0 ? 2 : 1); i++)
		if (mprotect((void *)(i ? p1 : p0), 4096, prot[i]) != 0) {
			snprintf(err, errcap, "mprotect restore: %s", strerror(errno));
			rc = -1;
		}
	return rc;
}

/* one RX page within +-2 GB of the low code (spike1 takes the first free one; this library the next) */
static unsigned char *g_stub;
static int g_stub_used;

static int stub_reach(uintptr_t stub, uintptr_t site)
{
	int64_t rel = (int64_t)stub - (int64_t)(site + 5);
	return rel >= INT32_MIN && rel <= INT32_MAX;
}

static unsigned char *stub_page(uintptr_t near_lo, uintptr_t near_hi)
{
	if (g_stub)
		return g_stub;
	static const uintptr_t far_tries[] = { 0x40000000, 0x50000000, 0x60000000, 0x70000000 };
	for (int i = 0; i < 64 + 4; i++) {
		uintptr_t want = i < 64 ? (uintptr_t)0x3f0000 - (uintptr_t)i * 0x10000 : far_tries[i - 64];
		if (want < 0x10000)
			continue;
		void *p = mmap((void *)want, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
		if (p == MAP_FAILED)
			continue;
		if (stub_reach((uintptr_t)p, near_lo) && stub_reach((uintptr_t)p, near_hi)) {
			memset(p, 0xcc, 4096);
			return g_stub = p;
		}
		munmap(p, 4096);
	}
	return NULL;
}

static int check_site(const struct site_spec *s, char *why, size_t cap)
{
	unsigned char exp[128], pro[32];
	int n = parse_hex(s->ctx_hex, exp, sizeof exp), np = parse_hex(s->prologue_hex, pro, sizeof pro);
	size_t off = s->addr - s->ctx, need = s->kind == K_CALL ? 5 : s->kind == K_VSLOT ? 8 : 1;
	char live[400];
	if (n <= 0 || np <= 0 || s->addr < s->ctx || off + need > (size_t)n) {
		snprintf(why, cap, "bad site spec");
		return -1;
	}
	if (!range_readable(s->ctx, (size_t)n) || !range_readable(s->target, (size_t)np)) {
		snprintf(why, cap, "site or target not mapped readable");
		return -1;
	}
	if (memcmp((const void *)s->ctx, exp, (size_t)n) != 0) {
		hex_of((const unsigned char *)s->ctx, n, live, sizeof live);
		snprintf(why, cap, "bytes at %#lx differ: live %s", (unsigned long)s->ctx, live);
		return -1;
	}
	if (s->kind == K_CALL) {
		const unsigned char *p = (const unsigned char *)s->addr;
		int32_t rel;
		memcpy(&rel, p + 1, 4);
		uintptr_t tgt = s->addr + 5 + (intptr_t)rel;
		if (p[0] != 0xe8 || tgt != s->target) {
			snprintf(why, cap, "not a call to %#lx (op %02x target %#lx)", (unsigned long)s->target, p[0], (unsigned long)tgt);
			return -1;
		}
	} else if (s->kind == K_VSLOT) {
		uintptr_t v;
		memcpy(&v, (const void *)s->addr, 8);
		if (v != s->target || (s->addr & 7)) {
			snprintf(why, cap, "slot holds %#lx, want %#lx", (unsigned long)v, (unsigned long)s->target);
			return -1;
		}
	} else if (s->addr != s->target) {
		snprintf(why, cap, "entry spec at %#lx names target %#lx", (unsigned long)s->addr, (unsigned long)s->target);
		return -1;
	}
	if (memcmp((const void *)s->target, pro, (size_t)np) != 0) {
		hex_of((const unsigned char *)s->target, np, live, sizeof live);
		snprintf(why, cap, "target %#lx prologue differs: live %s", (unsigned long)s->target, live);
		return -1;
	}
	hex_of((const unsigned char *)s->addr, s->kind == K_CALL ? 5 : 8, live, sizeof live);
	snprintf(why, cap, "%s", live);
	return 0;
}

static int skip_listed(const char *name)
{
	const char *sk = getenv("SOMVR_PROBE_SKIP");
	if (!sk || !*sk)
		return 0;
	size_t ln = strlen(name);
	for (const char *p = sk; *p;) {
		size_t n = strcspn(p, ", ");
		if (n == ln && !strncmp(p, name, n))
			return 1;
		p += n;
		while (*p == ',' || *p == ' ')
			p++;
	}
	return 0;
}

/* install the hooks: repl[i] = the wrapper (K_CALL, through the stub page) or the C function (K_VSLOT) */
static void install_hooks(const struct site_spec *sites, void *const *repl)
{
	int ok[HK_N], slot[HK_N];
	char why[HK_N][512];
	uintptr_t lo = UINTPTR_MAX, hi = 0;
	for (int i = 0; i < HK_N; i++) {
		g_h[i].name = sites[i].name;
		g_h[i].site = sites[i].addr;
		slot[i] = -1;
		if (skip_listed(sites[i].name)) {
			ok[i] = 0;
			snprintf(why[i], sizeof why[i], "skipped (SOMVR_PROBE_SKIP)");
			continue;
		}
		ok[i] = check_site(&sites[i], why[i], sizeof why[i]) == 0;
		if (ok[i] && sites[i].kind == K_CALL) {
			lo = sites[i].addr < lo ? sites[i].addr : lo;
			hi = sites[i].addr > hi ? sites[i].addr : hi;
		}
	}
	if (hi) {
		unsigned char *pg = stub_page(lo, hi);
		for (int i = 0; i < HK_N; i++) {
			if (!ok[i] || sites[i].kind != K_CALL)
				continue;
			if (!pg || g_stub_used >= 256) {
				ok[i] = 0;
				snprintf(why[i], sizeof why[i], "no stub page within +-2 GB");
				continue;
			}
			unsigned char *st = pg + 16 * g_stub_used;
			uintptr_t tgt = (uintptr_t)repl[i];
			static const unsigned char jmp[6] = { 0xff, 0x25, 0, 0, 0, 0 };
			memcpy(st, jmp, 6);
			memcpy(st + 6, &tgt, 8);
			slot[i] = g_stub_used++;
		}
		if (pg && mprotect(pg, 4096, PROT_READ | PROT_EXEC) != 0)
			for (int i = 0; i < HK_N; i++)
				if (slot[i] >= 0) {
					ok[i] = 0;
					snprintf(why[i], sizeof why[i], "stub mprotect rx: %s", strerror(errno));
				}
	}
	for (int i = 0; i < HK_N; i++) {
		const struct site_spec *s = &sites[i];
		char err[200] = "";
		int rc = -1;
		if (ok[i]) {
			g_h[i].orig = s->target; /* before the patch: the wrapper may run at once on another thread */
			somvr_probe_orig[i] = s->target;
			if (s->kind == K_CALL) {
				uintptr_t st = (uintptr_t)(g_stub + 16 * slot[i]);
				int32_t rel = (int32_t)((int64_t)st - (int64_t)(s->addr + 5));
				rc = write_code(s->addr + 1, &rel, 4, err, sizeof err);
			} else if (s->kind == K_VSLOT) {
				uintptr_t w = (uintptr_t)repl[i];
				rc = write_code(s->addr, &w, 8, err, sizeof err);
			}
		}
		if (ok[i] && rc == 0) {
			g_h[i].installed = 1;
			logf_("HOOK %s ok %s %#lx orig=[%s] target=%#lx via=%#lx", s->name, s->kind == K_CALL ? "call" : "vslot",
			      (unsigned long)s->addr, why[i], (unsigned long)s->target,
			      s->kind == K_CALL ? (unsigned long)(g_stub + 16 * slot[i]) : (unsigned long)repl[i]);
		} else {
			logf_("HOOK %s SKIPPED %#lx: %s%s", s->name, (unsigned long)s->addr, ok[i] ? "write failed: " : "",
			      ok[i] ? err : why[i]);
		}
	}
}

static void check_fns(const struct site_spec *fs)
{
	char why[512];
	int bad = 0;
	for (int i = 0; i < FN_N; i++) {
		g_fn_ok[i] = check_site(&fs[i], why, sizeof why) == 0 && fs[i].addr == G.fn[i];
		if (!g_fn_ok[i]) {
			bad++;
			logf_("FUNC %s UNAVAILABLE %#lx: %s", fs[i].name, (unsigned long)fs[i].addr, why);
		}
	}
	logf_("FUNC %d of %d called functions checked ok", FN_N - bad, FN_N);
}

/* the console sites: exec re-checks G22_RUNSTRING (and the live engine's vtable) before every call */
static void check_ro(const struct site_spec *ro)
{
	char why[512];
	g_ro = ro;
	for (int i = 0; i < RO_N; i++) {
		g_ro_ok[i] = check_site(&ro[i], why, sizeof why) == 0;
		logf_("CONSOLE %s %s %#lx: %s", ro[i].name, g_ro_ok[i] ? "ok" : "UNAVAILABLE", (unsigned long)ro[i].addr, why);
	}
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Game function types                                                                                            */

typedef struct { float x, y, z; } ltvec; /* by value: x,y in XMM0, z in XMM1 (SysV) */
typedef void (*cmd_fn)(uintptr_t pi, int cmd, int input);
typedef uint64_t (*active_fn)(uintptr_t pi, unsigned cmd);
typedef void (*inmode_fn)(uintptr_t im, uintptr_t entry, int pad);
typedef void (*dealhit_fn)(ltvec pos, uintptr_t attacker, uintptr_t target, uintptr_t attack, int kill);
typedef uintptr_t (*byname_fn)(const char *name);
typedef uintptr_t (*modelof_fn)(uintptr_t obj);
typedef uint64_t (*getnode_fn)(uintptr_t m, uintptr_t obj, const char *name, uint32_t *node);
typedef uint64_t (*nodexf_fn)(uintptr_t m, uintptr_t obj, uint32_t node, float *xf, int world);
typedef uint64_t (*hashindex_fn)(uintptr_t m, uintptr_t obj, uint32_t node); /* slot 17: hash -> skeleton index */
typedef uint64_t (*numpieces_fn)(uintptr_t m, uintptr_t obj, uint32_t *count);
typedef getnode_fn getpiece_fn; /* GetPiece / GetSocket: (m, obj, name, uint32_t *out), 0 = ok */
typedef uint64_t (*piecename_fn)(uintptr_t m, uintptr_t obj, uint32_t idx, char *buf, int len);
typedef uint64_t (*hidepiece_fn)(uintptr_t m, uintptr_t obj, uint32_t piece, int hidden);
typedef uint64_t (*cover_fn)(uintptr_t im);
typedef void (*setcam_fn)(uintptr_t body, const float *xf);
typedef void (*camreset_fn)(uintptr_t cam);
typedef uintptr_t (*localplayer_fn)(uintptr_t shell);
#define FN(i, T) ((T)G.fn[i])

/* ------------------------------------------------------------------------------------------------------------ */
/* Context: the objects of this frame (game thread)                                                              */

struct ctx {
	uintptr_t shell, im, entry, pi, player, cam, body, pc, scombat, hchar, bodyobj, profile;
	int ifs, menus, paused, life, cur;
};

static int msm_state(uintptr_t sm)
{
	if (!sm)
		return -9;
	int s = (int)r32(sm + 0xc);
	return s == -1 ? (int)r32(sm + 8) : s;
}

static int pi_ok(uintptr_t pi)
{
	uintptr_t b = rq(pi + PI_ENTRIES), e = rq(pi + PI_ENTRIES + 8);
	return pi && b && e > b && (e - b) % PIE_SIZE == 0 && (e - b) / PIE_SIZE >= NCMD && (e - b) / PIE_SIZE < 4096;
}

static uintptr_t pi_rec(uintptr_t pi, int cmd)
{
	if (cmd < 0 || cmd >= NCMD)
		return 0;
	return rq(rq(pi + PI_ENTRIES) + (uintptr_t)cmd * PIE_SIZE);
}

static void ctx_get(struct ctx *c)
{
	memset(c, 0, sizeof *c);
	c->shell = rq(G.shell_pp);
	if (!c->shell)
		return;
	c->im = rq(c->shell + SH_IM);
	c->ifs = c->im ? msm_state(rq(c->im)) : -9;
	c->menus = c->im ? msm_state(rq(c->im + 0x10)) : -9;
	uintptr_t pz = rq(rq(G.pause_pp) + 8);
	c->paused = pz ? rb(pz + 0x34) : 0;
	c->cur = (int)r32(c->shell + SH_LOCALS);
	uintptr_t b = rq(c->shell + SH_LOCALS + 8), e = rq(c->shell + SH_LOCALS + 0x10);
	if (c->cur >= 0 && c->cur != 6 && b && e > b && (uintptr_t)c->cur < (e - b) / 8)
		c->entry = rq(b + 8 * (uintptr_t)c->cur);
	if (c->entry) {
		c->pi = pi_ok(c->entry + EN_PI) ? c->entry + EN_PI : 0;
		c->profile = rq(c->entry + EN_PROFILE);
	}
	if (g_fn_ok[FN_LOCALPLAYER] && c->ifs >= 1 && c->ifs <= 4) /* the shell is complete once its interface state runs */
		c->player = FN(FN_LOCALPLAYER, localplayer_fn)(c->shell);
	if (c->player) {
		c->cam = rq(c->player + P_CAM);
		c->body = rq(c->player + P_BODY);
		c->pc = rq(c->player + P_COMBAT);
		c->life = rb(c->player + P_LIFE);
		if (c->pc)
			c->scombat = rq(c->pc + PC_SERVER);
		if (c->body) {
			c->hchar = rq(rq(c->body + BODY_MGR) + MGR_CHAR);
			c->bodyobj = rq(c->body + BODY_OBJ);
		}
		if (c->scombat && rq(c->scombat) != G.vt_combat)
			c->scombat = 0;
	}
}

static uintptr_t char_obj(const struct ctx *c) { return c->hchar ? c->hchar : c->bodyobj; }
static uintptr_t model_iface(void) { return rq(G.model_pp); }

static void obj_pos(uintptr_t obj, float out[3]) { rvec(obj + OBJ_POS, out, 3); }

/* ------------------------------------------------------------------------------------------------------------ */
/* Small math (quaternions x,y,z,w; v_world = R(q) v_local; +Z forward, +Y up)                                   */

static void qmul(const float a[4], const float b[4], float o[4])
{
	o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
	o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
	o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}
static void qrot(const float q[4], const float v[3], float o[3])
{
	float x = q[0], y = q[1], z = q[2], w = q[3];
	float tx = 2 * (y * v[2] - z * v[1]), ty = 2 * (z * v[0] - x * v[2]), tz = 2 * (x * v[1] - y * v[0]);
	o[0] = v[0] + w * tx + (y * tz - z * ty);
	o[1] = v[1] + w * ty + (z * tx - x * tz);
	o[2] = v[2] + w * tz + (x * ty - y * tx);
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Virtual input: overrides of CWBInput vf_22/vf_23/vf_25 for exact command records, plus ON/OFF events          */

#define VIN_MAX 48
struct vin {
	uintptr_t rec;
	int cmd, input, event, on, has_value, press_tick;
	float value;
	double until;
	_Atomic uint64_t polls;
};
static struct vin g_vin[VIN_MAX];
static _Atomic int g_vin_n;
static uintptr_t g_vin_pi;
static uint64_t g_tick; /* input ticks */

static int vin_find(uintptr_t rec)
{
	int n = atomic_load_explicit(&g_vin_n, memory_order_acquire);
	for (int i = 0; i < n; i++)
		if (g_vin[i].rec == rec)
			return i;
	return -1;
}

static int first_input(uintptr_t rec)
{
	for (int k = 0; k < 4; k++) {
		int id = (int)r32(rec + REC_INPUTS + 4 * (uintptr_t)k);
		if (id != -1)
			return id;
	}
	return 0;
}

static int cmd_active(uintptr_t pi, int cmd)
{
	return g_fn_ok[FN_CMD_ACTIVE] && cmd >= 0 && cmd < NCMD ? (int)(FN(FN_CMD_ACTIVE, active_fn)(pi, (unsigned)cmd) & 0xff) : -1;
}

/* game thread only. event = send ON now (if not already on) and OFF at the release */
static int vin_press(uintptr_t pi, int cmd, int input, float value, int has_value, int on, int event, double secs)
{
	uintptr_t rec = pi_rec(pi, cmd);
	if (!rec) {
		logf_("VIN cmd=%d %s: no command record (unbound)", cmd, g_cmd_names[cmd]);
		return -1;
	}
	int i = vin_find(rec), was_on = 0;
	if (i < 0) {
		int n = atomic_load(&g_vin_n);
		if (n >= VIN_MAX) {
			logf_("VIN cmd=%d %s: too many virtual inputs held", cmd, g_cmd_names[cmd]);
			return -1;
		}
		i = n;
		memset(&g_vin[i], 0, sizeof g_vin[i]);
		g_vin[i].rec = rec;
		g_vin[i].cmd = cmd;
		g_vin[i].press_tick = (int)g_tick;
		atomic_store_explicit(&g_vin_n, n + 1, memory_order_release);
	} else {
		was_on = g_vin[i].on;
	}
	g_vin_pi = pi;
	g_vin[i].input = input < 0 ? first_input(rec) : input;
	g_vin[i].value = value;
	g_vin[i].has_value = has_value;
	g_vin[i].on = on;
	g_vin[i].event = g_vin[i].event || event;
	g_vin[i].until = mono() + secs;
	int en = rb(pi + PI_ENABLED), act = cmd_active(pi, cmd);
	if (event && on && !was_on && g_fn_ok[FN_CMD_ON])
		FN(FN_CMD_ON, cmd_fn)(pi, cmd, g_vin[i].input);
	if (rl_ok(RL_VIN))
		logf_("VIN press cmd=%d %s value=%g on=%d event=%d input=%d hold=%.3f pi=%#lx enabled=%d active=%d rec=%#lx t=%.6f",
		      cmd, g_cmd_names[cmd], value, on, event, g_vin[i].input, secs, (unsigned long)pi, en, act,
		      (unsigned long)rec, mono());
	return 0;
}

static void vin_release_at(int i, const char *why)
{
	int cmd = g_vin[i].cmd, input = g_vin[i].input, event = g_vin[i].event, on = g_vin[i].on, pt = g_vin[i].press_tick;
	uint64_t polls = atomic_load(&g_vin[i].polls);
	int n = atomic_load(&g_vin_n);
	g_vin[i].rec = 0; /* readers stop matching before the slot is reused */
	if (i != n - 1) {
		g_vin[i].cmd = g_vin[n - 1].cmd;
		g_vin[i].input = g_vin[n - 1].input;
		g_vin[i].event = g_vin[n - 1].event;
		g_vin[i].on = g_vin[n - 1].on;
		g_vin[i].has_value = g_vin[n - 1].has_value;
		g_vin[i].press_tick = g_vin[n - 1].press_tick;
		g_vin[i].value = g_vin[n - 1].value;
		g_vin[i].until = g_vin[n - 1].until;
		atomic_store(&g_vin[i].polls, atomic_load(&g_vin[n - 1].polls));
		g_vin[i].rec = g_vin[n - 1].rec;
	}
	atomic_store_explicit(&g_vin_n, n - 1, memory_order_release);
	if (event && on && g_vin_pi && g_fn_ok[FN_CMD_OFF])
		FN(FN_CMD_OFF, cmd_fn)(g_vin_pi, cmd, input);
	if (rl_ok(RL_VIN))
		logf_("VIN release cmd=%d %s polled=%llu ticks=%llu why=%s t=%.6f", cmd, g_cmd_names[cmd], (unsigned long long)polls,
		      (unsigned long long)(g_tick - (uint64_t)pt), why, mono());
}

static void vin_update(void)
{
	double t = mono();
	for (int i = atomic_load(&g_vin_n) - 1; i >= 0; i--)
		if (t >= g_vin[i].until && g_tick > (uint64_t)g_vin[i].press_tick + 1)
			vin_release_at(i, "time");
}

static void vin_release_all(const char *why)
{
	for (int i = atomic_load(&g_vin_n) - 1; i >= 0; i--)
		vin_release_at(i, why);
}

/* the hot paths: CWBInput vf_22 / vf_23 / vf_25 */
typedef float (*value_fn)(void *self, void *rec);
typedef uint64_t (*ison_fn)(void *self, void *rec, uint64_t a3);

static float p_value_common(int hk, void *self, void *rec)
{
	int n = atomic_load_explicit(&g_vin_n, memory_order_acquire);
	for (int i = 0; i < n; i++)
		if (g_vin[i].rec == (uintptr_t)rec && g_vin[i].has_value) {
			atomic_fetch_add_explicit(&g_vin[i].polls, 1, memory_order_relaxed);
			return g_vin[i].value;
		}
	return ((value_fn)g_h[hk].orig)(self, rec);
}
static float p_value(void *self, void *rec) { first_call(HK_VALUE, atomic_fetch_add_explicit(&g_h[HK_VALUE].calls, 1, memory_order_relaxed) + 1); return p_value_common(HK_VALUE, self, rec); }
static float p_value2(void *self, void *rec) { first_call(HK_VALUE2, atomic_fetch_add_explicit(&g_h[HK_VALUE2].calls, 1, memory_order_relaxed) + 1); return p_value_common(HK_VALUE2, self, rec); }
static uint64_t p_ison(void *self, void *rec, uint64_t a3)
{
	first_call(HK_ISON, atomic_fetch_add_explicit(&g_h[HK_ISON].calls, 1, memory_order_relaxed) + 1);
	int n = atomic_load_explicit(&g_vin_n, memory_order_acquire);
	for (int i = 0; i < n; i++)
		if (g_vin[i].rec == (uintptr_t)rec) {
			atomic_fetch_add_explicit(&g_vin[i].polls, 1, memory_order_relaxed);
			return (uint64_t)(g_vin[i].on != 0);
		}
	return ((ison_fn)g_h[HK_ISON].orig)(self, rec, a3);
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Pad specs: the Win32GamePad input ids (docs/re-input.md 1.3)                                                  */

static const struct { const char *name; int id; } g_pad_ids[] = {
	{ "UP", 2000 }, { "DOWN", 2001 }, { "LEFT", 2002 }, { "RIGHT", 2003 }, { "START", 2004 }, { "BACK", 2005 },
	{ "L3", 2006 }, { "R3", 2007 }, { "LB", 2008 }, { "RB", 2009 }, { "A", 2010 }, { "B", 2011 }, { "X", 2012 },
	{ "Y", 2013 }, { "RT", 2014 }, { "LT", 2015 }, { "LX", 2016 }, { "LY", 2017 }, { "RX", 2018 }, { "RY", 2019 },
};

static int pad_id(const char *s)
{
	for (size_t i = 0; i < sizeof g_pad_ids / sizeof g_pad_ids[0]; i++)
		if (!strcasecmp(s, g_pad_ids[i].name))
			return g_pad_ids[i].id;
	return -1;
}

static const char *input_name(int id, char *b, size_t cap)
{
	if (id == -1)
		snprintf(b, cap, "-");
	else if (id >= 2000 && id <= 2021) {
		const char *n = "?";
		for (size_t i = 0; i < sizeof g_pad_ids / sizeof g_pad_ids[0]; i++)
			if (g_pad_ids[i].id == id)
				n = g_pad_ids[i].name;
		snprintf(b, cap, "pad:%s", n);
	} else if (id >= 1000 && id <= 1008)
		snprintf(b, cap, "mouse:%d", id - 1000);
	else if (id >= 0 && id <= 255)
		snprintf(b, cap, "key:0x%02x", id);
	else
		snprintf(b, cap, "id:%d", id);
	return b;
}

static float valrec_scale(uintptr_t rec, int k)
{
	int vi = (int)r32(rec + REC_VALIDX + 4 * (uintptr_t)k);
	uintptr_t ctx = rq(rec + REC_CTX), vr = rq(ctx + CTX_VALRECS);
	if (vi < 0 || vi > 100000 || !vr)
		return 1.0f;
	return rf(vr + (uintptr_t)vi * VALREC_SIZE);
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Command queue (the writer thread fills it, the game thread runs it)                                            */

#define CQ_N 64
#define CQ_LEN 512
static struct { char line[CQ_LEN]; unsigned seq; } g_cq[CQ_N];
static unsigned g_cq_head, g_cq_tail, g_cmd_seq;
static pthread_mutex_t g_cq_mu = PTHREAD_MUTEX_INITIALIZER;

static void cq_push(const char *line)
{
	pthread_mutex_lock(&g_cq_mu);
	if (g_cq_tail - g_cq_head < CQ_N) {
		unsigned i = g_cq_tail++ % CQ_N;
		snprintf(g_cq[i].line, CQ_LEN, "%s", line);
		g_cq[i].seq = ++g_cmd_seq;
		pthread_mutex_unlock(&g_cq_mu);
		logf_("CMD queued seq=%u '%s' t=%.6f", g_cmd_seq, line, mono());
		return;
	}
	pthread_mutex_unlock(&g_cq_mu);
	logf_("CMD DROPPED (queue full) '%s'", line);
}

static int cq_pop(char *out, unsigned *seq)
{
	pthread_mutex_lock(&g_cq_mu);
	int ok = g_cq_head != g_cq_tail;
	if (ok) {
		unsigned i = g_cq_head++ % CQ_N;
		snprintf(out, CQ_LEN, "%s", g_cq[i].line);
		*seq = g_cq[i].seq;
	}
	pthread_mutex_unlock(&g_cq_mu);
	return ok;
}

/* words that must run on the server tick (after CGameServerShell::vf_10) */
#define SQ_N 8
static struct { char line[CQ_LEN]; unsigned seq; } g_sq[SQ_N];
static int g_sq_n;

/* ------------------------------------------------------------------------------------------------------------ */
/* Probe state                                                                                                     */

static int g_state_on = 1;
static uintptr_t g_renderer;           /* CRendererHandle (from vf_107's this); frame counter at +0x38 */
static double g_resume_t;              /* `sleep` */
static double g_server_last_t;         /* last server tick */
static uintptr_t g_last_player_attack; /* from the P_ATK hooks */
static char g_last_player_attack_name[128];
static uintptr_t g_last_hit_c;         /* the last dealhit target (hitstop's default) */
static double g_last_hit_t;

/* Combat objects seen by the hooks (orcs that attacked, opened a parry window, were hit) */
#define SEEN_N 64
#define SEEN_AGE 10.0 /* a Combat pointer a hook saw is only a candidate for this long (the object may be freed) */
static struct { uintptr_t c; double t; } g_seen[SEEN_N];
static unsigned g_seen_i;
static pthread_mutex_t g_seen_mu = PTHREAD_MUTEX_INITIALIZER;
static int nc_seen(void)
{
	int n = 0;
	pthread_mutex_lock(&g_seen_mu);
	for (int i = 0; i < SEEN_N; i++)
		n += g_seen[i].c != 0;
	pthread_mutex_unlock(&g_seen_mu);
	return n;
}

static void seen_add(uintptr_t c)
{
	if (!c)
		return;
	pthread_mutex_lock(&g_seen_mu);
	for (int i = 0; i < SEEN_N; i++)
		if (g_seen[i].c == c) {
			g_seen[i].t = mono();
			pthread_mutex_unlock(&g_seen_mu);
			return;
		}
	g_seen[g_seen_i % SEEN_N].c = c;
	g_seen[g_seen_i % SEEN_N].t = mono();
	g_seen_i++;
	pthread_mutex_unlock(&g_seen_mu);
}

/* pose sampling */
static struct {
	int left, total, have_node;
	uint32_t node;
	char node_name[64];
	uintptr_t m, obj, body;
	float pre[3], post[3], cam[3], obj_pre[3], obj_post[3];
	int have_pre, have_post, have_cam;
	unsigned stats_eq_post_vis, stats_eq_pre_post, stats_eq_cam_vis, stats_frames;
} g_pose;

/* head pieces hidden by hidehead */
static struct { uint32_t piece; char name[64]; } g_hidden[64];
static int g_hidden_n;
static uintptr_t g_hidden_obj;

/* fpcam */
static struct { int on, have_pitch; float fwd, up, pitch_deg; } g_fp = { 0, 0, 10.0f, 8.0f, 0.0f };

/* near override */
static int g_near_saved;
static float g_near_old;

/* autocounter */
static struct { int on, cmd, frames, pending; uintptr_t last_c; unsigned presses; } g_ac;
static struct { uintptr_t c; uint8_t parry; double bpw_t; } g_win[SEEN_N];

/* vf_105 probe */
static _Atomic int g_ui105_budget = 20;

/* events for SOMVR_PROBE_AUTO */
enum { EV_START, EV_MENU, EV_WORLD, EV_N };
static double g_ev_t[EV_N] = { -1, -1, -1 };
static double g_ev_cand[EV_N];
#define AUTO_N 32
static struct { int ev; double after; char line[CQ_LEN]; int done; } g_auto[AUTO_N];
static int g_auto_n;

/* ------------------------------------------------------------------------------------------------------------ */
/* Read-only words                                                                                                */

static FILE *out_file(const char *base, unsigned seq, char *path, size_t cap)
{
	mkdir(g_out_dir, 0755);
	if (seq)
		snprintf(path, cap, "%s/%s-%u.tsv", g_out_dir, base, seq);
	else
		snprintf(path, cap, "%s/%s.tsv", g_out_dir, base);
	return fopen(path, "we");
}

static void w_bindings(const struct ctx *c, unsigned seq)
{
	if (!c->pi) {
		logf_("DONE seq=%u bindings: no PlayerInput (local player entry %#lx)", seq, (unsigned long)c->entry);
		return;
	}
	char path[1200];
	FILE *f = out_file("bindings", seq, path, sizeof path);
	if (f)
		fprintf(f, "id\tname\trec\tactive\tswallow\tin0\tin1\tin2\tin3\tscale0\tscale1\tscale2\tscale3\n");
	int bound = 0, pad = 0, kbm = 0;
	uintptr_t b = rq(c->pi + PI_ENTRIES);
	for (int i = 0; i < NCMD; i++) {
		uintptr_t rec = rq(b + (uintptr_t)i * PIE_SIZE);
		int in[4] = { -1, -1, -1, -1 }, any = 0, hp = 0, hk = 0;
		float sc[4] = { 0, 0, 0, 0 };
		if (rec)
			for (int k = 0; k < 4; k++) {
				in[k] = (int)r32(rec + REC_INPUTS + 4 * (uintptr_t)k);
				sc[k] = in[k] != -1 ? valrec_scale(rec, k) : 0.0f;
				any |= in[k] != -1;
				hp |= in[k] >= 2000 && in[k] <= 2021;
				hk |= in[k] >= 0 && in[k] < 2000;
			}
		bound += any;
		pad += hp;
		kbm += hk;
		char n[4][24];
		if (f)
			fprintf(f, "%d\t%s\t%#lx\t%d\t%u\t%s\t%s\t%s\t%s\t%g\t%g\t%g\t%g\n", i, g_cmd_names[i], (unsigned long)rec,
				cmd_active(c->pi, i), rb(b + (uintptr_t)i * PIE_SIZE + 0x29), input_name(in[0], n[0], 24),
				input_name(in[1], n[1], 24), input_name(in[2], n[2], 24), input_name(in[3], n[3], 24), sc[0],
				sc[1], sc[2], sc[3]);
		if (any && ((i >= 1 && i <= 34) || (i >= 42 && i <= 58) || (i >= 118 && i <= 160) || i == 243 || i == 260))
			logf_("BIND %d %s %s %s %s %s active=%d", i, g_cmd_names[i], input_name(in[0], n[0], 24),
			      input_name(in[1], n[1], 24), input_name(in[2], n[2], 24), input_name(in[3], n[3], 24),
			      cmd_active(c->pi, i));
	}
	if (f)
		fclose(f);
	logf_("DONE seq=%u bindings: %d commands bound (pad inputs in %d, keyboard/mouse in %d), pi=%#lx enabled=%d "
	      "index=%u padmode=%d file=%s", seq, bound, pad, kbm, (unsigned long)c->pi, rb(c->pi + PI_ENABLED),
	      rb(c->pi + PI_INDEX), c->profile ? rb(c->profile + PROF_PAD) : -1, f ? path : "(not written)");
}

static int piece_names(const struct ctx *c, char names[][64], int cap)
{
	uintptr_t obj = char_obj(c), m = model_iface();
	uint32_t cnt = 0;
	if (!obj || !g_fn_ok[FN_NUMPIECES] || !g_fn_ok[FN_PIECENAME] || FN(FN_NUMPIECES, numpieces_fn)(m, obj, &cnt) & 0xff)
		return -1;
	int n = (int)(cnt > (uint32_t)cap ? (uint32_t)cap : cnt);
	for (int i = 0; i < n; i++) {
		char buf[128] = "";
		FN(FN_PIECENAME, piecename_fn)(m, obj, (uint32_t)i, buf, sizeof buf);
		buf[127] = 0;
		memcpy(names[i], buf, 63);
		names[i][63] = 0;
	}
	return n;
}

static void w_pieces(const struct ctx *c, unsigned seq)
{
	static char names[128][64];
	int n = piece_names(c, names, 128);
	if (n < 0) {
		logf_("DONE seq=%u pieces: unavailable (obj=%#lx)", seq, (unsigned long)char_obj(c));
		return;
	}
	for (int i = 0; i < n; i++)
		logf_("PIECE %d '%s'", i, names[i]);
	logf_("DONE seq=%u pieces: %d pieces on obj=%#lx (hChar=%#lx bodyObj=%#lx same=%d)", seq, n,
	      (unsigned long)char_obj(c), (unsigned long)c->hchar, (unsigned long)c->bodyobj, c->hchar == c->bodyobj);
}

static int node_xf(uintptr_t obj, uint32_t node, float xf[8])
{
	float buf[16] = { 0 };
	if (!obj || !g_fn_ok[FN_NODEXF])
		return -1;
	uint64_t r = FN(FN_NODEXF, nodexf_fn)(model_iface(), obj, node, buf, 1);
	memcpy(xf, buf, 8 * sizeof(float));
	return (r & 0xff) ? -1 : 0;
}

static const char *const g_head_names[] = {
	"Head", "head", "HEAD", "Head_joint", "Head_Joint", "head_joint", "HeadJoint", "Head1", "Head_01", "Head_jnt",
	"C_Head", "M_Head", "Bip01 Head", "Bip01_Head", "Skull", "Face", "Jaw", "Jaw_joint", "HeadTop", "Head_Top",
	"HeadEnd", "Head_End", "HeadTip", "L_Eye", "R_Eye", "L_Eye_joint", "R_Eye_joint", "Eye_L", "Eye_R", "LeftEye",
	"RightEye", "Neck", "Neck_joint", "Neck1", "Neck1_joint", "Neck2", "Neck2_joint", "Spine4_joint", "Torso_Base",
	"Pelvis", "L_Hand", "R_Hand", "L_Armu", "R_Armu", "R_Foot", "Hood", "Helmet", "Hair",
};

static const size_t g_neck_index = 31; /* g_head_names[31] = "Neck" (checked in the selftest) */

/* the node-name hash of re-animation.md 2.3: h = h*0x397 + T[c] with the 256-byte table T in the binary */
static uint32_t name_hash(const char *s)
{
	static uint8_t tab[256];
	static int have;
	if (!have) {
		if (rd(G.hash_table, tab, sizeof tab))
			return 0;
		have = 1;
	}
	uint32_t h = 0;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++)
		h = h * 0x397u + tab[*p];
	return h;
}

/* skel = *(*(model+0x28)+0x10): +0x00 nodes (0x40 each: +0 parent byte, +1 index), +0x08 count, +0x10/+0x18 sockets
 * (0x30 each: hash, parent index, offset pos/quat), +0x38 char* per node, +0x40 hash per node */
struct skel {
	uintptr_t base, nodes, names, hashes, sockets;
	uint32_t count, nsockets;
};

static int skel_get(uintptr_t obj, struct skel *sk)
{
	memset(sk, 0, sizeof *sk);
	if (!g_fn_ok[FN_MODELOF] || !obj)
		return -1;
	uintptr_t model = FN(FN_MODELOF, modelof_fn)(obj);
	sk->base = rq(rq(model + 0x28) + 0x10);
	if (!sk->base)
		return -1;
	sk->nodes = rq(sk->base);
	sk->count = r32(sk->base + 8);
	sk->sockets = rq(sk->base + 0x10);
	sk->nsockets = r32(sk->base + 0x18);
	sk->names = rq(sk->base + 0x38);
	sk->hashes = rq(sk->base + 0x40);
	if (!sk->nodes || sk->count == 0 || sk->count > 4096)
		return -1;
	if (sk->nsockets > 256)
		sk->nsockets = 0;
	return 0;
}

static const char *skel_name(const struct skel *sk, uint32_t i, char *out, size_t cap)
{
	out[0] = 0;
	if (i < sk->count && sk->names)
		rstr(rq(sk->names + 8u * i), out, cap);
	return out;
}

static void w_nodes(const struct ctx *c, unsigned seq)
{
	uintptr_t obj = char_obj(c), m = model_iface();
	if (!obj || !g_fn_ok[FN_GETNODE]) {
		logf_("DONE seq=%u nodes: unavailable (obj=%#lx)", seq, (unsigned long)obj);
		return;
	}
	float op[3], cp[3] = { 0, 0, 0 };
	obj_pos(obj, op);
	if (c->cam)
		rvec(c->cam + CAM_POS, cp, 3);

	/* the skeleton: every node with its real name, parent and hash (re-animation.md 2.2/2.4) */
	struct skel sk;
	int have_skel = skel_get(obj, &sk) == 0, names_ok = 0, hash_ok = 0, head_idx = -1, head_from = 0;
	char path[1200], nm[128], hd[64] = "";
	FILE *f = have_skel ? out_file("nodes", seq, path, sizeof path) : NULL;
	if (f)
		fprintf(f, "kind\tindex\tparent\tname\thash\thash_of_name\tx\ty\tz\tqx\tqy\tqz\tqw\trel_x\trel_y\trel_z\n");
	if (have_skel) {
		for (uint32_t i = 0; i < sk.count; i++) {
			uint32_t h = sk.hashes ? r32(sk.hashes + 4u * i) : 0;
			uint8_t par = rb(sk.nodes + (uintptr_t)i * 0x40);
			skel_name(&sk, i, nm, sizeof nm);
			uint32_t hn = nm[0] ? name_hash(nm) : 0;
			names_ok += nm[0] != 0;
			hash_ok += nm[0] && hn == h;
			float xf[8] = { 0 };
			node_xf(obj, h, xf);
			if (f)
				fprintf(f, "node\t%u\t%u\t%s\t%#x\t%#x\t%.3f\t%.3f\t%.3f\t%.5f\t%.5f\t%.5f\t%.5f\t%.2f\t%.2f\t%.2f\n",
					i, par, nm[0] ? nm : "?", h, hn, xf[0], xf[1], xf[2], xf[3], xf[4], xf[5], xf[6],
					xf[0] - op[0], xf[1] - op[1], xf[2] - op[2]);
			if (nm[0] && (strcasestr(nm, "hand") || strcasestr(nm, "armu") || strcasestr(nm, "head") ||
				      strcasestr(nm, "neck") || strcasestr(nm, "pelvis") || strcasestr(nm, "weapon") ||
				      strcasestr(nm, "fore") || strcasestr(nm, "clav") || strcasestr(nm, "eye")))
				logf_("NODE %u parent=%u '%s' hash=%#x name_hash=%#x pos=%.2f,%.2f,%.2f rel_obj=%.2f,%.2f,%.2f", i, par,
				      nm, h, hn, xf[0], xf[1], xf[2], xf[0] - op[0], xf[1] - op[1], xf[2] - op[2]);
		}
		/* sockets (0x30 bytes: hash, parent node index, offset pos/quat): SocketHead names the head node */
		uint32_t want = name_hash("SocketHead");
		for (uint32_t i = 0; i < sk.nsockets; i++) {
			uintptr_t so = sk.sockets + (uintptr_t)i * 0x30;
			uint32_t h = r32(so), pidx = r32(so + 4);
			float off[7] = { 0 };
			rvec(so + 8, off, 7);
			float xf[8] = { 0 };
			node_xf(obj, h, xf);
			if (f)
				fprintf(f, "socket\t%u\t%u\t%s\t%#x\t%#x\t%.3f\t%.3f\t%.3f\t%.5f\t%.5f\t%.5f\t%.5f\t%.2f\t%.2f\t%.2f\n",
					i, pidx, h == want ? "SocketHead" : "?", h, 0u, xf[0], xf[1], xf[2], xf[3], xf[4], xf[5], xf[6],
					xf[0] - op[0], xf[1] - op[1], xf[2] - op[2]);
			if (h == want) {
				head_idx = pidx < sk.count ? (int)pidx : -1;
				head_from = 1;
				logf_("SOCKET %u 'SocketHead' hash=%#x parent_node=%u offset_pos=%.2f,%.2f,%.2f world=%.2f,%.2f,%.2f "
				      "rel_obj=%.2f,%.2f,%.2f", i, h, pidx, off[0], off[1], off[2], xf[0], xf[1], xf[2],
				      xf[0] - op[0], xf[1] - op[1], xf[2] - op[2]);
			}
		}
	}
	/* fallback (and a cross-check): GetNode with the candidate names of re-firstperson.md 2.2 */
	int found = 0, cand = -1;
	for (size_t i = 0; i < sizeof g_head_names / sizeof g_head_names[0]; i++) {
		uint32_t node = 0xffffffff;
		if (FN(FN_GETNODE, getnode_fn)(m, obj, g_head_names[i], &node) & 0xff)
			continue;
		float xf[8] = { 0 };
		int ok = node_xf(obj, node, xf) == 0;
		found++;
		uint32_t idx = g_fn_ok[FN_HASHINDEX] ? (uint32_t)FN(FN_HASHINDEX, hashindex_fn)(m, obj, node) : 0xffffffffu;
		logf_("NODEBYNAME '%s' id=%#x index=%d xf_ok=%d pos=%.3f,%.3f,%.3f rel_obj=%.2f,%.2f,%.2f", g_head_names[i], node,
		      (int)idx, ok, xf[0], xf[1], xf[2], xf[0] - op[0], xf[1] - op[1], xf[2] - op[2]);
		if (cand < 0 && i <= g_neck_index)
			cand = (int)i;
	}
	/* pick the node the pose/fpcam words use: the head bone named by SocketHead, else the best candidate name */
	if (head_idx >= 0 && sk.hashes) {
		g_pose.node = r32(sk.hashes + 4u * (uint32_t)head_idx);
		g_pose.have_node = g_pose.node != 0;
		skel_name(&sk, (uint32_t)head_idx, hd, sizeof hd);
		snprintf(g_pose.node_name, sizeof g_pose.node_name, "%s", hd[0] ? hd : "(SocketHead parent)");
	} else if (cand >= 0) {
		uint32_t node = 0;
		FN(FN_GETNODE, getnode_fn)(m, obj, g_head_names[cand], &node);
		g_pose.node = node;
		g_pose.have_node = 1;
		snprintf(g_pose.node_name, sizeof g_pose.node_name, "%s", g_head_names[cand]);
	}
	if (f)
		fclose(f);
	float hx[8] = { 0 };
	if (g_pose.have_node)
		node_xf(obj, g_pose.node, hx);
	logf_("DONE seq=%u nodes: skeleton %s (%u nodes, %u sockets, %d names readable, %d of them hash correctly), "
	      "%d candidate names found, file=%s; head node = %s (%#x, from %s) at %.2f,%.2f,%.2f = obj%+.2f,%+.2f,%+.2f, "
	      "camera (cam+0x44c) %.2f,%.2f,%.2f -> eye offset from the head %+.2f,%+.2f,%+.2f; obj=%#lx (hChar=%#lx "
	      "bodyObj=%#lx same=%d) obj pos=%.2f,%.2f,%.2f",
	      seq, have_skel ? "read" : "UNREADABLE", sk.count, sk.nsockets, names_ok, hash_ok, found,
	      f ? path : "(none)", g_pose.have_node ? g_pose.node_name : "(none)", g_pose.node,
	      head_from ? "SocketHead" : "candidate names", hx[0], hx[1], hx[2], hx[0] - op[0], hx[1] - op[1], hx[2] - op[2],
	      cp[0], cp[1], cp[2], cp[0] - hx[0], cp[1] - hx[1], cp[2] - hx[2], (unsigned long)obj, (unsigned long)c->hchar,
	      (unsigned long)c->bodyobj, c->hchar == c->bodyobj, op[0], op[1], op[2]);
}

static void w_attacks(unsigned seq)
{
	uintptr_t cat = rq(G.atkcat_pp);
	uint32_t n = r32(cat + CAT_COUNT);
	uintptr_t base = rq(cat + CAT_RECS);
	if (!cat || !base || n == 0 || n > 200000) {
		logf_("DONE seq=%u attacks: no Combat/Attack category (cat=%#lx n=%u)", seq, (unsigned long)cat, n);
		return;
	}
	char path[1200], name[256];
	FILE *f = out_file("attacks", 0, path, sizeof path);
	int shown = 0;
	if (f)
		fprintf(f, "index\trec\thash\tname\n");
	for (uint32_t i = 0; i < n; i++) {
		uintptr_t rec = base + (uintptr_t)i * DBREC_SIZE;
		rec_name(rec, name, sizeof name);
		if (f)
			fprintf(f, "%u\t%#lx\t%#x\t%s\n", i, (unsigned long)rec, r32(rec + 0x28), name);
		if (shown < 40 && (strcasestr(name, "player") || strcasestr(name, "talion"))) {
			logf_("ATTACK %u '%s'", i, name);
			shown++;
		}
	}
	if (f)
		fclose(f);
	logf_("DONE seq=%u attacks: %u Combat/Attack records, file=%s", seq, n, f ? path : "(not written)");
}

/* one candidate Combat* -> validated facts; 0 = not a valid character Combat */
struct orc { uintptr_t c, owner, hobj; float pos[3], dist; int dead, ai, player, npc; };
static int orc_get(uintptr_t c, const float ppos[3], struct orc *o)
{
	memset(o, 0, sizeof *o);
	if (!c || rq(c) != G.vt_combat)
		return 0;
	o->c = c;
	o->owner = rq(c + C_OWNER);
	uintptr_t vt = rq(o->owner);
	o->npc = vt == G.vt_npc;
	o->player = vt == G.vt_playerobj;
	if (!o->npc && !o->player)
		return 0;
	o->hobj = rq(c + C_HOBJ);
	if (!o->hobj)
		return 0;
	obj_pos(o->hobj, o->pos);
	o->dead = (rb(o->owner + CH_FLAGS) & 1) || rb(c + C_DEAD);
	o->ai = rq(c + C_AI) != 0;
	float dx = o->pos[0] - ppos[0], dy = o->pos[1] - ppos[1], dz = o->pos[2] - ppos[2];
	o->dist = sqrtf(dx * dx + dy * dy + dz * dz);
	return 1;
}

/* candidates rejected by the vtable check, so a wrong vtable guess shows in the log instead of an empty list */
static int g_orc_rejected, g_orc_nband;
static uintptr_t g_orc_reject_vt[2];

/* every Combat* near the player: the three targeting bands of PlayerCombat and the ones the hooks saw */
static int orcs_collect(const struct ctx *c, struct orc *out, int cap)
{
	uintptr_t cand[256];
	int nc = 0;
	int nband = 0;
	if (c->pc)
		for (int k = 0; k < 3; k++) {
			uintptr_t vb = c->pc + PC_BANDS + 0x18 * (uintptr_t)k, b = rq(vb), e = rq(vb + 8);
			size_t n = e > b && (e - b) % PCB_SIZE == 0 ? (size_t)(e - b) / PCB_SIZE : 0;
			if (n > 48)
				n = 48;
			static unsigned char band[48 * PCB_SIZE]; /* game thread only */
			if (!n || rd(b, band, n * PCB_SIZE))
				continue;
			for (size_t i = 0; i < n && nc < 192; i++) {
				uintptr_t v;
				memcpy(&v, band + i * PCB_SIZE + 0x30, 8);
				cand[nc++] = v;
				nband++;
			}
		}
	double now = mono();
	pthread_mutex_lock(&g_seen_mu);
	for (int i = 0; i < SEEN_N; i++) {
		if (!g_seen[i].c)
			continue;
		if (now - g_seen[i].t > SEEN_AGE)
			g_seen[i].c = 0; /* too old to dereference */
		else if (nc < 256)
			cand[nc++] = g_seen[i].c;
	}
	pthread_mutex_unlock(&g_seen_mu);
	float pp[3] = { 0, 0, 0 };
	if (c->hchar)
		obj_pos(c->hchar, pp);
	int n = 0;
	g_orc_nband = nband;
	g_orc_rejected = 0;
	g_orc_reject_vt[0] = g_orc_reject_vt[1] = 0;
	for (int i = 0; i < nc && n < cap; i++) {
		int dup = 0;
		for (int j = 0; j < n; j++)
			dup |= out[j].c == cand[i];
		if (dup || !cand[i] || cand[i] == c->scombat)
			continue;
		if (orc_get(cand[i], pp, &out[n])) {
			if (!out[n].player)
				n++;
			continue;
		}
		g_orc_rejected++;
		if (!g_orc_reject_vt[0]) { /* the first rejection's vtables: wrong guess or a class we do not know */
			g_orc_reject_vt[0] = rq(cand[i]);
			g_orc_reject_vt[1] = rq(rq(cand[i] + C_OWNER));
		}
	}
	for (int i = 1; i < n; i++) /* by distance */
		for (int j = i; j > 0 && out[j].dist < out[j - 1].dist; j--) {
			struct orc t = out[j];
			out[j] = out[j - 1];
			out[j - 1] = t;
		}
	return n;
}

static void w_orcs(const struct ctx *c, unsigned seq)
{
	struct orc o[64];
	int n = orcs_collect(c, o, 64);
	char a[128], r[128];
	for (int i = 0; i < n && i < 20; i++)
		logf_("ORC %d c=%#lx hobj=%#lx dist=%.1f pos=%.1f,%.1f,%.1f dead=%d ai=%d record='%s' attack='%s' parry=%u hitwin=%u",
		      i, (unsigned long)o[i].c, (unsigned long)o[i].hobj, o[i].dist, o[i].pos[0], o[i].pos[1], o[i].pos[2],
		      o[i].dead, o[i].ai, rec_name(rq(o[i].c + C_CHARREC), r, sizeof r), rec_name(rq(o[i].c + C_ATTACK), a, sizeof a),
		      rb(o[i].c + C_WIN_PARRY), rb(o[i].c + C_WIN_HIT));
	logf_("DONE seq=%u orcs: %d candidates, %d rejected (first rejected: Combat vtable %#lx want %#lx, owner vtable %#lx "
	      "want NPC %#lx or CPlayerObj %#lx); player server Combat %#lx (vtable %#lx), PlayerCombat %#lx", seq, n,
	      g_orc_rejected, (unsigned long)g_orc_reject_vt[0], (unsigned long)G.vt_combat, (unsigned long)g_orc_reject_vt[1],
	      (unsigned long)G.vt_npc, (unsigned long)G.vt_playerobj, (unsigned long)c->scombat,
	      (unsigned long)rq(c->scombat), (unsigned long)c->pc);
	logf_("ORCS from the targeting bands: %d entries; from the hooks: %d live entries (older than %.0f s are dropped)",
	      g_orc_nband, nc_seen(), SEEN_AGE);
}

static void w_ui(const struct ctx *c, unsigned seq)
{
	int ui[4] = { 0 }, d3[4] = { 0 };
	float uif[4] = { 0 };
	if (c->shell) {
		rd(c->shell + SH_UIRECT, ui, 16);
		rd(c->shell + SH_UIRECT, uif, 16);
		rd(c->shell + SH_3DRECT, d3, 16);
	}
	uintptr_t d = G.device;
	logf_("UI rect(shell+0x80dc8)=%d,%d,%d,%d (as floats %g,%g,%g,%g) 3D rect(shell+0x80df0)=%d,%d,%d,%d; device UI size "
	      "%ux%u 3D size %ux%u UI tex %#lx 3D tex %#lx (same=%d) UI srv %#lx 3D srv %#lx redirect fn %#lx; im modal=%u",
	      ui[0], ui[1], ui[2], ui[3], uif[0], uif[1], uif[2], uif[3], d3[0], d3[1], d3[2], d3[3], r32(d + 0x90), r32(d + 0x94),
	      r32(d + 0xd8), r32(d + 0xdc), (unsigned long)rq(d + 0x1b8), (unsigned long)rq(d + 0x1c0),
	      rq(d + 0x1b8) == rq(d + 0x1c0), (unsigned long)rq(d + 0x1e8), (unsigned long)rq(d + 0x1f0),
	      (unsigned long)rq(d + 0x200), c->im ? rb(c->im + 0x2a48) : 0);
	unsigned char fade[0x18] = { 0 };
	char fh[80];
	if (c->im)
		rd(c->im + IM_FADE, fade, sizeof fade);
	hex_of(fade, (int)sizeof fade, fh, sizeof fh);
	logf_("UI formats: UI surface dev+0xa0=%u 3D surface dev+0xe8=%u (DXGI; 28 = R8G8B8A8_UNORM); the screen fade im+0x2ad8: %s",
	      r32(d + 0xa0), r32(d + 0xe8), fh);
	atomic_store(&g_ui105_budget, 120);
	logf_("DONE seq=%u ui: vf_105 logging armed for 120 calls (hook %s)", seq, g_h[HK_UI105].installed ? "on" : "NOT installed");
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Active words                                                                                                    */

static int active_allowed(const char *word, unsigned seq)
{
	if (g_guard_ok == 1 || g_noguard_ok)
		return 1;
	logf_("FAIL seq=%u %s: refused, spike1's save guard is not in place (WBOnlineService slot 219 %s; set "
	      "SOMVR_PROBE_NOGUARD_OK=1 to override)", seq, word, g_guard_ok < 0 ? "not checked yet" : "unpatched");
	return 0;
}

static int parse_cmd_id(const char *s)
{
	char *e;
	long v = strtol(s, &e, 10);
	if (*s && !*e)
		return v >= 0 && v < NCMD ? (int)v : -1;
	for (int i = 0; i < NCMD; i++)
		if (!strcasecmp(s, g_cmd_names[i]))
			return i;
	return -1;
}

static double num(const char *s, double dflt)
{
	if (!s || !*s)
		return dflt;
	char *e;
	double v = strtod_l(s, &e, g_cloc);
	return *e ? dflt : v;
}
static int is_num(const char *s)
{
	char *e;
	if (!s || !*s)
		return 0;
	strtod_l(s, &e, g_cloc);
	return !*e;
}

static void w_cmd(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	if (!c->pi) {
		logf_("FAIL seq=%u cmd: no PlayerInput", seq);
		return;
	}
	int n = 0;
	double ms = 120;
	for (int i = 1; i < argc; i++)
		if (!strncmp(argv[i], "ms=", 3))
			ms = num(argv[i] + 3, 120);
	for (int i = 1; i < argc; i++) {
		if (!strncmp(argv[i], "ms=", 3))
			continue;
		int id = parse_cmd_id(argv[i]);
		if (id < 0) {
			logf_("FAIL seq=%u cmd: unknown command '%s'", seq, argv[i]);
			continue;
		}
		n += vin_press(c->pi, id, -1, 1.0f, 1, 1, 1, ms / 1000.0) == 0;
	}
	logf_("DONE seq=%u cmd: %d pressed for %.0f ms (a hold is `ms=N`; every other word is a command id or name)", seq, n, ms);
}

static void w_axes(const struct ctx *c, unsigned seq, const char *word, const int *cmds, int ncmds, const float *vals, double ms)
{
	if (!c->pi) {
		logf_("FAIL seq=%u %s: no PlayerInput", seq, word);
		return;
	}
	int n = 0;
	for (int i = 0; i < ncmds; i++)
		n += vin_press(c->pi, cmds[i], -1, vals[i], 1, fabsf(vals[i]) > 0.25f, 0, ms / 1000.0) == 0;
	float pp[3] = { 0, 0, 0 };
	if (c->hchar)
		obj_pos(c->hchar, pp);
	logf_("DONE seq=%u %s: %d axes set for %.0f ms; player pos now %.1f,%.1f,%.1f", seq, word, n, ms, pp[0], pp[1], pp[2]);
}

static void w_pad(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	if (!c->pi) {
		logf_("FAIL seq=%u pad: no PlayerInput", seq);
		return;
	}
	int last = argc > 2 && is_num(argv[argc - 1]) ? argc - 1 : argc;
	double ms = last < argc ? num(argv[argc - 1], 150) : 150;
	int presses = 0;
	uintptr_t b = rq(c->pi + PI_ENTRIES);
	for (int a = 1; a < last; a++) {
		char spec[64];
		snprintf(spec, sizeof spec, "%s", argv[a]);
		char *eq = strchr(spec, '=');
		float v = 1.0f;
		int is_axis = eq != NULL;
		if (eq) {
			*eq = 0;
			v = (float)num(eq + 1, 0);
		}
		int id = pad_id(spec);
		if (id < 0) {
			logf_("FAIL seq=%u pad: unknown input '%s'", seq, spec);
			continue;
		}
		int hits = 0;
		for (int cmd = 0; cmd < NCMD; cmd++) {
			uintptr_t rec = rq(b + (uintptr_t)cmd * PIE_SIZE);
			if (!rec)
				continue;
			for (int k = 0; k < 4; k++)
				if ((int)r32(rec + REC_INPUTS + 4 * (uintptr_t)k) == id) {
					float val = v * valrec_scale(rec, k);
					int on = is_axis ? fabsf(val) > 0.25f : 1;
					hits++;
					presses += vin_press(c->pi, cmd, id, val, 1, on, !is_axis, ms / 1000.0) == 0;
					break;
				}
		}
		if (!hits)
			logf_("PAD %s (input %d): no command is bound to it in the current binding set", spec, id);
	}
	logf_("DONE seq=%u pad: %d command presses for %.0f ms", seq, presses, ms);
}

static void w_padmode(const struct ctx *c, unsigned seq, const char *arg)
{
	if (!c->im || !c->entry || !g_fn_ok[FN_INPUT_MODE]) {
		logf_("FAIL seq=%u padmode: im=%#lx entry=%#lx fn=%d", seq, (unsigned long)c->im, (unsigned long)c->entry,
		      g_fn_ok[FN_INPUT_MODE]);
		return;
	}
	int on = arg && !strcmp(arg, "on");
	int before = c->profile ? rb(c->profile + PROF_PAD) : -1;
	uint8_t one = 1, zero = 0;
	if (on) /* never fall back to KBM on keyboard/mouse activity (the harness types keys) */
		memcpy((void *)G.allow_kbm, &zero, 1);
	else
		memcpy((void *)G.allow_kbm, &one, 1);
	FN(FN_INPUT_MODE, inmode_fn)(c->im, c->entry, on);
	logf_("DONE seq=%u padmode %s: profile pad byte %d -> %d; allow->pad=%u allow->kbm=%u", seq, on ? "on" : "off", before,
	      c->profile ? rb(c->profile + PROF_PAD) : -1, rb(G.allow_pad), rb(G.allow_kbm));
}

static uintptr_t atk_valid(uintptr_t rec)
{
	uintptr_t cat = rq(G.atkcat_pp), base = rq(cat + CAT_RECS);
	uint32_t n = r32(cat + CAT_COUNT);
	if (!rec || !base || rec < base || rec >= base + (uintptr_t)n * DBREC_SIZE || (rec - base) % DBREC_SIZE)
		return 0;
	return rec;
}

static uintptr_t atk_pick(const char *how, char *why, size_t cap)
{
	char name[256];
	if (how && strcmp(how, "auto") && strcmp(how, "last")) {
		uintptr_t r = g_fn_ok[FN_ATK_BYNAME] ? atk_valid(FN(FN_ATK_BYNAME, byname_fn)(how)) : 0;
		snprintf(why, cap, "by name '%s'%s", how, r ? "" : " (not found)");
		return r;
	}
	int recoil = !strncmp(g_last_player_attack_name, "PC_Recoil", 9); /* P8: a recoil deals 1 damage, no reaction */
	if (atk_valid(g_last_player_attack) && (!recoil || (how && !strcmp(how, "last")))) {
		snprintf(why, cap, "last player attack '%s'", g_last_player_attack_name);
		return g_last_player_attack;
	}
	if (how && !strcmp(how, "last")) {
		snprintf(why, cap, "no player attack seen yet");
		return 0;
	}
	static uintptr_t cached;
	static char cached_name[128];
	if (atk_valid(cached)) {
		snprintf(why, cap, "auto: the cached best name match '%s'", cached_name);
		return cached;
	}
	uintptr_t cat = rq(G.atkcat_pp), base = rq(cat + CAT_RECS), best = 0;
	uint32_t n = r32(cat + CAT_COUNT);
	int best_score = 0;
	for (uint32_t i = 0; base && i < n && i < 200000; i++) {
		uintptr_t rec = base + (uintptr_t)i * DBREC_SIZE;
		rec_name(rec, name, sizeof name);
		int sc = (strcasestr(name, "player") ? 4 : 0) + (strcasestr(name, "talion") ? 4 : 0) +
			 (strcasestr(name, "light") ? 2 : 0) + (strcasestr(name, "sword") ? 1 : 0) + (strcasestr(name, "strike") ? 1 : 0);
		if (strcasestr(name, "counter") || strcasestr(name, "execut") || strcasestr(name, "drain") ||
		    strcasestr(name, "brand") || strcasestr(name, "sync") || strcasestr(name, "aoe"))
			sc -= 3;
		if (sc > best_score) {
			best_score = sc;
			best = rec;
		}
	}
	snprintf(why, cap, "auto: best name match '%s' (score %d)", best ? rec_name(best, name, sizeof name) : "", best_score);
	if (best_score < 4)
		return 0;
	cached = best;
	snprintf(cached_name, sizeof cached_name, "%s", rec_name(best, name, sizeof name));
	return best;
}

static int pick_get(struct orc *o); /* P7: the picked orc, still alive (P9: dealhit's first choice) */

static void w_dealhit(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	const char *how = NULL;
	int kill = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "kill"))
			kill = 1;
		else
			how = argv[i];
	}
	if (!g_fn_ok[FN_DEALHIT] || !c->scombat || !rq(c->scombat + C_PLAYER)) {
		logf_("FAIL seq=%u dealhit: fn=%d player server Combat=%#lx", seq, g_fn_ok[FN_DEALHIT], (unsigned long)c->scombat);
		return;
	}
	struct orc o[65];
	int n = orcs_collect(c, o, 64), pick = -1;
	const char *twhy = "the nearest live AI character";
	if (pick_get(&o[64]) && o[64].ai && o[64].dist < 4000.0f) { /* P9: the picked (or spawned) orc first */
		o[0] = o[64];
		pick = 0;
		twhy = "the picked orc";
	}
	for (int i = 0; i < n && pick < 0; i++)
		if (!o[i].dead && o[i].ai && o[i].npc && o[i].dist < 4000.0f)
			pick = i;
	if (pick < 0) {
		logf_("FAIL seq=%u dealhit: no live AI character within 40 m (%d candidates, %d rejected by the vtable check; "
		      "run `orcs` to see why)", seq, n, g_orc_rejected);
		return;
	}
	char why[300], an[128], rn[128];
	uintptr_t atk = atk_pick(how, why, sizeof why);
	if (!atk) {
		logf_("FAIL seq=%u dealhit: no attack record (%s); pass a name from attacks.tsv", seq, why);
		return;
	}
	uintptr_t pc = c->scombat;
	float pp[3], dir[3], saved[3];
	obj_pos(rq(pc + C_HOBJ), pp);
	dir[0] = o[pick].pos[0] - pp[0];
	dir[1] = 0.0f;
	dir[2] = o[pick].pos[2] - pp[2];
	float len = sqrtf(dir[0] * dir[0] + dir[2] * dir[2]);
	if (len < 1e-3f) {
		dir[0] = 0;
		dir[2] = 1;
		len = 1;
	}
	dir[0] /= len;
	dir[2] /= len;
	ltvec hit = { o[pick].pos[0], o[pick].pos[1] + 120.0f, o[pick].pos[2] };
	memcpy(saved, (void *)(pc + C_HITDIR), sizeof saved);
	uint8_t was27a = *(volatile uint8_t *)(pc + C_HITONCE);
	memcpy((void *)(pc + C_HITDIR), dir, sizeof dir);
	*(volatile uint8_t *)(pc + C_HITONCE) = 0;
	logf_("DEALHIT seq=%u target c=%#lx hobj=%#lx '%s' (%s) dist=%.1f dead_before=%d attack=%#lx '%s' (%s) kill=%d "
	      "pos=%.1f,%.1f,%.1f dir=%.3f,0,%.3f t=%.6f", seq, (unsigned long)o[pick].c, (unsigned long)o[pick].hobj,
	      rec_name(rq(o[pick].c + C_CHARREC), rn, sizeof rn), twhy, o[pick].dist, o[pick].dead, (unsigned long)atk,
	      rec_name(atk, an, sizeof an), why, kill, hit.x, hit.y, hit.z, dir[0], dir[2], mono());
	flush_log();
	FN(FN_DEALHIT, dealhit_fn)(hit, pc, o[pick].hobj, atk, kill);
	g_last_hit_c = o[pick].c;
	g_last_hit_t = mono();
	memcpy((void *)(pc + C_HITDIR), saved, sizeof saved);
	*(volatile uint8_t *)(pc + C_HITONCE) = was27a;
	struct orc after;
	orc_get(o[pick].c, pp, &after);
	logf_("DONE seq=%u dealhit: returned; target dead_after=%d (see the HITEV/DEATH lines)", seq, after.dead);
}

static const char *const g_head_subs[] = { "head", "hair", "hood", "helm", "face", "eye", "mouth", "teeth", "beard",
					   "brow", "lash" };
/* the model components of re-firstperson.md 3.1 plus plain names: tried through GetPiece even when the piece-name
 * enumeration (IEngineModel slot 14) gives nothing useful */
static const char *const g_piece_candidates[] = { "SMC_HeadModel", "SMC_HeadSimpleModel", "SMC_HairModel",
						  "SMC_HelmetModel", "SMC_HeadScarModel", "SMC_AccessoryModel1",
						  "SMC_AccessoryModel2", "Head", "head", "Hair", "Hood", "Helmet", "Face" };

static void w_hidehead(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	static char names[128][64];
	uintptr_t obj = char_obj(c), m = model_iface();
	int n = piece_names(c, names, 128);
	if (n < 0 || !g_fn_ok[FN_GETPIECE] || !g_fn_ok[FN_HIDEPIECE]) {
		logf_("FAIL seq=%u hidehead: pieces unavailable", seq);
		return;
	}
	int hid = 0, tried_extra = 0;
	for (int i = 0; i < n && g_hidden_n < 64; i++) {
		char low[64];
		for (int k = 0; k < 64; k++)
			low[k] = (char)tolower((unsigned char)names[i][k]);
		int match = 0;
		if (argc > 1) {
			for (int a = 1; a < argc; a++)
				match |= !strcmp(argv[a], "*") || strcasestr(low, argv[a]) != NULL;
		} else {
			for (size_t s = 0; s < sizeof g_head_subs / sizeof g_head_subs[0]; s++)
				match |= strstr(low, g_head_subs[s]) != NULL;
		}
		if (!match || !names[i][0])
			continue;
		uint32_t piece = 0xffffffff;
		if (FN(FN_GETPIECE, getpiece_fn)(m, obj, names[i], &piece) & 0xff)
			continue;
		uint64_t r = FN(FN_HIDEPIECE, hidepiece_fn)(m, obj, piece, 1);
		g_hidden[g_hidden_n].piece = piece;
		memcpy(g_hidden[g_hidden_n].name, names[i], 63);
		g_hidden[g_hidden_n].name[63] = 0;
		g_hidden_n++;
		hid++;
		logf_("HIDE piece %u '%s' result=%u", piece, names[i], (unsigned)(r & 0xff));
	}
	/* the known component names through GetPiece (only with no explicit substrings: slot 14 may not be the piece-name
	 * enumerator), plus every name given as an argument, taken literally */
	int ncand = argc > 1 ? 0 : (int)(sizeof g_piece_candidates / sizeof g_piece_candidates[0]);
	for (int k = 0; k < ncand + argc - 1 && g_hidden_n < 64; k++) {
		const char *nm = k < ncand ? g_piece_candidates[k] : argv[1 + k - ncand];
		if (!strcmp(nm, "*"))
			continue;
		uint32_t piece = 0xffffffff;
		if (FN(FN_GETPIECE, getpiece_fn)(m, obj, nm, &piece) & 0xff)
			continue;
		int dup = 0;
		for (int j = 0; j < g_hidden_n; j++)
			dup |= g_hidden[j].piece == piece;
		if (dup)
			continue;
		FN(FN_HIDEPIECE, hidepiece_fn)(m, obj, piece, 1);
		g_hidden[g_hidden_n].piece = piece;
		memcpy(g_hidden[g_hidden_n].name, nm, strlen(nm) < 63 ? strlen(nm) + 1 : 63);
		g_hidden[g_hidden_n].name[63] = 0;
		g_hidden_n++;
		hid++;
		tried_extra++;
		logf_("HIDE piece %u '%s' (by name, not in the enumeration)", piece, nm);
	}
	g_hidden_obj = obj;
	logf_("DONE seq=%u hidehead: %d pieces hidden (%d of the %d enumerated, %d found by name)", seq, hid, hid - tried_extra,
	      n, tried_extra);
}

static void w_showhead(const struct ctx *c, unsigned seq)
{
	uintptr_t m = model_iface(), now = char_obj(c);
	if (g_hidden_n && now != g_hidden_obj) { /* a reload/respawn/model rebuild: the old object may be gone */
		logf_("DONE seq=%u showhead: the player object changed (%#lx -> %#lx), the %d hidden pieces are forgotten "
		      "unshown (a rebuilt model starts unhidden)", seq, (unsigned long)g_hidden_obj, (unsigned long)now, g_hidden_n);
		g_hidden_n = 0;
		return;
	}
	for (int i = 0; i < g_hidden_n; i++)
		if (g_fn_ok[FN_HIDEPIECE])
			FN(FN_HIDEPIECE, hidepiece_fn)(m, g_hidden_obj, g_hidden[i].piece, 0);
	logf_("DONE seq=%u showhead: %d pieces shown again", seq, g_hidden_n);
	g_hidden_n = 0;
}

static void w_fpcam(unsigned seq, int argc, char **argv)
{
	if (argc < 2 || strcmp(argv[1], "on")) {
		g_fp.on = 0;
		logf_("DONE seq=%u fpcam off", seq);
		return;
	}
	g_fp.have_pitch = 0;
	for (int i = 2; i < argc; i++) {
		if (!strncmp(argv[i], "fwd=", 4))
			g_fp.fwd = (float)num(argv[i] + 4, 10);
		else if (!strncmp(argv[i], "up=", 3))
			g_fp.up = (float)num(argv[i] + 3, 8);
		else if (!strncmp(argv[i], "pitch=", 6)) {
			g_fp.pitch_deg = (float)num(argv[i] + 6, 0);
			g_fp.have_pitch = 1;
		}
	}
	if (!g_pose.have_node)
		logf_("WARN fpcam: no head node yet (run `nodes` first): the camera stays the game's");
	g_fp.on = g_fn_ok[FN_BODYSETCAM] && g_fn_ok[FN_CAMRESET] && g_fn_ok[FN_NODEXF];
	logf_("DONE seq=%u fpcam %s fwd=%.1f up=%.1f pitch=%s%.1f node=%s (hook %s)", seq, g_fp.on ? "on" : "UNAVAILABLE",
	      g_fp.fwd, g_fp.up, g_fp.have_pitch ? "" : "game,", g_fp.pitch_deg, g_pose.node_name,
	      g_h[HK_CAMPOST_A].installed || g_h[HK_CAMPOST_B].installed ? "on" : "NOT installed");
}

static void w_near(unsigned seq, const char *arg)
{
	if (arg && !strcmp(arg, "off")) {
		if (g_near_saved)
			memcpy((void *)G.near_ovr, &g_near_old, 4);
		g_near_saved = 0;
		logf_("DONE seq=%u near off: %g", seq, rf(G.near_ovr));
		return;
	}
	float v = (float)num(arg, -1);
	if (v < 0 || v > 100) {
		logf_("FAIL seq=%u near: want 0..100 cm or off", seq);
		return;
	}
	if (!g_near_saved) {
		g_near_old = rf(G.near_ovr);
		g_near_saved = 1;
	}
	memcpy((void *)G.near_ovr, &v, 4);
	logf_("DONE seq=%u near %g (was %g)", seq, v, g_near_old);
}

static void w_autocounter(unsigned seq, int argc, char **argv)
{
	if (argc < 2 || !strcmp(argv[1], "off")) {
		g_ac.on = 0;
		logf_("DONE seq=%u autocounter off (%u presses)", seq, g_ac.presses);
		return;
	}
	int id = parse_cmd_id(argv[1]);
	if (id < 0) {
		logf_("FAIL seq=%u autocounter: unknown command '%s'", seq, argv[1]);
		return;
	}
	g_ac.cmd = id;
	g_ac.frames = argc > 2 ? (int)num(argv[2], 3) : 3;
	g_ac.on = 1;
	logf_("DONE seq=%u autocounter on: %s (%d) for %d frames when a parry window opens", seq, g_cmd_names[id], id, g_ac.frames);
}

/* ------------------------------------------------------------------------------------------------------------ */
/* The console executor and PlayerMovement (re-m1m2.md G22 and section 3; runtime checks X1 and X2)               */

typedef int (*runstring_fn)(void *eng, const char *s);
typedef uintptr_t (*find_fn)(uintptr_t repo, const char *name);
typedef void (*pmroot_fn)(void *pm, void *xf);

#define EXEC_MAX 400   /* characters after the expansion (a cfg line is at most 511) */
#define EXEC_TOKENS 48 /* the tokenizer takes at most 64 */
#define EXEC_NAMES 8

/* the root watch: the player root on the 3 ticks after an exec or a pmmove */
static struct {
	unsigned seq;
	int left, n, have_expect;
	char what[16];
	float p0[3], expect[3];
	uintptr_t pm;
} g_rw;

/* pmmove's handshake with the P_PMROOT hook (the hook may run on another thread) */
static _Atomic uintptr_t g_pm_armed;    /* the PlayerMovement the extra move was written to (0 = none) */
static _Atomic uint64_t g_pm_consumed;  /* the vf_11 call number that consumed it (0 = not yet) */
static uint64_t g_pm_armed_call;        /* the vf_11 call count when it was written */
static float g_pm_xf[2][3];             /* vf_11's transform position before / after the consuming call */
static _Atomic uintptr_t g_pm_last;     /* the last object vf_11 ran on */
static _Atomic uint64_t g_pm_offthread; /* vf_11 calls on another thread than the probe tick */
static _Atomic int g_game_tid;          /* the thread of the probe tick (P_INPUT) */

static struct {
	int on;
	unsigned seq;
	double until, secs;
	uint64_t last, frames, calls, hist[4], minc, maxc, off0;
	uintptr_t pm_seen;
	int pm_changes;
	char first[80];
} g_pmc;

static void rw_arm(unsigned seq, const char *what, const struct ctx *c, const float *expect, uintptr_t pm)
{
	uintptr_t o = char_obj(c);
	if (g_rw.left > 0 && g_rw.pm) /* a pmmove watch cut short: the hook stops looking */
		atomic_store_explicit(&g_pm_armed, 0, memory_order_release);
	memset(&g_rw, 0, sizeof g_rw);
	if (!o)
		return;
	g_rw.seq = seq;
	g_rw.left = 3;
	snprintf(g_rw.what, sizeof g_rw.what, "%s", what);
	obj_pos(o, g_rw.p0);
	if (expect) {
		memcpy(g_rw.expect, expect, sizeof g_rw.expect);
		g_rw.have_expect = 1;
	}
	g_rw.pm = pm;
}

static void rw_tick(const struct ctx *c)
{
	if (g_rw.left <= 0)
		return;
	uintptr_t o = char_obj(c);
	float p[3] = { 0, 0, 0 }, d[3];
	if (o)
		obj_pos(o, p);
	for (int k = 0; k < 3; k++)
		d[k] = p[k] - g_rw.p0[k];
	char ex[300] = "";
	size_t l = 0;
	if (g_rw.have_expect)
		l += (size_t)snprintf(ex, sizeof ex, " expect=(%.2f %.2f %.2f)", g_rw.expect[0], g_rw.expect[1], g_rw.expect[2]);
	uint64_t cons = atomic_load_explicit(&g_pm_consumed, memory_order_acquire);
	if (g_rw.pm)
		snprintf(ex + l, sizeof ex - l, " pending=%u vf11_calls_since=%llu consumed_by=%s%llu", rb(g_rw.pm + PM_PENDING),
			 (unsigned long long)(atomic_load(&g_h[HK_PMROOT].calls) - g_pm_armed_call), cons ? "call+" : "-",
			 (unsigned long long)(cons ? cons - g_pm_armed_call : 0));
	g_rw.n++;
	logf_("ROOT seq=%u %s tick+%d pos=(%.2f %.2f %.2f) d=(%.2f %.2f %.2f) |d|=%.2f%s t=%.6f", g_rw.seq, g_rw.what, g_rw.n,
	      p[0], p[1], p[2], d[0], d[1], d[2], sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), ex, mono());
	if (--g_rw.left > 0 || !g_rw.pm)
		return;
	if (cons)
		logf_("PMMOVE seq=%u consumed by vf_11 call +%llu: its transform (%.2f %.2f %.2f) -> (%.2f %.2f %.2f)", g_rw.seq,
		      (unsigned long long)(cons - g_pm_armed_call), g_pm_xf[0][0], g_pm_xf[0][1], g_pm_xf[0][2], g_pm_xf[1][0],
		      g_pm_xf[1][1], g_pm_xf[1][2]);
	else
		logf_("PMMOVE seq=%u not consumed through the hooked slot within 3 ticks (pending=%u, hook P_PMROOT %s)", g_rw.seq,
		      rb(g_rw.pm + PM_PENDING), g_h[HK_PMROOT].installed ? "on" : "NOT installed");
	atomic_store_explicit(&g_pm_armed, 0, memory_order_release);
}

/* P10: the transform vf_11 is updating, for P_ROOTDELTA inside it (the call site cannot see vf_11's rbp/rbx) */
static __thread void *t_vf11_pm, *t_vf11_xf;

/* the P_PMROOT hook: PlayerMovement::vf_11(pm, LTRigidTransform *xf {pos[3], rot[4]}) */
static void p_pmroot(void *pm, void *xf)
{
	uint64_t n = atomic_fetch_add_explicit(&g_h[HK_PMROOT].calls, 1, memory_order_relaxed) + 1;
	first_call(HK_PMROOT, n);
	int gt = atomic_load_explicit(&g_game_tid, memory_order_relaxed);
	if (gt && my_tid() != gt)
		atomic_fetch_add_explicit(&g_pm_offthread, 1, memory_order_relaxed);
	atomic_store_explicit(&g_pm_last, (uintptr_t)pm, memory_order_relaxed);
	uintptr_t armed = atomic_load_explicit(&g_pm_armed, memory_order_acquire);
	int watch = armed && armed == (uintptr_t)pm && !atomic_load_explicit(&g_pm_consumed, memory_order_relaxed) &&
		    rb(armed + PM_PENDING);
	float before[3];
	if (watch)
		rvec((uintptr_t)xf, before, 3);
	t_vf11_pm = pm;
	t_vf11_xf = xf;
	((pmroot_fn)g_h[HK_PMROOT].orig)(pm, xf);
	t_vf11_pm = t_vf11_xf = NULL;
	if (watch && !rb(armed + PM_PENDING)) {
		memcpy(g_pm_xf[0], before, sizeof before);
		rvec((uintptr_t)xf, g_pm_xf[1], 3);
		atomic_store_explicit(&g_pm_consumed, n, memory_order_release);
	}
}

static void pmc_tick(const struct ctx *c)
{
	if (!g_pmc.on)
		return;
	uint64_t tot = atomic_load_explicit(&g_h[HK_PMROOT].calls, memory_order_relaxed), k = tot - g_pmc.last;
	g_pmc.last = tot;
	g_pmc.frames++;
	g_pmc.calls += k;
	g_pmc.hist[k > 3 ? 3 : k]++;
	if (g_pmc.frames == 1 || k < g_pmc.minc)
		g_pmc.minc = k;
	if (k > g_pmc.maxc)
		g_pmc.maxc = k;
	size_t l = strlen(g_pmc.first);
	if (g_pmc.frames <= 24 && l + 6 < sizeof g_pmc.first)
		snprintf(g_pmc.first + l, sizeof g_pmc.first - l, "%s%llu", l ? "," : "", (unsigned long long)k);
	uintptr_t pm = atomic_load_explicit(&g_pm_last, memory_order_relaxed);
	if (pm && pm != g_pmc.pm_seen) {
		g_pmc.pm_changes += g_pmc.pm_seen != 0;
		g_pmc.pm_seen = pm;
	}
	if (mono() < g_pmc.until)
		return;
	g_pmc.on = 0;
	uintptr_t ppm = c->player ? rq(c->player + P_PM) : 0;
	logf_("PMCOUNT seq=%u %.1f s: ticks=%llu vf11_calls=%llu per_tick=%.3f min=%llu max=%llu hist 0:%llu 1:%llu 2:%llu "
	      "3+:%llu first=[%s] pm=%#lx (Player+0x2c8 %#lx: %s) pm_changes=%d offthread=%llu t=%.6f",
	      g_pmc.seq, g_pmc.secs, (unsigned long long)g_pmc.frames, (unsigned long long)g_pmc.calls,
	      g_pmc.frames ? (double)g_pmc.calls / (double)g_pmc.frames : 0.0, (unsigned long long)g_pmc.minc,
	      (unsigned long long)g_pmc.maxc, (unsigned long long)g_pmc.hist[0], (unsigned long long)g_pmc.hist[1],
	      (unsigned long long)g_pmc.hist[2], (unsigned long long)g_pmc.hist[3], g_pmc.first, (unsigned long)g_pmc.pm_seen,
	      (unsigned long)ppm, !g_pmc.pm_seen ? "no call seen" : g_pmc.pm_seen == ppm ? "the same" : "DIFFERENT", g_pmc.pm_changes,
	      (unsigned long long)(atomic_load(&g_pm_offthread) - g_pmc.off0), mono());
}

static void w_pmcount(const struct ctx *c, unsigned seq, const char *arg)
{
	if (!g_h[HK_PMROOT].installed) {
		logf_("FAIL seq=%u pmcount: the hook P_PMROOT is not installed (its HOOK line says why)", seq);
		return;
	}
	double secs = num(arg, 5);
	secs = secs < 0.1 ? 0.1 : secs > 60 ? 60 : secs;
	memset(&g_pmc, 0, sizeof g_pmc);
	g_pmc.on = 1;
	g_pmc.seq = seq;
	g_pmc.secs = secs;
	g_pmc.until = mono() + secs;
	g_pmc.last = atomic_load(&g_h[HK_PMROOT].calls);
	g_pmc.off0 = atomic_load(&g_pm_offthread);
	logf_("DONE seq=%u pmcount: PlayerMovement::vf_11 calls per tick for %.1f s (calls so far %llu, Player+0x2c8 %#lx)", seq,
	      secs, (unsigned long long)g_pmc.last, (unsigned long)(c->player ? rq(c->player + P_PM) : 0));
}

struct pmx {
	float pos[3], rot[4];
	uint8_t pending;
};
static int pmx_read(uintptr_t pm, struct pmx *x)
{
	unsigned char b[PM_PENDING + 1 - PM_XPOS];
	if (rd(pm + PM_XPOS, b, sizeof b))
		return -1;
	memcpy(x->pos, b, 12);
	memcpy(x->rot, b + (PM_XROT - PM_XPOS), 16);
	x->pending = b[PM_PENDING - PM_XPOS];
	return 0;
}

static int pmmove_do(const struct ctx *c, unsigned seq, const float d[3], const char *who);

static void w_pmmove(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	if (argc < 4 || !is_num(argv[1]) || !is_num(argv[2]) || !is_num(argv[3])) {
		logf_("FAIL seq=%u pmmove: want DX DY DZ (cm in Talion's frame: x right, y up, z forward)", seq);
		return;
	}
	float d[3] = { (float)num(argv[1], 0), (float)num(argv[2], 0), (float)num(argv[3], 0) };
	for (int k = 0; k < 3; k++)
		if (!(fabsf(d[k]) <= 200.0f)) {
			logf_("FAIL seq=%u pmmove: |%s| > 200 cm", seq, argv[1 + k]);
			return;
		}
	pmmove_do(c, seq, d, "pmmove");
}

/* the one-shot extra move D (cm, Talion's frame) through PlayerMovement +0x428/+0x444, watched for 3 ticks; 0 = written
 * (pmmove; P10: npcpush's push) */
static int pmmove_do(const struct ctx *c, unsigned seq, const float d[3], const char *who)
{
	if (!c->player) {
		logf_("FAIL seq=%u %s: no local player", seq, who);
		return -1;
	}
	uintptr_t pm = rq(c->player + P_PM), vt = pm ? rq(pm) : 0, owner = pm ? rq(pm + PM_OWNER) : 0;
	struct pmx x;
	if (!pm || vt != G.vt_pm || pmx_read(pm, &x)) {
		logf_("FAIL seq=%u %s: Player+0x2c8 = %#lx (vtable %#lx) is not a PlayerMovement (vtable %#lx)", seq, who,
		      (unsigned long)pm, (unsigned long)vt, (unsigned long)G.vt_pm);
		return -1;
	}
	logf_("PMMOVE seq=%u before pm=%#lx owner=%#lx (%s) +0x428=(%g %g %g) +0x434=(%g %g %g %g) +0x444=%u", seq,
	      (unsigned long)pm, (unsigned long)owner, owner == c->player ? "the player" : "NOT the player", x.pos[0], x.pos[1],
	      x.pos[2], x.rot[0], x.rot[1], x.rot[2], x.rot[3], x.pending);
	if (x.pending || x.pos[0] != 0 || x.pos[1] != 0 || x.pos[2] != 0 || x.rot[0] != 0 || x.rot[1] != 0 || x.rot[2] != 0 ||
	    x.rot[3] != 1.0f) {
		logf_("FAIL seq=%u %s: the extra move is not at rest (want 0 0 0 / 0 0 0 1 / 0): refusing to write (a move "
		      "still pending, or the offsets are wrong for this object)", seq, who);
		return -1;
	}
	uintptr_t o = char_obj(c);
	float q[4] = { 0, 0, 0, 1 }, ew[3];
	if (o)
		rvec(o + OBJ_ROT, q, 4);
	qrot(q, d, ew); /* the world move if the frame has no root motion of its own (re-m1m2.md 3.2 step 4) */
	rw_arm(seq, "pmmove", c, ew, pm);
	atomic_store_explicit(&g_pm_consumed, 0, memory_order_relaxed);
	g_pm_armed_call = atomic_load(&g_h[HK_PMROOT].calls);
	atomic_store_explicit(&g_pm_armed, pm, memory_order_release);
	memcpy((void *)(pm + PM_XPOS), d, 3 * sizeof *d); /* the rotation stays the identity it was checked to be */
	__atomic_store_n((uint8_t *)(pm + PM_PENDING), 1, __ATOMIC_RELEASE);
	pmx_read(pm, &x);
	logf_("PMMOVE seq=%u after +0x428=(%g %g %g) +0x434=(%g %g %g %g) +0x444=%u root_rot=(%.3f %.3f %.3f %.3f) "
	      "expect_world=(%.2f %.2f %.2f) hook=%s t=%.6f", seq, x.pos[0], x.pos[1], x.pos[2], x.rot[0], x.rot[1], x.rot[2],
	      x.rot[3], x.pending, q[0], q[1], q[2], q[3], ew[0], ew[1], ew[2], g_h[HK_PMROOT].installed ? "on" : "off", mono());
	logf_("DONE seq=%u %s (%g %g %g) cm: the root follows for 3 ticks", seq, who, d[0], d[1], d[2]);
	return 0;
}

/* console find through the repository's slot 8 (checked); *avail = 0 if the slot or the repository is not usable */
static uintptr_t con_find(const char *name, int *avail)
{
	*avail = 0;
	if (!g_ro || !g_ro_ok[RO_REPO_FIND])
		return 0;
	uintptr_t vt = rq(G.repo);
	if (vt != G.vt_repo || vt + 8 * 8 != g_ro[RO_REPO_FIND].addr || rq(vt + 8 * 8) != g_ro[RO_REPO_FIND].target)
		return 0;
	*avail = 1;
	return ((find_fn)g_ro[RO_REPO_FIND].target)(G.repo, name);
}

static void con_desc(uintptr_t prog, char *out, size_t cap)
{
	char nm[96];
	rstr(rq(prog + 8), nm, sizeof nm);
	uintptr_t vt = rq(prog);
	if (vt == G.vt_userprog) {
		snprintf(out, cap, "%#lx command '%s' handler=%#lx", (unsigned long)prog, nm, (unsigned long)rq(prog + 0x38));
		return;
	}
	unsigned char b[8] = { 0 };
	rd(prog + 0x38, b, 8);
	float f;
	int32_t i;
	memcpy(&f, b, 4);
	memcpy(&i, b, 4);
	snprintf(out, cap, "%#lx variable? vt=%#lx '%s' +0x38=[%02x %02x %02x %02x %02x %02x %02x %02x] u8=%u i32=%d f32=%g",
		 (unsigned long)prog, (unsigned long)vt, nm, b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[0], i, (double)f);
}

static int pick_pos(float out[3]); /* P7: the picked orc (pick) */

/* Feral's VFS (the Doze drive, runtime-probes.md P5 results; the mount table in FUN_0065cd10: FUN_02019310(host, doze)):
 * C:\Temp and C:\Feral\Temp = ${AppPreferences}/VFS/Temp, C:\Feral\Users\Default = ${AppPreferences}/VFS/User, with
 * ${AppPreferences} = $XDG_DATA_HOME/feral-interactive/ShadowOfMordor (boot_check: inside RUN) [med] */
#define DOZE_OUT "C:\\Temp"
static const char *app_prefs(char *out, size_t cap)
{
	const char *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
	if (x && *x)
		snprintf(out, cap, "%s/feral-interactive/ShadowOfMordor", x);
	else if (h && *h)
		snprintf(out, cap, "%s/.local/share/feral-interactive/ShadowOfMordor", h);
	else
		return NULL;
	return out;
}

/* a C: path -> the host path of its VFS mount; 0 = mapped */
static int doze_host(const char *dpath, char *out, size_t cap)
{
	static const struct { const char *doze, *host; } mnt[] = {
		{ "C:\\Feral\\Temp", "VFS/Temp" }, { "C:\\Temp", "VFS/Temp" }, { "C:\\Feral\\Users\\Default", "VFS/User" },
	};
	char ap[800];
	if (!app_prefs(ap, sizeof ap))
		return -1;
	for (size_t i = 0; i < sizeof mnt / sizeof mnt[0]; i++) {
		size_t l = strlen(mnt[i].doze), k = 0;
		for (; k < l; k++) {
			int a = (unsigned char)dpath[k] == '/' ? '\\' : (unsigned char)dpath[k];
			if (!a || tolower(a) != tolower((unsigned char)mnt[i].doze[k]))
				break;
		}
		if (k < l || (dpath[l] && dpath[l] != '\\' && dpath[l] != '/'))
			continue;
		size_t n = (size_t)snprintf(out, cap, "%s/%s", ap, mnt[i].host);
		for (const char *q = dpath + l; *q && n + 1 < cap; q++)
			out[n++] = *q == '\\' ? '/' : *q;
		out[n < cap ? n : cap - 1] = 0;
		return 0;
	}
	return -1;
}

/* copy a small file (a dump the game wrote) into OUT; the bytes copied or -1 */
static long long copy_file(const char *src, const char *dst)
{
	int in = open(src, O_RDONLY | O_CLOEXEC), out = in < 0 ? -1 : open(dst, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	long long n = 0;
	char buf[65536];
	ssize_t r;
	while (out >= 0 && n < (8 << 20) && (r = read(in, buf, sizeof buf)) > 0) {
		if (write(out, buf, (size_t)r) != r) {
			n = -1;
			break;
		}
		n += r;
	}
	if (in >= 0)
		close(in);
	if (out >= 0)
		close(out);
	return out < 0 ? -1 : n;
}

/* `\;` -> ';', @x/@y/@z[+-N] -> the root's coordinate, @ox/@oy/@oz[+-N] -> the picked orc's, @out -> the OUT directory
 * (a host path: Feral's file calls make it C:/home/..., which does not exist), @cout -> C:\Temp (lands in RUN); 0 = ok */
static int exec_expand(const struct ctx *c, const char *in, char *out, size_t cap, char *why, size_t wcap)
{
	size_t o = 0;
	float p[3], po[3];
	int havep = 0, haveo = 0;
	for (const char *s = in; *s;) {
		char tmp[64];
		const char *ins = NULL;
		size_t adv = 1;
		if ((unsigned char)*s < 0x20) {
			snprintf(why, wcap, "a control character (%#x) in the line", (unsigned char)*s);
			return -1;
		} else if (s[0] == '\\' && s[1] == ';') {
			ins = ";";
			adv = 2;
		} else if (s[0] == '@' && ((s[1] == 'x' || s[1] == 'y' || s[1] == 'z') ||
					   (s[1] == 'o' && (s[2] == 'x' || s[2] == 'y' || s[2] == 'z')))) {
			int orc = s[1] == 'o';
			char axis = orc ? s[2] : s[1];
			const char *e = s + (orc ? 3 : 2);
			double off = 0;
			if (*e == '+' || *e == '-') {
				char *end;
				off = strtod_l(e, &end, g_cloc);
				if (end == e) {
					snprintf(why, wcap, "'%.8s': want @%s%c+N or @%s%c-N", s, orc ? "o" : "", axis, orc ? "o" : "", axis);
					return -1;
				}
				e = end;
			}
			if (*e && *e != ' ' && *e != '\t' && *e != '\\' && *e != ';') {
				snprintf(why, wcap, "'%.12s': @x/@y/@z/@ox/@oy/@oz must end the token", s);
				return -1;
			}
			if (orc && !haveo) {
				if (pick_pos(po)) {
					snprintf(why, wcap, "@o%c: no picked orc (run `pick` first) or it died/left", axis);
					return -1;
				}
				haveo = 1;
			}
			if (!orc && !havep) {
				uintptr_t ob = char_obj(c);
				if (!ob) {
					snprintf(why, wcap, "@%c: no player root", s[1]);
					return -1;
				}
				obj_pos(ob, p);
				havep = 1;
			}
			locale_t old = g_cloc ? uselocale(g_cloc) : (locale_t)0; /* a '.' whatever the game's locale */
			snprintf(tmp, sizeof tmp, "%.2f", (double)(orc ? po : p)[axis - 'x'] + off);
			if (old)
				uselocale(old);
			ins = tmp;
			adv = (size_t)(e - s);
		} else if (!strncmp(s, "@cout", 5) && !isalnum((unsigned char)s[5]) && s[5] != '_') {
			ins = DOZE_OUT;
			adv = 5;
		} else if (!strncmp(s, "@out", 4) && !isalnum((unsigned char)s[4]) && s[4] != '_') {
			if (strpbrk(g_out_dir, " \t;%\"()")) {
				snprintf(why, wcap, "@out: the OUT directory has a character the console splits on (%.200s)", g_out_dir);
				return -1;
			}
			mkdir(g_out_dir, 0755);
			ins = g_out_dir;
			adv = 4;
		}
		size_t l = ins ? strlen(ins) : 1;
		if (o + l >= cap) {
			snprintf(why, wcap, "longer than %zu characters after the expansion", cap - 1);
			return -1;
		}
		memcpy(out + o, ins ? ins : s, l);
		o += l;
		s += adv;
	}
	out[o] = 0;
	return 0;
}

/* P8: exec logs the console's output of its own call (CONOUT lines, conlog's slot swap; defined further down) */
static int con_install(void);
static void con_remove(void);
static int g_conlog;
static _Atomic unsigned g_con_lines;

static void w_exec(const struct ctx *c, unsigned seq, const char *rest)
{
	char line[EXEC_MAX + 1], why[300];
	while (*rest == ' ' || *rest == '\t')
		rest++;
	if (!*rest) {
		logf_("FAIL seq=%u exec: no command line", seq);
		return;
	}
	if (exec_expand(c, rest, line, sizeof line, why, sizeof why)) {
		logf_("FAIL seq=%u exec: %s", seq, why);
		return;
	}
	size_t n = strlen(line);
	while (n && (line[n - 1] == ' ' || line[n - 1] == '\t'))
		line[--n] = 0;
	int ntok = 0;
	for (const char *s = line; *s;) {
		s += strspn(s, " \t;");
		if (*s)
			ntok++;
		s += strcspn(s, " \t;");
	}
	if (ntok > EXEC_TOKENS) {
		logf_("FAIL seq=%u exec: %d tokens, at most %d", seq, ntok, EXEC_TOKENS);
		return;
	}
	/* the slot, before every call: the site's bytes, then the live engine's vtable */
	if (!g_ro || !g_ro_ok[RO_EXEC]) {
		logf_("FAIL seq=%u exec: the executor site G22_EXEC did not check out at start", seq);
		return;
	}
	if (check_site(&g_ro[RO_RUNSTRING], why, sizeof why) != 0) {
		logf_("FAIL seq=%u exec: G22_RUNSTRING: %s", seq, why);
		return;
	}
	uintptr_t eng = rq(G.eng_pp);
	if (!eng) {
		logf_("FAIL seq=%u exec: no engine (*%#lx is null)", seq, (unsigned long)G.eng_pp);
		return;
	}
	uintptr_t vt = rq(eng), fn = vt ? rq(vt + 32 * 8) : 0;
	if (!vt || vt + 32 * 8 != g_ro[RO_RUNSTRING].addr || fn != g_ro[RO_RUNSTRING].target) {
		logf_("FAIL seq=%u exec: the engine %#lx has vtable %#lx, slot 32 holds %#lx; want the checked slot %#lx -> %#lx", seq,
		      (unsigned long)eng, (unsigned long)vt, (unsigned long)fn, (unsigned long)g_ro[RO_RUNSTRING].addr,
		      (unsigned long)g_ro[RO_RUNSTRING].target);
		return;
	}
	int server = g_ro_ok[RO_SERVERGATE] ? rq(rq(G.srv_pp) + 0xc58) != 0 : -1;
	/* every command's name through the console find (an unknown name is silently made a pending variable) */
	struct { char name[96]; uintptr_t prog; } nm[EXEC_NAMES];
	int nn = 0, avail = 0, known = 0;
	for (const char *s = line; *s && nn < EXEC_NAMES;) {
		s += strspn(s, " \t;");
		size_t l = strcspn(s, " \t;");
		if (l) {
			snprintf(nm[nn].name, sizeof nm[nn].name, "%.*s", (int)(l < 95 ? l : 95), s);
			nm[nn].prog = con_find(nm[nn].name, &avail);
			char d[300] = "";
			if (nm[nn].prog)
				con_desc(nm[nn].prog, d, sizeof d);
			known += nm[nn].prog != 0;
			logf_("EXEC seq=%u find '%s' -> %s", seq, nm[nn].name,
			      !avail ? "(find unavailable: G22_REPO_FIND or the repository vtable)" : nm[nn].prog ? d :
			      "UNKNOWN (the executor makes it a pending variable)");
			nn++;
		}
		s += l;
		s += strcspn(s, ";");
	}
	char cwd[512];
	if (!getcwd(cwd, sizeof cwd))
		snprintf(cwd, sizeof cwd, "?");
	rw_arm(seq, "exec", c, NULL, 0);
	unsigned cl0 = atomic_load(&g_con_lines);
	int capt = !g_conlog && con_install() == 0;
	double t0 = mono();
	int rc = ((runstring_fn)fn)((void *)eng, line);
	double t1 = mono();
	if (capt)
		con_remove();
	logf_("EXEC seq=%u rc=%d '%s' server=%d eng=%#lx tid=%d t=%.6f ms=%.3f cwd=%s; console output %u lines%s", seq, rc, line,
	      server, (unsigned long)eng, my_tid(), t0, (t1 - t0) * 1000.0, cwd, atomic_load(&g_con_lines) - cl0,
	      capt || g_conlog ? " (CONOUT)" : " (not captured: the repository's output slot is not as read)");
	for (int i = 0; i < nn; i++)
		if (nm[i].prog) {
			char d[300];
			con_desc(nm[i].prog, d, sizeof d);
			logf_("EXEC seq=%u after '%s' -> %s", seq, nm[i].name, d);
		}
	for (const char *s = line; *s;) {
		s += strspn(s, " \t;");
		size_t l = strcspn(s, " \t;");
		if (l > 1 && *s == '/') {
			char path[512];
			struct stat st;
			snprintf(path, sizeof path, "%.*s", (int)(l < 511 ? l : 511), s);
			if (stat(path, &st) == 0)
				logf_("EXEC seq=%u file '%s' exists, %lld bytes", seq, path, (long long)st.st_size);
			else
				logf_("EXEC seq=%u file '%s' absent right after the call (%s)", seq, path, strerror(errno));
		} else if (l > 3 && (s[0] == 'C' || s[0] == 'c') && s[1] == ':') { /* a Doze path: look at its host file */
			char dp[512], host[1024], dst[2100];
			struct stat st;
			snprintf(dp, sizeof dp, "%.*s", (int)(l < 511 ? l : 511), s);
			if (doze_host(dp, host, sizeof host)) {
				logf_("EXEC seq=%u file '%s': not under a known VFS mount (C:\\Temp, C:\\Feral\\Temp, C:\\Feral\\Users\\Default)",
				      seq, dp);
			} else if (stat(host, &st) == 0) {
				const char *bn = strrchr(host, '/') ? strrchr(host, '/') + 1 : host;
				mkdir(g_out_dir, 0755);
				snprintf(dst, sizeof dst, "%s/%s", g_out_dir, bn);
				long long cp = copy_file(host, dst);
				logf_("EXEC seq=%u file '%s' -> host '%s' exists, %lld bytes; copied to %s (%lld bytes)", seq, dp, host,
				      (long long)st.st_size, cp >= 0 ? dst : "(copy failed)", cp);
			} else {
				logf_("EXEC seq=%u file '%s' -> host '%s' absent right after the call (%s)", seq, dp, host, strerror(errno));
			}
		}
		s += l;
	}
	logf_("DONE seq=%u exec rc=%d: %d command(s), %d known to the console find", seq, rc, nn, known);
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P7: the cached player (hooks on any thread read it; written on the game thread) and small helpers              */

static struct {
	uintptr_t player, scombat, phobj, owner, inv, charobj;
	float pos[3], rot[4];
	uintptr_t wobj[32]; /* the model objects of the player's weapon items (fxlog's parent check) */
	int nwobj;
	double wobj_t;
} g_pi;
static uint64_t g_ticks_all; /* input + server ticks: deferred work waits for the next one */

__attribute__((format(printf, 3, 4))) static int cfmt(char *out, size_t cap, const char *fmt, ...)
{
	va_list ap;
	locale_t old = g_cloc ? uselocale(g_cloc) : (locale_t)0; /* a '.' whatever the game's locale */
	va_start(ap, fmt);
	int n = vsnprintf(out, cap, fmt, ap);
	va_end(ap);
	if (old)
		uselocale(old);
	return n < 0 ? 0 : (size_t)n >= cap ? (int)cap - 1 : n;
}

static float dist3(const float a[3], const float b[3])
{
	float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
	return sqrtf(dx * dx + dy * dy + dz * dz);
}

static uintptr_t inv_item(uintptr_t inv, int slot)
{
	return inv && slot >= 0 && slot < 8 ? rq(inv + INV_SLOT0 + (uintptr_t)slot * INV_SLOTSZ) : 0;
}

/* an item's model entries (0x48 bytes each): the count (at most cap), *base = the first */
static int item_entries(uintptr_t item, uintptr_t *base, int cap)
{
	uintptr_t b = item ? rq(item + IT_ENTRIES) : 0, e = item ? rq(item + IT_ENTRIES + 8) : 0;
	*base = b;
	if (!b || e < b || (e - b) % IT_ESIZE)
		return 0;
	size_t n = (size_t)(e - b) / IT_ESIZE;
	return (int)(n > (size_t)cap ? (size_t)cap : n);
}

static void p7_cache(const struct ctx *c)
{
	g_pi.player = c->player;
	g_pi.scombat = c->scombat;
	g_pi.phobj = c->scombat ? rq(c->scombat + C_HOBJ) : 0;
	g_pi.owner = c->scombat ? rq(c->scombat + C_OWNER) : 0;
	g_pi.inv = g_pi.owner ? rq(g_pi.owner + CH_INV) : 0;
	g_pi.charobj = char_obj(c);
	if (g_pi.charobj) {
		obj_pos(g_pi.charobj, g_pi.pos);
		rvec(g_pi.charobj + OBJ_ROT, g_pi.rot, 4);
	}
	double t = mono();
	if (t - g_pi.wobj_t < 1.0)
		return;
	g_pi.wobj_t = t;
	int n = 0;
	for (int s = 0; s < 8 && g_pi.inv; s++) {
		uintptr_t b, o;
		int ne = item_entries(inv_item(g_pi.inv, s), &b, 4);
		for (int k = 0; k < ne && n < 32; k++)
			if ((o = rq(b + (uintptr_t)k * IT_ESIZE + IE_OBJ)))
				g_pi.wobj[n++] = o;
	}
	g_pi.nwobj = n;
}

static int is_weapon_obj(uintptr_t o)
{
	for (int i = 0; o && i < g_pi.nwobj && i < 32; i++)
		if (g_pi.wobj[i] == o)
			return 1;
	return 0;
}

static const char *hobj_desc(uintptr_t h, char *out, size_t cap)
{
	snprintf(out, cap, h && (h == g_pi.phobj || h == g_pi.charobj) ? "%#lx (player)" : "%#lx", (unsigned long)h);
	return out;
}

static const char *combat_desc(uintptr_t c, char *out, size_t cap)
{
	char rn[96];
	if (!c || rq(c) != G.vt_combat)
		snprintf(out, cap, "%#lx", (unsigned long)c);
	else if (rq(c + C_PLAYER))
		snprintf(out, cap, "%#lx (player)", (unsigned long)c);
	else
		snprintf(out, cap, "%#lx '%s'", (unsigned long)c, rec_name(rq(c + C_CHARREC), rn, sizeof rn));
	return out;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P8: helpers - validated model lookups, the hidden-piece mask chain, model space                                */

static int code_ptr(uintptr_t v) { return v >= G.text_lo && v < G.text_hi; }

/* FUN_008f3c20(obj) (an HOBJECT's CSimpleModel) only when the chain it walks reads: *(obj+0x38), its vtable, and the
 * slot it calls (+0x20) inside the code segment; 0 = no model */
static uintptr_t obj_model(uintptr_t obj)
{
	uintptr_t p = obj ? rq(obj + 0x38) : 0, vt = p ? rq(p) : 0;
	if (!vt || !code_ptr(rq(vt + 0x20)) || !g_fn_ok[FN_MODELOF])
		return 0;
	return FN(FN_MODELOF, modelof_fn)(obj);
}

/* the render node of a CSimpleModel (re-resolution.md 2.1): *(*(sm+0xb0)+0x50) + 0x10; 0 unless the renderable and the
 * handle have their known vtables */
static uintptr_t sm_rnode(uintptr_t sm)
{
	uintptr_t rend = sm ? rq(sm + SM_REND) : 0, hnd = rend ? rq(rend + REND_HANDLE) : 0;
	if (!rend || rq(rend) != G.vt_smrend || !hnd || rq(hnd) != G.vt_rhandle)
		return 0;
	return hnd + HANDLE_NODE;
}

/* the mask along the chain: the model's game-side mask (+0x6c), the renderable's copy (+0x44) and the render node's
 * (+0x130, written on the render thread), read only; did a SetPieceHidden reach the node (re-resolution.md 2.4)? */
static const char *rmask_desc(uintptr_t sm, char *out, size_t cap)
{
	uint32_t m[2] = { 0, 0 }, rm[2] = { 0, 0 }, nm[2] = { 0, 0 };
	uintptr_t rend = sm ? rq(sm + SM_REND) : 0, hnd = rend ? rq(rend + REND_HANDLE) : 0, node = sm_rnode(sm);
	rd(sm + SM_MASK, m, 8);
	if (rend)
		rd(rend + REND_MASK, rm, 8);
	if (node)
		rd(node + RNODE_MASK, nm, 8);
	cfmt(out, cap, "model %#lx%s mask %08x:%08x dirty %u; renderable %#lx%s dirty %u mask %08x:%08x; handle %#lx%s node "
	     "%#lx mask %08x:%08x skip %u", (unsigned long)sm, sm && rq(sm) == G.vt_sm ? "" : " (vtable?)", m[1], m[0],
	     sm ? rb(sm + SM_DIRTY) : 0, (unsigned long)rend, rend && rq(rend) == G.vt_smrend ? "" : " (vtable?)",
	     rend ? rb(rend + REND_DIRTY) : 0, rm[1], rm[0], (unsigned long)hnd, hnd && rq(hnd) == G.vt_rhandle ? "" :
	     " (vtable?)", (unsigned long)node, nm[1], nm[0], node ? rb(node + RNODE_SKIP) : 0);
	return out;
}

/* world <-> Talion's model space (his object transform: +X right, +Y up, +Z forward, cm; re-arms-ik.md 2.2) */
static void to_model(const float o[3], const float q[4], const float w[3], float out[3])
{
	float d[3] = { w[0] - o[0], w[1] - o[1], w[2] - o[2] }, qc[4] = { -q[0], -q[1], -q[2], q[3] };
	qrot(qc, d, out);
}

/* the head node for trav and surf: the one `nodes` picked, else GetNode("Head") once per player object (P7's
 * head_y was 0 because `nodes` had not run) */
static int head_node(uintptr_t obj, uint32_t *node)
{
	static uintptr_t cobj;
	static uint32_t cnode;
	uint32_t n = 0xffffffffu;
	if (g_pose.have_node) {
		*node = g_pose.node;
		return 0;
	}
	if (obj && obj == cobj) {
		*node = cnode;
		return 0;
	}
	if (!obj || !g_fn_ok[FN_GETNODE] || FN(FN_GETNODE, getnode_fn)(model_iface(), obj, "Head", &n) & 0xff)
		return -1;
	cobj = obj;
	cnode = n;
	*node = n;
	return 0;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P7: the game database, read without calling the game (re-combat.md 8: FUN_0280a0b0, FUN_0280a2d0, FUN_026bfe40, */
/* FUN_00a09d60). A record's attributes are 16-byte entries at *(rec+8), their count the byte rec+0; rec+1 bit 0 =   */
/* sorted by the u32 hash at +0, else by +4 with the entry's own index at +9; +0xa type (1 bool, 2 float, 4 string,  */
/* 0xa record), +0xc count, +0xe value slot (8-byte slots at *(rec+0x10); a bool: slot >> 5, bit & 31; a list's       */
/* elements in the following slots [med]). A schema holder points at 0x30-byte entries: +0 the name, +8 the hash.    */

struct dbv { int type, count; uint64_t v[8]; };

static int db_entry(uintptr_t rec, uint32_t hash, unsigned char out[16])
{
	unsigned char hdr[2], ents[256 * 16];
	uintptr_t arr = rec ? rq(rec + 8) : 0;
	if (!rec || !hash || !arr || rd(rec, hdr, 2) || !hdr[0] || rd(arr, ents, (size_t)hdr[0] * 16))
		return -1;
	int n = hdr[0], keyoff = (hdr[1] & 1) ? 0 : 4, lo = 0, hi = n - 1;
	while (lo <= hi) {
		int mid = (lo + hi) >> 1;
		uint32_t h;
		memcpy(&h, ents + (size_t)mid * 16 + (size_t)keyoff, 4);
		if (h == hash) {
			int idx = keyoff ? ents[(size_t)mid * 16 + 9] : mid;
			if (idx >= n)
				return -1;
			memcpy(out, ents + (size_t)idx * 16, 16);
			return 0;
		}
		if (hash < h)
			hi = mid - 1;
		else
			lo = mid + 1;
	}
	return -1;
}

static int db_get(uintptr_t rec, uint32_t hash, struct dbv *v)
{
	unsigned char e[16];
	uint16_t cnt, vi;
	memset(v, 0, sizeof *v);
	if (db_entry(rec, hash, e))
		return -1;
	memcpy(&cnt, e + 0xc, 2);
	memcpy(&vi, e + 0xe, 2);
	v->type = e[0xa] & 0x3f;
	v->count = cnt;
	uintptr_t vals = rq(rec + 0x10);
	if (!cnt || !vals)
		return -1;
	if (v->type == 1) {
		v->v[0] = (r32(vals + (uintptr_t)(vi >> 5) * 8) >> (vi & 31)) & 1u;
		return 0;
	}
	return rd(vals + (uintptr_t)vi * 8, v->v, (size_t)(cnt < 8 ? cnt : 8) * 8) ? -1 : 0;
}

static const char *sch_name(uintptr_t holder, int i, char *out, size_t cap)
{
	uintptr_t a = rq(holder);
	out[0] = 0;
	if (a)
		rstr(rq(a + 0x30u * (unsigned)i), out, cap);
	return out;
}

static int ident_ok(const char *s)
{
	if (!s[0])
		return 0;
	for (; *s; s++)
		if (!isalnum((unsigned char)*s) && *s != '_')
			return 0;
	return 1;
}

/* the index of the schema attribute NAME (cached per schema array; the scan stops at the first entry without a name) */
#define SCH_MAX 160
static int sch_find(uintptr_t holder, const char *name)
{
	static struct { uintptr_t holder, arr; const char *name; int i; } cache[48];
	static int n;
	uintptr_t a = rq(holder);
	for (int k = 0; k < n; k++)
		if (cache[k].holder == holder && cache[k].arr == a && cache[k].name == name)
			return cache[k].i;
	int found = -1;
	char nm[96];
	for (int i = 0; a && i < SCH_MAX && found < 0; i++) {
		if (!ident_ok(sch_name(holder, i, nm, sizeof nm)))
			break;
		if (!strcmp(nm, name))
			found = i;
	}
	if (a && n < 48) {
		cache[n].holder = holder, cache[n].arr = a, cache[n].name = name, cache[n].i = found;
		n++;
	}
	return found;
}

static uint32_t sch_hash(uintptr_t holder, int i)
{
	uintptr_t a = rq(holder);
	return a && i >= 0 ? r32(a + 0x30u * (unsigned)i + 8) : 0;
}

static int db_field(uintptr_t rec, uintptr_t holder, const char *name, struct dbv *v)
{
	uint32_t h = rec ? sch_hash(holder, sch_find(holder, name)) : 0;
	memset(v, 0, sizeof *v);
	return h ? db_get(rec, h, v) : -1;
}

static uintptr_t db_recf(uintptr_t rec, uintptr_t holder, const char *name)
{
	struct dbv v;
	return db_field(rec, holder, name, &v) == 0 && v.type == 0xa ? (uintptr_t)v.v[0] : 0;
}

static void db_fmt(const struct dbv *v, char *out, size_t cap)
{
	char b[128];
	int k = v->count < 8 ? v->count : 8, o = 0;
	out[0] = 0;
	for (int i = 0; i < k && (size_t)o + 8 < cap; i++) {
		const char *sep = i ? "," : "";
		float f;
		uint32_t u = (uint32_t)v->v[i];
		switch (v->type) {
		case 0xa:
			if (rec_name((uintptr_t)v->v[i], b, sizeof b)[0])
				o += cfmt(out + o, cap - (size_t)o, "%s'%s'", sep, b);
			else
				o += cfmt(out + o, cap - (size_t)o, "%s%#llx", sep, (unsigned long long)v->v[i]);
			break;
		case 4:
			o += cfmt(out + o, cap - (size_t)o, "%s\"%s\"", sep, rstr((uintptr_t)v->v[i], b, sizeof b));
			break;
		case 2:
			memcpy(&f, &u, 4);
			o += cfmt(out + o, cap - (size_t)o, "%s%g", sep, (double)f);
			break;
		case 1:
			o += cfmt(out + o, cap - (size_t)o, "%u", (unsigned)v->v[0]);
			i = k;
			break;
		default:
			o += cfmt(out + o, cap - (size_t)o, "%s%#llx", sep, (unsigned long long)v->v[i]);
			break;
		}
	}
	if (v->count > k && (size_t)o + 16 < cap)
		cfmt(out + o, cap - (size_t)o, ",..(%d)", v->count);
	if (!out[0])
		cfmt(out, cap, "(type %d count %d)", v->type, v->count);
}

/* "-" or the formatted value of rec.NAME */
static const char *db_str(uintptr_t rec, uintptr_t holder, const char *name, char *out, size_t cap)
{
	struct dbv v;
	if (db_field(rec, holder, name, &v))
		snprintf(out, cap, "-");
	else
		db_fmt(&v, out, cap);
	return out;
}

/* the number of AI stimuli an attack registers (AttackData.RegisterStimulus entries + PersistentStimulus) */
static int atk_stims(uintptr_t rec)
{
	struct dbv v;
	uintptr_t ad = db_recf(rec, G.sch_attack, "AttackData");
	int n = ad && db_field(ad, G.sch_attackdata, "RegisterStimulus", &v) == 0 && v.type == 0xa ? v.count : 0;
	return n + (ad && db_recf(ad, G.sch_attackdata, "PersistentStimulus") ? 1 : 0);
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P7: timers (re-combat-vr.md 2.2/2.3) and the target orc (pick)                                                 */

static const char *tm_desc(uintptr_t t, char *out, size_t cap)
{
	unsigned char b[0x80];
	if (!t) {
		snprintf(out, cap, "0");
		return out;
	}
	if (rd(t, b, sizeof b)) {
		snprintf(out, cap, "%#lx (unreadable)", (unsigned long)t);
		return out;
	}
	uint64_t vt;
	uint32_t ref, fr, tot, num, den, mn, mx;
	float sc;
	double ts;
	memcpy(&vt, b, 8);
	memcpy(&ref, b + TM_REF, 4);
	memcpy(&fr, b + TM_FRAME, 4);
	memcpy(&tot, b + TM_TOTAL, 4);
	memcpy(&ts, b + TM_TOTALS, 8);
	memcpy(&num, b + TM_NUM, 4);
	memcpy(&den, b + TM_DEN, 4);
	memcpy(&sc, b + TM_SCALE, 4);
	memcpy(&mn, b + 0x2c, 4);
	memcpy(&mx, b + 0x30, 4);
	cfmt(out, cap, "%#lx%s ref=%u num/den=%u/%u scale=%g frame=%ums total=%ums (%.3f s) paused=%u min=%u max=%u",
	     (unsigned long)t, vt == G.vt_etimer ? "" : " (NOT a CEngineTimer)", ref, num, den, (double)sc, fr, tot, ts,
	     b[TM_PAUSED], mn, mx);
	return out;
}

static uintptr_t obj_deftimer(uintptr_t h)
{
	uintptr_t w = h ? rq(h + OBJ_WORLD) : 0;
	return w ? rq(w + WORLD_DEFTIMER) : 0;
}

static const char *tm_class(uintptr_t t, uintptr_t h, uintptr_t sim, uintptr_t game, uintptr_t ptimer)
{
	return !t ? "none" : t == sim ? "simulation" : t == game ? "game" : t == obj_deftimer(h) ? "default" :
	       t == ptimer ? "the-player's" : "own";
}

static void w_timers(const struct ctx *c, unsigned seq)
{
	char d[300], e[300];
	uintptr_t game = rq(rq(G.pause_pp) + 8), sim = rq(rq(G.srv_pp) + SRV_SIMTIMER), T = rq(G.timer_pp);
	uintptr_t ph = c->scombat ? rq(c->scombat + C_HOBJ) : 0, co = char_obj(c), pt = ph ? rq(ph + OBJ_TIMER) : 0;
	logf_("TIMERS manager=%#lx (vtable %#lx: %s) game=%s", (unsigned long)T, (unsigned long)rq(T),
	      rq(T) == G.vt_timer ? "CWBTimer" : "UNEXPECTED", tm_desc(game, d, sizeof d));
	logf_("TIMERS simulation=%s", tm_desc(sim, d, sizeof d));
	logf_("TIMER player hobj=%#lx timer=%s class=%s default=%#lx; client object %#lx%s timer=%s", (unsigned long)ph,
	      tm_desc(pt, d, sizeof d), tm_class(pt, ph, sim, game, 0), (unsigned long)obj_deftimer(ph), (unsigned long)co,
	      co == ph ? " (the same)" : "", tm_desc(co ? rq(co + OBJ_TIMER) : 0, e, sizeof e));
	struct orc o[64];
	int n = orcs_collect(c, o, 64), live = 0, cnt[6] = { 0 };
	static const char *const cls[6] = { "simulation", "default", "game", "the-player's", "own", "none" };
	char rn[96];
	for (int i = 0; i < n; i++) {
		if (o[i].dead)
			continue;
		uintptr_t t = rq(o[i].hobj + OBJ_TIMER);
		const char *cl = tm_class(t, o[i].hobj, sim, game, pt);
		for (int k = 0; k < 6; k++)
			cnt[k] += !strcmp(cl, cls[k]);
		if (live++ < 8)
			logf_("TIMER orc %d c=%#lx '%s' dist=%.1f timer=%s class=%s", i, (unsigned long)o[i].c,
			      rec_name(rq(o[i].c + C_CHARREC), rn, sizeof rn), o[i].dist / 100.0f, tm_desc(t, d, sizeof d), cl);
	}
	logf_("DONE seq=%u timers: %d live orcs: simulation %d, default %d, game %d, the player's %d, own %d, none %d; the "
	      "player's timer is %s", seq, live, cnt[0], cnt[1], cnt[2], cnt[3], cnt[4], cnt[5], tm_class(pt, ph, sim, game, 0));
}

static struct { uintptr_t c, hobj; char name[96]; } g_pick;
static const char *const g_pick_skip[] = { "Slave", "Human", "Prisoner", "Hostage", "Warg", "Caragor", "Ghul", "Graug",
					   "Animal", "Rat", "Bird", "Horse", "Beast", "Troll", "Spider" };

/* the picked orc, still the same live character */
static int pick_get(struct orc *o)
{
	return g_pick.c && orc_get(g_pick.c, g_pi.pos, o) && o->hobj == g_pick.hobj && !o->dead;
}

static int pick_pos(float out[3])
{
	struct orc o;
	if (!pick_get(&o))
		return -1;
	memcpy(out, o.pos, sizeof o.pos);
	return 0;
}

static void w_pick(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	struct orc o[64];
	int n = orcs_collect(c, o, 64), best = -1;
	char rn[128], an[128];
	for (int i = 0; i < n && best < 0; i++) {
		if (o[i].dead || !o[i].ai || !o[i].npc)
			continue;
		rec_name(rq(o[i].c + C_CHARREC), rn, sizeof rn);
		int ok = argc < 2;
		for (int a = 1; a < argc; a++)
			ok |= strcasestr(rn, argv[a]) != NULL;
		for (size_t k = 0; argc < 2 && k < sizeof g_pick_skip / sizeof g_pick_skip[0]; k++)
			if (strcasestr(rn, g_pick_skip[k]))
				ok = 0;
		if (ok)
			best = i;
	}
	if (best < 0) {
		memset(&g_pick, 0, sizeof g_pick);
		logf_("FAIL seq=%u pick: no live AI character matches (%d candidates, %d rejected by the vtable check)", seq, n,
		      g_orc_rejected);
		return;
	}
	g_pick.c = o[best].c;
	g_pick.hobj = o[best].hobj;
	rec_name(rq(o[best].c + C_CHARREC), g_pick.name, sizeof g_pick.name);
	logf_("PICK seq=%u c=%#lx hobj=%#lx '%s' dist=%.1f m pos=%.1f,%.1f,%.1f attack='%s' (the %d. of %d candidates)", seq,
	      (unsigned long)g_pick.c, (unsigned long)g_pick.hobj, g_pick.name, o[best].dist / 100.0f, o[best].pos[0],
	      o[best].pos[1], o[best].pos[2], rec_name(rq(o[best].c + C_ATTACK), an, sizeof an), best + 1, n);
	logf_("DONE seq=%u pick: '%s' at %.1f m", seq, g_pick.name, o[best].dist / 100.0f);
}

/* the orc for swingevent/hitstop: `last` hit (3 s), the picked one, else the nearest live AI character within 15 m */
static int target_orc(const struct ctx *c, struct orc *out, const char **why)
{
	if (g_last_hit_c && mono() - g_last_hit_t < 3.0 && orc_get(g_last_hit_c, g_pi.pos, out) && !out->dead) {
		*why = "the last dealhit target";
		return 1;
	}
	if (pick_get(out)) {
		*why = "the picked orc";
		return 1;
	}
	struct orc o[64];
	int n = orcs_collect(c, o, 64);
	for (int i = 0; i < n; i++)
		if (!o[i].dead && o[i].ai && o[i].npc && o[i].dist < 1500.0f) {
			*out = o[i];
			*why = "the nearest live AI character";
			return 1;
		}
	*why = "none within 15 m";
	return 0;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P7: the AI's view of an attack (re-combat-vr.md 1): stimlog, swingevent, aiwatch                               */

typedef uint64_t (*three_fn)(uint64_t a, uint64_t b, uint64_t c);
typedef uint64_t (*two_fn)(uint64_t a, uint64_t b);
typedef uint64_t (*six_fn)(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f);
typedef void (*stim_fn)(uintptr_t c, uintptr_t attackData, uintptr_t target);
typedef uintptr_t (*attackdata_fn)(uintptr_t attack);
typedef void (*stimrm_fn)(uintptr_t mgr, int id);

static int g_stimlog, g_aiwatch;
static __thread const char *t_stim_kind; /* which caller of FUN_026c7180 is running (its REGSTIM calls log it) */
static __thread uintptr_t t_stim_c;

static struct {
	int on, cur, ended;
	unsigned seq;
	uintptr_t pc, target_c, target_h, rec, ad, save_atk, save_tgt;
	uint8_t save_var;
	uint32_t id_before, id_ours;
	double t0, until;
	_Atomic unsigned regs, ai[5];
} g_swing;

static void stim_caller(int hk, const char *kind, struct regs *r)
{
	first_call(hk, atomic_fetch_add_explicit(&g_h[hk].calls, 1, memory_order_relaxed) + 1);
	const char *pk = t_stim_kind;
	uintptr_t pc = t_stim_c;
	t_stim_kind = kind;
	t_stim_c = r->rdi;
	if (g_stimlog && rl_ok(RL_STIM)) {
		char cd[160], an[128], td[64];
		logf_("STIM caller=%s c=%s attackData=%#lx '%s' target=%s t=%.6f", kind, combat_desc(r->rdi, cd, sizeof cd),
		      (unsigned long)r->rsi, rec_name(r->rsi, an, sizeof an), hobj_desc(r->rdx, td, sizeof td), mono());
	}
	r->rax = ((three_fn)g_h[hk].orig)(r->rdi, r->rsi, r->rdx);
	t_stim_kind = pk;
	t_stim_c = pc;
}
__attribute__((used)) void somvr_pc_stim_start(struct regs *r) { stim_caller(HK_STIM_START, "start", r); }
__attribute__((used)) void somvr_pc_stim_key(struct regs *r) { stim_caller(HK_STIM_KEY, "key", r); }
__attribute__((used)) void somvr_pc_stim_stop(struct regs *r) { stim_caller(HK_STIM_STOP, "stop", r); }

/* CAIStimulusMgr::RegisterStimulus(mgr, desc): desc +0 stimulus record, +0x20 source HOBJECT, +0x28 position,
 * +0x58 target HOBJECT, +0x60 weight, +0x68 persistent; returns the id (-1 = not registered) */
static void regstim_common(int hk, const char *kind, struct regs *r)
{
	first_call(hk, atomic_fetch_add_explicit(&g_h[hk].calls, 1, memory_order_relaxed) + 1);
	unsigned char d[0x70];
	int have = rd(r->rsi, d, sizeof d) == 0;
	uint8_t mgr_off = rb(r->rdi + 0x24), player_off = rb(G.stim_off);
	r->rax = ((two_fn)g_h[hk].orig)(r->rdi, r->rsi);
	if (t_stim_kind && !strcmp(t_stim_kind, "probe"))
		atomic_fetch_add(&g_swing.regs, 1);
	if (!g_stimlog || !have || !rl_ok(RL_STIM))
		return;
	uintptr_t srec, src, tgt;
	float pos[3], w;
	int32_t pers;
	memcpy(&srec, d, 8);
	memcpy(&src, d + 0x20, 8);
	memcpy(pos, d + 0x28, 12);
	memcpy(&tgt, d + 0x58, 8);
	memcpy(&w, d + 0x60, 4);
	memcpy(&pers, d + 0x68, 4);
	char sn[128], s1[64], s2[64], cd[160];
	logf_("STIMREG %s rc=%d stim='%s' source=%s target=%s pos=%.1f,%.1f,%.1f weight=%g persistent=%d caller=%s c=%s "
	      "mgr_off=%u player_off=%u t=%.6f", kind, (int)(uint32_t)r->rax, rec_name(srec, sn, sizeof sn),
	      hobj_desc(src, s1, sizeof s1), hobj_desc(tgt, s2, sizeof s2), pos[0], pos[1], pos[2], (double)w, pers,
	      t_stim_kind ? t_stim_kind : "other", combat_desc(t_stim_c, cd, sizeof cd), mgr_off, player_off, mono());
}
__attribute__((used)) void somvr_pc_regstim(struct regs *r) { regstim_common(HK_REGSTIM, "list", r); }
__attribute__((used)) void somvr_pc_regstim_p(struct regs *r) { regstim_common(HK_REGSTIM_P, "persist", r); }

/* aiwatch: CAIGoalCounterMelee::vf_07, CAIActionCounterMelee{Block,Dodge}::vf_19 (context check) / vf_11; this+8 =
 * the NPC (a CCharacter: +0x6d0 its Combat) */
static const char *const g_ai_kind[5] = { "goal_counter", "block_valid", "block_go", "dodge_valid", "dodge_go" };
static struct { uintptr_t npc; int8_t last[2]; } g_aiv[64];
static unsigned g_aiv_i;
static pthread_mutex_t g_ai_mu = PTHREAD_MUTEX_INITIALIZER;

/* the context checks run every AI tick: cheap unless the result changed (aiwatch) or a swing counts its target */
static uintptr_t ai_combat(uintptr_t npc)
{
	uintptr_t c = npc && rq(npc) == G.vt_npc ? rq(npc + CH_COMBAT) : 0;
	return c && rq(c) == G.vt_combat ? c : 0;
}

static void ai_note(int kind, uint64_t self, uint64_t res)
{
	uintptr_t npc = rq((uintptr_t)self + 8), c = 0;
	int valid = kind == 1 || kind == 3, r1 = (int)(res & 0xff), was = -1;
	if (g_swing.on && (!valid || r1) && (c = ai_combat(npc)) && c == g_swing.target_c)
		atomic_fetch_add(&g_swing.ai[kind], 1);
	if (!g_aiwatch)
		return;
	if (valid) { /* only the changes per NPC */
		pthread_mutex_lock(&g_ai_mu);
		int s = -1;
		for (int i = 0; i < 64 && s < 0; i++)
			if (g_aiv[i].npc == npc)
				s = i;
		if (s < 0) {
			s = (int)(g_aiv_i++ % 64);
			g_aiv[s].npc = npc;
			g_aiv[s].last[0] = g_aiv[s].last[1] = -1;
		}
		was = g_aiv[s].last[kind == 3];
		g_aiv[s].last[kind == 3] = (int8_t)r1;
		pthread_mutex_unlock(&g_ai_mu);
		if (was == r1)
			return;
	}
	if (!rl_ok(RL_AI))
		return;
	if (!c)
		c = ai_combat(npc);
	float op[3] = { 0, 0, 0 };
	uintptr_t h = c ? rq(c + C_HOBJ) : 0;
	if (h)
		obj_pos(h, op);
	char rn[96], an[96], pa[96], res_s[32] = "";
	if (valid)
		snprintf(res_s, sizeof res_s, " result=%d (was %d)", r1, was);
	logf_("AI %s npc=%#lx c=%#lx '%s' dist=%.1f%s orc_attack='%s' player_attack='%s' tid=%d t=%.6f", g_ai_kind[kind],
	      (unsigned long)npc, (unsigned long)c, c ? rec_name(rq(c + C_CHARREC), rn, sizeof rn) : "",
	      h ? dist3(op, g_pi.pos) / 100.0f : -1.0f, res_s, c ? rec_name(rq(c + C_ATTACK), an, sizeof an) : "",
	      g_pi.scombat ? rec_name(rq(g_pi.scombat + C_ATTACK), pa, sizeof pa) : "", my_tid(), mono());
}

static uint64_t p_ai_common(int hk, int kind, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f)
{
	uint64_t r = ((six_fn)g_h[hk].orig)(a, b, c, d, e, f); /* first: nothing of ours runs before the original */
	first_call(hk, atomic_fetch_add_explicit(&g_h[hk].calls, 1, memory_order_relaxed) + 1);
	if (g_aiwatch || g_swing.on)
		ai_note(kind, a, r);
	return r;
}
static uint64_t p_ai_goal(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) { return p_ai_common(HK_AI_GOAL, 0, a, b, c, d, e, f); }
static uint64_t p_ai_block_ok(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) { return p_ai_common(HK_AI_BLOCK_OK, 1, a, b, c, d, e, f); }
static uint64_t p_ai_block_go(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) { return p_ai_common(HK_AI_BLOCK_GO, 2, a, b, c, d, e, f); }
static uint64_t p_ai_dodge_ok(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) { return p_ai_common(HK_AI_DODGE_OK, 3, a, b, c, d, e, f); }
static uint64_t p_ai_dodge_go(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) { return p_ai_common(HK_AI_DODGE_GO, 4, a, b, c, d, e, f); }

/* swingevent's record: by name, the last player attack, or `auto` = the last player attack if it has stimuli, else the
 * first PC_ record with stimuli (PC_HC_* preferred: the sword strikes) */
static uintptr_t swing_rec(const char *how, char *why, size_t cap)
{
	char name[256];
	if (how && strcmp(how, "auto") && strcmp(how, "last")) {
		uintptr_t r = g_fn_ok[FN_ATK_BYNAME] ? atk_valid(FN(FN_ATK_BYNAME, byname_fn)(how)) : 0;
		snprintf(why, cap, "by name '%s'%s", how, r ? "" : " (not found)");
		return r;
	}
	if (atk_valid(g_last_player_attack) && (!strcmp(how ? how : "", "last") || atk_stims(g_last_player_attack))) {
		snprintf(why, cap, "the last player attack '%s'", g_last_player_attack_name);
		return g_last_player_attack;
	}
	if (how && !strcmp(how, "last")) {
		snprintf(why, cap, "no player attack seen yet");
		return 0;
	}
	static uintptr_t cached;
	if (atk_valid(cached)) {
		snprintf(why, cap, "auto: the first PC_ record with stimuli '%s'", rec_name(cached, name, sizeof name));
		return cached;
	}
	uintptr_t cat = rq(G.atkcat_pp), base = rq(cat + CAT_RECS), any = 0, hc = 0;
	uint32_t n = r32(cat + CAT_COUNT);
	for (uint32_t i = 0; base && i < n && i < 200000 && !hc; i++) {
		uintptr_t rec = base + (uintptr_t)i * DBREC_SIZE;
		if (strncmp(rec_name(rec, name, sizeof name), "PC_", 3) || !atk_stims(rec))
			continue;
		if (!any)
			any = rec;
		if (strstr(name, "_HC_"))
			hc = rec;
	}
	cached = hc ? hc : any;
	snprintf(why, cap, "auto: the first PC_ record with stimuli '%s'", cached ? rec_name(cached, name, sizeof name) : "");
	return cached;
}

static void w_swing(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	const char *how = "auto", *twhy = "";
	double ms = 400;
	int cur = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "cur"))
			cur = 1;
		else if (is_num(argv[i]))
			ms = num(argv[i], 400);
		else
			how = argv[i];
	}
	ms = ms < 50 ? 50 : ms > 3000 ? 3000 : ms;
	if (g_swing.on) {
		logf_("FAIL seq=%u swingevent: the swing of seq %u is still on", seq, g_swing.seq);
		return;
	}
	if (!g_fn_ok[FN_STIM] || !g_fn_ok[FN_ATTACKDATA] || !c->scombat) {
		logf_("FAIL seq=%u swingevent: fn=%d/%d player server Combat=%#lx", seq, g_fn_ok[FN_STIM], g_fn_ok[FN_ATTACKDATA],
		      (unsigned long)c->scombat);
		return;
	}
	struct orc t;
	if (!target_orc(c, &t, &twhy)) {
		logf_("FAIL seq=%u swingevent: no target orc (%s; `pick` one)", seq, twhy);
		return;
	}
	char why[300], rn[128], tn[128];
	uintptr_t rec = swing_rec(how, why, sizeof why), pc = c->scombat;
	uintptr_t ad = rec ? FN(FN_ATTACKDATA, attackdata_fn)(rec) : 0;
	if (!rec || !ad) {
		logf_("FAIL seq=%u swingevent: no attack record (%s) or no AttackData (%#lx)", seq, why, (unsigned long)ad);
		return;
	}
	if (cur && rq(pc + C_ATTACK)) {
		logf_("FAIL seq=%u swingevent cur: the player is in an attack ('%s'): refused", seq,
		      rec_name(rq(pc + C_ATTACK), rn, sizeof rn));
		return;
	}
	memset(&g_swing, 0, sizeof g_swing);
	g_swing.seq = seq;
	g_swing.pc = pc;
	g_swing.target_c = t.c;
	g_swing.target_h = t.hobj;
	g_swing.rec = rec;
	g_swing.ad = ad;
	g_swing.cur = cur;
	g_swing.id_before = r32(pc + C_STIMID);
	if (cur) {
		g_swing.save_atk = rq(pc + C_ATTACK);
		g_swing.save_var = rb(pc + C_VARIATION);
		g_swing.save_tgt = rq(pc + C_TARGET);
		uint8_t z = 0;
		memcpy((void *)(pc + C_ATTACK), &rec, 8);
		memcpy((void *)(pc + C_VARIATION), &z, 1);
		memcpy((void *)(pc + C_TARGET), &t.hobj, 8);
	}
	logf_("SWING seq=%u start rec=%#lx '%s' (%s, %d stimuli) attackData=%#lx target c=%#lx '%s' (%s) dist=%.1f m cur=%d "
	      "ms=%.0f persistent id before=%u player_off=%u t=%.6f", seq, (unsigned long)rec, rec_name(rec, rn, sizeof rn),
	      why, atk_stims(rec), (unsigned long)ad, (unsigned long)t.c, rec_name(rq(t.c + C_CHARREC), tn, sizeof tn), twhy,
	      t.dist / 100.0f, cur, ms, g_swing.id_before, rb(G.stim_off), mono());
	flush_log();
	const char *pk = t_stim_kind;
	uintptr_t pcc = t_stim_c;
	t_stim_kind = "probe";
	t_stim_c = pc;
	g_swing.t0 = mono();
	g_swing.on = 1; /* aiwatch counts from here */
	FN(FN_STIM, stim_fn)(pc, ad, t.hobj);
	t_stim_kind = pk;
	t_stim_c = pcc;
	uint32_t id = r32(pc + C_STIMID);
	g_swing.id_ours = id != g_swing.id_before ? id : 0;
	g_swing.until = g_swing.t0 + ms / 1000.0;
	logf_("SWING seq=%u called: %u registrations (hook P_REGSTIM/_P %s), persistent id %u -> %u", seq,
	      atomic_load(&g_swing.regs), g_h[HK_REGSTIM].installed ? "on" : "NOT installed", g_swing.id_before, id);
	logf_("DONE seq=%u swingevent: the stimuli of '%s' at '%s'; end in %.0f ms, AI reactions counted for 2 s", seq,
	      rec_name(rec, rn, sizeof rn), tn, ms);
}

static void swing_end(const char *why)
{
	uintptr_t pc = g_swing.pc;
	int restored = -1, removed = 0;
	if (pc && rq(pc) == G.vt_combat) {
		if (g_swing.cur) {
			restored = rq(pc + C_ATTACK) == g_swing.rec;
			if (restored) {
				memcpy((void *)(pc + C_ATTACK), &g_swing.save_atk, 8);
				memcpy((void *)(pc + C_VARIATION), &g_swing.save_var, 1);
				memcpy((void *)(pc + C_TARGET), &g_swing.save_tgt, 8);
			}
		}
		uint32_t id = r32(pc + C_STIMID), zero = 0;
		if (g_swing.id_ours && id == g_swing.id_ours && g_fn_ok[FN_STIM_REMOVE] && rq(G.stim_mgr_pp)) {
			FN(FN_STIM_REMOVE, stimrm_fn)(rq(G.stim_mgr_pp), (int)id);
			memcpy((void *)(pc + C_STIMID), &zero, 4);
			removed = 1;
		}
	}
	g_swing.ended = 1;
	logf_("SWING seq=%u end (%s) after %.3f s: cur %s, persistent stimulus %u %s", g_swing.seq, why, mono() - g_swing.t0,
	      restored < 0 ? "not used" : restored ? "restored" : "NOT restored (the game changed pc+0x4d0)", g_swing.id_ours,
	      removed ? "removed" : g_swing.id_ours ? "left (no longer pc+0x168)" : "none of ours");
}

static double g_swing_window = 2.0; /* the AI reactions counted after a swing (the selftest shortens it) */
static void swing_tick(void)
{
	double t = mono();
	if (!g_swing.ended && t >= g_swing.until)
		swing_end("time");
	if (t < g_swing.t0 + g_swing_window)
		return;
	logf_("SWING seq=%u result: the target's AI in %.1f s: goal_counter=%u block_valid=%u block_go=%u dodge_valid=%u "
	      "dodge_go=%u (aiwatch %s; hooks %s)", g_swing.seq, g_swing_window, atomic_load(&g_swing.ai[0]), atomic_load(&g_swing.ai[1]),
	      atomic_load(&g_swing.ai[2]), atomic_load(&g_swing.ai[3]), atomic_load(&g_swing.ai[4]), g_aiwatch ? "on" : "off",
	      g_h[HK_AI_GOAL].installed && g_h[HK_AI_BLOCK_OK].installed && g_h[HK_AI_DODGE_OK].installed ? "on" : "partly off");
	g_swing.on = 0;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P7: hit-stop (re-combat-vr.md 2.4 recipe A: the orc's own frozen timer; recipe B: every tracker's rate 0)      */

typedef uintptr_t *(*tm_create_fn)(uintptr_t *out, uintptr_t T);
typedef int (*tm_attach_fn)(uintptr_t T, uintptr_t *parent, uintptr_t *child);
typedef int (*tm_detach_fn)(uintptr_t T, uintptr_t *t);
typedef int (*tm_setobj_fn)(uintptr_t T, uintptr_t h, uintptr_t *t);
typedef uintptr_t *(*tm_getobj_fn)(uintptr_t *out, uintptr_t T, uintptr_t h);
typedef void (*tm_release_fn)(uintptr_t *p);
typedef uint64_t (*getcuranim_fn)(uintptr_t m, uintptr_t h, uint32_t trk, uint32_t *anim);
typedef uint64_t (*getanimtime_fn)(uintptr_t m, uintptr_t h, uint32_t trk, uint32_t *ms);
typedef uint64_t (*getanimrate_fn)(uintptr_t m, uintptr_t h, uint32_t trk, float *rate);
typedef uint64_t (*setanimrate_fn)(uintptr_t m, uintptr_t h, uint32_t trk, float rate);
typedef uint64_t (*keylookup_fn)(uintptr_t m, uintptr_t h, uint32_t anim, const char *key, const char *arg,
				 uint32_t *frame, uint32_t *ms, uint32_t from);
typedef uint64_t (*samplenode_fn)(uintptr_t m, uintptr_t h, uint32_t anim, uint32_t node, uint32_t ms, float *xf,
				  int flag);
typedef uint64_t (*findtracker_fn)(uintptr_t h, uint8_t *trk, uint32_t anim);

static struct {
	int on, mode, ntrk, ticks, logged, trk_found;
	unsigned seq, applies;
	uintptr_t c, h, old, mine; /* old/mine: HTIMER handles (the timers keep no pointer to these) */
	double t0, until;
	float scale, rate[33], trk_rate;
	uint8_t trk[33], trk_id;   /* trk_id: the tracker that plays the orc's current attack (c+0x17c), else 0xff */
	int32_t anim;
	uint32_t anim_t0, anim_last;
} g_hs;
static struct { uintptr_t c; unsigned seq; double t0, t_end; int logged; } g_hsa; /* the APPLYDAMAGE after a hold */
/* P8: `hitstop MS ... onbpw` = armed: the next counter prompt (BPW key) of an orc whose target is the player starts the
 * hold on that orc at the next server tick (the counter hook stores the orc, p7_tick starts it) */
static struct { int armed, mode; unsigned seq; double ms; float scale; _Atomic uintptr_t c; } g_hsarm;

/* the animation time of the orc's playing tracker: the one FindTracker gives for the current attack's animation
 * (Combat+0x17c, as keytimes reads it); P7 read tracker 0xff, which stayed at 0 ms */
static uint32_t trk_time(uintptr_t h)
{
	uint32_t ms = 0;
	if (g_fn_ok[FN_GETANIMTIME] && h)
		FN(FN_GETANIMTIME, getanimtime_fn)(model_iface(), h, g_hs.trk_id, &ms);
	return ms;
}

static int hs_fns(int mode)
{
	if (mode == 1)
		return g_fn_ok[FN_TM_GETOBJ] && g_fn_ok[FN_TM_CREATE] && g_fn_ok[FN_TM_ATTACH] && g_fn_ok[FN_TM_SETOBJ] &&
		       g_fn_ok[FN_TM_DETACH] && g_fn_ok[FN_TM_RELEASE];
	return g_fn_ok[FN_GETCURANIM] && g_fn_ok[FN_GETANIMRATE] && g_fn_ok[FN_SETANIMRATE];
}

/* start a hold on one orc (the checks of w_hitstop done); 0 = on */
static int hs_begin(const struct orc *op, double ms, int mode, float scale, unsigned seq, const char *why)
{
	struct orc o = *op;
	if (rq(o.c + C_SYNC1) || rq(o.c + C_SYNC2)) {
		logf_("FAIL seq=%u hitstop: the orc %#lx is in a sync action (+0x188/+0x190): refused", seq, (unsigned long)o.c);
		return -1;
	}
	if (!hs_fns(mode)) {
		logf_("FAIL seq=%u hitstop %s: a function did not check out (FUNC lines)", seq, mode == 1 ? "timer" : "rate");
		return -1;
	}
	uintptr_t h = o.hobj, m = model_iface(), T = rq(G.timer_pp);
	char d1[300], d2[300], d3[128], rn[128];
	memset(&g_hs, 0, sizeof g_hs);
	g_hs.seq = seq;
	g_hs.mode = mode;
	g_hs.c = o.c;
	g_hs.h = h;
	g_hs.scale = scale;
	g_hs.trk_id = 0xff;
	g_hs.anim = (int32_t)r32(o.c + C_ANIM);
	uint8_t tk = 0xff;
	if (g_fn_ok[FN_FINDTRACKER] && g_hs.anim != -1 &&
	    (uint32_t)FN(FN_FINDTRACKER, findtracker_fn)(h, &tk, (uint32_t)g_hs.anim) == 0) {
		g_hs.trk_found = 1;
		g_hs.trk_id = tk;
	}
	if (g_fn_ok[FN_GETANIMRATE])
		FN(FN_GETANIMRATE, getanimrate_fn)(m, h, g_hs.trk_id, &g_hs.trk_rate);
	g_hs.anim_t0 = g_hs.anim_last = trk_time(h);
	if (mode == 1) {
		if (rq(T) != G.vt_timer) {
			logf_("FAIL seq=%u hitstop: the timer manager %#lx has vtable %#lx, want %#lx", seq, (unsigned long)T,
			      (unsigned long)rq(T), (unsigned long)G.vt_timer);
			return -1;
		}
		FN(FN_TM_GETOBJ, tm_getobj_fn)(&g_hs.old, T, h);
		if (!g_hs.old) {
			logf_("FAIL seq=%u hitstop: the orc has no object timer", seq);
			return -1;
		}
		FN(FN_TM_CREATE, tm_create_fn)(&g_hs.mine, T);
		if (!g_hs.mine || rq(g_hs.mine) != G.vt_etimer) {
			logf_("FAIL seq=%u hitstop: CreateTimer gave %#lx (vtable %#lx)", seq, (unsigned long)g_hs.mine,
			      (unsigned long)rq(g_hs.mine));
			if (g_hs.mine)
				FN(FN_TM_RELEASE, tm_release_fn)(&g_hs.mine);
			FN(FN_TM_RELEASE, tm_release_fn)(&g_hs.old);
			return -1;
		}
		int ra = FN(FN_TM_ATTACH, tm_attach_fn)(T, &g_hs.old, &g_hs.mine);
		unsigned char cont[0x10];
		uint32_t numv = scale > 0 ? 1u : 0u, den = scale > 0 ? (uint32_t)lroundf(1.0f / scale) : 1u;
		if (!rd(g_hs.old + TM_TOTAL, cont, 4) && !rd(g_hs.old + TM_TOTALS, cont + 4, 8)) { /* the orc's clock goes on */
			memcpy((void *)(g_hs.mine + TM_TOTAL), cont, 4);
			memcpy((void *)(g_hs.mine + TM_TOTALS), cont + 4, 8);
		}
		if (!rd(g_hs.old + TM_CARRY, cont, 8))
			memcpy((void *)(g_hs.mine + TM_CARRY), cont, 8);
		memcpy((void *)(g_hs.mine + TM_NUM), &numv, 4);
		memcpy((void *)(g_hs.mine + TM_DEN), &den, 4);
		memcpy((void *)(g_hs.mine + TM_SCALE), &scale, 4);
		int rs = FN(FN_TM_SETOBJ, tm_setobj_fn)(T, h, &g_hs.mine);
		logf_("HITSTOP seq=%u timer: orc c=%#lx '%s' (%s) dist=%.1f m attack='%s' old=%s mine=%s attach=%d set=%d "
		      "h+0x68 now %#lx (%s)", seq, (unsigned long)o.c, rec_name(rq(o.c + C_CHARREC), rn, sizeof rn), why,
		      o.dist / 100.0f, rec_name(rq(o.c + C_ATTACK), d3, sizeof d3), tm_desc(g_hs.old, d1, sizeof d1),
		      tm_desc(g_hs.mine, d2, sizeof d2), ra, rs, (unsigned long)rq(h + OBJ_TIMER),
		      rq(h + OBJ_TIMER) == g_hs.mine ? "ours" : "NOT ours");
	} else {
		static const int order[33] = { 0xff, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
					       16,   17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 };
		for (int i = 0; i < 33; i++) {
			uint32_t anim = 0xffffffffu;
			float r = 0;
			if (FN(FN_GETCURANIM, getcuranim_fn)(m, h, (uint32_t)order[i], &anim) & 0xff || anim == 0xffffffffu)
				continue;
			if (FN(FN_GETANIMRATE, getanimrate_fn)(m, h, (uint32_t)order[i], &r) & 0xff)
				continue;
			g_hs.trk[g_hs.ntrk] = (uint8_t)order[i];
			g_hs.rate[g_hs.ntrk++] = r;
			FN(FN_SETANIMRATE, setanimrate_fn)(m, h, (uint32_t)order[i], 0.0f);
		}
		logf_("HITSTOP seq=%u rate: orc c=%#lx '%s' (%s) dist=%.1f m: %d trackers set to rate 0 (first %u: was %g)", seq,
		      (unsigned long)o.c, rec_name(rq(o.c + C_CHARREC), rn, sizeof rn), why, o.dist / 100.0f, g_hs.ntrk,
		      g_hs.ntrk ? g_hs.trk[0] : 0, g_hs.ntrk ? (double)g_hs.rate[0] : 0.0);
	}
	g_hs.t0 = mono();
	g_hs.until = g_hs.t0 + ms / 1000.0;
	g_hs.on = 1;
	logf_("DONE seq=%u hitstop %.0f ms (%s%s) on '%s': attack anim %d on tracker %u (%s, rate %g): animation time %u ms at "
	      "the start", seq, ms, mode == 1 ? "timer" : "rate", mode == 1 && scale > 0 ? ", scaled" : "", rn, g_hs.anim,
	      g_hs.trk_id, g_hs.trk_found ? "FindTracker" : "not found: tracker 0xff", (double)g_hs.trk_rate, g_hs.anim_t0);
	return 0;
}

static void w_hitstop(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	if (argc < 2 || !is_num(argv[1])) {
		logf_("FAIL seq=%u hitstop: want MS [timer|rate] [scale=S] [onbpw] (or hitstop off)", seq);
		return;
	}
	double ms = num(argv[1], 120);
	ms = ms < 10 ? 10 : ms > 3000 ? 3000 : ms;
	int mode = 1, onbpw = 0;
	float scale = 0;
	for (int i = 2; i < argc; i++) {
		if (!strcmp(argv[i], "rate"))
			mode = 2;
		else if (!strcmp(argv[i], "onbpw"))
			onbpw = 1;
		else if (!strncmp(argv[i], "scale=", 6))
			scale = (float)num(argv[i] + 6, 0);
	}
	if (!(scale >= 0 && scale <= 1)) {
		logf_("FAIL seq=%u hitstop: scale=%g, want 0..1", seq, (double)scale);
		return;
	}
	if (g_hs.on) {
		logf_("FAIL seq=%u hitstop: the hold of seq %u is still on", seq, g_hs.seq);
		return;
	}
	if (onbpw) {
		g_hsarm.mode = mode;
		g_hsarm.ms = ms;
		g_hsarm.scale = scale;
		g_hsarm.seq = seq;
		atomic_store(&g_hsarm.c, 0);
		g_hsarm.armed = 1;
		logf_("DONE seq=%u hitstop %.0f ms (%s) armed: the next counter prompt (BPW) of an orc at the player starts it "
		      "(hook P_COUNTER %s)", seq, ms, mode == 1 ? "timer" : "rate", g_h[HK_COUNTER].installed ? "on" : "NOT installed");
		return;
	}
	const char *why = "";
	struct orc o;
	if (g_hsarm.armed) { /* a direct hold replaces an armed one that has not fired */
		g_hsarm.armed = 0;
		logf_("HITSTOP seq=%u the armed hold of seq %u is dropped (it had not fired)", seq, g_hsarm.seq);
	}
	if (!target_orc(c, &o, &why)) {
		logf_("FAIL seq=%u hitstop: no target orc (%s)", seq, why);
		return;
	}
	hs_begin(&o, ms, mode, scale, seq, why);
}

/* the armed hold: the counter hook stored the orc; start it on this server tick */
static void hs_arm_tick(void)
{
	uintptr_t c = atomic_exchange(&g_hsarm.c, 0);
	struct orc o;
	if (!c || g_hs.on)
		return;
	g_hsarm.armed = 0;
	if (!orc_get(c, g_pi.pos, &o) || o.dead) {
		logf_("FAIL seq=%u hitstop onbpw: the orc %#lx is gone", g_hsarm.seq, (unsigned long)c);
		return;
	}
	hs_begin(&o, g_hsarm.ms, g_hsarm.mode, g_hsarm.scale, g_hsarm.seq, "its counter prompt (BPW) at the player");
}

static void hs_end(const char *why)
{
	if (!g_hs.on)
		return;
	struct orc o;
	int valid = orc_get(g_hs.c, g_pi.pos, &o) && o.hobj == g_hs.h, back = 0;
	uintptr_t T = rq(G.timer_pp), m = model_iface();
	uint32_t now = valid ? trk_time(g_hs.h) : 0;
	if (g_hs.mode == 1) {
		if (valid && rq(g_hs.h + OBJ_TIMER) == g_hs.mine) {
			FN(FN_TM_SETOBJ, tm_setobj_fn)(T, g_hs.h, &g_hs.old);
			back = rq(g_hs.h + OBJ_TIMER) == g_hs.old;
		}
		FN(FN_TM_DETACH, tm_detach_fn)(T, &g_hs.mine);
		FN(FN_TM_RELEASE, tm_release_fn)(&g_hs.mine);
		FN(FN_TM_RELEASE, tm_release_fn)(&g_hs.old);
	} else if (valid) {
		for (int i = 0; i < g_hs.ntrk; i++)
			FN(FN_SETANIMRATE, setanimrate_fn)(m, g_hs.h, g_hs.trk[i], g_hs.rate[i]);
		back = 1;
	}
	g_hs.on = 0;
	g_hsa.c = g_hs.c;
	g_hsa.seq = g_hs.seq;
	g_hsa.t0 = g_hs.t0;
	g_hsa.t_end = mono();
	g_hsa.logged = 0;
	logf_("HITSTOP seq=%u end (%s) after %.3f s: %s; animation time on tracker %u (anim %d%s): %u ms at the start, %u at "
	      "the hold's last tick (%+d ms during the hold), %u now; %u APPLYDAMAGE of the orc during it", g_hs.seq, why,
	      g_hsa.t_end - g_hs.t0, !valid ? "the orc is gone (nothing written to it)" : back ? (g_hs.mode == 1 ?
	      "its timer is back" : "its rates are back") : "its timer was no longer ours (left alone)", g_hs.trk_id,
	      g_hs.anim, g_hs.trk_found ? "" : ", not found", g_hs.anim_t0, g_hs.anim_last,
	      (int)g_hs.anim_last - (int)g_hs.anim_t0, now, g_hs.applies);
}

static void hs_tick(void)
{
	struct orc o;
	if (!orc_get(g_hs.c, g_pi.pos, &o) || o.hobj != g_hs.h || o.dead || rq(o.c + C_SYNC1) || rq(o.c + C_SYNC2)) {
		hs_end("the orc died, left or entered a sync");
		return;
	}
	g_hs.anim_last = trk_time(g_hs.h);
	if (++g_hs.ticks % 4 == 1 && g_hs.logged++ < 6 && rl_ok(RL_HSTOP)) {
		uintptr_t t = rq(g_hs.h + OBJ_TIMER);
		logf_("HITSTOP seq=%u +%.3f s: animation time %u ms on tracker %u (start %u), the orc's timer %#lx frame %u ms",
		      g_hs.seq, mono() - g_hs.t0, g_hs.anim_last, g_hs.trk_id, g_hs.anim_t0, (unsigned long)t,
		      t ? r32(t + TM_FRAME) : 0);
	}
	if (mono() >= g_hs.until)
		hs_end("time");
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P7: animation key times (re-combat-vr.md 3): at each Combat_StartAttack (P_KEYTIMES) the attack is queued; the   */
/* next probe tick samples it; the attacker's APPLYDAMAGE (P_APPLYDMG) measures it                                */

#define KT_N 16
static struct kt {
	int state; /* 0 free, 1 queued by the hook, 2 sampled */
	uintptr_t c, h, attack;
	double t0, ts;
	uint64_t tick0;
	int32_t anim;
	uint8_t trk;
	int found, xf_ok, key_ok[5], hits, at_player;
	uint32_t now, node, key_ms[5], timp;
	uintptr_t target;
	uint16_t ws[10];
	float rate, scale, sim, xf[16];
} g_kt[KT_N];
static unsigned g_kt_i, g_kt_rej;
static int g_keytimes;
static pthread_mutex_t g_kt_mu = PTHREAD_MUTEX_INITIALIZER;
static const char *const g_kt_keys[5] = { "APPLYDAMAGE", "BPW", "EPW", "BCOL", "ECOL" };

__attribute__((used)) void somvr_pc_keytimes(struct regs *r)
{
	first_call(HK_KEYTIMES, atomic_fetch_add_explicit(&g_h[HK_KEYTIMES].calls, 1, memory_order_relaxed) + 1);
	if (!g_keytimes)
		return;
	uintptr_t c = r->rdi - C_WS; /* FUN_02263590(ws = Combat+0xb0, hObj = Combat+0x4a0, attack) */
	if (rq(c) != G.vt_combat || rq(c + C_HOBJ) != r->rsi) {
		g_kt_rej++;
		return;
	}
	pthread_mutex_lock(&g_kt_mu);
	struct kt *k = &g_kt[g_kt_i++ % KT_N];
	memset(k, 0, sizeof *k);
	k->state = 1;
	k->c = c;
	k->h = r->rsi;
	k->attack = r->rdx;
	k->t0 = mono();
	k->tick0 = g_ticks_all;
	pthread_mutex_unlock(&g_kt_mu);
}

static void kt_sample(struct kt *k)
{
	uintptr_t m = model_iface(), h = k->h;
	k->anim = (int32_t)r32(k->c + C_ANIM);
	rd(k->c + C_KT, k->ws, sizeof k->ws);
	k->trk = 0xff;
	k->found = g_fn_ok[FN_FINDTRACKER] && k->anim != -1 &&
		   (uint32_t)FN(FN_FINDTRACKER, findtracker_fn)(h, &k->trk, (uint32_t)k->anim) == 0;
	if (k->found) {
		if (g_fn_ok[FN_GETANIMTIME])
			FN(FN_GETANIMTIME, getanimtime_fn)(m, h, k->trk, &k->now);
		if (g_fn_ok[FN_GETANIMRATE])
			FN(FN_GETANIMRATE, getanimrate_fn)(m, h, k->trk, &k->rate);
	}
	uintptr_t tm = rq(h + OBJ_TIMER), sim = rq(rq(G.srv_pp) + SRV_SIMTIMER);
	k->scale = tm ? rf(tm + TM_SCALE) : 0;
	k->sim = sim && sim != tm ? rf(sim + TM_SCALE) : 1.0f;
	for (int i = 0; i < 5; i++) {
		uint32_t fr = 0;
		k->key_ok[i] = g_fn_ok[FN_KEYLOOKUP] && k->anim != -1 &&
			       (FN(FN_KEYLOOKUP, keylookup_fn)(m, h, (uint32_t)k->anim, g_kt_keys[i], "", &fr, &k->key_ms[i], 0) & 0xff);
	}
	k->node = 0xffffffffu;
	if (g_fn_ok[FN_GETNODE] && FN(FN_GETNODE, getnode_fn)(m, h, "R_Hand", &k->node) & 0xff)
		k->node = 0xffffffffu;
	k->timp = k->ws[2] != 0xffff ? k->ws[2] : k->key_ok[0] ? k->key_ms[0] : 0xffffffffu;
	if (g_fn_ok[FN_SAMPLENODE] && k->node != 0xffffffffu && k->timp != 0xffffffffu && k->anim != -1)
		k->xf_ok = (FN(FN_SAMPLENODE, samplenode_fn)(m, h, (uint32_t)k->anim, k->node, k->timp, k->xf, 0) & 0xff) == 0;
	k->target = rq(k->c + C_TARGET);
	k->at_player = k->target && (k->target == g_pi.phobj || k->target == g_pi.charobj);
	k->ts = mono();
	k->state = 2;
	if (!rl_ok(RL_KEYT))
		return;
	char cd[160], an[128], ks[5][16], xs[96] = "-";
	for (int i = 0; i < 5; i++)
		snprintf(ks[i], sizeof ks[i], k->key_ok[i] ? "%u" : "-", k->key_ms[i]);
	double sp = (double)k->rate * k->scale * k->sim, lead = k->timp != 0xffffffffu && k->found && sp > 0 ?
						 ((double)k->timp - k->now) / 1000.0 / sp : -1.0;
	if (k->xf_ok)
		cfmt(xs, sizeof xs, "%.1f,%.1f,%.1f", k->xf[0], k->xf[1], k->xf[2]);
	logf_("KEYTIMES c=%s target=%s attack='%s' anim=%d table EAL=%u BCOL=%u APPLYDAMAGE=%u STARTFACING=%u STOPFACING=%u BEFACING=%u "
	      "BPW=%u EPW=%u SHOOT=%u STIMULUS=%u; tracker %s%u now=%u ms rate=%g timer=%g sim=%g; KeyLookup APPLYDAMAGE=%s "
	      "BPW=%s EPW=%s BCOL=%s ECOL=%s; impact in %.3f s; R_Hand at the impact (SampleNode, model) %s; sampled %.3f s "
	      "after the start t=%.6f", combat_desc(k->c, cd, sizeof cd), k->at_player ? "THE PLAYER" : k->target ? "other" : "none",
	      rec_name(k->attack, an, sizeof an), k->anim,
	      k->ws[0], k->ws[1], k->ws[2], k->ws[3], k->ws[4], k->ws[5], k->ws[6], k->ws[7], k->ws[8], k->ws[9],
	      k->found ? "" : "NOT FOUND ", k->trk, k->now, (double)k->rate, (double)k->scale, (double)k->sim, ks[0], ks[1],
	      ks[2], ks[3], ks[4], lead, xs, k->ts - k->t0, k->ts);
}

static void kt_process(void)
{
	for (int i = 0; i < KT_N; i++) {
		pthread_mutex_lock(&g_kt_mu);
		int due = g_kt[i].state == 1 && g_ticks_all > g_kt[i].tick0;
		pthread_mutex_unlock(&g_kt_mu);
		if (due)
			kt_sample(&g_kt[i]);
	}
}

/* at the attacker's APPLYDAMAGE: the measured time since the start and the hand vs SampleNode's prediction */
static void kt_applied(uintptr_t c)
{
	struct kt *k = NULL;
	double t = mono();
	pthread_mutex_lock(&g_kt_mu);
	for (int i = 0; i < KT_N; i++)
		if (g_kt[i].state == 2 && g_kt[i].c == c && t - g_kt[i].t0 < 10.0 && (!k || g_kt[i].t0 > k->t0))
			k = &g_kt[i];
	pthread_mutex_unlock(&g_kt_mu);
	if (!k || !rl_ok(RL_KEYT))
		return;
	k->hits++;
	float live[8] = { 0 }, op[3], q[4] = { 0, 0, 0, 1 }, pw[3] = { 0, 0, 0 };
	int lok = k->node != 0xffffffffu && node_xf(k->h, k->node, live) == 0;
	obj_pos(k->h, op);
	rvec(k->h + OBJ_ROT, q, 4);
	if (k->xf_ok) {
		qrot(q, k->xf, pw);
		for (int j = 0; j < 3; j++)
			pw[j] += op[j];
	}
	double sp = (double)k->rate * k->scale * k->sim;
	double pred = k->found && sp > 0 && k->timp != 0xffffffffu ? (k->ts - k->t0) + ((double)k->timp - k->now) / 1000.0 / sp : -1;
	char cd[160], win[160] = "no parry window in the table";
	double ims = (t - k->t0) * 1000.0;
	if (k->ws[6] != 0xffff && k->ws[7] != 0xffff) /* BPW / EPW: the parry window vs the measured impact */
		cfmt(win, sizeof win, "parry window BPW %u..EPW %u ms (%u ms): the impact %s (%+.0f ms after EPW), parry byte now %u",
		     k->ws[6], k->ws[7], (unsigned)(k->ws[7] - k->ws[6]), ims < k->ws[6] ? "BEFORE the window" : ims <= k->ws[7] ?
		     "inside the window" : "after the window", ims - k->ws[7], rb(c + C_WIN_PARRY));
	uintptr_t tg = rq(c + C_TARGET);
	logf_("KEYHIT c=%s hit %d target=%s: APPLYDAMAGE %.3f s after the start, predicted %.3f s (table %u ms); %s; R_Hand live "
	      "%s%.1f,%.1f,%.1f predicted (model x object now) %.1f,%.1f,%.1f err=%.1f cm, as world %.1f cm t=%.6f",
	      combat_desc(c, cd, sizeof cd), k->hits, tg && (tg == g_pi.phobj || tg == g_pi.charobj) ? "THE PLAYER" : tg ? "other" :
	      "none", t - k->t0, pred, k->timp, win, lok ? "" : "(no node) ", live[0], live[1], live[2], pw[0], pw[1], pw[2],
	      lok && k->xf_ok ? dist3(live, pw) : -1.0f, lok && k->xf_ok ? dist3(live, k->xf) : -1.0f, t);
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P7: projectiles (re-combat-items.md 1): projlog, projblock, projreflect                                       */

static int g_projlog;
static struct { int mode, own; float radius; unsigned seq; double until; } g_pjarm; /* mode 1 block, 2 reflect */
static struct { double until; uintptr_t p; unsigned seq; } g_pjwatch;
static struct pj { uintptr_t p; double t0, tl; unsigned n; float mind; } g_pj[32];
static pthread_mutex_t g_pj_mu = PTHREAD_MUTEX_INITIALIZER;
static _Atomic unsigned g_pj_blocked, g_pj_reflected;

static void pj_turn(uintptr_t p, const unsigned char *b, const float pos[3], const float dir[3])
{
	uintptr_t sh, shh;
	memcpy(&sh, b + PJ_SHOOTER, 8);
	shh = sh && rq(sh) == G.vt_combat ? rq(sh + C_HOBJ) : 0;
	float tp[3], nd[3], nq[4];
	if (shh) {
		obj_pos(shh, tp);
		tp[1] += 120.0f;
		for (int k = 0; k < 3; k++)
			nd[k] = tp[k] - pos[k];
	} else {
		for (int k = 0; k < 3; k++)
			nd[k] = -dir[k];
	}
	float l = sqrtf(nd[0] * nd[0] + nd[1] * nd[1] + nd[2] * nd[2]);
	if (l < 1e-3f)
		nd[0] = -dir[0], nd[1] = -dir[1], nd[2] = -dir[2], l = 1;
	for (int k = 0; k < 3; k++)
		nd[k] /= l;
	float yaw = atan2f(nd[0], nd[2]), pitch = -asinf(fmaxf(-1.0f, fminf(1.0f, nd[1])));
	float yq[4] = { 0, sinf(yaw / 2), 0, cosf(yaw / 2) }, pq[4] = { sinf(pitch / 2), 0, 0, cosf(pitch / 2) };
	qmul(yq, pq, nq);
	float np[3] = { pos[0] + nd[0] * 5.0f, pos[1] + nd[1] * 5.0f, pos[2] + nd[2] * 5.0f }, chk[3], fz[3] = { 0, 0, 1 };
	uint8_t z = 0;
	uint64_t z8 = 0;
	memcpy((void *)(p + PJ_POS), np, 12);
	memcpy((void *)(p + PJ_ROT), nq, 16);
	memcpy((void *)(p + PJ_HOMING), &z, 1);
	memcpy((void *)(p + PJ_SPLINE), &z, 1);
	memcpy((void *)(p + PJ_TARGET), &z8, 8);
	if (g_pjarm.own && g_pi.scombat)
		memcpy((void *)(p + PJ_SHOOTER), &g_pi.scombat, 8);
	qrot(nq, fz, chk);
	atomic_fetch_add(&g_pj_reflected, 1);
	char cd[160];
	logf_("PROJREFLECT seq=%u p=%#lx shooter=%s back %s: dir %.3f,%.3f,%.3f -> %.3f,%.3f,%.3f (R(q)+Z %.3f,%.3f,%.3f) homing, "
	      "spline, target cleared; shooter %s t=%.6f", g_pjarm.seq, (unsigned long)p, combat_desc(sh, cd, sizeof cd),
	      shh ? "at the shooter" : "along its own line", dir[0], dir[1], dir[2], nd[0], nd[1], nd[2], chk[0], chk[1], chk[2],
	      g_pjarm.own ? "= the player's Combat" : "kept", mono());
}

static void pj_pre(uintptr_t p)
{
	unsigned char b[PJ_SIZE];
	uint64_t vt;
	if (rd(p, b, sizeof b))
		return;
	memcpy(&vt, b, 8);
	if (vt != G.vt_proj)
		return;
	float pos[3], q[4], dir[3], fz[3] = { 0, 0, 1 }, chest[3] = { g_pi.pos[0], g_pi.pos[1] + 40.0f, g_pi.pos[2] };
	float spd, trav, rng, grav;
	uintptr_t tgt, sh;
	memcpy(pos, b + PJ_POS, 12);
	memcpy(q, b + PJ_ROT, 16);
	memcpy(&spd, b + PJ_SPEED, 4);
	memcpy(&trav, b + PJ_TRAVEL, 4);
	memcpy(&rng, b + PJ_RANGE, 4);
	memcpy(&grav, b + PJ_GRAVW, 4);
	memcpy(&tgt, b + PJ_TARGET, 8);
	memcpy(&sh, b + PJ_SHOOTER, 8);
	qrot(q, fz, dir);
	float d = dist3(pos, chest);
	int at_player = tgt && (tgt == g_pi.phobj || tgt == g_pi.charobj), fresh = 0, slot = -1;
	double now = mono();
	unsigned frames = 0;
	float mind = d;
	pthread_mutex_lock(&g_pj_mu);
	for (int i = 0; i < 32; i++) {
		if (g_pj[i].p && now - g_pj[i].tl > 3.0)
			g_pj[i].p = 0;
		if (g_pj[i].p == p)
			slot = i;
	}
	if (slot < 0)
		for (int i = 0; i < 32 && slot < 0; i++)
			if (!g_pj[i].p) {
				slot = i;
				fresh = 1;
				g_pj[i].p = p;
				g_pj[i].t0 = now;
				g_pj[i].n = 0;
				g_pj[i].mind = d;
			}
	if (slot >= 0) {
		g_pj[slot].tl = now;
		frames = ++g_pj[slot].n;
		if (d < g_pj[slot].mind)
			g_pj[slot].mind = d;
		mind = g_pj[slot].mind;
		if (b[PJ_DONE])
			g_pj[slot].p = 0;
	}
	pthread_mutex_unlock(&g_pj_mu);
	char cd[160], tn[64], r1[128], r2[128];
	if (g_projlog && fresh && rl_ok(RL_PROJ))
		logf_("PROJ new p=%#lx shooter=%s target=%s ranged='%s' attack='%s' speed=%.0f range=%.0f travelled=%.0f "
		      "gravity_weight=%.1f immediate=%u homing=%u gravity=%u spline=%u crit=%u pos=%.1f,%.1f,%.1f dir=%.3f,%.3f,%.3f "
		      "dist=%.2f m t=%.6f", (unsigned long)p, combat_desc(sh, cd, sizeof cd), hobj_desc(tgt, tn, sizeof tn),
		      rec_name(rq(p + PJ_RANGED), r1, sizeof r1), rec_name(rq(p + PJ_ATTACK), r2, sizeof r2), (double)spd,
		      (double)rng, (double)trav, (double)grav, b[PJ_IMMEDIATE], b[PJ_HOMING], b[PJ_GRAVITY], b[PJ_SPLINE],
		      b[PJ_CRIT], pos[0], pos[1], pos[2], dir[0], dir[1], dir[2], d / 100.0f, now);
	else if (g_projlog && !b[PJ_DONE] && (at_player || d < 1500.0f) && rl_ok(RL_PROJ))
		logf_("PROJ fly p=%#lx frame=%u pos=%.1f,%.1f,%.1f dir=%.3f,%.3f,%.3f dist=%.2f m travelled=%.0f%s", (unsigned long)p,
		      frames, pos[0], pos[1], pos[2], dir[0], dir[1], dir[2], d / 100.0f, (double)trav, at_player ? " at the player" : "");
	if (b[PJ_DONE]) {
		if (g_projlog && slot >= 0 && rl_ok(RL_PROJ))
			logf_("PROJ end p=%#lx after %u frames, travelled %.0f, closest %.2f m to the player", (unsigned long)p, frames,
			      (double)trav, mind / 100.0f);
		return;
	}
	if (!g_pjarm.mode || now >= g_pjarm.until || d >= g_pjarm.radius * 100.0f)
		return;
	if (g_pjarm.mode == 1) {
		uint8_t one = 1;
		memcpy((void *)(p + PJ_DONE), &one, 1); /* this same vf_13 call removes it before it moves */
		atomic_fetch_add(&g_pj_blocked, 1);
		logf_("PROJBLOCK seq=%u p=%#lx shooter=%s at %.2f m: +0x228 = 1 (removed by this update) t=%.6f", g_pjarm.seq,
		      (unsigned long)p, combat_desc(sh, cd, sizeof cd), d / 100.0f, now);
	} else {
		pj_turn(p, b, pos, dir);
	}
	g_pjwatch.until = now + 3.0;
	g_pjwatch.p = p;
	g_pjwatch.seq = g_pjarm.seq;
	g_pjarm.mode = 0;
}

static uint64_t p_pjupdate(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f)
{
	first_call(HK_PJ_UPDATE, atomic_fetch_add_explicit(&g_h[HK_PJ_UPDATE].calls, 1, memory_order_relaxed) + 1);
	if (g_projlog || g_pjarm.mode)
		pj_pre((uintptr_t)a);
	return ((six_fn)g_h[HK_PJ_UPDATE].orig)(a, b, c, d, e, f);
}

/* the projectile DealHit FUN_026c8e10(shooter Combat*, ProjHit* (+0 pos, +0x20 the resolved victim), attack, ranged
 * item, -, hit node, ...) */
__attribute__((used)) void somvr_pc_pjdamage(struct regs *r)
{
	first_call(HK_PJ_DAMAGE, atomic_fetch_add_explicit(&g_h[HK_PJ_DAMAGE].calls, 1, memory_order_relaxed) + 1);
	uintptr_t hobj = rq(r->rsi + 0x20);
	int on_player = hobj && (hobj == g_pi.phobj || hobj == g_pi.charobj);
	double now = mono();
	char cd[160], hn[64], an[128], rn[128], nn[128];
	if (now < g_pjwatch.until && rl_ok(RL_PROJ))
		logf_("PROJWATCH seq=%u a projectile impact %.2f s after the %s: victim %s", g_pjwatch.seq,
		      now - (g_pjwatch.until - 3.0), g_pj_reflected ? "block/reflect" : "block", hobj_desc(hobj, hn, sizeof hn));
	if (!g_projlog || !rl_ok(RL_PROJ))
		return;
	float pos[3];
	rvec(r->rsi, pos, 3);
	logf_("PROJHIT shooter=%s victim=%s%s pos=%.1f,%.1f,%.1f attack='%s' ranged='%s' hit_node='%s' t=%.6f",
	      combat_desc(r->rdi, cd, sizeof cd), hobj_desc(hobj, hn, sizeof hn), on_player ? " THE PLAYER" : "", pos[0], pos[1],
	      pos[2], rec_name(r->rdx, an, sizeof an), rec_name(r->rcx, rn, sizeof rn), rec_name(r->r9, nn, sizeof nn), now);
}

static void w_proj(unsigned seq, const char *w, int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "off")) {
		g_pjarm.mode = 0;
		logf_("DONE seq=%u %s off (blocked %u, reflected %u so far)", seq, w, atomic_load(&g_pj_blocked),
		      atomic_load(&g_pj_reflected));
		return;
	}
	float m = argc > 1 && is_num(argv[1]) ? (float)num(argv[1], 1.5) : 1.5f;
	m = m < 0.2f ? 0.2f : m > 10.0f ? 10.0f : m;
	g_pjarm.own = 0;
	for (int i = 1; i < argc; i++)
		g_pjarm.own |= !strcmp(argv[i], "own");
	g_pjarm.radius = m;
	g_pjarm.seq = seq;
	g_pjarm.until = mono() + 30.0;
	g_pjarm.mode = !strcmp(w, "projreflect") ? 2 : 1;
	logf_("DONE seq=%u %s armed for 30 s: the next projectile within %.1f m of the player%s (hook P_PJ_UPDATE %s)", seq, w,
	      (double)m, g_pjarm.own ? ", own" : "", g_h[HK_PJ_UPDATE].installed ? "on" : "NOT installed");
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P7: FX (re-combat-items.md 2): fxlog over the 19 FX init calls, weaponfx = the WEAPONFX key's recipe            */

typedef void (*fxcs_fn)(void *cs, const char *name, uintptr_t mh, uint32_t flags, uintptr_t parent);
typedef long (*fxcreate_fn)(uintptr_t mgr, uintptr_t *out, void *cs, int start, int b, int c);
typedef void (*fxfree_fn)(void *cs);
typedef void (*fxrel_fn)(uintptr_t *handle);
typedef uintptr_t (*objmh_fn)(uintptr_t objsys, uintptr_t obj);

static int g_fxlog;
static struct { char name[96]; unsigned n, weapon; } g_fxn[96];
static int g_fxn_n;
static char g_fx_last[96], g_fx_lastw[96];
static pthread_mutex_t g_fx_mu = PTHREAD_MUTEX_INITIALIZER;
static uintptr_t g_wfx_handle; /* the FX instance keeps a pointer to this slot (FUN_02aebfd0 checks it): never moves */

__attribute__((used)) void somvr_pc_fxi(struct regs *r)
{
	int hk = -1;
	for (int i = HK_FX0; i <= HK_FX_LAST && hk < 0; i++)
		if (g_h[i].installed && g_h[i].site + 5 == r->ra)
			hk = i;
	if (hk >= 0)
		first_call(hk, atomic_fetch_add_explicit(&g_h[hk].calls, 1, memory_order_relaxed) + 1);
	char nm[96];
	rstr(r->rsi, nm, sizeof nm);
	int weapon = is_weapon_obj(r->r8) || (hk >= 0 && !strcmp(g_h[hk].name, "P_FXI_26FC738")); /* the WEAPONFX key */
	if (nm[0]) {
		pthread_mutex_lock(&g_fx_mu);
		snprintf(g_fx_last, sizeof g_fx_last, "%s", nm);
		if (weapon)
			snprintf(g_fx_lastw, sizeof g_fx_lastw, "%s", nm);
		int k = 0;
		while (k < g_fxn_n && strcmp(g_fxn[k].name, nm))
			k++;
		if (k == g_fxn_n && g_fxn_n < 96)
			snprintf(g_fxn[g_fxn_n++].name, sizeof g_fxn[0].name, "%s", nm);
		if (k < g_fxn_n) {
			g_fxn[k].n++;
			g_fxn[k].weapon += (unsigned)weapon;
		}
		pthread_mutex_unlock(&g_fx_mu);
	}
	if (!g_fxlog || !rl_ok(RL_FX))
		return;
	logf_("FX site=%s name='%s' flags=%#x parent=%#lx (%s) mh=%#lx t=%.6f", hk >= 0 ? g_h[hk].name : "?", nm,
	      (unsigned)r->rcx, (unsigned long)r->r8, weapon ? "a player weapon" : r->r8 && (r->r8 == g_pi.phobj ||
	      r->r8 == g_pi.charobj) ? "the player" : "other", (unsigned long)r->rdx, mono());
}

static void fx_table(unsigned seq)
{
	char path[1200];
	FILE *f = out_file("fxnames", seq, path, sizeof path);
	if (f)
		fprintf(f, "name\tcount\ton_player_weapon\n");
	pthread_mutex_lock(&g_fx_mu);
	for (int i = 0; i < g_fxn_n; i++) {
		if (f)
			fprintf(f, "%s\t%u\t%u\n", g_fxn[i].name, g_fxn[i].n, g_fxn[i].weapon);
		if (i < 40)
			logf_("FXNAME %d '%s' count=%u on_player_weapon=%u", i, g_fxn[i].name, g_fxn[i].n, g_fxn[i].weapon);
	}
	int n = g_fxn_n;
	pthread_mutex_unlock(&g_fx_mu);
	if (f)
		fclose(f);
	logf_("FXNAMES %d distinct FX names, last '%s', last on a player weapon '%s', file=%s", n, g_fx_last, g_fx_lastw,
	      f ? path : "(not written)");
}

static void w_weaponfx(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	char name[128], rn[128];
	int loop = argc > 2 && !strcmp(argv[2], "loop");
	pthread_mutex_lock(&g_fx_mu);
	snprintf(name, sizeof name, "%s", argc < 2 || !strcmp(argv[1], "last") ? (g_fx_lastw[0] ? g_fx_lastw : g_fx_last) : argv[1]);
	pthread_mutex_unlock(&g_fx_mu);
	if (!name[0]) {
		logf_("FAIL seq=%u weaponfx: no FX name seen yet (fxlog during a fight first, or give one)", seq);
		return;
	}
	if (!g_fn_ok[FN_FX_CSINIT] || !g_fn_ok[FN_FX_CREATE] || !g_fn_ok[FN_FX_CSFREE] || (loop && !g_fn_ok[FN_FX_RELEASE])) {
		logf_("FAIL seq=%u weaponfx: an FX function did not check out", seq);
		return;
	}
	uintptr_t inv = g_pi.inv, b, objsys = rq(G.objsys_pp), vt = rq(objsys);
	int slot = inv ? (int)r32(inv + INV_ACTIVE) : -1;
	uintptr_t item = inv_item(inv, slot), obj = item_entries(item, &b, 1) ? rq(b + IE_OBJ) : 0;
	if (!obj) {
		logf_("FAIL seq=%u weaponfx: no active weapon model object (inv %#lx slot %d item %#lx)", seq, (unsigned long)inv,
		      slot, (unsigned long)item);
		return;
	}
	if (!g_ro || !g_ro_ok[RO_OBJSYS_MH] || vt != G.vt_objsys || vt + 0x118 != g_ro[RO_OBJSYS_MH].addr ||
	    rq(vt + 0x118) != g_ro[RO_OBJSYS_MH].target) {
		logf_("FAIL seq=%u weaponfx: the object system %#lx (vtable %#lx) is not the checked one", seq,
		      (unsigned long)objsys, (unsigned long)vt);
		return;
	}
	uintptr_t mh = ((objmh_fn)g_ro[RO_OBJSYS_MH].target)(objsys, obj), mgr = c->shell ? rq(c->shell + SH_FXMGR) : 0;
	if (!mgr) {
		logf_("FAIL seq=%u weaponfx: no ClientFX manager (shell+0x80c78)", seq);
		return;
	}
	if (loop && g_wfx_handle) {
		FN(FN_FX_RELEASE, fxrel_fn)(&g_wfx_handle);
		logf_("WEAPONFX seq=%u the previous looping FX released first (handle now %#lx)", seq, (unsigned long)g_wfx_handle);
	}
	static unsigned char cs[0x400] __attribute__((aligned(16))); /* CLIENTFX_CREATESTRUCT (>= 0x1c8 bytes) */
	memset(cs, 0, sizeof cs);
	FN(FN_FX_CSINIT, fxcs_fn)(cs, name, mh, loop ? 1u : 0u, obj);
	long rc = FN(FN_FX_CREATE, fxcreate_fn)(mgr + 0x30, loop ? &g_wfx_handle : NULL, cs, 1, 0, 0);
	FN(FN_FX_CSFREE, fxfree_fn)(cs);
	logf_("WEAPONFX seq=%u '%s' %s on slot %d '%s' obj=%#lx mh=%#lx mgr=%#lx clientfx=%u rc=%ld handle=%#lx t=%.6f", seq, name,
	      loop ? "loop" : "one shot", slot, rec_name(rq(item + IT_REC), rn, sizeof rn), (unsigned long)obj,
	      (unsigned long)mh, (unsigned long)mgr, rb(G.fx_enabled), rc, (unsigned long)g_wfx_handle, mono());
	logf_("DONE seq=%u weaponfx '%s' rc=%ld", seq, name, rc);
}

static void w_weaponfx_stop(unsigned seq)
{
	uintptr_t h = g_wfx_handle;
	if (h && g_fn_ok[FN_FX_RELEASE])
		FN(FN_FX_RELEASE, fxrel_fn)(&g_wfx_handle);
	logf_("DONE seq=%u weaponfx stop: handle %#lx -> %#lx (FUN_02aebfd0)%s", seq, (unsigned long)h,
	      (unsigned long)g_wfx_handle, h ? "" : ": nothing to stop");
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P7: weapons and the dagger (re-combat-items.md 3)                                                             */

typedef void (*item_fn)(uintptr_t item);
typedef void (*itemvis_fn)(uintptr_t item, int entry);

/* the player skeleton's node name for a node hash (also from the hooks' threads: the buffer is per thread) */
static const char *node_by_hash(uint32_t h, char *out, size_t cap)
{
	static __thread uint32_t hs[4096];
	struct skel sk;
	out[0] = 0;
	if (!h || skel_get(g_pi.charobj, &sk) || !sk.hashes || rd(sk.hashes, hs, (size_t)sk.count * 4))
		return out;
	for (uint32_t i = 0; i < sk.count; i++)
		if (hs[i] == h)
			return skel_name(&sk, i, out, cap);
	return out;
}

static int dagger_guess(uintptr_t inv)
{
	char rn[128];
	for (int s = 0; s < 8; s++) {
		uintptr_t it = inv_item(inv, s);
		rec_name(it ? rq(it + IT_REC) : 0, rn, sizeof rn);
		if (strcasestr(rn, "dagger") || strcasestr(rn, "acharn") || strcasestr(rn, "knife"))
			return s;
	}
	return -1;
}

static void w_weapons(unsigned seq)
{
	uintptr_t inv = g_pi.inv;
	if (!inv) {
		logf_("FAIL seq=%u weapons: no inventory (owner %#lx)", seq, (unsigned long)g_pi.owner);
		return;
	}
	int act = (int)r32(inv + INV_ACTIVE), items = 0;
	char rn[128], n1[64], n2[64];
	for (int s = 0; s < 8; s++) {
		uintptr_t it = inv_item(inv, s), b;
		if (!it)
			continue;
		items++;
		int ne = item_entries(it, &b, 8);
		uintptr_t fb = rq(it + IT_FX), fe = rq(it + IT_FX + 8);
		logf_("WEAPON slot=%d%s item=%#lx '%s' active=%u shown=%u visen=%u entries=%d loadedfx=%ld", s, s == act ? "*" : "",
		      (unsigned long)it, rec_name(rq(it + IT_REC), rn, sizeof rn), rb(it + IT_ACTIVE), rb(it + IT_SHOWN),
		      rb(it + IT_VISEN), ne, fe > fb ? (long)((fe - fb) / 8) : 0L);
		for (int k = 0; k < ne; k++) {
			uintptr_t e = b + (uintptr_t)k * IT_ESIZE, o = rq(e + IE_OBJ);
			uint32_t an = r32(e + IE_ACTNODE), in = r32(e + IE_INACTNODE);
			float op[3] = { 0, 0, 0 };
			if (o)
				obj_pos(o, op);
			logf_("WEAPON slot=%d entry=%d obj=%#lx pos=%.1f,%.1f,%.1f active_node=%#x '%s' inactive_node=%#x '%s' style=%u",
			      s, k, (unsigned long)o, op[0], op[1], op[2], an, node_by_hash(an, n1, sizeof n1), in,
			      node_by_hash(in, n2, sizeof n2), rb(e + IE_SOCKET));
		}
	}
	int dg = dagger_guess(inv);
	logf_("DONE seq=%u weapons: %d items, active slot %d, dagger slot %d (by record name)", seq, items, act, dg);
}

static struct { int on, slot, n; unsigned seq; uintptr_t item; uint32_t saved[8]; uint8_t shown; } g_dag;
/* P8: the dagger measured again 3 ticks after the move (P7's `DAGGER hand` line measured it in the same frame, before
 * the game moved the model object: 101.8 cm) */
static struct { uint64_t tick; unsigned seq; uintptr_t item; uint32_t node; char what[16]; } g_dagm;
static void dag_later(const char *what, unsigned seq, uintptr_t item, uint32_t node)
{
	g_dagm.tick = g_ticks_all + 3;
	g_dagm.seq = seq;
	g_dagm.item = item;
	g_dagm.node = node;
	snprintf(g_dagm.what, sizeof g_dagm.what, "%s", what);
}

static void dag_pos(const char *what, unsigned seq, uintptr_t item, uint32_t node)
{
	uintptr_t b;
	float op[3] = { 0, 0, 0 }, nx[8] = { 0 };
	uintptr_t o = item_entries(item, &b, 1) ? rq(b + IE_OBJ) : 0;
	if (o)
		obj_pos(o, op);
	int nok = node && node_xf(g_pi.charobj, node, nx) == 0;
	char nn[64];
	logf_("DAGGER seq=%u %s: model object %#lx at %.1f,%.1f,%.1f; node %#x '%s' at %.1f,%.1f,%.1f: %.1f cm apart", seq, what,
	      (unsigned long)o, op[0], op[1], op[2], node, node_by_hash(node, nn, sizeof nn), nx[0], nx[1], nx[2],
	      nok && o ? dist3(op, nx) : -1.0f);
}

static void w_dagger(unsigned seq, int argc, char **argv)
{
	int back = argc > 1 && !strcmp(argv[1], "back"), slot = -1;
	for (int i = 2; i < argc; i++)
		if (!strncmp(argv[i], "slot=", 5))
			slot = (int)num(argv[i] + 5, -1);
	if (!g_fn_ok[FN_ITEM_REATTACH] || !g_fn_ok[FN_ITEM_VISIBLE]) {
		logf_("FAIL seq=%u dagger: FUN_0231e2e0/FUN_0231e700 did not check out", seq);
		return;
	}
	if (back) {
		if (!g_dag.on) {
			logf_("DONE seq=%u dagger back: nothing to restore", seq);
			return;
		}
		uintptr_t b;
		int ne = item_entries(g_dag.item, &b, 8), kept = 0;
		uint32_t lw = name_hash("L_Weapon");
		for (int k = 0; k < ne && k < g_dag.n; k++) {
			uintptr_t e = b + (uintptr_t)k * IT_ESIZE;
			if (r32(e + IE_INACTNODE) == lw)
				memcpy((void *)(e + IE_INACTNODE), &g_dag.saved[k], 4);
			else
				kept++;
		}
		memcpy((void *)(g_dag.item + IT_SHOWN), &g_dag.shown, 1);
		FN(FN_ITEM_REATTACH, item_fn)(g_dag.item);
		FN(FN_ITEM_VISIBLE, itemvis_fn)(g_dag.item, -1);
		dag_pos("back", seq, g_dag.item, g_dag.saved[0]);
		dag_later("back", seq, g_dag.item, g_dag.saved[0]);
		logf_("DONE seq=%u dagger back: slot %d restored (%d entries the game had changed meanwhile), shown %u", seq, g_dag.slot,
		      kept, g_dag.shown);
		g_dag.on = 0;
		return;
	}
	if (g_dag.on) {
		logf_("FAIL seq=%u dagger hand: already in the hand (seq %u)", seq, g_dag.seq);
		return;
	}
	uintptr_t inv = g_pi.inv, b;
	if (slot < 0)
		slot = dagger_guess(inv);
	if (slot < 0)
		slot = 1; /* ACTIVATE_MELEEAUX (re-combat-items.md 3.1) */
	uintptr_t item = inv_item(inv, slot);
	int ne = item_entries(item, &b, 8);
	uint32_t lw = name_hash("L_Weapon");
	char nn[64], rn[128];
	if (!ne || !node_by_hash(lw, nn, sizeof nn)[0]) {
		logf_("FAIL seq=%u dagger hand: slot %d item %#lx with %d model entries; L_Weapon %#x %s in the player skeleton", seq,
		      slot, (unsigned long)item, ne, lw, nn[0] ? "found" : "NOT found");
		return;
	}
	memset(&g_dag, 0, sizeof g_dag);
	g_dag.seq = seq;
	g_dag.slot = slot;
	g_dag.item = item;
	g_dag.n = ne;
	g_dag.shown = rb(item + IT_SHOWN);
	dag_pos("before", seq, item, r32(b + IE_INACTNODE));
	for (int k = 0; k < ne; k++) {
		uintptr_t e = b + (uintptr_t)k * IT_ESIZE;
		g_dag.saved[k] = r32(e + IE_INACTNODE);
		memcpy((void *)(e + IE_INACTNODE), &lw, 4);
	}
	g_dag.on = 1;
	FN(FN_ITEM_REATTACH, item_fn)(item);
	if (!g_dag.shown) {
		uint8_t one = 1;
		memcpy((void *)(item + IT_SHOWN), &one, 1);
		FN(FN_ITEM_VISIBLE, itemvis_fn)(item, -1);
	}
	dag_pos("hand", seq, item, lw);
	dag_later("hand", seq, item, lw);
	logf_("DONE seq=%u dagger hand: slot %d '%s' (%s) %d entries inactive node %#x -> L_Weapon %#x, shown %u -> %u, active=%u",
	      seq, slot, rec_name(rq(item + IT_REC), rn, sizeof rn), dagger_guess(inv) == slot ? "by name" : "slot 1 / slot=",
	      ne, g_dag.saved[0], lw, g_dag.shown, rb(item + IT_SHOWN), rb(item + IT_ACTIVE));
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P7: database dumps: syncinfo (re-combat-items.md 4) and `attacks full` (re-combat-vr.md 4)                    */

static void sync_one(uintptr_t rec, int *with_sync)
{
	char nm[160], an[128], sn[128], st[300], ps[200], aoe[300], isb[32], v[400], fn[96];
	uintptr_t anim = db_recf(rec, G.sch_attack, "AnimationData"), sync = anim ? db_recf(anim, G.sch_anim, "Sync") : 0;
	uintptr_t ad = db_recf(rec, G.sch_attack, "AttackData"), post = db_recf(rec, G.sch_attack, "AttackPostData");
	*with_sync += sync != 0;
	logf_("SYNCINFO '%s' rec=%#lx AnimationData=%#lx '%s' Sync=%#lx '%s' AttackData=%#lx RegisterStimulus=%s "
	      "PersistentStimulus=%s IsBlock=%s AOEOnHit=%s", rec_name(rec, nm, sizeof nm), (unsigned long)rec,
	      (unsigned long)anim, rec_name(anim, an, sizeof an), (unsigned long)sync, rec_name(sync, sn, sizeof sn),
	      (unsigned long)ad, db_str(ad, G.sch_attackdata, "RegisterStimulus", st, sizeof st),
	      db_str(ad, G.sch_attackdata, "PersistentStimulus", ps, sizeof ps), db_str(ad, G.sch_attackdata, "IsBlock", isb, sizeof isb),
	      db_str(post, G.sch_post, "AOEOnHit", aoe, sizeof aoe));
	for (int i = 0; sync && i < SCH_MAX; i++) {
		struct dbv d;
		if (!ident_ok(sch_name(G.sch_sync, i, fn, sizeof fn)))
			break;
		if (db_get(sync, sch_hash(G.sch_sync, i), &d))
			continue;
		db_fmt(&d, v, sizeof v);
		logf_("SYNC '%s' %d %s = %s", nm, i, fn, v);
	}
}

static void w_syncinfo(unsigned seq, int argc, char **argv)
{
	int n = 0, with = 0;
	char name[256];
	for (int a = 1; a < argc; a++) {
		size_t l = strlen(argv[a]);
		if (l > 1 && argv[a][l - 1] == '*') {
			uintptr_t cat = rq(G.atkcat_pp), base = rq(cat + CAT_RECS);
			uint32_t cnt = r32(cat + CAT_COUNT);
			int m = 0;
			for (uint32_t i = 0; base && i < cnt && i < 200000 && m < 24; i++) {
				uintptr_t rec = base + (uintptr_t)i * DBREC_SIZE;
				if (!strncmp(rec_name(rec, name, sizeof name), argv[a], l - 1)) {
					sync_one(rec, &with);
					m++, n++;
				}
			}
			if (!m)
				logf_("SYNCINFO '%s': no record with that prefix", argv[a]);
			continue;
		}
		uintptr_t rec = g_fn_ok[FN_ATK_BYNAME] ? atk_valid(FN(FN_ATK_BYNAME, byname_fn)(argv[a])) : 0;
		if (!rec) {
			logf_("SYNCINFO '%s': no such Combat/Attack record", argv[a]);
			continue;
		}
		sync_one(rec, &with);
		n++;
	}
	logf_("DONE seq=%u syncinfo: %d records, %d with AnimationData.Sync (schemas: Sync field %d, AnimationData %d)", seq, n,
	      with, sch_find(G.sch_anim, "Sync"), sch_find(G.sch_attack, "AnimationData"));
}

static void w_attacks_full(unsigned seq)
{
	uintptr_t cat = rq(G.atkcat_pp), base = rq(cat + CAT_RECS);
	uint32_t n = r32(cat + CAT_COUNT);
	if (!cat || !base || n == 0 || n > 200000) {
		logf_("DONE seq=%u attacks full: no Combat/Attack category", seq);
		return;
	}
	char path[1200], nm[256], an[128], sn[128], st[200], ps[128], isb[16], pj[16], dc[32], aoe[200];
	FILE *f = out_file("attacks-full", 0, path, sizeof path);
	if (f)
		fprintf(f, "index\tname\tanimation\tsync\tregister_stimulus\tpersistent_stimulus\tis_block\tcan_be_hit_by_"
			   "projectiles\tdodge_chance\taoe_on_hit\n");
	double t0 = mono();
	int nsync = 0, nstim = 0, npcstim = 0, nblock = 0, shown = 0;
	for (uint32_t i = 0; i < n; i++) {
		uintptr_t rec = base + (uintptr_t)i * DBREC_SIZE;
		uintptr_t anim = db_recf(rec, G.sch_attack, "AnimationData"), sync = anim ? db_recf(anim, G.sch_anim, "Sync") : 0;
		uintptr_t ad = db_recf(rec, G.sch_attack, "AttackData"), post = db_recf(rec, G.sch_attack, "AttackPostData");
		uintptr_t aid = db_recf(rec, G.sch_attack, "AIData");
		rec_name(rec, nm, sizeof nm);
		db_str(ad, G.sch_attackdata, "RegisterStimulus", st, sizeof st);
		db_str(ad, G.sch_attackdata, "PersistentStimulus", ps, sizeof ps);
		db_str(ad, G.sch_attackdata, "IsBlock", isb, sizeof isb);
		db_str(ad, G.sch_attackdata, "CanBeHitByProjectiles", pj, sizeof pj);
		db_str(aid, G.sch_aidata, "DodgeChance", dc, sizeof dc);
		db_str(post, G.sch_post, "AOEOnHit", aoe, sizeof aoe);
		int stim = st[0] != '-' || ps[0] != '-';
		nsync += sync != 0;
		nstim += stim;
		npcstim += stim && !strncmp(nm, "PC_", 3);
		nblock += !strcmp(isb, "1");
		if (f)
			fprintf(f, "%u\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", i, nm, rec_name(anim, an, sizeof an),
				rec_name(sync, sn, sizeof sn), st, ps, isb, pj, dc, aoe);
		if (stim && !strncmp(nm, "PC_", 3) && shown++ < 12)
			logf_("ATTACKSTIM %u '%s' RegisterStimulus=%s PersistentStimulus=%s", i, nm, st, ps);
	}
	if (f)
		fclose(f);
	logf_("DONE seq=%u attacks full: %u records in %.0f ms: %d with AnimationData.Sync, %d with stimuli (%d of them PC_), %d "
	      "IsBlock; file=%s", seq, n, (mono() - t0) * 1000.0, nsync, nstim, npcstim, nblock, f ? path : "(not written)");
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P7: HUD (re-hud.md 3, 4, 2): navdump, prompts, uiscene                                                        */

typedef void (*navpos_fn)(uintptr_t marker, float *out);

static void w_navdump(unsigned seq)
{
	static const char *const at[] = { "IconMovieClip", "IconMovieClipPC", "IconTexture", "ActionID", "RenderOffScreen",
					  "DisplayRange" };
	char path[1200], rn[128], val[6][160];
	FILE *f = out_file("nav", seq, path, sizeof path);
	if (f)
		fprintf(f, "index\tmarker\trecord\tshown1\tshown2\tactive\tenabled\ttarget\tnode\tx\ty\tz\tdist_m\tIconMovieClip\t"
			   "IconMovieClipPC\tIconTexture\tActionID\tRenderOffScreen\tDisplayRange\n");
	uintptr_t node = rq(G.nav_head + 8);
	int n = 0, shown = 0, posd = 0;
	while (node && node != G.nav_head && n < 1024) {
		uintptr_t m = node - NAV_NODE, R = rq(m + NM_REC), tgt = rq(m + NM_TGT) ? rq(m + NM_TGT) : rq(m + NM_TGT2);
		uint8_t s1 = rb(m + NM_SHOWN1), s2 = rb(m + NM_SHOWN2), a = rb(m + NM_ACTIVE), e = rb(m + NM_ENABLED);
		float pos[3] = { 0, 0, 0 };
		/* the position only as FUN_00ec4a50 asks for it: active, enabled, a record and one of its objects */
		int can = a && e && R && (rq(m + 0x260) || rq(m + 0x268) || rq(m + 0x270) || rq(m + 0xb0));
		if (can && g_fn_ok[FN_NAV_POS]) {
			FN(FN_NAV_POS, navpos_fn)(m, pos);
			posd++;
		}
		for (int k = 0; k < 6; k++) {
			struct dbv v;
			if (db_field(R, G.sch_nav, at[k], &v)) {
				snprintf(val[k], sizeof val[k], "-");
			} else if (k == 3 && v.type != 0xa && v.type != 4 && v.type != 2 && (uint32_t)v.v[0] < NCMD) {
				cfmt(val[k], sizeof val[k], "%u %s", (unsigned)(uint32_t)v.v[0], g_cmd_names[(uint32_t)v.v[0]]);
			} else {
				db_fmt(&v, val[k], sizeof val[k]);
			}
		}
		shown += s1 || s2;
		rec_name(R, rn, sizeof rn);
		float d = can ? dist3(pos, g_pi.pos) / 100.0f : -1.0f;
		if (f)
			fprintf(f, "%d\t%#lx\t%s\t%u\t%u\t%u\t%u\t%#lx\t%d\t%.1f\t%.1f\t%.1f\t%.1f\t%s\t%s\t%s\t%s\t%s\t%s\n", n,
				(unsigned long)m, rn, s1, s2, a, e, (unsigned long)tgt, (int)r32(m + NM_NODE), pos[0], pos[1], pos[2], d,
				val[0], val[1], val[2], val[3], val[4], val[5]);
		if (n < 40)
			logf_("NAV %d m=%#lx '%s' shown=%u/%u active=%u enabled=%u target=%#lx node=%d pos=%.1f,%.1f,%.1f dist=%.1f m "
			      "clip=%s clipPC=%s texture=%s action=%s offscreen=%s range=%s", n, (unsigned long)m, rn, s1, s2, a, e,
			      (unsigned long)tgt, (int)r32(m + NM_NODE), pos[0], pos[1], pos[2], d, val[0], val[1], val[2], val[3],
			      val[4], val[5]);
		n++;
		node = rq(node + 8);
	}
	if (f)
		fclose(f);
	logf_("DONE seq=%u navdump: %d markers (%d shown this frame, %d positions asked), schema IconMovieClip=%d ActionID=%d, "
	      "file=%s", seq, n, shown, posd, sch_find(G.sch_nav, "IconMovieClip"), sch_find(G.sch_nav, "ActionID"),
	      f ? path : "(not written)");
}

static int g_prompts_watch;
static uint64_t g_prompts_sig;
static _Atomic int g_prompt_snap; /* P8: a counter prompt at the player: the next tick logs the prompt entries */

/* the live entries of the three prompt diamonds (re-hud.md 4.3); -1 = no HUD */
static int prompts_walk(const struct ctx *c, int logit, uint64_t *sig)
{
	static const char *const el[3] = { "standard", "quicktime", "keyboard" };
	uintptr_t pui = c->player ? rq(c->player + P_UI) : 0, base = pui ? rq(pui + 0xb0) : 0, hud = base ? base + 0x16e0 : 0;
	*sig = 1469598103934665603ull;
	if (!hud)
		return -1;
	int live = 0;
	char kn[96], st[96], ic[96];
	for (int k = 0; k < 3; k++) {
		uintptr_t pr = rq(hud + HUD_PROMPTS + 8u * (unsigned)k), dm = pr ? rq(pr + PR_DIAMOND) : 0, vt = dm ? rq(dm) : 0;
		uintptr_t slots = dm ? rq(dm + 0x18) : 0;
		uint32_t ns = dm ? r32(dm + 0x20) : 0;
		for (uint32_t s = 0; slots && s < ns && s < 16; s++) {
			uintptr_t sl = slots + s * 0x90u, b = rq(sl + 0x70), e = rq(sl + 0x78);
			if (!b || e < b || (e - b) % 0x38)
				continue;
			for (uintptr_t en = b; en < e && en < b + 32 * 0x38; en += 0x38) {
				int act = (int)r32(en + 0x28);
				if (act == NCMD)
					continue; /* 349 = a free entry */
				live++;
				uint32_t cat = r32(en + 0x2c);
				*sig = (*sig ^ (uint64_t)(uint32_t)act ^ ((uint64_t)cat << 32) ^ rq(en) ^ ((uint64_t)k << 60)) * 1099511628211ull;
				if (logit)
					logf_("PROMPT %s slot=%u %s action=%d %s key='%s' string='%s' icon='%s' category=%u flags=%02x%02x%02x",
					      el[k], s, vt == G.vt_diamond_pad ? "pad" : vt == G.vt_diamond_kb ? "keyboard" : "?", act,
					      act >= 0 && act < NCMD ? g_cmd_names[act] : "?", rec_name(rq(en), kn, sizeof kn),
					      rstr(rq(en + 0x10), st, sizeof st), rstr(rq(en + 0x20), ic, sizeof ic), cat, rb(en + 0x30),
					      rb(en + 0x31), rb(en + 0x32));
			}
		}
	}
	return live;
}

static void w_prompts(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	uint64_t sig;
	if (argc > 1 && !strcmp(argv[1], "watch")) {
		g_prompts_watch = argc > 2 && !strcmp(argv[2], "on");
		g_prompts_sig = 0;
		logf_("DONE seq=%u prompts watch %s", seq, g_prompts_watch ? "on" : "off");
		return;
	}
	int live = prompts_walk(c, 1, &sig);
	logf_("DONE seq=%u prompts: %d live entries%s", seq, live < 0 ? 0 : live, live < 0 ? " (no HUD: player UI +0xb0 null)" : "");
}

static void prompts_tick(const struct ctx *c)
{
	uint64_t sig;
	int live = prompts_walk(c, 0, &sig);
	if (live < 0 || sig == g_prompts_sig)
		return;
	g_prompts_sig = sig;
	if (!rl_ok(RL_PROMPT))
		return;
	logf_("PROMPTS changed: %d live t=%.6f", live, mono());
	prompts_walk(c, 1, &sig);
}

static _Atomic int g_uisc_budget = 3; /* the first 3 UI scene views of the boot are logged anyway */

static void w_uiscene(const struct ctx *c, unsigned seq)
{
	if (!c->im) {
		logf_("FAIL seq=%u uiscene: no interface manager", seq);
		return;
	}
	uintptr_t m = c->im + IM_UISCENE, cam = rq(m + 0x80), co = cam ? rq(cam + 8) : 0;
	float pos[3] = { 0, 0, 0 }, q[4] = { 0, 0, 0, 0 };
	double bs = 0, bd = 0;
	if (co) {
		rvec(co + 8, pos, 3);
		rvec(co + 0x14, q, 4);
	}
	rd(m + 0x240, &bs, 8);
	rd(m + 0x248, &bd, 8);
	logf_("UISCENE mgr=%#lx (vtable %#lx%s) level=%#lx ctx=%#lx camera=%#lx (object %#lx pos=%.1f,%.1f,%.1f quat=%.3f,%.3f,%.3f,%.3f) "
	      "next=%#lx blend start=%g duration=%g flags=%u/%u; ifs=%d menus=%d modal=%u", (unsigned long)m, (unsigned long)rq(m),
	      rq(m) == G.vt_uiscene ? ": CUISceneMgr" : ": UNEXPECTED", (unsigned long)rq(m + 0xd0), (unsigned long)rq(m + 0xf8),
	      (unsigned long)cam, (unsigned long)co, pos[0], pos[1], pos[2], q[0], q[1], q[2], q[3], (unsigned long)rq(m + 0xb8),
	      bs, bd, rb(m + 0x268), rb(m + 0x269), c->ifs, c->menus, rb(c->im + 0x2a48));
	atomic_store(&g_uisc_budget, 20);
	logf_("DONE seq=%u uiscene: the next 20 UI scene views are logged (hook P_UISCENE %s)", seq,
	      g_h[HK_UISCENE].installed ? "on" : "NOT installed");
}

/* FUN_009f2550(renderer, view) from FUN_00c1e370: the 0x328-byte UI scene view (re-hud.md 2.2) */
__attribute__((used)) void somvr_pc_uiscene(struct regs *r)
{
	first_call(HK_UISCENE, atomic_fetch_add_explicit(&g_h[HK_UISCENE].calls, 1, memory_order_relaxed) + 1);
	if (atomic_load_explicit(&g_uisc_budget, memory_order_relaxed) <= 0)
		return;
	atomic_fetch_sub(&g_uisc_budget, 1);
	float v[18] = { 0 };
	rvec(r->rsi, v, 18);
	logf_("UISCENEVIEW renderer=%#lx view=%#lx pos=%.1f,%.1f,%.1f quat=%.3f,%.3f,%.3f,%.3f fovX=%.2f fovY=%.2f deg near=%g "
	      "far=%g dof=%u tid=%d t=%.6f", (unsigned long)r->rdi, (unsigned long)r->rsi, v[0], v[1], v[2], v[3], v[4], v[5], v[6],
	      v[7] * 180.0f / (float)M_PI, v[8] * 180.0f / (float)M_PI, (double)v[16], (double)v[17], rb(r->rsi + 0x164), my_tid(),
	      mono());
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P7: sounds (re-combat-items.md 5): soundlog over AK PostEvent's two callers, sound = CSoundMgr::vf_13 or our    */
/* own Wwise object at the right hand                                                                             */

typedef uint32_t (*sndg_fn)(uintptr_t mgr, const char *name);
typedef uint64_t (*sndreg_fn)(uintptr_t wrap, uint32_t obj, const char *name);
typedef uint64_t (*sndpos_fn)(uintptr_t wrap, uint32_t obj, const float *pos, const float *front);
typedef uint32_t (*sndpost_fn)(uintptr_t wrap, uint32_t obj, const char *name);

#define SND_OBJ 0x50a70001u /* the probe's own Wwise game object id (the game's scheme is not traced) */
static int g_soundlog, g_snd_reg;
static __thread int t_snd_own;
static char g_snd_last[96];
static struct { char name[96]; unsigned n; } g_sndn[64];
static int g_sndn_n;
static pthread_mutex_t g_snd_mu = PTHREAD_MUTEX_INITIALIZER;

/* AK::SoundEngine::PostEvent(name, game object, ...) from FUN_008c9840 / FUN_008c9870 */
__attribute__((used)) void somvr_pc_snd(struct regs *r)
{
	int hk = g_h[HK_SND_POST_CB].installed && r->ra == g_h[HK_SND_POST_CB].site + 5 ? HK_SND_POST_CB : HK_SND_POST;
	first_call(hk, atomic_fetch_add_explicit(&g_h[hk].calls, 1, memory_order_relaxed) + 1);
	char nm[96];
	rstr(r->rdi, nm, sizeof nm);
	if (!t_snd_own && nm[0]) {
		pthread_mutex_lock(&g_snd_mu);
		snprintf(g_snd_last, sizeof g_snd_last, "%s", nm);
		int k = 0;
		while (k < g_sndn_n && strcmp(g_sndn[k].name, nm))
			k++;
		if (k == g_sndn_n && g_sndn_n < 64)
			snprintf(g_sndn[g_sndn_n++].name, sizeof g_sndn[0].name, "%s", nm);
		if (k < g_sndn_n)
			g_sndn[k].n++;
		pthread_mutex_unlock(&g_snd_mu);
	}
	if (!g_soundlog || !rl_ok(RL_SND))
		return;
	logf_("SOUND %s event='%s' obj=%#x%s tid=%d t=%.6f", hk == HK_SND_POST_CB ? "post+callback" : "post", nm,
	      (unsigned)r->rsi, t_snd_own ? " (the probe's)" : "", my_tid(), mono());
}

static void snd_table(void)
{
	pthread_mutex_lock(&g_snd_mu);
	for (int i = 0; i < g_sndn_n && i < 40; i++)
		logf_("SOUNDNAME %d '%s' count=%u", i, g_sndn[i].name, g_sndn[i].n);
	logf_("SOUNDNAMES %d distinct event names, last '%s'", g_sndn_n, g_snd_last);
	pthread_mutex_unlock(&g_snd_mu);
}

static void w_sound(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	char name[128];
	pthread_mutex_lock(&g_snd_mu);
	snprintf(name, sizeof name, "%s", argc < 2 || !strcmp(argv[1], "last") ? g_snd_last : argv[1]);
	pthread_mutex_unlock(&g_snd_mu);
	int hand = argc > 2 && !strcmp(argv[2], "hand");
	if (!name[0]) {
		logf_("FAIL seq=%u sound: no event name (none seen yet: soundlog first, or give one)", seq);
		return;
	}
	uint32_t id;
	if (!hand) {
		uintptr_t mgr = rq(G.snd_pp), vt = rq(mgr);
		if (!g_ro || !g_ro_ok[RO_SND_GLOBAL] || vt != G.vt_snd || vt + 13 * 8 != g_ro[RO_SND_GLOBAL].addr ||
		    rq(vt + 13 * 8) != g_ro[RO_SND_GLOBAL].target) {
			logf_("FAIL seq=%u sound: the sound manager %#lx (vtable %#lx) is not the checked CSoundMgr", seq,
			      (unsigned long)mgr, (unsigned long)vt);
			return;
		}
		t_snd_own = 1;
		id = ((sndg_fn)g_ro[RO_SND_GLOBAL].target)(mgr, name);
		t_snd_own = 0;
		logf_("DONE seq=%u sound '%s' global (CSoundMgr::vf_13, the 2D emitter): playing id %u (0 = not posted: unknown "
		      "event, bank not loaded or the manager off: mgr+8 = %u)", seq, name, id, rb(mgr + 8));
		return;
	}
	if (!g_fn_ok[FN_SND_REGOBJ] || !g_fn_ok[FN_SND_SETPOS] || !g_fn_ok[FN_SND_POST]) {
		logf_("FAIL seq=%u sound hand: a Wwise wrapper function did not check out", seq);
		return;
	}
	if (!g_snd_reg) {
		FN(FN_SND_REGOBJ, sndreg_fn)(G.snd_wrap, SND_OBJ, "somvr_probe_hand");
		g_snd_reg = 1;
	}
	float pos[8] = { 0 }, front[3], fz[3] = { 0, 0, 1 };
	uint32_t node = 0xffffffffu;
	uintptr_t o = char_obj(c);
	int ok = o && g_fn_ok[FN_GETNODE] && !(FN(FN_GETNODE, getnode_fn)(model_iface(), o, "R_Hand", &node) & 0xff) &&
		 node_xf(o, node, pos) == 0;
	if (!ok) {
		memcpy(pos, g_pi.pos, 12);
		pos[1] += 100.0f;
	}
	qrot(g_pi.rot, fz, front);
	FN(FN_SND_SETPOS, sndpos_fn)(G.snd_wrap, SND_OBJ, pos, front);
	t_snd_own = 1;
	id = FN(FN_SND_POST, sndpost_fn)(G.snd_wrap, SND_OBJ, name);
	t_snd_own = 0;
	logf_("DONE seq=%u sound '%s' at %s %.1f,%.1f,%.1f (object %#x): playing id %u", seq, name, ok ? "R_Hand" : "the root + 1 m",
	      pos[0], pos[1], pos[2], SND_OBJ, id);
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P7: the render size (re-resolution.md 1): res, restall (P_RES3D: the 3D surface's height = the ScreenHeight cvar)  */

static int g_restall;
static _Atomic int g_res3d_calls;
static _Atomic uint32_t g_res3d_last[2]; /* the height computed, the height passed */

/* FUN_0073fce0(dev, &dev+0xd8, w, h, windowed, 0) for the 3D surface (render thread, inside a mode set; no float args) */
__attribute__((used)) void somvr_pc_res3d(struct regs *r)
{
	first_call(HK_RES3D, atomic_fetch_add_explicit(&g_h[HK_RES3D].calls, 1, memory_order_relaxed) + 1);
	uint64_t h = r->rcx;
	uint32_t sh = r32(G.cvar_sh);
	if (g_restall && sh >= 1 && sh <= 16384)
		h = sh;
	atomic_store(&g_res3d_last[0], (uint32_t)r->rcx);
	atomic_store(&g_res3d_last[1], (uint32_t)h);
	atomic_fetch_add(&g_res3d_calls, 1);
	r->rax = ((six_fn)g_h[HK_RES3D].orig)(r->rdi, r->rsi, r->rdx, h, r->r8, r->r9);
}

static void w_res(const struct ctx *c, unsigned seq)
{
	uintptr_t d = G.device, rt = c->shell ? rq(c->shell + SH_RT3D) : 0;
	int ui[4] = { 0 }, d3[4] = { 0 };
	if (c->shell) {
		rd(c->shell + SH_UIRECT, ui, 16);
		rd(c->shell + SH_3DRECT, d3, 16);
	}
	logf_("RES cvars ScreenWidth=%u ScreenHeight=%u lock=%u; device back buffer %ux%u UI %ux%u 3D %ux%u, textures UI %#lx 3D "
	      "%#lx (%s); 3D rect %d,%d,%d,%d%s UI rect %d,%d,%d,%d; rt3D %#lx %ux%u flags %#x; P_RES3D %d calls (last height "
	      "%u -> %u), restall %s", r32(G.cvar_sw), r32(G.cvar_sh), r32(G.aspect_lock), r32(d + 0x48), r32(d + 0x4c),
	      r32(d + 0x90), r32(d + 0x94), r32(d + 0xd8), r32(d + 0xdc), (unsigned long)rq(d + 0x1b8), (unsigned long)rq(d + 0x1c0),
	      rq(d + 0x1b8) == rq(d + 0x1c0) ? "the same" : "separate", d3[0], d3[1], d3[2], d3[3], d3[1] ? " (LETTERBOXED)" : "",
	      ui[0], ui[1], ui[2], ui[3], (unsigned long)rt, rt ? r32(rt + 0xc) : 0, rt ? r32(rt + 0x10) : 0, rt ? r32(rt + 8) : 0,
	      atomic_load(&g_res3d_calls), atomic_load(&g_res3d_last[0]), atomic_load(&g_res3d_last[1]), g_restall ? "on" : "off");
	if (c->cam) {
		float v[4] = { 0 }, f[2] = { 0 };
		rvec(c->cam + 0x3a0, v, 4);
		rvec(c->cam + 0x3fc, f, 2);
		logf_("RES camera cam+0x3a0=%g,%g,%g,%g cam+0x3fc=%g,%g", (double)v[0], (double)v[1], (double)v[2], (double)v[3],
		      (double)f[0], (double)f[1]);
	}
	logf_("DONE seq=%u res", seq);
}

static void w_restall(unsigned seq, const char *arg)
{
	g_restall = arg && !strcmp(arg, "on");
	logf_("DONE seq=%u restall %s: the next mode set's 3D height = %s (hook P_RES3D %s; then `exec SetResolution W H`)", seq,
	      g_restall ? "on" : "off", g_restall ? "the ScreenHeight cvar" : "the game's (the back buffer's aspect)",
	      g_h[HK_RES3D].installed ? "on" : "NOT installed");
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P7: one piece hidden by index (re-resolution.md 2.4: the head pieces have no names) and the traversal state       */
/* (re-traversal.md 1.6)                                                                                         */

static struct { int on; uint32_t piece; uintptr_t obj; uint64_t all; } g_ph; /* all: the pieces `piecehide all` hid */
static struct { uint64_t tick; unsigned seq; uintptr_t obj; char what[40]; } g_phm; /* the mask, read 2 ticks later */

static void ph_mlater(const char *what, uintptr_t obj, unsigned seq)
{
	snprintf(g_phm.what, sizeof g_phm.what, "%s", what);
	g_phm.obj = obj;
	g_phm.seq = seq;
	g_phm.tick = g_ticks_all + 2;
}

/* undo the last piecehide (one piece, or the set `all` hid: only the pieces the game had shown) */
static void ph_undo(uintptr_t m, uintptr_t obj)
{
	if (!g_ph.on || g_ph.obj != obj)
		return;
	if (g_ph.all) {
		for (uint32_t p = 0; p < 64; p++)
			if (g_ph.all >> p & 1)
				FN(FN_HIDEPIECE, hidepiece_fn)(m, obj, p, 0);
	} else {
		FN(FN_HIDEPIECE, hidepiece_fn)(m, obj, g_ph.piece, 0);
	}
}

static void w_piecehide(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	uintptr_t obj = char_obj(c), m = model_iface(), sm = obj ? obj_model(obj) : 0;
	uint32_t cnt = 0;
	char md[400];
	if (!obj || !g_fn_ok[FN_HIDEPIECE] || !g_fn_ok[FN_NUMPIECES] || FN(FN_NUMPIECES, numpieces_fn)(m, obj, &cnt) & 0xff) {
		logf_("FAIL seq=%u piecehide: pieces unavailable (obj=%#lx)", seq, (unsigned long)obj);
		return;
	}
	ph_undo(m, obj); /* one at a time: show the last one first */
	int was = g_ph.on && g_ph.obj == obj, wasall = was && g_ph.all;
	g_ph.on = 0;
	g_ph.all = 0;
	if (argc < 2 || !strcmp(argv[1], "off")) {
		logf_("DONE seq=%u piecehide off (%s %s)", seq, wasall ? "the set of `all`" : "piece", was ? "shown again" :
		      "nothing was hidden");
		if (was)
			ph_mlater("off", obj, seq);
		return;
	}
	if (!strcmp(argv[1], "all")) { /* is the visible body this model at all? (P7: no single piece changed anything) */
		uint32_t mk[2] = { 0, 0 };
		if (sm)
			rd(sm + SM_MASK, mk, 8);
		uint64_t gm = (uint64_t)mk[1] << 32 | mk[0];
		for (uint32_t p = 0; p < cnt && p < 64; p++)
			if (!(gm >> p & 1)) {
				FN(FN_HIDEPIECE, hidepiece_fn)(m, obj, p, 1);
				g_ph.all |= 1ull << p;
			}
		g_ph.on = 1;
		g_ph.obj = obj;
		logf_("DONE seq=%u piecehide all: %d of %u pieces hidden (the others were hidden by the game); %s", seq,
		      __builtin_popcountll(g_ph.all), cnt, sm ? rmask_desc(sm, md, sizeof md) : "(no model)");
		ph_mlater("all", obj, seq);
		return;
	}
	if (!is_num(argv[1]) || num(argv[1], -1) < 0 || num(argv[1], -1) >= cnt) {
		logf_("FAIL seq=%u piecehide: want a piece index 0..%u, all or off", seq, cnt ? cnt - 1 : 0);
		return;
	}
	g_ph.piece = (uint32_t)num(argv[1], 0);
	g_ph.obj = obj;
	uint64_t rr = FN(FN_HIDEPIECE, hidepiece_fn)(m, obj, g_ph.piece, 1);
	g_ph.on = 1;
	char nm[128] = "";
	if (g_fn_ok[FN_PIECENAME])
		FN(FN_PIECENAME, piecename_fn)(m, obj, g_ph.piece, nm, sizeof nm);
	nm[127] = 0;
	logf_("DONE seq=%u piecehide %u of %u ('%s') result=%u%s; %s", seq, g_ph.piece, cnt, nm, (unsigned)(rr & 0xff),
	      was ? " (the last one shown again)" : "", sm ? rmask_desc(sm, md, sizeof md) : "(no model)");
	char w[24];
	snprintf(w, sizeof w, "piece %u", g_ph.piece);
	ph_mlater(w, obj, seq);
}

typedef uint64_t (*supported_fn)(uintptr_t player);
static struct { int on; unsigned seq, lines; double until, last_t; uint64_t sig; } g_trav;

static void w_trav(const struct ctx *c, unsigned seq, const char *arg)
{
	double secs = num(arg, 20);
	secs = secs < 0.5 ? 0.5 : secs > 120 ? 120 : secs;
	memset(&g_trav, 0, sizeof g_trav);
	g_trav.on = 1;
	g_trav.seq = seq;
	g_trav.until = mono() + secs;
	logf_("DONE seq=%u trav %.1f s: FSM, movement and support on change and every 0.5 s (player %#lx)", seq, secs,
	      (unsigned long)c->player);
}

static void trav_tick(const struct ctx *c)
{
	double t = mono();
	if (t >= g_trav.until || !c->player) {
		logf_("TRAV seq=%u end: %u lines", g_trav.seq, g_trav.lines);
		g_trav.on = 0;
		return;
	}
	uintptr_t P = c->player, pm = rq(P + P_PM), pc = pm ? rq(pm + PM_PHYS) : 0;
	uint8_t fsm = rb(P + P_FSM), fprev = rb(P + P_FSM + 1), vmode = pm ? rb(pm + 0x1a2) : 0, att = pm ? rb(pm + 0x1a6) : 0;
	uint8_t grav = pc ? rb(pc + 0x60) : 0;
	int sup = g_fn_ok[FN_SUPPORTED] ? (int)(FN(FN_SUPPORTED, supported_fn)(P) & 0xff) : -1;
	uint64_t sig = (uint64_t)fsm | (uint64_t)vmode << 8 | (uint64_t)att << 16 | (uint64_t)grav << 24 | (uint64_t)(sup & 0xff) << 32;
	if (sig == g_trav.sig && t - g_trav.last_t < 0.5)
		return;
	g_trav.sig = sig;
	g_trav.last_t = t;
	g_trav.lines++;
	float hx[8] = { 0 };
	uint32_t hn = 0;
	int hok = head_node(char_obj(c), &hn) == 0 && node_xf(char_obj(c), hn, hx) == 0;
	logf_("TRAV fsm=%u/%u velmode=%u attached=%u parent=%#lx node=%d gravity=%u g=%g supported=%d root=%.1f,%.1f,%.1f head_y=%.1f "
	      "head_dy=%.1f%s t=%.6f", fsm, fprev, vmode, att, (unsigned long)(pm ? rq(pm + 0x1f8) : 0), pm ? (int)r32(pm + 0x200) : -1,
	      grav, pc ? (double)rf(pc + 0x58) : 0.0, sup, g_pi.pos[0], g_pi.pos[1], g_pi.pos[2], hx[1], hok ? hx[1] - g_pi.pos[1] : 0.0f,
	      hok ? "" : " (no head node)", t);
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P8: the player model's attached children (re-animation.md 4) and hiding them one at a time (re-resolution.md   */
/* 2.4: none of the 17 pieces of hChar's own model was the head in P7)                                           */

typedef uint64_t (*arg1_fn)(uint64_t a);

#define ATT_N 48
struct att { uintptr_t entry, child, sm; uint32_t idx, hash; int parent; char id[24]; };
static struct att g_att[ATT_N];
static int g_att_n;

/* a CSimpleModel's attach node entries; 0 unless the node has its vtable and a sane vector */
static int att_entries(uintptr_t sm, uintptr_t *base)
{
	uintptr_t an = sm ? rq(sm + SM_ATTACH) : 0, b = an ? rq(an + AT_ENTRIES) : 0, e = an ? rq(an + AT_ENTRIES + 8) : 0;
	*base = b;
	if (!an || rq(an) != G.vt_attach || !b || e < b || (e - b) % AT_ESIZE || (e - b) / AT_ESIZE > 64)
		return 0;
	return (int)((e - b) / AT_ESIZE);
}

static void att_fill(struct att *a, uintptr_t entry, int parent, const char *id)
{
	memset(a, 0, sizeof *a);
	a->entry = entry;
	a->child = rq(entry + AE_CHILD);
	a->idx = r32(entry + AE_INDEX);
	a->hash = r32(entry + AE_HASH);
	a->parent = parent;
	a->sm = obj_model(a->child);
	snprintf(a->id, sizeof a->id, "%s", id);
}

/* the player model's children and theirs (one level): ids "N" and "N.M" */
static int att_collect(uintptr_t sm)
{
	uintptr_t b, b2;
	int n = att_entries(sm, &b);
	char id[24];
	g_att_n = 0;
	for (int i = 0; i < n && g_att_n < ATT_N; i++) {
		int me = g_att_n++;
		snprintf(id, sizeof id, "%d", i);
		att_fill(&g_att[me], b + (uintptr_t)i * AT_ESIZE, -1, id);
		int n2 = g_att[me].sm ? att_entries(g_att[me].sm, &b2) : 0;
		for (int j = 0; j < n2 && g_att_n < ATT_N; j++) {
			snprintf(id, sizeof id, "%d.%d", i, j);
			att_fill(&g_att[g_att_n++], b2 + (uintptr_t)j * AT_ESIZE, me, id);
		}
	}
	return g_att_n;
}

/* a node or socket hash of OBJ's skeleton -> its name ("socket N on 'node'") */
static const char *hash_name(uintptr_t obj, uint32_t h, char *out, size_t cap)
{
	struct skel sk;
	out[0] = 0;
	if (!h || !obj_model(obj) || skel_get(obj, &sk))
		return out;
	for (uint32_t i = 0; sk.hashes && i < sk.count; i++)
		if (r32(sk.hashes + 4u * i) == h)
			return skel_name(&sk, i, out, cap);
	for (uint32_t i = 0; i < sk.nsockets; i++)
		if (r32(sk.sockets + (uintptr_t)i * 0x30) == h) {
			char nm[64];
			cfmt(out, cap, "socket %u on '%s'", i, skel_name(&sk, r32(sk.sockets + (uintptr_t)i * 0x30 + 4), nm, sizeof nm));
			return out;
		}
	return out;
}

static int att_pieces(uintptr_t child, char *out, size_t cap)
{
	uintptr_t m = model_iface();
	uint32_t np = 0;
	size_t o = 0;
	out[0] = 0;
	if (!g_fn_ok[FN_NUMPIECES] || FN(FN_NUMPIECES, numpieces_fn)(m, child, &np) & 0xff)
		return -1;
	for (uint32_t p = 0; p < np && p < 6 && o + 80 < cap; p++) {
		char buf[128] = "";
		if (g_fn_ok[FN_PIECENAME])
			FN(FN_PIECENAME, piecename_fn)(m, child, p, buf, sizeof buf);
		buf[48] = 0;
		o += (size_t)cfmt(out + o, cap - o, "%s%u'%s'", p ? " " : "", p, buf);
	}
	return (int)np;
}

static void w_attached(const struct ctx *c, unsigned seq)
{
	uintptr_t obj = char_obj(c), sm = obj ? obj_model(obj) : 0;
	char md[400], hn[96], pl[600];
	if (!sm) {
		logf_("FAIL seq=%u attached: no player model (obj=%#lx)", seq, (unsigned long)obj);
		return;
	}
	int n = att_collect(sm), direct = 0;
	logf_("ATTACHED player obj=%#lx model %#lx attach node %#lx (%s); %s", (unsigned long)obj, (unsigned long)sm,
	      (unsigned long)rq(sm + SM_ATTACH), rq(rq(sm + SM_ATTACH)) == G.vt_attach ? "CSimpleModelAttachNode" : "none or vtable?",
	      rmask_desc(sm, md, sizeof md));
	for (int i = 0; i < n; i++) {
		struct att *a = &g_att[i];
		float off[7] = { 0 }, cp[3] = { 0 }, mp[3];
		rvec(a->entry + AE_OFFSET, off, 7);
		obj_pos(a->child, cp);
		to_model(g_pi.pos, g_pi.rot, cp, mp);
		direct += a->parent < 0;
		int np = a->sm ? att_pieces(a->child, pl, sizeof pl) : -1;
		if (!a->sm)
			snprintf(pl, sizeof pl, "-");
		logf_("ATTACH %s child=%#lx%s model=%#lx pieces=%d [%s] node index=%d hash=%#x '%s' offset=(%.1f %.1f %.1f | %.3f %.3f "
		      "%.3f %.3f) flag=%u resolved=%u at model (%.1f %.1f %.1f) cm; %s", a->id, (unsigned long)a->child,
		      is_weapon_obj(a->child) ? " (a player weapon)" : "", (unsigned long)a->sm, np, pl, (int)a->idx, a->hash,
		      hash_name(a->parent < 0 ? obj : g_att[a->parent].child, a->hash, hn, sizeof hn), off[0], off[1], off[2], off[3],
		      off[4], off[5], off[6], rb(a->entry + 0x60), rb(a->entry + 0x61), mp[0], mp[1], mp[2],
		      a->sm ? rmask_desc(a->sm, md, sizeof md) : "(no model)");
	}
	logf_("DONE seq=%u attached: %d entries (%d on the player model, %d one level down); `attachhide ID` hides one child",
	      seq, n, direct, n - direct);
}

/* attachhide: one attached child hidden at a time, with the children attached to it (the hair on a head model): their
 * pieces (only those the game shows), or their render nodes' skip byte */
#define AH_N 8
static struct {
	int on, node_mode, n;
	char id[24];
	unsigned seq;
	struct { uintptr_t child, sm, rnode; uint64_t bits; uint8_t skip_old; } o[AH_N];
} g_ah;

static void ah_undo(void)
{
	uintptr_t m = model_iface();
	if (!g_ah.on)
		return;
	for (int i = 0; i < g_ah.n; i++) {
		if (g_ah.node_mode) {
			if (g_ah.o[i].rnode && sm_rnode(g_ah.o[i].sm) == g_ah.o[i].rnode)
				memcpy((void *)(g_ah.o[i].rnode + RNODE_SKIP), &g_ah.o[i].skip_old, 1);
			continue;
		}
		for (uint32_t p = 0; p < 64; p++)
			if (g_ah.o[i].bits >> p & 1)
				FN(FN_HIDEPIECE, hidepiece_fn)(m, g_ah.o[i].child, p, 0);
	}
	g_ah.on = 0;
}

/* hide one object (pieces or node) into g_ah.o[g_ah.n]; the hidden count */
static int ah_hide(uintptr_t child, uintptr_t sm, int node_mode)
{
	uintptr_t m = model_iface();
	if (g_ah.n >= AH_N || !sm)
		return 0;
	int k = g_ah.n, cnt = 0;
	memset(&g_ah.o[k], 0, sizeof g_ah.o[k]);
	g_ah.o[k].child = child;
	g_ah.o[k].sm = sm;
	if (node_mode) {
		uintptr_t rn = sm_rnode(sm);
		if (!rn)
			return 0;
		uint8_t one = 1;
		g_ah.o[k].rnode = rn;
		g_ah.o[k].skip_old = rb(rn + RNODE_SKIP);
		memcpy((void *)(rn + RNODE_SKIP), &one, 1);
		cnt = 1;
	} else {
		uint32_t np = 0, mk[2] = { 0, 0 };
		if (FN(FN_NUMPIECES, numpieces_fn)(m, child, &np) & 0xff)
			return 0;
		rd(sm + SM_MASK, mk, 8);
		uint64_t gm = (uint64_t)mk[1] << 32 | mk[0];
		for (uint32_t p = 0; p < np && p < 64; p++)
			if (!(gm >> p & 1)) {
				FN(FN_HIDEPIECE, hidepiece_fn)(m, child, p, 1);
				g_ah.o[k].bits |= 1ull << p;
				cnt++;
			}
	}
	g_ah.n++;
	return cnt;
}

static void w_attachhide(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	uintptr_t obj = char_obj(c), sm = obj ? obj_model(obj) : 0;
	char md[400], wid[24];
	if (!sm || !g_fn_ok[FN_HIDEPIECE] || !g_fn_ok[FN_NUMPIECES]) {
		logf_("FAIL seq=%u attachhide: no player model or the piece functions did not check out", seq);
		return;
	}
	int n = att_collect(sm), was = g_ah.on;
	snprintf(wid, sizeof wid, "%s", g_ah.id);
	if (g_ah.on) { /* undo the last one, if its first child is still attached (else it went with its model) */
		int still = 0;
		for (int i = 0; i < n; i++)
			still |= g_att[i].child == g_ah.o[0].child && g_att[i].sm == g_ah.o[0].sm;
		if (still)
			ah_undo();
		else
			g_ah.on = 0;
	}
	if (argc < 2 || !strcmp(argv[1], "off")) {
		logf_("DONE seq=%u attachhide off (%s%s)", seq, was ? "child " : "nothing was hidden", was ? wid : "");
		return;
	}
	int k = -1;
	for (int i = 0; i < n && k < 0; i++)
		if (!strcmp(g_att[i].id, argv[1]))
			k = i;
	if (k < 0 || !g_att[k].sm) {
		logf_("FAIL seq=%u attachhide: no attached child '%s' with a model (%d entries; `attached` lists them)", seq, argv[1], n);
		return;
	}
	int node_mode = argc > 2 && !strcmp(argv[2], "node"), hidden = 0, sub = 0;
	memset(&g_ah, 0, sizeof g_ah);
	snprintf(g_ah.id, sizeof g_ah.id, "%s", g_att[k].id);
	g_ah.seq = seq;
	g_ah.node_mode = node_mode;
	hidden += ah_hide(g_att[k].child, g_att[k].sm, node_mode);
	for (int i = 0; i < n; i++) /* the children attached to it */
		if (g_att[i].parent == k && g_att[i].sm) {
			hidden += ah_hide(g_att[i].child, g_att[i].sm, node_mode);
			sub++;
		}
	g_ah.on = 1;
	char w[40];
	snprintf(w, sizeof w, "attach %s", g_att[k].id);
	ph_mlater(w, g_att[k].child, seq);
	logf_("DONE seq=%u attachhide %s child=%#lx%s: %d %s hidden on %d object(s) (%d attached to it); %s", seq, g_att[k].id,
	      (unsigned long)g_att[k].child, is_weapon_obj(g_att[k].child) ? " (a player weapon)" : "", hidden,
	      node_mode ? "render nodes (+0x40 = 1)" : "pieces", g_ah.n, sub, rmask_desc(g_att[k].sm, md, sizeof md));
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P8: wnodes - the active weapon model's own skeleton and sockets plus its attach entry (re-arms-ik.md 6.2)       */

static void w_wnodes(unsigned seq, int argc, char **argv)
{
	uintptr_t inv = g_pi.inv, b;
	int slot = argc > 1 && is_num(argv[1]) ? (int)num(argv[1], 0) : inv ? (int)r32(inv + INV_ACTIVE) : -1;
	uintptr_t item = inv_item(inv, slot);
	int ne = item_entries(item, &b, 8), nodes = 0, socks = 0;
	char path[1200], nm[96], rn[128], hn[96];
	if (!ne) {
		logf_("FAIL seq=%u wnodes: slot %d item %#lx has no model entries", seq, slot, (unsigned long)item);
		return;
	}
	FILE *f = out_file("wnodes", seq, path, sizeof path);
	if (f)
		fprintf(f, "entry\tkind\tindex\tparent\tname\thash\tx\ty\tz\tqx\tqy\tqz\tqw\tlocal_x\tlocal_y\tlocal_z\n");
	uintptr_t psm = g_pi.charobj ? obj_model(g_pi.charobj) : 0;
	int nat = psm ? att_collect(psm) : 0;
	for (int k = 0; k < ne; k++) {
		uintptr_t o = rq(b + (uintptr_t)k * IT_ESIZE + IE_OBJ);
		struct skel sk;
		if (!o || !obj_model(o) || skel_get(o, &sk)) {
			logf_("WNODES slot=%d entry=%d obj=%#lx: no model or skeleton", slot, k, (unsigned long)o);
			continue;
		}
		float op[3], oq[4] = { 0, 0, 0, 1 }, far = 0;
		char farn[96] = "";
		obj_pos(o, op);
		rvec(o + OBJ_ROT, oq, 4);
		for (uint32_t i = 0; i < sk.count && i < 512; i++) {
			uint32_t h = sk.hashes ? r32(sk.hashes + 4u * i) : 0;
			float xf[8] = { 0 }, lp[3];
			node_xf(o, h, xf);
			to_model(op, oq, xf, lp);
			skel_name(&sk, i, nm, sizeof nm);
			float dd = sqrtf(lp[0] * lp[0] + lp[1] * lp[1] + lp[2] * lp[2]);
			if (dd > far)
				far = dd, snprintf(farn, sizeof farn, "%s", nm);
			nodes++;
			if (f)
				fprintf(f, "%d\tnode\t%u\t%u\t%s\t%#x\t%.3f\t%.3f\t%.3f\t%.5f\t%.5f\t%.5f\t%.5f\t%.2f\t%.2f\t%.2f\n", k, i,
					rb(sk.nodes + (uintptr_t)i * 0x40), nm[0] ? nm : "?", h, xf[0], xf[1], xf[2], xf[3], xf[4], xf[5],
					xf[6], lp[0], lp[1], lp[2]);
			if (i < 24)
				logf_("WNODE slot=%d entry=%d %u parent=%u '%s' hash=%#x local=(%.2f %.2f %.2f) cm", slot, k, i,
				      rb(sk.nodes + (uintptr_t)i * 0x40), nm, h, lp[0], lp[1], lp[2]);
		}
		for (uint32_t i = 0; i < sk.nsockets; i++) {
			uintptr_t so = sk.sockets + (uintptr_t)i * 0x30;
			uint32_t h = r32(so), par = r32(so + 4);
			float off[7] = { 0 }, xf[8] = { 0 }, lp[3];
			rvec(so + 8, off, 7);
			node_xf(o, h, xf);
			to_model(op, oq, xf, lp);
			socks++;
			if (f)
				fprintf(f, "%d\tsocket\t%u\t%u\t%s\t%#x\t%.3f\t%.3f\t%.3f\t%.5f\t%.5f\t%.5f\t%.5f\t%.2f\t%.2f\t%.2f\n", k, i,
					par, skel_name(&sk, par, nm, sizeof nm), h, xf[0], xf[1], xf[2], xf[3], xf[4], xf[5], xf[6], lp[0],
					lp[1], lp[2]);
			logf_("WSOCKET slot=%d entry=%d %u hash=%#x on node %u '%s' offset=(%.2f %.2f %.2f | %.3f %.3f %.3f %.3f) local=(%.2f "
			      "%.2f %.2f) cm", slot, k, i, h, par, skel_name(&sk, par, nm, sizeof nm), off[0], off[1], off[2], off[3],
			      off[4], off[5], off[6], lp[0], lp[1], lp[2]);
		}
		for (int i = 0; i < nat; i++) {
			if (g_att[i].child != o || g_att[i].parent >= 0)
				continue;
			float e[7] = { 0 }, u[7] = { 0 };
			rvec(g_att[i].entry + AE_OFFSET, e, 7);
			rvec(g_att[i].entry + AE_USER, u, 7);
			logf_("WATTACH slot=%d entry=%d = the player's attach entry %s: node index %d hash %#x '%s' effective offset (%.2f "
			      "%.2f %.2f | %.4f %.4f %.4f %.4f) user offset (%.2f %.2f %.2f | %.4f %.4f %.4f %.4f) flag %u resolved %u",
			      slot, k, g_att[i].id, (int)g_att[i].idx, g_att[i].hash, hash_name(g_pi.charobj, g_att[i].hash, hn, sizeof hn),
			      e[0], e[1], e[2], e[3], e[4], e[5], e[6], u[0], u[1], u[2], u[3], u[4], u[5], u[6],
			      rb(g_att[i].entry + 0x60), rb(g_att[i].entry + 0x61));
		}
		logf_("WNODES slot=%d entry=%d obj=%#lx '%s': %u nodes, %u sockets; the farthest node from the object's origin '%s' "
		      "%.1f cm (a blade length candidate)", slot, k, (unsigned long)o, rec_name(rq(item + IT_REC), rn, sizeof rn),
		      sk.count, sk.nsockets, farn, far);
	}
	if (f)
		fclose(f);
	logf_("DONE seq=%u wnodes slot %d: %d nodes, %d sockets, file=%s", seq, slot, nodes, socks, f ? path : "(not written)");
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P8: ikwatch / ikcb - the engine's arm IK on Talion (re-arms-ik.md 1.2, 6.2)                                     */

static struct {
	int on, cb;
	unsigned seq, cbseq, setups, cbs, lines;
	double until, cb_until, next_t;
	uintptr_t mgr, obj; /* the player's CAnimationMgr and HOBJECT, refreshed on every game tick */
	_Atomic unsigned calls[4];
} g_ikw;

static int ik_slot(uintptr_t chain)
{
	uintptr_t base = g_ikw.mgr + MGR_CHAINS;
	if (!g_ikw.mgr || chain < base || chain >= base + 4 * CHAIN_SIZE || (chain - base) % CHAIN_SIZE)
		return -1;
	return (int)((chain - base) / CHAIN_SIZE);
}

static const char *chain_desc(uintptr_t ch, char *out, size_t cap)
{
	char rn[96], n1[48], n2[48], n3[48], n4[48];
	float f[4] = { 0 };
	rvec(ch + 0xa0, f, 4);
	cfmt(out, cap, "record %#lx '%s' root %#x '%s' joint %#x '%s' target node %#x '%s' target %#x '%s' lengths %.2f/%.2f "
	     "jointdist %.1f downsign %.1f in_use %u state %d weight %.3f", (unsigned long)rq(ch + 0x20),
	     rec_name(rq(ch + 0x20), rn, sizeof rn), r32(ch + 0x90), node_by_hash(r32(ch + 0x90), n1, sizeof n1), r32(ch + 0x94),
	     node_by_hash(r32(ch + 0x94), n2, sizeof n2), r32(ch + 0x98), node_by_hash(r32(ch + 0x98), n3, sizeof n3),
	     r32(ch + 0x70), node_by_hash(r32(ch + 0x70), n4, sizeof n4), (double)f[0], (double)f[1], (double)f[2], (double)f[3],
	     rb(ch + 0x110), (int)r32(ch + 0x10c), (double)rf(ch + 0x108));
	return out;
}

static void ik_refresh(const struct ctx *c)
{
	uintptr_t mgr = c->body ? rq(c->body + BODY_MGR) : 0;
	g_ikw.mgr = mgr && c->hchar && rq(mgr + MGR_CHAR) == c->hchar ? mgr : 0;
	g_ikw.obj = char_obj(c);
}

/* FUN_02185900(chain, hObj, record) from IK::vf_07 / vf_12: the chain's setup (post: its fields are filled) */
static void iksetup_common(int hk, struct regs *r)
{
	first_call(hk, atomic_fetch_add_explicit(&g_h[hk].calls, 1, memory_order_relaxed) + 1);
	r->rax = ((three_fn)g_h[hk].orig)(r->rdi, r->rsi, r->rdx);
	int k = ik_slot(r->rdi);
	if (k < 0 && !(g_ikw.obj && r->rsi == g_ikw.obj))
		return; /* another character's IK */
	g_ikw.setups++;
	if (!g_ikw.on || !rl_ok(RL_IK))
		return;
	char d[700];
	logf_("IKSETUP %s slot=%d chain=%#lx obj=%#lx: %s tid=%d t=%.6f", hk == HK_IK_SETUP_A ? "IK::vf_07" : "IK::vf_12", k,
	      (unsigned long)r->rdi, (unsigned long)r->rsi, chain_desc(r->rdi, d, sizeof d), my_tid(), mono());
}
__attribute__((used)) void somvr_pc_iksetup_a(struct regs *r) { iksetup_common(HK_IK_SETUP_A, r); }
__attribute__((used)) void somvr_pc_iksetup_b(struct regs *r) { iksetup_common(HK_IK_SETUP_B, r); }

/* FUN_02185590(chain) in the player update: target and weight of one slot (post) */
__attribute__((used)) void somvr_pc_ikchain(struct regs *r)
{
	first_call(HK_IK_CHAIN, atomic_fetch_add_explicit(&g_h[HK_IK_CHAIN].calls, 1, memory_order_relaxed) + 1);
	r->rax = ((arg1_fn)g_h[HK_IK_CHAIN].orig)(r->rdi);
	if (!g_ikw.on)
		return;
	int k = ik_slot(r->rdi);
	if (k < 0)
		return;
	unsigned n = atomic_fetch_add(&g_ikw.calls[k], 1) + 1;
	if (n % 10 || !r32(r->rdi + 0x10c) || !rl_ok(RL_IK))
		return; /* every 10th update of an active slot */
	float tg[3], tq[4], mt[3];
	rvec(r->rdi + 0x74, tg, 3);
	rvec(r->rdi + 0x80, tq, 4);
	to_model(g_pi.pos, g_pi.rot, tg, mt);
	logf_("IKCHAIN slot=%d update %u: state %d weight %.3f target world (%.1f %.1f %.1f) model (%.1f %.1f %.1f) quat (%.3f %.3f "
	      "%.3f %.3f) solved %u t=%.6f", k, n, (int)r32(r->rdi + 0x10c), (double)rf(r->rdi + 0x108), tg[0], tg[1], tg[2], mt[0],
	      mt[1], mt[2], tq[0], tq[1], tq[2], tq[3], rb(r->rdi + 0x9d), mono());
}

/* IEngineModel slots 38/39 (pre, log only): AddNodeCallback(m, hObj, node, type, flags, owner, [size]) and Remove */
static void cb_common(int hk, const char *what, struct regs *r)
{
	first_call(hk, atomic_fetch_add_explicit(&g_h[hk].calls, 1, memory_order_relaxed) + 1);
	if (!g_ikw.cb || !r->rsi || r->rsi != g_ikw.obj)
		return;
	g_ikw.cbs++;
	if (!rl_ok(RL_IK))
		return;
	char nn[64];
	logf_("IKCB %s obj=%#lx node=%#x '%s' type=%#x flags=%#x owner=%#lx tid=%d t=%.6f", what, (unsigned long)r->rsi,
	      (uint32_t)r->rdx, node_by_hash((uint32_t)r->rdx, nn, sizeof nn), (uint32_t)r->rcx, (uint32_t)r->r8,
	      (unsigned long)r->r9, my_tid(), mono());
}
__attribute__((used)) void somvr_pc_addcb(struct regs *r) { cb_common(HK_IEM_ADDCB, "add", r); }
__attribute__((used)) void somvr_pc_remcb(struct regs *r) { cb_common(HK_IEM_REMCB, "remove", r); }

static void w_ikwatch(const struct ctx *c, unsigned seq, int argc, char **argv, int cb)
{
	int off = argc > 1 && !strcmp(argv[1], "off");
	double secs = num(argc > 1 ? argv[1] : NULL, 20);
	secs = secs < 1 ? 1 : secs > 120 ? 120 : secs;
	ik_refresh(c);
	if (cb) {
		g_ikw.cb = !off;
		g_ikw.cbseq = seq;
		g_ikw.cb_until = mono() + secs;
		g_ikw.cbs = 0;
		logf_("DONE seq=%u ikcb %s: node callbacks added/removed on Talion's object %#lx (hooks P_IEM_ADDCB %s, _REMCB %s)",
		      seq, off ? "off" : "on", (unsigned long)g_ikw.obj, g_h[HK_IEM_ADDCB].installed ? "on" : "NOT installed",
		      g_h[HK_IEM_REMCB].installed ? "on" : "NOT installed");
		return;
	}
	if (off) {
		g_ikw.on = 0;
		logf_("DONE seq=%u ikwatch off: %u setups", seq, g_ikw.setups);
		return;
	}
	g_ikw.on = 1;
	g_ikw.seq = seq;
	g_ikw.until = mono() + secs;
	g_ikw.next_t = 0;
	g_ikw.setups = g_ikw.lines = 0;
	for (int k = 0; k < 4; k++)
		atomic_store(&g_ikw.calls[k], 0);
	char d[700];
	for (int k = 0; g_ikw.mgr && k < 4; k++)
		logf_("IKSLOT %d chain=%#lx: %s", k, (unsigned long)(g_ikw.mgr + MGR_CHAINS + (uintptr_t)k * CHAIN_SIZE),
		      chain_desc(g_ikw.mgr + MGR_CHAINS + (uintptr_t)k * CHAIN_SIZE, d, sizeof d));
	logf_("DONE seq=%u ikwatch %.0f s: the player's anim manager %#lx (hooks P_IK_SETUP_A %s, _B %s, P_IK_CHAIN %s); the "
	      "nodes every 0.5 s", seq, secs, (unsigned long)g_ikw.mgr, g_h[HK_IK_SETUP_A].installed ? "on" : "off",
	      g_h[HK_IK_SETUP_B].installed ? "on" : "off", g_h[HK_IK_CHAIN].installed ? "on" : "off");
}

static void ikwatch_tick(const struct ctx *c)
{
	double t = mono();
	ik_refresh(c);
	if (g_ikw.cb && t > g_ikw.cb_until) {
		g_ikw.cb = 0;
		logf_("IKCB seq=%u end: %u node callbacks added/removed on Talion", g_ikw.cbseq, g_ikw.cbs);
	}
	if (!g_ikw.on)
		return;
	if (t > g_ikw.until) {
		g_ikw.on = 0;
		logf_("IKWATCH seq=%u end: %u setups on Talion, chain updates %u/%u/%u/%u, %u lines", g_ikw.seq, g_ikw.setups,
		      atomic_load(&g_ikw.calls[0]), atomic_load(&g_ikw.calls[1]), atomic_load(&g_ikw.calls[2]),
		      atomic_load(&g_ikw.calls[3]), g_ikw.lines);
		return;
	}
	if (t < g_ikw.next_t)
		return;
	g_ikw.next_t = t + 0.5;
	static const char *const nn[4] = { "IK_LHand", "IK_RHand", "L_Hand", "R_Hand" };
	uintptr_t o = char_obj(c);
	float mp[4][3];
	int ok[4];
	for (int k = 0; k < 4; k++) {
		float xf[8] = { 0 };
		ok[k] = o && node_xf(o, name_hash(nn[k]), xf) == 0;
		to_model(g_pi.pos, g_pi.rot, xf, mp[k]);
	}
	char sl[400] = "";
	size_t l = 0;
	for (int k = 0; g_ikw.mgr && k < 4; k++) {
		uintptr_t ch = g_ikw.mgr + MGR_CHAINS + (uintptr_t)k * CHAIN_SIZE;
		char nm[48];
		l += (size_t)cfmt(sl + l, sizeof sl - l, " [%d use=%u state=%d w=%.2f root='%s']", k, rb(ch + 0x110),
				  (int)r32(ch + 0x10c), (double)rf(ch + 0x108), node_by_hash(r32(ch + 0x90), nm, sizeof nm));
	}
	g_ikw.lines++;
	if (!rl_ok(RL_IK))
		return;
	logf_("IKWATCH model cm: IK_LHand%s (%.1f %.1f %.1f) IK_RHand%s (%.1f %.1f %.1f) L_Hand (%.1f %.1f %.1f) R_Hand (%.1f %.1f "
	      "%.1f) |IK_RHand-R_Hand|=%.1f |IK_LHand-L_Hand|=%.1f; slots%s t=%.6f", ok[0] ? "" : "?", mp[0][0], mp[0][1], mp[0][2],
	      ok[1] ? "" : "?", mp[1][0], mp[1][1], mp[1][2], mp[2][0], mp[2][1], mp[2][2], mp[3][0], mp[3][1], mp[3][2],
	      dist3(mp[1], mp[3]), dist3(mp[0], mp[2]), sl[0] ? sl : " (no anim manager)", t);
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P8: armpose - Talion's right arm edited in the final model-space pose at phase 2 (re-arms-ik.md 2.2, 2.3)        */

#define ARM_MAXN 256
static struct {
	int on, mode;                 /* 1 rot, 2 ik */
	unsigned seq, applied, skipped, seen_n, sword_n;
	float deg, tgt[3];
	uintptr_t obj, sm, trk, attach, body, sword, sword_entry;
	uint32_t hW, sword_idx;
	int n, iS, iE, iW;
	uint8_t subS[ARM_MAXN], subE[ARM_MAXN], subW[ARM_MAXN];
	float hand0[3], hand1[3], resid, seen_err, sword_err, sword_err_stock, pre_node[8];
	int have_after, have_seen, have_sword;
	double next_log;
} g_arm;

static float vlen(const float v[3]) { return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
static void vcross(const float a[3], const float b[3], float o[3])
{
	o[0] = a[1] * b[2] - a[2] * b[1];
	o[1] = a[2] * b[0] - a[0] * b[2];
	o[2] = a[0] * b[1] - a[1] * b[0];
}
static void qnorm(float q[4])
{
	float l = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
	if (l < 1e-9f) {
		q[0] = q[1] = q[2] = 0, q[3] = 1;
		return;
	}
	for (int k = 0; k < 4; k++)
		q[k] /= l;
}
/* the shortest rotation taking direction u to direction v */
static void q_between(const float u[3], const float v[3], float q[4])
{
	float lu = vlen(u), lv = vlen(v), c[3];
	if (lu < 1e-6f || lv < 1e-6f) {
		q[0] = q[1] = q[2] = 0, q[3] = 1;
		return;
	}
	vcross(u, v, c);
	float w = lu * lv + (u[0] * v[0] + u[1] * v[1] + u[2] * v[2]);
	if (w < 1e-6f * lu * lv) { /* opposite: 180 degrees about an axis perpendicular to u */
		const float ax[3] = { fabsf(u[0]) < 0.9f * lu ? 1.0f : 0.0f, fabsf(u[0]) < 0.9f * lu ? 0.0f : 1.0f, 0 };
		vcross(u, ax, c);
		w = 0;
	}
	q[0] = c[0], q[1] = c[1], q[2] = c[2], q[3] = w;
	qnorm(q);
}
/* rotate node ROOT and its subtree SUB by Q about PIVOT (model space, absolute transforms, pad written 0) */
static void arm_rot(float (*P)[8], int n, int root, const uint8_t *sub, const float q[4], const float pivot[3])
{
	for (int i = 0; i < n; i++) {
		if (i != root && !sub[i])
			continue;
		float v[3] = { P[i][0] - pivot[0], P[i][1] - pivot[1], P[i][2] - pivot[2] }, w[3], qq[4];
		qrot(q, v, w);
		P[i][0] = pivot[0] + w[0], P[i][1] = pivot[1] + w[1], P[i][2] = pivot[2] + w[2], P[i][3] = 0;
		qmul(q, P[i] + 4, qq);
		qnorm(qq);
		memcpy(P[i] + 4, qq, 16);
	}
}

/* the edit itself, on the pose rows P (rot: the hand subtree about the forearm axis; ik: re-arms-ik.md 2.3 steps 3-6,
 * the elbow hint "out, down, back" of the animated elbow, no twist share) */
static void arm_solve(float (*P)[8], int n)
{
	int iS = g_arm.iS, iE = g_arm.iE, iW = g_arm.iW;
	if (g_arm.mode == 1) {
		float ax[3] = { P[iW][0] - P[iE][0], P[iW][1] - P[iE][1], P[iW][2] - P[iE][2] }, l = vlen(ax), q[4], piv[3];
		if (l < 1e-3f)
			return;
		float s = sinf(g_arm.deg * (float)M_PI / 360.0f) / l;
		q[0] = ax[0] * s, q[1] = ax[1] * s, q[2] = ax[2] * s, q[3] = cosf(g_arm.deg * (float)M_PI / 360.0f);
		memcpy(piv, P[iW], 12);
		arm_rot(P, n, iW, g_arm.subW, q, piv);
		g_arm.resid = 0;
		return;
	}
	float S[3], E[3], W[3], d[3], h[3], nrm[3], nd[3], ep[3], T[3], qs[4], qe[4], u[3], v[3];
	memcpy(S, P[iS], 12), memcpy(E, P[iE], 12), memcpy(W, P[iW], 12);
	float a = dist3(E, S), b = dist3(W, E);
	for (int k = 0; k < 3; k++)
		d[k] = g_arm.tgt[k] - S[k];
	float L = vlen(d), lo = fabsf(a - b) + 0.5f, hi = a + b - 0.5f, Lc = L < lo ? lo : L > hi ? hi : L;
	if (L < 1e-3f)
		d[0] = 0, d[1] = 0, d[2] = 1, L = 1;
	for (int k = 0; k < 3; k++)
		d[k] /= L, T[k] = S[k] + d[k] * Lc;
	g_arm.resid = dist3(T, g_arm.tgt);
	const float hint[3] = { 10.0f, -20.0f, -10.0f }; /* the right elbow out, down and back of the animated one */
	for (int k = 0; k < 3; k++)
		h[k] = E[k] + hint[k] - S[k];
	vcross(d, h, nrm);
	if (vlen(nrm) < 1e-3f) {
		const float up[3] = { 0, 1, 0 };
		vcross(d, up, nrm);
	}
	float ln = vlen(nrm);
	if (ln < 1e-6f)
		return;
	for (int k = 0; k < 3; k++)
		nrm[k] /= ln;
	vcross(nrm, d, nd);
	float ca = (a * a + Lc * Lc - b * b) / (2 * a * Lc);
	ca = ca < -1 ? -1 : ca > 1 ? 1 : ca;
	float sa = sqrtf(1 - ca * ca);
	for (int k = 0; k < 3; k++)
		ep[k] = S[k] + a * (ca * d[k] + sa * nd[k]), u[k] = E[k] - S[k], v[k] = ep[k] - S[k];
	q_between(u, v, qs);
	arm_rot(P, n, iS, g_arm.subS, qs, S);
	memcpy(E, P[iE], 12), memcpy(W, P[iW], 12);
	for (int k = 0; k < 3; k++)
		u[k] = W[k] - E[k], v[k] = T[k] - E[k];
	q_between(u, v, qe);
	arm_rot(P, n, iE, g_arm.subE, qe, E);
}

/* the post-hook body (game thread, phase 2): read the pose rows, edit, write back only the moved rows */
static void arm_apply(uintptr_t trk)
{
	static float P[ARM_MAXN][8];
	uintptr_t pb = rq(trk + TRK_POSE), xf = pb ? rq(pb + PB_XF) : 0;
	uint32_t cnt = pb ? r32(pb + PB_COUNT) : 0;
	if (!xf || (int)cnt != g_arm.n || rd(xf, P, (size_t)cnt * 32)) {
		g_arm.skipped++;
		return;
	}
	if (g_arm.sword_idx < cnt)
		memcpy(g_arm.pre_node, P[g_arm.sword_idx], 32);
	memcpy(g_arm.hand0, P[g_arm.iW], 12);
	arm_solve(P, g_arm.n);
	const uint8_t *sub = g_arm.mode == 1 ? g_arm.subW : g_arm.subS;
	int root = g_arm.mode == 1 ? g_arm.iW : g_arm.iS;
	for (int i = 0; i < g_arm.n; i++)
		if (i == root || sub[i])
			memcpy((void *)(xf + (uintptr_t)i * 32), P[i], 32);
	memcpy(g_arm.hand1, P[g_arm.iW], 12);
	g_arm.have_after = 1;
	g_arm.applied++;
}

/* the next player update reads R_Hand (GetNodeTransform, before FUN_00a51210): does it see our edit? */
static void arm_seen(void)
{
	float xf[8], op[3], oq[4] = { 0, 0, 0, 1 }, mp[3];
	if (node_xf(g_arm.obj, g_arm.hW, xf))
		return;
	obj_pos(g_arm.obj, op);
	rvec(g_arm.obj + OBJ_ROT, oq, 4);
	to_model(op, oq, xf, mp);
	g_arm.seen_err = dist3(mp, g_arm.hand1);
	g_arm.have_seen = 1;
	g_arm.seen_n++;
	g_arm.have_after = 0;
}

/* phase 7, after the attach node placed its children: the sword vs object x pose'[node] x the entry's offset */
static void arm_sword(void)
{
	uintptr_t pb = rq(g_arm.trk + TRK_POSE), xf = pb ? rq(pb + PB_XF) : 0;
	float node[8], off[7], X[3], Xq[4] = { 0, 0, 0, 1 }, cp[3], t[3], w[3], ex[3], st[3];
	if (!xf || g_arm.sword_idx >= (uint32_t)g_arm.n || rd(xf + (uintptr_t)g_arm.sword_idx * 32, node, 32) ||
	    rvec(g_arm.sword_entry + AE_OFFSET, off, 7))
		return;
	obj_pos(g_arm.obj, X);
	rvec(g_arm.obj + OBJ_ROT, Xq, 4);
	obj_pos(g_arm.sword, cp);
	qrot(node + 4, off, t); /* pose'[node] x offset, then the object */
	for (int k = 0; k < 3; k++)
		t[k] += node[k];
	qrot(Xq, t, w);
	for (int k = 0; k < 3; k++)
		ex[k] = X[k] + w[k];
	qrot(g_arm.pre_node + 4, off, t); /* the same with the node as the animation left it */
	for (int k = 0; k < 3; k++)
		t[k] += g_arm.pre_node[k];
	qrot(Xq, t, w);
	for (int k = 0; k < 3; k++)
		st[k] = X[k] + w[k];
	g_arm.sword_err = dist3(cp, ex);
	g_arm.sword_err_stock = dist3(cp, st);
	g_arm.have_sword = 1;
	g_arm.sword_n++;
}

static uint64_t p_trk_eval(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f)
{
	uint64_t r = ((six_fn)g_h[HK_TRK_EVAL].orig)(a, b, c, d, e, f);
	first_call(HK_TRK_EVAL, atomic_fetch_add_explicit(&g_h[HK_TRK_EVAL].calls, 1, memory_order_relaxed) + 1);
	if (g_arm.on && a == g_arm.trk)
		arm_apply((uintptr_t)a);
	return r;
}

static uint64_t p_attach_listen(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f)
{
	uint64_t r = ((six_fn)g_h[HK_ATTACH_LISTEN].orig)(a, b, c, d, e, f);
	first_call(HK_ATTACH_LISTEN, atomic_fetch_add_explicit(&g_h[HK_ATTACH_LISTEN].calls, 1, memory_order_relaxed) + 1);
	if (g_arm.on && g_arm.attach && a == g_arm.attach && g_arm.sword_entry && g_arm.applied)
		arm_sword();
	return r;
}

/* the player's tracker, attach node, skeleton indices and subtrees, the sword's attach entry; 0 = ready */
static int arm_resolve(const struct ctx *c, unsigned seq)
{
	uintptr_t obj = char_obj(c), sm = obj ? obj_model(obj) : 0, trk = sm ? rq(sm + SM_TRACKER) : 0;
	struct skel sk;
	if (!sm || !trk || rq(trk) != G.vt_tracker || skel_get(obj, &sk) || !sk.hashes || sk.count > ARM_MAXN) {
		if (seq)
			logf_("FAIL seq=%u armpose: model %#lx tracker %#lx (vtable %#lx, want %#lx) skeleton %u nodes", seq,
			      (unsigned long)sm, (unsigned long)trk, (unsigned long)rq(trk), (unsigned long)G.vt_tracker, sk.count);
		return -1;
	}
	if (trk == g_arm.trk && obj == g_arm.obj && sm == g_arm.sm && (int)sk.count == g_arm.n)
		goto sword; /* indices unchanged */
	int idx[3] = { -1, -1, -1 };
	static const char *const bn[3] = { "R_Armu", "R_Arml", "R_Hand" };
	for (int k = 0; k < 3; k++) {
		uint32_t h = name_hash(bn[k]);
		for (uint32_t i = 0; i < sk.count && idx[k] < 0; i++)
			if (r32(sk.hashes + 4u * i) == h)
				idx[k] = (int)i;
	}
	if (idx[0] < 0 || idx[1] < 0 || idx[2] < 0) {
		if (seq)
			logf_("FAIL seq=%u armpose: R_Armu %d R_Arml %d R_Hand %d in the skeleton", seq, idx[0], idx[1], idx[2]);
		return -1;
	}
	g_arm.obj = obj, g_arm.sm = sm, g_arm.trk = trk, g_arm.n = (int)sk.count;
	g_arm.iS = idx[0], g_arm.iE = idx[1], g_arm.iW = idx[2], g_arm.hW = name_hash("R_Hand");
	uint8_t par[ARM_MAXN];
	for (int i = 0; i < g_arm.n; i++)
		par[i] = rb(sk.nodes + (uintptr_t)i * 0x40);
	int root[3] = { g_arm.iS, g_arm.iE, g_arm.iW };
	uint8_t *sub[3] = { g_arm.subS, g_arm.subE, g_arm.subW };
	for (int k = 0; k < 3; k++) { /* parents precede children: one forward pass */
		memset(sub[k], 0, ARM_MAXN);
		for (int j = 0; j < g_arm.n; j++)
			sub[k][j] = par[j] < j && (par[j] == root[k] || sub[k][par[j]]);
	}
sword:
	g_arm.body = c->body;
	g_arm.attach = rq(sm + SM_ATTACH);
	if (rq(g_arm.attach) != G.vt_attach)
		g_arm.attach = 0;
	g_arm.sword = g_arm.sword_entry = 0;
	uintptr_t inv = g_pi.inv, b;
	uintptr_t item = inv_item(inv, inv ? (int)r32(inv + INV_ACTIVE) : -1);
	uintptr_t wo = item_entries(item, &b, 1) ? rq(b + IE_OBJ) : 0, eb;
	int ne = wo ? att_entries(sm, &eb) : 0;
	for (int i = 0; i < ne && !g_arm.sword; i++)
		if (rq(eb + (uintptr_t)i * AT_ESIZE + AE_CHILD) == wo) {
			g_arm.sword = wo;
			g_arm.sword_entry = eb + (uintptr_t)i * AT_ESIZE;
			g_arm.sword_idx = r32(g_arm.sword_entry + AE_INDEX);
		}
	return 0;
}

static void w_armpose(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	if (argc < 2 || !strcmp(argv[1], "off")) {
		g_arm.on = 0;
		logf_("DONE seq=%u armpose off (%u frames applied, %u skipped)", seq, g_arm.applied, g_arm.skipped);
		return;
	}
	int mode = !strcmp(argv[1], "rot") ? 1 : !strcmp(argv[1], "ik") ? 2 : 0;
	if ((mode == 1 && (argc < 3 || !is_num(argv[2]) || fabs(num(argv[2], 0)) > 180)) ||
	    (mode == 2 && (argc < 5 || !is_num(argv[2]) || !is_num(argv[3]) || !is_num(argv[4]) || fabs(num(argv[2], 0)) > 200 ||
			   fabs(num(argv[3], 0)) > 200 || fabs(num(argv[4], 0)) > 200)) || !mode) {
		logf_("FAIL seq=%u armpose: want rot DEG (|DEG| <= 180), ik X Y Z (cm in Talion's model space, |.| <= 200) or off", seq);
		return;
	}
	if (!g_h[HK_TRK_EVAL].installed) {
		logf_("FAIL seq=%u armpose: the hook P_TRK_EVAL is not installed (its HOOK line says why)", seq);
		return;
	}
	if (arm_resolve(c, seq))
		return;
	g_arm.mode = mode;
	g_arm.seq = seq;
	g_arm.deg = mode == 1 ? (float)num(argv[2], 0) : 0;
	for (int k = 0; mode == 2 && k < 3; k++)
		g_arm.tgt[k] = (float)num(argv[2 + k], 0);
	g_arm.applied = g_arm.skipped = g_arm.seen_n = g_arm.sword_n = 0;
	g_arm.have_after = g_arm.have_seen = g_arm.have_sword = 0;
	g_arm.next_log = mono() + 1.0;
	g_arm.on = 1;
	int ns = 0, ne = 0, nw = 0;
	for (int i = 0; i < g_arm.n; i++)
		ns += g_arm.subS[i], ne += g_arm.subE[i], nw += g_arm.subW[i];
	char hn[96], what[80];
	if (mode == 1)
		cfmt(what, sizeof what, "rot %.1f deg about the forearm", (double)g_arm.deg);
	else
		cfmt(what, sizeof what, "ik to (%.1f %.1f %.1f) cm", (double)g_arm.tgt[0], (double)g_arm.tgt[1], (double)g_arm.tgt[2]);
	logf_("DONE seq=%u armpose %s: tracker %#lx, %d nodes, R_Armu %d R_Arml %d R_Hand %d (subtrees %d/%d/%d), "
	      "the sword %#lx on attach entry %#lx node %u '%s' (hooks P_TRK_EVAL %s, P_ATTACH_LISTEN %s)", seq, what,
	      (unsigned long)g_arm.trk, g_arm.n, g_arm.iS, g_arm.iE, g_arm.iW, ns, ne, nw,
	      (unsigned long)g_arm.sword, (unsigned long)g_arm.sword_entry, g_arm.sword_idx,
	      g_arm.sword_entry ? hash_name(g_arm.obj, r32(g_arm.sword_entry + AE_HASH), hn, sizeof hn) : "",
	      g_h[HK_TRK_EVAL].installed ? "on" : "off", g_h[HK_ATTACH_LISTEN].installed ? "on" : "off");
}

static void arm_tick(const struct ctx *c)
{
	double t = mono();
	if (t < g_arm.next_log)
		return;
	g_arm.next_log = t + 1.0;
	if (arm_resolve(c, 0)) { /* the model changed under us: stop */
		g_arm.on = 0;
		logf_("ARMPOSE seq=%u stopped: the player's model or tracker changed", g_arm.seq);
		return;
	}
	if (!rl_ok(RL_ARM))
		return;
	logf_("ARMPOSE seq=%u: %u frames applied (%u skipped); R_Hand model (%.1f %.1f %.1f) -> (%.1f %.1f %.1f)%s; the next "
	      "player update read R_Hand %.1f cm from ours (%s, %u checks); the sword %.1f cm from object x pose'[node] x offset, "
	      "%.1f cm from the unedited pose (%s, %u checks) t=%.6f", g_arm.seq, g_arm.applied, g_arm.skipped, g_arm.hand0[0],
	      g_arm.hand0[1], g_arm.hand0[2], g_arm.hand1[0], g_arm.hand1[1], g_arm.hand1[2],
	      g_arm.mode == 2 ? (g_arm.resid < 1.0f ? " reached" : " NOT reached") : "", (double)g_arm.seen_err,
	      g_arm.have_seen ? "measured" : "no check yet", g_arm.seen_n, (double)g_arm.sword_err, (double)g_arm.sword_err_stock,
	      g_arm.have_sword ? "measured" : g_arm.sword_entry ? "no check yet" : "no sword entry", g_arm.sword_n, t);
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P8: aggro - orcs given Talion as their scripted combat target (the NPC script binding SetCombatScriptedTarget,  */
/* FUN_02405be0 -> FUN_0296f870(npc, targetCharacter, sticky): FUN_02964f70 (the AI learns the target), then        */
/* npc+0xc78 -> +0xb50..+0xb60 a WBObjRef to the target's HOBJECT (*(target+8)), +0xb68 = sticky; read by          */
/* CAITargetSelectCombatScripted::vf_07 0x2a94d70; ClearCombatScriptedTarget = FUN_0296f940(npc)); minhealth -      */
/* SetMinHealth (FUN_023fa7d0 -> FUN_0269da10: CCharacter+0x9b4), the floor CCharacter::vf_37 clamps damage to      */

typedef void (*scripttgt_fn)(uintptr_t npc, uintptr_t target, int sticky);
typedef void (*scriptclr_fn)(uintptr_t npc, int x);

#define AGG_N 8
static struct {
	int n, sticky;
	unsigned seq;
	double until, next_t;
	uintptr_t target_char, target_h;
	struct { uintptr_t c, npc, ai, hobj; char name[128]; } o[AGG_N];
} g_agg;
static struct { int on; uintptr_t ch; float old, val; unsigned seq; } g_mh;

/* the default filter of pick (not a slave, human, animal, troll or spider), or the SUB substrings */
static int agg_match(const struct orc *o, int nsub, char **subs)
{
	char rn[128];
	if (o->dead || !o->ai || !o->npc)
		return 0;
	rec_name(rq(o->c + C_CHARREC), rn, sizeof rn);
	if (nsub) {
		for (int i = 0; i < nsub; i++)
			if (strcasestr(rn, subs[i]))
				return 1;
		return 0;
	}
	for (size_t k = 0; k < sizeof g_pick_skip / sizeof g_pick_skip[0]; k++)
		if (strcasestr(rn, g_pick_skip[k]))
			return 0;
	return 1;
}

static void agg_clear(unsigned seq, const char *why)
{
	int cleared = 0, n = g_agg.n;
	for (int i = 0; i < g_agg.n; i++) {
		struct orc cur;
		uintptr_t npc = g_agg.o[i].npc, ai = g_agg.o[i].ai;
		if (!orc_get(g_agg.o[i].c, g_pi.pos, &cur) || cur.hobj != g_agg.o[i].hobj || cur.owner != npc ||
		    rq(npc) != G.vt_npc || rq(npc + NPC_AI) != ai || rq(ai + AI_SCRIPTTGT) != g_agg.target_h)
			continue; /* gone, or its scripted target is no longer Talion: leave it */
		if (g_fn_ok[FN_AI_SCRIPTCLR]) {
			FN(FN_AI_SCRIPTCLR, scriptclr_fn)(npc, 1);
			cleared++;
		}
	}
	g_agg.n = 0;
	logf_("AGGRO seq=%u cleared %d of %d scripted targets (%s)", seq, cleared, n, why);
}

static void w_aggro(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "off")) {
		agg_clear(seq, "aggro off");
		logf_("DONE seq=%u aggro off", seq);
		return;
	}
	uintptr_t pch = g_pi.owner, ph = g_pi.phobj;
	if (!g_fn_ok[FN_AI_SCRIPTTGT] || !g_fn_ok[FN_AI_SCRIPTCLR]) {
		logf_("FAIL seq=%u aggro: FUN_0296f870/FUN_0296f940 did not check out", seq);
		return;
	}
	if (!pch || rq(pch) != G.vt_playerobj || !ph || rq(pch + GB_HOBJ) != ph) {
		logf_("FAIL seq=%u aggro: the player's CCharacter %#lx (vtable %#lx) +8 = %#lx, want its HOBJECT %#lx: not the layout "
		      "read in FUN_0296f870 (nothing called)", seq, (unsigned long)pch, (unsigned long)rq(pch),
		      (unsigned long)rq(pch + GB_HOBJ), (unsigned long)ph);
		return;
	}
	int want = 1, sticky = 1, nsub = 0;
	char *subs[16];
	for (int i = 1; i < argc; i++) {
		if (is_num(argv[i]))
			want = (int)num(argv[i], 1);
		else if (!strcmp(argv[i], "loose"))
			sticky = 0;
		else if (nsub < 16)
			subs[nsub++] = argv[i];
	}
	want = want < 1 ? 1 : want > AGG_N ? AGG_N : want;
	if (g_agg.n)
		agg_clear(seq, "a new aggro");
	struct orc cand[64 + 1];
	int nc = 0;
	struct orc po;
	if (pick_get(&po) && agg_match(&po, nsub, subs))
		cand[nc++] = po; /* the picked orc first */
	struct orc o[64];
	int n = orcs_collect(c, o, 64);
	for (int i = 0; i < n && nc < 65; i++) {
		if (o[i].dist > 4000.0f || !agg_match(&o[i], nsub, subs) || (nc && o[i].c == cand[0].c))
			continue;
		cand[nc++] = o[i];
	}
	g_agg.target_char = pch;
	g_agg.target_h = ph;
	g_agg.sticky = sticky;
	g_agg.seq = seq;
	char rn[128], an[128];
	for (int i = 0; i < nc && g_agg.n < want; i++) {
		struct orc *x = &cand[i];
		uintptr_t npc = x->owner, ai = rq(npc + NPC_AI), tmp;
		rec_name(rq(x->c + C_CHARREC), rn, sizeof rn);
		if (rq(npc) != G.vt_npc || !ai || rq(npc + GB_HOBJ) != x->hobj || rd(ai + AI_SCRIPTTGT, &tmp, 8)) {
			logf_("AGGRO seq=%u skip c=%#lx '%s': npc %#lx (vtable %#lx) ai %#lx +8 %#lx (want %#lx)", seq, (unsigned long)x->c, rn,
			      (unsigned long)npc, (unsigned long)rq(npc), (unsigned long)ai, (unsigned long)rq(npc + GB_HOBJ),
			      (unsigned long)x->hobj);
			continue;
		}
		uintptr_t before = rq(ai + AI_SCRIPTTGT);
		uint8_t bs = rb(ai + AI_STICKY);
		FN(FN_AI_SCRIPTTGT, scripttgt_fn)(npc, pch, sticky);
		uintptr_t after = rq(ai + AI_SCRIPTTGT);
		int k = g_agg.n++;
		g_agg.o[k].c = x->c, g_agg.o[k].npc = npc, g_agg.o[k].ai = ai, g_agg.o[k].hobj = x->hobj;
		snprintf(g_agg.o[k].name, sizeof g_agg.o[k].name, "%s", rn);
		logf_("AGGRO seq=%u orc %d c=%#lx '%s' dist=%.1f m npc=%#lx ai=%#lx: scripted target %#lx/%u -> %#lx/%u (%s); its "
		      "combat target %#lx, attack '%s' t=%.6f", seq, k, (unsigned long)x->c, rn, x->dist / 100.0f, (unsigned long)npc,
		      (unsigned long)ai, (unsigned long)before, bs, (unsigned long)after, rb(ai + AI_STICKY),
		      after == ph ? "Talion" : "NOT Talion", (unsigned long)rq(x->c + C_TARGET),
		      rec_name(rq(x->c + C_ATTACK), an, sizeof an), mono());
	}
	g_agg.until = mono() + 40.0;
	g_agg.next_t = 0;
	logf_("DONE seq=%u aggro: %d of %d candidates have Talion as their scripted combat target (sticky %d); AGGROWATCH for "
	      "40 s; `aggro off` clears it", seq, g_agg.n, nc, sticky);
}

static void agg_tick(void)
{
	double t = mono();
	if (t > g_agg.until || t < g_agg.next_t)
		return;
	g_agg.next_t = t + 1.0;
	char an[128];
	uintptr_t pch = g_agg.target_char;
	for (int i = 0; i < g_agg.n; i++) {
		struct orc cur;
		if (!orc_get(g_agg.o[i].c, g_pi.pos, &cur) || cur.hobj != g_agg.o[i].hobj)
			continue;
		uintptr_t tg = rq(cur.c + C_TARGET);
		if (!rl_ok(RL_AGGRO))
			return;
		logf_("AGGROWATCH orc %d '%s' dist=%.1f m dead=%d combat target %s attack '%s' parry=%u scripted %s; Talion health "
		      "%.1f (min %.1f) t=%.6f", i, g_agg.o[i].name, cur.dist / 100.0f, cur.dead,
		      tg == g_agg.target_h ? "TALION" : tg ? "other" : "none", rec_name(rq(cur.c + C_ATTACK), an, sizeof an),
		      rb(cur.c + C_WIN_PARRY), rq(g_agg.o[i].ai + AI_SCRIPTTGT) == g_agg.target_h ? "Talion" : "cleared",
		      (double)rf(pch + CH_HEALTH), (double)rf(pch + CH_MINHEALTH), t);
	}
}

static void w_minhealth(unsigned seq, int argc, char **argv)
{
	uintptr_t ch = g_pi.owner;
	if (!ch || rq(ch) != G.vt_playerobj) {
		logf_("FAIL seq=%u minhealth: the player's CCharacter %#lx has vtable %#lx, want CPlayerObj %#lx", seq,
		      (unsigned long)ch, (unsigned long)rq(ch), (unsigned long)G.vt_playerobj);
		return;
	}
	if (argc < 2 || !strcmp(argv[1], "off")) {
		int back = g_mh.on && g_mh.ch == ch;
		if (back)
			memcpy((void *)(ch + CH_MINHEALTH), &g_mh.old, 4);
		g_mh.on = 0;
		logf_("DONE seq=%u minhealth off: %s (now %g, health %.1f)", seq, back ? "restored" : "nothing to restore",
		      (double)rf(ch + CH_MINHEALTH), (double)rf(ch + CH_HEALTH));
		return;
	}
	float v = (float)num(argv[1], -1);
	if (!(v > 0 && v <= 100000)) {
		logf_("FAIL seq=%u minhealth: want 0 < V <= 100000 or off", seq);
		return;
	}
	if (!g_mh.on || g_mh.ch != ch) {
		g_mh.old = rf(ch + CH_MINHEALTH);
		g_mh.ch = ch;
	}
	memcpy((void *)(ch + CH_MINHEALTH), &v, 4);
	g_mh.on = 1;
	g_mh.val = v;
	g_mh.seq = seq;
	logf_("DONE seq=%u minhealth %g (was %g): Talion's health %.1f cannot be taken below it (CCharacter::vf_37 clamps the "
	      "damage while the hit's own Damage.MinHealth is 0)", seq, (double)v, (double)g_mh.old, (double)rf(ch + CH_HEALTH));
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P8: the UI mouse (re-ui-pointer.md): mouselog, mstate, pointer, click, padguard, mcmd, hitscan                   */

typedef void (*immove_fn)(uintptr_t im, int pl, const int *pos);
typedef void (*imbtn_fn)(uintptr_t im, int pl, int btn, const int *pos);
typedef struct { float x, y; } f2;
typedef f2 (*mstate_fn)(uintptr_t view);
typedef uintptr_t (*clipview_fn)(uintptr_t clip);
typedef f2 (*tostage_fn)(uintptr_t view, int x, int y);
typedef uint64_t (*cliphit_fn)(uintptr_t clip, uint64_t xy);

static int g_mouselog, g_padguard_off;
static _Atomic uint64_t g_guard_n[2], g_guard_over[2];
static struct { int have, x, y, btn, down, pl; uint64_t up_tick; unsigned seq; } g_ptr;
static struct { int cmd, input, pending; unsigned seq; uintptr_t pi; uint64_t tick; } g_mc;
static uintptr_t g_ui_profile; /* the local user's profile (+0x238 = pad mode), cached on the game tick */

/* the player index the mouse path passes (re-ui-pointer.md M2): the player's +0x431, 0xff at the front end, else -1 */
static int ui_pl(const struct ctx *c)
{
	if (c->player)
		return rb(c->player + P_PLINDEX);
	return c->ifs == 1 ? 0xff : -1;
}

/* FUN_00cb52e0(ui) = *(*(ui+8)+0x50): the CUIView of m_GameUI (im+0x2708) or m_MessageBoxUI (im+0x2a50) */
static uintptr_t ui_view(uintptr_t im, uintptr_t off)
{
	uintptr_t v = im ? rq(rq(im + off + 8) + 0x50) : 0;
	return v && rq(v) == G.vt_uiview ? v : 0;
}
static uintptr_t ui_gameview(uintptr_t im) { return ui_view(im, IM_GAMEUI); }

static void ui_size(const struct ctx *c, int *iw, int *ih)
{
	int r[4] = { 0, 0, 0, 0 };
	if (c->shell)
		rd(c->shell + SH_UIRECT, r, 16);
	*iw = r[2] - r[0];
	*ih = r[3] - r[1];
}

/* the front end's menu state machine: *(*(*im + 0x10) + 0x10) (FUN_00a0fa20 -> FUN_00a23080), its vtable checked */
static uintptr_t ui_menusm(uintptr_t im)
{
	uintptr_t s = im ? rq(rq(rq(im) + 0x10) + 0x10) : 0;
	return s && rq(s) == G.vt_menusm ? s : 0;
}

static uintptr_t ui_puism(uintptr_t player)
{
	uintptr_t pui = player ? rq(player + P_UI) : 0, sm = pui ? rq(pui + 0xb0) : 0;
	return sm && rq(sm) == G.vt_puism ? sm : 0;
}

/* CGameSystemEventHandler vf_07/08 (h, button, short pos[2]) and vf_09 (h, short pos[2]) (pre, log only) */
static void sys_log(const char *what, uint64_t h, int btn, uintptr_t pos)
{
	short xy[2] = { 0, 0 };
	rd(pos, xy, 4);
	uintptr_t shell = rq(G.shell_pp), im = shell ? rq(shell + SH_IM) : 0, P = g_pi.player, ms = ui_menusm(im), ps = ui_puism(P);
	logf_("MOUSE sys %s btn=%d x=%d y=%d h=%#lx%s active(h+0x10)=%u up(h+0x12)=%u gate(0x46a95c4)=%u ifs=%d menu-sm=%d "
	      "player-ui=%d modal=%u P=%#lx padmode=%d user=%d tid=%d t=%.6f", what, btn, xy[0], xy[1], (unsigned long)h,
	      rq(h) == G.vt_sysh ? "" : " (vtable?)", rb(h + 0x10), rb(h + 0x12), rb(G.sys_gate), im ? msm_state(rq(im)) : -9,
	      ms ? msm_state(ms) : -9, ps ? msm_state(ps) : -9, im ? rb(im + 0x2a48) : 0, (unsigned long)P,
	      g_ui_profile ? rb(g_ui_profile + PROF_PAD) : -1, shell ? (int)r32(shell + SH_LOCALS) : -1, my_tid(), mono());
}
static uint64_t p_sys_down(uint64_t h, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e)
{
	first_call(HK_LP_SYS_DOWN, atomic_fetch_add_explicit(&g_h[HK_LP_SYS_DOWN].calls, 1, memory_order_relaxed) + 1);
	if (g_mouselog && rl_ok(RL_MOUSE))
		sys_log("down", h, (int)a, (uintptr_t)b);
	return ((six_fn)g_h[HK_LP_SYS_DOWN].orig)(h, a, b, c, d, e);
}
static uint64_t p_sys_up(uint64_t h, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e)
{
	first_call(HK_LP_SYS_UP, atomic_fetch_add_explicit(&g_h[HK_LP_SYS_UP].calls, 1, memory_order_relaxed) + 1);
	if (g_mouselog && rl_ok(RL_MOUSE))
		sys_log("up", h, (int)a, (uintptr_t)b);
	return ((six_fn)g_h[HK_LP_SYS_UP].orig)(h, a, b, c, d, e);
}
static uint64_t p_sys_move(uint64_t h, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e)
{
	first_call(HK_LP_SYS_MOVE, atomic_fetch_add_explicit(&g_h[HK_LP_SYS_MOVE].calls, 1, memory_order_relaxed) + 1);
	if (g_mouselog && rl_ok(RL_MOUSE))
		sys_log("move", h, -1, (uintptr_t)a);
	return ((six_fn)g_h[HK_LP_SYS_MOVE].orig)(h, a, b, c, d, e);
}

/* CUIView vf_65/66 (view, button, x, y) -> the HitTest result, vf_67 (view, short x, short y) (post) */
static void view_log(const char *what, uint64_t v, int btn, int x, int y, uint64_t r)
{
	uintptr_t shell = rq(G.shell_pp), im = shell ? rq(shell + SH_IM) : 0;
	logf_("MOUSE view %s %#lx (%s) btn=%d x=%d y=%d offset (%d, %d) result=%#lx t=%.6f", what, (unsigned long)v,
	      v == ui_gameview(im) ? "m_GameUI" : v == ui_view(im, IM_MSGBOX) ? "the message box" : "another view", btn, x, y,
	      (int)r32(v + VIEW_OFS), (int)r32(v + VIEW_OFS + 4), (unsigned long)r, mono());
}
static uint64_t p_view_down(uint64_t v, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e)
{
	uint64_t r = ((six_fn)g_h[HK_LP_VIEW_DOWN].orig)(v, a, b, c, d, e);
	first_call(HK_LP_VIEW_DOWN, atomic_fetch_add_explicit(&g_h[HK_LP_VIEW_DOWN].calls, 1, memory_order_relaxed) + 1);
	if (g_mouselog && rl_ok(RL_MOUSE))
		view_log("down", v, (int)a, (int)b, (int)c, r);
	return r;
}
static uint64_t p_view_up(uint64_t v, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e)
{
	uint64_t r = ((six_fn)g_h[HK_LP_VIEW_UP].orig)(v, a, b, c, d, e);
	first_call(HK_LP_VIEW_UP, atomic_fetch_add_explicit(&g_h[HK_LP_VIEW_UP].calls, 1, memory_order_relaxed) + 1);
	if (g_mouselog && rl_ok(RL_MOUSE))
		view_log("up", v, (int)a, (int)b, (int)c, r);
	return r;
}
static uint64_t p_view_move(uint64_t v, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e)
{
	uint64_t r = ((six_fn)g_h[HK_LP_VIEW_MOVE].orig)(v, a, b, c, d, e);
	first_call(HK_LP_VIEW_MOVE, atomic_fetch_add_explicit(&g_h[HK_LP_VIEW_MOVE].calls, 1, memory_order_relaxed) + 1);
	if (g_mouselog && rl_ok(RL_MOUSE))
		view_log("move", v, -1, (short)a, (short)b, r);
	return r;
}

/* GFx MovieImpl::vf_36 HandleEvent(movie, const Event&) (pre, log only): the mouse (1 move, 2 down, 3 up, 4 wheel) and
 * the focus events (8, 9) */
static uint64_t p_gfx_event(uint64_t mv, uint64_t ev, uint64_t c, uint64_t d, uint64_t e, uint64_t f)
{
	first_call(HK_LP_GFX_EVENT, atomic_fetch_add_explicit(&g_h[HK_LP_GFX_EVENT].calls, 1, memory_order_relaxed) + 1);
	if (g_mouselog) {
		uint32_t ty = r32(ev);
		if (((ty >= 1 && ty <= 4) || ty == 8 || ty == 9) && rl_ok(RL_MOUSE)) {
			static const char *const tn[10] = { "?", "move", "down", "up", "wheel", "?", "?", "?", "setfocus", "killfocus" };
			logf_("MOUSE gfx %s movie=%#lx x=%.1f y=%.1f button=%u index=%u focus=%u cursors=%u t=%.6f", tn[ty],
			      (unsigned long)mv, (double)rf(ev + 8), (double)rf(ev + 0xc), r32(ev + 0x14), r32(ev + 0x18),
			      (r32(mv + MV_FOCUS) >> 18) & 1, r32(mv + MV_CURSORS), mono());
		}
	}
	return ((six_fn)g_h[HK_LP_GFX_EVENT].orig)(mv, ev, c, d, e, f);
}

/* the menu items' pad guard FUN_00a38e80(P) at 0xb4e5d8 (hover) / 0xb4e62c (click): 0 while `padguard off` */
static void guard_common(int hk, int k, struct regs *r)
{
	first_call(hk, atomic_fetch_add_explicit(&g_h[hk].calls, 1, memory_order_relaxed) + 1);
	uint64_t res = ((arg1_fn)g_h[hk].orig)(r->rdi);
	atomic_fetch_add(&g_guard_n[k], 1);
	r->rax = res;
	int over = g_padguard_off && (res & 0xff);
	if (over) {
		r->rax = 0;
		atomic_fetch_add(&g_guard_over[k], 1);
	}
	if (g_mouselog && rl_ok(RL_MOUSE))
		logf_("MOUSE guard %s P=%#lx pad=%u -> %u%s t=%.6f", k ? "click" : "hover", (unsigned long)r->rdi, (unsigned)(res & 0xff),
		      (unsigned)(r->rax & 0xff), over ? " (padguard off: the item acts)" : "", mono());
}
__attribute__((used)) void somvr_pc_guard_hover(struct regs *r) { guard_common(HK_LP_GUARD_HOVER, 0, r); }
__attribute__((used)) void somvr_pc_guard_click(struct regs *r) { guard_common(HK_LP_GUARD_CLICK, 1, r); }

static void w_mstate(const struct ctx *c, unsigned seq)
{
	if (!c->im) {
		logf_("FAIL seq=%u mstate: no interface manager", seq);
		return;
	}
	int xy[2] = { 0, 0 }, iw, ih;
	float uv[2] = { 0, 0 };
	rd(c->im + IM_MOUSE, xy, 8);
	rd(c->im + IM_MOUSE + 8, uv, 8);
	ui_size(c, &iw, &ih);
	uintptr_t gv = ui_gameview(c->im), mv = gv ? rq(gv + VIEW_MOVIE) : 0, mb = ui_view(c->im, IM_MSGBOX);
	int mvok = mv && rq(mv) == G.vt_movie;
	f2 ms = { 0, 0 };
	if (gv && mvok && g_fn_ok[FN_VIEW_MSTATE])
		ms = FN(FN_VIEW_MSTATE, mstate_fn)(gv);
	uintptr_t msm = ui_menusm(c->im), ps = ui_puism(c->player);
	logf_("MSTATE im+0x3210 (%d, %d) (%.4f, %.4f); UI %dx%d; m_GameUI view %#lx offset (%d, %d) movie %#lx%s: its mouse "
	      "(%.1f, %.1f) focus=%u cursors=%u; message box view %#lx; ifs=%d menu-sm=%d player-ui=%d modal=%u padmode=%d pl=%d "
	      "pointer %s (%d, %d) padguard %s", xy[0], xy[1], (double)uv[0], (double)uv[1], iw, ih, (unsigned long)gv,
	      gv ? (int)r32(gv + VIEW_OFS) : 0, gv ? (int)r32(gv + VIEW_OFS + 4) : 0, (unsigned long)mv, mvok ? "" : " (not a MovieImpl)",
	      (double)ms.x, (double)ms.y, mvok ? (r32(mv + MV_FOCUS) >> 18) & 1 : 0, mvok ? r32(mv + MV_CURSORS) : 0,
	      (unsigned long)mb, c->ifs, msm ? msm_state(msm) : -9, ps ? msm_state(ps) : -9, rb(c->im + 0x2a48),
	      c->profile ? rb(c->profile + PROF_PAD) : -1, ui_pl(c), g_ptr.have ? "at" : "none", g_ptr.x, g_ptr.y,
	      g_padguard_off ? "off" : "on");
	logf_("DONE seq=%u mstate", seq);
}

static void w_pointer(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	int pl = ui_pl(c), iw, ih, x, y;
	if (!c->im || !g_fn_ok[FN_IM_MOVE]) {
		logf_("FAIL seq=%u pointer: no interface manager or FUN_00a0fa20 did not check out", seq);
		return;
	}
	if (pl < 0) {
		logf_("FAIL seq=%u pointer: no local player and not the front end (the game drops the mouse there)", seq);
		return;
	}
	ui_size(c, &iw, &ih);
	uintptr_t gv = ui_gameview(c->im);
	int vx = gv ? (int)r32(gv + VIEW_OFS) : 0, vy = gv ? (int)r32(gv + VIEW_OFS + 4) : 0;
	if (argc > 1 && !strcmp(argv[1], "off")) {
		x = y = -1000; /* off the panel: GFx's roll-out */
	} else if (argc > 2 && !strncmp(argv[1], "u=", 2) && !strncmp(argv[2], "v=", 2) && is_num(argv[1] + 2) && is_num(argv[2] + 2)) {
		x = (int)lround(num(argv[1] + 2, 0) * iw) + vx;
		y = (int)lround(num(argv[2] + 2, 0) * ih) + vy;
	} else if (argc > 2 && is_num(argv[1]) && is_num(argv[2]) && fabs(num(argv[1], 0)) < 20000 && fabs(num(argv[2], 0)) < 20000) {
		x = (int)num(argv[1], 0);
		y = (int)num(argv[2], 0);
	} else {
		logf_("FAIL seq=%u pointer: want X Y (UI pixels), u=U v=V (0..1 of the UI) or off", seq);
		return;
	}
	int pos[2] = { x, y }, now[2] = { 0, 0 };
	FN(FN_IM_MOVE, immove_fn)(c->im, pl, pos);
	rd(c->im + IM_MOUSE, now, 8);
	g_ptr.have = x > -1000;
	g_ptr.x = x, g_ptr.y = y, g_ptr.pl = pl, g_ptr.seq = seq;
	logf_("DONE seq=%u pointer (%d, %d) pl=%d: im+0x3210 now (%d, %d); UI %dx%d, view offset (%d, %d)", seq, x, y, pl, now[0],
	      now[1], iw, ih, vx, vy);
}

static void w_click(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	int pl = ui_pl(c);
	int btn = argc > 1 && is_num(argv[1]) ? (int)num(argv[1], 1) : 1, frames = argc > 2 && is_num(argv[2]) ? (int)num(argv[2], 2) : 2;
	if (!g_ptr.have || !c->im || pl < 0 || !g_fn_ok[FN_IM_MOVE] || !g_fn_ok[FN_IM_DOWN] || !g_fn_ok[FN_IM_UP]) {
		logf_("FAIL seq=%u click: no pointer position (pointer X Y first), no player/front end or a function did not check out",
		      seq);
		return;
	}
	if (g_ptr.down) {
		logf_("FAIL seq=%u click: the click of seq %u is still held", seq, g_ptr.seq);
		return;
	}
	btn = btn < 1 ? 1 : btn > 5 ? 5 : btn;
	frames = frames < 1 ? 1 : frames > 600 ? 600 : frames;
	int pos[2] = { g_ptr.x, g_ptr.y };
	FN(FN_IM_MOVE, immove_fn)(c->im, pl, pos);
	FN(FN_IM_DOWN, imbtn_fn)(c->im, pl, btn, pos);
	g_ptr.down = 1, g_ptr.btn = btn, g_ptr.pl = pl, g_ptr.seq = seq;
	g_ptr.up_tick = g_tick + (uint64_t)frames;
	logf_("DONE seq=%u click button %d at (%d, %d) pl=%d: down now (FUN_00a0f760), up after %d frames", seq, btn, pos[0], pos[1],
	      pl, frames);
}

static void w_padguard(unsigned seq, const char *arg)
{
	g_padguard_off = arg && !strcmp(arg, "off");
	logf_("DONE seq=%u padguard %s: FUN_00a38e80 in the menu items' hover/click callbacks %s (hooks %s/%s); so far hover %llu "
	      "calls (%llu overridden), click %llu (%llu)", seq, g_padguard_off ? "off" : "on", g_padguard_off ?
	      "answers 0 (the items act in pad mode)" : "answers as the game does", g_h[HK_LP_GUARD_HOVER].installed ? "on" : "off",
	      g_h[HK_LP_GUARD_CLICK].installed ? "on" : "off", (unsigned long long)atomic_load(&g_guard_n[0]),
	      (unsigned long long)atomic_load(&g_guard_over[0]), (unsigned long long)atomic_load(&g_guard_n[1]),
	      (unsigned long long)atomic_load(&g_guard_over[1]));
}

static void w_mcmd(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	int id = argc > 1 ? parse_cmd_id(argv[1]) : -1;
	if (!c->pi || !g_fn_ok[FN_CMD_ON] || !g_fn_ok[FN_CMD_OFF] || id < 0) {
		logf_("FAIL seq=%u mcmd: want a command id or name (PlayerInput %#lx)", seq, (unsigned long)c->pi);
		return;
	}
	if (g_mc.pending) {
		logf_("FAIL seq=%u mcmd: the command of seq %u is still on", seq, g_mc.seq);
		return;
	}
	uintptr_t rec = pi_rec(c->pi, id);
	int input = rec ? first_input(rec) : id == 100 ? 1000 : id == 101 ? 1001 : 0;
	FN(FN_CMD_ON, cmd_fn)(c->pi, id, input);
	g_mc.cmd = id, g_mc.input = input, g_mc.pending = 1, g_mc.seq = seq, g_mc.pi = c->pi, g_mc.tick = g_tick + 1;
	logf_("DONE seq=%u mcmd %s (%d) ON with input %d (pointer %s (%d, %d)); OFF next frame", seq, g_cmd_names[id], id, input,
	      g_ptr.have ? "at" : "none", g_ptr.x, g_ptr.y);
}

/* the menu manager that takes the mouse now (re-ui-pointer.md 1.2): front end states 0xf / 0xc, the player UI's 5 */
static uintptr_t ui_menu_mgr(const struct ctx *c, char *where, size_t cap)
{
	if (c->ifs == 1) {
		uintptr_t ms = ui_menusm(c->im);
		int st = ms ? msm_state(ms) : -9;
		uintptr_t mgr = st == 0xf ? ms + MSM_MAINMENU : st == 0xc ? ms + MSM_MENUC : 0;
		cfmt(where, cap, "front end, menu state %d%s", st, mgr ? "" : " (no mouse manager in this state)");
		return mgr;
	}
	uintptr_t ps = ui_puism(c->player);
	int st = ps ? msm_state(ps) : -9;
	cfmt(where, cap, "player UI state %d%s", st, st == 5 ? " (the pause menus)" : " (not 5: no menu manager takes the mouse)");
	return st == 5 ? ps + PUISM_MGR : 0;
}

static void w_hitscan(const struct ctx *c, unsigned seq)
{
	char where[160];
	if (!g_ptr.have) {
		logf_("FAIL seq=%u hitscan: no pointer position (pointer X Y first)", seq);
		return;
	}
	uintptr_t mgr = ui_menu_mgr(c, where, sizeof where);
	int nl = mgr ? (int)r32(mgr + MGR_NLAYERS) : 0;
	if (!mgr || nl < 1 || nl > 8) {
		logf_("FAIL seq=%u hitscan: %s, %d layers", seq, where, nl);
		return;
	}
	uintptr_t layer = rq(mgr + MGR_LAYERS + 8 * (uintptr_t)(nl - 1)), b = layer ? rq(layer + LAYER_COMPS) : 0;
	uintptr_t e = layer ? rq(layer + LAYER_COMPS + 8) : 0;
	if (!b || e < b || (e - b) % 8 || (e - b) / 8 > 64) {
		logf_("FAIL seq=%u hitscan: %s: layer %#lx without a component list", seq, where, (unsigned long)layer);
		return;
	}
	int n = (int)((e - b) / 8), focus = (int)r32(layer + LAYER_FOCUS), tested = 0, hits = 0;
	for (int i = 0; i < n; i++) {
		uintptr_t comp = rq(b + 8 * (uintptr_t)i), clip = comp ? rq(comp + COMP_CLIP) : 0, vt = clip ? rq(clip) : 0;
		if (!vt || rq(vt + 0x3c0) != G.fn[FN_CLIP_VIEW] || rq(vt + 0x368) != G.fn[FN_CLIP_HITTEST] || !g_fn_ok[FN_CLIP_VIEW] ||
		    !g_fn_ok[FN_CLIP_HITTEST] || !g_fn_ok[FN_VIEW_TOSTAGE]) {
			logf_("HITSCAN %d comp=%#lx clip=%#lx vtable %#lx: not a CUIMovieClip clip (slots 120/109 %#lx/%#lx)", i,
			      (unsigned long)comp, (unsigned long)clip, (unsigned long)vt, (unsigned long)rq(vt + 0x3c0),
			      (unsigned long)rq(vt + 0x368));
			continue;
		}
		uintptr_t view = FN(FN_CLIP_VIEW, clipview_fn)(clip);
		f2 st = { 0, 0 };
		int hit = -1;
		if (view) {
			st = FN(FN_VIEW_TOSTAGE, tostage_fn)(view, g_ptr.x, g_ptr.y);
			uint64_t xy = (uint64_t)(uint32_t)(int)st.x | (uint64_t)(uint32_t)(int)st.y << 32;
			hit = (int)(FN(FN_CLIP_HITTEST, cliphit_fn)(clip, xy) & 0xff);
		}
		tested++;
		hits += hit == 1;
		logf_("HITSCAN %d comp=%#lx clip=%#lx view=%#lx stage (%.1f, %.1f) hit=%d%s", i, (unsigned long)comp, (unsigned long)clip,
		      (unsigned long)view, (double)st.x, (double)st.y, hit, i == focus ? " (the focused item)" : "");
	}
	logf_("DONE seq=%u hitscan at (%d, %d): %s, layer %d of %d %#lx (override %u), %d components, %d tested, %d hit, focus %d",
	      seq, g_ptr.x, g_ptr.y, where, nl, nl, (unsigned long)layer, rb(mgr + MGR_OVERRIDE), n, tested, hits, focus);
}

static void ui_tick(const struct ctx *c)
{
	g_ui_profile = c->profile;
	if (g_ptr.down && g_tick >= g_ptr.up_tick) {
		int pos[2] = { g_ptr.x, g_ptr.y };
		if (c->im && g_fn_ok[FN_IM_UP])
			FN(FN_IM_UP, imbtn_fn)(c->im, g_ptr.pl, g_ptr.btn, pos);
		g_ptr.down = 0;
		logf_("CLICK seq=%u up (FUN_00a0f8c0) button %d at (%d, %d) t=%.6f", g_ptr.seq, g_ptr.btn, pos[0], pos[1], mono());
	}
	if (g_mc.pending && g_tick >= g_mc.tick) {
		if (c->pi == g_mc.pi)
			FN(FN_CMD_OFF, cmd_fn)(g_mc.pi, g_mc.cmd, g_mc.input);
		g_mc.pending = 0;
		logf_("MCMD seq=%u %s OFF%s", g_mc.seq, g_cmd_names[g_mc.cmd], c->pi == g_mc.pi ? "" : " skipped (PlayerInput changed)");
	}
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P8: surf, grav, mount (re-traversal.md 2.6, 3.6)                                                               */

typedef uint64_t (*raycast_fn)(uintptr_t phys, void *q, void *r);
typedef uintptr_t (*surfrec_fn)(void *r);

/* one G21 ray (IEnginePhysics::vf_64 -> FUN_008de5e0: query {from, to, flags 0x58, filter 0xfffffffd} as the combat
 * collider casts, result {hit pos, normal, fraction, +0x20 object, +0x28 triangle surface id, +0x2a}) and its surface */
static void surf_ray(unsigned seq, const char *what, const float from[3], const float dir[3], float len)
{
	uintptr_t phys = rq(G.phys_pp), vt = phys ? rq(phys) : 0;
	if (!g_ro || !g_ro_ok[RO_PHYS_RAY] || vt != G.vt_phys || vt + 0x200 != g_ro[RO_PHYS_RAY].addr ||
	    rq(vt + 0x200) != g_ro[RO_PHYS_RAY].target) {
		logf_("FAIL seq=%u surf: the physics object %#lx (vtable %#lx) is not the checked IEnginePhysics", seq,
		      (unsigned long)phys, (unsigned long)vt);
		return;
	}
	unsigned char q[0x40] __attribute__((aligned(16))), r[0x80] __attribute__((aligned(16)));
	float to[3] = { from[0] + dir[0] * len, from[1] + dir[1] * len, from[2] + dir[2] * len }, hit[3], nrm[3], fr;
	uint32_t fl = 0x58, flt = 0xfffffffdu;
	memset(q, 0, sizeof q);
	memset(r, 0, sizeof r);
	memcpy(q, from, 12);
	memcpy(q + 12, to, 12);
	memcpy(q + 0x18, &fl, 4);
	memcpy(q + 0x1c, &flt, 4);
	uint64_t rc = ((raycast_fn)g_ro[RO_PHYS_RAY].target)(phys, q, r);
	uintptr_t obj;
	uint16_t tri;
	memcpy(hit, r, 12);
	memcpy(nrm, r + 0xc, 12);
	memcpy(&fr, r + 0x18, 4);
	memcpy(&obj, r + 0x20, 8);
	memcpy(&tri, r + 0x28, 2);
	uint16_t osurf = 0;
	if (obj)
		rd(obj + 0x5a, &osurf, 2);
	uintptr_t rec = g_fn_ok[FN_SURF_FROM_RAY] ? FN(FN_SURF_FROM_RAY, surfrec_fn)(r) : 0;
	char rn[128], cw[64], sl[64];
	logf_("SURF seq=%u %s rc=%u from (%.1f %.1f %.1f) along (%.3f %.3f %.3f) %.0f cm: hit (%.1f %.1f %.1f) at %.1f cm normal "
	      "(%.3f %.3f %.3f) fraction %g object %#lx%s surface %u triangle %u/%u record %#lx '%s' CanWallClimb=%s IsSlippery=%s",
	      seq, what, (unsigned)(rc & 0xff), from[0], from[1], from[2], dir[0], dir[1], dir[2], (double)len, hit[0], hit[1], hit[2],
	      obj || (rc & 0xff) ? dist3(hit, from) : -1.0f, nrm[0], nrm[1], nrm[2], (double)fr, (unsigned long)obj,
	      obj && (obj == g_pi.phobj || obj == g_pi.charobj) ? " (THE PLAYER)" : "", osurf, tri, r[0x2a], (unsigned long)rec,
	      rec_name(rec, rn, sizeof rn), db_str(rec, G.sch_surface, "CanWallClimb", cw, sizeof cw),
	      db_str(rec, G.sch_surface, "IsSlippery", sl, sizeof sl));
}

static void w_surf(const struct ctx *c, unsigned seq, const char *arg)
{
	float m = (float)num(arg, 2);
	m = m < 0.2f ? 0.2f : m > 10.0f ? 10.0f : m;
	uintptr_t o = char_obj(c);
	float hx[8] = { 0 }, fz[3] = { 0, 0, 1 }, f[3];
	uint32_t hn;
	if (!o || head_node(o, &hn) || node_xf(o, hn, hx)) {
		logf_("FAIL seq=%u surf: no head node on the player object %#lx", seq, (unsigned long)o);
		return;
	}
	qrot(g_pi.rot, fz, f);
	surf_ray(seq, "head along Talion's forward", hx, f, m * 100.0f);
	float cp[3], q0[4], q1[4], q[4];
	if (c->cam && !rvec(c->cam + CAM_POS, cp, 3) && !rvec(c->cam + CAM_ROT, q0, 4) && !rvec(c->cam + CAM_EXTRA, q1, 4)) {
		qmul(q0, q1, q);
		qrot(q, fz, f);
		surf_ray(seq, "camera along its view", cp, f, m * 100.0f + 200.0f);
	}
	logf_("DONE seq=%u surf %.1f m", seq, (double)m);
}

static struct { int off; uintptr_t pm; unsigned seq; _Atomic uint64_t calls, over; } g_grav;

/* PlayerMovement::vf_06 (the gravity wish of the movement step, re-traversal.md 2.3/2.4): 0 while `grav off` */
static uint64_t p_grav(uint64_t pm, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f)
{
	uint64_t r = ((six_fn)g_h[HK_GRAV].orig)(pm, b, c, d, e, f);
	first_call(HK_GRAV, atomic_fetch_add_explicit(&g_h[HK_GRAV].calls, 1, memory_order_relaxed) + 1);
	atomic_fetch_add_explicit(&g_grav.calls, 1, memory_order_relaxed);
	if (g_grav.off && pm == g_grav.pm) {
		atomic_fetch_add_explicit(&g_grav.over, 1, memory_order_relaxed);
		return 0;
	}
	return r;
}

static void w_grav(const struct ctx *c, unsigned seq, const char *arg)
{
	uintptr_t pm = c->player ? rq(c->player + P_PM) : 0, pc = pm ? rq(pm + PM_PHYS) : 0;
	if (!pm || rq(pm) != G.vt_pm || !g_h[HK_GRAV].installed) {
		logf_("FAIL seq=%u grav: Player+0x2c8 %#lx is not a PlayerMovement or P_GRAV is not installed", seq, (unsigned long)pm);
		return;
	}
	int off = arg && !strcmp(arg, "off");
	g_grav.pm = pm;
	g_grav.off = off;
	g_grav.seq = seq;
	logf_("DONE seq=%u grav %s: PlayerMovement::vf_06 %s; the proxy %#lx gravity %g on=%u (vf_06 calls %llu, overridden %llu)",
	      seq, off ? "off" : "on", off ? "answers 0 from the next step" : "as the game", (unsigned long)pc,
	      pc ? (double)rf(pc + 0x58) : 0.0, pc ? rb(pc + 0x60) : 0, (unsigned long long)atomic_load(&g_grav.calls),
	      (unsigned long long)atomic_load(&g_grav.over));
}

static struct { int on; unsigned seq, lines; double until, last_t; uint64_t sig; } g_mnt;

static void w_mount(const struct ctx *c, unsigned seq, const char *arg)
{
	double secs = num(arg, 20);
	secs = secs < 0.5 ? 0.5 : secs > 120 ? 120 : secs;
	memset(&g_mnt, 0, sizeof g_mnt);
	g_mnt.on = 1;
	g_mnt.seq = seq;
	g_mnt.until = mono() + secs;
	logf_("DONE seq=%u mount %.1f s: MountInfo, the beast and the rider's attachment on change and every 0.5 s while mounted "
	      "(mounted now: %s)", seq, secs, c->player && rq(c->player + P_MOUNT) ? "yes" : "no");
}

static void mount_tick(const struct ctx *c)
{
	double t = mono();
	if (t >= g_mnt.until || !c->player) {
		logf_("MOUNT seq=%u end: %u lines", g_mnt.seq, g_mnt.lines);
		g_mnt.on = 0;
		return;
	}
	uintptr_t P = c->player, beast = rq(P + P_MOUNT), mi = P + P_MOUNTINFO, pm = rq(P + P_PM);
	char st[0x84];
	rstr(P + P_MOUNTSTATE, st, sizeof st);
	uint8_t att = pm ? rb(pm + 0x1a6) : 0;
	uint64_t sig = beast ^ (uint64_t)att << 56 ^ (uint64_t)(uint8_t)st[0] << 48;
	if (sig == g_mnt.sig && (!beast || t - g_mnt.last_t < 0.5))
		return;
	g_mnt.sig = sig;
	g_mnt.last_t = t;
	g_mnt.lines++;
	float f[12] = { 0 }, bp[3] = { 0, 0, 0 }, bq[4] = { 0, 0, 0, 1 };
	rvec(mi + 0x48, f, 12);
	if (beast)
		obj_pos(beast, bp), rvec(beast + OBJ_ROT, bq, 4);
	logf_("MOUNT %s beast=%#lx pos (%.1f %.1f %.1f) quat (%.3f %.3f %.3f %.3f); MountInfo RawMoveMag %.3f DeltaYaw %.2f "
	      "CameraDeltaYaw %.2f LastSpike %.2f WalkAndTalkScale %.2f aim (%.1f %.1f %.1f) camera quat (%.3f %.3f %.3f %.3f); "
	      "pending state '%s'; rider movement attached=%u parent=%#lx node=%d t=%.6f", beast ? "on" : "off", (unsigned long)beast,
	      bp[0], bp[1], bp[2], bq[0], bq[1], bq[2], bq[3], f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11],
	      st, att, (unsigned long)(pm ? rq(pm + 0x1f8) : 0), pm ? (int)r32(pm + 0x200) : -1, t);
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P8: conlog - the console's output (re-m1m2.md 2.4: the repository's output function *(0x4b5e920 + 8), called as  */
/* fn(colour, level, text) by its printf) logged as CONOUT and passed on; exec captures its own output the same way */

typedef void (*conout_fn)(const uint32_t *colour, int level, const char *text);
static _Atomic uintptr_t g_con_saved;
static _Atomic int g_con_installed;
static int g_conlog;
static _Atomic unsigned g_con_lines;

static void p_conout(const uint32_t *colour, int level, const char *text)
{
	char t[400];
	rstr((uintptr_t)text, t, sizeof t);
	size_t l = strlen(t);
	while (l && (t[l - 1] == '\n' || t[l - 1] == '\r'))
		t[--l] = 0;
	atomic_fetch_add(&g_con_lines, 1);
	if (rl_ok(RL_CONOUT))
		logf_("CONOUT level=%d '%s'", level, t);
	uintptr_t s = atomic_load(&g_con_saved);
	if (s)
		((conout_fn)s)(colour, level, text);
}

/* put p_conout in the repository's output slot (the repository's vtable checked, the old value a function or 0) */
static int con_install(void)
{
	if (atomic_load(&g_con_installed))
		return 0;
	uintptr_t cur = rq(G.repo + 8);
	if (rq(G.repo) != G.vt_repo || (cur && !code_ptr(cur)) || cur == (uintptr_t)p_conout)
		return -1;
	atomic_store(&g_con_saved, cur);
	__atomic_store_n((uintptr_t *)(G.repo + 8), (uintptr_t)p_conout, __ATOMIC_RELEASE);
	atomic_store(&g_con_installed, 1);
	return 0;
}

static void con_remove(void)
{
	if (!atomic_load(&g_con_installed))
		return;
	if (rq(G.repo + 8) == (uintptr_t)p_conout)
		__atomic_store_n((uintptr_t *)(G.repo + 8), atomic_load(&g_con_saved), __ATOMIC_RELEASE);
	atomic_store(&g_con_installed, 0);
}

static void w_conlog(unsigned seq, const char *arg)
{
	int on = !(arg && !strcmp(arg, "off"));
	int rc = 0;
	if (on)
		rc = con_install();
	else
		con_remove();
	g_conlog = on && rc == 0;
	logf_("DONE seq=%u conlog %s%s: the repository's output slot %#lx holds %#lx (saved %#lx), %u lines so far", seq,
	      on ? "on" : "off", rc ? " REFUSED (repository vtable or output slot not as read)" : "", (unsigned long)(G.repo + 8),
	      (unsigned long)rq(G.repo + 8), (unsigned long)atomic_load(&g_con_saved), atomic_load(&g_con_lines));
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P9: test orcs from our code (re-combat-spawn.md 1: FUN_029a4830, AI/Templates, the current faction target), the  */
/* AI's block/dodge trigger (2: the AIBlockTable roll in Combat_StartAttack, the incoming-attack fact FUN_029ac890,  */
/* CAIGoalCounterMelee's relevance, the shield test), the arena light (3) and the bow (re-bow.md 1.1/1.4/6: the     */
/* player weapon modes, the free-aim ray BOW_FA_CAMXF)                                                              */

typedef uintptr_t (*spawn_fn)(uintptr_t ctx, const char *name, const float *xf, uintptr_t tmpl, int a5, int a6, uintptr_t a7,
			      uintptr_t fm, uintptr_t a9);
typedef uintptr_t (*zoneat_fn)(const float *pos, uintptr_t a, uintptr_t b);
typedef void (*setzone_fn)(uintptr_t npc, uintptr_t zone, int x);
typedef void (*setlevel_fn)(uintptr_t combat, uint32_t level);
typedef void (*fmbind_fn)(uintptr_t npc, uintptr_t fm, int x);
typedef void (*fmact_fn)(uintptr_t fm, int on);
typedef void (*aifact_fn)(uintptr_t attacker_owner, uintptr_t npc);
typedef uintptr_t (*todname_fn)(uint32_t tod);
typedef uint32_t (*cmdid_fn)(uintptr_t pred);
typedef uintptr_t (*vget_fn)(uintptr_t self);
typedef uint64_t (*objremove_fn)(uintptr_t objsys, uintptr_t hobj);
typedef float (*rollf_fn)(float lo, float hi);

#define CCR_AIBLOCKTABLE 56 /* character combat record (schema *0x4ba0ca0): AIBlockTable; 61 = WeaponModeSetup */
#define CCR_WMSETUP      61
#define BTBL_ENTRIES     0  /* Combat.AIBlockTable (*0x4ba6460): Entries */
#define BENT_CATEGORY    0  /* CombatAIBlockChance (*0x4ba64e0): Category, BlockChance (default 0.5) */
#define BENT_CHANCE      1
#define ATK_CATEGORY     5  /* Combat/Attack: the categories FUN_00a7c690 reads */
#define NPC_RECHOLD      0x10 /* NPC -> +0x70 the record FUN_026ff330 reads the character combat record from */
#define NPC_WM           0xc70 /* NPC -> its working memory (FUN_029ac890's fact goes there) */
#define BRAIN_REPLAN     0x401 /* *(npc+0xc78)+0x401 = 1: re-plan (FUN_029ac890 sets it) */
#define HOBJ_CLASSES     0x48  /* HOBJECT -> the class list (+0 first, node +8 next) */
#define HOBJ_GAMEOBJ     0x38  /* HOBJECT -> its game object (the NPC) */
#define HOBJ_FLAGS       0x5c  /* byte: 0x08 queued for removal, 0x10 being deleted */
#define FM_FLAGS         0x3a4 /* faction member: bit 1 = already in the world (FUN_022ccee0(fm, 1)) */
#define FM_ACTIVE        0xa0
#define FM_LEVEL         0x340
#define W_RECORD         0x60  /* weather state (G->vtbl+0xa8): the weather record, +0x68 TOD byte, +0x8c index */
#define W_TOD            0x68
#define W_INDEX          0x8c
#define PC_FA            0x458 /* PlayerCombat -> the free-aim block (+0 aim point, +0x18 ray direction, +0xa8 target) */
#define FA_DIR           0x18
#define FA_TARGET        0xa8
#define FA_ACTIVE        0x3d8
#define P_AIMPT          0x214 /* Player: the live aim point the arrow flies to (FUN_026c7cf0) */

/* field IDX of a record (schema HOLDER's entry IDX), up to CAP values; *type and *count (the full count) */
static int db_listi(uintptr_t rec, uintptr_t holder, int idx, uint64_t *out, int cap, int *type, int *count)
{
	unsigned char e[16];
	uint16_t cnt, vi;
	uint32_t h = rec ? sch_hash(holder, idx) : 0;
	*type = *count = 0;
	if (!h || db_entry(rec, h, e))
		return -1;
	memcpy(&cnt, e + 0xc, 2);
	memcpy(&vi, e + 0xe, 2);
	*type = e[0xa] & 0x3f;
	*count = cnt;
	uintptr_t vals = rq(rec + 0x10);
	int n = cnt < cap ? cnt : cap;
	if (!vals || !n)
		return 0;
	if (*type == 1) {
		out[0] = (r32(vals + (uintptr_t)(vi >> 5) * 8) >> (vi & 31)) & 1u;
		return 1;
	}
	return rd(vals + (uintptr_t)vi * 8, out, (size_t)n * 8) ? -1 : n;
}
static uintptr_t db_reci(uintptr_t rec, uintptr_t holder, int idx)
{
	uint64_t v[1];
	int t, n;
	return db_listi(rec, holder, idx, v, 1, &t, &n) == 1 && t == 0xa ? (uintptr_t)v[0] : 0;
}

/* every field of REC that schema HOLDER names, "Name=value;..." (lists: the first 8 values) */
static int db_dumpall(uintptr_t rec, uintptr_t holder, char *out, size_t cap)
{
	char nm[96], val[300];
	int o = 0, n = 0;
	out[0] = 0;
	for (int i = 0; rec && i < SCH_MAX; i++) {
		if (!ident_ok(sch_name(holder, i, nm, sizeof nm)))
			break;
		struct dbv v;
		if (db_get(rec, sch_hash(holder, i), &v))
			continue;
		db_fmt(&v, val, sizeof val);
		if ((size_t)o + strlen(nm) + strlen(val) + 3 < cap)
			o += cfmt(out + o, cap - (size_t)o, "%s%s=%s", n ? ";" : "", nm, val);
		n++;
	}
	return n;
}

static uintptr_t cat_rec(uintptr_t cat, uint32_t i) { return rq(cat + CAT_RECS) + (uintptr_t)i * DBREC_SIZE; }
static uint32_t cat_n(uintptr_t cat) { uint32_t n = cat ? r32(cat + CAT_COUNT) : 0; return n > 200000 ? 0 : n; }

/* the shared game services object *0x46b7310: a getter slot, refused unless it points into the game's code */
static uintptr_t gsvc_get(unsigned off, const char *word, unsigned seq)
{
	uintptr_t g = rq(G.gsvc_pp), vt = g ? rq(g) : 0, fn = vt ? rq(vt + off) : 0;
	if (!g || !code_ptr(fn)) {
		logf_("FAIL seq=%u %s: the game services object %#lx (vtable %#lx) slot +%#x = %#lx is not game code", seq, word,
		      (unsigned long)g, (unsigned long)vt, off, (unsigned long)fn);
		return 0;
	}
	return ((vget_fn)fn)(g);
}

/* ---- the AIBlockTable (re-combat-spawn.md 2.1) ---- */

#define BT_MAX 32
struct btent { uintptr_t cat; float chance; int has_chance; };
struct bt { uintptr_t rec, rec4c8, tbl; int n, total, type; struct btent e[BT_MAX]; };

/* the orc's character combat record as the roll reads it (FUN_026ff330(*(*(npc+0x10)+0x70))), its table, the entries */
static int bt_read(const struct orc *o, struct bt *b)
{
	memset(b, 0, sizeof *b);
	uintptr_t hold = rq(rq(o->owner + NPC_RECHOLD) + 0x70);
	b->rec4c8 = rq(o->c + C_CHARREC);
	b->rec = hold && g_fn_ok[FN_CHAR_COMBATREC] ? FN(FN_CHAR_COMBATREC, arg1_fn)(hold) : 0;
	if (!b->rec)
		b->rec = b->rec4c8;
	b->tbl = db_reci(b->rec, G.sch_ccr, CCR_AIBLOCKTABLE);
	uint64_t v[BT_MAX];
	int n = b->tbl ? db_listi(b->tbl, G.sch_btbl, BTBL_ENTRIES, v, BT_MAX, &b->type, &b->total) : -1;
	for (int i = 0; i < n && b->type == 0xa; i++) {
		struct dbv cv;
		b->e[i].cat = db_reci((uintptr_t)v[i], G.sch_bent, BENT_CATEGORY);
		b->e[i].chance = 0.5f;
		if (db_get((uintptr_t)v[i], sch_hash(G.sch_bent, BENT_CHANCE), &cv) == 0 && cv.type == 2) {
			uint32_t u = (uint32_t)cv.v[0];
			memcpy(&b->e[i].chance, &u, 4);
			b->e[i].has_chance = 1;
		}
		b->n++;
	}
	return b->tbl ? 0 : -1;
}

/* the chance an attack start with ATTACK rolls against this table (the first entry whose Category is null or one of
 * the attack's categories; a table whose Entries are not records: 0.5), -1 = no roll; *ent = the entry */
static float bt_chance(const struct bt *b, uintptr_t attack, int *ent)
{
	*ent = -1;
	if (!b->tbl || !b->total)
		return -1.0f;
	if (b->type != 0xa)
		return 0.5f;
	uint64_t cats[16];
	int t, cn, n = attack ? db_listi(attack, G.sch_attack, ATK_CATEGORY, cats, 16, &t, &cn) : 0;
	for (int i = 0; i < b->n; i++) {
		int hit = !b->e[i].cat;
		for (int k = 0; k < n && !hit && t == 0xa; k++)
			hit = (uintptr_t)cats[k] == b->e[i].cat;
		if (hit) {
			*ent = i;
			return b->e[i].chance;
		}
	}
	return -1.0f;
}

static const char *atk_cats(uintptr_t attack, char *out, size_t cap)
{
	uint64_t cats[16];
	char nm[96];
	int t, cn, n = attack ? db_listi(attack, G.sch_attack, ATK_CATEGORY, cats, 16, &t, &cn) : -1, o = 0;
	out[0] = 0;
	for (int k = 0; k < n && t == 0xa; k++)
		o += cfmt(out + o, cap - (size_t)o, "%s'%s'", k ? "," : "", rec_name((uintptr_t)cats[k], nm, sizeof nm));
	if (!out[0])
		snprintf(out, cap, n < 0 ? "(no Category field)" : "(none)");
	return out;
}

/* blocktable [N] [ATTACK|last]: the picked orc (and the N-1 nearest others), or the N nearest */
static void w_blocktable(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	int want = 1;
	const char *aname = NULL;
	for (int i = 1; i < argc; i++) {
		if (is_num(argv[i]))
			want = (int)num(argv[i], 1);
		else
			aname = argv[i];
	}
	want = want < 1 ? 1 : want > 8 ? 8 : want;
	uintptr_t atk = 0;
	char why[300] = "", an[128], cs[400], sn[4][96];
	if (aname)
		atk = !strcmp(aname, "last") ? atk_valid(g_last_player_attack) : atk_pick(aname, why, sizeof why);
	struct orc list[9], o[64];
	int nl = 0, n = orcs_collect(c, o, 64);
	if (pick_get(&list[0]))
		nl = 1;
	for (int i = 0; i < n && nl < want; i++)
		if (!o[i].dead && o[i].npc && (!nl || o[i].c != list[0].c))
			list[nl++] = o[i];
	sch_name(G.sch_ccr, CCR_AIBLOCKTABLE, sn[0], sizeof sn[0]);
	sch_name(G.sch_btbl, BTBL_ENTRIES, sn[1], sizeof sn[1]);
	sch_name(G.sch_bent, BENT_CATEGORY, sn[2], sizeof sn[2]);
	sch_name(G.sch_bent, BENT_CHANCE, sn[3], sizeof sn[3]);
	if (atk)
		logf_("BLOCKTBL seq=%u attack '%s' categories %s", seq, rec_name(atk, an, sizeof an), atk_cats(atk, cs, sizeof cs));
	for (int k = 0; k < nl; k++) {
		struct bt b;
		char rn[128], r2[128], tn[128], cn[128];
		bt_read(&list[k], &b);
		logf_("BLOCKTBL seq=%u orc %d c=%#lx dist=%.1f m record '%s' (Combat+0x4c8 '%s') %s '%s': %d of %d entries "
		      "(type %d)", seq, k, (unsigned long)list[k].c, list[k].dist / 100.0f, rec_name(b.rec, rn, sizeof rn),
		      rec_name(b.rec4c8, r2, sizeof r2), sn[0], rec_name(b.tbl, tn, sizeof tn), b.n, b.total, b.type);
		for (int i = 0; i < b.n; i++)
			logf_("BLOCKTBL seq=%u orc %d entry %d %s=%s %s=%.3f%s", seq, k, i, sn[2],
			      b.e[i].cat ? rec_name(b.e[i].cat, cn, sizeof cn) : "(none: every attack)", sn[3], (double)b.e[i].chance,
			      b.e[i].has_chance ? "" : " (default)");
		if (atk) {
			int ent;
			float ch = bt_chance(&b, atk, &ent);
			logf_("BLOCKTBL seq=%u orc %d: an attack start with '%s' at it %s", seq, k, an,
			      ch < 0 ? "rolls nothing (no entry matches)" : ent < 0 ? "rolls 0.5 (the Entries are not records)" : "rolls");
			if (ch >= 0)
				logf_("BLOCKTBL seq=%u orc %d: chance %.3f (entry %d)", seq, k, (double)ch, ent);
		}
	}
	logf_("DONE seq=%u blocktable: %d orcs (schema names: %s / %s / %s, %s)%s%s", seq, nl, sn[0], sn[1], sn[2], sn[3],
	      aname && !atk ? "; attack not found: " : "", aname && !atk ? why : "");
}

/* ---- aiblockwatch / blockroll: the roll path's hooks (P_AIB_*, P_GOAL_REL, P_SHIELD) ---- */

#define SP_N 8
static struct sp {
	uintptr_t npc, hobj, c, fm, tmpl;
	char tmpl_name[96], arch[64];
	unsigned seq;
	double t0;
	int checks, captain;
} g_sp[SP_N];
static int g_sp_n;

static struct {
	int watch, force;
	unsigned force_seq;
	_Atomic uint64_t gate[2], recs, rolls, forced, won, notifies, goal_changes, shields;
	uintptr_t rec_arg, rec_val; /* the last P_AIB_REC (game thread) */
	struct { uintptr_t goal; int8_t rel; } gr[64];
	unsigned gr_i;
} g_aib;
static pthread_mutex_t g_aib_mu = PTHREAD_MUTEX_INITIALIZER;

/* which of our orcs (spawned or picked) a record holder / NPC is, for the log */
static const char *aib_who(uintptr_t npc, uintptr_t hold, char *out, size_t cap)
{
	for (int i = 0; i < g_sp_n; i++)
		if ((npc && g_sp[i].npc == npc) || (hold && rq(rq(g_sp[i].npc + NPC_RECHOLD) + 0x70) == hold)) {
			snprintf(out, cap, "spawn %d '%s'", i, g_sp[i].arch);
			return out;
		}
	struct orc p;
	if (pick_get(&p) && ((npc && p.owner == npc) || (hold && rq(rq(p.owner + NPC_RECHOLD) + 0x70) == hold))) {
		snprintf(out, cap, "the picked orc '%s'", g_pick.name);
		return out;
	}
	snprintf(out, cap, "another NPC");
	return out;
}

/* FUN_0229aef0(node, attack): the attack is not a block and the node type is one of mask 0x883d (post) */
__attribute__((used)) void somvr_pc_aib_gate(struct regs *r)
{
	first_call(HK_AIB_GATE, atomic_fetch_add_explicit(&g_h[HK_AIB_GATE].calls, 1, memory_order_relaxed) + 1);
	r->rax = ((two_fn)g_h[HK_AIB_GATE].orig)(r->rdi, r->rsi);
	int ok = (r->rax & 0xff) != 0;
	atomic_fetch_add_explicit(&g_aib.gate[ok], 1, memory_order_relaxed);
	if (!g_aib.watch || !rl_ok(RL_AIB))
		return;
	char nn[96], an[128];
	logf_("AIBLOCK gate node='%s' attack='%s' -> %s t=%.6f", rec_name(r->rdi, nn, sizeof nn), rec_name(r->rsi, an, sizeof an),
	      ok ? "passed (not a block, node type in 0x883d)" : "refused", mono());
}

/* FUN_026ff330(*(*(npc+0x10)+0x70)): the target's character combat record; the roll path is reached (post) */
__attribute__((used)) void somvr_pc_aib_rec(struct regs *r)
{
	first_call(HK_AIB_REC, atomic_fetch_add_explicit(&g_h[HK_AIB_REC].calls, 1, memory_order_relaxed) + 1);
	r->rax = ((arg1_fn)g_h[HK_AIB_REC].orig)(r->rdi);
	atomic_fetch_add_explicit(&g_aib.recs, 1, memory_order_relaxed);
	g_aib.rec_arg = r->rdi;
	g_aib.rec_val = r->rax;
	if (!g_aib.watch || !rl_ok(RL_AIB))
		return;
	char rn[128], tn[128], w[160];
	uintptr_t tbl = db_reci(r->rax, G.sch_ccr, CCR_AIBLOCKTABLE);
	logf_("AIBLOCK reached target=%s record '%s' AIBlockTable '%s' t=%.6f", aib_who(0, r->rdi, w, sizeof w),
	      rec_name(r->rax, rn, sizeof rn), rec_name(tbl, tn, sizeof tn), mono());
}

/* FUN_00c73440(0, 1) = the roll, the chance in xmm2 (the caller keeps it at (%rsp) across the call); blockroll force
 * returns the low bound, so every roll that has an entry succeeds (post, float in/out) */
__attribute__((used)) void somvr_pc_aib_roll(struct regs *r)
{
	first_call(HK_AIB_ROLL, atomic_fetch_add_explicit(&g_h[HK_AIB_ROLL].calls, 1, memory_order_relaxed) + 1);
	float lo = r->xmm[0][0], hi = r->xmm[1][0], chance = r->xmm[2][0];
	float v = ((rollf_fn)g_h[HK_AIB_ROLL].orig)(lo, hi), was = v;
	int forced = g_aib.force;
	if (forced)
		v = lo;
	r->xmm[0][0] = v;
	atomic_fetch_add_explicit(&g_aib.rolls, 1, memory_order_relaxed);
	if (forced)
		atomic_fetch_add_explicit(&g_aib.forced, 1, memory_order_relaxed);
	if (v < chance)
		atomic_fetch_add_explicit(&g_aib.won, 1, memory_order_relaxed);
	if (!(g_aib.watch || forced) || !rl_ok(RL_AIB))
		return;
	char rn[128], w[160], fs[64] = "";
	if (forced)
		cfmt(fs, sizeof fs, " (blockroll force; the game's roll %.3f)", (double)was);
	logf_("AIBLOCK roll target=%s record '%s' chance=%.3f roll=%.3f%s -> %s t=%.6f", aib_who(0, g_aib.rec_arg, w, sizeof w),
	      rec_name(g_aib.rec_val, rn, sizeof rn), (double)chance, (double)v, fs, v < chance ? "notify" : "no notify", mono());
}

/* FUN_029ac890(attackerOwner, npc): the incoming-attack fact in the NPC's working memory (post) */
__attribute__((used)) void somvr_pc_aib_notify(struct regs *r)
{
	first_call(HK_AIB_NOTIFY, atomic_fetch_add_explicit(&g_h[HK_AIB_NOTIFY].calls, 1, memory_order_relaxed) + 1);
	r->rax = ((two_fn)g_h[HK_AIB_NOTIFY].orig)(r->rdi, r->rsi);
	atomic_fetch_add_explicit(&g_aib.notifies, 1, memory_order_relaxed);
	if (!(g_aib.watch || g_aib.force) || !rl_ok(RL_AIB))
		return;
	char w[160];
	logf_("AIBLOCK notify attacker=%#lx%s npc=%#lx %s replan=%u t=%.6f", (unsigned long)r->rdi,
	      r->rdi == g_pi.owner ? " (THE PLAYER)" : "", (unsigned long)r->rsi, aib_who(r->rsi, 0, w, sizeof w),
	      rb(rq(r->rsi + NPC_AI) + BRAIN_REPLAN), mono());
}

/* CAIGoalCounterMelee::vf_03: the relevance (goal+0x10) per goal, logged on change (plain C, the original first) */
static uint64_t p_goal_rel(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f)
{
	uint64_t r = ((six_fn)g_h[HK_GOAL_REL].orig)(a, b, c, d, e, f);
	first_call(HK_GOAL_REL, atomic_fetch_add_explicit(&g_h[HK_GOAL_REL].calls, 1, memory_order_relaxed) + 1);
	if (!g_aib.watch)
		return r;
	int rel = rb((uintptr_t)a + 0x10), was = -1, s = -1;
	pthread_mutex_lock(&g_aib_mu);
	for (int i = 0; i < 64 && s < 0; i++)
		if (g_aib.gr[i].goal == (uintptr_t)a)
			s = i;
	if (s < 0) {
		s = (int)(g_aib.gr_i++ % 64);
		g_aib.gr[s].goal = (uintptr_t)a;
		g_aib.gr[s].rel = -1;
	}
	was = g_aib.gr[s].rel;
	g_aib.gr[s].rel = (int8_t)rel;
	pthread_mutex_unlock(&g_aib_mu);
	if (was == rel)
		return r;
	atomic_fetch_add_explicit(&g_aib.goal_changes, 1, memory_order_relaxed);
	if (!rl_ok(RL_AIB))
		return r;
	uintptr_t npc = rq((uintptr_t)a + 8), cm = ai_combat(npc);
	char an[128], w[160];
	logf_("AIBLOCK goal_counter relevant %d -> %d npc=%#lx %s orc_attack='%s' cancel=%u/%u t=%.6f", was, rel,
	      (unsigned long)npc, aib_who(npc, 0, w, sizeof w), cm ? rec_name(rq(cm + C_ATTACK), an, sizeof an) : "",
	      cm ? rb(cm + 0xe0) : 0, cm ? rb(cm + 0xe1) : 0, mono());
	return r;
}

/* FUN_028adf30(shield item, attack, ..) in the damage context: shield HP > 0, the category, the direction (pre, log
 * only: its result is the HITEV line's block byte) */
__attribute__((used)) void somvr_pc_shield(struct regs *r)
{
	first_call(HK_SHIELD, atomic_fetch_add_explicit(&g_h[HK_SHIELD].calls, 1, memory_order_relaxed) + 1);
	atomic_fetch_add_explicit(&g_aib.shields, 1, memory_order_relaxed);
	if (!g_aib.watch || !rl_ok(RL_AIB))
		return;
	char an[128], in[128];
	logf_("AIBLOCK shield item=%#lx '%s' hp=%.1f attack='%s' arg3=%#lx xmm0=%g t=%.6f", (unsigned long)r->rdi,
	      rec_name(rq(r->rdi + IT_REC), in, sizeof in), (double)rf(r->rdi + 0xa8), rec_name(r->rsi, an, sizeof an),
	      (unsigned long)r->rdx, (double)r->xmm[0][0], mono());
}

static void aib_counts(unsigned seq, const char *what)
{
	logf_("AIBLOCK seq=%u %s: gate passed %llu refused %llu, roll path reached %llu, rolls %llu (forced %llu, won %llu), "
	      "notifies %llu, goal relevance changes %llu, shield tests %llu (hooks %d%d%d%d%d%d)", seq, what,
	      (unsigned long long)atomic_load(&g_aib.gate[1]), (unsigned long long)atomic_load(&g_aib.gate[0]),
	      (unsigned long long)atomic_load(&g_aib.recs), (unsigned long long)atomic_load(&g_aib.rolls),
	      (unsigned long long)atomic_load(&g_aib.forced), (unsigned long long)atomic_load(&g_aib.won),
	      (unsigned long long)atomic_load(&g_aib.notifies), (unsigned long long)atomic_load(&g_aib.goal_changes),
	      (unsigned long long)atomic_load(&g_aib.shields), g_h[HK_AIB_GATE].installed, g_h[HK_AIB_REC].installed,
	      g_h[HK_AIB_ROLL].installed, g_h[HK_AIB_NOTIFY].installed, g_h[HK_GOAL_REL].installed, g_h[HK_SHIELD].installed);
}

static void w_aiblockwatch(unsigned seq, const char *arg)
{
	g_aib.watch = !(arg && !strcmp(arg, "off"));
	aib_counts(seq, g_aib.watch ? "watch on (counts so far)" : "watch off");
	logf_("DONE seq=%u aiblockwatch %s", seq, g_aib.watch ? "on" : "off");
}

static void w_blockroll(unsigned seq, const char *arg)
{
	int on = arg && !strcmp(arg, "force");
	if (on && !g_h[HK_AIB_ROLL].installed) {
		logf_("FAIL seq=%u blockroll force: P_AIB_ROLL is not installed", seq);
		return;
	}
	g_aib.force = on;
	g_aib.force_seq = seq;
	aib_counts(seq, on ? "blockroll force" : "blockroll off");
	logf_("DONE seq=%u blockroll %s", seq, on ? "force: every AIBlockTable roll succeeds" : "off: the game's roll");
}

/* incoming [roll] [ATTACK|last]: FUN_029ac890(the player's CCharacter, npc) for the picked orc (re-combat-spawn.md
 * 2.6, step 1b): the game's notification without its attack start; `roll` = only when our own roll from the orc's
 * table with ATTACK's categories (default: the last player attack, else swingevent's auto record) succeeds */
static void w_incoming(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	int roll = 0;
	const char *aname = NULL, *twhy = "";
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "roll"))
			roll = 1;
		else
			aname = argv[i];
	}
	uintptr_t own = g_pi.owner, npc;
	struct orc t;
	if (!g_fn_ok[FN_AI_FACT] || !own || rq(own) != G.vt_playerobj || rq(own + GB_HOBJ) != g_pi.phobj) {
		logf_("FAIL seq=%u incoming: fn=%d, the player's CCharacter %#lx (vtable %#lx) +8 %#lx (want %#lx)", seq,
		      g_fn_ok[FN_AI_FACT], (unsigned long)own, (unsigned long)rq(own), (unsigned long)rq(own + GB_HOBJ),
		      (unsigned long)g_pi.phobj);
		return;
	}
	if (!target_orc(c, &t, &twhy)) {
		logf_("FAIL seq=%u incoming: no target orc (%s; `pick` or `spawn` one)", seq, twhy);
		return;
	}
	npc = t.owner;
	uintptr_t brain = rq(npc + NPC_AI);
	if (rq(npc) != G.vt_npc || !rq(npc + NPC_WM) || !brain || rq(npc + GB_HOBJ) != t.hobj) {
		logf_("FAIL seq=%u incoming: npc %#lx (vtable %#lx) working memory %#lx brain %#lx +8 %#lx (want %#lx): not the layout "
		      "FUN_029ac890 reads", seq, (unsigned long)npc, (unsigned long)rq(npc), (unsigned long)rq(npc + NPC_WM),
		      (unsigned long)brain, (unsigned long)rq(npc + GB_HOBJ), (unsigned long)t.hobj);
		return;
	}
	char tn[128], an[128], why[300] = "";
	rec_name(rq(t.c + C_CHARREC), tn, sizeof tn);
	if (roll) {
		uintptr_t atk;
		if (aname && !strcmp(aname, "last"))
			atk = atk_valid(g_last_player_attack);
		else if (aname)
			atk = atk_pick(aname, why, sizeof why);
		else if (atk_valid(g_last_player_attack))
			atk = g_last_player_attack;
		else
			atk = swing_rec("auto", why, sizeof why);
		struct bt b;
		int ent = -1;
		bt_read(&t, &b);
		float ch = atk ? bt_chance(&b, atk, &ent) : -1.0f, v = (float)(rand() % 10000) / 10000.0f;
		logf_("INCOMING seq=%u roll: '%s' at '%s': chance %.3f (entry %d of %d) roll %.3f -> %s", seq,
		      atk ? rec_name(atk, an, sizeof an) : "(no attack record)", tn, (double)ch, ent, b.n, (double)v,
		      ch >= 0 && v < ch ? "notify" : "no notify");
		if (!(ch >= 0 && v < ch)) {
			logf_("DONE seq=%u incoming roll: lost, '%s' not notified", seq, tn);
			return;
		}
	}
	uint8_t before = rb(brain + BRAIN_REPLAN);
	FN(FN_AI_FACT, aifact_fn)(own, npc);
	logf_("INCOMING seq=%u FUN_029ac890(player %#lx, npc %#lx) '%s' (%s) dist=%.1f m: replan %u -> %u; its attack '%s' "
	      "cancel=%u/%u t=%.6f", seq, (unsigned long)own, (unsigned long)npc, tn, twhy, t.dist / 100.0f, before,
	      rb(brain + BRAIN_REPLAN), rec_name(rq(t.c + C_ATTACK), an, sizeof an), rb(t.c + 0xe0), rb(t.c + 0xe1), mono());
	logf_("DONE seq=%u incoming: '%s' told it is being attacked (watch AI goal_counter / AIBLOCK goal_counter)", seq, tn);
}

/* ---- templates, spawn, spawncaptain, despawn (re-combat-spawn.md 1.2-1.4) ---- */

#define TP_N 4096
static struct { int n, done, partial; uintptr_t cat; uintptr_t rec[TP_N], arch[TP_N]; uint8_t lvl[TP_N]; } g_tp;
static const char *const g_arch_known[] = { "AI_Defender", "AI_Berserker", "AI_Infantry", "AI_Spearman", "AI_Archer",
					     "AI_Slave_Human" };
static const char *const g_tp_bad[] = { "Captain", "Warchief", "Chief", "Boss", "Bodyguard", "Unique", "Named", "Test",
					 "Debug", "Cine", "Story", "Tutorial", "Quest", "Elite", "Legend", "Nemesis", "Slave",
					 "Human", "Mount", "Rider", "Dead", "Corpse" };

static int arch_name_ok(const char *nm)
{
	for (size_t i = 0; i < sizeof g_arch_known / sizeof g_arch_known[0]; i++)
		if (!strcmp(nm, g_arch_known[i]))
			return 1;
	return 0;
}

/* the records a record's attributes reference (raw: every 16-byte entry's own type/count/slot), at most cap */
static int rec_refs(uintptr_t rec, uintptr_t *out, int cap)
{
	unsigned char hdr[2], ents[256 * 16];
	uintptr_t arr = rec ? rq(rec + 8) : 0, vals = rec ? rq(rec + 0x10) : 0;
	int k = 0;
	if (!arr || !vals || rd(rec, hdr, 2) || !hdr[0] || rd(arr, ents, (size_t)hdr[0] * 16))
		return 0;
	for (int i = 0; i < hdr[0] && k < cap; i++) {
		const unsigned char *e = ents + (size_t)i * 16;
		uint16_t cnt, vi;
		memcpy(&cnt, e + 0xc, 2);
		memcpy(&vi, e + 0xe, 2);
		if ((e[0xa] & 0x3f) != 0xa || !cnt)
			continue;
		uint64_t v[4];
		int n = cnt < 4 ? cnt : 4;
		if (rd(vals + (uintptr_t)vi * 8, v, (size_t)n * 8))
			continue;
		for (int j = 0; j < n && k < cap; j++)
			if (v[j])
				out[k++] = (uintptr_t)v[j];
	}
	return k;
}

/* the archetype record (AI_Defender ...) a template references, one or two levels down; *lvl = the level */
static uintptr_t tp_arch(uintptr_t tmpl, uint8_t *lvl)
{
	uintptr_t l1[24], l2[24];
	char nm[96];
	int n1 = rec_refs(tmpl, l1, 24);
	for (int i = 0; i < n1; i++)
		if (arch_name_ok(rec_name(l1[i], nm, sizeof nm))) {
			*lvl = 1;
			return l1[i];
		}
	for (int i = 0; i < n1 && i < 12; i++) {
		int n2 = rec_refs(l1[i], l2, 24);
		for (int j = 0; j < n2; j++)
			if (arch_name_ok(rec_name(l2[j], nm, sizeof nm))) {
				*lvl = 2;
				return l2[j];
			}
	}
	*lvl = 0;
	return 0;
}

static void tp_scan(void)
{
	uintptr_t cat = rq(G.tmplcat_pp);
	uint32_t n = cat_n(cat);
	double t0 = mono();
	g_tp.cat = cat;
	g_tp.n = 0;
	g_tp.partial = 0;
	for (uint32_t i = 0; i < n && g_tp.n < TP_N; i++) {
		if (mono() - t0 > 2.0) { /* one game-thread stall at most */
			g_tp.partial = 1;
			break;
		}
		uintptr_t rec = cat_rec(cat, i);
		g_tp.rec[g_tp.n] = rec;
		g_tp.arch[g_tp.n] = tp_arch(rec, &g_tp.lvl[g_tp.n]);
		g_tp.n++;
	}
	g_tp.done = 1;
}

static int tp_score(const char *nm)
{
	int s = (int)strlen(nm);
	for (size_t k = 0; k < sizeof g_tp_bad / sizeof g_tp_bad[0]; k++)
		if (strcasestr(nm, g_tp_bad[k]))
			s += 100;
	return s;
}

/* templates [SUB]: every AI/Templates name and the archetype it references -> OUT/templates.tsv */
static void w_templates(unsigned seq, int argc, char **argv)
{
	uintptr_t cat = rq(G.tmplcat_pp);
	uint32_t n = cat_n(cat);
	if (!cat || !n) {
		logf_("FAIL seq=%u templates: no AI/Templates category (cat=%#lx n=%u)", seq, (unsigned long)cat, n);
		return;
	}
	double t0 = mono();
	tp_scan();
	char path[1200], nm[128], an[96];
	FILE *f = out_file("templates", 0, path, sizeof path);
	if (f)
		fprintf(f, "index\trec\tname\tarchetype\tlevel\n");
	int per[8] = { 0 }, shown = 0;
	for (int i = 0; i < g_tp.n; i++) {
		rec_name(g_tp.rec[i], nm, sizeof nm);
		rec_name(g_tp.arch[i], an, sizeof an);
		if (f)
			fprintf(f, "%d\t%#lx\t%s\t%s\t%u\n", i, (unsigned long)g_tp.rec[i], nm, an, g_tp.lvl[i]);
		for (size_t k = 0; k < sizeof g_arch_known / sizeof g_arch_known[0]; k++)
			per[k] += !strcmp(an, g_arch_known[k]);
		int match = argc > 1 ? strcasestr(nm, argv[1]) != NULL : shown < 30;
		if (match && shown < 60) {
			logf_("TEMPLATE %d '%s' archetype '%s' (level %u)", i, nm, an, g_tp.lvl[i]);
			shown++;
		}
	}
	if (f)
		fclose(f);
	for (size_t k = 0; k < sizeof g_arch_known / sizeof g_arch_known[0]; k++)
		logf_("TEMPLATE archetype %s: %d templates", g_arch_known[k], per[k]);
	logf_("DONE seq=%u templates: %u AI/Templates records, %d scanned%s in %.3f s, file=%s", seq, n, g_tp.n,
	      g_tp.partial ? " (stopped at 2 s)" : "", mono() - t0, f ? path : "(not written)");
}

/* the template for spawn: an exact template name, else (AI_...) the best template referencing that archetype, else
 * the best name containing WHAT (or its stem after AI_) */
static uintptr_t tp_pick(const char *what, char *why, size_t cap, char *arch_out, size_t acap)
{
	uintptr_t r = g_fn_ok[FN_TMPL_BYNAME] ? FN(FN_TMPL_BYNAME, byname_fn)(what) : 0;
	char nm[128], an[96];
	arch_out[0] = 0;
	if (r) {
		snprintf(why, cap, "by name");
		return r;
	}
	uintptr_t cat = rq(G.tmplcat_pp);
	if (!g_tp.done || g_tp.cat != cat)
		tp_scan();
	int best = -1, bs = 1 << 30, nc = 0;
	for (int i = 0; i < g_tp.n; i++) {
		if (!g_tp.arch[i] || strcmp(rec_name(g_tp.arch[i], an, sizeof an), what))
			continue;
		int s = tp_score(rec_name(g_tp.rec[i], nm, sizeof nm));
		nc++;
		if (nc <= 8)
			logf_("SPAWN candidate '%s' (archetype '%s', level %u, score %d)", nm, an, g_tp.lvl[i], s);
		if (s < bs)
			bs = s, best = i;
	}
	if (best >= 0) {
		snprintf(why, cap, "the best of %d templates referencing '%s' (score %d)", nc, what, bs);
		snprintf(arch_out, acap, "%s", what);
		return g_tp.rec[best];
	}
	const char *stem = !strncmp(what, "AI_", 3) ? what + 3 : what;
	for (int i = 0; i < g_tp.n; i++) {
		if (!strcasestr(rec_name(g_tp.rec[i], nm, sizeof nm), stem))
			continue;
		int s = tp_score(nm);
		nc++;
		if (nc <= 8)
			logf_("SPAWN candidate '%s' (name contains '%s', score %d)", nm, stem, s);
		if (s < bs)
			bs = s, best = i;
	}
	if (best < 0) {
		snprintf(why, cap, "no template named '%s', none referencing it, none containing '%s' (%d scanned)", what, stem, g_tp.n);
		return 0;
	}
	snprintf(why, cap, "the best of %d names containing '%s' (score %d)", nc, stem, bs);
	rec_name(g_tp.arch[best], arch_out, acap);
	return g_tp.rec[best];
}

static int sp_alive(const struct sp *s)
{
	return s->hobj && !(rb(s->hobj + HOBJ_FLAGS) & 0x18) && rq(s->hobj + HOBJ_GAMEOBJ) == s->npc && rq(s->npc) == G.vt_npc;
}

/* P10: npcpush spawns along the probe's heading instead of Talion's forward (a horizontal unit vector) */
static int g_spawn_dir_on;
static float g_spawn_dir[3];

/* the debug spawn of FUN_027b4560 (re-combat-spawn.md 1.2 steps 1-4) from our code, CM ahead of Talion on the ground */
static void sp_spawn(const struct ctx *c, unsigned seq, const char *word, uintptr_t tmpl, const char *tname, const char *arch,
		     uintptr_t fm, float cm, int level)
{
	(void)c;
	uintptr_t phys = rq(G.phys_pp), vt = phys ? rq(phys) : 0;
	if (!g_fn_ok[FN_SPAWN_AI] || !g_fn_ok[FN_ZONE_AT] || !g_fn_ok[FN_SETLEVEL] || !g_ro || !g_ro_ok[RO_NPC_SETZONE] ||
	    !g_ro_ok[RO_PHYS_RAY] || vt != G.vt_phys || vt + 0x200 != g_ro[RO_PHYS_RAY].addr ||
	    rq(vt + 0x200) != g_ro[RO_PHYS_RAY].target || G.vt_npc + 0x288 != g_ro[RO_NPC_SETZONE].addr ||
	    rq(G.vt_npc + 0x288) != g_ro[RO_NPC_SETZONE].target || (fm && (!g_fn_ok[FN_FM_BIND] || !g_fn_ok[FN_FM_ACTIVATE]))) {
		logf_("FAIL seq=%u %s: a function or slot did not check out (spawn %d zone %d level %d bind %d/%d; NPC slot %d; "
		      "ray %d, physics vtable %#lx)", seq, word, g_fn_ok[FN_SPAWN_AI], g_fn_ok[FN_ZONE_AT], g_fn_ok[FN_SETLEVEL],
		      g_fn_ok[FN_FM_BIND], g_fn_ok[FN_FM_ACTIVATE], g_ro && g_ro_ok[RO_NPC_SETZONE], g_ro && g_ro_ok[RO_PHYS_RAY],
		      (unsigned long)vt);
		return;
	}
	if (g_sp_n >= SP_N) {
		logf_("FAIL seq=%u %s: %d spawns already (`despawn all` first)", seq, word, g_sp_n);
		return;
	}
	if (!g_pi.charobj || !g_pi.phobj) {
		logf_("FAIL seq=%u %s: no player object", seq, word);
		return;
	}
	/* the spot CM ahead (Talion's forward in the xz plane; npcpush: the probe's heading), a ray down onto the ground as
	 * the debug spawn's G21 ray */
	float fz[3] = { 0, 0, 1 }, f[3], p[3];
	qrot(g_pi.rot, fz, f);
	if (g_spawn_dir_on)
		memcpy(f, g_spawn_dir, sizeof f);
	float fl = sqrtf(f[0] * f[0] + f[2] * f[2]);
	if (fl < 1e-3f)
		f[0] = 0, f[2] = 1, fl = 1;
	p[0] = g_pi.pos[0] + f[0] / fl * cm;
	p[1] = g_pi.pos[1];
	p[2] = g_pi.pos[2] + f[2] / fl * cm;
	unsigned char q[0x40] __attribute__((aligned(16))), rr[0x80] __attribute__((aligned(16)));
	float from[3] = { p[0], p[1] + 200.0f, p[2] }, to[3] = { p[0], p[1] - 600.0f, p[2] }, hit[3];
	uint32_t fls = 0x50, flt = 0xc;
	uint16_t nos = 0xffff;
	memset(q, 0, sizeof q);
	memset(rr, 0, sizeof rr);
	memcpy(q, from, 12);
	memcpy(q + 12, to, 12);
	memcpy(q + 0x18, &fls, 4);
	memcpy(q + 0x1c, &flt, 4);
	memcpy(rr + 0x28, &nos, 2);
	uint64_t rc = ((raycast_fn)g_ro[RO_PHYS_RAY].target)(phys, q, rr);
	uintptr_t hobj;
	memcpy(hit, rr, 12);
	memcpy(&hobj, rr + 0x20, 8);
	if (!(rc & 0xff)) {
		logf_("FAIL seq=%u %s: the ray down at (%.1f %.1f %.1f) hit nothing (no ground within 6 m below)", seq, word, p[0], p[1],
		      p[2]);
		return;
	}
	float ang = atan2f(g_pi.pos[0] - hit[0], g_pi.pos[2] - hit[2]);
	float xf[7] = { hit[0], hit[1] + 10.0f, hit[2], 0.0f, sinf(ang * 0.5f), 0.0f, cosf(ang * 0.5f) };
	uintptr_t ctxo = hobj ? hobj : g_pi.phobj;
	char tn[128];
	logf_("SPAWN seq=%u %s template '%s' archetype '%s' fm=%#lx at (%.1f %.1f %.1f) %.0f cm ahead, ground hit object %#lx%s, "
	      "facing Talion (yaw %.1f deg) t=%.6f", seq, word, rec_name(tmpl, tn, sizeof tn), arch, (unsigned long)fm, xf[0], xf[1],
	      xf[2], (double)cm, (unsigned long)hobj, hobj ? "" : " (null: Talion's HOBJECT is the context)",
	      (double)(ang * 57.29578f), mono());
	flush_log();
	uintptr_t ret = FN(FN_SPAWN_AI, spawn_fn)(ctxo, "DebugSpawnedAI", xf, tmpl, 1, 1, 0, fm, 0);
	uintptr_t h = ret ? rq(ret + 8) : 0, cls = h ? rq(rq(h + HOBJ_CLASSES)) : 0, npc = 0;
	for (int guard = 0; cls && guard < 32 && !npc; guard++, cls = rq(cls + 8))
		if (cls == G.class_npc)
			npc = rq(h + HOBJ_GAMEOBJ);
	if (!ret || !npc || rq(npc) != G.vt_npc) {
		logf_("FAIL seq=%u %s: FUN_029a4830 returned %#lx (HOBJECT %#lx, NPC %#lx vtable %#lx): no NPC (placement refused, or "
		      "the template is a prop)", seq, word, (unsigned long)ret, (unsigned long)h, (unsigned long)npc,
		      (unsigned long)rq(npc));
		return;
	}
	float hp[3];
	rvec(h + OBJ_POS, hp, 3);
	uintptr_t zone = FN(FN_ZONE_AT, zoneat_fn)(hp, 0, 0);
	((setzone_fn)g_ro[RO_NPC_SETZONE].target)(npc, zone, 1);
	uint32_t lvl = (uint32_t)level;
	if (fm) {
		FN(FN_FM_BIND, fmbind_fn)(npc, fm, 1);
		if (rq(fm + FM_ACTIVE))
			FN(FN_FM_ACTIVATE, fmact_fn)(fm, 1);
		lvl = r32(fm + FM_LEVEL);
	} else if (level < 0) {
		lvl = r32(G.spawn_level);
	}
	uintptr_t cm8 = rq(npc + CH_COMBAT);
	if (cm8 && rq(cm8) == G.vt_combat)
		FN(FN_SETLEVEL, setlevel_fn)(cm8, lvl);
	struct sp *s = &g_sp[g_sp_n++];
	memset(s, 0, sizeof *s);
	s->npc = npc, s->hobj = h, s->c = cm8, s->fm = fm, s->tmpl = tmpl, s->seq = seq, s->t0 = mono(), s->captain = fm != 0;
	snprintf(s->tmpl_name, sizeof s->tmpl_name, "%s", tname);
	snprintf(s->arch, sizeof s->arch, "%s", arch[0] ? arch : tname);
	g_pick.c = cm8; /* the new orc is the target of pick's users (aggro, dealhit, incoming, blocktable, hitstop) */
	g_pick.hobj = h;
	snprintf(g_pick.name, sizeof g_pick.name, "%s", s->arch);
	char rn[96];
	logf_("SPAWN seq=%u %d: npc=%#lx hobj=%#lx Combat=%#lx record '%s' at (%.1f %.1f %.1f) zone=%#lx level %u%s; picked",
	      seq, g_sp_n - 1, (unsigned long)npc, (unsigned long)h, (unsigned long)cm8, rec_name(rq(cm8 + C_CHARREC), rn, sizeof rn),
	      hp[0], hp[1], hp[2], (unsigned long)zone, lvl, fm ? " (bound to the faction member)" : "");
	logf_("DONE seq=%u %s: '%s' spawned %.1f m ahead (SPAWNCHECK in 1 and 3 s; `despawn` removes it)", seq, word, s->arch,
	      (double)(dist3(hp, g_pi.pos) / 100.0f));
}

/* spawn WHAT[,WHAT..] [CM] [LEVEL]: the first WHAT that yields a template */
static void w_spawn(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	if (argc < 2) {
		logf_("FAIL seq=%u spawn: want a template or archetype name (AI_Defender ...)", seq);
		return;
	}
	float cm = (float)num(argc > 2 ? argv[2] : NULL, 500);
	cm = cm < 150 ? 150 : cm > 2000 ? 2000 : cm;
	int level = argc > 3 ? (int)num(argv[3], -1) : -1;
	char why[300] = "", arch[96], tn[128], alts[128], *save = NULL;
	const char *used = argv[1];
	uintptr_t t = 0;
	snprintf(alts, sizeof alts, "%s", argv[1]);
	for (char *w = strtok_r(alts, ",", &save); w && !t; w = strtok_r(NULL, ",", &save)) {
		used = w;
		t = tp_pick(w, why, sizeof why, arch, sizeof arch);
		if (!t)
			logf_("SPAWN seq=%u '%s': %s", seq, w, why);
	}
	if (!t) {
		logf_("FAIL seq=%u spawn %s: %s", seq, argv[1], why);
		return;
	}
	logf_("SPAWN seq=%u template '%s' for '%s': %s", seq, rec_name(t, tn, sizeof tn), used, why);
	sp_spawn(c, seq, "spawn", t, tn, arch[0] ? arch : used, 0, cm, level);
}

/* spawncaptain [CM]: the current faction target (`exec Faction.SetTarget ROW COL` first) with its own template */
static void w_spawncaptain(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	float cm = (float)num(argc > 1 ? argv[1] : NULL, 500);
	cm = cm < 150 ? 150 : cm > 2000 ? 2000 : cm;
	if (!g_fn_ok[FN_FACTION_TGT] || !g_fn_ok[FN_FM_TEMPLATE]) {
		logf_("FAIL seq=%u spawncaptain: FUN_023d3f80/FUN_022d71b0 did not check out", seq);
		return;
	}
	uintptr_t fmgr = gsvc_get(0x78, "spawncaptain", seq);
	if (!fmgr)
		return;
	uintptr_t fm = FN(FN_FACTION_TGT, arg1_fn)(fmgr);
	if (!fm) {
		logf_("FAIL seq=%u spawncaptain: no current faction target (faction manager %#lx; `exec Faction.SetTarget ROW COL` "
		      "first)", seq, (unsigned long)fmgr);
		return;
	}
	uint32_t fl = r32(fm + FM_FLAGS);
	uintptr_t t = FN(FN_FM_TEMPLATE, arg1_fn)(fm);
	char tn[128];
	logf_("SPAWN seq=%u captain: faction manager %#lx target member %#lx flags %#x (in the world %u) level %u active %#lx "
	      "template %#lx '%s'", seq, (unsigned long)fmgr, (unsigned long)fm, fl, (fl >> 1) & 1, r32(fm + FM_LEVEL),
	      (unsigned long)rq(fm + FM_ACTIVE), (unsigned long)t, rec_name(t, tn, sizeof tn));
	if ((fl >> 1) & 1) {
		logf_("FAIL seq=%u spawncaptain: the member is already in the world (the debug spawn refuses it too)", seq);
		return;
	}
	if (!t) {
		logf_("FAIL seq=%u spawncaptain: the member has no template", seq);
		return;
	}
	sp_spawn(c, seq, "spawncaptain", t, tn, "captain", fm, cm, -1);
}

/* despawn [all]: the last (or every) orc spawn/spawncaptain made, through the object system's deferred remove */
static void w_despawn(unsigned seq, int argc, char **argv)
{
	int all = argc > 1 && !strcmp(argv[1], "all"), done = 0, gone = 0;
	uintptr_t os = rq(G.objsys_pp), vt = os ? rq(os) : 0;
	if (!g_ro || !g_ro_ok[RO_OBJSYS_REMOVE] || vt != G.vt_objsys || vt + 0x58 != g_ro[RO_OBJSYS_REMOVE].addr ||
	    rq(vt + 0x58) != g_ro[RO_OBJSYS_REMOVE].target) {
		logf_("FAIL seq=%u despawn: the object system %#lx (vtable %#lx) slot +0x58 did not check out", seq, (unsigned long)os,
		      (unsigned long)vt);
		return;
	}
	while (g_sp_n > 0) {
		struct sp *s = &g_sp[g_sp_n - 1];
		char rn[96];
		if (sp_alive(s)) {
			uint64_t rc = ((objremove_fn)g_ro[RO_OBJSYS_REMOVE].target)(os, s->hobj);
			logf_("DESPAWN seq=%u %d '%s' hobj=%#lx (record '%s', dead=%u): removal queued rc=%u flags now %#x", seq, g_sp_n - 1,
			      s->arch, (unsigned long)s->hobj, rec_name(rq(s->c + C_CHARREC), rn, sizeof rn),
			      rq(s->c) == G.vt_combat ? rb(s->c + C_DEAD) : 255u, (unsigned)(rc & 0xff), rb(s->hobj + HOBJ_FLAGS));
			done++;
		} else {
			logf_("DESPAWN seq=%u %d '%s' hobj=%#lx: already gone", seq, g_sp_n - 1, s->arch, (unsigned long)s->hobj);
			gone++;
		}
		if (g_pick.hobj == s->hobj)
			memset(&g_pick, 0, sizeof g_pick);
		g_sp_n--;
		if (!all)
			break;
	}
	logf_("DONE seq=%u despawn%s: %d removed, %d already gone, %d left", seq, all ? " all" : "", done, gone, g_sp_n);
}

/* 1 and 3 s after a spawn: is it a live orc, where, what it does */
static void sp_tick(void)
{
	double t = mono();
	for (int i = 0; i < g_sp_n; i++) {
		struct sp *s = &g_sp[i];
		if (s->checks >= 2 || t - s->t0 < (s->checks ? 3.0 : 1.0))
			continue;
		s->checks++;
		struct orc o;
		char rn[96], an[96];
		int ok = orc_get(s->c, g_pi.pos, &o) && o.hobj == s->hobj;
		uintptr_t tg = ok ? rq(o.c + C_TARGET) : 0;
		logf_("SPAWNCHECK %d '%s' after %.1f s: %s alive=%d dead=%d ai=%d dist=%.1f m pos=(%.1f %.1f %.1f) record '%s' attack '%s' "
		      "combat target %s", i, s->arch, t - s->t0, ok ? "a valid orc" : "NOT a valid Combat/NPC pair", sp_alive(s), ok ? o.dead : -1,
		      ok ? o.ai : -1, ok ? o.dist / 100.0f : -1.0f, o.pos[0], o.pos[1], o.pos[2],
		      ok ? rec_name(rq(o.c + C_CHARREC), rn, sizeof rn) : "", ok ? rec_name(rq(o.c + C_ATTACK), an, sizeof an) : "",
		      tg == g_pi.phobj && tg ? "TALION" : tg ? "other" : "none");
	}
}

/* ---- tod, weathers (re-combat-spawn.md 3) ---- */

static void w_tod(unsigned seq)
{
	uintptr_t w = gsvc_get(0xa8, "tod", seq);
	if (!w)
		return;
	uint8_t tod = rb(w + W_TOD);
	char rn[128], tn[64] = "";
	if (g_fn_ok[FN_TOD_NAME])
		rstr(FN(FN_TOD_NAME, todname_fn)(tod), tn, sizeof tn);
	logf_("TOD seq=%u weather state %#lx: Record(%s) GDBIndex(%d) TOD(%s) = byte %u t=%.6f", seq, (unsigned long)w,
	      rec_name(rq(w + W_RECORD), rn, sizeof rn), (int)r32(w + W_INDEX) + 1, tn, tod, mono());
	logf_("DONE seq=%u tod: '%s' %s", seq, rn, tn);
}

static void w_weathers(unsigned seq)
{
	uintptr_t cat = rq(G.wxcat_pp);
	uint32_t n = cat_n(cat);
	char path[1200], nm[128];
	FILE *f = n ? out_file("weathers", 0, path, sizeof path) : NULL;
	for (uint32_t i = 0; i < n; i++) {
		rec_name(cat_rec(cat, i), nm, sizeof nm);
		if (f)
			fprintf(f, "%u\t%s\n", i, nm);
		if (i < 60)
			logf_("WEATHER %u '%s'", i, nm);
	}
	if (f)
		fclose(f);
	logf_("DONE seq=%u weathers: %u Weather/Definition records (cat %#lx)%s%s", seq, n, (unsigned long)cat, f ? ", file=" : "",
	      f ? path : "");
}

/* ---- the bow: wmdump, wmhold, aimray (re-bow.md BW1, BW3) ---- */

static const struct { const char *field; uintptr_t *holder; } g_wm_comp[] = {
	{ "Predicate", &G.sch_wmpred }, { "Data", &G.sch_wmdata }, { "SlowMo", &G.sch_wmslowmo }, { "FreeAim", &G.sch_wmfreeaim },
	{ "VisionMode", &G.sch_wmvision }, { "UIMode", &G.sch_wmui },
};

/* the command a predicate record enters with (FUN_00e24570 = the CommandID getter), -1 = none */
static int wm_cmd(uintptr_t pred)
{
	if (!pred || !g_fn_ok[FN_WM_CMDID])
		return -1;
	uint32_t id = FN(FN_WM_CMDID, cmdid_fn)(pred);
	return id < NCMD ? (int)id : -1;
}

/* a mode's first predicate with a command; *pred = it */
static int wm_mode_cmd(uintptr_t mode, uintptr_t *pred)
{
	struct dbv v;
	*pred = 0;
	if (db_field(mode, G.sch_wm, "Predicate", &v) || v.type != 0xa)
		return -1;
	for (int k = 0; k < v.count && k < 8; k++) {
		int id = wm_cmd((uintptr_t)v.v[k]);
		if (id > 0) {
			*pred = (uintptr_t)v.v[k];
			return id;
		}
	}
	return -1;
}

static void w_wmdump(unsigned seq)
{
	uintptr_t cat = rq(G.wmcat_pp);
	uint32_t n = cat_n(cat);
	if (!n) {
		logf_("FAIL seq=%u wmdump: no Combat/PlayerWeaponModes/Modes category (cat=%#lx)", seq, (unsigned long)cat);
		return;
	}
	char path[1200], nm[128], buf[3000], cn[96];
	FILE *f = out_file("wmodes", seq, path, sizeof path);
	if (f)
		fprintf(f, "index\tmode\tcomponent\telement\tfields\n");
	for (uint32_t i = 0; i < n && i < 1000; i++) {
		uintptr_t mode = cat_rec(cat, i), pred;
		rec_name(mode, nm, sizeof nm);
		db_dumpall(mode, G.sch_wm, buf, sizeof buf);
		if (f)
			fprintf(f, "%u\t%s\tmode\t-\t%s\n", i, nm, buf);
		char comps[400] = "";
		int o = 0;
		for (size_t k = 0; k < sizeof g_wm_comp / sizeof g_wm_comp[0]; k++) {
			struct dbv v;
			if (db_field(mode, G.sch_wm, g_wm_comp[k].field, &v) || v.type != 0xa)
				continue;
			o += cfmt(comps + o, sizeof comps - (size_t)o, " %s=%d", g_wm_comp[k].field, v.count);
			for (int e = 0; e < v.count && e < 8; e++) {
				uintptr_t sub = (uintptr_t)v.v[e];
				db_dumpall(sub, *g_wm_comp[k].holder, buf, sizeof buf);
				int id = k == 0 ? wm_cmd(sub) : -1;
				if (f)
					fprintf(f, "%u\t%s\t%s\t%s\t%s%s%s\n", i, nm, g_wm_comp[k].field, rec_name(sub, cn, sizeof cn), buf,
						id >= 0 ? ";CommandID->" : "", id >= 0 ? g_cmd_names[id] : "");
			}
		}
		int id = wm_mode_cmd(mode, &pred);
		char held[48], tog[48], raw[48], tok[48], mine[48], minr[48];
		logf_("WMODE %u '%s' command %d '%s' HeldDuration=%s IsToggle=%s UseRawInput=%s RequireToken=%s MinEnergy=%s/%s |%s",
		      i, nm, id, id >= 0 ? g_cmd_names[id] : "-", db_str(pred, G.sch_wmpred, "HeldDuration", held, sizeof held),
		      db_str(pred, G.sch_wmpred, "IsToggle", tog, sizeof tog), db_str(pred, G.sch_wmpred, "UseRawInput", raw, sizeof raw),
		      db_str(pred, G.sch_wmpred, "RequireToken", tok, sizeof tok),
		      db_str(pred, G.sch_wmpred, "MinEnergyToEnter", mine, sizeof mine),
		      db_str(pred, G.sch_wmpred, "MinEnergyToRemain", minr, sizeof minr), comps);
	}
	if (f)
		fclose(f);
	/* the player's own list: the character combat record's WeaponModeSetup */
	uintptr_t ccr = g_pi.scombat ? rq(g_pi.scombat + C_CHARREC) : 0, setup = db_reci(ccr, G.sch_ccr, CCR_WMSETUP);
	char rn[128], sn[96];
	db_dumpall(setup, G.sch_wmsetup, buf, sizeof buf);
	logf_("WMSETUP the player's record '%s' %s '%s': %s", rec_name(ccr, rn, sizeof rn), sch_name(G.sch_ccr, CCR_WMSETUP, sn, sizeof sn),
	      rec_name(setup, nm, sizeof nm), buf[0] ? buf : "(no fields)");
	logf_("DONE seq=%u wmdump: %u modes, file=%s", seq, n, f ? path : "(not written)");
}

/* a mode whose FreeAim list holds a record (P9: every mode has a FreeAim list of one element, a null record for most) */
static int wm_has_freeaim(uintptr_t mode)
{
	struct dbv v;
	if (db_field(mode, G.sch_wm, "FreeAim", &v) || v.type != 0xa)
		return 0;
	for (int k = 0; k < v.count && k < 8; k++)
		if (v.v[k] && (rb((uintptr_t)v.v[k] + 1) & 2)) /* a record with a name (DBRecord_GetName's flag) */
			return 1;
	return 0;
}

/* wmhold TOKEN[,TOKEN..] [MS]: press the command of the first mode the first token that matches any finds (an exact
 * mode name, else a name substring, or `freeaim` = a mode with a FreeAim record) for MS (default 3000, max 10000): the
 * stock way into the bow's aim when the command is data. P9's bug: `freeaim` matched every mode (an array of one null
 * element) and took mode 0, PC_Stealth (125) */
static void w_wmhold(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	uintptr_t cat = rq(G.wmcat_pp), pred = 0, mode = 0;
	uint32_t n = cat_n(cat);
	const char *want = argc > 1 ? argv[1] : "freeaim", *how = "";
	double ms = num(argc > 2 ? argv[2] : NULL, 3000);
	ms = ms < 50 ? 50 : ms > 10000 ? 10000 : ms;
	char nm[128], subs[128], *save = NULL;
	int id = -1;
	if (!c->pi) {
		logf_("FAIL seq=%u wmhold: no PlayerInput", seq);
		return;
	}
	snprintf(subs, sizeof subs, "%s", want);
	for (char *t = strtok_r(subs, ",", &save); t && id < 0; t = strtok_r(NULL, ",", &save))
		for (int pass = 0; pass < 2 && id < 0; pass++) /* 0: the exact name (or freeaim), 1: a substring */
			for (uint32_t i = 0; i < n && i < 1000 && id < 0; i++) {
				uintptr_t m = cat_rec(cat, i);
				int fa = !strcmp(t, "freeaim"), match;
				if (fa)
					match = pass == 0 && wm_has_freeaim(m);
				else if (pass == 0)
					match = !strcasecmp(rec_name(m, nm, sizeof nm), t);
				else
					match = strcasestr(rec_name(m, nm, sizeof nm), t) != NULL;
				if (match && (id = wm_mode_cmd(m, &pred)) >= 0) {
					mode = m;
					how = fa ? "a FreeAim record" : pass == 0 ? "the exact name" : "the name contains it";
				}
			}
	if (id < 0) {
		logf_("FAIL seq=%u wmhold %s: no mode with a command matches (%u modes)", seq, want, n);
		return;
	}
	char held[48], tog[48];
	int rc = vin_press(c->pi, id, -1, 1.0f, 1, 1, 1, ms / 1000.0);
	logf_("WMHOLD seq=%u mode '%s' (%s) command %d '%s' held for %.0f ms (HeldDuration=%s IsToggle=%s) rc=%d t=%.6f", seq,
	      rec_name(mode, nm, sizeof nm), how, id, g_cmd_names[id], ms,
	      db_str(pred, G.sch_wmpred, "HeldDuration", held, sizeof held), db_str(pred, G.sch_wmpred, "IsToggle", tog, sizeof tog),
	      rc, mono());
	logf_("DONE seq=%u wmhold: %s", seq, g_cmd_names[id]);
}

static struct {
	int on, hand, have_hand;
	unsigned seq;
	float yaw, pitch, hand_pos[3];
	double until, next_t;
	uintptr_t hobj;
	uint32_t hnode;
	_Atomic unsigned calls, over;
	float in[7], out[7];
	_Atomic int fresh;
} g_aim;

/* FUN_00a5b8b0(cam, &xf) inside the free-aim update: the aim ray's transform; aimray turns it (post) */
__attribute__((used)) void somvr_pc_bowcamxf(struct regs *r)
{
	first_call(HK_BOW_CAMXF, atomic_fetch_add_explicit(&g_h[HK_BOW_CAMXF].calls, 1, memory_order_relaxed) + 1);
	r->rax = ((two_fn)g_h[HK_BOW_CAMXF].orig)(r->rdi, r->rsi);
	atomic_fetch_add_explicit(&g_aim.calls, 1, memory_order_relaxed);
	if (!g_aim.on || mono() > g_aim.until || !r->rsi)
		return;
	float *xf = (float *)r->rsi, q[4], qy[4], qp[4], t[4], o[4];
	float y = g_aim.yaw * 0.008726646f, p = -g_aim.pitch * 0.008726646f; /* half angles, radians; pitch up = about -X */
	memcpy(g_aim.in, xf, sizeof g_aim.in);
	memcpy(q, xf + 3, sizeof q);
	qy[0] = 0, qy[1] = sinf(y), qy[2] = 0, qy[3] = cosf(y);
	qp[0] = sinf(p), qp[1] = 0, qp[2] = 0, qp[3] = cosf(p);
	qmul(q, qp, t);
	qmul(qy, t, o);
	qnorm(o);
	if (g_aim.hand && g_aim.have_hand)
		memcpy(xf, g_aim.hand_pos, 12);
	memcpy(xf + 3, o, sizeof o);
	memcpy(g_aim.out, xf, sizeof g_aim.out);
	atomic_fetch_add_explicit(&g_aim.over, 1, memory_order_relaxed);
	atomic_store(&g_aim.fresh, 1);
}

static void aim_line(const struct ctx *c, const char *what)
{
	float fz[3] = { 0, 0, 1 }, di[3], dd[3], ap[3] = { 0 }, rd3[3] = { 0 }, pa[3] = { 0 }, tf[3];
	qrot(g_aim.in + 3, fz, di);
	qrot(g_aim.out + 3, fz, dd);
	uintptr_t fa = c->pc ? c->pc + PC_FA : 0;
	if (fa) {
		rvec(fa, ap, 3);
		rvec(fa + FA_DIR, rd3, 3);
	}
	if (c->player)
		rvec(c->player + P_AIMPT, pa, 3);
	qrot(g_pi.rot, fz, tf);
	logf_("AIMRAY seq=%u %s: %u calls, %u turned; camera pos (%.1f %.1f %.1f) dir (%.3f %.3f %.3f) -> ours pos (%.1f %.1f %.1f) "
	      "dir (%.3f %.3f %.3f); FA active=%u aim point (%.1f %.1f %.1f) ray dir (%.3f %.3f %.3f) target %#lx; P+0x214 (%.1f %.1f "
	      "%.1f); Talion yaw %.1f deg t=%.6f", g_aim.seq, what, atomic_load(&g_aim.calls), atomic_load(&g_aim.over), g_aim.in[0],
	      g_aim.in[1], g_aim.in[2], di[0], di[1], di[2], g_aim.out[0], g_aim.out[1], g_aim.out[2], dd[0], dd[1], dd[2],
	      fa ? rb(fa + FA_ACTIVE) : 0, ap[0], ap[1], ap[2], rd3[0], rd3[1], rd3[2], (unsigned long)(fa ? rq(fa + FA_TARGET) : 0),
	      pa[0], pa[1], pa[2], (double)(atan2f(tf[0], tf[2]) * 57.29578f), mono());
}

/* aimray YAW PITCH [hand] [SECS] | aimray off: the free-aim ray turned by YAW deg right / PITCH deg up from the
 * camera's (origin at R_Hand with `hand`) for SECS (default 6, max 30); the game's ray, reticle, P+0x214 and the stock
 * arrow follow (re-bow.md 1.4 recipe 1) */
static void w_aimray(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "off")) {
		if (g_aim.on)
			aim_line(c, "off");
		g_aim.on = 0;
		logf_("DONE seq=%u aimray off", seq);
		return;
	}
	if (!g_h[HK_BOW_CAMXF].installed || argc < 3 || !is_num(argv[1]) || !is_num(argv[2])) {
		logf_("FAIL seq=%u aimray: want YAW PITCH [hand] [SECS] (hook P_BOW_CAMXF %s)", seq,
		      g_h[HK_BOW_CAMXF].installed ? "on" : "NOT installed");
		return;
	}
	g_aim.yaw = (float)num(argv[1], 0);
	g_aim.pitch = (float)num(argv[2], 0);
	g_aim.hand = 0;
	double secs = 6;
	for (int i = 3; i < argc; i++) {
		if (!strcmp(argv[i], "hand"))
			g_aim.hand = 1;
		else if (is_num(argv[i]))
			secs = num(argv[i], 6);
	}
	secs = secs < 0.5 ? 0.5 : secs > 30 ? 30 : secs;
	g_aim.yaw = g_aim.yaw < -90 ? -90 : g_aim.yaw > 90 ? 90 : g_aim.yaw;
	g_aim.pitch = g_aim.pitch < -60 ? -60 : g_aim.pitch > 60 ? 60 : g_aim.pitch;
	g_aim.have_hand = 0;
	g_aim.hobj = 0;
	memset(g_aim.in, 0, sizeof g_aim.in);
	memset(g_aim.out, 0, sizeof g_aim.out);
	atomic_store(&g_aim.calls, 0);
	atomic_store(&g_aim.over, 0);
	atomic_store(&g_aim.fresh, 0);
	g_aim.seq = seq;
	g_aim.until = mono() + secs;
	g_aim.next_t = 0;
	g_aim.on = 1;
	logf_("DONE seq=%u aimray: yaw %+.1f pitch %+.1f origin %s for %.1f s (the free-aim update calls FUN_00a5b8b0 only while a "
	      "FreeAim mode is on: `wmhold freeaim` first)", seq, (double)g_aim.yaw, (double)g_aim.pitch,
	      g_aim.hand ? "R_Hand" : "the camera", secs);
}

static void aim_tick(const struct ctx *c)
{
	uintptr_t o = char_obj(c);
	if (g_aim.hand && o) { /* R_Hand's world position for the next free-aim update (one frame old) */
		float x[8];
		if (o != g_aim.hobj) {
			g_aim.hobj = o;
			g_aim.hnode = 0xffffffffu;
			if (g_fn_ok[FN_GETNODE] && (FN(FN_GETNODE, getnode_fn)(model_iface(), o, "R_Hand", &g_aim.hnode) & 0xff))
				g_aim.hnode = 0xffffffffu;
		}
		if (g_aim.hnode != 0xffffffffu && !node_xf(o, g_aim.hnode, x)) {
			memcpy(g_aim.hand_pos, x, 12);
			g_aim.have_hand = 1;
		}
	}
	double t = mono();
	if (t > g_aim.until) {
		aim_line(c, "end");
		g_aim.on = 0;
		return;
	}
	if (t >= g_aim.next_t && rl_ok(RL_AIM)) {
		g_aim.next_t = t + 0.5;
		aim_line(c, atomic_exchange(&g_aim.fresh, 0) ? "turned" : "no free-aim update since the last line");
	}
}

/* ---- per tick and the reset ---- */

static void p9_tick(const struct ctx *c)
{
	if (g_sp_n)
		sp_tick();
	if (g_aim.on)
		aim_tick(c);
}

static void p9_reset(void)
{
	g_sp_n = 0; /* a new player: the old spawns are the game's now (not removed) */
	g_aim.on = 0;
	g_aib.force = 0;
	g_tp.done = 0;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P10: locomotion (design-locomotion.md 2 and 4.2): the root delta (P_ROOTDELTA = LOCO_ROOTDELTA), speed, pmrec,  */
/* heading/walk, loco, fling, animrate, vaultlog, npcpush                                                         */

typedef uintptr_t (*gvar_fn)(uintptr_t mgr, const char *name);
typedef float (*gget_fn)(uintptr_t mgr, uintptr_t var);
typedef void (*velmode_fn)(uintptr_t pm, int rootmotion);

#define RAD2DEG 57.29578f
#define PI_F 3.14159265f

static float yaw_of(const float q[4])
{
	float fz[3] = { 0, 0, 1 }, f[3];
	qrot(q, fz, f);
	return atan2f(f[0], f[2]);
}
static float wrap_pi(float a)
{
	while (a > PI_F)
		a -= 2.0f * PI_F;
	while (a < -PI_F)
		a += 2.0f * PI_F;
	return a;
}
/* v in the frame of q: R(q)^-1 v */
static void qrot_inv(const float q[4], const float v[3], float o[3])
{
	float c[4] = { -q[0], -q[1], -q[2], q[3] };
	qrot(c, v, o);
}
/* the horizontal unit vector of yaw (atan2(x, z)) and its right */
static void yaw_dir(float yaw, float f[3], float r[3])
{
	f[0] = sinf(yaw), f[1] = 0, f[2] = cosf(yaw);
	if (r)
		r[0] = cosf(yaw), r[1] = 0, r[2] = -sinf(yaw);
}
static float hdist(const float a[3], const float b[3])
{
	float dx = a[0] - b[0], dz = a[2] - b[2];
	return sqrtf(dx * dx + dz * dz);
}

/* the gameplay camera's yaw (CAM_ROT x CAM_EXTRA, as surf reads it); 0 = ok */
static int cam_yaw(const struct ctx *c, float *yaw)
{
	float q0[4], q1[4], q[4];
	if (!c->cam || rvec(c->cam + CAM_ROT, q0, 4) || rvec(c->cam + CAM_EXTRA, q1, 4))
		return -1;
	qmul(q0, q1, q);
	if (q[0] == 0 && q[1] == 0 && q[2] == 0 && q[3] == 0)
		return -1;
	*yaw = yaw_of(q);
	return 0;
}

/* ---- the root sample of every probe tick (all P10 words read it) ---- */

#define RH_N 256
static struct {
	int have;
	double t, dt;
	float pos[3], hs, vs, yaw; /* hs/vs = the horizontal / vertical speed over the last probe tick (cm/s) */
	struct { double t; float hs; } h[RH_N];
	unsigned hn;
} g_root;

static void root_tick(const struct ctx *c)
{
	uintptr_t o = char_obj(c);
	if (!o || !c->player) {
		g_root.have = 0;
		g_root.hs = g_root.vs = 0;
		return;
	}
	float p[3], q[4] = { 0, 0, 0, 1 };
	obj_pos(o, p);
	rvec(o + OBJ_ROT, q, 4);
	double t = mono();
	g_root.dt = g_root.have ? t - g_root.t : 0;
	if (g_root.dt > 1e-4) {
		float dx = p[0] - g_root.pos[0], dy = p[1] - g_root.pos[1], dz = p[2] - g_root.pos[2];
		g_root.hs = (float)(sqrtf(dx * dx + dz * dz) / g_root.dt);
		g_root.vs = (float)(dy / g_root.dt);
	} else {
		g_root.hs = g_root.vs = 0;
	}
	memcpy(g_root.pos, p, sizeof p);
	g_root.yaw = yaw_of(q);
	g_root.t = t;
	g_root.have = 1;
	g_root.h[g_root.hn % RH_N].t = t;
	g_root.h[g_root.hn % RH_N].hs = g_root.hs;
	g_root.hn++;
}

/* the mean horizontal speed of the probe ticks in [t0, t1); *n = their count */
static float root_mean(double t0, double t1, int *n)
{
	double s = 0;
	int k = 0;
	for (unsigned i = 0; i < RH_N && i < g_root.hn; i++) {
		unsigned j = (g_root.hn - 1 - i) % RH_N;
		if (g_root.h[j].t < t0)
			break;
		if (g_root.h[j].t < t1)
			s += g_root.h[j].hs, k++;
	}
	*n = k;
	return k ? (float)(s / k) : 0.0f;
}

/* ---- the graph's inputs read back (FUN_0208dd90 = the variable by name, FUN_0208de50 = its value) ---- */

static const char *const g_gv_names[] = { "RawMoveMag", "Game_Velocity", "Game_SmoothedVelocity", "DeltaYaw" };
#define GV_N 4
static struct { uintptr_t mgr, def, var[GV_N]; } g_gv;

/* the values the animation inputs (FUN_00a3c8d0) wrote last; NAN = no such variable */
static int graph_vals(const struct ctx *c, float out[GV_N])
{
	for (int i = 0; i < GV_N; i++)
		out[i] = NAN;
	uintptr_t mgr = c->body ? rq(c->body + BODY_MGR) : 0, def = mgr ? rq(mgr + MGR_GRAPHDEF) : 0;
	if (!def || !g_fn_ok[FN_GRAPH_VAR] || !g_fn_ok[FN_GRAPH_GET])
		return -1;
	if (g_gv.mgr != mgr || g_gv.def != def) {
		g_gv.mgr = mgr;
		g_gv.def = def;
		for (int i = 0; i < GV_N; i++)
			g_gv.var[i] = FN(FN_GRAPH_VAR, gvar_fn)(mgr, g_gv_names[i]);
		logf_("GRAPH the animation manager %#lx (graph %#lx): RawMoveMag %#lx, Game_Velocity %#lx, Game_SmoothedVelocity %#lx, "
		      "DeltaYaw %#lx", (unsigned long)mgr, (unsigned long)def, (unsigned long)g_gv.var[0], (unsigned long)g_gv.var[1],
		      (unsigned long)g_gv.var[2], (unsigned long)g_gv.var[3]);
	}
	for (int i = 0; i < GV_N; i++)
		if (g_gv.var[i])
			out[i] = FN(FN_GRAPH_GET, gget_fn)(mgr, g_gv.var[i]);
	return 0;
}

/* the probe's held stick: StrafeAxis 6 (x right) and ForwardAxis 3 (y forward); 0 = neither held */
static int probe_stick(float *x, float *y)
{
	int got = 0, n = atomic_load(&g_vin_n);
	*x = *y = 0;
	for (int i = 0; i < n; i++) {
		if (!g_vin[i].rec || !g_vin[i].has_value)
			continue;
		if (g_vin[i].cmd == 6)
			*x = g_vin[i].value, got = 1;
		else if (g_vin[i].cmd == 3)
			*y = g_vin[i].value, got = 1;
	}
	return got;
}

/* the state tags (as the state logger reads them): the count, *set and the signature */
static int tags_read(const struct ctx *c, uintptr_t *set_out, uint64_t *sig)
{
	*sig = 0;
	*set_out = 0;
	if (!c->body)
		return 0;
	uintptr_t mgr = c->body + BODY_TAGS, set = mgr + (uintptr_t)rb(mgr + 0x158) * 0xa8 + 8;
	int tn = (int)r32(set + 0xa0);
	if (tn < 0 || tn > 20)
		tn = 0;
	uintptr_t recs[20];
	if (tn && !rd(set, recs, (size_t)tn * 8))
		for (int i = 0; i < tn; i++)
			*sig = *sig * 1000003u + recs[i];
	*set_out = set;
	return tn;
}
static const char *tags_names(uintptr_t set, int tn, char *out, size_t cap)
{
	char nm[96];
	int o = 0;
	out[0] = 0;
	for (int i = 0; i < tn && (size_t)o + 100 < cap; i++)
		o += cfmt(out + o, cap - (size_t)o, "%s%s", i ? "," : "", rec_name(rq(set + 8 * (uintptr_t)i), nm, sizeof nm));
	return out;
}

/* ---- P_ROOTDELTA: FUN_00a513a0(body, &delta {pos[3] in Talion's frame, quat[4]}) inside PlayerMovement::vf_11 ---- */

static struct {
	float game[7], ours[3];   /* the last player call: the delta as the game made it / the translation loco wrote */
	float gsum[3];            /* the game's translations summed since the probe tick took them */
	int gcnt, rcnt;           /* calls since then / of them replaced */
} g_rd;

enum { LOCO_OFF, LOCO_CONST, LOCO_STICK };
static struct {
	_Atomic int mode;
	unsigned seq;
	double t0, until, last_wall, last_line;
	float vw[3], vstick;      /* the asked world velocity (cm/s; stick: set every probe tick), cm/s at a full stick */
	uintptr_t player;
	char who[16], frame[64];
	_Atomic uint64_t calls, replaced, sk_fsm, sk_mount, sk_sync, lock_t, lock_r, attack, dt_obj, dt_game, dt_wall;
	float last_dt;
	/* the probe tick's statistics */
	int ticks, nmean, first90, lines;
	double sum_hs, sum_ask;
	float max_hs, last_ask;
	uint32_t fsms;
	unsigned stop_seq;
	int stop_left, stop_n;
	float stop_hs[3];
	char stop_what[24];
} g_loco;

/* this tick's dt for v*dt: the player object's timer (HOBJECT+0x68, frame s at +0x10), else the game timer, else the
 * wall clock since the last call */
static float loco_dt(void)
{
	uintptr_t o = g_pi.charobj, t = o ? rq(o + OBJ_TIMER) : 0;
	float dt = t ? rf(t + TM_FRAME + 4) : 0;
	double now = mono(), w = g_loco.last_wall > 0 ? now - g_loco.last_wall : 1.0 / 60;
	g_loco.last_wall = now;
	if (dt > 0.0005f && dt < 0.25f) {
		atomic_fetch_add_explicit(&g_loco.dt_obj, 1, memory_order_relaxed);
		return dt;
	}
	uintptr_t gt = rq(rq(G.pause_pp) + 8);
	dt = gt ? rf(gt + TM_FRAME + 4) : 0;
	if (dt > 0.0005f && dt < 0.25f) {
		atomic_fetch_add_explicit(&g_loco.dt_game, 1, memory_order_relaxed);
		return dt;
	}
	atomic_fetch_add_explicit(&g_loco.dt_wall, 1, memory_order_relaxed);
	return (float)(w < 0.001 ? 0.001 : w > 0.1 ? 0.1 : w);
}

/* post: the game's delta is kept for speed; with loco on (FSM 6/7, not mounted, not attached, no sync action) its
 * translation becomes R(xf.rot)^-1 (v*dt) with y = 0 in the world; the rotation stays the game's */
__attribute__((used)) void somvr_pc_rootdelta(struct regs *r)
{
	first_call(HK_ROOTDELTA, atomic_fetch_add_explicit(&g_h[HK_ROOTDELTA].calls, 1, memory_order_relaxed) + 1);
	r->rax = ((two_fn)g_h[HK_ROOTDELTA].orig)(r->rdi, r->rsi);
	float *d = (float *)r->rsi;
	uintptr_t body = r->rdi, player = g_pi.player;
	if (!d || !player || body != rq(player + P_BODY))
		return;
	memcpy(g_rd.game, d, sizeof g_rd.game);
	memcpy(g_rd.ours, d, sizeof g_rd.ours); /* what vf_11 goes on with: the game's unless replaced below */
	for (int k = 0; k < 3; k++)
		g_rd.gsum[k] += d[k];
	g_rd.gcnt++;
	if (!atomic_load_explicit(&g_loco.mode, memory_order_acquire) || player != g_loco.player)
		return;
	atomic_fetch_add_explicit(&g_loco.calls, 1, memory_order_relaxed);
	uint8_t fsm = rb(player + P_FSM);
	uintptr_t pm = rq(player + P_PM), sync = g_pi.scombat ? rq(g_pi.scombat + C_SYNC1) : 0;
	if (fsm != 6 && fsm != 7) {
		atomic_fetch_add_explicit(&g_loco.sk_fsm, 1, memory_order_relaxed);
		return;
	}
	if (rq(player + P_MOUNT) || (pm && rb(pm + PM_ATTACHED))) {
		atomic_fetch_add_explicit(&g_loco.sk_mount, 1, memory_order_relaxed);
		return;
	}
	if (sync && rq(sync + SYNC_REC)) {
		atomic_fetch_add_explicit(&g_loco.sk_sync, 1, memory_order_relaxed);
		return;
	}
	float dt = loco_dt(), q[4], dw[3] = { g_loco.vw[0] * dt, 0, g_loco.vw[2] * dt }, dl[3];
	if (t_vf11_xf && t_vf11_pm == (void *)pm)
		memcpy(q, (float *)t_vf11_xf + 3, sizeof q);
	else
		memcpy(q, g_pi.rot, sizeof q);
	qrot_inv(q, dw, dl);
	memcpy(d, dl, sizeof dl);
	memcpy(g_rd.ours, dl, sizeof dl);
	g_rd.rcnt++;
	g_loco.last_dt = dt;
	atomic_fetch_add_explicit(&g_loco.replaced, 1, memory_order_relaxed);
	uintptr_t mgr = rq(body + BODY_MGR);
	if (pm && rq(pm + PM_ATK))
		atomic_fetch_add_explicit(&g_loco.attack, 1, memory_order_relaxed); /* the locks are skipped while one runs */
	if (mgr && rb(mgr + MGR_LOCK_T))
		atomic_fetch_add_explicit(&g_loco.lock_t, 1, memory_order_relaxed); /* vf_11 zeroes our translation */
	if (mgr && rb(mgr + MGR_LOCK_R))
		atomic_fetch_add_explicit(&g_loco.lock_r, 1, memory_order_relaxed);
}

/* ---- heading: the probe's direction for walk, loco, fling and npcpush; a ray scan finds free ground ---- */

static struct { int set; float yaw; char how[40]; } g_head;
static int g_stick_mirror; /* walk found the game's stick x mirrored against the probe's math (flipped from then on) */

/* one G21 ray from -> to; 1 = a hit (*hit, *obj), 0 = none, -1 = the physics slot did not check out */
static int g21_ray(const float from[3], const float to[3], uint32_t flags, uint32_t filter, float hit[3], uintptr_t *obj)
{
	uintptr_t phys = rq(G.phys_pp), vt = phys ? rq(phys) : 0;
	if (!g_ro || !g_ro_ok[RO_PHYS_RAY] || vt != G.vt_phys || vt + 0x200 != g_ro[RO_PHYS_RAY].addr ||
	    rq(vt + 0x200) != g_ro[RO_PHYS_RAY].target)
		return -1;
	unsigned char q[0x40] __attribute__((aligned(16))), r[0x80] __attribute__((aligned(16)));
	uint16_t nos = 0xffff;
	memset(q, 0, sizeof q);
	memset(r, 0, sizeof r);
	memcpy(q, from, 12);
	memcpy(q + 12, to, 12);
	memcpy(q + 0x18, &flags, 4);
	memcpy(q + 0x1c, &filter, 4);
	memcpy(r + 0x28, &nos, 2);
	uint64_t rc = ((raycast_fn)g_ro[RO_PHYS_RAY].target)(phys, q, r);
	memcpy(hit, r, 12);
	memcpy(obj, r + 0x20, 8);
	return (rc & 0xff) != 0;
}

#define HD_N 16
/* the free distance (m) along 16 yaws from BASE: the nearer of two rays from the root (40 and 100 cm up) and the first
 * drop (no ground within 3 m below, checked every 3 m); a hit on Talion himself or at the start point (P8: the surf
 * ray hit his Flesh_Player at 0 cm) is cast again from 45 cm further out, at most 3 times (*selfhits counts them);
 * -1 = no physics */
static int head_scan(const float p0[3], float base, float maxm, float yaws[HD_N], float free_m[HD_N], int *selfhits)
{
	float maxd = maxm * 100.0f;
	*selfhits = 0;
	for (int k = 0; k < HD_N; k++) {
		float yaw = wrap_pi(base + (float)k * 2.0f * PI_F / HD_N), f[3], fr = maxd;
		yaw_dir(yaw, f, NULL);
		yaws[k] = yaw;
		for (int h = 0; h < 2; h++) {
			float up = h ? 100.0f : 40.0f, from[3], to[3], hit[3];
			for (int k2 = 0; k2 < 3; k2++) {
				from[k2] = p0[k2];
				to[k2] = p0[k2] + f[k2] * maxd;
			}
			from[1] += up, to[1] += up;
			for (int tries = 0; tries < 4; tries++) {
				uintptr_t obj = 0;
				int rc = g21_ray(from, to, 0x58, 0xfffffffdu, hit, &obj);
				if (rc < 0)
					return -1;
				if (!rc)
					break;
				if (tries < 3 && ((obj && (obj == g_pi.phobj || obj == g_pi.charobj)) || dist3(hit, from) < 5.0f)) {
					(*selfhits)++; /* Talion's own shape (or a start inside one): look past it */
					for (int k2 = 0; k2 < 3; k2++)
						if (k2 != 1)
							from[k2] = p0[k2] + f[k2] * 45.0f * (float)(tries + 1);
					continue;
				}
				float dd = hdist(hit, p0);
				if (dd < fr)
					fr = dd;
				break;
			}
		}
		for (float s = 300.0f; s <= fr; s += 300.0f) { /* the ground ahead: no ground within 3 m below = a drop */
			float a[3] = { p0[0] + f[0] * s, p0[1] + 150.0f, p0[2] + f[2] * s }, b[3] = { a[0], p0[1] - 300.0f, a[2] }, hit[3];
			uintptr_t obj = 0;
			if (g21_ray(a, b, 0x50, 0xc, hit, &obj) == 0) {
				fr = s - 100.0f;
				break;
			}
		}
		free_m[k] = fr / 100.0f;
	}
	return 0;
}

/* heading clear [M] | open | wall [M] | back | fwd | cam | orc | YAW */
static void w_heading(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	const char *how = argc > 1 ? argv[1] : "clear";
	if (!g_root.have || !c->player) {
		logf_("FAIL seq=%u heading: no player root", seq);
		return;
	}
	float face = g_root.yaw, cy = 0, yaw = face;
	int have_cam = cam_yaw(c, &cy) == 0;
	char extra[160] = "";
	if (!strcmp(how, "clear") || !strcmp(how, "open") || !strcmp(how, "wall")) {
		int wall = how[0] == 'w', open = how[0] == 'o';
		float m = (float)num(argc > 2 ? argv[2] : NULL, wall ? 10 : 15);
		m = m < 1 ? 1 : m > 40 ? 40 : m;
		float maxm = wall ? m : m + 5 > 40 ? 40 : m + 5, yaws[HD_N], fr[HD_N];
		int self = 0;
		if (head_scan(g_root.pos, face, maxm, yaws, fr, &self)) {
			logf_("FAIL seq=%u heading %s: the physics ray (PHYS_RAYCAST) did not check out", seq, how);
			return;
		}
		char line[900];
		int o = 0, best = -1;
		for (int k = 0; k < HD_N; k++)
			o += cfmt(line + o, sizeof line - (size_t)o, " %.0f:%.1f", (double)(yaws[k] * RAD2DEG), (double)fr[k]);
		if (wall) {
			for (int k = 0; k < HD_N; k++)
				if (fr[k] < m && (best < 0 || fr[k] < fr[best]))
					best = k;
		} else if (open) {
			for (int k = 0; k < HD_N; k++)
				if (best < 0 || fr[k] > fr[best])
					best = k;
		} else {
			for (int s = 0; s <= HD_N / 2 && best < 0; s++) { /* nearest Talion's facing first: 0, +1, -1, +2, ... */
				int a = s % HD_N, b = (HD_N - s) % HD_N;
				if (fr[a] >= m)
					best = a;
				else if (fr[b] >= m)
					best = b;
			}
			if (best < 0)
				for (int k = 0; k < HD_N; k++)
					if (best < 0 || fr[k] > fr[best])
						best = k;
		}
		logf_("HEADING seq=%u scan from (%.1f %.1f %.1f) facing %.1f deg, %.0f m, %d self hits (yaw deg:free m):%s", seq,
		      g_root.pos[0], g_root.pos[1], g_root.pos[2], (double)(face * RAD2DEG), (double)maxm, self, line);
		if (best < 0) {
			logf_("FAIL seq=%u heading wall: no obstacle within %.0f m", seq, (double)m);
			return;
		}
		yaw = yaws[best];
		cfmt(extra, sizeof extra, " (%.1f m free%s)", (double)fr[best], !wall && !open && fr[best] < m ? "; none had the "
		     "distance asked: the freest" : "");
	} else if (!strcmp(how, "back")) {
		yaw = (g_head.set ? g_head.yaw : face) + PI_F;
	} else if (!strcmp(how, "fwd")) {
		yaw = face;
	} else if (!strcmp(how, "cam")) {
		if (!have_cam) {
			logf_("FAIL seq=%u heading cam: no camera", seq);
			return;
		}
		yaw = cy;
	} else if (!strcmp(how, "orc")) {
		struct orc o;
		const char *why = "";
		if (!target_orc(c, &o, &why)) {
			logf_("FAIL seq=%u heading orc: no target orc (%s)", seq, why);
			return;
		}
		yaw = atan2f(o.pos[0] - g_root.pos[0], o.pos[2] - g_root.pos[2]);
		cfmt(extra, sizeof extra, " (%s at %.1f m)", why, (double)(hdist(o.pos, g_root.pos) / 100.0f));
	} else if (is_num(how)) {
		yaw = (float)num(how, 0) / RAD2DEG;
	} else {
		logf_("FAIL seq=%u heading: want clear [M], open, wall [M], back, fwd, cam, orc or a yaw in degrees", seq);
		return;
	}
	g_head.set = 1;
	g_head.yaw = wrap_pi(yaw);
	snprintf(g_head.how, sizeof g_head.how, "%s", how);
	logf_("DONE seq=%u heading %s: yaw %.1f deg%s (Talion faces %.1f, the camera %.1f%s)", seq, how,
	      (double)(g_head.yaw * RAD2DEG), extra, (double)(face * RAD2DEG), (double)(cy * RAD2DEG), have_cam ? "" : ": none");
}

/* the frame of loco const / fling: the heading if set, else Talion's facing (world = the world axes) */
static void head_frame(int world, float f[3], float r[3], char *desc, size_t cap)
{
	if (world) {
		f[0] = 0, f[1] = 0, f[2] = 1, r[0] = 1, r[1] = 0, r[2] = 0;
		snprintf(desc, cap, "the world axes");
		return;
	}
	float yaw = g_head.set ? g_head.yaw : g_root.yaw;
	yaw_dir(yaw, f, r);
	if (g_head.set)
		snprintf(desc, cap, "the heading (%s, %.1f deg)", g_head.how, (double)(yaw * RAD2DEG));
	else
		snprintf(desc, cap, "Talion's facing (%.1f deg; no heading)", (double)(yaw * RAD2DEG));
}

/* ---- walk: the stock stick along the heading, re-aimed through the camera's yaw every tick ---- */

static struct {
	int on, checked;
	unsigned seq;
	float mag, p0[3], cam0, yaw;
	double t0, until;
	uintptr_t pi;
} g_walk;

static void walk_stick(float yaw, float camy, float mag, float *x, float *y)
{
	float rel = wrap_pi(yaw - camy);
	*x = sinf(rel) * mag * (g_stick_mirror ? -1.0f : 1.0f);
	*y = cosf(rel) * mag;
}

/* a held value changed without a new press line (walk updates its two axes every tick) */
static int vin_set(uintptr_t pi, int cmd, float v)
{
	int i = vin_find(pi_rec(pi, cmd));
	if (i < 0)
		return -1;
	g_vin[i].value = v;
	g_vin[i].on = fabsf(v) > 0.25f;
	return 0;
}

static void walk_end(const char *why)
{
	if (!g_walk.on)
		return;
	g_walk.on = 0;
	float d = hdist(g_root.pos, g_walk.p0), ty = atan2f(g_root.pos[0] - g_walk.p0[0], g_root.pos[2] - g_walk.p0[2]), cy = 0;
	struct ctx c;
	ctx_get(&c);
	int hc = cam_yaw(&c, &cy) == 0;
	logf_("WALK seq=%u end (%s) after %.2f s: %.1f m travelled toward %.1f deg (heading %.1f, off by %+.1f); the camera "
	      "turned %+.1f deg%s; stick x %s", g_walk.seq, why, mono() - g_walk.t0, (double)(d / 100.0f), (double)(ty * RAD2DEG),
	      (double)(g_walk.yaw * RAD2DEG), d > 1 ? (double)(wrap_pi(ty - g_walk.yaw) * RAD2DEG) : 0.0,
	      hc ? (double)(wrap_pi(cy - g_walk.cam0) * RAD2DEG) : 0.0, hc ? "" : " (no camera)",
	      g_stick_mirror ? "mirrored (flipped)" : "as computed");
}

static void w_walk(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "off")) {
		int was = g_walk.on;
		walk_end("off");
		if (was && c->pi) {
			vin_set(c->pi, 6, 0);
			vin_set(c->pi, 3, 0);
		}
		logf_("DONE seq=%u walk off%s", seq, was ? " (the axes at 0 until their release)" : " (none on)");
		return;
	}
	float cy;
	if (argc < 2 || !is_num(argv[1]) || !c->pi || cam_yaw(c, &cy) || !g_root.have) {
		logf_("FAIL seq=%u walk: want MAG [MS] (PlayerInput %#lx, camera %#lx, root %d)", seq, (unsigned long)c->pi,
		      (unsigned long)c->cam, g_root.have);
		return;
	}
	float mag = (float)num(argv[1], 0);
	double ms = num(argc > 2 ? argv[2] : NULL, 3000);
	mag = mag < 0 ? 0 : mag > 1 ? 1 : mag;
	ms = ms < 100 ? 100 : ms > 15000 ? 15000 : ms;
	if (g_walk.on)
		walk_end("replaced by a new walk");
	float yaw = g_head.set ? g_head.yaw : g_root.yaw, x, y;
	walk_stick(yaw, cy, mag, &x, &y);
	int n = (vin_press(c->pi, 6, -1, x, 1, fabsf(x) > 0.25f, 0, ms / 1000.0) == 0) +
		(vin_press(c->pi, 3, -1, y, 1, fabsf(y) > 0.25f, 0, ms / 1000.0) == 0);
	memset(&g_walk, 0, sizeof g_walk);
	g_walk.on = n == 2;
	g_walk.seq = seq;
	g_walk.mag = mag;
	g_walk.yaw = yaw;
	g_walk.cam0 = cy;
	g_walk.pi = c->pi;
	g_walk.t0 = mono();
	g_walk.until = g_walk.t0 + ms / 1000.0;
	memcpy(g_walk.p0, g_root.pos, sizeof g_walk.p0);
	logf_("WALK seq=%u along %.1f deg (%s) at %.2f for %.0f ms: camera %.1f deg -> stick (%.3f %.3f)%s t=%.6f", seq,
	      (double)(yaw * RAD2DEG), g_head.set ? g_head.how : "Talion's facing: no heading", (double)mag, ms, (double)(cy * RAD2DEG),
	      (double)x, (double)y, g_stick_mirror ? " (x mirrored)" : "", mono());
	logf_("DONE seq=%u walk: %d axes held", seq, n);
}

static void walk_tick(const struct ctx *c)
{
	float cy, x, y;
	if (!c->pi || c->pi != g_walk.pi || vin_find(pi_rec(c->pi, 3)) < 0 || vin_find(pi_rec(c->pi, 6)) < 0) {
		walk_end(mono() >= g_walk.until ? "time" : "the axes were released");
		return;
	}
	if (cam_yaw(c, &cy))
		return;
	double t = mono();
	if (!g_walk.checked && t - g_walk.t0 > 0.6) { /* once: is the game's stick x the probe's? */
		float d = hdist(g_root.pos, g_walk.p0);
		if (d > 40.0f) {
			g_walk.checked = 1;
			float ty = atan2f(g_root.pos[0] - g_walk.p0[0], g_root.pos[2] - g_walk.p0[2]);
			float err = wrap_pi(ty - g_walk.yaw), mir = wrap_pi(ty - (2.0f * cy - g_walk.yaw));
			int flip = fabsf(err) > 50.0f / RAD2DEG && fabsf(mir) < fabsf(err) - 20.0f / RAD2DEG;
			if (flip)
				g_stick_mirror ^= 1;
			logf_("WALK seq=%u check after %.0f cm: travel %.1f deg, heading %.1f (off %+.1f), the mirror image %+.1f%s", g_walk.seq,
			      (double)d, (double)(ty * RAD2DEG), (double)(g_walk.yaw * RAD2DEG), (double)(err * RAD2DEG),
			      (double)(mir * RAD2DEG), flip ? ": the stick's x is mirrored, flipped from now on" : ": ok");
		}
	}
	walk_stick(g_walk.yaw, cy, g_walk.mag, &x, &y);
	vin_set(c->pi, 6, x);
	vin_set(c->pi, 3, y);
}

/* ---- speed: the stock (or our) speed per tick ---- */

#define SPD_N 4096
static struct {
	int on, n, tn;
	unsigned seq;
	double t0, until, next_line;
	FILE *f;
	char path[1200];
	uint64_t tags;
	uint32_t fsms, vms;
	struct { float t, dt, hs, stick, raw; } r[SPD_N];
} g_spd;

static void spd_end(void);

static void w_speed(const struct ctx *c, unsigned seq, const char *arg)
{
	double secs = num(arg, 4);
	secs = secs < 0.5 ? 0.5 : secs > 60 ? 60 : secs;
	if (g_spd.on)
		spd_end(); /* the window before: its summary now */
	memset(&g_spd, 0, sizeof g_spd);
	g_spd.f = out_file("speed", seq, g_spd.path, sizeof g_spd.path);
	if (g_spd.f)
		fprintf(g_spd.f, "t\tdt\tx\ty\tz\ths\tvs\tyaw\tvx\tvy\tvz\tvh\tvelmode\tfsm\tfsm_prev\tsupported\tRawMoveMag\t"
			"Game_Velocity\tGame_SmoothedVelocity\tDeltaYaw\tstick_x\tstick_y\tloco\tasked\tgame_n\tgame_dx\tgame_dy\t"
			"game_dz\tgame_speed\tours_n\tours_dx\tours_dz\ttags\n");
	g_spd.on = 1;
	g_spd.seq = seq;
	g_spd.t0 = mono();
	g_spd.until = g_spd.t0 + secs;
	float gv[GV_N];
	int gok = graph_vals(c, gv) == 0;
	logf_("DONE seq=%u speed %.1f s: per tick -> %s (graph values %s; P_ROOTDELTA %s)", seq, secs, g_spd.f ? g_spd.path :
	      "(not written)", gok ? "read" : "NOT readable (FUN_0208dd90/de50 or the manager)", g_h[HK_ROOTDELTA].installed ? "on" :
	      "NOT installed");
}

static void spd_end(void)
{
	g_spd.on = 0;
	if (g_spd.f)
		fclose(g_spd.f);
	g_spd.f = NULL;
	int n = g_spd.n, press = -1, rel = -1, probe = 0;
	float st = 0;
	for (int i = 0; i < n; i++) /* the probe's stick if it held one in the window (-1 = not held), else RawMoveMag */
		probe |= g_spd.r[i].stick >= 0;
	for (int i = 0; i < n; i++) {
		float sm = probe ? g_spd.r[i].stick : g_spd.r[i].raw == g_spd.r[i].raw ? g_spd.r[i].raw : 0;
		int on = sm > 0.05f;
		if (on && press < 0)
			press = i, st = sm;
		if (!on && press >= 0 && rel < 0)
			rel = i;
	}
	float plat = 0, vmax = 0, t90 = -1, tstop = -1, dstop = 0;
	int np = 0;
	for (int i = 0; i < n; i++)
		if (g_spd.r[i].hs > vmax && g_spd.r[i].dt > 0.003f)
			vmax = g_spd.r[i].hs;
	if (press >= 0) {
		int end = rel >= 0 ? rel : n;
		float tend = end > 0 ? g_spd.r[end - 1].t : 0, held = tend - g_spd.r[press].t;
		float from = held >= 1.5f ? tend - 1.0f : g_spd.r[press].t + held / 2;
		double s = 0;
		for (int i = press; i < end; i++)
			if (g_spd.r[i].t >= from)
				s += g_spd.r[i].hs, np++;
		plat = np ? (float)(s / np) : 0;
		for (int i = press + 2; i < end && t90 < 0 && plat > 1; i++) /* the 3-tick mean reaches 90 % of the plateau */
			if ((g_spd.r[i].hs + g_spd.r[i - 1].hs + g_spd.r[i - 2].hs) / 3 >= 0.9f * plat)
				t90 = g_spd.r[i].t - g_spd.r[press].t;
		if (rel >= 0) {
			float lim = plat * 0.05f > 10 ? plat * 0.05f : 10;
			for (int i = rel; i < n && tstop < 0; i++) {
				dstop += g_spd.r[i].hs * g_spd.r[i].dt;
				if (i + 1 < n && g_spd.r[i].hs < lim && g_spd.r[i + 1].hs < lim)
					tstop = g_spd.r[i].t - g_spd.r[rel].t;
			}
		}
	}
	char fs[80] = "", vs[16] = "";
	int o = 0;
	for (int k = 0; k < 32; k++)
		if (g_spd.fsms >> k & 1)
			o += cfmt(fs + o, sizeof fs - (size_t)o, "%s%d", o ? "," : "", k);
	o = 0;
	for (int k = 0; k < 2; k++)
		if (g_spd.vms >> k & 1)
			o += cfmt(vs + o, sizeof vs - (size_t)o, "%s%d", o ? "," : "", k);
	if (press < 0)
		logf_("SPEED seq=%u end: %d rows, no stick held (the probe's, else RawMoveMag); max %.1f cm/s; fsm {%s} velmode {%s}; "
		      "file=%s", g_spd.seq, n, (double)vmax, fs, vs, g_spd.path);
	else
		logf_("SPEED seq=%u end: %d rows; %s %.2f from +%.3f s%s: plateau %.1f cm/s (%d ticks), max %.1f, 90 %% after %.3f s; "
		      "%s %.3f s and %.1f cm after the release; fsm {%s} velmode {%s}; file=%s", g_spd.seq, n,
		      probe ? "stick" : "RawMoveMag", (double)st, (double)g_spd.r[press].t, rel >= 0 ? "" : " to the end", (double)plat,
		      np, (double)vmax, (double)t90,
		      rel < 0 ? "no release: -" : tstop < 0 ? "not stopped within the window:" : "stopped", (double)tstop,
		      (double)dstop, fs, vs, g_spd.path);
}

static void spd_tick(const struct ctx *c)
{
	double t = mono();
	if (t >= g_spd.until || !c->player) {
		spd_end();
		return;
	}
	uintptr_t P = c->player, pm = rq(P + P_PM);
	float v[3] = { 0, 0, 0 }, gv[GV_N], sx, sy;
	if (pm)
		rvec(pm + PM_VEL, v, 3);
	uint8_t vm = pm ? rb(pm + PM_VELMODE) : 0, fsm = rb(P + P_FSM), fprev = rb(P + P_FSM + 1);
	int sup = g_fn_ok[FN_SUPPORTED] ? (int)(FN(FN_SUPPORTED, supported_fn)(P) & 0xff) : -1;
	graph_vals(c, gv);
	int hs_stick = probe_stick(&sx, &sy);
	float smag = hs_stick ? sqrtf(sx * sx + sy * sy) : (gv[0] == gv[0] ? gv[0] : 0); /* else RawMoveMag */
	uintptr_t set;
	uint64_t sig;
	int tn = tags_read(c, &set, &sig);
	char tags[600] = "";
	int tch = sig != g_spd.tags || !g_spd.n;
	if (tch)
		tags_names(set, tn, tags, sizeof tags);
	g_spd.tags = sig;
	g_spd.fsms |= fsm < 32 ? 1u << fsm : 0;
	g_spd.vms |= vm < 2 ? 1u << vm : 0;
	float tt = (float)(t - g_spd.t0), gsp = g_root.dt > 1e-4 ? sqrtf(g_rd.gsum[0] * g_rd.gsum[0] + g_rd.gsum[2] * g_rd.gsum[2]) /
								 (float)g_root.dt : 0;
	int lm = atomic_load(&g_loco.mode);
	float ask = lm ? sqrtf(g_loco.vw[0] * g_loco.vw[0] + g_loco.vw[2] * g_loco.vw[2]) : 0;
	if (g_spd.f)
		fprintf(g_spd.f, "%.4f\t%.4f\t%.2f\t%.2f\t%.2f\t%.1f\t%.1f\t%.2f\t%.1f\t%.1f\t%.1f\t%.1f\t%u\t%u\t%u\t%d\t%.3f\t%.1f\t%.1f\t"
			"%.2f\t%.3f\t%.3f\t%d\t%.1f\t%d\t%.3f\t%.3f\t%.3f\t%.1f\t%d\t%.3f\t%.3f\t%s\n", tt, g_root.dt, g_root.pos[0],
			g_root.pos[1], g_root.pos[2], (double)g_root.hs, (double)g_root.vs, (double)(g_root.yaw * RAD2DEG), v[0], v[1], v[2],
			sqrtf(v[0] * v[0] + v[2] * v[2]), vm, fsm, fprev, sup, gv[0], gv[1], gv[2], gv[3], sx, sy, lm, ask, g_rd.gcnt,
			g_rd.gsum[0], g_rd.gsum[1], g_rd.gsum[2], gsp, g_rd.rcnt, g_rd.ours[0], g_rd.ours[2], tch ? tags : "");
	if (g_spd.n < SPD_N) {
		g_spd.r[g_spd.n].t = tt;
		g_spd.r[g_spd.n].dt = (float)g_root.dt;
		g_spd.r[g_spd.n].hs = g_root.hs;
		g_spd.r[g_spd.n].stick = hs_stick ? smag : -1.0f;
		g_spd.r[g_spd.n].raw = gv[0];
		g_spd.n++;
	}
	if (tch && g_spd.n > 1 && rl_ok(RL_SPEED))
		logf_("SPEED seq=%u +%.3f tags: %s", g_spd.seq, (double)tt, tn ? tags : "(none)");
	if (t >= g_spd.next_line && rl_ok(RL_SPEED)) {
		g_spd.next_line = t + 0.5;
		logf_("SPEED seq=%u +%.3f root %.1f cm/s (v %.1f) pm |v| %.1f (%.1f %.1f %.1f) velmode %u fsm %u/%u sup %d RawMoveMag %.3f "
		      "Game_Velocity %.1f smoothed %.1f DeltaYaw %.2f stick %.2f%s game delta %.1f cm/s (%d calls)%s", g_spd.seq,
		      (double)tt, (double)g_root.hs, (double)g_root.vs, sqrtf(v[0] * v[0] + v[2] * v[2]), v[0], v[1], v[2], vm, fsm, fprev,
		      sup, gv[0], gv[1], gv[2], gv[3], (double)smag, hs_stick ? "" : " (RawMoveMag)", (double)gsp, g_rd.gcnt,
		      lm ? " loco on" : "");
		if (g_spd.f)
			fflush(g_spd.f);
	}
}

/* ---- pmrec: Client.PlayerMovement's record and its console names (design-locomotion.md 2.2) ---- */

#define PMR_N 30
static struct {
	int nset;
	struct { int idx; char cvar[160], old[64]; } set[8];
	int base_on;
	float base_old;
	uintptr_t base_pm;
	uint64_t later_tick;
	unsigned later_seq;
	int later_idx;
} g_pmr;

/* attribute I of the record: the descriptor's fast path (*(D+0x18) >= 2: entry *(rec+8) + 16 * *(u8*)(D+0x28)) or
 * the hash search; *fast = 1 if the fast path was taken; the entry's 16 bytes in e */
static int pmr_entry(uintptr_t rec, int i, unsigned char e[16], int *fast)
{
	uintptr_t D = rq(G.sch_pm) + 0x30u * (unsigned)i;
	*fast = 0;
	if (!rec || !rq(G.sch_pm))
		return -1;
	if ((int)r32(D + 0x18) >= 2) {
		*fast = 1;
		return rd(rq(rec + 8) + 16u * rb(D + 0x28), e, 16);
	}
	return db_entry(rec, r32(D + 8), e);
}

/* the value of attribute I, formatted ("-" = none); *console = the same as a console argument (floats, ints, bools) */
static const char *pmr_value(uintptr_t rec, int i, char *out, size_t cap, char *console, size_t ccap, int *type)
{
	unsigned char e[16];
	int fast;
	uint16_t cnt, vi;
	snprintf(out, cap, "-");
	if (console)
		console[0] = 0;
	*type = 0;
	if (pmr_entry(rec, i, e, &fast))
		return out;
	memcpy(&cnt, e + 0xc, 2);
	memcpy(&vi, e + 0xe, 2);
	*type = e[0xa] & 0x3f;
	uintptr_t vals = rq(rec + 0x10);
	if (!cnt || !vals) {
		snprintf(out, cap, "(type %d, no value)%s", *type, fast ? " fast" : "");
		return out;
	}
	uint64_t v[4] = { 0 };
	int k = cnt < 4 ? cnt : 4, o = 0;
	if (*type == 1) {
		v[0] = (r32(vals + (uintptr_t)(vi >> 5) * 8) >> (vi & 31)) & 1u;
		k = 1;
	} else if (rd(vals + (uintptr_t)vi * 8, v, (size_t)k * 8)) {
		return out;
	}
	for (int j = 0; j < k && (size_t)o + 40 < cap; j++) {
		uint32_t u = (uint32_t)v[j];
		float f;
		memcpy(&f, &u, 4);
		char nm[96];
		switch (*type) {
		case 1: o += cfmt(out + o, cap - (size_t)o, "%u", (unsigned)v[0]); break;
		case 2: o += cfmt(out + o, cap - (size_t)o, "%s%g", j ? "," : "", (double)f); break;
		case 3: o += cfmt(out + o, cap - (size_t)o, "%s%d (%.3f s)", j ? "," : "", (int)u, (int)u * 0.001); break;
		case 4: o += cfmt(out + o, cap - (size_t)o, "%s\"%s\"", j ? "," : "", rstr((uintptr_t)v[j], nm, sizeof nm)); break;
		case 0xa: o += cfmt(out + o, cap - (size_t)o, "%s'%s'", j ? "," : "", rec_name((uintptr_t)v[j], nm, sizeof nm)); break;
		default: o += cfmt(out + o, cap - (size_t)o, "%s%#llx", j ? "," : "", (unsigned long long)v[j]); break;
		}
	}
	if (cnt > k)
		cfmt(out + o, cap - (size_t)o, ",..(%u)", cnt);
	if (console && k == 1) {
		uint32_t u = (uint32_t)v[0];
		float f;
		memcpy(&f, &u, 4);
		if (*type == 2)
			cfmt(console, ccap, "%g", (double)f);
		else if (*type == 1 || *type == 3)
			cfmt(console, ccap, "%d", (int)u);
	}
	return out;
}

/* the first of PlayerMovement.NAME, Client.PlayerMovement.NAME, NAME the console find knows; 0 = none (*avail = 0:
 * the find is not usable) */
static uintptr_t pmr_cvar(const char *name, char *out, size_t cap, int *avail)
{
	static const char *const pre[3] = { "PlayerMovement.", "Client.PlayerMovement.", "" };
	out[0] = 0;
	for (int k = 0; k < 3; k++) {
		char full[160];
		snprintf(full, sizeof full, "%s%s", pre[k], name);
		uintptr_t p = con_find(full, avail);
		if (!*avail)
			return 0;
		if (p) {
			snprintf(out, cap, "%s", full);
			return p;
		}
	}
	return 0;
}

static int pmr_index(const char *name)
{
	char nm[96];
	for (int i = 0; i < PMR_N; i++)
		if (!strcasecmp(sch_name(G.sch_pm, i, nm, sizeof nm), name))
			return i;
	return -1;
}

static void pmr_caches(const struct ctx *c, unsigned seq, const char *what)
{
	uintptr_t pm = c->player ? rq(c->player + P_PM) : 0, pc = pm ? rq(pm + PM_PHYS) : 0;
	logf_("PMREC seq=%u %s: pm=%#lx pm+0x32c BaseSpeedMultiplier=%g pm+0x450 PlayerYawInterpolateTime=%g s pm+0x454 "
	      "FallMinHeight=%g cm; the proxy %#lx: gravity pc+0x58=%g on pc+0x60=%u; velmode %u", seq, what, (unsigned long)pm,
	      pm ? (double)rf(pm + PM_BASESPEED) : 0.0, pm ? (double)rf(pm + PM_YAWTIME) : 0.0,
	      pm ? (double)rf(pm + PM_FALLMIN) : 0.0, (unsigned long)pc, pc ? (double)rf(pc + PCH_GRAVITY) : 0.0,
	      pc ? rb(pc + PCH_GRAVON) : 0u, pm ? rb(pm + PM_VELMODE) : 0u);
}

/* pmrec | pmrec set ATTR V | pmrec restore | pmrec base V | pmrec base off */
static void w_pmrec(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	uintptr_t pm = c->player ? rq(c->player + P_PM) : 0, rec = pm && rq(pm) == G.vt_pm ? rq(pm + PM_REC) : 0;
	const char *sub = argc > 1 ? argv[1] : "";
	char rn[128], val[300], con[64], cv[160];
	int type, avail = 0;
	if (!strcmp(sub, "base")) {
		if (argc > 2 && !strcmp(argv[2], "off")) {
			int was = g_pmr.base_on && g_pmr.base_pm == pm && pm;
			if (was)
				memcpy((void *)(pm + PM_BASESPEED), &g_pmr.base_old, 4);
			g_pmr.base_on = 0;
			logf_("DONE seq=%u pmrec base off: %s", seq, was ? "pm+0x32c back" : "nothing to restore");
			pmr_caches(c, seq, "after base off");
			return;
		}
		if (argc < 3 || !is_num(argv[2]) || !pm || rq(pm) != G.vt_pm) {
			logf_("FAIL seq=%u pmrec base: want V (0.1..5) and the player's PlayerMovement (%#lx)", seq, (unsigned long)pm);
			return;
		}
		float v = (float)num(argv[2], 1);
		v = v < 0.1f ? 0.1f : v > 5 ? 5 : v;
		if (!g_pmr.base_on || g_pmr.base_pm != pm)
			g_pmr.base_old = rf(pm + PM_BASESPEED);
		g_pmr.base_on = 1;
		g_pmr.base_pm = pm;
		memcpy((void *)(pm + PM_BASESPEED), &v, 4);
		logf_("DONE seq=%u pmrec base %g: pm+0x32c %g -> %g (no reader is known: a speed change = a reader)", seq, (double)v,
		      (double)g_pmr.base_old, (double)rf(pm + PM_BASESPEED));
		return;
	}
	if (!strcmp(sub, "restore")) {
		int n = 0;
		for (int i = g_pmr.nset - 1; i >= 0; i--) {
			char line[256];
			snprintf(line, sizeof line, "%s %s", g_pmr.set[i].cvar, g_pmr.set[i].old);
			w_exec(c, seq, line);
			n++;
		}
		g_pmr.nset = 0;
		logf_("DONE seq=%u pmrec restore: %d console value(s) set back", seq, n);
		pmr_caches(c, seq, "after restore");
		return;
	}
	if (!strcmp(sub, "set")) {
		int i = argc > 3 ? pmr_index(argv[2]) : -1;
		if (i < 0 || !is_num(argv[3])) {
			logf_("FAIL seq=%u pmrec set: want ATTR V with ATTR one of the 30 Client.PlayerMovement names", seq);
			return;
		}
		char nm[96];
		sch_name(G.sch_pm, i, nm, sizeof nm);
		if (!pmr_cvar(nm, cv, sizeof cv, &avail)) {
			logf_("FAIL seq=%u pmrec set %s: %s", seq, nm, avail ? "no console name (PlayerMovement.X, "
			      "Client.PlayerMovement.X, X) is known to the find" : "the console find is not usable");
			return;
		}
		pmr_value(rec, i, val, sizeof val, con, sizeof con, &type);
		int k;
		for (k = 0; k < g_pmr.nset && g_pmr.set[k].idx != i; k++)
			;
		if (k == g_pmr.nset && k < 8 && con[0]) { /* the value before the first set, for restore */
			g_pmr.set[k].idx = i;
			snprintf(g_pmr.set[k].cvar, sizeof g_pmr.set[k].cvar, "%s", cv);
			snprintf(g_pmr.set[k].old, sizeof g_pmr.set[k].old, "%s", con);
			g_pmr.nset++;
		}
		logf_("PMREC seq=%u set %d '%s' through '%s' to %s: the record now %s", seq, i, nm, cv, argv[3], val);
		char line[256];
		snprintf(line, sizeof line, "%s %s", cv, argv[3]);
		w_exec(c, seq, line);
		pmr_value(rec, i, val, sizeof val, NULL, 0, &type);
		logf_("PMREC seq=%u set %d '%s': the record right after %s (3 ticks later: one more line)", seq, i, nm, val);
		pmr_caches(c, seq, "right after set");
		g_pmr.later_tick = g_ticks_all + 3;
		g_pmr.later_seq = seq;
		g_pmr.later_idx = i;
		return;
	}
	/* the dump (read-only) */
	uint32_t n = r32(G.sch_pm_n);
	uintptr_t arr = rq(G.sch_pm);
	logf_("PMREC seq=%u record pm+0x448 = %#lx '%s' (flags %#x, %u attributes); schema %#lx: %u descriptors", seq,
	      (unsigned long)rec, rec_name(rec, rn, sizeof rn), rec ? rb(rec + 1) : 0u, rec ? rb(rec) : 0u, (unsigned long)arr, n);
	int named = 0;
	for (int i = 0; i < PMR_N && arr; i++) {
		char nm[96], fs[48];
		uintptr_t D = arr + 0x30u * (unsigned)i;
		sch_name(G.sch_pm, i, nm, sizeof nm);
		pmr_value(rec, i, val, sizeof val, NULL, 0, &type);
		unsigned char e[16];
		int fast = 0;
		int ok = pmr_entry(rec, i, e, &fast) == 0;
		snprintf(fs, sizeof fs, "%s", (int)r32(D + 0x18) >= 2 ? "fast" : "search");
		uintptr_t prog = pmr_cvar(nm, cv, sizeof cv, &avail);
		char pd[300] = "";
		if (prog)
			con_desc(prog, pd, sizeof pd);
		named += prog != 0;
		logf_("PMREC %2d '%s' hash %#x state %d index %u (%s%s) type %d = %s; console %s%s%s", i, nm, r32(D + 8),
		      (int)r32(D + 0x18), rb(D + 0x28), fs, ok ? "" : ", no entry", type, val,
		      !avail ? "(find unavailable)" : prog ? cv : "none of PlayerMovement./Client.PlayerMovement./bare", prog ? " -> " : "",
		      pd);
	}
	pmr_caches(c, seq, "caches");
	logf_("DONE seq=%u pmrec: %d attributes, %d with a console name", seq, arr ? PMR_N : 0, named);
}

static void pmr_tick(const struct ctx *c)
{
	if (!g_pmr.later_tick || g_ticks_all < g_pmr.later_tick)
		return;
	g_pmr.later_tick = 0;
	uintptr_t pm = c->player ? rq(c->player + P_PM) : 0, rec = pm ? rq(pm + PM_REC) : 0;
	char nm[96], val[300];
	int type;
	pmr_value(rec, g_pmr.later_idx, val, sizeof val, NULL, 0, &type);
	logf_("PMREC seq=%u set %d '%s': the record 3 ticks later %s (pm+0x448 %#lx)", g_pmr.later_seq, g_pmr.later_idx,
	      sch_name(G.sch_pm, g_pmr.later_idx, nm, sizeof nm), val, (unsigned long)rec);
	pmr_caches(c, g_pmr.later_seq, "3 ticks after set");
}

/* ---- loco: our velocity instead of the root motion (design-locomotion.md 2.6) ---- */

static void loco_end(const char *why)
{
	if (!atomic_load(&g_loco.mode))
		return;
	atomic_store_explicit(&g_loco.mode, LOCO_OFF, memory_order_release);
	char fs[80] = "";
	int o = 0;
	for (int k = 0; k < 32; k++)
		if (g_loco.fsms >> k & 1)
			o += cfmt(fs + o, sizeof fs - (size_t)o, "%s%d", o ? "," : "", k);
	float mh = g_loco.nmean ? (float)(g_loco.sum_hs / g_loco.nmean) : 0, ma = g_loco.nmean ? (float)(g_loco.sum_ask / g_loco.nmean) : 0;
	logf_("LOCO seq=%u end (%s, %s) after %.2f s: vf_11 calls %llu, replaced %llu, skipped fsm %llu / mount-attach %llu / sync "
	      "%llu; the translation lock was set in %llu, the rotation lock in %llu, an attack ran in %llu; dt from the object "
	      "timer %llu, the game timer %llu, the wall %llu (last %.4f s); %d ticks: the root %.1f cm/s on average vs %.1f asked "
	      "(%.3f), max %.1f, first at >= 90 %% of the asked speed: tick %d; fsm {%s}", g_loco.seq, g_loco.who, why,
	      mono() - g_loco.t0, (unsigned long long)atomic_load(&g_loco.calls), (unsigned long long)atomic_load(&g_loco.replaced),
	      (unsigned long long)atomic_load(&g_loco.sk_fsm), (unsigned long long)atomic_load(&g_loco.sk_mount),
	      (unsigned long long)atomic_load(&g_loco.sk_sync), (unsigned long long)atomic_load(&g_loco.lock_t),
	      (unsigned long long)atomic_load(&g_loco.lock_r), (unsigned long long)atomic_load(&g_loco.attack),
	      (unsigned long long)atomic_load(&g_loco.dt_obj), (unsigned long long)atomic_load(&g_loco.dt_game),
	      (unsigned long long)atomic_load(&g_loco.dt_wall), (double)g_loco.last_dt, g_loco.ticks, (double)mh, (double)ma,
	      ma > 0 ? (double)(mh / ma) : 0.0, (double)g_loco.max_hs, g_loco.first90, fs);
	g_loco.stop_seq = g_loco.seq; /* the stop: the root's speed on the next 3 ticks */
	g_loco.stop_left = 3;
	g_loco.stop_n = 0;
	snprintf(g_loco.stop_what, sizeof g_loco.stop_what, "after the end");
}

/* start loco on the player (mode const: vw fixed; stick: vw set every probe tick) */
static int loco_begin(const struct ctx *c, unsigned seq, int mode, const float vw[3], double secs, const char *who,
		      const char *frame)
{
	if (!g_h[HK_ROOTDELTA].installed || !c->player) {
		logf_("FAIL seq=%u %s: the hook P_ROOTDELTA is %s (player %#lx)", seq, who, g_h[HK_ROOTDELTA].installed ? "on" :
		      "NOT installed", (unsigned long)c->player);
		return -1;
	}
	if (atomic_load(&g_loco.mode))
		loco_end("replaced");
	g_loco.seq = seq;
	g_loco.t0 = mono();
	g_loco.until = g_loco.t0 + secs;
	g_loco.player = c->player;
	memcpy(g_loco.vw, vw, sizeof g_loco.vw);
	snprintf(g_loco.who, sizeof g_loco.who, "%s", who);
	snprintf(g_loco.frame, sizeof g_loco.frame, "%s", frame);
	atomic_store(&g_loco.calls, 0);
	atomic_store(&g_loco.replaced, 0);
	atomic_store(&g_loco.sk_fsm, 0);
	atomic_store(&g_loco.sk_mount, 0);
	atomic_store(&g_loco.sk_sync, 0);
	atomic_store(&g_loco.lock_t, 0);
	atomic_store(&g_loco.lock_r, 0);
	atomic_store(&g_loco.attack, 0);
	atomic_store(&g_loco.dt_obj, 0);
	atomic_store(&g_loco.dt_game, 0);
	atomic_store(&g_loco.dt_wall, 0);
	g_loco.last_wall = 0;
	g_loco.last_line = 0;
	g_loco.ticks = g_loco.nmean = g_loco.lines = 0;
	g_loco.first90 = -1;
	g_loco.sum_hs = g_loco.sum_ask = 0;
	g_loco.max_hs = g_loco.last_ask = 0;
	g_loco.fsms = 0;
	g_loco.stop_left = 0;
	uintptr_t pm = rq(c->player + P_PM);
	atomic_store_explicit(&g_loco.mode, mode, memory_order_release);
	logf_("LOCO seq=%u %s %s: world (%.1f 0 %.1f) cm/s |v| %.1f%s for %.1f s, from %s; fsm %u velmode %u; P_PMROOT %s (the "
	      "transform from %s) t=%.6f", seq, who, mode == LOCO_STICK ? "stick" : "const", vw[0], vw[2],
	      sqrtf(vw[0] * vw[0] + vw[2] * vw[2]), mode == LOCO_STICK ? " (now; the stick decides)" : "", secs, frame,
	      rb(c->player + P_FSM), pm ? rb(pm + PM_VELMODE) : 0u, g_h[HK_PMROOT].installed ? "on" : "off",
	      g_h[HK_PMROOT].installed ? "vf_11's own" : "the root object", mono());
	return 0;
}

/* loco const VX VZ SECS [world] | loco stick SECS [V] | loco off */
static void w_loco(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	const char *sub = argc > 1 ? argv[1] : "";
	if (!strcmp(sub, "off")) {
		int was = atomic_load(&g_loco.mode) != 0;
		loco_end("off");
		logf_("DONE seq=%u loco off%s", seq, was ? "" : " (it was off)");
		return;
	}
	float f[3], r[3], vw[3] = { 0, 0, 0 };
	char frame[64];
	if (!strcmp(sub, "const") && argc > 4 && is_num(argv[2]) && is_num(argv[3]) && is_num(argv[4])) {
		float vx = (float)num(argv[2], 0), vz = (float)num(argv[3], 0);
		double secs = num(argv[4], 3);
		if (!(fabsf(vx) <= 2000 && fabsf(vz) <= 2000) || !(secs >= 0.1 && secs <= 20)) {
			logf_("FAIL seq=%u loco const: |VX|,|VZ| <= 2000 cm/s and SECS 0.1..20", seq);
			return;
		}
		head_frame(argc > 5 && !strcmp(argv[5], "world"), f, r, frame, sizeof frame);
		for (int k = 0; k < 3; k++)
			vw[k] = vx * r[k] + vz * f[k];
		if (loco_begin(c, seq, LOCO_CONST, vw, secs, "loco", frame) == 0)
			logf_("DONE seq=%u loco const (%g %g) for %.1f s", seq, (double)vx, (double)vz, secs);
		return;
	}
	if (!strcmp(sub, "stick") && argc > 2 && is_num(argv[2])) {
		double secs = num(argv[2], 5);
		float v = (float)num(argc > 3 ? argv[3] : NULL, 500);
		if (!(secs >= 0.1 && secs <= 20) || !(v > 0 && v <= 2000)) {
			logf_("FAIL seq=%u loco stick: SECS 0.1..20, V 1..2000 cm/s", seq);
			return;
		}
		g_loco.vstick = v;
		char fr[64];
		snprintf(fr, sizeof fr, "the probe's stick through the camera's yaw at %.0f cm/s", (double)v);
		if (loco_begin(c, seq, LOCO_STICK, vw, secs, "loco", fr) == 0)
			logf_("DONE seq=%u loco stick for %.1f s at %.0f cm/s", seq, secs, (double)v);
		return;
	}
	logf_("FAIL seq=%u loco: want const VX VZ SECS [world], stick SECS [V] or off", seq);
}

static void loco_tick(const struct ctx *c)
{
	double t = mono();
	if (g_loco.stop_left > 0) { /* the stop after the end (or a stick release) */
		g_loco.stop_hs[g_loco.stop_n++] = g_root.hs;
		if (--g_loco.stop_left == 0)
			logf_("LOCO seq=%u stop %s: the root %.1f, %.1f, %.1f cm/s on the next 3 ticks", g_loco.stop_seq, g_loco.stop_what,
			      (double)g_loco.stop_hs[0], (double)g_loco.stop_hs[1], (double)g_loco.stop_hs[2]);
	}
	int mode = atomic_load(&g_loco.mode);
	if (!mode)
		return;
	if (t >= g_loco.until || !c->player || c->player != g_loco.player) {
		loco_end(c->player != g_loco.player ? "the player changed" : "time");
		return;
	}
	if (mode == LOCO_STICK) {
		float sx, sy, cy, f[3], r[3], v[3];
		if (probe_stick(&sx, &sy) && cam_yaw(c, &cy) == 0) {
			if (g_stick_mirror)
				sx = -sx;
			yaw_dir(cy, f, r);
			for (int k = 0; k < 3; k++)
				v[k] = (sx * r[k] + sy * f[k]) * g_loco.vstick;
		} else {
			v[0] = v[1] = v[2] = 0;
		}
		memcpy(g_loco.vw, v, sizeof v);
	}
	float ask = sqrtf(g_loco.vw[0] * g_loco.vw[0] + g_loco.vw[2] * g_loco.vw[2]);
	if (g_loco.last_ask > 1 && ask < 1 && !g_loco.stop_left) { /* the stick let go inside the window */
		g_loco.stop_seq = g_loco.seq;
		g_loco.stop_left = 3;
		g_loco.stop_n = 0;
		snprintf(g_loco.stop_what, sizeof g_loco.stop_what, "at the stick's release");
	}
	g_loco.last_ask = ask;
	uint8_t fsm = rb(c->player + P_FSM);
	g_loco.fsms |= fsm < 32 ? 1u << fsm : 0;
	if (g_loco.ticks++ > 0 && ask > 1) { /* the first tick is the start's (no replaced call yet) */
		g_loco.sum_hs += g_root.hs;
		g_loco.sum_ask += ask;
		g_loco.nmean++;
		if (g_loco.first90 < 0 && g_root.hs >= 0.9f * ask)
			g_loco.first90 = g_loco.ticks - 1;
	}
	if (g_root.hs > g_loco.max_hs && g_loco.ticks > 1)
		g_loco.max_hs = g_root.hs;
	if ((g_loco.lines < 8 || t - g_loco.last_line >= 0.25) && rl_ok(RL_LOCO)) {
		uintptr_t pm = rq(c->player + P_PM);
		float v[3] = { 0, 0, 0 };
		if (pm)
			rvec(pm + PM_VEL, v, 3);
		g_loco.lines++;
		g_loco.last_line = t;
		logf_("LOCO seq=%u +%.3f tick %d: asked %.1f (%.1f %.1f) root %.1f cm/s (v %.1f); game delta (%.2f %.2f %.2f) -> ours "
		      "(%.2f %.2f %.2f) dt %.4f; replaced %llu of %llu, skipped fsm %llu, locks %llu; fsm %u velmode %u pm v (%.1f %.1f "
		      "%.1f)", g_loco.seq, t - g_loco.t0, g_loco.ticks - 1, (double)ask, g_loco.vw[0], g_loco.vw[2], (double)g_root.hs,
		      (double)g_root.vs, g_rd.game[0], g_rd.game[1], g_rd.game[2], g_rd.ours[0], g_rd.ours[1], g_rd.ours[2],
		      (double)g_loco.last_dt, (unsigned long long)atomic_load(&g_loco.replaced),
		      (unsigned long long)atomic_load(&g_loco.calls), (unsigned long long)atomic_load(&g_loco.sk_fsm),
		      (unsigned long long)atomic_load(&g_loco.lock_t), fsm, pm ? rb(pm + PM_VELMODE) : 0u, v[0], v[1], v[2]);
	}
}

/* ---- fling: a launch through momentum mode (design-locomotion.md 2.7) ---- */

static struct {
	int on, lifted, ours_back, nf, nvm;
	unsigned seq;
	double t0, t_lift, t_land, t_apex, next_line;
	float p0[3], v0[3], apex, maxh;
	uintptr_t player, pm, pc;
	uint8_t fsm, vm, sup;
	char fsms[96], vms[48];
	FILE *f;
	char path[1200];
} g_fl;

static void fl_end(const char *why)
{
	g_fl.on = 0;
	if (g_fl.f)
		fclose(g_fl.f);
	g_fl.f = NULL;
	float d = hdist(g_root.pos, g_fl.p0);
	char land[48];
	if (g_fl.t_land > 0)
		cfmt(land, sizeof land, "at +%.3f s", g_fl.t_land);
	else
		snprintf(land, sizeof land, "not seen");
	logf_("FLING seq=%u end (%s) after %.2f s: %s; apex %.1f cm at +%.3f s; landing %s; %.1f cm away horizontally, %+.1f cm "
	      "in height; fsm path %s; velmode path %s; root-motion mode back by %s; file=%s", g_fl.seq, why, mono() - g_fl.t0,
	      g_fl.lifted ? "lifted off" : "NO liftoff", (double)g_fl.apex, g_fl.t_apex, land, (double)d,
	      (double)(g_root.pos[1] - g_fl.p0[1]), g_fl.fsms, g_fl.vms,
	      g_fl.ours_back ? "the probe (the graph did not)" : g_fl.vm ? "the game" : "nobody (still momentum)", g_fl.path);
	if (g_fl.t_land > 0)
		logf_("FLING seq=%u flight %.3f s (liftoff +%.3f, landing +%.3f)", g_fl.seq, g_fl.t_land - g_fl.t_lift, g_fl.t_lift,
		      g_fl.t_land);
}

/* fling VX VY VZ [world] */
static void w_fling(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	if (argc < 4 || !is_num(argv[1]) || !is_num(argv[2]) || !is_num(argv[3])) {
		logf_("FAIL seq=%u fling: want VX VY VZ (cm/s; x right, y up, z forward of the heading) [world]", seq);
		return;
	}
	float vx = (float)num(argv[1], 0), vy = (float)num(argv[2], 0), vz = (float)num(argv[3], 0);
	if (!(fabsf(vx) <= 1500 && fabsf(vz) <= 1500 && vy >= 0 && vy <= 1500)) {
		logf_("FAIL seq=%u fling: |VX|,|VZ| <= 1500 cm/s and 0 <= VY <= 1500", seq);
		return;
	}
	uintptr_t P = c->player, pm = P ? rq(P + P_PM) : 0, pc = pm ? rq(pm + PM_PHYS) : 0;
	uint8_t fsm = P ? rb(P + P_FSM) : 0;
	if (!pm || rq(pm) != G.vt_pm || !pc || !g_fn_ok[FN_VELMODE] || (fsm != 6 && fsm != 7) || rq(P + P_MOUNT) ||
	    rb(pm + PM_ATTACHED) || g_fl.on) {
		logf_("FAIL seq=%u fling: needs the player's PlayerMovement %#lx and its proxy %#lx, FUN_020c6ff0 (%d), fsm 6/7 (%u), "
		      "not mounted or attached, no fling on (%d)", seq, (unsigned long)pm, (unsigned long)pc, g_fn_ok[FN_VELMODE], fsm,
		      g_fl.on);
		return;
	}
	if (atomic_load(&g_loco.mode))
		loco_end("fling");
	float f[3], r[3], v[3];
	char frame[64];
	head_frame(argc > 4 && !strcmp(argv[4], "world"), f, r, frame, sizeof frame);
	for (int k = 0; k < 3; k++)
		v[k] = vx * r[k] + vz * f[k];
	v[1] = vy;
	float vb[3];
	rvec(pm + PM_VEL, vb, 3);
	uint8_t vm0 = rb(pm + PM_VELMODE), g0 = rb(pc + PCH_GRAVON);
	FN(FN_VELMODE, velmode_fn)(pm, 0);
	memcpy((void *)(pm + PM_VEL), v, sizeof v);
	*(uint8_t *)(pc + PCH_GRAVON) = 1;
	memset(&g_fl, 0, sizeof g_fl);
	g_fl.on = 1;
	g_fl.seq = seq;
	g_fl.t0 = mono();
	g_fl.player = P;
	g_fl.pm = pm;
	g_fl.pc = pc;
	g_fl.fsm = fsm;
	g_fl.vm = 0;
	g_fl.sup = 1;
	memcpy(g_fl.p0, g_root.pos, sizeof g_fl.p0);
	memcpy(g_fl.v0, v, sizeof v);
	snprintf(g_fl.fsms, sizeof g_fl.fsms, "%u", fsm);
	snprintf(g_fl.vms, sizeof g_fl.vms, "%u>0", vm0);
	g_fl.f = out_file("fling", seq, g_fl.path, sizeof g_fl.path);
	if (g_fl.f)
		fprintf(g_fl.f, "t\tx\ty\tz\theight\ths\tvs\tvx\tvy\tvz\tvelmode\tfsm\tsupported\tgravity_on\n");
	logf_("FLING seq=%u (%g %g %g) in %s = world (%.1f %.1f %.1f) cm/s: velmode %u -> %u, pm v (%.1f %.1f %.1f) -> (%.1f %.1f "
	      "%.1f), gravity on %u -> %u (g %g), fsm %u t=%.6f", seq, (double)vx, (double)vy, (double)vz, frame, v[0], v[1], v[2],
	      vm0, rb(pm + PM_VELMODE), vb[0], vb[1], vb[2], rf(pm + PM_VEL), rf(pm + PM_VEL + 4), rf(pm + PM_VEL + 8), g0,
	      rb(pc + PCH_GRAVON), (double)rf(pc + PCH_GRAVITY), fsm, mono());
	logf_("DONE seq=%u fling: launched (the arc per tick, then the landing)", seq);
}

static void fl_tick(const struct ctx *c)
{
	double t = mono(), tt = t - g_fl.t0;
	if (!c->player || c->player != g_fl.player) {
		fl_end("the player changed");
		return;
	}
	uintptr_t P = c->player, pm = g_fl.pm, pc = g_fl.pc;
	uint8_t fsm = rb(P + P_FSM), vm = rb(pm + PM_VELMODE), gon = rb(pc + PCH_GRAVON);
	int sup = g_fn_ok[FN_SUPPORTED] ? (int)(FN(FN_SUPPORTED, supported_fn)(P) & 0xff) : -1;
	float v[3], h = g_root.pos[1] - g_fl.p0[1];
	rvec(pm + PM_VEL, v, 3);
	if (g_fl.f)
		fprintf(g_fl.f, "%.4f\t%.2f\t%.2f\t%.2f\t%.2f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%u\t%u\t%d\t%u\n", tt, g_root.pos[0],
			g_root.pos[1], g_root.pos[2], h, (double)g_root.hs, (double)g_root.vs, v[0], v[1], v[2], vm, fsm, sup, gon);
	if (h > g_fl.apex) {
		g_fl.apex = h;
		g_fl.t_apex = tt;
	}
	if (!g_fl.lifted && (sup == 0 || fsm == 8 || h > 5.0f)) {
		g_fl.lifted = 1;
		g_fl.t_lift = tt;
	}
	int changed = fsm != g_fl.fsm || vm != g_fl.vm || (uint8_t)sup != g_fl.sup;
	if (fsm != g_fl.fsm) {
		size_t l = strlen(g_fl.fsms);
		cfmt(g_fl.fsms + l, sizeof g_fl.fsms - l, ">%u", fsm);
	}
	if (vm != g_fl.vm) {
		size_t l = strlen(g_fl.vms);
		cfmt(g_fl.vms + l, sizeof g_fl.vms - l, ">%u", vm);
	}
	g_fl.fsm = fsm, g_fl.vm = vm, g_fl.sup = (uint8_t)sup;
	if ((changed || t >= g_fl.next_line) && rl_ok(RL_LOCO)) {
		g_fl.next_line = t + 0.2;
		logf_("FLING seq=%u +%.3f height %.1f root %.1f/%.1f cm/s pm v (%.1f %.1f %.1f) velmode %u fsm %u supported %d gravity %u",
		      g_fl.seq, tt, (double)h, (double)g_root.hs, (double)g_root.vs, v[0], v[1], v[2], vm, fsm, sup, gon);
	}
	if (g_fl.lifted && g_fl.t_land <= 0 && sup == 1 && tt - g_fl.t_lift > 0.05)
		g_fl.t_land = tt;
	if (g_fl.t_land > 0 && tt - g_fl.t_land >= 0.4 && !vm && (fsm == 6 || fsm == 7) && !g_fl.ours_back) {
		FN(FN_VELMODE, velmode_fn)(pm, 1); /* the graph never called it a fall: our own return */
		g_fl.ours_back = 1;
		g_fl.vm = rb(pm + PM_VELMODE);
		logf_("FLING seq=%u +%.3f root-motion mode back by the probe (FUN_020c6ff0(pm, 1)): velmode %u", g_fl.seq, tt, g_fl.vm);
	}
	if (g_fl.t_land > 0 && tt - g_fl.t_land >= 0.6) {
		fl_end("landed");
		return;
	}
	if (!g_fl.lifted && tt > 1.0) {
		if (!vm && (fsm == 6 || fsm == 7)) {
			FN(FN_VELMODE, velmode_fn)(pm, 1);
			g_fl.ours_back = 1;
			g_fl.vm = rb(pm + PM_VELMODE);
		}
		fl_end("no liftoff in 1 s");
		return;
	}
	if (tt > 5.0) {
		if (!vm && (fsm == 6 || fsm == 7)) {
			FN(FN_VELMODE, velmode_fn)(pm, 1);
			g_fl.ours_back = 1;
			g_fl.vm = rb(pm + PM_VELMODE);
		}
		fl_end("5 s");
	}
}

/* ---- animrate: Talion's tracker rate during a move (re-combat-vr.md 2.1 recipe B; LR8) ---- */

static struct {
	int on, n, over, all;
	unsigned seq;
	double t0, until;
	float r, before, old[33];
	uint8_t trk[33];
	uintptr_t h;
	double sum;
	int ns;
} g_ar;

static void ar_end(const char *why)
{
	if (!g_ar.on)
		return;
	g_ar.on = 0;
	uintptr_t m = model_iface();
	int back = 0, left = 0;
	for (int i = 0; i < g_ar.n; i++) {
		float cur = -1;
		FN(FN_GETANIMRATE, getanimrate_fn)(m, g_ar.h, g_ar.trk[i], &cur);
		if (fabsf(cur - g_ar.r) < 1e-4f) {
			FN(FN_SETANIMRATE, setanimrate_fn)(m, g_ar.h, g_ar.trk[i], g_ar.old[i]);
			back++;
		} else {
			left++; /* the game set its own rate since: left alone */
		}
	}
	float mean = g_ar.ns ? (float)(g_ar.sum / g_ar.ns) : 0;
	logf_("ANIMRATE seq=%u end (%s) after %.2f s: %d trackers back to their rate, %d left as the game set them; the game wrote "
	      "the rate %d times (set again); the root %.1f cm/s during it (%d ticks after the first 0.2 s) vs %.1f in the second "
	      "before: x%.3f for rate %.2f", g_ar.seq, why, mono() - g_ar.t0, back, left, g_ar.over, (double)mean, g_ar.ns,
	      (double)g_ar.before, g_ar.before > 1 ? (double)(mean / g_ar.before) : 0.0, (double)g_ar.r);
}

/* animrate R [SECS] [all] | animrate off */
static void w_animrate(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "off")) {
		int was = g_ar.on;
		ar_end("off");
		logf_("DONE seq=%u animrate off%s", seq, was ? "" : " (it was off)");
		return;
	}
	uintptr_t h = char_obj(c), m = model_iface();
	if (argc < 2 || !is_num(argv[1]) || !h || !g_fn_ok[FN_GETCURANIM] || !g_fn_ok[FN_GETANIMRATE] || !g_fn_ok[FN_SETANIMRATE]) {
		logf_("FAIL seq=%u animrate: want R [SECS] [all] (object %#lx, functions %d%d%d)", seq, (unsigned long)h,
		      g_fn_ok[FN_GETCURANIM], g_fn_ok[FN_GETANIMRATE], g_fn_ok[FN_SETANIMRATE]);
		return;
	}
	float r = (float)num(argv[1], 1);
	double secs = 3;
	int all = 0;
	for (int i = 2; i < argc; i++) {
		if (!strcmp(argv[i], "all"))
			all = 1;
		else if (is_num(argv[i]))
			secs = num(argv[i], 3);
	}
	r = r < 0.05f ? 0.05f : r > 3 ? 3 : r;
	secs = secs < 0.2 ? 0.2 : secs > 20 ? 20 : secs;
	ar_end("replaced");
	memset(&g_ar, 0, sizeof g_ar);
	char list[400] = "";
	int o = 0;
	for (int i = 0; i < (all ? 33 : 1); i++) {
		uint32_t trk = i == 0 ? 0xff : (uint32_t)(i - 1), anim = 0xffffffffu;
		float old = 0;
		if (FN(FN_GETCURANIM, getcuranim_fn)(m, h, trk, &anim) & 0xff || anim == 0xffffffffu)
			continue;
		if (FN(FN_GETANIMRATE, getanimrate_fn)(m, h, trk, &old) & 0xff)
			continue;
		g_ar.trk[g_ar.n] = (uint8_t)trk;
		g_ar.old[g_ar.n++] = old;
		FN(FN_SETANIMRATE, setanimrate_fn)(m, h, trk, r);
		o += cfmt(list + o, sizeof list - (size_t)o, " %u(anim %u, was %g)", trk, anim, (double)old);
	}
	if (!g_ar.n) {
		logf_("FAIL seq=%u animrate: no playing tracker on %#lx%s", seq, (unsigned long)h, all ? "" : " (0xff; try `all`)");
		return;
	}
	int nb;
	g_ar.before = root_mean(mono() - 1.0, mono() + 1, &nb);
	g_ar.on = 1;
	g_ar.all = all;
	g_ar.seq = seq;
	g_ar.r = r;
	g_ar.h = h;
	g_ar.t0 = mono();
	g_ar.until = g_ar.t0 + secs;
	logf_("ANIMRATE seq=%u rate %.2f for %.1f s on %#lx: %d trackers:%s; the root %.1f cm/s in the second before (%d ticks)",
	      seq, (double)r, secs, (unsigned long)h, g_ar.n, list, (double)g_ar.before, nb);
	logf_("DONE seq=%u animrate %.2f", seq, (double)r);
}

static void ar_tick(const struct ctx *c)
{
	double t = mono();
	if (t >= g_ar.until || char_obj(c) != g_ar.h) {
		ar_end(char_obj(c) != g_ar.h ? "the player object changed" : "time");
		return;
	}
	uintptr_t m = model_iface();
	for (int i = 0; i < g_ar.n; i++) {
		float cur = -1;
		FN(FN_GETANIMRATE, getanimrate_fn)(m, g_ar.h, g_ar.trk[i], &cur);
		if (fabsf(cur - g_ar.r) > 1e-4f) {
			if (g_ar.over++ < 3 && rl_ok(RL_LOCO))
				logf_("ANIMRATE seq=%u +%.3f tracker %u: the game set rate %g (set back to %.2f)", g_ar.seq, t - g_ar.t0,
				      g_ar.trk[i], (double)cur, (double)g_ar.r);
			FN(FN_SETANIMRATE, setanimrate_fn)(m, g_ar.h, g_ar.trk[i], g_ar.r);
		}
	}
	if (t - g_ar.t0 >= 0.2) {
		g_ar.sum += g_root.hs;
		g_ar.ns++;
	}
}

/* ---- vaultlog: syncs, vaults and their paths (G14 read by polling; LR6, LR10) ---- */

static struct {
	int on, ep, eps, ticks;
	unsigned seq;
	double t0, until, ep_t0;
	FILE *f;
	char path[1200];
	uintptr_t atk, srec, partner_c;
	uint8_t fsm, col;
	float ep_p0[3], ep_last[3], ep_y0, ep_oy0, ep_path, ep_step;
	int ep_have_o, ep_coloff;
	char ep_names[300], ep_onames[300];
} g_vl;

static void vl_addname(char *list, size_t cap, const char *nm)
{
	if (!nm[0] || strstr(list, nm))
		return;
	size_t l = strlen(list);
	cfmt(list + l, cap - l, "%s%s", l ? "," : "", nm);
}

/* an episode's summary; doy = the partner's yaw change (deg), have = a partner seen at the start and now */
static void vl_ep_end(const char *why, float doy, int have)
{
	g_vl.ep = 0;
	g_vl.eps++;
	logf_("VAULT seq=%u episode %d end (%s): %.3f s; %s; root moved %.1f cm horizontally, %+.1f cm up, path %.1f cm, largest "
	      "step %.1f cm/tick; yaw %+.1f deg; partner yaw %+.1f deg%s, its attacks %s; the attack's EnableCollision "
	      "(Combat+0x56c) 0 seen: %s", g_vl.seq, g_vl.eps, why, mono() - g_vl.ep_t0,
	      g_vl.ep_names[0] ? g_vl.ep_names : "-", (double)hdist(g_root.pos, g_vl.ep_p0), (double)(g_root.pos[1] - g_vl.ep_p0[1]),
	      (double)g_vl.ep_path, (double)g_vl.ep_step, (double)(wrap_pi(g_root.yaw - g_vl.ep_y0) * RAD2DEG), (double)doy,
	      have ? "" : " (no partner)", g_vl.ep_onames[0] ? g_vl.ep_onames : "-", g_vl.ep_coloff ? "yes" : "no");
}

static void w_vaultlog(const struct ctx *c, unsigned seq, const char *arg)
{
	double secs = num(arg, 4);
	secs = secs < 0.5 ? 0.5 : secs > 30 ? 30 : secs;
	if (g_vl.on) { /* the window before ends here */
		if (g_vl.ep)
			vl_ep_end("a new vaultlog", 0, 0);
		if (g_vl.f)
			fclose(g_vl.f);
		logf_("VAULT seq=%u end (a new vaultlog): %d episodes, %d ticks; file=%s", g_vl.seq, g_vl.eps, g_vl.ticks, g_vl.path);
	}
	memset(&g_vl, 0, sizeof g_vl);
	g_vl.f = out_file("vault", seq, g_vl.path, sizeof g_vl.path);
	if (g_vl.f)
		fprintf(g_vl.f, "t\tx\ty\tz\tyaw\tfsm\tvelmode\tattack\tsync\tsync_target\tpartner\tox\toy\toz\toyaw\toattack\tcollide\n");
	g_vl.on = 1;
	g_vl.seq = seq;
	g_vl.t0 = mono();
	g_vl.until = g_vl.t0 + secs;
	g_vl.fsm = c->player ? rb(c->player + P_FSM) : 0;
	logf_("DONE seq=%u vaultlog %.1f s -> %s", seq, secs, g_vl.f ? g_vl.path : "(not written)");
}

static void vl_tick(const struct ctx *c)
{
	double t = mono();
	if (t >= g_vl.until || !c->player) {
		if (g_vl.ep)
			vl_ep_end("the window ended", 0, 0);
		g_vl.on = 0;
		if (g_vl.f)
			fclose(g_vl.f);
		g_vl.f = NULL;
		logf_("VAULT seq=%u end: %d episodes, %d ticks; file=%s", g_vl.seq, g_vl.eps, g_vl.ticks, g_vl.path);
		return;
	}
	g_vl.ticks++;
	uintptr_t P = c->player, sc = c->scombat, pm = rq(P + P_PM), atk = sc ? rq(sc + C_ATTACK) : 0;
	uintptr_t sync = sc ? rq(sc + C_SYNC1) : 0, srec = sync ? rq(sync + SYNC_REC) : 0, tgt = srec ? rq(sync + SYNC_TARGET) : 0;
	uint8_t fsm = rb(P + P_FSM), vm = pm ? rb(pm + PM_VELMODE) : 0;
	/* the movement's Combat (pm+0x38): +0x56c = the current attack's EnableCollision (somvr's SOMVR_PHASE holds it at 0;
	 * the solver drops its NavMeshPhysics correction while an attack runs with it 0; 0xff = unread) */
	uintptr_t pmc = pm ? rq(pm + 0x38) : 0;
	uint8_t col = pmc ? rb(pmc + 0x56c) : 0xff;
	struct orc o;
	int ho = 0;
	if (tgt && orc_get(tgt, g_pi.pos, &o))
		ho = 1;
	else if (pick_get(&o))
		ho = 1;
	else if (g_vl.partner_c && orc_get(g_vl.partner_c, g_pi.pos, &o) && !o.dead)
		ho = 1;
	else if (g_vl.ticks % 15 == 1) {
		struct orc oo[16];
		int n = orcs_collect(c, oo, 16);
		for (int i = 0; i < n && !ho; i++)
			if (!oo[i].dead && oo[i].ai && oo[i].npc && oo[i].dist < 1500.0f)
				o = oo[i], ho = 1;
	}
	if (ho)
		g_vl.partner_c = o.c;
	float oq[4] = { 0, 0, 0, 1 }, oyaw = 0;
	if (ho && !rvec(o.hobj + OBJ_ROT, oq, 4))
		oyaw = yaw_of(oq);
	char an[128], sn[128], on[128] = "";
	rec_name(atk, an, sizeof an);
	rec_name(srec, sn, sizeof sn);
	if (ho)
		rec_name(rq(o.c + C_ATTACK), on, sizeof on);
	if (g_vl.f)
		fprintf(g_vl.f, "%.4f\t%.2f\t%.2f\t%.2f\t%.2f\t%u\t%u\t%s\t%s\t%#lx\t%#lx\t%.2f\t%.2f\t%.2f\t%.2f\t%s\t%u\n", t - g_vl.t0,
			g_root.pos[0], g_root.pos[1], g_root.pos[2], (double)(g_root.yaw * RAD2DEG), fsm, vm, an, sn, (unsigned long)tgt,
			(unsigned long)(ho ? o.c : 0), ho ? o.pos[0] : 0.0f, ho ? o.pos[1] : 0.0f, ho ? o.pos[2] : 0.0f,
			(double)(oyaw * RAD2DEG), on, col);
	if ((atk != g_vl.atk || srec != g_vl.srec || fsm != g_vl.fsm || col != g_vl.col) && rl_ok(RL_VAULT))
		logf_("VAULT seq=%u +%.3f fsm %u->%u attack '%s' sync '%s' target %#lx; root (%.1f %.1f %.1f) yaw %.1f; partner %#lx at %.1f "
		      "m yaw %.1f attack '%s'; collide %u", g_vl.seq, t - g_vl.t0, g_vl.fsm, fsm, an, sn, (unsigned long)tgt, g_root.pos[0],
		      g_root.pos[1], g_root.pos[2], (double)(g_root.yaw * RAD2DEG), (unsigned long)(ho ? o.c : 0),
		      ho ? (double)(hdist(o.pos, g_root.pos) / 100.0f) : -1.0, (double)(oyaw * RAD2DEG), on, col);
	g_vl.atk = atk, g_vl.srec = srec, g_vl.fsm = fsm, g_vl.col = col;
	int act = srec || fsm == 9 || atk;
	if (act && !g_vl.ep) {
		g_vl.ep = 1;
		g_vl.ep_t0 = t;
		memcpy(g_vl.ep_p0, g_root.pos, sizeof g_vl.ep_p0);
		memcpy(g_vl.ep_last, g_root.pos, sizeof g_vl.ep_last);
		g_vl.ep_y0 = g_root.yaw;
		g_vl.ep_oy0 = oyaw;
		g_vl.ep_have_o = ho;
		g_vl.ep_path = g_vl.ep_step = 0;
		g_vl.ep_names[0] = g_vl.ep_onames[0] = 0;
		g_vl.ep_coloff = 0;
	}
	if (g_vl.ep) {
		g_vl.ep_coloff |= col == 0 && atk; /* the attack's EnableCollision 0 while it runs (the solver drops the correction) */
		float st = dist3(g_root.pos, g_vl.ep_last);
		g_vl.ep_path += st;
		if (st > g_vl.ep_step)
			g_vl.ep_step = st;
		memcpy(g_vl.ep_last, g_root.pos, sizeof g_vl.ep_last);
		char tag[160];
		if (an[0]) {
			snprintf(tag, sizeof tag, "attack %s", an);
			vl_addname(g_vl.ep_names, sizeof g_vl.ep_names, tag);
		}
		if (sn[0]) {
			snprintf(tag, sizeof tag, "sync %s", sn);
			vl_addname(g_vl.ep_names, sizeof g_vl.ep_names, tag);
		}
		if (fsm == 9)
			vl_addname(g_vl.ep_names, sizeof g_vl.ep_names, "fsm 9");
		vl_addname(g_vl.ep_onames, sizeof g_vl.ep_onames, on);
		if (!act)
			vl_ep_end("over", ho && g_vl.ep_have_o ? wrap_pi(oyaw - g_vl.ep_oy0) * RAD2DEG : 0, ho && g_vl.ep_have_o);
	}
}

/* ---- npcpush: does Talion's proxy stop at an orc (LR7)? ---- */

static struct {
	int stage, sp, passed_walk, passed_push, push_ticks, push_ok;
	unsigned seq;
	double t_next, t0;
	float cm, v, dir[3], o0[3], p0[3], dmin_walk, dmin_push, d_start;
	uintptr_t hobj;
} g_np;

/* npcpush [CM] [V] (the server tick: the spawn) */
static void w_npcpush(const struct ctx *c, unsigned seq, int argc, char **argv)
{
	float cm = (float)num(argc > 1 ? argv[1] : NULL, 150), v = (float)num(argc > 2 ? argv[2] : NULL, 250);
	cm = cm < 150 ? 150 : cm > 600 ? 600 : cm;
	v = v < 50 ? 50 : v > 800 ? 800 : v;
	if (!g_h[HK_ROOTDELTA].installed || g_np.stage || !g_root.have) {
		logf_("FAIL seq=%u npcpush: P_ROOTDELTA %s, a push %s, root %d", seq, g_h[HK_ROOTDELTA].installed ? "on" : "NOT installed",
		      g_np.stage ? "already running" : "not running", g_root.have);
		return;
	}
	char why[300], arch[96], tn[128];
	uintptr_t t = tp_pick("AI_Infantry", why, sizeof why, arch, sizeof arch);
	if (!t) {
		logf_("FAIL seq=%u npcpush: no AI_Infantry template (%s)", seq, why);
		return;
	}
	float yaw = g_head.set ? g_head.yaw : g_root.yaw, f[3];
	yaw_dir(yaw, f, NULL);
	int n0 = g_sp_n;
	memcpy(g_spawn_dir, f, sizeof f);
	g_spawn_dir_on = 1;
	sp_spawn(c, seq, "npcpush", t, rec_name(t, tn, sizeof tn), arch[0] ? arch : "AI_Infantry", 0, cm, -1);
	g_spawn_dir_on = 0;
	if (g_sp_n != n0 + 1) {
		logf_("FAIL seq=%u npcpush: the spawn failed (its lines say why)", seq);
		return;
	}
	memset(&g_np, 0, sizeof g_np);
	g_np.stage = 1;
	g_np.seq = seq;
	g_np.sp = g_sp_n - 1;
	g_np.hobj = g_sp[g_np.sp].hobj;
	g_np.cm = cm;
	g_np.v = v;
	g_np.t0 = mono();
	g_np.t_next = g_np.t0 + 1.2;
	logf_("NPCPUSH seq=%u an AI_Infantry %.0f cm along %.1f deg (%s); in 1.2 s loco at it at %.0f cm/s, then a push", seq,
	      (double)cm, (double)(yaw * RAD2DEG), g_head.set ? g_head.how : "Talion's facing", (double)v);
	logf_("DONE seq=%u npcpush: spawned", seq);
}

static void np_tick(const struct ctx *c)
{
	double t = mono();
	struct sp *s = g_np.sp < g_sp_n ? &g_sp[g_np.sp] : NULL;
	struct orc o;
	int ok = s && s->hobj == g_np.hobj && sp_alive(s) && orc_get(s->c, g_pi.pos, &o);
	if (!ok) {
		if (atomic_load(&g_loco.mode) && !strcmp(g_loco.who, "npcpush"))
			loco_end("the orc is gone");
		logf_("NPCPUSH seq=%u stopped at stage %d: the orc is gone", g_np.seq, g_np.stage);
		g_np.stage = 0;
		return;
	}
	float d = hdist(o.pos, g_root.pos), sp = (o.pos[0] - g_root.pos[0]) * g_np.dir[0] + (o.pos[2] - g_root.pos[2]) * g_np.dir[2];
	if (g_np.stage == 1) {
		if (t < g_np.t_next)
			return;
		if (d < 1) {
			logf_("NPCPUSH seq=%u the orc stands on Talion (%.1f cm): no direction", g_np.seq, (double)d);
			g_np.stage = 0;
			return;
		}
		g_np.dir[0] = (o.pos[0] - g_root.pos[0]) / d, g_np.dir[1] = 0, g_np.dir[2] = (o.pos[2] - g_root.pos[2]) / d;
		memcpy(g_np.o0, o.pos, sizeof g_np.o0);
		memcpy(g_np.p0, g_root.pos, sizeof g_np.p0);
		g_np.d_start = d;
		g_np.dmin_walk = g_np.dmin_push = d;
		float vw[3] = { g_np.dir[0] * g_np.v, 0, g_np.dir[2] * g_np.v };
		double secs = (d + 100.0) / g_np.v;
		char fr[64];
		snprintf(fr, sizeof fr, "the orc at %.1f m", (double)(d / 100.0f));
		if (loco_begin(c, g_np.seq, LOCO_CONST, vw, secs > 3 ? 3 : secs, "npcpush", fr)) {
			g_np.stage = 0;
			return;
		}
		g_np.stage = 2;
		return;
	}
	if (g_np.stage == 2) {
		if (d < g_np.dmin_walk)
			g_np.dmin_walk = d;
		if (sp < 0)
			g_np.passed_walk = 1;
		if (atomic_load(&g_loco.mode) && !strcmp(g_loco.who, "npcpush"))
			return;
		/* the push: the rest of the way + 60 cm in one shot (pm+0x428, in Talion's frame), at most 2 m */
		float L = sp + 60.0f, dw[3], dl[3];
		L = L < 20 ? 20 : L > 200 ? 200 : L;
		for (int k = 0; k < 3; k++)
			dw[k] = g_np.dir[k] * L;
		qrot_inv(g_pi.rot, dw, dl);
		dl[1] = 0;
		for (int k = 0; k < 3; k++)
			dl[k] = dl[k] > 200 ? 200 : dl[k] < -200 ? -200 : dl[k];
		g_np.push_ok = pmmove_do(c, g_np.seq, dl, "npcpush") == 0;
		g_np.stage = 3;
		g_np.push_ticks = 0;
		return;
	}
	if (d < g_np.dmin_push)
		g_np.dmin_push = d;
	if (sp < 0)
		g_np.passed_push = 1;
	if (++g_np.push_ticks < 4)
		return;
	float om = dist3(o.pos, g_np.o0);
	logf_("NPCPUSH seq=%u result: start %.1f cm apart; the walk (loco %.0f cm/s) came within %.1f cm%s; the push (%s) within "
	      "%.1f cm%s; now %.1f cm, the orc moved %.1f cm, Talion %.1f cm: %s", g_np.seq, (double)g_np.d_start, (double)g_np.v,
	      (double)g_np.dmin_walk, g_np.passed_walk ? " and PASSED it" : "", g_np.push_ok ? "written" : "refused",
	      (double)g_np.dmin_push, g_np.passed_push ? " and PASSED it" : "", (double)d, (double)om,
	      (double)hdist(g_root.pos, g_np.p0), g_np.passed_walk || g_np.passed_push ? "no collision with the orc" :
	      "stopped at the orc (a collision)");
	g_np.stage = 0;
}

/* ---- per tick and the reset ---- */

static void p10_tick(const struct ctx *c)
{
	root_tick(c);
	if (g_walk.on)
		walk_tick(c);
	loco_tick(c);
	if (g_spd.on)
		spd_tick(c);
	if (g_fl.on)
		fl_tick(c);
	if (g_ar.on)
		ar_tick(c);
	if (g_vl.on)
		vl_tick(c);
	if (g_np.stage)
		np_tick(c);
	pmr_tick(c);
	memset(g_rd.gsum, 0, sizeof g_rd.gsum); /* the root deltas since this tick */
	g_rd.gcnt = g_rd.rcnt = 0;
}

static void p10_reset(void)
{
	atomic_store(&g_loco.mode, LOCO_OFF); /* a new player: nothing of ours is written to it */
	g_loco.stop_left = 0;
	g_walk.on = 0;
	if (g_spd.f)
		fclose(g_spd.f);
	if (g_fl.f)
		fclose(g_fl.f);
	if (g_vl.f)
		fclose(g_vl.f);
	g_spd.f = g_fl.f = g_vl.f = NULL;
	g_spd.on = g_fl.on = g_vl.on = 0;
	g_ar.on = 0;
	g_np.stage = 0;
	g_pmr.base_on = 0;
	g_pmr.nset = 0;
	g_pmr.later_tick = 0;
	g_root.have = 0;
	memset(&g_gv, 0, sizeof g_gv);
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P8: per-tick work and the reset                                                                                */

static void p8_tick(const struct ctx *c)
{
	ui_tick(c);
	if (g_ikw.on || g_ikw.cb)
		ikwatch_tick(c);
	if (g_arm.on)
		arm_tick(c);
	if (g_agg.n)
		agg_tick();
	if (g_mnt.on)
		mount_tick(c);
	if (g_dagm.tick && g_ticks_all >= g_dagm.tick) {
		char w[40];
		snprintf(w, sizeof w, "%s (3 ticks later)", g_dagm.what);
		dag_pos(w, g_dagm.seq, g_dagm.item, g_dagm.node);
		g_dagm.tick = 0;
	}
	if (g_phm.tick && g_ticks_all >= g_phm.tick) {
		char md[400];
		uintptr_t sm = obj_model(g_phm.obj);
		logf_("PIECEMASK seq=%u %s, 2 ticks later: %s", g_phm.seq, g_phm.what, sm ? rmask_desc(sm, md, sizeof md) : "(no model)");
		g_phm.tick = 0;
	}
	if (atomic_exchange(&g_prompt_snap, 0)) {
		uint64_t sig;
		if (rl_ok(RL_PROMPT))
			logf_("PROMPTS at a counter prompt at Talion: %d live entries", prompts_walk(c, 1, &sig));
	}
}

static void p8_reset(void)
{
	g_arm.on = 0;
	g_arm.trk = 0;
	g_grav.off = 0;
	g_ikw.on = g_ikw.cb = 0;
	g_ptr.down = 0;
	g_ptr.have = 0;
	g_mc.pending = 0;
	g_agg.n = 0;
	g_mh.on = 0;
	g_ah.on = 0;
	g_att_n = 0;
	g_mnt.on = 0;
	g_hsarm.armed = 0;
	atomic_store(&g_hsarm.c, 0);
	g_dagm.tick = 0;
	g_phm.tick = 0;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* P7: the on/off loggers, the per-tick work and the reset                                                        */

static int w_onoff(const char *w, int argc, char **argv, unsigned seq)
{
	static const struct { const char *w; int *flag; int lo, hi; } t[] = { /* the word and its hooks */
		{ "stimlog", &g_stimlog, HK_STIM_START, HK_REGSTIM_P }, { "aiwatch", &g_aiwatch, HK_AI_GOAL, HK_AI_DODGE_GO },
		{ "keytimes", &g_keytimes, HK_KEYTIMES, HK_KEYTIMES }, { "projlog", &g_projlog, HK_PJ_UPDATE, HK_PJ_DAMAGE },
		{ "fxlog", &g_fxlog, HK_FX0, HK_FX_LAST }, { "soundlog", &g_soundlog, HK_SND_POST, HK_SND_POST_CB },
	};
	for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
		if (strcmp(w, t[i].w))
			continue;
		int on = !(argc > 1 && !strcmp(argv[1], "off")), inst = 0;
		*t[i].flag = on;
		for (int k = t[i].lo; k <= t[i].hi; k++)
			inst += g_h[k].installed;
		logf_("DONE seq=%u %s %s (%d of its %d hooks installed)", seq, w, on ? "on" : "off", inst, t[i].hi - t[i].lo + 1);
		if (!on && t[i].flag == &g_fxlog)
			fx_table(seq);
		if (!on && t[i].flag == &g_soundlog)
			snd_table();
		if (!on && t[i].flag == &g_keytimes)
			logf_("KEYTIMES off: %u attack starts not queued (ws - 0xb0 was not a Combat with that HOBJECT)", g_kt_rej);
		return 1;
	}
	return 0;
}

static int p7_busy(void) { return g_keytimes || g_hs.on || g_swing.on || g_pjarm.mode || g_hsarm.armed; }

static void p7_tick(const struct ctx *c, int server)
{
	g_ticks_all++;
	if (g_keytimes)
		kt_process();
	if (g_hs.on)
		hs_tick();
	if (g_swing.on)
		swing_tick();
	if (g_pjarm.mode && mono() >= g_pjarm.until) {
		logf_("PROJ seq=%u armed %s expired: no projectile came within %.1f m in 30 s", g_pjarm.seq,
		      g_pjarm.mode == 1 ? "block" : "reflect", (double)g_pjarm.radius);
		g_pjarm.mode = 0;
	}
	if (!server && g_prompts_watch)
		prompts_tick(c);
	if (!server && g_trav.on)
		trav_tick(c);
	if (server && g_hsarm.armed)
		hs_arm_tick(); /* P8: `hitstop ... onbpw` */
	if (!server) {
		p8_tick(c);
		p9_tick(c);
		p10_tick(c);
	}
}

static void p7_reset(void)
{
	if (g_hs.on)
		hs_end("the local player changed");
	if (g_swing.on && !g_swing.ended)
		swing_end("the local player changed");
	g_swing.on = 0;
	if (g_dag.on)
		logf_("DAGGER the local player changed: the dagger of seq %u is forgotten (a new model starts as the game made it)",
		      g_dag.seq);
	g_dag.on = 0;
	g_wfx_handle = 0; /* never released on a stale object */
	g_ph.on = 0;      /* a rebuilt model starts unhidden */
	g_trav.on = 0;
	memset(&g_pick, 0, sizeof g_pick);
	pthread_mutex_lock(&g_kt_mu);
	memset(g_kt, 0, sizeof g_kt);
	pthread_mutex_unlock(&g_kt_mu);
	p8_reset();
	p9_reset();
	p10_reset();
}

/* ------------------------------------------------------------------------------------------------------------ */
/* The command runner                                                                                             */

static int split(char *s, char **argv, int cap)
{
	int n = 0;
	char *save = NULL;
	for (char *p = strtok_r(s, " \t\r\n", &save); p && n < cap; p = strtok_r(NULL, " \t\r\n", &save))
		argv[n++] = p;
	return n;
}

/* dealhit and the P7 words that change combat state run right after the server update, as the game's own changes do */
static int is_server_word(const char *w)
{
	return !strcmp(w, "dealhit") || !strcmp(w, "swingevent") || !strcmp(w, "hitstop") || !strcmp(w, "dagger") ||
	       !strcmp(w, "weaponfx") || !strcmp(w, "aggro") || !strcmp(w, "minhealth") || !strcmp(w, "spawn") ||
	       !strcmp(w, "spawncaptain") || !strcmp(w, "despawn") || !strcmp(w, "incoming") || !strcmp(w, "aiattacked") ||
	       !strcmp(w, "npcpush");
}

/* run one word on the game thread; returns 1 if the runner must stop popping (sleep) */
static int run_word(const struct ctx *c, unsigned seq, char *word_line, int server)
{
	char *argv[32];
	char buf[CQ_LEN];
	snprintf(buf, sizeof buf, "%s", word_line);
	int argc = split(buf, argv, 32);
	if (!argc)
		return 0;
	const char *w = argv[0];
	if (!server && is_server_word(w) && g_h[HK_SERVER].installed && mono() - g_server_last_t < 1.0) {
		if (g_sq_n < SQ_N) {
			snprintf(g_sq[g_sq_n].line, CQ_LEN, "%s", word_line);
			g_sq[g_sq_n++].seq = seq;
		} else {
			logf_("FAIL seq=%u %s: server queue full", seq, w);
		}
		return 0;
	}
	if (!strcmp(w, "mark")) {
		const char *txt = word_line + strspn(word_line, " \t");
		txt += strcspn(txt, " \t");
		txt += strspn(txt, " \t");
		logf_("MARK seq=%u %s t=%.6f", seq, txt, mono());
	} else if (!strcmp(w, "sleep")) {
		g_resume_t = mono() + num(argc > 1 ? argv[1] : NULL, 0) / 1000.0;
		return 1;
	} else if (!strcmp(w, "state")) {
		g_state_on = !(argc > 1 && !strcmp(argv[1], "off"));
		logf_("DONE seq=%u state %s", seq, g_state_on ? "on" : "off");
	} else if (!strcmp(w, "bindings")) {
		w_bindings(c, seq);
	} else if (!strcmp(w, "pieces")) {
		w_pieces(c, seq);
	} else if (!strcmp(w, "nodes")) {
		w_nodes(c, seq);
	} else if (!strcmp(w, "pose")) {
		int nfr = argc > 1 ? (int)num(argv[1], 60) : 60;
		g_pose.left = nfr < 1 ? 1 : nfr > 600 ? 600 : nfr;
		g_pose.total = g_pose.left;
		g_pose.stats_eq_post_vis = g_pose.stats_eq_pre_post = g_pose.stats_eq_cam_vis = g_pose.stats_frames = 0;
		g_pose.have_pre = g_pose.have_post = g_pose.have_cam = 0;
		logf_("DONE seq=%u pose: sampling %d frames of node %s (hooks anim %d/%d campost %d/%d vis %d)", seq, g_pose.left,
		      g_pose.have_node ? g_pose.node_name : "(none: run nodes first; object position only)",
		      g_h[HK_ANIM_A].installed, g_h[HK_ANIM_B].installed, g_h[HK_CAMPOST_A].installed,
		      g_h[HK_CAMPOST_B].installed, g_h[HK_VIS].installed);
	} else if (!strcmp(w, "orcs")) {
		w_orcs(c, seq);
	} else if (!strcmp(w, "attacks")) {
		w_attacks(seq);
		if (argc > 1 && !strcmp(argv[1], "full"))
			w_attacks_full(seq);
	} else if (!strcmp(w, "ui")) {
		w_ui(c, seq);
	} else if (!strcmp(w, "release")) {
		vin_release_all("release word");
		logf_("DONE seq=%u release", seq);
	} else if (!strcmp(w, "showhead")) {
		w_showhead(c, seq);
	} else if (!strcmp(w, "fpcam") && (argc < 2 || strcmp(argv[1], "on"))) {
		w_fpcam(seq, argc, argv);
	} else if (!strcmp(w, "near") && argc > 1 && !strcmp(argv[1], "off")) {
		w_near(seq, "off");
	} else if (!strcmp(w, "autocounter") && (argc < 2 || !strcmp(argv[1], "off"))) {
		w_autocounter(seq, argc, argv);
	} else if (!strcmp(w, "pmcount")) {
		w_pmcount(c, seq, argc > 1 ? argv[1] : NULL);
	} else if (!strcmp(w, "timers")) { /* P7, read-only */
		w_timers(c, seq);
	} else if (!strcmp(w, "pick")) {
		w_pick(c, seq, argc, argv);
	} else if (w_onoff(w, argc, argv, seq)) {
		/* stimlog aiwatch keytimes projlog fxlog soundlog */
	} else if (!strcmp(w, "weapons")) {
		w_weapons(seq);
	} else if (!strcmp(w, "syncinfo")) {
		w_syncinfo(seq, argc, argv);
	} else if (!strcmp(w, "navdump")) {
		w_navdump(seq);
	} else if (!strcmp(w, "prompts")) {
		w_prompts(c, seq, argc, argv);
	} else if (!strcmp(w, "uiscene")) {
		w_uiscene(c, seq);
	} else if (!strcmp(w, "res")) {
		w_res(c, seq);
	} else if (!strcmp(w, "trav")) {
		w_trav(c, seq, argc > 1 ? argv[1] : NULL);
	} else if (!strcmp(w, "restall") && !(argc > 1 && !strcmp(argv[1], "on"))) {
		w_restall(seq, "off");
	} else if (!strcmp(w, "piecehide") && (argc < 2 || !strcmp(argv[1], "off"))) {
		w_piecehide(c, seq, argc, argv);
	} else if (!strcmp(w, "hitstop") && argc > 1 && !strcmp(argv[1], "off")) { /* P7 undo words: no guard needed */
		if (g_hs.on)
			hs_end("off");
		logf_("DONE seq=%u hitstop off%s", seq, g_hsarm.armed ? " (the armed hold dropped)" : "");
		g_hsarm.armed = 0;
	} else if ((!strcmp(w, "proj") || !strcmp(w, "projblock") || !strcmp(w, "projreflect")) && argc > 1 &&
		   !strcmp(argv[1], "off")) {
		w_proj(seq, w, argc, argv);
	} else if (!strcmp(w, "weaponfx") && argc > 1 && !strcmp(argv[1], "stop")) {
		w_weaponfx_stop(seq);
	} else if (!strcmp(w, "dagger") && argc > 1 && !strcmp(argv[1], "back")) {
		w_dagger(seq, argc, argv);
	} else if (!strcmp(w, "attached")) { /* P8, read-only */
		w_attached(c, seq);
	} else if (!strcmp(w, "wnodes")) {
		w_wnodes(seq, argc, argv);
	} else if (!strcmp(w, "ikwatch") || !strcmp(w, "ikcb")) {
		w_ikwatch(c, seq, argc, argv, !strcmp(w, "ikcb"));
	} else if (!strcmp(w, "mouselog")) {
		g_mouselog = !(argc > 1 && !strcmp(argv[1], "off"));
		logf_("DONE seq=%u mouselog %s (hooks sys %d/%d/%d view %d/%d/%d gfx %d guard %d/%d)", seq, g_mouselog ? "on" : "off",
		      g_h[HK_LP_SYS_DOWN].installed, g_h[HK_LP_SYS_UP].installed, g_h[HK_LP_SYS_MOVE].installed,
		      g_h[HK_LP_VIEW_DOWN].installed, g_h[HK_LP_VIEW_UP].installed, g_h[HK_LP_VIEW_MOVE].installed,
		      g_h[HK_LP_GFX_EVENT].installed, g_h[HK_LP_GUARD_HOVER].installed, g_h[HK_LP_GUARD_CLICK].installed);
	} else if (!strcmp(w, "mstate")) {
		w_mstate(c, seq);
	} else if (!strcmp(w, "hitscan")) {
		w_hitscan(c, seq);
	} else if (!strcmp(w, "surf")) {
		w_surf(c, seq, argc > 1 ? argv[1] : NULL);
	} else if (!strcmp(w, "mount")) {
		w_mount(c, seq, argc > 1 ? argv[1] : NULL);
	} else if (!strcmp(w, "conlog")) {
		w_conlog(seq, argc > 1 ? argv[1] : NULL);
	} else if (!strcmp(w, "attachhide") && (argc < 2 || !strcmp(argv[1], "off"))) { /* P8 undo words: no guard needed */
		w_attachhide(c, seq, argc, argv);
	} else if (!strcmp(w, "armpose") && (argc < 2 || !strcmp(argv[1], "off"))) {
		w_armpose(c, seq, argc, argv);
	} else if (!strcmp(w, "aggro") && argc > 1 && !strcmp(argv[1], "off")) {
		w_aggro(c, seq, argc, argv);
	} else if (!strcmp(w, "minhealth") && (argc < 2 || !strcmp(argv[1], "off"))) {
		w_minhealth(seq, argc, argv);
	} else if (!strcmp(w, "padguard") && argc > 1 && !strcmp(argv[1], "on")) {
		w_padguard(seq, "on");
	} else if (!strcmp(w, "grav") && argc > 1 && !strcmp(argv[1], "on")) {
		w_grav(c, seq, "on");
	} else if (!strcmp(w, "blocktable")) { /* P9, read-only */
		w_blocktable(c, seq, argc, argv);
	} else if (!strcmp(w, "aiblockwatch")) {
		w_aiblockwatch(seq, argc > 1 ? argv[1] : NULL);
	} else if (!strcmp(w, "templates")) {
		w_templates(seq, argc, argv);
	} else if (!strcmp(w, "tod")) {
		w_tod(seq);
	} else if (!strcmp(w, "weathers")) {
		w_weathers(seq);
	} else if (!strcmp(w, "wmdump")) {
		w_wmdump(seq);
	} else if (!strcmp(w, "blockroll") && !(argc > 1 && !strcmp(argv[1], "force"))) { /* P9 undo words */
		w_blockroll(seq, "off");
	} else if (!strcmp(w, "despawn")) {
		w_despawn(seq, argc, argv);
	} else if (!strcmp(w, "aimray") && argc > 1 && !strcmp(argv[1], "off")) {
		w_aimray(c, seq, argc, argv);
	} else if (!strcmp(w, "speed")) { /* P10, read-only */
		w_speed(c, seq, argc > 1 ? argv[1] : NULL);
	} else if (!strcmp(w, "pmrec") && (argc < 2 || !strcmp(argv[1], "dump") ||
					   (argc > 2 && !strcmp(argv[1], "base") && !strcmp(argv[2], "off")))) {
		w_pmrec(c, seq, argc, argv);
	} else if (!strcmp(w, "heading")) {
		w_heading(c, seq, argc, argv);
	} else if (!strcmp(w, "vaultlog")) {
		w_vaultlog(c, seq, argc > 1 ? argv[1] : NULL);
	} else if ((!strcmp(w, "walk") || !strcmp(w, "loco") || !strcmp(w, "animrate")) && argc > 1 && !strcmp(argv[1], "off")) {
		if (w[0] == 'w') /* P10 undo words */
			w_walk(c, seq, argc, argv);
		else if (w[0] == 'l')
			w_loco(c, seq, argc, argv);
		else
			w_animrate(c, seq, argc, argv);
	} else if (!strcmp(w, "cmd") || !strcmp(w, "move") || !strcmp(w, "look") || !strcmp(w, "pad") ||
		   !strcmp(w, "padmode") || !strcmp(w, "dealhit") || !strcmp(w, "hidehead") || !strcmp(w, "fpcam") ||
		   !strcmp(w, "near") || !strcmp(w, "autocounter") || !strcmp(w, "exec") || !strcmp(w, "pmmove") ||
		   !strcmp(w, "swingevent") || !strcmp(w, "hitstop") || !strcmp(w, "projblock") || !strcmp(w, "projreflect") ||
		   !strcmp(w, "weaponfx") || !strcmp(w, "dagger") || !strcmp(w, "sound") || !strcmp(w, "restall") ||
		   !strcmp(w, "piecehide") || !strcmp(w, "attachhide") || !strcmp(w, "armpose") || !strcmp(w, "aggro") ||
		   !strcmp(w, "minhealth") || !strcmp(w, "pointer") || !strcmp(w, "click") || !strcmp(w, "padguard") ||
		   !strcmp(w, "mcmd") || !strcmp(w, "grav") || !strcmp(w, "blockroll") || !strcmp(w, "incoming") ||
		   !strcmp(w, "aiattacked") || !strcmp(w, "spawn") || !strcmp(w, "spawncaptain") || !strcmp(w, "wmhold") ||
		   !strcmp(w, "aimray") || !strcmp(w, "walk") || !strcmp(w, "loco") || !strcmp(w, "fling") ||
		   !strcmp(w, "animrate") || !strcmp(w, "npcpush") || !strcmp(w, "pmrec")) {
		if (!active_allowed(w, seq))
			return 0;
		if (!strcmp(w, "walk")) { /* P10, active */
			w_walk(c, seq, argc, argv);
		} else if (!strcmp(w, "loco")) {
			w_loco(c, seq, argc, argv);
		} else if (!strcmp(w, "fling")) {
			w_fling(c, seq, argc, argv);
		} else if (!strcmp(w, "animrate")) {
			w_animrate(c, seq, argc, argv);
		} else if (!strcmp(w, "npcpush")) {
			w_npcpush(c, seq, argc, argv);
		} else if (!strcmp(w, "pmrec")) {
			w_pmrec(c, seq, argc, argv);
		} else if (!strcmp(w, "blockroll")) { /* P9, active */
			w_blockroll(seq, "force");
		} else if (!strcmp(w, "incoming") || !strcmp(w, "aiattacked")) {
			w_incoming(c, seq, argc, argv);
		} else if (!strcmp(w, "spawn")) {
			w_spawn(c, seq, argc, argv);
		} else if (!strcmp(w, "spawncaptain")) {
			w_spawncaptain(c, seq, argc, argv);
		} else if (!strcmp(w, "wmhold")) {
			w_wmhold(c, seq, argc, argv);
		} else if (!strcmp(w, "aimray")) {
			w_aimray(c, seq, argc, argv);
		} else if (!strcmp(w, "attachhide")) { /* P8, active */
			w_attachhide(c, seq, argc, argv);
		} else if (!strcmp(w, "armpose")) {
			w_armpose(c, seq, argc, argv);
		} else if (!strcmp(w, "aggro")) {
			w_aggro(c, seq, argc, argv);
		} else if (!strcmp(w, "minhealth")) {
			w_minhealth(seq, argc, argv);
		} else if (!strcmp(w, "pointer")) {
			w_pointer(c, seq, argc, argv);
		} else if (!strcmp(w, "click")) {
			w_click(c, seq, argc, argv);
		} else if (!strcmp(w, "padguard")) {
			w_padguard(seq, argc > 1 ? argv[1] : "off");
		} else if (!strcmp(w, "mcmd")) {
			w_mcmd(c, seq, argc, argv);
		} else if (!strcmp(w, "grav")) {
			w_grav(c, seq, argc > 1 ? argv[1] : "off");
		} else if (!strcmp(w, "swingevent")) {
			w_swing(c, seq, argc, argv);
		} else if (!strcmp(w, "hitstop")) {
			w_hitstop(c, seq, argc, argv);
		} else if (!strcmp(w, "projblock") || !strcmp(w, "projreflect")) {
			w_proj(seq, w, argc, argv);
		} else if (!strcmp(w, "weaponfx")) {
			w_weaponfx(c, seq, argc, argv);
		} else if (!strcmp(w, "dagger")) {
			w_dagger(seq, argc, argv);
		} else if (!strcmp(w, "sound")) {
			w_sound(c, seq, argc, argv);
		} else if (!strcmp(w, "restall")) {
			w_restall(seq, "on");
		} else if (!strcmp(w, "piecehide")) {
			w_piecehide(c, seq, argc, argv);
		} else if (!strcmp(w, "exec")) {
			const char *rest = word_line + strspn(word_line, " \t");
			w_exec(c, seq, rest + strcspn(rest, " \t"));
		} else if (!strcmp(w, "pmmove")) {
			w_pmmove(c, seq, argc, argv);
		} else if (!strcmp(w, "cmd")) {
			w_cmd(c, seq, argc, argv);
		} else if (!strcmp(w, "move")) {
			int cmds[2] = { 6, 3 };
			float v[2] = { (float)num(argc > 1 ? argv[1] : NULL, 0), (float)num(argc > 2 ? argv[2] : NULL, 0) };
			w_axes(c, seq, w, cmds, 2, v, num(argc > 3 ? argv[3] : NULL, 1000));
		} else if (!strcmp(w, "look")) {
			int cmds[4] = { 13, 23, 12, 22 };
			float yaw = (float)num(argc > 1 ? argv[1] : NULL, 0), pitch = (float)num(argc > 2 ? argv[2] : NULL, 0);
			float v[4] = { yaw, yaw, pitch, pitch };
			w_axes(c, seq, w, cmds, 4, v, num(argc > 3 ? argv[3] : NULL, 500));
		} else if (!strcmp(w, "pad")) {
			w_pad(c, seq, argc, argv);
		} else if (!strcmp(w, "padmode")) {
			w_padmode(c, seq, argc > 1 ? argv[1] : "on");
		} else if (!strcmp(w, "dealhit")) {
			w_dealhit(c, seq, argc, argv);
		} else if (!strcmp(w, "hidehead")) {
			w_hidehead(c, seq, argc, argv);
		} else if (!strcmp(w, "fpcam")) {
			w_fpcam(seq, argc, argv);
		} else if (!strcmp(w, "near")) {
			w_near(seq, argc > 1 ? argv[1] : NULL);
		} else {
			w_autocounter(seq, argc, argv);
		}
	} else {
		logf_("FAIL seq=%u unknown word '%s'", seq, w);
	}
	return 0;
}

/* a queued line may hold several words separated by ';' (`\;` is not a separator: exec passes it on as ';') */
static char g_pending[CQ_LEN];
static unsigned g_pending_seq;

static char *word_end(char *s)
{
	for (char *p = s; (p = strchr(p, ';')); p++)
		if (p == s || p[-1] != '\\')
			return p;
	return NULL;
}

static void run_queue(const struct ctx *c)
{
	for (int guard = 0; guard < 16; guard++) {
		if (mono() < g_resume_t)
			return;
		if (!g_pending[0] && !cq_pop(g_pending, &g_pending_seq))
			return;
		char *semi = word_end(g_pending), word[CQ_LEN];
		if (semi) {
			size_t n = (size_t)(semi - g_pending);
			memcpy(word, g_pending, n);
			word[n] = 0;
			memmove(g_pending, semi + 1, strlen(semi + 1) + 1);
		} else {
			snprintf(word, sizeof word, "%s", g_pending);
			g_pending[0] = 0;
		}
		if (run_word(c, g_pending_seq, word, 0))
			return;
	}
}

/* ------------------------------------------------------------------------------------------------------------ */
/* State logger and events (game thread, every input tick)                                                        */

struct st {
	int ifs, menus, paused, video, cur, pien, life, photo, spect, scripted, pad, allow_pad, allow_kbm, modal, fsm, fsm_prev;
	uintptr_t player, pi, cine, mount;
	uint64_t tags;
};
static struct st g_st_last;
static int g_st_have;

static void state_tick(const struct ctx *c)
{
	struct st s;
	memset(&s, 0, sizeof s);
	s.ifs = c->ifs;
	s.menus = c->menus;
	s.paused = c->paused;
	s.video = c->im && c->ifs >= 1 && c->ifs <= 4 && g_fn_ok[FN_VIDEOCOVER] ? (int)(FN(FN_VIDEOCOVER, cover_fn)(c->im) & 0xff) : -1;
	s.cur = c->cur;
	s.pi = c->pi;
	s.pien = c->pi ? rb(c->pi + PI_ENABLED) : -1;
	s.player = c->player;
	s.life = c->player ? c->life : -1;
	s.photo = c->player ? (int)r32(rq(c->player + P_PHOTO) + 0x30) : -1;
	s.spect = c->player ? rb(c->player + P_SPECT) : -1;
	s.mount = c->player ? rq(c->player + P_MOUNT) : 0;
	s.scripted = c->cam ? rb(c->cam + CAM_SCRIPTED) : -1;
	s.cine = c->shell ? rq(rq(c->shell + SH_CINE) + 0x20) : 0;
	s.pad = c->profile ? rb(c->profile + PROF_PAD) : -1;
	s.allow_pad = rb(G.allow_pad);
	s.allow_kbm = rb(G.allow_kbm);
	s.modal = c->im ? rb(c->im + 0x2a48) : -1;
	s.fsm = c->player ? rb(c->player + P_FSM) : -1; /* re-traversal.md 1.6: 8 falling, 9 transition, 10 landing */
	s.fsm_prev = c->player ? rb(c->player + P_FSM + 1) : -1;
	uintptr_t set = 0;
	int tn = 0;
	if (c->body) {
		uintptr_t mgr = c->body + BODY_TAGS;
		set = mgr + (uintptr_t)rb(mgr + 0x158) * 0xa8 + 8;
		tn = (int)r32(set + 0xa0);
		if (tn < 0 || tn > 20)
			tn = 0;
		uintptr_t recs[20];
		if (tn && !rd(set, recs, (size_t)tn * 8))
			for (int i = 0; i < tn; i++)
				s.tags = s.tags * 1000003u + recs[i];
	}
	if (g_st_have && !memcmp(&s, &g_st_last, sizeof s))
		return;
	int tags_changed = !g_st_have || s.tags != g_st_last.tags;
	g_st_last = s;
	g_st_have = 1;
	if (!g_state_on || !rl_ok(RL_STATE))
		return;
	logf_("STATE t=%.6f tick=%llu ifs=%d menus=%d paused=%d video=%d local=%d pi=%#lx pi_en=%d player=%#lx life=%d photo=%d "
	      "spect=%d mount=%#lx cine=%#lx scripted=%d padmode=%d allow=%d/%d modal=%d fsm=%d/%d", mono(), (unsigned long long)g_tick,
	      s.ifs, s.menus, s.paused, s.video, s.cur, (unsigned long)s.pi, s.pien, (unsigned long)s.player, s.life, s.photo,
	      s.spect, (unsigned long)s.mount, (unsigned long)s.cine, s.scripted, s.pad, s.allow_pad, s.allow_kbm, s.modal, s.fsm,
	      s.fsm_prev);
	if (tags_changed && tn) {
		char line[1500], nm[96];
		size_t o = 0;
		o += (size_t)snprintf(line + o, sizeof line - o, "TAGS n=%d:", tn);
		for (int i = 0; i < tn && o < sizeof line - 100; i++)
			o += (size_t)snprintf(line + o, sizeof line - o, " %s", rec_name(rq(set + 8 * (uintptr_t)i), nm, sizeof nm));
		logf_("%s", line);
	}
}

static void events_tick(const struct ctx *c)
{
	double t = mono();
	int menu = c->ifs == 1, world = c->player && c->ifs == 3 && !c->paused && c->life == 2;
	int now[EV_N] = { 1, menu, world };
	for (int e = 0; e < EV_N; e++) {
		if (g_ev_t[e] >= 0)
			continue;
		if (!now[e]) {
			g_ev_cand[e] = 0;
			continue;
		}
		if (!g_ev_cand[e])
			g_ev_cand[e] = t;
		if (e == EV_START || t - g_ev_cand[e] >= 3.0) {
			g_ev_t[e] = t;
			logf_("EVENT %s t=%.6f tick=%llu", e == EV_START ? "start" : e == EV_MENU ? "menu" : "world", t,
			      (unsigned long long)g_tick);
		}
	}
	for (int i = 0; i < g_auto_n; i++)
		if (!g_auto[i].done && g_ev_t[g_auto[i].ev] >= 0 && t >= g_ev_t[g_auto[i].ev] + g_auto[i].after) {
			g_auto[i].done = 1;
			cq_push(g_auto[i].line);
		}
}

static void guard_tick(void)
{
	if (g_guard_ok >= 0)
		return;
	uintptr_t v = rq(G.guard_slot);
	g_guard_ok = v && v != G.guard_orig;
	logf_("GUARDCHECK WBOnlineService slot 219 %#lx = %#lx: %s; active words %s", (unsigned long)G.guard_slot,
	      (unsigned long)v, g_guard_ok ? "patched (spike1's save guard is in place)" : "ORIGINAL (no save guard)",
	      g_guard_ok || g_noguard_ok ? "allowed" : "refused");
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Hooks: vtable slots (plain C)                                                                                  */

typedef void (*void1_fn)(void *self);
typedef uint64_t (*vis_fn)(void *self, void *view, uint64_t flag);
typedef uint64_t (*ui105_fn)(void *self, uint64_t target, uint64_t mask);

static void p_input(void *self)
{
	((void1_fn)g_h[HK_INPUT].orig)(self);
	first_call(HK_INPUT, atomic_fetch_add_explicit(&g_h[HK_INPUT].calls, 1, memory_order_relaxed) + 1);
	g_tick++;
	struct ctx c;
	ctx_get(&c);
	static uintptr_t last_player, last_pi;
	if ((last_player && c.player && c.player != last_player) || (last_pi && c.pi && c.pi != last_pi)) {
		logf_("RESET the local player changed (player %#lx -> %#lx, pi %#lx -> %#lx): virtual inputs released, the "
		      "hidden-piece list and the head node forgotten", (unsigned long)last_player, (unsigned long)c.player,
		      (unsigned long)last_pi, (unsigned long)c.pi);
		vin_release_all("player changed");
		g_hidden_n = 0;
		g_pose.have_node = 0;
		g_pose.left = 0;
		g_fp.on = 0;
		g_rw.left = 0;
		g_pmc.on = 0;
		atomic_store(&g_pm_armed, 0);
		p7_reset();
	}
	if (c.player)
		last_player = c.player;
	if (c.pi)
		last_pi = c.pi;
	atomic_store_explicit(&g_game_tid, my_tid(), memory_order_relaxed);
	p7_cache(&c);
	guard_tick();
	state_tick(&c);
	events_tick(&c);
	rw_tick(&c);
	pmc_tick(&c);
	if (g_ac.pending && c.pi) {
		vin_press(c.pi, g_ac.cmd, -1, 1.0f, 1, 1, 1, (double)g_ac.frames / 60.0);
		g_ac.pending = 0;
		g_ac.presses++;
	}
	run_queue(&c);
	vin_update();
	p7_tick(&c, 0);
	if (g_pose.left > 0) { /* the samples need this frame's objects */
		g_pose.m = model_iface();
		g_pose.obj = char_obj(&c);
		g_pose.body = c.body;
	}
}

#define AC_CAND 16 /* the nearest few only: the poll runs every other server tick */

static void autocounter_poll(const struct ctx *c)
{
	struct orc o[AC_CAND];
	int n = orcs_collect(c, o, AC_CAND);
	for (int i = 0; i < n; i++) {
		uint8_t p = rb(o[i].c + C_WIN_PARRY);
		int slot = -1, freeslot = -1;
		for (int k = 0; k < SEEN_N; k++) {
			if (g_win[k].c == o[i].c)
				slot = k;
			if (!g_win[k].c && freeslot < 0)
				freeslot = k;
		}
		if (slot < 0) {
			if (freeslot < 0)
				continue;
			slot = freeslot;
			g_win[slot].c = o[i].c;
			g_win[slot].parry = 0;
		}
		if (p && !g_win[slot].parry && !o[i].dead) {
			char an[128];
			g_ac.pending = 1;
			if (rl_ok(RL_AUTOC))
				logf_("AUTOCOUNTER orc=%#lx dist=%.1f attack='%s' parry window opened (bpw %.3f s ago) -> %s t=%.6f",
				      (unsigned long)o[i].c, o[i].dist, rec_name(rq(o[i].c + C_ATTACK), an, sizeof an),
				      g_win[slot].bpw_t > 0 ? mono() - g_win[slot].bpw_t : -1.0, g_cmd_names[g_ac.cmd], mono());
		}
		g_win[slot].parry = p;
	}
}

static void p_server(void *self)
{
	((void1_fn)g_h[HK_SERVER].orig)(self);
	first_call(HK_SERVER, atomic_fetch_add_explicit(&g_h[HK_SERVER].calls, 1, memory_order_relaxed) + 1);
	g_server_last_t = mono();
	if (!g_sq_n && !g_ac.on && !p7_busy())
		return;
	struct ctx c;
	ctx_get(&c);
	for (int i = 0; i < g_sq_n; i++)
		run_word(&c, g_sq[i].seq, g_sq[i].line, 1);
	g_sq_n = 0;
	static unsigned sticks;
	if (g_ac.on && (sticks++ & 1) == 0)
		autocounter_poll(&c);
	p7_tick(&c, 1);
}

static void pose_sample(float out[3], float obj[3])
{
	float xf[8] = { 0 };
	if (g_pose.have_node && node_xf(g_pose.obj, g_pose.node, xf) == 0)
		memcpy(out, xf, 12);
	else
		memset(out, 0, 12);
	if (obj)
		obj_pos(g_pose.obj, obj);
}

static int veq(const float a[3], const float b[3]) { return a[0] == b[0] && a[1] == b[1] && a[2] == b[2]; }

static uint64_t p_vis(void *self, void *view, uint64_t flag)
{
	first_call(HK_VIS, atomic_fetch_add_explicit(&g_h[HK_VIS].calls, 1, memory_order_relaxed) + 1);
	uintptr_t ra = (uintptr_t)__builtin_return_address(0);
	if (!g_renderer)
		g_renderer = (uintptr_t)self;
	if (g_pose.left > 0 && g_pose.obj && (!G.ra_vis_player || ra == G.ra_vis_player)) {
		float vis[3], ob[3], vp[7] = { 0 };
		pose_sample(vis, ob);
		rvec((uintptr_t)view, vp, 7);
		unsigned fr = g_renderer ? r32(g_renderer + 0x38) : 0;
		g_pose.stats_frames++;
		g_pose.stats_eq_post_vis += g_pose.have_post && veq(g_pose.post, vis);
		g_pose.stats_eq_pre_post += g_pose.have_pre && g_pose.have_post && veq(g_pose.pre, g_pose.post);
		g_pose.stats_eq_cam_vis += g_pose.have_cam && veq(g_pose.cam, vis);
		logf_("POSE f=%u t=%.6f pre=%.3f,%.3f,%.3f post=%.3f,%.3f,%.3f cam=%.3f,%.3f,%.3f vis=%.3f,%.3f,%.3f obj_pre=%.3f,%.3f,%.3f "
		      "obj_post=%.3f,%.3f,%.3f obj_vis=%.3f,%.3f,%.3f view=%.3f,%.3f,%.3f have=%d%d%d", fr, mono(), g_pose.pre[0],
		      g_pose.pre[1], g_pose.pre[2], g_pose.post[0], g_pose.post[1], g_pose.post[2], g_pose.cam[0], g_pose.cam[1],
		      g_pose.cam[2], vis[0], vis[1], vis[2], g_pose.obj_pre[0], g_pose.obj_pre[1], g_pose.obj_pre[2],
		      g_pose.obj_post[0], g_pose.obj_post[1], g_pose.obj_post[2], ob[0], ob[1], ob[2], vp[0], vp[1], vp[2],
		      g_pose.have_pre, g_pose.have_post, g_pose.have_cam);
		g_pose.have_pre = g_pose.have_post = g_pose.have_cam = 0;
		if (--g_pose.left == 0)
			logf_("POSE-SUMMARY frames=%u node=%s equal_post_vis=%u equal_pre_post=%u (frames in which the animation "
			      "update did not move the node) equal_campost_vis=%u", g_pose.stats_frames, g_pose.node_name,
			      g_pose.stats_eq_post_vis, g_pose.stats_eq_pre_post, g_pose.stats_eq_cam_vis);
	} else if (!G.ra_vis_player || ra == G.ra_vis_player) {
		if (rl_ok(RL_VIS) && atomic_load(&g_h[HK_VIS].calls) <= 3)
			logf_("VIS first player-view calls: this=%p view=%p flag=%u ra=%#lx frame=%u", self, view, (unsigned)(flag & 0xff),
			      (unsigned long)ra, r32((uintptr_t)self + 0x38));
	}
	return ((vis_fn)g_h[HK_VIS].orig)(self, view, flag);
}

static uint64_t p_ui105(void *self, uint64_t target, uint64_t mask)
{
	first_call(HK_UI105, atomic_fetch_add_explicit(&g_h[HK_UI105].calls, 1, memory_order_relaxed) + 1);
	if (atomic_load_explicit(&g_ui105_budget, memory_order_relaxed) > 0) {
		atomic_fetch_sub(&g_ui105_budget, 1);
		logf_("UI105 this=%p target=%d mask=%#x ra=%#lx tid=%d frame=%u t=%.6f", self, (int)target, (unsigned)mask,
		      (unsigned long)__builtin_return_address(0), my_tid(), g_renderer ? r32(g_renderer + 0x38) : 0, mono());
	}
	return ((ui105_fn)g_h[HK_UI105].orig)(self, target, mask);
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Hooks: call sites                                                                                              */

typedef uint64_t (*one_fn)(uint64_t a);

static void anim_common(int hk, struct regs *r)
{
	first_call(hk, atomic_fetch_add_explicit(&g_h[hk].calls, 1, memory_order_relaxed) + 1);
	int pose = g_pose.left > 0 && g_pose.obj && (!g_pose.body || r->rdi == g_pose.body);
	if (g_arm.on && g_arm.have_after && r->rdi == g_arm.body)
		arm_seen(); /* P8: armpose's edit as the next player update reads it */
	if (pose) {
		pose_sample(g_pose.pre, g_pose.obj_pre);
		g_pose.have_pre = 1;
	}
	r->rax = ((one_fn)g_h[hk].orig)(r->rdi);
	if (pose) {
		pose_sample(g_pose.post, g_pose.obj_post);
		g_pose.have_post = 1;
	}
}
__attribute__((used)) void somvr_pc_anim_a(struct regs *r) { anim_common(HK_ANIM_A, r); }
__attribute__((used)) void somvr_pc_anim_b(struct regs *r) { anim_common(HK_ANIM_B, r); }

static void fpcam_apply(uintptr_t player)
{
	uintptr_t cam = rq(player + P_CAM), body = rq(player + P_BODY);
	uintptr_t obj = rq(rq(body + BODY_MGR) + MGR_CHAR);
	if (!obj)
		obj = rq(body + BODY_OBJ);
	float head[8], q0[4], q1[4], q[4];
	uintptr_t shell = rq(G.shell_pp);
	if (!cam || !body || !g_pose.have_node || node_xf(obj, g_pose.node, head) != 0)
		return;
	if ((shell && rq(rq(shell + SH_CINE) + 0x20)) || r32(rq(player + P_PHOTO) + 0x30) != 3 || rb(player + P_LIFE) != 2)
		return; /* cinematic camera, photo mode, not alive: leave the game's camera */
	if (rvec(cam + CAM_ROT, q0, 4) || rvec(cam + CAM_EXTRA, q1, 4))
		return;
	qmul(q0, q1, q);
	float f[3], fz[3] = { 0, 0, 1 };
	qrot(q, fz, f);
	float h = atan2f(f[0], f[2]);
	float pitch = g_fp.have_pitch ? g_fp.pitch_deg * (float)M_PI / 180.0f : asinf(fmaxf(-1.0f, fminf(1.0f, -f[1])));
	float yq[4] = { 0, sinf(h / 2), 0, cosf(h / 2) }, pq[4] = { sinf(pitch / 2), 0, 0, cosf(pitch / 2) };
	float xf[8], off[3] = { 0, g_fp.up, g_fp.fwd }, offw[3];
	qrot(yq, off, offw);
	xf[0] = head[0] + offw[0];
	xf[1] = head[1] + offw[1];
	xf[2] = head[2] + offw[2];
	qmul(yq, pq, xf + 3);
	xf[7] = 1.0f;
	FN(FN_BODYSETCAM, setcam_fn)(body, xf);
	FN(FN_CAMRESET, camreset_fn)(cam);
	if (rl_ok(RL_FP))
		logf_("FPCAM head=%.2f,%.2f,%.2f cam=%.2f,%.2f,%.2f yaw=%.2f pitch=%.2f deg", head[0], head[1], head[2], xf[0], xf[1],
		      xf[2], h * 180.0f / (float)M_PI, pitch * 180.0f / (float)M_PI);
}

static void campost_common(int hk, struct regs *r)
{
	first_call(hk, atomic_fetch_add_explicit(&g_h[hk].calls, 1, memory_order_relaxed) + 1);
	r->rax = ((one_fn)g_h[hk].orig)(r->rdi);
	if (g_pose.left > 0 && g_pose.obj) {
		pose_sample(g_pose.cam, NULL);
		g_pose.have_cam = 1;
	}
	if (g_fp.on)
		fpcam_apply(r->rdi);
}
__attribute__((used)) void somvr_pc_campost_a(struct regs *r) { campost_common(HK_CAMPOST_A, r); }
__attribute__((used)) void somvr_pc_campost_b(struct regs *r) { campost_common(HK_CAMPOST_B, r); }

/* player attack start (FUN_00a8c700 -> the three start-attack wrappers): c, node, attack, target */
static void atk_common(int hk, const char *kind, struct regs *r)
{
	first_call(hk, atomic_fetch_add_explicit(&g_h[hk].calls, 1, memory_order_relaxed) + 1);
	char nn[128], an[128];
	rec_name(r->rdx, an, sizeof an);
	if (atk_valid(r->rdx)) {
		g_last_player_attack = r->rdx;
		snprintf(g_last_player_attack_name, sizeof g_last_player_attack_name, "%s", an);
	}
	if (rl_ok(RL_ATK))
		logf_("ATK %s c=%#lx node='%s' attack=%#lx '%s' target=%#lx t=%.6f tick=%llu", kind, (unsigned long)r->rdi,
		      rec_name(r->rsi, nn, sizeof nn), (unsigned long)r->rdx, an, (unsigned long)r->rcx, mono(),
		      (unsigned long long)g_tick);
}
__attribute__((used)) void somvr_pc_atk_obj(struct regs *r) { atk_common(HK_ATK_OBJ, "aimobj", r); }
__attribute__((used)) void somvr_pc_atk_pos(struct regs *r) { atk_common(HK_ATK_POS, "aimpos", r); }
__attribute__((used)) void somvr_pc_atk_nopos(struct regs *r) { atk_common(HK_ATK_NOPOS, "noaim", r); }

/* the game's own APPLYDAMAGE -> DealHit: pos xmm0/xmm1, attacker, target, attack, forceKill */
__attribute__((used)) void somvr_pc_dealhit(struct regs *r)
{
	first_call(HK_DEALHIT, atomic_fetch_add_explicit(&g_h[HK_DEALHIT].calls, 1, memory_order_relaxed) + 1);
	char an[128];
	if (rq(r->rdi + C_PLAYER) && atk_valid(r->rdx)) { /* a hit the player's own attack landed: a usable record */
		g_last_player_attack = r->rdx;
		snprintf(g_last_player_attack_name, sizeof g_last_player_attack_name, "%s",
			 rec_name(r->rdx, an, sizeof an));
	}
	if (!rl_ok(RL_DEALHIT))
		return;
	logf_("GAMEHIT attacker=%#lx player=%d target=%#lx attack='%s' kill=%u pos=%.1f,%.1f,%.1f t=%.6f", (unsigned long)r->rdi,
	      rq(r->rdi + C_PLAYER) != 0, (unsigned long)r->rsi, rec_name(r->rdx, an, sizeof an), (unsigned)(r->rcx & 0xff),
	      r->xmm[0][0], r->xmm[0][1], r->xmm[1][0], mono());
}

/* APPLYDAMAGE key: attacker Combat*, attack, target HOBJECT (orcs hitting the player: the telegraph's end) */
__attribute__((used)) void somvr_pc_applydmg(struct regs *r)
{
	first_call(HK_APPLYDMG, atomic_fetch_add_explicit(&g_h[HK_APPLYDMG].calls, 1, memory_order_relaxed) + 1);
	if (rq(r->rdi + C_AI))
		seen_add(r->rdi);
	if (g_keytimes)
		kt_applied(r->rdi);
	if (g_hs.on && r->rdi == g_hs.c)
		g_hs.applies++;
	else if (g_hsa.c && r->rdi == g_hsa.c && !g_hsa.logged && mono() - g_hsa.t_end < 3.0) {
		g_hsa.logged = 1;
		logf_("HITSTOP seq=%u the orc's APPLYDAMAGE %.3f s after the hold started (%.3f s after it ended)", g_hsa.seq,
		      mono() - g_hsa.t0, mono() - g_hsa.t_end);
	}
	double bpw = -1;
	for (int k = 0; k < SEEN_N; k++)
		if (g_win[k].c == r->rdi && g_win[k].bpw_t > 0)
			bpw = mono() - g_win[k].bpw_t;
	if (!rl_ok(RL_APPLY))
		return;
	char an[128];
	logf_("APPLYDAMAGE attacker=%#lx ai=%d attack='%s' target=%#lx%s since_bpw=%.3f parry=%u hitwin=%u t=%.6f",
	      (unsigned long)r->rdi, rq(r->rdi + C_AI) != 0, rec_name(r->rsi, an, sizeof an), (unsigned long)r->rdx,
	      r->rdx && (r->rdx == g_pi.phobj || r->rdx == g_pi.charobj) ? " (THE PLAYER)" : "", bpw, rb(r->rdi + C_WIN_PARRY),
	      rb(r->rdi + C_WIN_HIT), mono());
}

/* BPW key -> counter prompt (orc Combat*, window ms) */
__attribute__((used)) void somvr_pc_counter(struct regs *r)
{
	first_call(HK_COUNTER, atomic_fetch_add_explicit(&g_h[HK_COUNTER].calls, 1, memory_order_relaxed) + 1);
	seen_add(r->rdi);
	int slot = -1;
	for (int k = 0; k < SEEN_N && slot < 0; k++)
		if (g_win[k].c == r->rdi)
			slot = k;
	for (int k = 0; k < SEEN_N && slot < 0; k++)
		if (!g_win[k].c) {
			slot = k;
			g_win[k].c = r->rdi;
		}
	if (slot >= 0)
		g_win[slot].bpw_t = mono();
	uintptr_t tg = rq(r->rdi + C_TARGET);
	int at = tg && (tg == g_pi.phobj || tg == g_pi.charobj);
	if (at) {
		atomic_store(&g_prompt_snap, 1); /* P8: what the prompt diamonds hold right after a counter prompt at Talion */
		if (g_hsarm.armed)
			atomic_store(&g_hsarm.c, r->rdi);
	}
	if (!rl_ok(RL_COUNTER))
		return;
	char an[128];
	logf_("COUNTERPROMPT orc=%#lx window_ms=%d attack='%s' target=%#lx%s t=%.6f", (unsigned long)r->rdi, (int)r->rsi,
	      rec_name(rq(r->rdi + C_ATTACK), an, sizeof an), (unsigned long)tg, at ? " (THE PLAYER)" : "", mono());
}

/* hit event (target Combat*, event): +0x30 attacker HOBJECT, +0x38 attack, +0x50 damage, +0x58 block FX, +0x5a crit */
__attribute__((used)) void somvr_pc_hitev(struct regs *r)
{
	first_call(HK_HITEV, atomic_fetch_add_explicit(&g_h[HK_HITEV].calls, 1, memory_order_relaxed) + 1);
	seen_add(r->rdi);
	if (!rl_ok(RL_HITEV))
		return;
	char an[128];
	uintptr_t ev = r->rsi;
	logf_("HITEV target=%#lx attacker_hobj=%#lx attack='%s' damage=%.2f block=%u crit=%u pos=%.1f,%.1f,%.1f t=%.6f",
	      (unsigned long)r->rdi, (unsigned long)rq(ev + 0x30), rec_name(rq(ev + 0x38), an, sizeof an), rf(ev + 0x50),
	      rb(ev + 0x58), rb(ev + 0x5a), rf(ev), rf(ev + 4), rf(ev + 8), mono());
}

__attribute__((used)) void somvr_pc_death(struct regs *r)
{
	first_call(HK_DEATH, atomic_fetch_add_explicit(&g_h[HK_DEATH].calls, 1, memory_order_relaxed) + 1);
	if (!rl_ok(RL_DEATH))
		return;
	char an[128], vn[128];
	logf_("DEATH victim=%#lx '%s' attack='%s' killer=%#lx killer_is_player=%d t=%.6f", (unsigned long)r->rdi,
	      rec_name(rq(r->rdi + C_CHARREC), vn, sizeof vn), rec_name(r->rsi, an, sizeof an), (unsigned long)r->rdx,
	      r->rdx && rq(r->rdx + C_PLAYER) != 0, mono());
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Command file and writer thread                                                                                 */

static struct { ino_t ino; struct timespec mt; off_t size; int have; } g_cmdsig;

static void poll_cmd_file(void)
{
	if (!g_cmd_path[0])
		return;
	struct stat st;
	if (stat(g_cmd_path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0)
		return;
	if (g_cmdsig.have && g_cmdsig.ino == st.st_ino && g_cmdsig.size == st.st_size &&
	    g_cmdsig.mt.tv_sec == st.st_mtim.tv_sec && g_cmdsig.mt.tv_nsec == st.st_mtim.tv_nsec)
		return;
	char buf[4096];
	int fd = open(g_cmd_path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return;
	/* the signature comes from the opened file: a rename between the stat and the open would otherwise pair the new
	 * content with the old signature, and the next poll would queue the same lines again */
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
	    (g_cmdsig.have && g_cmdsig.ino == st.st_ino && g_cmdsig.size == st.st_size &&
	     g_cmdsig.mt.tv_sec == st.st_mtim.tv_sec && g_cmdsig.mt.tv_nsec == st.st_mtim.tv_nsec)) {
		close(fd);
		return;
	}
	ssize_t n = read(fd, buf, sizeof buf - 1);
	close(fd);
	if (n <= 0)
		return; /* not yet written: look again next time (signature not taken) */
	buf[n] = 0;
	g_cmdsig.ino = st.st_ino;
	g_cmdsig.size = st.st_size;
	g_cmdsig.mt = st.st_mtim;
	g_cmdsig.have = 1;
	char *save = NULL;
	for (char *line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		while (*line == ' ' || *line == '\t')
			line++;
		size_t l = strlen(line);
		while (l && (line[l - 1] == '\r' || line[l - 1] == ' '))
			line[--l] = 0;
		if (l && line[0] != '#')
			cq_push(line);
	}
}

static void *writer_main(void *arg)
{
	(void)arg;
	prctl(PR_SET_NAME, "somvr-probe", 0, 0, 0);
	double last = mono();
	uint64_t prev[HK_N] = { 0 };
	while (!atomic_load(&g_stop)) {
		struct timespec ts = { 0, 100 * 1000 * 1000 };
		nanosleep(&ts, NULL);
		poll_cmd_file();
		double t = mono();
		if (t - last >= 5.0) {
			char line[1500];
			size_t o = (size_t)snprintf(line, sizeof line, "RATE t=%.3f dt=%.3f", t, t - last);
			uint64_t fx = 0;
			int fxi = 0;
			for (int i = 0; i < HK_N; i++) {
				uint64_t cnt = atomic_load(&g_h[i].calls);
				if (g_h[i].installed && i >= HK_FX0 && i <= HK_FX_LAST) /* the 19 FX init sites as one count */
					fx += cnt - prev[i], fxi++;
				else if (g_h[i].installed)
					o += (size_t)snprintf(line + o, sizeof line - o, " %s=%llu", g_h[i].name + 2,
							      (unsigned long long)(cnt - prev[i]));
				prev[i] = cnt;
			}
			if (fxi)
				o += (size_t)snprintf(line + o, sizeof line - o, " FXI=%llu", (unsigned long long)fx);
			snprintf(line + o, sizeof line - o, " vin=%d drops=%llu", atomic_load(&g_vin_n),
				 (unsigned long long)atomic_load(&g_drops));
			logf_("%s", line);
			for (int i = 0; i < RL_N; i++) {
				uint32_t s = atomic_exchange(&g_rl[i].suppressed, 0);
				if (s)
					logf_("SUPPRESSED %s %u lines in the last 5 s", g_rl[i].name, s);
			}
			last = t;
		}
		flush_log();
	}
	return NULL;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Start                                                                                                          */

static void copy_env(const char *k, char *dst, size_t cap)
{
	const char *v = getenv(k);
	snprintf(dst, cap, "%s", v ? v : "");
}

static void parse_auto(const char *spec)
{
	if (!spec || !*spec)
		return;
	char buf[4096];
	snprintf(buf, sizeof buf, "%s", spec);
	char *save = NULL;
	for (char *it = strtok_r(buf, "|", &save); it && g_auto_n < AUTO_N; it = strtok_r(NULL, "|", &save)) {
		while (*it == ' ')
			it++;
		char *sp = strchr(it, ' ');
		if (!sp)
			continue;
		*sp = 0;
		char *plus = strchr(it, '+');
		double after = plus ? num(plus + 1, 0) : 0;
		if (plus)
			*plus = 0;
		int ev = !strcmp(it, "start") ? EV_START : !strcmp(it, "menu") ? EV_MENU : !strcmp(it, "world") ? EV_WORLD : -1;
		if (ev < 0) {
			logf_("WARN SOMVR_PROBE_AUTO: unknown event '%s'", it);
			continue;
		}
		g_auto[g_auto_n].ev = ev;
		g_auto[g_auto_n].after = after;
		snprintf(g_auto[g_auto_n].line, CQ_LEN, "%s", sp + 1);
		logf_("AUTO %d: at %s+%.1f s: %s", g_auto_n, it, after, g_auto[g_auto_n].line);
		g_auto_n++;
	}
}

static int probe_start(const struct site_spec *hooks, const struct site_spec *fns, const struct site_spec *ro, const char *who)
{
	copy_env("SOMVR_PROBE_LOG", g_log_path, sizeof g_log_path);
	if (!g_log_path[0])
		return -1;
	copy_env("SOMVR_PROBE_CMD", g_cmd_path, sizeof g_cmd_path);
	copy_env("SOMVR_PROBE_OUT", g_out_dir, sizeof g_out_dir);
	if (!g_out_dir[0]) {
		snprintf(g_out_dir, sizeof g_out_dir, "%s", g_log_path);
		char *sl = strrchr(g_out_dir, '/');
		if (sl)
			snprintf(sl, sizeof g_out_dir - (size_t)(sl - g_out_dir), "/probe-out");
		else
			snprintf(g_out_dir, sizeof g_out_dir, "probe-out");
	}
	g_noguard_ok = getenv("SOMVR_PROBE_NOGUARD_OK") && !strcmp(getenv("SOMVR_PROBE_NOGUARD_OK"), "1");
	g_fd = open(g_log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
	if (g_fd < 0)
		return -1;
	g_cloc = newlocale(LC_ALL_MASK, "C", (locale_t)0);
	g_pid = getpid();
	char exe[512] = "";
	ssize_t el = readlink("/proc/self/exe", exe, sizeof exe - 1);
	exe[el > 0 ? el : 0] = 0;
	struct timespec wall;
	clock_gettime(CLOCK_REALTIME, &wall);
	logf_("START somvr_probe built %s %s pid=%d exe=%s gate=%s t=%.6f wall=%lld.%03ld", __DATE__, __TIME__, (int)g_pid, exe,
	      who, mono(), (long long)wall.tv_sec, wall.tv_nsec / 1000000);
	logf_("CONFIG cmd=%s out=%s auto=%s skip=%s noguard_ok=%d savedir=%s", g_cmd_path[0] ? g_cmd_path : "(unset: words only from AUTO)",
	      g_out_dir, getenv("SOMVR_PROBE_AUTO") ? getenv("SOMVR_PROBE_AUTO") : "(none)",
	      getenv("SOMVR_PROBE_SKIP") ? getenv("SOMVR_PROBE_SKIP") : "(none)", g_noguard_ok,
	      getenv("SOMVR_SAVEDIR") ? getenv("SOMVR_SAVEDIR") : "(unset)");
	char ap[800];
	logf_("CONFIG @cout=%s -> %s/VFS/Temp (Feral's VFS mount)", DOZE_OUT, app_prefs(ap, sizeof ap) ? ap : "(no XDG_DATA_HOME/HOME)");
	if (fns)
		check_fns(fns);
	if (ro)
		check_ro(ro);
	void *const repl[HK_N] = { (void *)p_input, (void *)p_value, (void *)p_value2, (void *)p_ison, (void *)p_server,
				   (void *)p_vis, (void *)p_ui105, (void *)somvr_pw_anim_a, (void *)somvr_pw_anim_b,
				   (void *)somvr_pw_campost_a, (void *)somvr_pw_campost_b, (void *)somvr_pw_atk_obj,
				   (void *)somvr_pw_atk_pos, (void *)somvr_pw_atk_nopos, (void *)somvr_pw_dealhit,
				   (void *)somvr_pw_applydmg, (void *)somvr_pw_counter, (void *)somvr_pw_hitev,
				   (void *)somvr_pw_death, (void *)p_pmroot,
				   /* P7 */
				   (void *)somvr_pw_stim_start, (void *)somvr_pw_stim_key, (void *)somvr_pw_stim_stop,
				   (void *)somvr_pw_regstim, (void *)somvr_pw_regstim_p, (void *)p_ai_goal, (void *)p_ai_block_ok,
				   (void *)p_ai_block_go, (void *)p_ai_dodge_ok, (void *)p_ai_dodge_go, (void *)somvr_pw_keytimes,
				   (void *)p_pjupdate, (void *)somvr_pw_pjdamage, (void *)somvr_pw_snd, (void *)somvr_pw_snd_cb,
				   (void *)somvr_pw_uiscene, (void *)somvr_pw_fx0, (void *)somvr_pw_fx1, (void *)somvr_pw_fx2,
				   (void *)somvr_pw_fx3, (void *)somvr_pw_fx4, (void *)somvr_pw_fx5, (void *)somvr_pw_fx6,
				   (void *)somvr_pw_fx7, (void *)somvr_pw_fx8, (void *)somvr_pw_fx9, (void *)somvr_pw_fx10,
				   (void *)somvr_pw_fx11, (void *)somvr_pw_fx12, (void *)somvr_pw_fx13, (void *)somvr_pw_fx14,
				   (void *)somvr_pw_fx15, (void *)somvr_pw_fx16, (void *)somvr_pw_fx17, (void *)somvr_pw_fx18,
				   (void *)somvr_pw_res3d,
				   /* P8 */
				   (void *)somvr_pw_iksetup_a, (void *)somvr_pw_iksetup_b, (void *)somvr_pw_ikchain,
				   (void *)somvr_pw_addcb, (void *)somvr_pw_remcb, (void *)p_trk_eval, (void *)p_attach_listen,
				   (void *)somvr_pw_guard_hover, (void *)somvr_pw_guard_click, (void *)p_sys_down, (void *)p_sys_up,
				   (void *)p_sys_move, (void *)p_view_down, (void *)p_view_up, (void *)p_view_move, (void *)p_gfx_event,
				   (void *)p_grav,
				   /* P9 */
				   (void *)somvr_pw_aib_gate, (void *)somvr_pw_aib_rec, (void *)somvr_pw_aib_roll,
				   (void *)somvr_pw_aib_notify, (void *)p_goal_rel, (void *)somvr_pw_shield, (void *)somvr_pw_bowcamxf,
				   /* P10 */
				   (void *)somvr_pw_rootdelta };
	install_hooks(hooks, repl);
	parse_auto(getenv("SOMVR_PROBE_AUTO"));
	g_active = 1;
	if (!g_h[HK_INPUT].installed)
		logf_("WARN P_INPUT is not installed: no game-thread tick, no words will run");
	flush_log();
	sigset_t all, old;
	sigfillset(&all);
	pthread_sigmask(SIG_SETMASK, &all, &old);
	pthread_t th;
	int rc = pthread_create(&th, NULL, writer_main, NULL);
	pthread_sigmask(SIG_SETMASK, &old, NULL);
	if (rc == 0)
		pthread_detach(th);
	else
		logf_("WARN writer thread: %s (no command file, no periodic flush)", strerror(rc));
	return 0;
}

__attribute__((destructor)) static void probe_fini(void)
{
	if (!g_active)
		return;
	con_remove(); /* conlog: the console's own output function back */
	logf_("END t=%.6f input=%llu server=%llu vis=%llu vin=%d drops=%llu", mono(), (unsigned long long)atomic_load(&g_h[HK_INPUT].calls),
	      (unsigned long long)atomic_load(&g_h[HK_SERVER].calls), (unsigned long long)atomic_load(&g_h[HK_VIS].calls),
	      atomic_load(&g_vin_n), (unsigned long long)atomic_load(&g_drops));
	flush_log();
}

#ifndef SOMVR_SELFTEST
/* ------------------------------------------------------------------------------------------------------------ */
/* Gate: only the game's own process (spike1's gate)                                                              */

static int exe_is_game(void)
{
	char exe[4096];
	ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
	if (n <= 0)
		return 0;
	exe[n] = 0;
	const char *b = strrchr(exe, '/');
	return b && !strcmp(b + 1, SOM_EXE_NAME);
}

static int cmdline_is_helper(void)
{
	char buf[16384];
	int fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return 1;
	ssize_t n = read(fd, buf, sizeof buf - 1);
	close(fd);
	if (n < 0)
		return 1;
	buf[n] = 0;
	for (ssize_t i = 0; i < n; i += (ssize_t)strlen(buf + i) + 1)
		if (!strncmp(buf + i, "--type=", 7))
			return 1;
	return 0;
}

static int bid_cb(struct dl_phdr_info *info, size_t size, void *data)
{
	(void)size;
	int *res = data;
	*res = 0;
	if (info->dlpi_name && info->dlpi_name[0])
		return 1;
	if (info->dlpi_addr != 0)
		return 1;
	unsigned char want[20];
	if (parse_hex(SOM_BUILDID, want, sizeof want) != 20)
		return 1;
	for (int i = 0; i < info->dlpi_phnum; i++) {
		const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
		if (ph->p_type != PT_NOTE)
			continue;
		const unsigned char *p = (const unsigned char *)(info->dlpi_addr + ph->p_vaddr), *end = p + ph->p_memsz;
		while (p + sizeof(ElfW(Nhdr)) <= end) {
			const ElfW(Nhdr) *nh = (const ElfW(Nhdr) *)p;
			const unsigned char *name = p + sizeof *nh, *desc = name + ((nh->n_namesz + 3) & ~3u);
			if (desc + nh->n_descsz > end)
				break;
			if (nh->n_type == NT_GNU_BUILD_ID && nh->n_namesz == 4 && !memcmp(name, "GNU", 4) && nh->n_descsz == 20 &&
			    !memcmp(desc, want, 20)) {
				*res = 1;
				return 1;
			}
			p = desc + ((nh->n_descsz + 3) & ~3u);
		}
	}
	return 1;
}

static int buildid_matches(void)
{
	int res = 0;
	dl_iterate_phdr(bid_cb, &res);
	return res;
}

static void drop_self_from_preload(char *was, size_t wcap, char *now, size_t ncap)
{
	const char *lp = getenv("LD_PRELOAD");
	snprintf(was, wcap, "%s", lp ? lp : "");
	snprintf(now, ncap, "(unset)");
	if (!lp)
		return;
	Dl_info di;
	const char *me = dladdr((void *)drop_self_from_preload, &di) && di.dli_fname ? di.dli_fname : "";
	const char *mb = strrchr(me, '/') ? strrchr(me, '/') + 1 : me;
	char keep[4096] = "", tok[4096];
	size_t k = 0;
	for (const char *p = lp; *p;) {
		size_t n = strcspn(p, ": ");
		if (n && n < sizeof tok) {
			memcpy(tok, p, n);
			tok[n] = 0;
			const char *tb = strrchr(tok, '/') ? strrchr(tok, '/') + 1 : tok;
			int mine = !strcmp(tok, me) || (mb[0] && !strcmp(tb, mb));
			if (!mine && k + n + 2 < sizeof keep)
				k += (size_t)snprintf(keep + k, sizeof keep - k, "%s%s", k ? ":" : "", tok);
		}
		p += n;
		if (*p)
			p++;
	}
	if (k) {
		setenv("LD_PRELOAD", keep, 1);
		snprintf(now, ncap, "%s", keep);
	} else {
		unsetenv("LD_PRELOAD");
	}
}

__attribute__((constructor)) static void probe_ctor(void)
{
	if (!exe_is_game() || cmdline_is_helper())
		return;
	const char *lp = getenv("SOMVR_PROBE_LOG");
	if (!lp || !lp[0] || !buildid_matches())
		return;
	char was[1024], now[1024];
	drop_self_from_preload(was, sizeof was, now, sizeof now);
	if (probe_start(g_hook_sites, g_fn_sites, g_ro_sites, "exe+cmdline+buildid") == 0) {
		logf_("ENV LD_PRELOAD was=%s now=%s (children do not load the probe)", was, now);
		flush_log();
	}
}
#endif
