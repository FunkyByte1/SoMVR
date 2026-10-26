/* somvr_step_game.c - the game side of SOMVR_STEP / SOMVR_DASH / SOMVR_PHASE (somvr_step.h; docs/re-xr-pipeline.md 23).
 * NOT a translation unit of its own: somvr.c includes it right after the LOCO region (it uses somvr.c's statics: the
 * LOCO core g_lo, the input g_inp, the frame g_fr, peek/arm_poke, the logs, the config helpers). Keeping it in its own
 * file keeps somvr.c's part to its hook-in lines (five helpers merge into main).
 *
 * The hook-in points (game thread):
 *   - the combat frame (H4, after the orcs of this frame): st_combat - the orcs for the picks; a swing that just started
 *     -> the assisted step's pick and start;
 *   - the input tick, after lo_tick: st_tick - A's press -> the evade's pick (the phase or the dash) and start; the
 *     wraith vision's taps (122, PC_WraithPing's toggle, P9) around the phase's motion; after the turn's clamp:
 *     st_reface - the phase's re-face (an option);
 *   - lo_pm_pre, after the owner reads: st_pm_own - the move's owner override (the stock evade's ATTACK ticks are LOCO's
 *     while the dash or the phase runs) and the phase's yaw; after somvr_loco_step: st_pm - the move's displacement into
 *     LOCO's delta (somvr_loco_take_assist: one owner of the root translation), the phase's collision hold;
 *   - the frame (eye 0, after the fallback's fade): st_frame - the blink option's black into the record's fade level;
 *   - somvr_start: st_config (with the other configs), st_start (after lo_start); somvr_fini: st_end_log;
 *   - the VR menu: st_set (vm_apply_key's STEP / DASH / PHASE), st_avail (vm_start's avail bit).
 */

/* STEP_SITES_BEGIN (check_sites.py reads this file with somvr.c, by its #include: 6 read-only sites, nothing patched;
 * docs/re-xr-pipeline.md 23.3):
 *   ST_COL_SOLVER   the movement solver FUN_020cda10 drops its NavMeshPhysics correction (pm+0x11c..+0x124 x 0) while an
 *                   attack runs whose Combat+0x56c (the attack animation's EnableCollision) is 0: the read we rely on;
 *   ST_COL_MOVE7    BaseMovement::vf_07: the movement's Combat at +0x38, Combat_GetCurrentAttack, the +0x56c byte;
 *   ST_COL_OFF      Combat_OnModelString's COLLISION_OFF: +0x56c = 0 and +0x56f = 1 (what our hold mirrors);
 *   ST_COL_DEFAULT  Combat_StartAttack without an animation record: +0x56c = 1 (the default the next attack restores);
 *   ST_VISION_GET   the player update's call of FUN_00ad3ac0(*(P+0x2f0)): the PlayerVisionMode pointer;
 *   ST_VISION_ON    FUN_00ad3b60 (the component's on/off): +0x48 = the vision is on. */
static const struct site_spec g_step_sites_game[] = {
SITE("ST_COL_SOLVER", K_ENTRY, 0x20ce555, 0x20ce555, "80 ba 6c 05 00 00 00 0f 85 9e 0b 00 00", 0x20ce555, "80 ba 6c 05 00 00 00 0f 85 9e 0b 00"),
SITE("ST_COL_MOVE7", K_ENTRY, 0x20c5cf0, 0x20c5cf0, "53 48 83 ec 20 48 8b 5f 38 48 85 db 74 16 48 89 df e8 0a e8 60 00 48 85 c0 74 09 80 bb 6c 05 00 00 00", 0x20c5cf0, "53 48 83 ec 20 48 8b 5f 38 48 85 db 74 16 48"),
SITE("ST_COL_OFF", K_ENTRY, 0x26fd137, 0x26fd137, "80 bb 6c 05 00 00 00 0f 84 ef cc ff ff c6 83 6c 05 00 00 00 c6 83 6f 05 00 00 01", 0x26fd137, "80 bb 6c 05 00 00 00 0f 84 ef cc ff ff c6 83"),
SITE("ST_COL_DEFAULT", K_ENTRY, 0x26ec690, 0x26ec690, "c6 83 6c 05 00 00 01 c6 83 6d 05 00 00 01", 0x26ec690, "c6 83 6c 05 00 00 01 c6 83 6d 05 00 00 01"),
SITE("ST_VISION_GET", K_CALL, 0xa413bd, 0xa413b6, "48 8b bb f0 02 00 00 e8 fe 26 09 00", 0xad3ac0, "55 31 f6 53 48 89 fb 48 83 ec 08 48 8b 47 08"),
SITE("ST_VISION_ON", K_ENTRY, 0xad3b60, 0xad3b60, "40 38 77 48 74 12 40 84 f6 74 05 e9 c0 f7 ff ff", 0xad3b60, "40 38 77 48 74 12 40 84 f6 74 05 e9 c0 f7 ff"),
};
/* STEP_SITES_END */
enum { STS_COL_SOLVER, STS_COL_MOVE7, STS_COL_OFF, STS_COL_DEFAULT, STS_VISION_GET, STS_VISION_ON, STS_N };
_Static_assert(sizeof g_step_sites_game / sizeof g_step_sites_game[0] == STS_N, "step sites");
static const struct site_spec *g_step_sites = g_step_sites_game; /* selftest: stand-ins */
/* the game objects (selftest: stand-ins): PlayerVisionMode's vtable (classes: 0x3503bc0) */
static struct { uintptr_t vt_vm; } g_stepg = { 0x3503bc0 };
#define ST_C_COLLIDE 0x56c  /* Combat (pm+0x38, the player's): u8 the current attack's EnableCollision (ST_COL_*) */
#define ST_C_COLDIRTY 0x56f /* ... u8 "changed" (COLLISION_ON/OFF set it, the solver clears it) */
#define ST_P_VISION 0x2f0   /* Player: PlayerVisionMode* (ST_VISION_GET) */
#define ST_VM_OWNER 0x08    /* PlayerVisionMode: the Player */
#define ST_VM_ON 0x48       /* PlayerVisionMode: u8 the vision is on (ST_VISION_ON) */
#define ST_CMD_VISION 122   /* CombatSecondary01 = PC_WraithPing's toggle (P9; on the deny list: allowed for our tap) */
#define ST_TAP_TICKS 2      /* a tap: held two input ticks (a toggle; 0.85 s held would be PC_Radiance) */
#define ST_ORCS 16
#define ST_ORC_STALE_S 0.25 /* the combat frame's orcs older than this: none (the dash still works) */
#define ST_VIS_WAIT_S 0.3   /* the vision's flag not changed this long after a tap: refused (logged) */

