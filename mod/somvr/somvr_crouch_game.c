/* somvr_crouch_game.c - crouch by ducking, the game side (docs/re-xr-pipeline.md 41; included by somvr.c last; the
 * pure part is somvr_crouch.c). The game's stealth crouch is a hold: CombatSecondary04 (125, pad RT) held = PC_Stealth,
 * Talion low with the dagger while it is held (41.1, run 031239); the stock Crouch (15), which the right stick down
 * sends, does nothing at all.
 *   SOMVR_CROUCH=off|button|duck  (default off: build 12's - the stick's Crouch pulse, which does nothing). button: the
 *                           right stick down toggles the stealth crouch. duck: the player ducks for real and Talion
 *                           crouches, standing up stands him up; the stick toggles too. Seated play (SOMVR_SEATED, the
 *                           menu's Seated row) keeps the button and never ducks. The standing height is the head's at
 *                           every recentre (the first stereo frame, the headset's recentre, the word `recenter`) and
 *                           follows a taller stance. The VR menu's Crouch row (CROUCH in SOMVR_SETTINGS: button|duck)
 *                           switches it live; it is shown when SOMVR_CROUCH is button or duck.
 *   SOMVR_CROUCH_DEPTH_CM=28  the duck: the head this far below the standing height ...
 *   SOMVR_CROUCH_ON_MS=150    ... for this long = crouch
 *   SOMVR_CROUCH_UP_CM=18     standing again: the head back above standing - this ...
 *   SOMVR_CROUCH_OFF_MS=200   ... for this long; a crouch lasts at least SOMVR_CROUCH_HOLD_MS=500
 *   SOMVR_CROUCH_CMS=150      the crouched walk's top speed with SOMVR_LOCO while held (the stick's walk band; 0 = no cap)
 *   SOMVR_CROUCH_HAPTIC=0.3   the cue: a short pulse on both hands when Talion is down (half of it when he is up); 0 =
 *                           none
 * The held command is let go in a menu, a struggle and while a hand holds in a climb (held again after, if still
 * ducked). The game's state read back: the Head node's height over Talion's root (72-73 standing, 10-26 down; below
 * 40 = down, above 55 = up). The view never moves by the game's crouch: the eyes are the headset's (SOMVR_BASE=head,
 * SOMVR_ANCHOR=root: the root stays where it is, run 031239), the duck lowers them by the real duck.
 * Trigger words (headless): `crouch status`, `crouch press [TICKS]` (the stealth command for 1..30 ticks), `crouch hold
 * MS` (held MS, 1..10000), `crouch stand` (the standing height = the head now), `crouch try CMD MS` (the probe: command
 * CMD held MS, the deny list passed; CROUCH watch lines 5 a second until 3 s after: the Head node over the root, the
 * FSM, the eyes' push).
 * Lines: `CONFIG crouch ...`, `CROUCH stand|duck on|duck off|game down|game up|button|watch|status ...`, `CROUCH END`. */

static struct {
	int mode;                 /* CR_* (the menu's row changes button <-> duck live) */
	int row;                  /* SOMVR_CROUCH button or duck, the duck possible: the menu's Crouch row */
	somvr_crouch_cfg cfg;
	somvr_crouch s;
	somvr_crouch_hold h;
	float haptic, cap_cms, read_down_cm, read_up_cm;
	uint64_t recenters_seen;
	int edge;                 /* the stereo frames' edge since the last tick (the last one wins) */
	int ducked;               /* the duck's state at the last frame */
	int held;                 /* the stealth command held this tick */
	int cued;                 /* the down cue given for this hold (the up cue is due) */
	int game, game_last;      /* the game's crouch read back: 1 down, 0 up, -1 unknown */
	float head_cm;            /* the Head node over the root, the last frame */
	int press_ticks;          /* the word `crouch press` */
	double hold_until;        /* the word `crouch hold MS` */
	int try_cmd;              /* the word `crouch try CMD MS` */
	double try_until, watch_until, watch_next;
	uint64_t frames, ticks, held_ticks, buttons, game_downs, game_ups, cues, seated_frames, caps;
	uint32_t lines;
	double held_t;            /* build 15b: the last tick with the stealth command held (cr_sword_draw) */
	uint64_t sword_ups;       /* ... the stealth crouch stood up for the sword's draw */
} g_cr;
static _Atomic uint32_t g_cr_status_seq, g_cr_press_seq, g_cr_press_ticks, g_cr_hold_seq, g_cr_hold_ms, g_cr_stand_seq,
	g_cr_try_seq, g_cr_try_cmd, g_cr_try_ms;
