/* somvr_size_game.c - the player size, the game side (docs/re-xr-pipeline.md 35; included by somvr.c last; the pure
 * part is somvr_size.c):
 *   SOMVR_PLAYER_SIZE=PCT   100 (default) .. 140: the player's size in first person on the head base (SOMVR_BASE=head;
 *                           the camera base and third person stay 1:1). The rig scales about Talion's feet: the base
 *                           rises by (s - 1) x the eye height above the feet, and the tracked offsets from the
 *                           recentre's head (the eye separation, the head's room motion, the hands) are x s. The VR
 *                           menu's "Player size" row (PLAYER_SIZE in SOMVR_SETTINGS) changes it live: the change lands
 *                           on the black frame of a short blink (the record's fade), never while a climb hold is on.
 *   SOMVR_SIZE_ROOT_CM=104  Talion's root above his feet: the eye height above the feet at 100 % = the anchor's eye
 *                           height (SOMVR_ANCHOR_EYE_CM, 76.6; the Head node's 72.5 without SOMVR_ANCHOR=root) + this
 *                           + SOMVR_EYE_CM
 *   SOMVR_SIZE_BLINK_MS=350 the change's blink (a third to black, the rest back; 0 = no blink, the change at once)
 *   SOMVR_SIZE_BODY=0|1     (default 0) Talion's model at the player size while only his hands are drawn (first
 *                           person, SOMVR_BODY=hands' live frames): his object's scale obj+0x24 = s, written raw (no
 *                           SetObjectScale: the game's ray-hit dims stay), so his hands and the sword are the player's
 *                           size; 1 again in third person, a flat frame, the body moments' whole body and at the end.
 *                           The arms lay the hands / s in model space (sz_body_scale), the hit blade is the sword's
 *                           length x s, the Head node read is unscaled about the root (his capsule did not grow)
 *   SOMVR_ORC_SCALE=S       experimental (default off): our practice orc's size S (0.7 .. 1.5) at its spawn, the game's
 *                           own AI scale (the NPC's CCharacter+0xe08, which the game eases the object's scale obj+0x24
 *                           toward at 0.1/s: NPC::vf_10's AIScaleRange roll, FUN_029779d0; the model, the weapons, the
 *                           ray-hit dims follow; the collision capsule char+0x9d0 and the attacks' ApplyDamageRange do
 *                           NOT, and the model grows about the root, 104 cm over the feet: the feet sink 104 x (S - 1))
 * Trigger words (headless): `size PCT` (as the menu: the blink, the landing), `size probe` (the measures below, once,
 * at the next stereo frame), `size status`, `size orc S` (our practice orc's AI scale now, as SOMVR_ORC_SCALE),
 * `size talion S` (a test only: Talion's object scale obj+0x24 written raw, 0.7 .. 1.5, 1 = back; does his model grow?).
 * The probe (SIZEPROBE lines): Talion's and every orc's within 4 m skeleton heights above their World_Ground node and
 * the ground under them (GetNode by name + GetNodeTransform, SOMVR_THREAT's checked sites), a stature estimate from
 * the eye height (a human's eyes are at 0.933 of the stature), the capsule's top (a ray down onto the character); the
 * gaze ray (the head's forward); a column of level rays along the gaze's heading every 10 cm from the ground to 4 m,
 * then every 25 cm to 16 m
 * (a target's profile: an arch's opening, a ladder's rungs, a wall's top); the ground along the gaze every 10 cm
 * to 8 m and its steps (SOMVR_CLIMB's ray: needs SOMVR_CLIMB=b).
 * Lines: `CONFIG size ...`, `SIZE ask|land|hold ...`, `SIZEPROBE ...`, `SIZE END ...`. */

static struct {
	int on;              /* the head base: the size applies to first person */
	somvr_size z;
	somvr_size_cfg cfg;
	float root_cm;       /* SOMVR_SIZE_ROOT_CM */
	float eye_h;         /* the eyes above Talion's feet at 100 % */
	float eff, lift;     /* this frame's size and the base's rise (1 and 0 unless first person on the head base) */
	uint32_t word_seen, probe_seen;
	uint64_t first_frames, big_frames, probes;
	uint32_t lines;
} g_sz = { .eff = 1.0f };
static _Atomic uint32_t g_sz_word_seq, g_sz_word_pct, g_sz_probe_seq, g_sz_status_seq, g_sz_orc_seq, g_sz_orc_milli;
static _Atomic uint32_t g_sz_tal_seq, g_sz_tal_milli;
static uint32_t g_sz_status_seen, g_sz_orc_seen, g_sz_tal_seen;
static float g_sz_orc_scale;      /* SOMVR_ORC_SCALE (0 = off) */
static struct {
	int on;                       /* SOMVR_SIZE_BODY=1 */
	uintptr_t obj;                /* the object written */
	float b;                      /* the scale written last (1 = his own; the arms and the blade use it) */
	uint64_t writes, restores, foreign;
} g_szb = { .b = 1.0f };
static uintptr_t g_sz_orc_done;   /* the practice orc it was applied to */
#define SZ_CH_SCALE 0xe08 /* CCharacter (an NPC): the AI scale the game eases the object's scale toward */
#define SZ_OBJ_SCALE 0x24 /* engine object: the uniform scale after pos (+0x08) and rot (+0x14); SetObjectScale */

