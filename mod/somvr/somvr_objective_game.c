/* somvr_objective_game.c - SOMVR_OBJECTIVE (build 15 candidate, docs/re-xr-pipeline.md 44): the HUD's objective
 * marker points the way in VR. Included by somvr.c after the front file (it uses bo_patch_call, rx_rec_name, peek,
 * logf_, g_fr, g_rig, player_objs and the threat's world -> XR mapping).
 *
 * Why it was wrong: the game's nav markers (CNavMarkerInstance, the list 0x46b9200; re-hud.md 3) are placed by
 * FUN_00ec4a50(marker, player) once per view: the marker's world position (FUN_00ebc700), the range test
 * (FUN_00ebced0), then FUN_00a60920(player camera, pos, &onScreen, &behind) = the gameplay camera's own 43 x 25 deg
 * projection (re-ui-effects.md 2.1), then the icon on the flat HUD (FUN_00ec2660) or its edge arrow (FUN_00ec3900).
 * In VR the HUD is one quad and the camera is not the head: the coin sat at the quad's edge with an arrow whatever the
 * head did (re-xr-pipeline.md 43.6).
 * What this does: OB_EACH (the loop's call 0xec4f83 of FUN_00ec4a50) is ours: the original, then for a marker whose
 * record name matches SOMVR_OBJECTIVE_RECS and that passes the game's own tests before the projection (active,
 * enabled, a position, within its range) its world position is kept for this frame; with SOMVR_OBJECTIVE=on the
 * game's icon is hidden in our stereo frames by the game's own hide (the shown bytes +0x45/+0x6d = 0, the clip's
 * vf_0x208(0): what FUN_00ec4a50 does out of range). After the frame's body (H4) the nearest such marker goes to the
 * layer through `somvr_objective` (its place mapped with the frame's own rig, its distance from Talion); the layer
 * draws a small diamond world-locked over it with the distance, or at the view's edge an arrow (somvr_objective.h).
 * Nothing moves, nothing is pressed. SOMVR_OBJECTIVE=log: the hook logs every marker the game shows (record names,
 * places), nothing changed, nothing published. Fail soft: a site mismatch = nothing patched. */

/* OBJECTIVE_SITES_BEGIN (check_sites.py counts this block on its own: the patched OB_EACH = the call 0xec4f83 of
 * FUN_00ec4a50(marker %rdi = node - 0x80, player %rsi) in FUN_00ec4f10's walk of the list 0x46b9200 (the context from
 * 0xec4f70: the node, next, the marker, the call); called: OB_POS = FUN_00ebc700(marker, float out[3]) -> bool (the
 * marker's world position, the probe's F_NAV_POS), OB_RANGE = FUN_00ebced0(marker) -> {min, max} metres in xmm0 (its
 * record's range; FUN_00ec4a50 hides a marker outside it). Order = OBS_*) */
static const struct site_spec g_ob_sites_game[] = {
SITE("OB_EACH", K_CALL, 0xec4f83, 0xec4f70, "48 89 dd 4c 8b 63 08 48 83 c5 80 74 2a 4c 89 ee 48 89 ef e8 c8 fa ff ff", 0xec4a50, "41 57 41 56 41 55 41 54 49 89 fc 55 53 48 89"),
SITE("OB_POS", K_ENTRY, 0xebc700, 0xebc700, "48 8b 87 50 02 00 00 48 85 c0 74 0f 48 89 c7 48", 0xebc700, "48 8b 87 50 02 00 00 48 85 c0 74 0f 48 89 c7"),
SITE("OB_RANGE", K_ENTRY, 0xebced0, 0xebced0, "55 53 48 83 ec 28 48 8b 1d 43 a0 ca 03 48 8b af", 0xebced0, "55 53 48 83 ec 28 48 8b 1d 43 a0 ca 03 48 8b"),
};
/* OBJECTIVE_SITES_END */
enum { OBS_EACH, OBS_POS, OBS_RANGE, OBS_N };
_Static_assert(sizeof g_ob_sites_game / sizeof g_ob_sites_game[0] == OBS_N, "objective sites");
static const struct site_spec *g_ob_sites = g_ob_sites_game; /* selftest: stand-ins */
#define OB_SHOWN1 0x45   /* CNavMarkerInstance: byte, the primary icon shown this view (re-hud.md 3.2) */
#define OB_SHOWN2 0x6d   /* ... the secondary */
#define OB_CLIP 0xb0     /* ... the icon's Scaleform clip (vf_0x208 = set visible) */
#define OB_REC 0x298     /* ... the NavMarkerData record */
#define OB_ACTIVE 0x2e0  /* ... bytes active, enabled (+0x2e1) */
#define OB_CLIP_VIS 0x208
#define OB_MARKS 48      /* markers tracked (log, status) */
#define OB_RECS 256      /* records' names and matches cached (open addressing on the pointer) */

