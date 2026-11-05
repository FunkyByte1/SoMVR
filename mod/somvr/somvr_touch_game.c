/* somvr_touch_game.c - build 40 (som-b40-stealth; GAME-STUDY 1.5; docs/re-xr-pipeline.md 175): SOMVR_STEALTH_TOUCH=off|on
 * (code default off): reach an unaware orc's back. Included by somvr.c after somvr_stealth_game.c; the rule's pure part is
 * somvr_stealth.c (somvr_stl_touch_zone / _cut).
 *
 * What noticed a Talion behind an orc at ~108 cm, crouched or not (docs 72.1, 72.8): not sight in front and not the
 * sneak-scaled recognition, but three close senses that ignore the facing (static RE, both builds side by side):
 *   - CAIObserverFOV::vf_03 (Linux 0x2ab9360, Windows 0x140b890f0): a stimulus within the perception's NoFOVRadius
 *     (FUN_02aac760's +0x34 out: 'NoFOVRadius' 5 in Biped_Default_AlertLow) plus the stimulus type's own radius
 *     (+0xb0, Windows +0xac), horizontal from the NPC's +0xd2c (Windows +0xd34), inside its height band, passes
 *     with no FOV and no sneak factor;
 *   - CAIObserverAudible::vf_03 (0x2ab8910 / 0x140b852b0): heard within HearDistance (+0x40: 100) plus the audible
 *     stimulus's Radius (+0x2c: 'PlayerAudibleQuiet' 10 - Talion's while he sneaks or stands), 3D;
 *   - CAIObserverPersonalBubble::vf_03 (0x2abe6d0 / 0x140b85af0): any stimulus within PersonalBubbleDistance (50, 3D)
 *     is perceived at once.
 * The observers are the filters each stimulus passes for each AI (its 'Default' set: Obscured, CloseTarget, FOV, ...):
 * vf_03(observer, stimulus, query) - +8 the NPC; the stimulus +0 its type (+0x30 its record), +8 its source's HOBJECT,
 * +0x20 its position, +0x2c its radius; the query +0x90 the strength (FUN_02aa8b70 / 0x140b3dbc0 writes it). FOV and
 * Audible return 1 = rejected (the strength 0), 0 = through; the bubble returns 1 = perceived, final (strength 1), 0 =
 * on to the next.
 *
 * The rule (the three vtable slots, K_VSLOT, each calling the game's own first): a pass of a stimulus whose source is
 * Talion is undone - the strength 0 and "rejected" (the bubble: "not mine", the strength as it was) - when the NPC is an
 * orc in the touch zone (somvr_stl_touch_zone: crouched - the game's stealth crouch or the player's real duck - at most _CMS, unaware by the game's own read, Talion at least
 * _BEHIND_DEG off its facing) and Talion's root is farther than SOMVR_STEALTH_TOUCH_CM from its root (horizontal).
 * In front, beside, standing, fast or once it has noticed him: the game's rules, untouched. The zone is the game
 * thread's snapshot (the PM tick, stl_standoff's call: the orcs within 3 m), read by the hooks under a sequence lock;
 * one older than 1.5 s (a sync, a menu, a cutscene) cuts nothing (build 40b: was 0.3 s).
 * With the touch on the stand-off (SOMVR_STEALTH_KILL_STANDOFF_CM) is off: the player walks right up; nothing moves
 * Talion (the hooks write only the senses' strength).
 *   SOMVR_STEALTH_TOUCH=on, _CM=0 (0..100; build 40b: 0 = never by these senses while sneaking - was 45), _BEHIND_DEG=110 (90..180), _CMS=170 (0..400)
 * Lines: `CONFIG stealth touch ...`, `TOUCH t=.. orc ..: its <sense> passed Talion's '<record>' ...: CUT|the game's`,
 * `TOUCH t=.. orc .. the touch zone in|out ...`, `TOUCH END ...`. Words: `stealth touch status|on|off|CM` (on/off/CM
 * with SOMVR_HANDS_TEST=1: the A/B in one boot). */

/* TOUCH_SITES_BEGIN (check_sites.py counts this block on its own: the three patched K_VSLOTs, docs/re-xr-pipeline.md 175;
 * the slots: the observers' vtables, Linux slot 3 = Windows slot 2) */
static SITE_TABLE struct site_spec g_tc_sites_game[] = {
SITE("TC_FOV", K_VSLOT, 0x3aff7c0, 0x3aff7b0, "30 93 ab 02 00 00 00 00 00 93 ab 02 00 00 00 00 60 93 ab 02 00 00 00 00", 0x2ab9360, "41 57 41 56 41 55 41 54 49 89 fc 48 89 d7 55"),
SITE("TC_HEAR", K_VSLOT, 0x3afe100, 0x3afe0f0, "00 89 ab 02 00 00 00 00 e0 88 ab 02 00 00 00 00 10 89 ab 02 00 00 00 00", 0x2ab8910, "41 55 41 54 49 89 d4 55 48 89 fd 53 48 89 f3"),
SITE("TC_BUBBLE", K_VSLOT, 0x3b00ee8, 0x3b00ed8, "c0 e6 ab 02 00 00 00 00 a0 e6 ab 02 00 00 00 00 d0 e6 ab 02 00 00 00 00", 0x2abe6d0, "41 54 49 89 f4 55 48 89 fd 53 48 89 d3 e8 9e"),
};
/* TOUCH_SITES_END */
enum { TC_FOV = 0, TC_HEAR = 1, TC_BUBBLE = 2, TC_N = 3 };
static const char *const g_tc_sense[TC_N] = { "sight's no-FOV radius", "hearing", "personal bubble" };
#define TC_OBS_NPC 0x08                       /* observer -> its NPC (the server character: +0xc80 its perception) */
#define TC_NPC_POS OFS(STL_NPC_EYE, 0xd2c)    /* NPC: the observers' sense point (Windows +0xd34) */
#define TC_STIM_TYPE 0x00                     /* stimulus -> its type */
#define TC_STIM_SRC 0x08                      /* stimulus -> its source's HOBJECT */
#define TC_STIM_POS 0x20                      /* stimulus: its position (cm) */
#define TC_STIM_R 0x2c                        /* stimulus: its radius (the audible observer's term) */
#define TC_TYPE_REC 0x30                      /* stimulus type -> its record (the stimulus records' schema 0x4b83c40) */
#define TC_TYPE_R OFS(TC_TYPE_R, 0xb0)        /* stimulus type: the radius the FOV observer adds (Windows +0xac) */
#define TC_Q_STRENGTH 0x90                    /* the query: the strength (FUN_02aa8b70 / 0x140b3dbc0) */
#define TC_ORCS 8
#define TC_SNAP_S 1.5 /* (build 40b: was 0.3 - a 0.45 s game-thread stall left the AI's senses uncut, boot 20261105-100551) */

