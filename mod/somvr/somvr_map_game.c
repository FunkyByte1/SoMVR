/* somvr_map_game.c - build 27 (som-b27-world; docs/re-xr-pipeline.md 111): the game side of somvr_map.h, the world map
 * by the laser and the fingertip (SOMVR_MAP_POINT, code default off); included at the end of somvr.c after
 * somvr_screens_game.c (its player UI reads). ptr_tick calls map_ptr() every input tick: while the player UI's top menu
 * layer is the map (CUIPDAZoneMapLayer, its vtable 0x353e120, slot 10 checked at start), the pointer's presses on the
 * board become the pad's map commands (somvr_map_step): the stick's MapMoveX/Y values (in_apply's map_axis_wants, values
 * only, like the Nemesis hover's), A (MenuEnter + MapSelect), X (MenuAux6), Y (MenuAux7) held 0.12 s (o->cmd). The
 * selection under the cross is a travel point when CUIPDAZoneMapLayer+0xd00 -> +0x38 -> +0x48's type list holds
 * 0x46b8260 (FUN_00bf33d0's own test; read only). Nothing is patched. The word `map status|on|off|pan PXS LAG_MS
 * STEER_PX|hold X Y SEC` (hold = the stick's values held SEC seconds: the pan's rate measured). */

#define MAP_VT_ZONE 0x353e120UL      /* CUIPDAZoneMapLayer's vtable (reference/index/classes.tsv) */
#define MAP_VT_ZONE_VF10 0xc03fc0UL  /* its slot 10: the command handler (0x57 Fast Travel, 0x58 Advance Time) */
#define MAP_L_SEL 0xd00              /* the layer: the selected map icon under the cross */
#define MAP_SEL_OBJ 0x38             /* the icon: its object */
#define MAP_OBJ_TYPES 0x48           /* the object: its type list (a node: +0 the type, +8 the next) */
#define MAP_TYPE_TRAVEL 0x46b8260UL  /* the travel point's type (FUN_00bf33d0, vf_10) */

static struct {
	somvr_map_cfg cfg;
	somvr_map_st st;
	int vt_ok, bad;
	int top, travel;                 /* the map on top, a travel point under the cross (this tick) */
	int naxis;
	float axis[2];
	double hold_until;               /* the word `map hold`: the stick's values until */
	float hold_axis[2];
	uint64_t ticks_on, travel_ticks, held_ticks;
	int lines;
} g_map;
static _Atomic int g_map_req_on = -1, g_map_req_status;
static _Atomic int g_map_req_hold;   /* a `map hold` waiting (its values below) */
static float g_map_req_hold_v[3];
static _Atomic int g_map_req_pan;
static float g_map_req_pan_v[4];

static int map_log_ok(void) { return g_map.lines++ < 300; }

/* the player UI's top menu layer, if the state is the pause menus' (5): its pointer, 0 = none */
static uintptr_t map_top_layer(void)
{
	uintptr_t sh = peek_ptr(g_ing.shell_pp), P = sh && g_ptr.ok_pl ? LP_FN(LP_LOCALPLAYER, lp_player_fn)(sh) : 0;
	uintptr_t ui = P ? peek_ptr(P + SCR_P_UI) : 0, sm = ui ? peek_ptr(ui + SCR_UI_SM) : 0;
	int32_t st = -1;
	if (!sm || !peek(sm + SCR_SM_STATE, &st, sizeof st))
		return 0;
	if (st == -1 && !peek(sm + SCR_SM_STATE0, &st, sizeof st))
		return 0;
	if (st != 5)
		return 0;
	uintptr_t mgr = sm + SCR_SM_MGR;
	uint32_t n = 0;
	uint8_t ovr = 0;
	if (!peek(mgr + SCR_MGR_COUNT, &n, sizeof n) || !peek(mgr + SCR_MGR_OVERRIDE, &ovr, 1) || ovr)
		return 0;
	int top = somvr_scr_top_index(n);
	return top >= 0 ? peek_ptr(mgr + SCR_MGR_LAYERS + 8 * (uintptr_t)top) : 0;
}

