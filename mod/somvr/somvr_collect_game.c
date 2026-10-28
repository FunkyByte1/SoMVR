/* somvr_collect_game.c - build 17 candidates: the collectibles a VR player could not use (the collect drill's round 2,
 * handoff/HANDOFF-collect.md "Round 2", docs/re-xr-pipeline.md 45.7-45.12). Included by somvr.c after the objective's
 * file (it uses bo_patch_call, check_site, write_code, peek, logf_, g_inp, game_paused, player_objs).
 *
 * SOMVR_INSPECT=off|log|on (default off): an artifact's inspect view (and any pause-menu view the game renders from a
 *   camera of its own: the artifact menu's stage is 383 m under the map, logs/boot-check-20261026-140239's H1 lines
 *   `pos=4659,-38301,1215` while `paused=1`). Our eyes stay at Talion's head, so the turned object was never seen
 *   (45.2). H1 keeps the game's own view of every frame (the SomView before our eyes change it); at H4's top, while
 *   the game is paused in a menu and that view is more than SOMVR_INSPECT_M (default 30) metres from Talion's root for
 *   2 frames, the frame renders flat (the game's own camera: the object, its backdrop and the menu's text, the frame
 *   the PC window shows) and the layer puts it on the menu board as any flat menu frame; 2 frames back near Talion (or
 *   the pause over) = our eyes again. The player's head never follows the game's camera: the board stays where the
 *   menu opened. log: the decisions logged, nothing rendered flat. The word `collect inspect flat|auto|never` forces
 *   it (tests). No site.
 *
 * SOMVR_GLYPH=off|log (default off; test-only for now): why an Ithildin glyph is never collected (45.3). Two hooks,
 *   both log only, the game's call passed on unchanged:
 *   CO_LAT_TICK = LookAtTrigger's vtable slot 13 (its tick 0x279bbd0): each trigger within SOMVR_GLYPH_M (default 40)
 *     metres of Talion: its state (+0xc8: 0 idle, 1 looking, 2 fired, 3 done), the dwell (+0xb8), the eye mode (+0xcc),
 *     the gates +0xd0/+0xd2/+0xd3, its place (*(+8)+8); while the wraith vision is on and it is within 15 m also the
 *     game's own tests called read-only: CO_LAT_SEE = FUN_02799e90(trigger, player) (the radii, the view test on the
 *     camera copy, the line of sight) and CO_LAT_VIEW = FUN_029a8130(place, player) (the view test alone);
 *   CO_VLOOK = the call 0x274f5fe of FUN_00ad2da0(the vision-mode look-at list at character+0x2f0, the player's
 *     targeting): the list's objects (registered by the scripts' StartVisionModeLookAt), its gates (+0x48 on, the
 *     thresholds +0x40/+0x44), the targeting's focus (+0x90), its measure (+0x3a0) and flag (+0x566): the event
 *     CCharacter.OnVisionModeLookAt fires when the focus is a listed object and the measure is within the threshold.
 *
 * SOMVR_GLYPH=head (build 18 candidate, docs/re-xr-pipeline.md 45.13): the Ithildin collected by looking at it. The
 *   glyph's LookAtTrigger script only marks it spotted (the Nurn objres' Lua: GlyphLocation_SO.Spotted, the item's
 *   SetLookAtEnabled); the flat game's collection is a combat attack ('Activate_GlyphCollectable' on an 'AutoNode'
 *   target) that never comes in VR, though the item passes the targeting's rules (run 20261027-191043: 'AutoNode'
 *   VALIDATED at 5.6 m). CO_TGEOM_A/B (the two calls of FUN_0274ed10, the player's targeting candidates' geometry,
 *   passed on) follow the InventoryItemUI candidates within SOMVR_GLYPH_M of Talion; a glyph's item ('Lore_Glyph_*')
 *   whose LookAtTrigger (within 3 m) has fired - the game's own look test with our head as the camera copy
 *   (SOMVR_FRONT_CAMCOPY=head) and the wraith vision on - is collected at the next input tick by CO_COLLECT, the
 *   game's InventoryItemUI Collect (Lua's item:Collect(character)): the record into the inventory, OnCollect, the
 *   game's own Ithildin page. No attack, no animation, no camera: the view stays ours. log: the item candidates and
 *   their target rules' answers (CO_TSCORE_1..6 = FUN_00a941b0's six calls, passed on) logged, nothing collected.
 *
 * SOMVR_TEXT_BUTTONS=FROM:TO[,FROM:TO...] (default unset = nothing patched; the headset's 122:119): the game's UI
 *   texts draw an action's button as the flat pad's binding of that command (`{Action}` tokens, FUN_00a15c20). The
 *   wraith vision is CombatSecondary01 122 = the pad's LB ("Enter the Wraith World (LB) ..."), but in VR it is held
 *   on Y. CO_TXT_ICON is ours: a token's command FROM looks up the icon of command TO (119 CombatPrimary02 = the pad's
 *   Y) - the icon only: the caller's command and every binding stay the game's. Each new token's command is logged.
 *
 * SOMVR_HINTS_KEEP=PREFIX[,PREFIX...] (default unset = build 15: SOMVR_HINTS=off drops every combat hint): the combat
 *   hints whose text key starts with one of the prefixes pass even with SOMVR_HINTS=off. A hint's record is a property
 *   table; its text key is its string property (FUN_00efcd10 reads it so): at an artifact 'Tut_Collectible_Artifact_
 *   Located', at an Ithildin 'Tut_Collectible_Glyph_Located' (run 20261026-161702): the game's own "use the wraith vision
 *   here" tutorial, shown once near such a place (3 s .. 12 s), not the fight's button hints SOMVR_HINTS=off is for. The
 *   headset's choice: SOMVR_HINTS_KEEP=Tut_Collectible_. Each new hint record's properties are logged once (SOMVR_HINTS
 *   log|off) and the hint lines carry the key.
 *
 * The word `collect camhead 0|1|2` (SOMVR_GLYPH=log, a test): after each stereo frame the player camera's final
 *   transform copy (CPlayerCamera +0x44c position, +0x458 rotation: what the targeting update FUN_0274ed10 and the
 *   scripts' Player.GetPos read; re-view-render.md) gets our eye view's pose (1), and +0x484 the same rotation (2); the
 *   camera's own update rewrites it each frame. Logs the copy against the game's view once.
 *
 * The word `collect camstick X Y MS` (SOMVR_GLYPH=log, a test): the virtual pad's right stick at X, Y (-1..1) for MS ms
 *   in the world, its bound commands past the deny list (the game's own camera turns; our view never: the HMD's) - to
 *   aim the game's camera at a place (scripts/glyph_aim.py).
 *
 * Words: `collect status`, `collect inspect flat|auto|never`, `collect camhead N`, `collect camstick X Y MS`. Fail
 * soft: a site mismatch = that hook off. */