typedef uint64_t (*tc_obs_fn)(uintptr_t obs, uintptr_t stim, uintptr_t q);
struct tc_orc { uintptr_t npc, c; float dist, off; int zone, aware; float pos[3]; };
struct tc_ev { uintptr_t npc, type; uint32_t n, ncut; float d_root, d_flat, d_3d, up, rad, s1; int zone; };
static struct {
	somvr_stl_touch_cfg cfg;
	int want, in, ok[TC_N], tid;
	uintptr_t orig[TC_N];
	/* the game thread's snapshot, read by the hooks (the sequence lock: odd while it is written) */
	_Atomic uint32_t seq;
	uintptr_t me;
	int n, crouched;
	float speed;
	struct tc_orc o[TC_ORCS];
	_Atomic double snap_t;
	/* the hooks' counts (any thread) */
	_Atomic uint64_t calls[TC_N], mine[TC_N], passed[TC_N], near_[TC_N], cut[TC_N];
	/* the hooks' last pass per snapshot orc and sense, for the game thread's lines (a try-lock) */
	atomic_flag ev_lock;
	struct tc_ev ev[TC_ORCS][TC_N];
	/* the game thread's */
	struct { uintptr_t npc; uint32_t n[TC_N], ncut[TC_N]; double line_t[TC_N], seen_t; int zone, last_cut[TC_N]; } pl[16];
	struct { uintptr_t type; char name[64]; } tn[8];
	uint32_t lines;
	uint64_t ticks, zone_ticks, zone_ins;
	float min_quiet_cm;          /* the nearest Talion came to an orc in its zone with no sense passed for it */
	_Atomic int req_status;
} g_tc;
static struct log_late g_tc_late;
static int tc_line(void) { return g_tc.lines < 500 ? (int)++g_tc.lines : log_late_ok(&g_tc_late, 120, mono()); }

/* the hooks' thread (the AI's senses): the game's pass of Talion's stimulus undone in the touch zone beyond the touch */
static uint64_t tc_after(int k, uintptr_t obs, uintptr_t stim, uintptr_t q, float s0, uint64_t r)
{
	atomic_fetch_add_explicit(&g_tc.calls[k], 1, memory_order_relaxed);
	uintptr_t src = *(uintptr_t *)(stim + TC_STIM_SRC), npc = *(uintptr_t *)(obs + TC_OBS_NPC);
	uint32_t s = atomic_load(&g_tc.seq);
	if ((s & 1) || !g_tc.me || src != g_tc.me)
		return r;
	atomic_fetch_add_explicit(&g_tc.mine[k], 1, memory_order_relaxed);
	float s1 = *(float *)(q + TC_Q_STRENGTH);
	int through = k == TC_BUBBLE ? (r & 0xff) != 0 : (r & 0xff) == 0 && s1 > 0;
	if (!through)
		return r;
	atomic_fetch_add_explicit(&g_tc.passed[k], 1, memory_order_relaxed);
	int i = 0, n = g_tc.n;
	while (i < n && i < TC_ORCS && g_tc.o[i].npc != npc)
		i++;
	if (i >= n || i >= TC_ORCS)
		return r;
	struct tc_orc o = g_tc.o[i];
	double st = atomic_load(&g_tc.snap_t);
	if (atomic_load(&g_tc.seq) != s || o.npc != npc)
		return r;
	atomic_fetch_add_explicit(&g_tc.near_[k], 1, memory_order_relaxed);
	int fresh = mono() - st <= TC_SNAP_S, cut = fresh && somvr_stl_touch_cut(&g_tc.cfg, o.zone, o.dist);
	if (!atomic_flag_test_and_set(&g_tc.ev_lock)) { /* the line's facts (busy: this pass goes uncounted in the lines) */
		struct tc_ev *e = &g_tc.ev[i][k];
		const float *sp = (const float *)(stim + TC_STIM_POS), *np = (const float *)(npc + TC_NPC_POS);
		uintptr_t ty = *(uintptr_t *)(stim + TC_STIM_TYPE);
		if (e->npc != npc)
			memset(e, 0, sizeof *e), e->npc = npc;
		e->n++, e->ncut += cut, e->type = ty, e->d_root = o.dist, e->zone = o.zone && fresh, e->s1 = s1;
		float dx = sp[0] - np[0], dy = sp[1] - np[1], dz = sp[2] - np[2];
		e->d_flat = sqrtf(dx * dx + dz * dz), e->d_3d = sqrtf(dx * dx + dy * dy + dz * dz), e->up = dy;
		e->rad = k == TC_HEAR ? *(const float *)(stim + TC_STIM_R) : k == TC_FOV && ty ? *(const float *)(ty + TC_TYPE_R) : 0;
		atomic_flag_clear(&g_tc.ev_lock);
	}
	if (!cut)
		return r;
	atomic_fetch_add_explicit(&g_tc.cut[k], 1, memory_order_relaxed);
	if (k == TC_BUBBLE) { /* as outside the bubble: the strength as it was, on to the next observer */
		*(float *)(q + TC_Q_STRENGTH) = s0;
		return r & ~(uint64_t)0xff;
	}
	*(float *)(q + TC_Q_STRENGTH) = 0.0f; /* as the observer's own rejection */
	return (r & ~(uint64_t)0xff) | 1;
}
__attribute__((used)) uint64_t somvr_w_tc_fov(uintptr_t obs, uintptr_t stim, uintptr_t q)
{
	float s0 = *(float *)(q + TC_Q_STRENGTH);
	return tc_after(TC_FOV, obs, stim, q, s0, ((tc_obs_fn)g_tc.orig[TC_FOV])(obs, stim, q));
}
__attribute__((used)) uint64_t somvr_w_tc_hear(uintptr_t obs, uintptr_t stim, uintptr_t q)
{
	float s0 = *(float *)(q + TC_Q_STRENGTH);
	return tc_after(TC_HEAR, obs, stim, q, s0, ((tc_obs_fn)g_tc.orig[TC_HEAR])(obs, stim, q));
}
__attribute__((used)) uint64_t somvr_w_tc_bubble(uintptr_t obs, uintptr_t stim, uintptr_t q)
{
	float s0 = *(float *)(q + TC_Q_STRENGTH);
	return tc_after(TC_BUBBLE, obs, stim, q, s0, ((tc_obs_fn)g_tc.orig[TC_BUBBLE])(obs, stim, q));
}