/* an orc's NPC (HOBJECT+0x38, the NPC vtable checked) and its two scales; 0 = not an NPC */
static uintptr_t sz_orc_npc(uintptr_t hobj, float *obj_s, float *ai_s)
{
	uintptr_t npc = hobj ? peek_ptr(hobj + PR_HOBJ_GAMEOBJ) : 0;
	*obj_s = *ai_s = NAN;
	if (hobj)
		peek(hobj + SZ_OBJ_SCALE, obj_s, sizeof *obj_s);
	if (!npc || vptr_of((void *)npc) != g_cbg.vt_npc)
		return 0;
	peek(npc + SZ_CH_SCALE, ai_s, sizeof *ai_s);
	return npc;
}

/* our practice orc's AI scale to s (game thread): only over a value that reads as a scale (0.5 .. 2) */
static void sz_orc_set(float s, const char *who)
{
	float os = NAN, as = NAN;
	uintptr_t npc = g_pr.orc_h && pr_orc_alive() ? sz_orc_npc(g_pr.orc_h, &os, &as) : 0;
	if (!npc || !(as >= 0.5f && as <= 2.0f) || !(s >= 0.7f && s <= 1.5f)) {
		logf_("SIZE orc %.2f from %s: not set (%s; object %.3f, AI %.3f)", s, who, !npc ? "no practice orc of ours alive" :
		      !(s >= 0.7f && s <= 1.5f) ? "not 0.7 .. 1.5" : "its AI scale does not read as one", os, as);
		return;
	}
	*(volatile float *)(npc + SZ_CH_SCALE) = s;
	g_sz_orc_done = g_pr.orc_h;
	logf_("SIZE orc %.2f from %s: the %s orc hobj=%#lx npc=%#lx: the AI scale %.3f -> %.2f (the object's %.3f follows at "
	      "0.1/s)", s, who, somvr_pr_orc_label(g_pr.orc_type), (unsigned long)g_pr.orc_h, (unsigned long)npc, as, s, os);
}

/* the size the rig uses this frame (1 = 1:1); the hands' and the menus' code may scale with it */
static float sz_scale(void) { return g_sz.eff; }
static float sz_lift(void) { return g_sz.lift; }

static void sz_log_state(const char *why)
{
	logf_("SIZE %s: shown %.0f %%, asked %.0f %%, this frame %.0f %% (the eyes %+.1f cm over the anchor: %.1f cm above the "
	      "feet; the eye separation and the tracked moves x %.2f); asks %llu lands %llu held frames %llu (a climb hold)",
	      why, g_sz.z.cur * 100, g_sz.z.want * 100, g_sz.eff * 100, g_sz.lift, g_sz.eye_h * g_sz.eff, g_sz.eff,
	      (unsigned long long)g_sz.z.asks, (unsigned long long)g_sz.z.lands, (unsigned long long)g_sz.z.held_frames);
}

/* a new size: before the first stereo frame (the settings file, the switch) it holds at once, later with the blink */
static const char *sz_ask(float s, const char *who)
{
	if (!g_sz.on)
		return " (no head base: the size stays 100 %)";
	if (!g_sz.z.frames && !g_sz.z.lands) {
		somvr_size_init(&g_sz.z, s);
		if (g_sz.lines++ < 200)
			logf_("SIZE start %.0f %% from %s (no frame yet: no blink)", g_sz.z.cur * 100, who);
		return " (the start value)";
	}
	if (!somvr_size_ask(&g_sz.z, s))
		return " (already)";
	if (g_sz.lines++ < 200)
		logf_("SIZE ask %.0f %% from %s (shown %.0f %%): it lands on the blink's black", g_sz.z.want * 100, who,
		      g_sz.z.cur * 100);
	return " (lands on the blink's black)";
}

