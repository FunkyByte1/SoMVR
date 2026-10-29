/* somvr_captain_game.c - SOMVR_CAPTAIN: a Nemesis captain in VR, the game side (somvr_captain.h; docs/re-xr-pipeline.md
 * 88; handoff/HANDOFF-captain.md). Not a translation unit of its own: somvr.c includes it after the practice area's and
 * the finishers' files (it uses the practice's spawn, its orc and min health, the body moments' detector, combat's orcs).
 *
 * 1. The practice's Captain (the VR menu's Practice page, the trigger word `practice captain`): the save's next captain -
 *    the faction's members (the faction manager CGameServices+0x6c58, the current faction record +0xe8, KP_FACTION, its
 *    members +0x38..+0x40), the first after the last one brought that is in the hierarchy, no warchief (his bodyguards),
 *    alive, not in the world, with a template (somvr_cp_next) - spawned SOMVR_PRACTICE_CAPTAIN_CM ahead facing Talion as
 *    P9's `spawncaptain` did (re-combat-spawn.md 1.4: the debug spawn with the member, KP_FM_BIND, KP_FM_ACTIVATE, his
 *    level). He is the practice's orc (Remove, a new spawn and the trips remove him: the member is back out of the world).
 *    He is the save's own: his death is for good, a Talion he kills promotes him - SOMVR_PRACTICE_CAPTAIN_STAKES=on
 *    (default) lifts the practice's min health while he is out (the game's own death, the Nemesis screens in the window).
 * 2. The introduction (SOMVR_CAPTAIN_INTRO=world, default): a presentation (G16) that starts while Talion is alive and a
 *    captain (an NPC bound to a faction member: npc+0x728 = the member's seed, the member's +0x40 = the NPC) is within
 *    SOMVR_CAPTAIN_INTRO_GEOM's range of the head and angle of the view plays in the world: the body moments' window is not
 *    asked for it (the presentation camera ignored, the view the player's own); else (=window, or not near, not in view)
 *    the window as before. Decided once per presentation, kept while it runs.
 * 3. His traits' rules (SOMVR_CAPTAIN_RULES=on, default): our finishers ask them - the execution (combat finishers) and the
 *    stealth kill: Immune = no finisher (the ordinary hit), Vulnerable = the kill (forceKill), Resistant / DamagedBy / none
 *    = the record's own damage through the game's rules (no forceKill); =stagger's health write and SOMVR_COMBAT_SLIVER
 *    leave captains alone (his hits are the game's own damage). The bow's arrows are the stock ones (their rules the game's).
 * 4. His fate: CAPTAIN lines - the spawn (member, level, role, tree, traits), his health as it changes, the presentations
 *    while he is out (world or window), his death (the member's IsDead / KilledByPlayer), his leaving the world alive
 *    (an escape or a removal), Talion's death while he is out (the promotion follows).
 * Test words (`captain status|pick N|hp V|charge|stakes on|off`; all but status only with SOMVR_HANDS_TEST). Lines: `CAPTAIN ...`. */

/* CAPTAIN_SITES_BEGIN (read only, never patched). KP_FACTION FUN_023d8930(fmgr, faction record): the faction of a record
 * (its 0x228-byte entry + 8); KP_TARGET FUN_023d3f80 (a proof: its bytes read fmgr+0xe8, the current faction record);
 * KP_MEMBERS FUN_022aeed0 (a proof: a faction's members +0x38..+0x40, 8 bytes each); KP_FM_TEMPLATE FUN_022d71b0(fm): the
 * member's AI template (its bytes: +0x58 the faction record, +0x60 the combat tree); KP_FM_BIND FUN_02975eb0(npc, fm, 1):
 * the NPC bound to the member (FUN_022cf590: fm+0x40 = npc, +0x3a4 bit 1; npc+0x728 = fm+0x48); KP_FM_ACTIVATE
 * FUN_022cdad0(fm, 1) (P9's step for a member with +0xa0); KP_GS_FMGR CGameServices (vtable 0x34e2a90) slot 15 (+0x78):
 * `mov rax, [rdi+0x6c58]; ret` = the faction manager. Order = KPS_* */
static const struct site_spec g_kp_sites_game[] = {
SITE("KP_FACTION", K_ENTRY, 0x23d8930, 0x23d8930, "48 85 f6 74 58 48 8b 46 18 48 8b 50 38 f6 46 01", 0x23d8930, "48 85 f6 74 58 48 8b 46 18 48 8b 50 38 f6 46"),
SITE("KP_TARGET", K_ENTRY, 0x23d3f80, 0x23d3f80, "48 83 ec 08 48 8b b7 e8 00 00 00 e8 a0 49 00 00", 0x23d3f80, "48 83 ec 08 48 8b b7 e8 00 00 00 e8 a0 49 00"),
SITE("KP_MEMBERS", K_ENTRY, 0x22aeed0, 0x22aeed0, "48 8b 77 38 48 8b 57 40 31 c0 8b 8f b0 01 00 00", 0x22aeed0, "48 8b 77 38 48 8b 57 40 31 c0 8b 8f b0 01 00"),
SITE("KP_FM_TEMPLATE", K_ENTRY, 0x22d71b0, 0x22d71b0, "48 8b 77 60 48 8b 7f 58 e9 e3 39 03 00 90 66 90", 0x22d71b0, "48 8b 77 60 48 8b 7f 58 e9 e3 39 03 00 90 66"),
SITE("KP_FM_BIND", K_ENTRY, 0x2975eb0, 0x2975eb0, "41 56 41 55 49 89 f5 41 54 49 89 fc 55 53 89 d3", 0x2975eb0, "41 56 41 55 49 89 f5 41 54 49 89 fc 55 53 89"),
SITE("KP_FM_ACTIVATE", K_ENTRY, 0x22cdad0, 0x22cdad0, "41 55 41 54 41 89 f4 55 48 89 fd 53 48 83 ec 18", 0x22cdad0, "41 55 41 54 41 89 f4 55 48 89 fd 53 48 83 ec"),
SITE("KP_GS_FMGR", K_VSLOT, 0x34e2b08, 0x34e2b08, "e0 bd 9e 00 00 00 00 00", 0x9ebde0, "48 8b 87 58 6c 00 00 c3 0f 1f 84 00 00 00 00"),
};
/* CAPTAIN_SITES_END */
enum { KPS_FACTION, KPS_TARGET, KPS_MEMBERS, KPS_TEMPLATE, KPS_BIND, KPS_ACTIVATE, KPS_FMGR, KPS_N };
_Static_assert(sizeof g_kp_sites_game / sizeof g_kp_sites_game[0] == KPS_N, "captain sites");
static const struct site_spec *g_kp_sites = g_kp_sites_game; /* selftest: stand-ins */
#define KP_FN(i, T) ((T)g_kp_sites[i].target)
typedef uintptr_t (*kp_faction_fn)(uintptr_t fmgr, uintptr_t rec);
typedef uintptr_t (*kp_template_fn)(uintptr_t fm);
typedef void (*kp_bind_fn)(uintptr_t npc, uintptr_t fm, int x);
typedef void (*kp_activate_fn)(uintptr_t fm, int x);

#define KP_GS_FMGR 0x6c58   /* CGameServices: the faction manager (KP_GS_FMGR's bytes) */
#define KP_FMGR_FACREC 0xe8 /* the faction manager: the current faction record (KP_TARGET's bytes) */
#define KP_FAC_MEMBERS 0x38 /* a faction: its members, begin and end (+8) (KP_MEMBERS' bytes) */
#define KP_FM_NPC 0x40      /* member: the character bound to it (FUN_022cf590) */
#define KP_FM_SEED 0x48     /* ... RandomSeed (copied to the NPC's +0x728 by the bind) */
#define KP_FM_NAMEHASH 0x50 /* ... NameStringIDHash */
#define KP_FM_TREE 0x60     /* ... CombatTree record */
#define KP_FM_TRAITS 0x70   /* ... its trait records, begin and end (+8) (the dump's "Trait") */
#define KP_FM_POS 0x88      /* ... its hierarchy position (0: none, the dump's row 255) */
#define KP_FM_SPAWNCOND 0xa0 /* ... SpawnCondition (P9's FM_ACTIVE) */
#define KP_FM_ROLE 0x2b8    /* ... Role record */
#define KP_FM_LEVEL 0x340   /* ... XPLevel */
#define KP_FM_COL 0x3a0     /* ... ColumnNumber (byte) */
#define KP_FM_FLAGS 0x3a4   /* ... the flags (SOMVR_CP_FL_*) */
/* build 24: his death's record (FUN_022dbd20, the member's death, called by the death handler FUN_026f61f0(his Combat,
 * the attack, the killer's Combat) - the game's one place that credits a kill: FUN_022dbc20 fills +0xb0.., FUN_022d09a0
 * the death type, then IsDead (bit 2) and the KilledByPlayer / KilledByAI / unknown sim event to the faction manager).
 * The flags' bit 3 "KilledByPlayer" is not written at run time (only Faction.Load's reader 0x22e14de sets it; only
 * Faction.Dump's 0x22cdfca reads it): the stock kill leaves it 0 too (re-xr-pipeline.md 99.1) */