static const char *tc_type_name(uintptr_t ty)
{
	if (!ty)
		return "?";
	int k = 0;
	while (k < 8 && g_tc.tn[k].type && g_tc.tn[k].type != ty)
		k++;
	if (k == 8)
		k = 7;
	if (g_tc.tn[k].type != ty) {
		g_tc.tn[k].type = ty;
		if (!rx_rec_name(peek_ptr(ty + TC_TYPE_REC), g_tc.tn[k].name, sizeof g_tc.tn[k].name))
			snprintf(g_tc.tn[k].name, sizeof g_tc.tn[k].name, "type %#llx", (unsigned long long)ty);
	}
	return g_tc.tn[k].name;
}

static const char *tc_why_not(const struct tc_orc *o, char *b, size_t cap)
{
	snprintf(b, cap, "%s%s%s%s%s", !g_tc.cfg.on ? "the touch off, " : "", !g_tc.crouched ? "standing, " : "",
		 !(isfinite(g_tc.speed) && g_tc.speed <= g_tc.cfg.cms) ? "fast or unread, " : "", o->aware ? "aware, " : "",
		 fabsf(o->off) < g_tc.cfg.behind_deg ? "not behind it, " : "");
	size_t l = strlen(b);
	if (l >= 2)
		b[l - 2] = 0;
	return b;
}

/* the game thread: the hooks' passes since the last tick as lines - the first of a run, a change between cut and not,
 * then at most once a second (the passes in between counted into the next line) */
static void tc_drain(double t)
{
	struct tc_ev ev[TC_ORCS][TC_N];
	if (atomic_flag_test_and_set(&g_tc.ev_lock))
		return;
	memcpy(ev, g_tc.ev, sizeof ev);
	for (int i = 0; i < TC_ORCS; i++)
		for (int k = 0; k < TC_N; k++)
			g_tc.ev[i][k].n = g_tc.ev[i][k].ncut = 0; /* consumed */
	atomic_flag_clear(&g_tc.ev_lock);
	for (int i = 0; i < TC_ORCS; i++)
		for (int k = 0; k < TC_N; k++) {
			struct tc_ev *e = &ev[i][k];
			if (!e->npc || !e->n)
				continue;
			int p = 0;
			while (p < 16 && g_tc.pl[p].npc && g_tc.pl[p].npc != e->npc)
				p++;
			if (p == 16) { /* the oldest out */
				p = 0;
				for (int j = 1; j < 16; j++)
					if (g_tc.pl[j].seen_t < g_tc.pl[p].seen_t)
						p = j;
				memset(&g_tc.pl[p], 0, sizeof g_tc.pl[p]);
			}
			if (!g_tc.pl[p].npc)
				g_tc.pl[p].npc = e->npc;
			g_tc.pl[p].seen_t = t;
			g_tc.pl[p].n[k] += e->n, g_tc.pl[p].ncut[k] += e->ncut;
			int now_cut = e->ncut == e->n, first = t - g_tc.pl[p].line_t[k] > 1.5;
			if (!(first || now_cut != g_tc.pl[p].last_cut[k] || t - g_tc.pl[p].line_t[k] >= 1.0))
				continue;
			uint32_t dn = g_tc.pl[p].n[k], dc = g_tc.pl[p].ncut[k];
			g_tc.pl[p].n[k] = g_tc.pl[p].ncut[k] = 0, g_tc.pl[p].line_t[k] = t, g_tc.pl[p].last_cut[k] = now_cut;
			if (!tc_line())
				continue;
			const struct tc_orc *o = NULL;
			for (int j = 0; j < g_tc.n; j++)
				if (g_tc.o[j].npc == e->npc)
					o = &g_tc.o[j];
			char why[96] = "";
			logf_("TOUCH t=%.3f orc %#llx: its %s passed Talion's '%s' %u time(s), the last at %.0f cm root to root (its "
			      "sense point to the stimulus %.0f cm flat, %.0f cm 3D, the stimulus %+.0f cm over it; %s %.0f cm; strength "
			      "%.2f)%s: %s", t, (unsigned long long)(o ? o->c : e->npc), g_tc_sense[k], tc_type_name(e->type), dn,
			      (double)e->d_root, (double)e->d_flat, (double)e->d_3d, (double)e->up,
			      k == TC_HEAR ? "the stimulus's radius" : k == TC_FOV ? "the type's radius term" : "-", (double)e->rad,
			      (double)e->s1, o ? "" : " (no longer within 3 m)",
			      dc == dn ? "CUT (the touch zone, farther than the touch)" :
			      dc ? "partly cut (the zone or the distance changed)" :
			      e->zone ? "the game's (in the zone, within the touch)" :
			      o ? (tc_why_not(o, why, sizeof why), why[0] ? why : "the game's (the snapshot was stale)") : "the game's");
		}
}