/* the VR menu (vm_apply_key, game thread) */
static const char *sz_vm_set(int k, const somvr_settings *s)
{
	if (k != SOMVR_SK_PLAYER_SIZE)
		return "";
	return sz_ask(s->size, "the VR menu");
}
static void sz_vm_settings(somvr_settings *s) { s->size = g_sz.z.want > 0 ? g_sz.z.want : 1.0f; }
static uint32_t sz_vm_avail(void) { return g_sz.on ? SOMVR_VMA_SIZE : 0u; }

/* the trigger words "size PCT|probe|status" (writer thread) */
static const char *sz_word(const char *w)
{
	char a[16] = "";
	if (sscanf(w, "%15s", a) != 1)
		return "size (not PCT|probe|status: ignored)";
	if (!strcmp(a, "probe")) {
		atomic_fetch_add_explicit(&g_sz_probe_seq, 1, memory_order_release);
		return "size probe";
	}
	if (!strcmp(a, "orc") || !strcmp(a, "talion")) {
		int tal = a[0] == 't';
		double v = 0;
		char *e2 = NULL;
		const char *p = w + strlen(a);
		while (*p == ' ')
			p++;
		v = strtod_l(p, &e2, g_cloc);
		if (e2 == p || !(v >= 0.7 && v <= 1.5))
			return tal ? "size talion (not 0.7 .. 1.5: ignored)" : "size orc (not 0.7 .. 1.5: ignored)";
		atomic_store_explicit(tal ? &g_sz_tal_milli : &g_sz_orc_milli, (uint32_t)lrint(v * 1000), memory_order_relaxed);
		atomic_fetch_add_explicit(tal ? &g_sz_tal_seq : &g_sz_orc_seq, 1, memory_order_release);
		return tal ? "size talion" : "size orc";
	}
	if (!strcmp(a, "status")) {
		atomic_fetch_add_explicit(&g_sz_status_seq, 1, memory_order_release);
		return "size status";
	}
	char *end = NULL;
	double p = strtod_l(a, &end, g_cloc);
	if (!end || *end || !(p >= 50 && p <= 300))
		return "size (not PCT|probe|status: ignored)";
	atomic_store_explicit(&g_sz_word_pct, (uint32_t)lrint(p * 10), memory_order_relaxed);
	atomic_fetch_add_explicit(&g_sz_word_seq, 1, memory_order_release);
	return "size";
}

/* ---- the probe ------------------------------------------------------------------------------------------------- */
static const char *const g_sz_nodes[] = { "World_Ground", "Pelvis", "L_Legu", "L_Legl", "L_Foot", "L_Toe", "Torso",
					   "Neck", "Head", "L_Eye_Def", "R_Eye_Def", "L_Armu", "R_Armu", "L_Arml",
					   "L_Hand", "R_Hand", "L_Middle3" };
enum { SZN_GROUND, SZN_PELVIS, SZN_LEGU, SZN_LEGL, SZN_FOOT, SZN_TOE, SZN_TORSO, SZN_NECK, SZN_HEAD, SZN_LEYE, SZN_REYE,
       SZN_LARMU, SZN_RARMU, SZN_LARML, SZN_LHAND, SZN_RHAND, SZN_LMID3, SZN_N };
_Static_assert(sizeof g_sz_nodes / sizeof g_sz_nodes[0] == SZN_N, "size probe nodes");

/* the ground under p: a ray down from 40 cm above it, past `self` (NaN: none) */
static float sz_ground(const float p[3], uintptr_t self)
{
	float hit[3];
	uintptr_t ho = 0;
	float from[3] = { p[0], p[1] + 40.0f, p[2] };
	return pr_ray_ok() && pr_ray_down(from, 600.0f, self, hit, &ho) ? hit[1] : NAN;
}

