/* somvr_stealth_game.c - SOMVR_STEALTH_KILL=off|log|on (default off): the VR stealth kill (docs/re-xr-pipeline.md 67,
 * handoff/HANDOFF-stealth.md). Included by somvr.c after somvr_verbs_game.c, somvr_crouch_game.c and the combat code;
 * the pure part is somvr_stealth.c.
 *
 * Crouched (the game's stealth crouch: SOMVR_CROUCH's hold, or the Head node read low) or slow, behind an orc that has
 * not noticed Talion, the player's blade entering its back or neck - a hit by combat's rule or a stab along the blade -
 * kills it at once: Combat_DealHit(contact, Talion, the orc, the stock silent stealth kill's record
 * PC_Bip_SneakInstaKill_Silent_B, forceKill 1), the death SOMVR_COMBAT_DEATH's type (the body drops where it stands, as
 * the executions'). No attack of Talion's and no sync starts: nothing aligns or moves Talion or the view (the stock kill
 * is a paired sync that teleports him onto the orc's back); none of its stimuli is registered (the stock silent kill
 * raises 'Instakill_Seen' at its start - only orcs that see it react - the loud one 'Instakill_Loud').
 * "Not noticed" is the game's own: the AIIsAware predicate's function (FUN_0229bbc0 on the orc's HOBJECT, read here
 * as memory, no call): an NPC (class 0x4c52780 in the HOBJECT's list +0x48) whose character (+0x38) is of kind 4
 * (+0x10 -> +0x7c) is aware when its awareness float +0xeb8 is not 0 (or it is dead); and an orc whose attack target
 * (Combat+0x4c0) is Talion fights him. Anything not readable is aware (the ordinary hit).
 * The cue: one soft tick in the sword hand when an orc becomes a possible stealth kill (the nearest one within 2.5 m), again
 * only after 1.5 s without; the kill a strong pulse. The orcs' awareness changes are logged (the detection, the others
 * after a kill), the view's trace for 1.5 s after a kill.
 *   SOMVR_STEALTH_KILL_STANCE=either|crouch|slow  either: crouched or under _SLOW_CMS (90 cm/s, the body's ground speed)
 *   SOMVR_STEALTH_KILL_BEHIND_DEG=110  the player's angle off the orc's facing at least this (70 deg either side of behind)
 *   SOMVR_STEALTH_KILL_LOW_CM=-10, _HIGH_CM=100  the contact's height over the orc's root (its pelvis: the back, neck, head)
 *   SOMVR_STEALTH_KILL_STAB_MPS=0.8  a stab below combat's hit rule still counts (along the blade, cos >= 0.7)
 *   SOMVR_STEALTH_KILL_REC=A,B,...  the records in turn (default the silent family's B, B_2, B_3, B_4)
 *   SOMVR_STEALTH_KILL_CUE=0..1  the cue's strength (0.25; 0 = no cue)
 *   SOMVR_STEALTH_KILL_DAGGER=1  Acharn in the left hand (SOMVR_SLOTS' hip slot) kills too: its own sweep (4 cm back, a 30 cm
 *                                blade, combat's hit rule or the stab); the dagger deals no other hits
 * Test-only: SOMVR_STEALTH_KILL_MARKS=N (a bug mark the frame after each of the first N kills), the words `stealth
 * status|on|off|log` (SOMVR_HANDS_TEST for on/off/log). Lines: `CONFIG stealth ...`, `STEALTH ...`, `STEALTH END`. */

#define STL_NPC_CLASS 0x4c52780 /* the NPC game object class (re-combat-spawn.md; FUN_0229bbc0's compare) */
#define STL_HOBJ_CLASSES 0x48   /* HOBJECT: its class list (each entry: the class, +8 the next) */
#define STL_HOBJ_NPC 0x38       /* HOBJECT: the NPC */
#define STL_NPC_INFO 0x10       /* NPC: -> +0x7c its kind (4 = the kind with senses: FUN_0229bbc0) */
#define STL_INFO_KIND 0x7c
#define STL_NPC_AWARE 0xeb8     /* NPC: float, 0 = has not noticed (FUN_02972f70) */
#define STL_NPC_FLAGS 0x178     /* NPC: bit 0 (FUN_02972f70; on the CCharacter: dead) */
#define STL_ORCS 12
#define STL_RECS 6
#define STL_TRACE_S 1.5
#define STL_CUE_REARM_S 1.5

enum { STL_OFF = 0, STL_LOG = 1, STL_ON = 2 };
static const char *const g_stl_mode_w[] = { "off", "log", "on" };

struct stl_aw {
	int game;        /* FUN_0229bbc0's value: 1 aware, 0 not, -1 not readable */
	int engaged;     /* its attack target is Talion */
	int kind;        /* the NPC's kind (-1 not read) */
	float meter;     /* +0xeb8 */
	uintptr_t npc;
};
static struct {
	int mode, marks, tid;
	int start_mode; /* build 19: the mode at the start (the VR menu's Stealth kill row: mode = it or off) */
	somvr_stl_cfg cfg;
	float cue_amp;
	char recs[256];
	struct { char name[80]; uintptr_t rec; int state; } rec[STL_RECS];
	unsigned turn;
	somvr_stl_cue cue;
	/* the player's ground speed: the root sampled every 0.2 s */
	float sp_pos[3], speed_cms;
	double sp_t;
	/* the orcs seen: the last awareness, for the change lines */
	struct { uintptr_t c; int aware; float meter; double seen_t, unaware_t; } orc[STL_ORCS];
	/* after a kill: the others' awareness at +1 s and +3 s, the view's trace */
	uintptr_t killed_c;
	double kill_t, after1_t, after3_t, trace_until, mark_at, last_judge_t;
	float trace_root0[3], trace_eye0[3], trace_root_max, trace_eye_max;
	int trace_frames;
	/* the dagger in the left hand (SOMVR_SLOTS' hip slot): its own blade and sweep (the game's combat sweeps the sword) */
	int dagger;
	sc_blade_cfg dcfg;
	sc_cfg dsc;
	sc_blade_hist dhist[2];
	somvr_blade bl[2], bprev[2]; /* [SOMVR_HAND_L] the dagger, [SOMVR_HAND_R] the sword when combat has no blade */
	sc_speed bsp[2];
	int combat_blade;            /* combat_frame had its sword blade this frame (its contact loop judges the sword) */
	uint64_t dagger_frames, dagger_contacts, sword_frames, sword_contacts;
	uint64_t contacts, kills, not_killed, log_kills, verdicts[SOMVR_STL_N], cues, changes, frames;
	uint32_t lines;
	_Atomic int req_status, req_mode;
	/* test-only (docs 72): the perception records' dump, the approach's watch */
	_Atomic int req_dump, req_watch;
	int watch;
	double watch_t;
	uint32_t rlines;
	float chase_stick, chase_stop_cm;
	/* the stand-off (SOMVR_STEALTH_KILL_STANDOFF_CM): this tick's circles (the game thread), the cue's request */
	float so_cm;
	int so_n;
	float so_c[4][3], so_root[3], so_dt;
	uintptr_t so_orc[4], so_last_orc;
	double so_last_t, so_t;
	_Atomic int so_cue_req;
	uint64_t so_ticks, so_stops, silent_stims, silent_rolls;
	int silent, have_root;
	float frame_root[3];
	double silent_log_t;
	double so_cut_cm;
	int watch_hz;
	_Atomic int req_chase;
	uint8_t htab[256];
	int htab_ok;
	uintptr_t seen[160];
	int nseen;
} g_stl;

static void stl_sweep(int hand, uintptr_t pc, const struct cb_orc *o, int n, const float *root, double t);
static int stl_line(void) { return g_stl.lines < 600 ? (int)++g_stl.lines : 0; } /* a STEALTH line may go out */