static struct {
	int want, on, ok_col, ok_vis, nocollide, look, menu;
	somvr_step_cfg cfg;
	somvr_step_run run;
	uint32_t own_bits;
	char own_txt[96];
	/* the combat frame's orcs */
	somvr_step_orc orc[ST_ORCS];
	int norc;
	double orc_t;
	/* the last PM tick (LOCO's reads) */
	int fsm, attack, have_root;
	sv_vec3 root;
	uintptr_t cmb, pl;
	/* the input tick */
	int a_last;
	/* the vision: our taps */
	int tap, vis_ours, vis_asked; /* tap: ticks left; vis_asked: 1 on, -1 off asked at vis_t */
	double vis_t;
	/* the collision hold */
	uintptr_t col_cmb, col_atk;
	unsigned char col_was;
	int col_on;
	/* the move's log */
	double t0;
	float fade_max;
	uint64_t n_pick[SOMVR_ST_KINDS], n_none[SOMVR_STW_N], n_vis_on, n_vis_off, n_vis_refused, n_col, n_col_restored,
		n_reface, n_blink, n_peak, n_clamped, n_staleorcs, n_ticks, n_assist, n_fin;
	double assist_cm;
	int lines, lsec_n;
	double lsec;
	uint64_t suppressed;
} g_step;

static int st_log_ok(double t)
{
	if (g_step.lines < 200)
		return ++g_step.lines;
	if (t >= g_step.lsec + 1.0)
		g_step.lsec = t, g_step.lsec_n = 0;
	if (g_step.lsec_n < 10)
		return ++g_step.lsec_n;
	g_step.suppressed++;
	return 0;
}

static const char *const g_step_dash_w[] = { "off", "on", "blink" };
static const char *const g_step_phase_w[] = { "off", "on", "reface", "blink" };

/* the combat frame's orcs (fresh) for a pick; *n = 0 when stale */
static const somvr_step_orc *st_orcs(double t, int *n)
{
	*n = g_step.norc && t - g_step.orc_t <= ST_ORC_STALE_S ? g_step.norc : 0;
	if (!*n && g_step.norc)
		g_step.n_staleorcs++;
	return g_step.orc;
}

/* the move's target now (the step's clamp): the orc with its key in this frame's list */
static int st_orc_now(uintptr_t key, double t, sv_vec3 *out)
{
	if (!key || t - g_step.orc_t > ST_ORC_STALE_S)
		return 0;
	for (int i = 0; i < g_step.norc; i++)
		if (g_step.orc[i].key == key)
			return *out = g_step.orc[i].pos, 1;
	return 0;
}

static void st_log_start(double t, const char *what, int target, float D, float T, sv_vec3 dir, float dist, const char *extra)
{
	g_step.t0 = t, g_step.fade_max = 0;
	if (st_log_ok(t))
		logf_("STEP t=%.3f frame=%llu %s: %.1f cm in %.0f ms (peak %.0f cm/s) along %.2f,%.2f (yaw %.1f deg); orc #%d at %.1f cm; "
		      "Talion at %.1f,%.1f,%.1f fsm %d%s", t, (unsigned long long)rend_frame(), what, D, T * 1e3,
		      T > 0 ? 2 * D / T : 0, dir.x, dir.z, atan2f(dir.x, dir.z) * 57.29578f, target, dist, g_step.root.x, g_step.root.y,
		      g_step.root.z, g_step.fsm, extra);
}

