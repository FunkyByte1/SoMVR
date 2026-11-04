/* somvr_settings34_game.c - build 34 (som-b34-settings, docs/re-xr-pipeline.md 150): the VR menu's v8 rows on the game
 * side. Builds 31-33 added switches only the environment set (scripts/headset-build26.env); the ones a player may want
 * to tune are rows of the Settings tab's groups now (somvr_vrmenu.h: somvr_settings 96..109, SOMVR_SK_XR_WATCH_LOOK ..
 * SOMVR_SK_CLIMB_SWORD). Three parts, all on the game thread (vm_start, vm_tick's vm_apply_key):
 *   s34_vm_capture  at vm_start, before the file: the switches' own values (Normal = as set: the shove's speeds, the
 *                   force's joules, the low-health share, the heartbeat's) - the rows step around them
 *   s34_vm_settings the rows' start values from the features' configs (each switch, as its own start parsed it)
 *   s34_vm_av8      which of the rows' features run in this session (somvr_settings.av8: the 64 avail bits are all
 *                   taken); a row without its bit is dimmed
 *   s34_vm_set      a row's value into its feature, live where the feature reads its config when it decides (all of
 *                   them: the knife's hold is taken at its next draw); returns the apply line's note
 * The layer's two (the watch: SOMVR_XR_WATCH_LOOK, its size) are read by the layer from the menu's settings each frame
 * (somvr_menus_layer.c, somvr_vrhands_layer.c); here they only carry their start values.
 * Included by somvr.c after the features' files (kills, swordphys, hands31, fight, parry, verbs, climb, slots). */

static struct {
	int took;
	float shove_mps, hard_mps;   /* SOMVR_SHOVE_MPS, _HARD_MPS as set (the Shove push row's Normal) */
	float j_min, j_heavy, j_full, j_stab; /* SOMVR_COMBAT_FORCE_J's (and the stab's) as set (Force needed: Normal) */
	float low, beat;             /* SOMVR_HURT_LOW / _BEAT as set (0: the code's 0.30 / 0.20 when a row turns them on) */
} g_s34;

static const float S34_FORCE_K[3] = { 0.6f, 1.0f, 1.5f }; /* Force needed: Low, Normal, High (x the joules) */
static const float S34_SHOVE_D[3] = { -0.5f, 0.0f, 0.5f }; /* Shove push: Soft, Normal, Hard (+ m/s) */

static void s34_vm_capture(void)
{
	g_s34.took = 1;
	g_s34.shove_mps = g_py.shove_mps, g_s34.hard_mps = g_py.hard_mps;
	g_s34.j_min = g_cb.cfg.blow_min_j, g_s34.j_heavy = g_cb.cfg.blow_heavy_j, g_s34.j_full = g_cb.cfg.blow_full_j;
	g_s34.j_stab = g_cb.cfg.blow_stab_j;
	g_s34.low = g_hu.low > 0 ? g_hu.low : 0.30f;         /* build 31's shipped share */
	g_s34.beat = g_hu.beat_below > 0 ? g_hu.beat_below : 0.20f;
}

/* SOMVR_XR_MINIMAP_QUAD's SIZE (m; its first number), NAN = unset or not a number */
static float s34_watch_size_env(void)
{
	const char *e = getenv("SOMVR_XR_MINIMAP_QUAD");
	char b[32];
	double d;
	if (!e || !*e)
		return NAN;
	size_t n = strcspn(e, ",");
	if (n >= sizeof b)
		return NAN;
	memcpy(b, e, n), b[n] = 0;
	return somvr_set_num(b, &d) && d > 0 ? (float)d : NAN;
}

static void s34_vm_settings(somvr_settings *s)
{
	s34_vm_capture();
	const char *look = getenv("SOMVR_XR_WATCH_LOOK");
	s->watch_raise = look && !strcmp(look, "raise");
	float ws = s34_watch_size_env();
	s->watch_big = !(ws < 0.08f); /* unset: the layer's 10 cm (build 10); build 33's 6 cm = small */
	s->low_health = g_hu.low > 0 ? (g_hu.ring ? 2 : 1) : 0;
	s->heartbeat = g_hu.beat_below > 0;
	s->force = 1;
	s->ghost = g_swp.ghost != 0;
	s->kills = g_kl.on != 0, s->exec_blow = g_kl.exec_blow != 0;
	s->shove = 1;
	s->hold_s = (uint8_t)lrintf(fminf(60.0f, fmaxf(0.0f, g_vb.cfg.grab_max_s)));
	s->knife_hold = (uint8_t)((g_sl.cfg.hold_dagger >= 0 ? g_sl.cfg.hold_dagger : g_sl.cfg.hold) != 0);
	s->herbs = g_hrb.on != 0;
	s->fling_down = g_cl.cfg.fling_down != 0, s->climb_sword = g_cl.sword != 0;
}

static uint16_t s34_vm_av8(void)
{
	const char *mm = getenv("SOMVR_XR_MINIMAP");
	uint16_t a = 0;
	if (mm && !strcmp(mm, "wrist"))
		a |= SOMVR_VMA8_WATCH;
	if (g_hu.on)
		a |= SOMVR_VMA8_HURT;
	if (g_cb.cfg.force && g_arm.on && g_cb.mode)
		a |= SOMVR_VMA8_FORCE;
	if (g_swp.on)
		a |= SOMVR_VMA8_GHOST;
	if (g_kl.ok)
		a |= SOMVR_VMA8_KILLS;
	if (g_py.shove)
		a |= SOMVR_VMA8_SHOVE;
	if (g_vb.mode == VB_ON)
		a |= SOMVR_VMA8_HOLD;
	if (g_sl.on && g_sl.ok_dagger)
		a |= SOMVR_VMA8_KNIFE;
	if (g_us.on)
		a |= SOMVR_VMA8_HERBS;
	if (g_cl.on)
		a |= SOMVR_VMA8_CLIMB;
	if (cl_vm_avail() & SOMVR_VMA_FLING)
		a |= SOMVR_VMA8_FLINGDN;
	return a;
}