typedef void (*ob_each_fn)(uintptr_t m, uintptr_t pl);
typedef int (*ob_pos_fn)(uintptr_t m, float *out); /* bool in al */
typedef struct { float lo, hi; } ob_range_t;
typedef ob_range_t (*ob_range_fn)(uintptr_t m);
typedef void (*ob_vis_fn)(uintptr_t clip, uint64_t on);

/* the ninth exported object (somvr_objective.h): zero unless SOMVR_OBJECTIVE=on|both is on */
__attribute__((visibility("default"), used)) somvr_objective_shared g_obj __asm__(SOMVR_OBJECTIVE_SYMBOL);

struct ob_mark {
	uintptr_t m, rec;
	int match;          /* its record matches SOMVR_OBJECTIVE_RECS */
	int shown;          /* the game placed it in the last call (+0x45 | +0x6d) */
	int elig;           /* the game's own tests before the projection passed (matches only) */
	int hidden;         /* we hid it in the last call */
	float pos[3];       /* its world position (matches: every call; others: at first sight) */
	double t_first, t_seen, t_shown;
	uint32_t id;
	char name[48];
};
static struct {
	int mode;           /* 0 off, 1 log, 2 on (ours, the game's hidden), 3 both (ours and the game's) */
	int on;             /* OB_EACH installed */
	int live;           /* the word "objective on|game|both": 2 on, 0 the game's only, 3 both (starts = mode) */
	uintptr_t orig;
	char pats[256];
	long tid;
	struct ob_mark mk[OB_MARKS];
	int nmk;
	struct { uintptr_t rec; int match; char name[48]; } rc[OB_RECS];
	int nrc;          /* filled entries (the table is cleared when 3/4 full) */
	uint32_t next_id;
	/* the frame's choice (ob_frame) */
	uintptr_t cur;      /* the marker published (0 none) */
	uint32_t cur_id;
	double next_sum, cur_t;
	float cur_dist;
	uint64_t calls, shown_calls, match_calls, hides, frames, published, other_tid;
	uint32_t lines;
	int range_ok, pos_ok;
	int land, land_was;  /* build 16: the climb's landing ring published (SOMVR_CLIMB_PULLOVER), shown last frame */
	int ws, ws_was;      /* build 18: the wraith step's ring published (SOMVR_WRAITH_STEP), shown last frame (1 gold, 2 red) */
	int aim;             /* build 20: the bow's aim ring published (SOMVR_BOW_AIM_RING) */
	uint64_t land_frames;
	int strike;          /* build 17: the Shadow Strike's mark published (SOMVR_STRIKE_GAZE) */
	uint64_t strike_frames;
} g_ob;

static long ob_tid(void) { return (long)syscall(SYS_gettid); }