static void tc_close_tick(const float *root, const struct tc_orc *o, int no, double t);
/* the PM tick (stl_standoff's call, the game thread): the snapshot of the orcs within 3 m for the hooks */
static void tc_tick(uintptr_t pcomb, uintptr_t sc, const float *root, double t)
{
	if (!g_tc.in)
		return;
	if (atomic_exchange(&g_tc.req_status, 0))
		logf_("TOUCH t=%.3f status: %s, %.0f cm (behind %.0f deg, at most %.0f cm/s); %s %.0f cm/s; %d orc(s) within 3 m; "
		      "cut so far: sight %llu, hearing %llu, bubble %llu", t, g_tc.cfg.on ? "on" : "off", (double)g_tc.cfg.cm,
		      (double)g_tc.cfg.behind_deg, (double)g_tc.cfg.cms, g_tc.crouched ? "crouched" : "standing",
		      (double)g_tc.speed, g_tc.n, (unsigned long long)g_tc.cut[TC_FOV], (unsigned long long)g_tc.cut[TC_HEAR],
		      (unsigned long long)g_tc.cut[TC_BUBBLE]);
	tc_drain(t);
	struct cb_orc oo[TC_ORCS];
	int no = pcomb && sc && root ? cb_orcs_r(pcomb, sc, root, 300.0f, oo, TC_ORCS) : 0;
	struct tc_orc o[TC_ORCS];
	int cr = stl_crouched() || g_cr.ducked; /* the game's stealth crouch, or the player's own duck (a draw lets the game's
						  * crouch go: SOMVR_CROUCH's sword draw) */
	float sp = g_stl.speed_cms;
	for (int i = 0; i < no; i++) {
		somvr_stl_who w;
		struct stl_aw a;
		stl_who(&oo[i], sc, root, &w, &a);
		o[i] = (struct tc_orc){ a.npc, oo[i].c, w.dist_cm, w.player_off_deg, 0, w.aware != 0 || w.dead, { 0, 0, 0 } };
		memcpy(o[i].pos, oo[i].pos, sizeof o[i].pos);
		o[i].zone = a.npc && somvr_stl_touch_zone(&g_tc.cfg, cr, sp, o[i].aware, w.player_off_deg);
	}
	uintptr_t me = sc ? peek_ptr(sc + C_HOBJ) : 0;
	atomic_fetch_add(&g_tc.seq, 1);
	g_tc.me = me, g_tc.n = no, g_tc.crouched = cr, g_tc.speed = sp;
	memcpy(g_tc.o, o, sizeof o[0] * (size_t)no);
	atomic_store(&g_tc.snap_t, mono());
	atomic_fetch_add(&g_tc.seq, 1);
	if (!atomic_flag_test_and_set(&g_tc.ev_lock)) { /* the events keyed by the new order: a slot whose orc moved resets */
		for (int i = 0; i < TC_ORCS; i++)
			for (int k = 0; k < TC_N; k++)
				if (g_tc.ev[i][k].npc && (i >= no || g_tc.ev[i][k].npc != o[i].npc))
					memset(&g_tc.ev[i][k], 0, sizeof g_tc.ev[i][k]);
		atomic_flag_clear(&g_tc.ev_lock);
	}
	g_tc.ticks++;
	/* the zone's changes per orc (the lines), the nearest quiet approach */
	for (int i = 0; i < no; i++) {
		g_tc.zone_ticks += o[i].zone;
		int p = 0;
		while (p < 16 && g_tc.pl[p].npc && g_tc.pl[p].npc != o[i].npc)
			p++;
		if (p == 16)
			continue;
		if (!g_tc.pl[p].npc)
			memset(&g_tc.pl[p], 0, sizeof g_tc.pl[p]), g_tc.pl[p].npc = o[i].npc, g_tc.pl[p].seen_t = t;
		if (o[i].zone != g_tc.pl[p].zone) {
			g_tc.pl[p].zone = o[i].zone;
			g_tc.zone_ins += o[i].zone;
			char why[96];
			if (tc_line())
				logf_("TOUCH t=%.3f orc %#llx the touch zone %s: %s, %.0f cm/s, %.0f deg off its facing, %s, %.0f cm root to "
				      "root%s%s", t, (unsigned long long)o[i].c, o[i].zone ? "in" : "out", cr ? "crouched" : "standing",
				      (double)sp, (double)o[i].off, o[i].aware ? "aware" : "unaware", (double)o[i].dist,
				      o[i].zone ? "" : " - ", o[i].zone ? "" : tc_why_not(&o[i], why, sizeof why));
		}
		if (o[i].zone && (!(g_tc.min_quiet_cm > 0) || o[i].dist < g_tc.min_quiet_cm))
			g_tc.min_quiet_cm = o[i].dist;
	}
	tc_close_tick(root, o, no, t); /* build 40b (SOMVR_STEALTH_CLOSE): before the proxy's integrate */
}