/* one character's skeleton heights (obj = its engine object); label for the line */
static void sz_probe_char(uintptr_t obj, const char *label)
{
	float root[3], w[SZN_N][3];
	int ok[SZN_N] = { 0 }, n = 0;
	uintptr_t m = peek_ptr(g_som.model_pp), mi = obj ? peek_ptr(obj + OBJ_MODEL) : 0, mv = vptr_of((void *)mi);
	uintptr_t f = mv ? peek_ptr(mv + 0x20) : 0;
	int pr = f ? page_prot(f) : -1;
	if (!obj || !peek(obj + OBJ_POS, root, sizeof root) || !vec_ok(root, 1e7f)) {
		logf_("SIZEPROBE %s obj=%#lx: no position", label, (unsigned long)obj);
		return;
	}
	if (g_th.ok_node && m && pr >= 0 && (pr & PROT_EXEC))
		for (int k = 0; k < SZN_N; k++) {
			uint32_t node = 0;
			float xf[16] = { 0 };
			if ((TH_FN(THS_GETNODE, th_getnode_fn)(m, obj, g_sz_nodes[k], &node) & 0xff) ||
			    (((nodexf_fn)g_node_sites[NS_NODEXF].target)((void *)m, obj, node, xf, 1) & 0xff) || !vec_ok(xf, 1e7f) ||
			    dist3(xf, root) > 600.0f)
				continue;
			memcpy(w[k], xf, sizeof w[k]), ok[k] = 1, n++;
		}
	/* the heights above the ground under the root (a ray down); World_Ground's node only without one (it follows some
	 * animations: run 180704 had it at the root's height in the fight's poses) */
	float gy = sz_ground(root, obj), wg = isfinite(gy) ? gy : ok[SZN_GROUND] ? w[SZN_GROUND][1] : root[1] - g_sz.root_cm;
	char line[1400];
	int u = snprintf(line, sizeof line, "SIZEPROBE %s obj=%#lx root %.1f,%.1f,%.1f: the ground under it %.1f (the root "
			 "%.1f above), World_Ground %s%.1f; %d of %d nodes, cm above the ground:", label, (unsigned long)obj,
			 root[0], root[1], root[2], gy, root[1] - gy, ok[SZN_GROUND] ? "" : "not found ",
			 ok[SZN_GROUND] ? w[SZN_GROUND][1] - wg : NAN, n, SZN_N);
	for (int k = 1; k < SZN_N && u < (int)sizeof line - 40; k++)
		if (ok[k])
			u += snprintf(line + u, sizeof line - u, " %s %.1f", g_sz_nodes[k], w[k][1] - wg);
	logf_("%s", line);
	if (!ok[SZN_LEYE] || !ok[SZN_REYE])
		return;
	float eye = 0.5f * (w[SZN_LEYE][1] + w[SZN_REYE][1]) - wg, ipd = dist3(w[SZN_LEYE], w[SZN_REYE]);
	float sh = ok[SZN_LARMU] && ok[SZN_RARMU] ? dist3(w[SZN_LARMU], w[SZN_RARMU]) : NAN;
	float ua = ok[SZN_LARMU] && ok[SZN_LARML] ? dist3(w[SZN_LARMU], w[SZN_LARML]) : NAN;
	float fa = ok[SZN_LARML] && ok[SZN_LHAND] ? dist3(w[SZN_LARML], w[SZN_LHAND]) : NAN;
	float th = ok[SZN_LEGU] && ok[SZN_LEGL] ? dist3(w[SZN_LEGU], w[SZN_LEGL]) : NAN;
	float sn = ok[SZN_LEGL] && ok[SZN_FOOT] ? dist3(w[SZN_LEGL], w[SZN_FOOT]) : NAN;
	/* the capsule: a ray down from 3 m above the root onto the character (its first hit, if it is the character) */
	float cap = NAN;
	uintptr_t ho = 0;
	float hit[3], from[3] = { root[0], root[1] + 300.0f, root[2] };
	int capok = pr_ray_ok() && pr_ray_down(from, 600.0f, 0, hit, &ho);
	if (capok && ho == obj)
		cap = hit[1] - wg;
	float hd = ok[SZN_HEAD] ? w[SZN_HEAD][1] - wg : NAN, pel = ok[SZN_PELVIS] ? w[SZN_PELVIS][1] - wg : NAN;
	logf_("SIZEPROBE %s: the eyes %.1f cm above the ground (a human of %.0f cm: eyes at 0.933 of the stature), the Head "
	      "node %.1f, the pelvis %.1f, the eyes %.1f cm apart, shoulders (Armu) %.1f apart, upper arm %.1f, forearm %.1f, "
	      "thigh %.1f, shin %.1f; a ray down from 3 m above: %s %.1f cm above the ground", label, eye, eye / 0.933f, hd, pel,
	      ipd, sh, ua, fa, th, sn, !capok ? "no hit" : ho == obj ? "its capsule's top at" : "hit something else at",
	      capok ? hit[1] - wg : NAN);
	(void)cap;
}

/* a level ray from `from` along heading a (rad, game: + = right) for len cm: the distance (NaN = none), the name */
static float sz_level(const float from[3], float a, float len, char name[64])
{
	somvr_climb_hit hit;
	memset(&hit, 0, sizeof hit);
	sv_vec3 f = { from[0], from[1], from[2] }, d = { sinf(a), 0, cosf(a) };
	int ok = cl_ray(NULL, -1, f, d, len, &hit);
	if (name)
		snprintf(name, 64, "%s", ok && hit.hit ? g_cl.ri[(g_cl.nri - 1) & 15].name : "");
	return ok && hit.hit ? hit.dist : NAN;
}

