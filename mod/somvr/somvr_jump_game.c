/* somvr_jump_game.c - build 22: the jump and the vault, the game side (SOMVR_JUMP, SOMVR_VAULT; somvr_jump.h,
 * docs/re-xr-pipeline.md 91, handoff/HANDOFF-loco.md). Included by somvr.c last:
 *   jp_config (at the start): the switches and their knobs;
 *   jp_word: `jump on|off|state|trace SECS|stock on|off|cm N`, `vault on|off|scan [CM]` (SOMVR_HANDS_TEST's trigger file);
 *   jp_a_press / jp_a_release (ab_tick, A's press and let-go when the strike and the dodge have not taken it): on foot
 *     the vault (its ring shown), the jump (moving), or the climb first (standing: the descent's edge; its "nothing" -
 *     jp_climb_passed from cl_frame - makes it the jump); jp_owns_a: the climb sees A up for our press;
 *   jp_pm (lo_pm_pre, after the step's moves, on the PlayerMovement tick before vf_11): the press's take-off (on the
 *     ground, LOCO's tick), the arc's tick (somvr_jp_step: the proxy's gravity flag pc+0x60 off, its dy in LOCO's delta,
 *     the vault's horizontal too), gravity back at its end (not when the climb caught him);
 *   jp_exact (the anchor): the vertical exact while in the air (the root's own arc; its brake leaves no dip);
 *   jv_frame (H4, a stereo frame, after ws_frame, before ob_frame): the vault's landing searched every 0.1 s on foot
 *     (the ring, a light tick when it shows), the per-frame view lines while in the air;
 *   jv_entry (ob_frame): the vault's ring;
 *   jp_end_log.
 * Lines: `CONFIG jump ...`, `JUMP t=... press|go|refused|end|ceiling ...`, `JUMP tick ...` and `JUMP view ...` (per tick
 * and per frame while in the air and 0.3 s after), `VAULT t=... ring|no ring ...`, `VAULT scan ...`, `JUMP trace ...`,
 * `JUMP END build 22 ...`. */

#define JP_TICK_LINES 12000 /* JUMP tick / view / trace lines at most (a boot's tests) */
#define JP_PASS_S 0.25      /* the climb's "nothing" within this of the press makes it the jump */
#define JP_RING_KEEP_S 0.3  /* a ring no longer found stays this long (no flicker) */
/* how the arc's vertical step reaches the body (SOMVR_JUMP_VIA, the word `jump via`): run 001108 - in LOCO's root delta
 * alone the rise stopped after two ticks at 20 Hz (+22, +17 cm, then the +12 not applied: 39 cm every jump) while the
 * fall's steps went through; the climb's rises use its one-shot extra move and, through the rock, the proxy set up by
 * the step after vf_11 (cl_thr_set) */
enum { JP_VIA_DELTA = 0, JP_VIA_EXTRA = 1, JP_VIA_SETPOS = 2 };
static const char *const g_jp_via_w[] = { "delta", "extra", "setpos" };

static struct {
	int want_jump, want_vault; /* the switches at the start (the words turn them off and on again) */
	somvr_jp_cfg cfg;
	somvr_jp s;
	/* A's press */
	int a_owned;           /* this press is ours (the climb sees A up) */
	int press;             /* a take-off waiting for the PM tick: SOMVR_JPK_JUMP / _VAULT (0 none) */
	double press_t;
	int climb_first;       /* the press went to the climb first (standing): its "nothing" = the jump */
	double climb_first_t;
	int press_why;         /* somvr_jp_a_pick's answer at the press */
	float press_speed;
	/* the arc's ticks */
	float g;               /* this jump's gravity (cm/s^2) */
	uintptr_t pm, pc;      /* the proxy whose gravity flag we hold */
	int grav_off;          /* we wrote it 0 (and must write it 1 at the end) */
	double t0, view_until;
	sv_vec3 root0, eye_last;
	int have_eye;
	float floor_y;
	int floor_ok;
	float step_max_cm, eye_step_max_cm, yaw0, yaw_dev_max;
	/* the vault's ring */
	int ring;              /* shown */
	double ring_t, next_find, ring_first_t;
	somvr_jp_vplan plan;
	sv_vec3 ring_dir, find_root;
	int last_why;
	uint64_t finds, find_rays, ring_shows, ring_frames;
	/* tests */
	double trace_until;
	int stock;             /* `jump stock on`: the game's own A and free-run (game moves on, LOCO's latch the game's) */
	int stock_gm, stock_latch, stock_descent, stock_mark;
	uint32_t lines, tick_lines;
	uint64_t presses, refused, climb_firsts, climb_passes, climb_kept, takeoffs, vault_fallbacks, ticks, grav_writes,
		grav_resets, other_tid;
	float land_off_max;
	int via;               /* JP_VIA_* */
	int extra_pending;     /* the extra move written this tick (cleared after vf_11 if not taken) */
	float post_dy;         /* JP_VIA_SETPOS: this tick's rise, the proxy set up by it after vf_11 */
	uint64_t extra_taken, extra_missed, setpos_n, setpos_ceil;
	_Atomic int req_via;
	_Atomic int req_jump, req_vault, req_state, req_stock, req_scan, req_cm;
	_Atomic int req_trace_ms;
} g_jp = { .req_jump = -1, .req_vault = -1, .req_stock = -1, .req_cm = -1, .req_via = -1 };
static _Atomic int g_sv_req, g_sv_req_r, g_sv_req_step; /* build 24: `vault grid|edges` (sv_frame) */

static int jp_log_ok(void) { return g_jp.lines++ < 800; }
static int jp_tick_ok(void) { return g_jp.tick_lines++ < JP_TICK_LINES; }
static int jp_owns_a(void) { return g_jp.a_owned; }
static uint64_t jp_vm_avail(void) { return g_jp.cfg.jump ? SOMVR_VMA_JUMP : 0u; }
static int jp_air(void) { return g_jp.s.kind != SOMVR_JPK_NONE; }
static int jp_exact(void) { return g_jp.s.kind != SOMVR_JPK_NONE; }
static int jv_wanted(void) { return g_jp.want_vault; }
static int jp_extra_pending(void) { return g_jp.extra_pending; }

static void jp_config(int *bad)
{
	somvr_jp_cfg_default(&g_jp.cfg);
	somvr_jp_init(&g_jp.s);
	g_jp.want_jump = g_jp.cfg.jump = env_word("SOMVR_JUMP", (const char *const[]){ "off", "on" }, 2, 0);
	g_jp.want_vault = g_jp.cfg.vault = env_word("SOMVR_VAULT", (const char *const[]){ "off", "on" }, 2, 0);
	g_jp.cfg.jump_cm = env_float("SOMVR_JUMP_CM", 60, 20, 150, bad);
	g_jp.cfg.v_max_cm = env_float("SOMVR_VAULT_MAX_CM", 120, 60, 150, bad);
	g_jp.cfg.v_cms = env_float("SOMVR_VAULT_CMS", 300, 150, 700, bad);
	g_jp.cfg.v_ring_stop = env_word("SOMVR_VAULT_RING_STOP", (const char *const[]){ "off", "on" }, 2, 1);
	g_jp.via = env_word("SOMVR_JUMP_VIA", g_jp_via_w, 3, JP_VIA_SETPOS);
	if (g_cl.cfg.foot_cm > 0)
		g_jp.cfg.foot_cm = g_cl.cfg.foot_cm;
	if (g_jp.cfg.jump || g_jp.cfg.vault)
		logf_("CONFIG jump=%s vault=%s: A on foot when nothing else claims it (the strike's mark, the dodge in a fight, a "
		      "climb, the descent's edge standing) = the jump: %.0f cm up (%.0f cm/s at the game's gravity), the stick's "
		      "speed carried 1:1 in the air, the landing braked over the last %.0f cm; a low obstacle (%.0f-%.0f cm over the "
		      "feet) within %.0f-%.0f cm = its ring past it (or onto a deep top) and A = the vault over it (at least %.0f cm/s, "
		      "the feet %.0f cm over its top); LOCO's delta with the proxy's gravity off; no animation in the view "
		      "(SOMVR_JUMP, SOMVR_JUMP_CM, SOMVR_VAULT, SOMVR_VAULT_MAX_CM, SOMVR_VAULT_CMS); the vault never past its ring: %s "
		      "(SOMVR_VAULT_RING_STOP)",
		      g_jp.cfg.jump ? "on" : "off", g_jp.cfg.vault ? "on" : "off", g_jp.cfg.jump_cm,
		      somvr_jp_takeoff(g_jp.cfg.jump_cm, g_jp.cfg.g), g_jp.cfg.soft_cm, g_jp.cfg.v_min_cm, g_jp.cfg.v_max_cm,
		      g_jp.cfg.v_reach_cm, g_jp.cfg.v_reach_max_cm, g_jp.cfg.v_cms, g_jp.cfg.v_clear_cm,
		      g_jp.cfg.v_ring_stop ? "on" : "off");
}