/* ---- build 40b (som-b40-stealth2; docs/re-xr-pipeline.md 176): SOMVR_STEALTH_CLOSE - his body nearer than the game's
 * bodies let a walk come (79-85 cm root to root behind an infantry). Talion's movement shape is the Havok character
 * proxy's (CPhysicsCharacter = pm+0x50: +0x40 the hkpCharacterProxy, +0x50 its phantom; the shape a convex "pen" hull,
 * CPenPhysicsShape), and each proxy step asks its listener - the game's MyCharacterListener - to edit the contact planes
 * it will slide along: processConstraintsCallback(listener, proxy, manifold, input), the listener's vtable slot (TC_PCB;
 * Linux 0x98edd0, Windows 0x140e21750: both zero the plane velocity of non-fixed bodies). Manifold entries 0x40 bytes
 * (+0 the contact point, m; +0x30 the other collidable: +0x28 its kind - 1 a body, 2 a phantom -, +0x20 its owner's
 * offset; a body's motion type at owner +0x15c, Windows +0x160: 5 = fixed, the world; an orc is a non-fixed body,
 * boot 20261105-101838), the input's planes at +0x48 (0x40 bytes each: the plane +0, its velocity +0x10), their count
 * +0x50. The rule (the game's first): in Talion's own proxy, while he is close behind an orc - sneaking within
 * _NEAR_CM (160) of an unaware orc's back - the planes of a keyframed or character body (motion type 4 or 7: the orc's,
 * boot 20261105-102709: 4) or a phantom whose contact point lies on that orc
 * (within 80 cm of its root, flat) are cleared (plane and velocity 0: no constraint), so the bodies do not stop him; our
 * own brake stops his walk at SOMVR_STEALTH_CLOSE_CM (52) root to root - never into the orc, nothing pushed. It stays on
 * for that orc until Talion is SOMVR_STEALTH_CLOSE_CLEAR_CM (180; above _NEAR_CM) from it (whatever the orc does), so its body never
 * pushes him back out. Walls, the ground and every other character: the game's. */
static SITE_TABLE struct site_spec g_tcc_sites_game[] = {
/* CLOSE_SITES_BEGIN (check_sites.py counts this block on its own: the patched K_VSLOT TC_PCB) */
SITE("TC_PCB", K_VSLOT, 0x34b67e0, 0x34b67d8, "c0 ee 98 00 00 00 00 00 d0 ed 98 00 00 00 00 00 d0 ec 98 00 00 00 00 00", 0x98edd0, "44 8b 4a 08 31 ff 31 c0 66 0f ef c0 45 85 c9"),
/* CLOSE_SITES_END */
};
#define TC_PC_PROXY 0x40   /* CPhysicsCharacter -> its hkpCharacterProxy (W 0x140e22050: param_1[8], the listener's proxy) */
#define TC_MF_STRIDE 0x40  /* hkpRootCdPoint */
#define TC_MF_B 0x30       /* ... its other collidable */
#define TC_CO_KIND 0x28    /* hkpCollidable: the broadphase handle's kind (1 entity, 2 phantom) */
#define TC_CO_OWNER 0x20   /* hkpCollidable: the owner's offset (int8; the game's listener: movsx) */
#define TC_OWNER_MOTION OFS(TC_OWNER_MOTION, 0x15c) /* hkpEntity: its motion type (5 = fixed: the world; Windows +0x160) */
#define TC_IN_PLANES 0x48  /* hkpSimplexSolverInput: its surface constraints (0x40 each) */
#define TC_IN_N 0x50       /* ... their count */
typedef void (*tc_pcb_fn)(uintptr_t self, uintptr_t proxy, uintptr_t manifold, uintptr_t input);
static struct {
	int want, in;
	_Atomic int paused;           /* the word `stealth touch close off` (a test: the A/B in one boot) */
	float stop_cm, near_cm, clear_cm;
	uintptr_t orig;
	/* the game thread's (the PM tick): the orc we are close behind */
	_Atomic uint32_t seq;
	uintptr_t proxy, orc;
	float orc_pos[3], root[3];
	int active;
	double on_t;
	float min_cm;
	/* the hooks' (Talion's proxy step: the game thread) */
	_Atomic uint64_t calls, mine, cleared, kept_world, steps_cleared;
	_Atomic int proxy_seen, last_kind;
	uint64_t ons, offs, braked, brake_ticks;
	double brake_cm_cut;
	uintptr_t braked_orc;
	float dt, brake_root[3];
} g_tcc;

