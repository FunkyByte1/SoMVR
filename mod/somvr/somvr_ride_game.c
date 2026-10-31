/* somvr_ride_game.c - SOMVR_RIDE (build 26, som-b26-ride; docs/re-xr-pipeline.md 105, design-vr.md 5, design-hla.md
 * SH-2). NOT a translation unit of its own: somvr.c includes it last (after somvr_beasts_game.c, whose beast cache names
 * the mount's kind; it uses the rig, the input tick, the body moments, DEFENCE's tracks and the test hands).
 *
 * Before build 26 a mount (the Player's P+0x1d8 set) went to the body moments' window: the flat game camera on the calm
 * space's quad for the whole ride (SOMVR_BM_POLICY's default mount=window) - and the save could not mount at all (92.5).
 *   SOMVR_RIDE=off|on (code default off; build 26's env on): riding a caragor or a graug in first person, Seat mode:
 *     - the view on the saddle: the beast's root plus the rider's eye offset in its frame (somvr_ride.h: the kind's seat,
 *       Talion's head node's slow mean), through the root anchor's filter (horizontally exact, the vertical 3rd order
 *       with SOMVR_RIDE_Y_MS 120 on a caragor / 200 on a graug), extrapolated to the display time; the base has a
 *       position and a yaw only: no pitch or roll of the gait ever reaches the view;
 *     - the yaw car-style: the beast's heading low-passed (SOMVR_RIDE_YAW_MS 100) and followed by the view at most
 *       SOMVR_RIDE_YAW_DPS (150 deg/s) - the rig turns about the head as the smooth turn does (the comfort vignette sees
 *       it as a turn); the player's own snap and smooth turns stay theirs;
 *     - getting on and off: the view held from the moment the mount changes (or the game's own mount sync holds it)
 *       until Talion sits (his sync over + SOMVR_RIDE_SETTLE_MS, 0) or stands again, then (the body moments' 0.25 s on) their release - a
 *       blink (out, the cut on black, in) for the metre and more between the ground and the saddle; on the blink's black
 *       the view turns to the beast's heading (SOMVR_RIDE_ALIGN=on: you land seated facing its head);
 *     - while seated the moment conditions mount and attach are the ride's (not the window) and a sync or struggle on
 *       Talion (a mounted attack's) does not freeze the view (the seat is the beast's, not his);
 *     - the buttons: A = Jump + CombatPrimary04 + MNT_Charge (build 27c, SOMVR_RIDE_A=pad: the stock pad's A - tapped
 *       the leap, held the sprint), the right trigger = MNT_Attack_2 (the bite), B = 123 (off; build 27), the left
 *       stick's click (latched while pushed) = the sprint (SOMVR_RIDE_SPRINT_CMDS, the same three); the left stick
 *       steers in the beast's frame (build 27); B's interact, the crouch and A's on-foot owners (jump, dash, strike,
 *       climb) stand down meanwhile.
 *     A mount that is not a beast (the game's grab of an orc rides on P+0x1d8 too) is left alone.
 *   SOMVR_RIDE_SEAT=on|off (default on): the Seat mode above; off = a mount is the body moments' window (build 25), the
 *     counter, the mount's press, the domination's hold and the mounted buttons stay (build 26's env: off - Seat mode is
 *     proven on the selftest's stand-ins only; no headless boot reached a mount).
 *   SOMVR_RIDE_COUNTER=on|off (default on with SOMVR_RIDE): a caragor's counterable leap ('AI_Warg_JumpAttack') parried
 *     by the blade across its line presses the game's counter even with SOMVR_DEFENCE_PARRY=recoil (the build's choice
 *     for orcs): the counter stuns the caragor - the one the left hand then mounts (Big Game's lesson).
 *   SOMVR_RIDE_TRACE=N (tests): N lines at most of the view's per-frame numbers while mounted (RIDE view).
 *   Test words (SOMVR_HANDS_TEST): `ride status`, `ride guard on|off` (the test right hand holds the blade across a
 *   caragor's line - its head to Talion's body, 45 cm out - from 0.3 s before its parry window to its end: the player's
 *   block of a leap; DEFENCE judges it as it judges a player's), `ride face on|off` (the view turned to a stunned
 *   caragor within 8 m, as a player looks at it), `ride press CMD MS` (one mounted command held); build 27c: `ride run`,
 *   `ride sprint`, `ride tap`, `ride trace` (re-xr-pipeline.md 122).
 * The mount (build 26): the countered caragor lies stunned ('AI_Warg_Knockdown_*', ~10 s); while it is in reach and view
 * the game offers 'UI_MountWarg_Dominate' on 123 (the pad's RB) - the left hand reached out (30 cm or more from the eyes,
 * in the front half of the view) and closed, or B, presses 123 for 0.5 s: the game runs Talion onto it and mounts;
 * the view is held from the press (the run and the leap are not shown as motion) until he sits, then blinked.
 * Lines: CONFIG ride=, RIDE t=.. mounted|seated|dismounted|off, RIDE bindings, RIDE press, RIDE view (trace), RIDE
 * motion (every 2 s seated), RIDE guard, RIDE END. */

#define MT_P_MOUNT 0x1d8 /* Player: the mount HOBJECT (re-traversal.md 3.1; FB_MOUNT's proof) */
#define MT_C_ATTACK 0x4d0 /* Combat: the current attack record */
#define MT_CMD_MOUNT 181
#define MT_CMD_GETOFF 182
#define MT_CMD_JUMP 183
#define MT_CMD_CHARGE 184
#define MT_CMD_ATTACK1 185
#define MT_CMD_BITE 186     /* MNT_Attack_2: the ridden caragor's bite (build 27) */
#define MT_CMD_JUMP_A 16    /* Jump: the stock pad A (build 27; MNT_Jump has no record) */
#define MT_CMD_EVADE_A 121  /* CombatPrimary04: the stock pad A's other */
#define MT_CMD_OFF_PAD 123  /* CombatSecondary02, the pad's RB: off (build 27; MNT_GetOff has no record) */
#define MT_P_RIDER_CMDS 0x2b0 /* Player: MountInfo+0xf8, the rider's commands {int id; float value}[] (re-traversal.md */
#define MT_P_RIDER_NCMD 0x2c0 /* 3.1: filled each frame from the mount's data and the active commands), its count */
#define MT_RIDER_MAX 24
#define MT_HOLD_MAX_S 6.0  /* getting on or off: held at most this long */
#define MT_KIND_WAIT_S 8.0 /* a mount whose kind is not known yet: asked again this long, then not a beast (build 27:
			    * 2.0 s lost a checkpoint's restore on the beast - the BEASTS cache knew it 2.9 s after, boot
			    * 20261030-061050; meanwhile the mount is the window, as a grab's) */
#define MT_GUARD_OUT_CM 45.0f
#define MT_SPR_MAX 4 /* the sprint's commands at most (SOMVR_RIDE_SPRINT_CMDS, `ride run`) */
#define MT_LOG_MAX 800 /* RIDE lines at most (build 27c: 400 ran out 25 minutes into Big Game, boot 20261030-220337) */
#define MT_SPR_TAP_S 0.1 /* a tapped sprint command: on this long of each SOMVR_RIDE_SPRINT_PULSE_MS */
#define MT_SPR_DEFAULT "16,121,184" /* SOMVR_RIDE_SPRINT_CMDS: the stock pad's A held (Jump, CombatPrimary04, MNT_Charge) */
#define MT_SPR_EDGE_S 0.1 /* A's press or release inside the click's sprint: the shared commands let go this long first */
#define MT_SPR_PULSE_DEFAULT 0 /* SOMVR_RIDE_SPRINT_PULSE_MS: held */
#define MT_VT_STRUGGLE_AIM 0x3592bb8 /* CStruggleAim's vtable (index/vfuncs.tsv; the RTTI '12CStruggleAim') */
#define MT_STRUG_DONE 0xa5           /* ICombatStruggle: its completion asked (FUN_00e114e0 sets it: the game's own; the */
				     /* aim's next update wins - +0x8c = 1, its success UI, the end callback) */

enum { MT_OFF = 0, MT_MOUNTING, MT_SEATED, MT_DISMOUNTING };
static const char *const g_rd_state_names[] = { "off", "getting on", "seated", "getting off" };

static struct {
	int on, align, guard, trace_max, counter, face, seat_on;
	int win_rest, in_win;               /* build 27c: SOMVR_RIDE_WIN_REST; a window open last tick */
	uint32_t raw_prev, win_mask, rest_bits; /* ... the buttons last tick (unmasked), held since before the window, resting */
	uint64_t win_rests;                 /* windows the mounted buttons rested through */
	double press_mount_until, mount_wait_until; /* the mount's 123 pressed until; the view held until (the run onto it) */
	int sq_dn, b_was, offered_was, dom_was, dom_hold_was;
	uint64_t dom_holds;
	uintptr_t strug_last[2];
	/* the domination's struggle (build 27): its object, class, command and the game's own counters (bm_pre's read) */
	uintptr_t sg_obj;
	int sg_kind, sg_cmd, sg_need, sg_count, sg_res, sg_held;
	float sg_dur;
	_Atomic uint64_t sg_serial;
	uint64_t sg_serial_in, sg_ends, sg_won;
	somvr_rd_strug sg;
	double sg_t_last, sg_hap_next;
	int sg_on_was, sg_won_was;
	float dom_hold_s;                   /* SOMVR_RIDE_DOM_HOLD_MS: an aim struggle held on this long = won */
	_Atomic uintptr_t sg_win_for;       /* the struggle the grip won (the input tick's), completed by bm_pre */
	uintptr_t sg_forced;
	/* build 27: the rider's command list (what the beast reads) and the beast's own attack record, for the log */
	int rc_n, rc_id[MT_RIDER_MAX], rc_listed;
	float rc_v[MT_RIDER_MAX];
	uintptr_t beast_atk;
	uint64_t diag_lines, motion_lines; /* the rider's/ridden lines (120) and RIDE motion (every 2 s for 20, then 10 s) */
	uint64_t offers, mount_presses[2], faces;
	float settle_s;
	somvr_rd_cfg cfg;
	somvr_rd_btn_cfg bcfg;
	somvr_anchor_cfg acfg[SOMVR_RD_NKIND];
	int state, kind, kind_src;          /* kind_src: 0 none, 1 the beast cache, 2 Talion's record */
	uintptr_t mount, mount_last, atk;   /* atk: Talion's attack record (bm_pre's read) */
	uintptr_t sync;                     /* Talion's sync record (the fallback's read) */
	int struggle;
	double t_change, t_seated, t_last, t_sync_over, t_off;
	int beast_ok;                       /* the mount is a beast (riding), not the game's grab */
	somvr_rd_yaw yaw;
	somvr_rd_seat seat;
	somvr_anchor a;
	somvr_anchor_out ao;
	somvr_rd_btn btn;
	uint32_t held;
	int owns;                           /* the buttons are the ride's this tick */
	float body[3], seat_raw[3], beast[3], heading, dyaw, rate_dps;
	int have_body;
	int align_pending;
	float last_base[3], last_yaw;
	uint64_t last_frame;
	int have_last;
	_Atomic int req_status, req_press_cmd, req_press_ms;
	_Atomic int steer_on;               /* `ride steer X Z` (a test): seated, the stick toward a world point */
	float steer_x, steer_z;
	int press_cmd;
	double press_until;
	/* the guard (a test) */
	uintptr_t g_c;
	uint64_t g_serial;
	int g_placed;
	float g_rest[6], g_last[6];
	double g_t0;
	uint64_t g_guards;
	/* numbers */
	uint64_t mounts, dismounts, not_beast, seated_frames, trace_lines, lines, suppressed, aligns, presses[8];
	float step_max, ystep_max, yaw_step_max, yaw_dps_max, raw_y_span[2], out_y_span[2], pitch_max, roll_max;
	double next_motion;
	float win_step_max, win_ystep_max, win_yaw_max;
	uint64_t win_frames;
	/* build 27c (som-b27-ride3, re-xr-pipeline.md 122): the beast's speed (seated), the sprint's commands, `ride run` */
	somvr_rd_speed spd, run_spd;
	float win_spd_top;
	int spr_cmd[MT_SPR_MAX], spr_n, spr_pulse_ms, spr_was;
	double spr_t0, spr_s;
	uint64_t sprints;
	_Atomic int req_run_ms, req_sprint, tap_until_ms;
	int a_pad, rt_cmd[MT_SPR_MAX], rt_n, tap_cmd[MT_SPR_MAX], tap_n; /* SOMVR_RIDE_A; `ride tap` (a test) */
	double edge_until, tap_until;
	uint64_t edges;
	int rs_cmd[MT_SPR_MAX], rs_n, rs_pulse_ms; /* `ride sprint` (a test) */
	int rq_cmd[MT_SPR_MAX], rq_n, rq_pulse_ms;
	float rq_mag;
	int run_cmd[MT_SPR_MAX], run_n, run_pulse_ms, run_ms, run_have1, run_nrec, run_nrc, run_rc[8];
	float run_mag, run_bearing;
	double run_t0, run_until, run_d1, run_t1, t_tick;
	uintptr_t run_rec[4];
} g_mt;