/* the words (another thread: requests only) */
static const char *jp_word(const char *w)
{
	if (!strcmp(w, "on") || !strcmp(w, "off"))
		return atomic_store(&g_jp.req_jump, w[1] == 'n'), w[1] == 'n' ? "jump on" : "jump off";
	if (!strcmp(w, "state"))
		return atomic_store(&g_jp.req_state, 1), "jump state";
	if (!strncmp(w, "trace ", 6)) {
		float s = (float)atof(w + 6);
		if (!(s > 0 && s <= 30))
			return "jump trace SECS (0..30)";
		atomic_store(&g_jp.req_trace_ms, (int)(s * 1000));
		return "jump trace";
	}
	if (!strcmp(w, "stock on") || !strcmp(w, "stock off"))
		return atomic_store(&g_jp.req_stock, w[7] == 'n'), "jump stock";
	if (!strncmp(w, "via ", 4)) {
		for (int i = 0; i < 3; i++)
			if (!strcmp(w + 4, g_jp_via_w[i]))
				return atomic_store(&g_jp.req_via, i), "jump via";
		return "jump via delta|extra|setpos";
	}
	if (!strncmp(w, "cm ", 3)) {
		int cm = atoi(w + 3);
		if (cm < 20 || cm > 150)
			return "jump cm N (20..150)";
		atomic_store(&g_jp.req_cm, cm);
		return "jump cm";
	}
	return "jump on|off|state|trace SECS|stock on|off|cm N";
}
static const char *jv_word(const char *w)
{
	if (!strcmp(w, "on") || !strcmp(w, "off"))
		return atomic_store(&g_jp.req_vault, w[1] == 'n'), w[1] == 'n' ? "vault on" : "vault off";
	if (!strncmp(w, "scan", 4)) {
		int cm = w[4] == ' ' ? atoi(w + 5) : 600;
		atomic_store(&g_jp.req_scan, cm >= 100 && cm <= 1500 ? cm : 600);
		return "vault scan";
	}
	if (!strncmp(w, "grid", 4) || !strncmp(w, "edges", 5)) { /* build 24's survey (a test aid) */
		int kind = w[0] == 'g' ? 1 : 2, r = 0, step = 0;
		sscanf(w + (kind == 1 ? 4 : 5), "%d %d", &r, &step);
		r = r >= 200 && r <= 3000 ? r : 1500;
		step = step >= 50 && step <= 500 ? step : (kind == 1 ? 150 : 100);
		atomic_store(&g_sv_req_r, r), atomic_store(&g_sv_req_step, step), atomic_store(&g_sv_req, kind);
		return kind == 1 ? "vault grid" : "vault edges";
	}
	return "vault on|off|scan [CM]|grid [R] [STEP]|edges [R] [STEP]";
}

/* the body's horizontal speed (LOCO's read-back: what the root really did) */
static float jp_speed(void)
{
	if (!lo_ours_now())
		return 0;
	sv_vec3 v = g_lo.s.v_hand;
	float s = sqrtf(v.x * v.x + v.z * v.z);
	return isfinite(s) ? s : 0;
}

/* why the jump or the vault cannot start now (NULL = it can) */
static const char *jp_why_not(double t)
{
	if (!g_lo.on || !lo_ours_now())
		return "LOCO does not own Talion's ticks";
	if (g_cl.on && (g_cl.s.active || g_cl.s.phase != SOMVR_CLIMB_PH_NONE))
		return "a climb";
	if (g_cl.on && g_cl.dm_show)
		return "the drop's ring is up";
	if (fb_third())
		return "the view is third person";
	if (g_cr.mode != CR_OFF && g_cr.held)
		return "crouched (the stealth crouch)";
	if (fi_holds())
		return "a finisher";
	if (sk_away())
		return "the Shadow Strike";
	if (g_tk.on && g_tk.active)
		return "an enemy's sync holds Talion";
	if (g_step.run.kind != SOMVR_ST_NONE)
		return "a dodge or a phase runs";
	if (g_ws.on && g_ws.s.phase != SOMVR_WSP_IDLE)
		return "the wraith step aims";
	if (game_paused())
		return "paused";
	if (vm_open_recent()) /* build 27 (re-xr-pipeline.md 112.3): A at the card or the panel is theirs */
		return "the VR menu or the controls card is up";
	if (!pr_down_ok())
		return "a blink runs";
	if (g_jp.s.kind != SOMVR_JPK_NONE)
		return "in the air";
	(void)t;
	return NULL;
}

/* A's press (ab_tick, the input tick) when the strike and the dodge have not taken it: ours or the climb's */
static void jp_a_press(int world, double t)
{
	g_jp.a_owned = 0, g_jp.climb_first = 0;
	if (!world || (!g_jp.cfg.jump && !g_jp.cfg.vault))
		return;
	if (jp_air()) { /* a press in the air is ours and does nothing (no drop's ring over an edge mid-jump) */
		g_jp.a_owned = 1;
		if (jp_log_ok())
			logf_("JUMP t=%.3f press in the air (%s, %.2f s): nothing", t, somvr_jp_kind_name(g_jp.s.kind), g_jp.s.t);
		return;
	}
	const char *why = jp_why_not(t);
	somvr_jp_a_in in = { .jump = g_jp.cfg.jump, .vault = g_jp.cfg.vault, .ring = g_jp.ring, .speed = jp_speed(),
			     .can = !why, .climbing = g_cl.on && (g_cl.s.active || g_cl.s.phase != SOMVR_CLIMB_PH_NONE) };
	/* the climb's A on foot (the descent's edge look) can take this press: on, allowed, no hand taken */
	in.descent = g_cl.on && g_cl.cfg.descent && !g_cl.why_not &&
		     !(bo_out() || sl_nograb() || vb_nograb() || df_keep_sword(t));
	int a = somvr_jp_a_pick(&g_jp.cfg, &in);
	g_jp.presses++, g_jp.press_why = a, g_jp.press_speed = in.speed, g_jp.press_t = t;
	if (a == SOMVR_JPA_VAULT || a == SOMVR_JPA_JUMP) {
		g_jp.a_owned = 1, g_jp.press = a == SOMVR_JPA_VAULT ? SOMVR_JPK_VAULT : SOMVR_JPK_JUMP;
	} else if (a == SOMVR_JPA_CLIMB_FIRST) {
		g_jp.climb_first = 1, g_jp.climb_first_t = t, g_jp.climb_firsts++;
	}
	if (jp_log_ok())
		logf_("JUMP t=%.3f press: %s (the body %.0f cm/s%s%s%s%s)", t, somvr_jp_a_name(a), in.speed,
		      g_jp.ring ? ", the vault's ring shown" : "", why ? "; cannot: " : "", why ? why : "",
		      a == SOMVR_JPA_CLIMB_FIRST ? "; standing: the descent's edge first" : "");
	if (why && a == SOMVR_JPA_CLIMB && in.jump && !in.climbing)
		g_jp.refused++;
}
static void jp_a_release(void) { g_jp.a_owned = 0; }