__attribute__((used)) void somvr_w_tc_pcb(uintptr_t self, uintptr_t proxy, uintptr_t manifold, uintptr_t input)
{
	((tc_pcb_fn)g_tcc.orig)(self, proxy, manifold, input);
	atomic_fetch_add_explicit(&g_tcc.calls, 1, memory_order_relaxed);
	uint32_t s = atomic_load(&g_tcc.seq);
	if ((s & 1) || !g_tcc.active || !proxy || proxy != g_tcc.proxy)
		return;
	float c[3] = { g_tcc.orc_pos[0], g_tcc.orc_pos[1], g_tcc.orc_pos[2] };
	if (atomic_load(&g_tcc.seq) != s)
		return;
	atomic_fetch_add_explicit(&g_tcc.mine, 1, memory_order_relaxed);
	uintptr_t data = *(uintptr_t *)manifold, planes = *(uintptr_t *)(input + TC_IN_PLANES);
	int n = *(int *)(manifold + 8), np = *(int *)(input + TC_IN_N), any = 0;
	for (int i = 0; i < n && i < np && data && planes; i++) {
		uintptr_t e = data + (uintptr_t)i * TC_MF_STRIDE, b = *(uintptr_t *)(e + TC_MF_B);
		unsigned char kind = b ? *(unsigned char *)(b + TC_CO_KIND) : 0, motion = 0xff;
		if (kind == 1) { /* a body: the world is fixed (the game's listener reads the same) */
			uintptr_t ow = b + (intptr_t) * (signed char *)(b + TC_CO_OWNER);
			motion = ow != b ? *(unsigned char *)(ow + TC_OWNER_MOTION) : 0xff;
		}
		if (!b || (kind != 1 && kind != 2) || (kind == 1 && motion != 4 && motion != 7)) { /* the world, loose things: the game's */
			atomic_fetch_add_explicit(&g_tcc.kept_world, 1, memory_order_relaxed);
			continue;
		}
		const float *p = (const float *)e; /* the contact point, Havok metres */
		float dx = p[0] * 100.0f - c[0], dz = p[2] * 100.0f - c[2], dy = p[1] * 100.0f - c[1];
		if (dx * dx + dz * dz > 80.0f * 80.0f || dy < -40.0f || dy > 260.0f)
			continue; /* another character or a loose thing elsewhere */
		memset((void *)(planes + (uintptr_t)i * 0x40), 0, 32); /* no plane, no velocity: no constraint */
		atomic_store_explicit(&g_tcc.last_kind, (int)kind * 256 + motion, memory_order_relaxed);
		atomic_fetch_add_explicit(&g_tcc.cleared, 1, memory_order_relaxed), any = 1;
	}
	if (any)
		atomic_fetch_add_explicit(&g_tcc.steps_cleared, 1, memory_order_relaxed);
}

/* the PM tick (tc_tick, the game thread): which orc we are close behind (sticky until clear), the hook's snapshot */
static void tc_close_tick(const float *root, const struct tc_orc *o, int no, double t)
{
	if (!g_tcc.in)
		return;
	uintptr_t pc = g_cl.pc && g_cl.vt_pc && vptr_of((void *)g_cl.pc) == g_cl.vt_pc ? g_cl.pc : 0;
	uintptr_t proxy = pc ? peek_ptr(pc + TC_PC_PROXY) : 0;
	int k = -1;
	if (g_tcc.active) { /* the same orc until Talion is clear of it */
		for (int i = 0; i < no; i++)
			if (o[i].c == g_tcc.orc)
				k = i;
		if (k >= 0 && o[k].dist < g_tcc.min_cm)
			g_tcc.min_cm = o[k].dist;
		if (k < 0 || o[k].dist > g_tcc.clear_cm || !proxy || proxy != g_tcc.proxy) { /* (clear_cm > near_cm: no flapping) */
			g_tcc.offs++;
			logf_("CLOSE t=%.3f off: orc %#llx %s after %.1f s (the nearest %.0f cm; planes cleared in %llu proxy steps, %llu "
			      "planes)", t, (unsigned long long)g_tcc.orc, k < 0 ? "gone from 3 m" : !proxy || proxy != g_tcc.proxy ?
			      "(Talion's proxy changed)" : "clear", t - g_tcc.on_t, (double)g_tcc.min_cm,
			      (unsigned long long)g_tcc.steps_cleared, (unsigned long long)g_tcc.cleared);
			atomic_fetch_add(&g_tcc.seq, 1);
			g_tcc.active = 0;
			atomic_fetch_add(&g_tcc.seq, 1);
			k = -1;
		}
	}
	if (!g_tcc.active && proxy && !atomic_load(&g_tcc.paused))
		for (int i = 0; i < no; i++)
			if (o[i].zone && o[i].dist <= g_tcc.near_cm && (k < 0 || o[i].dist < o[k].dist))
				k = i;
	if (k < 0)
		return;
	atomic_fetch_add(&g_tcc.seq, 1);
	if (!g_tcc.active) {
		g_tcc.active = 1, g_tcc.orc = o[k].c, g_tcc.proxy = proxy, g_tcc.on_t = t, g_tcc.min_cm = o[k].dist, g_tcc.ons++;
		logf_("CLOSE t=%.3f on: orc %#llx at %.0f cm, %.0f deg off its facing, unaware; Talion sneaking - its body's planes "
		      "cleared from his proxy's steps (proxy %#llx), his walk stops at %.0f cm", t, (unsigned long long)o[k].c,
		      (double)o[k].dist, (double)o[k].off, (unsigned long long)proxy, (double)g_tcc.stop_cm);
	}
	memcpy(g_tcc.orc_pos, o[k].pos, sizeof g_tcc.orc_pos), memcpy(g_tcc.root, root, sizeof g_tcc.root);
	atomic_fetch_add(&g_tcc.seq, 1);
}

/* the PM tick's delta (stl_standoff, after tc_tick; its follow too): the walk toward the orc we are close behind stops
 * at SOMVR_STEALTH_CLOSE_CM - a firm stop like a body's (10 m/s^2: from a crouched walk in the last ~11 cm), sideways
 * and back free, never a push (somvr_stl_standoff_brake, build 19b's stop) */
