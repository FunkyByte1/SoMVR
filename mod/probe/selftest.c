/*
 * selftest.c - runs somvr_probe's patching, wrappers, command runner and every probe word against stand-in
 * functions and stand-in game objects in this program (no game): 14 vtable-slot hooks and 19 call-site hooks (CALLWRAP
 * post hooks, and PREWRAP pre-hooks, which must pass arguments - also on the stack - and return values through
 * untouched) are built at run time, the
 * `G` table is pointed at fake objects, and then each word is driven through the command file and the game-thread
 * tick. Checks: the read-only dumps, the virtual input (ON/OFF events, held value and is-on overrides, pass-through
 * for other records, release on time), the active words' guard refusal, dealhit through the game function with the
 * hit direction set and restored, piece hiding, the first-person camera write, autocounter on a parry window, the
 * near override, pose sampling, exec through a stand-in engine's checked slot 32 (the exact string, the escape and the
 * expansions, refusals on a slot mismatch, a foreign vtable, a null engine, an overlong line and without the guard),
 * pmcount through the P_PMROOT slot hook, pmmove writing only the documented PlayerMovement fields once, the log rate
 * limiter, and the P7 words: timers (the timer classes), pick, stimlog (the three callers and both registrations),
 * swingevent (the stimuli, `cur`'s three fields set and restored, the persistent stimulus removed, the AI counted),
 * aiwatch, keytimes (queued, sampled, measured: predicted hand = live hand), hitstop (own timer swapped in and back,
 * the references balanced; the rate variant), projlog/projblock/projreflect (removed by its own update; turned at the
 * shooter), fxlog/weaponfx (one shot, loop, stop), weapons/dagger (L_Weapon and back), syncinfo and `attacks full`
 * over direct and indirect database records, navdump, prompts (and watch), uiscene, soundlog/sound, @cout (the VFS
 * file found and copied), @ox, res/restall (the height through P_RES3D), piecehide and trav; and the P8 words: attached
 * and attachhide (pieces and the render node's skip byte, shown again), piecehide all, wnodes (the weapon's skeleton and
 * its attach entry), ikwatch/ikcb (the chain setup/update CALLWRAPs, the callback PREWRAPs with a stack argument),
 * armpose (ik: the hand at the target with the bone lengths kept; rot; the next update's read-back; the sword vs the
 * edited pose), aggro/minhealth (the scripted target set and cleared, the min health written and restored), pointer,
 * click, mstate, hitscan, padguard, mcmd and mouselog (the UI hooks pass through), surf (the ray slot and the surface
 * record), grav, mount, exec's console output and conlog (the output slot swapped and restored), hitstop onbpw (armed by
 * a counter prompt at Talion, FindTracker's tracker), the keytimes parry window, trav's head and the dagger's later
 * measure; and the P9 words: templates (the archetype one and two levels down), spawn (the template choice, the ray down,
 * the spawn call's arguments, the post steps, the pick, the checks), spawncaptain (refused in the world; bound, its level),
 * despawn, blocktable (entries, defaults, the chance per attack category), aiblockwatch over the roll path's hooks (the
 * gate, the record, the roll with its chance in xmm2 from an asm caller, blockroll force, the notify, the goal's
 * relevance on change, the shield pre-hook), incoming (plain and with our roll), dealhit's picked orc and recoil skip,
 * tod/weathers, wmdump/wmhold and aimray (yaw, pitch, the R_Hand origin, off); and the P10 words: P_ROOTDELTA (a call
 * site inside the stand-in vf_11: the game's delta through, then replaced), speed (rows, the graph values, the plateau and
 * the stop), pmrec (both read paths, the console names, set/restore through the console, base), heading (the scan: a
 * wall, a drop, Talion's own shape; clear/wall/back/open/orc), walk (the camera re-aim, the mirror check), loco const and
 * stick (in the world whatever Talion's facing; fsm 8 skipped; the lock; the stop), fling (momentum mode, the landing,
 * our return), animrate (the game's own write set back, restored; all), vaultlog (a sync episode) and npcpush (the spawn
 * along the heading, the walk, the push), and wmhold's fix (freeaim skips a null FreeAim; the exact name first).
 * Run by ./build.sh --check.
 */
#define SOMVR_SELFTEST
#include "somvr_probe.c"

#include <assert.h>

static int fails;
#define EXPECT(c, ...) do { if (!(c)) { fails++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)

/* ---------------------------------------------------------------- stand-in game functions ------------------- */

static struct {
	int on_calls, off_calls, mode_calls, hit_calls, setcam_calls, camreset_calls, hide_calls;
	int last_on_cmd, last_on_input, last_off_cmd, last_mode_pad;
	uintptr_t hit_attacker, hit_target, hit_attack;
	int hit_kill;
	float hit_pos[3], hit_dir[3], setcam_xf[8];
	uint8_t hit_27a;
	uint32_t hidden[16];
	int hidden_n, shown_n, child_hidden;
	int run_calls, find_calls, pm_consumed;
	void *run_eng;
	char run_s[1024];
} T;

static uintptr_t g_player, g_pi_obj, g_scombat;
static uintptr_t g_orc[3], g_orcobj[3];

static void t_cmd_on(uintptr_t pi, int cmd, int input)
{
	(void)pi;
	T.on_calls++;
	T.last_on_cmd = cmd;
	T.last_on_input = input;
}
static void t_cmd_off(uintptr_t pi, int cmd, int input)
{
	(void)pi, (void)input;
	T.off_calls++;
	T.last_off_cmd = cmd;
}
static uint64_t t_cmd_active(uintptr_t pi, unsigned cmd)
{
	return cmd < NCMD ? (rq(pi + PI_ACTIVESET + 8 * (cmd / 64)) >> (cmd % 64)) & 1 : 0;
}
static void t_input_mode(uintptr_t im, uintptr_t entry, int pad)
{
	(void)im, (void)entry;
	T.mode_calls++;
	T.last_mode_pad = pad;
	uintptr_t prof = rq(entry + EN_PROFILE);
	*(uint8_t *)(prof + PROF_PAD) = (uint8_t)pad;
}
static void t_dealhit(ltvec pos, uintptr_t attacker, uintptr_t target, uintptr_t attack, int kill)
{
	T.hit_calls++;
	T.hit_attacker = attacker;
	T.hit_target = target;
	T.hit_attack = attack;
	T.hit_kill = kill;
	T.hit_pos[0] = pos.x;
	T.hit_pos[1] = pos.y;
	T.hit_pos[2] = pos.z;
	memcpy(T.hit_dir, (void *)(attacker + C_HITDIR), sizeof T.hit_dir); /* what the game would read */
	T.hit_27a = *(uint8_t *)(attacker + C_HITONCE);
}

static uintptr_t g_atk_cat;
static uintptr_t t_atk_byname(const char *name)
{
	uintptr_t base = rq(g_atk_cat + CAT_RECS);
	uint32_t n = r32(g_atk_cat + CAT_COUNT);
	char buf[128];
	for (uint32_t i = 0; i < n; i++) {
		uintptr_t rec = base + (uintptr_t)i * DBREC_SIZE;
		if (!strcasecmp(rec_name(rec, buf, sizeof buf), name))
			return rec;
	}
	return 0;
}

static int g_t_rhand; /* P9: GetNode("R_Hand") on the player object resolves (the aimray `hand` test only) */
static uintptr_t g_cam_for_rd, g_charobj_for_rd; /* P10: the camera and the root object for t_rootdelta's mode 2 */
static uintptr_t g_model, g_charobj;
static uintptr_t g_child[2], g_childsm, g_wsm, g_wobj; /* P8: an attached child with a model, one without; the sword's */
static uintptr_t t_modelof(uintptr_t obj)
{
	return obj == g_charobj ? g_model : obj == g_child[0] ? g_childsm : obj && obj == g_wobj ? g_wsm : 0;
}

/* the skeleton: nodes 0-4 as before, 5-9 the right arm (R_Armu -> R_Arml -> R_Hand -> R_Weapon, R_Index1) */
#define T_NN 10
static const char *const T_NODES[T_NN] = { "Head", "Neck", "Pelvis", "L_Hand", "L_Weapon", "R_Armu", "R_Arml", "R_Hand",
					    "R_Weapon", "R_Index1" };
static const uint8_t T_PARENT[T_NN] = { 0xff, 0, 1, 1, 3, 1, 5, 6, 7, 7 };
static uint32_t t_hash_of(int i); /* the skeleton's stored hash of node i */
static uintptr_t g_pose_xf;      /* P8: the player tracker's pose buffer rows (model space, 0x20 each) */
static int t_arm_node(uint32_t node) /* the skeleton index of an arm node hash (5..9), else -1 */
{
	for (int i = 5; i < T_NN; i++)
		if (t_hash_of(i) == node)
			return i;
	return -1;
}
static uint64_t t_getnode(uintptr_t m, uintptr_t obj, const char *name, uint32_t *node)
{
	(void)m;
	if (obj == g_orcobj[1] && !strcmp(name, "R_Hand")) { /* keytimes: the attacker's hand */
		*node = 0x3000;
		return 0;
	}
	if (obj != g_charobj)
		return 1;
	if (g_t_rhand && !strcmp(name, "R_Hand")) { /* P9 aimray hand: the arm node (GetNodeTransform from the pose) */
		*node = t_hash_of(7);
		return 0;
	}
	for (size_t i = 0; i < 5; i++)
		if (!strcmp(name, T_NODES[i])) {
			*node = 0x1000 + (uint32_t)i;
			return 0;
		}
	return 1;
}
static uint64_t t_getsocket(uintptr_t m, uintptr_t obj, const char *name, uint32_t *out)
{
	(void)m, (void)obj;
	if (strcmp(name, "SocketHead"))
		return 1;
	*out = 0x2000;
	return 0;
}
static float g_head_up = 170.0f;
static uint64_t t_nodexf(uintptr_t m, uintptr_t obj, uint32_t node, float *xf, int world)
{
	(void)m, (void)world;
	if (obj == g_orcobj[1] && node == 0x3000) { /* the orc's R_Hand = its object + the SampleNode stand-in's model pos */
		float o3[3];
		obj_pos(obj, o3);
		xf[0] = o3[0] + 10.0f, xf[1] = o3[1] + 150.0f, xf[2] = o3[2] + 30.0f;
		return 0;
	}
	if (obj && obj == g_wobj && node >= 0x5000 && node < 0x5003) { /* the sword's nodes: 30 cm apart along its +Z */
		float o3[3];
		obj_pos(obj, o3);
		xf[0] = o3[0], xf[1] = o3[1], xf[2] = o3[2] + 30.0f * (float)(node - 0x5000);
		xf[3] = xf[4] = xf[5] = 0, xf[6] = 1;
		return 0;
	}
	if (obj != g_charobj)
		return 1;
	float op[3];
	obj_pos(obj, op);
	int an = t_arm_node(node);
	if (an >= 0 && g_pose_xf) { /* GetNodeTransform of an arm node = the object transform x the pose buffer's row */
		float q[4], w[3];
		memcpy(q, (void *)(obj + OBJ_ROT), 16);
		qrot(q, (float *)(g_pose_xf + (uintptr_t)an * 32), w);
		for (int k = 0; k < 3; k++)
			xf[k] = op[k] + w[k];
		qmul(q, (float *)(g_pose_xf + (uintptr_t)an * 32 + 16), xf + 3);
		xf[7] = 1;
		return 0;
	}
	float up = node == 0x1000 ? g_head_up : node == 0x1001 ? 150.0f : node == 0x2000 ? 172.0f : 100.0f;
	xf[0] = op[0];
	xf[1] = op[1] + up;
	xf[2] = op[2];
	xf[3] = 0;
	xf[4] = 0;
	xf[5] = 0;
	xf[6] = 1;
	xf[7] = 1;
	return 0;
}
static uint64_t t_hashindex(uintptr_t m, uintptr_t obj, uint32_t node)
{
	(void)m, (void)obj;
	return node >= 0x1000 && node < 0x1005 ? node - 0x1000 : 0xffffffffu;
}
static uint32_t t_hash_of(int i) { return i == 4 || i >= 5 ? name_hash(T_NODES[i]) : 0x1000u + (unsigned)i; }
static const char *const T_PIECES[] = { "SMC_BaseModel", "SMC_HeadModel", "SMC_HairModel", "SMC_BodyModel",
					"SMC_HelmetModel" };
static uint64_t t_numpieces(uintptr_t m, uintptr_t obj, uint32_t *count)
{
	(void)m;
	if (obj == g_child[0]) {
		*count = 3;
		return 0;
	}
	if (obj != g_charobj)
		return 1;
	*count = (uint32_t)(sizeof T_PIECES / sizeof T_PIECES[0]);
	return 0;
}
static uint64_t t_piecename(uintptr_t m, uintptr_t obj, uint32_t idx, char *buf, int len)
{
	(void)m, (void)obj;
	if (idx >= sizeof T_PIECES / sizeof T_PIECES[0])
		return 1;
	snprintf(buf, (size_t)len, "%s", T_PIECES[idx]);
	return 0;
}
static uint64_t t_getpiece(uintptr_t m, uintptr_t obj, const char *name, uint32_t *piece)
{
	(void)m, (void)obj;
	for (size_t i = 0; i < sizeof T_PIECES / sizeof T_PIECES[0]; i++)
		if (!strcmp(name, T_PIECES[i])) {
			*piece = (uint32_t)i;
			return 0;
		}
	return 1;
}
static uint64_t t_hidepiece(uintptr_t m, uintptr_t obj, uint32_t piece, int hidden)
{
	(void)m;
	uintptr_t sm = t_modelof(obj); /* the game-side mask +0x6c, as CSimpleModel::vf_50 keeps it */
	if (sm && piece < 32) {
		uint32_t mk = r32(sm + SM_MASK);
		mk = hidden ? mk | 1u << piece : mk & ~(1u << piece);
		memcpy((void *)(sm + SM_MASK), &mk, 4);
	}
	if (obj == g_child[0]) {
		T.child_hidden += hidden ? 1 : -1;
		return 0;
	}
	T.hide_calls++;
	if (hidden) {
		if (T.hidden_n < 16)
			T.hidden[T.hidden_n++] = piece;
	} else {
		T.shown_n++;
	}
	return 0;
}
static uint64_t t_videocover(uintptr_t im) { (void)im; return 0; }
static void t_setcam(uintptr_t body, const float *xf)
{
	(void)body;
	T.setcam_calls++;
	memcpy(T.setcam_xf, xf, sizeof T.setcam_xf);
}
static void t_camreset(uintptr_t cam) { (void)cam; T.camreset_calls++; }
static uintptr_t t_localplayer(uintptr_t shell) { (void)shell; return g_player; }

/* the console: a stand-in engine (vtable slot 32 = RunConsoleString), the repository's find, two programs and a bool */
static void *volatile g_engvt[96], *volatile g_repovt[12];
static uintptr_t g_eng, g_eng_cell, g_srv_cell, g_repo, g_var, g_tele, g_fdump;
static uintptr_t g_pmv[2], g_pmrec; /* P10: PlayerMovement.BaseSpeedMultiplier, Client.PlayerMovement.FallMinHeight; the record */
static void mkdirs(const char *path);
static int t_runstring(void *eng, const char *s)
{
	T.run_calls++;
	T.run_eng = eng;
	snprintf(T.run_s, sizeof T.run_s, "%s", s);
	const char *a = strstr(s, "Combat.Debug.Attack ");
	if (a)
		*(uint8_t *)(g_var + 0x38) = (uint8_t)atoi(a + 20);
	const char *fd = strstr(s, "Faction.Dump C:\\Temp\\");
	if (fd) { /* Feral's VFS: C:\\Temp = XDG_DATA_HOME/feral-interactive/ShadowOfMordor/VFS/Temp */
		char dir[1100], path[1300];
		snprintf(dir, sizeof dir, "%s/feral-interactive/ShadowOfMordor/VFS/Temp", getenv("XDG_DATA_HOME"));
		mkdirs(dir);
		snprintf(path, sizeof path, "%s/%.*s", dir, (int)strcspn(fd + 21, " ;"), fd + 21);
		FILE *f = fopen(path, "w");
		if (f) {
			fputs("faction members: 3\n", f);
			fclose(f);
		}
	}
	if (strstr(s, "AIDebug.ListAIs")) { /* the console's printf: the repository's output function at +8 */
		void (*out)(const uint32_t *, int, const char *) = (void (*)(const uint32_t *, int, const char *))rq(g_repo + 8);
		if (out)
			out(NULL, 2, "AI 'AI_Infantry' at 300 0 200\n");
	}
	const char *bs = strstr(s, "PlayerMovement.BaseSpeedMultiplier ");
	if (bs && g_pmrec) { /* the variable writes the record's attribute 16 (entry 16, direct) */
		float v = (float)atof(bs + 35);
		uintptr_t e = rq(g_pmrec + 8) + 16 * 16;
		uint16_t slot;
		memcpy(&slot, (void *)(e + 0xe), 2);
		memcpy((void *)(rq(g_pmrec + 0x10) + 8u * slot), &v, 4);
	}
	const char *t = strstr(s, "Teleport ");
	float x, y, z;
	if (t && sscanf(t + 9, "%f %f %f", &x, &y, &z) == 3) {
		memcpy((void *)(g_charobj + OBJ_POS), &x, 4);
		memcpy((void *)(g_charobj + OBJ_POS + 4), &y, 4);
		memcpy((void *)(g_charobj + OBJ_POS + 8), &z, 4);
	}
	return 0;
}
__attribute__((noinline, noclone)) static uint64_t t_srvgate(void *eng) { (void)eng; return 1; }
__attribute__((noinline, noclone)) static void t_exec(void *vars, const char *s) { (void)vars, (void)s; }
static uintptr_t t_find(uintptr_t repo, const char *name)
{
	T.find_calls++;
	if (repo != g_repo)
		return 0;
	return !strcasecmp(name, "Combat.Debug.Attack") ? g_var : !strcasecmp(name, "Teleport") ? g_tele :
	       !strcasecmp(name, "Faction.Dump") ? g_fdump : !strcasecmp(name, "PlayerMovement.BaseSpeedMultiplier") ? g_pmv[0] :
	       !strcasecmp(name, "Client.PlayerMovement.FallMinHeight") ? g_pmv[1] : 0;
}

/* ---------------------------------------------------------------- stand-in hook targets --------------------- */

static void *volatile g_vt[120];        /* fake CWBInput / CGameServerShell / CRendererHandle vtables */
static int g_tick_calls, g_server_calls, g_vis_calls, g_ui105_calls;
static float g_last_value_arg;

__attribute__((noinline, noclone)) static void o_input(void *self) { (void)self; g_tick_calls++; }
__attribute__((noinline, noclone)) static float o_value(void *self, void *rec)
{
	(void)self;
	g_last_value_arg = (float)(uintptr_t)rec;
	return 7.0f;
}
__attribute__((noinline, noclone)) static float o_value2(void *self, void *rec) { (void)self, (void)rec; return 8.0f; }
__attribute__((noinline, noclone)) static uint64_t o_ison(void *self, void *rec, uint64_t a3)
{
	(void)self, (void)rec, (void)a3;
	return 0;
}
__attribute__((noinline, noclone)) static void o_server(void *self) { (void)self; g_server_calls++; }
__attribute__((noinline, noclone)) static uint64_t o_vis(void *self, void *view, uint64_t f)
{
	(void)self, (void)view;
	g_vis_calls++;
	return f + 1;
}
__attribute__((noinline, noclone)) static uint64_t o_ui105(void *self, uint64_t t, uint64_t m)
{
	(void)self;
	g_ui105_calls++;
	return t + m;
}

/* call-site stand-ins */
static int o_anim_calls, o_campost_calls, o_atk_calls, o_gamehit_calls, o_counter_calls, o_death_calls;
static uintptr_t o_atk_args[5];
static float o_gamehit_pos[3];

__attribute__((noinline, noclone)) static uint64_t t_anim(uintptr_t body)
{
	o_anim_calls++;
	g_head_up += 1.0f; /* the animation update moves the head node */
	return body + 1;
}
__attribute__((noinline, noclone)) static uint64_t t_campost(uintptr_t player)
{
	o_campost_calls++;
	return player + 2;
}
__attribute__((noinline, noclone)) static uint64_t t_atk(uintptr_t c, uintptr_t node, uintptr_t attack, uintptr_t target,
							int flag)
{
	o_atk_calls++;
	o_atk_args[0] = c;
	o_atk_args[1] = node;
	o_atk_args[2] = attack;
	o_atk_args[3] = target;
	o_atk_args[4] = (uintptr_t)flag;
	return 0x1234;
}
__attribute__((noinline, noclone)) static uint64_t t_gamehit(ltvec pos, uintptr_t attacker, uintptr_t target,
							    uintptr_t attack, int kill)
{
	(void)attacker, (void)target, (void)attack, (void)kill;
	o_gamehit_calls++;
	o_gamehit_pos[0] = pos.x;
	o_gamehit_pos[1] = pos.y;
	o_gamehit_pos[2] = pos.z;
	return 7;
}
__attribute__((noinline, noclone)) static uint64_t t_counter(uintptr_t orc, int ms)
{
	o_counter_calls++;
	return orc + (uintptr_t)ms;
}
__attribute__((noinline, noclone)) static uint64_t t_death(uintptr_t victim, uintptr_t attack, uintptr_t killer, int x)
{
	(void)attack, (void)killer, (void)x;
	o_death_calls++;
	return victim;
}

/* the callers that hold the hooked call sites */
__attribute__((noinline, noclone)) static uint64_t c_anim(uintptr_t body) { return t_anim(body) + 10; }
__attribute__((noinline, noclone)) static uint64_t c_campost(uintptr_t p) { return t_campost(p) + 20; }
__attribute__((noinline, noclone)) static uint64_t c_atk(uintptr_t c, uintptr_t n, uintptr_t a, uintptr_t t, int f)
{
	return t_atk(c, n, a, t, f) + 30;
}
__attribute__((noinline, noclone)) static uint64_t c_gamehit(ltvec p, uintptr_t a, uintptr_t t, uintptr_t r, int k)
{
	return t_gamehit(p, a, t, r, k) + 40;
}
__attribute__((noinline, noclone)) static uint64_t c_counter(uintptr_t o, int ms) { return t_counter(o, ms) + 50; }
__attribute__((noinline, noclone)) static uint64_t c_death(uintptr_t v, uintptr_t a, uintptr_t k, int x)
{
	return t_death(v, a, k, x) + 60;
}

/* PlayerMovement::vf_11 as re-m1m2.md 3.2 / design-locomotion.md 2.1 step 3 have it: the root delta FUN_00a513a0(body,
 * &delta) (P10: the call P_ROOTDELTA hooks), the pending extra added to it (and reset), the translation lock
 * (mgr+0x62a unless pm+0x38), then xf.pos += R(xf.rot) delta.pos */
static uintptr_t g_pm;
static int g_pm_run = 1; /* the selftest's tick calls vf_11 after the input update */
static float g_t_rd[3];  /* P10: the stand-in animation's translation per call (Talion's frame) */
static int g_t_rd_mode;  /* 0: g_t_rd; 1: 5 cm forward x the probe's ForwardAxis; 2: 5 cm x the probe's stick through
                          * the camera's yaw with x mirrored, in the world (walk's mirror check) */
static int g_t_rd_calls;
__attribute__((noinline, noclone)) static void t_rootdelta(uintptr_t body, float *d)
{
	(void)body;
	g_t_rd_calls++;
	float sx, sy;
	if (g_t_rd_mode == 1) {
		probe_stick(&sx, &sy);
		d[2] = 5.0f * sy;
	} else if (g_t_rd_mode == 2) {
		struct ctx cc;
		float cy = 0, f[3], r[3], w[3], q[4];
		memset(&cc, 0, sizeof cc);
		cc.cam = g_cam_for_rd;
		cam_yaw(&cc, &cy);
		probe_stick(&sx, &sy);
		yaw_dir(cy, f, r);
		for (int k = 0; k < 3; k++)
			w[k] = (-sx * r[k] + sy * f[k]) * 5.0f;
		memcpy(q, (void *)(g_charobj_for_rd + OBJ_ROT), sizeof q);
		qrot_inv(q, w, d);
	} else {
		memcpy(d, g_t_rd, 12);
	}
}
__attribute__((noinline, noclone)) static void o_pmroot(void *pm, void *xfp)
{
	float *xf = xfp, d[7] = { 0, 0, 0, 0, 0, 0, 1 }, w[3];
	uintptr_t m = (uintptr_t)pm, body = rq(rq(m + PM_OWNER) + P_BODY);
	t_rootdelta(body, d);
	if (*(uint8_t *)(m + PM_PENDING)) {
		float e[3];
		memcpy(e, (void *)(m + PM_XPOS), 12);
		for (int k = 0; k < 3; k++)
			d[k] += e[k];
		static const float zero[3], rest[4] = { 0, 0, 0, 1 };
		memcpy((void *)(m + PM_XPOS), zero, 12);
		memcpy((void *)(m + PM_XROT), rest, 16);
		*(uint8_t *)(m + PM_PENDING) = 0;
		T.pm_consumed++;
	}
	uintptr_t mgr = rq(body + BODY_MGR);
	if (!rq(m + PM_ATK) && mgr && *(uint8_t *)(mgr + MGR_LOCK_T))
		d[0] = d[1] = d[2] = 0;
	qrot(xf + 3, d, w);
	for (int k = 0; k < 3; k++)
		xf[k] += w[k];
}

/* ---------------------------------------------------------------- site building ------------------------------ */

static char g_hex[HK_N][2][256];
static struct site_spec g_sites[HK_N];

static int mk_call(int hk, const char *name, void *caller, void *target)
{
	const unsigned char *p = caller;
	for (int k = 0; k < 1024; k++) {
		int32_t rel;
		memcpy(&rel, p + k + 1, 4);
		if (p[k] == 0xe8 && (uintptr_t)(p + k + 5 + rel) == (uintptr_t)target) {
			hex_of(p + k, 5, g_hex[hk][0], 256);
			hex_of(target, 6, g_hex[hk][1], 256);
			g_sites[hk] = (struct site_spec){ name, K_CALL, (uintptr_t)(p + k), (uintptr_t)(p + k), g_hex[hk][0],
							  (uintptr_t)target, g_hex[hk][1] };
			return 0;
		}
	}
	printf("FAIL no call site for %s in the stand-in caller\n", name);
	fails++;
	return -1;
}

static void mk_vslot(int hk, const char *name, int slot, void *target)
{
	g_vt[slot] = target;
	hex_of((const unsigned char *)&g_vt[slot], 8, g_hex[hk][0], 256);
	hex_of(target, 6, g_hex[hk][1], 256);
	g_sites[hk] = (struct site_spec){ name, K_VSLOT, (uintptr_t)&g_vt[slot], (uintptr_t)&g_vt[slot], g_hex[hk][0],
					  (uintptr_t)target, g_hex[hk][1] };
}

static char g_rohex[RO_N][2][400];
static struct site_spec g_ro_test[RO_N];

/* a console slot site as the doc prints them: the context = `before` slots ahead of the slot, `n` slots long */
static void mk_ro_slot(int i, const char *name, void *volatile *vt, int slot, int before, int n, void *target)
{
	vt[slot] = target;
	hex_of((const unsigned char *)&vt[slot - before], 8 * n, g_rohex[i][0], 400);
	hex_of(target, 6, g_rohex[i][1], 400);
	g_ro_test[i] = (struct site_spec){ name, K_VSLOT, (uintptr_t)&vt[slot], (uintptr_t)&vt[slot - before], g_rohex[i][0],
					   (uintptr_t)target, g_rohex[i][1] };
}
static void mk_ro_entry(int i, const char *name, void *fn)
{
	hex_of(fn, 16, g_rohex[i][0], 400);
	hex_of(fn, 15, g_rohex[i][1], 400);
	g_ro_test[i] = (struct site_spec){ name, K_ENTRY, (uintptr_t)fn, (uintptr_t)fn, g_rohex[i][0], (uintptr_t)fn, g_rohex[i][1] };
}

/* ---------------------------------------------------------------- stand-in game objects --------------------- */

#define VT_INPUT 4
#define VT_VALUE 22
#define VT_VALUE2 23
#define VT_ISON 25
#define VT_SERVER 40
#define VT_VIS 60
#define VT_UI105 61
#define VT_PMROOT 70
#define VT_AI0 71 /* the five AI slots: 71..75 */
#define VT_PJ 76
#define VT_ADDCB 77 /* P8 */
#define VT_REMCB 78
#define VT_TRK 79
#define VT_LISTEN 80
#define VT_SYS0 81 /* 81..83 down, up, move */
#define VT_VIEW0 84 /* 84..86 */
#define VT_GFX 87
#define VT_GRAV 88
#define VT_GOALREL 89 /* P9 */

static void *zalloc(size_t n)
{
	void *p = calloc(1, n);
	assert(p);
	return p;
}
static void put(uintptr_t base, size_t off, uint64_t v) { memcpy((char *)base + off, &v, 8); }
static void put32(uintptr_t base, size_t off, uint32_t v) { memcpy((char *)base + off, &v, 4); }
static void putf(uintptr_t base, size_t off, float v) { memcpy((char *)base + off, &v, 4); }

static uintptr_t g_shell, g_im, g_msm, g_entry, g_profile, g_cam, g_body, g_pc, g_device, g_near_cell, g_guard_cell;
static uintptr_t g_recs, g_valrecs, g_ctxobj, g_entries;
static uintptr_t g_shell_slot, g_model_slot, g_cat_slot, g_pause_slot, g_pausestate;
static uint8_t g_allow[2];
static uintptr_t g_tagrec[2];

/* ---------------------------------------------------------------- P8 stand-ins -------------------------------- */

static struct {
	int iksetup, ikchain, addcb, remcb, trk, listen, guard, sys, view, gfx, grav;
	uint64_t addcb_args[7], remcb_args[6], sys_args[3], view_args[4], gfx_args[2];
	int tgt_calls, clr_calls, move_calls, down_calls, up_calls, last_btn, last_pl, last_pos[2];
	int clipview, tostage, cliphit, ray, surfrec, conout, mstate;
	char conout_text[256];
} V;