/* cl_frame: the climb's A on foot found no edge (or a short drop): nothing of its own - the press is the jump */
static void jp_climb_passed(double t)
{
	if (!g_jp.climb_first)
		return;
	g_jp.climb_first = 0;
	if (t - g_jp.climb_first_t > JP_PASS_S || jp_why_not(t)) {
		g_jp.climb_kept++;
		return;
	}
	g_jp.climb_passes++, g_jp.press = SOMVR_JPK_JUMP, g_jp.a_owned = 1;
	if (jp_log_ok())
		logf_("JUMP t=%.3f the climb found no edge to drop from (%.0f ms after the press): the jump", t,
		      (t - g_jp.climb_first_t) * 1e3);
}

/* the proxy's gravity flag on the live PlayerMovement pm (its vtable checked); the value before, -1 = not written */
static int jp_grav(uintptr_t pm, int on)
{
	uintptr_t pc = pm ? peek_ptr(pm + CL_PM_PROXY) : 0;
	unsigned char v = 0xff;
	if (!pc || vptr_of((void *)pc) != g_cl.vt_pc || !peek(pc + CL_PC_GRAV, &v, 1))
		return -1;
	if (v != (on ? 1 : 0))
		*(volatile unsigned char *)(pc + CL_PC_GRAV) = on ? 1 : 0, g_jp.grav_writes++;
	g_jp.pc = pc;
	return v;
}
/* the game's gravity (the proxy's pc+0x58, m/s^2, -20 in the world: P7), cm/s^2; else the default */
static float jp_gravity(uintptr_t pm)
{
	uintptr_t pc = pm ? peek_ptr(pm + CL_PM_PROXY) : 0;
	float g = 0;
	if (pc && vptr_of((void *)pc) == g_cl.vt_pc && peek(pc + 0x58, &g, sizeof g) && isfinite(g) && g < -5 && g > -60)
		return -100.0f * g;
	return g_jp.cfg.g;
}

/* the floor under the capsule: five rays down (the axis and 25 cm round), the highest; the feet's height on it */
static int jp_floor(const float root[3], float *fy)
{
	static const float off[5][2] = { { 0, 0 }, { 25, 0 }, { -25, 0 }, { 0, 25 }, { 0, -25 } };
	int ok = 0;
	float best = -1e9f;
	for (int i = 0; i < 5; i++) {
		somvr_climb_hit h;
		memset(&h, 0, sizeof h);
		sv_vec3 f = { root[0] + off[i][0], root[1] + 20.0f, root[2] + off[i][1] };
		if (cl_ray(NULL, -1, f, (sv_vec3){ 0, -1, 0 }, 600.0f, &h) && h.hit && !h.chr && h.point.y > best)
			best = h.point.y, ok = 1;
	}
	*fy = best;
	return ok;
}

static void jp_end_line(double t, const float root[3])
{
	const somvr_jp *s = &g_jp.s;
	if (!jp_log_ok())
		return;
	float dx = root[0] - g_jp.root0.x, dz = root[2] - g_jp.root0.z;
	char off[96] = "";
	if (g_jp.press == SOMVR_JPK_VAULT || s->vh.x != 0 || s->vh.z != 0 || (s->land.x != 0 || s->land.z != 0)) {
		float lx = root[0] - s->land.x, lz = root[2] - s->land.z, lo = sqrtf(lx * lx + lz * lz);
		if (s->n_vault && (s->land.x != 0 || s->land.z != 0)) {
			g_jp.land_off_max = fmaxf(g_jp.land_off_max, lo);
			snprintf(off, sizeof off, "; %.1f cm from the ring", lo);
		}
	}
	logf_("JUMP t=%.3f end: %s after %.3f s in the air: the root %.1f,%.1f,%.1f -> %.1f,%.1f,%.1f (%.0f cm along the floor, "
	      "%+.1f cm; the highest %+.1f cm); the landing %s (from %.0f cm/s)%s; the largest step a tick %.1f cm, of the eyes a "
	      "frame %.1f cm; the view's yaw moved %.2f deg; gravity flag %s; the rise via %s", t, somvr_jp_end_name(s->end), s->t,
	      g_jp.root0.x, g_jp.root0.y, g_jp.root0.z, root[0], root[1], root[2], sqrtf(dx * dx + dz * dz),
	      root[1] - g_jp.root0.y, s->apex_y - g_jp.root0.y, s->brake_v > 0 ? "braked" : s->end == SOMVR_JPE_LANDED ? "hard" : "-",
	      s->v_land, off, g_jp.step_max_cm, g_jp.eye_step_max_cm, g_jp.yaw_dev_max,
	      s->end == SOMVR_JPE_CLIMB ? "left to the climb" : g_jp.grav_off ? "0 -> 1" : "untouched", g_jp_via_w[g_jp.via]);
}