/* H4's combat frame (game thread), with this frame's orcs; edge > 0 = a swing of the tracked blade just started */
static void st_combat(const struct cb_orc *o, int n, const somvr_blade *b, int edge, const float root[3], double t)
{
	if (!g_step.on)
		return;
	g_step.norc = 0;
	for (int i = 0; i < n && i < ST_ORCS; i++)
		g_step.orc[g_step.norc++] = (somvr_step_orc){ { o[i].pos[0], o[i].pos[1], o[i].pos[2] }, o[i].c };
	g_step.orc_t = t;
	if (edge <= 0 || !(g_step.cfg.step > 0) || !b || !b->valid)
		return;
	if (g_step.run.kind == SOMVR_ST_DASH || g_step.run.kind == SOMVR_ST_PHASE)
		return; /* an evade runs: no step on top of it */
	if (fi_holds()) { /* SOMVR_FINISH's moment (its sync aligns the bodies): no step into it */
		g_step.n_fin++;
		return;
	}
	somvr_step_swing_in in;
	memset(&in, 0, sizeof in);
	in.root = (sv_vec3){ root[0], root[1], root[2] };
	in.head_yaw = g_inp.hv;
	in.tip = (sv_vec3){ b->tip[0], b->tip[1], b->tip[2] };
	in.tip_v = (sv_vec3){ b->vel_tip[0], b->vel_tip[1], b->vel_tip[2] };
	in.stick_v = g_lo.stick_v;
	in.blocking = 0; /* the defence (HG-3) has no block state yet */
	in.orc = g_step.orc, in.n = g_step.norc;
	somvr_step_pick p;
	int why = somvr_step_swing_pick(&g_step.cfg, &in, &p);
	if (why != SOMVR_STW_OK) {
		g_step.n_none[why]++;
		if (why != SOMVR_STW_NO_ORC && why != SOMVR_STW_OFF && st_log_ok(t))
			logf_("STEP t=%.3f frame=%llu swing: no step (%s): orc #%d at %.1f cm (gaze cos %.2f, tip cos %.2f)", t,
			      (unsigned long long)rend_frame(), somvr_step_why_name(why), p.target, p.dist_cm, p.cos_gaze, p.cos_tip);
		return;
	}
	g_step.n_pick[SOMVR_ST_STEP]++;
	somvr_step_start(&g_step.run, &g_step.cfg, SOMVR_ST_STEP, p.target, g_step.orc[p.target].key, p.d_cm, g_step.cfg.step_s, p.dir,
			 in.root, p.dist_cm);
	char ex[96];
	snprintf(ex, sizeof ex, "; the swing's tip %.1f m/s (gaze cos %.2f, tip cos %.2f), strength %.2f", g_cb.sp.mean_mps, p.cos_gaze,
		 p.cos_tip, g_step.cfg.step);
	st_log_start(t, "assisted step", p.target, p.d_cm, g_step.cfg.step_s, p.dir, p.dist_cm, ex);
}

/* SOMVR_FINISH started a finisher (fi_trigger, later in the same combat frame as a swing's step pick): a step, dash or
 * phase running ends here, its motion not added to the sync's */
static void st_yield(double t)
{
	if (!g_step.on || g_step.run.kind == SOMVR_ST_NONE)
		return;
	int kind = somvr_step_stop(&g_step.run, SOMVR_STE_FINISH);
	if (st_log_ok(t))
		logf_("STEP t=%.3f frame=%llu %s end: %s after %.0f ms; moved %.1f of %.1f cm", t, (unsigned long long)rend_frame(),
		      somvr_step_kind_name(kind), somvr_step_end_name(SOMVR_STE_FINISH), (t - g_step.t0) * 1e3, g_step.run.done_cm,
		      g_step.run.D);
}

/* the vision's flag (PlayerVisionMode+0x48): 1 on, 0 off, -1 unreadable */
static int st_vision(void)
{
	if (!g_step.ok_vis || !g_step.pl)
		return -1;
	uintptr_t vm = peek_ptr(g_step.pl + ST_P_VISION);
	unsigned char on = 0;
	if (!vm || vptr_of((void *)vm) != g_stepg.vt_vm || peek_ptr(vm + ST_VM_OWNER) != g_step.pl || !peek(vm + ST_VM_ON, &on, 1))
		return -1;
	return on != 0;
}