/* the IK chain setup (FUN_02185900(chain, hObj, record)) and the per-frame update (FUN_02185590(chain)) */
__attribute__((noinline, noclone)) static uint64_t t_iksetup(uintptr_t ch, uintptr_t h, uintptr_t rec)
{
	V.iksetup++;
	put(ch, 0x18, h);
	put(ch, 0x20, rec);
	put32(ch, 0x90, name_hash("R_Armu"));
	put32(ch, 0x94, name_hash("R_Arml"));
	put32(ch, 0x98, name_hash("R_Hand"));
	putf(ch, 0xa0, 33.92f);
	putf(ch, 0xa4, 27.42f);
	return 0x5a;
}
__attribute__((noinline, noclone)) static uint64_t c_iksetup(uintptr_t ch, uintptr_t h, uintptr_t rec) { return t_iksetup(ch, h, rec) + 1; }
__attribute__((noinline, noclone)) static uint64_t c_iksetup2(uintptr_t ch, uintptr_t h, uintptr_t rec) { return t_iksetup(ch, h, rec) + 2; }
__attribute__((noinline, noclone)) static uint64_t t_ikchain(uintptr_t ch)
{
	V.ikchain++;
	putf(ch, 0x74, 11.0f); /* the world target */
	putf(ch, 0x78, 22.0f);
	putf(ch, 0x7c, 33.0f);
	return ch & 0xff;
}
__attribute__((noinline, noclone)) static uint64_t c_ikchain(uintptr_t ch) { return t_ikchain(ch) + 3; }

/* IEngineModel slots 38/39: seven arguments (the last on the stack) and six */
__attribute__((noinline, noclone)) static uint64_t o_addcb(uint64_t m, uint64_t h, uint64_t node, uint64_t type, uint64_t fl,
							   uint64_t owner, uint64_t size)
{
	uint64_t a[7] = { m, h, node, type, fl, owner, size };
	memcpy(V.addcb_args, a, sizeof a);
	V.addcb++;
	return 0x61;
}
__attribute__((noinline, noclone)) static uint64_t o_remcb(uint64_t m, uint64_t h, uint64_t node, uint64_t type, uint64_t fl,
							   uint64_t owner)
{
	uint64_t a[6] = { m, h, node, type, fl, owner };
	memcpy(V.remcb_args, a, sizeof a);
	V.remcb++;
	return 0x62;
}

/* CSimpleModelTrackerNode::vf_03: the animation's pose into the buffer (the base pose, every call) */
static float g_base_pose[T_NN][8];
static uintptr_t g_trk, g_pb, g_attach, g_entries8;
__attribute__((noinline, noclone)) static uint64_t o_trk(uint64_t trk)
{
	V.trk++;
	if (trk == g_trk)
		memcpy((void *)g_pose_xf, g_base_pose, sizeof g_base_pose);
	return 0x71;
}
/* CSimpleModelAttachNode::vf_02: each child placed at object x pose[node] x offset (as phase 7 does) */
__attribute__((noinline, noclone)) static uint64_t o_listen(uint64_t an)
{
	V.listen++;
	uintptr_t b = rq(an + AT_ENTRIES), e = rq(an + AT_ENTRIES + 8);
	for (uintptr_t en = b; en < e; en += AT_ESIZE) {
		uintptr_t ch = rq(en + AE_CHILD);
		uint32_t idx = r32(en + AE_INDEX);
		if (!ch || idx >= T_NN)
			continue;
		float *row = (float *)(g_pose_xf + (uintptr_t)idx * 32), off[7], t[3], w[3], X[3], q[4];
		memcpy(off, (void *)(en + AE_OFFSET), 28);
		qrot(row + 4, off, t);
		for (int k = 0; k < 3; k++)
			t[k] += row[k];
		memcpy(X, (void *)(g_charobj + OBJ_POS), 12);
		memcpy(q, (void *)(g_charobj + OBJ_ROT), 16);
		qrot(q, t, w);
		for (int k = 0; k < 3; k++)
			w[k] += X[k];
		memcpy((void *)(ch + OBJ_POS), w, 12);
	}
	return 0x72;
}

/* the pad guard FUN_00a38e80(P) and two callers (hover, click) */
__attribute__((noinline, noclone)) static uint64_t t_padmode(uintptr_t P) { (void)P; V.guard++; return 1; }
__attribute__((noinline, noclone)) static uint64_t c_guard_h(uintptr_t P) { return t_padmode(P) * 10 + 1; }
__attribute__((noinline, noclone)) static uint64_t c_guard_c(uintptr_t P) { return t_padmode(P) * 10 + 2; }

/* the system-event handler, CUIView and GFx slots */
__attribute__((noinline, noclone)) static uint64_t o_sys(uint64_t h, uint64_t a, uint64_t b)
{
	V.sys++;
	V.sys_args[0] = h, V.sys_args[1] = a, V.sys_args[2] = b;
	return 0x81;
}
__attribute__((noinline, noclone)) static uint64_t o_view(uint64_t v, uint64_t a, uint64_t b, uint64_t c)
{
	V.view++;
	V.view_args[0] = v, V.view_args[1] = a, V.view_args[2] = b, V.view_args[3] = c;
	return 0x82;
}
__attribute__((noinline, noclone)) static uint64_t o_gfx(uint64_t mv, uint64_t ev)
{
	V.gfx++;
	V.gfx_args[0] = mv, V.gfx_args[1] = ev;
	return 0x83;
}
/* PlayerMovement::vf_06: gravity wished */
__attribute__((noinline, noclone)) static uint64_t o_grav(uint64_t pm) { (void)pm; V.grav++; return 1; }

/* SetCombatScriptedTarget / Clear (FUN_0296f870 / FUN_0296f940 as read: npc+0xc78 -> +0xb60 = *(target+8), +0xb68) */
static void t_scripttgt(uintptr_t npc, uintptr_t target, int sticky)
{
	uintptr_t ai = rq(npc + NPC_AI);
	V.tgt_calls++;
	put(ai, AI_SCRIPTTGT, rq(target + GB_HOBJ));
	*(uint8_t *)(ai + AI_STICKY) = (uint8_t)sticky;
}
static void t_scriptclr(uintptr_t npc, int x)
{
	(void)x;
	uintptr_t ai = rq(npc + NPC_AI);
	V.clr_calls++;
	put(ai, AI_SCRIPTTGT, 0);
	*(uint8_t *)(ai + AI_STICKY) = 0;
}

/* CInterfaceMgr mouse move / down / up (re-ui-pointer.md 1.2: move stores the position at im+0x3210) */
static void t_immove(uintptr_t im, int pl, const int *pos)
{
	V.move_calls++;
	V.last_pl = pl, V.last_pos[0] = pos[0], V.last_pos[1] = pos[1];
	put32(im, IM_MOUSE, (uint32_t)pos[0]);
	put32(im, IM_MOUSE + 4, (uint32_t)pos[1]);
}
static void t_imdown(uintptr_t im, int pl, int btn, const int *pos) { (void)im, (void)pl, (void)pos; V.down_calls++; V.last_btn = btn; }
static void t_imup(uintptr_t im, int pl, int btn, const int *pos) { (void)im, (void)pl, (void)pos; V.up_calls++; V.last_btn = btn; }
static f2 t_mstate(uintptr_t gv) { (void)gv; V.mstate++; return (f2){ 12.5f, 7.5f }; }
static uintptr_t g_clipview_obj;
static uintptr_t t_clipview(uintptr_t clip) { (void)clip; V.clipview++; return g_clipview_obj; }
static f2 t_tostage(uintptr_t view, int x, int y) { (void)view; V.tostage++; return (f2){ x * 0.5f, y * 0.5f }; }
static uintptr_t g_clip[2];
static uint64_t t_cliphit(uintptr_t clip, uint64_t xy)
{
	V.cliphit++;
	int x = (int)(int32_t)(uint32_t)xy, y = (int)(int32_t)(uint32_t)(xy >> 32);
	return clip == g_clip[1] && x == 95 && y == 247; /* the second item sits at the pointer (190, 495) / 2 */
}

/* the physics ray (IEnginePhysics slot 64) and the surface of a hit */
static uintptr_t g_hitobj, g_surfrec;
/* P10: the heading scan's world: ground at y = 0 where z <= 1000 (a drop beyond), a wall plane at x = 500; with
 * g_ray_self a ray that starts at the root (40 or 100 cm up) hits Talion's own object there first (P8's surf) */
static int g_ray_world, g_ray_self, g_ray_world_n;
static uintptr_t g_ray_root;
static uint64_t t_raycast(uintptr_t phys, void *q, void *r)
{
	(void)phys;
	V.ray++;
	float from[3], to[3], hit[3], nrm[3] = { 0, 0, -1 }, fr = 0.25f;
	memcpy(from, q, 12);
	memcpy(to, (char *)q + 12, 12);
	if (g_ray_world) {
		float rp[3];
		uintptr_t obj = g_hitobj;
		memcpy(rp, (void *)(g_ray_root + OBJ_POS), 12);
		g_ray_world_n++;
		if (g_ray_self && fabsf(from[0] - rp[0]) < 1 && fabsf(from[2] - rp[2]) < 1 && from[1] > rp[1] + 10) {
			fr = 0, obj = g_ray_root;
		} else if (to[1] < from[1] - 100 && fabsf(to[0] - from[0]) < 1 && fabsf(to[2] - from[2]) < 1) { /* down */
			if (from[2] > 1000 || from[1] < 0 || to[1] > 0)
				return 0;
			fr = from[1] / (from[1] - to[1]);
		} else if ((from[0] - 500) * (to[0] - 500) < 0) {
			fr = (500 - from[0]) / (to[0] - from[0]);
		} else {
			return 0;
		}
		for (int k = 0; k < 3; k++)
			hit[k] = from[k] + (to[k] - from[k]) * fr;
		memcpy(r, hit, 12);
		memcpy((char *)r + 0x18, &fr, 4);
		memcpy((char *)r + 0x20, &obj, 8);
		return 1;
	}
	for (int k = 0; k < 3; k++)
		hit[k] = from[k] + (to[k] - from[k]) * fr;
	memcpy(r, hit, 12);
	memcpy((char *)r + 0xc, nrm, 12);
	memcpy((char *)r + 0x18, &fr, 4);
	memcpy((char *)r + 0x20, &g_hitobj, 8);
	return 1;
}
static uintptr_t t_surfrec(void *r) { V.surfrec++; return rq((uintptr_t)r + 0x20) == g_hitobj ? g_surfrec : 0; }

/* the console's output function (the repository's +8) */
__attribute__((noinline, noclone)) static void t_conout(const uint32_t *colour, int level, const char *text)
{
	(void)colour, (void)level;
	V.conout++;
	snprintf(V.conout_text, sizeof V.conout_text, "%s", text);
}

/* ---------------------------------------------------------------- P7 stand-ins -------------------------------- */

static struct {
	int stim_calls, reg_calls, regp_calls, rm_calls, rm_id;
	uintptr_t stim_ad, stim_tgt;
	int ai_calls, ai_ret;
	uint64_t ai_args[3];
	int pj_updates, pj_removed, pjd_calls;
	uint64_t pjd_args[8];
	int fx_inits, fx_creates, fx_frees, fx_releases;
	char fx_name[128];
	uint32_t fx_flags;
	uintptr_t fx_parent, *fx_out;
	int reattach, itemvis;
	int snd_global, snd_reg, snd_pos, snd_post, ak_calls;
	char snd_name[128];
	uint32_t snd_obj;
	uint64_t ak_args[8];
	int tm_created, tm_deleted, rates_set;
	float rate_set[2];
	int nav_pos, uis_calls;
} U;

static uintptr_t g_stimrec, g_stimrec2, g_phobj;

/* FUN_026c7180: one RegisterStimulus entry, then the persistent one, whose id goes to c+0x168 (noipa: the callees must
 * not let GCC drop the descriptor's stores) */
__attribute__((noipa)) static uint64_t t_regstim(uintptr_t mgr, uintptr_t desc)
{
	(void)mgr, (void)desc;
	U.reg_calls++;
	return 7;
}
__attribute__((noipa)) static uint64_t t_regstim_p(uintptr_t mgr, uintptr_t desc)
{
	(void)mgr, (void)desc;
	U.regp_calls++;
	return 42;
}
__attribute__((noinline, noclone)) static void t_stim(uintptr_t c, uintptr_t ad, uintptr_t tgt)
{
	unsigned char desc[0x90];
	uintptr_t mgr = rq(G.stim_mgr_pp), src = rq(c + C_HOBJ);
	int one = 1;
	memset(desc, 0, sizeof desc);
	memcpy(desc, &g_stimrec, 8);
	memcpy(desc + 0x20, &src, 8);
	memcpy(desc + 0x58, &tgt, 8);
	U.stim_calls++;
	U.stim_ad = ad;
	U.stim_tgt = tgt;
	if (t_regstim(mgr, (uintptr_t)desc) == 99)
		U.stim_calls += 100;
	memcpy(desc, &g_stimrec2, 8);
	memcpy(desc + 0x68, &one, 4);
	uint32_t id = (uint32_t)t_regstim_p(mgr, (uintptr_t)desc);
	memcpy((void *)(c + C_STIMID), &id, 4);
}
__attribute__((noinline, noclone)) static uint64_t c_stim_start(uintptr_t c, uintptr_t a, uintptr_t t) { t_stim(c, a, t); return 1; }
__attribute__((noinline, noclone)) static uint64_t c_stim_key(uintptr_t c, uintptr_t a, uintptr_t t) { t_stim(c, a, t); return 2; }
__attribute__((noinline, noclone)) static uint64_t c_stim_stop(uintptr_t c, uintptr_t a, uintptr_t t) { t_stim(c, a, t); return 3; }
static void t_stim_remove(uintptr_t mgr, int id) { (void)mgr; U.rm_calls++; U.rm_id = id; }
static uintptr_t t_attackdata(uintptr_t rec) { return db_recf(rec, G.sch_attack, "AttackData"); } /* field 1 */

/* the AI goal/actions: one stand-in for the five slots */
__attribute__((noinline, noclone)) static uint64_t o_ai(uint64_t self, uint64_t a, uint64_t b)
{
	U.ai_calls++;
	U.ai_args[0] = self, U.ai_args[1] = a, U.ai_args[2] = b;
	return (uint64_t)U.ai_ret;
}

/* key times and the model's key/sample interface (anim 5 plays on tracker 0xff; its APPLYDAMAGE key at 400 ms) */
static uint32_t g_anim_ms = 100;
__attribute__((noinline, noclone)) static uint64_t t_keytimes(uintptr_t ws, uintptr_t h, uintptr_t a) { (void)ws, (void)h, (void)a; return 0x55; }
__attribute__((noinline, noclone)) static uint64_t c_keytimes(uintptr_t ws, uintptr_t h, uintptr_t a) { return t_keytimes(ws, h, a) + 1; }
__attribute__((noinline, noclone)) static uint64_t t_applydmg(uintptr_t c, uintptr_t a, uintptr_t t) { (void)c, (void)a, (void)t; return 0x66; }
__attribute__((noinline, noclone)) static uint64_t c_applydmg(uintptr_t c, uintptr_t a, uintptr_t t) { return t_applydmg(c, a, t) + 1; }
static uint64_t t_findtracker(uintptr_t h, uint8_t *trk, uint32_t anim)
{
	(void)h;
	if (anim != 5)
		return 1;
	*trk = 0xff;
	return 0;
}
static uint64_t t_getcuranim(uintptr_t m, uintptr_t h, uint32_t trk, uint32_t *anim)
{
	(void)m, (void)h;
	*anim = trk == 0xff || trk == 0 ? 5 : 0xffffffffu;
	return 0;
}
static uint64_t t_getanimtime(uintptr_t m, uintptr_t h, uint32_t trk, uint32_t *ms) { (void)m, (void)h, (void)trk; *ms = g_anim_ms; return 0; }
static int g_rate_live; /* P10: GetAnimRate returns the rate last set (animrate), else the constants 1.0 / 0.5 */
static float g_rate_cur[2] = { 1.0f, 0.5f };
static uint64_t t_getanimrate(uintptr_t m, uintptr_t h, uint32_t trk, float *r)
{
	(void)m, (void)h;
	*r = g_rate_live ? g_rate_cur[trk == 0xff ? 0 : 1] : trk == 0xff ? 1.0f : 0.5f;
	return 0;
}
static uint64_t t_setanimrate(uintptr_t m, uintptr_t h, uint32_t trk, float rate)
{
	(void)m, (void)h;
	U.rates_set++;
	U.rate_set[trk == 0xff ? 0 : 1] = rate;
	g_rate_cur[trk == 0xff ? 0 : 1] = rate;
	return 0;
}
static uint64_t t_keylookup(uintptr_t m, uintptr_t h, uint32_t anim, const char *key, const char *arg, uint32_t *frame,
			    uint32_t *ms, uint32_t from)
{
	(void)m, (void)h, (void)anim, (void)arg, (void)from;
	if (!strcmp(key, "APPLYDAMAGE") || !strcmp(key, "BPW")) {
		*ms = key[0] == 'A' ? 400 : 250;
		*frame = *ms * 30 / 1000;
		return 1;
	}
	return 0;
}
static uint64_t t_samplenode(uintptr_t m, uintptr_t h, uint32_t anim, uint32_t node, uint32_t ms, float *xf, int flag)
{
	(void)m, (void)h, (void)flag;
	if (anim != 5 || node != 0x3000 || ms != 400)
		return 1;
	static const float v[7] = { 10, 150, 30, 0, 0, 0, 1 };
	memcpy(xf, v, sizeof v);
	return 0;
}

/* timers: refcounted like the engine's (SetObjectTimer takes a reference, the handle release drops one) */
static uintptr_t g_simt;
static void tm_ref(uintptr_t t, int d)
{
	if (!t)
		return;
	uint32_t r = r32(t + TM_REF) + (uint32_t)d;
	put32(t, TM_REF, r);
	if (!r)
		U.tm_deleted++;
}
static uintptr_t *t_tm_create(uintptr_t *out, uintptr_t tm)
{
	(void)tm;
	uintptr_t t = (uintptr_t)zalloc(0x100);
	put(t, 0, G.vt_etimer);
	put32(t, TM_REF, 1);
	put32(t, TM_NUM, 1);
	put32(t, TM_DEN, 1);
	putf(t, TM_SCALE, 1.0f);
	*out = t;
	U.tm_created++;
	return out;
}
static int t_tm_attach(uintptr_t tm, uintptr_t *parent, uintptr_t *child) { (void)tm; return *parent && *child ? 0 : 0x3c; }
static int t_tm_detach(uintptr_t tm, uintptr_t *t) { (void)tm; return *t ? 0 : 0x3c; }
static int t_tm_setobj(uintptr_t tm, uintptr_t h, uintptr_t *t)
{
	(void)tm;
	uintptr_t old = rq(h + OBJ_TIMER);
	tm_ref(*t, 1);
	tm_ref(old, -1);
	put(h, OBJ_TIMER, *t);
	return 0;
}
static uintptr_t *t_tm_getobj(uintptr_t *out, uintptr_t tm, uintptr_t h)
{
	(void)tm;
	*out = rq(h + OBJ_TIMER);
	tm_ref(*out, 1);
	return out;
}
static void t_tm_release(uintptr_t *p) { tm_ref(*p, -1); }

/* projectiles: vf_13 removes a done one, else moves it 10 cm along R(q) +Z */
__attribute__((noinline, noclone)) static uint64_t o_pjupdate(uint64_t self)
{
	uintptr_t p = (uintptr_t)self;
	float pos[3], q[4], d[3], fz[3] = { 0, 0, 1 };
	U.pj_updates++;
	if (*(uint8_t *)(p + PJ_DONE)) {
		U.pj_removed++;
		return 1;
	}
	memcpy(pos, (void *)(p + PJ_POS), 12);
	memcpy(q, (void *)(p + PJ_ROT), 16);
	qrot(q, fz, d);
	for (int k = 0; k < 3; k++)
		pos[k] += 10.0f * d[k];
	memcpy((void *)(p + PJ_POS), pos, 12);
	return 0;
}
static uintptr_t mk_proj(const float pos[3], const float q[4], uintptr_t shooter)
{
	uintptr_t p = (uintptr_t)zalloc(0x240);
	put(p, 0, G.vt_proj);
	memcpy((void *)(p + PJ_POS), pos, 12);
	memcpy((void *)(p + PJ_ROT), q, 16);
	put(p, PJ_SHOOTER, shooter);
	put(p, PJ_TARGET, g_phobj);
	putf(p, PJ_SPEED, 2500.0f);
	putf(p, PJ_RANGE, 10000.0f);
	*(uint8_t *)(p + PJ_HOMING) = 1;
	*(uint8_t *)(p + PJ_SPLINE) = 1;
	return p;
}
/* the projectile DealHit: 8 arguments, the last two on the stack (P_PJ_DAMAGE is a PREWRAP) */
__attribute__((noinline, noclone)) static uint64_t t_pjdamage(uintptr_t sh, uintptr_t hit, uintptr_t atk, uintptr_t rng,
							     uintptr_t x, uintptr_t node, int inc, int crit)
{
	U.pjd_calls++;
	uint64_t a[8] = { sh, hit, atk, rng, x, node, (uint64_t)inc, (uint64_t)crit };
	memcpy(U.pjd_args, a, sizeof a);
	return 0x77;
}
__attribute__((noinline, noclone)) static uint64_t c_pjdamage(uintptr_t sh, uintptr_t hit, uintptr_t atk, uintptr_t rng,
							     uintptr_t x, uintptr_t node, int inc, int crit)
{
	return t_pjdamage(sh, hit, atk, rng, x, node, inc, crit) + 1;
}

/* FX: the CS init (one hooked caller), the create (a handle with the back pointer to its slot), free, release */
__attribute__((noinline, noclone)) static void t_fxinit(void *cs, const char *name, uintptr_t mh, uint32_t flags, uintptr_t parent)
{
	(void)mh;
	U.fx_inits++;
	snprintf(U.fx_name, sizeof U.fx_name, "%s", name);
	snprintf((char *)cs, 0x104, "%s", name);
	U.fx_flags = flags;
	U.fx_parent = parent;
}
__attribute__((noinline, noclone)) static uint64_t c_fxinit(void *cs, const char *name, uintptr_t mh, uint32_t flags, uintptr_t parent)
{
	t_fxinit(cs, name, mh, flags, parent);
	return 9;
}
static long t_fxcreate(uintptr_t mgr, uintptr_t *out, void *cs, int start, int b, int c)
{
	(void)mgr, (void)cs, (void)start, (void)b, (void)c;
	U.fx_creates++;
	U.fx_out = out;
	if (out) {
		uintptr_t fx = (uintptr_t)zalloc(0x60);
		put(fx, 0x10, (uintptr_t)out);
		*out = fx;
	}
	return 1;
}
static void t_fxcsfree(void *cs) { (void)cs; U.fx_frees++; }
static void t_fxrelease(uintptr_t *h)
{
	U.fx_releases++;
	if (*h && rq(*h + 0x10) == (uintptr_t)h)
		*h = 0;
}
static void *volatile g_objvt[40], *volatile g_sndvt[24];
static uintptr_t t_objmh(uintptr_t objsys, uintptr_t obj) { (void)objsys; return obj ? rq(obj + 0xe0) : 0; }

/* inventory: the re-attach puts each entry's model object at its node (active or inactive) of the player skeleton */
static uintptr_t g_inv, g_item[2], g_itemobj[2];
static void t_reattach(uintptr_t item)
{
	U.reattach++;
	uintptr_t b = rq(item + IT_ENTRIES), e = rq(item + IT_ENTRIES + 8);
	for (uintptr_t en = b; en < e; en += IT_ESIZE) {
		float xf[8];
		uint32_t node = r32(en + (rb(item + IT_ACTIVE) ? IE_ACTNODE : IE_INACTNODE));
		if (t_nodexf(0, g_charobj, node, xf, 1) == 0)
			memcpy((void *)(rq(en + IE_OBJ) + OBJ_POS), xf, 12);
	}
}
static void t_itemvis(uintptr_t item, int entry) { (void)item, (void)entry; U.itemvis++; }

/* sounds: AK PostEvent (8 arguments, two on the stack) behind the two wrappers; CSoundMgr::vf_13 */
__attribute__((noinline, noclone)) static uint64_t t_akpost(const char *name, uint64_t obj, uint64_t a, uint64_t b, uint64_t c,
							   uint64_t d, uint64_t e, uint64_t f)
{
	uint64_t v[8] = { (uint64_t)(uintptr_t)name, obj, a, b, c, d, e, f };
	memcpy(U.ak_args, v, sizeof v);
	return 0x1000 + (uint64_t)++U.ak_calls;
}
__attribute__((noinline, noclone)) static uint32_t t_sndpost_wrap(uintptr_t wrap, uint32_t obj, const char *name)
{
	(void)wrap;
	uint32_t r = (uint32_t)t_akpost(name, obj, 0, 0, 0, 0, 0, 0);
	U.snd_post++;
	U.snd_obj = obj;
	return r;
}
__attribute__((noinline, noclone)) static uint32_t t_sndpost_cb(uintptr_t wrap, uint32_t obj, const char *name)
{
	(void)wrap;
	uint32_t r = (uint32_t)t_akpost(name, obj, 5, 0x8c8ba0, 0, 0, 0, 0);
	return r + 1;
}
static uint32_t t_sndglobal(uintptr_t mgr, const char *name)
{
	U.snd_global++;
	snprintf(U.snd_name, sizeof U.snd_name, "%s", name);
	return t_sndpost_wrap(G.snd_wrap, r32(mgr + 0xc), name);
}
static uint64_t t_sndreg(uintptr_t wrap, uint32_t obj, const char *name) { (void)wrap, (void)obj, (void)name; U.snd_reg++; return 1; }
static uint64_t t_sndpos(uintptr_t wrap, uint32_t obj, const float *pos, const float *front)
{
	(void)wrap, (void)obj, (void)pos, (void)front;
	U.snd_pos++;
	return 1;
}

/* the 3D surface desc (P_RES3D, a CALLWRAP: six register arguments) and the client's support test */
static uint64_t g_res3d_args[6];
__attribute__((noinline, noclone)) static uint64_t t_res3d(uintptr_t dev, uintptr_t desc, uint64_t w, uint64_t h, uint64_t win,
							  uint64_t z)
{
	uint64_t a[6] = { dev, desc, w, h, win, z };
	memcpy(g_res3d_args, a, sizeof a);
	return 0x31;
}
__attribute__((noinline, noclone)) static uint64_t c_res3d(uintptr_t dev, uintptr_t desc, uint64_t w, uint64_t h, uint64_t win,
							  uint64_t z)
{
	return t_res3d(dev, desc, w, h, win, z) + 1;
}
static int g_t_supported = 1; /* P10: fling's air phase sets it to 0 */
static uint64_t t_supported(uintptr_t player) { (void)player; return (uint64_t)g_t_supported; }

/* nav markers, the UI scene render */
static void t_navpos(uintptr_t m, float *out) { (void)m; U.nav_pos++; out[0] = 1, out[1] = 2, out[2] = 3; }
__attribute__((noinline, noclone)) static uint64_t t_uisrender(uintptr_t r, uintptr_t v) { (void)r, (void)v; U.uis_calls++; return 3; }
__attribute__((noinline, noclone)) static uint64_t c_uisrender(uintptr_t r, uintptr_t v) { return t_uisrender(r, v) + 1; }

/* ---------------------------------------------------------------- P9 stand-ins -------------------------------- */

static struct {
	int gate, rec, roll, fact, goalrel, shield, camxf, spawn, zone, setzone, level, bind, act, remove, refuse;
	float roll_lo, roll_hi;
	uintptr_t spawn_ctx, spawn_tmpl, spawn_fm, fact_owner, fact_npc, setzone_npc, setzone_zone, level_c, remove_h, rec_ret;
	uint32_t level_v;
	int spawn_a5, spawn_a6;
	float spawn_xf[7];
	char spawn_name[64];
	uint64_t shield_args[3];
} W;
static int g_goal_next = 1;
static void *volatile g_npcvt[96], *volatile g_gsvcvt[24];
static uintptr_t g_sp_npc, g_sp_c, g_sp_h, g_tmpl_base, g_tmpl[3], g_fm, g_fmgr, g_wstate, g_ccr_def, g_ccr_pl, g_goal;
static uintptr_t g_cls_obj[2], g_cls_npc[2], g_cls_head, g_wm_pred[3], g_cat_light, g_cat_heavy;

/* Combat_StartAttack's roll path: the gate FUN_0229aef0(node, attack), the record FUN_026ff330(holder), the roll
 * FUN_00c73440(0, 1) with the chance in xmm2 (an asm caller, as the game's), the fact FUN_029ac890(owner, npc) */
__attribute__((noinline, noclone)) static uint64_t t_aibgate(uintptr_t node, uintptr_t attack) { (void)node; W.gate++; return attack != 0; }
__attribute__((noinline, noclone)) static uint64_t c_aibgate(uintptr_t node, uintptr_t attack) { return t_aibgate(node, attack) + 0x100; }
__attribute__((noinline, noclone)) static uintptr_t t_charcombat(uintptr_t hold) { W.rec++; return hold ? g_ccr_def : 0; }
__attribute__((noinline, noclone)) static uintptr_t c_aibrec(uintptr_t hold)
{
	uintptr_t r = t_charcombat(hold);
	W.rec_ret = r;
	return r;
}
__attribute__((noinline, noclone, used)) static float t_roll(float lo, float hi)
{
	W.roll++;
	W.roll_lo = lo;
	W.roll_hi = hi;
	return 0.75f;
}
float c_aibroll(float chance);
__asm__(".text\n.p2align 4\n.type c_aibroll,@function\nc_aibroll:\n  sub $8, %rsp\n  movss %xmm0, %xmm2\n  pxor %xmm0, %xmm0\n"
	"  mov $0x3f800000, %eax\n  movd %eax, %xmm1\n  call t_roll\n  add $8, %rsp\n  ret\n.size c_aibroll, .-c_aibroll\n");
__attribute__((noinline, noclone)) static void t_aifact(uintptr_t owner, uintptr_t npc)
{
	W.fact++;
	W.fact_owner = owner;
	W.fact_npc = npc;
	uintptr_t b = rq(npc + NPC_AI);
	if (b)
		*(uint8_t *)(b + BRAIN_REPLAN) = 1;
}
__attribute__((noinline, noclone)) static int c_aibnotify(uintptr_t owner, uintptr_t npc)
{
	t_aifact(owner, npc);
	return W.fact;
}
/* CAIGoalCounterMelee::vf_03: the relevance into goal+0x10 */
__attribute__((noinline, noclone)) static uint64_t o_goalrel(uint64_t goal)
{
	W.goalrel++;
	*(uint8_t *)(goal + 0x10) = (uint8_t)g_goal_next;
	return 0x91;
}
/* the shield test FUN_028adf30(item, attack, x) and its caller (a PREWRAP: arguments and result pass through) */
__attribute__((noinline, noclone)) static uint64_t t_shield(uintptr_t item, uintptr_t atk, uintptr_t x)
{
	W.shield++;
	W.shield_args[0] = item, W.shield_args[1] = atk, W.shield_args[2] = x;
	return 0x92;
}
__attribute__((noinline, noclone)) static uint64_t c_shield(uintptr_t item, uintptr_t atk, uintptr_t x) { return t_shield(item, atk, x) + 1; }
/* FUN_00a5b8b0(cam, &xf): the camera object's transform (pos 100 150 200, heading +Z) */
__attribute__((noinline, noclone)) static void t_camxf(uintptr_t cam, float *xf)
{
	static const float cx[7] = { 100, 150, 200, 0, 0, 0, 1 };
	(void)cam;
	W.camxf++;
	memcpy(xf, cx, sizeof cx);
}
__attribute__((noinline, noclone)) static int c_camxf(uintptr_t cam, float *xf)
{
	t_camxf(cam, xf);
	return W.camxf;
}