/* the PM tick (lo_pm_pre, after st_pm): the take-off, the arc's tick, the end */
static void jp_pm(uintptr_t pm, const somvr_loco_in *in, somvr_loco_out *o, const float *root, int have_root)
{
	double t = cb_now();
	int rq;
	if ((rq = atomic_exchange(&g_jp.req_jump, -1)) >= 0)
		g_jp.cfg.jump = rq, logf_("JUMP t=%.3f the jump %s (the word)", t, rq ? "on" : "off");
	if ((rq = atomic_exchange(&g_jp.req_vault, -1)) >= 0)
		g_jp.cfg.vault = rq, g_jp.ring = rq ? g_jp.ring : 0, logf_("JUMP t=%.3f the vault %s (the word)", t, rq ? "on" : "off");
	if ((rq = atomic_exchange(&g_jp.req_via, -1)) >= 0)
		g_jp.via = rq, logf_("JUMP t=%.3f the vertical step via %s (the word)", t, g_jp_via_w[rq]);
	if ((rq = atomic_exchange(&g_jp.req_cm, -1)) >= 0)
		g_jp.cfg.jump_cm = (float)rq, logf_("JUMP t=%.3f the jump's rise %d cm (the word)", t, rq);
	if ((rq = atomic_exchange(&g_jp.req_trace_ms, 0)) > 0)
		g_jp.trace_until = t + rq * 1e-3, logf_("JUMP t=%.3f trace %.1f s: the root per tick (the word)", t, rq * 1e-3);
	if ((rq = atomic_exchange(&g_jp.req_stock, -1)) >= 0 && rq != g_jp.stock) { /* the game's own A, for measuring it */
		if (rq) /* (the climb's A on foot off too: the descent's ring would take the press at an edge) */
			g_jp.stock_gm = g_inp.cfg.game_moves, g_jp.stock_latch = g_lo.cfg.latch, g_inp.cfg.game_moves = 1,
			g_lo.cfg.latch = SOMVR_LOCO_LATCH_GAME, g_jp.cfg.jump = g_jp.cfg.vault = 0,
			g_jp.stock_descent = g_cl.cfg.descent, g_jp.stock_mark = g_cl.cfg.drop_mark, g_cl.cfg.descent = 0,
			g_cl.cfg.drop_mark = 0;
		else
			g_inp.cfg.game_moves = g_jp.stock_gm, g_lo.cfg.latch = g_jp.stock_latch, g_jp.cfg.jump = g_jp.want_jump,
			g_jp.cfg.vault = g_jp.want_vault, g_cl.cfg.descent = g_jp.stock_descent, g_cl.cfg.drop_mark = g_jp.stock_mark;
		g_jp.stock = rq;
		logf_("JUMP t=%.3f stock %s: %s (the word)", t, rq ? "on" : "off",
		      rq ? "A = the game's own (Jump, FreeRun), the sprint latch = the game's free-run; our jump and vault off"
			 : "back: A's game moves and the latch as they were, our jump and vault as switched");
	}
	if (g_jp.trace_until > 0 && t < g_jp.trace_until && have_root && jp_tick_ok()) {
		unsigned char fsm = 0, vm = 0;
		uintptr_t pl = peek_ptr(pm + PM_OWNER);
		float v[3] = { 0, 0, 0 };
		peek(pl + LO_P_FSM, &fsm, 1), peek(pm + LO_PM_VELMODE, &vm, 1), peek(pm + 0xe0, v, sizeof v);
		logf_("JUMP trace t=%.4f root %.1f,%.1f,%.1f fsm %d mode %d v %.0f,%.0f,%.0f LOCO %s", t, root[0], root[1], root[2],
		      fsm, vm, v[0], v[1], v[2], o->owned ? "ours" : "the game's");
	}
	if (my_tid() != g_inp.tid) {
		g_jp.other_tid++;
		return;
	}
	if (g_jp.press && g_jp.s.kind == SOMVR_JPK_NONE) { /* the take-off */
		int kind = g_jp.press;
		g_jp.press = 0;
		const char *why = jp_why_not(t);
		float fy = 0;
		int fok = have_root && jp_floor(root, &fy);
		if (!why && !o->owned)
			why = "LOCO does not own this tick";
		if (!why && !fok)
			why = "no floor under him";
		if (!why && root[1] - g_jp.cfg.foot_cm - fy > g_jp.cfg.ground_cm)
			why = "not on the ground";
		if (!why && t - g_jp.press_t > 0.3)
			why = "the press is old";
		if (why) {
			g_jp.refused++;
			if (t - g_jp.press_t <= 0.3) /* the soft "no" (not for a press long gone) */
				in_haptic_double(SOMVR_HAND_R, 0.2f, 0.02f, 0.07f, SOMVR_HAP_UI);
			if (jp_log_ok())
				logf_("JUMP t=%.3f refused: %s (the feet %.0f cm over the floor %.1f): a soft double buzz, nothing moves", t,
				      why, fok ? root[1] - g_jp.cfg.foot_cm - fy : -1.0f, fy);
		} else {
			float g = jp_gravity(pm);
			sv_vec3 r = { root[0], root[1], root[2] };
			somvr_jp_vplan p;
			int vault = 0;
			if (kind == SOMVR_JPK_VAULT) { /* the plan again from here, now */
				sv_vec3 d = g_jp.ring_dir;
				vault = somvr_jp_vault_find(&g_jp.cfg, cl_ray, NULL, r, fy, d, jp_speed(), g, &p);
				if (!vault)
					g_jp.vault_fallbacks++;
			}
			g_jp.g = g, g_jp.t0 = t, g_jp.root0 = r, g_jp.step_max_cm = 0, g_jp.eye_step_max_cm = 0;
			g_jp.yaw0 = g_fr.base.yaw, g_jp.yaw_dev_max = 0, g_jp.have_eye = 0, g_jp.pm = pm, g_jp.grav_off = 0;
			if (vault) {
				somvr_jp_start(&g_jp.s, SOMVR_JPK_VAULT, r, fy, p.vy0, g, p.vh, p.land, p.t_hold);
				g_jp.plan = p;
			} else {
				somvr_jp_start(&g_jp.s, SOMVR_JPK_JUMP, r, fy, somvr_jp_takeoff(g_jp.cfg.jump_cm, g), g, (sv_vec3){ 0, 0, 0 },
					       (sv_vec3){ 0, 0, 0 }, 0);
			}
			g_jp.takeoffs++, g_jp.ring = 0;
			in_haptic_event(SOMVR_HAND_R, 0.15f, 0.02f, SOMVR_HAP_UI);
			if (jp_log_ok()) {
				if (vault)
					logf_("JUMP t=%.3f go: the vault %s the %.0f cm top (%.0f cm deep, its face %.0f cm ahead) at %.0f cm/s: up "
					      "%.0f cm/s to the feet %.0f cm over the floor, level until %.2f s, down onto the ring at "
					      "%.0f,%.0f,%.0f (%.0f cm along the floor, %.2f s; the least clearance %.1f cm; %d casts); gravity %.0f "
					      "cm/s^2", t, p.kind == SOMVR_JPO_ONTO ? "onto" : "over", p.top_cm, p.depth_cm, p.face_cm,
					      sqrtf(p.vh.x * p.vh.x + p.vh.z * p.vh.z), p.vy0, p.apex_cm, p.t_hold, p.land.x, p.land.y,
					      p.land.z, hypotf(p.land.x - r.x, p.land.z - r.z), p.T, p.clear_cm, p.rays, g);
				else
					logf_("JUMP t=%.3f go: the jump%s: %.0f cm/s up (%.0f cm at %.0f cm/s^2), the stick's %.0f cm/s carried; the "
					      "root %.1f,%.1f,%.1f, the feet %.1f cm over the floor %.1f", t,
					      kind == SOMVR_JPK_VAULT ? " (the vault's plan gone: " : "",
					      somvr_jp_takeoff(g_jp.cfg.jump_cm, g), g_jp.cfg.jump_cm, g, jp_speed(), r.x, r.y, r.z,
					      r.y - g_jp.cfg.foot_cm - fy, fy);
				if (kind == SOMVR_JPK_VAULT && !vault)
					logf_("JUMP t=%.3f (the vault at the press: %s)", t, somvr_jp_vwhy_name(p.why));
			}
		}
	}
	if (g_jp.s.kind == SOMVR_JPK_NONE)
		return;
	somvr_jp_in ji = { .dt = in->dt, .owned = o->owned, .climbing = g_cl.on && g_cl.s.active, .have_root = have_root };
	if (have_root) {
		ji.root = (sv_vec3){ root[0], root[1], root[2] };
		ji.floor_ok = jp_floor(root, &ji.floor_y);
		/* moving: also the floor where this tick and the next take him (uphill the capsule meets the rising ground
		 * ahead before the rays under him see it: run 003247's running jump stopped hard at 230 cm/s) */
		float ax = o->delta.x * 2.0f, az = o->delta.z * 2.0f, fa;
		if (g_jp.s.kind == SOMVR_JPK_VAULT)
			ax = g_jp.s.vh.x * ji.dt * 2.0f, az = g_jp.s.vh.z * ji.dt * 2.0f;
		if (ax * ax + az * az > 4.0f) {
			float ra[3] = { root[0] + ax, root[1], root[2] + az };
			if (jp_floor(ra, &fa) && (!ji.floor_ok || fa > ji.floor_y))
				ji.floor_y = fa, ji.floor_ok = 1;
		}
		g_jp.floor_ok = ji.floor_ok, g_jp.floor_y = ji.floor_y;
	}
	somvr_jp_out jo;
	int kind = g_jp.s.kind;
	somvr_jp_step(&g_jp.s, &g_jp.cfg, &ji, &jo);
	g_jp.ticks++;
	if (jo.ceiling && jp_log_ok())
		logf_("JUMP t=%.3f a ceiling over the head after %.3f s (the rise blocked): the fall from here", t, g_jp.s.t);
	if (jo.ended) {
		if (g_jp.s.end != SOMVR_JPE_CLIMB && g_jp.grav_off) { /* gravity back (the climb keeps it off itself) */
			jp_grav(g_jp.pm, 1);
		}
		g_jp.view_until = t + 0.3;
		jp_end_line(t, root);
		g_jp.grav_off = 0;
		return;
	}
	if (!jo.active)
		return;
	int v = jp_grav(pm, 0); /* off for the arc's tick (an attack's own flag may set it again) */
	if (v == 1 && g_jp.grav_off)
		g_jp.grav_resets++;
	g_jp.grav_off |= v >= 0;
	if (g_jp.via == JP_VIA_EXTRA && pm_at_rest(pm)) { /* the climb's one-shot extra move (Talion's frame: y is up) */
		float loc[3] = { 0, jo.dy, 0 };
		memcpy((void *)(pm + PM_XPOS), loc, sizeof loc);
		*(volatile unsigned char *)(pm + PM_PENDING) = 1;
		g_jp.extra_pending = 1;
	} else {
		o->delta.y = jo.dy;
		if (g_jp.via == JP_VIA_SETPOS && jo.dy > 0) /* the rise: the proxy set up by it after vf_11 (the fall collides) */
			g_jp.post_dy = jo.dy;
	}
	float dt = in->dt > 1e-4f ? in->dt : 1.0f / 90;
	if (jo.fixed_h) { /* the vault: its horizontal instead of the stick's */
		o->delta.x = jo.dh.x, o->delta.z = jo.dh.z;
		o->v.x = jo.dh.x / dt, o->v.z = jo.dh.z / dt;
		g_lo.s.asked_last.x = jo.dh.x, g_lo.s.asked_last.z = jo.dh.z;
	}
	float st = sqrtf(o->delta.x * o->delta.x + jo.dy * jo.dy + o->delta.z * o->delta.z);
	g_jp.step_max_cm = fmaxf(g_jp.step_max_cm, st);
	if (jp_tick_ok())
		logf_("JUMP tick t=%.4f %s %.3f s: dy %+.2f vy %+.0f%s%s; h %+.2f,%+.2f; root %.1f,%.1f,%.1f (%+.1f), the floor %.1f "
		      "(feet %+.1f)", t, somvr_jp_kind_name(kind), g_jp.s.t, jo.dy, jo.vy, jo.braking ? " braking" : "",
		      g_jp.s.settling ? " settling" : g_jp.s.holding && g_jp.s.vy == 0 ? " level" : "", o->delta.x, o->delta.z,
		      root[0], root[1], root[2], root[1] - g_jp.root0.y, ji.floor_y, root[1] - g_jp.cfg.foot_cm - ji.floor_y);
	(void)kind;
}

