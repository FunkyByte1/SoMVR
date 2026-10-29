/* somvr_surface_game.c - SOMVR_HANDS_SURFACE=on: the hands and the sword stopped at surfaces, the game side
 * (somvr_surface.h; docs/re-xr-pipeline.md 38; the HL:A gap HG-6). NOT a translation unit of its own: somvr.c includes
 * it after somvr_vrhands_game.c (it uses the climb's cast and surface lookups (g_cl_sites, CL_FN, cl_rec_name; read
 * only, never patched), g_arm, g_vh, g_cb.bcfg, the haptics, df_clank_fx, the exceptions' predicates).
 *   SOMVR_HANDS_SURFACE=on   (default off) each stereo frame, in in_frame after the climb (the game thread, after the
 *                            render, where the climb casts): per hand the game's ray (CL_RAYCAST, the combat collider's
 *                            query plus 0x80 = the material and 0x20 = the hit's normal at r+0x0c (Physics_RayCast
 *                            0x8de79b / 0x8dea56 -> r+0x0c..0x18 = normal, plane d)) from a body-safe point (the head
 *                            in the yaw frame: 18 cm out, 25 down, 8 back, x the player size) to the palm where the
 *                            controller puts it, then from the palm as shown to the finger tips, and for the sword hand
 *                            from the shown grip along the blade to its tip; Talion's own object and every surface that
 *                            does not stop a hand (somvr_sf_solid: no record (an orc's capsule), Flesh / Armor,
 *                            foliage, water, ...) are
 *                            passed through (on 6 cm past, at most 6 times). The next phase 2 (arm_eval, before
 *                            SOMVR_TWOHAND and the VR hands) steps somvr_surface with those planes: the grips the arms
 *                            are solved to (so the game's IK, the drawn sword on R_Weapon, the combat's bone blade and
 *                            our VR hands) stop at the surfaces; the real grips go to the layer as the ghost (v3).
 *                            Never clamped: a climb's holding hand, both while the bow is out, the wrist panel open, a
 *                            pause, a finisher's moment. Haptics: a tick at a hand's contact (by its speed), the clank
 *                            (crack + ring) at the blade's, with the defence's sparks (SOMVR_DEFENCE_CLANK=fx) when fast.
 *   SOMVR_HANDS_SURFACE_GHOST=A    the ghost's opacity at full strength (0.35; 0 = none)
 *   SOMVR_HANDS_SURFACE_BLADE_MAX_CM=N  the sword pushes the hand back at most N cm (25; 0 = the blade never stops)
 *   SOMVR_HANDS_SURFACE_BLADE=push|pivot (default push = build 13; build 20, docs/re-xr-pipeline.md 79) pivot: the blade
 *                            does not push the hand - the hand stays the controller's (1:1) and the drawn blade turns
 *                            about the grip so it stays at the surface its ray met (walls, rock, the ground; orcs are no
 *                            surface), riding along it, up to SOMVR_SF's 85 deg off the hand (past it, or with the grip
 *                            itself at the surface, it goes in); the hits' blade is the drawn one, so no cut passes a wall.
 *                            Its meeting: the clank's pulses, SOMVR_HANDS_SURFACE_SOUND and the defence's sparks
 *                            (SOMVR_DEFENCE_CLANK_FX's name) where the blade meets the surface, in the world (build 13's
 *                            sparks sat on the sword's model). somvr_hitfeel_game.c's sfb_arm does it each arms frame.
 *   SOMVR_HANDS_SURFACE_SOUND=EVENT|off (default cbt_imp_surface_player: the game's own hard-surface impact) the pivot's
 *                            sound at the meeting (posted from THREAT's Wwise object; needs its TH_SND sites)
 * Lines: `CONFIG surface ...`, `SURF clamp|hold|release|blade|ghost|except|hit ...`, a summary every 10 s, `SURF END`. */

#define SF_BODY_X 18.0f /* the body-safe point: the shoulder, cm out (left: -), up, ahead in the head's yaw frame */
#define SF_BODY_Y -25.0f
#define SF_BODY_Z -8.0f
#define SF_PAST_CM 6.0f   /* a passed hit (Talion, a soft surface): the ray goes on from this far past it */
#define SF_MAX_STEPS 6
#define SF_FAR_CM 60.0f   /* a palm deeper than this behind a plane: not that surface (a ray past an edge) */
#define SF_CAST_FLAGS 0xf8u /* CL_CAST_FLAGS (0x58) | CL_CAST_MATERIAL (0x80) | 0x20: the hit's normal */
#define SF_NHIT 24        /* distinct hit objects / surfaces logged once (`SURF hit`) */

static somvr_vh_model g_sf_m; /* our drawn hand: the samples' places in the grip frame (the layer's model) */
struct sf_found {
	uint64_t frame;              /* the stereo frame the casts were made in */
	int npl, nbpl;
	somvr_sf_plane pl[SOMVR_SF_NPL], bpl[SOMVR_SF_NPL];
	char name[SOMVR_SF_NPL][32], bname[SOMVR_SF_NPL][32];
	uintptr_t obj[SOMVR_SF_NPL], bobj[SOMVR_SF_NPL];
};
static struct {
	int want, on, tid, model_ok;
	int menu_off;                /* build 13: the VR menu's Hands at walls: Off (an exception for both hands, no casts) */
	somvr_sf_cfg cfg;
	somvr_vh_cfg vc;             /* the drawn hand's (SOMVR_XR_HANDS_CAL, as the layer's) */
	somvr_sf_hand st[2];
	struct sf_found f[2];        /* in_frame -> the next arm_eval */
	struct cl_ud ud;             /* the casts' user data: Talion's object, skipped by the filter */
	uintptr_t vt_phys, self;
	float pg[2][SOMVR_SF_NS][3]; /* the samples in the game's grip frame (cm at size 1), from the last curls */
	float rad[SOMVR_SF_NS];
	somvr_posef ghost[2];        /* arm_eval -> vh_arm: the real grips (XR) and the ghost's opacity */
	float ghost_a[2];
	double t_last, next_log, hold_next[2];
	const char *why[2];          /* the exception holding each hand now (NULL: free) */
	int lines, lsec_n;
	double lsec;
	uint64_t suppressed;
	struct { uintptr_t obj; uint16_t id; } seen[SF_NHIT];
	int nseen;
	/* build 20, SOMVR_HANDS_SURFACE_BLADE=pivot (somvr_hitfeel_game.c sfb_arm) */
	char sound[64];              /* SOMVR_HANDS_SURFACE_SOUND ("" = none) */
	somvr_sf_pv pv;              /* the turned blade's contact */
	float p_now[3];              /* this arms frame's turn of the drawn blade (the grips' space; 0 = none), for swl_arm */
	double pv_t, pv_hold_next, pv_pend_t;
	int pv_void, ok_fx, fx_checked;
	int pv_pend;                 /* a meeting's sound (1) and sparks (2), made in the next combat frame (sfb_h4) */
	float pv_P[3];               /* ... at the contact (world cm) */
	char pv_surf[32];
	uint64_t pv_frames, pv_meets, pv_clanks, pv_sounds, pv_sound_fail, pv_fx, pv_fx_fail, pv_voids;
	float pv_peak_deg;
	/* build 22, SOMVR_HANDS_SURFACE_GROUND (docs/re-xr-pipeline.md 93.2): the open ground no ray along the blade meets */
	int ground;                  /* 1: a ray straight down at the tip (the practice's ground query, pr_ray_down) */
	uintptr_t orc_h[16];         /* the combat frame's orcs (their objects: an orc under the tip is no ground) */
	int norc;
	uint64_t gr_casts, gr_planes, gr_orcs, gr_soft, gr_noray;
	float gr_deep_max;
	int gr_lines;
	double gr_line_t;
	struct {
		uint64_t frames, evals, casts, ray_fail, hits, passed_self, passed_soft, far, clamped[2], contacts[2], releases[2],
			blades, clanks, btouches, voids, fx, ticks, eases, ghosts[2], except[2];
		float depth_max[2], bdepth_max;
		double us, us_max; /* sf_frame's time (the casts) */
	} n, w; /* the run's / the last 10 s */
} g_sf;
static const char *const g_sf_side[] = { "L", "R" };