/* the spawn and its post steps: FUN_029a4830 places the one prepared NPC at xf (unless told to refuse) */
static uintptr_t t_spawn(uintptr_t ctx, const char *name, const float *xf, uintptr_t tmpl, int a5, int a6, uintptr_t a7,
			 uintptr_t fm, uintptr_t a9)
{
	(void)a7, (void)a9;
	W.spawn++;
	W.spawn_ctx = ctx, W.spawn_tmpl = tmpl, W.spawn_fm = fm, W.spawn_a5 = a5, W.spawn_a6 = a6;
	snprintf(W.spawn_name, sizeof W.spawn_name, "%s", name);
	memcpy(W.spawn_xf, xf, sizeof W.spawn_xf);
	if (W.refuse)
		return 0;
	memcpy((void *)(g_sp_h + OBJ_POS), xf, 12);
	memcpy((void *)(g_sp_h + OBJ_ROT), xf + 3, 16);
	*(uint8_t *)(g_sp_h + HOBJ_FLAGS) = 0;
	return g_sp_npc;
}
static uintptr_t t_tmplbyname(const char *name)
{
	char nm[96];
	for (int i = 0; i < 3; i++)
		if (!strcmp(rec_name(g_tmpl[i], nm, sizeof nm), name))
			return g_tmpl[i];
	return 0;
}
static uintptr_t t_zoneat(const float *pos, uintptr_t a, uintptr_t b) { (void)pos, (void)a, (void)b; W.zone++; return 0x7a00; }
static void t_npcsetzone(uintptr_t npc, uintptr_t zone, int x) { (void)x; W.setzone++; W.setzone_npc = npc; W.setzone_zone = zone; }
static void t_setlevel(uintptr_t c, uint32_t lvl) { W.level++; W.level_c = c; W.level_v = lvl; }
static void t_fmbind(uintptr_t npc, uintptr_t fm, int x) { (void)npc, (void)fm, (void)x; W.bind++; }
static void t_fmact(uintptr_t fm, int on) { (void)fm, (void)on; W.act++; }
static uintptr_t t_factiontgt(uintptr_t fmgr) { return fmgr == g_fmgr ? g_fm : 0; }
static uintptr_t t_fmtemplate(uintptr_t fm) { return fm == g_fm ? g_tmpl[2] : 0; }
static uintptr_t t_todname(uint32_t tod) { return (uintptr_t)(tod == 3 ? "Day" : "?"); }
static uint32_t t_wmcmdid(uintptr_t pred)
{
	return pred == g_wm_pred[0] ? 118 : pred == g_wm_pred[1] ? 243 : pred == g_wm_pred[2] ? 125 : 0;
}
static uintptr_t t_fmgrget(uintptr_t self) { (void)self; return g_fmgr; }
static uintptr_t t_wstateget(uintptr_t self) { (void)self; return g_wstate; }
static uint64_t t_objremove(uintptr_t objsys, uintptr_t h)
{
	(void)objsys;
	W.remove++;
	W.remove_h = h;
	*(uint8_t *)(h + HOBJ_FLAGS) |= 8;
	return 0;
}

/* P10: the graph's variables (FUN_0208dd90 by name, FUN_0208de50 the value) and the velocity mode (FUN_020c6ff0) */
static struct { int gvar, gget, velmode, velmode_last; } X;
static float g_t_gv[4] = { 0.7f, 321.0f, 300.0f, 0.25f };
static uintptr_t g_t_var[4];
static uintptr_t t_graphvar(uintptr_t mgr, const char *name)
{
	(void)mgr;
	X.gvar++;
	for (int i = 0; i < 4; i++)
		if (!strcmp(name, g_gv_names[i]))
			return (uintptr_t)&g_t_var[i];
	return 0;
}
static float t_graphget(uintptr_t mgr, uintptr_t var)
{
	(void)mgr;
	X.gget++;
	for (int i = 0; i < 4; i++)
		if (var == (uintptr_t)&g_t_var[i])
			return g_t_gv[i];
	return 0;
}
static void t_velmode(uintptr_t pm, int rootmotion)
{
	X.velmode++;
	X.velmode_last = rootmotion;
	if (*(uint8_t *)(pm + PM_VELMODE) && !rootmotion) {
		memset((void *)(pm + 0x104), 0, 12);
		*(uint8_t *)(pm + PM_VELMODE) = 0;
		return;
	}
	*(uint8_t *)(pm + PM_VELMODE) = (uint8_t)rootmotion;
}

static uintptr_t mk_record(const char *name, uint32_t hash)
{
	uintptr_t r = (uintptr_t)zalloc(DBREC_SIZE);
	*(uint8_t *)(r + 1) = 2;
	put(r, 0x20, (uint64_t)(uintptr_t)name);
	put32(r, 0x28, hash);
	return r;
}

static void bind_cmd(int cmd, int input, float scale)
{
	static int next_val;
	uintptr_t rec = g_recs + (uintptr_t)cmd * 0x40;
	put(g_entries + (uintptr_t)cmd * PIE_SIZE, 0, rec);
	put(rec, REC_CTX, g_ctxobj);
	put(rec, REC_USER, g_pi_obj);
	for (int k = 0; k < 4; k++) {
		put32(rec, REC_INPUTS + 4u * (unsigned)k, 0xffffffffu);
		put32(rec, REC_VALIDX + 4u * (unsigned)k, 0xffffffffu);
	}
	put32(rec, REC_INPUTS, (uint32_t)input);
	put32(rec, REC_VALIDX, (uint32_t)next_val);
	putf(g_valrecs, (size_t)next_val * VALREC_SIZE, scale);
	next_val++;
}

static void build_world(void)
{
	g_pid = getpid(); /* the safe reads (rd) need it before probe_start sets it */
	static uint8_t hashtab[256];  /* as the game's table: letters 1..26 (case-folded), digits 27.., '_' 38 */
	for (int ch = 0; ch < 256; ch++) {
		if (ch >= 'A' && ch <= 'Z')
			hashtab[ch] = (uint8_t)(ch - 'A' + 1);
		else if (ch >= 'a' && ch <= 'z')
			hashtab[ch] = (uint8_t)(ch - 'a' + 1);
		else if (ch >= '0' && ch <= '9')
			hashtab[ch] = (uint8_t)(ch - '0' + 27);
		else if (ch == ' ')
			hashtab[ch] = 37;
		else if (ch == '_')
			hashtab[ch] = 38;
	}
	G.hash_table = (uintptr_t)hashtab;

	g_shell = (uintptr_t)zalloc(0x82000);
	g_im = (uintptr_t)zalloc(0x4000);
	g_msm = (uintptr_t)zalloc(0x40);
	g_entry = (uintptr_t)zalloc(0x400);
	g_profile = (uintptr_t)zalloc(0x300);
	g_player = (uintptr_t)zalloc(0x1000);
	g_cam = (uintptr_t)zalloc(0x600);
	g_body = (uintptr_t)zalloc(0x600);
	g_pc = (uintptr_t)zalloc(0xb20);
	g_device = (uintptr_t)zalloc(0x300);
	g_charobj = (uintptr_t)zalloc(0x80);
	g_model = (uintptr_t)zalloc(0x200); /* P8: the attach node at +0x90, the tracker at +0x178 */
	g_entries = (uintptr_t)zalloc((size_t)NCMD * PIE_SIZE);
	g_recs = (uintptr_t)zalloc((size_t)NCMD * 0x40);
	g_valrecs = (uintptr_t)zalloc((size_t)NCMD * VALREC_SIZE);
	g_ctxobj = (uintptr_t)zalloc(0x40);
	uintptr_t photo = (uintptr_t)zalloc(0x40), mgr = (uintptr_t)zalloc(0x4000), camobj = (uintptr_t)zalloc(0x40);
	uintptr_t entry_vec = (uintptr_t)zalloc(0x20), cine = (uintptr_t)zalloc(0x40);
	uintptr_t skel = (uintptr_t)zalloc(0x80), modeldata = (uintptr_t)zalloc(0x80), hashes = (uintptr_t)zalloc(0x40);
	uintptr_t snodes = (uintptr_t)zalloc(T_NN * 0x40), snames = (uintptr_t)zalloc(T_NN * 8), shashes = (uintptr_t)zalloc(T_NN * 4);
	uintptr_t sockets = (uintptr_t)zalloc(0x30);

	/* shell */
	put32(g_shell, SH_LOCALS, 0);
	put(entry_vec, 0, g_entry);
	put(g_shell, SH_LOCALS + 8, entry_vec);
	put(g_shell, SH_LOCALS + 0x10, entry_vec + 8);
	put(g_shell, SH_IM, g_im);
	put(g_shell, SH_CINE, cine);
	put32(g_shell, SH_UIRECT, 0);
	put32(g_shell, SH_UIRECT + 8, 1920);
	put32(g_shell, SH_UIRECT + 12, 1080);
	put32(g_shell, SH_3DRECT + 8, 3664);
	put32(g_shell, SH_3DRECT + 12, 1920);
	put(g_im, 0, g_msm);
	put(g_im, 0x10, g_msm);
	put32(g_msm, 8, 3);
	put32(g_msm, 0xc, 0xffffffffu);
	put32(g_device, 0x90, 1920);
	put32(g_device, 0x94, 1080);
	put32(g_device, 0xd8, 3664);
	put32(g_device, 0xdc, 1920);
	put(g_device, 0x1b8, 0xaaaa0000);
	put(g_device, 0x1c0, 0xbbbb0000);

	/* local player entry with an embedded PlayerInput */
	put(g_entry, EN_PROFILE, g_profile);
	g_pi_obj = g_entry + EN_PI;
	put(g_pi_obj, PI_ENTRIES, g_entries);
	put(g_pi_obj, PI_ENTRIES + 8, g_entries + (uintptr_t)NCMD * PIE_SIZE);
	*(uint8_t *)(g_pi_obj + PI_ENABLED) = 1;
	*(uint8_t *)(g_pi_obj + PI_INDEX) = 0;
	for (int i = 0; i < 6; i++)
		put(g_pi_obj, PI_ACTIVESET + 8u * (unsigned)i, ~0ull); /* every command in the active set */
	put(g_ctxobj, CTX_VALRECS, g_valrecs);
	bind_cmd(3, 2017, 1.0f);   /* ForwardAxis  <- LY */
	bind_cmd(6, 2016, 1.0f);   /* StrafeAxis   <- LX */
	bind_cmd(12, 2019, 1.0f);  /* PitchAxis    <- RY */
	bind_cmd(13, 2018, 1.0f);  /* YawAxis      <- RX */
	bind_cmd(22, 2019, 0.5f);  /* PitchAccel   <- RY */
	bind_cmd(23, 2018, 0.5f);  /* YawAccel     <- RX */
	bind_cmd(16, 2010, 1.0f);  /* Jump         <- A */
	bind_cmd(118, 2012, 1.0f); /* CombatPrimary01 <- X */
	bind_cmd(121, 2013, 1.0f); /* CombatPrimary04 <- Y */

	/* player and components */
	put(g_player, P_CAM, g_cam);
	put(g_player, P_BODY, g_body);
	put(g_player, P_COMBAT, g_pc);
	put(g_player, P_PHOTO, photo);
	put32(photo, 0x30, 3);
	*(uint8_t *)(g_player + P_LIFE) = 2;
	put(g_body, BODY_OBJ, g_charobj);
	put(g_body, BODY_MGR, mgr);
	put(mgr, MGR_CHAR, g_charobj);
	g_tagrec[0] = mk_record("InCombat", 1);
	g_tagrec[1] = mk_record("Stl", 2);
	put(g_body + BODY_TAGS, 8, g_tagrec[0]); /* set 0 = tags, count at +0xa0 */
	put32(g_body + BODY_TAGS, 8 + 0xa0, 1);
	put(g_cam, CAM_OBJ, camobj);
	putf(g_cam, CAM_POS, 100.0f);
	putf(g_cam, CAM_POS + 4, 150.0f);
	putf(g_cam, CAM_POS + 8, 200.0f);
	putf(g_cam, CAM_ROT + 12, 1.0f);   /* identity: heading 0 (+Z) */
	putf(g_cam, CAM_EXTRA + 12, 1.0f);
	putf(g_charobj, OBJ_POS, 100.0f);
	putf(g_charobj, OBJ_POS + 4, 0.0f);
	putf(g_charobj, OBJ_POS + 8, 200.0f);
	put(g_model, 0x28, modeldata);
	put(modeldata, 0x10, skel);
	put(skel, 0, snodes);
	put32(skel, 8, T_NN);
	put(skel, 0x10, sockets);
	put32(skel, 0x18, 1);
	put(skel, 0x20, hashes);
	put(skel, 0x28, hashes + 4 * 8);
	put(skel, 0x38, snames);
	put(skel, 0x40, shashes);
	for (int i = 0; i < T_NN; i++) { /* node 4 L_Weapon and the arm (5-9) carry their real name hashes */
		if (i < 5) {
			put32(hashes, 8u * (unsigned)i, 0x1000u + (unsigned)i);
			put32(hashes, 8u * (unsigned)i + 4, (unsigned)i);
		}
		*(uint8_t *)(snodes + (uintptr_t)i * 0x40) = i < 5 ? (uint8_t)(i ? i - 1 : 0xff) : T_PARENT[i]; /* parent byte */
		*(uint8_t *)(snodes + (uintptr_t)i * 0x40 + 1) = (uint8_t)i;
		put(snames, 8u * (unsigned)i, (uint64_t)(uintptr_t)T_NODES[i]);
		put32(shashes, 4u * (unsigned)i, t_hash_of(i));
	}
	/* the socket table: SocketHead, parent = node 0 ("Head"), hash from the probe's own hash of the name */
	put32(sockets, 0, name_hash("SocketHead"));
	put32(sockets, 4, 0);
	putf(sockets, 8 + 4, 2.0f); /* a 2 cm offset up from the head node */

	/* the player's server Combat and three orcs in the first targeting band */
	uintptr_t charrec = mk_record("Uruk_Grunt", 3), pobj = (uintptr_t)zalloc(0x100);
	g_scombat = (uintptr_t)zalloc(0x600);
	put(g_scombat, 0, G.vt_combat);
	put(g_scombat, C_PLAYER, 0x1111);
	put(g_scombat, C_HOBJ, pobj);
	putf(pobj, OBJ_POS, 100.0f);
	putf(pobj, OBJ_POS + 8, 200.0f);
	put(g_pc, PC_SERVER, g_scombat);
	uintptr_t band = (uintptr_t)zalloc(3 * PCB_SIZE);
	for (int i = 0; i < 3; i++) {
		g_orc[i] = (uintptr_t)zalloc(0x600);
		g_orcobj[i] = (uintptr_t)zalloc(0x100);
		uintptr_t owner = (uintptr_t)zalloc(0x1000);
		put(owner, CH_COMBAT, g_orc[i]);
		put(g_orc[i], 0, G.vt_combat);
		put(g_orc[i], C_OWNER, owner);
		put(owner, 0, G.vt_npc);
		put(g_orc[i], C_AI, 0x2222);
		put(g_orc[i], C_HOBJ, g_orcobj[i]);
		put(g_orc[i], C_CHARREC, charrec);
		putf(g_orcobj[i], OBJ_POS, 100.0f + 200.0f * (float)(i + 1)); /* 2 m, 4 m, 6 m away */
		putf(g_orcobj[i], OBJ_POS + 8, 200.0f);
		put(band, (size_t)i * PCB_SIZE + 0x30, g_orc[i]);
	}
	*(uint8_t *)(rq(g_orc[0] + C_OWNER) + CH_FLAGS) = 1; /* orc 0 is dead: must be skipped */
	put(g_pc, PC_BANDS, band);
	put(g_pc, PC_BANDS + 8, band + 3 * PCB_SIZE);

	/* attack category with three records */
	g_atk_cat = (uintptr_t)zalloc(0x80);
	uintptr_t base = (uintptr_t)zalloc(4 * DBREC_SIZE);
	static const char *const nm[4] = { "Orc_Club_Heavy", "PlayerLightSword1", "Player_Execution_Sync", "PC_HC_Low_L1_D0_F_1" };
	for (int i = 0; i < 4; i++) {
		uintptr_t r = base + (uintptr_t)i * DBREC_SIZE;
		*(uint8_t *)(r + 1) = 2;
		put(r, 0x20, (uint64_t)(uintptr_t)nm[i]);
		put32(r, 0x28, (uint32_t)(100 + i));
	}
	put32(g_atk_cat, CAT_COUNT, 4);
	put(g_atk_cat, CAT_RECS, base);

	/* the global holders G reads */
	g_shell_slot = (uintptr_t)zalloc(8);
	g_model_slot = (uintptr_t)zalloc(8);
	g_cat_slot = (uintptr_t)zalloc(8);
	g_pause_slot = (uintptr_t)zalloc(8);
	g_pausestate = (uintptr_t)zalloc(0x40);
	g_near_cell = (uintptr_t)zalloc(8);
	g_guard_cell = (uintptr_t)zalloc(8);
	put(g_shell_slot, 0, g_shell);
	put(g_model_slot, 0, 0xcafe0000);
	put(g_cat_slot, 0, g_atk_cat);
	put(g_pause_slot, 0, g_pausestate);
	put(g_pausestate, 8, g_pausestate + 0x20);
	putf(g_near_cell, 0, 27.5f);
	put(g_guard_cell, 0, 0x919970); /* = the original: the guard is NOT in place */

	G.shell_pp = g_shell_slot;
	G.model_pp = g_model_slot;
	G.atkcat_pp = g_cat_slot;
	G.pause_pp = g_pause_slot;
	G.allow_pad = (uintptr_t)&g_allow[0];
	G.allow_kbm = (uintptr_t)&g_allow[1];
	G.near_ovr = g_near_cell;
	G.device = g_device;
	G.guard_slot = g_guard_cell;
	G.guard_orig = 0x919970;
	G.ra_vis_player = 0; /* any return address counts as the player view */
	g_allow[0] = g_allow[1] = 1;
	void *fns[FN_N] = { (void *)t_cmd_on, (void *)t_cmd_off, (void *)t_cmd_active, (void *)t_input_mode,
			    (void *)t_dealhit, (void *)t_atk_byname, (void *)t_modelof, (void *)t_getnode, (void *)t_nodexf,
			    (void *)t_hashindex, (void *)t_getsocket, (void *)t_numpieces, (void *)t_getpiece,
			    (void *)t_piecename, (void *)t_hidepiece, (void *)t_videocover, (void *)t_setcam,
			    (void *)t_camreset, (void *)t_localplayer,
			    /* P7 */
			    (void *)t_stim, (void *)t_attackdata, (void *)t_stim_remove, (void *)t_tm_create, (void *)t_tm_attach,
			    (void *)t_tm_detach, (void *)t_tm_setobj, (void *)t_tm_getobj, (void *)t_tm_release, (void *)t_getcuranim,
			    (void *)t_getanimtime, (void *)t_getanimrate, (void *)t_setanimrate, (void *)t_keylookup,
			    (void *)t_samplenode, (void *)t_findtracker, (void *)t_fxinit, (void *)t_fxcreate, (void *)t_fxcsfree,
			    (void *)t_fxrelease, (void *)t_reattach, (void *)t_itemvis, (void *)t_sndreg, (void *)t_sndpos,
			    (void *)t_sndpost_wrap, (void *)t_navpos, (void *)t_supported,
			    /* P8 */
			    (void *)t_scripttgt, (void *)t_scriptclr, (void *)t_immove, (void *)t_imdown, (void *)t_imup,
			    (void *)t_mstate, (void *)t_tostage, (void *)t_clipview, (void *)t_cliphit, (void *)t_surfrec,
			    /* P9 */
			    (void *)t_spawn, (void *)t_tmplbyname, (void *)t_zoneat, (void *)t_setlevel, (void *)t_fmbind,
			    (void *)t_fmact, (void *)t_factiontgt, (void *)t_fmtemplate, (void *)t_charcombat, (void *)t_aifact,
			    (void *)t_todname, (void *)t_wmcmdid,
			    /* P10 */
			    (void *)t_graphvar, (void *)t_graphget, (void *)t_velmode };
	_Static_assert(FN_VELMODE == FN_N - 1, "the stand-in table covers every FN_");
	for (int i = 0; i < FN_N; i++) {
		G.fn[i] = (uintptr_t)fns[i];
		g_fn_ok[i] = 1;
	}

	/* the console */
	g_eng = (uintptr_t)zalloc(0x40);
	put(g_eng, 0, (uintptr_t)g_engvt);
	g_eng_cell = (uintptr_t)zalloc(8);
	put(g_eng_cell, 0, g_eng);
	uintptr_t srv = (uintptr_t)zalloc(0xd00);
	put(srv, 0xc58, 1);
	g_srv_cell = (uintptr_t)zalloc(8);
	put(g_srv_cell, 0, srv);
	g_repo = (uintptr_t)zalloc(0x60);
	put(g_repo, 0, (uintptr_t)g_repovt);
	G.eng_pp = g_eng_cell;
	G.srv_pp = g_srv_cell;
	G.repo = g_repo;
	G.vt_repo = (uintptr_t)g_repovt;
	G.vt_userprog = 0x5c4000;
	static const char *const pn[3] = { "Combat.Debug.Attack", "Teleport", "Faction.Dump" };
	uintptr_t *po[3] = { &g_var, &g_tele, &g_fdump };
	for (int i = 0; i < 3; i++) {
		*po[i] = (uintptr_t)zalloc(0x60);
		put(*po[i], 0, i ? G.vt_userprog : 0x5c3000);
		put(*po[i], 8, (uintptr_t)pn[i]);
		put(*po[i], 0x38, i ? 0xa36ae0u + (unsigned)i : 0);
	}
	for (int i = 0; i < 96; i++)
		g_engvt[i] = (void *)(uintptr_t)(0x8b0000 + 16 * i); /* never called: only the context bytes */
	mk_ro_slot(RO_RUNSTRING, "G22_RUNSTRING", g_engvt, 32, 2, 5, (void *)t_runstring);
	mk_ro_slot(RO_SERVERGATE, "G22_SERVERGATE", g_engvt, 79, 1, 3, (void *)t_srvgate);
	mk_ro_entry(RO_EXEC, "G22_EXEC", (void *)t_exec);
	mk_ro_slot(RO_REPO_FIND, "G22_REPO_FIND", g_repovt, 8, 0, 1, (void *)t_find);

	/* PlayerMovement at Player+0x2c8, the extra move at rest as the constructor leaves it */
	G.vt_pm = 0x5c5000;
	g_pm = (uintptr_t)zalloc(0x460);
	put(g_pm, 0, G.vt_pm);
	put(g_pm, PM_OWNER, g_player);
	putf(g_pm, PM_XROT + 12, 1.0f);
	put(g_player, P_PM, g_pm);
	putf(g_charobj, OBJ_ROT + 12, 1.0f);
}

/* ---------------------------------------------------------------- P7 world --------------------------------------- */

/* a database record's attributes (FUN_0280a0b0's layout): entries sorted by hash; direct = flags bit 0 (keys at +0,
 * the data in the same entry), else keys at +4 (+0 left 0) with +9 = the index of the entry holding the data: here
 * the reverse order, so a lookup that ignores the redirection reads another field */
struct dbf { uint32_t hash; int type, count; uint64_t v[4]; };
static void db_fill(uintptr_t rec, int direct, const struct dbf *f, int n)
{
	unsigned char *ents = zalloc((size_t)n * 16);
	uintptr_t vals = (uintptr_t)zalloc(64 * 8);
	int vi = 0;
	for (int i = 0; i < n; i++) {
		unsigned char *k = ents + i * 16, *e = direct ? k : ents + (n - 1 - i) * 16;
		uint16_t cnt = (uint16_t)f[i].count, slot;
		if (direct) {
			memcpy(k, &f[i].hash, 4);
		} else {
			memcpy(k + 4, &f[i].hash, 4);
			k[9] = (unsigned char)(n - 1 - i);
		}
		e[0xa] = (unsigned char)f[i].type;
		if (f[i].type == 1) { /* a bool: slot vi, bit 3 */
			slot = (uint16_t)(vi * 32 + 3);
			put(vals, (size_t)vi * 8, f[i].v[0] ? 1u << 3 : 0);
			vi++;
		} else {
			slot = (uint16_t)vi;
			for (int j = 0; j < f[i].count; j++)
				put(vals, (size_t)(vi++) * 8, f[i].v[j]);
		}
		memcpy(e + 0xc, &cnt, 2);
		memcpy(e + 0xe, &slot, 2);
	}
	*(uint8_t *)rec = (uint8_t)n;
	*(uint8_t *)(rec + 1) |= direct ? 1 : 0;
	put(rec, 8, (uintptr_t)ents);
	put(rec, 0x10, vals);
}

/* a schema holder: entries of 0x30 bytes (+0 name, +8 hash = base + index, +0x18 state 2), a null name at the end */
static uintptr_t mk_schema(const char *const *names, uint32_t base)
{
	int n = 0;
	while (names[n])
		n++;
	uintptr_t a = (uintptr_t)zalloc((size_t)(n + 1) * 0x30), cell = (uintptr_t)zalloc(8);
	for (int i = 0; i < n; i++) {
		put(a, (size_t)i * 0x30, (uintptr_t)names[i]);
		put32(a, (size_t)i * 0x30 + 8, base + (uint32_t)i);
		put32(a, (size_t)i * 0x30 + 0x18, 2);
	}
	put(cell, 0, a);
	return cell;
}

static uintptr_t g_atk[4], g_ad1, g_sync1, g_navm[2], g_prompt_entry, g_uiview;
static uintptr_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