static void mt_state(uintptr_t mount, double t);
static int mt_riding(void);
static const char *atk_name_or_dash(uintptr_t r, char *nm, size_t n) { return r ? fb_rec_name(r, nm, n) : "-"; }

static int mt_log_ok(void)
{
	if (g_mt.lines < MT_LOG_MAX)
		return ++g_mt.lines;
	g_mt.suppressed++;
	return 0;
}

/* "184" / "184,16,121" / "0" (none) -> the commands (each 1..SC_NCMD-1); -1 = not such a list */
static int mt_cmd_list(const char *s, int *out, int cap)
{
	int n = 0;
	if (!s || !strcmp(s, "0") || !strcmp(s, "none"))
		return 0;
	while (*s) {
		char *e;
		long v = strtol(s, &e, 10);
		if (e == s || v <= 0 || v >= SC_NCMD || n >= cap || (*e && *e != ','))
			return -1;
		out[n++] = (int)v, s = *e ? e + 1 : e;
	}
	return n;
}

static void mt_config(int *bad)
{
	memset(&g_mt, 0, sizeof g_mt);
	somvr_rd_speed_init(&g_mt.spd), somvr_rd_speed_init(&g_mt.run_spd);
	static const char *const onoff_w[] = { "off", "on" };
	g_mt.on = env_word("SOMVR_RIDE", onoff_w, 2, 0);
	somvr_rd_cfg_default(&g_mt.cfg);
	somvr_rd_cfg *c = &g_mt.cfg;
	float ys[2] = { c->y_s[0] * 1000, c->y_s[1] * 1000 };
	env_vec("SOMVR_RIDE_Y_MS", ys, 2, 0, 1000, bad);
	c->y_s[0] = ys[0] / 1000, c->y_s[1] = ys[1] / 1000;
	c->yaw_s = env_float("SOMVR_RIDE_YAW_MS", c->yaw_s * 1000, 0, 1000, bad) / 1000;
	c->yaw_dps = env_float("SOMVR_RIDE_YAW_DPS", c->yaw_dps, 0, 720, bad);
	c->off_s = env_float("SOMVR_RIDE_SEAT_S", c->off_s, 0, 30, bad);
	env_vec("SOMVR_RIDE_SEAT_CARAGOR", c->seat[SOMVR_RD_CARAGOR], 3, -300, 600, bad);
	env_vec("SOMVR_RIDE_SEAT_GRAUG", c->seat[SOMVR_RD_GRAUG], 3, -300, 800, bad);
	g_mt.settle_s = env_float("SOMVR_RIDE_SETTLE_MS", 0, 0, 2000, bad) / 1000;
	g_mt.align = env_word("SOMVR_RIDE_ALIGN", onoff_w, 2, 1);
	g_mt.counter = g_mt.on && env_word("SOMVR_RIDE_COUNTER", onoff_w, 2, 1);
	g_mt.seat_on = env_word("SOMVR_RIDE_SEAT", onoff_w, 2, 1);
	g_mt.win_rest = env_word("SOMVR_RIDE_WIN_REST", onoff_w, 2, 1);
	g_mt.dom_hold_s = env_float("SOMVR_RIDE_DOM_HOLD_MS", 1500, 0, 4500, bad) / 1000;
	g_mt.trace_max = (int)env_float("SOMVR_RIDE_TRACE", 0, 0, 100000, bad);
	{ /* build 27c (som-b27-ride3, re-xr-pipeline.md 122): what the sprint holds */
		const char *sc = getenv("SOMVR_RIDE_SPRINT_CMDS");
		int n = mt_cmd_list(sc && sc[0] ? sc : MT_SPR_DEFAULT, g_mt.spr_cmd, MT_SPR_MAX);
		if (n < 0) {
			logf_("WARN SOMVR_RIDE_SPRINT_CMDS=%s is not up to %d commands separated by commas (or 0): the default %s", sc,
			      MT_SPR_MAX, MT_SPR_DEFAULT), *bad = 1;
			n = mt_cmd_list(MT_SPR_DEFAULT, g_mt.spr_cmd, MT_SPR_MAX);
		}
		g_mt.spr_n = n;
		g_mt.spr_pulse_ms = (int)env_float("SOMVR_RIDE_SPRINT_PULSE_MS", MT_SPR_PULSE_DEFAULT, 0, 2000, bad);
		g_mt.a_pad = env_word("SOMVR_RIDE_A", (const char *const[]){ "leap", "pad" }, 2, 1);
		if (g_mt.spr_pulse_ms > 0 && g_mt.spr_pulse_ms < 2 * MT_SPR_TAP_S * 1000)
			g_mt.spr_pulse_ms = (int)(2 * MT_SPR_TAP_S * 1000);
	}
	g_mt.bcfg.trig_on = 0.6f, g_mt.bcfg.trig_off = 0.4f;
	for (int k = 0; k < SOMVR_RD_NKIND; k++) { /* the root anchor's filter on the seat (somvr_loco.h) */
		somvr_anchor_cfg_default(&g_mt.acfg[k]);
		g_mt.acfg[k].eye_cm = 0, g_mt.acfg[k].y_s = c->y_s[k] > 0.005f ? c->y_s[k] : 0.005f;
		g_mt.acfg[k].jump_cm = 200; /* a caragor's sprint at 20 Hz: 75 cm a frame */
	}
	if (!g_mt.on)
		return;
	if (!g_bm.on) /* (without SOMVR_INPUT the seat works; no turn, no mounted buttons) */
		logf_("WARN SOMVR_RIDE=on needs SOMVR_BODYMOMENTS=1 (the holds of getting on and off, the mount's conditions)"),
			*bad = 1;
	logf_("CONFIG ride=on (SOMVR_RIDE, build 26): Seat mode - the seat = the beast's root + the rider's eyes (caragor "
	      "%.0f,%.0f,%.0f, graug %.0f,%.0f,%.0f cm right/up/ahead; Talion's head node's mean over %.1f s, at most %.0f cm/s), "
	      "the vertical filter %.0f / %.0f ms, the yaw car-style (low-pass %.0f ms, at most %.0f deg/s), the hold until "
	      "seated + %.0f ms, the facing on the blink's black %s; A (SOMVR_RIDE_A), RT = MNT_Attack_2, B = 123 (the pad RB: off), the "
	      "stick's click = the sprint (SOMVR_RIDE_SPRINT_CMDS); a caragor's leap parried = the game's counter %s; the domination's struggle held on %.0f ms "
	      "(SOMVR_RIDE_DOM_HOLD_MS); Seat mode %s (SOMVR_RIDE_SEAT; off: a mount is the window, as build 25); trace %d", c->seat[0][0], c->seat[0][1], c->seat[0][2], c->seat[1][0], c->seat[1][1],
	      c->seat[1][2], c->off_s, c->off_cmps, c->y_s[0] * 1000, c->y_s[1] * 1000, c->yaw_s * 1000, c->yaw_dps,
	      g_mt.settle_s * 1000, g_mt.align ? "on" : "off", g_mt.counter ? "on" : "off", g_mt.dom_hold_s * 1000, g_mt.seat_on ? "on" : "off",
	      g_mt.trace_max);
	logf_("CONFIG ride win_rest=%s (SOMVR_RIDE_WIN_REST, build 27c%s): %s", g_mt.win_rest ? "on" : "off",
	      g_mt.win_rest ? ", the default" : "", g_mt.win_rest ? "while a window is open (a presentation, a failed or "
	      "result screen) a press is the screen's (SOMVR_USE: skip, Retry, Back, Abandon), never the beast's - it acts only "
	      "once let go after it; a button held since before the window goes on (a held run through a banner), never B" :
	      "the mounted buttons act through a window (build 27: B on a screen got Talion off, A's Retry leapt)");
	char cl[64];
	size_t n = 0;
	cl[0] = 0;
	for (int k = 0; k < g_mt.spr_n && n < sizeof cl - 8; k++)
		n += (size_t)snprintf(cl + n, sizeof cl - n, "%s%d", k ? "," : "", g_mt.spr_cmd[k]);
	logf_("CONFIG ride sprint=%s (SOMVR_RIDE_SPRINT_CMDS, SOMVR_RIDE_SPRINT_PULSE_MS %d; build 27c): the left stick's click "
	      "latches the beast's sprint while the stick stays pushed (the on-foot sprint's rule) - %s %s; A=%s (SOMVR_RIDE_A): "
	      "%s", n ? cl : "none", g_mt.spr_pulse_ms, n ? "its commands" : "nothing sent", !n ? "" : g_mt.spr_pulse_ms ?
	      "tapped" : "held", g_mt.a_pad ? "pad" : "leap", g_mt.a_pad ? "the stock pad's A (Jump + CombatPrimary04 + "
	      "MNT_Charge): tapped the leap, held the sprint" : "Jump + CombatPrimary04 (build 27: held, the beast stands)");
}

static int mt_riding(void) { return g_mt.on && g_mt.beast_ok && g_mt.state != MT_OFF; }
static int mt_seated(void) { return mt_riding() && g_mt.state == MT_SEATED; }

/* the kind of the mount: the beast cache (BEASTS saw it near), else Talion's attack or sync record ('PC_Warg_*' a
 * caragor's mount, '*Troll*' a graug's); -1 = not known (yet) */
static int mt_kind_of(uintptr_t mount, int *src)
{
	*src = 0;
	for (int i = 0; i < BZ_MAX; i++)
		if (g_bz.b[i].hobj == mount && g_bz.b[i].c && (g_bz.b[i].kind == SOMVR_BZ_CARAGOR || g_bz.b[i].kind == SOMVR_BZ_GRAUG))
			return *src = 1, g_bz.b[i].kind == SOMVR_BZ_GRAUG ? SOMVR_RD_GRAUG : SOMVR_RD_CARAGOR;
	char nm[96];
	for (int k = 0; k < 2; k++) {
		uintptr_t r = k ? g_mt.sync : g_mt.atk;
		int kd = r ? somvr_rd_kind(fb_rec_name(r, nm, sizeof nm)) : -1;
		if (kd >= 0)
			return *src = 2, kd;
	}
	return -1;
}

/* the mounted commands as the game binds them (logged once per mount: the pad's names, the record, active now) */
static void mt_bindings(void)
{
	char b[600];
	size_t o = 0;
	in_active_fn active = g_inp.on && g_inp.pi ? IN_FN(IS_CMD_ACTIVE, in_active_fn) : NULL;
	for (int c = MT_CMD_MOUNT; c <= 188 && o < sizeof b - 80; c++) {
		o += (size_t)snprintf(b + o, sizeof b - o, " %d '%s' rec %#lx first %d active %d pad", c, in_cmd_name(c),
				      (unsigned long)g_inp.rec[c], g_inp.first[c], active ? (int)(active(g_inp.pi, (unsigned)c) & 0xff) : -1);
		int any = 0;
		for (int i = 0; i < SC_NPAD; i++)
			for (int k = 0; k < g_inp.nmap[i]; k++)
				if (g_inp.map[i][k].cmd == c && o < sizeof b - 12)
					o += (size_t)snprintf(b + o, sizeof b - o, " %s", sc_pad_name(i)), any = 1;
		if (!any && o < sizeof b - 8)
			o += (size_t)snprintf(b + o, sizeof b - o, " -");
		if (o < sizeof b - 2)
			b[o++] = ';', b[o] = 0;
	}
	logf_("RIDE bindings:%s", b);
}