static uint32_t g_cr_status_seen, g_cr_press_seen, g_cr_hold_seen, g_cr_stand_seen, g_cr_try_seen;
#define CR_CMD 125            /* CombatSecondary04 (pad RT) held: PC_Stealth, the stealth crouch */
#define CR_STICK SC_CMD_CROUCH /* 15 Crouch: what somvr_ctl sends for the right stick down (does nothing in the game) */

static const char *const g_cr_mode_w[] = { "off", "button", "duck" };
enum { CR_OFF = 0, CR_BUTTON = 1, CR_DUCK = 2 };

/* in_frame (game thread, a stereo frame, after the rig): the duck from the tracked head's height, the game's state */
static void cr_frame(void)
{
	double t = cb_now();
	uintptr_t pm = 0, obj = 0;
	float r[3] = { NAN, NAN, NAN };
	int have = player_objs(g_fr.pl, &pm, &obj) && peek(obj + OBJ_POS, r, sizeof r) && vec_ok(r, 1e7f) && g_fr.head_ok;
	g_cr.head_cm = have ? g_fr.head[1] - r[1] : NAN;
	if (!have)
		g_cr.game = -1;
	else if (g_cr.head_cm < g_cr.read_down_cm)
		g_cr.game = 1;
	else if (g_cr.head_cm > g_cr.read_up_cm || g_cr.game < 0)
		g_cr.game = 0;
	if (t < g_cr.watch_until && t >= g_cr.watch_next) { /* the probe: what a command does to Talion */
		g_cr.watch_next = t + 0.2;
		unsigned char fsm = 0;
		peek(g_fr.pl + LO_P_FSM, &fsm, 1);
		logf_("CROUCH watch t=%.3f cmd %d %s: the head node %.1f cm over the root (root y %.1f), fsm %d, the eyes' push "
		      "%.1f cm", t, g_cr.try_cmd, t < g_cr.try_until || t < g_cr.hold_until ? "held" : "let go", g_cr.head_cm,
		      r[1], fsm, g_hv.o.push_cm);
	}
	if (g_views_fake || g_cr.mode == CR_OFF)
		return;
	sv_vec3 hp;
	sv_quat hq;
	somvr_head_game(g_fr.views.pose, &hp, &hq);
	int seated = g_cf.seat_cfg.on != 0, armed = g_cr.mode == CR_DUCK && !seated;
	uint32_t q = atomic_load_explicit(&g_cr_stand_seq, memory_order_acquire);
	int word = q != g_cr_stand_seen;
	g_cr_stand_seen = q;
	if (g_rig.recenters != g_cr.recenters_seen || word) { /* a recentre: the standing height is the head's now */
		g_cr.recenters_seen = g_rig.recenters;
		float was = g_cr.s.stand;
		int had = g_cr.s.have;
		somvr_crouch_stand(&g_cr.s, hp.y);
		if (g_cr.lines++ < 200)
			logf_("CROUCH stand t=%.3f frame=%llu: the standing height %.1f cm (%s; was %s%.1f)%s", t,
			      (unsigned long long)g_fr.frame, hp.y, word ? "the word" : "a recentre", had ? "" : "none, ",
			      had ? was : 0.0f, seated ? " - seated: the button's play, no duck" : "");
	}
	g_cr.frames++, g_cr.seated_frames += (uint64_t)seated;
	uint64_t rises = g_cr.s.rises;
	int e = somvr_crouch_step(&g_cr.s, &g_cr.cfg, t, hp.y, 1, armed);
	if (g_cr.s.rises != rises && g_cr.lines++ < 200)
		logf_("CROUCH stand t=%.3f: the standing height rose to %.1f cm (the head over it for %.1f s)", t, g_cr.s.stand,
		      g_cr.cfg.rise_s);
	if (e) {
		g_cr.edge = e;
		if (g_cr.lines++ < 200)
			logf_("CROUCH duck %s t=%.3f frame=%llu h=%.1f stand=%.1f (%.1f cm below; on below %.1f, off above %.1f); "
			      "Talion %s (the head node %.1f over the root)%s", e > 0 ? "on" : "off", t,
			      (unsigned long long)g_fr.frame, hp.y, g_cr.s.stand, g_cr.s.stand - hp.y, g_cr.s.stand - g_cr.cfg.depth_cm,
			      g_cr.s.stand - g_cr.cfg.up_cm, g_cr.game > 0 ? "down" : g_cr.game == 0 ? "up" : "unread", g_cr.head_cm,
			      armed ? "" : " (disarmed: the button's play)");
	}
	g_cr.ducked = g_cr.s.ducked;
}

