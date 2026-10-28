/* somvr_wstep_game.c - build 18: the wraith step's game side (SOMVR_WRAITH_STEP; somvr_wstep.h, docs/re-xr-pipeline.md
 * 68). Included by somvr.c last (after the practice's, the strike's and the hold's):
 *   ws_config (st_config, at the start): the switch and its knobs;
 *   ws_tick (the input tick, after cl_tick, before lo_tick): the move stick's tilt for ws_frame; while aiming (and
 *     until the cancel's push has rested) the walk held off, while the right stick is still the aim's no turn;
 *   ws_frame (H4, the game thread, a complete stereo frame, before ob_frame): the right stick, the head's ray, Talion
 *     and the ground under him, the orcs near; somvr_ws_tick; while aiming the landing every frame (somvr_ws_find
 *     through the climb's cl_ray); the go = the practice's quick blink down onto the ring (pr_down_start ..., 3: the
 *     drop mark's 0.12 s out, the Teleport, 0.1 s settle, 0.2 s in); the arrival's distance;
 *   ws_entry (ob_frame): the ring into the frame's objective entry (SOMVR_OBF_LAND; refused: | SOMVR_OBF_LAND_NO).
 * Lines: `CONFIG wraith step ...`, `WSTEP t=...` (the aim, each landing found as it changes, the go, the arrival, the
 * cancel, nothing, refused, lost), `WSTEP END ...`. */

#define WS_POST_CM 170.0f /* the gold ring's post: the line from this over the ring down to it */

static struct {
	int want, on;          /* want: SOMVR_WRAITH_STEP=on at the start (the word can turn it off and on again) */
	somvr_ws_cfg cfg;
	somvr_ws_state s;
	int world;             /* the input tick: live, in the world */
	float move;            /* ... the move stick's tilt */
	int have_head;
	sv_vec3 eye, gaze;
	int have_me, have_floor;
	sv_vec3 me;            /* Talion's root (his object's position) */
	float floor_y;
	const char *cannot;    /* why it cannot go now (NULL = it can) */
	int going;             /* the blink runs: its start, its landing, its target */
	double go_t;
	sv_vec3 go_from, go_land, go_to;
	int last_kind, last_why;
	double next_aim_line;
	uint64_t finds, rays, frames_ok, frames_no, other_tid, walk_ticks, turn_ticks, arrived, blink_refused, head_away;
	float off_max, step_max;
	int lines;
	_Atomic int req_onoff; /* the word "wstep on|off" (-1 none) */
	_Atomic int req_state; /* the word "wstep state" */
} g_ws = { .req_onoff = -1 };

static int ws_log_ok(void) { return g_ws.lines++ < 600; }
static int ws_wanted(void) { return g_ws.want; }
/* build 19 (re-xr-pipeline.md 73): the VR menu's Wraith step row: shown with SOMVR_WRAITH_STEP=on; on / off at the next
 * frame (the word's request: an aim on now is dropped, no blink) */
static uint64_t ws_vm_avail(void) { return g_ws.want ? SOMVR_VMA_WSTEP : 0u; }
static void ws_vm_settings(somvr_settings *s) { s->wstep = g_ws.want != 0; }
static const char *ws_vm_set(const somvr_settings *s)
{
	if (!g_ws.want)
		return " (SOMVR_WRAITH_STEP is off: not used)";
	atomic_store(&g_ws.req_onoff, s->wstep != 0);
	return s->wstep ? " (the right stick forward aims the blink)" : " (the right stick forward does nothing)";
}

