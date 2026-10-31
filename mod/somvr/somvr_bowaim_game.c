/* somvr_bowaim_game.c - build 20 (docs/re-xr-pipeline.md 76): where the bow really aims. Included by somvr.c after
 * somvr_quiver_game.c (it uses g_bow, cb_orcs_r, bo_node_world, cl_cast1, somvr_th_to_xr, logf_).
 *
 * SOMVR_BOW_AIM=assist|exact (default assist = builds 11-19, SOMVR_BOW_ASSIST_DEG's cone). exact: our arrow flies along
 * the bow's own line and hits an orc only where that line passes through it - its head (a sphere of
 * SOMVR_BOW_EXACT_HEAD_CM round its Head node, else the root + 80 cm: the headshot) or its body (a capsule of
 * SOMVR_BOW_EXACT_BODY_CM round the orc's upright axis, SOMVR_BOW_EXACT_SPAN_CM below and above its root: the knees to
 * the neck) - and nearer than the first wall or ground on the line (the climb's G21 cast; the orcs' own capsules and
 * Talion passed through). The game is given that point and that orc (as the assist gave them: P+0x214 / Combat+0x150,
 * the shooter's target, the Head node for a head), else nothing - straight along the line, our arrow put back on it if
 * the game turns it. The build 19b session: the shots felt steered by an assist (it was meant for the menus' pointer).
 *
 * SOMVR_BOW_AIM_RING=off|on (default off): while the bow is drawn, a ring where that line meets an orc or the world
 * (nothing within SOMVR_BOW_AIM_FAR_M: there, along it) - gold with a dot (the climb's landing ring's picture) on the
 * world, wraith-blue (the Shadow Strike's ring) on an orc it would hit - facing the eyes, SOMVR_BOW_AIM_RING_DEG of the
 * view across at any distance; somvr_objective's entry (SOMVR_OBF_AIM), so both eyes see it at its depth (the build 19b
 * session asked for the flat game's aiming circle back, where the bow really aims).
 *
 * Build 21 (re-xr-pipeline.md 82): SOMVR_BOW_AIM_LOOK=sight|ring20 (default sight): the ring's picture. sight = a thin
 * soft-white ring with a dot at its centre (about 80 % opaque, a faint dark edge: it reads against a bright sky) on the
 * world, the same in wraith-blue on an orc it would hit; SOMVR_BOW_AIM_RING_DEG's default 1.5 (half build 20's): the
 * build 20c session found build 20's ring too big and its gold out of place. ring20 = build 20's gold
 * ring / the strike's blue ring, 3 deg by default. Where it sits (the trace above) is the same in both.
 *
 * Lines: CONFIG bow aim ...; BOW t=.. aim: the line meets <what> (once per change, the first 300);
 * BOW t=.. assist: ... (bo_shoot's, as before, "exact" in it); BOW END aim: ... */

enum { BA_FAR = 0, BA_WORLD = 1, BA_BODY = 2, BA_HEAD = 3, BA_N };
static const char *const g_ba_kind_w[BA_N] = { "nothing (the ring far along the line)", "the world", "an orc's body",
					       "an orc's head" };
static struct {
	int exact, ring, look; /* look: build 21's SOMVR_BOW_AIM_LOOK (0 sight, 1 ring20) */
	float head_cm, body_cm, span_lo_cm, span_hi_cm, far_cm, ring_deg;
	/* the last trace (bo_frame, while drawn) */
	int have, kind, kind_was;
	uint64_t frame;
	float pt[3], dist_cm;
	uintptr_t hobj;
	uint64_t traces, casts, steps, nocast, kinds[BA_N], entries, lines;
	/* the shot (bo_shoot -> bo_pj's steer): the angle the arrow may leave the line by (deg) */
	float allow_deg;
	uint64_t shots[BA_N];
} g_ba;

/* the line o + t*d (t >= 0, d unit) against a sphere (c, r): the entry's t, or -1 */
static float ba_sphere(const float o[3], const float d[3], const float c[3], float r)
{
	float v[3] = { c[0] - o[0], c[1] - o[1], c[2] - o[2] };
	float tc = v[0] * d[0] + v[1] * d[1] + v[2] * d[2];
	if (tc < 0)
		return -1;
	float d2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2] - tc * tc;
	if (d2 > r * r)
		return -1;
	float t = tc - sqrtf(r * r - d2);
	return t > 0 ? t : 0;
}

