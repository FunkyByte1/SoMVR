/* somvr_finish_game.c - SOMVR_FINISH=1, the game side of the VR-native finishers (docs/re-xr-pipeline.md 24). Not a
 * translation unit of its own: somvr.c includes it at its end (it uses somvr.c's helpers: peek, the sites, the combat
 * module's orcs and DealHit, the database readers, the haptics), so the hook-in lines in somvr.c stay a handful. The
 * pure part (zones, judges, glow, latch, classes) is somvr_finish.c.
 *
 * Calls (all on the game thread, inside combat_frame):
 *   fi_frame(pl, t)       every combat frame: the claim's end, the slow-mo's end, the dagger's stab, the latched hand
 *   fi_sword(...)         after the orcs and the blade are known: the ready glow, the execution / ground execution /
 *                         sword stab judges; returns the orc the finisher holds (combat_frame deals it no hit)
 *   fi_hands(p, q, v, w)  from in_frame: the left grip -> the dagger's blade and speed, the left hand's controls
 * and fi_start / fi_end_log from somvr_start / somvr_fini.
 *
 * The game's own path for a finisher (re-xr-pipeline.md 24.2): the player's root combat nodes (PlayerCombat+0xab0, a
 * Combat character record: its Nodes list), each as FUN_00a8e120 tries them - FUN_02296f80 (the KBM variant),
 * FUN_02264a70 (the attack window allows the node) - but without the button check (FUN_02272ea0: the gesture is the
 * button); SharedCombat_SelectAttackFromPool(ctx, node, 1, &target, 0) with the orc as an explicit
 * SharedCombatTarget::CBaseTarget (FUN_0227d210 + FUN_02280b30, as FUN_00a8e120's chained path builds one), so the
 * game's predicates (the streak's HitCounterRange, AIIsAware, the direction, the target's state ...) decide against
 * that orc; the candidates' node and record names give the class; the wanted one starts with FUN_00a8c700(pc, node,
 * attack, hOrc) - the client side and Combat_StartAttack, the sync when the record has one (the victim's half, both
 * bodies aligned: the camera side is SOMVR_BODYMOMENTS'), the kill credit the game's. No camera presentation is asked
 * for (FUN_00a8e120's FUN_00a745c0/FUN_00a75190 step is skipped: the view is never the game's). */

/* FINISH_SITES_BEGIN (read only, never patched: what SOMVR_FINISH calls, and FI_HITCOUNT's bytes as the proof of the
 * streak's offsets). FI_HITCOUNT FUN_026d6110 = (short)(*(Combat+0x1f0))+0xc4 (the hit counter's value: the streak);
 * FI_CTX FUN_022617f0 the attack-selection context; FI_CTX_PC FUN_00a84540 its PlayerCombat part; FI_WINDOW
 * FUN_02264a70 the attack window's node check; FI_SELECT SharedCombat_SelectAttackFromPool; FI_TGT_INIT FUN_0227d210
 * (CBaseTarget's constructor: its vtable 0x38c3290 in the bytes); FI_TGT_SET FUN_02280b30 (the target object); FI_START
 * FUN_00a8c700 (the player's attack start); FI_PLAYERROOT FUN_00a36880 (*(*(Player+0x2d8)+0x1b8)); FI_WEAPON
 * FUN_00a379a0 (the equipped weapon); FI_DIRANGLE FUN_02297020 (the signed angle of a direction off a transform's
 * forward); FI_SLOWMO FUN_02824cd0 (the SlowMO.Set command); FI_FX_* the ClientFX create struct, create, free, release
 * (re-combat-items.md 2.4, the probe's weaponfx); FI_OBJSYS_MH the object system's vtbl+0x118 (an object's model
 * handle); FI_ITEM FUN_02311e20 (the active item: inv+0x280 the slot, inv+0x30+slot*0x38); FI_KBMNODE FUN_02296f80.
 * Order = FIS_* */