static int sf_log_ok(double t)
{
	if (g_sf.lines < 300)
		return ++g_sf.lines;
	if (t >= g_sf.lsec + 1.0)
		g_sf.lsec = t, g_sf.lsec_n = 0;
	if (g_sf.lsec_n < 10)
		return ++g_sf.lsec_n;
	g_sf.suppressed++;
	return 0;
}
#define SF_COUNT(f) (g_sf.n.f++, g_sf.w.f++)

/* the casts' filter (FUN_0270c4c0's way, as cl_ray_filter): Talion's own object is not hit */
static uint64_t sf_ray_filter(uintptr_t obj, uintptr_t ud)
{
	if (obj && ud == (uintptr_t)&g_sf.ud && obj == g_sf.ud.self)
		return 0;
	return 1;
}

static int sf_cast1(uintptr_t phys, const float from[3], const float to[3], unsigned char *r)
{
	unsigned char q[0x40] __attribute__((aligned(16)));
	uint32_t fl = SF_CAST_FLAGS, flt = 0xfffffffdu;
	uint16_t none = 0xffff;
	uintptr_t cb = (uintptr_t)sf_ray_filter, ud = (uintptr_t)&g_sf.ud;
	memset(q, 0, sizeof q);
	memset(r, 0, 0x80);
	memcpy(q, from, 12), memcpy(q + 12, to, 12), memcpy(q + 0x18, &fl, 4), memcpy(q + 0x1c, &flt, 4);
	memcpy(q + 0x20, &cb, 8), memcpy(q + 0x30, &ud, 8);
	memcpy(r + 0x28, &none, 2);
	SF_COUNT(casts);
	return (int)(CL_FN(CLS_RAYCAST, cl_ray_fn)(phys, q, r) & 0xff);
}

/* a hit's surface name as the climb reads it (its record: the object's +0x5a, else the triangle's id); "" = none */
static void sf_surface(const unsigned char *r, uintptr_t obj, char *name, size_t cap, uint16_t *idp)
{
	name[0] = 0, *idp = 0;
	uintptr_t rec = CL_FN(CLS_SURF_RAY, cl_surfray_fn)(r);
	uint16_t oid = 0, tid = 0;
	struct { uint16_t id; uint8_t one, pad; } key = { 0, 1, 0 };
	if (obj)
		peek(obj + CL_OBJ_SURF, &oid, sizeof oid);
	if (r[0x2a])
		memcpy(&tid, r + 0x28, sizeof tid);
	if (rec && oid && (key.id = oid, CL_FN(CLS_SURF_ID, cl_surfid_fn)(&key) == rec))
		*idp = oid;
	else if (rec && tid && (key.id = tid, CL_FN(CLS_SURF_ID, cl_surfid_fn)(&key) == rec))
		*idp = tid;
	char nb[64];
	cl_rec_name(rec, nb, sizeof nb);
	size_t n = strnlen(nb, sizeof nb - 1);
	if (n >= cap)
		n = cap - 1;
	memcpy(name, nb, n), name[n] = 0;
}

/* a distinct object / surface hit: logged once (what the rays meet: the world, an orc's flesh, a prop) */
static void sf_seen(uintptr_t obj, uint16_t id, const char *name, int solid, sv_vec3 n, double t)
{
	for (int i = 0; i < g_sf.nseen; i++)
		if (g_sf.seen[i].obj == obj && g_sf.seen[i].id == id)
			return;
	if (g_sf.nseen >= SF_NHIT)
		return;
	g_sf.seen[g_sf.nseen].obj = obj, g_sf.seen[g_sf.nseen].id = id, g_sf.nseen++;
	if (sf_log_ok(t))
		logf_("SURF t=%.3f hit obj=%#lx surf='%s' (id %u): %s; normal %.2f,%.2f,%.2f", t, (unsigned long)obj, name,
		      (unsigned)id, obj == g_sf.self ? "Talion (passed)" : solid ? "stops a hand" : "passed (not solid)",
		      (double)n.x, (double)n.y, (double)n.z);
}