/* the line o + t*d (t >= 0, d unit) against the upright segment a .. a + (0, len, 0) widened by r: the closest
 * approach's t (inside the capsule: the point the game is given) and the entry's t (the ring), or -1 */
static float ba_capsule(const float o[3], const float d[3], const float a[3], float len, float r, float *t_in)
{
	float w[3] = { o[0] - a[0], o[1] - a[1], o[2] - a[2] };
	float b = d[1], dd = d[0] * w[0] + d[1] * w[1] + d[2] * w[2], e = w[1], den = 1 - b * b;
	float t, s;
	if (den > 1e-6f) {
		t = (b * e - dd) / den, s = (e - b * dd) / den;
	} else { /* the line upright: along the axis */
		t = 0, s = e;
	}
	s = s < 0 ? 0 : s > len ? len : s;
	t = s * b - dd; /* the line's point nearest the segment's point s */
	if (t < 0)
		t = 0, s = e < 0 ? 0 : e > len ? len : e;
	float q[3] = { o[0] + t * d[0] - a[0], o[1] + t * d[1] - a[1] - s, o[2] + t * d[2] - a[2] };
	float m2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2];
	if (m2 > r * r)
		return -1;
	float h = sqrtf((r * r - m2) / (den > 0.04f ? den : 0.04f)); /* the line's half chord through the capsule (its side) */
	*t_in = t - h > 0 ? t - h : 0;
	return t;
}

/* the first wall or ground on the line within len cm (the climb's G21 cast): its distance, or -1 (none, or no cast).
 * Talion and the orcs' capsules (within body_cm + 30 of an orc's axis, over its span) are passed through */
static float ba_world(const float o[3], const float d[3], float len, const struct cb_orc *orcs, int n)
{
	uintptr_t phys = g_cl.on ? peek_ptr(g_clg.phys_pp) : 0;
	if (!phys || vptr_of((void *)phys) != g_cl.vt_phys ||
	    peek_ptr(g_cl.vt_phys + 8u * CL_SLOT_RAYCAST) != g_cl_sites[CLS_RAYCAST].target) {
		g_ba.nocast++;
		return -1;
	}
	float s = 0, to[3] = { o[0] + d[0] * len, o[1] + d[1] * len, o[2] + d[2] * len };
	for (int k = 0; k < 6 && s < len; k++) {
		float f[3] = { o[0] + d[0] * s, o[1] + d[1] * s, o[2] + d[2] * s }, hp[3];
		unsigned char r[0x80] __attribute__((aligned(16)));
		g_ba.casts++;
		if (!cl_cast1(phys, f, to, r))
			return -1;
		uintptr_t obj;
		memcpy(hp, r, sizeof hp), memcpy(&obj, r + 0x20, sizeof obj);
		if (!vec_ok(hp, 1e7f))
			return -1;
		float t = s + dist3(hp, f);
		int pass = obj && obj == g_cl.obj; /* Talion */
		pass |= mt_near_mount(obj, hp); /* build 27c: the beast he rides (234922: the ring on its own back, 0.0 m) */
		for (int i = 0; !pass && i < n; i++) {
			float dx = hp[0] - orcs[i].pos[0], dz = hp[2] - orcs[i].pos[2], dy = hp[1] - orcs[i].pos[1];
			pass = (obj && obj == orcs[i].hobj) || (sqrtf(dx * dx + dz * dz) <= g_ba.body_cm + 30.0f &&
								dy >= -g_ba.span_lo_cm - 30.0f && dy <= g_ba.span_hi_cm + 60.0f);
		}
		if (!pass)
			return t;
		g_ba.steps++;
		s = t + 10.0f;
	}
	return -1;
}

/* what the line o + t*d meets first: an orc's head or body before the world (BA_HEAD / BA_BODY: pt = the point the
 * game is given - the Head node, the body's nearest point on the line - ring = the entry where the ring goes, *hobj,
 * *head_node), the world (BA_WORLD), else nothing within far_cm (BA_FAR, both at far_cm). *t_cm the ring's distance */