static const struct site_spec g_fi_sites_game[] = {
SITE("FI_HITCOUNT", K_ENTRY, 0x26d6110, 0x26d6110, "48 8b 97 f0 01 00 00 31 c0 48 85 d2 74 07 0f bf 82 c4 00 00 00 c3", 0x26d6110, "48 8b 97 f0 01 00 00 31 c0 48 85 d2 74 07 0f"),
SITE("FI_CTX", K_ENTRY, 0x22617f0, 0x22617f0, "48 b8 00 00 80 bf 08 00 00 01 41 55 41 54 55 48", 0x22617f0, "48 b8 00 00 80 bf 08 00 00 01 41 55 41 54 55"),
SITE("FI_CTX_PC", K_ENTRY, 0xa84540, 0xa84540, "41 54 55 48 89 f5 53 48 89 fb 4c 8b a7 c8 08 00", 0xa84540, "41 54 55 48 89 f5 53 48 89 fb 4c 8b a7 c8 08"),
SITE("FI_WINDOW", K_ENTRY, 0x2264a70, 0x2264a70, "41 54 55 48 89 fd 53 48 89 f3 48 83 ec 10 48 8b", 0x2264a70, "41 54 55 48 89 fd 53 48 89 f3 48 83 ec 10 48"),
SITE("FI_SELECT", K_ENTRY, 0x2261c10, 0x2261c10, "41 57 49 89 ff 41 56 41 55 4d 89 c5 41 54 55 53", 0x2261c10, "41 57 49 89 ff 41 56 41 55 4d 89 c5 41 54 55"),
SITE("FI_TGT_INIT", K_ENTRY, 0x227d210, 0x227d210, "48 8d 47 18 66 0f ef c0 48 c7 07 90 32 8c 03 48 89 47 20 48 89 47 18 48", 0x227d210, "48 8d 47 18 66 0f ef c0 48 c7 07 90 32 8c 03"),
SITE("FI_TGT_SET", K_ENTRY, 0x2280b30, 0x2280b30, "48 83 ec 28 48 85 d2 74 37 f3 0f 10 42 20 0f 10", 0x2280b30, "48 83 ec 28 48 85 d2 74 37 f3 0f 10 42 20 0f"),
SITE("FI_START", K_ENTRY, 0xa8c700, 0xa8c700, "41 57 41 56 41 55 41 54 55 53 48 81 ec 18 01 00", 0xa8c700, "41 57 41 56 41 55 41 54 55 53 48 81 ec 18 01"),
SITE("FI_PLAYERROOT", K_ENTRY, 0xa36880, 0xa36880, "48 8b 87 d8 02 00 00 48 8b 80 b8 01 00 00 c3 90", 0xa36880, "48 8b 87 d8 02 00 00 48 8b 80 b8 01 00 00 c3"),
SITE("FI_WEAPON", K_ENTRY, 0xa379a0, 0xa379a0, "48 8b 87 20 04 00 00 48 85 c0 74 14 48 8b b8 e8", 0xa379a0, "48 8b 87 20 04 00 00 48 85 c0 74 14 48 8b b8"),
SITE("FI_DIRANGLE", K_ENTRY, 0x2297020, 0x2297020, "f3 0f 10 4f 0c f3 0f 10 6f 10 31 c0 f3 44 0f 10", 0x2297020, "f3 0f 10 4f 0c f3 0f 10 6f 10 31 c0 f3 44 0f"),
SITE("FI_SLOWMO", K_ENTRY, 0x2824cd0, 0x2824cd0, "55 48 89 f5 53 89 fb 48 83 ec 08 83 ff 01 0f 84", 0x2824cd0, "55 48 89 f5 53 89 fb 48 83 ec 08 83 ff 01 0f"),
SITE("FI_FX_CSINIT", K_ENTRY, 0xa09e70, 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89 cd", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("FI_FX_CREATE", K_ENTRY, 0xd8c7f0, 0xd8c7f0, "80 3d da aa 92 03 00 75 07 31 c0 c3 0f 1f 40 00", 0xd8c7f0, "80 3d da aa 92 03 00 75 07 31 c0 c3 0f 1f 40"),
SITE("FI_FX_CSFREE", K_ENTRY, 0x9ec750, 0x9ec750, "55 53 48 89 fb 48 83 ec 08 80 bf ac 01 00 00 00", 0x9ec750, "55 53 48 89 fb 48 83 ec 08 80 bf ac 01 00 00"),
SITE("FI_FX_RELEASE", K_ENTRY, 0x2aebfd0, 0x2aebfd0, "48 8b 07 48 85 c0 74 50 53 48 89 fb 48 89 c7 e8", 0x2aebfd0, "48 8b 07 48 85 c0 74 50 53 48 89 fb 48 89 c7"),
SITE("FI_OBJSYS_MH", K_VSLOT, 0x3453170, 0x3453170, "e0 6a 8c 00 00 00 00 00", 0x8c6ae0, "48 85 f6 74 0b 48 8b 86 e0 00 00 00 c3 0f 1f"),
SITE("FI_ITEM", K_ENTRY, 0x2311e20, 0x2311e20, "8b 87 80 02 00 00 83 f8 07 76 0b 48 c7 05 9a 2c 84 02 a0 19 35 03 48 8d 14 c5 00 00 00 00 48 29 c2 48 8b 44 d7 30 c3", 0x2311e20, "8b 87 80 02 00 00 83 f8 07 76 0b 48 c7 05 9a"),
SITE("FI_KBMNODE", K_ENTRY, 0x2296f80, 0x2296f80, "53 48 89 fb 48 83 ec 10 40 84 f6 74 13 48 89 d8", 0x2296f80, "53 48 89 fb 48 83 ec 10 40 84 f6 74 13 48 89"),
};
/* FINISH_SITES_END */
enum { FIS_HITCOUNT, FIS_CTX, FIS_CTX_PC, FIS_WINDOW, FIS_SELECT, FIS_TGT_INIT, FIS_TGT_SET, FIS_START, FIS_PLAYERROOT, FIS_WEAPON,
       FIS_DIRANGLE, FIS_SLOWMO, FIS_FX_CSINIT, FIS_FX_CREATE, FIS_FX_CSFREE, FIS_FX_RELEASE, FIS_OBJSYS_MH, FIS_ITEM, FIS_KBMNODE,
       FIS_N };
_Static_assert(sizeof g_fi_sites_game / sizeof g_fi_sites_game[0] == FIS_N, "finish sites");
static const struct site_spec *g_fi_sites = g_fi_sites_game; /* selftest: stand-ins */

typedef void (*fi_ctx_fn)(void *ctx, uintptr_t combat, uintptr_t weapon);
typedef void (*fi_ctxpc_fn)(uintptr_t pcomb, void *ctx);
typedef uint64_t (*fi_window_fn)(uintptr_t window, uintptr_t node, int busy);
typedef uintptr_t (*fi_select_fn)(void *ctx, uintptr_t node, int flag, void *target, void *score);
typedef void (*fi_tgtinit_fn)(void *tgt);
typedef void (*fi_tgtset_fn)(void *tgt, uintptr_t src, uintptr_t obj, int flag);
typedef void (*fi_start_fn)(uintptr_t pcomb, uintptr_t node, uintptr_t attack, uintptr_t target);
typedef uintptr_t (*fi_ptr1_fn)(uintptr_t a);
typedef uint64_t (*fi_dirangle_fn)(const void *xf, const float *dir, float *angle);
typedef void (*fi_slowmo_fn)(int argc, const char *const *argv);
typedef void (*fi_cs_fn)(void *cs, const char *name, uintptr_t mh, unsigned flags, uintptr_t parent);
typedef long (*fi_create_fn)(uintptr_t mgr, uintptr_t *handle, void *cs, int start, int b, int c);
typedef void (*fi_csfree_fn)(void *cs);
typedef void (*fi_release_fn)(uintptr_t *handle);
typedef uintptr_t (*fi_mh_fn)(uintptr_t objsys, uintptr_t obj);
typedef uintptr_t (*fi_kbm_fn)(uintptr_t node, int pad);
#define FI_FN(i, T) ((T)g_fi_sites[i].target)

/* the game's objects and schemas (selftest: stand-ins) */
static struct {
	uintptr_t sch_ccr, sch_node, sch_attack, sch_anim;     /* schema holders: character record, node, attack, AnimationData */
	uintptr_t shell_pp, objsys_pp, vt_objsys, fx_on, sever, timer_pp, vt_target;
} g_fig = { 0x4ba0ca0, 0x4ba20c0, 0x4ba23c0, 0x4ba8640, 0x46a9560, 0x466afa0, 0x3453058, 0x46b72d1, 0x4c449d8, 0x463e290,
	    0x38c3290 };
#define FI_PC_PLAYER 0x08    /* PlayerCombat: the Player (CPlayerObj) */
#define FI_PC_PAD 0x15d      /* FUN_02296f80's flag in FUN_00a8e120: the node itself, else its KBMNode */
#define FI_PC_WINDOW 0x838   /* the attack-window state FUN_02264a70 reads */
#define FI_PC_ROOT 0xab0     /* the current root record (a Combat character record: field 1 Nodes) */
#define FI_PC_PENDING 0xac8  /* FUN_02264a70's third argument in FUN_00a8e120: this pointer set */
#define FI_C_HITCTR 0x1f0    /* Combat: the hit counter object (its +0xc4 short = the value, FI_HITCOUNT) */
#define FI_HC_VALUE 0xc4
#define FI_C_STATE 0x268     /* Combat: the combat state record (re-combat.md 2.2) */
#define FI_C_TARGET 0x4c0    /* Combat: its target's HOBJECT */
#define FI_P_INVHOLD 0x420   /* Player: +0x420 -> +0xe8 the Inventory (FUN_00a379a0, FUN_00a8e120's end) */
#define FI_INV_OF 0xe8
#define FI_IT_ENTRIES 0x28   /* InventoryItem: vector of 0x48-byte model entries, +0x20 the model object */
#define FI_IE_OBJ 0x20
#define FI_SH_FXMGR 0x80c78  /* CGameClientShell: the ClientFX manager (create at +0x30) */
#define FI_TM_NUM 0x20       /* the game timer (*(0x463e290)+8): num, den (SlowMO.Set writes them) */
#define FI_CCR_NODES 1       /* schema 0x4ba0ca0 field 1: Nodes */
#define FI_ATK_ANIM 0        /* schema 0x4ba23c0 field 0: AnimationData */
#define FI_ANIM_SYNC 8       /* schema 0x4ba8640 field 8: Sync */
#define FI_TGT_SIZE 0x98     /* CBaseTarget (FUN_0227d210 writes up to +0x90) */
#define FI_TGT_OBJ 0x68      /* its target object (FUN_022806c0: param_1[0xd]) */
#define FI_CTX_ANGLE 0x24    /* the selection context: the input's angle (deg), as FUN_0229a190 sets it for a target */
#define FI_CTX_NOTME 0x48    /* ... "the target's target is not me" */
#define FI_NODES 48          /* root nodes looked at */
#define FI_CANDS 16          /* candidates kept per selection */
#define FI_ORCS 16
#define FI_RANGE_CM 300.0f   /* orcs this close to Talion's root are judged */
#define FI_RD2DEG 57.2957795f

enum { FI_OFF = 0, FI_ON = 1, FI_LOG = 2 };
enum { FI_PATH_STOCK = 0, FI_PATH_HIT = 1 };
enum { FI_STAB_OFF = 0, FI_STAB_LEFT = 1, FI_STAB_RIGHT = 2, FI_STAB_BOTH = 3 };
struct fi_cand { uintptr_t node, atk, sync; int cls; char nn[80], an[96]; };
struct fi_orcst { uintptr_t c, state; int kind; double since; int head_st; };
struct fi_cnt {
	uint64_t frames, ready_frames, glow_on, glow_off, fx_fail, judged, near_miss, gestures[SF_CLS_N], starts[SF_CLS_N],
		fallbacks, notoffered, selects, cands, latches, latch_ev[SF_LEV_LOST + 1], slowmo, slowmo_skip, claims_dead,
		claims_sync, claims_time, skipped_hits, held_hits, nohead;
};
static struct {
	int mode, path, fallback, sever, stab, latch_on, glow_on, head_mode, ok_stock, ok_fx, ok_slowmo, ok_streak, ok_node;
	sf_cfg cfg;
	sf_names names;
	char glow_fx[160], kill_name[96];
	uintptr_t kill_rec;
	int kill_looked;
	float slowmo, slowmo_s;
	/* state */
	double swing_t0, streak_since, smo_until, next_log, last_line;
	int streak, last_streak;
	uint32_t smo_num, smo_den;
	sf_glow glow;
	uintptr_t fx_handle, fx_item;
	sf_moment mo;
	int claim_sync_seen;
	sf_latch latch;
	struct fi_orcst os[FI_ORCS];
	/* the dagger (the left hand): its blade, the previous one, its speed; the left hand's controls */
	sc_blade_cfg dcfg;
	sc_blade_hist dhist;
	somvr_blade dag, dprev;
	sc_speed dsp;
	sc_cfg dsc;
	int lh_valid, lh_upper;
	float lh_pos[3], lh_squeeze, lh_trigger;
	uint64_t lh_frame;
	somvr_blade sprev; /* the sword's blade the previous frame (fi_sword's own copy) */
	double sword_t;    /* fi_sword's last run (it runs only while the sword has a blade) */
	int lines, lsec_n;
	double lsec;
	uint64_t suppressed;
	struct fi_cnt n, w;
} g_fi;

static int fi_log_ok(double t)
{
	if (g_fi.lines < 80)
		return ++g_fi.lines;
	if (t >= g_fi.lsec + 1.0)
		g_fi.lsec = t, g_fi.lsec_n = 0;
	if (g_fi.lsec_n < 8)
		return ++g_fi.lsec_n;
	g_fi.suppressed++;
	return 0;
}
#define FI_COUNT(f) (g_fi.n.f++, g_fi.w.f++)

/* ---- reading the game ------------------------------------------------------------------------------------------- */
/* the streak: (short)(*(Combat+0x1f0))+0xc4, what FUN_026d6110 returns (its bytes are FI_HITCOUNT's proof) */
static int fi_streak(uintptr_t pc)
{
	int16_t v = 0;
	uintptr_t hc = pc ? peek_ptr(pc + FI_C_HITCTR) : 0;
	return hc && peek(hc + FI_HC_VALUE, &v, sizeof v) ? v : 0;
}

/* an orc's state entry (kind and since), refreshed from Combat+0x268 by the record's name */
static struct fi_orcst *fi_orc_state(uintptr_t c, double t)
{
	struct fi_orcst *e = NULL, *old = &g_fi.os[0];
	for (int i = 0; i < FI_ORCS && !e; i++)
		if (g_fi.os[i].c == c)
			e = &g_fi.os[i];
		else if (!g_fi.os[i].c || g_fi.os[i].since < old->since)
			old = &g_fi.os[i];
	if (!e) {
		e = old;
		memset(e, 0, sizeof *e);
		e->c = c, e->since = t;
	}
	uintptr_t st = peek_ptr(c + FI_C_STATE);
	if (st != e->state) {
		char nm[96];
		e->state = st;
		e->kind = st ? sf_state_kind(rx_rec_name(st, nm, sizeof nm)) : SF_ST_NONE;
		if (st && e->kind == SF_ST_NONE)
			e->kind = SF_ST_OTHER; /* a state without a readable name */
		e->since = t;
	}
	return e;
}

/* the orc's Head node (GetNodeTransform with Talion's Head hash: RT whether orcs name it the same); 0 = not read */
static int fi_head(struct fi_orcst *e, uintptr_t hobj, const float root[3], float out[3])
{
	uintptr_t m = peek_ptr(g_som.model_pp), mi = peek_ptr(hobj + OBJ_MODEL), mv = vptr_of((void *)mi);
	if (!g_fi.ok_node || !m || e->head_st < 0)
		return 0;
	if (!e->head_st) { /* once per orc: its model interface must reach code (GetNodeTransform's FUN_008f3c20) */
		uintptr_t f = mv ? peek_ptr(mv + 0x20) : 0;
		int pr = f ? page_prot(f) : -1;
		e->head_st = pr >= 0 && (pr & PROT_EXEC) ? 1 : -1;
		if (e->head_st < 0)
			return 0;
	}
	float xf[16] = { 0 };
	if ((((nodexf_fn)g_node_sites[NS_NODEXF].target)((void *)m, hobj, g_som.head, xf, 1) & 0xff) || !vec_ok(xf, 1e7f) ||
	    dist3(xf, root) > 250.0f) {
		e->head_st = -1;
		FI_COUNT(nohead);
		return 0;
	}
	memcpy(out, xf, 12);
	return 1;
}

static void fi_zones(const struct cb_orc *o, struct fi_orcst *e, sf_zones *z, float *yaw)
{
	float rot[4] = { 0, 0, 0, 1 }, head[3];
	peek(o->hobj + OBJ_ROT, rot, sizeof rot);
	*yaw = sf_yaw_of_quat(rot);
	int down = e->kind == SF_ST_DOWN;
	int hn = !down && g_fi.head_mode && fi_head(e, o->hobj, o->pos, head);
	sf_zones_of(&g_fi.cfg, o->pos, *yaw, hn ? head : NULL, down, z);
}

/* ---- the glow on the sword: a looping ClientFX on the active weapon's model object (re-combat-items.md 2.4) ------- */
static void fi_fx_stop(const char *why, double t)
{
	if (!g_fi.fx_handle)
		return;
	uintptr_t h = g_fi.fx_handle;
	FI_FN(FIS_FX_RELEASE, fi_release_fn)(&g_fi.fx_handle);
	if (fi_log_ok(t))
		logf_("FINISH glow fx off (%s): handle %#lx -> %#lx", why, (unsigned long)h, (unsigned long)g_fi.fx_handle);
	g_fi.fx_handle = 0, g_fi.fx_item = 0;
}

static uintptr_t fi_active_item(uintptr_t pcomb)
{
	uintptr_t pl = peek_ptr(pcomb + FI_PC_PLAYER), hold = pl ? peek_ptr(pl + FI_P_INVHOLD) : 0;
	uintptr_t inv = hold ? peek_ptr(hold + FI_INV_OF) : 0;
	return inv ? FI_FN(FIS_ITEM, fi_ptr1_fn)(inv) : 0;
}

static void fi_fx_start(uintptr_t pcomb, double t)
{
	if (g_fi.fx_handle || !g_fi.ok_fx || !g_fi.glow_fx[0] || g_fi.mode != FI_ON)
		return;
	uintptr_t item = fi_active_item(pcomb), b = item ? peek_ptr(item + FI_IT_ENTRIES) : 0;
	uintptr_t e = item ? peek_ptr(item + FI_IT_ENTRIES + 8) : 0, obj = b && e > b ? peek_ptr(b + FI_IE_OBJ) : 0;
	uintptr_t os = peek_ptr(g_fig.objsys_pp), vt = vptr_of((void *)os), shell = peek_ptr(g_fig.shell_pp);
	uintptr_t mgr = shell ? peek_ptr(shell + FI_SH_FXMGR) : 0;
	const char *why = !obj ? "no active weapon model object" : !os || vt != g_fig.vt_objsys ||
			  vt + 0x118 != g_fi_sites[FIS_OBJSYS_MH].addr || peek_ptr(vt + 0x118) != g_fi_sites[FIS_OBJSYS_MH].target
			  ? "the object system is not the checked one" : !mgr ? "no ClientFX manager (shell+0x80c78)" : NULL;
	if (why) {
		FI_COUNT(fx_fail);
		if (fi_log_ok(t))
			logf_("FINISH glow fx not started: %s (item %#lx)", why, (unsigned long)item);
		return;
	}
	uintptr_t mh = FI_FN(FIS_OBJSYS_MH, fi_mh_fn)(os, obj);
	static unsigned char cs[0x400] __attribute__((aligned(16))); /* CLIENTFX_CREATESTRUCT (>= 0x1c8 bytes); game thread */
	memset(cs, 0, sizeof cs);
	FI_FN(FIS_FX_CSINIT, fi_cs_fn)(cs, g_fi.glow_fx, mh, 1u /* loop */, obj);
	long rc = FI_FN(FIS_FX_CREATE, fi_create_fn)(mgr + 0x30, &g_fi.fx_handle, cs, 1, 0, 0);
	FI_FN(FIS_FX_CSFREE, fi_csfree_fn)(cs);
	unsigned char on = 0;
	peek(g_fig.fx_on, &on, 1);
	if (!g_fi.fx_handle)
		FI_COUNT(fx_fail);
	else
		g_fi.fx_item = item;
	if (fi_log_ok(t))
		logf_("FINISH glow fx '%s' on item %#lx obj %#lx: rc=%ld handle=%#lx clientfx=%u", g_fi.glow_fx,
		      (unsigned long)item, (unsigned long)obj, rc, (unsigned long)g_fi.fx_handle, on);
}

/* the ready state of this frame -> the glow (the FX and one pulse on the sword hand) */
static void fi_glow(uintptr_t pcomb, int ready, int force_off, double t)
{
	if (g_fi.fx_handle && fi_active_item(pcomb) != g_fi.fx_item)
		fi_fx_stop("the active item changed", t); /* sheathed or swapped: the item's own FX code does not know ours */
	int e = sf_glow_step(&g_fi.glow, &g_fi.cfg, ready, force_off, t);
	if (e > 0) {
		FI_COUNT(glow_on);
		in_haptic_event(SOMVR_HAND_R, 0.5f, 0.06f, SOMVR_HAP_COMBAT);
		if (g_fi.glow_on)
			fi_fx_start(pcomb, t);
		if (fi_log_ok(t))
			logf_("FINISH ready: the glow on (streak %d of %d)", g_fi.streak, g_fi.cfg.exec_streak);
	} else if (e < 0) {
		FI_COUNT(glow_off);
		fi_fx_stop(force_off ? "a finisher or no sword" : "not ready", t);
	}
}

/* ---- the game's own selection ------------------------------------------------------------------------------------ */
static void fi_unlink(unsigned char *node) /* an intrusive list node {next, prev}, as the game's inline destructor */
{
	uintptr_t next, prev;
	memcpy(&next, node, 8), memcpy(&prev, node + 8, 8);
	if (next && prev)
		memcpy((void *)(next + 8), &prev, 8), memcpy((void *)prev, &next, 8);
}

/* The player's root nodes against orc o, as FUN_00a8e120 tries them minus the button: the candidates (node, attack,
 * class) the game's predicates pass. Returns the count (-1 = could not run: *why). */
static int fi_select(uintptr_t pcomb, uintptr_t pc, const struct cb_orc *o, struct fi_cand *cd, int cap, const char **why)
{
	uintptr_t player = peek_ptr(pcomb + FI_PC_PLAYER), root = peek_ptr(pcomb + FI_PC_ROOT);
	uint64_t nodes[FI_NODES];
	int type = 0, count = 0, nn = root ? rx_db_list(root, g_fig.sch_ccr, FI_CCR_NODES, nodes, FI_NODES, &type, &count) : -1;
	if (!player)
		return *why = "no Player at PlayerCombat+8", -1;
	if (nn <= 0 || type != 0xa)
		return *why = "no root nodes (PlayerCombat+0xab0's Nodes)", -1;
	static unsigned char ctx[0x100] __attribute__((aligned(16))), tgt[FI_TGT_SIZE + 0x68] __attribute__((aligned(16)));
	memset(ctx, 0, sizeof ctx), memset(tgt, 0, sizeof tgt);
	FI_FN(FIS_CTX, fi_ctx_fn)(ctx, pc, FI_FN(FIS_WEAPON, fi_ptr1_fn)(player));
	FI_FN(FIS_CTX_PC, fi_ctxpc_fn)(pcomb, ctx);
	/* as FUN_0229a190 does for an explicit target: the input's angle = the direction to it off Talion's facing, and
	 * whether the orc's target is someone else */
	uintptr_t me = peek_ptr(pc + C_HOBJ);
	float xf[7], dir[3], ang = 0;
	if (me && peek(me + OBJ_POS, xf, sizeof xf)) {
		dir[0] = o->pos[0] - xf[0], dir[1] = 0, dir[2] = o->pos[2] - xf[2];
		if (FI_FN(FIS_DIRANGLE, fi_dirangle_fn)(xf, dir, &ang) & 0xff) {
			float deg = ang * FI_RD2DEG;
			memcpy(ctx + FI_CTX_ANGLE, &deg, 4);
		}
	}
	ctx[FI_CTX_NOTME] = peek_ptr(o->c + FI_C_TARGET) != me;
	FI_FN(FIS_TGT_INIT, fi_tgtinit_fn)(tgt);
	uintptr_t vt, took = 0;
	memcpy(&vt, tgt, 8);
	if (vt != g_fig.vt_target)
		return *why = "the target's vtable is not CBaseTarget's (0x38c3290)", -1; /* nothing linked yet */
	FI_FN(FIS_TGT_SET, fi_tgtset_fn)(tgt, FI_FN(FIS_PLAYERROOT, fi_ptr1_fn)(player), o->hobj, 0);
	memcpy(&took, tgt + FI_TGT_OBJ, 8);
	int nc = 0;
	if (took != o->hobj) {
		*why = "the target did not take the orc (CBaseTarget+0x68)";
		nc = -1;
	}
	uint8_t pad = 0;
	peek(pcomb + FI_PC_PAD, &pad, 1);
	int busy = peek_ptr(pcomb + FI_PC_PENDING) != 0;
	FI_COUNT(selects);
	for (int k = 0; nc >= 0 && k < nn && nc < cap; k++) {
		uintptr_t node = FI_FN(FIS_KBMNODE, fi_kbm_fn)((uintptr_t)nodes[k], pad != 0);
		if (!node || !(FI_FN(FIS_WINDOW, fi_window_fn)(pcomb + FI_PC_WINDOW, node, busy) & 0xff))
			continue;
		uintptr_t atk = FI_FN(FIS_SELECT, fi_select_fn)(ctx, node, 1, tgt, NULL);
		if (!atk)
			continue;
		struct fi_cand *c = &cd[nc++];
		memset(c, 0, sizeof *c);
		c->node = node, c->atk = atk;
		rx_rec_name(node, c->nn, sizeof c->nn), rx_rec_name(atk, c->an, sizeof c->an);
		uintptr_t anim = rx_db_rec(atk, g_fig.sch_attack, FI_ATK_ANIM);
		c->sync = anim ? rx_db_rec(anim, g_fig.sch_anim, FI_ANIM_SYNC) : 0;
		c->cls = sf_class_of(&g_fi.names, c->nn, c->an);
		FI_COUNT(cands);
	}
	fi_unlink(tgt + 0x58), fi_unlink(tgt + 0x18); /* the target's two object references, as the game lets them go */
	return nc;
}

/* the fallback: a killing Combat_DealHit (forceKill) with the kill record, severing with SOMVR_FINISH_SEVER (the
 * debug byte Combat.Debug.AlwaysSever for this one call, re-combat.md 2.4) */
static int fi_kill_hit(uintptr_t pc, const struct cb_orc *o, const sf_hit *h, const somvr_blade *b, double t)
{
	const char *name = g_fi.kill_name;
	if (!g_fi.kill_rec && g_fi.kill_name[0] && g_cb.ok_hit && peek_ptr(g_cbg.atkcat_pp))
		g_fi.kill_rec = cb_rec_valid(CB_FN(CS_ATK_BYNAME, cb_byname_fn)(g_fi.kill_name));
	uintptr_t rec = g_fi.kill_rec ? g_fi.kill_rec : cb_record(SC_TIER_HEAVY, 0, &name);
	if (!g_cb.ok_hit || !rec || g_cb.mode != CB_FULL)
		return 0;
	float dir[3] = { 0, 0, 1 }, v = b ? sqrtf(b->vel_tip[0] * b->vel_tip[0] + b->vel_tip[1] * b->vel_tip[1] +
						 b->vel_tip[2] * b->vel_tip[2]) : 0;
	if (v > 1.0f)
		dir[0] = b->vel_tip[0] / v, dir[1] = b->vel_tip[1] / v, dir[2] = b->vel_tip[2] / v;
	float hd0[3];
	unsigned char once = 0, sev0 = 0, one = 1;
	peek(pc + C_HITDIR, hd0, sizeof hd0), peek(pc + C_HITONCE, &once, 1);
	int sev = g_fi.sever && peek(g_fig.sever, &sev0, 1);
	memcpy((void *)(pc + C_HITDIR), dir, sizeof dir);
	*(volatile unsigned char *)(pc + C_HITONCE) = 0;
	if (sev)
		memcpy((void *)g_fig.sever, &one, 1);
	int dead0 = cb_dead(o->c);
	int fk = kp_force_kill01(o->c, SOMVR_CPF_COMBAT, "the finisher", cb_now()); /* build 22: a captain's traits */
	struct kp_floor kf;
	kp_floor_on(o->c, fk, "the finisher", cb_now(), &kf);
	CB_FN(CS_DEALHIT, cb_dealhit_fn)((cb_ltvec){ h->point[0], h->point[1], h->point[2] }, pc, o->hobj, rec, fk);
	kp_floor_off(&kf);
	if (sev)
		memcpy((void *)g_fig.sever, &sev0, 1);
	memcpy((void *)(pc + C_HITDIR), hd0, sizeof hd0);
	*(volatile unsigned char *)(pc + C_HITONCE) = once;
	int dead1 = cb_dead(o->c);
	FI_COUNT(fallbacks);
	if (fi_log_ok(t))
		logf_("FINISH fallback kill: Combat_DealHit(pos=%.1f,%.1f,%.1f, pc=%#lx, hobj=%#lx, rec='%s' %#lx, forceKill=1)%s "
		      "dead=%d->%d", h->point[0], h->point[1], h->point[2], (unsigned long)pc, (unsigned long)o->hobj, name,
		      (unsigned long)rec, sev ? " with AlwaysSever" : "", dead0, dead1);
	return 1;
}

/* the slow motion (SOMVR_FINISH_SLOWMO): SlowMO.Set on the game timer, back to its own values after the time */
static void fi_slowmo(double t)
{
	uintptr_t tm = peek_ptr(g_fig.timer_pp) ? peek_ptr(peek_ptr(g_fig.timer_pp) + 8) : 0;
	uint32_t nd[2] = { 0, 0 };
	if (!(g_fi.slowmo > 0) || !g_fi.ok_slowmo || g_fi.mode != FI_ON || g_fi.smo_until > 0)
		return;
	if (!tm || !peek(tm + FI_TM_NUM, nd, sizeof nd) || nd[0] != nd[1] || !nd[0]) {
		FI_COUNT(slowmo_skip); /* the game's own slow motion (or none to read): not ours to change */
		return;
	}
	char a[32];
	snprintf(a, sizeof a, "%.3f", (double)g_fi.slowmo);
	const char *argv[1] = { a };
	FI_FN(FIS_SLOWMO, fi_slowmo_fn)(1, argv);
	g_fi.smo_num = nd[0], g_fi.smo_den = nd[1], g_fi.smo_until = t + g_fi.slowmo_s;
	FI_COUNT(slowmo);
}
static void fi_slowmo_end(double t, int now)
{
	if (!(g_fi.smo_until > 0) || (!now && t < g_fi.smo_until))
		return;
	char a[2][16];
	snprintf(a[0], sizeof a[0], "%u", g_fi.smo_num), snprintf(a[1], sizeof a[1], "%u", g_fi.smo_den);
	const char *argv[2] = { a[0], a[1] };
	FI_FN(FIS_SLOWMO, fi_slowmo_fn)(2, argv);
	g_fi.smo_until = 0;
}

/* A gesture asked for class want (alt: the second choice) on orc o: the game's own attack, else the fallback kill.
 * Returns 1 when something started (the orc is then claimed). */
static int fi_trigger(uintptr_t pcomb, uintptr_t pc, const struct cb_orc *o, int want, int alt, const sf_hit *h,
		      const somvr_blade *b, int hand, double t)
{
	FI_COUNT(gestures[want]);
	int lg = fi_log_ok(t);
	if (lg)
		logf_("FINISH gesture %s on orc=%#lx hobj=%#lx dist=%.1f zone=%d along=%.2f pos=%.1f,%.1f,%.1f streak=%d%s",
		      sf_class_name(want), (unsigned long)o->c, (unsigned long)o->hobj, o->dist, h->zone, h->along, h->point[0],
		      h->point[1], h->point[2], g_fi.streak, g_fi.mode == FI_LOG ? " (log only)" : "");
	if (g_fi.mode != FI_ON || !pcomb || !pc)
		return 0;
	struct fi_cand cd[FI_CANDS];
	int nc = 0, pick = -1;
	const char *why = "SOMVR_FINISH_PATH=hit";
	if (g_fi.path == FI_PATH_STOCK && g_fi.ok_stock) {
		nc = fi_select(pcomb, pc, o, cd, FI_CANDS, &why);
		int cls[FI_CANDS];
		for (int i = 0; i < nc; i++) {
			cls[i] = cd[i].cls;
			if (lg)
				logf_("FINISH candidate %d: node '%s' %#lx attack '%s' %#lx sync %#lx class %s", i, cd[i].nn,
				      (unsigned long)cd[i].node, cd[i].an, (unsigned long)cd[i].atk, (unsigned long)cd[i].sync,
				      sf_class_name(cd[i].cls));
		}
		pick = nc > 0 ? sf_pick(cls, nc, want, alt) : -1;
		if (nc >= 0 && pick < 0)
			why = nc ? "no candidate of that class (the names: SOMVR_FINISH_NAMES_*)" : "the game offered nothing";
	} else if (g_fi.path == FI_PATH_STOCK) {
		why = "a stock-path site did not match at start";
	}
	if (pick >= 0) {
		FI_FN(FIS_START, fi_start_fn)(pcomb, cd[pick].node, cd[pick].atk, o->hobj);
		FI_COUNT(starts[cd[pick].cls]);
		if (lg)
			logf_("FINISH start %s: FUN_00a8c700(pc=%#lx, node '%s', attack '%s', hobj=%#lx) sync %#lx", sf_class_name(cd[pick].cls),
			      (unsigned long)pcomb, cd[pick].nn, cd[pick].an, (unsigned long)o->hobj, (unsigned long)cd[pick].sync);
		sf_moment_start(&g_fi.mo, &g_fi.cfg, o->c, cd[pick].cls, t);
	} else if (g_fi.fallback && (want == SF_CLS_EXEC || want == SF_CLS_GROUND) && fi_kill_hit(pc, o, h, b, t)) {
		if (lg)
			logf_("FINISH %s by the fallback (%s)", sf_class_name(want), why);
		sf_moment_start(&g_fi.mo, &g_fi.cfg, o->c, want, t);
	} else {
		FI_COUNT(notoffered);
		if (lg)
			logf_("FINISH %s not started: %s", sf_class_name(want), why);
		return 0;
	}
	g_fi.claim_sync_seen = 0;
	st_yield(t); /* SOMVR_STEP: a step of this swing (or a dash) ends: the finisher's sync owns the bodies */
	in_haptic_event(hand, 1.0f, 0.25f, SOMVR_HAP_COMBAT);
	fi_slowmo(t);
	fi_glow(pcomb, 0, 1, t);
	return 1;
}

/* ---- per frame --------------------------------------------------------------------------------------------------- */
static int fi_holds(void) { return g_fi.mode == FI_ON && g_fi.mo.key != 0; }

/* the orc fights Talion (its target is his object): surely aware, no stealth kill to ask the game for */
static int fi_engaged(const struct cb_orc *o, uintptr_t pc)
{
	uintptr_t me = peek_ptr(pc + C_HOBJ);
	return me && peek_ptr(o->c + FI_C_TARGET) == me;
}

/* the previous blade sweeps to this one only as combat_frame's does: newer, at most 0.1 s, the tip within 150 cm */
static int fi_prev_ok(const somvr_blade *prev, const somvr_blade *b)
{
	return prev->valid && b->valid && b->t > prev->t && b->t - prev->t <= 0.1 && dist3(b->tip, prev->tip) <= CB_JUMP_CM;
}

static void fi_summary(double t)
{
	if (t < g_fi.next_log)
		return;
	g_fi.next_log = t + 1.0;
	struct fi_cnt *w = &g_fi.w;
	uint64_t g = 0, s = 0;
	for (int k = 0; k < SF_CLS_N; k++)
		g += w->gestures[k], s += w->starts[k];
	if (!(g || s || w->glow_on || w->glow_off || w->latches || w->near_miss || w->fx_fail) && t - g_fi.last_line < 10.0)
		return;
	logf_("FINISH t=%.3f last %.1f s: frames=%llu ready=%llu glow=+%llu/-%llu fx_fail=%llu judged=%llu near_miss=%llu "
	      "gestures=%llu starts=%llu fallbacks=%llu not_offered=%llu selects=%llu candidates=%llu latch=%llu "
	      "skipped_hits=%llu slowmo=%llu/%llu streak=%d suppressed=%llu", t, t - g_fi.last_line, (unsigned long long)w->frames,
	      (unsigned long long)w->ready_frames, (unsigned long long)w->glow_on, (unsigned long long)w->glow_off,
	      (unsigned long long)w->fx_fail, (unsigned long long)w->judged, (unsigned long long)w->near_miss,
	      (unsigned long long)g, (unsigned long long)s, (unsigned long long)w->fallbacks, (unsigned long long)w->notoffered,
	      (unsigned long long)w->selects, (unsigned long long)w->cands, (unsigned long long)w->latches,
	      (unsigned long long)w->skipped_hits, (unsigned long long)w->slowmo, (unsigned long long)w->slowmo_skip,
	      g_fi.streak, (unsigned long long)g_fi.suppressed);
	memset(w, 0, sizeof *w);
	g_fi.last_line = t;
}

/* the claim's end: the orc died, its sync started and ended, or the time is up */
static void fi_claim_tick(double t)
{
	uintptr_t k = g_fi.mo.key;
	if (!k)
		return;
	int dead = cb_dead(k), sync = cb_sync(k);
	if (sync)
		g_fi.claim_sync_seen = 1;
	int end = dead || (g_fi.claim_sync_seen && !sync);
	if (sf_moment_claim(&g_fi.mo, t, end))
		return;
	if (dead)
		FI_COUNT(claims_dead);
	else if (end)
		FI_COUNT(claims_sync);
	else
		FI_COUNT(claims_time);
	if (fi_log_ok(t))
		logf_("FINISH claim on %#lx ends: %s after %.2f s", (unsigned long)k, dead ? "dead" : end ? "its sync ended" :
		      "the time is up", t - g_fi.mo.t0);
}

/* in_frame (game thread, stereo with XR hands): the left grip as the dagger, the left hand's controls */
static void fi_hands(const sv_vec3 p[2], const sv_quat q[2], const sv_vec3 v[2], const sv_vec3 w[2])
{
	if (!g_fi.mode)
		return;
	const somvr_hand *L = &g_inp.hands[SOMVR_HAND_L];
	sc_grip g;
	memset(&g, 0, sizeof g);
	if (L->valid) {
		g.valid = 1, g.vel_valid = (L->vel_valid & 3) == 3, g.xr_t = (double)L->time * 1e-9;
		g.pos[0] = p[0].x, g.pos[1] = p[0].y, g.pos[2] = p[0].z;
		sv_quat_to(q[0], g.quat);
		g.vel[0] = v[0].x, g.vel[1] = v[0].y, g.vel[2] = v[0].z, g.avel[0] = w[0].x, g.avel[1] = w[0].y, g.avel[2] = w[0].z;
	}
	g_fi.dprev = g_fi.dag;
	somvr_blade_controller(&g_fi.dcfg, L->valid ? &g : NULL, &g_inp.rigxf, cb_now(), &g_fi.dhist, &g_fi.dag);
	if (g_fi.dag.valid)
		sc_speed_push(&g_fi.dsp, &g_fi.dsc, &g_fi.dag);
	else
		sc_speed_reset(&g_fi.dsp);
	sv_vec3 pw = somvr_rig_point(&g_rig, g_fr.base, p[0]);
	g_fi.lh_valid = L->valid, g_fi.lh_pos[0] = pw.x, g_fi.lh_pos[1] = pw.y, g_fi.lh_pos[2] = pw.z;
	g_fi.lh_squeeze = g_inp.st.hand[SOMVR_HAND_L].squeeze, g_fi.lh_trigger = g_inp.st.hand[SOMVR_HAND_L].trigger;
	g_fi.lh_upper = (g_inp.st.hand[SOMVR_HAND_L].buttons & SOMVR_BTN_UPPER) != 0;
	g_fi.lh_frame = g_fr.frame + 1; /* valid for this frame's combat_frame */
}

/* combat_frame, every frame (after the hit-stop's tick): claims, slow motion, the dagger, the latched hand */
static void fi_frame(uintptr_t pl, double t)
{
	if (!g_fi.mode)
		return;
	FI_COUNT(frames);
	fi_claim_tick(t);
	fi_slowmo_end(t, 0);
	uintptr_t pm = 0, obj = 0, pcomb = 0, pc = 0;
	float root[3];
	int body = pl && player_objs(pl, &pm, &obj) && peek(obj + OBJ_POS, root, sizeof root) && vec_ok(root, 1e7f);
	if (body && (pcomb = peek_ptr(pl + P_COMBAT)))
		pc = peek_ptr(pcomb + PC_SERVER);
	if (pc && (vptr_of((void *)pc) != g_cbg.vt_combat || !peek_ptr(pc + C_PLAYER)))
		pc = 0;
	if (g_fi.glow.on && t - g_fi.sword_t > 0.2)
		fi_glow(pcomb, 0, 1, t); /* no sword blade for a while (combat_frame returns before fi_sword then) */
	g_fi.streak = pc ? fi_streak(pc) : 0;
	if (g_fi.streak >= g_fi.cfg.exec_streak && g_fi.last_streak < g_fi.cfg.exec_streak)
		g_fi.streak_since = t;
	g_fi.last_streak = g_fi.streak;
	int hands = g_fi.lh_frame == g_fr.frame + 1 && !game_paused() && !vm_open();
	if (!pc || !hands) {
		if (g_fi.latch.held && sf_latch_step(&g_fi.latch, &g_fi.cfg, &(sf_latch_in){ .t = t }) && fi_log_ok(t))
			logf_("FINISH latch released (no hands or no player this frame)");
		fi_summary(t);
		return;
	}
	struct cb_orc o[FI_ORCS];
	int n = cb_orcs(pcomb, pc, root, o, FI_ORCS);
	const float *me = g_fr.head_ok ? g_fr.head : root;
	/* the dagger's stab (the left hand) from behind */
	if ((g_fi.stab & FI_STAB_LEFT) && g_fi.dag.valid && !g_fi.mo.key && sf_moment_cool(&g_fi.mo, &g_fi.cfg, t)) {
		for (int i = 0; i < n; i++) {
			if (o[i].dist > FI_RANGE_CM || fi_engaged(&o[i], pc))
				continue;
			struct fi_orcst *e = fi_orc_state(o[i].c, t);
			sf_zones z;
			float yaw;
			fi_zones(&o[i], e, &z, &yaw);
			sf_hit h;
			int why = sf_judge_stab(&g_fi.cfg, &g_fi.dsp, fi_prev_ok(&g_fi.dprev, &g_fi.dag) ? &g_fi.dprev : NULL, &g_fi.dag, &z,
						sf_behind_deg(o[i].pos, yaw, me), &h);
			FI_COUNT(judged);
			if (why == SF_WHY_OK) {
				int brutal = g_fi.lh_trigger >= g_fi.cfg.trigger_on; /* the wraith charge held: brutalize */
				fi_trigger(pcomb, pc, &o[i], brutal ? SF_CLS_BRUTAL : SF_CLS_STEALTH, brutal ? SF_CLS_STEALTH : SF_CLS_NONE,
					   &h, &g_fi.dag, SOMVR_HAND_L, t);
				break;
			}
			if (why != SF_WHY_MISS && why != SF_WHY_INSIDE && why != SF_WHY_FRONT) {
				FI_COUNT(near_miss);
				if (fi_log_ok(t))
					logf_("FINISH dagger near miss on %#lx: %s (speed %.2f along %.2f cos %.2f)", (unsigned long)o[i].c,
					      sf_why_name(why), g_fi.dsp.mean_mps, g_fi.dsp.axis_mps, g_fi.dsp.cos_axis);
			}
		}
	}
	/* the latched hand: the nearest stunned orc within reach of the left hand */
	if (g_fi.latch_on) {
		sf_latch_in li;
		memset(&li, 0, sizeof li);
		li.t = t, li.valid = g_fi.lh_valid, li.squeeze = g_fi.lh_squeeze, li.trigger = g_fi.lh_trigger;
		li.upper = g_fi.lh_upper;
		memcpy(li.hand, g_fi.lh_pos, sizeof li.hand);
		float best = 1e9f;
		int bi = -1, hi = -1;
		for (int i = 0; i < n; i++) {
			struct fi_orcst *e = fi_orc_state(o[i].c, t);
			sf_zones z;
			float yaw;
			fi_zones(&o[i], e, &z, &yaw);
			float d = dist3(z.neck, li.hand);
			if (o[i].c == g_fi.latch.key)
				hi = i, li.held_key_alive = o[i].c, memcpy(li.held_neck, z.neck, sizeof li.held_neck);
			if (e->kind == SF_ST_STUN && d < best)
				best = d, bi = i, li.key = o[i].c, memcpy(li.neck, z.neck, sizeof li.neck);
		}
		int ev = sf_latch_step(&g_fi.latch, &g_fi.cfg, &li);
		if (ev) {
			FI_COUNT(latch_ev[ev]);
			if (ev == SF_LEV_LATCH)
				FI_COUNT(latches), in_haptic_event(SOMVR_HAND_L, 0.6f, 0.05f, SOMVR_HAP_COMBAT);
			if (fi_log_ok(t))
				logf_("FINISH latch %s orc=%#lx (hand %.1f cm from its neck)", sf_lev_name(ev),
				      (unsigned long)(ev == SF_LEV_LATCH ? li.key : g_fi.latch.key), bi >= 0 ? best : -1.0f);
		}
		int ti = ev == SF_LEV_DRAIN || ev == SF_LEV_INTERROGATE ? hi : -1;
		if (ti >= 0 && !g_fi.mo.key) {
			sf_hit h;
			memset(&h, 0, sizeof h);
			memcpy(h.point, li.held_neck, sizeof h.point);
			if (ev == SF_LEV_DRAIN)
				fi_trigger(pcomb, pc, &o[ti], SF_CLS_DRAIN, SF_CLS_BRAND, &h, NULL, SOMVR_HAND_L, t);
			else
				fi_trigger(pcomb, pc, &o[ti], SF_CLS_INTERROGATE, SF_CLS_NONE, &h, NULL, SOMVR_HAND_L, t);
		}
	}
	fi_summary(t);
}

/* an orc's execution readiness: the streak (global, since it crossed the threshold) or its stun (since it began) */
static int fi_ready(const struct fi_orcst *e, double *since)
{
	int sready = g_fi.streak >= g_fi.cfg.exec_streak, tready = g_fi.cfg.exec_stunned && e->kind == SF_ST_STUN;
	*since = sready && tready ? (g_fi.streak_since < e->since ? g_fi.streak_since : e->since)
		 : sready ? g_fi.streak_since : e->since;
	return sready || tready;
}

/* combat_frame, after the orcs and this frame's sword blade are known: the swing's start, the glow, the ground
 * execution (its own zone: the body capsule stands up from a downed orc's root) */
static void fi_sword(uintptr_t pcomb, uintptr_t pc, const struct cb_orc *o, int n, const somvr_blade *b, int edge,
		     double t)
{
	if (!g_fi.mode)
		return;
	if (edge > 0)
		g_fi.swing_t0 = t;
	else if (edge < 0)
		g_fi.swing_t0 = -1;
	g_fi.sword_t = t;
	const somvr_blade *prev = fi_prev_ok(&g_fi.sprev, b) ? &g_fi.sprev : NULL;
	int ready_any = g_fi.streak >= g_fi.cfg.exec_streak; /* the streak's readiness is global (the HUD's) */
	for (int i = 0; i < n; i++) {
		if (o[i].dist > FI_RANGE_CM)
			continue;
		struct fi_orcst *e = fi_orc_state(o[i].c, t);
		double since;
		ready_any |= fi_ready(e, &since) && o[i].c != g_fi.mo.key;
		if (e->kind != SF_ST_DOWN || g_fi.mo.key || !sf_moment_cool(&g_fi.mo, &g_fi.cfg, t) || !b->valid)
			continue;
		sf_zones z;
		float yaw;
		fi_zones(&o[i], e, &z, &yaw);
		sf_hit h;
		FI_COUNT(judged);
		int why = sf_judge_ground(&g_fi.cfg, &g_cb.sp, prev, b, &z, 1, &h);
		if (why == SF_WHY_OK) {
			fi_trigger(pcomb, pc, &o[i], SF_CLS_GROUND, SF_CLS_EXEC, &h, b, SOMVR_HAND_R, t);
			break;
		}
		if (why == SF_WHY_TIP || why == SF_WHY_UP || why == SF_WHY_NOTSTAB) {
			FI_COUNT(near_miss);
			if (fi_log_ok(t))
				logf_("FINISH ground near miss on %#lx: %s (along-blade %.2f m/s cos %.2f)", (unsigned long)o[i].c,
				      sf_why_name(why), g_cb.sp.axis_mps, g_cb.sp.cos_axis);
		}
	}
	if (ready_any)
		FI_COUNT(ready_frames);
	fi_glow(pcomb, ready_any, !b->valid || g_fi.mo.key != 0, t);
	g_fi.sprev = *b;
}

/* combat_frame's contact loop, when a hit would land on orc o (its cooldown passed): 1 = no hit (the orc is the
 * finisher's; a downward stab into a downed orc is on its way to the torso; or this contact started a finisher: an
 * execution's cut at the head or neck, a stab from behind the game took as a stealth kill), 0 = combat's hit as usual */
static int fi_contact(uintptr_t pcomb, uintptr_t pc, const struct cb_orc *o, const sc_contact *ct, const somvr_blade *b,
		      double t)
{
	if (!g_fi.mode)
		return 0;
	if (g_fi.mo.key && o->c == g_fi.mo.key) {
		FI_COUNT(skipped_hits);
		return 1;
	}
	struct fi_orcst *e = fi_orc_state(o->c, t);
	if (e->kind == SF_ST_DOWN && sf_ground_hold(&g_fi.cfg, &g_cb.sp, b)) {
		FI_COUNT(held_hits);
		return 1;
	}
	if (g_fi.mo.key || !sf_moment_cool(&g_fi.mo, &g_fi.cfg, t))
		return 0;
	sf_zones z;
	float yaw;
	fi_zones(o, e, &z, &yaw);
	sf_hit h;
	memset(&h, 0, sizeof h);
	h.u = ct->u, h.along = ct->along, h.dist = ct->dist;
	memcpy(h.point, ct->point, sizeof h.point);
	double since;
	int ready = fi_ready(e, &since);
	FI_COUNT(judged);
	int why = sf_judge_exec_at(&g_fi.cfg, &g_cb.sp, g_fi.swing_t0, ct->point, &z, ready, since);
	if (why == SF_WHY_OK)
		return fi_trigger(pcomb, pc, o, SF_CLS_EXEC, SF_CLS_NONE, &h, b, SOMVR_HAND_R, t);
	if (ready && why != SF_WHY_NOSWING) {
		FI_COUNT(near_miss);
		if (fi_log_ok(t))
			logf_("FINISH near miss on %#lx (%s, streak %d): %s (contact y %.1f, neck %.1f; speed %.2f travel %.1f cos %.2f)",
			      (unsigned long)o->c, sf_state_name(e->kind), g_fi.streak, sf_why_name(why), ct->point[1], z.neck[1],
			      g_cb.sp.mean_mps, g_cb.sp.travel, g_cb.sp.cos_axis);
	}
	if (!(g_fi.stab & FI_STAB_RIGHT) || fi_engaged(o, pc))
		return 0;
	/* the sword's stab or slash from behind: a stealth kill when the game offers one (the stock strike on an unaware
	 * orc), else combat's hit */
	const float *me = g_fr.head_ok ? g_fr.head : b->base;
	if (sf_judge_back_at(&g_fi.cfg, &g_cb.sp, ct->point, ct->along, &z, sf_behind_deg(o->pos, yaw, me)) != SF_WHY_OK)
		return 0;
	return fi_trigger(pcomb, pc, o, SF_CLS_STEALTH, SF_CLS_NONE, &h, b, SOMVR_HAND_R, t);
}

/* ---- start and end ----------------------------------------------------------------------------------------------- */
static void fi_start(const struct site_spec *sites, int *bad)
{
	static const char *const mode_w[] = { "0", "1", "log" }, *const path_w[] = { "stock", "hit" },
				 *const onoff_w[] = { "0", "1" }, *const stab_w[] = { "off", "left", "right", "both" },
				 *const head_w[] = { "estimate", "node" };
	memset(&g_fi, 0, sizeof g_fi);
	g_fi_sites = sites;
	g_fi.mode = env_word("SOMVR_FINISH", mode_w, 3, 0);
	if (!g_fi.mode)
		return;
	if (g_cb.mode == CB_OFF) {
		logf_("WARN SOMVR_FINISH=%s needs SOMVR_COMBAT: the finishers stay off", mode_w[g_fi.mode]);
		g_fi.mode = FI_OFF;
		return;
	}
	sf_cfg *c = &g_fi.cfg;
	sf_cfg_default(c);
	c->exec_streak = (int)env_float("SOMVR_FINISH_STREAK", (float)c->exec_streak, 2, 30, bad);
	c->exec_stunned = env_word("SOMVR_FINISH_STUNNED", onoff_w, 2, c->exec_stunned);
	c->exec_mps = env_float("SOMVR_FINISH_SPEED", c->exec_mps, 2, 15, bad);
	c->exec_cm = env_float("SOMVR_FINISH_TRAVEL_CM", c->exec_cm, 0, 200, bad);
	c->exec_below_cm = env_float("SOMVR_FINISH_BELOW_CM", c->exec_below_cm, 0, 100, bad);
	c->neck_up_cm = env_float("SOMVR_FINISH_NECK_CM", c->neck_up_cm, 0, 200, bad);
	c->behind_deg = env_float("SOMVR_FINISH_BEHIND_DEG", c->behind_deg, 60, 180, bad);
	c->hold_s = env_float("SOMVR_FINISH_HOLD_MS", c->hold_s * 1000, 0, 3000, bad) / 1000;
	g_fi.path = env_word("SOMVR_FINISH_PATH", path_w, 2, FI_PATH_STOCK);
	g_fi.fallback = env_word("SOMVR_FINISH_FALLBACK", onoff_w, 2, 1);
	g_fi.sever = env_word("SOMVR_FINISH_SEVER", onoff_w, 2, 1);
	g_fi.stab = env_word("SOMVR_FINISH_STAB", stab_w, 4, FI_STAB_BOTH);
	g_fi.latch_on = env_word("SOMVR_FINISH_LATCH", onoff_w, 2, 1);
	g_fi.glow_on = env_word("SOMVR_FINISH_GLOW", onoff_w, 2, 1);
	g_fi.head_mode = env_word("SOMVR_FINISH_HEAD", head_w, 2, 1);
	g_fi.slowmo = env_float("SOMVR_FINISH_SLOWMO", 0, 0, 1, bad);
	g_fi.slowmo_s = env_float("SOMVR_FINISH_SLOWMO_MS", 500, 50, 3000, bad) / 1000;
	const char *fx = getenv("SOMVR_FINISH_GLOW_FX");
	snprintf(g_fi.glow_fx, sizeof g_fi.glow_fx, "%s", !fx || !fx[0] ? "fx\\characters\\player\\elf\\elf_lightchain.fxp"
							 : !strcmp(fx, "off") ? "" : fx);
	copy_env("SOMVR_FINISH_KILL_REC", g_fi.kill_name, sizeof g_fi.kill_name);
	sf_names_default(&g_fi.names);
	char nl[SF_CLS_N][64];
	for (int k = 1; k < SF_CLS_N; k++) {
		char key[64], up[16];
		snprintf(up, sizeof up, "%s", sf_class_name(k));
		for (char *s = up; *s; s++)
			*s = (char)(*s >= 'a' && *s <= 'z' ? *s - 32 : *s);
		snprintf(key, sizeof key, "SOMVR_FINISH_NAMES_%s", up);
		const char *v = getenv(key);
		if (v)
			sf_names_set(&g_fi.names, k, v);
		snprintf(nl[k], sizeof nl[k], "%s=%d%s", sf_class_name(k), g_fi.names.n[k], v ? "(env)" : "");
	}
	sc_blade_cfg_default(&g_fi.dcfg);
	g_fi.dcfg.back_cm = 4, g_fi.dcfg.len_cm = 30; /* Acharn: a dagger (design-vr.md 3; RT the model's length) */
	g_fi.dcfg.back_cm = env_float("SOMVR_FINISH_DAGGER_BACK_CM", g_fi.dcfg.back_cm, 0, 50, bad);
	g_fi.dcfg.len_cm = env_float("SOMVR_FINISH_DAGGER_CM", g_fi.dcfg.len_cm, 5, 120, bad);
	sc_cfg_default(&g_fi.dsc);
	sc_speed_reset(&g_fi.dsp);
	sf_glow_reset(&g_fi.glow);
	sf_moment_reset(&g_fi.mo);
	sf_latch_reset(&g_fi.latch);
	g_fi.swing_t0 = -1, g_fi.streak_since = -1e9;
	g_fi.last_line = cb_now(), g_fi.next_log = g_fi.last_line + 1.0;
	int ok[FIS_N];
	char why[512];
	for (int i = 0; i < FIS_N; i++) {
		ok[i] = check_site(&sites[i], why, sizeof why) == 0;
		if (!ok[i])
			logf_("FINISH site %s %#lx -> %#lx MISMATCH: %s", sites[i].name, (unsigned long)sites[i].addr,
			      (unsigned long)sites[i].target, why);
	}
	g_fi.ok_streak = ok[FIS_HITCOUNT];
	g_fi.ok_stock = ok[FIS_CTX] && ok[FIS_CTX_PC] && ok[FIS_WINDOW] && ok[FIS_SELECT] && ok[FIS_TGT_INIT] && ok[FIS_TGT_SET] &&
			ok[FIS_START] && ok[FIS_PLAYERROOT] && ok[FIS_WEAPON] && ok[FIS_DIRANGLE] && ok[FIS_KBMNODE];
	g_fi.ok_fx = ok[FIS_FX_CSINIT] && ok[FIS_FX_CREATE] && ok[FIS_FX_CSFREE] && ok[FIS_FX_RELEASE] && ok[FIS_OBJSYS_MH] && ok[FIS_ITEM];
	g_fi.ok_slowmo = ok[FIS_SLOWMO];
	char nw[512];
	g_fi.ok_node = g_fi.head_mode && check_site(&g_node_sites[NS_NODEXF], nw, sizeof nw) == 0;
	if (!g_fi.ok_streak) {
		logf_("WARN SOMVR_FINISH: FI_HITCOUNT did not match (the streak's offsets unproven): the finishers stay off");
		g_fi.mode = FI_OFF;
		return;
	}
	if (!g_fi.ok_stock && g_fi.path == FI_PATH_STOCK)
		logf_("WARN SOMVR_FINISH: a stock-path site did not match: %s", g_fi.fallback ? "executions by the fallback kill only, "
		      "no stealth kills, drains or interrogations" : "no finisher can start (SOMVR_FINISH_FALLBACK=0)");
	logf_("CONFIG finish mode=%s path=%s%s fallback=%d sever=%d kill_rec='%s' streak=%d stunned=%d speed=%.2f travel=%.0f "
	      "below=%.0f neck=%.0f head=%s behind=%.0f stab=%s latch=%d hold=%.2f glow=%d fx='%s'%s slowmo=%.2f for %.0f ms%s "
	      "dagger=%.0f/%.0f cm; names %s %s %s %s %s %s %s", mode_w[g_fi.mode], path_w[g_fi.path],
	      g_fi.ok_stock ? "" : " (its sites did not match)", g_fi.fallback, g_fi.sever,
	      g_fi.kill_name[0] ? g_fi.kill_name : "(the heavy tier's)", c->exec_streak, c->exec_stunned, c->exec_mps, c->exec_cm,
	      c->exec_below_cm, c->neck_up_cm, g_fi.ok_node ? "node" : "estimate", c->behind_deg, stab_w[g_fi.stab], g_fi.latch_on,
	      c->hold_s, g_fi.glow_on, g_fi.glow_fx, g_fi.ok_fx ? "" : " (an FX site did not match: no fx)", g_fi.slowmo,
	      g_fi.slowmo_s * 1000, g_fi.slowmo > 0 && !g_fi.ok_slowmo ? " (FI_SLOWMO did not match: none)" : "",
	      g_fi.dcfg.back_cm, g_fi.dcfg.len_cm, nl[1], nl[2], nl[3], nl[4], nl[5], nl[6], nl[7]);
}

static void fi_end_log(void)
{
	if (!g_fi.mode)
		return;
	fi_slowmo_end(0, 1);
	if (g_fi.fx_handle)
		fi_fx_stop("the end", mono());
	const struct fi_cnt *n = &g_fi.n;
	logf_("FINISH END frames=%llu ready=%llu glow=%llu fx_fail=%llu judged=%llu near_miss=%llu gestures exec=%llu "
	      "ground=%llu stealth=%llu brutal=%llu drain=%llu interrogate=%llu starts exec=%llu ground=%llu stealth=%llu "
	      "brutal=%llu drain=%llu brand=%llu interrogate=%llu fallbacks=%llu not_offered=%llu selects=%llu candidates=%llu "
	      "latches=%llu releases=%llu lost=%llu claims dead/sync/time=%llu/%llu/%llu skipped_hits=%llu held_hits=%llu "
	      "slowmo=%llu/%llu "
	      "no_head=%llu suppressed=%llu", (unsigned long long)n->frames, (unsigned long long)n->ready_frames,
	      (unsigned long long)n->glow_on, (unsigned long long)n->fx_fail, (unsigned long long)n->judged,
	      (unsigned long long)n->near_miss, (unsigned long long)n->gestures[SF_CLS_EXEC],
	      (unsigned long long)n->gestures[SF_CLS_GROUND], (unsigned long long)n->gestures[SF_CLS_STEALTH],
	      (unsigned long long)n->gestures[SF_CLS_BRUTAL], (unsigned long long)n->gestures[SF_CLS_DRAIN],
	      (unsigned long long)n->gestures[SF_CLS_INTERROGATE], (unsigned long long)n->starts[SF_CLS_EXEC],
	      (unsigned long long)n->starts[SF_CLS_GROUND], (unsigned long long)n->starts[SF_CLS_STEALTH],
	      (unsigned long long)n->starts[SF_CLS_BRUTAL], (unsigned long long)n->starts[SF_CLS_DRAIN],
	      (unsigned long long)n->starts[SF_CLS_BRAND], (unsigned long long)n->starts[SF_CLS_INTERROGATE],
	      (unsigned long long)n->fallbacks, (unsigned long long)n->notoffered, (unsigned long long)n->selects,
	      (unsigned long long)n->cands, (unsigned long long)n->latches, (unsigned long long)n->latch_ev[SF_LEV_RELEASE],
	      (unsigned long long)n->latch_ev[SF_LEV_LOST], (unsigned long long)n->claims_dead, (unsigned long long)n->claims_sync,
	      (unsigned long long)n->claims_time, (unsigned long long)n->skipped_hits, (unsigned long long)n->held_hits,
	      (unsigned long long)n->slowmo,
	      (unsigned long long)n->slowmo_skip, (unsigned long long)n->nohead, (unsigned long long)g_fi.suppressed);
}