#define KP_FM_LD_CHARTYPE 0xc0 /* ... LastDeathCharacterType: the killer's character type record */
#define KP_FM_LD_ATTACK 0xc8   /* ... LastDeathAttack: the attack record that killed him */
#define KP_FM_LD_PLAYER 0xd0   /* ... LastDeathPlayerKiller (byte): the killer's Combat had no NPC (+0x80) but the player (+0x88) */
#define KP_FM_LD_TYPE 0xd8     /* ... LastDeathType (the death's kind from the attack and the killer) */
#define KP_FM_PLAYERKILLS 0x348 /* ... PlayerKills (int) */
/* build 24: the game's own stat lines (Combat_LogPrintf "[COMBAT|STATS]: ... Damage Calc ...") - its console bools
 * (Init_CombatVariables: a CConsoleProgramVariable_Bool, +8 its name, +0x38 its value), a test aid (`captain gamelog`) */
static const struct { uintptr_t var; const char *name; } g_kp_dbg[] = {
	{ 0x4c44ca0, "Combat.Debug.Stats" }, { 0x4c44d60, "Combat.Debug.XP" }, { 0x4c44860, "Combat.Debug.HitCounter" },
	{ 0x4c44ee0, "Combat.Debug.ApplyDamage" },
};
#define KP_DBG_VALUE 0x38
#define KP_NPC_SEED 0x728   /* NPC: its member's seed (0 = no member) */
#define KP_MAX 64
#define KP_TRAITS_MAX 48

static struct {
	int want, on;          /* SOMVR_CAPTAIN set (kp_start); the sites checked out */
	int ok_mh;             /* PR_SETMINHEALTH checked (the finishers' clamp; the practice may be off) */
	int intro_world;       /* SOMVR_CAPTAIN_INTRO=world */
	int rules;             /* SOMVR_CAPTAIN_RULES=on */
	int stakes;            /* SOMVR_PRACTICE_CAPTAIN_STAKES=on */
	float spawn_cm;        /* SOMVR_PRACTICE_CAPTAIN_CM */
	float fin_pct;         /* SOMVR_CAPTAIN_FINISHER_PCT: our finisher takes this much of a non-vulnerable captain's max */
	somvr_cp_intro_cfg icfg;
	int last;              /* the member index brought last (-1: none yet) */
	_Atomic int req_pick;  /* `captain pick N`: the next Captain brings member N (-1: none) */
	_Atomic int req_status;
	_Atomic int req_hp;    /* `captain hp V` x 10 (-1: none) */
	_Atomic int req_charge; /* `captain charge`: Talion's hit counter charged (a test aid: the execution at once) */
	/* our captain (the practice's orc while it is him) */
	uintptr_t fm, npc, c, h;
	double t0, next_watch, health_t;
	float hp_last, hp_max0;
	uint32_t flags0;
	int dead_seen, gone_seen, talion_dead, pres_n_world, pres_n_window;
	char who[160];
	somvr_cp_traits tr;
	/* the presentation's decision */
	int pres_on, pres_world;
	double pres_t0, pres_last;
	/* the rules: the last captain told per kind (one line each) */
	uintptr_t told_c[SOMVR_CPF_N + 2];
	/* counts */
	uint64_t spawns, spawn_fail, intros_world, intros_window, rule_kills, rule_damage, rule_none, rule_skips,
		lines, member_reads, floors;
	/* build 24 (som-b24-captain): our cuts' damage on captains, the test aids, his flight */
	int dmg;               /* SOMVR_CAPTAIN_DAMAGE=cut (1) | off (0) */
	float cut_x;           /* SOMVR_CAPTAIN_CUT_X: a full-speed committed cut's factor on the game's damage on a captain */
	_Atomic int req_gamelog, req_tiers, req_away;
	int stealth_turn;      /* SOMVR_CAPTAIN_STEALTH_IMMUNE=turn (1) | hit (0) */
	uint64_t turned;
	int tiers_step;
	double tiers_next, place_next;
	float place_d;
	uint64_t dmg_hits, dmg_scaled, dmg_unseen, tiers_dealt, flee_lines;
	double dmg_game, dmg_ours;
} g_kp = { .last = -1 };

/* build 24: our hit on a captain in flight (cb_hit -> Combat_DealHit -> ... -> FR_DMG_ATK's DamageStruct_DoDamage) */
static struct {
	uintptr_t hobj, c;
	float x, k, a, b, hp0;
	int tid, on, seen, tier;
} g_kph;

static int kp_log_ok(void) { return g_kp.lines++ < 900; } /* build 24: 900 (was 400) */

/* the faction manager's current faction (0 = none readable) */
static uintptr_t kp_faction(void)
{
	uintptr_t gs = g_kp.on ? peek_ptr(g_prg.gsvc_pp) : 0, vt = gs ? vptr_of((void *)gs) : 0;
	if (!vt || vt + 0x78 != g_kp_sites[KPS_FMGR].addr || peek_ptr(vt + 0x78) != g_kp_sites[KPS_FMGR].target)
		return 0;
	uintptr_t fmgr = peek_ptr(gs + KP_GS_FMGR), rec = fmgr ? peek_ptr(fmgr + KP_FMGR_FACREC) : 0;
	return rec ? KP_FN(KPS_FACTION, kp_faction_fn)(fmgr, rec) : 0;
}

/* the faction's members (at most cap): their pointers; the count */
static int kp_members(uintptr_t *out, int cap)
{
	uintptr_t f = kp_faction(), b = f ? peek_ptr(f + KP_FAC_MEMBERS) : 0, e = f ? peek_ptr(f + KP_FAC_MEMBERS + 8) : 0;
	size_t n = e > b && (e - b) % 8 == 0 ? (e - b) / 8 : 0;
	if (n > (size_t)cap)
		n = (size_t)cap;
	if (!n || !peek(b, out, n * 8))
		return 0;
	g_kp.member_reads++;
	return (int)n;
}

/* a member's traits (and their names, comma separated, into names) */
static void kp_traits(uintptr_t fm, somvr_cp_traits *t, char *names, size_t cap)
{
	somvr_cp_traits_init(t);
	if (names && cap)
		names[0] = 0;
	uintptr_t b = peek_ptr(fm + KP_FM_TRAITS), e = peek_ptr(fm + KP_FM_TRAITS + 8), r[KP_TRAITS_MAX];
	size_t n = e > b && (e - b) % 8 == 0 ? (e - b) / 8 : 0, o = 0;
	if (n > KP_TRAITS_MAX)
		n = KP_TRAITS_MAX;
	if (!n || !peek(b, r, n * 8))
		return;
	for (size_t i = 0; i < n; i++) {
		char nm[64];
		if (!r[i] || !fb_rec_name(r[i], nm, sizeof nm) || !nm[0])
			continue;
		somvr_cp_traits_add(t, nm);
		if (names && o + 2 < cap && strncmp(nm, "Marker_", 7) && strncmp(nm, "Mannerism_", 10) &&
		    strncmp(nm, "AllNamed_", 9) && strncmp(nm, "Block_", 6)) /* the ones that matter in a fight */
			o += (size_t)snprintf(names + o, cap - o, "%s%s", o ? "," : "", nm), o = o < cap ? o : cap - 1;
	}
}