/* the struggle's fields by its class (the delegates' and updates' reads, re-xr-pipeline.md 109.2): CStruggleHold the
 * command u16 +0xfc, the result +0xfe, the hold's seconds +0x100, held +0x118; CStrugglePressCount the command +0x2f0,
 * the presses needed +0x2f4 and made +0x2f5, the result +0x2f6; CStruggleQuickTime the command +0xd0; CStruggleAim
 * +0x58c */
static void mt_struggle_fields(uintptr_t o, int kind)
{
	uint8_t b[4] = { 0 };
	uint16_t w = 0;
	uint32_t d = 0;
	float f = 0;
	g_mt.sg_need = g_mt.sg_count = g_mt.sg_res = g_mt.sg_held = 0, g_mt.sg_dur = 0;
	switch (kind) {
	case SOMVR_RDS_HOLD:
		if (peek(o + 0xfc, &w, sizeof w))
			g_mt.sg_cmd = w;
		if (peek(o + 0xfe, b, 1))
			g_mt.sg_res = b[0];
		if (peek(o + 0x100, &f, sizeof f) && isfinite(f))
			g_mt.sg_dur = f;
		if (peek(o + 0x118, b, 1))
			g_mt.sg_held = b[0];
		break;
	case SOMVR_RDS_COUNT:
		if (peek(o + 0x2f0, &d, sizeof d))
			g_mt.sg_cmd = (int)d;
		if (peek(o + 0x2f4, b, 3))
			g_mt.sg_need = b[0], g_mt.sg_count = b[1], g_mt.sg_res = b[2];
		break;
	case SOMVR_RDS_QT:
		if (peek(o + 0xd0, &d, sizeof d))
			g_mt.sg_cmd = (int)d;
		break;
	case SOMVR_RDS_AIM:
		if (peek(o + 0x58c, &d, sizeof d))
			g_mt.sg_cmd = (int)d;
		break;
	}
}

/* a struggle appeared on Talion (mt_fb's RTTI read): the one the domination answers */
static void mt_struggle_seen(int k, uintptr_t o, const char *cn, double t)
{
	int kind = o ? somvr_rd_strug_kind(cn) : SOMVR_RDS_NONE;
	if (!o || (g_mt.sg_obj && g_mt.sg_obj != o && kind == SOMVR_RDS_NONE))
		return;
	g_mt.sg_obj = o, g_mt.sg_kind = kind, g_mt.sg_cmd = 0;
	mt_struggle_fields(o, kind);
	atomic_fetch_add(&g_mt.sg_serial, 1);
	if (mt_log_ok())
		logf_("RIDE t=%.3f the struggle (Talion's %s, %#lx '%s'): %s of %d '%s' (the hold %.2f s; presses needed %d); the "
		      "answer: %s", t, k ? "sync" : "state", (unsigned long)o, cn, somvr_rd_strug_names[kind], g_mt.sg_cmd,
		      g_mt.sg_cmd > 0 && g_mt.sg_cmd < SC_NCMD ? in_cmd_name(g_mt.sg_cmd) : "?", g_mt.sg_dur, g_mt.sg_need,
		      kind == SOMVR_RDS_HOLD ? "its command held while the left hand stays closed (or B is held)" :
		      kind == SOMVR_RDS_COUNT || kind == SOMVR_RDS_QT ? "its command pressed at each fresh close of the left hand (or B)" :
		      kind == SOMVR_RDS_AIM || kind == SOMVR_RDS_DIR ? "the left hand kept closed (or B held) - held on = won" :
		      "none (a class not known: 123 held while the left hand stays closed)");
}

/* bm_pre: the answered struggle's counters while it lasts; its end logged with the last values */
static void mt_struggle_read(const somvr_fb_in *in, double t)
{
	uintptr_t o = g_mt.sg_obj;
	if (!o)
		return;
	if ((uintptr_t)in->struggle[0] == o || (uintptr_t)in->struggle[1] == o) {
		mt_struggle_fields(o, g_mt.sg_kind);
		if (atomic_load(&g_mt.sg_win_for) == o && g_mt.sg_forced != o) { /* held on: the game's own completion */
			static const uint8_t one = 1;
			char nm[96];
			int ok = peek_ptr(o) == MT_VT_STRUGGLE_AIM && arm_poke(o + MT_STRUG_DONE, &one, 1);
			g_mt.sg_forced = o, g_mt.sg_won += ok;
			if (mt_log_ok())
				logf_("RIDE t=%.3f the struggle %#lx held on %.1f s by the grip: %s (Talion's '%s')", t, (unsigned long)o,
				      g_mt.dom_hold_s, ok ? "completed as the game's own (ICombatStruggle+0xa5: its next update wins)" :
				      "NOT completed (not the aim's vtable, or the write failed)", atk_name_or_dash(g_mt.atk, nm, sizeof nm));
		}
		return;
	}
	g_mt.sg_ends++;
	char nm[96];
	if (mt_log_ok())
		logf_("RIDE t=%.3f the struggle %#lx ended (%s): the game's last result %d, held %d, presses %d of %d; ours: %llu fresh "
		      "presses; Talion's '%s'", t, (unsigned long)o, somvr_rd_strug_names[g_mt.sg_kind], g_mt.sg_res, g_mt.sg_held,
		      g_mt.sg_count, g_mt.sg_need, (unsigned long long)g_mt.sg.presses, atk_name_or_dash(g_mt.atk, nm, sizeof nm));
	g_mt.sg_obj = 0, g_mt.sg_kind = SOMVR_RDS_NONE, g_mt.sg_cmd = 0;
	atomic_fetch_add(&g_mt.sg_serial, 1);
}

/* bm_pre while riding: the rider's command list (the ids the beast reads, their values) and the beast's attack record -
 * which press reaches it (build 27: MNT_Jump and MNT_GetOff have no input record in the PC set, 109) */
static void mt_rider_log(double t)
{
	if (!mt_riding() || g_mt.state == MT_MOUNTING) {
		g_mt.rc_listed = 0, g_mt.rc_n = 0, g_mt.beast_atk = 0;
		return;
	}
	uintptr_t P = g_fr.pl, arr = P ? peek_ptr(P + MT_P_RIDER_CMDS) : 0;
	uint32_t n = 0;
	struct { int32_t id; float v; } e[MT_RIDER_MAX];
	if (arr && peek(P + MT_P_RIDER_NCMD, &n, sizeof n) && n > 0 && n <= MT_RIDER_MAX && peek(arr, e, n * sizeof e[0])) {
		if (!g_mt.rc_listed) {
			char b[700];
			size_t o = 0;
			for (uint32_t k = 0; k < n && o < sizeof b - 60; k++)
				o += (size_t)snprintf(b + o, sizeof b - o, " %d '%s' %.2f;", e[k].id, e[k].id > 0 && e[k].id < SC_NCMD ?
						      in_cmd_name(e[k].id) : "?", e[k].v);
			g_mt.rc_listed = 1;
			if (mt_log_ok())
				logf_("RIDE t=%.3f the rider's commands (P+0x2b0, %u):%s", t, n, b);
		}
		for (uint32_t k = 0; k < n; k++) {
			int was = (int)k < g_mt.rc_n && g_mt.rc_id[k] == e[k].id ? g_mt.rc_v[k] != 0 : 0;
			if ((e[k].v != 0) != was && g_mt.diag_lines < 120 && ++g_mt.diag_lines && mt_log_ok())
				logf_("RIDE t=%.3f the rider's %d '%s' %s (%.2f)", t, e[k].id, e[k].id > 0 && e[k].id < SC_NCMD ?
				      in_cmd_name(e[k].id) : "?", e[k].v != 0 ? "on" : "off", e[k].v);
			g_mt.rc_id[k] = e[k].id, g_mt.rc_v[k] = e[k].v;
			if (g_mt.run_until > 0 && e[k].v != 0) { /* `ride run`: the rider's commands seen on */
				int have = 0;
				for (int i = 0; i < g_mt.run_nrc; i++)
					have |= g_mt.run_rc[i] == e[k].id;
				if (!have && g_mt.run_nrc < 8)
					g_mt.run_rc[g_mt.run_nrc++] = e[k].id;
			}
		}
		g_mt.rc_n = (int)n;
	}
	uintptr_t a = 0;
	for (int i = 0; i < BZ_MAX; i++)
		if (g_bz.b[i].hobj == g_mt.mount && g_bz.b[i].c)
			a = peek_ptr(g_bz.b[i].c + BZ_C_ATTACK);
	if (a && g_mt.run_until > 0) { /* `ride run`: the beast's records seen */
		int have = 0;
		for (int i = 0; i < g_mt.run_nrec; i++)
			have |= g_mt.run_rec[i] == a;
		if (!have && g_mt.run_nrec < 4)
			g_mt.run_rec[g_mt.run_nrec++] = a;
	}
	if (a != g_mt.beast_atk) {
		char nm[96];
		g_mt.beast_atk = a;
		if (a && g_mt.diag_lines < 120 && ++g_mt.diag_lines && mt_log_ok())
			logf_("RIDE t=%.3f the ridden %s: '%s'", t, somvr_rd_kind_names[g_mt.kind >= 0 ? g_mt.kind : 0],
			      fb_rec_name(a, nm, sizeof nm));
	}
}

/* bm_pre (every H4, before the moment's conditions): Talion's attack record and the fallback's read; while riding a
 * beast the conditions mount and attach are the ride's, and while seated a sync or struggle on Talion (a mounted
 * attack's) is the beast's too - the seat is its, not his */
static uint32_t mt_fb(uint32_t fb, const somvr_fb_in *in, uintptr_t atk, double t)
{
	if (!g_mt.on)
		return fb;
	g_mt.atk = atk, g_mt.sync = in->sync, g_mt.struggle = in->struggle[0] || in->struggle[1];
	mt_struggle_read(in, t);
	for (int k = 0; k < 2; k++) { /* a struggle on Talion: its class by the object's RTTI (which input it counts) */
		uintptr_t o = (uintptr_t)in->struggle[k];
		if (o == g_mt.strug_last[k])
			continue;
		g_mt.strug_last[k] = o;
		uintptr_t vt = o ? peek_ptr(o) : 0, ti = vt ? peek_ptr(vt - 8) : 0, np = ti ? peek_ptr(ti + 8) : 0;
		char cn[48] = "", an[96];
		if (np)
			peek(np, cn, sizeof cn - 1);
		uint32_t wv[16] = { 0 };
		if (o)
			peek(o + 8, wv, sizeof wv);
		mt_struggle_seen(k, o, cn, t);
		if (o && mt_log_ok())
			logf_("RIDE t=%.3f Talion's %s struggle %#lx: '%s' (vtable %#lx; Talion's '%s'); words %x %x %x %x %x %x %x %x %x %x "
			      "%x %x %x %x %x %x", t, k ? "sync" : "state", (unsigned long)o, cn, (unsigned long)vt,
			      atk ? fb_rec_name(atk, an, sizeof an) : "-", wv[0], wv[1], wv[2], wv[3], wv[4], wv[5], wv[6], wv[7], wv[8],
			      wv[9], wv[10], wv[11], wv[12], wv[13], wv[14], wv[15]);
	}
	mt_state((in->have & SOMVR_FBR_MOUNT) ? in->mount : 0, t);
	mt_rider_log(t);
	if (!mt_riding() || !g_mt.seat_on) /* (Seat mode off: the mount is the window, as build 25) */
		return fb;
	fb &= ~(SOMVR_FB_BIT(SOMVR_FB_MOUNT) | SOMVR_FB_BIT(SOMVR_FB_ATTACH));
	if (g_mt.state == MT_SEATED)
		fb &= ~(SOMVR_FB_BIT(SOMVR_FB_SYNC) | SOMVR_FB_BIT(SOMVR_FB_STRUGGLE));
	return fb;
}

/* bm_pre, after the conditions: getting on or off holds the view (the body moments' hold; its release eases or
 * blinks) */