/* the game's awareness of Talion (FUN_0229bbc0 read as memory), and whether it fights him */
static int stl_aware(const struct cb_orc *o, uintptr_t me, struct stl_aw *a)
{
	memset(a, 0, sizeof *a);
	a->game = -1, a->kind = -1, a->meter = NAN;
	uintptr_t h = o->hobj ? o->hobj : peek_ptr(o->c + C_HOBJ);
	a->engaged = me && peek_ptr(o->c + TH_C_TARGET) == me;
	uintptr_t l = h ? peek_ptr(h + STL_HOBJ_CLASSES) : 0, e = l ? peek_ptr(l) : 0;
	int npc = 0;
	for (int k = 0; e && k < 32 && !npc; k++)
		if (e == STL_NPC_CLASS)
			npc = 1;
		else
			e = peek_ptr(e + 8);
	if (!npc)
		return a->engaged ? 1 : -1;
	uintptr_t ch = peek_ptr(h + STL_HOBJ_NPC), inf = ch ? peek_ptr(ch + STL_NPC_INFO) : 0;
	int32_t kind = -1;
	float m = NAN;
	unsigned char fl = 0;
	a->npc = ch;
	if (!ch || !inf || !peek(inf + STL_INFO_KIND, &kind, 4) || !peek(ch + STL_NPC_AWARE, &m, 4) ||
	    !peek(ch + STL_NPC_FLAGS, &fl, 1))
		return a->engaged ? 1 : -1;
	a->kind = kind, a->meter = m;
	a->game = kind == 4 ? ((fl & 1) || m != 0.0f) : 0;
	return a->game || a->engaged;
}

static float stl_orc_yaw(const struct cb_orc *o)
{
	float q[4] = { 0, 0, 0, 1 };
	return peek(o->hobj + OBJ_ROT, q, sizeof q) && vec_ok(q, 2.0f) ? sv_heading(sv_qnormalize(sv_quat_of(q))) : 0;
}

static int stl_crouched(void) { return g_cr.held || g_cr.game == 1; }

static int stl_who(const struct cb_orc *o, uintptr_t pc, const float root[3], somvr_stl_who *w, struct stl_aw *a);
/* ---- test-only (docs/re-xr-pipeline.md 72): what the orcs' senses read. `stealth dump`: the player's and the nearest
 * orc's AI character records (their CCharacter +0x2a8; schema holder 0x4b95f20) - every attribute (its name hash, type,
 * values) and, for the senses' fields, the records they point to, three levels down (the hashes are named offline:
 * scripts/somhash.py's table 0x38d7480). `stealth watch on|off`: a line 5 times a second - the nearest orc's awareness
 * and Talion's state as the game's senses could read it. */
#define STL_AIREC 0x2a8      /* CCharacter: its AI character record (CCharacter::vf_104/105) */
#define STL_AI_HOLDER 0x4b95f20
#define STL_HTAB 0x38d7480
#define STL_CPLAYER_VT 0x39959a0 /* CPlayerObj's vtable (classes.tsv) */
/* Talion's CPlayerObj: his HOBJECT's +0x38 (as an NPC's), the vtable checked; 0 = not found */
static uintptr_t stl_cplayer(uintptr_t pc)
{
	uintptr_t me = pc ? peek_ptr(pc + C_HOBJ) : 0, p = me ? peek_ptr(me + STL_HOBJ_NPC) : 0;
	return p && vptr_of((void *)p) == STL_CPLAYER_VT ? p : 0;
}
static uint32_t stl_hash(const char *nm)
{
	if (!g_stl.htab_ok)
		g_stl.htab_ok = peek(STL_HTAB, g_stl.htab, sizeof g_stl.htab) ? 1 : -1;
	uint32_t h = 0;
	for (const unsigned char *c = (const unsigned char *)nm; g_stl.htab_ok > 0 && *c; c++)
		h = h * 0x397u + (uint32_t)(int8_t)g_stl.htab[*c];
	return h;
}
static int stl_rline(void) { return g_stl.rlines < 2500 ? (int)++g_stl.rlines : 0; }
static void stl_rdump(uintptr_t rec, int depth, int maxd, const uint32_t *follow, int nf, const char *who)
{
	unsigned char hdr[2], ents[256 * 16];
	char nm[96] = "";
	uintptr_t arr = rec ? peek_ptr(rec + 8) : 0, vals = rec ? peek_ptr(rec + 0x10) : 0;
	if (!rec || !arr || !peek(rec, hdr, 2) || !peek(arr, ents, (size_t)hdr[0] * 16))
		return;
	for (int k = 0; k < g_stl.nseen; k++)
		if (g_stl.seen[k] == rec) {
			if (stl_rline())
				logf_("STLREC %s %*s%#lx (above)", who, depth * 2, "", (unsigned long)rec);
			return;
		}
	if (g_stl.nseen < (int)(sizeof g_stl.seen / sizeof g_stl.seen[0]))
		g_stl.seen[g_stl.nseen++] = rec;
	rx_rec_name(rec, nm, sizeof nm);
	int n = hdr[0], keyoff = (hdr[1] & 1) ? 0 : 4;
	if (stl_rline())
		logf_("STLREC %s %*s%#lx '%s' %d attributes", who, depth * 2, "", (unsigned long)rec, nm, n);
	for (int r = 0; r < n; r++) {
		uint32_t h;
		memcpy(&h, ents + (size_t)r * 16 + (size_t)keyoff, 4);
		int idx = keyoff ? ents[(size_t)r * 16 + 9] : r;
		if (idx >= n)
			continue;
		const unsigned char *e = ents + (size_t)idx * 16;
		uint16_t cnt, vi;
		memcpy(&cnt, e + 0xc, 2), memcpy(&vi, e + 0xe, 2);
		int type = e[0xa] & 0x3f;
		char buf[400];
		int bl = 0;
		buf[0] = 0;
		uint64_t v[6] = { 0 };
		int k = cnt < 6 ? cnt : 6;
		if (type == 1) {
			uint32_t w = 0;
			if (vals && peek(vals + (uintptr_t)(vi >> 5) * 8, &w, 4))
				bl += snprintf(buf, sizeof buf, "%u", (w >> (vi & 31)) & 1u);
		} else if (vals && k > 0 && peek(vals + (uintptr_t)vi * 8, v, (size_t)k * 8)) {
			for (int j = 0; j < k && bl < (int)sizeof buf - 120; j++) {
				if (type == 2) {
					float f;
					uint32_t u = (uint32_t)v[j];
					memcpy(&f, &u, 4);
					bl += snprintf(buf + bl, sizeof buf - bl, "%s%g", j ? " " : "", (double)f);
				} else if (type == 0xa) {
					char rn[96] = "";
					rx_rec_name((uintptr_t)v[j], rn, sizeof rn);
					bl += snprintf(buf + bl, sizeof buf - bl, "%s'%s'", j ? " " : "", rn);
				} else {
					bl += snprintf(buf + bl, sizeof buf - bl, "%s%#llx", j ? " " : "", (unsigned long long)v[j]);
				}
			}
		}
		if (stl_rline())
			logf_("STLREC %s %*s  %08x t%d n%u = %s%s", who, depth * 2, "", h, type, cnt, buf, cnt > 6 ? " ..." : "");
		if (type != 0xa || depth >= maxd)
			continue;
		int go = !follow;
		for (int f = 0; f < nf && !go; f++)
			go = follow[f] == h;
		for (int j = 0; go && j < k; j++)
			stl_rdump((uintptr_t)v[j], depth + 1, maxd, NULL, 0, who);
	}
}
static void stl_dump(uintptr_t pcomb, uintptr_t pc, const float *root)
{
	static const char *const fn[] = { "CharacterVisibleStimulus", "CharacterAudibleStimulus",
		"CharacterSneakingAudibleStimulus", "SneakingPerceptionDistanceFactor", "Perception", "PerceptionTarget",
		"AlertnessProfile", "SensorSet", "SightDistanceBonusType", "HearingDistanceBonusType",
		"SneakingPerceptionDistanceBonusType" };
	uint32_t fh[16];
	int nf = (int)(sizeof fn / sizeof fn[0]);
	for (int i = 0; i < nf; i++)
		fh[i] = stl_hash(fn[i]);
	fh[nf++] = 0x7e402290; /* (unnamed: the observers' set, 'Default' -> 'Obscured', 'CloseTarget', 'FOV', ...) */
	uint32_t sch = 0;
	uintptr_t a = peek_ptr(STL_AI_HOLDER);
	if (a)
		peek(a + 0x30u * 0x2f + 8, &sch, 4);
	logf_("STLREC the hash table %s: 'CharacterSneakingAudibleStimulus' %08x, the schema's field 0x2f %08x (%s)",
	      g_stl.htab_ok > 0 ? "read" : "NOT read", fh[2], sch, fh[2] == sch ? "the same" : "DIFFERENT");
	g_stl.nseen = 0;
	uintptr_t pl = stl_cplayer(pc);
	logf_("STLREC the player's CPlayerObj %#lx: its AI record", (unsigned long)pl);
	if (pl)
		stl_rdump(peek_ptr(pl + STL_AIREC), 0, 7, fh, nf, "P");
	struct cb_orc oo[4];
	int no = pcomb && root ? cb_orcs_r(pcomb, pc, root, 4000.0f, oo, 4) : 0;
	for (int i = 0; i < no && i < 1; i++) {
		uintptr_t h = oo[i].hobj, npc = h ? peek_ptr(h + STL_HOBJ_NPC) : 0;
		logf_("STLREC the nearest orc %#lx at %.0f cm: its NPC %#lx (vtable %#lx): its AI record", (unsigned long)oo[i].c,
		      (double)oo[i].dist, (unsigned long)npc, (unsigned long)(npc ? vptr_of((void *)npc) : 0));
		if (npc)
			stl_rdump(peek_ptr(npc + STL_AIREC), 0, 7, fh, nf, "O");
	}
	logf_("STLREC end (%u lines)", g_stl.rlines);
}
/* the AI settings' PersonalBubbleDistance (schema holder 0x4b97240, field 1; the record FUN_02177080(), a getter, called once
 * after its first 16 bytes matched): CAIObserverPersonalBubble::vf_03 perceives a stimulus within it (3D, from the NPC's
 * +0xd2c) at once, no sneaking factor */