/* "Role_Hook level 10 AI_Infantry (row-position set, column 3)" */
static void kp_who(uintptr_t fm, char *out, size_t cap)
{
	char role[64] = "?", tree[64] = "?";
	uint32_t lvl = 0, hash = 0;
	uint8_t col = 0xff;
	fb_rec_name(peek_ptr(fm + KP_FM_ROLE), role, sizeof role);
	fb_rec_name(peek_ptr(fm + KP_FM_TREE), tree, sizeof tree);
	peek(fm + KP_FM_LEVEL, &lvl, sizeof lvl), peek(fm + KP_FM_NAMEHASH, &hash, sizeof hash), peek(fm + KP_FM_COL, &col, 1);
	snprintf(out, cap, "member %#lx (name hash %u) %s level %u %s column %u", (unsigned long)fm, hash, role, lvl, tree, col);
}

/* the member bound to an NPC (0 = none: no seed, or no member has it) */
static uintptr_t kp_member_of(uintptr_t npc)
{
	uint64_t seed = 0;
	if (!g_kp.on || !npc || !peek(npc + KP_NPC_SEED, &seed, sizeof seed) || !seed)
		return 0;
	if (npc == g_kp.npc && g_kp.fm)
		return g_kp.fm;
	uintptr_t m[KP_MAX];
	int n = kp_members(m, KP_MAX);
	for (int i = 0; i < n; i++)
		if (m[i] && peek_ptr(m[i] + KP_FM_NPC) == npc)
			return m[i];
	return 0;
}

/* build 24: his last death as the member keeps it ("by the player 1, the attack '...', the killer's type '...', the
 * death type '...', PlayerKills N") */
static void kp_death_desc(uintptr_t fm, char *out, size_t cap)
{
	char at[96] = "-", ty[64] = "-", dt[64] = "-";
	uint8_t by = 0;
	int32_t pk = 0;
	uintptr_t ra = peek_ptr(fm + KP_FM_LD_ATTACK), rc = peek_ptr(fm + KP_FM_LD_CHARTYPE), rd = peek_ptr(fm + KP_FM_LD_TYPE);
	if (ra)
		fb_rec_name(ra, at, sizeof at);
	if (rc)
		fb_rec_name(rc, ty, sizeof ty);
	if (rd)
		fb_rec_name(rd, dt, sizeof dt);
	peek(fm + KP_FM_LD_PLAYER, &by, 1), peek(fm + KP_FM_PLAYERKILLS, &pk, sizeof pk);
	snprintf(out, cap, "the last death: by the player %u, the attack '%s', the killer's type '%s', the death type '%s'; "
		 "PlayerKills %d", by, at, ty, dt, pk);
}

/* ---- 3. the rules ------------------------------------------------------------------------------------------------- */
/* the captain behind a Combat (0 = not one) */
static uintptr_t kp_of_combat(uintptr_t c)
{
	uintptr_t npc = c ? peek_ptr(c + C_OWNER) : 0;
	return npc && vptr_of((void *)npc) == g_cbg.vt_npc ? kp_member_of(npc) : 0;
}

/* our finisher `what` of kind (SOMVR_CPF_COMBAT, _STEALTH) on the orc of Combat c: 1 = forceKill (not a captain, or a
 * vulnerable one), 0 = the record's own damage, -1 = none (immune: the ordinary hit). One line per captain and kind. */
static int kp_force_kill(uintptr_t c, int kind, const char *what, double t)
{
	if (!g_kp.rules)
		return 1;
	uintptr_t fm = kp_of_combat(c);
	if (!fm)
		return 1;
	somvr_cp_traits tr;
	char names[400], who[160];
	kp_traits(fm, &tr, names, sizeof names);
	int r = somvr_cp_force_kill(&tr, kind);
	r > 0 ? g_kp.rule_kills++ : r == 0 ? g_kp.rule_damage++ : g_kp.rule_none++;
	if (g_kp.told_c[kind] != c && kp_log_ok()) {
		g_kp.told_c[kind] = c;
		kp_who(fm, who, sizeof who);
		logf_("CAPTAIN t=%.3f %s on the captain (%s): %s to %s -> %s (SOMVR_CAPTAIN_RULES; traits %s)", t, what, who,
		      somvr_cp_rule_name(tr.rule[kind]), somvr_cp_kind_name(kind),
		      r > 0 ? "the kill (forceKill)" : r == 0 ? "the record's own damage through the game's rules (no forceKill)"
			    : "no finisher: the ordinary hit", names);
	}
	return r;
}
/* the record's own damage does not spare a captain (run 024656: 'PC_Bip_Standing_InstaKill_F' with forceKill 0 took a
 * resistant level-9 captain from 612.5 to 0), so a captain who is not vulnerable to the finisher is kept alive by his min
 * health for the call (CCharacter+0x9b4, the game's own clamp of damage: PR_SETMINHEALTH) - his health less
 * SOMVR_CAPTAIN_FINISHER_PCT of his max (33: the strongest blow of the fight, not his end), at least 1; back after it */
static void kp_floor_on(uintptr_t c, int fk, const char *what, double t, struct kp_floor *f)
{
	memset(f, 0, sizeof *f);
	if (fk != 0 || !g_kp.rules || !g_kp.ok_mh || !kp_of_combat(c))
		return;
	uintptr_t own = peek_ptr(c + C_OWNER);
	float hp = cb_health(c), mx = 0, was = 0;
	if (!own || !isfinite(hp) || !peek(own + PR_CH_HEALTH_MAX, &mx, sizeof mx) || !(mx > 0) ||
	    !peek(own + PR_CH_MINHEALTH, &was, sizeof was))
		return;
	float fl = fmaxf(1.0f, hp - mx * g_kp.fin_pct / 100.0f);
	PR_FN(PRS_MINHEALTH, pr_minhealth_fn)(own, fl);
	f->own = own, f->was = was, f->on = 1;
	g_kp.floors++;
	if (kp_log_ok())
		logf_("CAPTAIN t=%.3f %s on the captain: he survives it - his min health %.1f for the call (health %.1f of %.1f, "
		      "%.0f %% of it taken at most; SOMVR_CAPTAIN_FINISHER_PCT; it was %.1f)", t, what, fl, hp, mx, g_kp.fin_pct, was);
}
static void kp_floor_off(const struct kp_floor *f)
{
	if (f->on)
		PR_FN(PRS_MINHEALTH, pr_minhealth_fn)(f->own, f->was);
}

/* the same as forceKill's 0/1 for a call site that has no "none" (immune = the record's own damage) */
static int kp_force_kill01(uintptr_t c, int kind, const char *what, double t) { return kp_force_kill(c, kind, what, t) > 0; }

/* =stagger's health write, SOMVR_COMBAT_SLIVER: 1 = leave this orc alone (a captain: his hits are the game's) */
static int kp_no_cheat(uintptr_t c, const char *what)
{
	if (!g_kp.rules || !kp_of_combat(c))
		return 0;
	g_kp.rule_skips++;
	if (g_kp.told_c[SOMVR_CPF_N] != c && kp_log_ok()) {
		g_kp.told_c[SOMVR_CPF_N] = c;
		logf_("CAPTAIN orc %#lx is a captain: %s skipped - his hits are the game's own damage (SOMVR_CAPTAIN_RULES)",
		      (unsigned long)c, what);
	}
	return 1;
}

/* ---- 5. our cuts on a captain (build 24, SOMVR_CAPTAIN_DAMAGE=cut; docs/re-xr-pipeline.md 99.2) ------------------------
 * A captain takes the game's own damage for our hit (Combat_DealHit's record through his defence, buffs and clamps) times
 * a factor: a committed cut or stab (combat's heavy/stab tier: the edge leading, 50 cm of stroke, the contact at 12 m/s
 * or more) SOMVR_CAPTAIN_CUT_X x the cut's k (its contact speed: 0.6 at 12 m/s, 1 from 20), never under the game's own;
 * a light hit (a tap, the flat, a slow contact) the game's own. Applied to the DamageStruct the game built
 * (DamageStruct_DoDamage from FUN_02810e80: FRONT's FR_DMG_ATK hook) - his min health, the death and its credit, his
 * reaction stay the game's; forceKill's 1e6 and a heal untouched. Plain orcs: unchanged (=stagger's tiers). */