/* the record's name and match, cached (DB records stay put; the table starts again when 3/4 full) */
static int ob_rec(uintptr_t rec, const char **name)
{
	unsigned h = (unsigned)((rec >> 4) * 2654435761u) % OB_RECS;
	for (int n = 0; n < OB_RECS; n++, h = (h + 1) % OB_RECS) {
		if (g_ob.rc[h].rec == rec)
			return *name = g_ob.rc[h].name, g_ob.rc[h].match;
		if (!g_ob.rc[h].rec)
			break;
	}
	if (g_ob.nrc >= OB_RECS * 3 / 4) {
		memset(g_ob.rc, 0, sizeof g_ob.rc), g_ob.nrc = 0;
		h = (unsigned)((rec >> 4) * 2654435761u) % OB_RECS;
	}
	g_ob.nrc++;
	g_ob.rc[h].rec = rec;
	rx_rec_name(rec, g_ob.rc[h].name, sizeof g_ob.rc[h].name);
	g_ob.rc[h].match = somvr_ob_match(g_ob.pats, g_ob.rc[h].name);
	*name = g_ob.rc[h].name;
	return g_ob.rc[h].match;
}

static struct ob_mark *ob_mark_of(uintptr_t m, uintptr_t rec, const char *name, int match, double t)
{
	struct ob_mark *k = NULL, *old = NULL;
	for (int i = 0; i < g_ob.nmk; i++) {
		if (g_ob.mk[i].m == m && g_ob.mk[i].rec == rec)
			return &g_ob.mk[i];
		if (!old || g_ob.mk[i].t_seen < old->t_seen)
			old = &g_ob.mk[i];
	}
	k = g_ob.nmk < OB_MARKS ? &g_ob.mk[g_ob.nmk++] : old; /* full: the longest unseen one goes */
	memset(k, 0, sizeof *k);
	k->m = m, k->rec = rec, k->match = match, k->t_first = t, k->id = ++g_ob.next_id;
	snprintf(k->name, sizeof k->name, "%s", name);
	return k;
}

/* OB_EACH (the game thread, per view): the game's placement, then ours */
__attribute__((used)) void somvr_w_obeach(uintptr_t m, uintptr_t pl)
{
	((ob_each_fn)g_ob.orig)(m, pl);
	g_ob.calls++;
	if (!m)
		return;
	if (!g_ob.tid)
		g_ob.tid = ob_tid();
	int shown = *(volatile uint8_t *)(m + OB_SHOWN1) || *(volatile uint8_t *)(m + OB_SHOWN2);
	uintptr_t rec = *(volatile uintptr_t *)(m + OB_REC);
	if (!rec)
		return;
	if (g_ob.tid != ob_tid()) {
		g_ob.other_tid++;
		return;
	}
	const char *name = "";
	int match = ob_rec(rec, &name);
	if (!match && !shown) /* the objectives, and every marker the game shows (logged at first sight) */
		return;
	double t = mono();
	g_ob.shown_calls += shown, g_ob.match_calls += match;
	struct ob_mark *k = ob_mark_of(m, rec, name, match, t);
	int first = k->t_seen == 0;
	k->t_seen = t, k->shown = shown;
	if (shown)
		k->t_shown = t;
	if (match || first) { /* the place: an objective's every call (it may move with its target), the others' once */
		float p[3] = { 0, 0, 0 };
		int ok = ((ob_pos_fn)g_ob_sites[OBS_POS].target)(m, p) & 0xff;
		if (ok && vec_ok(p, 1e7f))
			memcpy(k->pos, p, sizeof p);
		if (match) { /* FUN_00ec4a50's own tests before its projection (the range against the player's position) */
			uint8_t ae[2] = { *(volatile uint8_t *)(m + OB_ACTIVE), *(volatile uint8_t *)(m + OB_ACTIVE + 1) };
			int elig = ok && ae[0] && ae[1];
			if (elig && g_ob.range_ok) {
				float tp[3];
				uintptr_t pm = 0, obj = 0;
				if (player_objs(pl, &pm, &obj) && peek(obj + OBJ_POS, tp, sizeof tp)) {
					ob_range_t r = ((ob_range_fn)g_ob_sites[OBS_RANGE].target)(m);
					float d = dist3(tp, p) / 100.0f;
					elig = !(d < r.lo) && !(r.hi < d);
				}
			}
			k->elig = elig;
		}
	}
	if (first && g_ob.lines < 400) {
		g_ob.lines++;
		logf_("OBJECTIVE t=%.3f marker %u m=%#lx '%s'%s: %s at (%.0f %.0f %.0f)", t, k->id, (unsigned long)m, name,
		      match ? " (an objective)" : "", shown ? "shown by the game" : "not shown", k->pos[0], k->pos[1], k->pos[2]);
	}
	k->hidden = 0;
	if (match && shown && g_ob.live == 2 && g_fr.on) { /* ours in the eyes: the game's own hide (FUN_00ec4a50's) */
		*(volatile uint8_t *)(m + OB_SHOWN1) = 0, *(volatile uint8_t *)(m + OB_SHOWN2) = 0;
		uintptr_t clip = *(volatile uintptr_t *)(m + OB_CLIP);
		if (clip) {
			uintptr_t vt = *(volatile uintptr_t *)clip;
			((ob_vis_fn) * (volatile uintptr_t *)(vt + OB_CLIP_VIS))(clip, 0);
		}
		k->hidden = 1, g_ob.hides++;
	}
}

