/* somvr_strike_game.c - SOMVR_BOW_STRIKE=dash|blink (build 12; somvr_strike.h, docs/re-bow.md 14, design-vr.md 3 "The
 * bow", design-locomotion.md 3.9 LO-9): the Shadow Strike on X with the bow out, the wraith dash. NOT a translation
 * unit of its own: somvr.c includes it last (it uses g_bow, g_inp, g_lo, g_fr, the log).
 *   block (the default, build 11)  pad A and 121 cut while the bow is out (bo_guard): nothing starts the strike.
 *   game                           nothing cut, nothing of ours sends it (round 2's A/B).
 *   dash                           X (the left lower button) while the bow is out (aiming or drawn) holds the game's
 *                                  CombatPrimary04 121 for press_s (the game's aim mode decides: its target, its
 *                                  range, its prompt); pad A stays cut. The game moves Talion; the view runs from where
 *                                  it was to the anchor's new point over SOMVR_BOW_STRIKE_MS (300) with
 *                                  SOMVR_BOW_STRIKE_EASE_MS (60) ends, position only, under the wraith tunnel
 *                                  (SOMVR_BOW_STRIKE_TUNNEL 0.4: the vignette quad's ring, white-blue; somvr_comfort's
 *                                  SOMVR_CFF_WRAITH). LOCO's hold of the game's attack moves (SOMVR_LOCO_HOLD=attack) is
 *                                  lifted from the press until the dash ends, so the game's own strike carries him.
 *   blink                          the same press; the view jumps under a 0.1 s white blink instead.
 * Needs SOMVR_BOW=1, SOMVR_INPUT=1, SOMVR_ANCHOR=root (the view's base is the anchor; without it: X sends the strike,
 * the view is the game's / the rig's as without us).
 * Lines: `CONFIG strike ...`, `STRIKE ...` (the press, the start, the end, a timeout), per stereo frame from the press
 * until 1 s after the dash `STRIKE f=...` (the eye's trace), `STRIKE END ...`. */

static struct {
	int on;                        /* dash or blink */
	int stock;                     /* the test word "bow strike stock": X sends the strike, the view is not touched */
	somvr_sk_cfg cfg;
	somvr_sk s;
	somvr_sk_out o;
	int x_was;
	double send_until, trace_until, lift_until;
	_Atomic int prompt;            /* the Shadow Strike's prompt (121) is up (somvr_prompts_game.c) */
	int prompt_at_press, denied_at_press, arrows_at_press;
	/* this frame (the anchor, then H1's eye 0) */
	int have;
	float root[3];
	sv_vec3 target, view, a0;
	float eye_last[3], view_last[3];
	int have_eye;
	float tunnel, blink;
	/* the dash's checks */
	float max_eye_step, max_off_path, max_dev, max_dyaw;
	float yaw0, byaw0;
	int have_yaw0;
	uint64_t presses, presses_nobow, sent_ticks, starts, ends, timeouts, trace_lines, frames_moving, bm_follows;
	uint64_t end_frame;            /* the frame the last dash ended (the body moments' hold takes its point) */
	/* SOMVR_XR_DUMP_MARKED=N (headless tests): the strike's moments marked for the layer's dumps (SOMVR_REC_DUMP) */
	int dump_marks, mark_press, move_frames, mark_sync, mark_after;
	_Atomic int mark_mask; /* the moments marked (bit 0 before .. bit 6 after; the word "bow strike marks N", default all) */
	double end_t;
	uint32_t lines;
	/* the orcs within 30 m at the press and their health, checked again SK_HC_S later (is the target hit, killed?) */
	struct bo_hc hc[BO_HC];
	int nhc, hc_done;
	double hc_t;
} g_sk;
#define SK_HC_S 4.5 /* the knockdown sync (PC_WraithDash_KD_Bip_Standing_F) runs ~3.6 s */

static int sk_log_ok(void) { return g_sk.lines++ < 400; }
static void sg_config(int *bad);       /* build 17: SOMVR_STRIKE_GAZE (the end of this file) */
static const char *sg_word(const char *v);

static void sk_hc_take(double t)
{
	struct bo_act a;
	bo_active(g_fr.pl, &a);
	struct cb_orc near[BO_HC];
	int n = a.pc ? cb_orcs_r(a.pc, a.sc, g_bow.obj_pos, 3000.0f, near, BO_HC) : 0;
	g_sk.nhc = 0, g_sk.hc_done = 0, g_sk.hc_t = t;
	for (int i = 0; i < n; i++) {
		float hp = cb_health(near[i].c);
		if (isfinite(hp))
			g_sk.hc[g_sk.nhc++] = (struct bo_hc){ near[i].c, near[i].hobj, hp, near[i].dist };
	}
}

static void sk_hc_check(double t)
{
	if (g_sk.hc_done || !(g_sk.hc_t > 0) || t - g_sk.hc_t < SK_HC_S)
		return;
	g_sk.hc_done = 1;
	char buf[480];
	size_t o = 0;
	buf[0] = 0;
	for (int i = 0; i < g_sk.nhc && o < sizeof buf; i++) {
		const struct bo_hc *h = &g_sk.hc[i];
		struct cb_orc now;
		int there = cb_orc_get(h->c, &now) && now.hobj == h->hobj, dead = there ? cb_dead(h->c) : 1;
		float hp = there ? cb_health(h->c) : NAN;
		o += (size_t)snprintf(buf + o, sizeof buf - o, "%s hobj %#lx at %.1f m: health %.1f -> %.1f%s", i ? ";" : " ",
				      (unsigned long)h->hobj, h->dist / 100.0f, h->hp0, hp, !there ? " (gone)" : dead ? " DEAD" : "");
	}
	if (sk_log_ok())
		logf_("STRIKE t=%.3f the orcs %.1f s after the press: %d within 30 m%s%s; arrows %d -> %d", t, SK_HC_S, g_sk.nhc,
		      g_sk.nhc ? ":" : "", buf, g_sk.arrows_at_press, bo_arrows(g_fr.pl));
}


static void sk_config(int *bad)
{
	somvr_sk_cfg_default(&g_sk.cfg);
	g_sk.cfg.mode = env_word("SOMVR_BOW_STRIKE", (const char *const[]){ "block", "game", "dash", "blink" }, 4,
				 SOMVR_BSK_BLOCK);
	g_sk.cfg.dash_s = 0.001f * env_float("SOMVR_BOW_STRIKE_MS", 1000 * g_sk.cfg.dash_s, 100, 1500, bad);
	g_sk.cfg.ease_s = 0.001f * env_float("SOMVR_BOW_STRIKE_EASE_MS", 1000 * g_sk.cfg.ease_s, 0, 500, bad);
	g_sk.cfg.tunnel = env_float("SOMVR_BOW_STRIKE_TUNNEL", g_sk.cfg.tunnel, 0, 1, bad);
	g_sk.cfg.window_s = 0.001f * env_float("SOMVR_BOW_STRIKE_WINDOW_MS", 1000 * g_sk.cfg.window_s, 200, 5000, bad);
	g_sk.cfg.hold_max_s = 0.001f * env_float("SOMVR_BOW_STRIKE_HOLD_MS", 1000 * g_sk.cfg.hold_max_s, 100, 3000, bad);
	g_sk.on = g_bow.on && (g_sk.cfg.mode == SOMVR_BSK_DASH || g_sk.cfg.mode == SOMVR_BSK_BLINK);
	atomic_store(&g_bow.strike_block, g_sk.cfg.mode != SOMVR_BSK_GAME);
	const char *dm = getenv("SOMVR_XR_DUMP_MARKED");
	g_sk.dump_marks = dm && atoi(dm) > 0;
	atomic_store(&g_sk.mark_mask, 127);
	if (g_bow.on)
		logf_("CONFIG strike=%s (SOMVR_BOW_STRIKE%s): %s; the dash %.0f ms, ends %.0f ms, tunnel %.2f, the move within %.0f "
		      "ms of the press, the view held over %.0f cm/s (the lunge) at most %.0f ms, the dash at a jump over %.0f cm a frame%s",
		      somvr_sk_mode_name(g_sk.cfg.mode),
		      g_sk.cfg.mode == SOMVR_BSK_BLOCK ? ", the default" : "",
		      g_sk.cfg.mode == SOMVR_BSK_BLOCK ? "pad A and 121 cut while the bow is out"
		      : g_sk.cfg.mode == SOMVR_BSK_GAME ? "nothing cut, nothing of ours sends 121"
		      : g_sk.cfg.mode == SOMVR_BSK_DASH ? "X with the bow out = the game's Shadow Strike (121); the view runs to "
							 "the new place (the wraith dash); pad A cut"
						       : "X with the bow out = the game's Shadow Strike (121); the view jumps "
							 "under a white blink; pad A cut",
		      g_sk.cfg.dash_s * 1e3, g_sk.cfg.ease_s * 1e3, g_sk.cfg.tunnel, g_sk.cfg.window_s * 1e3, g_sk.cfg.start_cms,
		      g_sk.cfg.hold_max_s * 1e3, g_sk.cfg.start_cm, g_sk.on && !g_lo.anchor ? " (WARN: no SOMVR_ANCHOR=root: the view is not ours)" : "");
	if (g_sk.on && !g_lo.anchor)
		logf_("WARN SOMVR_BOW_STRIKE=%s without SOMVR_ANCHOR=root: X sends the strike, the view does what the base does",
		      somvr_sk_mode_name(g_sk.cfg.mode));
	sg_config(bad); /* build 17: SOMVR_STRIKE_GAZE (below) */
}