static int ba_trace(sv_vec3 ov, sv_vec3 dv, float pt[3], float ring[3], float *t_cm, uintptr_t *hobj, int *head_node)
{
	float o[3] = { ov.x, ov.y, ov.z }, d[3] = { dv.x, dv.y, dv.z };
	uintptr_t pl = g_fr.pl, pc = pl ? peek_ptr(pl + P_COMBAT) : 0, sc = pc ? peek_ptr(pc + PC_SERVER) : 0;
	struct cb_orc near[8];
	int n = pc ? bo_orcs_r(pc, sc, o, g_ba.far_cm, near, 8) : 0, kind = BA_FAR; /* (never the beast ridden) */
	float best = g_ba.far_cm, best_pt = g_ba.far_cm;
	*hobj = 0, *head_node = 0;
	for (int i = 0; i < n; i++) {
		float hc[3] = { near[i].pos[0], near[i].pos[1] + BO_HEAD_CM, near[i].pos[2] };
		int node = bo_node_world(near[i].hobj, g_som.head, hc);
		float th = ba_sphere(o, d, hc, g_ba.head_cm);
		if (th >= 0) { /* through its head: the head (its body's shoulders round it do not take it) */
			if (th < best)
				best = th, kind = BA_HEAD, *hobj = near[i].hobj, *head_node = node, memcpy(pt, hc, sizeof hc), best_pt = th;
			continue;
		}
		float a[3] = { near[i].pos[0], near[i].pos[1] - g_ba.span_lo_cm, near[i].pos[2] }, tin = 0;
		float tb = ba_capsule(o, d, a, g_ba.span_lo_cm + g_ba.span_hi_cm, g_ba.body_cm, &tin);
		if (tb >= 0 && tin < best) {
			best = tin, kind = BA_BODY, *hobj = near[i].hobj, *head_node = 0, best_pt = tb;
			for (int k = 0; k < 3; k++)
				pt[k] = o[k] + tb * d[k];
		}
	}
	float tw = ba_world(o, d, kind == BA_FAR ? g_ba.far_cm : best + 1.0f, near, n);
	if (tw >= 0 && tw < best)
		kind = BA_WORLD, best = tw, best_pt = tw, *hobj = 0, *head_node = 0;
	if (kind == BA_FAR || kind == BA_WORLD)
		for (int k = 0; k < 3; k++)
			pt[k] = o[k] + best_pt * d[k];
	for (int k = 0; k < 3; k++)
		ring[k] = o[k] + best * d[k];
	*t_cm = best;
	return kind;
}

/* bo_shoot, SOMVR_BOW_AIM=exact: as bo_assist_point - 1 = an orc on the line (out = the point, the head by its node),
 * else 0 (straight along the line). The allowed angle for bo_pj's steer: an arrow homing onto that orc may leave the
 * line by the orc's own width there, else by 2 deg */
static int ba_exact_point(sv_vec3 o, sv_vec3 d, float out[3], const char **what, float *deg, float *dist_m, uintptr_t *hobj,
			  int *head_node)
{
	float ring[3], t = 0;
	int kind = ba_trace(o, d, out, ring, &t, hobj, head_node);
	g_ba.shots[kind]++;
	float v[3] = { out[0] - o.x, out[1] - o.y, out[2] - o.z }, lv = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	float c = lv > 1 ? (v[0] * d.x + v[1] * d.y + v[2] * d.z) / lv : 1;
	*deg = acosf(c > 1 ? 1 : c < -1 ? -1 : c) * 57.29578f, *dist_m = lv / 100.0f;
	*what = kind == BA_HEAD ? (*head_node ? "head (its Head node; the line through it)" : "head (root + 80 cm; the line "
		"through it)") : kind == BA_BODY ? "body (the line through it)" : kind == BA_WORLD ? "nothing: the world first" :
		"nothing";
	g_ba.allow_deg = kind >= BA_BODY && lv > 1 ? atan2f(g_ba.body_cm + g_ba.head_cm + 30.0f, lv) * 57.29578f + 2.0f : 2.0f;
	if (kind < BA_BODY)
		memset(out, 0, 3 * sizeof(float)), *hobj = 0, *head_node = 0;
	return kind >= BA_BODY;
}

/* the test words (writer thread; headless A/B in one boot): "bow auto on|off" = SOMVR_BOW_AUTO_ARROW, "bow aim
 * assist|exact" = SOMVR_BOW_AIM - taken by ba_frame (the game thread) for the next step */