/* COLLECT_SITES_BEGIN (check_sites.py counts this block on its own: SOMVR_GLYPH's patched slot CO_LAT_TICK =
 * LookAtTrigger's vtable 0x398ef30 slot 13 (the context: slots 12, 13), the patched call CO_VLOOK = 0x274f5fe in
 * FUN_0274ed10 (`mov rax,[r13]; mov rdi,[rax+0x2f0]; call FUN_00ad2da0`, %rsi = the targeting from [rbp+0x30]), the
 * called read-only CO_LAT_SEE = FUN_02799e90(trigger, player) -> bool and CO_LAT_VIEW = FUN_029a8130(float pos[3],
 * player) -> bool, the read-only proof CO_OBJ_PROPS = the object system's (vtable 0x3453058) slot 0xf8 = `mov rax,
 * [rsi+0xd0]; ret`: an object's property table is at +0xd0 (re-xr-pipeline.md 45.9); SOMVR_TEXT_BUTTONS' patched call
 * CO_TXT_ICON = 0xa15f6f in FUN_00a15c20 (the UI text's ReplaceActionsWithDevices: a `{Action}` token's command, from
 * FUN_025aa700, at [rsp+0x3c] -> FUN_00aaaf50(the player's input, &command) = the binding's button-icon record, whose
 * TextFieldGlyphReplacement is put in the text; re-xr-pipeline.md 45.10); SOMVR_GLYPH=head's patched calls CO_TGEOM_A/B
 * = 0xa9ca12 (FUN_00a9c8e0) and 0xa9cf2e (FUN_00a9ce70) of FUN_0274ed10(candidate, targeting context), and the
 * logged calls CO_TSCORE_1..6 of FUN_00a941b0(target record, context, candidate, float *score) -> bool: 0xa98942,
 * 0xa98a3c (FUN_00a98850), 0xa9865d, 0xa987b7 (FUN_00a98030), 0xa96310, 0xa9641c (FUN_00a961f0); re-xr-pipeline.md
 * 45.13); the called CO_COLLECT = FUN_02781370(item, character): InventoryItemUI's Collect (Lua's
 * `item:Collect(c)`, FUN_023f17a0): +0x68f set, the record into the character's inventory, OnCollect. Order = COS_*) */
static const struct site_spec g_co_sites_game[] = {
SITE("CO_LAT_TICK", K_VSLOT, 0x398ef98, 0x398ef90, "60 68 8c 00 00 00 00 00 d0 bb 79 02 00 00 00 00", 0x279bbd0, "53 48 89 fb 48 83 ec 30 80 bf d2 00 00 00 00 74"),
SITE("CO_VLOOK", K_CALL, 0x274f5fe, 0x274f5f3, "49 8b 45 00 48 8b b8 f0 02 00 00 e8 9d 37 38 fe", 0xad2da0, "80 7f 48 00 75 0a c3 66 0f 1f 84 00 00 00 00 00"),
SITE("CO_LAT_SEE", K_ENTRY, 0x2799e90, 0x2799e90, "48 85 f6 74 1e f6 86 78 01 00 00 01 75 15 48 8b", 0x2799e90, "48 85 f6 74 1e f6 86 78 01 00 00 01 75 15"),
SITE("CO_LAT_VIEW", K_ENTRY, 0x29a8130, 0x29a8130, "48 83 ec 38 48 8b 86 cc 0b 00 00 f3 0f 10 86 c8", 0x29a8130, "48 83 ec 38 48 8b 86 cc 0b 00 00 f3 0f 10"),
SITE("CO_OBJ_PROPS", K_VSLOT, 0x3453150, 0x3453140, "b0 70 8c 00 00 00 00 00 d0 74 8c 00 00 00 00 00 d0 6a 8c 00 00 00 00 00", 0x8c6ad0, "48 8b 86 d0 00 00 00 c3"),
SITE("CO_TXT_ICON", K_CALL, 0xa15f6f, 0xa15f65, "48 8d 74 24 3c 44 89 54 24 18 e8 dc 4f 09 00", 0xaaaf50, "48 8b 4f 60 48 8b 47 68 8b 16 48 29 c8 48 c1 f8"),
SITE("CO_TGEOM_A", K_CALL, 0xa9ca12, 0xa9ca07, "02 00 00 48 8b 74 24 08 4c 89 ff e8 f9 22 cb 01", 0x274ed10, "48 8b 4f 30 48 85 c9 0f 84 53 0a 00 00 41 57 41"),
SITE("CO_TGEOM_B", K_CALL, 0xa9cf2e, 0xa9cf23, "b8 f8 02 00 00 48 89 ee 4c 89 ff e8 dd 1d cb 01", 0x274ed10, "48 8b 4f 30 48 85 c9 0f 84 53 0a 00 00 41 57 41"),
SITE("CO_TSCORE_1", K_CALL, 0xa98942, 0xa98937, "4c 89 f2 c7 44 24 1c 00 00 00 00 e8 69 b8 ff ff", 0xa941b0, "41 57 41 56 49 89 f6 41 55 49 89 cd 41 54 49 89"),
SITE("CO_TSCORE_2", K_CALL, 0xa98a3c, 0xa98a31, "4c 89 f2 c7 44 24 1c 00 00 00 00 e8 6f b7 ff ff", 0xa941b0, "41 57 41 56 49 89 f6 41 55 49 89 cd 41 54 49 89"),
SITE("CO_TSCORE_3", K_CALL, 0xa9865d, 0xa98652, "48 89 ea c7 44 24 1c 00 00 00 00 e8 4e bb ff ff", 0xa941b0, "41 57 41 56 49 89 f6 41 55 49 89 cd 41 54 49 89"),
SITE("CO_TSCORE_4", K_CALL, 0xa987b7, 0xa987ac, "48 89 ea c7 44 24 1c 00 00 00 00 e8 f4 b9 ff ff", 0xa941b0, "41 57 41 56 49 89 f6 41 55 49 89 cd 41 54 49 89"),
SITE("CO_TSCORE_5", K_CALL, 0xa96310, 0xa96305, "4c 89 ea c7 44 24 0c 00 00 00 00 e8 9b de ff ff", 0xa941b0, "41 57 41 56 49 89 f6 41 55 49 89 cd 41 54 49 89"),
SITE("CO_TSCORE_6", K_CALL, 0xa9641c, 0xa96411, "4c 89 ea c7 44 24 0c 00 00 00 00 e8 8f dd ff ff", 0xa941b0, "41 57 41 56 49 89 f6 41 55 49 89 cd 41 54 49 89"),
SITE("CO_COLLECT", K_ENTRY, 0x2781370, 0x2781370, "48 85 f6 0f 84 87 00 00 00 80 bf 8f 06 00 00 00", 0x2781370, "48 85 f6 0f 84 87 00 00 00 80 bf 8f 06 00"),
};
/* COLLECT_SITES_END */
enum { COS_LAT_TICK, COS_VLOOK, COS_LAT_SEE, COS_LAT_VIEW, COS_OBJ_PROPS, COS_TXT_ICON, COS_TGEOM_A, COS_TGEOM_B,
       COS_TSCORE_1, COS_TSCORE_6 = COS_TSCORE_1 + 5, COS_COLLECT, COS_N };
_Static_assert(sizeof g_co_sites_game / sizeof g_co_sites_game[0] == COS_N, "collect sites");
static const struct site_spec *g_co_sites = g_co_sites_game; /* selftest: stand-ins */
#define CO_PLAYERS 0x4c47020u /* the player list (a std::vector of CPlayerObj*; FUN_0279b7a0 walks it) */
#define CO_LAT_STATE 0xc8
#define CO_LAT_DWELL 0xb8
#define CO_LAT_MODE 0xcc
#define CO_LAT_RADIUS 0xb0
#define CO_LAT_LOOKER 0x80
#define CO_OBJ_PROPTBL 0xd0 /* an engine object's property table (CO_OBJ_PROPS) */
#define CO_VL_ON 0x48
#define CO_VL_TARGET 0x90
#define CO_VL_MEASURE 0x3a0
#define CO_VL_FLAG 0x566
#define CO_TRIGS 48 /* triggers followed (logs) */
#define CO_RECS 32  /* hint records dumped / their keys cached */
#define CO_KEEP 8   /* SOMVR_HINTS_KEEP prefixes */
#define CO_C_OBJ 0x68        /* a targeting candidate (CombatHost+0x2f8): its object (FUN_00a941b0's name) */
#define CO_C_ONSCREEN 0xb9   /* FUN_0274ed10: FUN_00a60920's on-screen answer */
#define CO_ITEM_CLASS 0x4c464e0u /* InventoryItemUI's class in an object's class chain *(obj+0x48) (FUN_023f1bb0) */
#define CO_ITEM_LOOKAT 0x68d /* InventoryItemUI: SetLookAtEnabled's byte (FUN_02781350) */
#define CO_ITEM_TAKEN 0x68f  /* InventoryItemUI: collected (FUN_02781370, vf_31) */
#define CO_ITEM_REC 0x5c8    /* InventoryItemUI: its inventory record (FUN_02781370) */
#define CO_ITEMS 16          /* item candidates followed (logs) */
#define CO_CHAR_CLASS 0x4c41500u /* CCharacter's class in an object's class chain (FUN_02781370's check) */
#define CO_SEEN_CM 300.f     /* a glyph's LookAtTrigger within this of its item (round 2: 1 m) */

