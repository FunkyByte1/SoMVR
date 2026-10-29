/*
 * selftest.c - runs somvr.c's patching, wrappers and the whole game-side eye pipeline against stand-in functions in
 * this program (no game, no runtime): four call-site hooks (H1, H3, H4, H6) and two vtable slots (H2, H9) built at run
 * time. A stand-in per-view body (t_h4) behaves like FUN_009ee170 (H1 -> SomView, vf_37 -> H2, the render thread's
 * FUN_0072c400 -> H3 with tan(fov/2) and offset 0, the frame context -> H6), a stand-in IDXGISwapChain::Present (t_h9),
 * and a stand-in xrLocateViews published into somvr_shared the way the layer does it. Checks: flat frames without XR,
 * the two eyes (poses, covering fov, rects, exact tangents + offset in H3, widened corners in H6), the record and the
 * present tag, the per-call state restore, gm+0x70, layouts A and B, fake views, the trigger gate and its words, a
 * failing runtime, then spike1's save-guard tests. H8 (layout B's eye copy) runs against a stand-in EndRenderTarget
 * executor (a real E9 tail jump, like FUN_007c9de0) and stand-in IdxD3D11 device/context/textures and CVulkanTexture
 * objects: the copy order (eye L copied before eye R's pass begins), one copy per eye per frame, the right target only,
 * the tags' eye bits and the published VkImages, and the fail-soft paths. The optional hooks AUDIO (a stand-in
 * CSoundMgr::vf_23) and PM (a stand-in PlayerMovement::vf_11 with root motion and a wall after it), and the head base
 * against stand-in Player/PlayerBody/object/GetNodeTransform: the listener's head pose and when it is not used, the
 * Head-node base and its fallbacks, the body follow (Talion's frame, the step, the deadzone, the wall, walking, a
 * pending move, another PlayerMovement; the view never moves with the body). SOMVR_COMBAT against stand-in PlayerCombat
 * bands, orcs, the Combat/Attack category, Combat_DealHit, the record lookup, AttackData, the AI stimulus and the
 * IEngineTimer (K_ENTRY sites from the stand-ins' own bytes): the fake blade's heavy and light hits, the call's
 * arguments and the restored pc fields, the records in turn, the stimulus and its removal, the hit-stop and its
 * restore on time/death/sync, never touching a despawned orc, kills, charges, log mode, parts off, the controller
 * stub, another thread, the flat path. SOMVR_COMBAT_REACT against stand-in database records (the orc's character combat
 * record -> AIBlockTable -> entries, the light records' Category; sorted and unsorted attribute entries), its NPC and
 * the four called functions: one roll per swing on the orc ahead, the game's chance > roll, the notify's arguments, the
 * blocked hits (still Combat_DealHit, no hit-stop, +0x55b hits through), the gates, force, log mode, sites off, off;
 * no AIBlockTable (P9): the archetype's chance, no call in its own attack (a stand-in RX_ATK_GATE call), an empty table.
 * SOMVR_INPUT against stand-in CWBInput slots, PlayerInput records, the shell and
 * its state machines, the ON/OFF events, the active sets, the input mode, the rumble callers and a stand-in layer (the
 * snapshot, the grip spaces, xrLocateSpace): somvr_input exported and active, inert with a v2 layer, pad mode, the
 * stick rotated toward the head, the dead zone, the deny list, the stock A, taps, B = Activate, the M2 buttons, a
 * struggle, snap/smooth turns about the head, menus as an Xbox pad, input loss/stall/another thread/a player change,
 * the hands at the views' time, haptics (the word, the rumble) and the controller blade's heavy hit with its pulse.
 * SOMVR_EYE_SIZE against a stand-in mode set (FUN_00741980's 3D desc call, the two rect3D calls): the eye size in
 * layout B, the letterbox off, the eye textures following, xr/fallback. SOMVR_HIDE_HEAD against a stand-in
 * CSimpleModel/renderable/handle/node and the opaque stage's four shadow calls: hidden in the G-buffer, shown in the
 * shadow calls, the game's own pieces, the trigger words, rehide, flat frames, a rebuilt or missing model, a bad node.
 * SOMVR_ARMS against stand-in trackers (slot 3 = vf_03 swapping run 3's pose in), their pose buffers, the model data
 * and skeleton, the player's CSimpleModel and FUN_008f3c20: the player's tracker only, the ease in, both weapon nodes
 * on their grips at the eyes' T', walking with the head base, the bone blade (equal to the controller's while reached,
 * the drawn sword when not), lost hands and flat frames easing out to the animation, another thread, a broken
 * skeleton, arms off. SOMVR_HIDE_HEAD=bone|near on the same trackers, GetNodeTransform reading the player's buffer: the
 * words and the switch's bits (the hook without SOMVR_ARMS, the head base needed), Head's subtree collapsed in phase 2
 * with Head and the rest untouched, the eye base on the uncollapsed Head standing and walking, the flat frame's lag of
 * one, the camera base, another thread, a skeleton without Head, the arms in the same phase 2, the 12 cm near plane.
 * SOMVR_HUD=quad (H11) against a stand-in HUD pass FUN_00a11dc0 behind its two call sites (it draws the frame's and
 * eye's HUD into a stand-in UI surface) and a stand-in present composite FUN_00742d50 before Present: eye 0's pass
 * skipped, eye 1's kept, the UI surface copied into our HUD texture (made at the UI size and format) after eye 1's pass
 * and before the composite, somvr_hud's tag for the present (written before the core tag), video, no pass, shared
 * surfaces, a UI resize, the threaded context, flat frames, layout A, failures (off for the run), the switch off.
 * SOMVR_EYE_SIZE's runtime switch against a stand-in console (SetResolution runs the stand-in mode set) and the input
 * tick: 16:9 at boot, the first world frame held, the eye size at the next tick, the pause menu kept, the game's size
 * after the hold, a late mode set, a wrong engine, no mode set, no tick, always. The pad glyphs against stand-in UIs,
 * views and the controller manager: the boot's views re-armed and registered, a load's view, late textures, a real pad.
 * SOMVR_POINTER against stand-in CInterfaceMgr mouse calls (move/down/up), GetLocalPlayer, the front-end test and
 * FUN_00a38e80 behind two stand-in guard callers (real E8 call sites), a stand-in UI rect and m_GameUI view offset, and a
 * stand-in layer writing somvr_input's pointer block: the want bits and the v2 object, the guards (the game's answer,
 * then 0 while live, back after 0.5 s), the mapping with the view offset, moves only on a pixel change, a click's down
 * and up a tick apart, a click within one tick, the park, haptic ticks (on the quad, a hover, a click), the trigger's
 * owner per press (no LT/RT for the pointer's, the pad's kept for its whole press), a stale block, the world, the
 * front end without a player, no player at all, a HUD block without SOMVR_POINTER=hud.
 * SOMVR_VIGNETTE / SOMVR_SNAP_BLINK_MS / SOMVR_SEATED (comfort_tests, somvr_comfort.h): the fourth exported object and
 * its entry per stereo frame: the head's own motion not in it, Talion's walk on the head base, a smooth turn about the
 * head, a controller snap and a trigger word's snap held while the view darkens and landed on the black frame (the
 * view turned exactly there), the fade-in, a recentre and a teleport (the head base's fallback) not motion, the blink
 * off, seated height in LOCAL and STAGE (floor mode, a desk-height head rejected, SOMVR_HEIGHT_CM), the object off.
 * SOMVR_FALLBACK=auto against a stand-in Player with every field the detector reads, PlayerBody's state tags, the
 * camera, the presentation, photo mode, the cinematic manager, the sync action and struggles, the attach byte, named
 * records and its 17 read-only sites on stand-in code: calm first person, a cinematic (the fade out rising in the
 * records, the swap on the first black frame with the eyes on the camera, the HMD still applied, no arm rig and no
 * combat player in third person, back on the head node re-latched), 11 more conditions, short and long syncs, pause,
 * the trigger words (and through the file), mismatched sites, SOMVR_FALLBACK_ON, a plain cut, bad values, flat frames,
 * no head base.
 * SOMVR_CLIMB=b (climb_tests) against a stand-in IEnginePhysics cast (Talion's capsule, a wall of climbable stone and
 * rock, its top; the query checked against the combat collider's layout, the filter callback asked or not), the surface
 * lookups, a stand-in proxy with its gravity flag (and a player update with the wall and gravity) and its 8 read-only
 * sites on stand-in code: a hold (the flag 1 -> 0, a pulse), a 60 cm pull with the hand staying on its hold, the flag
 * kept 0, the wall clipping, hand over hand, no follow, the release (the flag 1, he falls), the stick off while holding,
 * Talion skipped by the filter and by stepping, rock, out of reach, the stand-downs (FSM 8, a mount, the input, a flat
 * frame, no climb step, the proxy changed: the old one untouched), the top's lift and cmd (LedgePullUp), off.
 * SOMVR_BOW=1 (bow_tests) against a stand-in free-aim call FUN_00a5b8b0 behind a real E8 call site, the read-only
 * proofs on stand-in code, our own shot's FUN_02332c50 / FUN_02720a90 stand-ins, the player's server Combat with the
 * arrows, its owner, inventory and the bow's record, and a stand-in game (the aim command's ON starts its bow mode after
 * 0.2 s, the mode asks for the free-aim ray every update): the idle pass-through, the nock (aim + draw, the aim
 * command's ON, a pulse, no blade), our ray from the nock along the draw, the release (our own shot once the game aims,
 * along the latched ray, an arrow less; no fire command, 125 never), the bow's forward while aiming, the ray carried
 * with Talion's walk, a second shot, the exit apart, the button, a dropped shot, refused shots (no arrows, not ranged,
 * no item, sites), unlimited, SOMVR_BOW_FIRE_CMD's tap, a menu, no hands, the arms' bow node (SOMVR_BOW_CAL, turned onto the shot),
 * off.
 * The review of the features together (review_tests, docs/re-xr-pipeline.md 16): the bone blade over a controller
 * snap and a recentre with the hands still (no swing, no contact with the orc in the snap's arc), the arms easing in
 * (no swing), no blade in the pause menu or with the sword hand on a climb hold, the head pieces shown in third person,
 * the fallback's swap a jump for comfort's speeds, the follow's step taken out of the arms' carried rig; with main's bow
 * (16.6): the follow's step out of the bow's ray, the nock next to a climbable wall (the bow, no hold), the bow in the
 * sword hand (no swing after it), a snap with the string hand lost (the draw line kept). And everything at once
 * (allon_tests): every switch on, the bow and the combat react too, for about 850 frames with a recentre, snaps, mode
 * switches, a fallback in and out, a climb grab and release, the bow drawn at the wall and shot, the HUD on and off,
 * checking the invariants of section 16, then a made swing (a hit, the react's roll).
 * SOMVR_VRMENU=1 (vm_tests, somvr_vrmenu.h) with the test as the layer on the fifth object: zero when off and without
 * SOMVR_INPUT, the header and avail, the settings file over the switches (a comment, blanks, a CR, quotes, a comma
 * decimal and an unknown key not taken), its values applied at start, every click applied live (the turn, the snap
 * angle the rig then turns by, the speed, the vignette, the blink, the haptics, the hand, seated play with its
 * recentre and latch, the view through the fallback), a torn read waiting, the panel's Recentre, the left menu
 * button's long press (the panel's toggle, never the game's) and short press (the game's at its release, world and
 * menu), the panel's trigger kept from the pad for its press, the save (1 s after the last change, the file's keys and
 * the changed ones, '.' decimals under a comma locale, read back the same, new directories, an unwritable path, the
 * writer thread on its own).
 * SOMVR_LOCO=1 and SOMVR_ANCHOR=root (loco_tests, somvr_loco.h) against a stand-in PlayerMovement::vf_11 that calls the
 * root delta through a real E8 call site (LOCO_ROOTDELTA), then the extra move, the locks (skipped in an attack) and the
 * transform, a stand-in attack test behind a real call (LOCO_ATTACK_Q), the lock getters in the PlayerMovement vtable,
 * Talion's object timer, a wall: standing (the game's sway dropped), 500 cm/s from the first tick and a stop in one, the
 * body yaw at 720 deg/s and the rotation lock, the owners (lock, attack, FSM 8, combat move, a climb) with their log
 * lines and the eased hand-off back, the leg feed at SOMVR_LOCO_SPEED 0.8, a wall, the follow in the same delta (with the
 * stick: both once; the extra move on the game's ticks), the anchor (the eyes 76.6 cm over the root while walking, the
 * head node's bob dropped, a 30 cm step eased, a teleport re-latched), off = the game's root motion and the head node.
 * SOMVR_THREAT=1 (threat_tests, somvr_threat.h) against the orcs of cb_world with their key times, target and attack
 * (Combat+0xc8.., +0x4c0, +0x4d0), stand-in proofs (K_CALL/K_ENTRY on stand-in code), GetNode for one orc's R_Hand, the
 * Wwise object's three calls and a stand-in CProjectile::vf_13 slot the hook goes into: the sixth object at start, an
 * attack in view (the glow at the R_Hand in the layer's space, peaking inside the parry window, gone after the impact,
 * no pulse, the parry tick on the sword hand), one behind on the left (the left edge, the announcement's left pulse and
 * the sound at the orc, LATE; the urgent pulse 0.35 s before the impact; the head turned: the glow, no new pulse), an
 * engaged orc close behind (the edge's low level) and at 4 m (none), a projectile from behind on the right (the right
 * edge and pulse; the head turned to it: the glow on it; gone 0.1 s after its last update), the pause (no cues, the
 * attack's clock held), flat frames (the track kept on its own clock and resumed; outlived: restarted; over 4 s:
 * dropped), the orc's own timer as the attack's clock (frozen, a third), a key-time proof mismatched and SOMVR_COMBAT off.
 * SOMVR_DEFENCE=1 (df_tests, somvr_defence.h) on THREAT's tracks of the same orcs, with a stand-in impact caller whose
 * real E8 call of a stand-in Combat_DealHit is DF_IMPACT, stand-in proofs, recoil and AttackData functions and the
 * database's RecoilTypeParry/RecoilTypeBlock: the parry (fired at the zone's first frame; the stock counter held 3 input
 * ticks, CombatPrimary02 in pad mode, CombatKBM09 in KBM mode; the parry pulse; the orc's attack replaced = countered;
 * not replaced = its impact stopped and its recoil with RecoilTypeParry from our contact), a late blade (a block, the
 * recoil with RecoilTypeBlock, took), no blade and a blade aside (the game's hit with the same arguments), the head 35 cm
 * aside (a dodge), a red attack and a shut window, two attackers (one stopped, one landing), an arrow removed before it
 * passes the blade and one aside flying on, log mode, off, a flat frame, the threat off and mismatched sites at start.
 * SOMVR_PRACTICE=1 (pr_tests, last) against stand-ins of its ten read-only sites (the template by name, the debug
 * spawn, the zone, the level, SetMinHealth, the TOD's name, the physics cast, the NPC's zone slot, the object system's
 * removal, CGameServices' weather state), the console (st_runstring's hook: Teleport moves Talion, AdvanceTimeOfDay and
 * ChangeWeather change the stand-in weather state) and band orcs around the spot on the fallback's Player: the trigger
 * words through the file, the blink there (fade, one teleport at black, the settle, the night made day and DayNoRain
 * under the black), min health 20 and back, the keep-clear passes (armed within 60 m; never ours, slaves, the dead, the
 * mount; held in a presentation), one orc at a time (the facing, the zone and level, a new one removing the old, the
 * menu's request word, down, removed, three failures), walking out and in, back with the light, a teleport that never
 * lands, auto once, a load, mismatched sites, a bad switch.
 * Run by ./build.sh --check. The stand-ins are noipa: GCC must not drop stores into buffers that only our hooks read
 * (IPA mod-ref would, legally).
 */
#define SOMVR_SELFTEST
#include "somvr.c"
#include <openxr/openxr.h>
#include <sys/wait.h>

static int fails;
#define EXPECT(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define NEAR(a, b, e) (fabs((double)(a) - (double)(b)) <= (e))

/* ---- stand-ins ------------------------------------------------------------------------------------------------- */
static const somvr_view GAME = { { 100, 200, 300 }, { 0, 0, 0, 1 }, 1.5f, 0.9f, 7.0f, 100000.0f };
static float g_seen_rect[4], g_seen_tan[2], g_seen_co[2], g_seen_corner_tan[2];
static int g_h3_type;

static float g_game_quat[4] = { 0, 0, 0, 1 }; /* bm_tests: the game camera turned (GAME's own quat otherwise) */
__attribute__((noipa)) uint64_t t_h1(void *cam, somvr_view *view, void *sp)
{
	(void)cam, (void)sp;
	*view = GAME;
	memcpy(view->quat, g_game_quat, sizeof g_game_quat);
	return 0;
}
__attribute__((noipa)) uint64_t t_h2(float nz, float fz, void *self, const float *xf, const float *fov,
						    const float *rect)
{
	(void)self, (void)xf, (void)fov;
	memcpy(g_seen_rect, rect, sizeof g_seen_rect);
	return nz > 0 && fz > nz;
}
__attribute__((noipa)) uint64_t t_h3(float nz, float fz, void *ds, const float *xf, float *tan_half,
						    float *center, int *prect, int type)
{
	(void)ds, (void)xf, (void)prect;
	memcpy(g_seen_tan, tan_half, sizeof g_seen_tan);
	memcpy(g_seen_co, center, sizeof g_seen_co);
	return nz > 0 && fz > nz && type == g_h3_type;
}
__attribute__((noipa)) uint64_t t_h6(float nz, float fz, void *corners, const float *xf, const float *th,
						    int ortho)
{
	(void)nz, (void)fz, (void)corners, (void)xf, (void)ortho;
	memcpy(g_seen_corner_tan, th, sizeof g_seen_corner_tan);
	return 0;
}
static int g_h9_tid_inside;
static uint64_t g_h9_tag_inside;
static int64_t g_h9_hud_gen; /* somvr_hud's entry for the tag inside Present (-1: none) */
__attribute__((noipa)) uint64_t t_h9(void *self, uint64_t sync, uint64_t flags)
{
	(void)self, (void)sync, (void)flags;
	g_h9_tid_inside = atomic_load(&g_sh.present_tid);
	g_h9_tag_inside = atomic_load(&g_sh.present_tag);
	somvr_hud_tag ht;
	g_h9_hud_gen = somvr_hud_tag_get(&g_hud, g_h9_tag_inside, &ht) ? (int64_t)ht.gen : -1;
	return 0;
}
typedef uint64_t (*t_h2_fn)(float, float, void *, const float *, const float *, const float *);
typedef uint64_t (*t_h9_fn)(void *, uint64_t, uint64_t);
static void *volatile g_vt[40], *volatile g_vt9[10];

__attribute__((noipa)) uint64_t c_h1(void *cam, somvr_view *view, void *sp) { return t_h1(cam, view, sp) + 1; }
/* FUN_0072c400 on the "render thread": tan(fov/2), offset 0, then the H3 call */
__attribute__((noipa)) uint64_t c_h3(void *ds, const float *xf, float *th, float *co, int *pr)
{
	return t_h3(7.0f, 100000.0f, ds, xf, th, co, pr, g_h3_type) + 1;
}
/* FUN_00810e80: the corners from the view state's tanHalf (offset behind it) */
__attribute__((noipa)) uint64_t c_h6(void *corners, const float *xf, const float *vs_c)
{
	return t_h6(7.0f, 100000.0f, corners, xf, vs_c, 0) + 1;
}

/* ---- stand-ins for H8: the DeviceState, rt3D, the EndRenderTarget executor, the IdxD3D11 / Feral objects ------- */
typedef struct { uint32_t state, pad; uintptr_t rt; } st_ds_t;                    /* +8 = the bound target */
typedef struct { uintptr_t vt; uint32_t flags; } st_rt_t;                         /* +8 = flags */
typedef struct { uintptr_t vt; char pad[0x100]; uint64_t image; } st_vktex_t;     /* +0x108 = VkImage */
typedef struct { uintptr_t vt; char pad0[0x78]; st_vktex_t *f3d; d3d_tex_desc desc; uint32_t content; int refs, id; } st_tex_t;
_Static_assert(offsetof(st_ds_t, rt) == DS_TARGET && offsetof(st_rt_t, flags) == RT_FLAGS &&
	       offsetof(st_vktex_t, image) == VKTEX_IMAGE && offsetof(st_tex_t, f3d) == TEX_F3D, "stand-in layouts");
__attribute__((used)) st_ds_t g_ds;
static st_rt_t g_rt3d = { 0x1234, 0x37d7 }, g_rt_other = { 0x1234, 0x37d7 };
static st_rt_t *g_rt_ptr = &g_rt3d, *g_bind_override; /* H4's RenderTarget**; a test binds another target */
static void *volatile st_vt_dev[8], *volatile st_vt_ctx[64], *volatile st_vt_ctx_mt[64], *volatile st_vt_tex[16],
	*volatile st_vt_vktex[16];
static struct { uintptr_t vt; } st_dev, st_ctx;
static st_tex_t g_surf, g_uisurf, g_tex_pool[48]; /* a bump pool: every (re-)made texture of ours takes the next one */
static st_vktex_t g_vk_pool[48];
static int g_ntex, g_creates, g_releases, g_create_fail, g_force_state = -1;
static char g_holder[0x200];
static struct { char k; int a; uint32_t v; } g_ev[64];
static int g_nev;
static void ev(char k, int a, uint32_t v)
{
	if (g_nev < 64)
		g_ev[g_nev].k = k, g_ev[g_nev].a = a, g_ev[g_nev].v = v, g_nev++;
}
__attribute__((noipa)) int32_t st_create(void *dev, const d3d_tex_desc *d, const void *init, void **out)
{
	(void)dev, (void)init;
	g_creates++;
	if (g_create_fail || g_ntex >= 48)
		return (int32_t)0x80004005;
	st_tex_t *t = &g_tex_pool[g_ntex];
	memset(t, 0, sizeof *t);
	t->vt = (uintptr_t)st_vt_tex, t->desc = *d, t->refs = 1, t->id = g_ntex;
	t->f3d = &g_vk_pool[g_ntex];
	t->f3d->vt = (uintptr_t)st_vt_vktex, t->f3d->image = 0; /* Feral makes the VkImage at the first use */
	g_ntex++;
	*out = t;
	return 0;
}
__attribute__((noipa)) void st_copy(void *ctx, void *dst, void *src)
{
	(void)ctx;
	st_tex_t *d = dst, *sr = src;
	d->content = sr->content;
	if (!d->f3d->image)
		d->f3d->image = 0xE000u + (uint64_t)d->id;
	ev('C', d->id, d->content);
}
__attribute__((noipa)) void st_copy_mt(void *ctx, void *dst, void *src) { st_copy(ctx, dst, src); }
__attribute__((noipa)) void st_getdesc(void *tex, d3d_tex_desc *out) { *out = ((st_tex_t *)tex)->desc; }
__attribute__((noipa)) uint32_t st_release(void *tex) { g_releases++; return (uint32_t)--((st_tex_t *)tex)->refs; }
__attribute__((noipa)) void st_vkcopy(void *a, void *b) { (void)a, (void)b; }
/* FUN_0072ac00: ends the bound target (state 3 -> 2, the reference dropped) */
__attribute__((used, noipa)) uint64_t t_h8(void *ds, uint32_t flags)
{
	st_ds_t *d = ds;
	ev('E', (int)flags, (uint32_t)d->state);
	if (d->state != 3)
		return 0;
	d->rt = 0, d->state = 2;
	return 1;
}
/* FUN_007c9de0: mov edi, DeviceState; jmp FUN_0072ac00 (an E9 rel32, the K_JMP site) */
uint64_t c_h8(uint64_t unused, uint32_t flags);
__asm__(".text\n.p2align 4\n.globl c_h8\n.type c_h8,@function\nc_h8:\n  lea g_ds(%rip), %rdi\n  .byte 0xe9\n"
	"  .long t_h8 - . - 4\n.size c_h8, .-c_h8\n");
/* SOMVR_HUD (H11): FUN_00a11dc0(im, player, beginTarget, clear), the HUD pass after each eye's EndRenderTarget: it
 * draws the HUD of (frame, eye) into the UI surface; its two call sites (the player view, a full-screen video); and
 * FUN_00742d50(dev), the present composite that samples the UI surface over the 3D surface */
static uint64_t g_h4_frame; /* the frame being rendered (set by frame(), below) */
static char g_im_obj[16];
static int g_hud_k, g_hud_draws, g_hud_video, g_hud_none, g_comp_calls;
static uint64_t g_hud_ret[4];
__attribute__((noipa)) uint64_t t_hud(void *im, void *pl, uint32_t begin, uint32_t clear)
{
	(void)pl;
	g_hud_draws++;
	g_uisurf.content = 0xB000u + (uint32_t)g_h4_frame * 4u + (uint32_t)g_hud_k;
	ev('H', g_hud_k, begin * 16u + clear);
	return im == (void *)g_im_obj ? 7 : 70;
}
__attribute__((noipa)) uint64_t c_hudeye(void *im, void *pl) { return t_hud(im, pl, 1, 1) + 1; }
__attribute__((noipa)) uint64_t c_hudcov(void *im) { return t_hud(im, NULL, 1, 1) + 2; }
__attribute__((noipa)) uint64_t t_comp(void *dev)
{
	g_comp_calls++;
	ev('P', 0, g_uisurf.content);
	return dev == (void *)g_holder ? 3 : 30;
}
__attribute__((noipa)) uint64_t c_comp(void *dev) { return t_comp(dev) + 1; }
static const d3d_tex_desc SURF = { 1832, 1920, 1, 1, 28, 1, 0, 0, 0x28, 0, 0 };
static const d3d_tex_desc UISURF = { 1920, 1080, 1, 1, 28, 1, 0, 0, 0x28, 0, 0 }; /* InterfaceWidth/Height != 3D size */
static void d3d_world(void)
{
	st_vt_dev[5] = (void *)st_create;
	st_vt_ctx[47] = (void *)st_copy, st_vt_ctx_mt[47] = (void *)st_copy_mt;
	st_vt_tex[2] = (void *)st_release, st_vt_tex[10] = (void *)st_getdesc;
	st_vt_vktex[10] = st_vt_vktex[11] = (void *)st_vkcopy;
	st_dev.vt = (uintptr_t)st_vt_dev, st_ctx.vt = (uintptr_t)st_vt_ctx;
	g_surf.vt = (uintptr_t)st_vt_tex, g_surf.desc = SURF, g_surf.refs = 1, g_surf.id = 99;
	g_uisurf.vt = (uintptr_t)st_vt_tex, g_uisurf.desc = UISURF, g_uisurf.refs = 1, g_uisurf.id = 98;
	void *dev = &st_dev, *ctx = &st_ctx, *surf = &g_surf, *ui = &g_uisurf;
	memcpy(g_holder, &dev, 8), memcpy(g_holder + HOLDER_CTX, &ctx, 8), memcpy(g_holder + HOLDER_TEX3D, &surf, 8);
	memcpy(g_holder + HOLDER_TEXUI, &ui, 8);
	g_d3d.holder = (uintptr_t)g_holder, g_d3d.vt_dev = (uintptr_t)st_vt_dev, g_d3d.vt_ctx = (uintptr_t)st_vt_ctx;
	g_d3d.vt_ctx_mt = (uintptr_t)st_vt_ctx_mt, g_d3d.vt_tex = (uintptr_t)st_vt_tex, g_d3d.vt_vktex = (uintptr_t)st_vt_vktex;
}

/* ---- stand-ins for AUDIO, SOMVR_BASE=head and SOMVR_FOLLOW: CSoundMgr::vf_23, the player's objects (Player,
 * PlayerBody, the animation manager, the engine object), GetNodeTransform, PlayerMovement::vf_11 and the collision -- */
static struct { int n; const float *pos, *front, *top; uint32_t idx; float p[3], f[3], t[3]; } g_aud;
__attribute__((noipa)) uint64_t t_aud(void *self, const float *pos, const float *front, const float *top, uint32_t idx)
{
	(void)self;
	g_aud.n++, g_aud.pos = pos, g_aud.front = front, g_aud.top = top, g_aud.idx = idx;
	memcpy(g_aud.p, pos, sizeof g_aud.p), memcpy(g_aud.f, front, sizeof g_aud.f), memcpy(g_aud.t, top, sizeof g_aud.t);
	return 1;
}
typedef uint64_t (*t_aud_fn)(void *, const float *, const float *, const float *, uint32_t);
static void *volatile g_vta[30]; /* CSoundMgr's vtable: slot 23 = vf_23 (hook AUDIO) */

typedef struct { uintptr_t vt; float pos[3], rot[4]; char pad[0x38 - 0x24]; void *iface; } st_obj_t;
typedef struct { uintptr_t vt; } st_iface_t;
typedef struct { char pad[0x560]; st_obj_t *hchar; } st_mgr_t;
typedef struct { char pad0[0x1b8]; st_obj_t *obj; char pad1[0x3b8 - 0x1c0]; st_mgr_t *mgr; } st_body_t;
typedef struct {
	uintptr_t vt;
	char pad0[0x218 - 8];
	void *owner;
	char pad1[0x428 - 0x220];
	float xpos[3], xrot[4];
	uint8_t pending;
} st_pm_t;
typedef struct { char pad0[0x2c8]; st_pm_t *pm; void *cam; st_body_t *body; void *pad1; void *pcomb; } st_player_t;
_Static_assert(offsetof(st_obj_t, pos) == OBJ_POS && offsetof(st_obj_t, rot) == OBJ_ROT &&
	       offsetof(st_obj_t, iface) == OBJ_MODEL && offsetof(st_mgr_t, hchar) == MGR_CHAR &&
	       offsetof(st_body_t, obj) == BODY_OBJ && offsetof(st_body_t, mgr) == BODY_MGR && offsetof(st_pm_t, owner) == PM_OWNER &&
	       offsetof(st_pm_t, xpos) == PM_XPOS && offsetof(st_pm_t, xrot) == PM_XROT && offsetof(st_pm_t, pending) == PM_PENDING &&
	       offsetof(st_player_t, pm) == P_PM && offsetof(st_player_t, body) == P_BODY &&
	       offsetof(st_player_t, pcomb) == P_COMBAT, "stand-in player layouts");
static st_obj_t g_pobj;
static st_iface_t g_piface;
static void *volatile g_vti[8];    /* the model interface's vtable: slot 4 = FUN_008f3c20's call (code) */
static st_mgr_t g_pmgr;
static st_body_t g_pbody;
static st_pm_t g_ppm, g_ppm2;      /* the player's PlayerMovement, and another one */
static st_player_t g_player, *g_pl; /* g_pl: H4's Player* (NULL: none) */
static void *volatile g_vtpm[16];  /* PlayerMovement's vtable: slot 11 = vf_11 (hook PM) */
static void *volatile g_vtm[32];   /* IEngineModel's vtable: slot 23 = GetNodeTransform (read only, G12_NODEXF) */
static struct { uintptr_t vt; } g_model;
static void *g_model_ptr = &g_model; /* *0x46a9230 */
static float g_head_up = 72.5f;    /* the Head node above the root (run 3) */
static int g_nodexf_fail, g_nodexf_nan, g_nodexf_calls;
static const sa_node *volatile g_nodexf_node; /* set: the node read from the player's pose buffer, as the game does */
__attribute__((noipa)) uint64_t st_nodexf(void *m, uintptr_t obj, uint32_t node, float *xf, int world)
{
	const st_obj_t *o = (const st_obj_t *)obj;
	g_nodexf_calls++;
	if (m != g_model_ptr || node != 0x72598328u || world != 1 || g_nodexf_fail)
		return 1; /* the stub returns result ^ 1: 0 = ok */
	xf[0] = o->pos[0], xf[1] = o->pos[1] + g_head_up, xf[2] = o->pos[2];
	memcpy(xf + 3, o->rot, sizeof o->rot);
	if (g_nodexf_node) { /* the object's transform x the node's model-space transform (re-firstperson.md 2.1) */
		sa_xf w = sa_xf_mul((sa_xf){ { o->pos[0], o->pos[1], o->pos[2] }, sv_qnormalize(sv_quat_of(o->rot)) },
				    sa_node_xf(g_nodexf_node));
		xf[0] = w.p.x, xf[1] = w.p.y, xf[2] = w.p.z;
		sv_quat_to(w.q, xf + 3);
	}
	if (g_nodexf_nan)
		xf[1] = NAN;
	return 0;
}
__attribute__((noipa)) uintptr_t st_getmodel(void *iface) { return (uintptr_t)iface; }
static float g_rm[3];              /* vf_11's own root motion per tick (Talion's frame: the stick) */
static float g_seen_extra[3];
static int g_pm_calls, g_pm_consumed;
/* PlayerMovement::vf_11 (re-m1m2.md 3.2; the root motion's rotation is the identity here) */
__attribute__((noipa)) uint64_t t_pm(void *pmv, float *xf)
{
	st_pm_t *pm = pmv;
	sv_vec3 d = { g_rm[0], g_rm[1], g_rm[2] };
	g_pm_calls++;
	if (pm->pending) { /* step 2: the extra rides on the root motion, then it is reset */
		g_pm_consumed++;
		memcpy(g_seen_extra, pm->xpos, sizeof g_seen_extra);
		d.x += pm->xpos[0], d.y += pm->xpos[1], d.z += pm->xpos[2];
		memset(pm->xpos, 0, sizeof pm->xpos);
		pm->xrot[0] = pm->xrot[1] = pm->xrot[2] = 0, pm->xrot[3] = 1;
		pm->pending = 0;
	}
	sv_vec3 w = sv_qrot(sv_qnormalize((sv_quat){ xf[3], xf[4], xf[5], xf[6] }), d); /* step 4 */
	xf[0] += w.x, xf[1] += w.y, xf[2] += w.z;
	return 0;
}
typedef uint64_t (*t_pm_fn)(void *, float *);
/* the player update: vf_11 through the vtable, then the collision (a wall at z = g_wall_z) puts the root down */
static int g_wall;
static float g_wall_z, g_wall_top = INFINITY; /* SOMVR_CLIMB's tests: the wall blocks only below its top */
static const volatile unsigned char *g_grav_flag; /* SOMVR_CLIMB's tests: the stand-in proxy's flag (gravity 4 cm/tick) */
static float g_ground_y;
static void player_update(void)
{
	float xf[7];
	memcpy(xf, g_pobj.pos, sizeof g_pobj.pos), memcpy(xf + 3, g_pobj.rot, sizeof g_pobj.rot);
	((t_pm_fn)g_vtpm[11])(&g_ppm, xf);
	if (g_wall && xf[2] > g_wall_z && xf[1] < g_wall_top)
		xf[2] = g_wall_z;
	if (g_grav_flag && *g_grav_flag && xf[1] > g_ground_y)
		xf[1] = fmaxf(g_ground_y, xf[1] - 4.0f);
	memcpy(g_pobj.pos, xf, sizeof g_pobj.pos);
}
static void player_world(void)
{
	g_vti[4] = (void *)st_getmodel;
	g_piface.vt = (uintptr_t)g_vti;
	g_pobj.vt = 0x7777, g_pobj.iface = &g_piface;
	g_pobj.pos[0] = 500, g_pobj.pos[1] = 100, g_pobj.pos[2] = -50, g_pobj.rot[3] = 1;
	g_pmgr.hchar = &g_pobj;
	g_pbody.obj = &g_pobj, g_pbody.mgr = &g_pmgr;
	g_ppm.vt = g_ppm2.vt = (uintptr_t)g_vtpm;
	g_ppm.owner = &g_player, g_ppm2.owner = &g_ppm2;
	g_ppm.xrot[3] = g_ppm2.xrot[3] = 1;
	g_player.pm = &g_ppm, g_player.body = &g_pbody;
	g_model.vt = 0x8888;
}

/* ---- stand-ins for SOMVR_EYE_SIZE: the mode set's 3D desc (FUN_0073fce0 from FUN_00741980) and the 3D rect
 * (FUN_00d28b50 from FUN_009e6b50 and the shell init) ------------------------------------------------------------ */
typedef struct { uint32_t w, h, refresh[2], format; } st_desc_t; /* +0 w, +4 h, +0x10 DXGI format */
static st_desc_t g_desc3d;
static int32_t g_rect3d[4];
static uint64_t g_rect_lb_seen;
__attribute__((noipa)) uint64_t t_desc(void *dev, void *desc, uint32_t w, uint32_t h, uint32_t windowed, uint32_t flag)
{
	st_desc_t *d = desc;
	(void)dev, (void)windowed;
	d->w = w, d->h = h, d->format = 28 + flag;
	return 0;
}
/* FUN_00741980: the 3D desc from ScreenWidth and the (snapped) back buffer's aspect */
__attribute__((noipa)) uint64_t c_desc(void *dev, uint32_t sw, uint32_t bbw, uint32_t bbh)
{
	return t_desc(dev, &g_desc3d, sw, (uint32_t)((float)sw * (float)bbh / (float)bbw + 0.5f), 1, 0) + 1;
}
/* FUN_00d28b50(rect, letterbox, ui): the 3D size, or with letterbox a centred 16:9 band of it */
__attribute__((noipa)) uint64_t t_rect(void *rect, uint64_t letterbox, uint64_t ui)
{
	int32_t *r = rect, w = (int32_t)g_desc3d.w, h = (int32_t)g_desc3d.h, hb = w * 9 / 16;
	g_rect_lb_seen = letterbox;
	if (letterbox && !ui && hb < h)
		r[0] = 0, r[1] = (h - hb) / 2, r[2] = w, r[3] = (h - hb) / 2 + hb;
	else
		r[0] = 0, r[1] = 0, r[2] = w, r[3] = h;
	return 0;
}
__attribute__((noipa)) uint64_t c_rect(uint64_t lb) { return t_rect(g_rect3d, lb, 0) + 1; }
__attribute__((noipa)) uint64_t c_rect_init(uint64_t lb) { return t_rect(g_rect3d, lb, 0) + 2; }

/* ---- stand-ins for SOMVR_HIDE_HEAD: the player's CSimpleModel, its renderable, the render node handle and the node,
 * the IEngineModel stubs, and the opaque stage's shadow calls ------------------------------------------------------- */
typedef struct { uintptr_t vt; char pad[0x130 - 8]; uint32_t mask[2]; } st_node_t;
typedef struct { uintptr_t vt, pad; st_node_t node; } st_handle_t;
typedef struct { uintptr_t vt; char pad[0x40 - 8]; uint8_t dirty; char p1[3]; uint32_t mask[2], p2; st_handle_t *handle; } st_rend_t;
typedef struct {
	uintptr_t vt;
	char pad[0x68 - 8];
	uint8_t dirty;
	char p1[3];
	uint32_t mask[2];
	char p2[0x88 - 0x74];
	void *cloth;                     /* SOMVR_HIDE_HEAD=parts' cloth: the CSimpleModelClothNode (hp_tests) */
	char p3[0xb0 - 0x90];
	st_rend_t *rend;
	st_rend_t *slot[SOMVR_HP_SLOTS]; /* SOMVR_HIDE_HEAD=parts: the part renderables (vf_20's) */
} st_smodel_t;
_Static_assert(offsetof(st_node_t, mask) == NODE_MASK && offsetof(st_handle_t, node) == HANDLE_NODE &&
	       offsetof(st_rend_t, dirty) == 0x40 && offsetof(st_rend_t, mask) == 0x44 && offsetof(st_rend_t, handle) == REND_HANDLE &&
	       offsetof(st_smodel_t, dirty) == 0x68 && offsetof(st_smodel_t, mask) == SM_MASK && offsetof(st_smodel_t, rend) == SM_REND &&
	       offsetof(st_smodel_t, slot) == SM_SLOTS && offsetof(st_smodel_t, cloth) == SM_CLOTH,
	       "stand-in model layouts");
static void *volatile g_vt_smodel[170], *volatile g_vt_rend[8], *volatile g_vt_handle[56], *volatile g_vt_node[8];
static st_node_t g_dummy_node;
static st_smodel_t g_sm, g_sm2;
static st_rend_t g_srend, g_srend2;
static st_handle_t g_shandle, g_shandle2;
static st_smodel_t *g_obj_model = &g_sm; /* what FUN_008f3c20 returns for the player's object (NULL: none) */
static int g_hide_calls, g_show_calls, g_modelof_calls;
/* CRenderNodeMeshSkeletal::vf_05 (the render-thread command): the mask */
__attribute__((noipa)) void st_node_setmask(void *node, uint32_t lo, uint32_t hi)
{
	st_node_t *n = node;
	n->mask[0] = lo, n->mask[1] = hi;
}
/* TRenderNodeMeshSkeletalHandle slot 53: queues vf_05 for the node at +0x10 (here it runs at once: the render update
 * below is called at the start of each RenderScene, on the "render thread") */
__attribute__((noipa)) void st_handle_setmask(void *h, uint32_t lo, uint32_t hi)
{
	((void (*)(void *, uint32_t, uint32_t))g_vt_node[5])((char *)h + HANDLE_NODE, lo, hi);
}
/* FUN_02382080(renderable, &model->dirty): dirty + the mask copy */
__attribute__((noipa)) void st_rend_mask(void *rend, const void *src)
{
	st_rend_t *r = rend;
	r->dirty = 1;
	memcpy(r->mask, (const char *)src + 4, sizeof r->mask);
}
/* CSimpleModelRenderable::vf_02 (the render update): a changed mask goes to the handle */
__attribute__((noipa)) uint64_t st_rend_update(void *rend)
{
	st_rend_t *r = rend;
	if (r->dirty)
		((void (*)(void *, uint32_t, uint32_t))g_vt_handle[53])(r->handle, r->mask[0], r->mask[1]), r->dirty = 0;
	return 0;
}
/* CSimpleModel::vf_50 SetPieceHidden(piece, hidden) */
__attribute__((noipa)) uint64_t st_vf50(void *model, uint32_t piece, uint32_t hidden)
{
	st_smodel_t *m = model;
	if (piece > 63)
		return 0;
	uint32_t bit = 1u << (piece & 31), *w = &m->mask[piece >> 5];
	*w = hidden ? *w | bit : *w & ~bit;
	m->dirty = 1;
	st_rend_mask(m->rend, &m->dirty);
	return 1;
}
__attribute__((noipa)) uintptr_t st_getrend(void *model) { return (uintptr_t)((st_smodel_t *)model)->rend; }
/* FUN_008f3c20(obj) -> the CSimpleModel */
__attribute__((noipa)) uintptr_t st_modelof(uintptr_t obj)
{
	g_modelof_calls++;
	return obj == (uintptr_t)&g_pobj ? (uintptr_t)g_obj_model : 0;
}
/* IEngineModel slot 7 (the stub 0x91b310): FUN_008f3c20(obj)->vf_50(piece, hidden) ^ 1, 0x3c without a model */
__attribute__((noipa)) uint64_t st_hidepiece(void *m, uintptr_t obj, uint32_t piece, uint32_t hidden)
{
	uintptr_t sm = st_modelof(obj);
	(void)m;
	if (!sm)
		return 0x3c;
	if (hidden)
		g_hide_calls++;
	else
		g_show_calls++;
	return ((uint64_t (*)(void *, uint32_t, uint32_t))((void **)((st_smodel_t *)sm)->vt)[50])((void *)sm, piece, hidden) ^ 1;
}
/* IEngineModel slot 3 GetNumPieces: 17 (run 3) */
__attribute__((noipa)) uint64_t st_numpieces(void *m, uintptr_t obj, uint32_t *count)
{
	(void)m, (void)obj;
	*count = 17;
	return 0;
}
static void model_world(void)
{
	g_vt_smodel[50] = (void *)st_vf50, g_vt_smodel[162] = (void *)st_getrend;
	g_vt_rend[2] = (void *)st_rend_update, g_vt_handle[53] = (void *)st_handle_setmask, g_vt_node[5] = (void *)st_node_setmask;
	st_smodel_t *m[2] = { &g_sm, &g_sm2 };
	st_rend_t *r[2] = { &g_srend, &g_srend2 };
	st_handle_t *h[2] = { &g_shandle, &g_shandle2 };
	for (int i = 0; i < 2; i++) {
		memset(m[i], 0, sizeof *m[i]), memset(r[i], 0, sizeof *r[i]), memset(h[i], 0, sizeof *h[i]);
		m[i]->vt = (uintptr_t)g_vt_smodel, m[i]->rend = r[i];
		r[i]->vt = (uintptr_t)g_vt_rend, r[i]->handle = h[i];
		h[i]->vt = (uintptr_t)g_vt_handle, h[i]->node.vt = (uintptr_t)g_vt_node;
	}
}
static st_node_t *player_node(void)
{
	return g_obj_model && g_obj_model->rend && g_obj_model->rend->handle ? &g_obj_model->rend->handle->node : &g_dummy_node;
}
/* the opaque stage: the node's mask (low word) as each pass sees it, per eye call: sky shadow, G-buffer, light bucket,
 * sun cascades, deferred lights; and the arguments the shadow calls got */
static uint32_t g_scm[4][5];
static int g_sck;
static uint64_t g_sca[4];
static float g_scf[2];
__attribute__((noipa)) uint64_t t_shpre(void *a, void *b)
{
	g_scm[g_sck][0] = player_node()->mask[0], g_sca[0] = (uintptr_t)a * 1000 + (uintptr_t)b;
	return 1;
}
__attribute__((noipa)) uint64_t t_shlights(void *a, void *b, void *c, uint32_t d, void *e)
{
	g_scm[g_sck][2] = player_node()->mask[0], g_sca[1] = (uintptr_t)a + (uintptr_t)b + (uintptr_t)c + d + (uintptr_t)e;
	return 2;
}
__attribute__((noipa)) uint64_t t_shcasc(float x0, float x1, void *a, void *b)
{
	g_scm[g_sck][3] = player_node()->mask[0], g_scf[0] = x0, g_scf[1] = x1, g_sca[2] = (uintptr_t)a + (uintptr_t)b;
	return 3;
}
__attribute__((noipa)) uint64_t t_shlights2(void *a, void *b, void *c, void *d, uint32_t e)
{
	g_scm[g_sck][4] = player_node()->mask[0], g_sca[3] = (uintptr_t)a + (uintptr_t)b + (uintptr_t)c + (uintptr_t)d + e;
	return 4;
}
__attribute__((noipa)) uint64_t c_shpre(void) { return t_shpre((void *)0x12, (void *)0x34) + 10; }
__attribute__((noipa)) uint64_t c_shlights(void) { return t_shlights((void *)0x100, (void *)0x200, (void *)0x300, 0, (void *)0x400) + 10; }
__attribute__((noipa)) uint64_t c_shcasc(void) { return t_shcasc(1.5f, 2.5f, (void *)0x500, (void *)0x600) + 10; }
__attribute__((noipa)) uint64_t c_shlights2(void) { return t_shlights2((void *)0x1, (void *)0x2, (void *)0x3, (void *)0x4, 5) + 10; }
static int g_sc_bad;

/* ---- stand-ins for SOMVR_HIDE_HEAD=parts: FUN_0081ff80 (the camera passes' skeletal submitter) behind its five builder
 * calls, FUN_0081fd10 (the shadow maps', never hooked); each eye's scene draws every node of g_hpn in all six ---------- */
#define HPN 8
static uintptr_t g_hpn[HPN];                    /* the nodes the passes draw (0 = none) */
static uint32_t g_hpd[5][SOMVR_HP_PASSES + 1];  /* [eye k, 4 = outside an eye][pass, the last = shadow]: bit i = g_hpn[i] */
static int g_hp_k = 4;
static uint64_t g_hpa_bad, g_hpa_calls;
static void hp_mark(int pass, uint64_t node)
{
	for (int i = 0; i < HPN; i++)
		if (g_hpn[i] && g_hpn[i] == node)
			g_hpd[g_hp_k][pass] |= 1u << i;
}
/* FUN_0081ff80(ctx, node, dl, cl, list, pos): what the hook passed on must be what the builder gave */
__attribute__((noipa)) uint64_t t_hp_submit(uint64_t ctx, uint64_t node, uint64_t a, uint64_t b, uint64_t list, uint64_t pos)
{
	int pass = (int)(ctx - 0x100);
	g_hpa_calls++;
	if (pass < 0 || pass >= SOMVR_HP_PASSES || (a & 0xff) != (uint64_t)(1 + pass) || (b & 0xff) != (uint64_t)(2 + pass) ||
	    list != 0x5000 + node || pos != 0x6000 + node) {
		g_hpa_bad++;
		return 0;
	}
	hp_mark(pass, node);
	return 0x70 + (uint64_t)pass;
}
__attribute__((noipa)) uint64_t t_hp_shadow(uint64_t node)
{
	hp_mark(SOMVR_HP_PASSES, node);
	return 0x80;
}
#define HP_CALLER(nm, P)                                                                                                    \
	__attribute__((noipa)) uint64_t nm(uint64_t n) { return t_hp_submit(0x100 + (P), n, 1 + (P), 2 + (P), 0x5000 + n, 0x6000 + n) + 1; }
HP_CALLER(c_hp_deep, SOMVR_HP_DEEP)
HP_CALLER(c_hp_fwd, SOMVR_HP_FWD)
HP_CALLER(c_hp_late, SOMVR_HP_LATE)
HP_CALLER(c_hp_gbuf, SOMVR_HP_GBUF)
HP_CALLER(c_hp_vel, SOMVR_HP_VEL)
/* the cloth: FUN_007ccec0(dl, obj, node, pass 6, flag) behind its eight builder calls (custom nodes), its shadow calls
 * (pass 7, never hooked); each eye's scene draws every node of g_hpcn through all nine */
#define HPCN 6
static uintptr_t g_hpcn[HPCN];
static uint32_t g_hpcd[5][HPC_N + 1]; /* [eye k, 4 = outside][call, the last = shadow]: bit i = g_hpcn[i] */
static uint64_t g_hpca_bad, g_hpca_calls, g_hpca_after;
static void hpc_mark(int call, uint64_t node)
{
	for (int i = 0; i < HPCN; i++)
		if (g_hpcn[i] && g_hpcn[i] == node)
			g_hpcd[g_hp_k][call] |= 1u << i;
}
__attribute__((noipa)) void t_hpc_submit(uint64_t dl, uint64_t obj, uint64_t node, uint64_t pass, uint64_t flag)
{
	int call = (int)(dl - 0x200);
	g_hpca_calls++;
	if (call < 0 || call >= HPC_N || obj != node + 8 || (pass & 0xffffffff) != 6 || (flag & 0xffffffff) != (call & 1 ? 0u : 2u)) {
		g_hpca_bad++;
		return;
	}
	hpc_mark(call, node);
}
__attribute__((noipa)) void t_hpc_shadow(uint64_t node) { hpc_mark(HPC_N, node); }
#define HPC_CALLER(nm, C)                                                                                                   \
	__attribute__((noipa)) void nm(uint64_t n)                                                                          \
	{                                                                                                                    \
		t_hpc_submit(0x200 + (C), n + 8, n, 6, (C) & 1 ? 0 : 2);                                                      \
		g_hpca_after++; /* not a tail call: the call site stays a call */                                             \
	}
HPC_CALLER(c_hpc_0, 0)
HPC_CALLER(c_hpc_1, 1)
HPC_CALLER(c_hpc_2, 2)
HPC_CALLER(c_hpc_3, 3)
HPC_CALLER(c_hpc_4, 4)
HPC_CALLER(c_hpc_5, 5)
HPC_CALLER(c_hpc_6, 6)
HPC_CALLER(c_hpc_7, 7)
static void (*const g_hpc_callers[HPC_N])(uint64_t) = { c_hpc_0, c_hpc_1, c_hpc_2, c_hpc_3, c_hpc_4, c_hpc_5, c_hpc_6, c_hpc_7 };
/* one view's draws: the five camera passes, then the shadow maps; the custom nodes' eight calls and their shadow;
 * k = the eye (4 = not an eye) */
static void hp_draws(int k)
{
	g_hp_k = k;
	for (int i = 0; i < HPN; i++) {
		if (!g_hpn[i])
			continue;
		uint64_t n = g_hpn[i];
		c_hp_deep(n), c_hp_fwd(n), c_hp_late(n), c_hp_gbuf(n), c_hp_vel(n); /* their results are not read (as the game's) */
		t_hp_shadow(n);
	}
	for (int i = 0; i < HPCN; i++) {
		if (!g_hpcn[i])
			continue;
		for (int c = 0; c < HPC_N; c++)
			g_hpc_callers[c](g_hpcn[i]);
		t_hpc_shadow(g_hpcn[i]);
	}
	g_hp_k = 4;
}

/* RenderScene of one eye: the render update first (a changed mask reaches the node), then the opaque stage */
static void scene(int k)
{
	if (g_obj_model && g_obj_model->rend && g_obj_model->rend->vt == (uintptr_t)g_vt_rend)
		((uint64_t (*)(void *))g_vt_rend[2])(g_obj_model->rend);
	g_sck = k;
	g_sc_bad += c_shpre() != 11;
	g_scm[k][1] = player_node()->mask[0]; /* the G-buffer build (HEAD_GBUF_DRAW_B): never swapped */
	g_sc_bad += c_shlights() != 12;
	g_sc_bad += c_shcasc() != 13;
	g_sc_bad += c_shlights2() != 14;
	hp_draws(k < 4 ? k : 3); /* SOMVR_HIDE_HEAD=parts: the camera passes and the shadow maps of g_hpn (none unless hp_tests) */
}

/* the per-view body: like FUN_009ee170 it builds the view (H1), opens the viewport (vf_37 = H2), the render thread
 * turns it into tangents (H3) and a frame context (H6), and it changes its per-call state */
static unsigned char g_fol[FOLIAGE_LEN];
static char g_renderer_obj[0x100];
static int g_h4_calls, g_h4_first[4], g_h4_bad_state;
static uint64_t g_h4_frame;
static somvr_view g_h4_view[4];
static float g_h4_rect[4][4], g_h4_tan[4][2], g_h4_co[4][2], g_h4_corner[4][2], g_h4_ov[4][4];
/* build 24 (SOMVR_EYE_MATCH): 1 = the render thread behind the game thread - eye 0's pass (H3, H6, H8, the HUD) runs
 * after eye 1's view is queued (H1), as in the game; 0 = each eye's pass right after its view */
static int g_h4_defer;
static void h4_render(int k, void *rt, void *pl, int ef);
__attribute__((noipa)) uint64_t t_h4(void *gm, void *rt, const int *org, void *pl, int ef, int first)
{
	(void)rt, (void)org, (void)pl, (void)ef;
	int k = g_h4_calls++;
	if (k < 4) {
		char cam[0x510] = { 0 };
		float player[3] = { 1000, 50, -20 };
		memcpy(cam + CAM_PLAYER_POS, player, sizeof player);
		somvr_view view;
		g_h4_first[k] = first;
		if (g_fol[0] != 0x11 || ((unsigned char *)gm)[0x20] != 0x22)
			g_h4_bad_state++;
		c_h1(cam, &view, NULL);
		g_h4_view[k] = view;
		float rect[4] = { 0, 0, 1, 1 };
		((t_h2_fn)g_vt[37])(view.near_z, view.far_z, g_renderer_obj, view.pos, &view.fov_x, rect);
		memcpy(g_h4_rect[k], g_seen_rect, sizeof g_h4_rect[k]);
		if (!g_h4_defer)
			h4_render(k, rt, pl, ef);
		else if (k == 1) /* both views queued: eye 0's pass, then eye 1's */
			h4_render(0, rt, pl, ef), h4_render(1, rt, pl, ef);
		memcpy((char *)gm + GM_LAST_VIEW, &view, sizeof view); /* the body's own "last view" copy */
	}
	g_fol[0] = 0x99;
	((unsigned char *)gm)[0x20] = 0x77;
	return 5;
}
/* the render thread's part of body call k: the view's projection (H3, an overlay with the same camera, H6), the eye's
 * pass into the 3D surface ending at H8, the HUD */
static void h4_render(int k, void *rt, void *pl, int ef)
{
	{
		somvr_view view = g_h4_view[k];
		float xf[7], th[2] = { tanf(0.5f * view.fov_x), tanf(0.5f * view.fov_y) }, co[2] = { 0, 0 };
		int pr[4] = { 0, 0, 1920, 1080 };
		memcpy(xf, &view, sizeof xf); /* the command's copy of the transform */
		c_h3(NULL, xf, th, co, pr);
		memcpy(g_h4_tan[k], g_seen_tan, sizeof g_h4_tan[k]);
		memcpy(g_h4_co[k], g_seen_co, sizeof g_h4_co[k]);
		float vs_c[4] = { g_seen_tan[0], g_seen_tan[1], g_seen_co[0], g_seen_co[1] }, corners[24];
		/* an overlay scene drawn with the same camera (FUN_00d8e180): its own vf_37, the same transform */
		float th2[2] = { tanf(0.5f * view.fov_x), tanf(0.5f * view.fov_y) }, co2[2] = { 0, 0 };
		c_h3(NULL, xf, th2, co2, pr);
		g_h4_ov[k][0] = g_seen_tan[0], g_h4_ov[k][1] = g_seen_tan[1], g_h4_ov[k][2] = g_seen_co[0], g_h4_ov[k][3] = g_seen_co[1];
		c_h6(corners, xf, vs_c);
		memcpy(g_h4_corner[k], g_seen_corner_tan, sizeof g_h4_corner[k]);
		/* BeginRenderTarget(*rt) ... RenderScene (the eye's pixels in the 3D surface) ... EndRenderTarget(ef) -> H8,
		 * then the HUD (after vf_34) */
		ev('B', k, 0);
		g_ds.state = g_force_state >= 0 ? (uint32_t)g_force_state : 3;
		g_ds.rt = g_bind_override ? (uintptr_t)g_bind_override : rt ? (uintptr_t) * (st_rt_t **)rt : 0;
		scene(k);
		g_surf.content = 0xA000u + (uint32_t)g_h4_frame * 4u + (uint32_t)k;
		c_h8(0, (uint32_t)ef);
		g_hud_k = k; /* the HUD pass after vf_34 (HUD_PASS_EYE; a full-screen video: HUD_PASS_COVER, no player) */
		g_hud_ret[k] = g_hud_none ? 0 : g_hud_video ? c_hudcov(g_im_obj) : c_hudeye(g_im_obj, pl);
	}
}
__attribute__((noipa)) uint64_t c_h4(void *gm, void *rt, const int *org, void *pl)
{
	return t_h4(gm, rt, org, pl, 2, 1) + 1;
}

/* ---- a stand-in runtime behind somvr_shared (what the layer publishes) ----------------------------------------- */
static const XrFovf EYE_FOV[2] = { { -0.9f, 0.7f, 0.8f, -0.95f }, { -0.7f, 0.9f, 0.8f, -0.95f } };
static int g_rt_calls, g_rt_bad;
static XrResult g_rt_result = XR_SUCCESS;
static XrTime g_rt_time;
static float g_rt_head[3];              /* the head (m), added to both eyes */
static sv_quat g_rt_q = { 0, 0, 0, 1 }; /* both eyes' orientation; the eyes sit -/+ 3.2 cm along its x axis */
static XrResult XRAPI_CALL st_locate(XrSession s, const XrViewLocateInfo *li, XrViewState *vs, uint32_t cap,
				     uint32_t *n, XrView *v)
{
	g_rt_calls++;
	if (s != (XrSession)(uintptr_t)0x2222 || li->space != (XrSpace)(uintptr_t)0x3333 || cap != 2)
		g_rt_bad++;
	g_rt_time = li->displayTime;
	*n = 2;
	vs->viewStateFlags = 0xf;
	for (int e = 0; e < 2; e++) {
		sv_vec3 o = sv_qrot(g_rt_q, (sv_vec3){ e ? 0.032f : -0.032f, 0, 0 });
		v[e].pose = (XrPosef){ { g_rt_q.x, g_rt_q.y, g_rt_q.z, g_rt_q.w },
				       { g_rt_head[0] + o.x, g_rt_head[1] + o.y, g_rt_head[2] + o.z } };
		v[e].fov = EYE_FOV[e];
	}
	return g_rt_result;
}
static void runtime_up(void)
{
	g_sh.xr.session = 0x2222;
	g_sh.xr.space = 0x3333;
	g_sh.xr.locate_views = (void *)st_locate;
	somvr_time_publish(&g_sh, 7000000000, 11111111);
	atomic_store(&g_sh.xr.state, SOMVR_XRS_RUNNING);
}

/* ---- one game frame: BeginFrame (+0x38), the player loop's call of the body, then Present ---------------------- */
static char g_gm[0x100];
static uint64_t g_frame_no = 5000;
static int g_pu_done; /* this frame's player update already ran (SOMVR_ARMS: before phase 2, as in the game) */
static int frame(void)
{
	*(volatile uint32_t *)(g_renderer_obj + RENDERER_FRAME_OFF) = (uint32_t)++g_frame_no;
	g_fol[0] = 0x11;
	g_gm[0x20] = 0x22;
	g_h4_calls = 0;
	g_h4_bad_state = 0;
	g_h4_frame = g_frame_no;
	g_nev = 0;
	if (g_pl && !g_pu_done)
		player_update(); /* the player update (vf_11, the collision) runs before the render's per-view body */
	g_pu_done = 0;
	EXPECT(c_h4(g_gm, &g_rt_ptr, NULL, g_pl) == 6, "H4 return value");
	EXPECT(c_comp(g_holder) == 4, "the present composite's return value"); /* FUN_00742ff0: the composite, then Present */
	EXPECT(((t_h9_fn)g_vt9[8])((void *)0x77, 1, 0) == 0, "H9 return value");
	return g_h4_calls;
}

static void set_trigger(const char *path, const char *word)
{
	if (!word) {
		unlink(path);
	} else {
		FILE *f = fopen(path, "w");
		fputs(word, f);
		fclose(f);
	}
	struct timespec ts = { 0, 600 * 1000 * 1000 };
	nanosleep(&ts, NULL);
}

static void check_mono(const char *what)
{
	EXPECT(g_h4_calls == 1 && g_h4_first[0] == 1, "%s: the body ran %d times", what, g_h4_calls);
	EXPECT(!memcmp(&g_h4_view[0], &GAME, sizeof GAME), "%s: the view changed", what);
	EXPECT(g_h4_rect[0][2] == 1 && g_h4_tan[0][0] == tanf(0.75f) && g_h4_co[0][0] == 0, "%s: rect/tangents changed",
	       what);
	somvr_tag t;
	EXPECT(somvr_tag_get(&g_sh, atomic_load(&g_sh.tag_seq), &t) && t.frame == 0, "%s: the present was tagged stereo",
	       what);
	printf("mono  %-22s one call, the game's view, present tag frame 0\n", what);
}

static void check_stereo(const char *what, int layout, int fake)
{
	EXPECT(g_h4_calls == 2 && g_h4_first[0] == 1 && g_h4_first[1] == 0, "%s: calls %d first %d,%d", what, g_h4_calls,
	       g_h4_first[0], g_h4_first[1]);
	EXPECT(g_h4_bad_state == 0, "%s: eye 1 did not start from eye 0's per-call state", what);
	somvr_rec rec;
	EXPECT(somvr_rec_read(&g_sh, g_frame_no, &rec), "%s: no complete record for frame %llu", what,
	       (unsigned long long)g_frame_no);
	EXPECT(rec.layout == (uint32_t)layout && rec.nslots == (layout == SOMVR_LAYOUT_B ? 2u : 1u) && rec.applied == 3,
	       "%s: record layout %u nslots %u applied %u", what, rec.layout, rec.nslots, rec.applied);
	EXPECT(!!(rec.flags & SOMVR_REC_FAKE) == fake, "%s: flags %#x", what, rec.flags);
	for (int e = 0; e < 2; e++) {
		const somvr_view *v = &g_h4_view[e];
		EXPECT(NEAR(v->pos[0], 100 + (e ? 3.2 : -3.2), 1e-4) && v->pos[1] == 200 && v->pos[2] == 300,
		       "%s: eye %d pos %g %g %g", what, e, v->pos[0], v->pos[1], v->pos[2]);
		somvr_fovf f = rec.eye[e].fov;
		if (!fake) { /* the runtime's fov, widened by SOMVR_OVERSCAN (bit for bit the runtime's at 0) */
			memcpy(&f, &EYE_FOV[e], sizeof f);
			f = sv_fov_overscan(&f, g_overscan);
		}
		sv_proj p = sv_proj_from_fov(&f);
		float fx, fy;
		sv_view_fov_cover(&f, &fx, &fy);
		EXPECT(NEAR(v->fov_x, fx, 1e-6) && NEAR(v->fov_y, fy, 1e-6) && v->near_z == 7.0f, "%s: eye %d SomView fov %g %g",
		       what, e, v->fov_x, v->fov_y);
		EXPECT(g_h4_tan[e][0] == p.tx && g_h4_tan[e][1] == p.ty && g_h4_co[e][0] == p.cx && g_h4_co[e][1] == p.cy,
		       "%s: eye %d H3 got %g %g %g %g, want %g %g %g %g", what, e, g_h4_tan[e][0], g_h4_tan[e][1], g_h4_co[e][0],
		       g_h4_co[e][1], p.tx, p.ty, p.cx, p.cy);
		EXPECT(g_h4_ov[e][0] == p.tx && g_h4_ov[e][1] == p.ty && g_h4_ov[e][2] == p.cx && g_h4_ov[e][3] == p.cy,
		       "%s: eye %d overlay with the eye camera got %g %g %g %g", what, e, g_h4_ov[e][0], g_h4_ov[e][1],
		       g_h4_ov[e][2], g_h4_ov[e][3]);
		EXPECT(NEAR(g_h4_corner[e][0], p.tx + fabsf(p.cx), 1e-6) && NEAR(g_h4_corner[e][1], p.ty + fabsf(p.cy), 1e-6),
		       "%s: eye %d corners from %g %g", what, e, g_h4_corner[e][0], g_h4_corner[e][1]);
		float want_rect[4];
		uint32_t slot;
		somvr_layout_rect(layout, e, want_rect, &slot);
		EXPECT(!memcmp(g_h4_rect[e], want_rect, sizeof want_rect) && !memcmp(rec.eye[e].rect, want_rect, sizeof want_rect) &&
			       rec.eye[e].slot == slot,
		       "%s: eye %d rect %g..%g slot %u", what, e, g_h4_rect[e][0], g_h4_rect[e][2], rec.eye[e].slot);
		EXPECT(!memcmp(&rec.eye[e].fov, &f, sizeof f) && rec.eye[e].hash == sv_xf_hash(v),
		       "%s: eye %d record fov/hash", what, e);
		EXPECT(NEAR(rec.eye[e].pose.p[0], e ? 0.032 : -0.032, 1e-7) && rec.eye[e].pose.q[3] == 1, "%s: eye %d xr pose",
		       what, e);
	}
	EXPECT(!memcmp(g_gm + GM_LAST_VIEW, &GAME, sizeof GAME), "%s: gm+0x70 is not the game's view", what);
	EXPECT(atomic_load(&g_sh.rec_pending) == 0, "%s: rec_pending not taken by H9", what);
	somvr_tag t;
	EXPECT(somvr_tag_get(&g_sh, atomic_load(&g_sh.tag_seq), &t) && t.frame == g_frame_no &&
		       t.slot == (layout == SOMVR_LAYOUT_B ? 1u : 0u) && t.tid == my_tid(),
	       "%s: tag frame %llu slot %u", what, (unsigned long long)t.frame, t.slot);
	EXPECT(g_h9_tid_inside == my_tid() && g_h9_tag_inside == atomic_load(&g_sh.tag_seq) &&
		       atomic_load(&g_sh.present_tid) == 0,
	       "%s: present bracket tid %d tag %llu", what, g_h9_tid_inside, (unsigned long long)g_h9_tag_inside);
	printf("stereo %-21s eyes x %.2f/%.2f fovX %.4f tan %.4f,%.4f co %.4f,%.4f rect R %g..%g tag %llu\n", what,
	       g_h4_view[0].pos[0], g_h4_view[1].pos[0], g_h4_view[0].fov_x, g_h4_tan[0][0], g_h4_tan[0][1], g_h4_co[0][0],
	       g_h4_co[0][1], g_h4_rect[1][0], g_h4_rect[1][2], (unsigned long long)g_frame_no);
}

static int log_has(const char *needle);
/* SOMVR_OVERSCAN (re-xr-pipeline.md 7.2): the default for views=xr; the eyes render the runtime's fov widened by s x the
 * half-extent per side (H3: (1+s) tx, (1+s) ty, the offset kept) and the record carries exactly the fov H3 rendered
 * (check_stereo: H3 = sv_proj_from_fov(record fov)), so the layer submits the rendered fov; 0 = the runtime's, bit for
 * bit */
static void overscan_tests(void)
{
	EXPECT(g_overscan == 0.1f && log_has("CONFIG overscan=0.100 (default)"), "views=xr: default overscan %g", g_overscan);
	static const float S[] = { 0.1f, 0.0f, 0.25f };
	for (size_t i = 0; i < sizeof S / sizeof S[0]; i++) {
		g_overscan = S[i];
		frame();
		check_stereo(S[i] > 0 ? "xr views, overscan" : "xr views, overscan 0", SOMVR_LAYOUT_A, 0);
		somvr_rec rec;
		EXPECT(somvr_rec_read(&g_sh, g_frame_no, &rec), "overscan %g: no record", S[i]);
		for (int e = 0; e < 2; e++) {
			somvr_fovf rt;
			memcpy(&rt, &EYE_FOV[e], sizeof rt);
			sv_proj p = sv_proj_from_fov(&rt);
			const somvr_fovf *f = &rec.eye[e].fov;
			float k = 1 + S[i];
			EXPECT(NEAR(g_h4_tan[e][0], k * p.tx, 2e-5) && NEAR(g_h4_tan[e][1], k * p.ty, 2e-5) &&
				       NEAR(g_h4_co[e][0], p.cx, 2e-5) && NEAR(g_h4_co[e][1], p.cy, 2e-5),
			       "overscan %g eye %d: H3 %g %g %g %g, want %g x (%g %g) and %g %g", S[i], e, g_h4_tan[e][0],
			       g_h4_tan[e][1], g_h4_co[e][0], g_h4_co[e][1], k, p.tx, p.ty, p.cx, p.cy);
			EXPECT(S[i] > 0 ? f->l < rt.l && f->r > rt.r && f->u > rt.u && f->d < rt.d : !memcmp(f, &rt, sizeof rt),
			       "overscan %g eye %d: record fov %g..%g %g..%g vs the runtime's %g..%g %g..%g", S[i], e, f->l, f->r,
			       f->d, f->u, rt.l, rt.r, rt.d, rt.u);
			EXPECT(rec.eye[e].fov_cover.r >= f->r && rec.eye[e].fov_cover.u >= f->u, "overscan %g eye %d: cover", S[i],
			       e);
		}
	}
	g_overscan = 0.1f;
	printf("overscan: default 0.1 (views=xr), H3 tangents x 1.1/1.25 with the offset kept, record fov = H3's; 0 = exact\n");
}

static char g_hex[80][256];
static int call_site(void *caller, void *target, struct site_spec *s, const char *name, int i)
{
	const unsigned char *p = caller;
	for (int k = 0; k < 96; k++) {
		int32_t rel;
		memcpy(&rel, p + k + 1, 4);
		if (p[k] == 0xe8 && (uintptr_t)(p + k + 5 + rel) == (uintptr_t)target) {
			hex_of(p + k, 5, g_hex[i], 128);
			hex_of(target, 4, g_hex[i] + 128, 128);
			*s = (struct site_spec){ name, K_CALL, (uintptr_t)(p + k), (uintptr_t)(p + k), g_hex[i],
						 (uintptr_t)target, g_hex[i] + 128 };
			return 0;
		}
	}
	return -1;
}
static int jmp_site(void *from, void *target, struct site_spec *s, const char *name, int i)
{
	const unsigned char *p = from;
	for (int k = 0; k < 32; k++) {
		int32_t rel;
		memcpy(&rel, p + k + 1, 4);
		if (p[k] == 0xe9 && (uintptr_t)(p + k + 5 + rel) == (uintptr_t)target) {
			hex_of(p + k, 5, g_hex[i], 128);
			hex_of(target, 4, g_hex[i] + 128, 128);
			*s = (struct site_spec){ name, K_JMP, (uintptr_t)(p + k), (uintptr_t)(p + k), g_hex[i],
						 (uintptr_t)target, g_hex[i] + 128 };
			return 0;
		}
	}
	return -1;
}
static void vslot_site(void *volatile *slot, void *target, struct site_spec *s, const char *name, int i)
{
	*slot = target;
	hex_of((const unsigned char *)slot, 8, g_hex[i], 128);
	hex_of(target, 4, g_hex[i] + 128, 128);
	*s = (struct site_spec){ name, K_VSLOT, (uintptr_t)slot, (uintptr_t)slot, g_hex[i], (uintptr_t)target,
				 g_hex[i] + 128 };
}

/* layout B after one frame: the tag's eye bits, the copies and their order, the eye images' pixels, the published info */
static void check_eyes(const char *what, uint32_t want, int want_copies)
{
	somvr_tag t;
	EXPECT(somvr_tag_get(&g_sh, atomic_load(&g_sh.tag_seq), &t) && t.frame == g_frame_no && t.eyes == want,
	       "%s: tag frame %llu eyes %u, want %u", what, (unsigned long long)t.frame, t.eyes, want);
	int ncopy = 0, nend = 0, end[2] = { -1, -1 }, copy[2] = { -1, -1 }, begin1 = -1;
	for (int i = 0; i < g_nev; i++) {
		if (g_ev[i].k == 'B' && g_ev[i].a == 1 && begin1 < 0)
			begin1 = i;
		if (g_ev[i].k == 'E' && nend < 2)
			end[nend++] = i;
		if (g_ev[i].k == 'C') {
			ncopy++;
			for (int e = 0; e < 2; e++)
				if (g_et.tex[e] == (void *)&g_tex_pool[g_ev[i].a])
					copy[e] = i;
		}
	}
	EXPECT(ncopy == want_copies, "%s: %d copies, want %d", what, ncopy, want_copies);
	if (want != 3)
		return;
	EXPECT(end[0] < copy[0] && copy[0] < begin1 && end[1] < copy[1], "%s: order end L %d copy L %d begin R %d end R %d "
	       "copy R %d", what, end[0], copy[0], begin1, end[1], copy[1]);
	somvr_eyes_info in = { 0 };
	EXPECT(somvr_eyes_read(&g_sh, &in) && in.gen == t.eye_gen && in.w == g_surf.desc.w && in.h == g_surf.desc.h,
	       "%s: eyes info gen %u (tag %u) %ux%u", what, in.gen, t.eye_gen, in.w, in.h);
	for (int e = 0; e < 2; e++) {
		st_tex_t *tx = g_et.tex[e];
		EXPECT(tx && tx->content == 0xA000u + (uint32_t)g_frame_no * 4u + (uint32_t)e && tx->f3d->image &&
			       in.image[e] == tx->f3d->image,
		       "%s: eye %d image holds %#x (want %#x), vkimage %#llx published %#llx", what, e, tx ? tx->content : 0,
		       0xA000u + (uint32_t)g_frame_no * 4u + (uint32_t)e, tx ? (unsigned long long)tx->f3d->image : 0ull,
		       (unsigned long long)in.image[e]);
	}
	EXPECT(atomic_load(&g_sh.eyes.expect_w) == g_surf.desc.w && atomic_load(&g_sh.eyes.expect_h) == g_surf.desc.h,
	       "%s: expect %ux%u", what, atomic_load(&g_sh.eyes.expect_w), atomic_load(&g_sh.eyes.expect_h));
	printf("eyes  %-22s L copied after its end and before R begins, images %#x/%#x, vkimage %#llx/%#llx gen %u\n",
	       what, ((st_tex_t *)g_et.tex[0])->content, ((st_tex_t *)g_et.tex[1])->content,
	       (unsigned long long)in.image[0], (unsigned long long)in.image[1], in.gen);
}

/* ---- SOMVR_AUDIO=head, SOMVR_BASE=head, SOMVR_FOLLOW=1 (switched on at start; flags toggled white box here) ------- */
static char g_logp[1024];
static int log_has(const char *needle)
{
	flush_log();
	FILE *f = fopen(g_logp, "r");
	char line[4096];
	int hit = 0;
	while (f && !hit && fgets(line, sizeof line, f))
		hit = strstr(line, needle) != NULL;
	if (f)
		fclose(f);
	return hit;
}
static sv_vec3 eye_mid(void)
{
	return (sv_vec3){ 0.5f * (g_h4_view[0].pos[0] + g_h4_view[1].pos[0]), 0.5f * (g_h4_view[0].pos[1] + g_h4_view[1].pos[1]),
			  0.5f * (g_h4_view[0].pos[2] + g_h4_view[1].pos[2]) };
}
#define NEAR3(v, X, Y, Z, e) (NEAR((v).x, X, e) && NEAR((v).y, Y, e) && NEAR((v).z, Z, e))
static sv_vec3 eye0(void) { return (sv_vec3){ g_h4_view[0].pos[0], g_h4_view[0].pos[1], g_h4_view[0].pos[2] }; }
static sv_vec3 root(void) { return (sv_vec3){ g_pobj.pos[0], g_pobj.pos[1], g_pobj.pos[2] }; }
static sv_vec3 vsub(sv_vec3 a, sv_vec3 b) { return (sv_vec3){ a.x - b.x, a.y - b.y, a.z - b.z }; }

static float g_gp[3] = { 11, 22, 33 }, g_gf[3] = { 0, 0, 1 }, g_gt[3] = { 0, 1, 0 }; /* the game's listener arguments */
static void call_aud(uint32_t idx)
{
	memset(&g_aud, 0, sizeof g_aud);
	EXPECT(((t_aud_fn)g_vta[23])((void *)0x467b400, g_gp, g_gf, g_gt, idx) == 1, "vf_23 return value");
}
static int aud_passed(void) { return g_aud.n == 1 && g_aud.pos == g_gp && g_aud.front == g_gf && g_aud.top == g_gt; }
static void *aud_thread(void *a)
{
	(void)a;
	call_aud(0);
	return NULL;
}

static void audio_tests(void)
{
	g_views_fake = 0;
	runtime_up();
	g_audio = 0;
	frame();
	call_aud(0);
	EXPECT(aud_passed(), "audio off: the listener changed");
	g_audio = 1;
	atomic_fetch_add(&g_req_recenter, 1); /* the reference: the head at the tracking origin, level */
	frame();
	call_aud(0);
	EXPECT(!aud_passed() && NEAR(g_aud.p[0], 100, 1e-4) && NEAR(g_aud.p[1], 200, 1e-4) && NEAR(g_aud.p[2], 300, 1e-4),
	       "audio: the recentred head is not on the camera base (%g %g %g)", g_aud.p[0], g_aud.p[1], g_aud.p[2]);
	/* 10 cm right, 5 cm up, 20 cm ahead, turned 90 deg left in OpenXR: game (+10, +5, +20), looking along -X */
	g_rt_head[0] = 0.1f, g_rt_head[1] = 0.05f, g_rt_head[2] = -0.2f;
	g_rt_q = (sv_quat){ 0, sinf(0.7853982f), 0, cosf(0.7853982f) };
	frame();
	call_aud(0);
	sv_vec3 m = eye_mid();
	EXPECT(!aud_passed() && NEAR(g_aud.p[0], 110, 1e-3) && NEAR(g_aud.p[1], 205, 1e-3) && NEAR(g_aud.p[2], 320, 1e-3) &&
		       NEAR3(m, g_aud.p[0], g_aud.p[1], g_aud.p[2], 1e-3),
	       "audio: head pos %g %g %g, want 110 205 320 = the eyes' centre %g %g %g", g_aud.p[0], g_aud.p[1], g_aud.p[2],
	       m.x, m.y, m.z);
	EXPECT(NEAR(g_aud.f[0], -1, 1e-5) && NEAR(g_aud.f[1], 0, 1e-5) && NEAR(g_aud.f[2], 0, 1e-5) && NEAR(g_aud.t[0], 0, 1e-5) &&
		       NEAR(g_aud.t[1], 1, 1e-5) && NEAR(g_aud.t[2], 0, 1e-5),
	       "audio: front %g %g %g top %g %g %g", g_aud.f[0], g_aud.f[1], g_aud.f[2], g_aud.t[0], g_aud.t[1], g_aud.t[2]);
	sv_vec3 f = sv_qrot(sv_quat_of(g_h4_view[0].quat), (sv_vec3){ 0, 0, 1 });
	EXPECT(NEAR3(f, g_aud.f[0], g_aud.f[1], g_aud.f[2], 1e-5), "audio: front is not the eyes' forward");
	EXPECT(g_gp[0] == 11 && g_gp[1] == 22 && g_gp[2] == 33 && g_gf[2] == 1 && g_gt[1] == 1, "audio: the caller's memory changed");
	call_aud(1);
	EXPECT(aud_passed() && g_aud.idx == 1, "audio: listener 1 changed");
	pthread_t th;
	pthread_create(&th, NULL, aud_thread, NULL);
	pthread_join(th, NULL);
	EXPECT(aud_passed(), "audio: a call on another thread got the head pose");
	g_frame_no += 3; /* three frames without H4 (a menu): the pose is stale */
	*(volatile uint32_t *)(g_renderer_obj + RENDERER_FRAME_OFF) = (uint32_t)g_frame_no;
	call_aud(0);
	EXPECT(aud_passed(), "audio: a stale head pose was used");
	atomic_store(&g_sh.xr.state, SOMVR_XRS_DEAD); /* a mono frame: the game's listener in that very frame */
	frame();
	call_aud(0);
	EXPECT(aud_passed(), "audio: a frame without views kept the head pose");
	atomic_store(&g_sh.xr.state, SOMVR_XRS_RUNNING);
	frame();
	call_aud(0);
	EXPECT(!aud_passed() && NEAR(g_aud.p[0], 110, 1e-3), "audio: no head pose after the views came back");
	EXPECT(log_has("-> head pos=") && log_has("-> game's ("), "audio: no AUDIO lines in the log");
	g_rt_head[0] = g_rt_head[1] = g_rt_head[2] = 0, g_rt_q = (sv_quat){ 0, 0, 0, 1 };
	g_audio = 0;
	printf("audio  head pose on listener 0 (110 205 320, front -X); listener 1, other thread, stale, mono: the game's\n");
}

static void head_tests(void)
{
	g_rigcfg.base = SOMVR_BASE_HEAD;
	g_pl = &g_player;
	atomic_fetch_add(&g_req_recenter, 1);
	frame();
	sv_vec3 m = eye_mid();
	EXPECT(NEAR3(m, 500, 172.5, -50, 1e-3) && NEAR(g_h4_view[0].pos[0], 496.8, 1e-3), "head base: eyes' centre %g %g %g",
	       m.x, m.y, m.z);
	EXPECT(g_hn.ok >= 1 && log_has("HEAD frame=") && log_has("first head node 0x72598328"), "head base: no HEAD line");
	/* each failure: that frame on the camera base, the next frame on the head again */
	struct { const char *what; int *flag; int on, off; } fl[] = {
		{ "NaN", &g_nodexf_nan, 1, 0 }, { "GetNodeTransform fails", &g_nodexf_fail, 1, 0 },
		{ "site off", &g_node_ok, 0, 1 },
	};
	uint64_t f0 = g_hn.falls;
	for (size_t i = 0; i < sizeof fl / sizeof fl[0]; i++) {
		*fl[i].flag = fl[i].on;
		frame();
		m = eye_mid();
		EXPECT(NEAR3(m, 100, 200, 300, 1e-3), "head base, %s: not the camera base (%g %g %g)", fl[i].what, m.x, m.y, m.z);
		*fl[i].flag = fl[i].off;
		frame();
		m = eye_mid();
		EXPECT(NEAR3(m, 500, 172.5, -50, 1e-3), "head base after %s: %g %g %g", fl[i].what, m.x, m.y, m.z);
	}
	g_head_up = 400; /* more than 3 m from the root */
	frame();
	EXPECT(NEAR3(eye_mid(), 100, 200, 300, 1e-3), "head base: a node 4 m above the root was used");
	g_head_up = 72.5f;
	g_ppm.vt = 0x1234; /* not a PlayerMovement: not the Player */
	frame();
	EXPECT(NEAR3(eye_mid(), 100, 200, 300, 1e-3), "head base: a player without a PlayerMovement was used");
	g_ppm.vt = (uintptr_t)g_vtpm;
	g_ppm.owner = NULL;
	frame();
	EXPECT(NEAR3(eye_mid(), 100, 200, 300, 1e-3), "head base: a PlayerMovement of another owner was used");
	g_ppm.owner = &g_player;
	g_vti[4] = (void *)g_holder; /* FUN_008f3c20's call would land in data (checked once per object) */
	g_hn.obj_ok = 0;
	frame();
	EXPECT(NEAR3(eye_mid(), 100, 200, 300, 1e-3), "head base: a model interface without code was called");
	g_vti[4] = (void *)st_getmodel;
	g_pmgr.hchar = NULL; /* hChar unset: bodyObj */
	frame();
	EXPECT(NEAR3(eye_mid(), 500, 172.5, -50, 1e-3), "head base: bodyObj not used");
	g_pbody.obj = NULL;
	frame();
	EXPECT(NEAR3(eye_mid(), 100, 200, 300, 1e-3), "head base: no object at all was used");
	g_pmgr.hchar = &g_pobj, g_pbody.obj = &g_pobj;
	frame();
	EXPECT(NEAR3(eye_mid(), 500, 172.5, -50, 1e-3), "head base: not back on the head");
	g_pobj.pos[0] += 400; /* a teleport: one frame on the camera, then the new place */
	frame();
	EXPECT(NEAR3(eye_mid(), 100, 200, 300, 1e-3), "head base: a 4 m jump was used at once");
	frame();
	EXPECT(NEAR3(eye_mid(), 900, 172.5, -50, 1e-3), "head base: the new place not taken (%g)", eye_mid().x);
	g_pobj.pos[0] -= 400;
	frame();
	frame();
	EXPECT(NEAR3(eye_mid(), 500, 172.5, -50, 1e-3), "head base: not back at 500");
	EXPECT(g_hn.falls == f0 + 10, "head base: %llu fallbacks, want 10", (unsigned long long)(g_hn.falls - f0));
	EXPECT(log_has("fallback to the camera base: a jump of more than 1 m") && log_has("fallback to the camera base: NaN"),
	       "head base: no fallback lines");
	printf("head   base on the Head node (500 172.5 -50); NaN, failed call, no site/player/object/code, 4 m, jump: camera\n");
}

/* ---- SOMVR_EYE_FWD_CM and SOMVR_TUNE (off at start: SOMVR_EYE_FWD_CM=0 SOMVR_EYE_MIN_CM=0; white box here): the head
 * base's eye point 16 cm along Talion's root yaw, the head near plane 6 cm, the tune file (missing, read, the 4 Hz
 * gate, a change, a bad line) and its TUNE lines; the clamp and the eases: tests/test_headview.c */
static void hv_write(const char *p, const char *text)
{
	FILE *f = fopen(p, "w");
	if (f)
		fputs(text, f), fclose(f);
}

static void hv_tests(void)
{
	int base0 = g_rigcfg.base, near0 = g_hb.near;
	g_rigcfg.base = SOMVR_BASE_HEAD;
	g_pl = &g_player;
	g_hb.near = 1;
	g_pobj.rot[1] = sinf(0.2617994f), g_pobj.rot[3] = cosf(0.2617994f); /* Talion faces 30 deg right */
	atomic_fetch_add(&g_req_recenter, 1);
	frame();
	EXPECT(!g_hv.on && NEAR3(eye_mid(), 500, 172.5, -50, 1e-3) && g_h4_view[0].near_z == HB_NEAR_CM,
	       "eye offset off: %g %g %g near %g", eye_mid().x, eye_mid().y, eye_mid().z, g_h4_view[0].near_z);
	somvr_hv_cfg_default(&g_hv.cfg);
	somvr_hv_init(&g_hv.s);
	g_hv.on = 1, g_hv.near_cm = HV_NEAR_CM;
	frame();
	sv_vec3 m = eye_mid();
	EXPECT(NEAR3(m, 508, 172.5, -50 + 16 * 0.8660254, 1e-3) && g_h4_view[0].near_z == 6.0f && g_h4_view[1].near_z == 6.0f,
	       "eye offset: %g %g %g near %g, want 508 172.5 %g near 6", m.x, m.y, m.z, g_h4_view[0].near_z, -50 + 16 * 0.8660254);
	EXPECT(log_has("EYEOFF frame=") && g_hv.frames >= 1, "eye offset: no EYEOFF line");
	/* a failed root read: the Head node as it was this frame */
	g_pl = NULL;
	frame();
	g_pl = &g_player;
	EXPECT(log_has("Talion's yaw is not readable") || g_hv.fails > 0 || !g_fr.head_ok, "eye offset: no failure path");
	/* the tune file */
	char tp[480];
	snprintf(tp, sizeof tp, "%.400s.tune", g_logp);
	unlink(tp);
	snprintf(g_hv.tune, sizeof g_hv.tune, "%s", tp);
	g_hv.tune_state = 0, g_hv.tune_next = 0;
	double t = mono();
	hv_tune_poll(t);
	EXPECT(g_hv.tune_state == 2 && log_has(".tune: no file") && g_hv.cfg.fwd_cm == 16, "tune: missing file %d",
	       g_hv.tune_state);
	hv_write(tp, "eye_fwd_cm=10\n# the coordinator's\nnear_cm=8\nbogus=1\n");
	hv_tune_poll(t + 0.1); /* inside the 0.25 s: not looked at */
	EXPECT(g_hv.tune_reads == 0 && g_hv.cfg.fwd_cm == 16, "tune: read inside the 4 Hz gate");
	hv_tune_poll(t + 0.3);
	EXPECT(g_hv.tune_reads == 1 && g_hv.cfg.fwd_cm == 10 && g_hv.near_cm == 8 && log_has("eye_fwd_cm 16 -> 10") &&
		       log_has("near_cm 6 -> 8") && log_has("ignored: bogus=1"),
	       "tune: read %llu fwd %g near %g", (unsigned long long)g_hv.tune_reads, g_hv.cfg.fwd_cm, g_hv.near_cm);
	hv_tune_poll(t + 0.6); /* unchanged: not read again */
	EXPECT(g_hv.tune_reads == 1, "tune: an unchanged file read again");
	g_hv.tune_next = 0;
	frame();
	m = eye_mid();
	EXPECT(NEAR3(m, 505, 172.5, -50 + 10 * 0.8660254, 1e-3) && g_h4_view[0].near_z == 8.0f,
	       "tune applied: %g %g %g near %g", m.x, m.y, m.z, g_h4_view[0].near_z);
	hv_write(tp, "eye_fwd_cm=12.5\neye_up_cm=-2\nnear_cm=99\n");
	g_hv.tune_next = 0;
	hv_tune_poll(t + 0.9);
	EXPECT(g_hv.tune_reads == 2 && g_hv.cfg.fwd_cm == 12.5f && g_hv.cfg.up_cm == -2 && g_hv.near_cm == 8 &&
		       log_has("eye_fwd_cm 10 -> 12.5") && log_has("eye_up_cm 0 -> -2") && log_has("ignored: near_cm=99"),
	       "tune: changed %llu fwd %g up %g near %g", (unsigned long long)g_hv.tune_reads, g_hv.cfg.fwd_cm, g_hv.cfg.up_cm,
	       g_hv.near_cm);
	unlink(tp);
	g_hv.tune_next = 0;
	hv_tune_poll(t + 1.2);
	EXPECT(g_hv.tune_state == 2 && g_hv.cfg.fwd_cm == 12.5f, "tune: the file gone, values kept");
	/* back off: the base as it was */
	g_hv.on = 0, g_hv.tune[0] = 0, g_hv.near_cm = HB_NEAR_CM;
	g_pobj.rot[1] = 0, g_pobj.rot[3] = 1;
	frame();
	frame();
	EXPECT(NEAR3(eye_mid(), 500, 172.5, -50, 1e-3), "eye offset off again: %g %g %g", eye_mid().x, eye_mid().y,
	       eye_mid().z);
	g_rigcfg.base = base0, g_hb.near = near0;
	printf("eyeoff 16 cm along Talion's yaw on the head base, near 6; SOMVR_TUNE missing/read/4 Hz/changed/bad/gone\n");
}

static void follow_tests(void)
{
	g_rigcfg.base = SOMVR_BASE_HEAD;
	g_pl = &g_player;
	g_follow.on = 1;
	g_pobj.rot[1] = sinf(0.2617994f), g_pobj.rot[3] = cosf(0.2617994f); /* Talion faces 30 deg right */
	atomic_fetch_add(&g_req_recenter, 1);
	frame();
	EXPECT(!g_follow.req, "follow: a request with the head on the base");
	sv_vec3 r0 = root(), v;
	int c0 = g_pm_consumed;
	/* the head steps 20 cm forward: the next vf_11 moves Talion 20 cm, in his frame R(30)^-1 (0 0 20); the view stays */
	g_rt_head[2] = -0.2f;
	frame();
	v = eye0();
	EXPECT(g_follow.req && NEAR3(g_follow.req_w, 0, 0, 20, 1e-3) && NEAR3(root(), r0.x, r0.y, r0.z, 1e-4),
	       "follow: request %d %g %g %g", g_follow.req, g_follow.req_w.x, g_follow.req_w.y, g_follow.req_w.z);
	frame();
	EXPECT(g_pm_consumed == c0 + 1 && NEAR(g_seen_extra[0], -10, 1e-3) && NEAR(g_seen_extra[1], 0, 1e-4) &&
		       NEAR(g_seen_extra[2], 17.320508, 1e-3),
	       "follow: extra %g %g %g in Talion's frame, want -10 0 17.32", g_seen_extra[0], g_seen_extra[1], g_seen_extra[2]);
	EXPECT(NEAR3(root(), r0.x, r0.y, r0.z + 20, 1e-3), "follow: root moved %g %g %g", root().x - r0.x, root().y - r0.y,
	       root().z - r0.z);
	EXPECT(NEAR3(eye0(), v.x, v.y, v.z, 1e-3), "follow: the view moved with the body (%g %g %g)", eye0().x - v.x,
	       eye0().y - v.y, eye0().z - v.z);
	EXPECT(!g_follow.req && !g_ppm.pending && NEAR(g_follow.last_app.z, 20, 1e-3), "follow: req %d pending %d applied %g",
	       g_follow.req, g_ppm.pending, g_follow.last_app.z);
	frame();
	EXPECT(NEAR3(root(), r0.x, r0.y, r0.z + 20, 1e-3) && NEAR3(eye0(), v.x, v.y, v.z, 1e-3), "follow: moved again");
	/* 50 cm at once: 30 (the step) then 20 */
	g_rt_head[2] = -0.7f;
	frame();
	v = eye0();
	frame();
	EXPECT(NEAR(root().z, r0.z + 50, 1e-3) && NEAR3(eye0(), v.x, v.y, v.z, 1e-3), "follow: step 1 root +%g", root().z - r0.z);
	frame();
	EXPECT(NEAR(root().z, r0.z + 70, 1e-3) && NEAR3(eye0(), v.x, v.y, v.z, 1e-3), "follow: step 2 root +%g", root().z - r0.z);
	frame();
	EXPECT(NEAR(root().z, r0.z + 70, 1e-3) && NEAR3(eye0(), v.x, v.y, v.z, 1e-3), "follow: step 3 root +%g", root().z - r0.z);
	/* 5 cm: inside the 8 cm deadzone */
	g_rt_head[2] = -0.75f;
	frame();
	frame();
	EXPECT(NEAR(root().z, r0.z + 70, 1e-3) && !g_follow.req, "follow: moved inside the deadzone");
	/* a wall 10 cm ahead: the collision keeps 10 of the 30, the view stays, the rest waits at the wall */
	g_wall = 1, g_wall_z = root().z + 10;
	g_rt_head[2] = -1.05f; /* the offset is 35 cm now */
	frame();
	v = eye0();
	double a0 = g_follow.app_cm, q0 = g_follow.req_cm;
	frame();
	EXPECT(NEAR(root().z, g_wall_z, 1e-3) && NEAR3(eye0(), v.x, v.y, v.z, 1e-3) && NEAR(g_follow.last_app.z, 10, 1e-3),
	       "follow: wall: root %g (wall %g) applied %g", root().z, g_wall_z, g_follow.last_app.z);
	frame();
	EXPECT(NEAR(root().z, g_wall_z, 1e-3) && NEAR3(eye0(), v.x, v.y, v.z, 1e-3) && NEAR(g_follow.last_app.z, 0, 1e-3) &&
		       NEAR(g_follow.app_cm - a0, 10, 1e-2) && NEAR(g_follow.req_cm - q0, 55, 1e-2),
	       "follow: at the wall: applied %g of %g", g_follow.app_cm - a0, g_follow.req_cm - q0);
	g_wall = 0;
	frame();
	frame();
	EXPECT(NEAR(root().z, g_wall_z + 25, 1e-3) && NEAR3(eye0(), v.x, v.y, v.z, 1e-3), "follow: after the wall root %g",
	       root().z - g_wall_z);
	/* walking with the stick (5 cm per tick) while following: the view moves with the walk only */
	g_rm[2] = 5;
	g_rt_head[2] -= 0.2f;
	frame();
	sv_vec3 va = eye0();
	frame();
	sv_vec3 vb = eye0();
	frame();
	sv_vec3 vc = eye0();
	EXPECT(NEAR3(vsub(vb, va), 2.5, 0, 4.330127, 1e-3) && NEAR3(vsub(vc, vb), 2.5, 0, 4.330127, 1e-3),
	       "follow + walk: view steps %g %g %g / %g %g %g", vb.x - va.x, vb.y - va.y, vb.z - va.z, vc.x - vb.x,
	       vc.y - vb.y, vc.z - vb.z);
	g_rm[2] = 0;
	/* a move already pending (not ours): ours is not stacked on it, it goes the frame after */
	g_rt_head[2] -= 0.2f;
	frame();
	sv_vec3 rb = root();
	uint64_t b0 = g_follow.busy;
	g_ppm.pending = 1;
	frame();
	EXPECT(g_follow.busy == b0 + 1 && NEAR3(root(), rb.x, rb.y, rb.z, 1e-3) && !g_ppm.pending,
	       "follow: stacked on a pending move");
	frame();
	EXPECT(NEAR(root().z, rb.z + 20, 1e-3), "follow: the move after the busy frame %g", root().z - rb.z);
	/* another PlayerMovement's vf_11 while a request waits: left alone, the player's own call takes it */
	g_rt_head[2] -= 0.2f;
	frame();
	float x2[7] = { 1, 2, 3, 0, 0, 0, 1 };
	((t_pm_fn)g_vtpm[11])(&g_ppm2, x2);
	EXPECT(g_follow.req && !g_ppm2.pending && g_ppm2.xpos[2] == 0 && x2[2] == 3, "follow: another PlayerMovement was moved");
	rb = root();
	frame();
	EXPECT(NEAR(root().z, rb.z + 20, 1e-3), "follow: the player's move after the other call %g", root().z - rb.z);
	/* off: nothing moves */
	g_follow.on = 0;
	rb = root();
	g_rt_head[2] -= 0.3f;
	frame();
	frame();
	EXPECT(NEAR3(root(), rb.x, rb.y, rb.z, 1e-4), "follow off: Talion moved");
	EXPECT(log_has("FOLLOW t=") && log_has(" -> moved req=") && log_has(" -> busy "), "follow: no FOLLOW/PM lines in the log");
	printf("follow 20 cm -> extra -10,0,17.32 in Talion's frame, view still; 50 = 30+20; deadzone; wall keeps 10 of 30;"
	       " walk; busy; other pm; %llu moves %.1f of %.1f cm\n", (unsigned long long)g_follow.moves, g_follow.app_cm,
	       g_follow.req_cm);
	g_rt_head[0] = g_rt_head[1] = g_rt_head[2] = 0;
	g_pobj.rot[1] = 0, g_pobj.rot[3] = 1;
	g_rigcfg.base = SOMVR_BASE_CAMERA;
	g_pl = NULL;
}

/* ---- SOMVR_HEAD_TETHER through the hooks (build 19): build 18 measured in H4 before the eyes, where g_fr (cleared at
 * H4's start) had no game view yet: "no head" every frame. Measured after the eyes now; a back re-latches before the
 * next frame's eyes. The follow's stand-ins: the head walks off the body into a wall (g_wall), the body held there. */
static int tt_rec_fade(void)
{
	somvr_rec rr;
	return somvr_rec_read(&g_sh, g_frame_no, &rr) ? (int)somvr_rec_fade(rr.flags) : -1;
}

static void tt_frames(int n)
{
	for (int i = 0; i < n; i++) {
		g_cb_test_t += 1.0 / 90;
		frame();
	}
}

static void tether_hook_tests(void)
{
	g_rigcfg.base = SOMVR_BASE_HEAD;
	g_pl = &g_player;
	g_follow.on = 1, g_follow.measure_old = 0;
	g_cb_test_t = 9500;
	int inp0 = g_inp.on, ctx0 = g_inp.ctx;
	g_inp.ctx = SC_CTX_WORLD; /* (the input's tests come later: its context is still 0, a menu) */
	setenv("SOMVR_HEAD_TETHER", "on", 1);
	tt_config();
	unsetenv("SOMVR_HEAD_TETHER");
	EXPECT(g_tt.on && g_tt.cfg.start_cm == 50 && g_tt.cfg.black_cm == 90, "tether: config");
	atomic_fetch_add(&g_req_recenter, 1);
	tt_frames(80); /* past the 0.6 s grace */
	EXPECT(g_tt.frames == 80 && g_tt.skipped == 0 && g_tt.why_n[0] == 0 && g_tt.off_cm < 1,
	       "tether: not measured in the hooks (frames %llu, not measured %llu, no head %llu; off %.1f)",
	       (unsigned long long)g_tt.frames, (unsigned long long)g_tt.skipped, (unsigned long long)g_tt.why_n[0],
	       g_tt.off_cm);
	/* a step on open ground: the follow takes the body, no fade */
	sv_vec3 r0 = root();
	for (int i = 0; i < 6; i++) { /* 10 cm a frame (9 m/s; the stand-in follow takes 30) */
		g_rt_head[2] -= 0.1f;
		tt_frames(1);
	}
	tt_frames(4);
	EXPECT(NEAR(root().z, r0.z + 60, 1e-2) && g_tt.s.episodes == 0 && tt_rec_fade() == 0 && g_tt.off_max < 50,
	       "tether: open ground: root +%g, episodes %llu, fade %d, the most %.1f cm", root().z - r0.z,
	       (unsigned long long)g_tt.s.episodes, tt_rec_fade(), g_tt.off_max);
	/* the head quicker than the follow (4 cm a frame here, 10 cm a frame the head: 40 cm/s at 4 Hz...): past 50 cm while
	 * the follow takes each step whole = catching up, no fade (when=held); build 18's when=always fades */
	float step0 = g_follow.step_cm;
	g_follow.step_cm = 4;
	for (int pass = 0; pass < 2; pass++) {
		g_tt.always = pass;
		uint64_t e1 = g_tt.s.episodes, c1 = g_tt.catchup;
		for (int i = 0; i < 12; i++) {
			g_rt_head[2] -= 0.1f;
			tt_frames(1);
		}
		float most = g_tt.off_cm;
		tt_frames(40); /* caught up */
		EXPECT(most > 55 && g_tt.off_cm < 25 && (pass ? g_tt.s.episodes == e1 + 1 && g_tt.catchup == c1
							  : g_tt.s.episodes == e1 && g_tt.catchup > c1 + 2),
		       "tether: catching up (when=%s): the most %.1f cm, now %.1f, episodes +%llu, catch-up frames +%llu",
		       pass ? "always" : "held", most, g_tt.off_cm, (unsigned long long)(g_tt.s.episodes - e1),
		       (unsigned long long)(g_tt.catchup - c1));
	}
	g_tt.always = 0;
	g_follow.step_cm = step0;
	atomic_fetch_add(&g_req_recenter, 1); /* the follow's dead zone left a few cm: the origin back on the head */
	tt_frames(2);
	uint64_t ep0 = g_tt.s.episodes;
	/* the body at a wall, the head 70 cm into it: half faded; the head back in 0.4 s: clear, no back */
	g_wall = 1, g_wall_z = root().z;
	g_rt_head[2] -= 0.7f;
	tt_frames(30);
	EXPECT(g_tt.s.episodes == ep0 + 1 && NEAR(g_tt.off_cm, 70, 0.5) && tt_rec_fade() >= 120 && tt_rec_fade() <= 135 &&
		       g_tt.relatches == 0,
	       "tether: 70 cm into the wall: off %.1f, fade %d, episodes %llu", g_tt.off_cm, tt_rec_fade(),
	       (unsigned long long)g_tt.s.episodes);
	g_rt_head[2] += 0.7f;
	tt_frames(36);
	EXPECT(tt_rec_fade() == 0 && !g_tt.s.in && g_tt.relatches == 0 && log_has("TETHER t=") &&
		       log_has("clear again: the view faded in (the farthest 70 cm") && log_has("frames ago): the view fades"),
	       "tether: the head back from the wall: fade %d, in %d, back %llu", tt_rec_fade(), g_tt.s.in,
	       (unsigned long long)g_tt.relatches);
	/* 100 cm into it: black in 0.15 s, 0.3 s of black = back at Talion's eyes (the next frame's eyes), then it fades in */
	g_rt_head[2] -= 1.0f;
	float p0x = g_rig.p0.x, p0z = g_rig.p0.z;
	int black_at = -1, back_at = -1, clear_at = -1;
	for (int i = 0; i < 90 && clear_at < 0; i++) {
		tt_frames(1);
		int fd = tt_rec_fade();
		if (fd == 255 && black_at < 0)
			black_at = i;
		if (g_tt.relatches && back_at < 0)
			back_at = i;
		if (back_at >= 0 && fd == 0)
			clear_at = i;
	}
	EXPECT(black_at >= 13 && black_at <= 16 && back_at >= black_at + 27 && back_at <= black_at + 29 &&
		       clear_at >= back_at + 26 && clear_at <= back_at + 29 && g_tt.relatches == 1 && g_tt.off_cm < 1 &&
		       NEAR(root().z, g_wall_z, 1e-3),
	       "tether: 100 cm into the wall: black at frame %d, back %d, clear %d, back %llu, off %.1f", black_at, back_at,
	       clear_at, (unsigned long long)g_tt.relatches, g_tt.off_cm);
	EXPECT(log_has("cm off: the view back to Talion's eyes (the rig's origin re-latched by ") &&
		       NEAR(hypotf(g_rig.p0.x - p0x, g_rig.p0.z - p0z), 100, 0.5),
	       "tether: the back: the origin moved %g,%g", g_rig.p0.x - p0x, g_rig.p0.z - p0z);
	/* the head back where it was: the follow takes the body back from the wall (no fade) */
	g_wall = 0;
	g_rt_head[2] += 1.0f;
	uint64_t e0 = g_tt.s.episodes;
	tt_frames(10);
	EXPECT(g_tt.s.episodes == e0 && tt_rec_fade() == 0 && NEAR(root().z, g_wall_z - 100, 1e-2),
	       "tether: back from the wall: episodes +%llu, fade %d, root %g", (unsigned long long)(g_tt.s.episodes - e0),
	       tt_rec_fade(), root().z - g_wall_z);
	/* a menu: not measured, logged once (the reason), counted */
	g_inp.on = 1, g_inp.ctx = SC_CTX_MENU;
	uint64_t m0 = g_tt.why_n[3];
	tt_frames(200);
	EXPECT(g_tt.why_n[3] - m0 == 200 && log_has("not measured: a menu (frames so far"), "tether: the menu %llu",
	       (unsigned long long)(g_tt.why_n[3] - m0));
	g_inp.ctx = SC_CTX_WORLD;
	tt_end_log();
	EXPECT(log_has("not measured: a menu 200; the grace after ") && log_has(" frames (when=held; the most "),
	       "tether: END's reasons");
	printf("tether in the hooks: measured %llu of %llu frames; the follow catching up past 50 cm: no fade (when=held), a "
	       "fade (always); 70 cm in the wall = half, back out clear; 100 cm = black "
	       "in %d frames, back after %d, clear %d later; a menu not measured\n", (unsigned long long)(g_tt.frames -
	       g_tt.skipped), (unsigned long long)g_tt.frames, black_at + 1, back_at - black_at, clear_at - back_at);
	memset(&g_tt, 0, sizeof g_tt);
	g_inp.on = inp0, g_inp.ctx = ctx0;
	g_rt_head[0] = g_rt_head[1] = g_rt_head[2] = 0;
	g_follow.on = 0;
	g_cb_test_t = -1;
	g_rigcfg.base = SOMVR_BASE_CAMERA;
	g_pl = NULL;
	frame();
}

/* ---- SOMVR_COMBAT: stand-ins for PlayerCombat and its targeting bands, the player's server Combat, the orcs'
 * Combat/CCharacter/HOBJECT, the Combat/Attack category, Combat_DealHit, the record lookup, AttackData, the AI stimulus
 * and its manager, the IEngineTimer and its CEngineTimers (references counted like the engine's, re-combat-vr.md 2.2) */
typedef struct {
	uintptr_t vt;
	uint32_t ref, frame_ms;
	float frame_s;
	uint32_t total_ms;
	double total_s;
	uint32_t num, den;
	float scale;
	char pad[TM_CARRY - TM_SCALE - 4];
	double carry;
	int deleted;
} st_timer_t;
typedef struct { uintptr_t vt; float pos[3], rot[4]; char pad[OBJ_TIMER - OBJ_ROT - 16]; st_timer_t *timer; } st_hobj_t;
typedef struct { uintptr_t vt; char pad[CH_FLAGS - 8]; uint8_t flags; } st_char_t;
typedef struct {
	uintptr_t vt;
	char p0[C_OWNER - 8];
	st_char_t *owner;
	char p1[C_AI - C_OWNER - 8];
	void *ai, *player;
	char p2[C_HITDIR - C_PLAYER - 8];
	float hitdir[3];
	int32_t stim;
	char p3[C_SYNC - C_STIM - 4];
	void *sync[2];
	char p4[C_HITONCE - C_SYNC - 16];
	uint8_t hitonce;
	char p5[C_DEAD - C_HITONCE - 1];
	uint8_t dead;
	char p6[C_HOBJ - C_DEAD - 1];
	st_hobj_t *hobj;
	char p7[RX_C_ATTACK - C_HOBJ - 8];
	uintptr_t attack; /* the current attack record (SOMVR_COMBAT_REACT's own-attack gate, RX_ATK_GATE) */
	char p8[0x600 - RX_C_ATTACK - 8]; /* to +0x600: SOMVR_DEFENCE's type +0x55b and window byte +0x562 */
} st_combat_t;
typedef struct { char p[PCB_COMBAT]; st_combat_t *c; } st_band_t;
typedef struct { char p0[PC_BANDS]; struct { st_band_t *b, *e, *cap; } band[3]; char p1[PC_SERVER - PC_BANDS - 72]; st_combat_t *server; } st_pcomb_t;
typedef struct { char p0[CAT_COUNT]; uint32_t count; char p1[CAT_RECS - CAT_COUNT - 4]; char *recs; } st_cat_t;
_Static_assert(offsetof(st_timer_t, frame_ms) == TM_FRAME_MS && offsetof(st_timer_t, frame_s) == TM_FRAME_S &&
	       offsetof(st_timer_t, total_ms) == TM_TOTAL_MS && offsetof(st_timer_t, total_s) == TM_TOTAL_S &&
	       offsetof(st_timer_t, num) == TM_NUM && offsetof(st_timer_t, den) == TM_DEN && offsetof(st_timer_t, scale) == TM_SCALE &&
	       offsetof(st_timer_t, carry) == TM_CARRY && offsetof(st_hobj_t, pos) == OBJ_POS && offsetof(st_hobj_t, timer) == OBJ_TIMER &&
	       offsetof(st_char_t, flags) == CH_FLAGS && offsetof(st_combat_t, owner) == C_OWNER && offsetof(st_combat_t, ai) == C_AI &&
	       offsetof(st_combat_t, player) == C_PLAYER && offsetof(st_combat_t, hitdir) == C_HITDIR &&
	       offsetof(st_combat_t, stim) == C_STIM && offsetof(st_combat_t, sync) == C_SYNC &&
	       offsetof(st_combat_t, hitonce) == C_HITONCE && offsetof(st_combat_t, dead) == C_DEAD &&
	       offsetof(st_combat_t, hobj) == C_HOBJ && offsetof(st_combat_t, attack) == RX_C_ATTACK &&
	       sizeof(st_band_t) == PCB_SIZE && offsetof(st_pcomb_t, band) == PC_BANDS &&
	       offsetof(st_pcomb_t, server) == PC_SERVER && offsetof(st_cat_t, count) == CAT_COUNT &&
	       offsetof(st_cat_t, recs) == CAT_RECS, "stand-in combat layouts");
static void *volatile st_vt_combat[4], *volatile st_vt_npc[4], *volatile st_vt_T[16], *volatile st_vt_etimer[8];
static struct { uintptr_t vt; } st_T;
static void *g_T_ptr = &st_T;     /* *0x466c718 */
static st_timer_t g_tpool[4], g_orc_timer;
static int g_ntimer, g_tcreate_calls, g_tget_calls, g_tset_calls, g_trel_calls;
static st_char_t g_och[5];
static st_hobj_t g_oobj[4];
static st_combat_t g_oc[4], g_pcs; /* the orcs A (ahead), B (behind), C (dead), D (not an NPC); the player's server Combat */
static st_band_t g_bands[2][4];
static st_pcomb_t g_pcomb;
static char g_recs[6][DBREC_SIZE];
static const char *const g_rec_names[6] = { "ST_L1", "ST_L2", "ST_H1", "ST_H2", "ST_OTHER", "ST_X" };
static st_cat_t g_cat;
static void *g_cat_ptr = &g_cat;  /* *0x4b568d0 */
static char g_stim_mgr[64];
static void *g_stim_mgr_ptr = g_stim_mgr; /* *0x4c500e0 */
static unsigned char g_nostim_byte;
static struct { int n, live[64], removed; uintptr_t c, ad, target; int32_t next; } g_st;
static struct { int n, kill_next; float pos[3], dir[3]; uintptr_t attacker, target, attack; int kill; uint8_t once, sever, atype; } g_dh, g_dhs[8];
static int g_rx_block; /* (the REACT stand-ins below: orc A is in its block) */
static volatile uint8_t *g_dh_sever_p; /* SOMVR_FINISH: the sever byte at the call (finish_tests) */

/* Combat_DealHit: what it was given (and the direction / "hit once" byte at the call); the game marks the hit */
__attribute__((noipa)) void st_dealhit(cb_ltvec pos, uintptr_t attacker, uintptr_t target, uintptr_t attack, int kill)
{
	st_combat_t *pc = (st_combat_t *)attacker;
	g_dh.attacker = attacker, g_dh.target = target, g_dh.attack = attack, g_dh.kill = kill;
	g_dh.pos[0] = pos.x, g_dh.pos[1] = pos.y, g_dh.pos[2] = pos.z;
	memcpy(g_dh.dir, pc->hitdir, sizeof g_dh.dir);
	g_dh.once = pc->hitonce;
	g_dh.sever = g_dh_sever_p ? *g_dh_sever_p : 0;
	g_dh.atype = ((uint8_t *)pc)[RX_C_NOBLOCK];
	pc->hitonce = 1;
	/* the damage context's rule (0x281769e): orc A in its block and the attacker's +0x55b clear = blocked (no damage);
	 * else 10 off its health (CCharacter+0x164) */
	for (int i = 0; i < 4; i++)
		if ((uintptr_t)g_oc[i].hobj == target && !(i == 0 && g_rx_block && !g_dh.atype)) {
			float *hp = (float *)((char *)&g_och[i] + CH_HEALTH);
			*hp -= 10;
		}
	for (int i = 0; i < 4 && g_dh.kill_next; i++)
		if ((uintptr_t)g_oc[i].hobj == target)
			g_oc[i].dead = 1, g_dh.kill_next = 0;
	if (g_dh.n < 8)
		g_dhs[g_dh.n] = g_dh;
	g_dh.n++;
}
__attribute__((noipa)) uintptr_t st_byname(const char *name)
{
	for (int i = 0; i < 6; i++)
		if (!strcmp(name, g_rec_names[i]))
			return (uintptr_t)g_recs[i];
	return 0;
}
__attribute__((noipa)) uintptr_t st_attackdata(uintptr_t rec) { return rec + 0x100000; }
/* FUN_026c7180: the old persistent stimulus goes first, then a new one */
__attribute__((noipa)) void st_stim(uintptr_t c, uintptr_t ad, uintptr_t target)
{
	st_combat_t *cc = (st_combat_t *)c;
	g_st.n++, g_st.c = c, g_st.ad = ad, g_st.target = target;
	if (cc->stim > 0 && cc->stim < 64)
		g_st.live[cc->stim] = 0;
	cc->stim = g_st.next = g_st.next % 62 + 1;
	g_st.live[cc->stim] = 1;
}
__attribute__((noipa)) uintptr_t st_stimfind(uintptr_t mgr, int32_t id)
{
	return mgr == (uintptr_t)g_stim_mgr && id > 0 && id < 64 && g_st.live[id];
}
__attribute__((noipa)) void st_stimremove(uintptr_t mgr, int32_t id)
{
	if (mgr == (uintptr_t)g_stim_mgr && id > 0 && id < 64)
		g_st.live[id] = 0, g_st.removed++;
}
static void tdrop(st_timer_t *t)
{
	if (t && --t->ref == 0)
		t->deleted = 1;
}
/* CreateTimer: the manager's reference and the caller's */
__attribute__((noipa)) void *st_tcreate(uintptr_t *out, void *T)
{
	(void)T;
	g_tcreate_calls++;
	st_timer_t *t = &g_tpool[g_ntimer++ % 4];
	memset(t, 0, sizeof *t);
	t->vt = (uintptr_t)st_vt_etimer, t->ref = 2, t->num = t->den = 1, t->scale = 1, t->frame_ms = 16;
	*out = (uintptr_t)t;
	return out;
}
__attribute__((noipa)) void *st_tget(uintptr_t *out, void *T, uintptr_t h)
{
	(void)T;
	g_tget_calls++;
	st_timer_t *t = ((st_hobj_t *)h)->timer;
	*out = (uintptr_t)t;
	if (t)
		t->ref++;
	return out;
}
__attribute__((noipa)) uint64_t st_tset(void *T, uintptr_t h, const uintptr_t *tp)
{
	(void)T;
	g_tset_calls++;
	st_hobj_t *o = (st_hobj_t *)h;
	st_timer_t *n = (st_timer_t *)*tp, *old = o->timer;
	if (n == old)
		return 0;
	if (n)
		n->ref++;
	o->timer = n;
	tdrop(old);
	return 0;
}
__attribute__((noipa)) void st_trelease(uintptr_t *p)
{
	g_trel_calls++;
	tdrop((st_timer_t *)*p);
}

static void entry_site(void *fn, struct site_spec *s, const char *name, int i)
{
	hex_of(fn, 8, g_hex[i], 128);
	hex_of(fn, 4, g_hex[i] + 128, 128);
	*s = (struct site_spec){ name, K_ENTRY, (uintptr_t)fn, (uintptr_t)fn, g_hex[i], (uintptr_t)fn, g_hex[i] + 128 };
}

/* Talion at (500 100 -50) facing +Z; A 110 cm ahead (its timer shared by 2 other holders), B 3 m behind, C dead and D
 * with a non-NPC owner 110 cm away at 45 deg right / left (both in the fake swing's path), in the bands with A twice,
 * the player's own Combat and an unmapped pointer */
static void cb_world(void)
{
	static const float pos[4][3] = { { 500, 100, 60 }, { 500, 100, -350 }, { 577.78f, 100, 27.78f }, { 422.22f, 100, 27.78f } };
	st_T.vt = (uintptr_t)st_vt_T;
	memset(&g_orc_timer, 0, sizeof g_orc_timer);
	g_orc_timer.vt = (uintptr_t)st_vt_etimer, g_orc_timer.ref = 3, g_orc_timer.frame_ms = 11, g_orc_timer.frame_s = 0.011f;
	g_orc_timer.total_ms = 123456, g_orc_timer.total_s = 123.456, g_orc_timer.num = g_orc_timer.den = 1;
	g_orc_timer.scale = 1, g_orc_timer.carry = 0.25;
	for (int i = 0; i < 4; i++) {
		memset(&g_oc[i], 0, sizeof g_oc[i]);
		g_och[i].vt = i == 3 ? 0x4444 : (uintptr_t)st_vt_npc, g_och[i].flags = i == 2;
		memcpy(g_oobj[i].pos, pos[i], sizeof g_oobj[i].pos);
		g_oobj[i].rot[3] = 1, g_oobj[i].timer = i ? NULL : &g_orc_timer;
		g_oc[i].vt = (uintptr_t)st_vt_combat, g_oc[i].owner = &g_och[i], g_oc[i].ai = (void *)1, g_oc[i].hobj = &g_oobj[i];
	}
	memset(&g_pcs, 0, sizeof g_pcs);
	g_och[4].vt = 0x5555;
	g_pcs.vt = (uintptr_t)st_vt_combat, g_pcs.owner = &g_och[4], g_pcs.player = (void *)1, g_pcs.hobj = (st_hobj_t *)&g_pobj;
	g_pcs.hitdir[0] = 7, g_pcs.hitdir[1] = 8, g_pcs.hitdir[2] = 9, g_pcs.hitonce = 0x5a;
	st_combat_t *b0[4] = { &g_oc[0], &g_oc[1], &g_pcs, (st_combat_t *)0x10 }, *b1[3] = { &g_oc[2], &g_oc[3], &g_oc[0] };
	for (int i = 0; i < 4; i++)
		g_bands[0][i].c = b0[i];
	for (int i = 0; i < 3; i++)
		g_bands[1][i].c = b1[i];
	g_pcomb.band[0].b = g_bands[0], g_pcomb.band[0].e = g_bands[0] + 4;
	g_pcomb.band[1].b = g_bands[1], g_pcomb.band[1].e = g_bands[1] + 3;
	g_pcomb.server = &g_pcs;
	g_player.pcomb = &g_pcomb;
	g_cat.count = 0, g_cat.recs = g_recs[0];
	g_cbg.vt_combat = (uintptr_t)st_vt_combat, g_cbg.vt_npc = (uintptr_t)st_vt_npc, g_cbg.atkcat_pp = (uintptr_t)&g_cat_ptr;
	g_cbg.stim_mgr_pp = (uintptr_t)&g_stim_mgr_ptr, g_cbg.nostim = (uintptr_t)&g_nostim_byte;
	g_cbg.timer_pp = (uintptr_t)&g_T_ptr, g_cbg.vt_timer = (uintptr_t)st_vt_T, g_cbg.vt_etimer = (uintptr_t)st_vt_etimer;
}

static void cb_frame(void)
{
	g_cb_test_t += 1.0 / 90;
	frame();
}
static void cb_cycle(void) /* one fake cycle at 90 Hz: the heavy swing, then the light one */
{
	for (int i = 0; i < (int)(SC_FAKE_PERIOD * 90 + 0.5); i++)
		cb_frame();
}
static float axis_dist(const float p[3], int orc)
{
	return hypotf(p[0] - g_oobj[orc].pos[0], p[2] - g_oobj[orc].pos[2]);
}
static void *cb_thread(void *a)
{
	(void)a;
	combat_frame((uintptr_t)&g_player, 0);
	return NULL;
}

static void combat_tests(void)
{
	g_pobj.pos[0] = 500, g_pobj.pos[1] = 100, g_pobj.pos[2] = -50, g_pobj.rot[1] = 0, g_pobj.rot[3] = 1;
	g_pl = &g_player;
	g_cb.mode = CB_FULL, g_cb.blade = CB_BLADE_FAKE, g_cb.warned = 0, g_cb.prev.valid = 0;
	g_cb_test_t = 1000, g_cb.t0 = 1000, g_cb.next_log = 1001, g_cb.last_line = 1000;
	sc_speed_reset(&g_cb.sp);
	sc_cool_reset(&g_cb.cool);
	sc_charges_init(&g_cb.ch, &g_cb.cfg, g_cb_test_t);
	/* 1. the database is not loaded yet: the contacts and tiers, no call, no name given up */
	cb_cycle();
	EXPECT(g_dh.n == 0 && g_st.n == 0 && g_cb.n.nocall == 2 && g_cb.n.hits[SC_TIER_HEAVY] == 1 && g_cb.n.hits[SC_TIER_LIGHT] == 1,
	       "no database: calls %d stims %d nocall %llu heavy %llu light %llu", g_dh.n, g_st.n,
	       (unsigned long long)g_cb.n.nocall, (unsigned long long)g_cb.n.hits[SC_TIER_HEAVY],
	       (unsigned long long)g_cb.n.hits[SC_TIER_LIGHT]);
	EXPECT(g_cb.rec[SC_TIER_HEAVY][0].state == 0 && g_cb.rec[SC_TIER_LIGHT][0].state == 0, "a name was given up early");
	/* 2. one cycle: a heavy hit (ST_H1) and a light one (ST_L1) on A only, through Combat_DealHit as `dealhit` calls it;
	 * the stimulus at each swing start, removed at its end; A frozen for 120 ms after the heavy hit */
	g_cat.count = 6;
	int frozen = 0, n0 = g_dh.n, restored = 1;
	st_timer_t *mine = NULL;
	for (int i = 0; i < (int)(SC_FAKE_PERIOD * 90 + 0.5); i++) {
		int k = g_dh.n;
		cb_frame();
		if (g_dh.n > k)
			restored &= g_pcs.hitonce == 0x5a && g_pcs.hitdir[0] == 7 && g_pcs.hitdir[1] == 8 && g_pcs.hitdir[2] == 9;
		if (g_cb.hs.mine)
			mine = (st_timer_t *)g_cb.hs.mine;
		frozen += mine && g_oobj[0].timer == mine;
	}
	EXPECT(g_dh.n == n0 + 2, "one cycle: %d calls", g_dh.n - n0);
	for (int k = 0; k < 2 && g_dh.n == n0 + 2; k++) {
		typeof(g_dh) *d = &g_dhs[n0 + k];
		float l = sqrtf(d->dir[0] * d->dir[0] + d->dir[1] * d->dir[1] + d->dir[2] * d->dir[2]);
		EXPECT(d->attack == (uintptr_t)g_recs[k ? 0 : 2] && d->target == (uintptr_t)&g_oobj[0] && d->attacker == (uintptr_t)&g_pcs &&
			       !d->kill && d->once == 0,
		       "hit %d: attack %#lx target %#lx attacker %#lx kill %d once %u", k, (unsigned long)d->attack,
		       (unsigned long)d->target, (unsigned long)d->attacker, d->kill, d->once);
		EXPECT(NEAR(l, 1, 1e-5) && NEAR(d->dir[1], 0, 1e-5) && (k ? d->dir[0] > 0.8f : d->dir[0] < -0.8f),
		       "hit %d: dir %g %g %g (the blade's velocity: heavy to the left, light to the right)", k, d->dir[0],
		       d->dir[1], d->dir[2]);
		EXPECT(NEAR(axis_dist(d->pos, 0), 35, 1e-2) && NEAR(d->pos[1], 172.5 - SC_FAKE_DROP_CM, 1e-2),
		       "hit %d: pos %g %g %g not on A's capsule at the blade's height", k, d->pos[0], d->pos[1], d->pos[2]);
	}
	EXPECT(restored && g_pcs.hitonce == 0x5a && g_pcs.hitdir[0] == 7, "pc+0x15c / +0x27a not restored after the call");
	EXPECT(g_tcreate_calls == 1 && mine && mine->vt == (uintptr_t)st_vt_etimer && mine->ref == 2 && mine->total_ms == 123456 &&
		       mine->total_s == 123.456 && mine->carry == 0.25 && mine->num == 0 && mine->den == 1 && mine->scale == 0 &&
		       mine->frame_ms == 0 && mine->frame_s == 0,
	       "the frozen timer: creates %d ref %u total %u %g carry %g num %u den %u scale %g frame %u", g_tcreate_calls,
	       mine ? mine->ref : 0, mine ? mine->total_ms : 0, mine ? mine->total_s : 0, mine ? mine->carry : 0,
	       mine ? mine->num : 0, mine ? mine->den : 0, mine ? mine->scale : 0, mine ? mine->frame_ms : 0);
	EXPECT(frozen >= 10 && frozen <= 11 && g_oobj[0].timer == &g_orc_timer && g_orc_timer.ref == 3 && !g_orc_timer.deleted &&
		       g_cb.n.hs_on == 1 && g_cb.n.hs_off == 1 && !g_cb.hs.on,
	       "hit-stop: %d frozen frames (want 10-11 for 120 ms), timer back %d, ref %u, on %llu off %llu", frozen,
	       g_oobj[0].timer == &g_orc_timer, g_orc_timer.ref, (unsigned long long)g_cb.n.hs_on,
	       (unsigned long long)g_cb.n.hs_off);
	EXPECT(g_st.n == 2 && g_st.c == (uintptr_t)&g_pcs && g_st.ad == (uintptr_t)g_recs[0] + 0x100000 &&
		       g_st.target == (uintptr_t)&g_oobj[0] && g_st.removed == 2 && g_pcs.stim == 0,
	       "stimulus: %d calls c %#lx ad %#lx target %#lx removed %d pc+0x168 %d", g_st.n, (unsigned long)g_st.c,
	       (unsigned long)g_st.ad, (unsigned long)g_st.target, g_st.removed, g_pcs.stim);
	for (int i = 1; i < 64; i++)
		EXPECT(!g_st.live[i], "stimulus %d still live", i);
	/* 3. the next cycle: the heavy list's next usable record (ST_MISSING skipped for the run), the light list's next */
	n0 = g_dh.n;
	cb_cycle();
	EXPECT(g_dh.n == n0 + 2 && g_dhs[n0].attack == (uintptr_t)g_recs[3] && g_dhs[n0 + 1].attack == (uintptr_t)g_recs[1] &&
		       g_cb.rec[SC_TIER_HEAVY][1].state == -1 && g_tcreate_calls == 1 && g_orc_timer.ref == 3,
	       "round robin: calls %d attacks %#lx %#lx, ST_MISSING state %d, creates %d", g_dh.n - n0,
	       (unsigned long)g_dhs[n0].attack, (unsigned long)g_dhs[n0 + 1].attack, g_cb.rec[SC_TIER_HEAVY][1].state,
	       g_tcreate_calls);
	EXPECT(log_has("COMBAT record heavy 'ST_MISSING' -> 0 (not in Combat/Attack") && log_has("COMBAT record heavy 'ST_H2'"),
	       "no COMBAT record lines");
	/* 3b. build 10, SOMVR_COMBAT_KNOCKBACK=stagger: the heavy hit dealt with the light record (a light hit's reaction)
	 * after A's health took the difference (3 x a light hit's 10): 100 -> 70 -> 60, then the light hit 50; a heavy hit
	 * that would kill leaves 5 and the light hit kills (a light death) */
	{
		float *ahp = (float *)((char *)&g_och[0] + CH_HEALTH), hp_was = *ahp;
		int mode0 = g_kx.mode;
		g_kx.mode = KX_STAGGER, g_kx.light_dmg = 10;
		*ahp = 100;
		n0 = g_dh.n;
		uint64_t st0 = g_kx.staggers;
		cb_cycle();
		int light_rec = g_dh.n >= n0 + 1 && (g_dhs[n0].attack == (uintptr_t)g_recs[0] || g_dhs[n0].attack == (uintptr_t)g_recs[1]);
		EXPECT(g_dh.n == n0 + 2 && light_rec && g_kx.staggers == st0 + 1 && NEAR(*ahp, 50, 0.01) && NEAR(g_kx.light_dmg, 10, 0.01),
		       "knockback stagger: calls %d first attack %#lx (light %d) staggers %llu health %g light dmg %g", g_dh.n - n0,
		       (unsigned long)g_dhs[n0].attack, light_rec, (unsigned long long)(g_kx.staggers - st0), *ahp, g_kx.light_dmg);
		*ahp = 35; /* 35 - 30 = 5: the heavy hit kills: 5 left for the light hit, which kills (a light death) */
		n0 = g_dh.n, st0 = g_kx.staggers;
		cb_cycle();
		EXPECT(g_dh.n >= n0 + 1 && g_kx.staggers == st0 + 1 &&
			       (g_dhs[n0].attack == (uintptr_t)g_recs[0] || g_dhs[n0].attack == (uintptr_t)g_recs[1]) && !(*ahp > 0),
		       "knockback stagger: the kill not a light one (staggers %llu, health %g)", (unsigned long long)(g_kx.staggers - st0),
		       *ahp);
		g_kx.mode = mode0, *ahp = hp_was, g_oc[0].dead = 0;
	}
	/* 4. the heavy hit kills A: no hit-stop; the light swing then finds no live A (dead orcs are not candidates) */
	uint64_t hs0 = g_cb.n.hs_on, k0 = g_cb.n.kills;
	n0 = g_dh.n;
	g_dh.kill_next = 1;
	cb_cycle();
	EXPECT(g_dh.n == n0 + 1 && g_cb.n.hs_on == hs0 && g_cb.n.kills == k0 + 1, "kill: calls %d hitstops %llu kills %llu",
	       g_dh.n - n0, (unsigned long long)(g_cb.n.hs_on - hs0), (unsigned long long)(g_cb.n.kills - k0));
	g_oc[0].dead = 0;
	/* 5. death, a sync action and a despawn while A is frozen: back at once (death, sync), never written to (despawn) */
	for (int sc = 0; sc < 3; sc++) {
		int seen = 0, sets = 0;
		for (int i = 0; i < (int)(SC_FAKE_PERIOD * 90 + 0.5); i++) {
			cb_frame();
			if (g_cb.hs.on && !seen) {
				seen = 1, sets = g_tset_calls;
				if (sc == 0)
					g_och[0].flags = 1;
				else if (sc == 1)
					g_oc[0].sync[1] = (void *)1;
				else
					g_oc[0].vt = 0;
				cb_frame();
				i++;
				EXPECT(!g_cb.hs.on, "case %d: still frozen", sc);
				if (sc < 2)
					EXPECT(g_tset_calls == sets + 1 && g_oobj[0].timer == &g_orc_timer && g_orc_timer.ref == 3,
					       "case %d: not restored (sets %d timer back %d ref %u)", sc, g_tset_calls - sets,
					       g_oobj[0].timer == &g_orc_timer, g_orc_timer.ref);
				else
					EXPECT(g_tset_calls == sets && g_oobj[0].timer == mine && g_orc_timer.ref == 2 && mine->ref == 3,
					       "despawn: sets %d, timer %p, old ref %u, ours %u", g_tset_calls - sets,
					       (void *)g_oobj[0].timer, g_orc_timer.ref, mine->ref);
				g_och[0].flags = 0, g_oc[0].sync[1] = NULL, g_oc[0].vt = (uintptr_t)st_vt_combat;
				if (sc == 2) /* a new A: the engine dropped the gone object's reference to ours */
					g_oobj[0].timer = &g_orc_timer, g_orc_timer.ref++, mine->ref--;
			}
		}
		EXPECT(seen, "case %d: no hit-stop", sc);
	}
	EXPECT(log_has("(death,") && log_has("(sync action,") && log_has("COMBAT hitstop lost"), "no hit-stop off/lost lines");
	/* a heavy hit while A is in a sync action: no hit-stop */
	uint64_t sk = g_cb.n.hs_skip;
	g_oc[0].sync[0] = (void *)1;
	cb_cycle();
	g_oc[0].sync[0] = NULL;
	EXPECT(g_cb.n.hs_skip == sk + 1 && g_oobj[0].timer == &g_orc_timer && log_has("the orc is in a sync action"),
	       "sync: skipped %llu", (unsigned long long)(g_cb.n.hs_skip - sk));
	/* 6. no hit charge: glancing ticks only */
	n0 = g_dh.n;
	uint64_t nc = g_cb.n.nocharge;
	cb_cycle();
	g_cb.ch.n = -20, g_cb.ch.t = g_cb_test_t; /* (each cycle starts and ends with the blade at rest) */
	cb_cycle();
	EXPECT(g_dh.n == n0 + 2 && g_cb.n.nocharge == nc + 2 && log_has("no hit charge: no damage"), "no charge: calls %d nocharge %llu",
	       g_dh.n - n0, (unsigned long long)(g_cb.n.nocharge - nc));
	sc_charges_init(&g_cb.ch, &g_cb.cfg, g_cb_test_t);
	/* 6a. SOMVR_COMBAT_HIT_MARKS=1: one mark at the first hit (SOMVR_MARK off here: counted only) */
	g_cb.hit_marks = 1;
	cb_cycle();
	EXPECT(g_cb.hit_marks == 0, "hit marks: %d left", g_cb.hit_marks);
	/* 6b. the blade at rest in an orc (round 2): B moved to where the fake's held blade lies (+75 deg, 90 cm out), the
	 * fake held still for 1 s (its clock pinned at 0.1 s into the cycle): one touch, one glancing tick, no rub (still),
	 * no hit (build 7: a tick every 0.25 s, the re-hit cooldown's rhythm); 0.2 s apart and back: one more */
	float bpos[3];
	memcpy(bpos, g_oobj[1].pos, sizeof bpos);
	g_oobj[1].pos[0] = 586.93f, g_oobj[1].pos[2] = -26.71f;
	uint64_t gl0 = g_cb.n.glance, tc0 = g_cb.n.touches, rb0 = g_cb.n.rubs, ct0 = g_cb.n.contacts;
	n0 = g_dh.n;
	for (int i = 0; i < 90; i++) {
		g_cb.t0 = g_cb_test_t + 1.0 / 90 - 0.1;
		cb_frame();
	}
	EXPECT(g_cb.n.contacts - ct0 >= 85 && g_cb.n.touches == tc0 + 1 && g_cb.n.glance == gl0 + 1 && g_cb.n.rubs == rb0 &&
		       g_dh.n == n0 && log_has("below the slash rule: no damage, no streak; the touch's start"),
	       "rest: contacts %llu touches %llu glances %llu rubs %llu calls %d", (unsigned long long)(g_cb.n.contacts - ct0),
	       (unsigned long long)(g_cb.n.touches - tc0), (unsigned long long)(g_cb.n.glance - gl0),
	       (unsigned long long)(g_cb.n.rubs - rb0), g_dh.n - n0);
	g_oobj[1].pos[2] = -350;
	for (int i = 0; i < 18; i++) {
		g_cb.t0 = g_cb_test_t + 1.0 / 90 - 0.1;
		cb_frame();
	}
	g_oobj[1].pos[2] = -26.71f;
	for (int i = 0; i < 10; i++) {
		g_cb.t0 = g_cb_test_t + 1.0 / 90 - 0.1;
		cb_frame();
	}
	EXPECT(g_cb.n.touches == tc0 + 2 && g_cb.n.glance == gl0 + 2, "apart and back: touches %llu glances %llu",
	       (unsigned long long)(g_cb.n.touches - tc0), (unsigned long long)(g_cb.n.glance - gl0));
	memcpy(g_oobj[1].pos, bpos, sizeof bpos);
	g_cb.t0 = g_cb_test_t; /* the next cycle from its start again */
	/* 7. log mode: contacts and tiers, no game call at all */
	int c0 = g_tcreate_calls + g_tget_calls + g_tset_calls + g_trel_calls + g_st.n;
	n0 = g_dh.n;
	g_cb.mode = CB_LOG;
	uint64_t h0 = g_cb.n.hits[SC_TIER_HEAVY];
	cb_cycle();
	EXPECT(g_dh.n == n0 && g_tcreate_calls + g_tget_calls + g_tset_calls + g_trel_calls + g_st.n == c0 &&
		       g_cb.n.hits[SC_TIER_HEAVY] == h0 + 1 && log_has("-> no call (log only)") && log_has("-> no stimulus (log only)"),
	       "log mode: calls %d game calls %d", g_dh.n - n0, g_tcreate_calls + g_tget_calls + g_tset_calls + g_trel_calls + g_st.n - c0);
	g_cb.mode = CB_FULL;
	/* 8. parts off (a site mismatch at start, white box): no hit-stop, then no hits */
	g_cb.ok_stop = 0;
	hs0 = g_cb.n.hs_on;
	n0 = g_dh.n;
	cb_cycle();
	EXPECT(g_dh.n == n0 + 2 && g_cb.n.hs_on == hs0 && log_has("a CB_TIMER/CB_OBJTIMER site did not match"), "hit-stop off");
	g_cb.ok_hit = 0;
	n0 = g_dh.n;
	cb_cycle();
	EXPECT(g_dh.n == n0 && log_has("a CB_DEALHIT/CB_ATK_BYNAME site did not match"), "hits off: %d calls", g_dh.n - n0);
	g_cb.ok_hit = g_cb.ok_stop = 1;
	/* 9. the controller blade without a tracked grip (no layer yet): no blade, said once */
	g_cb.blade = CB_BLADE_CONTROLLER;
	uint64_t nb = g_cb.n.noblade, ct = g_cb.n.contacts;
	n0 = g_dh.n;
	cb_cycle();
	EXPECT(g_cb.n.noblade == nb + 216 && g_cb.n.contacts == ct && g_dh.n == n0 &&
		       log_has("COMBAT no blade: SOMVR_BLADE=controller has no tracked right grip"), "controller: noblade %llu contacts %llu",
	       (unsigned long long)(g_cb.n.noblade - nb), (unsigned long long)(g_cb.n.contacts - ct));
	g_cb.blade = CB_BLADE_FAKE;
	/* 10. another thread, a mono frame (stereo off: H4's flat path), combat off */
	uint64_t fr = g_cb.n.frames, ot = g_cb.n.other;
	pthread_t th;
	pthread_create(&th, NULL, cb_thread, NULL);
	pthread_join(th, NULL);
	EXPECT(g_cb.n.frames == fr && g_cb.n.other == ot + 1, "another thread ran a combat frame");
	g_layout = SOMVR_LAYOUT_MONO;
	cb_frame();
	g_layout = SOMVR_LAYOUT_A;
	EXPECT(g_cb.n.frames == fr + 1 && g_h4_calls == 1, "the flat path: frames %llu", (unsigned long long)(g_cb.n.frames - fr));
	g_cb.mode = CB_OFF;
	cb_frame();
	EXPECT(g_cb.n.frames == fr + 1, "combat off ran a frame");
	EXPECT(log_has("COMBAT config mode=1 blade=fake stim=1 hitstop_ms=120") && log_has("COMBAT site CB_DEALHIT ok") &&
		       log_has("COMBAT site CB_OBJTIMER_GET ok") && log_has("COMBAT t=") && log_has("COMBAT stimulus FUN_026c7180(pc=") &&
		       log_has("COMBAT swing end (the swing ended)") && log_has("persistent stimulus id 1 (now 1) removed") &&
		       log_has("COMBAT hitstop on orc=") && log_has("COMBAT hit tier=heavy") && log_has("-> Combat_DealHit(pc=") &&
		       log_has("COMBAT the 'no stimuli from the player' byte"),
	       "COMBAT lines missing from the log");
	/* K_ENTRY specs: wrong bytes / an entry that is not its own target are refused */
	char why[512];
	struct site_spec bad = g_cb_sites[CS_DEALHIT];
	bad.ctx_hex = "00 00 00 00 00 00 00 00";
	EXPECT(check_site(&bad, why, sizeof why) != 0, "a K_ENTRY with wrong bytes passed");
	bad = g_cb_sites[CS_DEALHIT];
	bad.target = g_cb_sites[CS_STIMULUS].target;
	EXPECT(check_site(&bad, why, sizeof why) != 0, "a K_ENTRY whose target is another function passed");
	printf("combat heavy+light per cycle on A only (ST_H1/ST_L1, then ST_H2/ST_L2), dir = the blade, pos on the capsule, "
	       "pc restored; stimulus x2 removed; hit-stop %d frames, back on death/sync, lost on despawn; kill, no charge, "
	       "log mode, parts off, controller without a grip, other thread, flat path; %llu hits %llu calls\n", frozen,
	       (unsigned long long)(g_cb.n.hits[SC_TIER_LIGHT] + g_cb.n.hits[SC_TIER_HEAVY]), (unsigned long long)g_cb.n.calls);
	g_pl = NULL;
	g_cb_test_t = -1;
}

/* ---- SOMVR_COMBAT_REACT: stand-ins for the database (the four schema holders, records with sorted and unsorted
 * attribute entries: orc A's character combat record -> its AIBlockTable -> two CombatAIBlockChance entries, the light
 * records' Category), orc A's NPC (its record holder, working memory, brain), a player Combat with room up to +0x55b
 * and its CCharacter, FUN_026ff330, the roll FUN_00c73440, FUN_029ac890 and FUN_026d4dc0 (K_ENTRY sites from the
 * stand-ins' own bytes) */
typedef struct { uint32_t hash, alt; uint8_t b8, idx, type, b11; uint16_t count, slot; } st_dbent_t;
typedef struct { uint8_t n, flags, pad[6]; st_dbent_t *ents; uint64_t *vals; uint64_t pad2; const char *name; } st_dbrec_t;
typedef struct { const char *name; uint32_t hash; char pad[0x30 - 12]; } st_sch_t;
typedef struct {
	uintptr_t vt;
	void *hobj, *holder_box;
	char p0[CH_FLAGS - 0x18];
	uint8_t flags;
	char p1[RX_NPC_WM - CH_FLAGS - 1];
	void *wm;
	uint8_t *brain;
} st_rx_npc_t;
_Static_assert(sizeof(st_dbent_t) == 16 && offsetof(st_dbent_t, idx) == 9 && offsetof(st_dbent_t, type) == 0xa &&
	       offsetof(st_dbent_t, count) == 0xc && offsetof(st_dbent_t, slot) == 0xe && offsetof(st_dbrec_t, ents) == 8 &&
	       offsetof(st_dbrec_t, vals) == 0x10 && offsetof(st_dbrec_t, name) == 0x20 && sizeof(st_dbrec_t) <= DBREC_SIZE &&
	       sizeof(st_sch_t) == 0x30 && offsetof(st_sch_t, hash) == 8 && offsetof(st_rx_npc_t, hobj) == RX_GB_HOBJ &&
	       offsetof(st_rx_npc_t, holder_box) == RX_NPC_RECHOLD && offsetof(st_rx_npc_t, flags) == CH_FLAGS &&
	       offsetof(st_rx_npc_t, wm) == RX_NPC_WM && offsetof(st_rx_npc_t, brain) == RX_NPC_BRAIN,
	       "stand-in react layouts");
#define H_ABT 0x0abc0056u  /* AIBlockTable */
#define H_OTHER 0x0f000000u /* another field of the character combat record */
#define H_ENT 0x0e000000u  /* Entries */
#define H_CAT 0x0ca70000u  /* Category */
#define H_BC 0x0bc00001u   /* BlockChance */
#define H_ACAT 0x0a7c0005u /* Combat/Attack Category */
static st_sch_t g_rs_ccr[RX_CCR_AIBLOCKTABLE + 1], g_rs_btbl[1], g_rs_bent[2], g_rs_atk[RX_ATK_CATEGORY + 1];
static void *g_rs_p[4] = { g_rs_ccr, g_rs_btbl, g_rs_bent, g_rs_atk };
static st_dbrec_t g_rd_cat_light, g_rd_cat_other, g_rd_ent[2], g_rd_tbl, g_rd_ccr;
static st_dbent_t g_rd_ent_e[2][2], g_rd_tbl_e[1], g_rd_ccr_e[2], g_rd_atk_e[1];
static uint64_t g_rd_ent_v[2][2], g_rd_tbl_v[2], g_rd_ccr_v[2], g_rd_atk_v[1];
static uintptr_t g_rx_box[16];
static char g_rx_hold[16], g_rx_wm[16];
static uint8_t g_rx_brain[RX_BRAIN_REPLAN + 8];
static st_rx_npc_t g_rx_npc;
static struct { uintptr_t vt; void *hobj; } g_rx_pown; /* the player's CCharacter: +8 its HOBJECT */
static union { st_combat_t c; unsigned char raw[0x600]; } g_rx_pcs; /* the player's server Combat, +0x55b readable */
static struct { int charrec, roll, notify, blocking; float lo, hi; uintptr_t own, npc; } g_rxc;
static float g_rx_rolls[4];
static int g_rx_nroll = 1, g_rx_ri, g_rx_block;
static char g_rxhex[RS_N][256];
static struct site_spec g_rxs[RS_N];

__attribute__((noipa)) uintptr_t st_rx_charrec(uintptr_t hold)
{
	g_rxc.charrec++;
	return hold == (uintptr_t)g_rx_hold ? (uintptr_t)&g_rd_ccr : 0;
}
__attribute__((noipa)) float st_rx_roll(float lo, float hi)
{
	g_rxc.roll++, g_rxc.lo = lo, g_rxc.hi = hi;
	return lo + (hi - lo) * g_rx_rolls[g_rx_ri++ % g_rx_nroll];
}
__attribute__((noipa)) void st_rx_notify(uintptr_t own, uintptr_t npc)
{
	g_rxc.notify++, g_rxc.own = own, g_rxc.npc = npc;
	((st_rx_npc_t *)npc)->brain[RX_BRAIN_REPLAN] = 1;
}
__attribute__((noipa)) uint8_t st_rx_blocking(uintptr_t c)
{
	g_rxc.blocking++;
	return c == (uintptr_t)&g_oc[0] && g_rx_block;
}
static void rx_entry_site(void *fn, struct site_spec *s, const char *name, int i)
{
	hex_of(fn, 8, g_rxhex[i], 128);
	hex_of(fn, 4, g_rxhex[i] + 128, 128);
	*s = (struct site_spec){ name, K_ENTRY, (uintptr_t)fn, (uintptr_t)fn, g_rxhex[i], (uintptr_t)fn, g_rxhex[i] + 128 };
}
/* RX_ATK_GATE's stand-in: a real E8 call to a stand-in "current attack" getter (never called by somvr: it only reads) */
__attribute__((noipa)) uintptr_t st_rx_curatk(uintptr_t c) { return c ? ((st_combat_t *)c)->attack : 0; }
__attribute__((noipa)) uintptr_t st_rx_gate(uintptr_t c) { return st_rx_curatk(c) ^ 0x5a; }
static int rx_call_site(void *caller, void *target, struct site_spec *s, const char *name, int i)
{
	const unsigned char *p = caller;
	for (int k = 0; k < 96; k++) {
		int32_t rel;
		memcpy(&rel, p + k + 1, 4);
		if (p[k] == 0xe8 && (uintptr_t)(p + k + 5 + rel) == (uintptr_t)target) {
			hex_of(p + k, 5, g_rxhex[i], 128);
			hex_of(target, 4, g_rxhex[i] + 128, 128);
			*s = (struct site_spec){ name, K_CALL, (uintptr_t)(p + k), (uintptr_t)(p + k), g_rxhex[i], (uintptr_t)target,
						 g_rxhex[i] + 128 };
			return 0;
		}
	}
	return -1;
}
static int rx_stand_in_sites(void) /* main, before somvr_start */
{
	rx_entry_site((void *)st_rx_charrec, &g_rxs[RS_CHARREC], "RX_CHARREC", RS_CHARREC);
	rx_entry_site((void *)st_rx_roll, &g_rxs[RS_ROLL], "RX_ROLL", RS_ROLL);
	rx_entry_site((void *)st_rx_notify, &g_rxs[RS_NOTIFY], "RX_NOTIFY", RS_NOTIFY);
	rx_entry_site((void *)st_rx_blocking, &g_rxs[RS_BLOCKING], "RX_BLOCKING", RS_BLOCKING);
	g_rx_sites = g_rxs;
	return rx_call_site((void *)st_rx_gate, (void *)st_rx_curatk, &g_rxs[RS_ATKGATE], "RX_ATK_GATE", RS_ATKGATE);
}
static uint64_t fbits(float f)
{
	uint32_t u;
	memcpy(&u, &f, 4);
	return u;
}
static st_dbent_t dbent(uint32_t hash, uint8_t type, uint16_t count, uint16_t slot)
{
	return (st_dbent_t){ .hash = hash, .type = type, .count = count, .slot = slot };
}
/* the database (entry 0: Category ST_CAT_OTHER, 0.9; entry 1: ST_CAT_LIGHT, 0.6; ST_L1/ST_L2 are ST_CAT_LIGHT), orc A's
 * NPC, the player Combat with its CCharacter */
static void rx_world(void)
{
	g_rs_ccr[RX_CCR_AIBLOCKTABLE] = (st_sch_t){ "AIBlockTable", H_ABT, { 0 } };
	g_rs_btbl[RX_BTBL_ENTRIES] = (st_sch_t){ "Entries", H_ENT, { 0 } };
	g_rs_bent[RX_BENT_CATEGORY] = (st_sch_t){ "Category", H_CAT, { 0 } };
	g_rs_bent[RX_BENT_CHANCE] = (st_sch_t){ "BlockChance", H_BC, { 0 } };
	g_rs_atk[RX_ATK_CATEGORY] = (st_sch_t){ "Category", H_ACAT, { 0 } };
	g_rxg.sch_ccr = (uintptr_t)&g_rs_p[0], g_rxg.sch_btbl = (uintptr_t)&g_rs_p[1];
	g_rxg.sch_bent = (uintptr_t)&g_rs_p[2], g_rxg.sch_attack = (uintptr_t)&g_rs_p[3];
	g_rd_cat_light = (st_dbrec_t){ .flags = 2, .name = "ST_CAT_LIGHT" };
	g_rd_cat_other = (st_dbrec_t){ .flags = 2, .name = "ST_CAT_OTHER" };
	static const float ch[2] = { 0.9f, 0.6f };
	st_dbrec_t *cat[2] = { &g_rd_cat_other, &g_rd_cat_light };
	for (int i = 0; i < 2; i++) { /* sorted by the hash at +0: BlockChance, then Category */
		g_rd_ent_e[i][0] = dbent(H_BC, 2, 1, 0), g_rd_ent_e[i][1] = dbent(H_CAT, 0xa, 1, 1);
		g_rd_ent_v[i][0] = fbits(ch[i]), g_rd_ent_v[i][1] = (uintptr_t)cat[i];
		g_rd_ent[i] = (st_dbrec_t){ 2, 3, { 0 }, g_rd_ent_e[i], g_rd_ent_v[i], 0, i ? "ST_BENT_1" : "ST_BENT_0" };
	}
	g_rd_tbl_e[0] = dbent(H_ENT, 0xa, 2, 0);
	g_rd_tbl_v[0] = (uintptr_t)&g_rd_ent[0], g_rd_tbl_v[1] = (uintptr_t)&g_rd_ent[1];
	g_rd_tbl = (st_dbrec_t){ 1, 3, { 0 }, g_rd_tbl_e, g_rd_tbl_v, 0, "ST_BLOCKTABLE" };
	/* unsorted (flags bit 0 clear): the search runs on the hashes at +4, +9 = the entry's own index */
	g_rd_ccr_e[0] = dbent(H_OTHER, 2, 1, 1), g_rd_ccr_e[0].alt = H_ABT, g_rd_ccr_e[0].idx = 1;
	g_rd_ccr_e[1] = dbent(H_ABT, 0xa, 1, 0), g_rd_ccr_e[1].alt = H_OTHER, g_rd_ccr_e[1].idx = 0;
	g_rd_ccr_v[0] = (uintptr_t)&g_rd_tbl, g_rd_ccr_v[1] = fbits(1.0f);
	g_rd_ccr = (st_dbrec_t){ 2, 2, { 0 }, g_rd_ccr_e, g_rd_ccr_v, 0, "ST_CCR_A" };
	g_rd_atk_e[0] = dbent(H_ACAT, 0xa, 1, 0);
	g_rd_atk_v[0] = (uintptr_t)&g_rd_cat_light;
	for (int i = 0; i < 2; i++) { /* ST_L1, ST_L2 */
		st_dbrec_t h = { 1, 1, { 0 }, g_rd_atk_e, g_rd_atk_v, 0, NULL };
		memcpy(g_recs[i], &h, sizeof h);
	}
	g_rx_box[0x70 / 8] = (uintptr_t)g_rx_hold;
	memset(g_rx_brain, 0, sizeof g_rx_brain);
	g_rx_npc = (st_rx_npc_t){ .vt = (uintptr_t)st_vt_npc, .hobj = &g_oobj[0], .holder_box = g_rx_box, .wm = g_rx_wm,
				   .brain = g_rx_brain };
	g_oc[0].owner = (st_char_t *)&g_rx_npc;
	g_rx_pown.vt = 0x6666, g_rx_pown.hobj = &g_pobj;
	memset(&g_rx_pcs, 0, sizeof g_rx_pcs);
	g_rx_pcs.c = g_pcs;
	g_rx_pcs.c.owner = (st_char_t *)&g_rx_pown;
	g_pcomb.server = &g_rx_pcs.c;
}
static void rx_unworld(void)
{
	g_oc[0].owner = &g_och[0];
	g_pcomb.server = &g_pcs;
	memset(g_recs[0], 0, sizeof g_recs[0]), memset(g_recs[1], 0, sizeof g_recs[1]);
}

static void react_tests(void)
{
	cb_world();
	g_cat.count = 6;
	rx_world();
	g_pobj.pos[0] = 500, g_pobj.pos[1] = 100, g_pobj.pos[2] = -50, g_pobj.rot[1] = 0, g_pobj.rot[3] = 1;
	g_pl = &g_player;
	g_cb.mode = CB_FULL, g_cb.blade = CB_BLADE_FAKE, g_cb.warned = 0, g_cb.prev.valid = 0;
	g_cb.ok_hit = g_cb.ok_stim = g_cb.ok_stop = 1;
	g_cb_test_t = 3000, g_cb.t0 = 3000, g_cb.next_log = 3001, g_cb.last_line = 3000;
	sc_speed_reset(&g_cb.sp), sc_cool_reset(&g_cb.cool), sc_charges_init(&g_cb.ch, &g_cb.cfg, g_cb_test_t);
	memset(g_cb.next, 0, sizeof g_cb.next); /* the records in turn from the first again (ST_L1 for the first swing) */
	g_rx.mode = RX_ON, g_rx.next_log = 3001, g_rx.last_line = 3000;
	sc_react_reset(&g_rx.rs), sc_cool_reset(&g_rx.notified);
	/* 1. one cycle: both swings head at A, one roll each (many frames head at it) with ST_L1's category against
	 * entry 1 (ST_CAT_LIGHT, 0.6; entry 0 is another category): 0.3 wins -> FUN_029ac890(the player's CCharacter, A's
	 * NPC), 0.8 loses; B behind, C dead, D not an NPC: none. A does not block: both hits as before, the heavy one frozen */
	g_rx_rolls[0] = 0.3f, g_rx_rolls[1] = 0.8f, g_rx_nroll = 2, g_rx_ri = 0;
	int d0 = g_dh.n;
	uint64_t hs0 = g_cb.n.hs_on;
	cb_cycle();
	EXPECT(g_rx.n.swings == 2 && g_rx.n.toward == 2 && g_rx.n.rolls == 2 && g_rx.n.won == 1 && g_rxc.roll == 2 &&
		       g_rxc.lo == 0 && g_rxc.hi == 1 && g_rxc.charrec == 2,
	       "rolls: swings %llu toward %llu rolls %llu won %llu (roll calls %d lo %g hi %g, record reads %d)",
	       (unsigned long long)g_rx.n.swings, (unsigned long long)g_rx.n.toward, (unsigned long long)g_rx.n.rolls,
	       (unsigned long long)g_rx.n.won, g_rxc.roll, g_rxc.lo, g_rxc.hi, g_rxc.charrec);
	EXPECT(g_rxc.notify == 1 && g_rxc.own == (uintptr_t)&g_rx_pown && g_rxc.npc == (uintptr_t)&g_rx_npc &&
		       g_rx_brain[RX_BRAIN_REPLAN] == 1 && g_rx.n.notifies == 1,
	       "notify: %d calls own %#lx npc %#lx replan %u", g_rxc.notify, (unsigned long)g_rxc.own, (unsigned long)g_rxc.npc,
	       g_rx_brain[RX_BRAIN_REPLAN]);
	EXPECT(g_dh.n == d0 + 2 && g_cb.n.hs_on == hs0 + 1 && g_rxc.blocking == 2 && g_rx.n.blocked == 0 &&
		       g_rx.n.open_ours == 2 && g_dh.target == (uintptr_t)&g_oobj[0],
	       "unblocked: %d calls, hitstops %llu, block reads %d, blocked %llu, open after ours %llu", g_dh.n - d0,
	       (unsigned long long)(g_cb.n.hs_on - hs0), g_rxc.blocking, (unsigned long long)g_rx.n.blocked,
	       (unsigned long long)g_rx.n.open_ours);
	EXPECT(log_has("REACT roll orc=") && log_has("record 'ST_CCR_A' AIBlockTable 'ST_BLOCKTABLE' 2 entries, swing 'ST_L1' "
						   "categories 'ST_CAT_LIGHT' -> chance 0.600 (entry 1) roll 0.300 -> notify") &&
		       log_has("roll 0.800 -> no notify") && log_has("REACT notify FUN_029ac890(attacker=") &&
		       log_has("REACT open orc=") && log_has("REACT schema: character combat record field 56 'AIBlockTable', "
							 "AIBlockTable field 0 'Entries', entry fields 0 'Category' / 1 'BlockChance', "
							 "Combat/Attack field 5 'Category'"),
	       "REACT roll/notify/open/schema lines missing");
	/* 2. A is in its block (FUN_026d4dc0 = 1): the same Combat_DealHit calls (the game's blocked branch decides), no
	 * hit-stop for the heavy one, REACT blocked; both within 2 s of a notify (rolls 0.1, 0.1) */
	g_rx_block = 1, g_rx_rolls[0] = g_rx_rolls[1] = 0.1f;
	d0 = g_dh.n, hs0 = g_cb.n.hs_on;
	uint64_t bl0 = g_rx.n.blocked;
	cb_cycle();
	EXPECT(g_dh.n == d0 + 2 && g_cb.n.hs_on == hs0 && g_rx.n.blocked == bl0 + 2 && g_rx.n.blocked_ours == 2 &&
		       g_dh.attack == (uintptr_t)g_recs[1] && g_dh.target == (uintptr_t)&g_oobj[0] &&
		       log_has("REACT blocked orc=") && log_has("ms after our notify -> Combat_DealHit as usual"),
	       "blocked: %d calls (last %#lx), hitstops %llu, blocked %llu (after ours %llu)", g_dh.n - d0,
	       (unsigned long)g_dh.attack, (unsigned long long)(g_cb.n.hs_on - hs0), (unsigned long long)(g_rx.n.blocked - bl0),
	       (unsigned long long)g_rx.n.blocked_ours);
	EXPECT(g_dh.atype == 0 && g_cb.n.blocked >= 2 && log_has("NO DAMAGE (Talion's attack type 0 = 0; the contact"),
	       "blocked: the type at the call %u, calls without damage %llu", g_dh.atype, (unsigned long long)g_cb.n.blocked);
	/* ... the player's Combat+0x55b stale (build 7's runs: 5, his last real attack's AttackType): with
	 * SOMVR_COMBAT_ATKTYPE=record (the default) our record's type is written for the call (ST_L1/ST_H1 have none: 0),
	 * so the block counts, and the stale byte is back after each call */
	g_rx_pcs.raw[RX_C_NOBLOCK] = 5;
	hs0 = g_cb.n.hs_on, bl0 = g_rx.n.blocked;
	uint64_t ty0 = g_cb.n.typed, nd0 = g_cb.n.blocked;
	cb_cycle();
	EXPECT(g_rx.n.through == 0 && g_rx.n.blocked == bl0 + 2 && g_cb.n.hs_on == hs0 && g_dh.atype == 0 &&
		       g_rx_pcs.raw[RX_C_NOBLOCK] == 5 && g_cb.n.typed == ty0 + 2 && g_cb.n.blocked == nd0 + 2 &&
		       log_has("(Talion's attack type 5 -> ours 0; the contact"),
	       "stale +0x55b, ATKTYPE=record: through %llu blocked %llu hitstops %llu type at the call %u, after %u, typed %llu",
	       (unsigned long long)g_rx.n.through, (unsigned long long)(g_rx.n.blocked - bl0),
	       (unsigned long long)(g_cb.n.hs_on - hs0), g_dh.atype, g_rx_pcs.raw[RX_C_NOBLOCK],
	       (unsigned long long)(g_cb.n.typed - ty0));
	/* =keep (build 7): the stale byte stays, the game hits through: a plain hit, hit-stop again */
	g_cb.atktype = 0;
	hs0 = g_cb.n.hs_on, bl0 = g_rx.n.blocked, ty0 = g_cb.n.typed;
	cb_cycle();
	EXPECT(g_rx.n.through == 2 && g_rx.n.blocked == bl0 && g_cb.n.hs_on == hs0 + 1 && log_has("the game hits through") &&
		       g_dh.atype == 5 && g_cb.n.typed == ty0,
	       "+0x55b kept: through %llu blocked %llu hitstops %llu", (unsigned long long)g_rx.n.through,
	       (unsigned long long)(g_rx.n.blocked - bl0), (unsigned long long)(g_cb.n.hs_on - hs0));
	g_cb.atktype = 1;
	g_rx_pcs.raw[RX_C_NOBLOCK] = 0, g_rx_block = 0;
	/* 3. the gates: A in a combat state (Combat+0x268): no roll */
	int r0 = g_rxc.roll;
	uint64_t nr0 = g_rx.n.noroll;
	*(uintptr_t *)((char *)&g_oc[0] + RX_C_STATE) = 1;
	cb_cycle();
	*(uintptr_t *)((char *)&g_oc[0] + RX_C_STATE) = 0;
	EXPECT(g_rxc.roll == r0 && g_rx.n.noroll == nr0 + 2 && log_has("the orc is in a combat state"), "combat state: rolls %d",
	       g_rxc.roll - r0);
	/* no entry matches the swing's category: no roll */
	g_rd_ent_v[1][1] = (uintptr_t)&g_rd_cat_other;
	cb_cycle();
	g_rd_ent_v[1][1] = (uintptr_t)&g_rd_cat_light;
	EXPECT(g_rxc.roll == r0 && g_rx.n.noroll == nr0 + 4 && log_has("categories 'ST_CAT_LIGHT' -> no roll (no entry matches)"),
	       "no match: rolls %d", g_rxc.roll - r0);
	/* Entries that are not records: 0.5 (0.49 wins, 0.51 loses) */
	g_rd_tbl_e[0].type = 2, g_rx_rolls[0] = 0.49f, g_rx_rolls[1] = 0.51f, g_rx_ri = 0;
	uint64_t w0 = g_rx.n.won;
	cb_cycle();
	g_rd_tbl_e[0].type = 0xa;
	EXPECT(g_rxc.roll == r0 + 2 && g_rx.n.won == w0 + 1 && log_has("chance 0.500 (entry -1) roll 0.490 -> notify"),
	       "not records: rolls %d won %llu", g_rxc.roll - r0, (unsigned long long)(g_rx.n.won - w0));
	/* no record from FUN_026ff330: no roll, no call */
	r0 = g_rxc.roll, nr0 = g_rx.n.noroll;
	g_rx_box[0x70 / 8] = (uintptr_t)g_rx_wm;
	cb_cycle();
	g_rx_box[0x70 / 8] = (uintptr_t)g_rx_hold;
	EXPECT(g_rxc.roll == r0 && g_rx.n.noroll == nr0 + 2 && log_has("no character combat record (FUN_026ff330 gave 0)"),
	       "no record: rolls %d noroll %llu", g_rxc.roll - r0, (unsigned long long)(g_rx.n.noroll - nr0));
	/* force: every roll with an entry wins, the game's roll is not called */
	int n0 = g_rxc.notify;
	g_rx.mode = RX_FORCE;
	cb_cycle();
	g_rx.mode = RX_ON;
	EXPECT(g_rxc.roll == r0 && g_rxc.notify == n0 + 2 && log_has("roll 0.000 (force) -> notify"), "force: rolls %d notifies %d",
	       g_rxc.roll - r0, g_rxc.notify - n0);
	/* 3b. no AIBlockTable (P9: AI_Defender, AI_Berserker): our own roll at the archetype's chance, only while the orc is
	 * out of its own attack; the game's roll function, the same call on a win, one per swing */
	static st_dbrec_t own_atk = { .flags = 2, .name = "ST_DEF_CONE" };
	uint8_t *cw = (uint8_t *)&g_oc[0] + RX_C_CANCEL;
	const char *name0 = g_rd_ccr.name;
	g_rd_ccr.n = 0, g_rd_ccr.name = "AI_Defender"; /* no attributes: no AIBlockTable */
	g_rx_rolls[0] = 0.3f, g_rx_rolls[1] = 0.8f, g_rx_nroll = 2, g_rx_ri = 0;
	r0 = g_rxc.roll, n0 = g_rxc.notify;
	uint64_t dr0 = g_rx.n.direct, bz0 = g_rx.n.busy;
	w0 = g_rx.n.won;
	cb_cycle();
	EXPECT(g_rxc.roll == r0 + 2 && g_rxc.notify == n0 + 1 && g_rx.n.direct == dr0 + 2 && g_rx.n.won == w0 + 1 &&
		       g_rxc.npc == (uintptr_t)&g_rx_npc && g_rxc.own == (uintptr_t)&g_rx_pown &&
		       log_has("REACT direct roll orc=") &&
		       log_has("record 'AI_Defender' (defender; its character combat record has no AIBlockTable), out of its own "
			       "attack ('' cancel=0/0) -> chance 0.600 (SOMVR_COMBAT_REACT_DEFENDER) roll 0.300 -> notify") &&
		       log_has("chance 0.600 (SOMVR_COMBAT_REACT_DEFENDER) roll 0.800 -> no notify"),
	       "direct defender: rolls %d notifies %d direct %llu", g_rxc.roll - r0, g_rxc.notify - n0,
	       (unsigned long long)(g_rx.n.direct - dr0));
	/* ... in its own attack (an attack record, cancel 0/0): no roll and no call, counted busy */
	g_oc[0].attack = (uintptr_t)&own_atk;
	r0 = g_rxc.roll, n0 = g_rxc.notify, nr0 = g_rx.n.noroll;
	cb_cycle();
	EXPECT(g_rxc.roll == r0 && g_rxc.notify == n0 && g_rx.n.busy == bz0 + 2 && g_rx.n.noroll == nr0 + 2 &&
		       log_has("its attack 'ST_DEF_CONE' cancel=0/0 -> no call (in its own attack: the counter goal ignores a "
			       "notify then)"),
	       "direct busy: rolls %d notifies %d busy %llu", g_rxc.roll - r0, g_rxc.notify - n0,
	       (unsigned long long)(g_rx.n.busy - bz0));
	/* ... also with force (the gate is not the roll's) */
	g_rx.mode = RX_FORCE;
	cb_cycle();
	g_rx.mode = RX_ON;
	EXPECT(g_rxc.roll == r0 && g_rxc.notify == n0 && g_rx.n.busy == bz0 + 4, "direct busy with force: notifies %d",
	       g_rxc.notify - n0);
	/* ... in an attack inside its cancel window (+0xe0 set): out of it for the goal, the roll again (0.1 wins twice) */
	cw[0] = 1, g_rx_rolls[0] = g_rx_rolls[1] = 0.1f;
	cb_cycle();
	EXPECT(g_rxc.roll == r0 + 2 && g_rxc.notify == n0 + 2 && log_has("out of its own attack ('ST_DEF_CONE' cancel=1/0)"),
	       "direct, cancel window: rolls %d notifies %d", g_rxc.roll - r0, g_rxc.notify - n0);
	cw[0] = 0, g_oc[0].attack = 0;
	/* ... a berserker: 0.4 (0.3 wins, 0.5 loses); force wins without the game's roll */
	g_rd_ccr.name = "AI_Berserker";
	g_rx_rolls[0] = 0.3f, g_rx_rolls[1] = 0.5f, g_rx_ri = 0;
	r0 = g_rxc.roll, n0 = g_rxc.notify;
	cb_cycle();
	EXPECT(g_rxc.roll == r0 + 2 && g_rxc.notify == n0 + 1 &&
		       log_has("record 'AI_Berserker' (berserker; its character combat record has no AIBlockTable)") &&
		       log_has("chance 0.400 (SOMVR_COMBAT_REACT_BERSERKER) roll 0.500 -> no notify"),
	       "direct berserker: rolls %d notifies %d", g_rxc.roll - r0, g_rxc.notify - n0);
	g_rx.mode = RX_FORCE;
	cb_cycle();
	g_rx.mode = RX_ON;
	EXPECT(g_rxc.roll == r0 + 2 && g_rxc.notify == n0 + 3 && log_has("chance 0.400 (SOMVR_COMBAT_REACT_BERSERKER) roll 0.000 "
										  "(force) -> notify"),
	       "direct berserker force: rolls %d notifies %d", g_rxc.roll - r0, g_rxc.notify - n0);
	/* ... another record (other, 0.5: 0.49 wins, 0.51 loses); its chance 0 = no call */
	g_rd_ccr.name = name0;
	g_rx_rolls[0] = 0.49f, g_rx_rolls[1] = 0.51f, g_rx_ri = 0;
	r0 = g_rxc.roll, n0 = g_rxc.notify;
	cb_cycle();
	EXPECT(g_rxc.roll == r0 + 2 && g_rxc.notify == n0 + 1 && log_has("record 'ST_CCR_A' (other;") &&
		       log_has("chance 0.500 (SOMVR_COMBAT_REACT_OTHER) roll 0.490 -> notify"),
	       "direct other: rolls %d notifies %d", g_rxc.roll - r0, g_rxc.notify - n0);
	g_rx.cfg.chance[SC_ARCH_OTHER] = 0;
	cb_cycle();
	g_rx.cfg.chance[SC_ARCH_OTHER] = SC_REACT_OTHER;
	EXPECT(g_rxc.roll == r0 + 2 && g_rxc.notify == n0 + 1 && log_has("-> no call (the archetype's chance is 0)"),
	       "direct other at 0: rolls %d", g_rxc.roll - r0);
	/* ... an AIBlockTable with no entries is the same path */
	g_rd_ccr.n = 2, g_rd_ccr.name = "AI_Defender", g_rd_tbl_e[0].count = 0;
	g_rx_rolls[0] = g_rx_rolls[1] = 0.1f;
	r0 = g_rxc.roll, n0 = g_rxc.notify, dr0 = g_rx.n.direct;
	cb_cycle();
	g_rd_tbl_e[0].count = 2, g_rd_ccr.name = name0;
	EXPECT(g_rxc.roll == r0 + 2 && g_rxc.notify == n0 + 2 && g_rx.n.direct == dr0 + 2 &&
		       log_has("(defender; its AIBlockTable has no entries)"),
	       "direct, empty table: rolls %d notifies %d", g_rxc.roll - r0, g_rxc.notify - n0);
	/* 4. log mode: the rolls are named, not one game call */
	int calls0 = g_rxc.charrec + g_rxc.roll + g_rxc.notify + g_rxc.blocking;
	uint64_t tw0 = g_rx.n.toward;
	g_cb.mode = CB_LOG;
	cb_cycle();
	g_cb.mode = CB_FULL;
	EXPECT(g_rxc.charrec + g_rxc.roll + g_rxc.notify + g_rxc.blocking == calls0 && g_rx.n.toward == tw0 + 2 &&
		       log_has("-> no roll (log only)"),
	       "log mode: %d game calls", g_rxc.charrec + g_rxc.roll + g_rxc.notify + g_rxc.blocking - calls0);
	/* 5. a REACT site mismatch at start (white box): no calls, the hits as before */
	g_rx.ok = 0;
	d0 = g_dh.n;
	cb_cycle();
	g_rx.ok = 1;
	EXPECT(g_rxc.charrec + g_rxc.roll + g_rxc.notify + g_rxc.blocking == calls0 && g_dh.n == d0 + 2 &&
		       log_has("a REACT site did not match at start"),
	       "sites off: %d game calls, %d hits", g_rxc.charrec + g_rxc.roll + g_rxc.notify + g_rxc.blocking - calls0, g_dh.n - d0);
	/* 6. reactions off: nothing of it runs, the hits as before */
	struct rx_cnt c0 = g_rx.n;
	g_rx.mode = RX_OFF, g_rx_block = 1;
	d0 = g_dh.n, hs0 = g_cb.n.hs_on;
	cb_cycle();
	g_rx_block = 0;
	EXPECT(!memcmp(&c0, &g_rx.n, sizeof c0) && g_rxc.charrec + g_rxc.roll + g_rxc.notify + g_rxc.blocking == calls0 &&
		       g_dh.n == d0 + 2 && g_cb.n.hs_on == hs0 + 1,
	       "react off: counters moved or %d game calls; hits %d hitstops %llu",
	       g_rxc.charrec + g_rxc.roll + g_rxc.notify + g_rxc.blocking - calls0, g_dh.n - d0,
	       (unsigned long long)(g_cb.n.hs_on - hs0));
	EXPECT(log_has("REACT config mode=1 (SOMVR_COMBAT=1)") && log_has("REACT site RX_CHARREC ok") &&
		       log_has("REACT site RX_ROLL ok") && log_has("REACT site RX_NOTIFY ok") && log_has("REACT site RX_BLOCKING ok") &&
		       log_has("REACT site RX_ATK_GATE ok") &&
		       log_has("no AIBlockTable: our roll at defender 0.60 berserker 0.40 other 0.50, only out of the orc's own "
			       "attack") &&
		       log_has("REACT t=") && log_has("(direct "),
	       "REACT start/summary lines missing");
	char why[512];
	struct site_spec bad = g_rx_sites[RS_NOTIFY];
	bad.ctx_hex = "00 00 00 00 00 00 00 00";
	EXPECT(check_site(&bad, why, sizeof why) != 0, "a REACT K_ENTRY with wrong bytes passed");
	printf("react: one roll per swing on the orc ahead (its table through FUN_026ff330, unsorted and sorted records, "
	       "ST_L1's category -> entry 1, 0.6), 0.3 notifies, 0.8 not; blocked hits still through Combat_DealHit without "
	       "hit-stop, +0x55b hits through; combat state, no match, not records (0.5), no record, force; no table: "
	       "defender 0.6 / berserker 0.4 / other 0.5 (0 = never), in its own attack no call (also with force), its cancel "
	       "window open = a call, an empty table; log mode, sites off, off; %llu rolls (%llu direct, %llu busy) %llu "
	       "notifies %llu blocked\n", (unsigned long long)g_rx.n.rolls, (unsigned long long)g_rx.n.direct,
	       (unsigned long long)g_rx.n.busy, (unsigned long long)g_rx.n.notifies, (unsigned long long)g_rx.n.blocked);
	rx_unworld();
	g_cb.mode = CB_OFF;
	g_pl = NULL;
	g_cb_test_t = -1;
}

/* ---- SOMVR_INPUT: stand-ins for CWBInput (its vtable slots 4/22/23/25), PlayerInput and its command records, the local
 * player entry and its profile, CGameClientShell with the interface/menu state machines, the game timer (pause), the
 * ON/OFF events, the active set, the input mode, the video test, FUN_00aaa450's two callers (the rumble), and the layer
 * (the snapshot, the grip spaces, xrLocateSpace) ------------------------------------------------------------------- */
static void *volatile g_vtin[32]; /* CWBInput's vtable: 4 the device update, 22/23 values, 25 is-on */
static struct { uintptr_t vt; } g_cwbi;
static int g_vf04_calls;
static uintptr_t g_dev_on_rec; /* the record the stand-in device reports on (vf_25) */
__attribute__((noipa)) void t_vf04(void *self) { (void)self; g_vf04_calls++; }
__attribute__((noipa)) float t_vf22(void *self, void *rec) { (void)self, (void)rec; return 0.77f; }
__attribute__((noipa)) float t_vf23(void *self, void *rec) { (void)self, (void)rec; return 0.66f; }
__attribute__((noipa)) uint64_t t_vf25(void *self, void *rec, uint64_t a3) { (void)self, (void)a3; return (uintptr_t)rec == g_dev_on_rec; }
typedef void (*t_vf04_fn)(void *);
typedef float (*t_vf22_fn)(void *, void *);
typedef uint64_t (*t_vf25_fn)(void *, void *, uint64_t);
static struct { char k; int cmd, input; uintptr_t pi; } g_inev[512];
static int g_ninev;
__attribute__((noipa)) void st_cmd_on(uintptr_t pi, int cmd, int input)
{
	if (g_ninev < 512)
		g_inev[g_ninev].k = 'N', g_inev[g_ninev].cmd = cmd, g_inev[g_ninev].input = input, g_inev[g_ninev++].pi = pi;
}
__attribute__((noipa)) void st_cmd_off(uintptr_t pi, int cmd, int input)
{
	if (g_ninev < 512)
		g_inev[g_ninev].k = 'F', g_inev[g_ninev].cmd = cmd, g_inev[g_ninev].input = input, g_inev[g_ninev++].pi = pi;
}
static unsigned char g_act_world[349], g_act_menu[349];
static int g_menu_set, g_video, g_inmode_calls, g_inmode_last, g_rum_calls, g_rum_after;
static char g_profile[0x240];
__attribute__((noipa)) uint64_t st_cmd_active(uintptr_t pi, unsigned cmd)
{
	(void)pi;
	return cmd < 349 && (g_menu_set ? g_act_menu : g_act_world)[cmd];
}
__attribute__((noipa)) void st_inmode(uintptr_t im, uintptr_t entry, int pad)
{
	(void)im, (void)entry;
	g_inmode_calls++, g_inmode_last = pad;
	g_profile[0x238] = (char)pad;
}
__attribute__((noipa)) uint64_t st_video(uintptr_t im) { (void)im; return (uint64_t)g_video; }
__attribute__((noipa)) void t_rumble(void *pi, int ch, float v) { (void)pi, (void)ch, (void)v; g_rum_calls++; }
__attribute__((noipa)) void c_rum1(void *pi, int ch, float v) { t_rumble(pi, ch, v); g_rum_after += 1; }
__attribute__((noipa)) void c_rum2(void *pi, int ch, float v) { t_rumble(pi, ch, v); g_rum_after += 2; }
typedef void (*t_rum_fn)(void *, int, float);

static char g_shell[0x81010], g_im[0x2b00], g_timerobj[0x40], g_pz[0x40], g_entry[0x100], g_entry2[0x100];
static char g_pie[360 * 0x30], g_irec[40][0x40], g_ictx[0x20];
static float g_ivalrec[96][5];
static int g_nirec, g_nival;
static void *g_shell_ptr = g_shell, *g_locals[1];
static uintptr_t g_pause_pp_val;
static unsigned char g_allow_kbm = 1;
typedef struct { char pad[8]; int32_t s8, sc; } st_sm_t;
static st_sm_t g_sm_ifs = { { 0 }, 3, 3 }, g_sm_menus = { { 0 }, 0, 0 };
static uintptr_t g_rec_of[349];
/* a command record with up to two bindings (input id, scale) in the stand-in PlayerInput's entry vector */
static uintptr_t bind_cmd(int cmd, int i0, float s0, int i1, float s1)
{
	char *r = g_irec[g_nirec++];
	memset(r, 0, 0x40);
	uintptr_t ctx = (uintptr_t)g_ictx, rp = (uintptr_t)r;
	int32_t in4[4] = { i0, i1, -1, -1 }, vi[4] = { g_nival, g_nival + 1, -1, -1 };
	g_ivalrec[g_nival++][0] = s0, g_ivalrec[g_nival++][0] = s1;
	memcpy(r + IN_REC_CTX, &ctx, 8), memcpy(r + IN_REC_INPUTS, in4, 16), memcpy(r + IN_REC_VALIDX, vi, 16);
	memcpy(g_pie + (size_t)cmd * 0x30, &rp, 8);
	return g_rec_of[cmd] = rp;
}
static void set_entry(char *en) /* the local player's entry: +8 the profile, +0x10 PlayerInput (+0x60 the entries) */
{
	uintptr_t prof = (uintptr_t)g_profile, b = (uintptr_t)g_pie, e = b + sizeof g_pie;
	memset(en, 0, 0x100);
	memcpy(en + IN_EN_PROFILE, &prof, 8);
	memcpy(en + IN_EN_PI + IN_PI_ENTRIES, &b, 8), memcpy(en + IN_EN_PI + IN_PI_ENTRIES + 8, &e, 8);
	en[IN_EN_PI + 0x78] = 1; /* enabled */
	g_locals[0] = en;
}
static void input_world(void)
{
	uintptr_t v;
	v = (uintptr_t)g_im, memcpy(g_shell + IN_SH_IM, &v, 8);
	v = (uintptr_t)&g_sm_ifs, memcpy(g_im, &v, 8);
	v = (uintptr_t)&g_sm_menus, memcpy(g_im + 0x10, &v, 8);
	int32_t cur = 0;
	memcpy(g_shell + IN_SH_LOCALS, &cur, 4);
	v = (uintptr_t)&g_locals[0], memcpy(g_shell + IN_SH_LOCALS + 8, &v, 8);
	v = (uintptr_t)&g_locals[1], memcpy(g_shell + IN_SH_LOCALS + 0x10, &v, 8);
	g_pause_pp_val = (uintptr_t)g_timerobj;
	v = (uintptr_t)g_pz, memcpy(g_timerobj + 8, &v, 8);
	v = (uintptr_t)g_ivalrec, memcpy(g_ictx + IN_CTX_VALRECS, &v, 8);
	set_entry(g_entry);
	g_ing.shell_pp = (uintptr_t)&g_shell_ptr, g_ing.pause_pp = (uintptr_t)&g_pause_pp_val;
	g_ing.allow_kbm = (uintptr_t)&g_allow_kbm;
	/* run 3's pad bindings (runtime-probes.md P1), plus a few menu commands; the world and the menu command sets */
	bind_cmd(SC_CMD_FORWARDAXIS, 2017, 1, 'W', 1), bind_cmd(SC_CMD_STRAFEAXIS, 2016, 1, 'D', 1);
	bind_cmd(SC_CMD_JUMP, 2010, 1, 0x20, 1), bind_cmd(SC_CMD_FREERUN, 2010, 1, -1, 1), bind_cmd(SC_CMD_EVADE, 2010, 1, 0x20, 1);
	bind_cmd(SC_CMD_STRIKE, 2012, 1, 1000, 1), bind_cmd(SC_CMD_COUNTER, 2013, 1, -1, 1), bind_cmd(SC_CMD_STUN, 2011, 1, 'E', 1);
	bind_cmd(SC_CMD_ACTIVATE, 2009, 1, 0xa2, 1), bind_cmd(SC_CMD_CROUCH, 2009, 1, -1, 1), bind_cmd(SC_CMD_MENU, 2004, 1, 0x1b, 1);
	bind_cmd(SC_CMD_PDA, 2005, 1, 'M', 1), bind_cmd(SC_CMD_PITCHACCEL, 2019, -1, 1006, 0.005f);
	bind_cmd(SC_CMD_YAWACCEL, 2018, 1, 1005, 0.005f), bind_cmd(132, 2016, 1, -1, 1), bind_cmd(161, 2012, 1, -1, 1);
	bind_cmd(54, 2010, 1, 0x0d, 1), bind_cmd(55, 2011, 1, 0x1b, 1), bind_cmd(44, 2000, 1, -1, 1), bind_cmd(45, 2001, 1, -1, 1);
	bind_cmd(52, 2017, -1, -1, 1);
	static const int world[] = { 3, 6, 14, 15, 16, 18, 22, 23, 34, 95, 118, 119, 120, 121, 132, 161 },
			 menu[] = { 14, 44, 45, 52, 54, 55, 95 };
	for (size_t i = 0; i < sizeof world / sizeof world[0]; i++)
		g_act_world[world[i]] = 1;
	for (size_t i = 0; i < sizeof menu / sizeof menu[0]; i++)
		g_act_menu[menu[i]] = 1;
	g_profile[0x238] = 0; /* KBM mode at start */
}

/* the layer: the snapshot it publishes after each xrWaitFrame, the grip spaces and xrLocateSpace it publishes */
static somvr_input_state g_lst;
static int g_layer_live;
static XrPosef g_lpose[2];
static XrVector3f g_lvel[2], g_lang[2];
static XrTime g_ls_time;
static int g_ls_calls, g_ls_bad, g_ls_fail, g_ls_lost[2]; /* g_ls_lost: that hand's pose not valid (no position) */
static XrResult XRAPI_CALL st_lspace(XrSpace sp, XrSpace base, XrTime t, XrSpaceLocation *loc)
{
	uintptr_t h = (uintptr_t)sp - 0x7100u;
	g_ls_calls++;
	if (h > 1 || base != (XrSpace)(uintptr_t)0x3333 || loc->type != XR_TYPE_SPACE_LOCATION || !loc->next)
		return g_ls_bad++, XR_ERROR_VALIDATION_FAILURE;
	g_ls_time = t;
	if (g_ls_fail)
		return XR_ERROR_RUNTIME_FAILURE;
	XrSpaceVelocity *v = (XrSpaceVelocity *)loc->next;
	loc->locationFlags = g_ls_lost[h] ? 0 : 0xf, loc->pose = g_lpose[h];
	v->velocityFlags = 3, v->linearVelocity = g_lvel[h], v->angularVelocity = g_lang[h];
	return XR_SUCCESS;
}
static XrTime g_xrt = 9000000000;
static void layer_up(void)
{
	for (int h = 0; h < 2; h++) {
		g_in.xr.grip_space[h] = 0x7100u + (uint64_t)h;
		g_lst.hand[h].active = SOMVR_HACT_POSE | SOMVR_HACT_INPUT;
		g_lst.hand[h].grip_flags = 0xf, g_lst.hand[h].vel_flags = 3;
		g_lpose[h] = (XrPosef){ { 0, 0, 0, 1 }, { h ? 0.3f : -0.3f, -0.3f, -0.4f } };
		memcpy(&g_lst.hand[h].grip, &g_lpose[h], sizeof g_lst.hand[h].grip);
	}
	g_in.xr.locate_space = (void *)st_lspace;
	atomic_store(&g_in.xr.ready, 1);
	g_lst.flags = SOMVR_INS_FOCUSED | SOMVR_INS_SYNCED | SOMVR_INS_LOCATED;
	g_lst.profile[0] = g_lst.profile[1] = SOMVR_PROF_TOUCH;
	g_layer_live = 1;
}
/* one game frame with the input tick: the layer's wait (the views' time) and sync, then I0, the player update, H4, H9 */
static void iframe(void)
{
	g_cb_test_t += 1.0 / 90;
	g_xrt += 11111111;
	somvr_time_publish(&g_sh, g_xrt, 11111111);
	if (g_layer_live) {
		g_lst.sync_seq++, g_lst.time = g_xrt;
		for (int h = 0; h < 2; h++)
			memcpy(&g_lst.hand[h].grip, &g_lpose[h], sizeof g_lst.hand[h].grip);
		somvr_input_publish(&g_in, &g_lst);
	}
	((t_vf04_fn)g_vtin[4])(&g_cwbi);
	frame();
}
static void ipress(int h, uint32_t bit)
{
	if (!(g_lst.hand[h].buttons & bit))
		g_lst.hand[h].presses[__builtin_ctz(bit)]++;
	g_lst.hand[h].buttons |= bit;
}
static void irelease(int h, uint32_t bit) { g_lst.hand[h].buttons &= ~bit; }
static float poll22(int cmd) { return ((t_vf22_fn)g_vtin[22])(&g_cwbi, (void *)g_rec_of[cmd]); }
static float poll23(int cmd) { return ((t_vf22_fn)g_vtin[23])(&g_cwbi, (void *)g_rec_of[cmd]); }
static int poll25(int cmd) { return (int)(((t_vf25_fn)g_vtin[25])(&g_cwbi, (void *)g_rec_of[cmd], 0) & 1); }
static int nev(char k, int cmd, int from) /* events of kind k ('N' on, 'F' off) for cmd since event index `from` */
{
	int n = 0;
	for (int i = from; i < g_ninev; i++)
		n += g_inev[i].k == k && g_inev[i].cmd == cmd;
	return n;
}
static int ev_input(char k, int cmd, int from)
{
	for (int i = from; i < g_ninev; i++)
		if (g_inev[i].k == k && g_inev[i].cmd == cmd)
			return g_inev[i].input;
	return -2;
}
static void *poll_thread(void *a)
{
	*(float *)a = poll22(SC_CMD_FORWARDAXIS);
	return NULL;
}
static int hap_take(int h, somvr_haptic_req *q) /* the layer side: a new request since the last take */
{
	static uint32_t last[2];
	return somvr_haptic_take(&g_in, h, &last[h], q);
}

static void input_tests(void)
{
	g_views_fake = 0, g_rt_result = XR_SUCCESS, g_layout = SOMVR_LAYOUT_A, g_rigcfg.layout = SOMVR_LAYOUT_A;
	g_rigcfg.base = SOMVR_BASE_CAMERA, g_rigcfg.yaw = SOMVR_YAW_CAMERA;
	g_rt_head[0] = g_rt_head[1] = g_rt_head[2] = 0, g_rt_q = (sv_quat){ 0, 0, 0, 1 };
	runtime_up();
	g_cb_test_t = 5000; /* the clocks of the tests before ran on mono(): restart the input's line limits on this one */
	g_inp.t0 = g_cb_test_t, g_inp.next_log = g_cb_test_t + 1, g_inp.last_line = g_cb_test_t;
	g_inp.lines = 0, g_inp.lsec = 0, g_inp.hand_next = 0;
	g_xrt = g_vst.last_time + 1000000000; /* the layer's next waits: after every views time of the tests before */
	g_pl = &g_player;
	/* S1: the second exported object, active with SOMVR_INPUT=1 */
	EXPECT(dlsym(RTLD_DEFAULT, SOMVR_INPUT_SYMBOL) == (void *)&g_in && somvr_input_ok(&g_in) &&
		       g_in.want == (SOMVR_INW_STATE | SOMVR_INW_HAPTICS | SOMVR_INW_SPACES | SOMVR_INW_POINTER) &&
		       g_in.size == SOMVR_INPUT_V2_SIZE && g_in.version == 2, /* SOMVR_POINTER=1 (ptr_tests) */
	       "somvr_input: found %d ok %d want %#x", dlsym(RTLD_DEFAULT, SOMVR_INPUT_SYMBOL) == (void *)&g_in,
	       somvr_input_ok(&g_in), g_in.want);
	EXPECT(g_inp.on && g_inp.inject && g_inp.ok_mode && g_inp.ok_video && log_has("INPUT config input=1 inject=1") &&
		       log_has("INPUT site IN_CMD_ON ok") && log_has("HOOK IN_TICK ok vslot") && log_has("HOOK RUM1 ok call"),
	       "input start: on %d inject %d", g_inp.on, g_inp.inject);
	/* a v2 layer (no snapshot): ticks run, nothing injected, the stand-in's values; the warning after 5 s */
	int e0 = g_ninev;
	for (int i = 0; i < 20; i++)
		iframe();
	EXPECT(g_vf04_calls >= 20 && g_ninev == e0 && poll22(SC_CMD_FORWARDAXIS) == 0.77f && poll25(SC_CMD_JUMP) == 0 && !g_inp.niv,
	       "v2 layer: events %d held %d value %g", g_ninev - e0, g_inp.niv, poll22(SC_CMD_FORWARDAXIS));
	/* pad mode: forced at the first tick with the entry (the profile byte 0 -> 1, allow->kbm 0), again when it reads 0 */
	EXPECT(g_inmode_calls == 1 && g_inmode_last == 1 && g_profile[0x238] == 1 && g_allow_kbm == 0 &&
		       log_has("PADMODE on: profile byte 0 -> 1, allow->kbm 1 -> 0"),
	       "pad mode: calls %d byte %d allow %u", g_inmode_calls, g_profile[0x238], g_allow_kbm);
	g_profile[0x238] = 0;
	for (int i = 0; i < 100; i++)
		iframe();
	EXPECT(g_inmode_calls == 2 && g_profile[0x238] == 1, "pad mode again after the byte read 0: %d calls", g_inmode_calls);
	g_cb_test_t += 5;
	iframe();
	EXPECT(log_has("INPUT no controller state from the layer after 5 s"), "no v2 warning");
	/* the layer comes: live; the head straight, the left stick forward = ForwardAxis 1 (our value), StrafeAxis untouched */
	layer_up();
	atomic_fetch_add(&g_req_recenter, 1);
	iframe();
	EXPECT(g_inp.live && log_has("INPUT live t="), "not live");
	g_lst.hand[0].stick[1] = 1;
	iframe();
	EXPECT(poll22(SC_CMD_FORWARDAXIS) == 1.0f && poll22(SC_CMD_STRAFEAXIS) == 0.77f && poll25(SC_CMD_FORWARDAXIS) == 1 &&
		       poll23(SC_CMD_FORWARDAXIS) == 1.0f && nev('N', SC_CMD_FORWARDAXIS, 0) == 0,
	       "forward: Forward %g Strafe %g", poll22(SC_CMD_FORWARDAXIS), poll22(SC_CMD_STRAFEAXIS));
	EXPECT(poll22(132) == 0.77f, "CombatPrimaryAnalogX (LX) moved with a forward stick");
	/* the head turned 90 deg right of the game camera: forward is strafe right (the next tick, from this frame's H4) */
	g_rt_q = (sv_quat){ 0, -sinf(0.7853982f), 0, cosf(0.7853982f) };
	iframe();
	iframe();
	EXPECT(NEAR(poll22(SC_CMD_STRAFEAXIS), 1, 1e-5) && poll22(SC_CMD_FORWARDAXIS) == 0.77f && NEAR(poll22(132), 1, 1e-5),
	       "head 90 right: Strafe %g Forward %g (want 1, the stand-in's)", poll22(SC_CMD_STRAFEAXIS), poll22(SC_CMD_FORWARDAXIS));
	EXPECT(log_has("MOVE stick=(0.00,1.00) d=90.0 -> StrafeAxis 1.00 ForwardAxis 0.00"), "no MOVE line for the turned head");
	g_rt_q = (sv_quat){ 0, 0, 0, 1 };
	g_lst.hand[0].stick[1] = 0.1f; /* inside the dead zone: nothing held */
	iframe();
	EXPECT(poll22(SC_CMD_FORWARDAXIS) == 0.77f && poll22(SC_CMD_STRAFEAXIS) == 0.77f, "the dead zone moved");
	g_lst.hand[0].stick[1] = 0;
	/* the deny list in the world: the stock strike/counter/stun and the camera axes read 0/off, whatever the device says */
	g_dev_on_rec = g_rec_of[SC_CMD_STRIKE];
	iframe();
	EXPECT(poll22(SC_CMD_STRIKE) == 0 && poll25(SC_CMD_STRIKE) == 0 && poll22(SC_CMD_COUNTER) == 0 && poll22(SC_CMD_STUN) == 0 &&
		       poll22(SC_CMD_YAWACCEL) == 0 && poll23(SC_CMD_PITCHACCEL) == 0,
	       "deny: strike %g/%d counter %g yaw %g", poll22(SC_CMD_STRIKE), poll25(SC_CMD_STRIKE), poll22(SC_CMD_COUNTER),
	       poll22(SC_CMD_YAWACCEL));
	/* A: the stock A (Jump, FreeRun, CombatPrimary04 with the pad A input), never MenuEnter (inactive in the world) */
	e0 = g_ninev;
	ipress(1, SOMVR_BTN_LOWER);
	iframe();
	iframe();
	EXPECT(nev('N', SC_CMD_JUMP, e0) == 1 && nev('N', SC_CMD_FREERUN, e0) == 1 && nev('N', SC_CMD_EVADE, e0) == 1 &&
		       nev('N', 54, e0) == 0 && ev_input('N', SC_CMD_JUMP, e0) == 2010 && poll25(SC_CMD_JUMP) == 1 &&
		       poll22(SC_CMD_FREERUN) == 1.0f && g_inev[e0].pi == (uintptr_t)g_entry + IN_EN_PI,
	       "A: on Jump %d FreeRun %d Evade %d MenuEnter %d input %d", nev('N', SC_CMD_JUMP, e0), nev('N', SC_CMD_FREERUN, e0),
	       nev('N', SC_CMD_EVADE, e0), nev('N', 54, e0), ev_input('N', SC_CMD_JUMP, e0));
	irelease(1, SOMVR_BTN_LOWER);
	iframe();
	EXPECT(nev('F', SC_CMD_JUMP, e0) == 1 && nev('F', SC_CMD_FREERUN, e0) == 1 && nev('F', SC_CMD_EVADE, e0) == 1 &&
		       poll25(SC_CMD_JUMP) == 0 && poll22(SC_CMD_JUMP) == 0.77f,
	       "A released: off %d %d %d", nev('F', SC_CMD_JUMP, e0), nev('F', SC_CMD_FREERUN, e0), nev('F', SC_CMD_EVADE, e0));
	/* a tap shorter than a sync: ON at this tick, OFF at the next */
	e0 = g_ninev;
	g_lst.hand[1].presses[0]++;
	iframe();
	int on1 = nev('N', SC_CMD_JUMP, e0), off1 = nev('F', SC_CMD_JUMP, e0);
	iframe();
	EXPECT(on1 == 1 && off1 == 0 && nev('F', SC_CMD_JUMP, e0) == 1, "tap: on %d off %d then %d", on1, off1, nev('F', SC_CMD_JUMP, e0));
	/* B = Activate (its own first input, RB), never Crouch (also RB) nor the stock stun (pad B) */
	e0 = g_ninev;
	ipress(1, SOMVR_BTN_UPPER);
	iframe();
	irelease(1, SOMVR_BTN_UPPER);
	iframe();
	EXPECT(nev('N', SC_CMD_ACTIVATE, e0) == 1 && nev('F', SC_CMD_ACTIVATE, e0) == 1 && ev_input('N', SC_CMD_ACTIVATE, e0) == 2009 &&
		       !nev('N', SC_CMD_CROUCH, e0) && !nev('N', SC_CMD_STUN, e0),
	       "B: Activate %d/%d input %d crouch %d stun %d", nev('N', SC_CMD_ACTIVATE, e0), nev('F', SC_CMD_ACTIVATE, e0),
	       ev_input('N', SC_CMD_ACTIVATE, e0), nev('N', SC_CMD_CROUCH, e0), nev('N', SC_CMD_STUN, e0));
	/* menu = Menu, right stick click = PDA, right stick down = one Crouch pulse; X (pad X = the stock strike): nothing */
	e0 = g_ninev;
	ipress(0, SOMVR_BTN_MENU), ipress(1, SOMVR_BTN_STICK), ipress(0, SOMVR_BTN_LOWER);
	g_lst.hand[1].stick[1] = -0.9f;
	iframe();
	irelease(0, SOMVR_BTN_MENU), irelease(1, SOMVR_BTN_STICK);
	iframe();
	g_lst.hand[1].stick[1] = 0;
	irelease(0, SOMVR_BTN_LOWER);
	iframe();
	EXPECT(nev('N', SC_CMD_MENU, e0) == 1 && nev('F', SC_CMD_MENU, e0) == 1 && nev('N', SC_CMD_PDA, e0) == 1 &&
		       nev('N', SC_CMD_CROUCH, e0) == 1 && nev('F', SC_CMD_CROUCH, e0) == 1 && nev('N', SC_CMD_STRIKE, e0) == 0 &&
		       nev('N', 161, e0) == 0,
	       "menu %d PDA %d crouch %d/%d strike %d", nev('N', SC_CMD_MENU, e0), nev('N', SC_CMD_PDA, e0),
	       nev('N', SC_CMD_CROUCH, e0), nev('F', SC_CMD_CROUCH, e0), nev('N', SC_CMD_STRIKE, e0));
	/* a struggle: pad X presses CombatStruggle01 (bound to X), still never CombatPrimary01 */
	e0 = g_ninev;
	uintptr_t one = 1;
	memcpy((char *)&g_pcomb + IN_PC_STRUGGLE, &one, 8);
	iframe();
	ipress(0, SOMVR_BTN_LOWER);
	iframe();
	irelease(0, SOMVR_BTN_LOWER);
	iframe();
	memset((char *)&g_pcomb + IN_PC_STRUGGLE, 0, 8);
	EXPECT(nev('N', 161, e0) == 1 && nev('F', 161, e0) == 1 && nev('N', SC_CMD_STRIKE, e0) == 0 && log_has("ctx=struggle"),
	       "struggle: CombatStruggle01 %d/%d strike %d", nev('N', 161, e0), nev('F', 161, e0), nev('N', SC_CMD_STRIKE, e0));
	/* the snap turn about the head: the head 50 cm right of P0 keeps its world position; one step per push */
	g_rt_head[0] = 0.5f;
	iframe();
	sv_vec3 m0 = eye_mid();
	int32_t s0 = g_rig.snap_steps;
	g_lst.hand[1].stick[0] = 0.9f;
	for (int i = 0; i < 30; i++)
		iframe();
	sv_vec3 m1 = eye_mid();
	sv_vec3 f = sv_qrot(sv_quat_of(g_h4_view[0].quat), (sv_vec3){ 0, 0, 1 });
	EXPECT(g_rig.snap_steps == s0 + 1 && NEAR3(m1, m0.x, m0.y, m0.z, 1e-2) && NEAR(atan2f(f.x, f.z), g_rigcfg.snap_rad, 1e-4) &&
		       log_has("TURN snap +1") && log_has("RIG t="),
	       "snap: steps %d->%d head %g %g %g -> %g %g %g heading %g", s0, g_rig.snap_steps, m0.x, m0.y, m0.z, m1.x, m1.y, m1.z,
	       atan2f(f.x, f.z));
	g_lst.hand[1].stick[0] = 0;
	iframe();
	/* smooth turn: 120 deg/s, the head still in place */
	g_inp.cfg.smooth = 1;
	m0 = eye_mid();
	float y0 = g_rig.yaw0;
	g_lst.hand[1].stick[0] = 1;
	for (int i = 0; i < 45; i++)
		iframe();
	g_lst.hand[1].stick[0] = 0;
	iframe();
	m1 = eye_mid();
	EXPECT(NEAR(y0 - g_rig.yaw0, 0.5235988 * 2 - 0.5235988 * 2 * 0.1, 0.03) && NEAR3(m1, m0.x, m0.y, m0.z, 1e-2),
	       "smooth: yaw0 %g -> %g (%g deg) head %g -> %g", y0, g_rig.yaw0, (y0 - g_rig.yaw0) * 57.29578, m0.x, m1.x);
	g_inp.cfg.smooth = 0;
	g_rt_head[0] = 0;
	atomic_fetch_add(&g_req_recenter, 1);
	iframe();
	/* menus: an Xbox pad; A = MenuEnter (pad A), B = MenuEscape, a flick down = MenuDown, the left stick = MenuAxisY */
	e0 = g_ninev;
	g_sm_menus.sc = 5, g_menu_set = 1;
	iframe();
	ipress(1, SOMVR_BTN_LOWER);
	iframe();
	irelease(1, SOMVR_BTN_LOWER), ipress(1, SOMVR_BTN_UPPER);
	iframe();
	irelease(1, SOMVR_BTN_UPPER);
	g_lst.hand[0].stick[1] = -0.9f;
	iframe();
	float may = poll22(52);
	iframe();
	g_lst.hand[0].stick[1] = 0;
	iframe();
	EXPECT(nev('N', 54, e0) == 1 && nev('F', 54, e0) == 1 && ev_input('N', 54, e0) == 2010 && nev('N', 55, e0) == 1 &&
		       nev('N', SC_CMD_JUMP, e0) == 0 && nev('N', SC_CMD_STUN, e0) == 0 && nev('N', 45, e0) == 1 &&
		       NEAR(may, (0.9 - 0.15) / 0.85, 1e-4) && poll22(SC_CMD_STRIKE) == 0.77f,
	       "menu: MenuEnter %d/%d input %d MenuEscape %d Jump %d MenuDown %d MenuAxisY %g strike %g", nev('N', 54, e0),
	       nev('F', 54, e0), ev_input('N', 54, e0), nev('N', 55, e0), nev('N', SC_CMD_JUMP, e0), nev('N', 45, e0), may,
	       poll22(SC_CMD_STRIKE));
	g_sm_menus.sc = 0, g_menu_set = 0;
	iframe();
	/* input lost while A is held: OFF for everything, the stand-in's values back; back with A held: nothing until seen up */
	ipress(1, SOMVR_BTN_LOWER);
	iframe();
	e0 = g_ninev;
	g_lst.flags = SOMVR_INS_SYNCED; /* the Quest menu: not focused */
	iframe();
	EXPECT(nev('F', SC_CMD_JUMP, e0) == 1 && nev('F', SC_CMD_EVADE, e0) == 1 && poll25(SC_CMD_JUMP) == 0 &&
		       poll22(SC_CMD_STRIKE) == 0.77f && !g_inp.niv && log_has("INPUT lost t=") && log_has("why=input-lost"),
	       "loss: off %d held %d", nev('F', SC_CMD_JUMP, e0), g_inp.niv);
	g_lst.flags = SOMVR_INS_FOCUSED | SOMVR_INS_SYNCED | SOMVR_INS_LOCATED;
	e0 = g_ninev;
	iframe();
	iframe();
	int held_on = nev('N', SC_CMD_JUMP, e0);
	irelease(1, SOMVR_BTN_LOWER);
	iframe();
	ipress(1, SOMVR_BTN_LOWER);
	iframe();
	irelease(1, SOMVR_BTN_LOWER);
	iframe();
	EXPECT(held_on == 0 && nev('N', SC_CMD_JUMP, e0) == 1, "after the loss: held %d then %d", held_on, nev('N', SC_CMD_JUMP, e0));
	/* the layer stalls (no sync for 4 ticks and 50 ms): lost */
	g_lst.hand[0].stick[1] = 1;
	iframe();
	g_layer_live = 0;
	for (int i = 0; i < 6; i++)
		iframe();
	EXPECT(!g_inp.live && g_inp.why == SC_LIVE_STALLED && poll22(SC_CMD_FORWARDAXIS) == 0.77f, "stall: live %d why %d",
	       g_inp.live, g_inp.why);
	g_layer_live = 1;
	iframe();
	iframe();
	EXPECT(g_inp.live && NEAR(poll22(SC_CMD_FORWARDAXIS), cos(0.5235988), 1e-5) && NEAR(poll22(SC_CMD_STRAFEAXIS), 0.5, 1e-5),
	       "back after the stall: live %d forward %g (the rig turned 30 deg by the snap test: cos 30)", g_inp.live,
	       poll22(SC_CMD_FORWARDAXIS));
	/* another thread's poll passes through; another local player: everything dropped, no OFF to the old PlayerInput */
	float other = 0;
	pthread_t th;
	pthread_create(&th, NULL, poll_thread, &other);
	pthread_join(th, NULL);
	EXPECT(other == 0.77f && atomic_load(&g_in_other_polls) >= 1, "another thread got our value %g", other);
	ipress(1, SOMVR_BTN_LOWER);
	iframe();
	e0 = g_ninev;
	set_entry(g_entry2);
	iframe();
	EXPECT(nev('F', SC_CMD_JUMP, e0) == 0 && g_inp.pi == (uintptr_t)g_entry2 + IN_EN_PI && log_has("INPUT RESET"),
	       "player change: %d OFF events to the old one", nev('F', SC_CMD_JUMP, e0));
	irelease(1, SOMVR_BTN_LOWER);
	g_lst.hand[0].stick[1] = 0;
	iframe();
	iframe();
	set_entry(g_entry);
	iframe();
	/* the hands: located at the views' time in the eye records' space; 30 cm right, 30 below, 40 ahead of the head */
	int c0 = g_ls_calls;
	g_inp.hand_next = 0; /* this frame's HAND line */
	iframe();
	EXPECT(g_ls_calls == c0 + 2 && g_ls_bad == 0 && g_ls_time == g_rt_time && g_inp.hands[1].src == SOMVR_HSRC_LOCATED &&
		       atomic_load(&g_in.n_hand_locates) >= 2,
	       "hands: calls %d bad %d time %lld views %lld", g_ls_calls - c0, g_ls_bad, (long long)g_ls_time, (long long)g_rt_time);
	/* (the rig still has the snap test's +30 deg: the hands turn with it, as the eyes do) */
	EXPECT(log_has("R-head=+30.0 right -30.0 up +40.0 ahead d_snap=0.00 yaw L=30.0 R=30.0 head=30.0 deg") &&
		       log_has("world L=94.0,170.0,349.6 R=146.0,170.0,319.6"),
	       "no HAND line with the right hand 30/-30/40 from the head");
	g_ls_fail = 1; /* the locate fails: the snapshot */
	iframe();
	EXPECT(g_inp.hands[1].src == SOMVR_HSRC_SNAPSHOT && g_inp.hands[1].valid, "fallback: src %d", g_inp.hands[1].src);
	g_ls_fail = 0;
	atomic_store(&g_in.xr.ready, 0);
	c0 = g_ls_calls;
	iframe();
	EXPECT(g_ls_calls == c0 && g_inp.hands[1].src == SOMVR_HSRC_SNAPSHOT, "not ready: %d calls", g_ls_calls - c0);
	atomic_store(&g_in.xr.ready, 1);
	/* haptics: the trigger word, the game's rumble (both hands at 50 %, stopped when the game stops sending it) */
	somvr_haptic_req q = { 0, 0, 0, 0, 0 };
	while (hap_take(0, &q) || hap_take(1, &q)) {}
	EXPECT(in_haptic_word("r 0.5 200") && !in_haptic_word("x 0.5 200") && !in_haptic_word("r 2 200"), "haptic word parse");
	iframe();
	EXPECT(hap_take(1, &q) && NEAR(q.amplitude, 0.5, 1e-6) && NEAR(q.duration_ns, 2e8, 1e3) && q.source == SOMVR_HAP_TEST &&
		       !hap_take(0, &q) && log_has("HAP R n=") && log_has("amp=0.50 dur=200ms src=test"),
	       "haptic word: amp %g dur %lld src %u", q.amplitude, (long long)q.duration_ns, q.source);
	for (int i = 0; i < 20; i++) /* the 200 ms pulse ends on its own (no stop) */
		iframe();
	EXPECT(!hap_take(1, &q), "a flat pulse was stopped or re-posted (%g)", q.amplitude);
	for (int i = 0; i < 3; i++) {
		c_rum1((void *)0x1234, 0, 0.6f), c_rum2((void *)0x1234, 1, 0.2f);
		iframe();
	}
	int lh = hap_take(0, &q);
	float la = q.amplitude;
	int rh = hap_take(1, &q);
	EXPECT(g_rum_calls == 6 && g_rum_after == 9 && lh && rh && NEAR(la, 0.3, 1e-6) && NEAR(q.amplitude, 0.3, 1e-6) &&
		       q.source == SOMVR_HAP_RUMBLE,
	       "rumble: calls %d L %d %g R %d %g", g_rum_calls, lh, la, rh, q.amplitude);
	for (int i = 0; i < 4; i++)
		iframe();
	EXPECT(hap_take(0, &q) && q.amplitude == 0 && hap_take(1, &q) && q.amplitude == 0, "rumble gone: no stop (%g)", q.amplitude);
	/* the controller blade: a 160 deg swing of the right grip through an orc 1 m ahead = a heavy hit through
	 * Combat_DealHit, and a pulse on the right hand */
	g_pobj.pos[0] = 100, g_pobj.pos[1] = 120, g_pobj.pos[2] = 300;
	g_oobj[0].pos[0] = 100, g_oobj[0].pos[1] = 150, g_oobj[0].pos[2] = 420;
	g_cb.mode = CB_FULL, g_cb.blade = CB_BLADE_CONTROLLER, g_cb.prev.valid = 0, g_cb.bhist.have = 0;
	sc_speed_reset(&g_cb.sp), sc_cool_reset(&g_cb.cool), sc_charges_init(&g_cb.ch, &g_cb.cfg, g_cb_test_t);
	int d0 = g_dh.n;
	uint64_t h0 = g_cb.n.hits[SC_TIER_HEAVY];
	while (hap_take(1, &q)) {}
	int got_hit_hap = 0;
	for (int i = 0; i < 60; i++) { /* 0.2 s held right (80 deg), the swing in 0.25 s, held left */
		float ph = (float)i / 90.0f, a = ph < 0.2f ? 80 : ph < 0.45f ? 80 - 160 * (ph - 0.2f) / 0.25f : -80;
		float r = -a * 0.01745329252f * 0.5f; /* a game yaw of +a (right) = an OpenXR turn about +Y of -a */
		g_lpose[1].orientation = (XrQuaternionf){ 0, sinf(r), 0, cosf(r) };
		iframe();
		while (hap_take(1, &q))
			got_hit_hap |= q.amplitude >= 0.99f && q.source == SOMVR_HAP_COMBAT;
	}
	EXPECT(g_dh.n == d0 + 1 && g_cb.n.hits[SC_TIER_HEAVY] == h0 + 1 && g_dhs[d0 < 8 ? d0 : 7].target == (uintptr_t)&g_oobj[0] &&
		       got_hit_hap,
	       "controller blade: %d calls, heavy %llu, haptic %d", g_dh.n - d0, (unsigned long long)(g_cb.n.hits[SC_TIER_HEAVY] - h0),
	       got_hit_hap);
	g_lpose[1].orientation = (XrQuaternionf){ 0, 0, 0, 1 };
	g_cb.mode = CB_OFF;
	/* the session ends: no locate, the snapshot cleared by the layer -> lost */
	atomic_store(&g_sh.xr.state, SOMVR_XRS_DEAD);
	c0 = g_ls_calls;
	iframe();
	EXPECT(g_ls_calls == c0 && !g_inp.live && g_inp.why == SC_LIVE_NOTRUNNING, "session dead: calls %d live %d", g_ls_calls - c0,
	       g_inp.live);
	atomic_store(&g_sh.xr.state, SOMVR_XRS_RUNNING);
	g_layer_live = 0;
	EXPECT(log_has("INPUT t=") && log_has("VIN press cmd=16 Jump input=2010 (A)") && log_has("VIN release cmd=16 Jump"),
	       "INPUT/VIN lines missing");
	printf("input  somvr_input exported+active; v2 layer inert; pad mode forced twice; forward = ForwardAxis 1, head 90 right "
	       "= StrafeAxis 1; dead zone; deny 0/off; A = Jump+FreeRun+Evade on/off; tap; B = Activate; menu/PDA/crouch; "
	       "struggle; snap + smooth about the head; menus as a pad; loss/stall/thread/player change; hands at the views' "
	       "time; haptic word, rumble; controller blade heavy hit + pulse; %d events\n", g_ninev);
	g_pl = NULL;
	g_cb_test_t = -1;
}

/* ---- SOMVR_POINTER: stand-ins for the three CInterfaceMgr mouse calls, GetLocalPlayer, the front-end test, FUN_00a38e80
 * behind the menu items' two guard call sites, the UI rect and the m_GameUI view's offset; the layer's pointer block ---- */
static struct { char k; uintptr_t im; uint32_t pl; int btn, x, y; } g_lpev[256];
static int g_nlpev, g_padmode_calls;
static uintptr_t g_lp_player; /* what GetLocalPlayer returns */
static uint64_t g_lp_fe;      /* the front-end test's answer */
static char g_lp_P[0x440], g_lp_im[sizeof g_im], g_lp_gameui[0x60], g_lp_view[0x40]; /* g_lp_im: a copy of g_im */
static void lp_ev(char k, uintptr_t im, uint32_t pl, int btn, const int *pos)
{
	if (g_nlpev < 256)
		g_lpev[g_nlpev++] = (__typeof__(g_lpev[0])){ k, im, pl, btn, pos[0], pos[1] };
}
__attribute__((noipa)) uint64_t st_im_down(uintptr_t im, uint32_t pl, int btn, const int *pos) { lp_ev('D', im, pl, btn, pos); return 1; }
__attribute__((noipa)) uint64_t st_im_up(uintptr_t im, uint32_t pl, int btn, const int *pos) { lp_ev('U', im, pl, btn, pos); return 1; }
__attribute__((noipa)) uint64_t st_im_move(uintptr_t im, uint32_t pl, const int *pos) { lp_ev('M', im, pl, 0, pos); return 0; }
__attribute__((noipa)) uintptr_t st_localplayer(uintptr_t shell) { (void)shell; return g_lp_player; }
__attribute__((noipa)) uint64_t st_frontend(uintptr_t im) { (void)im; return g_lp_fe; }
__attribute__((noipa)) uint64_t t_padmode(void *P) { (void)P; g_padmode_calls++; return 1; }
/* the AS2 callbacks' shape: the guard's answer decides (here: returned, + a marker) */
__attribute__((noipa)) uint64_t c_guard_hover(void *P) { return t_padmode(P) + 10; }
__attribute__((noipa)) uint64_t c_guard_click(void *P) { return t_padmode(P) + 20; }
static char g_lphex[8][256];
static int lp_call_site(void *caller, void *target, struct site_spec *s, const char *name, int i)
{
	const unsigned char *p = caller;
	for (int k = 0; k < 96; k++) {
		int32_t rel;
		memcpy(&rel, p + k + 1, 4);
		if (p[k] == 0xe8 && (uintptr_t)(p + k + 5 + rel) == (uintptr_t)target) {
			hex_of(p + k, 5, g_lphex[i], 128);
			hex_of(target, 4, g_lphex[i] + 128, 128);
			*s = (struct site_spec){ name, K_CALL, (uintptr_t)(p + k), (uintptr_t)(p + k), g_lphex[i], (uintptr_t)target,
						 g_lphex[i] + 128 };
			return 0;
		}
	}
	return -1;
}
static void lp_entry_site(void *fn, struct site_spec *s, const char *name, int i)
{
	hex_of(fn, 8, g_lphex[i], 128);
	hex_of(fn, 4, g_lphex[i] + 128, 128);
	*s = (struct site_spec){ name, K_ENTRY, (uintptr_t)fn, (uintptr_t)fn, g_lphex[i], (uintptr_t)fn, g_lphex[i] + 128 };
}
static int lp_sites(struct site_spec *sp)
{
	if (lp_call_site((void *)c_guard_hover, (void *)t_padmode, &sp[LP_GUARD_HOVER], "LP_GUARD_HOVER", 0) ||
	    lp_call_site((void *)c_guard_click, (void *)t_padmode, &sp[LP_GUARD_CLICK], "LP_GUARD_CLICK", 1))
		return -1;
	lp_entry_site((void *)st_im_down, &sp[LP_IM_DOWN], "LP_IM_DOWN", 2);
	lp_entry_site((void *)st_im_up, &sp[LP_IM_UP], "LP_IM_UP", 3);
	lp_entry_site((void *)st_im_move, &sp[LP_IM_MOVE], "LP_IM_MOVE", 4);
	lp_entry_site((void *)st_localplayer, &sp[LP_LOCALPLAYER], "LP_LOCALPLAYER", 5);
	lp_entry_site((void *)st_frontend, &sp[LP_IFS_FRONTEND], "LP_IFS_FRONTEND", 6);
	return 0;
}
/* the layer's block for this frame (age_ns: how old its stamp is) */
static uint64_t g_lp_frame;
static void lp_pub(uint32_t quad, uint32_t flags, float u, float v, uint32_t downs, uint32_t ups, uint32_t consume,
		   int64_t age_ns)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	somvr_ptr_pub p = { .frame = ++g_lp_frame, .time = g_xrt, .mono_ns = (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec - age_ns,
			    .quad = quad, .flags = flags, .hand = 1, .consume = consume, .u = u, .v = v, .downs = downs,
			    .ups = ups, .dist = 1.6f, .img_w = 1920, .img_h = 1080 };
	somvr_ptr_publish(&g_in, &p);
}
#define LP_ON (SOMVR_PTRF_ON | SOMVR_PTRF_AIM | SOMVR_PTRF_DRAWN)
static int lp_n(char k, int from)
{
	int n = 0;
	for (int i = from; i < g_nlpev; i++)
		n += g_lpev[i].k == k;
	return n;
}
static int lp_last(char k, int x, int y, uint32_t pl) /* the last event of kind k is at x, y with player byte pl */
{
	for (int i = g_nlpev - 1; i >= 0; i--)
		if (g_lpev[i].k == k)
			return g_lpev[i].x == x && g_lpev[i].y == y && g_lpev[i].pl == pl && g_lpev[i].im == (uintptr_t)g_lp_im &&
			       (k == 'M' || g_lpev[i].btn == 1);
	return 0;
}
static int lp_tick_hap(somvr_haptic_req *q) /* a UI tick on the right hand since the last take */
{
	int got = 0;
	while (hap_take(1, q))
		got |= q->source == SOMVR_HAP_UI;
	return got;
}

static void ptr_tests(void)
{
	EXPECT(g_ptr.on && !g_ptr.hud && g_lp_h[0].installed && g_lp_h[1].installed && ptr_want() == SOMVR_INW_POINTER &&
		       log_has("POINTER config pointer=1") && log_has("HOOK LP_GUARD_HOVER ok call") &&
		       log_has("HOOK LP_GUARD_CLICK ok call") && log_has("POINTER site LP_IM_DOWN ok") &&
		       log_has("POINTER site LP_IFS_FRONTEND ok"),
	       "pointer start: on %d hooks %d %d want %#x", g_ptr.on, g_lp_h[0].installed, g_lp_h[1].installed, ptr_want());
	int pm0 = g_padmode_calls;
	EXPECT(c_guard_hover(g_lp_P) == 11 && c_guard_click(g_lp_P) == 21 && g_padmode_calls == pm0 + 2,
	       "the guards before the pointer: the game's answer (%d calls)", g_padmode_calls - pm0);
	/* the pointer's world: a CInterfaceMgr big enough for m_GameUI's holder (the input tests' state machines copied), the
	 * UI rect 1920x1080, the view offset 3,-2, a local player with index 0; a menu (pause: menus state 5) */
	memcpy(g_lp_im, g_im, sizeof g_im);
	uintptr_t v = (uintptr_t)g_lp_im;
	memcpy(g_shell + IN_SH_IM, &v, 8);
	v = (uintptr_t)g_lp_gameui, memcpy(g_lp_im + LP_IM_GAMEUI, &v, 8);
	v = (uintptr_t)g_lp_view, memcpy(g_lp_gameui + LP_GAMEUI_VIEW, &v, 8);
	int32_t ofs[2] = { 3, -2 }, rc[4] = { 0, 0, 1920, 1080 };
	memcpy(g_lp_view + LP_VIEW_OFS, ofs, 8), memcpy(g_shell + LP_SH_UIRECT, rc, 16);
	g_lp_P[LP_P_INDEX] = 0, g_lp_player = (uintptr_t)g_lp_P, g_lp_fe = 0;
	bind_cmd(56, 2014, 1, -1, 1), g_act_menu[56] = 1; /* a menu command on RT: what the pointer's trigger must not press */
	g_cb_test_t = 6000;
	g_inp.lines = 0, g_inp.lsec = 0, g_ptr.next_log = 0;
	g_pl = &g_player; /* H4's player, as in the input tests (the arms tests start from its tracker) */
	layer_up();
	atomic_store(&g_sh.xr.state, SOMVR_XRS_RUNNING);
	g_sm_menus.sc = 5, g_menu_set = 1;
	iframe(), iframe();
	EXPECT(g_inp.live && g_inp.ctx == SC_CTX_MENU, "not live in a menu: live %d ctx %d", g_inp.live, g_inp.ctx);
	somvr_haptic_req q;
	lp_tick_hap(&q);
	int e0 = g_nlpev;
	iframe();
	EXPECT(g_nlpev == e0 && !atomic_load(&g_ptr.live), "calls without a pointer block: %d", g_nlpev - e0);
	/* on the menu quad at uv 0.25, 0.75: one move to 480+3, 810-2 with the player's byte; a light tick */
	lp_pub(SOMVR_PTRQ_MENU, LP_ON, 0.25f, 0.75f, 0, 0, 2, 0);
	iframe();
	EXPECT(lp_n('M', e0) == 1 && lp_last('M', 483, 808, 0) && atomic_load(&g_ptr.live) == 1 && lp_tick_hap(&q) &&
		       log_has("POINTER on the menu quad at uv 0.2500,0.7500 -> 483,808 (UI 1920x1080 offset 3,-2"),
	       "on: %d moves, at %d,%d, live %d", lp_n('M', e0), g_lpev[g_nlpev ? g_nlpev - 1 : 0].x,
	       g_lpev[g_nlpev ? g_nlpev - 1 : 0].y, atomic_load(&g_ptr.live));
	lp_pub(SOMVR_PTRQ_MENU, LP_ON, 0.25f, 0.75f, 0, 0, 2, 0);
	iframe();
	EXPECT(lp_n('M', e0) == 1 && !lp_tick_hap(&q), "the same pixel moved again, or a second enter tick");
	lp_pub(SOMVR_PTRQ_MENU, LP_ON, 0.5f, 0.5f, 0, 0, 2, 0);
	iframe();
	EXPECT(lp_n('M', e0) == 2 && lp_last('M', 963, 538, 0), "a new point: %d moves", lp_n('M', e0));
	/* a click: the down, then its up (the block's counters), a stronger tick */
	g_cb_test_t += 0.1;
	lp_pub(SOMVR_PTRQ_MENU, LP_ON | SOMVR_PTRF_DOWN, 0.5f, 0.5f, 1, 0, 2, 0);
	iframe();
	EXPECT(lp_n('D', e0) == 1 && lp_last('D', 963, 538, 0) && lp_n('U', e0) == 0 && lp_tick_hap(&q) && q.amplitude > 0.25f,
	       "down: %d, up %d, tick %g", lp_n('D', e0), lp_n('U', e0), q.amplitude);
	lp_pub(SOMVR_PTRQ_MENU, LP_ON, 0.5f, 0.5f, 1, 1, 2, 0);
	iframe();
	EXPECT(lp_n('U', e0) == 1 && lp_last('U', 963, 538, 0) && log_has("POINTER down at 963,538 pl=0 hand R") &&
		       log_has("POINTER up at 963,538"),
	       "up: %d", lp_n('U', e0));
	/* a whole click between two ticks: the down now, the up one tick later */
	lp_pub(SOMVR_PTRQ_MENU, LP_ON, 0.5f, 0.5f, 2, 2, 2, 0);
	iframe();
	EXPECT(lp_n('D', e0) == 2 && lp_n('U', e0) == 1, "a click within a tick: downs %d ups %d", lp_n('D', e0), lp_n('U', e0));
	iframe();
	EXPECT(lp_n('U', e0) == 2, "its up the tick after: %d", lp_n('U', e0));
	/* the guards while live: 0 without the game's call; a menu item's hover = a light tick */
	pm0 = g_padmode_calls;
	EXPECT(c_guard_hover(g_lp_P) == 10 && c_guard_click(g_lp_P) == 20 && g_padmode_calls == pm0 &&
		       atomic_load(&g_lp_h[0].mods) == 1 && atomic_load(&g_lp_h[1].mods) == 1,
	       "the guards while live: %d game calls", g_padmode_calls - pm0);
	g_cb_test_t += 0.1;
	lp_tick_hap(&q);
	iframe();
	EXPECT(lp_tick_hap(&q) && q.amplitude < 0.25f && g_ptr.hovers == 1 && g_ptr.clicks == 1, "hover tick: hovers %llu",
	       (unsigned long long)g_ptr.hovers);
	/* off the quad: one park off the UI; the guards back 0.5 s later */
	lp_pub(SOMVR_PTRQ_MENU, SOMVR_PTRF_AIM | SOMVR_PTRF_DRAWN, 0, 0, 2, 2, 0, 0);
	iframe();
	lp_pub(SOMVR_PTRQ_MENU, SOMVR_PTRF_AIM | SOMVR_PTRF_DRAWN, 0, 0, 2, 2, 0, 0);
	iframe();
	EXPECT(lp_n('M', e0) == 3 && lp_last('M', -1000, -1000, 0) && log_has("POINTER park (off the quad)"),
	       "off: %d moves", lp_n('M', e0));
	for (int i = 0; i < 50; i++)
		lp_pub(SOMVR_PTRQ_MENU, SOMVR_PTRF_AIM, 0, 0, 2, 2, 0, 0), iframe();
	EXPECT(!atomic_load(&g_ptr.live) && c_guard_hover(g_lp_P) == 11, "the guards are not back after 0.55 s");
	/* the trigger's owner: pressed on the quad = the pointer's (no RT for command 56); pressed off it = the pad's, kept
	 * for the whole press even when the ray comes on the quad */
	int i0 = g_ninev;
	lp_pub(SOMVR_PTRQ_MENU, LP_ON, 0.5f, 0.5f, 2, 2, 2, 0);
	g_lst.hand[1].trigger = 0.8f;
	iframe();
	lp_pub(SOMVR_PTRQ_MENU, LP_ON, 0.5f, 0.5f, 2, 2, 2, 0);
	iframe();
	EXPECT(nev('N', 56, i0) == 0 && g_ptr.consumed >= 2, "the pointer's trigger pressed RT (%d), consumed %llu",
	       nev('N', 56, i0), (unsigned long long)g_ptr.consumed);
	g_lst.hand[1].trigger = 0;
	iframe();
	lp_pub(SOMVR_PTRQ_MENU, SOMVR_PTRF_AIM, 0, 0, 2, 2, 0, 0);
	g_lst.hand[1].trigger = 0.8f;
	iframe();
	EXPECT(nev('N', 56, i0) == 1, "the pad's trigger: RT %d", nev('N', 56, i0));
	lp_pub(SOMVR_PTRQ_MENU, LP_ON, 0.5f, 0.5f, 2, 2, 2, 0);
	iframe();
	EXPECT(nev('F', 56, i0) == 0, "the ray came on the quad: RT released mid-press");
	g_lst.hand[1].trigger = 0;
	iframe();
	EXPECT(nev('F', 56, i0) == 1, "RT's release");
	/* a held click when the block goes stale (the layer stopped): its up, then the park */
	int d0 = lp_n('D', e0), u0 = lp_n('U', e0), m0 = lp_n('M', e0);
	lp_pub(SOMVR_PTRQ_MENU, LP_ON | SOMVR_PTRF_DOWN, 0.5f, 0.5f, 3, 2, 2, 0);
	iframe();
	lp_pub(SOMVR_PTRQ_MENU, LP_ON | SOMVR_PTRF_DOWN, 0.5f, 0.5f, 3, 2, 2, 400000000);
	iframe();
	EXPECT(lp_n('D', e0) == d0 + 1 && lp_n('U', e0) == u0 + 1 && lp_last('M', -1000, -1000, 0) && g_ptr.stale >= 1 &&
		       g_ptr.cancels >= 1 && log_has("POINTER park (no pointer block)"),
	       "stale: downs %d ups %d", lp_n('D', e0) - d0, lp_n('U', e0) - u0);
	lp_pub(SOMVR_PTRQ_MENU, LP_ON, 0.5f, 0.5f, 3, 3, 2, 0); /* the layer's up arrives late: not sent again */
	iframe();
	EXPECT(lp_n('U', e0) == u0 + 1, "a late up was sent again");
	/* the world (not a menu): no mouse */
	g_sm_menus.sc = 0, g_menu_set = 0;
	m0 = lp_n('M', e0);
	lp_pub(SOMVR_PTRQ_MENU, LP_ON, 0.25f, 0.25f, 3, 3, 2, 0);
	iframe();
	EXPECT(lp_n('M', e0) == m0 + 1 && lp_last('M', -1000, -1000, 0) && g_ptr.nomenu >= 1, "the world: moves %d",
	       lp_n('M', e0) - m0); /* the cursor that was on the UI is parked once */
	lp_pub(SOMVR_PTRQ_MENU, LP_ON, 0.25f, 0.25f, 3, 3, 2, 0);
	iframe();
	EXPECT(lp_n('M', e0) == m0 + 1, "the world moved the mouse");
	g_sm_menus.sc = 5, g_menu_set = 1;
	/* the front end without a player: the byte 0xff; neither a player nor the front end: no call */
	g_lp_player = 0, g_lp_fe = 1;
	lp_pub(SOMVR_PTRQ_MENU, LP_ON, 0.25f, 0.25f, 3, 3, 2, 0);
	iframe();
	EXPECT(lp_last('M', 483, 268, 0xff), "front end: not pl 0xff");
	g_lp_fe = 0;
	m0 = lp_n('M', e0);
	uint64_t np = g_ptr.noplayer;
	lp_pub(SOMVR_PTRQ_MENU, LP_ON, 0.75f, 0.25f, 3, 3, 2, 0);
	iframe();
	EXPECT(lp_n('M', e0) == m0 && g_ptr.noplayer == np + 1, "no player byte: %d moves", lp_n('M', e0) - m0);
	g_lp_player = (uintptr_t)g_lp_P;
	/* a HUD block in a menu (build 6: the layer's menu board, SOMVR_XR_MENU=front): a target like the menu quad */
	lp_pub(SOMVR_PTRQ_HUD, LP_ON, 0.5f, 0.5f, 3, 3, 2, 0);
	iframe();
	m0 = lp_n('M', e0);
	lp_pub(SOMVR_PTRQ_HUD, LP_ON, 0.6f, 0.5f, 3, 3, 2, 0);
	iframe();
	EXPECT(lp_n('M', e0) == m0 + 1, "a HUD block in a menu did not move the mouse (%d moves)", lp_n('M', e0) - m0);
	flush_log();
	EXPECT(log_has("POINTER LP_GUARD_HOVER n=") && log_has("(the pointer is live)"), "no guard lines");
	printf("pointer: guards (game / live 0 / back), map with offset, moves on change, down/up a tick apart, click in a tick, "
	       "park, ticks (on, hover, click), trigger owner per press, stale, world, front end 0xff, no player, HUD board; "
	       "%d IM calls\n", g_nlpev);
	/* back to the input tests' world */
	v = (uintptr_t)g_im, memcpy(g_shell + IN_SH_IM, &v, 8);
	g_sm_menus.sc = 0, g_menu_set = 0;
	g_layer_live = 0;
	lp_pub(SOMVR_PTRQ_NONE, 0, 0, 0, 3, 3, 0, 0);
	g_pl = NULL;
	g_cb_test_t = -1;
}

/* ---- SOMVR_ARMS: stand-in trackers (vtable slot 3 = vf_03 swaps the animation in), their pose buffers, the model
 * data and run 3's skeleton, the player's CSimpleModel (+0x178), FUN_008f3c20 ------------------------------------- */
#include "tests/run3_skeleton.h"
typedef struct { char pad[0x40]; sa_node *nodes; uint32_t count; } st_pb_t;
typedef struct { uint8_t *ents; uint32_t count, pad0; char pad[0x40 - 0x10]; uint32_t *hashes; } st_skel_t;
typedef struct { char pad[0x10]; st_skel_t *skel; } st_md_t;
typedef struct {
	uintptr_t vt;
	char pad0[0x58 - 8];
	st_pb_t *pb;
	char pad1[0x168 - 0x60];
	void *hobj;
	char pad2[0x808 - 0x170];
	st_md_t *md;
} st_trk_t;
_Static_assert(offsetof(st_pb_t, nodes) == PB_NODES && offsetof(st_pb_t, count) == PB_COUNT &&
	       offsetof(st_skel_t, count) == SK_COUNT && offsetof(st_skel_t, hashes) == SK_HASHES &&
	       offsetof(st_md_t, skel) == MD_SKEL && offsetof(st_trk_t, pb) == TRK_POSE && offsetof(st_trk_t, hobj) == TRK_HOBJ &&
	       offsetof(st_trk_t, md) == TRK_MDATA, "stand-in arm layouts");
static void *volatile g_vttrk[8];          /* CSimpleModelTrackerNode's vtable: slot 3 = vf_03 (ARM_TRK_EVAL) */
static st_trk_t g_ptrk, g_otrk;            /* the player's tracker, an orc's (the same skeleton) */
static st_pb_t g_ppb, g_opb;
static _Alignas(16) sa_node g_pnodes[RUN3_N], g_onodes[RUN3_N], g_anim[RUN3_N]; /* the buffers; the animation (run 3) */
static uint8_t g_ents[RUN3_N * SK_ENTRY];
static uint32_t g_hashes[RUN3_N], g_hashes2[RUN3_N];
static st_skel_t g_skel, g_skel2;
static st_md_t g_md;
static char g_amodel[0x200];               /* the player's CSimpleModel: +0x178 its tracker */
static int g_trk_calls;
static XrTime g_arm_lt;                    /* the arms' locate time this frame (phase 2) */
__attribute__((noipa)) void t_trk_eval(void *trk) /* vf_03: the job's pose swapped in (here: the animation copied) */
{
	st_trk_t *t = trk;
	g_trk_calls++;
	if (t->pb && t->pb->nodes)
		memcpy(t->pb->nodes, g_anim, sizeof g_anim);
}
__attribute__((noipa)) uintptr_t st_arm_modelof(uintptr_t obj)
{
	return obj == (uintptr_t)&g_pobj ? (uintptr_t)g_amodel : 0;
}
static void arm_world(void)
{
	for (int i = 0; i < RUN3_N; i++) {
		const float *r = RUN3_POSE[i];
		sa_node_set(&g_anim[i], (sa_xf){ { r[0], r[1], r[2] }, { r[3], r[4], r[5], r[6] } });
		g_ents[i * SK_ENTRY] = RUN3_PARENT[i];
		g_hashes[i] = RUN3_HASH[i];
	}
	memcpy(g_pnodes, g_anim, sizeof g_anim), memcpy(g_onodes, g_anim, sizeof g_anim);
	g_skel = (st_skel_t){ g_ents, RUN3_N, 0, { 0 }, g_hashes };
	g_md.skel = &g_skel;
	g_ppb.nodes = g_pnodes, g_ppb.count = RUN3_N, g_opb.nodes = g_onodes, g_opb.count = RUN3_N;
	g_ptrk.vt = g_otrk.vt = (uintptr_t)g_vttrk;
	g_ptrk.pb = &g_ppb, g_ptrk.hobj = &g_pobj, g_ptrk.md = &g_md;
	g_otrk.pb = &g_opb, g_otrk.hobj = &g_oobj[1], g_otrk.md = &g_md;
	uintptr_t v = (uintptr_t)&g_ptrk;
	memcpy(g_amodel + SM_TRK, &v, sizeof v);
}
typedef void (*t_trk_fn)(void *);
static void bw_game(void); /* SOMVR_BOW's stand-in game (bow_tests' region; nothing while g_bwg.on = 0) */
/* one game frame as the game orders it: the layer's wait and sync, I0, the player update (and the stand-in game's bow
 * mode), phase 2 (an orc's tracker, then the player's), then the render's H4 and the present */
static void aframe(void)
{
	g_cb_test_t += 1.0 / 90;
	g_xrt += 11111111;
	somvr_time_publish(&g_sh, g_xrt, 11111111);
	if (g_layer_live) {
		g_lst.sync_seq++, g_lst.time = g_xrt;
		for (int h = 0; h < 2; h++)
			memcpy(&g_lst.hand[h].grip, &g_lpose[h], sizeof g_lst.hand[h].grip);
		somvr_input_publish(&g_in, &g_lst);
	}
	((t_vf04_fn)g_vtin[4])(&g_cwbi);
	if (g_pl)
		player_update();
	bw_game();
	g_pu_done = 1;
	g_ls_time = 0;
	((t_trk_fn)g_vttrk[3])(&g_otrk);
	((t_trk_fn)g_vttrk[3])(&g_ptrk);
	g_arm_lt = g_ls_time;
	frame();
}
static float qang2(sv_quat a, sv_quat b) /* the angle between two rotations: a^-1 b in double (exact near 0) */
{
	double ax = -a.x, ay = -a.y, az = -a.z, aw = a.w;
	double x = aw * b.x + ax * b.w + ay * b.z - az * b.y, y = aw * b.y - ax * b.z + ay * b.w + az * b.x,
	       z = aw * b.z + ax * b.y - ay * b.x + az * b.w, w = aw * b.w - ax * b.x - ay * b.y - az * b.z;
	return (float)(2.0 * atan2(sqrt(x * x + y * y + z * z), fabs(w)));
}
/* the weapon node of arm h in the world (Talion's object x the edited buffer) against this frame's grip x CAL with the
 * frame's rig (what the eyes got): cm, and the rotation's error in *deg */
static float arm_err(int h, float *deg)
{
	sa_xf X = { { g_pobj.pos[0], g_pobj.pos[1], g_pobj.pos[2] }, sv_qnormalize(sv_quat_of(g_pobj.rot)) };
	sa_xf wn = sa_xf_mul(X, sa_node_xf(&g_pnodes[g_arm.ch[h].wpn]));
	sv_vec3 p;
	sv_quat q;
	somvr_hand_game(&g_inp.hands[h], &p, &q, NULL, NULL);
	sa_xf gw = { somvr_rig_point(&g_rig, g_fr.base, p), somvr_rig_quat(&g_rig, g_fr.base, q) };
	sa_xf want = sa_xf_mul(gw, g_arm.cal[h]);
	*deg = qang2(wn.q, want.q) / 0.01745329252f;
	return sqrtf((wn.p.x - want.p.x) * (wn.p.x - want.p.x) + (wn.p.y - want.p.y) * (wn.p.y - want.p.y) +
		     (wn.p.z - want.p.z) * (wn.p.z - want.p.z));
}
/* the player's buffer outside both arms is the animation's, bit for bit; inside, `arms` = 1: edited, 0: the animation */
static int body_untouched(void)
{
	for (int i = 0; i < RUN3_N; i++) {
		int arm = (i >= g_arm.ch[0].lo && i <= g_arm.ch[0].hi) || (i >= g_arm.ch[1].lo && i <= g_arm.ch[1].hi);
		if (!arm && memcmp(&g_pnodes[i], &g_anim[i], sizeof g_anim[i]))
			return 0;
	}
	return 1;
}
static void *trk_thread(void *a)
{
	((t_trk_fn)g_vttrk[3])(a);
	return NULL;
}

static void arms_tests(void)
{
	somvr_rig rig0 = g_rig;
	somvr_rig_cfg cfg0 = g_rigcfg;
	float pos0[3], rot0[4];
	memcpy(pos0, g_pobj.pos, sizeof pos0), memcpy(rot0, g_pobj.rot, sizeof rot0);
	g_views_fake = 0, g_rt_result = XR_SUCCESS, g_layout = SOMVR_LAYOUT_A, g_rigcfg.layout = SOMVR_LAYOUT_A;
	g_rigcfg.base = SOMVR_BASE_HEAD, g_rigcfg.yaw = SOMVR_YAW_CAMERA;
	somvr_rig_init(&g_rig); /* a fresh recentre: no snap turn left over from the input tests */
	g_rt_head[0] = g_rt_head[1] = g_rt_head[2] = 0, g_rt_q = (sv_quat){ 0, 0, 0, 1 };
	g_pobj.pos[0] = 100, g_pobj.pos[1] = 127.5f, g_pobj.pos[2] = 300;
	g_pobj.rot[0] = g_pobj.rot[1] = g_pobj.rot[2] = 0, g_pobj.rot[3] = 1;
	g_rm[0] = g_rm[1] = g_rm[2] = 0;
	g_cb_test_t = 7000;
	g_arm.lines = 0, g_arm.next_log = g_cb_test_t + 1, g_arm.last_line = g_cb_test_t;
	g_xrt = g_vst.last_time + 1000000000;
	g_pl = &g_player;
	layer_up();
	EXPECT(g_h[HK_ARM].installed && g_arm.on && g_arm.ok_model && log_has("HOOK ARM_TRK_EVAL ok vslot") &&
		       log_has("ARMS site ARM_MODELOF ok") && log_has("ARMS config arms=1 pole=0.40,-1.00,-0.30") &&
		       g_arm.vt_trk == (uintptr_t)g_vttrk,
	       "arms start: hook %d on %d site %d vtable %#lx", g_h[HK_ARM].installed, g_arm.on, g_arm.ok_model,
	       (unsigned long)g_arm.vt_trk);
	/* the first frame: no recent rig yet (the last one is from the input tests' clock), so phase 2 passes through */
	uint64_t e0 = g_arm.n.evals;
	aframe();
	EXPECT(g_arm.n.evals == e0 + 1 && !memcmp(g_pnodes, g_anim, sizeof g_anim) && !memcmp(g_onodes, g_anim, sizeof g_anim) &&
		       g_arm.trk == (uintptr_t)&g_ptrk && g_arm.chains_ok && log_has("ARMS tracker frame=") &&
		       log_has("ARMS skeleton trk=") && log_has("(trk+0x168=") && log_has(": the object)"),
	       "first frame: evals %llu edited %d tracker %#lx chains %d", (unsigned long long)(g_arm.n.evals - e0),
	       memcmp(g_pnodes, g_anim, sizeof g_anim) != 0, (unsigned long)g_arm.trk, g_arm.chains_ok);
	/* the ease in: the weight rises every frame to 1 (0.15 s at 90 Hz), then both weapon nodes sit on their grips */
	float last = 0;
	int up = 1, frames = 0;
	while (g_arm.weight[SA_R] < 1 && frames < 40) {
		aframe();
		frames++;
		up &= g_arm.weight[SA_R] > last && g_arm.weight[SA_L] == g_arm.weight[SA_R];
		last = g_arm.weight[SA_R];
	}
	EXPECT(up && frames == 14, "ease in: %d frames, rising %d", frames, up);
	float er[2], dg[2];
	for (int h = 0; h < 2; h++)
		er[h] = arm_err(h, &dg[h]);
	EXPECT(er[0] < 0.2f && er[1] < 0.2f && dg[0] < 0.5f && dg[1] < 0.5f && g_arm.rep[0].reached && g_arm.rep[1].reached,
	       "on the grips: L %.3f cm %.2f deg, R %.3f cm %.2f deg (reached %d %d)", er[0], dg[0], er[1], dg[1],
	       g_arm.rep[0].reached, g_arm.rep[1].reached);
	EXPECT(!memcmp(g_onodes, g_anim, sizeof g_anim) && body_untouched() && memcmp(g_pnodes, g_anim, sizeof g_anim),
	       "only the player's arms changed: orc %d body %d", !memcmp(g_onodes, g_anim, sizeof g_anim), body_untouched());
	EXPECT(g_arm_lt == g_rt_time && g_arm_lt > 0, "the arms' grips at %lld, the eyes at %lld: not the same T'",
	       (long long)g_arm_lt, (long long)g_rt_time);
	EXPECT(log_has("ARMS R on t=") && log_has("ARMS L on t="), "no ARMS on lines");
	/* walking (5 cm per update, 4.5 m/s): the head base and the hands move with Talion, the carried rig keeps them on */
	float wmax = 0;
	g_rm[2] = 5;
	for (int i = 0; i < 30; i++) {
		aframe();
		for (int h = 0; h < 2; h++)
			wmax = fmaxf(wmax, arm_err(h, &dg[h]));
	}
	g_rm[2] = 0;
	EXPECT(wmax < 0.2f && g_pobj.pos[2] > 300 + 140, "walking: the weapon nodes off by up to %.3f cm (moved to z %.0f)", wmax,
	       g_pobj.pos[2]);
	/* the bone blade (SOMVR_COMBAT=log, the controller blade): from R_Weapon, equal to the controller's while reached */
	g_cb.mode = CB_LOG, g_cb.blade = CB_BLADE_CONTROLLER, g_cb.prev.valid = 0, g_cb.bhist.have = 0;
	uint64_t bb = g_arm.n.blade_bone;
	aframe();
	sc_grip gr;
	memset(&gr, 0, sizeof gr);
	sv_vec3 gp;
	sv_quat gq;
	somvr_hand_game(&g_inp.hands[SA_R], &gp, &gq, NULL, NULL);
	gr.valid = 1, gr.pos[0] = gp.x, gr.pos[1] = gp.y, gr.pos[2] = gp.z, gr.xr_t = 1;
	sv_quat_to(gq, gr.quat);
	sc_rigxf rx = { { g_fr.base.pos.x, g_fr.base.pos.y, g_fr.base.pos.z }, { g_rig.p0.x, g_rig.p0.y, g_rig.p0.z },
			g_fr.base.yaw - g_rig.yaw0, somvr_rig_scale(&g_rig) };
	sc_blade_hist hh = { 0 };
	somvr_blade bx;
	somvr_blade_controller(&g_cb.bcfg, &gr, &rx, 0, &hh, &bx);
	float dbs = dist3(bx.base, g_cb.prev.base), dtp = dist3(bx.tip, g_cb.prev.tip);
	EXPECT(g_arm.n.blade_bone == bb + 1 && g_cb.prev.valid && dbs < 0.3f && dtp < 0.5f,
	       "bone blade: %llu used, base %.3f tip %.3f cm from the controller blade", (unsigned long long)(g_arm.n.blade_bone - bb),
	       dbs, dtp);
	XrPosef keep = g_lpose[1];
	g_lpose[1].position.z = -1.5f; /* 1.5 m ahead: out of reach, the drawn sword stays at the arm's length */
	for (int i = 0; i < 3; i++)
		aframe();
	somvr_hand_game(&g_inp.hands[SA_R], &gp, &gq, NULL, NULL);
	gr.pos[0] = gp.x, gr.pos[1] = gp.y, gr.pos[2] = gp.z;
	hh.have = 0;
	somvr_blade_controller(&g_cb.bcfg, &gr, &rx, 0, &hh, &bx);
	EXPECT(!g_arm.rep[SA_R].reached && g_arm.rep[SA_R].clamp == SA_CLAMP_FAR && dist3(bx.base, g_cb.prev.base) > 50,
	       "out of reach: reached %d clamp %d, the bone blade %.1f cm behind the controller's", g_arm.rep[SA_R].reached,
	       g_arm.rep[SA_R].clamp, dist3(bx.base, g_cb.prev.base));
	g_lpose[1] = keep;
	g_cb.mode = CB_OFF;
	/* the hands lost (no locate, no new snapshot): both arms ease out to the animation's, continuously */
	g_layer_live = 0, g_ls_fail = 1;
	float mstep = 0;
	sa_node prev[RUN3_N];
	memcpy(prev, g_pnodes, sizeof prev);
	int k;
	for (k = 0; k < 40 && (g_arm.weight[0] > 0 || g_arm.weight[1] > 0 || k < 2); k++) {
		aframe();
		for (int i = g_arm.ch[SA_R].lo; i <= g_arm.ch[SA_R].hi; i++)
			mstep = fmaxf(mstep, dist3(g_pnodes[i].pos, prev[i].pos));
		memcpy(prev, g_pnodes, sizeof prev);
	}
	EXPECT(g_arm.weight[0] == 0 && g_arm.weight[1] == 0 && !memcmp(g_pnodes, g_anim, sizeof g_anim) && mstep < 16 && k > 14 &&
		       log_has("ARMS R off") && g_arm.n.nolive > 0,
	       "hands lost: weights %.2f %.2f after %d frames, animation %d, max step %.2f cm", g_arm.weight[0], g_arm.weight[1],
	       k, !memcmp(g_pnodes, g_anim, sizeof g_anim), mstep);
	g_layer_live = 1, g_ls_fail = 0;
	for (int i = 0; i < 20; i++)
		aframe();
	EXPECT(g_arm.weight[SA_R] == 1 && arm_err(SA_R, &dg[1]) < 0.2f, "hands back: weight %.2f", g_arm.weight[SA_R]);
	/* another thread's phase 2: passed through */
	uint64_t o0 = g_arm.n.other;
	pthread_t th;
	pthread_create(&th, NULL, trk_thread, &g_ptrk);
	pthread_join(th, NULL);
	EXPECT(g_arm.n.other == o0 + 1 && !memcmp(g_pnodes, g_anim, sizeof g_anim), "another thread: other %llu, edited %d",
	       (unsigned long long)(g_arm.n.other - o0), memcmp(g_pnodes, g_anim, sizeof g_anim) != 0);
	/* a rebuilt skeleton without R_Arml: no edit, said once; the good one back: on again */
	memcpy(g_hashes2, g_hashes, sizeof g_hashes2);
	g_hashes2[110] = 0x1234;
	g_skel2 = (st_skel_t){ g_ents, RUN3_N, 0, { 0 }, g_hashes2 };
	g_md.skel = &g_skel2;
	aframe();
	EXPECT(!g_arm.chains_ok && !memcmp(g_pnodes, g_anim, sizeof g_anim) && log_has("an arm is missing") &&
		       log_has("no R_Arml (0x844d7d34)"),
	       "broken skeleton: chains %d edited %d", g_arm.chains_ok, memcmp(g_pnodes, g_anim, sizeof g_anim) != 0);
	g_md.skel = &g_skel;
	for (int i = 0; i < 20; i++)
		aframe();
	EXPECT(g_arm.chains_ok && g_arm.weight[SA_R] == 1 && arm_err(SA_R, &dg[1]) < 0.2f, "skeleton back: chains %d weight %.2f",
	       g_arm.chains_ok, g_arm.weight[SA_R]);
	/* no stereo frame (the session dead: flat H4s): the rig goes stale, the arms ease out */
	atomic_store(&g_sh.xr.state, SOMVR_XRS_DEAD);
	for (int i = 0; i < 30; i++)
		aframe();
	EXPECT(g_arm.weight[0] == 0 && g_arm.weight[1] == 0 && !memcmp(g_pnodes, g_anim, sizeof g_anim) && g_arm.n.norig > 0,
	       "flat frames: weights %.2f %.2f", g_arm.weight[0], g_arm.weight[1]);
	atomic_store(&g_sh.xr.state, SOMVR_XRS_RUNNING);
	/* arms off: the hook passes everything through */
	g_arm.on = 0;
	for (int i = 0; i < 3; i++)
		aframe();
	EXPECT(!memcmp(g_pnodes, g_anim, sizeof g_anim), "arms off edited the pose");
	g_arm.on = 1;
	for (int i = 0; i < 20; i++)
		aframe();
	EXPECT(g_arm.weight[SA_R] == 1 && arm_err(SA_R, &dg[1]) < 0.2f && g_trk_calls > 200 && log_has("ARMS t="),
	       "arms on again: weight %.2f, tracker calls %d", g_arm.weight[SA_R], g_trk_calls);
	printf("arms   ARM_TRK_EVAL on stand-in trackers: the player's only; eased in over %d frames; both weapon nodes on "
	       "their grips (L %.3f R %.3f cm) at the eyes' T'; walking %.3f cm; the bone blade = the controller's while "
	       "reached, the drawn sword when not; lost hands / flat frames ease out to the animation; another thread, a "
	       "broken skeleton, arms off: untouched\n", frames, er[0], er[1], wmax);
	g_rig = rig0, g_rigcfg = cfg0;
	memcpy(g_pobj.pos, pos0, sizeof pos0), memcpy(g_pobj.rot, rot0, sizeof rot0);
	g_layer_live = 0;
	g_pl = NULL;
	g_cb_test_t = -1;
}

/* ---- SOMVR_HIDE_HEAD=bone|near (start ran with the piece list 3,5: the words and the switch's bits white box here,
 * then the collapse on the stand-in trackers, GetNodeTransform reading the player's buffer, and the eyes' near plane) */
/* the player's buffer: Head and everything outside its subtree 29..85 (and the arms' blocks while the arms are on) bit
 * for bit the animation's; the subtree within 0.02 cm of the Head joint with the animation's rotations (collapsed) or
 * the animation's bit for bit (not) */
static int head_state(int collapsed)
{
	const float *c = g_anim[28].pos;
	for (int i = 0; i < RUN3_N; i++) {
		int sub = i >= 29 && i <= 85;
		if (g_arm.on && ((i >= g_arm.ch[0].lo && i <= g_arm.ch[0].hi) || (i >= g_arm.ch[1].lo && i <= g_arm.ch[1].hi)))
			continue;
		if (!sub || !collapsed) {
			if (memcmp(&g_pnodes[i], &g_anim[i], sizeof g_anim[i]))
				return 0;
		} else if (dist3(g_pnodes[i].pos, c) > 0.02f || memcmp(g_pnodes[i].q, g_anim[i].q, sizeof g_anim[i].q)) {
			return 0;
		}
	}
	return 1;
}
/* the eye base the head base must give: Talion's object x the animation's Head (never the collapsed one) */
static sv_vec3 head_want(void)
{
	sa_xf X = { { g_pobj.pos[0], g_pobj.pos[1], g_pobj.pos[2] }, sv_qnormalize(sv_quat_of(g_pobj.rot)) };
	return sa_xf_mul(X, sa_node_xf(&g_anim[28])).p;
}
static float head_err(void)
{
	sv_vec3 m = eye_mid(), w = head_want();
	return sqrtf((m.x - w.x) * (m.x - w.x) + (m.y - w.y) * (m.y - w.y) + (m.z - w.z) * (m.z - w.z));
}

static void hb_tests(void)
{
	/* 1. the words */
	EXPECT(hb_parse("bone") == 1 && hb_parse("near") == 2 && hb_parse("bone+near") == 3 && hb_parse("near+bone") == 3 &&
		       hb_parse("3,5") == 0 && hb_parse("none") == 0 && hb_parse("auto") == 0 && hb_parse("") == 0 &&
		       hb_parse(NULL) == 0 && hb_parse("bone+bone") == -1 && hb_parse("bone+") == -1 && hb_parse("+near") == -1 &&
		       hb_parse("Bone") == -1 && hb_parse("bones") == -1 && hb_parse("x") == -1 && hb_parse("bone+near+") == -1,
	       "hb_parse");
	/* 2. the switch: bone asks for ARM_TRK_EVAL with SOMVR_ARMS off, near alone for no hook, both need the head base */
	int arm_on0 = g_arm.on, fake0 = g_views_fake, layout0 = g_layout;
	somvr_rig rig0 = g_rig;
	somvr_rig_cfg cfg0 = g_rigcfg;
	float pos0[3], rot0[4];
	memcpy(pos0, g_pobj.pos, sizeof pos0), memcpy(rot0, g_pobj.rot, sizeof rot0);
	g_arm.on = 0, g_rigcfg.base = SOMVR_BASE_HEAD;
	static const char *const words[] = { "bone", "near", "near+bone", "bone+x", "3,5" };
	uint32_t bits[6];
	int on[6][2];
	for (int i = 0; i < 5; i++) {
		setenv("SOMVR_HIDE_HEAD", words[i], 1);
		bits[i] = hb_config(), on[i][0] = g_hb.bone, on[i][1] = g_hb.near;
	}
	g_rigcfg.base = SOMVR_BASE_CAMERA;
	setenv("SOMVR_HIDE_HEAD", "bone+near", 1);
	bits[5] = hb_config(), on[5][0] = g_hb.bone, on[5][1] = g_hb.near;
	EXPECT(bits[0] == 1u << HK_ARM && on[0][0] && !on[0][1] && bits[1] == 0 && !on[1][0] && on[1][1] &&
		       bits[2] == 1u << HK_ARM && on[2][0] && on[2][1] && bits[3] == 0 && !on[3][0] && !on[3][1] && bits[4] == 0 &&
		       !on[4][0] && !on[4][1] && bits[5] == 0 && !on[5][0] && !on[5][1] &&
		       log_has("WARN SOMVR_HIDE_HEAD=bone+x is not bone, near, parts (joined by +)") &&
		       log_has("WARN SOMVR_HIDE_HEAD=bone+near needs SOMVR_BASE=head"),
	       "hb_config: bits %#x %#x %#x %#x %#x %#x", bits[0], bits[1], bits[2], bits[3], bits[4], bits[5]);
	g_rigcfg.base = SOMVR_BASE_HEAD;
	hb_config();
	hb_start();
	/* arm_start with the arms off: the bone path alone checks ARM_MODELOF and takes the tracker vtable */
	struct site_spec hk[HK_N];
	memset(hk, 0, sizeof hk);
	hk[HK_ARM].addr = (uintptr_t)&g_vttrk[3];
	g_arm.vt_trk = 0;
	arm_start(g_arm_sites, hk);
	EXPECT(g_hb.bone && g_hb.near && !g_arm.on && g_arm.vt_trk == (uintptr_t)g_vttrk && g_arm.ok_model &&
		       log_has("CONFIG hide_head=bone+near: bone=1 (in phase 2 (ARM_TRK_EVAL)") &&
		       log_has("near=1 (12 cm on the eye views while the base is the head)"),
	       "bone+near start: bone %d near %d arms %d vtable %#lx", g_hb.bone, g_hb.near, g_arm.on, (unsigned long)g_arm.vt_trk);
	setenv("SOMVR_HIDE_HEAD", "3,5", 1);
	g_hb.bone = g_hb.near = 0;

	/* 3. the collapse with the arms off: fake views, Talion standing at (100, 127.5, 300), GetNodeTransform reading his
	 * buffer (so the head base would see a moved Head) */
	g_views_fake = 1, g_layout = g_rigcfg.layout = SOMVR_LAYOUT_A;
	g_pl = &g_player;
	g_pobj.pos[0] = 100, g_pobj.pos[1] = 127.5f, g_pobj.pos[2] = 300;
	g_pobj.rot[0] = g_pobj.rot[1] = g_pobj.rot[2] = 0, g_pobj.rot[3] = 1;
	g_rm[0] = g_rm[1] = g_rm[2] = 0;
	g_cb_test_t = 8000;
	g_nodexf_node = &g_pnodes[28];
	atomic_fetch_add(&g_req_recenter, 1);
	aframe();
	aframe();
	float e_off = head_err();
	EXPECT(head_state(0) && e_off < 1e-3f, "bone off: pose untouched %d, eye base %.4f cm from X (x) Head", head_state(0),
	       e_off);
	g_hb.bone = 1;
	aframe(); /* phase 2 before the first H4 with bone on: as animated; that H4 arms the next phase 2 */
	int first_untouched = head_state(0);
	float emax = 0;
	int all = 1;
	for (int i = 0; i < 5; i++) {
		aframe();
		all &= head_state(1) && !memcmp(g_onodes, g_anim, sizeof g_anim);
		emax = fmaxf(emax, head_err());
	}
	EXPECT(first_untouched && all && emax < 1e-3f && g_hb.tree_ok && g_hb.tree.head == 28 && g_hb.tree.nsub == 57 &&
		       g_hb.tree.nrun == 1 && g_hb.tree.run[0][0] == 29 && g_hb.tree.run[0][1] == 85 &&
		       log_has("HEADBONE skeleton trk=") && log_has("Head 28 (in 24), 57 nodes below in 1 run: 29..85") &&
		       log_has("HEADBONE on t="),
	       "bone on: first %d collapsed %d, eye base off by %.4f cm, tree %d head %d nsub %d", first_untouched, all, emax,
	       g_hb.tree_ok, g_hb.tree.head, g_hb.tree.nsub);
	EXPECT(g_hb.n.checks >= 5 && g_hb.n.dmax < 1e-3f, "base check: %llu frames, max %.4f cm", (unsigned long long)g_hb.n.checks,
	       g_hb.n.dmax);
	/* walking (5 cm per update): collapsed every frame, the eye base on the moving uncollapsed Head */
	g_rm[2] = 5;
	for (int i = 0; i < 10; i++) {
		aframe();
		all &= head_state(1);
		emax = fmaxf(emax, head_err());
	}
	g_rm[2] = 0;
	EXPECT(all && emax < 1e-3f && g_pobj.pos[2] > 340, "walking: collapsed %d, eye base off by %.4f cm (z %.0f)", all, emax,
	       g_pobj.pos[2]);
	/* stereo stops (no views): that frame's phase 2 ran before its H4 (still collapsed), the next is as animated; back */
	g_views_fake = 0;
	atomic_store(&g_sh.xr.state, SOMVR_XRS_DEAD);
	aframe();
	int lag = head_state(1) && g_h4_calls == 1;
	aframe();
	int flat = head_state(0);
	g_views_fake = 1;
	aframe();
	int back0 = head_state(0);
	aframe();
	int back1 = head_state(1);
	EXPECT(lag && flat && back0 && back1 && log_has("HEADBONE off t=") && log_has("no stereo frame on the head base"),
	       "flat: lag %d flat %d back %d/%d", lag, flat, back0, back1);
	atomic_store(&g_sh.xr.state, SOMVR_XRS_RUNNING);
	/* a frame on the camera base (GetNodeTransform fails): the next phase 2 as animated, then collapsed again */
	g_nodexf_fail = 1;
	aframe();
	g_nodexf_fail = 0;
	aframe();
	int cam = head_state(0);
	aframe();
	EXPECT(cam && head_state(1), "camera base: untouched %d, back %d", cam, head_state(1));
	/* another thread's phase 2: as animated */
	uint64_t o0 = g_hb.n.other;
	pthread_t th;
	pthread_create(&th, NULL, trk_thread, &g_ptrk);
	pthread_join(th, NULL);
	EXPECT(g_hb.n.other == o0 + 1 && head_state(0), "another thread: other %llu", (unsigned long long)(g_hb.n.other - o0));
	/* a skeleton without Head: no collapse, said once; the good one back: collapsed */
	memcpy(g_hashes2, g_hashes, sizeof g_hashes2);
	g_hashes2[28] = 0x1234;
	g_skel2 = (st_skel_t){ g_ents, RUN3_N, 0, { 0 }, g_hashes2 };
	g_md.skel = &g_skel2;
	aframe();
	aframe();
	int noh = head_state(0) && !g_hb.tree_ok && g_hb.n.notree >= 2;
	g_md.skel = &g_skel;
	aframe();
	EXPECT(noh && head_state(1) && log_has("no Head (0x72598328) in the skeleton (no collapse for this skeleton)"),
	       "no Head: untouched %d, back %d", noh, head_state(1));

	/* 4. with the arms on (xr views, the layer's hands): both arms on their grips and the head collapsed in one phase 2 */
	g_arm.on = 1;
	g_views_fake = 0, g_rt_result = XR_SUCCESS;
	g_xrt = g_vst.last_time + 1000000000;
	layer_up();
	for (int i = 0; i < 25; i++)
		aframe();
	float dg[2], er0 = arm_err(SA_L, &dg[0]), er1 = arm_err(SA_R, &dg[1]);
	EXPECT(g_arm.weight[0] == 1 && g_arm.weight[1] == 1 && er0 < 0.2f && er1 < 0.2f && head_state(1) && head_err() < 1e-3f,
	       "arms + head: weights %.2f %.2f, weapon nodes %.3f / %.3f cm, head %d, eye base %.4f cm", g_arm.weight[0],
	       g_arm.weight[1], er0, er1, head_state(1), head_err());
	g_layer_live = 0;
	g_arm.on = 0;

	/* 5. near: 12 cm on both eye views while the base is the head; the game's on the camera base; SOMVR_NEAR_CM wins */
	g_views_fake = 1;
	g_hb.bone = 0, g_hb.near = 1;
	uint64_t ne0 = g_hb.near_eyes;
	aframe();
	int n12 = g_h4_view[0].near_z == HB_NEAR_CM && g_h4_view[1].near_z == HB_NEAR_CM && g_hb.near_eyes == ne0 + 2;
	g_nodexf_fail = 1;
	aframe();
	int ncam = g_h4_view[0].near_z == 7.0f && g_h4_view[1].near_z == 7.0f;
	g_nodexf_fail = 0;
	g_near_cm = 5;
	aframe();
	int n5 = g_h4_view[0].near_z == 5.0f && g_h4_view[1].near_z == 5.0f;
	g_near_cm = 0, g_hb.near = 0;
	aframe();
	int noff = g_h4_view[0].near_z == 7.0f && g_h4_view[1].near_z == 7.0f;
	EXPECT(n12 && ncam && n5 && noff && head_state(0), "near: 12 cm %d, camera base 7 %d, SOMVR_NEAR_CM 5 %d, off 7 %d", n12,
	       ncam, n5, noff);
	/* 6. bone off again: the pose as animated */
	aframe();
	EXPECT(head_state(0), "bone off edited the pose");
	printf("hidehead bone|near|bone+near parsed; bone asks for ARM_TRK_EVAL without SOMVR_ARMS, both need the head base; "
	       "Head's 57 nodes collapsed in phase 2 (Head and the rest bit for bit), the eye base on the uncollapsed Head "
	       "(%.4f cm) standing and walking; a flat frame's lag of one, the camera base, another thread, no Head: as "
	       "animated; with the arms in one phase 2; near 12 cm / the game's / SOMVR_NEAR_CM\n", emax);
	g_nodexf_node = NULL;
	g_hb.bone = g_hb.near = 0, g_hb.live = 0;
	g_arm.on = arm_on0, g_views_fake = fake0, g_layout = layout0;
	g_rig = rig0, g_rigcfg = cfg0;
	memcpy(g_pobj.pos, pos0, sizeof pos0), memcpy(g_pobj.rot, rot0, sizeof rot0);
	g_pl = NULL;
	g_cb_test_t = -1;
}

/* ---- the pad glyphs (SOMVR_INPUT=1): stand-in CGameUI / CMessageBoxUI in the stand-in interface manager (+0x2708,
 * +0x2a50), their holders and CUIViews (slot 37 "texture loaded", slot 40 "clear"), the UIs' update as the game runs it
 * (CBaseUI::vf_05: while ui+0x10 is 0 and all 22 pad textures are loaded, register every record), the Feral controller
 * manager (guard, vector), the 22 names ------------------------------------------------------------------------- */
static void *volatile g_vtview[44], *volatile g_vtgameui[8], *volatile g_vtmsgbox[8];
typedef struct { uintptr_t vt; char pad[0xc8]; uint32_t count; } st_view_t;
typedef struct { char pad[0x50]; st_view_t *view; } st_holder_t;
_Static_assert(offsetof(st_view_t, count) == GL_VIEW_COUNT && offsetof(st_holder_t, view) == GL_HOLDER_VIEW, "glyph layouts");
static st_view_t g_gview[4];
static st_holder_t g_gholder[2];
static uintptr_t g_gnames[GL_NAMES + 2];
static char g_gname_buf[GL_NAMES][48];
static unsigned char g_gmgr_guard = 1;
static struct { char pad[GL_MGR_VEC]; uintptr_t vb, ve; } g_gmgr;
static uintptr_t g_gpads[2];
static int g_gl_loaded = GL_NAMES, g_gl_has, g_gl_clears, g_gl_kbm = 40;
__attribute__((noipa)) uint64_t st_view_has(void *view, const char *name)
{
	(void)view;
	g_gl_has++;
	for (int i = 0; i < GL_NAMES; i++)
		if ((uintptr_t)name == g_gnames[i])
			return i < g_gl_loaded;
	return 0;
}
__attribute__((noipa)) void st_view_clear(void *view) { g_gl_clears++, ((st_view_t *)view)->count = 0; }
static void gl_ui_update(char *ui) /* CBaseUI::vf_05's re-arm branch: done 0 -> all 22 loaded -> every record, done 1 */
{
	st_holder_t *ho = *(st_holder_t **)(ui + GL_UI_HOLDER);
	st_view_t *v = ho ? ho->view : NULL;
	if (!v || ui[GL_UI_DONE]) /* not made yet (the game would make it now), or registered */
		return;
	int all = 1;
	for (int i = 0; i < GL_NAMES; i++)
		all &= (int)st_view_has(v, (const char *)g_gnames[i]);
	if (all)
		ui[GL_UI_DONE] = 1, v->count += (uint32_t)(g_gl_kbm + GL_NAMES);
}
__attribute__((noipa)) uint64_t t_gameui_update(void *ui) { gl_ui_update(ui); return 1; }
__attribute__((noipa)) uint64_t t_msgbox_update(void *ui) { gl_ui_update(ui); return 1; }
__attribute__((noipa)) uint64_t c_uis_msgbox(void *im) { return t_msgbox_update((char *)im + GL_UI_MSGBOX) + 1; }
__attribute__((noipa)) uint64_t c_uis_gameui(void *im) { return t_gameui_update((char *)im + GL_UI_GAME) + 2; }
__attribute__((noipa)) int st_gl_rearm(void) { return 3; }
__attribute__((noipa)) int st_gl_nopad(void) { return 4; }
__attribute__((noipa)) int st_gl_padmgr(void) { return 5; }
__attribute__((noipa)) int st_gl_padcount(void) { return 6; }
/* a made view: KBM pairs only (the game's first pass without a controller), done 1 */
static void gl_view(int k, st_view_t *v)
{
	char *ui = g_im + (k ? GL_UI_MSGBOX : GL_UI_GAME);
	uintptr_t vt = (uintptr_t)(k ? g_vtmsgbox : g_vtgameui), hp = (uintptr_t)&g_gholder[k];
	memcpy(ui, &vt, 8), memcpy(ui + GL_UI_HOLDER, &hp, 8);
	v->vt = (uintptr_t)g_vtview, v->count = (uint32_t)g_gl_kbm;
	g_gholder[k].view = v;
	ui[GL_UI_DONE] = 1;
}
static void gl_world(struct site_spec *gls)
{
	vslot_site(&g_vtview[37], (void *)st_view_has, &gls[GL_VIEW_HAS], "GL_VIEW_HAS", 65);
	vslot_site(&g_vtview[40], (void *)st_view_clear, &gls[GL_VIEW_CLEAR], "GL_VIEW_CLEAR", 66);
	vslot_site(&g_vtgameui[5], (void *)t_gameui_update, &gls[GL_GAMEUI_UPDATE], "GL_GAMEUI_UPDATE", 67);
	vslot_site(&g_vtmsgbox[5], (void *)t_msgbox_update, &gls[GL_MSGBOX_UPDATE], "GL_MSGBOX_UPDATE", 68);
	if (call_site((void *)c_uis_msgbox, (void *)t_msgbox_update, &gls[GL_UIS_MSGBOX], "GL_UIS_MSGBOX", 69) ||
	    call_site((void *)c_uis_gameui, (void *)t_gameui_update, &gls[GL_UIS_GAMEUI], "GL_UIS_GAMEUI", 70))
		printf("FAIL: no call site in the stand-in interface manager update\n"), fails++;
	entry_site((void *)st_gl_rearm, &gls[GL_REARM], "GL_REARM", 71);
	entry_site((void *)st_gl_nopad, &gls[GL_NOPAD_SKIP], "GL_NOPAD_SKIP", 72);
	entry_site((void *)st_gl_padmgr, &gls[GL_PADMGR], "GL_PADMGR", 73);
	entry_site((void *)st_gl_padcount, &gls[GL_PADCOUNT], "GL_PADCOUNT", 74);
	for (int i = 0; i < GL_NAMES; i++) {
		snprintf(g_gname_buf[i], sizeof g_gname_buf[i], "interface\\textures\\buttons\\pad%02d.tex", i);
		g_gnames[i] = (uintptr_t)g_gname_buf[i];
	}
	g_gmgr.vb = g_gmgr.ve = (uintptr_t)g_gpads; /* the manager made, no controller */
	g_glg.names = (uintptr_t)g_gnames, g_glg.mgr_guard = (uintptr_t)&g_gmgr_guard, g_glg.mgr = (uintptr_t)&g_gmgr;
	g_gl_sites = gls;
}
/* one game frame with the glyph UIs: the input tick (in_glyphs at most twice a second), then the UIs' update */
static void gl_frame(double dt)
{
	g_cb_test_t += dt;
	((t_vf04_fn)g_vtin[4])(&g_cwbi);
	EXPECT(c_uis_msgbox(g_im) == 2 && c_uis_gameui(g_im) == 3, "the UIs' update return values");
}
static void glyph_tests(void)
{
	EXPECT(g_gl.on && log_has("GLYPHS on: 10 sites ok, 22 pad textures (interface\\textures\\buttons\\pad00.tex .. "
				  "interface\\textures\\buttons\\pad21.tex)") && g_gl.arms == 0,
	       "glyphs at start: on %d arms %llu", g_gl.on, (unsigned long long)g_gl.arms);
	g_profile[0x238] = 1, g_allow_kbm = 0; /* pad mode as forced: in_padmode calls nothing */
	int im0 = g_inmode_calls;
	g_cb_test_t = 9000;
	gl_frame(0.6); /* no UI made yet: nothing */
	EXPECT(g_gl_clears == 0 && g_gl.arms == 0, "no UI: re-armed");
	/* the boot's views: KBM pairs only. The next tick re-arms both (cleared, 22 vf_37, done 0); the game registers */
	gl_view(0, &g_gview[0]), gl_view(1, &g_gview[1]);
	gl_frame(0.6);
	EXPECT(g_gl_clears == 2 && g_gview[0].count == (uint32_t)(g_gl_kbm + GL_NAMES) &&
		       g_gview[1].count == (uint32_t)(g_gl_kbm + GL_NAMES) && g_im[GL_UI_GAME + GL_UI_DONE] == 1 &&
		       g_im[GL_UI_MSGBOX + GL_UI_DONE] == 1 && g_gl.arms == 2 &&
		       log_has("GLYPHS CGameUI view=") && log_has("re-armed (no Feral controller): 40 pairs cleared, the pad "
							      "textures loaded 22 of 22"),
	       "re-arm: clears %d counts %u/%u done %d/%d arms %llu", g_gl_clears, g_gview[0].count, g_gview[1].count,
	       g_im[GL_UI_GAME + GL_UI_DONE], g_im[GL_UI_MSGBOX + GL_UI_DONE], (unsigned long long)g_gl.arms);
	gl_frame(0.6); /* seen registered: the prompts set again at the following tick, once for both */
	EXPECT(g_gl.done == 2 && g_inmode_calls == im0 && log_has("registered again after") &&
		       log_has(": 62 pairs (were 40 without the pad ones)"),
	       "registered: done %llu, input-mode calls %d", (unsigned long long)g_gl.done, g_inmode_calls - im0);
	gl_frame(0.1);
	EXPECT(g_inmode_calls == im0 + 1 && g_inmode_last == 1 && g_gl.refreshes == 1, "prompts set again: %d calls",
	       g_inmode_calls - im0);
	for (int i = 0; i < 20; i++)
		gl_frame(0.6);
	EXPECT(g_gl_clears == 2 && g_inmode_calls == im0 + 1 && g_gview[0].count == 62, "re-armed again: clears %d calls %d",
	       g_gl_clears, g_inmode_calls - im0);
	/* a load makes a new CGameUI view: re-armed once; its textures not all loaded yet: the game waits, we log */
	g_gl_loaded = 10;
	gl_view(0, &g_gview[2]);
	gl_frame(0.6);
	EXPECT(g_gl_clears == 3 && g_gview[2].count == 0 && g_im[GL_UI_GAME + GL_UI_DONE] == 0, "new view: clears %d count %u",
	       g_gl_clears, g_gview[2].count);
	for (int i = 0; i < 12; i++)
		gl_frame(0.6);
	EXPECT(g_gl_clears == 3 && g_im[GL_UI_GAME + GL_UI_DONE] == 0 && g_gl.waits >= 1 &&
		       log_has("still waiting: the pad textures loaded 10 of 22"),
	       "waiting: clears %d waits %llu", g_gl_clears, (unsigned long long)g_gl.waits);
	g_gl_loaded = GL_NAMES; /* the bundle finished */
	gl_frame(0.6), gl_frame(0.6), gl_frame(0.1);
	EXPECT(g_gview[2].count == 62 && g_gl.done == 3 && g_inmode_calls == im0 + 2, "after the load: count %u calls %d",
	       g_gview[2].count, g_inmode_calls - im0);
	/* a real pad: the game's own set; a new view is left alone */
	g_gmgr.ve = (uintptr_t)&g_gpads[1];
	gl_view(1, &g_gview[3]);
	for (int i = 0; i < 4; i++)
		gl_frame(0.6);
	EXPECT(g_gl_clears == 3 && g_gview[3].count == 40 && g_gl.pads_seen >= 3, "a real pad: clears %d pads %llu",
	       g_gl_clears, (unsigned long long)g_gl.pads_seen);
	g_gmgr.ve = g_gmgr.vb;
	g_gmgr_guard = 0; /* the manager not made yet: no controller either */
	gl_frame(0.6);
	EXPECT(g_gl_clears == 4 && g_gview[3].count == 62, "manager not made: clears %d count %u", g_gl_clears, g_gview[3].count);
	g_gmgr_guard = 1;
	/* another object at the UI's place (a wrong vtable), or no view yet: skipped */
	uintptr_t bad = 0x4444;
	memcpy(g_im + GL_UI_GAME, &bad, 8);
	g_gholder[1].view = NULL;
	gl_view(0, &g_gview[0]), memcpy(g_im + GL_UI_GAME, &bad, 8);
	for (int i = 0; i < 4; i++)
		gl_frame(0.6);
	EXPECT(g_gl_clears == 4, "a wrong UI vtable or no view: re-armed (%d)", g_gl_clears);
	/* the switch and the sites at start */
	struct site_spec gls2[GL_N];
	memcpy(gls2, g_gl_sites, sizeof gls2);
	const struct site_spec *keep = g_gl_sites;
	setenv("SOMVR_PADGLYPHS", "0", 1);
	EXPECT(!gl_start() && log_has("GLYPHS off (SOMVR_PADGLYPHS=0)"), "SOMVR_PADGLYPHS=0: on");
	unsetenv("SOMVR_PADGLYPHS");
	gls2[GL_VIEW_CLEAR].target = (uintptr_t)st_view_has;
	g_gl_sites = gls2;
	EXPECT(!gl_start() && log_has("GLYPHS site GL_VIEW_CLEAR MISMATCH") && log_has("WARN GLYPHS: 1 of the read-only"),
	       "a mismatched site: on");
	g_gl_sites = keep;
	g_gnames[5] = (uintptr_t)"textures\\other.tex";
	EXPECT(!gl_start() && log_has("GLYPHS pad texture name 5"), "a bad name: on");
	g_gnames[5] = (uintptr_t)g_gname_buf[5];
	EXPECT(gl_start(), "the sites again: off");
	memset(g_im + GL_UI_GAME, 0, 0x18), memset(g_im + GL_UI_MSGBOX, 0, 0x18); /* no glyph UIs for the tests after */
	g_cb_test_t = -1;
	printf("glyphs: both boot views re-armed (cleared, 22 x vf_37, done 0), the game's 62 pairs, prompts set again once; "
	       "a load's new view, textures late (waiting logged), a real pad and an unmade manager, wrong objects; "
	       "SOMVR_PADGLYPHS=0, a bad site, a bad name\n");
}

/* ---- SOMVR_EYE_SIZE (switched on at start with 1832x1920; mode and layout toggled white box) ---------------------- */
static void res_mode_set(uint32_t sw, uint32_t bbw, uint32_t bbh, int lb)
{
	memset(&g_desc3d, 0, sizeof g_desc3d);
	EXPECT(c_desc(NULL, sw, bbw, bbh) == 1, "FUN_0073fce0 return value");
	EXPECT(c_rect(lb ? 1 : 0) == 1 && c_rect_init(lb ? 1 : 0) == 2, "FUN_00d28b50 return value");
}
/* SOMVR_EYE_SIZE=xr with SOMVR_OVERSCAN (re-xr-pipeline.md 6.5, 7.2): an xr size (the runtime's recommended one or the
 * fallback after the comma) x (1 + s), so the widened fov keeps the runtime's pixel density; a plain WxH stays as given.
 * Then xr-views frames in layout B at that size: the 3D surface, H8's eye textures, the published eye info (what the
 * layer sizes its 2W x H swapchain from and recognises the images by), H3's tangents and the record's fov (what the
 * layer submits as the projection fov, test_layer `predict`) all agree: the runtime's pixels per tangent unit, the
 * widened fov's aspect. Called by res_tests with the xr session up and layout B */
static void eyesize_overscan_tests(void)
{
	float os0 = g_overscan;
	d3d_tex_desc surf0 = g_surf.desc;
	int mode0 = g_eye_sz.mode;
	g_overscan = 0.1f;
	g_eye_sz.mode = 1, g_eye_sz.w = 1832, g_eye_sz.h = 1920;
	res_mode_set(1920, 1920, 1080, 0);
	EXPECT(g_desc3d.w == 1832 && g_desc3d.h == 1920, "WxH with overscan 0.1: desc %ux%u (want 1832x1920 as given)",
	       g_desc3d.w, g_desc3d.h);
	g_eye_sz.mode = 2;
	atomic_store(&g_sh.xr.state, SOMVR_XRS_NONE);
	res_mode_set(1920, 1920, 1080, 0);
	EXPECT(g_desc3d.w == 2015 && g_desc3d.h == 2112, "xr,1832x1920 + overscan 0.1 before the session: desc %ux%u (want "
	       "2015x2112)", g_desc3d.w, g_desc3d.h);
	g_sh.xr.view_w = 1832, g_sh.xr.view_h = 2015; /* headset look 1's recommended size -> 2015x2217 (7.2) */
	runtime_up();
	res_mode_set(1920, 1920, 1080, 0);
	EXPECT(g_desc3d.w == 2015 && g_desc3d.h == 2217, "xr 1832x2015 + overscan 0.1: desc %ux%u (want 2015x2217)",
	       g_desc3d.w, g_desc3d.h);
	EXPECT(log_has("the game's 1920x1080 -> 2015x2217 (the runtime's recommended eye size x 1.100 for SOMVR_OVERSCAN "
		       "0.100); desc now 2015x2217"),
	       "eye size xr + overscan: no RES line with the scale");
	g_overscan = 0.9f; /* white box, above SOMVR_OVERSCAN_MAX: capped at 0.5 like sv_fov_overscan */
	res_mode_set(1920, 1920, 1080, 0);
	EXPECT(g_desc3d.w == 2748 && g_desc3d.h == 3023, "xr + overscan 0.9 (capped 0.5): desc %ux%u", g_desc3d.w, g_desc3d.h);
	g_overscan = 0;
	res_mode_set(1920, 1920, 1080, 0);
	EXPECT(g_desc3d.w == 1832 && g_desc3d.h == 2015, "xr, overscan 0: desc %ux%u (the runtime's)", g_desc3d.w, g_desc3d.h);
	/* the agreement, per s: the eye images are the surface's size, the fov is widened by the same 1 + s */
	static const float OS[] = { 0.1f, 0.25f };
	g_sh.xr.view_w = 1600, g_sh.xr.view_h = 1680;
	g_views_fake = 0;
	g_layout = g_rigcfg.layout = SOMVR_LAYOUT_B;
	for (size_t i = 0; i < sizeof OS / sizeof OS[0]; i++) {
		g_overscan = OS[i];
		res_mode_set(1920, 1920, 1080, 0);
		uint32_t ww = (uint32_t)(1600 * (1.0 + (double)OS[i]) + 0.5), wh = (uint32_t)(1680 * (1.0 + (double)OS[i]) + 0.5);
		EXPECT(g_desc3d.w == ww && g_desc3d.h == wh, "xr 1600x1680 + overscan %g: desc %ux%u (want %ux%u)", OS[i],
		       g_desc3d.w, g_desc3d.h, ww, wh);
		g_surf.desc.w = g_desc3d.w, g_surf.desc.h = g_desc3d.h; /* FUN_007414b0 makes the surface from that desc */
		frame();
		check_eyes("eye size xr + overscan", 3, 2); /* eye images and the published info = the surface */
		somvr_rec rec;
		EXPECT(g_h4_calls == 2 && somvr_rec_read(&g_sh, g_frame_no, &rec) && rec.layout == SOMVR_LAYOUT_B &&
			       rec.applied == 3 && !(rec.flags & SOMVR_REC_FAKE),
		       "eye size xr + overscan %g: calls %d, no complete xr record", OS[i], g_h4_calls);
		EXPECT(g_et.pub.w == ww && g_et.pub.h == wh && atomic_load(&g_sh.eyes.expect_w) == ww &&
			       atomic_load(&g_sh.eyes.expect_h) == wh,
		       "overscan %g: eye textures %ux%u, layer expects %ux%u (want %ux%u)", OS[i], g_et.pub.w, g_et.pub.h,
		       atomic_load(&g_sh.eyes.expect_w), atomic_load(&g_sh.eyes.expect_h), ww, wh);
		for (int e = 0; e < 2; e++) {
			somvr_fovf rt, wf;
			memcpy(&rt, &EYE_FOV[e], sizeof rt);
			wf = sv_fov_overscan(&rt, OS[i]); /* what eye0_setup renders with and the record carries */
			EXPECT(!memcmp(&rec.eye[e].fov, &wf, sizeof wf), "overscan %g eye %d: record fov %g..%g %g..%g, want the "
			       "widened %g..%g %g..%g", OS[i], e, rec.eye[e].fov.l, rec.eye[e].fov.r, rec.eye[e].fov.d,
			       rec.eye[e].fov.u, wf.l, wf.r, wf.d, wf.u);
			sv_proj p0 = sv_proj_from_fov(&rt), p1 = sv_proj_from_fov(&rec.eye[e].fov);
			/* pixels per tangent unit: the runtime's recommended image over its fov vs our image over the submitted fov */
			double dx0 = 1600 / (2.0 * p0.tx), dy0 = 1680 / (2.0 * p0.ty);
			double dx1 = g_et.pub.w / (2.0 * p1.tx), dy1 = g_et.pub.h / (2.0 * p1.ty);
			EXPECT(fabs(dx1 / dx0 - 1) < 1e-3 && fabs(dy1 / dy0 - 1) < 1e-3 && p1.tx == g_h4_tan[e][0] &&
				       p1.ty == g_h4_tan[e][1] && p1.cx == g_h4_co[e][0] && p1.cy == g_h4_co[e][1] &&
				       NEAR(p1.tx, (1 + OS[i]) * p0.tx, 2e-5) && NEAR(p1.ty, (1 + OS[i]) * p0.ty, 2e-5) &&
				       NEAR(p1.cx, p0.cx, 2e-5) && g_h4_rect[e][0] == 0 && g_h4_rect[e][2] == 1,
			       "overscan %g eye %d: density %.2f,%.2f px/tan (runtime %.2f,%.2f), H3 %g,%g, record %g,%g, rect %g..%g",
			       OS[i], e, dx1, dy1, dx0, dy0, g_h4_tan[e][0], g_h4_tan[e][1], p1.tx, p1.ty, g_h4_rect[e][0],
			       g_h4_rect[e][2]);
		}
	}
	g_overscan = os0, g_surf.desc = surf0, g_eye_sz.mode = mode0;
	g_views_fake = 1;
	printf("eyesize xr + overscan: x 1.1 -> 2015x2112 (fallback), 2015x2217 (look 1), capped x 1.5, plain WxH kept; "
	       "1760x1848 / 2000x2100 frames: surface = eye textures = layer's size, H3 = record fov, density kept\n");
}

static void res_tests(void)
{
	uint32_t w = 0, h = 0;
	EXPECT(!parse_size("1832x1920", &w, &h) && w == 1832 && h == 1920 && !parse_size("2064X2208", &w, &h) && w == 2064 &&
		       parse_size("10x10", &w, &h) && parse_size("1832x", &w, &h) && parse_size("1832", &w, &h) &&
		       parse_size("x1920", &w, &h) && parse_size("1832x1920y", &w, &h) && parse_size("20000x100", &w, &h),
	       "parse_size");
	EXPECT(g_eye_sz.mode == 1 && g_eye_sz.w == 1832 && g_eye_sz.h == 1920 && g_ex[EX_RES3D].installed && g_ex[EX_LB].installed &&
		       g_ex[EX_LB_INIT].installed,
	       "eye size config: mode %d %ux%u", g_eye_sz.mode, g_eye_sz.w, g_eye_sz.h);
	EXPECT(log_has("WARN SOMVR_EYE_SIZE=1832x1920 applies in layout B only") && log_has("CONFIG eye_size=1832x1920"),
	       "eye size: no layout warning / CONFIG line at start (layout A)");
	int lay = g_layout;
	/* layout A: everything passes through */
	g_layout = SOMVR_LAYOUT_A;
	res_mode_set(1920, 1920, 1080, 1);
	EXPECT(g_desc3d.w == 1920 && g_desc3d.h == 1080 && g_rect_lb_seen == 1 && g_ex[EX_RES3D].mods == 0,
	       "layout A: desc %ux%u letterbox seen %llu", g_desc3d.w, g_desc3d.h, (unsigned long long)g_rect_lb_seen);
	/* layout B, the switch off (white box): what the doc predicts without the hook - 1832 x 1031 and, letterboxed, a band */
	g_layout = SOMVR_LAYOUT_B;
	g_eye_sz.mode = 0;
	res_mode_set(1832, 1920, 1080, 0);
	EXPECT(g_desc3d.w == 1832 && g_desc3d.h == 1031 && g_rect3d[3] == 1031, "control: desc %ux%u", g_desc3d.w, g_desc3d.h);
	g_eye_sz.mode = 1;
	/* layout B: the 3D surface 1832 x 1920, the rect never letterboxed */
	res_mode_set(1832, 1920, 1080, 1);
	EXPECT(g_desc3d.w == 1832 && g_desc3d.h == 1920 && g_desc3d.format == 28 && g_rect_lb_seen == 0 && g_rect3d[0] == 0 &&
		       g_rect3d[1] == 0 && g_rect3d[2] == 1832 && g_rect3d[3] == 1920,
	       "layout B: desc %ux%u format %u letterbox seen %llu rect %d,%d,%d,%d", g_desc3d.w, g_desc3d.h, g_desc3d.format,
	       (unsigned long long)g_rect_lb_seen, g_rect3d[0], g_rect3d[1], g_rect3d[2], g_rect3d[3]);
	EXPECT(g_ex[EX_LB].mods == 1 && g_ex[EX_LB_INIT].mods == 1, "letterbox forced off %llu+%llu",
	       (unsigned long long)g_ex[EX_LB].mods, (unsigned long long)g_ex[EX_LB_INIT].mods);
	/* the game asks for 1920 wide: the eye size wins (both numbers) */
	res_mode_set(1920, 1920, 1080, 0);
	EXPECT(g_desc3d.w == 1832 && g_desc3d.h == 1920, "layout B from 1920: desc %ux%u", g_desc3d.w, g_desc3d.h);
	EXPECT(log_has("the game's 1832x1031 -> 1832x1920 (SOMVR_EYE_SIZE); desc now 1832x1920 format 28") &&
		       log_has("UseWidescreen=1 -> 0 rect=0,0,1832,1920 (letterbox off: SOMVR_EYE_SIZE)"),
	       "eye size: no RES lines");
	/* the eye textures follow the surface made from that desc (FUN_007414b0) */
	g_surf.desc.w = g_desc3d.w, g_surf.desc.h = g_desc3d.h;
	g_rigcfg.layout = SOMVR_LAYOUT_B;
	g_views_fake = 1;
	frame();
	check_eyes("eye size 1832x1920", 3, 2);
	EXPECT(g_et.pub.w == 1832 && g_et.pub.h == 1920 && atomic_load(&g_sh.eyes.expect_h) == 1920 && g_h4_rect[1][2] == 1 &&
		       g_h4_rect[1][3] == 1,
	       "eye textures %ux%u, H2 rect %g,%g", g_et.pub.w, g_et.pub.h, g_h4_rect[1][2], g_h4_rect[1][3]);
	/* xr: the runtime's recommended size once published; before that the fallback, else the game's */
	g_eye_sz.mode = 2, g_eye_sz.w = g_eye_sz.h = 0;
	atomic_store(&g_sh.xr.state, SOMVR_XRS_NONE);
	res_mode_set(1832, 1920, 1080, 0);
	EXPECT(g_desc3d.w == 1832 && g_desc3d.h == 1031, "xr, no session: desc %ux%u (want the game's)", g_desc3d.w, g_desc3d.h);
	g_sh.xr.view_w = 1600, g_sh.xr.view_h = 1680;
	runtime_up();
	res_mode_set(1832, 1920, 1080, 1);
	EXPECT(g_desc3d.w == 1600 && g_desc3d.h == 1680 && g_rect_lb_seen == 0, "xr: desc %ux%u", g_desc3d.w, g_desc3d.h);
	g_eye_sz.w = 1832, g_eye_sz.h = 1920;
	atomic_store(&g_sh.xr.state, SOMVR_XRS_NONE);
	res_mode_set(1920, 1920, 1080, 0);
	EXPECT(g_desc3d.w == 1832 && g_desc3d.h == 1920, "xr,1832x1920 before the session: desc %ux%u", g_desc3d.w, g_desc3d.h);
	g_sh.xr.view_w = 20000; /* nonsense from the runtime: the fallback */
	runtime_up();
	res_mode_set(1920, 1920, 1080, 0);
	EXPECT(g_desc3d.w == 1832 && g_desc3d.h == 1920, "xr with a bad runtime size: desc %ux%u", g_desc3d.w, g_desc3d.h);
	EXPECT(log_has("(the runtime's recommended eye size)") && log_has("(the fallback: the runtime's size is not published yet)"),
	       "eye size xr: no RES lines");
	eyesize_overscan_tests();
	g_sh.xr.view_w = g_sh.xr.view_h = 0;
	g_eye_sz.mode = 1;
	g_layout = lay, g_rigcfg.layout = lay;
	g_views_fake = 0;
	printf("eyesize layout B: 1832x1031 -> 1832x1920, rect not letterboxed, eye textures 1832x1920; A passes; xr 1600x1680, "
	       "fallback, game's\n");
}

/* ---- SOMVR_EYE_SIZE's runtime switch (re-xr-pipeline.md 9): a stand-in console whose SetResolution runs the stand-in
 * mode set (FUN_00741980's 3D desc call through RES_3D_DESC, the rect, the 3D surface made from the desc), the input
 * tick through IN_TICK's slot, the test clock --------------------------------------------------------------------- */
static void *volatile g_vteng[40]; /* CWBEngine's vtable: 32 = RunConsoleString */
static struct { uintptr_t vt; } g_eng;
static void *g_eng_ptr; /* the IWBEngine* holder (0x466afa8 in the game) */
static int g_rs_calls, g_rs_mode; /* the mode set: 0 inside the call, 1 later (es_late_mode_set), 2 never */
static uint32_t g_rs_pend_w;
static char g_rs_last[128];
static void es_game_mode_set(uint32_t sw) /* ScreenWidth sw, the window 1920x1080: the 3D desc, rect, surface */
{
	res_mode_set(sw, 1920, 1080, 0);
	g_surf.desc.w = g_desc3d.w, g_surf.desc.h = g_desc3d.h; /* FUN_007414b0 makes the 3D surface from that desc */
}
static void (*g_rs_hook)(const char *s); /* SOMVR_PRACTICE's tests: Teleport, AdvanceTimeOfDay, ChangeWeather */
__attribute__((noipa)) int st_runstring(void *eng, const char *s)
{
	unsigned w = 0, h = 0;
	g_rs_calls++;
	snprintf(g_rs_last, sizeof g_rs_last, "%s", s);
	EXPECT(eng == (void *)&g_eng, "RunConsoleString this %p", eng);
	if (g_rs_hook)
		g_rs_hook(s);
	if (sscanf(s, "SetResolution %u %u", &w, &h) != 2)
		return 0;
	if (g_rs_mode == 1)
		g_rs_pend_w = w;
	else if (g_rs_mode == 0)
		es_game_mode_set(w);
	return 0;
}
static void es_tick_at(double t) /* the game thread's input tick (CWBInput::vf_04 through the IN_TICK slot) at t */
{
	g_cb_test_t = t;
	((t_vf04_fn)g_vtin[4])(&g_cwbi);
}
static void es_reset(void)
{
	g_es.failed = g_es.pending = g_es.tries = g_es.held_out = 0, g_es.tid = 0;
	g_es.last_want = g_es.held_since = g_es.next_try = 0, g_es.last_tick = 0;
	g_es.held = g_es.switches[0] = g_es.switches[1] = g_es.fails = 0;
	atomic_store(&g_es.target, 0);
}
/* a world frame at t: the tick, then H4 (the player loop); 2 = stereo, 1 = flat */
static int es_world(double t)
{
	es_tick_at(t);
	return frame();
}
static void es_tests(void)
{
	int lay = g_layout, fake = g_views_fake, inp = g_inp.on, mode = g_eye_sz.mode, sw0 = g_es.sw, hud = g_hudq.on;
	uint32_t ew = g_eye_sz.w, eh = g_eye_sz.h;
	d3d_tex_desc surf0 = g_surf.desc;
	double hold0 = g_es.hold_s;
	/* SOMVR_EYE_SIZE_HOLD_MS and the tick: es_config at start (white box) */
	g_layout = SOMVR_LAYOUT_B;
	setenv("SOMVR_EYE_SIZE_HOLD_MS", "always", 1);
	EXPECT(es_config() == 0 && g_es.sw == 0, "HOLD_MS=always: switch %d", g_es.sw);
	setenv("SOMVR_EYE_SIZE_HOLD_MS", "500", 1);
	EXPECT(es_config() == 1u << HK_IN_TICK && g_es.sw == 1 && NEAR(g_es.hold_s, 0.5, 1e-9), "HOLD_MS=500: %d %g",
	       g_es.sw, g_es.hold_s);
	setenv("SOMVR_EYE_SIZE_HOLD_MS", "2s", 1);
	EXPECT(es_config() && NEAR(g_es.hold_s, 0.5, 1e-9) && log_has("WARN SOMVR_EYE_SIZE_HOLD_MS=2s is not 0..600000"),
	       "HOLD_MS=2s: hold %g", g_es.hold_s);
	unsetenv("SOMVR_EYE_SIZE_HOLD_MS");
	g_layout = SOMVR_LAYOUT_A;
	EXPECT(es_config() == 0, "layout A: the tick asked for");
	g_layout = g_rigcfg.layout = SOMVR_LAYOUT_B;
	g_es.hold_s = 2.0, g_es.sw = 1;
	es_start();
	EXPECT(g_es.sw == 1 && log_has("ESIZE site ES_RUNSTRING ok"), "es_start: switch %d", g_es.sw);
	g_views_fake = 1, g_inp.on = 0, g_hudq.on = 0;
	g_eye_sz.mode = 1, g_eye_sz.w = 1832, g_eye_sz.h = 1920;
	g_trig_path[0] = 0;
	es_reset();
	double t = 1000;
	g_cb_test_t = t;
	/* the boot's mode set: the game's size (no stereo yet), recorded as the flat size */
	es_game_mode_set(1920);
	EXPECT(g_desc3d.w == 1920 && g_desc3d.h == 1080 && atomic_load(&g_es.cur) == 0 && atomic_load(&g_es.flat_w) == 1920 &&
		       atomic_load(&g_es.flat_h) == 1080 && log_has("the game's 1920x1080 -> 1920x1080 (the game's: no stereo now"),
	       "boot: desc %ux%u cur %d flat %ux%u", g_desc3d.w, g_desc3d.h, atomic_load(&g_es.cur),
	       atomic_load(&g_es.flat_w), atomic_load(&g_es.flat_h));
	/* the front end: ticks only (no H4) - nothing asked for */
	for (int i = 0; i < 30; i++)
		es_tick_at(t += 0.1);
	EXPECT(g_rs_calls == 0, "front end: %d console calls", g_rs_calls);
	/* the first world frame: eyes wanted, the surface still 16:9 -> flat (held); the next tick switches, then stereo */
	int creates0 = g_creates;
	EXPECT(es_world(t += 0.1) == 1 && g_es.held == 1, "first world frame: %d calls, held %llu", g_h4_calls,
	       (unsigned long long)g_es.held);
	check_mono("eye size held");
	EXPECT(es_world(t += 0.1) == 2 && g_rs_calls == 1 && !strcmp(g_rs_last, "SetResolution 1832 1920") &&
		       atomic_load(&g_es.cur) == 1 && g_desc3d.w == 1832 && g_desc3d.h == 1920 && g_es.switches[1] == 1,
	       "switch to the eye size: calls %d '%s' cur %d desc %ux%u", g_rs_calls, g_rs_last, atomic_load(&g_es.cur),
	       g_desc3d.w, g_desc3d.h);
	somvr_rec rec;
	EXPECT(g_h4_calls == 2 && somvr_rec_read(&g_sh, g_frame_no, &rec) && rec.layout == SOMVR_LAYOUT_B && rec.applied == 3,
	       "eye size: no complete layout-B record");
	check_eyes("eye size: stereo", 3, 2);
	EXPECT(g_et.pub.w == 1832 && g_et.pub.h == 1920 && g_creates == creates0 + 2 &&
		       log_has("'SetResolution 1832 1920' -> 3D 1832x1920 (the eye size: stereo)"),
	       "eye textures %ux%u, made %d", g_et.pub.w, g_et.pub.h, g_creates - creates0);
	/* the pause menu: the world still renders through H4 (stereo); 5 s, no switch */
	for (int i = 0; i < 50; i++)
		EXPECT(es_world(t += 0.1) == 2, "pause menu frame %d: not stereo", i);
	EXPECT(g_rs_calls == 1, "pause menu: %d console calls", g_rs_calls);
	/* the main menu / a loading screen: no H4; the game's size after the hold (2 s), not before */
	double t_off = t;
	while (t < t_off + 1.9)
		es_tick_at(t += 0.1);
	EXPECT(g_rs_calls == 1 && atomic_load(&g_es.cur) == 1, "before the hold ran out: %d calls", g_rs_calls);
	es_tick_at(t += 0.2);
	EXPECT(g_rs_calls == 2 && !strcmp(g_rs_last, "SetResolution 1920 1080") && atomic_load(&g_es.cur) == 0 &&
		       g_desc3d.w == 1920 && g_desc3d.h == 1080 && g_es.switches[0] == 1 &&
		       log_has("'SetResolution 1920 1080' -> 3D 1920x1080 (the game's size: menus, loading, flat)"),
	       "after the hold: calls %d '%s' cur %d desc %ux%u", g_rs_calls, g_rs_last, atomic_load(&g_es.cur), g_desc3d.w,
	       g_desc3d.h);
	for (int i = 0; i < 20; i++)
		es_tick_at(t += 0.1);
	EXPECT(g_rs_calls == 2, "menu: thrash (%d calls)", g_rs_calls);
	/* back in the world: held one frame, the eye size again, the same eye textures (made once) */
	EXPECT(es_world(t += 0.1) == 1 && es_world(t += 0.1) == 2 && g_rs_calls == 3 && atomic_load(&g_es.cur) == 1,
	       "back in the world: calls %d cur %d", g_rs_calls, atomic_load(&g_es.cur));
	check_eyes("eye size again", 3, 2);
	EXPECT(g_creates == creates0 + 2 && g_et.pub.w == 1832, "eye textures re-made (%d) at the switch", g_creates - creates0);
	/* a short menu (1 s without H4, less than the hold): no switch; stereo right away after it */
	for (int i = 0; i < 10; i++)
		es_tick_at(t += 0.1);
	EXPECT(es_world(t += 0.1) == 2 && g_rs_calls == 3, "after a short menu: calls %d", g_rs_calls);
	/* SOMVR_EYE_SIZE_STALL_MS=250 (never a void): a 3 s stall in the world (no tick, no frame) is not a menu: stereo at
	 * once after it, no mode set; then a real menu still switches after the hold */
	g_es.stall_s = 0.25;
	EXPECT(es_world(t += 3.0) == 2 && g_rs_calls == 3 && atomic_load(&g_es.cur) == 1 &&
		       log_has("a stall of 3000 ms (no input tick): not counted toward the hold"),
	       "a stall in the world: calls %d cur %d", g_rs_calls, atomic_load(&g_es.cur));
	for (int i = 0; i < 5; i++)
		EXPECT(es_world(t += 0.1) == 2, "after the stall frame %d: not stereo", i);
	double t_m = t;
	while (t < t_m + 2.2)
		es_tick_at(t += 0.1);
	EXPECT(g_rs_calls == 4 && atomic_load(&g_es.cur) == 0, "a menu after the stall: calls %d cur %d", g_rs_calls,
	       atomic_load(&g_es.cur));
	EXPECT(es_world(t += 0.1) == 1 && es_world(t += 0.1) == 2 && g_rs_calls == 5, "back in the world: calls %d",
	       g_rs_calls);
	g_es.stall_s = 0;
	/* without it (build 12's): the same stall switches to the game's size at its first tick */
	EXPECT(es_world(t += 3.0) == 1 && g_rs_calls == 6 && atomic_load(&g_es.cur) == 0, "a stall, the switch off: calls %d "
	       "cur %d", g_rs_calls, atomic_load(&g_es.cur));
	EXPECT(es_world(t += 0.1) == 2 && g_rs_calls == 7, "back: calls %d", g_rs_calls);
	g_rs_calls = 3; /* (the counts below are build 12's) */
	/* flat play in the world (the trigger off): H4 wants no eyes; the game's size after the hold */
	snprintf(g_trig_path, sizeof g_trig_path, "%s", "/nonexistent/somvr.trigger");
	atomic_store(&g_trig, 0);
	for (int i = 0; i < 25; i++)
		EXPECT(es_world(t += 0.1) == 1, "trigger off frame %d: not flat", i);
	EXPECT(g_rs_calls == 4 && atomic_load(&g_es.cur) == 0, "trigger off: calls %d cur %d", g_rs_calls,
	       atomic_load(&g_es.cur));
	g_trig_path[0] = 0;
	/* a mode set that runs after the call returns: pending, done at a later tick */
	g_rs_mode = 1;
	EXPECT(es_world(t += 0.1) == 1, "async: first frame not held");
	es_tick_at(t += 0.1);
	EXPECT(g_es.pending == 1 && g_rs_calls == 5 && atomic_load(&g_es.cur) == 0, "async: pending %d calls %d", g_es.pending,
	       g_rs_calls);
	EXPECT(frame() == 1, "async: a frame before the mode set was not held");
	es_game_mode_set(g_rs_pend_w);
	es_tick_at(t += 0.1);
	EXPECT(!g_es.pending && atomic_load(&g_es.cur) == 1 && log_has("(its mode set after the call;"), "async: not done");
	EXPECT(frame() == 2, "async: no stereo after the late mode set");
	g_rs_mode = 0;
	/* the engine's vtable is not the checked one: no call, a failure, retried after 1 s */
	uint64_t f0 = g_es.fails;
	g_eng.vt = 0x1234;
	t_off = t;
	while (t < t_off + 2.2)
		es_tick_at(t += 0.1);
	EXPECT(g_es.fails == f0 + 1 && g_rs_calls == 6 - 1 && atomic_load(&g_es.cur) == 1 && log_has("has vtable 0x1234"),
	       "bad engine vtable: fails %llu calls %d", (unsigned long long)(g_es.fails - f0), g_rs_calls);
	g_eng.vt = (uintptr_t)g_vteng;
	es_tick_at(t += 1.1);
	EXPECT(g_rs_calls == 6 && atomic_load(&g_es.cur) == 0 && g_es.tries == 0, "retry: calls %d cur %d", g_rs_calls,
	       atomic_load(&g_es.cur));
	/* a mode set that never comes: the eyes held 2 s, then stereo at the game's size while the tick tries on; three
	 * tries (1 s pending, 1 s apart), then no more switching */
	g_rs_mode = 2;
	uint64_t ho0 = g_es.held_outs;
	int st2 = 0;
	for (int i = 0; i < 80 && !g_es.failed; i++)
		st2 += es_world(t += 0.1) == 2;
	EXPECT(g_es.failed && g_es.fails == f0 + 4 && g_rs_calls == 9 && g_es.held_outs == ho0 + 1 && st2 >= 30 &&
		       log_has("no mode set within 1 s") && log_has("no more switching, stereo at whatever size the 3D surface has") &&
		       log_has("WARN ESIZE eyes held"),
	       "never a mode set: failed %d fails %llu calls %d held-outs %llu stereo frames %d", g_es.failed,
	       (unsigned long long)(g_es.fails - f0), g_rs_calls, (unsigned long long)(g_es.held_outs - ho0), st2);
	EXPECT(es_world(t += 0.1) == 2 && g_rs_calls == 9, "given up: not stereo (calls %d)", g_rs_calls);
	check_eyes("given up: stereo at the game's size", 3, 2);
	EXPECT(g_et.pub.w == 1920 && g_et.pub.h == 1080, "given up: eye textures %ux%u", g_et.pub.w, g_et.pub.h);
	g_rs_mode = 0;
	/* no tick ever comes (another library has the slot): held at most 2 s, then stereo at the game's size */
	es_reset();
	es_game_mode_set(1920);
	ho0 = g_es.held_outs;
	int held = 0;
	for (int n = 0; n < 40 && frame() == 1; n++)
		g_cb_test_t = (t += 0.1), held++;
	EXPECT(held >= 19 && held <= 22 && g_es.held_out && g_es.held_outs == ho0 + 1 && !g_es.failed && g_rs_calls == 9,
	       "no tick: held %d frames, held out %d, calls %d", held, g_es.held_out, g_rs_calls);
	EXPECT(frame() == 2, "no tick: not stereo after the hold-out");
	/* always (no switch): every mode set gets the eye size, as before */
	g_es.sw = 0;
	es_game_mode_set(1920);
	EXPECT(g_desc3d.w == 1832 && g_desc3d.h == 1920, "always: desc %ux%u", g_desc3d.w, g_desc3d.h);
	flush_log();
	printf("eyesize switch: boot 16:9, first world frame held, 'SetResolution 1832 1920' at the tick, stereo; pause menu "
	       "kept; the game's 1920x1080 after the 2 s hold (not at 1.9 s), no thrash; the eye textures made once; a short "
	       "menu, flat play, a late mode set, a bad engine vtable (retried), no mode set (3 tries, given up), no tick "
	       "(held 2 s); always\n");
	es_reset();
	atomic_store(&g_es.cur, -1);
	g_es.sw = sw0, g_es.hold_s = hold0;
	g_layout = g_rigcfg.layout = lay, g_views_fake = fake, g_inp.on = inp, g_hudq.on = hud;
	g_eye_sz.mode = mode, g_eye_sz.w = ew, g_eye_sz.h = eh;
	g_surf.desc = surf0;
	g_cb_test_t = -1;
}

/* ---- SOMVR_HIDE_HEAD (switched on at start with 3,5) ---------------------------------------------------------------- */
#define HB(p) (1u << (p))
static int scene_ok(int k, uint32_t gbuf, uint32_t shadow)
{
	return g_scm[k][1] == gbuf && g_scm[k][0] == shadow && g_scm[k][2] == shadow && g_scm[k][3] == shadow &&
	       g_scm[k][4] == shadow;
}
#define SCENE(what, gbuf, shadow)                                                                                    \
	EXPECT(scene_ok(0, gbuf, shadow) && scene_ok(1, gbuf, shadow) && player_node()->mask[0] == (gbuf),             \
	       "%s: G-buffer %#x/%#x shadows %#x,%#x,%#x,%#x / %#x,%#x,%#x,%#x node now %#x (want %#x, %#x)", what,      \
	       g_scm[0][1], g_scm[1][1], g_scm[0][0], g_scm[0][2], g_scm[0][3], g_scm[0][4], g_scm[1][0], g_scm[1][2],   \
	       g_scm[1][3], g_scm[1][4], player_node()->mask[0], (unsigned)(gbuf), (unsigned)(shadow))
static void hide_frame(void)
{
	memset(g_scm, 0xee, sizeof g_scm);
	frame();
}
static void hide_tests(const char *trig)
{
	uint64_t b = 1;
	EXPECT(!parse_pieces("3,5", &b) && b == 0x28 && !parse_pieces("2-4,9", &b) && b == 0x21c && !parse_pieces("none", &b) &&
		       b == 0 && !parse_pieces("63", &b) && b == 1ull << 63 && !parse_pieces("1,2 rest", &b) && b == 6 &&
		       !parse_pieces("7\n", &b) && b == 0x80 && parse_pieces("x", &b) && parse_pieces("64", &b) &&
		       parse_pieces("3,", &b) && parse_pieces("5-3", &b) && parse_pieces("-1", &b) && parse_pieces("2;3", &b) &&
		       parse_pieces("", &b),
	       "parse_pieces");
	EXPECT(g_hide.want == 0x28 && g_hide.shadow == 1 && g_ex[EX_SH_PRE].installed && g_ex[EX_SH_LIGHTS].installed &&
		       g_ex[EX_SH_CASC].installed && g_ex[EX_SH_LIGHTS2].installed && log_has("HEADSITE HEAD_NODE_SETMASK ok") &&
		       log_has("hide_head=3,5 head_shadow=1"),
	       "hide config: want %#llx shadow %d", (unsigned long long)g_hide.want, g_hide.shadow);
	EXPECT(g_hh.vt_model == (uintptr_t)g_vt_smodel && g_hh.vt_rend == (uintptr_t)g_vt_rend &&
		       g_hh.vt_handle == (uintptr_t)g_vt_handle && g_hh.vt_node == (uintptr_t)g_vt_node,
	       "the vtables from the slot sites");
	g_hide.on = 1;
	g_pl = &g_player;
	g_layout = g_rigcfg.layout = SOMVR_LAYOUT_B;
	g_views_fake = 1;
	/* 1. stereo: hidden in the G-buffer, shown in the four shadow calls, hidden again after each */
	hide_frame();
	EXPECT(g_sm.mask[0] == 0x28 && g_hide.ours == 0x28 && g_hide_calls == 2 && g_show_calls == 0 && g_hide.count == 17,
	       "hide: model mask %#x ours %#llx hides %d shows %d count %u", g_sm.mask[0], (unsigned long long)g_hide.ours,
	       g_hide_calls, g_show_calls, g_hide.count);
	SCENE("stereo", 0x28, 0);
	EXPECT(g_sca[0] == 0x12 * 1000 + 0x34 && g_sca[1] == 0xa00 && g_scf[0] == 1.5f && g_scf[1] == 2.5f && g_sca[2] == 0xb00 &&
		       g_sca[3] == 15 && g_sc_bad == 0,
	       "shadow calls: arguments %#llx %#llx %g %g %#llx %#llx, %d bad returns", (unsigned long long)g_sca[0],
	       (unsigned long long)g_sca[1], g_scf[0], g_scf[1], (unsigned long long)g_sca[2], (unsigned long long)g_sca[3], g_sc_bad);
	EXPECT(atomic_load(&g_hs_swaps) == 8 && log_has("HIDE first shadow window HEAD_SHADOW_PRE"), "swaps %llu",
	       (unsigned long long)atomic_load(&g_hs_swaps));
	hide_frame();
	EXPECT(g_hide_calls == 2, "hide: SetPieceHidden called again (%d) for pieces already hidden", g_hide_calls);
	SCENE("stereo, 2nd frame", 0x28, 0);
	/* 2. a piece the game hid itself (9): not ours - hidden everywhere, never shown by us */
	st_vf50(&g_sm, 9, 1);
	hide_frame();
	SCENE("the game's piece 9", 0x228, 0x200);
	/* 3. the trigger words: headshadow-/+ and a new set */
	snprintf(g_trig_path, sizeof g_trig_path, "%s", trig);
	set_trigger(trig, "on");
	set_trigger(trig, "headshadow-");
	hide_frame();
	EXPECT(!g_hide.shadow && log_has("head shadow off (trigger word)"), "headshadow-: shadow %d", g_hide.shadow);
	SCENE("headshadow-", 0x228, 0x228);
	set_trigger(trig, "headshadow+");
	hide_frame();
	SCENE("headshadow+", 0x228, 0x200);
	int s0 = g_show_calls;
	set_trigger(trig, "hide 4-6");
	hide_frame();
	EXPECT(g_hide.want == 0x70 && g_hide.ours == 0x70 && g_sm.mask[0] == 0x270 && g_show_calls == s0 + 1,
	       "hide 4-6: want %#llx ours %#llx mask %#x shows %d", (unsigned long long)g_hide.want, (unsigned long long)g_hide.ours,
	       g_sm.mask[0], g_show_calls - s0);
	SCENE("hide 4-6", 0x270, 0x200);
	set_trigger(trig, "hide 3,20");
	hide_frame();
	EXPECT(g_hide.ours == 0x08 && g_sm.mask[0] == 0x208 && log_has("HIDE pieces 20 skipped: the model has 17 pieces"),
	       "hide 3,20: ours %#llx mask %#x", (unsigned long long)g_hide.ours, g_sm.mask[0]);
	set_trigger(trig, "hide x");
	hide_frame();
	EXPECT(g_hide.ours == 0x08 && log_has("TRIGGER cmd=hide (not a piece list: ignored)"), "hide x changed the set");
	set_trigger(trig, "hide 3,5");
	hide_frame();
	SCENE("hide 3,5 again", 0x228, 0x200);
	/* 4. the game shows one of ours (a SHOW_PIECE_KEY, a rebuild in place): hidden again the next frame */
	st_vf50(&g_sm, 5, 0);
	uint64_t r0 = g_hide.rehides;
	hide_frame();
	EXPECT(g_hide.rehides == r0 + 1 && g_sm.mask[0] == 0x228 && log_has("(again: the game had shown ours)"),
	       "rehide: %llu mask %#x", (unsigned long long)(g_hide.rehides - r0), g_sm.mask[0]);
	/* 5. stereo stops (the trigger file goes): the flat frame shows ours, the game's piece 9 stays hidden */
	set_trigger(trig, NULL);
	hide_frame();
	EXPECT(g_h4_calls == 1 && g_sm.mask[0] == 0x200 && g_hide.ours == 0 && g_hide.model == 0, "flat: mask %#x ours %#llx",
	       g_sm.mask[0], (unsigned long long)g_hide.ours);
	EXPECT(g_scm[0][1] == 0x200 && g_scm[0][0] == 0x200, "flat frame: the G-buffer saw %#x", g_scm[0][1]);
	int h1 = g_hide_calls, s1 = g_show_calls;
	hide_frame();
	EXPECT(g_hide_calls == h1 && g_show_calls == s1, "flat frames keep calling SetPieceHidden");
	set_trigger(trig, "on");
	hide_frame();
	SCENE("stereo again", 0x228, 0x200);
	/* 6. no XR views for a frame (layout B on the runtime): flat, shown; then hidden again */
	g_views_fake = 0;
	atomic_store(&g_sh.xr.state, SOMVR_XRS_DEAD);
	hide_frame();
	EXPECT(g_h4_calls == 1 && g_sm.mask[0] == 0x200, "no views: mask %#x", g_sm.mask[0]);
	g_views_fake = 1;
	hide_frame();
	EXPECT(g_sm.mask[0] == 0x228, "views back: mask %#x", g_sm.mask[0]);
	/* 7. another model under the player (a rebuild): the old one is forgotten, never touched; the new one hidden */
	g_obj_model = &g_sm2;
	h1 = g_hide_calls, s1 = g_show_calls;
	hide_frame();
	EXPECT(g_hide.forgets == 1 && g_sm.mask[0] == 0x228 && g_sm2.mask[0] == 0x28 && g_hide_calls == h1 + 2 && g_show_calls == s1 &&
		       g_hide.model == (uintptr_t)&g_sm2 && log_has("pieces 3,5 forgotten, never touched again"),
	       "rebuild: forgets %llu old %#x new %#x", (unsigned long long)g_hide.forgets, g_sm.mask[0], g_sm2.mask[0]);
	SCENE("the new model", 0x28, 0);
	/* 8. no model for a while (loading): nothing changes, nothing is called; then the same model again */
	g_obj_model = NULL;
	h1 = g_hide_calls, s1 = g_show_calls;
	hide_frame();
	EXPECT(g_hide_calls == h1 && g_show_calls == s1 && g_hide.ours == 0x28 && g_hide.forgets == 1, "no model: calls or forgets");
	set_trigger(trig, NULL); /* stereo stops while there is no model: shown as soon as the model is back */
	hide_frame();
	EXPECT(g_hide.ours == 0x28 && g_show_calls == s1, "no model, flat: ours %#llx", (unsigned long long)g_hide.ours);
	g_obj_model = &g_sm2;
	hide_frame();
	EXPECT(g_hide.ours == 0 && g_sm2.mask[0] == 0 && g_show_calls == s1 + 2, "model back, flat: ours %#llx mask %#x",
	       (unsigned long long)g_hide.ours, g_sm2.mask[0]);
	set_trigger(trig, "on");
	/* 9. the render node's vtable is wrong: no swap (the shadow loses the pieces), logged */
	g_shandle2.node.vt = 0x1234;
	uint64_t sw0 = atomic_load(&g_hs_swaps);
	hide_frame();
	EXPECT(atomic_load(&g_hs_swaps) == sw0 && g_hide.nonode >= 1 && g_sm2.mask[0] == 0x28 &&
		       log_has("no render node behind +0xb0/+0x50"),
	       "bad node: swaps %llu nonode %llu", (unsigned long long)(atomic_load(&g_hs_swaps) - sw0),
	       (unsigned long long)g_hide.nonode);
	g_shandle2.node.vt = (uintptr_t)g_vt_node;
	hide_frame();
	SCENE("node good again", 0x28, 0);
	/* 10. the ring: an entry is only good for its own frame */
	uintptr_t nd = 0;
	uint64_t bits = 0;
	hide_publish(100, 0x5000, 0x28);
	EXPECT(hide_lookup(100, &nd, &bits) && nd == 0x5000 && bits == 0x28 && !hide_lookup(104, &nd, &bits) &&
		       !hide_lookup(101, &nd, &bits) && !hide_lookup(0, &nd, &bits),
	       "hide ring lookups");
	/* 11. the switch off: nothing hidden, nothing swapped */
	set_trigger(trig, NULL);
	hide_frame();
	set_trigger(trig, "on");
	g_hide.on = 0;
	h1 = g_hide_calls;
	hide_frame();
	SCENE("off", 0, 0);
	EXPECT(g_hide_calls == h1, "off: SetPieceHidden called");
	set_trigger(trig, NULL);
	g_trig_path[0] = 0;
	EXPECT(log_has("HIDE t=") && log_has(": hid 3,5, showed none"), "hide: no HIDE lines");
	printf("hide   3,5 hidden in the G-buffer, shown in the 4 shadow calls; the game's piece kept; words; rehide; flat "
	       "shows; rebuild forgets; no model; bad node; %llu swaps\n", (unsigned long long)atomic_load(&g_hs_swaps));
	g_obj_model = &g_sm;
	g_layout = g_rigcfg.layout = SOMVR_LAYOUT_A;
	g_views_fake = 0;
	g_pl = NULL;
}

/* ---- SOMVR_HUD=quad, H11 (switched on at start; layout A then, so inactive; toggled white box here) ----------------- */
/* the frame's HUD as somvr_hud names it for the present's tag (*gen, *kind); 0 = no entry for that tag */
static int hud_tag_of_last(uint32_t *gen, uint32_t *kind)
{
	somvr_hud_tag t;
	*gen = 999, *kind = 99;
	if (!somvr_hud_tag_get(&g_hud, atomic_load(&g_sh.tag_seq), &t))
		return 0;
	return *gen = t.gen, *kind = t.kind, 1;
}
/* the frame was stereo in `layout` (a complete record of it; the eyes themselves: check_stereo, check_eyes) */
static void check_rec(const char *what, uint32_t layout)
{
	somvr_rec rec;
	EXPECT(g_h4_calls == 2 && somvr_rec_read(&g_sh, g_frame_no, &rec) && rec.layout == layout && rec.applied == 3,
	       "%s: %d body calls, no complete record of layout %u", what, g_h4_calls, layout);
}
static int ev_ix(char k, int a, int from)
{
	for (int i = from; i < g_nev; i++)
		if (g_ev[i].k == k && (a < 0 || g_ev[i].a == a))
			return i;
	return -1;
}
/* one stereo frame whose HUD reached the texture: eye 0's pass skipped, eye 1's drew it, the composite copied it into
 * the HUD texture (after eye 1's pass, before the composite sampled the UI surface), the present's tag names it */
static void check_hud_frame(const char *what, uint32_t kind_want)
{
	st_tex_t *ht = g_ht.tex;
	uint32_t want = 0xB000u + (uint32_t)g_frame_no * 4u + 1u, kind = 0, gen = 0;
	uint64_t r0 = g_hud_video ? 2 : 1; /* the stand-in call sites add 1 (player view) or 2 (video) to the pass's 7 */
	int found = hud_tag_of_last(&gen, &kind), h0 = ev_ix('H', 0, 0), h1 = ev_ix('H', 1, 0), c = -1, p = ev_ix('P', -1, 0);
	for (int i = 0; ht && i < g_nev; i++)
		if (g_ev[i].k == 'C' && g_ev[i].a == ht->id)
			c = i;
	EXPECT(g_h4_calls == 2 && h0 < 0 && g_hud_ret[0] == r0 && h1 > ev_ix('E', -1, ev_ix('B', 1, 0)) &&
		       g_hud_ret[1] == 7 + r0,
	       "%s: eye 0's HUD pass ran (event %d, ret %llu) or eye 1's did not (event %d, ret %llu)", what, h0,
	       (unsigned long long)g_hud_ret[0], h1, (unsigned long long)g_hud_ret[1]);
	EXPECT(ht && c > h1 && p > c && ht->content == want && g_ev[p].v == want,
	       "%s: HUD copy at event %d (eye 1's pass %d, composite %d), texture holds %#x, want %#x", what, c, h1, p,
	       ht ? ht->content : 0, want);
	somvr_hud_info in;
	memset(&in, 0, sizeof in);
	EXPECT(found && somvr_hud_read(&g_hud, &in) && gen == in.gen && gen == g_ht.pub.gen && kind == kind_want && ht &&
		       in.image == ht->f3d->image && in.image && in.w == g_uisurf.desc.w && in.h == g_uisurf.desc.h &&
		       in.dxgi_format == g_uisurf.desc.format && g_h9_hud_gen == (int64_t)gen,
	       "%s: tag gen %u kind %u (want %u), info gen %u image %#llx %ux%u, inside Present %lld", what, gen, kind,
	       kind_want, in.gen, (unsigned long long)in.image, in.w, in.h, (long long)g_h9_hud_gen);
	EXPECT(ht && ht->desc.w == g_uisurf.desc.w && ht->desc.h == g_uisurf.desc.h && ht->desc.format == g_uisurf.desc.format &&
		       ht->desc.bind == 0x28 && ht->desc.usage == 0 && atomic_load(&g_hud.expect_w) == g_uisurf.desc.w &&
		       atomic_load(&g_hud.expect_h) == g_uisurf.desc.h,
	       "%s: HUD texture %ux%u format %u bind %#x, layer expects %ux%u", what, ht ? ht->desc.w : 0, ht ? ht->desc.h : 0,
	       ht ? ht->desc.format : 0, ht ? ht->desc.bind : 0, atomic_load(&g_hud.expect_w), atomic_load(&g_hud.expect_h));
}
/* a stereo frame without a HUD image: no copy into it, the tag names none */
static void check_no_hud(const char *what)
{
	uint32_t kind = 0, gen = 0;
	int found = hud_tag_of_last(&gen, &kind), c = -1;
	for (int i = 0; g_ht.tex && i < g_nev; i++)
		if (g_ev[i].k == 'C' && g_ev[i].a == ((st_tex_t *)g_ht.tex)->id)
			c = i;
	/* SOMVR_HUD on: an entry naming no HUD; off: no entry at all for this present */
	EXPECT(c < 0 && (g_hudq.on ? found && gen == 0 && kind == SOMVR_HUDK_NONE && g_h9_hud_gen == 0
				  : !found && g_h9_hud_gen == -1),
	       "%s: HUD copy event %d, tag %s gen %u kind %u, inside Present %lld", what, c, found ? "found" : "absent", gen,
	       kind, (long long)g_h9_hud_gen);
}
static void hud_tests(void)
{
	EXPECT(g_ex[EX_HUD_EYE].installed && g_ex[EX_HUD_COVER].installed && g_ex[EX_UI_COMP].installed &&
		       somvr_hud_ok(&g_hud) && g_hud.size == sizeof g_hud && dlsym(RTLD_DEFAULT, SOMVR_HUD_SYMBOL) == (void *)&g_hud &&
		       log_has("WARN SOMVR_HUD=quad applies in layout B only (SOMVR_STEREO=A)") &&
		       log_has("CONFIG hud=quad (inactive: not layout B)") && log_has("HOOK UI_PRESENT_COMPOSITE ok call"),
	       "hud config: hooks %d %d %d, object ok %d", g_ex[EX_HUD_EYE].installed, g_ex[EX_HUD_COVER].installed,
	       g_ex[EX_UI_COMP].installed, somvr_hud_ok(&g_hud));
	EXPECT(g_ht.state == 0 && g_ht.copies == 0 && g_hudq.skips == 0 && atomic_load(&g_hud.copies) == 0,
	       "the pipeline tests (SOMVR_HUD off, white box) touched the HUD: state %d copies %llu skips %llu", g_ht.state,
	       (unsigned long long)g_ht.copies, (unsigned long long)g_hudq.skips);
	g_hudq.on = 1;
	g_layout = g_rigcfg.layout = SOMVR_LAYOUT_B;
	g_views_fake = 1;
	g_surf.desc = SURF, g_uisurf.desc = UISURF;
	arm_bursts("hud tests"); /* as the trigger does: the next 300 calls of H9 and the three HUD hooks are logged */
	int makes0 = (int)g_ht.makes;
	/* 1. layout-B stereo frames: one HUD pass (eye 1), one copy per frame, the texture made once at the UI size */
	for (int i = 0; i < 3; i++) {
		frame();
		check_rec("hud: stereo B", SOMVR_LAYOUT_B);
		check_eyes("hud: stereo B", 3, 3); /* the two eye copies and the HUD copy */
		check_hud_frame("hud: stereo B", SOMVR_HUDK_WORLD);
	}
	EXPECT(g_ht.makes == (uint64_t)makes0 + 1 && g_ht.pub.gen == 2 && g_hud_draws > 0,
	       "hud: made %llu, gen %u (want: made once, gen 1 then 2 when its VkImage was known)",
	       (unsigned long long)g_ht.makes, g_ht.pub.gen);
	EXPECT(log_has("HUD HUD_PASS_EYE n=") && log_has("eye=0 frame=") && log_has("-> skipped") && log_has("-> kept") &&
		       log_has("HUD made 1: texture 1920x1080 format 28 bind 0x28") && log_has("-> copied gen=2 1920x1080") &&
		       log_has("HUD gen=2 1920x1080 dxgi=28 vkimage=") && log_has(" hud=2"),
	       "hud: no HUD / UI / H9 log lines");
	/* 2. a full-screen video inside H4 (HUD_PASS_COVER, no player): the same, kind video */
	g_hud_video = 1;
	frame();
	check_hud_frame("hud: video", SOMVR_HUDK_VIDEO);
	g_hud_video = 0;
	EXPECT(g_ex[EX_HUD_COVER].mods == 1, "hud: video pass skips %llu", (unsigned long long)g_ex[EX_HUD_COVER].mods);
	/* 3. eye 1's pass did not run (white box): nothing copied, the tag names no HUD (not the stale texture) */
	uint64_t miss0 = atomic_load(&g_hud.misses);
	g_hud_none = 1;
	frame();
	check_no_hud("hud: no HUD pass");
	g_hud_none = 0;
	EXPECT(g_ht.nopass == 1 && atomic_load(&g_hud.misses) == miss0 + 1, "hud: nopass %llu misses %llu",
	       (unsigned long long)g_ht.nopass, (unsigned long long)atomic_load(&g_hud.misses));
	frame();
	check_hud_frame("hud: back", SOMVR_HUDK_WORLD);
	/* 4. the UI surface is the 3D surface (InterfaceWidth/Height = the 3D size): no copy, the HUD stays on */
	void *ui = &g_uisurf, *s3 = &g_surf;
	memcpy(g_holder + HOLDER_TEXUI, &s3, 8);
	frame();
	check_no_hud("hud: shared surface");
	EXPECT(g_ht.shared == 1 && g_ht.state == 1 && log_has("HUD the UI surface is the 3D surface (1 stereo presents so far)"),
	       "hud: shared %llu state %d", (unsigned long long)g_ht.shared, g_ht.state);
	memcpy(g_holder + HOLDER_TEXUI, &ui, 8);
	/* 5. the UI size changes (SetResolution): a new texture, the old reference released, a new gen */
	uint32_t gen0 = g_ht.pub.gen;
	int rel0 = g_releases;
	void *old = g_ht.tex;
	g_uisurf.desc.w = 1280, g_uisurf.desc.h = 720;
	frame();
	check_hud_frame("hud: new UI size", SOMVR_HUDK_WORLD);
	EXPECT(g_ht.tex != old && g_ht.makes == (uint64_t)makes0 + 2 && g_releases == rel0 + 1 && g_ht.pub.gen == gen0 + 2 &&
		       g_ht.pub.w == 1280 && ((st_tex_t *)old)->refs == 0,
	       "hud: re-made %llu, releases +%d, gen %u -> %u, %ux%u", (unsigned long long)g_ht.makes, g_releases - rel0, gen0,
	       g_ht.pub.gen, g_ht.pub.w, g_ht.pub.h);
	g_uisurf.desc = UISURF;
	frame();
	check_hud_frame("hud: UI size back", SOMVR_HUDK_WORLD);
	/* 6. the threaded context: CopyResource through its own slot 47 */
	st_ctx.vt = (uintptr_t)st_vt_ctx_mt;
	frame();
	check_hud_frame("hud: threaded context", SOMVR_HUDK_WORLD);
	st_ctx.vt = (uintptr_t)st_vt_ctx;
	/* 7. a flat frame in layout B (no XR views): one body call, its HUD pass as it is, nothing copied */
	g_views_fake = 0;
	atomic_store(&g_sh.xr.state, SOMVR_XRS_DEAD);
	int d0 = g_hud_draws;
	frame();
	somvr_tag tg;
	EXPECT(g_h4_calls == 1 && somvr_tag_get(&g_sh, atomic_load(&g_sh.tag_seq), &tg) && tg.frame == 0,
	       "hud: the flat frame: %d body calls, tag frame %llu", g_h4_calls, (unsigned long long)tg.frame);
	check_no_hud("hud: flat frame");
	EXPECT(g_hud_draws == d0 + 1 && g_hud_ret[0] == 8, "hud: the flat frame's HUD pass did not run (%d)", g_hud_draws - d0);
	g_views_fake = 1;
	/* 8. layout A with SOMVR_HUD: both passes run, nothing copied */
	g_layout = g_rigcfg.layout = SOMVR_LAYOUT_A;
	d0 = g_hud_draws;
	frame();
	check_rec("hud: layout A", SOMVR_LAYOUT_A);
	check_no_hud("hud: layout A");
	EXPECT(g_hud_draws == d0 + 2 && g_hud_ret[0] == 8 && g_hud_ret[1] == 8, "hud: layout A passes %d", g_hud_draws - d0);
	g_layout = g_rigcfg.layout = SOMVR_LAYOUT_B;
	/* 9. fail soft: CreateTexture2D fails (white box: not made yet) -> HUD off for the run; stereo and the eyes go on,
	 * eye 0's pass is still skipped */
	g_ht.state = 0;
	memset(&g_ht.desc, 0, sizeof g_ht.desc);
	g_create_fail = 1;
	frame();
	check_rec("hud: CreateTexture2D fails", SOMVR_LAYOUT_B);
	check_eyes("hud: CreateTexture2D fails", 3, 2);
	check_no_hud("hud: CreateTexture2D fails");
	EXPECT(g_ht.state == -1 && log_has("HUD off for this run: CreateTexture2D(1920x1080 format 28 bind 0x28)") &&
		       g_hud_ret[0] == 1,
	       "hud: not off after a failed CreateTexture2D (state %d)", g_ht.state);
	g_create_fail = 0;
	frame();
	check_eyes("hud: off stays off", 3, 2);
	check_no_hud("hud: off stays off");
	/* 10. a UI surface that is not an IdxD3D11Texture2D: off (white box: recovered first) */
	g_ht.state = 0;
	frame();
	check_hud_frame("hud: recovered (white box)", SOMVR_HUDK_WORLD);
	g_uisurf.vt = 0x5555;
	frame();
	check_eyes("hud: bad UI surface vtable", 3, 2);
	check_no_hud("hud: bad UI surface vtable");
	EXPECT(g_ht.state == -1 && log_has("UI surface 0x") && log_has("(vtable 0x5555): not the expected objects"),
	       "hud: not off after a bad UI surface (state %d)", g_ht.state);
	g_uisurf.vt = (uintptr_t)st_vt_tex;
	g_ht.state = 1;
	/* 11. the switch off (white box): both passes run, nothing copied, no somvr_hud tags written */
	g_hudq.on = 0;
	d0 = g_hud_draws;
	frame();
	check_no_hud("hud: switch off");
	EXPECT(g_hud_draws == d0 + 2 && g_hud_ret[0] == 8, "hud: switch off: passes %d", g_hud_draws - d0);
	g_hudq.on = 1;
	frame();
	check_hud_frame("hud: on again", SOMVR_HUDK_WORLD);
	printf("hud    eye 0's pass skipped, eye 1's kept, the UI surface copied at the composite into a %ux%u texture (made "
	       "%llu times), tags gen %u; video kind; no pass / shared / flat / layout A / off: no copy; resize; failures off\n",
	       g_ht.pub.w, g_ht.pub.h, (unsigned long long)g_ht.makes, g_ht.pub.gen);
	g_hudq.on = 0;
	g_layout = g_rigcfg.layout = SOMVR_LAYOUT_A;
	g_views_fake = 0;
}

/* ---- comfort: SOMVR_VIGNETTE, SOMVR_SNAP_BLINK_MS, SOMVR_SEATED (somvr_comfort.h, re-xr-pipeline.md 11) ---------- */
/* this frame's somvr_comfort entry (what the layer reads for the record it submits) */
static somvr_comfort_frame cf_entry(const char *what)
{
	somvr_comfort_frame e;
	memset(&e, 0, sizeof e);
	EXPECT(somvr_comfort_get(&g_cft, g_frame_no, &e), "comfort %s: no entry for frame %llu", what,
	       (unsigned long long)g_frame_no);
	return e;
}
static float cf_heading(void) { return sv_heading(sv_quat_of(g_h4_view[0].quat)) * 57.29578f; }
/* the heading now minus h0 + d, wrapped (deg): 0 = turned by d from h0 */
static float cf_dh(float h0, float d) { return sv_wrap_pi((cf_heading() - h0 - d) * 0.01745329f) * 57.29578f; }
static void comfort_tests(void)
{
	/* the switches (main: SOMVR_VIGNETTE=0.6 SOMVR_SNAP_BLINK_MS=90; the blink's gate off until here, white box) */
	EXPECT(dlsym(RTLD_DEFAULT, SOMVR_COMFORT_SYMBOL) == (void *)&g_cft && somvr_comfort_ok(&g_cft) &&
		       g_cft.size == sizeof g_cft && NEAR(g_cft.vignette, 0.6, 1e-6) && g_cft.blink_ms == 90 && g_cf.on &&
		       log_has("CONFIG comfort vignette=0.60 (SOMVR_VIGNETTE) snap_blink_ms=90 (out 30 ms, in 60 ms) seated=0"),
	       "comfort config: object ok %d vignette %g blink %u", somvr_comfort_ok(&g_cft), g_cft.vignette, g_cft.blink_ms);
	EXPECT(atomic_load(&g_cft.n_frames) > 20, "comfort: the pipeline tests' stereo frames wrote %llu entries",
	       (unsigned long long)atomic_load(&g_cft.n_frames));
	int base0 = g_rigcfg.base, follow0 = g_follow.on, lay0 = g_layout, inp0 = g_inp.on;
	st_player_t *pl0 = g_pl;
	const sa_node *node0 = g_nodexf_node;
	float pos0[3], rot0[4];
	memcpy(pos0, g_pobj.pos, sizeof pos0), memcpy(rot0, g_pobj.rot, sizeof rot0);
	g_layout = g_rigcfg.layout = SOMVR_LAYOUT_A;
	g_views_fake = 0, g_rt_result = XR_SUCCESS, g_trig_path[0] = 0;
	g_rigcfg.base = SOMVR_BASE_HEAD, g_pl = &g_player, g_follow.on = 0, g_nodexf_node = NULL;
	g_pobj.pos[0] = 500, g_pobj.pos[1] = 100, g_pobj.pos[2] = -50;
	g_pobj.rot[0] = g_pobj.rot[1] = g_pobj.rot[2] = 0, g_pobj.rot[3] = 1;
	g_rt_head[0] = g_rt_head[1] = g_rt_head[2] = 0, g_rt_q = (sv_quat){ 0, 0, 0, 1 };
	g_rm[0] = g_rm[1] = g_rm[2] = 0;
	g_inp.pend_snap = 0, g_inp.pend_turn = 0, g_inp.on = 1;
	runtime_up();
	somvr_time_publish(&g_sh, g_vst.last_time + 2000000000, 11111111); /* a fresh time 2 s on: the speeds start again */
	atomic_fetch_add(&g_req_recenter, 1);
	frame();
	somvr_comfort_frame e = cf_entry("start");
	EXPECT((e.flags & SOMVR_CFF_RECENTER) && (e.flags & (SOMVR_CFF_GAP | SOMVR_CFF_FIRST)) && e.lin_mps == 0,
	       "comfort start: flags %#x lin %g", e.flags, e.lin_mps);
	/* Talion was put 1 m+ from his last place: the head base took the camera that frame, the head again now (a jump) */
	frame();
	e = cf_entry("settle");
	EXPECT(e.flags == (SOMVR_CFF_JUMP | SOMVR_CFF_HELD) && e.lin_mps == 0 && log_has("fallback to the camera base"),
	       "comfort: the camera base's frame and back is not a jump (%#x)", e.flags);
	/* 1. standing still; then the player's own head walks 3 cm a frame in the room: no artificial motion */
	float worst = 0;
	for (int i = 0; i < 8; i++) {
		if (i >= 3)
			g_rt_head[0] += 0.03f, g_rt_head[2] -= 0.02f;
		frame();
		e = cf_entry("still");
		worst = fmaxf(worst, fmaxf(e.lin_mps, e.ang_dps));
		EXPECT(e.flags == SOMVR_CFF_COUNTED && NEAR(e.dt_s, 0.011111, 1e-5), "comfort still: flags %#x dt %g", e.flags,
		       e.dt_s);
	}
	EXPECT(worst < 1e-3f, "comfort: the head's own motion counted as the rig's (%g)", worst);
	/* 2. Talion walks (his root 2 cm a tick: the head base moves with it): 1.8 m/s */
	g_rm[2] = 2;
	for (int i = 0; i < 4; i++)
		frame();
	e = cf_entry("walk");
	EXPECT(e.flags == SOMVR_CFF_COUNTED && NEAR(e.lin_mps, 1.8, 2e-3) && e.ang_dps < 1e-3f, "comfort walk: %g m/s %g deg/s",
	       e.lin_mps, e.ang_dps);
	g_rm[2] = 0;
	frame();
	/* 3. a smooth turn about the head (the controller's turn, white box: 1 deg a frame): 90 deg/s, no linear */
	for (int i = 0; i < 3; i++) {
		g_inp.pend_turn = 0.01745329f;
		frame();
	}
	e = cf_entry("smooth turn");
	EXPECT(e.flags == SOMVR_CFF_COUNTED && NEAR(e.ang_dps, 90, 0.05) && e.lin_mps < 2e-3f, "comfort smooth: %g deg/s %g m/s",
	       e.ang_dps, e.lin_mps);
	frame();
	/* 4. the snap blink (90 ms: 30 out, 60 in): the controller's snap is held until the black frame, lands there */
	somvr_blink_times(90, &g_cf.out_s, &g_cf.in_s);
	frame();
	float h0 = cf_heading();
	int32_t steps0 = g_rig.snap_steps;
	g_inp.pend_snap = 1;
	frame();
	e = cf_entry("snap held 1");
	EXPECT((e.flags & SOMVR_CFF_SNAP_HELD) && e.blink > 0.3f && e.blink < 0.45f && g_rig.snap_steps == steps0 &&
		       NEAR(cf_dh(h0, 0), 0, 1e-3) && g_inp.pend_snap == 1,
	       "comfort held 1: flags %#x blink %g steps %d heading %g (was %g)", e.flags, e.blink, g_rig.snap_steps,
	       cf_heading(), h0);
	frame();
	e = cf_entry("snap held 2");
	EXPECT((e.flags & SOMVR_CFF_SNAP_HELD) && e.blink > 0.7f && e.blink < 0.8f && g_rig.snap_steps == steps0,
	       "comfort held 2: flags %#x blink %g", e.flags, e.blink);
	frame();
	e = cf_entry("snap landed");
	EXPECT((e.flags & SOMVR_CFF_SNAP) && !(e.flags & SOMVR_CFF_SNAP_HELD) && e.blink == 1 && g_rig.snap_steps == steps0 + 1 &&
		       NEAR(cf_dh(h0, 30), 0, 1e-2) && g_inp.pend_snap == 0 && e.ang_dps < 1e-3f,
	       "comfort landed: flags %#x blink %g steps %d heading %g (was %g) ang %g", e.flags, e.blink, g_rig.snap_steps,
	       cf_heading(), h0, e.ang_dps);
	float prev = 1;
	int fade = 0;
	for (int i = 0; i < 8 && prev > 0; i++) {
		frame();
		e = cf_entry("fade in");
		EXPECT(e.blink < prev && NEAR(cf_dh(h0, 30), 0, 1e-2) && e.flags == SOMVR_CFF_COUNTED,
		       "comfort fade in: %g after %g flags %#x", e.blink, prev, e.flags);
		prev = e.blink;
		fade++;
	}
	EXPECT(prev == 0 && fade == 6, "comfort: faded in after %d frames (%g)", fade, prev);
	/* 5. the trigger word's snap (the writer thread's counter): held the same way, its request not consumed until black */
	uint32_t seen0 = (uint32_t)g_seen_req_snap;
	atomic_fetch_add(&g_req_snap, 1);
	frame();
	EXPECT((cf_entry("word held").flags & SOMVR_CFF_SNAP_HELD) && (uint32_t)g_seen_req_snap == seen0 &&
		       g_rig.snap_steps == steps0 + 1,
	       "comfort word: not held (seen %d steps %d)", g_seen_req_snap, g_rig.snap_steps);
	frame();
	frame();
	e = cf_entry("word landed");
	EXPECT(e.blink == 1 && (e.flags & SOMVR_CFF_SNAP) && g_rig.snap_steps == steps0 + 2 && NEAR(cf_dh(h0, 60), 0, 1e-2),
	       "comfort word landed: blink %g flags %#x steps %d heading %g", e.blink, e.flags, g_rig.snap_steps, cf_heading());
	for (int i = 0; i < 8; i++)
		frame();
	EXPECT(cf_entry("after").blink == 0, "comfort: still black");
	/* 6. a recentre and a teleport are not motion */
	atomic_fetch_add(&g_req_recenter, 1);
	frame();
	e = cf_entry("recentre");
	EXPECT((e.flags & SOMVR_CFF_RECENTER) && (e.flags & SOMVR_CFF_HELD), "comfort recentre: flags %#x", e.flags);
	frame();
	g_pobj.pos[0] += 600; /* a teleport: the head base's own 1 m check takes the camera that frame, then the new place */
	int jumps = 0;
	for (int i = 0; i < 4; i++) {
		frame();
		jumps += (cf_entry("teleport").flags & SOMVR_CFF_JUMP) != 0;
	}
	e = cf_entry("after the teleport");
	EXPECT(jumps >= 1 && e.flags == SOMVR_CFF_COUNTED && e.lin_mps < 1e-3f, "comfort teleport: %d jumps, then %#x %g",
	       jumps, e.flags, e.lin_mps);
	/* 7. the blink off: the turn lands at once, no black */
	g_cf.out_s = g_cf.in_s = 0;
	g_inp.pend_snap = 1;
	frame();
	e = cf_entry("no blink");
	EXPECT(e.blink == 0 && (e.flags & SOMVR_CFF_SNAP) && g_rig.snap_steps == steps0 + 3, "comfort no blink: %g %#x %d",
	       e.blink, e.flags, g_rig.snap_steps);
	/* 8. seated, LOCAL (no floor): the head at a recentre is the reference; the view is the base there */
	g_cf.seat_cfg.on = 1;
	g_sh.xr.space_type = 2; /* LOCAL */
	g_rt_head[0] = g_rt_head[2] = 0, g_rt_head[1] = -0.35f; /* sat down 35 cm below the runtime's origin */
	atomic_fetch_add(&g_req_recenter, 1);
	frame();
	EXPECT(NEAR(g_rig.p0.y, -35, 1e-3) && NEAR(eye_mid().y, g_pobj.pos[1] + 72.5f, 1e-3) && log_has("SEATED frame="),
	       "comfort seated LOCAL: p0.y %g view %g (want the base %g)", g_rig.p0.y, eye_mid().y, g_pobj.pos[1] + 72.5f);
	g_rt_head[1] = -0.45f; /* leans 10 cm down: the view 10 cm lower */
	frame();
	EXPECT(NEAR(eye_mid().y, g_pobj.pos[1] + 62.5f, 1e-3), "comfort seated: leaning %g", eye_mid().y);
	/* STAGE (a floor): the seated head 1.15 m above it -> the base there; SOMVR_HEIGHT_CM=120 fixes it; a head on the desk
	 * (0.3 m) is not a seated head: the last good height stays */
	g_sh.xr.space_type = 3;
	g_rigcfg.floor = 1; /* SOMVR_FLOOR=1: the rig itself keeps the real height (P0.y = 0); seated replaces that */
	g_rt_head[1] = 1.15f;
	atomic_fetch_add(&g_req_recenter, 1);
	frame();
	EXPECT(NEAR(g_rig.p0.y, 115, 1e-3) && NEAR(eye_mid().y, g_pobj.pos[1] + 72.5f, 1e-3), "comfort seated STAGE: %g %g",
	       g_rig.p0.y, eye_mid().y);
	g_rt_head[1] = 0.3f;
	atomic_fetch_add(&g_req_recenter, 1);
	frame();
	EXPECT(NEAR(g_rig.p0.y, 115, 1e-3) && g_cf.seat.rejected >= 1 && log_has("not a seated head"),
	       "comfort seated: the desk height taken (%g)", g_rig.p0.y);
	g_cf.seat_cfg.has_height = 1, g_cf.seat_cfg.height_cm = 120;
	g_rt_head[1] = 1.15f;
	atomic_fetch_add(&g_req_recenter, 1);
	frame();
	EXPECT(NEAR(g_rig.p0.y, 120, 1e-3) && NEAR(eye_mid().y, g_pobj.pos[1] + 67.5f, 1e-3), "comfort HEIGHT_CM: %g %g",
	       g_rig.p0.y, eye_mid().y);
	/* not seated: the rig's own floor latch again (P0.y = 0: the view the real 115 cm above the base) */
	g_cf.seat_cfg.on = 0, g_cf.seat_cfg.has_height = 0;
	atomic_fetch_add(&g_req_recenter, 1);
	frame();
	EXPECT(g_rig.p0.y == 0 && NEAR(eye_mid().y, g_pobj.pos[1] + 72.5f + 115, 1e-3), "comfort: not seated, p0.y %g view %g",
	       g_rig.p0.y, eye_mid().y);
	g_sh.xr.space_type = 0, g_rigcfg.floor = 0;
	/* 9. off (the object inactive, white box): no entry for the next frames */
	g_cf.on = 0;
	frame();
	somvr_comfort_frame none;
	EXPECT(!somvr_comfort_get(&g_cft, g_frame_no, &none), "comfort off: an entry was written");
	g_cf.on = 1;
	uint64_t n = atomic_load(&g_cft.n_frames);
	frame();
	EXPECT(atomic_load(&g_cft.n_frames) == n + 1, "comfort on again");
	printf("comfort somvr_comfort exported+active (vignette 0.60, blink 90 ms); the head's own motion 0, walk %.2f m/s, "
	       "smooth turn %.1f deg/s; a snap held 2 frames, landed on black, faded in over 6; recentre/teleport not motion; "
	       "seated LOCAL/STAGE/HEIGHT_CM; off\n", 1.8, 90.0);
	g_rt_head[0] = g_rt_head[1] = g_rt_head[2] = 0;
	g_rigcfg.base = base0, g_follow.on = follow0, g_layout = g_rigcfg.layout = lay0, g_pl = pl0, g_nodexf_node = node0;
	memcpy(g_pobj.pos, pos0, sizeof pos0), memcpy(g_pobj.rot, rot0, sizeof rot0);
	g_inp.pend_snap = 0, g_inp.pend_turn = 0, g_inp.on = inp0;
	atomic_fetch_add(&g_req_recenter, 1);
}

/* ---- SOMVR_FALLBACK=auto (switched on at start with stand-in sites, then off until fb_tests: white box). A stand-in
 * Player big enough for every field the detector reads (its first bytes = g_player's), PlayerBody with a StateTagMgr,
 * the camera, the presentation, photo mode, the cinematic manager in the stand-in shell, the server Combat's sync
 * action, PlayerCombat's struggles, the movement's attach byte, tag/sync records with names. The clock is g_fb_test_t
 * (90 Hz). ------------------------------------------------------------------------------------------------------- */
__attribute__((noipa)) uint64_t st_fb_code(uint64_t a) { return a * 3 + 1; }
static char g_fbhex[FS_N][256];
static void fb_stand_in_sites(struct site_spec *s) /* the 17 read-only sites on stand-in code (K_ENTRY, its own bytes) */
{
	for (int i = 0; i < FS_N; i++) {
		hex_of((const unsigned char *)st_fb_code, 8, g_fbhex[i], 128);
		hex_of((const unsigned char *)st_fb_code, 4, g_fbhex[i] + 128, 128);
		s[i] = (struct site_spec){ g_fb_sites_game[i].name, K_ENTRY, (uintptr_t)st_fb_code, (uintptr_t)st_fb_code,
					   g_fbhex[i], (uintptr_t)st_fb_code, g_fbhex[i] + 128 };
	}
}
static _Alignas(16) char g_fbP[0x800], g_fbbody[0x600], g_fbcam[0x400], g_fbpres[0x100], g_fbphoto[0x40],
	g_fbcine[0x40], g_fbsync[0x40], g_fbrec[6][0x30];
static const char *const g_fbrec_names[6] = { "PC_Bip_Sync_Cntr_F", "PreDeath_Kneeling", "PreDeath", "Gnd", "FinalBlow",
					      "Idl" };
static struct site_spec g_fbs[FS_N];
static void fb_p(size_t off, uintptr_t v) { memcpy(g_fbP + off, &v, 8); }
static void fb_i(size_t off, int32_t v) { memcpy(g_fbP + off, &v, 4); }
static void fb_tagset(int n, const int *which) /* the current state tags (set 1 of the double buffer) */
{
	char *mgr = g_fbbody + FB_BODY_TAGS, *set = mgr + FB_TAGS_SET0 + FB_TAGS_SET;
	mgr[FB_TAGS_CUR] = 1;
	memset(mgr + FB_TAGS_SET0, 0x5a, FB_TAGS_SET); /* set 0 (being built) holds garbage */
	for (int i = 0; i < n; i++) {
		uintptr_t r = (uintptr_t)g_fbrec[which[i]];
		memcpy(set + 8 * i, &r, 8);
	}
	uint32_t c = (uint32_t)n;
	memcpy(set + FB_TAGS_COUNT, &c, 4);
}
static const int FB_CALM_TAGS[2] = { 3, 5 }; /* Gnd, Idl */
static void fb_world(void)
{
	memset(g_fbP, 0, sizeof g_fbP);
	memcpy(g_fbP, &g_player, sizeof g_player);
	memset(g_fbbody, 0, sizeof g_fbbody);
	memcpy(g_fbbody, &g_pbody, sizeof g_pbody);
	fb_p(FB_P_BODY, (uintptr_t)g_fbbody);
	g_ppm.owner = g_fbP;
	g_fbP[FB_P_LIFE] = 2, g_fbP[FB_P_FSM] = 6, fb_i(FB_P_SPECT, 0);
	memset(g_fbphoto, 0, sizeof g_fbphoto), memset(g_fbcam, 0, sizeof g_fbcam), memset(g_fbpres, 0, sizeof g_fbpres);
	int32_t off = SOMVR_FB_PHOTO_OFF;
	memcpy(g_fbphoto + FB_PHOTO_STATE, &off, 4);
	fb_p(FB_P_PHOTO, (uintptr_t)g_fbphoto), fb_p(FB_P_CAM, (uintptr_t)g_fbcam), fb_p(FB_P_PRES, (uintptr_t)g_fbpres);
	fb_p(FB_P_MOUNT, 0);
	memset(g_fbcine, 0, sizeof g_fbcine), memset(g_fbsync, 0, sizeof g_fbsync);
	uintptr_t v = (uintptr_t)g_fbcine;
	memcpy(g_shell + FB_SH_CINE, &v, 8);
	g_pcs.sync[0] = g_fbsync;
	memset((char *)&g_pcomb + FB_PC_STRUGGLE, 0, 16);
	((char *)&g_ppm)[FB_PM_ATTACHED] = 0;
	for (int i = 0; i < 6; i++) {
		memset(g_fbrec[i], 0, sizeof g_fbrec[i]);
		g_fbrec[i][1] = 2;
		uintptr_t n = (uintptr_t)g_fbrec_names[i];
		memcpy(g_fbrec[i] + 0x20, &n, 8);
	}
	fb_tagset(2, FB_CALM_TAGS);
	g_pz[0x34] = 0;
	g_pl = (st_player_t *)g_fbP;
}
static void fb_env(const char *on, const char *tags, const char *ms, const char *sync, const char *fade)
{
	setenv("SOMVR_FALLBACK", "auto", 1);
	on ? setenv("SOMVR_FALLBACK_ON", on, 1) : unsetenv("SOMVR_FALLBACK_ON");
	tags ? setenv("SOMVR_FALLBACK_TAGS", tags, 1) : unsetenv("SOMVR_FALLBACK_TAGS");
	ms ? setenv("SOMVR_FALLBACK_MS", ms, 1) : unsetenv("SOMVR_FALLBACK_MS");
	sync ? setenv("SOMVR_FALLBACK_SYNC", sync, 1) : unsetenv("SOMVR_FALLBACK_SYNC");
	fade ? setenv("SOMVR_FALLBACK_FADE_MS", fade, 1) : unsetenv("SOMVR_FALLBACK_FADE_MS");
}
static void fb_restart(const struct site_spec *s)
{
	int bad = 0;
	fb_start(s, &bad);
}
static uint32_t fb_level(void) /* this frame's record: its fade level (999 = no record) */
{
	somvr_rec r;
	return somvr_rec_read(&g_sh, g_frame_no, &r) ? somvr_rec_fade(r.flags) : 999u;
}
static sv_vec3 fb_head(void) { sv_vec3 r = root(); return (sv_vec3){ r.x, r.y + g_head_up, r.z }; }
static int fb_at(sv_vec3 m, sv_vec3 w, float e) { return NEAR3(m, w.x, w.y, w.z, e); }
static void fb_frame1(void)
{
	g_fb_test_t += 1.0 / 90;
	frame();
}
/* frames until the shown presentation is `want` (at most n); the number of frames, -1 = never */
static int fb_until(int want, int n)
{
	for (int i = 1; i <= n; i++) {
		fb_frame1();
		if (g_fb.tr.shown == want && g_fb.tr.phase == SOMVR_FBT_IDLE)
			return i;
	}
	return -1;
}
/* n frames: 1 = third person shown in none of them */
static int fb_stays_first(int n)
{
	int ok = 1;
	for (int i = 0; i < n; i++) {
		fb_frame1();
		ok &= !g_fb.tr.shown && fb_level() == 0;
	}
	return ok;
}

static void fb_tests(const char *trig)
{
	g_views_fake = 0, g_layout = g_rigcfg.layout = SOMVR_LAYOUT_A;
	runtime_up();
	g_rigcfg.base = SOMVR_BASE_HEAD, g_node_ok = 1, g_follow.on = 0, g_audio = 0, g_cb.mode = CB_OFF, g_hide.on = 0;
	g_hudq.on = 0, g_nodexf_node = NULL, g_head_up = 72.5f;
	g_rm[0] = g_rm[1] = g_rm[2] = 0;
	g_rt_head[0] = g_rt_head[1] = g_rt_head[2] = 0, g_rt_q = (sv_quat){ 0, 0, 0, 1 };
	g_fb_test_t = 1000;
	fb_world();
	fb_env(NULL, NULL, NULL, NULL, NULL);
	fb_restart(g_fbs);
	EXPECT(g_fb.on && g_fb.sites_ok == (1u << FS_N) - 1 && g_fb.can == SOMVR_FB_ALL && g_fb.reads == 0x1fff &&
		       g_fb.sw_cfg.enabled == SOMVR_FB_ALL && NEAR(g_fb.sw_cfg.hold_s, 1.5, 1e-6) && g_fb.tags.n == 2,
	       "fallback: start: on %d sites %#x can %#x reads %#x tags %d", g_fb.on, g_fb.sites_ok, g_fb.can, g_fb.reads,
	       g_fb.tags.n);
	EXPECT(log_has("CONFIG fallback=auto: conditions cine,scripted,pres,sync,struggle,mount,attach,climb,death,photo,spect,"
		       "tag,forced") && log_has("FBSITE FB_ATTACH ok"), "fallback: no CONFIG/FBSITE lines");
	atomic_fetch_add(&g_req_recenter, 1);
	/* 1. calm: first person on the head node, no fade */
	EXPECT(fb_stays_first(30) && fb_at(eye_mid(), fb_head(), 1e-3) && g_fr.head_ok,
	       "fallback calm: eyes %g %g %g", eye_mid().x, eye_mid().y, eye_mid().z);
	EXPECT(log_has("FALLBACK t=") && log_has("cond - -> - (+- --) life=2 spect=0 photo=3 fsm=6"),
	       "fallback: no first cond line");
	/* 2. a cinematic: the fade out rising, the swap on the first black frame (the eyes at the camera), black, in */
	uintptr_t cam1 = 0x1234;
	memcpy(g_fbcine + FB_CINE_CAM, &cam1, 8);
	uint32_t lv[80];
	sv_vec3 mid[80];
	int shown[80], swap_at = -1;
	for (int i = 0; i < 80; i++) {
		fb_frame1();
		lv[i] = fb_level(), mid[i] = eye_mid(), shown[i] = g_fb.tr.shown;
		if (swap_at < 0 && shown[i])
			swap_at = i;
	}
	int rising = 1, falling = 1, before = 1, after = 1, last_black = -1;
	for (int i = 0; i < 80; i++) {
		if (swap_at >= 0 && i < swap_at)
			rising &= (i == 0 || lv[i] >= lv[i - 1]) && lv[i] < 255, before &= fb_at(mid[i], fb_head(), 1e-3);
		if (lv[i] == 255)
			last_black = i;
		if (swap_at >= 0 && i >= swap_at)
			after &= fb_at(mid[i], (sv_vec3){ 100, 200, 300 }, 1e-3) && shown[i];
	}
	for (int i = last_black + 1; i < 80 && lv[i]; i++)
		falling &= lv[i] < lv[i - 1];
	EXPECT(swap_at >= 20 && swap_at <= 25 && lv[swap_at] == 255 && rising && before && lv[0] == 0,
	       "fallback cine: the swap at frame %d (level %u), out rising %d, first person before %d", swap_at,
	       swap_at >= 0 ? lv[swap_at] : 0, rising, before);
	EXPECT(after && last_black >= swap_at + 2 && falling && lv[79] == 0 && !g_fr.head_ok && g_fb.tr.phase == SOMVR_FBT_IDLE,
	       "fallback cine: the camera base after the swap %d, black until %d, in falling %d, now %u", after, last_black,
	       falling, lv[79]);
	EXPECT(log_has("switch -> third person (game camera) by cine") && log_has("swap -> third person (game camera) at alpha "
		       "1.000") && log_has("fade out -> black") && log_has("fade in -> idle") && log_has("cond - -> cine (+cine --)"),
	       "fallback cine: log lines");
	/* in third person: no arm rig, no player for the combat */
	g_arm.have_rig = 1;
	fb_frame1();
	EXPECT(g_arm.have_rig == 0 && fb_after_body(0x77) == 0 && !g_fr.head_ok, "fallback: the arms' rig / combat player kept");
	/* the HMD still moves the view around the camera: 20 cm to the right */
	g_rt_head[0] = 0.2f;
	fb_frame1();
	EXPECT(NEAR(dist3((float[]){ eye_mid().x, eye_mid().y, eye_mid().z }, (float[]){ 100, 200, 300 }), 20, 1e-2),
	       "fallback: the HMD offset in third person (%g %g %g)", eye_mid().x, eye_mid().y, eye_mid().z);
	/* 3. the cinematic ends: back to the head node after the exit debounce, the head re-latched (the 20 cm gone) */
	cam1 = 0;
	memcpy(g_fbcine + FB_CINE_CAM, &cam1, 8);
	int n = fb_until(0, 200);
	EXPECT(n > 0 && fb_at(eye_mid(), fb_head(), 1e-3) && g_fr.head_ok && fb_level() == 0,
	       "fallback cine off: back after %d frames, eyes %g %g %g", n, eye_mid().x, eye_mid().y, eye_mid().z);
	EXPECT(g_fb.relatch == 2 && g_fb.swaps == 2 && g_fb.sw.switches == 2, "fallback: relatched %llu swaps %llu",
	       (unsigned long long)g_fb.relatch, (unsigned long long)g_fb.swaps);
	g_rt_head[0] = 0;
	atomic_fetch_add(&g_req_recenter, 1);
	fb_frame1();
	/* 4. every other condition: third person, then back */
	struct { const char *what; size_t off; int kind; uintptr_t on, off_v; } cs[] = {
		{ "scripted", 0, 1, 1, 0 }, { "pres", 0, 2, 0x55, 0 }, { "struggle", 0, 3, 0x99, 0 },
		{ "mount", FB_P_MOUNT, 0, 0xabc, 0 }, { "attach", 0, 4, 1, 0 }, { "climb (fsm 9)", FB_P_FSM, 5, 9, 6 },
		{ "death (life 3)", FB_P_LIFE, 5, 3, 2 }, { "photo", 0, 6, 0, SOMVR_FB_PHOTO_OFF },
		{ "spect", FB_P_SPECT, 7, 4, 0 }, { "death (tag PreDeath_Kneeling)", 0, 8, 1, 0 }, { "tag FinalBlow", 0, 8, 4, 0 },
	};
	for (size_t k = 0; k < sizeof cs / sizeof cs[0]; k++) {
		for (int on = 1; on >= 0; on--) {
			uintptr_t val = on ? cs[k].on : cs[k].off_v;
			int32_t i32 = (int32_t)val;
			switch (cs[k].kind) {
			case 0: fb_p(cs[k].off, val); break;
			case 1: g_fbcam[FB_CAM_SCRIPTED] = (char)val; break;
			case 2: memcpy(g_fbpres + FB_PRES_CUR, &val, 8); break;
			case 3: memcpy((char *)&g_pcomb + FB_PC_STRUGGLE + 8, &val, 8); break;
			case 4: ((char *)&g_ppm)[FB_PM_ATTACHED] = (char)val; break;
			case 5: g_fbP[cs[k].off] = (char)val; break;
			case 6: memcpy(g_fbphoto + FB_PHOTO_STATE, &i32, 4); break;
			case 7: fb_i(cs[k].off, i32); break;
			case 8: {
				int t2[3] = { 3, (int)val, 5 };
				on ? fb_tagset(3, t2) : fb_tagset(2, FB_CALM_TAGS);
				break;
			}
			}
			n = fb_until(on, 300);
			EXPECT(n > 0 && (on ? !g_fr.head_ok && fb_at(eye_mid(), (sv_vec3){ 100, 200, 300 }, 1e-3)
					    : g_fr.head_ok && fb_at(eye_mid(), fb_head(), 1e-3)),
			       "fallback %s %s: %d frames, eyes %g %g %g", cs[k].what, on ? "on" : "off", n, eye_mid().x, eye_mid().y,
			       eye_mid().z);
		}
	}
	EXPECT(log_has("FALLBACK tag 'PreDeath_Kneeling'") && log_has("is in class death") && log_has("FALLBACK tag 'FinalBlow'"),
	       "fallback: no tag class lines");
	/* 5. paired actions: a 0.3 s sync stays first person, a long one (0.6 s) goes to third person, its name logged */
	uintptr_t srec = (uintptr_t)g_fbrec[0];
	memcpy(g_fbsync + FB_SYNC_REC, &srec, 8);
	EXPECT(fb_stays_first(27), "fallback: a 0.3 s sync left first person");
	memset(g_fbsync + FB_SYNC_REC, 0, 8);
	EXPECT(fb_stays_first(60), "fallback: after a short sync");
	EXPECT(log_has("sync=0x") && log_has("'PC_Bip_Sync_Cntr_F'") && log_has("too short and still: first person"),
	       "fallback: the short sync's lines");
	fb_stays_first(90); /* the minimum hold since the last switch is over */
	memcpy(g_fbsync + FB_SYNC_REC, &srec, 8);
	n = fb_until(1, 200); /* 0.6 s + the 0.1 s debounce + out 0.15, black, in 0.2 */
	EXPECT(n >= 90 && n <= 110, "fallback: a long sync: third and clear after %d frames", n);
	memset(g_fbsync + FB_SYNC_REC, 0, 8);
	n = fb_until(0, 300);
	EXPECT(n > 0 && log_has("(counted)"), "fallback: back after the sync: %d", n);
	/* the server Combat is not a Combat (vtable): no sync read */
	g_pcs.vt = 0x4321;
	memcpy(g_fbsync + FB_SYNC_REC, &srec, 8);
	EXPECT(fb_stays_first(120), "fallback: a sync on a non-Combat was read");
	g_pcs.vt = (uintptr_t)st_vt_combat;
	memset(g_fbsync + FB_SYNC_REC, 0, 8);
	fb_stays_first(2);
	/* 6. pause holds the switch */
	g_pz[0x34] = 1;
	cam1 = 0x1234, memcpy(g_fbcine + FB_CINE_CAM, &cam1, 8);
	EXPECT(fb_stays_first(90) && g_fb.paused >= 90, "fallback: paused: switched");
	g_pz[0x34] = 0;
	n = fb_until(1, 60);
	EXPECT(n >= 38 && n <= 52, "fallback: unpaused: third and clear after %d frames", n);
	/* 7. the trigger words: first holds first person against a cinematic, auto lets it go, third forces it */
	EXPECT(!strcmp(fb_word(" first"), "fallback first"), "fallback: word first");
	n = fb_until(0, 300);
	EXPECT(n > 0 && fb_stays_first(120), "fallback first: %d", n);
	EXPECT(!strcmp(fb_word("auto"), "fallback auto") && fb_until(1, 60) > 0, "fallback auto: not back to third");
	cam1 = 0, memcpy(g_fbcine + FB_CINE_CAM, &cam1, 8);
	fb_until(0, 300);
	EXPECT(!strcmp(fb_word(" third"), "fallback third") && fb_until(1, 300) > 0 && g_fb.sw.cond == SOMVR_FB_BIT(SOMVR_FB_FORCED),
	       "fallback third: cond %#x", g_fb.sw.cond);
	EXPECT(!strncmp(fb_word(" sideways"), "fallback (bad", 13), "fallback: a bad word");
	/* ... through the trigger file (the writer thread) */
	snprintf(g_trig_path, sizeof g_trig_path, "%s", trig);
	set_trigger(trig, "on");
	set_trigger(trig, "fallback auto");
	n = fb_until(0, 300);
	EXPECT(n > 0 && log_has("TRIGGER cmd=fallback auto") && log_has("forced: third -> auto (trigger word)"),
	       "fallback: the trigger word through the file: %d", n);
	set_trigger(trig, NULL);
	g_trig_path[0] = 0;
	/* 8. a mismatched site drops its reads only: FB_MOUNT, then FB_TAGS (death by tag gone, death by life kept) */
	struct site_spec bad[FS_N];
	memcpy(bad, g_fbs, sizeof bad);
	bad[FS_MOUNT].ctx_hex = "00 11 22 33 44 55 66 77";
	bad[FS_TAGS].ctx_hex = "00 11 22 33 44 55 66 77";
	fb_restart(bad);
	EXPECT(g_fb.on && !(g_fb.sites_ok & FSB(MOUNT)) && !(g_fb.can & SOMVR_FB_BIT(SOMVR_FB_MOUNT)) &&
		       !(g_fb.reads & (SOMVR_FBR_MOUNT | SOMVR_FBR_TAGS)) && (g_fb.can & SOMVR_FB_BIT(SOMVR_FB_DEATH)) &&
		       log_has("FBSITE FB_MOUNT MISMATCH"), "fallback: a mismatched site: can %#x reads %#x", g_fb.can, g_fb.reads);
	fb_p(FB_P_MOUNT, 0xabc);
	int td[3] = { 3, 1, 5 };
	fb_tagset(3, td);
	EXPECT(fb_stays_first(120), "fallback: mount / death tag read without their sites");
	g_fbP[FB_P_LIFE] = 3;
	EXPECT(fb_until(1, 60) > 0, "fallback: death by life without the tag sites");
	g_fbP[FB_P_LIFE] = 2, fb_p(FB_P_MOUNT, 0), fb_tagset(2, FB_CALM_TAGS);
	/* 9. SOMVR_FALLBACK_ON=cine: a mount does not count */
	fb_env("cine", NULL, NULL, NULL, NULL);
	fb_restart(g_fbs);
	EXPECT(g_fb.can == (SOMVR_FB_BIT(SOMVR_FB_CINE) | SOMVR_FB_BIT(SOMVR_FB_FORCED)) &&
		       g_fb.reads == (SOMVR_FBR_CINE | SOMVR_FBR_PAUSE), "fallback ON=cine: can %#x reads %#x", g_fb.can, g_fb.reads);
	fb_p(FB_P_MOUNT, 0xabc);
	EXPECT(fb_stays_first(120), "fallback ON=cine: a mount counted");
	fb_p(FB_P_MOUNT, 0);
	/* 10. a plain cut (fade 0,0,0): the swap without black, no record ever faded; bad values: warnings, defaults */
	fb_env("bogus,cine", "death=,X", "100,400,200", NULL, "0,0,0");
	fb_restart(g_fbs);
	EXPECT(g_fb.sw_cfg.hold_s == 1.0f && g_fb.tags.n == 1 && log_has("WARN SOMVR_FALLBACK_MS hold 200 ms: 1000") &&
		       log_has("WARN SOMVR_FALLBACK_ON=bogus,cine") && log_has("WARN SOMVR_FALLBACK_TAGS=death=,X") &&
		       log_has("(a plain cut)"), "fallback: bad config: hold %g tags %d", g_fb.sw_cfg.hold_s, g_fb.tags.n);
	cam1 = 0x1234, memcpy(g_fbcine + FB_CINE_CAM, &cam1, 8);
	int faded = 0;
	for (int i = 0; i < 30; i++)
		fb_frame1(), faded |= fb_level() != 0;
	EXPECT(g_fb.tr.shown && !faded && g_fb.swaps == 1 && fb_at(eye_mid(), (sv_vec3){ 100, 200, 300 }, 1e-3),
	       "fallback cut: shown %d faded %d swaps %llu", g_fb.tr.shown, faded, (unsigned long long)g_fb.swaps);
	cam1 = 0, memcpy(g_fbcine + FB_CINE_CAM, &cam1, 8);
	/* 11. flat frames: the fallback does not run */
	uint64_t f0 = g_fb.frames;
	atomic_store(&g_sh.xr.state, SOMVR_XRS_DEAD);
	fb_frame1();
	atomic_store(&g_sh.xr.state, SOMVR_XRS_RUNNING);
	EXPECT(g_fb.frames == f0, "fallback: ran on a flat frame");
	/* 12. not the head base: off */
	g_rigcfg.base = SOMVR_BASE_CAMERA;
	fb_env(NULL, NULL, NULL, NULL, NULL);
	fb_restart(g_fbs);
	EXPECT(!g_fb.on && log_has("WARN SOMVR_FALLBACK=auto needs SOMVR_BASE=head"), "fallback: on without the head base");
	EXPECT(!strcmp(fb_word(" third"), "fallback (SOMVR_FALLBACK is off: ignored)"), "fallback: a word while off");
	g_rigcfg.base = SOMVR_BASE_HEAD;
	fb_frame1();
	EXPECT(fb_level() == 0 && g_fr.head_ok, "fallback off: the frame changed");
	printf("fallback  first person calm; a cinematic: fade out, the swap on black (eyes at the camera, the HMD still "
	       "applied), back on the head re-latched; 11 more conditions; short/long syncs; pause; the words (and the file); "
	       "site mismatches; ON=cine; a plain cut; flat; no head base\n");
	unsetenv("SOMVR_FALLBACK");
	g_pl = &g_player, g_ppm.owner = &g_player, g_pcs.sync[0] = NULL;
	memset(g_shell + FB_SH_CINE, 0, 8);
	g_fb.on = 0, g_fb_test_t = -1;
}

/* ---- SOMVR_CLIMB=b (switched on at start with stand-in sites, then off until climb_tests: white box). The stand-ins:
 * IEnginePhysics with slot 64 = a cast against a small world (Talion's capsule, r 35 cm about his root: hit first when a
 * ray starts inside it or crosses it, unless the cast's filter says skip; a wall face at z = 0 from y 0 to 400, triangle
 * id 7 = climbable stone for x < 560, id 8 = plain rock beyond; its top y = 400 for z 0..100, an object with surface id 9),
 * the surface lookups (FUN_02ae5d90 / FUN_02ae5e60 / FUN_021902d0 on those ids), the proxy (CPhysicsCharacter slots 12/13
 * and its flag), a Player big enough for the FSM byte and the mount, the proof sites on stand-in code. The player update
 * has the wall (Talion's root stops at z = -35 below the top) and gravity (4 cm a tick while the flag is 1). ---------- */
#define CLW_Z 0.0f
#define CLW_TOP 400.0f
#define CLW_ROCK_X 560.0f
#define CLW_R 35.0f
static struct { int calls, bad, honor_cb, cb_calls, self_hits; } g_clw;
static struct { uintptr_t vt; } g_phys;
static void *g_phys_ptr = &g_phys;
static void *volatile g_vtphys[70];
static _Alignas(16) char g_wallobj[0x80], g_topobj[0x80], g_srec[3][0x40], g_clP[0x800];
static const char *const g_srec_names[3] = { "ST_Climbable_Stone", "ST_Rock", "ST_Top" };
typedef struct { uintptr_t vt; char pad[0x38]; uintptr_t proxy; char pad2[0x18]; volatile unsigned char grav; } st_pc_t;
_Static_assert(offsetof(st_pc_t, grav) == CL_PC_GRAV && offsetof(st_pc_t, proxy) == UN_PROXY, "stand-in proxy");
/* SOMVR_CLIMB_UNPIN's stand-in hkpCharacterProxy: the manifold at +0x20 (data, size, capacity), its position (cm) */
typedef struct { char pad[0x20]; uintptr_t data; int32_t n, cap; float pos[3]; int sets; } st_pxy_t;
_Static_assert(offsetof(st_pxy_t, data) == UN_MANIFOLD, "stand-in hkpCharacterProxy");
static st_pxy_t g_pxy;
static _Alignas(16) float g_pxy_man[4][16];
static st_pc_t g_ppc, g_ppc2;
static void *volatile g_vtpc[16];
static char g_clhex[CLS_N][256];
static struct site_spec g_cls[CLS_N];
/* CL_CTXACT's stand-in: PlayerBody's context-action handler slot (SOMVR_CLIMB_LEDGES, 18i) */
static void *volatile g_vtctx[4];
static char g_clctx_hex[256];
static struct site_spec g_clctx_site;
static struct { int calls; uintptr_t a[4]; } g_ctxw;
__attribute__((noipa)) uint64_t st_ctxact(uintptr_t self, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	g_ctxw.calls++, g_ctxw.a[0] = self, g_ctxw.a[1] = a2, g_ctxw.a[2] = a3, g_ctxw.a[3] = a4;
	return 1;
}
typedef uint64_t (*st_filter_fn)(uintptr_t obj, uintptr_t ud);
__attribute__((noipa)) void st_pc_setgrav(void *pc, int on) { ((st_pc_t *)pc)->grav = (unsigned char)on; }
__attribute__((noipa)) void st_pc_setpos(uintptr_t pc, float *p)
{
	st_pxy_t *x = (st_pxy_t *)((st_pc_t *)pc)->proxy;
	if (x)
		memcpy(x->pos, p, sizeof x->pos), x->sets++;
}
__attribute__((noipa)) void st_pc_getpos(uintptr_t pc, float *p)
{
	st_pxy_t *x = (st_pxy_t *)((st_pc_t *)pc)->proxy;
	if (x)
		memcpy(p, x->pos, sizeof x->pos);
}
__attribute__((noipa)) uint64_t st_pc_getgrav(void *pc) { return ((st_pc_t *)pc)->grav != 0; }
__attribute__((noipa)) uint64_t st_cl_code(uint64_t a) { return a * 5 + 3; } /* the proof sites' stand-in code */
static uintptr_t st_rec_of(uint16_t id) { return id >= 7 && id <= 9 ? (uintptr_t)g_srec[id - 7] : 0; }
__attribute__((noipa)) uintptr_t st_surf_byid(const void *k)
{
	const unsigned char *p = k;
	uint16_t id;
	memcpy(&id, p, 2);
	return p[2] ? st_rec_of(id) : 0;
}
__attribute__((noipa)) uintptr_t st_surf_ray(const void *rv) /* FUN_02ae5d90: the object's id, else the triangle's */
{
	const unsigned char *r = rv;
	uintptr_t obj;
	uint16_t id = 0;
	memcpy(&obj, r + 0x20, 8);
	if (obj)
		memcpy(&id, (const char *)obj + CL_OBJ_SURF, 2);
	if (id && st_rec_of(id))
		return st_rec_of(id);
	if (r[0x2a]) {
		memcpy(&id, r + 0x28, 2);
		return st_rec_of(id);
	}
	return 0;
}
__attribute__((noipa)) uint64_t st_canclimb(const void *k)
{
	uintptr_t rec = st_surf_byid(k);
	return rec && rec != (uintptr_t)g_srec[1];
}
/* IEnginePhysics::vf_64(phys, q, r): the query checked against the combat collider's layout plus the material flag 0x80
 * (flags 0xd8), then the nearest hit; as Physics_RayCast, the triangle's material id only with 0x80 */
__attribute__((noipa)) uint64_t st_raycast(uintptr_t phys, void *qv, void *rv)
{
	const unsigned char *q = qv;
	unsigned char *r = rv;
	float f[3], t[3];
	uint32_t fl, flt;
	uintptr_t cb, z28, ud;
	uint16_t pre;
	memcpy(f, q, 12), memcpy(t, q + 12, 12), memcpy(&fl, q + 0x18, 4), memcpy(&flt, q + 0x1c, 4);
	memcpy(&cb, q + 0x20, 8), memcpy(&z28, q + 0x28, 8), memcpy(&ud, q + 0x30, 8), memcpy(&pre, r + 0x28, 2);
	g_clw.calls++;
	/* 0xd8: the climb's cast; 0xf8: SOMVR_HANDS_SURFACE's (+ 0x20: the hit's normal at r+0x0c) */
	if (phys != (uintptr_t)&g_phys || (fl != 0xd8 && fl != 0xf8) || flt != (g_cl.probe_flt ? g_cl.probe_flt : 0xfffffffdu) || !cb ||
	    z28 || !ud || pre != 0xffff || r[0x2a])
		g_clw.bad++;
	float d[3] = { t[0] - f[0], t[1] - f[1], t[2] - f[2] }, len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	if (!(len > 0))
		return 0;
	for (int i = 0; i < 3; i++)
		d[i] /= len;
	struct { float s; uintptr_t obj; uint16_t tri; } c[3];
	int n = 0;
	/* Talion: a vertical cylinder about his root, y root-100 .. root+90 */
	float cx = g_pobj.pos[0], cz = g_pobj.pos[2], ox = f[0] - cx, oz = f[2] - cz;
	float a = d[0] * d[0] + d[2] * d[2], b = 2 * (ox * d[0] + oz * d[2]), cc = ox * ox + oz * oz - CLW_R * CLW_R;
	float st = -1;
	if (cc <= 0)
		st = 0;
	else if (a > 1e-9f && b * b - 4 * a * cc >= 0 && (-b - sqrtf(b * b - 4 * a * cc)) / (2 * a) >= 0)
		st = (-b - sqrtf(b * b - 4 * a * cc)) / (2 * a);
	if (st >= 0 && f[1] + d[1] * st > g_pobj.pos[1] - 100 && f[1] + d[1] * st < g_pobj.pos[1] + 90)
		c[n++] = (typeof(c[0])){ st, (uintptr_t)&g_pobj, 0 };
	if (d[2] > 1e-6f && f[2] <= CLW_Z) { /* the face */
		float s = (CLW_Z - f[2]) / d[2], x = f[0] + d[0] * s, y = f[1] + d[1] * s;
		if (y >= 0 && y <= CLW_TOP && x > -1000 && x < 2000)
			c[n++] = (typeof(c[0])){ s, (uintptr_t)g_wallobj, (uint16_t)(x < CLW_ROCK_X ? 7 : 8) };
	}
	if (d[1] < -1e-6f && f[1] >= CLW_TOP) { /* the top */
		float s = (CLW_TOP - f[1]) / d[1], z = f[2] + d[2] * s;
		if (z >= CLW_Z && z <= CLW_Z + 100)
			c[n++] = (typeof(c[0])){ s, (uintptr_t)g_topobj, 0 };
	}
	for (int i = 0; i < n; i++) /* nearest first */
		for (int j = i + 1; j < n; j++)
			if (c[j].s < c[i].s) {
				typeof(c[0]) x = c[i];
				c[i] = c[j], c[j] = x;
			}
	for (int i = 0; i < n; i++) {
		if (c[i].s > len)
			break;
		if (c[i].obj == (uintptr_t)&g_pobj) {
			g_clw.self_hits++;
			if (g_clw.honor_cb && (g_clw.cb_calls++, !(((st_filter_fn)cb)(c[i].obj, ud) & 0xff)))
				continue;
		}
		float p[3] = { f[0] + d[0] * c[i].s, f[1] + d[1] * c[i].s, f[2] + d[2] * c[i].s };
		memcpy(r, p, 12);
		memcpy(r + 0x20, &c[i].obj, 8);
		if (fl & 0x20) { /* Physics_RayCast 0x8de79b: the normal (and the plane's d) with the flag 0x20 */
			float nv[4] = { 0, 0, -1, 0 };
			if (c[i].obj == (uintptr_t)g_topobj)
				nv[1] = 1, nv[2] = 0;
			else if (c[i].obj == (uintptr_t)&g_pobj) {
				float rx = p[0] - g_pobj.pos[0], rz = p[2] - g_pobj.pos[2], l = sqrtf(rx * rx + rz * rz);
				nv[0] = l > 0 ? rx / l : 1, nv[2] = l > 0 ? rz / l : 0;
			}
			nv[3] = nv[0] * p[0] + nv[1] * p[1] + nv[2] * p[2];
			memcpy(r + 0x0c, nv, 16);
		}
		if (c[i].tri && (fl & 0x80)) /* Physics_RayCast: the material id only with the flag 0x80 */
			memcpy(r + 0x28, &c[i].tri, 2), r[0x2a] = 1;
		return 1;
	}
	return 0;
}
static void cl_entry(void *fn, int i)
{
	hex_of(fn, 8, g_clhex[i], 128);
	hex_of(fn, 4, g_clhex[i] + 128, 128);
	g_cls[i] = (struct site_spec){ g_cl_sites_game[i].name, K_ENTRY, (uintptr_t)fn, (uintptr_t)fn, g_clhex[i], (uintptr_t)fn,
				       g_clhex[i] + 128 };
}
static void cl_vslot(void *volatile *slot, void *fn, int i)
{
	*slot = fn;
	hex_of((const unsigned char *)slot, 8, g_clhex[i], 128);
	hex_of(fn, 4, g_clhex[i] + 128, 128);
	g_cls[i] = (struct site_spec){ g_cl_sites_game[i].name, K_VSLOT, (uintptr_t)slot, (uintptr_t)slot, g_clhex[i],
				       (uintptr_t)fn, g_clhex[i] + 128 };
}
static struct { uint16_t id, pad[3]; uintptr_t rec; } g_cltab[4]; /* FUN_02ae5d90's table: ids 7, 8, 9 and a 0 */
static uintptr_t g_cltab_b, g_cltab_e;
static void cl_stand_in_sites(void)
{
	cl_vslot(&g_vtphys[CL_SLOT_RAYCAST], (void *)st_raycast, CLS_RAYCAST);
	cl_entry((void *)st_surf_ray, CLS_SURF_RAY);
	cl_entry((void *)st_surf_byid, CLS_SURF_ID);
	cl_entry((void *)st_canclimb, CLS_CANCLIMB);
	cl_entry((void *)st_cl_code, CLS_GRAV_WISH);
	g_vtpc[13] = (void *)st_pc_getgrav;
	cl_vslot(&g_vtpc[CL_SLOT_PC_GRAV], (void *)st_pc_setgrav, CLS_PC_GRAV);
	cl_entry((void *)st_cl_code, CLS_FSM);
	cl_entry((void *)st_cl_code, CLS_MOUNT);
	cl_vslot(&g_vtpc[CL_SLOT_PC_SETPOS], (void *)st_pc_setpos, CLS_PC_SETPOS);
	cl_vslot(&g_vtpc[CL_SLOT_PC_GETPOS], (void *)st_pc_getpos, CLS_PC_GETPOS);
	g_vtctx[2] = (void *)st_ctxact;
	hex_of((const unsigned char *)&g_vtctx[2], 8, g_clctx_hex, 128);
	hex_of((const void *)st_ctxact, 4, g_clctx_hex + 128, 128);
	g_clctx_site = (struct site_spec){ "CL_CTXACT", K_VSLOT, (uintptr_t)&g_vtctx[2], (uintptr_t)&g_vtctx[2], g_clctx_hex,
					   (uintptr_t)st_ctxact, g_clctx_hex + 128 };
	g_cl_ledge_site = &g_clctx_site;
	g_phys.vt = (uintptr_t)g_vtphys;
	g_clg.phys_pp = (uintptr_t)&g_phys_ptr;
	for (int i = 0; i < 3; i++)
		g_cltab[i].id = (uint16_t)(7 + i), g_cltab[i].rec = (uintptr_t)g_srec[i];
	g_cltab_b = (uintptr_t)g_cltab, g_cltab_e = (uintptr_t)(g_cltab + 4);
	g_clg.tab_pp[0] = (uintptr_t)&g_cltab_b, g_clg.tab_pp[1] = (uintptr_t)&g_cltab_e;
	g_ppc.vt = g_ppc2.vt = (uintptr_t)g_vtpc;
	g_ppc.grav = g_ppc2.grav = 1;
	for (int i = 0; i < 3; i++) {
		const char *nm = g_srec_names[i];
		g_srec[i][1] = 2;
		memcpy(g_srec[i] + 0x20, &nm, 8);
	}
	uint16_t top_id = 9;
	memcpy(g_topobj + CL_OBJ_SURF, &top_id, 2);
}
/* the player's PlayerMovement -> the proxy (pm+0x50) */
static void cl_set_proxy(st_pc_t *pc) { memcpy((char *)&g_ppm + CL_PM_PROXY, &pc, 8); }
/* a hand in XR space (m), the grip's orientation the identity: palm -X (right) / +X (left), fingers toward +Z (game) */
static void cl_hand(int h, float x, float y, float z) { g_lpose[h] = (XrPosef){ { 0, 0, 0, 1 }, { x, y, z } }; }
static void cl_grip(int h, float v) { g_lst.hand[h].squeeze = v; }
static int cl_frames(int n)
{
	for (int i = 0; i < n; i++)
		iframe();
	return n;
}
/* the right hand's world point now (the HAND line's mapping: B + R (m(p) - P0)) */
static sv_vec3 cl_hand_world(int h)
{
	const float p[3] = { g_lpose[h].position.x, g_lpose[h].position.y, g_lpose[h].position.z };
	return somvr_rig_point(&g_rig, g_fr.base, sv_xr_to_game_pos(p));
}

static void climb_tests(void)
{
	EXPECT(log_has("CONFIG climb=b (hand over hand): reach 25 cm (+10 behind) along the palm and fingers") &&
		       log_has("CLSITE CL_RAYCAST ok") && log_has("CLSITE CL_MOUNT ok"),
	       "climb: no CONFIG/CLSITE lines");
	float pos0[3], rot0[4];
	memcpy(pos0, g_pobj.pos, sizeof pos0), memcpy(rot0, g_pobj.rot, sizeof rot0);
	somvr_rig rig0 = g_rig;
	somvr_rig_cfg cfg0 = g_rigcfg;
	const sa_node *node0 = g_nodexf_node;
	g_views_fake = 0, g_rt_result = XR_SUCCESS, g_layout = g_rigcfg.layout = SOMVR_LAYOUT_A;
	g_rigcfg.base = SOMVR_BASE_HEAD, g_rigcfg.yaw = SOMVR_YAW_CAMERA, g_follow.on = 0, g_fb.on = 0, g_cb.mode = CB_OFF;
	g_hide.on = 0, g_hudq.on = 0, g_nodexf_node = NULL, g_head_up = 72.5f, g_cf.out_s = g_cf.in_s = 0;
	somvr_rig_init(&g_rig);
	g_rt_head[0] = g_rt_head[1] = g_rt_head[2] = 0, g_rt_q = (sv_quat){ 0, 0, 0, 1 };
	g_rm[0] = g_rm[1] = g_rm[2] = 0;
	g_pobj.pos[0] = 500, g_pobj.pos[1] = 100, g_pobj.pos[2] = -50;
	g_pobj.rot[0] = g_pobj.rot[1] = g_pobj.rot[2] = 0, g_pobj.rot[3] = 1;
	memset(g_clP, 0, sizeof g_clP);
	memcpy(g_clP, &g_player, sizeof g_player);
	g_clP[CL_P_FSM] = CL_FSM_SUPPORTED;
	g_pl = (st_player_t *)g_clP, g_ppm.owner = g_clP;
	cl_set_proxy(&g_ppc);
	g_wall = 1, g_wall_z = CLW_Z - CLW_R, g_wall_top = CLW_TOP;
	g_grav_flag = &g_ppc.grav, g_ground_y = 100;
	g_cb_test_t = 10000;
	g_xrt = g_vst.last_time + 1000000000;
	runtime_up();
	g_inp.on = 1, g_inp.lines = 0, g_inp.lsec = 0;
	layer_up();
	for (int h = 0; h < 2; h++)
		g_lst.hand[h].squeeze = 0, g_lst.hand[h].stick[0] = g_lst.hand[h].stick[1] = 0;
	cl_hand(SOMVR_HAND_R, 0.2f, -0.2f, -0.45f); /* world (520, 152.5, -5): 5 cm before the wall, the fingers toward it */
	cl_hand(SOMVR_HAND_L, -0.2f, -0.2f, -0.2f); /* (480, 152.5, -30): by his side */
	EXPECT(g_cl.surf == SOMVR_CLIMB_SURF_WALLS && log_has("held: every solid surface (SOMVR_CLIMB_SURF=walls)"),
	       "climb: the default surfaces %d", g_cl.surf);
	g_cl.surf = SOMVR_CLIMB_SURF_GAME; /* the stand-in's rock is not held by CanWallClimb (18e: walls and any) */
	EXPECT(g_cl.cfg.letgo == SOMVR_CLIMB_LETGO_HANG && log_has("both hands open: hang (Talion stays; A drops; no timeout"),
	       "climb: the default letgo %d", g_cl.cfg.letgo);
	g_cl.cfg.letgo = SOMVR_CLIMB_LETGO_FALL; /* both open = released (build 10's hang: 18g) */
	g_cl.cfg.cue = 0, g_cl.cfg.inside_cm = 0; /* no reach cue's rays, no inside ray (build 10: 18h) */
	g_cl.on = 1, g_cl.tid = 0, g_cl.next_log = 0, g_cl.lines = 0;
	somvr_climb_init(&g_cl.s);
	g_clw = (typeof(g_clw)){ 0, 0, 1, 0, 0 };
	atomic_fetch_add(&g_req_recenter, 1);
	cl_frames(4); /* the recentre; the head base settles at the new place */
	EXPECT(g_inp.live && g_fr.head_ok && g_ppc.grav == 1 && !g_cl.s.active && g_clw.calls == 0,
	       "climb: before: live %d head %d flag %d active %d casts %d", g_inp.live, g_fr.head_ok, g_ppc.grav, g_cl.s.active,
	       g_clw.calls);
	sv_vec3 hw = cl_hand_world(SOMVR_HAND_R);
	EXPECT(NEAR3(hw, 520, 152.5, -5, 1e-2), "climb: the right hand at %g %g %g", hw.x, hw.y, hw.z);
	/* 1. the stick walks (ours) before a climb */
	g_lst.hand[0].stick[1] = 1;
	cl_frames(2);
	EXPECT(poll22(SC_CMD_FORWARDAXIS) == 1.0f, "climb: the stick before: %g", poll22(SC_CMD_FORWARDAXIS));
	g_lst.hand[0].stick[1] = 0;
	cl_frames(2);
	/* 2. the right grip: a hold on the stone, gravity off, a haptic pulse */
	uint64_t hp0 = atomic_load(&g_in.n_hap_posts);
	cl_grip(SOMVR_HAND_R, 1);
	iframe();
	EXPECT(g_cl.s.active && g_cl.s.h[1].holding && g_ppc.grav == 0 && g_cl.act_pc == (uintptr_t)&g_ppc &&
		       g_cl.act_pm == (uintptr_t)&g_ppm && NEAR3(g_cl.s.h[1].hit, 520, 152.5, 0, 0.05),
	       "climb: grab: active %d flag %d hit %g %g %g", g_cl.s.active, g_ppc.grav, g_cl.s.h[1].hit.x, g_cl.s.h[1].hit.y,
	       g_cl.s.h[1].hit.z);
	EXPECT(log_has("CLIMB t=") && log_has("grab R: hand 520.0,152.5,-5.0 -> surface at 520.0,152.5,0.0") &&
		       log_has("'ST_Climbable_Stone' (id 7, record") && log_has("start: gravity flag 1 -> 0 on the proxy"),
	       "climb: no grab/start lines");
	EXPECT(g_clw.bad == 0 && g_clw.calls >= 2, "climb: the cast's query: %d bad of %d", g_clw.bad, g_clw.calls);
	EXPECT(atomic_load(&g_in.n_hap_posts) > hp0, "climb: no haptic pulse at the grab");
	/* 3. pull: the right hand 3 cm down a frame for 20 frames -> Talion 60 cm up (a frame late), the hand on its hold;
	 * gravity (the stand-in's 4 cm a tick while the flag is 1) never pulls him down; the stick held off */
	float y0 = g_pobj.pos[1];
	g_lst.hand[0].stick[1] = 1;
	float worst = 0;
	for (int i = 0; i < 20; i++) {
		g_lpose[1].position.y -= 0.03f;
		iframe();
		sv_vec3 w = cl_hand_world(SOMVR_HAND_R);
		worst = fmaxf(worst, fabsf(w.y - 152.5f));
	}
	iframe();
	EXPECT(NEAR(g_pobj.pos[1] - y0, 60, 0.05) && NEAR(g_pobj.pos[0], 500, 1e-3) && NEAR(g_pobj.pos[2], -50, 1e-3) &&
		       g_ppc.grav == 0 && g_cl.pm_moves >= 20,
	       "climb: pulled %g (x %g z %g) flag %d pm moves %llu", g_pobj.pos[1] - y0, g_pobj.pos[0], g_pobj.pos[2], g_ppc.grav,
	       (unsigned long long)g_cl.pm_moves);
	EXPECT(worst <= 3.05f && NEAR(cl_hand_world(SOMVR_HAND_R).y, 152.5, 0.05),
	       "climb: the hand left its hold by %g cm (now %g)", worst, cl_hand_world(SOMVR_HAND_R).y);
	EXPECT(poll22(SC_CMD_FORWARDAXIS) == 0.77f && g_cl.stick_ticks > 0, "climb: the stick walked while holding (%g)",
	       poll22(SC_CMD_FORWARDAXIS));
	g_lst.hand[0].stick[1] = 0;
	/* 4. the game sets the flag again (an attack's own): the next PM call writes 0 */
	g_ppc.grav = 1;
	uint64_t rs0 = g_cl.grav_resets;
	iframe();
	EXPECT(g_ppc.grav == 0 && g_cl.grav_resets == rs0 + 1 && NEAR(g_pobj.pos[1] - y0, 60, 0.05),
	       "climb: the flag set back: %d resets %llu", g_ppc.grav, (unsigned long long)(g_cl.grav_resets - rs0));
	/* 5. the collision clips: the hand pulled 30 cm toward the body (XR +z) asks Talion 30 cm toward the wall, the wall
	 * stops his root at -35 (15 of the 30) */
	for (int i = 0; i < 6; i++) {
		g_lpose[1].position.z += 0.05f;
		iframe();
	}
	cl_frames(3);
	EXPECT(NEAR(g_pobj.pos[2], CLW_Z - CLW_R, 1e-3) && NEAR(g_pobj.pos[1] - y0, 60, 0.05), "climb: the wall: z %g",
	       g_pobj.pos[2]);
	/* 6. hand over hand: the left reaches up to the head's height (inside Talion's capsule: the filter skips him) and
	 * grips; the right lets go; the left pulls 30 cm */
	cl_hand(SOMVR_HAND_L, -0.1f, 0.0f, g_lpose[1].position.z);
	iframe();
	cl_grip(SOMVR_HAND_L, 1);
	iframe();
	EXPECT(g_cl.s.h[0].holding && g_cl.s.driver == 0 && log_has("grab L: hand"), "climb: the left hold (driver %d)",
	       g_cl.s.driver);
	cl_grip(SOMVR_HAND_R, 0);
	iframe();
	EXPECT(g_cl.s.active && !g_cl.s.h[1].holding && log_has("let go R (grip open)"), "climb: the right let go");
	float y1 = g_pobj.pos[1];
	for (int i = 0; i < 10; i++) {
		g_lpose[0].position.y -= 0.03f;
		iframe();
	}
	iframe();
	EXPECT(NEAR(g_pobj.pos[1] - y1, 30, 0.05), "climb: the left pulled %g", g_pobj.pos[1] - y1);
	/* 7. the body follow is off while holding (the head 30 cm forward in the room: no move) */
	g_follow.on = 1;
	uint64_t fm0 = g_follow.moves;
	sv_vec3 r1 = root();
	g_rt_head[0] = 0.3f;
	cl_frames(4);
	EXPECT(g_follow.moves == fm0 && NEAR3(root(), r1.x, r1.y, r1.z, 1e-3), "climb: the follow moved Talion while holding");
	g_rt_head[0] = 0;
	cl_frames(2);
	g_follow.on = 0;
	/* 8. both open: gravity back (the flag 1), he falls to the ground (the stand-in's), the stick walks again */
	cl_grip(SOMVR_HAND_L, 0);
	iframe();
	EXPECT(!g_cl.s.active && g_ppc.grav == 1 && g_cl.s.last_end == SOMVR_CLIMB_END_RELEASED &&
		       log_has("end: released after") && log_has("gravity flag 0 -> 1"),
	       "climb: released: active %d flag %d end %d", g_cl.s.active, g_ppc.grav, g_cl.s.last_end);
	cl_frames(30);
	EXPECT(NEAR(g_pobj.pos[1], 100, 1e-3), "climb: he did not fall back (y %g)", g_pobj.pos[1]);
	g_lst.hand[0].stick[1] = 1;
	cl_frames(2);
	EXPECT(poll22(SC_CMD_FORWARDAXIS) == 1.0f, "climb: the stick after: %g", poll22(SC_CMD_FORWARDAXIS));
	g_lst.hand[0].stick[1] = 0;
	cl_frames(2);
	/* 9. Talion's own body: a hand at his chest (inside his capsule) with the fingers toward the wall: the cast's filter
	 * skips him; with the filter not asked (honor_cb 0) the ray steps past him */
	g_pobj.pos[2] = -50; /* back from the wall */
	cl_frames(2);
	cl_hand(SOMVR_HAND_R, 0.1f, -0.2f, -0.32f); /* (510, 152.5, -18): 33.5 cm from his axis */
	cl_hand(SOMVR_HAND_L, -0.2f, -0.2f, -0.2f);
	int cb0 = g_clw.cb_calls;
	cl_grip(SOMVR_HAND_R, 1);
	iframe();
	EXPECT(g_cl.s.active && g_clw.cb_calls > cb0 && g_cl.self_cb > 0 && NEAR(g_cl.s.h[1].hit.z, 0, 0.05),
	       "climb: the filter: active %d filter calls %d skips %llu", g_cl.s.active, g_clw.cb_calls - cb0,
	       (unsigned long long)g_cl.self_cb);
	cl_grip(SOMVR_HAND_R, 0);
	iframe();
	g_clw.honor_cb = 0;
	uint64_t st0 = g_cl.self_steps;
	cl_grip(SOMVR_HAND_R, 1);
	iframe();
	EXPECT(g_cl.s.active && g_cl.self_steps >= st0 + 2 && NEAR(g_cl.s.h[1].hit.z, 0, 0.05) &&
		       NEAR(g_cl.s.h[1].hit_dist, 28, 0.1),
	       "climb: stepping past Talion: active %d steps %llu dist %g", g_cl.s.active,
	       (unsigned long long)(g_cl.self_steps - st0), g_cl.s.h[1].hit_dist);
	g_clw.honor_cb = 1;
	cl_grip(SOMVR_HAND_R, 0);
	iframe();
	/* 10. rock (not CanWallClimb) and out of reach: no hold */
	cl_hand(SOMVR_HAND_R, 0.7f, -0.2f, -0.45f); /* x 570: rock */
	cl_grip(SOMVR_HAND_R, 1);
	iframe();
	EXPECT(!g_cl.s.active && g_ppc.grav == 1 && log_has("press R: not climbable: 'ST_Rock'"), "climb: rock");
	cl_grip(SOMVR_HAND_R, 0);
	iframe();
	cl_hand(SOMVR_HAND_R, 0.2f, -0.2f, -0.05f); /* 45 cm from the wall */
	cl_grip(SOMVR_HAND_R, 1);
	cl_frames(30);
	EXPECT(!g_cl.s.active && log_has("press R: nothing climbable within reach"), "climb: out of reach");
	cl_grip(SOMVR_HAND_R, 0);
	iframe();
	/* 11. stand down: the FSM leaves Supported (a fall): gravity back; the held grip does not grab again by itself */
	cl_hand(SOMVR_HAND_R, 0.2f, -0.2f, -0.45f);
	cl_grip(SOMVR_HAND_R, 1);
	iframe();
	EXPECT(g_cl.s.active && g_ppc.grav == 0, "climb: hold for the stand-down");
	g_clP[CL_P_FSM] = 8;
	iframe();
	EXPECT(!g_cl.s.active && g_ppc.grav == 1 && log_has("end: game: the FSM is not Supported (6)"),
	       "climb: stand down on FSM 8: active %d flag %d", g_cl.s.active, g_ppc.grav);
	g_clP[CL_P_FSM] = CL_FSM_SUPPORTED;
	cl_frames(3);
	EXPECT(!g_cl.s.active, "climb: the held grip grabbed again");
	cl_grip(SOMVR_HAND_R, 0);
	iframe();
	cl_grip(SOMVR_HAND_R, 1);
	iframe();
	EXPECT(g_cl.s.active, "climb: reopened: a hold");
	/* 11b. FSM 7 (an orc engages, a hit's recoil): SOMVR_CLIMB_FIGHT=hold keeps the climb on (gravity off); a new press in
	 * FSM 7 starts none; =drop ends it */
	g_clP[CL_P_FSM] = CL_FSM_COMBAT;
	cl_frames(3);
	EXPECT(g_cl.s.active && g_ppc.grav == 0 && g_cl.fight, "climb: FSM 7 holds: active %d flag %d", g_cl.s.active, g_ppc.grav);
	cl_grip(SOMVR_HAND_R, 0);
	cl_frames(2);
	EXPECT(!g_cl.s.active && g_ppc.grav == 1 && g_cl.s.last_end == SOMVR_CLIMB_END_RELEASED, "climb: FSM 7: released");
	cl_grip(SOMVR_HAND_R, 1);
	cl_frames(2);
	EXPECT(!g_cl.s.active, "climb: FSM 7: a new climb started");
	cl_grip(SOMVR_HAND_R, 0);
	g_clP[CL_P_FSM] = CL_FSM_SUPPORTED;
	cl_frames(2);
	cl_grip(SOMVR_HAND_R, 1);
	cl_frames(2);
	g_cl.fight = 0;
	g_clP[CL_P_FSM] = CL_FSM_COMBAT;
	cl_frames(2);
	EXPECT(!g_cl.s.active && g_ppc.grav == 1 && log_has("end: game: the FSM is Combat (7; SOMVR_CLIMB_FIGHT=drop)"),
	       "climb: FSM 7 with drop: active %d", g_cl.s.active);
	g_cl.fight = 1;
	g_clP[CL_P_FSM] = CL_FSM_SUPPORTED;
	cl_grip(SOMVR_HAND_R, 0);
	cl_frames(2);
	cl_grip(SOMVR_HAND_R, 1);
	iframe();
	EXPECT(g_cl.s.active, "climb: after FSM 7: a hold");
	/* 12. mounted: stand down */
	uintptr_t beast = 0x1234;
	memcpy(g_clP + CL_P_MOUNT, &beast, 8);
	iframe();
	EXPECT(!g_cl.s.active && g_ppc.grav == 1 && log_has("end: game: mounted"), "climb: mounted");
	beast = 0, memcpy(g_clP + CL_P_MOUNT, &beast, 8);
	cl_grip(SOMVR_HAND_R, 0);
	iframe();
	/* 13. the input lost (no new snapshot): stand down */
	cl_grip(SOMVR_HAND_R, 1);
	iframe();
	g_layer_live = 0;
	cl_frames(10);
	EXPECT(!g_cl.s.active && g_ppc.grav == 1 && g_cl.s.last_end == SOMVR_CLIMB_END_INPUT, "climb: input lost: end %d",
	       g_cl.s.last_end);
	g_layer_live = 1;
	cl_grip(SOMVR_HAND_R, 0);
	cl_frames(3);
	/* 14. a flat frame (no views): the climb ends there, gravity back */
	cl_grip(SOMVR_HAND_R, 1);
	iframe();
	atomic_store(&g_sh.xr.state, SOMVR_XRS_DEAD);
	iframe();
	atomic_store(&g_sh.xr.state, SOMVR_XRS_RUNNING);
	EXPECT(!g_cl.s.active && g_ppc.grav == 1 && log_has("end: outside: a flat frame"), "climb: flat frame");
	cl_grip(SOMVR_HAND_R, 0);
	cl_frames(2);
	/* 15. no climb step for 3 frames (fake views: no hands): the PM hook ends it */
	cl_grip(SOMVR_HAND_R, 1);
	iframe();
	EXPECT(g_cl.s.active, "climb: hold for the stale test");
	g_views_fake = 1;
	cl_frames(6);
	g_views_fake = 0;
	EXPECT(!g_cl.s.active && g_ppc.grav == 1 && log_has("end: outside: no climb step for 3 frames (the PM hook)"),
	       "climb: stale: active %d flag %d", g_cl.s.active, g_ppc.grav);
	cl_grip(SOMVR_HAND_R, 0);
	cl_frames(2);
	/* 16. the player's proxy changed under a climb: ended, the old proxy never written */
	cl_grip(SOMVR_HAND_R, 1);
	iframe();
	EXPECT(g_cl.s.active && g_ppc.grav == 0, "climb: hold for the proxy test");
	cl_set_proxy(&g_ppc2);
	iframe();
	EXPECT(!g_cl.s.active && g_ppc.grav == 0 && g_ppc2.grav == 1 && log_has("not written (not the climb's live proxy)"),
	       "climb: proxy changed: active %d old %d new %d", g_cl.s.active, g_ppc.grav, g_ppc2.grav);
	cl_set_proxy(&g_ppc);
	g_ppc.grav = 1;
	cl_grip(SOMVR_HAND_R, 0);
	cl_frames(2);
	/* 17. the top, lift: Talion 1.5 m up the wall (no gravity in the stand-in now), an edge hold 10 cm under the top
	 * (y 390), pulled until the head is 10 above it, then the mod's pull-up to the head 185 over the top and 40 cm
	 * toward the wall over its edge; gravity back */
	g_grav_flag = NULL;
	g_pobj.pos[1] = 250;
	cl_frames(3);
	g_cl.cfg.top_mode = SOMVR_CLIMB_TOP_LIFT;
	cl_hand(SOMVR_HAND_R, 0.2f, 0.675f, -0.45f); /* world y 390 */
	cl_grip(SOMVR_HAND_R, 1);
	iframe();
	EXPECT(g_cl.s.active && g_cl.s.h[1].edge && g_cl.s.h[1].top_found && NEAR(g_cl.s.h[1].top_y, CLW_TOP, 0.05),
	       "climb: the edge hold: edge %d top %g", g_cl.s.h[1].edge, g_cl.s.h[1].top_y);
	int n = 0;
	while (g_cl.s.phase == SOMVR_CLIMB_PH_HOLD && n++ < 60) {
		g_lpose[1].position.y -= 0.03f;
		iframe();
	}
	EXPECT(g_cl.s.phase == SOMVR_CLIMB_PH_LIFT && log_has("top: the head over the hold's top (head 412.5) -> lift") &&
		       g_fr.head[1] >= CLW_TOP + 10,
	       "climb: the top after %d frames (head %g)", n, g_fr.head[1]);
	cl_grip(SOMVR_HAND_R, 0); /* letting go in the lift changes nothing */
	for (n = 0; n < 120 && g_cl.s.active; n++)
		iframe();
	cl_frames(2);
	EXPECT(!g_cl.s.active && g_cl.s.last_end == SOMVR_CLIMB_END_LIFTED && g_ppc.grav == 1 &&
		       NEAR(g_pobj.pos[1] + 72.5f, CLW_TOP + 185, 0.5) && NEAR(g_pobj.pos[2], -50 + 40, 0.05),
	       "climb: lifted: end %s flag %d head %g z %g", somvr_climb_end_name(g_cl.s.last_end), g_ppc.grav,
	       g_pobj.pos[1] + 72.5f, g_pobj.pos[2]);
	/* 17b. the top, push (the default): the edge hold (y 390) pulled until the head is 10 over the top: the climb goes
	 * on ("-> push"), 90 cm more up 1:1 (the root over the top: the stand-in's wall lets it past z -35 then), then the
	 * hand toward the body (XR +z) moves Talion over the wall: the head (over the root) 30 cm past the hold's face =
	 * "end: over", gravity back */
	g_pobj.pos[1] = 250, g_pobj.pos[2] = -50;
	cl_frames(3);
	g_cl.cfg.top_mode = SOMVR_CLIMB_TOP_PUSH;
	cl_hand(SOMVR_HAND_R, 0.2f, 0.675f, -0.45f);
	cl_grip(SOMVR_HAND_R, 1);
	iframe();
	n = 0;
	while (g_cl.s.active && !g_cl.s.topped && n++ < 60) {
		g_lpose[1].position.y -= 0.03f;
		iframe();
	}
	EXPECT(g_cl.s.active && g_cl.s.topped && log_has("top: the head over the hold's top (head 412.5) -> push"),
	       "climb: push: the top after %d frames (active %d)", n, g_cl.s.active);
	for (int i = 0; i < 30; i++) {
		g_lpose[1].position.y -= 0.03f;
		iframe();
	}
	cl_frames(2);
	EXPECT(g_cl.s.active && g_pobj.pos[1] > CLW_TOP && NEAR(g_pobj.pos[2], -50, 1e-3), "climb: push: up to %g",
	       g_pobj.pos[1]);
	for (n = 0; n < 80 && g_cl.s.active; n++) {
		g_lpose[1].position.z += 0.02f;
		iframe();
	}
	cl_frames(2);
	EXPECT(!g_cl.s.active && g_cl.s.last_end == SOMVR_CLIMB_END_OVER && g_ppc.grav == 1 && log_has("end: over after") &&
		       g_pobj.pos[2] >= 30 - 1e-3 && g_pobj.pos[2] < 32.1f && g_cl.s.overs == 1,
	       "climb: push: over after %d frames: end %s z %g flag %d", n, somvr_climb_end_name(g_cl.s.last_end),
	       g_pobj.pos[2], g_ppc.grav);
	cl_grip(SOMVR_HAND_R, 0);
	cl_frames(2);
	/* 17c. push's edge step through the root's read: at the top (the root 337.5, under the stand-in wall's 400) the hand
	 * toward the body at once: the wall keeps the root at z -35, the blocked part lifts him (the per-second line's
	 * "edge step") until the root clears 400, then over */
	g_pobj.pos[1] = 250, g_pobj.pos[2] = -50;
	cl_frames(3);
	cl_hand(SOMVR_HAND_R, 0.2f, 0.675f, -0.45f);
	cl_grip(SOMVR_HAND_R, 1);
	iframe();
	n = 0;
	while (g_cl.s.active && !g_cl.s.topped && n++ < 60) {
		g_lpose[1].position.y -= 0.03f;
		iframe();
	}
	double rc0 = g_cl.s.ramp_cm;
	float ry0 = g_pobj.pos[1];
	for (n = 0; n < 200 && g_cl.s.active; n++) {
		g_lpose[1].position.z += 0.02f;
		iframe();
	}
	cl_frames(2);
	EXPECT(!g_cl.s.active && g_cl.s.last_end == SOMVR_CLIMB_END_OVER && g_cl.s.ramp_cm - rc0 > 50 && ry0 < CLW_TOP &&
		       g_pobj.pos[1] >= CLW_TOP - 1e-3 && g_pobj.pos[1] < CLW_TOP + 4.1f && g_pobj.pos[2] >= 30 - 1e-3 &&
		       g_ppc.grav == 1,
	       "climb: the edge step: end %s after %d frames, lifted %g (root %g -> %g, z %g)",
	       somvr_climb_end_name(g_cl.s.last_end), n, g_cl.s.ramp_cm - rc0, ry0, g_pobj.pos[1], g_pobj.pos[2]);
	cl_grip(SOMVR_HAND_R, 0);
	cl_frames(2);
	/* 18. the top, cmd: the stock LedgePullUp (27) pulsed through the binding layer, then released after 0.6 s */
	g_pobj.pos[1] = 250, g_pobj.pos[2] = -50;
	cl_frames(3);
	bind_cmd(CL_CMD_PULLUP, 2010, 1, 0x20, 1);
	g_cl.cfg.top_mode = SOMVR_CLIMB_TOP_CMD;
	cl_hand(SOMVR_HAND_R, 0.2f, 0.675f, -0.45f);
	cl_grip(SOMVR_HAND_R, 1);
	iframe();
	int ev0 = g_ninev;
	for (n = 0; g_cl.s.phase == SOMVR_CLIMB_PH_HOLD && n < 60; n++) {
		g_lpose[1].position.y -= 0.03f;
		iframe();
	}
	cl_frames(4);
	EXPECT(g_cl.s.phase == SOMVR_CLIMB_PH_CMD && nev('N', CL_CMD_PULLUP, ev0) == 1 && g_cl.pulses == 3 &&
		       log_has("VIN press cmd=27 LedgePullUp"),
	       "climb: cmd: phase %d ON events %d pulses %llu", g_cl.s.phase, nev('N', CL_CMD_PULLUP, ev0),
	       (unsigned long long)g_cl.pulses);
	for (n = 0; n < 90 && g_cl.s.active; n++)
		iframe();
	EXPECT(!g_cl.s.active && g_cl.s.last_end == SOMVR_CLIMB_END_CMD && g_ppc.grav == 1 && nev('F', CL_CMD_PULLUP, ev0) == 1,
	       "climb: cmd ended %s, OFF events %d", somvr_climb_end_name(g_cl.s.last_end), nev('F', CL_CMD_PULLUP, ev0));
	cl_grip(SOMVR_HAND_R, 0);
	cl_frames(2);
	/* 18b. SOMVR_HANDS_TEST (headless drives): the controller's inputs inactive (no controller), its squeeze ignored;
	 * the test squeeze `hands squeeze R 1` grabs and pulls, `hands squeeze R 0` lets go (released, gravity back);
	 * without the test mode the word is ignored and nothing grabs */
	g_pobj.pos[1] = 100, g_pobj.pos[2] = -50;
	g_cl.cfg.top_mode = SOMVR_CLIMB_TOP_RELEASE;
	cl_frames(3);
	int bt0 = g_bd.test, bton0[2] = { g_bd.t_on[0], g_bd.t_on[1] };
	uint32_t act0 = g_lst.hand[1].active;
	g_bd.t_on[0] = g_bd.t_on[1] = 0, g_bd_req.on[0] = g_bd_req.on[1] = 0;
	g_lst.hand[1].active = SOMVR_HACT_POSE;
	cl_hand(SOMVR_HAND_R, 0.2f, -0.2f, -0.45f); /* 5 cm before the stone */
	cl_grip(SOMVR_HAND_R, 1);                  /* the controller's squeeze, its inputs inactive */
	g_bd.test = 0;
	const char *bw = bd_word("squeeze R 1");
	cl_frames(3);
	EXPECT(!g_cl.s.active && !strcmp(bw, "hands (SOMVR_HANDS_TEST is off: ignored)"),
	       "climb: an inactive controller's squeeze or the word without the test mode grabbed (%s)", bw);
	cl_grip(SOMVR_HAND_R, 0);
	g_bd.test = 1;
	bw = bd_word("squeeze R 1");
	cl_frames(2);
	EXPECT(!strcmp(bw, "hands squeeze") && g_cl.s.active && g_cl.s.h[1].holding && g_ppc.grav == 0 &&
		       log_has("BODY test squeeze R 1.00"),
	       "climb: the test squeeze: %s, active %d holding %d flag %d", bw, g_cl.s.active, g_cl.s.h[1].holding, g_ppc.grav);
	float yt = g_pobj.pos[1];
	for (int i = 0; i < 10; i++) {
		g_lpose[1].position.y -= 0.03f;
		iframe();
	}
	iframe();
	EXPECT(NEAR(g_pobj.pos[1] - yt, 30, 0.05), "climb: the test squeeze pulled %g", g_pobj.pos[1] - yt);
	bd_word("squeeze R 0");
	cl_frames(2);
	EXPECT(!g_cl.s.active && g_cl.s.last_end == SOMVR_CLIMB_END_RELEASED && g_ppc.grav == 1,
	       "climb: the test squeeze opened: active %d end %s flag %d", g_cl.s.active, somvr_climb_end_name(g_cl.s.last_end),
	       g_ppc.grav);
	bd_word("squeeze R -1");
	cl_frames(1);
	g_bd.test = bt0, g_bd.t_on[0] = bton0[0], g_bd.t_on[1] = bton0[1], g_lst.hand[1].active = act0;
	/* 18c. the trigger word "climb probe [up|table]": from the view's head (500, 172.5, -50), heading 0 (+z, at the wall 50 cm
	 * ahead): the stone ahead and 30/60 deg left, the rock 60 deg right (x 587 > 560), nothing to the sides and behind
	 * (Talion skipped); up: the face until the head + 225 (y 397.5), nothing over the top */
	g_pobj.pos[1] = 100, g_pobj.pos[2] = -50;
	g_clw.honor_cb = 1;
	cl_frames(3);
	EXPECT(!strcmp(cl_word("probe"), "climb probe") &&
		       !strcmp(cl_word("prob"), "climb (not probe [up|table|edge], state, watch [off], writes [S], contacts, trace S, "
						"ledges off|game, loyaw game|ours, overhang on|off, unpin on|off, pullover on|off, through "
						"on|off, face on|off, ramp X: ignored)") &&
		       !strcmp(cl_word("probe down"), "climb (not probe [up|table|edge], state, watch [off], writes [S], contacts, "
						      "trace S, ledges off|game, loyaw game|ours, overhang on|off, unpin on|off, pullover on|off, "
						      "through on|off, face on|off, ramp X: ignored)"),
	       "climb: the probe word");
	float oh0 = g_cl.cfg.overhang; /* build 15b's test aid: SOMVR_CLIMB_OVERHANG live */
	EXPECT(!strcmp(cl_word("overhang on"), "climb overhang on") && g_cl.cfg.overhang == 1.0f &&
		       !strcmp(cl_word("overhang off"), "climb overhang off") && g_cl.cfg.overhang == 0.0f &&
		       log_has("overhang on (the trigger word, a test aid)"),
	       "climb: the overhang word");
	int ohm0 = g_cl.cfg.oh_mantle;
	EXPECT(!strcmp(cl_word("overhang step"), "climb overhang step") && g_cl.cfg.overhang == 1.0f && !g_cl.cfg.oh_mantle &&
		       !strcmp(cl_word("overhang on"), "climb overhang on") && g_cl.cfg.oh_mantle,
	       "climb: the overhang word's step (build 16: no mantle)");
	g_cl.cfg.overhang = oh0, g_cl.cfg.oh_mantle = ohm0;
	EXPECT(!strcmp(cl_word("writes 2"), "climb writes") && atomic_load(&g_cw.req) == 2u &&
		       !strcmp(cl_word("writes 99"), "climb writes (SOMVR_CLIMB off or not 1..20 s: nothing)"),
	       "climb: the writes word (a test aid)");
	atomic_store(&g_cw.req, 0u);
	uint64_t pc0 = g_cl.casts;
	iframe();
	EXPECT(log_has("CLIMB probe t=") && log_has("at head +0 cm: bearing (deg, + = right) = the distance (cm): +0=50C +30=58C "
						    "+60=100n +90=- +120=- +150=- +180=- -150=- -120=- -90=- -60=100C -30=58C") &&
		       log_has("at head -100 cm: bearing (deg, + = right) = the distance (cm): +0=50C") &&
		       log_has("CLIMB probe surfaces: id 7 'ST_Climbable_Stone' CanWallClimb, held, id 8 'ST_Rock' not held (49 casts); "
			       "the ground under Talion's root: none within 4 m") &&
		       g_cl.casts - pc0 == 49 && !g_cl.s.active,
	       "climb: the probe: %llu casts", (unsigned long long)(g_cl.casts - pc0));
	cl_word("probe up");
	iframe();
	EXPECT(log_has("up: bearing 0, head +cm = the distance (cm): -100=50C") && log_has(" +225=50C +250=- +275=-") &&
		       g_cl.probes == 2,
	       "climb: the probe up (probes %llu)", (unsigned long long)g_cl.probes);
	/* 18c'. `climb probe edge` (SOMVR_CLIMB_DESCENT's geometry, a tower top's edge): from the root (500, 100, -50) along the
	 * heading (+z): the profile down, the face back from 120 cm ahead, a wall at the edge: the stone 50 cm ahead */
	EXPECT(!strcmp(cl_word("probe edge"), "climb probe edge"), "climb: the edge probe word");
	iframe();
	EXPECT(log_has("CLIMB probe edge t=") && log_has("root 500.0,100.0,-50.0 heading 0.0 deg; the profile: cm ahead = the "
							 "landing, cm under the root (60 m down): +0=") &&
		       log_has("a wall at the edge: root +cm = the distance ahead (2 m): -80=50C#7 -40=50C#7 +0=50C#7") &&
		       log_has("CLIMB probe edge surfaces: #7 'ST_Climbable_Stone' CanWallClimb, held") && g_cl.probes == 3,
	       "climb: the edge probe (probes %llu)", (unsigned long long)g_cl.probes);
	/* 18d. `climb probe table`: the stand-in table (ids 7, 8, 9 and an empty entry) with the game's CanWallClimb */
	EXPECT(!strcmp(cl_word("probe table"), "climb probe table"), "climb: the table word");
	iframe();
	EXPECT(log_has("CLIMB table CanWallClimb: 7 'ST_Climbable_Stone', 9 'ST_Top'") &&
		       log_has("CLIMB table not climbable: 8 'ST_Rock'") && log_has("CLIMB table: 4 records (holders") &&
		       log_has("2 CanWallClimb, 1 not") && g_cl.probes == 4,
	       "climb: the table (probes %llu)", (unsigned long long)g_cl.probes);
	/* 18f. low_cm: a hold at y 40 (60 cm under the root at 100) starts no climb */
	g_pobj.pos[1] = 100, g_pobj.pos[2] = -50;
	cl_frames(3);
	cl_hand(SOMVR_HAND_R, 0.2f, -1.325f, -0.45f);
	cl_grip(SOMVR_HAND_R, 1);
	cl_frames(2);
	EXPECT(!g_cl.s.active && g_cl.s.lows == 1 && log_has("cm under Talion's root (over SOMVR_CLIMB_LOW_CM 50): no climb from the "
							      "ground 'ST_Climbable_Stone'"),
	       "climb: low: active %d lows %llu", g_cl.s.active, (unsigned long long)g_cl.s.lows);
	cl_grip(SOMVR_HAND_R, 0);
	cl_frames(2);
	/* 18e. SOMVR_CLIMB_SURF: walls (the default) and any hold the rock (x 580: not CanWallClimb) and say so; game does not
	 * (the probe's letter h) */
	for (int m = SOMVR_CLIMB_SURF_WALLS; m <= SOMVR_CLIMB_SURF_ANY; m++) {
		g_cl.surf = m;
		int nc0 = (int)g_cl.s.noclimb;
		cl_hand(SOMVR_HAND_R, 0.8f, -0.2f, -0.45f);
		cl_grip(SOMVR_HAND_R, 1);
		cl_frames(2);
		int held = g_cl.s.active && g_cl.s.h[1].holding;
		EXPECT(m == SOMVR_CLIMB_SURF_GAME ? !held && (int)g_cl.s.noclimb == nc0 + 1
						  : held && log_has("'ST_Rock' (id 8") &&
							    log_has(m == SOMVR_CLIMB_SURF_ANY ? "not CanWallClimb, held (SOMVR_CLIMB_SURF=any); edge 0"
											   : "not CanWallClimb, held (SOMVR_CLIMB_SURF=walls); edge 0"),
		       "climb: surfaces %s: the rock held %d", g_cl_surf_n[m], held);
		cl_grip(SOMVR_HAND_R, 0);
		cl_frames(2);
	}
	cl_word("probe");
	iframe();
	EXPECT(log_has("+0=50C +30=58C +60=100h +90=-"), "climb: the probe's h (any)");
	g_cl.surf = SOMVR_CLIMB_SURF_WALLS;
	/* 18g. build 10's hang (SOMVR_CLIMB_LETGO=hang, the default): both grips open = Talion stays (the PM hook keeps the
	 * flag 0, the stand-in's gravity never pulls), the stick still held off; A (the right hand's lower button) drops:
	 * gravity back, he falls */
	g_cl.cfg.letgo = SOMVR_CLIMB_LETGO_HANG;
	g_grav_flag = &g_ppc.grav, g_ground_y = 100;
	g_pobj.pos[1] = 100, g_pobj.pos[2] = -50;
	cl_frames(3);
	cl_hand(SOMVR_HAND_R, 0.2f, -0.2f, -0.45f);
	cl_grip(SOMVR_HAND_R, 1);
	iframe();
	for (int i = 0; i < 10; i++) {
		g_lpose[1].position.y -= 0.03f;
		iframe();
	}
	iframe();
	float yh = g_pobj.pos[1];
	cl_grip(SOMVR_HAND_R, 0);
	g_lst.hand[0].stick[1] = 1;
	uint64_t sk0 = g_cl.stick_ticks;
	cl_frames(30);
	EXPECT(g_cl.s.active && g_cl.s.phase == SOMVR_CLIMB_PH_HANG && g_ppc.grav == 0 && NEAR(g_pobj.pos[1], yh, 1e-3) &&
		       yh > 125 && log_has("hang: no hand holds: Talion stays where he is") && g_cl.stick_ticks > sk0,
	       "climb: hang: active %d phase %d flag %d y %g (was %g)", g_cl.s.active, g_cl.s.phase, g_ppc.grav, g_pobj.pos[1], yh);
	g_lst.hand[0].stick[1] = 0;
	g_lst.hand[1].buttons |= SOMVR_BTN_LOWER;
	iframe();
	EXPECT(!g_cl.s.active && g_cl.s.last_end == SOMVR_CLIMB_END_DROP && g_ppc.grav == 1 && log_has("end: drop: A pressed after"),
	       "climb: A drops: active %d end %s flag %d", g_cl.s.active, somvr_climb_end_name(g_cl.s.last_end), g_ppc.grav);
	g_lst.hand[1].buttons &= ~SOMVR_BTN_LOWER;
	cl_frames(30);
	EXPECT(NEAR(g_pobj.pos[1], 100, 1e-3), "climb: after the drop he fell (y %g)", g_pobj.pos[1]);
	/* 18h. the reach cue (SOMVR_CLIMB_CUE=1): the open right hand raised over the root comes 5 cm before the wall: one
	 * light tick (a haptic post); 40 cm off, a press: a near miss, the double buzz; 15 cm inside the wall (the fingers'
	 * ray starts in it): held by the head's ray (SOMVR_CLIMB_INSIDE_CM 30) */
	g_cl.cfg.cue = 1, g_cl.cfg.inside_cm = 30;
	cl_hand(SOMVR_HAND_R, 0.2f, -0.2f, 0.0f); /* world z -50: 50 cm off */
	cl_frames(40);
	uint64_t ct0 = g_cl.cue_ticks, hp1 = atomic_load(&g_in.n_hap_posts);
	cl_hand(SOMVR_HAND_R, 0.2f, -0.2f, -0.45f);
	cl_frames(10);
	EXPECT(g_cl.cue_ticks == ct0 + 1 && atomic_load(&g_in.n_hap_posts) > hp1 && !g_cl.s.active,
	       "climb: the reach cue: ticks %llu", (unsigned long long)(g_cl.cue_ticks - ct0));
	cl_frames(30);
	EXPECT(g_cl.cue_ticks == ct0 + 1, "climb: the cue ticked again while the hand stayed");
	cl_hand(SOMVR_HAND_R, 0.2f, -0.2f, -0.10f); /* world z -40: 40 cm off, 15 past the reach */
	cl_frames(40);
	uint64_t mb0 = g_cl.miss_buzzes;
	cl_grip(SOMVR_HAND_R, 1);
	cl_frames(40);
	EXPECT(!g_cl.s.active && g_cl.miss_buzzes == mb0 + 1 && log_has("the nearest surface 15.0 cm past the reach (fingers ray); "
										"allowed; the miss buzz)"),
	       "climb: the near miss: buzzes %llu", (unsigned long long)(g_cl.miss_buzzes - mb0));
	cl_grip(SOMVR_HAND_R, 0);
	cl_frames(3);
	cl_hand(SOMVR_HAND_R, 0.2f, -0.2f, -0.65f); /* world z +15: 15 cm inside the wall */
	cl_frames(3);
	cl_grip(SOMVR_HAND_R, 1);
	cl_frames(2);
	EXPECT(g_cl.s.active && g_cl.s.h[1].holding && g_cl.s.h[1].via == SOMVR_CLIMB_VIA_INSIDE && log_has("inside: the hand 1"),
	       "climb: inside the wall: active %d via %d", g_cl.s.active, g_cl.s.h[1].via);
	cl_grip(SOMVR_HAND_R, 0);
	cl_frames(3);
	g_lst.hand[1].buttons |= SOMVR_BTN_LOWER;
	cl_frames(2);
	g_lst.hand[1].buttons &= ~SOMVR_BTN_LOWER;
	cl_frames(30);
	EXPECT(!g_cl.s.active && NEAR(g_pobj.pos[1], 100, 1e-3), "climb: dropped after the inside hold (y %g)", g_pobj.pos[1]);
	g_cl.cfg.cue = 0, g_cl.cfg.inside_cm = 0, g_cl.cfg.letgo = SOMVR_CLIMB_LETGO_FALL;
	g_grav_flag = NULL;
	/* 18i. SOMVR_CLIMB_LEDGES (CL_CTXACT: PlayerBody's context-action slot, patched at start): game = the original
	 * call with its arguments; the word `climb ledges off` = 0 without it; back with `climb ledges game` */
	typedef uint64_t (*ctx_fn)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
	EXPECT(g_cl.ledge_hook && g_cl.ledges == 1 && g_vtctx[2] == (void *)cl_ctxact_vf && log_has("HOOK CL_CTXACT ok vslot") &&
		       log_has("the game's ledges as the game's (SOMVR_CLIMB_LEDGES=game)"),
	       "climb: the ledge slot: hook %d ledges %d", g_cl.ledge_hook, g_cl.ledges);
	int cc0 = g_ctxw.calls;
	uint64_t rv = ((ctx_fn)g_vtctx[2])(11, 22, 33, 44);
	EXPECT(rv == 1 && g_ctxw.calls == cc0 + 1 && g_ctxw.a[0] == 11 && g_ctxw.a[3] == 44, "climb: ledges game: the original");
	EXPECT(!strcmp(cl_word("ledges off"), "climb ledges off") && g_cl.ledges == 0 &&
		       log_has("ledges off (the trigger word): the game's context actions never found for Talion"),
	       "climb: the ledges word");
	uint64_t b0 = atomic_load(&g_cl.ctx_block);
	rv = ((ctx_fn)g_vtctx[2])(11, 22, 33, 44);
	EXPECT(rv == 0 && g_ctxw.calls == cc0 + 1 && atomic_load(&g_cl.ctx_block) == b0 + 1, "climb: ledges off: nothing found");
	EXPECT(!strcmp(cl_word("ledges game"), "climb ledges game") && g_cl.ledges == 1 && ((ctx_fn)g_vtctx[2])(1, 2, 3, 4) == 1 &&
		       g_ctxw.calls == cc0 + 2,
	       "climb: ledges game again");
	EXPECT(!strcmp(cl_word("loyaw game"), "climb loyaw game") && g_cl.loyaw && !strcmp(cl_word("loyaw ours"), "climb loyaw ours") &&
		       !g_cl.loyaw && !strcmp(cl_word("state"), "climb state"),
	       "climb: the loyaw and state words");
	iframe();
	EXPECT(log_has("CLIMB state t=") && log_has("(asked): fsm 6 life") && log_has("ledges game (context finds passed"),
	       "climb: the state line");
	/* 18j. build 13's fling (SOMVR_CLIMB_FLING=on, re-xr-pipeline.md 32.14): the right hand pulled 4 cm a frame and let
	 * go while still moving: our own arc through the PM hook (the flag kept 0, the stand-in's gravity never pulls),
	 * 360 cm/s up = 66 cm, slowing, then the apex hang (no fall); A drops. A slow let-go (1 cm a frame): the hang */
	g_cl.cfg.fling = 1, g_cl.cfg.letgo = SOMVR_CLIMB_LETGO_HANG, g_cl.cfg.cue = 0, g_cl.cfg.inside_cm = 0;
	g_grav_flag = &g_ppc.grav, g_ground_y = 100;
	g_pobj.pos[1] = 100, g_pobj.pos[2] = -50;
	cl_frames(3);
	cl_hand(SOMVR_HAND_R, 0.2f, -0.2f, -0.45f);
	cl_grip(SOMVR_HAND_R, 1);
	iframe();
	for (int i = 0; i < 10; i++) {
		g_lpose[1].position.y -= 0.04f;
		iframe();
	}
	uint64_t fhp0 = atomic_load(&g_in.n_hap_posts);
	g_lpose[1].position.y -= 0.04f;
	cl_grip(SOMVR_HAND_R, 0);
	iframe();
	float yf0 = g_pobj.pos[1];
	EXPECT(g_cl.s.active && g_cl.s.phase == SOMVR_CLIMB_PH_FLING && g_cl.s.flings == 1 && NEAR(g_cl.s.fl_v0.y, 360, 6) &&
		       log_has("fling launch R: v=3") && log_has("(up 3") && atomic_load(&g_in.n_hap_posts) > fhp0,
	       "climb: fling: launch phase %d v0 %g", g_cl.s.phase, g_cl.s.fl_v0.y);
	cl_frames(60);
	float yfa = g_pobj.pos[1];
	EXPECT(g_cl.s.active && g_cl.s.phase == SOMVR_CLIMB_PH_HANG && g_cl.s.fl_why == SOMVR_CLIMB_FL_APEX && g_ppc.grav == 0 &&
		       yfa - yf0 > 55 && yfa - yf0 < 75 && log_has("fling apex hang (the arc's top) after 0.3"),
	       "climb: fling: the apex hang: phase %d why %d flag %d rose %g", g_cl.s.phase, g_cl.s.fl_why, g_ppc.grav, yfa - yf0);
	cl_frames(30);
	EXPECT(NEAR(g_pobj.pos[1], yfa, 1e-3), "climb: fling: at the apex %g -> %g (a fall)", yfa, g_pobj.pos[1]);
	g_lst.hand[1].buttons |= SOMVR_BTN_LOWER;
	iframe();
	EXPECT(!g_cl.s.active && g_cl.s.last_end == SOMVR_CLIMB_END_DROP && g_ppc.grav == 1, "climb: fling: A drops");
	g_lst.hand[1].buttons &= ~SOMVR_BTN_LOWER;
	cl_frames(40);
	g_pobj.pos[1] = 100, g_pobj.pos[2] = -50;
	cl_frames(3);
	cl_hand(SOMVR_HAND_R, 0.2f, -0.2f, -0.45f);
	cl_grip(SOMVR_HAND_R, 1);
	iframe();
	for (int i = 0; i < 20; i++) {
		g_lpose[1].position.y -= 0.01f;
		iframe();
	}
	g_lpose[1].position.y -= 0.01f;
	cl_grip(SOMVR_HAND_R, 0);
	iframe();
	iframe();
	yf0 = g_pobj.pos[1];
	cl_frames(30);
	EXPECT(g_cl.s.active && g_cl.s.phase == SOMVR_CLIMB_PH_HANG && g_cl.s.flings == 1 && NEAR(g_pobj.pos[1], yf0, 1e-3) &&
		       log_has("fling none R: up 9"),
	       "climb: fling: a slow let-go hangs (phase %d, flings %llu)", g_cl.s.phase, (unsigned long long)g_cl.s.flings);
	g_lst.hand[1].buttons |= SOMVR_BTN_LOWER;
	iframe();
	g_lst.hand[1].buttons &= ~SOMVR_BTN_LOWER;
	cl_frames(40);
	g_cl.cfg.fling = 0;
	/* 19. off: nothing */
	g_cl.on = 0;
	cl_hand(SOMVR_HAND_R, 0.2f, -0.2f, -0.45f);
	cl_grip(SOMVR_HAND_R, 1);
	int c0 = g_clw.calls;
	cl_frames(3);
	EXPECT(!g_cl.s.active && g_clw.calls == c0 && g_ppc.grav == 1, "climb off: something ran");
	cl_grip(SOMVR_HAND_R, 0);
	g_cl.on = 1;
	cl_end_log();
	EXPECT(log_has("CLIMB END climbs=") && log_has("gravity off ") && log_has("Talion skipped ") &&
		       log_has("flings 1 (none 1, pulses 1) catches 0 apexes 1 (blocked 0)"),
	       "climb: no END line");
	EXPECT(g_clw.bad == 0, "climb: %d casts with a bad query", g_clw.bad);
	printf("climb  hand over hand on stand-ins: a hold on the stone (gravity flag 1 -> 0, a pulse), 60 cm pulled with the "
	       "hand on its hold, the flag kept 0, the wall clips, the left takes over, no follow, released (flag 1, he falls), "
	       "the stick off while holding; Talion skipped by the filter and by stepping; rock, out of reach; stand-downs "
	       "(FSM 8, mount, input, flat, stale, the proxy changed: the old one untouched); the top: lift and cmd "
	       "(LedgePullUp); %llu casts\n", (unsigned long long)g_cl.casts);
	/* 18m. SOMVR_CLIMB_UNPIN (build 16, re-xr-pipeline.md 50.1): the PM hook's ends with a stand-in manifold: a wall
	 * 6 cm into the capsule at -x (its normal +x) and the floor; a tick asked +10 cm x and not done = the proxy +8 cm
	 * (the step's most); -x (into that wall): refused; no contact inside: nothing; off: nothing */
	{
		int on0 = g_un.on, ok0 = g_un.ok;
		somvr_unpin_cfg uc0 = g_un.cfg;
		uintptr_t pm0 = g_cl.pm, obj0 = g_cl.obj;
		int tid0 = g_cl.tid, fsm0 = g_cl.fsm, mnt0 = g_cl.mounted;
		g_cl.fsm = CL_FSM_SUPPORTED, g_cl.mounted = 0;
		g_cl.on = 1, g_un.on = 1, g_un.ok = 1, g_un.cfg.hold_s = 0, g_un.have = 0, g_un.lines = 0;
		somvr_unpin_init(&g_un.s);
		g_cl.pm = (uintptr_t)&g_ppm, g_cl.obj = (uintptr_t)&g_pobj, g_cl.tid = my_tid();
		g_ppc.proxy = (uintptr_t)&g_pxy;
		memset(g_pxy_man, 0, sizeof g_pxy_man);
		float wall[8] = { 4.65f, 1.0f, -0.5f, 0, 1, 0, 0, -0.06f }, floor_[8] = { 5.0f, 0.0f, -0.5f, 0, 0, 1, 0, 0.004f };
		memcpy(g_pxy_man[0], wall, sizeof wall), memcpy(g_pxy_man[1], floor_, sizeof floor_);
		g_pxy.data = (uintptr_t)g_pxy_man, g_pxy.n = 2, g_pxy.cap = 4, g_pxy.sets = 0;
		g_pxy.pos[0] = 500, g_pxy.pos[1] = 30, g_pxy.pos[2] = -50;
		float r0[3] = { 500, 100, -50 }, xf[7] = { 510, 100, -50, 0, 0, 0, 1 };
		memcpy(g_pobj.pos, r0, sizeof r0);
		int wall0 = g_wall;
		g_wall = 0;
		un_pre((uintptr_t)&g_ppm), un_post((uintptr_t)&g_ppm, xf); /* the first tick: only its start */
		un_pre((uintptr_t)&g_ppm);                                  /* the root stayed: blocked, in the wall */
		EXPECT(g_pxy.sets == 1 && fabsf(g_pxy.pos[0] - 508) < 0.01f && g_pxy.pos[1] == 30 && g_pxy.pos[2] == -50 &&
			       g_un.s.steps == 1 && log_has("UNPIN t=") && log_has("pinned: the asked move given to the proxy"),
		       "climb: unpin: the step (sets %d, x %g, steps %llu)", g_pxy.sets, g_pxy.pos[0],
		       (unsigned long long)g_un.s.steps);
		g_pobj.pos[0] = 508; /* the game writes the proxy's position to the root */
		xf[0] = 498, un_post((uintptr_t)&g_ppm, xf), un_pre((uintptr_t)&g_ppm); /* -x: into the wall it is in */
		EXPECT(g_pxy.sets == 1 && g_un.s.into == 1 && log_has("into a surface"), "climb: unpin: into the wall (sets %d)",
		       g_pxy.sets);
		g_pxy_man[0][7] = 0.003f; /* touching, not inside: the game's own block (a wall, an edge) */
		xf[0] = 518, un_post((uintptr_t)&g_ppm, xf), un_pre((uintptr_t)&g_ppm);
		EXPECT(g_pxy.sets == 1 && g_un.s.notin >= 1, "climb: unpin: not inside (sets %d)", g_pxy.sets);
		g_pxy_man[0][7] = -0.06f;
		EXPECT(!strcmp(cl_word("unpin off"), "climb unpin off") && !g_un.on, "climb: unpin: the word");
		un_post((uintptr_t)&g_ppm, xf), un_pre((uintptr_t)&g_ppm), un_post((uintptr_t)&g_ppm, xf), un_pre((uintptr_t)&g_ppm);
		EXPECT(g_pxy.sets == 1, "climb: unpin off: nothing (sets %d)", g_pxy.sets);
		EXPECT(!strcmp(cl_word("contacts"), "climb contacts"), "climb: the contacts word");
		g_wall = wall0, g_ppc.proxy = 0, g_un.on = on0, g_un.ok = ok0, g_un.cfg = uc0, g_un.have = 0;
		g_cl.pm = pm0, g_cl.obj = obj0, g_cl.tid = tid0, g_cl.fsm = fsm0, g_cl.mounted = mnt0;
		printf("climb unpin       the PM hook's ends: a capsule 6 cm in a wall, the asked move not done = the proxy "
		       "moved by it (8 cm, the most); into that wall: refused; not inside: nothing; off: nothing\n");
	}
	/* 18n. SOMVR_CLIMB_THROUGH (build 16 round 2, re-xr-pipeline.md 50.8): the request's vertical part through the rock
	 * = the proxy set up by it after vf_11 took the move (cl_thr_set); none = nothing; the words */
	{
		g_ppc.proxy = (uintptr_t)&g_pxy;
		g_pxy.sets = 0, g_pxy.pos[0] = 500, g_pxy.pos[1] = 30, g_pxy.pos[2] = -50;
		uint64_t s0 = g_cl.thr_sets;
		cl_thr_set((uintptr_t)&g_ppm, 2.5f, 0, 0);
		EXPECT(g_pxy.sets == 1 && g_pxy.pos[0] == 500 && fabsf(g_pxy.pos[1] - 32.5f) < 1e-4f && g_pxy.pos[2] == -50 &&
			       g_cl.thr_sets == s0 + 1,
		       "climb: through: the proxy up 2.5 (sets %d, y %g)", g_pxy.sets, g_pxy.pos[1]);
		cl_thr_set((uintptr_t)&g_ppm, 0, 0, 0);
		EXPECT(g_pxy.sets == 1, "climb: through: none = nothing (sets %d)", g_pxy.sets);
		/* SOMVR_CLIMB_TOPOUT (build 17): the level part too */
		cl_thr_set((uintptr_t)&g_ppm, 1.0f, 3.0f, -2.0f);
		EXPECT(g_pxy.sets == 2 && fabsf(g_pxy.pos[0] - 503) < 1e-4f && fabsf(g_pxy.pos[1] - 33.5f) < 1e-4f &&
			       fabsf(g_pxy.pos[2] + 52) < 1e-4f, "climb: topout: the proxy by 3, 1, -2 (%g %g %g)", g_pxy.pos[0],
		       g_pxy.pos[1], g_pxy.pos[2]);
		int hug0 = g_cl.cfg.hug, sd0 = g_cl.cfg.stand, to0 = g_cl.cfg.topout, hr0 = g_hr.mode;
		EXPECT(!strcmp(cl_word("hug on"), "climb hug on") && g_cl.cfg.hug == 1 &&
			       !strcmp(cl_word("hug off"), "climb hug off") && g_cl.cfg.hug == 0 &&
			       !strcmp(cl_word("stand on"), "climb stand on") && g_cl.cfg.stand == 1 &&
			       !strcmp(cl_word("topout on"), "climb topout on") && g_cl.cfg.topout == 1 &&
			       !strcmp(cl_word("headrock fade"), "climb headrock fade") && g_hr.mode == 1 &&
			       !strcmp(cl_word("headrock on"), "climb headrock on") && g_hr.mode == 2 &&
			       !strcmp(cl_word("headrock off"), "climb headrock off") && g_hr.mode == 0,
		       "climb: build 17's words (hug, stand, topout, headrock)");
		g_cl.cfg.hug = hug0, g_cl.cfg.stand = sd0, g_cl.cfg.topout = to0, g_hr.mode = hr0;
		int am0 = g_cl.cfg.mantle; /* build 20: SOMVR_CLIMB_MANTLE's word */
		EXPECT(!strcmp(cl_word("mantle glide"), "climb mantle glide") && g_cl.cfg.mantle == 1 &&
			       !strcmp(cl_word("mantle blink"), "climb mantle blink") && g_cl.cfg.mantle == 2 &&
			       !strcmp(cl_word("mantle off"), "climb mantle off") && g_cl.cfg.mantle == 0 &&
			       !strcmp(somvr_climb_phase_name(SOMVR_CLIMB_PH_MANTLE), "mantle"),
		       "climb: build 20's word (mantle)");
		g_cl.cfg.mantle = am0;
		int th0 = g_cl.cfg.through;
		EXPECT(!strcmp(cl_word("through on"), "climb through on") && g_cl.cfg.through == 1 &&
			       !strcmp(cl_word("through off"), "climb through off") && g_cl.cfg.through == 0 &&
			       !strcmp(cl_word("face off"), "climb face off") && g_cl.cfg.face_toward == 0 &&
			       !strcmp(cl_word("face on"), "climb face on") && g_cl.cfg.face_toward == 1 &&
			       !strcmp(cl_word("trace 2"), "climb trace") && atomic_load(&g_ct.until) > 0 &&
			       !strcmp(cl_word("trace 99"), "climb trace (SOMVR_CLIMB off or not 0..30 s: nothing)"),
		       "climb: through: the words");
		atomic_store(&g_ct.until, 0.0);
		g_cl.cfg.through = th0, g_ppc.proxy = 0;
		printf("climb through     the request's vertical part through the rock: the proxy set up by it (2.5 cm); none: "
		       "nothing; the words through on|off, trace S; build 17: the top-out's level part too, the words hug, stand, "
		       "topout, headrock\n");
	}
	g_cl.on = 0;
	g_wall = 0, g_wall_top = INFINITY, g_grav_flag = NULL;
	g_pl = &g_player, g_ppm.owner = &g_player;
	cl_set_proxy(NULL);
	g_rig = rig0, g_rigcfg = cfg0, g_nodexf_node = node0;
	memcpy(g_pobj.pos, pos0, sizeof pos0), memcpy(g_pobj.rot, rot0, sizeof rot0);
	g_layer_live = 0;
	g_cb_test_t = -1;
}

/* ---- SOMVR_BOW=1 (bow_tests; switched on at start with stand-in sites, then off until bow_tests: white box). The
 * stand-ins: the free-aim update's call of FUN_00a5b8b0(cam, &xf) behind a real E8 call site (the stand-in writes the
 * gameplay camera's transform), the two read-only K_ENTRY proofs on stand-in code, the player's PlayerCombat -> its
 * server Combat -> the resource object with the arrows, and a stand-in game after each player update: the aim command's
 * ON event starts its bow mode after the mode's HeldDuration (0.2 s), the mode runs the free-aim update (the hooked
 * call) every update, the fire command's ON event while the mode is on shoots one arrow along that update's ray (an
 * arrow less), the aim command's OFF ends the mode. ------------------------------------------------------------------ */
#define BW_AIM 124
#define BW_STEALTH 125 /* P9: PC_Stealth's command, never sent by default */
#define BW_CMD 121     /* SOMVR_BOW_FIRE_CMD's test: CombatPrimary04 (not on the deny list) */
static struct {
	int on, never, aim_held, mode, ev_i, calls, shots, fire_cmd, fire_ons, fire_nomode, stealth;
	double aim_t0;
	float cam[7], xf[7], shot[7];
} g_bwg;
/* our own shot's calls: FUN_02332c50's arguments and the aim object it found, FUN_02720a90's */
static struct { int n, tok, delta; uintptr_t inv, sc, ranged, acc, obj; int32_t node; float xf[7]; } g_bwf;
static int g_bw_calls;
static char g_bwhex[BOS_N][256];
static struct site_spec g_bos[BOS_N];
static _Alignas(16) char g_bwpc[0xa10], g_bwsc[BO_C_ATKOVR + 0x10], g_bwres[0x80], g_bwown[BO_CH_INV + 0x10],
	g_bwinv[BO_INV_SLOT + 0x10], g_bwitem[BO_ITEM_REC + 0x10];
/* the item schema (field 6 RangedWeaponData), the bow's record with it, its RangedWeaponData record */
#define H_RWD 0x0a6e0006u
static st_sch_t g_bw_sch[BO_ITEM_RANGED + 1];
static void *g_bw_schp = g_bw_sch;
static st_dbent_t g_rd_bow_e[1];
static uint64_t g_rd_bow_v[1];
static st_dbrec_t g_rd_bow, g_rd_bow_rwd, g_rd_sword;
__attribute__((noipa)) uint64_t t_bowcamxf(void *cam, float *xf)
{
	(void)cam;
	g_bw_calls++;
	memcpy(xf, g_bwg.cam, sizeof g_bwg.cam);
	return 0x55;
}
__attribute__((noipa)) uint64_t c_bowcamxf(void *cam, float *xf) { return t_bowcamxf(cam, xf) + 1; }
__attribute__((noipa)) uint64_t st_bo_code(uint64_t a) { return a * 7 + 5; } /* the proof sites' stand-in code */
/* FUN_02332c50 (the stand-in game's arrow along the fire transform) and FUN_02720a90 */
__attribute__((noipa)) void st_bow_fire(uintptr_t inv, uintptr_t sc, uintptr_t ranged, uintptr_t acc, const float *xf)
{
	g_bwf.n++, g_bwf.inv = inv, g_bwf.sc = sc, g_bwf.ranged = ranged, g_bwf.acc = acc;
	memcpy(&g_bwf.obj, (const char *)sc + BO_C_AIMOBJ, 8), memcpy(&g_bwf.node, (const char *)sc + BO_C_AIMNODE, 4);
	memcpy(g_bwf.xf, xf, sizeof g_bwf.xf);
	memcpy(g_bwg.shot, xf, sizeof g_bwg.shot);
	g_bwg.shots++;
}
/* Inventory_ActivateSlot FUN_02315ca0 (SOMVR_BOW_RELEASE=now): the active slot = slot */
static int g_bw_acts;
__attribute__((noipa)) uint64_t st_bow_activate(uintptr_t inv, uint32_t slot)
{
	memcpy((char *)inv + BO_INV_SLOT, &slot, 4);
	g_bw_acts++;
	return 1;
}
__attribute__((noipa)) void st_bow_tokens(uintptr_t res, int delta)
{
	int32_t n;
	memcpy(&n, (const char *)res + BO_RES_COUNT, 4);
	n += delta;
	memcpy((char *)res + BO_RES_COUNT, &n, 4);
	g_bwf.tok++, g_bwf.delta = delta;
}
__attribute__((noipa)) uintptr_t t_bowitem(uintptr_t inv) { return inv ^ 0x77; } /* FUN_02311e20 (proof only) */
__attribute__((noipa)) uint64_t t_bowmat(uintptr_t wm) { return wm ^ 5; } /* FUN_00e1f450 behind its two calls */
__attribute__((noipa)) uint64_t c_bowmat(uintptr_t wm) { return t_bowmat(wm) + 1; }
__attribute__((noipa)) uint64_t c_bowmat2(uintptr_t wm) { return t_bowmat(wm) + 2; }
__attribute__((noipa)) uint64_t t_bowslow(uintptr_t wm) { return wm ^ 9; } /* FUN_00e1cf90 behind its two calls */
__attribute__((noipa)) uint64_t c_bowslow(uintptr_t wm) { return t_bowslow(wm) + 1; }
__attribute__((noipa)) uint64_t c_bowslow2(uintptr_t wm) { return t_bowslow(wm) + 2; }
__attribute__((noipa)) uint64_t t_bowfx(uintptr_t wm) { return wm ^ 11; } /* the FX components behind their calls */
__attribute__((noipa)) uint64_t c_bowfx(uintptr_t wm) { return t_bowfx(wm) + 1; }
__attribute__((noipa)) uint64_t c_bowfx2(uintptr_t wm) { return t_bowfx(wm) + 2; }
__attribute__((noipa)) uint64_t c_bowfx3(uintptr_t wm) { return t_bowfx(wm) + 3; }
__attribute__((noipa)) uint64_t c_bowfx4(uintptr_t wm) { return t_bowfx(wm) + 4; }
__attribute__((noipa)) uintptr_t c_bowitem(uintptr_t inv) { return t_bowitem(inv) + 3; }
static int bw_call_site(void *caller, void *target, int i, const char *name)
{
	const unsigned char *p = caller;
	for (int k = 0; k < 96; k++) {
		int32_t rel;
		memcpy(&rel, p + k + 1, 4);
		if (p[k] == 0xe8 && (uintptr_t)(p + k + 5 + rel) == (uintptr_t)target) {
			hex_of(p + k, 5, g_bwhex[i], 128);
			hex_of(target, 4, g_bwhex[i] + 128, 128);
			g_bos[i] = (struct site_spec){ name, K_CALL, (uintptr_t)(p + k), (uintptr_t)(p + k), g_bwhex[i],
						       (uintptr_t)target, g_bwhex[i] + 128 };
			return 0;
		}
	}
	return -1;
}
static void bw_entry_site(void *fn, int i)
{
	hex_of(fn, 8, g_bwhex[i], 128);
	hex_of(fn, 4, g_bwhex[i] + 128, 128);
	g_bos[i] = (struct site_spec){ g_bo_sites_game[i].name, K_ENTRY, (uintptr_t)fn, (uintptr_t)fn, g_bwhex[i],
				       (uintptr_t)fn, g_bwhex[i] + 128 };
}
static int bo_stand_in_sites(void)
{
	if (bw_call_site((void *)c_bowcamxf, (void *)t_bowcamxf, BOS_CAMXF, "BOW_FA_CAMXF") ||
	    bw_call_site((void *)c_bowitem, (void *)t_bowitem, BOS_FIRE_ITEM, "BOW_FIRE_ITEM") ||
	    bw_call_site((void *)c_bowmat, (void *)t_bowmat, BOS_MAT_APPLY, "BOW_MAT_APPLY") ||
	    bw_call_site((void *)c_bowmat2, (void *)t_bowmat, BOS_MAT_CLEAR, "BOW_MAT_CLEAR") ||
	    bw_call_site((void *)c_bowslow, (void *)t_bowslow, BOS_SLOW_APPLY, "BOW_SLOW_APPLY") ||
	    bw_call_site((void *)c_bowslow2, (void *)t_bowslow, BOS_SLOW_CLEAR, "BOW_SLOW_CLEAR") ||
	    bw_call_site((void *)c_bowfx, (void *)t_bowfx, BOS_FX_APPLY, "BOW_FX_APPLY") ||
	    bw_call_site((void *)c_bowfx2, (void *)t_bowfx, BOS_FXALT_APPLY, "BOW_FXALT_APPLY") ||
	    bw_call_site((void *)c_bowfx3, (void *)t_bowfx, BOS_FX_CLEAR, "BOW_FX_CLEAR") ||
	    bw_call_site((void *)c_bowfx4, (void *)t_bowfx, BOS_FXALT_CLEAR, "BOW_FXALT_CLEAR"))
		return -1;
	bw_entry_site((void *)st_bo_code, BOS_AMMO), bw_entry_site((void *)st_bo_code, BOS_AMMO_RES);
	bw_entry_site((void *)st_bow_fire, BOS_FIRE), bw_entry_site((void *)st_bo_code, BOS_ITEM);
	bw_entry_site((void *)st_bo_code, BOS_INVENTORY), bw_entry_site((void *)st_bo_code, BOS_AIM_OBJ);
	bw_entry_site((void *)st_bow_tokens, BOS_TOKENS_ADD), bw_entry_site((void *)st_bow_activate, BOS_ACTIVATE);
	return 0;
}
/* the player's PlayerCombat (+0xa08 the server Combat: the vtable, +0x88 the player, +0x1c0 the resource: the arrows;
 * +0x70 its owner CCharacter: +8 = the Combat's HOBJECT, +0x910 the inventory: slot 2 = the bow, its record with
 * RangedWeaponData) */
static void bw_setup(int arrows)
{
	uintptr_t sc = (uintptr_t)g_bwsc, res = (uintptr_t)g_bwres, pl = (uintptr_t)&g_player, own = (uintptr_t)g_bwown,
		  inv = (uintptr_t)g_bwinv, item = (uintptr_t)g_bwitem, rec = (uintptr_t)&g_rd_bow, hobj = (uintptr_t)&g_pobj;
	int32_t n = arrows;
	uint32_t slot = 2;
	memset(g_bwpc, 0, sizeof g_bwpc), memset(g_bwsc, 0, sizeof g_bwsc), memset(g_bwres, 0, sizeof g_bwres);
	memset(g_bwown, 0, sizeof g_bwown), memset(g_bwinv, 0, sizeof g_bwinv), memset(g_bwitem, 0, sizeof g_bwitem);
	memcpy(g_bwpc + PC_SERVER, &sc, 8);
	memcpy(g_bwsc, &g_cbg.vt_combat, 8), memcpy(g_bwsc + C_PLAYER, &pl, 8), memcpy(g_bwsc + BO_SC_RES, &res, 8);
	memcpy(g_bwsc + C_OWNER, &own, 8), memcpy(g_bwsc + C_HOBJ, &hobj, 8);
	memcpy(g_bwown + RX_GB_HOBJ, &hobj, 8), memcpy(g_bwown + BO_CH_INV, &inv, 8);
	memcpy(g_bwinv + BO_INV_SLOT, &slot, 4), memcpy(g_bwinv + BO_INV_ITEMS + 2 * BO_INV_STRIDE, &item, 8);
	memcpy(g_bwitem + BO_ITEM_REC, &rec, 8);
	memcpy(g_bwres + BO_RES_COUNT, &n, 4);
	g_bw_sch[BO_ITEM_RANGED] = (st_sch_t){ "RangedWeaponData", H_RWD, { 0 } };
	g_rd_bow_rwd = (st_dbrec_t){ .flags = 2, .name = "ST_BOW_RANGED" };
	g_rd_bow_e[0] = dbent(H_RWD, 0xa, 1, 0), g_rd_bow_v[0] = (uintptr_t)&g_rd_bow_rwd;
	g_rd_bow = (st_dbrec_t){ 1, 3, { 0 }, g_rd_bow_e, g_rd_bow_v, 0, "ST_PC_BOW" };
	g_rd_sword = (st_dbrec_t){ .flags = 2, .name = "ST_PC_SWORD" }; /* no attributes: no RangedWeaponData */
	g_bog.sch_item = (uintptr_t)&g_bw_schp;
	g_player.pcomb = g_bwpc;
}
static int bw_arrows(void)
{
	int32_t n;
	memcpy(&n, g_bwres + BO_RES_COUNT, 4);
	return n;
}
/* the stand-in game after the player update: the bow mode, its free-aim update, the shot of a fire command (only the
 * SOMVR_BOW_FIRE_CMD test sets one: g_bwg.fire_cmd); a 125 press is counted (it would enter the stealth mode) */
static void bw_game(void)
{
	int fire = 0;
	for (; g_bwg.on && g_bwg.ev_i < g_ninev; g_bwg.ev_i++) {
		int cmd = g_inev[g_bwg.ev_i].cmd;
		char k = g_inev[g_bwg.ev_i].k;
		if (cmd == BW_STEALTH)
			g_bwg.stealth++;
		if (cmd == BW_AIM && k == 'N')
			g_bwg.aim_held = 1, g_bwg.aim_t0 = g_cb_test_t;
		else if (cmd == BW_AIM && k == 'F')
			g_bwg.aim_held = 0, g_bwg.mode = 0;
		else if (g_bwg.fire_cmd && cmd == g_bwg.fire_cmd && k == 'N')
			fire = 1, g_bwg.fire_ons++;
	}
	if (!g_bwg.on)
		return;
	if (g_bwg.aim_held && !g_bwg.never && g_cb_test_t - g_bwg.aim_t0 >= 0.2 - 1e-9)
		g_bwg.mode = 1;
	if (g_bwg.mode) {
		float xf[7];
		uint64_t rc = c_bowcamxf((void *)0x4444, xf);
		EXPECT(rc == 0x56, "the free-aim call's return value %#llx", (unsigned long long)rc);
		memcpy(g_bwg.xf, xf, sizeof xf);
		g_bwg.calls++;
	}
	if (fire && g_bwg.mode) {
		int32_t n = bw_arrows() - 1;
		memcpy(g_bwg.shot, g_bwg.xf, sizeof g_bwg.shot);
		g_bwg.shots++;
		memcpy(g_bwres + BO_RES_COUNT, &n, 4);
	} else if (fire) {
		g_bwg.fire_nomode++;
	}
}
/* one game frame: the layer's wait and sync, I0 (the commands), the player update and the stand-in game (the free aim,
 * the shot), with arms the player's phase 2, then H4 (the bow step) and the present */
static void bframe(int arms)
{
	g_cb_test_t += 1.0 / 90;
	g_xrt += 11111111;
	somvr_time_publish(&g_sh, g_xrt, 11111111);
	if (g_layer_live) {
		g_lst.sync_seq++, g_lst.time = g_xrt;
		for (int h = 0; h < 2; h++)
			memcpy(&g_lst.hand[h].grip, &g_lpose[h], sizeof g_lst.hand[h].grip);
		somvr_input_publish(&g_in, &g_lst);
	}
	((t_vf04_fn)g_vtin[4])(&g_cwbi);
	if (g_pl)
		player_update();
	bw_game();
	g_pu_done = 1;
	if (arms)
		((t_trk_fn)g_vttrk[3])(&g_ptrk);
	frame();
}
static void bframes(int n, int arms)
{
	for (int i = 0; i < n; i++)
		bframe(arms);
}
static void bw_hand(int h, float x, float y, float z) { g_lpose[h].position = (XrVector3f){ x, y, z }; }
static void bw_grip(float v) { g_lst.hand[SOMVR_HAND_R].squeeze = v; }
static sv_vec3 bw_unit(float x, float y, float z)
{
	float l = sqrtf(x * x + y * y + z * z);
	return (sv_vec3){ x / l, y / l, z / l };
}
static sv_vec3 bw_fwd(const float *xf) { return sv_qrot(sv_quat_of(xf + 3), (sv_vec3){ 0, 0, 1 }); }
/* the string hand moved from its place to (x, y, z) over n frames (XR space, m) */
static void bw_move(float x, float y, float z, int n, int arms)
{
	XrVector3f a = g_lpose[SOMVR_HAND_R].position;
	for (int i = 1; i <= n; i++) {
		float k = (float)i / (float)n;
		bw_hand(SOMVR_HAND_R, a.x + (x - a.x) * k, a.y + (y - a.y) * k, a.z + (z - a.z) * k);
		bframe(arms);
	}
}
static int bw_until(int phase, int max, int arms)
{
	int n = 0;
	while (g_bow.s.phase != phase && n < max)
		bframe(arms), n++;
	return n;
}
/* one shot from aiming (or idle): the nock, 40 cm back, the release, 6 frames, the string hand back at the bow, aiming */
static void bw_shoot_cycle(void)
{
	bw_hand(SOMVR_HAND_R, -0.1f, -0.2f, -0.35f);
	bframe(0);
	bw_grip(1);
	bframe(0);
	bw_move(-0.1f, -0.2f, 0.05f, 20, 0);
	bw_grip(0);
	bframes(6, 0);
	bw_hand(SOMVR_HAND_R, -0.1f, -0.2f, -0.3f);
	bw_until(SOMVR_BOW_PH_AIM, 60, 0);
}
/* the bow hand's weapon node in the world against its grip turned onto the shot (the string hand -> the nock, the ray's
 * blend) x SOMVR_BOW_CAL with this frame's rig: cm, and the rotation's error in *deg */
static float bw_arm_err(float *deg, int aligned)
{
	sa_xf X = { { g_pobj.pos[0], g_pobj.pos[1], g_pobj.pos[2] }, sv_qnormalize(sv_quat_of(g_pobj.rot)) };
	sa_xf wn = sa_xf_mul(X, sa_node_xf(&g_pnodes[g_arm.ch[SA_L].wpn]));
	sv_vec3 pl, pr;
	sv_quat ql;
	somvr_hand_game(&g_inp.hands[SA_L], &pl, &ql, NULL, NULL);
	somvr_hand_game(&g_inp.hands[SA_R], &pr, NULL, NULL, NULL);
	sa_xf gw = { somvr_rig_point(&g_rig, g_fr.base, pl), somvr_rig_quat(&g_rig, g_fr.base, ql) };
	if (aligned) {
		float d;
		sv_vec3 rw = somvr_rig_point(&g_rig, g_fr.base, pr), nock = somvr_bow_nock(&g_bow.cfg, gw.p, gw.q),
			dir = somvr_bow_draw_dir(nock, rw, &d);
		gw.q = somvr_bow_align(gw.q, dir, somvr_bow_dir_weight(&g_bow.cfg, d));
	}
	sa_xf want = sa_xf_mul(gw, g_bow.cal);
	*deg = qang2(wn.q, want.q) / 0.01745329252f;
	return sqrtf((wn.p.x - want.p.x) * (wn.p.x - want.p.x) + (wn.p.y - want.p.y) * (wn.p.y - want.p.y) +
		     (wn.p.z - want.p.z) * (wn.p.z - want.p.z));
}

static void bow_tests(void)
{
	EXPECT(log_has("CONFIG bow=1: the bow in the left hand, the string = the right hand's grip (squeeze); the aim command "
		       "124 'CombatSecondary03'") &&
		       log_has("the fire: our own shot FUN_02332c50 and an arrow less (no fire command; sites ok)") &&
		       !log_has("the fire command 125") && log_has("aim button trigger") &&
		       log_has("the bow node 0,2,0,90,30,0, align 1") && log_has("BOWSITE BOW_FA_CAMXF ok") &&
		       log_has("BOWSITE BOW_AMMO_RES ok") && log_has("BOWSITE BOW_FIRE ok") && log_has("BOWSITE BOW_FIRE_ITEM ok") &&
		       log_has("BOWSITE BOW_ITEM ok") && log_has("BOWSITE BOW_INVENTORY ok") && log_has("BOWSITE BOW_AIM_OBJ ok") &&
		       log_has("BOWSITE BOW_TOKENS_ADD ok") && log_has("HOOK BOW_FA_CAMXF ok call"),
	       "bow: no CONFIG/BOWSITE/HOOK lines");
	float pos0[3], rot0[4];
	memcpy(pos0, g_pobj.pos, sizeof pos0), memcpy(rot0, g_pobj.rot, sizeof rot0);
	somvr_rig rig0 = g_rig;
	somvr_rig_cfg cfg0 = g_rigcfg;
	void *pcomb0 = g_player.pcomb;
	int arm0 = g_arm.on;
	g_views_fake = 0, g_rt_result = XR_SUCCESS, g_layout = g_rigcfg.layout = SOMVR_LAYOUT_A;
	g_rigcfg.base = SOMVR_BASE_CAMERA, g_rigcfg.yaw = SOMVR_YAW_CAMERA, g_follow.on = 0, g_fb.on = 0, g_cb.mode = CB_OFF;
	g_cl.on = 0, g_hide.on = 0, g_hudq.on = 0, g_cf.out_s = g_cf.in_s = 0, g_arm.on = 0;
	somvr_rig_init(&g_rig);
	g_rt_head[0] = g_rt_head[1] = g_rt_head[2] = 0, g_rt_q = (sv_quat){ 0, 0, 0, 1 };
	g_rm[0] = g_rm[1] = g_rm[2] = 0;
	g_pobj.pos[0] = 500, g_pobj.pos[1] = 100, g_pobj.pos[2] = -50;
	g_pobj.rot[0] = g_pobj.rot[1] = g_pobj.rot[2] = 0, g_pobj.rot[3] = 1;
	g_cb_test_t = 11000;
	g_xrt = g_vst.last_time + 1000000000;
	runtime_up();
	g_inp.on = 1, g_inp.lines = 0, g_inp.lsec = 0;
	layer_up();
	for (int h = 0; h < 2; h++) {
		g_lst.hand[h].squeeze = g_lst.hand[h].trigger = 0, g_lst.hand[h].buttons = 0;
		g_lst.hand[h].stick[0] = g_lst.hand[h].stick[1] = 0;
	}
	g_pl = &g_player, g_ppm.owner = &g_player;
	bw_setup(10);
	g_ninev = 0;
	memset(&g_bwg, 0, sizeof g_bwg), memset(&g_bwf, 0, sizeof g_bwf);
	g_bwg.on = 1;
	g_bow.direct = g_bow.refused = 0;
	const float cam[7] = { 100, 200, 300, 0, 0, 0, 1 };
	memcpy(g_bwg.cam, cam, sizeof cam);
	g_bow.on = 1, g_bow.tid = 0, g_bow.lines = 0, g_bow.next_log = 0, g_bow.cam_seen = 0;
	somvr_bow_init(&g_bow.s);
	bw_hand(SOMVR_HAND_L, -0.1f, -0.2f, -0.4f); /* world (90, 180, 340): the bow ahead, its grip's +Z forward */
	bw_hand(SOMVR_HAND_R, 0.2f, -0.3f, -0.2f);  /* (120, 170, 320) */
	atomic_fetch_add(&g_req_recenter, 1);
	bframes(4, 0);
	sv_vec3 lw = cl_hand_world(SOMVR_HAND_L);
	EXPECT(NEAR3(lw, 90, 180, 340, 1e-2) && g_inp.live && g_bow.s.phase == SOMVR_BOW_PH_IDLE && g_bw_calls == 0 &&
		       g_bwg.calls == 0,
	       "bow: before: the bow hand at %g %g %g live %d phase %d", lw.x, lw.y, lw.z, g_inp.live, g_bow.s.phase);
	/* 1. idle: the free-aim call passes the camera through (counted) */
	float xf[7];
	uint64_t no0 = g_bow.notours;
	EXPECT(c_bowcamxf(NULL, xf) == 0x56 && !memcmp(xf, cam, sizeof cam) && g_bow.notours == no0 + 1 && g_bow_h.calls > 0,
	       "bow idle: the camera's transform was changed");
	/* 2. the nock: the string hand 5 cm behind the bow hand, its grip closes: aim and draw at once, a haptic pulse */
	bw_hand(SOMVR_HAND_R, -0.1f, -0.2f, -0.35f);
	bframe(0);
	uint64_t hp0 = atomic_load(&g_in.n_hap_posts);
	bw_grip(1);
	bframe(0);
	EXPECT(g_bow.s.phase == SOMVR_BOW_PH_DRAW && log_has("enter (by the draw): the aim command 124 'CombatSecondary03' held; "
							      "arrows 10") &&
		       log_has("nock: the string hand 5.0 cm from the nock (radius 15)") && atomic_load(&g_in.n_hap_posts) > hp0,
	       "bow: the nock: phase %d", g_bow.s.phase);
	EXPECT(!g_inp.grip_ready, "bow: the combat blade has a grip while the bow is out");
	/* 3. the next input tick holds the aim command: its ON event, the binding layer's press */
	bframe(0);
	EXPECT(nev('N', BW_AIM, 0) == 1 && log_has("VIN press cmd=124 CombatSecondary03") && g_bwg.aim_held && !g_bwg.mode,
	       "bow: the aim command: ON events %d held %d mode %d", nev('N', BW_AIM, 0), g_bwg.aim_held, g_bwg.mode);
	/* 4. draw back 25 cm, 5 right, 5 down: the game's mode comes on after its 0.2 s and its free-aim update gets our ray:
	 * from the nock (90, 180, 340) along the string hand (95, 175, 310) -> the nock */
	bw_move(-0.05f, -0.25f, -0.1f, 30, 0);
	bframes(2, 0);
	sv_vec3 want = bw_unit(-5, 5, 30), got = bw_fwd(g_bwg.xf);
	EXPECT(g_bwg.mode && g_bwg.calls > 0 && NEAR3(got, want.x, want.y, want.z, 1e-4) && NEAR(g_bwg.xf[0], 90, 1e-3) &&
		       NEAR(g_bwg.xf[1], 180, 1e-3) && NEAR(g_bwg.xf[2], 340, 1e-3) && g_bow.overrides > 0,
	       "bow: the drawn ray: mode %d calls %d pos %g %g %g dir %g %g %g (want %g %g %g)", g_bwg.mode, g_bwg.calls,
	       g_bwg.xf[0], g_bwg.xf[1], g_bwg.xf[2], got.x, got.y, got.z, want.x, want.y, want.z);
	EXPECT(log_has("BOWRAY n=") && log_has("armed: draw") && !log_has("full draw") && g_bow.s.ticks >= 7,
	       "bow: no BOWRAY/armed lines (ticks %d)", g_bow.s.ticks);
	EXPECT(g_bwg.shots == 0 && g_bwf.n == 0, "bow: fired while drawing");
	/* 5. release (the hand flies forward): our own shot in the same step (the game's aim on, the aim held 0.25 s):
	 * FUN_02332c50(inv, sc, 0, 0, fireXf) from the nock along the latched ray, the stale aim object cleared (+0x170 0,
	 * +0x178 -1), an arrow less through FUN_02720a90(res, -1); no fire command at all, 125 (P9: PC_Stealth) never */
	hp0 = atomic_load(&g_in.n_hap_posts);
	memset(g_bwsc + BO_C_AIMOBJ, 0x5a, 8), memset(g_bwsc + BO_C_AIMNODE, 0x11, 4); /* a stale aim object */
	bw_grip(0);
	bw_hand(SOMVR_HAND_R, 0.1f, 0.0f, -0.6f);
	bframe(0);
	got = bw_fwd(g_bwf.xf);
	EXPECT(g_bow.s.phase == SOMVR_BOW_PH_FOLLOW && log_has("release: draw 30.8 cm, the ray from 90.0,180.0,340.0") &&
		       log_has("fire: our shot FUN_02332c50(inv=") && log_has("from 90.0,180.0,340.0 along") &&
		       log_has("arrows 10 -> 9; shots 1 ours 1") && atomic_load(&g_in.n_hap_posts) > hp0,
	       "bow: the release: phase %d", g_bow.s.phase);
	EXPECT(g_bwf.n == 1 && g_bwf.inv == (uintptr_t)g_bwinv && g_bwf.sc == (uintptr_t)g_bwsc && !g_bwf.ranged && !g_bwf.acc &&
		       !g_bwf.obj && g_bwf.node == -1 && g_bwf.tok == 1 && g_bwf.delta == -1 && g_bwg.shots == 1 &&
		       bw_arrows() == 9 && NEAR3(got, want.x, want.y, want.z, 1e-4) && NEAR(g_bwf.xf[0], 90, 1e-3) &&
		       NEAR(g_bwf.xf[1], 180, 1e-3) && NEAR(g_bwf.xf[2], 340, 1e-3) && g_bow.direct == 1,
	       "bow: our shot: %d calls inv %d sc %d obj %#lx node %d tokens %d/%d arrows %d dir %g %g %g", g_bwf.n,
	       g_bwf.inv == (uintptr_t)g_bwinv, g_bwf.sc == (uintptr_t)g_bwsc, (unsigned long)g_bwf.obj, g_bwf.node, g_bwf.tok,
	       g_bwf.delta, bw_arrows(), got.x, got.y, got.z);
	bframes(12, 0);
	EXPECT(nev('N', BW_STEALTH, 0) == 0 && nev('F', BW_STEALTH, 0) == 0 && g_bwg.stealth == 0 && g_bow.fire_ticks == 0 &&
		       nev('F', BW_AIM, 0) == 0 && g_bwg.mode && g_bwg.shots == 1 && g_bwf.n == 1,
	       "bow: no fire command: 125 ON %d OFF %d, fire ticks %llu, aim OFF %d", nev('N', BW_STEALTH, 0),
	       nev('F', BW_STEALTH, 0), (unsigned long long)g_bow.fire_ticks, nev('F', BW_AIM, 0));
	/* 6. the follow-through over: aiming again, the ray along the bow's own forward (+Z) */
	bw_hand(SOMVR_HAND_R, -0.1f, -0.2f, -0.3f); /* the string hand back at the bow */
	bw_until(SOMVR_BOW_PH_AIM, 60, 0);
	bframes(2, 0);
	got = bw_fwd(g_bwg.xf);
	EXPECT(g_bow.s.phase == SOMVR_BOW_PH_AIM && NEAR3(got, 0, 0, 1, 1e-5) && NEAR(g_bwg.xf[2], 340, 1e-3),
	       "bow: aiming after the shot: phase %d dir %g %g %g", g_bow.s.phase, got.x, got.y, got.z);
	/* 7. Talion walks 3 cm a tick: the ray goes with him by his move since the step */
	g_rm[2] = 3;
	bframes(5, 0);
	EXPECT(NEAR(g_bwg.xf[2], 343, 1e-3) && NEAR(g_bwg.xf[0], 90, 1e-3), "bow: walking: the ray at z %g", g_bwg.xf[2]);
	g_rm[2] = 0;
	bframes(2, 0);
	EXPECT(NEAR(g_bwg.xf[2], 340, 1e-3), "bow: standing again: z %g", g_bwg.xf[2]);
	/* 8. a second shot, straight back 40 cm */
	bw_hand(SOMVR_HAND_R, -0.1f, -0.2f, -0.35f);
	bframe(0);
	bw_grip(1);
	bframe(0);
	bw_move(-0.1f, -0.2f, 0.05f, 20, 0);
	bw_grip(0);
	bframes(2, 0);
	got = bw_fwd(g_bwg.shot);
	EXPECT(g_bwg.shots == 2 && g_bwf.n == 2 && bw_arrows() == 8 && g_bow.s.fired == 2 && g_bow.s.draws == 2 &&
		       g_bow.s.aims == 1 && NEAR3(got, 0, 0, 1, 1e-4),
	       "bow: the second shot: shots %d arrows %d dir %g %g %g", g_bwg.shots, bw_arrows(), got.x, got.y, got.z);
	/* 9. the hands apart: the aim ends (after the follow-through, 0.8 s apart); the aim command's OFF, the mode off */
	bw_hand(SOMVR_HAND_R, 0.4f, -0.4f, -0.1f);
	int n = bw_until(SOMVR_BOW_PH_IDLE, 200, 0);
	bframes(2, 0);
	int c0 = g_bwg.calls;
	bframes(3, 0);
	EXPECT(g_bow.s.phase == SOMVR_BOW_PH_IDLE && g_bow.s.last_end == SOMVR_BOW_END_APART && n > 100 && n < 120 &&
		       log_has("exit: apart; aims 1 shots 2 fired 2 dropped 0") && nev('F', BW_AIM, 0) == 1 && !g_bwg.mode &&
		       g_bwg.calls == c0 && g_inp.grip_ready,
	       "bow: apart: phase %d end %d after %d frames, aim OFFs %d", g_bow.s.phase, g_bow.s.last_end, n, nev('F', BW_AIM, 0));
	/* 10. the button (the bow hand's trigger): aiming with the hands apart along the bow's forward (turned left); held:
	 * no exit; let go apart: the exit */
	const float hy = -0.25f; /* XR: about +Y by -0.5 rad (the mirror: the game's forward turns to +X) */
	g_lpose[SOMVR_HAND_L].orientation = (XrQuaternionf){ 0, sinf(hy), 0, cosf(hy) };
	const float xq[4] = { 0, sinf(hy), 0, cosf(hy) };
	sv_vec3 bf = sv_qrot(sv_xr_to_game_quat(xq), (sv_vec3){ 0, 0, 1 });
	g_lst.hand[SOMVR_HAND_L].trigger = 1;
	bframe(0);
	EXPECT(g_bow.s.phase == SOMVR_BOW_PH_AIM && g_bow.s.by_button && log_has("enter (by the button)"), "bow: the button");
	/* build 17: the VR menu's Arrows row (bo_vm_set, SOMVR_BOW_QUIVER live) while the bow is out: held for the bow's
	 * putting away (below), the Controls page's bits */
	int qv0 = g_bow.cfg.quiver;
	somvr_settings qs;
	memset(&qs, 0, sizeof qs);
	qs.quiver = (uint8_t)!qv0;
	const char *qnote = bo_vm_set(&qs);
	uint32_t qa = bo_vm_avail();
	EXPECT(strstr(qnote, "once the bow is put away") && atomic_load(&g_bo_req_quiver) == !qv0 && (qa & SOMVR_VMA_ARROWS) &&
		       !(qa & SOMVR_VMA_BOWTRIG) == !g_bow.string_trigger &&
		       !(qa & (SOMVR_VMA_BOWRT | SOMVR_VMA_QVGRIP)) == !qv0,
	       "bow: the Arrows row with the bow out: '%s', avail %#x", qnote, qa);
	bframes(80, 0);
	EXPECT(g_bow.cfg.quiver == qv0 && !log_has("(the VR menu's Arrows"), "bow: the Arrows row: not mid-aim");
	got = bw_fwd(g_bwg.xf);
	EXPECT(g_bow.s.phase == SOMVR_BOW_PH_AIM && g_bwg.mode && NEAR3(got, bf.x, bf.y, bf.z, 1e-4) && fabsf(bf.x) > 0.4f,
	       "bow: button held: phase %d dir %g %g %g (want %g %g %g)", g_bow.s.phase, got.x, got.y, got.z, bf.x, bf.y, bf.z);
	g_lst.hand[SOMVR_HAND_L].trigger = 0;
	n = bw_until(SOMVR_BOW_PH_IDLE, 120, 0);
	EXPECT(g_bow.s.phase == SOMVR_BOW_PH_IDLE && g_bow.s.last_end == SOMVR_BOW_END_APART && n > 60,
	       "bow: the button let go: phase %d after %d", g_bow.s.phase, n);
	bframe(0); /* the bow away: the Arrows row's change taken now, then back (the next frame) */
	int qv1 = g_bow.cfg.quiver;
	qs.quiver = (uint8_t)qv0;
	qnote = bo_vm_set(&qs);
	bframe(0);
	EXPECT(qv1 == !qv0 && g_bow.cfg.quiver == qv0 && atomic_load(&g_bo_req_quiver) == -1 &&
		       log_has(qv0 ? "quiver on -> off (the VR menu's Arrows: an arrow on the string by itself" :
				     "quiver off -> on (the VR menu's Arrows: an arrow from behind the string hand's shoulder") &&
		       log_has(qv0 ? "quiver off -> on (the VR menu's Arrows" : "quiver on -> off (the VR menu's Arrows") &&
		       strstr(qnote, qv0 ? "behind the right shoulder" : "on the string at the nock"),
	       "bow: the Arrows row with the bow away: %d then %d ('%s')", qv1, g_bow.cfg.quiver, qnote);
	g_lpose[SOMVR_HAND_L].orientation = (XrQuaternionf){ 0, 0, 0, 1 };
	bframes(3, 0);
	/* 11. a wrong aim command (the game's mode never comes on): the shot waits 0.6 s and is dropped, no shot of ours */
	g_bwg.never = 1;
	int f0 = g_bwf.n;
	c0 = g_bwg.calls;
	bw_hand(SOMVR_HAND_R, -0.1f, -0.2f, -0.35f);
	bframe(0);
	bw_grip(1);
	bframe(0);
	bw_move(-0.1f, -0.2f, -0.05f, 30, 0);
	bw_grip(0);
	bframes(70, 0);
	EXPECT(log_has("dropped: the shot waited 600 ms without the game's free aim") && g_bwf.n == f0 &&
		       g_bow.s.dropped == 1 && g_bwg.calls == c0 && bw_arrows() == 8,
	       "bow: dropped: %llu, our shots %d", (unsigned long long)g_bow.s.dropped, g_bwf.n - f0);
	g_bwg.never = 0;
	bw_hand(SOMVR_HAND_R, 0.4f, -0.4f, -0.1f);
	bw_until(SOMVR_BOW_PH_IDLE, 120, 0);
	bframes(2, 0); /* the aim command's OFF at the next tick */
	/* 11b. our shot's gates (re-bow.md 2.3 B): no arrows, the active item without RangedWeaponData, no active item (a
	 * slot past 7), a fire site that did not match: each release refused (logged, the "denied" pulse), no FUN_02332c50,
	 * no arrow taken; unlimited: the shot, no arrow taken */
	f0 = g_bwf.n;
	int tk0 = g_bwf.tok;
	uint64_t rf0 = g_bow.refused;
	int32_t ar = 0;
	memcpy(g_bwres + BO_RES_COUNT, &ar, 4);
	hp0 = atomic_load(&g_in.n_hap_posts);
	bw_shoot_cycle();
	EXPECT(g_bwf.n == f0 && g_bow.refused == rf0 + 1 && bw_arrows() == 0 && g_bow.s.fired == 3 &&
		       atomic_load(&g_in.n_hap_posts) > hp0 &&
		       log_has("dropped: our shot refused (no arrows; slot 2 item") && log_has("no FUN_02332c50, the arrow kept"),
	       "bow gates: no arrows: our shots %d refused %llu", g_bwf.n - f0, (unsigned long long)(g_bow.refused - rf0));
	ar = 8, memcpy(g_bwres + BO_RES_COUNT, &ar, 4);
	uintptr_t sword = (uintptr_t)&g_rd_sword, bow = (uintptr_t)&g_rd_bow;
	memcpy(g_bwitem + BO_ITEM_REC, &sword, 8);
	bw_shoot_cycle();
	memcpy(g_bwitem + BO_ITEM_REC, &bow, 8);
	EXPECT(g_bwf.n == f0 && g_bow.refused == rf0 + 2 && bw_arrows() == 8 &&
		       log_has("dropped: our shot refused (the active item has no RangedWeaponData;"),
	       "bow gates: not ranged: our shots %d", g_bwf.n - f0);
	uint32_t sl = 9;
	memcpy(g_bwinv + BO_INV_SLOT, &sl, 4);
	bw_shoot_cycle();
	sl = 2, memcpy(g_bwinv + BO_INV_SLOT, &sl, 4);
	EXPECT(g_bwf.n == f0 && g_bow.refused == rf0 + 3 && bw_arrows() == 8 && log_has("dropped: our shot refused (no active item;"),
	       "bow gates: slot 9: our shots %d", g_bwf.n - f0);
	g_bow.ok_fire = 0;
	bw_shoot_cycle();
	g_bow.ok_fire = 1;
	EXPECT(g_bwf.n == f0 && g_bow.refused == rf0 + 4 && log_has("dropped: our shot refused (a fire site did not match at start;"),
	       "bow gates: sites: our shots %d", g_bwf.n - f0);
	g_bwres[BO_RES_UNLIM] = 1;
	bw_shoot_cycle();
	g_bwres[BO_RES_UNLIM] = 0;
	EXPECT(g_bwf.n == f0 + 1 && g_bwf.tok == tk0 && bw_arrows() == 8 && g_bow.refused == rf0 + 4 &&
		       log_has("arrows 8 -> 8 (unlimited)"),
	       "bow gates: unlimited: our shots %d tokens %d arrows %d", g_bwf.n - f0, g_bwf.tok - tk0, bw_arrows());
	/* 11b'. build 11, SOMVR_BOW_RELEASE=now: a hit put the bow away (slot 0, the sword, active): the release activates
	 * slot 2 (Inventory_ActivateSlot FUN_02315ca0) and shoots, an arrow less */
	static _Alignas(16) char swi[BO_ITEM_REC + 0x10];
	memset(swi, 0, sizeof swi), memcpy(swi + BO_ITEM_REC, &sword, 8);
	uintptr_t swp = (uintptr_t)swi;
	memcpy(g_bwinv + BO_INV_ITEMS, &swp, 8);
	sl = 0, memcpy(g_bwinv + BO_INV_SLOT, &sl, 4);
	g_bow.cfg.release_now = 1;
	int acts0 = g_bw_acts;
	bw_shoot_cycle();
	g_bow.cfg.release_now = 0;
	memcpy(&sl, g_bwinv + BO_INV_SLOT, 4);
	EXPECT(g_bwf.n == f0 + 2 && g_bw_acts == acts0 + 1 && sl == 2 && bw_arrows() == 7 &&
		       log_has("slot 2 activated for the shot (FUN_02315ca0"),
	       "bow: release now: our shots %d, activations %d, the slot %u, arrows %d", g_bwf.n - f0, g_bw_acts - acts0, sl,
	       bw_arrows());
	sl = 2, memcpy(g_bwinv + BO_INV_SLOT, &sl, 4);
	memset(g_bwinv + BO_INV_ITEMS, 0, 8);
	ar = 8, memcpy(g_bwres + BO_RES_COUNT, &ar, 4);
	f0 = g_bwf.n - 1; /* 11c counts from the unlimited shot as before */
	/* 11c. SOMVR_BOW_FIRE_CMD (white box: 121 CombatPrimary04, as if the real fire were found): the release taps it,
	 * our shot is not called; the stand-in game shoots on it (an arrow less, its own) */
	int ev0 = g_ninev, s0 = g_bwg.shots;
	atomic_store(&g_bow.strike_block, 0); /* the round 2 guard (SOMVR_BOW_STRIKE=block) would cut the white box's 121 */
	g_bow.fire_cmd = BW_CMD, g_bwg.fire_cmd = BW_CMD;
	bw_shoot_cycle();
	bframes(12, 0);
	EXPECT(nev('N', BW_CMD, ev0) == 1 && nev('F', BW_CMD, ev0) == 1 && g_bwf.n == f0 + 1 && g_bwg.shots == s0 + 1 &&
		       log_has("fire: the fire command 121 'CombatPrimary04' tapped after"),
	       "bow: SOMVR_BOW_FIRE_CMD: ON %d OFF %d, our shots %d, the game's %d", nev('N', BW_CMD, ev0), nev('F', BW_CMD, ev0),
	       g_bwf.n - f0, g_bwg.shots - s0);
	g_bow.fire_cmd = 0, g_bwg.fire_cmd = 0;
	bw_hand(SOMVR_HAND_R, 0.4f, -0.4f, -0.1f);
	bw_until(SOMVR_BOW_PH_IDLE, 200, 0);
	bframes(2, 0);
	/* 12. a menu while drawing: the aim ends (game: a menu), the aim command's OFF */
	int a0 = nev('F', BW_AIM, 0);
	bw_hand(SOMVR_HAND_R, -0.1f, -0.2f, -0.35f);
	bframe(0);
	bw_grip(1);
	bframes(30, 0);
	EXPECT(g_bow.s.phase == SOMVR_BOW_PH_DRAW && g_bwg.mode, "bow: drawn for the menu");
	g_sm_menus.sc = 5, g_menu_set = 1;
	bframes(2, 0);
	EXPECT(g_bow.s.phase == SOMVR_BOW_PH_IDLE && g_bow.s.last_end == SOMVR_BOW_END_GAME && log_has("exit: game: a menu") &&
		       nev('F', BW_AIM, 0) == a0 + 1,
	       "bow: a menu: phase %d end %d", g_bow.s.phase, g_bow.s.last_end);
	g_sm_menus.sc = 0, g_menu_set = 0;
	bw_grip(0);
	bframes(3, 0);
	/* 13. no hands (fake views): no bow step for 3 frames, the tick ends the aim */
	bw_grip(1);
	bframe(0);
	EXPECT(g_bow.s.phase == SOMVR_BOW_PH_DRAW, "bow: drawn for the stale test");
	g_views_fake = 1;
	bframes(6, 0);
	g_views_fake = 0;
	EXPECT(g_bow.s.phase == SOMVR_BOW_PH_IDLE && g_bow.s.last_end == SOMVR_BOW_END_OUTSIDE &&
		       log_has("exit: outside: no bow step for 3 frames") && g_bow.step_stale == 1,
	       "bow: stale: phase %d end %d", g_bow.s.phase, g_bow.s.last_end);
	bw_grip(0);
	bframes(3, 0);
	/* 14. SOMVR_ARMS: the bow hand's weapon node takes SOMVR_BOW_CAL while the bow is out, turned onto the shot while
	 * drawn (the head base: Talion's arms reach the hands) */
	if (arm0) {
		sa_xf g = { { 1, 2, 3 }, sv_qyaw(0.2f) }, cal = g_arm.cal[SA_L], g1 = g;
		bo_arm_grip(SA_L, g_inp.hands, &g, &cal);
		EXPECT(!memcmp(&g, &g1, sizeof g) && !memcmp(&cal, &g_arm.cal[SA_L], sizeof cal), "bow arms: idle changed the node");
		g_arm.on = 1, g_rigcfg.base = SOMVR_BASE_HEAD;
		g_pobj.pos[0] = 100, g_pobj.pos[1] = 127.5f, g_pobj.pos[2] = 300; /* the head node at (100, 200, 300) */
		somvr_rig_init(&g_rig);
		bw_hand(SOMVR_HAND_L, -0.3f, -0.3f, -0.4f); /* (70, 170, 340), the arms tests' left */
		bw_hand(SOMVR_HAND_R, -0.25f, -0.3f, -0.33f);
		atomic_fetch_add(&g_req_recenter, 1);
		bframes(20, 1);
		float dg, er = arm_err(SA_L, &dg);
		EXPECT(er < 0.2f && dg < 0.5f, "bow arms: before: the left node %.3f cm %.2f deg off its grip x CAL_L", er, dg);
		bw_grip(1);
		bframe(1);
		bw_move(-0.05f, -0.3f, -0.2f, 20, 1);
		bframes(3, 1);
		er = bw_arm_err(&dg, 1);
		float dg2;
		arm_err(SA_L, &dg2);
		EXPECT(g_bow.s.phase == SOMVR_BOW_PH_DRAW && g_arm.rep[SA_L].reached && er < 0.3f && dg < 1.0f && dg2 > 20,
		       "bow arms: drawn: the bow node %.3f cm %.2f deg off (the sword's CAL_L %.2f deg off; reached %d)", er, dg,
		       dg2, g_arm.rep[SA_L].reached);
		g = g1, cal = g_arm.cal[SA_L];
		bo_arm_grip(SA_R, g_inp.hands, &g, &cal);
		EXPECT(!memcmp(&g, &g1, sizeof g) && !memcmp(&cal, &g_arm.cal[SA_L], sizeof cal), "bow arms: the string hand's node");
		g_bow.align = 0;
		bo_arm_grip(SA_L, g_inp.hands, &g, &cal);
		EXPECT(!memcmp(&g, &g1, sizeof g) && !memcmp(&cal, &g_bow.cal, sizeof cal), "bow arms: SOMVR_BOW_ALIGN=0");
		g_bow.align = 1;
		bw_grip(0); /* the shot; then the hands apart: the sword's CAL_L again */
		bw_hand(SOMVR_HAND_R, 0.3f, -0.3f, -0.4f);
		bw_until(SOMVR_BOW_PH_AIM, 60, 1);
		bframes(2, 1);
		er = bw_arm_err(&dg, 0);
		EXPECT(g_bow.s.phase == SOMVR_BOW_PH_AIM && er < 0.3f && dg < 1.0f, "bow arms: aiming: %.3f cm %.2f deg", er, dg);
		bw_until(SOMVR_BOW_PH_IDLE, 120, 1);
		bframes(20, 1);
		er = arm_err(SA_L, &dg);
		EXPECT(g_bow.s.phase == SOMVR_BOW_PH_IDLE && er < 0.2f && dg < 0.5f, "bow arms: after: %.3f cm %.2f deg", er, dg);
		g_arm.on = 0;
	}
	/* 15. off: the call passes through, nothing is held */
	g_bow.on = 0;
	int e0 = g_ninev;
	bw_hand(SOMVR_HAND_R, -0.1f, -0.2f, -0.35f);
	bframe(0);
	bw_grip(1);
	bframes(3, 0);
	EXPECT(g_bow.s.phase == SOMVR_BOW_PH_IDLE && g_ninev == e0 && c_bowcamxf(NULL, xf) == 0x56 && !memcmp(xf, cam, sizeof cam),
	       "bow off: something ran");
	bw_grip(0);
	g_bow.on = 1;
	bo_end_log();
	EXPECT(log_has("BOW END aims=") && log_has("free-aim calls="), "bow: no END line");
	EXPECT(g_bwg.stealth == 0 && nev('N', BW_STEALTH, 0) == 0 && nev('F', BW_STEALTH, 0) == 0,
	       "bow: 125 (PC_Stealth) was sent: %d events", g_bwg.stealth);
	printf("bow    on stand-ins: the nock (aim + draw, the aim command 124 held, a pulse), the game's mode on after 0.2 s with "
	       "our ray from the nock along the draw, the release (our own shot FUN_02332c50 once the game aims, along the "
	       "latched ray, the aim object cleared, an arrow less by FUN_02720a90; no fire command, 125 never), the bow's "
	       "forward while aiming, the ray carried with Talion's walk, a second shot, the exit apart; the button; a dropped "
	       "shot; refused shots (no arrows, not ranged, no item, sites) and unlimited; SOMVR_BOW_FIRE_CMD taps its command; "
	       "a menu; no hands; the arms' bow node%s; off; %d free-aim calls, %llu overrides, %llu shots of ours, %llu "
	       "refused\n", arm0 ? " (SOMVR_BOW_CAL, turned onto the shot)" : " (skipped: arms off)", g_bwg.calls,
	       (unsigned long long)g_bow.overrides, (unsigned long long)g_bow.direct, (unsigned long long)g_bow.refused);
	g_bow.on = 0;
	g_bwg.on = 0;
	g_player.pcomb = pcomb0;
	g_arm.on = arm0;
	g_rig = rig0, g_rigcfg = cfg0;
	memcpy(g_pobj.pos, pos0, sizeof pos0), memcpy(g_pobj.rot, rot0, sizeof rot0);
	g_layer_live = 0;
	g_cb_test_t = -1;
}

/* ---- the review of the features together (docs/re-xr-pipeline.md 16): one test per interaction bug ---------------- */
static uint64_t cb_swings(void) { return g_cb.n.stims + g_cb.n.stim_none; }
/* the world the review tests start from: xr views, layout A, the head base, Talion at (100, 127.5, 300) facing +Z, the
 * layer's controllers up (the hands still), every feature off (each test turns its own on, white box) */
static void rv_world(void)
{
	g_views_fake = 0, g_rt_result = XR_SUCCESS, g_layout = g_rigcfg.layout = SOMVR_LAYOUT_A, g_trig_path[0] = 0;
	g_rigcfg.base = SOMVR_BASE_HEAD, g_rigcfg.yaw = SOMVR_YAW_CAMERA, g_node_ok = 1;
	g_follow.on = 0, g_fb.on = 0, g_cl.on = 0, g_hide.on = 0, g_hudq.on = 0, g_arm.on = 0, g_hb.bone = g_hb.near = 0;
	g_cb.mode = CB_OFF, g_rx.mode = RX_OFF, g_audio = 0, g_cf.out_s = g_cf.in_s = 0, g_cf.seat_cfg.on = 0;
	g_nodexf_node = NULL, g_head_up = 72.5f, g_wall = 0, g_wall_top = INFINITY, g_grav_flag = NULL;
	somvr_rig_init(&g_rig);
	g_rt_head[0] = g_rt_head[1] = g_rt_head[2] = 0, g_rt_q = (sv_quat){ 0, 0, 0, 1 };
	g_rm[0] = g_rm[1] = g_rm[2] = 0;
	g_pobj.pos[0] = 100, g_pobj.pos[1] = 127.5f, g_pobj.pos[2] = 300;
	g_pobj.rot[0] = g_pobj.rot[1] = g_pobj.rot[2] = 0, g_pobj.rot[3] = 1;
	g_pl = &g_player, g_ppm.owner = &g_player;
	g_pz[0x34] = 0;
	g_cb_test_t = 20000;
	g_inp.on = 1, g_inp.pend_snap = 0, g_inp.pend_turn = 0, g_inp.cfg.smooth = 0;
	g_inp.lines = 0, g_inp.lsec = 0, g_inp.hand_next = 1e9;
	g_xrt = g_vst.last_time + 1000000000;
	runtime_up();
	somvr_time_publish(&g_sh, g_xrt, 11111111);
	layer_up();
	for (int h = 0; h < 2; h++)
		g_lst.hand[h].squeeze = 0, g_lst.hand[h].trigger = 0, g_lst.hand[h].stick[0] = g_lst.hand[h].stick[1] = 0,
		g_lst.hand[h].buttons = 0;
	atomic_fetch_add(&g_req_recenter, 1);
}
/* SOMVR_COMBAT with the controller blade: the orcs of cb_world, only orc A near (at a, the root; B C D far), the
 * database loaded (Combat_DealHit is called), the state fresh */
static void rv_combat(float ax, float ay, float az)
{
	cb_world();
	g_cat.count = 6;
	g_oobj[0].pos[0] = ax, g_oobj[0].pos[1] = ay, g_oobj[0].pos[2] = az;
	for (int i = 1; i < 4; i++)
		g_oobj[i].pos[0] = 5000 + 300.0f * (float)i;
	g_cb.mode = CB_FULL, g_cb.blade = CB_BLADE_CONTROLLER, g_cb.prev.valid = 0, g_cb.bhist.have = 0, g_cb.warned = 1;
	g_cb.t0 = g_cb_test_t, g_cb.next_log = g_cb_test_t + 1, g_cb.last_line = g_cb_test_t, g_cb.hs.on = 0, g_cb.stim.on = 0;
	sc_speed_reset(&g_cb.sp), sc_cool_reset(&g_cb.cool), sc_charges_init(&g_cb.ch, &g_cb.cfg, g_cb_test_t);
}
/* the right stick pushed for 3 ticks then released: one controller snap turn (+30 deg) */
static int rv_snap(void (*fr)(void))
{
	int32_t s0 = g_rig.snap_steps;
	g_lst.hand[1].stick[0] = 0.9f;
	for (int i = 0; i < 3; i++)
		fr();
	g_lst.hand[1].stick[0] = 0;
	for (int i = 0; i < 3; i++)
		fr();
	return g_rig.snap_steps - s0;
}
/* SOMVR_BOW on in the review's world (the stand-in game's bow mode, its free-aim call; the arrows unknown) */
static void rv_bow(void)
{
	const float cam[7] = { 100, 200, 300, 0, 0, 0, 1 };
	g_ninev = 0;
	memset(&g_bwg, 0, sizeof g_bwg);
	g_bwg.on = 1;
	memcpy(g_bwg.cam, cam, sizeof cam);
	g_bow.on = 1, g_bow.tid = 0, g_bow.lines = 0, g_bow.next_log = 0, g_bow.cam_seen = 0;
	g_bow.trk_have[0] = g_bow.trk_have[1] = 0;
	somvr_bow_init(&g_bow.s);
}
static void rv_unbow(void)
{
	g_bow.on = 0, g_bwg.on = 0;
	somvr_bow_init(&g_bow.s);
	for (int h = 0; h < 2; h++)
		g_lst.hand[h].squeeze = g_lst.hand[h].trigger = 0;
}
static void bframe0(void) { bframe(0); }
static void review_tests(void)
{
	float pos0[3], rot0[4];
	memcpy(pos0, g_pobj.pos, sizeof pos0), memcpy(rot0, g_pobj.rot, sizeof rot0);
	somvr_rig rig0 = g_rig;
	somvr_rig_cfg cfg0 = g_rigcfg;
	const sa_node *node0 = g_nodexf_node;

	/* 1. SOMVR_ARMS + SOMVR_COMBAT (the bone blade) + a controller snap with the hands held still. Phase 2 solves the arms
	 * with the frame before's rig (the known one-frame lag): after the snap the drawn sword's grip mapped back into
	 * tracking space jumped by the snap's arc (about 70 cm at the tip) and back the frame after - a phantom swing (the
	 * stimulus, a heavy tier) whose world sweep crossed orc A, standing in the arc between the resting blades: a heavy
	 * hit nobody swung. Now the bone blade waits for a phase 2 solved with this frame's rig, and no sweep or tip
	 * velocity crosses a jump of the rig or of the grip's source */
	rv_world();
	g_arm.on = 1, g_arm.lines = 0, g_arm.next_log = g_cb_test_t + 1, g_arm.last_line = g_cb_test_t;
	rv_combat(174.2f, 150, 439.5f); /* 28 deg right of the head, 1.58 m: inside the snap's arc, clear of both rests */
	for (int i = 0; i < 25; i++)
		aframe();
	uint64_t bb0 = g_arm.n.blade_bone, sw0 = cb_swings(), ct0 = g_cb.n.contacts, st0 = g_arm.n.blade_stale;
	int dh0 = g_dh.n;
	aframe();
	EXPECT(g_arm.weight[SA_R] == 1 && g_arm.n.blade_bone == bb0 + 1 && cb_swings() == sw0 && g_cb.n.contacts == ct0,
	       "review 1: before the snap: weight %.2f bone blade %llu swings %llu contacts %llu", g_arm.weight[SA_R],
	       (unsigned long long)(g_arm.n.blade_bone - bb0), (unsigned long long)(cb_swings() - sw0),
	       (unsigned long long)(g_cb.n.contacts - ct0));
	int sn = rv_snap(aframe);
	for (int i = 0; i < 10; i++)
		aframe();
	EXPECT(sn == 1 && cb_swings() == sw0 && g_cb.n.contacts == ct0 && g_dh.n == dh0 && g_arm.n.blade_stale > st0 &&
		       g_arm.weight[SA_R] == 1,
	       "review 1: a snap with the hands still: %d step(s), %llu swings, %llu contacts, %d hits (stale bone %llu)", sn,
	       (unsigned long long)(cb_swings() - sw0), (unsigned long long)(g_cb.n.contacts - ct0), g_dh.n - dh0,
	       (unsigned long long)(g_arm.n.blade_stale - st0));
	/* ... and a recentre with the head 40 cm off its reference: the same */
	g_rt_head[0] = 0.4f;
	for (int i = 0; i < 3; i++)
		aframe();
	atomic_fetch_add(&g_req_recenter, 1);
	for (int i = 0; i < 10; i++)
		aframe();
	EXPECT(cb_swings() == sw0 && g_dh.n == dh0, "review 1: a recentre: %llu swings, %d hits",
	       (unsigned long long)(cb_swings() - sw0), g_dh.n - dh0);
	g_rt_head[0] = 0;
	/* 1b. the arms easing in (the hands back after a loss): the drawn sword travels from the animation's hand to the
	 * grip in 0.15 s - not the player's swing: the controller's grip until the arm is fully on */
	g_layer_live = 0, g_ls_fail = 1;
	for (int i = 0; i < 30; i++)
		aframe();
	g_layer_live = 1, g_ls_fail = 0;
	sw0 = cb_swings();
	int blended = 0;
	for (int i = 0; i < 25; i++) {
		aframe();
		blended += g_arm.weight[SA_R] > 0 && g_arm.weight[SA_R] < 1;
	}
	EXPECT(blended >= 10 && cb_swings() == sw0 && g_dh.n == dh0 && g_arm.weight[SA_R] == 1,
	       "review 1b: easing in over %d frames: %llu swings, %d hits", blended, (unsigned long long)(cb_swings() - sw0),
	       g_dh.n - dh0);
	g_arm.on = 0, g_cb.mode = CB_OFF;

	/* 2. SOMVR_COMBAT in the pause menu: H4 and stereo go on (the world behind the menu), the hands point at the menu -
	 * a swing hit the frozen orc through Combat_DealHit. Now no blade while the game is paused; unpaused the same swing
	 * is a heavy hit */
	for (int pass = 0; pass < 2; pass++) {
		rv_world();
		g_pobj.pos[1] = 120;
		rv_combat(100, 150, 420);
		g_pz[0x34] = pass == 0;
		for (int i = 0; i < 4; i++)
			iframe();
		uint64_t s0 = cb_swings(), h0 = g_cb.n.hits[SC_TIER_HEAVY];
		int d0 = g_dh.n;
		for (int i = 0; i < 60; i++) { /* input_tests' swing: 80 deg right, 160 deg to the left in 0.25 s */
			float ph = (float)i / 90.0f, a = ph < 0.2f ? 80 : ph < 0.45f ? 80 - 160 * (ph - 0.2f) / 0.25f : -80;
			float r = -a * 0.01745329252f * 0.5f;
			g_lpose[1].orientation = (XrQuaternionf){ 0, sinf(r), 0, cosf(r) };
			iframe();
		}
		g_lpose[1].orientation = (XrQuaternionf){ 0, 0, 0, 1 };
		if (pass == 0)
			EXPECT(g_dh.n == d0 && cb_swings() == s0 && g_cb.n.paused >= 60,
			       "review 2: paused: %d hits, %llu swings (paused frames %llu)", g_dh.n - d0,
			       (unsigned long long)(cb_swings() - s0), (unsigned long long)g_cb.n.paused);
		else
			EXPECT(g_dh.n == d0 + 1 && g_cb.n.hits[SC_TIER_HEAVY] == h0 + 1, "review 2: unpaused: %d hits (heavy %llu)",
			       g_dh.n - d0, (unsigned long long)(g_cb.n.hits[SC_TIER_HEAVY] - h0));
	}
	g_pz[0x34] = 0, g_cb.mode = CB_OFF;

	/* 3. SOMVR_CLIMB + SOMVR_COMBAT: the sword hand holding a hold pulls Talion up; its pull (5.4 m/s down, tracking
	 * space) was a swing (the stimulus to the orcs, their block rolls). Now no blade while the right hand holds */
	rv_world();
	g_pobj.pos[0] = 500, g_pobj.pos[1] = 100, g_pobj.pos[2] = -50;
	memset(g_clP, 0, sizeof g_clP);
	memcpy(g_clP, &g_player, sizeof g_player);
	g_clP[CL_P_FSM] = CL_FSM_SUPPORTED;
	g_pl = (st_player_t *)g_clP, g_ppm.owner = g_clP;
	cl_set_proxy(&g_ppc);
	g_ppc.grav = 1;
	g_wall = 1, g_wall_z = CLW_Z - CLW_R, g_wall_top = CLW_TOP;
	g_grav_flag = &g_ppc.grav, g_ground_y = 100;
	g_cl.on = 1, g_cl.tid = 0, g_cl.next_log = 0, g_cl.cfg.top_mode = SOMVR_CLIMB_TOP_RELEASE;
	g_cl.cfg.letgo = SOMVR_CLIMB_LETGO_FALL; /* both open = released (build 10's hang: 18g) */
	g_cl.cfg.cue = 0, g_cl.cfg.inside_cm = 0; /* no reach cue's rays, no inside ray (build 10: 18h) */
	somvr_climb_init(&g_cl.s);
	rv_combat(5000, 100, 5000);
	cl_hand(SOMVR_HAND_R, 0.2f, -0.2f, -0.45f);
	cl_hand(SOMVR_HAND_L, -0.2f, -0.2f, -0.2f);
	cl_frames(4);
	cl_grip(SOMVR_HAND_R, 1);
	iframe();
	EXPECT(g_cl.s.active && g_cl.s.h[1].holding, "review 3: no hold");
	uint64_t s3 = cb_swings(), c3 = g_cb.n.climbing;
	float y3 = g_pobj.pos[1];
	for (int i = 0; i < 8; i++) {
		g_lpose[1].position.y -= 0.06f;
		iframe();
	}
	cl_frames(3);
	EXPECT(g_pobj.pos[1] - y3 > 40 && cb_swings() == s3 && g_cb.n.climbing > c3,
	       "review 3: pulled %.1f cm with the sword hand: %llu swings (climbing frames %llu)", g_pobj.pos[1] - y3,
	       (unsigned long long)(cb_swings() - s3), (unsigned long long)(g_cb.n.climbing - c3));
	cl_grip(SOMVR_HAND_R, 0);
	cl_frames(3);
	g_cl.on = 0, g_cb.mode = CB_OFF;
	g_wall = 0, g_wall_top = INFINITY, g_grav_flag = NULL;
	cl_set_proxy(NULL);

	/* 4. SOMVR_HIDE_HEAD=LIST + SOMVR_FALLBACK: in third person (the camera base) Talion's head pieces stayed hidden. Now
	 * third person shows them, as a flat frame does; back in first person they are hidden again */
	rv_world();
	g_layout = g_rigcfg.layout = SOMVR_LAYOUT_B;
	g_pobj.pos[0] = 500, g_pobj.pos[1] = 100, g_pobj.pos[2] = -50;
	g_fb_test_t = 3000;
	fb_world();
	fb_env(NULL, NULL, NULL, NULL, NULL);
	fb_restart(g_fbs);
	g_obj_model = &g_sm, g_sm.mask[0] = g_sm.mask[1] = 0;
	g_hide.ours = 0, g_hide.obj = g_hide.model = 0, g_hide.cnt_model = 0;
	g_hide.on = 1, g_hide.want = 0x28, g_hide.shadow = 1;
	for (int i = 0; i < 5; i++)
		fb_frame1();
	EXPECT(!g_fb.tr.shown && (g_sm.mask[0] & 0x28) == 0x28, "review 4: first person: mask %#x", g_sm.mask[0]);
	atomic_store(&g_fb_req, 1); /* "fallback third" */
	int n4 = fb_until(1, 90);
	fb_frame1();
	EXPECT(n4 > 0 && g_fb.tr.shown && (g_sm.mask[0] & 0x28) == 0 && !g_hide.ours,
	       "review 4: third person after %d frames: head pieces mask %#x (ours %#llx)", n4, g_sm.mask[0],
	       (unsigned long long)g_hide.ours);
	atomic_store(&g_fb_req, 2); /* "fallback first" */
	n4 = fb_until(0, 300);
	fb_frame1();
	EXPECT(n4 > 0 && !g_fb.tr.shown && (g_sm.mask[0] & 0x28) == 0x28, "review 4: first person again after %d: mask %#x", n4,
	       g_sm.mask[0]);

	/* 5. SOMVR_FALLBACK + comfort: the swap to the camera base is a jump for the vignette's speeds whatever its size;
	 * a camera 20 cm from the head (a close shot) was 18 m/s of motion (the ring at full strength after the fade) */
	g_pobj.pos[0] = 100, g_pobj.pos[1] = 107.5f, g_pobj.pos[2] = 300; /* the head at 100 180 300: the camera 20 cm up */
	for (int i = 0; i < 4; i++)
		fb_frame1();
	atomic_store(&g_fb_req, 1);
	somvr_comfort_frame e5;
	int swapped = 0, counted_fast = 0;
	for (int i = 0; i < 300 && !swapped; i++) { /* the switch waits for the 1.5 s hold of the last one */
		int was = g_fb.tr.shown;
		fb_frame1();
		e5 = cf_entry("review 5");
		swapped = !was && g_fb.tr.shown;
		counted_fast += (e5.flags & SOMVR_CFF_COUNTED) && e5.lin_mps > 5;
	}
	EXPECT(swapped && (e5.flags & SOMVR_CFF_JUMP) && !(e5.flags & SOMVR_CFF_COUNTED) && !counted_fast && e5.lin_mps < 1,
	       "review 5: the swap frame: flags %#x lin %.2f m/s (fast counted frames %d)", e5.flags, e5.lin_mps, counted_fast);
	atomic_store(&g_fb_req, 0);
	g_fb.on = 0, g_hide.on = 0;
	g_hide.ours = 0, g_hide.obj = g_hide.model = 0, g_sm.mask[0] = g_sm.mask[1] = 0;
	g_pl = &g_player, g_ppm.owner = &g_player;
	g_fb_test_t = -1;

	/* 6. SOMVR_FOLLOW + SOMVR_ARMS: phase 2 carries the last rig along by Talion's move since, but the follow's move does
	 * not carry the view (H1 shifts the rig's origin by it): the hands were put that far ahead of the grips for a frame
	 * (a 20 cm step: 20 cm). Now the follow's applied move is taken out */
	rv_world();
	g_arm.on = 1, g_follow.on = 1;
	for (int i = 0; i < 25; i++)
		aframe();
	uint64_t fm0 = g_follow.moves, fo0 = g_arm.n.follow_out;
	float worst = 0, deg;
	g_rt_head[2] = -0.2f; /* the head 20 cm forward in the room: one 20 cm follow step */
	for (int i = 0; i < 6; i++) {
		aframe();
		worst = fmaxf(worst, arm_err(SA_R, &deg));
	}
	EXPECT(g_follow.moves == fm0 + 1 && g_arm.n.follow_out == fo0 + 1 && worst < 0.5f,
	       "review 6: the follow's step (%llu moves): the weapon node off its grip by up to %.2f cm",
	       (unsigned long long)(g_follow.moves - fm0), worst);
	g_rt_head[2] = 0;
	/* 7. ... with SOMVR_COMBAT and the sword hand out of Talion's reach (the arm at full stretch toward it): a follow
	 * catch-up step (30 cm in one frame) moved the shoulder and with it the drawn sword 30 cm in tracking space - a
	 * swing (27 m/s in one sample) nobody made. Now a step longer than 5 cm is a jump for the bone blade */
	rv_world();
	g_arm.on = 1, g_follow.on = 1;
	rv_combat(5000, 100, 5000);
	g_lpose[1].position.z = -1.5f; /* 1.5 m ahead: out of reach */
	for (int i = 0; i < 30; i++)
		aframe();
	uint64_t sw7 = cb_swings(), bb7 = g_arm.n.blade_bone;
	fm0 = g_follow.moves;
	g_rt_head[2] = -0.3f;
	for (int i = 0; i < 8; i++)
		aframe();
	EXPECT(!g_arm.rep[SA_R].reached && g_arm.n.blade_bone > bb7 && g_follow.moves == fm0 + 1 && cb_swings() == sw7,
	       "review 7: a 30 cm follow step with the arm at its reach (reached %d, bone blade %llu): %llu moves, %llu swings",
	       g_arm.rep[SA_R].reached, (unsigned long long)(g_arm.n.blade_bone - bb7), (unsigned long long)(g_follow.moves - fm0),
	       (unsigned long long)(cb_swings() - sw7));
	g_rt_head[2] = 0;
	g_lpose[1].position.z = -0.4f;
	g_arm.on = 0, g_follow.on = 0, g_cb.mode = CB_OFF;
	/* 13 (VR build 3 together, docs/re-xr-pipeline.md 19.2): SOMVR_ARMS + SOMVR_COMBAT + a fast body turn (SOMVR_LOCO's
	 * body yaw: up to SOMVR_LOCO_YAW_DPS, 720 deg/s by default, 2000 allowed; the game's own turns) with the sword hand
	 * held still: with the arm at its reach the drawn sword swings with the shoulder - at 1440 deg/s a swing (4.4 m/s)
	 * nobody made, at 675 deg/s 2.2 m/s. Now a body turn over 360 deg/s with the arm at its reach is a jump for the bone
	 * blade (as 7's follow step); an arm that reaches holds the hand (still: under 1 m/s) */
	static const struct { float deg, z; int reach; } p13[3] = { { 7.5f, -1.5f, 0 }, { 16.0f, -1.5f, 0 }, { 16.0f, -0.4f, 1 } };
	for (int pass = 0; pass < 3; pass++) {
		rv_world();
		g_arm.on = 1;
		rv_combat(5000, 100, 5000);
		g_lpose[1].position.z = p13[pass].z;
		for (int i = 0; i < 30; i++)
			aframe();
		uint64_t sw13 = cb_swings(), bb13 = g_arm.n.blade_bone, tu13 = g_cb.n.turns;
		float mps13 = 0;
		for (int i = 1; i <= 12; i++) { /* 12 frames at 90 Hz */
			float a = 0.5f * p13[pass].deg * (float)i * 0.01745329252f;
			g_pobj.rot[1] = sinf(a), g_pobj.rot[3] = cosf(a);
			aframe();
			mps13 = fmaxf(mps13, g_cb.sp.mean_mps);
		}
		for (int i = 0; i < 10; i++)
			aframe();
		EXPECT(g_arm.rep[SA_R].reached == p13[pass].reach && g_arm.n.blade_bone > bb13 && cb_swings() == sw13 &&
			       (p13[pass].reach ? mps13 < 1 : g_cb.n.turns >= tu13 + 10) && mps13 < 3,
		       "review 13: the body turned %.0f deg/s, the hand %s (reached %d, bone blade %llu): %llu swings, the blade's "
		       "mean speed up to %.2f m/s, %llu body-turn jumps", p13[pass].deg * 90, p13[pass].reach ? "in reach" : "beyond it",
		       g_arm.rep[SA_R].reached, (unsigned long long)(g_arm.n.blade_bone - bb13),
		       (unsigned long long)(cb_swings() - sw13), mps13, (unsigned long long)(g_cb.n.turns - tu13));
		g_pobj.rot[1] = 0, g_pobj.rot[3] = 1;
		g_lpose[1].position.z = -0.4f;
		g_arm.on = 0, g_cb.mode = CB_OFF;
	}

	/* ---- the merge with SOMVR_BOW (main's bow, the same classes of bug) ---- */
	/* 8. SOMVR_BOW + SOMVR_FOLLOW: the free-aim ray is carried by Talion's move since the bow step, but the follow's
	 * extra move does not carry the view or the hands (the next H1 shifts the rig's origin by it): the ray's origin sat
	 * a follow step (20 cm) off the nock for a frame. Now the follow's applied move is taken out, as the arms' phase 2 */
	rv_world();
	g_follow.on = 1;
	rv_bow();
	bw_hand(SOMVR_HAND_L, -0.1f, -0.2f, -0.4f), bw_hand(SOMVR_HAND_R, 0.3f, -0.3f, -0.2f);
	bframes(25, 0);
	g_lst.hand[SOMVR_HAND_L].trigger = 1; /* the aim button: the bow's forward from the nock */
	bframes(40, 0);
	uint64_t fm8 = g_follow.moves, fo8 = g_bow.follow_out;
	float worst8 = 0;
	g_rt_head[2] = -0.2f; /* the head 20 cm forward in the room: one 20 cm follow step */
	for (int i = 0; i < 6; i++) {
		sv_vec3 o = g_bow.origin; /* the step the next free-aim call carries */
		bframe(0);
		worst8 = fmaxf(worst8, sqrtf((g_bwg.xf[0] - o.x) * (g_bwg.xf[0] - o.x) + (g_bwg.xf[1] - o.y) * (g_bwg.xf[1] - o.y) +
					     (g_bwg.xf[2] - o.z) * (g_bwg.xf[2] - o.z)));
	}
	EXPECT(g_bow.s.phase == SOMVR_BOW_PH_AIM && g_bwg.mode && g_follow.moves == fm8 + 1 && g_bow.follow_out == fo8 + 1 &&
		       worst8 < 0.05f,
	       "review 8: the bow's ray over a follow step (phase %d, %llu moves, %llu out): up to %.2f cm off the nock",
	       g_bow.s.phase, (unsigned long long)(g_follow.moves - fm8), (unsigned long long)(g_bow.follow_out - fo8), worst8);
	g_rt_head[2] = 0;
	g_lst.hand[SOMVR_HAND_L].trigger = 0;
	rv_unbow();

	/* 9. SOMVR_BOW + SOMVR_CLIMB, the same squeeze: drawing the bow with the hands within reach of a climbable wall
	 * grabbed the wall (the climb's step ran first), the bow never drew; the bow hand's squeeze on the wall while the
	 * bow was out started a climb and dropped the bow. Now the bow steps first, and while it is out the climb takes no
	 * new hold (a press then is spent: the grip must open before it grabs) */
	rv_world();
	g_pobj.pos[0] = 500, g_pobj.pos[1] = 100, g_pobj.pos[2] = -50;
	memset(g_clP, 0, sizeof g_clP);
	memcpy(g_clP, &g_player, sizeof g_player);
	g_clP[CL_P_FSM] = CL_FSM_SUPPORTED;
	g_pl = (st_player_t *)g_clP, g_ppm.owner = g_clP;
	cl_set_proxy(&g_ppc);
	g_ppc.grav = 1;
	g_wall = 1, g_wall_z = CLW_Z - CLW_R, g_wall_top = CLW_TOP;
	g_grav_flag = &g_ppc.grav, g_ground_y = 100;
	g_cl.on = 1, g_cl.tid = 0, g_cl.next_log = 0, g_cl.cfg.top_mode = SOMVR_CLIMB_TOP_RELEASE;
	g_cl.cfg.letgo = SOMVR_CLIMB_LETGO_FALL; /* both open = released (build 10's hang: 18g) */
	g_cl.cfg.cue = 0, g_cl.cfg.inside_cm = 0; /* no reach cue's rays, no inside ray (build 10: 18h) */
	somvr_climb_init(&g_cl.s);
	rv_bow();
	cl_hand(SOMVR_HAND_L, 0.2f, -0.2f, -0.5f), cl_hand(SOMVR_HAND_R, 0.2f, -0.2f, -0.45f); /* both within the wall's reach */
	bframes(4, 0);
	uint64_t gr9 = g_cl.s.grabs;
	cl_grip(SOMVR_HAND_R, 1); /* the string at the nock (5 cm) */
	bframes(3, 0);
	int drew9 = g_bow.s.phase == SOMVR_BOW_PH_DRAW, climb9 = g_cl.s.active;
	cl_grip(SOMVR_HAND_L, 1); /* the bow hand's squeeze on the wall while drawn */
	bframes(3, 0);
	EXPECT(drew9 && !climb9 && g_bow.s.phase == SOMVR_BOW_PH_DRAW && !g_cl.s.active && g_cl.s.grabs == gr9 &&
		       g_ppc.grav == 1,
	       "review 9: the nock next to the wall: drawn %d (phase %d), a climb %d/%d (grabs %llu)", drew9, g_bow.s.phase,
	       climb9, g_cl.s.active, (unsigned long long)(g_cl.s.grabs - gr9));
	cl_grip(SOMVR_HAND_R, 0); /* the shot; the string hand away: the bow ends 0.8 s later, the bow hand still squeezed */
	bframe(0);
	cl_hand(SOMVR_HAND_R, 0.8f, -0.4f, -0.1f);
	bw_until(SOMVR_BOW_PH_IDLE, 200, 0);
	bframes(5, 0);
	EXPECT(g_bow.s.phase == SOMVR_BOW_PH_IDLE && g_bow.s.last_end == SOMVR_BOW_END_APART && !g_cl.s.active &&
		       g_cl.s.grabs == gr9,
	       "review 9: the bow over (%s), the bow hand still squeezed: a climb %d", somvr_bow_end_name(g_bow.s.last_end),
	       g_cl.s.active);
	cl_grip(SOMVR_HAND_L, 0);
	bframe(0);
	cl_grip(SOMVR_HAND_L, 1);
	bframes(2, 0);
	EXPECT(g_cl.s.active && g_cl.s.h[SOMVR_HAND_L].holding && g_ppc.grav == 0, "review 9: opened and pressed: a hold %d",
	       g_cl.s.active);
	cl_grip(SOMVR_HAND_L, 0);
	bframes(3, 0);
	g_cl.on = 0;
	g_wall = 0, g_wall_top = INFINITY, g_grav_flag = NULL;
	cl_set_proxy(NULL);
	rv_unbow();

	/* 10. SOMVR_BOW_HAND=right + SOMVR_ARMS + SOMVR_COMBAT: while the bow is out the sword hand's weapon node takes
	 * SOMVR_BOW_CAL (0,2,0,50,30,0 here: the blade's line 40 deg off CAL_R's); the first frame after the bow the bone
	 * blade read that pose back through CAL_R^-1 and the next frame's grip jumped by the difference: a swing nobody made
	 * (the arm at its reach: the bone tip's own deltas, as review 7). Now a pose with the bow's node is no bone blade
	 * (the controller's grip, then a jump of the grip's source) */
	rv_world();
	g_arm.on = 1, g_arm.lines = 0, g_arm.next_log = g_cb_test_t + 1, g_arm.last_line = g_cb_test_t;
	rv_combat(5000, 100, 5000);
	rv_bow();
	int bh0 = g_bow.cfg.bow_hand;
	sa_xf bcal0 = g_bow.cal;
	const float bc10[6] = { 0, 2, 0, 50, 30, 0 };
	g_bow.cfg.bow_hand = SOMVR_HAND_R, g_bow.cal = sa_xf_euler(bc10);
	g_lpose[1].position.z = -1.5f; /* 1.5 m ahead: out of reach */
	bframes(30, 1);
	uint64_t sw10 = cb_swings(), bb10 = g_arm.n.blade_bone, bw10 = g_arm.n.blade_bow;
	g_lst.hand[SOMVR_HAND_R].trigger = 1; /* the aim button on the bow hand */
	bframes(10, 1);
	int out10 = g_bow.s.phase == SOMVR_BOW_PH_AIM;
	g_lst.hand[SOMVR_HAND_R].trigger = 0; /* the hands apart (60 cm): the bow ends after 0.8 s */
	bw_until(SOMVR_BOW_PH_IDLE, 200, 1);
	bframes(20, 1);
	EXPECT(out10 && g_bow.s.phase == SOMVR_BOW_PH_IDLE && !g_arm.rep[SA_R].reached && g_arm.n.blade_bow > bw10 &&
		       g_arm.n.blade_bone > bb10 + 15 && cb_swings() == sw10,
	       "review 10: the bow in the sword hand (out %d, the bow's node %llu frames, reached %d), then the bone blade "
	       "(%llu): %llu swings", out10, (unsigned long long)(g_arm.n.blade_bow - bw10), g_arm.rep[SA_R].reached,
	       (unsigned long long)(g_arm.n.blade_bone - bb10), (unsigned long long)(cb_swings() - sw10));
	g_bow.cfg.bow_hand = bh0, g_bow.cal = bcal0;
	g_lpose[1].position.z = -0.4f;
	g_arm.on = 0, g_cb.mode = CB_OFF;
	rv_unbow();

	/* 11. SOMVR_BOW, the string hand untracked for a moment (the step keeps its last grip for lost_s) and a snap: the
	 * kept grip stayed where it was in the world while the bow hand turned with the view - the draw line swung about
	 * 8 deg. Now a lost hand stays where it was in the room, mapped by this frame's rig */
	rv_world();
	rv_bow();
	bw_hand(SOMVR_HAND_L, -0.1f, -0.2f, -0.4f), bw_hand(SOMVR_HAND_R, -0.1f, -0.2f, -0.35f);
	bframes(25, 0);
	bw_grip(1);
	bframe(0);
	bw_move(-0.05f, -0.25f, -0.1f, 20, 0); /* drawn 31 cm */
	bframes(3, 0);
	sv_quat lq;
	somvr_hand_game(&g_inp.hands[SOMVR_HAND_L], NULL, &lq, NULL, NULL);
	float hb = sv_heading(somvr_rig_quat(&g_rig, g_fr.base, lq));
	sv_vec3 rel0 = sv_qrot(sv_qyaw(-hb), g_bow.dir);
	uint64_t ca11 = g_bow.carried;
	g_ls_lost[SOMVR_HAND_R] = 1, g_lst.hand[SOMVR_HAND_R].grip_flags = 0;
	bframes(2, 0);
	int sn11 = rv_snap(bframe0);
	bframes(2, 0);
	int lost11 = !g_inp.hands[SOMVR_HAND_R].valid && g_bow.s.phase == SOMVR_BOW_PH_DRAW;
	somvr_hand_game(&g_inp.hands[SOMVR_HAND_L], NULL, &lq, NULL, NULL);
	float hb1 = sv_heading(somvr_rig_quat(&g_rig, g_fr.base, lq));
	sv_vec3 rel1 = sv_qrot(sv_qyaw(-hb1), g_bow.dir);
	float dd = sqrtf((rel1.x - rel0.x) * (rel1.x - rel0.x) + (rel1.y - rel0.y) * (rel1.y - rel0.y) +
			 (rel1.z - rel0.z) * (rel1.z - rel0.z));
	EXPECT(sn11 == 1 && lost11 && g_bow.carried > ca11 && fabsf(hb1 - hb) > 0.4f && dd < 1e-3f,
	       "review 11: a snap (%d) with the string hand lost (%d, carried %llu): the draw line moved %.4f against the bow "
	       "(heading %.2f -> %.2f rad)", sn11, lost11, (unsigned long long)(g_bow.carried - ca11), dd, hb, hb1);
	g_ls_lost[SOMVR_HAND_R] = 0, g_lst.hand[SOMVR_HAND_R].grip_flags = 0xf;
	bw_grip(0);
	bframes(3, 0);
	rv_unbow();

	/* 12. SOMVR_VRMENU + SOMVR_COMBAT (main's VR menu, merged later): the wrist panel open, the main hand pointing and
	 * clicking at it while the game runs on behind it - the sword hand's sweep hit the orc in front, as in the pause
	 * menu (2). Now no blade while the layer shows the panel; closed, the same swing is a heavy hit */
	for (int pass = 0; pass < 2; pass++) {
		rv_world();
		g_pobj.pos[1] = 120;
		rv_combat(100, 150, 420);
		typeof(g_vm) vm0 = g_vm;
		g_vm.on = 1; /* white box: the menu on, the layer's panel open (pass 0) or closed */
		atomic_store(&g_vmo.open, pass == 0 ? 1u : 0u);
		for (int i = 0; i < 4; i++)
			iframe();
		uint64_t s12 = cb_swings(), h12 = g_cb.n.hits[SC_TIER_HEAVY], v12 = g_cb.n.vrmenu;
		int d12 = g_dh.n;
		for (int i = 0; i < 60; i++) { /* input_tests' swing, as review 2 */
			float ph = (float)i / 90.0f, a = ph < 0.2f ? 80 : ph < 0.45f ? 80 - 160 * (ph - 0.2f) / 0.25f : -80;
			float r = -a * 0.01745329252f * 0.5f;
			g_lpose[1].orientation = (XrQuaternionf){ 0, sinf(r), 0, cosf(r) };
			iframe();
		}
		g_lpose[1].orientation = (XrQuaternionf){ 0, 0, 0, 1 };
		if (pass == 0)
			EXPECT(g_dh.n == d12 && cb_swings() == s12 && g_cb.n.vrmenu >= v12 + 60,
			       "review 12: the VR menu open: %d hits, %llu swings (menu frames %llu)", g_dh.n - d12,
			       (unsigned long long)(cb_swings() - s12), (unsigned long long)(g_cb.n.vrmenu - v12));
		else
			EXPECT(g_dh.n == d12 + 1 && g_cb.n.hits[SC_TIER_HEAVY] == h12 + 1 && g_cb.n.vrmenu == v12,
			       "review 12: the VR menu closed: %d hits (heavy %llu)", g_dh.n - d12,
			       (unsigned long long)(g_cb.n.hits[SC_TIER_HEAVY] - h12));
		atomic_store(&g_vmo.open, 0u);
		g_vm = vm0;
	}
	g_cb.mode = CB_OFF;

	printf("review the bone blade over a snap and a recentre (no swing, no hit), the arms easing in (the controller's "
	       "grip), no blade paused or climbing, the head pieces shown in third person, the fallback's swap a jump for the "
	       "vignette, the follow's step out of the arms' carried rig (%.2f cm), a catch-up step with the arm at its reach "
	       "(no swing); with the bow: the follow's step out of its ray (%.2f cm), the nock next to a climbable wall (the "
	       "bow, no hold), the bow in the sword hand (no swing after), a snap with the string hand lost (the draw line "
	       "kept); no blade with the VR menu open\n", worst, worst8);
	g_rig = rig0, g_rigcfg = cfg0, g_nodexf_node = node0;
	memcpy(g_pobj.pos, pos0, sizeof pos0), memcpy(g_pobj.rot, rot0, sizeof rot0);
	g_layer_live = 0;
	g_pl = NULL;
	g_cb_test_t = -1;
}

/* ---- everything at once (docs/re-xr-pipeline.md 16): every switch on, a few hundred frames of events ----------- */
static struct {
	int frames, stereo, flat, held, third, climbing, hud_frames, faded, bad_pm, both_moves, bad_grav, bad_eyes, bad_hud,
		bad_cf, bad_third, bad_fade, bad_aud, swing_at, bow, bad_bow;
	int first_bad[8];
	uint64_t moves0;
	/* SOMVR_VRMENU's gesture gate: the bits the next input tick must publish (this frame's states), checked frames,
	 * mismatches (the first one), frames with each cause */
	uint32_t vm_want;
	uint64_t vm_tick;
	int vm_checked, bad_vm, vm_first, vm_bow, vm_climb, vm_swing;
} g_ao;
static void ao_bad(int k, int *n)
{
	if (!*n && k < 8)
		g_ao.first_bad[k] = g_ao.frames;
	(*n)++;
}
/* one game frame as aframe orders it (the layer's wait and sync, the input tick, the player update, phase 2, H4, the
 * composite and Present), the fallback's clock with it, the listener's call after it; then the invariants */
static void ao_frame(void)
{
	g_fb_test_t += 1.0 / 90;
	int pm0 = g_pm_consumed, act0 = g_cl.s.active;
	uint64_t fm0 = g_follow.moves, cm0 = g_cl.pm_moves, sw0 = cb_swings();
	aframe();
	call_aud(0);
	g_ao.frames++;
	/* SOMVR_VRMENU's gate: this frame's input tick published the last frame's bow, climb and swing (white box: the
	 * menu's game side on); then this frame's states for the next one */
	if (g_vm.on && g_ao.vm_tick && g_inp.ticks == g_ao.vm_tick + 1) {
		uint32_t got = atomic_load(&g_vmo.block);
		g_ao.vm_checked++;
		g_ao.vm_bow += (got & SOMVR_VMG_BOW) != 0, g_ao.vm_climb += (got & SOMVR_VMG_CLIMB) != 0;
		g_ao.vm_swing += (got & SOMVR_VMG_SWING) != 0;
		if (got != g_ao.vm_want && !g_ao.bad_vm++)
			g_ao.vm_first = g_ao.frames;
	}
	g_ao.vm_want = (bo_out() ? SOMVR_VMG_BOW : 0u) |
		       (g_cl.s.active && (g_cl.s.h[0].holding || g_cl.s.h[1].holding) ? SOMVR_VMG_CLIMB : 0u) |
		       (g_cb.sp.above ? SOMVR_VMG_SWING : 0u);
	g_ao.vm_tick = g_inp.ticks;
	if (cb_swings() != sw0 && !g_ao.swing_at) /* a swing nobody made (the hands still or holding): where */
		g_ao.swing_at = g_ao.frames;
	int stereo = g_h4_calls == 2;
	g_ao.stereo += stereo, g_ao.flat += !stereo, g_ao.third += stereo && g_fb.tr.shown;
	g_ao.climbing += g_cl.s.active;
	/* SOMVR_BOW: while it is out no climb hold and no blade grip (the hands are on the bow) */
	g_ao.bow += bo_out();
	if (bo_out() && (g_cl.s.active || g_inp.grip_ready))
		g_ao.bad_bow++;
	/* one extra move per player update at most, never the follow's while a climb is on */
	if (g_pm_consumed - pm0 > 1 || g_follow.moves - fm0 + g_cl.pm_moves - cm0 > 1)
		ao_bad(0, &g_ao.bad_pm);
	if (act0 && g_follow.moves != fm0)
		ao_bad(1, &g_ao.both_moves);
	/* the proxy's gravity flag: off exactly while a climb is on */
	if (g_ppc.grav != (g_cl.s.active ? 0 : 1))
		ao_bad(2, &g_ao.bad_grav);
	if (!stereo)
		return;
	/* the eye images of this frame, from textures made at this 3D surface's size (no stale ones after a mode set) */
	somvr_tag t;
	somvr_eyes_info in;
	memset(&in, 0, sizeof in);
	int ok = somvr_tag_get(&g_sh, atomic_load(&g_sh.tag_seq), &t) && t.frame == g_frame_no && t.eyes == 3 &&
		 somvr_eyes_read(&g_sh, &in) && in.gen == t.eye_gen && in.w == g_surf.desc.w && in.h == g_surf.desc.h &&
		 g_et.desc.w == g_surf.desc.w && g_et.desc.h == g_surf.desc.h;
	for (int e = 0; ok && e < 2; e++) {
		st_tex_t *tx = g_et.tex[e];
		ok = tx && tx->desc.w == g_surf.desc.w && tx->content == 0xA000u + (uint32_t)g_frame_no * 4u + (uint32_t)e &&
		     tx->f3d->image && in.image[e] == tx->f3d->image;
	}
	if (!ok)
		ao_bad(3, &g_ao.bad_eyes);
	/* the HUD: this frame's pass in the HUD texture and named by the present, or no entry naming one */
	uint32_t hg = 0, hk = 0;
	int found = hud_tag_of_last(&hg, &hk);
	st_tex_t *ht = g_ht.tex;
	if (g_hud_none ? !found || hg != 0 : !found || !hg || hg != g_ht.pub.gen || !ht ||
						     ht->content != 0xB000u + (uint32_t)g_frame_no * 4u + 1u)
		ao_bad(4, &g_ao.bad_hud);
	g_ao.hud_frames += !g_hud_none;
	/* comfort: an entry for every stereo frame */
	somvr_comfort_frame ce;
	if (!somvr_comfort_get(&g_cft, g_frame_no, &ce))
		ao_bad(5, &g_ao.bad_cf);
	/* third person: the camera base, no head collapse, no arm rig, no combat player */
	if (g_fb.tr.shown && (g_fr.head_ok || g_hb.live || g_arm.have_rig))
		ao_bad(6, &g_ao.bad_third);
	/* the fade only while the fallback turns, and the listener on the head in every stereo frame */
	somvr_rec r;
	uint32_t lv = somvr_rec_read(&g_sh, g_frame_no, &r) ? somvr_rec_fade(r.flags) : 999u;
	g_ao.faded += lv > 0 && lv < 999;
	if (lv == 999 || (lv && g_fb.tr.phase == SOMVR_FBT_IDLE))
		ao_bad(7, &g_ao.bad_fade);
	if (g_aud.pos != g_head.pos)
		g_ao.bad_aud++;
}
static void ao_frames(int n)
{
	for (int i = 0; i < n; i++)
		ao_frame();
}
/* both hands moved to (l) and (r) over n frames (XR space, m; slow: no swing) */
static void ao_glide(const float l[3], const float r[3], int n)
{
	XrVector3f a[2] = { g_lpose[0].position, g_lpose[1].position };
	const float *b[2] = { l, r };
	for (int i = 1; i <= n; i++) {
		float k = (float)i / (float)n;
		for (int h = 0; h < 2; h++)
			g_lpose[h].position = (XrVector3f){ a[h].x + (b[h][0] - a[h].x) * k, a[h].y + (b[h][1] - a[h].y) * k,
							    a[h].z + (b[h][2] - a[h].z) * k };
		ao_frame();
	}
}
static void allon_tests(void)
{
	float pos0[3], rot0[4];
	memcpy(pos0, g_pobj.pos, sizeof pos0), memcpy(rot0, g_pobj.rot, sizeof rot0);
	somvr_rig rig0 = g_rig;
	somvr_rig_cfg cfg0 = g_rigcfg;
	const sa_node *node0 = g_nodexf_node;
	memset(&g_ao, 0, sizeof g_ao);
	rv_world();
	/* the player: the fallback's stand-in Player (its FSM byte Supported, no mount, every field the detector reads),
	 * 50 cm before the climbing wall, facing it; the climb's proxy with its gravity flag */
	g_layout = g_rigcfg.layout = SOMVR_LAYOUT_B;
	g_pobj.pos[0] = 500, g_pobj.pos[1] = 100, g_pobj.pos[2] = -50;
	g_fb_test_t = 8000;
	fb_world();
	fb_env(NULL, NULL, NULL, NULL, NULL);
	fb_restart(g_fbs);
	cl_set_proxy(&g_ppc);
	g_ppc.grav = 1;
	g_wall = 1, g_wall_z = CLW_Z - CLW_R, g_wall_top = CLW_TOP;
	g_grav_flag = &g_ppc.grav, g_ground_y = 100;
	g_cl.on = 1, g_cl.tid = 0, g_cl.next_log = 0, g_cl.cfg.top_mode = SOMVR_CLIMB_TOP_RELEASE;
	g_cl.cfg.letgo = SOMVR_CLIMB_LETGO_FALL; /* both open = released (build 10's hang: 18g) */
	g_cl.cfg.cue = 0, g_cl.cfg.inside_cm = 0; /* no reach cue's rays, no inside ray (build 10: 18h) */
	somvr_climb_init(&g_cl.s);
	g_clw = (typeof(g_clw)){ 0, 0, 1, 0, 0 };
	/* every other switch: the follow, the listener, the arms and the head collapse + near plane, the controller blade
	 * (orc A out of reach behind, the database loaded), the vignette + snap blink + seated height, the HUD quad, the
	 * eye size switch (the boot's mode set at the game's size), the pointer (configured at start) */
	g_follow.on = 1, g_audio = 1, g_arm.on = 1, g_hb.bone = 1, g_hb.near = 1;
	g_arm.lines = 0, g_arm.next_log = g_cb_test_t + 1, g_arm.last_line = g_cb_test_t;
	rv_combat(420, 100, -160);
	/* SOMVR_COMBAT_REACT=1 (orc A's block table: a won roll notifies it) and SOMVR_BOW=1 (the stand-in game's bow mode) */
	rx_world();
	g_rx.mode = RX_ON, g_rx.ok = 1, g_rx.next_log = g_cb_test_t + 1, g_rx.last_line = g_cb_test_t;
	sc_react_reset(&g_rx.rs), sc_cool_reset(&g_rx.notified);
	g_rx_rolls[0] = 0.3f, g_rx_nroll = 1, g_rx_ri = 0;
	struct rx_cnt rxn0 = g_rx.n;
	int rxc0 = g_rxc.notify;
	rv_bow();
	somvr_blink_times(90, &g_cf.out_s, &g_cf.in_s);
	g_cf.seat_cfg.on = 1;
	g_vm.on = 1, g_vm.gate = 0, g_vm.lines = 0; /* SOMVR_VRMENU's game side (white box): the gesture's gate each tick */
	memset(g_vm.gate_ticks, 0, sizeof g_vm.gate_ticks);
	g_hudq.on = 1, g_hud_none = 0, g_hud_video = 0;
	g_ht.state = 0, memset(&g_ht.desc, 0, sizeof g_ht.desc);
	g_et.state = 0, memset(&g_et.desc, 0, sizeof g_et.desc);
	g_eye_sz.mode = 1, g_eye_sz.w = 1832, g_eye_sz.h = 1920;
	g_es.sw = 1, g_es.hold_s = 2.0, g_rs_mode = 0;
	es_reset();
	es_game_mode_set(1920);
	EXPECT(g_fb.on && g_cl.on && g_inp.on && g_ptr.on && g_cf.on && g_hudq.on && g_es.sw && atomic_load(&g_es.cur) == 0 &&
		       g_bow.on && g_rx.mode == RX_ON,
	       "allon: switches fb %d climb %d input %d pointer %d comfort %d hud %d es %d cur %d bow %d react %d", g_fb.on,
	       g_cl.on, g_inp.on, g_ptr.on, g_cf.on, g_hudq.on, g_es.sw, atomic_load(&g_es.cur), g_bow.on, g_rx.mode);
	uint64_t sw0 = cb_swings();
	int dh0 = g_dh.n, rs0 = g_rs_calls, creates0 = g_creates;
	/* the first world frames: held flat until the tick's SetResolution, then stereo at the eye size */
	ao_frames(30);
	EXPECT(atomic_load(&g_es.cur) == 1 && g_surf.desc.w == 1832 && g_rs_calls == rs0 + 1 && g_ao.flat >= 1 &&
		       g_ao.stereo >= 25 && g_arm.weight[SA_R] == 1,
	       "allon: the eye size: cur %d surface %u calls %d flat %d stereo %d arms %.2f", atomic_load(&g_es.cur),
	       g_surf.desc.w, g_rs_calls - rs0, g_ao.flat, g_ao.stereo, g_arm.weight[SA_R]);
	/* a snap right (the blink holds it until black), a snap back left */
	int32_t s0 = g_rig.snap_steps;
	g_lst.hand[1].stick[0] = 0.9f;
	ao_frames(3);
	g_lst.hand[1].stick[0] = 0;
	ao_frames(12);
	int32_t s1 = g_rig.snap_steps;
	g_lst.hand[1].stick[0] = -0.9f;
	ao_frames(3);
	g_lst.hand[1].stick[0] = 0;
	ao_frames(12);
	EXPECT(s1 == s0 + 1 && g_rig.snap_steps == s0 && g_cf.blink.blinks >= 2, "allon: snaps %d then %d (blinks %llu)",
	       s1 - s0, g_rig.snap_steps - s0, (unsigned long long)g_cf.blink.blinks);
	/* the HUD off (a cinematic's hidden HUD) and on again */
	g_hud_none = 1;
	ao_frames(20);
	g_hud_none = 0;
	ao_frames(10);
	/* a climb: the right hand grips the stone, pulls 60 cm (the head 20 cm forward in the room meanwhile: no follow
	 * move while holding), lets go: gravity back, he falls, the follow catches up */
	cl_hand(SOMVR_HAND_R, 0.2f, -0.2f, -0.45f);
	cl_hand(SOMVR_HAND_L, -0.2f, -0.2f, -0.2f);
	ao_frames(3);
	cl_grip(SOMVR_HAND_R, 1);
	ao_frames(1);
	int grabbed = g_cl.s.active && g_ppc.grav == 0;
	float y0 = g_pobj.pos[1];
	g_rt_head[2] = -0.2f;
	for (int i = 0; i < 20; i++) {
		g_lpose[1].position.y -= 0.03f;
		ao_frame();
	}
	ao_frames(2);
	float pulled = g_pobj.pos[1] - y0;
	cl_grip(SOMVR_HAND_R, 0);
	uint64_t fm1 = g_follow.moves;
	ao_frames(40);
	EXPECT(grabbed && pulled > 55 && !g_cl.s.active && g_ppc.grav == 1 && NEAR(g_pobj.pos[1], 100, 1e-3) &&
		       g_follow.moves > fm1,
	       "allon: the climb: grabbed %d pulled %.1f cm, now active %d flag %d y %.1f, follow moves after %llu", grabbed,
	       pulled, g_cl.s.active, g_ppc.grav, g_pobj.pos[1], (unsigned long long)(g_follow.moves - fm1));
	g_rt_head[2] = 0;
	ao_frames(10);
	/* the bow drawn with both hands within the wall's reach, a snap while drawn, the shot, the hands apart: no climb
	 * hold, no blade, one shot (our own since P9, refused in this world: no bow item) */
	static const float bl[3] = { 0.2f, -0.2f, -0.5f }, br[3] = { 0.2f, -0.2f, -0.45f }, rest_l[3] = { -0.3f, -0.3f, -0.4f },
			   rest_r[3] = { 0.3f, -0.3f, -0.4f }, away_r[3] = { 0.8f, -0.4f, -0.1f };
	ao_glide(bl, br, 60);
	ao_frames(3);
	uint64_t gr1 = g_cl.s.grabs;
	cl_grip(SOMVR_HAND_R, 1);
	ao_frames(2);
	int drew = g_bow.s.phase == SOMVR_BOW_PH_DRAW;
	for (int i = 0; i < 20; i++) {
		g_lpose[1].position.z += 0.015f, g_lpose[1].position.x += 0.0025f;
		ao_frame();
	}
	g_lst.hand[1].stick[0] = 0.9f;
	ao_frames(3);
	g_lst.hand[1].stick[0] = 0;
	ao_frames(12);
	int32_t s2 = g_rig.snap_steps;
	uint64_t ours0 = g_bow.direct + g_bow.refused;
	cl_grip(SOMVR_HAND_R, 0);
	ao_frames(3);
	/* the shot: since P9 our own (bo_shoot; the review's world has no bow item behind the player's Combat, so it is
	 * refused there: logged, no arrow taken), never a 125 press; the stand-in game's arrows (a fire command) none */
	int shots = (int)(g_bow.direct + g_bow.refused - ours0);
	EXPECT(g_bwg.shots == 0 && g_bwg.stealth == 0, "allon: a fire command went out (%d shots, %d 125 events)", g_bwg.shots,
	       g_bwg.stealth);
	ao_glide(bl, away_r, 30);
	for (int i = 0; i < 200 && g_bow.s.phase != SOMVR_BOW_PH_IDLE; i++)
		ao_frame();
	ao_frames(5);
	EXPECT(drew && shots == 1 && s2 == s0 + 1 && g_bow.s.phase == SOMVR_BOW_PH_IDLE && g_cl.s.grabs == gr1 && g_ao.bow > 100 &&
		       !g_ao.bad_bow,
	       "allon: the bow at the wall: drawn %d, shots %d, the snap %d, phase %d, climb grabs %llu, bow frames %d (a hold "
	       "or a blade grip in %d)", drew, shots, s2 - s0, g_bow.s.phase, (unsigned long long)(g_cl.s.grabs - gr1), g_ao.bow,
	       g_ao.bad_bow);
	ao_glide(rest_l, rest_r, 60);
	ao_frames(10);
	/* a recentre (the trigger word's) */
	uint64_t rc0 = g_rig.recenters;
	atomic_fetch_add(&g_req_recenter, 1);
	ao_frames(10);
	/* the fallback in ("fallback third") and out ("fallback auto": the 1.5 s hold, then back on the head node) */
	atomic_store(&g_fb_req, 1);
	int in_at = -1, out_at = -1;
	for (int i = 0; i < 160; i++) {
		ao_frame();
		if (in_at < 0 && g_fb.tr.shown)
			in_at = i;
	}
	atomic_store(&g_fb_req, 0);
	for (int i = 0; i < 300 && out_at < 0; i++) {
		ao_frame();
		if (!g_fb.tr.shown && g_fb.tr.phase == SOMVR_FBT_IDLE)
			out_at = i;
	}
	ao_frames(20);
	EXPECT(g_rig.recenters == rc0 + 1 && in_at > 0 && out_at > 0 && g_ao.third > 100 && g_ao.faded >= 20 && g_fr.head_ok &&
		       g_arm.weight[SA_R] == 1,
	       "allon: recentres %llu, the fallback in after %d out after %d frames (third %d, faded %d), head %d arms %.2f",
	       (unsigned long long)(g_rig.recenters - rc0), in_at, out_at, g_ao.third, g_ao.faded, g_fr.head_ok,
	       g_arm.weight[SA_R]);
	/* a menu (no H4) past the hold: the game's size; back in the world at another eye size (the runtime's own, published
	 * late): the eye textures made again at it, the tags naming the new generation */
	uint32_t gen0 = g_et.pub.gen;
	for (int i = 0; i < 200; i++) {
		g_cb_test_t += 1.0 / 90, g_fb_test_t += 1.0 / 90;
		((t_vf04_fn)g_vtin[4])(&g_cwbi);
	}
	int menu_size = atomic_load(&g_es.cur) == 0 && g_surf.desc.w == 1920 && g_surf.desc.h == 1080;
	g_eye_sz.w = 1600, g_eye_sz.h = 1680;
	ao_frames(60);
	EXPECT(menu_size && atomic_load(&g_es.cur) == 1 && g_surf.desc.w == 1600 && g_et.pub.w == 1600 && g_et.pub.h == 1680 &&
		       g_et.pub.gen > gen0 && g_creates >= creates0 + 4 && g_rs_calls == rs0 + 3,
	       "allon: the menu's size %d, then the eye size %ux%u (textures %ux%u gen %u -> %u, made %d), console calls %d",
	       menu_size, g_surf.desc.w, g_surf.desc.h, g_et.pub.w, g_et.pub.h, gen0, g_et.pub.gen, g_creates - creates0,
	       g_rs_calls - rs0);
	/* the invariants over all of it */
	EXPECT(g_ao.frames > 400 && g_ao.stereo > 350 && !g_ao.bad_pm && !g_ao.both_moves && !g_ao.bad_grav && g_ao.climbing >= 20,
	       "allon: %d frames (%d stereo): extra moves bad %d (first %d) follow+climb %d (first %d), gravity flag bad %d "
	       "(first %d), climbing %d", g_ao.frames, g_ao.stereo, g_ao.bad_pm, g_ao.first_bad[0], g_ao.both_moves,
	       g_ao.first_bad[1], g_ao.bad_grav, g_ao.first_bad[2], g_ao.climbing);
	EXPECT(!g_ao.bad_eyes && !g_ao.bad_hud && !g_ao.bad_cf && g_ao.hud_frames > 300,
	       "allon: stale or missing eye images in %d frames (first %d), HUD tags wrong in %d (first %d), comfort entries "
	       "missing in %d (first %d); HUD frames %d", g_ao.bad_eyes, g_ao.first_bad[3], g_ao.bad_hud, g_ao.first_bad[4],
	       g_ao.bad_cf, g_ao.first_bad[5], g_ao.hud_frames);
	EXPECT(!g_ao.bad_third && !g_ao.bad_fade && !g_ao.bad_aud,
	       "allon: third person with the head base/collapse/arm rig in %d frames (first %d), a fade outside a turn or no "
	       "record in %d (first %d), the listener not on the head in %d", g_ao.bad_third, g_ao.first_bad[6], g_ao.bad_fade,
	       g_ao.first_bad[7], g_ao.bad_aud);
	EXPECT(cb_swings() == sw0 && g_dh.n == dh0 && g_rx.n.swings == rxn0.swings && g_rx.n.rolls == rxn0.rolls &&
		       g_rxc.notify == rxc0,
	       "allon: %llu swings, %d hits, %llu react rolls and %d notifies nobody made (snaps, the recentre, the climb's "
	       "pull, the bow, the fallback, the mode sets; the first at frame %d)", (unsigned long long)(cb_swings() - sw0),
	       g_dh.n - dh0, (unsigned long long)(g_rx.n.rolls - rxn0.rolls), g_rxc.notify - rxc0, g_ao.swing_at);
	/* and a swing that is made: orc A ahead, the sword hand 80 deg right to 80 deg left in 0.25 s (input_tests'), with
	 * everything still on: a hit, the react's roll (0.3 against 0.6: the orc notified) */
	g_oobj[0].pos[0] = 500, g_oobj[0].pos[1] = 130, g_oobj[0].pos[2] = 70;
	ao_frames(5);
	uint64_t sw1 = cb_swings();
	int dh1 = g_dh.n;
	for (int i = 0; i < 60; i++) {
		float ph = (float)i / 90.0f, a = ph < 0.2f ? 80 : ph < 0.45f ? 80 - 160 * (ph - 0.2f) / 0.25f : -80;
		float r = -a * 0.01745329252f * 0.5f;
		g_lpose[1].orientation = (XrQuaternionf){ 0, sinf(r), 0, cosf(r) };
		ao_frame();
	}
	g_lpose[1].orientation = (XrQuaternionf){ 0, 0, 0, 1 };
	ao_frames(10);
	EXPECT(cb_swings() > sw1 && g_dh.n > dh1 && g_rx.n.rolls > rxn0.rolls && g_rxc.notify > rxc0,
	       "allon: a made swing at orc A: %llu swings, %d hits, %llu react rolls, %d notifies",
	       (unsigned long long)(cb_swings() - sw1), g_dh.n - dh1, (unsigned long long)(g_rx.n.rolls - rxn0.rolls),
	       g_rxc.notify - rxc0);
	/* the VR menu's gesture gate over all of it: the bow, the climb's hold, the made swing each published at the next
	 * input tick, nothing else (re-xr-pipeline.md 15.7) */
	EXPECT(!g_ao.bad_vm && g_ao.vm_checked > 600 && g_ao.vm_bow >= g_ao.bow - 2 && g_ao.vm_bow > 100 &&
		       g_ao.vm_climb >= 20 && g_ao.vm_swing >= 3 && g_vm.gate_ticks[0] >= (uint64_t)g_ao.vm_bow &&
		       log_has("VRMENU gate on: the bow (the palm gesture opens nothing meanwhile)") &&
		       log_has("VRMENU gate on: a climb hold") && log_has("VRMENU gate on: a blade swing") &&
		       log_has("VRMENU gate off: nothing"),
	       "allon: the VR menu's gate: %d checked frames, %d wrong (the first at frame %d), bow %d (out %d) climb %d swing %d",
	       g_ao.vm_checked, g_ao.bad_vm, g_ao.vm_first, g_ao.vm_bow, g_ao.bow, g_ao.vm_climb, g_ao.vm_swing);
	printf("allon  every switch at once, %d frames (%d stereo, %d flat, %d in third person, %d climbing, %d with the bow "
	       "out): the eye size switched 3 times (1832x1920, 1920x1080, 1600x1680), 3 snaps with the blink, the HUD off and "
	       "on, a climb (%.1f cm), the bow drawn at the wall and shot, a recentre, the fallback in and out; one extra move "
	       "per tick, the flag, the eye images and HUD tags of every frame, comfort's entries, no hold or blade with the "
	       "bow out, no phantom swing or react roll; then a made swing: hit and rolled\n", g_ao.frames, g_ao.stereo,
	       g_ao.flat, g_ao.third, g_ao.climbing, g_ao.bow, pulled);
	/* back to the others' world */
	rv_unbow();
	rx_unworld();
	g_rx.mode = RX_OFF;
	g_vm.on = 0, g_vm.gate = 0, atomic_store(&g_vmo.block, 0);
	g_fb.on = 0, g_cl.on = 0, g_follow.on = 0, g_audio = 0, g_arm.on = 0, g_hb.bone = g_hb.near = 0, g_cb.mode = CB_OFF;
	g_cf.out_s = g_cf.in_s = 0, g_cf.seat_cfg.on = 0, g_hudq.on = 0, g_es.sw = 0, g_eye_sz.w = 1832, g_eye_sz.h = 1920;
	atomic_store(&g_es.cur, -1);
	g_wall = 0, g_wall_top = INFINITY, g_grav_flag = NULL;
	cl_set_proxy(NULL);
	g_pl = &g_player, g_ppm.owner = &g_player;
	g_layout = SOMVR_LAYOUT_A;
	g_rig = rig0, g_rigcfg = cfg0, g_nodexf_node = node0;
	memcpy(g_pobj.pos, pos0, sizeof pos0), memcpy(g_pobj.rot, rot0, sizeof rot0);
	g_layer_live = 0;
	g_pl = NULL;
	g_cb_test_t = -1, g_fb_test_t = -1;
}

/* ---- SOMVR_VRMENU (vm_tests): the game side of the VR settings menu; the test plays the layer on somvr_vrmenu ---- */
static void vm_put_file(const char *path, const char *text)
{
	FILE *f = fopen(path, "w");
	if (f)
		fputs(text, f), fclose(f);
}
static int vm_slurp(const char *path, char *buf, size_t cap)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return buf[0] = 0, 0;
	size_t n = fread(buf, 1, cap - 1, f);
	fclose(f);
	buf[n] = 0;
	return 1;
}
/* the menu's part of somvr_start again (white box): vm_config (after in_config), vm_start (after ex_start) */
static void vm_restart(void)
{
	atomic_store(&g_vm_writer_off, 1);
	memset(&g_vmo, 0, sizeof g_vmo);
	vm_config();
	vm_start();
}
/* the layer: a click changed the settings (somvr_vrmenu_publish: set, then gen + 1) */
static somvr_settings vm_cur(void)
{
	somvr_settings s;
	memset(&s, 0, sizeof s);
	somvr_vrmenu_read(&g_vmo, &s);
	return s;
}
static void vm_tests(const char *base)
{
	static const unsigned char zero[sizeof g_vmo];
	/* (allon_tests ran the menu's gesture gate white box: its "VRMENU gate" lines; nothing else of the menu) */
	EXPECT(dlsym(RTLD_DEFAULT, SOMVR_VRMENU_SYMBOL) == (void *)&g_vmo && !memcmp(&g_vmo, zero, sizeof g_vmo) && !g_vm.on &&
		       !log_has("VRMENU config") && !log_has("VRMENU apply") && !log_has("SOMVR_VRMENU"),
	       "SOMVR_VRMENU unset: the object is not zero or the menu ran");
	somvr_rig_cfg cfg0 = g_rigcfg;
	sc_ctl_cfg ic0 = g_inp.cfg;
	float hs0 = g_inp.hap_scale, vig0 = g_cf.vignette, out0 = g_cf.out_s, in0 = g_cf.in_s;
	uint32_t bl0 = g_cf.blink_ms;
	somvr_seat_cfg seat0 = g_cf.seat_cfg;
	int fb0 = g_fb.on, hap0 = g_inp.hap_on;
	/* the switches as somvr_start configured them (white box): snap 30, smooth 120 deg/s, vignette 0.6, blink 90 */
	g_rigcfg.snap_rad = 30 / VM_DEG, g_inp.cfg.smooth = 0, g_inp.cfg.smooth_rate = 120 / VM_DEG, g_inp.hap_scale = 1;
	g_cf.vignette = 0.6f, g_cf.blink_ms = 90, somvr_blink_times(90, &g_cf.out_s, &g_cf.in_s);
	g_cf.seat_cfg = (somvr_seat_cfg){ 0, 0, SOMVR_SEAT_DEFAULT_CM };
	g_fb.on = 1, g_fb.forced = 0, atomic_store(&g_fb_req, -1); /* SOMVR_FALLBACK=auto: the view row is available */
	char dir[512], path[600], buf[4096], want[4096];
	snprintf(dir, sizeof dir, "%s/vm", base);
	mkdir(dir, 0755);
	snprintf(path, sizeof path, "%s/settings.conf", dir);
	setenv("SOMVR_VRMENU", "1", 1);
	setenv("SOMVR_SETTINGS", path, 1);
	/* 1. without SOMVR_INPUT: a WARN, the object zero, the file not read */
	g_inp.on = 0;
	vm_restart();
	g_inp.on = 1;
	EXPECT(log_has("WARN SOMVR_VRMENU=1 without SOMVR_INPUT=1: no VR menu") && !g_vm.on && !g_vm.want &&
		       !memcmp(&g_vmo, zero, sizeof g_vmo) && !somvr_vrmenu_ok(&g_vmo),
	       "no SOMVR_INPUT: on %d, the object not zero", g_vm.on);
	/* 2. the file over the switches: a comment, three keys (blanks, a CR, quotes), a comma decimal and an unknown key
	 * (not taken), the view */
	vm_put_file(path, "# my settings\nSNAP_DEG=45\nTURN_DPS = 90 \r\nVIGNETTE=0,4\nBOGUS=1\nHAPTICS=\"50\"\nVIEW=third\n");
	vm_restart();
	somvr_settings s = vm_cur();
	EXPECT(g_vm.on && somvr_vrmenu_ok(&g_vmo) && g_vmo.size == 208 && g_vmo.version == SOMVR_VRMENU_VERSION &&
		       g_vmo.pid == (int32_t)getpid() && g_vmo.avail == (SOMVR_VMA_VIEW | SOMVR_VMA_HUD | SOMVR_VMA_COMFORT | sz_vm_avail()) &&
		       g_vmo.blink_on_ms == 90 && atomic_load(&g_vmo.gen) == 1,
	       "vrmenu header: ok %d size %u avail %#llx blink %u gen %u", somvr_vrmenu_ok(&g_vmo), g_vmo.size,
	       (unsigned long long)g_vmo.avail, g_vmo.blink_on_ms, atomic_load(&g_vmo.gen));
	EXPECT(g_vm.file_mask == ((1u << SOMVR_SK_SNAP_DEG) | (1u << SOMVR_SK_TURN_DPS) | (1u << SOMVR_SK_HAPTICS) |
				  (1u << SOMVR_SK_VIEW)) &&
		       s.turn == 0 && NEAR(s.snap_deg, 45, 1e-4) && NEAR(s.turn_dps, 90, 1e-4) && NEAR(s.vignette, 0.6, 1e-6) &&
		       s.blink_ms == 90 && !s.seated && s.height_cm == 0 && s.hud == 1 && NEAR(s.hud_dist_m, 1.5, 1e-6) &&
		       s.hand == 0 && s.view == 1 && NEAR(s.haptics, 0.5, 1e-6),
	       "start settings: mask %#x snap %g dps %g vignette %g view %u haptics %g", g_vm.file_mask, s.snap_deg, s.turn_dps,
	       s.vignette, s.view, s.haptics);
	EXPECT(NEAR(g_rigcfg.snap_rad * VM_DEG, 45, 1e-3) && NEAR(g_inp.cfg.smooth_rate * VM_DEG, 90, 1e-3) &&
		       NEAR(g_inp.hap_scale, 0.5, 1e-6) && g_inp.cfg.smooth == 0 && NEAR(g_cf.vignette, 0.6, 1e-6) &&
		       atomic_load(&g_fb_req) == 1,
	       "the file's values not applied at start: snap %g dps %g haptics %g fb_req %d", g_rigcfg.snap_rad * VM_DEG,
	       g_inp.cfg.smooth_rate * VM_DEG, g_inp.hap_scale, atomic_load(&g_fb_req));
	EXPECT(log_has("VRMENU config on: settings TURN=snap SNAP_DEG=45 TURN_DPS=90 VIGNETTE=0.6 SNAP_BLINK_MS=90 SEATED=0 "
		       "HEIGHT_CM=0 HUD=on HUD_DIST_M=1.5 HAND=right VIEW=third HAPTICS=50") &&
		       log_has("keys taken SNAP_DEG=45 TURN_DPS=90 VIEW=third HAPTICS=50, bad lines 2, the first: line 4: a value "
			       "the key does not take") &&
		       log_has("VRMENU apply SNAP_DEG=45 from the file") && log_has("VRMENU apply VIEW=third from the file (fallback "
										 "third)") &&
		       log_has("WARN VRMENU") && !log_has("VRMENU apply TURN=") && !log_has("VRMENU apply VIGNETTE="),
	       "no VRMENU config/apply lines for the file");
	g_fb.on = 0, atomic_store(&g_fb_req, -1); /* white box: no fallback frames from here */
	/* 3. the layer's clicks applied live (the input tick, the world) */
	g_views_fake = 0, g_rt_result = XR_SUCCESS, g_layout = g_rigcfg.layout = SOMVR_LAYOUT_A;
	g_rigcfg.base = SOMVR_BASE_CAMERA, g_rigcfg.yaw = SOMVR_YAW_CAMERA, g_follow.on = 0, g_cb.mode = CB_OFF;
	g_cl.on = 0, g_bow.on = 0, g_hide.on = 0, g_hudq.on = 0, g_arm.on = 0, g_trig_path[0] = 0;
	g_cf.out_s = g_cf.in_s = 0;
	g_cb_test_t = 12000;
	g_xrt = g_vst.last_time + 1000000000;
	runtime_up();
	g_inp.on = 1, g_inp.lines = 0, g_inp.lsec = 0;
	layer_up();
	for (int h = 0; h < 2; h++)
		g_lst.hand[h].squeeze = g_lst.hand[h].trigger = 0, g_lst.hand[h].buttons = 0, g_lst.hand[h].stick[0] =
											      g_lst.hand[h].stick[1] = 0;
	g_pl = &g_player;
	g_sm_menus.sc = 0, g_menu_set = 0;
	iframe(), iframe();
	EXPECT(g_inp.live && g_inp.ctx == SC_CTX_WORLD, "vrmenu: not live in the world (%d %d)", g_inp.live, g_inp.ctx);
	uint64_t ap0 = g_vm.applies;
	s.turn = 1, s.snap_deg = 60, s.turn_dps = 150, s.haptics = 1.25f, s.vignette = 0.4f, s.blink_ms = 0, s.hand = 1;
	s.hud = 0, s.hud_dist_m = 2;
	somvr_vrmenu_publish(&g_vmo, &s);
	iframe();
	EXPECT(g_inp.cfg.smooth == 1 && NEAR(g_rigcfg.snap_rad * VM_DEG, 60, 1e-3) && NEAR(g_inp.cfg.smooth_rate * VM_DEG, 150, 1e-3) &&
		       NEAR(g_inp.hap_scale, 1.25, 1e-6) && NEAR(g_cf.vignette, 0.4, 1e-6) && g_cf.blink_ms == 0 && g_cf.out_s == 0 &&
		       g_cf.in_s == 0 && g_inp.cfg.left_handed == 1 && g_vm.applies == ap0 + 9 && g_vm.gen == 2,
	       "live: smooth %d snap %g dps %g haptics %g vignette %g blink %u applies %llu", g_inp.cfg.smooth,
	       g_rigcfg.snap_rad * VM_DEG, g_inp.cfg.smooth_rate * VM_DEG, g_inp.hap_scale, g_cf.vignette, g_cf.blink_ms,
	       (unsigned long long)(g_vm.applies - ap0));
	EXPECT(log_has("VRMENU apply TURN=smooth from the menu") && log_has("VRMENU apply SNAP_DEG=60 from the menu") &&
		       log_has("VRMENU apply HUD=off from the menu (the layer's HUD quad)") &&
		       log_has("VRMENU apply HAND=left from the menu (the panel's wrist and the pointing hand; the controls do not "
			       "swap yet)"),
	       "no VRMENU apply lines for the clicks");
	/* the snap angle is the rig's: a controller snap now turns 60 deg */
	somvr_rig_init(&g_rig);
	atomic_fetch_add(&g_req_recenter, 1);
	iframe();
	s.turn = 0;
	somvr_vrmenu_publish(&g_vmo, &s);
	iframe();
	g_lst.hand[1].stick[0] = 1;
	iframe();
	g_lst.hand[1].stick[0] = 0;
	iframe(), iframe();
	sv_vec3 fw = sv_qrot(sv_quat_of(g_h4_view[0].quat), (sv_vec3){ 0, 0, 1 });
	EXPECT(g_rig.snap_steps == 1 && NEAR(fabsf(atan2f(fw.x, fw.z)), 1.0471976, 1e-4), "snap with the menu's angle: steps "
	       "%d heading %g", g_rig.snap_steps, atan2f(fw.x, fw.z));
	/* seated play: a recentre is asked for and latches the seated height at the next stereo frame */
	uint32_t rq0 = atomic_load(&g_req_recenter);
	uint64_t rc0 = g_rig.recenters, lat0 = g_cf.seat_latches;
	s.seated = 1, s.blink_ms = 140;
	somvr_vrmenu_publish(&g_vmo, &s);
	iframe();
	EXPECT(g_cf.seat_cfg.on == 1 && atomic_load(&g_req_recenter) == rq0 + 1 && g_rig.recenters == rc0 + 1 &&
		       g_cf.seat_latches == lat0 + 1 && g_cf.blink_ms == 140 && NEAR(g_cf.out_s, 0.14 / 3, 1e-6),
	       "seated: on %d recentre requests +%u recentres +%llu latches +%llu blink %u", g_cf.seat_cfg.on,
	       atomic_load(&g_req_recenter) - rq0, (unsigned long long)(g_rig.recenters - rc0),
	       (unsigned long long)(g_cf.seat_latches - lat0), g_cf.blink_ms);
	g_cf.out_s = g_cf.in_s = 0; /* white box: snap turns at once again */
	/* the view row: the fallback's forced third person and back to auto */
	g_fb.on = 1;
	s.view = 0;
	somvr_vrmenu_publish(&g_vmo, &s);
	sc_tick_out o0;
	memset(&o0, 0, sizeof o0);
	vm_tick(&o0, 0, g_cb_test_t);
	EXPECT(atomic_load(&g_fb_req) == 0 && log_has("VRMENU apply VIEW=first from the menu (fallback auto)"), "view first: %d",
	       atomic_load(&g_fb_req));
	g_fb.on = 0, atomic_store(&g_fb_req, -1);
	/* a torn read (the layer mid-write) waits for the next tick */
	atomic_fetch_add(&g_vmo.seq, 1);
	s.haptics = 0.75f;
	memcpy((void *)&g_vmo.set, &s, sizeof s);
	atomic_fetch_add(&g_vmo.gen, 1);
	iframe();
	EXPECT(NEAR(g_inp.hap_scale, 1.25, 1e-6), "a torn read was applied (%g)", g_inp.hap_scale);
	atomic_fetch_add(&g_vmo.seq, 1);
	iframe();
	EXPECT(NEAR(g_inp.hap_scale, 0.75, 1e-6), "the write's end was not applied (%g)", g_inp.hap_scale);
	/* 4. the panel's Recentre */
	rc0 = g_rig.recenters;
	atomic_fetch_add(&g_vmo.recenters, 1);
	iframe();
	EXPECT(g_rig.recenters == rc0 + 1 && log_has("VRMENU recentre (the panel's button, 1 so far)"), "panel recentre: +%llu",
	       (unsigned long long)(g_rig.recenters - rc0));
	/* 5. the left menu button: a long press toggles the panel and never reaches the game; a short one does at its release */
	int e0 = g_ninev, during = 0;
	uint32_t tg0 = atomic_load(&g_vmo.toggles);
	ipress(0, SOMVR_BTN_MENU);
	for (int i = 0; i < 70; i++) /* 0.78 s */
		iframe(), during += poll25(SC_CMD_MENU);
	irelease(0, SOMVR_BTN_MENU);
	iframe(), iframe();
	EXPECT(atomic_load(&g_vmo.toggles) == tg0 + 1 && nev('N', SC_CMD_MENU, e0) == 0 && during == 0 &&
		       log_has("VRMENU menu button held 0.6"),
	       "long press: toggles +%u, Menu events %d, polled on %d", atomic_load(&g_vmo.toggles) - tg0,
	       nev('N', SC_CMD_MENU, e0), during);
	e0 = g_ninev;
	uint64_t taps0 = g_vm.taps;
	ipress(0, SOMVR_BTN_MENU);
	iframe(), iframe(), iframe();
	int held_on = nev('N', SC_CMD_MENU, e0);
	irelease(0, SOMVR_BTN_MENU);
	iframe();
	int at_release = nev('N', SC_CMD_MENU, e0);
	iframe();
	EXPECT(held_on == 0 && at_release == 1 && nev('F', SC_CMD_MENU, e0) == 1 && g_vm.taps == taps0 + 1 &&
		       atomic_load(&g_vmo.toggles) == tg0 + 1,
	       "short press (world): on while held %d, at the release %d, off %d", held_on, at_release, nev('F', SC_CMD_MENU, e0));
	g_sm_menus.sc = 5, g_menu_set = 1; /* a menu: the button is the pad's START */
	iframe();
	e0 = g_ninev;
	ipress(0, SOMVR_BTN_MENU);
	iframe(), iframe();
	held_on = nev('N', SC_CMD_MENU, e0);
	irelease(0, SOMVR_BTN_MENU);
	iframe(), iframe();
	EXPECT(held_on == 0 && nev('N', SC_CMD_MENU, e0) == 1, "short press (menu): on while held %d, after %d", held_on,
	       nev('N', SC_CMD_MENU, e0));
	/* 6. the panel's trigger (consume): in a menu RT presses command 56 (ptr_tests' binding) unless the panel has it */
	e0 = g_ninev;
	uint64_t cs0 = g_vm.consumed;
	atomic_store(&g_vmo.open, 1), atomic_store(&g_vmo.consume, 1u << SOMVR_HAND_R);
	g_lst.hand[1].trigger = 1;
	iframe(), iframe();
	EXPECT(nev('N', 56, e0) == 0 && g_vm.consumed == cs0 + 2 && g_inp.st.hand[1].trigger == 0,
	       "consume: command 56 on %d, consumed %llu, the snapshot's trigger %g", nev('N', 56, e0),
	       (unsigned long long)(g_vm.consumed - cs0), g_inp.st.hand[1].trigger);
	atomic_store(&g_vmo.consume, 0); /* the ray left the panel mid-press: the press stays the panel's */
	iframe();
	EXPECT(nev('N', 56, e0) == 0, "the press changed owner mid-press");
	g_lst.hand[1].trigger = 0;
	iframe();
	atomic_store(&g_vmo.open, 0); /* closed: the trigger is the pad's */
	g_lst.hand[1].trigger = 1;
	iframe();
	EXPECT(nev('N', 56, e0) == 1, "the panel closed: RT did not reach the pad (%d)", nev('N', 56, e0));
	g_lst.hand[1].trigger = 0;
	iframe();
	g_sm_menus.sc = 0, g_menu_set = 0;
	/* 7. the save: 1 s after the last change, the file's keys + the changed ones, '.' decimals */
	somvr_settings cur = vm_cur();
	double T = 50000;
	EXPECT(vm_save_tick(T, 0, 0) == 0 && vm_save_tick(T + 0.5, 0, 0) == 0 && vm_save_tick(T + 1.1, 0, 0) == 1 &&
		       vm_save_tick(T + 3, 0, 0) == 0,
	       "save timing (saves %llu)", (unsigned long long)g_vm.saves);
	uint32_t keys = (1u << SOMVR_SK_SNAP_DEG) | (1u << SOMVR_SK_TURN_DPS) |
			(1u << SOMVR_SK_VIGNETTE) | (1u << SOMVR_SK_SNAP_BLINK_MS) | (1u << SOMVR_SK_SEATED) |
			(1u << SOMVR_SK_HUD) | (1u << SOMVR_SK_HUD_DIST_M) | (1u << SOMVR_SK_HAND) | (1u << SOMVR_SK_VIEW) |
			(1u << SOMVR_SK_HAPTICS);
	somvr_set_format(&cur, keys, want, sizeof want);
	char tmp[1200];
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	EXPECT(vm_slurp(path, buf, sizeof buf) && !strcmp(buf, want) && strstr(buf, "\nVIGNETTE=0.4\n") &&
		       strstr(buf, "\nSNAP_DEG=60\n") && strstr(buf, "\nHAPTICS=75\n") && strstr(buf, "\nVIEW=first\n") &&
		       !strstr(buf, "HEIGHT_CM") && access(tmp, F_OK) != 0 && g_vm.keys == keys &&
		       log_has("VRMENU save ") && log_has("keys SNAP_DEG=60 TURN_DPS=150 VIGNETTE=0.4"),
	       "saved file (keys %#x):\n%s\nwant:\n%s", g_vm.keys, buf, want);
	/* a comma locale: still '.' in the file, and the file reads back the same */
	int comma = 0;
	if (setlocale(LC_NUMERIC, "de_DE.UTF-8") || setlocale(LC_NUMERIC, "de_DE.utf8")) {
		char t[16];
		snprintf(t, sizeof t, "%.1f", 0.5);
		comma = !strcmp(t, "0,5");
	}
	cur.vignette = 0.3f, cur.hud_dist_m = 2.5f;
	somvr_vrmenu_publish(&g_vmo, &cur);
	iframe();
	EXPECT(vm_save_tick(T + 10, 1, 0) == 1 && vm_slurp(path, buf, sizeof buf) && strstr(buf, "\nVIGNETTE=0.3\n") &&
		       strstr(buf, "\nHUD_DIST_M=2.5\n") && !strstr(buf, "0,3") &&
		       !strstr(buf, "2,5"),
	       "saved under %s: %s", comma ? "de_DE" : "C", buf);
	vm_restart(); /* the file read back (under the same locale) */
	EXPECT(g_vm.on && g_vm.file_mask == keys && somvr_set_diff(&g_vm.start, &cur) == 0 && NEAR(g_cf.vignette, 0.3, 1e-6),
	       "read back: mask %#x diff %#x", g_vm.file_mask, somvr_set_diff(&g_vm.start, &cur));
	/* v4: the sword angle from the menu, live (bd_vm_set; SOMVR_ARMS decides whether a sword turns), then back */
	float sw0 = g_bd.angle_deg;
	cur.sword_deg = 10;
	somvr_vrmenu_publish(&g_vmo, &cur);
	iframe();
	EXPECT(log_has("VRMENU apply SWORD_ANGLE=10 from the menu") && (!g_arm.on || NEAR(g_bd.angle_deg, 10, 1e-6)),
	       "the sword angle from the menu: %g", g_bd.angle_deg);
	cur.sword_deg = sw0;
	somvr_vrmenu_publish(&g_vmo, &cur);
	iframe();
	setlocale(LC_NUMERIC, "C");
	printf("vrmenu  somvr_vrmenu exported, zero without SOMVR_INPUT; the file over the switches (a comma decimal and an "
	       "unknown key refused); clicks applied live (turn, snap angle = the rig's, speed, haptics, seated -> a recentre, "
	       "the view); a torn read waits; the panel's Recentre; the menu button's long press toggles unseen, a short one "
	       "reaches the game at its release (world and menu); the panel's trigger kept from the pad per press; saved 1 s "
	       "after the last change with the file's + the changed keys, '.' decimals%s, read back, folders made, an "
	       "unwritable place WARNed, the writer thread's own save\n", comma ? " under de_DE" : " (de_DE not installed)");
	/* the directories above the file are made */
	snprintf(g_vm.path, sizeof g_vm.path, "%s/vm/new/deeper/settings.conf", base);
	cur.snap_deg = 15;
	somvr_vrmenu_publish(&g_vmo, &cur);
	char deep[1100];
	snprintf(deep, sizeof deep, "%s", g_vm.path);
	EXPECT(vm_save_tick(T + 20, 1, 0) == 1 && vm_slurp(deep, buf, sizeof buf) && strstr(buf, "\nSNAP_DEG=15\n"),
	       "no file in the new directories: %s", buf);
	/* an unwritable place: one WARN, the menu goes on */
	snprintf(g_vm.path, sizeof g_vm.path, "/proc/somvr-no/settings.conf");
	cur.snap_deg = 30;
	somvr_vrmenu_publish(&g_vmo, &cur);
	EXPECT(vm_save_tick(T + 30, 1, 0) == 0 && g_vm.save_fails >= 1 && log_has("WARN VRMENU settings file directory for"),
	       "an unwritable path: fails %llu", (unsigned long long)g_vm.save_fails);
	/* the writer thread saves on its own (real time) */
	snprintf(g_vm.path, sizeof g_vm.path, "%s", path);
	uint64_t sv0 = g_vm.saves;
	cur.snap_deg = 90;
	somvr_vrmenu_publish(&g_vmo, &cur);
	atomic_store(&g_vm_writer_off, 0);
	for (int i = 0; i < 40 && g_vm.saves == sv0; i++) {
		struct timespec ts = { 0, 100 * 1000 * 1000 };
		nanosleep(&ts, NULL);
	}
	atomic_store(&g_vm_writer_off, 1);
	pthread_mutex_lock(&g_vm_save_mu);
	uint64_t sv1 = g_vm.saves;
	pthread_mutex_unlock(&g_vm_save_mu);
	EXPECT(sv1 == sv0 + 1 && vm_slurp(path, buf, sizeof buf) && strstr(buf, "\nSNAP_DEG=90\n"),
	       "the writer thread did not save within 4 s (saves %llu)", (unsigned long long)(sv1 - sv0));
	/* build 19 (re-xr-pipeline.md 73): a v6 menu's settings file (build 17b's lines, as its menu wrote them) read by this
	 * build: its 7 keys and values over the switches, no bad line; the Fight page's new rows shown with their features
	 * (white box: the wraith step wanted, the stealth kill on, FRONT's judge hooked, the counter icon hooked) and their
	 * keys following the switches; three Fight changes applied live and saved after the old lines, which stay */
	{
		somvr_step_cfg sc0 = g_step.cfg;
		int hb0 = g_hf.bite, ws_want0 = g_ws.want, ws_on0 = g_ws.on, stl0 = g_stl.mode, stls0 = g_stl.start_mode;
		int ffm0 = g_ff.mode, ffs0 = g_ff.on_score, ffg0 = g_ff.on_grant, ffp0 = g_ff.pace_on, cpf0 = g_cp.ok_fx;
		int cpm0 = atomic_load(&g_cp.mode);
		float gap0 = g_ff.pcfg.gap_s;
		g_ws.want = g_ws.on = 1, g_stl.mode = g_stl.start_mode = STL_ON;
		g_ff.mode = FF_ON, g_ff.on_score = g_ff.on_grant = 1, g_ff.pace_on = 1, g_ff.pcfg.gap_s = 0.7f;
		g_cp.ok_fx = 1, atomic_store(&g_cp.mode, CP_HIDE);
		static const char v6[] = "# Shadow of Mordor VR settings (SOMVR_SETTINGS), written by the in-game VR menu "
					 "(SOMVR_VRMENU=1).\n# KEY=VALUE; a key here wins over its SOMVR_* switch, a key not here "
					 "follows the switch.\nTURN=smooth\nTURN_DPS=90\nVIGNETTE=0.5\nSNAP_BLINK_MS=140\n"
					 "HAPTICS=100\nPHASE=on\nCOMBAT_BITE=on\n";
		snprintf(g_vm.path, sizeof g_vm.path, "%s", path);
		vm_put_file(path, v6);
		vm_restart();
		somvr_settings u = vm_cur();
		uint32_t k7 = 1u << SOMVR_SK_TURN | 1u << SOMVR_SK_TURN_DPS | 1u << SOMVR_SK_VIGNETTE |
			      1u << SOMVR_SK_SNAP_BLINK_MS | 1u << SOMVR_SK_HAPTICS | 1u << SOMVR_SK_PHASE | 1u << SOMVR_SK_COMBAT_BITE;
		uint64_t fb = SOMVR_VMA_WSTEP | SOMVR_VMA_STEALTH | SOMVR_VMA_CROWD | SOMVR_VMA_CICON;
		EXPECT(g_vm.on && g_vm.file_mask == k7 && u.turn == 1 && u.turn_dps == 90 &&
			       NEAR(u.vignette, 0.5, 1e-6) && u.blink_ms == 140 && NEAR(u.haptics, 1.0, 1e-6) && u.phase == 1 &&
			       u.bite == 1 && (g_vmo.avail & fb) == fb && u.wstep == 1 && u.stealth == 1 && u.crowd_ms == 700 &&
			       u.counter_icon == 0 && g_inp.cfg.smooth && NEAR(g_inp.cfg.smooth_rate * VM_DEG, 90, 1e-3),
		       "a v6 file in v7: mask %#x avail %#llx, wstep %u stealth %u pace %u icon %u", g_vm.file_mask,
		       (unsigned long long)g_vmo.avail, u.wstep, u.stealth, u.crowd_ms, u.counter_icon);
		/* the layer's clicks on the Fight page: the pace 1.0 s, the wraith step off, the stealth kill off, the icon on */
		u.crowd_ms = 1000, u.wstep = 0, u.stealth = 0, u.counter_icon = 1;
		somvr_vrmenu_publish(&g_vmo, &u);
		iframe();
		int req = atomic_load(&g_ws.req_onoff);
		EXPECT(log_has("VRMENU apply CROWD_PACE_MS=1000 from the menu") && log_has("VRMENU apply WRAITH_STEP=off from the menu") &&
			       log_has("VRMENU apply STEALTH_KILL=off from the menu") &&
			       log_has("VRMENU apply COUNTER_ICON=game from the menu") && NEAR(g_ff.pcfg.gap_s, 1.0, 1e-6) &&
			       g_ff.pace_on && g_stl.mode == STL_OFF && (req == 0 || !g_ws.on) && atomic_load(&g_cp.mode) == CP_GAME,
		       "the Fight rows live: pace %.2f s on %d, stealth %d, wstep req %d on %d, icon %d", g_ff.pcfg.gap_s,
		       g_ff.pace_on, g_stl.mode, req, g_ws.on, atomic_load(&g_cp.mode));
		u.crowd_ms = 0, u.stealth = 1;
		somvr_vrmenu_publish(&g_vmo, &u);
		iframe();
		EXPECT(!g_ff.pace_on && g_stl.mode == STL_ON, "the pace off (%d), the stealth kill on again (%d)", g_ff.pace_on,
		       g_stl.mode);
		u.crowd_ms = 1000, u.stealth = 0;
		somvr_vrmenu_publish(&g_vmo, &u);
		iframe();
		somvr_settings w6 = g_vm.start;
		char w7[4096];
		somvr_set_format(&u, k7 | 1u << SOMVR_SK_WRAITH_STEP | 1u << SOMVR_SK_STEALTH_KILL | 1u << SOMVR_SK_CROWD_PACE_MS |
					 1u << SOMVR_SK_COUNTER_ICON, w7, sizeof w7);
		EXPECT(vm_save_tick(T + 40, 1, 0) == 1 && vm_slurp(path, buf, sizeof buf) && !strcmp(buf, w7) &&
			       !strncmp(buf, v6, sizeof v6 - 1) && somvr_set_diff(&w6, &u) == (1u << SOMVR_SK_WRAITH_STEP |
			       1u << SOMVR_SK_STEALTH_KILL | 1u << SOMVR_SK_CROWD_PACE_MS | 1u << SOMVR_SK_COUNTER_ICON),
		       "saved: the v6 lines unchanged, the four new keys after them:\n%s", buf);
		vm_restart(); /* read back */
		u = vm_cur();
		EXPECT(g_vm.file_mask == (k7 | 1u << SOMVR_SK_WRAITH_STEP | 1u << SOMVR_SK_STEALTH_KILL |
					  1u << SOMVR_SK_CROWD_PACE_MS | 1u << SOMVR_SK_COUNTER_ICON) &&
			       u.crowd_ms == 1000 && !u.wstep && !u.stealth && u.counter_icon && u.turn == 1 && u.bite == 1 &&
			       g_stl.mode == STL_OFF && NEAR(g_ff.pcfg.gap_s, 1.0, 1e-6),
		       "read back: mask %#x, the Fight values held and applied at the start", g_vm.file_mask);
		g_step.cfg = sc0, g_hf.bite = hb0, g_ws.want = ws_want0, g_ws.on = ws_on0, atomic_store(&g_ws.req_onoff, -1);
		g_stl.mode = stl0, g_stl.start_mode = stls0, g_ff.mode = ffm0, g_ff.on_score = ffs0, g_ff.on_grant = ffg0;
		g_ff.pace_on = ffp0, g_ff.pcfg.gap_s = gap0, g_cp.ok_fx = cpf0, atomic_store(&g_cp.mode, cpm0);
		printf("vrmenu19 a v6 settings file read by v7 (7 keys, the same values), the Fight page's rows from their "
		       "switches, live (the pace, the wraith step, the stealth kill, the counter icon), saved after the old "
		       "lines, read back\n");
	}
	/* back to the main run's state: no menu */
	g_vm.on = 0;
	unsetenv("SOMVR_VRMENU"), unsetenv("SOMVR_SETTINGS");
	g_rigcfg = cfg0, g_inp.cfg = ic0, g_inp.hap_scale = hs0, g_inp.hap_on = hap0;
	g_cf.vignette = vig0, g_cf.blink_ms = bl0, g_cf.out_s = out0, g_cf.in_s = in0, g_cf.seat_cfg = seat0;
	g_fb.on = fb0;
	g_layer_live = 0;
	g_cb_test_t = -1;
}

/* ---- SOMVR_LOCO=1 and SOMVR_ANCHOR=root (loco_tests; switched on at start with stand-in sites, then off until
 * loco_tests: white box). The stand-ins: a PlayerMovement::vf_11 (t_pm_loco) that calls the root delta (t_rootdelta, the
 * game's root motion g_lot.rm, the identity while combat owns the movement) through a real E8 call site
 * (LOCO_ROOTDELTA), then the one-shot extra, the locks (skipped while an attack runs) and the transform, as vf_11 does;
 * the attack test behind a real call (LOCO_ATTACK_Q), "combat owns the movement" and the stock lock getters (K_ENTRY;
 * the getters in a PlayerMovement vtable of 40 slots), the other proofs on stand-in code; a bigger engine object with its
 * object timer, an animation manager with the lock bytes, a Player buffer with the FSM, life and mount fields, a wall.
 * Each frame: the input tick, the player update (the PM hook -> t_pm_loco), H4. ----------------------------------------- */
typedef struct { char pad[0x560]; st_obj_t *hchar; char pad2[LO_MGR_LOCK_R - 0x568]; unsigned char lock_r, lock_t; } st_lomgr_t;
_Static_assert(offsetof(st_lomgr_t, hchar) == MGR_CHAR && offsetof(st_lomgr_t, lock_t) == LO_MGR_LOCK_T &&
		       offsetof(st_lomgr_t, lock_r) == LO_MGR_LOCK_R, "stand-in animation manager");
typedef struct { st_obj_t o; char pad[OBJ_TIMER - sizeof(st_obj_t)]; void *timer; } st_loobj_t;
_Static_assert(offsetof(st_loobj_t, timer) == OBJ_TIMER, "stand-in object timer");
typedef struct { uintptr_t vt; char pad[TM_FRAME_S - 8]; float frame_s; } st_lotimer_t;
_Static_assert(offsetof(st_lotimer_t, frame_s) == TM_FRAME_S, "stand-in timer");
static st_lomgr_t g_lomgr;
static st_loobj_t g_loobj;
static st_lotimer_t g_lotimer;
static _Alignas(16) char g_loP[0x800], g_locmb[0x600]; /* the Combat to +0x56f (SOMVR_PHASE's collision byte) */
static void *volatile g_lovt[40];
static char g_lohex[LOS_N][256];
static struct site_spec g_los[LOS_N];
static struct { int calls, rd_calls, combat_owns, attack, extra, wall; float rm[3], rmq[4], wall_z; } g_lot;
__attribute__((noipa)) uint64_t t_rootdelta(void *bodyv, float *d)
{
	(void)bodyv;
	g_lot.rd_calls++;
	if (g_lot.combat_owns) { /* FUN_00a513a0: the identity while combat owns the movement */
		d[0] = d[1] = d[2] = d[3] = d[4] = d[5] = 0, d[6] = 1;
		return (uint64_t)(uintptr_t)d;
	}
	d[0] = g_lot.rm[0], d[1] = g_lot.rm[1], d[2] = g_lot.rm[2];
	d[3] = g_lot.rmq[0], d[4] = g_lot.rmq[1], d[5] = g_lot.rmq[2], d[6] = g_lot.rmq[3];
	return (uint64_t)(uintptr_t)d;
}
__attribute__((noipa)) uint64_t t_pm_loco(void *pmv, float *xf)
{
	st_pm_t *pm = pmv;
	st_body_t *body = ((st_player_t *)pm->owner)->body;
	float d[7] = { 0, 0, 0, 0, 0, 0, 1 };
	t_rootdelta(body, d); /* the call LOCO_ROOTDELTA patches (vf_11's 0xab5ea9) */
	g_lot.calls++;
	sv_vec3 dp = { d[0], d[1], d[2] };
	sv_quat dq = sv_qnormalize((sv_quat){ d[3], d[4], d[5], d[6] });
	if (pm->pending) { /* the one-shot extra rides on the root motion (R(rootmotion.rot) extra), then it is reset */
		sv_vec3 e = sv_qrot(dq, (sv_vec3){ pm->xpos[0], pm->xpos[1], pm->xpos[2] });
		dp.x += e.x, dp.y += e.y, dp.z += e.z;
		memset(pm->xpos, 0, sizeof pm->xpos);
		pm->xrot[0] = pm->xrot[1] = pm->xrot[2] = 0, pm->xrot[3] = 1;
		pm->pending = 0;
		g_lot.extra++;
	}
	const st_lomgr_t *m = (const st_lomgr_t *)body->mgr;
	if (!g_lot.attack) { /* the animation locks, unless an attack runs */
		if (m->lock_t)
			dp = (sv_vec3){ 0, 0, 0 };
		if (m->lock_r)
			dq = (sv_quat){ 0, 0, 0, 1 };
	}
	sv_quat q = sv_qnormalize((sv_quat){ xf[3], xf[4], xf[5], xf[6] });
	sv_vec3 w = sv_qrot(q, dp);
	xf[0] += w.x, xf[1] += w.y, xf[2] += w.z;
	sv_quat_to(sv_qnormalize(sv_qmul(q, dq)), xf + 3);
	return 0;
}
__attribute__((noipa)) uintptr_t st_lo_attack(uintptr_t c) { return c && g_lot.attack ? c : 0; }
__attribute__((noipa)) uintptr_t c_lo_attackq(uintptr_t c) { return st_lo_attack(c) + 1; }
__attribute__((noipa)) uint64_t st_lo_cowns(uintptr_t pc) { return pc && g_lot.combat_owns; }
__attribute__((noipa)) uint64_t st_lo_lockt(void *pm) { return pm ? g_lomgr.lock_t : 0; }
__attribute__((noipa)) uint64_t st_lo_lockr(void *pm) { return pm ? g_lomgr.lock_r : 0; }
__attribute__((noipa)) uint64_t st_lo_code(uint64_t a) { return a * 5 + 3; } /* the proof sites' stand-in code */
static int lo_call(void *caller, void *target, int i, int scan)
{
	const unsigned char *p = caller;
	for (int k = 0; k < scan; k++) {
		int32_t rel;
		memcpy(&rel, p + k + 1, 4);
		if (p[k] == 0xe8 && (uintptr_t)(p + k + 5 + rel) == (uintptr_t)target) {
			hex_of(p + k, 5, g_lohex[i], 128);
			hex_of(target, 4, g_lohex[i] + 128, 128);
			g_los[i] = (struct site_spec){ g_lo_sites_game[i].name, K_CALL, (uintptr_t)(p + k), (uintptr_t)(p + k),
						       g_lohex[i], (uintptr_t)target, g_lohex[i] + 128 };
			return 0;
		}
	}
	return -1;
}
static void lo_entry(void *fn, int i)
{
	hex_of(fn, 8, g_lohex[i], 128);
	hex_of(fn, 4, g_lohex[i] + 128, 128);
	g_los[i] = (struct site_spec){ g_lo_sites_game[i].name, K_ENTRY, (uintptr_t)fn, (uintptr_t)fn, g_lohex[i], (uintptr_t)fn,
				       g_lohex[i] + 128 };
}
static int lo_stand_in_sites(void)
{
	if (lo_call((void *)t_pm_loco, (void *)t_rootdelta, LOS_ROOTDELTA, 256) ||
	    lo_call((void *)c_lo_attackq, (void *)st_lo_attack, LOS_ATTACK_Q, 64))
		return -1;
	lo_entry((void *)st_lo_cowns, LOS_COMBAT_OWNS);
	lo_entry((void *)st_lo_lockt, LOS_LOCK_T);
	lo_entry((void *)st_lo_lockr, LOS_LOCK_R);
	lo_entry((void *)st_lo_code, LOS_FSM);
	lo_entry((void *)st_lo_code, LOS_VELMODE);
	lo_entry((void *)st_lo_code, LOS_MOUNT);
	lo_entry((void *)st_lo_code, LOS_ATTACH);
	lo_entry((void *)st_lo_code, LOS_LIFE);
	lo_entry((void *)st_lo_code, LOS_OBJTIMER);
	return 0;
}
/* SOMVR_STEP / _DASH / _PHASE's read-only sites (somvr_step_game.c): the proofs on stand-in code, the vision's call in a
 * stand-in caller */
static char g_sthex[STS_N][256];
static struct site_spec g_sts[STS_N];
__attribute__((noipa)) uint64_t st_t_vmupd(uintptr_t vm) { return vm ? vm * 7 + 5 : 1; }
__attribute__((noipa)) uint64_t c_st_vmupd(uintptr_t vm) { return st_t_vmupd(vm) + 3; }
static int step_stand_in_sites(void)
{
	for (int i = 0; i < STS_N; i++) {
		if (i != STS_VISION_GET) {
			void *fn = (void *)st_lo_code;
			hex_of(fn, 8, g_sthex[i], 128);
			hex_of(fn, 4, g_sthex[i] + 128, 128);
			g_sts[i] = (struct site_spec){ g_step_sites_game[i].name, K_ENTRY, (uintptr_t)fn, (uintptr_t)fn, g_sthex[i],
						       (uintptr_t)fn, g_sthex[i] + 128 };
			continue;
		}
		const unsigned char *p = (const unsigned char *)c_st_vmupd;
		int k = 0;
		for (; k < 64; k++) {
			int32_t rel;
			memcpy(&rel, p + k + 1, 4);
			if (p[k] == 0xe8 && (uintptr_t)(p + k + 5 + rel) == (uintptr_t)st_t_vmupd)
				break;
		}
		if (k == 64)
			return -1;
		hex_of(p + k, 5, g_sthex[i], 128);
		hex_of((const void *)st_t_vmupd, 4, g_sthex[i] + 128, 128);
		g_sts[i] = (struct site_spec){ g_step_sites_game[i].name, K_CALL, (uintptr_t)(p + k), (uintptr_t)(p + k), g_sthex[i],
					       (uintptr_t)st_t_vmupd, g_sthex[i] + 128 };
	}
	return 0;
}
/* the player update: vf_11 through the PM hook's slot (-> t_pm_loco, LOCO_ROOTDELTA inside), then a wall at z */
static void lo_update(void)
{
	float xf[7];
	memcpy(xf, g_loobj.o.pos, sizeof g_loobj.o.pos), memcpy(xf + 3, g_loobj.o.rot, sizeof g_loobj.o.rot);
	((t_pm_fn)g_vtpm[11])(&g_ppm, xf);
	if (g_lot.wall && xf[2] > g_lot.wall_z)
		xf[2] = g_lot.wall_z;
	memcpy(g_loobj.o.pos, xf, sizeof g_loobj.o.pos), memcpy(g_loobj.o.rot, xf + 3, sizeof g_loobj.o.rot);
}
/* one game frame: the layer's wait and sync, the input tick, the player update, H4 and Present */
static void lo_frame(void)
{
	g_cb_test_t += 1.0 / 90;
	g_xrt += 11111111;
	somvr_time_publish(&g_sh, g_xrt, 11111111);
	if (g_layer_live) {
		g_lst.sync_seq++, g_lst.time = g_xrt;
		for (int h = 0; h < 2; h++)
			memcpy(&g_lst.hand[h].grip, &g_lpose[h], sizeof g_lst.hand[h].grip);
		somvr_input_publish(&g_in, &g_lst);
	}
	((t_vf04_fn)g_vtin[4])(&g_cwbi);
	lo_update();
	g_pu_done = 1;
	frame();
}
static void lo_frames(int n)
{
	for (int i = 0; i < n; i++)
		lo_frame();
}
static sv_vec3 lo_root(void) { return (sv_vec3){ g_loobj.o.pos[0], g_loobj.o.pos[1], g_loobj.o.pos[2] }; }
static float lo_yaw(void) { return sv_heading(sv_qnormalize(sv_quat_of(g_loobj.o.rot))) * 57.29578f; }

/* the player size on a camera-base frame: the size is 1 there (the frame's base is not the head node) */
static void sz_rig_check_camera(void)
{
	g_fr.head_ok = 0;
	sz_frame();
	EXPECT(g_sz.eff == 1.0f && g_sz.lift == 0, "size: a camera frame is 1:1 (%g, %g)", g_sz.eff, g_sz.lift);
}

static void loco_tests(void)
{
	EXPECT(log_has("CONFIG loco=1 (the 1:1 core, LOCO_ROOTDELTA): the stick's bands walk 150 / run 500 / the latch 800") &&
		       log_has("CONFIG anchor=root (LO-1): the eyes 76.6 cm above the root") && log_has("HOOK LOCO_ROOTDELTA ok call") &&
		       log_has("LOSITE LOCO_ROOTDELTA ok") && log_has("LOSITE LOCO_OBJTIMER ok"),
	       "loco: no CONFIG/HOOK/LOSITE lines");
	float pos0[3], rot0[4];
	memcpy(pos0, g_pobj.pos, sizeof pos0), memcpy(rot0, g_pobj.rot, sizeof rot0);
	somvr_rig rig0 = g_rig;
	somvr_rig_cfg cfg0 = g_rigcfg;
	const sa_node *node0 = g_nodexf_node;
	st_obj_t *bobj0 = g_pbody.obj;
	st_mgr_t *bmgr0 = g_pbody.mgr;
	uintptr_t vtpm0 = g_som.vt_pm, pmorig0 = g_h[HK_PM].orig;
	int arm0 = g_arm.on;
	g_views_fake = 0, g_rt_result = XR_SUCCESS, g_layout = g_rigcfg.layout = SOMVR_LAYOUT_A;
	g_rigcfg.base = SOMVR_BASE_HEAD, g_rigcfg.yaw = SOMVR_YAW_CAMERA, g_rigcfg.eye_cm = 0;
	g_follow.on = 0, g_fb.on = 0, g_cb.mode = CB_OFF, g_cl.on = 0, g_bow.on = 0, g_arm.on = 0;
	g_hide.on = 0, g_hudq.on = 0, g_nodexf_node = NULL, g_head_up = 72.5f, g_cf.out_s = g_cf.in_s = 0;
	somvr_rig_init(&g_rig);
	g_rt_head[0] = g_rt_head[1] = g_rt_head[2] = 0, g_rt_q = (sv_quat){ 0, 0, 0, 1 };
	/* Talion: a bigger object with its timer (1/90 s a tick), the manager with the locks, the Player buffer */
	memset(&g_loobj, 0, sizeof g_loobj);
	g_loobj.o = g_pobj;
	g_loobj.o.pos[0] = 500, g_loobj.o.pos[1] = 100, g_loobj.o.pos[2] = -50;
	g_loobj.o.rot[0] = g_loobj.o.rot[1] = g_loobj.o.rot[2] = 0, g_loobj.o.rot[3] = 1;
	g_lotimer.vt = g_cbg.vt_etimer, g_lotimer.frame_s = 1 / 90.0f;
	g_loobj.timer = &g_lotimer;
	memset(&g_lomgr, 0, sizeof g_lomgr);
	g_lomgr.hchar = &g_loobj.o;
	g_pbody.obj = &g_loobj.o, g_pbody.mgr = (st_mgr_t *)&g_lomgr;
	memset(g_loP, 0, sizeof g_loP);
	memcpy(g_loP, &g_player, sizeof g_player);
	g_loP[LO_P_FSM] = 6, g_loP[LO_P_LIFE] = 2;
	g_pl = (st_player_t *)g_loP, g_ppm.owner = g_loP;
	for (int i = 0; i < 40; i++)
		g_lovt[i] = i < 16 ? g_vtpm[i] : NULL;
	g_lovt[LO_VT_LOCK_T] = (void *)st_lo_lockt, g_lovt[LO_VT_LOCK_R] = (void *)st_lo_lockr;
	g_ppm.vt = g_som.vt_pm = (uintptr_t)g_lovt;
	((char *)&g_ppm)[LO_PM_VELMODE] = 1, ((char *)&g_ppm)[LO_PM_ATTACHED] = 0;
	uintptr_t cmb = (uintptr_t)g_locmb;
	memcpy((char *)&g_ppm + LO_PM_ATTACK, &cmb, 8);
	g_h[HK_PM].orig = (uintptr_t)t_pm_loco; /* white box: the stand-in vf_11 with the root-delta call */
	memset(&g_lot, 0, sizeof g_lot);
	g_lot.rmq[3] = 1;
	g_cb_test_t = 20000;
	g_xrt = g_vst.last_time + 1000000000;
	runtime_up();
	g_inp.on = 1, g_inp.lines = 0, g_inp.lsec = 0;
	layer_up();
	for (int h = 0; h < 2; h++)
		g_lst.hand[h].squeeze = 0, g_lst.hand[h].stick[0] = g_lst.hand[h].stick[1] = 0;
	g_lo.on = 1, g_lo.anchor = 1, g_lo.lines = 0, g_lo.next_log = 0, g_lo.t_pm = 0, g_lo.owned_last = 0;
	somvr_loco_init(&g_lo.s);
	somvr_anchor_init(&g_lo.a);
	memset(&g_lo.n, 0, sizeof g_lo.n), memset(&g_lo.w, 0, sizeof g_lo.w);
	atomic_fetch_add(&g_req_recenter, 1);
	lo_frames(4); /* the recentre; the head base (the anchor) settles */
	sv_vec3 r = lo_root(), e = eye_mid();
	EXPECT(g_fr.head_ok && g_lo.n.ours >= 2 && NEAR3(e, r.x, r.y + 76.6, r.z, 0.01) && g_lot.rd_calls >= 4,
	       "loco: before: head %d ours %llu eyes %g %g %g root %g %g %g", g_fr.head_ok, (unsigned long long)g_lo.n.ours, e.x, e.y,
	       e.z, r.x, r.y, r.z);
	/* 1. standing still: the game's idle root motion (a sway) never reaches Talion on our ticks */
	g_lot.rm[0] = 0.3f, g_lot.rm[2] = 0.2f;
	sv_vec3 r0 = lo_root();
	lo_frames(10);
	EXPECT(NEAR3(lo_root(), r0.x, r0.y, r0.z, 1e-4) && g_lo.n.hook_ours >= 10, "loco: standing: moved %g,%g",
	       lo_root().x - r0.x, lo_root().z - r0.z);
	/* 2. the stick pushed: 500 cm/s from the very first tick (Talion's timer, 1/90 s), not the game's start clip */
	g_lot.rm[0] = 0, g_lot.rm[2] = 1.0f; /* the stock start clip's slow first steps */
	g_lst.hand[0].stick[1] = 1;
	r0 = lo_root();
	lo_frame();
	EXPECT(NEAR(lo_root().z - r0.z, 500 / 90.0, 1e-3) && NEAR(lo_root().x, r0.x, 1e-4),
	       "loco: the first tick moved %g (want %g)", lo_root().z - r0.z, 500 / 90.0);
	lo_frames(44);
	EXPECT(NEAR(lo_root().z - r0.z, 250, 0.05) && NEAR(lo_root().y, r0.y, 1e-4), "loco: 45 ticks = 250 cm: %g",
	       lo_root().z - r0.z);
	EXPECT(poll22(SC_CMD_FORWARDAXIS) == 1.0f, "loco: the legs: ForwardAxis %g", poll22(SC_CMD_FORWARDAXIS));
	EXPECT(g_lo.n.timer > 40 && fabsf(lo_yaw()) < 1e-3, "loco: dt from the timer (%llu), the yaw kept %g",
	       (unsigned long long)g_lo.n.timer, lo_yaw());
	/* the anchor while walking: the eyes exactly over the root (no lag, no bob from the head node's 3 cm sway) */
	float worst = 0;
	for (int i = 0; i < 20; i++) {
		g_head_up = 72.5f + 3 * sinf(i * 1.3f); /* the walk bob of the head node */
		lo_frame();
		e = eye_mid(), r = lo_root();
		worst = fmaxf(worst, fmaxf(fabsf(e.z - r.z), fabsf(e.y - (r.y + 76.6f))));
	}
	g_head_up = 72.5f;
	EXPECT(worst < 0.01f, "loco: the anchor while walking: %g cm off the root's eye point", worst);
	/* 3. released: stopped in the same tick, no slide */
	g_lst.hand[0].stick[1] = 0;
	lo_frame();
	r0 = lo_root();
	lo_frames(10);
	EXPECT(NEAR3(lo_root(), r0.x, r0.y, r0.z, 1e-4), "loco: no stop slide (%g cm)", lo_root().z - r0.z);
	e = eye_mid();
	EXPECT(NEAR(e.z, r0.z, 0.01), "loco: the view stopped with him");
	/* 4. the stick right: he walks +x at once and turns to face it at 720 deg/s (8 deg a tick) */
	g_lst.hand[0].stick[0] = 1;
	r0 = lo_root();
	lo_frame();
	EXPECT(NEAR(lo_root().x - r0.x, 500 / 90.0, 1e-3) && NEAR(lo_yaw(), 8, 0.01), "loco: right: %g cm, yaw %g",
	       lo_root().x - r0.x, lo_yaw());
	lo_frames(12);
	EXPECT(NEAR(lo_yaw(), 90, 0.01) && NEAR(lo_root().x - r0.x, 13 * 500 / 90.0, 0.01) && NEAR(lo_root().z, r0.z, 1e-3),
	       "loco: faced the move: yaw %g moved %g,%g", lo_yaw(), lo_root().x - r0.x, lo_root().z - r0.z);
	/* the rotation lock: the yaw stays the game's */
	g_lomgr.lock_r = 1;
	g_lst.hand[0].stick[0] = 0, g_lst.hand[0].stick[1] = 1;
	lo_frames(5);
	EXPECT(NEAR(lo_yaw(), 90, 0.01) && g_lo.o.owned && !g_lo.o.yaw_ours, "loco: the rotation lock: yaw %g", lo_yaw());
	g_lomgr.lock_r = 0;
	lo_frames(15);
	EXPECT(NEAR(lo_yaw(), 0, 0.01), "loco: facing forward again: %g", lo_yaw());
	/* 5. who owns: the translation lock (the game zeroes even ours), an attack (the game's root motion carries him),
	 * FSM 8, combat owning the movement (the identity), a climb (the climb's), then ours again */
	g_lomgr.lock_t = 1;
	r0 = lo_root();
	lo_frames(3);
	EXPECT(!g_lo.o.owned && (g_lo.o.why & SOMVR_LOCO_W_LOCK) && NEAR3(lo_root(), r0.x, r0.y, r0.z, 1e-4) &&
		       log_has("owner ours -> lock (the game's)"),
	       "loco: lock: owned %d why %#x", g_lo.o.owned, g_lo.o.why);
	g_lomgr.lock_t = 0;
	g_lot.attack = 1, g_lot.rm[2] = 3;
	r0 = lo_root();
	lo_frames(4);
	EXPECT(!g_lo.o.owned && (g_lo.o.why & SOMVR_LOCO_W_ATTACK) && NEAR(lo_root().z - r0.z, 12, 1e-3) &&
		       log_has("owner lock -> attack"),
	       "loco: attack: the game's 3 cm a tick: %g", lo_root().z - r0.z);
	/* ... SOMVR_LOCO_HOLD=attack (run 13: an orc's hits carried Talion 27 m): the attack's ticks ours, the stick at rest:
	 * Talion stays where he is */
	g_lo.hold = SOMVR_LOCO_W_ATTACK;
	float stk0[2] = { g_lst.hand[0].stick[0], g_lst.hand[0].stick[1] };
	g_lst.hand[0].stick[0] = g_lst.hand[0].stick[1] = 0;
	lo_frames(3);
	uint64_t held0 = g_lo.n_held;
	r0 = lo_root();
	lo_frames(4);
	EXPECT(g_lo.o.owned && !(g_lo.o.why & SOMVR_LOCO_W_ATTACK) && NEAR3(lo_root(), r0.x, r0.y, r0.z, 1e-3) &&
		       g_lo.n_held >= held0 + 4,
	       "loco: the hold: owned %d why %#x moved %g cm held %llu", g_lo.o.owned, g_lo.o.why, lo_root().z - r0.z,
	       (unsigned long long)(g_lo.n_held - held0));
	g_lo.hold = 0;
	g_lst.hand[0].stick[0] = stk0[0], g_lst.hand[0].stick[1] = stk0[1];
	lo_frames(2);
	g_lot.attack = 0;
	g_loP[LO_P_FSM] = LO_FSM_FALLING;
	r0 = lo_root();
	lo_frames(2);
	EXPECT(!g_lo.o.owned && (g_lo.o.why & SOMVR_LOCO_W_FSM) && NEAR(lo_root().z - r0.z, 6, 1e-3), "loco: FSM 8: %g",
	       lo_root().z - r0.z);
	g_loP[LO_P_FSM] = 6;
	g_lot.combat_owns = 1;
	r0 = lo_root();
	lo_frames(2);
	EXPECT(!g_lo.o.owned && (g_lo.o.why & SOMVR_LOCO_W_COMBATMOVE) && NEAR3(lo_root(), r0.x, r0.y, r0.z, 1e-4),
	       "loco: combat move: why %#x", g_lo.o.why);
	g_lot.combat_owns = 0;
	g_cl.on = 1, g_cl.s.active = 1, g_cl.tid = 1, g_cl.act_pm = 0; /* white box: a climb holds (its own thread id: inert) */
	uint64_t eases0 = g_lo.a_blend;
	r0 = lo_root();
	lo_frames(2);
	EXPECT(!g_lo.o.owned && (g_lo.o.why & SOMVR_LOCO_W_CLIMB) && NEAR(lo_root().z - r0.z, 6, 1e-3) && g_lo.ao.exact_y,
	       "loco: the climb owns: why %#x moved %g, the anchor's y %s", g_lo.o.why, lo_root().z - r0.z,
	       g_lo.ao.exact_y ? "exact" : "filtered");
	g_cl.on = 0, g_cl.s.active = 0, g_cl.tid = 0;
	r0 = lo_root();
	lo_frame();
	EXPECT(g_lo.o.owned && g_lo.o.handoff == 1 && NEAR(lo_root().z - r0.z, 500 / 90.0, 1e-3) && log_has("-> ours (ours)"),
	       "loco: ours again: %g", lo_root().z - r0.z);
	EXPECT(g_lo.a_blend >= 1 && g_lo.a_blend == eases0, "loco: the anchor eased the hand-off to the lock (%llu), not the "
	       "climb's (%llu)", (unsigned long long)g_lo.a_blend, (unsigned long long)(g_lo.a_blend - eases0));
	lo_frames(60);
	e = eye_mid(), r = lo_root();
	EXPECT(NEAR(e.z, r.z, 0.01), "loco: the hand-off ease decayed: %g", e.z - r.z);
	/* the leg feed with SOMVR_LOCO_SPEED 0.8: the full stick's 400 cm/s plays the band of 0.857 */
	g_lo.cfg.speed = 0.8f;
	r0 = lo_root();
	lo_frames(3);
	EXPECT(NEAR(poll22(SC_CMD_FORWARDAXIS), 0.5 + 0.5 * 250 / 350.0, 1e-4) && NEAR(lo_root().z - r0.z, 3 * 400 / 90.0, 1e-3),
	       "loco: speed 0.8: legs %g, moved %g", poll22(SC_CMD_FORWARDAXIS), lo_root().z - r0.z);
	g_lo.cfg.speed = 1;
	/* 6. a wall: the collision takes the part into it; the next tick asks the stick's speed again */
	g_lot.wall = 1, g_lot.wall_z = lo_root().z + 2;
	lo_frames(4);
	EXPECT(NEAR(lo_root().z, g_lot.wall_z, 1e-4) && NEAR(g_lo.o.v.z, 500, 1e-3) && g_lo.o.have_applied &&
		       NEAR(g_lo.o.applied.z, 0, 1e-4),
	       "loco: the wall: z %g v %g applied %g", lo_root().z, g_lo.o.v.z, g_lo.o.applied.z);
	g_lot.wall = 0;
	g_lst.hand[0].stick[1] = 0;
	lo_frames(2);
	/* 7. the follow in the same delta: the head 20 cm right in the room -> Talion follows on our tick, no extra move,
	 * the view stays; with the stick at the same time both, once; on the game's ticks the follow's own extra move */
	g_follow.on = 1, g_follow.next_log = 0;
	uint64_t fm0 = g_follow.moves, fo0 = g_lo.n.follow;
	int ex0 = g_lot.extra;
	lo_frames(2);
	sv_vec3 v0 = eye_mid();
	r0 = lo_root();
	g_rt_head[0] = 0.2f;
	lo_frames(6);
	sv_vec3 v1 = eye_mid();
	EXPECT(g_follow.moves > fm0 && g_lo.n.follow > fo0 && g_lot.extra == ex0 && NEAR(lo_root().x - r0.x, 20, 0.05) &&
		       NEAR(v1.x - v0.x, 20, 0.05) && NEAR(v1.z, v0.z, 0.05),
	       "loco: the follow in our delta: moves %llu merged %llu extras %d, root +%g, the view +%g (the head's own 20)",
	       (unsigned long long)(g_follow.moves - fm0), (unsigned long long)(g_lo.n.follow - fo0), g_lot.extra - ex0,
	       lo_root().x - r0.x, v1.x - v0.x);
	g_rt_head[0] = 0.4f; /* 20 more, and the stick forward meanwhile */
	g_lst.hand[0].stick[1] = 1;
	r0 = lo_root();
	lo_frames(9);
	g_lst.hand[0].stick[1] = 0;
	lo_frames(3);
	EXPECT(NEAR(lo_root().x - r0.x, 20, 0.05) && NEAR(lo_root().z - r0.z, 9 * 500 / 90.0, 0.05) && g_lot.extra == ex0,
	       "loco: stick + follow, once: %g,%g (want 20, %g), extras %d", lo_root().x - r0.x, lo_root().z - r0.z,
	       9 * 500 / 90.0, g_lot.extra - ex0);
	g_lot.attack = 1, g_lot.rm[2] = 0; /* the game's ticks: the follow's own extra move */
	g_rt_head[0] = 0.55f;
	r0 = lo_root();
	lo_frames(6);
	EXPECT(g_lot.extra > ex0 && NEAR(lo_root().x - r0.x, 15, 0.05) && g_lo.n.extra > 0,
	       "loco: the follow's extra on the game's ticks: extras %d, +%g", g_lot.extra - ex0, lo_root().x - r0.x);
	g_lot.attack = 0;
	g_follow.on = 0;
	lo_frames(3);
	/* 8. the anchor: a 30 cm step up (the proxy's step) rises over frames, no step in the view */
	r0 = lo_root();
	float y0 = eye_mid().y;
	uint64_t ep8 = g_rig_epoch;
	g_loobj.o.pos[1] += 30;
	lo_frame();
	float y1 = eye_mid().y;
	lo_frames(40);
	EXPECT(y1 - y0 > 0 && y1 - y0 < 6 && NEAR(eye_mid().y - y0, 30, 0.3) && g_rig_epoch == ep8,
	       "loco: the step: the first frame %g, then %g (rig jumps %llu)", y1 - y0, eye_mid().y - y0,
	       (unsigned long long)(g_rig_epoch - ep8));
	/* a teleport: re-latched. The head base takes a head node 1 m away as a camera-base frame (a jump), the next frame's
	 * re-latch is a jump of the rig too (re-xr-pipeline.md 19.2: a re-latch drops the hand-off offset and the Y lag at
	 * once; the bone blade, the follow and the vignette never measure across it) */
	uint64_t tp0 = g_lo.a_tele;
	float dx0 = eye_mid().x - lo_root().x;
	g_loobj.o.pos[0] += 400;
	lo_frame();
	int bj1 = g_fr.base_jump;
	lo_frame();
	int rl2 = g_fr.relatched, bj2 = g_fr.base_jump;
	lo_frame();
	EXPECT(g_lo.a_tele == tp0 + 1 && NEAR(eye_mid().x - lo_root().x, dx0, 0.05) && log_has("TELEPORT (re-latched)") &&
		       bj1 && rl2 && bj2 && !g_fr.base_jump && g_rig_epoch == ep8 + 2,
	       "loco: teleport: %llu, the view %g from the root (was %g), jumps %d %d (the re-latch's %d) then %d, epoch +%llu",
	       (unsigned long long)(g_lo.a_tele - tp0), eye_mid().x - lo_root().x, dx0, bj1, bj2, rl2, g_fr.base_jump,
	       (unsigned long long)(g_rig_epoch - ep8));
	/* 10. the player size (SOMVR_PLAYER_SIZE, somvr_size_game.c): `size 130` fades out, lands on the black frame (one
	 * jump of the rig), the eyes 0.3 x 180.6 cm higher and 1.3 x as far apart, then fades in; the menu's 100 back */
	lo_frames(3);
	float sep0 = dist3(g_h4_view[0].pos, g_h4_view[1].pos), lift0 = eye_mid().y - lo_root().y;
	uint64_t ep10 = g_rig_epoch, lands0 = g_sz.z.lands;
	EXPECT(g_sz.on && NEAR(g_sz.eye_h, 180.6, 0.01) && g_sz.eff == 1.0f && NEAR(lift0, 76.6, 0.05),
	       "size: on %d, the eye height %g, now %g, the eyes %g over the root", g_sz.on, g_sz.eye_h, g_sz.eff, lift0);
	EXPECT(!strcmp(sz_word("130"), "size") && !strcmp(sz_word("huge"), "size (not PCT|probe|status: ignored)"),
	       "size: the words");
	int land_at = -1, fade_max = 0, fade_land = 0, n10 = 0;
	for (; n10 < 60; n10++) {
		lo_frame();
		int fd = (int)somvr_rec_fade(g_fr.rec->flags);
		fade_max = fd > fade_max ? fd : fade_max;
		if (g_sz.z.landed)
			land_at = n10, fade_land = fd;
		if (land_at < 0)
			EXPECT(NEAR(eye_mid().y - lo_root().y, 76.6, 0.05), "size: frame %d before the black: the old height", n10);
	}
	float sep1 = dist3(g_h4_view[0].pos, g_h4_view[1].pos), lift1 = eye_mid().y - lo_root().y;
	EXPECT(land_at >= 8 && land_at <= 12 && fade_land == 255 && fade_max == 255 && g_sz.z.lands == lands0 + 1 &&
		       g_rig_epoch == ep10 + 1 && NEAR(g_sz.eff, 1.3, 1e-6) && NEAR(lift1, 76.6 + 0.3 * 180.6, 0.05) &&
		       NEAR(sep1, 1.3 * sep0, 0.01) && (int)somvr_rec_fade(g_fr.rec->flags) == 0 && NEAR(g_inp.rigxf.s, 1.3, 1e-6) &&
		       log_has("SIZE land (from 100 %"),
	       "size: landed at frame %d (fade %d, max %d), jumps %llu, the eyes %g over the root, %g apart (were %g), the "
	       "blade's rig %g", land_at, fade_land, fade_max, (unsigned long long)(g_rig_epoch - ep10), lift1, sep1, sep0,
	       g_inp.rigxf.s);
	somvr_settings ss = g_vm.applied;
	ss.size = 1.0f;
	vm_apply_key(SOMVR_SK_PLAYER_SIZE, &ss, "the selftest");
	lo_frames(40);
	EXPECT(g_sz.eff == 1.0f && NEAR(eye_mid().y - lo_root().y, 76.6, 0.05) && g_sz.z.lands == lands0 + 2 &&
		       g_rig_epoch == ep10 + 2 && log_has("VRMENU apply PLAYER_SIZE=100 from the selftest (lands on the blink's black)"),
	       "size: the menu's 100 %%: %g, the eyes %g over the root", g_sz.eff, eye_mid().y - lo_root().y);
	/* SOMVR_SIZE_BODY: Talion's object scale (obj+0x24) follows the size while only his hands are drawn, the arms and
	 * the blade read it; at 100 % his own again; a scale the game set itself is left alone */
	int bd0 = g_bd.on;
	float one = 1.0f, sc = 0;
	memcpy((char *)&g_loobj.o + 0x24, &one, 4);
	g_szb.on = 1, g_bd.on = 1;
	sz_word("115");
	lo_frames(40);
	memcpy(&sc, (char *)&g_loobj.o + 0x24, 4);
	EXPECT(NEAR(sc, 1.15, 1e-6) && NEAR(sz_body_scale(), 1.15, 1e-6) && log_has("SIZE body 1.15: Talion's object scale 1.000 -> 1.150"),
	       "size body: his scale %g, the arms' %g", sc, sz_body_scale());
	sz_word("100");
	lo_frames(40);
	memcpy(&sc, (char *)&g_loobj.o + 0x24, 4);
	EXPECT(sc == 1.0f && sz_body_scale() == 1.0f, "size body: back to his own (%g)", sc);
	float odd = 0.9f;
	memcpy((char *)&g_loobj.o + 0x24, &odd, 4);
	sz_word("115");
	lo_frames(40);
	memcpy(&sc, (char *)&g_loobj.o + 0x24, 4);
	EXPECT(sc == 0.9f && sz_body_scale() == 1.0f && g_szb.foreign > 0 && log_has("left alone"),
	       "size body: the game's own 0.9 left alone (%g)", sc);
	memcpy((char *)&g_loobj.o + 0x24, &one, 4);
	sz_word("100");
	lo_frames(40);
	g_szb.on = 0, g_bd.on = bd0;
	/* the camera base (third person, a camera frame): 1:1 whatever the size */
	g_sz.z.cur = g_sz.z.want = 1.3f;
	sz_rig_check_camera();
	g_sz.z.cur = g_sz.z.want = 1.0f;
	lo_frames(2);
	/* 9. off: the game's root motion again, the call passes */
	g_lo.on = 0;
	g_lot.rm[2] = 2;
	uint64_t pass0 = g_lo.n.hook_pass;
	r0 = lo_root();
	lo_frames(3);
	EXPECT(NEAR(lo_root().z - r0.z, 6, 1e-3) && g_lo.n.hook_pass == pass0 + 3, "loco off: the game's: %g (passes %llu)",
	       lo_root().z - r0.z, (unsigned long long)(g_lo.n.hook_pass - pass0));
	/* SOMVR_COMBAT_HOLD_TALION (build 17): an enemy's sync on Talion (his Combat +0x190 set, +0x188's record empty),
	 * combat owning his movement and moving him between the ticks (the berserker's headbutt: 108 cm between the ticks in
	 * the user's build 16 session): the ticks ours, the move pulled back, the view on the held point; the hold ends with
	 * his half; his own sync (+0x188's record) and the switch off leave the move to the game */
	{
		static unsigned char tsc[0x500], tact[2][0x40], trec[0x40], tatk[0x40];
		st_combat_t *sc0 = g_pcomb.server;
		st_player_t *tlp = (st_player_t *)g_loP;
		__typeof__(tlp->pcomb) tpc0 = tlp->pcomb;
		tlp->pcomb = &g_pcomb;
		int tlon0 = g_lo.on;
		float trm0 = g_lot.rm[2];
		g_lo.on = 1, g_lot.rm[2] = 0; /* (after the off step: LOCO on again, no root motion of its own) */
		float stk[2] = { g_lst.hand[0].stick[0], g_lst.hand[0].stick[1] };
		g_lst.hand[0].stick[0] = g_lst.hand[0].stick[1] = 0;
		uintptr_t vt = (uintptr_t)st_vt_combat, a0 = (uintptr_t)tact[0], a1 = (uintptr_t)tact[1], rec = (uintptr_t)trec,
			  atk = (uintptr_t)tatk;
		memcpy(tsc, &vt, 8), memcpy(tsc + C_SYNC + 8, &a1, 8), memcpy(tsc + VB_C_ATTACK, &atk, 8);
		memcpy(tact[0] + FB_SYNC_REC, &rec, 8), memcpy(tact[1] + FB_SYNC_REC, &rec, 8);
		g_pcomb.server = (st_combat_t *)tsc;
		g_tk.on = 1, g_lot.combat_owns = 1;
		lo_frame();
		sv_vec3 h0 = lo_root(), e0 = eye_mid();
		float t_rw = 0, t_ew = 0;
		for (int i = 0; i < 40; i++) {
			g_loobj.o.pos[2] += 1.5f; /* the game's move between the ticks */
			g_loobj.o.pos[1] += i == 20 ? 5.0f : i == 22 ? -5.0f : 0; /* lifted 5 cm for two ticks (run 121811: 12.7) */
			lo_frame();
			sv_vec3 rr = lo_root(), ee = eye_mid();
			t_rw = fmaxf(t_rw, hypotf(rr.x - h0.x, rr.z - h0.z));
			t_ew = fmaxf(t_ew, sqrtf((ee.x - e0.x) * (ee.x - e0.x) + (ee.y - e0.y) * (ee.y - e0.y) + (ee.z - e0.z) * (ee.z - e0.z)));
		}
		EXPECT(g_tk.active && g_lo.o.owned && t_rw < 0.01f && t_ew < 0.01f && g_tk.pulls >= 39 &&
			       NEAR(g_tk.dev_max, 1.5, 0.01) && log_has("HOLD talion start"),
		       "hold talion: owned %d active %d, the root off %g cm, the eyes %g, pulls %llu, dev %g", g_lo.o.owned,
		       g_tk.active, t_rw, t_ew, (unsigned long long)g_tk.pulls, g_tk.dev_max);
		/* the sync and his half over with a last move of 70 cm: the body back under the view (60 cm a tick at most)
		 * before the view takes the root again */
		memset(tsc + C_SYNC, 0, 16), memset(tsc + VB_C_ATTACK, 0, 8);
		g_loobj.o.pos[2] += 70;
		lo_frame();
		sv_vec3 e1 = eye_mid();
		int ret_active = g_tk.active;
		lo_frames(2);
		sv_vec3 e2 = eye_mid();
		EXPECT(ret_active && !g_tk.active && log_has("HOLD talion end") && log_has("(the sync and his half ended)") &&
			       log_has("42 pulls back (130.0 cm)") && log_has("back under the view in 2 ticks") &&
			       hypotf(e1.x - e0.x, e1.z - e0.z) < 0.01f && hypotf(e2.x - e0.x, e2.z - e0.z) < 0.01f &&
			       hypotf(lo_root().x - h0.x, lo_root().z - h0.z) < 0.01f,
		       "hold talion: the end (active %d -> %d), the eyes %g / %g cm off, the root %g", ret_active, g_tk.active,
		       hypotf(e1.x - e0.x, e1.z - e0.z), hypotf(e2.x - e0.x, e2.z - e0.z), hypotf(lo_root().x - h0.x, lo_root().z - h0.z));
		memcpy(tsc + C_SYNC, &a0, 8), memcpy(tsc + C_SYNC + 8, &a1, 8), memcpy(tsc + VB_C_ATTACK, &atk, 8);
		sv_vec3 r1 = lo_root();
		g_loobj.o.pos[2] += 1.5f;
		lo_frames(2);
		EXPECT(!g_tk.active && !g_lo.o.owned && NEAR(lo_root().z - r1.z, 1.5, 1e-3), "hold talion: his own sync held "
		       "(active %d owned %d moved %g)", g_tk.active, g_lo.o.owned, lo_root().z - r1.z);
		memset(tsc + C_SYNC, 0, 8), g_tk.on = 0;
		r1 = lo_root();
		g_loobj.o.pos[2] += 1.5f;
		lo_frames(2);
		EXPECT(!g_tk.active && !g_lo.o.owned && NEAR(lo_root().z - r1.z, 1.5, 1e-3), "hold talion: off held (active %d "
		       "owned %d moved %g)", g_tk.active, g_lo.o.owned, lo_root().z - r1.z);
		g_pcomb.server = sc0, g_lot.combat_owns = 0, tlp->pcomb = tpc0;
		g_lo.on = tlon0, g_lot.rm[2] = trm0;
		g_lst.hand[0].stick[0] = stk[0], g_lst.hand[0].stick[1] = stk[1];
		lo_frames(2);
		printf("hold   SOMVR_COMBAT_HOLD_TALION: an enemy's sync moving Talion 1.5 cm between each of 40 ticks (and 5 cm up for two): the "
		       "root and the eyes kept (off by %g / %g cm), the hold's end with his half, a last 70 cm move back under the still view "
		       "in 2 ticks; his own sync and the switch off: the game's\n", t_rw, t_ew);
	}
	g_lo.anchor = 0;
	lo_frames(2);
	e = eye_mid(), r = lo_root();
	EXPECT(NEAR(e.y, r.y + 72.5, 0.05), "anchor off: the head node again (%g above the root)", e.y - r.y);
	g_lo.on = 1, g_lo.anchor = 1;
	lo_end_log();
	EXPECT(log_has("LOCO END ticks=") && log_has("ANCHOR END frames=") && log_has("LOCO t=") && log_has("ANCHOR frame="),
	       "loco: no END/LOCO/ANCHOR lines");
	printf("loco   1:1 on stand-ins: standing (the game's sway dropped), the run speed from the first tick (Talion's timer), "
	       "the stop in one tick, the body yaw at 720 deg/s (the rotation lock: the game's), the owners (lock, attack, FSM 8, "
	       "combat move, a climb) and back with an eased hand-off, the legs at speed 0.8, a wall, the follow in the same delta "
	       "(and with the stick, once; the extra move on the game's ticks); the anchor: the eyes 76.6 cm over the root while "
	       "walking (the head bob dropped), a step eased, a teleport re-latched; off = the game's; %llu ticks\n",
	       (unsigned long long)g_lo.n.ticks);
	g_lo.on = 0, g_lo.anchor = 0;
	g_lst.hand[0].stick[0] = g_lst.hand[0].stick[1] = 0;
	g_rt_head[0] = 0;
	g_h[HK_PM].orig = pmorig0, g_som.vt_pm = vtpm0, g_ppm.vt = (uintptr_t)g_vtpm;
	g_pbody.obj = bobj0, g_pbody.mgr = bmgr0;
	g_pl = &g_player, g_ppm.owner = &g_player;
	g_rig = rig0, g_rigcfg = cfg0, g_nodexf_node = node0;
	memcpy(g_pobj.pos, pos0, sizeof pos0), memcpy(g_pobj.rot, rot0, sizeof rot0);
	g_arm.on = arm0;
	g_layer_live = 0;
	g_cb_test_t = -1;
}

/* ---- SOMVR_STEP / SOMVR_DASH / SOMVR_PHASE on the LOCO stand-ins (somvr_step_game.c, re-xr-pipeline.md 23) ------- */
static void step_frames(int n, const struct cb_orc *co, int nco)
{
	for (int i = 0; i < n; i++) {
		if (co) { /* the combat frame's orcs (white box: its own enumeration needs the bands) */
			sv_vec3 r = lo_root();
			float rt[3] = { r.x, r.y, r.z };
			st_combat(co, nco, NULL, 0, rt, cb_now());
		}
		lo_frame();
	}
}
static void step_tests(void)
{
	EXPECT(log_has("CONFIG step=1.00 (the assisted step") && log_has("dash=on (200 cm in 300 ms) phase=on") &&
		       log_has("STSITE ST_COL_SOLVER ok") && log_has("STSITE ST_VISION_GET ok") && g_step.ok_col && g_step.ok_vis,
	       "step: no CONFIG/STSITE lines (col %d vis %d)", g_step.ok_col, g_step.ok_vis);
	float pos0[3], rot0[4];
	memcpy(pos0, g_pobj.pos, sizeof pos0), memcpy(rot0, g_pobj.rot, sizeof rot0);
	somvr_rig rig0 = g_rig;
	somvr_rig_cfg cfg0 = g_rigcfg;
	const sa_node *node0 = g_nodexf_node;
	st_obj_t *bobj0 = g_pbody.obj;
	st_mgr_t *bmgr0 = g_pbody.mgr;
	uintptr_t vtpm0 = g_som.vt_pm, pmorig0 = g_h[HK_PM].orig, vtvm0 = g_stepg.vt_vm;
	int arm0 = g_arm.on;
	/* the loco_tests stage: Talion on the stand-in vf_11 with its timer (1/90 s), the head base, the stick live */
	g_views_fake = 0, g_rt_result = XR_SUCCESS, g_layout = g_rigcfg.layout = SOMVR_LAYOUT_A;
	g_rigcfg.base = SOMVR_BASE_HEAD, g_rigcfg.yaw = SOMVR_YAW_CAMERA, g_rigcfg.eye_cm = 0;
	g_follow.on = 0, g_fb.on = 0, g_cb.mode = CB_OFF, g_cl.on = 0, g_bow.on = 0, g_arm.on = 0;
	g_hide.on = 0, g_hudq.on = 0, g_nodexf_node = NULL, g_head_up = 72.5f, g_cf.out_s = g_cf.in_s = 0;
	somvr_rig_init(&g_rig);
	g_rt_head[0] = g_rt_head[1] = g_rt_head[2] = 0, g_rt_q = (sv_quat){ 0, 0, 0, 1 };
	memset(&g_loobj, 0, sizeof g_loobj);
	g_loobj.o = g_pobj;
	g_loobj.o.pos[0] = 500, g_loobj.o.pos[1] = 100, g_loobj.o.pos[2] = -50;
	g_loobj.o.rot[0] = g_loobj.o.rot[1] = g_loobj.o.rot[2] = 0, g_loobj.o.rot[3] = 1;
	g_lotimer.vt = g_cbg.vt_etimer, g_lotimer.frame_s = 1 / 90.0f;
	g_loobj.timer = &g_lotimer;
	memset(&g_lomgr, 0, sizeof g_lomgr);
	g_lomgr.hchar = &g_loobj.o;
	g_pbody.obj = &g_loobj.o, g_pbody.mgr = (st_mgr_t *)&g_lomgr;
	memset(g_loP, 0, sizeof g_loP);
	memcpy(g_loP, &g_player, sizeof g_player);
	g_loP[LO_P_FSM] = 7, g_loP[LO_P_LIFE] = 2; /* a fight */
	g_pl = (st_player_t *)g_loP, g_ppm.owner = g_loP;
	for (int i = 0; i < 40; i++)
		g_lovt[i] = i < 16 ? g_vtpm[i] : NULL;
	g_lovt[LO_VT_LOCK_T] = (void *)st_lo_lockt, g_lovt[LO_VT_LOCK_R] = (void *)st_lo_lockr;
	g_ppm.vt = g_som.vt_pm = (uintptr_t)g_lovt;
	((char *)&g_ppm)[LO_PM_VELMODE] = 1, ((char *)&g_ppm)[LO_PM_ATTACHED] = 0;
	memset(g_locmb, 0, sizeof g_locmb);
	g_locmb[ST_C_COLLIDE] = 1; /* the stock evade's EnableCollision: 1 (our hold must take it to 0 and back) */
	uintptr_t cmb = (uintptr_t)g_locmb;
	memcpy((char *)&g_ppm + LO_PM_ATTACK, &cmb, 8);
	g_h[HK_PM].orig = (uintptr_t)t_pm_loco;
	memset(&g_lot, 0, sizeof g_lot);
	g_lot.rmq[3] = 1;
	/* the wraith vision: P+0x2f0 -> a PlayerVisionMode stand-in (its vtable, the owner, the flag at +0x48) */
	static _Alignas(16) unsigned char vm[0x60];
	static uintptr_t vt_vm_stub;
	memset(vm, 0, sizeof vm);
	uintptr_t vt = (uintptr_t)&vt_vm_stub, P = (uintptr_t)g_loP, vmp = (uintptr_t)vm;
	memcpy(vm, &vt, 8), memcpy(vm + ST_VM_OWNER, &P, 8), memcpy(g_loP + ST_P_VISION, &vmp, 8);
	g_stepg.vt_vm = vt;
	g_cb_test_t = 30000;
	g_xrt = g_vst.last_time + 1000000000;
	runtime_up();
	g_inp.on = 1, g_inp.lines = 0, g_inp.lsec = 0;
	layer_up();
	for (int h = 0; h < 2; h++)
		g_lst.hand[h].squeeze = 0, g_lst.hand[h].stick[0] = g_lst.hand[h].stick[1] = 0;
	irelease(1, SOMVR_BTN_LOWER);
	g_lo.on = 1, g_lo.anchor = 1, g_lo.lines = 0, g_lo.next_log = 0, g_lo.t_pm = 0, g_lo.owned_last = 0;
	somvr_loco_init(&g_lo.s);
	somvr_anchor_init(&g_lo.a);
	g_step.on = 1, g_step.lines = 0, g_step.a_last = 0, g_step.tap = 0, g_step.vis_ours = g_step.vis_asked = 0;
	somvr_step_run_init(&g_step.run);
	atomic_fetch_add(&g_req_recenter, 1);
	lo_frames(4);
	EXPECT(g_lo.o.owned && g_step.fsm == 7 && g_step.have_root, "step: the stage: owned %d fsm %d", g_lo.o.owned, g_step.fsm);

	/* 1. the assisted step: an orc 1.7 m ahead, a swing starts -> 50 cm toward it over 0.18 s; the orc in reach: none */
	sv_vec3 r0 = lo_root();
	struct cb_orc co[1] = { { 0x88, 0, { r0.x, r0.y + 90, r0.z + 170 }, 170 } };
	somvr_blade b;
	memset(&b, 0, sizeof b);
	b.valid = 1, b.t = cb_now();
	b.tip[0] = r0.x + 60, b.tip[1] = r0.y + 150, b.tip[2] = r0.z + 50, b.vel_tip[0] = -600;
	float rt[3] = { r0.x, r0.y, r0.z };
	st_combat(co, 1, &b, 1, rt, cb_now());
	EXPECT(g_step.run.kind == SOMVR_ST_STEP && log_has("assisted step: 50.0 cm in 180 ms (peak 556 cm/s)"),
	       "step: the swing's step: kind %d", g_step.run.kind);
	step_frames(17, co, 1);
	sv_vec3 r1 = lo_root();
	EXPECT(NEAR(r1.z - r0.z, 50, 0.3) && NEAR(r1.x, r0.x, 1e-3) && g_step.run.kind == SOMVR_ST_NONE &&
		       log_has("step end: done after"),
	       "step: 50 cm toward the orc: %g, %g (kind %d)", r1.x - r0.x, r1.z - r0.z, g_step.run.kind);
	co[0].pos[2] = r1.z + 110;
	rt[0] = r1.x, rt[1] = r1.y, rt[2] = r1.z;
	st_combat(co, 1, &b, 1, rt, cb_now());
	EXPECT(g_step.run.kind == SOMVR_ST_NONE && log_has("swing: no step (in reach)"), "step: in reach: none");
	r0 = lo_root();
	lo_frames(3);
	EXPECT(NEAR3(lo_root(), r0.x, r0.y, r0.z, 1e-4), "step: standing still after it");

	/* 2. the dash: the stick left, A -> 200 cm left in 0.3 s (the stick's part along it is the dash's), the stock roll
	 * (an attack, its root motion +z) under it never moves Talion; the hold: the stick 1:1 until the roll ends */
	g_lst.hand[0].stick[0] = -1;
	lo_frames(3); /* walking left, capped at the run band in the fight */
	r0 = lo_root();
	ipress(1, SOMVR_BTN_LOWER);
	lo_frame();
	EXPECT(g_step.run.kind == SOMVR_ST_DASH && log_has("dash: 200.0 cm in 300 ms"), "dash: started: kind %d", g_step.run.kind);
	g_lot.attack = 1, g_lot.rm[2] = 3; /* the stock roll from the second tick */
	lo_frames(26);
	sv_vec3 r2 = lo_root();
	EXPECT(NEAR(r2.x - r0.x, -200, 0.5) && NEAR(r2.z, r0.z, 1e-3) && g_lo.o.owned && g_lo.n.ours > 0,
	       "dash: 200 cm left in 27 ticks, none of the roll's: %g, %g (owned %d)", r2.x - r0.x, r2.z - r0.z, g_lo.o.owned);
	irelease(1, SOMVR_BTN_LOWER);
	r0 = lo_root();
	lo_frames(5);
	EXPECT(NEAR(lo_root().x - r0.x, -5 * 500 / 90.0, 0.05) && NEAR(lo_root().z, r0.z, 1e-3) && g_lo.o.owned &&
		       g_step.run.kind == SOMVR_ST_DASH,
	       "dash: the hold: the stick 1:1 over the roll: %g", lo_root().x - r0.x);
	g_lot.attack = 0;
	lo_frame();
	EXPECT(g_step.run.kind == SOMVR_ST_NONE && log_has("dash end: held"), "dash: held to the roll's end (kind %d)",
	       g_step.run.kind);
	g_lot.attack = 1;
	lo_frames(2);
	EXPECT(!g_lo.o.owned && (g_lo.o.why & SOMVR_LOCO_W_ATTACK), "dash: over: an attack is the game's again");
	g_lot.attack = 0, g_lot.rm[2] = 0;
	g_lst.hand[0].stick[0] = 0;
	lo_frames(2);

	/* 3. the wraith phase: an orc 2 m ahead, the stick at rest, A -> through it to 1.1 m beyond in 0.4 s; the stock vault
	 * (an attack) under it: its root motion and its re-face rotation never reach Talion, its EnableCollision held 0 and
	 * restored; the wraith vision tapped on (122) and off around the motion */
	r0 = lo_root();
	co[0].pos[0] = r0.x, co[0].pos[2] = r0.z + 200;
	step_frames(1, co, 1);
	r0 = lo_root();
	int ev0 = g_ninev;
	ipress(1, SOMVR_BTN_LOWER);
	step_frames(1, co, 1);
	EXPECT(g_step.run.kind == SOMVR_ST_PHASE && log_has("wraith phase: 310.0 cm in 400 ms"), "phase: started: kind %d",
	       g_step.run.kind);
	irelease(1, SOMVR_BTN_LOWER);
	g_lot.attack = 1, g_lot.rm[2] = 3;
	sv_quat ry = sv_qyaw(10 * 0.01745329f); /* the vault turns the root 10 deg a tick (its re-face) */
	g_lot.rmq[0] = ry.x, g_lot.rmq[1] = ry.y, g_lot.rmq[2] = ry.z, g_lot.rmq[3] = ry.w;
	step_frames(2, co, 1);
	EXPECT(g_locmb[ST_C_COLLIDE] == 0 && g_locmb[ST_C_COLDIRTY] == 1 && log_has("the orc's collision off"),
	       "phase: the collision held: %d", g_locmb[ST_C_COLLIDE]);
	EXPECT(log_has("wraith vision on (a tap of 122") && (nev('N', ST_CMD_VISION, ev0) >= 1 || !g_rec_of[ST_CMD_VISION]),
	       "phase: the vision's tap: %d events", nev('N', ST_CMD_VISION, ev0));
	vm[ST_VM_ON] = 1; /* the game turned the vision on */
	step_frames(33, co, 1);
	sv_vec3 r3 = lo_root();
	EXPECT(NEAR(r3.z - r0.z, 310, 0.5) && NEAR(r3.x, r0.x, 1e-3) && fabsf(lo_yaw()) < 0.05f && log_has("the phase at the orc's axis"),
	       "phase: through the orc to %g (x %g), the yaw %g (no re-face of the body)", r3.z - r0.z, r3.x - r0.x, lo_yaw());
	step_frames(2, co, 1);
	EXPECT(g_locmb[ST_C_COLLIDE] == 1 && log_has("EnableCollision restored to 1") && log_has("wraith vision off (our tap)"),
	       "phase: after the motion: the collision %d, the vision off", g_locmb[ST_C_COLLIDE]);
	vm[ST_VM_ON] = 0;
	g_lot.attack = 0;
	step_frames(1, co, 1);
	EXPECT(g_step.run.kind == SOMVR_ST_NONE && log_has("phase end: held"), "phase: held to the vault's end");
	g_lot.rm[2] = 0, g_lot.rmq[0] = g_lot.rmq[1] = g_lot.rmq[2] = 0, g_lot.rmq[3] = 1;

	/* 4. the options: the dash's blink (the record's fade level), the menu turning a running dash off, the re-face */
	g_step.cfg.dash = SOMVR_ST_DASH_BLINK;
	g_lst.hand[0].stick[0] = 1;
	lo_frames(2);
	ipress(1, SOMVR_BTN_LOWER);
	lo_frames(10);
	irelease(1, SOMVR_BTN_LOWER);
	EXPECT(g_step.run.kind == SOMVR_ST_DASH && g_fr.rec && somvr_rec_fade(g_fr.rec->flags) == 255 && g_step.n_blink > 0,
	       "blink: the fade level %u in the dash", g_fr.rec ? somvr_rec_fade(g_fr.rec->flags) : 0);
	somvr_settings s;
	somvr_set_default(&s);
	s.step = g_step.cfg.step, s.dash = 0, s.phase = (uint32_t)g_step.cfg.phase;
	uint64_t off0 = g_step.run.ends[SOMVR_STE_OFF];
	st_set(SOMVR_SK_DASH, &s);
	EXPECT(g_step.run.kind == SOMVR_ST_NONE && g_step.run.ends[SOMVR_STE_OFF] == off0 + 1 && g_step.cfg.dash == 0,
	       "the menu: the dash off stops it");
	lo_frame(); /* A seen up */
	r0 = lo_root();
	ipress(1, SOMVR_BTN_LOWER);
	lo_frame();
	irelease(1, SOMVR_BTN_LOWER);
	EXPECT(g_step.run.kind == SOMVR_ST_NONE && log_has("A: no evade of ours (off"), "the dash off: the stock A");
	g_lst.hand[0].stick[0] = 0;
	g_step.cfg.dash = SOMVR_ST_DASH_ON, g_step.cfg.phase = SOMVR_ST_PHASE_REFACE;
	lo_frames(2);
	r0 = lo_root();
	co[0].pos[0] = r0.x, co[0].pos[2] = r0.z + 150;
	step_frames(1, co, 1);
	ipress(1, SOMVR_BTN_LOWER);
	step_frames(30, co, 1);
	irelease(1, SOMVR_BTN_LOWER);
	EXPECT(g_step.n_reface == 1 && log_has("re-face: the rig turns 180 deg"), "the re-face: %llu",
	       (unsigned long long)g_step.n_reface);
	step_frames(10, co, 1);
	g_step.cfg.phase = SOMVR_ST_PHASE_ON;
	/* 5. off: A is the stock A */
	g_step.on = 0;
	ipress(1, SOMVR_BTN_LOWER);
	lo_frame();
	irelease(1, SOMVR_BTN_LOWER);
	EXPECT(g_step.run.kind == SOMVR_ST_NONE, "off: no move");
	g_step.on = 1;
	st_end_log();
	EXPECT(log_has("STEP END picks step 1 dash 2 phase 2"), "step: no END line");
	printf("step   on the LOCO stand-ins: the assisted step (50 cm toward the orc in 0.18 s, none in reach), the dash (200 cm "
	       "in 0.3 s over the stock roll, the hold, the game's after), the wraith phase (through the orc to 310 cm over the "
	       "stock vault: no root motion, no re-face rotation, the collision held and restored, the vision tapped on and "
	       "off), the blink, the menu, the re-face, off\n");
	g_step.on = 0;
	g_lo.on = 0, g_lo.anchor = 0;
	g_lot.attack = 0;
	g_lst.hand[0].stick[0] = g_lst.hand[0].stick[1] = 0;
	g_h[HK_PM].orig = pmorig0, g_som.vt_pm = vtpm0, g_ppm.vt = (uintptr_t)g_vtpm, g_stepg.vt_vm = vtvm0;
	g_pbody.obj = bobj0, g_pbody.mgr = bmgr0;
	g_pl = &g_player, g_ppm.owner = &g_player;
	g_rig = rig0, g_rigcfg = cfg0, g_nodexf_node = node0;
	memcpy(g_pobj.pos, pos0, sizeof pos0), memcpy(g_pobj.rot, rot0, sizeof rot0);
	g_arm.on = arm0;
	g_layer_live = 0;
	g_cb_test_t = -1;
}

/* ---- spike1's save-guard tests ---------------------------------------------------------------------------------- */
static volatile int g_orig_reached;
__attribute__((noipa)) uint64_t o_write(void *t, const char *n, const void *d, int sz) { (void)t, (void)n, (void)d, (void)sz; g_orig_reached++; return 1; }
__attribute__((noipa)) uint64_t o_size(void *t, const char *n) { (void)t, (void)n; g_orig_reached += 2; return 7; }
__attribute__((noipa)) uint64_t o_read(void *t, const char *n, void *b, int sz) { (void)t, (void)n, (void)b, (void)sz; g_orig_reached += 4; return 7; }
__attribute__((noipa)) uint64_t o_delete(void *t, const char *n) { (void)t, (void)n; g_orig_reached += 8; return 1; }
typedef uint64_t (*w_fn)(void *, const char *, const void *, int);
typedef uint64_t (*s_fn)(void *, const char *);
typedef uint64_t (*r_fn)(void *, const char *, void *, int);
static void *volatile g_gvt[4];
static char g_ghex[4][256];
static void guard_specs(struct site_spec *gs)
{
	static const char *const names[4] = { "G219", "G220", "G221", "G222" };
	void *fns[4] = { (void *)o_write, (void *)o_size, (void *)o_read, (void *)o_delete };
	for (int i = 0; i < 4; i++) {
		g_gvt[i] = fns[i];
		hex_of((const unsigned char *)&g_gvt[i], 8, g_ghex[i], 128);
		hex_of(fns[i], 4, g_ghex[i] + 128, 128);
		gs[i] = (struct site_spec){ names[i], K_VSLOT, (uintptr_t)&g_gvt[i], (uintptr_t)&g_gvt[i], g_ghex[i],
					    (uintptr_t)fns[i], g_ghex[i] + 128 };
	}
}
static int child_status(const struct site_spec *gs, const char *dir)
{
	pid_t pid = fork();
	if (pid == 0) {
		setenv("SOMVR_SAVEDIR", dir, 1);
		guard_or_die(gs, 1);
		_exit(0);
	}
	int st = 0;
	waitpid(pid, &st, 0);
	return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}
static void guard_tests(const char *base)
{
	char dir[1024], path[1100], buf[16] = { 0 };
	snprintf(dir, sizeof dir, "%s/saves", base);
	mkdir(dir, 0755);
	struct site_spec gs[4], bad[4];
	guard_specs(gs);
	memcpy(bad, gs, sizeof bad);
	bad[2].ctx_hex = "00 00 00 00 00 00 00 00";
	EXPECT(child_status(bad, dir) == 3, "guard with a mismatching slot did not exit 3");
	EXPECT(child_status(gs, "/nonexistent/somvr") == 3, "guard with a missing directory did not exit 3");
	EXPECT(g_gvt[0] == (void *)o_write, "the failed guard children changed the parent's table");
	EXPECT(guard_install(gs, dir) == 0, "guard_install failed (see the log)");
	EXPECT(((s_fn)g_gvt[1])(NULL, "GameData.sav") == 0, "size of an absent file is not 0");
	EXPECT(((w_fn)g_gvt[0])(NULL, "GameData.sav", "hello", 5) == 1, "write failed");
	snprintf(path, sizeof path, "%s/GameData.sav", dir);
	FILE *f = fopen(path, "r");
	EXPECT(f && fread(buf, 1, sizeof buf, f) == 5 && !memcmp(buf, "hello", 5), "written file content");
	if (f)
		fclose(f);
	EXPECT(((r_fn)g_gvt[2])(NULL, "GameData.sav", buf, sizeof buf) == 5 && !memcmp(buf, "hello", 5), "read back");
	/* the game's own name form (spike1 run 2): "\GameData.sav" is the same file */
	EXPECT(((s_fn)g_gvt[1])(NULL, "\\GameData.sav") == 5, "size of \\GameData.sav");
	memset(buf, 0, sizeof buf);
	EXPECT(((r_fn)g_gvt[2])(NULL, "\\GameData.sav", buf, sizeof buf) == 5 && !memcmp(buf, "hello", 5),
	       "read back as \\GameData.sav");
	EXPECT(((w_fn)g_gvt[0])(NULL, "\\GameData.sav", "world!", 6) == 1, "write as \\GameData.sav");
	EXPECT(((s_fn)g_gvt[1])(NULL, "GameData.sav") == 6, "\\GameData.sav wrote another file");
	snprintf(path, sizeof path, "%s/\\GameData.sav", dir);
	EXPECT(access(path, F_OK) != 0, "a file named \\GameData.sav was created");
	EXPECT(((w_fn)g_gvt[0])(NULL, "\\", "x", 1) == 0, "an empty name after the backslash was written");
	EXPECT(((w_fn)g_gvt[0])(NULL, "\\..\\escape.sav", "x", 1) == 0, "a backslash path was written");
	EXPECT(((w_fn)g_gvt[0])(NULL, "a\\b.sav", "x", 1) == 0, "an inner backslash was written");
	EXPECT(((w_fn)g_gvt[0])(NULL, "../escape.sav", "x", 1) == 0, "a name with a path was written");
	EXPECT(((w_fn)g_gvt[0])(NULL, NULL, "x", 1) == 0, "a null name was written");
	EXPECT(((s_fn)g_gvt[3])(NULL, "GameData.sav") == 1, "delete failed");
	EXPECT(((s_fn)g_gvt[1])(NULL, "GameData.sav") == 0, "size after the delete");
	EXPECT(g_orig_reached == 0, "an original storage function ran (%d)", g_orig_reached);
	printf("guard: fail-closed children exit 3, write/size/read/delete served from %s, originals never ran\n", dir);
}

/* ---- SOMVR_THREAT (threat_tests): the read-only proofs on stand-in code, GetNode, the Wwise object, the projectile
 * slot (a stand-in CProjectile::vf_13 the hook goes into) and the orcs of cb_world with their key times ------------ */
static char g_thhex[THS_N][256];
static struct site_spec g_ths[THS_N];
__attribute__((noipa)) uint64_t t_thproof(uint64_t a) { return a * 3 + 1; } /* the K_CALL proofs' stand-in code */
__attribute__((noipa)) uint64_t c_thproof(uint64_t a) { return t_thproof(a) + 2; }
static struct { int reg, pos, post; uintptr_t wrap; uint32_t obj; float at[3]; char name[64]; uint32_t id; } g_thsnd;
static int g_thnode_calls;
static char g_snd_wrap[64];
/* GetNode: only orc A has an R_Hand (the id the stand-in GetNodeTransform knows: the root + g_head_up) */
__attribute__((noipa)) uint64_t st_th_getnode(uintptr_t m, uintptr_t obj, const char *name, uint32_t *node)
{
	g_thnode_calls++;
	if (m != (uintptr_t)g_model_ptr || obj != (uintptr_t)&g_oobj[0] || strcmp(name, "R_Hand"))
		return 1;
	*node = 0x72598328u;
	return 0;
}
__attribute__((noipa)) uint64_t st_th_sndreg(uintptr_t wrap, uint32_t obj, const char *name)
{
	g_thsnd.reg++, g_thsnd.wrap = wrap, g_thsnd.obj = obj;
	return strcmp(name, "somvr_threat") == 0;
}
__attribute__((noipa)) uint64_t st_th_sndpos(uintptr_t wrap, uint32_t obj, const float *pos, const float *front)
{
	g_thsnd.pos++, g_thsnd.wrap = wrap, g_thsnd.obj = obj;
	memcpy(g_thsnd.at, pos, sizeof g_thsnd.at);
	return front[0] * front[0] + front[1] * front[1] + front[2] * front[2] > 0.99f;
}
__attribute__((noipa)) uint32_t st_th_sndpost(uintptr_t wrap, uint32_t obj, const char *name)
{
	g_thsnd.post++, g_thsnd.wrap = wrap, g_thsnd.obj = obj;
	snprintf(g_thsnd.name, sizeof g_thsnd.name, "%s", name);
	return g_thsnd.id;
}
static void *volatile g_vtpj[16]; /* CProjectile's vtable: slot 13 = vf_13 (TH_PJ_UPDATE) */
static int g_pjv_calls;
__attribute__((noipa)) uint64_t st_pj_vf13(uint64_t self, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f)
{
	(void)b, (void)c, (void)d, (void)e, (void)f;
	g_pjv_calls++;
	return self + 7;
}
static int th_call_site(int i, const char *name)
{
	const unsigned char *p = (const unsigned char *)c_thproof;
	for (int k = 0; k < 96; k++) {
		int32_t rel;
		memcpy(&rel, p + k + 1, 4);
		if (p[k] == 0xe8 && (uintptr_t)(p + k + 5 + rel) == (uintptr_t)t_thproof) {
			hex_of(p + k, 5, g_thhex[i], 128);
			hex_of((const void *)t_thproof, 4, g_thhex[i] + 128, 128);
			g_ths[i] = (struct site_spec){ name, K_CALL, (uintptr_t)(p + k), (uintptr_t)(p + k), g_thhex[i],
						       (uintptr_t)t_thproof, g_thhex[i] + 128 };
			return 0;
		}
	}
	return -1;
}
static void th_entry_site(void *fn, int i, const char *name)
{
	hex_of(fn, 8, g_thhex[i], 128);
	hex_of(fn, 4, g_thhex[i] + 128, 128);
	g_ths[i] = (struct site_spec){ name, K_ENTRY, (uintptr_t)fn, (uintptr_t)fn, g_thhex[i], (uintptr_t)fn, g_thhex[i] + 128 };
}
static int th_stand_in_sites(void)
{
	if (th_call_site(THS_ATK_START, "TH_ATK_START") || th_call_site(THS_WS, "TH_WS") ||
	    th_call_site(THS_KEYTABLE, "TH_KEYTABLE") || th_call_site(THS_PJ_MOVE, "TH_PJ_MOVE"))
		return -1;
	th_entry_site((void *)t_thproof, THS_CURATK, "TH_CURATK");
	th_entry_site((void *)st_th_getnode, THS_GETNODE, "TH_GETNODE");
	th_entry_site((void *)st_th_sndreg, THS_SND_REG, "TH_SND_REGOBJ");
	th_entry_site((void *)st_th_sndpos, THS_SND_POS, "TH_SND_SETPOS");
	th_entry_site((void *)st_th_sndpost, THS_SND_POST, "TH_SND_POST");
	g_vtpj[13] = (void *)st_pj_vf13;
	hex_of((const unsigned char *)&g_vtpj[13], 8, g_thhex[THS_PJ_UPDATE], 128);
	hex_of((const void *)st_pj_vf13, 4, g_thhex[THS_PJ_UPDATE] + 128, 128);
	g_ths[THS_PJ_UPDATE] = (struct site_spec){ "TH_PJ_UPDATE", K_VSLOT, (uintptr_t)&g_vtpj[13], (uintptr_t)&g_vtpj[13],
						   g_thhex[THS_PJ_UPDATE], (uintptr_t)st_pj_vf13, g_thhex[THS_PJ_UPDATE] + 128 };
	g_thg.snd_wrap = (uintptr_t)g_snd_wrap;
	g_thsnd.id = 4242;
	return 0;
}
static int th_log_count(const char *needle)
{
	flush_log();
	FILE *f = fopen(g_logp, "r");
	char line[4096];
	int n = 0;
	while (f && fgets(line, sizeof line, f))
		n += strstr(line, needle) != NULL;
	if (f)
		fclose(f);
	return n;
}
static somvr_threat_frame th_entry(const char *what)
{
	somvr_threat_frame e;
	memset(&e, 0, sizeof e);
	EXPECT(somvr_threat_get(&g_tht, g_frame_no, &e), "threat %s: no entry for frame %llu", what,
	       (unsigned long long)g_frame_no);
	return e;
}
static const uint16_t TH_KT_INF[10] = { 0xffff, 266, 800, 0xffff, 0xffff, 0xffff, 0, 761, 0xffff, 100 }; /* P8's melee */
static void th_attack(int i, uintptr_t target, uintptr_t attack, const uint16_t *ws)
{
	memcpy((char *)&g_oc[i] + TH_C_TARGET, &target, 8);
	if (ws)
		memcpy((char *)&g_oc[i] + TH_C_KT, ws, 20);
	g_oc[i].attack = attack;
}
static struct th_track *th_tr_of(int i) /* orc i's track (NULL: none) */
{
	for (int k = 0; k < TH_TRACKS; k++)
		if (g_th.tr[k].c == (uintptr_t)&g_oc[i])
			return &g_th.tr[k];
	return NULL;
}
static double th_ts(int i) { return th_tr_of(i) ? g_cb_test_t - th_tr_of(i)->t0 : -1; }
static void th_drain_haptics(void)
{
	somvr_haptic_req q;
	while (hap_take(0, &q) || hap_take(1, &q)) {
	}
}
static unsigned char g_proj[TH_PJ_END + 16]; /* a stand-in CProjectile */
static void th_proj_tick(void)            /* the game's update: vf_13 through its slot (our hook), then the flight */
{
	EXPECT(((th_six_fn)g_vtpj[13])((uint64_t)(uintptr_t)g_proj, 1, 2, 3, 4, 5) == (uint64_t)(uintptr_t)g_proj + 7,
	       "the projectile slot's return value");
	float p[3], q[4], sp;
	memcpy(p, g_proj + TH_PJ_POS, 12), memcpy(q, g_proj + TH_PJ_ROT, 16), memcpy(&sp, g_proj + TH_PJ_SPEED, 4);
	sv_vec3 d = sv_qrot(sv_quat_of(q), (sv_vec3){ 0, 0, 1 });
	p[0] += d.x * sp / 90, p[1] += d.y * sp / 90, p[2] += d.z * sp / 90;
	memcpy(g_proj + TH_PJ_POS, p, 12);
}

/* build 17: A's one owner per press (ab_tick: the strike > the dodge > the climb) through the glue: the dodge's own tick
 * yields or acts, the climb's drop sees A up whenever the press is another's, the owner kept until A is let go */
static void ab_tests(void)
{
	__typeof__(g_ab) ao0 = g_ab;
	__typeof__(g_dg) dg0 = g_dg;
	int sg_on0 = g_sg.on, st_on0 = g_step.on, fsm0 = g_step.fsm, cl_on0 = g_cl.on, cl_act0 = g_cl.s.active, th_on0 = g_th.on;
	int gm0 = g_inp.cfg.game_moves;
	uint32_t down0 = g_inp.ctl.down[SOMVR_HAND_R];
	uint64_t mark0 = g_sg.g.mark;
	int phase0 = g_sg.g.phase;
	double t0 = g_cb_test_t;
	memset(&g_ab, 0, sizeof g_ab);
	g_dg.on = g_dg.want = 1, g_dg.a_last = 0, g_dg.ours = 0, g_dg.next_ok = 1e12; /* a dodge asked for = refused by the cooldown (no rays) */
	g_sg.on = 1, g_sg.g.mark = 0, g_sg.g.phase = 0;
	g_step.on = 1, g_cl.on = 1, g_cl.s.active = 0, g_th.on = 0, g_inp.cfg.game_moves = 0;
	g_cb_test_t = 40000;
	/* build 19: the VR menu's 64-bit avail - the Fight page's Dodge (A) row (bit 32) while the dodge is ours; the row
	 * turns it off and on live (dg_vm_set), the bit stays (SOMVR_DODGE at the start) */
	uint64_t sa_dodge = st_avail();
	g_inp.cfg.game_moves = 1;
	uint64_t sa_game = st_avail();
	g_inp.cfg.game_moves = 0, g_dg.on = g_dg.want = 0;
	uint64_t sa_off = st_avail();
	const char *dn_off = dg_vm_set(&(somvr_settings){ .dodge = 1 });
	int dg_unused = !g_dg.on;
	g_dg.on = g_dg.want = 1;
	const char *dn0 = dg_vm_set(&(somvr_settings){ .dodge = 0 });
	int dg_off = g_dg.on, dg_bit = (st_avail() & SOMVR_VMA_DODGE) != 0;
	dg_vm_set(&(somvr_settings){ .dodge = 1 });
	EXPECT(sa_dodge == SOMVR_VMA_DODGE && sa_game == SOMVR_VMA_MOVES && sa_off == 0 && SOMVR_VMA_DODGE == 1ull << 32 &&
		       dg_unused && strstr(dn_off, "not used") && !dg_off && dg_bit && strstr(dn0, "does nothing") && g_dg.on,
	       "st_avail: the dodge %#llx, the game's moves %#llx, off %#llx; the row: off '%s' (on %d, the bit %d), on again %d",
	       (unsigned long long)sa_dodge, (unsigned long long)sa_game, (unsigned long long)sa_off, dn0, dg_off, dg_bit, g_dg.on);
	sc_tick_out o;
	memset(&o, 0, sizeof o);
	o.live = 1, o.ctx = SC_CTX_WORLD;
	struct { const char *what; int fsm, mark, climbing, menu, owner, not_climbs, why; } cs[] = {
		{ "a fight at an edge", 7, 0, 0, 0, SOMVR_AO_DODGE, 1, DGR_COOL },
		{ "the vision held in a fight, an orc marked", 7, 5, 0, 0, SOMVR_AO_STRIKE, 1, DGR_YIELD },
		{ "the vision held in a fight, no mark", 7, 0, 0, 0, SOMVR_AO_DODGE, 1, DGR_COOL },
		{ "a hang in a fight", 7, 0, 1, 0, SOMVR_AO_DROP, 0, DGR_YIELD },
		{ "no fight, no mark (an edge)", 0, 0, 0, 0, SOMVR_AO_DROP, 0, DGR_NOFIGHT },
		{ "a mark, no fight (an edge)", 0, 5, 0, 0, SOMVR_AO_STRIKE, 1, DGR_YIELD },
		{ "a menu's A", 7, 5, 0, 1, SOMVR_AO_NONE, 0, -1 },
	};
	for (size_t i = 0; i < sizeof cs / sizeof cs[0]; i++) {
		g_step.fsm = cs[i].fsm, g_sg.g.mark = (uint64_t)cs[i].mark, g_cl.s.active = cs[i].climbing;
		o.ctx = cs[i].menu ? SC_CTX_MENU : SC_CTX_WORLD;
		uint64_t why0[DGR_N], go0 = g_dg.n_go;
		memcpy(why0, g_dg.n_why, sizeof why0);
		g_inp.ctl.down[SOMVR_HAND_R] |= SOMVR_BTN_LOWER;
		ab_tick(&o);
		dg_tick(&o);
		int owner = ab_owner(), nc = ab_not_climbs(), why = -1, nwhy = 0;
		for (int k = 0; k < DGR_N; k++)
			if (g_dg.n_why[k] != why0[k])
				why = k, nwhy++;
		/* held on: the mark goes, the fight ends - the press stays its owner's (no second act mid-hold) */
		g_sg.g.mark = 0, g_step.fsm = 0, g_cl.s.active = 0;
		g_cb_test_t += 0.1;
		ab_tick(&o);
		dg_tick(&o);
		int kept = ab_owner() == owner && ab_not_climbs() == nc;
		g_inp.ctl.down[SOMVR_HAND_R] &= ~SOMVR_BTN_LOWER;
		ab_tick(&o);
		dg_tick(&o);
		int released = ab_owner() == SOMVR_AO_NONE && !ab_not_climbs();
		g_cb_test_t += 0.1;
		EXPECT(owner == cs[i].owner && nc == cs[i].not_climbs && why == cs[i].why && nwhy == (cs[i].why >= 0) &&
			       g_dg.n_go == go0 && kept && released,
		       "A's owner, %s: owner %d (want %d), the climb sees A %s (want %s), the dodge's %d (want %d, %d reasons), "
		       "kept %d, released %d", cs[i].what, owner, cs[i].owner, nc ? "up" : "down", cs[i].not_climbs ? "up" : "down",
		       why, cs[i].why, nwhy, kept, released);
	}
	EXPECT(log_has("A t=") && log_has("press: the strike's (wanted by the strike the dodge the climb") &&
		       log_has("press: the dodge's (wanted by the dodge the climb"),
	       "A's owner: the contested presses logged");
	g_ab = ao0, g_dg = dg0;
	g_sg.on = sg_on0, g_step.on = st_on0, g_step.fsm = fsm0, g_cl.on = cl_on0, g_cl.s.active = cl_act0, g_th.on = th_on0;
	g_inp.cfg.game_moves = gm0, g_inp.ctl.down[SOMVR_HAND_R] = down0, g_sg.g.mark = mark0, g_sg.g.phase = phase0;
	g_cb_test_t = t0;
}

static void threat_tests(void)
{
	EXPECT(log_has("CONFIG threat on=1 view_deg=110 view=bearing keyless=0 glow=0.70 edge=0.80 near_m=2.5 haptic=1.00 parry=1 "
		       "sound='fol_cmn_unsheathe' proj=on") &&
		       log_has("TH_ATK_START ok, TH_WS ok, TH_KEYTABLE ok, TH_CURATK ok, TH_GETNODE ok (R_Hand read)") &&
		       log_has("TH_SND_REGOBJ ok TH_SND_SETPOS ok TH_SND_POST ok") && log_has("HOOK TH_PJ_UPDATE ok slot"),
	       "threat: no CONFIG/HOOK lines at start");
	EXPECT(dlsym(RTLD_DEFAULT, SOMVR_THREAT_SYMBOL) == (void *)&g_tht && somvr_threat_ok(&g_tht) &&
		       g_tht.size == sizeof g_tht && NEAR(g_tht.glow, 0.7, 1e-6) && NEAR(g_tht.edge, 0.8, 1e-6) &&
		       g_vtpj[13] == (void *)th_pj_vf13 && g_th_pj_orig == (uintptr_t)st_pj_vf13,
	       "threat: somvr_threat %d, the projectile slot %p", somvr_threat_ok(&g_tht), g_vtpj[13]);
	/* the world: the game camera base (GAME: 100 200 300, ahead = +Z), Talion's root 1.7 m below it; A 1.5 m ahead of
	 * the head (its R_Hand 2.5 cm above the head's height), B behind and left */
	g_views_fake = 0, g_rt_result = XR_SUCCESS, g_layout = g_rigcfg.layout = SOMVR_LAYOUT_A;
	g_rigcfg.base = SOMVR_BASE_CAMERA, g_rigcfg.yaw = SOMVR_YAW_CAMERA, g_follow.on = 0, g_fb.on = 0, g_cb.mode = CB_OFF;
	g_cl.on = 0, g_hide.on = 0, g_hudq.on = 0, g_cf.out_s = g_cf.in_s = 0, g_arm.on = 0, g_bow.on = 0, g_rx.mode = RX_OFF;
	somvr_rig_init(&g_rig);
	g_rt_head[0] = g_rt_head[1] = g_rt_head[2] = 0, g_rt_q = (sv_quat){ 0, 0, 0, 1 };
	g_rm[0] = g_rm[1] = g_rm[2] = 0;
	g_pobj.pos[0] = 100, g_pobj.pos[1] = 30, g_pobj.pos[2] = 300;
	g_pobj.rot[0] = g_pobj.rot[1] = g_pobj.rot[2] = 0, g_pobj.rot[3] = 1;
	g_cb_test_t = 20000;
	g_xrt = g_vst.last_time + 1000000000;
	runtime_up();
	g_inp.on = 1, g_inp.lines = 0, g_inp.lsec = 0;
	layer_up();
	g_pl = &g_player, g_ppm.owner = &g_player;
	g_pz[0x34] = 0;
	g_nodexf_node = NULL, g_nodexf_fail = g_nodexf_nan = 0, g_head_up = 72.5f;
	cb_world();
	static const float opos[2][3] = { { 100, 130, 450 }, { 0, 130, 100 } };
	for (int i = 0; i < 2; i++)
		memcpy(g_oobj[i].pos, opos[i], sizeof opos[i]);
	void *mi = &g_piface;
	memcpy((char *)&g_oobj[0] + OBJ_MODEL, &mi, 8); /* A's model interface (GetNodeTransform's FUN_008f3c20) */
	g_th.on = 1, g_th.tid = 0, g_th.last_t = 0, g_th.lines = 0;
	memset(g_th.tr, 0, sizeof g_th.tr);
	uintptr_t T = (uintptr_t)&g_pobj;
	atomic_fetch_add(&g_req_recenter, 1);
	for (int i = 0; i < 3; i++)
		iframe();
	somvr_threat_frame e = th_entry("calm");
	EXPECT(e.nglow == 0 && e.edge[0] == 0 && e.edge[1] == 0 && e.threats == 0 && e.flags == 0 && g_inp.live &&
		       g_th.n.frames >= 3,
	       "threat calm: glows %u edges %g %g threats %u flags %#x live %d", e.nglow, e.edge[0], e.edge[1], e.threats,
	       e.flags, g_inp.live);
	/* 1. A (in view) starts P8's infantry strike at Talion: the glow at its R_Hand, rising to its peak inside the parry
	 * window, out after the impact; no pulse or sound for an attack in view; the parry tick at the window's opening */
	th_drain_haptics();
	int starts0 = th_log_count("THREAT start orc="), warn0 = th_log_count("THREAT warn orc=");
	g_vm.on = 1, g_vm.gate = 0, g_vm.lines = 0; /* SOMVR_VRMENU's game side (white box): the gesture's gate */
	iframe();
	EXPECT(atomic_load(&g_vmo.block) == 0, "threat calm: the gate %#x", atomic_load(&g_vmo.block));
	th_attack(0, T, (uintptr_t)g_recs[0], TH_KT_INF);
	iframe();
	somvr_haptic_req q;
	int tick = hap_take(SOMVR_HAND_R, &q) && NEAR(q.amplitude, 0.15, 1e-3), tick_l = hap_take(SOMVR_HAND_L, &q);
	EXPECT(th_log_count("THREAT start orc=") == starts0 + 1 &&
		       log_has("kind=melee impact=0.800 s window=0.000..0.761 s bcol=0.266 s (table EAL 65535 BCOL 266 APPLYDAMAGE 800 "
			       "BPW 0 EPW 761 SHOOT 65535)") &&
		       log_has("THREAT parry tick orc=") && tick && !tick_l && g_th.n.ticks == 1,
	       "threat A start: lines %d, parry tick %d (left %d), ticks %llu", th_log_count("THREAT start orc=") - starts0, tick,
	       tick_l, (unsigned long long)g_th.n.ticks);
	float peak = 0, tpeak = 0, prev = 0, size = 0, pos[3] = { 0, 0, 0 };
	int rising = 1, window = 0, frames = 0, g_before = 0, g_nbefore = 0, g_after = 0, g_nafter = 0;
	for (int i = 0; i < 100; i++) {
		iframe();
		e = th_entry("A");
		float ts = (float)th_ts(0);
		if (!th_tr_of(0))
			break;
		frames++;
		/* the gate this tick published (the last frame's): an attack incoming until its impact, nothing after it */
		uint32_t gb = atomic_load(&g_vmo.block);
		if (ts <= 0.78f)
			g_nbefore++, g_before += gb == SOMVR_VMG_THREAT;
		else if (ts >= 0.83f)
			g_nafter++, g_after += gb != 0;
		window += (e.flags & SOMVR_THF_WINDOW) != 0;
		if (e.nglow) {
			if (ts <= 0.8f)
				rising &= e.g[0].level >= prev - 1e-6f;
			if (e.g[0].level > peak)
				peak = e.g[0].level, tpeak = ts;
			prev = e.g[0].level, size = e.g[0].size_m;
			memcpy(pos, e.g[0].pos, sizeof pos);
			EXPECT(e.g[0].kind == SOMVR_THK_MELEE && e.edge[0] == 0 && e.edge[1] == 0 && e.threats == 1,
			       "threat A frame %d: kind %u edges %g %g", i, e.g[0].kind, e.edge[0], e.edge[1]);
		}
		if (i == 30)
			EXPECT(e.nglow == 1 && NEAR(e.g[0].pos[0], 0, 1e-4) && NEAR(e.g[0].pos[1], 0.025, 1e-4) &&
				       NEAR(e.g[0].pos[2], -1.5, 1e-4) && NEAR(e.g[0].size_m, 2 * 1.5002 * tan(4.5 * 0.01745329), 1e-3),
			       "threat A's glow: %u at %g %g %g size %g (want 0 0.025 -1.5: R_Hand 1.5 m ahead, 2.5 cm up)", e.nglow,
			       e.g[0].pos[0], e.g[0].pos[1], e.g[0].pos[2], e.g[0].size_m);
		if (ts > 1.1f)
			EXPECT(e.nglow == 0, "threat A: still a glow %.3f s after the start", ts);
	}
	EXPECT(rising && NEAR(peak, 0.7, 1e-4) && tpeak > 0.55f && tpeak < 0.81f && window >= 60 && window <= 70 &&
		       th_log_count("THREAT warn orc=") == warn0 && !hap_take(SOMVR_HAND_L, &q) && !hap_take(SOMVR_HAND_R, &q) &&
		       g_thnode_calls >= 1 && th_tr_of(0) && th_tr_of(0)->node_st == 1,
	       "threat A: rising %d peak %g at %.3f s, window frames %d, warnings %d, node %d", rising, peak, tpeak, window,
	       th_log_count("THREAT warn orc=") - warn0, th_tr_of(0) ? th_tr_of(0)->node_st : -9);
	g_oc[0].attack = 0; /* its attack ends: the track too */
	iframe();
	EXPECT(!th_tr_of(0) && log_has("THREAT end orc=") && log_has("(the attack is over; impact 0.800 s)") &&
		       th_entry("A over").threats == 0,
	       "threat A: not ended");
	EXPECT(g_nbefore >= 65 && g_before == g_nbefore && g_nafter >= 10 && g_after == 0 && atomic_load(&g_vmo.block) == 0 &&
		       g_vm.gate_ticks[3] >= (uint64_t)g_nbefore && log_has("VRMENU gate on: an attack incoming") &&
		       log_has("VRMENU gate off: nothing"),
	       "threat A: the VR menu's gate %d of %d frames before the impact, %d of %d after it, now %#x", g_before, g_nbefore,
	       g_after, g_nafter, atomic_load(&g_vmo.block));
	g_vm.on = 0, g_vm.gate = 0, atomic_store(&g_vmo.block, 0);
	(void)size, (void)pos, (void)frames;
	th_attack(0, 0, 0, NULL); /* A no longer at Talion (else it is the engaged orc behind once the head turns) */
	/* 2. B (behind, left) starts the same strike: the left edge, announced on the left hand with the sound at B (LATE:
	 * 0.8 s of lead), the urgent pulse 0.35 s before the impact; the edge rises to the edge's strength at the impact;
	 * B is the nearest attacker now: the parry tick on the sword hand at its window's opening */
	th_drain_haptics();
	int reg0 = g_thsnd.reg, post0 = g_thsnd.post;
	th_attack(1, T, (uintptr_t)g_recs[1], TH_KT_INF);
	iframe();
	e = th_entry("B start");
	int lp = hap_take(SOMVR_HAND_L, &q);
	float lamp = q.amplitude;
	int rt = hap_take(SOMVR_HAND_R, &q) && NEAR(q.amplitude, 0.15, 1e-3);
	EXPECT(e.nglow == 0 && e.edge[1] == 0 && e.threats == 1 && lp && NEAR(lamp, 0.45, 1e-3) && rt &&
		       log_has("cue=edge side=L yaw=-153 pitch=") && log_has("lead=0.800 s level=0.00") && /* the body's direction */
		       log_has("haptic=announce sound LATE") &&
		       g_thsnd.reg == reg0 + 1 && g_thsnd.post == post0 + 1 && g_thsnd.wrap == (uintptr_t)g_snd_wrap &&
		       g_thsnd.obj == TH_SND_OBJ && !strcmp(g_thsnd.name, "fol_cmn_unsheathe") && NEAR(g_thsnd.at[0], 0, 1e-3) &&
		       NEAR(g_thsnd.at[1], 270, 1e-3) && NEAR(g_thsnd.at[2], 100, 1e-3) &&
		       log_has("THREAT sound 'fol_cmn_unsheathe' at 0.0,270.0,100.0"),
	       "threat B start: edges %g %g, left pulse %d (%g), sound reg %d post %d '%s' at %g %g %g", e.edge[0], e.edge[1], lp,
	       lamp, g_thsnd.reg - reg0, g_thsnd.post - post0, g_thsnd.name, g_thsnd.at[0], g_thsnd.at[1], g_thsnd.at[2]);
	int urgent = 0, edge_peak = 0;
	float emax = 0;
	for (int i = 0; i < 90 && th_tr_of(1); i++) {
		iframe();
		e = th_entry("B");
		emax = fmaxf(emax, e.edge[0]);
		edge_peak |= NEAR(e.edge[0], 0.8, 1e-3);
		if (hap_take(SOMVR_HAND_L, &q)) {
			float lead = 0.8f - (float)th_ts(1);
			urgent++;
			EXPECT(NEAR(q.amplitude, 0.8, 1e-3) && lead <= 0.35f && lead > 0.33f, "threat B urgent: amp %g lead %g",
			       q.amplitude, lead);
		}
		if (i == 20) { /* 3. turning around (the head 180 deg): B is in view now - the glow, no new announcement */
			g_rt_q = (sv_quat){ 0, 1, 0, 0 };
			iframe();
			e = th_entry("B in view");
			EXPECT(e.nglow == 1 && e.g[0].kind == SOMVR_THK_MELEE && e.edge[0] == 0 && e.edge[1] == 0 &&
				       log_has("cue=glow side=- yaw=+27"),
			       "threat B in view: glows %u edges %g %g", e.nglow, e.edge[0], e.edge[1]);
			g_rt_q = (sv_quat){ 0, 0, 0, 1 };
			iframe();
			e = th_entry("B out again");
			EXPECT(e.nglow == 0 && e.edge[0] > 0 && !hap_take(SOMVR_HAND_L, &q), "threat B out again: %u %g", e.nglow,
			       e.edge[0]);
		}
	}
	EXPECT(urgent == 1 && edge_peak && emax <= 0.8f + 1e-4f && g_thsnd.post == post0 + 1 && g_th.n.late >= 1 &&
		       log_has("haptic=urgent"),
	       "threat B: urgent pulses %d, edge peak %d (max %g), sounds %d", urgent, edge_peak, emax, g_thsnd.post - post0);
	/* 4. B engaged (Talion its target) without an attack, close behind: the left edge's low level, no pulse */
	g_oc[1].attack = 0;
	g_oobj[1].pos[0] = 50, g_oobj[1].pos[1] = 60, g_oobj[1].pos[2] = 200;
	iframe();
	e = th_entry("near");
	EXPECT((e.flags & SOMVR_THF_NEAR) && e.edge[0] > 0.2f && e.edge[0] < 0.25f && e.edge[1] == 0 && e.nglow == 0 &&
		       !hap_take(SOMVR_HAND_L, &q) && !th_tr_of(1),
	       "threat near: flags %#x edge %g", e.flags, e.edge[0]);
	g_oobj[1].pos[0] = -200, g_oobj[1].pos[2] = 0; /* 4 m away: nothing */
	iframe();
	e = th_entry("near, far");
	EXPECT(!(e.flags & SOMVR_THF_NEAR) && e.edge[0] == 0, "threat near at 4 m: flags %#x edge %g", e.flags, e.edge[0]);
	th_attack(1, 0, 0, NULL);
	/* 5. a projectile at the head from 20 m behind on the right (28 m/s, straight): the right edge and a right pulse;
	 * the head turned toward it: the glow on the arrow */
	th_drain_haptics();
	memset(g_proj, 0, sizeof g_proj);
	float pp[3] = { 1100, 200, 300 - 1732.05f }, pq[4] = { 0, sinf(-15 * 0.01745329f), 0, cosf(-15 * 0.01745329f) }, ps = 2800;
	memcpy(g_proj + TH_PJ_POS, pp, 12), memcpy(g_proj + TH_PJ_ROT, pq, 16), memcpy(g_proj + TH_PJ_SPEED, &ps, 4);
	memcpy(g_proj + TH_PJ_TARGET, &T, 8);
	int pj0 = g_pjv_calls;
	th_proj_tick();
	iframe();
	e = th_entry("projectile");
	int rp = hap_take(SOMVR_HAND_R, &q);
	EXPECT(g_pjv_calls == pj0 + 1 && (e.flags & SOMVR_THF_PROJ) && e.edge[1] > 0.6f * 0.8f && e.edge[0] == 0 &&
		       e.nglow == 0 && rp && NEAR(q.amplitude, 0.45, 1e-3) && !hap_take(SOMVR_HAND_L, &q) &&
		       log_has("THREAT proj p=") && log_has("(at Talion) cue=proj-edge side=R yaw=+150") && log_has("(head)"),
	       "threat projectile: calls %d flags %#x edges %g %g pulse %d", g_pjv_calls - pj0, e.flags, e.edge[0], e.edge[1], rp);
	g_rt_q = (sv_quat){ 0, sinf(-75 * 0.01745329f), 0, cosf(-75 * 0.01745329f) }; /* the head turned 150 deg right */
	th_proj_tick();
	iframe();
	e = th_entry("projectile in view");
	EXPECT(e.nglow == 1 && e.g[0].kind == SOMVR_THK_PROJ && e.edge[1] == 0 && !hap_take(SOMVR_HAND_R, &q),
	       "threat projectile in view: glows %u kind %u edge %g", e.nglow, e.g[0].kind, e.edge[1]);
	g_rt_q = (sv_quat){ 0, 0, 0, 1 };
	for (int i = 0; i < 12; i++) /* it stops being updated (it hit): gone after 0.1 s */
		iframe();
	e = th_entry("projectile gone");
	EXPECT(!(e.flags & SOMVR_THF_PROJ) && e.edge[1] == 0 && e.nglow == 0, "threat projectile gone: flags %#x", e.flags);
	/* 6. the pause menu: no cues, the attack's clock held */
	th_attack(0, T, (uintptr_t)g_recs[2], TH_KT_INF);
	for (int i = 0; i < 20; i++)
		iframe();
	double t0 = th_tr_of(0) ? th_tr_of(0)->t0 : 0;
	g_pz[0x34] = 1;
	for (int i = 0; i < 30; i++)
		iframe();
	e = th_entry("paused");
	EXPECT(e.nglow == 0 && e.threats == 0 && th_tr_of(0) && NEAR(th_tr_of(0)->t0 - t0, 30.0 / 90, 1e-6),
	       "threat pause: glows %u, the clock moved %g s", e.nglow, th_tr_of(0) ? th_tr_of(0)->t0 - t0 : -1);
	g_pz[0x34] = 0;
	iframe();
	EXPECT(th_entry("unpaused").nglow == 1, "threat after the pause: no glow");
	/* 7. flat frames (no XR views; the fallback's third person is the same, re-xr-pipeline.md 19.2): nothing published,
	 * the track kept with its own clock (A's timer: 0.011 s a frame); back in stereo the same track (no new start) */
	struct th_track *k7 = th_tr_of(0);
	double t07 = k7 ? k7->t0 : 0;
	int ff7 = th_log_count("(a flat frame;"); /* df_tests' long stretch ended one before */
	uint64_t st7 = g_th.n.starts, rs7 = g_th.n.resumed, rr7 = g_th.n.restarted;
	atomic_store(&g_sh.xr.state, SOMVR_XRS_NONE);
	uint64_t fl0 = g_th.n.flat;
	for (int i = 0; i < 9; i++)
		g_cb_test_t += 1.0 / 90, frame();
	k7 = th_tr_of(0);
	EXPECT(k7 && k7->dormant && g_th.n.flat == fl0 + 9 && NEAR(k7->t0 - t07, 9 * (1.0 / 90 - 0.011), 1e-6) &&
		       th_log_count("(a flat frame;") == ff7,
	       "threat flat: track %p dormant %d, its clock moved t0 by %g (want %g)", (void *)k7, k7 ? k7->dormant : -1,
	       k7 ? k7->t0 - t07 : -1, 9 * (1.0 / 90 - 0.011));
	runtime_up();
	iframe();
	EXPECT(th_tr_of(0) == k7 && k7 && !k7->dormant && g_th.n.starts == st7 && g_th.n.resumed == rs7 + 1 &&
		       th_entry("resumed").nglow == 1,
	       "threat flat: resumed %llu, starts %llu, the same track %d", (unsigned long long)(g_th.n.resumed - rs7),
	       (unsigned long long)(g_th.n.starts - st7), th_tr_of(0) == k7);
	/* ... 2 s flat: kept, but its attack (impact 0.8 s) is over by its own clock while the pointer is still set: the
	 * same record again or a start not seen - a new track from now */
	atomic_store(&g_sh.xr.state, SOMVR_XRS_NONE);
	for (int i = 0; i < 180; i++)
		g_cb_test_t += 1.0 / 90, frame();
	EXPECT(th_tr_of(0) && th_tr_of(0)->dormant && th_ts(0) > 1.9, "threat flat 2 s: track %p ts %g", (void *)th_tr_of(0),
	       th_ts(0));
	runtime_up();
	iframe();
	EXPECT(th_tr_of(0) && g_th.n.restarted == rr7 + 1 && g_th.n.starts == st7 + 1 && th_ts(0) < 0.02 &&
		       log_has("(a flat stretch outlived it;"),
	       "threat flat 2 s: restarted %llu starts %llu ts %g", (unsigned long long)(g_th.n.restarted - rr7),
	       (unsigned long long)(g_th.n.starts - st7), th_ts(0));
	/* 8. the attack's clock is the orc's own timer: frozen (our hit-stop's 0 ms frames) it holds, at a third (the game's
	 * slow motion) it runs at a third; back to the timer's 0.011 s */
	double ts8 = th_ts(0);
	uint64_t oc8 = g_th.n.orc_clock;
	g_orc_timer.frame_s = 0;
	for (int i = 0; i < 20; i++)
		iframe();
	double ts8b = th_ts(0);
	g_orc_timer.frame_s = 0.011f / 3;
	for (int i = 0; i < 30; i++)
		iframe();
	double ts8c = th_ts(0);
	g_orc_timer.frame_s = 0.011f;
	EXPECT(NEAR(ts8b, ts8, 1e-6) && NEAR(ts8c - ts8b, 30 * (double)(0.011f / 3), 1e-5) && g_th.n.orc_clock >= oc8 + 50,
	       "threat clock: frozen %g -> %g, a third: +%g (want %g), clocked %llu", ts8, ts8b, ts8c - ts8b,
	       30 * (double)(0.011f / 3), (unsigned long long)(g_th.n.orc_clock - oc8));
	/* ... and a flat stretch over TH_FLAT_KEEP_S (4 s) drops the tracks */
	atomic_store(&g_sh.xr.state, SOMVR_XRS_NONE);
	for (int i = 0; i < 380; i++)
		g_cb_test_t += 1.0 / 90, frame();
	EXPECT(!th_tr_of(0) && th_log_count("(a flat frame;") == ff7 + 1, "threat flat 4.2 s: track %p", (void *)th_tr_of(0));
	runtime_up();
	th_attack(0, 0, 0, NULL);
	/* 8. at start: a key-time proof that does not match, SOMVR_COMBAT off, a stereo-less layout: off, nothing patched */
	struct site_spec bad[THS_N];
	memcpy(bad, g_ths, sizeof bad);
	bad[THS_KEYTABLE].ctx_hex = "90 90 90 90 90";
	int bad_flag = 0;
	g_cb.mode = CB_FULL;
	th_start(bad, &bad_flag);
	EXPECT(!g_th.on && log_has("WARN SOMVR_THREAT=1: a key-time proof site did not match") &&
		       log_has("THREAT site TH_KEYTABLE") && g_vtpj[13] == (void *)th_pj_vf13,
	       "threat bad site: on %d", g_th.on);
	g_cb.mode = CB_OFF;
	th_start(g_ths, &bad_flag);
	EXPECT(!g_th.on && log_has("WARN SOMVR_THREAT=1 needs SOMVR_COMBAT (off)"), "threat without combat: on %d", g_th.on);
	g_th_sites = g_ths;
	printf("threat: start %d/%d, A's glow at its R_Hand peaking at %.2f (%.3f s), B's edge announced (pulse, sound) and "
	       "urgent, near, projectile edge/glow, pause, flat frames kept/restarted/dropped, the orc's clock, sites\n", starts0,
	       warn0, peak, tpeak);
}

/* ---- SOMVR_BODYMOMENTS=1 (switched on at start with stand-in sites, then off until bm_tests: white box). On the
 * fallback's stand-in world (fb_world: the Player, PlayerBody's tags, the sync action, the cinematic manager) plus the
 * player's server Combat g_pcs (its attack +0x4d0, its state +0x268, its dead byte), our records with names, and the
 * game camera's heading (g_game_quat). The clock is g_fb_test_t (90 Hz). ------------------------------------------- */
static struct site_spec g_bms[BMS_N];
static char g_bmhex[BMS_N][256];
static void bm_stand_in_sites(struct site_spec *s)
{
	for (int i = 0; i < BMS_N; i++) {
		hex_of((const unsigned char *)st_fb_code, 8, g_bmhex[i], 128);
		hex_of((const unsigned char *)st_fb_code, 4, g_bmhex[i] + 128, 128);
		s[i] = (struct site_spec){ g_bm_sites_game[i].name, K_ENTRY, (uintptr_t)st_fb_code, (uintptr_t)st_fb_code,
					   g_bmhex[i], (uintptr_t)st_fb_code, g_bmhex[i] + 128 };
	}
}
static _Alignas(16) char g_bmrec[6][0x30];
static const char *const g_bmrec_names[6] = { "PC_Bip_Sync_Cntr_F", "PC_Recoil_Heavy_Knockdown_B", "PC_Recoil_Light_R2L_F",
					      "Knockdown", "PC_RecoilAny_PreDeath", "Gnd" };
static uintptr_t bm_rec(int i) { return (uintptr_t)g_bmrec[i]; }
static void bm_set_attack(uintptr_t r) { g_pcs.attack = r; }
static void bm_set_state(uintptr_t r) { memcpy((char *)&g_pcs + BM_C_STATE, &r, 8); }
static void bm_set_sync(uintptr_t r) { memcpy(g_fbsync + FB_SYNC_REC, &r, 8); }
static void bm_tag1(uintptr_t r) /* one current state tag (set 1), or Gnd */
{
	char *mgr = g_fbbody + FB_BODY_TAGS, *set = mgr + FB_TAGS_SET0 + FB_TAGS_SET;
	mgr[FB_TAGS_CUR] = 1;
	memcpy(set, &r, 8);
	uint32_t c = 1;
	memcpy(set + FB_TAGS_COUNT, &c, 4);
}
static void bm_restart(const struct site_spec *s)
{
	int bad = 0;
	bm_start(s, &bad);
}
static void bm_frame1(void)
{
	g_fb_test_t += 1.0 / 90;
	frame();
}
static void bm_move(float dx, float dy, float dz) { g_pobj.pos[0] += dx, g_pobj.pos[1] += dy, g_pobj.pos[2] += dz; }
static float bm_heading(void) /* eye 0's heading in the game (the view's yaw) */
{
	return sv_heading(sv_qnormalize(sv_quat_of(g_h4_view[0].quat)));
}
static void bm_camera_yaw(float a) /* the game camera turned to heading a (sv_qyaw's convention) */
{
	sv_quat q = sv_qyaw(a);
	g_game_quat[0] = q.x, g_game_quat[1] = q.y, g_game_quat[2] = q.z, g_game_quat[3] = q.w;
}

static void bm_tests(void)
{
	g_views_fake = 0, g_layout = g_rigcfg.layout = SOMVR_LAYOUT_A;
	runtime_up();
	g_rigcfg.base = SOMVR_BASE_HEAD, g_rigcfg.yaw = SOMVR_YAW_CAMERA, g_node_ok = 1, g_follow.on = 0, g_audio = 0;
	g_cb.mode = CB_OFF, g_hide.on = 0, g_hudq.on = 0, g_nodexf_node = NULL, g_head_up = 72.5f, g_lo.on = g_lo.anchor = 0;
	g_rm[0] = g_rm[1] = g_rm[2] = 0;
	g_rt_head[0] = g_rt_head[1] = g_rt_head[2] = 0, g_rt_q = (sv_quat){ 0, 0, 0, 1 };
	g_fb_test_t = 3000;
	cb_world(); /* the player's server Combat g_pcs (its vtable the combat one) in PlayerCombat+0xa08 */
	fb_world();
	for (int i = 0; i < 6; i++) {
		memset(g_bmrec[i], 0, sizeof g_bmrec[i]);
		g_bmrec[i][1] = 2;
		uintptr_t n = (uintptr_t)g_bmrec_names[i];
		memcpy(g_bmrec[i] + 0x20, &n, 8);
	}
	g_pcs.sync[0] = g_fbsync;
	bm_set_attack(0), bm_set_state(0), g_pcs.dead = 0;
	fb_env(NULL, NULL, NULL, NULL, NULL);
	setenv("SOMVR_BODYMOMENTS", "1", 1);
	unsetenv("SOMVR_BM_POLICY"), unsetenv("SOMVR_BM_NAMES"), unsetenv("SOMVR_BM_YAW"), unsetenv("SOMVR_BM_DIM");
	unsetenv("SOMVR_BM_SEAT"), unsetenv("SOMVR_BM_BLINK_MS"), unsetenv("SOMVR_BM_MS");
	fb_restart(g_fbs);
	bm_restart(g_bms);
	EXPECT(g_bm.on && g_fb.on && g_bm.sites_ok == (1u << BMS_N) - 1 && g_bm.names.n == 10 &&
		       g_bm.yaw_mode == SOMVR_BMY_PLAYER && g_bm.pol.act[SOMVR_FB_SYNC] == SOMVR_BMA_HOLD,
	       "bodymoments: start: on %d sites %#x names %d", g_bm.on, g_bm.sites_ok, g_bm.names.n);
	EXPECT(log_has("BMSITE BM_DEAD ok") && log_has("CONFIG bodymoments=1 (sites 3 of 3): policy cine=window,scripted=first") &&
		       log_has("knockdown=atk:PC_Recoil*Knockdown*") && log_has("yaw player"), "bodymoments: no CONFIG/BMSITE lines");
	atomic_fetch_add(&g_req_recenter, 1);
	bm_frame1();
	/* 1. calm: first person on the head node, stereo, no fade */
	int ok = 1;
	for (int i = 0; i < 30; i++)
		bm_frame1(), ok &= g_h4_calls == 2 && fb_level() == 0 && fb_at(eye_mid(), fb_head(), 1e-3) && !g_bm.o.frozen;
	EXPECT(ok && g_fr.head_ok, "bodymoments calm: eyes %g %g %g", eye_mid().x, eye_mid().y, eye_mid().z);
	/* 2. a sync (a counter): held from its first frame while the body moves 1.2 m; then the blink, the cut on black */
	sv_vec3 e0 = eye_mid();
	bm_set_sync(bm_rec(0));
	bm_move(2, 0, 0);
	bm_frame1();
	EXPECT(g_bm.o.frozen && fb_at(eye_mid(), e0, 1e-3) && g_h4_calls == 2 && fb_level() == 0,
	       "bodymoments sync: the first frame moved the view (%g %g %g)", eye_mid().x, eye_mid().y, eye_mid().z);
	EXPECT(fb_after_body(0x77) == 0x77 && !bm_head_away(), "bodymoments sync: the body under the view lost the hands");
	ok = 1;
	for (int i = 0; i < 59; i++)
		bm_move(2, 0, 0), bm_frame1(), ok &= fb_at(eye_mid(), e0, 1e-3) && g_bm.o.frozen;
	EXPECT(ok && NEAR(g_pobj.pos[0] - (e0.x), 120, 1e-2), "bodymoments sync: the view moved with the body");
	EXPECT(log_has("cond - -> sync (+sync --) attack '' state '' sync 'PC_Bip_Sync_Cntr_F'") &&
		       log_has("act first -> hold by sync, window 0 -> 0"), "bodymoments sync: log lines");
	bm_set_sync(0);
	uint32_t lv[60];
	int cut_at = -1, at_head = 0, held_before = 1;
	for (int i = 0; i < 60; i++) {
		bm_frame1();
		lv[i] = fb_level();
		if (cut_at < 0 && fb_at(eye_mid(), fb_head(), 1e-3))
			cut_at = i;
		if (cut_at < 0)
			held_before &= fb_at(eye_mid(), e0, 1e-3);
		at_head = fb_at(eye_mid(), fb_head(), 1e-3);
	}
	EXPECT(cut_at > 20 && cut_at < 32 && lv[cut_at] == 255 && held_before && lv[cut_at - 1] > 0 && lv[cut_at - 1] < 255 &&
		       at_head && lv[59] == 0 && g_bm.s.blinks == 1,
	       "bodymoments sync end: the cut at frame %d (level %u, before %u), held before %d, at the head %d, blinks %llu",
	       cut_at, cut_at >= 0 ? lv[cut_at] : 0, cut_at > 0 ? lv[cut_at - 1] : 0, held_before, at_head,
	       (unsigned long long)g_bm.s.blinks);
	EXPECT(log_has("released by a blink (120.0 cm from the held point)") && log_has("blink out -> black"),
	       "bodymoments sync end: log lines");
	/* 2b. a record that outlives Talion's part (boot-check-20261024-020824: the stun counter's record stayed 87 s): his
	 * attack the counter, then a recoil while the record stays: held 1 s more, then stale (the blink back to him) */
	bm_set_attack(bm_rec(0)), bm_set_sync(bm_rec(0));
	for (int i = 0; i < 30; i++)
		bm_move(1, 0, 0), bm_frame1();
	EXPECT(g_bm.o.frozen, "bodymoments stale sync: held while the counter runs");
	bm_set_attack(bm_rec(2));
	int held_n = 0;
	for (int i = 0; i < 150; i++)
		bm_move(0.5f, 0, 0), bm_frame1(), held_n += g_bm.o.frozen;
	EXPECT(held_n >= 88 && held_n <= 120 && !g_bm.o.frozen && fb_at(eye_mid(), fb_head(), 1e-3) && g_fb_sa.stale &&
		       log_has("FALLBACK sync record") && log_has("'PC_Bip_Sync_Cntr_F' stale: Talion's attack 'PC_Recoil_Light_R2L_F' (another)"),
	       "bodymoments stale sync: held %d frames after his attack changed, then back at the head", held_n);
	bm_set_sync(0), bm_set_attack(0);
	for (int i = 0; i < 60; i++)
		bm_frame1();
	EXPECT(!g_fb_sa.stale && !g_bm.o.frozen, "bodymoments stale sync: the record cleared");
	/* 3. a knockdown by Talion's attack record: held, dimmed to 0.5, the arms to the animation, no blade */
	bm_set_attack(bm_rec(2)); /* a light recoil: nothing */
	for (int i = 0; i < 10; i++)
		bm_frame1();
	EXPECT(!g_bm.o.frozen && fb_level() == 0 && fb_after_body(0x77) == 0x77, "bodymoments: a light recoil held or dimmed");
	e0 = eye_mid();
	bm_set_attack(bm_rec(1));
	for (int i = 0; i < 30; i++)
		bm_move(0, -0.5f, 0.5f), bm_frame1(); /* sliding down and back 21 cm: an ease when he is up */
	g_arm.have_rig = 1;
	somvr_rec rr;
	EXPECT(somvr_rec_read(&g_sh, g_frame_no, &rr) && (rr.flags & SOMVR_REC_FADE_RED), "bodymoments: the dim not red");
	EXPECT(g_bm.o.act == SOMVR_BMA_DIM && fb_level() == 128 && fb_at(eye_mid(), e0, 1e-3) && fb_after_body(0x77) == 0 &&
		       g_arm.have_rig == 0 && log_has("attack 'PC_Recoil_Heavy_Knockdown_B' (") && log_has("is in class knockdown"),
	       "bodymoments knockdown: act %d level %u", g_bm.o.act, fb_level());
	bm_set_attack(bm_rec(2));
	int n = 0;
	while (g_bm.o.act != SOMVR_BMA_FIRST && n < 60)
		bm_frame1(), n++;
	for (int i = 0; i < 60; i++)
		bm_frame1();
	EXPECT(n >= 21 && n <= 24 && fb_level() == 0 && g_bm.s.eases == 1 && fb_at(eye_mid(), fb_head(), 1e-3) &&
		       fb_after_body(0x77) == 0x77, "bodymoments knockdown end: up after %d, eases %llu", n,
	       (unsigned long long)g_bm.s.eases);
	/* ... by the combat state, by a tag */
	bm_set_state(bm_rec(3));
	bm_frame1();
	EXPECT(g_bm.o.act == SOMVR_BMA_DIM, "bodymoments: the knockdown state");
	bm_set_state(0), bm_tag1(bm_rec(3));
	for (int i = 0; i < 30; i++)
		bm_frame1();
	EXPECT(g_bm.o.act == SOMVR_BMA_DIM && (g_bm.last_conds & SOMVR_BMC_BIT(SOMVR_BMC_KNOCKDOWN)), "bodymoments: the tag");
	bm_tag1(bm_rec(5));
	for (int i = 0; i < 90; i++)
		bm_frame1();
	EXPECT(g_bm.o.act == SOMVR_BMA_FIRST && fb_level() == 0, "bodymoments: the knockdown gone");
	/* 4. the death: by the dead flag (grey 0.8 within 1 s), then the Nemesis screen (a presentation) in the window */
	e0 = eye_mid();
	g_pcs.dead = 1;
	for (int i = 0; i < 95; i++)
		bm_frame1();
	EXPECT(somvr_rec_read(&g_sh, g_frame_no, &rr) && !(rr.flags & SOMVR_REC_FADE_RED), "bodymoments: the grey-out red");
	EXPECT(g_bm.o.act == SOMVR_BMA_GREY && fb_level() == 204 && fb_at(eye_mid(), e0, 1e-3) && fb_after_body(0x77) == 0,
	       "bodymoments death: act %d level %u", g_bm.o.act, fb_level());
	uintptr_t pres = 0x55;
	memcpy(g_fbpres + FB_PRES_CUR, &pres, 8);
	n = 0;
	while (g_h4_calls != 1 && n < 60)
		bm_frame1(), n++;
	EXPECT(n >= 9 && n <= 11 && g_bm.o.window, "bodymoments: the window after %d frames", n);
	check_mono("bodymoments window");
	bm_move(3000, 0, -2000); /* the respawn far away */
	g_pcs.dead = 0, pres = 0;
	memcpy(g_fbpres + FB_PRES_CUR, &pres, 8);
	n = 0;
	while (!(g_h4_calls == 2 && g_fr.head_ok) && n < 200) /* the teleport: one frame on the camera base (the head read) */
		bm_frame1(), n++;
	EXPECT(n >= 85 && n <= 96 && fb_at(eye_mid(), fb_head(), 1e-3) && !g_bm.o.frozen && fb_level() == 0 && g_bm.s.cuts >= 2,
	       "bodymoments: back from the window after %d frames at the head (%g %g %g), cuts %llu", n, eye_mid().x, eye_mid().y,
	       eye_mid().z, (unsigned long long)g_bm.s.cuts);
	EXPECT(log_has("window 0 -> 1 by pres (flat frames: the menu quad)") && log_has("released by a cut"),
	       "bodymoments window: log lines");
	/* 5. a cinematic in the window too; the trigger words: first = no window, third = the window, auto */
	uintptr_t cam1 = 0x1234;
	memcpy(g_fbcine + FB_CINE_CAM, &cam1, 8);
	n = 0;
	while (g_h4_calls != 1 && n < 200) /* at least 1 s after the last window change */
		bm_frame1(), n++;
	EXPECT(n >= 85 && n <= 95 && g_bm.o.window && g_bm.o.win_cond == SOMVR_FB_CINE,
	       "bodymoments: a cinematic in the window after %d frames", n);
	EXPECT(!strcmp(fb_word("first"), "fallback first"), "bodymoments: the word first");
	n = 0;
	while (g_h4_calls != 2 && n < 200)
		bm_frame1(), n++;
	EXPECT(n >= 85 && n <= 95 && !g_bm.o.window && log_has("forced: auto -> first (no window)"),
	       "bodymoments: first: stereo after %d frames", n);
	cam1 = 0, memcpy(g_fbcine + FB_CINE_CAM, &cam1, 8);
	EXPECT(!strcmp(fb_word("third"), "fallback third"), "bodymoments: the word third");
	n = 0;
	while (g_h4_calls != 1 && n < 200)
		bm_frame1(), n++;
	EXPECT(n >= 85 && n <= 95 && g_bm.o.window && g_bm.o.win_cond == SOMVR_FB_FORCED, "bodymoments: third: %d", n);
	EXPECT(!strcmp(fb_word("auto"), "fallback auto"), "bodymoments: the word auto");
	n = 0;
	while (g_h4_calls != 2 && n < 200)
		bm_frame1(), n++;
	EXPECT(n >= 85 && n <= 95 && !g_bm.o.window && fb_at(eye_mid(), fb_head(), 1e-3), "bodymoments: auto: %d", n);
	/* 6. the traversal (FSM 9) and the kill cam (FinalBlow) stay first person; an attach is the window, not while our
	 * climb holds */
	g_fbP[FB_P_FSM] = 9;
	int td[1] = { 4 };
	fb_tagset(1, td);
	ok = 1;
	for (int i = 0; i < 60; i++)
		bm_move(1, 1, 0), bm_frame1(), ok &= g_h4_calls == 2 && !g_bm.o.frozen && fb_at(eye_mid(), fb_head(), 1e-3);
	EXPECT(ok && (g_bm.last_conds & SOMVR_BMC_BIT(SOMVR_FB_CLIMB)) && (g_bm.last_conds & SOMVR_BMC_BIT(SOMVR_FB_TAG)),
	       "bodymoments: the traversal / the kill cam left first person (conds %#x)", g_bm.last_conds);
	g_fbP[FB_P_FSM] = 6, fb_tagset(2, FB_CALM_TAGS);
	int cl_on = g_cl.on, cl_act = g_cl.s.active, cl_tid = g_cl.tid;
	g_cl.on = 1, g_cl.s.active = 1, g_cl.tid = -7; /* white box: the climb holds (its own code idle: another thread) */
	((char *)&g_ppm)[FB_PM_ATTACHED] = 1;
	ok = 1;
	for (int i = 0; i < 40; i++)
		bm_frame1(), ok &= g_h4_calls == 2;
	EXPECT(ok && !(g_bm.last_conds & SOMVR_BMC_BIT(SOMVR_FB_ATTACH)), "bodymoments: our climb's attach went to the window");
	g_cl.on = cl_on, g_cl.s.active = cl_act, g_cl.tid = cl_tid;
	n = 0;
	while (g_h4_calls != 1 && n < 200)
		bm_frame1(), n++;
	EXPECT(n > 0 && n < 200 && g_bm.o.win_cond == SOMVR_FB_ATTACH, "bodymoments: an attach is not the window");
	((char *)&g_ppm)[FB_PM_ATTACHED] = 0;
	n = 0;
	while (g_h4_calls != 2 && n < 200)
		bm_frame1(), n++;
	EXPECT(n > 0 && n < 200, "bodymoments: the attach's window stayed");
	/* 7. the yaw: the game camera turns 40 deg (a combat or kill camera): the view does not; a snap turn does */
	for (int i = 0; i < 10; i++)
		bm_frame1();
	float h0 = bm_heading();
	for (int i = 1; i <= 40; i++)
		bm_camera_yaw(0.01745329f * (float)i), bm_frame1();
	EXPECT(NEAR(bm_heading(), h0, 1e-4) && g_bm.s.yaw_absorbed > 0.69f, "bodymoments yaw: the camera turned the view "
	       "(%g -> %g)", h0, bm_heading());
	atomic_fetch_add(&g_req_snap, 1);
	bm_frame1();
	float dh = bm_heading() - h0;
	EXPECT(NEAR(fabsf(dh), g_rigcfg.snap_rad, 1e-3), "bodymoments yaw: the snap %g (want %g)", dh, g_rigcfg.snap_rad);
	/* a recentre faces the game camera's heading again (the camera at 40 deg, plus the snap) */
	atomic_fetch_add(&g_req_recenter, 1);
	bm_frame1();
	float hr = bm_heading();
	bm_frame1();
	EXPECT(fabsf(g_bm.s.yaw_off) < 1e-6f && NEAR(bm_heading(), hr, 1e-4) && !NEAR(hr, h0 + dh, 1e-2),
	       "bodymoments yaw: the recentre kept the offset (%g, heading %g)", g_bm.s.yaw_off, hr);
	/* a held moment with the camera swinging: neither moves */
	e0 = eye_mid();
	float h1 = bm_heading();
	bm_set_sync(bm_rec(0));
	ok = 1;
	for (int i = 1; i <= 30; i++)
		bm_camera_yaw(0.7f + 0.05f * (float)i), bm_move(0.8f, 0, 0.8f), bm_frame1(),
		ok &= fb_at(eye_mid(), e0, 1e-3) && NEAR(bm_heading(), h1, 1e-4);
	EXPECT(ok, "bodymoments: a sync's camera moved or turned the view");
	/* the head away from the held view: SOMVR_HIDE_HEAD=bone leaves it */
	EXPECT(bm_head_away() && fb_after_body(0x77) == 0, "bodymoments: the head 34 cm away not shown, or the hands kept");
	bm_set_sync(0);
	for (int i = 0; i < 90; i++)
		bm_frame1();
	EXPECT(!bm_head_away() && !g_bm.o.frozen && g_bm.s.blinks == 2 && g_bm.s.eases == 3, /* (2b's stale release: a blink) */
	       "bodymoments: the head away when free; blinks %llu eases %llu", (unsigned long long)g_bm.s.blinks,
	       (unsigned long long)g_bm.s.eases);
	/* SOMVR_BM_YAW=camera: the camera turns it (as without body moments) */
	setenv("SOMVR_BM_YAW", "camera", 1);
	bm_restart(g_bms);
	bm_frame1();
	h0 = bm_heading();
	bm_camera_yaw(0.3f), bm_frame1();
	EXPECT(g_bm.yaw_mode == SOMVR_BMY_CAMERA && !NEAR(bm_heading(), h0, 1e-2), "bodymoments yaw=camera: %g -> %g", h0,
	       bm_heading());
	unsetenv("SOMVR_BM_YAW");
	bm_camera_yaw(0);
	/* 8. the policy and the names from the environment; bad items warned */
	bm_set_attack(0), bm_set_sync(0);
	for (int i = 0; i < 90; i++)
		bm_frame1();
	setenv("SOMVR_BM_POLICY", "sync=first,bogus=hold", 1);
	setenv("SOMVR_BM_NAMES", "+hold=atk:PC_Recoil_Light*,death=nowhere:X", 1);
	setenv("SOMVR_BM_SEAT", "0,50", 1);
	bm_restart(g_bms);
	EXPECT(g_bm.on && g_bm.pol.act[SOMVR_FB_SYNC] == SOMVR_BMA_FIRST && g_bm.names.n == 11 && g_bm.cfg.seat_cm == 0 &&
		       log_has("WARN SOMVR_BM_POLICY=sync=first,bogus=hold") && log_has("WARN SOMVR_BM_NAMES=+hold=atk:"),
	       "bodymoments env: sync %d names %d seat %g", g_bm.pol.act[SOMVR_FB_SYNC], g_bm.names.n, g_bm.cfg.seat_cm);
	bm_set_sync(bm_rec(0));
	for (int i = 0; i < 10; i++)
		bm_frame1();
	EXPECT(!g_bm.o.frozen, "bodymoments: sync=first held");
	bm_set_sync(0), bm_set_attack(bm_rec(2));
	bm_frame1();
	EXPECT(g_bm.o.frozen && g_bm.o.act == SOMVR_BMA_HOLD, "bodymoments: a hold name");
	bm_set_attack(0);
	for (int i = 0; i < 60; i++)
		bm_frame1();
	EXPECT(!g_bm.o.frozen && g_bm.s.blinks == 1 && g_bm.s.eases == 0, "bodymoments: seat 0: a blink in place");
	unsetenv("SOMVR_BM_POLICY"), unsetenv("SOMVR_BM_NAMES"), unsetenv("SOMVR_BM_SEAT");
	/* 9. a mismatched BM_CURATK: the attack is not read (the dead flag still is) */
	struct site_spec bad[BMS_N];
	memcpy(bad, g_bms, sizeof bad);
	bad[BMS_CURATK].ctx_hex = "00 11 22 33 44 55 66 77";
	bm_restart(bad);
	bm_set_attack(bm_rec(1));
	for (int i = 0; i < 10; i++)
		bm_frame1();
	EXPECT(g_bm.on && !(g_bm.sites_ok & BMSB(CURATK)) && !g_bm.o.frozen && log_has("BMSITE BM_CURATK MISMATCH") &&
		       log_has("WARN SOMVR_BODYMOMENTS: a BMSITE did not match"), "bodymoments: a mismatched site");
	g_pcs.dead = 1;
	bm_frame1();
	EXPECT(g_bm.o.act == SOMVR_BMA_GREY, "bodymoments: the dead flag without BM_CURATK");
	g_pcs.dead = 0, bm_set_attack(0);
	/* 10. the END line; without the fallback: off; off: the fallback's own third person again */
	bm_end_log();
	EXPECT(log_has("BODYMOMENTS END h4=") && log_has("frames per action: first="), "bodymoments: no END line");
	g_fb.on = 0;
	bm_restart(g_bms);
	EXPECT(!g_bm.on && log_has("WARN SOMVR_BODYMOMENTS=1 needs SOMVR_FALLBACK=auto"), "bodymoments: on without the fallback");
	unsetenv("SOMVR_BODYMOMENTS");
	fb_restart(g_fbs);
	bm_restart(g_bms);
	cam1 = 0x1234, memcpy(g_fbcine + FB_CINE_CAM, &cam1, 8);
	n = fb_until(1, 120);
	EXPECT(!g_bm.on && g_fb.on && n > 0 && g_h4_calls == 2, "bodymoments off: the fallback's third person (%d)", n);
	cam1 = 0, memcpy(g_fbcine + FB_CINE_CAM, &cam1, 8);
	printf("bodymoments  first person calm; a sync held from its first frame (1.2 m away: the blink, the cut on black); a "
	       "knockdown by attack, state and tag (dim 0.5, the arms the animation's, an ease back); the death (grey 0.8), the "
	       "Nemesis screen and a cinematic in the window (flat), the respawn by a cut; the words; the traversal and the kill "
	       "cam first person, our climb's attach; the camera's turns absorbed, the snap kept; env, a mismatched site, off\n");
	unsetenv("SOMVR_FALLBACK");
	g_pl = &g_player, g_ppm.owner = &g_player, g_pcs.sync[0] = NULL;
	memset(g_shell + FB_SH_CINE, 0, 8);
	g_fb.on = 0, g_bm.on = 0, g_fb_test_t = -1;
}

/* ---- SOMVR_SLOTS=1 (slots_tests; switched on at start with stand-in sites, then off until slots_tests: white box). The
 * stand-ins: the model-string handler's tail jump into FUN_023116f0 (c_slkey: a real E9, the K_JMP site), FUN_023116f0
 * (st_sl_sheathe: the sheathed flag +0xa3, 0 = "already so"), the re-attach FUN_0231e2e0 (st_sl_reattach: records the
 * first entry's InactiveNode), Inventory_ActivateSlot FUN_02315ca0 (st_sl_activate: the active slot), the re-attach's
 * AttachChild call (c_slnode -> t_slnode, the read-only K_CALL), two read-only K_ENTRY proofs, the three Wwise calls;
 * the player's inventory of bow_tests' bw_setup with Urfael in slot 0 and Acharn in slot 1 (their owner, a record, two
 * model entries with P7's node hashes). ------------------------------------------------------------------------------ */
static struct {
	int sheathes, reattaches, activates, regs, posts, keys;
	uintptr_t inv, item;
	uint32_t sh, act_slot, reatt_node;
	char snd[64];
	float snd_at[3];
} g_slt;
uint32_t c_slkey(uintptr_t inv, uintptr_t item, uint32_t sheathe); /* FUN_02315f40's tail: jmp FUN_023116f0 */
__attribute__((noipa, used)) uint32_t st_sl_sheathe(uintptr_t inv, uintptr_t item, uint32_t sheathe);
__asm__(".text\n.p2align 4\n.globl c_slkey\n.type c_slkey,@function\nc_slkey:\n  nop\n  .byte 0xe9\n"
	"  .long st_sl_sheathe - . - 4\n.size c_slkey, .-c_slkey\n");
__attribute__((noipa, used)) uint32_t st_sl_sheathe(uintptr_t inv, uintptr_t item, uint32_t sheathe)
{
	g_slt.sheathes++, g_slt.inv = inv, g_slt.item = item, g_slt.sh = sheathe & 0xff;
	unsigned char *f = (unsigned char *)item + SL_IT_SHEATHED;
	if ((*f != 0) == ((sheathe & 0xff) != 0))
		return 0;
	*f = (unsigned char)(sheathe & 0xff);
	return 1;
}
__attribute__((noipa)) void st_sl_reattach(uintptr_t item)
{
	uintptr_t b;
	g_slt.reattaches++, g_slt.item = item;
	memcpy(&b, (const char *)item + SL_IT_ENTRIES, 8);
	memcpy(&g_slt.reatt_node, (const char *)b + SL_IE_INACT, 4);
}
__attribute__((noipa)) uint64_t st_sl_activate(uintptr_t inv, uint32_t slot)
{
	g_slt.activates++, g_slt.act_slot = slot;
	memcpy((char *)inv + SL_INV_SLOT, &slot, 4);
	return 1;
}
__attribute__((noipa)) uint64_t st_sl_sndreg(uintptr_t w, uint32_t obj, const char *name)
{
	(void)w, (void)obj, (void)name;
	return (uint64_t)++g_slt.regs;
}
__attribute__((noipa)) uint64_t st_sl_sndpos(uintptr_t w, uint32_t obj, const float *pos, const float *front)
{
	(void)w, (void)obj, (void)front;
	memcpy(g_slt.snd_at, pos, sizeof g_slt.snd_at);
	return 1;
}
__attribute__((noipa)) uint32_t st_sl_sndpost(uintptr_t w, uint32_t obj, const char *name)
{
	(void)w;
	g_slt.posts++;
	snprintf(g_slt.snd, sizeof g_slt.snd, "%s", obj == SL_SND_OBJ ? name : "wrong object");
	return 77;
}
__attribute__((noipa)) uint64_t t_slnode(uint64_t a) { return a + 1; }
__attribute__((noipa)) uint64_t c_slnode(uint64_t a) { return t_slnode(a) * 3; }
static char g_slhex[SLS_N][256];
static struct site_spec g_sls[SLS_N];
static void sl_entry_site(void *fn, int i)
{
	hex_of(fn, 8, g_slhex[i], 128);
	hex_of(fn, 4, g_slhex[i] + 128, 128);
	g_sls[i] = (struct site_spec){ g_sl_sites_game[i].name, K_ENTRY, (uintptr_t)fn, (uintptr_t)fn, g_slhex[i], (uintptr_t)fn,
				       g_slhex[i] + 128 };
}
static int sl_rel_site(void *from, void *target, int op, int i)
{
	const unsigned char *p = from;
	for (int k = 0; k < 96; k++) {
		int32_t rel;
		memcpy(&rel, p + k + 1, 4);
		if (p[k] == op && (uintptr_t)(p + k + 5 + rel) == (uintptr_t)target) {
			hex_of(p + k, 5, g_slhex[i], 128);
			hex_of(target, 4, g_slhex[i] + 128, 128);
			g_sls[i] = (struct site_spec){ g_sl_sites_game[i].name, op == 0xe8 ? K_CALL : K_JMP, (uintptr_t)(p + k),
						       (uintptr_t)(p + k), g_slhex[i], (uintptr_t)target, g_slhex[i] + 128 };
			return 0;
		}
	}
	return -1;
}
static int sl_stand_in_sites(void)
{
	if (sl_rel_site((void *)c_slkey, (void *)st_sl_sheathe, 0xe9, SLS_SHEATH_JMP) ||
	    sl_rel_site((void *)c_slnode, (void *)t_slnode, 0xe8, SLS_REATTACH_NODE))
		return -1;
	sl_entry_site((void *)st_sl_sheathe, SLS_SHEATHE), sl_entry_site((void *)st_sl_reattach, SLS_REATTACH);
	sl_entry_site((void *)st_sl_activate, SLS_ACTIVATE), sl_entry_site((void *)st_bo_code, SLS_ITEM);
	sl_entry_site((void *)st_bo_code, SLS_INVENTORY), sl_entry_site((void *)st_sl_sndreg, SLS_SND_REG);
	sl_entry_site((void *)st_sl_sndpos, SLS_SND_POS), sl_entry_site((void *)st_sl_sndpost, SLS_SND_POST);
	return 0;
}
/* Urfael (slot 0, sheathed, active) and Acharn (slot 1) in bw_setup's inventory: owner, record, two model entries with
 * P7's hashes (ActiveNode R_Weapon's socket; InactiveNode scabbard_back's / Dagger_Sheath's socket) */
static _Alignas(16) char g_slitem[3][0xb0], g_slent[3][2 * SL_IT_ESIZE];
#define SL_T_SCABBARD 0xdecfcc84u
#define SL_T_DSHEATH 0xb9770fb8u
static void sl_item(int k, uint32_t inact)
{
	char *it = g_slitem[k], *en = g_slent[k];
	memset(it, 0, sizeof g_slitem[k]), memset(en, 0, sizeof g_slent[k]);
	uintptr_t b = (uintptr_t)en, e = b + 2 * SL_IT_ESIZE, rec = (uintptr_t)&g_rd_sword, own = (uintptr_t)g_bwown;
	memcpy(it + SL_IT_ENTRIES, &b, 8), memcpy(it + SL_IT_ENTRIES + 8, &e, 8);
	memcpy(it + SL_IT_OWNER, &own, 8), memcpy(it + SL_IT_REC, &rec, 8);
	uint32_t act = SL_H_RWEAPON_SOCKET;
	for (int i = 0; i < 2; i++)
		memcpy(en + i * SL_IT_ESIZE + SL_IE_ACT, &act, 4), memcpy(en + i * SL_IT_ESIZE + SL_IE_INACT, &inact, 4);
}
static void sl_setup(void)
{
	bw_setup(10);
	uint32_t slot = 0;
	memcpy(g_bwinv + BO_INV_SLOT, &slot, 4);
	sl_item(0, SL_T_SCABBARD), sl_item(1, SL_T_DSHEATH);
	g_slitem[0][SL_IT_SHEATHED] = 1, g_slitem[0][SL_IT_ACTIVE] = 1;
	for (int k = 0; k < 2; k++) {
		uintptr_t ip = (uintptr_t)g_slitem[k];
		memcpy(g_bwinv + BO_INV_ITEMS + (uintptr_t)k * BO_INV_STRIDE, &ip, 8);
	}
}
static void sl_set_slot(uint32_t s) { memcpy(g_bwinv + BO_INV_SLOT, &s, 4); }
static uint32_t sl_inact(int k, int i)
{
	uint32_t v;
	memcpy(&v, g_slent[k] + i * SL_IT_ESIZE + SL_IE_INACT, 4);
	return v;
}
static int sl_sheathed(void) { return g_slitem[0][SL_IT_SHEATHED] != 0; }
/* XR space (m; the head at the origin facing -Z): the sword's slot (10, -10, -15 cm in the game's convention) */
static void sl_at_sword(void) { bw_hand(SOMVR_HAND_R, 0.10f, -0.10f, 0.15f); }
static void sl_at_hip(void) { bw_hand(SOMVR_HAND_L, -0.18f, -0.62f, 0.05f); }
static void sl_away(int h) { bw_hand(h, h ? 0.3f : -0.3f, -0.3f, -0.4f); }
static void sl_grip(int h, float v) { g_lst.hand[h].squeeze = v; }

static void slots_tests(void)
{
	EXPECT(log_has("CONFIG slots=1: Urfael at 10,-10,-15 cm from the head") && log_has("Acharn hip at -18,-62,-5") &&
		       log_has("quick draw a double squeeze") && log_has("D14 a: it stays in the hand") &&
		       log_has("the game's draw/sheathe refused: drawn (SHEATH of a held sword") && log_has("sound on") &&
		       log_has("HOOK SL_SHEATH_JMP ok jmp"),
	       "slots: no CONFIG/HOOK lines");
	float pos0[3], rot0[4];
	memcpy(pos0, g_pobj.pos, sizeof pos0), memcpy(rot0, g_pobj.rot, sizeof rot0);
	somvr_rig rig0 = g_rig;
	somvr_rig_cfg cfg0 = g_rigcfg;
	void *pcomb0 = g_player.pcomb;
	int arm0 = g_arm.on;
	g_views_fake = 0, g_rt_result = XR_SUCCESS, g_layout = g_rigcfg.layout = SOMVR_LAYOUT_A;
	g_rigcfg.base = SOMVR_BASE_CAMERA, g_rigcfg.yaw = SOMVR_YAW_CAMERA, g_follow.on = 0, g_fb.on = 0, g_cb.mode = CB_OFF;
	g_cl.on = 0, g_hide.on = 0, g_hudq.on = 0, g_cf.out_s = g_cf.in_s = 0, g_arm.on = 0, g_bow.on = 0, g_th.on = 0;
	g_lo.on = 0, g_lo.anchor = 0;
	somvr_rig_init(&g_rig);
	g_rt_head[0] = g_rt_head[1] = g_rt_head[2] = 0, g_rt_q = (sv_quat){ 0, 0, 0, 1 };
	g_rm[0] = g_rm[1] = g_rm[2] = 0;
	g_pobj.pos[0] = 500, g_pobj.pos[1] = 100, g_pobj.pos[2] = -50;
	g_pobj.rot[0] = g_pobj.rot[1] = g_pobj.rot[2] = 0, g_pobj.rot[3] = 1;
	g_cb_test_t = 12000;
	g_xrt = g_vst.last_time + 1000000000;
	runtime_up();
	g_inp.on = 1, g_inp.lines = 0, g_inp.lsec = 0;
	layer_up();
	for (int h = 0; h < 2; h++) {
		g_lst.hand[h].squeeze = g_lst.hand[h].trigger = 0, g_lst.hand[h].buttons = 0;
		g_lst.hand[h].stick[0] = g_lst.hand[h].stick[1] = 0;
	}
	g_pl = &g_player, g_ppm.owner = &g_player;
	sl_setup();
	g_ninev = 0;
	memset(&g_bwg, 0, sizeof g_bwg);
	memset(&g_slt, 0, sizeof g_slt);
	g_slg.snd_wrap = 0x5157;
	g_sl.on = 1, g_sl.tid = 0, g_sl.lines = 0, g_sl.lsec_n = 0;
	somvr_slots_init(&g_sl.s);
	uintptr_t inv = (uintptr_t)g_bwinv, sword = (uintptr_t)g_slitem[0], dagger = (uintptr_t)g_slitem[1];
	sl_away(SOMVR_HAND_L), sl_away(SOMVR_HAND_R);
	atomic_fetch_add(&g_req_recenter, 1);
	bframes(4, 0);
	EXPECT(g_inp.live && g_sl.have_g && g_sl.g.inv == inv && g_sl.g.item[0] == sword && g_sl.g.item[1] == dagger &&
		       g_sl.g.sheathed == 1 && g_sl.g.slot == 0,
	       "slots: the reads: live %d read %d inv %#lx items %#lx %#lx sheathed %d slot %u", g_inp.live, g_sl.have_g,
	       (unsigned long)g_sl.g.inv, (unsigned long)g_sl.g.item[0], (unsigned long)g_sl.g.item[1], g_sl.g.sheathed,
	       g_sl.g.slot);
	/* 1. Urfael on the back: no blade from the empty hand */
	EXPECT(!g_inp.grip_ready && g_sl.gated > 0, "slots: a blade with the sword sheathed (gated %llu)",
	       (unsigned long long)g_sl.gated);
	/* 2. the right hand into the slot over the shoulder: the tick */
	uint64_t hp0 = atomic_load(&g_in.n_hap_posts);
	sl_at_sword();
	bframe(0);
	EXPECT(g_sl.s.enters[0] == 1 && g_sl.s.inside[0] && atomic_load(&g_in.n_hap_posts) > hp0,
	       "slots: the enter tick: enters %llu inside %d", (unsigned long long)g_sl.s.enters[0], g_sl.s.inside[0]);
	/* 3. the squeeze there: the game's own draw FUN_023116f0(inv, sword, 0), the pulse, the unsheathe sound at the hand */
	hp0 = atomic_load(&g_in.n_hap_posts);
	sl_grip(SOMVR_HAND_R, 1);
	bframe(0);
	sv_vec3 rw = cl_hand_world(SOMVR_HAND_R);
	EXPECT(g_slt.sheathes == 1 && g_slt.inv == inv && g_slt.item == sword && g_slt.sh == 0 && !sl_sheathed() &&
		       atomic_load(&g_in.n_hap_posts) > hp0 && g_slt.posts == 1 && !strcmp(g_slt.snd, "fol_cmn_unsheathe") &&
		       NEAR(g_slt.snd_at[0], rw.x, 1e-2) && NEAR(g_slt.snd_at[1], rw.y, 1e-2) && NEAR(g_slt.snd_at[2], rw.z, 1e-2) && sl_nograb() &&
		       log_has("sword: drawn by the game's own FUN_023116f0") && log_has("sword: draw asked"),
	       "slots: the draw: calls %d sh %u sheathed %d sound %d '%s' at %g %g %g (hand %g %g %g)", g_slt.sheathes, g_slt.sh,
	       sl_sheathed(), g_slt.posts, g_slt.snd, g_slt.snd_at[0], g_slt.snd_at[1], g_slt.snd_at[2], rw.x, rw.y, rw.z);
	bframe(0);
	EXPECT(g_inp.grip_ready && g_sl.s.done[0] == 1 && g_sl.s.keep_last[0] == 1, "slots: drawn: blade %d done %llu",
	       g_inp.grip_ready, (unsigned long long)g_sl.s.done[0]);
	/* 4. the game's SHEATH key on the held sword: refused at the tail jump; an orc's, the dagger's and an UNSHEATH pass */
	int sh0 = g_slt.sheathes;
	uint32_t r = c_slkey(inv, sword, 1);
	EXPECT(r == 0 && !sl_sheathed() && g_slt.sheathes == sh0 && atomic_load(&g_sl.n_refused[1]) == 1,
	       "slots: the game's SHEATH of the held sword: r %u sheathed %d calls %d", r, sl_sheathed(), g_slt.sheathes);
	static _Alignas(16) char orc[0xb0];
	r = c_slkey((uintptr_t)g_bwres, (uintptr_t)orc, 1);
	EXPECT(r == 1 && orc[SL_IT_SHEATHED] == 1 && g_slt.sheathes == sh0 + 1, "slots: an orc's SHEATH did not pass (r %u)", r);
	r = c_slkey(inv, dagger, 1);
	EXPECT(g_slt.sheathes == sh0 + 2 && g_slt.item == dagger, "slots: the dagger's key did not pass");
	g_slitem[1][SL_IT_SHEATHED] = 0;
	r = c_slkey(inv, sword, 0);
	EXPECT(r == 0 && g_slt.sheathes == sh0 + 3 && atomic_load(&g_sl.n_passed[0]) == 1, "slots: an UNSHEATH did not pass");
	bframe(0);
	EXPECT(log_has("the game's SHEATH of Urfael refused (the player keeps it in the hand; 1 so far)") &&
		       log_has("the game's UNSHEATH of Urfael passed (already so for the player; 1 so far)") && !sl_sheathed(),
	       "slots: no refusal lines");
	/* 5. it stays in the hand away from the slot (D14 a) */
	sl_grip(SOMVR_HAND_R, 0);
	sl_away(SOMVR_HAND_R);
	bframes(20, 0);
	EXPECT(!sl_sheathed() && g_inp.grip_ready && !sl_nograb(), "slots: put away by itself");
	/* 6. another of the game's sheathes (FUN_023115f0 writes the flag): drawn again (re-assert) */
	sh0 = g_slt.sheathes;
	g_slitem[0][SL_IT_SHEATHED] = 1;
	bframes(2, 0);
	EXPECT(!sl_sheathed() && g_slt.sheathes == sh0 + 1 && g_slt.sh == 0 && g_sl.s.reasserts[0] == 1 &&
		       log_has("sword: draw asked changed by the game (the game put it away: back to the hand)"),
	       "slots: the re-assert: sheathed %d calls %d", sl_sheathed(), g_slt.sheathes - sh0);
	/* 7. the squeeze in the slot again: put back (FUN_023116f0(inv, sword, 1), the sheathe sound); no blade */
	sl_at_sword();
	bframe(0);
	sl_grip(SOMVR_HAND_R, 1);
	bframe(0);
	EXPECT(sl_sheathed() && g_slt.sh == 1 && !strcmp(g_slt.snd, "fol_cmn_sheathe") && log_has("sword: put back by the game's"),
	       "slots: the put-back: sheathed %d sh %u sound '%s'", sl_sheathed(), g_slt.sh, g_slt.snd);
	bframe(0);
	EXPECT(!g_inp.grip_ready, "slots: a blade after the put-back");
	sl_grip(SOMVR_HAND_R, 0);
	sl_away(SOMVR_HAND_R);
	bframes(3, 0);
	/* 8. the quick draw: a double squeeze away from the slot */
	sl_grip(SOMVR_HAND_R, 1), bframes(2, 0), sl_grip(SOMVR_HAND_R, 0), bframes(3, 0), sl_grip(SOMVR_HAND_R, 1), bframe(0);
	EXPECT(!sl_sheathed() && g_sl.s.quick == 1 && log_has("(quick draw)"), "slots: the quick draw: sheathed %d quick %llu",
	       sl_sheathed(), (unsigned long long)g_sl.s.quick);
	sl_grip(SOMVR_HAND_R, 0), bframes(2, 0);
	/* 8b. SOMVR_SLOTS_KEEP=attach (headset session 1's draw loop): another of the game's sheathes of the held sword -> no
	 * draw call; its entries' InactiveNode = their ActiveNode and the re-attach; it stays in the hand with its blade */
	g_sl.keep_mode = SL_KEEP_ATTACH, g_sl.cfg.reassert = 0, g_sl.ok_reattach = 1;
	sh0 = g_slt.sheathes;
	int ra1 = g_slt.reattaches;
	uint64_t re0 = g_sl.s.reasserts[0];
	uint32_t ina0 = 0, ina = 0, act = 0;
	memcpy(&ina0, g_slent[0] + SL_IE_INACT, 4), memcpy(&act, g_slent[0] + SL_IE_ACT, 4);
	g_slitem[0][SL_IT_SHEATHED] = 1;
	bframes(3, 0);
	memcpy(&ina, g_slent[0] + SL_IE_INACT, 4);
	EXPECT(g_slt.sheathes == sh0 && g_sl.sw_on && ina == act && ina0 != act && g_slt.reattaches == ra1 + 1 &&
		       g_inp.grip_ready && g_sl.s.reasserts[0] == re0 && sl_sheathed() &&
		       log_has("sword: the game put it away: kept in the hand"),
	       "slots: the attach keep: calls %d on %d inactive %#x active %#x (was %#x) re-attaches %d blade %d re-asserts %llu",
	       g_slt.sheathes - sh0, g_sl.sw_on, ina, act, ina0, g_slt.reattaches - ra1, g_inp.grip_ready,
	       (unsigned long long)(g_sl.s.reasserts[0] - re0));
	/* ... the put-back at the shoulder: the saved InactiveNodes back and the re-attach, no game call (sheathed already) */
	sl_at_sword(), bframe(0), sl_grip(SOMVR_HAND_R, 1), bframe(0);
	memcpy(&ina, g_slent[0] + SL_IE_INACT, 4);
	EXPECT(!g_sl.sw_on && ina == ina0 && g_slt.reattaches == ra1 + 2 && g_slt.sheathes == sh0 && sl_sheathed() &&
		       log_has("sword: put back by the game's own re-attach (the kept sword"),
	       "slots: the kept sword's put-back: on %d inactive %#x (saved %#x) re-attaches %d calls %d", g_sl.sw_on, ina,
	       ina0, g_slt.reattaches - ra1, g_slt.sheathes - sh0);
	sl_grip(SOMVR_HAND_R, 0), bframes(2, 0);
	EXPECT(!g_inp.grip_ready, "slots: a blade after the kept sword's put-back");
	/* ... drawn again (the game's draw), then the game draws it itself while kept: undone */
	sl_grip(SOMVR_HAND_R, 1), bframe(0), sl_grip(SOMVR_HAND_R, 0), sl_away(SOMVR_HAND_R), bframes(2, 0);
	g_slitem[0][SL_IT_SHEATHED] = 1, bframes(2, 0);
	int kept = g_sl.sw_on;
	g_slitem[0][SL_IT_SHEATHED] = 0, bframes(2, 0);
	memcpy(&ina, g_slent[0] + SL_IE_INACT, 4);
	EXPECT(kept && !g_sl.sw_on && ina == ina0 && !sl_sheathed() && g_inp.grip_ready &&
		       log_has("sword: our keep undone (the game drew it)"),
	       "slots: the keep undone by the game's draw: kept %d on %d inactive %#x sheathed %d", kept, g_sl.sw_on, ina,
	       sl_sheathed());
	/* ... build 10, SOMVR_SLOTS_GAMEDRAW=refuse: the sword put back; the game's UNSHEATH key refused; another of its
	 * draws (the flag written: a combat start) kept on the back (ActiveNode = InactiveNode, the re-attach; no blade, no
	 * fist); the player's draw brings it out (its ActiveNodes back, no game call); the game's sheathe ends the move */
	sl_at_sword(), bframe(0), sl_grip(SOMVR_HAND_R, 1), bframe(0), sl_grip(SOMVR_HAND_R, 0), sl_away(SOMVR_HAND_R), bframes(3, 0);
	EXPECT(sl_sheathed() && g_sl.s.keep_last[0] == 0, "slots gamedraw: not put back (sheathed %d keep %d)", sl_sheathed(),
	       g_sl.s.keep_last[0]);
	g_sl.gamedraw = 1, g_sl.cfg.undraw = 1;
	uint64_t ur0 = atomic_load(&g_sl.n_refused[0]);
	r = c_slkey(inv, sword, 0);
	bframe(0);
	EXPECT(r == 0 && sl_sheathed() && atomic_load(&g_sl.n_refused[0]) == ur0 + 1 &&
		       log_has("the game's UNSHEATH of Urfael refused (the player keeps it away"),
	       "slots gamedraw: the UNSHEATH key not refused (r %u sheathed %d)", r, sl_sheathed());
	uint32_t act1 = 0, ina1 = 0, actn = 0;
	memcpy(&act1, g_slent[0] + SL_IE_ACT, 4), memcpy(&ina1, g_slent[0] + SL_IE_INACT, 4);
	sh0 = g_slt.sheathes, ra1 = g_slt.reattaches;
	g_slitem[0][SL_IT_SHEATHED] = 0; /* the game's other draw (FUN_023115f0) */
	bframes(3, 0);
	memcpy(&actn, g_slent[0] + SL_IE_ACT, 4);
	EXPECT(g_sl.sw_away && actn == ina1 && act1 != ina1 && g_slt.reattaches == ra1 + 1 && g_slt.sheathes == sh0 &&
		       !sl_sheathed() && !g_inp.grip_ready && !bd_sword_in_hand() && g_sl.s.keep_last[0] == 0 &&
		       g_sl.s.undraws[0] == 0 && log_has("sword: the game drew it: kept on the back"),
	       "slots gamedraw: the game's draw not kept on the back: away %d active %#x (inactive %#x, was %#x) re-attaches %d "
	       "calls %d blade %d held %d keep %d", g_sl.sw_away, actn, ina1, act1, g_slt.reattaches - ra1, g_slt.sheathes - sh0,
	       g_inp.grip_ready, bd_sword_in_hand(), g_sl.s.keep_last[0]);
	sl_at_sword(), bframe(0), sl_grip(SOMVR_HAND_R, 1), bframe(0);
	memcpy(&actn, g_slent[0] + SL_IE_ACT, 4);
	EXPECT(!g_sl.sw_away && actn == act1 && g_slt.reattaches == ra1 + 2 && g_slt.sheathes == sh0 &&
		       log_has("(drawn for the game: its ActiveNodes back)"),
	       "slots gamedraw: the player's draw: away %d active %#x (want %#x) re-attaches %d calls %d", g_sl.sw_away, actn,
	       act1, g_slt.reattaches - ra1, g_slt.sheathes - sh0);
	sl_grip(SOMVR_HAND_R, 0), sl_away(SOMVR_HAND_R), bframes(2, 0);
	EXPECT(g_inp.grip_ready && bd_sword_in_hand() && g_sl.s.keep_last[0] == 1, "slots gamedraw: not in the hand after the draw");
	sl_at_sword(), bframe(0), sl_grip(SOMVR_HAND_R, 1), bframe(0), sl_grip(SOMVR_HAND_R, 0), sl_away(SOMVR_HAND_R), bframes(3, 0);
	g_slitem[0][SL_IT_SHEATHED] = 0, bframes(2, 0);
	int away1 = g_sl.sw_away;
	g_slitem[0][SL_IT_SHEATHED] = 1, bframes(2, 0); /* the game sheathes it again */
	memcpy(&actn, g_slent[0] + SL_IE_ACT, 4);
	EXPECT(away1 && !g_sl.sw_away && actn == act1 && sl_sheathed() && log_has("kept on the back no more (the game sheathed it)"),
	       "slots gamedraw: the game's sheathe did not end the move: was %d away %d active %#x", away1, g_sl.sw_away, actn);
	g_sl.gamedraw = 0, g_sl.cfg.undraw = 0;
	sl_at_sword(), bframe(0), sl_grip(SOMVR_HAND_R, 1), bframe(0), sl_grip(SOMVR_HAND_R, 0), sl_away(SOMVR_HAND_R), bframes(2, 0);
	EXPECT(!sl_sheathed() && g_inp.grip_ready, "slots gamedraw: not drawn again after the test");
	g_sl.keep_mode = SL_KEEP_DRAWN, g_sl.cfg.reassert = 1;
	/* 9. the bow's slot still active (its aim over): melee activated first, then the draw */
	sl_at_sword(), bframe(0), sl_grip(SOMVR_HAND_R, 1), bframe(0), sl_grip(SOMVR_HAND_R, 0), bframe(0);
	EXPECT(sl_sheathed(), "slots: not put back for the activation test");
	sl_set_slot(SL_SLOT_RANGED);
	int act0 = g_slt.activates;
	sl_grip(SOMVR_HAND_R, 1), bframe(0);
	uint32_t slot;
	memcpy(&slot, g_bwinv + BO_INV_SLOT, 4);
	EXPECT(g_slt.activates == act0 + 1 && g_slt.act_slot == 0 && slot == 0 && !sl_sheathed(),
	       "slots: the activation: calls %d slot %u sheathed %d", g_slt.activates - act0, slot, sl_sheathed());
	sl_grip(SOMVR_HAND_R, 0), bframe(0), sl_grip(SOMVR_HAND_R, 1), bframe(0), sl_grip(SOMVR_HAND_R, 0), bframe(0);
	EXPECT(sl_sheathed(), "slots: not put back after the activation");
	/* ... the dagger's slot active (a kill): refused, then DENIED and its pulse */
	sl_set_slot(SL_SLOT_DAGGER);
	sh0 = g_slt.sheathes;
	sl_grip(SOMVR_HAND_R, 1), bframes(50, 0);
	EXPECT(sl_sheathed() && g_slt.sheathes == sh0 && g_sl.s.denied[0] == 1 &&
		       log_has("the draw call refused: another weapon is active") && log_has("sword: DENIED"),
	       "slots: the dagger active: sheathed %d denied %llu", sl_sheathed(), (unsigned long long)g_sl.s.denied[0]);
	sl_grip(SOMVR_HAND_R, 0), sl_set_slot(0), sl_away(SOMVR_HAND_R), bframes(2, 0);
	/* ... build 15b (SOMVR_SLOTS_STEALTH=on): Acharn's slot from our stealth crouch (the stick's latch, PC_Stealth):
	 * the crouch stands up, melee activated, the sword drawn; put back after */
	g_sl.stealth = 1;
	int crm0 = g_cr.mode, crh0 = g_cr.held;
	somvr_crouch_hold crhold0 = g_cr.h;
	double crt0 = g_cr.held_t;
	g_cr.mode = CR_BUTTON, g_cr.h.latched = 1, g_cr.held = 1;
	sl_set_slot(SL_SLOT_DAGGER), sl_at_sword(), bframe(0);
	act0 = g_slt.activates;
	sl_grip(SOMVR_HAND_R, 1), bframe(0);
	memcpy(&slot, g_bwinv + BO_INV_SLOT, 4);
	EXPECT(g_slt.activates == act0 + 1 && slot == 0 && !sl_sheathed() && !g_cr.h.latched && g_sl.stealth_draws == 1 &&
		       log_has("CROUCH sword t=") && log_has("the stealth crouch let go for the sword's draw"),
	       "slots stealth: the draw from the crouch's Acharn: activations %d slot %u sheathed %d latched %d",
	       g_slt.activates - act0, slot, sl_sheathed(), g_cr.h.latched);
	sl_grip(SOMVR_HAND_R, 0), bframe(0), sl_grip(SOMVR_HAND_R, 1), bframe(0), sl_grip(SOMVR_HAND_R, 0), bframe(0);
	EXPECT(sl_sheathed(), "slots stealth: not put back after the draw");
	/* ... Acharn's slot without our crouch (a kill): still refused */
	g_cr.held = 0, g_cr.held_t = 0, g_cr.game = 0;
	sl_set_slot(SL_SLOT_DAGGER);
	act0 = g_slt.activates;
	sl_grip(SOMVR_HAND_R, 1), bframes(50, 0);
	EXPECT(sl_sheathed() && g_slt.activates == act0, "slots stealth: a kill's Acharn activated melee (%d)",
	       g_slt.activates - act0);
	sl_grip(SOMVR_HAND_R, 0), sl_set_slot(0), sl_away(SOMVR_HAND_R), bframes(2, 0);
	g_cr.mode = crm0, g_cr.held = crh0, g_cr.h = crhold0, g_cr.held_t = crt0, g_sl.stealth = 0;
	/* 10. Acharn: the left hand at the hip, a squeeze: its entries' InactiveNode -> L_Weapon and the re-attach; again:
	 * the saved hashes back and the re-attach */
	int ra0 = g_slt.reattaches;
	sl_at_hip(), bframe(0), sl_grip(SOMVR_HAND_L, 1), bframe(0);
	EXPECT(g_slt.reattaches == ra0 + 1 && g_slt.item == dagger && g_slt.reatt_node == SA_H_L_WEAPON &&
		       sl_inact(1, 0) == SA_H_L_WEAPON && sl_inact(1, 1) == SA_H_L_WEAPON && g_sl.dag_on &&
		       log_has("dagger: drawn by the game's own re-attach FUN_0231e2e0 (InactiveNode L_Weapon)"),
	       "slots: the dagger to the hand: reattaches %d node %#x %#x", g_slt.reattaches - ra0, sl_inact(1, 0), sl_inact(1, 1));
	bframe(0);
	EXPECT(sl_dagger_drawn(&g_sl.g) && g_sl.s.done[1] == 1 && sl_sheathed(), "slots: the dagger not in the hand");
	sl_grip(SOMVR_HAND_L, 0), bframe(0), sl_grip(SOMVR_HAND_L, 1), bframe(0);
	EXPECT(g_slt.reattaches == ra0 + 2 && sl_inact(1, 0) == SL_T_DSHEATH && sl_inact(1, 1) == SL_T_DSHEATH && !g_sl.dag_on,
	       "slots: the dagger back: %#x %#x", sl_inact(1, 0), sl_inact(1, 1));
	sl_grip(SOMVR_HAND_L, 0), bframe(0);
	/* ... drawn, then the inventory's dagger changes (a load): our move forgotten, the old item not written again */
	sl_grip(SOMVR_HAND_L, 1), bframe(0), sl_grip(SOMVR_HAND_L, 0), bframe(0);
	EXPECT(g_sl.dag_on, "slots: the dagger not drawn for the load test");
	sl_item(2, SL_T_DSHEATH);
	uintptr_t d2 = (uintptr_t)g_slitem[2];
	memcpy(g_bwinv + BO_INV_ITEMS + 1 * BO_INV_STRIDE, &d2, 8);
	ra0 = g_slt.reattaches;
	bframes(3, 0);
	EXPECT(!g_sl.dag_on && g_sl.dag_lost == 1 && sl_inact(1, 0) == SA_H_L_WEAPON && g_slt.reattaches == ra0 &&
		       log_has("our move forgotten"),
	       "slots: the load: on %d lost %llu old %#x", g_sl.dag_on, (unsigned long long)g_sl.dag_lost, sl_inact(1, 0));
	memcpy(g_bwinv + BO_INV_ITEMS + 1 * BO_INV_STRIDE, &dagger, 8);
	sl_item(1, SL_T_DSHEATH);
	sl_away(SOMVR_HAND_L), bframes(3, 0);
	/* 11. the bow (the real step: the right grip at the left hand's nock): both hands busy and away: Urfael back; after
	 * the bow and its 0.3 s tail it comes back */
	sl_grip(SOMVR_HAND_R, 1), bframes(2, 0), sl_grip(SOMVR_HAND_R, 0), bframes(3, 0), sl_grip(SOMVR_HAND_R, 1), bframe(0);
	sl_grip(SOMVR_HAND_R, 0), bframes(2, 0);
	EXPECT(!sl_sheathed(), "slots: not drawn for the bow test");
	g_bow.on = 1, g_bow.tid = 0, g_bow.lines = 0, g_bow.next_log = 0, g_bow.cam_seen = 0;
	somvr_bow_init(&g_bow.s);
	bw_hand(SOMVR_HAND_L, -0.1f, -0.2f, -0.4f);
	bw_hand(SOMVR_HAND_R, -0.1f, -0.2f, -0.35f);
	bframe(0);
	sl_grip(SOMVR_HAND_R, 1);
	bframe(0);
	EXPECT(bo_out() && g_sl.s.suspends[0] == 1 && sl_sheathed() && !sl_nograb(),
	       "slots: the bow: out %d suspends %llu sheathed %d", bo_out(), (unsigned long long)g_sl.s.suspends[0], sl_sheathed());
	sl_grip(SOMVR_HAND_R, 0); /* a let-down; the hands apart: the aim ends after 0.8 s */
	bw_hand(SOMVR_HAND_R, 0.4f, -0.3f, -0.2f);
	int n = 0;
	while (bo_out() && n < 200)
		bframe(0), n++;
	EXPECT(!bo_out() && sl_sheathed(), "slots: the bow did not end (%d frames) or the sword came back during it", n);
	bframes(30, 0);
	EXPECT(!sl_sheathed() && g_sl.s.restores[0] == 1 && log_has("(back after it)"), "slots: not restored after the bow");
	g_bow.on = 0;
	/* 12. no step for a few renderer frames (flat frames): the refusal lets the game's SHEATH through */
	g_sl.on = 0;
	bframes(5, 0);
	r = c_slkey(inv, sword, 1);
	EXPECT(r == 1 && sl_sheathed(), "slots: a stale refusal (r %u)", r);
	g_sl.on = 1;
	bframes(2, 0);
	EXPECT(!sl_sheathed() && g_sl.s.reasserts[0] == 2, "slots: back in stereo after 5 frames: not re-asserted (it should be)");
	/* ... after a gap of more than 1 s (a cutscene): a new start, the game's state stands */
	g_sl.on = 0;
	bframes(100, 0);
	c_slkey(inv, sword, 1);
	g_sl.on = 1;
	bframes(3, 0);
	EXPECT(sl_sheathed() && g_sl.s.reasserts[0] == 2 && g_sl.resets == 1, "slots: re-asserted after a gap (resets %llu)",
	       (unsigned long long)g_sl.resets);
	/* 13. the end line, then the fail-soft start: SL_ITEM's bytes wrong = the slots off, nothing patched */
	sl_end_log();
	EXPECT(log_has("SLOTS END frames=") && log_has("SHEATH refused 1 passed 2") && log_has("new starts 1"), "slots: no END line");
	struct site_spec bad_sites[SLS_N];
	memcpy(bad_sites, g_sls, sizeof bad_sites);
	bad_sites[SLS_ITEM].ctx_hex = "00 11 22 33 44 55 66 77";
	int bad = 0;
	sl_start(bad_sites, &bad);
	EXPECT(!g_sl.on && log_has("SLOTS site SL_ITEM") && log_has("the slots stay off, nothing patched"),
	       "slots: a mismatching site did not keep them off");
	g_sl_sites = g_sls;
	g_player.pcomb = pcomb0;
	g_arm.on = arm0;
	g_rig = rig0, g_rigcfg = cfg0;
	memcpy(g_pobj.pos, pos0, sizeof pos0), memcpy(g_pobj.rot, rot0, sizeof rot0);
	g_layer_live = 0;
	g_cb_test_t = -1;
}

/* ---- SOMVR_DEFENCE (df_tests): DF_IMPACT on a stand-in impact caller (a real E8 call of a stand-in Combat_DealHit),
 * the read-only proofs and the recoil and AttackData functions as stand-ins, the database's AttackData (RecoilTypeParry,
 * RecoilTypeBlock), the orcs of cb_world attacking through THREAT's tracks (run before threat_tests, with the threat's
 * state from start); the blade set by hand (white box: the combat frame off, g_cb.prev) ---------------------------- */
static char g_dfhex[DFS_N][256];
static struct site_spec g_dfs[DFS_N];
static struct { int n; float pos[3]; uintptr_t att, tgt, atk; int kill; } g_dfdh;
static int g_df_after;
__attribute__((noipa)) void t_dfdealhit(cb_ltvec pos, uintptr_t att, uintptr_t tgt, uintptr_t atk, int kill)
{
	g_dfdh.n++, g_dfdh.pos[0] = pos.x, g_dfdh.pos[1] = pos.y, g_dfdh.pos[2] = pos.z;
	g_dfdh.att = att, g_dfdh.tgt = tgt, g_dfdh.atk = atk, g_dfdh.kill = kill;
}
__attribute__((noipa)) void c_dfimpact(cb_ltvec pos, uintptr_t att, uintptr_t tgt, uintptr_t atk, int kill)
{
	t_dfdealhit(pos, att, tgt, atk, kill);
	g_df_after++;
}
__attribute__((noipa)) uint64_t st_df_window(uintptr_t a, uintptr_t b) { return a == b ? 3 : 5; }
__attribute__((noipa)) uint64_t st_df_type(uintptr_t c) { return c * 5 + 3; }
static struct { int n; uintptr_t att, tgt, atk, rec; float pos[3]; uint8_t haspos; } g_dfr;
static uintptr_t g_df_recoil_to; /* the attack the stand-in recoil puts on the orc (0: refused, no change) */
static uintptr_t g_df_recoil_need; /* the permission (FUN_0280fe60) as a stand-in: only this attack may (0: any) */
static uintptr_t g_df_recoil_rec;  /* ... and only this recoil record has a node (0: any) */
static int g_df_recoil_defer;      /* the game's way: the picked recoil sets Combat+0x56a now, the attack changes later */
__attribute__((noipa)) void st_df_recoil(uintptr_t att, uintptr_t tgt, uintptr_t atk, uintptr_t rec, void *hit)
{
	g_dfr.n++, g_dfr.att = att, g_dfr.tgt = tgt, g_dfr.atk = atk, g_dfr.rec = rec;
	memcpy(g_dfr.pos, (char *)hit + DF_HIT_POS, 12), g_dfr.haspos = ((uint8_t *)hit)[DF_HIT_HASPOS];
	for (int i = 0; i < 4 && g_df_recoil_to && (!g_df_recoil_need || atk == g_df_recoil_need) &&
			(!g_df_recoil_rec || rec == g_df_recoil_rec); i++)
		if ((uintptr_t)g_oc[i].owner == tgt) {
			((uint8_t *)&g_oc[i])[DF_C_RECOILED] = 1;
			if (!g_df_recoil_defer)
				g_oc[i].attack = g_df_recoil_to;
		}
}
#define H_RBLK 0x0b100013u /* AttackData RecoilTypeBlock */
#define H_RPAR 0x0b200014u /* AttackData RecoilTypeParry */
#define H_CANI 0x0b300017u /* AttackData CanInterrupt */
#define H_CNE 0x0b40001bu  /* AttackData CannotInterruptExceptions */
#define H_CLC 0x0c1c0000u  /* the category list's categories */
static st_sch_t g_df_sch[DF_AD_CANNOTBUFF + 1], g_df_clsch[1];
static void *g_df_sch_p = g_df_sch, *g_df_clsch_p = g_df_clsch;
static st_dbrec_t g_df_ad, g_df_ad2, g_df_rblk, g_df_rpar, g_df_cl, g_df_cat_x, g_df_cat_y;
static st_dbent_t g_df_ad_e[4], g_df_ad2_e[1], g_df_cl_e[1], g_df_x_e[1];
static uint64_t g_df_ad_v[4], g_df_ad2_v[1], g_df_cl_v[1], g_df_x_v[1];
__attribute__((noipa)) uintptr_t st_df_attackdata(uintptr_t rec)
{
	return rec == (uintptr_t)g_recs[0] ? (uintptr_t)&g_df_ad : rec == (uintptr_t)g_recs[1] ? (uintptr_t)&g_df_ad2 : 0;
}
static void df_entry_site(void *fn, int i, const char *name)
{
	hex_of(fn, 8, g_dfhex[i], 128);
	hex_of(fn, 4, g_dfhex[i] + 128, 128);
	g_dfs[i] = (struct site_spec){ name, K_ENTRY, (uintptr_t)fn, (uintptr_t)fn, g_dfhex[i], (uintptr_t)fn, g_dfhex[i] + 128 };
}
static int df_stand_in_sites(void) /* main, before somvr_start */
{
	const unsigned char *p = (const unsigned char *)c_dfimpact;
	int found = 0;
	for (int k = 0; k < 96 && !found; k++) {
		int32_t rel;
		memcpy(&rel, p + k + 1, 4);
		if (p[k] == 0xe8 && (uintptr_t)(p + k + 5 + rel) == (uintptr_t)t_dfdealhit) {
			hex_of(p + k, 5, g_dfhex[DFS_IMPACT], 128);
			hex_of((const void *)t_dfdealhit, 4, g_dfhex[DFS_IMPACT] + 128, 128);
			g_dfs[DFS_IMPACT] = (struct site_spec){ "DF_IMPACT", K_CALL, (uintptr_t)(p + k), (uintptr_t)(p + k),
								g_dfhex[DFS_IMPACT], (uintptr_t)t_dfdealhit, g_dfhex[DFS_IMPACT] + 128 };
			found = 1;
		}
	}
	df_entry_site((void *)st_df_window, DFS_WINDOW, "DF_WINDOW");
	df_entry_site((void *)st_df_type, DFS_TYPE, "DF_TYPE");
	df_entry_site((void *)st_df_recoil, DFS_RECOIL, "DF_RECOIL");
	df_entry_site((void *)st_df_attackdata, DFS_ATTACKDATA, "DF_ATTACKDATA");
	g_df_sch[DF_AD_RECOIL_BLOCK] = (st_sch_t){ "RecoilTypeBlock", H_RBLK, { 0 } };
	g_df_sch[DF_AD_RECOIL_PARRY] = (st_sch_t){ "RecoilTypeParry", H_RPAR, { 0 } };
	g_dfg.sch_ad = (uintptr_t)&g_df_sch_p;
	g_df_rblk = (st_dbrec_t){ .flags = 2, .name = "ST_RECOIL_BLOCK" };
	g_df_rpar = (st_dbrec_t){ .flags = 2, .name = "ST_RECOIL_PARRY" };
	g_df_ad_e[0] = dbent(H_RBLK, 0xa, 1, 0), g_df_ad_e[1] = dbent(H_RPAR, 0xa, 1, 1); /* sorted by the hash */
	g_df_ad_v[0] = (uintptr_t)&g_df_rblk, g_df_ad_v[1] = (uintptr_t)&g_df_rpar;
	/* the recoil's permission (SOMVR_DEFENCE_CLANK=recoil): CanInterrupt 0 (the bool: bit 0 of slot 3), only the
	 * categories of ST_CATLIST may (ST_CAT_X) */
	g_df_sch[DF_AD_CANINTERRUPT] = (st_sch_t){ "CanInterrupt", H_CANI, { 0 } };
	g_df_sch[DF_AD_CANNOTEXC] = (st_sch_t){ "CannotInterruptExceptions", H_CNE, { 0 } };
	g_df_clsch[0] = (st_sch_t){ "Categories", H_CLC, { 0 } };
	g_dfg.sch_cl = (uintptr_t)&g_df_clsch_p;
	g_df_ad_e[2] = dbent(H_CANI, 1, 1, 3 << 5), g_df_ad_e[3] = dbent(H_CNE, 0xa, 1, 2);
	g_df_ad_v[2] = (uintptr_t)&g_df_cl, g_df_ad_v[3] = 0;
	g_df_cat_x = (st_dbrec_t){ .flags = 2, .name = "ST_CAT_X" };
	g_df_cat_y = (st_dbrec_t){ .flags = 2, .name = "ST_CAT_Y" };
	g_df_cl_e[0] = dbent(H_CLC, 0xa, 1, 0), g_df_cl_v[0] = (uintptr_t)&g_df_cat_x;
	g_df_cl = (st_dbrec_t){ 1, 3, { 0 }, g_df_cl_e, g_df_cl_v, 0, "ST_CATLIST" };
	g_df_ad = (st_dbrec_t){ 4, 3, { 0 }, g_df_ad_e, g_df_ad_v, 0, "ST_AD_A" };
	g_df_ad2_e[0] = dbent(H_RBLK, 0xa, 1, 0), g_df_ad2_v[0] = (uintptr_t)&g_df_rblk;
	g_df_ad2 = (st_dbrec_t){ 1, 3, { 0 }, g_df_ad2_e, g_df_ad2_v, 0, "ST_AD_BLOCKONLY" };
	return found ? 0 : -1;
}
static uint8_t *df_byte(int i, int off) { return (uint8_t *)&g_oc[i] + off; }
/* the blade (game cm) for the next frame; valid 0 = none */
static void df_blade(int valid, float x, float z)
{
	memset(&g_cb.prev, 0, sizeof g_cb.prev);
	g_cb.prev.valid = valid;
	g_cb.prev.base[0] = x, g_cb.prev.base[1] = 150, g_cb.prev.base[2] = z;
	g_cb.prev.tip[0] = x, g_cb.prev.tip[1] = 240, g_cb.prev.tip[2] = z;
	g_cb.prev.t = g_cb_test_t + 1.0 / 90;
}
static struct df_track *df_tr_of(int i)
{
	for (int k = 0; k < TH_TRACKS; k++)
		if (g_df.tr[k].c == (uintptr_t)&g_oc[i])
			return &g_df.tr[k];
	return NULL;
}
static void df_impact_call(int i) /* the orc's APPLYDAMAGE, its range/angle test passed: the call of Combat_DealHit */
{
	c_dfimpact((cb_ltvec){ 1, 2, 3 }, (uintptr_t)&g_oc[i], (uintptr_t)&g_pobj, g_oc[i].attack, 0);
}
/* frames of orc i's attack (started at the first) until ts >= until; the blade from `blade_from` s (-1 = none, x) */
static void df_run(int i, float until, float blade_from, float x)
{
	for (int f = 0; f < 200; f++) {
		struct th_track *tr = th_tr_of(i);
		double ts = tr ? g_cb_test_t + 1.0 / 90 - tr->t0 : 0;
		if (tr && ts > until + 1e-6)
			break;
		df_blade(blade_from >= 0 && ts >= blade_from - 1e-6, x, 340);
		iframe();
	}
}
static void df_end_attack(int i)
{
	g_oc[i].attack = 0;
	df_blade(0, 100, 340);
	iframe();
}

static void df_tests(void)
{
	static __typeof__(g_th) th_save; /* threat_tests runs next: its counters, sound registration and config as at start */
	th_save = g_th;
	int padmode0 = g_inp.padmode;
	EXPECT(log_has("CONFIG defence mode=1 parry=counter block=recoil arrows=1 red=dodge way=20 cm cross=35 deg align=50 deg "
		       "parry_ms=300 grace_ms=50 dodge=30 cm back=40 cm arrow=12 cm haptic=1.00 counter_cmd=auto press_ticks=3; "
		       "sites: DF_IMPACT ok, DF_WINDOW ok, DF_TYPE ok, DF_RECOIL ok, DF_ATTACKDATA ok") &&
		       log_has("HOOK DF_IMPACT ok call") && g_df_h.installed && g_df_h.orig == (uintptr_t)t_dfdealhit,
	       "defence: no CONFIG/HOOK lines at start");
	/* the world of threat_tests: the camera base (100 200 300, ahead +Z), A 1.5 m ahead (its R_Hand at the head's height
	 * + 2.5 cm), B behind left */
	g_views_fake = 0, g_rt_result = XR_SUCCESS, g_layout = g_rigcfg.layout = SOMVR_LAYOUT_A;
	g_rigcfg.base = SOMVR_BASE_CAMERA, g_rigcfg.yaw = SOMVR_YAW_CAMERA, g_follow.on = 0, g_fb.on = 0, g_cb.mode = CB_OFF;
	g_cl.on = 0, g_hide.on = 0, g_hudq.on = 0, g_cf.out_s = g_cf.in_s = 0, g_arm.on = 0, g_bow.on = 0, g_rx.mode = RX_OFF;
	somvr_rig_init(&g_rig);
	g_rt_head[0] = g_rt_head[1] = g_rt_head[2] = 0, g_rt_q = (sv_quat){ 0, 0, 0, 1 };
	g_rm[0] = g_rm[1] = g_rm[2] = 0;
	g_pobj.pos[0] = 100, g_pobj.pos[1] = 30, g_pobj.pos[2] = 300;
	g_pobj.rot[0] = g_pobj.rot[1] = g_pobj.rot[2] = 0, g_pobj.rot[3] = 1;
	g_cb_test_t = 20000; /* loco_tests' and threat_tests' clock (threat_tests starts there again) */
	g_xrt = g_vst.last_time + 1000000000;
	runtime_up();
	g_inp.on = 1, g_inp.lines = 0, g_inp.lsec = 0;
	layer_up();
	g_pl = &g_player, g_ppm.owner = &g_player;
	g_pz[0x34] = 0;
	g_nodexf_node = NULL, g_nodexf_fail = g_nodexf_nan = 0, g_head_up = 72.5f;
	cb_world();
	static const float opos[2][3] = { { 100, 130, 450 }, { 0, 130, 100 } };
	for (int i = 0; i < 2; i++)
		memcpy(g_oobj[i].pos, opos[i], sizeof opos[i]);
	void *mi = &g_piface;
	memcpy((char *)&g_oobj[0] + OBJ_MODEL, &mi, 8);
	g_th.on = 1, g_th.tid = 0, g_th.last_t = 0, g_th.lines = 0, g_th.hap = 0, g_th.parry = 0; /* its pulses off: ours alone */
	memset(g_th.tr, 0, sizeof g_th.tr);
	g_df.mode = DF_ON, g_df.tid = 0, g_df.lines = 0;
	memset(g_df.tr, 0, sizeof g_df.tr);
	uintptr_t T = (uintptr_t)&g_pobj;
	atomic_fetch_add(&g_req_recenter, 1);
	df_blade(0, 100, 340);
	for (int i = 0; i < 3; i++)
		iframe();
	EXPECT(g_df.n.frames >= 3 && !df_tr_of(0) && g_df.ph[0] == T, "defence calm: frames %llu ph %#lx",
	       (unsigned long long)g_df.n.frames, (unsigned long)g_df.ph[0]);
	somvr_haptic_req q;
	memset(&q, 0, sizeof q);
	th_drain_haptics();

	/* 1. the parry: A's infantry strike with the window byte open, the sword held across its line 40 cm ahead (pad
	 * mode): fires at the zone's first frame (its 42nd: the attack's time is THREAT's clock, the orc's own 0.011 s
	 * timer since re-xr-pipeline.md 19.2, ts 0.462 s, not 42/90 s of real time), CombatPrimary02 held for 3 input
	 * ticks (its ON, then OFF), the parry pulse; the orc's attack then replaced (the stock counter's sync) = countered */
	g_profile[0x238] = 1;
	*df_byte(0, DF_C_WINDOW) = 1, *df_byte(0, DF_C_TYPE) = 0;
	int ev0 = g_ninev, parry0 = th_log_count("DEFENCE parry orc=");
	th_attack(0, T, (uintptr_t)g_recs[0], TH_KT_INF);
	df_run(0, 0.45f, 0, 100);
	EXPECT(df_tr_of(0) && !df_tr_of(0)->s.parried && df_tr_of(0)->s.g.valid && df_tr_of(0)->s.g.in_way &&
		       df_tr_of(0)->s.g.aligned && NEAR(df_tr_of(0)->s.g.dist_cm, 0, 0.05) && df_tr_of(0)->window == 1,
	       "parry: before the zone: tracked %d parried %d way %d dist %g", df_tr_of(0) != NULL,
	       df_tr_of(0) ? df_tr_of(0)->s.parried : -1, df_tr_of(0) ? df_tr_of(0)->s.g.in_way : -1,
	       df_tr_of(0) ? df_tr_of(0)->s.g.dist_cm : -1);
	df_run(0, 0.47f, 0, 100);
	int rp = hap_take(SOMVR_HAND_R, &q);
	float ramp = q.amplitude;
	int lp = hap_take(SOMVR_HAND_L, &q);
	EXPECT(df_tr_of(0) && df_tr_of(0)->s.parried &&
		       NEAR(df_tr_of(0)->s.parry_t - th_tr_of(0)->t0, 42 * (double)g_orc_timer.frame_s, 1e-6) &&
		       th_log_count("DEFENCE parry orc=") == parry0 + 1 &&
		       log_has("(normal) at 0.462 s (window 0.000..0.761 s, byte 1, impact 0.800 s): blade 0.0 cm from the blow") &&
		       log_has("-> the stock counter pressed cmd=CombatPrimary02") && rp && NEAR(ramp, 0.9, 1e-3) && lp &&
		       NEAR(q.amplitude, 0.35, 1e-3) && g_df.press.presses == 1,
	       "parry fired: parried %d at %g s, lines %d, pulses R %d (%g) L %d (%g)", df_tr_of(0) ? df_tr_of(0)->s.parried : -1,
	       df_tr_of(0) ? df_tr_of(0)->s.parry_t - th_tr_of(0)->t0 : -1, th_log_count("DEFENCE parry orc=") - parry0, rp, ramp,
	       lp, q.amplitude);
	float held[4]; /* the denied command's record is held already (the deny list's 0): our press turns it on in place */
	for (int k = 0; k < 4; k++) {
		df_blade(1, 100, 340);
		iframe();
		held[k] = poll22(SC_CMD_COUNTER);
	}
	EXPECT(nev('N', SC_CMD_COUNTER, ev0) == 1 && nev('F', SC_CMD_COUNTER, ev0) == 1 && held[0] == 1 && held[2] == 1 &&
		       held[3] == 0 && ev_input('N', SC_CMD_COUNTER, ev0) == 2013 &&
		       g_df.press.left == 0,
	       "the counter press: ON %d OFF %d, polls %g %g %g %g, input %d", nev('N', SC_CMD_COUNTER, ev0),
	       nev('F', SC_CMD_COUNTER, ev0), held[0], held[1], held[2], held[3], ev_input('N', SC_CMD_COUNTER, ev0));
	g_oc[0].attack = (uintptr_t)g_recs[2]; /* the stock counter took: the orc's sync reaction replaces its attack */
	df_blade(1, 100, 340);
	iframe();
	EXPECT(g_df.n.countered == 1 && log_has("DEFENCE countered orc=") && log_has("ended") && g_dfdh.n == 0,
	       "countered: %llu", (unsigned long long)g_df.n.countered);
	df_end_attack(0);
	for (int k = 0; k < 30; k++) /* the press's cooldown */
		iframe();
	th_drain_haptics();

	/* 2. the counter did not take (the attack runs on): its impact is stopped (parry-block), the orc's recoil the next
	 * frame with its attack's RecoilTypeParry from our contact; KBM mode: CombatKBM09 */
	g_profile[0x238] = 0, g_inp.padmode = 0; /* KBM mode, not forced back */
	ev0 = g_ninev;
	int dh0 = g_dfdh.n, af0 = g_df_after, r0 = g_dfr.n;
	th_attack(0, T, (uintptr_t)g_recs[0], TH_KT_INF);
	df_run(0, 0.8f, 0, 100);
	EXPECT(df_tr_of(0) && df_tr_of(0)->s.parried && nev('N', DF_CMD_KBM, ev0) == 1 && log_has("cmd=CombatKBM09"),
	       "KBM parry: parried %d ON(144) %d", df_tr_of(0) ? df_tr_of(0)->s.parried : -1, nev('N', DF_CMD_KBM, ev0));
	th_drain_haptics();
	df_impact_call(0);
	EXPECT(g_dfdh.n == dh0 && g_df_after == af0 + 1 && df_tr_of(0)->outcome == SOMVR_DFO_PARRY_BLOCK &&
		       atomic_load(&g_df_h.mods) >= 1,
	       "parry-block: DealHit %d (want %d), after %d", g_dfdh.n, dh0, g_df_after - af0);
	df_blade(1, 100, 340);
	iframe();
	rp = hap_take(SOMVR_HAND_R, &q);
	EXPECT(g_dfr.n == r0 + 1 && g_dfr.att == T && g_dfr.tgt == (uintptr_t)&g_och[0] && g_dfr.atk == (uintptr_t)g_recs[0] &&
		       g_dfr.rec == (uintptr_t)&g_df_rpar && g_dfr.haspos == 1 && NEAR(g_dfr.pos[0], 100, 1e-3) &&
		       NEAR(g_dfr.pos[2], 340, 1e-3) && g_dfr.pos[1] > 185 && g_dfr.pos[1] < 195 && rp && NEAR(q.amplitude, 0.75, 1e-3) &&
		       log_has("DEFENCE parry-block orc=") && log_has("the game's hit skipped; its recoil: no change") &&
		       log_has("recoil 0x") && log_has("'ST_RECOIL_PARRY' (RecoilTypeParry of '')") &&
		       log_has("attack '(its own attack: no light record yet)'") && g_df.n.parry_blocks == 1,
	       "parry-block recoil: calls %d att %#lx tgt %#lx atk %#lx rec %#lx pos %g %g %g haspos %d pulse %d (%g)",
	       g_dfr.n - r0, (unsigned long)g_dfr.att, (unsigned long)g_dfr.tgt, (unsigned long)g_dfr.atk,
	       (unsigned long)g_dfr.rec, g_dfr.pos[0], g_dfr.pos[1], g_dfr.pos[2], g_dfr.haspos, rp, q.amplitude);
	df_impact_call(0); /* a second impact of the same attack: judged once, the game's */
	EXPECT(g_dfdh.n == dh0 + 1 && g_dfdh.att == (uintptr_t)&g_oc[0] && g_dfdh.tgt == T && g_dfdh.atk == (uintptr_t)g_recs[0] &&
		       g_dfdh.kill == 0 && NEAR(g_dfdh.pos[0], 1, 1e-6) && NEAR(g_dfdh.pos[1], 2, 1e-6) && NEAR(g_dfdh.pos[2], 3, 1e-6),
	       "second impact: passed %d with pos %g %g %g", g_dfdh.n - dh0, g_dfdh.pos[0], g_dfdh.pos[1], g_dfdh.pos[2]);
	df_end_attack(0);
	for (int k = 0; k < 30; k++)
		iframe();
	th_drain_haptics();

	/* 3. a late blade (after the zone): no parry; in the way at the impact = a block; the recoil takes (the stand-in
	 * replaces the orc's attack); an attack with RecoilTypeBlock only (g_recs[1]) */
	dh0 = g_dfdh.n, r0 = g_dfr.n;
	uint64_t pr0 = g_df.n.parries;
	g_df_recoil_to = (uintptr_t)g_recs[3];
	th_attack(0, T, (uintptr_t)g_recs[1], TH_KT_INF);
	df_run(0, 0.8f, 0.75f, 100);
	EXPECT(df_tr_of(0) && !df_tr_of(0)->s.parried && g_df.n.parries == pr0, "late blade: parried %d",
	       df_tr_of(0) ? df_tr_of(0)->s.parried : -1);
	df_impact_call(0);
	df_blade(1, 100, 340);
	iframe();
	rp = hap_take(SOMVR_HAND_R, &q);
	EXPECT(g_dfdh.n == dh0 && g_dfr.n == r0 + 1 && g_dfr.rec == (uintptr_t)&g_df_rblk && g_oc[0].attack == (uintptr_t)g_recs[3] &&
		       rp && NEAR(q.amplitude, 0.75, 1e-3) && log_has("DEFENCE block orc=") && log_has("its recoil took") &&
		       log_has("(RecoilTypeBlock of '')") && g_df.n.blocks == 1 && g_df.n.recoil_took >= 1,
	       "block: DealHit %d recoil %d rec %#lx attack now %#lx pulse %d", g_dfdh.n - dh0, g_dfr.n - r0,
	       (unsigned long)g_dfr.rec, (unsigned long)g_oc[0].attack, rp);
	g_df_recoil_to = 0;
	df_end_attack(0);

	/* 4. no blade: the impact is the game's (the same arguments), a hit line; the blade 40 cm aside: the same */
	dh0 = g_dfdh.n;
	th_attack(0, T, (uintptr_t)g_recs[0], TH_KT_INF);
	df_run(0, 0.8f, -1, 100);
	df_impact_call(0);
	EXPECT(g_dfdh.n == dh0 + 1 && g_dfdh.atk == (uintptr_t)g_recs[0] && df_tr_of(0)->outcome == -1, "no blade: passed %d",
	       g_dfdh.n - dh0);
	df_end_attack(0);
	th_attack(0, T, (uintptr_t)g_recs[0], TH_KT_INF);
	df_run(0, 0.8f, 0, 140);
	df_impact_call(0);
	iframe();
	EXPECT(g_dfdh.n == dh0 + 2 && df_tr_of(0) && !df_tr_of(0)->s.parried && NEAR(df_tr_of(0)->s.g.dist_cm, 40, 0.05) &&
		       log_has("DEFENCE hit orc=") && log_has("-> the hit lands (the game's)"),
	       "blade aside: passed %d dist %g", g_dfdh.n - dh0, df_tr_of(0) ? df_tr_of(0)->s.g.dist_cm : -1);
	df_end_attack(0);
	th_drain_haptics();

	/* 4b. build 10, SOMVR_HURT: a landed blow (A's, ahead) and Talion's health dropping: the pulse in both hands, the red
	 * flash in the threat entries (the blow's side full, the other half), over after SOMVR_HURT_MS; off: the line alone */
	{
		float *thp = (float *)((char *)&g_och[4] + CH_HEALTH), hp_was = *thp;
		g_hu.on = 1, g_hu.ms = 300, g_hu.flash = 0.8f, g_hu.have = 0;
		*thp = 120;
		iframe(), iframe();
		th_drain_haptics();
		th_attack(0, T, (uintptr_t)g_recs[0], TH_KT_INF);
		df_run(0, 0.8f, -1, 100);
		df_impact_call(0); /* the hit passes (the game's); its event carries the weapon hand */
		*thp = 99;
		uint64_t fl0 = g_hu.flashes;
		iframe(); /* df_drain: the blow's side; then the health's drop: the pulse, the flash from now */
		int hr = hap_take(SOMVR_HAND_R, &q);
		float ra = q.amplitude;
		int hl = hap_take(SOMVR_HAND_L, &q);
		iframe(); /* the threat entry carries it */
		somvr_threat_frame te;
		memset(&te, 0, sizeof te);
		int got = somvr_threat_get(&g_tht, atomic_load(&g_tht.last), &te);
		int hi = te.hurt[0] > te.hurt[1] ? te.hurt[0] : te.hurt[1], lo = te.hurt[0] > te.hurt[1] ? te.hurt[1] : te.hurt[0];
		EXPECT(g_hu.flashes == fl0 + 1 && hr && hl && ra >= 0.8f && got && (te.flags & SOMVR_THF_HURT) && hi > 150 &&
			       lo > 60 && lo < hi && log_has("FIGHT hurt t=") && log_has("Talion's health 120.0 -> 99.0 (-21.0") &&
			       log_has(": the pulse, the red flash"),
		       "hurt: flashes %llu pulses R %d (%g) L %d, entry %d flags %#x hurt %u %u", (unsigned long long)(g_hu.flashes - fl0),
		       hr, ra, hl, got, te.flags, te.hurt[0], te.hurt[1]);
		for (int k = 0; k < 30; k++)
			iframe();
		got = somvr_threat_get(&g_tht, atomic_load(&g_tht.last), &te);
		EXPECT(got && te.hurt[0] == 0 && te.hurt[1] == 0 && !(te.flags & SOMVR_THF_HURT), "hurt: the flash not over: %u %u",
		       te.hurt[0], te.hurt[1]);
		g_hu.on = 0, *thp = 80;
		iframe(), iframe();
		got = somvr_threat_get(&g_tht, atomic_load(&g_tht.last), &te);
		EXPECT(g_hu.flashes == fl0 + 1 && got && !te.hurt[0] && !te.hurt[1] && log_has("(SOMVR_HURT off: no pulse, no flash)"),
		       "hurt off: a flash %llu", (unsigned long long)(g_hu.flashes - fl0));
		/* build 24, SOMVR_HURT_LOW=0.25: under a quarter of the most seen (120) a faint steady red on both edges
		 * (SOMVR_HURT_LOW_EDGE 0.3 = 76 of 255) after the blow's flash; above it none */
		g_hu.on = 1, g_hu.low = 0.25f, g_hu.low_edge = 0.3f, *thp = 20;
		for (int k = 0; k < 30; k++)
			iframe();
		got = somvr_threat_get(&g_tht, atomic_load(&g_tht.last), &te);
		EXPECT(got && te.hurt[0] == 76 && te.hurt[1] == 76 && (te.flags & SOMVR_THF_HURT) &&
			       log_has("FIGHT low t=") && log_has("Talion's health 20.0 of 120.0: under SOMVR_HURT_LOW - the faint red edge on"),
		       "hurt low: the edge %u %u flags %#x", te.hurt[0], te.hurt[1], te.flags);
		*thp = 60;
		iframe(), iframe();
		got = somvr_threat_get(&g_tht, atomic_load(&g_tht.last), &te);
		EXPECT(got && !te.hurt[0] && !te.hurt[1] && log_has("Talion's health 60.0 of 120.0: the faint red edge off"),
		       "hurt low: above it, the edge %u %u", te.hurt[0], te.hurt[1]);
		g_hu.on = 0, g_hu.low = 0;
		df_end_attack(0);
		th_drain_haptics();
		*thp = hp_was, g_hu.have = 0;
	}

	/* 5. the dodge: the head 35 cm sideways during the attack (no blade): stopped, the dodge pulse on both hands */
	dh0 = g_dfdh.n;
	th_attack(0, T, (uintptr_t)g_recs[0], TH_KT_INF);
	df_run(0, 0.3f, -1, 100);
	g_rt_head[0] = 0.35f;
	df_run(0, 0.8f, -1, 100);
	df_impact_call(0);
	iframe();
	rp = hap_take(SOMVR_HAND_R, &q);
	lp = hap_take(SOMVR_HAND_L, &q);
	EXPECT(g_dfdh.n == dh0 && df_tr_of(0)->outcome == SOMVR_DFO_DODGE && NEAR(df_tr_of(0)->s.lateral_cm, 35, 0.05) && rp &&
		       lp && NEAR(q.amplitude, 0.25, 1e-3) && log_has("DEFENCE dodge orc=") && log_has("head 35.0 cm aside"),
	       "dodge: DealHit %d outcome %d lateral %g pulses %d %d", g_dfdh.n - dh0, df_tr_of(0) ? df_tr_of(0)->outcome : -9,
	       df_tr_of(0) ? df_tr_of(0)->s.lateral_cm : -1, rp, lp);
	g_rt_head[0] = 0;
	df_end_attack(0);
	/* 5b. the same 35 cm in the frame of a rig jump (a recentre, a re-seat's cut: the game's, not the player's): carried
	 * into the start (somvr_df_in.moved), so the hit lands (boot-check-20261024-020824: a knockback judged a dodge) */
	iframe();
	dh0 = g_dfdh.n;
	double moved0 = g_df.moved_cm;
	th_attack(0, T, (uintptr_t)g_recs[0], TH_KT_INF);
	df_run(0, 0.3f, -1, 100);
	g_rt_head[0] = 0.35f, g_rig_epoch++;
	df_run(0, 0.8f, -1, 100);
	df_impact_call(0);
	iframe();
	th_drain_haptics();
	EXPECT(g_dfdh.n == dh0 + 1 && df_tr_of(0)->outcome == -1 /* judged, landed */ && df_tr_of(0)->s.lateral_cm < 1 &&
		       NEAR(g_df.moved_cm - moved0, 35, 0.5),
	       "a rig jump's 35 cm: no dodge: DealHit %d outcome %d lateral %g carried %g", g_dfdh.n - dh0,
	       df_tr_of(0) ? df_tr_of(0)->outcome : -9, df_tr_of(0) ? df_tr_of(0)->s.lateral_cm : -1, g_df.moved_cm - moved0);
	g_rt_head[0] = 0, g_rig_epoch++;
	df_end_attack(0);
	iframe();

	/* 6. a red attack (type 1, cannot parry): the blade in the way does nothing, the hit lands; with the window byte
	 * shut a normal one is not parried but blocked */
	dh0 = g_dfdh.n, pr0 = g_df.n.parries;
	*df_byte(0, DF_C_TYPE) = 1;
	th_attack(0, T, (uintptr_t)g_recs[0], TH_KT_INF);
	df_run(0, 0.8f, 0, 100);
	df_impact_call(0);
	iframe();
	EXPECT(g_dfdh.n == dh0 + 1 && g_df.n.parries == pr0 && log_has("(cannot-parry) at its impact"), "red: passed %d parries %llu",
	       g_dfdh.n - dh0, (unsigned long long)(g_df.n.parries - pr0));
	df_end_attack(0);
	*df_byte(0, DF_C_TYPE) = 0, *df_byte(0, DF_C_WINDOW) = 0;
	th_attack(0, T, (uintptr_t)g_recs[0], TH_KT_INF);
	df_run(0, 0.8f, 0, 100);
	df_impact_call(0);
	EXPECT(g_dfdh.n == dh0 + 1 && g_df.n.parries == pr0 && df_tr_of(0)->window == 0 && df_tr_of(0)->outcome == SOMVR_DFO_BLOCK,
	       "window shut: parries %llu outcome %d", (unsigned long long)(g_df.n.parries - pr0),
	       df_tr_of(0) ? df_tr_of(0)->outcome : -9);
	df_end_attack(0);
	*df_byte(0, DF_C_WINDOW) = 1;
	/* an archer's shot (a SHOOT key: P8's table) at A's place with the sword across its line: never a parry (no
	 * counter press at a ranged attack: P9's PC_CounterAttempt_Fail), never stopped here (its arrow is the threat) */
	static const uint16_t KT_ARCH[10] = { 0xffff, 0xffff, 2633, 0xffff, 0xffff, 0xffff, 1566, 2566, 2600, 0xffff };
	pr0 = g_df.n.parries;
	th_attack(0, T, (uintptr_t)g_recs[0], KT_ARCH);
	df_run(0, 2.64f, 0, 100);
	df_impact_call(0);
	EXPECT(g_df.n.parries == pr0 && g_dfdh.n == dh0 + 2 && df_tr_of(0) && df_tr_of(0)->type == SOMVR_DF_TYPE_RANGED,
	       "ranged: parries %llu passed %d type %d", (unsigned long long)(g_df.n.parries - pr0), g_dfdh.n - dh0,
	       df_tr_of(0) ? df_tr_of(0)->type : -9);
	df_end_attack(0);
	dh0 = g_dfdh.n;

	/* 7. two attackers: A ahead and B behind left strike together; the blade before A: A stopped, B's hit lands */
	dh0 = g_dfdh.n;
	th_attack(0, T, (uintptr_t)g_recs[0], TH_KT_INF);
	th_attack(1, T, (uintptr_t)g_recs[1], TH_KT_INF);
	df_run(0, 0.8f, 0, 100);
	EXPECT(df_tr_of(0) && df_tr_of(1) && df_tr_of(0)->s.parried && !df_tr_of(1)->s.parried && !df_tr_of(1)->s.g.in_way,
	       "two: A parried %d, B parried %d way %d", df_tr_of(0) ? df_tr_of(0)->s.parried : -1,
	       df_tr_of(1) ? df_tr_of(1)->s.parried : -1, df_tr_of(1) ? df_tr_of(1)->s.g.in_way : -1);
	df_impact_call(0);
	df_impact_call(1);
	iframe();
	EXPECT(g_dfdh.n == dh0 + 1 && g_dfdh.att == (uintptr_t)&g_oc[1], "two: passed %d (B's)", g_dfdh.n - dh0);
	g_oc[1].attack = 0;
	df_end_attack(0);
	th_attack(1, 0, 0, NULL);
	for (int k = 0; k < 30; k++)
		iframe();
	th_drain_haptics();

	/* 8. an arrow from 20 m ahead at the head through the blade: removed (+0x228) before it passes the blade */
	memset(g_proj, 0, sizeof g_proj);
	float pp[3] = { 100, 195, 2300 }, pq[4] = { 0, 1, 0, 0 }, ps = 2800;
	memcpy(g_proj + TH_PJ_POS, pp, 12), memcpy(g_proj + TH_PJ_ROT, pq, 16), memcpy(g_proj + TH_PJ_SPEED, &ps, 4);
	memcpy(g_proj + TH_PJ_TARGET, &T, 8);
	uint64_t ar0 = g_df.n.arrows;
	float zdone = 0;
	for (int k = 0; k < 90 && !g_proj[TH_PJ_DONE]; k++) {
		df_blade(1, 100, 340);
		iframe();
		th_proj_tick();
		memcpy(&zdone, g_proj + TH_PJ_POS + 8, 4);
	}
	df_blade(1, 100, 340);
	iframe();
	rp = hap_take(SOMVR_HAND_R, &q);
	EXPECT(g_proj[TH_PJ_DONE] == 1 && zdone > 340 && g_df.n.arrows == ar0 + 1 && rp && NEAR(q.amplitude, 0.6, 1e-3) &&
		       log_has("DEFENCE arrow p=") && log_has("(at Talion) blocked") && log_has("28 m/s -> removed (+0x228)"),
	       "arrow: done %d at z %g, arrows %llu, pulse %d", g_proj[TH_PJ_DONE], zdone,
	       (unsigned long long)(g_df.n.arrows - ar0), rp);
	/* one 50 cm to the side: flies on */
	memset(g_proj, 0, sizeof g_proj);
	pp[0] = 150;
	memcpy(g_proj + TH_PJ_POS, pp, 12), memcpy(g_proj + TH_PJ_ROT, pq, 16), memcpy(g_proj + TH_PJ_SPEED, &ps, 4);
	for (int k = 0; k < 90; k++) {
		df_blade(1, 100, 340);
		iframe();
		th_proj_tick();
	}
	EXPECT(!g_proj[TH_PJ_DONE] && g_df.n.arrows == ar0 + 1, "arrow aside: done %d", g_proj[TH_PJ_DONE]);
	memset(g_proj, 0, sizeof g_proj);
	for (int k = 0; k < 12; k++)
		iframe();

	/* 9. log mode: judged, never applied (the hit passes, no press); off: nothing */
	g_df.mode = DF_LOG;
	dh0 = g_dfdh.n, ev0 = g_ninev;
	th_attack(0, T, (uintptr_t)g_recs[0], TH_KT_INF);
	df_run(0, 0.8f, 0, 100);
	df_impact_call(0);
	iframe();
	EXPECT(g_dfdh.n == dh0 + 1 && nev('N', DF_CMD_KBM, ev0) == 0 && nev('N', DF_CMD_PAD, ev0) == 0 &&
		       log_has("-> log only") && df_tr_of(0) && df_tr_of(0)->outcome == SOMVR_DFO_PARRY_BLOCK,
	       "log mode: passed %d presses %d", g_dfdh.n - dh0, nev('N', DF_CMD_KBM, ev0));
	df_end_attack(0);
	g_df.mode = DF_OFF;
	uint64_t fr0 = g_df.n.frames;
	th_attack(0, T, (uintptr_t)g_recs[0], TH_KT_INF);
	df_run(0, 0.8f, 0, 100);
	df_impact_call(0);
	EXPECT(g_dfdh.n == dh0 + 2 && g_df.n.frames == fr0, "off: passed %d frames %llu", g_dfdh.n - dh0,
	       (unsigned long long)(g_df.n.frames - fr0));
	df_end_attack(0);

	/* 10. the pause (THREAT holds the attack's clock): the same track, the parry at 0.462 s of attack time (42 frames
	 * of the orc's timer); a flat stretch keeps the track with THREAT's (its parry, no second one, the head's motion in
	 * it no dodge), past TH_FLAT_KEEP_S both end; at start: SOMVR_THREAT off, then DF_IMPACT not matching (fail-soft) */
	g_df.mode = DF_ON;
	for (int k = 0; k < 30; k++)
		iframe();
	uint64_t tk0 = g_df.n.tracks;
	pr0 = g_df.n.parries;
	th_attack(0, T, (uintptr_t)g_recs[0], TH_KT_INF);
	df_run(0, 0.2f, 0, 100);
	g_pz[0x34] = 1;
	for (int k = 0; k < 30; k++) {
		df_blade(1, 100, 340);
		iframe();
	}
	g_pz[0x34] = 0;
	df_run(0, 0.47f, 0, 100);
	EXPECT(g_df.n.tracks == tk0 + 1 && g_df.n.parries == pr0 + 1 && df_tr_of(0) && df_tr_of(0)->s.parried &&
		       NEAR(df_tr_of(0)->s.parry_t - df_tr_of(0)->th_t0, 42 * (double)g_orc_timer.frame_s, 1e-6) &&
		       g_df.n.paused >= 30,
	       "pause: tracks %llu parries %llu at %g s", (unsigned long long)(g_df.n.tracks - tk0),
	       (unsigned long long)(g_df.n.parries - pr0), df_tr_of(0) ? df_tr_of(0)->s.parry_t - df_tr_of(0)->th_t0 : -1);
	struct df_track *kt = df_tr_of(0);
	uint64_t ser = kt ? kt->serial : 0, fl0 = g_df.n.flat;
	atomic_store(&g_sh.xr.state, SOMVR_XRS_NONE);
	frame();
	g_rt_head[0] = 0.35f; /* the head 35 cm aside while nothing is seen */
	frame();
	EXPECT(kt && df_tr_of(0) == kt && kt->serial == ser && ser && kt->s.parried && g_df.n.flat == fl0 + 2 && g_df.bl_n == 0,
	       "flat: track %p (was %p) parried %d flat %llu", (void *)df_tr_of(0), (void *)kt, kt ? kt->s.parried : -1,
	       (unsigned long long)(g_df.n.flat - fl0));
	runtime_up();
	int dh1 = g_dfdh.n;
	df_run(0, 0.6f, -1, 100);
	df_impact_call(0);
	iframe();
	EXPECT(df_tr_of(0) == kt && g_df.n.tracks == tk0 + 1 && g_df.n.parries == pr0 + 1 && kt->outcome == SOMVR_DFO_PARRY_BLOCK &&
		       g_dfdh.n == dh1 && kt->s.lateral_cm < 1 && kt->s.dodge_t < 0,
	       "resumed: tracks %llu parries %llu outcome %d DealHit %d lateral %g", (unsigned long long)(g_df.n.tracks - tk0),
	       (unsigned long long)(g_df.n.parries - pr0), df_tr_of(0) ? df_tr_of(0)->outcome : -9, g_dfdh.n - dh1,
	       df_tr_of(0) ? df_tr_of(0)->s.lateral_cm : -1);
	atomic_store(&g_sh.xr.state, SOMVR_XRS_NONE);
	frame();
	g_cb_test_t += TH_FLAT_KEEP_S + 0.1;
	frame();
	EXPECT(!th_tr_of(0) && !df_tr_of(0) && g_df.bl_n == 0, "flat past %g s: threat %p defence %p", TH_FLAT_KEEP_S,
	       (void *)th_tr_of(0), (void *)df_tr_of(0));
	g_rt_head[0] = 0;
	runtime_up();
	th_attack(0, 0, 0, NULL);

	/* 11. SOMVR_DEFENCE_CLANK=recoil,late,stop,haptic (round 2) with the parry recoil: A's attack (g_recs[0]:
	 * CanInterrupt 0, CannotInterruptExceptions = ST_CAT_X) refuses its own record (no category; no light record in
	 * this world); the index (one frame: 6 records) finds PC_ST_INTERRUPTER of ST_CAT_X; the parry's recoil with it
	 * takes: the clank now (the crack 1.0 on the sword hand, the knock 0.45 on the other, no parry pulse; A frozen
	 * 120 ms by its own timer) */
	g_df.mode = DF_ON, g_df.parry = DFP_RECOIL, g_df.block = DFB_RECOIL;
	g_df.clank = SOMVR_DFC_RECOIL | SOMVR_DFC_LATE | SOMVR_DFC_STOP | SOMVR_DFC_HAPTIC, g_df.clank_s = 0.12f;
	memset(&g_df.ix, 0, sizeof g_df.ix);
	g_cat.count = 6;
	g_df_x_e[0] = dbent(H_ACAT, 0xa, 1, 0), g_df_x_v[0] = (uintptr_t)&g_df_cat_x;
	st_dbrec_t xr = { 1, 3, { 0 }, g_df_x_e, g_df_x_v, 0, "PC_ST_INTERRUPTER" };
	memcpy(g_recs[4], &xr, sizeof xr);
	g_df_recoil_need = (uintptr_t)g_recs[4], g_df_recoil_to = (uintptr_t)g_recs[3];
	for (int k = 0; k < 3; k++) {
		df_blade(0, 100, 340);
		iframe();
	}
	EXPECT(g_df.ix.state == 2 && g_df.ix.n == 1 && g_df.ix.npc == 1 && g_df.ix.e[0].rec == (uintptr_t)g_recs[4] &&
		       log_has("DEFENCE clank: the attack categories indexed: 1 categories (1 with a PC_ record) from 6 attack records"),
	       "clank: the index state %d categories %d (PC %d)", g_df.ix.state, g_df.ix.n, g_df.ix.npc);
	th_drain_haptics();
	uint64_t hs1 = g_cb.n.hs_on;
	int r1 = g_dfr.n;
	g_df.clank_marks = 1; /* SOMVR_DEFENCE_CLANK_MARKS: one mark (SOMVR_MARK is off here: counted only) */
	*df_byte(0, DF_C_WINDOW) = 1, *df_byte(0, DF_C_TYPE) = 0;
	th_attack(0, T, (uintptr_t)g_recs[0], TH_KT_INF);
	df_run(0, 0.47f, 0, 100);
	rp = hap_take(SOMVR_HAND_R, &q);
	float ra = q.amplitude;
	lp = hap_take(SOMVR_HAND_L, &q);
	EXPECT(g_dfr.n == r1 + 1 && g_dfr.atk == (uintptr_t)g_recs[4] && g_dfr.rec == (uintptr_t)&g_df_rpar &&
		       g_oc[0].attack == (uintptr_t)g_recs[3] && g_df.n.interrupters == 1 && g_df.n.interrupter_took == 1 &&
		       g_df.n.clanks == 1 && g_df.n.stops == 1 && g_cb.n.hs_on == hs1 + 1 && g_cb.hs.on &&
		       g_cb.hs.c == (uintptr_t)&g_oc[0] && rp && NEAR(ra, 1.0, 1e-3) && lp && NEAR(q.amplitude, 0.45, 1e-3) &&
		       log_has("(the interrupter: CanInterrupt 0, its category 'ST_CAT_X')") &&
		       log_has("-> its parry recoil (took)") && log_has("DEFENCE clank orc=") &&
		       log_has("for 120 ms (the parry's recoil)"),
	       "clank at the parry: recoils %d atk %#lx (want %#lx) attack now %#lx interrupters %llu took %llu clanks %llu "
	       "stops %llu hitstop %d pulses R %d (%g) L %d (%g)", g_dfr.n - r1, (unsigned long)g_dfr.atk,
	       (unsigned long)g_recs[4], (unsigned long)g_oc[0].attack, (unsigned long long)g_df.n.interrupters,
	       (unsigned long long)g_df.n.interrupter_took, (unsigned long long)g_df.n.clanks,
	       (unsigned long long)g_df.n.stops, g_cb.hs.on, rp, ra, lp, q.amplitude);
	EXPECT(g_df.clank_marks == 0, "clank: the mark asked (%d left)", g_df.clank_marks);
	EXPECT(log_has("DEFENCE attack '': AttackType 0, StrikeRange -, ApplyDamageRange -, half-angle -; recoils hit '' block "
		       "'ST_RECOIL_BLOCK' parry 'ST_RECOIL_PARRY'; CanInterrupt 0, CannotInterruptExceptions (can) 'ST_CAT_X'; the "
		       "light record 'ST_L") && log_has("(none) may NOT interrupt it"),
	       "clank: the attack's rules line");
	cb_hs_tick(g_cb_test_t + 1);
	EXPECT(!g_cb.hs.on && g_oobj[0].timer == &g_orc_timer, "clank: the freeze released (%d)", g_cb.hs.on);
	df_end_attack(0);
	for (int k = 0; k < 30; k++)
		iframe();
	th_drain_haptics();
	/* ... the list names ST_CAT_Y (no record of it): no interrupter, the parry's recoil refused with its RecoilTypeParry
	 * and then its RecoilTypeBlock (two calls), no pulse (the clank waits for the blades to meet); the impact: a
	 * parry-block, the block's recoil refused (two more) -> late, the clank then; A's attack ends: the late try takes
	 * (the stand-in lets any attack now) */
	g_df_cl_v[0] = (uintptr_t)&g_df_cat_y;
	int r2 = g_dfr.n;
	uint64_t cl0 = g_df.n.clanks, pb0 = g_df.n.parry_blocks;
	th_attack(0, T, (uintptr_t)g_recs[0], TH_KT_INF);
	df_run(0, 0.47f, 0, 100);
	rp = hap_take(SOMVR_HAND_R, &q);
	EXPECT(g_dfr.n == r2 + 2 && g_dfr.atk == (uintptr_t)g_recs[0] && g_dfr.rec == (uintptr_t)&g_df_rblk &&
		       g_oc[0].attack == (uintptr_t)g_recs[0] && !rp && g_df.n.clanks == cl0 &&
		       log_has("(no interrupter: CanInterrupt 0, 1 categories listed; the index complete)") &&
		       log_has("-> its parry recoil (no change)") && log_has("; tried first 'ST_RECOIL_PARRY' (Parry) not picked"),
	       "no interrupter: recoils %d atk %#lx pulse %d clanks %llu", g_dfr.n - r2, (unsigned long)g_dfr.atk, rp,
	       (unsigned long long)(g_df.n.clanks - cl0));
	df_run(0, 0.8f, 0, 100);
	df_impact_call(0);
	df_blade(1, 100, 340);
	iframe();
	rp = hap_take(SOMVR_HAND_R, &q);
	EXPECT(g_df.n.parry_blocks == pb0 + 1 && g_dfr.n == r2 + 4 && g_df.n.late == 1 && g_df.late[0].c == (uintptr_t)&g_oc[0] &&
		       g_df.n.clanks == cl0 + 1 && rp && NEAR(q.amplitude, 1.0, 1e-3) &&
		       log_has("its recoil: no change (late: tried again when its attack changes)") && log_has("(parry-block)"),
	       "the impact: parry-blocks %llu recoils %d late %llu clanks %llu pulse %d (%g)",
	       (unsigned long long)(g_df.n.parry_blocks - pb0), g_dfr.n - r2, (unsigned long long)g_df.n.late,
	       (unsigned long long)(g_df.n.clanks - cl0), rp, q.amplitude);
	cb_hs_tick(g_cb_test_t + 1);
	g_df_recoil_need = 0, g_df_recoil_to = (uintptr_t)g_recs[2];
	df_end_attack(0); /* its attack 0 now: the late try (the stand-in takes it) */
	EXPECT(g_df.n.late_took == 1 && g_oc[0].attack == (uintptr_t)g_recs[2] && !g_df.late[0].c &&
		       log_has("DEFENCE late recoil orc=") && log_has("ms after the block of ''"),
	       "late: took %llu attack %#lx", (unsigned long long)g_df.n.late_took, (unsigned long)g_oc[0].attack);
	/* a late entry the orc never allows: given up after 2 s */
	g_df_recoil_to = 0;
	df_late_add((uintptr_t)&g_oc[0], (uintptr_t)g_recs[0], (const float[3]){ 0, 0, 0 }, g_cb_test_t);
	g_oc[0].attack = (uintptr_t)g_recs[1];
	iframe();
	g_cb_test_t += 2.1;
	iframe();
	EXPECT(g_df.n.late_gave_up == 1 && !g_df.late[0].c && log_has("none within 2 s"), "late: given up %llu",
	       (unsigned long long)g_df.n.late_gave_up);
	/* the game's way (boot-check-20261024-042304): the picked recoil sets the orc's Combat+0x56a at once, its attack
	 * changes only at its next update; only RecoilTypeBlock has a node here: Parry not picked, Block picked = took,
	 * the clank at the parry (the attack still the strike) */
	g_oc[0].attack = 0;
	for (int k = 0; k < 30; k++)
		iframe();
	th_drain_haptics();
	g_df_recoil_need = 0, g_df_recoil_to = (uintptr_t)g_recs[2], g_df_recoil_rec = (uintptr_t)&g_df_rblk, g_df_recoil_defer = 1;
	((uint8_t *)&g_oc[0])[DF_C_RECOILED] = 0;
	uint64_t cl2 = g_df.n.clanks, tk2 = g_df.n.recoil_took;
	int r3 = g_dfr.n;
	th_attack(0, T, (uintptr_t)g_recs[0], TH_KT_INF);
	df_run(0, 0.47f, 0, 100);
	rp = hap_take(SOMVR_HAND_R, &q);
	EXPECT(g_dfr.n == r3 + 2 && g_dfr.rec == (uintptr_t)&g_df_rblk && g_oc[0].attack == (uintptr_t)g_recs[0] &&
		       ((uint8_t *)&g_oc[0])[DF_C_RECOILED] == 1 && g_df.n.recoil_took == tk2 + 1 && g_df.n.clanks == cl2 + 1 && rp &&
		       NEAR(q.amplitude, 1.0, 1e-3) && log_has("(took: its recoil picked (+0x56a)); tried first 'ST_RECOIL_PARRY' (Parry) "
							       "not picked"),
	       "deferred: recoils %d rec %#lx attack %#lx flag %u took %llu clanks %llu pulse %d", g_dfr.n - r3,
	       (unsigned long)g_dfr.rec, (unsigned long)g_oc[0].attack, ((uint8_t *)&g_oc[0])[DF_C_RECOILED],
	       (unsigned long long)(g_df.n.recoil_took - tk2), (unsigned long long)(g_df.n.clanks - cl2), rp);
	cb_hs_tick(g_cb_test_t + 1);
	g_df_recoil_rec = 0, g_df_recoil_defer = 0;
	((uint8_t *)&g_oc[0])[DF_C_RECOILED] = 0;
	df_end_attack(0);
	g_oc[0].attack = 0, g_df_recoil_need = 0, g_df_recoil_to = 0, g_df_cl_v[0] = (uintptr_t)&g_df_cat_x;
	g_df.clank = 0, g_df.parry = DFP_COUNTER;
	memset(g_recs[4], 0, sizeof g_recs[4]);
	for (int k = 0; k < 30; k++)
		iframe();
	th_drain_haptics();

	int bad = 0;
	g_cb.mode = CB_FULL;
	setenv("SOMVR_DEFENCE", "1", 1);
	setenv("SOMVR_DEFENCE_CLANK", "stop,spark", 1);
	df_start(g_dfs, &bad);
	EXPECT(g_df.clank == 0 && bad && log_has("WARN SOMVR_DEFENCE_CLANK=stop,spark: not a list") &&
		       log_has("CONFIG defence clank=off (SOMVR_DEFENCE_CLANK) freeze 120 ms"),
	       "clank: a bad list (%#x)", g_df.clank);
	setenv("SOMVR_DEFENCE_CLANK", "on", 1);
	setenv("SOMVR_DEFENCE_CLANK_MS", "90", 1);
	bad = 0;
	df_start(g_dfs, &bad);
	EXPECT(g_df.clank == SOMVR_DFC_ALL && NEAR(g_df.clank_s, 0.09, 1e-6) &&
		       log_has("CONFIG defence clank=recoil,late,stop,haptic,fx (SOMVR_DEFENCE_CLANK) freeze 90 ms, fx "
			       "'fx\\weapons\\shared\\impactsa\\impactsa_sparksmall_weapontip.fxp'"),
	       "clank: on (%#x, %g s)", g_df.clank, g_df.clank_s);
	unsetenv("SOMVR_DEFENCE_CLANK"), unsetenv("SOMVR_DEFENCE_CLANK_MS");
	g_df.clank = 0;
	g_th.on = 0;
	df_start(g_dfs, &bad);
	EXPECT(g_df.mode == DF_OFF && log_has("WARN SOMVR_DEFENCE needs SOMVR_COMBAT (on) and SOMVR_THREAT=1 (off"),
	       "without the threat: mode %d", g_df.mode);
	g_th.on = 1;
	struct site_spec badsites[DFS_N];
	memcpy(badsites, g_dfs, sizeof badsites);
	badsites[DFS_RECOIL].ctx_hex = "90 90 90 90";
	df_start(badsites, &bad); /* DF_IMPACT is ours already (patched): it does not match again either */
	EXPECT(g_df.mode == DF_ON && g_df.block == DFB_OFF && g_df.parry == DFP_COUNTER && !g_df.ok_recoil && !g_df.ok_impact &&
		       g_df.cfg.dodge_cm == 0 && log_has("WARN SOMVR_DEFENCE: DF_IMPACT is not hooked") &&
		       log_has("DEFENCE site DF_RECOIL") && log_has("block=off (no DF_IMPACT)"),
	       "bad sites: mode %d block %d parry %d recoil %d impact %d", g_df.mode, g_df.block, g_df.parry, g_df.ok_recoil,
	       g_df.ok_impact);
	unsetenv("SOMVR_DEFENCE");
	g_df.mode = DF_OFF;
	g_df_sites = g_dfs;
	g_cb.mode = CB_OFF;
	g_th = th_save;
	g_th.on = 0;
	g_inp.padmode = padmode0;
	*df_byte(0, DF_C_WINDOW) = 0, *df_byte(0, DF_C_TYPE) = 0;
	g_profile[0x238] = 1;
	memset(&g_cb.prev, 0, sizeof g_cb.prev);
	printf("defence: parry (counter pressed, countered; not taken = stopped and the recoil), block, hit, dodge, red, window "
	       "shut, two attackers, an arrow removed, log mode, off, flat, sites\n");
}

/* ---- SOMVR_FINISH (finish_tests): the read-only proofs and calls on stand-ins (the selection context, the attack
 * window, SharedCombat_SelectAttackFromPool with an explicit CBaseTarget, the player's attack start, the ClientFX glow on
 * the active weapon, SlowMO.Set), the database (the root record's Nodes, the attacks' AnimationData.Sync), a
 * PlayerCombat with room up to +0xb20, the streak at the server Combat's hit counter, the orcs' states; driven by the
 * fake blade's heavy cut through orc A (ahead, its back to Talion) and white-box frames for the dagger and the latch -- */
#define FI_H_NODES 0x0f1a0001u
#define FI_H_ANIM 0x0f1a0002u
#define FI_H_SYNC 0x0f1a0003u
static char g_fihex[FIS_N][256];
static struct site_spec g_fis[FIS_N];
static st_sch_t g_fis_ccr[2], g_fis_atk[1], g_fis_anim[9], g_fis_node[1];
static void *g_fis_p[4] = { g_fis_ccr, g_fis_node, g_fis_atk, g_fis_anim };
enum { FN_STRIKE, FN_EXEC, FN_DRAIN, FN_INTERROG, FN_N };
enum { FA_STRIKE, FA_SNEAK, FA_EXEC, FA_DRAIN, FA_INTERROG, FA_ANIM, FA_SYNC, FA_STUN, FA_KD, FA_N };
static const char *const g_fi_nn[FN_N] = { "PC_Node_Strike", "PC_Node_Execute", "PC_Node_Drain", "PC_Node_Interrogate" };
static const char *const g_fi_an[FA_N] = { "PC_HC_High_L1_D0_F_1", "PC_Bip_SneakInstaKill_B", "PC_Bip_Execution_F_1",
					    "PC_Bip_Drain_Stunned_F", "PC_Bip_Interrogate_F", "PC_Bip_Execution_F_1_Anim",
					    "PC_Bip_Sync_Execution_F", "Combat_State_Stunned", "Combat_State_KnockDown" };
static st_dbrec_t g_fir_root, g_fir_node[FN_N], g_fir_atk[FA_N];
static st_dbent_t g_fir_root_e[1], g_fir_exec_e[1], g_fir_anim_e[1];
static uint64_t g_fir_root_v[FN_N], g_fir_exec_v[1], g_fir_anim_v[1];
static union { st_pcomb_t p; unsigned char raw[0xb20]; } g_fipc;
static unsigned char g_fiplayer[0x500], g_fi_hold[0x100], g_fi_inv[0x40], g_fi_item[0x80], g_fi_ient[0x48], g_fi_wobj[0x40];
static unsigned char g_fihc[0x100], g_fi_proot[0x40];
static void *volatile g_fi_vtos[64];
static uintptr_t g_fi_os, g_fi_osp, g_fi_mgrp, g_fi_shellv;
static uint8_t g_fi_fxon = 1, g_fi_sever;
static uint32_t g_fi_tm[16];
static uintptr_t g_fi_t0[2], g_fi_tptr;
static struct {
	int ctx, ctxpc, window, select, tgtinit, tgtset, start, root, weapon, dirangle, slowmo, cs, create, csfree, release,
		item, kbm, mh, unaware, exec_ok, sync_on_start, kbm_pad, window_busy;
	uintptr_t start_pc, start_node, start_atk, start_tgt, ctx_combat, ctx_weapon, set_src, set_obj, cs_parent, cs_mh;
	float ctx_angle;
	uint8_t notme;
	unsigned cs_flags;
	char cs_name[160], slowmo_arg[2][32];
	uintptr_t list[2]; /* the target object's reference list head {next, prev} */
} g_fic;
static uintptr_t g_fi_vt_target[4];

__attribute__((noipa)) uint64_t st_fi_proof(uint64_t a) { return a * 5 + 3; } /* FI_HITCOUNT's stand-in bytes */
__attribute__((noipa)) void st_fi_ctx(void *ctx, uintptr_t combat, uintptr_t weapon)
{
	g_fic.ctx++, g_fic.ctx_combat = combat, g_fic.ctx_weapon = weapon;
	memcpy(ctx, &combat, 8);
}
__attribute__((noipa)) void st_fi_ctxpc(uintptr_t pcomb, void *ctx)
{
	float a = 99;
	g_fic.ctxpc += pcomb == (uintptr_t)&g_fipc;
	memcpy((char *)ctx + FI_CTX_ANGLE, &a, 4);
}
__attribute__((noipa)) uint64_t st_fi_window(uintptr_t window, uintptr_t node, int busy)
{
	g_fic.window += window == (uintptr_t)&g_fipc + FI_PC_WINDOW && node, g_fic.window_busy |= busy;
	return 1;
}
static int fi_streak_now(void) { int16_t v; memcpy(&v, g_fihc + FI_HC_VALUE, 2); return v; }
static int fi_orc_stunned(int i)
{
	uintptr_t st;
	memcpy(&st, (char *)&g_oc[i] + FI_C_STATE, 8);
	return st == (uintptr_t)&g_fir_atk[FA_STUN];
}
__attribute__((noipa)) uintptr_t st_fi_select(void *ctx, uintptr_t node, int flag, void *tgt, void *score)
{
	uintptr_t obj;
	memcpy(&obj, (char *)tgt + FI_TGT_OBJ, 8);
	g_fic.select++;
	memcpy(&g_fic.ctx_angle, (char *)ctx + FI_CTX_ANGLE, 4);
	g_fic.notme = ((unsigned char *)ctx)[FI_CTX_NOTME];
	if (flag != 1 || score)
		return 0;
	int a = obj == (uintptr_t)g_oc[0].hobj;
	if (node == (uintptr_t)&g_fir_node[FN_STRIKE])
		return (uintptr_t)&g_fir_atk[a && g_fic.unaware ? FA_SNEAK : FA_STRIKE];
	if (node == (uintptr_t)&g_fir_node[FN_EXEC])
		return g_fic.exec_ok && fi_streak_now() >= 8 ? (uintptr_t)&g_fir_atk[FA_EXEC] : 0;
	if (node == (uintptr_t)&g_fir_node[FN_DRAIN])
		return a && fi_orc_stunned(0) ? (uintptr_t)&g_fir_atk[FA_DRAIN] : 0;
	if (node == (uintptr_t)&g_fir_node[FN_INTERROG])
		return a && fi_orc_stunned(0) ? (uintptr_t)&g_fir_atk[FA_INTERROG] : 0;
	return 0;
}
__attribute__((noipa)) void st_fi_tgtinit(void *tgt)
{
	uintptr_t *p = tgt;
	g_fic.tgtinit++;
	memset(tgt, 0, FI_TGT_SIZE);
	p[0] = (uintptr_t)g_fi_vt_target, p[3] = p[4] = (uintptr_t)(p + 3), p[0xb] = p[0xc] = (uintptr_t)(p + 0xb);
}
__attribute__((noipa)) void st_fi_tgtset(void *tgt, uintptr_t src, uintptr_t obj, int flag)
{
	uintptr_t *p = tgt, *node = p + 0xb;
	g_fic.tgtset++, g_fic.set_src = src, g_fic.set_obj = obj;
	if (!obj || flag)
		return;
	/* as FUN_022806c0: the reference node goes into the object's list (here g_fic.list), the object at +0x68 */
	node[0] = (uintptr_t)g_fic.list, node[1] = g_fic.list[1];
	*(uintptr_t *)g_fic.list[1] = (uintptr_t)node;
	g_fic.list[1] = (uintptr_t)node;
	p[0xd] = obj;
}
__attribute__((noipa)) void st_fi_start(uintptr_t pcomb, uintptr_t node, uintptr_t atk, uintptr_t target)
{
	g_fic.start++, g_fic.start_pc = pcomb, g_fic.start_node = node, g_fic.start_atk = atk, g_fic.start_tgt = target;
	if (g_fic.sync_on_start)
		for (int i = 0; i < 4; i++)
			if ((uintptr_t)g_oc[i].hobj == target)
				g_oc[i].sync[0] = (void *)1;
}
__attribute__((noipa)) uintptr_t st_fi_root(uintptr_t player) { g_fic.root++; return player == (uintptr_t)g_fiplayer ? (uintptr_t)g_fi_proot : 0; }
__attribute__((noipa)) uintptr_t st_fi_weapon(uintptr_t player) { g_fic.weapon++; return player == (uintptr_t)g_fiplayer ? 0x5150 : 0; }
__attribute__((noipa)) uint64_t st_fi_dirangle(const void *xf, const float *dir, float *angle)
{
	g_fic.dirangle++;
	*angle = 0.5f;
	return xf && dir[1] == 0;
}
__attribute__((noipa)) void st_fi_slowmo(int argc, const char *const *argv)
{
	g_fic.slowmo++;
	snprintf(g_fic.slowmo_arg[0], 32, "%s", argv[0]);
	snprintf(g_fic.slowmo_arg[1], 32, "%s", argc > 1 ? argv[1] : "");
	if (argc == 1)
		g_fi_tm[8] = (uint32_t)(strtod(argv[0], NULL) * 65535), g_fi_tm[9] = 65535;
	else
		g_fi_tm[8] = (uint32_t)atoi(argv[0]), g_fi_tm[9] = (uint32_t)atoi(argv[1]);
}
__attribute__((noipa)) void st_fi_cs(void *cs, const char *name, uintptr_t mh, unsigned flags, uintptr_t parent)
{
	g_fic.cs++, g_fic.cs_mh = mh, g_fic.cs_flags = flags, g_fic.cs_parent = parent;
	snprintf(g_fic.cs_name, sizeof g_fic.cs_name, "%s", name);
	memset(cs, 0x11, 0x1c8);
}
__attribute__((noipa)) long st_fi_create(uintptr_t mgr, uintptr_t *handle, void *cs, int start, int b, int c)
{
	g_fic.create++;
	if (mgr != g_fi_mgrp + 0x30 || !cs || start != 1 || b || c) /* the manager at shell+0x80c78, created at +0x30 */
		return 0;
	if (handle)
		*handle = 0x7770 + (uintptr_t)g_fic.create;
	return 1;
}
__attribute__((noipa)) void st_fi_csfree(void *cs) { g_fic.csfree += cs != NULL; }
__attribute__((noipa)) void st_fi_release(uintptr_t *handle) { g_fic.release++, *handle = 0; }
__attribute__((noipa)) uintptr_t st_fi_mh(uintptr_t os, uintptr_t obj) { g_fic.mh++; return os == (uintptr_t)&g_fi_os ? obj + 1 : 0; }
__attribute__((noipa)) uintptr_t st_fi_item(uintptr_t inv) { g_fic.item++; return inv == (uintptr_t)g_fi_inv ? (uintptr_t)g_fi_item : 0; }
__attribute__((noipa)) uintptr_t st_fi_kbm(uintptr_t node, int pad) { g_fic.kbm++, g_fic.kbm_pad = pad; return node; }

static void fi_entry_site(void *fn, int i, const char *name)
{
	hex_of(fn, 8, g_fihex[i], 128);
	hex_of(fn, 4, g_fihex[i] + 128, 128);
	g_fis[i] = (struct site_spec){ name, K_ENTRY, (uintptr_t)fn, (uintptr_t)fn, g_fihex[i], (uintptr_t)fn, g_fihex[i] + 128 };
}
static void fi_stand_ins(void)
{
	static void *const fns[FIS_N] = { (void *)st_fi_proof, (void *)st_fi_ctx, (void *)st_fi_ctxpc, (void *)st_fi_window,
					  (void *)st_fi_select, (void *)st_fi_tgtinit, (void *)st_fi_tgtset, (void *)st_fi_start,
					  (void *)st_fi_root, (void *)st_fi_weapon, (void *)st_fi_dirangle, (void *)st_fi_slowmo,
					  (void *)st_fi_cs, (void *)st_fi_create, (void *)st_fi_csfree, (void *)st_fi_release, NULL,
					  (void *)st_fi_item, (void *)st_fi_kbm };
	for (int i = 0; i < FIS_N; i++)
		if (i != FIS_OBJSYS_MH)
			fi_entry_site(fns[i], i, g_fi_sites_game[i].name);
	g_fi_vtos[0x118 / 8] = (void *)st_fi_mh;
	hex_of((const unsigned char *)&g_fi_vtos[0x118 / 8], 8, g_fihex[FIS_OBJSYS_MH], 128);
	hex_of((const void *)st_fi_mh, 4, g_fihex[FIS_OBJSYS_MH] + 128, 128);
	g_fis[FIS_OBJSYS_MH] = (struct site_spec){ "FI_OBJSYS_MH", K_VSLOT, (uintptr_t)&g_fi_vtos[0x118 / 8],
						    (uintptr_t)&g_fi_vtos[0x118 / 8], g_fihex[FIS_OBJSYS_MH],
						    (uintptr_t)st_fi_mh, g_fihex[FIS_OBJSYS_MH] + 128 };
	/* the database: the schema holders, the root record (sorted entries: Nodes), the nodes, the attacks (the
	 * execution's AnimationData -> its Sync), the state records */
	g_fis_ccr[1].hash = FI_H_NODES, g_fis_atk[0].hash = FI_H_ANIM, g_fis_anim[8].hash = FI_H_SYNC;
	g_fig.sch_ccr = (uintptr_t)&g_fis_p[0], g_fig.sch_node = (uintptr_t)&g_fis_p[1];
	g_fig.sch_attack = (uintptr_t)&g_fis_p[2], g_fig.sch_anim = (uintptr_t)&g_fis_p[3];
	for (int i = 0; i < FN_N; i++) {
		g_fir_node[i] = (st_dbrec_t){ .flags = 2, .name = g_fi_nn[i] };
		g_fir_root_v[i] = (uint64_t)(uintptr_t)&g_fir_node[i];
	}
	for (int i = 0; i < FA_N; i++)
		g_fir_atk[i] = (st_dbrec_t){ .flags = 2, .name = g_fi_an[i] };
	g_fir_root_e[0] = (st_dbent_t){ .hash = FI_H_NODES, .type = 0xa, .count = FN_N, .slot = 0 };
	g_fir_root = (st_dbrec_t){ .n = 1, .flags = 3, .ents = g_fir_root_e, .vals = g_fir_root_v, .name = "PC_Talion_Combat" };
	g_fir_exec_e[0] = (st_dbent_t){ .hash = FI_H_ANIM, .type = 0xa, .count = 1, .slot = 0 };
	g_fir_exec_v[0] = (uint64_t)(uintptr_t)&g_fir_atk[FA_ANIM];
	g_fir_atk[FA_EXEC].n = 1, g_fir_atk[FA_EXEC].flags = 3, g_fir_atk[FA_EXEC].ents = g_fir_exec_e, g_fir_atk[FA_EXEC].vals = g_fir_exec_v;
	g_fir_anim_e[0] = (st_dbent_t){ .hash = FI_H_SYNC, .type = 0xa, .count = 1, .slot = 0 };
	g_fir_anim_v[0] = (uint64_t)(uintptr_t)&g_fir_atk[FA_SYNC];
	g_fir_atk[FA_ANIM].n = 1, g_fir_atk[FA_ANIM].flags = 3, g_fir_atk[FA_ANIM].ents = g_fir_anim_e, g_fir_atk[FA_ANIM].vals = g_fir_anim_v;
	/* the objects: the object system, the shell's FX manager, the ClientFX byte, the sever byte, the game timer, the
	 * player (+0x420 -> +0xe8 the inventory), the active item and its model object, the hit counter */
	g_fi_os = (uintptr_t)g_fi_vtos, g_fi_osp = (uintptr_t)&g_fi_os;
	g_fi_mgrp = 0x4242;
	g_fi_shellv = (uintptr_t)&g_fi_mgrp - FI_SH_FXMGR;
	g_fig.objsys_pp = (uintptr_t)&g_fi_osp, g_fig.vt_objsys = (uintptr_t)g_fi_vtos;
	g_fig.shell_pp = (uintptr_t)&g_fi_shellv, g_fig.fx_on = (uintptr_t)&g_fi_fxon, g_fig.sever = (uintptr_t)&g_fi_sever;
	g_fi_t0[1] = (uintptr_t)g_fi_tm, g_fi_tptr = (uintptr_t)g_fi_t0, g_fig.timer_pp = (uintptr_t)&g_fi_tptr;
	g_fig.vt_target = (uintptr_t)g_fi_vt_target;
	uintptr_t v = (uintptr_t)g_fi_hold;
	memcpy(g_fiplayer + FI_P_INVHOLD, &v, 8);
	v = (uintptr_t)g_fi_inv, memcpy(g_fi_hold + FI_INV_OF, &v, 8);
	v = (uintptr_t)g_fi_ient, memcpy(g_fi_item + FI_IT_ENTRIES, &v, 8);
	v = (uintptr_t)g_fi_ient + 0x48, memcpy(g_fi_item + FI_IT_ENTRIES + 8, &v, 8);
	v = (uintptr_t)g_fi_wobj, memcpy(g_fi_ient + FI_IE_OBJ, &v, 8);
}
static void fi_set_streak(int n)
{
	int16_t v = (int16_t)n;
	uintptr_t hc = (uintptr_t)g_fihc;
	memcpy(g_fihc + FI_HC_VALUE, &v, 2);
	memcpy((char *)&g_pcs + FI_C_HITCTR, &hc, 8);
}
static void fi_set_state(int i, int rec) /* rec: FA_STUN, FA_KD or -1 = none */
{
	uintptr_t r = rec < 0 ? 0 : (uintptr_t)&g_fir_atk[rec];
	memcpy((char *)&g_oc[i] + FI_C_STATE, &r, 8);
}
static void fi_world(void) /* cb_world, then the bigger PlayerCombat and the finish state */
{
	cb_world();
	memset(&g_fipc, 0, sizeof g_fipc);
	memcpy(&g_fipc, &g_pcomb, sizeof g_pcomb);
	uintptr_t v = (uintptr_t)g_fiplayer;
	memcpy(g_fipc.raw + FI_PC_PLAYER, &v, 8);
	g_fipc.raw[FI_PC_PAD] = 1;
	v = (uintptr_t)&g_fir_root, memcpy(g_fipc.raw + FI_PC_ROOT, &v, 8);
	g_player.pcomb = &g_fipc.p;
	g_fic.list[0] = g_fic.list[1] = (uintptr_t)g_fic.list;
	fi_set_streak(0);
	for (int i = 0; i < 4; i++)
		fi_set_state(i, -1);
	g_pobj.pos[0] = 500, g_pobj.pos[1] = 100, g_pobj.pos[2] = -50, g_pobj.rot[1] = 0, g_pobj.rot[3] = 1;
	g_pl = &g_player;
	g_cb.mode = CB_FULL, g_cb.blade = CB_BLADE_FAKE, g_cb.warned = 0, g_cb.prev.valid = 0;
	sc_speed_reset(&g_cb.sp);
	sc_cool_reset(&g_cb.cool);
	sc_charges_init(&g_cb.ch, &g_cb.cfg, g_cb_test_t);
	g_cb.t0 = g_cb_test_t;
	g_cat.count = 6;
	g_fi_tm[8] = g_fi_tm[9] = 1000;
	g_fi_sever = 0;
}
static const char *const fi_env[] = { "SOMVR_FINISH", "SOMVR_FINISH_PATH", "SOMVR_FINISH_FALLBACK", "SOMVR_FINISH_SLOWMO",
				      "SOMVR_FINISH_HEAD", "SOMVR_FINISH_NECK_CM", "SOMVR_FINISH_NAMES_EXEC", "SOMVR_FINISH_STAB",
				      "SOMVR_FINISH_GLOW_FX" };
/* one start with these words (the rest unset), the neck at the fake blade's height (orc A's root 100 + 40 = 140; the
 * fake's blade is level at the head - 30 = 142.5) */
static void fi_go(const char *mode, const char *path, const char *slowmo)
{
	for (size_t i = 0; i < sizeof fi_env / sizeof fi_env[0]; i++)
		unsetenv(fi_env[i]);
	if (mode)
		setenv("SOMVR_FINISH", mode, 1);
	if (path)
		setenv("SOMVR_FINISH_PATH", path, 1);
	if (slowmo)
		setenv("SOMVR_FINISH_SLOWMO", slowmo, 1);
	setenv("SOMVR_FINISH_HEAD", "estimate", 1);
	setenv("SOMVR_FINISH_NECK_CM", "40", 1);
	int bad = 0;
	fi_start(g_fis, &bad);
}
static void fi_cycle(void)
{
	for (int i = 0; i < (int)(SC_FAKE_PERIOD * 90 + 0.5); i++)
		cb_frame();
}
static void fi_line_blade(const float base[3], const float dir[3], float len, const float vel[3], double t,
			  somvr_blade *b)
{
	memset(b, 0, sizeof *b);
	b->valid = 1, b->t = t;
	for (int k = 0; k < 3; k++)
		b->base[k] = base[k], b->tip[k] = base[k] + len * dir[k], b->vel_tip[k] = vel[k];
}
static int fi_list_empty(void) { return g_fic.list[0] == (uintptr_t)g_fic.list && g_fic.list[1] == (uintptr_t)g_fic.list; }
/* a white-box frame for the dagger and the left hand: this frame's hand data, then combat_frame */
static void fi_hand_frame(const somvr_blade *dprev, const somvr_blade *dag, float axis_mps, float trigger, float squeeze,
			  const float pos[3])
{
	g_cb_test_t += 1.0 / 90;
	if (dprev)
		g_fi.dprev = *dprev;
	if (dag)
		g_fi.dag = *dag;
	else
		g_fi.dag.valid = 0;
	g_fi.dsp.axis_mps = axis_mps, g_fi.dsp.cos_axis = 0.95f, g_fi.dsp.mean_mps = axis_mps;
	g_fi.lh_valid = 1, g_fi.lh_trigger = trigger, g_fi.lh_squeeze = squeeze, g_fi.lh_upper = 0;
	if (pos)
		memcpy(g_fi.lh_pos, pos, sizeof g_fi.lh_pos);
	g_fi.lh_frame = g_fr.frame + 1;
	combat_frame((uintptr_t)&g_player, 0);
}

/* SOMVR_VERBS (build 13): the left hand's squeeze on an orc's head -> B (120) held through the input's press path while
 * the grip stays closed; the climb told; the game's hold's end lets B go; the test words; the prompts' words */
static st_dbrec_t g_vb_hold = { .flags = 2, .name = "PC_HoG_Standing_Hold_Normal" },
		  g_vb_done = { .flags = 2, .name = "PC_HoG_Standing_Complete" };
/* build 24: the stand-in for the input mode's "is this command enabled" (its vtable slot 3) */
static int st_brcmd_val;
static uint32_t st_brcmd_last;
static uint8_t st_brcmd_on(uintptr_t mode, uint32_t cmd)
{
	(void)mode;
	st_brcmd_last = cmd;
	return (uint8_t)st_brcmd_val;
}
/* build 24: the stand-in for the game's sink write (SOMVR_VERBS_BRAND_NODELOG's call) */
static int *st_nl_writes;
static void st_nl_game_write(uintptr_t sink, const char *text)
{
	(void)sink, (void)text;
	if (st_nl_writes)
		(*st_nl_writes)++;
}

static void vb_hand_at2(float x, float y, float z, float sq, float tr)
{
	sv_vec3 pw[2] = { { x, y, z }, { 0, 0, 0 } }, eye = { 500, 176, -50 };
	g_inp.hands[SOMVR_HAND_L].valid = 1;
	g_inp.st.hand[SOMVR_HAND_L].squeeze = sq, g_inp.st.hand[SOMVR_HAND_L].trigger = tr;
	g_inp.st.hand[SOMVR_HAND_L].active |= SOMVR_HACT_INPUT;
	g_fr.frame++;
	vb_hands(pw, eye);
}
static void vb_hand_at(float x, float y, float z, float sq) { vb_hand_at2(x, y, z, sq, 0); }
static int vb_has(const int *out, int n, int cmd)
{
	for (int k = 0; k < n; k++)
		if (out[k] == cmd)
			return 1;
	return 0;
}
static void vb_tests(void)
{
	int bad = 0, out[4];
	unsetenv("SOMVR_VERBS");
	vb_start(&bad);
	EXPECT(g_vb.mode == VB_OFF && vb_press_cmds(out) == 0 && !strcmp(vb_word(" status"), "verbs (SOMVR_VERBS is off: ignored)") &&
		       !vb_trigger_taken(),
	       "verbs: off by default");
	cb_world();
	g_cb_test_t = 12000;
	int inp = g_inp.on, ctx = g_inp.ctx;
	g_inp.on = 1, g_inp.ctx = SC_CTX_WORLD;
	setenv("SOMVR_VERBS", "on", 1);
	g_cb.mode = CB_OFF;
	vb_start(&bad);
	EXPECT(g_vb.mode == VB_OFF && log_has("WARN SOMVR_VERBS=on needs SOMVR_COMBAT"), "verbs: on without combat: off");
	g_cb.mode = CB_FULL;
	vb_start(&bad);
	EXPECT(g_vb.mode == VB_ON && log_has("CONFIG verbs mode=on test=0: the left hand's squeeze on an orc's head"),
	       "verbs: on (%d)", g_vb.mode);
	g_pcs.attack = 0;
	cb_frame(); /* the heads: orc A 110 cm ahead (its root 500,100,60) */
	EXPECT(g_vb.nheads >= 1 && g_vb.head_c[0] == (uintptr_t)&g_oc[0] && g_vb.heads[0][1] > g_oobj[0].pos[1] + 50 &&
		       g_vb.heads[0][1] < g_oobj[0].pos[1] + 90 && g_vb.heads_frame == g_fr.frame,
	       "verbs: the near orcs' heads (%d, %.1f)", g_vb.nheads, g_vb.nheads ? g_vb.heads[0][1] : 0);
	float hy = g_vb.heads[0][1];
	g_vb.heads_frame = g_fr.frame + 1; /* (vb_hand_at steps the frame) */
	vb_hand_at(500, 120, 0, 1); /* a squeeze at the hip: nothing */
	EXPECT(!g_vb.press_grab && vb_press_cmds(out) == 0 && !vb_nograb(), "verbs: a squeeze at the hip: nothing");
	vb_hand_at(500, 120, 0, 0);
	g_cb_test_t += 0.5;
	g_vb.heads_frame = g_fr.frame + 1;
	vb_hand_at(502, hy - 5, 52, 1); /* on A's head: the grab (123), through the deny list's press path */
	int n = vb_press_cmds(out);
	EXPECT(g_vb.press_grab && !g_vb.press_b && n == 1 && out[0] == VB_CMD_GRAB && vb_nograb() && vb_trigger_taken() &&
		       g_vb.held_c == (uintptr_t)&g_oc[0] && log_has("grab: the left hand on orc"),
	       "verbs: the squeeze on A's head: the grab held (%d %d)", n, n ? out[0] : 0);
	vb_hand_at2(530, 140, 20, 0.6f, 1); /* the trigger while holding: B too */
	n = vb_press_cmds(out);
	EXPECT(n == 2 && vb_has(out, n, VB_CMD_GRAB) && vb_has(out, n, VB_CMD_B) && log_has("wraith: the left hand holding orc"),
	       "verbs: the trigger while holding: 123 + B (%d)", n);
	g_pcs.attack = (uintptr_t)&g_vb_hold; /* the game's hold runs */
	cb_frame();
	g_cb_test_t += 0.3;
	vb_hand_at2(530, 140, 20, 0.6f, 1);
	EXPECT(g_vb.press_b && g_vb.saw_hog && g_vb.hog_starts == 1 && log_has("the game: Talion's 'PC_HoG_Standing_Hold_Normal'"),
	       "verbs: B held through the game's hold (%d %d)", g_vb.press_b, g_vb.saw_hog);
	g_pcs.attack = (uintptr_t)&g_vb_done; /* the game's hold completes: B let go, the grab still held */
	cb_frame();
	vb_hand_at2(530, 140, 20, 1, 1);
	n = vb_press_cmds(out);
	EXPECT(!g_vb.press_b && g_vb.press_grab && n == 1 && out[0] == VB_CMD_GRAB && g_vb.hog_completes == 1 &&
		       g_vb.ev[SOMVR_VBE_DONE] == 1 && log_has("done after"),
	       "verbs: the game's end: B let go, the grab held (%d %d)", g_vb.press_b, n);
	/* the interrogation: the held orc pulled to the face (the eyes 500,176,-50) */
	g_cb_test_t += 0.1;
	vb_hand_at2(500, 172, -25, 1, 0); /* 25 cm from the eyes */
	g_cb_test_t += 0.3;
	vb_hand_at2(500, 172, -25, 1, 0);
	n = vb_press_cmds(out);
	EXPECT(vb_has(out, n, VB_CMD_A) && vb_has(out, n, VB_CMD_GRAB) && g_vb.a_taps == 1 && log_has("ask: orc"),
	       "verbs: pulled to the face: A tapped (%d)", n);
	g_cb_test_t += 0.3;
	n = vb_press_cmds(out);
	EXPECT(!vb_has(out, n, VB_CMD_A), "verbs: A's tap over");
	vb_hand_at2(530, 140, 20, 0, 0); /* the grip opened: let go */
	EXPECT(!g_vb.press_grab && vb_press_cmds(out) == 0 && !vb_trigger_taken() && log_has("release after"),
	       "verbs: the grip opened: the grab let go");
	g_pcs.attack = 0;
	cb_frame();
	/* the wraith hand with the open hand on a head: B (a tap = the game's stun) */
	g_cb_test_t += 1.0;
	g_vb.heads_frame = g_fr.frame + 1;
	vb_hand_at2(502, hy - 5, 52, 0, 1);
	n = vb_press_cmds(out);
	EXPECT(n == 1 && out[0] == VB_CMD_B && vb_trigger_taken() && g_vb.ev[SOMVR_VBE_WRAITH] == 2, "verbs: the open hand's trigger "
	       "on A's head: B (%d)", n);
	g_cb_test_t += 0.1;
	vb_hand_at2(502, hy - 5, 52, 0, 0);
	EXPECT(vb_press_cmds(out) == 0 && g_vb.ev[SOMVR_VBE_WRAITH_END] == 1 && !vb_trigger_taken(), "verbs: let go: B let go");
	/* build 23, SOMVR_VERBS_BRAND (somvr_brand_game.c): the game's offer at the trigger's press = the brand's press
	 * instead of B held (the offer itself is the game's selection, gc_pick: not in the selftest - set here) */
	unsetenv("SOMVR_VERBS_BRAND");
	br_start(&bad);
	EXPECT(g_br.mode == BR_OFF && !strcmp(br_word(" status"), "verbs brand (SOMVR_VERBS_BRAND is off: ignored)"),
	       "brand: off by default");
	g_br.mode = BR_ON, g_br.stealth_tap = 1, g_br.press_s = 0.15f, g_br.fresh_s = 0.3f;
	uintptr_t oa_c = (uintptr_t)&g_oc[0];
	g_cb_test_t += 1.0;
	g_vb.heads_frame = g_fr.frame + 1;
	vb_hand_at2(502, hy - 5, 52, 0, 0); /* the open hand on A's head: the orc asked about */
	EXPECT(g_br.reach_c == oa_c && !g_br.reach_held, "brand: the hand on A's head is the orc asked (%#lx)",
	       (unsigned long)g_br.reach_c);
	g_br.ask_c = oa_c, g_br.ask_kind = BRK_COMBAT, g_br.ask_t = g_cb_test_t - 0.05;
	snprintf(g_br.ask_atk, sizeof g_br.ask_atk, "PC_InstaHOG_F");
	g_vb.heads_frame = g_fr.frame + 1;
	vb_hand_at2(502, hy - 5, 52, 0, 1); /* the trigger: the combat brand offered = the chord */
	n = vb_press_cmds(out);
	EXPECT(n == 2 && vb_has(out, n, VB_CMD_B) && vb_has(out, n, VB_CMD_A) && g_br.chords == 1 && !g_vb.press_b &&
		       log_has("VERBS brand t=") && log_has("the chord B + A (120 + 121)"),
	       "brand: the combat brand offered: B + A at the press (%d)", n);
	g_cb_test_t += 0.2;
	g_vb.heads_frame = g_fr.frame + 1;
	vb_hand_at2(502, hy - 5, 52, 0, 1);
	EXPECT(vb_press_cmds(out) == 0, "brand: the chord's 150 ms over, the trigger still held: no B");
	vb_hand_at2(502, hy - 5, 52, 0, 0);
	g_cb_test_t += 1.0; /* the combat brand offered, but Talion hit meanwhile (a recoil): B held as before, no chord */
	g_br.ask_t = g_cb_test_t - 0.05, g_br.talion_recoil = 1;
	g_vb.heads_frame = g_fr.frame + 1;
	vb_hand_at2(502, hy - 5, 52, 0, 1);
	n = vb_press_cmds(out);
	EXPECT(n == 1 && out[0] == VB_CMD_B && g_br.chords == 1 && log_has("but Talion is in a recoil now"),
	       "brand: Talion in a recoil at the press: B as before (%d)", n);
	g_br.talion_recoil = 0;
	g_cb_test_t += 0.2;
	vb_hand_at2(502, hy - 5, 52, 0, 0);
	g_cb_test_t += 1.0; /* an old answer: B held as before */
	g_br.ask_t = g_cb_test_t - 1.0;
	g_vb.heads_frame = g_fr.frame + 1;
	vb_hand_at2(502, hy - 5, 52, 0, 1);
	n = vb_press_cmds(out);
	EXPECT(n == 1 && out[0] == VB_CMD_B && g_br.chords == 1, "brand: an answer 1 s old: B held as before (%d)", n);
	g_cb_test_t += 0.2;
	vb_hand_at2(502, hy - 5, 52, 0, 0);
	g_cb_test_t += 1.0; /* the stealth brand offered: B as a 150 ms tap */
	g_br.ask_kind = BRK_STEALTH, g_br.ask_t = g_cb_test_t;
	g_vb.heads_frame = g_fr.frame + 1;
	vb_hand_at2(502, hy - 5, 52, 0, 1);
	n = vb_press_cmds(out);
	EXPECT(n == 1 && out[0] == VB_CMD_B && g_br.taps == 1 && log_has("B (120) tapped") && !atomic_load(&g_br.unblock_cmd),
	       "brand: the stealth brand: B's tap, nothing let go (%d)", n);
	g_cb_test_t += 0.2;
	g_vb.heads_frame = g_fr.frame + 1;
	vb_hand_at2(502, hy - 5, 52, 0, 1);
	EXPECT(vb_press_cmds(out) == 0, "brand: the stealth brand's tap over: no B held");
	vb_hand_at2(502, hy - 5, 52, 0, 0);
	/* the game's blocking command held by one of our holds (the crouch's 125): let go for the press, then back */
	int niv0 = g_inp.niv;
	g_inp.iv[g_inp.niv++] = (struct in_iv){ 0, 125, 0, 1.0f, 1, 1, 0, 0, 0 };
	g_cb_test_t += 1.0;
	g_br.ask_kind = BRK_STEALTH, g_br.ask_t = g_cb_test_t;
	g_br.unblock = 1, g_br.block_cmd = 125, g_br.block_held = 1, g_br.block_t = g_cb_test_t;
	g_vb.heads_frame = g_fr.frame + 1;
	vb_hand_at2(502, hy - 5, 52, 0, 1);
	EXPECT(g_br.taps == 2 && br_unblock_now(g_cb_test_t) == 125 && g_br.unblocks == 1 && log_has("is held by us: let go for 200 ms"),
	       "brand: the game's blocking command of ours let go at the press (%d)", br_unblock_now(g_cb_test_t));
	EXPECT(br_unblock_now(g_cb_test_t + 0.25) == 0, "brand: ... and back after 200 ms");
	g_inp.niv = niv0;
	g_cb_test_t += 0.3;
	vb_hand_at2(502, hy - 5, 52, 0, 0);
	g_br.block_held = 0;
	g_br.mode = BR_LOG; /* log: the line only, B held as before */
	g_cb_test_t += 1.0;
	g_br.ask_t = g_cb_test_t, g_br.ask_kind = BRK_COMBAT;
	g_vb.heads_frame = g_fr.frame + 1;
	vb_hand_at2(502, hy - 5, 52, 0, 1);
	n = vb_press_cmds(out);
	EXPECT(n == 1 && out[0] == VB_CMD_B && g_br.chords == 1 && atomic_load(&g_br.req_c) == oa_c,
	       "brand: log mode: B held, the press's line asked (%d)", n);
	g_cb_test_t += 0.2;
	vb_hand_at2(502, hy - 5, 52, 0, 0);
	g_br.mode = BR_OFF;
	atomic_store(&g_br.req_c, 0);
	{ /* build 24, SOMVR_VERBS_BRAND_NODELOG: the game's node log lines, once per node and again when one changes; the
	   * byte and the sink set for the window and put back (stand-ins: our own byte and sink; the call not patched) */
		static uint8_t nl_flag;
		static uintptr_t nl_sink;
		static int nl_game_writes;
		unsetenv("SOMVR_VERBS_BRAND_NODELOG");
		br_nl_start(g_br_sites_game, (uintptr_t)&nl_flag, (uintptr_t)&nl_sink, &bad);
		EXPECT(!g_brnl.ok && !g_brnl.want, "brand node log: off by default (nothing patched)");
		static struct site_spec nl_site;
		nl_site = g_br_sites_game[0], nl_site.target = (uintptr_t)st_nl_game_write;
		g_br_sites = &nl_site;
		st_nl_writes = &nl_game_writes;
		g_brnl.ok = 1, g_brnl.flag_a = (uintptr_t)&nl_flag, g_brnl.sink_a = (uintptr_t)&nl_sink;
		g_brnl.dummy = calloc(2, sizeof *g_brnl.dummy);
		snprintf(g_brnl.filter, sizeof g_brnl.filter, "PC_");
		double t0 = g_cb_test_t;
		br_nl_open(t0, 1.0);
		br_nl_frame(t0);
		EXPECT(nl_flag == 1 && nl_sink == (uintptr_t)g_brnl.dummy && log_has("VERBS brand node log t=") && log_has(": open for 1.0 s"),
		       "brand node log: the window sets the byte and our sink");
		uintptr_t dm = (uintptr_t)g_brnl.dummy;
		somvr_w_brnodelog(dm, "12.345 [COMBAT|NODE]: Node Rejected 'PC_StealthBrand', InputType (Press)\n");
		somvr_w_brnodelog(dm, "12.362 [COMBAT|NODE]: Node Rejected 'PC_StealthBrand', InputType (Press)\n");
		somvr_w_brnodelog(dm, "12.362 [COMBAT|NODE]: Node Rejected 'AI_Bip_Something', InputType (Press)\n");
		EXPECT(g_brnl.logged == 1 && g_brnl.kept == 2 && g_brnl.lines == 3 &&
			       log_has("f+0 Node Rejected 'PC_StealthBrand', InputType (Press)") && !log_has("AI_Bip_Something"),
		       "brand node log: a node's line once, others' filtered (%llu %llu)", (unsigned long long)g_brnl.logged,
		       (unsigned long long)g_brnl.kept);
		somvr_w_brnodelog(dm, "12.380 [COMBAT|NODE]: Node Rejected 'PC_StealthBrand', IgnoreIfBlocking\n");
		somvr_w_brnodelog(dm, "12.380 [COMBAT|NODE]: Root node validated: PC_Stun_Root\n");
		EXPECT(g_brnl.logged == 3 && log_has("IgnoreIfBlocking (before: 'Node Rejected 'PC_StealthBrand', InputType (Press)' 2 "
							"time(s)") && log_has("Root node validated: PC_Stun_Root") && g_brnl.nnodes == 2,
		       "brand node log: a changed line with the one before and its count; the node loop's own");
		somvr_w_brnodelog((uintptr_t)&nl_sink, "the game's own file");
		EXPECT(nl_game_writes == 1 && g_brnl.logged == 3, "brand node log: another sink's write is the game's");
		br_nl_frame(t0 + 2.0);
		EXPECT(nl_flag == 0 && nl_sink == 0 && !g_brnl.on && log_has("closed after 2.0 s") &&
			       log_has("the brand nodes' last lines: 'Node Rejected 'PC_StealthBrand', IgnoreIfBlocking' 1 x"),
		       "brand node log: the window's end puts the byte and the sink back, the brand nodes' runs");
		free(g_brnl.dummy);
		memset(&g_brnl, 0, sizeof g_brnl);
		g_br_sites = g_br_sites_game;
	}
	{ /* build 24: the game's combat commands off (the input mode's slot 3, FUN_02271890's first test): a line on each
	   * change, a brand press then says the game takes none (stand-ins: our mode object, vtable and slot) */
		static uintptr_t mvt[4], mobj[1], pcb[0x20];
		mvt[3] = (uintptr_t)st_brcmd_on, mobj[0] = (uintptr_t)mvt, pcb[0x80 / 8] = (uintptr_t)mobj;
		uintptr_t lo0 = g_br.text_lo, hi0 = g_br.text_hi;
		g_br.text_lo = (uintptr_t)st_brcmd_on, g_br.text_hi = (uintptr_t)st_brcmd_on + 1;
		g_br.b_on = -1, g_br.locks = g_br.lock_presses = 0;
		st_brcmd_val = 1;
		br_b_on_frame((uintptr_t)pcb, g_cb_test_t);
		EXPECT(g_br.b_on == 1 && st_brcmd_last == VB_CMD_B && !log_has("the game's combat commands on again"),
		       "brand: the game's B enabled read (%d, cmd %u)", g_br.b_on, st_brcmd_last);
		st_brcmd_val = 0;
		br_b_on_frame((uintptr_t)pcb, g_cb_test_t + 0.05);
		EXPECT(g_br.b_on == 0 && g_br.locks == 1 && log_has("the game's combat commands OFF (B 120 not enabled"),
		       "brand: the game's combat commands off: a line");
		g_br.mode = BR_ON, g_br.stealth_tap = 1;
		g_cb_test_t += 1.0;
		g_br.ask_c = oa_c, g_br.ask_kind = BRK_STEALTH, g_br.ask_t = g_cb_test_t;
		g_vb.heads_frame = g_fr.frame + 1;
		vb_hand_at2(502, hy - 5, 52, 0, 1);
		EXPECT(g_br.lock_presses == 1 && log_has("the game has its combat commands off now"),
		       "brand: a press in the game's lock says so (%llu)", (unsigned long long)g_br.lock_presses);
		g_cb_test_t += 0.2;
		vb_hand_at2(502, hy - 5, 52, 0, 0);
		st_brcmd_val = 1;
		br_b_on_frame((uintptr_t)pcb, g_cb_test_t);
		EXPECT(g_br.b_on == 1 && log_has("the game's combat commands on again") && log_has("... off for 1.2 s"),
		       "brand: the game's combat commands back: a line with the time off");
		uintptr_t far_vt[4] = { 0, 0, 0, 0x10 }; /* a slot outside the game's code: not called, not read */
		mobj[0] = (uintptr_t)far_vt;
		br_b_on_frame((uintptr_t)pcb, g_cb_test_t + 0.1);
		EXPECT(g_br.b_on == -1 && log_has("the game's combat commands not readable"), "brand: a slot outside the code: not called");
		g_br.text_lo = lo0, g_br.text_hi = hi0, g_br.b_on = -1, g_br.mode = BR_OFF;
		atomic_store(&g_br.req_c, 0);
	}
	/* the trigger elsewhere: the bow's */
	g_cb_test_t += 1.0;
	g_vb.heads_frame = g_fr.frame + 1;
	vb_hand_at2(450, 100, -50, 0, 1);
	EXPECT(vb_press_cmds(out) == 0 && !vb_trigger_taken(), "verbs: the trigger away from the orcs: nothing (the bow's)");
	vb_hand_at2(450, 100, -50, 0, 0);
	/* a fist moved onto the head: nothing */
	g_cb_test_t += 1.0;
	g_vb.heads_frame = g_fr.frame + 1;
	vb_hand_at(500, 120, 0, 1);
	g_vb.heads_frame = g_fr.frame + 1;
	vb_hand_at(502, hy - 5, 52, 1);
	EXPECT(!g_vb.press_grab && g_vb.ev[SOMVR_VBE_GRAB] == 1, "verbs: a fist moved onto the head: nothing");
	vb_hand_at(502, hy - 5, 52, 0);
	/* reached out at A ~1 m off: the eyes 500,176,-50, the palm 55 cm out toward A's head */
	g_cb_test_t += 1.0;
	g_vb.heads_frame = g_fr.frame + 1;
	vb_hand_at(500, 176 + (hy - 176) * 0.5f, 5, 1);
	EXPECT(g_vb.press_grab && g_vb.ev[SOMVR_VBE_GRAB] == 2 && log_has("grab: the left hand reached out at orc"),
	       "verbs: reached out at A: a grab");
	/* the sword on the held orc: X instead of the hit */
	struct cb_orc oa = { (uintptr_t)&g_oc[0], (uintptr_t)&g_oobj[0], { 500, 100, 60 }, 110 };
	EXPECT(vb_contact((uintptr_t)&g_pcomb, (uintptr_t)&g_pcs, &oa, SC_TIER_LIGHT, g_cb_test_t) == 1 &&
		       vb_press_cmds(out) >= 2 && vb_has(out, 2, VB_CMD_X) && g_vb.flurries == 1 && log_has("kill: the blade on the held orc"),
	       "verbs: the sword on the held orc: X");
	EXPECT(vb_contact((uintptr_t)&g_pcomb, (uintptr_t)&g_pcs, &oa, SC_TIER_NONE, g_cb_test_t) == 0, "verbs: a glance: not ours");
	{ /* the held orc's own sweep: in the bands = combat's loop has it (nothing here); no blade history = no contact */
		somvr_blade bb;
		memset(&bb, 0, sizeof bb);
		double bt0 = g_vb.blade_t;
		vb_blade((uintptr_t)&g_pcomb, (uintptr_t)&g_pcs, &oa, 1, &bb, 1, NULL, g_cb_test_t + 1);
		g_cb.prev.valid = 0;
		vb_blade((uintptr_t)&g_pcomb, (uintptr_t)&g_pcs, NULL, 0, &bb, 1, NULL, g_cb_test_t + 1);
		EXPECT(g_vb.blade_t == bt0, "verbs: the held orc's sweep: in the bands / no swept blade: no contact");
	}
	g_inp.ctx = SC_CTX_MENU; /* a menu while held: lost */
	vb_hand_at(500, 176, 5, 1);
	EXPECT(!g_vb.press_grab && g_vb.ev[SOMVR_VBE_LOST] == 1, "verbs: a menu while held: let go");
	g_inp.ctx = SC_CTX_WORLD;
	vb_hand_at(500, 176, 5, 0);
	/* stale heads (no combat frame for 3 frames): nothing to grab */
	g_cb_test_t += 1.0;
	g_vb.heads_frame = g_fr.frame - 3;
	vb_hand_at(502, hy - 5, 52, 1);
	EXPECT(!g_vb.press_grab, "verbs: stale heads: nothing");
	vb_hand_at(502, hy - 5, 52, 0);
	/* the words: the test ones need SOMVR_HANDS_TEST; the prompts' words off by default */
	EXPECT(!strcmp(vb_word(" press 120 300"), "verbs press (SOMVR_HANDS_TEST is off: ignored)") &&
		       !strcmp(vb_word(" words test"), "verbs words (off|on|test; test needs SOMVR_HANDS_TEST: ignored)") &&
		       !strcmp(vb_word(" tree"), "verbs tree (SOMVR_HANDS_TEST is off: ignored)") &&
		       !strcmp(vb_word(" status"), "verbs status") && vb_text(121, "UI_Wraith_Dash") == NULL,
	       "verbs: the words");
	g_vb.test = 1;
	g_cb_test_t += 1.0;
	EXPECT(!strcmp(vb_word(" press 160+120 250"), "verbs press") && !strcmp(vb_word(" press 0 250"),
	       "verbs press (CMD[+CMD] MS: ignored)") && !strcmp(vb_word(" words test"), "verbs words test (every prompt's text = "
	       "VR + its StringId; prompts already up keep theirs)"), "verbs: the test words");
	cb_frame();
	n = vb_press_cmds(out);
	const char *vt = vb_text(121, "UI_Wraith_Dash");
	EXPECT(n == 2 && out[0] == 160 && out[1] == 120 && log_has("test press 160 'SpecialAttackModifier' + CombatPrimary03") &&
		       vt && !strcmp(vt, "VR UI_Wraith_Dash") && vb_text(121, "UI_Wraith_Dash") == vt,
	       "verbs: the test press and words (%d; '%s')", n, vt ? vt : "-");
	vb_word(" words off");
	g_cb_test_t += 1.0;
	cb_frame();
	EXPECT(vb_press_cmds(out) == 0 && vb_text(121, "UI_Wraith_Dash") == NULL, "verbs: the press over, the words off");
	vb_end_log();
	EXPECT(log_has("VERBS END: ") && log_has("grabs 2, near misses 2, releases 1, done 1, lost 1") &&
		       log_has("A taps 1, kills pressed 1"), "verbs: the END line");
	unsetenv("SOMVR_VERBS");
	vb_start(&bad);
	atomic_store(&g_vb_words_mode, 0);
	g_inp.on = inp, g_inp.ctx = ctx;
	g_inp.hands[SOMVR_HAND_L].valid = 0, g_inp.st.hand[SOMVR_HAND_L].squeeze = 0, g_inp.st.hand[SOMVR_HAND_L].trigger = 0;
}

static void finish_tests(void)
{
	fi_stand_ins();
	g_cb_test_t = 9000;
	/* 1. off by default; on without SOMVR_COMBAT: off; a wrong FI_HITCOUNT: off */
	fi_world();
	fi_go(NULL, NULL, NULL);
	EXPECT(g_fi.mode == FI_OFF && g_fi_sites == g_fis, "finish: off by default (mode %d)", g_fi.mode);
	g_cb.mode = CB_OFF;
	fi_go("1", NULL, NULL);
	EXPECT(g_fi.mode == FI_OFF && log_has("WARN SOMVR_FINISH=1 needs SOMVR_COMBAT"), "finish: on without combat");
	g_cb.mode = CB_FULL;
	const char *keep = g_fis[FIS_HITCOUNT].ctx_hex;
	g_fis[FIS_HITCOUNT].ctx_hex = "00 11 22 33";
	fi_go("1", NULL, NULL);
	EXPECT(g_fi.mode == FI_OFF && log_has("FINISH site FI_HITCOUNT") && log_has("WARN SOMVR_FINISH: FI_HITCOUNT did not match"),
	       "finish: a wrong proof site keeps it off");
	g_fis[FIS_HITCOUNT].ctx_hex = keep;
	/* 2. on, not ready (streak 3): the cycle's hits land as before; the slashes at A's back (Talion behind it) ask the
	 * game for a stealth kill, which it does not offer (A is aware): no start */
	fi_world();
	fi_go("1", NULL, NULL);
	EXPECT(g_fi.mode == FI_ON && g_fi.ok_stock && g_fi.ok_fx && g_fi.ok_slowmo && log_has("CONFIG finish mode=1 path=stock"),
	       "finish: on (mode %d stock %d fx %d)", g_fi.mode, g_fi.ok_stock, g_fi.ok_fx);
	fi_set_streak(3);
	g_fic.exec_ok = 1, g_fic.unaware = 0;
	int dh0 = g_dh.n, sel0 = g_fic.select;
	fi_cycle();
	EXPECT(g_dh.n == dh0 + 2 && g_fic.start == 0 && g_fic.create == 0, "finish: not ready: %d hits (want 2), %d starts, %d fx",
	       g_dh.n - dh0, g_fic.start, g_fic.create);
	EXPECT(g_fic.select > sel0 && g_fi.n.notoffered >= 1 && fi_list_empty() && g_fic.tgtinit == g_fic.tgtset,
	       "finish: the stealth asks (selects %d, not offered %llu, the target let go %d)", g_fic.select - sel0,
	       (unsigned long long)g_fi.n.notoffered, fi_list_empty());
	/* A fighting Talion (its target his object): no stealth ask at all */
	uintptr_t me = (uintptr_t)g_pcs.hobj;
	memcpy((char *)&g_oc[0] + FI_C_TARGET, &me, 8);
	sel0 = g_fic.select, dh0 = g_dh.n;
	fi_cycle();
	EXPECT(g_fic.select == sel0 && g_dh.n == dh0 + 2, "finish: an engaged orc: no stealth ask (%d), the hits land (%d)",
	       g_fic.select - sel0, g_dh.n - dh0);
	me = 0;
	memcpy((char *)&g_oc[0] + FI_C_TARGET, &me, 8);
	/* 3. ready (streak 9): the glow (the looping FX on the active weapon's model object, a pulse), then the heavy cut
	 * through A's neck starts the game's execution against A instead of the hit; the light swing back is skipped
	 * (claimed) */
	fi_world();
	fi_go("1", NULL, NULL);
	fi_set_streak(9);
	memset(&g_fic, 0, offsetof(typeof(g_fic), list));
	g_fic.list[0] = g_fic.list[1] = (uintptr_t)g_fic.list;
	g_fic.exec_ok = 1;
	dh0 = g_dh.n;
	cb_frame();
	EXPECT(g_fi.glow.on && g_fic.create == 1 && g_fic.cs == 1 && g_fic.csfree == 1 &&
		       !strcmp(g_fic.cs_name, "fx\\characters\\player\\elf\\elf_lightchain.fxp") && g_fic.cs_flags == 1 &&
		       g_fic.cs_parent == (uintptr_t)g_fi_wobj && g_fic.cs_mh == (uintptr_t)g_fi_wobj + 1 && g_fi.fx_handle == 0x7771,
	       "finish: the glow fx (create %d cs %d name '%s' flags %u parent ok %d handle %#lx)", g_fic.create, g_fic.cs,
	       g_fic.cs_name, g_fic.cs_flags, g_fic.cs_parent == (uintptr_t)g_fi_wobj, (unsigned long)g_fi.fx_handle);
	/* SOMVR_STEP on with a step of this swing running: the execution's start ends it (its sync owns the bodies), and
	 * no new step starts while the finisher holds A */
	g_step.on = 1;
	uint64_t sfin0 = g_step.run.ends[SOMVR_STE_FINISH], nfin0 = g_step.n_fin;
	somvr_step_start(&g_step.run, &g_step.cfg, SOMVR_ST_STEP, 0, (uintptr_t)&g_oc[0], 30, 0.25f, (sv_vec3){ 0, 0, 1 },
			 (sv_vec3){ 0, 0, 0 }, 100);
	for (int i = 1; i < (int)(SC_FAKE_PERIOD * 90 + 0.5); i++)
		cb_frame();
	somvr_blade fsb;
	memset(&fsb, 0, sizeof fsb);
	fsb.valid = 1;
	float fsr[3] = { 0, 0, 0 }, fss = g_step.cfg.step;
	uint64_t nfin1 = g_step.n_fin; /* the cycle's later swings were refused already (nfin0 on) */
	g_step.cfg.step = 1;
	st_combat(NULL, 0, &fsb, 1, fsr, g_cb_test_t);
	g_step.cfg.step = fss, g_step.on = 0;
	EXPECT(g_step.run.kind == SOMVR_ST_NONE && g_step.run.ends[SOMVR_STE_FINISH] == sfin0 + 1 && g_step.n_fin == nfin1 + 1 &&
		       log_has("step end: a finisher (SOMVR_FINISH started one"),
	       "finish: the step yields (kind %d, ends %llu, refused %llu)", g_step.run.kind,
	       (unsigned long long)(g_step.run.ends[SOMVR_STE_FINISH] - sfin0), (unsigned long long)(g_step.n_fin - nfin0));
	EXPECT(g_fic.start == 1 && g_fic.start_pc == (uintptr_t)&g_fipc && g_fic.start_node == (uintptr_t)&g_fir_node[FN_EXEC] &&
		       g_fic.start_atk == (uintptr_t)&g_fir_atk[FA_EXEC] && g_fic.start_tgt == (uintptr_t)g_oc[0].hobj,
	       "finish: the execution started (%d starts, node ok %d, attack ok %d, target A %d)", g_fic.start,
	       g_fic.start_node == (uintptr_t)&g_fir_node[FN_EXEC], g_fic.start_atk == (uintptr_t)&g_fir_atk[FA_EXEC],
	       g_fic.start_tgt == (uintptr_t)g_oc[0].hobj);
	EXPECT(g_dh.n == dh0 && g_fi.n.skipped_hits >= 1 && g_fi.n.starts[SF_CLS_EXEC] == 1,
	       "finish: no hit on A in the cycle (%d hits, %llu skipped)", g_dh.n - dh0, (unsigned long long)g_fi.n.skipped_hits);
	EXPECT(g_fic.release == 1 && g_fi.fx_handle == 0 && !g_fi.glow.on, "finish: the glow ends at the start (release %d)",
	       g_fic.release);
	EXPECT(g_fic.ctx_combat == (uintptr_t)&g_pcs && g_fic.ctx_weapon == 0x5150 && g_fic.ctxpc >= 1 &&
		       NEAR(g_fic.ctx_angle, 0.5f * 57.2957795f, 1e-3) && g_fic.notme == 1 && g_fic.kbm_pad == 1 &&
		       !g_fic.window_busy && g_fic.set_src == (uintptr_t)g_fi_proot && g_fic.set_obj == (uintptr_t)g_oc[0].hobj &&
		       fi_list_empty() && g_fic.dirangle >= 1,
	       "finish: the selection as FUN_00a8e120/FUN_0229a190 do it (combat %d weapon %d angle %.2f notme %u pad %d "
	       "src %d obj %d list %d)", g_fic.ctx_combat == (uintptr_t)&g_pcs, g_fic.ctx_weapon == 0x5150, g_fic.ctx_angle,
	       g_fic.notme, g_fic.kbm_pad, g_fic.set_src == (uintptr_t)g_fi_proot, g_fic.set_obj == (uintptr_t)g_oc[0].hobj,
	       fi_list_empty());
	EXPECT(log_has("FINISH candidate 1: node 'PC_Node_Execute'") && log_has("attack 'PC_Bip_Execution_F_1'") &&
		       log_has("class exec") && log_has("FINISH start exec: FUN_00a8c700"),
	       "finish: the candidate and start lines");
	/* the claim ends when A dies */
	g_oc[0].dead = 1;
	cb_frame();
	EXPECT(g_fi.mo.key == 0 && g_fi.n.claims_dead == 1, "finish: the claim ends at A's death");
	/* 4. the fallback: the game offers no execution -> a killing DealHit with the heavy record, severing */
	fi_world();
	fi_go("1", NULL, "0.3");
	fi_set_streak(9);
	g_fic.exec_ok = 0, g_fic.start = 0, g_fic.slowmo = 0;
	dh0 = g_dh.n;
	g_dh_sever_p = &g_fi_sever;
	fi_cycle();
	g_dh_sever_p = NULL;
	EXPECT(g_dh.n == dh0 + 1 && g_dh.kill == 1 && g_dh.sever == 1 && g_fi_sever == 0 && g_fi.n.fallbacks == 1 &&
		       g_fic.start == 0 && g_dh.target == (uintptr_t)g_oc[0].hobj && g_dh.attacker == (uintptr_t)&g_pcs,
	       "finish: the fallback kill (%d hits, kill %d, sever at the call %d, after %d)", g_dh.n - dh0, g_dh.kill,
	       g_dh.sever, g_fi_sever);
	EXPECT(g_fic.slowmo == 2 && !strcmp(g_fic.slowmo_arg[0], "1000") && !strcmp(g_fic.slowmo_arg[1], "1000") &&
		       g_fi_tm[8] == 1000 && g_fi_tm[9] == 1000 && g_fi.smo_until == 0,
	       "finish: the slow motion set and given back (%d calls, last '%s' '%s', timer %u/%u)", g_fic.slowmo,
	       g_fic.slowmo_arg[0], g_fic.slowmo_arg[1], g_fi_tm[8], g_fi_tm[9]);
	/* 5. SOMVR_FINISH_PATH=hit: the kill hit with no selection; log mode: no game call at all */
	fi_world();
	fi_go("1", "hit", NULL);
	fi_set_streak(9);
	sel0 = g_fic.select, dh0 = g_dh.n;
	fi_cycle();
	EXPECT(g_fic.select == sel0 && g_dh.n == dh0 + 1 && g_dh.kill == 1, "finish: path=hit (selects %d, hits %d, kill %d)",
	       g_fic.select - sel0, g_dh.n - dh0, g_dh.kill);
	fi_world();
	fi_go("log", NULL, NULL);
	fi_set_streak(9);
	int cr0 = g_fic.create;
	sel0 = g_fic.select, dh0 = g_dh.n;
	g_fic.start = 0;
	fi_cycle();
	EXPECT(g_fic.select == sel0 && g_fic.create == cr0 && g_fic.start == 0 && g_dh.n == dh0 + 2 && g_fi.n.gestures[SF_CLS_EXEC] >= 1 &&
		       log_has("(log only)"),
	       "finish: log mode (selects %d fx %d starts %d hits %d gestures %llu)", g_fic.select - sel0, g_fic.create - cr0,
	       g_fic.start, g_dh.n - dh0, (unsigned long long)g_fi.n.gestures[SF_CLS_EXEC]);
	/* 6. the dagger from behind A: a stab into its upper back -> the stealth kill (the game offers it: A unaware);
	 * with the left trigger held: brutalize wanted, the stealth kill taken as the second choice */
	for (int pass = 0; pass < 2; pass++) {
		fi_world();
		fi_go("1", NULL, NULL);
		g_fic.unaware = 1, g_fic.start = 0;
		float back[3] = { 500, 140 - 16, 60 - 14 }; /* A's upper back: the neck (root + 40) - 16 down, 14 behind */
		somvr_blade d0, d1;
		float dir[3] = { 0, 0, 1 }, v[3] = { 0, 0, 200 };
		float b0[3] = { back[0], back[1], back[2] - 60 }, b1[3] = { back[0], back[1], back[2] - 38 };
		fi_line_blade(b0, dir, 30, v, g_cb_test_t, &d0);
		fi_hand_frame(NULL, &d0, 2.0f, pass ? 1.0f : 0.0f, 0, NULL);
		fi_line_blade(b1, dir, 30, v, g_cb_test_t + 1.0 / 90, &d1);
		fi_hand_frame(&d0, &d1, 2.0f, pass ? 1.0f : 0.0f, 0, NULL);
		EXPECT(g_fic.start == 1 && g_fic.start_atk == (uintptr_t)&g_fir_atk[FA_SNEAK] && g_fic.start_tgt == (uintptr_t)g_oc[0].hobj &&
			       g_fi.n.gestures[pass ? SF_CLS_BRUTAL : SF_CLS_STEALTH] == 1 && g_fi.n.starts[SF_CLS_STEALTH] == 1,
		       "finish: the dagger's stealth kill, pass %d (%d starts, sneak %d)", pass, g_fic.start,
		       g_fic.start_atk == (uintptr_t)&g_fir_atk[FA_SNEAK]);
	}
	/* 7. the latched hand: A stunned, the left hand closes at its neck, the trigger held 0.4 s -> the drain */
	fi_world();
	fi_go("1", NULL, NULL);
	fi_set_state(0, FA_STUN);
	g_fi.cfg.exec_stunned = 0; /* the fake's next cut must not execute the stunned orc first */
	g_fic.start = 0;
	float neck[3] = { 500, 140, 60 }, near[3] = { 505, 140, 52 };
	fi_hand_frame(NULL, NULL, 0, 0, 0, near);
	fi_hand_frame(NULL, NULL, 0, 0, 1, near);
	EXPECT(g_fi.latch.held && g_fi.latch.key == (uintptr_t)&g_oc[0] && g_fi.n.latches == 1, "finish: latched (%d)", g_fi.latch.held);
	for (int i = 0; i < 40 && !g_fic.start; i++)
		fi_hand_frame(NULL, NULL, 0, 1, 1, near);
	EXPECT(g_fic.start == 1 && g_fic.start_atk == (uintptr_t)&g_fir_atk[FA_DRAIN] && g_fi.n.starts[SF_CLS_DRAIN] == 1,
	       "finish: the drain by the held trigger (%d starts)", g_fic.start);
	(void)neck;
	fi_hand_frame(NULL, NULL, 0, 0, 0, near);
	EXPECT(!g_fi.latch.held && g_fi.n.latch_ev[SF_LEV_RELEASE] == 1, "finish: released");
	/* 8. a knocked-down orc: the sword's downward stab is held back from the body capsule (no hit) and judged at the
	 * torso: the ground execution, the execution as the second choice (the game offers that one here) */
	fi_world();
	fi_go("1", NULL, NULL);
	fi_set_state(0, FA_KD);
	fi_set_streak(9);
	g_fic.exec_ok = 1, g_fic.start = 0;
	{
		struct cb_orc oa;
		EXPECT(cb_orc_get((uintptr_t)&g_oc[0], &oa), "finish: orc A");
		oa.dist = 100;
		float dn[3] = { 0, -1, 0 }, v[3] = { 0, -250, 0 }, b0[3] = { 500, 260, 60 }, b1[3] = { 500, 225, 60 };
		somvr_blade s0, s1;
		fi_line_blade(b0, dn, 90, v, g_cb_test_t + 0.5, &s0);
		fi_line_blade(b1, dn, 90, v, g_cb_test_t + 0.5 + 1.0 / 90, &s1);
		g_cb.sp.axis_mps = 2.5f, g_cb.sp.cos_axis = 0.98f, g_cb.sp.mean_mps = 2.5f;
		sc_contact ct = { 1, 0.9f, 20, { 500, 128, 60 } };
		EXPECT(fi_contact((uintptr_t)&g_fipc, (uintptr_t)&g_pcs, &oa, &ct, &s0, g_cb_test_t + 0.5) == 1 &&
			       g_fi.n.held_hits == 1, "finish: the stab's body contact held back");
		g_fi.sprev.valid = 0;
		fi_sword((uintptr_t)&g_fipc, (uintptr_t)&g_pcs, &oa, 1, &s0, 0, g_cb_test_t + 0.5);
		EXPECT(g_fic.start == 0, "finish: above the torso: nothing yet");
		fi_sword((uintptr_t)&g_fipc, (uintptr_t)&g_pcs, &oa, 1, &s1, 0, g_cb_test_t + 0.5 + 1.0 / 90);
		EXPECT(g_fic.start == 1 && g_fic.start_atk == (uintptr_t)&g_fir_atk[FA_EXEC] && g_fi.n.gestures[SF_CLS_GROUND] == 1 &&
			       g_fi.n.starts[SF_CLS_EXEC] == 1 && g_fi.mo.key == (uintptr_t)&g_oc[0],
		       "finish: the ground stab -> the execution (%d starts, ground gestures %llu)", g_fic.start,
		       (unsigned long long)g_fi.n.gestures[SF_CLS_GROUND]);
		EXPECT(fi_contact((uintptr_t)&g_fipc, (uintptr_t)&g_pcs, &oa, &ct, &s1, g_cb_test_t + 0.6) == 1 &&
			       g_fi.n.skipped_hits == 1, "finish: the claimed orc takes no hit");
		/* a standing orc that is not ready: combat's hit as usual */
		fi_set_state(0, -1);
		fi_set_streak(2);
		g_fi.mo.key = 0, g_fi.mo.last_t = -1e9;
		g_fi.stab = FI_STAB_OFF;
		EXPECT(fi_contact((uintptr_t)&g_fipc, (uintptr_t)&g_pcs, &oa, &ct, &s1, g_cb_test_t + 0.7) == 0,
		       "finish: an ordinary contact goes to combat");
	}
	EXPECT(g_fi_sites == g_fis, "finish: the stand-ins in use");
	/* the end line */
	fi_end_log();
	EXPECT(log_has("FINISH END frames="), "finish: the end line");
	for (size_t i = 0; i < sizeof fi_env / sizeof fi_env[0]; i++)
		unsetenv(fi_env[i]);
	memset(&g_fi, 0, sizeof g_fi); /* off again */
	g_player.pcomb = &g_pcomb;
}

/* SOMVR_DEFENCE_CLANK=fx (round 2): the spark FX, a one-shot ClientFX on the active weapon's model object through the
 * finisher's stand-ins (after finish_tests: its sites and objects) */
static void df_fx_tests(void)
{
	char why[200] = "";
	fi_world();
	g_fi_sites = g_fis;
	g_df.mode = DF_ON, g_df.lines = 0;
	g_df.ok_fx = df_fx_check(why, sizeof why);
	snprintf(g_df.clank_fx, sizeof g_df.clank_fx, "fx\\test\\sparks.fxp");
	g_df.pcomb = (uintptr_t)&g_fipc;
	int cs0 = g_fic.cs, cr0 = g_fic.create, fr0 = g_fic.csfree;
	uint64_t fx0 = g_df.n.fx;
	df_clank_fx(g_cb_test_t);
	EXPECT(g_df.ok_fx && g_fic.cs == cs0 + 1 && g_fic.create == cr0 + 1 && g_fic.csfree == fr0 + 1 && g_fic.cs_flags == 0 &&
		       !strcmp(g_fic.cs_name, "fx\\test\\sparks.fxp") && g_fic.cs_parent == (uintptr_t)g_fi_wobj &&
		       g_fic.cs_mh == (uintptr_t)g_fi_wobj + 1 && g_df.n.fx == fx0 + 1 &&
		       log_has("DEFENCE clank fx 'fx\\test\\sparks.fxp' on item") && log_has("rc=1"),
	       "clank fx: ok %d (%s) cs %d create %d free %d flags %u name '%s' parent %#lx", g_df.ok_fx, why, g_fic.cs - cs0,
	       g_fic.create - cr0, g_fic.csfree - fr0, g_fic.cs_flags, g_fic.cs_name, (unsigned long)g_fic.cs_parent);
	/* no active item: not made, said */
	uintptr_t keep;
	memcpy(&keep, g_fi_hold + FI_INV_OF, 8);
	memset(g_fi_hold + FI_INV_OF, 0, 8);
	uint64_t ff0 = g_df.n.fx_fail;
	df_clank_fx(g_cb_test_t);
	memcpy(g_fi_hold + FI_INV_OF, &keep, 8);
	EXPECT(g_df.n.fx_fail == ff0 + 1 && g_fic.create == cr0 + 1 && log_has("DEFENCE clank fx not made: no active weapon model object"),
	       "clank fx without an item: failed %llu", (unsigned long long)(g_df.n.fx_fail - ff0));
	/* a site that does not match: no fx at all */
	struct site_spec keep_s = g_fis[FIS_FX_CREATE];
	g_fis[FIS_FX_CREATE].ctx_hex = "90 90 90 90";
	EXPECT(!df_fx_check(why, sizeof why) && strstr(why, "FI_FX_CREATE did not match"), "clank fx: a bad site passed (%s)", why);
	g_fis[FIS_FX_CREATE] = keep_s;
	g_df.mode = DF_OFF, g_df.ok_fx = 0, g_df.pcomb = 0;
	g_player.pcomb = &g_pcomb;
	printf("defence clank fx: a one-shot spark FX on the drawn sword's model object; none without an item or a site\n");
}

/* ---- SOMVR_HIDE_HEAD=parts (re-xr-pipeline.md 26; off at start, switched on here, white box, the last tests): the five
 * FUN_0081ff80 calls patched in the stand-in builders c_hp_*, the read-only proofs as stand-ins, the player's
 * CSimpleModel with part renderables in slots 0 (body), 1 (head), 3 (helm), 4 (hair), 5 (acc1) next to its base
 * renderable, and an orc's node. Checks: the words; a proof or a call site that does not match (no parts, nothing
 * patched, near with section 25's eye offset); the five hooks in and the eye defaults (no offset, the hands' 6 cm plane,
 * 12 with near, an explicit offset kept); stereo frames: the head, helm and hair nodes skipped in both eyes' five camera
 * passes and drawn in the shadow maps, everything else drawn, the calls' arguments passed on; outside an eye, a flat
 * frame, the camera base: drawn; a rebuilt head (a new renderable), a part still loading, the trigger words; the 5 s
 * line, the summary; the run-time fallback (no head node for HP_EMPTY_FRAMES frames) */
static char g_hphex[HPS_N][256];
static int hp_site_call(void *caller, void *target, struct site_spec *s, const char *name, int i)
{
	const unsigned char *p = caller;
	for (int k = 0; k < 96; k++) {
		int32_t rel;
		memcpy(&rel, p + k + 1, 4);
		if (p[k] == 0xe8 && (uintptr_t)(p + k + 5 + rel) == (uintptr_t)target) {
			hex_of(p + k, 5, g_hphex[i], 128);
			hex_of(target, 4, g_hphex[i] + 128, 128);
			*s = (struct site_spec){ name, K_CALL, (uintptr_t)(p + k), (uintptr_t)(p + k), g_hphex[i], (uintptr_t)target,
						 g_hphex[i] + 128 };
			return 0;
		}
	}
	return -1;
}
static void hp_site_entry(void *fn, struct site_spec *s, const char *name, int i)
{
	hex_of(fn, 8, g_hphex[i], 128);
	hex_of(fn, 4, g_hphex[i] + 128, 128);
	*s = (struct site_spec){ name, K_ENTRY, (uintptr_t)fn, (uintptr_t)fn, g_hphex[i], (uintptr_t)fn, g_hphex[i] + 128 };
}
/* CSimpleModel::vf_20 (never called here: its slot is a proof) and three functions whose first bytes stand for the
 * slot proofs */
__attribute__((noipa)) uint64_t st_vf20(void *m, uint32_t slot, void *mesh) { return (uintptr_t)m + slot + (uintptr_t)mesh; }
__attribute__((noipa)) uint64_t st_hp_head(uint64_t a) { return a * 3 + 1; }
__attribute__((noipa)) uint64_t st_hp_helm(uint64_t a) { return a * 5 + 3; }
__attribute__((noipa)) uint64_t st_hp_hair(uint64_t a) { return a * 7 + 4; }
static st_rend_t g_prend[6];
static st_handle_t g_phandle[6], g_orc_handle;
static const int g_pslot[6] = { 0, 1, 3, 4, 5, 1 }; /* g_prend[5]: a rebuilt head */
static uint32_t hp_all(void)
{
	uint32_t a = 0;
	for (int i = 0; i < HPN; i++)
		a |= g_hpn[i] ? 1u << i : 0;
	return a;
}
/* both eyes: the nodes in hidden missing from all five camera passes, the rest in all of them; all in the shadow maps */
static int hp_scene_ok(uint32_t hidden, char *why, size_t cap)
{
	uint32_t all = hp_all();
	for (int k = 0; k < 2; k++) {
		for (int p = 0; p < SOMVR_HP_PASSES; p++)
			if (g_hpd[k][p] != (all & ~hidden))
				return snprintf(why, cap, "eye %d %s drew %#x, want %#x", k, somvr_hp_pass_name(p), g_hpd[k][p],
						all & ~hidden), 0;
		if (g_hpd[k][SOMVR_HP_PASSES] != all)
			return snprintf(why, cap, "eye %d shadow drew %#x, want %#x", k, g_hpd[k][SOMVR_HP_PASSES], all), 0;
	}
	return snprintf(why, cap, "ok"), 1;
}
static void hp_frame_run(void)
{
	memset(g_hpd, 0, sizeof g_hpd), memset(g_hpcd, 0, sizeof g_hpcd);
	frame();
}
/* the cloth stand-ins: the CSimpleModelClothNode (+0x138 its set, +0x170 its descriptors), the set's entries, two
 * visuals with their custom handles and nodes, an orc's custom node; the proofs' stand-in functions */
typedef struct { uintptr_t vt; char p0[CN_SET - 8]; void *set; char p1[CN_DESC - CN_SET - 8]; void *desc_b, *desc_e; } st_cloth_t;
typedef struct { void *b, *e; uint32_t cap; uint8_t owned; char p[0x58 - 0x15]; } st_cset_t;
typedef struct { void *vis, *mat; uint32_t hash, flags; } st_centry_t;
typedef struct { uintptr_t vt; char p0[VIS_HANDLE - 8]; st_handle_t *handle; char p1[0x30]; } st_vis_t;
_Static_assert(offsetof(st_cloth_t, set) == CN_SET && offsetof(st_cloth_t, desc_b) == CN_DESC && sizeof(st_centry_t) == CS_ENTRY &&
		       offsetof(st_vis_t, handle) == VIS_HANDLE && offsetof(st_handle_t, node) == CHANDLE_NODE,
	       "stand-in cloth layouts");
static void *volatile g_vt_cloth[16], *volatile g_vt_vis[4], *volatile g_vt_chandle[56], *volatile g_vt_cnode[8];
static st_cloth_t g_cloth;
static st_cset_t g_cset;
static st_centry_t g_centry[3];
static st_vis_t g_cvis[3];
static st_handle_t g_chandle[3], g_orc_chandle;
static char g_cdesc[2][CN_DESC_SIZE];
__attribute__((noipa)) uint64_t st_cn_eval(void *cn) { return (uintptr_t)cn + 6; }
__attribute__((noipa)) uint64_t st_vis_dtor(void *v) { return (uintptr_t)v + 1; }
__attribute__((noipa)) uint64_t st_chandle_dtor(void *h) { return (uintptr_t)h + 2; }
__attribute__((noipa)) uint64_t st_cnode_dtor(void *n) { return (uintptr_t)n + 3; }
__attribute__((noipa)) uint64_t st_rh_custom(void *r, void *d, void *o) { return (uintptr_t)r + (uintptr_t)d + (uintptr_t)o; }
__attribute__((noipa)) uint64_t st_hpc_store(uint64_t a) { return a * 11 + 1; }
__attribute__((noipa)) uint64_t st_hpc_set(uint64_t a) { return a * 13 + 2; }
__attribute__((noipa)) uint64_t st_hpc_entry(uint64_t a) { return a * 17 + 3; }
__attribute__((noipa)) uint64_t st_hpc_handle(uint64_t a) { return a * 19 + 4; }
__attribute__((noipa)) uint64_t st_hpc_inline(uint64_t a) { return a * 23 + 5; }
static void hp_site_vslot(void *volatile *slot, void *fn, struct site_spec *s, const char *name, int i)
{
	*slot = fn;
	hex_of((const unsigned char *)slot, 8, g_hphex[i], 128);
	hex_of(fn, 4, g_hphex[i] + 128, 128);
	*s = (struct site_spec){ name, K_VSLOT, (uintptr_t)slot, (uintptr_t)slot, g_hphex[i], (uintptr_t)fn, g_hphex[i] + 128 };
}
/* Talion's cloth: two display meshes (entries 0, 1: g_hpcn[0], [1]); g_hpcn[2] = entry 2's node (not in the set unless
 * a test adds it), g_hpcn[3] = an orc's custom node */
static void hpc_world(void)
{
	memset(&g_cloth, 0, sizeof g_cloth), memset(&g_cset, 0, sizeof g_cset), memset(g_centry, 0, sizeof g_centry);
	memset(g_cvis, 0, sizeof g_cvis), memset(g_chandle, 0, sizeof g_chandle), memset(&g_orc_chandle, 0, sizeof g_orc_chandle);
	g_cloth.vt = (uintptr_t)g_vt_cloth, g_cloth.set = &g_cset;
	snprintf(g_cdesc[0], CN_DESC_SIZE, "Talion_Cloak_Display"), snprintf(g_cdesc[1], CN_DESC_SIZE, "Talion_Fur_Display");
	g_cloth.desc_b = g_cdesc[0], g_cloth.desc_e = g_cdesc[2];
	for (int j = 0; j < 3; j++) {
		g_centry[j].vis = &g_cvis[j], g_centry[j].hash = 0x100u + (uint32_t)j;
		g_cvis[j].vt = (uintptr_t)g_vt_vis, g_cvis[j].handle = &g_chandle[j];
		g_chandle[j].vt = (uintptr_t)g_vt_chandle, g_chandle[j].node.vt = (uintptr_t)g_vt_cnode;
	}
	g_cset.b = &g_centry[0], g_cset.e = &g_centry[2], g_cset.cap = 3;
	g_orc_chandle.vt = (uintptr_t)g_vt_chandle, g_orc_chandle.node.vt = (uintptr_t)g_vt_cnode;
	memset(g_hpcn, 0, sizeof g_hpcn);
	for (int j = 0; j < 3; j++)
		g_hpcn[j] = (uintptr_t)&g_chandle[j].node;
	g_hpcn[3] = (uintptr_t)&g_orc_chandle.node;
}
static uint32_t hpc_all(void)
{
	uint32_t a = 0;
	for (int i = 0; i < HPCN; i++)
		a |= g_hpcn[i] ? 1u << i : 0;
	return a;
}
/* both eyes: the custom nodes in hidden missing from all eight calls, the rest in all of them; all in the shadow call */
static int hpc_scene_ok(uint32_t hidden, char *why, size_t cap)
{
	uint32_t all = hpc_all();
	for (int k = 0; k < 2; k++) {
		for (int c = 0; c < HPC_N; c++)
			if (g_hpcd[k][c] != (all & ~hidden))
				return snprintf(why, cap, "eye %d %s drew %#x, want %#x", k, g_hpc_h[c].name, g_hpcd[k][c], all & ~hidden), 0;
		if (g_hpcd[k][HPC_N] != all)
			return snprintf(why, cap, "eye %d cloth shadow drew %#x, want %#x", k, g_hpcd[k][HPC_N], all), 0;
	}
	return snprintf(why, cap, "ok"), 1;
}

static void hp_tests(const char *trig)
{
	char why[200];
	/* 1. the words */
	EXPECT(hb_parse("parts") == 4 && hb_parse("parts+near") == 6 && hb_parse("near+parts") == 6 && hb_parse("bone+parts") == 5 &&
		       hb_parse("bone+near+parts") == 7 && hb_parse("parts+parts") == -1 && hb_parse("part") == -1 &&
		       hb_parse("Parts") == -1 && hb_parse("parts+") == -1 && hb_parse("partsnear") == -1 && hb_parse("bone") == 1 &&
		       hb_parse("near") == 2,
	       "hb_parse with parts");
	/* the state the earlier tests left, restored at the end */
	int hide0 = g_hide.on, layout0 = g_layout, fake0 = g_views_fake, near0 = g_hb.near;
	float near_cm0 = g_near_cm;
	somvr_rig_cfg cfg0 = g_rigcfg;
	char tp0[sizeof g_trig_path];
	snprintf(tp0, sizeof tp0, "%s", g_trig_path);
	g_hide.on = 0;
	unsetenv("SOMVR_EYE_FWD_CM"), unsetenv("SOMVR_EYE_MIN_CM"), unsetenv("SOMVR_EYE_NEAR_CM"), unsetenv("SOMVR_TUNE");
	unsetenv("SOMVR_HEAD_PARTS");
	g_rigcfg.base = SOMVR_BASE_HEAD;
	g_near_cm = 0;
	/* the stand-in sites */
	static struct site_spec hps[HPS_N];
	if (hp_site_call((void *)c_hp_deep, (void *)t_hp_submit, &hps[HPS_DEEP], "HP_DRAW_DEEP", HPS_DEEP) ||
	    hp_site_call((void *)c_hp_fwd, (void *)t_hp_submit, &hps[HPS_FWD], "HP_DRAW_FWD", HPS_FWD) ||
	    hp_site_call((void *)c_hp_late, (void *)t_hp_submit, &hps[HPS_LATE], "HP_DRAW_LATE", HPS_LATE) ||
	    hp_site_call((void *)c_hp_gbuf, (void *)t_hp_submit, &hps[HPS_GBUF], "HP_DRAW_GBUF", HPS_GBUF) ||
	    hp_site_call((void *)c_hp_vel, (void *)t_hp_submit, &hps[HPS_VEL], "HP_DRAW_VEL", HPS_VEL)) {
		EXPECT(0, "parts: no call site found in the stand-in builders");
		return;
	}
	g_vt_smodel[20] = (void *)st_vf20;
	hex_of((const unsigned char *)&g_vt_smodel[20], 8, g_hphex[HPS_SLOT_SET], 128);
	hex_of((const unsigned char *)st_vf20, 4, g_hphex[HPS_SLOT_SET] + 128, 128);
	hps[HPS_SLOT_SET] = (struct site_spec){ "HP_SLOT_SET", K_VSLOT, (uintptr_t)&g_vt_smodel[20], (uintptr_t)&g_vt_smodel[20],
						g_hphex[HPS_SLOT_SET], (uintptr_t)st_vf20, g_hphex[HPS_SLOT_SET] + 128 };
	hp_site_entry((void *)st_vf20, &hps[HPS_SLOT_STORE], "HP_SLOT_STORE", HPS_SLOT_STORE);
	hp_site_entry((void *)st_hp_head, &hps[HPS_HEAD], "HP_HEAD_SLOT", HPS_HEAD);
	hp_site_entry((void *)st_hp_helm, &hps[HPS_HELM], "HP_HELM_SLOT", HPS_HELM);
	hp_site_entry((void *)st_hp_hair, &hps[HPS_HAIR], "HP_HAIR_SLOT", HPS_HAIR);
	hp_site_entry((void *)t_hp_shadow, &hps[HPS_SHADOW], "HP_SUBMIT_SHADOW", HPS_SHADOW);
	for (int c = 0; c < HPC_N; c++)
		if (hp_site_call((void *)g_hpc_callers[c], (void *)t_hpc_submit, &hps[HPS_C_DEEP + c], g_hpc_h[c].name, HPS_C_DEEP + c)) {
			EXPECT(0, "parts: no call site found in the stand-in custom builder %d", c);
			return;
		}
	hp_site_entry((void *)st_hpc_store, &hps[HPS_CLOTH_STORE], "HP_CLOTH_STORE", HPS_CLOTH_STORE);
	hp_site_entry((void *)st_hpc_set, &hps[HPS_CLOTH_SET], "HP_CLOTH_SET", HPS_CLOTH_SET);
	hp_site_entry((void *)st_hpc_entry, &hps[HPS_CLOTH_ENTRY], "HP_CLOTH_ENTRY", HPS_CLOTH_ENTRY);
	hp_site_entry((void *)st_hpc_handle, &hps[HPS_CLOTH_HANDLE], "HP_CLOTH_HANDLE", HPS_CLOTH_HANDLE);
	hp_site_entry((void *)st_hpc_inline, &hps[HPS_CUST_INLINE], "HP_CUST_INLINE", HPS_CUST_INLINE);
	hp_site_vslot(&g_vt_cloth[HPC_SLOT_CLOTH], (void *)st_cn_eval, &hps[HPS_CLOTH_VT], "HP_CLOTH_VT", HPS_CLOTH_VT);
	hp_site_vslot(&g_vt_vis[HPC_SLOT_VIS], (void *)st_vis_dtor, &hps[HPS_VIS_VT], "HP_VIS_VT", HPS_VIS_VT);
	hp_site_vslot(&g_vt_chandle[HPC_SLOT_CHANDLE], (void *)st_chandle_dtor, &hps[HPS_CHANDLE_VT], "HP_CHANDLE_VT", HPS_CHANDLE_VT);
	hp_site_vslot(&g_vt_cnode[HPC_SLOT_CNODE], (void *)st_cnode_dtor, &hps[HPS_CNODE_VT], "HP_CNODE_VT", HPS_CNODE_VT);
	static void *volatile rh_vt[8];
	hp_site_vslot(&rh_vt[HPC_SLOT_CREATE], (void *)st_rh_custom, &hps[HPS_CUST_CREATE], "HP_CUST_CREATE", HPS_CUST_CREATE);
	g_hp_sites = hps;
	/* the world: Talion's model with its base renderable and five parts, an orc */
	model_world();
	g_obj_model = &g_sm;
	for (int j = 0; j < 6; j++) {
		memset(&g_prend[j], 0, sizeof g_prend[j]), memset(&g_phandle[j], 0, sizeof g_phandle[j]);
		g_prend[j].vt = (uintptr_t)g_vt_rend, g_prend[j].handle = &g_phandle[j];
		g_phandle[j].vt = (uintptr_t)g_vt_handle, g_phandle[j].node.vt = (uintptr_t)g_vt_node;
		if (j < 5)
			g_sm.slot[g_pslot[j]] = &g_prend[j];
	}
	g_orc_handle.vt = (uintptr_t)g_vt_handle, g_orc_handle.node.vt = (uintptr_t)g_vt_node;
	hpc_world();
	g_sm.cloth = &g_cloth;
	memset(g_hpn, 0, sizeof g_hpn);
	g_hpn[0] = (uintptr_t)&g_sm.rend->handle->node; /* the base renderable's node (the 17 pieces: nothing visible) */
	for (int j = 0; j < 5; j++)
		g_hpn[1 + j] = (uintptr_t)&g_phandle[j].node; /* 1 body, 2 head, 3 helm, 4 hair, 5 acc1 */
	g_hpn[6] = (uintptr_t)&g_orc_handle.node;
	const uint32_t HID = 0x1c; /* head, helm, hair */

	/* 2. a read-only proof that does not match: no parts, nothing patched, near with section 25's eye offset */
	const char *keep = hps[HPS_HEAD].ctx_hex;
	static char other[256]; /* the same bytes but the first */
	snprintf(other, sizeof other, "%s", keep);
	other[0] = other[0] == 'f' ? '0' : 'f';
	hps[HPS_HEAD].ctx_hex = other;
	setenv("SOMVR_HIDE_HEAD", "parts", 1);
	int bad = 0;
	uint32_t hk = hb_config();
	hv_config(&bad);
	int none_in = !g_hp_h[0].installed && !g_hp_h[1].installed && !g_hp_h[2].installed && !g_hp_h[3].installed &&
		      !g_hp_h[4].installed;
	EXPECT(hk == 0 && !hp_live() && g_hb.near && g_hp.fell_back && none_in && log_has("HEADPARTS site HP_HEAD_SLOT MISMATCH") &&
		       log_has("WARN SOMVR_HIDE_HEAD=parts: 1 of its read-only sites did not match: no parts") &&
		       log_has("parts is off: near instead") && g_hv.on && g_hv.cfg.fwd_cm == 16 && g_hv.cfg.min_cm == 10 &&
		       g_hv.near_cm == HV_NEAR_CM,
	       "parts, a proof mismatch: live %d near %d fell back %d hooks none %d; eye fwd %g min %g near %g", hp_live(), g_hb.near,
	       g_hp.fell_back, none_in, g_hv.cfg.fwd_cm, g_hv.cfg.min_cm, g_hv.near_cm);
	hps[HPS_HEAD].ctx_hex = keep;
	/* 3. a call site that does not match: all five or none */
	struct site_spec late = hps[HPS_LATE];
	hps[HPS_LATE].target = (uintptr_t)t_hp_shadow;
	hb_config();
	none_in = !g_hp_h[0].installed && !g_hp_h[1].installed && !g_hp_h[2].installed && !g_hp_h[3].installed &&
		  !g_hp_h[4].installed;
	EXPECT(!hp_live() && g_hb.near && none_in && log_has("HOOK HP_DRAW_DEEP SKIPPED") &&
		       log_has("another HP_DRAW_* site did not match (all five or none)") &&
		       log_has("the five HP_DRAW_* calls are not all in: no parts"),
	       "parts, a call-site mismatch: live %d near %d none in %d", hp_live(), g_hb.near, none_in);
	hps[HPS_LATE] = late;
	/* 3b. a cloth proof that does not match: parts on, the cloth off (a WARN), no custom call patched */
	keep = hps[HPS_CLOTH_SET].ctx_hex;
	static char other2[256];
	snprintf(other2, sizeof other2, "%s", keep);
	other2[0] = other2[0] == 'f' ? '0' : 'f';
	hps[HPS_CLOTH_SET].ctx_hex = other2;
	hb_config();
	int c_none = 1;
	for (int c = 0; c < HPC_N; c++)
		c_none &= !g_hpc_h[c].installed;
	EXPECT(hp_live() && !hpc_live() && c_none && log_has("HEADPARTS site HP_CLOTH_SET MISMATCH") &&
		       log_has("WARN SOMVR_HIDE_HEAD=parts: 1 of the cloth's read-only sites did not match: the cloth (the fur mantle) "
			       "stays in the eyes") && log_has("; the cloth is off) while the base is the head"),
	       "cloth, a proof mismatch: parts %d cloth %d none in %d", hp_live(), hpc_live(), c_none);
	hps[HPS_CLOTH_SET].ctx_hex = keep;
	/* 3c. a custom call site that does not match: all eight or none */
	struct site_spec clb = hps[HPS_C_LATE_B];
	hps[HPS_C_LATE_B].target = (uintptr_t)t_hpc_shadow;
	hb_config();
	c_none = 1;
	for (int c = 0; c < HPC_N; c++)
		c_none &= !g_hpc_h[c].installed;
	EXPECT(hp_live() && !hpc_live() && c_none && log_has("HOOK HP_CUST_DEEP SKIPPED") &&
		       log_has("another HP_CUST_* site did not match (all eight or none)") &&
		       log_has("the eight HP_CUST_* calls are not all in: the cloth (the fur mantle) stays in the eyes"),
	       "cloth, a call-site mismatch: parts %d cloth %d none in %d", hp_live(), hpc_live(), c_none);
	hps[HPS_C_LATE_B] = clb;
	/* 4. the five calls in; the eye defaults */
	hk = hb_config();
	hb_start();
	hv_config(&bad);
	int all_in = g_hp_h[0].installed && g_hp_h[1].installed && g_hp_h[2].installed && g_hp_h[3].installed &&
		     g_hp_h[4].installed;
	int c_all = 1;
	for (int c = 0; c < HPC_N; c++)
		c_all &= g_hpc_h[c].installed;
	EXPECT(hk == 0 && hp_live() && !g_hb.near && !g_hp.fell_back && all_in && g_hp.slots == SOMVR_HP_DEFAULT &&
		       hpc_live() && c_all && log_has("HOOK HP_CUST_GBUF_A ok call") && log_has("HOOK HP_CUST_VEL_B ok call") &&
		       log_has("and the eight FUN_007ccec0 calls) while the base is the head") &&
		       log_has("HOOK HP_DRAW_GBUF ok call") && log_has("(the velocity pass)") &&
		       log_has("CONFIG head parts: slots 1,3,4") &&
		       log_has("CONFIG hide_head=parts: bone=0 (not asked) near=0 (not asked) parts=1 (the head, hood and hair"),
	       "parts on: live %d near %d hooks %d slots %#x", hp_live(), g_hb.near, all_in, g_hp.slots);
	EXPECT(!g_hv.on && g_hv.cfg.fwd_cm == 0 && g_hv.cfg.min_cm == 0 && g_hv.near_cm == HV_NEAR_CM &&
		       log_has("head near plane 6 cm (SOMVR_HIDE_HEAD=parts: the hands' plane, no head drawn in the eyes)"),
	       "parts, the eye: on %d fwd %g min %g near %g", g_hv.on, g_hv.cfg.fwd_cm, g_hv.cfg.min_cm, g_hv.near_cm);
	setenv("SOMVR_HIDE_HEAD", "parts+near", 1);
	hb_config();
	hb_start();
	hv_config(&bad);
	EXPECT(hp_live() && g_hb.near && g_hv.near_cm == HB_NEAR_CM && g_hv.cfg.fwd_cm == 0 &&
		       log_has("CONFIG hide_head=near+parts: bone=0 (not asked) near=1") &&
		       log_has("head near plane 12 cm (SOMVR_HIDE_HEAD=parts+near)"),
	       "parts+near: live %d near %d plane %g fwd %g", hp_live(), g_hb.near, g_hv.near_cm, g_hv.cfg.fwd_cm);
	setenv("SOMVR_HIDE_HEAD", "parts", 1);
	setenv("SOMVR_EYE_FWD_CM", "12", 1), setenv("SOMVR_HEAD_PARTS", "1,3,x", 1);
	hb_config();
	hv_config(&bad);
	EXPECT(hp_live() && g_hv.on && g_hv.cfg.fwd_cm == 12 && g_hv.cfg.min_cm == 0 && g_hv.near_cm == HV_NEAR_CM &&
		       g_hp.slots == SOMVR_HP_DEFAULT && log_has("WARN SOMVR_HEAD_PARTS=1,3,x is not a slot list"),
	       "parts with SOMVR_EYE_FWD_CM=12: on %d fwd %g near %g slots %#x", g_hv.on, g_hv.cfg.fwd_cm, g_hv.near_cm, g_hp.slots);
	unsetenv("SOMVR_EYE_FWD_CM"), unsetenv("SOMVR_HEAD_PARTS");
	hb_config();
	hb_start();
	hv_config(&bad);

	/* 5. stereo frames on the head base: the head, helm and hair skipped in both eyes' five passes, drawn in the shadow
	 * maps; the body, the acc1 part, the base node and the orc drawn everywhere; the eye at the base, the 6 cm plane */
	g_pl = &g_player;
	g_layout = g_rigcfg.layout = SOMVR_LAYOUT_B;
	g_views_fake = 1;
	g_hpa_bad = 0;
	hp_frame_run();
	uint64_t calls0 = g_hpa_calls;
	hp_frame_run();
	int ok5 = hp_scene_ok(HID, why, sizeof why);
	sv_vec3 m = eye_mid(), m0 = m; /* the eye on the base itself (no offset): the fallback's 16 cm are measured from it */
	EXPECT(ok5 && g_hpa_bad == 0 && g_hpa_calls - calls0 == 2 * (7 * 5 - 3 * 5) &&
		       log_has("HEADPARTS first skip in the gbuffer pass (HP_DRAW_GBUF)") &&
		       log_has("HEADPARTS first skip in the deep pass") && log_has("HEADPARTS first skip in the velocity pass") &&
		       log_has("the player's model") && log_has("slot 1 (head) 0 -> ") && g_hp.published >= 2,
	       "stereo: %s; bad args %llu, submits %llu", why, (unsigned long long)g_hpa_bad,
	       (unsigned long long)(g_hpa_calls - calls0));
	EXPECT(hpc_scene_ok(0, why, sizeof why) && g_hpca_bad == 0 && g_hpca_calls > 0,
	       "stereo, slots 1,3,4: the cloth %s, bad custom args %llu", why, (unsigned long long)g_hpca_bad);
	EXPECT(g_h4_view[0].near_z == HV_NEAR_CM && g_h4_view[1].near_z == HV_NEAR_CM && !g_hv.on && g_fr.head_ok,
	       "stereo: near %g/%g, the eye offset on %d, head base %d", g_h4_view[0].near_z, g_h4_view[1].near_z, g_hv.on,
	       g_fr.head_ok);
	/* outside an eye window (the HUD, a UI scene after H8): drawn */
	memset(g_hpd, 0, sizeof g_hpd);
	hp_draws(4);
	int out_ok = g_rt_eye < 0;
	for (int p = 0; p <= SOMVR_HP_PASSES; p++)
		out_ok &= g_hpd[4][p] == hp_all();
	EXPECT(out_ok, "outside an eye: eye %d, gbuffer drew %#x", g_rt_eye, g_hpd[4][SOMVR_HP_GBUF]);
	/* 6. a flat frame (no XR views): drawn */
	g_views_fake = 0;
	atomic_store(&g_sh.xr.state, SOMVR_XRS_DEAD);
	hp_frame_run();
	int flat_ok = g_h4_calls == 1;
	for (int p = 0; p < SOMVR_HP_PASSES; p++)
		flat_ok &= g_hpd[0][p] == hp_all();
	EXPECT(flat_ok && g_hp.flat > 0, "flat: calls %d, gbuffer drew %#x", g_h4_calls, g_hpd[0][SOMVR_HP_GBUF]);
	g_views_fake = 1;
	/* 7. the camera base for a frame (no head transform): drawn */
	g_nodexf_fail = 1;
	hp_frame_run();
	g_nodexf_fail = 0;
	int cam_ok = hp_scene_ok(0, why, sizeof why);
	hp_frame_run();
	EXPECT(cam_ok && hp_scene_ok(HID, why, sizeof why), "camera base: drawn %d, then %s", cam_ok, why);
	/* 8. a rebuild (a new head renderable in slot 1): the new node skipped, the old one (orphaned) drawn */
	g_sm.slot[1] = &g_prend[5];
	g_hpn[7] = (uintptr_t)&g_phandle[5].node;
	hp_frame_run();
	EXPECT(hp_scene_ok(0x98, why, sizeof why) && log_has("slot 1 (head) 0x"), "rebuild: %s", why);
	g_sm.slot[1] = &g_prend[1];
	g_hpn[7] = 0;
	hp_frame_run();
	EXPECT(hp_scene_ok(HID, why, sizeof why), "rebuild back: %s", why);
	/* 9. a part still loading (no handle yet): drawn, said once; then hidden */
	g_prend[3].handle = NULL;
	hp_frame_run();
	hp_frame_run();
	int loading = hp_scene_ok(0x0c, why, sizeof why) && log_has("slot 4 (hair): renderable") &&
		      log_has("has no skeletal node yet");
	g_prend[3].handle = &g_phandle[3];
	hp_frame_run();
	EXPECT(loading && hp_scene_ok(HID, why, sizeof why), "loading: %d, then %s", loading, why);
	/* 10. the trigger words */
	snprintf(g_trig_path, sizeof g_trig_path, "%s", trig);
	set_trigger(trig, "on");
	set_trigger(trig, "headparts 1");
	hp_frame_run();
	int w1 = hp_scene_ok(0x04, why, sizeof why) && log_has("the slots are now 1 (trigger word)") &&
		 log_has("TRIGGER cmd=headparts t=");
	set_trigger(trig, "headparts x");
	hp_frame_run();
	int wx = hp_scene_ok(0x04, why, sizeof why) && log_has("headparts (not a slot list 0..19: ignored)");
	set_trigger(trig, "headparts 1,3,4");
	hp_frame_run();
	EXPECT(w1 && wx && hp_scene_ok(HID, why, sizeof why) && g_hp.slots == SOMVR_HP_DEFAULT,
	       "trigger words: 1 %d, x %d, 1,3,4 %s", w1, wx, why);
	/* 10b. the cloth (26.7): "main" = the body (the base renderable's node, g_hpn[0]) and the cloth's two display meshes
	 * (g_hpcn[0], [1]) skipped in both eyes, drawn in the shadows; entry 2 (not in the set) and the orc's custom node
	 * drawn; "body" draws the cloth, "cloth" draws the body */
	set_trigger(trig, "headparts main");
	hp_frame_run();
	hp_frame_run();
	int mb = hp_scene_ok(0x01, why, sizeof why);
	char why2[200];
	int mc = hpc_scene_ok(0x03, why2, sizeof why2);
	EXPECT(mb && mc && g_hpca_bad == 0 && log_has("the slots are now main (trigger word)") &&
		       log_has("the player's cloth node 0x") && log_has("2 display mesh entries (the set read), 2 node(s) not drawn") &&
		       log_has("descriptors 2 'Talion_Cloak_Display' 'Talion_Fur_Display'") &&
		       log_has("HEADPARTS first skip in the gbuffer pass (HP_CUST_GBUF_A): node 0x") &&
		       log_has("HEADPARTS first skip in the late pass (HP_CUST_LATE_B)") && log_has("= cloth (kind 0, category 0"),
	       "headparts main: body %s, cloth %s, bad custom args %llu", why, why2, (unsigned long long)g_hpca_bad);
	/* SOMVR_HP_POSE (on by default): a node animated by the hidden body's skeletal transforms (the wraith over Talion,
	 * run 034707; here the orc's stand-in node given the body's) is hidden with the body in both eyes and drawn in the
	 * shadows; "hppose off" draws it again; transforms whose vtable is not CSkeletalTransforms' hide nothing more */
	{
		static uintptr_t skt[4] = { HP_SKT_VT }, skt_bad[4] = { 0x1234 };
		st_node_t *bn = (st_node_t *)g_hpn[0], *on = (st_node_t *)g_hpn[6];
		uintptr_t sp = (uintptr_t)skt, bp = (uintptr_t)skt_bad;
		memcpy((char *)bn + SK_POSE, &sp, 8), memcpy((char *)bn + SK_POSE + 8, &sp, 8), memcpy((char *)on + SK_POSE, &sp, 8);
		hp_frame_run();
		hp_frame_run();
		int pz = hp_scene_ok(0x41, why, sizeof why);
		set_trigger(trig, "hppose off");
		hp_frame_run();
		int po = hp_scene_ok(0x01, why2, sizeof why2);
		set_trigger(trig, "hppose on");
		hp_frame_run();
		int pa = hp_scene_ok(0x41, why, sizeof why);
		memcpy((char *)bn + SK_POSE, &bp, 8), memcpy((char *)bn + SK_POSE + 8, &bp, 8), memcpy((char *)on + SK_POSE, &bp, 8);
		hp_frame_run();
		int pb = hp_scene_ok(0x01, why2, sizeof why2);
		EXPECT(pz && po && pa && pb && log_has("is animated by the skeletal transforms 0x") &&
			       log_has("shares the hidden body's skeletal transforms") && log_has("HEADPARTS hppose off") &&
			       log_has("is not its CSkeletalTransforms"),
		       "SOMVR_HP_POSE: on %d, off %d, on again %d, a bad vtable %d (%s / %s)", pz, po, pa, pb, why, why2);
		memset((char *)bn + SK_POSE, 0, 16), memset((char *)on + SK_POSE, 0, 8);
	}
	set_trigger(trig, "headparts body");
	hp_frame_run();
	mb = hp_scene_ok(0x01, why, sizeof why), mc = hpc_scene_ok(0, why2, sizeof why2);
	EXPECT(mb && mc && log_has("the slots are now body (trigger word)"), "headparts body: body %s, cloth %s", why, why2);
	set_trigger(trig, "headparts cloth");
	hp_frame_run();
	mb = hp_scene_ok(0, why, sizeof why), mc = hpc_scene_ok(0x03, why2, sizeof why2);
	EXPECT(mb && mc, "headparts cloth: body %s, cloth %s", why, why2);
	/* a third display mesh whose visual is not a CPhysicsClothVisualization: drawn, said; then a good one: hidden */
	g_cset.e = &g_centry[3];
	g_cvis[2].vt = (uintptr_t)g_vt_chandle;
	hp_frame_run();
	mc = hpc_scene_ok(0x03, why2, sizeof why2);
	int said = log_has("3 display mesh entries (the set read), 2 node(s) not drawn") &&
		   log_has("(entries without a custom node: drawn while so)");
	g_cvis[2].vt = (uintptr_t)g_vt_vis;
	hp_frame_run();
	EXPECT(mc && said && hpc_scene_ok(0x07, why, sizeof why), "a bad visual: %s (said %d), then %s", why2, said, why);
	g_cset.e = &g_centry[2];
	/* a model without cloth (+0x88 = 0): all drawn, said once */
	g_sm.cloth = NULL;
	hp_frame_run();
	mc = hpc_scene_ok(0, why2, sizeof why2);
	g_sm.cloth = &g_cloth;
	hp_frame_run();
	EXPECT(mc && log_has("has no cloth node (+0x88): no cloth to hide") && hpc_scene_ok(0x03, why, sizeof why),
	       "no cloth node: %s, then %s", why2, why);
	set_trigger(trig, "headparts 1,3,4");
	hp_frame_run();
	EXPECT(hp_scene_ok(HID, why, sizeof why) && hpc_scene_ok(0, why2, sizeof why2), "back to 1,3,4: %s, cloth %s", why, why2);
	set_trigger(trig, NULL);
	snprintf(g_trig_path, sizeof g_trig_path, "%s", tp0);
	/* 11. the 5 s line, the summary */
	g_hp.next_log = 0;
	hp_frame_run();
	hp_end_log();
	EXPECT(log_has("HEADPARTS t=") && log_has("nodes head=0x") && log_has("skipped/calls deep=") &&
		       log_has("; cloth on skipped/calls deep=") && log_has("SUMMARY head parts: on; frames") &&
		       log_has("SUMMARY head parts cloth on skipped/calls deep=") && log_has("cloth nodes total "),
	       "parts: the 5 s line / the summary");
	/* 11b. SOMVR_HP_NEARLOG: the orc's custom node and the orc's skeletal node with a box at the head point are logged
	 * (once each), a node far away is not; the hidden nodes never */
	setenv("SOMVR_HP_NEARLOG", "1", 1);
	hb_config();
	unsetenv("SOMVR_HP_NEARLOG");
	hp_frame_run();
	float at[3];
	memcpy(at, g_fr.head, sizeof at);
	float lo[3] = { at[0] - 10, at[1] - 30, at[2] - 10 }, hi[3] = { at[0] + 10, at[1] + 5, at[2] + 10 };
	memcpy((char *)&g_orc_chandle.node + NODE_BOX, lo, 12), memcpy((char *)&g_orc_chandle.node + NODE_BOX + 0x10, hi, 12);
	memcpy((char *)&g_orc_handle.node + NODE_BOX, lo, 12), memcpy((char *)&g_orc_handle.node + NODE_BOX + 0x10, hi, 12);
	hp_frame_run();
	hp_frame_run();
	char want[96], want2[96];
	snprintf(want, sizeof want, "HEADPARTS near: node %#lx (vtable", (unsigned long)&g_orc_chandle.node);
	snprintf(want2, sizeof want2, "HEADPARTS near: node %#lx (vtable", (unsigned long)&g_orc_handle.node);
	int nl = g_hp_near_cm == 60 && log_has("CONFIG head parts near-log: every node an eye pass still draws") && log_has(want) &&
		 log_has(want2) && log_has("0.0 cm from the head point") && atomic_load(&g_hp_near_n) == 2 &&
		 atomic_load(&g_hp_near_hits) > 2;
	setenv("SOMVR_HP_NEARLOG", "x", 1);
	hb_config();
	unsetenv("SOMVR_HP_NEARLOG");
	EXPECT(nl && g_hp_near_cm == 0 && log_has("WARN SOMVR_HP_NEARLOG=x is not 1 or a distance"),
	       "near-log: %d (cm now %g, nodes %d hits %llu)", nl, g_hp_near_cm, atomic_load(&g_hp_near_n),
	       (unsigned long long)atomic_load(&g_hp_near_hits));
	memset((char *)&g_orc_chandle.node + NODE_BOX, 0, 0x20), memset((char *)&g_orc_handle.node + NODE_BOX, 0, 0x20);
	/* 12. the run-time fallback: the model there, no head node for HP_EMPTY_FRAMES stereo frames running */
	g_sm.slot[1] = NULL;
	for (int i = 0; i < HP_EMPTY_FRAMES && hp_live(); i++)
		hp_frame_run();
	EXPECT(!hp_live() && g_hp.fell_back && g_hb.near && g_hv.on && g_hv.cfg.fwd_cm == 16 && g_hv.cfg.min_cm == 10 &&
		       g_hv.near_cm == HV_NEAR_CM && log_has("had no head node (slot 1) for 300 stereo frames running") &&
		       log_has("falling back to near") && log_has("(SOMVR_HIDE_HEAD=parts fell back to near): fwd_cm=16.0"),
	       "run-time fallback: live %d fell back %d near %d eye on %d fwd %g", hp_live(), g_hp.fell_back, g_hb.near, g_hv.on,
	       g_hv.cfg.fwd_cm);
	g_sm.slot[1] = &g_prend[1];
	hp_frame_run();
	hp_frame_run();
	m = eye_mid();
	float dxz = sqrtf((m.x - m0.x) * (m.x - m0.x) + (m.z - m0.z) * (m.z - m0.z));
	EXPECT(hp_scene_ok(0, why, sizeof why) && g_h4_view[0].near_z == HV_NEAR_CM && NEAR(dxz, 16, 0.05) &&
		       NEAR(m.y, m0.y, 1e-3),
	       "after the fallback: %s, near %g, the eye %.3f cm from the parts eye (want 16 ahead)", why, g_h4_view[0].near_z,
	       dxz);
	printf("hidehead parts: the five FUN_0081ff80 calls (all or none), the head/helm/hair nodes skipped in both eyes' five "
	       "camera passes and drawn in the shadow maps; outside the eyes, flat, the camera base drawn; a rebuild, a "
	       "loading part, the words; no offset and the 6 cm plane (12 with near); the fallbacks to near; the cloth's "
	       "eight FUN_007ccec0 calls (all or none, its own fail-soft), main/body/cloth, a bad visual, no cloth; the "
	       "near-log\n");
	/* back as the earlier tests left it (the hooks stay in, passing through) */
	memset(g_hpn, 0, sizeof g_hpn), memset(g_hpcn, 0, sizeof g_hpcn);
	atomic_store(&g_hpc.on, 0);
	atomic_store(&g_hp.on, 0);
	g_hp.want = 0;
	memset(g_sm.slot, 0, sizeof g_sm.slot);
	setenv("SOMVR_HIDE_HEAD", "3,5", 1);
	setenv("SOMVR_EYE_FWD_CM", "0", 1), setenv("SOMVR_EYE_MIN_CM", "0", 1);
	g_rigcfg = cfg0;
	hv_config(&bad);
	g_hb.near = near0, g_hb.want = 0, g_hb.words = 0;
	g_hide.on = hide0, g_layout = layout0, g_views_fake = fake0, g_near_cm = near_cm0;
	g_pl = NULL;
}

/* ---- SOMVR_PRACTICE (pr_tests): stand-ins for its ten read-only sites (the template by name, the debug spawn, the
 * zone, the level, SetMinHealth, the TOD's name; the physics cast, the NPC's zone slot, the object system's removal,
 * CGameServices' weather state), the console's Teleport / AdvanceTimeOfDay / ChangeWeather (st_runstring's hook) and a
 * world of band orcs around the practice spot ------------------------------------------------------------------- */
typedef struct { uintptr_t vt; float pos[3], rot[4]; char p0[PR_HOBJ_GAMEOBJ - 0x24]; void *gameobj; char p1[PR_HOBJ_CLASSES - PR_HOBJ_GAMEOBJ - 8];
		 void *classes; char p2[PR_HOBJ_FLAGS - PR_HOBJ_CLASSES - 8]; uint8_t flags; char p3[0x70 - PR_HOBJ_FLAGS - 1]; } st_prh_t;
typedef struct { uintptr_t vt; char p0[CH_FLAGS - 8]; uint8_t flags; char p1[PR_CH_COMBAT - CH_FLAGS - 1]; void *combat; } st_prnpc_t;
typedef struct { uintptr_t vt; char p0[PR_CH_HEALTH - 8]; float health; char p1[PR_CH_MINHEALTH - PR_CH_HEALTH - 4]; float minhealth; } st_prpc_t;
_Static_assert(offsetof(st_prh_t, pos) == OBJ_POS && offsetof(st_prh_t, gameobj) == PR_HOBJ_GAMEOBJ &&
		       offsetof(st_prh_t, classes) == PR_HOBJ_CLASSES && offsetof(st_prh_t, flags) == PR_HOBJ_FLAGS &&
		       offsetof(st_prnpc_t, flags) == CH_FLAGS && offsetof(st_prnpc_t, combat) == PR_CH_COMBAT &&
		       offsetof(st_prpc_t, health) == PR_CH_HEALTH && offsetof(st_prpc_t, minhealth) == PR_CH_MINHEALTH,
	       "stand-in practice layouts");
#define PR_ST_N 10 /* the world's orcs: 0 A infantry, 1 S a slave, 2 W a warg, 3 F far, 4 D dead, 5 M the mount, 6 B later,
		    * 7 E another, 8/9 our spawns */
static st_prh_t g_prh[PR_ST_N], g_pr_ground;
static st_prnpc_t g_prnpc[PR_ST_N];
static st_combat_t g_prc[PR_ST_N];
static st_prpc_t g_prpc;
static st_band_t g_prband[2][8];
static char g_prrec[8][0x30];
static const char *const g_prrec_names[8] = { "AI_Infantry", "AI_Slave_Human", "Warg_Brown", "Orc_Infantry", "Orc_Defender",
					      "Orc_Berserker", "Caragor_Saddled", "AI_Defender" };
static char g_prwrec[4][0x30];
static const char *const g_prwrec_names[4] = { "SON_Night_01", "SON_Day02", "DayNoRain", "SON_Day03_Rain" };
static struct { char p[PR_W_RECORD]; void *rec; uint8_t tod; } g_prw;
static void *volatile st_pr_vt_npc[96], *volatile st_pr_vt_phys[70], *volatile st_pr_vt_os[16], *volatile st_pr_vt_gsvc[24],
	*volatile st_pr_vt_pobj[4];
static struct { uintptr_t vt; } g_pr_phys, g_pr_os, g_pr_gsvc;
static void *g_pr_phys_ptr = &g_pr_phys, *g_pr_os_ptr = &g_pr_os, *g_pr_gsvc_ptr = &g_pr_gsvc;
static uintptr_t g_pr_cls_npc[2], g_pr_cls_other[2], g_pr_cls_cell, g_pr_ret[2][2];
static uint32_t g_pr_level = 7;
static struct site_spec g_prs[PRS_N];
static char g_prhex[PRS_N][256];
/* what the stand-ins saw */
static int g_pr_spawns, g_pr_spawn_slot, g_pr_ray_miss, g_pr_spawn_refuse, g_pr_tp_block, g_pr_removes, g_pr_zone_calls,
	g_pr_level_calls, g_pr_mh_calls, g_pr_ncmd;
static uintptr_t g_pr_spawn_ctx, g_pr_spawn_tmpl, g_pr_removed[16], g_pr_zone_npc;
static float g_pr_spawn_xf[7], g_pr_ground_y = 3900, g_pr_ray_from[3], g_pr_ray_to[3];
static uint32_t g_pr_ray_flags[2], g_pr_level_seen;
static char g_pr_spawn_name[64], g_pr_cmds[24][160];

__attribute__((noipa)) uintptr_t st_pr_byname(const char *name)
{
	for (int i = 3; i <= 5; i++)
		if (!strcmp(name, g_prrec_names[i]) && !(g_pr_spawn_refuse & 2))
			return (uintptr_t)g_prrec[i];
	return 0;
}
__attribute__((noipa)) uintptr_t st_pr_spawn(uintptr_t ctx, const char *name, const float *xf, uintptr_t tmpl, int a5, int a6,
					   uintptr_t a7, uintptr_t fm, uintptr_t a9)
{
	g_pr_spawns++;
	g_pr_spawn_ctx = ctx, g_pr_spawn_tmpl = tmpl;
	snprintf(g_pr_spawn_name, sizeof g_pr_spawn_name, "%s", name);
	memcpy(g_pr_spawn_xf, xf, sizeof g_pr_spawn_xf);
	EXPECT(a5 == 1 && a6 == 1 && !a7 && !fm && !a9, "the spawn's flags: %d %d %#lx %#lx %#lx", a5, a6, (unsigned long)a7,
	       (unsigned long)fm, (unsigned long)a9);
	if (g_pr_spawn_refuse & 1)
		return 0;
	int k = 8 + (g_pr_spawn_slot++ & 1);
	memset(&g_prh[k], 0, sizeof g_prh[k]);
	memset(&g_prnpc[k], 0, sizeof g_prnpc[k]);
	memset(&g_prc[k], 0, sizeof g_prc[k]);
	memcpy(g_prh[k].pos, xf, 3 * sizeof(float));
	g_prh[k].rot[3] = 1, g_prh[k].gameobj = &g_prnpc[k], g_prh[k].classes = &g_pr_cls_cell;
	g_prnpc[k].vt = (uintptr_t)st_pr_vt_npc, g_prnpc[k].combat = &g_prc[k];
	g_prc[k].vt = (uintptr_t)st_vt_combat, g_prc[k].owner = (st_char_t *)&g_prnpc[k], g_prc[k].ai = (void *)1;
	g_prc[k].hobj = (st_hobj_t *)&g_prh[k];
	uintptr_t r = tmpl == (uintptr_t)g_prrec[4] ? (uintptr_t)g_prrec[7] : (uintptr_t)g_prrec[0];
	memcpy((char *)&g_prc[k] + PR_C_CHARREC, &r, 8);
	g_prband[1][7].c = &g_prc[k]; /* in the bands, like any orc (the clear must skip it) */
	g_pr_ret[k - 8][1] = (uintptr_t)&g_prh[k];
	return (uintptr_t)g_pr_ret[k - 8];
}
__attribute__((noipa)) uintptr_t st_pr_zone(const float *pos, uintptr_t a, uintptr_t b)
{
	(void)pos, (void)a, (void)b;
	return 0x2345;
}
__attribute__((noipa)) void st_pr_setzone(uintptr_t npc, uintptr_t zone, int x)
{
	g_pr_zone_calls++, g_pr_zone_npc = npc;
	EXPECT(zone == 0x2345 && x == 1, "the zone call: %#lx %d", (unsigned long)zone, x);
}
__attribute__((noipa)) void st_pr_level(uintptr_t c, uint32_t lvl)
{
	(void)c;
	g_pr_level_calls++, g_pr_level_seen = lvl;
}
__attribute__((noipa)) void st_pr_minhealth(uintptr_t ch, float v)
{
	g_pr_mh_calls++;
	memcpy((char *)ch + PR_CH_MINHEALTH, &v, sizeof v); /* FUN_0269da10: movss [rdi+0x9b4], xmm0 */
}
__attribute__((noipa)) uintptr_t st_pr_todname(uint32_t tod) { return (uintptr_t)(tod == 1 ? "Night" : "Day"); }
__attribute__((noipa)) uint64_t st_pr_ray(uintptr_t phys, void *q, void *r)
{
	EXPECT(phys == (uintptr_t)&g_pr_phys, "the cast's this %#lx", (unsigned long)phys);
	memcpy(g_pr_ray_from, q, 12), memcpy(g_pr_ray_to, (char *)q + 12, 12), memcpy(g_pr_ray_flags, (char *)q + 0x18, 8);
	if (g_pr_ray_miss)
		return 0;
	float hit[3] = { g_pr_ray_from[0], g_pr_ground_y, g_pr_ray_from[2] };
	uintptr_t h = (uintptr_t)&g_pr_ground;
	memcpy(r, hit, 12), memcpy((char *)r + 0x20, &h, 8);
	return 1;
}
__attribute__((noipa)) uint64_t st_pr_remove(uintptr_t os, uintptr_t h)
{
	EXPECT(os == (uintptr_t)&g_pr_os, "the removal's this %#lx", (unsigned long)os);
	((st_prh_t *)h)->flags |= 8;
	if (g_pr_removes < 16)
		g_pr_removed[g_pr_removes] = h;
	g_pr_removes++;
	return 0;
}
__attribute__((noipa)) uintptr_t st_pr_wstate(uintptr_t g)
{
	return g == (uintptr_t)&g_pr_gsvc ? (uintptr_t)&g_prw : 0;
}
/* the console: each ';' command logged; Teleport moves Talion's object (unless blocked), the TOD and weather change */
static void pr_st_console(const char *s)
{
	char buf[320];
	snprintf(buf, sizeof buf, "%s", s);
	for (char *save = NULL, *c = strtok_r(buf, ";", &save); c; c = strtok_r(NULL, ";", &save)) {
		while (*c == ' ')
			c++;
		if (g_pr_ncmd < 24)
			snprintf(g_pr_cmds[g_pr_ncmd++], sizeof g_pr_cmds[0], "%s", c);
		long x, y, z;
		char w[64];
		if (sscanf(c, "Teleport %ld %ld %ld", &x, &y, &z) == 3 && !g_pr_tp_block)
			g_pobj.pos[0] = (float)x, g_pobj.pos[1] = (float)y, g_pobj.pos[2] = (float)z;
		else if (!strcmp(c, "GameServerShell.AdvanceTimeOfDay"))
			g_prw.tod ^= 1, g_prw.rec = g_prwrec[g_prw.tod ? 0 : 1];
		else if (sscanf(c, "GameServerShell.ChangeWeather %63s", w) == 1)
			for (int i = 0; i < 4; i++)
				if (!strcmp(w, g_prwrec_names[i]))
					g_prw.rec = g_prwrec[i];
	}
}
static int pr_cmd_has(const char *c)
{
	for (int i = 0; i < g_pr_ncmd; i++)
		if (!strcmp(g_pr_cmds[i], c))
			return i + 1;
	return 0;
}
static void pr_entry(void *fn, struct site_spec *s, const char *name, int i)
{
	hex_of(fn, 8, g_prhex[i], 128);
	hex_of(fn, 4, g_prhex[i] + 128, 128);
	*s = (struct site_spec){ name, K_ENTRY, (uintptr_t)fn, (uintptr_t)fn, g_prhex[i], (uintptr_t)fn, g_prhex[i] + 128 };
}
static void pr_vslot(void *volatile *slot, void *target, struct site_spec *s, const char *name, int i)
{
	*slot = target;
	hex_of((const unsigned char *)slot, 8, g_prhex[i], 128);
	hex_of(target, 4, g_prhex[i] + 128, 128);
	*s = (struct site_spec){ name, K_VSLOT, (uintptr_t)slot, (uintptr_t)slot, g_prhex[i], (uintptr_t)target,
				 g_prhex[i] + 128 };
}
/* SOMVR_PRACTICE_QUIET: stand-ins of the spawn manager's three calls (FUN_0286fb80/90/a0: no arguments, they load the
 * manager pointer themselves) and of its flag's store FUN_02919d70; a manager with its flag at +0x164 and three ambient AI
 * in its list */
static struct { char p0[PRQ_MGR_LIST]; uintptr_t b, e; char p1[PRQ_MGR_FLAG - PRQ_MGR_LIST - 16]; uint8_t flag; } g_prq_mgr,
	g_prq_mgr2;
_Static_assert(offsetof(typeof(g_prq_mgr), b) == PRQ_MGR_LIST && offsetof(typeof(g_prq_mgr), flag) == PRQ_MGR_FLAG,
	       "stand-in spawn manager");
static void *volatile g_prq_mgr_ptr = &g_prq_mgr;
static char g_prq_list[3 * PRQ_ENTRY];
static int g_prq_en, g_prq_dis, g_prq_rm;
static struct site_spec g_prqs[PRQ_N];
static char g_prqhex[PRQ_N][256];
__attribute__((noipa)) void st_prq_enable(void) { g_prq_en++, ((typeof(g_prq_mgr) *)g_prq_mgr_ptr)->flag = 1; }
__attribute__((noipa)) void st_prq_disable(void) { g_prq_dis++, ((typeof(g_prq_mgr) *)g_prq_mgr_ptr)->flag = 0; }
__attribute__((noipa)) void st_prq_removeall(void) { g_prq_rm++; }
__attribute__((noipa)) void st_prq_flag_on(void *m) { ((typeof(g_prq_mgr) *)m)->flag = 1; }
static void prq_entry(void *fn, const char *name, int i)
{
	hex_of(fn, 8, g_prqhex[i], 128);
	hex_of(fn, 4, g_prqhex[i] + 128, 128);
	g_prqs[i] = (struct site_spec){ name, K_ENTRY, (uintptr_t)fn, (uintptr_t)fn, g_prqhex[i], (uintptr_t)fn, g_prqhex[i] + 128 };
}
/* build 17: the practice page's refills - stand-ins of FUN_02720a90 (the tokens add, clamped to the max), FUN_0271e2e0
 * (the energy and +0x54 = the max, the unlimited flag stored) and FUN_02763910 (the heal up to the max) on a stand-in
 * resource object and the stand-in CPlayerObj's health component */
static struct site_spec g_prfs[PRF_N];
static char g_prfhex[PRF_N][256];
static char g_prf_res[0x80];
static int g_prf_calls[PRF_N];
__attribute__((noipa)) void st_prf_tokens(uintptr_t res, int d)
{
	int32_t n, mx;
	memcpy(&n, (char *)res + BO_RES_COUNT, 4), memcpy(&mx, (char *)res + PRF_RES_MAX, 4);
	n = n + d > mx ? mx : n + d < 0 ? 0 : n + d;
	memcpy((char *)res + BO_RES_COUNT, &n, 4), g_prf_calls[PRF_TOKENS_ADD]++;
}
__attribute__((noipa)) void st_prf_energy(uintptr_t res, int unl)
{
	float m;
	memcpy(&m, (char *)res + PRF_RES_EMAX, 4);
	((char *)res)[PRF_RES_EUNLIM] = (char)unl;
	memcpy((char *)res + 0x54, &m, 4), memcpy((char *)res + PRF_RES_ENERGY, &m, 4), g_prf_calls[PRF_ENERGY_FULL]++;
}
__attribute__((noipa)) int st_prf_heal(uintptr_t h, float a)
{
	float hp, mx;
	memcpy(&mx, (char *)h + 0x30, 4), memcpy(&hp, (char *)h + 0x34, 4);
	if (hp >= mx)
		return 0;
	hp = hp + a > mx ? mx : hp + a;
	memcpy((char *)h + 0x34, &hp, 4), g_prf_calls[PRF_HEAL]++;
	return 1;
}
static void prf_entry(void *fn, const char *name, int i)
{
	hex_of(fn, 8, g_prfhex[i], 128);
	hex_of(fn, 4, g_prfhex[i] + 128, 128);
	g_prfs[i] = (struct site_spec){ name, K_ENTRY, (uintptr_t)fn, (uintptr_t)fn, g_prfhex[i], (uintptr_t)fn, g_prfhex[i] + 128 };
}
static void pr_stand_in_sites(void)
{
	prf_entry((void *)st_prf_tokens, "PRF_TOKENS_ADD", PRF_TOKENS_ADD);
	prf_entry((void *)st_prf_energy, "PRF_ENERGY_FULL", PRF_ENERGY_FULL);
	prf_entry((void *)st_prf_heal, "PRF_HEAL", PRF_HEAL);
	g_prf_sites = g_prfs;
	prq_entry((void *)st_prq_enable, "PRQ_ENABLE", PRQ_ENABLE);
	prq_entry((void *)st_prq_disable, "PRQ_DISABLE", PRQ_DISABLE);
	prq_entry((void *)st_prq_removeall, "PRQ_REMOVEALL", PRQ_REMOVEALL);
	prq_entry((void *)st_prq_flag_on, "PRQ_FLAG_ON", PRQ_FLAG_ON);
	g_prq_sites = g_prqs;
	g_prg.spawnmgr_pp = (uintptr_t)&g_prq_mgr_ptr;
	g_prq_mgr.flag = 1, g_prq_mgr.b = (uintptr_t)g_prq_list, g_prq_mgr.e = (uintptr_t)g_prq_list + sizeof g_prq_list;
	pr_entry((void *)st_pr_byname, &g_prs[PRS_BYNAME], "PR_TMPL_BYNAME", PRS_BYNAME);
	pr_entry((void *)st_pr_spawn, &g_prs[PRS_SPAWN], "PR_SPAWN_AI", PRS_SPAWN);
	pr_entry((void *)st_pr_zone, &g_prs[PRS_ZONE], "PR_ZONE_AT", PRS_ZONE);
	pr_entry((void *)st_pr_level, &g_prs[PRS_LEVEL], "PR_SETLEVEL", PRS_LEVEL);
	pr_entry((void *)st_pr_minhealth, &g_prs[PRS_MINHEALTH], "PR_SETMINHEALTH", PRS_MINHEALTH);
	pr_entry((void *)st_pr_todname, &g_prs[PRS_TODNAME], "PR_TOD_NAME", PRS_TODNAME);
	pr_vslot(&st_pr_vt_phys[PR_SLOT_RAY / 8], (void *)st_pr_ray, &g_prs[PRS_RAY], "PR_RAYCAST", PRS_RAY);
	pr_vslot(&st_pr_vt_npc[PR_SLOT_SETZONE / 8], (void *)st_pr_setzone, &g_prs[PRS_SETZONE], "PR_NPC_SETZONE", PRS_SETZONE);
	pr_vslot(&st_pr_vt_os[PR_SLOT_REMOVE / 8], (void *)st_pr_remove, &g_prs[PRS_REMOVE], "PR_OBJSYS_REMOVE", PRS_REMOVE);
	pr_vslot(&st_pr_vt_gsvc[PR_SLOT_WEATHER / 8], (void *)st_pr_wstate, &g_prs[PRS_WEATHER], "PR_WEATHER", PRS_WEATHER);
	g_pr_phys.vt = (uintptr_t)st_pr_vt_phys, g_pr_os.vt = (uintptr_t)st_pr_vt_os, g_pr_gsvc.vt = (uintptr_t)st_pr_vt_gsvc;
	g_prg.objsys_pp = (uintptr_t)&g_pr_os_ptr, g_prg.phys_pp = (uintptr_t)&g_pr_phys_ptr;
	g_prg.gsvc_pp = (uintptr_t)&g_pr_gsvc_ptr, g_prg.class_npc = (uintptr_t)g_pr_cls_npc;
	g_prg.spawn_level = (uintptr_t)&g_pr_level, g_prg.vt_playerobj = (uintptr_t)st_pr_vt_pobj;
	g_pr_cls_other[1] = (uintptr_t)g_pr_cls_npc, g_pr_cls_cell = (uintptr_t)g_pr_cls_other; /* other -> NPC */
	g_pr_sites = g_prs;
}
/* one band orc k: its record r, dist m from the spot (straight east), dead or not */
static void pr_orc(int k, int r, float m, int dead)
{
	memset(&g_prh[k], 0, sizeof g_prh[k]), memset(&g_prnpc[k], 0, sizeof g_prnpc[k]), memset(&g_prc[k], 0, sizeof g_prc[k]);
	g_prh[k].pos[0] = 40400 + m * 100, g_prh[k].pos[1] = 3900, g_prh[k].pos[2] = -21600, g_prh[k].rot[3] = 1;
	g_prh[k].gameobj = &g_prnpc[k];
	g_prnpc[k].vt = (uintptr_t)st_pr_vt_npc, g_prnpc[k].combat = &g_prc[k];
	g_prc[k].vt = (uintptr_t)st_vt_combat, g_prc[k].owner = (st_char_t *)&g_prnpc[k], g_prc[k].ai = (void *)1;
	g_prc[k].hobj = (st_hobj_t *)&g_prh[k], g_prc[k].dead = (uint8_t)dead;
	uintptr_t rec = (uintptr_t)g_prrec[r];
	memcpy((char *)&g_prc[k] + PR_C_CHARREC, &rec, 8);
}
static void pr_step(int n) /* n game frames at 90 Hz */
{
	for (int i = 0; i < n; i++) {
		g_pr_test_t += 1.0 / 90;
		pr_frame((uintptr_t)g_fbP);
	}
}
static int pr_removed(int k)
{
	for (int i = 0; i < g_pr_removes && i < 16; i++)
		if (g_pr_removed[i] == (uintptr_t)&g_prh[k])
			return 1;
	return 0;
}
static float pr_mh_now(void) { return g_prpc.minhealth; }

/* ---- SOMVR_BODY=hands, SOMVR_SWORD_ANGLE, SOMVR_HANDS_TEST (somvr_body_game.c): the slot on a stand-in renderable
 * vtable, the player's renderable handed an edited copy (the body at the wrists, the hands kept), others and flat frames
 * the game's own, a proof mismatch, the eye passes' word for SOMVR_HEAD_PARTS=main, the words, the angle, a test grip -- */
static void *volatile g_bd_vt[4];
static const void *g_bd_seen_skin;
static sa_node g_bd_seen[RUN3_N];
static uint64_t g_bd_seen_a8;
__attribute__((noipa)) uint64_t st_bd_copy(void *self, void *skel, void *mpose, void *skin, uint64_t count, void *hdr,
					   uint64_t a7, uint64_t a8)
{
	(void)self, (void)skel, (void)mpose, (void)hdr, (void)a7;
	g_bd_seen_skin = skin, g_bd_seen_a8 = a8;
	if (skin && (uint32_t)count == RUN3_N)
		memcpy(g_bd_seen, skin, sizeof g_bd_seen);
	return 0x5eed;
}
__attribute__((noipa)) uint64_t st_bd_call(uint64_t a) { return a * 7 + 1; }
__attribute__((noipa)) uint64_t st_bd_src(uint64_t a) { return a * 9 + 2; }
__attribute__((noipa)) uint64_t st_bd_loop(uint64_t a) { return a * 11 + 3; }
static void bd_tests(void)
{
	int arm_on0 = g_arm.on, chains0 = g_arm.chains_ok, tid0 = g_arm.tid;
	int base0 = g_rigcfg.base;
	uint32_t nodes0 = g_arm.nodes;
	uintptr_t skel0 = g_arm.skel, model0 = g_arm.model, rend0 = atomic_load(&g_renderer);
	float w0[2] = { g_arm.weight[0], g_arm.weight[1] }, cal0[2][6];
	sa_chain ch0[2];
	sa_xf calx0[2] = { g_arm.cal[0], g_arm.cal[1] };
	memcpy(ch0, g_arm.ch, sizeof ch0), memcpy(cal0, g_arm.cal_v, sizeof cal0);
	float pitch0 = g_cb.bcfg.pitch_rad;
	/* the world: run 3's skeleton as the arms' (g_skel), both chains, the player's model with its main renderable */
	arm_world();
	char why[300];
	for (int h = 0; h < 2; h++)
		sa_chain_build(&g_arm.ch[h], h, RUN3_PARENT, RUN3_HASH, RUN3_N, why, sizeof why);
	g_arm.on = 1, g_arm.chains_ok = 1, g_arm.nodes = RUN3_N, g_arm.skel = (uintptr_t)&g_skel, g_arm.tid = my_tid();
	g_rigcfg.base = SOMVR_BASE_HEAD;
	static union { st_rend_t r; char b[0x260]; } rend, other; /* + vf_02's bone map at +0x1e8 */
	static char model[0x200];
	memset(&rend, 0, sizeof rend), memset(&other, 0, sizeof other);
	rend.r.vt = other.r.vt = (uintptr_t)g_bd_vt;
	uintptr_t rp = (uintptr_t)&rend, bmap[2] = { 1, 2 };
	memcpy(rend.b + BD_R_MAP, bmap, sizeof bmap); /* a bone map first: vf_02 copies `skin` (the copy path) */
	memcpy(model + SM_MAIN, &rp, sizeof rp);
	g_arm.model = (uintptr_t)model;
	atomic_store(&g_renderer, (uintptr_t)g_renderer_obj);
	volatile uint32_t *rf = (volatile uint32_t *)(g_renderer_obj + RENDERER_FRAME_OFF);
	/* the stand-in sites */
	static struct site_spec bds[BDS_N];
	vslot_site(&g_bd_vt[BD_REND_SLOT], (void *)st_bd_copy, &bds[BDS_COPY], "BD_REND_COPY", 75);
	entry_site((void *)st_bd_call, &bds[BDS_CALL], "BD_LISTEN_CALL", 76);
	entry_site((void *)st_bd_src, &bds[BDS_SRC], "BD_COPY_SRC", 77);
	entry_site((void *)st_bd_loop, &bds[BDS_LOOP], "BD_COPY_LOOP", 78);
	g_bd_sites = bds;
	int bad = 0;
	/* 1. a proof that does not match: off, the slot untouched */
	const char *keep = bds[BDS_SRC].ctx_hex;
	static char other_hex[256];
	snprintf(other_hex, sizeof other_hex, "%s", keep);
	other_hex[0] = other_hex[0] == 'f' ? '0' : 'f';
	bds[BDS_SRC].ctx_hex = other_hex;
	setenv("SOMVR_BODY", "hands", 1);
	bd_config(&bad);
	bd_start();
	EXPECT(!g_bd.on && g_bd_vt[BD_REND_SLOT] == (void *)st_bd_copy && log_has("BODY site BD_COPY_SRC MISMATCH") &&
		       log_has("WARN SOMVR_BODY=hands: a site did not match: the whole body as before"),
	       "body, a proof mismatch: on %d", g_bd.on);
	bds[BDS_SRC].ctx_hex = keep;
	/* 2. the slot in */
	setenv("SOMVR_SWORD_ANGLE", "20", 1);
	bd_config(&bad);
	bd_start();
	EXPECT(g_bd.on && g_bd_vt[BD_REND_SLOT] == (void *)bd_copy && g_bd.orig == (uintptr_t)st_bd_copy &&
		       g_bd.vt_rend == (uintptr_t)g_bd_vt && log_has("HOOK BD_REND_COPY ok slot") && log_has("CONFIG body=hands") &&
		       NEAR(g_bd.angle_deg, 20, 1e-6) && NEAR(g_cb.bcfg.pitch_rad, pitch0 + 20 * BD_DEG, 1e-6),
	       "body: the slot in: on %d slot %p angle %g pitch %g", g_bd.on, g_bd_vt[BD_REND_SLOT], g_bd.angle_deg,
	       g_cb.bcfg.pitch_rad);
	/* the default hide mode is the line (test_body checks its geometry); zero below (easier to check here) */
	EXPECT(g_bd.mode == SB_HIDE_LINE && log_has("hide=line edit=auto"), "the default hide mode: %d", g_bd.mode);
	g_bd.mode = SB_HIDE_ZERO;
	/* the right weapon node's calibration carries the angle: its blade axis (the default's +Y) on (0, sin 20, cos 20) */
	sv_vec3 bdir = sv_qrot(g_arm.cal[SA_R].q, (sv_vec3){ 0, 1, 0 });
	EXPECT(NEAR(bdir.x, 0, 1e-5) && NEAR(bdir.y, sinf(20 * BD_DEG), 1e-5) && NEAR(bdir.z, cosf(20 * BD_DEG), 1e-5),
	       "the angle: R_Weapon's +Y -> %.4f,%.4f,%.4f", bdir.x, bdir.y, bdir.z);
	/* build 15b's follow-up: SOMVR_SWORD_GRIP_CM=8 slides the right weapon node 8 cm along the (tilted) blade and the
	 * combat blade's tip 8 cm farther; back to 0 = as before */
	{
		sv_vec3 p0 = g_arm.cal[SA_R].p;
		float len0 = g_cb.bcfg.len_cm;
		g_bd.grip_cm = 8;
		bd_grip_blade(), bd_apply_cal();
		sv_vec3 dp = { g_arm.cal[SA_R].p.x - p0.x, g_arm.cal[SA_R].p.y - p0.y, g_arm.cal[SA_R].p.z - p0.z };
		EXPECT(NEAR(dp.x, 0, 1e-4) && NEAR(dp.y, 8 * sinf(20 * BD_DEG), 1e-4) && NEAR(dp.z, 8 * cosf(20 * BD_DEG), 1e-4) &&
			       NEAR(g_cb.bcfg.len_cm, len0 + 8, 1e-4) && log_has("grip 0.0 cm (SOMVR_SWORD_GRIP_CM"),
		       "the grip: R_Weapon moved %.3f,%.3f,%.3f, the blade %g (was %g)", dp.x, dp.y, dp.z, g_cb.bcfg.len_cm, len0);
		g_bd.grip_cm = 0;
		bd_grip_blade(), bd_apply_cal();
		EXPECT(NEAR(g_arm.cal[SA_R].p.z, p0.z, 1e-4) && NEAR(g_cb.bcfg.len_cm, len0, 1e-4),
		       "the grip back to 0: z %g blade %g", g_arm.cal[SA_R].p.z, g_cb.bcfg.len_cm);
	}
	/* 3. the copies: a flat frame (no H4 word) = the game's skin */
	static _Alignas(16) sa_node skin[RUN3_N], mpose[RUN3_N];
	for (int i = 0; i < RUN3_N; i++) {
		sa_xf x = sa_node_xf(&g_anim[i]);
		x.p.x += 1;
		sa_node_set(&skin[i], x);
	}
	memcpy(mpose, g_anim, sizeof mpose);
	typedef uint64_t (*cp_fn)(void *, void *, void *, void *, uint64_t, void *, uint64_t, uint64_t);
	cp_fn cp = (cp_fn)g_bd_vt[BD_REND_SLOT];
	*rf = 1000;
	bd_frame(0); /* third person or flat: no word */
	uint64_t r = cp(&rend, NULL, mpose, skin, RUN3_N, NULL, 1, 1);
	EXPECT(r == 0x5eed && g_bd_seen_skin == skin && g_bd.n.edits == 0 && !atomic_load(&g_bd.rend), "not live: the game's skin (%p)", g_bd_seen_skin);
	/* a first-person H4, the left hand held by the IK, the right lost */
	g_arm.weight[0] = 1, g_arm.weight[1] = 0;
	bd_frame(1);
	EXPECT(atomic_load(&g_bd.rend) == rp && atomic_load(&g_bd.live_frame) == 1000 && !bd_body_drawn(),
	       "the H4 word: rend %#lx live %llu drawn %d", (unsigned long)atomic_load(&g_bd.rend),
	       (unsigned long long)atomic_load(&g_bd.live_frame), bd_body_drawn());
	*rf = 1001;
	r = cp(&other, NULL, mpose, skin, RUN3_N, NULL, 1, 7);
	EXPECT(r == 0x5eed && g_bd_seen_skin == skin && g_bd_seen_a8 == 7, "another renderable: untouched");
	r = cp(&rend, (void *)1, mpose, skin, RUN3_N, (void *)2, 3, 4);
	int ok = r == 0x5eed && g_bd_seen_skin == g_bd.skin && g_bd_seen_a8 == 4, kept = 1, moved = 1;
	for (int i = 0; i < RUN3_N; i++) {
		int k = g_bd.keep.kind[i];
		if (k == SB_HAND_L) {
			kept &= !memcmp(&g_bd_seen[i], &skin[i], sizeof skin[i]);
			continue;
		}
		int sd = k == SB_HAND_R ? 1 : g_bd.keep.aside[i];
		const float *wp = mpose[g_bd.keep.wrist[sd]].pos;
		moved &= g_bd_seen[i].pos[0] == wp[0] && g_bd_seen[i].pos[1] == wp[1] && g_bd_seen[i].pos[2] == wp[2] &&
			 g_bd_seen[i].q[0] == 0 && g_bd_seen[i].q[3] == 0;
	}
	EXPECT(ok && kept && moved && g_bd.keep_ok && g_bd.n.edits == 1 && log_has("BODY keep skel=") &&
		       log_has("edit 168 nodes (zero, a copy, hands shown L=1 R=0)"),
	       "the player's: an edited copy (ok %d, the left hand kept %d, the rest at the wrists %d)", ok, kept, moved);
	EXPECT(skin[0].pos[0] == g_anim[0].pos[0] + 1, "the game's buffer itself untouched");
	/* the next H4: the body was edited -> drawn in the eyes; one without an edit -> hidden again */
	bd_frame(1);
	EXPECT(bd_body_drawn(), "edited this frame: the body node drawn");
	bd_frame(1);
	EXPECT(!bd_body_drawn(), "no edit since: hidden by SOMVR_HEAD_PARTS=main again");
	/* an empty bone map (the player's main renderable, run 154417): it draws the shared buffer `skin` itself, so the
	 * edit goes into that buffer and the game's pointer is passed on */
	memset(rend.b + BD_R_MAP, 0, sizeof bmap);
	bd_frame(1);
	r = cp(&rend, (void *)1, mpose, skin, RUN3_N, (void *)2, 3, 4);
	int inpl = r == 0x5eed && g_bd_seen_skin == skin && g_bd_seen_a8 == 4 && g_bd.n.inplace == 1, moved2 = 1;
	for (int i = 0; i < RUN3_N; i++) {
		int k = g_bd.keep.kind[i];
		if (k == SB_HAND_L)
			continue;
		int sd = k == SB_HAND_R ? 1 : g_bd.keep.aside[i];
		const float *wp = mpose[g_bd.keep.wrist[sd]].pos;
		moved2 &= skin[i].pos[0] == wp[0] && skin[i].pos[1] == wp[1] && skin[i].pos[2] == wp[2] && skin[i].q[0] == 0 &&
			  skin[i].q[3] == 0;
	}
	EXPECT(inpl && moved2 && log_has("BODY path t=") && log_has("(empty: it draws the shared buffer, no copy)") &&
		       log_has("(zero, in place, hands shown L=1 R=0)"),
	       "the no-copy path: edited in place (%d, the rest at the wrists %d, in place %llu)", inpl, moved2,
	       (unsigned long long)g_bd.n.inplace);
	/* stale: the word is 4 renderer frames old */
	*rf = 1005;
	cp(&rend, NULL, mpose, skin, RUN3_N, NULL, 1, 1);
	EXPECT(g_bd_seen_skin == skin, "a stale word: the game's skin");
	/* another skeleton count: not ready */
	*rf = 1001;
	cp(&rend, NULL, mpose, skin, RUN3_N - 1, NULL, 1, 1);
	EXPECT(g_bd_seen_skin == skin && g_bd.n.notready >= 1, "another node count: the game's skin");
	/* 4. the words: hide nan, angle, the test words off without SOMVR_HANDS_TEST */
	EXPECT(!strcmp(bd_word("hide nan"), "hands hide") && !strcmp(bd_word("angle 35"), "hands angle") &&
		       !strcmp(bd_word("edit copy"), "hands edit") &&
		       !strcmp(bd_word("edit x"), "hands edit (not auto|inplace|copy: ignored)") &&
		       !strcmp(bd_word("hide x"), "hands hide (not line|cuff|cuffik|forearm|zero|nan|far|show: ignored)") &&
		       !strcmp(bd_word("angle 120"), "hands angle (not -90..90 deg: ignored)") &&
		       !strcmp(bd_word("test R 1 2 3"), "hands (SOMVR_HANDS_TEST is off: ignored)"),
	       "the words");
	bd_frame(1);
	EXPECT(g_bd.mode == SB_HIDE_NAN && NEAR(g_bd.angle_deg, 35, 1e-6) && NEAR(g_cb.bcfg.pitch_rad, pitch0 + 35 * BD_DEG, 1e-6) &&
		       log_has("hide zero -> nan (trigger word)") && log_has("sword angle 20.0 -> 35.0 deg") &&
		       g_bd.edit == BD_EDIT_COPY && log_has("edit auto -> copy (trigger word)"),
	       "the words taken: mode %d angle %g", g_bd.mode, g_bd.angle_deg);
	/* round 3: hide cuff: the shown left hand's forearm twist node (91) carries the left wrist's (90) skin transform */
	EXPECT(!strcmp(bd_word("hide cuff"), "hands hide"), "hide cuff");
	bd_frame(1);
	for (int i = 0; i < RUN3_N; i++) { /* the game writes a fresh palette every frame (the in-place test edited this one) */
		sa_xf x = sa_node_xf(&g_anim[i]);
		x.p.x += 1;
		sa_node_set(&skin[i], x);
	}
	r = cp(&rend, NULL, mpose, skin, RUN3_N, NULL, 1, 1);
	EXPECT(g_bd.mode == SB_HIDE_CUFF && g_bd_seen_skin == g_bd.skin && !memcmp(g_bd_seen[91].q, skin[90].q, sizeof skin[90].q) &&
		       !memcmp(g_bd_seen[91].pos, skin[90].pos, sizeof skin[90].pos) && log_has("hide nan -> cuff (trigger word)"),
	       "cuff: the left stub with the left hand (mode %d, q91 %g,%g,%g,%g q90 %g,%g,%g,%g)", g_bd.mode,
	       g_bd_seen[91].q[0], g_bd_seen[91].q[1], g_bd_seen[91].q[2], g_bd_seen[91].q[3], skin[90].q[0], skin[90].q[1],
	       skin[90].q[2], skin[90].q[3]);
	bd_word("hide nan");
	bd_frame(1);
	/* 5. a test grip: 20 cm right, 30 down, 40 ahead of the head's yaw frame, the fist axis ahead */
	g_bd.test = 1;
	EXPECT(!strcmp(bd_word("test R 20 -30 40 0 0 0"), "hands test") && !strcmp(bd_word("squeeze L 0.8"), "hands squeeze") &&
		       !strcmp(bd_word("cal R 0 0 0 0 0 0"), "hands cal") &&
		       !strcmp(bd_word("test Q 1 2 3"), "hands (not test|swing|noise|wiggle|weighthand|twohold|squeeze|draw|cal|hide|angle: ignored)") &&
		       !strcmp(bd_word("weighthand follow"), "hands weighthand") &&
		       atomic_exchange(&g_hsm.hand_req, 0) == 2 &&
		       !strcmp(bd_word("weighthand loose"), "hands weighthand (not fixed|follow|lock|unlock: ignored)") &&
		       !strcmp(bd_word("weighthand lock"), "hands weighthand") && atomic_exchange(&g_hsm.lock_req, 0) == 2 &&
		       !strcmp(bd_word("weighthand unlock"), "hands weighthand") && atomic_exchange(&g_hsm.lock_req, 0) == 1 &&
		       !strcmp(bd_word("twohold 10.5 13"), "hands twohold") && atomic_exchange(&g_bd_req_hold, 0) == 1 &&
		       !strcmp(bd_word("twohold 20 10"), "hands twohold (not LO HI, 4 <= LO <= HI <= 30 cm: ignored)") &&
		       !strcmp(bd_word("noise 0.1 0.5"), "hands noise") && !strcmp(bd_word("noise 9 1"),
		       "hands noise (not DEG 0..5 MM 0..20: ignored)") && !strcmp(bd_word("noise 0 0"), "hands noise") &&
		       !strcmp(bd_word("wiggle R 50 4 2"), "hands wiggle (not L|R DEG 0..45 HZ 0.2..12 S 0..20 [x|y]: ignored)") &&
		       !strcmp(bd_word("wiggle X 10 4 2"), "hands wiggle (not L|R DEG 0..45 HZ 0.2..12 S 0..20 [x|y]: ignored)") &&
		       !strcmp(bd_word("wiggle R 10 4 2 z"), "hands wiggle (not L|R DEG 0..45 HZ 0.2..12 S 0..20 [x|y]: ignored)"),
	       "the test words");
	/* build 16: `hands wiggle`: a sine about the grip's own up axis from the test pose, its angular speed added */
	{
		int seq0 = atomic_load(&g_hsm.wig_seq);
		EXPECT(!strcmp(bd_word("wiggle R 10 5 1"), "hands wiggle") && atomic_load(&g_hsm.wig_seq) == seq0 + 1,
		       "the wiggle word");
		somvr_hand wh;
		memset(&wh, 0, sizeof wh);
		wh.pose.q[3] = 1, wh.time = 1000000000LL;
		hsm_test_wiggle(SOMVR_HAND_R, &wh); /* its start: sin 0 */
		somvr_hand w2 = wh;
		memset(w2.ang, 0, sizeof w2.ang);
		w2.time += 50000000LL; /* 50 ms = a quarter period at 5 Hz: +10 deg about +Y, no speed */
		hsm_test_wiggle(SOMVR_HAND_R, &w2);
		float ang = 2 * atan2f(w2.pose.q[1], w2.pose.q[3]) / 0.01745329f;
		EXPECT(log_has("BODY test wiggle R: +-10.0 deg at 5.0 Hz for 1.0 s") && NEAR(ang, 10, 0.01) &&
			       fabsf(w2.ang[1]) < 1e-3f && NEAR(wh.ang[1], 10 * 0.01745329 * 6.2831853 * 5, 1e-3),
		       "the wiggle: %.3f deg at a quarter, the start's speed %.3f rad/s", ang, wh.ang[1]);
		somvr_hand w3 = wh;
		w3.time += 1100000000LL;
		hsm_test_wiggle(SOMVR_HAND_R, &w3);
		EXPECT(!g_hsm.wig_on && w3.pose.q[3] == 1 && log_has("BODY test wiggle R end: "), "the wiggle ends");
		/* build 20: `hands wiggle R DEG HZ S x`: about the grip's own +X (the edge-on wiggle) */
		EXPECT(!strcmp(bd_word("wiggle R 10 5 1 x"), "hands wiggle"), "the edge-on wiggle word");
		somvr_hand wx = wh;
		memset(wx.ang, 0, sizeof wx.ang);
		hsm_test_wiggle(SOMVR_HAND_R, &wx);
		somvr_hand wx2 = wh;
		memset(wx2.ang, 0, sizeof wx2.ang), wx2.time += 50000000LL;
		hsm_test_wiggle(SOMVR_HAND_R, &wx2);
		float angx = 2 * atan2f(wx2.pose.q[0], wx2.pose.q[3]) / 0.01745329f;
		EXPECT(log_has("for 1.0 s (about the grip's +X (edge-on) axis)") && NEAR(angx, 10, 0.01) && fabsf(wx2.pose.q[1]) < 1e-6f &&
			       NEAR(wx.ang[0], 10 * 0.01745329 * 6.2831853 * 5, 1e-3),
		       "the edge-on wiggle: %.3f deg about +X at a quarter, the start's speed %.3f rad/s", angx, wx.ang[0]);
		wx2.time += 1100000000LL;
		hsm_test_wiggle(SOMVR_HAND_R, &wx2);
	}
	float yaw = 0.6f; /* the head turned 34 deg left (OpenXR: about +Y) */
	for (int e = 0; e < 2; e++) {
		g_fr.views.pose[e].q[0] = 0, g_fr.views.pose[e].q[1] = sinf(0.5f * yaw), g_fr.views.pose[e].q[2] = 0;
		g_fr.views.pose[e].q[3] = cosf(0.5f * yaw);
		g_fr.views.pose[e].p[0] = e ? 0.032f : -0.032f, g_fr.views.pose[e].p[1] = 1.6f, g_fr.views.pose[e].p[2] = 0;
	}
	bd_frame(1);
	somvr_hand hh[2];
	memset(hh, 0, sizeof hh);
	bd_test_hands(hh, 0, 0);
	sv_quat qy = { 0, sinf(0.5f * yaw), 0, cosf(0.5f * yaw) };
	sv_vec3 want = sv_qrot(qy, (sv_vec3){ 0.2f, -0.3f, -0.4f });
	sv_vec3 fwd = sv_qrot(sv_quat_of(hh[1].pose.q), (sv_vec3){ 0, 0, -1 }), hf = sv_qrot(qy, (sv_vec3){ 0, 0, -1 });
	EXPECT(hh[1].valid && !hh[0].valid && NEAR(hh[1].pose.p[0], want.x, 1e-5) && NEAR(hh[1].pose.p[1], 1.6f + want.y, 1e-5) &&
		       NEAR(hh[1].pose.p[2], want.z, 1e-5) && NEAR(fwd.x, hf.x, 1e-5) && NEAR(fwd.z, hf.z, 1e-5) &&
		       NEAR(bd_squeeze(SA_L, 0.1f), 0.8f, 1e-6) && NEAR(bd_squeeze(SA_R, 0.3f), 0.3f, 1e-6) &&
		       log_has("BODY test cal R = 0,0,0,0,0,0"),
	       "the test grip at %.3f,%.3f,%.3f (want %.3f,%.3f,%.3f), the fist axis %.3f,%.3f,%.3f", hh[1].pose.p[0],
	       hh[1].pose.p[1], hh[1].pose.p[2], want.x, 1.6f + want.y, want.z, fwd.x, fwd.y, fwd.z);
	/* the angle back to 0 by a word (its float bits are 0) */
	EXPECT(!strcmp(bd_word("angle 0"), "hands angle"), "angle 0");
	bd_frame(1);
	EXPECT(g_bd.angle_deg == 0 && NEAR(g_cb.bcfg.pitch_rad, pitch0, 1e-6) && log_has("sword angle 35.0 -> 0.0 deg"),
	       "angle 0 taken: %g", g_bd.angle_deg);
	/* the end line; the slot back to the stand-in (the later frames of other tests never reach it) */
	bd_end_log();
	EXPECT(log_has("BODY END body=hands hide=nan"), "the end line");
	g_bd_vt[BD_REND_SLOT] = (void *)st_bd_copy;
	g_bd.on = 0, g_bd.test = 0, g_bd.twohand = 0, g_bd.mode = SB_HIDE_LINE, g_bd.angle_deg = 0, g_bd.edit = BD_EDIT_AUTO;
	atomic_store(&g_bd_req_edit, BD_EDIT_N), atomic_store(&g_bd_req_angle_set, 0);
	g_bd_sites = g_bd_sites_game;
	unsetenv("SOMVR_BODY"), unsetenv("SOMVR_SWORD_ANGLE");
	atomic_store(&g_bd.rend, 0), atomic_store(&g_bd.live_frame, 0);
	g_arm.on = arm_on0, g_arm.chains_ok = chains0, g_arm.nodes = nodes0, g_arm.skel = skel0, g_arm.model = model0;
	g_arm.tid = tid0, g_arm.weight[0] = w0[0], g_arm.weight[1] = w0[1], g_rigcfg.base = base0;
	memcpy(g_arm.ch, ch0, sizeof ch0), memcpy(g_arm.cal_v, cal0, sizeof cal0);
	g_arm.cal[0] = calx0[0], g_arm.cal[1] = calx0[1], g_cb.bcfg.pitch_rad = pitch0;
	atomic_store(&g_renderer, rend0);
}

/* ---- SOMVR_HANDS_TEST's scripted swing `hands swing` (somvr_body_game.c, sc_swing_pose): the words, the pose frame by
 * frame at 20 Hz on the views' time (the locate's time set: the blade's clock), the velocity, the end, a cancel ---- */
static void bd_swing_tests(void)
{
	int test0 = g_bd.test;
	int64_t time0 = g_fr.views.time;
	somvr_posef pose0[2] = { g_fr.views.pose[0], g_fr.views.pose[1] };
	g_bd.test = 1;
	for (int e = 0; e < 2; e++) { /* the head at 1.6 m, facing -Z (OpenXR) */
		memset(&g_fr.views.pose[e], 0, sizeof g_fr.views.pose[e]);
		g_fr.views.pose[e].q[3] = 1, g_fr.views.pose[e].p[0] = e ? 0.032f : -0.032f, g_fr.views.pose[e].p[1] = 1.6f;
	}
	/* sc_euler_quat = sa_xf_euler (the swing's end pose = the test pose: no step at the end) */
	float ang[4][3] = { { 0, -90, 0 }, { 0, 90, 0 }, { 30, 40, 50 }, { -70, 15, 120 } }, mx = 0;
	for (int i = 0; i < 4; i++) {
		float q[4], r[4];
		sc_euler_quat(ang[i], q);
		sv_quat_to(sa_xf_euler((float[6]){ 0, 0, 0, ang[i][0], ang[i][1], ang[i][2] }).q, r);
		float d = fabsf(q[0] * r[0] + q[1] * r[1] + q[2] * r[2] + q[3] * r[3]);
		mx = fmaxf(mx, 1 - d);
	}
	EXPECT(mx < 1e-6f, "sc_euler_quat = sa_xf_euler (1 - |dot| %.2g)", mx);
	/* the words */
	pthread_mutex_lock(&g_bd_mu);
	g_bd_req.on[0] = 0;
	pthread_mutex_unlock(&g_bd_mu);
	EXPECT(!strcmp(bd_word("swing L -20 -20 40 0 0 0 300"), "hands swing (that hand has no test pose: `hands test` first; ignored)") &&
		       !strcmp(bd_word("swing R 1 2 3"), "hands swing (not L|R x y z rx ry rz MS [OPEN [CATCH]]: ignored)") &&
		       !strcmp(bd_word("swing X 1 2 3 0 0 0 100"), "hands swing (not L|R x y z rx ry rz MS [OPEN [CATCH]]: ignored)") &&
		       !strcmp(bd_word("swing R 0 0 0 0 0 0 100 1.5"), "hands swing (OPEN not 0 or between 0 and 1: ignored)") &&
		       !strcmp(bd_word("swing R 0 0 0 0 0 0 100 0.5 0"), "hands swing (CATCH not in (0, 1]: ignored)") &&
		       !strcmp(bd_word("swing R 0 0 0 0 0 0 5"), "hands swing (MS not 10..5000: ignored)") &&
		       !strcmp(bd_word("swing R 0 0 999 0 0 0 100"), "hands swing (a number out of -400..400: ignored)"),
	       "the swing's refusals");
	EXPECT(!strcmp(bd_word("test R 35 -20 30 0 -90 0"), "hands test"), "the start pose");
	g_fr.views.time = 5000000000LL;
	bd_frame(1);
	EXPECT(!strcmp(bd_word("swing R -25 -25 45 0 90 0 300"), "hands swing"), "the swing word");
	int64_t T0 = 5100000000LL;
	g_fr.views.time = T0;
	bd_frame(1);
	EXPECT(g_bd_sw.on[1] && g_bd_sw.t0[1] == T0 && g_bd_sw.dur[1] == 300000000 &&
		       log_has("BODY test swing R: from 35.0,-20.0,30.0 to -25,-25,45 cm 0,90,0 deg in 300 ms"),
	       "the swing taken at the frame's views time");
	somvr_hand hh[2], ha[2];
	float lastx = 35;
	int ok_t = 1, ok_mid = 0, ok_left = 1;
	for (int i = 1; i <= 7; i++) {
		int64_t t = T0 + i * 50000000LL;
		memset(hh, 0, sizeof hh), memset(ha, 0, sizeof ha);
		bd_test_hands(hh, t, 1);
		bd_test_hands(ha, t, 0); /* the arms' locate at the same time: the same pose, no line */
		float dp = 0;
		for (int j = 0; j < 3; j++)
			dp = fmaxf(dp, fabsf(hh[1].pose.p[j] - ha[1].pose.p[j]));
		for (int j = 0; j < 4; j++)
			dp = fmaxf(dp, fabsf(hh[1].pose.q[j] - ha[1].pose.q[j]));
		ok_t &= hh[1].time == t && hh[1].valid && hh[1].vel_valid == 3 && dp < 1e-6f;
		float x = 100 * hh[1].pose.p[0];
		ok_left &= i >= 6 || (x < lastx && hh[1].lin[0] < 0); /* moving left (OpenXR +x = right) */
		lastx = x;
		if (i == 3) { /* u = 0.5: the fist axis straight ahead (-Z), the grip on the arc about the shoulder */
			sv_vec3 f = sv_qrot(sv_quat_of(hh[1].pose.q), (sv_vec3){ 0, 0, -1 });
			float sp = sqrtf(hh[1].lin[0] * hh[1].lin[0] + hh[1].lin[1] * hh[1].lin[1] + hh[1].lin[2] * hh[1].lin[2]);
			float w = sqrtf(hh[1].ang[0] * hh[1].ang[0] + hh[1].ang[1] * hh[1].ang[1] + hh[1].ang[2] * hh[1].ang[2]);
			/* 180 deg in 0.3 s: the peak of the minimum jerk 1.875 x pi / 0.3 = 19.6 rad/s, about +Y (turning left) */
			ok_mid = f.z < -0.999f && NEAR(w, 1.875f * 3.14159f / 0.3f, 0.2f) && hh[1].ang[1] > 0 && sp > 1.0f && sp < 6.0f;
			EXPECT(ok_mid, "u 0.5: the axis %.3f,%.3f,%.3f, %.2f m/s, %.2f rad/s (%.2f about +Y)", f.x, f.y, f.z, sp, w,
			       hh[1].ang[1]);
		}
	}
	EXPECT(ok_t, "every frame at the locate's time, both calls the same pose");
	EXPECT(ok_left, "moving left");
	sv_vec3 f = sv_qrot(sv_quat_of(hh[1].pose.q), (sv_vec3){ 0, 0, -1 });
	EXPECT(!g_bd_sw.on[1] && log_has("BODY test swing R end: 6 frames") && log_has("BODY test swing R frame 3 u=0.50") &&
		       NEAR(hh[1].pose.p[0], -0.25f, 1e-4) && NEAR(hh[1].pose.p[2], -0.45f, 1e-4) && f.x < -0.999f &&
		       hh[1].lin[0] == 0,
	       "the end: 6 frames, at the end pose (the fist axis left: %.3f), still", f.x);
	/* a swing back, cancelled by a test word after one frame */
	EXPECT(!strcmp(bd_word("swing R 35 -20 30 0 -90 0 300"), "hands swing"), "the swing back");
	g_fr.views.time = T0 + 1000000000LL;
	bd_frame(1);
	bd_test_hands(hh, g_fr.views.time + 50000000LL, 1);
	EXPECT(!strcmp(bd_word("test R 0 -20 40 0 0 0"), "hands test"), "a test word");
	bd_frame(1);
	memset(hh, 0, sizeof hh);
	bd_test_hands(hh, g_fr.views.time + 100000000LL, 1);
	EXPECT(!g_bd_sw.on[1] && log_has("BODY test swing R cancelled after 1 frames (a test word)") &&
		       NEAR(hh[1].pose.p[0], 0, 1e-5) && NEAR(hh[1].pose.p[2], -0.40f, 1e-5),
	       "cancelled: the test pose");
	/* OPEN (SOMVR_CLIMB_FLING's tests): the squeezed hand's test squeeze opens in the frame the swing passes u 0.5 */
	EXPECT(!strcmp(bd_word("test L -20 0 40 0 0 0"), "hands test") && !strcmp(bd_word("squeeze L 0"), "hands squeeze") &&
		       !strcmp(bd_word("squeeze R 1"), "hands squeeze") &&
		       !strcmp(bd_word("swing R 0 -60 40 0 0 0 200 0.5 0.75"), "hands swing"),
	       "a squeeze and a swing with OPEN and CATCH");
	int64_t T2 = T0 + 2000000000LL;
	g_fr.views.time = T2;
	bd_frame(1);
	int open_at = -1;
	for (int i = 1; i <= 5; i++) {
		bd_test_hands(hh, T2 + i * 50000000LL, 1);
		if (open_at < 0 && bd_squeeze(1, 0.7f) == 0) open_at = i;
		if (open_at < 0 && !(bd_squeeze(1, 0.7f) == 1)) open_at = -2;
	}
	bd_frame(1);
	EXPECT(open_at == 2 && bd_squeeze(1, 0.7f) == 0 && log_has("u=0.50: the squeeze opened") && g_bd_req.sq[1] == 0,
	       "OPEN: the squeeze 1 until u 0.5 (frame %d), then 0", open_at);
	EXPECT(bd_squeeze(0, 0.2f) == 1 && log_has("u=0.75: the L squeeze closed") && g_bd_req.sq[0] == 1,
	       "CATCH: the left squeeze closed at u 0.75");
	EXPECT(!strcmp(bd_word("squeeze R -1"), "hands squeeze") && !strcmp(bd_word("squeeze L -1"), "hands squeeze"),
	       "the squeezes back");
	/* the state back */
	pthread_mutex_lock(&g_bd_mu);
	g_bd_req.on[0] = g_bd_req.on[1] = 0, g_bd_req.seq++;
	pthread_mutex_unlock(&g_bd_mu);
	bd_frame(1);
	g_bd.test = test0, g_fr.views.time = time0, g_fr.views.pose[0] = pose0[0], g_fr.views.pose[1] = pose0[1];
}

/* ---- SOMVR_FINGERS (somvr_fingers_game.c): the switch, the hands of the arms' skeleton, the test words, the curls into
 * the solved arms' pose (the right hand a fist from its trigger, grip and thumb words, the left open), a held sword and the
 * hilt in two hands = fists whatever the buttons, the bow hand the game's (eased out), an unsolved arm untouched, the dump
 * (the game's fingers, the bind pose from a palette), off and on live, the VR menu's sword angle, the end line ------- */
static float bf_qang(sv_quat a, sv_quat b)
{
	sv_quat r = sv_qmul(sv_qconj(sv_qnormalize(a)), sv_qnormalize(b));
	return 2 * atan2f(sqrtf(r.x * r.x + r.y * r.y + r.z * r.z), fabsf(r.w));
}
/* the largest angle of hand h's joints from the pose p (their locals) */
static float bf_off(int h, const fg_pose *p)
{
	float m = 0;
	for (int a = 0; a < FG_NF; a++)
		for (int k = 0; k < FG_NJ; k++) {
			sa_xf l;
			if (!fg_local(g_arm.pose, &g_bf.f[h], a, k, &l))
				return 99;
			m = fmaxf(m, bf_qang(l.q, p->q[a][k]));
		}
	return m;
}
static void bf_frames(int n, int dl, int dr)
{
	int done[2] = { dl, dr };
	for (int i = 0; i < n; i++) {
		memcpy(g_arm.pose, g_anim, sizeof g_anim); /* the game's phase 2 writes its animation every frame */
		bf_eval(done, 1.0f / 90);
	}
}
static void bf_tests(void)
{
	int arm_on0 = g_arm.on, chains0 = g_arm.chains_ok, test0 = g_bd.test, sl_on0 = g_sl.on, sl_hg0 = g_sl.have_g;
	uint32_t nodes0 = g_arm.nodes;
	uintptr_t skel0 = g_arm.skel, item0 = g_sl.g.item[0];
	int sheathed0 = g_sl.g.sheathed, bow_on0 = g_bow.on, bow_tid0 = g_bow.tid, bow_ph0 = g_bow.s.phase;
	float w0[2] = { g_arm.weight[0], g_arm.weight[1] }, two_w0 = g_bd.two.w, angle0 = g_bd.angle_deg, cal0[2][6];
	float pitch0 = g_cb.bcfg.pitch_rad;
	sa_chain ch0[2];
	sa_xf calx0[2] = { g_arm.cal[0], g_arm.cal[1] };
	memcpy(ch0, g_arm.ch, sizeof ch0), memcpy(cal0, g_arm.cal_v, sizeof cal0);
	arm_world();
	char why[300];
	for (int h = 0; h < 2; h++)
		sa_chain_build(&g_arm.ch[h], h, RUN3_PARENT, RUN3_HASH, RUN3_N, why, sizeof why);
	g_arm.on = 1, g_arm.chains_ok = 1, g_arm.nodes = RUN3_N, g_arm.skel = (uintptr_t)&g_skel;
	g_arm.weight[0] = g_arm.weight[1] = 1;
	g_sl.on = 0, g_bd.two.w = 0, g_bow.on = 0;
	g_bd.t_sq[0] = g_bd.t_sq[1] = -1, g_bd_req.sq[0] = g_bd_req.sq[1] = -1; /* bd_tests' test squeeze L 0.8 */
	int bad = 0;
	/* 1. off by default; SOMVR_FINGERS=1 on (with the arms) */
	unsetenv("SOMVR_FINGERS");
	bf_config(&bad);
	bf_start();
	EXPECT(!g_bf.on && log_has("CONFIG fingers=0"), "fingers: off by default");
	setenv("SOMVR_FINGERS", "1", 1), setenv("SOMVR_FINGERS_MS", "30", 1);
	bf_config(&bad);
	bf_start();
	EXPECT(g_bf.on && !bad && NEAR(g_bf.cfg.tau_s, 0.03f, 1e-6) && log_has("CONFIG fingers=1") &&
		       log_has("ease tau 30 ms, blend 100 ms"),
	       "fingers on: %d tau %g", g_bf.on, g_bf.cfg.tau_s);
	bf_frames(2, 1, 1); /* open hands (no controller): the ease's state begins */
	/* 2. the words: the test ones need SOMVR_HANDS_TEST */
	g_bd.test = 0;
	EXPECT(!strcmp(bd_word("trigger R 1"), "hands (SOMVR_HANDS_TEST is off: ignored)") &&
		       !strcmp(bd_word("fingers"), "hands fingers (a dump)"),
	       "the words without the test switch");
	g_bd.test = 1;
	EXPECT(!strcmp(bd_word("trigger R 1"), "hands trigger") && !strcmp(bd_word("thumb R 1"), "hands thumb") &&
		       !strcmp(bd_word("trigger X 1"), "hands trigger|thumb (not L|R V: ignored)") &&
		       !strcmp(bd_word("thumb L"), "hands trigger|thumb (not L|R V: ignored)") &&
		       !strcmp(bd_word("squeeze R 1"), "hands squeeze"),
	       "the test words");
	bd_frame(1);
	EXPECT(log_has("FINGERS test trigger R 1.00") && log_has("FINGERS test thumb R 1.00") &&
		       log_has("FINGERS t=") && log_has("dump asked (trigger word)") && NEAR(bd_squeeze(SA_R, 0), 1, 1e-6),
	       "the words taken");
	/* 3. the right hand closes (trigger, grip, thumb), the left stays open; the dump of the game's fingers */
	bf_frames(1, 1, 1);
	EXPECT(log_has("FINGERS hands skel=") && log_has("R hand 112: thumb 114/120/121 index 115/122/123") &&
		       log_has("FINGERS dump anim R index1 node 115: q ") && log_has("FINGERS dump anim L pinky3 node 107"),
	       "the hands and the dump");
	float r1 = bf_off(SA_R, &fg_pose_fist);
	bf_frames(11, 1, 1);
	float r12 = bf_off(SA_R, &fg_pose_fist), l12 = bf_off(SA_L, &fg_pose_open);
	EXPECT(r1 > 0.5f && r12 < 0.05f && l12 < 2e-3f && g_bf.n.applied[1] == 14 && g_bf.n.applied[0] == 14,
	       "the right hand eases into a fist (%.3f -> %.3f rad off it), the left open (%.4f)", r1, r12, l12);
	EXPECT(!memcmp(&g_arm.pose[112], &g_anim[112], sizeof g_anim[0]) && !memcmp(&g_arm.pose[119], &g_anim[119], sizeof g_anim[0]) &&
		       !memcmp(&g_arm.pose[86], &g_anim[86], sizeof g_anim[0]),
	       "the hand, its weapon node and the arm untouched");
	/* 4. the buttons released: open again; then the sword in the right hand: a fist whatever the buttons */
	bd_word("trigger R -1"), bd_word("thumb R -1"), bd_word("squeeze R -1");
	bd_frame(1);
	bf_frames(12, 1, 1);
	EXPECT(bf_off(SA_R, &fg_pose_open) < 0.05f, "released: open (%.3f)", bf_off(SA_R, &fg_pose_open));
	g_sl.on = 1, g_sl.have_g = 1, g_sl.g.item[0] = (uintptr_t)1, g_sl.g.sheathed = 0;
	bf_frames(12, 1, 1);
	EXPECT(bf_off(SA_R, &fg_pose_fist) < 0.05f && bf_off(SA_L, &fg_pose_open) < 2e-3f &&
		       log_has("FINGERS hold R t=") && log_has("the controller -> a fist (a held weapon)"),
	       "the sword: a fist (%.3f)", bf_off(SA_R, &fg_pose_fist));
	/* 5. the hilt in both hands: the left a fist too */
	g_bd.two.w = 0.4f;
	bf_frames(12, 1, 1);
	EXPECT(bf_off(SA_L, &fg_pose_fist) < 0.05f && log_has("FINGERS hold L t="), "two hands: the left fist (%.3f)",
	       bf_off(SA_L, &fg_pose_fist));
	g_bd.two.w = 0;
	/* 6. the bow out in the left hand: the game's fingers, eased out over 0.1 s */
	g_bow.on = 1, g_bow.tid = my_tid(), g_bow.s.phase = SOMVR_BOW_PH_IDLE + 1;
	bf_frames(5, 1, 1);
	float wb = g_bf.w[g_bow.cfg.bow_hand];
	bf_frames(5, 1, 1);
	int bh = g_bow.cfg.bow_hand;
	sa_xf l;
	fg_local(g_arm.pose, &g_bf.f[bh], FG_MIDDLE, 1, &l);
	sa_xf l0;
	fg_local(g_anim, &g_bf.f[bh], FG_MIDDLE, 1, &l0);
	EXPECT(wb > 0 && wb < 1 && g_bf.w[bh] == 0 && bf_qang(l.q, l0.q) < 1e-3f && g_bf.n.game[bh] >= 10 &&
		       log_has("-> the game's (the bow)"),
	       "the bow hand: weight %.2f then %.2f, the game's fingers (%.4f)", wb, g_bf.w[bh], bf_qang(l.q, l0.q));
	g_bow.on = bow_on0, g_bow.tid = bow_tid0, g_bow.s.phase = bow_ph0;
	/* 7. an unsolved arm: its fingers untouched */
	uint64_t sk = g_bf.n.skipped[0];
	bf_frames(3, 0, 1);
	EXPECT(g_bf.n.skipped[0] == sk + 3 && !memcmp(&g_arm.pose[93], &g_anim[93], sizeof g_anim[0]), "an unsolved arm: skipped");
	/* 8. the bind pose's dump from a palette (skin = model x bind^-1: identity skin = the model pose is the bind) */
	static _Alignas(16) sa_node skin[RUN3_N];
	for (int i = 0; i < RUN3_N; i++)
		sa_node_set(&skin[i], (sa_xf){ { 0, 0, 0 }, { 0, 0, 0, 1 } });
	bd_word("fingers");
	bd_frame(1);
	bf_bind(skin, g_anim, RUN3_N);
	EXPECT(log_has("FINGERS dump bind R index2 node 122: model p ") && log_has("FINGERS dump bind L hand1 node 90") &&
		       log_has("t 4.680,"),
	       "the bind dump");
	/* 9. off and on live */
	bd_word("fingers off");
	bd_frame(1);
	bf_frames(3, 1, 1);
	EXPECT(!g_bf.on && log_has("FINGERS t=") && log_has(" off (trigger word)") &&
		       !memcmp(&g_arm.pose[115], &g_anim[115], sizeof g_anim[0]),
	       "off: the game's fingers");
	bd_word("fingers on");
	bd_frame(1);
	EXPECT(g_bf.on && log_has(" on (trigger word)"), "on again");
	/* 10. the VR menu's sword angle (bd_vm_*): the row's bit with the arms, the angle live, the settings from it */
	somvr_settings vs;
	somvr_set_default(&vs);
	g_bd.angle_deg = 0;
	bd_vm_settings(&vs);
	vs.sword_deg = 15;
	const char *note = bd_vm_set(SOMVR_SK_SWORD_ANGLE, &vs);
	EXPECT(bd_vm_avail() == SOMVR_VMA_SWORD && NEAR(g_bd.angle_deg, 15, 1e-6) && strstr(note, "the combat blade") &&
		       log_has("sword angle 0.0 -> 15.0 deg (the VR menu)") && !strcmp(bd_vm_set(SOMVR_SK_HUD, &vs), ""),
	       "the menu's sword angle: %g (%s)", g_bd.angle_deg, note);
	somvr_settings vs2;
	bd_vm_settings(&vs2);
	EXPECT(vs2.sword_deg == 15, "the settings carry it: %g", vs2.sword_deg);
	/* 10b. build 16: the sword's weight (SOMVR_SWORD_WEIGHT, somvr_smooth_game.c) and the VR menu's Sword weight row:
	 * the switch's level, the row's bit with the arms, the level live from the row (its constants, the spring reset),
	 * the settings from it, the hits' haptic, off */
	int wbad = 0, wt0 = g_hsm.weight;
	setenv("SOMVR_SWORD_WEIGHT", "heavy", 1);
	hsm_config(&wbad);
	vs.sword_weight = 0;
	swg_vm_settings(&vs);
	EXPECT(!wbad && g_hsm.weight == SWG_HEAVY && NEAR(g_hsm.wcfg.a_up, 24, 1e-6) && vs.sword_weight == 2 &&
		       swg_vm_avail() == SOMVR_VMA_WEIGHT && log_has("sword weight=heavy (the blade's swing speed grows at 24, "),
	       "the weight: heavy from the switch (%d, %u)", g_hsm.weight, vs.sword_weight);
	g_hsm.ws.have = 1;
	vs.sword_weight = 1;
	note = swg_vm_set(&vs);
	EXPECT(g_hsm.weight == SWG_LIGHT && NEAR(g_hsm.wcfg.a_up, 60, 1e-6) && !g_hsm.ws.have &&
		       strstr(note, "the hits' blade") && log_has("weight heavy -> light (the VR menu)"),
	       "the row: light live (%d, %s)", g_hsm.weight, note);
	vs.sword_weight = 2;
	vm_apply_key(SOMVR_SK_SWORD_WEIGHT, &vs, "the selftest");
	float wamp = 0.8f, wdur = 0.2f;
	swg_hit_haptic(&wamp, &wdur);
	EXPECT(g_hsm.weight == SWG_HEAVY && log_has("VRMENU apply SWORD_WEIGHT=heavy from the selftest (the drawn sword") &&
		       NEAR(wamp, 1.0, 1e-6) && NEAR(wdur, 0.4, 1e-6),
	       "vm_apply_key: heavy; the hit haptic %g x %g s", wamp, wdur);
	vs.sword_weight = 0;
	swg_vm_set(&vs);
	wamp = 0.8f, wdur = 0.2f;
	swg_hit_haptic(&wamp, &wdur);
	EXPECT(g_hsm.weight == SWG_OFF && log_has("weight heavy -> off (the VR menu)") && NEAR(wamp, 0.8, 1e-6) &&
		       NEAR(wdur, 0.2, 1e-6),
	       "the row: off (no haptic change)");
	/* SOMVR_SWORD_WEIGHT_HAND=follow (swg_vh_follow): the layer's hands turned with the blade about their grips, the
	 * positions kept; the left only with both hands on the hilt */
	setenv("SOMVR_SWORD_WEIGHT_HAND", "follow", 1);
	hsm_config(&wbad);
	EXPECT(g_hsm.hand_follow && log_has("the drawn hands follow the blade (SOMVR_SWORD_WEIGHT_HAND)"), "follow: the switch");
	unsetenv("SOMVR_SWORD_WEIGHT_HAND");
	hsm_config(&wbad);
	EXPECT(!g_hsm.hand_follow && log_has("the drawn hands fixed on the controllers"), "follow: fixed by default");
	/* build 17: the VR menu's Sword hand row (SOMVR_SWORD_WEIGHT_HAND live): its bit with the layer's hands, the
	 * settings from the switch, the row's request taken at the arms' next frame (swg_arm, before the weight's own
	 * work), dimmed with the weight off (the note), nothing without the layer's hands */
	{
		int vw0 = g_vh.want, wl0 = g_hsm.weight;
		const int z2[2] = { 0, 0 };
		const sa_xf zg[2] = { { { 0, 0, 0 }, { 0, 0, 0, 1 } }, { { 0, 0, 0 }, { 0, 0, 0, 1 } } };
		const sa_xf zx = { { 0, 0, 0 }, { 0, 0, 0, 1 } };
		g_vh.want = 1;
		vs.sword_hand = 1;
		swg_vm_settings(&vs);
		uint32_t ha = swg_vm_avail();
		EXPECT(vs.sword_hand == 0 && ha == (SOMVR_VMA_WEIGHT | SOMVR_VMA_SWHAND), "the Sword hand: fixed, avail %#x", ha);
		swg_level(SWG_HEAVY);
		vs.sword_hand = 1;
		vm_apply_key(SOMVR_SK_SWORD_WEIGHT_HAND, &vs, "the selftest");
		int hq = atomic_load(&g_hsm.hand_req);
		swg_level(SWG_OFF); /* swg_arm returns after the request with no weight (no arms frame needed) */
		swg_arm(z2, zg, z2, z2, zx, (sv_vec3){ 0, 0, 0 }, 0.011f);
		EXPECT(hq == 4 && g_hsm.hand_follow && !atomic_load(&g_hsm.hand_req) &&
			       log_has("VRMENU apply SWORD_WEIGHT_HAND=follow from the selftest (the drawn hands turn with the "
				       "weighted blade)") &&
			       log_has("the drawn hands follow the blade (the VR menu's Sword hand)"),
		       "the Sword hand row: follow live (req %d, follow %d)", hq, g_hsm.hand_follow);
		vs.sword_hand = 0;
		const char *hn = swg_vm_hand_set(&vs);
		swg_arm(z2, zg, z2, z2, zx, (sv_vec3){ 0, 0, 0 }, 0.011f);
		EXPECT(!g_hsm.hand_follow && strstr(hn, "used with the Sword weight on") &&
			       log_has("the drawn hands fixed (the VR menu's Sword hand)"),
		       "the Sword hand row: fixed again ('%s')", hn);
		g_vh.want = 0;
		EXPECT(swg_vm_avail() == SOMVR_VMA_WEIGHT && strstr(swg_vm_hand_set(&vs), "no drawn hands") &&
			       !atomic_load(&g_hsm.hand_req),
		       "the Sword hand row: none without the layer's hands");
		g_vh.want = vw0;
		swg_level(wl0);
	}
	{
		sa_xf fg[2] = { { { 30, -20, 40 }, sv_qnormalize((sv_quat){ 0.1f, 0.2f, 0.05f, 0.97f }) },
				{ { 20, -22, 30 }, sv_qnormalize((sv_quat){ -0.1f, 0.3f, 0.0f, 0.95f }) } };
		somvr_vrhands_frame fe, f0;
		memset(&fe, 0, sizeof fe);
		fe.flags = SOMVR_VHF_R | SOMVR_VHF_L;
		vh_to_xr(&fg[0], &fe.grip[0]), vh_to_xr(&fg[1], &fe.grip[1]);
		f0 = fe;
		float frot[3] = { 0, 0.2f, 0.05f }, fq[4], oq[4];
		swg_vh_follow(&fe, fg, frot, 0);
		sv_quat_to(fg[1].q, fq);
		swg_follow(frot, fq, oq);
		sa_xf want = { fg[1].p, sv_quat_of(oq) };
		somvr_posef wx;
		vh_to_xr(&want, &wx);
		EXPECT(!memcmp(fe.grip[1].p, f0.grip[1].p, 12) && NEAR(fe.grip[1].q[0], wx.q[0], 1e-6) &&
			       NEAR(fe.grip[1].q[1], wx.q[1], 1e-6) && NEAR(fe.grip[1].q[2], wx.q[2], 1e-6) &&
			       NEAR(fe.grip[1].q[3], wx.q[3], 1e-6) && !memcmp(&fe.grip[0], &f0.grip[0], sizeof fe.grip[0]),
		       "follow: the right hand turned about its grip (the position kept), the left untouched with one hand");
		swg_vh_follow(&fe, fg, frot, 1);
		EXPECT(!memcmp(fe.grip[0].p, f0.grip[0].p, 12) && memcmp(fe.grip[0].q, f0.grip[0].q, 16),
		       "follow: with both hands the left turns too, its position kept");
		/* build 18: SOMVR_SWORD_HAND_LOCK (somvr_hitfeel_game.c swl_arm): the weight's turn and the bite's on both drawn
		 * hands about the right grip point - the right turned in place, the left with both hands carried round it */
		int on0 = g_vh.on, have0 = g_vh.have, lock0 = g_hsm.lock, wn0 = g_hsm.w_now, gv2[2] = { 1, 1 };
		float wr0[3], bn0[3], tw0 = g_bd.two.w;
		somvr_vrhands_frame e0 = g_vh.e;
		memcpy(wr0, g_hsm.w_rot_now, 12), memcpy(bn0, g_hf.b_now, 12);
		g_vh.on = g_vh.have = 1, g_vh.e = f0, g_hsm.lock = 1, g_hsm.w_now = 1, g_bd.two.w = 1;
		float lw[3] = { 0, 0.15f, 0.02f }, lb[3] = { 0.05f, -0.3f, 0 }, ew[4], eb[4];
		memcpy(g_hsm.w_rot_now, lw, 12), memcpy(g_hf.b_now, lb, 12);
		swl_arm(fg, gv2);
		swg_qexp(lw, ew), swg_qexp(lb, eb);
		sv_quat R = sv_qnormalize(sv_qmul(sv_quat_of(eb), sv_quat_of(ew)));
		sv_vec3 dl = sv_qrot(R, (sv_vec3){ fg[0].p.x - fg[1].p.x, fg[0].p.y - fg[1].p.y, fg[0].p.z - fg[1].p.z });
		sa_xf wr = { fg[1].p, sv_qnormalize(sv_qmul(R, fg[1].q)) },
		      wl = { { fg[1].p.x + dl.x, fg[1].p.y + dl.y, fg[1].p.z + dl.z }, sv_qnormalize(sv_qmul(R, fg[0].q)) };
		somvr_posef xr, xl;
		vh_to_xr(&wr, &xr), vh_to_xr(&wl, &xl);
		int okr = 1, okl = 1;
		for (int k = 0; k < 4; k++)
			okr &= NEAR(g_vh.e.grip[1].q[k], xr.q[k], 1e-5), okl &= NEAR(g_vh.e.grip[0].q[k], xl.q[k], 1e-5);
		for (int k = 0; k < 3; k++)
			okr &= NEAR(g_vh.e.grip[1].p[k], f0.grip[1].p[k], 1e-6), okl &= NEAR(g_vh.e.grip[0].p[k], xl.p[k], 1e-5);
		EXPECT(okr && okl && memcmp(g_vh.e.grip[0].p, f0.grip[0].p, 12),
		       "lock: the right hand turned by the weight's and the bite's turn in place, the left carried round the right grip");
		g_vh.e = f0, g_bd.two.w = 0;
		swl_arm(fg, gv2);
		EXPECT(!memcmp(&g_vh.e.grip[0], &f0.grip[0], sizeof f0.grip[0]) && memcmp(g_vh.e.grip[1].q, f0.grip[1].q, 16) &&
			       !(swg_vm_avail() & SOMVR_VMA_SWHAND),
		       "lock: one hand - the left untouched; the Sword hand row not offered");
		g_vh.e = f0, g_hsm.lock = 0;
		swl_arm(fg, gv2);
		EXPECT(!memcmp(&g_vh.e, &f0, sizeof f0), "lock off: nothing turned");
		g_vh.on = on0, g_vh.have = have0, g_vh.e = e0, g_hsm.lock = lock0, g_hsm.w_now = wn0, g_bd.two.w = tw0;
		memcpy(g_hsm.w_rot_now, wr0, 12), memcpy(g_hf.b_now, bn0, 12);
	}
	/* 10c. build 17: the bite (SOMVR_COMBAT_BITE, somvr_hitfeel_game.c): the switch, its row live, which hits bite (a
	 * landed cut only: no tap, slap or stab, no glance, no blocked one, none without a call or a fresh drawn blade), the
	 * test words, a hit's start and a second hit refused, the switch off ending it */
	{
		int hbad = 0, mode0 = g_cb.mode;
		g_cb.mode = CB_FULL;
		setenv("SOMVR_COMBAT_BITE", "on", 1);
		hf_config(&hbad);
		somvr_settings bs;
		memset(&bs, 0, sizeof bs);
		hf_vm_settings(&bs);
		EXPECT(!hbad && g_hf.bite && bs.bite == 1 && hf_vm_avail() == SOMVR_VMA_BITE && NEAR(g_hf.cfg.hold_s, 0.06, 1e-6) &&
			       NEAR(g_hf.cfg.ease_s, 0.1, 1e-6) && log_has("CONFIG hitfeel bite=on (SOMVR_COMBAT_BITE: a landed cut holds "),
		       "bite: on from the switch (%d)", g_hf.bite);
		g_hf.d_have = 1, g_hf.d_t = cb_now(), g_hf.d_p = (sv_vec3){ 20, -20, 30 }, g_hf.d_q = (sv_quat){ 0, 0, 0, 1 };
		g_hf.d_shift = (sv_vec3){ 0, 0, 0 };
		EXPECT(hf_will_bite(SC_KIND_CUT, 0, 0, 1) && !hf_will_bite(SC_KIND_TAP, 0, 0, 1) &&
			       !hf_will_bite(SC_KIND_SLAP, 0, 0, 1) && !hf_will_bite(SC_KIND_STAB, 0, 0, 1) &&
			       !hf_will_bite(SC_KIND_CUT, 1, 0, 1) && !hf_will_bite(SC_KIND_CUT, 0, 1, 1) &&
			       !hf_will_bite(SC_KIND_CUT, 0, 0, 0),
		       "bite: a landed cut only");
		struct cb_orc bo = { 0x1234, 0, { 0, 0, 100 }, 100 };
		sc_contact bc;
		memset(&bc, 0, sizeof bc);
		bc.u = 0.5f, bc.along = 0.9f;
		somvr_blade bb;
		memset(&bb, 0, sizeof bb);
		bb.valid = 1, bb.base[0] = 20, bb.base[1] = -20, bb.base[2] = 30, bb.tip[0] = 20, bb.tip[1] = -20, bb.tip[2] = 130;
		hf_hit(&bo, &bc, SC_KIND_CUT, 1, 0, 0, 1, 1, &bb, cb_now());
		EXPECT(g_hf.st.phase == SWB_HOLD && g_hf.orc == 0x1234 && hf_biting() && log_has("BITE t=") &&
			       log_has(" start orc=0x1234: a cut at 90 % along the blade") && !hf_will_bite(SC_KIND_CUT, 0, 0, 1),
		       "bite: a hit starts one; no other meanwhile");
		uint64_t ref0 = g_hf.st.refused;
		hf_hit(&bo, &bc, SC_KIND_CUT, 1, 0, 0, 1, 1, &bb, cb_now());
		EXPECT(g_hf.st.refused == ref0 + 1 && log_has(" refused orc=0x1234"), "bite: a second start refused");
		bs.bite = 0;
		const char *bn = hf_vm_set(&bs);
		EXPECT(!g_hf.bite && g_hf.st.phase == SWB_IDLE && !hf_biting() && strstr(bn, "stops in the body") &&
			       log_has("BITE t=") && log_has(" on -> off (the VR menu)"),
		       "bite: the row off ends it (%s)", bn);
		bs.bite = 1;
		vm_apply_key(SOMVR_SK_COMBAT_BITE, &bs, "the selftest");
		EXPECT(g_hf.bite && log_has("VRMENU apply COMBAT_BITE=on from the selftest (a full cut stops"), "bite: vm_apply_key");
		int test1 = g_bd.test;
		g_bd.test = 1;
		int seq0 = atomic_load(&g_hf.req_seq);
		EXPECT(!strcmp(hf_word("bite off"), "fight bite") && atomic_exchange(&g_hf.req, 0) == 1 &&
			       !strcmp(hf_word("bite hold 800 300 60"), "fight bite hold") && atomic_load(&g_hf.req_seq) == seq0 + 1 &&
			       atomic_load(&g_hf.req_hold_b) == 800 && atomic_load(&g_hf.req_ease_b) == 300 &&
			       atomic_load(&g_hf.req_deg_b) == 60 && strstr(hf_word("bite hard"), "ignored") &&
			       !strcmp(hf_word("hitfx off"), "fight hitfx") && atomic_exchange(&g_hf.req_fx, 0) == 1 &&
			       atomic_load(&g_hf.req) == 0 && !strcmp(hf_word("bite dumps 3"), "fight bite dumps") &&
			       atomic_exchange(&g_hf.req_dumps, 0) == 4,
		       "bite: the test words");
		g_bd.test = test1;
		unsetenv("SOMVR_COMBAT_BITE");
		hf_config(&hbad);
		EXPECT(!g_hf.bite && !hf_will_bite(SC_KIND_CUT, 0, 0, 1), "bite: off by default");
		g_cb.mode = mode0;
	}
	unsetenv("SOMVR_SWORD_WEIGHT");
	swg_level(wt0);
	/* 11. the end line */
	bf_end_log();
	EXPECT(log_has("FINGERS END on=1: evals "), "the end line");
	/* restore */
	g_bd.angle_deg = angle0, g_cb.bcfg.pitch_rad = pitch0;
	g_arm.cal[0] = calx0[0], g_arm.cal[1] = calx0[1];
	memcpy(g_arm.cal_v, cal0, sizeof cal0);
	g_bf.on = 0, g_bf.want = 0, g_bf.skel = 0, g_bf.nodes = 0;
	bd_word("trigger R -1"), bd_word("thumb R -1"), bd_word("squeeze R -1");
	bd_frame(1);
	unsetenv("SOMVR_FINGERS"), unsetenv("SOMVR_FINGERS_MS");
	g_bd.test = test0, g_sl.on = sl_on0, g_sl.have_g = sl_hg0, g_sl.g.item[0] = item0, g_sl.g.sheathed = sheathed0;
	g_bd.two.w = two_w0;
	g_arm.on = arm_on0, g_arm.chains_ok = chains0, g_arm.nodes = nodes0, g_arm.skel = skel0;
	g_arm.weight[0] = w0[0], g_arm.weight[1] = w0[1];
	memcpy(g_arm.ch, ch0, sizeof ch0);
}

/* SOMVR_VRHANDS (build 10, somvr_vrhands_game.c): the switch, the object, the grips back in XR space, the entry per
 * frame with HIDE only while the layer draws ours and the body node was hidden that frame, the body node kept hidden */
static void vh_tests(void)
{
	int arm_on0 = g_arm.on, bd_on0 = g_bd.on, drawn0 = g_bd.drawn;
	uint64_t frame0 = g_fr.frame, hid0 = g_hp_main_hid;
	int bad = 0;
	unsetenv("SOMVR_VRHANDS");
	vh_config(&bad);
	g_arm.on = 1;
	vh_start();
	EXPECT(!g_vh.on && !atomic_load(&g_vho.active) && log_has("CONFIG vrhands=0"), "vrhands: off by default");
	setenv("SOMVR_VRHANDS", "1", 1);
	vh_config(&bad);
	vh_start();
	EXPECT(g_vh.on && !bad && somvr_vrhands_ok(&g_vho) && log_has("CONFIG vrhands=1"), "vrhands on: %d", g_vh.on);
	/* the game convention and back: exactly the XR grip */
	somvr_hand hx = { .valid = 1, .pose = { { 0.1f, -0.2f, 0.3f, 0.9273618f }, { 0.25f, 1.1f, -0.4f } } };
	sa_xf g[2];
	somvr_hand_game(&hx, &g[1].p, &g[1].q, NULL, NULL);
	g[0] = g[1];
	somvr_posef back;
	vh_to_xr(&g[1], &back);
	int same = 1;
	for (int i = 0; i < 4; i++)
		same &= NEAR(back.q[i], hx.pose.q[i], 1e-6);
	for (int i = 0; i < 3; i++)
		same &= NEAR(back.p[i], hx.pose.p[i], 1e-6);
	EXPECT(same, "vrhands: the grip back in XR space: %g %g %g %g %g %g %g", back.q[0], back.q[1], back.q[2], back.q[3],
	       back.p[0], back.p[1], back.p[2]);
	/* the layer not drawing yet: the game's hands, no HIDE */
	atomic_store(&g_vho.layer_state, SOMVR_VHL_UNKNOWN);
	g_bd.on = 1, g_bd.drawn = 1;
	EXPECT(!vh_hides() && bd_body_drawn(), "vrhands: the game's hands while the layer has not said it draws");
	int valid[2] = { 0, 1 };
	g_fr.frame = 4321, g_hp_main_hid = 4321;
	vh_arm(g, valid, 777, 0.011f);
	vh_h4(1);
	somvr_vrhands_frame e;
	EXPECT(somvr_vrhands_get(&g_vho, 4321, &e) && e.flags == SOMVR_VHF_R && e.time == 777 &&
		       NEAR(e.grip[1].p[2], -0.4f, 1e-6) && e.curl[1][2] >= 0 && e.curl[1][2] <= 1,
	       "vrhands: the entry before the layer draws: flags 0x%x", e.flags);
	/* drawing: the body node stays hidden, HIDE on a first-person frame whose eyes hid the body */
	atomic_store(&g_vho.layer_state, SOMVR_VHL_DRAWING);
	EXPECT(vh_hides() && !bd_body_drawn(), "vrhands: the body node hidden while ours are drawn");
	g_fr.frame = 4322, g_hp_main_hid = 4322;
	vh_arm(g, valid, 778, 0.011f);
	vh_h4(1);
	EXPECT(somvr_vrhands_get(&g_vho, 4322, &e) && e.flags == (SOMVR_VHF_R | SOMVR_VHF_HIDE) &&
		       log_has("VRHANDS t=") && log_has("the layer: drawing ours (the game's hands hidden in the eyes)"),
	       "vrhands: HIDE: flags 0x%x", e.flags);
	g_fr.frame = 4323; /* the eyes did not hide the body (third person, the body moments) */
	vh_arm(g, valid, 779, 0.011f);
	vh_h4(1);
	EXPECT(somvr_vrhands_get(&g_vho, 4323, &e) && !(e.flags & SOMVR_VHF_HIDE), "vrhands: no HIDE: flags 0x%x", e.flags);
	g_fr.frame = 4324, g_hp_main_hid = 4324;
	vh_h4(0); /* not live, and no grips kept since the last H4 */
	EXPECT(somvr_vrhands_get(&g_vho, 4324, &e) && e.flags == 0, "vrhands: not live, no grips: flags 0x%x", e.flags);
	/* the layer off: the game's hands come back */
	atomic_store(&g_vho.layer_state, SOMVR_VHL_OFF);
	EXPECT(!vh_hides() && bd_body_drawn(), "vrhands: the layer off: the game's hands again");
	vh_end_log();
	EXPECT(log_has("VRHANDS END on=1: entries "), "vrhands: the end line");
	/* restore */
	unsetenv("SOMVR_VRHANDS");
	g_vh.on = g_vh.want = 0;
	atomic_store(&g_vho.active, 0);
	g_arm.on = arm_on0, g_bd.on = bd_on0, g_bd.drawn = drawn0, g_fr.frame = frame0, g_hp_main_hid = hid0;
}

/* SOMVR_HANDS_SURFACE=on (somvr_surface_game.c) on the climb's stand-ins (cl_stand_in_sites: the wall's face at z = 0,
 * its stone, Talion's cylinder): the switch and its needs, the ray (Talion passed by the filter, the normal, the
 * surface's name; a soft surface passed), a grip pushed 10 cm into the wall stopped off it with the ghost at the real
 * grip (the entry's v3 fields), the contact's tick and lines, the release, an exception; the blade's push is the pure
 * part's (tests/test_surface.c) */
static void sf_tests(void)
{
	int arm_on0 = g_arm.on, inp_on0 = g_inp.on, bad = 0;
	float pos0[3];
	memcpy(pos0, g_pobj.pos, sizeof pos0);
	sc_rigxf rig0 = g_arm.rig;
	double cbt0 = g_cb_test_t;
	unsetenv("SOMVR_HANDS_SURFACE");
	sf_config(&bad);
	sf_start();
	EXPECT(!g_sf.on && !bad && log_has("CONFIG surface=off (the hands and the sword pass through the world"),
	       "surface: off by default");
	setenv("SOMVR_HANDS_SURFACE", "on", 1);
	g_arm.on = 0, g_inp.on = 1;
	sf_config(&bad);
	sf_start();
	EXPECT(!g_sf.on && log_has("CONFIG surface=off: SOMVR_HANDS_SURFACE=on needs SOMVR_ARMS=1"), "surface: needs the arms");
	g_arm.on = 1;
	setenv("SOMVR_HANDS_SURFACE_GHOST", "2", 1);
	sf_config(&bad);
	EXPECT(bad && log_has("WARN SOMVR_HANDS_SURFACE_GHOST=2 is not 0..1"), "surface: a bad ghost opacity");
	unsetenv("SOMVR_HANDS_SURFACE_GHOST");
	bad = 0;
	sf_config(&bad);
	sf_start();
	EXPECT(g_sf.on && !bad && g_sf.model_ok && log_has("CONFIG surface=on (the hands and the sword stop at the world's surfaces") &&
		       log_has("on our drawn hand (its curls)"),
	       "surface: on (%d, model %d)", g_sf.on, g_sf.model_ok);
	/* the ray: from inside Talion (his cylinder: the filter passes him) to 5 cm past the wall's face */
	g_pobj.pos[0] = 500, g_pobj.pos[1] = 100, g_pobj.pos[2] = -50;
	g_sf.self = g_sf.ud.self = (uintptr_t)&g_pobj;
	g_clw = (typeof(g_clw)){ 0, 0, 1, 0, 0 };
	uintptr_t phys = peek_ptr(g_clg.phys_pp);
	somvr_sf_plane pl;
	char nm[32];
	uintptr_t ob = 0;
	int hit = sf_ray(phys, (sv_vec3){ 500, 150, -50 }, (sv_vec3){ 520, 150, 5 }, 5, &pl, nm, &ob, 1.0);
	EXPECT(hit && NEAR(pl.p.z, 0, 1e-3) && NEAR(pl.n.z, -1, 1e-5) && !strcmp(nm, "ST_Climbable_Stone") &&
		       ob == (uintptr_t)g_wallobj && g_clw.bad == 0 && g_clw.cb_calls > 0,
	       "surface: the ray: hit %d at z %g n %g '%s' (bad %d, the filter asked %d)", hit, pl.p.z, pl.n.z, nm, g_clw.bad,
	       g_clw.cb_calls);
	EXPECT(!sf_ray(phys, (sv_vec3){ 500, 150, -50 }, (sv_vec3){ 520, 150, -20 }, 5, &pl, nm, &ob, 1.0),
	       "surface: a ray short of the wall");
	const char *stone = g_srec_names[0], *bush = "Foliage_Bush";
	memcpy(g_srec[0] + 0x20, &bush, 8);
	uint64_t soft0 = g_sf.n.passed_soft;
	EXPECT(!sf_ray(phys, (sv_vec3){ 500, 150, -50 }, (sv_vec3){ 520, 150, 5 }, 5, &pl, nm, &ob, 1.0) &&
		       g_sf.n.passed_soft == soft0 + 1,
	       "surface: a bush is passed");
	memcpy(g_srec[0] + 0x20, &stone, 8);
	hit = sf_ray(phys, (sv_vec3){ 500, 150, -50 }, (sv_vec3){ 520, 150, 5 }, 5, &pl, nm, &ob, 1.0);
	/* the grips: the rig the identity about (500, 100, -50); the right grip 10 cm into the wall */
	g_arm.rig = (sc_rigxf){ { 500, 100, -50 }, { 0, 0, 0 }, 0, 1 };
	struct sf_found *f = &g_sf.f[1];
	memset(f, 0, sizeof *f);
	f->frame = g_fr.frame, f->npl = 1, f->pl[0] = pl, f->obj[0] = ob;
	snprintf(f->name[0], sizeof f->name[0], "%s", nm);
	sa_xf gg[2], real = { { 20, 50, 60 }, { 0, 0, 0, 1 } };
	int gv[2] = { 0, 1 }, bowed[2] = { 0, 0 };
	uint64_t hp0 = atomic_load(&g_in.n_hap_posts);
	g_cb_test_t = 20000;
	g_sf.t_last = 0, g_sf.tid = my_tid(); /* (sf_frame's, the game thread) */
	for (int k = 0; k < 12; k++) {
		f->frame = g_fr.frame;
		gg[1] = real;
		sf_arm(gg, gv, bowed, (sv_vec3){ 0, 0, 0 });
		g_cb_test_t += 0.011;
	}
	float zmax = -1e9f;
	for (int i = 0; i < SOMVR_SF_NS; i++)
		zmax = fmaxf(zmax, gg[1].p.z - 50 + g_sf.pg[1][i][2] + g_sf.rad[i]);
	EXPECT(gg[1].p.z < 50 && NEAR(zmax, 0, 0.02) && gg[1].p.x == 20 && gg[1].p.y == 50 &&
		       log_has("SURF t=20000.000 clamp hand=R depth=") && log_has("surf=ST_Climbable_Stone") &&
		       atomic_load(&g_in.n_hap_posts) > hp0,
	       "surface: the grip stopped: z %g (the deepest sample's edge %g), haptics %llu", gg[1].p.z, zmax,
	       (unsigned long long)(atomic_load(&g_in.n_hap_posts) - hp0));
	somvr_vrhands_frame e;
	memset(&e, 0, sizeof e);
	sf_ghost(&e);
	somvr_posef rx;
	vh_to_xr(&real, &rx);
	EXPECT(g_sf.ghost_a[1] > 0.3f && e.ghost_a[1] == g_sf.ghost_a[1] && e.ghost_a[0] == 0 &&
		       !memcmp(&e.ghost[1], &rx, sizeof rx) && log_has("SURF t=") && log_has("ghost hand=R on"),
	       "surface: the ghost at the real grip: %g", g_sf.ghost_a[1]);
	/* out of the wall (the plane kept): the release; then an exception (the bow): nothing clamps */
	gg[1] = (sa_xf){ { 20, 50, 20 }, { 0, 0, 0, 1 } };
	sf_arm(gg, gv, bowed, (sv_vec3){ 0, 0, 0 });
	EXPECT(log_has("release hand=R after "), "surface: the release");
	for (int k = 0; k < 30; k++) { /* the ease back */
		g_cb_test_t += 0.011;
		gg[1] = (sa_xf){ { 20, 50, 20 }, { 0, 0, 0, 1 } };
		sf_arm(gg, gv, bowed, (sv_vec3){ 0, 0, 0 });
	}
	EXPECT(gg[1].p.z == 20 && g_sf.ghost_a[1] == 0, "surface: back on the controller: %g", gg[1].p.z);
	bowed[0] = 1;
	gg[1] = real;
	g_cb_test_t += 0.011;
	sf_arm(gg, gv, bowed, (sv_vec3){ 0, 0, 0 });
	EXPECT(gg[1].p.z == 60 && log_has("except hand=R: the bow is out"), "surface: the bow: no clamp (%g)", gg[1].p.z);
	sf_end_log();
	EXPECT(log_has("SURF END frames ") && log_has("contacts 0 / 1"), "surface: the end line");
	printf("surface  the switch (off by default, needs the arms, a bad opacity), the ray on the stand-ins (Talion passed "
	       "by the filter, the normal, stone; a bush passed), a grip 10 cm into the wall stopped off it, the tick, the "
	       "ghost at the real grip into the entry, the release and the ease back, the bow's exception\n");
	/* restore */
	unsetenv("SOMVR_HANDS_SURFACE");
	g_sf.on = g_sf.want = 0;
	g_arm.on = arm_on0, g_inp.on = inp_on0, g_arm.rig = rig0, g_cb_test_t = cbt0;
	memcpy(g_pobj.pos, pos0, sizeof pos0);
}

static void pr_tests(const char *trig)
{
	/* the state the earlier tests left (restored at the end) */
	char tp0[sizeof g_trig_path];
	snprintf(tp0, sizeof tp0, "%s", g_trig_path);
	uintptr_t vt_npc0 = g_cbg.vt_npc;
	st_char_t *owner0 = g_pcs.owner;
	st_hobj_t *hobj0 = g_pcs.hobj;
	st_obj_t pobj0 = g_pobj;
	int vm_on0 = g_vm.on;
	somvr_rec *rec0 = g_fr.rec;
	typeof(g_pcomb.band) bands0;
	memcpy(bands0, g_pcomb.band, sizeof bands0);
	/* the world: the fallback's Player (life 2, no presentation, no cinematic, no sync, not paused), Talion at the
	 * save's tower top facing +Z, his CPlayerObj (min health 0), the band orcs around the spot */
	fb_world();
	g_cbg.vt_npc = (uintptr_t)st_pr_vt_npc;
	memset(&g_prpc, 0, sizeof g_prpc);
	g_prpc.vt = (uintptr_t)st_pr_vt_pobj, g_prpc.health = 300;
	g_pcs.owner = (st_char_t *)&g_prpc, g_pcs.hobj = (st_hobj_t *)&g_pobj;
	g_pobj.pos[0] = 35400, g_pobj.pos[1] = 5402, g_pobj.pos[2] = -22400;
	memset(g_pobj.rot, 0, sizeof g_pobj.rot), g_pobj.rot[3] = 1;
	for (int i = 0; i < 8; i++) {
		memset(g_prrec[i], 0, sizeof g_prrec[i]);
		g_prrec[i][1] = 2;
		uintptr_t n = (uintptr_t)g_prrec_names[i];
		memcpy(g_prrec[i] + 0x20, &n, 8);
	}
	for (int i = 0; i < 4; i++) {
		memset(g_prwrec[i], 0, sizeof g_prwrec[i]);
		g_prwrec[i][1] = 2;
		uintptr_t n = (uintptr_t)g_prwrec_names[i];
		memcpy(g_prwrec[i] + 0x20, &n, 8);
	}
	g_prw.rec = g_prwrec[0], g_prw.tod = 1; /* a night */
	pr_orc(0, 0, 20, 0), pr_orc(1, 1, 10, 0), pr_orc(2, 2, 59, 0), pr_orc(3, 0, 70, 0), pr_orc(4, 0, 15, 1);
	pr_orc(5, 6, 3, 0), pr_orc(6, 0, 30, 0), pr_orc(7, 2, 40, 0);
	fb_p(FB_P_MOUNT, (uintptr_t)&g_prh[5]); /* Talion rides orc 5 (a caragor) */
	memset(g_prband, 0, sizeof g_prband);
	g_prband[0][0].c = &g_pcs, g_prband[0][1].c = &g_prc[0], g_prband[0][2].c = &g_prc[1], g_prband[0][3].c = &g_prc[2];
	g_prband[1][0].c = &g_prc[3], g_prband[1][1].c = &g_prc[4], g_prband[1][2].c = &g_prc[5], g_prband[1][3].c = &g_prc[0];
	g_pcomb.band[0].b = g_prband[0], g_pcomb.band[0].e = g_prband[0] + 4;
	g_pcomb.band[1].b = g_prband[1], g_pcomb.band[1].e = g_prband[1] + 8; /* [4..7]: later orcs, our spawns at [7] */
	memset(&g_pcomb.band[2], 0, sizeof g_pcomb.band[2]);
	g_rs_hook = pr_st_console, g_pr_ncmd = 0, g_pr_removes = 0;
	g_vm.on = 1;
	atomic_store(&g_vmo.practice, 0), atomic_store(&g_vmo.pstate, 0);
	g_pr.on = 1, g_pr.menu_seen = 0, g_pr.trig_seen = somvr_vm_pr_seq(atomic_load(&g_pr_trig));
	g_pr_test_t = 20000;
	EXPECT(g_pr.mode == SOMVR_PR_ON && g_pr.ok_mh && g_pr.ok_spawn && g_pr.ok_clear && g_pr.ok_light && g_pr.ok_gate &&
		       g_pr.minhealth == 20 && g_pr.clear_cm == 6000 && g_pr.radius_cm == 10000 && g_pr.ahead_cm == 350 &&
		       g_pr.spot[0] == 40400 && g_pr.spot[2] == -21600 && !strcmp(g_pr.weather, "DayNoRain") &&
		       pr_avail() == SOMVR_VMA_PRACTICE && log_has("CONFIG practice: on; the spot (40400 3900 -21600) cm"),
	       "practice at start: mode %d mh %d spawn %d clear %d light %d gate %d", g_pr.mode, g_pr.ok_mh, g_pr.ok_spawn,
	       g_pr.ok_clear, g_pr.ok_light, g_pr.ok_gate);
	/* 1. idle: nothing asked, nothing done */
	pr_step(30);
	EXPECT(g_pr_ncmd == 0 && g_pr_removes == 0 && g_pr_mh_calls == 0 && !g_pr.here && atomic_load(&g_vmo.pstate) == 0,
	       "idle: %d commands, %d removals", g_pr_ncmd, g_pr_removes);
	/* 2. "practice go" through the trigger file: the blink, one teleport at black, the arrival, the night made day and
	 * the weather under the black, the first clear pass (A and the warg out; the slave, the far one, the dead one and
	 * the mount stay), min health 20 from the blink's start; the fade-in */
	snprintf(g_trig_path, sizeof g_trig_path, "%s", trig);
	set_trigger(trig, "on");
	set_trigger(trig, "practice go");
	EXPECT(log_has("TRIGGER cmd=practice go t="), "the trigger word practice go");
	pr_step(1);
	EXPECT(g_pr.trip.phase == SOMVR_PRT_OUT && pr_mh_now() == 20 && g_pr.mh_old == 0 && g_pr.have_back &&
		       g_pr.back[1] == 5402 && (atomic_load(&g_vmo.pstate) & SOMVR_VMA_PR_BUSY),
	       "go: phase %d, min health %g", g_pr.trip.phase, (double)pr_mh_now());
	somvr_rec rr;
	memset(&rr, 0, sizeof rr);
	g_fr.rec = &rr;
	pr_step(24); /* 0.27 s: black */
	pr_fade();
	EXPECT(somvr_rec_fade(rr.flags) == 255 && pr_cmd_has("Teleport 40400 3900 -21600") == 1 && g_pobj.pos[0] == 40400,
	       "the teleport at black: fade %u, commands %d ('%s')", somvr_rec_fade(rr.flags), g_pr_ncmd, g_pr_cmds[0]);
	pr_step(50); /* the settle */
	EXPECT(g_pr.here && g_pr.went && g_pr.trip.phase == SOMVR_PRT_HOLD && pr_cmd_has("GameServerShell.AdvanceTimeOfDay") &&
		       g_prw.tod == 0 && log_has("light: weather 'SON_Night_01', time of day 'Night': a night, the day now"),
	       "arrived: here %d phase %d tod %u", g_pr.here, g_pr.trip.phase, g_prw.tod);
	EXPECT(pr_removed(0) && pr_removed(2) && !pr_removed(1) && !pr_removed(3) && !pr_removed(4) && !pr_removed(5) &&
		       g_pr_removes == 2 && log_has("clear 'AI_Infantry' at 20.0 m") && log_has("clear 'Warg_Brown' at 59.0 m"),
	       "the first pass: %d removals (A %d warg %d slave %d far %d dead %d mount %d)", g_pr_removes, pr_removed(0),
	       pr_removed(2), pr_removed(1), pr_removed(3), pr_removed(4), pr_removed(5));
	EXPECT(g_pr.ok_quiet && g_pr.quiet && g_prq_mgr.flag == 0 && g_prq_dis == 1 && g_prq_rm == 1 && g_prq_en == 0 &&
		       g_pr.quiet_was && log_has("quiet (in the area): the ambient AI spawn manager") &&
		       log_has("(it was on, now off); its 3 ambient AI removed"),
	       "quiet at the arrival: ok %d on %d flag %u disable %d removeall %d", g_pr.ok_quiet, g_pr.quiet, g_prq_mgr.flag,
	       g_prq_dis, g_prq_rm);
	pr_step(60);
	EXPECT(pr_cmd_has("GameServerShell.ChangeWeather DayNoRain") && g_prw.rec == g_prwrec[2] && g_pr.weather_set &&
		       g_pr.advanced && !strcmp(g_pr.orig_weather, "SON_Night_01"),
	       "the weather: %d commands, orig '%s'", g_pr_ncmd, g_pr.orig_weather);
	/* 2a. SOMVR_PRACTICE_LOADHOLD: the game's pause ~3 s after our ChangeWeather comes under the black: the black holds
	 * while it is to come and while it is on, then fades in */
	EXPECT(g_pr.load_mode && g_pr.pw_waits == 1 && g_pr.pw_t > 0 && log_has("load hold on (the black holds for the game's"),
	       "the pause awaited: mode %d waits %llu", g_pr.load_mode, (unsigned long long)g_pr.pw_waits);
	pr_step(80); /* past the 1 s hold */
	pr_fade();
	EXPECT(g_pr.trip.phase == SOMVR_PRT_HOLD && somvr_rec_fade(rr.flags) == 255 &&
		       log_has("the black held past its 1.00 s for the game's pause after our ChangeWeather"),
	       "held for the pause to come: phase %d fade %u", g_pr.trip.phase, somvr_rec_fade(rr.flags));
	g_pz[0x34] = 1; /* the game's pause */
	pr_step(30);
	EXPECT(g_pr.trip.phase == SOMVR_PRT_HOLD && g_pr.pw_pauses == 1 && g_pr.pw_open == 0 &&
		       log_has("s after our ChangeWeather (under the blink's black)"),
	       "held while paused: phase %d pauses %llu open %llu", g_pr.trip.phase, (unsigned long long)g_pr.pw_pauses,
	       (unsigned long long)g_pr.pw_open);
	g_pz[0x34] = 0;
	pr_step(1);
	EXPECT(g_pr.trip.phase == SOMVR_PRT_IN && g_pr.pw_t == 0 &&
		       log_has("the game's pause after our ChangeWeather over (0.33 s long)") &&
		       log_has("longer (the game's pause over): fading in"),
	       "the pause over: phase %d", g_pr.trip.phase);
	pr_step(40);
	pr_fade();
	EXPECT(g_pr.trip.phase == SOMVR_PRT_IDLE && log_has("the blink there ended") && g_pr.teleports == 1 &&
		       (atomic_load(&g_vmo.pstate) & (SOMVR_VMA_PR_HERE | SOMVR_VMA_PR_BACK | SOMVR_VMA_PR_BUSY)) ==
			       (SOMVR_VMA_PR_HERE | SOMVR_VMA_PR_BACK),
	       "faded in: phase %d pstate %#x", g_pr.trip.phase, atomic_load(&g_vmo.pstate));
	EXPECT(log_has("light now: weather 'DayNoRain', time of day 'Day' (was 'SON_Night_01', 'Night')"), "the light line");
	/* 2b. a script turns the spawn manager on again: off again (and its AI removed) at the next pass; held in a
	 * presentation like the clear */
	g_prq_mgr.flag = 1;
	*(uintptr_t *)(g_fbpres + FB_PRES_CUR) = 1;
	pr_step(100);
	EXPECT(g_prq_mgr.flag == 1 && g_prq_dis == 1, "quiet held in a presentation: flag %u disable %d", g_prq_mgr.flag,
	       g_prq_dis);
	*(uintptr_t *)(g_fbpres + FB_PRES_CUR) = 0;
	pr_step(100);
	EXPECT(g_prq_mgr.flag == 0 && g_prq_dis == 2 && g_prq_rm == 2 && g_pr.quiet_again == 1 &&
		       log_has("quiet (the game had turned it on again)"),
	       "quiet again: flag %u disable %d removeall %d", g_prq_mgr.flag, g_prq_dis, g_prq_rm);
	/* 3. a presentation (a Nemesis showdown): nothing removed while it runs; the next pass after it */
	g_prband[1][4].c = &g_prc[6];
	*(uintptr_t *)(g_fbpres + FB_PRES_CUR) = 1;
	pr_step(100);
	EXPECT(!pr_removed(6) && g_pr.held >= 1 && log_has("clear held: a presentation (nothing removed meanwhile)"),
	       "held in a presentation: removed %d held %llu", pr_removed(6), (unsigned long long)g_pr.held);
	*(uintptr_t *)(g_fbpres + FB_PRES_CUR) = 0;
	pr_step(100);
	EXPECT(pr_removed(6), "after the presentation: removed");
	/* 4. "practice spawn shield" (trigger file): the defender's template, the ray down 3.5 m ahead (+Z), the debug
	 * spawn's call facing Talion, the zone and the level; the clear never takes it */
	set_trigger(trig, "practice spawn shield");
	pr_step(1);
	float yaw = 2 * atan2f(g_pr_spawn_xf[4], g_pr_spawn_xf[6]) * 57.29578f;
	EXPECT(g_pr_spawns == 1 && g_pr_spawn_tmpl == (uintptr_t)g_prrec[4] && !strcmp(g_pr_spawn_name, "SomVR_Practice") &&
		       NEAR(g_pr_ray_from[0], 40400, 0.01) && NEAR(g_pr_ray_from[2], -21600 + 350, 0.01) &&
		       NEAR(g_pr_ray_from[1], 3900 + 200, 0.01) && NEAR(g_pr_ray_to[1], 3900 - 600, 0.01) &&
		       g_pr_ray_flags[0] == 0x50 && g_pr_ray_flags[1] == 0xc && NEAR(g_pr_spawn_xf[1], 3910, 0.01) &&
		       NEAR(fabsf(yaw), 180, 0.01) && g_pr_spawn_ctx == (uintptr_t)&g_pr_ground && g_pr_zone_calls == 1 &&
		       g_pr_zone_npc == (uintptr_t)&g_prnpc[8] && g_pr_level_calls == 1 && g_pr_level_seen == 7 &&
		       g_pr.orc_h == (uintptr_t)&g_prh[8] && log_has("TRIGGER cmd=practice spawn t=") &&
		       log_has("spawn Shield (the trigger word) template Orc_Defender ('Orc_Defender') ok"),
	       "spawn shield: spawns %d yaw %g from (%g %g %g) zone %d level %d/%u", g_pr_spawns, yaw, g_pr_ray_from[0],
	       g_pr_ray_from[1], g_pr_ray_from[2], g_pr_zone_calls, g_pr_level_calls, g_pr_level_seen);
	set_trigger(trig, "practice spawn spider");
	EXPECT(log_has("TRIGGER cmd=practice (not go|back|spawn [infantry|shield|berserker|caragor|ghul|graug]|far [TYPE]|remove|"
		       "status|arrows|health|hurt HP|focus F: ignored)"),
	       "a bad type word");
	set_trigger(trig, NULL);
	snprintf(g_trig_path, sizeof g_trig_path, "%s", tp0);
	int rm0 = g_pr_removes;
	pr_step(300); /* the checks at 1 and 3 s; clear passes */
	EXPECT(g_pr_removes == rm0 && g_pr.orc_checks == 2 && log_has("spawn check 2: the Shield orc 'AI_Defender' alive"),
	       "our orc kept by the clear: removals %d -> %d, checks %d", rm0, g_pr_removes, g_pr.orc_checks);
	/* 5. a second spawn (the word, no type: the last one's) removes the first; a berserker by the menu then */
	EXPECT(!strcmp(pr_word("spawn"), "practice spawn"), "the word: spawn");
	pr_step(1);
	EXPECT(g_pr_spawns == 2 && pr_removed(8) && g_pr.orc_h == (uintptr_t)&g_prh[9] && g_pr_spawn_tmpl == (uintptr_t)g_prrec[4],
	       "a second spawn: spawns %d, the first removed %d", g_pr_spawns, pr_removed(8));
	atomic_store(&g_vmo.practice, somvr_vm_pr_pack(1, SOMVR_VMPR_SPAWN, SOMVR_PRO_BERSERKER));
	pr_step(1);
	EXPECT(g_pr_spawns == 3 && pr_removed(9) && g_pr_spawn_tmpl == (uintptr_t)g_prrec[5] && g_pr.req_menu == 1 &&
		       log_has("spawn Berserker (the menu) template Orc_Berserker"),
	       "the menu's spawn: spawns %d", g_pr_spawns);
	/* 6. our orc goes down; the menu's Remove removes it; Remove again: nothing */
	g_prc[8].dead = 1;
	pr_step(2);
	EXPECT(g_pr.orc_dead && g_pr.killed == 1 && log_has("the Berserker orc is down after"), "down: %d", g_pr.orc_dead);
	int r6 = g_pr_removes;
	atomic_store(&g_vmo.practice, somvr_vm_pr_pack(2, SOMVR_VMPR_REMOVE, 0));
	pr_step(1);
	EXPECT(g_pr_removes == r6 + 1 && r6 < 16 && g_pr_removed[r6] == (uintptr_t)&g_prh[8] && !g_pr.orc_h &&
		       !(atomic_load(&g_vmo.pstate) & SOMVR_VMA_PR_ORC),
	       "remove: removals %d -> %d, orc %#lx", r6, g_pr_removes, (unsigned long)g_pr.orc_h);
	pr_word("remove");
	pr_step(1);
	EXPECT(log_has("remove (the trigger word): no orc out"), "remove without an orc");
	/* 6b. build 18: the far orc (the user's ask: unaware, far, for the strike, the bow, the stealth kill): the ray down
	 * SOMVR_PRACTICE_FAR_CM (18 m) ahead, the spawn facing away from Talion (+Z: yaw 0), the watch every 2 s (no target:
	 * unaware), then its Combat targeting Talion's HOBJECT = noticed (a hit, the game's senses) */
	EXPECT(!strcmp(pr_word("far berserker"), "practice far"), "the word: far");
	pr_step(1);
	float fyaw = 2 * atan2f(g_pr_spawn_xf[4], g_pr_spawn_xf[6]) * 57.29578f;
	uintptr_t fc = g_pr.orc_c;
	EXPECT(g_pr.far && fc && NEAR(g_pr_ray_from[0], 40400, 0.01) && NEAR(g_pr_ray_from[2], -21600 + 1800, 0.01) &&
		       NEAR(fyaw, 0, 0.01) && g_pr.cur_orc == SOMVR_PRO_BERSERKER &&
		       log_has("spawn far Berserker (the trigger word) template ") && log_has("1800 cm ahead") &&
		       log_has("its back to Talion (unaware"),
	       "far: the ray from (%g %g %g), yaw %g, Combat %#lx", g_pr_ray_from[0], g_pr_ray_from[1], g_pr_ray_from[2], fyaw,
	       (unsigned long)fc);
	pr_step(200);
	EXPECT(log_has("far orc +2 s: 18.0 m from Talion (18.0 at the spawn), moved 0.0 m; its target none; unaware"),
	       "far: the watch at 2 s");
	uintptr_t th = (uintptr_t)g_pcs.hobj;
	memcpy((char *)fc + PR_C_TARGET, &th, 8);
	pr_step(1);
	EXPECT(g_pr.far_aware && g_pr.far_noticed == 1 && log_has("far orc noticed Talion after 2.") &&
		       log_has("its target = Talion (Combat+0x4c0"),
	       "far: noticed (aware %d)", g_pr.far_aware);
	th = 0, memcpy((char *)fc + PR_C_TARGET, &th, 8);
	pr_word("remove"), pr_step(1);
	EXPECT(!g_pr.orc_h, "far: removed");
	/* 7. failures: no template, no ground, no NPC */
	g_pr_spawn_refuse = 2, pr_word("spawn infantry"), pr_step(1);
	g_pr_spawn_refuse = 0, g_pr_ray_miss = 1, pr_word("spawn"), pr_step(1);
	g_pr_ray_miss = 0, g_pr_spawn_refuse = 1, pr_word("spawn"), pr_step(1);
	g_pr_spawn_refuse = 0;
	EXPECT(g_pr.spawn_fail == 3 && !g_pr.orc_h && log_has("FAILED: no such AI/Templates record") &&
		       log_has("FAILED: the ray down hit nothing") && log_has("FAILED: FUN_029a4830 made no NPC"),
	       "spawn failures %llu", (unsigned long long)g_pr.spawn_fail);
	/* 8. the game sets the min health back: ours again within a second; walking out: back to 0, no clearing; in again */
	g_prpc.minhealth = 0;
	pr_step(100);
	EXPECT(pr_mh_now() == 20 && g_pr.mh_reasserts == 1, "reasserted: %g (%llu)", (double)pr_mh_now(),
	       (unsigned long long)g_pr.mh_reasserts);
	g_pobj.pos[0] = 40400 + 15000;
	g_prband[1][5].c = &g_prc[7]; /* a warg near the spot, Talion away */
	pr_step(200);
	EXPECT(!g_pr.here && pr_mh_now() == 0 && !pr_removed(7) && g_pr.walked_out == 1 &&
		       log_has("Talion walked out of the practice area (150.0 m from the spot)"),
	       "walked out: here %d min %g", g_pr.here, (double)pr_mh_now());
	EXPECT(!g_pr.quiet && g_prq_mgr.flag == 1 && g_prq_en == 1 &&
		       log_has("quiet ends (walked out of the area): the spawn manager on again"),
	       "walked out: quiet %d flag %u enable %d", g_pr.quiet, g_prq_mgr.flag, g_prq_en);
	pr_word("spawn");
	pr_step(1);
	EXPECT(log_has("refused: not in the practice area"), "no spawn outside the area");
	g_pobj.pos[0] = 40400;
	pr_step(100);
	EXPECT(g_pr.here && pr_mh_now() == 20 && pr_removed(7), "back in: here %d min %g warg removed %d", g_pr.here,
	       (double)pr_mh_now(), pr_removed(7));
	EXPECT(g_pr.quiet && g_prq_mgr.flag == 0 && g_prq_dis == 3 && g_pr.quiet_on == 2 &&
		       log_has("quiet (back in the area)"),
	       "back in: quiet %d flag %u disable %d", g_pr.quiet, g_prq_mgr.flag, g_prq_dis);
	/* 9. the status word; "go" again while there: refused */
	pr_word("status"), pr_word("go");
	pr_step(1); /* (one word per frame: the second overwrote the first) */
	pr_word("status");
	pr_step(1);
	EXPECT(log_has("go (the trigger word) refused: already there") && log_has("status: in the practice area; trip idle"),
	       "status / go refused");
	/* 9b. build 17: the page's refills in the area (the user's ask): the test aids lower Talion's health and the Focus;
	 * the menu's Refill arrows + focus (the tokens to their max, the energy full) and Refill health (healed to the
	 * max); again when full: nothing called */
	{
		int32_t an = 2, amx = 8;
		float e = 10, emx = 100, hmx = 300;
		memset(g_prf_res, 0, sizeof g_prf_res), memset(g_prf_calls, 0, sizeof g_prf_calls);
		memcpy(g_prf_res + BO_RES_COUNT, &an, 4), memcpy(g_prf_res + PRF_RES_MAX, &amx, 4);
		memcpy(g_prf_res + PRF_RES_ENERGY, &e, 4), memcpy(g_prf_res + PRF_RES_EMAX, &emx, 4);
		uintptr_t rp = (uintptr_t)g_prf_res, rp0;
		memcpy(&rp0, (char *)&g_pcs + BO_SC_RES, 8), memcpy((char *)&g_pcs + BO_SC_RES, &rp, 8);
		memcpy((char *)&g_prpc + PR_CH_HEALTH_MAX, &hmx, 4);
		g_prpc.health = 300;
		EXPECT(!strcmp(pr_word("hurt 120"), "practice hurt"), "the word: hurt");
		pr_step(1);
		EXPECT(!strcmp(pr_word("focus 5"), "practice focus"), "the word: focus");
		pr_step(1);
		memcpy(&e, g_prf_res + PRF_RES_ENERGY, 4);
		EXPECT(g_prpc.health == 120 && e == 5 && log_has("Talion's health 300.0 -> 120.0 of 300.0 (a test aid") &&
			       log_has("the Focus 10.0 -> 5.0 of 100.0 (a test aid"),
		       "the test aids: health %g focus %g", (double)g_prpc.health, (double)e);
		atomic_store(&g_vmo.practice, somvr_vm_pr_pack(3, SOMVR_VMPR_ARROWS, 0));
		pr_step(1);
		memcpy(&an, g_prf_res + BO_RES_COUNT, 4), memcpy(&e, g_prf_res + PRF_RES_ENERGY, 4);
		EXPECT(an == 8 && e == 100 && g_prf_calls[PRF_TOKENS_ADD] == 1 && g_prf_calls[PRF_ENERGY_FULL] == 1 &&
			       log_has("refill arrows (the menu): the arrows 2 -> 8 of 8 (FUN_02720a90 +6), the Focus 5.0 -> 100.0 of "
				       "100.0 (FUN_0271e2e0); no save"),
		       "Refill arrows + focus: arrows %d focus %g", an, (double)e);
		atomic_store(&g_vmo.practice, somvr_vm_pr_pack(4, SOMVR_VMPR_HEALTH, 0));
		pr_step(1);
		EXPECT(g_prpc.health == 300 && g_prf_calls[PRF_HEAL] == 1 &&
			       log_has("refill health (the menu): Talion's health 120.0 -> 300.0 of 300.0 (FUN_02763910 healed); no "
				       "save"),
		       "Refill health: %g", (double)g_prpc.health);
		pr_word("refill arrows"), pr_step(1);
		pr_word("refill health"), pr_step(1);
		EXPECT(g_prf_calls[PRF_TOKENS_ADD] == 1 && g_prf_calls[PRF_ENERGY_FULL] == 1 && g_prf_calls[PRF_HEAL] == 1 &&
			       log_has("the arrows 8 -> 8 of 8 (full) (FUN_02720a90 +0), the Focus 100.0 -> 100.0 of 100.0 (full)") &&
			       log_has("Talion's health 300.0 -> 300.0 of 300.0 (full)") && g_pr.fill_arrows == 2 &&
			       g_pr.fill_health == 2,
		       "full: nothing called (%d %d %d)", g_prf_calls[0], g_prf_calls[1], g_prf_calls[2]);
		memcpy((char *)&g_pcs + BO_SC_RES, &rp0, 8);
	}
	/* 10. back (the menu): our orc removed, the light given back with the teleport, the min health restored at the end */
	pr_word("spawn"), pr_step(1);
	int nc = g_pr_ncmd;
	atomic_store(&g_vmo.practice, somvr_vm_pr_pack(5, SOMVR_VMPR_BACK, 0));
	pr_step(1);
	EXPECT(!g_pr.orc_h && !g_pr.here && !g_pr.went && g_pr.trip_kind == 1 && log_has("remove the Berserker orc (going back)"),
	       "back starts: orc %#lx here %d", (unsigned long)g_pr.orc_h, g_pr.here);
	EXPECT(!g_pr.quiet && g_prq_mgr.flag == 1 && g_prq_en == 2 && log_has("quiet ends (going back): the spawn manager on again"),
	       "back: quiet %d flag %u enable %d", g_pr.quiet, g_prq_mgr.flag, g_prq_en);
	pr_step(30);
	EXPECT(g_pr_ncmd == nc + 3 && !strcmp(g_pr_cmds[nc], "GameServerShell.AdvanceTimeOfDay") &&
		       !strcmp(g_pr_cmds[nc + 1], "GameServerShell.ChangeWeather SON_Night_01") &&
		       !strcmp(g_pr_cmds[nc + 2], "Teleport 35400 5402 -22400") && g_pobj.pos[1] == 5402 && g_prw.tod == 1 &&
		       g_prw.rec == g_prwrec[0] && pr_mh_now() == 20,
	       "the trip back's commands: %d ('%s' '%s' '%s')", g_pr_ncmd - nc, g_pr_cmds[nc], g_pr_cmds[nc + 1], g_pr_cmds[nc + 2]);
	pr_step(250);
	EXPECT(g_pr.trip.phase == SOMVR_PRT_IDLE && !g_pr.have_back && pr_mh_now() == 0 && g_pr.mh_restores >= 2 &&
		       atomic_load(&g_vmo.pstate) == 0 && log_has("the blink back ended"),
	       "home: phase %d back %d min %g pstate %#x", g_pr.trip.phase, g_pr.have_back, (double)pr_mh_now(),
	       atomic_load(&g_vmo.pstate));
	pr_word("back"), pr_step(1);
	EXPECT(log_has("back (the trigger word) refused: no place to go back to"), "back again: refused");
	pr_word("health"), pr_step(1); /* build 17: the refills only in the area */
	EXPECT(log_has("refill health (the trigger word): not in the practice area: nothing") && g_pr.fill_refused == 1 &&
		       g_pr.fill_health == 2,
	       "a refill outside the area: refused");
	/* 11. a teleport that never lands: three of them, then the fade-in where he is (a WARN); the place kept */
	g_pr_tp_block = 1;
	int tp0n = (int)g_pr.teleports;
	pr_word("go"), pr_step(1);
	pr_step(90 * 10);
	EXPECT(g_pr.teleports == (uint64_t)tp0n + 3 && g_pr.failed == 1 && !g_pr.went && g_pr.have_back &&
		       g_pr.trip.phase == SOMVR_PRT_IDLE && log_has("WARN PRACTICE t=") && pr_mh_now() == 0,
	       "no landing: teleports %llu failed %llu", (unsigned long long)g_pr.teleports, (unsigned long long)g_pr.failed);
	g_pr_tp_block = 0;
	/* 11b. a wet day: the ChangeWeather goes with the teleport, the black holds for its pause (none comes here: 3.6 s
	 * after the ChangeWeather), then back; a dry day: no ChangeWeather at all */
	g_prw.rec = g_prwrec[3], g_prw.tod = 0;
	int wn0 = g_pr_ncmd;
	uint64_t pw0 = g_pr.pw_waits, none0 = g_pr.pw_none;
	pr_word("go"), pr_step(1);
	pr_step(30); /* the teleport at black */
	EXPECT(g_pr_ncmd == wn0 + 2 && !strcmp(g_pr_cmds[wn0], "GameServerShell.ChangeWeather DayNoRain") &&
		       !strcmp(g_pr_cmds[wn0 + 1], "Teleport 40400 3900 -21600") && g_prw.rec == g_prwrec[2] &&
		       g_pr.weather_set && g_pr.pw_waits == pw0 + 1 && !strcmp(g_pr.orig_weather, "SON_Day03_Rain"),
	       "wet: the weather with the teleport (%d commands, '%s')", g_pr_ncmd - wn0, g_pr_cmds[g_pr_ncmd ? g_pr_ncmd - 1 : 0]);
	pr_step(90 * 2); /* the settle, the 1 s hold and more: held */
	EXPECT(g_pr.went && g_pr.trip.phase == SOMVR_PRT_HOLD && log_has("(ours since the teleport; was 'SON_Day03_Rain')"),
	       "wet: held (phase %d)", g_pr.trip.phase);
	pr_step(90 * 2);
	EXPECT(g_pr.trip.phase == SOMVR_PRT_IDLE && g_pr.pw_none == none0 + 1 && g_pr.pw_t == 0 &&
		       log_has("no game pause within 3.6 s of our ChangeWeather") && log_has("longer (the game's pause over)"),
	       "wet: in after 3.6 s without a pause (phase %d none %llu)", g_pr.trip.phase, (unsigned long long)g_pr.pw_none);
	pr_word("back"), pr_step(90 * 3);
	EXPECT(g_pr.trip.phase == SOMVR_PRT_IDLE && g_prw.rec == g_prwrec[3] && !g_pr.went,
	       "wet: back with the rain (phase %d)", g_pr.trip.phase);
	g_prw.rec = g_prwrec[1];
	uint64_t dry0 = g_pr.pw_dry, ws0 = g_pr.weathers;
	pr_word("go"), pr_step(1), pr_step(90 * 3);
	EXPECT(g_pr.went && g_pr.trip.phase == SOMVR_PRT_IDLE && g_pr.pw_dry == dry0 + 1 && g_pr.weathers == ws0 &&
		       !g_pr.weather_set && log_has("a dry day, kept (no ChangeWeather"),
	       "dry: kept, no wait (phase %d dry %llu weathers %llu)", g_pr.trip.phase, (unsigned long long)g_pr.pw_dry,
	       (unsigned long long)g_pr.weathers);
	pr_word("back"), pr_step(90 * 3);
	g_prw.rec = g_prwrec[0], g_prw.tod = 1;
	/* 11c. SOMVR_PRACTICE_WAIT (the drill): "go" asked for in the pause waits (no black over the pause menu, the menu's
	 * buttons busy); a presentation after the pause restarts the 1 s; then the trip; "back" when the game is up: at once;
	 * "go" while Talion is dead, then "back" (the newest wins), then a load: dropped */
	g_pr.wait_mode = 1;
	g_pz[0x34] = 1; /* paused */
	pr_word("go"), pr_step(1);
	EXPECT(g_pr.trip.phase == SOMVR_PRT_IDLE && g_pr.pend_act == SOMVR_PRA_GO && g_pr.waits == 1 &&
		       (atomic_load(&g_vmo.pstate) & SOMVR_VMA_PR_BUSY) &&
		       log_has("go (the trigger word) waits for the game to be back (the game paused)"),
	       "wait: go in the pause: phase %d pending %d pstate %#x", g_pr.trip.phase, g_pr.pend_act, atomic_load(&g_vmo.pstate));
	pr_step(90 * 3);
	EXPECT(g_pr.trip.phase == SOMVR_PRT_IDLE && g_pr.pend_act == SOMVR_PRA_GO, "wait: still paused: phase %d",
	       g_pr.trip.phase);
	g_pz[0x34] = 0;
	pr_step(45);
	EXPECT(g_pr.trip.phase == SOMVR_PRT_IDLE && g_pr.pend_act, "wait: 0.5 s after the pause: phase %d", g_pr.trip.phase);
	*(uintptr_t *)(g_fbpres + FB_PRES_CUR) = 1; /* a presentation (the respawn's) */
	pr_step(90);
	EXPECT(g_pr.trip.phase == SOMVR_PRT_IDLE && g_pr.pend_act, "wait: in a presentation: phase %d", g_pr.trip.phase);
	*(uintptr_t *)(g_fbpres + FB_PRES_CUR) = 0;
	pr_step(80);
	EXPECT(g_pr.trip.phase == SOMVR_PRT_IDLE && g_pr.pend_act, "wait: 0.9 s after it: phase %d", g_pr.trip.phase);
	pr_step(12);
	EXPECT(g_pr.trip.phase != SOMVR_PRT_IDLE && !g_pr.pend_act && g_pr.wait_starts == 1 && g_pr.trip_kind == PR_TRIP_GO &&
		       log_has("the waiting go (the trigger word): the game is back (waited"),
	       "wait: the trip after 1 s back: phase %d starts %llu", g_pr.trip.phase, (unsigned long long)g_pr.wait_starts);
	pr_step(90 * 8); /* (the night made day: the load hold waits up to 3.6 s for a pause after our ChangeWeather) */
	EXPECT(g_pr.went && g_pr.trip.phase == SOMVR_PRT_IDLE, "wait: arrived (phase %d)", g_pr.trip.phase);
	pr_word("back"), pr_step(1);
	EXPECT(g_pr.trip.phase != SOMVR_PRT_IDLE && g_pr.trip_kind == PR_TRIP_BACK && g_pr.waits == 1,
	       "wait: back with the game up: at once (phase %d)", g_pr.trip.phase);
	pr_step(90 * 8);
	g_fbP[FB_P_LIFE] = 3; /* dead */
	pr_word("go"), pr_step(1);
	pr_word("back"), pr_step(1);
	EXPECT(g_pr.trip.phase == SOMVR_PRT_IDLE && g_pr.pend_act == SOMVR_PRA_BACK && g_pr.waits == 3 &&
		       log_has(", Talion not up)") && log_has("it replaces the waiting one"),
	       "wait: dead, go then back: pending %d waits %llu", g_pr.pend_act, (unsigned long long)g_pr.waits);
	g_fbP[FB_P_LIFE] = 2;
	pr_new_player((uintptr_t)g_fbP, g_pr_test_t); /* a load meanwhile (the same stand-in Player) */
	EXPECT(!g_pr.pend_act && g_pr.wait_dropped == 1 && log_has("the waiting back (the trigger word) dropped: a new player"),
	       "wait: a load drops the waiting back (%llu)", (unsigned long long)g_pr.wait_dropped);
	g_pr.wait_mode = 0;
	/* 12. auto: once, 5 s after the player is in the world and not paused */
	g_pr.mode = SOMVR_PR_AUTO, g_pr.auto_done = 0, g_pr.world_t = 0, g_pr.have_back = 0;
	g_pz[0x34] = 1; /* paused */
	pr_step(90 * 6);
	EXPECT(g_pr.autos == 0 && g_pr.trip.phase == SOMVR_PRT_IDLE, "auto while paused");
	g_pz[0x34] = 0;
	pr_step(90 * 4);
	EXPECT(g_pr.autos == 0, "auto before 5 s");
	pr_step(90 * 2);
	EXPECT(g_pr.autos == 1 && g_pr.trip.phase != SOMVR_PRT_IDLE && log_has("go to the practice area (auto)"),
	       "auto: %llu", (unsigned long long)g_pr.autos);
	pr_step(90 * 3);
	EXPECT(g_pr.here && g_pr.autos == 1, "auto arrived once");
	/* 12b. auto arrived: quiet again; then the load below: a new manager object, left as the load made it */
	EXPECT(g_pr.quiet && g_prq_mgr.flag == 0, "auto: quiet %d flag %u", g_pr.quiet, g_prq_mgr.flag);
	g_prq_mgr2 = g_prq_mgr, g_prq_mgr2.flag = 0, g_prq_mgr_ptr = &g_prq_mgr2;
	int en0 = g_prq_en;
	/* 13. a new Player (a load): the area, the trip and our orc forgotten; nothing written to the old character */
	static _Alignas(16) char P2[0x800];
	memcpy(P2, g_fbP, sizeof P2);
	g_ppm.owner = P2;
	g_pr_test_t += 0.02;
	pr_frame((uintptr_t)P2);
	EXPECT(!g_pr.here && !g_pr.went && !g_pr.have_back && g_pr.pl == (uintptr_t)P2 && log_has("a new player"),
	       "a load: here %d", g_pr.here);
	EXPECT(!g_pr.quiet && g_prq_en == en0 && g_prq_mgr2.flag == 0 &&
		       log_has("quiet ends (a new player (a load)): the spawn manager is another one now (a load: left as it is)"),
	       "a load: quiet %d enables %d", g_pr.quiet, g_prq_en - en0);
	g_prq_mgr_ptr = &g_prq_mgr, g_prq_mgr.flag = 1;
	g_ppm.owner = g_fbP;
	g_pr_test_t += 0.02;
	pr_frame((uintptr_t)g_fbP);
	/* 14. a mismatched site turns its part off (the spawn: no orcs, the rest on); the console's: no practice at all */
	struct site_spec bad_s[PRS_N];
	memcpy(bad_s, g_prs, sizeof bad_s);
	bad_s[PRS_SPAWN].prologue_hex = "cc cc cc cc";
	int bad = 0;
	pr_start(bad_s, &bad);
	EXPECT(g_pr.on && !g_pr.ok_spawn && g_pr.ok_clear && g_pr.ok_mh &&
		       log_has("WARN SOMVR_PRACTICE: the spawn's sites did not all match: no orcs on demand"),
	       "a spawn site off: on %d spawn %d", g_pr.on, g_pr.ok_spawn);
	struct site_spec es0 = g_es_sites[ES_RUNSTRING], es_bad = es0;
	es_bad.prologue_hex = "cc cc cc cc";
	((struct site_spec *)g_es_sites)[ES_RUNSTRING] = es_bad;
	pr_start(g_prs, &bad);
	((struct site_spec *)g_es_sites)[ES_RUNSTRING] = es0;
	EXPECT(!g_pr.on && log_has("WARN SOMVR_PRACTICE: the console site ES_RUNSTRING did not match: no practice area"),
	       "no console: on %d", g_pr.on);
	struct site_spec q0 = g_prqs[PRQ_DISABLE];
	g_prqs[PRQ_DISABLE].prologue_hex = "cc cc cc cc";
	pr_start(g_prs, &bad);
	g_prqs[PRQ_DISABLE] = q0;
	EXPECT(g_pr.on && !g_pr.ok_quiet && g_pr.ok_clear && log_has("WARN SOMVR_PRACTICE: the spawn manager's sites (PRQ_*)"),
	       "a quiet site off: on %d quiet %d", g_pr.on, g_pr.ok_quiet);
	setenv("SOMVR_PRACTICE_QUIET", "0", 1);
	pr_start(g_prs, &bad);
	int dis0 = g_prq_dis;
	pr_quiet_tick(g_pr_test_t, "a test");
	EXPECT(g_pr.on && !g_pr.quiet_mode && !g_pr.quiet && g_prq_dis == dis0 && log_has("quiet off (SOMVR_PRACTICE_QUIET=0)"),
	       "SOMVR_PRACTICE_QUIET=0: mode %d calls %d", g_pr.quiet_mode, g_prq_dis - dis0);
	unsetenv("SOMVR_PRACTICE_QUIET");
	setenv("SOMVR_PRACTICE", "maybe", 1);
	pr_start(g_prs, &bad);
	EXPECT(!g_pr.want && log_has("WARN SOMVR_PRACTICE=maybe is not 0|1|auto: off"), "a bad switch");
	setenv("SOMVR_PRACTICE", "1", 1);
	pr_start(g_prs, &bad);
	g_pr.lines = 0;
	pr_end_log();
	EXPECT(g_pr.on && log_has("PRACTICE END on: frames"), "the END line");
	printf("practice: the blink there (one teleport at black, the settle, the night made day and DayNoRain under the black, the black held for the game's pause after it, "
	       "fade in), min health 20 and back, the keep-clear pass (armed within 60 m; never ours, slaves, the dead, the "
	       "mount; held in a presentation), one orc at a time (shield, again, berserker by the menu; down, removed; three "
	       "failures), walking out and in, back with the light, a teleport that never lands, SOMVR_PRACTICE_WAIT (a trip "
	       "asked for in the pause, a presentation or a death waits), auto once, a load, the sites; "
	       "quiet: the spawn manager off and its AI removed in the area (again after a script, held in a presentation), on "
	       "again walking out, going back; a load's new manager left alone; its sites, SOMVR_PRACTICE_QUIET=0\n");
	/* back as the earlier tests left it */
	g_pr.on = 0;
	g_pr_test_t = -1;
	g_rs_hook = NULL;
	g_cbg.vt_npc = vt_npc0, g_pcs.owner = owner0, g_pcs.hobj = hobj0, g_pobj = pobj0;
	memcpy(g_pcomb.band, bands0, sizeof bands0);
	fb_p(FB_P_MOUNT, 0);
	g_vm.on = vm_on0, g_fr.rec = rec0;
	atomic_store(&g_vmo.pstate, 0);
	g_ppm.owner = &g_player;
	g_pl = NULL;
}

/* ---- SOMVR_MUSIC / SOMVR_HINTS (somvr_gameopts_game.c, build 10, re-xr-pipeline.md 34): stand-ins of the sound
 * manager (its vtable's slot 22 = SetRTPC(name, value in xmm0), the holder whose vf_0x68 returns it), a GameOptions
 * object (music at +0x190), DBRecord_GetName and the HUD text manager's three delegates (slot 2 each) ------------ */
static void *volatile g_go_vt_snd[32], *volatile g_go_vt_holder[16], *volatile g_go_vt_dlg[3][4];
static struct { uintptr_t vt; uint8_t pad, ready; } g_go_snd;
static struct { uintptr_t vt; } g_go_holder;
static void *volatile g_go_holder_ptr = &g_go_holder;
static unsigned char g_go_opts[0x200];
static char g_go_last[32];
static float g_go_last_v;
static int g_go_rtpc_n, g_go_dlg_n[3];
static struct site_spec g_gos[GOS_N];
static char g_gohex[GOS_N][256];
__attribute__((noipa)) uint64_t st_go_rtpc(void *snd, const char *name, float v)
{
	g_go_rtpc_n++, g_go_last_v = v;
	snprintf(g_go_last, sizeof g_go_last, "%s%s", snd == &g_go_snd ? "" : "?", name ? name : "(null)");
	return 1;
}
__attribute__((noipa)) void *st_go_sndget(void *holder) { return holder == &g_go_holder ? (void *)&g_go_snd : NULL; }
__attribute__((noipa)) uintptr_t st_go_opts(void) { return (uintptr_t)g_go_opts; }
__attribute__((noipa)) void st_go_apply(void *o) { (void)o; }
__attribute__((noipa)) const char *st_go_recname(uintptr_t rec) { return rec ? (const char *)(rec + 0x20) : ""; }
__attribute__((noipa)) void st_go_dlg0(void *d, void *s, void *p) { (void)d, (void)s, (void)p, g_go_dlg_n[0]++; }
__attribute__((noipa)) void st_go_dlg1(void *d, void *s, void *p) { (void)d, (void)s, (void)p, g_go_dlg_n[1]++; }
__attribute__((noipa)) void st_go_dlg2(void *d, void *s, void *p) { (void)d, (void)s, (void)p, g_go_dlg_n[2]++; }
static void go_site(void *volatile *slot, void *fn, const char *name, int i, int kind)
{
	if (kind == K_VSLOT)
		*slot = fn;
	hex_of(kind == K_VSLOT ? (const unsigned char *)slot : fn, 8, g_gohex[i], 128);
	hex_of(fn, 4, g_gohex[i] + 128, 128);
	uintptr_t a = kind == K_VSLOT ? (uintptr_t)slot : (uintptr_t)fn;
	g_gos[i] = (struct site_spec){ name, kind, a, a, g_gohex[i], (uintptr_t)fn, g_gohex[i] + 128 };
}
static void go_stand_ins(void)
{
	go_site(&g_go_vt_snd[22], (void *)st_go_rtpc, "GO_RTPC", GOS_RTPC, K_VSLOT);
	go_site(NULL, (void *)st_go_opts, "GO_OPTIONS", GOS_OPTIONS, K_ENTRY);
	go_site(NULL, (void *)st_go_apply, "GO_APPLY", GOS_APPLY, K_ENTRY);
	go_site(&g_go_vt_dlg[0][2], (void *)st_go_dlg0, "GO_HINT", GOS_HINT, K_VSLOT);
	go_site(&g_go_vt_dlg[1][2], (void *)st_go_dlg1, "GO_TUTORIAL", GOS_TUTORIAL, K_VSLOT);
	go_site(&g_go_vt_dlg[2][2], (void *)st_go_dlg2, "GO_PROMPT", GOS_PROMPT, K_VSLOT);
	go_site(NULL, (void *)st_go_recname, "GO_RECNAME", GOS_RECNAME, K_ENTRY);
	g_go_snd.vt = (uintptr_t)g_go_vt_snd, g_go_snd.ready = 1;
	g_go_vt_holder[GO_SND_GET / 8] = (void *)st_go_sndget;
	g_go_holder.vt = (uintptr_t)g_go_vt_holder;
	g_gog.snd_pp = (uintptr_t)&g_go_holder_ptr, g_gog.opt_music = 0x190;
	float m = 0.8f;
	memcpy(g_go_opts + 0x190, &m, 4);
}
typedef uint64_t (*st_go_rtpc_fn)(void *snd, const char *name, float v);
typedef void (*st_go_dlg_fn)(void *d, void *s, void *p);
static void go_tests(void)
{
	/* somvr_start with neither switch: nothing patched, the menu's row not offered */
	EXPECT(!g_go.mu_on && !g_go.hi_ok[0] && !g_go.hi_ok[1] && !g_go.hi_ok[2] && go_vm_avail() == 0,
	       "gameopts at start without SOMVR_MUSIC / SOMVR_HINTS: music %d hints %d", g_go.mu_on, g_go.hi_ok[0]);
	go_stand_ins();
	setenv("SOMVR_MUSIC", "0.3", 1), setenv("SOMVR_HINTS", "off", 1);
	int bad = 0;
	go_start(g_gos, &bad);
	EXPECT(!bad && g_go.mu_on && g_go.hi_ok[0] && g_go.hi_ok[1] && g_go.hi_ok[2] && g_go.ok_name && g_go.ok_apply &&
		       g_go_vt_snd[22] == (void *)somvr_w_gortpc && g_go_vt_dlg[0][2] == (void *)somvr_w_gohint &&
		       g_go_vt_dlg[2][2] == (void *)somvr_w_goprompt && go_vm_avail() == SOMVR_VMA_MUSIC,
	       "gameopts on: music %d hints %d %d %d", g_go.mu_on, g_go.hi_ok[0], g_go.hi_ok[1], g_go.hi_ok[2]);
	somvr_settings s;
	somvr_set_default(&s);
	go_vm_settings(&s);
	EXPECT(NEAR(s.music, 0.3, 1e-6) && log_has("CONFIG music=0.30") && log_has("CONFIG hints=off"),
	       "the menu's start value %g, the CONFIG lines", s.music);
	/* the start's share before the game ever set it: GameOptions +0x190 (0.8) x 0.3 on the holder's sound manager */
	go_tick(100.0);
	EXPECT(g_go_rtpc_n == 1 && !strcmp(g_go_last, "options_music") && NEAR(g_go_last_v, 0.24, 1e-5) && !g_go.mu_dirty &&
		       log_has("from GameOptions +0x190"),
	       "the start's music: %d calls, %s %g", g_go_rtpc_n, g_go_last, g_go_last_v);
	go_tick(101.0); /* nothing more to do */
	EXPECT(g_go_rtpc_n == 1, "a second tick sent again");
	/* the game sets its music (the options menu's slider, a load's apply) through the slot: at the share */
	((st_go_rtpc_fn)g_go_vt_snd[22])(&g_go_snd, "options_music", 0.6f);
	EXPECT(g_go_rtpc_n == 2 && !strcmp(g_go_last, "options_music") && NEAR(g_go_last_v, 0.18, 1e-5),
	       "the game's 0.6: %s %g", g_go_last, g_go_last_v);
	/* the other options and any other RTPC pass unchanged */
	((st_go_rtpc_fn)g_go_vt_snd[22])(&g_go_snd, "options_sfx", 0.7f);
	EXPECT(!strcmp(g_go_last, "options_sfx") && NEAR(g_go_last_v, 0.7, 1e-6) && log_has("options_sfx 0.700: passed unchanged"),
	       "sfx: %s %g", g_go_last, g_go_last_v);
	((st_go_rtpc_fn)g_go_vt_snd[22])(&g_go_snd, "player_health", 0.25f);
	((st_go_rtpc_fn)g_go_vt_snd[22])(&g_go_snd, "options_musicx", 0.5f);
	((st_go_rtpc_fn)g_go_vt_snd[22])(&g_go_snd, NULL, 0.5f);
	EXPECT(g_go_rtpc_n == 6 && !strcmp(g_go_last, "(null)") && atomic_load(&g_go.n_music) == 1, "other RTPCs: %d calls",
	       g_go_rtpc_n);
	/* the VR menu's Music row: the game's last value (0.6) x the new share, at once on the game thread */
	s.music = 0.5f;
	EXPECT(strstr(go_vm_set(SOMVR_SK_MUSIC, &s), "at once") && !strcmp(go_vm_set(SOMVR_SK_HUD, &s), ""), "the menu's note");
	go_tick(102.0);
	EXPECT(g_go_rtpc_n == 7 && !strcmp(g_go_last, "options_music") && NEAR(g_go_last_v, 0.3, 1e-5) &&
		       log_has("from its last options_music"),
	       "the menu's 50 %%: %s %g", g_go_last, g_go_last_v);
	s.music = 0;
	go_vm_set(SOMVR_SK_MUSIC, &s), go_tick(103.0);
	EXPECT(NEAR(g_go_last_v, 0, 1e-6), "Off: %g", g_go_last_v);
	((st_go_rtpc_fn)g_go_vt_snd[22])(&g_go_snd, "options_music", 1.0f);
	EXPECT(NEAR(g_go_last_v, 0, 1e-6), "Off holds when the game sets 1.0: %g", g_go_last_v);
	s.music = 1;
	go_vm_set(SOMVR_SK_MUSIC, &s), go_tick(104.0);
	EXPECT(NEAR(g_go_last_v, 1.0, 1e-6), "100 %%: the game's own %g", g_go_last_v);
	/* the hints: a combat hint's and a tutorial's show dropped, their hide passed, a prompt passed; names logged */
	struct { uintptr_t rec; uint8_t show; } hp = { 0, 1 };
	struct { uint8_t show; uint8_t pad[7]; uintptr_t rec; } tp = { 1, { 0 }, 0 };
	static char rec[0x40];
	rec[1] = 2, snprintf(rec + 0x20, 0x20, "Hint_StrikeBasic");
	hp.rec = tp.rec = (uintptr_t)rec;
	((st_go_dlg_fn)g_go_vt_dlg[0][2])((void *)1, (void *)2, &hp);
	EXPECT(g_go_dlg_n[0] == 0 && atomic_load(&g_go.n_drop[GO_EV_HINT]) == 1 &&
		       log_has("HINTS combat hint #1") && log_has("show 'Hint_StrikeBasic'") && log_has(": dropped (SOMVR_HINTS=off"),
	       "the combat hint's show: %d calls", g_go_dlg_n[0]);
	hp.show = 0;
	((st_go_dlg_fn)g_go_vt_dlg[0][2])((void *)1, (void *)2, &hp);
	EXPECT(g_go_dlg_n[0] == 1, "the combat hint's hide passed: %d", g_go_dlg_n[0]);
	((st_go_dlg_fn)g_go_vt_dlg[1][2])((void *)1, (void *)2, &tp);
	((st_go_dlg_fn)g_go_vt_dlg[2][2])((void *)1, (void *)2, &tp);
	EXPECT(g_go_dlg_n[1] == 0 && g_go_dlg_n[2] == 1 && atomic_load(&g_go.n_drop[GO_EV_TUTORIAL]) == 1 &&
		       atomic_load(&g_go.n_drop[GO_EV_PROMPT]) == 0 && log_has("HINTS behaviour prompt #1"),
	       "tutorial %d prompt %d", g_go_dlg_n[1], g_go_dlg_n[2]);
	((st_go_dlg_fn)g_go_vt_dlg[0][2])((void *)1, (void *)2, NULL); /* unreadable params: passed on as they are */
	EXPECT(g_go_dlg_n[0] == 2, "NULL params passed: %d", g_go_dlg_n[0]);
	/* the words: "hints log" passes every show (logged), "gameopts" a state line */
	EXPECT(!strcmp(go_word("hints log"), "hints") && !strcmp(go_word("hints loud"), "hints (not game|log|off: ignored)"),
	       "hints words");
	hp.show = 1;
	((st_go_dlg_fn)g_go_vt_dlg[0][2])((void *)1, (void *)2, &hp);
	EXPECT(g_go_dlg_n[0] == 3 && log_has(": passed on"), "hints log: the show passed: %d", g_go_dlg_n[0]);
	EXPECT(!strcmp(go_word("gameopts"), "gameopts") && log_has("GAMEOPTS t=") && log_has("combat hints 4 (shown 2, dropped 1)"),
	       "the state line");
	go_end_log();
	EXPECT(log_has("GAMEOPTS END music on"), "the END line");
	/* a mismatched slot (the game's own value not ours): that part off, nothing written */
	go_stand_ins();
	g_gohex[GOS_RTPC][0] ^= 1; /* the slot's expected bytes differ */
	g_go_vt_dlg[0][2] = (void *)st_go_dlg0;
	go_start(g_gos, &bad);
	EXPECT(!g_go.mu_on && g_go_vt_snd[22] == (void *)st_go_rtpc && g_go.hi_ok[0] && log_has("GO_RTPC") &&
		       log_has("MISMATCH") && go_vm_avail() == 0,
	       "a bad GO_RTPC: music %d slot %p", g_go.mu_on, g_go_vt_snd[22]);
	/* SOMVR_VISION=hold: Y's press and release each tap 122 for two ticks (the flag unread: blind), 243 dropped; tap: the
	 * press alone */
	int ok_vis = g_step.ok_vis, inp_allow = g_inp.allow_cmd, nv = 0, n122[8] = { 0 }, n243 = 0;
	g_step.ok_vis = 0; /* st_vision -1 */
	static const int ys[8] = { 1, 1, 1, 1, 0, 0, 0, 0 };
	for (int pass = 0; pass < 2; pass++) {
		setenv("SOMVR_VISION", pass ? "tap" : "hold", 1);
		go_start(g_gos, &bad);
		memset(n122, 0, sizeof n122), n243 = 0;
		for (int k = 0; k < 8; k++) {
			sc_tick_out o;
			memset(&o, 0, sizeof o);
			o.live = 1, o.ctx = SC_CTX_WORLD; /* in the world Y is the command alone (no pad Y) */
			if (ys[k])
				o.cmd[o.ncmd++] = GO_CMD_VMT;
			g_inp.allow_cmd = 0;
			go_vision_tick(&o, 200.0 + 0.011 * k + pass);
			for (int i = 0; i < o.ncmd; i++)
				n122[k] += o.cmd[i] == GO_CMD_VISION && g_inp.allow_cmd == GO_CMD_VISION, n243 += o.cmd[i] == GO_CMD_VMT;
		}
		nv += !pass ? (n122[0] && n122[1] && !n122[2] && !n122[3] && n122[4] && n122[5] && !n122[6] && !n243)
			    : (n122[0] && n122[1] && !n122[2] && !n122[4] && !n122[5] && !n243);
	}
	EXPECT(nv == 2 && log_has("CONFIG vision=hold") && log_has("CONFIG vision=tap") &&
		       log_has("Y not held and the vision ours: off wished; a tap of 122 for 2 ticks (off, try 1) blind"),
	       "SOMVR_VISION: hold/tap taps %d %d %d %d %d %d, 243 %d", n122[0], n122[1], n122[2], n122[3], n122[4], n122[5], n243);
	/* build 16's test words: "vision y MS" = Y over the snapshot (its presses counter rising over the layer's, a kept
	 * snapshot not counted twice), "vision taps N MS" = raw taps; a state line */
	somvr_input_state ty_keep = g_inp.st;
	memset(&g_inp.st, 0, sizeof g_inp.st);
	g_inp.st.hand[SOMVR_HAND_L].presses[1] = 7;
	EXPECT(!strcmp(go_vision_word("vision y 50"), "vision y") && !strcmp(go_vision_word("vision y 0"), "vision y (bad: "
			"1..60000 ms: ignored)"), "vision y words");
	go_test_y(210.0);
	int ty1 = (g_inp.st.hand[SOMVR_HAND_L].buttons & SOMVR_BTN_UPPER) && g_inp.st.hand[SOMVR_HAND_L].presses[1] == 8;
	go_test_y(210.02); /* the read failed: the same (edited) snapshot kept */
	int ty2 = (g_inp.st.hand[SOMVR_HAND_L].buttons & SOMVR_BTN_UPPER) && g_inp.st.hand[SOMVR_HAND_L].presses[1] == 8;
	g_inp.st.hand[SOMVR_HAND_L].presses[1] = 7, g_inp.st.hand[SOMVR_HAND_L].buttons = 0; /* a fresh one from the layer */
	go_test_y(210.1);
	int ty3 = !(g_inp.st.hand[SOMVR_HAND_L].buttons & SOMVR_BTN_UPPER) && g_inp.st.hand[SOMVR_HAND_L].presses[1] == 8;
	EXPECT(ty1 && ty2 && ty3 && log_has("test: Y pressed for 50 ms"), "vision y: held %d kept %d after %d", ty1, ty2, ty3);
	g_inp.st = ty_keep;
	EXPECT(!strcmp(go_vision_word("vision taps 2 100"), "vision taps") && !strcmp(go_vision_word("vision"), "vision") &&
		       log_has("VISION t=") && log_has("state: tap"), "vision taps / state words");
	int rt = 0;
	for (int k = 0; k < 20; k++) {
		sc_tick_out o;
		memset(&o, 0, sizeof o);
		o.live = 1, o.ctx = SC_CTX_WORLD;
		go_vision_tick(&o, 211.0 + 0.011 * k);
		for (int i = 0; i < o.ncmd; i++)
			rt += o.cmd[i] == GO_CMD_VISION;
	}
	EXPECT(rt == 4 && log_has("test: a raw tap of 122"), "vision taps 2 100: %d ticks of 122 (4)", rt);
	g_step.ok_vis = ok_vis, g_inp.allow_cmd = inp_allow;
	unsetenv("SOMVR_VISION");
	/* SOMVR_LOAD_VOICES=off (build 15b): options_dialog 0 through a load on flat frames (the state 3/0/paused with no
	 * stereo frame for LV_STILL_S), the game's own value back after; the pause menu (stereo frames) is no load; the
	 * game's set during a load held at 0; music passes unchanged without SOMVR_MUSIC */
	go_stand_ins();
	unsetenv("SOMVR_MUSIC"), unsetenv("SOMVR_HINTS");
	setenv("SOMVR_LOAD_VOICES", "off", 1);
	go_start(g_gos, &bad);
	EXPECT(!bad && g_go.lv_on && !g_go.mu_on && g_go_vt_snd[22] == (void *)somvr_w_gortpc && go_vm_avail() == 0 &&
		       log_has("CONFIG load_voices=off") && log_has(": on"),
	       "load voices on: %d (music %d)", g_go.lv_on, g_go.mu_on);
	int lv_in = g_inp.on, lv_ifs = g_inp.ifs, lv_menus = g_inp.menus, lv_paused = g_inp.paused;
	uint64_t lv_ns = atomic_load(&g_sh.n_stereo);
	g_inp.on = 1, g_inp.ifs = 3, g_inp.menus = 0, g_inp.paused = 1;
	int lv_n0 = g_go_rtpc_n;
	go_tick(300.0), go_tick(300.5); /* a load before the game's value was seen: nothing sent */
	EXPECT(g_go_rtpc_n == lv_n0 && !atomic_load(&g_go.lv_muted) && log_has("options_dialog was not seen yet"),
	       "a load, the value unknown: %d sent", g_go_rtpc_n - lv_n0);
	g_inp.paused = 0, go_tick(301.0);
	((st_go_rtpc_fn)g_go_vt_snd[22])(&g_go_snd, "options_dialog", 0.9f); /* the game's start */
	EXPECT(!strcmp(g_go_last, "options_dialog") && NEAR(g_go_last_v, 0.9, 1e-6), "the game's dialogue: %g", g_go_last_v);
	((st_go_rtpc_fn)g_go_vt_snd[22])(&g_go_snd, "options_music", 0.6f);
	EXPECT(!strcmp(g_go_last, "options_music") && NEAR(g_go_last_v, 0.6, 1e-6), "music unchanged: %g", g_go_last_v);
	g_inp.paused = 1, lv_n0 = g_go_rtpc_n;
	for (int k = 0; k < 10; k++) /* the pause menu: its state, stereo frames going on */
		atomic_fetch_add(&g_sh.n_stereo, 1), go_tick(302.0 + 0.1 * k);
	go_tick(303.0), go_tick(303.1);
	EXPECT(g_go_rtpc_n == lv_n0 && !atomic_load(&g_go.lv_muted), "the pause menu or under 0.25 s muted: %d",
	       g_go_rtpc_n - lv_n0);
	go_tick(303.2);
	EXPECT(g_go_rtpc_n == lv_n0 + 1 && atomic_load(&g_go.lv_muted) && !strcmp(g_go_last, "options_dialog") &&
		       NEAR(g_go_last_v, 0, 1e-6) && log_has("LOADVOICES t=303.200 a load") && log_has("(the game's 0.900"),
	       "the load's mute: %s %g", g_go_last, g_go_last_v);
	go_tick(304.0);
	EXPECT(g_go_rtpc_n == lv_n0 + 1, "the mute sent again");
	((st_go_rtpc_fn)g_go_vt_snd[22])(&g_go_snd, "options_dialog", 0.8f); /* the game's apply during the load */
	EXPECT(NEAR(g_go_last_v, 0, 1e-6) && atomic_load(&g_go.n_lv_held) == 1 && log_has("during a load: 0 sent"),
	       "the game's set held: %g", g_go_last_v);
	g_inp.paused = 0, go_tick(305.0); /* the world's state: the end */
	EXPECT(!atomic_load(&g_go.lv_muted) && !strcmp(g_go_last, "options_dialog") && NEAR(g_go_last_v, 0.8, 1e-6) &&
		       log_has("(its state left): options_dialog 0.800 sent back"),
	       "back after the load: %s %g", g_go_last, g_go_last_v);
	g_inp.paused = 1, go_tick(306.0); /* a second load, ended by stereo frames in the same state */
	EXPECT(atomic_load(&g_go.lv_muted) && NEAR(g_go_last_v, 0, 1e-6), "the second load: %g", g_go_last_v);
	atomic_fetch_add(&g_sh.n_stereo, 1), go_tick(306.4);
	EXPECT(!atomic_load(&g_go.lv_muted) && NEAR(g_go_last_v, 0.8, 1e-6) && log_has("(stereo frames again)"),
	       "the second load's end: %g", g_go_last_v);
	EXPECT(!strcmp(go_word("gameopts"), "gameopts") && log_has("load voices off (loads muted 2, not known 1, muted now 0"),
	       "the state line");
	go_end_log();
	EXPECT(log_has("LOADVOICES END loads muted 2") && log_has("not muted at the end"), "the END line");
	g_inp.on = lv_in, g_inp.ifs = lv_ifs, g_inp.menus = lv_menus, g_inp.paused = lv_paused;
	atomic_store(&g_sh.n_stereo, lv_ns);
	unsetenv("SOMVR_LOAD_VOICES");
	/* back to nothing for the rest (the stand-in slots restored first) */
	go_stand_ins();
	unsetenv("SOMVR_MUSIC"), unsetenv("SOMVR_HINTS");
	go_start(g_gos, &bad);
	EXPECT(!g_go.mu_on && !g_go.hi_ok[0] && g_go_vt_snd[22] == (void *)st_go_rtpc, "off again");
}

/* SOMVR_PAD_PROMPTS (the bow's round 2): the HUD prompt delegates' three slots against stand-ins */
static void *volatile g_pp_vt[PPS_N][4];
static int g_pp_n[PPS_N], g_pp_act[PPS_N];
static struct site_spec g_pps[PPS_N];
static char g_pphex[PPS_N][256];
static void pp_rec(int k, void *p)
{
	int32_t a = -1;
	if (p)
		memcpy(&a, (char *)p + 8, 4);
	g_pp_n[k]++, g_pp_act[k] = a;
}
__attribute__((noipa)) void st_pp_show(void *d, void *s, void *p) { (void)d, (void)s, pp_rec(PPS_SHOW, p); }
__attribute__((noipa)) void st_pp_hide(void *d, void *s, void *p) { (void)d, (void)s, pp_rec(PPS_HIDE, p); }
__attribute__((noipa)) void st_pp_hold(void *d, void *s, void *p) { (void)d, (void)s, pp_rec(PPS_HOLD, p); }
static void pp_stand_ins(void)
{
	static void *const fns[PPS_N] = { (void *)st_pp_show, (void *)st_pp_hide, (void *)st_pp_hold };
	static const char *const names[PPS_N] = { "PP_SHOW", "PP_HIDE", "PP_HOLD" };
	for (int i = 0; i < PPS_N; i++) {
		g_pp_vt[i][2] = fns[i];
		hex_of((const unsigned char *)&g_pp_vt[i][2], 8, g_pphex[i], 128);
		hex_of(fns[i], 4, g_pphex[i] + 128, 128);
		uintptr_t a = (uintptr_t)&g_pp_vt[i][2];
		g_pps[i] = (struct site_spec){ names[i], K_VSLOT, a, a, g_pphex[i], (uintptr_t)fns[i], g_pphex[i] + 128 };
	}
}
/* build 20, SOMVR_SLOTS_GAMESOUND (somvr_gsound_game.c): gs_pre's choice on stand-in posts - Talion's listed sword
 * sounds posted by the game (BS_POST_B's callback) dropped while the slots step runs; ours (path A), an orc's draw, a
 * footstep, a stale or missing step, log and game passed; the cookie's source in the line */
static void gs_tests(void)
{
	static char un[] = "fol_ply_cbt_swd_unsheath", sh[] = "fol_ply_cbt_swd_sheath", orc[] = "fol_cmn_unsheathe",
		    twirl[] = "fol_ply_cbt_swd_twirl", step[] = "fol_ply_step_dirt";
	static uintptr_t ck[5];
	struct gs_post p;
	void *cb = (void *)(uintptr_t)GS_CB;
	EXPECT(!g_gs.patched && atomic_load(&g_gs.mode) == GS_GAME, "gamesound at start without the switch: %d", g_gs.patched);
	EXPECT(gs_pre(&p, un, 73, cb, NULL) == 0 && !p.log && !p.name[0], "gamesound game: not passed untouched");
	EXPECT(gs_parse_names(" a_b, c,") == 2 && !strcmp(g_gs.names[0], "a_b") && !strcmp(g_gs.names[1], "c"),
	       "gs_parse_names: %d '%s' '%s'", g_gs.nnames, g_gs.names[0], g_gs.names[1]);
	EXPECT(gs_parse_names(GS_NAMES_DEFAULT) == 3 && gs_listed(un) && gs_listed(sh) && gs_listed(twirl) && !gs_listed(orc),
	       "the default names: %d", g_gs.nnames);
	g_gs.patched = 1;
	struct sl_game g = { .own = 0x1000 };
	gs_frame(&g);
	atomic_store(&g_gs.mode, GS_DROP);
	ck[GS_CK_SRC / 8] = 0x2ba964b8;
	EXPECT(gs_pre(&p, un, 73, cb, ck) == 1 && p.listed == 3 && p.log && p.src == 0x2ba964b8,
	       "the game's draw sound on Talion not dropped: listed %d src %#lx", p.listed, (unsigned long)p.src);
	EXPECT(gs_pre(&p, sh, 73, cb, NULL) == 1 && p.listed == 3 && gs_pre(&p, twirl, 73, cb, NULL) == 1,
	       "the game's sheathe (its twirl, its sound) on Talion not dropped");
	EXPECT(gs_pre(&p, un, SL_SND_OBJ, NULL, NULL) == 0 && !p.log, "our own post (path A, no callback) touched");
	EXPECT(gs_pre(&p, orc, 91, cb, NULL) == 0 && !p.listed && !p.log, "an orc's draw: listed %d log %d", p.listed, p.log);
	EXPECT(gs_pre(&p, step, 73, cb, NULL) == 0 && !p.log, "a footstep");
	atomic_store(&g_gs.step_ms, (int64_t)(mono() * 1e3) - 2 * GS_FRESH_MS);
	EXPECT(gs_pre(&p, un, 73, cb, NULL) == 0 && p.listed == 2, "no slots step for 2 s: listed %d", p.listed);
	gs_frame(NULL);
	EXPECT(atomic_load(&g_gs.step_ms) == 0 && gs_pre(&p, sh, 73, cb, NULL) == 0 && p.listed == 2,
	       "no player read: passed as with no step (listed %d)", p.listed);
	gs_frame(&g);
	atomic_store(&g_gs.mode, GS_LOG);
	EXPECT(gs_pre(&p, un, 73, cb, NULL) == 0 && p.listed == 1 && p.log, "log: listed %d log %d", p.listed, p.log);
	EXPECT(gs_pre(&p, twirl, 73, cb, NULL) == 0 && p.log && p.listed == 1 && gs_pre(&p, orc, 91, cb, NULL) == 0 &&
		       p.log && !p.listed && gs_pre(&p, step, 73, cb, NULL) == 0 && !p.log,
	       "log: the listed and the other sword sounds logged, a footstep not");
	EXPECT(atomic_load(&g_gs.dropped) == 3 && atomic_load(&g_gs.stale) == 2 && atomic_load(&g_gs.swd) == 1 &&
		       atomic_load(&g_gs.named) == 7,
	       "counters: dropped %llu stale %llu swd %llu named %llu", (unsigned long long)atomic_load(&g_gs.dropped),
	       (unsigned long long)atomic_load(&g_gs.stale), (unsigned long long)atomic_load(&g_gs.swd),
	       (unsigned long long)atomic_load(&g_gs.named));
	EXPECT(!strcmp(gs_word("slsnd drop"), "slsnd") && atomic_load(&g_gs.mode) == GS_DROP, "the word slsnd drop");
	memset(&g_gs, 0, sizeof g_gs);
	EXPECT(!strncmp(gs_word("slsnd log"), "slsnd (the post is not patched", 30), "the word without the patch");
}

static void pp_tests(void)
{
	typedef void (*dlg_fn)(void *, void *, void *);
	EXPECT(!g_pp.ok[PPS_SHOW] && atomic_load(&g_pp.mode) == PP_GAME, "padprompts at start without the switch: %d",
	       g_pp.ok[PPS_SHOW]);
	/* the keep rule: a VR control sends it; never 121 while the bow is out */
	EXPECT(pp_vr_keep(34, 0, 0, 0) && pp_vr_keep(14, 0, 0, 0) && pp_vr_keep(95, 0, 0, 0) && pp_vr_keep(15, 0, 0, 0) &&
		       !pp_vr_keep(119, 1, 1, 1) && !pp_vr_keep(120, 1, 1, 0) && !pp_vr_keep(126, 1, 1, 0) &&
		       !pp_vr_keep(127, 1, 1, 0) && !pp_vr_keep(121, 0, 1, 0) && pp_vr_keep(121, 1, 1, 0) &&
		       !pp_vr_keep(121, 1, 1, 1) && pp_vr_keep(124, 0, 1, 1) && !pp_vr_keep(124, 0, 0, 0) &&
		       !pp_vr_keep(16, 0, 0, 0) && pp_vr_keep(16, 1, 0, 0) && pp_vr_keep(349, 0, 0, 0),
	       "pp_vr_keep");
	int f[PP_MAP_MAX], t[PP_MAP_MAX], n = -1;
	EXPECT(pp_parse_map("34:120,95:127", f, t, &n) && n == 2 && f[1] == 95 && t[1] == 127 && !pp_parse_map("34-120", f, t, &n) &&
		       !pp_parse_map("34:999", f, t, &n) && pp_parse_map("", f, t, &n) && n == 0,
	       "pp_parse_map %d", n);
	pp_stand_ins();
	setenv("SOMVR_PAD_PROMPTS", "vr", 1);
	int bad = 0, gm = g_inp.cfg.game_moves, bon = g_bow.on, bph = g_bow.s.phase;
	pp_start(g_pps, &bad);
	EXPECT(!bad && g_pp.ok[PPS_SHOW] && g_pp.ok[PPS_HIDE] && g_pp.ok[PPS_HOLD] && g_pp_vt[PPS_SHOW][2] == (void *)somvr_w_ppshow &&
		       g_pp_vt[PPS_HIDE][2] == (void *)somvr_w_pphide && atomic_load(&g_pp.nmap) == 2 &&
		       log_has("CONFIG pad_prompts=vr") && log_has("the relabel 34->120,95->127"),
	       "padprompts vr: %d %d %d", g_pp.ok[0], g_pp.ok[1], g_pp.ok[2]);
	struct { uintptr_t key; int32_t act; int32_t pad; const char *str, *icon; uint32_t cat; uint8_t fl[4]; } sp = {
		0x1234, 121, 0, "UI_ShadowStrike", "{ui_icon}", 2, { 0 } };
	struct { uintptr_t key; int32_t act; float fill; } hp = { 0x1234, 34, 0.5f };
	g_inp.cfg.game_moves = 0, g_bow.on = 1, g_bow.s.phase = SOMVR_BOW_PH_AIM;
	memset(g_pp_n, 0, sizeof g_pp_n);
	((dlg_fn)g_pp_vt[PPS_SHOW][2])((void *)1, (void *)2, &sp); /* Shadow Strike with the bow out: dropped */
	sp.act = 119;
	((dlg_fn)g_pp_vt[PPS_SHOW][2])((void *)1, (void *)2, &sp); /* Distract: dropped */
	sp.act = 127;
	((dlg_fn)g_pp_vt[PPS_SHOW][2])((void *)1, (void *)2, &sp); /* Focus (R3): dropped */
	EXPECT(g_pp_n[PPS_SHOW] == 0 && atomic_load(&g_pp.n_drop) == 3 && log_has("action 121 'CombatPrimary04' 'UI_ShadowStrike'") &&
		       log_has("dropped (no VR control sends it; the bow is out)"),
	       "the bow's prompts dropped: %d shows passed", g_pp_n[PPS_SHOW]);
	sp.act = 34; /* Activate (our B): shown as 120 (the pad's B), its hide and hold too; the params unchanged */
	((dlg_fn)g_pp_vt[PPS_SHOW][2])((void *)1, (void *)2, &sp);
	EXPECT(g_pp_n[PPS_SHOW] == 1 && g_pp_act[PPS_SHOW] == 120 && sp.act == 34 && log_has("shown as 120 'CombatPrimary03'"),
	       "Activate relabelled: %d %d", g_pp_n[PPS_SHOW], g_pp_act[PPS_SHOW]);
	((dlg_fn)g_pp_vt[PPS_HIDE][2])((void *)1, (void *)2, &sp);
	((dlg_fn)g_pp_vt[PPS_HOLD][2])((void *)1, (void *)2, &hp);
	EXPECT(g_pp_n[PPS_HIDE] == 1 && g_pp_act[PPS_HIDE] == 120 && g_pp_n[PPS_HOLD] == 1 && g_pp_act[PPS_HOLD] == 120,
	       "hide/hold as the shown command: %d %d", g_pp_act[PPS_HIDE], g_pp_act[PPS_HOLD]);
	sp.act = 119; /* a dropped show's hide: passed on its own command */
	((dlg_fn)g_pp_vt[PPS_HIDE][2])((void *)1, (void *)2, &sp);
	sp.act = 14;
	((dlg_fn)g_pp_vt[PPS_SHOW][2])((void *)1, (void *)2, &sp);
	EXPECT(g_pp_n[PPS_HIDE] == 2 && g_pp_act[PPS_HIDE] == 119 && g_pp_n[PPS_SHOW] == 2 && g_pp_act[PPS_SHOW] == 14,
	       "a hide passed, Menu passed: %d %d", g_pp_act[PPS_HIDE], g_pp_act[PPS_SHOW]);
	g_inp.cfg.game_moves = 1, sp.act = 121; /* the game's moves: A's evade prompt only without the bow */
	((dlg_fn)g_pp_vt[PPS_SHOW][2])((void *)1, (void *)2, &sp);
	g_bow.s.phase = SOMVR_BOW_PH_IDLE;
	((dlg_fn)g_pp_vt[PPS_SHOW][2])((void *)1, (void *)2, &sp);
	EXPECT(g_pp_n[PPS_SHOW] == 3 && g_pp_act[PPS_SHOW] == 121, "121 with the game's moves: %d", g_pp_n[PPS_SHOW]);
	((dlg_fn)g_pp_vt[PPS_SHOW][2])((void *)1, (void *)2, NULL); /* unreadable params: passed as they are */
	EXPECT(g_pp_n[PPS_SHOW] == 4, "NULL params passed: %d", g_pp_n[PPS_SHOW]);
	/* the words: log passes everything (no relabel), a map, the state line */
	EXPECT(!strcmp(pp_word(" log"), "padprompts") && !strcmp(pp_word(" loud"), "padprompts (not game|log|vr|map: ignored)"),
	       "padprompts words");
	sp.act = 119;
	((dlg_fn)g_pp_vt[PPS_SHOW][2])((void *)1, (void *)2, &sp);
	sp.act = 34;
	((dlg_fn)g_pp_vt[PPS_SHOW][2])((void *)1, (void *)2, &sp);
	EXPECT(g_pp_n[PPS_SHOW] == 6 && g_pp_act[PPS_SHOW] == 34, "log: passed unchanged: %d %d", g_pp_n[PPS_SHOW],
	       g_pp_act[PPS_SHOW]);
	EXPECT(!strcmp(pp_word(" map 121:120"), "padprompts map") && !strcmp(pp_word(" vr"), "padprompts") &&
		       atomic_load(&g_pp.nmap) == 1 && pp_mapped(121) == 120 && pp_mapped(34) == 34,
	       "the map word: %d", atomic_load(&g_pp.nmap));
	EXPECT(!strcmp(pp_word(""), "padprompts") && log_has("dropped by command: 119 CombatPrimary02 x1, 121 CombatPrimary04 x2, 127"),
	       "the state line");
	pp_end_log();
	EXPECT(log_has("PROMPTS END vr: shows"), "the END line");
	/* SOMVR_USE (the story drill): a pick prompt (123 'Combat_Activate_*') kept and shown as B (120), not '_Full'; B
	 * pressed while one is up holds 123 until B is let go; its hide carries 120; off = build 13 (dropped) */
	sp.act = 123, sp.str = "Combat_Activate_Flower", sp.key = 0x5150;
	((dlg_fn)g_pp_vt[PPS_SHOW][2])((void *)1, (void *)2, &sp);
	EXPECT(g_pp_n[PPS_SHOW] == 6 && log_has("'Combat_Activate_Flower' icon '{ui_icon}' category 2 key 0x5150: dropped"),
	       "use off: the pick prompt dropped (build 13): %d", g_pp_n[PPS_SHOW]);
	((dlg_fn)g_pp_vt[PPS_HIDE][2])((void *)1, (void *)2, &sp);
	setenv("SOMVR_USE", "1", 1);
	us_config(&bad);
	EXPECT(!bad && g_us.on && log_has("CONFIG use=1 (SOMVR_USE): B at a pick prompt"), "use on: %d", g_us.on);
	for (int k = 0; k < 3; k++) /* the HUD's three prompt objects: the same key three times */
		((dlg_fn)g_pp_vt[PPS_SHOW][2])((void *)1, (void *)2, &sp);
	sp.str = "Combat_Activate_Ammo_Full", sp.key = 0x5160;
	((dlg_fn)g_pp_vt[PPS_SHOW][2])((void *)1, (void *)2, &sp);
	EXPECT(g_pp_n[PPS_SHOW] == 9 && g_pp_act[PPS_SHOW] == 120 && g_us.nup == 1 && g_us.up[0].n == 3 && g_us.refused_full == 1 &&
		       log_has("USE t=") && log_has("pick prompt up: 'Combat_Activate_Flower'") &&
		       log_has("'Combat_Activate_Ammo_Full' icon '{ui_icon}' category 2 key 0x5160: dropped"),
	       "use: the flower kept as 120, the full ammo dropped: %d %d %d", g_pp_n[PPS_SHOW], g_pp_act[PPS_SHOW], g_us.nup);
	sc_tick_out uo;
	memset(&uo, 0, sizeof uo);
	uo.live = 1, uo.ctx = SC_CTX_WORLD, uo.cmd[uo.ncmd++] = 34;
	int uc[12], mok = g_inp.map_ok, un;
	g_inp.map_ok = 0; /* no binding table: the pick only */
	un = us_press_cmds(&uo, uc);
	EXPECT(un == 1 && uc[0] == 123 && g_us.picks == 1 && log_has("B at 'Combat_Activate_Flower': 123 'CombatSecondary02' held"),
	       "B at the flower: %d %d", un, un ? uc[0] : 0);
	sp.str = "Combat_Activate_Flower", sp.key = 0x5150;
	for (int k = 0; k < 3; k++)
		((dlg_fn)g_pp_vt[PPS_HIDE][2])((void *)1, (void *)2, &sp);
	un = us_press_cmds(&uo, uc); /* the prompt went down mid-press (the pick): 123 held while B is */
	EXPECT(un == 1 && g_pp_act[PPS_HIDE] == 120 && g_us.nup == 0 && log_has("pick prompt down: 'Combat_Activate_Flower'"),
	       "held through the hide, the hide as 120: %d %d %d", un, g_pp_act[PPS_HIDE], g_us.nup);
	uo.ncmd = 0;
	un = us_press_cmds(&uo, uc);
	uo.cmd[uo.ncmd++] = 34;
	int un2 = us_press_cmds(&uo, uc); /* B again with no prompt up: Activate only */
	EXPECT(un == 0 && un2 == 0 && g_us.pick_ticks == 2, "let go, then B without a prompt: %d %d", un, un2);
	uo.ctx = SC_CTX_MENU;
	EXPECT(us_press_cmds(&uo, uc) == 0, "nothing in a menu");
	/* the window's buttons: one held when the window opens (or closes) counts only after it is let go */
	const uint32_t pb = 1u << SC_PAD_B, pa = 1u << SC_PAD_A;
	g_us.fresh = 0, g_us.win_prev = 0, g_us.held = 0;
	EXPECT(us_btns(0, pb) == pb && us_btns(1, pb) == 0 && us_btns(1, pb) == 0 && us_btns(1, pb | pa) == pa &&
		       us_btns(1, 0) == 0 && us_btns(1, pb) == pb && us_btns(0, pb) == 0 && us_btns(0, 0) == 0 && us_btns(0, pb) == pb,
	       "the held buttons across the window's edges");
	g_us.fresh = 1;
	EXPECT(us_btns(0, pb) == 0 && us_btns(0, 0) == 0 && us_btns(0, pb) == pb, "back from a menu: B held counts after a release");
	EXPECT(!strcmp(us_word("off"), "use off") && !g_us.on && !strcmp(us_word("status"), "use status") &&
		       !strcmp(us_word("x"), "use (not on|off|status: ignored)"),
	       "use words");
	us_end_log();
	EXPECT(log_has("USE END on=0 pick prompts kept 1 (refused '_Full' 1), picks 1 (123 held 2 ticks)"), "the USE END line");
	g_inp.map_ok = mok;
	unsetenv("SOMVR_USE");
	us_config(&bad);
	/* a mismatched hide slot: the show is put back too (a show without its hide would leave prompts up) */
	pp_stand_ins();
	g_pphex[PPS_HIDE][0] ^= 1;
	pp_start(g_pps, &bad);
	EXPECT(!g_pp.ok[PPS_SHOW] && !g_pp.ok[PPS_HIDE] && g_pp_vt[PPS_SHOW][2] == (void *)st_pp_show &&
		       log_has("PP_SHOW without PP_HIDE: put back") && atomic_load(&g_pp.mode) == PP_GAME,
	       "a bad PP_HIDE: show %d slot %p", g_pp.ok[PPS_SHOW], g_pp_vt[PPS_SHOW][2]);
	/* the bow's strike guard: pad A and 121 out of the tick while the bow is out (and only then) */
	sc_tick_out o;
	memset(&o, 0, sizeof o);
	o.live = 1, o.ctx = SC_CTX_WORLD, o.pad[SC_PAD_A] = 1, o.pad_on = 1u << SC_PAD_A, o.cmd[o.ncmd++] = 34,
	o.cmd[o.ncmd++] = 121;
	atomic_store(&g_bow.strike_block, 1);
	g_bow.s.phase = SOMVR_BOW_PH_IDLE;
	bo_guard(&o);
	EXPECT(o.ncmd == 2 && o.pad_on, "the guard idle: %d", o.ncmd);
	g_bow.s.phase = SOMVR_BOW_PH_DRAW;
	bo_guard(&o);
	EXPECT(o.ncmd == 1 && o.cmd[0] == 34 && !o.pad_on && o.pad[SC_PAD_A] == 0 && log_has("strike guard: pad A taken out"),
	       "the guard with the bow out: %d %u", o.ncmd, o.pad_on);
	g_inp.cfg.game_moves = gm, g_bow.on = bon, g_bow.s.phase = bph;
	unsetenv("SOMVR_PAD_PROMPTS");
	pp_stand_ins();
	pp_start(g_pps, &bad);
	EXPECT(!g_pp.ok[PPS_SHOW] && g_pp_vt[PPS_SHOW][2] == (void *)st_pp_show, "off again");
}

/* SOMVR_BOW_HITLOG (the bow's round 2): the projectile hit call patched in a stand-in caller, our arrow's hit logged */
static int g_bh_n;
static uintptr_t g_bh_last;
__attribute__((noipa)) uint64_t t_bhhit(uintptr_t sh, uintptr_t hit, uintptr_t atk, uintptr_t rng, uintptr_t a5,
					  uintptr_t node, uint64_t inc, uint64_t crit)
{
	g_bh_n++, g_bh_last = sh ^ hit ^ atk ^ rng ^ a5 ^ node ^ inc ^ crit;
	return 7;
}
__attribute__((noipa)) uint64_t c_bhhit(uintptr_t sh, uintptr_t hit, uintptr_t atk, uintptr_t node)
{
	return t_bhhit(sh, hit, atk, 0x55, 0x66, node, 1, 0) + 1;
}
static void bh_tests(void)
{
	static struct site_spec s;
	static char hx[256];
	const unsigned char *p = (const unsigned char *)c_bhhit;
	int k = 0;
	for (; k < 96; k++) {
		int32_t rel;
		memcpy(&rel, p + k + 1, 4);
		if (p[k] == 0xe8 && (uintptr_t)(p + k + 5 + rel) == (uintptr_t)t_bhhit)
			break;
	}
	EXPECT(k < 96, "bowhit: the stand-in call");
	if (k >= 96)
		return;
	hex_of(p + k, 5, hx, 128), hex_of((const void *)t_bhhit, 4, hx + 128, 128);
	s = (struct site_spec){ "BOW_PJ_HIT", K_CALL, (uintptr_t)(p + k), (uintptr_t)(p + k), hx, (uintptr_t)t_bhhit, hx + 128 };
	g_bh_sites = &s;
	int bon = g_bow.on, bad = 0;
	uintptr_t fsc = g_bow.fire_sc, fatk = g_bow.atk;
	g_bow.on = 1;
	setenv("SOMVR_BOW_HITLOG", "1", 1);
	bh_start(&bad);
	static _Alignas(8) char atk[0x30], ovr[0x30], nodeh[0x30]; /* records: flags +1 bit 1, the name's pointer at +0x20 */
	static const char *const nm[3] = { "PC_Ranged_Ground_Normal_Shoot_Light", "PC_Ranged_HeadShot", "Head" };
	atk[1] = ovr[1] = nodeh[1] = 2;
	memcpy(atk + 0x20, &nm[0], 8), memcpy(ovr + 0x20, &nm[1], 8), memcpy(nodeh + 0x20, &nm[2], 8);
	struct { float pos[3], n[3]; float f; uint32_t pad; uintptr_t obj; uint16_t node; } hit = { { 1, 2, 3 }, { 0 }, 0, 0, 0xabc, 7 };
	g_bow.fire_sc = 0x1000, g_bow.atk = (uintptr_t)atk;
	uint64_t r1 = c_bhhit(0x1000, (uintptr_t)&hit, (uintptr_t)ovr, (uintptr_t)nodeh);
	uint64_t r2 = c_bhhit(0x2000, (uintptr_t)&hit, (uintptr_t)atk, 0); /* another shooter: passed, not logged */
	EXPECT(g_bh.on && r1 == 8 && r2 == 8 && g_bh_n == 2 && g_bh.ours == 1 && g_bh.overrides == 1 &&
		       log_has("the hit node record 'Head', the attack 'PC_Ranged_HeadShot' (an AttackOverrides match") &&
		       log_has("hit object 0xabc node 7"),
	       "bowhit: %d calls, ours %llu, overrides %llu", g_bh_n, (unsigned long long)g_bh.ours,
	       (unsigned long long)g_bh.overrides);
	bh_end_log();
	EXPECT(log_has("BOW HIT END calls 2, ours 1, with an override attack 1"), "bowhit END");
	unsetenv("SOMVR_BOW_HITLOG");
	g_bow.on = bon, g_bow.fire_sc = fsc, g_bow.atk = fatk;
	g_bh_sites = g_bh_sites_game;
	memset(&g_bh, 0, sizeof g_bh);
}

/* SOMVR_FRONT (build 14): the score's veto, the warning, the ready grant, the damage scale and the camera copy on
 * stand-ins (the wrappers called directly; the hooks' patching is bo_patch_call's, tested by the bow's) */
static float g_fft_score = 3.5f;
static int g_fft_scores, g_fft_grants, g_fft_dmg, g_fft_cam;
__attribute__((noipa)) static void t_ffscore(uintptr_t e, uintptr_t tgt, uintptr_t slot, uint64_t n, uint64_t ring)
{
	(void)tgt, (void)slot, (void)n, (void)ring;
	memcpy((void *)(e + FF_E_SCORE), &g_fft_score, 4);
	g_fft_scores++;
}
__attribute__((noipa)) static uint64_t t_ffgrant(uintptr_t e, uintptr_t now)
{
	uint32_t g = (uint32_t)now;
	memcpy((void *)(e + FF_E_GRANT), &g, 4);
	g_fft_grants++;
	return 5;
}
__attribute__((noipa)) static uint64_t t_ffdmg(uintptr_t ds, uintptr_t hobj)
{
	(void)ds, (void)hobj;
	return (uint64_t)++g_fft_dmg;
}
__attribute__((noipa)) static uint64_t t_ffcam(uintptr_t pl, uintptr_t cp)
{
	(void)cp;
	float game[16] = { 1, 2, 3, 0, 0, 0, 1, 1, 2, 3, 0, 0, 0, 1, 1.9f, 1.1f };
	memcpy((void *)(pl + FF_PL_COPY), game, sizeof game);
	return (uint64_t)++g_fft_cam;
}
/* SOMVR_PARRY / SOMVR_SHOVE (build 18): the window, the opening, the riposte's kind, the type byte for the call */
static void py_tests(void)
{
	static _Alignas(16) unsigned char orc[0x800], ch[0x400], pcb[0x800];
	memset(orc, 0, sizeof orc), memset(ch, 0, sizeof ch), memset(pcb, 0, sizeof pcb);
	uintptr_t c = (uintptr_t)orc, chp = (uintptr_t)ch;
	memcpy(orc + C_OWNER, &chp, sizeof chp); /* alive: an owner, no dead flags */
	double tt = g_cb_test_t;
	int nh = g_cb.nrec[SC_TIER_HEAVY];
	memset(&g_py, 0, sizeof g_py);
	g_py.on = 1, g_py.streak = 1, g_py.window_s = 0.25f, g_py.open_s = 1.0f, g_py.brk = 1;
	g_cb_test_t = 100;
	somvr_df_in in;
	memset(&in, 0, sizeof in);
	float pt[3] = { 1, 2, 3 };
	in.impact = 0.8f, in.ts = 0.4f; /* 400 ms before the impact: a block */
	py_parry(c, 0, &in, pt, 100);
	EXPECT(g_py.early == 1 && g_py.parries == 0 && !py_open_now(c, 100.1), "parry: an early contact is a block (early %llu)",
	       (unsigned long long)g_py.early);
	in.ts = 0.79f; /* 10 ms before it: the parry */
	py_parry(c, 0, &in, pt, 100);
	EXPECT(g_py.parries == 1 && py_open_now(c, 100.5) && py_open_now(c, 101.0) && !py_open_now(c, 101.01) &&
		       log_has("PARRY parry") && log_has("PARRY open"), "parry: the opening 100..101 s");
	g_cb.nrec[SC_TIER_HEAVY] = 1;
	EXPECT(py_hit_kind(c, SC_TIER_LIGHT, 100.4) == 1 && py_hit_kind(c, SC_TIER_HEAVY, 100.5) == 2 &&
		       py_hit_kind(c, SC_TIER_STAB, 100.6) == 2 && py_hit_kind(c, SC_TIER_HEAVY, 101.5) == 0 && g_py.ripostes == 2,
	       "parry: open = through, a committed cut or a stab the riposte, after it nothing (ripostes %llu)",
	       (unsigned long long)g_py.ripostes);
	EXPECT(py_rx_skip(c, 100.2) && !py_rx_skip(c, 102) && !py_rx_skip(c + 8, 100.2), "parry: no REACT roll while open");
	uintptr_t pc = (uintptr_t)pcb;
	pcb[RX_C_NOBLOCK] = 5;
	uint8_t was = 0;
	int set = py_through_set(pc, &was);
	EXPECT(set && was == 5 && pcb[RX_C_NOBLOCK] == PY_TYPE_THROUGH, "parry: Talion's +0x55b for the call (%u)", pcb[RX_C_NOBLOCK]);
	py_through_back(pc, set, was);
	EXPECT(pcb[RX_C_NOBLOCK] == 5, "parry: +0x55b back (%u)", pcb[RX_C_NOBLOCK]);
	g_py.dodge = 0, g_py.dodge_s = 1.5f;
	py_dodge(c, 0, 1, -1, 103);
	EXPECT(!py_open_now(c, 103.1), "parry: a dodge opens nothing with SOMVR_PARRY_DODGE off");
	g_py.dodge = 1;
	py_dodge(c, 0, 0, -1, 103); /* not applied (log mode) */
	EXPECT(!py_open_now(c, 103.1), "parry: an unapplied dodge opens nothing");
	py_dodge(c, 0, 1, 250, 103);
	EXPECT(py_open_now(c, 104.4) && !py_open_now(c, 104.6) && g_py.dodge_opens == 1 && log_has("PARRY dodge"),
	       "parry: a dodged blow opens its attacker for 1.5 s");
	g_py.on = 0;
	in.ts = 0.8f;
	py_parry(c, 0, &in, pt, 105);
	EXPECT(g_py.parries == 1 && !py_open_now(c, 105.1), "parry: off = nothing");
	g_cb.nrec[SC_TIER_HEAVY] = nh;
	memset(&g_py, 0, sizeof g_py);
	g_cb_test_t = tt;
	printf("parry  the window (an early contact a block, 10 ms before the impact a parry), the 1 s opening, through/riposte/none "
	       "by tier and time, no REACT roll while open, Talion's +0x55b for the call and back, a dodge's 1.5 s opening, off\n");
}

static void ff_tests(void)
{
	static struct site_spec ss[FFS_N];
	ss[FFS_SCORE].target = (uintptr_t)t_ffscore, ss[FFS_GRANT].target = (uintptr_t)t_ffgrant;
	ss[FFS_CAM_A].target = ss[FFS_CAM_B].target = (uintptr_t)t_ffcam, ss[FFS_DMG_ATK].target = (uintptr_t)t_ffdmg;
	static _Alignas(16) unsigned char ent[0x240], tgt[0x80], ch[0xd40], ds[0x60], pl[0xd20];
	static uintptr_t clk_p, clk_q[4];
	static uint32_t clk[8];
	clk_q[1] = (uintptr_t)clk - 0x14; /* *(*clock_pp + 8) + 0x14 = &clk[0]: now */
	clk_p = (uintptr_t)clk_q;
	struct site_spec const *s0 = g_ff_sites;
	uintptr_t cpp = g_ffg.clock_pp, ph0 = g_th.ph[0], ph1 = g_th.ph[1];
	double tt = g_cb_test_t;
	float edge0 = g_th.cfg.edge, view0 = g_th.cfg.view_deg;
	g_th.cfg.edge = 0.8f, g_th.cfg.view_deg = 110;
	g_ff_sites = ss, g_ffg.clock_pp = (uintptr_t)&clk_p;
	memset(&g_ff, 0, sizeof g_ff);
	g_ff.mode = FF_ON, g_ff.damage = 0.5f, g_ff.keep = 1000, g_ff.camcopy = 1, g_ff.fov_h = 110, g_ff.fov_v = 90;
	g_ff.once = 1, g_ff.onecue = 1;
	somvr_front_cfg_default(&g_ff.cfg);
	somvr_front_init(&g_ff.f);
	g_th.ph[0] = 0x7700, g_th.ph[1] = 0x7701;
	uintptr_t chp = (uintptr_t)ch, id = 0x5500;
	memcpy(ent + FF_E_ORC, &chp, 8), memcpy(ch + FF_O_HOBJ, &id, 8), memcpy(tgt + FF_T_HOBJ, &g_th.ph[0], 8);
	memcpy(ent + FF_E_TGT, &g_th.ph[0], 8);
	float head[3] = { 0, 170, 0 }, hq[4] = { 0, 0, 0, 1 }, front[3] = { 0, 100, 300 }, back[3] = { 0, 100, -300 };
	somvr_threat_frame e;
	int haps[2] = { 0, 0 };
	memset(&e, 0, sizeof e);
	g_cb_test_t = 500, clk[0] = 10000;
	ff_cues(head, hq, g_cb_test_t, &e, haps, 0, 0); /* the head */
	/* in front: unchanged, in view */
	memcpy(ch + FF_O_POS, front, 12);
	somvr_w_ffscore((uintptr_t)ent, (uintptr_t)tgt, 0x900, 1, 1);
	float sc = 0;
	memcpy(&sc, ent + FF_E_SCORE, 4);
	EXPECT(g_fft_scores == 1 && sc == 3.5f && g_ff.n.verdict[SOMVR_FR_IN_VIEW] == 1, "front: in view unchanged (%.2f)", sc);
	/* behind: vetoed; the frame's pass end starts its warning, the cue on both sides (straight behind) */
	memcpy(ch + FF_O_POS, back, 12);
	g_cb_test_t += 0.02, clk[0] += 20;
	somvr_w_ffscore((uintptr_t)ent, (uintptr_t)tgt, 0x900, 1, 1);
	memcpy(&sc, ent + FF_E_SCORE, 4);
	EXPECT(sc == -50.f && g_ff.n.verdict[SOMVR_FR_VETO] == 1 && log_has("FRONT veto t=") && log_has("score=3.50 -> -50"),
	       "front: behind vetoed (%.2f)", sc);
	g_cb_test_t += 0.01, clk[0] += 10;
	ff_cues(head, hq, g_cb_test_t, &e, haps, 0, 0);
	EXPECT(g_ff.f.warnings == 1 && log_has("FRONT warn t=") && log_has("THREAT pre orc=0x5500 (FRONT warning) cue=edge") &&
		       haps[0] == SOMVR_THH_ANNOUNCE && haps[1] == SOMVR_THH_ANNOUNCE && e.edge[0] > 0 && e.edge[1] > 0,
	       "front: the warning and its cue (haps %d %d, edge %.2f %.2f)", haps[0], haps[1], e.edge[0], e.edge[1]);
	/* ONECUE: THREAT's announce (pulse, sound, LATE) for the warned orc while its warning runs: skipped, not late */
	int snd = g_th.sound[0] != 0;
	somvr_th_pick pk = { .cue = SOMVR_THC_EDGE, .side = SOMVR_TH_BOTH, .haptic = SOMVR_THH_ANNOUNCE, .sound = 1, .late = 1,
			     .lead = 0.8f };
	ff_prewarned(0, id, g_cb_test_t, g_cb_test_t, &pk);
	EXPECT(!pk.haptic && pk.sound == !snd && !pk.late && log_has("THREAT quiet orc=0 haptic=announce") &&
		       g_ff.n.quiet_pulses == 1 && g_ff.n.prewarned == 1,
	       "front onecue: during the warning (haptic %d sound %d late %d)", pk.haptic, pk.sound, pk.late);
	/* still warning: vetoed; 1 s on: ready, the score stays; the grant: verdict ready, the warning used */
	g_cb_test_t += 0.5, clk[0] += 500;
	ff_cues(head, hq, g_cb_test_t, &e, haps, 0, 0);
	g_fft_score = 2.5f;
	somvr_w_ffscore((uintptr_t)ent, (uintptr_t)tgt, 0x900, 1, 1);
	memcpy(&sc, ent + FF_E_SCORE, 4);
	EXPECT(sc == -50.f && g_ff.n.verdict[SOMVR_FR_WAIT] == 1, "front: the warning runs: vetoed (%.2f)", sc);
	g_cb_test_t += 0.6, clk[0] += 600;
	ff_cues(head, hq, g_cb_test_t, &e, haps, 0, 0);
	somvr_w_ffscore((uintptr_t)ent, (uintptr_t)tgt, 0x901, 1, 1);
	memcpy(&sc, ent + FF_E_SCORE, 4);
	uint64_t r = somvr_w_ffgrant((uintptr_t)ent, clk[0]);
	EXPECT(sc == 2.5f && log_has("FRONT ready t=") && r == 5 && g_fft_grants == 1 &&
		       log_has("verdict=ready attacks=") && g_ff.f.used == 1 && g_ff.n.grant_verdict[SOMVR_FR_READY] == 1,
	       "front: ready, granted (%.2f, used %llu)", sc, (unsigned long long)g_ff.f.used);
	/* ONECUE after the warned grant: its edge held (0.8 x the edge, both sides: straight behind) until its attack; the
	 * attack starting 0.3 s after the grant: THREAT's urgent pulse skipped; one starting 3 s after: not the warned one */
	double tg = g_cb_test_t;
	g_cb_test_t += 0.01;
	memset(&e, 0, sizeof e), haps[0] = haps[1] = 0;
	ff_cues(head, hq, g_cb_test_t, &e, haps, 0, 0);
	EXPECT(NEAR(e.edge[0], 0.64, 1e-4) && NEAR(e.edge[1], 0.64, 1e-4) && !haps[0] && !haps[1] && g_ff.n.hold_frames == 1,
	       "front onecue: the edge held after the grant (%.2f %.2f)", e.edge[0], e.edge[1]);
	pk = (somvr_th_pick){ .cue = SOMVR_THC_EDGE, .side = SOMVR_TH_BOTH, .haptic = SOMVR_THH_URGENT, .lead = 0.35f };
	ff_prewarned(0, id, tg + 0.3, g_cb_test_t, &pk);
	EXPECT(!pk.haptic && g_ff.n.quiet_pulses == 2 && log_has("THREAT quiet orc=0 haptic=urgent"),
	       "front onecue: the warned attack's urgent pulse (%d)", pk.haptic);
	pk = (somvr_th_pick){ .cue = SOMVR_THC_EDGE, .side = SOMVR_TH_BOTH, .haptic = SOMVR_THH_ANNOUNCE, .late = 1 };
	ff_prewarned(0, id, tg + 3.0, g_cb_test_t, &pk);
	EXPECT(pk.haptic == SOMVR_THH_ANNOUNCE && pk.late && g_ff.n.quiet_pulses == 2,
	       "front onecue: a later attack is not the warned one (haptic %d late %d)", pk.haptic, pk.late);
	/* a stale head (no frame for 0.2 s): the game's own score */
	g_cb_test_t += 0.2, clk[0] += 200, g_fft_score = 1.5f;
	somvr_w_ffscore((uintptr_t)ent, (uintptr_t)tgt, 0x902, 1, 1);
	memcpy(&sc, ent + FF_E_SCORE, 4);
	EXPECT(sc == 1.5f && g_ff.n.stale == 1, "front: a stale head: the game's (%.2f)", sc);
	/* another target: untouched */
	ff_cues(head, hq, g_cb_test_t, &e, haps, 0, 0);
	uintptr_t other = 0x9999;
	memcpy(tgt + FF_T_HOBJ, &other, 8);
	somvr_w_ffscore((uintptr_t)ent, (uintptr_t)tgt, 0x903, 1, 1);
	memcpy(&sc, ent + FF_E_SCORE, 4);
	EXPECT(sc == 1.5f && g_ff.n.other_tgt == 1, "front: another target: the game's (%.2f)", sc);
	/* the damage: 20 at Talion -> 10; 2000 (a scripted kill) stays; at another object untouched */
	float a = 20.f;
	memcpy(ds + FF_DS_AMOUNT, &a, 4);
	uint64_t d1 = somvr_w_ffdmg((uintptr_t)ds, 0x7701);
	memcpy(&a, ds + FF_DS_AMOUNT, 4);
	float b = 2000.f;
	memcpy(ds + FF_DS_AMOUNT, &b, 4);
	somvr_w_ffdmg((uintptr_t)ds, 0x7700);
	memcpy(&b, ds + FF_DS_AMOUNT, 4);
	float c = 20.f;
	memcpy(ds + FF_DS_AMOUNT, &c, 4);
	somvr_w_ffdmg((uintptr_t)ds, 0x1234);
	memcpy(&c, ds + FF_DS_AMOUNT, 4);
	EXPECT(d1 == 1 && g_fft_dmg == 3 && a == 10.f && b == 2000.f && c == 20.f && g_ff.n.dmg_scaled == 1 &&
		       log_has("FRONT damage at Talion t=") && log_has("20.00 -> 10.00"),
	       "front: damage %.1f %.1f %.1f", a, b, c);
	/* the camera copy: the head's over the game's (fresh head), the game's kept with a stale one */
	uint64_t c1 = somvr_w_ffcam_b((uintptr_t)pl, 0x10);
	somvr_front_camcopy cc;
	memcpy(&cc, pl + FF_PL_COPY, sizeof cc);
	EXPECT(c1 == 1 && cc.pos[1] == 170.f && cc.pos2[1] == 170.f && cc.quat[3] == 1.f && fabsf(cc.fov[0] - 1.9199f) < 1e-3f &&
		       g_ff.n.cam_writes == 1 && log_has("FRONT camcopy t=") && log_has("ours written"),
	       "front: camcopy %.1f %.3f", cc.pos[1], cc.fov[0]);
	g_cb_test_t += 0.3;
	somvr_w_ffcam_a((uintptr_t)pl, 0x10);
	memcpy(&cc, pl + FF_PL_COPY, sizeof cc);
	EXPECT(cc.pos[1] == 2.f && g_ff.n.cam_skips == 1 && g_ff.n.cam[0] == 1 && g_ff.n.cam[1] == 1,
	       "front: camcopy, a stale head: the game's (%.1f)", cc.pos[1]);
	/* build 19, SOMVR_CROWD_PACE_MS: a grant holds the turn (the next candidate in view vetoed: CROWD wait); let go after
	 * start_s with no blow; then one grant a pass: a better second candidate of the same pass wins, the kept one's score
	 * written back to -50; a worse third one vetoed */
	static _Alignas(16) unsigned char ent2[0x240], ch2[0xd40];
	uintptr_t chp2 = (uintptr_t)ch2, id2 = 0x5600;
	memcpy(ent2 + FF_E_ORC, &chp2, 8), memcpy(ch2 + FF_O_HOBJ, &id2, 8), memcpy(ent2 + FF_E_TGT, &g_th.ph[0], 8);
	memcpy(tgt + FF_T_HOBJ, &g_th.ph[0], 8);
	memcpy(ch + FF_O_POS, front, 12), memcpy(ch2 + FF_O_POS, front, 12);
	g_ff.pace_on = 1;
	somvr_pace_cfg_default(&g_ff.pcfg);
	somvr_pace_init(&g_ff.pace);
	g_cb_test_t += 0.01, clk[0] += 10;
	ff_cues(head, hq, g_cb_test_t, &e, haps, 0, 0);
	g_fft_score = 4.0f;
	somvr_w_ffscore((uintptr_t)ent, (uintptr_t)tgt, 0x904, 1, 1);
	memcpy(&sc, ent + FF_E_SCORE, 4);
	somvr_w_ffgrant((uintptr_t)ent, clk[0]);
	g_cb_test_t += 0.05, clk[0] += 50;
	somvr_w_ffscore((uintptr_t)ent2, (uintptr_t)tgt, 0x905, 1, 1);
	float sc2 = 0;
	memcpy(&sc2, ent2 + FF_E_SCORE, 4);
	EXPECT(sc == 4.0f && sc2 == -50.f && g_ff.pace.grants == 1 && g_ff.n.verdict[SOMVR_FR_PACE] == 1 &&
		       log_has("CROWD grant t=") && log_has("CROWD wait t=") && log_has("another orc's grant holds"),
	       "crowd: the grant holds the turn (%.1f, %.1f)", sc, sc2);
	g_cb_test_t += 2.6, clk[0] += 2600;
	ff_cues(head, hq, g_cb_test_t, &e, haps, 0, 0);
	g_fft_score = 3.0f;
	somvr_w_ffscore((uintptr_t)ent, (uintptr_t)tgt, 0x906, 1, 1);
	g_fft_score = 5.0f;
	somvr_w_ffscore((uintptr_t)ent2, (uintptr_t)tgt, 0x906, 1, 1);
	memcpy(&sc, ent + FF_E_SCORE, 4), memcpy(&sc2, ent2 + FF_E_SCORE, 4);
	EXPECT(g_ff.pace.let_go == 1 && sc == -50.f && sc2 == 5.0f && g_ff.pn.pass_writes == 1 && log_has("(written back)"),
	       "crowd: let go after 2.5 s; one grant a pass: the better one kept (%.1f, %.1f)", sc, sc2);
	g_fft_score = 2.0f;
	somvr_w_ffscore((uintptr_t)ent, (uintptr_t)tgt, 0x906, 1, 1);
	memcpy(&sc, ent + FF_E_SCORE, 4);
	EXPECT(sc == -50.f && g_ff.n.verdict[SOMVR_FR_PASS] == 1 && log_has("kept in this pass (one grant a pass)"),
	       "crowd: a worse one in the same pass vetoed (%.1f)", sc);
	/* the frame: THREAT's track of orc 2 (a keyed blow, its impact 0.8 s; its Combat not readable here): its start on
	 * the timeline (no grant seen: unannounced), the others held meanwhile, then its impact (the first: no gap) */
	struct th_track tr0 = g_th.tr[0];
	memset(&g_th.tr[0], 0, sizeof g_th.tr[0]);
	g_th.tr[0].c = 0x4240, g_th.tr[0].hobj = id2, g_th.tr[0].serial = 777, g_th.tr[0].t0 = g_cb_test_t;
	g_th.tr[0].k.impact = 0.8f, g_th.tr[0].k.bpw = g_th.tr[0].k.epw = g_th.tr[0].k.bcol = -1.f;
	ff_cues(head, hq, g_cb_test_t, &e, haps, 0, 0);
	int held = !somvr_pace_may(&g_ff.pace, &g_ff.pcfg, id, g_cb_test_t + 0.1, NULL);
	EXPECT(g_ff.pace.starts == 1 && g_ff.pace.unannounced == 1 && held && log_has("CROWD start t=") &&
		       log_has("NO GRANT seen (unannounced)"),
	       "crowd: the track's start on the timeline, the others held (%d)", held);
	g_cb_test_t += 0.85, clk[0] += 850;
	ff_cues(head, hq, g_cb_test_t, &e, haps, 0, 0);
	EXPECT(g_ff.pace.impacts == 1 && log_has("CROWD impact t=") && log_has("(the first)") &&
		       somvr_pace_may(&g_ff.pace, &g_ff.pcfg, id, g_cb_test_t, NULL),
	       "crowd: its impact on the timeline, the turn free");
	/* build 19, SOMVR_CROWD_ANSWER_MS: orc 2's next blow (marked as a berserker's) lands and DEFENCE says it hit Talion:
	 * its turn holds on (the others wait: its answer to a cut starts in its own turn); its answer starts in the hold (no
	 * grant, no overlap); that one is dodged: the turn is free at once */
	g_ff.pcfg.answer_s = 3.0f;
	memset(&g_th.tr[0], 0, sizeof g_th.tr[0]);
	g_th.tr[0].c = 0x4240, g_th.tr[0].hobj = id2, g_th.tr[0].serial = 778, g_th.tr[0].t0 = g_cb_test_t;
	g_th.tr[0].k.impact = 0.8f, g_th.tr[0].k.bpw = g_th.tr[0].k.epw = g_th.tr[0].k.bcol = -1.f;
	ff_cues(head, hq, g_cb_test_t, &e, haps, 0, 0);
	int marked = somvr_pace_may_answer(&g_ff.pace, &g_ff.pcfg, id2);
	g_cb_test_t += 0.85, clk[0] += 850;
	ff_cues(head, hq, g_cb_test_t, &e, haps, 0, 0);
	int wait = somvr_pace_holding(&g_ff.pace, id2, NULL);
	ff_ans_verdict(0x4240, id2, 1, g_cb_test_t);
	EXPECT(marked && wait == 1 && somvr_pace_holding(&g_ff.pace, id2, NULL) == 2 && g_ff.pace.impacts == 2 &&
		       !somvr_pace_may(&g_ff.pace, &g_ff.pcfg, id, g_cb_test_t + 2.0, NULL) && log_has("CROWD answer-hold t=") &&
		       log_has("its blow hit Talion -> its turn holds while he recoils"),
	       "crowd: a berserker's blow that hit: its turn holds (%d)", wait);
	g_cb_test_t += 0.5, clk[0] += 500;
	memset(&g_th.tr[0], 0, sizeof g_th.tr[0]);
	g_th.tr[0].c = 0x4240, g_th.tr[0].hobj = id2, g_th.tr[0].serial = 779, g_th.tr[0].t0 = g_cb_test_t;
	g_th.tr[0].k.impact = 0.8f, g_th.tr[0].k.bpw = g_th.tr[0].k.epw = g_th.tr[0].k.bcol = -1.f;
	uint64_t ov0 = g_ff.pace.overlaps, un0 = g_ff.pace.unannounced;
	ff_cues(head, hq, g_cb_test_t, &e, haps, 0, 0);
	EXPECT(g_ff.pace.answers == 1 && g_ff.pace.overlaps == ov0 && g_ff.pace.unannounced == un0 &&
		       log_has("ITS ANSWER in its own held turn (no grant)") && log_has("; source: no hit of ours just before") &&
		       log_has("CROWD answer t=") && log_has("ms before its impact (from its start)"),
	       "crowd: its answer starts in its own turn (answers %llu)", (unsigned long long)g_ff.pace.answers);
	g_cb_test_t += 0.85, clk[0] += 850;
	ff_cues(head, hq, g_cb_test_t, &e, haps, 0, 0);
	ff_ans_verdict(0x4240, id2, 0, g_cb_test_t);
	EXPECT(!somvr_pace_holding(&g_ff.pace, id2, NULL) && somvr_pace_may(&g_ff.pace, &g_ff.pcfg, id, g_cb_test_t, NULL) &&
		       log_has("did not land (blocked, parried or dodged) -> no answer to fear: the turn is free"),
	       "crowd: its answer dodged: the turn free");
	g_th.tr[0] = tr0;
	ff_end_log();
	EXPECT(log_has("FRONT END judged") && log_has("grants at Talion 2 (in-view 1, ready 1") && log_has("CROWD END pace 700 ms") &&
		       log_has("CROWD END answers (SOMVR_CROWD_ANSWER_MS=3000): verdicts at Talion hit 1 / not 1; holds after a hit 1"),
	       "front: END");
	memset(&g_ff, 0, sizeof g_ff);
	g_ff_sites = s0, g_ffg.clock_pp = cpp, g_th.ph[0] = ph0, g_th.ph[1] = ph1, g_cb_test_t = tt;
	g_th.cfg.edge = edge0, g_th.cfg.view_deg = view0;
}

/* SOMVR_HUD_FX: CBaseUI::CreateUIFXGroup's thunk (mov 0x8(%rdi),%rdi; add $0x68,%rdi; jmp FUN_00cbbe60) */
uint64_t c_ufthunk(uintptr_t ui, uintptr_t desc);
__attribute__((noipa, used)) uint64_t st_ufcreate(uintptr_t mgr, uintptr_t desc);
__asm__(".text\n.p2align 4\n.globl c_ufthunk\n.type c_ufthunk,@function\nc_ufthunk:\n  mov 0x8(%rdi), %rdi\n"
	"  add $0x68, %rdi\n  .byte 0xe9\n  .long st_ufcreate - . - 4\n.size c_ufthunk, .-c_ufthunk\n");
static struct { int n; uintptr_t mgr, desc; } g_uft;
__attribute__((noipa, used)) uint64_t st_ufcreate(uintptr_t mgr, uintptr_t desc)
{
	g_uft.n++, g_uft.mgr = mgr, g_uft.desc = desc;
	return 0x101; /* al = 1: a group made */
}
static void uf_tests(void)
{
	static struct site_spec s;
	static char hx[256];
	const unsigned char *p = (const unsigned char *)c_ufthunk;
	int k = 0;
	for (; k < 32; k++) {
		int32_t rel;
		memcpy(&rel, p + k + 1, 4);
		if (p[k] == 0xe9 && (uintptr_t)(p + k + 5 + rel) == (uintptr_t)st_ufcreate)
			break;
	}
	EXPECT(k < 32, "hudfx: the stand-in jmp");
	if (k >= 32)
		return;
	hex_of(p + k, 5, hx, 128), hex_of((const void *)st_ufcreate, 4, hx + 128, 128);
	s = (struct site_spec){ "UF_CREATE", K_JMP, (uintptr_t)(p + k), (uintptr_t)(p + k), hx, (uintptr_t)st_ufcreate, hx + 128 };
	int bad = 0;
	setenv("SOMVR_HUD_FX", "hide", 1);
	uf_start(&s, &bad);
	static _Alignas(8) unsigned char impl[0x80], desc[0x20];
	uintptr_t ui[2] = { 0, (uintptr_t)impl };
	int32_t gid = 7;
	memcpy(desc + UF_DESC_GROUP, &gid, sizeof gid);
	uint64_t r0 = c_ufthunk((uintptr_t)ui, (uintptr_t)desc); /* no frame of ours yet: made */
	uf_frame(1);
	uint64_t r1 = c_ufthunk((uintptr_t)ui, (uintptr_t)desc); /* our eyes: not made */
	const char *w1 = uf_word("game");
	uint64_t r2 = c_ufthunk((uintptr_t)ui, (uintptr_t)desc); /* the word: made */
	const char *w2 = uf_word("hide");
	uf_frame(0);
	uint64_t r3 = c_ufthunk((uintptr_t)ui, (uintptr_t)desc); /* a flat frame: made */
	EXPECT(g_uf.installed && (r0 & 0xff) == 1 && (r1 & 0xff) == 0 && (r2 & 0xff) == 1 && (r3 & 0xff) == 1 && g_uft.n == 3 &&
		       g_uft.mgr == (uintptr_t)impl + 0x68 && g_uft.desc == (uintptr_t)desc && g_uf.hidden == 1 && g_uf.made == 3 &&
		       !strncmp(w1, "hudfx game", 10) && !strncmp(w2, "hudfx hide", 10) && log_has("HOOK UF_CREATE ok jmp") &&
		       log_has("HUDFX group 7 (call 2, manager") && log_has("not made (our eyes: hide)") &&
		       log_has("made (a flat frame)") && log_has("CONFIG hud_fx=hide"),
	       "hudfx: %d calls of the game's, hidden %llu, made %llu", g_uft.n, (unsigned long long)g_uf.hidden,
	       (unsigned long long)g_uf.made);
	uf_end_log();
	EXPECT(log_has("HUDFX END groups asked 4: not made in our eyes 1, made 3, the game's own 0 0; the last GROUPID 7; now hide"),
	       "hudfx END");
	unsetenv("SOMVR_HUD_FX");
	memset(&g_uf, 0, sizeof g_uf); /* (the stand-in stays patched; nothing calls it again) */
	g_uf_sites = g_uf_sites_game;
}

/* SOMVR_VERBS_GLOW: ClientFX's two create thunks (FUN_00d8c7f0: movzbl %cl,%ecx; movzbl %r8b,%r8d; jmp FUN_00d8bae0 -
 * FUN_00d8c810: the same with start 1, b 1, c 0 and its out/cs swapped) */
uint64_t c_vgthunk0(uintptr_t mgr, uintptr_t out, uintptr_t cs, uint64_t start, uint64_t b, uint64_t c);
uint64_t c_vgthunk1(uintptr_t mgr, uintptr_t cs, uintptr_t out);
__attribute__((noipa, used)) uint64_t st_vgcreate(uintptr_t mgr, uintptr_t out, uintptr_t cs, uint64_t start, uint64_t b,
						  uint64_t c);
__asm__(".text\n.p2align 4\n.globl c_vgthunk0\n.type c_vgthunk0,@function\nc_vgthunk0:\n  movzbl %cl, %ecx\n"
	"  movzbl %r8b, %r8d\n  .byte 0xe9\n  .long st_vgcreate - . - 4\n.size c_vgthunk0, .-c_vgthunk0\n"
	".p2align 4\n.globl c_vgthunk1\n.type c_vgthunk1,@function\nc_vgthunk1:\n  mov %rdx, %rax\n  mov %rsi, %rdx\n"
	"  xor %r9d, %r9d\n  mov $1, %ecx\n  mov %rax, %rsi\n  mov $1, %r8d\n  .byte 0xe9\n  .long st_vgcreate - . - 4\n"
	".size c_vgthunk1, .-c_vgthunk1\n");
static struct { int n; uintptr_t mgr, out, cs; uint64_t start, b, c; } g_vgt;
__attribute__((noipa, used)) uint64_t st_vgcreate(uintptr_t mgr, uintptr_t out, uintptr_t cs, uint64_t start, uint64_t b,
						  uint64_t c)
{
	g_vgt.n++, g_vgt.mgr = mgr, g_vgt.out = out, g_vgt.cs = cs, g_vgt.start = start, g_vgt.b = b, g_vgt.c = c;
	return 0x55; /* an FX made */
}
static int vg_standin(const void *fn, const char *name, struct site_spec *s, char *hx)
{
	const unsigned char *p = (const unsigned char *)fn;
	int k = 0;
	for (; k < 40; k++) {
		int32_t rel;
		memcpy(&rel, p + k + 1, 4);
		if (p[k] == 0xe9 && (uintptr_t)(p + k + 5 + rel) == (uintptr_t)st_vgcreate)
			break;
	}
	if (k >= 40)
		return 0;
	hex_of(p + k, 5, hx, 128), hex_of((const void *)st_vgcreate, 4, hx + 128, 128);
	*s = (struct site_spec){ name, K_JMP, (uintptr_t)(p + k), (uintptr_t)(p + k), hx, (uintptr_t)st_vgcreate, hx + 128 };
	return 1;
}
static void vg_tests(void)
{
	static struct site_spec s[2];
	static char hx[2][256];
	int ok0 = vg_standin((const void *)c_vgthunk0, "VG_FXC", &s[0], hx[0]);
	int ok1 = vg_standin((const void *)c_vgthunk1, "VG_FXC1", &s[1], hx[1]);
	EXPECT(ok0 && ok1, "vbglow: the stand-ins' jmps");
	if (!ok0 || !ok1)
		return;
	int bad = 0;
	setenv("SOMVR_VERBS_GLOW", "hide", 1);
	setenv("SOMVR_VERBS_GLOW_FX", "Flare, orb", 1);
	vg_start(s, &bad);
	static _Alignas(16) unsigned char flare[0x1c8], other[0x1c8];
	strcpy((char *)flare, "FX\\Characters\\Player\\Wraith\\Wraith_FLARE_01.fxp");
	strcpy((char *)other, "fx\\common\\blood\\blood_level01_weapontrail.fxp");
	int32_t node = 90;
	uintptr_t parent = 0x1234;
	memcpy(flare + VG_CS_NODE, &node, sizeof node), memcpy(flare + VG_CS_PARENT, &parent, sizeof parent);
	uintptr_t handle = 0;
	uint64_t r0 = c_vgthunk0(0x111, (uintptr_t)&handle, (uintptr_t)flare, 0x101, 0x200, 7); /* no frame of ours: made */
	int n0 = g_vgt.n;
	uint64_t a0 = g_vgt.start, b0 = g_vgt.b, cc0 = g_vgt.c;
	vg_frame(1);
	uint64_t r1 = c_vgthunk0(0x111, (uintptr_t)&handle, (uintptr_t)flare, 1, 0, 0); /* our eyes, named: not made */
	uint64_t r2 = c_vgthunk1(0x222, (uintptr_t)other, (uintptr_t)&handle);            /* another FX: made */
	int args1 = g_vgt.mgr == 0x222 && g_vgt.out == (uintptr_t)&handle && g_vgt.cs == (uintptr_t)other &&
		    g_vgt.start == 1 && g_vgt.b == 1 && g_vgt.c == 0;
	const char *w1 = vg_word("log");
	uint64_t r3 = c_vgthunk1(0x222, (uintptr_t)flare, (uintptr_t)&handle); /* log: made */
	const char *w2 = vg_word("hide");
	vg_frame(0);
	uint64_t r4 = c_vgthunk0(0x111, 0, (uintptr_t)flare, 1, 0, 0); /* a flat frame: made */
	const char *w3 = vg_word("fx blood");
	vg_frame(1);
	uint64_t r5 = c_vgthunk1(0x222, (uintptr_t)other, (uintptr_t)&handle); /* the new word: not made */
	EXPECT(!bad && g_vg.installed && r0 == 0x55 && n0 == 1 && a0 == 1 && b0 == 0 && cc0 == 7 && r1 == 0 && r2 == 0x55 &&
		       args1 && r3 == 0x55 && r4 == 0x55 && r5 == 0 && g_vgt.n == 4 && g_vg.hidden == 2 && g_vg.made == 4 &&
		       !strncmp(w1, "vbglow log", 10) && !strncmp(w2, "vbglow hide", 11) && !strncmp(w3, "vbglow fx: 1 words", 18) &&
		       log_has("HOOK VG_FXC ok jmp") && log_has("HOOK VG_FXC1 ok jmp") &&
		       log_has("CONFIG verbs_glow=hide (SOMVR_VERBS_GLOW: ClientFX's create watched") && log_has("'flare', 'orb'") &&
		       log_has("Wraith_FLARE_01.fxp' on an object 0x1234 node 90") && log_has("not made (our eyes: hide)"),
	       "vbglow: %d calls of the game's, r %#llx %#llx %#llx %#llx %#llx %#llx, hidden %llu, made %llu, bad %d", g_vgt.n,
	       (unsigned long long)r0, (unsigned long long)r1, (unsigned long long)r2, (unsigned long long)r3,
	       (unsigned long long)r4, (unsigned long long)r5, (unsigned long long)g_vg.hidden, (unsigned long long)g_vg.made, bad);
	/* build 21: SOMVR_FX_FACE by its words in our eyes, whatever it is made on; another name made; "vbglow all N" */
	static _Alignas(16) unsigned char cam[0x1c8];
	strcpy((char *)cam, "fx\\ui\\wraith_ranged\\records\\pres_wraith_CAMERA.fxp");
	memcpy(cam + VG_CS_NODE, &node, sizeof node);
	char w4[200], w5[200], w6[200]; /* (one static message buffer) */
	snprintf(w4, sizeof w4, "%s", vg_word("face fx pres_wraith_camera,smoke"));
	snprintf(w5, sizeof w5, "%s", vg_word("face hide"));
	snprintf(w6, sizeof w6, "%s", vg_word("all 2"));
	uintptr_t me = 0x4321, orc = 0x9999, none = 0;
	vg_frame(1);
	atomic_store(&g_vg.me, me);
	memcpy(cam + VG_CS_PARENT, &me, sizeof me);
	uint64_t r6 = c_vgthunk0(0x111, (uintptr_t)&handle, (uintptr_t)cam, 1, 0, 0); /* on Talion: not made */
	memcpy(cam + VG_CS_PARENT, &orc, sizeof orc);
	uint64_t r7 = c_vgthunk1(0x222, (uintptr_t)flare, (uintptr_t)&handle); /* another name: made */
	uint64_t r7b = c_vgthunk1(0x222, (uintptr_t)cam, (uintptr_t)&handle); /* on an object (the camera's): not made */
	memcpy(cam + VG_CS_PARENT, &none, sizeof none);
	uint64_t r8 = c_vgthunk1(0x222, (uintptr_t)cam, (uintptr_t)&handle); /* on nothing: not made */
	vg_frame(0);
	memcpy(cam + VG_CS_PARENT, &me, sizeof me);
	uint64_t r9 = c_vgthunk0(0x111, (uintptr_t)&handle, (uintptr_t)cam, 1, 0, 0); /* a flat frame: made */
	EXPECT(r6 == 0 && r7 == 0x55 && r7b == 0 && r8 == 0 && r9 == 0x55 && g_vg.face_hidden == 3 && !strncmp(w4, "vbglow face: game, 2 words", 26) &&
		       !strncmp(w5, "vbglow face: hide, 2 words", 26) && !strncmp(w6, "vbglow all: the next 2 FX logged", 32) &&
		       log_has("pres_wraith_CAMERA.fxp' on Talion 0x4321 node 90 off 0.0,0.0,0.0 flags 0 start 1 (caller") &&
		       log_has(" (all): not made (our eyes: the face)") && log_has("pres_wraith_CAMERA.fxp' on an object 0x9999") &&
		       log_has("pres_wraith_CAMERA.fxp' on nothing 0 node 90") && atomic_load(&g_vg.all) == 0,
	       "vbglow face: r %#llx %#llx %#llx %#llx, hidden by the face %llu; '%s' '%s' '%s'", (unsigned long long)r6,
	       (unsigned long long)r7, (unsigned long long)r8, (unsigned long long)r9, (unsigned long long)g_vg.face_hidden, w4,
	       w5, w6);
	vg_end_log();
	EXPECT(log_has("VBGLOW END fx asked 11: made 6, not made in our eyes 5 (the last 'fx\\ui\\wraith_ranged\\records\\pres_wraith_CAMERA.fxp'; "
		       "3 by SOMVR_FX_FACE)") && log_has("now hide, the face hide"),
	       "vbglow END");
	unsetenv("SOMVR_VERBS_GLOW");
	unsetenv("SOMVR_VERBS_GLOW_FX");
	memset(&g_vg, 0, sizeof g_vg); /* (the stand-ins stay patched; nothing calls them again) */
	g_vg_sites = g_vg_sites_game;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: selftest DIR\n");
		return 2;
	}
	char *logp = g_logp, trig[1024];
	snprintf(g_logp, sizeof g_logp, "%s/somvr.log", argv[1]);
	snprintf(trig, sizeof trig, "%s/somvr.trigger", argv[1]);
	setenv("SOMVR_LOG", logp, 1);
	setenv("SOMVR_STEREO", "A", 1);
	unsetenv("SOMVR_TRIGGER");
	unsetenv("SOMVR_VIEWS");
	setenv("SOMVR_AUDIO", "head", 1); /* the optional hooks go in; their flags are toggled by the tests below */
	setenv("SOMVR_FOLLOW", "1", 1);
	setenv("SOMVR_BASE", "head", 1);
	setenv("SOMVR_COMBAT", "1", 1); /* checked and configured at start, then off until combat_tests (white box) */
	setenv("SOMVR_BLADE", "fake", 1);
	setenv("SOMVR_COMBAT_REACT", "1", 1); /* the REACT sites checked at start, then off until react_tests (white box) */
	setenv("SOMVR_INPUT", "1", 1); /* the IN_ and RUM hooks go in, somvr_input goes active (input_tests) */
	setenv("SOMVR_GAMEMOVES", "on", 1); /* the stock A (input_tests' Jump/LedgePullUp; the off path: test_ctl) */
	setenv("SOMVR_ARMS", "1", 1);  /* ARM_TRK_EVAL goes in (arms_tests) */
	unsetenv("SOMVR_ARMS_POLE"), unsetenv("SOMVR_ARMS_POLE_ANIM"), unsetenv("SOMVR_ARMS_CAL_R"), unsetenv("SOMVR_ARMS_CAL_L");
	unsetenv("SOMVR_HANDS"), unsetenv("SOMVR_TURN"), unsetenv("SOMVR_HAPTICS"), unsetenv("SOMVR_RUMBLE");
	setenv("SOMVR_COMBAT_LIGHT", "ST_L1,ST_L2", 1);
	setenv("SOMVR_COMBAT_HEAVY", "ST_H1,ST_MISSING,ST_H2", 1);
	unsetenv("SOMVR_COMBAT_STAB");
	unsetenv("SOMVR_HITSTOP_MS");
	setenv("SOMVR_EYE_SIZE", "1832x1920", 1); /* the EX hooks go in; layout A at start: inactive until B (white box) */
	setenv("SOMVR_HIDE_HEAD", "3,5", 1);
	unsetenv("SOMVR_HEAD_SHADOW");
	setenv("SOMVR_HUD", "quad", 1); /* the HUD_PASS_* and UI_PRESENT_COMPOSITE hooks go in, somvr_hud goes active */
	setenv("SOMVR_POINTER", "1", 1); /* the LP_GUARD_* hooks go in, somvr_input wants the pointer (ptr_tests) */
	setenv("SOMVR_VIGNETTE", "0.6", 1);     /* somvr_comfort goes active: an entry per stereo frame (comfort_tests) */
	setenv("SOMVR_SNAP_BLINK_MS", "90", 1); /* ... the blink's gate switched off below until comfort_tests (white box) */
	unsetenv("SOMVR_SEATED"), unsetenv("SOMVR_HEIGHT_CM");
	unsetenv("SOMVR_VRMENU"), unsetenv("SOMVR_SETTINGS"); /* the menu off at start: the object stays zero (vm_tests) */
	unsetenv("SOMVR_XR_HUD_QUAD");
	setenv("SOMVR_EYE_FWD_CM", "0", 1), setenv("SOMVR_EYE_MIN_CM", "0", 1); /* the eye offset off (hv_tests) */
	unsetenv("SOMVR_EYE_UP_CM"), unsetenv("SOMVR_EYE_NEAR_CM"), unsetenv("SOMVR_TUNE");
	setenv("SOMVR_THREAT", "1", 1); /* SOMVR_THREAT: its sites, the projectile slot, somvr_threat at start (threat_tests) */
	setenv("SOMVR_THREAT_PARRY", "1", 1);
	static const char *const th_env[] = { "SOMVR_THREAT_VIEW_DEG", "SOMVR_THREAT_GLOW", "SOMVR_THREAT_EDGE",
					      "SOMVR_THREAT_NEAR_M", "SOMVR_THREAT_HAPTIC", "SOMVR_THREAT_PROJ", "SOMVR_THREAT_SOUND" };
	for (size_t i = 0; i < sizeof th_env / sizeof th_env[0]; i++)
		unsetenv(th_env[i]);

	/* spike1's comfort pokes write fixed game addresses: a buffer here; the velocity-blur bytes start at the game's 1 */
	static unsigned char poke_buf[sizeof g_pokes / sizeof g_pokes[0]] = { 0, 1, 1, 1, 1 };
	for (size_t i = 0; i < sizeof g_pokes / sizeof g_pokes[0]; i++)
		g_pokes[i].addr = (uintptr_t)&poke_buf[i];
	struct site_spec sites[HK_N];
	if (call_site((void *)c_h1, (void *)t_h1, &sites[HK_H1], "H1", 0) ||
	    call_site((void *)c_h3, (void *)t_h3, &sites[HK_H3], "H3", 2) ||
	    call_site((void *)c_h4, (void *)t_h4, &sites[HK_H4], "H4", 3) ||
	    call_site((void *)c_h6, (void *)t_h6, &sites[HK_H6], "H6", 4)) {
		printf("FAIL: no call site found in the stand-in callers\n");
		return 1;
	}
	vslot_site(&g_vt[37], (void *)t_h2, &sites[HK_H2], "H2", 1);
	vslot_site(&g_vt9[8], (void *)t_h9, &sites[HK_H9], "H9", 5);
	if (jmp_site((void *)c_h8, (void *)t_h8, &sites[HK_H8], "H8", 6)) {
		printf("FAIL: no E9 tail jump in the stand-in executor\n");
		return 1;
	}
	d3d_world();
	struct site_spec d3d[DS_N];
	vslot_site(&st_vt_dev[5], (void *)st_create, &d3d[DS_CREATETEX2D], "D3D_CREATETEX2D", 7);
	vslot_site(&st_vt_ctx[47], (void *)st_copy, &d3d[DS_COPY], "D3D_COPYRESOURCE", 8);
	vslot_site(&st_vt_ctx_mt[47], (void *)st_copy_mt, &d3d[DS_COPY_MT], "D3D_COPYRESOURCE_MT", 9);
	vslot_site(&st_vt_tex[10], (void *)st_getdesc, &d3d[DS_GETDESC], "D3D_TEX_GETDESC", 10);
	vslot_site(&st_vt_tex[2], (void *)st_release, &d3d[DS_RELEASE], "D3D_TEX_RELEASE", 11);
	vslot_site(&st_vt_vktex[10], (void *)st_vkcopy, &d3d[DS_VKCOPY], "VK_TEX_COPY", 12);
	vslot_site(&st_vt_vktex[11], (void *)st_vkcopy, &d3d[DS_VKCOPY_MT], "VK_TEX_COPY_MT", 13);
	vslot_site(&g_vta[23], (void *)t_aud, &sites[HK_AUD], "AUDIO", 14);
	vslot_site(&g_vtpm[11], (void *)t_pm, &sites[HK_PM], "PM", 15);
	static struct site_spec nodes[NS_N];
	vslot_site(&g_vtm[23], (void *)st_nodexf, &nodes[NS_NODEXF], "G12_NODEXF", 16);
	g_node_sites = nodes;
	g_som.model_pp = (uintptr_t)&g_model_ptr, g_som.vt_pm = (uintptr_t)g_vtpm;
	player_world();
	static struct site_spec cbs[CS_N];
	entry_site((void *)st_dealhit, &cbs[CS_DEALHIT], "CB_DEALHIT", 17);
	entry_site((void *)st_byname, &cbs[CS_ATK_BYNAME], "CB_ATK_BYNAME", 18);
	entry_site((void *)st_attackdata, &cbs[CS_ATTACKDATA], "CB_ATTACKDATA", 19);
	entry_site((void *)st_stim, &cbs[CS_STIMULUS], "CB_STIMULUS", 20);
	entry_site((void *)st_stimfind, &cbs[CS_STIM_FIND], "CB_STIM_FIND", 21);
	entry_site((void *)st_stimremove, &cbs[CS_STIM_REMOVE], "CB_STIM_REMOVE", 22);
	vslot_site(&st_vt_T[3], (void *)st_tcreate, &cbs[CS_TIMER_CREATE], "CB_TIMER_CREATE", 23);
	vslot_site(&st_vt_T[9], (void *)st_tset, &cbs[CS_OBJTIMER_SET], "CB_OBJTIMER_SET", 24);
	vslot_site(&st_vt_T[10], (void *)st_tget, &cbs[CS_OBJTIMER_GET], "CB_OBJTIMER_GET", 25);
	entry_site((void *)st_trelease, &cbs[CS_TIMER_RELEASE], "CB_TIMER_RELEASE", 26);
	g_cb_sites = cbs;
	cb_world();
	if (rx_stand_in_sites()) { /* SOMVR_COMBAT_REACT's four K_ENTRY stand-ins and RX_ATK_GATE's call */
		printf("FAIL: no call site found in the stand-in RX_ATK_GATE caller\n");
		return 1;
	}
	vslot_site(&g_vtin[4], (void *)t_vf04, &sites[HK_IN_TICK], "IN_TICK", 27);
	vslot_site(&g_vtin[22], (void *)t_vf22, &sites[HK_IN_VALUE], "IN_VALUE", 28);
	vslot_site(&g_vtin[23], (void *)t_vf23, &sites[HK_IN_VALUE2], "IN_VALUE2", 29);
	vslot_site(&g_vtin[25], (void *)t_vf25, &sites[HK_IN_ISON], "IN_ISON", 30);
	if (call_site((void *)c_rum1, (void *)t_rumble, &sites[HK_RUM1], "RUM1", 31) ||
	    call_site((void *)c_rum2, (void *)t_rumble, &sites[HK_RUM2], "RUM2", 32)) {
		printf("FAIL: no call site found in the stand-in rumble callers\n");
		return 1;
	}
	g_cwbi.vt = (uintptr_t)g_vtin;
	static struct site_spec ins[IS_N];
	entry_site((void *)st_cmd_on, &ins[IS_CMD_ON], "IN_CMD_ON", 33);
	entry_site((void *)st_cmd_off, &ins[IS_CMD_OFF], "IN_CMD_OFF", 34);
	entry_site((void *)st_cmd_active, &ins[IS_CMD_ACTIVE], "IN_CMD_ACTIVE", 35);
	entry_site((void *)st_inmode, &ins[IS_INPUT_MODE], "IN_INPUT_MODE", 36);
	entry_site((void *)st_video, &ins[IS_VIDEOCOVER], "IN_VIDEOCOVER", 37);
	g_in_sites = ins;
	input_world();
	static struct site_spec gls[GL_N];
	gl_world(gls); /* the pad glyphs' stand-ins (checked at start: SOMVR_INPUT=1) */
	static struct site_spec ess[ES_N];
	vslot_site(&g_vteng[32], (void *)st_runstring, &ess[ES_RUNSTRING], "ES_RUNSTRING", 64);
	g_es_sites = ess;
	g_eng.vt = (uintptr_t)g_vteng, g_eng_ptr = &g_eng, g_es.eng_pp = (uintptr_t)&g_eng_ptr;
	vslot_site(&g_vttrk[3], (void *)t_trk_eval, &sites[HK_ARM], "ARM_TRK_EVAL", 57);
	static struct site_spec arms[AS_N];
	entry_site((void *)st_arm_modelof, &arms[AS_MODELOF], "ARM_MODELOF", 58);
	g_arm_sites = arms;
	arm_world();
	model_world();
	static struct site_spec ex[EX_N], hs[HS_N];
	if (call_site((void *)c_desc, (void *)t_desc, &ex[EX_RES3D], "RES_3D_DESC", 38) ||
	    call_site((void *)c_rect, (void *)t_rect, &ex[EX_LB], "RES_RECT_LETTERBOX", 39) ||
	    call_site((void *)c_rect_init, (void *)t_rect, &ex[EX_LB_INIT], "RES_RECT_LETTERBOX_INIT", 40) ||
	    call_site((void *)c_shpre, (void *)t_shpre, &ex[EX_SH_PRE], "HEAD_SHADOW_PRE", 41) ||
	    call_site((void *)c_shlights, (void *)t_shlights, &ex[EX_SH_LIGHTS], "HEAD_SHADOW_LIGHTS", 42) ||
	    call_site((void *)c_shcasc, (void *)t_shcasc, &ex[EX_SH_CASC], "HEAD_SHADOW_CASCADES", 43) ||
	    call_site((void *)c_shlights2, (void *)t_shlights2, &ex[EX_SH_LIGHTS2], "HEAD_SHADOW_LIGHTS2", 44) ||
	    call_site((void *)st_hidepiece, (void *)st_modelof, &hs[HS_MODELOF], "G12_MODELOF", 45) ||
	    call_site((void *)st_vf50, (void *)st_rend_mask, &hs[HS_MODEL_MASK], "HEAD_MODEL_MASK", 46) ||
	    call_site((void *)c_hudeye, (void *)t_hud, &ex[EX_HUD_EYE], "HUD_PASS_EYE", 54) ||
	    call_site((void *)c_hudcov, (void *)t_hud, &ex[EX_HUD_COVER], "HUD_PASS_COVER", 55) ||
	    call_site((void *)c_comp, (void *)t_comp, &ex[EX_UI_COMP], "UI_PRESENT_COMPOSITE", 56)) {
		printf("FAIL: no call site found in the EX stand-in callers\n");
		return 1;
	}
	vslot_site(&g_vtm[7], (void *)st_hidepiece, &hs[HS_HIDEPIECE], "G12_HIDEPIECE", 47);
	vslot_site(&g_vtm[3], (void *)st_numpieces, &hs[HS_NUMPIECES], "G12_NUMPIECES", 48);
	vslot_site(&g_vt_smodel[50], (void *)st_vf50, &hs[HS_MODEL_VT], "HEAD_MODEL_VT", 49);
	vslot_site(&g_vt_smodel[162], (void *)st_getrend, &hs[HS_MODEL_REND], "HEAD_MODEL_RENDERABLE", 50);
	vslot_site(&g_vt_rend[2], (void *)st_rend_update, &hs[HS_REND_VT], "HEAD_RENDERABLE_VT", 51);
	vslot_site(&g_vt_handle[53], (void *)st_handle_setmask, &hs[HS_HANDLE], "HEAD_HANDLE_SETMASK", 52);
	vslot_site(&g_vt_node[5], (void *)st_node_setmask, &hs[HS_NODE], "HEAD_NODE_SETMASK", 53);
	g_ex_sites = ex, g_hh_sites = hs;
	static struct site_spec lps[LP_N];
	if (lp_sites(lps)) {
		printf("FAIL: no call site found in the stand-in guard callers\n");
		return 1;
	}
	g_lp_sites = lps;
	setenv("SOMVR_FALLBACK", "auto", 1); /* SOMVR_FALLBACK: its sites checked and the config at start (fb_tests) */
	unsetenv("SOMVR_FALLBACK_ON"), unsetenv("SOMVR_FALLBACK_TAGS"), unsetenv("SOMVR_FALLBACK_MS");
	unsetenv("SOMVR_FALLBACK_SYNC"), unsetenv("SOMVR_FALLBACK_FADE_MS");
	fb_stand_in_sites(g_fbs);
	g_fb_sites = g_fbs;
	setenv("SOMVR_BODYMOMENTS", "1", 1); /* SOMVR_BODYMOMENTS: its sites checked and the config at start (bm_tests) */
	unsetenv("SOMVR_BM_POLICY"), unsetenv("SOMVR_BM_NAMES"), unsetenv("SOMVR_BM_YAW"), unsetenv("SOMVR_BM_DIM");
	unsetenv("SOMVR_BM_SEAT"), unsetenv("SOMVR_BM_BLINK_MS"), unsetenv("SOMVR_BM_MS");
	bm_stand_in_sites(g_bms);
	g_bm_sites = g_bms;
	setenv("SOMVR_CLIMB", "b", 1); /* SOMVR_CLIMB: its sites checked and the config at start (climb_tests) */
	unsetenv("SOMVR_CLIMB_REACH_CM"), unsetenv("SOMVR_CLIMB_STEP_CM"), unsetenv("SOMVR_CLIMB_MAX_S");
	unsetenv("SOMVR_CLIMB_TOP"), unsetenv("SOMVR_CLIMB_RAYS"), unsetenv("SOMVR_CLIMB_STICK");
	cl_stand_in_sites();
	g_cl_sites = g_cls;
	setenv("SOMVR_BOW", "1", 1); /* SOMVR_BOW: its sites, the free-aim hook and the config at start (bow_tests) */
	static const char *const bow_env[] = { "SOMVR_BOW_HAND", "SOMVR_BOW_AIM_CMD", "SOMVR_BOW_FIRE_CMD", "SOMVR_BOW_FIRE",
					       "SOMVR_BOW_FIRE_MS", "SOMVR_BOW_STRING", "SOMVR_BOW_BUTTON", "SOMVR_BOW_NOCK_CM",
					       "SOMVR_BOW_NOCK_R_CM", "SOMVR_BOW_MIN_DRAW_CM", "SOMVR_BOW_FULL_DRAW_CM",
					       "SOMVR_BOW_EXIT_CM", "SOMVR_BOW_EXIT_MS", "SOMVR_BOW_AIM_MIN_MS", "SOMVR_BOW_WAIT_MS",
					       "SOMVR_BOW_FOLLOW_MS", "SOMVR_BOW_IDLE_RAY", "SOMVR_BOW_ALIGN" };
	for (size_t i = 0; i < sizeof bow_env / sizeof bow_env[0]; i++)
		unsetenv(bow_env[i]);
	setenv("SOMVR_BOW_CAL", "0,2,0,90,30,0", 1); /* the bow node: not the arms' CAL_L (bow_tests' arms part) */
	if (bo_stand_in_sites()) {
		printf("FAIL: no call site found in the stand-in free-aim caller\n");
		return 1;
	}
	g_bo_sites = g_bos;
	setenv("SOMVR_LOCO", "1", 1);      /* SOMVR_LOCO: its sites, LOCO_ROOTDELTA and the config at start (loco_tests) */
	setenv("SOMVR_ANCHOR", "root", 1); /* SOMVR_ANCHOR=root: the anchor's config at start (loco_tests) */
	static const char *const lo_env[] = { "SOMVR_LOCO_SPEED", "SOMVR_LOCO_STICK_MS", "SOMVR_LOCO_AIM_CMS", "SOMVR_LOCO_YAW_DPS",
					      "SOMVR_LOCO_YAW", "SOMVR_LOCO_LATCH", "SOMVR_ANCHOR_EYE_CM", "SOMVR_ANCHOR_Y_MS",
					      "SOMVR_ANCHOR_BLEND_MS", "SOMVR_ANCHOR_LEAD_MS" };
	for (size_t i = 0; i < sizeof lo_env / sizeof lo_env[0]; i++)
		unsetenv(lo_env[i]);
	/* the bands pinned to the pre-P10 150 / 500 / 800 that loco_tests' numbers use (the env path parsed too); the P10
	 * defaults (230 / 640) are test_loco's */
	setenv("SOMVR_LOCO_WALK_CMS", "150", 1);
	setenv("SOMVR_LOCO_RUN_CMS", "500", 1);
	setenv("SOMVR_LOCO_SPRINT_CMS", "800", 1);
	if (lo_stand_in_sites()) {
		printf("FAIL: no call site found in the stand-in vf_11 / attack caller\n");
		return 1;
	}
	g_lo_sites = g_los;
	setenv("SOMVR_STEP", "1", 1);    /* SOMVR_STEP / _DASH / _PHASE: their sites and the config at start (step_tests) */
	setenv("SOMVR_DASH", "on", 1);
	setenv("SOMVR_PHASE", "on", 1);
	static const char *const st_env[] = { "SOMVR_STEP_MAX_CM", "SOMVR_STEP_MS", "SOMVR_STEP_REACH_CM", "SOMVR_STEP_STOP_CM",
					      "SOMVR_DASH_CM", "SOMVR_DASH_MS", "SOMVR_PHASE_MS", "SOMVR_PHASE_BEHIND_CM",
					      "SOMVR_PHASE_RANGE_CM", "SOMVR_PHASE_HOLD_MS", "SOMVR_PHASE_LOOK", "SOMVR_PHASE_NOCOLLIDE",
					      "SOMVR_STEP_OWN" };
	for (size_t i = 0; i < sizeof st_env / sizeof st_env[0]; i++)
		unsetenv(st_env[i]);
	if (step_stand_in_sites()) {
		printf("FAIL: no call site found in the stand-in vision caller\n");
		return 1;
	}
	g_step_sites = g_sts;
	if (th_stand_in_sites()) {
		printf("FAIL: no call site found in the stand-in threat proof caller\n");
		return 1;
	}
	g_th_sites = g_ths;
	setenv("SOMVR_SLOTS", "1", 1); /* SOMVR_SLOTS: its sites, the tail jump and the config at start (slots_tests) */
	static const char *const sl_env[] = { "SOMVR_SLOTS_DAGGER", "SOMVR_SLOTS_SWORD_CM", "SOMVR_SLOTS_HIP_CM",
					      "SOMVR_SLOTS_BACK_CM", "SOMVR_SLOTS_SWORD_R_CM", "SOMVR_SLOTS_DAGGER_R_CM",
					      "SOMVR_SLOTS_APPROACH_CM", "SOMVR_SLOTS_DEADBAND_DEG", "SOMVR_SLOTS_QUICK_MS",
					      "SOMVR_SLOTS_HOLD", "SOMVR_SLOTS_RESTORE", "SOMVR_SLOTS_KEEP", "SOMVR_SLOTS_SOUND",
					      "SOMVR_SLOTS_ACTIVATE" };
	for (size_t i = 0; i < sizeof sl_env / sizeof sl_env[0]; i++)
		unsetenv(sl_env[i]);
	if (sl_stand_in_sites()) {
		printf("FAIL: no jump/call site found in the stand-in model-string handler / re-attach\n");
		return 1;
	}
	g_sl_sites = g_sls;
	setenv("SOMVR_DEFENCE", "1", 1); /* SOMVR_DEFENCE: its sites, DF_IMPACT and the config at start (df_tests) */
	static const char *const df_env[] = { "SOMVR_DEFENCE_WAY_CM", "SOMVR_DEFENCE_CROSS_DEG", "SOMVR_DEFENCE_ALIGN_DEG",
					      "SOMVR_DEFENCE_PARRY_MS", "SOMVR_DEFENCE_GRACE_MS", "SOMVR_DEFENCE_DODGE_CM",
					      "SOMVR_DEFENCE_BACK_CM", "SOMVR_DEFENCE_ARROW_CM", "SOMVR_DEFENCE_RED", "SOMVR_DEFENCE_HAPTIC",
					      "SOMVR_DEFENCE_PARRY", "SOMVR_DEFENCE_BLOCK", "SOMVR_DEFENCE_ARROWS",
					      "SOMVR_DEFENCE_COUNTER_CMD", "SOMVR_DEFENCE_PRESS_TICKS" };
	for (size_t i = 0; i < sizeof df_env / sizeof df_env[0]; i++)
		unsetenv(df_env[i]);
	if (df_stand_in_sites()) {
		printf("FAIL: no call site found in the stand-in impact caller\n");
		return 1;
	}
	g_df_sites = g_dfs;
	setenv("SOMVR_PRACTICE", "1", 1); /* SOMVR_PRACTICE: its sites and the config at start (pr_tests) */
	static const char *const pr_env[] = { "SOMVR_PRACTICE_AT", "SOMVR_PRACTICE_RADIUS_M", "SOMVR_PRACTICE_CLEAR",
					      "SOMVR_PRACTICE_CLEAR_M", "SOMVR_PRACTICE_DAY", "SOMVR_PRACTICE_WEATHER",
					      "SOMVR_PRACTICE_MINHEALTH", "SOMVR_PRACTICE_AHEAD_CM", "SOMVR_PRACTICE_AUTO_S",
					      "SOMVR_PRACTICE_ORC", "SOMVR_PRACTICE_QUIET" };
	for (size_t i = 0; i < sizeof pr_env / sizeof pr_env[0]; i++)
		unsetenv(pr_env[i]);
	pr_stand_in_sites();
	if (somvr_start(sites, d3d, "selftest") != 0) {
		printf("FAIL: somvr_start\n");
		return 1;
	}
	for (int i = 0; i < HK_N; i++)
		EXPECT(g_h[i].installed, "%s not installed (see the log)", g_h[i].name);
	EXPECT(somvr_shared_ok(&g_sh) && g_sh.layout == SOMVR_LAYOUT_A, "the shared struct is not published");
	EXPECT(g_d3d_ok == 1, "the stand-in D3D sites did not match (see the D3D lines)");
	EXPECT(dlsym(RTLD_DEFAULT, SOMVR_SHARED_SYMBOL) == (void *)&g_sh, "dlsym(RTLD_DEFAULT, \"somvr_shared\") does not find it");
	EXPECT(g_audio == 1 && g_follow.on == 1 && g_rigcfg.base == SOMVR_BASE_HEAD && g_node_ok == 1 && g_follow.dead_cm == 8 &&
		       g_follow.step_cm == 30,
	       "switches: audio %d follow %d base %d node site %d dead %g step %g", g_audio, g_follow.on, g_rigcfg.base, g_node_ok,
	       g_follow.dead_cm, g_follow.step_cm);
	EXPECT(g_cb.mode == CB_FULL && g_cb.blade == CB_BLADE_FAKE && g_cb.ok_hit && g_cb.ok_stim && g_cb.ok_stop &&
		       g_cb.nrec[SC_TIER_LIGHT] == 2 && g_cb.nrec[SC_TIER_HEAVY] == 3 && g_cb.nrec[SC_TIER_STAB] == 1 &&
		       NEAR(g_cb.hitstop_s, 0.12, 1e-6) && g_cb.orc_r == 35,
	       "combat: mode %d blade %d parts %d %d %d records %d %d %d hitstop %g", g_cb.mode, g_cb.blade, g_cb.ok_hit,
	       g_cb.ok_stim, g_cb.ok_stop, g_cb.nrec[1], g_cb.nrec[2], g_cb.nrec[3], g_cb.hitstop_s);
	EXPECT(g_rx.mode == RX_ON && g_rx.ok == 1, "react at start: mode %d sites %d", g_rx.mode, g_rx.ok);
	g_rx.mode = RX_OFF; /* white box: combat_tests without reactions (react_tests turns them on) */
	for (int i = 0; i < EX_N; i++)
		EXPECT(g_ex[i].installed, "%s not installed (see the log)", g_ex[i].name);
	EXPECT(g_hide.on == 1 && g_eye_sz.mode == 1 && g_hudq.on == 1, "switches: hide %d eye size %d hud %d", g_hide.on,
	       g_eye_sz.mode, g_hudq.on);
	EXPECT(dlsym(RTLD_DEFAULT, SOMVR_HUD_SYMBOL) == (void *)&g_hud && somvr_hud_ok(&g_hud), "somvr_hud not exported/active");
	EXPECT(g_fb.on == 1 && g_fb.sites_ok == (1u << FS_N) - 1, "fallback at start: on %d sites %#x", g_fb.on, g_fb.sites_ok);
	EXPECT(g_cl.on == 1 && g_cl.ok_surf && g_cl.vt_phys == (uintptr_t)g_vtphys && g_cl.vt_pc == (uintptr_t)g_vtpc &&
		       g_cl.cfg.top_mode == SOMVR_CLIMB_TOP_PUSH && g_cl.cfg.down_assist && g_h[HK_PM].installed,
	       "climb at start: on %d surfaces %d", g_cl.on, g_cl.ok_surf);
	EXPECT(g_bow.on == 1 && g_bow_h.installed && g_bow.ok_ammo && g_bow.ok_fire && g_bow.aim_cmd == 124 &&
		       g_bow.fire_cmd == 0 && g_bow.button == 1 && g_bow.align == 1 && g_bow.cal_v[4] == 30,
	       "bow at start: on %d hook %d ammo %d fire %d commands %d/%d", g_bow.on, g_bow_h.installed, g_bow.ok_ammo,
	       g_bow.ok_fire, g_bow.aim_cmd, g_bow.fire_cmd);
	EXPECT(g_bm.on == 1 && g_bm.sites_ok == (1u << BMS_N) - 1, "body moments at start: on %d sites %#x", g_bm.on,
	       g_bm.sites_ok);
	g_fb.on = 0;   /* white box: the pipeline tests without the fallback (fb_tests turns it on again) */
	g_bm.on = 0;   /* ... and without the body moments (bm_tests turns them on again) */
	g_cl.on = 0;   /* ... and without climbing (climb_tests turns it on again) */
	g_bow.on = 0;  /* ... and without the bow (bow_tests turns it on again) */
	EXPECT(g_lo.on == 1 && g_lo.anchor == 1 && g_lo_h.installed && g_lo.ok_timer && g_lo.ok_lockr && g_lo.ok_fsm,
	       "loco at start: on %d anchor %d hook %d timer %d lock_r %d fsm %d", g_lo.on, g_lo.anchor, g_lo_h.installed,
	       g_lo.ok_timer, g_lo.ok_lockr, g_lo.ok_fsm);
	g_lo.on = 0, g_lo.anchor = 0; /* ... and without the locomotion core and the root anchor (loco_tests turns them on) */
	EXPECT(g_step.on == 1 && g_step.ok_col && g_step.ok_vis && g_step.cfg.step == 1 && g_step.cfg.dash == SOMVR_ST_DASH_ON &&
		       g_step.cfg.phase == SOMVR_ST_PHASE_ON,
	       "step at start: on %d col %d vis %d", g_step.on, g_step.ok_col, g_step.ok_vis);
	g_step.on = 0; /* ... and without the assisted step, the dash and the phase (step_tests turns them on) */
	EXPECT(g_th.on == 1 && g_th.parry == 1 && g_th.proj == 1 && g_th.ok_keys && g_th.ok_node && g_th.ok_snd,
	       "threat at start: on %d parry %d proj %d keys %d node %d sound %d", g_th.on, g_th.parry, g_th.proj, g_th.ok_keys,
	       g_th.ok_node, g_th.ok_snd);
	g_th.on = 0;   /* ... and without the threat warnings (threat_tests turns them on again) */
	EXPECT(g_sl.on == 1 && g_sl_h.installed && g_sl.ok_sword && g_sl.ok_dagger && g_sl.ok_act && g_sl.ok_snd &&
		       g_sl.keep_mode == SL_KEEP_DRAWN && g_sl.cfg.reassert == 1,
	       "slots at start: on %d hook %d sword %d dagger %d activate %d sound %d keep %d", g_sl.on, g_sl_h.installed,
	       g_sl.ok_sword, g_sl.ok_dagger, g_sl.ok_act, g_sl.ok_snd, g_sl.keep_mode);
	g_sl.on = 0;   /* ... and without the body slots (slots_tests turns them on again) */
	EXPECT(g_df.mode == DF_ON && g_df_h.installed && g_df.ok_impact && g_df.ok_window && g_df.ok_type && g_df.ok_recoil &&
		       g_df.ok_ad && g_df.parry == DFP_COUNTER && g_df.block == DFB_RECOIL && g_df.arrows == 1,
	       "defence at start: mode %d hook %d parry %d block %d arrows %d", g_df.mode, g_df_h.installed, g_df.parry,
	       g_df.block, g_df.arrows);
	g_df.mode = DF_OFF; /* ... and without the defence (df_tests turns it on again) */
	EXPECT(g_pr.on == 1 && g_pr.ok_spawn && g_pr.ok_clear && g_pr.ok_mh && g_pr.ok_light && g_pr.ok_gate,
	       "practice at start: on %d spawn %d clear %d mh %d light %d gate %d", g_pr.on, g_pr.ok_spawn, g_pr.ok_clear,
	       g_pr.ok_mh, g_pr.ok_light, g_pr.ok_gate);
	g_pr.on = 0; /* ... and without the practice area (pr_tests turns it on again) */
	g_hide.on = 0; /* white box: the pipeline tests without head hiding (hide_tests turns it on again) */
	g_cf.out_s = g_cf.in_s = 0; /* ... and with snap turns at once (comfort_tests turns the blink on) */
	g_hudq.on = 0; /* ... and without the HUD hooks' work (hud_tests turns it on again) */
	g_audio = 0, g_follow.on = 0, g_rigcfg.base = SOMVR_BASE_CAMERA; /* the pipeline tests first, on the camera base */
	g_cb.mode = CB_OFF;
	g_ra_vp = 0; /* any H2 return address counts as the player view */
	g_foliage = (uintptr_t)g_fol;

	/* before any vf_37: no renderer known -> flat */
	frame();
	check_mono("no renderer yet");
	float rect0[4] = { 0, 0, 1, 1 }, fov0[2] = { 1.5f, 0.9f };
	((t_h2_fn)g_vt[37])(7, 100000, g_renderer_obj, GAME.pos, fov0, rect0); /* a menu frame: H2 learns the renderer */
	EXPECT(atomic_load(&g_renderer) == (uintptr_t)g_renderer_obj, "H2 did not capture the renderer");
	frame();
	check_mono("no XR session");
	EXPECT(g_rt_calls == 0, "the runtime was called without a session");

	runtime_up();
	frame();
	check_stereo("xr views, layout A", SOMVR_LAYOUT_A, 0);
	EXPECT(g_rt_calls == 1 && g_rt_bad == 0 && g_rt_time == 7000000000, "runtime calls %d bad %d time %lld", g_rt_calls,
	       g_rt_bad, (long long)g_rt_time);
	frame(); /* no new xrWaitFrame: extrapolated by one period */
	check_stereo("xr views, extrapolated", SOMVR_LAYOUT_A, 0);
	somvr_rec rec;
	EXPECT(somvr_rec_read(&g_sh, g_frame_no, &rec) && (rec.flags & SOMVR_REC_EXTRAPOLATED) &&
		       rec.display_time == 7000000000 + 11111111,
	       "extrapolated record: flags %#x time %lld", rec.flags, (long long)rec.display_time);
	overscan_tests();

	/* layout B */
	g_layout = SOMVR_LAYOUT_B;
	g_rigcfg.layout = SOMVR_LAYOUT_B;
	EXPECT(g_creates == 0 && atomic_load(&g_sh.eyes.copies) == 0, "layout A made or copied eye textures");
	frame();
	check_stereo("xr views, layout B", SOMVR_LAYOUT_B, 0);
	check_eyes("layout B, 1st frame", 3, 2);
	EXPECT(g_creates == 2 && g_et.pub.gen == 3, "textures made %d times, gen %u (made, then each VkImage known)",
	       g_creates, g_et.pub.gen);
	for (int i = 0; i < 3; i++) {
		frame();
		check_stereo("xr views, layout B", SOMVR_LAYOUT_B, 0);
		check_eyes("layout B, next frames", 3, 2);
	}
	EXPECT(g_creates == 2 && g_et.pub.gen == 3 && atomic_load(&g_sh.eyes.copies) == 8, "creates %d gen %u copies %llu",
	       g_creates, g_et.pub.gen, (unsigned long long)atomic_load(&g_sh.eyes.copies));
	/* fail-soft: another target is bound at the eyes' ends -> no copy, the tag carries no eye */
	g_bind_override = &g_rt_other;
	frame();
	check_eyes("another target bound", 0, 0);
	EXPECT(g_et.other == 2, "other-target ends %llu", (unsigned long long)g_et.other);
	g_bind_override = NULL;
	/* the end fails (no target bound, state != 3) -> no copy */
	g_force_state = 2;
	frame();
	check_eyes("EndRenderTarget failed", 0, 0);
	g_force_state = -1;
	/* a flat frame (no XR views): one body call, H8 sees no eye match, nothing is copied */
	atomic_store(&g_sh.xr.state, SOMVR_XRS_DEAD);
	frame();
	check_mono("layout B, no views");
	int nc = 0;
	for (int i = 0; i < g_nev; i++)
		nc += g_ev[i].k == 'C';
	EXPECT(nc == 0, "a flat frame copied %d eye images", nc);
	atomic_store(&g_sh.xr.state, SOMVR_XRS_RUNNING);
	frame();
	check_eyes("layout B again", 3, 2);
	/* build 24, SOMVR_EYE_MATCH (re-xr-pipeline.md 100): the render thread behind the game thread (both eyes' views
	 * queued before eye 0's pass) with an overlay view of the eye's camera in each pass (the game's COverlayFX at low
	 * health) and both eyes one camera (`fx eyesame 1`). hash (build 23): the overlay in eye 0's pass takes eye 1's
	 * entry, eye 0's end is copied as eye 1's, eye 1's own end as nothing - the tag says 0x2 (runs 005146, 060526,
	 * 064728); pass: both eyes, in order. And the same with each eye its own camera, and with the next frame queued. */
	{
		g_h4_defer = 1;
		atomic_store(&g_rd.eyesame, 1);
		atomic_store(&g_em.pass, 0);
		uint64_t st0 = atomic_load(&g_em.stolen);
		frame();
		somvr_tag t;
		EXPECT(somvr_tag_get(&g_sh, atomic_load(&g_sh.tag_seq), &t) && t.frame == g_frame_no && t.eyes == 2u &&
			       atomic_load(&g_em.stolen) == st0 + 1 && g_et.tex[1] &&
			       ((st_tex_t *)g_et.tex[1])->content == 0xA000u + (uint32_t)g_frame_no * 4u,
		       "eyematch hash, eyesame, the pass behind: tag eyes %u (want 2: build 23's loss), taken %llu, eye 1's "
		       "image %#x (want eye 0's %#x)", t.eyes, (unsigned long long)(atomic_load(&g_em.stolen) - st0),
		       g_et.tex[1] ? ((st_tex_t *)g_et.tex[1])->content : 0, 0xA000u + (uint32_t)g_frame_no * 4u);
		atomic_store(&g_em.pass, 1);
		uint64_t k0 = atomic_load(&g_em.kept), r0 = atomic_load(&g_st_repeat);
		frame();
		check_eyes("eyematch pass, eyesame, the pass behind", 3, 2);
		EXPECT(atomic_load(&g_em.kept) == k0 + 1 && atomic_load(&g_st_repeat) == r0 + 2 && g_h4_ov[0][0] == g_h4_tan[0][0] &&
			       g_h4_ov[1][0] == g_h4_tan[1][0], "eyematch pass: kept %llu (want 1), repeats %llu (want 2), the "
		       "overlays' tangents %g/%g vs %g/%g", (unsigned long long)(atomic_load(&g_em.kept) - k0),
		       (unsigned long long)(atomic_load(&g_st_repeat) - r0), g_h4_ov[0][0], g_h4_ov[1][0], g_h4_tan[0][0],
		       g_h4_tan[1][0]);
		atomic_store(&g_rd.eyesame, 0);
		frame();
		check_eyes("eyematch pass, each eye its camera, the pass behind", 3, 2);
		g_h4_defer = 0;
		frame();
		check_eyes("eyematch pass, the pass right after its view", 3, 2);
		/* the word "eyedrop E N": eye E's bit out of the next N tags, then back (the layer's stale-frame net) */
		atomic_store(&g_em.drop_n, 2);
		atomic_store(&g_em.drop_eye, 0);
		frame();
		EXPECT(somvr_tag_get(&g_sh, atomic_load(&g_sh.tag_seq), &t) && t.eyes == 2u, "eyedrop 0 2: tag eyes %u", t.eyes);
		frame();
		EXPECT(somvr_tag_get(&g_sh, atomic_load(&g_sh.tag_seq), &t) && t.eyes == 2u, "eyedrop 0 2, 2nd: tag eyes %u", t.eyes);
		frame();
		check_eyes("eyedrop over", 3, 2);
		EXPECT(atomic_load(&g_em.drop_eye) == -1 && atomic_load(&g_em.dropped) == 2, "eyedrop: eye %d, dropped %llu",
		       atomic_load(&g_em.drop_eye), (unsigned long long)atomic_load(&g_em.dropped));
	}
	/* the 3D surface changes size (SetResolution): new textures, the old references dropped, a new gen */
	uint32_t gen0 = g_et.pub.gen;
	g_surf.desc.w = 1600, g_surf.desc.h = 1680;
	frame();
	check_eyes("new 3D surface size", 3, 2);
	EXPECT(g_creates == 4 && g_releases == 2 && g_et.pub.gen == gen0 + 3 && g_et.pub.w == 1600,
	       "re-made: creates %d releases %d gen %u->%u w %u", g_creates, g_releases, gen0, g_et.pub.gen, g_et.pub.w);
	/* the threaded context: CopyResource through its own slot 47 */
	st_ctx.vt = (uintptr_t)st_vt_ctx_mt;
	frame();
	check_eyes("threaded D3D11 context", 3, 2);
	st_ctx.vt = (uintptr_t)st_vt_ctx;
	/* the surface is not an IdxD3D11Texture2D (wrong vtable): eye copies off for the run, frames go on */
	g_surf.vt = 0x5555;
	frame();
	check_stereo("bad surface vtable", SOMVR_LAYOUT_B, 0);
	check_eyes("bad surface vtable", 0, 0);
	EXPECT(g_et.state == -1, "eye copies not off after a bad vtable");
	g_surf.vt = (uintptr_t)st_vt_tex;
	frame();
	check_eyes("off stays off", 0, 0);
	EXPECT(atomic_load(&g_sh.eyes.misses) >= 4, "misses %llu", (unsigned long long)atomic_load(&g_sh.eyes.misses));
	/* CreateTexture2D fails: off (white box: state reset to "not made") */
	g_et.state = 0;
	memset(&g_et.desc, 0, sizeof g_et.desc);
	g_create_fail = 1;
	frame();
	check_eyes("CreateTexture2D fails", 0, 0);
	EXPECT(g_et.state == -1, "eye copies not off after a failed CreateTexture2D");
	g_create_fail = 0;
	g_et.state = 0;
	frame();
	check_eyes("recovered (white box)", 3, 2);
	g_layout = SOMVR_LAYOUT_A;
	g_rigcfg.layout = SOMVR_LAYOUT_A;
	int c0 = (int)atomic_load(&g_sh.eyes.copies);
	frame();
	EXPECT((int)atomic_load(&g_sh.eyes.copies) == c0 && g_h[HK_H8].mods > 0, "layout A copied (%d -> %llu)", c0,
	       (unsigned long long)atomic_load(&g_sh.eyes.copies));

	/* the runtime fails: last good views for SOMVR_VIEWS_MAX_REUSE frames (flag REUSED), then flat */
	g_rt_result = XR_ERROR_RUNTIME_FAILURE;
	int stereo = 0;
	for (int i = 0; i < SOMVR_VIEWS_MAX_REUSE + 3; i++)
		stereo += frame() == 2;
	EXPECT(stereo == SOMVR_VIEWS_MAX_REUSE, "frames with reused views %d", stereo);
	check_mono("runtime failing");
	g_rt_result = XR_SUCCESS;
	frame();
	check_stereo("runtime recovered", SOMVR_LAYOUT_A, 0);
	atomic_store(&g_sh.xr.state, SOMVR_XRS_DEAD); /* the layer tore the session down */
	frame();
	check_mono("session dead");

	/* fake views: stereo without a runtime, the half-width fov for A; SOMVR_OVERSCAN's default there is 0, a set value
	 * widens the fake fov too */
	g_views_fake = 1;
	g_overscan = 0.2f;
	frame();
	check_stereo("fake views, overscan", SOMVR_LAYOUT_A, 1);
	EXPECT(NEAR(tanf(g_h4_view[0].fov_x * 0.5f), 1.2f * 0.5f * tanf(0.75f), 1e-6), "fake A fov with overscan 0.2");
	g_overscan = 0;
	frame();
	check_stereo("fake views", SOMVR_LAYOUT_A, 1);
	EXPECT(NEAR(tanf(g_h4_view[0].fov_x * 0.5f), 0.5f * tanf(0.75f), 1e-6), "fake A fov");

	/* the trigger gate and its words */
	snprintf(g_trig_path, sizeof g_trig_path, "%s", trig);
	set_trigger(trig, NULL);
	frame();
	check_mono("trigger file absent");
	set_trigger(trig, "on");
	frame();
	check_stereo("trigger file present", SOMVR_LAYOUT_A, 1);
	uint64_t rc0 = g_rig.recenters;
	set_trigger(trig, "turn+");
	frame();
	sv_vec3 f = sv_qrot(sv_quat_of(g_h4_view[0].quat), (sv_vec3){ 0, 0, 1 });
	EXPECT(g_rig.snap_steps == 1 && NEAR(atan2f(f.x, f.z), 0.5235988, 1e-5), "turn+: steps %d heading %g",
	       g_rig.snap_steps, atan2f(f.x, f.z));
	set_trigger(trig, "recenter");
	frame();
	EXPECT(g_rig.recenters == rc0 + 1, "recenter word: recentres %llu -> %llu", (unsigned long long)rc0,
	       (unsigned long long)g_rig.recenters);
	set_trigger(trig, "turn-");
	frame();
	EXPECT(g_rig.snap_steps == 0, "turn-: steps %d", g_rig.snap_steps);
	atomic_fetch_add(&g_sh.xr.recenter_seq, 1); /* the layer saw REFERENCE_SPACE_CHANGE_PENDING */
	frame();
	EXPECT(g_rig.recenters == rc0 + 2, "xr recenter_seq: recentres %llu", (unsigned long long)g_rig.recenters);
	set_trigger(trig, NULL);
	frame();
	check_mono("trigger file removed");
	g_trig_path[0] = 0;

	/* overlay scenes with the same camera (H3 repeats the eye's projection), and a type-1 viewport is left alone */
	g_h3_type = 1;
	frame();
	EXPECT(g_h4_tan[0][0] == tanf(0.5f * g_h4_view[0].fov_x) && g_h4_co[0][0] == 0, "ortho viewport changed");
	g_h3_type = 0;
	EXPECT(poke_buf[0] == 1 && poke_buf[1] == 0 && poke_buf[2] == 0 && poke_buf[3] == 0 && poke_buf[4] == 0,
	       "stereo did not force the comfort pokes");
	EXPECT(g_pm_calls == 0 && g_nodexf_calls == 0, "the camera base read the head or ran vf_11");
	audio_tests();
	head_tests();
	hv_tests();
	follow_tests();
	tether_hook_tests();
	combat_tests();
	react_tests();
	input_tests();
	ptr_tests();
	arms_tests();
	hb_tests();
	glyph_tests();
	res_tests();
	es_tests();
	hide_tests(trig);
	hud_tests();
	fb_tests(trig);
	comfort_tests();
	climb_tests();
	bow_tests();
	review_tests();
	allon_tests();
	vm_tests(argv[1]);
	loco_tests();
	step_tests();
	ab_tests(); /* build 17: A's one owner (the strike, the dodge, the climb) */
	guard_tests(argv[1]);
	df_tests(); /* before threat_tests: the threat's state from start (it restores it) */
	threat_tests();
	bm_tests();
	slots_tests();
	finish_tests();
	vb_tests();     /* SOMVR_VERBS: the left hand's grab (build 13; combat's fake world) */
	df_fx_tests();
	hp_tests(trig); /* SOMVR_HIDE_HEAD=parts (its hooks stay in, passing through) */
	pr_tests(trig); /* SOMVR_PRACTICE: last (the fallback's Player world, its own stand-ins) */
	bd_tests();     /* SOMVR_BODY=hands, SOMVR_SWORD_ANGLE, SOMVR_HANDS_TEST: their own stand-ins */
	bd_swing_tests(); /* SOMVR_HANDS_TEST: `hands swing` */
	bf_tests();     /* SOMVR_FINGERS: the fingers into the solved arms' pose, the menu's sword angle */
	vh_tests();     /* SOMVR_VRHANDS: the layer's own hands (build 10) */
	sf_tests();     /* SOMVR_HANDS_SURFACE: on the climb's stand-ins (build 13) */
	go_tests();     /* SOMVR_MUSIC / SOMVR_HINTS: their own stand-ins (build 10) */
	gs_tests();     /* build 20, SOMVR_SLOTS_GAMESOUND: stand-in objects and posts */
	pp_tests();     /* SOMVR_PAD_PROMPTS, SOMVR_BOW_STRIKE: their own stand-ins (the bow's round 2) */
	bh_tests();     /* SOMVR_BOW_HITLOG: a stand-in call (the bow's round 2) */
	uf_tests();     /* SOMVR_HUD_FX: a stand-in thunk (the build 12 HUD round) */
	vg_tests();     /* SOMVR_VERBS_GLOW: two stand-in thunks (build 13) */
	ff_tests();     /* SOMVR_FRONT: the wrappers on stand-ins (build 14) */
	py_tests();     /* SOMVR_PARRY / SOMVR_SHOVE: the window, the opening, the riposte (build 18) */
	flush_log();
	printf("RATE-like summary: stereo %llu mono %llu h3 %llu+%llu widen %llu stereo tags %llu\n",
	       (unsigned long long)atomic_load(&g_st_stereo), (unsigned long long)atomic_load(&g_st_mono),
	       (unsigned long long)atomic_load(&g_st_apply), (unsigned long long)atomic_load(&g_st_repeat),
	       (unsigned long long)atomic_load(&g_st_widen), (unsigned long long)atomic_load(&g_st_tags_stereo));
	printf("%s (%d failures); log %s\n", fails ? "SELFTEST FAILED" : "selftest ok", fails, logp);
	return fails != 0;
}