static float stl_bubble_cm(void)
{
	static int state;
	static float v = NAN;
	if (!state) {
		static const unsigned char want[16] = { 0x48, 0x83, 0xec, 0x08, 0xe8, 0xd7, 0x6b, 0xfd,
							0xff, 0x48, 0x85, 0xc0, 0x74, 0x1a, 0xe8, 0x0d };
		unsigned char got[16];
		state = -1;
		if (peek(0x2177080, got, sizeof got) && !memcmp(got, want, sizeof want)) {
			uintptr_t rec = ((uintptr_t (*)(void))0x2177080)();
			char nm[96] = "";
			rx_rec_name(rec, nm, sizeof nm);
			v = rx_db_float(rec, 0x4b97240, 1, NAN), state = 1;
			logf_("STEALTH the AI settings %#lx '%s': PersonalBubbleDistance %.1f, ThreatTooCloseDistance %.1f",
			      (unsigned long)rec, nm, (double)v, (double)rx_db_float(rec, 0x4b97240, 2, NAN));
		} else {
			logf_("STEALTH the AI settings: FUN_02177080's bytes differ: not called");
		}
	}
	return v;
}
/* `stealth chase STICK STOP_CM` (a test, SOMVR_HANDS_TEST): the test stick forward at STICK toward the nearest orc within
 * 40 m (snap turns at the head toward it, as `execute chase`), stopped at STOP_CM from it (0 = off) */
static void stl_chase(uintptr_t pcomb, uintptr_t pc, const float *root, double t)
{
	static double snap_t;
	struct cb_orc oo[2];
	int no = pcomb && root ? cb_orcs_r(pcomb, pc, root, 4000.0f, oo, 2) : 0;
	sv_vec3 hp;
	sv_quat hq;
	somvr_rig_head(&g_rig, g_fr.base, g_fr.views.pose, &hp, &hq);
	float head[3] = { hp.x, hp.y, hp.z }, hqv[4] = { hq.x, hq.y, hq.z, hq.w }, l[3] = { 0, 0, 1 };
	if (no)
		somvr_th_local(head, hqv, oo[0].pos, l);
	float br = no ? somvr_th_direction(l).yaw_deg : 0;
	int steps = no && fabsf(br) > 20 && t - snap_t > 0.3 ? (int)lrintf(br / (g_rigcfg.snap_rad * 57.29578f)) : 0;
	if (steps)
		atomic_fetch_add(&g_req_snap, steps), snap_t = t;
	float fw = no && oo[0].dist > g_stl.chase_stop_cm && fabsf(br) < 45 ? g_stl.chase_stick : 0;
	if (g_stl.chase_stick > 0 && no && oo[0].dist <= g_stl.chase_stop_cm) {
		logf_("STEALTH t=%.3f chase: stopped %.0f cm from orc %#lx", t, (double)oo[0].dist, (unsigned long)oo[0].c);
		g_stl.chase_stick = 0, fw = 0;
	}
	uint32_t b0 = 0, b1;
	memcpy(&b1, &fw, 4);
	atomic_store_explicit(&g_tstick[0], b0, memory_order_relaxed);
	atomic_store_explicit(&g_tstick[1], b1, memory_order_relaxed);
	atomic_store_explicit(&g_tstick[2], b0, memory_order_relaxed);
	atomic_store_explicit(&g_tstick_until_ns, fw > 0 ? (int64_t)((mono() + 0.2) * 1e9) : 0, memory_order_release);
}
/* the watch: the nearest orc within 25 m and Talion, 5 times a second */
static void stl_watch(uintptr_t pcomb, uintptr_t pc, const float *root, double t)
{
	static double asked0, applied0;
	if (t - g_stl.watch_t < 1.0 / (g_stl.watch_hz > 0 ? g_stl.watch_hz : 5))
		return;
	g_stl.watch_t = t;
	struct cb_orc oo[2];
	int no = pcomb && root ? cb_orcs_r(pcomb, pc, root, 2500.0f, oo, 2) : 0;
	uintptr_t pl = stl_cplayer(pc), mgr = pl ? peek_ptr(pl + 0xb60) : 0, it = mgr ? peek_ptr(mgr + 0x638) : 0;
	int32_t d64 = -1, d68 = -1;
	uint32_t ps[2] = { 0, 0 }, os[2] = { 0, 0 };
	uintptr_t it10 = it && it != mgr + 0x630 ? peek_ptr(it + 0x10) : 0, c6d0 = pl ? peek_ptr(pl + 0x6d0) : 0;
	unsigned char b569 = 0xff;
	if (pl)
		peek(pl + 0xd64, &d64, 4), peek(pl + 0xd68, &d68, 4), peek(pl + 0x2d4, ps, 8);
	if (c6d0)
		peek(c6d0 + 0x569, &b569, 1);
	int sneak_stim = !mgr ? -1 : (it == mgr + 0x630 || !it10 || d64 != 0);
	double da = g_lo.s.asked_cm - asked0, dp = g_lo.s.applied_cm - applied0;
	asked0 = g_lo.s.asked_cm, applied0 = g_lo.s.applied_cm;
	char ob[400] = "no orc within 25 m";
	if (no > 0) {
		somvr_stl_who w;
		struct stl_aw a;
		stl_who(&oo[0], pc, root, &w, &a);
		uintptr_t perc = a.npc ? peek_ptr(a.npc + 0xc80) : 0;
		unsigned char use = 0xff;
		float fac = NAN, ep[3] = { NAN, NAN, NAN };
		if (a.npc)
			peek(a.npc + 0x2d4, os, 8), peek(a.npc + 0xd2c, ep, sizeof ep);
		if (perc)
			peek(perc + 0x328, &use, 1), peek(perc + 0x32c, &fac, 4);
		snprintf(ob, sizeof ob, "orc %#lx %.0f cm %+.0f deg: aware %d (kind %d, +0xeb8 %.3f, fights %d); its stimuli %08x/%08x; "
			 "its perception %#lx: the sneak factor on %d, %.2f; its eye (+0xd2c) %+.0f cm over its root, 3D to Talion's "
			 "head %.0f (%.0f over his root), root %.0f, chest %.0f (the bubble %.0f)", (unsigned long)oo[0].c, w.dist_cm,
			 w.player_off_deg, w.aware, a.kind, (double)a.meter, a.engaged, os[0], os[1], (unsigned long)perc, use,
			 (double)fac, (double)(ep[1] - oo[0].pos[1]), (double)dist3(ep, g_fr.head),
			 (double)(g_fr.head[1] - root[1]), (double)dist3(ep, root),
			 (double)dist3(ep, (const float[3]){ root[0], root[1] + 0.5f * (g_fr.head[1] - root[1]), root[2] }),
			 (double)stl_bubble_cm());
	}
	logf_("STEALTH watch t=%.3f %s | Talion %s (held %d, read %d) %.0f cm/s, moved %.1f of %.1f cm asked; the game's sneaking "
	      "%s (+0xd68 %d, +0x6d0 -> +0x569 %d); +0xd64 %d, the list %s (it %#lx +0x10 %#lx) -> the %s audible stimulus; his "
	      "stimuli %08x/%08x", t, ob, stl_crouched() ? "crouched" : "standing", g_cr.held, g_cr.game,
	      (double)g_stl.speed_cms, dp, da, !pl ? "unread" : (d68 != 0 || (b569 != 0 && b569 != 0xff)) ? "YES" : "no", d68,
	      b569 == 0xff ? -1 : b569, d64, !mgr ? "unread" : it == mgr + 0x630 ? "empty" : "set", (unsigned long)it,
	      (unsigned long)it10, sneak_stim < 0 ? "?" : sneak_stim ? "sneaking" : "ordinary", ps[0], ps[1]);
}