static void cr_add(sc_tick_out *o, int cmd)
{
	for (int i = 0; i < o->ncmd; i++)
		if (o->cmd[i] == cmd)
			return;
	if (o->ncmd < SC_OUT_CMDS)
		o->cmd[o->ncmd++] = cmd;
}

/* in_tick (game thread), after the bow's strike and before the binding layer: the stealth hold from the duck and the
 * stick, the test words; the cue when the game's crouch changes */
static void cr_tick(sc_tick_out *o, double t)
{
	g_cr.ticks++;
	int world = o->live && o->ctx == SC_CTX_WORLD, may = world && !(g_cl.on && g_cl.s.active);
	/* the cue: once when our hold has him down, once when he is up after we let go (not a knockdown's, not each time a
	 * blow's recoil stands him up for a moment while held: run 032244) */
	int cue = g_cr.mode == CR_OFF || !(g_cr.haptic > 0) || !world ? 0
		  : g_cr.held && g_cr.game > 0 && !g_cr.cued ? 1 : !g_cr.held && g_cr.game == 0 && g_cr.cued ? -1 : 0;
	if (cue) {
		float a = cue > 0 ? g_cr.haptic : 0.5f * g_cr.haptic;
		in_haptic_event(SOMVR_HAND_L, a, 0.04f, SOMVR_HAP_UI), in_haptic_event(SOMVR_HAND_R, a, 0.04f, SOMVR_HAP_UI);
		g_cr.cued = cue > 0, g_cr.cues++;
	}
	if (g_cr.game >= 0 && g_cr.game_last >= 0 && g_cr.game != g_cr.game_last) {
		g_cr.game_downs += (uint64_t)(g_cr.game > 0), g_cr.game_ups += (uint64_t)(g_cr.game == 0);
		if (g_cr.lines++ < 200)
			logf_("CROUCH game %s t=%.3f: the head node %.1f cm over the root (the stealth command %s; the duck %s, the "
			      "head %.1f below the standing height)", g_cr.game > 0 ? "down" : "up", t, g_cr.head_cm,
			      g_cr.held ? "held" : "not held", g_cr.ducked ? "on" : "off", somvr_crouch_depth(&g_cr.s));
	}
	g_cr.game_last = g_cr.game;
	uint32_t q = atomic_load_explicit(&g_cr_status_seq, memory_order_acquire);
	if (q != g_cr_status_seen) {
		g_cr_status_seen = q;
		logf_("CROUCH status t=%.3f mode=%s seated=%d stand=%s%.1f head %.1f below it; duck %s (on %llu off %llu, rises "
		      "%llu); held=%d (ticks %llu, the button %llu, latched %d); Talion %s (the head node %.1f over the root; down "
		      "%llu up %llu); speed caps %llu", t, g_cr_mode_w[g_cr.mode], g_cf.seat_cfg.on != 0,
		      g_cr.s.have ? "" : "none ", g_cr.s.stand, somvr_crouch_depth(&g_cr.s), g_cr.ducked ? "on" : "off",
		      (unsigned long long)g_cr.s.ons, (unsigned long long)g_cr.s.offs, (unsigned long long)g_cr.s.rises,
		      g_cr.held, (unsigned long long)g_cr.held_ticks, (unsigned long long)g_cr.buttons, g_cr.h.latched,
		      g_cr.game > 0 ? "down" : g_cr.game == 0 ? "up" : "unread", g_cr.head_cm,
		      (unsigned long long)g_cr.game_downs, (unsigned long long)g_cr.game_ups, (unsigned long long)g_cr.caps);
	}
	q = atomic_load_explicit(&g_cr_press_seq, memory_order_acquire);
	if (q != g_cr_press_seen)
		g_cr_press_seen = q, g_cr.press_ticks = (int)atomic_load(&g_cr_press_ticks);
	q = atomic_load_explicit(&g_cr_hold_seq, memory_order_acquire);
	if (q != g_cr_hold_seen)
		g_cr_hold_seen = q, g_cr.hold_until = t + 1e-3 * atomic_load(&g_cr_hold_ms), g_cr.try_cmd = CR_CMD,
		g_cr.watch_until = g_cr.hold_until + 3.0, g_cr.watch_next = 0; /* the probe's watch with it */
	q = atomic_load_explicit(&g_cr_try_seq, memory_order_acquire);
	if (q != g_cr_try_seen) {
		g_cr_try_seen = q, g_cr.try_cmd = (int)atomic_load(&g_cr_try_cmd);
		g_cr.try_until = t + 1e-3 * atomic_load(&g_cr_try_ms), g_cr.watch_until = g_cr.try_until + 3.0, g_cr.watch_next = 0;
	}
	if (t < g_cr.try_until && world) { /* the probe: a command held (the deny list passed) */
		cr_add(o, g_cr.try_cmd);
		if (sc_denied(g_cr.try_cmd, o->ctx))
			g_inp.allow_cmd = g_cr.try_cmd;
	}
	if (g_cr.mode == CR_OFF)
		return; /* build 12: the stick's Crouch pulse goes to the game as it is */
	int button = 0, n = 0;
	for (int i = 0; i < o->ncmd; i++) /* the right stick down: the button (its stock Crouch does nothing: dropped) */
		if (o->cmd[i] == CR_STICK)
			button = world;
		else
			o->cmd[n++] = o->cmd[i];
	o->ncmd = n;
	int e = g_cr.edge;
	g_cr.edge = 0;
	int want = somvr_crouch_hold_step(&g_cr.h, e, button, g_cr.ducked);
	if (button) {
		g_cr.buttons++;
		if (g_cr.lines++ < 200)
			logf_("CROUCH button t=%.3f (the right stick down): the stealth crouch %s (the duck %s)", t,
			      want ? "held" : "let go", g_cr.ducked ? "on" : "off");
	}
	int word = g_cr.press_ticks > 0 || t < g_cr.hold_until;
	if (g_cr.press_ticks > 0)
		g_cr.press_ticks--;
	g_cr.held = (want && may) || (word && world);
	if (g_cr.held)
		cr_add(o, CR_CMD), g_cr.held_ticks++, g_cr.held_t = t;
}

