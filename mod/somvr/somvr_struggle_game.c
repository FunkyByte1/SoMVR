/* somvr_struggle_game.c - build 27 (som-b27-combat; docs/re-xr-pipeline.md 108.6): the death struggle's VR answer
 * (SOMVR_STRUGGLE). Not a translation unit of its own: somvr.c includes it last (it uses somvr.c's input tick, peek, the
 * rig, the haptics and the logs).
 *
 * When Talion goes down near an orc ('PC_RecoilAny_PreDeath', then the orc's '*_PreDeath' sync) the game starts a
 * struggle - the "last chance" - at PlayerCombat+0x8d8 (from a combat state, FUN_00e15690) or +0x8e0 (from a sync
 * action, FUN_00e16ef0) (re-m1m2.md 6.2). Each struggle class listens to one command through its OnCommandOn delegate
 * (re-xr-pipeline.md 108.6, static): CStrugglePressCount (vtable 0x3592ab8) counts presses of its command (+0x2f0) up to
 * its need (+0x2f4; the count +0x2f5, the phase +0x2f6: 0 waiting, 1 open, 2 over; +0x8c 1 = done by the presses);
 * CStruggleQuickTime (0x3592af8) one press of its command (+0xd0); CStruggleAim (0x3592bb8) a press of its command
 * (+0x58c) while its cursor is in the target; CStruggleHold (0x3592b38) and CStruggleInputDirection (0x3592b78): logged
 * only. The pad's buttons (A/B/X/Y pass through in a struggle) reach the command only through the binding the game
 * shows as a glyph; the VR player has no such button in mind (soak 2: 167 s on the knees).
 *
 * The answer: **a hard thrust of either hand** (its speed in the world over 80-250 ms past SOMVR_STRUGGLE_MPS, 2.0 m/s;
 * re-armed under a third of it) = one press of the struggle's own command (read from the object, our tap: 2 input ticks
 * on, 2 off; a thrust queued while one runs counts too). Stab or shove the orc off you: a mash is many thrusts, a timed
 * press is one. A pulse in that hand per press; at the struggle's start an urgent double pulse in both hands (now!).
 *   stg_hands (in_frame, after py_hands): the hands' history, the thrusts;
 *   stg_tick (the input tick, after st_tick): the struggle's object, its class and command, the taps, the logs;
 *   stg_config / stg_end_log (start / end). Lines: `CONFIG struggle`, `STRUGGLE t=...` (start with its fields and a
 *   dump, the thrusts, the presses, its fields as they change, the end and Talion's state), `STRUGGLE END`. */

#define STG_VT_PRESS 0x3592ab8u
#define STG_VT_QT 0x3592af8u
#define STG_VT_HOLD 0x3592b38u
#define STG_VT_DIR 0x3592b78u
#define STG_VT_AIM 0x3592bb8u
enum { STG_NONE = 0, STG_PRESS, STG_QT, STG_HOLD, STG_DIR, STG_AIM, STG_OTHER, STG_N };
static const char *const g_stg_kind[STG_N] = { "none", "CStrugglePressCount", "CStruggleQuickTime", "CStruggleHold",
					       "CStruggleInputDirection", "CStruggleAim", "unknown" };
#define STG_HIST 16