/* the input tick (after lo_tick): A's press -> the evade's pick and start; the vision's taps; the re-face */
static void st_tick(sc_tick_out *o)
{
	g_inp.allow_cmd = 0;
	if (!g_step.on)
		return;
	double t = cb_now();
	int world = o->live && o->ctx == SC_CTX_WORLD;
	int a = world && o->pad[SC_PAD_A] > 0.5f, edge = a && !g_step.a_last;
	g_step.a_last = a;
	if (edge && (g_step.cfg.dash || g_step.cfg.phase) && fi_holds()) { /* the stock A as it is during a finisher */
		g_step.n_fin++;
		if (st_log_ok(t))
			logf_("STEP t=%.3f frame=%llu A: no evade of ours (a finisher's moment): the stock A as it is", t,
			      (unsigned long long)rend_frame());
	} else if (edge && (g_step.cfg.dash || g_step.cfg.phase) && g_lo.stick_ok && g_step.have_root) {
		float m = sqrtf(o->move[0] * o->move[0] + o->move[1] * o->move[1]), sl = hypotf(g_lo.stick_v.x, g_lo.stick_v.z);
		somvr_step_evade_in in;
		memset(&in, 0, sizeof in);
		in.root = g_step.root, in.head_yaw = g_inp.hv;
		in.stick_dir = sl > 1e-3f ? (sv_vec3){ g_lo.stick_v.x / sl, 0, g_lo.stick_v.z / sl } : (sv_vec3){ 0, 0, 0 };
		in.stick_mag = m > 1 ? 1 : m;
		in.fight = g_step.fsm == 7;
		in.orc = st_orcs(t, &in.n);
		somvr_step_evade e;
		int why = somvr_step_evade_pick(&g_step.cfg, &in, &e);
		if (why != SOMVR_STW_OK) {
			g_step.n_none[why]++;
			if (st_log_ok(t))
				logf_("STEP t=%.3f frame=%llu A: no evade of ours (%s; orc #%d at %.1f cm, %d orcs, stick %.2f, fsm %d; dash=%s "
				      "phase=%s): the stock A as it is", t, (unsigned long long)rend_frame(), somvr_step_why_name(why), e.target,
				      e.dist_cm, in.n, in.stick_mag, g_step.fsm, g_step_dash_w[g_step.cfg.dash % 3],
				      g_step_phase_w[g_step.cfg.phase % 4]);
		} else {
			g_step.n_pick[e.kind]++;
			somvr_step_start(&g_step.run, &g_step.cfg, e.kind, e.target, e.target >= 0 ? in.orc[e.target].key : 0, e.D, e.T, e.dir,
					 g_step.root, e.dist_cm);
			char ex[320];
			snprintf(ex, sizeof ex, "; landing %.1f,%.1f; stick %.2f; the stock evade (A) runs under it, its %.60s ticks ours%s",
				 e.land.x, e.land.z, in.stick_mag, g_step.own_txt,
				 e.kind == SOMVR_ST_PHASE ? (g_step.cfg.phase == SOMVR_ST_PHASE_REFACE ? "; re-face at the orc" : "; facing the gaze")
							 : "");
			st_log_start(t, e.kind == SOMVR_ST_PHASE ? "wraith phase" : "dash", e.target, e.D, e.T, e.dir, e.dist_cm, ex);
			/* the haptic: the phase sweeps from the sword hand (here) to the other one (at the orc's axis); the dash a tick */
			in_haptic_event(SOMVR_HAND_R, e.kind == SOMVR_ST_PHASE ? 0.35f : 0.25f, e.kind == SOMVR_ST_PHASE ? 0.10f : 0.05f,
					SOMVR_HAP_COMBAT);
		}
	}
	/* the wraith look: the vision on for the phase's motion (only when it was off: the player's own stays as it is), off
	 * again after it if we turned it on */
	int want = g_step.look && g_step.ok_vis && somvr_step_look(&g_step.run), vis = st_vision();
	if (g_step.vis_asked && t - g_step.vis_t > ST_VIS_WAIT_S) {
		if ((g_step.vis_asked > 0 && vis != 1) || (g_step.vis_asked < 0 && vis != 0)) {
			g_step.n_vis_refused++;
			if (st_log_ok(t))
				logf_("STEP t=%.3f the wraith vision did not turn %s within %.0f ms of our tap (flag %d): refused (in the evade? "
				      "Focus?)", t, g_step.vis_asked > 0 ? "on" : "off", ST_VIS_WAIT_S * 1e3, vis);
			if (g_step.vis_asked > 0)
				g_step.vis_ours = 0;
		}
		g_step.vis_asked = 0;
	}
	if (!g_step.tap && !g_step.vis_asked && world) {
		if (want && !g_step.vis_ours && vis == 0) {
			g_step.tap = ST_TAP_TICKS, g_step.vis_ours = 1, g_step.vis_asked = 1, g_step.vis_t = t, g_step.n_vis_on++;
			if (st_log_ok(t))
				logf_("STEP t=%.3f wraith vision on (a tap of %d '%s' for %d ticks) for the phase", t, ST_CMD_VISION,
				      bo_cmd_name(ST_CMD_VISION), ST_TAP_TICKS);
		} else if (!want && g_step.vis_ours) { /* the motion is over (or the phase ended early) */
			g_step.vis_ours = 0;
			if (vis == 1) {
				g_step.tap = ST_TAP_TICKS, g_step.vis_asked = -1, g_step.vis_t = t, g_step.n_vis_off++;
				if (st_log_ok(t))
					logf_("STEP t=%.3f wraith vision off (our tap) after the phase", t);
			}
		}
	}
	if (g_step.tap > 0) {
		g_step.tap--;
		if (world && o->ncmd < SC_OUT_CMDS) {
			o->cmd[o->ncmd++] = ST_CMD_VISION;
			g_inp.allow_cmd = ST_CMD_VISION; /* on the deny list (re-xr-input.md 3.6): our tap passes this tick */
		}
	}
}