/* the test word "bow strike free|cost" (headless only: run 065308 struck once, then the game showed no Shadow Strike
 * prompt again): the game's debug switches Combat.DisableEnergyCost and Combat.DisableChargeCost (their bool values,
 * reference/work-gameplay/combat_cvars.tsv) set / cleared, so several strikes can be tested in one boot */
#define SK_CV_ENERGY 0x4c44398u
#define SK_CV_CHARGE 0x4c44358u
/* which = 3 both, 1 the energy alone, 2 the charge alone ("bow strike free energy|charge": which cost blocks the next) */
static const char *sk_cost_word(int free_, int which)
{
	unsigned char v = (unsigned char)free_, b0 = 0, b1 = 0, a0, a1;
	int ok = peek(SK_CV_ENERGY, &b0, 1) && peek(SK_CV_CHARGE, &b1, 1);
	a0 = which & 1 ? v : b0, a1 = which & 2 ? v : b1;
	ok = ok && arm_poke(SK_CV_ENERGY, &a0, 1) && arm_poke(SK_CV_CHARGE, &a1, 1);
	logf_("BOW strike %s%s (the test word): Combat.DisableEnergyCost / DisableChargeCost %u/%u -> %u/%u; arrows %d%s",
	      free_ ? "free" : "cost", which == 1 ? " energy" : which == 2 ? " charge" : "", b0, b1, a0, a1, bo_arrows(g_fr.pl),
	      ok ? "" : " (NOT written: a read or write failed)");
	return free_ ? "bow strike free (the game's energy/charge costs off: a test)" : "bow strike cost";
}

/* the word "bow strike block|game|dash|blink" (the writer thread: the mode is read by the game thread once a frame) */
static const char *sk_word(const char *v)
{
	if (!strncmp(v, "marks ", 6)) { /* SOMVR_XR_DUMP_MARKED's moments for the next strikes (a mask, tests) */
		atomic_store(&g_sk.mark_mask, atoi(v + 6) & 127);
		logf_("BOW strike marks %d (the word): the moments dumped (1 before, 2 mid-hold, 4 the start, 8 mid-dash, 16 the "
		      "arrival, 32 the sync, 64 after)", atomic_load(&g_sk.mark_mask));
		return "bow strike marks";
	}
	if (!strncmp(v, "gaze", 4)) /* build 17: "bow strike gaze [on|off]" */
		return sg_word(v + 4);
	if (!strncmp(v, "free", 4) || !strncmp(v, "cost", 4))
		return sk_cost_word(v[0] == 'f', strstr(v, "energy") ? 1 : strstr(v, "charge") ? 2 : 3);
	int m = somvr_sk_mode_parse(v), stock = !strncmp(v, "stock", 5);
	if (stock) /* a test: X sends the strike as with dash, the view as the anchor gives it (the stock presentation) */
		m = SOMVR_BSK_DASH;
	if (m < 0)
		return "bow strike (not block|game|dash|blink|stock: ignored)";
	g_sk.stock = stock;
	g_sk.cfg.mode = m;
	g_sk.on = g_bow.on && (m == SOMVR_BSK_DASH || m == SOMVR_BSK_BLINK);
	atomic_store(&g_bow.strike_block, m != SOMVR_BSK_GAME);
	logf_("BOW strike now %s (the word)", stock ? "stock (X sends it, the view untouched: a test)" : somvr_sk_mode_name(m));
	return "bow strike";
}

/* somvr_prompts_game.c: the game shows (1) or hides (0) a prompt for command act */
static _Atomic uint64_t g_sk_shows; /* build 17 (SOMVR_STRIKE_GAZE): the Shadow Strike prompt's shows (a show after A) */
static void sk_prompt(int act, int up)
{
	if (act == BO_CMD_STRIKE) {
		atomic_store_explicit(&g_sk.prompt, up, memory_order_relaxed);
		if (up)
			atomic_fetch_add_explicit(&g_sk_shows, 1, memory_order_relaxed);
	}
}

static int sk_prompt_up(void) { return atomic_load_explicit(&g_sk.prompt, memory_order_relaxed); }

/* bo_guard: our own 121 (X) passes; 1 = it is ours this tick */
static int sk_ours(double t) { return g_sk.on && t < g_sk.send_until; }
/* SOMVR_HP_AWAY: the view is ours to hold or move (from the press until the dash ends): Talion hidden in the eyes */
static int sk_away(void) { return g_sk.on && g_sk.s.phase != SOMVR_BSK_IDLE; }
/* the prompts: X sends the strike with the bow out (the prompt stays, shown with the X glyph) */
static int sk_x(void) { return g_sk.on; }
/* LOCO: the game's attack moves carry Talion from the press until the dash ends */
static int sk_lift(double t) { return g_sk.on && t < g_sk.lift_until; }

/* a press that sends 121 (X with the bow out, or the gaze's once the game's prompt is up): the dash armed, 121 for
 * press_s, LOCO's hold lifted, the trace and the orcs' health taken */
static void sk_arm(double t)
{
	somvr_sk_press(&g_sk.s, t);
	sk_hc_take(t);
	g_sk.mark_press = 1, g_sk.mark_sync = g_sk.mark_after = 0, g_sk.end_t = 0;
	g_sk.send_until = t + g_sk.cfg.press_s;
	g_sk.lift_until = t + g_sk.cfg.window_s + g_sk.cfg.dash_s;
	g_sk.trace_until = t + g_sk.cfg.window_s + g_sk.cfg.dash_s + 1.0;
	g_sk.max_eye_step = g_sk.max_off_path = g_sk.max_dev = g_sk.max_dyaw = 0, g_sk.have_yaw0 = 0;
}

static void sg_tick(sc_tick_out *o, double t);