static struct {
	int on;                  /* SOMVR_STRUGGLE=thrust (1) | off (0) */
	float mps;               /* SOMVR_STRUGGLE_MPS */
	uintptr_t obj;           /* the struggle now (0 = none) */
	int kind, slot, cmd;     /* its class, 0 = the state's (+0x8d8) / 1 = the sync's (+0x8e0), its command */
	double t0, next_line;
	unsigned char f[0x600];  /* its last read (the change lines) */
	int have_f;
	struct { double t; sv_vec3 p; } h[2][STG_HIST];
	int nh[2], hpos[2], armed[2];
	int pending, tap_on, tap_off;
	float aim_cm;            /* SOMVR_STRUGGLE_AIM_CM: a hand this far from where it was at the start = the aim's full tilt */
	int aim_have, aim_hand;  /* CStruggleAim: the hand's aim now (MenuAxisX/Y, the cursor's analog direction) */
	float aim[2], aim0[2][2];/* the aim (x right, y up; -1..1); each hand's place at the start (cm, the head's right/up) */
	int aim0_ok[2];
	double aim_line;
	uint64_t n_aim_ticks, n_aim_pad, n_aim_outside;
	int aim_via;             /* SOMVR_STRUGGLE_AIM: 1 the pad's left stick, 2 MenuAxisX/Y, 3 both */
	int death;               /* the struggle is Talion's last chance (his attack '*PreDeath*'): ours to answer */
	int finisher;            /* build 27, SOMVR_STRUGGLE_FINISHER: a beaten captain's Eliminate ('PC_Bip_Finisher_*') too */
	int fin_now;             /* this struggle is that finisher's */
	uintptr_t fin_c;         /* its captain (the Last Words answered last) */
	int fin_stab;            /* a stab of the sword hand at him: the completion asked (stg_tick writes it) */
	float fin_cos;           /* SOMVR_STRUGGLE_FINISHER_DEG's cosine: how near the stab's line must point at him */
	uint64_t n_fin, n_fin_stabs, n_fin_won;
	uint64_t n_struggles, n_kind[STG_N], n_thrusts, n_thrusts_in, n_presses, n_nocmd, n_done;
	int lines;
} g_stg = { .armed = { 1, 1 } };

static int stg_log_ok(void) { return g_stg.lines++ < 400; }

/* Talion's attack record now (his Combat's: the PreDeath sync's own, 'PC_PreDeathRecover' when he gets up) */
static const char *stg_talion_attack(char *buf, size_t cap)
{
	uintptr_t pcomb = g_inp.pl ? peek_ptr(g_inp.pl + P_COMBAT) : 0, pc = pcomb ? peek_ptr(pcomb + PC_SERVER) : 0;
	snprintf(buf, cap, "-");
	if (pc && vptr_of((void *)pc) == g_cbg.vt_combat)
		rx_rec_name(peek_ptr(pc + RX_C_ATTACK), buf, cap);
	return buf;
}

static void stg_config(void)
{
	int bad = 0;
	g_stg.on = env_word("SOMVR_STRUGGLE", (const char *const[]){ "off", "thrust" }, 2, 0);
	g_stg.mps = env_float("SOMVR_STRUGGLE_MPS", 2.0f, 0.8f, 6.0f, &bad);
	g_stg.aim_cm = env_float("SOMVR_STRUGGLE_AIM_CM", 20.0f, 8.0f, 60.0f, &bad);
	g_stg.aim_via = 1 + env_word("SOMVR_STRUGGLE_AIM", (const char *const[]){ "stick", "axes", "both" }, 3, 2);
	g_stg.finisher = env_word("SOMVR_STRUGGLE_FINISHER", (const char *const[]){ "off", "on" }, 2, 1); /* build 27 */
	g_stg.fin_cos = cosf(env_float("SOMVR_STRUGGLE_FINISHER_DEG", 40.0f, 10.0f, 90.0f, &bad) / 57.29578f);
	logf_("CONFIG struggle=%s: in the game's death struggle (its last chance) a hard thrust of either hand (over %.1f m/s) "
	      "= one press of the struggle's own command (a mash = many thrusts, a timed press = one); an urgent double "
	      "pulse at its start, a pulse per press (SOMVR_STRUGGLE, _MPS); an aim struggle (the cursor into the target) "
	      "steered by the hand that moves from where it was at its start (%.0f cm = full tilt, the head's right/up = the "
	      "cursor's: MenuAxisX/Y); the struggle's class, command and fields are logged either way",
	      g_stg.on ? "thrust" : "off", g_stg.mps, g_stg.aim_cm);
	logf_("CONFIG struggle build 27: finisher=%s (SOMVR_STRUGGLE_FINISHER%s): %s", g_stg.finisher ? "on" : "off",
	      getenv("SOMVR_STRUGGLE_FINISHER") ? "" : "; unset: on", g_stg.finisher ? "a beaten captain's Eliminate (Talion's "
	      "'PC_Bip_Finisher_*', the captain's own last chance) answered the same way, and the sword hand's thrust at him "
	      "(its line within SOMVR_STRUGGLE_FINISHER_DEG of him) = the kill (the struggle completed as the game's own)"
	      : "only Talion's own last chance "
	      "(build 26: the captain's finisher struggle left alone - lost, he gets up whole)");
}