static void ws_config(int *bad)
{
	somvr_ws_cfg_default(&g_ws.cfg);
	somvr_ws_init(&g_ws.s);
	g_ws.want = g_ws.on = env_word("SOMVR_WRAITH_STEP", (const char *const[]){ "off", "on" }, 2, 0);
	g_ws.cfg.max_cm = env_float("SOMVR_WRAITH_STEP_M", 7, 3, 10, bad) * 100;
	g_ws.cfg.min_s = env_float("SOMVR_WRAITH_STEP_HOLD_MS", 250, 0, 1500, bad) / 1000;
	g_ws.cfg.cool_s = env_float("SOMVR_WRAITH_STEP_COOL_MS", 1000, 0, 10000, bad) / 1000;
	if (g_ws.on)
		logf_("CONFIG wraith step=on: the right stick pushed forward and held = aim (a ring where the head's ray meets the "
		      "ground, %.1f to %.1f m; gold = there, red and crossed = not there: a wall, a ledge over %.1f m, over %.1f m up, "
		      "water, no room, an orc there, out of sight); let go after %.0f ms = the quick blink onto the gold ring; the "
		      "move stick past %.1f while aiming = the cancel; the next after %.0f ms; no game cost (SOMVR_WRAITH_STEP)",
		      g_ws.cfg.min_cm / 100, g_ws.cfg.max_cm / 100, g_ws.cfg.drop_cm / 100, g_ws.cfg.rise_cm / 100,
		      g_ws.cfg.min_s * 1e3, g_ws.cfg.cancel, g_ws.cfg.cool_s * 1e3);
}

/* the word "wstep on|off|state" (SOMVR_HANDS_TEST's trigger file; another thread: requests only) */
static const char *ws_word(const char *w)
{
	if (!g_ws.want)
		return "wstep: SOMVR_WRAITH_STEP is not on";
	if (!strcmp(w, "on") || !strcmp(w, "off")) {
		atomic_store(&g_ws.req_onoff, w[1] == 'n');
		return w[1] == 'n' ? "wstep on" : "wstep off";
	}
	if (!strcmp(w, "state")) {
		atomic_store(&g_ws.req_state, 1);
		return "wstep state";
	}
	return "wstep on|off|state";
}

/* the input tick: the move stick's tilt (the cancel's), then the walk and the turn held off while aiming */
static void ws_tick(sc_tick_out *o)
{
	g_ws.world = o->live && o->ctx == SC_CTX_WORLD;
	g_ws.move = g_ws.world ? sqrtf(o->move[0] * o->move[0] + o->move[1] * o->move[1]) : 0;
	if (!g_ws.on || o->ctx == SC_CTX_MENU)
		return;
	if (!somvr_ws_walk_ok(&g_ws.s)) { /* the move stick is the cancel now, not a walk */
		o->pad[SC_PAD_LX] = o->pad[SC_PAD_LY] = 0;
		o->pad_on &= ~((1u << SC_PAD_LX) | (1u << SC_PAD_LY));
		o->move[0] = o->move[1] = 0;
		g_ws.walk_ticks++;
	}
	if (g_ws.s.phase != SOMVR_WSP_IDLE && (o->snap || o->turn != 0)) /* the right stick is the aim's until it rests */
		o->snap = 0, o->turn = 0, g_ws.turn_ticks++;
}

/* Talion's root and the ground under him (1 = on the ground) */
static int ws_ground(uintptr_t pl)
{
	uintptr_t pm = 0, obj = 0;
	float p[3];
	g_ws.have_me = pl && player_objs(pl, &pm, &obj) && peek(obj + OBJ_POS, p, sizeof p) && vec_ok(p, 1e7f);
	g_ws.have_floor = 0;
	if (!g_ws.have_me)
		return 0;
	g_ws.me = (sv_vec3){ p[0], p[1], p[2] };
	float foot = g_cl.cfg.foot_cm > 0 ? g_cl.cfg.foot_cm : 102.0f;
	somvr_climb_hit h;
	memset(&h, 0, sizeof h);
	if (cl_ray(NULL, -1, (sv_vec3){ p[0], p[1] + 30.0f, p[2] }, (sv_vec3){ 0, -1, 0 }, 30.0f + foot + 60.0f, &h) && h.hit &&
	    !h.chr)
		g_ws.floor_y = h.point.y, g_ws.have_floor = 1;
	return g_ws.have_floor;
}