typedef uint32_t (*co_tick_fn)(uintptr_t trig);
typedef void (*co_vlook_fn)(uintptr_t list, uintptr_t targ);
typedef uintptr_t (*co_icon_fn)(uintptr_t input, uint32_t *cmd);
#define CO_TXMAP 8
struct co_trig { uintptr_t t; int state, d0, d2, see, view; double next; uint32_t id; float pos[3]; };
typedef int (*co_see_fn)(uintptr_t trig, uintptr_t player);
typedef int (*co_view_fn)(const float *pos, uintptr_t player);
typedef void (*co_geom_fn)(uintptr_t cand, uintptr_t ctx);
typedef uint64_t (*co_score_fn)(uintptr_t rec, uintptr_t ctx, uintptr_t cand, float *score);
typedef void (*co_collect_fn)(uintptr_t item, uintptr_t character);
struct co_item { uintptr_t obj, item; double next; uint8_t onscreen, lookat; uint32_t id; char rec[48]; uint64_t rv;
		 char name[48]; int glyph; };

static struct {
	/* SOMVR_INSPECT */
	int ins_mode;       /* 0 off, 1 log, 2 on */
	_Atomic int ins_force; /* the word: 0 auto, 1 flat while paused, 2 never */
	float ins_far_cm;
	int ins_tid;
	int have_view;      /* H1's game view of the last H4 */
	float view[3], vq[4];
	float root[3];
	int have_root;
	int flat, far_n, near_n;
	double flat_t0, flat_s, next_log;
	float last_d;
	uint64_t n_flats, n_flat_frames, n_far_frames, n_paused_frames;
	uint32_t ins_lines;
	/* SOMVR_GLYPH */
	int gl_mode;        /* 0 off, 1 log, 2 head */
	float gl_far_cm;
	int ok[COS_N];
	int on_tick, on_vlook;
	uintptr_t tick_orig;
	struct co_trig tr[CO_TRIGS];
	int ntr;
	uint32_t next_id;
	uint64_t n_ticks, n_near, n_vlook, n_vlook_on;
	uint32_t gl_lines, vl_lines;
	double vl_next;
	uintptr_t vl_last_target;
	int vl_last_n, vl_last_on;
	uintptr_t cam;      /* H1's CPlayerCamera */
	_Atomic uint32_t cs_x, cs_y, cs_ms; /* the word `collect camstick` (float bits, ms) */
	_Atomic uint32_t cs_seq;
	uint32_t cs_seen;
	float csx, csy;
	double cs_until;
	int cs_on;          /* this tick's pad RX/RY are ours */
	uint64_t n_cs;
	_Atomic int camhead; /* the word: 0 off, 1 pos+rot, 2 + the second rotation */
	uint64_t n_camhead;
	int camhead_logged;
	/* SOMVR_GLYPH=head / log: the item candidates of the player's targeting */
	int on_geom, on_score;
	uint64_t n_geom_items, n_scores, n_valid;
	uint32_t it_lines, next_item;
	uint32_t sc_lines;
	uintptr_t pend_item, pend_obj; /* a spotted glyph's item: collected at the next input tick */
	uint32_t pend_id;
	uint64_t n_collects;
	struct co_item it[CO_ITEMS];
	int nit;
	/* SOMVR_TEXT_BUTTONS */
	int tx_n, on_txt;
	uint32_t tx_from[CO_TXMAP], tx_to[CO_TXMAP];
	uint32_t tx_seen[64];
	_Atomic int tx_nseen;
	_Atomic uint64_t n_tx, n_tx_mapped;
	/* the hint records */
	uintptr_t rec[CO_RECS];
	char key[CO_RECS][64];
	int nrec;
	char keep[CO_KEEP][48];
	int nkeep;
	uint64_t n_kept;
} g_co;

static void co_props(uintptr_t tbl, char *s, size_t cap, char *key, size_t kcap);
static int co_log_ok(void) { return g_co.gl_lines < 800 && ++g_co.gl_lines; }      /* the triggers' lines */
static int co_vl_log_ok(void) { return g_co.vl_lines < 300 && ++g_co.vl_lines; }   /* the vision look-at's */

/* Talion's position for the logs: the first player's object (the list LookAtTrigger walks) */
static int co_player(uintptr_t *p, float pos[3])
{
	uintptr_t b = 0, e = 0, o;
	*p = 0;
	if (!peek(CO_PLAYERS, &b, sizeof b) || !peek(CO_PLAYERS + 8, &e, sizeof e) || !b || e <= b || e - b > 64)
		return 0;
	*p = peek_ptr(b);
	o = *p ? peek_ptr(*p + 8) : 0;
	return o && peek(o + OBJ_POS, pos, 12) && vec_ok(pos, 1e7f);
}

static int co_vision(uintptr_t p)
{
	uintptr_t v = p ? peek_ptr(p + 0x10) : 0;
	uint8_t b = 0;
	return v && peek(v + 0x118, &b, 1) ? b : -1;
}

/* ---- SOMVR_INSPECT: H1 (every call, the game thread inside H4): the game's own view of this frame ---- */
static void co_h1(uintptr_t cam, const somvr_view *in)
{
	g_co.cam = cam;
	if (!g_co.ins_mode || my_tid() != g_co.ins_tid || !vec_ok(in->pos, 1e7f))
		return;
	memcpy(g_co.view, in->pos, sizeof g_co.view), memcpy(g_co.vq, in->quat, sizeof g_co.vq);
	g_co.have_view = 1;
}