/* SOMVR_SLOTS_STEALTH=on (build 15b; the slots' sword draw, game thread): our stealth crouch is PC_Stealth, which makes
 * Acharn (slot 1) the active item and sheathes the sword (PC_SheathMeleeToStealth): build 15's headset session drew
 * nothing after the bow ("another weapon is active", DENIED) while the right stick's crouch was latched, and Acharn
 * hung in Talion's hidden hand. 1 = the dagger's slot is our crouch's (held now, let go within CR_SWORD_S, or Talion
 * still down by the read-back); release: the hold let go as the button would (a duck holds nothing until its next
 * start; the test words' hold ends too), logged */
#define CR_SWORD_S 1.5
static int cr_sword_draw(double t, int release)
{
	if (g_cr.mode == CR_OFF && !g_cr.held)
		return 0;
	int want = g_cr.mode != CR_OFF && (g_cr.h.latched || (g_cr.ducked && !g_cr.h.duck_off));
	int ours = g_cr.held || want || (g_cr.held_t > 0 && t - g_cr.held_t < CR_SWORD_S) || (g_cr.game > 0 && g_cr.held_t > 0);
	if (ours && release && (want || g_cr.held)) {
		somvr_crouch_hold_release(&g_cr.h, g_cr.ducked);
		g_cr.press_ticks = 0, g_cr.hold_until = 0;
		g_cr.sword_ups++;
		if (g_cr.lines++ < 200)
			logf_("CROUCH sword t=%.3f: the stealth crouch let go for the sword's draw (PC_Stealth holds Acharn's slot; "
			      "the duck %s; %llu so far)", t, g_cr.ducked ? "on: it holds nothing until its next start" : "off",
			      (unsigned long long)g_cr.sword_ups);
	}
	return ours;
}