/* the orc as a target now: who (its awareness, the player's angle and distance) */
static int stl_who(const struct cb_orc *o, uintptr_t pc, const float root[3], somvr_stl_who *w, struct stl_aw *a)
{
	uintptr_t me = pc ? peek_ptr(pc + C_HOBJ) : 0;
	w->aware = stl_aware(o, me, a);
	if (w->aware == 0 && cb_sync(o->c))
		w->aware = 1; /* in a paired action (a grab, a sync with someone): not a stealth kill's target */
	w->crouched = stl_crouched();
	w->speed_cms = g_stl.speed_cms;
	w->player_off_deg = somvr_stl_off_deg(root, o->pos, stl_orc_yaw(o));
	float dx = root[0] - o->pos[0], dz = root[2] - o->pos[2];
	w->dist_cm = sqrtf(dx * dx + dz * dz);
	w->dead = cb_dead(o->c);
	/* grabbed from behind while it had not noticed (SOMVR_VERBS' left hand on its shoulder or head): still a stealth
	 * kill for 3 s after it was last unaware - the grab turns it, its awareness rises with the hold */
	for (int k = 0; k < STL_ORCS; k++)
		if (g_stl.orc[k].c == o->c && g_vb.hand.phase == SOMVR_VBH_GRAB && g_vb.held_c == o->c && g_stl.orc[k].unaware_t > 0 &&
		    cb_now() - g_stl.orc[k].unaware_t <= 3.0 && !w->dead) {
			w->aware = 0, w->player_off_deg = 180.0f;
			return 2;
		}
	return 1;
}

/* SOMVR_STEALTH_KILL_SILENT (default 1): a swing that heads at an orc which is a stealth kill's target now (the game's
 * unaware, Talion within the back arc, crouched or slow: somvr_stl_possible) registers no attack stimulus
 * (cb_stim_start's FUN_026c7180, the call Combat_StartAttack makes) and rolls no block (SOMVR_COMBAT_REACT): the stock
 * stealth kill starts no attack either, and the stimulus turned the orc ~0.2 s after the wind-up (run 013422:
 * `ReactToSusp_FromBase`), before the blade landed. Any other swing, as before. */
static int stl_silent(uintptr_t pc, const struct cb_orc *o, double t, const char *what)
{
	if (g_stl.mode != STL_ON || !g_stl.silent || !g_stl.have_root || !pc)
		return 0;
	somvr_stl_who w;
	struct stl_aw a;
	stl_who(o, pc, g_stl.frame_root, &w, &a);
	if (somvr_stl_possible(&g_stl.cfg, &w) != SOMVR_STL_KILL)
		return 0;
	if (what[4] == 'a')
		g_stl.silent_stims++;
	else
		g_stl.silent_rolls++;
	if (t - g_stl.silent_log_t > 0.5 && stl_line()) {
		g_stl.silent_log_t = t;
		logf_("STEALTH t=%.3f a swing at orc %#lx (unaware, Talion %+.0f deg off its facing at %.0f cm, %s): no %s "
		      "(SOMVR_STEALTH_KILL_SILENT)", t, (unsigned long)o->c, w.player_off_deg, w.dist_cm,
		      w.crouched ? "crouched" : "slow", what);
	}
	return 1;
}

#define STL_SO_EASE_HZ 8.0f /* the stand-off's stop: the step toward the edge at most room x (1 - e^(-dt x 8)) */
/* SOMVR_STEALTH_KILL_STANDOFF_CM (the PM hook, the game thread, after the core's step): crouched (the game's stealth
 * crouch) in the back half of an orc that has not noticed Talion, within 3 m: his move this tick (stick, steps, dashes)
 * is kept outside the stand-off circle around it - the game notices a Talion inside ~108 cm root to root, crouched or
 * not (docs 72.1). Only the part toward the orc is cut (somvr_loco_take_assist: delta, v and the read-back alike), eased
 * over the last ~20 cm; sideways and back are free; nothing is ever pushed. Standing, in front of it, or an orc that
 * has noticed him: no stand-off (the game's own rules). */
static void stl_so_apply(somvr_loco *s, somvr_loco_out *o, double t, int follow)
{
	for (int i = 0; i < g_stl.so_n; i++) {
		float d[3] = { o->delta.x, 0, o->delta.z }, corr[3];
		if (!somvr_stl_standoff(g_stl.so_root, g_stl.so_c[i], g_stl.so_cm, g_stl.so_dt, STL_SO_EASE_HZ, d, corr))
			continue;
		somvr_loco_take_assist(s, o, (sv_vec3){ corr[0], 0, corr[2] }, g_stl.so_dt);
		g_stl.so_cut_cm += sqrtf(corr[0] * corr[0] + corr[2] * corr[2]);
		g_stl.so_ticks++;
		if (follow)
			continue;
		float dx = g_stl.so_root[0] - g_stl.so_c[i][0], dz = g_stl.so_root[2] - g_stl.so_c[i][2];
		if (g_stl.so_orc[i] != g_stl.so_last_orc || t - g_stl.so_last_t > STL_CUE_REARM_S) {
			g_stl.so_stops++, g_stl.so_cue_req = 1;
			if (stl_line())
				logf_("STEALTH t=%.3f stand-off: orc %#lx (unaware, Talion in its back half) at %.0f cm: the walk stops "
				      "at %.0f cm (the game notices inside ~108 cm)", t, (unsigned long)g_stl.so_orc[i],
				      sqrtf(dx * dx + dz * dz), (double)g_stl.so_cm);
		}
		g_stl.so_last_orc = g_stl.so_orc[i], g_stl.so_last_t = t;
	}
}
static void stl_standoff(uintptr_t pcomb, uintptr_t sc, const float *root, somvr_loco *s, somvr_loco_out *o, float dt,
			 double t)
{
	g_stl.so_n = 0;
	if (g_stl.mode != STL_ON || !(g_stl.so_cm > 0) || !pcomb || !sc || !root || !stl_crouched())
		return;
	struct cb_orc oo[4];
	int no = cb_orcs_r(pcomb, sc, root, 300.0f, oo, 4);
	for (int i = 0; i < no; i++) {
		somvr_stl_who w;
		struct stl_aw a;
		stl_who(&oo[i], sc, root, &w, &a);
		if (w.aware != 0 || w.dead || fabsf(w.player_off_deg) < 90.0f)
			continue;
		memcpy(g_stl.so_c[g_stl.so_n], oo[i].pos, sizeof g_stl.so_c[0]);
		g_stl.so_orc[g_stl.so_n++] = oo[i].c;
	}
	if (!g_stl.so_n)
		return;
	memcpy(g_stl.so_root, root, sizeof g_stl.so_root);
	g_stl.so_dt = dt, g_stl.so_t = t;
	stl_so_apply(s, o, t, 0);
}
/* the follow's request merged into the same tick's delta (lo_take_follow): the same circles */
static void stl_standoff_follow(somvr_loco *s, somvr_loco_out *o)
{
	if (g_stl.so_n)
		stl_so_apply(s, o, g_stl.so_t, 1);
}

/* every combat frame with a blade (after the orcs are read): the speed, the awareness lines, the cue, the after-kill
 * checks and the view's trace */