static _Atomic int g_ba_req_auto = -1, g_ba_req_aim = -1;
static const char *ba_word(const char *a, const char *v, int nw)
{
	if (nw == 2 && !strcmp(a, "auto")) {
		int m = !strcmp(v, "on") ? 1 : !strcmp(v, "off") ? 0 : -1;
		if (m < 0)
			return "bow auto (not on|off: ignored)";
		atomic_store(&g_ba_req_auto, m);
		return "bow auto (SOMVR_BOW_AUTO_ARROW from the next step)";
	}
	if (nw == 2 && !strcmp(a, "aim")) {
		int m = !strcmp(v, "exact") ? 1 : !strcmp(v, "assist") ? 0 : -1;
		if (m < 0)
			return "bow aim (not assist|exact: ignored)";
		atomic_store(&g_ba_req_aim, m);
		return "bow aim (SOMVR_BOW_AIM from the next shot)";
	}
	return NULL;
}

/* bo_frame, after the step: while the bow is drawn and the ring is on, this frame's trace (for ob_frame's entry) */
static void ba_frame(const somvr_bow_out *o, double t)
{
	int ra = atomic_exchange(&g_ba_req_auto, -1), rm = atomic_exchange(&g_ba_req_aim, -1);
	if (ra >= 0)
		g_bow.cfg.auto_arrow = ra, logf_("BOW t=%.3f auto_arrow %s (the test word)", t, ra ? "on" : "off");
	if (rm >= 0)
		g_ba.exact = rm, logf_("BOW t=%.3f aim %s (the test word; SOMVR_BOW_ASSIST_DEG %.1f)", t, rm ? "exact" : "assist",
				       g_bow.assist_deg);
	if (!g_ba.ring || o->phase != SOMVR_BOW_PH_DRAW || !o->ray) {
		g_ba.have = 0;
		return;
	}
	float pt[3], ring[3], tc = 0;
	uintptr_t hobj = 0;
	int node = 0, kind = ba_trace(o->origin, o->dir, pt, ring, &tc, &hobj, &node);
	g_ba.traces++, g_ba.kinds[kind]++;
	memcpy(g_ba.pt, ring, sizeof ring);
	g_ba.kind = kind, g_ba.dist_cm = tc, g_ba.hobj = hobj, g_ba.frame = g_fr.frame, g_ba.have = 1;
	if (kind != g_ba.kind_was && g_ba.lines++ < 300 && bo_log_ok(t))
		logf_("BOW t=%.3f aim: the line meets %s %.1f m from the nock at %.0f,%.0f,%.0f%s (the ring %s)", t,
		      g_ba_kind_w[kind], tc / 100.0f, ring[0], ring[1], ring[2], hobj ? " (an orc on it)" : "",
		      kind >= BA_BODY ? "wraith-blue: a hit" : g_ba.look ? "gold" : "white");
	g_ba.kind_was = kind;
}

/* ob_frame (H4, after bo_frame's stereo frame): the ring into this frame's objective entry; 1 = published */
static int ba_entry(somvr_objective_frame *e)
{
	if (!g_ba.ring || !g_ba.have || g_bow.s.phase != SOMVR_BOW_PH_DRAW || g_ba.frame + 1 < g_fr.frame || game_paused())
		return 0;
	somvr_th_map mp = { { g_fr.base.pos.x, g_fr.base.pos.y, g_fr.base.pos.z }, g_fr.base.yaw - g_rig.yaw0,
			    { g_rig.p0.x, g_rig.p0.y, g_rig.p0.z }, somvr_rig_scale(&g_rig) };
	somvr_th_to_xr(&mp, g_ba.pt, e->pos);
	if (!isfinite(e->pos[0] + e->pos[1] + e->pos[2]))
		return 0;
	e->n = 1, e->id = (uint32_t)g_ba.kind;
	e->flags = SOMVR_OBF_AIM | (g_ba.kind >= BA_BODY ? SOMVR_OBF_AIM_HIT : 0u) | (g_ba.look ? 0u : SOMVR_OBF_AIM_SIGHT);
	e->dist_m = g_ba.ring_deg; /* SOMVR_OBF_AIM: the ring's size in degrees of the view (the layer has the eyes) */
	g_ba.entries++;
	return 1;
}