/* lo_pm (the LOCO tick): the crouched walk at most SOMVR_CROUCH_CMS (the stick's walk band) */
static void cr_cap(float *cap)
{
	if (g_cr.mode == CR_OFF || !(g_cr.cap_cms > 0) || !g_cr.held) /* our hold only (not a knockdown's or a roll's low head) */
		return;
	if (!(*cap > 0) || *cap > g_cr.cap_cms)
		*cap = g_cr.cap_cms, g_cr.caps++;
}

/* ---- the words, the menu, the config, the end ---------------------------------------------------------------------- */
/* the VR menu (game thread, vm_apply_key): the Crouch row (shown when SOMVR_CROUCH is button or duck) */
static const char *cr_vm_set(int k, const somvr_settings *s)
{
	if (k != SOMVR_SK_CROUCH)
		return "";
	if (!g_cr.row)
		return " (SOMVR_CROUCH off: not used)";
	int m = s->crouch ? CR_DUCK : CR_BUTTON, was = g_cr.mode;
	g_cr.mode = m;
	return m == was ? " (as it was)" : m == CR_DUCK ? " (the duck crouches Talion; the stick toggles too)"
						 : " (the right stick down toggles; a duck's crouch stands up)";
}
static void cr_vm_settings(somvr_settings *s) { s->crouch = g_cr.mode == CR_DUCK; }
static uint32_t cr_vm_avail(void) { return g_cr.row ? SOMVR_VMA_CROUCH : 0u; }

static const char *cr_word(const char *w)
{
	char a[16] = "";
	int n = 0, ms = 0;
	if (sscanf(w, "%15s %d %d", a, &n, &ms) < 1)
		return "crouch (not status|press|hold|stand|try: ignored)";
	if (!strcmp(a, "status"))
		return atomic_fetch_add(&g_cr_status_seq, 1), "crouch status";
	if (!strcmp(a, "stand"))
		return atomic_fetch_add(&g_cr_stand_seq, 1), "crouch stand";
	if (!strcmp(a, "press")) {
		atomic_store(&g_cr_press_ticks, (uint32_t)(n >= 1 && n <= 30 ? n : 3));
		return atomic_fetch_add(&g_cr_press_seq, 1), "crouch press";
	}
	if (!strcmp(a, "hold")) {
		if (n < 1 || n > 10000)
			return "crouch hold (not 1..10000 ms: ignored)";
		atomic_store(&g_cr_hold_ms, (uint32_t)n);
		return atomic_fetch_add(&g_cr_hold_seq, 1), "crouch hold";
	}
	if (!strcmp(a, "try")) {
		if (n < 1 || n >= SC_NCMD || ms < 1 || ms > 10000)
			return "crouch try (not CMD 1..399 MS 1..10000: ignored)";
		atomic_store(&g_cr_try_cmd, (uint32_t)n), atomic_store(&g_cr_try_ms, (uint32_t)ms);
		return atomic_fetch_add(&g_cr_try_seq, 1), "crouch try";
	}
	return "crouch (not status|press|hold|stand|try: ignored)";
}