static int mt_hold(double t)
{
	if (!g_mt.on)
		return 0;
	if (!mt_riding() && t < g_mt.mount_wait_until) { /* pressed: the game's run and leap onto the beast is not motion */
		char nm[96];
		if (g_mt.atk && (strstr(fb_rec_name(g_mt.atk, nm, sizeof nm), "Mount") || strstr(nm, "Domination")))
			g_mt.mount_wait_until = fmax(g_mt.mount_wait_until, t + 0.3);
		return 1;
	}
	if (mt_riding() && g_mt.seat_on)
		return g_mt.state != MT_SEATED || t - g_mt.t_seated < g_mt.settle_s;
	if (!g_mt.seat_on)
		return 0;
	return g_mt.t_off > 0 && t - g_mt.t_off < g_mt.settle_s; /* just off: until he stands */
}

/* bm_pre (every H4, at its top - before the frame's base): the mount and the state, so the frame the mount changes is
 * already held */
static void mt_state(uintptr_t mount, double t)
{
	if (mount != g_mt.mount_last) {
		if (mount) {
			g_mt.mount = mount, g_mt.t_change = t, g_mt.state = MT_MOUNTING, g_mt.beast_ok = 0, g_mt.kind = -1;
			g_mt.t_sync_over = 0;
			somvr_rd_yaw_init(&g_mt.yaw), somvr_rd_btn_init(&g_mt.btn), somvr_anchor_init(&g_mt.a);
			g_mt.held = 0, g_mt.have_last = 0;
		} else if (g_mt.mount_last) {
			if (g_mt.beast_ok) {
				g_mt.dismounts++;
				if (mt_log_ok()) {
					char nm[96];
					logf_("RIDE t=%.3f dismounted (P+0x1d8 cleared) after %.1f s on the %s; Talion's attack '%s'; the view held "
					      "until he stands", t, t - g_mt.t_change, somvr_rd_kind_names[g_mt.kind >= 0 ? g_mt.kind : 0],
					      g_mt.atk ? fb_rec_name(g_mt.atk, nm, sizeof nm) : "-");
				}
				g_mt.state = MT_DISMOUNTING, g_mt.t_change = t, g_mt.t_sync_over = 0;
			} else
				g_mt.state = MT_OFF;
			g_mt.mount = 0;
		}
		g_mt.mount_last = mount;
	}
	if (g_mt.state == MT_MOUNTING && !g_mt.beast_ok) { /* a beast, or the game's grab of an orc */
		int src, k = mt_kind_of(mount, &src);
		if (k >= 0) {
			g_mt.kind = k, g_mt.kind_src = src, g_mt.beast_ok = 1, g_mt.mounts++;
			somvr_rd_seat_init(&g_mt.seat, &g_mt.cfg, k);
			char nm[2][96];
			float p[3] = { 0, 0, 0 };
			peek(mount + OBJ_POS, p, sizeof p);
			if (mt_log_ok())
				logf_("RIDE t=%.3f mounted: a %s (hobj %#lx, by %s) at %.0f,%.0f,%.0f; Talion's attack '%s' sync '%s'; the view "
				      "held until he sits", t, somvr_rd_kind_names[k], (unsigned long)mount, src == 1 ? "the beast cache" :
				      "Talion's record", p[0], p[1], p[2], g_mt.atk ? fb_rec_name(g_mt.atk, nm[0], sizeof nm[0]) : "-",
				      g_mt.sync ? fb_rec_name(g_mt.sync, nm[1], sizeof nm[1]) : "-");
			mt_bindings();
		} else if (t - g_mt.t_change > MT_KIND_WAIT_S) {
			g_mt.state = MT_OFF, g_mt.not_beast++;
			if (mt_log_ok())
				logf_("RIDE t=%.3f the mount %#lx is not a beast (the game's grab of an orc?): left alone", t, (unsigned long)mount);
		}
	}
	int busy = g_mt.sync || g_mt.struggle;
	if ((g_mt.state == MT_MOUNTING && g_mt.beast_ok) || g_mt.state == MT_DISMOUNTING) {
		if (busy)
			g_mt.t_sync_over = 0;
		else if (!g_mt.t_sync_over)
			g_mt.t_sync_over = t;
		int done = (g_mt.t_sync_over && t - g_mt.t_sync_over >= 0.1) || t - g_mt.t_change > MT_HOLD_MAX_S;
		if (done && g_mt.state == MT_MOUNTING) {
			g_mt.state = MT_SEATED, g_mt.t_seated = t, g_mt.align_pending = g_mt.align;
			if (mt_log_ok())
				logf_("RIDE t=%.3f seated on the %s %.2f s after the mount (%s): Seat mode", t, somvr_rd_kind_names[g_mt.kind],
				      t - g_mt.t_change, busy ? "the hold's limit" : "his sync over");
		} else if (done) {
			g_mt.state = MT_OFF, g_mt.t_off = t, g_mt.beast_ok = 0;
			if (mt_log_ok())
				logf_("RIDE t=%.3f off: standing %.2f s after the dismount", t, t - g_mt.t_change);
		}
	}
}

/* eye0_setup's top (a stereo frame, before the turn): seated, the beast, the yaw's turn, the seat */
static void mt_frame(void)
{
	if (!g_mt.on)
		return;
	double t = fb_now(); /* the body moments' clock (their holds and this state share it) */
	float dt = g_mt.t_last > 0 && t > g_mt.t_last ? (float)(t - g_mt.t_last) : 0;
	g_mt.t_last = t;
	g_mt.have_body = 0, g_mt.dyaw = 0;
	if (g_mt.face && g_bd.test && !mt_riding() && g_inp.on && g_inp.head_ok) { /* `ride face on` (a test): the view turned */
		float best = 800, tgt[3];                                             /* to a stunned caragor (a player looks) */
		int have = 0;
		for (int i = 0; i < BZ_MAX; i++) {
			struct bz_one *e = &g_bz.b[i];
			char an[96];
			float p[3];
			if (!e->c || e->kind != SOMVR_BZ_CARAGOR || !peek(e->hobj + OBJ_POS, p, sizeof p) || !vec_ok(p, 1e7f))
				continue;
			fb_rec_name(peek_ptr(e->c + BZ_C_ATTACK), an, sizeof an);
			float dd = hypotf(p[0] - g_fr.head[0], p[2] - g_fr.head[2]);
			if ((strstr(an, "Knockdown") || strstr(an, "Sync_Cntr")) && dd < best)
				best = dd, memcpy(tgt, p, sizeof tgt), have = 1;
		}
		if (have) {
			float d = sv_wrap_pi(atan2f(tgt[0] - g_fr.head[0], tgt[2] - g_fr.head[2]) - g_inp.hv);
			if (fabsf(d) > 0.15f)
				g_inp.pend_turn = fmaxf(-0.5f, fminf(0.5f, g_inp.pend_turn + fmaxf(-0.25f, fminf(0.25f, d)))), g_mt.faces++;
		}
	}
	if (mt_riding()) /* the room-scale follow asks Talion for no step while he rides (his body is the saddle's) */
		g_follow.pm = 0;
	if (g_mt.state != MT_SEATED || !g_mt.seat_on) {
		g_mt.spd.n = 0, g_mt.spd.mps = NAN; /* (the next seat measures afresh) */
		return;
	}
	float q[4];
	if (!peek(g_mt.mount + OBJ_POS, g_mt.beast, sizeof g_mt.beast) || !vec_ok(g_mt.beast, 1e7f) ||
	    !peek(g_mt.mount + OBJ_ROT, q, sizeof q) || !vec_ok(q, 2.0f))
		return;
	g_mt.heading = sv_heading(sv_qnormalize(sv_quat_of(q)));
	float mps = somvr_rd_speed_step(&g_mt.spd, g_mt.beast, t); /* build 27c: the beast's speed (RIDE motion, run, END) */
	if (mps > g_mt.win_spd_top)
		g_mt.win_spd_top = mps;
	if (g_mt.run_until > 0) { /* `ride run` (a test): its own measure from its start */
		double d0 = g_mt.run_spd.dist_m, t0 = g_mt.run_spd.time_s;
		somvr_rd_speed_step(&g_mt.run_spd, g_mt.beast, t);
		if (g_mt.run_have1) /* after its first second (the beast up to speed) */
			g_mt.run_d1 += g_mt.run_spd.dist_m - d0, g_mt.run_t1 += g_mt.run_spd.time_s - t0;
	}
	g_mt.dyaw = somvr_rd_yaw_step(&g_mt.yaw, &g_mt.cfg, g_mt.heading, dt, &g_mt.rate_dps);
	if (g_inp.on && !(g_bm.on && g_bm.o.frozen)) /* the turn about the head at this frame (in_rig_turn) */
		g_inp.pend_turn = fmaxf(-0.5f, fminf(0.5f, g_inp.pend_turn + g_mt.dyaw));
	somvr_rd_seat_step(&g_mt.seat, &g_mt.cfg, g_mt.beast, g_mt.heading, g_fr.head_ok ? g_fr.head : NULL, dt, g_mt.seat_raw);
	somvr_anchor_in in;
	memset(&in, 0, sizeof in);
	in.t = t, in.now_ns = (int64_t)(t * 1e9), in.display_ns = g_views_fake ? 0 : g_fr.views.time;
	in.root = (sv_vec3){ g_mt.seat_raw[0], g_mt.seat_raw[1], g_mt.seat_raw[2] };
	somvr_anchor_step(&g_mt.a, &g_mt.acfg[g_mt.kind], &in, &g_mt.ao);
	g_mt.body[0] = g_mt.ao.pos.x, g_mt.body[1] = g_mt.ao.pos.y - g_rigcfg.eye_cm, g_mt.body[2] = g_mt.ao.pos.z;
	g_mt.have_body = vec_ok(g_mt.body, 1e7f);
}

/* in_dpsi (build 27, 109.5): seated in Seat mode the left stick steers in the beast's own frame - forward runs
 * straight on, sideways turns it (a car's wheel). Build 26 rotated it toward the head, as on foot: the beast steers to
 * where the stick points from the camera, so it turned toward the view while the view turned with it (car-style) - with
 * any offset between them, the beast and the view chased each other round at the yaw's cap (boot 20261030-052710: the
 * stick straight ahead, the caragor circling at ~130 deg/s for 4.5 s); looking around steered it too */
static int mt_steer_heading(float *h)
{
	if (!g_mt.on || !g_mt.seat_on || !mt_seated() || !g_mt.yaw.have)
		return 0;
	*h = g_mt.heading;
	return 1;
}

/* eye0_setup, the frame's body point: seated, the seat (the rig adds SOMVR_EYE_CM) */
static const float *mt_body(const float *body)
{
	return g_mt.on && mt_seated() && g_mt.have_body ? g_mt.body : body;
}