static int ba_wanted(void) { return g_bow.on && g_ba.ring; }
static int ba_exact(void) { return g_ba.exact; }
static float ba_allow_deg(void) { return g_ba.allow_deg > 0 ? g_ba.allow_deg : 2.0f; }

static void ba_config(int *bad)
{
	memset(&g_ba, 0, sizeof g_ba);
	g_ba.exact = env_word("SOMVR_BOW_AIM", (const char *const[]){ "assist", "exact" }, 2, 0);
	g_ba.ring = env_word("SOMVR_BOW_AIM_RING", (const char *const[]){ "off", "on" }, 2, 0);
	g_ba.look = env_word("SOMVR_BOW_AIM_LOOK", (const char *const[]){ "sight", "ring20" }, 2, 0); /* build 21 */
	g_ba.head_cm = env_float("SOMVR_BOW_EXACT_HEAD_CM", 16, 5, 40, bad);
	g_ba.body_cm = env_float("SOMVR_BOW_EXACT_BODY_CM", 24, 5, 60, bad);
	float span[2] = { 65, 50 }; /* below and above the root (the capsule's middle: COMBAT's -90..+90) */
	env_vec("SOMVR_BOW_EXACT_SPAN_CM", span, 2, 0, 120, bad);
	g_ba.span_lo_cm = span[0], g_ba.span_hi_cm = span[1];
	g_ba.far_cm = 100.0f * env_float("SOMVR_BOW_AIM_FAR_M", 40, 5, 100, bad);
	g_ba.ring_deg = env_float("SOMVR_BOW_AIM_RING_DEG", g_ba.look ? 3.0f : 1.5f, 0.5f, 10, bad); /* build 21: 1.5 */
	g_ba.kind_was = -1;
}

static void ba_config_log(void)
{
	if (g_ba.exact)
		logf_("CONFIG bow aim=exact (SOMVR_BOW_AIM; SOMVR_BOW_ASSIST_DEG not used): the arrow along the bow's own line, an "
		      "orc hit only where it passes through its head (%.0f cm round the Head node) or body (%.0f cm round its axis, "
		      "%.0f below to %.0f above its root) before the first wall or ground (the G21 cast%s)", g_ba.head_cm,
		      g_ba.body_cm, g_ba.span_lo_cm, g_ba.span_hi_cm, g_cl.on ? "" : ": NOT available, SOMVR_CLIMB is off - no walls");
	if (g_ba.ring)
		logf_("CONFIG bow aim_ring=on (SOMVR_BOW_AIM_RING): while drawn, a ring %.1f deg across where the line meets an "
		      "orc (wraith-blue) or the world (%s), else %.0f m along it; both eyes at its depth%s; look %s "
		      "(SOMVR_BOW_AIM_LOOK)", g_ba.ring_deg, g_ba.look ? "gold" : "soft white, a dark edge",
		      g_ba.far_cm / 100.0f, g_cl.on ? "" : " (no G21 cast: SOMVR_CLIMB off - orcs only, else far)",
		      g_ba.look ? "ring20 (build 20's rings)" : "sight (build 21: a thin ring and a dot)");
}

static void ba_end_log(void)
{
	if (!g_ba.exact && !g_ba.ring)
		return;
	logf_("BOW END aim (build 20): %s, ring %s; traces %llu (far %llu, world %llu, body %llu, head %llu), casts %llu (passed "
	      "through %llu, none %llu), ring entries %llu; shots: far %llu, world %llu, body %llu, head %llu",
	      g_ba.exact ? "exact" : "assist", g_ba.ring ? "on" : "off", (unsigned long long)g_ba.traces,
	      (unsigned long long)g_ba.kinds[BA_FAR], (unsigned long long)g_ba.kinds[BA_WORLD],
	      (unsigned long long)g_ba.kinds[BA_BODY], (unsigned long long)g_ba.kinds[BA_HEAD], (unsigned long long)g_ba.casts,
	      (unsigned long long)g_ba.steps, (unsigned long long)g_ba.nocast, (unsigned long long)g_ba.entries,
	      (unsigned long long)g_ba.shots[BA_FAR], (unsigned long long)g_ba.shots[BA_WORLD],
	      (unsigned long long)g_ba.shots[BA_BODY], (unsigned long long)g_ba.shots[BA_HEAD]);
}