static const char *ws_why_not(uintptr_t pl)
{
	if (!g_ws.have_head)
		return "no head pose";
	if (fb_third())
		return "the view is third person";
	if (!ws_ground(pl))
		return g_ws.have_me ? "not on the ground" : "no player";
	if (g_cl.on && (g_cl.s.active || g_cl.s.phase != SOMVR_CLIMB_PH_NONE))
		return "a climb";
	if (g_cl.on && g_cl.dm_show)
		return "the drop's ring is up";
	if (bo_out())
		return "the bow is out";
	if (fi_holds())
		return "a finisher";
	if (sk_away())
		return "the Shadow Strike";
	if (g_tk.on && g_tk.active)
		return "an enemy's sync holds Talion";
	if (g_step.run.kind != SOMVR_ST_NONE)
		return "a dodge or a phase runs";
	if (game_paused())
		return "paused";
	if (!pr_down_ok())
		return !g_pr.on ? "the practice is off (the blink is its trip's)" : "a blink runs";
	return NULL;
}

static void ws_aim(double t)
{
	if (hypotf(g_ws.eye.x - g_ws.me.x, g_ws.eye.z - g_ws.me.z) > 200.0f) { /* run 184609 frame 4423: one frame's view 4 m
										 * off Talion (an orc's hit): the last ring kept */
		g_ws.head_away++;
		return;
	}
	uintptr_t pl = g_fr.pl, pcomb = pl ? peek_ptr(pl + P_COMBAT) : 0, pc = pcomb ? peek_ptr(pcomb + PC_SERVER) : 0;
	if (pc && vptr_of((void *)pc) != g_cbg.vt_combat)
		pc = 0;
	struct cb_orc near[8];
	sv_vec3 orcs[8];
	float at[3] = { g_ws.me.x, g_ws.me.y, g_ws.me.z };
	int n = pcomb ? cb_orcs_r(pcomb, pc, at, g_ws.cfg.max_cm + 300.0f, near, 8) : 0;
	for (int i = 0; i < n; i++)
		orcs[i] = (sv_vec3){ near[i].pos[0], near[i].pos[1], near[i].pos[2] };
	somvr_ws_aim a;
	somvr_ws_find(&g_ws.cfg, cl_ray, NULL, g_ws.eye, g_ws.gaze, g_ws.me, g_ws.floor_y, orcs, n > 0 ? n : 0, &a);
	g_ws.finds++, g_ws.rays += (uint64_t)a.rays;
	uint32_t ev = somvr_ws_aimed(&g_ws.s, &a);
	if (ev & SOMVR_WSE_TURN_NO) /* the pullover's soft "no" (re-xr-pipeline.md 55.1) */
		in_haptic_double(SOMVR_HAND_R, 0.2f, 0.02f, 0.07f, SOMVR_HAP_UI);
	else if (ev & SOMVR_WSE_TURN_OK)
		in_haptic_event(SOMVR_HAND_R, 0.15f, 0.02f, SOMVR_HAP_UI);
	if ((a.kind != g_ws.last_kind || a.why != g_ws.last_why || t >= g_ws.next_aim_line) && ws_log_ok()) {
		float pit = asinf(fmaxf(-1, fminf(1, g_ws.gaze.y))) * 57.29578f;
		logf_("WSTEP t=%.3f frame=%llu aim: %s%s%s at (%.0f %.0f %.0f), %.0f cm from Talion along the floor, %+.0f cm over "
		      "his floor%s%s; the head's ray %+.1f deg (pitch) from (%.0f %.0f %.0f); orcs near %d; %d casts", t,
		      (unsigned long long)g_fr.frame, a.kind == SOMVR_WSA_OK ? "the gold ring" : a.kind == SOMVR_WSA_NO ? "refused: "
		      : "no ring: ", a.kind == SOMVR_WSA_OK ? "" : somvr_ws_why_name(a.why), a.kind == SOMVR_WSA_NO ? " (the red crossed ring)" : "",
		      a.land.x, a.land.y, a.land.z, a.dist_cm, a.rise_cm, a.far ? ", the farthest (the head's ray past it)" : "",
		      a.through ? ", through an orc" : "", pit, g_ws.eye.x, g_ws.eye.y, g_ws.eye.z, n, a.rays);
		g_ws.next_aim_line = t + 1.0;
	}
	g_ws.last_kind = a.kind, g_ws.last_why = a.why;
}