static int kp_dmg_want(void)
{
	const char *c = getenv("SOMVR_CAPTAIN"), *d = getenv("SOMVR_CAPTAIN_DAMAGE");
	return !(c && !strcmp(c, "off")) && !(d && !strcmp(d, "off"));
}
static void kp_hit_pre(uintptr_t c, uintptr_t hobj, int tier, float k, double t)
{
	(void)t;
	g_kph.on = 0;
	if (!g_kp.dmg || !g_kp.on || !g_ff.on_dmg || !kp_of_combat(c))
		return;
	float kk = isfinite(k) && k > 0 ? fminf(k, 1.0f) : 1.0f;
	float x = somvr_cp_cut_factor(tier == SC_TIER_HEAVY || tier == SC_TIER_STAB, k, g_kp.cut_x);
	g_kph.hobj = hobj, g_kph.c = c, g_kph.x = x, g_kph.k = kk, g_kph.a = g_kph.b = NAN, g_kph.hp0 = cb_health(c);
	g_kph.tid = my_tid(), g_kph.seen = 0, g_kph.tier = tier, g_kph.on = 1;
}
/* FR_DMG_ATK (every attack's damage, before FRONT's own look at it): our hit's DamageStruct on the captain */
static void kp_dmg(uintptr_t ds, uintptr_t hobj)
{
	if (!g_kph.on || g_kph.seen || hobj != g_kph.hobj || my_tid() != g_kph.tid)
		return;
	float a = NAN;
	if (!peek(ds + FF_DS_AMOUNT, &a, sizeof a))
		return;
	g_kph.seen = 1, g_kph.a = g_kph.b = a;
	if (!(a > 0) || a >= 1e5f || !(g_kph.x > 1.0f)) /* none, a heal, forceKill's 1e6, a light hit */
		return;
	float b = a * g_kph.x;
	memcpy((void *)(ds + FF_DS_AMOUNT), &b, sizeof b);
	g_kph.b = b;
}
static void kp_hit_post(uintptr_t c, double t)
{
	if (!g_kph.on)
		return;
	g_kph.on = 0;
	g_kp.dmg_hits++;
	float hp1 = cb_health(c);
	if (!g_kph.seen) {
		g_kp.dmg_unseen++;
		if (kp_log_ok())
			logf_("CAPTAIN t=%.3f our %s hit on the captain: no damage reached the game's DamageStruct (his block or the "
			      "data's 0); his health %.1f -> %.1f", t, sc_tier_name(g_kph.tier), (double)g_kph.hp0, (double)hp1);
		return;
	}
	if (g_kph.b != g_kph.a)
		g_kp.dmg_scaled++, g_kp.dmg_game += g_kph.a, g_kp.dmg_ours += g_kph.b;
	if (kp_log_ok())
		logf_("CAPTAIN t=%.3f our %s hit on the captain: the game's damage %.1f x %.2f = %.1f (the cut's k %.2f, "
		      "SOMVR_CAPTAIN_CUT_X %.2f); his health %.1f -> %.1f%s", t, sc_tier_name(g_kph.tier), (double)g_kph.a,
		      (double)g_kph.x, (double)g_kph.b, (double)g_kph.k, (double)g_kp.cut_x, (double)g_kph.hp0, (double)hp1,
		      cb_dead(c) ? " - dead" : "");
}

/* ---- 7. the stealth thrust into a captain immune to stealth kills (build 24, SOMVR_CAPTAIN_STEALTH_IMMUNE=turn; default hit:
 * unproven headless, docs/re-xr-pipeline.md 99.3) ---------------------------------------------------------------------
 * The game offers no stealth kill on him; build 23 dealt the thrust as an ordinary hit (a wound - the player cannot tell
 * a refused kill from a poor thrust). turn: the blade is turned at his back - no damage, no stick; the clank's sound and
 * sparks where it met him (the surface meeting's, sfb_h4, in the next combat frame), the stealth code's knock in the hand
 * and his alert (the attack stimulus): he turns to fight. 0 = not taken (=hit, or not a captain: the ordinary hit). */
static int kp_stealth_turn(uintptr_t c, const float E[3], double t)
{
	if (!g_kp.stealth_turn || !g_kp.on)
		return 0;
	uintptr_t fm = kp_of_combat(c);
	if (!fm)
		return 0;
	g_kp.turned++;
	memcpy(g_sf.pv_P, E, sizeof g_sf.pv_P);
	snprintf(g_sf.pv_surf, sizeof g_sf.pv_surf, "a captain's back");
	g_sf.pv_pend = 3, g_sf.pv_pend_t = t;
	char who[160];
	kp_who(fm, who, sizeof who);
	if (kp_log_ok())
		logf_("CAPTAIN t=%.3f the stealth thrust turned (%s): immune to stealth kills - no wound, the clank's sound and sparks "
		      "at %.1f,%.1f,%.1f, the knock in the hand, his alert (SOMVR_CAPTAIN_STEALTH_IMMUNE=turn)", t, who, (double)E[0],
		      (double)E[1], (double)E[2]);
	return 1;
}

/* ---- 6. test aids (build 24): the game's stat lines, the strikes' own damage on him ---------------------------------- */
static void kp_gamelog(double t)
{
	char got[200] = "";
	size_t o = 0;
	for (size_t i = 0; i < sizeof g_kp_dbg / sizeof g_kp_dbg[0]; i++) {
		char nm[48] = "";
		uintptr_t np = peek_ptr(g_kp_dbg[i].var + 8), v = g_kp_dbg[i].var + KP_DBG_VALUE;
		uint8_t was = 9, one = 1;
		int p = page_prot(v);
		int ok = np && peek(np, nm, sizeof nm - 1) && !strcmp(nm, g_kp_dbg[i].name) && peek(v, &was, 1) && was <= 1 &&
			 p >= 0 && (p & PROT_WRITE);
		if (ok)
			memcpy((void *)v, &one, 1);
		o += (size_t)snprintf(got + o, sizeof got - o, "%s%s %s", o ? ", " : "", g_kp_dbg[i].name, ok ? "1" : "NOT set");
		if (o >= sizeof got)
			o = sizeof got - 1;
	}
	char dir[1024], cmd[1100], why[256] = "";
	snprintf(dir, sizeof dir, "%s", g_log_path);
	char *sl = strrchr(dir, '/');
	if (sl)
		*sl = 0;
	snprintf(cmd, sizeof cmd, "Combat.Value.LogFileName %s/combat-debug.log", sl ? dir : ".");
	int rc = es_run(cmd, why, sizeof why);
	logf_("CAPTAIN t=%.3f gamelog (a test aid): %s; console '%s' %s%s", t, got, cmd, rc == 0 ? "run" : "NOT run: ",
	      rc == 0 ? "" : why);
}
/* `captain tiers`: the four strike records our tiers use (the pad's PC_HC_* strikes) dealt on him one by one, 1.5 s
 * apart, by Combat_DealHit as combat does (Talion's Combat, his chest, forceKill 0) - the game's own damage, no factor */
static void kp_tiers_tick(const struct pr_me *m, int have, double t)
{
	static const int tier[4] = { SC_TIER_LIGHT, SC_TIER_STAB, CB_MED, SC_TIER_HEAVY };
	if (!g_kp.tiers_step || t < g_kp.tiers_next)
		return;
	struct cb_orc o;
	int i = g_kp.tiers_step - 1;
	const char *nm = "(none)";
	uintptr_t rec = cb_record(tier[i], 0, &nm), pc = have ? m->pc : 0;
	if (!rec || !pc || !g_kp.c || cb_dead(g_kp.c) || !cb_orc_get(g_kp.c, &o) || !g_cb.ok_hit) {
		logf_("CAPTAIN t=%.3f tiers: step %d '%s' not dealt (%s)", t, i, nm, !rec ? "no record" : !pc ? "no Talion"
		      : !g_kp.c || cb_dead(g_kp.c) ? "no captain alive" : !g_cb.ok_hit ? "combat cannot deal hits" : "his place");
		g_kp.tiers_step = 0;
		return;
	}
	float dir[3] = { o.pos[0] - m->pos[0], 0, o.pos[2] - m->pos[2] }, l = hypotf(dir[0], dir[2]), hd0[3];
	if (l > 1)
		dir[0] /= l, dir[2] /= l;
	else
		dir[0] = 0, dir[2] = 1;
	unsigned char once = 0;
	peek(pc + C_HITDIR, hd0, sizeof hd0), peek(pc + C_HITONCE, &once, 1);
	memcpy((void *)(pc + C_HITDIR), dir, sizeof dir);
	*(volatile unsigned char *)(pc + C_HITONCE) = 0;
	float hp0 = cb_health(g_kp.c);
	int s0 = xc_streak_now();
	CB_FN(CS_DEALHIT, cb_dealhit_fn)((cb_ltvec){ o.pos[0], o.pos[1] + 120.0f, o.pos[2] }, pc, o.hobj, rec, 0);
	memcpy((void *)(pc + C_HITDIR), hd0, sizeof hd0);
	*(volatile unsigned char *)(pc + C_HITONCE) = once;
	float hp1 = cb_health(g_kp.c);
	g_kp.tiers_dealt++;
	logf_("CAPTAIN t=%.3f tiers: %s '%s' (the game's own, no factor): his health %.1f -> %.1f (%.1f), the streak %d -> %d, "
	      "%.1f m away", t, tier[i] == CB_MED ? "medium" : sc_tier_name(tier[i]), nm, (double)hp0, (double)hp1,
	      (double)(hp0 - hp1), s0, xc_streak_now(), (double)(l / 100.0f));
	g_kp.tiers_step = g_kp.tiers_step < 4 ? g_kp.tiers_step + 1 : 0, g_kp.tiers_next = t + 1.5;
}

