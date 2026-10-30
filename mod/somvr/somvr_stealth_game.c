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
	/* build 19b: the stand-off per kind of orc (its character combat record: sc_archetype), the brake, the tick at rest;
	 * a stealth target's back for SOMVR_VERBS' grab */
	float so_decel, grab_cm;
	struct { uintptr_t c; int arch; float head_up; char name[48]; } kc[16];
	int nkc;
	uintptr_t rest_orc;          /* the orc the stand-off has Talion at rest at (0 = none) */
	double brake_t;              /* the brake's start (the first cut of this approach) */
	float brake_cm, brake_v;     /* ... its room and Talion's speed toward the orc then */
	uint64_t so_rests, back_offers;
	int watch_hz;
	_Atomic int req_chase;
	uint8_t htab[256];
	int htab_ok;
	uintptr_t seen[160];
	int nseen;
	/* build 21 (docs 84): the thrust, the touch's grace and alert, the body, the blade that sticks, the blood */
	int tier_cut;                /* SOMVR_COMBAT_SWING=stroke's tier of this contact (combat_frame sets it; -1 none) */
	int stick_on, blood;         /* SOMVR_STEALTH_KILL_STICK, _BLOOD */
	float keep_s;                /* SOMVR_STEALTH_KILL_STANDOFF_KEEP_S: the stand-off holds this long after the crouch */
	double crouch_t;             /* the last stand-off tick crouched */
	struct stl_ep { uintptr_t c, hobj; double t0, last_t, alert_t, alerted_t; float T0[3], d0, own, od[3]; int32_t stim; } ep[STL_ORCS];
	struct { uintptr_t hobj; uint32_t node[4]; int st; int logged; } bn[8];
	int nbn;
	char fx[128];
	somvr_stl_stick_cfg kcfg;
	somvr_stl_stick stk;
	uintptr_t stk_c, stk_hobj;
	int stk_node, stk_ease, stk_have;
	float stk_off[3], stk_Ew[3], stk_E0[3], stk_last_G[3], stk_prev_depth, stk_hand_off_max, stk_wound_max;
	sv_quat stk_last_q;
	double stk_t0, stk_prev_t, stk_rel_t, stk_line_t, stk_rum_t, stk_ok_t, air_t;
	_Atomic int stk_fx_req;
	float stk_fx_P[3];
	char stk_fx_what[40];
	uint64_t touches, alerts, airs, sticks, stk_outs, stk_tears, stk_falls, stk_cancels, stk_frames, stk_rumbles, blood_made,
		blood_fail, by[4], keep_ticks, carried;
	double carried_t;
	/* build 22: the controller's own blade (g_cb.hand) against Talion's root, this combat frame [0] and the last [1] -
	 * the hand's own motion (the walk moves both; the drawn sword's bone grip sways with the body) */
	struct stl_hk { int ok; double t; float tip[3], base[3]; } hk[2];
	float hk_yaw;
	uint64_t hk_epoch;
} g_stl;

static void stl_sweep(int hand, uintptr_t pc, const struct cb_orc *o, int n, const float *root, double t);
static void stl_b21_frame(const struct cb_orc *o, int n, uintptr_t pc, double t); /* build 21 */
static struct log_late g_stl_late; /* build 27: then 150 a 10 minutes */
static int stl_line(void) { return g_stl.lines < 600 ? (int)++g_stl.lines : log_late_ok(&g_stl_late, 150, mono()); } /* a STEALTH line may go out */

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

/* build 19b: an orc's kind - its character combat record's name (REACT's read: FUN_026ff330 on the NPC's record holder;
 * sc_archetype) and its head node's height over its root - logged once per orc near Talion (the combat frame). Run
 * 20261028-054549: the infantry, the shield orc, the berserker and the E3 demo's berserker leader (a bigger model) all
 * notice a crouched Talion at the same distance (113-114 cm by this code's measure, calm at 119): one stand-off fits all */
static int stl_kind(const struct cb_orc *o)
{
	int i = 0;
	while (i < g_stl.nkc && g_stl.kc[i].c != o->c)
		i++;
	if (i < g_stl.nkc)
		return i;
	if (g_stl.nkc == 16)
		memmove(g_stl.kc, g_stl.kc + 1, 15 * sizeof g_stl.kc[0]), g_stl.nkc--;
	i = g_stl.nkc++;
	memset(&g_stl.kc[i], 0, sizeof g_stl.kc[i]);
	g_stl.kc[i].c = o->c, g_stl.kc[i].arch = SC_ARCH_OTHER;
	struct rx_tbl b;
	const char *why = NULL;
	uintptr_t npc = peek_ptr(o->c + C_OWNER);
	if (g_rx.ok && npc && rx_table(npc, &b, &why) >= 0 && b.ccr) {
		rx_rec_name(b.ccr, g_stl.kc[i].name, sizeof g_stl.kc[i].name);
		g_stl.kc[i].arch = sc_archetype(g_stl.kc[i].name);
	} else {
		snprintf(g_stl.kc[i].name, sizeof g_stl.kc[i].name, "?");
	}
	float h[3];
	vb_head_of(o, h);
	g_stl.kc[i].head_up = h[1] - o->pos[1];
	if (stl_line())
		logf_("STEALTH orc %#lx: its combat record '%s' (%s), its head %.0f cm over its root", (unsigned long)o->c,
		      g_stl.kc[i].name, sc_archetype_name(g_stl.kc[i].arch), (double)g_stl.kc[i].head_up);
	return i;
}

/* build 19b, SOMVR_STEALTH_KILL_GRAB_CM (default 45; 0 = the head only, build 19): SOMVR_VERBS' left hand takes a stealth
 * kill's target (somvr_stl_possible: unaware, Talion within its back arc, crouched or slow) by its back too - the column
 * from its root (the pelvis) up to its head point, the palm within this of it or the arm reached out at it. From the
 * stand-off (Talion's root 118 cm from its) its head is ~145 cm ahead and high: out of a crouched arm's reach, and the
 * reach-out cone at its head misses a hand that goes for its back. The grab keeps Talion outside the game's notice range
 * (the game's own stealth grab is offered from ~3 m). vb_heads (the combat frame) asks per orc; base = its root. */