static void stl_frame(uintptr_t pcomb, uintptr_t pc, const struct cb_orc *o, int n, const float *root, double t)
{
	if (!g_stl.mode)
		return;
	g_stl.frames++;
	int cbl = g_stl.combat_blade;
	g_stl.combat_blade = 1;
	if (g_stl.req_mode) {
		int m = g_stl.req_mode - 1;
		g_stl.req_mode = 0;
		logf_("STEALTH t=%.3f mode %s -> %s (the word)", t, g_stl_mode_w[g_stl.mode], g_stl_mode_w[m]);
		g_stl.mode = m;
		if (!m)
			return;
	}
	g_stl.have_root = root != NULL;
	if (!root)
		return;
	memcpy(g_stl.frame_root, root, sizeof g_stl.frame_root);
	if (g_stl.sp_t <= 0 || t - g_stl.sp_t > 1.0) {
		memcpy(g_stl.sp_pos, root, sizeof g_stl.sp_pos), g_stl.sp_t = t, g_stl.speed_cms = NAN;
	} else if (t - g_stl.sp_t >= 0.2) {
		float dx = root[0] - g_stl.sp_pos[0], dz = root[2] - g_stl.sp_pos[2];
		g_stl.speed_cms = sqrtf(dx * dx + dz * dz) / (float)(t - g_stl.sp_t);
		memcpy(g_stl.sp_pos, root, sizeof g_stl.sp_pos), g_stl.sp_t = t;
	}
	/* the orcs: awareness changes, the nearest possible stealth kill */
	uintptr_t best = 0;
	float best_d = 1e9f;
	for (int i = 0; i < n; i++) {
		somvr_stl_who w;
		struct stl_aw a;
		stl_who(&o[i], pc, root, &w, &a);
		int k, fr = -1;
		for (k = 0; k < STL_ORCS && g_stl.orc[k].c != o[i].c; k++)
			if (fr < 0 && (!g_stl.orc[k].c || t - g_stl.orc[k].seen_t > 30.0))
				fr = k;
		if (k == STL_ORCS && fr >= 0) {
			k = fr;
			memset(&g_stl.orc[k], 0, sizeof g_stl.orc[k]);
			g_stl.orc[k].c = o[i].c, g_stl.orc[k].aware = -2;
		}
		if (k < STL_ORCS) {
			if (g_stl.orc[k].aware != w.aware && !w.dead) {
				g_stl.changes++;
				if (stl_line())
					logf_("STEALTH t=%.3f aware orc %#lx: %d -> %d (the game's %d: kind %d, +0xeb8 %.3f; fights Talion %d; "
					      "in a sync %d) at %.0f cm, %+.0f deg off its facing; Talion %s, %.0f cm/s", t,
					      (unsigned long)o[i].c, g_stl.orc[k].aware, w.aware, a.game, a.kind, (double)a.meter, a.engaged,
					      cb_sync(o[i].c), w.dist_cm, w.player_off_deg, w.crouched ? "crouched" : "standing",
					      (double)w.speed_cms);
				g_stl.orc[k].aware = w.aware;
			}
			g_stl.orc[k].meter = a.meter, g_stl.orc[k].seen_t = t;
			if (w.aware == 0 && !w.dead)
				g_stl.orc[k].unaware_t = t;
		}
		if (w.dist_cm <= g_stl.cfg.near_cm && somvr_stl_possible(&g_stl.cfg, &w) == SOMVR_STL_KILL && w.dist_cm < best_d)
			best = o[i].c, best_d = w.dist_cm;
	}
	stl_sweep(SOMVR_HAND_L, pc, o, n, root, t);
	if (!cbl)
		stl_sweep(SOMVR_HAND_R, pc, o, n, root, t);
	if (somvr_stl_cue_step(&g_stl.cue, best, best != 0, t, STL_CUE_REARM_S)) {
		g_stl.cues++;
		if (g_stl.cue_amp > 0 && g_stl.mode == STL_ON)
			in_haptic_event(SOMVR_HAND_R, g_stl.cue_amp, 0.03f, SOMVR_HAP_COMBAT);
		if (stl_line())
			logf_("STEALTH t=%.3f possible: orc %#lx at %.0f cm, Talion %s at %.0f cm/s: the cue (%.2f, 30 ms)", t,
			      (unsigned long)best, best_d, stl_crouched() ? "crouched" : "standing", (double)g_stl.speed_cms,
			      (double)g_stl.cue_amp);
	}
	if (g_stl.so_cue_req) { /* the stand-off held him at arm's reach: one soft tick (the cue's) */
		g_stl.so_cue_req = 0;
		if (g_stl.cue_amp > 0)
			in_haptic_event(SOMVR_HAND_R, g_stl.cue_amp, 0.03f, SOMVR_HAP_COMBAT);
	}
	/* after a kill: did the others notice? (+1 s, +3 s: every live orc within 40 m) */
	for (int pass = 0; pass < 2; pass++) {
		double *at = pass ? &g_stl.after3_t : &g_stl.after1_t;
		if (*at <= 0 || t < *at)
			continue;
		*at = 0;
		struct cb_orc oo[16];
		int no = pcomb ? cb_orcs_r(pcomb, pc, root, 4000.0f, oo, 16) : 0, others = 0, aw = 0;
		char buf[600];
		int bl = 0;
		buf[0] = 0;
		for (int i = 0; i < no; i++) {
			if (oo[i].c == g_stl.killed_c || cb_dead(oo[i].c))
				continue;
			struct stl_aw a;
			int v = stl_aware(&oo[i], pc ? peek_ptr(pc + C_HOBJ) : 0, &a);
			others++, aw += v == 1;
			if (bl < (int)sizeof buf - 90)
				bl += snprintf(buf + bl, sizeof buf - bl, "%s orc %#lx %.0f cm aware %d (+0xeb8 %.3f, fights %d)",
					       others > 1 ? ";" : "", (unsigned long)oo[i].c, oo[i].dist, v, (double)a.meter, a.engaged);
		}
		logf_("STEALTH t=%.3f after the kill +%.0f s: the killed orc %#lx dead %d; %d other live orcs within 40 m, %d aware%s%s",
		      t, pass ? 3.0 : 1.0, (unsigned long)g_stl.killed_c, cb_dead(g_stl.killed_c), others, aw, others ? ":" : "", buf);
	}
	/* the view's trace: Talion's root and the view's anchor against the kill's frame */
	if (g_stl.trace_until > 0) {
		float dr = dist3(root, g_stl.trace_root0), de = dist3(g_lo.anchor_pos, g_stl.trace_eye0);
		g_stl.trace_root_max = fmaxf(g_stl.trace_root_max, dr), g_stl.trace_eye_max = fmaxf(g_stl.trace_eye_max, de);
		g_stl.trace_frames++;
		if (t >= g_stl.trace_until) {
			g_stl.trace_until = 0;
			logf_("STEALTH t=%.3f trace %.1f s after the kill (%d frames): Talion's root moved at most %.1f cm, the view's "
			      "anchor %.1f cm (the driver stood still: 0 = no game motion)", t, STL_TRACE_S, g_stl.trace_frames,
			      (double)g_stl.trace_root_max, (double)g_stl.trace_eye_max);
		}
	}
	if (g_stl.mark_at > 0 && t >= g_stl.mark_at) {
		g_stl.mark_at = 0;
		if (g_stl.marks > 0) {
			g_stl.marks--;
			ss_mark("SOMVR_STEALTH_KILL_MARKS", "the stealth kill (the next frame)", t);
		}
	}
	if (g_stl.req_dump) {
		g_stl.req_dump = 0;
		stl_dump(pcomb, pc, root);
	}
	if (g_stl.req_watch) {
		g_stl.watch = g_stl.req_watch == 2, g_stl.req_watch = 0;
		logf_("STEALTH t=%.3f watch %s (the word)", t, g_stl.watch ? "on" : "off");
	}
	if (g_stl.watch)
		stl_watch(pcomb, pc, root, t);
	if (g_stl.req_chase) {
		g_stl.req_chase = 0;
		logf_("STEALTH t=%.3f chase %s (the word: stick %.2f, stop at %.0f cm)", t, g_stl.chase_stick > 0 ? "on" : "off",
		      (double)g_stl.chase_stick, (double)g_stl.chase_stop_cm);
		if (g_stl.chase_stick <= 0)
			atomic_store_explicit(&g_tstick_until_ns, 0, memory_order_release);
	}
	if (g_stl.chase_stick > 0)
		stl_chase(pcomb, pc, root, t);
	if (g_stl.req_status) {
		g_stl.req_status = 0;
		struct cb_orc oo[8];
		int no = pcomb ? cb_orcs_r(pcomb, pc, root, 4000.0f, oo, 8) : 0;
		logf_("STEALTH t=%.3f status: mode %s, Talion %s (crouch held %d, the game's read %d) at %.0f cm/s; %d orcs within 40 m",
		      t, g_stl_mode_w[g_stl.mode], stl_crouched() ? "crouched" : "standing", g_cr.held, g_cr.game,
		      (double)g_stl.speed_cms, no);
		for (int i = 0; i < no; i++) {
			somvr_stl_who w;
			struct stl_aw a;
			stl_who(&oo[i], pc, root, &w, &a);
			logf_("STEALTH   orc %#lx at %.0f cm, %+.0f deg off its facing: aware %d (the game's %d: kind %d, +0xeb8 %.3f, "
			      "flags-npc %#lx; fights Talion %d), dead %d: %s", (unsigned long)oo[i].c, w.dist_cm, w.player_off_deg,
			      w.aware, a.game, a.kind, (double)a.meter, (unsigned long)a.npc, a.engaged, w.dead,
			      somvr_stl_why(somvr_stl_possible(&g_stl.cfg, &w)));
		}
	}
}