/* the blink ended: where Talion stands against the ring */
static void ws_arrival(double t)
{
	if (g_pr.trip.phase != SOMVR_PRT_IDLE || t - g_ws.go_t < 0.05)
		return;
	g_ws.going = 0;
	uintptr_t pm = 0, obj = 0;
	float p[3];
	if (!(g_fr.pl && player_objs(g_fr.pl, &pm, &obj) && peek(obj + OBJ_POS, p, sizeof p) && vec_ok(p, 1e7f)))
		return;
	float step = hypotf(p[0] - g_ws.go_from.x, p[2] - g_ws.go_from.z), off = hypotf(p[0] - g_ws.go_land.x, p[2] - g_ws.go_land.z);
	g_ws.arrived++, g_ws.off_max = fmaxf(g_ws.off_max, off), g_ws.step_max = fmaxf(g_ws.step_max, step);
	if (ws_log_ok())
		logf_("WSTEP t=%.3f frame=%llu arrived %.0f ms after the let-go: Talion at (%.0f %.0f %.0f): the step %.0f cm along "
		      "the floor; %.1f cm from the ring's middle, his root %+.0f cm over its floor (the target %+.0f)", t,
		      (unsigned long long)g_fr.frame, (t - g_ws.go_t) * 1e3, p[0], p[1], p[2], step, off, p[1] - g_ws.go_land.y,
		      g_ws.go_to.y - g_ws.go_land.y);
}