/* the input tick, after the bow's (bo_tick): X's edge with the bow out; 121 held for press_s */
static void sk_tick(sc_tick_out *o, double t)
{
	if (!g_sk.on)
		return;
	int x = o->live && o->ctx == SC_CTX_WORLD && (g_inp.ctl.down[SOMVR_HAND_L] & SOMVR_BTN_LOWER);
	int edge = x && !g_sk.x_was;
	g_sk.x_was = x;
	if (edge && ab3_x_taken()) /* build 27d (SOMVR_ABIL_DISTRACT, the skill owned): X while drawn is the distract's */
		edge = 0;
	sg_tick(o, t); /* build 17, SOMVR_STRIKE_GAZE: A at the orc looked at in the wraith vision (arms the dash below) */
	if (edge) {
		int out = g_bow.s.phase != SOMVR_BOW_PH_IDLE;
		if (!out) {
			g_sk.presses_nobow++;
		} else {
			g_sk.presses++;
			g_sk.prompt_at_press = atomic_load_explicit(&g_sk.prompt, memory_order_relaxed);
			g_sk.arrows_at_press = bo_arrows(g_fr.pl);
			/* the game shows no Shadow Strike prompt, or fewer than 2 arrows are left (a strike takes 2; at 1 and 0
			 * the prompt still shows and nothing happens: runs 082932, 082001): it refuses (re-bow.md 14.1, 14.7); the
			 * "denied" buzz now, not 2 s later at the window's end (121 is still sent: the game decides) */
			g_sk.denied_at_press = !g_sk.prompt_at_press || (g_sk.arrows_at_press >= 0 && g_sk.arrows_at_press < 2);
			if (g_sk.denied_at_press)
				in_haptic_event(SOMVR_HAND_L, 0.25f, 0.08f, SOMVR_HAP_COMBAT);
			sk_arm(t);
			if (sk_log_ok())
				logf_("STRIKE t=%.3f press %llu: X with the bow out (%s): CombatPrimary04 121 for %.0f ms; the game's "
				      "Shadow Strike prompt %s; Talion at %.1f,%.1f,%.1f; arrows %d", t, (unsigned long long)g_sk.presses,
				      somvr_bow_phase_name(g_bow.s.phase), g_sk.cfg.press_s * 1e3,
				      !g_sk.prompt_at_press ? "not up (the game will refuse: the denied buzz now)"
				      : g_sk.arrows_at_press >= 0 && g_sk.arrows_at_press < 2
					      ? "up, under 2 arrows (the game will refuse: the denied buzz now)"
					      : "up",
				      g_bow.obj_pos[0], g_bow.obj_pos[1], g_bow.obj_pos[2], g_sk.arrows_at_press);
		}
	}
	if (t < g_sk.send_until && o->live && o->ctx == SC_CTX_WORLD && o->ncmd < SC_OUT_CMDS) {
		o->cmd[o->ncmd++] = BO_CMD_STRIKE;
		g_sk.sent_ticks++;
	}
}

/* SOMVR_XR_DUMP_MARKED (tests): this frame's record marked for a dump of the next stereo frame (the layer's), named */
static void sk_mark(int bit, const char *what, double t)
{
	if (!g_sk.dump_marks || !g_fr.rec || !(atomic_load_explicit(&g_sk.mark_mask, memory_order_relaxed) >> bit & 1))
		return;
	g_fr.rec->flags |= SOMVR_REC_DUMP;
	logf_("STRIKE t=%.3f dump mark %s: frame %llu (%.3f s after the press%s)", t, what, (unsigned long long)g_fr.frame,
	      t - g_sk.s.press_t, g_sk.end_t > 0 ? "" : ", the dash not ended");
}

/* lo_anchor_body, after the anchor's step (eye 0 of a stereo frame): pos = the anchor's output (the base), r = Talion's
 * root; the dash replaces pos while it runs */
static void sk_anchor(double t, const float r[3], float pos[3])
{
	g_sk.have = 0;
	sk_hc_check(t);
	if (!g_sk.on && g_sk.s.phase == SOMVR_BSK_IDLE)
		return;
	sv_vec3 p = { pos[0], pos[1], pos[2] };
	somvr_sk_out *o = &g_sk.o;
	somvr_sk_step(&g_sk.s, &g_sk.cfg, t, p, o);
	memcpy(g_sk.root, r, sizeof g_sk.root);
	g_sk.target = p, g_sk.view = o->view, g_sk.have = 1;
	g_sk.tunnel = o->tunnel, g_sk.blink = o->blink;
	if (g_sk.stock)
		o->active = 0, o->tunnel = o->blink = 0, o->view = p, g_sk.view = p, g_sk.tunnel = g_sk.blink = 0;
	if (o->active)
		pos[0] = o->view.x, pos[1] = o->view.y, pos[2] = o->view.z, g_sk.frames_moving++;
	if (o->started) {
		g_sk.starts++, g_sk.a0 = g_sk.s.a0;
		g_sk.lift_until = t + g_sk.cfg.dash_s + 0.2;
		if (sk_log_ok())
			logf_("STRIKE t=%.3f start (%s): the view point moved %.1f cm in one frame %.3f s after the press; the %s from "
			      "%.1f,%.1f,%.1f to %.1f,%.1f,%.1f (%.1f cm) over %.0f ms, %.0f cm/s at constant speed; Talion at "
			      "%.1f,%.1f,%.1f", t, somvr_sk_mode_name(g_sk.cfg.mode), g_sk.s.jumped, t - g_sk.s.press_t,
			      g_sk.cfg.mode == SOMVR_BSK_DASH ? "dash" : "blink", g_sk.s.a0.x, g_sk.s.a0.y, g_sk.s.a0.z, p.x, p.y,
			      p.z, g_sk.s.dist0, g_sk.cfg.dash_s * 1e3,
			      somvr_sk_vmax(g_sk.s.dist0, g_sk.cfg.dash_s, g_sk.cfg.ease_s), r[0], r[1], r[2]);
	}
	if (o->ended) {
		g_sk.ends++;
		g_sk.lift_until = t, g_sk.trace_until = t + 1.0;
		if (sk_log_ok())
			logf_("STRIKE t=%.3f end: the view at the anchor again (%.1f,%.1f,%.1f); this dash: the eye's largest frame "
			      "step %.1f cm, off the straight line at most %.2f cm, off its designed point at most %.2f cm, the "
			      "view's base turned at most %.3f deg (the eye's yaw also has the tracked head's)", t, p.x, p.y, p.z, g_sk.max_eye_step, g_sk.max_off_path,
			      g_sk.max_dev, g_sk.max_dyaw);
	}
	if (o->held && g_sk.s.carried == 1 && sk_log_ok())
		logf_("STRIKE t=%.3f hold: the game's lunge (a move over %.0f cm/s, under %.0f cm a frame) %.3f s after the press: "
		      "the view stays at %.1f,%.1f,%.1f until the teleport (at most %.0f ms); Talion at %.1f,%.1f,%.1f", t,
		      g_sk.cfg.start_cms, g_sk.cfg.start_cm, t - g_sk.s.press_t, g_sk.s.held.x, g_sk.s.held.y, g_sk.s.held.z,
		      g_sk.cfg.hold_max_s * 1e3, r[0], r[1], r[2]);
	g_sk.end_frame = o->ended ? g_fr.frame : g_sk.end_frame;
	if (g_sk.dump_marks) { /* the pictures: before, mid-hold, the dash's start, mid-dash, the arrival, the sync, after */
		g_sk.move_frames = o->started ? 0 : g_sk.move_frames + (g_sk.s.phase == SOMVR_BSK_MOVE);
		if (g_sk.mark_press)
			g_sk.mark_press = 0, sk_mark(0, "before (the press)", t);
		else if (o->held && g_sk.s.carried == 2)
			sk_mark(1, "mid-hold (the lunge)", t);
		else if (o->started)
			sk_mark(2, "the dash's start (the teleport)", t);
		else if (g_sk.s.phase == SOMVR_BSK_MOVE && g_sk.move_frames == 2)
			sk_mark(3, "mid-dash", t);
		else if (o->ended)
			sk_mark(4, "the arrival", t), g_sk.end_t = t;
		else if (g_sk.end_t > 0 && !g_sk.mark_sync && t >= g_sk.end_t + 1.5)
			g_sk.mark_sync = 1, sk_mark(5, "the sync (1.5 s after the arrival)", t);
		else if (g_sk.end_t > 0 && !g_sk.mark_after && t >= g_sk.end_t + 3.8)
			g_sk.mark_after = 1, sk_mark(6, "after (3.8 s after the arrival: the sync released)", t);
	}
	if (o->timed_out) {
		g_sk.timeouts++;
		if (!g_sk.denied_at_press) /* the prompt was up and still nothing: "denied" in the X hand (as the bow's) */
			in_haptic_event(SOMVR_HAND_L, 0.25f, 0.08f, SOMVR_HAP_COMBAT);
		if (sk_log_ok())
			logf_("STRIKE t=%.3f no move within %.0f ms of the press (the prompt was %s at the press): the game did not "
			      "strike", t, g_sk.cfg.window_s * 1e3, g_sk.prompt_at_press ? "up" : "not up");
	}
}