/* ---- 1. the practice's Captain ------------------------------------------------------------------------------------ */
/* while our captain is out (alive) and the stakes are on: the practice's min health lifted */
static int kp_stakes_now(void) { return g_kp.stakes && g_kp.npc && !g_kp.dead_seen && !g_kp.gone_seen && pr_orc_alive() && g_pr.orc_npc == g_kp.npc; }

static void kp_spawn(const struct pr_me *m, double t, const char *who)
{
	const char *fail = !g_kp.on ? "the captain's sites did not check out at start (CAPTAIN lines)"
			   : !g_pr.ok_spawn ? "the spawn's sites did not check out at start"
			   : !g_pr.here ? "not in the practice area (go there first)"
			   : g_pr.trip.phase != SOMVR_PRT_IDLE ? "a trip is on" : NULL;
	uintptr_t mm[KP_MAX];
	somvr_cp_member cm[KP_MAX];
	int n = fail ? 0 : kp_members(mm, KP_MAX), pick = -1, req = atomic_exchange(&g_kp.req_pick, -1);
	if (!fail && !n)
		fail = "no faction members readable (the faction manager or its current faction)";
	for (int i = 0; i < n; i++) {
		memset(&cm[i], 0, sizeof cm[i]);
		somvr_cp_traits tr;
		kp_traits(mm[i], &tr, NULL, 0);
		peek(mm[i] + KP_FM_FLAGS, &cm[i].flags, sizeof cm[i].flags);
		cm[i].in_hierarchy = peek_ptr(mm[i] + KP_FM_POS) != 0, cm[i].warchief = tr.warchief;
		cm[i].has_template = KP_FN(KPS_TEMPLATE, kp_template_fn)(mm[i]) != 0;
	}
	if (!fail) {
		const char *why = NULL;
		pick = req >= 0 ? (req < n && somvr_cp_eligible(&cm[req], &why) ? req : -1) : somvr_cp_next(cm, n, g_kp.last);
		if (pick < 0)
			fail = req >= 0 ? (req < n ? why : "no such member") : "no captain of the save can come (each is a warchief, dead, in the world or without a template)";
	}
	uintptr_t fm = pick >= 0 ? mm[pick] : 0, tmpl = fm ? KP_FN(KPS_TEMPLATE, kp_template_fn)(fm) : 0;
	uintptr_t ground = 0, ctx = 0, ret = 0, h = 0, npc = 0, c = 0, zone = 0;
	float hit[3] = { 0, 0, 0 }, at[3], q[4], hp[3] = { 0, 0, 0 }, dist = g_kp.spawn_cm;
	uint32_t lvl = 0;
	if (!fail) {
		sv_vec3 f = sv_qrot(sv_qnormalize(sv_quat_of(m->rot)), (sv_vec3){ 0, 0, 1 });
		static const float dd[] = { 0, -300, 200, -600 }, yaw[] = { 0, 0.2617994f, -0.2617994f };
		int found = 0;
		for (size_t i = 0; i < sizeof dd / sizeof dd[0] && !found; i++)
			for (size_t j = 0; j < sizeof yaw / sizeof yaw[0] && !found; j++) {
				float cs = cosf(yaw[j]), sn = sinf(yaw[j]), fw[3] = { f.x * cs + f.z * sn, 0, -f.x * sn + f.z * cs };
				dist = g_kp.spawn_cm + dd[i];
				somvr_pr_ahead(m->pos, fw, dist, at);
				found = pr_ground(at, hit, &ground);
			}
		if (!found)
			fail = "no ground ahead at SOMVR_PRACTICE_CAPTAIN_CM -6..+2 m (3 directions)";
		else if (!(ctx = ground ? ground : m->pc ? peek_ptr(m->pc + C_HOBJ) : 0))
			fail = "no context object (neither the ground's nor Talion's)";
	}
	if (!fail) {
		pr_remove_orc("a captain comes", t);
		int away = atomic_exchange(&g_kp.req_away, 0); /* build 24, `captain away`: his back to Talion (a test aid) */
		float aw[3] = { 2.0f * hit[0] - m->pos[0], hit[1], 2.0f * hit[2] - m->pos[2] };
		somvr_pr_facing(hit, away ? aw : m->pos, q);
		float xf[7] = { hit[0], hit[1] + 10.0f, hit[2], q[0], q[1], q[2], q[3] };
		ret = PR_FN(PRS_SPAWN, pr_spawn_fn)(ctx, "SomVR_Practice", xf, tmpl, 1, 1, 0, fm, 0);
		h = ret ? peek_ptr(ret + 8) : 0;
		for (uintptr_t cls = h ? peek_ptr(peek_ptr(h + PR_HOBJ_CLASSES)) : 0, k = 0; cls && k < 32 && !npc;
		     cls = peek_ptr(cls + 8), k++)
			if (cls == g_prg.class_npc)
				npc = peek_ptr(h + PR_HOBJ_GAMEOBJ);
		if (!npc || vptr_of((void *)npc) != g_cbg.vt_npc)
			fail = "FUN_029a4830 made no NPC (placement refused, or the member's template is a prop)", npc = 0;
	}
	if (!fail) {
		peek(h + OBJ_POS, hp, sizeof hp);
		zone = PR_FN(PRS_ZONE, pr_zone_fn)(hp, 0, 0);
		if (pr_slot_ok(npc, PRS_SETZONE, PR_SLOT_SETZONE))
			PR_FN(PRS_SETZONE, pr_setzone_fn)(npc, zone, 1);
		KP_FN(KPS_BIND, kp_bind_fn)(npc, fm, 1);
		if (peek_ptr(fm + KP_FM_SPAWNCOND))
			KP_FN(KPS_ACTIVATE, kp_activate_fn)(fm, 1);
		peek(fm + KP_FM_LEVEL, &lvl, sizeof lvl);
		c = peek_ptr(npc + PR_CH_COMBAT);
		if (c && vptr_of((void *)c) == g_cbg.vt_combat)
			PR_FN(PRS_LEVEL, pr_level_fn)(c, lvl);
		else
			c = 0;
		char tree[64] = "";
		fb_rec_name(peek_ptr(fm + KP_FM_TREE), tree, sizeof tree);
		int type = strstr(tree, "Defender") ? SOMVR_PRO_SHIELD : strstr(tree, "Berserker") ? SOMVR_PRO_BERSERKER
											       : SOMVR_PRO_INFANTRY;
		g_pr.orc_h = h, g_pr.orc_npc = npc, g_pr.orc_c = c, g_pr.orc_type = type, g_pr.orc_t = t;
		g_pr.orc_dead = 0, g_pr.orc_checks = 0, g_pr.far = 0, g_pr.spawns++;
		g_kp.last = pick, g_kp.fm = fm, g_kp.npc = npc, g_kp.c = c, g_kp.h = h, g_kp.t0 = t, g_kp.next_watch = t + 1.0;
		g_kp.hp_last = c ? cb_health(c) : NAN, g_kp.hp_max0 = g_kp.hp_last, g_kp.health_t = t;
		g_kp.dead_seen = g_kp.gone_seen = g_kp.talion_dead = 0, g_kp.pres_n_world = g_kp.pres_n_window = 0;
		g_kp.place_d = 0, g_kp.place_next = t;
		peek(fm + KP_FM_FLAGS, &g_kp.flags0, sizeof g_kp.flags0);
		kp_who(fm, g_kp.who, sizeof g_kp.who);
		g_kp.spawns++;
	} else {
		g_kp.spawn_fail++;
	}
	char names[400] = "";
	if (fm)
		kp_traits(fm, &g_kp.tr, names, sizeof names);
	logf_("CAPTAIN t=%.3f spawn (%s) %s: %s%s%s; template %#lx at (%.1f %.1f %.1f) %.0f cm ahead facing Talion, hobj=%#lx "
	      "npc=%#lx Combat=%#lx zone=%#lx level %u health %.1f; the member's flags %#x -> %#x, its NPC %#lx, the NPC's seed "
	      "%#lx; traits %s; %d members read, the pick %d (last %d)", t, who, fail ? "REFUSED" : "ok", fail ? fail : g_kp.who,
	      fail ? "" : " - the save's own: his fate is for good", !fail && g_kp.stakes ? " (Talion can die while he is out: "
	      "SOMVR_PRACTICE_CAPTAIN_STAKES)" : "", (unsigned long)tmpl, hit[0], hit[1], hit[2], (double)dist, (unsigned long)h,
	      (unsigned long)npc, (unsigned long)c, (unsigned long)zone, lvl, c ? (double)cb_health(c) : -1.0, g_kp.flags0,
	      fm ? (uint32_t)peek_ptr(fm + KP_FM_FLAGS) : 0u, (unsigned long)(fm ? peek_ptr(fm + KP_FM_NPC) : 0),
	      (unsigned long)(npc ? peek_ptr(npc + KP_NPC_SEED) : 0), names[0] ? names : "-", n, pick, g_kp.last);
}