/* the selection under the cross is a travel point (the popup "(X) Fast Travel / (Y) Advance Time") */
static int map_travel(uintptr_t layer)
{
	uintptr_t sel = peek_ptr(layer + MAP_L_SEL), obj = sel ? peek_ptr(sel + MAP_SEL_OBJ) : 0;
	uintptr_t lst = obj ? peek_ptr(obj + MAP_OBJ_TYPES) : 0, p = lst ? peek_ptr(lst) : 0;
	for (int i = 0; p && i < 16; i++, p = peek_ptr(p + 8))
		if (p == MAP_TYPE_TRAVEL)
			return 1;
	return 0;
}

/* ptr_tick (the input tick): ptr = the pointer on the board (u, v its point), pressing / down its press */
static void map_ptr(sc_tick_out *o, int ptr, int pressing, int down, float u, float v, int ui_w, int ui_h, double t)
{
	int rq = atomic_exchange(&g_map_req_on, -1);
	if (rq >= 0 && rq != g_map.cfg.on && (g_map.vt_ok || !rq)) {
		logf_("MAP t=%.3f the pointer on the map: %s -> %s (the test word)", t, g_map.cfg.on ? "on" : "off", rq ? "on" : "off");
		g_map.cfg.on = rq;
	}
	if (atomic_exchange(&g_map_req_pan, 0)) {
		g_map.cfg.pan_pxs = g_map_req_pan_v[0], g_map.cfg.lag_s = g_map_req_pan_v[1] / 1000.0f;
		g_map.cfg.steer_px = g_map_req_pan_v[2], g_map.cfg.ysign = g_map_req_pan_v[3];
		logf_("MAP t=%.3f the pan: %.0f px/s, lag %.0f ms, steer %.0f px, MapMoveY %+.0f = up (the test word)", t,
		      g_map.cfg.pan_pxs, g_map.cfg.lag_s * 1000, g_map.cfg.steer_px, g_map.cfg.ysign);
	}
	uintptr_t layer = g_inp.ctx == SC_CTX_MENU && g_map.vt_ok ? map_top_layer() : 0;
	int top = layer && peek_ptr(layer) == MAP_VT_ZONE;
	int travel = top && map_travel(layer);
	int req_status = atomic_exchange(&g_map_req_status, 0);
	if ((top != g_map.top || travel != g_map.travel || req_status) && map_log_ok())
		logf_("MAP t=%.3f %s; the selection under the cross %s a travel point; the pointer %s (%s)", t,
		      top ? "the map is the top menu layer" : "the map is not up", travel ? "is" : "is not",
		      g_map.cfg.on ? "on the map" : "off", somvr_map_phase_names[g_map.st.phase]);
	g_map.top = top, g_map.travel = travel;
	g_map.ticks_on += top, g_map.travel_ticks += travel;
	if (atomic_exchange(&g_map_req_hold, 0)) {
		g_map.hold_axis[0] = g_map_req_hold_v[0], g_map.hold_axis[1] = g_map_req_hold_v[1];
		g_map.hold_until = t + g_map_req_hold_v[2];
		logf_("MAP t=%.3f hold MapMoveX/Y at %.3f,%.3f for %.2f s (the test word)", t, g_map.hold_axis[0],
		      g_map.hold_axis[1], g_map_req_hold_v[2]);
	}
	g_map.naxis = 0;
	if (top && t < g_map.hold_until) {
		g_map.naxis = 2, g_map.axis[0] = g_map.hold_axis[0], g_map.axis[1] = g_map.hold_axis[1];
		g_map.held_ticks++;
		return;
	}
	if (!g_map.cfg.on)
		return;
	float w = ui_w > 0 ? (float)ui_w : 1920.0f, h = ui_h > 0 ? (float)ui_h : 1080.0f;
	somvr_map_in in = { .map = top, .ptr = ptr && isfinite(u) && isfinite(v), .pressing = pressing, .down = down,
			    .px = { (u - 0.5f) * w, (v - 0.5f) * h }, .travel = travel, .t = t };
	int ph = g_map.st.phase;
	somvr_map_out mo;
	somvr_map_step(&g_map.st, &g_map.cfg, &in, &mo);
	if (mo.naxis == 2)
		g_map.naxis = 2, g_map.axis[0] = mo.axis[0], g_map.axis[1] = mo.axis[1];
	for (int k = 0; k < mo.ncmd; k++)
		if (o->ncmd < SC_OUT_CMDS)
			o->cmd[o->ncmd++] = mo.cmd[k];
	if (mo.event == SOMVR_MAPE_NONE || !map_log_ok())
		return;
	if (mo.event == SOMVR_MAPE_CLICK) {
		if (g_map.st.region != SOMVR_MAPR_PAN)
			logf_("MAP t=%.3f a press at %+.0f,%+.0f px from the cross: %s (commands %d%s%s)", t, g_map.st.down[0],
			      g_map.st.down[1], somvr_map_region_names[g_map.st.region], mo.cmd[0], mo.ncmd > 1 ? " and " : "",
			      mo.ncmd > 1 ? (mo.cmd[1] == SOMVR_MAP_CMD_SELECT ? "65" : "?") : "");
		else
			logf_("MAP t=%.3f a press at %+.0f,%+.0f px from the cross: %s (%.0f px: MapMoveX/Y %.3f,%.3f for %.3f s)",
			      t, g_map.st.down[0], g_map.st.down[1], g_map.st.phase == SOMVR_MAPP_PAN ? "the map brought there" :
			      "at the cross already", g_map.st.plan_px, mo.axis[0], mo.axis[1], g_map.st.plan_s);
	} else if (mo.event == SOMVR_MAPE_STEER_ON)
		logf_("MAP t=%.3f still held (the press at %+.0f,%+.0f px): steering the map toward the pointer", t,
		      g_map.st.down[0], g_map.st.down[1]);
	else if (mo.event == SOMVR_MAPE_STEER_OFF || mo.event == SOMVR_MAPE_PAN_END || mo.event == SOMVR_MAPE_CANCEL ||
		 mo.event == SOMVR_MAPE_CLOSED)
		logf_("MAP t=%.3f %s (%s -> %s)", t, mo.event == SOMVR_MAPE_STEER_OFF ? "let go: the steering stopped" :
		      mo.event == SOMVR_MAPE_PAN_END ? "the pan done" : mo.event == SOMVR_MAPE_CANCEL ? "the pan cancelled by a "
		      "new press" : "the map closed", somvr_map_phase_names[ph], somvr_map_phase_names[g_map.st.phase]);
}