/* after the frame's body (H4, the game thread): the objective of this frame to the layer; build 16: while the climb's
 * pullover is armed, its landing instead (SOMVR_OBF_LAND: a ring on the floor where he will stand); build 17: while A
 * chooses a drop (SOMVR_CLIMB_DESCENT_MARK), its landing and the line from the edge down to it (SOMVR_OBF_LINE) */
static void ob_frame(uintptr_t pl, int stereo)
{
	int pub = g_ob.mode >= 2 && g_ob.on;
	if ((!pub && !g_ob.land && !g_ob.strike && !g_ob.ws && !g_ob.aim) || !stereo)
		return;
	if (g_ob.tid && g_ob.tid != ob_tid())
		return;
	somvr_objective_frame e;
	memset(&e, 0, sizeof e);
	int dm = g_cl.dm_show; /* the drop's choice over the pullover's ring (A is the newer act) */
	if (g_ob.land && g_cl.on && (g_cl.po_show || dm) && g_cl.tid == my_tid() && !game_paused()) {
		somvr_th_map mp = { { g_fr.base.pos.x, g_fr.base.pos.y, g_fr.base.pos.z }, g_fr.base.yaw - g_rig.yaw0,
				    { g_rig.p0.x, g_rig.p0.y, g_rig.p0.z }, somvr_rig_scale(&g_rig) };
		sv_vec3 l = dm ? g_cl.dm_land : g_cl.po_land;
		float w[3] = { l.x, l.y, l.z }, w2[3] = { w[0] + 35.0f, w[1], w[2] }, x2[3];
		somvr_th_to_xr(&mp, w, e.pos), somvr_th_to_xr(&mp, w2, x2);
		e.n = 1, e.flags = SOMVR_OBF_LAND, e.id = 0;
		e.dist_m = sqrtf((x2[0] - e.pos[0]) * (x2[0] - e.pos[0]) + (x2[1] - e.pos[1]) * (x2[1] - e.pos[1]) +
				 (x2[2] - e.pos[2]) * (x2[2] - e.pos[2])); /* the ring's radius: Talion's capsule's (35 cm) */
		if (dm) { /* the line from the edge's (or the feet's) height straight down to the ring */
			float wt[3] = { g_cl.dm_top.x, g_cl.dm_top.y, g_cl.dm_top.z };
			somvr_th_to_xr(&mp, wt, e.top);
			e.flags |= SOMVR_OBF_LINE;
		}
		if (dm && g_cl.cfg.dm_timer && g_cl.s.dm == 1) { /* build 21: the hold's timer in the ring (SOMVR_CLIMB_DESCENT_TIMER):
								      * the white ring's steps left, then armed - flashing 3 beats, then lit */
			float a = g_cl.dm_armed_s;
			e.flags |= SOMVR_OBF_TIMER;
			e.id = a >= 0 ? (a < 0.36f && ((int)(a / 0.06f) & 1) ? 0u : (uint32_t)SOMVR_OB_TIMER_ARMED)
				      : (uint32_t)ceilf((1.0f - g_cl.dm_held) * SOMVR_OB_TIMER_STEPS - 1e-3f);
			if (e.id > SOMVR_OB_TIMER_STEPS && a < 0)
				e.id = SOMVR_OB_TIMER_STEPS;
			g_cl.dm_timer_frames++;
		}
		somvr_objective_put(&g_obj, g_fr.frame, &e);
		if (g_ob.land_was != 1 + dm && g_ob.lines++ < 400)
			logf_("OBJECTIVE t=%.3f frame=%llu: the climb's landing in the eyes: a ring at (%.0f %.0f %.0f), xr (%+.2f %+.2f "
			      "%+.2f) m, radius %.2f m%s (%s)", mono(), (unsigned long long)g_fr.frame, w[0], w[1], w[2], e.pos[0],
			      e.pos[1], e.pos[2], (double)e.dist_m, dm ? ", the line from its top 2 cm over it down to it" : "",
			      dm ? "SOMVR_CLIMB_DESCENT_MARK" : "SOMVR_CLIMB_PULLOVER");
		g_ob.land_was = 1 + dm, g_ob.land_frames++, g_ob.cur = 0;
		return;
	}
	g_ob.land_was = 0;
	if (g_ob.ws && ws_entry(&e)) { /* build 18: the wraith step's landing (SOMVR_WRAITH_STEP: gold, or red and crossed) */
		somvr_objective_put(&g_obj, g_fr.frame, &e);
		if (g_ob.ws_was != 1 + !!(e.flags & SOMVR_OBF_LAND_NO) && g_ob.lines++ < 400)
			logf_("OBJECTIVE t=%.3f frame=%llu: the wraith step's landing in the eyes: %s ring, xr (%+.2f %+.2f %+.2f) m, "
			      "radius %.2f m (SOMVR_WRAITH_STEP)", mono(), (unsigned long long)g_fr.frame,
			      e.flags & SOMVR_OBF_LAND_NO ? "the red crossed" : "the gold", e.pos[0], e.pos[1], e.pos[2], (double)e.dist_m);
		g_ob.ws_was = 1 + !!(e.flags & SOMVR_OBF_LAND_NO), g_ob.cur = 0;
		return;
	}
	g_ob.ws_was = 0;
	if (g_ob.aim && ba_entry(&e)) { /* build 20: the drawn bow's aim ring (SOMVR_BOW_AIM_RING) */
		somvr_objective_put(&g_obj, g_fr.frame, &e);
		g_ob.cur = 0;
		return;
	}
	if (g_ob.strike && sg_entry(&e)) { /* build 17: the Shadow Strike's mark on the orc looked at (SOMVR_STRIKE_GAZE) */
		somvr_objective_put(&g_obj, g_fr.frame, &e);
		g_ob.strike_frames++, g_ob.cur = 0;
		return;
	}
	if (!pub) {
		somvr_objective_put(&g_obj, g_fr.frame, &e); /* nothing */
		return;
	}
	double t = mono();
	g_ob.frames++;
	float tp[3] = { 0, 0, 0 };
	uintptr_t pm = 0, obj = 0;
	int body = pl && player_objs(pl, &pm, &obj) && peek(obj + OBJ_POS, tp, sizeof tp) && vec_ok(tp, 1e7f);
	struct ob_mark *best = NULL;
	float bd = 0;
	for (int i = 0; body && !game_paused() && g_ob.live && i < g_ob.nmk; i++) { /* the nearest objective seen now */
		struct ob_mark *k = &g_ob.mk[i];
		if (!k->match || !k->elig || t - k->t_seen > 0.25)
			continue;
		float d = dist3(tp, k->pos);
		if (!best || d < bd)
			best = k, bd = d;
	}
	if (best) {
		somvr_th_map mp = { { g_fr.base.pos.x, g_fr.base.pos.y, g_fr.base.pos.z }, g_fr.base.yaw - g_rig.yaw0,
				    { g_rig.p0.x, g_rig.p0.y, g_rig.p0.z }, somvr_rig_scale(&g_rig) };
		somvr_th_to_xr(&mp, best->pos, e.pos);
		e.n = 1, e.dist_m = bd / 100.0f, e.id = best->id;
		e.flags = (best->hidden ? SOMVR_OBF_HIDDEN : 0u) | (t - best->t_shown < 0.25 ? SOMVR_OBF_SHOWN : 0u);
		g_ob.published++;
	}
	somvr_objective_put(&g_obj, g_fr.frame, &e);
	uintptr_t cur = best ? best->m : 0;
	if (cur != g_ob.cur || (best && best->id != g_ob.cur_id)) {
		if (g_ob.lines++ < 400)
			logf_("OBJECTIVE t=%.3f frame=%llu: %s%s%s%s", t, (unsigned long long)g_fr.frame,
			      best ? "the marker in the eyes: '" : "no objective in the eyes",
			      best ? best->name : "", best ? "'" : "", best ? "" : (game_paused() ? " (paused)" : ""));
		g_ob.cur = cur, g_ob.cur_id = best ? best->id : 0, g_ob.next_sum = 0;
	}
	if (best && t >= g_ob.next_sum) {
		g_ob.next_sum = t + 5.0;
		logf_("OBJECTIVE t=%.3f frame=%llu '%s' marker %u at (%.0f %.0f %.0f), %.1f m from Talion at (%.0f %.0f %.0f); "
		      "xr (%+.2f %+.2f %+.2f) m; the game's icon %s%s; layer: marker frames %llu, edge frames %llu",
		      t, (unsigned long long)g_fr.frame, best->name, best->id, best->pos[0], best->pos[1], best->pos[2],
		      (double)e.dist_m, tp[0], tp[1], tp[2], e.pos[0], e.pos[1], e.pos[2],
		      e.flags & SOMVR_OBF_SHOWN ? "placed" : "not placed", e.flags & SOMVR_OBF_HIDDEN ? " (hidden by us)" : "",
		      (unsigned long long)atomic_load(&g_obj.n_marker), (unsigned long long)atomic_load(&g_obj.n_edge));
	}
}