static int stg_kind_of(uintptr_t s)
{
	uintptr_t vt = s ? (uintptr_t)vptr_of((void *)s) : 0;
	return !s ? STG_NONE : vt == STG_VT_PRESS ? STG_PRESS : vt == STG_VT_QT ? STG_QT : vt == STG_VT_HOLD ? STG_HOLD
	       : vt == STG_VT_DIR ? STG_DIR : vt == STG_VT_AIM ? STG_AIM : STG_OTHER;
}

/* the class's command (the one its OnCommandOn compares with), -1 = not known for it */
static int stg_cmd_of(int kind, const unsigned char *f)
{
	int32_t c = -1;
	if (kind == STG_PRESS)
		memcpy(&c, f + 0x2f0, 4);
	else if (kind == STG_QT)
		memcpy(&c, f + 0xd0, 4);
	else if (kind == STG_AIM)
		memcpy(&c, f + 0x58c, 4);
	return c > 0 && c < SC_NCMD ? c : -1;
}

/* the class's fields in a line (what decides it) */
static void stg_fields(int kind, const unsigned char *f, char *out, size_t cap)
{
	uint32_t a80, a84, a88, a8c;
	memcpy(&a80, f + 0x80, 4), memcpy(&a84, f + 0x84, 4), memcpy(&a88, f + 0x88, 4), memcpy(&a8c, f + 0x8c, 4);
	int n = snprintf(out, cap, "+80 %u +84 %u +88 %u result(+8c) %u +a0 %u +a2 %u +a5 %u", a80, a84, a88, a8c, f[0xa0], f[0xa2],
			 f[0xa5]);
	if (n < 0 || (size_t)n >= cap)
		return;
	if (kind == STG_PRESS)
		snprintf(out + n, cap - (size_t)n, "; presses %u of %u, phase %u (0 waits, 1 open, 2 over)", f[0x2f5], f[0x2f4], f[0x2f6]);
	else if (kind == STG_QT)
		snprintf(out + n, cap - (size_t)n, "; +d4 %u pressed(+d5) %u wrong(+d6) %u", f[0xd4], f[0xd5], f[0xd6]);
	else if (kind == STG_AIM) { /* its widget at +0xe8: the aim input (+0x474/+0x478 there), the cursor's speed (+0x45c) */
		float ax, ay, sp;
		memcpy(&ax, f + 0xe8 + 0x474, 4), memcpy(&ay, f + 0xe8 + 0x478, 4), memcpy(&sp, f + 0xe8 + 0x45c, 4);
		snprintf(out + n, cap - (size_t)n, "; +57a %u +57b %u done(+591) %u +592 %u; the widget's aim %+.2f %+.2f speed %.2f",
			 f[0x57a], f[0x57b], f[0x591], f[0x592], ax, ay, sp);
	}
}

/* in_frame: the hands' places (tracking space) -> the world's speed -> a thrust; their places against the head (world,
 * pw / hp / hq) -> an aim struggle's direction */