/* the PM hook after vf_11 (lo_pm_post): an extra move not taken is cleared; JP_VIA_SETPOS: the proxy up by this tick's
 * rise (vf_11's transform took it from our delta; the step that follows then has only the level part to move with its
 * collision: cl_thr_set's way), unless a ray up from the head's top meets something within it (a ceiling: the rise up
 * to it; the read-back sees the rest blocked) */
static void jp_pm_post(uintptr_t pm, const float *xf)
{
	(void)xf;
	if (g_jp.extra_pending) {
		g_jp.extra_pending = 0;
		if (*(volatile unsigned char *)(pm + PM_PENDING)) {
			memset((void *)(pm + PM_XPOS), 0, 3 * sizeof(float));
			*(volatile unsigned char *)(pm + PM_PENDING) = 0;
			g_jp.extra_missed++;
		} else {
			g_jp.extra_taken++;
		}
	}
	float dy = g_jp.post_dy;
	g_jp.post_dy = 0;
	if (!(dy > 0) || !g_lo.fired)
		return;
	uintptr_t pc = peek_ptr(pm + CL_PM_PROXY);
	float p[3];
	if (!un_slots(pc))
		return;
	CL_FN(CLS_PC_GETPOS, un_pos_fn)(pc, p);
	if (!vec_ok(p, 1e7f))
		return;
	somvr_climb_hit h;
	memset(&h, 0, sizeof h);
	float top = g_jp.cfg.foot_cm + 5.0f; /* the head's top over the root (the capsule's 195 cm over the feet) */
	if (cl_ray(NULL, -1, (sv_vec3){ p[0], p[1] + top - 30.0f, p[2] }, (sv_vec3){ 0, 1, 0 }, 30.0f + dy + 8.0f, &h) && h.hit &&
	    !h.chr) {
		float room = h.dist - 30.0f - 8.0f;
		dy = room > 0 ? fminf(dy, room) : 0, g_jp.setpos_ceil++;
	}
	if (dy > 0) {
		p[1] += dy;
		CL_FN(CLS_PC_SETPOS, un_pos_fn)(pc, p);
		g_jp.setpos_n++;
	}
}

/* the vault's ring on foot: the plan along the body's way (moving) or the head's heading (standing), every 0.1 s */
static void jv_find(double t, uintptr_t pl)
{
	uintptr_t pm = 0, obj = 0;
	float r[3], fy;
	if (!pl || !player_objs(pl, &pm, &obj) || !peek(obj + OBJ_POS, r, sizeof r) || !vec_ok(r, 1e7f) || !jp_floor(r, &fy) ||
	    r[1] - g_jp.cfg.foot_cm - fy > g_jp.cfg.ground_cm) {
		g_jp.ring = 0;
		return;
	}
	float sp = jp_speed();
	sv_vec3 d;
	if (sp >= 50) {
		sv_vec3 v = g_lo.s.v_hand;
		d = (sv_vec3){ v.x / sp, 0, v.z / sp };
	} else {
		d = (sv_vec3){ sinf(g_inp.hv), 0, cosf(g_inp.hv) };
	}
	sv_vec3 root = { r[0], r[1], r[2] };
	somvr_jp_vplan p;
	int ok = somvr_jp_vault_find(&g_jp.cfg, cl_ray, NULL, root, fy, d, sp, jp_gravity(pm), &p);
	g_jp.finds++, g_jp.find_rays += (uint64_t)p.rays;
	if (ok) {
		int was = g_jp.ring;
		g_jp.ring = 1, g_jp.ring_t = t, g_jp.plan = p, g_jp.ring_dir = d;
		if (!was) {
			g_jp.ring_shows++, g_jp.ring_first_t = t;
			in_haptic_event(SOMVR_HAND_R, 0.15f, 0.012f, SOMVR_HAP_UI); /* the mantle's light tick */
			if (jp_log_ok())
				logf_("VAULT t=%.3f ring: %s the %.0f cm top %.0f cm ahead (%.0f cm deep) %s: the ring at %.0f,%.0f,%.0f, %.0f cm "
				      "along the floor (%+.0f cm); A = %.0f cm/s, the feet %.0f cm over the floor, %.2f s; %d casts", t,
				      p.kind == SOMVR_JPO_ONTO ? "onto" : "over", p.top_cm, p.face_cm, p.depth_cm,
				      sp >= 50 ? "along the way he moves" : "along the gaze", p.land.x, p.land.y, p.land.z,
				      hypotf(p.land.x - root.x, p.land.z - root.z), p.land.y - fy, sqrtf(p.vh.x * p.vh.x + p.vh.z * p.vh.z),
				      p.apex_cm, p.T, p.rays);
		}
	} else if (g_jp.ring && t - g_jp.ring_t > JP_RING_KEEP_S) {
		g_jp.ring = 0;
		if (jp_log_ok())
			logf_("VAULT t=%.3f no ring now (%s) after %.2f s", t, somvr_jp_vwhy_name(p.why), t - g_jp.ring_first_t);
	}
	if (p.why != g_jp.last_why && !ok && jp_log_ok() && g_jp.lines < 600)
		logf_("VAULT t=%.3f ahead: %s (the top %.0f cm, %.0f cm ahead, %.0f cm deep)", t, somvr_jp_vwhy_name(p.why), p.top_cm,
		      p.face_cm, p.depth_cm);
	g_jp.last_why = p.why;
}