/* in_apply (menus): MapMoveX/Y's values this tick */
static int map_axis_wants(int cmd[2], float val[2])
{
	if (g_map.naxis != 2)
		return 0;
	cmd[0] = SOMVR_MAP_CMD_MOVEX, cmd[1] = SOMVR_MAP_CMD_MOVEY, val[0] = g_map.axis[0], val[1] = g_map.axis[1];
	return 2;
}

static void map_start(int *bad)
{
	memset(&g_map, 0, sizeof g_map);
	somvr_map_init(&g_map.st);
	const char *v = getenv("SOMVR_MAP_POINT");
	if (somvr_map_parse(v, &g_map.cfg)) {
		*bad = 1;
		logf_("WARN SOMVR_MAP_POINT=%s is not on|off[,PXS,LAG_MS,STEER_PX]: off", v);
	}
	uintptr_t vf = 0;
	g_map.vt_ok = peek(MAP_VT_ZONE + 10 * 8, &vf, sizeof vf) && vf == MAP_VT_ZONE_VF10;
	if (g_map.cfg.on && (!g_ptr.on || !g_inp.inject || !g_map.vt_ok)) {
		logf_("WARN SOMVR_MAP_POINT=on: %s: off", !g_ptr.on ? "needs SOMVR_POINTER" : !g_inp.inject ?
		      "needs SOMVR_INPUT's command events" : "CUIPDAZoneMapLayer's vtable does not match");
		g_map.cfg.on = 0, *bad = 1;
	}
	if (g_map.cfg.on || v)
		logf_("CONFIG map pointer %s (SOMVR_MAP_POINT): %s; the pan %.0f px/s, lag %.0f ms, steer %.0f px, steering after "
		      "%.0f ms held; CUIPDAZoneMapLayer's vtable %#lx %s", g_map.cfg.on ? "on" : "off", g_map.cfg.on ?
		      "the laser's or the fingertip's click on the map brings that place to the cross, on the cross = A (the "
		      "waypoint), on the tower's popup = X (Fast Travel) / Y (Advance Time); still held, it steers the map" :
		      "the map by the pad's buttons only", g_map.cfg.pan_pxs, g_map.cfg.lag_s * 1000, g_map.cfg.steer_px,
		      g_map.cfg.click_s * 1000, MAP_VT_ZONE, g_map.vt_ok ? "checked" : "NOT matched");
}