static void stg_hands(const sv_vec3 p[2], const sv_vec3 pw[2], sv_vec3 hp, sv_quat hq)
{
	double t = cb_now();
	if (g_stg.on && g_stg.obj && g_stg.death && g_stg.kind == STG_AIM) {
		sv_quat qn = sv_qnormalize(hq);
		sv_vec3 r = sv_qrot(qn, (sv_vec3){ 1, 0, 0 }), u = sv_qrot(qn, (sv_vec3){ 0, 1, 0 });
		float best = 0;
		int bh = -1;
		float bd[2] = { 0, 0 };
		for (int hnd = 0; hnd < 2; hnd++) {
			if (!g_inp.hands[hnd].valid || !isfinite(pw[hnd].x + pw[hnd].y + pw[hnd].z + hp.x + r.x + u.x))
				continue;
			sv_vec3 o = { pw[hnd].x - hp.x, pw[hnd].y - hp.y, pw[hnd].z - hp.z };
			float x = o.x * r.x + o.y * r.y + o.z * r.z, y = o.x * u.x + o.y * u.y + o.z * u.z;
			if (!g_stg.aim0_ok[hnd]) {
				g_stg.aim0[hnd][0] = x, g_stg.aim0[hnd][1] = y, g_stg.aim0_ok[hnd] = 1;
				continue;
			}
			float dx = x - g_stg.aim0[hnd][0], dy = y - g_stg.aim0[hnd][1], d = hypotf(dx, dy);
			if (d > best)
				best = d, bh = hnd, bd[0] = dx, bd[1] = dy;
		}
		g_stg.aim_have = bh >= 0 && best >= 3.0f;
		if (g_stg.aim_have) {
			float k = 1.0f / g_stg.aim_cm, ax = bd[0] * k, ay = bd[1] * k, l = hypotf(ax, ay);
			if (l > 1)
				ax /= l, ay /= l;
			g_stg.aim[0] = ax, g_stg.aim[1] = ay, g_stg.aim_hand = bh;
			if (t >= g_stg.aim_line && stg_log_ok()) {
				logf_("STRUGGLE t=%.3f +%.2f s: the aim %+.2f %+.2f (the %s hand %+.0f cm right, %+.0f cm up of where it was "
				      "at the start)", t, t - g_stg.t0, ax, ay, bh ? "right" : "left", bd[0], bd[1]);
				g_stg.aim_line = t + 0.25;
			}
		}
	}
	for (int hnd = 0; hnd < 2; hnd++) {
		if (!g_inp.hands[hnd].valid || !isfinite(p[hnd].x + p[hnd].y + p[hnd].z)) {
			g_stg.nh[hnd] = 0;
			continue;
		}
		int pos = g_stg.hpos[hnd];
		g_stg.h[hnd][pos].t = t, g_stg.h[hnd][pos].p = p[hnd];
		g_stg.hpos[hnd] = (pos + 1) % STG_HIST, g_stg.nh[hnd] += g_stg.nh[hnd] < STG_HIST;
		if (!g_stg.obj || g_stg.nh[hnd] < 2)
			continue;
		int now = pos, k = -1;
		for (int j = 1; j < g_stg.nh[hnd]; j++) { /* the newest sample at least 80 ms older (at most 250) */
			int ix = (now - j + STG_HIST) % STG_HIST;
			double age = t - g_stg.h[hnd][ix].t;
			if (age > 0.25)
				break;
			if (age >= 0.08) {
				k = ix;
				break;
			}
		}
		if (k < 0)
			continue;
		double age = t - g_stg.h[hnd][k].t;
		sv_vec3 a = somvr_rig_point(&g_rig, g_fr.base, g_stg.h[hnd][k].p), b = somvr_rig_point(&g_rig, g_fr.base, p[hnd]);
		float sp = (float)(sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y) + (b.z - a.z) * (b.z - a.z)) / age);
		if (sp < g_stg.mps * 100.0f / 3.0f)
			g_stg.armed[hnd] = 1;
		if (!g_stg.armed[hnd] || sp < g_stg.mps * 100.0f || sp > 1500.0f) /* (faster: a tracking jump) */
			continue;
		g_stg.armed[hnd] = 0, g_stg.n_thrusts++;
		/* build 27: the finisher's struggle - a thrust of the sword hand along a line at the captain held before Talion
		 * (B&S: the kill is the player's own stab, not a ring on the HUD) completes it as the game's own (+0xa5) */
		struct cb_orc oc;
		if (g_stg.on && g_stg.fin_now && g_stg.fin_c && !g_stg.fin_stab && hnd == SOMVR_HAND_R && g_stg.kind == STG_AIM &&
		    cb_orc_get(g_stg.fin_c, &oc)) {
			float v[3] = { b.x - a.x, b.y - a.y, b.z - a.z }, d[3] = { oc.pos[0] - a.x, oc.pos[1] + 100.0f - a.y,
				     oc.pos[2] - a.z };
			float lv = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]), ld = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
			float c = lv > 1 && ld > 1 ? (v[0] * d[0] + v[1] * d[1] + v[2] * d[2]) / (lv * ld) : -1;
			int at = c >= g_stg.fin_cos && ld < 400.0f;
			g_stg.fin_stab = at, g_stg.n_fin_stabs += (uint64_t)at;
			if (at)
				in_haptic_event(hnd, 1.0f, 0.12f, SOMVR_HAP_COMBAT);
			if (stg_log_ok())
				logf_("STRUGGLE t=%.3f the finisher: a thrust of the sword hand at %.1f m/s, %.0f deg off the line to the "
				      "captain %#lx (%.0f cm away): %s", t, sp * 0.01f, acosf(fmaxf(-1.0f, fminf(1.0f, c))) * 57.29578f,
				      (unsigned long)g_stg.fin_c, ld, at ? "the stab - the kill asked (completed as the game's own)"
				      : "not at him");
		}
		int counts = g_stg.on && g_stg.death && g_stg.cmd > 0;
		if (counts)
			g_stg.pending += g_stg.pending < 4, g_stg.n_thrusts_in++,
				in_haptic_event(hnd, 0.7f, 0.05f, SOMVR_HAP_COMBAT);
		if (stg_log_ok())
			logf_("STRUGGLE t=%.3f a thrust of the %s hand at %.1f m/s%s", t, hnd ? "right" : "left", sp * 0.01f,
			      counts ? ": one press of the struggle's command queued" : !g_stg.on ? " (SOMVR_STRUGGLE=off: nothing)"
			      : !g_stg.death ? " (not the last chance: nothing)" : " (no command known for this struggle: nothing)");
	}
}