/* `captain status`: the hierarchy as the practice sees it */
static void kp_status(double t)
{
	uintptr_t mm[KP_MAX];
	int n = kp_members(mm, KP_MAX);
	logf_("CAPTAIN t=%.3f status: %d members (the faction %#lx); ours %#lx (%s)", t, n, (unsigned long)kp_faction(),
	      (unsigned long)g_kp.fm, g_kp.fm ? g_kp.who : "none");
	somvr_cp_member cm[KP_MAX];
	for (int i = 0; i < n; i++) {
		somvr_cp_traits tr;
		char names[400], who[160];
		const char *why = NULL;
		kp_traits(mm[i], &tr, names, sizeof names);
		memset(&cm[i], 0, sizeof cm[i]);
		peek(mm[i] + KP_FM_FLAGS, &cm[i].flags, sizeof cm[i].flags);
		cm[i].in_hierarchy = peek_ptr(mm[i] + KP_FM_POS) != 0, cm[i].warchief = tr.warchief;
		cm[i].has_template = KP_FN(KPS_TEMPLATE, kp_template_fn)(mm[i]) != 0;
		somvr_cp_eligible(&cm[i], &why);
		kp_who(mm[i], who, sizeof who);
		char ld[300] = "";
		if (cm[i].flags & 4 || peek_ptr(mm[i] + KP_FM_LD_ATTACK)) /* build 24: a dead one's record of it */
			ld[0] = ';', ld[1] = ' ', kp_death_desc(mm[i], ld + 2, sizeof ld - 2);
		logf_("CAPTAIN member %d: %s flags %#x: %s; combat finishers %s, stealth %s, ranged %s, grab %s; traits %s%s", i, who,
		      cm[i].flags, why, somvr_cp_rule_name(tr.rule[SOMVR_CPF_COMBAT]), somvr_cp_rule_name(tr.rule[SOMVR_CPF_STEALTH]),
		      somvr_cp_rule_name(tr.rule[SOMVR_CPF_RANGED]), somvr_cp_rule_name(tr.rule[SOMVR_CPF_GRAB]), names, ld);
	}
	logf_("CAPTAIN status: the next Captain brings member %d", somvr_cp_next(cm, n, g_kp.last));
}

/* ---- 2. the introduction -------------------------------------------------------------------------------------------- */
/* bm_pre: this frame's fallback bits -> the bits the body moments see (the presentation's bit cleared while it plays in
 * the world). dead = Talion's dead flag */
static uint32_t kp_fb(uintptr_t pl, uint32_t fb, int dead, double t)
{
	const uint32_t P = SOMVR_FB_BIT(SOMVR_FB_PRES);
	if (!(fb & P)) {
		if (g_kp.pres_on && t - g_kp.pres_last > 1.0) { /* over (a gap of 1 s ends it) */
			if (kp_log_ok())
				logf_("CAPTAIN t=%.3f the presentation is over after %.1f s (%s)", t, g_kp.pres_last - g_kp.pres_t0,
				      g_kp.pres_world ? "in the world" : "the window");
			g_kp.pres_on = 0;
		}
		return fb;
	}
	g_kp.pres_last = t;
	if (g_kp.pres_on)
		return g_kp.pres_world ? fb & ~P : fb;
	g_kp.pres_on = 1, g_kp.pres_t0 = t, g_kp.pres_world = 0;
	/* the decision, once: the nearest captain to the head in reach of the rule */
	sv_vec3 hp;
	sv_quat hq;
	somvr_rig_head(&g_rig, g_fr.base, g_fr.views.pose, &hp, &hq);
	sv_vec3 f = sv_qrot(sv_qnormalize(hq), (sv_vec3){ 0, 0, 1 });
	float head[3] = { hp.x, hp.y, hp.z }, fwd[3] = { f.x, f.y, f.z }, tp[3] = { 0, 0, 0 };
	int have_head = vec_ok(head, 1e7f) && isfinite(f.x + f.y + f.z);
	uintptr_t pm = 0, tobj = 0;
	int body = pl && player_objs(pl, &pm, &tobj) && peek(tobj + OBJ_POS, tp, sizeof tp) && vec_ok(tp, 1e7f);
	struct cb_orc near[12];
	int n = 0;
	if (body && g_kp.on) {
		struct bo_act a;
		bo_active(pl, &a);
		if (a.pc)
			n = cb_orcs_r(a.pc, a.sc, tp, g_kp.icfg.range_cm + 1000.0f, near, 11);
	}
	if (g_kp.c && pr_orc_alive() && g_pr.orc_c == g_kp.c) { /* ours, in the bands or not */
		int dup = 0;
		for (int i = 0; i < n; i++)
			dup |= near[i].c == g_kp.c;
		if (!dup && cb_orc_get(g_kp.c, &near[n]))
			near[n].dist = dist3(near[n].pos, tp), n++;
	}
	uintptr_t best = 0, bfm = 0;
	float bd = 0, bo = 0;
	int bw = 0;
	for (int i = 0; i < n; i++) {
		uintptr_t fm = kp_of_combat(near[i].c);
		float d = 0, o = 0;
		if (!fm)
			continue;
		int w = have_head && somvr_cp_intro_world(&g_kp.icfg, head, fwd, near[i].pos, &d, &o);
		if (!best || (w && !bw) || (w == bw && d < bd))
			best = near[i].c, bfm = fm, bd = d, bo = o, bw = w;
	}
	const char *why = !g_kp.intro_world ? "SOMVR_CAPTAIN_INTRO=window" : dead ? "Talion is dead (the death's screens)"
			  : !g_kp.on ? "the captain's sites did not check out" : !have_head ? "no head pose yet"
			  : !best ? "no captain near (another kind of presentation)" : !bw ? "the captain is not near and in view"
			  : NULL;
	g_kp.pres_world = !why;
	g_kp.pres_world ? g_kp.intros_world++ : g_kp.intros_window++;
	if (best && best == g_kp.c)
		g_kp.pres_world ? g_kp.pres_n_world++ : g_kp.pres_n_window++;
	if (kp_log_ok()) {
		char who[160] = "-";
		if (bfm)
			kp_who(bfm, who, sizeof who);
		logf_("CAPTAIN t=%.3f a presentation starts: %s%s; the captain %s (orc %#lx%s) %.1f m from the head, %.0f deg off "
		      "the view (the rule: within %.0f m and %.0f deg)", t, g_kp.pres_world ? "in the world - the player's own view, "
		      "the presentation camera ignored" : "the window: ", why ? why : "", who, (unsigned long)best,
		      best && best == g_kp.c ? ", the practice's" : "", bd / 100.0, bo, g_kp.icfg.range_cm / 100.0,
		      g_kp.icfg.half_deg);
	}
	return g_kp.pres_world ? fb & ~P : fb;
}