/* "objective status|on|game|both|recs PATTERNS" */
static const char *ob_word(const char *w)
{
	if (!g_ob.on)
		return "objective: not on (SOMVR_OBJECTIVE unset or OB_EACH not hooked)";
	if (!strncmp(w, "recs ", 5)) { /* the record patterns live (the cache and the tracked markers' matches again) */
		char p[256];
		snprintf(p, sizeof p, "%s", w + 5);
		p[strcspn(p, " \r\n")] = 0;
		snprintf(g_ob.pats, sizeof g_ob.pats, "%s", p);
		memset(g_ob.rc, 0, sizeof g_ob.rc), g_ob.nrc = 0;
		for (int i = 0; i < g_ob.nmk; i++)
			g_ob.mk[i].match = somvr_ob_match(g_ob.pats, g_ob.mk[i].name);
		logf_("OBJECTIVE records now '%s'", g_ob.pats);
		return "objective: records set";
	}
	if (!strncmp(w, "on", 2) || !strncmp(w, "game", 4) || !strncmp(w, "both", 4)) {
		if (g_ob.mode < 2)
			return "objective: SOMVR_OBJECTIVE=log publishes nothing";
		g_ob.live = w[0] == 'o' ? 2 : w[0] == 'g' ? 0 : 3;
		logf_("OBJECTIVE live: %s", g_ob.live == 2 ? "ours, the game's icon hidden" : g_ob.live ? "ours and the game's"
											      : "the game's icon only");
		return "objective: set";
	}
	double t = mono();
	logf_("OBJECTIVE status t=%.3f: mode %d live %d; calls %llu (shown %llu, objectives %llu), hides %llu, frames %llu "
	      "published %llu, other threads %llu; %d markers tracked, %d records", t, g_ob.mode, g_ob.live,
	      (unsigned long long)g_ob.calls, (unsigned long long)g_ob.shown_calls, (unsigned long long)g_ob.match_calls,
	      (unsigned long long)g_ob.hides, (unsigned long long)g_ob.frames, (unsigned long long)g_ob.published,
	      (unsigned long long)g_ob.other_tid, g_ob.nmk, g_ob.nrc);
	for (int i = 0; i < g_ob.nmk; i++) {
		const struct ob_mark *k = &g_ob.mk[i];
		if (t - k->t_seen > 5.0)
			continue;
		logf_("OBJECTIVE status marker %u m=%#lx '%s'%s at (%.0f %.0f %.0f): seen %.1f s ago, shown %.1f s ago%s%s", k->id,
		      (unsigned long)k->m, k->name, k->match ? " (an objective)" : "", k->pos[0], k->pos[1], k->pos[2],
		      t - k->t_seen, k->t_shown > 0 ? t - k->t_shown : -1.0, k->match ? (k->elig ? ", eligible" : ", not eligible") : "",
		      k->hidden ? ", hidden by us" : "");
	}
	return "objective: status logged";
}