static void build_p7(void)
{
	G.vt_proj = 0x5c6000;
	G.vt_etimer = 0x5c7000;
	G.vt_uiscene = 0x5c8000;
	G.vt_diamond_pad = 0x5c9000;
	G.vt_diamond_kb = 0x5c9100;
	G.vt_timer = 0x5ca000;

	/* the database: schemas (hash = base + index) and the records the words read */
	static const char *const s_atk[] = { "AnimationData", "AttackData", "AttackPostData", "Predicate", "MultiHitRecords",
					     "Category", "AIData", NULL };
	static const char *const s_anim[] = { "Name", "FacialAnimTags", "BehaviorNode", "SyncToAnim", "WeaponAnimationName",
					      "Loop", "BlendInTime", "PlaybackRate", "Sync", NULL };
	static const char *const s_sync[] = { "SourceAttack", "TargetAttack", "AttachSocket", "AlignAttachSockets",
					      "KillTargetDamage", NULL };
	static const char *const s_ad[] = { "AlignToTargetType", "CanBeHitByProjectiles", "IsBlock", "RegisterStimulus",
					    "PersistentStimulus", NULL };
	static const char *const s_post[] = { "CanNextAttackCrit", "AOEOnHit", NULL };
	static const char *const s_ai[] = { "BlockLoopLength", "DodgeChance", NULL };
	static const char *const s_nav[] = { "WorldOffset", "IconMovieClip", "IconMovieClipPC", "IconTexture", "ActionID",
					     "RenderOffScreen", "DisplayRange", NULL };
	G.sch_attack = mk_schema(s_atk, 0xa100);
	G.sch_anim = mk_schema(s_anim, 0xa200);
	G.sch_sync = mk_schema(s_sync, 0xa300);
	G.sch_attackdata = mk_schema(s_ad, 0xa400);
	G.sch_post = mk_schema(s_post, 0xa500);
	G.sch_aidata = mk_schema(s_ai, 0xa600);
	G.sch_nav = mk_schema(s_nav, 0xa700);
	uintptr_t base = rq(g_atk_cat + CAT_RECS);
	for (int i = 0; i < 4; i++)
		g_atk[i] = base + (uintptr_t)i * DBREC_SIZE;
	g_stimrec = mk_record("AI/StimulusType/Visual_Attack", 11);
	g_stimrec2 = mk_record("AI/StimulusType/Audio_Persistent", 12);
	uintptr_t stim3 = mk_record("AI/StimulusType/Tactile", 13), victim = mk_record("AI_Bip_PC_Sneak_IK_1", 14);
	uintptr_t aoe = mk_record("Atk_C_BrutalK_3xMorale_Rune", 15), sock = (uintptr_t)"SocketHead";
	uintptr_t anim1 = mk_record("Anim_PlayerLightSword1", 16), post1 = mk_record("Post_Light", 17);
	uintptr_t ai1 = mk_record("AIData_Light", 18), ad2 = mk_record("AD_PC_HC", 19);
	g_ad1 = mk_record("AD_Light", 20);
	g_sync1 = mk_record("Sync_Light", 21);
	const struct dbf a1[] = { { 0xa100, 0xa, 1, { anim1 } }, { 0xa101, 0xa, 1, { g_ad1 } }, { 0xa102, 0xa, 1, { post1 } },
				  { 0xa106, 0xa, 1, { ai1 } } };
	db_fill(g_atk[1], 1, a1, 4);
	const struct dbf an[] = { { 0xa208, 0xa, 1, { g_sync1 } } };
	db_fill(anim1, 1, an, 1);
	const struct dbf sy[] = { { 0xa300, 0xa, 1, { g_atk[1] } }, { 0xa301, 0xa, 1, { victim } }, { 0xa302, 4, 1, { sock } },
				  { 0xa303, 1, 1, { 1 } }, { 0xa304, 2, 1, { f2u(100.0f) } } };
	db_fill(g_sync1, 1, sy, 5);
	const struct dbf ad[] = { { 0xa401, 1, 1, { 0 } }, { 0xa402, 1, 1, { 0 } }, { 0xa403, 0xa, 2, { g_stimrec, stim3 } },
				  { 0xa404, 0xa, 1, { g_stimrec2 } } };
	db_fill(g_ad1, 1, ad, 4);
	const struct dbf po[] = { { 0xa501, 0xa, 1, { aoe } } };
	db_fill(post1, 1, po, 1);
	const struct dbf ai[] = { { 0xa601, 2, 1, { f2u(0.25f) } } };
	db_fill(ai1, 1, ai, 1);
	const struct dbf a3[] = { { 0xa101, 0xa, 1, { ad2 } }, { 0xa106, 0xa, 1, { ai1 } } }; /* the indirect lookup */
	db_fill(g_atk[3], 0, a3, 2);
	const struct dbf d2[] = { { 0xa403, 0xa, 1, { stim3 } } };
	db_fill(ad2, 0, d2, 1);

	/* the stimulus manager, the player's CCharacter and its inventory: slot 0 the sword (active), 1 the dagger */
	uintptr_t mgr = (uintptr_t)zalloc(0x40), cell = (uintptr_t)zalloc(8), powner = (uintptr_t)zalloc(0x1000);
	put(cell, 0, mgr);
	G.stim_mgr_pp = cell;
	G.stim_off = (uintptr_t)zalloc(8);
	g_phobj = rq(g_scombat + C_HOBJ);
	put(powner, 0, G.vt_playerobj);
	put(g_scombat, C_OWNER, powner);
	g_inv = (uintptr_t)zalloc(0x300);
	put(powner, CH_INV, g_inv);
	static const char *const iname[2] = { "Urfael_Sword", "Acharn_Dagger" };
	for (int k = 0; k < 2; k++) {
		uintptr_t it = (uintptr_t)zalloc(0x100), en = (uintptr_t)zalloc(IT_ESIZE);
		g_item[k] = it;
		g_itemobj[k] = (uintptr_t)zalloc(0x100);
		put(g_itemobj[k], 0xe0, 0x7770u + (unsigned)k); /* its model handle (objsys vtbl+0x118) */
		put(it, IT_ENTRIES, en);
		put(it, IT_ENTRIES + 8, en + IT_ESIZE);
		put(en, IE_OBJ, g_itemobj[k]);
		put32(en, IE_ACTNODE, 0x1003);   /* L_Hand */
		put32(en, IE_INACTNODE, 0x1001); /* Neck (a back sheath) */
		put(it, IT_REC, mk_record(iname[k], 30u + (unsigned)k));
		*(uint8_t *)(it + IT_ACTIVE) = (uint8_t)(k == 0);
		*(uint8_t *)(it + IT_SHOWN) = (uint8_t)(k == 0);
		*(uint8_t *)(it + IT_VISEN) = 1;
		put(g_inv, INV_SLOT0 + (size_t)k * INV_SLOTSZ, it);
	}
	put32(g_inv, INV_ACTIVE, 0);
	put(g_shell, SH_FXMGR, (uintptr_t)zalloc(0x80));
	G.fx_enabled = (uintptr_t)zalloc(8);
	*(uint8_t *)G.fx_enabled = 1;
	uintptr_t objsys = (uintptr_t)zalloc(0x20), oc = (uintptr_t)zalloc(8);
	put(objsys, 0, (uintptr_t)g_objvt);
	put(oc, 0, objsys);
	G.objsys_pp = oc;
	G.vt_objsys = (uintptr_t)g_objvt;
	for (int i = 0; i < 40; i++)
		g_objvt[i] = (void *)(uintptr_t)(0x8c0000 + 16 * i);
	mk_ro_slot(RO_OBJSYS_MH, "OBJSYS_MODELHANDLE", g_objvt, 35, 0, 1, (void *)t_objmh);
	uintptr_t snd = (uintptr_t)zalloc(0x20), sc = (uintptr_t)zalloc(8);
	put(snd, 0, (uintptr_t)g_sndvt);
	*(uint8_t *)(snd + 8) = 1;
	put32(snd, 0xc, 3);
	put(sc, 0, snd);
	G.snd_pp = sc;
	G.vt_snd = (uintptr_t)g_sndvt;
	G.snd_wrap = (uintptr_t)zalloc(0x20);
	for (int i = 0; i < 24; i++)
		g_sndvt[i] = (void *)(uintptr_t)(0x8e0000 + 16 * i);
	mk_ro_slot(RO_SND_GLOBAL, "SND_POSTGLOBAL", g_sndvt, 13, 0, 1, (void *)t_sndglobal);

	/* timers: the manager, the simulation timer (orc 1's), a world's default timer (orc 2's), the player's own */
	uintptr_t tm = (uintptr_t)zalloc(0x20), tc = (uintptr_t)zalloc(8), world = (uintptr_t)zalloc(0xc100);
	put(tm, 0, G.vt_timer);
	put(tc, 0, tm);
	G.timer_pp = tc;
	uintptr_t deft = (uintptr_t)zalloc(0x100), pt = (uintptr_t)zalloc(0x100);
	g_simt = (uintptr_t)zalloc(0x100);
	uintptr_t ts[3] = { g_simt, deft, pt };
	for (int i = 0; i < 3; i++) {
		put(ts[i], 0, G.vt_etimer);
		put32(ts[i], TM_REF, 5);
		put32(ts[i], TM_NUM, 1);
		put32(ts[i], TM_DEN, 1);
		putf(ts[i], TM_SCALE, 1.0f);
		put32(ts[i], TM_FRAME, 16);
		put32(ts[i], TM_TOTAL, 12345);
	}
	put(rq(G.srv_pp), SRV_SIMTIMER, g_simt);
	put(world, WORLD_DEFTIMER, deft);
	put(g_orcobj[1], OBJ_TIMER, g_simt);
	put(g_orcobj[2], OBJ_TIMER, deft);
	put(g_orcobj[2], OBJ_WORLD, world);
	put(g_phobj, OBJ_TIMER, pt);
	put32(g_device, 0xa0, 28);
	put32(g_device, 0xe8, 28);

	/* two nav markers: one shown (an icon, an action id, a bool), one inactive */
	uintptr_t head = (uintptr_t)zalloc(0x20);
	G.nav_head = head;
	uintptr_t navrec = mk_record("NavMarker_Quest", 40);
	const struct dbf nv[] = { { 0xa701, 4, 1, { (uintptr_t)"Icon_Quest" } }, { 0xa704, 3, 1, { 34 } }, { 0xa705, 1, 1, { 1 } } };
	db_fill(navrec, 1, nv, 3);
	for (int k = 0; k < 2; k++) {
		g_navm[k] = (uintptr_t)zalloc(0x300);
		put(g_navm[k], NM_REC, navrec);
		*(uint8_t *)(g_navm[k] + NM_ACTIVE) = (uint8_t)(k == 0);
		*(uint8_t *)(g_navm[k] + NM_ENABLED) = 1;
		*(uint8_t *)(g_navm[k] + NM_SHOWN1) = (uint8_t)(k == 0);
		put(g_navm[k], 0x260, 1);
		put32(g_navm[k], NM_NODE, 0xffffffffu);
	}
	put(head, 8, g_navm[0] + NAV_NODE);
	put(g_navm[0] + NAV_NODE, 8, g_navm[1] + NAV_NODE);
	put(g_navm[1] + NAV_NODE, 8, head);

	/* the HUD: player UI -> +0xb0 -> +0x16e0; the standard prompts' pad diamond with one slot: a free entry, Activate */
	uintptr_t pui = (uintptr_t)zalloc(0x100), hb = (uintptr_t)zalloc(0x1800), pr = (uintptr_t)zalloc(0x200);
	uintptr_t dm = (uintptr_t)zalloc(0x40), slots = (uintptr_t)zalloc(0x90), ents = (uintptr_t)zalloc(2 * 0x38);
	put(g_player, P_UI, pui);
	put(pui, 0xb0, hb);
	put(hb + 0x16e0, HUD_PROMPTS, pr);
	put(pr, PR_DIAMOND, dm);
	put(dm, 0, G.vt_diamond_pad);
	put(dm, 0x18, slots);
	put32(dm, 0x20, 1);
	put(slots, 0x70, ents);
	put(slots, 0x78, ents + 2 * 0x38);
	put32(ents, 0x28, NCMD);
	g_prompt_entry = ents + 0x38;
	put(g_prompt_entry, 0, mk_record("Prompt_Activate", 41));
	put(g_prompt_entry, 0x10, (uintptr_t)"PROMPT_DRAIN");
	put(g_prompt_entry, 0x20, (uintptr_t)"Icon_B");
	put32(g_prompt_entry, 0x28, 34);
	put32(g_prompt_entry, 0x2c, 2);

	/* the UI scene manager (embedded in the interface manager) and a view for the render call */
	uintptr_t uis = g_im + IM_UISCENE, cam = (uintptr_t)zalloc(0x20), camo = (uintptr_t)zalloc(0x40);
	put(uis, 0, G.vt_uiscene);
	put(uis, 0x80, cam);
	put(cam, 8, camo);
	putf(camo, 8, 1.0f);
	putf(camo, 0x20, 1.0f);
	g_uiview = (uintptr_t)zalloc(0x328);
	G.cvar_sw = (uintptr_t)zalloc(8);
	G.cvar_sh = (uintptr_t)zalloc(8);
	G.aspect_lock = (uintptr_t)zalloc(8);
	put32(G.cvar_sw, 0, 1832);
	put32(G.cvar_sh, 0, 1920);
	putf(g_uiview, 0x20, 60.0f * (float)M_PI / 180.0f);
	putf(g_uiview, 0x44, 5000.0f);
}

static void *volatile g_physvt[70], *volatile g_clipvt[130];
static uintptr_t g_ai[3], g_beast, g_tgtrec;

extern char __executable_start[], etext[];

static void build_p8(void)
{
	G.vt_sm = 0x5d0000, G.vt_smrend = 0x5d1000, G.vt_rhandle = 0x5d2000, G.vt_rnode = 0x5d3000, G.vt_attach = 0x5d4000;
	G.vt_tracker = 0x5d5000, G.vt_uiview = 0x5d6000, G.vt_movie = 0x5d7000, G.vt_clip = 0x5d8000, G.vt_menusm = 0x5d9000;
	G.vt_puism = 0x5da000, G.vt_sysh = 0x5db000, G.vt_phys = (uintptr_t)g_physvt;
	G.text_lo = (uintptr_t)__executable_start;
	G.text_hi = (uintptr_t)etext;
	G.sys_gate = (uintptr_t)zalloc(8);

	/* objects with a model: +0x38 -> an object whose vtable's +0x20 is a function (obj_model's check) */
	static uintptr_t impl[2], implvt[8];
	implvt[4] = (uintptr_t)t_modelof;
	impl[0] = (uintptr_t)implvt;
	put(g_charobj, 0x38, (uintptr_t)impl);
	put(g_model, 0, G.vt_sm);

	/* the player model's renderable -> handle -> node (the mask chain) */
	uintptr_t rend = (uintptr_t)zalloc(0x80), hnd = (uintptr_t)zalloc(0x200);
	put(rend, 0, G.vt_smrend);
	put(rend, REND_HANDLE, hnd);
	put(hnd, 0, G.vt_rhandle);
	put(hnd, HANDLE_NODE, G.vt_rnode);
	put(g_model, SM_REND, rend);

	/* the attached children: 0 a model with 3 pieces (its own renderable chain), 1 without; 2 = the sword */
	for (int k = 0; k < 2; k++) {
		g_child[k] = (uintptr_t)zalloc(0x80);
		putf(g_child[k], OBJ_POS, 100.0f);
		putf(g_child[k], OBJ_POS + 4, 170.0f + 10.0f * (float)k);
		putf(g_child[k], OBJ_POS + 8, 200.0f);
	}
	put(g_child[0], 0x38, (uintptr_t)impl);
	g_childsm = (uintptr_t)zalloc(0x200);
	put(g_childsm, 0, G.vt_sm);
	uintptr_t crend = (uintptr_t)zalloc(0x80), chnd = (uintptr_t)zalloc(0x200);
	put(crend, 0, G.vt_smrend);
	put(crend, REND_HANDLE, chnd);
	put(chnd, 0, G.vt_rhandle);
	put(chnd, HANDLE_NODE, G.vt_rnode);
	put(g_childsm, SM_REND, crend);
	g_wobj = g_itemobj[0];
	put(g_wobj, 0x38, (uintptr_t)impl);
	g_wsm = (uintptr_t)zalloc(0x200);
	put(g_wsm, 0, G.vt_sm);
	uintptr_t wmd = (uintptr_t)zalloc(0x40), wsk = (uintptr_t)zalloc(0x80), wn = (uintptr_t)zalloc(3 * 0x40);
	uintptr_t wnames = (uintptr_t)zalloc(3 * 8), whashes = (uintptr_t)zalloc(3 * 4), wsock = (uintptr_t)zalloc(0x30);
	static const char *const wnm[3] = { "Grip", "Blade_Mid", "Blade_Tip" };
	put(g_wsm, 0x28, wmd);
	put(wmd, 0x10, wsk);
	put(wsk, 0, wn);
	put32(wsk, 8, 3);
	put(wsk, 0x10, wsock);
	put32(wsk, 0x18, 1);
	put(wsk, 0x38, wnames);
	put(wsk, 0x40, whashes);
	for (int i = 0; i < 3; i++) {
		*(uint8_t *)(wn + (uintptr_t)i * 0x40) = (uint8_t)(i ? i - 1 : 0xff);
		put(wnames, 8u * (unsigned)i, (uintptr_t)wnm[i]);
		put32(whashes, 4u * (unsigned)i, 0x5000u + (unsigned)i);
	}
	put32(wsock, 0, 0x5002);
	put32(wsock, 4, 2);
	g_attach = (uintptr_t)zalloc(0x60);
	g_entries8 = (uintptr_t)zalloc(3 * AT_ESIZE);
	put(g_attach, 0, G.vt_attach);
	put(g_attach, AT_ENTRIES, g_entries8);
	put(g_attach, AT_ENTRIES + 8, g_entries8 + 3 * AT_ESIZE);
	put(g_model, SM_ATTACH, g_attach);
	uintptr_t ch3[3] = { g_child[0], g_child[1], g_wobj };
	uint32_t idx3[3] = { 0, 2, 8 }, hs3[3] = { 0x1000, 0x1002, 0 };
	hs3[2] = name_hash("R_Weapon");
	for (int k = 0; k < 3; k++) {
		uintptr_t en = g_entries8 + (uintptr_t)k * AT_ESIZE;
		put(en, AE_CHILD, ch3[k]);
		put32(en, AE_INDEX, idx3[k]);
		put32(en, AE_HASH, hs3[k]);
		putf(en, AE_OFFSET + 12 + 12, 1.0f); /* quat w */
		if (k == 2)
			putf(en, AE_OFFSET + 8, 10.0f); /* the sword 10 cm along R_Weapon's +Z */
	}

	/* the player's tracker and its pose buffer: the base pose (model space), the right arm along +X at shoulder height */
	g_trk = (uintptr_t)zalloc(0x200);
	put(g_trk, 0, G.vt_tracker);
	g_pb = (uintptr_t)zalloc(0x60);
	g_pose_xf = (uintptr_t)zalloc(T_NN * 32);
	put(g_trk, TRK_POSE, g_pb);
	put(g_pb, PB_XF, g_pose_xf);
	put32(g_pb, PB_COUNT, T_NN);
	put(g_model, SM_TRACKER, g_trk);
	static const float bp[T_NN][3] = { { 0, 72, 0 }, { 0, 60, 0 }, { 0, 0, 0 }, { -50, 40, 0 }, { -60, 40, 0 },
					   { 20, 55, 0 }, { 50, 50, 5 }, { 75, 45, 5 }, { 85, 45, 5 }, { 80, 45, 10 } };
	for (int i = 0; i < T_NN; i++) {
		memcpy(g_base_pose[i], bp[i], 12);
		g_base_pose[i][7] = 1.0f; /* quat w */
	}
	memcpy((void *)g_pose_xf, g_base_pose, sizeof g_base_pose);

	/* the orcs' NPCs: +8 their HOBJECT, +0xc78 their AI; the player's CCharacter +8 its HOBJECT, health, min health */
	for (int i = 0; i < 3; i++) {
		uintptr_t npc = rq(g_orc[i] + C_OWNER);
		g_ai[i] = (uintptr_t)zalloc(0xc00);
		put(npc, GB_HOBJ, g_orcobj[i]);
		put(npc, NPC_AI, g_ai[i]);
	}
	uintptr_t powner = rq(g_scombat + C_OWNER);
	put(powner, GB_HOBJ, g_phobj);
	putf(powner, CH_HEALTH, 250.0f);
	*(uint8_t *)(g_player + P_PLINDEX) = 0;

	/* the UI: m_GameUI's view (im+0x2708 -> +8 -> +0x50) and its movie; the player-UI SM (state 5) with a menu layer */
	uintptr_t gvh = (uintptr_t)zalloc(0x60), gv = (uintptr_t)zalloc(0x40), mv = (uintptr_t)zalloc(0x5400);
	put(g_im, IM_GAMEUI + 8, gvh);
	put(gvh, 0x50, gv);
	put(gv, 0, G.vt_uiview);
	put(gv, VIEW_MOVIE, mv);
	put(mv, 0, G.vt_movie);
	put32(mv, MV_FOCUS, 0x40180);
	put32(mv, MV_CURSORS, 1);
	uintptr_t mbh = (uintptr_t)zalloc(0x60), mbv = (uintptr_t)zalloc(0x40); /* m_MessageBoxUI: +8 -> +0x50 = its view */
	put(g_im, IM_MSGBOX + 8, mbh);
	put(mbh, 0x50, mbv);
	put(mbv, 0, G.vt_uiview);
	uintptr_t hb = rq(rq(g_player + P_UI) + 0xb0), layer = (uintptr_t)zalloc(0x700), comps = (uintptr_t)zalloc(16);
	put(hb, 0, G.vt_puism);
	put32(hb, 8, 5);
	put32(hb, 0xc, 0xffffffffu);
	put32(hb + PUISM_MGR, MGR_NLAYERS, 1);
	put(hb + PUISM_MGR, MGR_LAYERS, layer);
	put(layer, LAYER_COMPS, comps);
	put(layer, LAYER_COMPS + 8, comps + 16);
	put32(layer, LAYER_FOCUS, 0);
	for (int i = 0; i < 130; i++)
		g_clipvt[i] = (void *)(uintptr_t)(0x8f0000 + 16 * i);
	g_clipvt[120] = (void *)t_clipview;
	g_clipvt[109] = (void *)t_cliphit;
	g_clipview_obj = (uintptr_t)zalloc(0x40);
	for (int i = 0; i < 2; i++) {
		uintptr_t comp = (uintptr_t)zalloc(0x40);
		g_clip[i] = (uintptr_t)zalloc(0x40);
		put(g_clip[i], 0, (uintptr_t)g_clipvt);
		put(comp, COMP_CLIP, g_clip[i]);
		put(comps, 8u * (unsigned)i, comp);
	}

	/* the physics object (slot 64 = the ray), a hit object with a surface id, the Surface schema and a record */
	uintptr_t phys = (uintptr_t)zalloc(0x40), pc = (uintptr_t)zalloc(8);
	put(phys, 0, (uintptr_t)g_physvt);
	put(pc, 0, phys);
	G.phys_pp = pc;
	for (int i = 0; i < 70; i++)
		g_physvt[i] = (void *)(uintptr_t)(0x8a0000 + 16 * i);
	mk_ro_slot(RO_PHYS_RAY, "PHYS_RAYCAST", g_physvt, 64, 0, 1, (void *)t_raycast);
	g_hitobj = (uintptr_t)zalloc(0x80);
	*(uint16_t *)(g_hitobj + 0x5a) = 7;
	static const char *const s_surf[] = { "CanSeeThrough", "CanWallClimb", "IsSlippery", NULL };
	G.sch_surface = mk_schema(s_surf, 0xa800);
	g_surfrec = mk_record("Surfaces/Stone_Climbable", 50);
	const struct dbf sf[] = { { 0xa801, 1, 1, { 1 } }, { 0xa802, 1, 1, { 0 } } };
	db_fill(g_surfrec, 1, sf, 2);

	/* the console's output slot (the repository's +8) */
	put(g_repo, 8, (uintptr_t)t_conout);

	/* a beast for `mount` (set only for that test) */
	g_beast = (uintptr_t)zalloc(0x80);
	putf(g_beast, OBJ_POS, 500.0f);
	putf(g_beast, OBJ_ROT + 12, 1.0f);
	g_tgtrec = mk_record("AI_Infantry", 60);
}

/* ---------------------------------------------------------------- P9 world ---------------------------------------- */

/* a category of N inline records (0x30 apart) named NAMES */
static uintptr_t mk_cat(const char *const *names, int n, uintptr_t *base_out)
{
	uintptr_t cat = (uintptr_t)zalloc(0x80), base = (uintptr_t)zalloc((size_t)n * DBREC_SIZE), cell = (uintptr_t)zalloc(8);
	for (int i = 0; i < n; i++) {
		uintptr_t r = base + (uintptr_t)i * DBREC_SIZE;
		*(uint8_t *)(r + 1) = 2;
		put(r, 0x20, (uintptr_t)names[i]);
		put32(r, 0x28, 0x900u + (unsigned)i);
	}
	put32(cat, CAT_COUNT, (uint32_t)n);
	put(cat, CAT_RECS, base);
	put(cell, 0, cat);
	*base_out = base;
	return cell;
}

static void build_p9(void)
{
	/* the NPC class chain and the one NPC the spawn stand-in places: HOBJECT (+8 pos, +0x38 the NPC, +0x48 the classes),
	 * the NPC (vtable = G.vt_npc, +8 its HOBJECT, +0x10 -> +0x70 the record holder, working memory, brain, Combat) */
	g_cls_obj[1] = (uintptr_t)g_cls_npc;
	g_cls_head = (uintptr_t)g_cls_obj;
	G.class_npc = (uintptr_t)g_cls_npc;
	g_sp_h = (uintptr_t)zalloc(0x100);
	g_sp_npc = (uintptr_t)zalloc(0x1000);
	g_sp_c = (uintptr_t)zalloc(0x600);
	uintptr_t hold = (uintptr_t)zalloc(0x80), brain = (uintptr_t)zalloc(0xc00);
	put(g_sp_h, HOBJ_CLASSES, (uintptr_t)&g_cls_head);
	put(g_sp_h, HOBJ_GAMEOBJ, g_sp_npc);
	put(g_sp_npc, 0, G.vt_npc);
	put(g_sp_npc, GB_HOBJ, g_sp_h);
	put(g_sp_npc, NPC_RECHOLD, hold);
	put(hold, 0x70, 0x4242);
	put(g_sp_npc, NPC_WM, (uintptr_t)zalloc(0x40));
	put(g_sp_npc, NPC_AI, brain);
	put(g_sp_npc, CH_COMBAT, g_sp_c);
	put(g_sp_c, 0, G.vt_combat);
	put(g_sp_c, C_OWNER, g_sp_npc);
	put(g_sp_c, C_AI, 0x3333);
	put(g_sp_c, C_HOBJ, g_sp_h);
	for (int i = 0; i < 96; i++)
		g_npcvt[i] = (void *)(uintptr_t)(0x8d0000 + 16 * i);
	mk_ro_slot(RO_NPC_SETZONE, "NPC_SETZONE", g_npcvt, 0x288 / 8, 0, 1, (void *)t_npcsetzone);
	mk_ro_slot(RO_OBJSYS_REMOVE, "OBJSYS_REMOVE", g_objvt, 0x58 / 8, 0, 1, (void *)t_objremove);

	/* the character combat record schema (56 AIBlockTable, 61 WeaponModeSetup), the table and its two entries */
	static char ccrn[62][16];
	static const char *s_ccr[63];
	for (int i = 0; i < 62; i++) {
		snprintf(ccrn[i], sizeof ccrn[i], "CCR_%d", i);
		s_ccr[i] = ccrn[i];
	}
	s_ccr[CCR_AIBLOCKTABLE] = "AIBlockTable";
	s_ccr[CCR_WMSETUP] = "WeaponModeSetup";
	static const char *const s_btbl[] = { "Entries", NULL }, *const s_bent[] = { "Category", "BlockChance", NULL };
	G.sch_ccr = mk_schema(s_ccr, 0xb300);
	G.sch_btbl = mk_schema(s_btbl, 0xb100);
	G.sch_bent = mk_schema(s_bent, 0xb200);
	g_cat_light = mk_record("Cat_Light", 70);
	g_cat_heavy = mk_record("Cat_Heavy", 71);
	uintptr_t tbl = mk_record("AIBlockTable_Defender", 72), e0 = mk_record("BlockChance_Light", 73),
		  e1 = mk_record("BlockChance_Any", 74);
	const struct dbf fe0[] = { { 0xb200, 0xa, 1, { g_cat_light } }, { 0xb201, 2, 1, { f2u(0.25f) } } };
	const struct dbf fe1[] = { { 0xb200, 0xa, 1, { 0 } } };
	const struct dbf ft[] = { { 0xb100, 0xa, 2, { e0, e1 } } };
	db_fill(e0, 1, fe0, 2);
	db_fill(e1, 1, fe1, 1);
	db_fill(tbl, 1, ft, 1);
	g_ccr_def = mk_record("AI_Defender", 75);
	const struct dbf fc[] = { { 0xb300 + CCR_AIBLOCKTABLE, 0xa, 1, { tbl } } };
	db_fill(g_ccr_def, 1, fc, 1);
	put(g_sp_c, C_CHARREC, g_ccr_def);

	/* AI/Templates: one references AI_Defender directly (a captain: scored down), one through a character record, one
	 * AI_Infantry (the faction member's) */
	static const char *const tn[3] = { "Tmpl_Orc_Defender_Captain", "Tmpl_Uruk_Shield", "Tmpl_Ghash_Captain" };
	G.tmplcat_pp = mk_cat(tn, 3, &g_tmpl_base);
	for (int i = 0; i < 3; i++)
		g_tmpl[i] = g_tmpl_base + (uintptr_t)i * DBREC_SIZE;
	uintptr_t chr = mk_record("Char_Uruk_Shield", 76), inf = mk_record("AI_Infantry", 77);
	const struct dbf ch1[] = { { 0xb401, 0xa, 1, { g_ccr_def } } };
	db_fill(chr, 1, ch1, 1);
	const struct dbf t0[] = { { 0xb400, 0xa, 1, { g_ccr_def } } }, t1[] = { { 0xb400, 0xa, 1, { chr } } },
			 t2[] = { { 0xb400, 0xa, 1, { inf } } };
	db_fill(g_tmpl[0], 1, t0, 1);
	db_fill(g_tmpl[1], 1, t1, 1);
	db_fill(g_tmpl[2], 1, t2, 1);

	/* the game services (vtbl+0x78 the faction manager, +0xa8 the weather state), a faction member, the weathers */
	uintptr_t gs = (uintptr_t)zalloc(0x20), gcell = (uintptr_t)zalloc(8);
	for (int i = 0; i < 24; i++)
		g_gsvcvt[i] = (void *)(uintptr_t)(0x8e8000 + 16 * i);
	g_gsvcvt[0x78 / 8] = (void *)t_fmgrget;
	g_gsvcvt[0xa8 / 8] = (void *)t_wstateget;
	put(gs, 0, (uintptr_t)g_gsvcvt);
	put(gcell, 0, gs);
	G.gsvc_pp = gcell;
	g_fmgr = (uintptr_t)zalloc(0x100);
	g_fm = (uintptr_t)zalloc(0x400);
	put(g_fm, FM_ACTIVE, 1);
	put32(g_fm, FM_LEVEL, 12);
	g_wstate = (uintptr_t)zalloc(0x100);
	put(g_wstate, W_RECORD, mk_record("Weather_Clear_Day", 78));
	*(uint8_t *)(g_wstate + W_TOD) = 3;
	put32(g_wstate, W_INDEX, 4);
	static const char *const wn[2] = { "Weather_Clear_Day", "Weather_Rain" };
	uintptr_t wb;
	G.wxcat_pp = mk_cat(wn, 2, &wb);
	G.spawn_level = (uintptr_t)zalloc(8);
	put32(G.spawn_level, 0, 3);

	/* the weapon modes: a bow aim (Predicate -> command 118, FreeAim) and wraith vision (243, VisionMode); the player's
	 * WeaponModeSetup lists both */
	static const char *const s_wm[] = { "OnDuration", "Predicate", "Data", "SlowMo", "VisionMode", "UIMode", "FreeAim", NULL };
	static const char *const s_pred[] = { "UseRawInput", "CommandID", "HeldDuration", "IsToggle", "RequireToken",
					      "MinEnergyToEnter", "MinEnergyToRemain", NULL };
	static const char *const s_fa[] = { "MaxTargetDistance", "ModelNodeTestFirePos", NULL }, *const s_vm[] = { "VisionMode", NULL };
	static const char *const s_one[] = { "WeaponSlot", NULL }, *const s_setup[] = { "Entries", "EnergyDrain", NULL };
	G.sch_wm = mk_schema(s_wm, 0xb500);
	G.sch_wmpred = mk_schema(s_pred, 0xb600);
	G.sch_wmfreeaim = mk_schema(s_fa, 0xb700);
	G.sch_wmvision = mk_schema(s_vm, 0xb800);
	G.sch_wmdata = mk_schema(s_one, 0xb900);
	G.sch_wmslowmo = mk_schema(s_one, 0xba00);
	G.sch_wmui = mk_schema(s_one, 0xbb00);
	G.sch_wmsetup = mk_schema(s_setup, 0xbc00);
	static const char *const mn[3] = { "Mode_Stealth", "Mode_Bow_Aim", "Mode_Wraith_Vision" }; /* P10: P9's wrong pick first */
	uintptr_t mb, ms;
	G.wmcat_pp = mk_cat(mn, 3, &ms);
	mb = ms + DBREC_SIZE;
	g_wm_pred[0] = mk_record("Pred_Bow", 80);
	g_wm_pred[1] = mk_record("Pred_Vision", 81);
	g_wm_pred[2] = mk_record("Pred_Stealth", 86);
	const struct dbf ps[] = { { 0xb601, 4, 1, { (uintptr_t)"CombatSecondary04" } } };
	const struct dbf mst[] = { { 0xb501, 0xa, 1, { g_wm_pred[2] } }, { 0xb506, 0xa, 1, { 0 } } }; /* FreeAim: one null */
	db_fill(g_wm_pred[2], 1, ps, 1);
	db_fill(ms, 1, mst, 2);
	uintptr_t fa = mk_record("FreeAim_Bow", 82), vm = mk_record("Vision_Wraith", 83), setup = mk_record("Setup_Talion", 84);
	const struct dbf p0[] = { { 0xb601, 4, 1, { (uintptr_t)"CombatSecondary03" } }, { 0xb602, 2, 1, { f2u(0.2f) } },
				  { 0xb603, 1, 1, { 0 } } };
	const struct dbf p1[] = { { 0xb601, 4, 1, { (uintptr_t)"VisionModeToggle" } }, { 0xb603, 1, 1, { 1 } } };
	const struct dbf ffa[] = { { 0xb700, 2, 1, { f2u(5000.0f) } }, { 0xb701, 4, 1, { (uintptr_t)"LeftHand" } } };
	const struct dbf fvm[] = { { 0xb800, 0xa, 1, { vm } } };
	const struct dbf m0[] = { { 0xb501, 0xa, 1, { g_wm_pred[0] } }, { 0xb506, 0xa, 1, { fa } } };
	const struct dbf m1[] = { { 0xb501, 0xa, 1, { g_wm_pred[1] } }, { 0xb504, 0xa, 1, { vm } } };
	const struct dbf fs[] = { { 0xbc00, 0xa, 2, { mb, mb + DBREC_SIZE } } };
	db_fill(g_wm_pred[0], 1, p0, 3);
	db_fill(g_wm_pred[1], 1, p1, 2);
	db_fill(fa, 1, ffa, 2);
	db_fill(vm, 1, fvm, 1);
	db_fill(mb, 1, m0, 2);
	db_fill(mb + DBREC_SIZE, 1, m1, 2);
	db_fill(setup, 1, fs, 1);
	g_ccr_pl = mk_record("PC_Talion_Combat", 85);
	const struct dbf fp[] = { { 0xb300 + CCR_WMSETUP, 0xa, 1, { setup } } };
	db_fill(g_ccr_pl, 1, fp, 1);

	/* a CAIGoalCounterMelee stand-in: +8 the NPC, +0x10 the relevance */
	g_goal = (uintptr_t)zalloc(0x60);
	put(g_goal, 8, g_sp_npc);
}