static float stl_grab_back(const struct cb_orc *o, uintptr_t pc, const float *root, float base[3])
{
	if (g_stl.mode != STL_ON || !(g_stl.grab_cm > 0) || !root || !pc)
		return 0;
	somvr_stl_who w;
	struct stl_aw a;
	stl_who(o, pc, root, &w, &a);
	if (somvr_stl_possible(&g_stl.cfg, &w) != SOMVR_STL_KILL)
		return 0;
	memcpy(base, o->pos, sizeof o->pos);
	g_stl.back_offers++;
	return g_stl.grab_cm;
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
		float d[3] = { o->delta.x, 0, o->delta.z }, corr[3], r = g_stl.so_cm;
		float dx = g_stl.so_root[0] - g_stl.so_c[i][0], dz = g_stl.so_root[2] - g_stl.so_c[i][2], dist = sqrtf(dx * dx + dz * dz);
		if (!(g_stl.so_decel > 0 ? somvr_stl_standoff_brake(g_stl.so_root, g_stl.so_c[i], r, g_stl.so_dt, g_stl.so_decel, d, corr)
					 : somvr_stl_standoff(g_stl.so_root, g_stl.so_c[i], r, g_stl.so_dt, STL_SO_EASE_HZ, d, corr)))
			continue;
		somvr_loco_take_assist(s, o, (sv_vec3){ corr[0], 0, corr[2] }, g_stl.so_dt);
		g_stl.so_cut_cm += sqrtf(corr[0] * corr[0] + corr[2] * corr[2]);
		g_stl.so_ticks++;
		if (follow)
			continue;
		if (g_stl.so_orc[i] != g_stl.so_last_orc || t - g_stl.so_last_t > STL_CUE_REARM_S) {
			g_stl.so_stops++, g_stl.rest_orc = 0;
			g_stl.brake_t = t, g_stl.brake_cm = dist - r;
			g_stl.brake_v = g_stl.so_dt > 0 ? -(d[0] * dx + d[2] * dz) / (dist > 1e-3f ? dist : 1) / g_stl.so_dt : 0;
			if (!(g_stl.so_decel > 0))
				g_stl.so_cue_req = 1; /* build 19: the tick as the ease starts */
			if (stl_line())
				logf_("STEALTH t=%.3f stand-off: orc %#lx (unaware, Talion in its back half) at %.0f cm: the walk stops "
				      "at %.0f cm (the game notices inside ~108 cm; %s)", t, (unsigned long)g_stl.so_orc[i], dist, (double)r,
				      g_stl.so_decel > 0 ? "braking" : "eased");
		}
		/* build 19b (the brake): at rest on the circle (within 1 cm, still asked in) = in reach: the tick in both hands */
		if (g_stl.so_decel > 0 && g_stl.rest_orc != g_stl.so_orc[i] && dist - r <= 1.0f) {
			g_stl.rest_orc = g_stl.so_orc[i], g_stl.so_rests++, g_stl.so_cue_req = 2;
			if (stl_line())
				logf_("STEALTH t=%.3f stand-off: at rest %.1f cm from orc %#lx (the brake from %.0f cm out at %.0f cm/s took "
				      "%.2f s): in reach - the tick in both hands", t, dist, (unsigned long)g_stl.so_orc[i],
				      (double)g_stl.brake_cm, (double)g_stl.brake_v, t - g_stl.brake_t);
		}
		g_stl.so_last_orc = g_stl.so_orc[i], g_stl.so_last_t = t;
	}
}
static void stl_standoff(uintptr_t pcomb, uintptr_t sc, const float *root, somvr_loco *s, somvr_loco_out *o, float dt,
			 double t)
{
	g_stl.so_n = 0;
	/* build 21 (SOMVR_STEALTH_KILL_STANDOFF_KEEP_S): an approach begun crouched keeps the stand-off for this long after the
	 * crouch ends - the sword's draw lets the stealth crouch go (SOMVR_CROUCH: PC_Stealth holds Acharn's slot), and in the
	 * user's build 20c session the stick still forward then walked Talion in at standing speed to 109 cm: noticed */
	int cr = stl_crouched(), keep = 0;
	if (cr)
		g_stl.crouch_t = t;
	else
		keep = g_stl.keep_s > 0 && g_stl.crouch_t > 0 && t - g_stl.crouch_t <= g_stl.keep_s;
	if (g_stl.mode != STL_ON || !(g_stl.so_cm > 0) || !pcomb || !sc || !root || !(cr || keep))
		return;
	g_stl.keep_ticks += keep;
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
	stl_b21_frame(o, n, pc, t); /* build 21: the wound follows the body, the blood as the blade came out, the alerts */
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
		if (g_stl.so_cm > 0 && o[i].dist <= 400.0f)
			stl_kind(&o[i]); /* build 19b: its kind's line, once */
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
	if (g_stl.so_cue_req) { /* the stand-off held him at arm's reach: one soft tick (the cue's; at rest: both hands) */
		int both = g_stl.so_cue_req == 2;
		g_stl.so_cue_req = 0;
		if (g_stl.cue_amp > 0)
			in_haptic_event(SOMVR_HAND_R, g_stl.cue_amp, 0.03f, SOMVR_HAP_COMBAT);
		if (g_stl.cue_amp > 0 && both)
			in_haptic_event(SOMVR_HAND_L, g_stl.cue_amp, 0.03f, SOMVR_HAP_COMBAT);
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

/* a blade's contact with an orc (the sword's from combat_frame, the dagger's from stl_frame): 1 = the stealth kill took it,
 * 2 (build 21) = taken with no damage (a touch behind an unaware orc, its capsule only, the blade stuck in it) */
/* the stealth kill's hit: the stock silent stealth kill's record dealt with forceKill at the contact; the direction the
 * blade's (or Talion -> the orc); SOMVR_COMBAT_DEATH's type. death = how it falls (the words) */
static void stl_deal(uintptr_t pc, const struct cb_orc *o, const sc_contact *ct, const somvr_blade *b, const float *root,
		     uintptr_t rec, int fk, char *death, size_t dcap)
{
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
	snprintf(death, dcap, "the record's own RecoilTypeHit");
	if (g_kd.mode == KD_TYPE) { /* SOMVR_COMBAT_DEATH's type for this kill too (as the executions') */
		uintptr_t ad = g_df.ok_ad ? DF_FN(DFS_ATTACKDATA, cb_attackdata_fn)(rec) : 0;
		uintptr_t sl = ad ? kx_slot(ad, g_dfg.sch_ad, DF_AD_RECOIL_HIT) : 0, r = kx_find(g_kd.type);
		uint64_t want = (uint64_t)r;
		if (sl && r && peek(sl, &dwas, 8) && (dwas == want || arm_poke(sl, &want, 8))) {
			dslot = dwas == want ? 0 : sl;
			snprintf(death, dcap, "RecoilTypeHit '%s' (SOMVR_COMBAT_DEATH)", g_kd.type);
		} else {
			snprintf(death, dcap, "no '%s': the record's own", g_kd.type);
		}
	}
	snprintf(g_kd.note, sizeof g_kd.note, "the stealth kill: %.70s", death);
	g_kd.pre_c = o->c, g_kd.pre_dead = cb_dead(o->c);
	CB_FN(CS_DEALHIT, cb_dealhit_fn)((cb_ltvec){ ct->point[0], ct->point[1], ct->point[2] }, pc, o->hobj, rec, fk);
	kx_hit_back(dslot, dwas);
	memcpy((void *)(pc + C_HITDIR), hd0, sizeof hd0);
	*(volatile unsigned char *)(pc + C_HITONCE) = once;
}

/* ---- build 21 (SOMVR_STEALTH_KILL_THRUST, _STICK, _BLOOD; docs/re-xr-pipeline.md 84) ------------------------------- */
#define STL_NECK_CM 12.0f    /* the way in within this of the Neck node: the neck */
#define STL_EP_GAP_S 0.5     /* a new touch after this long without the blade in the body: a new episode */
#define STL_OWN_MIN_CMS 20.0f /* build 22: the hand's own tip slower than this is held still (no push of its own) */
#define STL_ALERTED_S 3.0    /* an orc alerted by a touch is not alerted again within this */
#define STL_STK_MAX_S 10.0   /* the blade left in a body this long: out (a safety) */
#define STL_STK_EASE_S 0.12  /* the blade back onto the hand after it comes out */
static const char *const g_stl_bn_w[4] = { "Pelvis", "Torso", "Neck", "Head" };
static const float g_stl_bn_r[3] = { 17.0f, 16.0f, 9.0f }; /* cm: the spine, the chest, the neck (an orc's back from behind) */

/* the orc's body: its Pelvis, Torso, Neck and Head nodes (GetNode by name once per orc, then its world transform); 1 =
 * its skeleton, 0 = an estimate (a column over its root to its head height) */
static int stl_body(const struct cb_orc *o, float nd[4][3])
{
	int i = 0;
	while (i < g_stl.nbn && g_stl.bn[i].hobj != o->hobj)
		i++;
	if (i == g_stl.nbn) {
		if (g_stl.nbn == 8)
			memmove(g_stl.bn, g_stl.bn + 1, 7 * sizeof g_stl.bn[0]), g_stl.nbn--, i--;
		memset(&g_stl.bn[g_stl.nbn], 0, sizeof g_stl.bn[0]);
		g_stl.bn[g_stl.nbn++].hobj = o->hobj;
		i = g_stl.nbn - 1;
	}
	uintptr_t m = peek_ptr(g_som.model_pp), mi = o->hobj ? peek_ptr(o->hobj + OBJ_MODEL) : 0, mv = vptr_of((void *)mi);
	uintptr_t f = mv ? peek_ptr(mv + 0x20) : 0;
	int ok = 0;
	if (g_stl.bn[i].st >= 0 && g_th.ok_node && g_node_ok && m && f && (page_prot(f) & PROT_EXEC)) {
		if (!g_stl.bn[i].st) {
			g_stl.bn[i].st = 1;
			for (int k = 0; k < 4 && g_stl.bn[i].st > 0; k++)
				if (TH_FN(THS_GETNODE, th_getnode_fn)(m, o->hobj, g_stl_bn_w[k], &g_stl.bn[i].node[k]) & 0xff)
					g_stl.bn[i].st = -1;
		}
		ok = g_stl.bn[i].st > 0;
		for (int k = 0; ok && k < 4; k++) {
			float xf[16] = { 0 };
			if ((((nodexf_fn)g_node_sites[NS_NODEXF].target)((void *)m, o->hobj, g_stl.bn[i].node[k], xf, 1) & 0xff) ||
			    !vec_ok(xf, 1e7f) || dist3(xf, o->pos) > 300.0f)
				ok = 0;
			else
				memcpy(nd[k], xf, 3 * sizeof(float));
		}
	}
	if (!ok) {
		int kk = stl_kind(o);
		float hu = kk >= 0 && g_stl.kc[kk].head_up > 30 ? g_stl.kc[kk].head_up : VB_HEAD_UP_CM;
		float up[4] = { 0, 45, hu - 15, hu };
		for (int k = 0; k < 4; k++)
			nd[k][0] = o->pos[0], nd[k][1] = o->pos[1] + up[k], nd[k][2] = o->pos[2];
	}
	if (!g_stl.bn[i].logged++ && stl_line()) {
		float yaw = stl_orc_yaw(o), fx = sinf(yaw), fz = cosf(yaw);
		logf_("STEALTH orc %#lx body (%s): pelvis, torso, neck, head %+.0f %+.0f %+.0f %+.0f cm over its root, %+.0f %+.0f "
		      "%+.0f %+.0f cm ahead of it; radii %.0f/%.0f/%.0f cm", (unsigned long)o->c,
		      ok ? "its skeleton" : g_stl.bn[i].st < 0 ? "an estimate: no such nodes" : "an estimate: not readable",
		      nd[0][1] - o->pos[1], nd[1][1] - o->pos[1], nd[2][1] - o->pos[1], nd[3][1] - o->pos[1],
		      (nd[0][0] - o->pos[0]) * fx + (nd[0][2] - o->pos[2]) * fz, (nd[1][0] - o->pos[0]) * fx + (nd[1][2] - o->pos[2]) * fz,
		      (nd[2][0] - o->pos[0]) * fx + (nd[2][2] - o->pos[2]) * fz, (nd[3][0] - o->pos[0]) * fx + (nd[3][2] - o->pos[2]) * fz,
		      (double)g_stl_bn_r[0], (double)g_stl_bn_r[1], (double)g_stl_bn_r[2]);
	}
	return ok;
}

static struct stl_ep *stl_ep_of(uintptr_t c, int make)
{
	int fr = -1, old = 0;
	for (int k = 0; k < STL_ORCS; k++) {
		if (g_stl.ep[k].c == c)
			return &g_stl.ep[k];
		if (fr < 0 && !g_stl.ep[k].c)
			fr = k;
		if (g_stl.ep[k].last_t < g_stl.ep[old].last_t)
			old = k;
	}
	if (!make)
		return NULL;
	if (fr < 0)
		fr = old;
	memset(&g_stl.ep[fr], 0, sizeof g_stl.ep[fr]);
	g_stl.ep[fr].c = c;
	return &g_stl.ep[fr];
}

/* the game's blood (SOMVR_COMBAT_HITFX's stab FX, one shot) at P */
static void stl_blood(uintptr_t hobj, const float P[3], const char *what, double t)
{
	if (!g_stl.blood)
		return;
	char why[160];
	int made = hf_fx_at(g_stl.fx, hobj, P, t, why, sizeof why);
	g_stl.blood_made += made, g_stl.blood_fail += !made;
	if (stl_line())
		logf_("STEALTH t=%.3f blood '%s' at %s %.1f,%.1f,%.1f: %s%s", t, g_stl.fx, what, (double)P[0], (double)P[1],
		      (double)P[2], made ? "made, " : "not made: ", why);
}

/* the touch's alert: the attack stimulus (the one a swing's start registers; SOMVR_STEALTH_KILL_SILENT keeps it from a
 * swing at a stealth target) at this orc - it turns as to a wind-up (runs 20261028-011320/013422) */
static void stl_alert(uintptr_t pc, struct stl_ep *e, double t)
{
	const char *name = "", *why = g_cb.mode != CB_FULL ? "log only" : !g_cb.ok_stim ? "a stimulus site did not match"
				   : !pc ? "no player server Combat" : !e->hobj ? "no orc object" : NULL;
	uintptr_t rec = 0, ad = 0;
	if (!why && !(rec = cb_record(SC_TIER_LIGHT, 0, &name)))
		why = "no attack record";
	if (!why && !(ad = CB_FN(CS_ATTACKDATA, cb_attackdata_fn)(rec)))
		why = "the record has no AttackData";
	int32_t id1 = 0;
	if (!why) {
		cb_stim_end(t, "the touch's alert");
		CB_FN(CS_STIMULUS, cb_stim_fn)(pc, ad, e->hobj);
		peek(pc + C_STIM, &id1, sizeof id1);
		g_cb.stim.on = 1, g_cb.stim.pc = pc, g_cb.stim.c = e->c, g_cb.stim.hobj = e->hobj, g_cb.stim.id = id1;
		g_cb.stim.t = t;
		e->stim = id1;
	}
	g_stl.alerts++;
	if (stl_line())
		logf_("STEALTH t=%.3f alert: orc %#lx touched %.0f ms ago and no kill came - the attack stimulus at it: %s", t,
		      (unsigned long)e->c, (t - e->t0) * 1e3, why ? why : "registered");
}

static int stl_sticking(void) { return g_stl.stk.on; }
static void stl_set_tier_cut(int t) { g_stl.tier_cut = t; }

/* the blade comes out (the arms frame): r = SOMVR_STL_STK_OUT|TEAR|OFF (cancelled: why) */
static void stl_stick_end(double t, int r, const char *why)
{
	float mv = dist3(g_stl.stk_Ew, g_stl.stk_E0);
	int fell = r == SOMVR_STL_STK_TEAR && mv > 15.0f;
	const char *how = r == SOMVR_STL_STK_OUT ? "drawn out past the wound" : fell ? "the body fell off the blade"
			  : r == SOMVR_STL_STK_TEAR ? "torn free (the hand off the pinned blade)" : why;
	g_stl.stk_outs += r == SOMVR_STL_STK_OUT, g_stl.stk_falls += fell, g_stl.stk_tears += r == SOMVR_STL_STK_TEAR && !fell;
	g_stl.stk_cancels += r == SOMVR_STL_STK_OFF;
	if (r == SOMVR_STL_STK_OUT) {
		in_haptic_event(SOMVR_HAND_R, 0.6f, 0.05f, SOMVR_HAP_COMBAT);
	} else if (r == SOMVR_STL_STK_TEAR) {
		in_haptic_event(SOMVR_HAND_R, 0.85f, 0.06f, SOMVR_HAP_COMBAT);
		if (g_bd.two.w > 0)
			in_haptic_event(SOMVR_HAND_L, 0.5f, 0.06f, SOMVR_HAP_COMBAT);
	}
	if (r != SOMVR_STL_STK_OFF) { /* the blood as it comes out, in the next combat frame (not inside the pose update) */
		for (int j = 0; j < 3; j++)
			g_stl.stk_fx_P[j] = g_stl.stk_Ew[j] - 3.0f * g_stl.stk.a[j];
		snprintf(g_stl.stk_fx_what, sizeof g_stl.stk_fx_what, "the wound as the blade came out");
		atomic_store(&g_stl.stk_fx_req, 1);
	}
	g_stl.stk.on = 0;
	g_stl.stk_ease = g_stl.stk_have, g_stl.stk_rel_t = t;
	if (stl_line())
		logf_("STEALTH t=%.3f stick out: %s after %.2f s (the point %.0f cm in at the deepest, the hand up to %.0f cm off the "
		      "pinned blade, the wound moved %.0f cm); the blade eases back onto the hand over %.0f ms", t, how,
		      t - g_stl.stk_t0, (double)g_stl.stk.peak, (double)g_stl.stk_hand_off_max, (double)mv, STL_STK_EASE_S * 1e3);
}

/* the shortest turn taking unit u onto unit v */
static sv_quat stl_q_from_to(sv_vec3 u, sv_vec3 v)
{
	float d = u.x * v.x + u.y * v.y + u.z * v.z;
	sv_vec3 c = { u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x };
	if (d < -0.9999f) /* opposite: any axis across */
		return fabsf(u.x) < 0.9f ? sv_qnormalize((sv_quat){ 0, -u.z, u.y, 0 }) : sv_qnormalize((sv_quat){ u.z, 0, -u.x, 0 });
	return sv_qnormalize((sv_quat){ c.x, c.y, c.z, 1.0f + d });
}

/* arm_eval, after sfb_arm (SOMVR_STEALTH_KILL_STICK): the right weapon node pinned in the wound - its grip on the wound's
 * axis where the hand's grip projects onto it, the blade along the axis (the hand's roll kept), the hand the
 * controller's (1:1; the sword lags it sideways, the offset shown); after it comes out, eased back onto the hand */
static void stl_stick_arm(const int done[2], const sa_xf gg[2], const int gv[2], const int bowed[2], sa_xf X, sv_vec3 shift)
{
	if (!g_stl.stk.on && !g_stl.stk_ease)
		return;
	double t = cb_now();
	const sa_chain *c = &g_arm.ch[SA_R];
	/* (not the bite's g_aw used_held: the right hand's held weapon frames are 0 headless - run 20261028-204131's first
	 * stick ended at once; the turn below moves the node as it is, rigidly) */
	unsigned bad = (!done[SA_R]) | (!gv[SA_R]) << 1 | (!!bowed[SA_R]) << 2 | (c->wpn < 0) << 3 | (!bd_sword_in_hand()) << 4 |
		       (!(g_arm.weight[SA_R] >= 1)) << 5;
	if (!bad)
		g_stl.stk_ok_t = t;
	if ((bad && t - g_stl.stk_ok_t > 0.15) || (g_stl.stk.on && t - g_stl.stk_t0 > STL_STK_MAX_S)) {
		if (g_stl.stk.on) {
			char why[96];
			snprintf(why, sizeof why, bad ? "cancelled: no sword in the solved hand for 150 ms (%#x)" : "out: left in the body 10 s", bad);
			stl_stick_end(t, SOMVR_STL_STK_OFF, why);
		}
		if (bad) {
			g_stl.stk_ease = 0, g_stl.stk_have = 0;
			return;
		}
	}
	if (bad)
		return; /* a frame without the solved sword: nothing this frame */
	sa_xf g = gg[SA_R];
	float zero[3] = { 0, 0, 0 }, e[4];
	swg_qexp(g_hsm.w_now ? g_hsm.w_rot_now : zero, e); /* the drawn grip: the weight, the bite's and the pivot's turns */
	sv_quat qd = sv_qnormalize(sv_qmul(sv_quat_of(e), sv_qnormalize(g.q)));
	swg_qexp(g_hf.b_now, e);
	qd = sv_qnormalize(sv_qmul(sv_quat_of(e), qd));
	swg_qexp(g_sf.p_now, e);
	qd = sv_qnormalize(sv_qmul(sv_quat_of(e), qd));
	const sc_rigxf *rig = &g_arm.rig;
	const sc_blade_cfg *bc = &g_cb.bcfg;
	float k = sc_rig_scale(rig);
	sv_quat y = sv_qyaw(rig->yaw), yi = sv_qyaw(-rig->yaw), qwd = sv_qnormalize(sv_qmul(y, qd));
	sv_vec3 d0 = sv_qrot(y, (sv_vec3){ k * (g.p.x - rig->p0[0]), k * (g.p.y - rig->p0[1]), k * (g.p.z - rig->p0[2]) });
	float G[3] = { rig->base[0] + shift.x + d0.x, rig->base[1] + shift.y + d0.y, rig->base[2] + shift.z + d0.z }, Pw[3];
	sv_vec3 dir = sv_qrot(qwd, (sv_vec3){ 0, sinf(bc->pitch_rad), cosf(bc->pitch_rad) });
	sv_quat qw = qwd;
	if (g_stl.stk.on) {
		float Gs[3];
		int r = somvr_stl_stick_step(&g_stl.stk, &g_stl.kcfg, g_stl.stk_Ew, G, Gs);
		if (r == SOMVR_STL_STK_HOLD) {
			sv_vec3 a = { g_stl.stk.a[0], g_stl.stk.a[1], g_stl.stk.a[2] };
			qw = sv_qnormalize(sv_qmul(stl_q_from_to(dir, a), qwd));
			memcpy(Pw, Gs, sizeof Pw);
			memcpy(g_stl.stk_last_G, Gs, sizeof Gs), g_stl.stk_last_q = qw, g_stl.stk_have = 1;
			g_stl.stk_frames++;
			g_stl.stk_hand_off_max = fmaxf(g_stl.stk_hand_off_max, g_stl.stk.off);
			/* the rough rumble while it slides in or out */
			float dt = (float)(t - g_stl.stk_prev_t), v = dt > 0 && dt < 0.25f ? fabsf(g_stl.stk.depth - g_stl.stk_prev_depth) / dt : 0;
			if (v > 4.0f && t >= g_stl.stk_rum_t) {
				float amp = 0.25f + 0.45f * fminf(1.0f, v / 80.0f);
				in_haptic_event(SOMVR_HAND_R, amp, 0.04f, SOMVR_HAP_COMBAT);
				if (g_bd.two.w > 0)
					in_haptic_event(SOMVR_HAND_L, 0.6f * amp, 0.04f, SOMVR_HAP_COMBAT);
				g_stl.stk_rum_t = t + 0.035, g_stl.stk_rumbles++;
			}
			g_stl.stk_prev_depth = g_stl.stk.depth, g_stl.stk_prev_t = t;
			if (t >= g_stl.stk_line_t && stl_line()) {
				g_stl.stk_line_t = t + 0.25;
				logf_("STEALTH t=%.3f stick hold: the point %.1f cm in (deepest %.1f), the hand %.1f cm off the pinned grip, "
				      "the wound %.1f cm from where it went in, the blade turned %.1f deg off the hand's", t,
				      (double)g_stl.stk.depth, (double)g_stl.stk.peak, (double)g_stl.stk.off,
				      (double)dist3(g_stl.stk_Ew, g_stl.stk_E0),
				      (double)(acosf(fmaxf(-1.0f, fminf(1.0f, dir.x * a.x + dir.y * a.y + dir.z * a.z))) * 57.29578f));
			}
		} else {
			stl_stick_end(t, r, "");
		}
	}
	if (!g_stl.stk.on) {
		if (!g_stl.stk_ease)
			return;
		float u = (float)((t - g_stl.stk_rel_t) / STL_STK_EASE_S);
		if (u >= 1.0f) {
			g_stl.stk_ease = 0, g_stl.stk_have = 0;
			return;
		}
		u = u < 0 ? 0 : u * u * (3.0f - 2.0f * u);
		for (int j = 0; j < 3; j++)
			Pw[j] = g_stl.stk_last_G[j] + u * (G[j] - g_stl.stk_last_G[j]);
		sv_quat a = g_stl.stk_last_q, b = qwd;
		float s = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0 ? -1.0f : 1.0f;
		qw = sv_qnormalize((sv_quat){ a.x + u * (s * b.x - a.x), a.y + u * (s * b.y - a.y), a.z + u * (s * b.z - a.z),
					      a.w + u * (s * b.w - a.w) });
	}
	/* the weapon node as if the drawn grip were at (Pw, qw): W' = gm3 (x) gm2^-1 (x) W (as the bite and the pivot) */
	sv_vec3 r3 = sv_qrot(yi, (sv_vec3){ (Pw[0] - rig->base[0] - shift.x) / k, (Pw[1] - rig->base[1] - shift.y) / k,
					     (Pw[2] - rig->base[2] - shift.z) / k });
	sa_xf g2 = { g.p, qd }, g3 = { { rig->p0[0] + r3.x, rig->p0[1] + r3.y, rig->p0[2] + r3.z }, sv_qnormalize(sv_qmul(yi, qw)) };
	sa_xf gm2 = sa_grip_to_model(&g_arm.rig, shift, X, g2), gm3 = sa_grip_to_model(&g_arm.rig, shift, X, g3);
	float bs = sz_body_scale();
	if (bs != 1.0f)
		gm2.p = (sv_vec3){ gm2.p.x / bs, gm2.p.y / bs, gm2.p.z / bs }, gm3.p = (sv_vec3){ gm3.p.x / bs, gm3.p.y / bs, gm3.p.z / bs };
	sa_xf W = sa_node_xf(&g_arm.pose[c->wpn]);
	sa_node_set(&g_arm.pose[c->wpn], sa_xf_mul(sa_xf_mul(gm3, sa_xf_inv(gm2)), W));
}

/* stl_frame (the combat frame): the wound follows the body (its node), the blood as the blade came out, the alerts */
static void stl_b21_frame(const struct cb_orc *o, int n, uintptr_t pc, double t)
{
	(void)o, (void)n;
	if (atomic_exchange(&g_stl.stk_fx_req, 0))
		stl_blood(g_stl.stk_hobj, g_stl.stk_fx_P, g_stl.stk_fx_what, t);
	if (g_stl.stk.on) {
		float nd[4][3], at[3];
		struct cb_orc oc;
		/* the killed orc is out of combat's list (dead): read it by its Combat (its body as it slumps) */
		if (cb_orc_get(g_stl.stk_c, &oc) && oc.hobj == g_stl.stk_hobj) {
			if (g_stl.stk_node >= 0 && stl_body(&oc, nd))
				memcpy(at, nd[g_stl.stk_node], sizeof at);
			else
				memcpy(at, oc.pos, sizeof at);
			for (int j = 0; j < 3; j++)
				g_stl.stk_Ew[j] = at[j] + g_stl.stk_off[j];
		}
		g_stl.stk_wound_max = fmaxf(g_stl.stk_wound_max, dist3(g_stl.stk_Ew, g_stl.stk_E0));
	}
	for (int k = 0; k < STL_ORCS; k++) {
		struct stl_ep *e = &g_stl.ep[k];
		if (!e->c)
			continue;
		if (e->alert_t > 0 && t >= e->alert_t) {
			e->alert_t = 0, e->alerted_t = t;
			stl_alert(pc, e, t);
		}
		if (e->stim && t - e->alerted_t > 1.0) { /* its stimulus off after 1 s (a swing's would end with the swing) */
			if (g_cb.stim.on && g_cb.stim.id == e->stim)
				cb_stim_end(t, "the touch's alert, 1 s on");
			e->stim = 0;
		}
	}
}

/* build 22 (somvr.c's combat frame, before its contacts): the controller's blade back in the tracking space (the rig it
 * was placed by undone, in the world's orientation of that rig and its scale): the hand's own motion only - not the
 * walk, the turns, the body's follow under the head (run 20261029-003708: against Talion's root a still hand moved
 * 1.17 m/s as the body caught up with the crouched head) */
static void stl_hand_track(const float *root, double t)
{
	g_stl.hk[1] = g_stl.hk[0];
	struct stl_hk *a = &g_stl.hk[0];
	const sc_rigxf *rx = &g_cb.hand_rx;
	a->ok = root && g_cb.hand.valid && isfinite(rx->yaw + rx->base[0] + rx->base[1] + rx->base[2]), a->t = t;
	if (!a->ok)
		return;
	if (g_stl.hk[1].ok && (fabsf(rx->yaw - g_stl.hk_yaw) > 1e-4f || g_rig_epoch != g_stl.hk_epoch))
		g_stl.hk[1].ok = 0; /* a turn or a jump of the rig: no motion across it */
	g_stl.hk_yaw = rx->yaw, g_stl.hk_epoch = g_rig_epoch;
	for (int j = 0; j < 3; j++)
		a->tip[j] = g_cb.hand.tip[j] - rx->base[j], a->base[j] = g_cb.hand.base[j] - rx->base[j];
}

/* build 22: the frame's span for the hand's own motion - since the episode's last contact (gap, when under 0.1 s), else
 * the speed tracker's last two samples (the first frame of a contact) */
static float stl_frame_dt(const sc_speed *sp, double gap)
{
	if (gap > 0 && gap <= 0.1)
		return (float)gap;
	if (!sp || sp->n < 2)
		return 0;
	double d = sp->s[(sp->head + SC_RING - 1) % SC_RING].t - sp->s[(sp->head + SC_RING - 2) % SC_RING].t;
	return d > 0 && d <= 0.1 ? (float)d : 0;
}

static int stl_strike(uintptr_t pc, const struct cb_orc *o, const sc_contact *ct, int tier, const somvr_blade *b,
		      const sc_speed *sp, int hand, const float *root, double t)
{
	if (!g_stl.mode || !root || !pc)
		return 0;
	if (g_vb.hand.phase == SOMVR_VBH_GRAB && g_vb.held_c == o->c && g_stl.killed_c == o->c)
		return 1;
	if (g_stl.stk.on && o->c == g_stl.stk_c)
		return 2; /* build 21: the blade in its body - no other hit on it (2: taken, no re-hit cooldown) */
	g_stl.contacts++;
	somvr_stl_who w;
	struct stl_aw a;
	int grabbed = stl_who(o, pc, root, &w, &a) == 2;
	somvr_stl_hit h = { somvr_stl_off_deg(ct->point, o->pos, stl_orc_yaw(o)), ct->point[1] - o->pos[1], tier,
			    sp->axis_mps, sp->cos_axis, NAN, 0, g_stl.tier_cut >= 0 ? g_stl.tier_cut : tier, grabbed, 0 };
	/* build 21 (SOMVR_STEALTH_KILL_THRUST): the blade against its body (its skeleton), not combat's hit capsule */
	float E[3] = { ct->point[0], ct->point[1], ct->point[2] }, nd[4][3];
	int link = -1, skel = 0;
	struct stl_ep *ep = NULL;
	if (g_stl.cfg.thrust) {
		skel = stl_body(o, nd);
		h.depth_cm = somvr_stl_body_depth(b->base, b->tip, (const float(*)[3])nd, g_stl_bn_r, 4, E, &link);
		if (h.depth_cm >= 0) {
			h.contact_off_deg = somvr_stl_off_deg(E, o->pos, stl_orc_yaw(o)), h.contact_up_cm = E[1] - o->pos[1];
			h.neck = link == 2 || dist3(E, nd[2]) < STL_NECK_CM;
			ep = stl_ep_of(o->c, 1);
			int fresh = !ep->t0 || t - ep->last_t > STL_EP_GAP_S;
			if (fresh)
				ep->t0 = t, ep->d0 = h.depth_cm, memcpy(ep->T0, b->tip, sizeof ep->T0), ep->own = 0,
				ep->od[0] = ep->od[1] = ep->od[2] = 0;
			/* build 22 (SOMVR_STEALTH_KILL_CARRIED): the hand's own share - its tracking-space velocity over the frame
			 * (the walk and the turns are not in it): the depth it added, the point's own way */
			/* the sword's: the controller's own blade (g_cb.hand: the drawn sword's grip is the solved arm's bone, which
			 * the body's own sway moves - run 20261029-001737: 5 cm of "push" in 0.6 s with the scripted hand still);
			 * a tip slower than STL_OWN_MIN_CMS is a hand held still (tremor, sway): no push */
			float vb[3], vt[3], fdt = 0;
			if (hand != SOMVR_HAND_L) { /* the controller's blade against Talion's root, this frame less the last */
				const struct stl_hk *h1 = &g_stl.hk[0], *h0 = &g_stl.hk[1];
				if (h1->ok && h0->ok && h1->t > h0->t && h1->t - h0->t <= 0.1 && fabs(h1->t - t) < 1e-3) {
					fdt = (float)(h1->t - h0->t);
					for (int j = 0; j < 3; j++)
						vt[j] = (h1->tip[j] - h0->tip[j]) / fdt, vb[j] = (h1->base[j] - h0->base[j]) / fdt;
				}
			} else { /* the dagger: its provider's velocity */
				fdt = stl_frame_dt(sp, fresh ? 0 : t - ep->last_t);
				memcpy(vt, b->vel_tip, sizeof vt), memcpy(vb, b->vel_base, sizeof vb);
			}
			float vs = fdt > 0 ? sqrtf(vt[0] * vt[0] + vt[1] * vt[1] + vt[2] * vt[2]) : 0;
			if (!(vs >= STL_OWN_MIN_CMS))
				fdt = 0;
			ep->own = fmaxf(ep->own + somvr_stl_own_push(b->base, b->tip, vb, vt, fdt, (const float(*)[3])nd, g_stl_bn_r, 4),
					0.0f);
			for (int j = 0; j < 3; j++)
				ep->od[j] += vt[j] * fdt;
			h.carried = ep->own < g_stl.cfg.own_cm;
			ep->hobj = o->hobj, ep->last_t = t;
			/* point-first by the point's own way since it went in too (a thrust's first frames can be sideways: the
			 * sword's weight, the arc - runs 20261028-155330/164235), and the speed it went in at since (build 22:
			 * the hand's own, not the walk's) */
			float dp[3] = { b->tip[0] - ep->T0[0], b->tip[1] - ep->T0[1], b->tip[2] - ep->T0[2] },
			      ax[3] = { b->tip[0] - b->base[0], b->tip[1] - b->base[1], b->tip[2] - b->base[2] };
			if (g_stl.cfg.carried)
				memcpy(dp, ep->od, sizeof dp);
			float ld = sqrtf(dp[0] * dp[0] + dp[1] * dp[1] + dp[2] * dp[2]), la = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
			if (ld >= 5.0f && la > 1.0f)
				h.cos_axis = fmaxf(h.cos_axis, (dp[0] * ax[0] + dp[1] * ax[1] + dp[2] * ax[2]) / (ld * la));
			if (t > ep->t0 + 1e-3)
				h.axis_mps = fmaxf(h.axis_mps, (g_stl.cfg.carried ? ep->own : h.depth_cm - ep->d0) / (float)(t - ep->t0) * 0.01f);
		}
	}
	if (grabbed)
		h.contact_off_deg = 180.0f; /* held from behind: wherever the blade goes in */
	int v = somvr_stl_judge(&g_stl.cfg, &w, &h);
	g_stl.verdicts[v]++;
	/* build 21: behind an unaware orc, a blade in its body that is no kill does no damage (no ordinary hit: a thrust's
	 * first frames never wake it - the 20b regression); after the grace it is alerted unless the kill came. Not its
	 * body (the hit capsule only): nothing */
	int quiet = g_stl.cfg.thrust && g_stl.mode == STL_ON &&
		    (v == SOMVR_STL_TOUCH || v == SOMVR_STL_SIDE || v == SOMVR_STL_LOW || v == SOMVR_STL_AIR || v == SOMVR_STL_CARRIED);
	if (quiet && v == SOMVR_STL_CARRIED) { /* build 22: the walk carried it in: no damage, no alert, no tick */
		g_stl.carried++;
		if (t - g_stl.carried_t > 0.3 && stl_line()) {
			g_stl.carried_t = t;
			logf_("STEALTH t=%.3f carried (%s) orc %#lx: the point %.1f cm in at %s%s, carried in by the walk - the hand's own push "
			      "%.1f cm (under %.0f), its own way %.1f cm: no touch, no alert (along %.2f m/s cos %.2f; Talion %+.0f deg off its "
			      "facing at %.0f cm, %s)", t, hand == SOMVR_HAND_L ? "the dagger" : "the sword", (unsigned long)o->c,
			      (double)h.depth_cm, link == 0 ? "its spine" : link == 1 ? "its chest" : link == 2 ? "its neck" : "?",
			      skel ? "" : " (an estimated body)", ep ? (double)ep->own : -1.0, (double)g_stl.cfg.own_cm,
			      ep ? (double)sqrtf(ep->od[0] * ep->od[0] + ep->od[1] * ep->od[1] + ep->od[2] * ep->od[2]) : -1.0,
			      (double)h.axis_mps, (double)h.cos_axis, w.player_off_deg, w.dist_cm, w.crouched ? "crouched" : "standing");
		}
		return 2;
	}
	if (quiet && v == SOMVR_STL_AIR) {
		g_stl.airs++;
		if (t - g_stl.air_t > 0.3 && stl_line()) { /* how far off its body the blade passed (combat's capsule is wider) */
			float gap = 1e9f, sa, sb;
			for (int k2 = 0; k2 < 3; k2++)
				gap = fminf(gap, sc_seg_dist(b->base, b->tip, nd[k2], nd[k2 + 1], &sa, &sb) - g_stl_bn_r[k2]);
			g_stl.air_t = t;
			logf_("STEALTH t=%.3f air (%s) orc %#lx: in combat's hit capsule, not its body - nothing (the blade %.1f cm off "
			      "its body%s; along %.2f m/s cos %.2f)", t, hand == SOMVR_HAND_L ? "the dagger" : "the sword",
			      (unsigned long)o->c, (double)gap, skel ? "" : ", an estimated body", (double)h.axis_mps, (double)h.cos_axis);
		}
		return 2;
	}
	if (quiet) {
		g_stl.touches++;
		int arm = ep && !grabbed && !ep->alert_t && !(ep->alerted_t > 0 && t - ep->alerted_t < STL_ALERTED_S);
		if (arm) {
			ep->alert_t = ep->t0 + g_stl.cfg.grace_s;
			in_haptic_event(hand, 0.3f, 0.04f, SOMVR_HAP_COMBAT); /* the touch felt (combat's glancing tick) */
		}
		if ((arm || t - g_stl.last_judge_t > 0.3) && stl_line()) {
			g_stl.last_judge_t = t;
			logf_("STEALTH t=%.3f touch (%s) orc %#lx: %s - no damage%s (the point %.1f cm in at %s%s, %+.0f deg off its facing, "
			      "%.0f cm up; along %.2f m/s cos %.2f, the stroke's tier %s; Talion %+.0f deg off its facing at %.0f cm, %s; "
			      "the hand's own push %.1f cm)", t,
			      hand == SOMVR_HAND_L ? "the dagger" : "the sword", (unsigned long)o->c, somvr_stl_why(v),
			      grabbed ? "; held: no alert" : arm ? ", the alert at +0.3 s unless the kill comes" : ep && ep->alert_t
			      ? ", the alert pending" : ", alerted already", (double)h.depth_cm, link == 0 ? "its spine" : link == 1
			      ? "its chest" : link == 2 ? "its neck" : "?", skel ? "" : " (an estimated body)", h.contact_off_deg,
			      h.contact_up_cm, (double)h.axis_mps, (double)h.cos_axis, sc_tier_name(h.tier_cut), w.player_off_deg,
			      w.dist_cm, w.crouched ? "crouched" : "standing", ep ? (double)ep->own : -1.0);
		}
		return 2; /* no re-hit cooldown: the thrust's next frames are judged */
	}
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
	/* build 22 (SOMVR_CAPTAIN_RULES): a captain immune to stealth gets the ordinary hit; vulnerable: the kill; else the
	 * record's own damage (no forceKill) */
	int fk = kp_force_kill(o->c, SOMVR_CPF_STEALTH, "the stealth kill", t);
	if (fk < 0) { /* build 24: immune - the thrust turned on him (the clank, no wound) and he is alerted, or (=hit) the
		       * ordinary hit (build 23) */
		if (!kp_stealth_turn(o->c, h.depth_cm >= 0 ? E : ct->point, t))
			return 0;
		if (ep)
			ep->alert_t = 0, ep->alerted_t = t, stl_alert(pc, ep, t);
		in_haptic_event(hand, 1.0f, 0.06f, SOMVR_HAP_COMBAT);
		in_haptic_after(hand, 0.45f, 0.18f, 0.06f, SOMVR_HAP_COMBAT);
		return 1;
	}
	char death[160], rn[96] = "?";
	int dead0 = cb_dead(o->c);
	float hp0 = cb_health(o->c);
	rx_rec_name(rec, rn, sizeof rn);
	sc_contact cE = *ct;
	if (g_stl.cfg.thrust && h.depth_cm >= 0)
		memcpy(cE.point, E, sizeof cE.point); /* build 21: the hit at the wound (on its body), not combat's capsule */
	struct kp_floor kf; /* build 22: a captain not vulnerable to it survives it */
	kp_floor_on(o->c, fk, "the stealth kill", t, &kf);
	stl_deal(pc, o, &cE, b, root, rec, fk, death, sizeof death);
	kp_floor_off(&kf);
	int dead1 = cb_dead(o->c);
	float hp1 = cb_health(o->c);
	if (dead1 && !dead0)
		g_stl.kills++, CB_COUNT(kills);
	else
		g_stl.not_killed++;
	CB_COUNT(calls);
	int by = somvr_stl_kill_by(&g_stl.cfg, &h);
	g_stl.by[by]++;
	if (ep)
		ep->alert_t = 0;
	float ax[3] = { b->tip[0] - b->base[0], b->tip[1] - b->base[1], b->tip[2] - b->base[2] };
	float la = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
	for (int j = 0; j < 3; j++)
		ax[j] = la > 1e-3f ? ax[j] / la : 0;
	/* build 21 (SOMVR_STEALTH_KILL_STICK): a thrust or a stab stays in the wound (the sword in the right hand, the
	 * drawn one); a heavy thunk as it goes in */
	int stick = g_stl.stick_on && g_stl.cfg.thrust && hand == SOMVR_HAND_R && sp == &g_cb.sp && g_arm.on && h.depth_cm >= 0 &&
		    by != SOMVR_STL_BY_NECK && la > 1e-3f && dead1; /* build 24: only in the dead (a captain survives it) */
	if (stick) {
		if (g_stl.stk.on || g_stl.stk_ease)
			g_stl.stk.on = 0, g_stl.stk_ease = 0, g_stl.stk_have = 0;
		somvr_stl_stick_start(&g_stl.stk, E, ax, g_cb.bcfg.len_cm);
		int nk = link >= 0 && skel ? (dist3(E, nd[link]) <= dist3(E, nd[link + 1]) ? link : link + 1) : -1;
		const float *at = nk >= 0 ? nd[nk] : o->pos;
		for (int j = 0; j < 3; j++)
			g_stl.stk_off[j] = E[j] - at[j];
		g_stl.stk_node = nk, g_stl.stk_c = o->c, g_stl.stk_hobj = o->hobj, g_stl.stk_t0 = t, g_stl.stk_line_t = t;
		memcpy(g_stl.stk_Ew, E, sizeof g_stl.stk_Ew), memcpy(g_stl.stk_E0, E, sizeof g_stl.stk_E0);
		g_stl.stk_hand_off_max = 0, g_stl.stk_wound_max = 0, g_stl.stk_prev_t = 0, g_stl.stk_have = 0, g_stl.stk_ok_t = t;
		g_stl.sticks++;
		in_haptic_event(SOMVR_HAND_R, 1.0f, 0.1f, SOMVR_HAP_COMBAT);
		in_haptic_after(SOMVR_HAND_R, 0.55f, 0.12f, 0.1f, SOMVR_HAP_COMBAT);
		if (g_bd.two.w > 0)
			in_haptic_event(SOMVR_HAND_L, 0.6f, 0.1f, SOMVR_HAP_COMBAT);
	} else {
		in_haptic_event(hand, 0.9f, 0.25f, SOMVR_HAP_COMBAT);
	}
	if (g_stl.cfg.thrust && h.depth_cm >= 0) { /* build 21 (SOMVR_STEALTH_KILL_BLOOD): the game's blood at the wound */
		float P[3] = { E[0] - 2.0f * ax[0], E[1] - 2.0f * ax[1], E[2] - 2.0f * ax[2] };
		stl_blood(o->hobj, P, "the wound as the blade went in", t);
	}
	if (g_stl.cfg.thrust && stl_line())
		logf_("STEALTH t=%.3f kill by %s: the point %.1f cm in at %s%s (went in at %.1f,%.1f,%.1f), along %.2f m/s cos %.2f, "
		      "the stroke's tier %s; %s", t, somvr_stl_by_name(by), (double)h.depth_cm, link == 0 ? "its spine" : link == 1 ?
		      "its chest" : link == 2 ? "its neck" : "?", skel ? "" : " (an estimated body)", (double)E[0], (double)E[1],
		      (double)E[2], (double)h.axis_mps, (double)h.cos_axis, sc_tier_name(h.tier_cut), stick ?
		      "the blade stays in the wound (SOMVR_STEALTH_KILL_STICK), following its nearest node" : "no stick");
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

/* build 20 (SOMVR_VERBS_ASK_KILL=blade; docs/re-xr-pipeline.md 77.2): the sword's stab into the interrogated orc is the
 * player's kill - its Eliminate choice. The stealth kill's own hit does not take an orc in the interrogation (run
 * 20261028-123717: Combat_DealHit forceKill on it, dead 0->0, health 95->95): the game's finisher is its only death
 * there; the stab chooses it, the held view stays, the body walks back after it (SOMVR_BM_RETURN_CM). While the choices
 * are up; a contact softer than a hit or a stab leaves it held; while its finisher runs no hit on it */
static int stl_ask_kill(uintptr_t pc, const struct cb_orc *o, const sc_contact *ct, int tier, const somvr_blade *b,
			const float *root, double t)
{
	(void)pc, (void)ct, (void)b, (void)root;
	if (g_vb.mode != VB_ON || g_vb.ask_kill != VB_ASK_BLADE || !g_vb.ask_c || o->c != g_vb.ask_c)
		return 0;
	int el = vb_choice(1);
	if (!el)
		return g_vb.kind == SOMVR_VBK_ASK || vb_choice(0); /* its finisher (or the choices coming): no hit on it */
	const sc_speed *sp = &g_cb.sp;
	if (tier == SC_TIER_NONE && !(sp->axis_mps >= g_stl.cfg.stab_mps && fabsf(sp->cos_axis) >= g_stl.cfg.stab_cos)) {
		if (g_vb.ask_weak++ % 20 == 0 && vb_log_ok())
			logf_("VERBS t=%.3f the blade on the interrogated orc %#lx: below a hit and a stab (along %.2f m/s cos %.2f): "
			      "held on (%llu so far)", t, (unsigned long)o->c, (double)sp->axis_mps, (double)sp->cos_axis,
			      (unsigned long long)g_vb.ask_weak);
		return 1;
	}
	vb_tap(el, t), g_vb.choices++, g_vb.ask_kills++;
	in_haptic_event(SOMVR_HAND_R, 0.9f, 0.25f, SOMVR_HAP_COMBAT);
	logf_("VERBS t=%.3f kill: the blade in the interrogated orc %#lx (tier %s, along %.2f m/s cos %.2f): %d '%s' tapped (its "
	      "Eliminate: the game's finisher under the held view)", t, (unsigned long)o->c, sc_tier_name(tier),
	      (double)sp->axis_mps, (double)sp->cos_axis, el, in_cmd_name(el));
	return 1;
}

static int stl_contact(uintptr_t pcomb, uintptr_t pc, const struct cb_orc *o, const sc_contact *ct, int tier,
		       const somvr_blade *b, const float *root, double t)
{
	(void)pcomb;
	if (stl_ask_kill(pc, o, ct, tier, b, root, t))
		return 1;
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
		if (stl_strike(pc, &o[i], &ct, sc_tier(cf, &g_stl.bsp[hand]), &b, &g_stl.bsp[hand], hand, root, t) == 1)
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
	if (!strncmp(w, "grab ", 5) || !strncmp(w, "decel ", 6)) { /* build 19b, tests: the old rule against the new in one boot */
		if (!getenv("SOMVR_HANDS_TEST") || strcmp(getenv("SOMVR_HANDS_TEST"), "1"))
			return "stealth grab|decel (SOMVR_HANDS_TEST is off: ignored)";
		float v = strtof(w + (w[0] == 'g' ? 5 : 6), NULL);
		if (w[0] == 'g')
			g_stl.grab_cm = v >= 0 && v <= 80 ? v : g_stl.grab_cm;
		else
			g_stl.so_decel = v >= 0 && v <= 20 ? 100.0f * v : g_stl.so_decel;
		logf_("STEALTH the word '%s': SOMVR_STEALTH_KILL_GRAB_CM now %.0f, SOMVR_STEALTH_KILL_STANDOFF_DECEL %.1f (a test)", w,
		      (double)g_stl.grab_cm, g_stl.so_decel / 100.0);
		return w[0] == 'g' ? "stealth grab CM (a test: 0 = the head only)" : "stealth decel M/S2 (a test: 0 = build 19's ease)";
	}
	if (!strncmp(w, "carried ", 8)) { /* build 22, a test: build 21's touch rule against the carried one */
		if (!getenv("SOMVR_HANDS_TEST") || strcmp(getenv("SOMVR_HANDS_TEST"), "1"))
			return "stealth carried (SOMVR_HANDS_TEST is off: ignored)";
		g_stl.cfg.carried = !strcmp(w + 8, "on");
		logf_("STEALTH the word '%s': carried %d (a test)", w, g_stl.cfg.carried);
		return "stealth carried on|off (a test: build 21's touch against build 22's)";
	}
	if (!strncmp(w, "thrust ", 7) || !strncmp(w, "stick ", 6) || !strncmp(w, "keep ", 5)) { /* build 21, tests */
		if (!getenv("SOMVR_HANDS_TEST") || strcmp(getenv("SOMVR_HANDS_TEST"), "1"))
			return "stealth thrust|stick|keep (SOMVR_HANDS_TEST is off: ignored)";
		if (w[0] == 'k')
			g_stl.keep_s = strtof(w + 5, NULL) >= 0 ? strtof(w + 5, NULL) : g_stl.keep_s;
		else if (w[1] == 'h')
			g_stl.cfg.thrust = !strcmp(w + 7, "on");
		else
			g_stl.stick_on = !strcmp(w + 6, "on");
		logf_("STEALTH the word '%s': thrust %d, stick %d, the stand-off's keep %.1f s (a test)", w, g_stl.cfg.thrust,
		      g_stl.stick_on, (double)g_stl.keep_s);
		return "stealth thrust on|off, stick on|off, keep S (a test: build 20c's rule against build 21's)";
	}
	return "stealth status|on|off|log|grab CM|decel M/S2|thrust on|off|stick on|off|keep S|carried on|off";
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
	/* build 19b: the brake, the back for the grab */
	g_stl.so_decel = 100.0f * env_float("SOMVR_STEALTH_KILL_STANDOFF_DECEL", 2.5f, 0, 20, bad);
	g_stl.grab_cm = env_float("SOMVR_STEALTH_KILL_GRAB_CM", 45.0f, 0, 80, bad);
	g_stl.silent = env_word("SOMVR_STEALTH_KILL_SILENT", (const char *const[]){ "0", "1" }, 2, 1);
	g_stl.marks = (int)env_float("SOMVR_STEALTH_KILL_MARKS", 0, 0, 20, bad);
	g_stl.dagger = env_word("SOMVR_STEALTH_KILL_DAGGER", (const char *const[]){ "0", "1" }, 2, 1);
	/* build 21 (docs 84) */
	static const char *const onoff[] = { "off", "on" };
	g_stl.tier_cut = -1;
	g_stl.cfg.thrust = env_word("SOMVR_STEALTH_KILL_THRUST", onoff, 2, 0);
	g_stl.cfg.thrust_mps = env_float("SOMVR_STEALTH_KILL_THRUST_MPS", g_stl.cfg.thrust_mps, 0.3f, 10, bad);
	g_stl.cfg.thrust_cos = cosf(env_float("SOMVR_STEALTH_KILL_THRUST_DEG", 30, 5, 80, bad) * 0.01745329f);
	g_stl.cfg.depth_cm = env_float("SOMVR_STEALTH_KILL_DEPTH_CM", g_stl.cfg.depth_cm, 1, 60, bad);
	g_stl.cfg.grab_depth_cm = env_float("SOMVR_STEALTH_KILL_GRAB_DEPTH_CM", g_stl.cfg.grab_depth_cm, 1, 60, bad);
	g_stl.cfg.grace_s = env_float("SOMVR_STEALTH_KILL_GRACE_MS", 300, 0, 2000, bad) * 1e-3f;
	g_stl.stick_on = env_word("SOMVR_STEALTH_KILL_STICK", onoff, 2, 0);
	g_stl.kcfg.tear_cm = env_float("SOMVR_STEALTH_KILL_STICK_TEAR_CM", 25, 5, 80, bad);
	g_stl.kcfg.guard_cm = 12.0f;
	g_stl.blood = env_word("SOMVR_STEALTH_KILL_BLOOD", onoff, 2, 0);
	g_stl.keep_s = env_float("SOMVR_STEALTH_KILL_STANDOFF_KEEP_S", 0, 0, 30, bad);
	g_stl.cfg.carried = env_word("SOMVR_STEALTH_KILL_CARRIED", onoff, 2, 0); /* build 22 (docs 93.4) */
	g_stl.cfg.own_cm = env_float("SOMVR_STEALTH_KILL_CARRIED_CM", g_stl.cfg.own_cm, 0.5f, 30, bad);
	{
		const char *fx = getenv("SOMVR_COMBAT_HITFX_STAB");
		snprintf(g_stl.fx, sizeof g_stl.fx, "%s", fx && fx[0] && strcmp(fx, "off") ? fx : HF_FX_STAB);
		for (char *q = g_stl.fx; *q; q++)
			if (*q == '/')
				*q = '\\';
		char fw[200] = "";
		if (g_stl.blood && !g_hf.ok_fx)
			g_hf.ok_fx = df_fx_check(fw, sizeof fw);
		if (g_stl.blood && !g_hf.ok_fx)
			logf_("CONFIG stealth blood: the FX sites did not match (%s): no blood of ours", fw);
	}
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
	if (g_stl.so_cm > 0)
		logf_("CONFIG stealth stand-off's stop (build 19b): %s", g_stl.so_decel > 0 ?
		      "the stop: a steady brake to rest on the circle (SOMVR_STEALTH_KILL_STANDOFF_DECEL, m/s^2) and the soft tick in "
		      "both hands at rest (in reach)" : "the stop: build 19's ease (SOMVR_STEALTH_KILL_STANDOFF_DECEL=0), the tick in "
		      "the sword hand as it starts");
	logf_("CONFIG stealth grab=%.0f cm (SOMVR_STEALTH_KILL_GRAB_CM, build 19b): %s", (double)g_stl.grab_cm, g_stl.grab_cm > 0
	      ? "SOMVR_VERBS' left hand takes a stealth kill's target by its back too (the column from its root up to its head: "
		"the palm within this of it, or the arm reached out at it)" : "the left hand's grab on its head only (build 19)");
	logf_("CONFIG stealth thrust=%s (SOMVR_STEALTH_KILL_THRUST, build 21: %s; the point %.0f deg of the blade's axis at "
	      "%.1f m/s, %.0f cm into its body - its skeleton's spine, chest and neck - from behind, its back, neck or side; a "
	      "full-speed cut to its neck; held by the left hand a point-first stab %.0f cm in; anything else in its body no "
	      "damage and the alert after %.0f ms) stick=%s (the blade stays in the wound: tears free %.0f cm off the hand) "
	      "blood=%s ('%s' at the wound in and out) stand-off keep=%.1f s (SOMVR_STEALTH_KILL_STANDOFF_KEEP_S: an approach "
	      "begun crouched keeps the stand-off this long standing)", onoff[g_stl.cfg.thrust], g_stl.cfg.thrust ?
	      "the kill is the player's own thrust" : "build 20c's rule: a hit or a stab on the contact",
	      (double)(acosf(g_stl.cfg.thrust_cos) * 57.29578f), (double)g_stl.cfg.thrust_mps, (double)g_stl.cfg.depth_cm,
	      (double)g_stl.cfg.grab_depth_cm, (double)g_stl.cfg.grace_s * 1e3, onoff[g_stl.stick_on],
	      (double)g_stl.kcfg.tear_cm, onoff[g_stl.blood], g_stl.fx, (double)g_stl.keep_s);
	logf_("CONFIG stealth carried=%s (SOMVR_STEALTH_KILL_CARRIED, build 22: %s)", onoff[g_stl.cfg.carried], g_stl.cfg.carried
	      ? "a blade the walk carried into its body is no touch - the hand's own push into it under "
		"SOMVR_STEALTH_KILL_CARRIED_CM since it went in: no damage, no alert; the thrust's speed and way are the hand's own"
	      : "build 21's: any blade in its body that is no kill is a touch");
	if (g_stl.cfg.carried)
		logf_("CONFIG stealth carried: the hand's own push %.1f cm (SOMVR_STEALTH_KILL_CARRIED_CM)", (double)g_stl.cfg.own_cm);
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
	      "changes %llu; the stand-off held him %llu times (%llu ticks, %.0f cm cut; at rest %llu); silent swings: %llu stimuli, %llu block "
	      "rolls left out; the back offered to the grab %llu frames", (unsigned long long)g_stl.frames,
	      (unsigned long long)g_stl.contacts, l ? vb : "none",
	      (unsigned long long)g_stl.dagger_frames, (unsigned long long)g_stl.dagger_contacts,
	      (unsigned long long)g_stl.sword_frames, (unsigned long long)g_stl.sword_contacts,
	      (unsigned long long)g_stl.kills, (unsigned long long)g_stl.not_killed, (unsigned long long)g_stl.log_kills,
	      (unsigned long long)g_stl.cues, (unsigned long long)g_stl.changes, (unsigned long long)g_stl.so_stops,
	      (unsigned long long)g_stl.so_ticks, g_stl.so_cut_cm, (unsigned long long)g_stl.so_rests,
	      (unsigned long long)g_stl.silent_stims, (unsigned long long)g_stl.silent_rolls, (unsigned long long)g_stl.back_offers);
	logf_("STEALTH END build 21: kills by a thrust %llu, a neck cut %llu, a stab into the held orc %llu, build 20c's rule %llu; "
	      "touches (no damage) %llu, alerts %llu, the capsule only %llu; stuck %llu (out %llu, torn free %llu, fell off %llu, "
	      "cancelled %llu; %llu frames, %llu rumbles); blood %llu made, %llu not; the stand-off kept standing %llu ticks",
	      (unsigned long long)g_stl.by[SOMVR_STL_BY_THRUST], (unsigned long long)g_stl.by[SOMVR_STL_BY_NECK],
	      (unsigned long long)g_stl.by[SOMVR_STL_BY_GRAB], (unsigned long long)g_stl.by[SOMVR_STL_BY_HIT],
	      (unsigned long long)g_stl.touches, (unsigned long long)g_stl.alerts, (unsigned long long)g_stl.airs,
	      (unsigned long long)g_stl.sticks, (unsigned long long)g_stl.stk_outs, (unsigned long long)g_stl.stk_tears,
	      (unsigned long long)g_stl.stk_falls, (unsigned long long)g_stl.stk_cancels, (unsigned long long)g_stl.stk_frames,
	      (unsigned long long)g_stl.stk_rumbles, (unsigned long long)g_stl.blood_made, (unsigned long long)g_stl.blood_fail,
	      (unsigned long long)g_stl.keep_ticks);
	logf_("STEALTH END build 22: carried=%s - contacts the walk carried in (no touch, no alert) %llu", g_stl.cfg.carried ? "on"
	      : "off", (unsigned long long)g_stl.carried);
}
