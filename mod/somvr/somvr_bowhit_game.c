/* somvr_bowhit_game.c - the bow's round 2 (docs/re-bow.md 13): what our arrows hit, and with which attack.
 * Included by somvr.c after the prompts file (it uses bo_patch_call, rx_rec_name, peek, logf_, g_bow).
 *
 * SOMVR_BOW_HITLOG=1 (default off: nothing patched; needs SOMVR_BOW=1) - the one call of FUN_026c8e10 =
 *   Combat_ApplyProjectileHit(shooter Combat, ProjHit*, attack, ranged item, -, hit node record, mayIncHitCounter,
 *   isCrit) in CProjectile's impact FUN_027be5d0 (re-combat-items.md 1.1, 5). The attack passed there is the
 *   projectile's own (proj+0x190: ours = SOMVR_BOW_ATTACK's 'PC_Ranged_Ground_Normal_Shoot_Light') or the match of its
 *   RangedWeaponData's AttackOverrides ("Find Override RangedData = %s, CharType = %s, HitNode = %s": RequiredHitNode,
 *   RequiredCharacterType, Chance, ...; the headshot is one), the hit node record the zone the ray hit. Logged for the
 *   player's shots (the shooter = the Combat of our last shot), passed on unchanged: `BOW hit: ... node '<zone>' attack
 *   '<attack>' (the projectile's own | an override)`. ProjHit: +0 pos, +0x20 the hit object (HOBJECT), +0x28 u16 node.
 *
 * SOMVR_BOW_HEADSHOT_CM=R (default 0 = off) - the headshot for our arrows. Boots 043211 / 044033: every hit of ours on an
 *   orc came with the hit node record '' and the override 'PC_Ranged_Normal_Hit_Light_CharOrc' (10 damage), also with
 *   the head as the shot's aim object and node: the arrow's own hit-node choice never named the head. So: when our ray
 *   (the latched draw, g_bow.fire_o/fire_d) passed within R cm of the hit orc's Head node (GetNodeTransform) and the
 *   game picked the body override SOMVR_BOW_HEADSHOT_BODY ('PC_Ranged_Normal_Hit_Light_CharOrc'), the call gets the
 *   head override SOMVR_BOW_HEADSHOT_ATTACK ('PC_Ranged_Normal_Hit_Light_HeadOrc') instead: the stock arrow's own
 *   AttackOverrides entry for an orc's head, the game's damage. Other characters (goblins, beasts) keep the game's.
 *
 * SOMVR_BOW_HEADKILL=on (build 27b, som-b27-nemesis3; docs/re-xr-pipeline.md 124; default on) - an arrow in an orc's head
 *   kills him, as the flat game's one-arrow headshot. Our arrows' head hits came with the game's own
 *   'PC_Ranged_Normal_Hit_Light_HeadOrc' (20 of an orc's 95: re-bow.md 13.4; boot 20261031-025854: "Piercing Light"'s
 *   supporters hit in the head at 1-7 m, 95 -> 75, none killed; the mission asks five killed by the bow). The game's own
 *   'PC_Ranged_Normal_Hit_Heavy_HeadOrc' in its place killed at once (boot 20261031-032341: 95 -> 0 at 29 m). So when the
 *   game itself picks the light head hit (its impact at the Head node: the player's own aim with SOMVR_BOW_AIM=exact) on
 *   an orc who is not a Nemesis captain (kp_hobj_captain: the members in the world, at any distance; SOMVR_CAPTAIN off =
 *   no way to tell: kept), the call gets the heavy one; a captain keeps the game's (his Nemesis traits decide). With it on, SOMVR_BOW_HEADSHOT_BODY / _ATTACK default to the light / heavy head records and no ray test is
 *   made (SOMVR_BOW_HEADSHOT_CM is the off case's). Lines: `BOW headkill t=..`, `BOW HIT END ... headkills N (captains
 *   kept N)`. Off: build 27b (the light head hit, five arrows an orc). */

/* BOWHIT_SITES_BEGIN (check_sites.py counts this block on its own: the patched K_CALL in FUN_027be5d0) */
static const struct site_spec g_bh_sites_game[] = {
SITE("BOW_PJ_HIT", K_CALL, 0x27bf8ba, 0x27bf8a7, "5c 24 20 48 8b 54 24 70 4c 8b 8c 24 b0 00 00 00 48 89 de e8 51 95 f0 ff", 0x26c8e10, "41 57 49 89 f7 41 56 41 55 4d 89 cd 41 54 49"),
};
/* BOWHIT_SITES_END */
static const struct site_spec *g_bh_sites = g_bh_sites_game; /* selftest: a stand-in */
typedef uint64_t (*bh_hit_fn)(uintptr_t shooter, uintptr_t hit, uintptr_t attack, uintptr_t ranged, uintptr_t a5,
			      uintptr_t node, uint64_t inc, uint64_t crit);
#define BH_HIT_OBJ 0x20
#define BH_HIT_NODE 0x28