/* the input tick, after the turn's clamp (in_tick): the re-face option (SOMVR_PHASE=reface), a half turn of the rig about
 * the head at the orc's axis (seated play); in_rig_turn applies it at the next stereo frame like a smooth turn's part */
static void st_reface(int live, int stereo)
{
	if (!g_step.on || !g_step.run.reface)
		return;
	g_step.run.reface = 0;
	if (!live || g_inp.ctx == SC_CTX_MENU || !stereo)
		return;
	g_step.n_reface++;
	g_inp.pend_turn += 3.14159265f;
	if (st_log_ok(cb_now()))
		logf_("STEP t=%.3f re-face: the rig turns 180 deg about the head at the orc's axis", cb_now());
}

/* lo_pm_pre, after the owner reads (own) and the game's why/yaw (in): the move's override */
static void st_pm_own(const somvr_loco_own_in *own, somvr_loco_in *in, uintptr_t pl, uintptr_t cmb, const float *root, int have_root)
{
	if (!g_step.on)
		return;
	g_step.fsm = own->fsm, g_step.attack = own->attack, g_step.cmb = cmb, g_step.pl = pl, g_step.have_root = have_root;
	if (have_root)
		g_step.root = (sv_vec3){ root[0], root[1], root[2] };
	uint32_t m = somvr_step_mask(&g_step.run, &g_step.cfg, g_step.own_bits, own->attack);
	in->why &= ~m;
	if (!in->why && somvr_step_yaw_ours(&g_step.run, &g_step.cfg, own->attack) && g_lo.heading_ok)
		in->yaw_game = 0; /* the phase: Talion turns to the gaze, never the stock vault's re-face */
}

/* the phase's collision hold: while its motion runs under an attack, the attack's EnableCollision stays 0 (what
 * COLLISION_OFF does); restored at the end when the same attack still runs (the next attack's start sets it anyway) */
static void st_collision(int want, double t)
{
	uintptr_t cmb = g_step.cmb;
	uintptr_t atk = cmb && g_step.attack ? LO_FN(LOS_ATTACK_Q, lo_attack_fn)(cmb) : 0;
	if (g_step.col_on && (!want || cmb != g_step.col_cmb || atk != g_step.col_atk)) {
		if (cmb == g_step.col_cmb && atk == g_step.col_atk && atk) {
			unsigned char one = 1;
			if (g_step.col_was && arm_poke(cmb + ST_C_COLLIDE, &g_step.col_was, 1) && arm_poke(cmb + ST_C_COLDIRTY, &one, 1))
				g_step.n_col_restored++;
		}
		g_step.col_on = 0;
		if (st_log_ok(t))
			logf_("STEP t=%.3f the orc's collision: the attack's EnableCollision %s", t,
			      atk && atk == g_step.col_atk ? (g_step.col_was ? "restored to 1" : "was 0 by itself (the stock vault's own)")
							 : "left to the next attack (the evade ended)");
	}
	if (!want || !atk)
		return;
	unsigned char v = 1, zero = 0, one = 1;
	if (!peek(cmb + ST_C_COLLIDE, &v, 1))
		return;
	if (!g_step.col_on) {
		g_step.col_on = 1, g_step.col_cmb = cmb, g_step.col_atk = atk, g_step.col_was = v;
		g_step.n_col++;
		if (st_log_ok(t))
			logf_("STEP t=%.3f the orc's collision off: the stock evade's attack %#lx EnableCollision %u%s", t, (unsigned long)atk, v,
			      v ? " -> 0 for the phase (COLLISION_OFF's write)" : " (already 0: the vault's own record)");
	}
	if (v && arm_poke(cmb + ST_C_COLLIDE, &zero, 1))
		arm_poke(cmb + ST_C_COLDIRTY, &one, 1);
}