/* the trigger word "map ..." (the writer thread) */
static const char *map_word(const char *w)
{
	char a[16] = "";
	float x = 0, y = 0, s = 0;
	if (sscanf(w, "map %15s", a) < 1)
		return "map (not status|on|off|pan PXS LAG_MS STEER_PX [YSIGN]|hold X Y SEC)";
	if (!strcmp(a, "status")) {
		atomic_store(&g_map_req_status, 1);
		return "map status";
	}
	if (!strcmp(a, "on") || !strcmp(a, "off")) {
		atomic_store(&g_map_req_on, a[1] == 'n');
		return a[1] == 'n' ? "map on" : "map off";
	}
	float ys = 1;
	int np = sscanf(w, "map pan %f %f %f %f", &x, &y, &s, &ys);
	if (!strcmp(a, "pan") && np >= 3 && x >= 50 && x <= 5000 && y >= -500 && y <= 1000 && s >= 50 && s <= 1000 &&
	    (ys == 1 || ys == -1)) {
		g_map_req_pan_v[0] = x, g_map_req_pan_v[1] = y, g_map_req_pan_v[2] = s, g_map_req_pan_v[3] = ys;
		atomic_store(&g_map_req_pan, 1);
		return "map pan";
	}
	if (!strcmp(a, "hold") && sscanf(w, "map hold %f %f %f", &x, &y, &s) == 3 && fabsf(x) <= 1 && fabsf(y) <= 1 &&
	    s > 0 && s <= 10) {
		g_map_req_hold_v[0] = x, g_map_req_hold_v[1] = y, g_map_req_hold_v[2] = s;
		atomic_store(&g_map_req_hold, 1);
		return "map hold";
	}
	return "map (not status|on|off|pan PXS LAG_MS STEER_PX [YSIGN]|hold X Y SEC)";
}

static void map_end_log(void)
{
	if (!g_map.cfg.on && !g_map.ticks_on)
		return;
	logf_("MAP END pointer %s: the map up %llu tick(s) (a travel point under the cross %llu); clicks %llu: pans %llu "
	      "(cancelled %llu), A %llu, X %llu, Y %llu; held presses steering %llu; the stick's ticks %llu (the word's hold "
	      "%llu)", g_map.cfg.on ? "on" : "off", (unsigned long long)g_map.ticks_on, (unsigned long long)g_map.travel_ticks,
	      (unsigned long long)g_map.st.clicks, (unsigned long long)g_map.st.pans, (unsigned long long)g_map.st.cancels,
	      (unsigned long long)g_map.st.a, (unsigned long long)g_map.st.x, (unsigned long long)g_map.st.y,
	      (unsigned long long)g_map.st.steers, (unsigned long long)g_map.st.ticks_axis,
	      (unsigned long long)g_map.held_ticks);
}