/* H1, eye 0 of a stereo frame, after the rig: the trace (where the eye went this frame) */
static void sk_eye(const somvr_eye_out *e)
{
	if (!g_sk.on || cb_now() > g_sk.trace_until) {
		g_sk.have_eye = 0;
		return;
	}
	const float *ep = e->view.pos;
	float step = g_sk.have_eye ? dist3(ep, g_sk.eye_last) : 0;
	float vv[3] = { g_sk.view.x, g_sk.view.y, g_sk.view.z }, vstep = g_sk.have_eye ? dist3(vv, g_sk.view_last) : 0;
	float yaw = sv_heading(sv_qnormalize(sv_quat_of(e->view.quat))) * 57.29578f, by = g_fr.base.yaw * 57.29578f;
	if (!g_sk.have_yaw0)
		g_sk.yaw0 = yaw, g_sk.byaw0 = by, g_sk.have_yaw0 = 1;
	/* the eye's yaw also has the tracked head's turns (headless: a slow steady drift); the dash's own turn is the base's */
	float dyaw = fabsf(remainderf(yaw - g_sk.yaw0, 360.0f)), dby = fabsf(remainderf(by - g_sk.byaw0, 360.0f));
	if (g_sk.o.active && !g_sk.o.held) { /* the checks: the eye's step, the view on the line A0 -> P, the designed point */
		g_sk.max_eye_step = fmaxf(g_sk.max_eye_step, step);
		sv_vec3 a = g_sk.s.a0, b = g_sk.target, v = g_sk.view;
		sv_vec3 ab = { b.x - a.x, b.y - a.y, b.z - a.z }, av = { v.x - a.x, v.y - a.y, v.z - a.z };
		float l2 = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z, k = l2 > 1e-6f ? (av.x * ab.x + av.y * ab.y + av.z * ab.z) / l2 : 0;
		sv_vec3 q = { a.x + k * ab.x - v.x, a.y + k * ab.y - v.y, a.z + k * ab.z - v.z };
		g_sk.max_off_path = fmaxf(g_sk.max_off_path, sqrtf(q.x * q.x + q.y * q.y + q.z * q.z));
		sv_vec3 dp = { a.x + g_sk.o.f * ab.x - v.x, a.y + g_sk.o.f * ab.y - v.y, a.z + g_sk.o.f * ab.z - v.z };
		g_sk.max_dev = fmaxf(g_sk.max_dev, sqrtf(dp.x * dp.x + dp.y * dp.y + dp.z * dp.z));
		g_sk.max_dyaw = fmaxf(g_sk.max_dyaw, dby);
	}
	g_sk.trace_lines++;
	/* SOMVR_HP_AWAY: what the eyes hide of Talion this frame (hp_frame ran at this frame's H4); the arms' state */
	int hpa = g_hpa.frame == g_fr.frame;
	uint64_t ms = 0;
	for (int p = 0; p < SOMVR_HP_PASSES; p++)
		ms += atomic_load_explicit(&g_hp_mesh_skips[p], memory_order_relaxed);
	logf_("STRIKE f=%llu t=%.4f %s root %.1f,%.1f,%.1f target %.1f,%.1f,%.1f view %.1f,%.1f,%.1f eye %.1f,%.1f,%.1f step %.1f "
	      "(view %.1f) cm yaw %.2f (%+.3f) base yaw %.2f f %.3f speed %.2f tunnel %.2f blink %.2f prompt %d; talion %s away %d "
	      "body %d pose %d nodes %u meshes %u weapons hid %d kept %d (mesh skips %llu); arms L %d %.0f R %d %.0f cm; bm %d",
	      (unsigned long long)g_fr.frame, cb_now(),
	      g_sk.s.phase == SOMVR_BSK_MOVE ? "MOVE" : g_sk.s.phase == SOMVR_BSK_HOLD ? "HOLD" : g_sk.s.phase ? "ARMED" : "idle",
	      g_sk.root[0], g_sk.root[1], g_sk.root[2], g_sk.target.x, g_sk.target.y, g_sk.target.z, g_sk.view.x, g_sk.view.y,
	      g_sk.view.z, ep[0], ep[1], ep[2], step, vstep, yaw, dyaw, g_fr.base.yaw * 57.29578f, g_sk.o.f, g_sk.o.speed,
	      g_sk.tunnel, g_sk.blink, atomic_load(&g_sk.prompt), !hpa ? "unset" : g_hpa.main ? "hidden" : "DRAWN",
	      hpa && g_hpa.away, hpa && g_hpa.main, hpa && g_hpa.pose, hpa ? g_hpa.n : 0, hpa ? g_hpa.nmesh : 0,
	      hpa ? g_hpa.hid : 0, hpa ? g_hpa.kept : 0, (unsigned long long)ms, g_arm.state[SA_L], g_arm.rep[SA_L].reach,
	      g_arm.state[SA_R], g_arm.rep[SA_R].reach, g_bm.on && g_bm.o.frozen);
	memcpy(g_sk.eye_last, ep, sizeof g_sk.eye_last), memcpy(g_sk.view_last, vv, sizeof g_sk.view_last);
	g_sk.have_eye = 1;
}

/* bm_base (eye 0, after the anchor): the body moments hold the view through the strike's sync (the knockdown, 3.6 s);
 * a hold that began during our hold or dash takes our view point each frame, so it holds where the dash arrived, not
 * where the lunge left the view (run 064438: held 8.2 m behind, released by a blink 3.6 s later). 1 = follow */
static int sk_bm_follow(void)
{
	int f = g_sk.on && !g_sk.stock && g_sk.have &&
		(g_sk.s.phase == SOMVR_BSK_HOLD || g_sk.s.phase == SOMVR_BSK_MOVE || g_sk.end_frame == g_fr.frame);
	g_sk.bm_follows += (uint64_t)f;
	return f;
}

/* cf_frame: this frame's wraith tunnel / white blink into the comfort entry (SOMVR_CFF_WRAITH) */
static uint32_t sk_comfort(float *blink)
{
	if (!g_sk.on || !g_sk.have || (g_sk.tunnel <= 0 && g_sk.blink <= 0))
		return 0;
	if (g_sk.blink > *blink)
		*blink = g_sk.blink;
	return SOMVR_CFF_WRAITH | somvr_cff_tunnel_bits(g_sk.tunnel);
}

static void sg_end_log(void);
static void sk_end_log(void)
{
	if (!g_bow.on)
		return;
	sg_end_log();
	if (!g_sk.on && !g_sk.presses)
		return;
	logf_("STRIKE END mode %s: presses %llu (X without the bow %llu), 121 ticks sent %llu; moves %llu, dashes ended %llu, no "
	      "move %llu; frames moving %llu (held %llu); the body moments' hold took our point %llu frames; trace lines %llu",
	      somvr_sk_mode_name(g_sk.cfg.mode),
	      (unsigned long long)g_sk.presses, (unsigned long long)g_sk.presses_nobow, (unsigned long long)g_sk.sent_ticks,
	      (unsigned long long)g_sk.starts, (unsigned long long)g_sk.ends, (unsigned long long)g_sk.timeouts,
	      (unsigned long long)g_sk.frames_moving, (unsigned long long)g_sk.s.carried_frames,
	      (unsigned long long)g_sk.bm_follows, (unsigned long long)g_sk.trace_lines);
}

/* ==== build 17: SOMVR_STRIKE_GAZE=on (somvr_gaze.h; docs/re-xr-pipeline.md 63, handoff/HANDOFF-wraith.md "build 17") ====
 * The Shadow Strike by gaze. Y held with the wraith vision on, on foot, the bow away, no menu: the orc the head looks
 * at (within SOMVR_STRIKE_GAZE_DEG of the head's ray, in range and in sight; somvr_gz_pick's hysteresis) is marked in
 * the world (somvr_objective's entry, SOMVR_OBF_STRIKE: a wraith-blue ring round its chest, the A button and "Shadow
 * Strike" beside it; under 2 arrows grey and "No arrows"). A (the right lower button: X sits under the thumb that holds
 * Y) with a mark and 2+ arrows commits: the mark locked and filled, the game's aim command (124) held with its free-aim
 * ray from the head to that orc's chest (bo_camxf) until the game's own Shadow Strike prompt comes up (its range, its
 * sight, its target: the game's verdict), then 121 as X sends it (sk_arm: build 12's dash carries the view). No prompt
 * within 0.8 s: the denied buzz, nothing moves. Looking away (0.25 s) or letting go of Y before A: no mark, A does
 * nothing (and the descent's A at an edge is its own again). Needs SOMVR_BOW_STRIKE=dash|blink (the presentation),
 * SOMVR_BOW=1 (the aim command, the ray hook), SOMVR_VISION (Y), the objective layer (SOMVR_XR_OBJECTIVE, default on). */