/* ---- 4. his fate (pr_frame, every frame) --------------------------------------------------------------------------- */
static void kp_frame(const struct pr_me *m, int have, double t)
{
	if (atomic_exchange(&g_kp.req_status, 0))
		kp_status(t);
	if (atomic_exchange(&g_kp.req_gamelog, 0))
		kp_gamelog(t);
	if (atomic_exchange(&g_kp.req_tiers, 0))
		g_kp.tiers_step = 1, g_kp.tiers_next = t;
	kp_tiers_tick(m, have, t);
	int hv = atomic_exchange(&g_kp.req_hp, -1);
	if (hv >= 0 && g_kp.c && !cb_dead(g_kp.c)) { /* a test aid: his health set (the grab's gate at a given health) */
		uintptr_t own = peek_ptr(g_kp.c + C_OWNER);
		float v = hv / 10.0f, was = cb_health(g_kp.c);
		if (own && v >= 1.0f && arm_poke(own + CH_HEALTH, &v, sizeof v))
			logf_("CAPTAIN t=%.3f his health %.1f -> %.1f (a test aid: the word)", t, (double)was, (double)cb_health(g_kp.c));
	}
	if (atomic_exchange(&g_kp.req_charge, 0) && have && m->pc && g_xc.ok_hc) { /* a test aid: the game's own setter */
		uintptr_t hc = peek_ptr(m->pc + FI_C_HITCTR);
		int16_t need = 0;
		if (hc && peek(hc + XC_HC_NEED, &need, sizeof need) && need > 0 && need < 100) {
			XC_FN(XCS_CHARGE, xc_charge_fn)(hc, need);
			logf_("CAPTAIN t=%.3f Talion's hit counter charged to %d (a test aid: the word; XC_CHARGE)", t, need);
		}
	}
	if (!g_kp.fm)
		return;
	/* Talion's death while he is out: the promotion follows (the game's screens) */
	if (have && m->pc && cb_dead(m->pc) && !g_kp.talion_dead && !g_kp.dead_seen && !g_kp.gone_seen) {
		g_kp.talion_dead = 1;
		logf_("CAPTAIN t=%.3f Talion is down (his dead flag; life %d) with the captain out (%s, health %.1f): the Nemesis "
		      "death and promotion are the game's (their screens in the window)", t, m->life, g_kp.who, (double)g_kp.hp_last);
	}
	int alive = g_pr.orc_npc == g_kp.npc && pr_orc_alive();
	uint32_t fl = 0;
	peek(g_kp.fm + KP_FM_FLAGS, &fl, sizeof fl);
	if (g_kp.c && !g_kp.dead_seen && alive && cb_dead(g_kp.c)) {
		g_kp.dead_seen = 1, g_kp.next_watch = t + 2.0;
		char ld[300];
		kp_death_desc(g_kp.fm, ld, sizeof ld);
		logf_("CAPTAIN t=%.3f he died %.1f s after the spawn (%s): the member's flags %#x (in the world %u, IsDead %u, "
		      "KilledByPlayer %u (no run-time writer: 99.1), IsSevered %u, IsMockDead %u); %s; presentations while he was "
		      "out: %d in the world, %d in the window", t, t - g_kp.t0, g_kp.who, fl, fl >> 1 & 1, fl >> 2 & 1, fl >> 3 & 1,
		      fl >> 17 & 1, fl >> 23 & 1, ld, g_kp.pres_n_world, g_kp.pres_n_window);
	} else if (!g_kp.dead_seen && !g_kp.gone_seen && !alive) {
		g_kp.gone_seen = 1, g_kp.next_watch = t + 2.0;
		logf_("CAPTAIN t=%.3f he left the world alive %.1f s after the spawn (%s; removed by the practice, or an escape): "
		      "the member's flags %#x (in the world %u, IsDead %u), its NPC %#lx", t, t - g_kp.t0, g_kp.who, fl, fl >> 1 & 1,
		      fl >> 2 & 1, (unsigned long)peek_ptr(g_kp.fm + KP_FM_NPC));
	}
	if ((g_kp.dead_seen || g_kp.gone_seen) && t >= g_kp.next_watch) { /* the member 2 s later: the save's record of it */
		char ld[300];
		kp_death_desc(g_kp.fm, ld, sizeof ld);
		logf_("CAPTAIN t=%.3f the member 2 s on: flags %#x (in the world %u, IsDead %u, KilledByPlayer %u, IsMockDead %u), "
		      "its NPC %#lx, level %u; %s", t, fl, fl >> 1 & 1, fl >> 2 & 1, fl >> 3 & 1, fl >> 23 & 1,
		      (unsigned long)peek_ptr(g_kp.fm + KP_FM_NPC), (uint32_t)peek_ptr(g_kp.fm + KP_FM_LEVEL), ld);
		g_kp.fm = g_kp.npc = g_kp.c = g_kp.h = 0;
		return;
	}
	if (!alive || !g_kp.c || t < g_kp.next_watch)
		return;
	g_kp.next_watch = t + 0.25;
	float hp = cb_health(g_kp.c), mx = NAN; /* build 24: his max as the character has it now (his level applies ~1 s in) */
	uintptr_t own = peek_ptr(g_kp.c + C_OWNER);
	if (own && peek(own + PR_CH_HEALTH_MAX, &mx, sizeof mx) && isfinite(mx) && mx > 0)
		g_kp.hp_max0 = mx;
	/* a drop of 0.5, a rise of 10 (build 24: his regeneration in lines of 10, not every quarter second) */
	if (isfinite(hp) && (!isfinite(g_kp.hp_last) || g_kp.hp_last - hp >= 0.5f || hp - g_kp.hp_last >= 10.0f) && kp_log_ok()) {
		char an[96] = "-";
		fb_rec_name(peek_ptr(g_kp.c + RX_C_ATTACK), an, sizeof an);
		float d = 0;
		struct cb_orc o;
		if (have && cb_orc_get(g_kp.c, &o))
			d = dist3(o.pos, m->pos);
		logf_("CAPTAIN t=%.3f his health %.1f -> %.1f (of %.1f; %.1f s in), %.1f m from Talion, his attack '%s'", t,
		      (double)g_kp.hp_last, (double)hp, (double)g_kp.hp_max0, t - g_kp.t0, d / 100.0, an);
		g_kp.hp_last = hp;
	}
	/* build 24: his place once a second while he is low (a third of his health or less: Fear_LowHealth's flight) */
	struct cb_orc po;
	if (have && t >= g_kp.place_next && isfinite(hp) && g_kp.hp_max0 > 0 && hp <= g_kp.hp_max0 / 3.0f &&
	    cb_orc_get(g_kp.c, &po)) {
		float d = dist3(po.pos, m->pos);
		if (fabsf(d - g_kp.place_d) >= 100.0f && g_kp.flee_lines < 120) {
			char an[96] = "-";
			fb_rec_name(peek_ptr(g_kp.c + RX_C_ATTACK), an, sizeof an);
			g_kp.flee_lines++;
			logf_("CAPTAIN t=%.3f his place: %.1f m from Talion (%+.1f m since the last line), health %.1f of %.1f, his "
			      "attack '%s'", t, (double)(d / 100.0f), (double)((d - g_kp.place_d) / 100.0f), (double)hp,
			      (double)g_kp.hp_max0, an);
			g_kp.place_d = d;
		}
		g_kp.place_next = t + 1.0;
	}
}