/* `vault scan [CM]`: the plan in 24 directions from here, the face within CM (a test aid: where the obstacles are) */
static void jv_scan(double t, uintptr_t pl, int cm)
{
	uintptr_t pm = 0, obj = 0;
	float r[3], fy;
	if (!pl || !player_objs(pl, &pm, &obj) || !peek(obj + OBJ_POS, r, sizeof r) || !vec_ok(r, 1e7f) || !jp_floor(r, &fy)) {
		logf_("VAULT scan t=%.3f: no player or no floor", t);
		return;
	}
	somvr_jp_cfg c = g_jp.cfg;
	c.v_reach_cm = c.v_reach_max_cm = (float)cm;
	int n[SOMVR_JPV_N] = { 0 };
	char buf[1400] = "", fr[200] = "";
	size_t k = 0, kf = 0;
	for (int i = 0; i < 24; i++) {
		float a = i * 15.0f * 0.01745329f;
		somvr_jp_vplan p;
		sv_vec3 root = { r[0], r[1], r[2] };
		int ok = somvr_jp_vault_find(&c, cl_ray, NULL, root, fy, (sv_vec3){ sinf(a), 0, cosf(a) }, 0, jp_gravity(pm), &p);
		n[p.why]++;
		if (p.why == SOMVR_JPV_NONE && kf + 6 < sizeof fr)
			kf += (size_t)snprintf(fr + kf, sizeof fr - kf, " %d", i * 15);
		if ((ok || p.why == SOMVR_JPV_HIGH || p.why == SOMVR_JPV_NOFLOOR || p.why == SOMVR_JPV_NOWAY ||
		     p.why == SOMVR_JPV_NOROOM || p.why == SOMVR_JPV_NOARC) && k + 120 < sizeof buf)
			k += (size_t)snprintf(buf + k, sizeof buf - k, " | %d deg: %s %.0f cm at %.0f%s", i * 15,
					      ok ? (p.kind == SOMVR_JPO_ONTO ? "ONTO" : "OVER") : somvr_jp_vwhy_name(p.why), p.top_cm,
					      p.face_cm, ok ? "" : "");
	}
	logf_("VAULT scan t=%.3f from %.0f,%.0f,%.0f (the floor %.0f; the heading 0 = +z, 90 = +x; the head's %.0f; faces within "
	      "%d cm): ok %d, nothing %d, steps %d, too high %d, no top %d, no landing %d, no room %d, no way %d, no arc %d; "
	      "free:%s%s", t, r[0], r[1], r[2], fy, g_inp.hv * 57.29578f, cm, n[0], n[SOMVR_JPV_NONE], n[SOMVR_JPV_LOW],
	      n[SOMVR_JPV_HIGH], n[SOMVR_JPV_NOTOP], n[SOMVR_JPV_NOFLOOR], n[SOMVR_JPV_NOROOM], n[SOMVR_JPV_NOWAY],
	      n[SOMVR_JPV_NOARC], fr, buf);
}

/* build 24 (som-b24-move, docs/re-xr-pipeline.md 101): `vault grid [R] [STEP]` and `vault edges [R] [STEP]` - test aids
 * that survey the ground round Talion for the real places the moves need, a few points a frame (the casts spread out):
 *   grid  every STEP cm within R: the floor there (a ray down), then the vault's own plan (somvr_jp_vault_find, standing)
 *         in 8 headings - each obstacle found once (its face point), `VAULT grid find ...`; walls 121-160 cm counted;
 *   edges every STEP cm within R: the top there, in 8 headings a drop of 150+ cm within 60 cm (its edge found by 5 cm
 *         rays), the face under it from 150 cm out at 20/40/60/100/150 cm under the top: an OVERHANG (the face 15+ cm
 *         behind the lip within 60 cm under it, as the climb's am_oh_test), a WALL (the face within 30 cm of the lip all
 *         the way: down over's), else a SLOPE; the drop under it, the room on the top; `VAULT edge ...`.
 * Each find once (within 150 cm of one already logged, the same way); at most SV_MAX. The answers go to the driver. */
#define SV_MAX 48
#define SV_PTS_FRAME 16   /* at most this many points a frame ... */
#define SV_CASTS_FRAME 500 /* ... and about this many casts */
static struct {
	int kind;              /* 0 none, 1 grid, 2 edges */
	float cx, cy, cz;      /* the centre (Talion's root at the word) */
	int r, step, side, i;  /* the radius, the spacing, the points a side, the cursor */
	double t0;
	int pts, nf, dup, held;
	sv_vec3 fp[SV_MAX], fu[SV_MAX];
	int why[SOMVR_JPV_N], high_near;
	int ek[4];             /* edges: overhangs, walls, slopes, inside */
	uint64_t casts0;
} g_sv;
static int g_sv_lines;
static int sv_log_ok(void) { return g_sv_lines++ < 600; }