static struct {
	int on;                   /* the switch (and SOMVR_BOW_STRIKE=dash|blink) */
	_Atomic int req;          /* the word "bow strike gaze on|off": -1 none */
	_Atomic int req_orcs;     /* the word "bow strike gaze orcs": the orcs' bearings from the head next frame (tests) */
	_Atomic int req_probe;    /* the word "bow strike gaze probe S": the aim held S s, its ray on the nearest orc (tests:
				   * the game's prompt against the distance and the time) */
	double probe_until, probe_t0, probe_first;
	int probe_prompt, probe_have;
	float probe_aim[3], probe_dist;
	somvr_gz_cfg cfg;
	somvr_gz g;
	somvr_gz_out o;           /* this frame's pick */
	float ring_min_m, ring_deg;
	int mark_chest;           /* build 18: SOMVR_STRIKE_GAZE_MARK=chest (build 17's ring round the chest); ground = the default */
	float land_cm, land_r_m;  /* the landing: this far from the orc's root toward the head (60); the ring at least this radius */
	uint64_t land_mark, land_frames, land_casts;
	float land_y, land_oy;    /* the last cast's ground and the orc's root height then */
	_Atomic int hold_sync;    /* SOMVR_STRIKE_GAZE_AIM_HOLD=sync: the aim held until the strike's sync is over (as the bow
				   * path holds it: the bow stays out); move (default): until the strike's move begins */
	uint64_t ends_at_send;    /* g_sk.ends at 121 */
	int moved;                /* this press's strike has moved the view (its hold or dash): no mark from then on */
	int world, a_was, arrows, climbing;
	int have_head;
	float head[3], fwd[3];    /* the last frame's head (world cm) and its forward */
	uint64_t shows_at_press, head_frame;
	uint32_t ids[16];         /* the marks' small numbers (logs, the layer's id) */
	uint64_t idk[16];
	uint32_t next_id;
	uint64_t frames_on, frames_marked, aim_ticks, cam_over, casts, blocked, published, other_tid;
	uint32_t lines;
	double sum_t;
} g_sg;

static int sg_log_ok(void) { return g_sg.lines++ < 300; }

static uint32_t sg_id(uint64_t k)
{
	for (int i = 0; i < 16; i++)
		if (g_sg.idk[i] == k)
			return g_sg.ids[i];
	int j = (int)(g_sg.next_id % 16);
	g_sg.idk[j] = k, g_sg.ids[j] = ++g_sg.next_id;
	return g_sg.next_id;
}

static void sg_config(int *bad)
{
	somvr_gz_cfg_default(&g_sg.cfg);
	static const char *const w[] = { "off", "on" };
	int on = env_word("SOMVR_STRIKE_GAZE", w, 2, 0);
	g_sg.cfg.cone_deg = env_float("SOMVR_STRIKE_GAZE_DEG", g_sg.cfg.cone_deg, 1, 20, bad);
	g_sg.cfg.keep_deg = env_float("SOMVR_STRIKE_GAZE_KEEP_DEG", g_sg.cfg.keep_deg, g_sg.cfg.cone_deg, 40, bad);
	g_sg.cfg.range_cm = 100 * env_float("SOMVR_STRIKE_GAZE_RANGE_M", g_sg.cfg.range_cm / 100, 3, 60, bad);
	g_sg.cfg.aim_max_s = 0.001f * env_float("SOMVR_STRIKE_GAZE_AIM_MS", 1000 * g_sg.cfg.aim_max_s, 100, 3000, bad);
	g_sg.ring_min_m = 1.0f, g_sg.ring_deg = 7.0f;
	g_sg.mark_chest = env_word("SOMVR_STRIKE_GAZE_MARK", (const char *const[]){ "ground", "chest" }, 2, 0);
	g_sg.land_cm = env_float("SOMVR_STRIKE_GAZE_LAND_CM", 60, 0, 300, bad), g_sg.land_r_m = 0.42f;
	atomic_store(&g_sg.hold_sync, env_word("SOMVR_STRIKE_GAZE_AIM_HOLD", (const char *const[]){ "move", "sync" }, 2, 0));
	atomic_store(&g_sg.req, -1);
	g_sg.on = on && g_sk.on;
	if (on || g_bow.on)
		logf_("CONFIG strike gaze=%s (SOMVR_STRIKE_GAZE%s): %s; the mark within %.1f deg of the head's ray (kept to %.1f, "
		      "another orc %.1f deg nearer), after %.0f ms, lost after %.0f ms; range %.1f..%.0f m; the game's prompt within "
		      "%.0f ms of A; 121 %.0f ms, the aim held %.0f ms after; the ring %.1f m or %.0f deg", on ? "on" : "off",
		      on ? "" : ", the default",
		      !on ? "nothing marked, A does nothing new"
		      : g_sk.on ? "Y held in the wraith vision marks the orc looked at; A = the game's Shadow Strike at it"
				: "NOT on: it needs SOMVR_BOW_STRIKE=dash|blink and SOMVR_BOW=1",
		      g_sg.cfg.cone_deg, g_sg.cfg.keep_deg, g_sg.cfg.switch_deg, g_sg.cfg.dwell_s * 1e3, g_sg.cfg.lose_s * 1e3,
		      g_sg.cfg.near_cm / 100, g_sg.cfg.range_cm / 100, g_sg.cfg.aim_max_s * 1e3, g_sg.cfg.press_s * 1e3,
		      g_sg.cfg.hold_s * 1e3, g_sg.ring_min_m, g_sg.ring_deg);
	if (on)
		logf_("CONFIG strike gaze mark=%s (SOMVR_STRIKE_GAZE_MARK%s): %s; the landing %.0f cm from the orc's root toward "
		      "the head (SOMVR_STRIKE_GAZE_LAND_CM)", g_sg.mark_chest ? "chest" : "ground", g_sg.mark_chest ? "" : ", the default",
		      g_sg.mark_chest ? "a ring round the orc's chest, the label beside it"
				      : "a ring flat on the ground where Talion lands (its ground by a ray), the label standing beside it",
		      g_sg.land_cm);
}