/* H4's top, after the body moments: 1 = this frame renders flat (the game's camera shows something away from Talion) */
static int co_pre(uintptr_t pl)
{
	if (!g_co.ins_mode)
		return 0;
	g_co.ins_tid = my_tid();
	double t = mono();
	uintptr_t pm, obj;
	g_co.have_root = pl && player_objs(pl, &pm, &obj) && peek(obj + OBJ_POS, g_co.root, sizeof g_co.root) &&
			 vec_ok(g_co.root, 1e7f);
	int paused = game_paused(), menu = !g_inp.on || g_inp.ctx == SC_CTX_MENU, force = atomic_load(&g_co.ins_force);
	float d = g_co.have_view && g_co.have_root ? dist3(g_co.view, g_co.root) : -1;
	int far = paused && menu && d > g_co.ins_far_cm;
	if (force == 1)
		far = paused;
	else if (force == 2)
		far = 0;
	g_co.n_paused_frames += paused, g_co.n_far_frames += far;
	g_co.far_n = far ? g_co.far_n + 1 : 0, g_co.near_n = far ? 0 : g_co.near_n + 1;
	int was = g_co.flat;
	if (!g_co.flat && g_co.far_n >= 2)
		g_co.flat = 1;
	else if (g_co.flat && (!paused || g_co.near_n >= 2))
		g_co.flat = 0;
	if (g_co.flat != was) {
		if (g_co.flat)
			g_co.n_flats++, g_co.flat_t0 = t;
		if (g_co.flat && g_co.ins_mode == 2) /* SOMVR_EYE_SIZE's switch: the game's size at the next tick, not after its */
			g_es.last_want = 0;          /* 2 s hold (2 s of the 3D squeezed into the tall eye size on the board) */
		else
			g_co.flat_s += t - g_co.flat_t0;
		if (g_co.ins_lines++ < 200)
			logf_("INSPECT t=%.3f %s: the game's camera %.1f m from Talion (at %.0f %.0f %.0f; Talion %.0f %.0f %.0f), paused=%d "
			      "ctx=%s%s: %s", t, g_co.flat ? "flat" : "eyes", d * 0.01f, g_co.view[0], g_co.view[1], g_co.view[2],
			      g_co.root[0], g_co.root[1], g_co.root[2], paused, menu ? "menu" : "world",
			      force == 1 ? " (forced flat)" : force == 2 ? " (forced never)" : "",
			      g_co.flat ? (g_co.ins_mode == 2 ? "the game's own view on the menu board" : "would be flat (log only)")
					: "our eyes again");
	} else if (paused && (fabsf(d - g_co.last_d) > 100 || t >= g_co.next_log) && g_co.ins_lines < 200) {
		g_co.ins_lines++;
		logf_("INSPECT t=%.3f paused: the game's camera %.1f m from Talion (at %.0f %.0f %.0f) ctx=%s; %s", t, d * 0.01f,
		      g_co.view[0], g_co.view[1], g_co.view[2], menu ? "menu" : "world", g_co.flat ? "flat" : "eyes");
		g_co.next_log = t + 5.0;
	}
	if (paused)
		g_co.last_d = d;
	g_co.n_flat_frames += g_co.flat;
	return g_co.ins_mode == 2 && g_co.flat;
}

static int co_flat_now(void) { return g_co.ins_mode == 2 && g_co.flat; }

/* the input tick (game thread), after the crouch's: the test word `collect camstick X Y MS` - the pad's right stick */
static void co_collect_tick(void); /* SOMVR_GLYPH=head: a spotted glyph's collect */
static void co_tick(sc_tick_out *o, double t)
{
	co_collect_tick();
	uint32_t q = atomic_load_explicit(&g_co.cs_seq, memory_order_acquire);
	if (q != g_co.cs_seen) {
		uint32_t bx = atomic_load(&g_co.cs_x), by = atomic_load(&g_co.cs_y);
		g_co.cs_seen = q;
		memcpy(&g_co.csx, &bx, 4), memcpy(&g_co.csy, &by, 4);
		g_co.cs_until = t + 1e-3 * atomic_load(&g_co.cs_ms);
		logf_("GLYPH camstick t=%.3f the pad's right stick %.2f %.2f for %u ms (the game's camera)", t, g_co.csx, g_co.csy,
		      atomic_load(&g_co.cs_ms));
	}
	g_co.cs_on = t < g_co.cs_until && g_inp.ctx == SC_CTX_WORLD;
	if (!g_co.cs_on)
		return;
	o->pad[SC_PAD_RX] = g_co.csx, o->pad[SC_PAD_RY] = g_co.csy;
	o->pad_on |= (g_co.csx != 0 ? 1u << SC_PAD_RX : 0) | (g_co.csy != 0 ? 1u << SC_PAD_RY : 0);
	g_co.n_cs++;
}
static int co_cam_pad(int i) { return g_co.cs_on && (i == SC_PAD_RX || i == SC_PAD_RY); }

/* H4, after a stereo frame's eyes (the test word `collect camhead N`): eye 0's view pose into the camera's copy */
static void co_post(void)
{
	int m = atomic_load_explicit(&g_co.camhead, memory_order_relaxed);
	uintptr_t c = g_co.cam;
	if (!m || !c || !g_fr.rec || !(g_fr.written & 1u))
		return;
	const somvr_eye_rec *e = &g_fr.rec->eye[0];
	float pos[3], q[4];
	memcpy(pos, e->som_pos, sizeof pos), memcpy(q, e->som_quat, sizeof q);
	if (!vec_ok(pos, 1e7f))
		return;
	if (!g_co.camhead_logged++) {
		float f[24];
		if (peek(c + 0x44c, f, sizeof f))
			logf_("GLYPH camhead: the camera %#lx +0x44c..: %.1f %.1f %.1f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f "
			      "| +0x478 %.3f %.3f %.3f | +0x484 %.3f %.3f %.3f %.3f; the game's view %.1f %.1f %.1f q %.3f %.3f %.3f %.3f; "
			      "eye 0 %.1f %.1f %.1f q %.3f %.3f %.3f %.3f", (unsigned long)c, f[0], f[1], f[2], f[3], f[4], f[5], f[6],
			      f[7], f[8], f[9], f[10], f[11], f[12], f[13], f[14], f[15], f[16], f[17], g_fr.game.pos[0],
			      g_fr.game.pos[1], g_fr.game.pos[2], g_fr.game.quat[0], g_fr.game.quat[1], g_fr.game.quat[2],
			      g_fr.game.quat[3], pos[0], pos[1], pos[2], q[0], q[1], q[2], q[3]);
	}
	memcpy((void *)(c + 0x44c), pos, sizeof pos);
	memcpy((void *)(c + 0x458), q, sizeof q);
	if (m == 2)
		memcpy((void *)(c + 0x484), q, sizeof q);
	g_co.n_camhead++;
}

/* ---- SOMVR_GLYPH: the LookAtTriggers near Talion ---- */
__attribute__((used)) uint32_t somvr_w_colat(uintptr_t trig)
{
	uint32_t rv = ((co_tick_fn)g_co.tick_orig)(trig);
	g_co.n_ticks++;
	uintptr_t p, o = peek_ptr(trig + 8);
	float me[3], pos[3];
	if (!o || !peek(o + OBJ_POS, pos, sizeof pos) || !vec_ok(pos, 1e7f) || !co_player(&p, me))
		return rv;
	float d = dist3(me, pos);
	if (d > g_co.gl_far_cm)
		return rv;
	g_co.n_near++;
	int k = 0;
	while (k < g_co.ntr && g_co.tr[k].t != trig)
		k++;
	if (k == g_co.ntr) {
		if (g_co.ntr == CO_TRIGS)
			return rv;
		memset(&g_co.tr[k], 0, sizeof g_co.tr[k]);
		g_co.tr[k].t = trig, g_co.tr[k].state = -1, g_co.tr[k].id = ++g_co.next_id, g_co.ntr++;
		if (g_co.ok[COS_OBJ_PROPS] && co_log_ok()) { /* its settings: the radii, the threshold, LookAtScript ... */
			char s[1000];
			co_props(peek_ptr(o + CO_OBJ_PROPTBL), s, sizeof s, NULL, 0);
			logf_("GLYPH trigger #%u %#lx object %#lx at (%.0f %.0f %.0f):%s", g_co.tr[k].id, (unsigned long)trig,
			      (unsigned long)o, pos[0], pos[1], pos[2], s);
		}
	}
	int32_t st = -1, mode = -1;
	float dwell = 0, rad = 0;
	uint8_t d0 = 0, d2 = 0, d3 = 0;
	uintptr_t looker = peek_ptr(trig + CO_LAT_LOOKER);
	peek(trig + CO_LAT_STATE, &st, 4), peek(trig + CO_LAT_DWELL, &dwell, 4), peek(trig + CO_LAT_MODE, &mode, 4);
	peek(trig + CO_LAT_RADIUS, &rad, 4), peek(trig + 0xd0, &d0, 1), peek(trig + 0xd2, &d2, 1), peek(trig + 0xd3, &d3, 1);
	int vis = co_vision(p), see = -1, view = -1;
	if (vis > 0) {
		if (g_co.ok[COS_LAT_SEE])
			see = ((co_see_fn)g_co_sites[COS_LAT_SEE].target)(trig, p) & 0xff;
		if (g_co.ok[COS_LAT_VIEW])
			view = ((co_view_fn)g_co_sites[COS_LAT_VIEW].target)(pos, p) & 0xff;
	}
	double t = mono();
	struct co_trig *r = &g_co.tr[k];
	if ((st != r->state || d0 != r->d0 || d2 != r->d2 || see != r->see || view != r->view || t >= r->next) && co_log_ok()) {
		float cp[3] = { 0, 0, 0 };
		peek(p + 0xbb0, cp, sizeof cp);
		logf_("GLYPH t=%.3f trigger #%u %#lx at (%.0f %.0f %.0f) %.1f m from Talion (%.0f %.0f %.0f): state %d dwell %.2f s "
		      "eye mode %d radius %.0f gates d0=%u d2=%u d3=%u looker %#lx; vision %d; the game's tests: see %d view %d "
		      "(the camera copy at %.0f %.0f %.0f, %.1f m from it)", t, r->id, (unsigned long)trig, pos[0], pos[1], pos[2],
		      d * 0.01f, me[0], me[1], me[2], st, dwell, mode, rad, d0, d2, d3, (unsigned long)looker, vis, see, view, cp[0],
		      cp[1], cp[2], dist3(cp, pos) * 0.01f);
		r->next = t + 3.0;
	}
	r->state = st, r->d0 = d0, r->d2 = d2, r->see = see, r->view = view;
	memcpy(r->pos, pos, sizeof r->pos);
	return rv;
}