/* ---------------------------------------------------------------- P10 world --------------------------------------- */

/* Client.PlayerMovement's 30 descriptors (even ones on the fast path: +0x18 = 2, the entry index +0x28; odd ones by the
 * hash search), the player record at pm+0x448 (direct, entry i = attribute i), the caches, the proxy, the graph, the
 * game timer's frame time (loco's dt), two console variables */
static const char *const g_pm_names[31] = {
	"FallDamageMinHeight", "FallDamageMaxHeight", "FallDamageMin", "FallDamageMax", "FallMinHeight", "CamLandMinHeight",
	"CamLandMoveDist", "CamLandDownTime", "CamLandUpTime", "CamLandRollVal", "CamLandRollTime1", "CamLandRollTime2",
	"CamLandPitchVal", "CamLandPitchTime1", "CamLandPitchTime2", "PlayerYawInterpolateTime", "BaseSpeedMultiplier",
	"PhysicsCharacterGravity", "UsePenCollision", "PenOriginOffset", "StandingPenOriginOffset", "AimingPenOriginOffset",
	"AimingRadiusScale", "AimingTeleportRange", "AimingTeleportExclusiveTags", "StandCapsuleHeight", "StandCapsuleRadius",
	"StandCapsuleStepHeight", "SpectatorSpeedMultiplier1", "SpectatorSpeedMultiplier2", NULL };
static uintptr_t g_pc10;

static void build_p10(void)
{
	G.sch_pm = mk_schema(g_pm_names, 0x7000);
	uintptr_t arr = rq(G.sch_pm), ncell = (uintptr_t)zalloc(8);
	for (int i = 0; i < 30; i++) {
		put32(arr, (size_t)i * 0x30 + 0x18, i % 2 ? 0 : 2);
		*(uint8_t *)(arr + (size_t)i * 0x30 + 0x28) = (uint8_t)i;
	}
	put32(ncell, 0, 30);
	G.sch_pm_n = ncell;
	static struct dbf f[30];
	for (int i = 0; i < 30; i++)
		f[i] = (struct dbf){ 0x7000u + (uint32_t)i, 2, 1, { f2u(10.0f * (float)i) } };
	f[4].v[0] = f2u(100.0f);
	f[15] = (struct dbf){ 0x7000 + 15, 3, 1, { 400 } };
	f[16].v[0] = f2u(1.0f);
	f[17].v[0] = f2u(-20.0f);
	f[18] = (struct dbf){ 0x7000 + 18, 1, 1, { 1 } };
	f[19] = (struct dbf){ 0x7000 + 19, 2, 3, { 0, f2u(50.0f), 0 } };
	g_pmrec = mk_record("PlayerMovement_Talion", 0x7100);
	db_fill(g_pmrec, 1, f, 30);
	put(g_pm, PM_REC, g_pmrec);
	putf(g_pm, PM_BASESPEED, 1.0f);
	putf(g_pm, PM_YAWTIME, 0.4f);
	putf(g_pm, PM_FALLMIN, 100.0f);
	*(uint8_t *)(g_pm + PM_VELMODE) = 1;
	g_pc10 = (uintptr_t)zalloc(0x80);
	putf(g_pc10, PCH_GRAVITY, -20.0f);
	*(uint8_t *)(g_pc10 + PCH_GRAVON) = 1;
	put(g_pm, PM_PHYS, g_pc10);
	put(rq(g_body + BODY_MGR), MGR_GRAPHDEF, (uintptr_t)zalloc(0x40));
	putf(g_pausestate, 0x20 + TM_FRAME + 4, 0.016f); /* the game timer's frame: 16 ms */
	put32(g_pausestate, 0x20 + TM_FRAME, 16);
	static const char *const pn[2] = { "PlayerMovement.BaseSpeedMultiplier", "Client.PlayerMovement.FallMinHeight" };
	for (int i = 0; i < 2; i++) {
		g_pmv[i] = (uintptr_t)zalloc(0x60);
		put(g_pmv[i], 0, 0x5c3000);
		put(g_pmv[i], 8, (uintptr_t)pn[i]);
	}
	g_cam_for_rd = g_cam;
	g_charobj_for_rd = g_charobj;
	g_ray_root = g_charobj;
}

/* ---------------------------------------------------------------- driving ------------------------------------ */

static char g_cmd_file[1024];
static unsigned g_seq_sent;

static void tick(int n)
{
	for (int i = 0; i < n; i++) {
		((void1_fn)g_vt[VT_INPUT])((void *)0x1234);
		if (!g_pm_run)
			continue;
		float xf[7]; /* the movement: vf_11 on the root's transform, then the transform onto the object */
		memcpy(xf, (void *)(g_charobj + OBJ_POS), 12);
		memcpy(xf + 3, (void *)(g_charobj + OBJ_ROT), 16);
		((pmroot_fn)g_vt[VT_PMROOT])((void *)g_pm, xf);
		memcpy((void *)(g_charobj + OBJ_POS), xf, 12);
	}
}

/* write the command file the way the harness does (tmp + rename), then let the probe pick it up */
static void send(const char *line, int ticks)
{
	char tmp[1100];
	snprintf(tmp, sizeof tmp, "%s.tmp", g_cmd_file);
	FILE *f = fopen(tmp, "w");
	assert(f);
	fprintf(f, "%s\n", line);
	fclose(f);
	struct timespec ts = { 0, 20 * 1000 * 1000 };
	nanosleep(&ts, NULL); /* a different mtime than the last version */
	rename(tmp, g_cmd_file);
	poll_cmd_file();
	g_seq_sent++;
	tick(ticks);
}

static float cmd_value(int cmd)
{
	uintptr_t rec = pi_rec(g_pi_obj, cmd);
	return ((value_fn)g_vt[VT_VALUE])((void *)0x1, (void *)rec);
}
static int cmd_ison(int cmd)
{
	uintptr_t rec = pi_rec(g_pi_obj, cmd);
	return (int)(((ison_fn)g_vt[VT_ISON])((void *)0x1, (void *)rec, 0) & 0xff);
}

static void srv_tick(void) { ((void1_fn)g_vt[VT_SERVER])((void *)0x7); } /* runs the words waiting for the server tick */

/* the log so far contains NEEDLE */
static char g_logp[1024];
static int log_has(const char *needle)
{
	static char buf[1 << 22];
	flush_log();
	FILE *f = fopen(g_logp, "r");
	if (!f)
		return 0;
	size_t n = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	buf[n] = 0;
	return strstr(buf, needle) != NULL;
}

static void msleep(long ms)
{
	struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
	nanosleep(&t, NULL);
}