/* "bow strike gaze [on|off]" (the writer thread): the switch from the next frame; a status line */
static const char *sg_word(const char *v)
{
	while (*v == ' ')
		v++;
	if (!strncmp(v, "on", 2) || !strncmp(v, "off", 3)) {
		atomic_store(&g_sg.req, v[1] == 'n');
		return v[1] == 'n' ? "bow strike gaze on" : "bow strike gaze off";
	}
	if (!strncmp(v, "hold", 4)) { /* "bow strike gaze hold move|sync" (tests: the A/B) */
		atomic_store(&g_sg.hold_sync, strstr(v, "sync") != NULL);
		logf_("STRIKE gaze: the aim held until %s (the word)", atomic_load(&g_sg.hold_sync) ? "the strike's sync is over" : "the strike's move begins");
		return "bow strike gaze hold";
	}
	if (!strncmp(v, "probe", 5)) {
		atomic_store(&g_sg.req_probe, atoi(v + 5) > 0 ? atoi(v + 5) : 10);
		return "bow strike gaze probe (the aim held, its ray on the nearest orc: the prompt's lines)";
	}
	if (!strncmp(v, "orcs", 4)) {
		atomic_store(&g_sg.req_orcs, 1);
		return "bow strike gaze orcs (their bearings from the head: the next frame's STRIKE gaze orc lines)";
	}
	uintptr_t pcb = g_fr.pl ? peek_ptr(g_fr.pl + P_COMBAT) : 0, pcs = pcb ? peek_ptr(pcb + PC_SERVER) : 0;
	uintptr_t sya = pcs ? peek_ptr(pcs + FB_C_SYNC) : 0, syr = sya ? peek_ptr(sya + FB_SYNC_REC) : 0;
	logf_("STRIKE gaze status: Talion's sync record %s; the aim held until %s", syr ? "SET (in a sync)" : "none",
	      atomic_load(&g_sg.hold_sync) ? "the sync is over" : "the move begins");
	logf_("STRIKE gaze status: %s; mark %llu (%.1f deg, %.1f m), phase %d; marks %llu switches %llu unmarks %llu; presses "
	      "%llu (no mark %llu, no arrows %llu), sent %llu, refused %llu", g_sg.on ? "on" : "off",
	      (unsigned long long)g_sg.g.mark, g_sg.g.deg, g_sg.g.dist_cm / 100, g_sg.g.phase, (unsigned long long)g_sg.g.marks,
	      (unsigned long long)g_sg.g.switches, (unsigned long long)g_sg.g.unmarks, (unsigned long long)g_sg.g.presses,
	      (unsigned long long)g_sg.g.nomarks, (unsigned long long)g_sg.g.noarrows, (unsigned long long)g_sg.g.sends,
	      (unsigned long long)g_sg.g.refused);
	return "bow strike gaze (a status line)";
}

/* the head's ray to the orc's chest clear? 1 yes, 0 a wall or rock first, -1 unknown (no cast: the climb's off) */
static int sg_los(const float from[3], const float to[3], uintptr_t hobj)
{
	uintptr_t phys = g_cl.on ? peek_ptr(g_clg.phys_pp) : 0;
	if (!phys || vptr_of((void *)phys) != g_cl.vt_phys ||
	    peek_ptr(g_cl.vt_phys + 8u * CL_SLOT_RAYCAST) != g_cl_sites[CLS_RAYCAST].target)
		return -1;
	unsigned char r[0x80] __attribute__((aligned(16)));
	g_sg.casts++;
	if (!cl_cast1(phys, from, to, r))
		return 1;
	float hp[3];
	uintptr_t obj;
	memcpy(hp, r, sizeof hp), memcpy(&obj, r + 0x20, sizeof obj);
	if (!vec_ok(hp, 1e7f) || obj == hobj || dist3(hp, to) < 70.0f) /* its own capsule, its weapon */
		return 1;
	g_sg.blocked++;
	return 0;
}

/* H4 (the game thread, a complete stereo frame, before ob_frame): this frame's pick and its lines */
static void sg_frame(uintptr_t pl, int stereo)
{
	int rq = atomic_exchange(&g_sg.req, -1);
	if (rq >= 0) {
		g_sg.on = rq && g_sk.on;
		logf_("STRIKE gaze now %s (the word)%s", g_sg.on ? "on" : "off", rq && !g_sk.on ? ": NOT on (SOMVR_BOW_STRIKE is not dash|blink)" : "");
		if (!g_sg.on)
			somvr_gz_reset(&g_sg.g);
	}
	if (!g_sg.on || !stereo || !pl)
		return;
	if (my_tid() != g_inp.tid) {
		g_sg.other_tid++;
		return;
	}
	double t = cb_now();
	sv_vec3 hp;
	sv_quat hq;
	somvr_rig_head(&g_rig, g_fr.base, g_fr.views.pose, &hp, &hq);
	sv_vec3 f = sv_qrot(sv_qnormalize(hq), (sv_vec3){ 0, 0, 1 });
	g_sg.head[0] = hp.x, g_sg.head[1] = hp.y, g_sg.head[2] = hp.z, g_sg.fwd[0] = f.x, g_sg.fwd[1] = f.y, g_sg.fwd[2] = f.z;
	g_sg.have_head = vec_ok(g_sg.head, 1e7f) && isfinite(f.x + f.y + f.z), g_sg.head_frame = g_fr.frame;
	g_sg.climbing = g_cl.on && g_cl.s.phase != SOMVR_CLIMB_PH_NONE;
	int y = g_sg.world && (g_inp.ctl.down[SOMVR_HAND_L] & SOMVR_BTN_UPPER), vis = st_vision();
	int on = g_sg.have_head && y && vis == 1 && !g_sg.climbing && g_bow.s.phase == SOMVR_BOW_PH_IDLE && !sk_away() &&
		 !game_paused();
	g_sg.arrows = bo_arrows(pl);
	struct bo_act a;
	bo_active(pl, &a);
	struct cb_orc near[12];
	somvr_gz_orc orcs[12];
	int n = 0;
	int pr = atomic_exchange(&g_sg.req_probe, 0);
	if (pr > 0) {
		g_sg.probe_t0 = t, g_sg.probe_until = t + pr, g_sg.probe_first = 0, g_sg.probe_prompt = -1;
		logf_("STRIKE t=%.3f gaze probe: the aim held %d s, its ray on the nearest orc's chest", t, pr);
	}
	int probe = t < g_sg.probe_until;
	float tp[3] = { 0, 0, 0 }; /* Talion (the orcs' list is about him) */
	uintptr_t pm = 0, tobj = 0;
	int body = player_objs(pl, &pm, &tobj) && peek(tobj + OBJ_POS, tp, sizeof tp) && vec_ok(tp, 1e7f);
	if ((on || g_sg.g.phase || probe) && a.pc && body)
		n = cb_orcs_r(a.pc, a.sc, tp, g_sg.cfg.range_cm + g_sg.cfg.range_keep_cm + 300.0f, near, 12);
	int seen = 0;
	for (int i = 0; i < n; i++) {
		orcs[i].id = near[i].hobj, memcpy(orcs[i].root, near[i].pos, sizeof orcs[i].root), orcs[i].los = -1;
		float d, deg = somvr_gz_angle(&g_sg.cfg, g_sg.head, g_sg.fwd, near[i].pos, &d);
		if (on && deg <= g_sg.cfg.keep_deg + 3) { /* only the ones near the ray are cast for */
			float to[3] = { near[i].pos[0], near[i].pos[1] + g_sg.cfg.chest_cm, near[i].pos[2] };
			orcs[i].los = sg_los(g_sg.head, to, near[i].hobj);
			seen += orcs[i].los == 1;
		}
	}
	if (atomic_exchange(&g_sg.req_orcs, 0)) { /* tests: where each orc is from the head (the driver turns the head) */
		if (!n && a.pc && body)
			n = cb_orcs_r(a.pc, a.sc, tp, 4000.0f, near, 12);
		float yaw = atan2f(g_sg.fwd[0], g_sg.fwd[2]), pit = asinf(fmaxf(-1, fminf(1, g_sg.fwd[1])));
		for (int i = 0; i < n; i++) {
			float to[3] = { near[i].pos[0] - g_sg.head[0], near[i].pos[1] + g_sg.cfg.chest_cm - g_sg.head[1],
					near[i].pos[2] - g_sg.head[2] }, d;
			float h = sqrtf(to[0] * to[0] + to[2] * to[2]), by = remainderf(atan2f(to[0], to[2]) - yaw, 6.2831853f);
			float deg = somvr_gz_angle(&g_sg.cfg, g_sg.head, g_sg.fwd, near[i].pos, &d);
			logf_("STRIKE gaze orc %d: hobj %#lx at %.1f m: its chest %+.1f deg right %+.1f deg up of the head's ray (%.1f "
			      "deg off its body); Y %d vision %d on %d", i, (unsigned long)near[i].hobj, d / 100, by * 57.29578f,
			      (atan2f(to[1], h) - pit) * 57.29578f, deg, y, vis, on);
		}
		if (!n)
			logf_("STRIKE gaze orc: none within 40 m (Y %d vision %d on %d)", y, vis, on);
	}
	g_sg.probe_have = probe && n > 0;
	if (g_sg.probe_have) {
		g_sg.probe_aim[0] = near[0].pos[0], g_sg.probe_aim[1] = near[0].pos[1] + g_sg.cfg.chest_cm, g_sg.probe_aim[2] = near[0].pos[2];
		g_sg.probe_dist = dist3(g_sg.probe_aim, g_sg.head);
		int up = sk_prompt_up();
		if (up && !g_sg.probe_first)
			g_sg.probe_first = t;
		if (up != g_sg.probe_prompt && sg_log_ok())
			logf_("STRIKE t=%.3f gaze probe: the prompt %s at %.2f m (%.0f ms into the probe; Talion at %.0f,%.0f,%.0f)", t,
			      up ? "UP" : "down", g_sg.probe_dist / 100, (t - g_sg.probe_t0) * 1e3, tp[0], tp[1], tp[2]);
		g_sg.probe_prompt = up;
	} else if (g_sg.probe_until > 0 && !probe) {
		logf_("STRIKE t=%.3f gaze probe over: the first prompt %.0f ms in", t, g_sg.probe_first ? (g_sg.probe_first - g_sg.probe_t0) * 1e3 : -1.0);
		g_sg.probe_until = 0;
	}
	uint64_t was = g_sg.g.mark;
	somvr_gz_pick(&g_sg.g, &g_sg.cfg, t, on, g_sg.head, g_sg.fwd, orcs, n, &g_sg.o);
	g_sg.frames_on += on, g_sg.frames_marked += g_sg.o.mark != 0;
	if (g_sg.o.ev && sg_log_ok()) {
		uint32_t ev = g_sg.o.ev;
		logf_("STRIKE t=%.3f frame=%llu gaze: %s orc %u (hobj %#llx) at %.1f m, %.1f deg off the head's ray%s%s; arrows %d; "
		      "Y %d vision %d", t, (unsigned long long)g_fr.frame,
		      ev & SOMVR_GZE_SWITCH ? "the mark moved to" : ev & SOMVR_GZE_MARK ? "marked" : "unmarked",
		      sg_id(ev & SOMVR_GZE_UNMARK && !(ev & (SOMVR_GZE_MARK | SOMVR_GZE_SWITCH)) ? was : g_sg.o.mark),
		      (unsigned long long)(ev & SOMVR_GZE_UNMARK && !(ev & (SOMVR_GZE_MARK | SOMVR_GZE_SWITCH)) ? was : g_sg.o.mark),
		      g_sg.o.dist_cm / 100, g_sg.o.deg, ev & SOMVR_GZE_UNMARK ? ": " : "",
		      ev & SOMVR_GZE_UNMARK ? somvr_gz_why_name(g_sg.o.why) : "", g_sg.arrows, y, vis);
	}
	if (t >= g_sg.sum_t && (on || g_sg.o.mark)) {
		g_sg.sum_t = t + 2.0;
		logf_("STRIKE t=%.3f gaze: %s; head %.0f,%.0f,%.0f ray (%.3f %.3f %.3f); orcs %d (cast for and in sight %d); mark %u "
		      "%.1f m %.1f deg (frames on %llu, marked %llu; casts %llu, blocked %llu)", t, on ? "on" : "off (a press runs)",
		      g_sg.head[0], g_sg.head[1], g_sg.head[2], g_sg.fwd[0], g_sg.fwd[1], g_sg.fwd[2], n, seen, g_sg.o.mark ? sg_id(g_sg.o.mark) : 0,
		      g_sg.o.dist_cm / 100, g_sg.o.deg, (unsigned long long)g_sg.frames_on, (unsigned long long)g_sg.frames_marked,
		      (unsigned long long)g_sg.casts, (unsigned long long)g_sg.blocked);
	}
}