/* eye0_setup after the body moments' base: the facing on the blink's black, the view's numbers, the trace */
static void mt_after_base(void)
{
	int black = g_bm.on && g_bm.s.blink == SOMVR_BMB_BLACK, cut = g_fr.relatched;
	if (!g_mt.on)
		return;
	double t = fb_now();
	if (g_mt.align_pending && g_mt.seat_on && mt_seated() && black && g_rig.have_ref) { /* the blink's black: face the beast's head */
		sv_vec3 hp;
		sv_quat hq;
		somvr_rig_head(&g_rig, g_fr.base, g_fr.views.pose, &hp, &hq);
		float d = sv_wrap_pi(g_mt.heading - sv_heading(hq));
		sv_vec3 p0 = g_rig.p0, hx;
		sv_quat hxq;
		somvr_head_game(g_fr.views.pose, &hx, &hxq);
		sv_vec3 p1 = sc_turn_p0(g_rig.p0, hx, d);
		g_rig.p0.x = p1.x, g_rig.p0.z = p1.z, g_rig.yaw0 -= d;
		g_mt.align_pending = 0, g_mt.aligns++;
		if (mt_log_ok())
			logf_("RIDE t=%.3f the facing on the blink's black: the view turned %+.1f deg to the %s's heading (p0 %.1f,%.1f -> "
			      "%.1f,%.1f)", t, d * 57.29578f, somvr_rd_kind_names[g_mt.kind], p0.x, p0.z, g_rig.p0.x, g_rig.p0.z);
	}
	if (g_mt.align_pending && mt_seated() && t - g_mt.t_seated > MT_HOLD_MAX_S)
		g_mt.align_pending = 0; /* released by an ease (no black): the player's own facing stays */
	if (!mt_riding()) {
		g_mt.have_last = 0;
		return;
	}
	sv_vec3 hp;
	sv_quat hq;
	somvr_rig_head(&g_rig, g_fr.base, g_fr.views.pose, &hp, &hq);
	float b[3] = { g_fr.base.pos.x, g_fr.base.pos.y, g_fr.base.pos.z }, yaw = g_fr.base.yaw - g_rig.yaw0;
	if (g_mt.have_last && g_fr.frame != g_mt.last_frame + 1) /* stereo frames missed (a window: the flat quad): no step */
		g_mt.have_last = 0;
	g_mt.last_frame = g_fr.frame;
	float step = g_mt.have_last ? hypotf(b[0] - g_mt.last_base[0], b[2] - g_mt.last_base[2]) : 0,
	      ystep = g_mt.have_last ? fabsf(b[1] - g_mt.last_base[1]) : 0,
	      ys = g_mt.have_last ? fabsf(sv_wrap_pi(yaw - g_mt.last_yaw)) * 57.29578f : 0;
	/* the view's pitch and roll: the head's orientation in the world minus the tracked head's own (the base adds yaw
	 * only, so both stay the HMD's) */
	sv_vec3 f = sv_qrot(hq, (sv_vec3){ 0, 0, 1 }), up = sv_qrot(hq, (sv_vec3){ 0, 1, 0 });
	float pitch = asinf(fmaxf(-1, fminf(1, f.y))) * 57.29578f, roll = asinf(fmaxf(-1, fminf(1, (up.x * f.z - up.z * f.x) /
		      fmaxf(1e-3f, hypotf(f.x, f.z))))) * 57.29578f;
	int seated = g_mt.state == MT_SEATED && !(g_bm.on && (g_bm.o.frozen || g_bm.s.blink)) && !g_fr.rig_jump && !cut;
	if (seated && g_mt.have_last) {
		g_mt.seated_frames++, g_mt.win_frames++;
		g_mt.step_max = fmaxf(g_mt.step_max, step), g_mt.ystep_max = fmaxf(g_mt.ystep_max, ystep);
		g_mt.yaw_step_max = fmaxf(g_mt.yaw_step_max, ys), g_mt.yaw_dps_max = fmaxf(g_mt.yaw_dps_max, fabsf(g_mt.rate_dps));
		g_mt.win_step_max = fmaxf(g_mt.win_step_max, step), g_mt.win_ystep_max = fmaxf(g_mt.win_ystep_max, ystep);
		g_mt.win_yaw_max = fmaxf(g_mt.win_yaw_max, ys);
		g_mt.pitch_max = fmaxf(g_mt.pitch_max, fabsf(pitch)), g_mt.roll_max = fmaxf(g_mt.roll_max, fabsf(roll));
		float ry = g_mt.seat_raw[1] - g_mt.beast[1], oy = b[1] - g_mt.beast[1];
		if (g_mt.seated_frames == 1)
			g_mt.raw_y_span[0] = g_mt.raw_y_span[1] = ry, g_mt.out_y_span[0] = g_mt.out_y_span[1] = oy;
		g_mt.raw_y_span[0] = fminf(g_mt.raw_y_span[0], ry), g_mt.raw_y_span[1] = fmaxf(g_mt.raw_y_span[1], ry);
		g_mt.out_y_span[0] = fminf(g_mt.out_y_span[0], oy), g_mt.out_y_span[1] = fmaxf(g_mt.out_y_span[1], oy);
	}
	if (g_mt.trace_max && g_mt.trace_lines < (uint64_t)g_mt.trace_max) {
		g_mt.trace_lines++;
		logf_("RIDE view f=%llu t=%.4f %s beast %.1f,%.1f,%.1f hdg %.1f head %s%.1f,%.1f,%.1f seat %.1f,%.1f,%.1f base "
		      "%.1f,%.1f,%.1f yaw %.2f step %.1f cm y %.1f cm yaw %.2f deg (turn %+.2f, %.0f deg/s) pitch %.2f roll %.2f "
		      "%s%s", (unsigned long long)g_fr.frame, t, g_rd_state_names[g_mt.state], g_mt.beast[0], g_mt.beast[1],
		      g_mt.beast[2], g_mt.heading * 57.29578f, g_fr.head_ok ? "" : "(none) ", g_fr.head[0], g_fr.head[1],
		      g_fr.head[2], g_mt.seat_raw[0], g_mt.seat_raw[1], g_mt.seat_raw[2], b[0], b[1], b[2], yaw * 57.29578f, step,
		      ystep, ys, g_mt.dyaw * 57.29578f, g_mt.rate_dps, pitch, roll,
		      g_bm.on && g_bm.o.frozen ? "held " : "", g_bm.on && g_bm.s.blink ? "blink" : "");
	}
	if (seated && t >= g_mt.next_motion) {
		if (g_mt.win_frames && mt_log_ok())
			logf_("RIDE motion t=%.3f last %llu seated frames: the view's largest step %.1f cm across, %.1f cm up/down, "
			      "%.2f deg of yaw; the seat %.1f cm over the root (Talion's head node's mean), its raw span %.1f cm, the "
			      "view's %.1f cm; pitch/roll at most %.2f/%.2f deg (the base: a position and a yaw only); the beast at "
			      "%.1f m/s now, its top %.1f m/s (a 1-s window)%s", t,
			      (unsigned long long)g_mt.win_frames, g_mt.win_step_max, g_mt.win_ystep_max, g_mt.win_yaw_max,
			      g_mt.seat.off[1], g_mt.raw_y_span[1] - g_mt.raw_y_span[0], g_mt.out_y_span[1] - g_mt.out_y_span[0],
			      g_mt.pitch_max, g_mt.roll_max, isfinite(g_mt.spd.mps) ? g_mt.spd.mps : 0.0f, g_mt.win_spd_top,
			      (g_mt.held & SOMVR_RDB_CHARGE) ? "; sprinting" : "");
		g_mt.win_spd_top = 0;
		g_mt.next_motion = t + (g_mt.motion_lines++ < 20 ? 2.0 : 10.0), g_mt.win_frames = 0, /* (a long ride: 10 s) */ g_mt.win_step_max = g_mt.win_ystep_max = g_mt.win_yaw_max = 0;
	}
	memcpy(g_mt.last_base, b, sizeof b), g_mt.last_yaw = yaw, g_mt.have_last = 1;
}

/* DEFENCE's parry with SOMVR_DEFENCE_PARRY=recoil (an orc's counter moves the view; the build's choice): a caragor's
 * counterable leap ('AI_Warg_JumpAttack', not '_NoCounter') is the exception - the game's counter is what stuns it
 * (PC_Counter_Warg_*) and a stunned caragor is the one the left hand mounts (Big Game's "Counter (Y) a Caragor's leap
 * ... mount stunned Caragors"); the counter's sync is held and blinked by the body moments like any */
static int mt_counter_leap(uintptr_t c, uintptr_t attack)
{
	if (!g_mt.counter || !bz_line_judge(c))
		return 0;
	char an[96];
	rx_rec_name(attack, an, sizeof an);
	return !strncmp(an, "AI_Warg_JumpAttack", 18) && !strstr(an, "NoCounter");
}

/* the game offers the mount now: its prompt on 123 ('UI_MountWarg_Dominate' - a stunned caragor in reach and view) */
static int mt_offered(const char **str)
{
	for (int i = 0; i < g_vb.nup; i++)
		if (g_vb.up[i].act == VB_CMD_GRAB && strstr(g_vb.up[i].str, "Mount")) {
			if (str)
				*str = g_vb.up[i].str;
			return 1;
		}
	return 0;
}

/* the mount's press: 123 for 0.5 s (the game's own run and leap onto the beast follows), the view held meanwhile */
static void mt_mount_press(int by_b, float cm, float deg, const char *offer)
{
	double t = cb_now(), tf = fb_now();
	g_mt.press_mount_until = t + 0.5, g_mt.mount_wait_until = tf + 0.6, g_mt.mount_presses[by_b]++;
	if (mt_log_ok())
		logf_("RIDE t=%.3f the mount: %s at the game's offer '%s': 123 'CombatSecondary02' pressed (0.5 s); the view held "
		      "until he sits", t, by_b ? "B" : "the left hand reached out and closed", offer);
	if (!by_b && mt_log_ok())
		logf_("RIDE t=%.3f   (the left palm %.0f cm out from the eyes, %.0f deg off the view)", t, cm, deg);
}

/* in_frame (a stereo frame): the left hand reached out (30 cm or more from the eyes, in the front half of the view) and
 * closed while the game offers the mount = the mount (HL:A's reach and grab; the beast lies stunned up to ~6 m off,
 * the game runs Talion onto it) */
static void mt_reach(const sv_vec3 pw[2], sv_vec3 eye)
{
	if (!g_mt.on || mt_riding())
		return;
	const char *offer = "";
	int off = mt_offered(&offer);
	if (off && !g_mt.offered_was) {
		g_mt.offers++;
		if (mt_log_ok())
			logf_("RIDE t=%.3f the game offers the mount ('%s'): the left hand reached out and closed, or B", cb_now(), offer);
	}
	g_mt.offered_was = off;
	float sq = bd_squeeze(SOMVR_HAND_L, g_inp.st.hand[SOMVR_HAND_L].squeeze);
	int was = g_mt.sq_dn;
	g_mt.sq_dn = g_mt.sq_dn ? sq > 0.35f : sq > 0.7f;
	int live = g_inp.ctx == SC_CTX_WORLD && !game_paused() && !vm_open() && !bo_out() && !cl_holds(SOMVR_HAND_L);
	if (!off || !live || !g_mt.sq_dn || was || !g_inp.hands[SOMVR_HAND_L].valid)
		return;
	float cm, deg;
	if (somvr_rd_reached(pw[0].x - eye.x, pw[0].z - eye.z, g_inp.hv, &cm, &deg))
		mt_mount_press(0, cm, deg, offer);
}

/* in_apply: 123 while the mount's press lasts; through the domination that follows (Talion's record
 * 'PC_Warg_Domination_*') its struggle answered by the grip that took the beast (build 27, 109): the struggle's own
 * command (its class's field) held while the left hand stays closed or B is held (CStruggleHold), or pressed at each
 * fresh close (CStrugglePressCount, CStruggleQuickTime); let go for its first 0.1 s - the press must come after the
 * struggle began (build 26 held 123 on from the grab: no fresh press reached it); a class not known: 123 held */