static void mkdirs(const char *path)
{
	char b[1100];
	snprintf(b, sizeof b, "%s", path);
	for (char *q = b + 1; *q; q++)
		if (*q == '/') {
			*q = 0;
			mkdir(b, 0755);
			*q = '/';
		}
	mkdir(b, 0755);
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: selftest DIR\n");
		return 2;
	}
	char logp[1024], outd[1024];
	snprintf(logp, sizeof logp, "%s/probe.log", argv[1]);
	snprintf(g_logp, sizeof g_logp, "%s", logp);
	snprintf(g_cmd_file, sizeof g_cmd_file, "%s/probe.cmd", argv[1]);
	snprintf(outd, sizeof outd, "%s/out", argv[1]);
	setenv("SOMVR_PROBE_LOG", logp, 1);
	setenv("SOMVR_PROBE_CMD", g_cmd_file, 1);
	setenv("SOMVR_PROBE_OUT", outd, 1);
	char xdg[1100];
	snprintf(xdg, sizeof xdg, "%s/xdg", argv[1]);
	setenv("XDG_DATA_HOME", xdg, 1); /* @cout's host side: XDG/feral-interactive/ShadowOfMordor/VFS/Temp */
	unsetenv("SOMVR_PROBE_AUTO");
	unsetenv("SOMVR_PROBE_NOGUARD_OK");
	EXPECT(!strcmp(g_head_names[g_neck_index], "Neck"), "g_neck_index does not point at \"Neck\"");
	EXPECT(g_cmd_names[118] && !strcmp(g_cmd_names[118], "CombatPrimary01"), "command table: 118 is not CombatPrimary01");
	EXPECT(!strcmp(g_cmd_names[3], "ForwardAxis") && !strcmp(g_cmd_names[6], "StrafeAxis"), "command table: move axes");

	G.vt_combat = 0x5c0000; /* fake vtable values; only compared, never called */
	G.vt_npc = (uintptr_t)g_npcvt; /* P9: a real table (spawn calls its slot 0x288 after checking it) */
	G.vt_playerobj = 0x5c2000;
	build_world();
	build_p7();
	build_p8();
	build_p9();
	build_p10();

	mk_vslot(HK_INPUT, "P_INPUT", VT_INPUT, (void *)o_input);
	mk_vslot(HK_VALUE, "P_VALUE", VT_VALUE, (void *)o_value);
	mk_vslot(HK_VALUE2, "P_VALUE2", VT_VALUE2, (void *)o_value2);
	mk_vslot(HK_ISON, "P_ISON", VT_ISON, (void *)o_ison);
	mk_vslot(HK_SERVER, "P_SERVER", VT_SERVER, (void *)o_server);
	mk_vslot(HK_VIS, "P_VIS", VT_VIS, (void *)o_vis);
	mk_vslot(HK_UI105, "P_UI105", VT_UI105, (void *)o_ui105);
	mk_vslot(HK_PMROOT, "P_PMROOT", VT_PMROOT, (void *)o_pmroot);
	mk_call(HK_ANIM_A, "P_ANIM_A", (void *)c_anim, (void *)t_anim);
	mk_call(HK_CAMPOST_A, "P_CAMPOST_A", (void *)c_campost, (void *)t_campost);
	mk_call(HK_ATK_NOPOS, "P_ATK_NOPOS", (void *)c_atk, (void *)t_atk);
	mk_call(HK_DEALHIT, "P_DEALHIT", (void *)c_gamehit, (void *)t_gamehit);
	mk_call(HK_COUNTER, "P_COUNTER", (void *)c_counter, (void *)t_counter);
	mk_call(HK_DEATH, "P_DEATH", (void *)c_death, (void *)t_death);
	mk_call(HK_APPLYDMG, "P_APPLYDMG", (void *)c_applydmg, (void *)t_applydmg);
	/* P7 */
	mk_call(HK_STIM_START, "P_STIM_START", (void *)c_stim_start, (void *)t_stim);
	mk_call(HK_STIM_KEY, "P_STIM_KEY", (void *)c_stim_key, (void *)t_stim);
	mk_call(HK_STIM_STOP, "P_STIM_STOP", (void *)c_stim_stop, (void *)t_stim);
	mk_call(HK_REGSTIM, "P_REGSTIM", (void *)t_stim, (void *)t_regstim);
	mk_call(HK_REGSTIM_P, "P_REGSTIM_P", (void *)t_stim, (void *)t_regstim_p);
	static const char *const ai_names[5] = { "P_AI_GOAL", "P_AI_BLOCK_OK", "P_AI_BLOCK_GO", "P_AI_DODGE_OK", "P_AI_DODGE_GO" };
	for (int k = 0; k < 5; k++)
		mk_vslot(HK_AI_GOAL + k, ai_names[k], VT_AI0 + k, (void *)o_ai);
	mk_call(HK_KEYTIMES, "P_KEYTIMES", (void *)c_keytimes, (void *)t_keytimes);
	mk_vslot(HK_PJ_UPDATE, "P_PJ_UPDATE", VT_PJ, (void *)o_pjupdate);
	mk_call(HK_PJ_DAMAGE, "P_PJ_DAMAGE", (void *)c_pjdamage, (void *)t_pjdamage);
	mk_call(HK_SND_POST, "P_SND_POST", (void *)t_sndpost_wrap, (void *)t_akpost);
	mk_call(HK_SND_POST_CB, "P_SND_POST_CB", (void *)t_sndpost_cb, (void *)t_akpost);
	mk_call(HK_UISCENE, "P_UISCENE", (void *)c_uisrender, (void *)t_uisrender);
	mk_call(HK_FX0, "P_FXI_A07FE1", (void *)c_fxinit, (void *)t_fxinit);
	mk_call(HK_RES3D, "P_RES3D", (void *)c_res3d, (void *)t_res3d);
	/* P8 */
	mk_call(HK_IK_SETUP_A, "P_IK_SETUP_A", (void *)c_iksetup, (void *)t_iksetup);
	mk_call(HK_IK_SETUP_B, "P_IK_SETUP_B", (void *)c_iksetup2, (void *)t_iksetup);
	mk_call(HK_IK_CHAIN, "P_IK_CHAIN", (void *)c_ikchain, (void *)t_ikchain);
	mk_vslot(HK_IEM_ADDCB, "P_IEM_ADDCB", VT_ADDCB, (void *)o_addcb);
	mk_vslot(HK_IEM_REMCB, "P_IEM_REMCB", VT_REMCB, (void *)o_remcb);
	mk_vslot(HK_TRK_EVAL, "P_TRK_EVAL", VT_TRK, (void *)o_trk);
	mk_vslot(HK_ATTACH_LISTEN, "P_ATTACH_LISTEN", VT_LISTEN, (void *)o_listen);
	mk_call(HK_LP_GUARD_HOVER, "P_LP_GUARD_HOVER", (void *)c_guard_h, (void *)t_padmode);
	mk_call(HK_LP_GUARD_CLICK, "P_LP_GUARD_CLICK", (void *)c_guard_c, (void *)t_padmode);
	static const char *const lp_names[7] = { "P_LP_SYS_DOWN", "P_LP_SYS_UP", "P_LP_SYS_MOVE", "P_LP_VIEW_DOWN", "P_LP_VIEW_UP",
						 "P_LP_VIEW_MOVE", "P_LP_GFX_EVENT" };
	for (int k = 0; k < 3; k++)
		mk_vslot(HK_LP_SYS_DOWN + k, lp_names[k], VT_SYS0 + k, (void *)o_sys);
	for (int k = 0; k < 3; k++)
		mk_vslot(HK_LP_VIEW_DOWN + k, lp_names[3 + k], VT_VIEW0 + k, (void *)o_view);
	mk_vslot(HK_LP_GFX_EVENT, lp_names[6], VT_GFX, (void *)o_gfx);
	mk_vslot(HK_GRAV, "P_GRAV", VT_GRAV, (void *)o_grav);
	/* P9 */
	mk_call(HK_AIB_GATE, "P_AIB_GATE", (void *)c_aibgate, (void *)t_aibgate);
	mk_call(HK_AIB_REC, "P_AIB_REC", (void *)c_aibrec, (void *)t_charcombat);
	mk_call(HK_AIB_ROLL, "P_AIB_ROLL", (void *)c_aibroll, (void *)t_roll);
	mk_call(HK_AIB_NOTIFY, "P_AIB_NOTIFY", (void *)c_aibnotify, (void *)t_aifact);
	mk_vslot(HK_GOAL_REL, "P_GOAL_REL", VT_GOALREL, (void *)o_goalrel);
	mk_call(HK_SHIELD, "P_SHIELD", (void *)c_shield, (void *)t_shield);
	mk_call(HK_BOW_CAMXF, "P_BOW_CAMXF", (void *)c_camxf, (void *)t_camxf);
	/* P10 */
	mk_call(HK_ROOTDELTA, "P_ROOTDELTA", (void *)o_pmroot, (void *)t_rootdelta);
	/* the ones without a stand-in: skipped (SOMVR_PROBE_SKIP), specs that are never looked at */
	static char skip[4096];
	size_t so = 0;
	int si = 0;
	for (int i = 0; i < HK_N; i++)
		if (!g_sites[i].name) {
			g_sites[i] = (struct site_spec){ g_hook_sites[i].name, K_VSLOT, 0, 0, "00", 0, "00" };
			so += (size_t)snprintf(skip + so, sizeof skip - so, "%s%s", so ? "," : "", g_hook_sites[i].name);
			si++;
		}
	setenv("SOMVR_PROBE_SKIP", skip, 1);
	EXPECT(si == 5 + HK_FX_N - 1, "selftest: %d sites without a stand-in, expected %d (P_ANIM_B, P_CAMPOST_B, P_ATK_OBJ, "
	       "P_ATK_POS, P_HITEV and 18 of the FX inits)", si, 5 + HK_FX_N - 1);

	if (probe_start(g_sites, NULL, g_ro_test, "selftest") != 0) {
		printf("FAIL probe_start\n");
		return 1;
	}
	for (int i = 0; i < HK_N; i++) {
		int want = !skip_listed(g_sites[i].name);
		EXPECT(g_h[i].installed == want, "%s installed=%d want=%d (see the log)", g_sites[i].name, g_h[i].installed, want);
	}
	for (int i = 0; i < RO_N; i++)
		EXPECT(g_ro_ok[i], "console site %s did not check out against its stand-in", g_ro_test[i].name);

	/* 1. the tick, the state logger and the guard check */
	tick(3);
	EXPECT(g_tick_calls == 3, "the original CWBInput::vf_04 ran %d times, want 3", g_tick_calls);
	EXPECT(g_tick >= 3, "no probe ticks (%llu)", (unsigned long long)g_tick);
	EXPECT(g_guard_ok == 0, "guard check: %d (the fake slot still holds the original)", g_guard_ok);
	EXPECT(g_st_have && g_st_last.ifs == 3 && g_st_last.player == g_player, "state: ifs=%d player=%#lx", g_st_last.ifs,
	       (unsigned long)g_st_last.player);

	/* 2. active words are refused while the guard is not in place */
	send("cmd Jump ms=50", 2);
	EXPECT(T.on_calls == 0, "cmd ran without the save guard (%d ON calls)", T.on_calls);
	put(g_guard_cell, 0, 0x12340000); /* spike1's guard would hold its own function here */
	g_guard_ok = -1;
	tick(1);
	EXPECT(g_guard_ok == 1, "guard check after the patch: %d", g_guard_ok);

	/* 3. read-only words; then more polls of the same version queue nothing */
	send("bindings;pieces;nodes;orcs;attacks;ui", 3);
	unsigned seq0 = g_cmd_seq;
	poll_cmd_file();
	poll_cmd_file();
	EXPECT(g_cmd_seq == seq0, "the same command file version was queued again (seq %u -> %u)", seq0, g_cmd_seq);
	EXPECT(g_orc_rejected == 0, "orcs: %d of the stand-in candidates were rejected by the vtable check", g_orc_rejected);
	EXPECT(g_pose.have_node && !strcmp(g_pose.node_name, "Head"), "nodes: head node '%s' (from the SocketHead parent)",
	       g_pose.node_name);
	EXPECT(g_pose.node == 0x1000, "nodes: head node id %#x, want the hash of skeleton node 0", g_pose.node);
	EXPECT(name_hash("Neck") == 0x87ec5687u, "the node-name hash of \"Neck\" is %#x, want 0x87ec5687 (re-animation.md 2.3)",
	       name_hash("Neck"));
	EXPECT(name_hash("R_Hand") == 0xc7485ad3u && name_hash("SocketHead") == 0x7b92d623u,
	       "the node-name hash: R_Hand %#x (want 0xc7485ad3), SocketHead %#x (want 0x7b92d623)", name_hash("R_Hand"),
	       name_hash("SocketHead"));
	char p[1200];
	snprintf(p, sizeof p, "%s/bindings-%u.tsv", outd, g_seq_sent);
	EXPECT(access(p, R_OK) == 0, "bindings file %s missing", p);
	snprintf(p, sizeof p, "%s/nodes-%u.tsv", outd, g_seq_sent);
	EXPECT(access(p, R_OK) == 0, "nodes file %s missing", p);
	snprintf(p, sizeof p, "%s/attacks.tsv", outd);
	EXPECT(access(p, R_OK) == 0, "attacks file %s missing", p);

	/* 4. one command: ON now, polled state on, OFF at the release; other records pass through */
	T.on_calls = T.off_calls = 0;
	send("cmd CombatPrimary01 ms=120", 1);
	EXPECT(T.on_calls == 1 && T.last_on_cmd == 118, "cmd ON calls=%d cmd=%d", T.on_calls, T.last_on_cmd);
	EXPECT(T.last_on_input == 2012, "cmd ON input=%d want 2012 (the bound pad input)", T.last_on_input);
	EXPECT(cmd_ison(118) == 1, "is-on override for the pressed command");
	EXPECT(cmd_value(118) == 1.0f, "value override for the pressed command: %g", cmd_value(118));
	EXPECT(cmd_ison(16) == 0 && cmd_value(16) == 7.0f, "another command did not pass through to the original");
	EXPECT(T.off_calls == 0, "OFF came too early");
	struct timespec ts = { 0, 200 * 1000 * 1000 };
	nanosleep(&ts, NULL);
	tick(3);
	EXPECT(T.off_calls == 1 && T.last_off_cmd == 118, "cmd OFF calls=%d cmd=%d", T.off_calls, T.last_off_cmd);
	EXPECT(cmd_ison(118) == 0 && cmd_value(118) == 7.0f, "the released command still overrides");
	EXPECT(atomic_load(&g_vin_n) == 0, "virtual inputs left after the release: %d", atomic_load(&g_vin_n));

	/* 5. analog move and look: values only, no ON/OFF events */
	T.on_calls = T.off_calls = 0;
	send("move 0.5 -1 400", 1);
	EXPECT(cmd_value(6) == 0.5f && cmd_value(3) == -1.0f, "move: strafe=%g forward=%g", cmd_value(6), cmd_value(3));
	EXPECT(cmd_ison(3) == 1 && cmd_ison(6) == 1, "move: held axes are not 'on' (%d,%d)", cmd_ison(3), cmd_ison(6));
	EXPECT(T.on_calls == 0, "move fired command ON events (%d)", T.on_calls);
	send("look 0.25 0 300", 1);
	EXPECT(cmd_value(13) == 0.25f && cmd_value(23) == 0.25f, "look: yaw axes %g/%g", cmd_value(13), cmd_value(23));
	EXPECT(cmd_ison(13) == 0, "look: a small yaw must not count as 'on'");
	send("release", 2);
	EXPECT(atomic_load(&g_vin_n) == 0, "release left %d virtual inputs", atomic_load(&g_vin_n));

	/* 6. the virtual pad: every command bound to that input */
	T.on_calls = 0;
	send("pad X RX=0.8 200", 1);
	EXPECT(cmd_ison(118) == 1 && T.on_calls == 1, "pad X: ison=%d ON=%d", cmd_ison(118), T.on_calls);
	EXPECT(cmd_value(13) == 0.8f && cmd_value(23) == 0.4f, "pad RX: axis values %g/%g (scale 1.0 and 0.5)", cmd_value(13),
	       cmd_value(23));
	send("release", 2);

	/* 7. the input mode */
	send("padmode on", 1);
	EXPECT(T.mode_calls == 1 && T.last_mode_pad == 1 && g_allow[1] == 0, "padmode on: calls=%d pad=%d allow_kbm=%u",
	       T.mode_calls, T.last_mode_pad, g_allow[1]);
	send("padmode off", 1);
	EXPECT(T.last_mode_pad == 0 && g_allow[1] == 1, "padmode off: pad=%d allow_kbm=%u", T.last_mode_pad, g_allow[1]);

	/* 8. dealhit: the nearest live orc, the direction set for the call and restored after it */
	memcpy((void *)(g_scombat + C_HITDIR), "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c", 12);
	*(uint8_t *)(g_scombat + C_HITONCE) = 0x55;
	float saved[3];
	memcpy(saved, (void *)(g_scombat + C_HITDIR), sizeof saved);
	send("dealhit auto", 3);
	EXPECT(T.hit_calls == 1, "dealhit: %d calls", T.hit_calls);
	EXPECT(T.hit_attacker == g_scombat, "dealhit attacker=%#lx want the player's server Combat %#lx",
	       (unsigned long)T.hit_attacker, (unsigned long)g_scombat);
	EXPECT(T.hit_target == g_orcobj[1], "dealhit target=%#lx want orc 1 (orc 0 is dead) %#lx", (unsigned long)T.hit_target,
	       (unsigned long)g_orcobj[1]);
	char an[128];
	EXPECT(T.hit_attack && !strcmp(rec_name(T.hit_attack, an, sizeof an), "PlayerLightSword1"), "dealhit attack '%s'",
	       rec_name(T.hit_attack, an, sizeof an));
	EXPECT(T.hit_kill == 0, "dealhit kill=%d", T.hit_kill);
	EXPECT(fabsf(T.hit_dir[0] - 1.0f) < 1e-5f && T.hit_dir[1] == 0.0f, "dealhit: direction at +0x15c during the call = %g,%g,%g",
	       T.hit_dir[0], T.hit_dir[1], T.hit_dir[2]);
	EXPECT(T.hit_27a == 0, "dealhit: +0x27a was %u during the call, want 0", T.hit_27a);
	EXPECT(!memcmp(saved, (void *)(g_scombat + C_HITDIR), sizeof saved), "dealhit: the hit direction was not restored");
	EXPECT(*(uint8_t *)(g_scombat + C_HITONCE) == 0x55, "dealhit: +0x27a was not restored");
	send("dealhit Orc_Club_Heavy kill", 3);
	EXPECT(T.hit_calls == 2 && T.hit_kill == 1 && !strcmp(rec_name(T.hit_attack, an, sizeof an), "Orc_Club_Heavy"),
	       "dealhit by name: calls=%d kill=%d attack='%s'", T.hit_calls, T.hit_kill, rec_name(T.hit_attack, an, sizeof an));
	send("dealhit Nope_Not_A_Record", 2);
	EXPECT(T.hit_calls == 2, "dealhit with an unknown record still called the game (%d)", T.hit_calls);

	/* 9. pieces: hide and show */
	send("hidehead", 2);
	EXPECT(T.hidden_n == 3, "hidehead hid %d pieces, want 3 (Head, Hair, Helmet)", T.hidden_n);
	send("showhead", 2);
	EXPECT(T.shown_n == 3, "showhead showed %d pieces", T.shown_n);
	T.hidden_n = T.shown_n = 0;
	send("hidehead body", 2);
	EXPECT(T.hidden_n == 1, "hidehead body: hid %d pieces, want 1 (an explicit substring must not pull in the "
			       "head candidates)", T.hidden_n);
	send("showhead", 2);
	T.hidden_n = T.shown_n = 0;
	send("hidehead SMC_HelmetModel", 2);
	EXPECT(T.hidden_n == 1 && T.hidden[0] == 4, "hidehead by exact name: %d pieces (first %u), want 1 (4)", T.hidden_n,
	       T.hidden_n ? T.hidden[0] : 99);
	send("showhead", 2);

	/* 10. pose sampling through the animation, camera-post and visibility hooks */
	send("pose 4", 1);
	for (int i = 0; i < 4; i++) {
		EXPECT(c_anim(g_body) == t_anim(g_body) + 10 - 1 + 1, "P_ANIM_A changed the caller's return value");
		c_campost(g_player);
		((vis_fn)g_vt[VT_VIS])((void *)0x9, (void *)g_cam, 1);
		tick(1);
	}
	EXPECT(g_pose.left == 0, "pose: %d samples left after 4 frames", g_pose.left);
	EXPECT(g_pose.stats_frames == 4, "pose: %u frames sampled", g_pose.stats_frames);
	EXPECT(g_pose.stats_eq_pre_post == 0, "pose: pre==post although the stand-in moved the node");

	/* 11. the first-person camera write */
	send("fpcam on fwd=10 up=5 pitch=0", 1);
	T.setcam_calls = 0;
	c_campost(g_player);
	EXPECT(T.setcam_calls == 1 && T.camreset_calls >= 1, "fpcam: setcam=%d camreset=%d", T.setcam_calls, T.camreset_calls);
	float hx[8];
	t_nodexf(0, g_charobj, 0x1000, hx, 1);
	EXPECT(fabsf(T.setcam_xf[1] - (hx[1] + 5.0f)) < 1e-3f && fabsf(T.setcam_xf[2] - (hx[2] + 10.0f)) < 1e-3f,
	       "fpcam: camera at %.2f,%.2f,%.2f, head %.2f,%.2f,%.2f + (0,5,10 forward)", T.setcam_xf[0], T.setcam_xf[1],
	       T.setcam_xf[2], hx[0], hx[1], hx[2]);
	EXPECT(fabsf(T.setcam_xf[6] - 1.0f) < 1e-4f, "fpcam: quaternion w=%g, want identity for heading 0 and pitch 0",
	       T.setcam_xf[6]);
	send("fpcam off", 1);
	T.setcam_calls = 0;
	c_campost(g_player);
	EXPECT(T.setcam_calls == 0, "fpcam off still wrote the camera");

	/* 12. autocounter on a parry window */
	send("autocounter CombatPrimary04 3", 1);
	T.on_calls = 0;
	*(uint8_t *)(g_orc[1] + C_WIN_PARRY) = 0;
	((void1_fn)g_vt[VT_SERVER])((void *)0x7); /* no window yet */
	tick(1);
	EXPECT(T.on_calls == 0, "autocounter pressed without a parry window");
	c_counter(g_orc[1], 250);                 /* the BPW key -> counter prompt */
	*(uint8_t *)(g_orc[1] + C_WIN_PARRY) = 1; /* the window is open */
	((void1_fn)g_vt[VT_SERVER])((void *)0x7);  /* the poll runs on every other server tick */
	((void1_fn)g_vt[VT_SERVER])((void *)0x7);
	tick(1);
	EXPECT(T.on_calls == 1 && T.last_on_cmd == 121, "autocounter: ON=%d cmd=%d", T.on_calls, T.last_on_cmd);
	EXPECT(o_counter_calls == 1, "the counter-prompt original ran %d times", o_counter_calls);
	send("autocounter off", 1);
	send("release", 2);

	/* 13. the near override */
	send("near 5", 1);
	EXPECT(rf(g_near_cell) == 5.0f, "near 5: %g", rf(g_near_cell));
	send("near off", 1);
	EXPECT(rf(g_near_cell) == 27.5f, "near off: %g (want the old 27.5)", rf(g_near_cell));

	/* 14. the pre-hooks pass arguments and return values through */
	o_atk_calls = 0;
	EXPECT(c_atk(0x11, 0x22, 0x33, 0x44, 1) == 0x1234 + 30, "P_ATK_NOPOS changed the caller's return value");
	EXPECT(o_atk_calls == 1 && o_atk_args[0] == 0x11 && o_atk_args[1] == 0x22 && o_atk_args[2] == 0x33 &&
		       o_atk_args[3] == 0x44 && o_atk_args[4] == 1,
	       "P_ATK_NOPOS arguments: %#lx %#lx %#lx %#lx %#lx", (unsigned long)o_atk_args[0], (unsigned long)o_atk_args[1],
	       (unsigned long)o_atk_args[2], (unsigned long)o_atk_args[3], (unsigned long)o_atk_args[4]);
	ltvec hp = { 1.5f, 2.5f, 3.5f };
	EXPECT(c_gamehit(hp, 0x55, 0x66, 0x77, 1) == 7 + 40, "P_DEALHIT changed the caller's return value");
	EXPECT(o_gamehit_pos[0] == 1.5f && o_gamehit_pos[1] == 2.5f && o_gamehit_pos[2] == 3.5f,
	       "P_DEALHIT xmm arguments: %g,%g,%g", o_gamehit_pos[0], o_gamehit_pos[1], o_gamehit_pos[2]);
	EXPECT(c_death(0x88, 0x99, 0xaa, 0) == 0x88 + 60 && o_death_calls == 1, "P_DEATH pass-through");
	EXPECT(c_counter(0x10, 5) == 0x10 + 5 + 50, "P_COUNTER pass-through");
	EXPECT(((vis_fn)g_vt[VT_VIS])((void *)1, (void *)2, 3) == 4, "P_VIS return value");
	EXPECT(((ui105_fn)g_vt[VT_UI105])((void *)1, 2, 3) == 5, "P_UI105 return value");
	EXPECT(((value_fn)g_vt[VT_VALUE2])((void *)1, (void *)2) == 8.0f, "P_VALUE2 pass-through");

	/* 15. a changed local player releases everything */
	send("cmd CombatPrimary01 ms=5000;hidehead", 2);
	EXPECT(atomic_load(&g_vin_n) == 1 && T.hidden_n > 0, "setup for the reset test: vin=%d hidden=%d",
	       atomic_load(&g_vin_n), T.hidden_n);
	uintptr_t keep = g_player;
	g_player = (uintptr_t)zalloc(0x1000); /* as a reload/respawn would: a new Player object */
	put(g_player, P_PHOTO, rq(keep + P_PHOTO));
	put(g_player, P_CAM, g_cam);
	put(g_player, P_BODY, g_body);
	put(g_player, P_COMBAT, g_pc);
	*(uint8_t *)(g_player + P_LIFE) = 2;
	tick(2);
	EXPECT(atomic_load(&g_vin_n) == 0, "the player change left %d virtual inputs held", atomic_load(&g_vin_n));
	T.shown_n = 0;
	send("showhead", 2);
	EXPECT(T.shown_n == 0, "showhead unhid %d pieces on a stale object", T.shown_n);
	g_player = keep;
	tick(2);

	/* 16. exec: the exact string reaches the engine's slot 32; the escape and the expansions; the refusals */
	int runs0 = T.run_calls;
	send("exec Combat.Debug.Attack 1", 2);
	EXPECT(T.run_calls == runs0 + 1 && T.run_eng == (void *)g_eng && !strcmp(T.run_s, "Combat.Debug.Attack 1"),
	       "exec: runs=%d eng=%p string '%s'", T.run_calls - runs0, T.run_eng, T.run_s);
	EXPECT(*(uint8_t *)(g_var + 0x38) == 1, "exec: the stand-in bool was not set");
	EXPECT(T.find_calls >= 1, "exec: no console find before the call");
	float r0[3], r1[3];
	memcpy(r0, (void *)(g_charobj + OBJ_POS), 12);
	char want[1200];
	snprintf(want, sizeof want, "Combat.Debug.Attack 0; Teleport %.2f %.2f %.2f", r0[0] + 100.0f, r0[1], r0[2] - 50.0f);
	send("exec Combat.Debug.Attack 0\\; Teleport @x+100 @y @z-50;mark after-exec", 4);
	EXPECT(T.run_calls == runs0 + 2 && !strcmp(T.run_s, want), "exec: escaped/expanded string '%s', want '%s'", T.run_s, want);
	EXPECT(*(uint8_t *)(g_var + 0x38) == 0, "exec: the second command of one string did not run");
	memcpy(r1, (void *)(g_charobj + OBJ_POS), 12);
	EXPECT(r1[0] == r0[0] + 100.0f && r1[2] == r0[2] - 50.0f, "exec: the stand-in teleport did not move the root");
	EXPECT(g_rw.n == 3 && g_rw.left == 0, "exec: root watch logged %d ticks (left %d), want 3", g_rw.n, g_rw.left);
	send("exec Faction.Dump @out/faction.txt", 1);
	snprintf(want, sizeof want, "Faction.Dump %s/faction.txt", outd);
	EXPECT(T.run_calls == runs0 + 3 && !strcmp(T.run_s, want), "exec: @out gave '%s', want '%s'", T.run_s, want);
	char longl[600] = "exec Combat.Debug.Attack ";
	memset(longl + strlen(longl), '1', 420);
	send(longl, 1);
	EXPECT(T.run_calls == runs0 + 3, "exec: an overlong line ran");
	void *keep_slot = g_engvt[32];
	g_engvt[32] = (void *)t_find; /* a slot that no longer holds the checked function */
	send("exec Combat.Debug.Attack 1", 1);
	g_engvt[32] = keep_slot;
	put(g_eng, 0, (uintptr_t)&g_engvt[1]); /* an engine whose vtable is not the checked one */
	send("exec Combat.Debug.Attack 1", 1);
	put(g_eng, 0, (uintptr_t)g_engvt);
	put(g_eng_cell, 0, 0);
	send("exec Combat.Debug.Attack 1", 1);
	put(g_eng_cell, 0, g_eng);
	EXPECT(T.run_calls == runs0 + 3 && *(uint8_t *)(g_var + 0x38) == 0, "exec ran on a mismatched slot, vtable or null engine");
	put(g_guard_cell, 0, 0x919970); /* the guard gone: both active words refused */
	g_guard_ok = -1;
	tick(1);
	EXPECT(g_guard_ok == 0, "guard check after the unpatch: %d", g_guard_ok);
	send("exec Combat.Debug.Attack 1", 1);
	send("pmmove 10 0 0", 1);
	EXPECT(T.run_calls == runs0 + 3 && !*(uint8_t *)(g_pm + PM_PENDING) && T.pm_consumed == 0,
	       "exec/pmmove ran without the save guard");
	put(g_guard_cell, 0, 0x12340000);
	g_guard_ok = -1;
	tick(1);
	send("exec Combat.Debug.Attack 1", 1);
	EXPECT(T.run_calls == runs0 + 4, "exec refused again after the guard came back");

	/* 17. pmcount through the P_PMROOT slot; pmmove writes only +0x428..+0x433 and +0x444, once */
	send("pmcount 0.1", 5); /* the word runs in the first tick: ticks 2-5 and the one after the sleep count */
	struct timespec ms150 = { 0, 150 * 1000 * 1000 };
	nanosleep(&ms150, NULL);
	tick(1);
	EXPECT(!g_pmc.on && g_pmc.frames == 5 && g_pmc.hist[1] == 5 && g_pmc.calls == 5 && g_pmc.pm_seen == g_pm,
	       "pmcount: on=%d ticks=%llu calls=%llu hist1=%llu pm=%#lx", g_pmc.on, (unsigned long long)g_pmc.frames,
	       (unsigned long long)g_pmc.calls, (unsigned long long)g_pmc.hist[1], (unsigned long)g_pmc.pm_seen);
	static const float yaw90[4] = { 0, 0.70710678f, 0, 0.70710678f }; /* +90 deg about +Y: local +X -> world -Z */
	memcpy((void *)(g_charobj + OBJ_ROT), yaw90, 16);
	memcpy(r0, (void *)(g_charobj + OBJ_POS), 12);
	static unsigned char snap[0x460];
	memcpy(snap, (void *)g_pm, sizeof snap);
	g_pm_run = 0; /* the word's own tick without the movement: the bytes it wrote, before vf_11 consumes them */
	send("pmmove 10 0 0", 1);
	int other = 0;
	for (size_t i = 0; i < sizeof snap; i++)
		if (((unsigned char *)g_pm)[i] != snap[i] && !(i >= PM_XPOS && i < PM_XPOS + 12) && i != PM_PENDING)
			other++;
	float xp[3];
	memcpy(xp, (void *)(g_pm + PM_XPOS), 12);
	EXPECT(!other && xp[0] == 10.0f && xp[1] == 0 && xp[2] == 0 && *(uint8_t *)(g_pm + PM_PENDING) == 1,
	       "pmmove: %d bytes outside the fields changed; +0x428=(%g %g %g) pending=%u", other, xp[0], xp[1], xp[2],
	       *(uint8_t *)(g_pm + PM_PENDING));
	g_pm_run = 1;
	tick(4);
	memcpy(r1, (void *)(g_charobj + OBJ_POS), 12);
	EXPECT(T.pm_consumed == 1 && !*(uint8_t *)(g_pm + PM_PENDING), "pmmove: consumed %d times, pending %u", T.pm_consumed,
	       *(uint8_t *)(g_pm + PM_PENDING));
	EXPECT(fabsf(r1[0] - r0[0]) < 1e-3f && fabsf(r1[2] - (r0[2] - 10.0f)) < 1e-3f,
	       "pmmove: the root moved (%g %g %g), want (0 0 -10) for 10 cm right at yaw 90", r1[0] - r0[0], r1[1] - r0[1],
	       r1[2] - r0[2]);
	EXPECT(atomic_load(&g_pm_consumed) != 0 && atomic_load(&g_pm_armed) == 0 && g_rw.n == 3,
	       "pmmove: the hook did not see the consuming call (consumed=%llu armed=%#lx watch=%d)",
	       (unsigned long long)atomic_load(&g_pm_consumed), (unsigned long)atomic_load(&g_pm_armed), g_rw.n);
	*(uint8_t *)(g_pm + PM_PENDING) = 1; /* a move still pending: refused, nothing written */
	send("pmmove 0 0 10", 1);
	memcpy(xp, (void *)(g_pm + PM_XPOS), 12);
	EXPECT(xp[2] == 0 && T.pm_consumed == 2, "pmmove wrote over a pending move (+0x430=%g)", xp[2]);
	memcpy((void *)(g_charobj + OBJ_ROT), (const float[4]){ 0, 0, 0, 1 }, 16);

	/* 19 (P7). timers, pick, stimlog, swingevent, aiwatch, keytimes, hitstop, projectiles, FX, weapons, dagger, the
	 * database dumps, the HUD dumps, sounds, @cout and @ox (docs/runtime-probes.md P7) */
	g_swing_window = 0.5;
	send("timers", 1);
	EXPECT(log_has("timers: 2 live orcs: simulation 1, default 1, game 0, the player's 0, own 0"), "timers: the classes");
	EXPECT(log_has("UI formats: UI surface dev+0xa0=28 3D surface dev+0xe8=28"), "ui: the surface formats");
	send("pick", 1);
	EXPECT(g_pick.c == g_orc[1] && g_pick.hobj == g_orcobj[1], "pick: %#lx, want orc 1 (orc 0 is dead)", (unsigned long)g_pick.c);
	send("pick NoSuchOrc", 1); /* refused, the old pick forgotten */
	EXPECT(!g_pick.c, "pick NoSuchOrc kept a target");
	send("pick Grunt", 1);
	EXPECT(g_pick.c == g_orc[1], "pick Grunt: %#lx", (unsigned long)g_pick.c);

	/* stimlog: the three callers pass their arguments and results through; both registrations logged with the caller */
	send("stimlog on", 1);
	EXPECT(c_stim_start(g_orc[1], 0xad1, g_phobj) == 1 && c_stim_key(g_orc[2], 0xad2, g_phobj) == 2 &&
		       c_stim_stop(g_orc[1], 0xad3, 0) == 3, "P_STIM_*: a caller's return value changed");
	EXPECT(U.stim_calls == 3 && U.stim_ad == 0xad3 && U.reg_calls == 3 && U.regp_calls == 3,
	       "stim: calls %d ad %#lx reg %d/%d", U.stim_calls, (unsigned long)U.stim_ad, U.reg_calls, U.regp_calls);
	EXPECT(log_has("STIMREG list rc=7 stim='AI/StimulusType/Visual_Attack'") && log_has("STIMREG persist rc=42") &&
		       log_has("caller=key"), "stimlog: the STIMREG lines");

	/* swingevent with cur: FUN_026c7180(pc, AttackData, the orc), the swing in pc+0x4d0/+0x55c/+0x4c0, the persistent
	 * stimulus removed at the end, the target's AI counted (aiwatch: a context check logged on changes only) */
	put32(g_scombat, C_STIMID, 0);
	put(g_scombat, C_ATTACK, 0);
	put(g_scombat, C_TARGET, 0xbeef);
	*(uint8_t *)(g_scombat + C_VARIATION) = 3;
	U.stim_calls = 0;
	send("aiwatch on;swingevent PlayerLightSword1 200 cur", 1);
	srv_tick();
	EXPECT(U.stim_calls == 1 && U.stim_ad == g_ad1 && U.stim_tgt == g_orcobj[1], "swingevent: stim calls %d ad %#lx tgt %#lx",
	       U.stim_calls, (unsigned long)U.stim_ad, (unsigned long)U.stim_tgt);
	EXPECT(rq(g_scombat + C_ATTACK) == g_atk[1] && rq(g_scombat + C_TARGET) == g_orcobj[1] && rb(g_scombat + C_VARIATION) == 0,
	       "swingevent cur: pc+0x4d0 %#lx +0x4c0 %#lx +0x55c %u", (unsigned long)rq(g_scombat + C_ATTACK),
	       (unsigned long)rq(g_scombat + C_TARGET), rb(g_scombat + C_VARIATION));
	EXPECT(log_has("caller=probe") && g_swing.id_ours == 42, "swingevent: the REGSTIM lines say caller=probe, id %u",
	       g_swing.id_ours);
	send("swingevent auto", 1); /* refused: the first swing is still on */
	srv_tick();
	uintptr_t act = (uintptr_t)zalloc(0x20);
	put(act, 8, rq(g_orc[1] + C_OWNER));
	U.ai_ret = 0;
	EXPECT(((six_fn)g_vt[VT_AI0 + 1])(act, 0x11, 0x22, 0, 0, 0) == 0 && U.ai_args[0] == act && U.ai_args[1] == 0x11 &&
		       U.ai_args[2] == 0x22, "P_AI_BLOCK_OK: arguments or result changed");
	U.ai_ret = 1;
	((six_fn)g_vt[VT_AI0 + 1])(act, 0, 0, 0, 0, 0);
	EXPECT(((six_fn)g_vt[VT_AI0 + 1])(act, 0, 0, 0, 0, 0) == 1, "P_AI_BLOCK_OK: result 1 not passed");
	((six_fn)g_vt[VT_AI0 + 2])(act, 0, 0, 0, 0, 0);
	EXPECT(log_has("AI block_valid") && log_has("result=1 (was 0)") && log_has("AI block_go"), "aiwatch: the AI lines");
	msleep(250);
	tick(1);
	EXPECT(rq(g_scombat + C_ATTACK) == 0 && rq(g_scombat + C_TARGET) == 0xbeef && rb(g_scombat + C_VARIATION) == 3,
	       "swingevent end: pc+0x4d0 %#lx +0x4c0 %#lx +0x55c %u not restored", (unsigned long)rq(g_scombat + C_ATTACK),
	       (unsigned long)rq(g_scombat + C_TARGET), rb(g_scombat + C_VARIATION));
	EXPECT(r32(g_scombat + C_STIMID) == 0 && U.rm_calls == 1 && U.rm_id == 42, "swingevent end: persistent stimulus %u, "
	       "removes %d (id %d)", r32(g_scombat + C_STIMID), U.rm_calls, U.rm_id);
	msleep(300);
	tick(1);
	EXPECT(!g_swing.on && log_has("goal_counter=0 block_valid=2 block_go=1 dodge_valid=0 dodge_go=0"), "swingevent: the result");
	send("swingevent auto 100", 1);
	srv_tick();
	EXPECT(log_has("auto: the first PC_ record with stimuli 'PC_HC_Low_L1_D0_F_1'"), "swingevent auto: the record");
	msleep(600);
	tick(2);
	send("stimlog off;aiwatch off", 1);

	/* keytimes: queued at the start, sampled on the next tick, measured at the attacker's APPLYDAMAGE */
	send("keytimes on", 1);
	put32(g_orc[1], C_ANIM, 5);
	uint16_t kt[10] = { 0xffff, 120, 400, 0xffff, 0xffff, 0xffff, 250, 330, 0xffff, 0xffff };
	memcpy((void *)(g_orc[1] + C_KT), kt, sizeof kt);
	EXPECT(c_keytimes(g_orc[1] + C_WS, g_orcobj[1], g_atk[0]) == 0x56, "P_KEYTIMES: the return value changed");
	tick(1);
	EXPECT(log_has("APPLYDAMAGE=400 STARTFACING") && log_has("KeyLookup APPLYDAMAGE=400 BPW=250 EPW=- BCOL=- ECOL=-") &&
		       log_has("impact in 0.300 s") && log_has("(SampleNode, model) 10.0,150.0,30.0"), "keytimes: the KEYTIMES line");
	EXPECT(c_applydmg(g_orc[1], g_atk[0], g_phobj) == 0x67, "P_APPLYDMG: the return value changed");
	EXPECT(log_has("KEYHIT c=") && log_has("err=0.0 cm"), "keytimes: the KEYHIT line (predicted hand = live hand)");
	send("keytimes off", 1);

	/* hitstop (timer): the orc gets its own timer (num 0, the clock copied), then the old one back, ours released */
	send("hitstop 80", 1);
	srv_tick();
	uintptr_t mine = rq(g_orcobj[1] + OBJ_TIMER);
	EXPECT(mine && mine != g_simt && r32(mine + TM_NUM) == 0 && rf(mine + TM_SCALE) == 0.0f && r32(mine + TM_TOTAL) == 12345 &&
		       U.tm_created == 1, "hitstop: the orc's timer %#lx num %u total %u", (unsigned long)mine,
	       mine ? r32(mine + TM_NUM) : 0, mine ? r32(mine + TM_TOTAL) : 0);
	send("hitstop 50", 1); /* refused: the hold is on */
	srv_tick();
	msleep(120);
	tick(1);
	EXPECT(rq(g_orcobj[1] + OBJ_TIMER) == g_simt && U.tm_deleted == 1 && r32(g_simt + TM_REF) == 5 && !g_hs.on,
	       "hitstop end: timer %#lx (want the simulation timer), deleted %d, sim ref %u", (unsigned long)rq(g_orcobj[1] + OBJ_TIMER),
	       U.tm_deleted, r32(g_simt + TM_REF));
	EXPECT(log_has("its timer is back"), "hitstop: the end line");
	send("hitstop 50 rate", 1);
	srv_tick();
	EXPECT(U.rates_set == 2 && U.rate_set[0] == 0.0f && U.rate_set[1] == 0.0f, "hitstop rate: %d rates set", U.rates_set);
	msleep(80);
	tick(1);
	EXPECT(U.rates_set == 4 && U.rate_set[0] == 1.0f && U.rate_set[1] == 0.5f, "hitstop rate: restored %g/%g",
	       (double)U.rate_set[0], (double)U.rate_set[1]);

	/* projectiles: logged, one blocked (removed by its own update), one turned back at its shooter */
	float cp[3], back[4] = { 0, 1, 0, 0 }; /* yaw 180: flies along -Z */
	memcpy(cp, (void *)(g_charobj + OBJ_POS), 12);
	float p1pos[3] = { cp[0], cp[1] + 40.0f, cp[2] + 500.0f };
	uintptr_t p1 = mk_proj(p1pos, back, g_orc[2]);
	send("projlog on", 1);
	EXPECT(((six_fn)g_vt[VT_PJ])(p1, 0, 0, 0, 0, 0) == 0 && U.pj_updates == 1, "P_PJ_UPDATE: the update did not pass");
	EXPECT(log_has("PROJ new p=") && log_has("homing=1"), "projlog: the new line");
	send("projblock 1.5", 1);
	putf(p1, PJ_POS + 8, cp[2] + 100.0f);
	EXPECT(((six_fn)g_vt[VT_PJ])(p1, 0, 0, 0, 0, 0) == 1 && U.pj_removed == 1, "projblock: not removed (%d)", U.pj_removed);
	send("projreflect 2 own", 1);
	float p2pos[3] = { cp[0], cp[1] + 40.0f, cp[2] + 100.0f };
	uintptr_t p2 = mk_proj(p2pos, back, g_orc[2]);
	((six_fn)g_vt[VT_PJ])(p2, 0, 0, 0, 0, 0);
	float q2[4], d2[3], fz[3] = { 0, 0, 1 }, sp[3], wd[3];
	memcpy(q2, (void *)(p2 + PJ_ROT), 16);
	qrot(q2, fz, d2);
	obj_pos(g_orcobj[2], sp);
	for (int k = 0; k < 3; k++)
		wd[k] = sp[k] + (k == 1 ? 120.0f : 0.0f) - p2pos[k];
	float wl = sqrtf(wd[0] * wd[0] + wd[1] * wd[1] + wd[2] * wd[2]);
	EXPECT(fabsf(d2[0] - wd[0] / wl) < 1e-3f && fabsf(d2[1] - wd[1] / wl) < 1e-3f && fabsf(d2[2] - wd[2] / wl) < 1e-3f,
	       "projreflect: flies %.3f,%.3f,%.3f, want %.3f,%.3f,%.3f (at the shooter)", d2[0], d2[1], d2[2], wd[0] / wl,
	       wd[1] / wl, wd[2] / wl);
	EXPECT(rq(p2 + PJ_SHOOTER) == g_scombat && !rb(p2 + PJ_HOMING) && !rb(p2 + PJ_SPLINE) && !rq(p2 + PJ_TARGET),
	       "projreflect: shooter/homing/spline/target not rewritten");
	uintptr_t hit = (uintptr_t)zalloc(0x40);
	put(hit, 0x20, g_phobj);
	EXPECT(c_pjdamage(g_orc[2], hit, g_atk[0], 0x99, 5, 6, 1, 0) == 0x78 && U.pjd_args[1] == hit && U.pjd_args[5] == 6 &&
		       U.pjd_args[6] == 1 && U.pjd_args[7] == 0, "P_PJ_DAMAGE: arguments (two on the stack) or result changed");
	EXPECT(log_has("PROJHIT shooter=") && log_has("THE PLAYER") && log_has("PROJWATCH"), "projlog: the hit lines");
	send("projlog off", 1);

	/* fxlog and weaponfx: the name seen on a weapon, the recipe one-shot and looping, the stop */
	send("fxlog on", 1);
	char csb[0x200];
	EXPECT(c_fxinit(csb, "fx/sword_glow.fxf", 0x7770, 0, g_itemobj[0]) == 9 && U.fx_inits == 1, "P_FXI: the call did not pass");
	EXPECT(log_has("FX site=P_FXI_A07FE1 name='fx/sword_glow.fxf'") && log_has("(a player weapon)"), "fxlog: the FX line");
	send("weaponfx last", 1);
	srv_tick();
	EXPECT(U.fx_inits == 2 && !strcmp(U.fx_name, "fx/sword_glow.fxf") && U.fx_parent == g_itemobj[0] && U.fx_flags == 0 &&
		       U.fx_creates == 1 && !U.fx_out && U.fx_frees == 1, "weaponfx: init %d '%s' parent %#lx creates %d", U.fx_inits,
	       U.fx_name, (unsigned long)U.fx_parent, U.fx_creates);
	send("weaponfx last loop", 1);
	srv_tick();
	EXPECT(U.fx_flags == 1 && U.fx_out == &g_wfx_handle && g_wfx_handle, "weaponfx loop: flags %u handle %#lx", U.fx_flags,
	       (unsigned long)g_wfx_handle);
	send("weaponfx stop", 1);
	srv_tick();
	EXPECT(!g_wfx_handle && U.fx_releases == 1, "weaponfx stop: handle %#lx releases %d", (unsigned long)g_wfx_handle,
	       U.fx_releases);
	send("fxlog off", 1);
	snprintf(p, sizeof p, "%s/fxnames-%u.tsv", outd, g_seq_sent);
	EXPECT(log_has("FXNAMES 1 distinct FX names") && access(p, R_OK) == 0, "fxlog off: the name table (%s)", p);

	/* weapons and the dagger: to L_Weapon through the re-attach, shown, and back */
	send("weapons", 1);
	EXPECT(log_has("weapons: 2 items, active slot 0, dagger slot 1") && log_has("inactive_node=0x1001 'Neck'"),
	       "weapons: the slots");
	uintptr_t den = rq(g_item[1] + IT_ENTRIES);
	send("dagger hand", 1);
	srv_tick();
	EXPECT(r32(den + IE_INACTNODE) == name_hash("L_Weapon") && rb(g_item[1] + IT_SHOWN) == 1 && U.reattach == 1 &&
		       U.itemvis == 1 && log_has("'L_Weapon'") && log_has("0.0 cm apart"), "dagger hand: node %#x shown %u reattach %d",
	       r32(den + IE_INACTNODE), rb(g_item[1] + IT_SHOWN), U.reattach);
	send("dagger back", 1);
	srv_tick();
	EXPECT(r32(den + IE_INACTNODE) == 0x1001 && rb(g_item[1] + IT_SHOWN) == 0 && U.reattach == 2 && !g_dag.on,
	       "dagger back: node %#x shown %u", r32(den + IE_INACTNODE), rb(g_item[1] + IT_SHOWN));

	/* the database: syncinfo (a record and a prefix; direct and indirect records) and attacks full */
	send("syncinfo PlayerLightSword1 PC_HC_*", 1);
	EXPECT(log_has("SYNC 'PlayerLightSword1' 1 TargetAttack = 'AI_Bip_PC_Sneak_IK_1'") &&
		       log_has("2 AttachSocket = \"SocketHead\"") && log_has("3 AlignAttachSockets = 1") &&
		       log_has("4 KillTargetDamage = 100"), "syncinfo: the Sync fields");
	EXPECT(log_has("RegisterStimulus='AI/StimulusType/Visual_Attack','AI/StimulusType/Tactile'") &&
		       log_has("AOEOnHit='Atk_C_BrutalK_3xMorale_Rune'") && log_has("syncinfo: 2 records, 1 with AnimationData.Sync"),
	       "syncinfo: the attack's fields");
	send("attacks full", 1);
	snprintf(p, sizeof p, "%s/attacks-full.tsv", outd);
	EXPECT(log_has("1 with AnimationData.Sync, 2 with stimuli (1 of them PC_), 0 IsBlock") && access(p, R_OK) == 0,
	       "attacks full: the summary or %s", p);

	/* the HUD: nav markers, prompts (and their watch), the UI scene */
	send("navdump", 1);
	EXPECT(log_has("navdump: 2 markers (1 shown this frame, 1 positions asked)") && U.nav_pos == 1 &&
		       log_has("clip=\"Icon_Quest\"") && log_has("action=34 Activate") && log_has("offscreen=1"), "navdump");
	send("prompts", 1);
	EXPECT(log_has("PROMPT standard slot=0 pad action=34 Activate key='Prompt_Activate' string='PROMPT_DRAIN' icon='Icon_B' "
		       "category=2") && log_has("prompts: 1 live entries"), "prompts: the entry");
	send("prompts watch on", 2);
	put32(g_prompt_entry, 0x28, 35);
	tick(1);
	EXPECT(log_has("PROMPTS changed: 1 live") && log_has("action=35 Abort"), "prompts watch: the change");
	send("prompts watch off;uiscene", 1);
	EXPECT(c_uisrender(0x4242, g_uiview) == 4 && U.uis_calls == 1 && log_has("UISCENEVIEW renderer=0x4242") &&
		       log_has("fovY=60.00 deg") && log_has("UISCENE mgr="), "uiscene");

	/* sounds: the game's posts logged (arguments on the stack untouched), ours global and at the hand */
	send("soundlog on", 1);
	uint32_t pid = t_sndpost_cb(G.snd_wrap, 5, "ui_game_back");
	EXPECT(pid == 0x1000 + (uint32_t)U.ak_calls + 1 && U.ak_args[2] == 5 && U.ak_args[3] == 0x8c8ba0 && U.ak_args[6] == 0 &&
		       U.ak_args[7] == 0, "P_SND_POST_CB: arguments or result changed");
	EXPECT(log_has("SOUND post+callback event='ui_game_back'"), "soundlog: the SOUND line");
	send("sound last", 1);
	EXPECT(U.snd_global == 1 && !strcmp(U.snd_name, "ui_game_back") && log_has("(the probe's)") &&
		       log_has("sound 'ui_game_back' global"), "sound: the global post");
	send("sound last hand", 1);
	EXPECT(U.snd_reg == 1 && U.snd_pos == 1 && U.snd_obj == SND_OBJ, "sound hand: reg %d pos %d obj %#x", U.snd_reg, U.snd_pos,
	       U.snd_obj);
	send("soundlog off", 1);
	EXPECT(log_has("SOUNDNAMES 1 distinct event names, last 'ui_game_back'"), "soundlog off: the table");

	/* exec: @cout = C:\Temp (the stand-in game writes the file into the VFS folder; the probe finds it and copies it)
	 * and @ox/@oy/@oz = the picked orc */
	send("exec Faction.Dump @cout\\somvr-faction.txt", 1);
	snprintf(p, sizeof p, "%s/somvr-faction.txt", outd);
	EXPECT(!strcmp(T.run_s, "Faction.Dump C:\\Temp\\somvr-faction.txt") && access(p, R_OK) == 0 && log_has("copied to"),
	       "exec @cout: string '%s', copy %s", T.run_s, p);
	float op1[3];
	obj_pos(g_orcobj[1], op1);
	send("exec Teleport @ox+300 @oy+50 @oz", 2);
	float np[3];
	memcpy(np, (void *)(g_charobj + OBJ_POS), 12);
	EXPECT(fabsf(np[0] - (op1[0] + 300)) < 0.01f && fabsf(np[1] - (op1[1] + 50)) < 0.01f && fabsf(np[2] - op1[2]) < 0.01f,
	       "exec @ox: at %.2f,%.2f,%.2f, want the orc + (300, 50, 0)", np[0], np[1], np[2]);

	/* res and restall (P_RES3D passes the ScreenHeight cvar only while on), piecehide (one at a time), trav and fsm= */
	send("res", 1);
	EXPECT(log_has("RES cvars ScreenWidth=1832 ScreenHeight=1920") && log_has("3D 3664x1920"), "res: the sizes");
	EXPECT(c_res3d(0x10, 0x20, 1832, 1031, 1, 0) == 0x32 && g_res3d_args[3] == 1031 && g_res3d_args[0] == 0x10 &&
		       g_res3d_args[4] == 1, "P_RES3D off: the height %llu", (unsigned long long)g_res3d_args[3]);
	send("restall on", 1);
	c_res3d(0x10, 0x20, 1832, 1031, 1, 0);
	EXPECT(g_res3d_args[3] == 1920 && g_res3d_args[2] == 1832, "restall on: the height %llu (want the cvar 1920)",
	       (unsigned long long)g_res3d_args[3]);
	send("restall off;res", 1);
	c_res3d(0x10, 0x20, 1832, 1031, 1, 0);
	EXPECT(g_res3d_args[3] == 1031 && log_has("P_RES3D 2 calls (last height 1031 -> 1920)"), "restall off: %llu",
	       (unsigned long long)g_res3d_args[3]);
	T.hidden_n = T.shown_n = 0;
	send("piecehide 2", 1);
	send("piecehide 3", 1);
	EXPECT(T.hidden_n == 2 && T.hidden[0] == 2 && T.hidden[1] == 3 && T.shown_n == 1, "piecehide: hidden %d (%u,%u) shown %d",
	       T.hidden_n, T.hidden[0], T.hidden[1], T.shown_n);
	send("piecehide off", 1);
	EXPECT(T.shown_n == 2 && !g_ph.on && log_has("piecehide 3 of 5 ('SMC_BodyModel')"), "piecehide off: shown %d", T.shown_n);
	send("trav 0.5", 1);
	*(uint8_t *)(g_player + P_FSM) = 8;
	tick(2);
	EXPECT(log_has("TRAV fsm=8/0") && log_has("supported=1") && log_has("fsm=8/0"), "trav / STATE fsm: the lines");
	*(uint8_t *)(g_player + P_FSM) = 0;
	msleep(550);
	tick(2);
	EXPECT(!g_trav.on && log_has("TRAV seq="), "trav: the end");

	/* 20 (P8). attached/attachhide, piecehide all, wnodes, ikwatch/ikcb, armpose, aggro/minhealth, the UI words, surf,
	 * grav, mount, conlog and exec's output, hitstop onbpw, the keytimes window, trav's head, the dagger's later measure
	 * (docs/runtime-probes.md P8) */
	send("attached", 1);
	EXPECT(log_has("ATTACH 0 child=") && log_has("pieces=3") && log_has("ATTACH 1 child=") && log_has("ATTACH 2 child=") &&
		       log_has("(a player weapon)") && log_has("'R_Weapon'") && log_has("attached: 3 entries (3 on the player model"),
	       "attached: the entries");
	EXPECT(log_has("renderable 0x") && log_has("node 0x"), "attached: the mask chain");
	T.child_hidden = 0;
	send("attachhide 0", 1);
	EXPECT(T.child_hidden == 3 && (r32(g_childsm + SM_MASK) & 7) == 7 && g_ah.on, "attachhide 0: %d pieces hidden, mask %#x",
	       T.child_hidden, r32(g_childsm + SM_MASK));
	send("attachhide 1", 1); /* no model: refused, the last one shown again first */
	EXPECT(T.child_hidden == 0 && !g_ah.on, "attachhide 1: the child 0 pieces not shown again (%d)", T.child_hidden);
	send("attachhide 0 node", 3);
	uintptr_t crn = rq(rq(g_childsm + SM_REND) + REND_HANDLE) + HANDLE_NODE;
	EXPECT(rb(crn + RNODE_SKIP) == 1 && log_has("PIECEMASK seq="), "attachhide node: the skip byte %u", rb(crn + RNODE_SKIP));
	send("attachhide off", 1);
	EXPECT(rb(crn + RNODE_SKIP) == 0 && !g_ah.on, "attachhide off: the skip byte %u", rb(crn + RNODE_SKIP));
	T.hidden_n = T.shown_n = 0;
	put32(g_model, SM_MASK, 0); /* every piece shown (the earlier hidehead before the player change left two hidden) */
	send("piecehide all", 3);
	EXPECT(T.hidden_n == 5 && log_has("piecehide all: 5 of 5 pieces hidden") && log_has("PIECEMASK seq="), "piecehide all: %d",
	       T.hidden_n);
	send("piecehide off", 1);
	EXPECT(T.shown_n == 5 && (r32(g_model + SM_MASK) & 0x1f) == 0, "piecehide off after all: %d shown, mask %#x", T.shown_n,
	       r32(g_model + SM_MASK));

	send("wnodes", 1);
	snprintf(p, sizeof p, "%s/wnodes-%u.tsv", outd, g_seq_sent);
	EXPECT(access(p, R_OK) == 0 && log_has("WNODE slot=0 entry=0 2 parent=1 'Blade_Tip'") && log_has("WSOCKET slot=0") &&
		       log_has("WATTACH slot=0 entry=0 = the player's attach entry 2") && log_has("'Blade_Tip' 60.0 cm"),
	       "wnodes: the sword's nodes, socket, attach entry (%s)", p);

	/* ikwatch / ikcb: the chain setup and update hooks pass through; Talion's chain logged, another's not */
	uintptr_t pmgr = rq(g_body + BODY_MGR), chain1 = pmgr + MGR_CHAINS + CHAIN_SIZE, other_ch = (uintptr_t)zalloc(0x200);
	send("ikwatch 1;ikcb 1", 1);
	EXPECT(c_iksetup(chain1, g_charobj, g_tgtrec) == 0x5b && c_iksetup2(other_ch, 0x777, 0) == 0x5c && V.iksetup == 2,
	       "P_IK_SETUP_A/B: the result or the call changed");
	EXPECT(log_has("IKSETUP IK::vf_07 slot=1") && log_has("root 0x") && log_has("'R_Armu'") && !log_has("obj=0x777"),
	       "ikwatch: the setup line");
	put32(chain1, 0x10c, 2);
	putf(chain1, 0x108, 1.0f);
	for (int i = 0; i < 10; i++)
		EXPECT(c_ikchain(chain1) == (chain1 & 0xff) + 3, "P_IK_CHAIN: the result changed");
	EXPECT(V.ikchain == 10 && log_has("IKCHAIN slot=1 update 10: state 2 weight 1.000 target world (11.0 22.0 33.0)"),
	       "ikwatch: the chain update line");
	EXPECT(((uint64_t(*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t))g_vt[VT_ADDCB])(
		       0x31, g_charobj, name_hash("R_Hand"), 0xc, 0, chain1, 0x110) == 0x61 && V.addcb_args[6] == 0x110 &&
		       V.addcb_args[2] == name_hash("R_Hand") && V.addcb_args[5] == chain1,
	       "P_IEM_ADDCB: the arguments (the seventh on the stack) or the result changed: size %#llx",
	       (unsigned long long)V.addcb_args[6]);
	EXPECT(((six_fn)g_vt[VT_REMCB])(0x31, g_charobj, name_hash("R_Hand"), 0xc, 0, chain1) == 0x62 && V.remcb == 1,
	       "P_IEM_REMCB: pass-through");
	EXPECT(log_has("IKCB add obj=") && log_has("type=0xc") && log_has("'R_Hand'") && log_has("IKCB remove"), "ikcb: the lines");
	msleep(600);
	tick(2);
	EXPECT(log_has("IKWATCH model cm: IK_LHand") && log_has("[1 use="), "ikwatch: the node line");
	msleep(600);
	tick(2);
	EXPECT(!g_ikw.on && !g_ikw.cb && log_has("IKWATCH seq=") && log_has("IKCB seq="), "ikwatch/ikcb: the end lines");

	/* armpose ik: R_Hand to the target, the bone lengths kept; the next update reads it; the sword follows */
	float S0[3], E0[3], W0[3];
	memcpy(S0, g_base_pose[5], 12), memcpy(E0, g_base_pose[6], 12), memcpy(W0, g_base_pose[7], 12);
	send("armpose ik 30 30 35", 1);
	EXPECT(g_arm.on && g_arm.iS == 5 && g_arm.iE == 6 && g_arm.iW == 7 && g_arm.sword_entry == g_entries8 + 2 * AT_ESIZE,
	       "armpose: resolved S %d E %d W %d sword entry %#lx", g_arm.iS, g_arm.iE, g_arm.iW, (unsigned long)g_arm.sword_entry);
	EXPECT(((six_fn)g_vt[VT_TRK])(g_trk, 0, 0, 0, 0, 0) == 0x71 && g_arm.applied == 1, "P_TRK_EVAL: pass-through / applied %u",
	       g_arm.applied);
	float *row = (float *)g_pose_xf;
	const float tgt[3] = { 30, 30, 35 };
	EXPECT(dist3(row + 7 * 8, tgt) < 0.05f && fabsf(dist3(row + 6 * 8, row + 5 * 8) - dist3(E0, S0)) < 0.01f &&
		       fabsf(dist3(row + 7 * 8, row + 6 * 8) - dist3(W0, E0)) < 0.01f && !memcmp(row + 0 * 8, g_base_pose[0], 32) &&
		       dist3(row + 5 * 8, S0) < 1e-4f, "armpose ik: R_Hand at %.2f,%.2f,%.2f (want 30,30,35), lengths %.2f/%.2f",
	       row[56], row[57], row[58], dist3(row + 6 * 8, row + 5 * 8), dist3(row + 7 * 8, row + 6 * 8));
	EXPECT(fabsf(dist3(row + 8 * 8, row + 7 * 8) - 10.0f) < 0.01f, "armpose ik: the weapon node not carried rigidly");
	EXPECT(c_anim(g_body) == t_anim(g_body) + 10 - 1 + 1 && g_arm.have_seen && g_arm.seen_err < 0.05f,
	       "armpose: the next update read R_Hand %.2f cm from ours (seen %d)", (double)g_arm.seen_err, g_arm.have_seen);
	EXPECT(((uint64_t(*)(uint64_t))g_vt[VT_LISTEN])(g_attach) == 0x72 && g_arm.have_sword && g_arm.sword_err < 0.05f &&
		       g_arm.sword_err_stock > 1.0f, "armpose: the sword %.2f cm from the edited pose, %.2f from the stock one",
	       (double)g_arm.sword_err, (double)g_arm.sword_err_stock);
	msleep(1050);
	tick(1);
	EXPECT(log_has("ARMPOSE seq=") && log_has(" reached;"), "armpose: the once-a-second line");
	send("armpose rot 90", 1);
	((six_fn)g_vt[VT_TRK])(g_trk, 0, 0, 0, 0, 0);
	EXPECT(dist3(row + 7 * 8, W0) < 1e-3f && fabsf(dist3(row + 8 * 8, row + 7 * 8) - 10.0f) < 0.01f &&
		       dist3(row + 8 * 8, g_base_pose[8]) > 1.0f && fabsf(row[7 * 8 + 7]) < 0.99f,
	       "armpose rot: the hand stays, the weapon node turns about the forearm (%.2f,%.2f,%.2f)", row[64], row[65], row[66]);
	send("armpose off", 1);
	((six_fn)g_vt[VT_TRK])(g_trk, 0, 0, 0, 0, 0);
	EXPECT(!g_arm.on && !memcmp((void *)g_pose_xf, g_base_pose, sizeof g_base_pose), "armpose off: the pose still edited");

	/* aggro: the picked orc and the next nearest get Talion's HOBJECT as their scripted target, then cleared; minhealth */
	put(g_orc[1] + C_TARGET, 0, g_phobj); /* orc 1 attacks Talion: the watch line says so */
	send("pick;aggro 2", 1);
	srv_tick();
	EXPECT(V.tgt_calls == 2 && rq(g_ai[1] + AI_SCRIPTTGT) == g_phobj && rb(g_ai[1] + AI_STICKY) == 1 &&
		       rq(g_ai[2] + AI_SCRIPTTGT) == g_phobj && rq(g_ai[0] + AI_SCRIPTTGT) == 0 && g_agg.n == 2,
	       "aggro 2: %d calls, ai1 %#lx ai2 %#lx ai0 %#lx", V.tgt_calls, (unsigned long)rq(g_ai[1] + AI_SCRIPTTGT),
	       (unsigned long)rq(g_ai[2] + AI_SCRIPTTGT), (unsigned long)rq(g_ai[0] + AI_SCRIPTTGT));
	EXPECT(log_has("AGGRO seq=") && log_has("(Talion)") && log_has("aggro: 2 of 2 candidates"), "aggro: the lines");
	tick(1);
	EXPECT(log_has("AGGROWATCH orc 0") && log_has("combat target TALION"), "aggro: the watch line");
	send("minhealth 30", 1);
	srv_tick();
	uintptr_t pown = rq(g_scombat + C_OWNER);
	EXPECT(rf(pown + CH_MINHEALTH) == 30.0f && g_mh.on, "minhealth 30: %g", (double)rf(pown + CH_MINHEALTH));
	send("minhealth off;aggro off", 1);
	srv_tick();
	EXPECT(rf(pown + CH_MINHEALTH) == 0.0f && !g_mh.on && V.clr_calls == 2 && !rq(g_ai[1] + AI_SCRIPTTGT) && !g_agg.n,
	       "minhealth/aggro off: min %g, %d clears", (double)rf(pown + CH_MINHEALTH), V.clr_calls);
	put(g_orc[1] + C_TARGET, 0, 0);

	/* the UI: pointer (CInterfaceMgr move with the player's index), click (down now, up 2 ticks later), mstate, hitscan
	 * (the second component at the pointer), mcmd, padguard over the two guard calls, mouselog's hooks pass through */
	send("pointer 190 495", 1);
	EXPECT(V.move_calls == 1 && V.last_pos[0] == 190 && V.last_pos[1] == 495 && V.last_pl == 0 && g_ptr.have &&
		       log_has("im+0x3210 now (190, 495)"), "pointer: %d moves at %d,%d pl %d", V.move_calls, V.last_pos[0],
	       V.last_pos[1], V.last_pl);
	send("mstate;hitscan", 1);
	EXPECT(V.mstate == 1 && log_has("MSTATE im+0x3210 (190, 495)") && log_has("its mouse (12.5, 7.5) focus=1 cursors=1") &&
		       log_has("player-ui=5"), "mstate: the line");
	EXPECT(V.cliphit == 2 && log_has("HITSCAN 1 comp=") && log_has("stage (95.0, 247.5) hit=1") &&
		       log_has("hitscan at (190, 495): player UI state 5 (the pause menus), layer 1 of 1") &&
		       log_has("2 tested, 1 hit, focus 0"), "hitscan: %d hit tests", V.cliphit);
	send("click", 1);
	EXPECT(V.down_calls == 1 && V.up_calls == 0 && g_ptr.down && V.move_calls == 2, "click: down %d up %d", V.down_calls,
	       V.up_calls);
	tick(2);
	EXPECT(V.up_calls == 1 && !g_ptr.down && log_has("CLICK seq="), "click: the up after 2 ticks (%d)", V.up_calls);
	send("mouselog on", 1);
	EXPECT(c_guard_h(0x1234) == 11 && c_guard_c(0x1234) == 12, "P_LP_GUARD_*: pad guard as the game while padguard is on");
	send("padguard off", 1);
	EXPECT(c_guard_h(0x1234) == 1 && c_guard_c(0x1234) == 2 && atomic_load(&g_guard_over[0]) == 1 &&
		       atomic_load(&g_guard_over[1]) == 1, "padguard off: the guard still answered 1");
	send("padguard on", 1);
	EXPECT(c_guard_h(0x1234) == 11 && !g_padguard_off, "padguard on: not restored");
	short sxy[2] = { 33, 44 };
	EXPECT(((six_fn)g_vt[VT_SYS0])(0x55, 1, (uint64_t)(uintptr_t)sxy, 0, 0, 0) == 0x81 && V.sys_args[1] == 1 &&
		       ((six_fn)g_vt[VT_SYS0 + 2])(0x55, (uint64_t)(uintptr_t)sxy, 0, 0, 0, 0) == 0x81, "P_LP_SYS_*: pass-through");
	uintptr_t gview = rq(rq(g_im + IM_GAMEUI + 8) + 0x50);
	EXPECT(((six_fn)g_vt[VT_VIEW0])(gview, 1, 190, 495, 0, 0) == 0x82 && V.view_args[3] == 495 &&
		       ((six_fn)g_vt[VT_VIEW0 + 2])(gview, 190, 495, 0, 0, 0) == 0x82, "P_LP_VIEW_*: pass-through");
	unsigned char gev[0x20] = { 0 };
	gev[0] = 2;
	putf((uintptr_t)gev, 8, 190.0f);
	EXPECT(((six_fn)g_vt[VT_GFX])(0x99, (uint64_t)(uintptr_t)gev, 0, 0, 0, 0) == 0x83 && V.gfx_args[1] == (uintptr_t)gev,
	       "P_LP_GFX_EVENT: pass-through");
	EXPECT(log_has("MOUSE sys down btn=1 x=33 y=44") && log_has("MOUSE sys move btn=-1 x=33 y=44") &&
		       log_has("MOUSE view down") && log_has("(m_GameUI) btn=1 x=190 y=495") && log_has("MOUSE gfx down movie=0x99 x=190.0") &&
		       log_has("MOUSE guard hover P=0x1234 pad=1 -> 0 (padguard off"), "mouselog: the lines");
	send("mouselog off;mcmd 100", 1);
	EXPECT(T.last_on_cmd == 100, "mcmd: ON for %d", T.last_on_cmd);
	int offs = T.off_calls;
	tick(1);
	EXPECT(T.off_calls == offs + 1 && T.last_off_cmd == 100 && !g_mc.pending, "mcmd: OFF next tick");
	send("pointer off", 1);
	EXPECT(V.last_pos[0] == -1000 && !g_ptr.have, "pointer off: at %d", V.last_pos[0]);

	/* surf: the head ray and the camera ray through the checked slot; the surface record's fields */
	send("surf 2", 1);
	EXPECT(V.ray == 2 && V.surfrec == 2 && log_has("SURF seq=") && log_has("head along Talion's forward rc=1") &&
		       log_has("surface 7") && log_has("'Surfaces/Stone_Climbable' CanWallClimb=1 IsSlippery=0") &&
		       log_has("at 50.0 cm"), "surf: %d rays", V.ray);

	/* grav: vf_06 answers 0 for the player's PlayerMovement while off */
	EXPECT(((six_fn)g_vt[VT_GRAV])(g_pm, 0, 0, 0, 0, 0) == 1, "P_GRAV: pass-through");
	send("grav off", 1);
	EXPECT(((six_fn)g_vt[VT_GRAV])(g_pm, 0, 0, 0, 0, 0) == 0 && ((six_fn)g_vt[VT_GRAV])(0x4444, 0, 0, 0, 0, 0) == 1,
	       "grav off: the player's step still has gravity, or another object lost it");
	send("grav on", 1);
	EXPECT(((six_fn)g_vt[VT_GRAV])(g_pm, 0, 0, 0, 0, 0) == 1 && log_has("grav on: PlayerMovement::vf_06 as the game"),
	       "grav on");

	/* mount: off, then on a beast */
	send("mount 0.6", 1);
	put(g_player, P_MOUNT, g_beast);
	snprintf((char *)(g_player + P_MOUNTSTATE), 0x40, "MountWarg");
	tick(1);
	EXPECT(log_has("MOUNT on beast=") && log_has("pending state 'MountWarg'"), "mount: the line");
	put(g_player, P_MOUNT, 0);
	*(char *)(g_player + P_MOUNTSTATE) = 0;
	msleep(650);
	tick(1);
	EXPECT(!g_mnt.on && log_has("MOUNT seq="), "mount: the end");

	/* exec captures the console's output (the repository's +8 swapped for the call and back), conlog keeps it */
	V.conout = 0;
	send("exec AIDebug.ListAIs", 1);
	EXPECT(log_has("CONOUT level=2 'AI 'AI_Infantry' at 300 0 200'") && V.conout == 1 && rq(g_repo + 8) == (uintptr_t)t_conout &&
		       log_has("console output 1 lines (CONOUT)"), "exec: the console output (conout %d, slot %#lx)", V.conout,
	       (unsigned long)rq(g_repo + 8));
	send("conlog on", 1);
	EXPECT(rq(g_repo + 8) == (uintptr_t)p_conout, "conlog on: the slot");
	((conout_fn)rq(g_repo + 8))(NULL, 1, "Combat.Debug.Prompt = 1\n");
	EXPECT(log_has("CONOUT level=1 'Combat.Debug.Prompt = 1'") && V.conout == 2, "conlog: a line outside exec");
	send("conlog off", 1);
	EXPECT(rq(g_repo + 8) == (uintptr_t)t_conout && !g_conlog, "conlog off: the slot not restored");

	/* hitstop onbpw: armed, the counter prompt at Talion picks the orc, the next server tick holds it; the playing
	 * tracker is FindTracker's (anim 5 -> 0xff) */
	put(g_orc[1] + C_TARGET, 0, g_phobj);
	put32(g_orc[1], C_ANIM, 5);
	send("hitstop 60 onbpw", 1);
	srv_tick();
	EXPECT(g_hsarm.armed && !g_hs.on, "hitstop onbpw: not armed");
	c_counter(g_orc[1], 300);
	srv_tick();
	EXPECT(g_hs.on && g_hs.c == g_orc[1] && g_hs.trk_found && !g_hsarm.armed && log_has("COUNTERPROMPT orc=") &&
		       log_has("(THE PLAYER)") && log_has("its counter prompt (BPW) at the player"), "hitstop onbpw: on=%d c=%#lx",
	       g_hs.on, (unsigned long)g_hs.c);
	tick(1);
	EXPECT(log_has("PROMPTS at a counter prompt at Talion: 1 live entries"), "the prompt snap after a counter prompt");
	msleep(100);
	tick(1);
	EXPECT(!g_hs.on && log_has("animation time on tracker 255 (anim 5)"), "hitstop onbpw: the end line");
	put(g_orc[1] + C_TARGET, 0, 0);

	/* keytimes: the KEYHIT line names the target and the parry window */
	send("keytimes on", 1);
	EXPECT(c_keytimes(g_orc[1] + C_WS, g_orcobj[1], g_atk[0]) == 0x56, "P_KEYTIMES: the return value changed");
	tick(1);
	c_applydmg(g_orc[1], g_atk[0], g_phobj);
	EXPECT(log_has("parry window BPW 250..EPW 330 ms (80 ms): the impact BEFORE the window") && log_has("KEYTIMES c=") &&
		       log_has("target=none"), "keytimes: the window and the target");
	send("keytimes off", 1);

	/* trav's head height without `nodes`, the dagger measured again 3 ticks after the move */
	g_pose.have_node = 0;
	send("trav 0.3", 2);
	EXPECT(log_has("head_dy=1"), "trav: the head height without nodes");
	msleep(350);
	tick(2);
	send("dagger hand", 1);
	srv_tick();
	tick(4);
	EXPECT(log_has("hand (3 ticks later)"), "dagger: the later measure");
	send("dagger back", 1);
	srv_tick();
	tick(4);
	EXPECT(log_has("back (3 ticks later)"), "dagger back: the later measure");

	/* 21 (P9). templates, spawn/spawncaptain/despawn, blocktable, aiblockwatch/blockroll over the roll path's hooks,
	 * incoming, dealhit's picked orc and the recoil skip, tod/weathers, wmdump/wmhold, aimray (docs/runtime-probes.md P9) */
	g_last_hit_c = 0;
	send("templates", 1);
	EXPECT(log_has("TEMPLATE 1 'Tmpl_Uruk_Shield' archetype 'AI_Defender' (level 2)") &&
		       log_has("TEMPLATE 0 'Tmpl_Orc_Defender_Captain' archetype 'AI_Defender' (level 1)") &&
		       log_has("TEMPLATE archetype AI_Defender: 2 templates") && log_has("templates: 3 AI/Templates records, 3 scanned"),
	       "templates: the archetypes one and two levels down");
	float tp9[3], fz9[3] = { 0, 0, 1 }, fd9[3], d9[3];
	memcpy(tp9, g_pi.pos, sizeof tp9);
	send("spawn AI_Nobody,AI_Defender 400", 1);
	srv_tick();
	EXPECT(W.spawn == 1 && W.spawn_tmpl == g_tmpl[1] && W.spawn_ctx == g_hitobj && W.spawn_fm == 0 && W.spawn_a5 == 1 &&
		       W.spawn_a6 == 1 && !strcmp(W.spawn_name, "DebugSpawnedAI"),
	       "spawn: %d calls, template %#lx (want the shield one %#lx), context %#lx", W.spawn, (unsigned long)W.spawn_tmpl,
	       (unsigned long)g_tmpl[1], (unsigned long)W.spawn_ctx);
	for (int k = 0; k < 3; k++)
		d9[k] = tp9[k] - W.spawn_xf[k];
	d9[1] = 0;
	qrot(W.spawn_xf + 3, fz9, fd9);
	float dl9 = sqrtf(d9[0] * d9[0] + d9[2] * d9[2]);
	EXPECT(fabsf(dl9 - 400.0f) < 0.5f && fabsf(W.spawn_xf[1] - (tp9[1] + 10.0f)) < 0.01f &&
		       fabsf((fd9[0] * d9[0] + fd9[2] * d9[2]) / dl9 - 1.0f) < 1e-3f,
	       "spawn: at %.1f cm, %.1f cm up, facing Talion %.3f", dl9, W.spawn_xf[1] - tp9[1],
	       (fd9[0] * d9[0] + fd9[2] * d9[2]) / dl9);
	EXPECT(W.zone == 1 && W.setzone == 1 && W.setzone_npc == g_sp_npc && W.setzone_zone == 0x7a00 && W.level == 1 &&
		       W.level_c == g_sp_c && W.level_v == 3, "spawn: the post steps (zone %d, NPC slot %d, level %d = %u)", W.zone,
	       W.setzone, W.level, W.level_v);
	EXPECT(g_sp_n == 1 && g_pick.c == g_sp_c && g_pick.hobj == g_sp_h && log_has("SPAWN seq=") && log_has("; picked"),
	       "spawn: the new orc is not the pick");
	msleep(1100);
	tick(1);
	EXPECT(log_has("SPAWNCHECK 0 'AI_Defender' after") && log_has("a valid orc alive=1 dead=0 ai=1"), "spawn: the 1 s check");

	/* blocktable: the spawned orc's table; Orc_Club_Heavy with Cat_Light rolls entry 0 (0.25), with Cat_Heavy entry 1 */
	const struct dbf c9a[] = { { 0xa105, 0xa, 1, { g_cat_light } } }, c9b[] = { { 0xa105, 0xa, 1, { g_cat_heavy } } };
	db_fill(g_atk[0], 1, c9a, 1);
	send("blocktable 1 Orc_Club_Heavy", 1);
	EXPECT(log_has("record 'AI_Defender' (Combat+0x4c8 'AI_Defender') AIBlockTable 'AIBlockTable_Defender': 2 of 2 entries") &&
		       log_has("entry 0 Category=Cat_Light BlockChance=0.250") &&
		       log_has("entry 1 Category=(none: every attack) BlockChance=0.500 (default)") &&
		       log_has("attack 'Orc_Club_Heavy' categories 'Cat_Light'") && log_has("chance 0.250 (entry 0)"),
	       "blocktable: the table and the chance");
	db_fill(g_atk[0], 1, c9b, 1);
	send("blocktable Orc_Club_Heavy", 1);
	EXPECT(log_has("chance 0.500 (entry 1)"), "blocktable: the null-category entry");

	/* aiblockwatch: the gate, the record, the roll (chance in xmm2; blockroll force = the low bound), the notify, the
	 * goal's relevance on change, the shield test (a pre hook: arguments and result untouched) */
	send("aiblockwatch on", 1);
	EXPECT(c_aibgate(0x1234, g_atk[0]) == 0x101 && W.gate == 1 && log_has("AIBLOCK gate node=") && log_has("-> passed"),
	       "P_AIB_GATE");
	EXPECT(c_aibrec(0x4242) == g_ccr_def &&
		       log_has("AIBLOCK reached target=spawn 0 'AI_Defender' record 'AI_Defender' AIBlockTable 'AIBlockTable_Defender'"),
	       "P_AIB_REC");
	float rv9 = c_aibroll(0.4f);
	EXPECT(rv9 == 0.75f && W.roll == 1 && W.roll_lo == 0.0f && W.roll_hi == 1.0f &&
		       log_has("chance=0.400 roll=0.750 -> no notify"), "P_AIB_ROLL: %g", rv9);
	send("blockroll force", 1);
	rv9 = c_aibroll(0.4f);
	EXPECT(rv9 == 0.0f && log_has("roll=0.000 (blockroll force; the game's roll 0.750) -> notify"), "blockroll force: %g", rv9);
	send("blockroll off", 1);
	EXPECT(c_aibroll(0.4f) == 0.75f && !g_aib.force, "blockroll off");
	EXPECT(c_aibnotify(g_pi.owner, g_sp_npc) == 1 && W.fact == 1 && W.fact_npc == g_sp_npc && log_has("AIBLOCK notify attacker=") &&
		       log_has("(THE PLAYER) npc="), "P_AIB_NOTIFY");
	((six_fn)g_vt[VT_GOALREL])(g_goal, 0, 0, 0, 0, 0);
	((six_fn)g_vt[VT_GOALREL])(g_goal, 0, 0, 0, 0, 0);
	g_goal_next = 0;
	((six_fn)g_vt[VT_GOALREL])(g_goal, 0, 0, 0, 0, 0);
	EXPECT(W.goalrel == 3 && atomic_load(&g_aib.goal_changes) == 2 && log_has("AIBLOCK goal_counter relevant -1 -> 1") &&
		       log_has("relevant 1 -> 0 npc="), "P_GOAL_REL: %d calls, %llu changes", W.goalrel,
	       (unsigned long long)atomic_load(&g_aib.goal_changes));
	EXPECT(c_shield(0x5550, g_atk[0], 7) == 0x93 && W.shield_args[0] == 0x5550 && W.shield_args[1] == g_atk[0] &&
		       W.shield_args[2] == 7 && log_has("AIBLOCK shield item=0x5550"), "P_SHIELD: pass-through and the line");
	send("aiblockwatch off", 1);
	EXPECT(log_has("watch off: gate passed 1 refused 0, roll path reached 1, rolls 3 (forced 1, won 1), notifies 1"),
	       "aiblockwatch off: the counts");

	/* incoming: FUN_029ac890(Talion, the picked orc); with `roll` our own roll from its table first */
	*(uint8_t *)(rq(g_sp_npc + NPC_AI) + BRAIN_REPLAN) = 0;
	send("incoming", 1);
	srv_tick();
	EXPECT(W.fact == 2 && W.fact_owner == g_pi.owner && W.fact_npc == g_sp_npc && log_has("INCOMING seq=") &&
		       log_has("replan 0 -> 1"), "incoming: %d facts", W.fact);
	send("incoming roll Orc_Club_Heavy", 1);
	srv_tick();
	EXPECT(log_has(" roll: 'Orc_Club_Heavy' at 'AI_Defender': chance 0.500 (entry 1 of 2)"), "incoming roll: the line");

	/* dealhit: the picked (spawned) orc before the nearest; `auto` skips a PC_Recoil_* last attack */
	int h9 = T.hit_calls;
	g_last_player_attack = g_atk[3];
	snprintf(g_last_player_attack_name, sizeof g_last_player_attack_name, "PC_Recoil_Light_R2L_F");
	send("dealhit auto", 1);
	srv_tick();
	EXPECT(T.hit_calls == h9 + 1 && T.hit_target == g_sp_h && T.hit_attack == g_atk[1] && log_has("(the picked orc)"),
	       "dealhit: target %#lx (want the spawned %#lx), attack %#lx (the recoil skipped)", (unsigned long)T.hit_target,
	       (unsigned long)g_sp_h, (unsigned long)T.hit_attack);

	/* spawncaptain: refused while the member is in the world; despawn; then the member's template, bound, its level */
	put32(g_fm, FM_FLAGS, 2);
	send("spawncaptain 300", 1);
	srv_tick();
	EXPECT(W.spawn == 1, "spawncaptain spawned a member that is in the world");
	put32(g_fm, FM_FLAGS, 0);
	send("despawn", 1);
	srv_tick();
	EXPECT(W.remove == 1 && W.remove_h == g_sp_h && g_sp_n == 0 && !g_pick.c && log_has("DESPAWN seq=") &&
		       log_has("removal queued"), "despawn: %d removals", W.remove);
	send("spawncaptain 300", 1);
	srv_tick();
	EXPECT(W.spawn == 2 && W.spawn_fm == g_fm && W.spawn_tmpl == g_tmpl[2] && W.bind == 1 && W.act == 1 && W.level_v == 12 &&
		       g_sp_n == 1 && g_sp[0].captain, "spawncaptain: spawn %d fm %#lx bind %d act %d level %u", W.spawn,
	       (unsigned long)W.spawn_fm, W.bind, W.act, W.level_v);
	send("spawn NoSuchThing", 1);
	srv_tick();
	send("despawn all", 1);
	srv_tick();
	EXPECT(W.remove == 2 && g_sp_n == 0, "despawn all: %d removals, %d left", W.remove, g_sp_n);

	/* tod, weathers */
	send("tod;weathers", 1);
	EXPECT(log_has("Record(Weather_Clear_Day) GDBIndex(5) TOD(Day) = byte 3") && log_has("WEATHER 1 'Weather_Rain'") &&
		       log_has("weathers: 2 Weather/Definition records"), "tod/weathers: the lines");

	/* wmdump: the modes with their command and components, the player's WeaponModeSetup; wmhold presses a mode's command */
	put(g_scombat, C_CHARREC, g_ccr_pl);
	send("wmdump", 1);
	EXPECT(log_has("WMODE 1 'Mode_Bow_Aim' command 118 'CombatPrimary01' HeldDuration=0.2 IsToggle=0 UseRawInput=-") &&
		       log_has(" Predicate=1 FreeAim=1") && log_has("WMODE 2 'Mode_Wraith_Vision' command 243 'VisionModeToggle'") &&
		       log_has("WMODE 0 'Mode_Stealth' command 125 'CombatSecondary04'") &&
		       log_has(" Predicate=1 VisionMode=1") &&
		       log_has("WMSETUP the player's record 'PC_Talion_Combat' WeaponModeSetup 'Setup_Talion': "
			       "Entries='Mode_Bow_Aim','Mode_Wraith_Vision'"), "wmdump: the lines");
	snprintf(p, sizeof p, "%s/wmodes-%u.tsv", outd, g_seq_sent);
	EXPECT(access(p, R_OK) == 0, "wmdump: %s missing", p);
	put(g_scombat, C_CHARREC, 0);
	int on9 = T.on_calls;
	send("wmhold NoSuchMode,freeaim 300", 1); /* P10's fix: not Mode_Stealth (its FreeAim element is a null record) */
	EXPECT(T.on_calls == on9 + 1 && T.last_on_cmd == 118 &&
		       log_has("mode 'Mode_Bow_Aim' (a FreeAim record) command 118 'CombatPrimary01' held for 300 ms (HeldDuration=0.2"),
	       "wmhold freeaim: last ON %d", T.last_on_cmd);
	send("wmhold Vision 200", 1);
	EXPECT(log_has("mode 'Mode_Wraith_Vision' (the name contains it) command 243 'VisionModeToggle'"), "wmhold by name");
	send("wmhold mode_stealth 200", 1);
	EXPECT(log_has("mode 'Mode_Stealth' (the exact name) command 125 'CombatSecondary04'"), "wmhold by the exact name");
	send("release", 2);

	/* aimray: the free-aim ray's transform turned (yaw right, pitch up), from R_Hand with `hand`, and back */
	float ax9[7];
	EXPECT(c_camxf(0x77, ax9) == 1 && ax9[6] == 1.0f, "P_BOW_CAMXF: pass-through");
	send("aimray 90 0 2", 1);
	c_camxf(0x77, ax9);
	qrot(ax9 + 3, fz9, fd9);
	EXPECT(fabsf(fd9[0] - 1.0f) < 1e-4f && fabsf(fd9[2]) < 1e-4f && ax9[0] == 100.0f && atomic_load(&g_aim.over) == 1,
	       "aimray 90 0: dir %.3f %.3f %.3f", fd9[0], fd9[1], fd9[2]);
	msleep(550);
	tick(1);
	EXPECT(log_has("AIMRAY seq=") && log_has(" turned: 1 calls, 1 turned"), "aimray: the line");
	g_t_rhand = 1;
	send("aimray 0 30 hand 2", 1);
	c_camxf(0x77, ax9);
	qrot(ax9 + 3, fz9, fd9);
	float hw9[8];
	node_xf(g_charobj, t_hash_of(7), hw9);
	EXPECT(fabsf(fd9[1] - 0.5f) < 1e-3f && fabsf(fd9[2] - 0.866f) < 1e-3f && dist3(ax9, hw9) < 0.01f,
	       "aimray 0 30 hand: dir %.3f %.3f %.3f pos %.1f %.1f %.1f (R_Hand %.1f %.1f %.1f)", fd9[0], fd9[1], fd9[2], ax9[0],
	       ax9[1], ax9[2], hw9[0], hw9[1], hw9[2]);
	g_t_rhand = 0;
	send("aimray off", 1);
	c_camxf(0x77, ax9);
	EXPECT(!g_aim.on && ax9[3] == 0.0f && ax9[6] == 1.0f && log_has(" off: "), "aimray off");

	/* 22 (P10). P_ROOTDELTA, speed, pmrec (set/restore/base), heading (the scan: free ground, a wall, a drop, self hits),
	 * walk (the camera re-aim, the mirror check), loco const/stick (the replacement in Talion's frame, the skips, the
	 * lock, the stop), fling (momentum mode, the landing, our return), animrate (set, the game's write, restored),
	 * vaultlog (a sync episode), npcpush (the spawn along the heading, the walk, the push) (docs/runtime-probes.md P10) */
	static const float id4[4] = { 0, 0, 0, 1 }; /* yaw90: the pmmove test's */
	const float z3[3] = { 0, 0, 0 };
	uintptr_t atk10 = rq(g_scombat + C_ATTACK);
	put(g_scombat, C_ATTACK, 0);
	memcpy((void *)(g_charobj + OBJ_ROT), id4, 16);
	memcpy((void *)(g_cam + CAM_ROT), id4, 16);
	memcpy((void *)(g_cam + CAM_EXTRA), id4, 16);
	memcpy((void *)(g_charobj + OBJ_POS), z3, 12);
	*(uint8_t *)(g_player + P_FSM) = 6;
	send("release", 2);
	/* the root delta: the game's translation passes through and is kept for speed */
	float q10[3], q11[3];
	int rd0 = g_t_rd_calls;
	g_t_rd[2] = 5.0f;
	memcpy(q10, (void *)(g_charobj + OBJ_POS), 12);
	tick(3);
	memcpy(q11, (void *)(g_charobj + OBJ_POS), 12);
	EXPECT(g_t_rd_calls == rd0 + 3 && fabsf(q11[2] - q10[2] - 15.0f) < 0.01f && g_rd.game[2] == 5.0f && g_rd.game[6] == 1.0f,
	       "P_ROOTDELTA: %d calls, the root moved %.2f (want 15), the game delta z %g", g_t_rd_calls - rd0,
	       (double)(q11[2] - q10[2]), (double)g_rd.game[2]);
	g_t_rd[2] = 0;

	/* speed: the stick held 0.4 s (the stand-in: 5 cm a tick while ForwardAxis is held), the graph values, the stop */
	g_t_rd_mode = 1;
	send("speed 1.0;move 0 1 400", 1);
	unsigned spseq = g_seq_sent;
	for (int i = 0; i < 80 && g_spd.on; i++) {
		msleep(20);
		tick(1);
	}
	snprintf(p, sizeof p, "%s/speed-%u.tsv", outd, spseq);
	EXPECT(!g_spd.on && g_spd.n > 20 && access(p, R_OK) == 0 && log_has("RawMoveMag 0.700 Game_Velocity 321.0 smoothed 300.0 DeltaYaw 0.25") &&
		       log_has("stick 1.00 from +") && log_has("stopped") && X.gvar == 4 && X.gget > 20,
	       "speed: %d rows, file %s, graph lookups %d / reads %d", g_spd.n, p, X.gvar, X.gget);
	float plat10 = 0;
	int npl = 0;
	for (int i = 0; i < g_spd.n; i++)
		if (g_spd.r[i].stick > 0.5f && g_spd.r[i].t > 0.15f)
			plat10 += g_spd.r[i].hs, npl++;
	plat10 = npl ? plat10 / (float)npl : 0;
	EXPECT(plat10 > 100 && plat10 < 300, "speed: the held rows' speed %.1f cm/s (5 cm per ~20 ms tick)", (double)plat10);
	g_t_rd_mode = 0;

	/* pmrec: both read paths, the console names, the caches; set through the console (the stand-in variable writes the
	 * record), restore, the direct cache write */
	send("pmrec", 1);
	EXPECT(log_has("'PlayerMovement_Talion' (flags 0x3, 30 attributes); schema") &&
		       log_has("PMREC 17 'PhysicsCharacterGravity' hash 0x7011 state 0 index 17 (search) type 2 = -20; console none") &&
		       log_has("PMREC 16 'BaseSpeedMultiplier' hash 0x7010 state 2 index 16 (fast) type 2 = 1; console "
			       "PlayerMovement.BaseSpeedMultiplier -> ") &&
		       log_has("PMREC 15 'PlayerYawInterpolateTime' hash 0x700f state 0 index 15 (search) type 3 = 400 (0.400 s)") &&
		       log_has("PMREC  4 'FallMinHeight' hash 0x7004 state 2 index 4 (fast) type 2 = 100; console "
			       "Client.PlayerMovement.FallMinHeight") &&
		       log_has("PMREC 19 'PenOriginOffset' hash 0x7013 state 0 index 19 (search) type 2 = 0,50,0;") &&
		       log_has("PMREC 18 'UsePenCollision' hash 0x7012 state 2 index 18 (fast) type 1 = 1;") &&
		       log_has("pm+0x32c BaseSpeedMultiplier=1 pm+0x450 PlayerYawInterpolateTime=0.4 s pm+0x454 FallMinHeight=100 cm") &&
		       log_has("gravity pc+0x58=-20 on pc+0x60=1") && log_has("pmrec: 30 attributes, 2 with a console name"),
	       "pmrec: the dump");
	send("pmrec set BaseSpeedMultiplier 1.5", 1);
	EXPECT(strstr(T.run_s, "PlayerMovement.BaseSpeedMultiplier 1.5") &&
		       log_has("set 16 'BaseSpeedMultiplier' through 'PlayerMovement.BaseSpeedMultiplier' to 1.5: the record now 1") &&
		       log_has("'BaseSpeedMultiplier': the record right after 1.5"), "pmrec set: '%s'", T.run_s);
	tick(4);
	EXPECT(log_has("'BaseSpeedMultiplier': the record 3 ticks later 1.5"), "pmrec set: 3 ticks later");
	send("pmrec restore", 1);
	char pv10[100], pc10[40];
	int pt10;
	pmr_value(g_pmrec, 16, pv10, sizeof pv10, pc10, sizeof pc10, &pt10);
	EXPECT(strstr(T.run_s, "PlayerMovement.BaseSpeedMultiplier 1") && !strcmp(pv10, "1") && g_pmr.nset == 0 &&
		       log_has("pmrec restore: 1 console value(s) set back"), "pmrec restore: '%s', the record %s", T.run_s, pv10);
	send("pmrec base 2", 1);
	EXPECT(rf(g_pm + PM_BASESPEED) == 2.0f && log_has("pmrec base 2: pm+0x32c 1 -> 2"), "pmrec base 2");
	send("pmrec base off", 1);
	EXPECT(rf(g_pm + PM_BASESPEED) == 1.0f && log_has("pmrec base off: pm+0x32c back"), "pmrec base off");

	/* heading: the stand-in world has a wall plane at x = 500 and a drop beyond z = 1000 */
	memcpy((void *)(g_charobj + OBJ_POS), z3, 12);
	tick(2);
	g_ray_world = 1;
	send("heading clear 15", 1);
	EXPECT(log_has("facing 0.0 deg, 20 m, 0 self hits (yaw deg:free m): 0:11.0 22:11.0 45:7.1 68:5.4 90:5.0") &&
		       log_has("heading clear: yaw -67.5 deg (20.0 m free)") && fabsf(g_head.yaw + 1.1781f) < 1e-3f,
	       "heading clear 15: %.1f deg", (double)(g_head.yaw * RAD2DEG));
	g_ray_self = 1;
	send("heading wall 10", 1);
	EXPECT(log_has(", 32 self hits ") && log_has("heading wall: yaw 90.0 deg (5.0 m free)") && fabsf(g_head.yaw - 1.5708f) < 1e-3f,
	       "heading wall 10 (with self hits): %.1f deg", (double)(g_head.yaw * RAD2DEG));
	g_ray_self = 0;
	send("heading back", 1);
	EXPECT(log_has("heading back: yaw -90.0 deg") && fabsf(g_head.yaw + 1.5708f) < 1e-3f, "heading back");
	send("heading open", 1);
	EXPECT(fabsf(fabsf(g_head.yaw) - 3.14159f) < 1e-3f && log_has("heading open: yaw"), "heading open: %.1f deg",
	       (double)(g_head.yaw * RAD2DEG));
	g_last_hit_c = 0;
	send("pick Grunt;heading orc", 1);
	float oo10[3];
	obj_pos(g_orcobj[1], oo10);
	EXPECT(fabsf(g_head.yaw - atan2f(oo10[0] - g_root.pos[0], oo10[2] - g_root.pos[2])) < 1e-3f &&
		       log_has("heading orc: yaw") && log_has("(the picked orc at "), "heading orc: %.1f deg", (double)(g_head.yaw * RAD2DEG));
	g_ray_world = 0;

	/* walk: the stick re-aimed through the camera's yaw; the mirror check (the stand-in game maps x mirrored) */
	send("heading 90;walk 0.5 300", 1);
	EXPECT(fabsf(cmd_value(6) - 0.5f) < 1e-4f && fabsf(cmd_value(3)) < 1e-4f && g_walk.on, "walk: stick (%g %g) for heading 90, "
	       "the camera at 0", (double)cmd_value(6), (double)cmd_value(3));
	memcpy((void *)(g_cam + CAM_ROT), yaw90, 16);
	tick(1);
	EXPECT(fabsf(cmd_value(6)) < 1e-4f && fabsf(cmd_value(3) - 0.5f) < 1e-4f, "walk: stick (%g %g) with the camera at 90",
	       (double)cmd_value(6), (double)cmd_value(3));
	memcpy((void *)(g_cam + CAM_ROT), id4, 16);
	send("walk off", 1);
	EXPECT(!g_walk.on && cmd_value(6) == 0.0f && log_has("end (off)"), "walk off");
	send("release", 2);
	g_t_rd_mode = 2;
	g_stick_mirror = 0;
	memcpy(q10, (void *)(g_charobj + OBJ_POS), 12);
	send("walk 1 1500", 1);
	for (int i = 0; i < 40 && g_walk.on; i++) {
		msleep(60);
		tick(1);
	}
	memcpy(q11, (void *)(g_charobj + OBJ_POS), 12);
	EXPECT(g_stick_mirror == 1 && log_has("the stick's x is mirrored, flipped from now on") && log_has("end (time)") &&
		       q11[0] > q10[0] + 20.0f, "walk: the mirror check (mirror %d, x moved %.1f)", g_stick_mirror,
	       (double)(q11[0] - q10[0]));
	g_stick_mirror = 0;
	g_t_rd_mode = 0;
	send("release", 2);

	/* loco const: v*dt (dt = the game timer's 16 ms) in the world whatever Talion's facing; fsm 8 skipped; the lock */
	g_t_rd[2] = 3.0f;
	send("heading 0", 1);
	memcpy(q10, (void *)(g_charobj + OBJ_POS), 12);
	send("loco const 0 500 0.3", 1);
	memcpy(q11, (void *)(g_charobj + OBJ_POS), 12);
	EXPECT(fabsf(q11[2] - q10[2] - 8.0f) < 0.01f && fabsf(q11[0] - q10[0]) < 0.01f && fabsf(g_rd.ours[2] - 8.0f) < 0.01f &&
		       atomic_load(&g_loco.replaced) == 1, "loco const: the root moved (%.2f %.2f), want (0 8)",
	       (double)(q11[0] - q10[0]), (double)(q11[2] - q10[2]));
	memcpy((void *)(g_charobj + OBJ_ROT), yaw90, 16);
	memcpy(q10, (void *)(g_charobj + OBJ_POS), 12);
	tick(1);
	memcpy(q11, (void *)(g_charobj + OBJ_POS), 12);
	EXPECT(fabsf(q11[2] - q10[2] - 8.0f) < 0.01f && fabsf(q11[0] - q10[0]) < 0.01f && fabsf(g_rd.ours[0] + 8.0f) < 0.01f,
	       "loco const at yaw 90: the root moved (%.2f %.2f), ours (%g %g %g)", (double)(q11[0] - q10[0]),
	       (double)(q11[2] - q10[2]), (double)g_rd.ours[0], (double)g_rd.ours[1], (double)g_rd.ours[2]);
	*(uint8_t *)(g_player + P_FSM) = 8;
	memcpy(q10, (void *)(g_charobj + OBJ_POS), 12);
	tick(1);
	memcpy(q11, (void *)(g_charobj + OBJ_POS), 12);
	EXPECT(atomic_load(&g_loco.sk_fsm) == 1 && fabsf(q11[0] - q10[0] - 3.0f) < 0.01f, "loco: fsm 8 not replaced (moved %.2f)",
	       (double)(q11[0] - q10[0]));
	*(uint8_t *)(g_player + P_FSM) = 6;
	uintptr_t mgr10 = rq(g_body + BODY_MGR);
	*(uint8_t *)(mgr10 + MGR_LOCK_T) = 1;
	memcpy(q10, (void *)(g_charobj + OBJ_POS), 12);
	tick(1);
	memcpy(q11, (void *)(g_charobj + OBJ_POS), 12);
	EXPECT(atomic_load(&g_loco.lock_t) == 1 && dist3(q10, q11) < 0.01f, "loco: the lock (counted %llu, moved %.2f)",
	       (unsigned long long)atomic_load(&g_loco.lock_t), (double)dist3(q10, q11));
	*(uint8_t *)(mgr10 + MGR_LOCK_T) = 0;
	memcpy((void *)(g_charobj + OBJ_ROT), id4, 16);
	msleep(320);
	tick(4);
	EXPECT(!atomic_load(&g_loco.mode) && log_has("end (loco, time)") && log_has("skipped fsm 1 /") &&
		       log_has("the translation lock was set in 1,") && log_has("the game timer 3,") && log_has("stop after the end: the root"),
	       "loco const: the end line");
	g_t_rd[2] = 0;
	/* loco stick: the probe's ForwardAxis through the camera (yaw 0) at 500 cm/s; the release inside the window */
	send("move 0 1 150;loco stick 0.6", 1);
	EXPECT(fabsf(g_rd.ours[2] - 8.0f) < 0.01f && atomic_load(&g_loco.mode) == LOCO_STICK, "loco stick: ours z %g",
	       (double)g_rd.ours[2]);
	msleep(200);
	tick(6);
	EXPECT(log_has("stop at the stick's release: the root"), "loco stick: the release");
	send("loco off", 1);
	EXPECT(!atomic_load(&g_loco.mode) && log_has("end (loco, off)"), "loco off");

	/* fling: momentum mode, the launch velocity along the heading, gravity on; the air (fsm 8, not supported), the
	 * landing (fsm 6): the probe puts root-motion mode back 0.4 s later */
	*(uint8_t *)(g_pm + PM_VELMODE) = 1;
	*(uint8_t *)(g_pc10 + PCH_GRAVON) = 0;
	int vm10 = X.velmode;
	send("fling 0 600 400", 1);
	unsigned flseq = g_seq_sent;
	float fv10[3];
	memcpy(fv10, (void *)(g_pm + PM_VEL), 12);
	EXPECT(X.velmode == vm10 + 1 && X.velmode_last == 0 && rb(g_pm + PM_VELMODE) == 0 && fabsf(fv10[0]) < 1e-3f &&
		       fv10[1] == 600.0f && fabsf(fv10[2] - 400.0f) < 1e-3f && rb(g_pc10 + PCH_GRAVON) == 1 && g_fl.on,
	       "fling: velmode %d calls (last %d), v (%g %g %g), gravity %u", X.velmode - vm10, X.velmode_last, (double)fv10[0],
	       (double)fv10[1], (double)fv10[2], rb(g_pc10 + PCH_GRAVON));
	*(uint8_t *)(g_player + P_FSM) = 8;
	g_t_supported = 0;
	for (int i = 0; i < 4; i++) {
		msleep(50);
		tick(1);
	}
	*(uint8_t *)(g_player + P_FSM) = 6;
	g_t_supported = 1;
	for (int i = 0; i < 40 && g_fl.on; i++) {
		msleep(50);
		tick(1);
	}
	snprintf(p, sizeof p, "%s/fling-%u.tsv", outd, flseq);
	EXPECT(!g_fl.on && rb(g_pm + PM_VELMODE) == 1 && X.velmode_last == 1 && log_has("root-motion mode back by the probe") &&
		       log_has("end (landed)") && log_has("fsm path 6>8>6;") && log_has("lifted off") && access(p, R_OK) == 0,
	       "fling: the landing and our return (velmode %u)", rb(g_pm + PM_VELMODE));

	/* animrate: the main tracker at 0.7, the game's own write set back, then restored; `all` = every playing tracker */
	g_rate_live = 1;
	g_rate_cur[0] = 1.0f, g_rate_cur[1] = 0.5f;
	send("animrate 0.7 0.3", 1);
	EXPECT(g_ar.on && g_ar.n == 1 && g_rate_cur[0] == 0.7f && log_has("1 trackers: 255(anim 5, was 1);"), "animrate 0.7: %d "
	       "trackers, rate %g", g_ar.n, (double)g_rate_cur[0]);
	g_rate_cur[0] = 1.0f; /* the game writes its own rate */
	tick(1);
	EXPECT(g_rate_cur[0] == 0.7f && log_has("the game set rate 1 (set back to 0.70)"), "animrate: the game's write");
	msleep(350);
	tick(1);
	EXPECT(!g_ar.on && g_rate_cur[0] == 1.0f && log_has("1 trackers back to their rate, 0 left as the game set them; the game "
	       "wrote the rate 1 times"), "animrate: restored (%g)", (double)g_rate_cur[0]);
	send("animrate 0.5 2 all", 1);
	EXPECT(g_ar.n == 2 && g_rate_cur[0] == 0.5f && g_rate_cur[1] == 0.5f, "animrate all: %d trackers", g_ar.n);
	send("animrate off", 1);
	EXPECT(!g_ar.on && g_rate_cur[0] == 1.0f && g_rate_cur[1] == 0.5f && log_has("end (off)"), "animrate off");
	g_rate_live = 0;

	/* vaultlog: a sync action with its target and fsm 9 for 3 ticks (4 cm a tick), then over */
	uintptr_t sync10 = (uintptr_t)zalloc(0x200), vrec10 = mk_record("PC_Vault_Over_Orc_Test", 0x7200);
	send("vaultlog 0.6", 1);
	unsigned vlseq = g_seq_sent;
	put(sync10, SYNC_REC, vrec10);
	put(sync10, SYNC_TARGET, g_orc[1]);
	put(g_scombat, C_SYNC1, sync10);
	*(uint8_t *)(g_player + P_FSM) = 9;
	g_t_rd[2] = 4.0f;
	tick(3);
	put(sync10, SYNC_REC, 0);
	*(uint8_t *)(g_player + P_FSM) = 6;
	g_t_rd[2] = 0;
	tick(1);
	EXPECT(log_has("sync 'PC_Vault_Over_Orc_Test' target ") && log_has("episode 1 end (over)") &&
		       log_has("sync PC_Vault_Over_Orc_Test,fsm 9; root moved 12.0 cm horizontally") && log_has("path 12.0 cm") &&
		       log_has("the attack's EnableCollision (Combat+0x56c) 0 seen: no"),
	       "vaultlog: the episode");
	msleep(650);
	tick(1);
	snprintf(p, sizeof p, "%s/vault-%u.tsv", outd, vlseq);
	EXPECT(!g_vl.on && log_has("end: 1 episodes") && access(p, R_OK) == 0, "vaultlog: the end");
	put(g_scombat, C_SYNC1, 0);

	/* npcpush: an AI_Infantry along the heading (not Talion's facing), loco at it, then the push */
	memcpy((void *)(g_charobj + OBJ_ROT), yaw90, 16); /* Talion faces +X; the heading is +Z */
	tick(1);
	send("heading 0", 1);
	int sp10 = W.spawn;
	srv_tick();
	send("npcpush 200 250", 1);
	srv_tick();
	EXPECT(W.spawn == sp10 + 1 && W.spawn_tmpl == g_tmpl[2] && g_np.stage == 1 && fabsf(W.spawn_xf[2] - g_root.pos[2] - 200.0f) < 1 &&
		       fabsf(W.spawn_xf[0] - g_root.pos[0]) < 1, "npcpush: the spawn (%d, at %.1f %.1f; Talion at %.1f %.1f)",
	       W.spawn - sp10, (double)W.spawn_xf[0], (double)W.spawn_xf[2], (double)g_root.pos[0], (double)g_root.pos[2]);
	msleep(1250);
	tick(1);
	EXPECT(g_np.stage == 2 && atomic_load(&g_loco.mode) == LOCO_CONST && !strcmp(g_loco.who, "npcpush"), "npcpush: the walk");
	for (int i = 0; i < 120 && g_np.stage; i++) {
		msleep(15);
		tick(1);
	}
	EXPECT(!g_np.stage && log_has("NPCPUSH seq=") && log_has("PASSED it") && log_has("the push (written)") &&
		       log_has("no collision with the orc"), "npcpush: the result");
	send("despawn all", 1);
	srv_tick();
	memcpy((void *)(g_charobj + OBJ_ROT), id4, 16);
	put(g_scombat, C_ATTACK, atk10);
	send("release", 2);

	/* 18. unknown words and the rate limiter */
	send("definitely_not_a_word", 2);
	unsigned before = atomic_load(&g_rl[RL_STATE].suppressed);
	for (int i = 0; i < RL_FIRST + 4 * RL_PER_SEC; i++)
		rl_ok(RL_STATE);
	EXPECT(atomic_load(&g_rl[RL_STATE].suppressed) > before, "the rate limiter suppressed nothing");

	flush_log();
	atomic_store(&g_stop, 1);
	FILE *lf = fopen(logp, "r");
	int seen_fail = 0, seen_unknown = 0, lines = 0, seen_socket = 0, seen_guard = 0, seen_refused = 0;
	char line[4096];
	while (lf && fgets(line, sizeof line, lf)) {
		lines++;
		if (strstr(line, "'SocketHead'") || strstr(line, "from SocketHead"))
			seen_socket++;
		if (!strncmp(line, "FAIL ", 5)) {
			if (strstr(line, "definitely_not_a_word") || strstr(line, "Nope_Not_A_Record"))
				seen_unknown++;
			else if (strstr(line, "refused, spike1's save guard"))
				seen_guard++;
			else if (strstr(line, "exec: longer than") || strstr(line, "exec: G22_RUNSTRING: bytes") ||
				 strstr(line, "exec: the engine") || strstr(line, "exec: no engine") ||
				 strstr(line, "pmmove: the extra move is not at rest") || strstr(line, "pick: no live AI character") ||
				 strstr(line, "swingevent: the swing of seq") || strstr(line, "hitstop: the hold of seq") ||
				 strstr(line, "attachhide: no attached child '1' with a model") ||
				 strstr(line, "spawncaptain: the member is already in the world") || strstr(line, "spawn NoSuchThing:"))
				seen_refused++;
			else
				seen_fail++;
		}
	}
	if (lf)
		fclose(lf);
	EXPECT(seen_fail == 0, "%d unexpected FAIL lines in the log %s", seen_fail, logp);
	EXPECT(seen_socket >= 2, "the nodes dump did not use the SocketHead socket (%d lines mention it)", seen_socket);
	EXPECT(seen_unknown == 2, "%d expected FAIL lines (unknown word, unknown record), want 2", seen_unknown);
	EXPECT(seen_guard == 3, "%d guard refusals (cmd, exec, pmmove), want 3", seen_guard);
	EXPECT(seen_refused == 11, "%d expected refusals (exec: overlong, slot, vtable, null engine; pmmove pending; pick, "
	       "swingevent and hitstop while on; attachhide without a model; P9: spawncaptain in the world, spawn unknown), want 11",
	       seen_refused);
	printf("log %s (%d lines); hooks installed %d of %d\n", logp, lines, HK_N - si, HK_N);
	printf("%s (%d failures)\n", fails ? "SELFTEST FAILED" : "selftest ok", fails);
	return fails != 0;
}