static void sz_probe(void)
{
	uint32_t q = atomic_load_explicit(&g_sz_probe_seq, memory_order_acquire);
	if (q == g_sz.probe_seen)
		return;
	g_sz.probe_seen = q, g_sz.probes++;
	sz_log_state("probe");
	uintptr_t pm = 0, obj = 0;
	if (!g_fr.pl || !player_objs(g_fr.pl, &pm, &obj)) {
		logf_("SIZEPROBE no player object");
		return;
	}
	if (!g_th.ok_node)
		logf_("SIZEPROBE no GetNode (needs SOMVR_THREAT=1's checked TH_GETNODE): the ground and the rays only");
	float tsc = NAN;
	peek(obj + SZ_OBJ_SCALE, &tsc, sizeof tsc);
	char tl[48];
	snprintf(tl, sizeof tl, "talion (scale %.3f)", tsc);
	sz_probe_char(obj, tl);
	/* the orcs within 4 m (PlayerCombat's bands, as SOMVR_COMBAT finds them) */
	float root[3];
	uintptr_t pcomb = peek_ptr(g_fr.pl + P_COMBAT), pc = pcomb ? peek_ptr(pcomb + PC_SERVER) : 0;
	if (pc && (vptr_of((void *)pc) != g_cbg.vt_combat || !peek_ptr(pc + C_PLAYER)))
		pc = 0;
	struct cb_orc o[CB_ORCS];
	int n = pcomb && peek(obj + OBJ_POS, root, sizeof root) ? cb_orcs(pcomb, pc, root, o, CB_ORCS) : 0, ours = 0;
	for (int i = 0; i < n; i++)
		ours |= o[i].hobj == g_pr.orc_h;
	if (!ours && g_pr.orc_h && pr_orc_alive() && n < CB_ORCS) { /* our practice orc beyond the 4 m: probed too */
		float op[3] = { 0, 0, 0 };
		peek(g_pr.orc_h + OBJ_POS, op, sizeof op);
		o[n++] = (struct cb_orc){ 0, g_pr.orc_h, { op[0], op[1], op[2] }, dist3(op, root) };
	}
	for (int i = 0; i < n && i < 6; i++) {
		char lab[64];
		float os, as;
		uintptr_t npc = sz_orc_npc(o[i].hobj, &os, &as);
		snprintf(lab, sizeof lab, "orc %d (%s, %.0f cm away, scale %.3f, AI %.3f%s)", i,
			 o[i].hobj == g_pr.orc_h && g_pr.orc_h ? somvr_pr_orc_label(g_pr.orc_type) : "not ours", o[i].dist, os, as,
			 npc ? "" : ", not an NPC");
		sz_probe_char(o[i].hobj, lab);
	}
	if (!n)
		logf_("SIZEPROBE no orc within 4 m (and none of ours)");
	if (!g_cl.on) {
		logf_("SIZEPROBE no rays (SOMVR_CLIMB=b's cast is off)");
		return;
	}
	/* the gaze: the head's forward; then the column along its heading and the ground under it */
	sv_vec3 hp;
	sv_quat hq;
	somvr_rig_head(&g_rig, g_fr.base, g_fr.views.pose, &hp, &hq);
	sv_vec3 fw = sv_qrot(hq, (sv_vec3){ 0, 0, 1 });
	somvr_climb_hit gh;
	memset(&gh, 0, sizeof gh);
	int gok = cl_ray(NULL, -1, hp, fw, 6000.0f, &gh) && gh.hit;
	float a = sv_heading(hq), gy = sz_ground((const float[3]){ hp.x, hp.y - 150.0f, hp.z }, obj);
	logf_("SIZEPROBE gaze from the eyes %.1f,%.1f,%.1f (%.1f cm above the ground under them) heading %.1f deg pitch "
	      "%.1f deg: %s%.1f cm, at %.1f,%.1f,%.1f ('%s')", hp.x, hp.y, hp.z, hp.y - gy, a * 57.29578f,
	      asinf(fw.y > 1 ? 1 : fw.y < -1 ? -1 : fw.y) * 57.29578f, gok ? "a hit at " : "no hit within 60 m", gok ? gh.dist : NAN,
	      gh.point.x, gh.point.y, gh.point.z, gok ? g_cl.ri[(g_cl.nri - 1) & 15].name : "");
	if (!isfinite(gy))
		gy = hp.y - g_sz.eye_h * g_sz.eff;
	char line[4000], nm[64], last[64] = "";
	int u = snprintf(line, sizeof line, "SIZEPROBE column along heading %.1f deg from %.1f,%.1f (ground %.1f): height "
			 "above the ground = the distance (cm; '-' none within 40 m; a name where it changes):", a * 57.29578f,
			 hp.x, hp.z, gy);
	for (int h = 0; h <= 1600 && u < (int)sizeof line - 48; h += h < 400 ? 10 : 25) {
		float from[3] = { hp.x, gy + (float)h, hp.z }, d = sz_level(from, a, 4000.0f, nm);
		if (!isfinite(d))
			u += snprintf(line + u, sizeof line - u, " %d=-", h), last[0] = 0;
		else
			u += snprintf(line + u, sizeof line - u, " %d=%.0f%s%s%s", h, d, strcmp(nm, last) ? "'" : "",
				      strcmp(nm, last) ? nm : "", strcmp(nm, last) ? "'" : ""),
			snprintf(last, sizeof last, "%s", nm);
	}
	logf_("%s", line);
	float hs[81];
	u = snprintf(line, sizeof line, "SIZEPROBE ground along heading %.1f deg every 10 cm to 8 m (cm above the ground under "
		     "the eyes):", a * 57.29578f);
	for (int i = 0; i <= 80; i++) {
		float p[3] = { hp.x + sinf(a) * 10.0f * (float)i, gy + 300.0f, hp.z + cosf(a) * 10.0f * (float)i };
		float hit[3];
		uintptr_t ho = 0;
		hs[i] = pr_ray_ok() && pr_ray_down(p, 900.0f, obj, hit, &ho) ? hit[1] - gy : NAN;
		if (u < (int)sizeof line - 16)
			u += snprintf(line + u, sizeof line - u, " %.0f", hs[i]);
	}
	logf_("%s", line);
	float st[16];
	int si[16], ns = somvr_size_steps(hs, 81, 5.0f, st, si, 16);
	u = snprintf(line, sizeof line, "SIZEPROBE steps (rises of 5 cm or more between neighbours 10 cm apart): %d", ns);
	for (int i = 0; i < ns && i < 16 && u < (int)sizeof line - 40; i++)
		u += snprintf(line + u, sizeof line - u, "%s %+.1f cm at %d cm", i ? "," : ":", st[i], si[i] * 10 + 5);
	logf_("%s", line);
}