/* one ray from -> to (+ extra cm past it): the first solid surface's plane into pl[*n] (its name, object); 1 = one */
static int sf_ray(uintptr_t phys, sv_vec3 from, sv_vec3 to, float extra, somvr_sf_plane *pl, char *name, uintptr_t *objp,
		  double t)
{
	sv_vec3 d = { to.x - from.x, to.y - from.y, to.z - from.z };
	float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
	if (!(len > 0.5f) || len > 1e4f)
		return 0;
	d.x /= len, d.y /= len, d.z /= len, len += extra;
	float e[3] = { from.x + d.x * len, from.y + d.y * len, from.z + d.z * len }, s = 0;
	for (int k = 0; k <= SF_MAX_STEPS && s < len; k++) {
		float f[3] = { from.x + d.x * s, from.y + d.y * s, from.z + d.z * s }, hp[3], nr[3];
		unsigned char r[0x80] __attribute__((aligned(16)));
		if (!sf_cast1(phys, f, e, r))
			return 0;
		uintptr_t obj;
		memcpy(hp, r, sizeof hp), memcpy(nr, r + 0x0c, sizeof nr), memcpy(&obj, r + 0x20, sizeof obj);
		if (!vec_ok(hp, 1e7f))
			return 0;
		float hd = s + dist3(hp, f);
		if (hd > len + 1.0f)
			return 0;
		SF_COUNT(hits);
		char nm[32];
		uint16_t id = 0;
		sv_vec3 hn = { nr[0], nr[1], nr[2] };
		if (obj && obj == g_sf.self) {
			SF_COUNT(passed_self);
			sf_seen(obj, 0, "", 0, hn, t);
			s = hd + SF_PAST_CM;
			continue;
		}
		sf_surface(r, obj, nm, sizeof nm, &id);
		int solid = somvr_sf_solid(nm);
		sf_seen(obj, id, nm, solid, hn, t);
		if (!solid) {
			SF_COUNT(passed_soft);
			s = hd + SF_PAST_CM;
			continue;
		}
		if (!somvr_sf_plane_of(from, (sv_vec3){ hp[0], hp[1], hp[2] }, hn, pl))
			return 0;
		size_t n = strnlen(nm, 31);
		memcpy(name, nm, n), name[n] = 0;
		*objp = obj;
		return 1;
	}
	return 0;
}

/* build 22 (SOMVR_HANDS_SURFACE_GROUND): the combat frame's orcs (somvr.c's combat frame) */
static void sf_note_orcs(const struct cb_orc *o, int n)
{
	int k = 0;
	for (int i = 0; i < n && k < 16; i++)
		if (o[i].hobj)
			g_sf.orc_h[k++] = o[i].hobj;
	g_sf.norc = k;
}
/* build 22 (SOMVR_HANDS_SURFACE_GROUND): the open ground of the practice meadow is met by no ray along the blade (79.6:
 * 0 hits in 4209 casts, the ground query's flags along it 0 too) but a ray straight down finds it every time
 * (pr_ray_down: the debug spawn's cast). So with the tip under the grip, a ray straight down at the tip from the grip's
 * height to just under it: a hit there (not Talion, not an orc, a solid surface or the world's unnamed ground) is the
 * ground's plane - the normal from two more such rays 10 cm round the tip (else straight up). 1 = a plane */