static void ws_frame(uintptr_t pl, int stereo)
{
	int rq = atomic_exchange(&g_ws.req_onoff, -1);
	if (rq >= 0 && g_ws.want) {
		g_ws.on = rq;
		somvr_ws_init(&g_ws.s);
		logf_("WSTEP t=%.3f now %s (the word or the VR menu's Wraith step row)", cb_now(), rq ? "on" : "off");
	}
	if (!g_ws.on || !stereo || !pl)
		return;
	if (my_tid() != g_inp.tid) {
		g_ws.other_tid++;
		return;
	}
	double t = cb_now();
	sv_vec3 hp;
	sv_quat hq;
	somvr_rig_head(&g_rig, g_fr.base, g_fr.views.pose, &hp, &hq);
	sv_vec3 f = sv_qrot(sv_qnormalize(hq), (sv_vec3){ 0, 0, 1 });
	g_ws.have_head = isfinite(hp.x + hp.y + hp.z + f.x + f.y + f.z) && fabsf(hp.x) < 1e7f;
	g_ws.eye = hp, g_ws.gaze = f;
	if (g_ws.going)
		ws_arrival(t);
	const somvr_hand_in *R = &g_inp.st.hand[SOMVR_HAND_R];
	int rin = g_inp.have_st && (R->active & SOMVR_HACT_INPUT) && isfinite(R->stick[0] + R->stick[1]);
	somvr_ws_in in = { .t = t, .world = g_ws.world, .rx = rin ? R->stick[0] : 0, .ry = rin ? R->stick[1] : 0, .move = g_ws.move };
	int asks = g_ws.s.phase == SOMVR_WSP_AIM || (g_ws.s.phase == SOMVR_WSP_IDLE && in.world && in.ry >= g_ws.cfg.on);
	g_ws.cannot = asks ? ws_why_not(pl) : NULL;
	in.can = asks && !g_ws.cannot;
	if (atomic_exchange(&g_ws.req_state, 0))
		logf_("WSTEP state t=%.3f: phase %d, the right stick (%.2f %.2f), the move stick %.2f, world %d, %s; Talion (%.0f %.0f "
		      "%.0f) floor %.0f (%d); the head (%.0f %.0f %.0f) ray (%.3f %.3f %.3f)", t, g_ws.s.phase, in.rx, in.ry, in.move,
		      in.world, g_ws.cannot ? g_ws.cannot : "can go", g_ws.me.x, g_ws.me.y, g_ws.me.z, g_ws.floor_y, g_ws.have_floor,
		      hp.x, hp.y, hp.z, f.x, f.y, f.z);
	uint32_t ev = somvr_ws_tick(&g_ws.s, &g_ws.cfg, &in);
	if (ev & SOMVR_WSE_AIM) {
		g_ws.last_kind = -1, g_ws.last_why = -1;
		if (ws_log_ok())
			logf_("WSTEP t=%.3f frame=%llu the right stick forward (%.2f %.2f): aiming (Talion at %.0f %.0f %.0f, the floor "
			      "%.0f)", t, (unsigned long long)g_fr.frame, in.rx, in.ry, g_ws.me.x, g_ws.me.y, g_ws.me.z, g_ws.floor_y);
	}
	if (ev & SOMVR_WSE_REFUSED) {
		in_haptic_double(SOMVR_HAND_R, 0.2f, 0.02f, 0.07f, SOMVR_HAP_UI);
		if (ws_log_ok())
			logf_("WSTEP t=%.3f frame=%llu the right stick forward: refused: %s (a soft double buzz; nothing until the stick "
			      "rests)", t, (unsigned long long)g_fr.frame, g_ws.s.refused == 1 ? "the cooldown" : g_ws.cannot ? g_ws.cannot : "?");
	}
	if (ev & SOMVR_WSE_LOST && ws_log_ok())
		logf_("WSTEP t=%.3f frame=%llu lost after %.2f s: %s: no ring, nothing moves", t, (unsigned long long)g_fr.frame,
		      t - g_ws.s.t0, !in.world ? "a menu or the input lost" : g_ws.cannot ? g_ws.cannot : "?");
	if (ev & SOMVR_WSE_CANCEL && ws_log_ok())
		logf_("WSTEP t=%.3f frame=%llu cancelled by the move stick (%.2f) after %.2f s: no ring, nothing moves; the walk waits "
		      "for the stick's rest", t, (unsigned long long)g_fr.frame, in.move, t - g_ws.s.t0);
	if (ev & SOMVR_WSE_NOGO && ws_log_ok())
		logf_("WSTEP t=%.3f frame=%llu let go after %.2f s: %s: nothing moves", t, (unsigned long long)g_fr.frame,
		      g_ws.s.held_s, g_ws.s.held_s < g_ws.cfg.min_s ? "a flick (under the hold)"
		      : g_ws.last_kind == SOMVR_WSA_NO ? "over the red ring" : "no ring");
	if (ev & SOMVR_WSE_GO) {
		sv_vec3 l = g_ws.s.aim.land;
		float foot = g_cl.cfg.foot_cm > 0 ? g_cl.cfg.foot_cm : 102.0f;
		sv_vec3 to = { l.x, l.y + foot + 5.0f, l.z };
		if (!pr_down_start(to, t, 3)) {
			g_ws.blink_refused++;
			in_haptic_double(SOMVR_HAND_R, 0.2f, 0.02f, 0.07f, SOMVR_HAP_UI);
			if (ws_log_ok())
				logf_("WSTEP t=%.3f frame=%llu let go after %.2f s: the blink refused (the practice's trip): nothing moves", t,
				      (unsigned long long)g_fr.frame, g_ws.s.held_s);
		} else {
			g_ws.going = 1, g_ws.go_t = t, g_ws.go_from = g_ws.me, g_ws.go_land = l, g_ws.go_to = to;
			in_haptic_event(SOMVR_HAND_L, 0.3f, 0.05f, SOMVR_HAP_COMBAT), in_haptic_event(SOMVR_HAND_R, 0.3f, 0.05f, SOMVR_HAP_COMBAT);
			if (ws_log_ok())
				logf_("WSTEP t=%.3f frame=%llu let go after %.2f s: the blink onto the gold ring at (%.0f %.0f %.0f), %.0f cm from "
				      "Talion along the floor, %+.0f cm over his floor, under the quick fade (the target %.0f %.0f %.0f)", t,
				      (unsigned long long)g_fr.frame, g_ws.s.held_s, l.x, l.y, l.z, hypotf(l.x - g_ws.me.x, l.z - g_ws.me.z),
				      l.y - g_ws.floor_y, to.x, to.y, to.z);
		}
	}
	if (g_ws.s.phase == SOMVR_WSP_AIM)
		ws_aim(t);
}