/* the vision-mode look-at (every call of FUN_00ad2da0 from the player's targeting update) */
__attribute__((used)) void somvr_w_covlook(uintptr_t list, uintptr_t targ)
{
	g_co.n_vlook++;
	uint8_t on = 0, flag = 0;
	float th[2] = { 0, 0 }, meas = 0;
	uintptr_t b = 0, e = 0, tgt = 0;
	if (list && peek(list + CO_VL_ON, &on, 1) && on) {
		g_co.n_vlook_on++;
		peek(list + 0x10, &b, 8), peek(list + 0x18, &e, 8), peek(list + 0x40, th, sizeof th);
		if (targ)
			tgt = peek_ptr(targ + CO_VL_TARGET), peek(targ + CO_VL_MEASURE, &meas, 4), peek(targ + CO_VL_FLAG, &flag, 1);
		int n = !b && !e ? 0 : b && e >= b && e - b <= 0x20 * 256 ? (int)((e - b) / 0x20) : -1;
		double t = mono();
		if (((n > 0 && (tgt != g_co.vl_last_target || n != g_co.vl_last_n)) || t >= g_co.vl_next) && co_vl_log_ok()) {
			uintptr_t p;
			float me[3] = { 0, 0, 0 }, tp[3] = { 0, 0, 0 };
			co_player(&p, me);
			uintptr_t to = tgt ? peek_ptr(tgt + 8) : 0;
			if (to)
				peek(to + OBJ_POS, tp, sizeof tp);
			char s[600];
			size_t o = 0;
			s[0] = 0;
			for (int i = 0; i < n && i < 8 && o < sizeof s - 80; i++) {
				uintptr_t ob = peek_ptr(b + 0x20 * (uintptr_t)i + 0x18);
				float op[3] = { 0, 0, 0 };
				if (ob)
					peek(ob + OBJ_POS, op, sizeof op);
				o += (size_t)snprintf(s + o, sizeof s - o, " [%d] %#lx at (%.0f %.0f %.0f) %.1f m", i, (unsigned long)ob,
						      op[0], op[1], op[2], dist3(op, me) * 0.01f);
			}
			logf_("GLYPH t=%.3f vision look-at: on, %d listed:%s; thresholds %.3f / %.3f; the focus %#lx (object %#lx at "
			      "%.0f %.0f %.0f) measure %.3f flag %u; Talion at (%.0f %.0f %.0f) vision %d", t, n, s, th[0], th[1],
			      (unsigned long)tgt, (unsigned long)to, tp[0], tp[1], tp[2], meas, flag, me[0], me[1], me[2], co_vision(p));
			g_co.vl_next = t + 5.0;
		}
		g_co.vl_last_target = tgt, g_co.vl_last_n = n;
	}
	g_co.vl_last_on = on;
	((co_vlook_fn)g_co_sites[COS_VLOOK].target)(list, targ);
}

/* ---- SOMVR_GLYPH=head / log: the player's targeting of a collectible item (the game thread, PlayerCombat's update) ---- */
/* an object's InventoryItemUI (its class chain *(obj+0x48) holds CO_ITEM_CLASS; the item at +0x38), else 0 */
static uintptr_t co_item_of(uintptr_t obj)
{
	uintptr_t h = obj ? peek_ptr(obj + 0x48) : 0, c = h ? peek_ptr(h) : 0;
	for (int i = 0; c && i < 16; i++, c = peek_ptr(c + 8))
		if (c == CO_ITEM_CLASS)
			return peek_ptr(obj + 0x38);
	return 0;
}

/* the item candidate's log record (followed by object), or NULL when full */
static struct co_item *co_item_rec(uintptr_t obj, uintptr_t item)
{
	for (int i = 0; i < g_co.nit; i++)
		if (g_co.it[i].obj == obj)
			return &g_co.it[i];
	if (g_co.nit == CO_ITEMS)
		return NULL;
	struct co_item *r = &g_co.it[g_co.nit++];
	memset(r, 0, sizeof *r);
	r->obj = obj, r->item = item, r->id = ++g_co.next_item, r->onscreen = 0xff, r->rv = ~0ull;
	char nm[48];
	rx_rec_name(peek_ptr(item + CO_ITEM_REC), nm, sizeof nm);
	snprintf(r->name, sizeof r->name, "%s", nm);
	r->glyph = !strncmp(nm, "Lore_Glyph_", 11); /* the Ithildin's inventory records (the game database) */
	float op[3] = { 0, 0, 0 };
	peek(obj + OBJ_POS, op, sizeof op);
	if (g_co.it_lines++ < 300)
		logf_("GLYPH item #%u: a targeting candidate InventoryItemUI %#lx (object %#lx at %.0f %.0f %.0f), its record '%s'",
		      r->id, (unsigned long)item, (unsigned long)obj, op[0], op[1], op[2], nm);
	return r;
}