/* SOMVR_SIZE_BODY: Talion's object scale to want (1 = his own) on the game thread; only over a value that reads as his
 * own or ours (a scale the game set itself, a pin's or a cinematic's, is left alone and counted) */
static void sz_body_set(uintptr_t pl, float want)
{
	uintptr_t pm = 0, obj = 0;
	if (!g_szb.on || !pl || !player_objs(pl, &pm, &obj))
		return;
	if (obj != g_szb.obj)
		g_szb.obj = obj, g_szb.b = 1.0f;
	float cur = NAN;
	peek(obj + SZ_OBJ_SCALE, &cur, sizeof cur);
	if (fabsf(cur - want) < 1e-5f) {
		g_szb.b = want;
		return;
	}
	if (!(fabsf(cur - 1.0f) < 1e-4f || fabsf(cur - g_szb.b) < 1e-4f)) {
		if (g_szb.foreign++ < 5)
			logf_("SIZE body: his object's scale reads %.3f (not his 1 nor our %.3f): left alone", cur, g_szb.b);
		g_szb.b = 1.0f;
		return;
	}
	*(volatile float *)(obj + SZ_OBJ_SCALE) = want;
	if (fabsf(want - 1.0f) < 1e-5f)
		g_szb.restores++;
	else
		g_szb.writes++;
	if (g_sz.lines++ < 200)
		logf_("SIZE body %.2f: Talion's object scale %.3f -> %.3f (%s)", want, cur, want,
		      want > 1.0001f ? "the hands and the sword at the player's size" : "his own again");
	g_szb.b = want;
}
/* the body scale the next phase 2 and 7 see (the arms' hands / s, the blade x s); 1 = none */
static float sz_body_scale(void) { return g_szb.on ? g_szb.b : 1.0f; }
/* body_frame: the Head node read about the root without the body scale (his capsule and his physics did not grow) */
static void sz_unscale_head(uintptr_t obj, float head[3])
{
	float b = sz_body_scale(), root[3];
	if (b == 1.0f || !obj || obj != g_szb.obj || !peek(obj + OBJ_POS, root, sizeof root))
		return;
	for (int i = 0; i < 3; i++)
		head[i] = root[i] + (head[i] - root[i]) / b;
}