/* lo_pm_pre, right after somvr_loco_step: the move's displacement into LOCO's delta; the ends */
static void st_pm(const somvr_loco_in *in, somvr_loco_out *o)
{
	if (!g_step.on || g_step.run.kind == SOMVR_ST_NONE) {
		if (g_step.col_on)
			st_collision(0, cb_now());
		return;
	}
	double t = cb_now();
	somvr_step_tick_in ti;
	memset(&ti, 0, sizeof ti);
	ti.dt = in->dt, ti.owned = o->owned, ti.attack = g_step.attack, ti.have_root = in->have_root, ti.root = in->root;
	ti.have_orc = g_step.run.kind == SOMVR_ST_STEP && st_orc_now(g_step.run.key, t, &ti.orc);
	ti.stick_d = o->delta;
	int kind = g_step.run.kind, motion = g_step.run.t < g_step.run.T;
	float done0 = g_step.run.done_cm;
	uint64_t ticks = g_step.run.ticks + 1, own = g_step.run.owned_ticks, game = g_step.run.game_ticks, hold = g_step.run.hold_ticks;
	float asked = g_step.run.asked_cm, body = g_step.run.body_cm, D = g_step.run.D;
	int seen = g_step.run.attack_seen || g_step.attack;
	somvr_step_tick_out so;
	somvr_step_tick(&g_step.run, &g_step.cfg, &ti, &so);
	g_step.n_ticks++;
	if (so.active && o->owned && somvr_loco_take_assist(&g_lo.s, o, so.d, so.dt)) {
		g_step.n_assist++;
		g_step.assist_cm += hypotf(so.d.x, so.d.z);
	}
	if (so.clamped)
		g_step.n_clamped++;
	if (so.peak) {
		g_step.n_peak++;
		in_haptic_event(SOMVR_HAND_L, 0.35f, 0.10f, SOMVR_HAP_COMBAT); /* the sweep's other end: through the orc */
		if (st_log_ok(t))
			logf_("STEP t=%.3f the phase at the orc's axis (%.1f cm along%s)", t, g_step.run.done_cm,
			      g_step.cfg.phase == SOMVR_ST_PHASE_REFACE ? "; the re-face" : "");
	}
	st_collision(kind == SOMVR_ST_PHASE && motion && !so.ended && g_step.nocollide && g_step.ok_col, t);
	if (so.ended) {
		if (so.ended != SOMVR_STE_BLOCKED)
			asked = g_step.run.asked_cm, body = g_step.run.body_cm;
		if (st_log_ok(t))
			logf_("STEP t=%.3f frame=%llu %s end: %s after %.0f ms; moved %.1f of %.1f cm (the profile), asked %.1f, the body %.1f "
			      "along; ticks %llu (ours %llu, the game's %llu, held %llu); the stock evade %s; blink max %.2f",
			      t, (unsigned long long)rend_frame(), somvr_step_kind_name(kind), somvr_step_end_name(so.ended),
			      (t - g_step.t0) * 1e3, done0 + (so.ended == SOMVR_STE_BLOCKED ? 0 : hypotf(so.d.x, so.d.z)), D, asked, body,
			      (unsigned long long)ticks, (unsigned long long)own, (unsigned long long)game, (unsigned long long)hold,
			      seen ? "ran under it" : "not seen (our own move)", g_step.fade_max);
		if (kind == SOMVR_ST_PHASE && g_step.col_on)
			st_collision(0, t);
	}
}

/* the frame (eye 0, after the fallback's fade): the blink option's black into the record's fade level (the larger one) */
static void st_frame(void)
{
	if (!g_step.on || !g_fr.rec || g_step.run.kind == SOMVR_ST_NONE)
		return;
	float b = somvr_step_blink(&g_step.run, &g_step.cfg);
	if (!(b > 0))
		return;
	uint32_t lv = (uint32_t)(b * 255 + 0.5f), was = somvr_rec_fade(g_fr.rec->flags);
	if (lv > was)
		g_fr.rec->flags = (g_fr.rec->flags & ~SOMVR_REC_FADE_MASK) | lv << SOMVR_REC_FADE_SHIFT;
	g_step.fade_max = fmaxf(g_step.fade_max, b);
	g_step.n_blink++;
}

/* the VR menu's STEP / DASH / PHASE (vm_apply_key): the setting live; a note for its line */
static const char *st_set(int k, const somvr_settings *s)
{
	if (k == SOMVR_SK_STEP)
		g_step.cfg.step = s->step;
	else if (k == SOMVR_SK_DASH)
		g_step.cfg.dash = (int)s->dash;
	else if (k == SOMVR_SK_PHASE)
		g_step.cfg.phase = (int)s->phase;
	if (!g_step.cfg.dash && g_step.run.kind == SOMVR_ST_DASH)
		somvr_step_stop(&g_step.run, SOMVR_STE_OFF);
	if (!g_step.cfg.phase && g_step.run.kind == SOMVR_ST_PHASE)
		somvr_step_stop(&g_step.run, SOMVR_STE_OFF);
	if (!(g_step.cfg.step > 0) && g_step.run.kind == SOMVR_ST_STEP)
		somvr_step_stop(&g_step.run, SOMVR_STE_OFF);
	return g_step.on ? " (live)" : " (no SOMVR_LOCO core: not used)";
}
/* the menu's rows (build 10): with SOMVR_GAMEMOVES=off (the headset builds since 4b: A makes no move in the world) the
 * menu does not show them (somvr_vm_shown): the dash and the phase are inert without A's stock evade (the input's WARN;
 * run 181931: A by an orc with SOMVR_PHASE=on, no STEP line), the step works (181931: one 50 cm pick on a swing) but is
 * a lunge at the orc the stick did not ask for (the comfort rule); the user turned the phase on and "nothing happens"
 * (session 2026-10-24 16:50: STEP END picks 0, ticks 0). SOMVR_STEP / _DASH / _PHASE still configure them for tests */
static uint32_t st_avail(void) { return g_step.on && g_inp.cfg.game_moves ? SOMVR_VMA_MOVES : 0u; }
/* vm_start's start values from the switches */
static void st_settings(somvr_settings *s)
{
	s->step = g_step.cfg.step;
	s->dash = (uint32_t)g_step.cfg.dash;
	s->phase = (uint32_t)g_step.cfg.phase;
}