/* CO_TGEOM_A/B: FUN_0274ed10(candidate, context) - the candidate's distance, angles and on-screen answer */
__attribute__((used)) void somvr_w_cogeom(uintptr_t cand, uintptr_t ctx)
{
	((co_geom_fn)g_co_sites[COS_TGEOM_A].target)(cand, ctx);
	uintptr_t obj = cand ? peek_ptr(cand + CO_C_OBJ) : 0, item = co_item_of(obj), p = 0;
	uint8_t taken = 1, lookat = 0;
	int vis = -1;
	float me[3], op[3];
	if (item && peek(item + CO_ITEM_TAKEN, &taken, 1) && !taken && (!co_player(&p, me) ||
	    !peek(obj + OBJ_POS, op, sizeof op) || dist3(me, op) > g_co.gl_far_cm))
		item = 0; /* only the items near Talion (SOMVR_GLYPH_M): the targeting also weighs artifacts 800 m away */
	if (item && !taken) {
		g_co.n_geom_items++;
		peek(item + CO_ITEM_LOOKAT, &lookat, 1);
		vis = co_vision(p);
	}
	if (!item || taken)
		return;
	struct co_item *r = co_item_rec(obj, item);
	uint8_t on = 0;
	float f[8] = { 0 };
	peek(cand + CO_C_ONSCREEN, &on, 1), peek(cand + 0x80, f, sizeof f);
	double t = mono();
	if (r && (on != r->onscreen || lookat != r->lookat || t >= r->next) && g_co.it_lines < 300) {
		g_co.it_lines++;
		logf_("GLYPH t=%.3f item #%u geometry: on screen %u (was %d), distance %.1f m, angles %.1f / %.1f deg; "
		      "look-at enabled %u; vision %d; %s", t, r->id, on, r->onscreen == 0xff ? -1 : r->onscreen, f[0] * 0.01f, f[1],
		      f[5], lookat, vis, g_co.gl_mode == 2 ? "head" : "log");
		r->onscreen = on, r->lookat = lookat, r->next = t + 3.0;
	}
	/* head: a glyph whose LookAtTrigger has fired with our head (the game's own look test, the vision on) is collected
	 * at the next input tick (co_collect_tick); the item's look-at byte alone is not used (its start value is data) */
	if (g_co.gl_mode == 2 && r && r->glyph && vis == 1 && !g_co.pend_item && g_co.ok[COS_COLLECT]) {
		int seen = 0;
		for (int i = 0; i < g_co.ntr && !seen; i++)
			seen = g_co.tr[i].state >= 2 && dist3(g_co.tr[i].pos, op) < CO_SEEN_CM;
		if (seen)
			g_co.pend_item = item, g_co.pend_obj = obj, g_co.pend_id = r->id;
	}
}

/* the input tick (game thread, outside the targeting's loops): a spotted glyph collected the way the game's scripts
 * collect an item (Lua's InventoryItemUI:Collect(character) = FUN_02781370): no attack, no animation, no camera */
static void co_collect_tick(void)
{
	uintptr_t item = g_co.pend_item, obj = g_co.pend_obj, p, ch;
	if (!item)
		return;
	g_co.pend_item = 0;
	float me[3];
	uint8_t taken = 1;
	int ok = co_item_of(obj) == item && peek(item + CO_ITEM_TAKEN, &taken, 1) && !taken && co_player(&p, me) &&
		 co_vision(p) == 1 && (ch = peek_ptr(p + 8)) != 0;
	int isch = 0;
	if (ok) { /* FUN_02781370 takes the character whose object's class chain holds CCharacter */
		uintptr_t h = peek_ptr(ch + 0x48), c = h ? peek_ptr(h) : 0;
		for (int i = 0; c && i < 16 && !isch; i++, c = peek_ptr(c + 8))
			isch = c == CO_CHAR_CLASS;
	}
	if (!ok || !isch) {
		if (g_co.it_lines++ < 300)
			logf_("GLYPH collect: item #%u %#lx not collected (%s)", g_co.pend_id, (unsigned long)item,
			      !ok ? "taken, gone, or the vision off" : "the player's object has no CCharacter class");
		return;
	}
	((co_collect_fn)g_co_sites[COS_COLLECT].target)(item, p);
	peek(item + CO_ITEM_TAKEN, &taken, 1);
	g_co.n_collects += taken != 0;
	char nm[48];
	rx_rec_name(peek_ptr(item + CO_ITEM_REC), nm, sizeof nm);
	logf_("GLYPH collect t=%.3f: item #%u %#lx '%s' collected by our look (the trigger fired with our head, the vision "
	      "on): InventoryItemUI Collect(player %#lx) -> taken %u", mono(), g_co.pend_id, (unsigned long)item, nm,
	      (unsigned long)p, taken);
}

/* CO_TSCORE_1..6: FUN_00a941b0(target record, context, candidate, &score) - a target record's rules (logged, passed) */
__attribute__((used)) uint64_t somvr_w_coscore(uintptr_t rec, uintptr_t ctx, uintptr_t cand, float *score)
{
	uint64_t rv = ((co_score_fn)g_co_sites[COS_TSCORE_1].target)(rec, ctx, cand, score);
	uintptr_t obj = cand ? peek_ptr(cand + CO_C_OBJ) : 0, item = co_item_of(obj), p;
	float me[3], op[3];
	if (!item || !co_player(&p, me) || !peek(obj + OBJ_POS, op, sizeof op) || dist3(me, op) > g_co.gl_far_cm)
		return rv;
	g_co.n_scores++;
	g_co.n_valid += (rv & 0xff) != 0;
	struct co_item *r = co_item_rec(obj, item);
	char nm[48];
	rx_rec_name(rec, nm, sizeof nm);
	double t = mono();
	if (r && ((rv & 0xff) != r->rv || strcmp(nm, r->rec)) && g_co.sc_lines < 120) {
		float sc = 0;
		if (score)
			peek((uintptr_t)score, &sc, 4);
		uint8_t on = 0;
		peek(cand + CO_C_ONSCREEN, &on, 1);
		g_co.sc_lines++;
		logf_("GLYPH t=%.3f item #%u target record '%s': %s (score %.3f; on screen %u)", t, r->id, nm,
		      (rv & 0xff) ? "VALIDATED" : "rejected", sc, on);
		r->rv = rv & 0xff, snprintf(r->rec, sizeof r->rec, "%s", nm);
	}
	return rv;
}

/* ---- SOMVR_TEXT_BUTTONS: a UI text's `{Action}` token's button icon (any thread that builds UI text) ---- */
__attribute__((used)) uintptr_t somvr_w_cotxt(uintptr_t input, uint32_t *cmd)
{
	uint32_t c = 0, m;
	atomic_fetch_add_explicit(&g_co.n_tx, 1, memory_order_relaxed);
	if (!cmd || !peek((uintptr_t)cmd, &c, sizeof c))
		return ((co_icon_fn)g_co_sites[COS_TXT_ICON].target)(input, cmd);
	int k = 0;
	while (k < g_co.tx_n && g_co.tx_from[k] != c)
		k++;
	int ns = atomic_load_explicit(&g_co.tx_nseen, memory_order_relaxed), seen = 0;
	for (int i = 0; i < ns && i < 64; i++)
		seen |= g_co.tx_seen[i] == c;
	if (!seen && ns < 64) {
		g_co.tx_seen[ns] = c;
		atomic_store_explicit(&g_co.tx_nseen, ns + 1, memory_order_relaxed);
		logf_("TEXTBUTTONS a UI text's token: command %u '%s'%s%s", c, in_cmd_name((int)c), k < g_co.tx_n ?
		      " -> the icon of " : " (kept)", k < g_co.tx_n ? in_cmd_name((int)g_co.tx_to[k]) : "");
	}
	if (k == g_co.tx_n)
		return ((co_icon_fn)g_co_sites[COS_TXT_ICON].target)(input, cmd);
	atomic_fetch_add_explicit(&g_co.n_tx_mapped, 1, memory_order_relaxed);
	m = g_co.tx_to[k]; /* the icon only: the caller's command stays */
	return ((co_icon_fn)g_co_sites[COS_TXT_ICON].target)(input, &m);
}

/* ---- the hint records (gameopts' delegate, SOMVR_HINTS=log|off): a new combat hint record's properties, once. A
 * database record is a property table (FUN_00efcd10 reads the hint's text so): byte 0 the count, +8 the entries (0x10
 * each: the name's hash at +0, the type at +10 & 0x3f, the value's index at +0xe), +0x10 the values (8 bytes each); type
 * 4 = a string (a char pointer) ---- */