static uintptr_t stl_rec(void)
{
	char nm[80];
	for (int tries = 0; tries < STL_RECS; tries++) {
		if (!somvr_ex_pick(g_stl.recs, g_stl.turn++, nm, sizeof nm))
			return 0;
		int k, fr = -1;
		for (k = 0; k < STL_RECS && strcmp(g_stl.rec[k].name, nm); k++)
			if (fr < 0 && !g_stl.rec[k].name[0])
				fr = k;
		if (k == STL_RECS) {
			if (fr < 0)
				return 0;
			k = fr;
			snprintf(g_stl.rec[k].name, sizeof g_stl.rec[k].name, "%s", nm);
		}
		if (!g_stl.rec[k].state) {
			uintptr_t cat = peek_ptr(g_cbg.atkcat_pp);
			uint32_t cnt = 0;
			if (!cat || !peek(cat + CAT_COUNT, &cnt, sizeof cnt) || !cnt)
				return 0; /* the database is not loaded yet */
			g_stl.rec[k].rec = cb_rec_valid(CB_FN(CS_ATK_BYNAME, cb_byname_fn)(nm));
			g_stl.rec[k].state = g_stl.rec[k].rec ? 1 : -1;
			logf_("STEALTH record '%s': %s %#lx", nm, g_stl.rec[k].rec ? "found" : "NOT in Combat/Attack (skipped)",
			      (unsigned long)g_stl.rec[k].rec);
		}
		if (g_stl.rec[k].state > 0)
			return g_stl.rec[k].rec;
	}
	return 0;
}

/* a blade's contact with an orc (the sword's from combat_frame, the dagger's from stl_frame): 1 = the stealth kill took it */
static int stl_strike(uintptr_t pc, const struct cb_orc *o, const sc_contact *ct, int tier, const somvr_blade *b,
		      const sc_speed *sp, int hand, const float *root, double t)
{
	if (!g_stl.mode || !root || !pc)
		return 0;
	if (g_vb.hand.phase == SOMVR_VBH_GRAB && g_vb.held_c == o->c && g_stl.killed_c == o->c)
		return 1;
	g_stl.contacts++;
	somvr_stl_who w;
	struct stl_aw a;
	int grabbed = stl_who(o, pc, root, &w, &a) == 2;
	somvr_stl_hit h = { somvr_stl_off_deg(ct->point, o->pos, stl_orc_yaw(o)), ct->point[1] - o->pos[1], tier,
			    sp->axis_mps, sp->cos_axis };
	if (grabbed)
		h.contact_off_deg = 180.0f; /* held from behind: wherever the blade goes in */
	int v = somvr_stl_judge(&g_stl.cfg, &w, &h);
	g_stl.verdicts[v]++;
	if (v != SOMVR_STL_KILL || g_stl.mode == STL_LOG) {
		/* the ordinary hit; a line when it was close (unaware, or behind) - from the front the normal fight */
		if ((w.aware == 0 || fabsf(w.player_off_deg) >= g_stl.cfg.behind_deg || v == SOMVR_STL_KILL) &&
		    t - g_stl.last_judge_t > 0.3 && stl_line()) {
			g_stl.last_judge_t = t;
			if (v == SOMVR_STL_KILL)
				g_stl.log_kills++;
			logf_("STEALTH t=%.3f contact (%s) orc %#lx: %s%s (aware %d: the game's %d +0xeb8 %.3f fights %d; Talion %+.0f deg "
			      "off its facing at %.0f cm, %s, %.0f cm/s; the contact %+.0f deg, %.0f cm up; tier %s, along %.2f m/s cos "
			      "%.2f)", t, hand == SOMVR_HAND_L ? "the dagger" : sp == &g_cb.sp ? "the sword" : "the sword, combat without a blade", (unsigned long)o->c, somvr_stl_why(v), v == SOMVR_STL_KILL ? " - log only: the ordinary hit" :
			      ": the ordinary hit", w.aware, a.game, (double)a.meter, a.engaged, w.player_off_deg, w.dist_cm,
			      w.crouched ? "crouched" : "standing", (double)w.speed_cms, h.contact_off_deg, h.contact_up_cm,
			      sc_tier_name(tier), (double)h.axis_mps, (double)h.cos_axis);
		}
		return 0;
	}
	uintptr_t rec = stl_rec();
	if (!rec || !g_cb.ok_hit || g_cb.mode != CB_FULL) {
		logf_("STEALTH t=%.3f contact orc %#lx: the stealth kill, but %s: the ordinary hit", t, (unsigned long)o->c,
		      !rec ? "no stealth kill record in the database" : "combat cannot deal hits (see its CONFIG)");
		return 0;
	}
	/* the hit: the stock silent stealth kill's record, forceKill; the direction the blade's (or Talion -> the orc) */
	float vv = sqrtf(b->vel_tip[0] * b->vel_tip[0] + b->vel_tip[1] * b->vel_tip[1] + b->vel_tip[2] * b->vel_tip[2]);
	float dir[3] = { o->pos[0] - root[0], 0, o->pos[2] - root[2] }, hd0[3];
	if (vv > 1.0f)
		dir[0] = b->vel_tip[0] / vv, dir[1] = b->vel_tip[1] / vv, dir[2] = b->vel_tip[2] / vv;
	else {
		float l = sqrtf(dir[0] * dir[0] + dir[2] * dir[2]);
		dir[0] = l > 1 ? dir[0] / l : 0, dir[2] = l > 1 ? dir[2] / l : 1;
	}
	unsigned char once = 0;
	peek(pc + C_HITDIR, hd0, sizeof hd0), peek(pc + C_HITONCE, &once, 1);
	memcpy((void *)(pc + C_HITDIR), dir, sizeof dir);
	*(volatile unsigned char *)(pc + C_HITONCE) = 0;
	uintptr_t dslot = 0;
	uint64_t dwas = 0;
	char death[160] = "the record's own RecoilTypeHit";
	if (g_kd.mode == KD_TYPE) { /* SOMVR_COMBAT_DEATH's type for this kill too (as the executions') */
		uintptr_t ad = g_df.ok_ad ? DF_FN(DFS_ATTACKDATA, cb_attackdata_fn)(rec) : 0;
		uintptr_t sl = ad ? kx_slot(ad, g_dfg.sch_ad, DF_AD_RECOIL_HIT) : 0, r = kx_find(g_kd.type);
		uint64_t want = (uint64_t)r;
		if (sl && r && peek(sl, &dwas, 8) && (dwas == want || arm_poke(sl, &want, 8))) {
			dslot = dwas == want ? 0 : sl;
			snprintf(death, sizeof death, "RecoilTypeHit '%s' (SOMVR_COMBAT_DEATH)", g_kd.type);
		} else {
			snprintf(death, sizeof death, "no '%s': the record's own", g_kd.type);
		}
	}
	snprintf(g_kd.note, sizeof g_kd.note, "the stealth kill: %s", death);
	g_kd.pre_c = o->c, g_kd.pre_dead = cb_dead(o->c);
	int dead0 = cb_dead(o->c);
	float hp0 = cb_health(o->c);
	char rn[96] = "?";
	rx_rec_name(rec, rn, sizeof rn);
	CB_FN(CS_DEALHIT, cb_dealhit_fn)((cb_ltvec){ ct->point[0], ct->point[1], ct->point[2] }, pc, o->hobj, rec, 1);
	kx_hit_back(dslot, dwas);
	memcpy((void *)(pc + C_HITDIR), hd0, sizeof hd0);
	*(volatile unsigned char *)(pc + C_HITONCE) = once;
	int dead1 = cb_dead(o->c);
	float hp1 = cb_health(o->c);
	if (dead1 && !dead0)
		g_stl.kills++, CB_COUNT(kills);
	else
		g_stl.not_killed++;
	CB_COUNT(calls);
	in_haptic_event(hand, 0.9f, 0.25f, SOMVR_HAP_COMBAT);
	logf_("STEALTH kill t=%.3f by %s, orc %#lx: Combat_DealHit(pos=%.1f,%.1f,%.1f, rec='%s' %#lx, forceKill=1) dead=%d->%d health "
	      "%.1f->%.1f; %s (the game's aware %d +0xeb8 %.3f, fights %d), Talion %+.0f deg off its facing at %.0f cm, %s, %.0f "
	      "cm/s; the contact %+.0f deg, %.0f cm up, tier %s, along %.2f m/s cos %.2f; the death: %s", t,
	      hand == SOMVR_HAND_L ? "the dagger" : "the sword", (unsigned long)o->c,
	      ct->point[0], ct->point[1], ct->point[2], rn, (unsigned long)rec, dead0, dead1, hp0, hp1,
	      grabbed ? "held from behind (the left hand)" : "unaware", a.game, (double)a.meter, a.engaged, w.player_off_deg,
	      w.dist_cm, w.crouched ? "crouched" : "standing", (double)w.speed_cms, h.contact_off_deg, h.contact_up_cm,
	      sc_tier_name(tier), (double)h.axis_mps, (double)h.cos_axis, death);
	kb_note(o->c, o->hobj, rec, "a stealth kill", t); /* COMBAT death orc ...: where the body goes */
	g_stl.killed_c = o->c, g_stl.kill_t = t, g_stl.after1_t = t + 1.0, g_stl.after3_t = t + 3.0;
	memcpy(g_stl.trace_root0, root, sizeof g_stl.trace_root0);
	memcpy(g_stl.trace_eye0, g_lo.anchor_pos, sizeof g_stl.trace_eye0);
	g_stl.trace_root_max = g_stl.trace_eye_max = 0, g_stl.trace_frames = 0, g_stl.trace_until = t + STL_TRACE_S;
	if (g_stl.marks > 0)
		g_stl.mark_at = t + 0.001;
	return 1;
}