static void copy_env(const char *k, char *dst, size_t cap); /* the config helpers (somvr.c, below) */

/* somvr_start, with the other configs: the switches (all off by default) */
static void st_config(int *bad)
{
	static const char *const look_w[] = { "vision", "none" }, *const onoff_w[] = { "0", "1" };
	memset(&g_step, 0, sizeof g_step);
	somvr_step_cfg *c = &g_step.cfg;
	somvr_step_cfg_default(c);
	c->step = env_float("SOMVR_STEP", 0, 0, 1, bad);
	c->dash = env_word("SOMVR_DASH", g_step_dash_w, 3, SOMVR_ST_DASH_OFF);
	c->phase = env_word("SOMVR_PHASE", g_step_phase_w, 4, SOMVR_ST_PHASE_OFF);
	c->step_max_cm = env_float("SOMVR_STEP_MAX_CM", c->step_max_cm, 5, 100, bad);
	c->step_s = env_float("SOMVR_STEP_MS", c->step_s * 1000, 60, 500, bad) / 1000;
	c->reach_cm = env_float("SOMVR_STEP_REACH_CM", c->reach_cm, 60, 250, bad);
	c->stop_cm = env_float("SOMVR_STEP_STOP_CM", c->stop_cm, 30, 200, bad);
	c->dash_cm = env_float("SOMVR_DASH_CM", c->dash_cm, 50, 500, bad);
	c->dash_s = env_float("SOMVR_DASH_MS", c->dash_s * 1000, 100, 800, bad) / 1000;
	c->phase_s = env_float("SOMVR_PHASE_MS", c->phase_s * 1000, 150, 1200, bad) / 1000;
	c->phase_behind_cm = env_float("SOMVR_PHASE_BEHIND_CM", c->phase_behind_cm, 40, 300, bad);
	c->phase_range_cm = env_float("SOMVR_PHASE_RANGE_CM", c->phase_range_cm, 100, 400, bad);
	c->hold_s = env_float("SOMVR_PHASE_HOLD_MS", c->hold_s * 1000, 0, 2000, bad) / 1000;
	g_step.look = env_word("SOMVR_PHASE_LOOK", look_w, 2, 0) == 0;
	g_step.nocollide = env_word("SOMVR_PHASE_NOCOLLIDE", onoff_w, 2, 1);
	copy_env("SOMVR_STEP_OWN", g_step.own_txt, sizeof g_step.own_txt);
	if (!g_step.own_txt[0])
		snprintf(g_step.own_txt, sizeof g_step.own_txt, "attack");
	int nb = 0;
	g_step.own_bits = somvr_step_own_bits(g_step.own_txt, &nb);
	if (nb || !g_step.own_bits) {
		logf_("WARN SOMVR_STEP_OWN=%s: %d word(s) not LOCO owner names; using attack", g_step.own_txt, nb);
		*bad = 1;
		snprintf(g_step.own_txt, sizeof g_step.own_txt, "attack");
		g_step.own_bits = SOMVR_LOCO_W_ATTACK;
	}
	g_step.own_bits &= ~(SOMVR_LOCO_W_OFF | SOMVR_LOCO_W_INPUT | SOMVR_LOCO_W_PLAYER | SOMVR_LOCO_W_READ | SOMVR_LOCO_W_DEAD |
			   SOMVR_LOCO_W_MENU | SOMVR_LOCO_W_STRUGGLE); /* never: those keep LOCO out whatever a move wants */
	g_step.want = c->step > 0 || c->dash || c->phase;
}