/* H4, every frame (stereo: sz_frame; flat: sz_flat): the orc and Talion scale words, SOMVR_ORC_SCALE (game thread) */
static void sz_words(uintptr_t pl)
{
	uint32_t oq = atomic_load_explicit(&g_sz_orc_seq, memory_order_acquire);
	if (oq != g_sz_orc_seen)
		g_sz_orc_seen = oq, sz_orc_set((float)atomic_load(&g_sz_orc_milli) / 1000.0f, "the trigger word");
	if (g_sz_orc_scale > 0 && g_pr.orc_h && g_pr.orc_h != g_sz_orc_done && pr_orc_alive())
		g_sz_orc_done = g_pr.orc_h, sz_orc_set(g_sz_orc_scale, "SOMVR_ORC_SCALE");
	uint32_t tq = atomic_load_explicit(&g_sz_tal_seq, memory_order_acquire);
	uintptr_t tpm = 0, tobj = 0;
	if (tq != g_sz_tal_seen && (g_sz_tal_seen = tq, pl && player_objs(pl, &tpm, &tobj))) {
		float was = NAN, v = (float)atomic_load(&g_sz_tal_milli) / 1000.0f;
		peek(tobj + SZ_OBJ_SCALE, &was, sizeof was);
		if (was >= 0.5f && was <= 2.0f)
			*(volatile float *)(tobj + SZ_OBJ_SCALE) = v;
		logf_("SIZE talion %.2f (a test): his object's scale obj+0x24 %.3f%s", v, was,
		      was >= 0.5f && was <= 2.0f ? " -> written (raw: no SetObjectScale, no ray-hit dims)" : ": not a scale, not written");
	}
}

/* H4, a flat frame (third person's flat window, menus): the words only */
static void sz_flat(uintptr_t pl)
{
	if (!g_sz.on)
		return;
	sz_words(pl);
	if (g_szb.b != 1.0f)
		sz_body_set(pl, 1.0f); /* SOMVR_SIZE_BODY: the game's own view shows his whole body */
}

/* H4, a stereo frame, right before the eyes (after hp_frame: head_ok is known): the trigger word's ask, the blink
 * and the landing (the record's fade; a landing is a jump of the rig, not motion), this frame's size, the probe */
static void sz_frame(void)
{
	if (!g_sz.on) {
		g_sz.eff = 1.0f, g_sz.lift = 0;
		return;
	}
	uint32_t q = atomic_load_explicit(&g_sz_word_seq, memory_order_acquire);
	if (q != g_sz.word_seen) {
		g_sz.word_seen = q;
		sz_ask((float)atomic_load_explicit(&g_sz_word_pct, memory_order_relaxed) / 1000.0f, "the trigger word");
	}
	int hold = cl_holds(0) || cl_holds(1);
	uint64_t held = g_sz.z.held_frames;
	float a = somvr_size_frame(&g_sz.z, &g_sz.cfg, hold, cf_time());
	if (g_sz.z.held_frames > held && held == 0 && g_sz.lines++ < 200)
		logf_("SIZE hold %.0f %%: a climb hold is on (it lands after the release)", g_sz.z.want * 100);
	if (a > 0 && g_fr.rec) { /* the record's fade (the layer's black over the frame), the larger one */
		uint32_t lv = (uint32_t)(a * 255 + 0.5f), was = somvr_rec_fade(g_fr.rec->flags);
		if (lv > was)
			g_fr.rec->flags = (g_fr.rec->flags & ~SOMVR_REC_FADE_MASK) | lv << SOMVR_REC_FADE_SHIFT;
	}
	int first = g_rigcfg.base == SOMVR_BASE_HEAD && g_fr.head_ok;
	g_sz.eff = first ? g_sz.z.cur : 1.0f;
	g_sz.lift = somvr_size_lift(g_sz.eff, g_sz.eye_h);
	/* SOMVR_SIZE_BODY: while only his hands are drawn (bd_frame's live frames), his model at the size */
	if (g_szb.on)
		sz_body_set(g_fr.pl, first && g_bd.on && !bm_head_away() && !fb_third() ? g_sz.eff : 1.0f);
	g_sz.first_frames += (uint64_t)first;
	g_sz.big_frames += (uint64_t)(g_sz.eff > 1.0001f);
	if (g_sz.z.landed) {
		g_fr.relatched = 1; /* a jump of the rig: no sweep, no follow measure, not motion (rig_jump_frame) */
		if (g_sz.lines++ < 200) {
			char w[48];
			snprintf(w, sizeof w, "land (from %.0f %%, black %.2f)", g_sz.z.from * 100, a);
			sz_log_state(w);
		}
	}
	sz_words(g_fr.pl);
	uint32_t ss = atomic_load_explicit(&g_sz_status_seq, memory_order_acquire);
	if (ss != g_sz_status_seen)
		g_sz_status_seen = ss, sz_log_state("status");
}