/* the input tick: the struggle's object; our press of its command */
static void stg_tick(sc_tick_out *o)
{
	double t = cb_now();
	uintptr_t pc = g_inp.pl ? peek_ptr(g_inp.pl + P_COMBAT) : 0, s[2] = { 0, 0 };
	if (!(pc && peek(pc + IN_PC_STRUGGLE, s, sizeof s)))
		s[0] = s[1] = 0;
	int slot = s[0] ? 0 : s[1] ? 1 : -1;
	uintptr_t obj = slot >= 0 ? s[slot] : 0;
	unsigned char f[sizeof g_stg.f];
	int rd = obj && peek(obj, f, sizeof f);
	if (obj != g_stg.obj) {
		if (g_stg.obj && stg_log_ok()) {
			char fl[260], an[96];
			stg_fields(g_stg.kind, g_stg.f, fl, sizeof fl);
			logf_("STRUGGLE t=%.3f over after %.2f s: %s at %#lx; its last read: %s; presses of ours %llu so far; "
			      "Talion's attack now '%s'", t, t - g_stg.t0, g_stg_kind[g_stg.kind], (unsigned long)g_stg.obj, fl,
			      (unsigned long long)g_stg.n_presses, stg_talion_attack(an, sizeof an));
		}
		g_stg.obj = obj, g_stg.kind = stg_kind_of(obj), g_stg.slot = slot, g_stg.t0 = t, g_stg.have_f = 0;
		char dn[96];
		g_stg.death = obj && strstr(stg_talion_attack(dn, sizeof dn), "PreDeath") != NULL; /* the last chance only (the
								 * caragor's domination is a CStruggleAim too: ride2's grip) */
		/* build 27 (re-xr-pipeline.md 116): a beaten captain's Eliminate at his Last Words is Talion's finisher with the
		 * captain's own struggle ('PC_Bip_Finisher_PCLose' from its start; boot 20261030-141203: left alone it was lost
		 * and Ratlug got up whole, 5 -> 569) - the same CStruggleAim, the same answer */
		g_stg.fin_now = obj && !g_stg.death && g_stg.finisher && strstr(dn, "_Finisher_") != NULL;
		g_stg.fin_c = g_stg.fin_now ? g_vb.lw_asked_c : 0, g_stg.fin_stab = 0;
		g_stg.death |= g_stg.fin_now;
		g_stg.n_fin += g_stg.fin_now;
		g_stg.pending = g_stg.tap_on = g_stg.tap_off = 0, g_stg.cmd = -1, g_stg.next_line = t + 0.5;
		g_stg.aim_have = 0, g_stg.aim0_ok[0] = g_stg.aim0_ok[1] = 0, g_stg.aim_line = 0;
		if (obj && rd) {
			g_stg.n_struggles++, g_stg.n_kind[g_stg.kind]++;
			g_stg.cmd = stg_cmd_of(g_stg.kind, f);
			g_stg.n_nocmd += g_stg.cmd < 0;
			char fl[260], hx[3 * 64 + 1], hx2[3 * 48 + 1], an[96];
			stg_fields(g_stg.kind, f, fl, sizeof fl);
			for (int i = 0; i < 64; i++) /* +0x80..+0xbf: the base's timers and result */
				snprintf(hx + 3 * i, 4, "%02x ", f[0x80 + i]);
			int base2 = g_stg.kind == STG_PRESS ? 0x2e0 : g_stg.kind == STG_AIM ? 0x570 : 0xc0;
			for (int i = 0; i < 48; i++)
				snprintf(hx2 + 3 * i, 4, "%02x ", f[base2 + i]);
			if (stg_log_ok())
				logf_("STRUGGLE t=%.3f start: %s at %#lx (PlayerCombat+%#x: the %s's) - its command %d '%s'%s; %s; "
				      "+0x80: %s; +%#x: %s; Talion's attack '%s'", t, g_stg_kind[g_stg.kind], (unsigned long)obj,
				      0x8d8 + 8 * slot, slot ? "sync action" : "combat state", g_stg.cmd, g_stg.cmd > 0 ?
				      bo_cmd_name(g_stg.cmd) : "-", !g_stg.on ? " (SOMVR_STRUGGLE=off)" : !g_stg.death ?
				      " - not Talion's last chance (no PreDeath attack): left alone" : g_stg.fin_now ? (g_stg.kind ==
				      STG_AIM ? " - a captain's finisher (SOMVR_STRUGGLE_FINISHER): the hand steers its cursor, a thrust in "
				      "its ring presses" : " - a captain's finisher (SOMVR_STRUGGLE_FINISHER): a thrust = one press")
				      : g_stg.kind == STG_AIM ?
				      " - the last chance: the hand steers its cursor, a thrust in its ring presses (its command is set as it "
				      "opens)" : g_stg.cmd > 0 ? " - the last chance: a thrust = one press" : " - the last chance, no "
				      "answer of ours (its class's command not known)", fl, hx, base2, hx2,
				      stg_talion_attack(an, sizeof an));
			if (g_stg.on && g_stg.death && (g_stg.cmd > 0 || g_stg.kind == STG_AIM))
				for (int h = 0; h < 2; h++)
					in_haptic_double(h, 0.8f, 0.06f, 0.08f, SOMVR_HAP_COMBAT);
		}
	}
	if (obj && obj == g_stg.obj && g_stg.fin_stab == 1 && g_stg.kind == STG_AIM && peek_ptr(obj) == MT_VT_STRUGGLE_AIM) {
		static const uint8_t one = 1;
		int ok = arm_poke(obj + MT_STRUG_DONE, &one, 1);
		g_stg.fin_stab = 2, g_stg.n_fin_won += (uint64_t)ok;
		if (stg_log_ok())
			logf_("STRUGGLE t=%.3f +%.2f s: the finisher's stab %s", t, t - g_stg.t0, ok ? "completed it as the game's own "
			      "(ICombatStruggle+0xa5: its next update wins)" : "NOT written");
	}
	if (!obj || !rd)
		return;
	if (!g_stg.death) { /* another struggle (a domination, an interrogation): logged at its start, left alone */
		g_stg.pending = 0;
		return;
	}
	char fl[260];
	if (!g_stg.have_f || memcmp(f + 0x80, g_stg.f + 0x80, 0x20) || (g_stg.kind == STG_PRESS && memcmp(f + 0x2f4, g_stg.f + 0x2f4, 3)) ||
	    (g_stg.kind == STG_QT && memcmp(f + 0xd4, g_stg.f + 0xd4, 3)) || (g_stg.kind == STG_AIM && (memcmp(f + 0x591, g_stg.f + 0x591, 2) ||
	    memcmp(f + 0xe8 + 0x474, g_stg.f + 0xe8 + 0x474, 8)))) {
		int res_chg = g_stg.have_f && memcmp(f + 0x8c, g_stg.f + 0x8c, 4);
		if (res_chg)
			g_stg.n_done++;
		if ((res_chg || t >= g_stg.next_line) && stg_log_ok()) {
			stg_fields(g_stg.kind, f, fl, sizeof fl);
			logf_("STRUGGLE t=%.3f +%.2f s: %s%s", t, t - g_stg.t0, fl, res_chg ? " (the result changed)" : "");
			g_stg.next_line = t + 0.25;
		}
	}
	memcpy(g_stg.f, f, sizeof f), g_stg.have_f = 1;
	int c2 = stg_cmd_of(g_stg.kind, f); /* (an aim struggle's command is set when it opens) */
	if (c2 != g_stg.cmd && stg_log_ok())
		logf_("STRUGGLE t=%.3f +%.2f s: its command now %d '%s'", t, t - g_stg.t0, c2, c2 > 0 ? bo_cmd_name(c2) : "-");
	g_stg.cmd = c2;
	if (g_stg.on && g_stg.kind == STG_AIM && g_stg.aim_have && (g_stg.aim_via & 1)) { /* the hand as the pad's left */
		o->pad[SC_PAD_LX] = g_stg.aim[0], o->pad[SC_PAD_LY] = g_stg.aim[1];            /* stick (the game's "L") */
		o->pad_on |= (1u << SC_PAD_LX) | (1u << SC_PAD_LY);
		g_stg.n_aim_pad++;
	} else if (g_stg.on && g_stg.kind == STG_AIM) { /* the stick itself: as the widget shows it (run 063029: the struggle's
							 * stick turned by the head's yaw off the body: up became up-left) */
		float dz[2];
		sc_deadzone(o->move_raw, g_inp.cfg.dead, dz);
		o->pad[SC_PAD_LX] = dz[0], o->pad[SC_PAD_LY] = dz[1];
		if (dz[0] != 0 || dz[1] != 0)
			o->pad_on |= (1u << SC_PAD_LX) | (1u << SC_PAD_LY);
	}
	if (g_stg.on && g_stg.kind == STG_AIM && g_stg.pending > 0 && !g_stg.tap_on && !g_stg.tap_off) {
		/* CStruggleAim's OnCommandOn: its command with the cursor in the ring (+0x57a) = the success (+0x8c 1); out of
		 * it a press can end it as a miss - a thrust outside the ring is not pressed */
		g_stg.pending = 0;
		if (!f[0x57a]) {
			g_stg.n_aim_outside++;
			if (stg_log_ok())
				logf_("STRUGGLE t=%.3f +%.2f s: a thrust with the cursor outside the ring: not pressed", t, t - g_stg.t0);
			return;
		}
		g_stg.pending = 1; /* pressed below, as any struggle's */
	}
	if (!g_stg.on || g_stg.cmd <= 0)
		return;
	if (g_stg.tap_on > 0) { /* our press: held two ticks */
		g_stg.tap_on--;
		if (o->ncmd < SC_OUT_CMDS)
			o->cmd[o->ncmd++] = g_stg.cmd;
		if (sc_denied(g_stg.cmd, g_inp.ctx))
			g_inp.allow_cmd = g_stg.cmd;
		if (!g_stg.tap_on)
			g_stg.tap_off = 2;
	} else if (g_stg.tap_off > 0) {
		g_stg.tap_off--;
	} else if (g_stg.pending > 0) {
		g_stg.pending--, g_stg.tap_on = 2, g_stg.n_presses++;
		if (o->ncmd < SC_OUT_CMDS)
			o->cmd[o->ncmd++] = g_stg.cmd;
		if (sc_denied(g_stg.cmd, g_inp.ctx))
			g_inp.allow_cmd = g_stg.cmd;
		g_stg.tap_on--;
		if (stg_log_ok())
			logf_("STRUGGLE t=%.3f +%.2f s: our press %llu of its command %d '%s'", t, t - g_stg.t0,
			      (unsigned long long)g_stg.n_presses, g_stg.cmd, bo_cmd_name(g_stg.cmd));
	}
}