/* ob_frame: the mark into this frame's objective entry (1 = published); while the dash holds or moves the view: none */
static int sg_entry(somvr_objective_frame *e)
{
	if (g_sg.g.phase && (g_sk.s.phase == SOMVR_BSK_HOLD || g_sk.s.phase == SOMVR_BSK_MOVE))
		g_sg.moved = 1; /* the arrival and the sync after it show no mark (run 102126: it hung over the orc on the ground) */
	if (!g_sg.on || !g_sg.o.mark || g_sg.head_frame != g_fr.frame || g_sk.s.phase == SOMVR_BSK_HOLD ||
	    g_sk.s.phase == SOMVR_BSK_MOVE || (g_sg.g.phase && g_sg.moved))
		return 0;
	somvr_th_map mp = { { g_fr.base.pos.x, g_fr.base.pos.y, g_fr.base.pos.z }, g_fr.base.yaw - g_rig.yaw0,
			    { g_rig.p0.x, g_rig.p0.y, g_rig.p0.z }, somvr_rig_scale(&g_rig) };
	float hx[3];
	somvr_th_to_xr(&mp, g_sg.o.aim, e->pos), somvr_th_to_xr(&mp, g_sg.head, hx);
	float d = sqrtf((e->pos[0] - hx[0]) * (e->pos[0] - hx[0]) + (e->pos[1] - hx[1]) * (e->pos[1] - hx[1]) +
			(e->pos[2] - hx[2]) * (e->pos[2] - hx[2]));
	e->n = 1, e->id = sg_id(g_sg.o.mark);
	e->flags = SOMVR_OBF_STRIKE | (g_sg.g.phase ? SOMVR_OBF_STRIKE_GO
				       : g_sg.arrows >= 0 && g_sg.arrows < g_sg.cfg.arrows_min ? SOMVR_OBF_STRIKE_NO : 0u);
	e->dist_m = somvr_ob_strike_ring(d, g_sg.ring_min_m, g_sg.ring_deg);
	if (!g_sg.mark_chest) { /* build 18: the landing on the ground (the user: "a symbol on the ground where I would land") */
		float o[3] = { g_sg.o.aim[0], g_sg.o.aim[1] - g_sg.cfg.chest_cm, g_sg.o.aim[2] }, l[3], g[3];
		float hx2 = g_sg.head[0] - o[0], hz2 = g_sg.head[2] - o[2], hl = sqrtf(hx2 * hx2 + hz2 * hz2);
		float k = hl > 1 ? fminf(g_sg.land_cm, 0.8f * hl) / hl : 0;
		l[0] = o[0] + hx2 * k, l[2] = o[2] + hz2 * k, l[1] = o[1] - g_sg.cfg.seg_lo_cm - 20.0f; /* the orc's feet (root - 90) */
		float from[3] = { l[0], o[1] + 60.0f, l[2] };
		uintptr_t ho = 0;
		if (g_sg.o.mark == g_sg.land_mark && g_sg.land_frames++ % 8) /* the ground: cast every 8th frame, kept between */
			l[1] = g_sg.land_y + (o[1] - g_sg.land_oy);
		else if (pr_ray_ok() && pr_ray_down(from, 300.0f, (uintptr_t)g_sg.o.mark, g, &ho) && vec_ok(g, 1e7f))
			l[1] = g[1], g_sg.land_y = g[1], g_sg.land_oy = o[1], g_sg.land_mark = g_sg.o.mark, g_sg.land_casts++;
		else
			g_sg.land_y = l[1], g_sg.land_oy = o[1], g_sg.land_mark = g_sg.o.mark;
		somvr_th_to_xr(&mp, l, e->pos);
		float dl = sqrtf((e->pos[0] - hx[0]) * (e->pos[0] - hx[0]) + (e->pos[1] - hx[1]) * (e->pos[1] - hx[1]) +
				 (e->pos[2] - hx[2]) * (e->pos[2] - hx[2]));
		e->dist_m = 0.5f * somvr_ob_strike_ring(dl, 2 * g_sg.land_r_m, 3.2f); /* the radius: 3.2 deg across, >= 0.42 m */
	} else {
		e->flags |= SOMVR_OBF_STRIKE_CHEST;
	}
	g_sg.published++;
	return 1;
}