static int stl_contact(uintptr_t pcomb, uintptr_t pc, const struct cb_orc *o, const sc_contact *ct, int tier,
		       const somvr_blade *b, const float *root, double t)
{
	(void)pcomb;
	return stl_strike(pc, o, ct, tier, b, &g_cb.sp, SOMVR_HAND_R, root, t);
}

/* in_frame (a stereo frame, the left grip): the dagger's blade and speed (as SOMVR_FINISH's fi_hands) */
static void stl_hands(const sv_vec3 p[2], const sv_quat q[2], const sv_vec3 v[2], const sv_vec3 w[2])
{
	if (!g_stl.mode)
		return;
	for (int h = 0; h < 2; h++) {
		const somvr_hand *H = &g_inp.hands[h];
		sc_grip g;
		memset(&g, 0, sizeof g);
		if (H->valid) {
			g.valid = 1, g.vel_valid = (H->vel_valid & 3) == 3, g.xr_t = (double)H->time * 1e-9;
			g.pos[0] = p[h].x, g.pos[1] = p[h].y, g.pos[2] = p[h].z;
			sv_quat_to(q[h], g.quat);
			g.vel[0] = v[h].x, g.vel[1] = v[h].y, g.vel[2] = v[h].z, g.avel[0] = w[h].x, g.avel[1] = w[h].y, g.avel[2] = w[h].z;
		}
		sc_blade_cfg bcf = h == SOMVR_HAND_L ? g_stl.dcfg : g_cb.bcfg; /* the sword: combat's blade, x the body's scale */
		if (h == SOMVR_HAND_R) {
			float bs = sz_body_scale();
			bcf.back_cm *= bs, bcf.len_cm *= bs, bcf.ofs_cm[0] *= bs, bcf.ofs_cm[1] *= bs, bcf.ofs_cm[2] *= bs;
		}
		somvr_blade nb;
		somvr_blade_controller(&bcf, H->valid ? &g : NULL, &g_inp.rigxf, cb_now(), &g_stl.dhist[h], &nb);
		g_stl.bl[h] = nb;
		if (nb.valid)
			sc_speed_push(&g_stl.bsp[h], h == SOMVR_HAND_L ? &g_stl.dsc : &g_cb.cfg, &nb);
		else
			sc_speed_reset(&g_stl.bsp[h]);
	}
}

/* combat_frame without its sword blade (out of a fight the game sheathes Urfael - SOMVR_SLOTS keeps it in the hand - and
 * the stealth crouch makes Acharn the active item: combat gets no blade, so no hit): the stealth kill's own frame */
static void stl_noblade(uintptr_t pl, const float *root, double t)
{
	if (!g_stl.mode || game_paused() || cl_holds(SOMVR_HAND_R) || cl_holds(SOMVR_HAND_L) || vm_open())
		return; /* paused, a climb's hold, the VR menu: no blade of ours either */
	uintptr_t pcomb = pl && root ? peek_ptr(pl + P_COMBAT) : 0, pc = pcomb ? peek_ptr(pcomb + PC_SERVER) : 0;
	if (pc && (vptr_of((void *)pc) != g_cbg.vt_combat || !peek_ptr(pc + C_PLAYER)))
		pc = 0;
	struct cb_orc o[CB_ORCS];
	int n = pcomb && root ? cb_orcs(pcomb, pc, root, o, CB_ORCS) : 0;
	g_stl.combat_blade = 0;
	stl_frame(pcomb, pc, o, n, root, t);
}

/* stl_frame: the dagger in the left hand (and the sword kept in the right while combat has no blade) swept against the
 * orcs; only the stealth kill comes of it (the sword's ordinary hits are combat_frame's) */
static void stl_sweep(int hand, uintptr_t pc, const struct cb_orc *o, int n, const float *root, double t)
{
	uintptr_t objs[4];
	somvr_blade b = g_stl.bl[hand], prev = g_stl.bprev[hand];
	g_stl.bprev[hand] = b;
	if ((hand == SOMVR_HAND_L && !g_stl.dagger) || !b.valid || sl_held_objs(hand, objs, 4) <= 0)
		return;
	if (hand == SOMVR_HAND_L)
		g_stl.dagger_frames++;
	else
		g_stl.sword_frames++;
	if (!(prev.valid && b.t > prev.t && b.t - prev.t <= 0.1 && dist3(b.tip, prev.tip) <= CB_JUMP_CM))
		return;
	for (int i = 0; i < n; i++) {
		float c0[3], c1[3];
		sc_contact ct;
		cb_capsule(&o[i], c0, c1);
		if (!sc_sweep_capsule(&prev, &b, g_cb.cfg.blade_r, c0, c1, g_cb.orc_r, g_cb.cfg.steps, &ct))
			continue;
		if (!sc_cool_ready(&g_cb.cool, o[i].c, t, g_cb.cfg.cool_s))
			continue;
		if (hand == SOMVR_HAND_L)
			g_stl.dagger_contacts++;
		else
			g_stl.sword_contacts++;
		const sc_cfg *cf = hand == SOMVR_HAND_L ? &g_stl.dsc : &g_cb.cfg;
		if (stl_strike(pc, &o[i], &ct, sc_tier(cf, &g_stl.bsp[hand]), &b, &g_stl.bsp[hand], hand, root, t))
			sc_cool_mark(&g_cb.cool, o[i].c, t);
	}
}

static const char *stl_word(const char *w)
{
	if (!strcmp(w, "status")) {
		g_stl.req_status = 1;
		return g_stl.mode ? "stealth status (the STEALTH lines)" : "stealth status (SOMVR_STEALTH_KILL is off)";
	}
	if (!strcmp(w, "dump")) {
		g_stl.req_dump = 1;
		return "stealth dump (a test: the AI records, STLREC lines)";
	}
	if (!strncmp(w, "chase ", 6)) {
		if (!getenv("SOMVR_HANDS_TEST") || strcmp(getenv("SOMVR_HANDS_TEST"), "1"))
			return "stealth chase (SOMVR_HANDS_TEST is off: ignored)";
		float st = 0, cm = 0;
		if (sscanf(w + 6, "%f %f", &st, &cm) < 1 || st < 0 || st > 1)
			return "stealth chase STICK [STOP_CM] (0..1: ignored)";
		g_stl.chase_stop_cm = cm > 0 ? cm : 120, g_stl.chase_stick = st, g_stl.req_chase = 1;
		return "stealth chase (a test: the test stick toward the nearest orc within 40 m)";
	}
	if (!strncmp(w, "watch ", 6)) {
		int hz = atoi(w + 6);
		g_stl.req_watch = !strcmp(w + 6, "on") || hz > 0 ? 2 : 1;
		g_stl.watch_hz = hz > 0 && hz <= 20 ? hz : 5;
		return "stealth watch on|off|HZ (a test: STEALTH watch lines, 5 a second or HZ)";
	}
	for (int m = 0; m < 3; m++)
		if (!strcmp(w, g_stl_mode_w[m])) {
			if (!getenv("SOMVR_HANDS_TEST") || strcmp(getenv("SOMVR_HANDS_TEST"), "1"))
				return "stealth on|off|log (SOMVR_HANDS_TEST is off: ignored)";
			if (!g_stl.mode && m)
				return "stealth on|log (SOMVR_STEALTH_KILL was off at the start: ignored)";
			g_stl.req_mode = m + 1;
			return "stealth mode (a test)";
		}
	return "stealth status|on|off|log";
}