/* the layer's object on (the objective's settings c); why: a log line (the landing ring alone) */
static void ob_publish(const somvr_ob_cfg *c, const char *why)
{
	g_obj.magic = SOMVR_OBJECTIVE_MAGIC, g_obj.version = SOMVR_OBJECTIVE_VERSION;
	g_obj.size = (uint32_t)sizeof g_obj, g_obj.pid = (int32_t)getpid();
	g_obj.size_deg = c->size_deg, g_obj.dist_m = c->dist_m, g_obj.faint_deg = c->faint_deg, g_obj.edge_deg = c->edge_deg;
	atomic_store_explicit(&g_obj.active, 1, memory_order_release);
	if (why)
		logf_("CONFIG objective: the layer's object on for %s", why);
}

static void ob_start(int *bad)
{
	somvr_ob_cfg c0;
	somvr_ob_cfg_default(&c0);
	static const char *const w[] = { "off", "log", "on", "both" };
	memset(&g_ob, 0, sizeof g_ob);
	g_ob.mode = env_word("SOMVR_OBJECTIVE", w, 4, 0);
	g_ob.land = g_cl.on && (g_cl.cfg.pullover || g_cl.cfg.drop_mark || g_cl.cfg.mantle); /* build 16: the climb's landing
									   * ring (SOMVR_CLIMB_PULLOVER; build 17: _DESCENT_MARK; 20: _MANTLE) */
	g_ob.strike = g_sg.on; /* build 17: the Shadow Strike's mark (SOMVR_STRIKE_GAZE) */
	g_ob.ws = ws_wanted(); /* build 18: the wraith step's landing (SOMVR_WRAITH_STEP) */
	g_ob.aim = ba_wanted(); /* build 20: the bow's aim ring (SOMVR_BOW_AIM_RING) */
	if (!g_ob.mode) {
		if (g_ob.land || g_ob.strike || g_ob.ws || g_ob.aim)
			ob_publish(&c0, "the climb's landing and the strike's mark only (SOMVR_OBJECTIVE off)");
		return;
	}
	const char *p = getenv("SOMVR_OBJECTIVE_RECS");
	snprintf(g_ob.pats, sizeof g_ob.pats, "%s", p && *p ? p : "Objective*");
	somvr_ob_cfg c;
	somvr_ob_cfg_default(&c);
	float size = env_float("SOMVR_OBJECTIVE_SIZE_DEG", c.size_deg, 1, 15, bad);
	float dist = env_float("SOMVR_OBJECTIVE_DIST_M", c.dist_m, 2, 100, bad);
	float faint = env_float("SOMVR_OBJECTIVE_FAINT_DEG", c.faint_deg, 0, 30, bad);
	float edge = env_float("SOMVR_OBJECTIVE_EDGE_DEG", c.edge_deg, 1, 15, bad);
	char why[512];
	int ok = 1;
	for (int i = OBS_POS; i < OBS_N; i++) {
		int k = check_site(&g_ob_sites[i], why, sizeof why) == 0;
		logf_("OBJECTIVE site %s %s %#lx: %s", g_ob_sites[i].name, k ? "ok" : "MISMATCH", (unsigned long)g_ob_sites[i].addr, why);
		ok &= k || i == OBS_RANGE;
		if (i == OBS_RANGE)
			g_ob.range_ok = k;
	}
	g_ob.pos_ok = ok;
	g_ob.orig = g_ob_sites[OBS_EACH].target;
	if (!ok || !(g_ob.on = bo_patch_call(&g_ob_sites[OBS_EACH], (void *)somvr_w_obeach))) {
		logf_("WARN SOMVR_OBJECTIVE=%s: %s: the game's markers as they are", w[g_ob.mode],
		      ok ? "OB_EACH not hooked" : "OB_POS differs");
		if (g_ob.land || g_ob.strike)
			ob_publish(&c0, "the climb's landing and the strike's mark only (OB_EACH not hooked)");
		return;
	}
	g_ob.live = g_ob.mode == 1 ? 0 : g_ob.mode;
	if (g_ob.mode >= 2 || g_ob.land || g_ob.strike) {
		c.size_deg = size, c.dist_m = dist, c.faint_deg = faint, c.edge_deg = edge;
		ob_publish(&c, g_ob.mode >= 2 ? NULL : "the climb's landing and the strike's mark only (SOMVR_OBJECTIVE=log)");
	}
	logf_("CONFIG objective=%s (SOMVR_OBJECTIVE: %s), records '%s'; the marker %.1f deg, at most %.0f m away, faint within "
	      "%.0f deg of the view's centre, the edge arrow %.1f deg; range test %s", w[g_ob.mode],
	      g_ob.mode == 1 ? "the markers the game shows logged, nothing changed"
	      : g_ob.mode == 2 ? "our marker in the world, the game's icon hidden in our stereo frames"
			       : "our marker in the world and the game's icon", g_ob.pats, size, dist, faint, edge,
	      g_ob.range_ok ? "the game's" : "none (OB_RANGE differs)");
}

static void ob_end_log(void)
{
	if (g_ob.land)
		logf_("OBJECTIVE END the climb's landing ring: frames %llu (SOMVR_CLIMB_PULLOVER, _DESCENT_MARK: %llu with the "
		      "drop's line)", (unsigned long long)g_ob.land_frames, (unsigned long long)g_cl.dm_frames);
	if (!g_ob.on)
		return;
	logf_("OBJECTIVE END calls %llu (shown %llu, objectives %llu), hides %llu, frames %llu, published %llu, other threads "
	      "%llu; layer: marker frames %llu, edge frames %llu", (unsigned long long)g_ob.calls,
	      (unsigned long long)g_ob.shown_calls, (unsigned long long)g_ob.match_calls, (unsigned long long)g_ob.hides,
	      (unsigned long long)g_ob.frames, (unsigned long long)g_ob.published, (unsigned long long)g_ob.other_tid,
	      (unsigned long long)atomic_load(&g_obj.n_marker), (unsigned long long)atomic_load(&g_obj.n_edge));
}