static int mt_press_cmd(void)
{
	if (!g_mt.on)
		return 0;
	double t = cb_now();
	if (g_mt.press_cmd && t < g_mt.press_until) /* `ride press CMD MS` (a test) */
		return g_mt.press_cmd;
	if (mt_seated() && (g_mt.held & SOMVR_RDB_OFF)) /* B held while seated: the pad's RB, the game's get-off (109.4) */
		return MT_CMD_OFF_PAD;
	float dt = g_mt.sg_t_last > 0 && t > g_mt.sg_t_last ? (float)fmin(t - g_mt.sg_t_last, 0.2) : 0;
	g_mt.sg_t_last = t;
	char nm[96];
	int dom = g_mt.atk && strstr(fb_rec_name(g_mt.atk, nm, sizeof nm), "Domination") && !strstr(nm, "Fail") &&
		  (strstr(nm, "Warg") || strstr(nm, "Troll") || strstr(nm, "Graug")); /* a beast's (a captain's is the brand's) */
	int grip = g_mt.sq_dn || (g_inp.ctl.down[SOMVR_HAND_R] & SOMVR_BTN_UPPER);
	if (dom && !g_mt.dom_was && mt_log_ok())
		logf_("RIDE t=%.3f the domination: Talion's '%s' - its struggle answered by the left hand kept closed (or B)", t, nm);
	g_mt.dom_was = dom;
	uint64_t ser = atomic_load(&g_mt.sg_serial);
	int kind = g_mt.sg_kind, cmd = g_mt.sg_cmd;
	uintptr_t obj = g_mt.sg_obj;
	if (!obj) /* (no struggle: the mount's own press only) */
		kind = SOMVR_RDS_NONE;
	else if (kind == SOMVR_RDS_AIM || kind == SOMVR_RDS_DIR) /* held on = won (no command: its stick is not the hand's) */
		cmd = 0;
	else if (kind == SOMVR_RDS_NONE || !(cmd > 0 && cmd < SC_NCMD)) /* a struggle of a class not known: 123 held */
		kind = SOMVR_RDS_HOLD, cmd = VB_CMD_GRAB;
	if (ser != g_mt.sg_serial_in) /* a new struggle (or its end): a fresh answer */
		g_mt.sg_serial_in = ser, somvr_rd_strug_init(&g_mt.sg, kind, g_mt.dom_hold_s), g_mt.sg_won_was = 0;
	if (!dom || kind == SOMVR_RDS_NONE) {
		g_mt.dom_hold_was = g_mt.sg_on_was = 0;
		return t < g_mt.press_mount_until ? VB_CMD_GRAB : 0;
	}
	if (t < g_mt.press_mount_until) /* the struggle began: the mount's press is over */
		g_mt.press_mount_until = t;
	int on = somvr_rd_strug_step(&g_mt.sg, grip, dt) && cmd;
	if (g_mt.sg.won && !g_mt.sg_won_was) { /* held on: bm_pre completes it; the hand feels the beast give in */
		atomic_store(&g_mt.sg_win_for, obj), g_mt.sg_won_was = 1;
		in_haptic_event(SOMVR_HAND_L, 0.9f, 0.15f, SOMVR_HAP_COMBAT);
		if (mt_log_ok())
			logf_("RIDE t=%.3f the struggle (%s) held on %.2f s by %s: won", t, somvr_rd_strug_names[kind], g_mt.sg.held,
			      g_mt.sq_dn ? "the left hand" : "B");
	}
	if (grip && !g_mt.dom_hold_was)
		g_mt.dom_holds++;
	g_mt.dom_hold_was = grip;
	if (on && !g_mt.sg_on_was && cmd && mt_log_ok())
		logf_("RIDE t=%.3f the struggle answered: %d '%s' %s (%s; the game's held %d, presses %d of %d)", t, cmd, in_cmd_name(cmd),
		      kind == SOMVR_RDS_HOLD ? "held" : "pressed", g_mt.sq_dn ? "the left hand closed" : "B", g_mt.sg_held,
		      g_mt.sg_count, g_mt.sg_need);
	g_mt.sg_on_was = on;
	if (grip && !g_mt.sg.won && t >= g_mt.sg_hap_next) /* the beast bucking under the hand that holds it, harder as */
		in_haptic_event(SOMVR_HAND_L, 0.3f + 0.4f * fminf(1, g_mt.sg.held / fmaxf(0.1f, g_mt.dom_hold_s)), 0.07f, /* it */
				SOMVR_HAP_COMBAT), g_mt.sg_hap_next = t + 0.2;                                            /* tires */
	return on ? cmd : 0;
}

/* combat's sweep: the beast ridden is never a target (the rider's own legs are in its capsules) */
static int mt_is_mount(uintptr_t hobj) { return hobj && mt_riding() && hobj == g_mt.mount; }

/* the bow's line (ba_world): a cast that meets the beast ridden - its object, or within 2.5 m of its root across and
 * -0.5..+2.5 m up (the rider's nock sits over its back: build 27c, boot 20261030-234922 "the world first 0.0 m") */
static int mt_near_mount(uintptr_t obj, const float p[3])
{
	if (!mt_riding())
		return 0;
	if (obj && obj == g_mt.mount)
		return 1;
	float dx = p[0] - g_mt.beast[0], dz = p[2] - g_mt.beast[2], dy = p[1] - g_mt.beast[1];
	return g_mt.state == MT_SEATED && vec_ok(g_mt.beast, 1e7f) && dx * dx + dz * dz <= 250.0f * 250.0f && dy >= -50 &&
	       dy <= 250;
}

/* A's on-foot owners stand down while riding (ab_tick) */
static int mt_owns_buttons(void) { return g_mt.on && g_mt.owns; }

/* build 27c (som-b27-ride3, re-xr-pipeline.md 122): commands into the tick's list, held or tapped (on MT_SPR_TAP_S
 * of every pulse_ms from `since`'s start) */
static void mt_add_cmds(sc_tick_out *o, const int *cmds, int n, int pulse_ms, double since)
{
	if (pulse_ms > 0 && fmod(since, pulse_ms * 0.001) >= MT_SPR_TAP_S)
		return;
	for (int k = 0; k < n; k++) {
		int have = 0;
		for (int i = 0; i < o->ncmd; i++)
			have |= o->cmd[i] == cmds[k];
		if (!have && o->ncmd < SC_OUT_CMDS)
			o->cmd[o->ncmd++] = cmds[k];
	}
}
static const char *mt_cmds_str(const int *cmds, int n)
{
	static char b[160];
	size_t o = 0;
	b[0] = 0;
	for (int k = 0; k < n && o < sizeof b - 40; k++)
		o += (size_t)snprintf(b + o, sizeof b - o, "%s%d '%s'", k ? " + " : "", cmds[k], in_cmd_name(cmds[k]));
	return n ? b : "none";
}
static const char *mt_ms_str(int ms)
{
	static char b[24];
	snprintf(b, sizeof b, "%d ms", ms);
	return b;
}

/* `ride run MS [CMDS] [PULSE_MS] [MAG]` (a test, SOMVR_HANDS_TEST; seated): the stick MAG (1.0) ahead in the beast's frame,
 * held on the heading it had at the start (a rider holding a line: at most 0.6 rad of the stick aside), the commands
 * held (or tapped every PULSE_MS) for MS; then one line - the path, its speed, after the first second (the beast up to
 * speed), the top 1-s window, the beast's records and the rider's list's commands seen on. What the sprint wants is
 * measured, not guessed: the same ground, one input set at a time */
static void mt_run_end(double t, const char *why)
{
	char rec[200], rc[200];
	size_t n = 0;
	rec[0] = rc[0] = 0;
	for (int i = 0; i < g_mt.run_nrec && n < sizeof rec - 100; i++) {
		char nm[96];
		n += (size_t)snprintf(rec + n, sizeof rec - n, "%s'%s'", i ? ", " : "", fb_rec_name(g_mt.run_rec[i], nm, sizeof nm));
	}
	n = 0;
	for (int i = 0; i < g_mt.run_nrc && n < sizeof rc - 40; i++)
		n += (size_t)snprintf(rc + n, sizeof rc - n, "%s%d '%s'", i ? ", " : "", g_mt.run_rc[i], g_mt.run_rc[i] > 0 &&
				      g_mt.run_rc[i] < SC_NCMD ? in_cmd_name(g_mt.run_rc[i]) : "?");
	const somvr_rd_speed *r = &g_mt.run_spd;
	logf_("RIDE run t=%.3f over%s: %.1f s, the stick %.2f ahead, %s%s%s: %.1f m in %.1f s = %.1f m/s; after its first second "
	      "%.1f m in %.1f s = %.1f m/s; the top 1-s %.1f m/s; the beast's records: %s; the rider's list on: %s", t, why,
	      t - g_mt.run_t0, g_mt.run_mag, mt_cmds_str(g_mt.run_cmd, g_mt.run_n), g_mt.run_n ? (g_mt.run_pulse_ms ?
	      " tapped every " : " held") : "", g_mt.run_pulse_ms && g_mt.run_n ? mt_ms_str(g_mt.run_pulse_ms) : "", r->dist_m,
	      r->time_s, r->time_s > 0 ? r->dist_m / r->time_s : 0.0, g_mt.run_d1, g_mt.run_t1, g_mt.run_t1 > 0 ? g_mt.run_d1 /
	      g_mt.run_t1 : 0.0, r->top, g_mt.run_nrec ? rec : "none", g_mt.run_nrc ? rc : "none");
	g_mt.run_until = 0;
}
static void mt_run(sc_tick_out *o, double t)
{
	int ask = atomic_exchange(&g_mt.req_run_ms, 0);
	if (ask > 0 && mt_seated()) {
		g_mt.run_n = g_mt.rq_n, g_mt.run_pulse_ms = g_mt.rq_pulse_ms, g_mt.run_mag = g_mt.rq_mag, g_mt.run_ms = ask;
		memcpy(g_mt.run_cmd, g_mt.rq_cmd, sizeof g_mt.run_cmd);
		g_mt.run_t0 = t, g_mt.run_until = t + ask * 0.001, g_mt.run_bearing = g_mt.heading;
		somvr_rd_speed_init(&g_mt.run_spd);
		g_mt.run_have1 = 0, g_mt.run_d1 = g_mt.run_t1 = 0, g_mt.run_nrec = g_mt.run_nrc = 0;
		logf_("RIDE run t=%.3f: %d ms, the stick %.2f ahead on the heading %.1f deg, %s%s%s (the beast at %.1f m/s)", t, ask,
		      g_mt.run_mag, g_mt.heading * 57.29578f, mt_cmds_str(g_mt.run_cmd, g_mt.run_n), g_mt.run_n ?
		      (g_mt.run_pulse_ms ? " tapped every " : " held") : "", g_mt.run_pulse_ms && g_mt.run_n ?
		      mt_ms_str(g_mt.run_pulse_ms) : "", isfinite(g_mt.spd.mps) ? g_mt.spd.mps : 0.0f);
	} else if (ask > 0) {
		logf_("RIDE run t=%.3f: not seated - ignored", t);
	}
	if (!(g_mt.run_until > 0))
		return;
	if (t >= g_mt.run_until || !mt_seated()) {
		mt_run_end(t, mt_seated() ? "" : " (off the beast)");
		return;
	}
	if (!g_mt.run_have1 && t - g_mt.run_t0 >= 1.0)
		g_mt.run_have1 = 1;
	float d = sv_wrap_pi(g_mt.run_bearing - g_mt.heading), c = fmaxf(-0.6f, fminf(0.6f, d));
	float m[2] = { g_mt.run_mag * sinf(c), g_mt.run_mag * cosf(c) };
	sc_rotate(m, in_dpsi()); /* the beast's frame -> the game camera's, as the stick */
	o->move[0] = m[0], o->move[1] = m[1], o->pad[SC_PAD_LX] = m[0], o->pad[SC_PAD_LY] = m[1];
	o->pad_on |= 1u << SC_PAD_LX | 1u << SC_PAD_LY;
	mt_add_cmds(o, g_mt.run_cmd, g_mt.run_n, g_mt.run_pulse_ms, t - g_mt.run_t0);
}