/* `captain status|pick N|hp V|charge|stakes on|off` */
static const char *kp_word(const char *w)
{
	while (*w == ' ')
		w++;
	if (!strncmp(w, "status", 6)) {
		atomic_store(&g_kp.req_status, 1);
		return "captain status";
	}
	if (!strncmp(w, "gamelog", 7)) { /* build 24: the game's own stat lines into the run's combat-debug.log */
		atomic_store(&g_kp.req_gamelog, 1);
		return "captain gamelog";
	}
	if (!g_vb.test)
		return "captain (pick and hp need SOMVR_HANDS_TEST: ignored)";
	char *e;
	if (!strncmp(w, "pick ", 5)) {
		long v = strtol(w + 5, &e, 10);
		if (e == w + 5 || v < 0 || v >= KP_MAX)
			return "captain pick (not 0..63: ignored)";
		atomic_store(&g_kp.req_pick, (int)v);
		return "captain pick (the next Captain brings that member)";
	}
	if (!strcmp(w, "away")) { /* build 24: the next Captain comes with his back to Talion (the stealth thrust's test) */
		atomic_store(&g_kp.req_away, 1);
		return "captain away (the next one his back to Talion)";
	}
	if (!strcmp(w, "tiers")) { /* build 24: the strike records' own damage on him */
		atomic_store(&g_kp.req_tiers, 1);
		return "captain tiers (a test aid)";
	}
	if (!strcmp(w, "charge")) {
		atomic_store(&g_kp.req_charge, 1);
		return "captain charge (a test aid)";
	}
	if (!strncmp(w, "stakes ", 7)) { /* the A/B in one boot: the first captain without, the next with */
		int on = !strcmp(w + 7, "on") ? 1 : !strcmp(w + 7, "off") ? 0 : -1;
		if (on < 0)
			return "captain stakes (not on|off: ignored)";
		g_kp.stakes = on;
		return on ? "captain stakes on" : "captain stakes off";
	}
	if (!strncmp(w, "hp ", 3)) {
		double v = strtod(w + 3, &e);
		if (e == w + 3 || !(v >= 1 && v <= 5000))
			return "captain hp (not 1..5000: ignored)";
		atomic_store(&g_kp.req_hp, (int)(v * 10));
		return "captain hp (a test aid)";
	}
	return "captain (not status|gamelog|pick N|hp V|charge|tiers|away|stakes on|off: ignored)";
}

static void kp_start(int *bad)
{
	static const char *const intro_w[] = { "window", "world" }, *const onoff_w[] = { "off", "on" };
	g_kp.want = env_word("SOMVR_CAPTAIN", onoff_w, 2, 1);
	g_kp.intro_world = env_word("SOMVR_CAPTAIN_INTRO", intro_w, 2, 1);
	g_kp.rules = env_word("SOMVR_CAPTAIN_RULES", onoff_w, 2, 1);
	g_kp.stakes = env_word("SOMVR_PRACTICE_CAPTAIN_STAKES", onoff_w, 2, 1);
	g_kp.spawn_cm = env_float("SOMVR_PRACTICE_CAPTAIN_CM", 1000, 400, 2500, bad);
	g_kp.fin_pct = env_float("SOMVR_CAPTAIN_FINISHER_PCT", 33, 5, 95, bad);
	static const char *const dmg_w[] = { "off", "cut" };
	g_kp.dmg = env_word("SOMVR_CAPTAIN_DAMAGE", dmg_w, 2, 1); /* build 24 */
	g_kp.cut_x = env_float("SOMVR_CAPTAIN_CUT_X", 2.0f, 1.0f, 5.0f, bad);
	static const char *const turn_w[] = { "hit", "turn" };
	g_kp.stealth_turn = env_word("SOMVR_CAPTAIN_STEALTH_IMMUNE", turn_w, 2, 0); /* hit: turn is unproven (99.3) */
	somvr_cp_intro_default(&g_kp.icfg);
	const char *g = getenv("SOMVR_CAPTAIN_INTRO_GEOM");
	if (g && *g) {
		float r = 0, a = 0;
		if (sscanf(g, "%f,%f", &r, &a) == 2 && r >= 300 && r <= 4000 && a >= 10 && a <= 90)
			g_kp.icfg.range_cm = r, g_kp.icfg.half_deg = a;
		else
			logf_("WARN SOMVR_CAPTAIN_INTRO_GEOM=%s is not RANGE_CM,HALF_DEG (300..4000, 10..90): 1500,60", g), *bad = 1;
	}
	atomic_store(&g_kp.req_pick, -1), atomic_store(&g_kp.req_hp, -1);
	char why[400];
	int ok = g_kp.want;
	for (int i = 0; i < KPS_N && g_kp.want; i++) {
		int k = check_site(&g_kp_sites[i], why, sizeof why) == 0;
		ok &= k;
		logf_("CAPTAIN site %s %s %#lx -> %#lx: %s", g_kp_sites[i].name, k ? "ok" : "MISMATCH",
		      (unsigned long)g_kp_sites[i].addr, (unsigned long)g_kp_sites[i].target, why);
	}
	g_kp.on = ok;
	g_kp.ok_mh = ok && check_site(&g_pr_sites[PRS_MINHEALTH], why, sizeof why) == 0;
	if (ok && !g_kp.ok_mh)
		logf_("CAPTAIN site PR_SETMINHEALTH MISMATCH (%s): a captain not vulnerable to our finisher dies to it", why);
	logf_("CONFIG captain=%s (SOMVR_CAPTAIN%s): the practice's Captain = the save's next captain %.0f cm ahead (stakes %s: "
	      "Talion %s while he is out); intro=%s (in the world within %.0f m and %.0f deg of the view, else the window); "
	      "rules=%s (the execution and the stealth kill by his traits - not vulnerable: he survives it, %.0f %% of his max "
	      "taken at most -, no health writes on captains)",
	      g_kp.on ? "on" : "off", g_kp.want && !ok ? ": a site did not check out - off" : "", g_kp.spawn_cm,
	      onoff_w[g_kp.stakes], g_kp.stakes ? "can die" : "cannot die", intro_w[g_kp.intro_world], g_kp.icfg.range_cm / 100.0,
	      g_kp.icfg.half_deg, onoff_w[g_kp.rules], g_kp.fin_pct);
	logf_("CONFIG captain damage=%s (SOMVR_CAPTAIN_DAMAGE, build 24): %s; cut_x=%.2f (SOMVR_CAPTAIN_CUT_X); the damage hook "
	      "(FRONT's FR_DMG_ATK) %s", dmg_w[g_kp.dmg], g_kp.dmg ? "a committed cut or stab on a captain takes the game's damage "
	      "x cut_x x the cut's k (at least the game's own), a light hit the game's own" : "the game's own damage for every hit "
	      "(build 23)", g_kp.cut_x, g_ff.on_dmg ? "in" : g_kp.dmg && g_kp.on ? "NOT in (SOMVR_FRONT off or its site): the "
	      "game's own damage" : "not needed");
	logf_("CONFIG captain stealth_immune=%s (SOMVR_CAPTAIN_STEALTH_IMMUNE, build 24): %s", turn_w[g_kp.stealth_turn],
	      g_kp.stealth_turn ? "a stealth thrust into a captain immune to it is turned (the clank, no wound, his alert)"
				: "the ordinary hit (build 23)");
	if (g_kp.dmg && g_kp.on && !g_ff.on_dmg)
		logf_("WARN SOMVR_CAPTAIN_DAMAGE=cut without FRONT's damage hook (SOMVR_FRONT off or FR_DMG_ATK): no factor on captains");
}

static void kp_end_log(void)
{
	if (!g_kp.want)
		return;
	logf_("CAPTAIN END: spawns %llu (failed %llu); presentations in the world %llu, in the window %llu; the rules: kills "
	      "%llu, the record's damage %llu (survived %llu), no finisher %llu, health writes skipped %llu; member reads %llu",
	      (unsigned long long)g_kp.spawns, (unsigned long long)g_kp.spawn_fail, (unsigned long long)g_kp.intros_world,
	      (unsigned long long)g_kp.intros_window, (unsigned long long)g_kp.rule_kills,
	      (unsigned long long)g_kp.rule_damage, (unsigned long long)g_kp.floors, (unsigned long long)g_kp.rule_none, (unsigned long long)g_kp.rule_skips,
	      (unsigned long long)g_kp.member_reads);
	logf_("CAPTAIN END damage (%s, cut_x %.2f): our hits on captains %llu, scaled %llu (the game's %.0f -> ours %.0f), no "
	      "damage reached %llu; tiers dealt %llu; his place lines %llu; stealth thrusts turned %llu", g_kp.dmg ? "cut" : "off", (double)g_kp.cut_x,
	      (unsigned long long)g_kp.dmg_hits, (unsigned long long)g_kp.dmg_scaled, g_kp.dmg_game, g_kp.dmg_ours,
	      (unsigned long long)g_kp.dmg_unseen, (unsigned long long)g_kp.tiers_dealt, (unsigned long long)g_kp.flee_lines,
	      (unsigned long long)g_kp.turned);
}