/* somvr_start, after lo_start (the core's hook) and vm_config (the menu can turn the moves on later): the sites, on */
static void st_start(const struct site_spec *sites, int menu)
{
	g_step.menu = menu;
	if (!g_step.want && !g_step.menu)
		return;
	if (!g_lo.on) {
		logf_("WARN SOMVR_STEP/SOMVR_DASH/SOMVR_PHASE need SOMVR_LOCO=1 (the core's root delta): the assisted step, the dash and "
		      "the wraith phase stay off");
		return;
	}
	g_step_sites = sites;
	uint32_t ok = 0;
	for (int i = 0; i < STS_N; i++) {
		char why[512];
		int good = check_site(&sites[i], why, sizeof why) == 0;
		ok |= good ? 1u << i : 0;
		logf_("STSITE %s %s %#lx -> %#lx: %s", sites[i].name, good ? "ok" : "MISMATCH", (unsigned long)sites[i].addr,
		      (unsigned long)sites[i].target, why);
	}
	uint32_t col = (1u << STS_COL_SOLVER) | (1u << STS_COL_MOVE7) | (1u << STS_COL_OFF) | (1u << STS_COL_DEFAULT),
		 vis = (1u << STS_VISION_GET) | (1u << STS_VISION_ON);
	g_step.ok_col = (ok & col) == col;
	g_step.ok_vis = (ok & vis) == vis;
	if (!g_step.ok_col)
		logf_("WARN SOMVR_PHASE: the collision proofs did not match (sites %#x): the phase leaves the attack's EnableCollision as "
		      "it is", ok);
	if (!g_step.ok_vis && g_step.look)
		logf_("WARN SOMVR_PHASE: the vision proofs did not match (sites %#x): no wraith look", ok);
	g_step.on = 1;
	somvr_step_run_init(&g_step.run);
	const somvr_step_cfg *c = &g_step.cfg;
	logf_("CONFIG step=%.2f (the assisted step: at most %.0f cm x the strength in %.0f ms, the reach %.0f cm, + %.0f, stopping %.0f "
	      "cm from the orc) dash=%s (%.0f cm in %.0f ms) phase=%s (through the orc to %.0f cm beyond it in %.0f ms, within %.0f cm; "
	      "the look %s; the collision hold %s): the evades own LOCO's %s ticks, at most %.0f ms after the motion; orcs from "
	      "SOMVR_COMBAT %s; the menu %s", c->step, c->step_max_cm, c->step_s * 1e3, c->reach_cm, c->step_extra_cm, c->stop_cm,
	      g_step_dash_w[c->dash], c->dash_cm, c->dash_s * 1e3, g_step_phase_w[c->phase], c->phase_behind_cm, c->phase_s * 1e3,
	      c->phase_range_cm, !g_step.look ? "none" : g_step.ok_vis ? "the wraith vision (122)" : "none (no proofs)",
	      !g_step.nocollide ? "off" : g_step.ok_col ? "on (Combat+0x56c)" : "off (no proofs)", g_step.own_txt, c->hold_s * 1e3,
	      g_cb.mode != CB_OFF ? "on" : "OFF (no step, no phase: the dash only)", g_step.menu ? "may change them live" : "off");
	if (g_cb.mode == CB_OFF && (c->step > 0 || c->phase))
		logf_("WARN SOMVR_STEP/SOMVR_PHASE need SOMVR_COMBAT (its orcs): without it only the dash runs");
}

static void st_end_log(void)
{
	if (!g_step.on)
		return;
	const somvr_step_run *r = &g_step.run;
	logf_("STEP END picks step %llu dash %llu phase %llu (cm %.1f / %.1f / %.1f); ends done %llu held %llu hold-over %llu lost %llu "
	      "blocked %llu close %llu new %llu off %llu finisher %llu; none: in reach %llu, stick away %llu, short %llu, no fight %llu, stick at rest "
	      "%llu, off %llu, no orc %llu (stale orcs %llu), a finisher's moment %llu; ticks %llu (assist %llu, %.1f cm), clamped %llu, peaks %llu, re-faces "
	      "%llu; vision on %llu off %llu refused %llu; collision held %llu restored %llu; blink frames %llu; suppressed %llu",
	      (unsigned long long)g_step.n_pick[SOMVR_ST_STEP], (unsigned long long)g_step.n_pick[SOMVR_ST_DASH],
	      (unsigned long long)g_step.n_pick[SOMVR_ST_PHASE], r->cm[SOMVR_ST_STEP], r->cm[SOMVR_ST_DASH], r->cm[SOMVR_ST_PHASE],
	      (unsigned long long)r->ends[SOMVR_STE_DONE], (unsigned long long)r->ends[SOMVR_STE_HELD],
	      (unsigned long long)r->ends[SOMVR_STE_HOLD_OVER], (unsigned long long)r->ends[SOMVR_STE_LOST],
	      (unsigned long long)r->ends[SOMVR_STE_BLOCKED], (unsigned long long)r->ends[SOMVR_STE_CLOSE],
	      (unsigned long long)r->ends[SOMVR_STE_NEW], (unsigned long long)r->ends[SOMVR_STE_OFF],
	      (unsigned long long)r->ends[SOMVR_STE_FINISH],
	      (unsigned long long)g_step.n_none[SOMVR_STW_IN_REACH], (unsigned long long)g_step.n_none[SOMVR_STW_STICK_AWAY],
	      (unsigned long long)g_step.n_none[SOMVR_STW_SHORT], (unsigned long long)g_step.n_none[SOMVR_STW_NO_FIGHT],
	      (unsigned long long)g_step.n_none[SOMVR_STW_NO_STICK], (unsigned long long)g_step.n_none[SOMVR_STW_OFF],
	      (unsigned long long)g_step.n_none[SOMVR_STW_NO_ORC], (unsigned long long)g_step.n_staleorcs,
	      (unsigned long long)g_step.n_fin,
	      (unsigned long long)g_step.n_ticks, (unsigned long long)g_step.n_assist, g_step.assist_cm, (unsigned long long)g_step.n_clamped,
	      (unsigned long long)g_step.n_peak, (unsigned long long)g_step.n_reface, (unsigned long long)g_step.n_vis_on,
	      (unsigned long long)g_step.n_vis_off, (unsigned long long)g_step.n_vis_refused, (unsigned long long)g_step.n_col,
	      (unsigned long long)g_step.n_col_restored, (unsigned long long)g_step.n_blink, (unsigned long long)g_step.suppressed);
}
