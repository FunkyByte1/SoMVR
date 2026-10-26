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
static void sk_prompt(int act, int up)
{
	if (act == BO_CMD_STRIKE)
		atomic_store_explicit(&g_sk.prompt, up, memory_order_relaxed);
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

/* the input tick, after the bow's (bo_tick): X's edge with the bow out; 121 held for press_s */
static void sk_tick(sc_tick_out *o, double t)
{
	if (!g_sk.on)
		return;
	int x = o->live && o->ctx == SC_CTX_WORLD && (g_inp.ctl.down[SOMVR_HAND_L] & SOMVR_BTN_LOWER);
	int edge = x && !g_sk.x_was;
	g_sk.x_was = x;
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
			somvr_sk_press(&g_sk.s, t);
			sk_hc_take(t);
			g_sk.mark_press = 1, g_sk.mark_sync = g_sk.mark_after = 0, g_sk.end_t = 0;
			g_sk.send_until = t + g_sk.cfg.press_s;
			g_sk.lift_until = t + g_sk.cfg.window_s + g_sk.cfg.dash_s;
			g_sk.trace_until = t + g_sk.cfg.window_s + g_sk.cfg.dash_s + 1.0;
			g_sk.max_eye_step = g_sk.max_off_path = g_sk.max_dev = g_sk.max_dyaw = 0, g_sk.have_yaw0 = 0;
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

static void sk_end_log(void)
{
	if (!g_bow.on || (!g_sk.on && !g_sk.presses))
		return;
	logf_("STRIKE END mode %s: presses %llu (X without the bow %llu), 121 ticks sent %llu; moves %llu, dashes ended %llu, no "
	      "move %llu; frames moving %llu (held %llu); the body moments' hold took our point %llu frames; trace lines %llu",
	      somvr_sk_mode_name(g_sk.cfg.mode),
	      (unsigned long long)g_sk.presses, (unsigned long long)g_sk.presses_nobow, (unsigned long long)g_sk.sent_ticks,
	      (unsigned long long)g_sk.starts, (unsigned long long)g_sk.ends, (unsigned long long)g_sk.timeouts,
	      (unsigned long long)g_sk.frames_moving, (unsigned long long)g_sk.s.carried_frames,
	      (unsigned long long)g_sk.bm_follows, (unsigned long long)g_sk.trace_lines);
}