static int sv_cast(sv_vec3 from, sv_vec3 dir, float len, somvr_climb_hit *h)
{
	memset(h, 0, sizeof *h);
	return cl_ray(NULL, -1, from, dir, len, h);
}
static int sv_seen(sv_vec3 p, sv_vec3 u)
{
	for (int j = 0; j < g_sv.nf; j++)
		if (hypotf(p.x - g_sv.fp[j].x, p.z - g_sv.fp[j].z) < 150.0f && fabsf(p.y - g_sv.fp[j].y) < 100.0f &&
		    u.x * g_sv.fu[j].x + u.z * g_sv.fu[j].z > 0.7f)
			return 1;
	return 0;
}
static void sv_keep(sv_vec3 p, sv_vec3 u)
{
	if (g_sv.nf < SV_MAX)
		g_sv.fp[g_sv.nf] = p, g_sv.fu[g_sv.nf] = u, g_sv.nf++;
}
/* the floor (or top) at x,z: the first hit down from 4 m over the centre's root; 0 = none or a body */
static int sv_floor(float x, float z, float *fy)
{
	somvr_climb_hit h;
	if (!sv_cast((sv_vec3){ x, g_sv.cy + 400.0f, z }, (sv_vec3){ 0, -1, 0 }, 1400.0f, &h) || !h.hit || h.chr)
		return 0;
	*fy = h.point.y;
	return 1;
}
static void sv_point_grid(double t, uintptr_t pm, float x, float z)
{
	float fy;
	somvr_climb_hit h;
	if (!sv_floor(x, z, &fy))
		return;
	if (sv_cast((sv_vec3){ x, fy + 10.0f, z }, (sv_vec3){ 0, 1, 0 }, 180.0f, &h) && h.hit) { /* no room to stand */
		g_sv.held++;
		return;
	}
	somvr_jp_cfg c = g_jp.cfg;
	c.v_reach_cm = c.v_reach_max_cm = 0.5f * (float)g_sv.step + 100.0f;
	sv_vec3 root = { x, fy + c.foot_cm, z };
	for (int k = 0; k < 8; k++) {
		float a = k * 45.0f * 0.01745329f;
		sv_vec3 d = { sinf(a), 0, cosf(a) };
		somvr_jp_vplan p;
		int ok = somvr_jp_vault_find(&c, cl_ray, NULL, root, fy, d, 0, jp_gravity(pm), &p);
		g_sv.why[p.why]++;
		if (!ok && p.why == SOMVR_JPV_HIGH && p.top_cm > c.v_max_cm && p.top_cm <= 160.0f)
			g_sv.high_near++;
		if (!ok && !(p.why == SOMVR_JPV_HIGH && p.top_cm > c.v_max_cm && p.top_cm <= 160.0f))
			continue;
		sv_vec3 f = { x + d.x * p.face_cm, fy, z + d.z * p.face_cm };
		if (sv_seen(f, d)) {
			g_sv.dup++;
			continue;
		}
		sv_keep(f, d);
		if (sv_log_ok())
			logf_("VAULT grid t=%.3f %s: from %.0f %.0f %.0f heading %d: %s the %.0f cm top %.0f cm ahead (%.0f deep), the "
			      "face at %.0f,%.0f; %s%.0f,%.0f,%.0f (%+.0f cm)", t, ok ? "find" : "near", x, root.y, z, k * 45,
			      ok ? (p.kind == SOMVR_JPO_ONTO ? "ONTO" : "OVER") : "too high:", p.top_cm, p.face_cm, p.depth_cm, f.x,
			      f.z, ok ? "the ring at " : "-", ok ? p.land.x : 0, ok ? p.land.y : 0, ok ? p.land.z : 0,
			      ok ? p.land.y - fy : 0);
	}
}
/* the face's offset out along u from the edge (cm, + = out past the lip) at y: a ray in from 150 cm out; -60 = none */
static float sv_face(sv_vec3 e, sv_vec3 u, float y)
{
	somvr_climb_hit h;
	sv_vec3 f = { e.x + u.x * 150.0f, y, e.z + u.z * 150.0f };
	if (!sv_cast(f, (sv_vec3){ -u.x, 0, -u.z }, 210.0f, &h) || !h.hit)
		return -60.0f;
	return 150.0f - h.dist;
}
static void sv_point_edges(double t, float x, float z)
{
	float ty;
	somvr_climb_hit h;
	if (!sv_floor(x, z, &ty))
		return;
	if (sv_cast((sv_vec3){ x, ty + 10.0f, z }, (sv_vec3){ 0, 1, 0 }, 180.0f, &h) && h.hit) {
		g_sv.ek[3]++;
		return;
	}
	for (int k = 0; k < 8; k++) {
		float a = k * 45.0f * 0.01745329f;
		sv_vec3 u = { sinf(a), 0, cosf(a) };
		/* a drop within 60 cm: nothing 150 cm under the top 60 cm out */
		if (!sv_cast((sv_vec3){ x + u.x * 60.0f, ty + 30.0f, z + u.z * 60.0f }, (sv_vec3){ 0, -1, 0 }, 180.0f, &h) || h.hit)
			continue;
		float last = 0;
		for (float d = 5; d <= 60; d += 5) {
			if (!sv_cast((sv_vec3){ x + u.x * d, ty + 30.0f, z + u.z * d }, (sv_vec3){ 0, -1, 0 }, 55.0f, &h))
				return;
			if (!h.hit || fabsf(h.point.y - ty) > 25.0f)
				break;
			last = d;
		}
		sv_vec3 e = { x + u.x * (last + 2.5f), ty, z + u.z * (last + 2.5f) };
		if (sv_seen(e, u)) {
			g_sv.dup++;
			continue;
		}
		static const float dep[5] = { 20, 40, 60, 100, 150 };
		float fo[5], fo_max = -1e9f, fo_min = 1e9f, under = 1e9f;
		for (int j = 0; j < 5; j++) {
			fo[j] = sv_face(e, u, ty - dep[j]);
			fo_max = fmaxf(fo_max, fo[j]), fo_min = fminf(fo_min, fo[j]);
			if (j < 3)
				under = fminf(under, fo[j]);
		}
		/* the floor under it, past the face */
		float out = fmaxf(fo_max, 0) + 50.0f, drop = -1;
		if (sv_cast((sv_vec3){ e.x + u.x * out, ty - 10.0f, e.z + u.z * out }, (sv_vec3){ 0, -1, 0 }, 3000.0f, &h) && h.hit)
			drop = ty - h.point.y;
		/* the top's room (its own point had it) and a landing 40 cm in from the lip */
		int room = !(sv_cast((sv_vec3){ e.x - u.x * 40.0f, ty + 10.0f, e.z - u.z * 40.0f }, (sv_vec3){ 0, 1, 0 }, 180.0f, &h) &&
			     h.hit);
		int kind = under <= -15.0f ? 0 : (fo_max - fminf(fo_min, 0) <= 30.0f && fo_max <= 30.0f) ? 1 : 2;
		g_sv.ek[kind]++;
		sv_keep(e, u);
		if (sv_log_ok())
			logf_("VAULT edge t=%.3f %s: the lip at %.0f %.0f %.0f heading %d (out); the face from the lip at 20/40/60/100/150 "
			      "cm under: %+.0f %+.0f %+.0f %+.0f %+.0f cm; the drop %.0f cm; room on the top %d; stand at %.0f %.0f %.0f",
			      t, kind == 0 ? "OVERHANG" : kind == 1 ? "WALL" : "SLOPE", e.x, e.y, e.z, k * 45, fo[0], fo[1], fo[2],
			      fo[3], fo[4], drop, room, x, ty + g_jp.cfg.foot_cm, z);
	}
}
static void sv_start(double t, uintptr_t pl, int kind, int r, int step)
{
	uintptr_t pm = 0, obj = 0;
	float p[3];
	if (!pl || !player_objs(pl, &pm, &obj) || !peek(obj + OBJ_POS, p, sizeof p) || !vec_ok(p, 1e7f)) {
		logf_("VAULT survey t=%.3f: no player", t);
		return;
	}
	memset(&g_sv, 0, sizeof g_sv);
	g_sv.kind = kind, g_sv.cx = p[0], g_sv.cy = p[1], g_sv.cz = p[2], g_sv.r = r, g_sv.step = step;
	g_sv.side = 2 * (r / step) + 1, g_sv.t0 = t, g_sv.casts0 = g_cl.casts;
	logf_("VAULT survey t=%.3f %s: round %.0f %.0f %.0f, %d cm, every %d cm (%d points a side)", t,
	      kind == 1 ? "grid" : "edges", p[0], p[1], p[2], r, step, g_sv.side);
}
static void sv_frame(double t, uintptr_t pl)
{
	int rq = atomic_exchange(&g_sv_req, 0);
	if (rq)
		sv_start(t, pl, rq, atomic_load(&g_sv_req_r), atomic_load(&g_sv_req_step));
	if (!g_sv.kind)
		return;
	uintptr_t pm = 0, obj = 0;
	if (!player_objs(pl, &pm, &obj))
		return;
	int n = g_sv.side * g_sv.side, done = 0;
	uint64_t c0 = g_cl.casts;
	while (g_sv.i < n && done < SV_PTS_FRAME && g_cl.casts - c0 < SV_CASTS_FRAME) {
		int ix = g_sv.i % g_sv.side, iz = g_sv.i / g_sv.side;
		g_sv.i++;
		float dx = (float)((ix - g_sv.side / 2) * g_sv.step), dz = (float)((iz - g_sv.side / 2) * g_sv.step);
		if (dx * dx + dz * dz > (float)g_sv.r * (float)g_sv.r)
			continue;
		g_sv.pts++, done++;
		if (g_sv.kind == 1)
			sv_point_grid(t, pm, g_sv.cx + dx, g_sv.cz + dz);
		else
			sv_point_edges(t, g_sv.cx + dx, g_sv.cz + dz);
	}
	if (g_sv.i < n)
		return;
	if (g_sv.kind == 1)
		logf_("VAULT survey t=%.3f grid done: %d points in %.1f s (%llu casts), found %d (again %d; no room to stand %d); "
		      "the plans: ok %d, nothing %d, steps %d, too high %d (121-160 cm: %d), no top %d, no landing %d, no room %d, no "
		      "way %d, no arc %d, slopes %d", t, g_sv.pts, t - g_sv.t0, (unsigned long long)(g_cl.casts - g_sv.casts0),
		      g_sv.nf, g_sv.dup, g_sv.held, g_sv.why[0], g_sv.why[SOMVR_JPV_NONE], g_sv.why[SOMVR_JPV_LOW],
		      g_sv.why[SOMVR_JPV_HIGH], g_sv.high_near, g_sv.why[SOMVR_JPV_NOTOP], g_sv.why[SOMVR_JPV_NOFLOOR],
		      g_sv.why[SOMVR_JPV_NOROOM], g_sv.why[SOMVR_JPV_NOWAY], g_sv.why[SOMVR_JPV_NOARC], g_sv.why[SOMVR_JPV_SLOPE]);
	else
		logf_("VAULT survey t=%.3f edges done: %d points in %.1f s (%llu casts): overhangs %d, walls %d, slopes %d (again %d; "
		      "no room to stand %d)", t, g_sv.pts, t - g_sv.t0, (unsigned long long)(g_cl.casts - g_sv.casts0), g_sv.ek[0],
		      g_sv.ek[1], g_sv.ek[2], g_sv.dup, g_sv.ek[3]);
	g_sv.kind = 0;
}