#define TC_CLOSE_DECEL 1000.0f
static void tc_close_apply(somvr_loco *s, somvr_loco_out *lo, const float *root, float dt, double t, int follow)
{
	if (!g_tcc.in || !g_tcc.active || !s || !lo)
		return;
	if (!follow)
		g_tcc.dt = dt, memcpy(g_tcc.brake_root, root, sizeof g_tcc.brake_root);
	float d[3] = { lo->delta.x, 0, lo->delta.z }, corr[3];
	if (!somvr_stl_standoff_brake(g_tcc.brake_root, g_tcc.orc_pos, g_tcc.stop_cm, g_tcc.dt, TC_CLOSE_DECEL, d, corr))
		return;
	somvr_loco_take_assist(s, lo, (sv_vec3){ corr[0], 0, corr[2] }, g_tcc.dt);
	g_tcc.brake_ticks++, g_tcc.brake_cm_cut += sqrtf(corr[0] * corr[0] + corr[2] * corr[2]);
	if (!follow && g_tcc.braked_orc != g_tcc.orc) {
		g_tcc.braked_orc = g_tcc.orc, g_tcc.braked++;
		float dx = g_tcc.brake_root[0] - g_tcc.orc_pos[0], dz = g_tcc.brake_root[2] - g_tcc.orc_pos[2];
		logf_("CLOSE t=%.3f the walk stops at %.0f cm from orc %#llx (now %.0f cm root to root): its body no longer does it",
		      t, (double)g_tcc.stop_cm, (unsigned long long)g_tcc.orc, (double)sqrtf(dx * dx + dz * dz));
	}
}
/* the end (the END lines) */
static void tc_close_end(void)
{
	if (!g_tcc.in)
		return;
	int lk = atomic_load(&g_tcc.last_kind);
	logf_("CLOSE END on %llu times, off %llu; the listener %llu calls, Talion's while on %llu, planes cleared %llu (in %llu "
	      "steps; the last one's collidable kind %d, motion type %d), world planes kept %llu; the walk stopped %llu times "
	      "(%llu ticks, %.0f cm cut)%s",
	      (unsigned long long)g_tcc.ons, (unsigned long long)g_tcc.offs, (unsigned long long)g_tcc.calls,
	      (unsigned long long)g_tcc.mine, (unsigned long long)g_tcc.cleared, (unsigned long long)g_tcc.steps_cleared,
	      lk >> 8, lk & 0xff, (unsigned long long)g_tcc.kept_world, (unsigned long long)g_tcc.braked, (unsigned long long)g_tcc.brake_ticks,
	      g_tcc.brake_cm_cut, g_tcc.active ? "; on at the end" : "");
}
static const char *tc_word(const char *w)
{
	static char b[160];
	while (*w == ' ')
		w++;
	if (!*w || !strcmp(w, "status")) {
		atomic_store(&g_tc.req_status, 1);
		return g_tc.in ? "stealth touch status (a TOUCH line)" : "stealth touch status (SOMVR_STEALTH_TOUCH is not in)";
	}
	if (!getenv("SOMVR_HANDS_TEST") || strcmp(getenv("SOMVR_HANDS_TEST"), "1"))
		return "stealth touch (SOMVR_HANDS_TEST is off: ignored)";
	if (!g_tc.in)
		return "stealth touch (the hooks are not in: SOMVR_STEALTH_TOUCH=on at the start)";
	if (!strcmp(w, "close on") || !strcmp(w, "close off")) { /* build 40b: SOMVR_STEALTH_CLOSE's A/B */
		if (!g_tcc.want)
			return "stealth touch close (SOMVR_STEALTH_CLOSE is not on: ignored)";
		atomic_store(&g_tcc.paused, w[7] == 'f');
		return w[7] == 'f' ? "stealth touch close off (a test: the orc's body stops him again - once he is clear of it)"
				   : "stealth touch close on (a test: an unaware orc's body no longer stops a sneaking Talion)";
	}
	if (!strcmp(w, "on") || !strcmp(w, "off")) {
		g_tc.cfg.on = w[1] == 'n';
		snprintf(b, sizeof b, "stealth touch %s (a test: %s)", w, g_tc.cfg.on ? "the touch zone's cut" : "the game's senses");
		return b;
	}
	char *e;
	double v = strtod(w, &e);
	if (e != w && !*e && v >= 0 && v <= 100) {
		g_tc.cfg.cm = (float)v;
		snprintf(b, sizeof b, "stealth touch %.0f cm (a test: SOMVR_STEALTH_TOUCH_CM)", v);
		return b;
	}
	return "stealth touch: status|on|off|CM|close on|close off";
}