/* the Climb fling row changed: the fling down follows it (it is a fling's) */
static void s34_fling_follow(const somvr_settings *s)
{
	g_cl.cfg.fling_down = s->fling_down && g_cl.cfg.fling;
}

static const char *s34_vm_set(int k, const somvr_settings *s)
{
	static char note[160];
	int lv;
	note[0] = 0;
	switch (k) {
	case SOMVR_SK_XR_WATCH_LOOK:
	case SOMVR_SK_WATCH_SIZE:
		return " (the layer's watch: its next frame)";
	case SOMVR_SK_LOW_HEALTH: /* th_frame's next health read: the ring / the edge by g_hu.low, hu_low's ring by .ring */
		lv = s->low_health < 3 ? s->low_health : 0;
		g_hu.low = lv ? g_s34.low : 0, g_hu.ring = lv == 2;
		snprintf(note, sizeof note, lv ? " (under %.0f %% of the most health: %s)" : " (no low-health cue)",
			 g_s34.low * 100.0f, lv == 2 ? "the red ring" : "the faint red edge");
		return note;
	case SOMVR_SK_HEARTBEAT:
		g_hu.beat_below = s->heartbeat ? g_s34.beat : 0;
		snprintf(note, sizeof note, s->heartbeat ? " (the hands' heartbeat under %.0f %%, with the ring)" : " (none)",
			 g_s34.beat * 100.0f);
		return note;
	case SOMVR_SK_FORCE_NEEDED: {
		float kf = S34_FORCE_K[s->force < 3 ? s->force : 1];
		g_cb.cfg.blow_min_j = g_s34.j_min * kf, g_cb.cfg.blow_heavy_j = g_s34.j_heavy * kf;
		g_cb.cfg.blow_full_j = g_s34.j_full * kf, g_cb.cfg.blow_stab_j = g_s34.j_stab * kf;
		snprintf(note, sizeof note, " (a hit from %.0f J, a cut from %.0f J, full at %.0f J, a stab %.0f J%s)",
			 (double)g_cb.cfg.blow_min_j, (double)g_cb.cfg.blow_heavy_j, (double)g_cb.cfg.blow_full_j,
			 (double)g_cb.cfg.blow_stab_j, g_cb.cfg.force ? "" : "; SOMVR_COMBAT_FORCE off: not used");
		return note;
	}
	case SOMVR_SK_SWORD_PHYS_GHOST:
		g_swp.ghost = s->ghost != 0;
		if (!g_swp.ghost)
			g_swp.ghost_a = 0;
		return g_swp.on ? (s->ghost ? " (the faint hand while the blade lags)" : " (no faint hand)")
				: " (no SOMVR_SWORD_PHYS: not used)";
	case SOMVR_SK_KILLS: /* kl_frame / kl_take read it at the next kill (as the test word's switch) */
		g_kl.on = s->kills != 0;
		return !g_kl.ok ? " (KL_START did not match: not used)"
				: s->kills ? " (the next kill: the orc's half of the game's own)" : " (the next kill: the plain death)";
	case SOMVR_SK_KILLS_EXEC:
		g_kl.exec_blow = s->exec_blow != 0;
		return s->exec_blow ? " (the next heavy blow while charged)" : " (the trigger while charged)";
	case SOMVR_SK_SHOVE_PUSH: {
		float d = S34_SHOVE_D[s->shove < 3 ? s->shove : 1];
		g_py.shove_mps = fmaxf(0.5f, g_s34.shove_mps + d), g_py.hard_mps = fmaxf(1.0f, g_s34.hard_mps + d);
		snprintf(note, sizeof note, " (a shove from %.1f m/s, at the grab's orc %.1f m/s%s)", (double)g_py.shove_mps,
			 (double)g_py.hard_mps, g_py.shove ? "" : "; SOMVR_SHOVE off: not used");
		return note;
	}
	case SOMVR_SK_VERBS_GRAB_MAX_S:
		g_vb.cfg.grab_max_s = (float)s->hold_s;
		return s->hold_s ? " (the next tick of a hold)" : " (the game's own end)";
	case SOMVR_SK_SLOTS_DAGGER_HOLD:
		g_sl.cfg.hold_dagger = s->knife_hold != 0;
		return s->knife_hold ? " (at the next draw: held while gripped, let go = on the belt)"
				     : " (at the next draw: it stays in the hand, put back at the hip)";
	case SOMVR_SK_HERBS:
		g_hrb.on = s->herbs && g_us.on;
		if (!g_hrb.on)
			g_hrb.hold_h = -1, g_hrb.cue_level = 0; /* a herb in the fist is dropped (B still picks) */
		return !g_us.on ? " (no SOMVR_USE: not used)" : s->herbs ? " (by the hand)" : " (by B only)";
	case SOMVR_SK_CLIMB_FLING_DOWN:
		s34_fling_follow(s);
		return g_cl.cfg.fling ? (s->fling_down ? " (a fast let-go with the hand rising flings down)" : " (no fling down)")
				      : " (with the Climb fling only: not now)";
	case SOMVR_SK_CLIMB_SWORD:
		g_cl.sword = s->climb_sword != 0;
		return s->climb_sword ? " (the free hand may draw on the wall)" : " (no sword on the wall)";
	default:
		return "";
	}
}