static struct {
	int on, log, head_state;    /* head_state: 0 not looked up, 1 found, -1 not in the database */
	_Atomic int head_crit;      /* SOMVR_BOW_HEADSHOT_CRIT=1: a head hit also passes isCrit (the word "bow headcrit 0|1") */
	float head_cm;
	char head_name[96], body_name[96];
	uintptr_t head_atk, body_atk;
	uint64_t calls, ours, overrides, heads, head_miss, headkills, hk_captains;
	int headkill;               /* build 27b: SOMVR_BOW_HEADKILL */
	uint32_t lines;
} g_bh;

/* the two records by name, once (the attack category loaded: the game thread, at a hit) */
static int bh_head_records(void)
{
	if (g_bh.head_state)
		return g_bh.head_state > 0;
	if (!g_cb.ok_hit)
		return 0;
	uintptr_t cat = peek_ptr(g_cbg.atkcat_pp);
	uint32_t cnt = 0;
	if (!cat || !peek(cat + CAT_COUNT, &cnt, sizeof cnt) || !cnt)
		return 0;
	g_bh.head_atk = CB_FN(CS_ATK_BYNAME, cb_byname_fn)(g_bh.head_name);
	g_bh.body_atk = CB_FN(CS_ATK_BYNAME, cb_byname_fn)(g_bh.body_name);
	g_bh.head_state = g_bh.head_atk && g_bh.body_atk ? 1 : -1;
	logf_("BOW headshot records: '%s' %#lx, '%s' %#lx: %s", g_bh.head_name, (unsigned long)g_bh.head_atk, g_bh.body_name,
	      (unsigned long)g_bh.body_atk, g_bh.head_state > 0 ? "found" : "NOT both in the database: no headshot");
	return g_bh.head_state > 0;
}

/* our ray's distance to a point (cm; -1 behind the nock) */
static float bh_ray_dist(sv_vec3 o, sv_vec3 d, const float p[3])
{
	sv_vec3 v = { p[0] - o.x, p[1] - o.y, p[2] - o.z };
	float t = v.x * d.x + v.y * d.y + v.z * d.z;
	if (t < 0)
		return -1;
	sv_vec3 w = { v.x - t * d.x, v.y - t * d.y, v.z - t * d.z };
	return sqrtf(w.x * w.x + w.y * w.y + w.z * w.z);
}


/* the call (the game thread, in a projectile's update or our shot's fire call): logged for the player's arrows */
__attribute__((used)) uint64_t somvr_w_bowhit(uintptr_t shooter, uintptr_t hit, uintptr_t attack, uintptr_t ranged,
					     uintptr_t a5, uintptr_t node, uint64_t inc, uint64_t crit)
{
	g_bh.calls++;
	int rq = atomic_exchange(&g_bh_head_crit_req, -1);
	if (rq >= 0)
		atomic_store(&g_bh.head_crit, rq), logf_("BOW headshot isCrit now %d (the word)", rq);
	if (g_bow.fire_sc && shooter == g_bow.fire_sc) {
		g_bh.ours++;
		float pos[3] = { 0, 0, 0 };
		uint16_t nd = 0xffff;
		uintptr_t obj = 0;
		peek(hit, pos, sizeof pos), peek(hit + BH_HIT_OBJ, &obj, 8), peek(hit + BH_HIT_NODE, &nd, 2);
		uintptr_t own = g_bow.atk; /* the attack our shot carries (SOMVR_BOW_ATTACK's, or the stock shot's) */
		int ovr = own && attack && attack != own;
		g_bh.overrides += (uint64_t)ovr;
		char an[96], nn[96], rn[96];
		float hp[3], hd = -1;
		if (g_bh.headkill && obj && attack && bh_head_records() && attack == g_bh.body_atk) {
			int cap = kp_hobj_captain(obj); /* every captain in the world, at any distance */
			g_bh.headkills += !cap, g_bh.hk_captains += cap;
			if (g_bh.lines++ < 200)
				logf_("BOW headkill t=%.3f: the game's own head hit '%s' on %s%s%s (SOMVR_BOW_HEADKILL)", cb_now(),
				      g_bh.body_name, cap ? "a Nemesis captain (or SOMVR_CAPTAIN off): kept" : "an orc: '", cap ? "" : g_bh.head_name,
				      cap ? "" : "' instead");
			if (!cap)
				attack = g_bh.head_atk;
		} else if (!g_bh.headkill && g_bh.head_cm > 0 && obj && attack && bh_head_records() && attack == g_bh.body_atk &&
		    bo_node_world(obj, g_som.head, hp) && (hd = bh_ray_dist(g_bow.fire_o, g_bow.fire_d, hp)) >= 0) {
			if (hd <= g_bh.head_cm) {
				g_bh.heads++;
				if (g_bh.lines++ < 200)
					logf_("BOW headshot t=%.3f: our ray passed %.1f cm from the orc's Head node (%.1f,%.1f,%.1f; "
					      "SOMVR_BOW_HEADSHOT_CM %.0f): the attack '%s' instead of '%s'%s", cb_now(), hd, hp[0], hp[1], hp[2],
					      g_bh.head_cm, g_bh.head_name, g_bh.body_name,
					      atomic_load(&g_bh.head_crit) ? ", isCrit 1 (SOMVR_BOW_HEADSHOT_CRIT)" : "");
				attack = g_bh.head_atk;
				if (atomic_load_explicit(&g_bh.head_crit, memory_order_relaxed))
					crit = (crit & ~(uint64_t)0xff) | 1;
			} else {
				g_bh.head_miss++;
				if (g_bh.lines++ < 200)
					logf_("BOW headshot t=%.3f: no - our ray passed %.1f cm from the orc's Head node (over %.0f)", cb_now(),
					      hd, g_bh.head_cm);
			}
		}
		if (g_bh.log && g_bh.lines++ < 200)
			logf_("BOW hit t=%.3f: our arrow at %.1f,%.1f,%.1f hit object %#lx node %d, the hit node record '%s', the attack "
			      "'%s' (%s), ranged '%s', crit %d, %.2f s after the shot", cb_now(), pos[0], pos[1], pos[2],
			      (unsigned long)obj, nd == 0xffff ? -1 : (int)nd, rx_rec_name(node, nn, sizeof nn),
			      rx_rec_name(attack, an, sizeof an), !attack ? "none" : ovr ? "an AttackOverrides match: not the "
			      "shot's own" : own ? "the shot's own" : "the shot's attack unknown", rx_rec_name(ranged, rn, sizeof rn),
			      (int)(crit & 0xff), g_bow.fire_t > 0 ? cb_now() - g_bow.fire_t : -1.0);
	}
	return ((bh_hit_fn)g_bh_sites[0].target)(shooter, hit, attack, ranged, a5, node, inc, crit);
}