/* build 19 (re-xr-pipeline.md 73): the VR menu's Stealth kill row: shown with SOMVR_STEALTH_KILL on (with combat); off =
 * every orc a normal fight (no tick, no kill), on again = the start's mode (the game thread: combat_frame's) */
static uint64_t stl_vm_avail(void) { return g_stl.start_mode ? SOMVR_VMA_STEALTH : 0u; }
static void stl_vm_settings(somvr_settings *s) { s->stealth = g_stl.start_mode != 0; }
static const char *stl_vm_set(const somvr_settings *s)
{
	if (!g_stl.start_mode)
		return " (SOMVR_STEALTH_KILL is off: not used)";
	g_stl.mode = s->stealth ? g_stl.start_mode : STL_OFF;
	return s->stealth ? " (behind an unaware orc, the blade kills)" : " (every orc a normal fight)";
}

static void stl_start(int *bad)
{
	memset(&g_stl, 0, sizeof g_stl);
	g_stl.mode = env_word("SOMVR_STEALTH_KILL", g_stl_mode_w, 3, STL_OFF);
	if (!g_stl.mode)
		return;
	if (g_cb.mode == CB_OFF) {
		logf_("WARN SOMVR_STEALTH_KILL=%s needs SOMVR_COMBAT: the stealth kill stays off", g_stl_mode_w[g_stl.mode]);
		g_stl.mode = STL_OFF;
		return;
	}
	somvr_stl_defaults(&g_stl.cfg);
	const char *st = getenv("SOMVR_STEALTH_KILL_STANCE");
	if (st && st[0]) {
		int s = somvr_stl_stance_of(st);
		if (s < 0)
			*bad = 1, logf_("WARN unknown SOMVR_STEALTH_KILL_STANCE=%s, using either", st);
		else
			g_stl.cfg.stance = s;
	}
	g_stl.cfg.behind_deg = env_float("SOMVR_STEALTH_KILL_BEHIND_DEG", g_stl.cfg.behind_deg, 90, 180, bad);
	g_stl.cfg.slow_cms = env_float("SOMVR_STEALTH_KILL_SLOW_CMS", g_stl.cfg.slow_cms, 0, 500, bad);
	g_stl.cfg.low_cm = env_float("SOMVR_STEALTH_KILL_LOW_CM", g_stl.cfg.low_cm, -90, 90, bad);
	g_stl.cfg.high_cm = env_float("SOMVR_STEALTH_KILL_HIGH_CM", g_stl.cfg.high_cm, 0, 200, bad);
	g_stl.cfg.stab_mps = env_float("SOMVR_STEALTH_KILL_STAB_MPS", g_stl.cfg.stab_mps, 0.1f, 10, bad);
	g_stl.cue_amp = env_float("SOMVR_STEALTH_KILL_CUE", 0.25f, 0, 1, bad);
	g_stl.so_cm = env_float("SOMVR_STEALTH_KILL_STANDOFF_CM", 118.0f, 0, 300, bad);
	g_stl.silent = env_word("SOMVR_STEALTH_KILL_SILENT", (const char *const[]){ "0", "1" }, 2, 1);
	g_stl.marks = (int)env_float("SOMVR_STEALTH_KILL_MARKS", 0, 0, 20, bad);
	g_stl.dagger = env_word("SOMVR_STEALTH_KILL_DAGGER", (const char *const[]){ "0", "1" }, 2, 1);
	sc_blade_cfg_default(&g_stl.dcfg);
	g_stl.dcfg.back_cm = 4, g_stl.dcfg.len_cm = 30; /* Acharn (SOMVR_FINISH's dagger blade) */
	sc_cfg_default(&g_stl.dsc);
	const char *r = getenv("SOMVR_STEALTH_KILL_REC");
	snprintf(g_stl.recs, sizeof g_stl.recs, "%s", r && r[0] ? r :
		 "PC_Bip_SneakInstaKill_Silent_B,PC_Bip_SneakInstaKill_Silent_B_2,PC_Bip_SneakInstaKill_Silent_B_3,"
		 "PC_Bip_SneakInstaKill_Silent_B_4");
	logf_("CONFIG stealth=%s (SOMVR_STEALTH_KILL): behind an orc that has not noticed Talion (the game's AIIsAware read, "
	      "its target), %s (slow = under %.0f cm/s), within %.0f deg of behind, the blade into its back half %+.0f..%+.0f cm "
	      "over its root (the pelvis) by a hit or a stab (%.1f m/s along the blade) -> Combat_DealHit with '%s' forceKill (no sync, no view move); "
	      "the cue %.2f within %.0f cm; the dagger in the left hand %s; the stand-off %s%s", g_stl_mode_w[g_stl.mode],
	      g_stl.cfg.stance == SOMVR_STL_STANCE_EITHER ? "crouched or slow" : somvr_stl_stance_name(g_stl.cfg.stance),
	      (double)g_stl.cfg.slow_cms, 180.0 - g_stl.cfg.behind_deg, (double)g_stl.cfg.low_cm, (double)g_stl.cfg.high_cm,
	      (double)g_stl.cfg.stab_mps, g_stl.recs, (double)g_stl.cue_amp, (double)g_stl.cfg.near_cm,
	      g_stl.dagger ? "too (its own sweep: 4 cm back, a 30 cm blade)" : "not (SOMVR_STEALTH_KILL_DAGGER=0)",
	      g_stl.so_cm > 0 ? "on" : "off (SOMVR_STEALTH_KILL_STANDOFF_CM=0)",
	      g_stl.silent ? "; a swing at its target starts no attack stimulus and no block roll (SOMVR_STEALTH_KILL_SILENT)" : "");
	if (g_stl.so_cm > 0)
		logf_("CONFIG stealth stand-off=%.0f cm (SOMVR_STEALTH_KILL_STANDOFF_CM): crouched in the back half of an orc that has "
		      "not noticed Talion, within 3 m, his walk stops this far from it (root to root; the game notices inside ~108 cm, "
		      "crouched or not: docs 72), eased over the last ~20 cm, sideways and back free, never a push; one soft tick "
		      "when it holds him", (double)g_stl.so_cm);
	g_stl.start_mode = g_stl.mode; /* build 19 */
}

static void stl_end_log(void)
{
	if (!g_stl.mode && !g_stl.frames)
		return;
	char vb[400];
	int l = 0;
	vb[0] = 0;
	for (int v = 0; v < SOMVR_STL_N; v++)
		if (g_stl.verdicts[v] && l < (int)sizeof vb - 60)
			l += snprintf(vb + l, sizeof vb - l, "%s%s %llu", l ? ", " : "", somvr_stl_why(v),
				      (unsigned long long)g_stl.verdicts[v]);
	logf_("STEALTH END frames %llu contacts %llu (%s; the dagger in hand %llu frames, %llu contacts; the sword without "
	      "combat's blade %llu frames, %llu contacts); kills %llu, not killed %llu, log-only kills %llu; cues %llu, awareness "
	      "changes %llu; the stand-off held him %llu times (%llu ticks, %.0f cm cut); silent swings: %llu stimuli, %llu block "
	      "rolls left out", (unsigned long long)g_stl.frames,
	      (unsigned long long)g_stl.contacts, l ? vb : "none",
	      (unsigned long long)g_stl.dagger_frames, (unsigned long long)g_stl.dagger_contacts,
	      (unsigned long long)g_stl.sword_frames, (unsigned long long)g_stl.sword_contacts,
	      (unsigned long long)g_stl.kills, (unsigned long long)g_stl.not_killed, (unsigned long long)g_stl.log_kills,
	      (unsigned long long)g_stl.cues, (unsigned long long)g_stl.changes, (unsigned long long)g_stl.so_stops,
	      (unsigned long long)g_stl.so_ticks, g_stl.so_cut_cm, (unsigned long long)g_stl.silent_stims,
	      (unsigned long long)g_stl.silent_rolls);
}