/* the input tick, after sc_tick: while riding the buttons are the beast's */
static void mt_tick(sc_tick_out *o, double t)
{
	g_mt.owns = 0;
	if (!g_mt.on)
		return;
	int ask = atomic_exchange(&g_mt.req_press_cmd, 0);
	if (ask > 0) /* `ride press CMD MS` (a test) */
		g_mt.press_cmd = ask, g_mt.press_until = t + atomic_load(&g_mt.req_press_ms) * 0.001;
	if (!(g_mt.press_cmd && t < g_mt.press_until)) /* (mt_press_cmd sends it: the deny list does not apply to a test) */
		g_mt.press_cmd = 0;
	if (g_mt.run_until > 0 && !mt_seated()) /* `ride run`: off the beast meanwhile */
		mt_run_end(t, " (off the beast)");
	if (atomic_exchange(&g_mt.req_sprint, 0)) { /* `ride sprint` (a test) */
		memcpy(g_mt.spr_cmd, g_mt.rs_cmd, sizeof g_mt.spr_cmd), g_mt.spr_n = g_mt.rs_n, g_mt.spr_pulse_ms = g_mt.rs_pulse_ms;
		logf_("RIDE t=%.3f the sprint (the test word): %s%s%s", t, mt_cmds_str(g_mt.spr_cmd, g_mt.spr_n), g_mt.spr_n ?
		      (g_mt.spr_pulse_ms ? " tapped every " : " held") : "", g_mt.spr_pulse_ms && g_mt.spr_n ?
		      mt_ms_str(g_mt.spr_pulse_ms) : "");
	}
	int bb = (g_inp.ctl.down[SOMVR_HAND_R] & SOMVR_BTN_UPPER) != 0, bfresh = bb && !g_mt.b_was;
	g_mt.b_was = bb;
	const char *offer = "";
	if (!mt_riding() && o->live && o->ctx == SC_CTX_WORLD && bfresh && mt_offered(&offer)) { /* B at the mount's offer */
		mt_mount_press(1, 0, 0, offer);
		int n = 0;
		for (int k = 0; k < o->ncmd; k++)
			if (o->cmd[k] != SC_CMD_ACTIVATE)
				o->cmd[n++] = o->cmd[k];
		o->ncmd = n;
	}
	if (!mt_riding() || !o->live || o->ctx == SC_CTX_MENU)
		return;
	g_mt.owns = 1;
	int n = 0; /* B's interact, the crouch: not while riding */
	for (int k = 0; k < o->ncmd; k++)
		if (o->cmd[k] != SC_CMD_ACTIVATE && o->cmd[k] != SC_CMD_CROUCH && o->cmd[k] != SC_CMD_FREERUN)
			o->cmd[n++] = o->cmd[k];
	o->ncmd = n;
	if (g_mt.state != MT_SEATED)
		return;
	if (atomic_load(&g_mt.steer_on) && g_mt.yaw.have && g_bd.test) { /* `ride steer X Z` (a test: a rider steering) */
		float d = sv_wrap_pi(atan2f(g_mt.steer_x - g_mt.beast[0], g_mt.steer_z - g_mt.beast[2]) - g_mt.heading);
		float m[2] = { fmaxf(-0.9f, fminf(0.9f, d / 0.8f)), fabsf(d) > 1.2f ? 0.3f : 1.0f };
		float len = hypotf(m[0], m[1]);
		sc_rotate(m, in_dpsi()); /* the beast's frame -> the game camera's, as the stick */
		o->move[0] = m[0], o->move[1] = m[1], o->pad[SC_PAD_LX] = m[0], o->pad[SC_PAD_LY] = m[1];
		o->pad_on |= 1u << SC_PAD_LX | 1u << SC_PAD_LY;
		o->move_raw[0] = 0, o->move_raw[1] = fminf(1.0f, len); /* (build 27c: the thumb on the stick - the sprint's latch reads it) */
	}
	/* build 27c (som-b27-story4, re-xr-pipeline.md 120): a window (SOMVR_BODYMOMENTS': a presentation, a failed or a
	 * result screen) has A B X Y (SOMVR_USE: the skip, Retry, a result's Back, Abandon). A press made in it is the
	 * screen's and never the beast's: boot 20261030-163325 - B on a Retry's window also pressed 123, Talion got off the
	 * beast the Retry had just put him on (the mission's caragor then never offered again); boot -195017 - A's Retry
	 * also leapt (16) and Talion was off 1.3 s later. A button held since before the window goes on through it (a
	 * banner's window in a chase - "Stop the runner" - does not stop a held run), but never B (off). A press made in
	 * the window acts only once let go after it */
	int win = g_mt.win_rest && g_bm.on && g_bm.last_win, win_start = win && !g_mt.in_win;
	if (win != g_mt.in_win && mt_log_ok())
		logf_("RIDE t=%.3f %s", t, win ? "a window while seated: a press is the screen's, not the beast's; held ones go on, "
		      "never B (SOMVR_RIDE_WIN_REST)" : "the window is over: its presses act once let go");
	g_mt.win_rests += win_start;
	g_mt.in_win = win;
	if (win_start)
		g_mt.win_mask = g_mt.raw_prev & ~SOMVR_RDB_OFF;
	const somvr_hand_in *R = &g_inp.st.hand[SOMVR_HAND_R];
	float rt = (R->active & SOMVR_HACT_INPUT) ? R->trigger : NAN;
	uint32_t dn0 = g_inp.ctl.down[SOMVR_HAND_L], dn1 = g_inp.ctl.down[SOMVR_HAND_R];
	float stick = hypotf(o->move_raw[0], o->move_raw[1]);
	uint32_t was = g_mt.held;
	g_mt.held = somvr_rd_btn_step(&g_mt.btn, &g_mt.bcfg, (dn1 & SOMVR_BTN_LOWER) != 0, (dn1 & SOMVR_BTN_UPPER) != 0, rt,
				      (dn0 & SOMVR_BTN_STICK) != 0, stick);
	uint32_t raw = g_mt.held; /* build 27c: SOMVR_RIDE_WIN_REST's masks (above) */
	g_mt.raw_prev = raw;
	if (win) {
		g_mt.win_mask &= raw; /* a held button let go in the window is the screen's from then on */
		g_mt.rest_bits |= raw & ~g_mt.win_mask;
	}
	g_mt.rest_bits &= raw; /* let go: acts again */
	g_mt.held = raw & ~g_mt.rest_bits;
	/* build 27 (109.4, boot 20261030-044056): the PC command set has no input record for MNT_Mount/GetOff/Jump (181-183:
	 * nothing reaches the beast); what does: the pad's RB 123 gets off (as it mounted - mt_press_cmd sends it, a combat
	 * slot), MNT_Attack_2 186 is the ridden bite ('AI_Warg_Ridden_Attack_Light'; 185 did nothing), A = the stock pad A's
	 * Jump 16 and CombatPrimary04 121 (the rider's list reads them) */
	/* build 27c (som-b27-ride3, re-xr-pipeline.md 122, boot 20261030-220337's `ride run`s, the stick full ahead): the plain
	 * stick 5.4-6.5 m/s; MNT_Charge 184 alone 5.3; Jump 16 + CombatPrimary04 121 held (build 27's A) 0.0 - the beast
	 * stands; the three held together 11.0 m/s (15.1 at the top) - the stock pad's A (its records: 16 and 121 'first
	 * 32' = Space, 184 bound to the pad's A) held is the sprint. SOMVR_RIDE_A=pad: A sends the three (the stock pad's A:
	 * tapped the leap, held the sprint); the left stick's click latches the sprint (SOMVR_RIDE_SPRINT_CMDS, the same
	 * three) while the stick stays pushed. A pressed or let go while the click's sprint holds them: the shared commands
	 * let go for MT_SPR_EDGE_S first - the game sees A's own press and release (the leap mid-sprint) */
	static const int cmds[4] = { MT_CMD_JUMP_A, MT_CMD_BITE, MT_CMD_OFF_PAD, MT_CMD_CHARGE };
	static const char *const by[4] = { "A", "the right trigger", "B", "the left stick's click" };
	int a_now = (g_mt.held & SOMVR_RDB_JUMP) != 0, a_was = (was & SOMVR_RDB_JUMP) != 0;
	if (a_now != a_was && (g_mt.held & was & SOMVR_RDB_CHARGE) && g_mt.spr_n) /* A's edge inside the click's sprint */
		g_mt.edge_until = t + MT_SPR_EDGE_S, g_mt.edges++;
	int edge = t < g_mt.edge_until;
	int acmd[3] = { MT_CMD_JUMP_A, MT_CMD_EVADE_A, MT_CMD_CHARGE };
	for (int k = 0; k < 4; k++) {
		if (!(g_mt.held >> k & 1))
			continue;
		if (k == 0 && !edge) /* A: the stock pad A's (SOMVR_RIDE_A=pad: the three; leap: build 27's pair) */
			mt_add_cmds(o, acmd, g_mt.a_pad ? 3 : 2, 0, 0);
		if (k == 3) { /* build 27c (som-b27-ride3, 122): the sprint's commands, held or tapped */
			if (!(was >> k & 1))
				g_mt.spr_t0 = t;
			if (!edge)
				mt_add_cmds(o, g_mt.spr_cmd, g_mt.spr_n, g_mt.spr_pulse_ms, t - g_mt.spr_t0);
		} else if (k != 0 && k != 2 && o->ncmd < SC_OUT_CMDS) /* (B's 123 goes by mt_press_cmd) */
			o->cmd[o->ncmd++] = cmds[k];
		if (!(was >> k & 1)) {
			g_mt.presses[k]++;
			if (k == 3 && mt_log_ok())
				logf_("RIDE t=%.3f the sprint by the left stick's click (latched while the stick is pushed): %d command%s "
				      "%s%s%s (the beast at %.1f m/s)", t, g_mt.spr_n, g_mt.spr_n == 1 ? "" : "s", mt_cmds_str(g_mt.spr_cmd,
				      g_mt.spr_n), g_mt.spr_n ? (g_mt.spr_pulse_ms ? " tapped every " : " held") : "",
				      g_mt.spr_pulse_ms && g_mt.spr_n ? mt_ms_str(g_mt.spr_pulse_ms) : "",
				      isfinite(g_mt.spd.mps) ? g_mt.spd.mps : 0.0f);
			else if (k == 0 && mt_log_ok())
				logf_("RIDE t=%.3f press A: %s%s (rec %#lx first %d; the beast at %.1f m/s)", t, mt_cmds_str(acmd,
				      g_mt.a_pad ? 3 : 2), edge ? " after the click's sprint let them go" : "",
				      (unsigned long)g_inp.rec[MT_CMD_JUMP_A], g_inp.first[MT_CMD_JUMP_A],
				      isfinite(g_mt.spd.mps) ? g_mt.spd.mps : 0.0f);
			else if (mt_log_ok())
				logf_("RIDE t=%.3f press %d '%s' by %s (rec %#lx first %d)", t, cmds[k], in_cmd_name(cmds[k]), by[k],
				      (unsigned long)g_inp.rec[cmds[k]], g_inp.first[cmds[k]]);
		}
	}
	if (atomic_load(&g_mt.tap_until_ms) && g_bd.test) { /* `ride tap CMDS MS` (a test): the commands held MS */
		int ms = atomic_exchange(&g_mt.tap_until_ms, 0);
		g_mt.tap_until = t + ms * 0.001, g_mt.tap_n = g_mt.rt_n, memcpy(g_mt.tap_cmd, g_mt.rt_cmd, sizeof g_mt.tap_cmd);
		logf_("RIDE t=%.3f tap (the test word): %s for %d ms (the beast at %.1f m/s)", t, mt_cmds_str(g_mt.tap_cmd,
		      g_mt.tap_n), ms, isfinite(g_mt.spd.mps) ? g_mt.spd.mps : 0.0f);
	}
	if (t < g_mt.tap_until)
		mt_add_cmds(o, g_mt.tap_cmd, g_mt.tap_n, 0, 0);
	if ((g_mt.held & SOMVR_RDB_CHARGE) && g_mt.t_tick > 0 && t > g_mt.t_tick)
		g_mt.spr_s += fmin(t - g_mt.t_tick, 0.2);
	if ((g_mt.held & SOMVR_RDB_CHARGE) && !g_mt.spr_was)
		g_mt.sprints++;
	g_mt.spr_was = (g_mt.held & SOMVR_RDB_CHARGE) != 0;
	g_mt.t_tick = t;
	mt_run(o, t); /* `ride run` (a test) */
}

/* DEFENCE's tracks (df_frame, a caragor's or ghul's line; outside its lock): `ride guard on` - the test right hand holds
 * the blade across the line from the beast's head to Talion's body, MT_GUARD_OUT_CM out, from 0.3 s before the parry
 * window to its end, back to where it was after: a player's block of the leap (headless; the hand placed as a real one
 * holds the sword: the test pose's grip turned the least that lays the blade across) */