static void cr_config(int *bad)
{
	somvr_crouch_cfg_default(&g_cr.cfg);
	somvr_crouch_init(&g_cr.s);
	somvr_crouch_hold_init(&g_cr.h);
	g_cr.game = g_cr.game_last = -1, g_cr.head_cm = NAN;
	g_cr.read_down_cm = 40, g_cr.read_up_cm = 55;
	g_cr.mode = env_word("SOMVR_CROUCH", g_cr_mode_w, 3, CR_OFF);
	somvr_crouch_cfg *c = &g_cr.cfg;
	c->depth_cm = env_float("SOMVR_CROUCH_DEPTH_CM", c->depth_cm, 10, 80, bad);
	c->up_cm = env_float("SOMVR_CROUCH_UP_CM", c->up_cm, 2, c->depth_cm - 2, bad);
	c->on_s = 1e-3f * env_float("SOMVR_CROUCH_ON_MS", c->on_s * 1e3f, 0, 2000, bad);
	c->off_s = 1e-3f * env_float("SOMVR_CROUCH_OFF_MS", c->off_s * 1e3f, 0, 2000, bad);
	c->hold_s = 1e-3f * env_float("SOMVR_CROUCH_HOLD_MS", c->hold_s * 1e3f, 0, 5000, bad);
	g_cr.cap_cms = env_float("SOMVR_CROUCH_CMS", 150, 0, 1000, bad);
	g_cr.haptic = env_float("SOMVR_CROUCH_HAPTIC", 0.3f, 0, 1, bad);
	int can = g_inp.on && g_rigcfg.base == SOMVR_BASE_HEAD;
	if (g_cr.mode == CR_DUCK && !can) {
		logf_("WARN SOMVR_CROUCH=duck needs SOMVR_INPUT=1 and SOMVR_BASE=head: the button only");
		g_cr.mode = g_inp.on ? CR_BUTTON : CR_OFF;
	}
	if (g_cr.mode == CR_BUTTON && !g_inp.on)
		g_cr.mode = CR_OFF;
	g_cr.row = g_cr.mode != CR_OFF && can;
	logf_("CONFIG crouch=%s (SOMVR_CROUCH%s): the game's stealth crouch = command %d held (PC_Stealth); the duck %.0f cm "
	      "below the standing height for %.0f ms = down, back above %.0f cm below for %.0f ms = up, at least %.0f ms; the "
	      "standing height at each recentre, rising with a taller stance (%.0f cm for %.1f s); the right stick down %s; "
	      "seated play: the button; the crouched walk at most %.0f cm/s; the cue %.2f; the menu's row %s",
	      g_cr_mode_w[g_cr.mode], getenv("SOMVR_CROUCH") ? "" : " unset", CR_CMD, c->depth_cm, c->on_s * 1e3f, c->up_cm,
	      c->off_s * 1e3f, c->hold_s * 1e3f, c->rise_cm, c->rise_s,
	      g_cr.mode == CR_OFF ? "= the stock Crouch pulse (does nothing)" : "toggles it", g_cr.cap_cms, g_cr.haptic,
	      g_cr.row ? "shown" : "hidden");
}

static void cr_end_log(void)
{
	logf_("CROUCH END mode=%s frames %llu (seated %llu) ticks %llu; the standing height %.1f (measured %llu, rose %llu); "
	      "ducks on %llu off %llu, tracking jumps %llu; held ticks %llu, the button %llu; Talion down %llu up %llu; cues "
	      "%llu; speed caps %llu; stood up for the sword %llu", g_cr_mode_w[g_cr.mode], (unsigned long long)g_cr.frames,
	      (unsigned long long)g_cr.seated_frames, (unsigned long long)g_cr.ticks, g_cr.s.stand,
	      (unsigned long long)g_cr.s.stands, (unsigned long long)g_cr.s.rises, (unsigned long long)g_cr.s.ons,
	      (unsigned long long)g_cr.s.offs, (unsigned long long)g_cr.s.jumps, (unsigned long long)g_cr.held_ticks,
	      (unsigned long long)g_cr.buttons, (unsigned long long)g_cr.game_downs, (unsigned long long)g_cr.game_ups,
	      (unsigned long long)g_cr.cues, (unsigned long long)g_cr.caps, (unsigned long long)g_cr.sword_ups);
}