/* the input's wants (the struggle context): an aim struggle's direction from the hand = MenuAxisX (56) / MenuAxisY
 * (52), the axes CStruggleAim's update reads (re-xr-pipeline.md 108.6); none = the stick's own (the pad's bindings) */
static int stg_axis_wants(int cmd[2], float val[2])
{
	if (!g_stg.on || !g_stg.obj || !g_stg.death || g_stg.kind != STG_AIM || !g_stg.aim_have || !(g_stg.aim_via & 2))
		return 0;
	cmd[0] = 56, val[0] = g_stg.aim[0];
	cmd[1] = 52, val[1] = g_stg.aim[1];
	g_stg.n_aim_ticks++;
	return 2;
}

static void stg_end_log(void)
{
	logf_("STRUGGLE END build 27 (SOMVR_STRUGGLE=%s): struggles %llu (press-count %llu, quick-time %llu, aim %llu, hold "
	      "%llu, direction %llu, unknown %llu; no command known %llu); thrusts %llu (in a struggle with its command %llu), "
	      "our presses %llu; results changed %llu; input ticks aimed by a hand %llu (as the pad's stick %llu), thrusts outside the ring %llu", g_stg.on ? "thrust" : "off",
	      (unsigned long long)g_stg.n_struggles,
	      (unsigned long long)g_stg.n_kind[STG_PRESS], (unsigned long long)g_stg.n_kind[STG_QT],
	      (unsigned long long)g_stg.n_kind[STG_AIM], (unsigned long long)g_stg.n_kind[STG_HOLD],
	      (unsigned long long)g_stg.n_kind[STG_DIR], (unsigned long long)g_stg.n_kind[STG_OTHER],
	      (unsigned long long)g_stg.n_nocmd, (unsigned long long)g_stg.n_thrusts, (unsigned long long)g_stg.n_thrusts_in,
	      (unsigned long long)g_stg.n_presses, (unsigned long long)g_stg.n_done, (unsigned long long)g_stg.n_aim_ticks,
	      (unsigned long long)g_stg.n_aim_pad, (unsigned long long)g_stg.n_aim_outside);
	logf_("STRUGGLE END build 27: a captain's finisher answered %llu, stabs at him %llu, completed %llu "
	      "(SOMVR_STRUGGLE_FINISHER=%s)", (unsigned long long)g_stg.n_fin, (unsigned long long)g_stg.n_fin_stabs,
	      (unsigned long long)g_stg.n_fin_won, g_stg.finisher ? "on" : "off");
}