/* a property table's entries for a log (strings, floats, ints t3, bools t1 = a bit of the values): ` HASH:VALUE`; the
 * first non-empty string into key (if given) */
static void co_props(uintptr_t tbl, char *s, size_t cap, char *key, size_t kcap)
{
	uint8_t hd[2];
	uintptr_t ent = tbl ? peek_ptr(tbl + 8) : 0, val = tbl ? peek_ptr(tbl + 0x10) : 0;
	size_t o = 0;
	s[0] = 0;
	if (key)
		key[0] = 0;
	if (!ent || !val || !peek(tbl, hd, 2))
		return;
	o += (size_t)snprintf(s, cap, " %u properties:", hd[0]);
	for (int k = 0; k < hd[0] && k < 48 && o < cap - 100; k++) {
		uint8_t e[16];
		if (!peek(ent + 16 * (uintptr_t)k, e, sizeof e))
			break;
		uint32_t h;
		uint16_t idx;
		memcpy(&h, e, 4), memcpy(&idx, e + 14, 2);
		int ty = e[10] & 0x3f;
		uint64_t v = 0;
		peek(val + 8 * (uintptr_t)(ty == 1 ? idx >> 5 : idx), &v, 8);
		char str[64] = "";
		if (ty == 4 && v)
			go_str((const char *)v, str, sizeof str);
		if (key && ty == 4 && str[0] && str[0] != '?' && !key[0])
			snprintf(key, kcap, "%s", str);
		if (ty == 4)
			o += (size_t)snprintf(s + o, cap - o, " %08x:'%s'", h, str);
		else if (ty == 2) {
			float f;
			memcpy(&f, &v, 4);
			o += (size_t)snprintf(s + o, cap - o, " %08x:%g", h, f);
		} else if (ty == 1)
			o += (size_t)snprintf(s + o, cap - o, " %08x:b%d", h, (int)((uint32_t)v >> (idx & 31) & 1));
		else
			o += (size_t)snprintf(s + o, cap - o, " %08x:t%d=%#lx", h, ty, (unsigned long)v);
	}
}

/* the record's index in the cache (its properties logged and its key read at first sight); -1 = none or full */
static int co_hint_slot(uintptr_t rec)
{
	if (!rec)
		return -1;
	for (int i = 0; i < g_co.nrec; i++)
		if (g_co.rec[i] == rec)
			return i;
	if (g_co.nrec == CO_RECS)
		return -1;
	int r = g_co.nrec++;
	g_co.rec[r] = rec, g_co.key[r][0] = 0;
	char s[1000];
	co_props(rec, s, sizeof s, g_co.key[r], sizeof g_co.key[r]);
	logf_("HINTS record %#lx:%s", (unsigned long)rec, s);
	return r;
}

/* gameopts' delegate (a combat hint's event, SOMVR_HINTS=log|off): its text key ("" = unknown) */
static const char *co_hint_key(uintptr_t rec)
{
	int r = co_hint_slot(rec);
	return r >= 0 ? g_co.key[r] : "";
}

/* SOMVR_HINTS_KEEP: 1 = this hint passes although SOMVR_HINTS=off drops hints */
static int co_hint_keep(uintptr_t rec)
{
	const char *k = co_hint_key(rec);
	for (int i = 0; k[0] && i < g_co.nkeep; i++)
		if (!strncmp(k, g_co.keep[i], strlen(g_co.keep[i]))) {
			g_co.n_kept++;
			return 1;
		}
	return 0;
}

static const char *co_word(const char *w)
{
	static char out[200];
	if (!strncmp(w, "inspect ", 8)) {
		const char *a = w + 8;
		int f = !strncmp(a, "flat", 4) ? 1 : !strncmp(a, "never", 5) ? 2 : !strncmp(a, "auto", 4) ? 0 : -1;
		if (f < 0)
			return "collect inspect: flat|auto|never";
		atomic_store(&g_co.ins_force, f);
		snprintf(out, sizeof out, "collect inspect %s (SOMVR_INSPECT=%s)", f == 1 ? "flat" : f == 2 ? "never" : "auto",
			 g_co.ins_mode == 2 ? "on" : g_co.ins_mode == 1 ? "log" : "off: nothing");
		return out;
	}
	if (!strncmp(w, "camstick ", 9)) {
		float x = 0, y = 0;
		unsigned ms = 0;
		if (!g_co.gl_mode || sscanf(w + 9, "%f %f %u", &x, &y, &ms) != 3 || fabsf(x) > 1 || fabsf(y) > 1 || ms > 10000)
			return "collect camstick: X Y MS (-1..1, ms <= 10000; with SOMVR_GLYPH=log)";
		uint32_t bx, by;
		memcpy(&bx, &x, 4), memcpy(&by, &y, 4);
		atomic_store(&g_co.cs_x, bx), atomic_store(&g_co.cs_y, by), atomic_store(&g_co.cs_ms, ms);
		atomic_fetch_add_explicit(&g_co.cs_seq, 1, memory_order_release);
		snprintf(out, sizeof out, "collect camstick %.2f %.2f %u", x, y, ms);
		return out;
	}
	if (!strncmp(w, "camhead ", 8)) {
		int m = atoi(w + 8);
		if (!g_co.gl_mode || m < 0 || m > 2)
			return "collect camhead: 0|1|2 (with SOMVR_GLYPH=log)";
		atomic_store(&g_co.camhead, m);
		g_co.camhead_logged = 0;
		snprintf(out, sizeof out, "collect camhead %d", m);
		return out;
	}
	if (!strncmp(w, "status", 6)) {
		const float *q = g_co.vq; /* the game's view forward R(q)(0,0,1) (somvr_front_cam_dot's): its yaw and pitch */
		float fx = 2.f * (q[0] * q[2] + q[1] * q[3]), fy = 2.f * (q[1] * q[2] - q[0] * q[3]),
		      fz = 1.f - 2.f * (q[0] * q[0] + q[1] * q[1]);
		logf_("COLLECT view: the game's camera at (%.0f %.0f %.0f) yaw %.1f pitch %.1f deg (forward %+.2f %+.2f %+.2f)",
		      g_co.view[0], g_co.view[1], g_co.view[2], atan2f(fx, fz) * 57.29578f,
		      asinf(fmaxf(-1.f, fminf(1.f, fy))) * 57.29578f, fx, fy, fz);
		logf_("COLLECT status: inspect mode %d force %d flat %d (%llu times, %llu frames, %.1f s; %llu paused frames, %llu "
		      "far); the game's camera at (%.0f %.0f %.0f), Talion (%.0f %.0f %.0f); glyph mode %d: ticks %llu near %llu, "
		      "%d triggers followed, vision look-at calls %llu (on %llu); hint records %d, kept %llu; camhead %d (%llu)",
		      g_co.ins_mode,
		      atomic_load(&g_co.ins_force), g_co.flat, (unsigned long long)g_co.n_flats,
		      (unsigned long long)g_co.n_flat_frames, g_co.flat_s, (unsigned long long)g_co.n_paused_frames,
		      (unsigned long long)g_co.n_far_frames, g_co.view[0], g_co.view[1], g_co.view[2], g_co.root[0], g_co.root[1],
		      g_co.root[2], g_co.gl_mode, (unsigned long long)g_co.n_ticks, (unsigned long long)g_co.n_near, g_co.ntr,
		      (unsigned long long)g_co.n_vlook, (unsigned long long)g_co.n_vlook_on, g_co.nrec,
		      (unsigned long long)g_co.n_kept, atomic_load(&g_co.camhead), (unsigned long long)g_co.n_camhead);
		if (g_co.gl_mode)
			logf_("COLLECT glyph items: %llu item geometries, %llu target answers (%llu validated), %d item(s) followed, "
			      "%llu glyph(s) collected by a look", (unsigned long long)g_co.n_geom_items,
			      (unsigned long long)g_co.n_scores, (unsigned long long)g_co.n_valid, g_co.nit,
			      (unsigned long long)g_co.n_collects);
		if (g_co.on_txt)
			logf_("COLLECT text buttons: %llu token(s) looked up, %llu shown with the mapped icon",
			      (unsigned long long)atomic_load(&g_co.n_tx), (unsigned long long)atomic_load(&g_co.n_tx_mapped));
		return "collect status";
	}
	return NULL;
}