/* H4, a stereo frame, after the eyes and the frame's game-side work (the rig of this frame exists): the probe */
static void sz_after(void)
{
	if (g_sz.on)
		sz_probe();
}

/* eye0_setup, after the rig's frame and the body moments' base: this frame's size into the rig and the base */
static void sz_rig(void)
{
	g_rig.scale = g_sz.eff;
	g_fr.base.pos.y += g_sz.lift;
}

/* somvr_start, after lo_config (the anchor's eye height) */
static void sz_config(int *bad)
{
	memset(&g_sz, 0, sizeof g_sz);
	g_sz.eff = 1.0f;
	somvr_size_cfg_default(&g_sz.cfg);
	float pct = env_float("SOMVR_PLAYER_SIZE", 100, SOMVR_SIZE_MIN_PCT, SOMVR_SIZE_MAX_PCT, bad);
	g_sz.root_cm = env_float("SOMVR_SIZE_ROOT_CM", SOMVR_SIZE_ROOT_CM, 0, 300, bad);
	float ms = env_float("SOMVR_SIZE_BLINK_MS", 350, 0, 2000, bad);
	uint32_t ims = (uint32_t)lrintf(ms);
	somvr_blink_times(ims, &g_sz.cfg.out_s, &g_sz.cfg.in_s);
	g_sz.on = g_rigcfg.base == SOMVR_BASE_HEAD;
	g_sz.eye_h = (g_lo.anchor ? g_lo.acfg.eye_cm : CB_HEAD_UP_CM) + g_sz.root_cm + g_rigcfg.eye_cm;
	somvr_size_init(&g_sz.z, pct / 100.0f);
	g_sz_orc_scale = env_float("SOMVR_ORC_SCALE", 0, 0, 1.5f, bad);
	static const char *const onoff_w[] = { "0", "1" };
	g_szb = (__typeof__(g_szb)){ .b = 1.0f };
	g_szb.on = g_sz.on && env_word("SOMVR_SIZE_BODY", onoff_w, 2, 0);
	if (g_sz_orc_scale > 0 && g_sz_orc_scale < 0.7f)
		g_sz_orc_scale = 0.7f;
	logf_("CONFIG size %s: %.0f %% (SOMVR_PLAYER_SIZE%s; the VR menu's Player size 100..140 %%), the eyes %.1f cm above "
	      "the feet at 100 %% (%s %.1f + SOMVR_SIZE_ROOT_CM %.1f + SOMVR_EYE_CM %.1f): %.1f cm at this size; a change's "
	      "blink %u ms (out %.0f, in %.0f); SOMVR_SIZE_BODY %d (Talion's model at the size while only his hands are "
	      "drawn); SOMVR_ORC_SCALE %.2f (experimental, 0 = off)",
	      g_sz.on ? "on" : "off (no SOMVR_BASE=head: 100 % always)", g_sz.z.cur * 100,
	      getenv("SOMVR_PLAYER_SIZE") ? "" : " unset", g_sz.eye_h, g_lo.anchor ? "the root anchor's" : "the Head node's",
	      g_lo.anchor ? g_lo.acfg.eye_cm : CB_HEAD_UP_CM, g_sz.root_cm, g_rigcfg.eye_cm, g_sz.eye_h * g_sz.z.cur, ims,
	      g_sz.cfg.out_s * 1e3, g_sz.cfg.in_s * 1e3, g_szb.on, g_sz_orc_scale);
}

static void sz_end_log(void)
{
	if (g_szb.on && g_szb.obj && g_szb.b != 1.0f) { /* his own scale back (the end: any thread; a float) */
		*(volatile float *)(g_szb.obj + SZ_OBJ_SCALE) = 1.0f;
		g_szb.b = 1.0f, g_szb.restores++;
	}
	logf_("SIZE END shown %.0f %% asked %.0f %%: asks %llu lands %llu held frames %llu; first-person frames %llu, of them "
	      "above 100 %% %llu; probes %llu; SOMVR_SIZE_BODY %d: writes %llu, back to 1 %llu, a scale not ours left alone %llu",
	      g_sz.z.cur * 100, g_sz.z.want * 100, (unsigned long long)g_sz.z.asks,
	      (unsigned long long)g_sz.z.lands, (unsigned long long)g_sz.z.held_frames, (unsigned long long)g_sz.first_frames,
	      (unsigned long long)g_sz.big_frames, (unsigned long long)g_sz.probes, g_szb.on, (unsigned long long)g_szb.writes,
	      (unsigned long long)g_szb.restores, (unsigned long long)g_szb.foreign);
}