static int sf_ground(sv_vec3 g, sv_vec3 tip, somvr_sf_plane *pl, char *name, uintptr_t *objp, double t)
{
	float r0 = g_sf.cfg.blade_r_cm + 1.0f;
	if (!(tip.y < g.y - 2.0f))
		return 0;
	if (!pr_ray_ok()) {
		g_sf.gr_noray++;
		return 0;
	}
	unsigned char r[0x80] __attribute__((aligned(16)));
	/* from 2 m over the tip (run 20261029-001737: 364 rays from the grip's own height down to the tip met nothing - as
	 * the debug spawn's ray, which starts 2 m up); a surface over the grip is no ground the blade is in: on under it */
	float from[3] = { tip.x, fmaxf(g.y, tip.y) + 200.0f, tip.z }, hit[3], end = tip.y - r0;
	uintptr_t obj = 0;
	int got = 0, k;
	for (k = 0; k < 4 && from[1] > end; k++) {
		g_sf.gr_casts++, SF_COUNT(casts);
		if (!pr_ray_down_r(from, from[1] - end, g_sf.self, hit, &obj, r))
			break;
		if (hit[1] <= g.y) {
			got = 1;
			break;
		}
		from[1] = hit[1] - 2.0f;
	}
	if (g_sf.gr_lines < 40 && t - g_sf.gr_line_t > 0.5) { /* (a few lines: where the tip is against the ground) */
		float gf[3] = { g.x, g.y + 200.0f, g.z }, gh[3] = { 0, NAN, 0 };
		uintptr_t go = 0;
		int gg = pr_ray_down(gf, 600.0f, g_sf.self, gh, &go);
		g_sf.gr_lines++, g_sf.gr_line_t = t;
		logf_("SURF t=%.3f ground probe: the grip %.1f,%.1f,%.1f, the tip %.1f,%.1f,%.1f: the ray down at the tip %s%.1f "
		      "(%d casts); the ground under the grip %s%.1f", t, g.x, g.y, g.z, tip.x, tip.y, tip.z, got ? "hit at " : "none ",
		      got ? hit[1] : (double)NAN, k + 1, gg ? "at " : "none ", gg ? gh[1] : (double)NAN);
	}
	if (!got)
		return 0;
	for (int i = 0; i < g_sf.norc; i++)
		if (obj && obj == g_sf.orc_h[i]) {
			g_sf.gr_orcs++;
			return 0;
		}
	char nm[32];
	uint16_t id = 0;
	sf_surface(r, obj, nm, sizeof nm, &id);
	/* the meadow's ground may have no surface record (''): the world's own; a named soft one (flesh, water..) is not */
	if (nm[0] && !somvr_sf_solid(nm)) {
		g_sf.gr_soft++;
		return 0;
	}
	/* the normal: the ground 10 cm round the tip (two more rays; a slope), else straight up */
	sv_vec3 n = { 0, 1, 0 }, side = { -(tip.z - g.z), 0, tip.x - g.x };
	float sl = sqrtf(side.x * side.x + side.z * side.z);
	sv_vec3 fw = sl > 1.0f ? (sv_vec3){ side.z / sl, 0, -side.x / sl } : (sv_vec3){ 0, 0, 1 };
	side = sl > 1.0f ? (sv_vec3){ side.x / sl, 0, side.z / sl } : (sv_vec3){ 1, 0, 0 };
	float ha[3], hb[3], fa[3] = { tip.x + 10.0f * fw.x, g.y, tip.z + 10.0f * fw.z },
			      fb[3] = { tip.x + 10.0f * side.x, g.y, tip.z + 10.0f * side.z };
	uintptr_t oa = 0, ob = 0;
	g_sf.gr_casts += 2, SF_COUNT(casts), SF_COUNT(casts);
	if (pr_ray_down(fa, g.y - tip.y + 60.0f, g_sf.self, ha, &oa) && pr_ray_down(fb, g.y - tip.y + 60.0f, g_sf.self, hb, &ob) &&
	    oa == obj && ob == obj) {
		sv_vec3 a = { ha[0] - hit[0], ha[1] - hit[1], ha[2] - hit[2] }, b = { hb[0] - hit[0], hb[1] - hit[1], hb[2] - hit[2] };
		sv_vec3 c = { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
		float cl = sqrtf(c.x * c.x + c.y * c.y + c.z * c.z);
		if (cl > 1e-3f) {
			c = (sv_vec3){ c.x / cl, c.y / cl, c.z / cl };
			if (c.y < 0)
				c = (sv_vec3){ -c.x, -c.y, -c.z };
			if (c.y > 0.5f) /* at most 60 deg of slope: steeper is a wall the blade's own ray meets */
				n = c;
		}
	}
	if (!somvr_sf_plane_of(g, (sv_vec3){ hit[0], hit[1], hit[2] }, n, pl))
		return 0;
	SF_COUNT(hits);
	sf_seen(obj, id, nm[0] ? nm : "ground", 1, n, t);
	snprintf(name, 32, "%s", nm[0] ? nm : "ground");
	*objp = obj;
	g_sf.gr_planes++;
	g_sf.gr_deep_max = fmaxf(g_sf.gr_deep_max, hit[1] - tip.y);
	return 1;
}

/* the samples in the game's grip frame from the drawn hand's curls (somvr_vh_joint_g: the right grip's frame, XR m;
 * the left mirrored; the game's frame: (x, y, -z) x 100): the palm (the wrist and the middle knuckle's midpoint), the
 * index, middle and thumb tips */
static void sf_samples(int h, const float curl[5])
{
	static const float rest[5] = { 0.3f, 0.3f, 0.3f, 0.3f, 0.3f };
	float skin[SOMVR_VH_NJ][12], j[4][3];
	static const int jn[4] = { 0, 11, 9, 14 };
	somvr_vh_pose(&g_sf_m, &g_sf.vc, curl ? curl : rest, skin);
	for (int k = 0; k < 4; k++)
		somvr_vh_joint_g(&g_sf_m, (const float(*)[12])skin, jn[k], j[k]);
	float pal[3] = { 0.5f * (j[0][0] + j[1][0]), 0.5f * (j[0][1] + j[1][1]), 0.5f * (j[0][2] + j[1][2]) }, th[3];
	somvr_vh_joint_g(&g_sf_m, (const float(*)[12])skin, 4, th);
	const float *pt[SOMVR_SF_NS] = { pal, j[2], j[3], th };
	for (int i = 0; i < SOMVR_SF_NS; i++) {
		g_sf.pg[h][i][0] = (h ? 100.0f : -100.0f) * pt[i][0];
		g_sf.pg[h][i][1] = 100.0f * pt[i][1];
		g_sf.pg[h][i][2] = -100.0f * pt[i][2];
	}
}

/* world point of grip-frame sample i of hand h (k: the player size) */
static sv_vec3 sf_at(sv_vec3 p, sv_quat q, int h, int i, float k)
{
	sv_vec3 o = sv_qrot(q, (sv_vec3){ k * g_sf.pg[h][i][0], k * g_sf.pg[h][i][1], k * g_sf.pg[h][i][2] });
	return (sv_vec3){ p.x + o.x, p.y + o.y, p.z + o.z };
}

/* the blade in the world from the grip's world pose (somvr_blade_controller's lay-out, world cm) */
static void sf_blade(sv_vec3 p, sv_quat q, sv_vec3 *base, sv_vec3 *tip)
{
	const sc_blade_cfg *c = &g_cb.bcfg;
	sv_vec3 d = sv_qrot(q, (sv_vec3){ 0, sinf(c->pitch_rad), cosf(c->pitch_rad) });
	sv_vec3 o = sv_qrot(q, (sv_vec3){ c->ofs_cm[0], c->ofs_cm[1], c->ofs_cm[2] });
	sv_vec3 g = { p.x + o.x, p.y + o.y, p.z + o.z };
	*base = (sv_vec3){ g.x - c->back_cm * d.x, g.y - c->back_cm * d.y, g.z - c->back_cm * d.z };
	*tip = (sv_vec3){ g.x + c->len_cm * d.x, g.y + c->len_cm * d.y, g.z + c->len_cm * d.z };
}

/* the exception that holds hand h now (NULL: none) */
static const char *sf_except(int h, const int bowed[2])
{
	if (g_sf.menu_off)
		return "the VR menu's Hands at walls: Off";
	if (cl_holds(h))
		return "a climb hold";
	if (bo_out() || bowed[0] || bowed[1])
		return "the bow is out";
	if (vm_open())
		return "the wrist panel is open";
	if (game_paused())
		return "a pause";
	if (fi_holds())
		return "a finisher holds an orc";
	return NULL; /* (the Nemesis verbs' latched hand on an orc: an orc's body never clamps, so it needs no exception) */
}

/* the sword in the right hand (not the bow) */
static int sf_blade_on(void)
{
	return bd_sword_in_hand() && !bo_out();
}

/* in_frame (a stereo frame, after the climb): the casts for the next phase 2. pw/qw: the controllers in the world,
 * hp/hq: the head */
static void sf_frame(const sv_vec3 pw[2], const sv_quat qw[2], sv_vec3 hp, sv_quat hq)
{
	if (!g_sf.on)
		return;
	if (!g_sf.tid)
		g_sf.tid = my_tid();
	if (g_sf.tid != my_tid())
		return;
	SF_COUNT(frames);
	double t = cb_now(), t0 = mono();
	uintptr_t phys = peek_ptr(g_clg.phys_pp), pm = 0, obj = 0;
	for (int h = 0; h < 2; h++)
		g_sf.f[h].npl = g_sf.f[h].nbpl = 0, g_sf.f[h].frame = g_fr.frame;
	if (g_sf.menu_off) /* the VR menu turned it off: no casts (sf_arm releases both hands by the exception) */
		return;
	if (!phys || vptr_of((void *)phys) != g_sf.vt_phys ||
	    peek_ptr(g_sf.vt_phys + 8u * CL_SLOT_RAYCAST) != g_cl_sites[CLS_RAYCAST].target) {
		SF_COUNT(ray_fail);
		return;
	}
	g_sf.self = g_sf.ud.self = player_objs(g_fr.pl, &pm, &obj) ? obj : 0;
	float k = somvr_rig_scale(&g_rig);
	if (!(k > 0.2f && k < 5.0f))
		k = 1;
	sv_quat y = sv_qyaw(sv_heading(hq));
	for (int h = 0; h < 2; h++) {
		if (!g_inp.hands[h].valid)
			continue;
		struct sf_found *f = &g_sf.f[h];
		sv_vec3 bo = sv_qrot(y, (sv_vec3){ (h ? SF_BODY_X : -SF_BODY_X) * k, SF_BODY_Y * k, SF_BODY_Z * k });
		sv_vec3 body = { hp.x + bo.x, hp.y + bo.y, hp.z + bo.z }, off = g_sf.st[h].off;
		sv_vec3 palm = sf_at(pw[h], qw[h], h, 0, k);
		float r0 = g_sf.rad[0] * k;
		if (sf_ray(phys, body, palm, r0 + 2.0f, &f->pl[f->npl], f->name[f->npl], &f->obj[f->npl], t)) {
			const somvr_sf_plane *p = &f->pl[f->npl];
			float deep = r0 - ((palm.x - p->p.x) * p->n.x + (palm.y - p->p.y) * p->n.y + (palm.z - p->p.z) * p->n.z);
			if (deep > SF_FAR_CM * k)
				SF_COUNT(far);
			else
				f->npl++;
		}
		/* from the palm as shown (the last offset) to each finger tip as shown */
		sv_vec3 sp = { palm.x + off.x, palm.y + off.y, palm.z + off.z };
		for (int i = 1; i < SOMVR_SF_NS && f->npl < SOMVR_SF_NPL; i++) {
			sv_vec3 tp = sf_at(pw[h], qw[h], h, i, k);
			tp.x += off.x, tp.y += off.y, tp.z += off.z;
			if (sf_ray(phys, sp, tp, g_sf.rad[i] * k + 1.0f, &f->pl[f->npl], f->name[f->npl], &f->obj[f->npl], t))
				f->npl++;
		}
		/* the sword hand: along the blade as shown */
		if (h == SA_R && sf_blade_on() && g_sf.cfg.blade_max_cm > 0) {
			sv_vec3 base, tip, g = { pw[h].x + off.x, pw[h].y + off.y, pw[h].z + off.z };
			sf_blade(g, qw[h], &base, &tip);
			if (sf_ray(phys, g, tip, g_sf.cfg.blade_r_cm + 1.0f, &f->bpl[0], f->bname[0], &f->bobj[0], t))
				f->nbpl = 1;
			else if (g_sf.ground && sf_ground(g, tip, &f->bpl[0], f->bname[0], &f->bobj[0], t))
				f->nbpl = 1; /* build 22: the open ground under the tip */
		}
	}
	double us = (mono() - t0) * 1e6;
	g_sf.n.us += us, g_sf.w.us += us;
	g_sf.n.us_max = fmax(g_sf.n.us_max, us), g_sf.w.us_max = fmax(g_sf.w.us_max, us);
}

static void sf_hap(int h, float amp, float dur)
{
	if (amp > 0)
		in_haptic_event(h, amp, dur, SOMVR_HAP_COMBAT);
}

/* arm_eval, after the bow's grips and before SOMVR_TWOHAND: each valid grip (game tracking cm, rig = the frame's)
 * stopped at the planes in_frame found; the real ones kept for the ghost */
static void sf_arm(sa_xf gg[2], const int gv[2], const int bowed[2], sv_vec3 shift)
{
	g_sf.ghost_a[0] = g_sf.ghost_a[1] = 0;
	if (!g_sf.on || my_tid() != g_sf.tid)
		return;
	double t = cb_now();
	float dt = g_sf.t_last > 0 && t > g_sf.t_last && t - g_sf.t_last < 0.25 ? (float)(t - g_sf.t_last) : 0;
	g_sf.t_last = t;
	SF_COUNT(evals);
	const sc_rigxf *rig = &g_arm.rig;
	float k = sc_rig_scale(rig);
	sv_quat y = sv_qyaw(rig->yaw), yi = sv_qyaw(-rig->yaw);
	for (int h = 0; h < 2; h++) {
		somvr_sf_hand *st = &g_sf.st[h];
		if (!gv[h]) {
			somvr_sf_hand_reset(st);
			continue;
		}
		if (g_sf.model_ok)
			sf_samples(h, g_vh.on ? g_vh.st[h].c : NULL);
		somvr_sf_in in;
		memset(&in, 0, sizeof in);
		const char *why = sf_except(h, bowed);
		if (why != g_sf.why[h]) {
			if (sf_log_ok(t))
				logf_("SURF t=%.3f except hand=%s: %s", t, g_sf_side[h], why ? why : "none (free again)");
			g_sf.why[h] = why;
		}
		in.free = !why;
		if (why)
			SF_COUNT(except[h]);
		sv_vec3 d = sv_qrot(y, (sv_vec3){ k * (gg[h].p.x - rig->p0[0]), k * (gg[h].p.y - rig->p0[1]),
						   k * (gg[h].p.z - rig->p0[2]) });
		sv_vec3 gw = { rig->base[0] + shift.x + d.x, rig->base[1] + shift.y + d.y, rig->base[2] + shift.z + d.z };
		sv_quat qw = sv_qnormalize(sv_qmul(y, gg[h].q));
		for (int i = 0; i < SOMVR_SF_NS; i++)
			in.s[i] = sf_at(gw, qw, h, i, k), in.r[i] = g_sf.rad[i] * k;
		const struct sf_found *f = &g_sf.f[h];
		int fresh = g_fr.frame >= f->frame && g_fr.frame - f->frame <= 2;
		if (fresh) {
			in.npl = f->npl, in.nbpl = f->nbpl;
			memcpy(in.pl, f->pl, sizeof in.pl), memcpy(in.bpl, f->bpl, sizeof in.bpl);
		}
		if (h == SA_R && sf_blade_on() && !g_sf.cfg.blade_pivot) { /* (pivot: sfb_arm turns the blade; no push) */
			in.blade = 1;
			sf_blade(gw, qw, &in.base, &in.tip);
		}
		somvr_sf_out o;
		somvr_sf_step(st, &g_sf.cfg, &in, t, dt, &o);
		/* the real grip for the ghost, then the shown one */
		vh_to_xr(&gg[h], &g_sf.ghost[h]);
		g_sf.ghost_a[h] = o.alpha;
		if (o.off.x != 0 || o.off.y != 0 || o.off.z != 0) {
			sv_vec3 dtk = sv_qrot(yi, (sv_vec3){ o.off.x / k, o.off.y / k, o.off.z / k });
			gg[h].p.x += dtk.x, gg[h].p.y += dtk.y, gg[h].p.z += dtk.z;
			SF_COUNT(clamped[h]);
		}
		const char *pn = o.plane >= 0 ? f->name[o.plane] : "-";
		uintptr_t po = o.plane >= 0 ? f->obj[o.plane] : 0;
		if (o.depth > g_sf.n.depth_max[h])
			g_sf.n.depth_max[h] = o.depth;
		if (o.depth > g_sf.w.depth_max[h])
			g_sf.w.depth_max[h] = o.depth;
		if (o.ev & SOMVR_SF_EV_CONTACT) {
			SF_COUNT(contacts[h]);
			sf_hap(h, o.tick, 0.02f);
			if (o.tick > 0)
				SF_COUNT(ticks);
			const somvr_sf_plane *p = &in.pl[o.plane >= 0 ? o.plane : 0];
			if (sf_log_ok(t))
				logf_("SURF t=%.3f clamp hand=%s depth=%.1f cm surf=%s obj=%#lx n=%.2f,%.2f,%.2f speed=%.0f cm/s tick=%.2f "
				      "frame=%llu", t, g_sf_side[h], (double)o.depth, pn, (unsigned long)po, (double)p->n.x, (double)p->n.y,
				      (double)p->n.z, (double)o.speed, (double)o.tick, (unsigned long long)g_fr.frame);
			g_sf.hold_next[h] = t + 0.5;
		} else if (st->contact && t >= g_sf.hold_next[h]) {
			g_sf.hold_next[h] = t + 0.5;
			if (sf_log_ok(t))
				logf_("SURF t=%.3f hold hand=%s depth=%.1f cm surf=%s shown-real=%.1f cm ghost=%.2f (%.2f s on)", t,
				      g_sf_side[h], (double)o.depth, pn, (double)sqrtf(o.off.x * o.off.x + o.off.y * o.off.y +
										  o.off.z * o.off.z), (double)o.alpha,
				      t - st->t_contact);
		}
		if ((o.ev & SOMVR_SF_EV_RELEASE) && (SF_COUNT(releases[h]), sf_log_ok(t)))
			logf_("SURF t=%.3f release hand=%s after %.2f s, deepest %.1f cm%s", t, g_sf_side[h], (double)o.held_s,
			      (double)o.depth_max, why ? " (an exception)" : "");
		if (h == SA_R && o.bdepth > g_sf.n.bdepth_max && !st->vd)
			g_sf.n.bdepth_max = o.bdepth;
		if (h == SA_R && o.bdepth > g_sf.w.bdepth_max && !st->vd)
			g_sf.w.bdepth_max = o.bdepth;
		if (o.ev & SOMVR_SF_EV_BLADE) {
			SF_COUNT(blades);
			const char *bn = o.bplane >= 0 ? f->bname[o.bplane] : "-";
			int fx = 0;
			if (o.clank > 0) {
				SF_COUNT(clanks);
				in_haptic_event(SOMVR_HAND_R, 0.9f * o.clank, 0.04f, SOMVR_HAP_COMBAT);
				in_haptic_ramp(SOMVR_HAND_R, 0.45f * o.clank, 0.15f, SOMVR_HAP_COMBAT);
				if (o.fx && g_df.ok_fx && g_df.clank_fx[0] && g_df.pcomb)
					fx = 1, SF_COUNT(fx), df_clank_fx(t);
			} else if (o.tick > 0) {
				SF_COUNT(btouches), SF_COUNT(ticks);
				sf_hap(SOMVR_HAND_R, o.tick, 0.02f);
			}
			if (sf_log_ok(t))
				logf_("SURF t=%.3f blade %s depth=%.1f cm surf=%s obj=%#lx speed=%.0f cm/s clank=%.2f fx=%d frame=%llu", t,
				      o.clank > 0 ? "clank" : "touch", (double)o.bdepth, bn,
				      (unsigned long)(o.bplane >= 0 ? f->bobj[o.bplane] : 0), (double)o.bspeed, (double)o.clank, fx,
				      (unsigned long long)g_fr.frame);
		}
		if ((o.ev & SOMVR_SF_EV_BLADE_OFF) && sf_log_ok(t))
			logf_("SURF t=%.3f blade release after %.2f s, deepest %.1f cm", t, (double)o.bheld_s, (double)o.bdepth_max);
		if ((o.ev & SOMVR_SF_EV_VOID) && (SF_COUNT(voids), sf_log_ok(t)))
			logf_("SURF t=%.3f blade void: the tip %.1f cm in (past %.0f cm: the blade goes in)", t, (double)o.bdepth,
			      (double)g_sf.cfg.blade_max_cm);
		if (o.ev & SOMVR_SF_EV_EASE)
			SF_COUNT(eases);
		if ((o.ev & SOMVR_SF_EV_GHOST_ON) && (SF_COUNT(ghosts[h]), sf_log_ok(t)))
			logf_("SURF t=%.3f ghost hand=%s on (shown %.1f cm from the real hand)", t, g_sf_side[h],
			      (double)sqrtf(o.off.x * o.off.x + o.off.y * o.off.y + o.off.z * o.off.z));
		if ((o.ev & SOMVR_SF_EV_GHOST_OFF) && sf_log_ok(t))
			logf_("SURF t=%.3f ghost hand=%s off", t, g_sf_side[h]);
	}
	if (t >= g_sf.next_log) {
		g_sf.next_log = t + 10.0;
		const typeof(g_sf.w) *c = &g_sf.w;
		logf_("SURF t=%.3f last 10 s: frames %llu, evals %llu, casts %llu (hits %llu: passed Talion %llu, soft %llu; far "
		      "%llu; no cast %llu), clamped frames L %llu R %llu (deepest %.1f / %.1f cm), contacts %llu / %llu, blade %llu "
		      "(clanks %llu, touches %llu, voids %llu, fx %llu, deepest %.1f cm), ticks %llu, eases %llu, ghosts %llu / %llu, "
		      "excepted L %llu R %llu; the casts %.0f us a frame (max %.0f); suppressed lines %llu", t,
		      (unsigned long long)c->frames,
		      (unsigned long long)c->evals, (unsigned long long)c->casts, (unsigned long long)c->hits,
		      (unsigned long long)c->passed_self, (unsigned long long)c->passed_soft, (unsigned long long)c->far,
		      (unsigned long long)c->ray_fail, (unsigned long long)c->clamped[0], (unsigned long long)c->clamped[1],
		      (double)c->depth_max[0], (double)c->depth_max[1], (unsigned long long)c->contacts[0],
		      (unsigned long long)c->contacts[1], (unsigned long long)c->blades, (unsigned long long)c->clanks,
		      (unsigned long long)c->btouches, (unsigned long long)c->voids, (unsigned long long)c->fx,
		      (double)c->bdepth_max, (unsigned long long)c->ticks, (unsigned long long)c->eases,
		      (unsigned long long)c->ghosts[0], (unsigned long long)c->ghosts[1], (unsigned long long)c->except[0],
		      (unsigned long long)c->except[1], c->frames ? c->us / (double)c->frames : 0.0, c->us_max,
		      (unsigned long long)g_sf.suppressed);
		memset(&g_sf.w, 0, sizeof g_sf.w);
	}
}

/* vh_arm: the ghosts into this frame's entry (v3) */
static void sf_ghost(somvr_vrhands_frame *e)
{
	for (int h = 0; h < 2; h++) {
		e->ghost_a[h] = g_sf.on ? g_sf.ghost_a[h] : 0;
		if (e->ghost_a[h] > 0)
			e->ghost[h] = g_sf.ghost[h];
	}
}

/* the VR menu (build 13): the Hands at walls row while SOMVR_HANDS_SURFACE=on runs; Off = both hands free at once (the
 * exceptions' release and ease back), on again from the next frame */
static uint32_t sf_vm_avail(void) { return g_sf.on ? SOMVR_VMA_SURFACE : 0u; }
static const char *sf_vm_set(const somvr_settings *s)
{
	if (!g_sf.on)
		return " (no SOMVR_HANDS_SURFACE=on running: not used)";
	g_sf.menu_off = !s->surface;
	return g_sf.menu_off ? " (the hands pass through the world)" : " (the hands stop at surfaces)";
}

/* bd_config (after SOMVR_VRHANDS) */
static void sf_config(int *bad)
{
	static const char *const w[] = { "off", "on" };
	g_sf.want = env_word("SOMVR_HANDS_SURFACE", w, 2, 0);
	somvr_sf_cfg_default(&g_sf.cfg);
	const char *v = getenv("SOMVR_HANDS_SURFACE_GHOST");
	if (v && *v && !somvr_sf_parse_ghost(v, &g_sf.cfg))
		*bad = 1, logf_("WARN SOMVR_HANDS_SURFACE_GHOST=%s is not 0..1: %.2f", v, (double)g_sf.cfg.ghost_a);
	v = getenv("SOMVR_HANDS_SURFACE_BLADE_MAX_CM");
	if (v && *v && !somvr_sf_parse_blade_max(v, &g_sf.cfg))
		*bad = 1, logf_("WARN SOMVR_HANDS_SURFACE_BLADE_MAX_CM=%s is not 0..100: %.0f", v, (double)g_sf.cfg.blade_max_cm);
	static const char *const bw[] = { "push", "pivot" };
	g_sf.cfg.blade_pivot = env_word("SOMVR_HANDS_SURFACE_BLADE", bw, 2, 0);
	g_sf.ground = env_word("SOMVR_HANDS_SURFACE_GROUND", w, 2, 0); /* build 22 (docs 93.2) */
	snprintf(g_sf.sound, sizeof g_sf.sound, "%s", "cbt_imp_surface_player");
	v = getenv("SOMVR_HANDS_SURFACE_SOUND");
	if (v && *v)
		snprintf(g_sf.sound, sizeof g_sf.sound, "%s", strcmp(v, "off") ? v : "");
}

/* bd_start (after the climb's start: its sites are checked here again, read only) */
static void sf_start(void)
{
	g_sf.on = 0;
	g_sf.rad[0] = g_sf.cfg.palm_r_cm, g_sf.rad[1] = g_sf.rad[2] = g_sf.rad[3] = g_sf.cfg.tip_r_cm;
	if (!g_sf.want) {
		logf_("CONFIG surface=off (the hands and the sword pass through the world, as before)");
		return;
	}
	const char *off = !g_arm.on ? "SOMVR_ARMS=1 (the grips the arms are solved to)" : !g_inp.on ? "SOMVR_INPUT=1" : NULL;
	char why[512] = "";
	static const int need[] = { CLS_RAYCAST, CLS_SURF_RAY, CLS_SURF_ID };
	for (size_t i = 0; !off && i < sizeof need / sizeof need[0]; i++)
		if (check_site(&g_cl_sites[need[i]], why, sizeof why) != 0)
			off = g_cl_sites[need[i]].name;
	if (off) {
		logf_("CONFIG surface=off: SOMVR_HANDS_SURFACE=on needs %s%s%s", off, why[0] ? ": " : "", why);
		return;
	}
	g_sf.vt_phys = g_cl_sites[CLS_RAYCAST].addr - 8u * CL_SLOT_RAYCAST;
	somvr_vh_cfg_default(&g_sf.vc);
	const char *cal = getenv("SOMVR_XR_HANDS_CAL");
	if (cal && *cal)
		somvr_vh_parse_cal(cal, &g_sf.vc);
	somvr_vh_parse_size(getenv("SOMVR_XR_HANDS_SIZE"), &g_sf.vc); /* build 17: the layer's drawn size, the same here */
	somvr_vh_parse_grip(getenv("SOMVR_XR_HANDS_GRIP_CM"), &g_sf.vc); /* build 18: the fist round the handle, as drawn */
	g_sf.model_ok = somvr_vh_init(&g_sf_m, &g_sf.vc) == 0;
	if (g_sf.model_ok) {
		sf_samples(0, NULL), sf_samples(1, NULL);
	} else { /* a fist's guess: the palm at the grip, the tips 6 cm ahead along the blade's axis */
		for (int h = 0; h < 2; h++)
			for (int i = 0; i < SOMVR_SF_NS; i++)
				g_sf.pg[h][i][0] = 0, g_sf.pg[h][i][1] = 0, g_sf.pg[h][i][2] = i ? 6.0f : 0;
	}
	for (int h = 0; h < 2; h++)
		somvr_sf_hand_reset(&g_sf.st[h]);
	g_sf.on = 1;
	g_sf.next_log = cb_now() + 10.0;
	const somvr_sf_cfg *c = &g_sf.cfg;
	logf_("CONFIG surface=on (the hands and the sword stop at the world's surfaces; orcs' bodies never): the rays from the "
	      "head's shoulder point (%.0f out, %.0f up, %.0f ahead cm) to the palm, the palm to the finger tips, the grip "
	      "along the blade (CL_RAYCAST flags %#x: the normal; Talion and soft surfaces passed); samples: palm r %.1f cm, "
	      "tips r %.1f cm %s (R palm %.1f,%.1f,%.1f index %.1f,%.1f,%.1f); the blade's margin %.1f cm, it pushes the hand "
	      "back at most %.0f cm; the ghost from %.0f cm, full at %.0f cm, opacity %.2f (in %.2f s, out %.2f s); a jump back "
	      "of over %.1f cm eases over %.2f s; ticks at %.0f cm/s full, the clank from %.0f cm/s (full %.0f, sparks %.0f); "
	      "never: a climb hold, the bow, the wrist panel, a pause, a finisher", SF_BODY_X, SF_BODY_Y, SF_BODY_Z,
	      SF_CAST_FLAGS, (double)c->palm_r_cm, (double)c->tip_r_cm,
	      g_sf.model_ok ? "on our drawn hand (its curls)" : "(no hand model: a fist's guess)", (double)g_sf.pg[1][0][0],
	      (double)g_sf.pg[1][0][1], (double)g_sf.pg[1][0][2], (double)g_sf.pg[1][1][0], (double)g_sf.pg[1][1][1],
	      (double)g_sf.pg[1][1][2], (double)c->blade_r_cm, (double)c->blade_max_cm, (double)c->ghost_cm,
	      (double)c->ghost_full_cm, (double)c->ghost_a, (double)c->ghost_in_s, (double)c->ghost_out_s, (double)c->jump_cm,
	      (double)c->spring_s, (double)c->tick_cms, (double)c->clank_cms, (double)c->clank_full_cms, (double)c->fx_cms);
	if (c->blade_pivot)
		logf_("CONFIG surface blade=pivot (SOMVR_HANDS_SURFACE_BLADE): the blade never pushes the hand (it stays 1:1); the "
		      "drawn blade turns about the grip to stay at the surface (at most %.0f deg off the hand; past it, or the grip "
		      "itself there, it goes in); its meeting: the clank's pulses, the sound '%s' (SOMVR_HANDS_SURFACE_SOUND%s) and "
		      "the sparks at the contact", (double)c->pivot_max_rad / 0.01745329, g_sf.sound[0] ? g_sf.sound : "none", "");
	logf_("CONFIG surface ground=%s (SOMVR_HANDS_SURFACE_GROUND, build 22)%s", g_sf.ground ? "on" : "off", g_sf.ground
	      ? ": with no surface on the blade's ray, a ray straight down at the tip (the practice's ground query) - the open "
		"ground the blade's own ray does not meet stops it too" : "");
}

static void sf_end_log(void)
{
	if (!g_sf.on)
		return;
	const typeof(g_sf.n) *c = &g_sf.n;
	logf_("SURF END frames %llu, evals %llu, casts %llu (hits %llu: passed Talion %llu, soft %llu; far %llu; no cast %llu), "
	      "clamped frames L %llu R %llu (deepest %.1f / %.1f cm), contacts %llu / %llu, releases %llu / %llu, blade %llu "
	      "(clanks %llu, touches %llu, voids %llu, fx %llu, deepest %.1f cm), ticks %llu, eases %llu, ghosts %llu / %llu, "
	      "excepted frames L %llu R %llu; distinct hits %d; the casts %.0f us a frame (max %.0f); suppressed lines %llu",
	      (unsigned long long)c->frames,
	      (unsigned long long)c->evals, (unsigned long long)c->casts, (unsigned long long)c->hits,
	      (unsigned long long)c->passed_self, (unsigned long long)c->passed_soft, (unsigned long long)c->far,
	      (unsigned long long)c->ray_fail, (unsigned long long)c->clamped[0], (unsigned long long)c->clamped[1],
	      (double)c->depth_max[0], (double)c->depth_max[1], (unsigned long long)c->contacts[0],
	      (unsigned long long)c->contacts[1], (unsigned long long)c->releases[0], (unsigned long long)c->releases[1],
	      (unsigned long long)c->blades, (unsigned long long)c->clanks, (unsigned long long)c->btouches,
	      (unsigned long long)c->voids, (unsigned long long)c->fx, (double)c->bdepth_max, (unsigned long long)c->ticks,
	      (unsigned long long)c->eases, (unsigned long long)c->ghosts[0], (unsigned long long)c->ghosts[1],
	      (unsigned long long)c->except[0], (unsigned long long)c->except[1], g_sf.nseen,
	      c->frames ? c->us / (double)c->frames : 0.0, c->us_max, (unsigned long long)g_sf.suppressed);
	if (g_sf.cfg.blade_pivot)
		logf_("SURF END blade=pivot: turned frames %llu, meetings %llu (clanks %llu), the largest turn %.1f deg; sounds %llu "
		      "(no playing id %llu), sparks %llu (failed %llu); went in (past the turn's cap or the grip at the surface) %llu",
		      (unsigned long long)g_sf.pv_frames, (unsigned long long)g_sf.pv_meets, (unsigned long long)g_sf.pv_clanks,
		      (double)g_sf.pv_peak_deg, (unsigned long long)g_sf.pv_sounds, (unsigned long long)g_sf.pv_sound_fail,
		      (unsigned long long)g_sf.pv_fx, (unsigned long long)g_sf.pv_fx_fail, (unsigned long long)g_sf.pv_voids);
	if (g_sf.ground || g_sf.gr_casts)
		logf_("SURF END build 22 ground=%s: rays down at the tip %llu, the ground's planes %llu (the tip at most %.1f cm under it), "
		      "an orc under the tip %llu, a soft surface %llu, no ray (the practice's site) %llu", g_sf.ground ? "on" : "off",
		      (unsigned long long)g_sf.gr_casts, (unsigned long long)g_sf.gr_planes, (double)g_sf.gr_deep_max,
		      (unsigned long long)g_sf.gr_orcs, (unsigned long long)g_sf.gr_soft, (unsigned long long)g_sf.gr_noray);
}

/* build 22, a test word: "surface ground on|off" (build 20's blade against the ground's ray, one boot) */
static const char *sf_word(const char *w)
{
	if (!strcmp(w, "ground on") || !strcmp(w, "ground off")) {
		g_sf.ground = w[8] == 'n';
		logf_("SURF the word 'surface %s': SOMVR_HANDS_SURFACE_GROUND now %s (a test)", w, g_sf.ground ? "on" : "off");
		return g_sf.ground ? "surface ground on" : "surface ground off";
	}
	return "surface ground on|off";
}