/* ob_frame: while aiming, the ring into this frame's objective entry (1 = published) */
static int ws_entry(somvr_objective_frame *e)
{
	if (!g_ws.on || g_ws.s.phase != SOMVR_WSP_AIM || !g_ws.s.have_aim || g_ws.s.aim.kind == SOMVR_WSA_NONE || game_paused())
		return 0;
	somvr_th_map mp = { { g_fr.base.pos.x, g_fr.base.pos.y, g_fr.base.pos.z }, g_fr.base.yaw - g_rig.yaw0,
			    { g_rig.p0.x, g_rig.p0.y, g_rig.p0.z }, somvr_rig_scale(&g_rig) };
	sv_vec3 l = g_ws.s.aim.land;
	float w[3] = { l.x, l.y, l.z }, w2[3] = { w[0] + 35.0f, w[1], w[2] }, x2[3];
	somvr_th_to_xr(&mp, w, e->pos), somvr_th_to_xr(&mp, w2, x2);
	e->n = 1, e->flags = SOMVR_OBF_LAND | (g_ws.s.aim.kind == SOMVR_WSA_NO ? SOMVR_OBF_LAND_NO : 0u), e->id = 0;
	e->dist_m = sqrtf((x2[0] - e->pos[0]) * (x2[0] - e->pos[0]) + (x2[1] - e->pos[1]) * (x2[1] - e->pos[1]) +
			  (x2[2] - e->pos[2]) * (x2[2] - e->pos[2])); /* the ring's radius: Talion's capsule's (35 cm) */
	if (g_ws.s.aim.kind == SOMVR_WSA_OK) { /* and the drop's line standing up from it, Talion's height: the flat ring alone
						* is a thin ellipse at 5-7 m (run 184609 frame 518) */
		float wt[3] = { l.x, l.y + WS_POST_CM, l.z };
		somvr_th_to_xr(&mp, wt, e->top);
		e->flags |= SOMVR_OBF_LINE;
	}
	if (g_ws.s.aim.kind == SOMVR_WSA_OK)
		g_ws.frames_ok++;
	else
		g_ws.frames_no++;
	return 1;
}

static void ws_end_log(void)
{
	if (!g_ws.want)
		return;
	const somvr_ws_state *s = &g_ws.s;
	logf_("WSTEP END build 18 wraith step: aims %llu, goes %llu (arrived %llu, the longest %.0f cm, at most %.1f cm off the "
	      "ring; the blink refused %llu), nothing on the let-go %llu (flicks %llu), cancels %llu, lost %llu, refused %llu "
	      "(the cooldown %llu), turns to red %llu; frames with the gold ring %llu, the red %llu; finds %llu (%llu casts); the "
	      "walk held %llu ticks, the turn %llu; the view off Talion %llu (the ring kept); other thread %llu",
	      (unsigned long long)s->n_aim, (unsigned long long)s->n_go,
	      (unsigned long long)g_ws.arrived, g_ws.step_max, g_ws.off_max, (unsigned long long)g_ws.blink_refused,
	      (unsigned long long)s->n_nogo, (unsigned long long)s->n_tap, (unsigned long long)s->n_cancel,
	      (unsigned long long)s->n_lost, (unsigned long long)(s->n_cool + s->n_cannot), (unsigned long long)s->n_cool,
	      (unsigned long long)s->n_turn_no, (unsigned long long)g_ws.frames_ok, (unsigned long long)g_ws.frames_no,
	      (unsigned long long)g_ws.finds, (unsigned long long)g_ws.rays, (unsigned long long)g_ws.walk_ticks,
	      (unsigned long long)g_ws.turn_ticks, (unsigned long long)g_ws.head_away, (unsigned long long)g_ws.other_tid);
}