/* the start (after stl_start): the switches, the three slots; the stand-off off */
static void tc_start(int *bad)
{
	memset(&g_tc, 0, sizeof g_tc);
	atomic_flag_clear(&g_tc.ev_lock);
	somvr_stl_touch_defaults(&g_tc.cfg);
	static const char *const ow[] = { "off", "on" };
	g_tc.want = env_word("SOMVR_STEALTH_TOUCH", ow, 2, 0);
	g_tc.cfg.cm = env_float("SOMVR_STEALTH_TOUCH_CM", g_tc.cfg.cm, 0, 100, bad);
	g_tc.cfg.behind_deg = env_float("SOMVR_STEALTH_TOUCH_BEHIND_DEG", g_tc.cfg.behind_deg, 90, 180, bad);
	g_tc.cfg.cms = env_float("SOMVR_STEALTH_TOUCH_CMS", g_tc.cfg.cms, 0, 400, bad);
	if (!g_tc.want)
		return;
	if (g_stl.mode != STL_ON) {
		logf_("WARN SOMVR_STEALTH_TOUCH=on needs SOMVR_STEALTH_KILL=on: the touch stays off");
		(*bad)++;
		return;
	}
	void *fn[TC_N] = { (void *)somvr_w_tc_fov, (void *)somvr_w_tc_hear, (void *)somvr_w_tc_bubble };
	char why[512], err[200] = "";
	for (int k = 0; k < TC_N; k++)
		if (check_site(&g_tc_sites_game[k], why, sizeof why) != 0) {
			logf_("WARN SOMVR_STEALTH_TOUCH: the site %s %#llx did not match (%s): off", g_tc_sites_game[k].name,
			      (unsigned long long)g_tc_sites_game[k].addr, why);
			(*bad)++;
			return;
		}
	for (int k = 0; k < TC_N; k++) {
		g_tc.orig[k] = g_tc_sites_game[k].target;
		if (write_code(g_tc_sites_game[k].addr, &fn[k], 8, err, sizeof err) != 0) {
			logf_("WARN SOMVR_STEALTH_TOUCH: the slot %s %#llx not written (%s)%s", g_tc_sites_game[k].name,
			      (unsigned long long)g_tc_sites_game[k].addr, err, k ? "; the earlier slots stay in, the touch off" : ": off");
			(*bad)++;
			return;
		}
		g_tc.ok[k] = 1;
	}
	g_tc.in = 1, g_tc.cfg.on = 1;
	memset(&g_tcc, 0, sizeof g_tcc);
	g_tcc.want = env_word("SOMVR_STEALTH_CLOSE", ow, 2, 0);
	g_tcc.stop_cm = env_float("SOMVR_STEALTH_CLOSE_CM", 52.0f, 40, 80, bad);
	g_tcc.near_cm = env_float("SOMVR_STEALTH_CLOSE_NEAR_CM", 160.0f, 90, 400, bad);
	g_tcc.clear_cm = env_float("SOMVR_STEALTH_CLOSE_CLEAR_CM", 180.0f, 90, 400, bad);
	if (g_tcc.clear_cm < g_tcc.near_cm + 10.0f) /* boot 20261105-103516: on at 160, off at 100 flapped every tick at 140 */
		g_tcc.clear_cm = g_tcc.near_cm + 10.0f;
	if (g_tcc.want && !g_cl.vt_pc) {
		logf_("WARN SOMVR_STEALTH_CLOSE=on needs the climb's reads (SOMVR_CLIMB: Talion's physics character): off");
		g_tcc.want = 0, (*bad)++;
	}
	if (g_tcc.want) {
		void *f = (void *)somvr_w_tc_pcb;
		if (check_site(&g_tcc_sites_game[0], why, sizeof why) != 0) {
			logf_("WARN SOMVR_STEALTH_CLOSE: the site TC_PCB %#llx did not match (%s): off",
			      (unsigned long long)g_tcc_sites_game[0].addr, why);
			(*bad)++;
		} else if (g_tcc.orig = g_tcc_sites_game[0].target, write_code(g_tcc_sites_game[0].addr, &f, 8, err, sizeof err) != 0) {
			logf_("WARN SOMVR_STEALTH_CLOSE: the slot TC_PCB %#llx not written (%s): off",
			      (unsigned long long)g_tcc_sites_game[0].addr, err);
			(*bad)++;
		} else {
			g_tcc.in = 1;
			logf_("CONFIG stealth close=on %.0f cm (SOMVR_STEALTH_CLOSE, _CM): sneaking within %.0f cm of an unaware orc's back "
			      "(_NEAR_CM; the touch zone), that orc's body no longer stops Talion (its contact planes cleared from his "
			      "proxy's steps: the slot TC_PCB %#llx, the game's listener first; walls and every other body the game's); "
			      "his walk stops at this distance root to root instead - never through it, never a push; on for that orc "
			      "until he is %.0f cm from it (_CLEAR_CM)", (double)g_tcc.stop_cm, (double)g_tcc.near_cm,
			      (unsigned long long)g_tcc_sites_game[0].addr, (double)g_tcc.clear_cm);
		}
	}
	float so = g_stl.so_cm;
	g_stl.so_cm = 0;
	char within[48] = "never notice him";
	if (g_tc.cfg.cm > 0)
		snprintf(within, sizeof within, "notice him only within %.0f cm", (double)g_tc.cfg.cm);
	logf_("CONFIG stealth touch=on %.0f cm (SOMVR_STEALTH_TOUCH, _CM): crouched, at most %.0f cm/s (_CMS), at least %.0f deg off "
	      "an unaware orc's facing (_BEHIND_DEG: its back arc), its sight's no-FOV radius, its hearing and its personal bubble "
	      "%s (root to root); in front, beside, standing, fast or noticed: the game's "
	      "(it notices any Talion within ~108 cm, docs 72.1). The slots TC_FOV %#llx, TC_HEAR %#llx, TC_BUBBLE %#llx; the "
	      "stand-off off (was %.0f cm: he walks right up)", (double)g_tc.cfg.cm, (double)g_tc.cfg.cms,
	      (double)g_tc.cfg.behind_deg, within, (unsigned long long)g_tc_sites_game[0].addr,
	      (unsigned long long)g_tc_sites_game[1].addr, (unsigned long long)g_tc_sites_game[2].addr, (double)so);
}

static void tc_end_log(void)
{
	if (!g_tc.in)
		return;
	tc_close_end(); /* build 40b: a thin capsule back to full */
	char b[600];
	int l = 0;
	for (int k = 0; k < TC_N; k++)
		l += snprintf(b + l, sizeof b - (size_t)l, "%s%s: %llu calls, Talion's %llu, passed %llu (within 3 m %llu), cut %llu",
			      k ? "; " : "", g_tc_sense[k], (unsigned long long)g_tc.calls[k], (unsigned long long)g_tc.mine[k],
			      (unsigned long long)g_tc.passed[k], (unsigned long long)g_tc.near_[k], (unsigned long long)g_tc.cut[k]);
	logf_("TOUCH END %s; ticks %llu, orc-ticks in the zone %llu (%llu entries), the nearest in the zone %.0f cm", b,
	      (unsigned long long)g_tc.ticks, (unsigned long long)g_tc.zone_ticks, (unsigned long long)g_tc.zone_ins,
	      (double)g_tc.min_quiet_cm);
}