static void mt_guard(const struct df_track *d, const somvr_df_in *in, double t)
{
	if (!g_mt.guard || !g_bd.test || !g_bd.t_on[1] || !g_tg.have_blade || !(in->bpw >= 0) || !(in->epw > in->bpw))
		return;
	int live = in->ts >= in->bpw - 0.3f && in->ts <= in->epw + 0.05f;
	if (g_mt.g_c && (g_mt.g_c != d->c || g_mt.g_serial != d->serial)) {
		if (g_mt.g_c != d->c)
			return; /* one attack at a time */
		g_mt.g_c = 0;
	}
	if (!live) {
		if (g_mt.g_placed && g_mt.g_c == d->c) {
			tg_swing(g_mt.g_rest, 250);
			g_mt.g_placed = 0, g_mt.g_c = 0;
			if (mt_log_ok())
				logf_("RIDE guard t=%.3f back to rest (the window over at %.3f s)", t, in->ts);
		}
		return;
	}
	const somvr_df_cfg *c = &g_df.cfg;
	float top = in->head[1] - c->body_top_cm, bot = in->head[1] - c->body_bot_cm, y = in->hand[1];
	float T[3] = { in->head[0], y > top ? top : y < bot ? bot : y, in->head[2] };
	float u[3] = { in->hand[0] - T[0], in->hand[1] - T[1], in->hand[2] - T[2] };
	if (tg_norm(u) < 30)
		return;
	float P[3] = { T[0] + u[0] * MT_GUARD_OUT_CM, T[1] + u[1] * MT_GUARD_OUT_CM, T[2] + u[2] * MT_GUARD_OUT_CM };
	float up[3] = { 0, 1, 0 }, D[3];
	tg_cross(u, up, D);
	if (tg_norm(D) < 1e-3f)
		D[0] = 1, D[1] = D[2] = 0;
	sv_vec3 hp;
	sv_quat hq;
	somvr_rig_head(&g_rig, g_fr.base, g_fr.views.pose, &hp, &hq);
	float hd[3] = { hp.x, hp.y, hp.z }, yaw = sv_heading(hq), M[3], Dw[3] = { hd[0] + D[0], hd[1] + D[1], hd[2] + D[2] }, Dh[3];
	tg_to(P, hd, yaw, M), tg_to(Dw, hd, yaw, Dh);
	if (!g_mt.g_placed)
		memcpy(g_mt.g_rest, g_bd.t_v[1], sizeof g_mt.g_rest), memcpy(g_tg.rest, g_bd.t_v[1], sizeof g_tg.rest);
	float pa[6], pb[6], Dn[3] = { -Dh[0], -Dh[1], -Dh[2] };
	float ra = tg_pose(M, Dh, 0.5f, pa), rb = tg_pose(M, Dn, 0.5f, pb);
	const float *pz = ra <= rb ? pa : pb;
	char w[160];
	if (!g_mt.g_placed) {
		g_mt.g_c = d->c, g_mt.g_serial = d->serial, g_mt.g_placed = 1, g_mt.g_guards++;
		tg_swing(pz, 120);
		if (mt_log_ok())
			logf_("RIDE guard t=%.3f the blade across the line at %.3f s (window %.3f..%.3f): the beast's head %.0f cm from "
			      "Talion's body, the blade's middle %.0f cm out, the grip %.0f cm from the shoulder", t, in->ts, in->bpw,
			      in->epw, sqrtf((in->hand[0] - T[0]) * (in->hand[0] - T[0]) + (in->hand[1] - T[1]) * (in->hand[1] - T[1]) +
				   (in->hand[2] - T[2]) * (in->hand[2] - T[2])), MT_GUARD_OUT_CM, fminf(ra, rb));
		memcpy(g_mt.g_last, pz, sizeof g_mt.g_last), g_mt.g_t0 = t;
	} else if (t - g_mt.g_t0 >= 0.12 && /* the first swing done: follow the line where it moved 10 cm (a 60 ms swing: */
		   sqrtf((pz[0] - g_mt.g_last[0]) * (pz[0] - g_mt.g_last[0]) + (pz[1] - g_mt.g_last[1]) * (pz[1] - g_mt.g_last[1]) +
			 (pz[2] - g_mt.g_last[2]) * (pz[2] - g_mt.g_last[2])) > 10) { /* no jump of the test hand) */
		snprintf(w, sizeof w, "swing R %.1f %.1f %.1f %.1f %.1f %.1f 60", pz[0], pz[1], pz[2], pz[3], pz[4], pz[5]);
		bd_word(w);
		memcpy(g_mt.g_last, pz, sizeof g_mt.g_last), g_mt.g_t0 = t;
	}
}

/* "ride status|guard on|off|press CMD MS" (the writer thread) */
static const char *mt_word(const char *w)
{
	if (!g_mt.on)
		return "ride (SOMVR_RIDE is off: ignored)";
	if (!strcmp(w, "status")) {
		atomic_store(&g_mt.req_status, 1);
		logf_("RIDE status: %s, the mount %#lx (%s), mounts %llu dismounts %llu, seated frames %llu, presses jump %llu bite "
		      "%llu off %llu run %llu, guards %llu", g_rd_state_names[g_mt.state], (unsigned long)g_mt.mount,
		      g_mt.beast_ok ? somvr_rd_kind_names[g_mt.kind >= 0 ? g_mt.kind : 0] : "no beast",
		      (unsigned long long)g_mt.mounts, (unsigned long long)g_mt.dismounts, (unsigned long long)g_mt.seated_frames,
		      (unsigned long long)g_mt.presses[0], (unsigned long long)g_mt.presses[1], (unsigned long long)g_mt.presses[2],
		      (unsigned long long)g_mt.presses[3], (unsigned long long)g_mt.g_guards);
		return "ride status";
	}
	if (!strncmp(w, "guard ", 6)) {
		if (!g_bd.test)
			return "ride guard (SOMVR_HANDS_TEST is off: ignored)";
		g_mt.guard = !strcmp(w + 6, "on");
		return g_mt.guard ? "ride guard on (the test blade across a beast's line in its parry window)" : "ride guard off";
	}
	if (!strncmp(w, "face ", 5)) {
		if (!g_bd.test)
			return "ride face (SOMVR_HANDS_TEST is off: ignored)";
		g_mt.face = !strcmp(w + 5, "on");
		return g_mt.face ? "ride face on (a test: the view turned to a stunned caragor, as a player looks at it)" : "ride face off";
	}
	if (!strncmp(w, "steer ", 6)) {
		if (!g_bd.test)
			return "ride steer (SOMVR_HANDS_TEST is off: ignored)";
		float x, z;
		if (sscanf(w + 6, "%f %f", &x, &z) == 2)
			g_mt.steer_x = x, g_mt.steer_z = z, atomic_store(&g_mt.steer_on, 1);
		else
			atomic_store(&g_mt.steer_on, 0);
		return atomic_load(&g_mt.steer_on) ? "ride steer (a test: seated, the stick toward the point)" : "ride steer off";
	}
	if (!strncmp(w, "press ", 6)) {
		if (!g_bd.test)
			return "ride press (SOMVR_HANDS_TEST is off: ignored)";
		int cmd = 0, ms = 0;
		if (sscanf(w + 6, "%d %d", &cmd, &ms) != 2 || cmd <= 0 || cmd >= SC_NCMD || ms <= 0 || ms > 5000)
			return "ride press (CMD MS: ignored)";
		atomic_store(&g_mt.req_press_ms, ms), atomic_store(&g_mt.req_press_cmd, cmd);
		return "ride press";
	}
	if (!strncmp(w, "sprint ", 7)) { /* build 27c: `ride sprint CMDS [PULSE_MS]` - what the click's sprint sends (a test) */
		if (!g_bd.test)
			return "ride sprint (SOMVR_HANDS_TEST is off: ignored)";
		char cl[64] = "";
		int pulse = 0, rq[MT_SPR_MAX];
		int nf = sscanf(w + 7, "%63s %d", cl, &pulse), n = mt_cmd_list(cl, rq, MT_SPR_MAX);
		if (nf < 1 || n < 0 || pulse < 0 || pulse > 2000)
			return "ride sprint (CMDS|0 [PULSE_MS]: ignored)";
		memcpy(g_mt.rs_cmd, rq, sizeof rq), g_mt.rs_n = n, g_mt.rs_pulse_ms = pulse > 0 && pulse < 200 ? 200 : pulse;
		atomic_store(&g_mt.req_sprint, 1);
		return "ride sprint (a test: the click's sprint sends these from now on)";
	}
	if (!strncmp(w, "tap ", 4)) { /* build 27c: `ride tap CMDS MS` - the commands held MS while seated (a test) */
		if (!g_bd.test)
			return "ride tap (SOMVR_HANDS_TEST is off: ignored)";
		char cl[64] = "";
		int ms = 0, rq[MT_SPR_MAX];
		int n = sscanf(w + 4, "%63s %d", cl, &ms) == 2 ? mt_cmd_list(cl, rq, MT_SPR_MAX) : -1;
		if (n <= 0 || ms <= 0 || ms > 10000)
			return "ride tap (CMDS MS: ignored)";
		memcpy(g_mt.rt_cmd, rq, sizeof rq), g_mt.rt_n = n;
		atomic_store(&g_mt.tap_until_ms, ms);
		return "ride tap (a test: the commands held for MS)";
	}
	if (!strncmp(w, "trace ", 6)) { /* build 27c: `ride trace N` - N more RIDE view lines (a test) */
		int n = atoi(w + 6);
		if (n <= 0 || n > 20000)
			return "ride trace (N: ignored)";
		g_mt.trace_max = (int)g_mt.trace_lines + n;
		return "ride trace (N more RIDE view lines)";
	}
	if (!strncmp(w, "run ", 4)) { /* build 27c: `ride run MS [CMDS] [PULSE_MS] [MAG]` */
		if (!g_bd.test)
			return "ride run (SOMVR_HANDS_TEST is off: ignored)";
		char cl[64] = "0";
		int ms = 0, pulse = 0;
		float mag = 1.0f;
		int nf = sscanf(w + 4, "%d %63s %d %f", &ms, cl, &pulse, &mag);
		int n = mt_cmd_list(cl, g_mt.rq_cmd, MT_SPR_MAX);
		if (nf < 1 || ms <= 0 || ms > 30000 || n < 0 || pulse < 0 || pulse > 2000 || !(mag > 0 && mag <= 1.5f))
			return "ride run (MS [CMDS|0] [PULSE_MS] [MAG]: ignored)";
		g_mt.rq_n = n, g_mt.rq_pulse_ms = pulse > 0 && pulse < 200 ? 200 : pulse, g_mt.rq_mag = mag;
		atomic_store(&g_mt.req_run_ms, ms);
		return "ride run (a test: seated, the stick ahead and the commands for MS)";
	}
	return "ride (not status|guard on|off|face on|off|press CMD MS|steer X Z|steer off|run MS [CMDS] [PULSE_MS] [MAG]|sprint CMDS [PULSE_MS]|tap CMDS MS|trace N: "
	       "ignored)";
}

static uint64_t mt_vm_avail(void) { return g_mt.on ? SOMVR_VMA_RIDE : 0; }

static void mt_end_log(void)
{
	if (!g_mt.on)
		return;
	logf_("RIDE END mounts %llu (not a beast %llu) dismounts %llu, seated frames %llu: the view's largest step %.1f cm across, "
	      "%.1f cm up/down, %.2f deg of yaw a frame (the turn at most %.0f deg/s); the seat's raw span %.1f cm over the root, "
	      "the view's %.1f; pitch/roll at most %.2f/%.2f deg; facings on the black %llu; presses jump %llu bite %llu off %llu "
	      "run %llu; guards %llu; yaw capped %llu frames; mount offers %llu, presses by the hand %llu by B %llu, domination holds "
	      "%llu, struggles ended %llu won by the grip %llu; windows rested through %llu; the beast's top speed %.1f m/s (a 1-s "
	      "window), sprints %llu (%.1f s); %llu lines suppressed", (unsigned long long)g_mt.mounts,
	      (unsigned long long)g_mt.not_beast, (unsigned long long)g_mt.dismounts, (unsigned long long)g_mt.seated_frames,
	      g_mt.step_max, g_mt.ystep_max, g_mt.yaw_step_max, g_mt.yaw_dps_max, g_mt.raw_y_span[1] - g_mt.raw_y_span[0],
	      g_mt.out_y_span[1] - g_mt.out_y_span[0], g_mt.pitch_max, g_mt.roll_max, (unsigned long long)g_mt.aligns,
	      (unsigned long long)g_mt.presses[0], (unsigned long long)g_mt.presses[1], (unsigned long long)g_mt.presses[2],
	      (unsigned long long)g_mt.presses[3], (unsigned long long)g_mt.g_guards, (unsigned long long)g_mt.yaw.capped,
	      (unsigned long long)g_mt.offers, (unsigned long long)g_mt.mount_presses[0], (unsigned long long)g_mt.mount_presses[1],
	      (unsigned long long)g_mt.dom_holds, (unsigned long long)g_mt.sg_ends, (unsigned long long)g_mt.sg_won,
	      (unsigned long long)g_mt.win_rests, g_mt.spd.top, (unsigned long long)g_mt.sprints, g_mt.spr_s,
	      (unsigned long long)g_mt.suppressed);
}