/* the input tick (sk_tick, before X's own): A's edge with a mark; the press's aim command and its strike */
static void sg_tick(sc_tick_out *o, double t)
{
	g_sg.world = o->live && o->ctx == SC_CTX_WORLD;
	if (!g_sg.on)
		return;
	int a = g_sg.world && (g_inp.ctl.down[SOMVR_HAND_R] & SOMVR_BTN_LOWER), edge = a && !g_sg.a_was;
	g_sg.a_was = a;
	if (edge && ab_owner() == SOMVR_AO_STRIKE) { /* a mark at the press (ab_tick: the dodge and the climb yield) */
		uint32_t r = somvr_gz_press(&g_sg.g, &g_sg.cfg, t, g_sg.arrows);
		if (r == SOMVR_GZE_PRESS) {
			g_sg.shows_at_press = atomic_load_explicit(&g_sk_shows, memory_order_relaxed), g_sg.moved = 0;
			in_haptic_event(SOMVR_HAND_R, 0.35f, 0.03f, SOMVR_HAP_COMBAT); /* a click: committed */
		} else if (r == SOMVR_GZE_NOARROWS) {
			in_haptic_event(SOMVR_HAND_R, 0.25f, 0.08f, SOMVR_HAP_COMBAT); /* the denied buzz */
		}
		if (r && sg_log_ok())
			logf_("STRIKE t=%.3f gaze: A at orc %u (%.1f m, %.1f deg off the head's ray): %s; arrows %d", t,
			      sg_id(g_sg.g.target ? g_sg.g.target : g_sg.g.mark), g_sg.g.dist_cm / 100, g_sg.g.deg,
			      r == SOMVR_GZE_PRESS ? "committed: the game's aim with its ray at the orc's chest, 121 when its "
						     "Shadow Strike prompt comes"
			      : r == SOMVR_GZE_NOARROWS ? "refused: under 2 arrows (the denied buzz; nothing moves)"
							: "nothing",
			      g_sg.arrows);
	} else if (edge) {
		g_sg.g.nomarks++;
		if (g_sg.g.nomarks <= 20 && sg_log_ok())
			logf_("STRIKE t=%.3f gaze: A without a mark: nothing (Y %d, the vision %d)", t,
			      (g_inp.ctl.down[SOMVR_HAND_L] & SOMVR_BTN_UPPER) != 0, st_vision());
	}
	int prompt = sk_prompt_up() && atomic_load_explicit(&g_sk_shows, memory_order_relaxed) > g_sg.shows_at_press;
	int hs = atomic_load_explicit(&g_sg.hold_sync, memory_order_relaxed);
	/* move: the aim's end once the strike moves him; sync: once its dash has ended and the body moments' hold is over
	 * (the knockdown's sync), at most 6 s */
	int struck = hs ? g_sg.g.phase == SOMVR_GZP_SENT && g_sk.s.phase == SOMVR_BSK_IDLE && g_sk.ends > g_sg.ends_at_send &&
				  !(g_bm.on && g_bm.o.frozen)
			: g_sk.s.phase == SOMVR_BSK_HOLD || g_sk.s.phase == SOMVR_BSK_MOVE;
	float hold0 = g_sg.cfg.hold_s;
	if (hs)
		g_sg.cfg.hold_s = 6.0f;
	somvr_gz_out to;
	memset(&to, 0, sizeof to);
	int ph = g_sg.g.phase;
	somvr_gz_tick(&g_sg.g, &g_sg.cfg, t, prompt, struck, &to);
	g_sg.cfg.hold_s = hold0;
	if (to.ev & SOMVR_GZE_SEND) {
		g_sk.presses++;
		g_sk.prompt_at_press = 1, g_sk.denied_at_press = 0, g_sk.arrows_at_press = g_sg.arrows;
		g_sg.ends_at_send = g_sk.ends;
		sk_arm(t);
		if (sk_log_ok())
			logf_("STRIKE t=%.3f press %llu: A by gaze at orc %u (the game's Shadow Strike prompt up %.0f ms after A): "
			      "CombatPrimary04 121 for %.0f ms; Talion at %.1f,%.1f,%.1f; arrows %d", t,
			      (unsigned long long)g_sk.presses, sg_id(g_sg.g.target), (t - g_sg.g.press_t) * 1e3,
			      g_sk.cfg.press_s * 1e3, g_bow.obj_pos[0], g_bow.obj_pos[1], g_bow.obj_pos[2], g_sg.arrows);
	}
	if (to.ev & SOMVR_GZE_REFUSED) {
		in_haptic_event(SOMVR_HAND_R, 0.25f, 0.08f, SOMVR_HAP_COMBAT);
		if (sg_log_ok())
			logf_("STRIKE t=%.3f gaze: no Shadow Strike prompt within %.0f ms of A: the game refuses (the denied buzz; "
			      "nothing moves); the aim let go", t, g_sg.cfg.aim_max_s * 1e3);
	}
	if ((to.ev & SOMVR_GZE_DONE) && ph == SOMVR_GZP_SENT && sg_log_ok())
		logf_("STRIKE t=%.3f gaze: the aim let go %.0f ms after 121 (%s)", t, (t - g_sg.g.send_t) * 1e3,
		      struck ? (hs ? "the strike's sync is over" : "the strike moves him") : hs ? "the sync not over: 6 s" : "no move seen yet");
	if (g_sg.probe_have && t < g_sg.probe_until) /* the test word's probe: the aim held */
		to.aim_cmd = 1;
	if (to.aim_cmd && o->live && o->ctx == SC_CTX_WORLD && o->ncmd < SC_OUT_CMDS)
		o->cmd[o->ncmd++] = g_bow.aim_cmd, g_sg.aim_ticks++;
}

/* bo_camxf: while a gaze press runs, the game's free-aim ray from the head to the target's chest; 1 = written */
static int sg_camxf(float *xf)
{
	int probe = g_sg.probe_have && cb_now() < g_sg.probe_until;
	if (!g_sg.on || (g_sg.g.phase == SOMVR_GZP_OFF && !probe) || !g_sg.have_head)
		return 0;
	const float *at = g_sg.g.phase ? g_sg.g.target_aim : g_sg.probe_aim;
	sv_vec3 d = { at[0] - g_sg.head[0], at[1] - g_sg.head[1], at[2] - g_sg.head[2] };
	float l = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
	if (!(l > 10.0f) || !isfinite(l))
		return 0;
	d.x /= l, d.y /= l, d.z /= l;
	sv_quat q = somvr_bow_look(d, sv_heading(sv_qnormalize(sv_quat_of(xf + 3))));
	xf[0] = g_sg.head[0], xf[1] = g_sg.head[1], xf[2] = g_sg.head[2];
	sv_quat_to(q, xf + 3);
	if (++g_sg.cam_over <= 3 && sg_log_ok())
		logf_("STRIKE gaze: the game's free-aim ray from the head %.0f,%.0f,%.0f along (%.3f %.3f %.3f) to orc %u's chest "
		      "(%.1f m)", xf[0], xf[1], xf[2], d.x, d.y, d.z, sg_id(g_sg.g.target), l / 100);
	return 1;
}

/* SOMVR_CLIMB_DESCENT's A on foot: not while A belongs to the gaze's mark */
static int sg_wants_a(void) { return g_sg.on && (g_sg.g.mark || g_sg.g.phase); }
static int sg_on(void) { return g_sg.on; }

static void sg_end_log(void)
{
	if (!g_sg.on && !g_sg.g.presses)
		return;
	logf_("STRIKE END gaze: frames on %llu, marked %llu (published %llu); marks %llu, switches %llu, unmarks %llu; A: "
	      "committed %llu, without a mark %llu, under 2 arrows %llu; 121 sent %llu, refused %llu; aim ticks %llu, ray "
	      "writes %llu; sight casts %llu (blocked %llu)%s", (unsigned long long)g_sg.frames_on,
	      (unsigned long long)g_sg.frames_marked, (unsigned long long)g_sg.published, (unsigned long long)g_sg.g.marks,
	      (unsigned long long)g_sg.g.switches, (unsigned long long)g_sg.g.unmarks, (unsigned long long)g_sg.g.presses,
	      (unsigned long long)g_sg.g.nomarks, (unsigned long long)g_sg.g.noarrows, (unsigned long long)g_sg.g.sends,
	      (unsigned long long)g_sg.g.refused, (unsigned long long)g_sg.aim_ticks, (unsigned long long)g_sg.cam_over,
	      (unsigned long long)g_sg.casts, (unsigned long long)g_sg.blocked, g_sg.other_tid ? " (frames on another thread skipped)" : "");
}