/* H4, a stereo frame (after ws_frame): the scan word, the vault's ring, the view's path while in the air */
static void jv_frame(uintptr_t pl, int stereo)
{
	if ((!g_jp.want_jump && !g_jp.want_vault) || !stereo || !pl)
		return;
	if (my_tid() != g_inp.tid) {
		g_jp.other_tid++;
		return;
	}
	double t = cb_now();
	int scan = atomic_exchange(&g_jp.req_scan, 0);
	if (scan > 0)
		jv_scan(t, pl, scan);
	sv_frame(t, pl);
	if (atomic_exchange(&g_jp.req_state, 0))
		logf_("JUMP state t=%.3f: jump %d vault %d stock %d; %s; the ring %d; in the air %s (%.2f s); the body %.0f cm/s; A ours "
		      "%d; presses %llu, take-offs %llu; the head's heading %.1f; the stick's %.1f", t, g_jp.cfg.jump, g_jp.cfg.vault,
		      g_jp.stock, jp_why_not(t) ? jp_why_not(t) : "can go", g_jp.ring, somvr_jp_kind_name(g_jp.s.kind), g_jp.s.t,
		      jp_speed(), g_jp.a_owned, (unsigned long long)g_jp.presses, (unsigned long long)g_jp.takeoffs,
		      g_inp.hv * 57.29578f, g_inp.hc * 57.29578f);
	if (g_jp.climb_first && t - g_jp.climb_first_t > JP_PASS_S) /* the climb took the press (its edge, its ring) */
		g_jp.climb_first = 0, g_jp.climb_kept++;
	/* the view's path while in the air and just after: the anchor's eye point per frame, the base's yaw */
	if (jp_air() || t < g_jp.view_until) {
		sv_vec3 e = { g_lo.anchor_pos[0], g_lo.anchor_pos[1], g_lo.anchor_pos[2] };
		float st = g_jp.have_eye ? sqrtf((e.x - g_jp.eye_last.x) * (e.x - g_jp.eye_last.x) +
						 (e.y - g_jp.eye_last.y) * (e.y - g_jp.eye_last.y) +
						 (e.z - g_jp.eye_last.z) * (e.z - g_jp.eye_last.z))
					 : 0;
		float yd = fabsf(sv_wrap_pi(g_fr.base.yaw - g_jp.yaw0)) * 57.29578f;
		if (jp_air())
			g_jp.eye_step_max_cm = fmaxf(g_jp.eye_step_max_cm, st), g_jp.yaw_dev_max = fmaxf(g_jp.yaw_dev_max, yd);
		if (jp_tick_ok())
			logf_("JUMP view f=%llu t=%.4f eye %.1f,%.2f,%.1f step %.2f cm (y %+.2f) %s; yaw %+.2f deg from the take-off; %s",
			      (unsigned long long)g_fr.frame, t, e.x, e.y, e.z, st, g_jp.have_eye ? e.y - g_jp.eye_last.y : 0,
			      g_lo.ao.exact_y ? "exact" : "filtered", sv_wrap_pi(g_fr.base.yaw - g_jp.yaw0) * 57.29578f,
			      jp_air() ? somvr_jp_kind_name(g_jp.s.kind) : "landed");
		g_jp.eye_last = e, g_jp.have_eye = 1;
	} else {
		g_jp.have_eye = 0;
	}
	if (!g_jp.cfg.vault || jp_air()) {
		g_jp.ring = jp_air() ? 0 : g_jp.ring;
		return;
	}
	float ap[3];
	if (jp_why_not(t) || th_attacker(t, 3.0, ap) || g_step.fsm == 7) { /* no ring in a fight: A is the dodge's there */
		g_jp.ring = 0;
		return;
	}
	if (t >= g_jp.next_find) {
		g_jp.next_find = t + 0.1;
		jv_find(t, pl);
	}
	if (g_jp.ring)
		g_jp.ring_frames++;
}

/* ob_frame: the vault's ring into this frame's objective entry (1 = published): gold, with the post over it */
static int jv_entry(somvr_objective_frame *e)
{
	if (!g_jp.cfg.vault || !g_jp.ring || jp_air() || game_paused())
		return 0;
	somvr_th_map mp = { { g_fr.base.pos.x, g_fr.base.pos.y, g_fr.base.pos.z }, g_fr.base.yaw - g_rig.yaw0,
			    { g_rig.p0.x, g_rig.p0.y, g_rig.p0.z }, somvr_rig_scale(&g_rig) };
	sv_vec3 l = g_jp.plan.land;
	float w[3] = { l.x, l.y, l.z }, w2[3] = { w[0] + 35.0f, w[1], w[2] }, x2[3], wt[3] = { l.x, l.y + 120.0f, l.z };
	somvr_th_to_xr(&mp, w, e->pos), somvr_th_to_xr(&mp, w2, x2), somvr_th_to_xr(&mp, wt, e->top);
	e->n = 1, e->flags = SOMVR_OBF_LAND | SOMVR_OBF_LINE, e->id = 0;
	e->dist_m = sqrtf((x2[0] - e->pos[0]) * (x2[0] - e->pos[0]) + (x2[1] - e->pos[1]) * (x2[1] - e->pos[1]) +
			  (x2[2] - e->pos[2]) * (x2[2] - e->pos[2])); /* the ring's radius: Talion's capsule's (35 cm) */
	return 1;
}

static void jp_end_log(void)
{
	if (!g_jp.want_jump && !g_jp.want_vault)
		return;
	const somvr_jp *s = &g_jp.s;
	logf_("JUMP END build 22: presses %llu (the climb first %llu: passed %llu, kept %llu), refused %llu, take-offs %llu "
	      "(jumps %llu, vaults %llu; the vault's plan gone at the press %llu; stopped at the ring's distance %llu); ends: landed %llu, lost %llu, caught by the "
	      "climb %llu, the game's fall %llu, stopped %llu; ceilings %llu, brakes %llu (settle misses %llu); the highest rise "
	      "%.1f cm, the longest %.2f s, the fastest landing %.0f cm/s; the vault's rings %llu (%llu frames; %llu finds, %llu "
	      "casts), the farthest from its ring %.1f cm; ticks %llu; gravity writes %llu (the game set it again %llu); the rise "
	      "via %s (extra moves taken %llu, not %llu; the proxy set up %llu, by a ceiling %llu); other thread %llu",
	      (unsigned long long)g_jp.presses, (unsigned long long)g_jp.climb_firsts, (unsigned long long)g_jp.climb_passes,
	      (unsigned long long)g_jp.climb_kept, (unsigned long long)g_jp.refused, (unsigned long long)g_jp.takeoffs,
	      (unsigned long long)s->n_jump, (unsigned long long)s->n_vault, (unsigned long long)g_jp.vault_fallbacks,
	      (unsigned long long)s->n_ring_stop,
	      (unsigned long long)s->n_landed, (unsigned long long)s->n_lost, (unsigned long long)s->n_climb,
	      (unsigned long long)s->n_fall, (unsigned long long)s->n_stop, (unsigned long long)s->n_ceiling,
	      (unsigned long long)s->n_brakes, (unsigned long long)s->n_settle_miss, s->max_rise_cm, s->max_air_s, s->max_land_cms,
	      (unsigned long long)g_jp.ring_shows, (unsigned long long)g_jp.ring_frames, (unsigned long long)g_jp.finds,
	      (unsigned long long)g_jp.find_rays, g_jp.land_off_max, (unsigned long long)g_jp.ticks,
	      (unsigned long long)g_jp.grav_writes, (unsigned long long)g_jp.grav_resets, g_jp_via_w[g_jp.via],
	      (unsigned long long)g_jp.extra_taken, (unsigned long long)g_jp.extra_missed, (unsigned long long)g_jp.setpos_n,
	      (unsigned long long)g_jp.setpos_ceil, (unsigned long long)g_jp.other_tid);
}