static void bh_start(int *bad)
{
	memset(&g_bh, 0, sizeof g_bh);
	static const char *const onoff_w[] = { "0", "1" };
	g_bh.log = env_word("SOMVR_BOW_HITLOG", onoff_w, 2, 0);
	g_bh.head_cm = env_float("SOMVR_BOW_HEADSHOT_CM", 0, 0, 100, bad);
	atomic_store(&g_bh.head_crit, env_word("SOMVR_BOW_HEADSHOT_CRIT", onoff_w, 2, 0));
	g_bh.headkill = env_word("SOMVR_BOW_HEADKILL", (const char *const[]){ "off", "on" }, 2, 1);
	const char *hn = getenv("SOMVR_BOW_HEADSHOT_ATTACK"), *bn = getenv("SOMVR_BOW_HEADSHOT_BODY");
	snprintf(g_bh.head_name, sizeof g_bh.head_name, "%s", hn && *hn ? hn : g_bh.headkill ?
		 "PC_Ranged_Normal_Hit_Heavy_HeadOrc" : "PC_Ranged_Normal_Hit_Light_HeadOrc");
	snprintf(g_bh.body_name, sizeof g_bh.body_name, "%s", bn && *bn ? bn : g_bh.headkill ?
		 "PC_Ranged_Normal_Hit_Light_HeadOrc" : "PC_Ranged_Normal_Hit_Light_CharOrc");
	if (g_bow.on)
		logf_("CONFIG bow_headkill=%s (SOMVR_BOW_HEADKILL, build 27b%s): %s", g_bh.headkill ? "on" : "off",
		      getenv("SOMVR_BOW_HEADKILL") ? "" : "; unset: on", g_bh.headkill ? "an arrow in an orc's head kills him - the "
		      "game's own light head hit on an orc (not a Nemesis captain) becomes its heavy one, the flat game's one-arrow "
		      "headshot" : "the game's light head hit (20 of 95: five arrows an orc; build 27b)");
	if (!g_bh.log && !(g_bh.head_cm > 0) && !g_bh.headkill)
		return;
	if (!g_bow.on) {
		logf_("CONFIG bow_hitlog / bow_headshot need SOMVR_BOW=1: not patched");
		return;
	}
	g_bh.on = bo_patch_call(&g_bh_sites[0], (void *)somvr_w_bowhit);
	logf_("CONFIG bow_hitlog=%d (SOMVR_BOW_HITLOG: the projectile hit call logged for our arrows - the hit node record and "
	      "the attack, the projectile's own or an AttackOverrides match), bow_headshot_cm=%.0f (SOMVR_BOW_HEADSHOT_CM: %s): %s",
	      g_bh.log, g_bh.head_cm, g_bh.head_cm > 0 ? "our ray within it of an orc's Head node turns the game's body override "
	      "into the head one" : "off", g_bh.on ? "on" : "off (BOW_PJ_HIT did not match or not written)");
}

static void bh_end_log(void)
{
	if (g_bh.on)
		logf_("BOW HIT END calls %llu, ours %llu, with an override attack %llu; headshots %llu (rays too far from the head %llu); "
		      "headkills %llu (captains kept %llu; SOMVR_BOW_HEADKILL %s)", (unsigned long long)g_bh.calls,
		      (unsigned long long)g_bh.ours, (unsigned long long)g_bh.overrides, (unsigned long long)g_bh.heads,
		      (unsigned long long)g_bh.head_miss, (unsigned long long)g_bh.headkills, (unsigned long long)g_bh.hk_captains,
		      g_bh.headkill ? "on" : "off");
}