static void co_start(int *bad)
{
	static const char *const ins_w[] = { "off", "log", "on" }, *const gl_w[] = { "off", "log", "head" };
	g_co.ins_mode = env_word("SOMVR_INSPECT", ins_w, 3, 0);
	g_co.ins_far_cm = 100 * env_float("SOMVR_INSPECT_M", 30, 5, 10000, bad);
	g_co.gl_mode = env_word("SOMVR_GLYPH", gl_w, 3, 0);
	g_co.gl_far_cm = 100 * env_float("SOMVR_GLYPH_M", 40, 2, 500, bad);
	const char *hk = getenv("SOMVR_HINTS_KEEP");
	for (const char *q = hk; q && *q && g_co.nkeep < CO_KEEP;) {
		while (*q == ',' || *q == ' ')
			q++;
		size_t n = strcspn(q, ", ");
		if (n && n < sizeof g_co.keep[0])
			memcpy(g_co.keep[g_co.nkeep], q, n), g_co.keep[g_co.nkeep][n] = 0, g_co.nkeep++;
		else if (n)
			*bad = 1;
		q += n;
	}
	if (hk && *hk)
		logf_("CONFIG hints_keep=%s (SOMVR_HINTS_KEEP): %d prefix(es); combat hints whose text key starts so pass even "
		      "with SOMVR_HINTS=off", hk, g_co.nkeep);
	const char *tb = getenv("SOMVR_TEXT_BUTTONS");
	for (const char *q = tb; q && *q && g_co.tx_n < CO_TXMAP;) {
		unsigned a, b;
		int n = 0;
		while (*q == ',' || *q == ' ')
			q++;
		if (!*q)
			break;
		if (sscanf(q, "%u:%u%n", &a, &b, &n) != 2 || a >= SOMVR_NCMD || b >= SOMVR_NCMD) {
			*bad = 1;
			break;
		}
		g_co.tx_from[g_co.tx_n] = a, g_co.tx_to[g_co.tx_n] = b, g_co.tx_n++;
		q += n;
	}
	if (g_co.tx_n) {
		char why[512];
		int ok = check_site(&g_co_sites[COS_TXT_ICON], why, sizeof why) == 0;
		g_co.on_txt = ok && bo_patch_call(&g_co_sites[COS_TXT_ICON], (void *)somvr_w_cotxt);
		logf_("CONFIG text_buttons=%s (SOMVR_TEXT_BUTTONS): %d map(s), the first %u '%s' -> the icon of %u '%s'; the call "
		      "CO_TXT_ICON %s%s%s", tb, g_co.tx_n, g_co.tx_from[0], in_cmd_name((int)g_co.tx_from[0]), g_co.tx_to[0],
		      in_cmd_name((int)g_co.tx_to[0]), g_co.on_txt ? "in" : "OFF", ok ? "" : ": MISMATCH ", ok ? "" : why);
	}
	if (g_co.ins_mode)
		logf_("CONFIG inspect=%s (SOMVR_INSPECT): a paused menu whose camera is over %.0f m from Talion renders flat%s",
		      ins_w[g_co.ins_mode], g_co.ins_far_cm * 0.01f, g_co.ins_mode == 2 ? " (the game's own view on the menu board)"
										    : " (log only)");
	if (!g_co.gl_mode)
		return;
	char why[512], ln[COS_N][48];
	for (int i = 0; i < COS_N; i++) {
		g_co.ok[i] = check_site(&g_co_sites[i], why, sizeof why) == 0;
		snprintf(ln[i], sizeof ln[i], "%s %s", g_co_sites[i].name, g_co.ok[i] ? "ok" : "MISMATCH");
		if (!g_co.ok[i])
			logf_("GLYPH site %s %#lx -> %#lx MISMATCH: %s", g_co_sites[i].name, (unsigned long)g_co_sites[i].addr,
			      (unsigned long)g_co_sites[i].target, why);
	}
	if (g_co.ok[COS_LAT_TICK])
		g_co.on_tick = go_slot(&g_co_sites[COS_LAT_TICK], (void *)somvr_w_colat, &g_co.tick_orig);
	if (g_co.ok[COS_VLOOK])
		g_co.on_vlook = bo_patch_call(&g_co_sites[COS_VLOOK], (void *)somvr_w_covlook);
	g_co.on_geom = g_co.ok[COS_TGEOM_A] && g_co.ok[COS_TGEOM_B];
	if (g_co.on_geom) /* both or none: one camera rule for both targeting passes */
		g_co.on_geom = bo_patch_call(&g_co_sites[COS_TGEOM_A], (void *)somvr_w_cogeom) +
			       bo_patch_call(&g_co_sites[COS_TGEOM_B], (void *)somvr_w_cogeom);
	for (int i = COS_TSCORE_1; i <= COS_TSCORE_6; i++)
		g_co.on_score += g_co.ok[i] && bo_patch_call(&g_co_sites[i], (void *)somvr_w_coscore);
	logf_("CONFIG glyph=%s: the targeting's item candidates: geometry calls %d of 2 ours (%s), target rules logged at %d "
	      "of 6 calls; %s", gl_w[g_co.gl_mode], g_co.on_geom, g_co.gl_mode == 2 ? "followed" : "logged only",
	      g_co.on_score, g_co.gl_mode == 2 && g_co.ok[COS_COLLECT] ? "a glyph whose trigger fired with our head and the "
	      "vision is collected (CO_COLLECT)" : "nothing collected");
	logf_("CONFIG glyph=%s (SOMVR_GLYPH): LookAtTriggers within %.0f m and the vision look-at logged; hooks: tick %s, "
	      "vision look-at %s; sites: %s, %s, %s, %s, %s", gl_w[g_co.gl_mode], g_co.gl_far_cm * 0.01f,
	      g_co.on_tick ? "in" : "OFF", g_co.on_vlook ? "in" : "OFF", ln[0], ln[1], ln[2], ln[3], ln[4]);
}

static void co_end_log(void)
{
	if (g_co.ins_mode) {
		if (g_co.flat)
			g_co.flat_s += mono() - g_co.flat_t0;
		logf_("INSPECT END flat %llu time(s), %llu frames, %.1f s (paused frames %llu, far %llu)",
		      (unsigned long long)g_co.n_flats, (unsigned long long)g_co.n_flat_frames, g_co.flat_s,
		      (unsigned long long)g_co.n_paused_frames, (unsigned long long)g_co.n_far_frames);
	}
	if (g_co.gl_mode)
		logf_("GLYPH END trigger ticks %llu (near %llu), %d followed; vision look-at calls %llu (on %llu); item "
		      "geometries %llu; target answers %llu (validated %llu); glyphs collected %llu", (unsigned long long)g_co.n_ticks,
		      (unsigned long long)g_co.n_near, g_co.ntr, (unsigned long long)g_co.n_vlook,
		      (unsigned long long)g_co.n_vlook_on, (unsigned long long)g_co.n_geom_items,
		      (unsigned long long)g_co.n_scores, (unsigned long long)g_co.n_valid, (unsigned long long)g_co.n_collects);
}
