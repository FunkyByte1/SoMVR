/* somvr_practice_game.c - SOMVR_PRACTICE=1|auto, the practice area's game side (somvr_practice.h; docs/re-xr-pipeline.md
 * 27). NOT a translation unit of its own: somvr.c includes it near its end (it uses somvr.c's statics: peek, peek_ptr,
 * vptr_of, check_site, the log, the console call es_run with its ES_RUNSTRING site, the fallback's reads and sites
 * (FB_P_*, g_fb_sites, fb_rec_name), the combat module's PlayerCombat bands and objects (P_COMBAT, PC_*, C_*, g_cbg,
 * cb_dead), player_objs, game_paused, the frame's record g_fr, the VR menu's object g_vmo, the env helpers).
 *
 * Calls (the game thread: every game call and every action happens in pr_frame):
 *   pr_frame(pl)   H4, after the frame's body (flat or stereo): the requests, the trip, the area (min health), the auto
 *                  go, our orc, the keep-clear pass (about once a second), the light's steps, the menu's state
 *   pr_fade()      H4's stereo path, with SOMVR_DASH's blink: the trip's black into the record's fade level
 *   pr_word(w)     the writer thread: the trigger word "practice go|back|spawn [TYPE]|remove|status"
 *   pr_start / pr_end_log / pr_avail from somvr_start / somvr_fini / vm_start
 *
 * What it calls (all proven by P9 from the probe on the user's save copy, runtime-probes.md P9 results; each one a
 * byte-checked site below or an existing one):
 *   the console (ES_RUNSTRING, RunConsoleString): `Teleport x y z` (P5: moved the root), GameServerShell.AdvanceTimeOfDay
 *     (P9: one step = Day -> Night), GameServerShell.ChangeWeather NAME (P9: 79 names, DayNoRain among them);
 *   the debug spawn's own path (re-combat-spawn.md 1.2/1.3): the template by name FUN_02153910, a ray down
 *     (IEnginePhysics vf_64), FUN_029a4830(ctx, name, xf, template, 1, 1, 0, 0, 0), the zone FUN_022a2a80 + the NPC's
 *     vtbl+0x288, the level FUN_026ee7c0; the removal: the object system's vtbl+0x58 (deferred, P9: clean);
 *   SetMinHealth's setter FUN_0269da10 (movss [rdi+0x9b4], xmm0; ret: the field CCharacter::vf_37 clamps damage to);
 *   the weather state CGameServices::vf_21 (mov rax, [rdi+0x6c80]; ret): +0x60 the weather record, +0x68 the TOD byte;
 *     the TOD's name FUN_025ccd10 (P9's `tod`).
 * Fail-soft: a site that does not match turns its part off with a WARN (nothing is patched anywhere: every site here is
 * read-only); no console = no practice at all. */

/* PRACTICE_SITES_BEGIN (read only, never patched; checked at start with SOMVR_PRACTICE; counted on its own by
 * check_sites.py): PR_TMPL_BYNAME FUN_02153910 (AI/Templates by name); PR_SPAWN_AI FUN_029a4830 (the debug spawn's
 * call); PR_ZONE_AT FUN_022a2a80 (the zone at a point); PR_SETLEVEL FUN_026ee7c0 (Combat's level); PR_SETMINHEALTH
 * FUN_0269da10 (the min health's store: its bytes prove CCharacter+0x9b4); PR_TOD_NAME FUN_025ccd10; PR_RAYCAST
 * IEnginePhysics (vtable 0x3472258) slot 64; PR_NPC_SETZONE the NPC's (vtable 0x3a2b3d0) slot 81 (+0x288);
 * PR_OBJSYS_REMOVE the object system's (vtable 0x3453058) slot 11 (+0x58); PR_WEATHER CGameServices (vtable 0x34e2a90,
 * the object at *0x46b7310 = IGameServices.Default) slot 21 (+0xa8). Order = PRS_* */
static SITE_TABLE struct site_spec g_pr_sites_game[] = {
SITE("PR_TMPL_BYNAME", K_ENTRY, 0x2153910, 0x2153910, "0f b6 07 31 d2 84 c0 74 21 0f 1f 80 00 00 00 00", 0x2153910, "0f b6 07 31 d2 84 c0 74 21 0f 1f 80 00 00 00"),
SITE("PR_SPAWN_AI", K_ENTRY, 0x29a4830, 0x29a4830, "48 85 ff 0f 84 b7 07 00 00 41 57 48 89 f8 45 89", 0x29a4830, "48 85 ff 0f 84 b7 07 00 00 41 57 48 89 f8 45"),
SITE("PR_ZONE_AT", K_ENTRY, 0x22a2a80, 0x22a2a80, "41 57 41 56 41 55 41 54 49 89 d4 55 53 48 89 fb", 0x22a2a80, "41 57 41 56 41 55 41 54 49 89 d4 55 53 48 89"),
SITE("PR_SETLEVEL", K_ENTRY, 0x26ee7c0, 0x26ee7c0, "53 ba 01 00 00 00 48 89 fb e8 82 f7 ff ff 48 89", 0x26ee7c0, "53 ba 01 00 00 00 48 89 fb e8 82 f7 ff ff 48"),
SITE("PR_SETMINHEALTH", K_ENTRY, 0x269da10, 0x269da10, "f3 0f 11 87 b4 09 00 00 c3 90 66 0f 1f 44 00 00", 0x269da10, "f3 0f 11 87 b4 09 00 00 c3 90 66 0f 1f 44 00"),
SITE("PR_TOD_NAME", K_ENTRY, 0x25ccd10, 0x25ccd10, "53 0f b6 05 78 8e 66 02 89 fb 84 c0 74 22 39 1d", 0x25ccd10, "53 0f b6 05 78 8e 66 02 89 fb 84 c0 74 22 39"),
SITE("PR_RAYCAST", K_VSLOT, 0x3472458, 0x3472458, "d0 4f 8f 00 00 00 00 00", 0x8f4fd0, "48 8b 7f 08 48 85 ff 74 07 e9 02 96 fe ff 66"),
SITE("PR_NPC_SETZONE", K_VSLOT, 0x3a2b658, 0x3a2b658, "b0 39 96 02 00 00 00 00", 0x29639b0, "53 48 89 fb 48 83 ec 10 48 8b 47 10 48 8b 50"),
SITE("PR_OBJSYS_REMOVE", K_VSLOT, 0x34530b0, 0x34530b0, "60 6f 8c 00 00 00 00 00", 0x8c6f60, "48 85 f6 74 1b 48 83 ec 08 48 8b 7f 08 e8 66"),
SITE("PR_WEATHER", K_VSLOT, 0x34e2b38, 0x34e2b38, "70 37 9f 00 00 00 00 00", 0x9f3770, "48 8b 87 80 6c 00 00 c3 0f 1f 84 00 00 00 00"),
};
/* PRACTICE_SITES_END */
enum { PRS_BYNAME, PRS_SPAWN, PRS_ZONE, PRS_LEVEL, PRS_MINHEALTH, PRS_TODNAME, PRS_RAY, PRS_SETZONE, PRS_REMOVE, PRS_WEATHER,
       PRS_N };

/* PRQUIET_SITES_BEGIN (SOMVR_PRACTICE_QUIET, build 6: "voices from a bunch of different places, but no orcs"; read only,
 * never patched; checked at start with SOMVR_PRACTICE; counted on its own by check_sites.py). The HavokScript bindings
 * AISpawnManager.Enable / .Disable / .RemoveAllAmbientAI (FUN_023dc430 / 4c0 / 550) call these three: each loads the
 * ambient AI spawn manager *0x4c4f7c0 (the rip displacement is in the checked bytes) and jumps to its method:
 * FUN_02919d70 / d80 store 1 / 0 in its byte +0x164, FUN_02919d90 removes every entry of its list (+0x48..+0x50, 0x50
 * bytes each, +0x30 the NPC, the NPC's +8 HOBJECT) through the object system's vtbl+0x58 (deferred, as ours). The
 * manager's update FUN_02921370 runs its five spawn queues only while +0x164 is set. PRQ_FLAG_ON = FUN_02919d70 itself
 * (`movb $1, 0x164(%rdi); ret`): it proves the offset we read first. Order = PRQ_* */
static SITE_TABLE struct site_spec g_prq_sites_game[] = {
SITE("PRQ_ENABLE", K_ENTRY, 0x286fb80, 0x286fb80, "48 8b 3d 39 fc 3d 02 e9 e4 a1 0a 00 0f 1f 40 00", 0x286fb80, "48 8b 3d 39 fc 3d 02 e9 e4 a1 0a 00 0f 1f 40"),
SITE("PRQ_DISABLE", K_ENTRY, 0x286fb90, 0x286fb90, "48 8b 3d 29 fc 3d 02 e9 e4 a1 0a 00 0f 1f 40 00", 0x286fb90, "48 8b 3d 29 fc 3d 02 e9 e4 a1 0a 00 0f 1f 40"),
SITE("PRQ_REMOVEALL", K_ENTRY, 0x286fba0, 0x286fba0, "48 8b 3d 19 fc 3d 02 e9 e4 a1 0a 00 0f 1f 40 00", 0x286fba0, "48 8b 3d 19 fc 3d 02 e9 e4 a1 0a 00 0f 1f 40"),
SITE("PRQ_FLAG_ON", K_ENTRY, 0x2919d70, 0x2919d70, "c6 87 64 01 00 00 01 c3 0f 1f 84 00 00 00 00 00", 0x2919d70, "c6 87 64 01 00 00 01 c3 0f 1f 84 00 00 00 00"),
};
/* PRQUIET_SITES_END */
enum { PRQ_ENABLE, PRQ_DISABLE, PRQ_REMOVEALL, PRQ_FLAG_ON, PRQ_N };
_Static_assert(sizeof g_prq_sites_game / sizeof g_prq_sites_game[0] == PRQ_N, "practice quiet sites");
static const struct site_spec *g_prq_sites = g_prq_sites_game; /* selftest: stand-ins */

/* PRNAV_SITES_BEGIN (the story drill's round 2: the test word "practice status nav" logs the game's nav markers - the
 * story's and the missions' places - with their world positions, as the probe's navdump (P7, docs/runtime-probes.md);
 * read only, never patched; checked at the first such word; counted on its own by check_sites.py). PRN_NAV_POS
 * FUN_00ebc700: a marker's world position (the probe's F_NAV_POS). The CNavMarkerInstance list: its sentinel at
 * 0x46b9200 (first = +8, next = node+8, the marker = node - 0x80), the NavMarkerData record at +0x298 (its name at +0x20
 * when the record's byte +1 has bit 2), active / enabled bytes +0x2e0 / +0x2e1, an object at +0x260/+0x268/+0x270/+0xb0
 * (the probe's rule for asking the position). Order = PRN_* */
static SITE_TABLE struct site_spec g_prn_sites_game[] = {
SITE("PRN_NAV_POS", K_ENTRY, 0xebc700, 0xebc700, "48 8b 87 50 02 00 00 48 85 c0 74 0f 48 89 c7 48", 0xebc700, "48 8b 87 50 02 00 00 48 85 c0 74 0f 48 89 c7"),
};
/* PRNAV_SITES_END */
enum { PRN_NAV_POS, PRN_N };

/* PRFILL_SITES_BEGIN (build 17, the practice page's Refill arrows + focus and Refill health - the user's ask;
 * re-xr-pipeline.md 56; read only, never patched, called on the game thread; checked at start with SOMVR_PRACTICE;
 * counted on its own by check_sites.py). PRF_TOKENS_ADD FUN_02720a90(res, delta): the resource's tokens (the arrows,
 * +0x50; +0x4c their max) raised by delta up to the max, the HUD and the anim parameter Game_NumTokens refreshed (re-bow.md
 * 2.2; the bow's BOW_TOKENS_ADD, also what our own shot calls with -1); PRF_ENERGY_FULL FUN_0271e2e0(res, unlimited):
 * stores the unlimited flag (+0x60), the energy (the bow's Focus, +0x44) and +0x54 = the max (+0x48), then the energy's
 * notify FUN_0271d600 (the HUD meter) - its bytes prove the three offsets; PRF_HEAL FUN_02763910(health, amount): the heal
 * the damage path calls for a negative amount (re-combat.md 1.3, FUN_02810e80), CCharacter+0x130's component: +0x34 the
 * health (CCharacter+0x164) raised by amount up to +0x30, the max (CCharacter+0x160) - its bytes prove both. Order = PRF_* */
static SITE_TABLE struct site_spec g_prf_sites_game[] = {
SITE("PRF_TOKENS_ADD", K_ENTRY, 0x2720a90, 0x2720a90, "8b 47 50 01 c6 39 f0 0f 84 c3 00 00 00 55 31 c0", 0x2720a90, "8b 47 50 01 c6 39 f0 0f 84 c3 00 00 00 55 31"),
SITE("PRF_ENERGY_FULL", K_ENTRY, 0x271e2e0, 0x271e2e0, "f3 0f 10 47 48 40 88 77 60 f3 0f 11 47 54 f3 0f", 0x271e2e0, "f3 0f 10 47 48 40 88 77 60 f3 0f 11 47 54 f3"),
SITE("PRF_HEAL", K_ENTRY, 0x2763910, 0x2763910, "f3 0f 10 57 34 f3 0f 10 4f 30 31 c0 0f 2f d1 73", 0x2763910, "f3 0f 10 57 34 f3 0f 10 4f 30 31 c0 0f 2f d1"),
};
/* PRFILL_SITES_END */
enum { PRF_TOKENS_ADD, PRF_ENERGY_FULL, PRF_HEAL, PRF_N };
_Static_assert(sizeof g_prf_sites_game / sizeof g_prf_sites_game[0] == PRF_N, "practice refill sites");
static const struct site_spec *g_prf_sites = g_prf_sites_game; /* selftest: stand-ins */
#define PRF_RES_MAX OFS(PRF_RES_MAX, 0x4c)        /* the resource object (Combat+0x1c0, BO_SC_RES): int, the arrows' max (BO_RES_COUNT +0x50) */
#define PRF_RES_ENERGY OFS(PRF_RES_ENERGY, 0x44)     /* ... float, the energy = the Focus (FUN_0271e300 reads it; +0x48 when unlimited) */
#define PRF_RES_EMAX OFS(PRF_RES_EMAX, 0x48)       /* ... float, its max */
#define PRF_RES_EUNLIM OFS(PRF_RES_EUNLIM, 0x60)     /* ... byte, the energy unlimited (BO_RES_UNLIM +0x61: the arrows') */
#define PR_CH_HEALTHC OFS(PR_CH_HEALTHC, 0x130)     /* CCharacter: the health component (+0x30 the max, +0x34 the health: PRF_HEAL) */
#define PR_CH_HEALTH_MAX OFS(PR_CH_HEALTH_MAX, 0x160)  /* CCharacter: float, the max health */
typedef void (*prf_tokens_fn)(uintptr_t res, int delta);
typedef void (*prf_energy_fn)(uintptr_t res, int unlimited);
typedef int (*prf_heal_fn)(uintptr_t health, float amount);
#define PRF_FN(i, T) ((T)g_prf_sites[i].target)
_Static_assert(sizeof g_prn_sites_game / sizeof g_prn_sites_game[0] == PRN_N, "practice nav sites");
static const struct site_spec *g_prn_sites = g_prn_sites_game; /* selftest: stand-ins */
#define PRN_LIST GADDR(PRN_LIST, 0x46b9200)
#define PRN_NODE OFS(PRN_NODE, 0x80)
#define PRN_REC OFS(PRN_REC, 0x298)
#define PRN_ACTIVE OFS(PRN_ACTIVE, 0x2e0) /* +0x2e1 enabled */
typedef void (*prn_pos_fn)(uintptr_t marker, float *out);
#define PRQ_MGR_FLAG OFS(PRQ_MGR_FLAG, 0x164)   /* the spawn manager: byte, its spawn queues run (FUN_02919d70/d80's store) */
#define PRQ_MGR_LIST OFS(PRQ_MGR_LIST, 0x48)    /* ... its ambient AI: begin, end (+8), 0x50 bytes each (FUN_02919d90) */
#define PRQ_ENTRY OFS(PRQ_ENTRY, 0x50)
typedef void (*prq_fn)(void);
_Static_assert(sizeof g_pr_sites_game / sizeof g_pr_sites_game[0] == PRS_N, "practice sites");
_Static_assert((int)SOMVR_PRA_GO == (int)SOMVR_VMPR_GO && (int)SOMVR_PRA_BACK == (int)SOMVR_VMPR_BACK &&
		       (int)SOMVR_PRA_SPAWN == (int)SOMVR_VMPR_SPAWN &&
		       (int)SOMVR_PRA_REMOVE == (int)SOMVR_VMPR_REMOVE && (int)SOMVR_PRA_ARROWS == (int)SOMVR_VMPR_ARROWS &&
		       (int)SOMVR_PRA_HEALTH == (int)SOMVR_VMPR_HEALTH && (int)SOMVR_PRA_FAR == (int)SOMVR_VMPR_FAR &&
		       (int)SOMVR_PRA_GROUP == (int)SOMVR_VMPR_GROUP && (int)SOMVR_PRA_CAPTAIN == (int)SOMVR_VMPR_CAPTAIN &&
		       (int)SOMVR_PRA_PLACE == (int)SOMVR_VMPR_PLACE && /* build 31 */
		       (int)SOMVR_PRA_CHARGE == (int)SOMVR_VMPR_CHARGE && /* build 36 */
		       (int)SOMVR_PRA_HERB == (int)SOMVR_VMPR_HERB && /* build 37 */
		       SOMVR_PRA_N <= 16 && SOMVR_PRO_N == 6 /* the menu's six labels (build 22) */,
	       "the menu's practice requests = the practice module's");
static const struct site_spec *g_pr_sites = g_pr_sites_game; /* selftest: stand-ins */
#define PR_FN(i, T) ((T)g_pr_sites[i].target)
#define PRQ_FN_CALL(i) (((prq_fn)g_prq_sites[i].target)())
#define PR_SLOT_RAY OFS(PR_SLOT_RAY, 0x200)       /* IEnginePhysics: the cast (vf_64) */
#define PR_SLOT_SETZONE OFS(PR_SLOT_SETZONE, 0x288)   /* NPC: the zone (vf_81) */
#define PR_SLOT_REMOVE OFS(PR_SLOT_REMOVE, 0x58)     /* the object system: the deferred removal (vf_11) */
#define PR_SLOT_WEATHER OFS(PR_SLOT_WEATHER, 0xa8)    /* CGameServices: the weather state (vf_21) */
#define PR_CH_MINHEALTH OFS(PR_CH_MINHEALTH, 0x9b4)   /* CCharacter: float, the min health (PR_SETMINHEALTH's store) */
#define PR_CH_HEALTH OFS(PR_CH_HEALTH, 0x164)      /* CCharacter: float, the health (the probe's AGGROWATCH lines; logs only) */
#define PR_CH_COMBAT OFS(PR_CH_COMBAT, 0x6d0)      /* CCharacter: its Combat* (re-combat.md 1.3) */
#define PR_C_CHARREC OFS(PR_C_CHARREC, 0x4c8)      /* Combat: the character record ('AI_Infantry', 'Warg_Brown', 'AI_Slave_Human': P7) */
#define PR_HOBJ_GAMEOBJ OFS(PR_HOBJ_GAMEOBJ, 0x38)    /* HOBJECT: its game object (the NPC) */
#define PR_HOBJ_CLASSES OFS(PR_HOBJ_CLASSES, 0x48)    /* HOBJECT: its class list (first *(+0x48), next node +8) */
#define PR_HOBJ_FLAGS OFS(PR_HOBJ_FLAGS, 0x5c)      /* HOBJECT: byte, 0x08 queued for removal, 0x10 being deleted */
#define PR_W_RECORD OFS(PR_W_RECORD, 0x60)        /* the weather state: the weather record */
#define PR_W_TOD OFS(PR_W_TOD, 0x68)           /* ... the TOD byte (FUN_025ccd10's argument) */
#define PR_BANDS_MAX 48         /* entries read per targeting band */
#define PR_LIGHT_WEATHER_S 0.6  /* after the arrival: the weather this much after the TOD step (both under the black) */
#define PR_LIGHT_LATER_S 10.0   /* build 36: the weather's record read again this long after our ChangeWeather (its reset
				 * comes ~3 s after it; the record read at +1.5 s still names the old weather) */
#define PR_PAUSE_WAIT_S 3.6     /* SOMVR_PRACTICE_LOADHOLD: our ChangeWeather's pause comes this soon (runs 154417 .. 163004:
				 * 3.05 s after it, ~1.1 s long, every trip there; none after the trip back's) */
#define PR_MH_CHECK_S 1.0       /* the min health checked (set again if the game changed it) this often */
#define PR_WAIT_CLEAR_S 1.0     /* SOMVR_PRACTICE_WAIT: a waiting trip starts once the game has been back this long */
typedef uintptr_t (*pr_byname_fn)(const char *name);
typedef uintptr_t (*pr_spawn_fn)(uintptr_t ctx, const char *name, const float *xf, uintptr_t tmpl, int a5, int a6,
				 uintptr_t a7, uintptr_t fm, uintptr_t a9);
typedef uintptr_t (*pr_zone_fn)(const float *pos, uintptr_t a, uintptr_t b);
typedef void (*pr_setzone_fn)(uintptr_t npc, uintptr_t zone, int x);
typedef void (*pr_level_fn)(uintptr_t combat, uint32_t level);
typedef void (*pr_minhealth_fn)(uintptr_t ch, float v);
typedef uintptr_t (*pr_todname_fn)(uint32_t tod);
typedef uint64_t (*pr_ray_fn)(uintptr_t phys, void *q, void *r);
typedef uint64_t (*pr_remove_fn)(uintptr_t objsys, uintptr_t hobj);
typedef uintptr_t (*pr_vget_fn)(uintptr_t self);

/* the game objects (selftest: stand-ins): the object system, IEnginePhysics and IGameServices.Default holders, the
 * NPC class in an HOBJECT's class list, the debug spawner's level for non-members, the CPlayerObj vtable */
static struct {
	uintptr_t objsys_pp, phys_pp, gsvc_pp, class_npc, spawn_level, vt_playerobj, spawnmgr_pp;
} g_prg = { 0x466afa0, 0x466c758, 0x46b7310, 0x4c52780, 0x4c471cc, 0x39959a0, 0x4c4f7c0 };

enum { PR_TRIP_GO = 0, PR_TRIP_BACK = 1, PR_TRIP_DOWN = 2 }; /* DOWN: SOMVR_CLIMB_DESCENT's blink (pr_down_start) */
enum { PR_DAY_OFF = 0, PR_DAY_WEATHER = 1, PR_DAY_FULL = 2 }; /* SOMVR_PRACTICE_DAY=0|weather|day */
struct pr_me {
	uintptr_t pl, obj, pcomb, pc, ch, mount;
	uintptr_t pm;        /* build 37: the PlayerMovement (SOMVR_PRACTICE_GROUND_HOLD=grav's gravity flag) */
	float pos[3], rot[4];
	int life;            /* FB_P_LIFE: 2 = alive in the world; -1 = not read */
};
static struct {
	int want;            /* SOMVR_PRACTICE set (pr_start) */
	int on;              /* ... and the console: the practice runs */
	int mode, clear, day, def_orc;
	int ok_mh, ok_spawn, ok_remove, ok_light, ok_gate, ok_mount, ok_life, ok_clear, ok_ray;
	/* build 17: the refills (PRF_* sites) and their counts; the test aids `practice hurt HP` / `practice focus F` */
	int ok_tokens, ok_energy, ok_heal;
	uint64_t fill_arrows, fill_health, fill_refused, aid_hurt, aid_focus, fill_charge; /* (build 36: Charge execution) */
	int charge_orc; /* build 37: SOMVR_PRACTICE_CHARGE_ORC - Charge execution with no orc near spawns one facing Talion */
	/* SOMVR_PRACTICE_QUIET: the ambient AI spawn manager off while in the area */
	int quiet_mode, ok_quiet;
	int quiet;           /* we hold it off now */
	int quiet_was;       /* its flag before (given back only if it was on) */
	uintptr_t quiet_mgr; /* the manager we turned off (a load may make another) */
	uint64_t quiet_on, quiet_off, quiet_again, quiet_removed;
	/* SOMVR_PRACTICE_WAIT (the drill, handoff/HANDOFF-drill.md): a trip asked for while the game is paused, Talion is
	 * dead or a scripted moment runs waits for the game to be back (else its black hides the pause menu or the Nemesis
	 * board, and its teleport lands in the respawn's presentation) */
	int wait_mode;
	int pend_act, pend_orc; /* the waiting request (SOMVR_PRA_GO/BACK; 0 = none) */
	const char *pend_who;
	double pend_t, pend_clear_t;
	uint64_t waits, wait_starts, wait_dropped;
	/* SOMVR_PRACTICE_LOADHOLD: the game's pause ~3 s after our ChangeWeather (a trip there) comes under the black */
	int load_mode;
	double pw_t;         /* our ChangeWeather of this trip there (0 = no pause expected) */
	int pw_seen;         /* ... the game's pause came */
	double pw_seen_t;
	uint64_t pw_waits, pw_pauses, pw_none, pw_open, pw_dry;
	double pw_after_max, pw_len_max;
	float spot[3], radius_cm, clear_cm, ahead_cm, auto_s, minhealth;
	float herb_m;        /* build 37: SOMVR_PRACTICE_HERB_M, New herb's reach */
	float edge_cm; /* build 36 (SOMVR_PRACTICE_EDGE_M): out only past radius + this; back in within the radius */
	char weather[64];    /* SOMVR_PRACTICE_WEATHER */
	int sky;             /* SOMVR_PRACTICE_SKY (build 36): SOMVR_PRSKY_DRY|CLEAR|ALWAYS - which day weathers are changed */
	int light_tp;        /* the weather went with this trip's teleport (pr_light_teleport) */
	somvr_pr_tcfg tcfg;
	somvr_pr_tcfg tcfg_down; /* the climb's blink down: tcfg, a short hold (no light to change) */
	int down_tp;         /* the blink down's teleport sent (pr_down_teleported takes it) */
	int trip_tp;         /* any other trip's teleport sent (pr_trip_teleported takes it: a climb ends there) */
	int down_quick;      /* this blink down is the pullover's quick one (tcfg_quick) */
	int down_ws;         /* build 18: ... it is the wraith step's (SOMVR_WRAITH_STEP; the arrival's line) */
	somvr_pr_tcfg tcfg_quick; /* build 16, SOMVR_CLIMB_PULLOVER: a mantle's blink (0.12 s out, 0.1 s settle, no hold, 0.2 s in) */
	int tid;
	uintptr_t pl;        /* the Player* last seen (another one: a load) */
	somvr_pr_trip trip;
	int trip_kind, trip_first_tp;
	/* build 37 (SOMVR_PRACTICE_GROUND, re-xr-pipeline.md 27.10): the world under Talion during a trip */
	int ground_on;       /* the trip's ground rules on (default 1) */
	int ground_last;     /* SOMVR_PRGD_* at the last step */
	int ground_said;     /* this trip's wait logged (1) and its end (2) */
	int ground_back;     /* this trip back is the one a trip there without ground started */
	double ground_y;     /* the last hit's height */
	double trip_end_t;   /* the last trip's end (pr_settling: the fall rules wait PR_SETTLE_S after it) */
	double ground_probe_t; /* the wait's next probe of the target and the place he left */
	uint64_t ground_waits, ground_pins, ground_none, ground_backs;
	double ground_wait_max;
	int ground_hold;     /* SOMVR_PRACTICE_GROUND_HOLD: 0 pin (teleports keep him at the target), 1 grav (his gravity off) */
	int grav_off;        /* ... we turned it off (on the PlayerMovement grav_pm) */
	uintptr_t grav_pm;
	double grav_t;
	uint64_t grav_holds, grav_frames;
	int went;            /* a trip there arrived, and no trip back since */
	int here;            /* ... and Talion within the area's radius now */
	int have_back;
	float back[3];       /* where he was when he went (the root, cm) */
	int auto_done;
	double world_t;      /* auto: in the world since (0 = not) */
	/* the min health */
	int mh_on;
	uintptr_t mh_ch;
	float mh_old;
	double mh_next;
	/* the light */
	int advanced;        /* our AdvanceTimeOfDay made the day (the trip back advances again) */
	int weather_set;     /* our ChangeWeather is on (the trip back restores orig) */
	char orig_weather[64], orig_tod[32];
	int light_step;      /* 1: the weather is due at light_t + PR_LIGHT_WEATHER_S; 2: its log line at light_t + 1.5;
			      * 3 (build 36): the weather read again at light_t + PR_LIGHT_LATER_S (the game's reset done) */
	double light_t;
	/* our orc */
	uintptr_t orc_h, orc_npc, orc_c;
	int orc_type, orc_dead, orc_checks;
	/* build 18: the far orc (SOMVR_PRA_FAR): SOMVR_PRACTICE_FAR_CM ahead, its back to Talion; the watch of its target
	 * and its place (pr_far_tick) */
	float far_cm, far_pos0[3], far_dist0;
	int far, far_aware, far_logs;
	double far_next, far_aware_t;
	uint64_t far_spawns, far_noticed;
	double orc_t;
	int cur_orc;         /* the type "practice spawn" without one spawns */
	/* requests */
	uint32_t trig_seen, menu_seen;
	double next_clear;
	uint32_t gate_last;
	/* counts */
	int group_n;         /* build 19: SOMVR_PRACTICE_GROUP_N, the group's size (3, 4) */
	uint64_t groups;
	uint64_t frames, other, trips[3], arrived, failed, teleports, spawns, spawn_fail, removes, killed, cleared, passes,
		held, mh_sets, mh_restores, mh_reasserts, advances, weathers, restores, req_trig, req_menu, refused, autos,
		walked_out;
	/* logs */
	int lines, lsec_n;
	double lsec;
	uint64_t suppressed;
} g_pr;
static _Atomic uint32_t g_pr_trig; /* the trigger word (writer thread): seq << 8 | (orc + 1) << 4 | action */
static _Atomic uint32_t g_pr_at_req[3]; /* "practice go X Y Z": the spot's floats (published by g_pr_trig's release) */
static char g_pr_wx[64];                /* build 36's test word "practice weather DEF" (published by g_pr_wx_pend) */
static _Atomic int g_pr_wx_pend;
static _Atomic int g_pr_at_has;
static double g_pr_test_t = -1;    /* selftest: the clock (-1 = CLOCK_MONOTONIC) */
static double pr_now(void) { return g_pr_test_t >= 0 ? g_pr_test_t : mono(); }

/* PRACTICE lines: the first 200, then at most 8 per second */
static int pr_log_ok(double t)
{
	if (g_pr.lines < 200)
		return ++g_pr.lines;
	if (t >= g_pr.lsec + 1.0)
		g_pr.lsec = t, g_pr.lsec_n = 0;
	if (g_pr.lsec_n < 8)
		return ++g_pr.lsec_n;
	g_pr.suppressed++;
	return 0;
}

/* the object's vtable is the one the K_VSLOT site checked (the site's slot = its vtable + off) */
static int pr_slot_ok(uintptr_t obj, int site, uintptr_t off)
{
	uintptr_t vt = obj ? vptr_of((void *)obj) : 0;
	return vt && vt + off == g_pr_sites[site].addr && peek_ptr(vt + off) == g_pr_sites[site].target;
}

/* ---- reading the game ---------------------------------------------------------------------------------------- */
static int pr_me(uintptr_t pl, struct pr_me *m)
{
	uintptr_t pm = 0;
	memset(m, 0, sizeof *m);
	m->pl = pl, m->life = -1;
	if (!pl || !player_objs(pl, &pm, &m->obj) || !peek(m->obj + OBJ_POS, m->pos, sizeof m->pos) ||
	    !peek(m->obj + OBJ_ROT, m->rot, sizeof m->rot) || !vec_ok(m->pos, 1e7f))
		return 0;
	m->pm = pm;
	m->pcomb = peek_ptr(pl + P_COMBAT);
	m->pc = m->pcomb ? peek_ptr(m->pcomb + PC_SERVER) : 0;
	if (m->pc && vptr_of((void *)m->pc) != g_cbg.vt_combat)
		m->pc = 0;
	m->ch = m->pc ? peek_ptr(m->pc + C_OWNER) : 0;
	if (m->ch && vptr_of((void *)m->ch) != g_prg.vt_playerobj)
		m->ch = 0;
	uint8_t b = 0;
	if (g_pr.ok_mount)
		m->mount = peek_ptr(pl + FB_P_MOUNT);
	if (g_pr.ok_life && peek(pl + FB_P_LIFE, &b, 1))
		m->life = b;
	return 1;
}

/* the scripted-moment gate (SOMVR_PRG_*): a presentation (a Nemesis showdown), a cinematic or scripted camera, Talion
 * in a sync, the pause; UNKNOWN without the fallback's sites */
static uint32_t pr_gate(const struct pr_me *m)
{
	uint32_t g = game_paused() ? SOMVR_PRG_PAUSED : 0;
	if (!g_pr.ok_gate)
		return g | SOMVR_PRG_UNKNOWN;
	uintptr_t sh = peek_ptr(g_ing.shell_pp), cm = sh ? peek_ptr(sh + FB_SH_CINE) : 0;
	if (cm && peek_ptr(cm + FB_CINE_CAM))
		g |= SOMVR_PRG_CINE;
	if (!m->pl)
		return g;
	uintptr_t pres = peek_ptr(m->pl + FB_P_PRES), cam = peek_ptr(m->pl + FB_P_CAM);
	uint8_t b = 0;
	if (pres && peek_ptr(pres + FB_PRES_CUR))
		g |= SOMVR_PRG_PRES;
	if (cam && peek(cam + FB_CAM_SCRIPTED, &b, 1) && b)
		g |= SOMVR_PRG_SCRIPTED;
	uintptr_t sy = m->pc ? peek_ptr(m->pc + FB_C_SYNC) : 0, rec = sy ? peek_ptr(sy + FB_SYNC_REC) : 0;
	/* build 17 (the strike17 helper): a record the fallback found stale (Talion's part over, the game still holding it:
	 * re-xr-pipeline.md 30.2) is no sync - after a Shadow Strike it stayed set a minute and more (runs 101013, 102126,
	 * 111356), and every trip waited */
	if (rec && !(g_fb_sa.stale && g_fb_sa.rec == rec))
		g |= SOMVR_PRG_SYNC;
	return g;
}

/* the weather state (CGameServices::vf_21's member, read as the checked slot's `mov rax, [rdi+0x6c80]`: the call
 * itself, through the checked slot); its record's name and the TOD's name. 0 = not read */
static uintptr_t pr_weather(char *rec, size_t rcap, char *tod, size_t tcap)
{
	rec[0] = tod[0] = 0;
	uintptr_t g = g_pr.ok_light ? peek_ptr(g_prg.gsvc_pp) : 0;
	if (!g || !pr_slot_ok(g, PRS_WEATHER, PR_SLOT_WEATHER))
		return 0;
	uintptr_t w = PR_FN(PRS_WEATHER, pr_vget_fn)(g);
	uint8_t b = 0;
	if (!w || !peek(w + PR_W_TOD, &b, 1))
		return 0;
	fb_rec_name(peek_ptr(w + PR_W_RECORD), rec, rcap);
	rx_str(PR_FN(PRS_TODNAME, pr_todname_fn)(b), tod, tcap);
	return w;
}

/* ---- the console ------------------------------------------------------------------------------------------------ */
static int pr_console(const char *cmd, double t)
{
	char why[256] = "";
	int rc = es_run(cmd, why, sizeof why);
	if (pr_log_ok(t))
		logf_("PRACTICE t=%.3f console '%s'%s%s", t, cmd, rc ? ": not run: " : "", rc ? why : "");
	return rc;
}

/* ---- the min health ------------------------------------------------------------------------------------------- */
static void pr_mh(const struct pr_me *m, int want, double t)
{
	if (!g_pr.ok_mh || !(g_pr.minhealth > 0))
		return;
	float cur = 0, hp = 0;
	if (want && m->ch && (!g_pr.mh_on || g_pr.mh_ch != m->ch || t >= g_pr.mh_next)) {
		g_pr.mh_next = t + PR_MH_CHECK_S;
		if (!peek(m->ch + PR_CH_MINHEALTH, &cur, sizeof cur))
			return;
		if (g_pr.mh_on && g_pr.mh_ch == m->ch && cur == g_pr.minhealth)
			return;
		int again = g_pr.mh_on && g_pr.mh_ch == m->ch;
		if (!again)
			g_pr.mh_old = cur, g_pr.mh_ch = m->ch;
		PR_FN(PRS_MINHEALTH, pr_minhealth_fn)(m->ch, g_pr.minhealth);
		g_pr.mh_on = 1;
		again ? g_pr.mh_reasserts++ : g_pr.mh_sets++;
		peek(m->ch + PR_CH_HEALTH, &hp, sizeof hp);
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f min health %g (%s %g): Talion's health %.1f cannot be taken below it", t,
			      (double)g_pr.minhealth, again ? "the game had set it to" : "was", (double)(again ? cur : g_pr.mh_old),
			      (double)hp);
	} else if (!want && g_pr.mh_on) {
		g_pr.mh_on = 0;
		if (m->ch && m->ch == g_pr.mh_ch) { /* never the min health of another (an old) character */
			PR_FN(PRS_MINHEALTH, pr_minhealth_fn)(m->ch, g_pr.mh_old);
			g_pr.mh_restores++;
			peek(m->ch + PR_CH_HEALTH, &hp, sizeof hp);
			if (pr_log_ok(t))
				logf_("PRACTICE t=%.3f min health back to %g (health %.1f)", t, (double)g_pr.mh_old, (double)hp);
		}
	}
}

/* ---- our orc ---------------------------------------------------------------------------------------------------- */
static int pr_orc_alive(void)
{
	uint8_t fl = 0x18;
	return g_pr.orc_h && peek(g_pr.orc_h + PR_HOBJ_FLAGS, &fl, 1) && !(fl & 0x18) &&
	       peek_ptr(g_pr.orc_h + PR_HOBJ_GAMEOBJ) == g_pr.orc_npc && vptr_of((void *)g_pr.orc_npc) == g_cbg.vt_npc;
}

/* the object system's deferred removal of h (its game object `npc`, checked first); 1 = queued */
static int pr_remove_obj(uintptr_t h, uintptr_t npc)
{
	uintptr_t os = g_pr.ok_remove ? peek_ptr(g_prg.objsys_pp) : 0;
	uint8_t fl = 0x18;
	if (!os || !pr_slot_ok(os, PRS_REMOVE, PR_SLOT_REMOVE) || !h || !peek(h + PR_HOBJ_FLAGS, &fl, 1) || (fl & 0x18) ||
	    peek_ptr(h + PR_HOBJ_GAMEOBJ) != npc)
		return 0;
	PR_FN(PRS_REMOVE, pr_remove_fn)(os, h);
	return 1;
}

/* a test (SOMVR_FRONT's word "front keep 1", headless): a new spawn keeps the previous orc (several at once: the fight
 * circle's turns, re-xr-pipeline.md 42.10); kept orcs are ours for the keep-clear pass; removed with the next removal
 * after "front keep 0" */
static struct { int on, n; uintptr_t h[8], npc[8]; } g_pr_keep;
/* build 19: the practice page's "Spawn group" (re-xr-pipeline.md 70): 3-4 orcs of mixed types around Talion; ours
 * like the single orc (the keep-clear pass leaves them, Remove and a new spawn remove them) */
static struct {
	int n, alive;
	uintptr_t h[SOMVR_PR_GROUP_MAX], npc[SOMVR_PR_GROUP_MAX], c[SOMVR_PR_GROUP_MAX];
	int type[SOMVR_PR_GROUP_MAX], down[SOMVR_PR_GROUP_MAX], gone[SOMVR_PR_GROUP_MAX];
	double t;
} g_pr_grp;
static int pr_kept(uintptr_t h)
{
	for (int i = 0; i < g_pr_keep.n; i++)
		if (g_pr_keep.h[i] == h)
			return 1;
	for (int i = 0; i < g_pr_grp.n; i++)
		if (g_pr_grp.h[i] == h)
			return 1;
	return 0;
}
static int pr_obj_alive(uintptr_t h, uintptr_t npc) /* pr_orc_alive's test for any of ours */
{
	uint8_t fl = 0x18;
	return h && peek(h + PR_HOBJ_FLAGS, &fl, 1) && !(fl & 0x18) && peek_ptr(h + PR_HOBJ_GAMEOBJ) == npc &&
	       vptr_of((void *)npc) == g_cbg.vt_npc;
}

static void pr_remove_orc(const char *why, double t)
{
	for (int i = 0; !g_pr_keep.on && i < g_pr_keep.n; i++) { /* the kept ones go with the next removal */
		int done = pr_remove_obj(g_pr_keep.h[i], g_pr_keep.npc[i]);
		g_pr.removes += done;
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f remove kept orc %d (%s) hobj=%#llx: %s", t, i, why, (unsigned long long)g_pr_keep.h[i],
			      done ? "removal queued" : "NOT removed (gone?)");
	}
	if (!g_pr_keep.on)
		g_pr_keep.n = 0;
	for (int i = 0; i < g_pr_grp.n; i++) { /* build 19: the group goes with every removal */
		int done = !g_pr_grp.gone[i] && pr_remove_obj(g_pr_grp.h[i], g_pr_grp.npc[i]);
		g_pr.removes += done;
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f remove the group's %s orc %d (%s) hobj=%#llx after %.1f s: %s", t,
			      somvr_pr_orc_label(g_pr_grp.type[i]), i, why, (unsigned long long)g_pr_grp.h[i], t - g_pr_grp.t,
			      done ? "removal queued" : "already gone");
	}
	g_pr_grp.n = g_pr_grp.alive = 0;
	if (!g_pr.orc_h)
		return;
	int alive = pr_orc_alive(), done = alive && pr_remove_obj(g_pr.orc_h, g_pr.orc_npc);
	g_pr.removes += done;
	if (pr_log_ok(t))
		logf_("PRACTICE t=%.3f remove the %s orc (%s) hobj=%#llx after %.1f s: %s", t, somvr_pr_orc_label(g_pr.orc_type), why,
		      (unsigned long long)g_pr.orc_h, t - g_pr.orc_t, done ? "removal queued" : alive ? "NOT removed (the object "
		      "system's slot or the object did not check out)" : "already gone");
	g_pr.orc_h = g_pr.orc_npc = g_pr.orc_c = 0, g_pr.orc_dead = 0;
}

/* a ray straight down from `from` for len cm, the debug spawn's G21 cast (flags 0x50, filter 0xc); a hit on `self`
 * (Talion's object, 0 = none) goes on from 5 cm below it (at most 4 times); 1 = a hit (*hit, *hobj = the hit object or
 * 0). The game thread; needs PR_RAYCAST's site (SOMVR_PRACTICE's start checked it: g_pr.ok_ray) */
static int pr_ray_down_r(const float from[3], float len, uintptr_t self, float hit[3], uintptr_t *hobj, unsigned char *rout);
static int pr_ray_down(const float from[3], float len, uintptr_t self, float hit[3], uintptr_t *hobj)
{
	return pr_ray_down_r(from, len, self, hit, hobj, NULL);
}
/* (build 22: rout = the hit's record, 0x80 bytes, for its surface: SOMVR_HANDS_SURFACE_GROUND) */
static int pr_ray_down_r(const float from[3], float len, uintptr_t self, float hit[3], uintptr_t *hobj, unsigned char *rout)
{
	uintptr_t phys = peek_ptr(g_prg.phys_pp);
	static int said; /* winport: why a ray fails, once (the first Windows practice spawn's "the ray down hit nothing") */
	if (!phys || !pr_slot_ok(phys, PRS_RAY, PR_SLOT_RAY)) {
		if (!said++)
			logf_("PRACTICE ray: not cast: IEnginePhysics %#llx (holder %#llx), vtable %#llx + %#llx vs the site %#llx -> %#llx (live %#llx)",
			      (unsigned long long)phys, (unsigned long long)g_prg.phys_pp, (unsigned long long)(phys ? vptr_of((void *)phys) : 0),
			      (unsigned long long)PR_SLOT_RAY, (unsigned long long)g_pr_sites[PRS_RAY].addr,
			      (unsigned long long)g_pr_sites[PRS_RAY].target,
			      (unsigned long long)(phys ? peek_ptr(vptr_of((void *)phys) + PR_SLOT_RAY) : 0));
		return 0;
	}
	float y0 = from[1];
	for (int k = 0; k < 5; k++) {
		unsigned char q[0x40] __attribute__((aligned(16))), r[0x80] __attribute__((aligned(16)));
		float f[3] = { from[0], y0, from[2] }, to[3] = { from[0], from[1] - len, from[2] };
		uint32_t fl = 0x50, flt = 0xc;
		uint16_t none = 0xffff;
		if (!(y0 > to[1]))
			return 0;
		memset(q, 0, sizeof q), memset(r, 0, sizeof r);
		memcpy(q, f, 12), memcpy(q + 12, to, 12), memcpy(q + 0x18, &fl, 4), memcpy(q + 0x1c, &flt, 4);
		memcpy(r + 0x28, &none, 2);
		if (!(PR_FN(PRS_RAY, pr_ray_fn)(phys, q, r) & 0xff)) {
			if (!said++)
				logf_("PRACTICE ray: cast from %.0f,%.0f,%.0f down %.0f cm: no hit (the call returned 0)", (double)f[0],
				      (double)f[1], (double)f[2], (double)len);
			return 0;
		}
		memcpy(hit, r, 12), memcpy(hobj, r + 0x20, 8);
		if (!vec_ok(hit, 1e7f))
			return 0;
		if (!self || *hobj != self) {
			if (rout)
				memcpy(rout, r, 0x80);
			return 1;
		}
		y0 = hit[1] - 5.0f; /* Talion himself: on below him */
	}
	return 0;
}

/* the anchor's SIZE / EYEH lines may cast (the practice's sites checked at start) */
static int pr_ray_ok(void) { return g_pr.on && g_pr.ok_ray; }

/* a ray straight down through `at` (2 m above it to 6 m below): the spawn's ground */
static int pr_ground(const float at[3], float hit[3], uintptr_t *hobj)
{
	float from[3] = { at[0], at[1] + 200.0f, at[2] };
	return pr_ray_down(from, 800.0f, 0, hit, hobj);
}

/* build 37 (SOMVR_PRACTICE_GROUND): the world under Talion during a trip (SOMVR_PRGD_*): a ray straight down from 40 cm
 * over his root (his own body stepped past, as the eye line's ray) for PR_GROUND_RAY_CM; ON = the root at most
 * PR_GROUND_ON_CM over the hit (he stands about 103 cm over it). Nothing within the ray = the world's collision has not
 * streamed in there yet (a 461 m trip: boot win-session-20261104-160647 fell through the world after 3 teleports) or he
 * is under it. UNKNOWN: the rules off, no ray, no Talion */
#define PR_GROUND_RAY_CM 4000.0f
#define PR_GROUND_ON_CM 250.0f
#define PR_SETTLE_S 2.0 /* the fall rules (SOMVR_WALK_GROUND) wait this long after a trip ends */
static int pr_trip_ground(const struct pr_me *m, int have)
{
	if (!g_pr.ground_on || !have || !pr_ray_ok())
		return SOMVR_PRGD_UNKNOWN;
	float from[3] = { m->pos[0], m->pos[1] + 40.0f, m->pos[2] }, hit[3];
	uintptr_t ho = 0;
	if (!pr_ray_down(from, PR_GROUND_RAY_CM + 40.0f, m->obj, hit, &ho))
		return SOMVR_PRGD_NONE;
	g_pr.ground_y = hit[1];
	return m->pos[1] - hit[1] <= PR_GROUND_ON_CM ? SOMVR_PRGD_ON : SOMVR_PRGD_OVER;
}

/* build 37: a trip runs, or one ended under PR_SETTLE_S ago: the world may still be streaming in under Talion - the fall
 * rules (somvr.c's un_ground_guard, SOMVR_WALK_GROUND) leave him to the trip */
static int pr_settling(void)
{
	if (!g_pr.on)
		return 0;
	return g_pr.trip.phase != SOMVR_PRT_IDLE || (g_pr.trip_end_t > 0 && pr_now() - g_pr.trip_end_t < PR_SETTLE_S);
}

/* ---- SOMVR_PRACTICE_QUIET: the ambient AI spawn manager off while Talion is in the area ------------------------ */
/* its flag and the length of its ambient AI list; 0 = not readable */
static int prq_state(int *on, uint32_t *n, uintptr_t *mgr_out)
{
	uintptr_t mgr = peek_ptr(g_prg.spawnmgr_pp), b = 0, e = 0;
	uint8_t f = 0;
	if (mgr_out)
		*mgr_out = mgr;
	if (!mgr || !peek(mgr + PRQ_MGR_FLAG, &f, 1))
		return 0;
	*on = f != 0;
	if (n) {
		b = peek_ptr(mgr + PRQ_MGR_LIST), e = peek_ptr(mgr + PRQ_MGR_LIST + 8);
		*n = e >= b && (e - b) % PRQ_ENTRY == 0 && e - b < PRQ_ENTRY * 100000u ? (uint32_t)((e - b) / PRQ_ENTRY) : 0;
	}
	return 1;
}

/* the pass in the area (every second, the gate open): off and its AI removed the first time; again if the game turned
 * it back on (a script) */
static void pr_quiet_tick(double t, const char *why)
{
	if (!g_pr.quiet_mode || !g_pr.ok_quiet)
		return;
	int on = 0;
	uint32_t n = 0;
	uintptr_t mgr = 0;
	if (!prq_state(&on, &n, &mgr)) {
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f quiet: the spawn manager *%#llx is not readable now (nothing changed)", t,
			      (unsigned long long)g_prg.spawnmgr_pp);
		return;
	}
	if (g_pr.quiet && (!on || mgr != g_pr.quiet_mgr))
		return; /* still off (or another manager: a load, handled by pr_new_player) */
	int again = g_pr.quiet;
	if (!again)
		g_pr.quiet_was = on, g_pr.quiet_mgr = mgr;
	PRQ_FN_CALL(PRQ_DISABLE);
	PRQ_FN_CALL(PRQ_REMOVEALL);
	g_pr.quiet = 1;
	g_pr.quiet_on += !again, g_pr.quiet_again += again, g_pr.quiet_removed += n;
	int now = 1;
	prq_state(&now, NULL, NULL);
	if (pr_log_ok(t))
		logf_("PRACTICE t=%.3f quiet (%s): the ambient AI spawn manager %#llx %s (it was %s, now %s); its %u ambient AI "
		      "removed (AISpawnManager.Disable and .RemoveAllAmbientAI's calls): no new patrols, camps or barks here", t,
		      again ? "the game had turned it on again" : why, (unsigned long long)mgr, again ? "off again" : "off",
		      on ? "on" : "off", now ? "on (!)" : "off", n);
}

/* walking out, the trip back, a load: the manager as it was (on again only if it was on and is still ours) */
static void pr_quiet_off(double t, const char *why)
{
	if (!g_pr.quiet)
		return;
	g_pr.quiet = 0, g_pr.quiet_off++;
	int on = 0;
	uintptr_t mgr = 0;
	int ok = prq_state(&on, NULL, &mgr), same = ok && mgr == g_pr.quiet_mgr;
	if (same && g_pr.quiet_was && !on)
		PRQ_FN_CALL(PRQ_ENABLE);
	int now = 0;
	prq_state(&now, NULL, NULL);
	if (pr_log_ok(t))
		logf_("PRACTICE t=%.3f quiet ends (%s): the spawn manager %s", t, why,
		      !ok ? "not readable (left as it is)" : !same ? "is another one now (a load: left as it is)" :
		      !g_pr.quiet_was ? "left off (it was off before)" : on ? "already on (the game's)" :
		      now ? "on again" : "NOT on again (!)");
}

static char g_pr_tmpl_test[64]; /* a test (SOMVR_VERBS' "verbs template NAME"): the next spawns' AI/Templates name */

/* one orc of type `type` ahead of Talion, facing him (re-combat-spawn.md 1.2 steps 1-4 as P9's `spawn`); the previous
 * one removed first */
/* build 18: the far orc's place - ahead along Talion's facing at far_cm, or 3 m nearer, 2 m farther, 6 m nearer, each
 * straight ahead then 15 deg either side: the first with ground under it within the ray's window (2 m over .. 6 m
 * under Talion's root) */
static int pr_far_spot(const struct pr_me *m, const float fwd[3], float at[3], float hit[3], uintptr_t *ground,
		       float *dist)
{
	static const float dd[] = { 0, -300, 200, -600 }, yaw[] = { 0, 0.2617994f, -0.2617994f };
	for (size_t i = 0; i < sizeof dd / sizeof dd[0]; i++)
		for (size_t j = 0; j < sizeof yaw / sizeof yaw[0]; j++) {
			float c = cosf(yaw[j]), sn = sinf(yaw[j]);
			float f[3] = { fwd[0] * c + fwd[2] * sn, 0, -fwd[0] * sn + fwd[2] * c };
			*dist = g_pr.far_cm + dd[i];
			somvr_pr_ahead(m->pos, f, *dist, at);
			if (pr_ground(at, hit, ground))
				return 1;
		}
	return 0;
}

/* build 31 (re-xr-pipeline.md 136): Place orc's spot for pr_spawn_at(.., far = 2); b35 (160): `practice put`'s
 * (far = 3: the spot given, the orc facing Talion - a replay's orcs where the recording had them) */
#define PR_CHARGE_NEAR_CM 400.0f /* build 37: Charge execution's orc - none this near: one */
#define PR_CHARGE_AT_CM 200.0f   /* ... this far ahead of the eyes, facing Talion */
static float g_pr_place_at[3];
static _Atomic uint32_t g_pr_put_seq, g_pr_put_at[3], g_pr_put_type; /* b35: the word -> the next pr_frame */
static void pr_spawn_at(const struct pr_me *m, int type, double t, const char *who, int far)
{
	const char *fail = NULL;
	char tn[96] = "";
	if (!g_pr.ok_spawn)
		fail = "the spawn's sites did not check out at start";
	else if (!g_pr.here)
		fail = "not in the practice area (go there first)";
	else if (g_pr.trip.phase != SOMVR_PRT_IDLE)
		fail = "a trip is on";
	if (fail) {
		g_pr.refused++;
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f spawn %s (%s) refused: %s", t, somvr_pr_orc_label(type), who, fail);
		return;
	}
	if (g_pr_keep.on && g_pr.orc_h && pr_orc_alive() && g_pr_keep.n < 8) { /* a test: the previous orc stays */
		g_pr_keep.h[g_pr_keep.n] = g_pr.orc_h, g_pr_keep.npc[g_pr_keep.n++] = g_pr.orc_npc;
		g_pr.orc_h = g_pr.orc_npc = g_pr.orc_c = 0;
	} else {
		pr_remove_orc("a new spawn", t);
	}
	const char *name = g_pr_tmpl_test[0] ? g_pr_tmpl_test : somvr_pr_orc_template(type); /* (a test: SOMVR_VERBS) */
	uintptr_t tmpl = PR_FN(PRS_BYNAME, pr_byname_fn)(name);
	if (tmpl)
		fb_rec_name(tmpl, tn, sizeof tn);
	sv_vec3 f = sv_qrot(sv_qnormalize(sv_quat_of(m->rot)), (sv_vec3){ 0, 0, 1 });
	float fwd[3] = { f.x, f.y, f.z }, at[3], hit[3] = { 0, 0, 0 }, q[4], hp[3] = { 0, 0, 0 }, dist = g_pr.ahead_cm;
	uintptr_t ground = 0, ctx = 0, ret = 0, h = 0, npc = 0, c = 0, zone = 0;
	uint32_t lvl = 0;
	somvr_pr_ahead(m->pos, fwd, g_pr.ahead_cm, at);
	if (!tmpl)
		fail = "no such AI/Templates record (by name)";
	else if (far >= 2 ? !pr_ground(g_pr_place_at, hit, &ground)
		 : far ? !pr_far_spot(m, fwd, at, hit, &ground, &dist) : !pr_ground(at, hit, &ground))
		fail = far >= 2 ? "the ray down at the placed spot hit nothing"
		       : far ? "no ground ahead at SOMVR_PRACTICE_FAR_CM -6..+2 m (the ray down from 2 m over Talion's root to 6 m "
			     "under it, 3 directions)"
			   : "the ray down hit nothing (no ground within 6 m below the spot ahead)";
	else if (!(ctx = ground ? ground : m->pc ? peek_ptr(m->pc + C_HOBJ) : 0))
		fail = "no context object (neither the ground's nor Talion's)";
	if (far >= 2 && !fail) /* build 31: Place orc - its distance (the line's "ahead"); b35: `practice put` */
		dist = dist3(hit, m->pos);
	if (!fail) {
		/* the far orc turns its back to Talion: it faces the point as far beyond it (its eyes away from him) */
		float away[3] = { 2 * hit[0] - m->pos[0], hit[1], 2 * hit[2] - m->pos[2] };
		somvr_pr_facing(hit, far == 1 || far == 2 ? away : m->pos, q); /* b35: `practice put` (3) faces Talion */
		float xf[7] = { hit[0], hit[1] + 10.0f, hit[2], q[0], q[1], q[2], q[3] };
		ret = PR_FN(PRS_SPAWN, pr_spawn_fn)(ctx, "SomVR_Practice", xf, tmpl, 1, 1, 0, 0, 0);
		h = ret ? peek_ptr(ret + 8) : 0;
		for (uintptr_t cls = h ? peek_ptr(peek_ptr(h + PR_HOBJ_CLASSES)) : 0, k = 0; cls && k < 32 && !npc;
		     cls = peek_ptr(cls + 8), k++)
			if (cls == g_prg.class_npc)
				npc = peek_ptr(h + PR_HOBJ_GAMEOBJ);
		if (!npc || vptr_of((void *)npc) != g_cbg.vt_npc)
			fail = "FUN_029a4830 made no NPC (placement refused, or the template is a prop)";
	}
	if (!fail) {
		peek(h + OBJ_POS, hp, sizeof hp);
		zone = PR_FN(PRS_ZONE, pr_zone_fn)(hp, 0, 0);
		if (pr_slot_ok(npc, PRS_SETZONE, PR_SLOT_SETZONE))
			PR_FN(PRS_SETZONE, pr_setzone_fn)(npc, zone, 1);
		c = peek_ptr(npc + PR_CH_COMBAT);
		peek(g_prg.spawn_level, &lvl, sizeof lvl);
		if (c && vptr_of((void *)c) == g_cbg.vt_combat)
			PR_FN(PRS_LEVEL, pr_level_fn)(c, lvl);
		else
			c = 0;
		g_pr.orc_h = h, g_pr.orc_npc = npc, g_pr.orc_c = c, g_pr.orc_type = type, g_pr.orc_t = t;
		g_pr.orc_dead = 0, g_pr.orc_checks = 0;
		g_pr.cur_orc = type;
		g_pr.spawns++;
		g_pr.far = far == 4 ? 0 : far, g_pr.far_aware = 0, g_pr.far_logs = 0, g_pr.far_next = t + 2.0, g_pr.far_aware_t = 0;
		memcpy(g_pr.far_pos0, hp, sizeof hp), g_pr.far_dist0 = dist3(hp, m->pos);
		g_pr.far_spawns += far != 0 && far != 4;
	} else {
		g_pr.spawn_fail++;
	}
	if (pr_log_ok(t))
		logf_("PRACTICE t=%.3f spawn%s %s (%s) template %s ('%s') %s: %s; at (%.1f %.1f %.1f) %.0f cm ahead, ground object %#llx, "
		      "%s; hobj=%#llx npc=%#llx Combat=%#llx zone=%#llx level %u", t,
		      far == 4 ? " near (Charge execution)" : far == 3 ? " put" : far == 2 ? " placed" : far ? " far" : "",
		      somvr_pr_orc_label(type), who,
		      name, tmpl ? tn : "-", fail ? "FAILED" : "ok", fail ? fail : "one orc out", hit[0], hit[1], hit[2],
		      (double)dist, (unsigned long long)ground, far == 1 || far == 2 ? "its back to Talion (unaware: it sees him only "
		      "as the game's senses would)" : "facing Talion", (unsigned long long)h, (unsigned long long)npc, (unsigned long long)c, (unsigned long long)zone,
		      lvl);
}
static void pr_spawn(const struct pr_me *m, int type, double t, const char *who) { pr_spawn_at(m, type, t, who, 0); }

/* build 18: the far orc's watch (the game thread, every frame while it is out): every 2 s for its first 40 s - how far
 * from Talion, how far it moved since the spawn, what its Combat targets (+0x4c0: none, Talion's
 * HOBJECT = it fights him, another); the first frame it targets Talion: "noticed" (the game's own senses, a hit, ...),
 * then every 2 s for 6 s */
#define PR_C_TARGET OFS(PR_C_TARGET, 0x4c0) /* Combat: its current target HOBJECT (as the threat's TH_C_TARGET) */
static void pr_far_tick(const struct pr_me *m, double t)
{
	if (!g_pr.far || !g_pr.orc_h || !g_pr.orc_c || g_pr.orc_dead)
		return;
	uintptr_t tg = peek_ptr(g_pr.orc_c + PR_C_TARGET), th = m->pc ? peek_ptr(m->pc + C_HOBJ) : 0;
	int at_him = tg && (tg == th || tg == m->obj);
	float p[3] = { 0, 0, 0 };
	peek(g_pr.orc_h + OBJ_POS, p, sizeof p);
	float d = dist3(p, m->pos) / 100.0f, mv = dist3(p, g_pr.far_pos0) / 100.0f;
	double age = t - g_pr.orc_t;
	if (at_him && !g_pr.far_aware) {
		g_pr.far_aware = 1, g_pr.far_aware_t = t, g_pr.far_noticed++;
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f far orc noticed Talion after %.1f s: its target = Talion (Combat+0x4c0 %#llx), %.1f m "
			      "from him, moved %.1f m", t, age, (unsigned long long)tg, (double)d, (double)mv);
		return;
	}
	if (t < g_pr.far_next || (age > 40.5 && !(g_pr.far_aware && t - g_pr.far_aware_t < 6.5)) || g_pr.far_logs >= 40)
		return; /* (its first 40 s, and 6 s after it noticed him: it comes) */
	g_pr.far_next = t + 2.0, g_pr.far_logs++;
	if (pr_log_ok(t))
		logf_("PRACTICE t=%.3f far orc +%.0f s: %.1f m from Talion (%.1f at the spawn), moved %.1f m; its target %s; %s",
		      t, age, (double)d, (double)(g_pr.far_dist0 / 100.0f), (double)mv,
		      at_him ? "Talion" : tg ? "another" : "none", g_pr.far_aware ? "aware (fighting him)" : "unaware");
}

/* build 19 (the group): one orc of type `type` on the ground under `at`, facing Talion (re-combat-spawn.md 1.2 steps 1-4 as P9's `spawn`):
 * NULL = made (*o filled), else why not (o->hit, o->ground, o->tn as far as it went) */
struct pr_made { uintptr_t h, npc, c, zone, ground, tmpl; float hit[3]; uint32_t lvl; char tn[96]; const char *name; };
static const char *pr_make_at(const struct pr_me *m, int type, const float at[3], struct pr_made *o)
{
	const char *fail = NULL;
	memset(o, 0, sizeof *o);
	o->name = g_pr_tmpl_test[0] ? g_pr_tmpl_test : somvr_pr_orc_template(type); /* (a test: SOMVR_VERBS) */
	o->tmpl = PR_FN(PRS_BYNAME, pr_byname_fn)(o->name);
	if (o->tmpl)
		fb_rec_name(o->tmpl, o->tn, sizeof o->tn);
	float q[4], hp[3] = { 0, 0, 0 };
	uintptr_t ctx, ret = 0;
	if (!o->tmpl)
		fail = "no such AI/Templates record (by name)";
	else if (!pr_ground(at, o->hit, &o->ground))
		fail = "the ray down hit nothing (no ground within 6 m below the spot ahead)";
	else if (!(ctx = o->ground ? o->ground : m->pc ? peek_ptr(m->pc + C_HOBJ) : 0))
		fail = "no context object (neither the ground's nor Talion's)";
	if (!fail) {
		somvr_pr_facing(o->hit, m->pos, q);
		float xf[7] = { o->hit[0], o->hit[1] + 10.0f, o->hit[2], q[0], q[1], q[2], q[3] };
		ret = PR_FN(PRS_SPAWN, pr_spawn_fn)(ctx, "SomVR_Practice", xf, o->tmpl, 1, 1, 0, 0, 0);
		o->h = ret ? peek_ptr(ret + 8) : 0;
		for (uintptr_t cls = o->h ? peek_ptr(peek_ptr(o->h + PR_HOBJ_CLASSES)) : 0, k = 0; cls && k < 32 && !o->npc;
		     cls = peek_ptr(cls + 8), k++)
			if (cls == g_prg.class_npc)
				o->npc = peek_ptr(o->h + PR_HOBJ_GAMEOBJ);
		if (!o->npc || vptr_of((void *)o->npc) != g_cbg.vt_npc)
			fail = "FUN_029a4830 made no NPC (placement refused, or the template is a prop)";
	}
	if (!fail) {
		peek(o->h + OBJ_POS, hp, sizeof hp);
		o->zone = PR_FN(PRS_ZONE, pr_zone_fn)(hp, 0, 0);
		if (pr_slot_ok(o->npc, PRS_SETZONE, PR_SLOT_SETZONE))
			PR_FN(PRS_SETZONE, pr_setzone_fn)(o->npc, o->zone, 1);
		o->c = peek_ptr(o->npc + PR_CH_COMBAT);
		peek(g_prg.spawn_level, &o->lvl, sizeof o->lvl);
		if (o->c && vptr_of((void *)o->c) == g_cbg.vt_combat)
			PR_FN(PRS_LEVEL, pr_level_fn)(o->c, o->lvl);
		else
			o->c = 0;
	}
	return fail;
}

/* why a spawn is refused now (NULL = not) */
static const char *pr_spawn_refusal(void)
{
	return !g_pr.ok_spawn ? "the spawn's sites did not check out at start" : !g_pr.here ? "not in the practice area (go there first)"
	       : g_pr.trip.phase != SOMVR_PRT_IDLE ? "a trip is on" : NULL;
}


/* build 19: "Spawn group" - n (3 or 4; somvr_pr_group_plan) of mixed types around Talion at 4.5-5.5 m, each facing
 * him; everything of ours out before removed first */
static void pr_group(const struct pr_me *m, int n, double t, const char *who)
{
	const char *fail = pr_spawn_refusal();
	if (fail) {
		g_pr.refused++;
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f group (%s) refused: %s", t, who, fail);
		return;
	}
	pr_remove_orc("a new group", t);
	somvr_pr_group_orc plan[SOMVR_PR_GROUP_MAX];
	int k = somvr_pr_group_plan(n, plan), made = 0;
	sv_vec3 f = sv_qrot(sv_qnormalize(sv_quat_of(m->rot)), (sv_vec3){ 0, 0, 1 });
	float fwd[3] = { f.x, f.y, f.z };
	memset(&g_pr_grp, 0, sizeof g_pr_grp);
	g_pr_grp.t = t;
	for (int i = 0; i < k; i++) {
		float at[3];
		somvr_pr_group_spot(m->pos, fwd, plan[i].yaw_deg, plan[i].dist_cm, at);
		struct pr_made o;
		const char *why = pr_make_at(m, plan[i].type, at, &o);
		if (!why) {
			int j = g_pr_grp.n++;
			g_pr_grp.h[j] = o.h, g_pr_grp.npc[j] = o.npc, g_pr_grp.c[j] = o.c, g_pr_grp.type[j] = plan[i].type;
			made++, g_pr.spawns++;
		} else {
			g_pr.spawn_fail++;
		}
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f group (%s) orc %d: %s '%s' %s%s at %+.0f deg %.1f m (%.1f %.1f %.1f) hobj=%#llx Combat=%#llx "
			      "level %u", t, who, i, somvr_pr_orc_label(plan[i].type), o.tmpl ? o.tn : "-", why ? "FAILED: " : "ok",
			      why ? why : "", (double)plan[i].yaw_deg, (double)(plan[i].dist_cm / 100.0f), o.hit[0], o.hit[1], o.hit[2],
			      (unsigned long long)o.h, (unsigned long long)o.c, o.lvl);
	}
	g_pr_grp.alive = made;
	g_pr.groups++;
	if (pr_log_ok(t))
		logf_("PRACTICE t=%.3f group (%s): %d of %d orcs out around Talion (two in front, the rest beside and behind him)", t,
		      who, made, k);
}

/* a group orc of ours still in the world (dead bodies too: Remove takes them) */
static int pr_group_out(void)
{
	for (int i = 0; i < g_pr_grp.n; i++)
		if (!g_pr_grp.gone[i])
			return 1;
	return 0;
}

/* the group's orcs: each one's death or removal by the game, once */
static void pr_group_tick(double t)
{
	for (int i = 0; i < g_pr_grp.n; i++) {
		if (g_pr_grp.gone[i])
			continue;
		if (!pr_obj_alive(g_pr_grp.h[i], g_pr_grp.npc[i])) {
			g_pr_grp.gone[i] = 1, g_pr_grp.alive -= !g_pr_grp.down[i];
			if (pr_log_ok(t))
				logf_("PRACTICE t=%.3f the group's %s orc %d is gone (the game removed it) after %.1f s", t,
				      somvr_pr_orc_label(g_pr_grp.type[i]), i, t - g_pr_grp.t);
			continue;
		}
		if (!g_pr_grp.down[i] && g_pr_grp.c[i] && cb_dead(g_pr_grp.c[i])) {
			g_pr_grp.down[i] = 1, g_pr_grp.alive--, g_pr.killed++;
			if (pr_log_ok(t))
				logf_("PRACTICE t=%.3f the group's %s orc %d is down after %.1f s (%d standing)", t,
				      somvr_pr_orc_label(g_pr_grp.type[i]), i, t - g_pr_grp.t, g_pr_grp.alive);
		}
	}
}

/* our orc 1 and 3 s after its spawn (P9's SPAWNCHECK) and its death */
static void pr_orc_tick(const struct pr_me *m, double t)
{
	if (!g_pr.orc_h)
		return;
	if (!pr_orc_alive()) {
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f the %s orc is gone (the game removed it) after %.1f s", t,
			      somvr_pr_orc_label(g_pr.orc_type), t - g_pr.orc_t);
		g_pr.orc_h = g_pr.orc_npc = g_pr.orc_c = 0;
		return;
	}
	int dead = g_pr.orc_c && cb_dead(g_pr.orc_c);
	if (dead && !g_pr.orc_dead) {
		g_pr.orc_dead = 1, g_pr.killed++;
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f the %s orc is down after %.1f s (\"Spawn an orc\" makes the next)", t,
			      somvr_pr_orc_label(g_pr.orc_type), t - g_pr.orc_t);
	}
	if (g_pr.orc_checks < 2 && t - g_pr.orc_t >= (g_pr.orc_checks ? 3.0 : 1.0)) {
		g_pr.orc_checks++;
		float p[3] = { 0, 0, 0 };
		char rn[96] = "";
		peek(g_pr.orc_h + OBJ_POS, p, sizeof p);
		if (g_pr.orc_c)
			fb_rec_name(peek_ptr(g_pr.orc_c + PR_C_CHARREC), rn, sizeof rn);
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f spawn check %d: the %s orc '%s' alive, dead=%d, %.1f m from Talion at (%.1f %.1f %.1f)",
			      t, g_pr.orc_checks, somvr_pr_orc_label(g_pr.orc_type), rn, dead, (double)(dist3(p, m->pos) / 100.0f),
			      p[0], p[1], p[2]);
	}
}

/* ---- keep clear: every armed AI within clear_cm (PlayerCombat's three targeting bands: P9's ORC lists reached 110 m)
 * removed, never ours, the player, his mount, the dead, slaves; nothing in a scripted moment ---------------------- */
static void pr_clear(const struct pr_me *m, uint32_t gate, double t)
{
	static unsigned char band[PR_BANDS_MAX * PCB_SIZE]; /* the game thread only */
	if (!g_pr.ok_clear || g_pr.clear == SOMVR_PRC_OFF || !m->pcomb)
		return;
	gate &= ~(uint32_t)SOMVR_PRG_TRIP;
	if (gate) {
		g_pr.held++;
		if (gate != g_pr.gate_last && pr_log_ok(t)) {
			char w[160];
			somvr_pr_gate_text(gate, w, sizeof w);
			logf_("PRACTICE t=%.3f clear held: %s (nothing removed meanwhile)", t, w);
		}
		g_pr.gate_last = gate;
		return;
	}
	g_pr.gate_last = 0;
	g_pr.passes++;
	uintptr_t seen[3 * PR_BANDS_MAX];
	int ns = 0, n = 0;
	for (int k = 0; k < 3; k++) {
		uintptr_t vb = m->pcomb + PC_BANDS + 0x18 * (uintptr_t)k, b = peek_ptr(vb), e = peek_ptr(vb + 8);
		size_t cnt = e > b && (e - b) % PCB_SIZE == 0 ? (size_t)(e - b) / PCB_SIZE : 0;
		if (cnt > PR_BANDS_MAX)
			cnt = PR_BANDS_MAX;
		if (!cnt || !peek(b, band, cnt * PCB_SIZE))
			continue;
		for (size_t i = 0; i < cnt; i++) {
			uintptr_t c;
			memcpy(&c, band + i * PCB_SIZE + PCB_COMBAT, sizeof c);
			int dup = !c || c == m->pc;
			for (int j = 0; j < ns && !dup; j++)
				dup = seen[j] == c;
			if (dup)
				continue;
			seen[ns++] = c;
			uintptr_t owner = vptr_of((void *)c) == g_cbg.vt_combat ? peek_ptr(c + C_OWNER) : 0, h = 0;
			float p[3];
			somvr_pr_cand cd;
			memset(&cd, 0, sizeof cd);
			uint8_t fl = 0x18;
			cd.npc = owner && vptr_of((void *)owner) == g_cbg.vt_npc && peek_ptr(c + C_AI) && (h = peek_ptr(c + C_HOBJ)) &&
				 peek(h + OBJ_POS, p, sizeof p) && vec_ok(p, 1e7f) && peek(h + PR_HOBJ_FLAGS, &fl, 1);
			if (!cd.npc || (fl & 0x18))
				continue; /* not an NPC, or its removal queued already (ours or the game's) */
			char rn[96];
			fb_rec_name(peek_ptr(c + PR_C_CHARREC), rn, sizeof rn);
			cd.dist_cm = dist3(p, m->pos), cd.ours = h == g_pr.orc_h || pr_kept(h), cd.mount = h == m->mount;
			cd.dead = cb_dead(c), cd.harmless = somvr_pr_harmless(rn);
			if (!somvr_pr_clear_pick(&cd, g_pr.clear, g_pr.clear_cm))
				continue;
			int done = pr_remove_obj(h, owner);
			n += done;
			if (pr_log_ok(t))
				logf_("PRACTICE t=%.3f clear '%s' at %.1f m hobj=%#llx: %s", t, rn[0] ? rn : "?",
				      (double)(cd.dist_cm / 100.0f), (unsigned long long)h, done ? "removal queued" : "NOT removed");
		}
	}
	g_pr.cleared += (uint64_t)n;
}

/* ---- the light ------------------------------------------------------------------------------------------------- */
/* the trip there arrived (under the black): a night made day (SOMVR_PRACTICE_DAY=day), the weather 0.6 s later */
static void pr_light_arrived(double t)
{
	if (g_pr.day == PR_DAY_OFF || !g_pr.ok_light)
		return;
	char rec[64], tod[32];
	if (!pr_weather(rec, sizeof rec, tod, sizeof tod)) {
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f light: the weather state is not readable now (no change)", t);
		return;
	}
	if (g_pr.light_tp) { /* the weather went with the teleport (a day): nothing more here */
		g_pr.light_tp = 0;
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f light: weather '%s', time of day '%s' (ours since the teleport; was '%s')", t, rec, tod,
			      g_pr.orig_weather);
		g_pr.light_step = 2, g_pr.light_t = t - 0.5; /* the "light now" line 1 s later */
		return;
	}
	if (!g_pr.weather_set) /* a second trip there keeps the first one's original */
		snprintf(g_pr.orig_weather, sizeof g_pr.orig_weather, "%s", rec);
	if (!g_pr.advanced)
		snprintf(g_pr.orig_tod, sizeof g_pr.orig_tod, "%s", tod);
	int night = somvr_pr_is_night(tod);
	/* a dry day: no change, no pause (build 36, SOMVR_PRACTICE_SKY=clear: an overcast one is changed too) */
	int dry = !night && !g_pr.weather_set && !somvr_pr_sky_change(rec, g_pr.sky, g_pr.weather);
	int grey = !night && !dry && !somvr_pr_is_wet(rec);
	if (pr_log_ok(t))
		logf_("PRACTICE t=%.3f light: weather '%s', time of day '%s'%s%s", t, rec, tod,
		      night && g_pr.day == PR_DAY_FULL ? ": a night, the day now (GameServerShell.AdvanceTimeOfDay)" :
		      night ? ": a night, kept (SOMVR_PRACTICE_DAY=weather)" :
		      dry ? ": a dry day, kept (no ChangeWeather: no pause ~3 s later)" : "",
		      grey && !g_pr.weather_set ? (g_pr.sky == SOMVR_PRSKY_ALWAYS ? ": changed (SOMVR_PRACTICE_SKY=always, a test)" :
		      ": an overcast day, a clear one now (SOMVR_PRACTICE_SKY=clear)") : "");
	if (night && g_pr.day == PR_DAY_FULL && !g_pr.advanced && !pr_console("GameServerShell.AdvanceTimeOfDay", t))
		g_pr.advanced = 1, g_pr.advances++;
	if (dry) {
		g_pr.pw_dry++;
		return;
	}
	g_pr.light_step = 1, g_pr.light_t = t;
}

/* the trip there's teleport (the view black): a wet day's weather goes with it, so the game's pause that a ChangeWeather
 * brings ~3 s later falls under the blink's black (SOMVR_PRACTICE_LOADHOLD); a night keeps the arrival's order (the
 * day first, the weather 0.6 s later). Returns the console text to put before the Teleport ("" = none) */
static const char *pr_light_teleport(double t)
{
	static char cmd[96];
	char rec[64], tod[32];
	g_pr.light_tp = 0;
	if (g_pr.day == PR_DAY_OFF || !g_pr.ok_light || g_pr.weather_set || !pr_weather(rec, sizeof rec, tod, sizeof tod) ||
	    somvr_pr_is_night(tod) || !somvr_pr_sky_change(rec, g_pr.sky, g_pr.weather))
		return ""; /* (build 36: SOMVR_PRACTICE_SKY=clear changes an overcast day too) */
	snprintf(g_pr.orig_weather, sizeof g_pr.orig_weather, "%s", rec);
	snprintf(g_pr.orig_tod, sizeof g_pr.orig_tod, "%s", tod);
	snprintf(cmd, sizeof cmd, "GameServerShell.ChangeWeather %s; ", g_pr.weather);
	g_pr.light_tp = 1, g_pr.weather_set = 1, g_pr.weathers++;
	g_pr.pw_t = t, g_pr.pw_seen = 0, g_pr.pw_waits++;
	return cmd;
}

/* each frame before the trip's step: 1 = the game's pause after our ChangeWeather is still to come (at most
 * PR_PAUSE_WAIT_S after it) or on now: the black holds (somvr_pr_trip_step_busy, at most hold_max_s after the arrival) */
static int pr_pause_busy(double t, int paused)
{
	if (!g_pr.pw_t)
		return 0;
	int black = g_pr.trip.phase == SOMVR_PRT_MOVE || g_pr.trip.phase == SOMVR_PRT_HOLD;
	if (paused && !g_pr.pw_seen) {
		g_pr.pw_seen = 1, g_pr.pw_seen_t = t, g_pr.pw_pauses++;
		g_pr.pw_after_max = fmax(g_pr.pw_after_max, t - g_pr.pw_t);
		g_pr.pw_open += !black;
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f the game paused %.2f s after our ChangeWeather (%s)", t, t - g_pr.pw_t,
			      black ? "under the blink's black" : "IN VIEW: the world stands still");
	}
	if (paused)
		return g_pr.load_mode;
	if (g_pr.pw_seen) {
		g_pr.pw_len_max = fmax(g_pr.pw_len_max, t - g_pr.pw_seen_t);
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f the game's pause after our ChangeWeather over (%.2f s long)", t, t - g_pr.pw_seen_t);
		g_pr.pw_t = 0;
		return 0;
	}
	if (t - g_pr.pw_t < PR_PAUSE_WAIT_S)
		return g_pr.load_mode;
	g_pr.pw_none++;
	if (pr_log_ok(t))
		logf_("PRACTICE t=%.3f no game pause within %.1f s of our ChangeWeather", t, PR_PAUSE_WAIT_S);
	g_pr.pw_t = 0;
	return 0;
}

static void pr_light_tick(double t)
{
	if (atomic_load_explicit(&g_pr_wx_pend, memory_order_acquire)) { /* build 36's test word: the weather, read after */
		char cmd[128], rec[64] = "?", tod[32] = "?";
		pr_weather(rec, sizeof rec, tod, sizeof tod);
		snprintf(cmd, sizeof cmd, "GameServerShell.ChangeWeather %s", g_pr_wx);
		atomic_store(&g_pr_wx_pend, 0);
		if (!pr_console(cmd, t)) {
			snprintf(g_pr.orig_weather, sizeof g_pr.orig_weather, "%s", rec);
			g_pr.pw_t = t, g_pr.pw_seen = 0, g_pr.pw_waits++;
			g_pr.light_step = 3, g_pr.light_t = t; /* the "light later" line */
		}
		return;
	}
	if (g_pr.light_step == 1 && t - g_pr.light_t >= PR_LIGHT_WEATHER_S) {
		char cmd[128];
		snprintf(cmd, sizeof cmd, "GameServerShell.ChangeWeather %s", g_pr.weather);
		if (!pr_console(cmd, t))
			g_pr.weather_set = 1, g_pr.weathers++, g_pr.pw_t = t, g_pr.pw_seen = 0, g_pr.pw_waits++;
		g_pr.light_step = 2;
	} else if (g_pr.light_step == 2 && t - g_pr.light_t >= 1.5) {
		char rec[64], tod[32];
		g_pr.light_step = g_pr.weather_set ? 3 : 0;
		if (pr_weather(rec, sizeof rec, tod, sizeof tod) && pr_log_ok(t))
			logf_("PRACTICE t=%.3f light now: weather '%s', time of day '%s' (was '%s', '%s')", t, rec, tod,
			      g_pr.orig_weather, g_pr.orig_tod);
	} else if (g_pr.light_step == 3 && t - g_pr.light_t >= PR_LIGHT_LATER_S) {
		char rec[64], tod[32];
		g_pr.light_step = 0;
		if (pr_weather(rec, sizeof rec, tod, sizeof tod) && pr_log_ok(t))
			logf_("PRACTICE t=%.3f light later (%.0f s): weather '%s', time of day '%s' (was '%s'%s; asked %s)", t,
			      PR_LIGHT_LATER_S, rec, tod, g_pr.orig_weather, strcmp(rec, g_pr.orig_weather) ? ": changed" :
			      ": UNCHANGED", g_pr.weather);
	}
}

/* ---- the trips ------------------------------------------------------------------------------------------------- */
/* the trip's timing: the climb's blink down holds the black shorter (nothing to change under it) */
static const somvr_pr_tcfg *pr_tc(void)
{
	return g_pr.trip_kind == PR_TRIP_DOWN ? (g_pr.down_quick ? &g_pr.tcfg_quick : &g_pr.tcfg_down) : &g_pr.tcfg;
}
static const char *pr_kind_word(int kind, int there)
{
	return kind == PR_TRIP_DOWN ? "down" : kind == PR_TRIP_GO ? (there ? "there" : "go to the practice area")
				      : (there ? "back" : "back to where Talion was");
}

static void pr_trip_begin(int kind, const float target[3], double t, const char *who)
{
	g_pr.trips[kind]++;
	g_pr.trip_kind = kind, g_pr.trip_first_tp = 1;
	g_pr.ground_last = SOMVR_PRGD_UNKNOWN, g_pr.ground_said = 0, g_pr.ground_back = 0;
	somvr_pr_trip_start(&g_pr.trip, pr_tc(), target, t);
	if (pr_log_ok(t))
		logf_("PRACTICE t=%.3f %s (%s): the blink to (%.0f %.0f %.0f) starts", t, pr_kind_word(kind, 0), who, target[0],
		      target[1], target[2]);
}

/* SOMVR_CLIMB_DESCENT (somvr.c's climb, the game thread): A over a high drop = the trip's blink to the ground under
 * him (no area, no light, no orc: only the fade, the Teleport, the settle). pr_down_ok: one can start now (the console,
 * no trip on or waiting, not paused); pr_down_start: 1 = started; pr_down_teleported: 1 once after its teleport */
static int pr_down_ok(void)
{
	return g_pr.on && g_pr.trip.phase == SOMVR_PRT_IDLE && !g_pr.pend_act && !game_paused();
}

static int pr_down_start(sv_vec3 to, double t, int quick)
{
	const char *who = quick == 3 ? "the wraith step, quick" : quick == 2 ? "the climb's drop mark, quick"
			  : quick ? "the climb's pullover, quick" : "the climb's A";
	if (!pr_down_ok()) {
		g_pr.refused++;
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f down (%s) refused: %s", t, who, !g_pr.on ? "the practice is off"
			      : g_pr.trip.phase != SOMVR_PRT_IDLE ? "a trip is on" : g_pr.pend_act ? "a trip waits" : "paused");
		return 0;
	}
	float p[3] = { to.x, to.y, to.z };
	g_pr.down_tp = 0, g_pr.down_quick = quick != 0, g_pr.down_ws = quick == 3;
	pr_trip_begin(PR_TRIP_DOWN, p, pr_now(), who);
	(void)t;
	return 1;
}

static int pr_down_teleported(void)
{
	if (!g_pr.down_tp)
		return 0;
	g_pr.down_tp = 0;
	return 1;
}

/* a teleport of any other trip (go, back, the menu's): 1 once after it - somvr.c's climb ends there (re-xr-pipeline.md
 * 101.7: a hang went through two trips and hung in the air at each arrival) */
static int pr_trip_teleported(void)
{
	if (!g_pr.trip_tp)
		return 0;
	g_pr.trip_tp = 0;
	return 1;
}

static void pr_go(const struct pr_me *m, int have, double t, const char *who)
{
	const char *no = !have ? "no player in the world now" : g_pr.went ? "already there (\"practice back\" first)" :
			 g_pr.trip.phase != SOMVR_PRT_IDLE ? "a trip is on" : NULL;
	if (no) {
		g_pr.refused++;
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f go (%s) refused: %s", t, who, no);
		return;
	}
	if (!g_pr.have_back) /* a trip there that did not arrive keeps the place Talion first left */
		memcpy(g_pr.back, m->pos, sizeof g_pr.back);
	g_pr.have_back = 1;
	pr_trip_begin(PR_TRIP_GO, g_pr.spot, t, who);
}

static void pr_back(double t, const char *who)
{
	const char *no = !g_pr.have_back ? "no place to go back to (never went)" :
			 g_pr.trip.phase != SOMVR_PRT_IDLE ? "a trip is on" : NULL;
	if (no) {
		g_pr.refused++;
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f back (%s) refused: %s", t, who, no);
		return;
	}
	pr_remove_orc("going back", t);
	pr_quiet_off(t, "going back");
	g_pr.went = 0, g_pr.here = 0, g_pr.light_step = 0, g_pr.light_tp = 0, g_pr.pw_t = 0;
	pr_trip_begin(PR_TRIP_BACK, g_pr.back, t, who);
}

/* the trip's teleport now (the view black): the trip back first gives the light back (the TOD, the weather). Build 37:
 * pin = a teleport while no ground is under him (the world streams in): the same Teleport, not a try, logged sparsely */
static void pr_teleport(double t, int pin)
{
	char cmd[320];
	size_t o = 0;
	if (pin) {
		const float *q = g_pr.trip.target;
		snprintf(cmd, sizeof cmd, "Teleport %ld %ld %ld", lroundf(q[0]), lroundf(q[1]), lroundf(q[2]));
		g_pr.ground_pins++;
		if (g_pr.trip_kind == PR_TRIP_DOWN)
			g_pr.down_tp = 1;
		else
			g_pr.trip_tp = 1;
		char why[256] = "";
		int rc = es_run(cmd, why, sizeof why), n = g_pr.trip.pins;
		if ((rc || n <= 2 || n % 20 == 0) && pr_log_ok(t))
			logf_("PRACTICE t=%.3f console '%s' (pin %d: no ground under him yet, kept at the target)%s%s", t, cmd, n,
			      rc ? ": not run: " : "", rc ? why : "");
		return;
	}
	if (g_pr.trip_kind == PR_TRIP_BACK && g_pr.trip_first_tp) {
		if (g_pr.advanced)
			o += (size_t)snprintf(cmd + o, sizeof cmd - o, "GameServerShell.AdvanceTimeOfDay; ");
		if (g_pr.weather_set && g_pr.orig_weather[0])
			o += (size_t)snprintf(cmd + o, sizeof cmd - o, "GameServerShell.ChangeWeather %s; ", g_pr.orig_weather);
		g_pr.restores += g_pr.advanced || g_pr.weather_set;
		g_pr.advanced = g_pr.weather_set = 0;
	}
	if (g_pr.trip_kind == PR_TRIP_GO && g_pr.trip_first_tp)
		o += (size_t)snprintf(cmd + o, sizeof cmd - o, "%s", pr_light_teleport(t));
	g_pr.trip_first_tp = 0;
	if (g_pr.trip_kind == PR_TRIP_DOWN)
		g_pr.down_tp = 1; /* the climb ends now: gravity back, he stands where the Teleport puts him */
	else
		g_pr.trip_tp = 1; /* the trips by the menu too: no hang carried into the air at the arrival (101.7) */
	const float *p = g_pr.trip.target; /* whole centimetres: no locale's decimal point (P5: Teleport x y z) */
	snprintf(cmd + o, sizeof cmd - o, "Teleport %ld %ld %ld", lroundf(p[0]), lroundf(p[1]), lroundf(p[2]));
	g_pr.teleports++;
	pr_console(cmd, t);
}

/* build 37 (SOMVR_PRACTICE_GROUND_HOLD=grav): with no ground under Talion his proxy's gravity off (the jump module's
 * jp_grav, its vtable checked; written again each frame: the game may set it back) - he hangs at the target with no
 * teleport after the first; pm 0 = give it back now (once). The pins stay as the backstop */
static int jp_grav(uintptr_t pm, int on); /* somvr_jump_game.c */
static void pr_grav_hold(uintptr_t pm, double t, const char *why)
{
	if (pm) {
		int v = jp_grav(pm, 0);
		if (v < 0)
			return;
		g_pr.grav_frames++;
		if (!g_pr.grav_off) {
			g_pr.grav_off = 1, g_pr.grav_pm = pm, g_pr.grav_t = t, g_pr.grav_holds++;
			if (pr_log_ok(t))
				logf_("PRACTICE t=%.3f no ground under Talion: his gravity held off (SOMVR_PRACTICE_GROUND_HOLD=grav; the "
				      "flag was %d): he hangs at the target while the world streams in", t, v);
		}
		return;
	}
	if (!g_pr.grav_off)
		return;
	g_pr.grav_off = 0;
	int v = jp_grav(g_pr.grav_pm, 1);
	if (pr_log_ok(t))
		logf_("PRACTICE t=%.3f his gravity back on after %.2f s (%s; the flag was %d)", t, t - g_pr.grav_t, why, v);
}

static void pr_trip_tick(const struct pr_me *m, int have, double t, int busy)
{
	int held = g_pr.trip.held_busy, ph = g_pr.trip.phase, pins0 = g_pr.trip.pins;
	double hold_t0 = g_pr.trip.t0;
	/* build 37 (SOMVR_PRACTICE_GROUND): the world under him, while he is moved and held in the black */
	int gnd = ph == SOMVR_PRT_MOVE || ph == SOMVR_PRT_HOLD ? pr_trip_ground(m, have) : SOMVR_PRGD_UNKNOWN;
	int x = somvr_pr_trip_step_ground(&g_pr.trip, pr_tc(), t, m->pos, have, game_paused(), busy, gnd);
	if (gnd == SOMVR_PRGD_NONE && !(g_pr.ground_said & 1)) {
		g_pr.ground_said |= 1, g_pr.ground_waits++;
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f no ground under Talion %.1f s after the teleport (a ray down %.0f m from his root at "
			      "%.0f %.0f %.0f: nothing - the world streams in there): the black holds, he is kept at the target, at "
			      "most %.0f s (SOMVR_PRACTICE_GROUND)", t, t - g_pr.trip.t_first, (double)(PR_GROUND_RAY_CM / 100),
			      m->pos[0], m->pos[1], m->pos[2], (double)pr_tc()->ground_wait_s);
	}
	if (g_pr.trip.ground_t > 0 && (g_pr.ground_said & 3) == 1) {
		double w = g_pr.trip.ground_t - g_pr.trip.t_first;
		g_pr.ground_said |= 2;
		if (w > g_pr.ground_wait_max)
			g_pr.ground_wait_max = w;
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f the ground came under Talion %.2f s after the first teleport (%d pins; the ground at "
			      "%.1f, the root %.1f: %s)", t, w, g_pr.trip.pins, g_pr.ground_y, m->pos[1],
			      gnd == SOMVR_PRGD_ON ? "on it" : "over it, landing");
	}
	g_pr.ground_last = gnd;
	/* build 37: while no ground is under him, every 5 s the ground at the target and at the start (the streaming's
	 * progress: is the place he left still loaded, has anything come at the spot) */
	if (gnd == SOMVR_PRGD_NONE && g_pr.trip.wait_t > 0 && t >= g_pr.ground_probe_t && pr_log_ok(t)) {
		float a[3] = { g_pr.trip.target[0], g_pr.trip.target[1] + 3000.0f, g_pr.trip.target[2] }, h[3], b[3];
		float o[3] = { g_pr.back[0], g_pr.back[1] + 3000.0f, g_pr.back[2] }, hb[3];
		uintptr_t ho = 0, hob = 0;
		int ta = pr_ray_down(a, 9000.0f, m->obj, h, &ho), tb = g_pr.have_back && pr_ray_down(o, 9000.0f, m->obj, hb, &hob);
		memcpy(b, g_pr.back, sizeof b);
		g_pr.ground_probe_t = t + 5.0;
		logf_("PRACTICE t=%.3f the wait for the ground, %.0f s in: at the target %s; where he left (%.0f %.0f %.0f) %s; the "
		      "root %.0f %.0f %.0f", t, t - g_pr.trip.t_first, ta ? "ground" : "nothing (30 m over it to 60 m under)",
		      b[0], b[1], b[2], !g_pr.have_back ? "-" : tb ? "ground (still loaded)" : "nothing (unloaded)", m->pos[0],
		      m->pos[1], m->pos[2]);
	}
	/* SOMVR_PRACTICE_GROUND_HOLD=grav: held while nothing is under him in the move or the hold (after the step) */
	int ph1 = g_pr.trip.phase;
	pr_grav_hold(g_pr.ground_hold && have && m->pm && gnd == SOMVR_PRGD_NONE &&
			     (ph1 == SOMVR_PRT_MOVE || ph1 == SOMVR_PRT_HOLD) ? m->pm : 0, t,
		     gnd != SOMVR_PRGD_NONE ? "the ground came" : "the trip moved on");
	if (g_pr.trip.held_busy && !held && pr_log_ok(t))
		logf_("PRACTICE t=%.3f the black held past its %.2f s for the game's pause after our ChangeWeather (at most %.1f s "
		      "after the arrival)", t, (double)pr_tc()->hold_s, (double)pr_tc()->hold_max_s);
	if (held && ph == SOMVR_PRT_HOLD && g_pr.trip.phase == SOMVR_PRT_IN && pr_log_ok(t))
		logf_("PRACTICE t=%.3f the black held %.2f s longer (%s): fading in", t, t - hold_t0 - (double)pr_tc()->hold_s,
		      busy ? "the cap: the game still paused" : "the game's pause over");
	if (x == SOMVR_PRX_TELEPORT) {
		pr_teleport(t, g_pr.trip.pins > pins0);
	} else if (x == SOMVR_PRX_ARRIVED) {
		g_pr.arrived++;
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f arrived at (%.1f %.1f %.1f) after %d teleport(s); %s", t, m->pos[0], m->pos[1], m->pos[2],
			      g_pr.trip.tries, g_pr.trip_kind == PR_TRIP_GO ? "in the practice area" : g_pr.trip_kind == PR_TRIP_DOWN
			      ? (g_pr.down_ws ? "the wraith step's blink (SOMVR_WRAITH_STEP)" : "down from the climb (SOMVR_CLIMB_DESCENT)")
			      : "back where Talion was");
		if (g_pr.trip_kind == PR_TRIP_GO) {
			g_pr.went = 1, g_pr.here = 1;
			pr_light_arrived(t);
			g_pr.next_clear = 0; /* a first pass now, under the black */
		}
	} else if (x == SOMVR_PRX_FAILED) {
		g_pr.failed++;
		g_pr.ground_none += g_pr.trip.noground;
		/* build 37: a trip there whose spot never had ground under it: back where Talion was (that trip waits for its
		 * ground too), never a fade-in over nothing */
		int back = g_pr.trip.noground && g_pr.trip_kind == PR_TRIP_GO && g_pr.have_back && !g_pr.ground_back;
		if (pr_log_ok(t))
			logf_("WARN PRACTICE t=%.3f the trip %s did not arrive after %d teleports%s (Talion at %.1f %.1f %.1f, the "
			      "target %.0f %.0f %.0f): %s", t, pr_kind_word(g_pr.trip_kind, 1), g_pr.trip.tries,
			      g_pr.trip.noground ? " and the ground never came under him" : "", m->pos[0], m->pos[1], m->pos[2],
			      g_pr.trip.target[0], g_pr.trip.target[1], g_pr.trip.target[2],
			      back ? "back where he was, in the black" : "fading in where he is");
		if (back) {
			g_pr.ground_backs++;
			pr_remove_orc("no ground at the spot", t);
			pr_quiet_off(t, "no ground at the spot");
			g_pr.went = 0, g_pr.here = 0, g_pr.light_step = 0, g_pr.light_tp = 0, g_pr.pw_t = 0;
			pr_trip_begin(PR_TRIP_BACK, g_pr.back, t, "no ground at the practice spot");
			g_pr.ground_back = 1;
			return;
		}
	} else if (x == SOMVR_PRX_DONE && pr_log_ok(t)) {
		logf_("PRACTICE t=%.3f the blink %s ended (faded in)", t, pr_kind_word(g_pr.trip_kind, 1));
	}
	if (g_pr.trip.phase == SOMVR_PRT_IDLE)
		g_pr.trip_end_t = t;
	if (x == SOMVR_PRX_DONE && g_pr.trip_kind == PR_TRIP_BACK)
		g_pr.have_back = 0; /* back home: nothing to go back to */
}

static void pr_status(const struct pr_me *m, int have, double t)
{
	float mh = 0, hp = 0;
	if (m->ch)
		peek(m->ch + PR_CH_MINHEALTH, &mh, sizeof mh), peek(m->ch + PR_CH_HEALTH, &hp, sizeof hp);
	logf_("PRACTICE t=%.3f status: %s; trip %s; Talion %s at (%.1f %.1f %.1f), %.1f m from the spot; back %s (%.0f %.0f %.0f); "
	      "orc %s%s; type %s; health %.1f min %.1f; light: advanced %d weather set %d (was '%s', '%s')", t,
	      g_pr.here ? "in the practice area" : g_pr.went ? "away from the practice area (walked out)" : "in the world",
	      somvr_pr_phase_name(g_pr.trip.phase), have ? "known" : "not known", m->pos[0], m->pos[1], m->pos[2],
	      (double)(have ? dist3(m->pos, g_pr.spot) / 100.0f : -1.0f), g_pr.have_back ? "kept" : "none", g_pr.back[0],
	      g_pr.back[1], g_pr.back[2], g_pr.orc_h ? somvr_pr_orc_label(g_pr.orc_type) : "none",
	      g_pr.orc_h && g_pr.orc_dead ? " (down)" : "", somvr_pr_orc_label(g_pr.cur_orc), (double)hp, (double)mh,
	      g_pr.advanced, g_pr.weather_set, g_pr.orig_weather, g_pr.orig_tod);
}

/* an object's class by its vtable's typeinfo (the Itanium ABI: *(vptr - 8) = the typeinfo, its +8 = the mangled name,
 * e.g. "13LookAtTrigger"): into b (n bytes), "" when it does not read as one; read only */
static void pr_rtti(uintptr_t obj, char *b, size_t n)
{
	uintptr_t vp = 0, ti = 0, np = 0;
	b[0] = 0;
#ifdef _WIN32 /* the Windows exe has RTTI only for its libraries' classes (MSVC's, not this layout): the logs show "" */
	(void)obj, (void)n, (void)vp, (void)ti, (void)np;
	return;
#endif
	if (!obj || !peek(obj, &vp, sizeof vp) || !vp || !peek(vp - 8, &ti, sizeof ti) || !ti || !peek(ti + 8, &np, sizeof np) || !np)
		return;
	for (size_t i = 0; i + 1 < n; i++) {
		char c = 0;
		if (!peek(np + i, &c, 1) || !c || c < 32 || c > 126)
			break;
		b[i] = c, b[i + 1] = 0;
	}
}

/* the classes an object points at in its first 0x400 bytes (its parts: a plugin, a trigger, a prompt): " +OFF 'name'"
 * for each new name, 8 at most; read only */
static void pr_rtti_parts(uintptr_t obj, char *b, size_t n)
{
	char seen[8][40];
	int ns = 0;
	size_t l = 0;
	b[0] = 0;
	for (uintptr_t off = 0; off < 0x400 && ns < 8 && l < n - 60; off += 8) {
		uintptr_t p = 0;
		char c[40];
		if (!peek(obj + off, &p, sizeof p) || p < 0x10000 || (p & 7))
			continue;
		pr_rtti(p, c, sizeof c);
		if (!c[0])
			continue;
		int dup = 0;
		for (int i = 0; i < ns && !dup; i++)
			dup = !strcmp(seen[i], c);
		if (dup)
			continue;
		memcpy(seen[ns++], c, sizeof c);
		l += (size_t)snprintf(b + l, n - l, " +%#llx '%s'", (unsigned long long)off, c);
	}
}

/* "practice status nav": the nav markers with a place (active, enabled, an object), their record's name, the world
 * position and the distance from Talion (and, for the collect drill, the classes of a marker's objects within 20 m);
 * the ammo, explosives, fire pits and bait (dozens) only counted */
static void pr_navdump(const struct pr_me *m, int have, double t)
{
	static int ok = -1;
	if (ok < 0) {
		char why[160];
		ok = check_site(&g_prn_sites[PRN_NAV_POS], why, sizeof why) == 0;
		logf_("PRACTICE site %s %s %#llx: %s", g_prn_sites[PRN_NAV_POS].name, ok ? "ok" : "MISMATCH (no places)",
		      (unsigned long long)g_prn_sites[PRN_NAV_POS].addr, why);
	}
	uintptr_t node = 0;
	peek(PRN_LIST + 8, &node, sizeof node);
	int n = 0, placed = 0, logged = 0, skipped = 0;
	while (node && node != PRN_LIST && n < 1024) {
		uintptr_t mk = node - PRN_NODE, rec = 0, nx = 0, o[4] = { 0, 0, 0, 0 }, np = 0;
		uint8_t ae[2] = { 0, 0 }, rf = 0;
		char name[64] = "";
		peek(mk + PRN_REC, &rec, sizeof rec), peek(mk + PRN_ACTIVE, ae, sizeof ae);
		peek(mk + 0x260, &o[0], 8), peek(mk + 0x268, &o[1], 8), peek(mk + 0x270, &o[2], 8), peek(mk + 0xb0, &o[3], 8);
		if (rec && peek(rec + 1, &rf, 1) && (rf & 2) && peek(rec + 0x20, &np, sizeof np) && np)
			for (int i = 0; i + 1 < (int)sizeof name; i++) {
				char c = 0;
				if (!peek(np + (uintptr_t)i, &c, 1) || !c)
					break;
				name[i] = c, name[i + 1] = 0;
			}
		float pos[3] = { 0, 0, 0 };
		if (ok && ae[0] && ae[1] && rec && (o[0] || o[1] || o[2] || o[3])) {
			((prn_pos_fn)g_prn_sites[PRN_NAV_POS].target)(mk, pos);
			placed++;
			if (!strcmp(name, "Ammo") || !strcmp(name, "Explosive") || !strcmp(name, "Firepit") ||
			    !strcmp(name, "HangingBait"))
				skipped++;
			else if (logged++ < 60) {
				char cls[200] = "";
				float dm = have ? dist3(pos, m->pos) / 100.0f : -1.0f;
				if (dm >= 0 && dm < 20) /* the objects behind a near marker: their classes (+0x260 +0x268 +0x270 +0xb0:
							 * run 20261026-131340: the icon's CTextures) */
					for (int k = 0; k < 4; k++) {
						char c[40];
						size_t l = strlen(cls);
						pr_rtti(o[k], c, sizeof c);
						if (o[k] && l < sizeof cls - 60)
							snprintf(cls + l, sizeof cls - l, " o%d %#llx '%s'", k, (unsigned long long)o[k], c);
					}
				logf_("PRACTICE nav %d '%s' at (%.0f %.0f %.0f), %.1f m from Talion%s", n, name, pos[0], pos[1], pos[2],
				      (double)dm, cls);
				uintptr_t tg = 0; /* the marker's target (the probe's NM_TGT +0x230, else NM_TGT2 +0x210), its parts */
				char tc[40] = "", parts[520];
				if (dm >= 0 && dm < 20 && ((peek(mk + 0x230, &tg, sizeof tg) && tg) || (peek(mk + 0x210, &tg, sizeof tg) && tg))) {
					pr_rtti(tg, tc, sizeof tc);
					pr_rtti_parts(tg, parts, sizeof parts);
					logf_("PRACTICE nav %d   target %#llx '%s' parts:%s", n, (unsigned long long)tg, tc, parts[0] ? parts : " none");
					uintptr_t t2 = 0; /* (an object handle's object: +8, as the probe's) */
					if (peek(tg + 8, &t2, sizeof t2) && t2) {
						pr_rtti(t2, tc, sizeof tc);
						pr_rtti_parts(t2, parts, sizeof parts);
						logf_("PRACTICE nav %d   target+8 %#llx '%s' parts:%s", n, (unsigned long long)t2, tc, parts[0] ? parts : " none");
					}
				}
			}
		}
		n++;
		peek(node + 8, &nx, sizeof nx);
		node = nx;
	}
	logf_("PRACTICE t=%.3f nav: %d markers, %d with a place (%d logged, %d ammo/explosive/fire pit/bait)", t, n, placed,
	      logged < 60 ? logged : 60, skipped);
}

/* "practice status ground X Y Z" (a test word, the collect drill): the floors under a place before a trip there - a ray
 * straight down from 30 m over Y to 60 m under it and every surface it meets (6 at most, each ray from 5 cm under the
 * last hit; Talion skipped); read only, nothing moves. Far from Talion the world there may not be loaded: no hit */
static void pr_floor(const struct pr_me *m, int have, double t)
{
	float at[3], hit[3];
	for (int i = 0; i < 3; i++) {
		uint32_t b = atomic_load_explicit(&g_pr_at_req[i], memory_order_relaxed);
		memcpy(&at[i], &b, 4);
	}
	uintptr_t self = have ? m->obj : 0, obj = 0; /* (Talion: as the eye line's ray) */
	float from[3] = { at[0], at[1] + 3000.0f, at[2] }, end = at[1] - 6000.0f;
	char b[480];
	size_t o = 0;
	int n = 0;
	b[0] = 0;
	while (pr_ray_ok() && n < 6 && from[1] > end && pr_ray_down(from, from[1] - end, self, hit, &obj) && o < sizeof b - 64) {
		o += (size_t)snprintf(b + o, sizeof b - o, "%s%.0f (%+.1f m, object %#llx)", n ? ", " : " ", hit[1],
				      (double)(hit[1] - at[1]) / 100.0, (unsigned long long)obj);
		n++, from[1] = hit[1] - 5.0f;
	}
	float dx = have ? m->pos[0] - at[0] : 0, dz = have ? m->pos[2] - at[2] : 0;
	logf_("PRACTICE t=%.3f ground at (%.0f %.0f %.0f): %d surface(s) from 30 m over to 60 m under%s%s; Talion %.1f m away "
	      "(across)", t, at[0], at[1], at[2], n, n ? ":" : "", !pr_ray_ok() ? " (no ray: the site)" : b,
	      (double)(have ? sqrtf(dx * dx + dz * dz) / 100.0f : -1.0f));
}

/* "practice status look" (a test word, the collect drill; read only): what the game's look-at triggers (LookAtTrigger,
 * static: FUN_0279b7a0 walks the player list [0x4c47020, 0x4c47028), FUN_02799e90 drops a player whose +0x178 bit 0 is
 * set, FUN_0279b150 wants the vision mode's byte *(+0x10)+0x118 when the trigger's RequireVisionMode is set,
 * FUN_027982d0 / FUN_029a8130 test the point against the camera copy +0xbb0 pos, +0xbbc quat, +0xbcc FOVs) read of
 * each player now */
#define PRL_PLAYERS GADDR(PRL_PLAYERS, 0x4c47020)
static void pr_look(const struct pr_me *m, int have, double t)
{
	uintptr_t b = 0, e = 0;
	peek(PRL_PLAYERS, &b, sizeof b), peek(PRL_PLAYERS + 8, &e, sizeof e);
	int n = b && e >= b && e - b <= 64 ? (int)((e - b) / 8) : -1;
	logf_("PRACTICE t=%.3f look: the player list %#llx..%#llx: %d player(s); Talion's Player %#llx (the practice's)", t, (unsigned long long)b,
	      (unsigned long long)e, n, (unsigned long long)(have ? m->pl : 0));
	for (int i = 0; i < n && i < 8; i++) {
		uintptr_t p = 0, o = 0, v = 0;
		uint8_t f178 = 0, vis = 0;
		float cp[3] = { 0, 0, 0 }, cq[4] = { 0, 0, 0, 0 }, fov[2] = { 0, 0 }, op[3] = { 0, 0, 0 };
		peek(b + 8 * (uintptr_t)i, &p, sizeof p);
		if (!p)
			continue;
		peek(p + 0x178, &f178, 1), peek(p + 8, &o, sizeof o), peek(p + 0x10, &v, sizeof v);
		if (v)
			peek(v + 0x118, &vis, 1);
		if (o)
			peek(o + 8, op, sizeof op);
		peek(p + 0xbb0, cp, sizeof cp), peek(p + 0xbbc, cq, sizeof cq), peek(p + 0xbcc, fov, sizeof fov);
		float fx = 2.f * (cq[0] * cq[2] + cq[1] * cq[3]), fy = 2.f * (cq[1] * cq[2] - cq[0] * cq[3]),
		      fz = 1.f - 2.f * (cq[0] * cq[0] + cq[1] * cq[1]); /* R(q)(0,0,1): somvr_front_cam_dot's forward */
		logf_("PRACTICE look %d: player %#llx flags+0x178 %#x (bit 0 = the triggers skip him) vision byte %u; its object "
		      "%#llx at (%.0f %.0f %.0f); the camera copy at (%.0f %.0f %.0f) fwd %+.2f,%+.2f,%+.2f fov %.1f x %.1f deg", i,
		      (unsigned long long)p, f178, vis, (unsigned long long)o, op[0], op[1], op[2], cp[0], cp[1], cp[2], fx, fy, fz,
		      fov[0] * 57.29578f, fov[1] * 57.29578f);
	}
}

/* build 17: the practice page's refills (the user's ask): in the practice area only, the game's own calls (PRF_*), no
 * save (nothing here asks the game to save; its own checkpoints are as before). The bow's arrows to their max
 * (PRF_TOKENS_ADD: the HUD's pips and its collect sound) and its Focus - the energy the bow's slow motion and the
 * wraith's Radiance need, PC_SlowMo / PC_Radiance's MinEnergyToEnter (re-bow.md 2.2, 9) - to its max
 * (PRF_ENERGY_FULL); the wraith's abilities that cost arrows (Shadow Strike) take them from the same tokens */
static uintptr_t pr_res(const struct pr_me *m)
{
	uintptr_t res = m->pc ? peek_ptr(m->pc + BO_SC_RES) : 0;
	int32_t n = 0;
	return res && peek(res + BO_RES_COUNT, &n, sizeof n) ? res : 0;
}
static int pr_refill_ok(const struct pr_me *m, int have, const char *what, double t, const char *who)
{
	const char *no = !have ? "no player" : !g_pr.here ? "not in the practice area"
		       : g_pr.trip.phase != SOMVR_PRT_IDLE ? "a trip is on"
		       : m->life >= 0 && m->life != 2 ? "Talion is not up (dead or not in the world)" : NULL;
	if (!no)
		return 1;
	g_pr.fill_refused++;
	if (pr_log_ok(t))
		logf_("PRACTICE t=%.3f refill %s (%s): %s: nothing", t, what, who, no);
	return 0;
}
static void pr_refill_arrows(const struct pr_me *m, int have, double t, const char *who)
{
	if (!pr_refill_ok(m, have, "arrows", t, who))
		return;
	uintptr_t res = pr_res(m);
	int32_t n0 = -1, mx = -1, n1 = -1;
	uint8_t unl = 0, eunl = 0;
	float e0 = NAN, emax = NAN, e1 = NAN;
	if (!res || !peek(res + PRF_RES_MAX, &mx, sizeof mx) || !peek(res + BO_RES_COUNT, &n0, sizeof n0) ||
	    !peek(res + BO_RES_UNLIM, &unl, 1) || !peek(res + PRF_RES_ENERGY, &e0, sizeof e0) ||
	    !peek(res + PRF_RES_EMAX, &emax, sizeof emax) || !peek(res + PRF_RES_EUNLIM, &eunl, 1)) {
		g_pr.fill_refused++;
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f refill arrows (%s): no resource object (Combat+0x1c0): nothing", t, who);
		return;
	}
	int add = g_pr.ok_tokens && !unl && mx > 0 && mx < 1000 && n0 >= 0 && n0 < mx ? mx - n0 : 0;
	if (add)
		PRF_FN(PRF_TOKENS_ADD, prf_tokens_fn)(res, add);
	int ef = g_pr.ok_energy && isfinite(e0) && isfinite(emax) && emax > 0 && emax < 1e6f && e0 < emax;
	if (ef)
		PRF_FN(PRF_ENERGY_FULL, prf_energy_fn)(res, eunl);
	peek(res + BO_RES_COUNT, &n1, sizeof n1), peek(res + PRF_RES_ENERGY, &e1, sizeof e1);
	g_pr.fill_arrows++;
	if (pr_log_ok(t))
		logf_("PRACTICE t=%.3f refill arrows (%s): the arrows %d -> %d of %d%s (FUN_02720a90 %+d), the Focus %.1f -> %.1f "
		      "of %.1f%s (FUN_0271e2e0); no save", t, who, n0, n1, mx, unl ? " (unlimited)" : !g_pr.ok_tokens ?
		      " (PRF_TOKENS_ADD did not match)" : add ? "" : " (full)", add, (double)e0, (double)e1, (double)emax,
		      eunl ? " (unlimited)" : !g_pr.ok_energy ? " (PRF_ENERGY_FULL did not match)" : ef ? "" : " (full)");
}
static void pr_refill_health(const struct pr_me *m, int have, double t, const char *who)
{
	if (!pr_refill_ok(m, have, "health", t, who))
		return;
	float hp0 = NAN, mx = NAN, hp1 = NAN;
	if (!m->ch || !peek(m->ch + PR_CH_HEALTH, &hp0, sizeof hp0) || !peek(m->ch + PR_CH_HEALTH_MAX, &mx, sizeof mx) ||
	    !isfinite(hp0) || !isfinite(mx) || !(mx > 0 && mx < 1e6f)) {
		g_pr.fill_refused++;
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f refill health (%s): no CPlayerObj or no health (%g of %g): nothing", t, who,
			      (double)hp0, (double)mx);
		return;
	}
	int healed = 0;
	if (g_pr.ok_heal && hp0 > 0 && hp0 < mx) /* (0: dead - never brought back by us) */
		healed = PRF_FN(PRF_HEAL, prf_heal_fn)(m->ch + PR_CH_HEALTHC, mx - hp0) & 0xff;
	peek(m->ch + PR_CH_HEALTH, &hp1, sizeof hp1);
	g_pr.fill_health++;
	if (pr_log_ok(t))
		logf_("PRACTICE t=%.3f refill health (%s): Talion's health %.1f -> %.1f of %.1f%s (FUN_02763910 %s); no save", t,
		      who, (double)hp0, (double)hp1, (double)mx, !g_pr.ok_heal ? " (PRF_HEAL did not match)"
		      : !(hp0 > 0) ? " (dead: left alone)" : hp0 >= mx ? " (full)" : "", healed ? "healed" : "not called or full");
}

/* the test aids (headless proofs of the refills; the writer thread's word, the game thread's write): Talion's health
 * set to HP (1 .. the max) and the Focus to F (0 .. the max) - in the practice area only, as the refills */
static _Atomic int g_pr_mortal; /* build 27, `practice mortal on|off`: no practice min health while on (a test aid) */
static _Atomic uint32_t g_pr_aid_seq, g_pr_aid_kind, g_pr_aid_val; /* kind 1 hurt, 2 focus; val = the float's bits */
static uint32_t g_pr_aid_seen;
static void pr_aid_tick(const struct pr_me *m, int have, double t)
{
	uint32_t q = atomic_load_explicit(&g_pr_aid_seq, memory_order_acquire);
	if (q == g_pr_aid_seen)
		return;
	g_pr_aid_seen = q;
	uint32_t kind = atomic_load(&g_pr_aid_kind), vb = atomic_load(&g_pr_aid_val);
	float v, was = NAN, mx = NAN, now = NAN;
	memcpy(&v, &vb, sizeof v);
	if (!pr_refill_ok(m, have, kind == 1 ? "(the test aid hurt)" : "(the test aid focus)", t, "the trigger word"))
		return;
	uintptr_t res = kind == 2 ? pr_res(m) : 0, at = kind == 1 ? m->ch + PR_CH_HEALTH : res + PRF_RES_ENERGY;
	if ((kind == 1 && (!m->ch || !peek(m->ch + PR_CH_HEALTH_MAX, &mx, sizeof mx))) ||
	    (kind == 2 && (!res || !peek(res + PRF_RES_EMAX, &mx, sizeof mx))) || !peek(at, &was, sizeof was) ||
	    !(mx > 0 && mx < 1e6f))
		return;
	v = v < (kind == 1 ? 1.0f : 0.0f) ? (kind == 1 ? 1.0f : 0.0f) : v > mx ? mx : v;
	memcpy((void *)at, &v, sizeof v);
	peek(at, &now, sizeof now);
	kind == 1 ? g_pr.aid_hurt++ : g_pr.aid_focus++;
	logf_("PRACTICE t=%.3f %s %.1f -> %.1f of %.1f (a test aid: the trigger word)", t,
	      kind == 1 ? "Talion's health" : "the Focus", (double)was, (double)now, (double)mx);
}

/* ---- build 31 (re-xr-pipeline.md 136; the 11-02 session's mark 21: "point where I want it in kind of like a circle
 * guide and then just place the orc unaware"): Practice > Place orc. The sword hand's aim (its laser's line) to the
 * world within 40 m: the ground there (the ray down from 30 cm back along the aim, so a wall's face puts it at the
 * wall's foot) gets the gold ring and its post (the objective layer, as the wraith step's); with the panel closed a
 * fresh trigger press there puts an orc of the page's type on it, its back to Talion (unaware, as Spawn far orc's);
 * B, the button again or 30 s cancel. In the practice area only (pr_spawn_at's rules). ---------------------------- */
#define PR_PLACE_RANGE_CM 4000.0f
#define PR_PLACE_TIMEOUT_S 30.0
static struct {
	int on, type, armed, b_was, have, logs;
	double t0;
	float spot[3], dist;
	uint64_t starts, placed, cancels, timeouts, frames, frames_spot;
} g_ppl;
static int pr_place_on(void) { return g_ppl.on; }
static void pr_place_end(double t, const char *why)
{
	if (!g_ppl.on)
		return;
	g_ppl.on = 0;
	if (pr_log_ok(t))
		logf_("PRACTICE t=%.3f place: %s (%.1f s; frames %llu, with a spot %llu)", t, why, t - g_ppl.t0,
		      (unsigned long long)g_ppl.frames, (unsigned long long)g_ppl.frames_spot);
}
static void pr_place_start(int type, double t, const char *who)
{
	if (g_ppl.on) { /* the button again: off */
		g_ppl.cancels++;
		pr_place_end(t, "cancelled (the button again)");
		return;
	}
	memset(&g_ppl.spot, 0, sizeof g_ppl.spot);
	g_ppl.on = 1, g_ppl.type = type >= 0 && type < SOMVR_PRO_N ? type : g_pr.cur_orc, g_ppl.armed = 0, g_ppl.b_was = 1;
	g_ppl.t0 = t, g_ppl.have = 0, g_ppl.logs = 0, g_ppl.frames = g_ppl.frames_spot = 0, g_ppl.starts++;
	if (pr_log_ok(t))
		logf_("PRACTICE t=%.3f place (%s): aim the sword hand at the ground - the gold ring; close the panel, the trigger "
		      "puts an unaware %s there (its back to Talion); B, the button again or %.0f s cancel", t, who,
		      somvr_pr_orc_label(g_ppl.type), PR_PLACE_TIMEOUT_S);
}
/* the sword hand's aim in the world (cm): its origin and direction; 0 = not tracked */
static int pr_place_aim(sv_vec3 *o, sv_vec3 *d)
{
	const somvr_hand *hd = &g_inp.hands[SOMVR_HAND_R];
	const somvr_hand_in *hi = &g_inp.st.hand[SOMVR_HAND_R];
	if (!hd->valid)
		return 0;
	sv_vec3 p;
	sv_quat q;
	somvr_hand_game(hd, &p, &q, NULL, NULL);
	if ((hi->aim_flags & 3u) == 3u) /* the aim pose's turn (the laser's: OpenXR -Z = the game's +Z), mirrored as the grip's */
		q = sv_qnormalize((sv_quat){ -hi->aim.q[0], -hi->aim.q[1], hi->aim.q[2], hi->aim.q[3] });
	sv_vec3 f = somvr_rig_vec(&g_rig, g_fr.base, sv_qrot(q, (sv_vec3){ 0, 0, 1 }));
	float n = sqrtf(f.x * f.x + f.y * f.y + f.z * f.z);
	if (!(n > 1e-4f))
		return 0;
	*o = somvr_rig_point(&g_rig, g_fr.base, p), *d = (sv_vec3){ f.x / n, f.y / n, f.z / n };
	return 1;
}
static void pr_place_tick(const struct pr_me *m, int have, double t)
{
	if (!g_ppl.on)
		return;
	g_ppl.frames++;
	if (t - g_ppl.t0 > PR_PLACE_TIMEOUT_S) {
		g_ppl.timeouts++;
		pr_place_end(t, "timed out (nothing placed)");
		return;
	}
	const somvr_hand_in *hi = &g_inp.st.hand[SOMVR_HAND_R];
	int b = (hi->buttons & SOMVR_BTN_UPPER) != 0, panel = vm_open();
	if (b && !g_ppl.b_was && !panel) {
		g_ppl.cancels++;
		pr_place_end(t, "cancelled (B)");
		return;
	}
	g_ppl.b_was = b;
	sv_vec3 o, d;
	somvr_climb_hit h;
	memset(&h, 0, sizeof h);
	g_ppl.have = 0;
	if (have && pr_place_aim(&o, &d) && cl_ray(NULL, -1, o, d, PR_PLACE_RANGE_CM, &h) && h.hit) {
		float back[3] = { h.point.x - 30.0f * d.x, h.point.y - 30.0f * d.y, h.point.z - 30.0f * d.z }, g[3];
		uintptr_t gobj = 0;
		if (pr_ground(back, g, &gobj))
			memcpy(g_ppl.spot, g, sizeof g), g_ppl.have = 1, g_ppl.dist = dist3(g, m->pos), g_ppl.frames_spot++;
	}
	if (g_ppl.logs < 6 && t - g_ppl.t0 >= 0.5 * g_ppl.logs) {
		g_ppl.logs++;
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f place: the aim %s", t, g_ppl.have ? "on the ground (the spot below)" : "on nothing (no "
			      "ground within 40 m along the sword hand's aim)");
		if (g_ppl.have && pr_log_ok(t))
			logf_("PRACTICE t=%.3f place: the spot (%.0f %.0f %.0f), %.1f m from Talion, %.1f m below him", t,
			      g_ppl.spot[0], g_ppl.spot[1], g_ppl.spot[2], g_ppl.dist / 100.0f, (m->pos[1] - g_ppl.spot[1]) / 100.0f);
	}
	float tr = hi->trigger;
	if (panel || tr < 0.3f)
		g_ppl.armed = !panel;
	else if (g_ppl.armed && tr > 0.8f && g_ppl.have) {
		memcpy(g_pr_place_at, g_ppl.spot, sizeof g_pr_place_at);
		uint64_t n0 = g_pr.spawns;
		pr_spawn_at(m, g_ppl.type, t, "Place orc", 2);
		g_ppl.placed += g_pr.spawns > n0;
		pr_place_end(t, g_pr.spawns > n0 ? "placed" : "the spawn failed (its line above)");
	}
}
/* ob_frame (H4): the spot's gold ring and its post, as the wraith step's */
static int pr_place_entry(somvr_objective_frame *e)
{
	if (!g_ppl.on || !g_ppl.have || game_paused())
		return 0;
	somvr_th_map mp = { { g_fr.base.pos.x, g_fr.base.pos.y, g_fr.base.pos.z }, g_fr.base.yaw - g_rig.yaw0,
			    { g_rig.p0.x, g_rig.p0.y, g_rig.p0.z }, somvr_rig_scale(&g_rig) };
	float w[3] = { g_ppl.spot[0], g_ppl.spot[1], g_ppl.spot[2] }, w2[3] = { w[0] + 45.0f, w[1], w[2] }, x2[3],
	      wt[3] = { w[0], w[1] + 170.0f, w[2] };
	somvr_th_to_xr(&mp, w, e->pos), somvr_th_to_xr(&mp, w2, x2), somvr_th_to_xr(&mp, wt, e->top);
	e->n = 1, e->flags = SOMVR_OBF_LAND | SOMVR_OBF_LINE, e->id = 0;
	e->dist_m = sqrtf((x2[0] - e->pos[0]) * (x2[0] - e->pos[0]) + (x2[1] - e->pos[1]) * (x2[1] - e->pos[1]) +
			  (x2[2] - e->pos[2]) * (x2[2] - e->pos[2])); /* an orc's footprint: 45 cm */
	return 1;
}

/* build 37 (som-b37-cues, docs 169.9): New herb - the blink to a spot 70 cm short of the nearest herb the game offers
 * (an active, enabled 'Collectible_Flower*' nav marker; a picked one is off until the game's own respawn) within
 * SOMVR_PRACTICE_HERB_M (150) of Talion and not at his feet (1.2 m), on his side of it: the spot moves there (a trip, as
 * `practice go X Y Z`). Herbs are not respawned: the game's own spawner does that after its delay. */
static void pr_herb(const struct pr_me *m, int have, double t, const char *who)
{
	char why[160];
	const char *no = !have ? "no player in the world now" : g_pr.trip.phase != SOMVR_PRT_IDLE ? "a trip is on"
			 : check_site(&g_prn_sites[PRN_NAV_POS], why, sizeof why) ? "PRN_NAV_POS not matched (no herbs' places)" : NULL;
	float best = INFINITY, hp[3] = { 0, 0, 0 };
	char bn[40] = "";
	int n = 0, seen = 0;
	uintptr_t node = 0;
	float lim = 100.0f * g_pr.herb_m;
	if (!no) {
		peek(PRN_LIST + 8, &node, sizeof node);
		while (node && node != PRN_LIST && n++ < 1024) {
			uintptr_t mk = node - PRN_NODE, rec = 0, nx = 0;
			uint8_t ae[2] = { 0, 0 };
			char name[40] = "";
			peek(mk + PRN_REC, &rec, sizeof rec), peek(mk + PRN_ACTIVE, ae, sizeof ae);
			if (rec && ae[0] && ae[1])
				rx_rec_name(rec, name, sizeof name);
			if (!strncmp(name, "Collectible_Flower", 18)) {
				float pos[3] = { 0, 0, 0 };
				((prn_pos_fn)g_prn_sites[PRN_NAV_POS].target)(mk, pos);
				float d = dist3(pos, m->pos);
				seen++;
				if (vec_ok(pos, 1e7f) && d > 120 && d < lim && d < best)
					best = d, memcpy(hp, pos, sizeof hp), snprintf(bn, sizeof bn, "%s", name);
			}
			peek(node + 8, &nx, sizeof nx);
			node = nx;
		}
		if (!(best < lim))
			no = "no herb the game offers within SOMVR_PRACTICE_HERB_M (all picked: they come back after the game's delay)";
	}
	if (no) {
		g_pr.refused++;
		haptic(SOMVR_HAND_L, SOMVR_HX_NO), haptic(SOMVR_HAND_R, SOMVR_HX_NO);
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f new herb (%s) refused: %s (%d herbs seen)", t, who, no, seen);
		return;
	}
	float dx = m->pos[0] - hp[0], dz = m->pos[2] - hp[2], dl = hypotf(dx, dz);
	float at[3] = { hp[0] + (dl > 1 ? 70 * dx / dl : 70), hp[1] + 50, hp[2] + (dl > 1 ? 70 * dz / dl : 0) };
	if (pr_log_ok(t))
		logf_("PRACTICE t=%.3f new herb (%s): '%s' at (%.0f %.0f %.0f), %.1f m away (%d herbs seen); the spot 70 cm short of it "
		      "(%.0f %.0f %.0f)", t, who, bn, hp[0], hp[1], hp[2], best / 100.0f, seen, at[0], at[1], at[2]);
	memcpy(g_pr.spot, at, sizeof at);
	g_pr.here = g_pr.went = 0;
	pr_go(m, have, t, who);
}

static void pr_request(const struct pr_me *m, int have, int action, int orc, double t, const char *who)
{
	switch (action) {
	case SOMVR_PRA_ARROWS: pr_refill_arrows(m, have, t, who); break; /* build 17 */
	case SOMVR_PRA_HEALTH: pr_refill_health(m, have, t, who); break;
	case SOMVR_PRA_GO: pr_go(m, have, t, who); break;
	case SOMVR_PRA_BACK: pr_back(t, who); break;
	case SOMVR_PRA_SPAWN: pr_spawn(m, orc >= 0 && orc < SOMVR_PRO_N ? orc : g_pr.cur_orc, t, who); break;
	case SOMVR_PRA_GROUP: pr_group(m, orc >= 3 && orc <= SOMVR_PR_GROUP_MAX ? orc : g_pr.group_n, t, who); break; /* build 19; 27: 3..8 */
	case SOMVR_PRA_CAPTAIN: kp_spawn(m, t, who); break; /* build 22: SOMVR_CAPTAIN (somvr_captain_game.c) */
	case SOMVR_PRA_PLACE: pr_place_start(orc, t, who); break; /* build 31 */
	case SOMVR_PRA_CHARGE: /* build 36 (re-xr-pipeline.md 164): Charge execution (in the area only, as the refills) */
		if (pr_refill_ok(m, have, "the execution's charge", t, who)) {
			char w[200];
			int ok = xc_fill(m->pc, w, sizeof w);
			g_pr.fill_charge += ok;
			if (pr_log_ok(t))
				logf_("PRACTICE t=%.3f charge execution (%s): %s; no save", t, who, w);
			/* build 37 (SOMVR_PRACTICE_CHARGE_ORC, re-xr-pipeline.md 171): no orc within PR_CHARGE_NEAR_CM - one facing
			 * Talion PR_CHARGE_AT_CM ahead of the eyes (the finisher's ready comes on it; our far orc goes) */
			float nc = fn_near_cm(t), op[3];
			if (nc > 1e8f) /* (no finisher frame lately - the sword away: our own orc's distance) */
				nc = g_pr.orc_h && pr_orc_alive() && peek(g_pr.orc_h + OBJ_POS, op, sizeof op) && vec_ok(op, 1e7f)
					     ? hypotf(op[0] - m->pos[0], op[2] - m->pos[2])
					     : 1e9f;
			float at[3];
			if (ok && g_pr.charge_orc && nc > PR_CHARGE_NEAR_CM && fn_spot_ahead(PR_CHARGE_AT_CM, at)) {
				if (pr_log_ok(t))
					logf_("PRACTICE t=%.3f charge execution: no orc within %.0f cm (the nearest %.0f) - one facing Talion "
					      "%.0f cm ahead", t, (double)PR_CHARGE_NEAR_CM, nc > 1e8f ? -1.0 : (double)nc, (double)PR_CHARGE_AT_CM);
				memcpy(g_pr_place_at, at, sizeof at);
				pr_spawn_at(m, g_pr.cur_orc, t, "Charge execution: no orc near", 4);
			}
		}
		break;
	case SOMVR_PRA_FAR: /* build 18 */
		pr_spawn_at(m, orc >= 0 && orc < SOMVR_PRO_N ? orc : g_pr.cur_orc, t, who, 1);
		break;
	case SOMVR_PRA_HERB: pr_herb(m, have, t, who); break; /* build 37: New herb */
	case SOMVR_PRA_REMOVE:
		if (!g_pr.orc_h && pr_log_ok(t))
			logf_("PRACTICE t=%.3f remove (%s): no orc out", t, who);
		pr_remove_orc(who, t);
		break;
	case SOMVR_PRA_STATUS:
		pr_status(m, have, t);
		if (orc == 1)
			pr_navdump(m, have, t);
		else if (orc == 2)
			pr_floor(m, have, t);
		else if (orc == 3)
			pr_look(m, have, t);
		break;
	default: break;
	}
}

/* a new Player (a load): our orc, the trip, the area and the min health belong to the old world */
static void pr_new_player(uintptr_t pl, double t)
{
	if (g_pr.pl && pr_log_ok(t))
		logf_("PRACTICE t=%.3f a new player %#llx (was %#llx: a load): our orc, the trip and the area forgotten", t,
		      (unsigned long long)pl, (unsigned long long)g_pr.pl);
	g_pr.pl = pl;
	g_pr.grav_off = 0; /* (build 37: the old PlayerMovement's flag is gone with it) */
	pr_quiet_off(t, "a new player (a load)");
	g_pr.orc_h = g_pr.orc_npc = g_pr.orc_c = 0;
	g_pr_keep.n = 0;
	memset(&g_pr_grp, 0, sizeof g_pr_grp); /* build 19 */
	memset(&g_pr.trip, 0, sizeof g_pr.trip);
	g_pr.went = g_pr.here = g_pr.have_back = 0;
	g_pr.mh_on = 0, g_pr.mh_ch = 0;
	g_pr.advanced = g_pr.weather_set = 0, g_pr.light_step = 0;
	g_pr.world_t = 0;
	if (g_pr.pend_act) { /* SOMVR_PRACTICE_WAIT: the place it would go to belongs to the old world too */
		g_pr.wait_dropped++;
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f the waiting %s (%s) dropped: a new player (a load)", t,
			      somvr_pr_action_name(g_pr.pend_act), g_pr.pend_who);
		g_pr.pend_act = 0;
	}
}

/* SOMVR_PRACTICE_WAIT: the game is not back for a trip (paused, Talion not alive in the world, a scripted moment) */
static int pr_held(const struct pr_me *m, int have, uint32_t gate)
{
	return !have || (m->life >= 0 && m->life != 2) ||
	       (gate & (uint32_t)(SOMVR_PRG_PRES | SOMVR_PRG_CINE | SOMVR_PRG_SCRIPTED | SOMVR_PRG_SYNC | SOMVR_PRG_PAUSED));
}

/* a request from the trigger word or the menu: with SOMVR_PRACTICE_WAIT a trip (go, back) asked for while held waits
 * (the newest request wins), everything else at once */
static void pr_take(const struct pr_me *m, int have, uint32_t gate, int action, int orc, double t, const char *who)
{
	if (g_pr.wait_mode && (action == SOMVR_PRA_GO || action == SOMVR_PRA_BACK) &&
	    (g_pr.pend_act || pr_held(m, have, gate))) {
		char w[96];
		somvr_pr_gate_text(gate & ~(uint32_t)SOMVR_PRG_UNKNOWN, w, sizeof w);
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f %s (%s) waits for the game to be back (%s%s%s)%s", t, somvr_pr_action_name(action), who,
			      w, !have ? ", no player" : m->life >= 0 && m->life != 2 ? ", Talion not up" : "",
			      pr_held(m, have, gate) ? "" : ", a request waits already",
			      g_pr.pend_act ? ": it replaces the waiting one" : "");
		g_pr.pend_act = action, g_pr.pend_orc = orc, g_pr.pend_who = who, g_pr.pend_t = t, g_pr.pend_clear_t = 0;
		g_pr.waits++;
		return;
	}
	pr_request(m, have, action, orc, t, who);
}

/* ... and once a frame: the waiting trip starts PR_WAIT_CLEAR_S after the game is back */
static void pr_wait_tick(const struct pr_me *m, int have, uint32_t gate, double t)
{
	if (!g_pr.pend_act)
		return;
	if (pr_held(m, have, gate)) {
		g_pr.pend_clear_t = 0;
		return;
	}
	if (!g_pr.pend_clear_t)
		g_pr.pend_clear_t = t;
	if (t - g_pr.pend_clear_t < PR_WAIT_CLEAR_S)
		return;
	int a = g_pr.pend_act;
	g_pr.pend_act = 0, g_pr.wait_starts++;
	if (pr_log_ok(t))
		logf_("PRACTICE t=%.3f the waiting %s (%s): the game is back (waited %.1f s): now", t, somvr_pr_action_name(a),
		      g_pr.pend_who, t - g_pr.pend_t);
	pr_request(m, have, a, g_pr.pend_orc, t, g_pr.pend_who);
}

/* H4 (game thread), every call after the frame's body */
static void pr_frame(uintptr_t pl)
{
	if (!g_pr.on)
		return;
	if (!g_pr.tid)
		g_pr.tid = my_tid();
	if (g_pr.tid != my_tid()) {
		g_pr.other++;
		return;
	}
	double t = pr_now();
	g_pr.frames++;
	if (pl && pl != g_pr.pl)
		pr_new_player(pl, t);
	struct pr_me m;
	int have = pr_me(pl, &m);
	uint32_t gate = pr_gate(&m);
	/* the requests: the trigger word, the menu's buttons */
	uint32_t tw = atomic_load_explicit(&g_pr_trig, memory_order_acquire);
	if (somvr_vm_pr_seq(tw) != g_pr.trig_seen) {
		g_pr.trig_seen = somvr_vm_pr_seq(tw), g_pr.req_trig++;
		if (somvr_vm_pr_action(tw) == SOMVR_PRA_GO && atomic_load_explicit(&g_pr_at_has, memory_order_relaxed)) {
			float at[3];
			for (int i = 0; i < 3; i++) {
				uint32_t b = atomic_load_explicit(&g_pr_at_req[i], memory_order_relaxed);
				memcpy(&at[i], &b, 4);
			}
			if (pr_log_ok(t))
				logf_("PRACTICE t=%.3f the spot moved (the trigger word): (%.0f %.0f %.0f) -> (%.0f %.0f %.0f)", t,
				      g_pr.spot[0], g_pr.spot[1], g_pr.spot[2], at[0], at[1], at[2]);
			memcpy(g_pr.spot, at, sizeof at);
			g_pr.here = g_pr.went = 0; /* a trip from the old spot to the new: the way back stays the first place */
		}
		pr_take(&m, have, gate, somvr_vm_pr_action(tw), somvr_vm_pr_orc(tw) - 1, t, "the trigger word");
	}
	if (g_vm.on) {
		uint32_t mw = atomic_load_explicit(&g_vmo.practice, memory_order_acquire);
		if (somvr_vm_pr_seq(mw) != g_pr.menu_seen) {
			g_pr.menu_seen = somvr_vm_pr_seq(mw), g_pr.req_menu++;
			pr_take(&m, have, gate, somvr_vm_pr_action(mw), somvr_vm_pr_orc(mw), t, "the menu");
		}
	}
	pr_wait_tick(&m, have, gate, t);
	pr_aid_tick(&m, have, t); /* build 17: `practice hurt HP` / `practice focus F` */
	int pbusy = pr_pause_busy(t, game_paused()); /* SOMVR_PRACTICE_LOADHOLD (logged with it off too) */
	if (g_pr.trip.phase != SOMVR_PRT_IDLE)
		pr_trip_tick(&m, have, t, pbusy);
	/* the area: in it while within the radius after a trip there (walking out ends the protections, not the area).
	 * Build 36 (som-b36-polish, re-xr-pipeline.md 167.5; the 11-03 Windows session: Talion stood 100.0-100.2 m from the
	 * spot and crossed the edge 10 times in 24 s; each walk-out turned the ambient AI spawn manager on and each walk back
	 * removed what it had spawned - 104 ambient AI, a voice heard with no orc in sight among them): out only past
	 * radius + SOMVR_PRACTICE_EDGE_M, back in within the radius */
	float pdist = have ? hypotf(m.pos[0] - g_pr.spot[0], m.pos[2] - g_pr.spot[2]) : 0;
	int in = g_pr.went && have && pdist <= g_pr.radius_cm + (g_pr.here ? g_pr.edge_cm : 0);
	if (g_pr.went && have && in != g_pr.here) {
		g_pr.walked_out += !in;
		if (pr_log_ok(t))
			logf_("PRACTICE t=%.3f Talion %s the practice area (%.1f m from the spot; %s %.0f m)", t,
			      in ? "is back in" : "walked out of", (double)(pdist / 100.0f), in ? "in within" : "out past",
			      (double)((g_pr.radius_cm + (in ? 0 : g_pr.edge_cm)) / 100.0f));
		g_pr.here = in;
		if (!in)
			pr_quiet_off(t, "walked out of the area");
	}
	if (have) /* the min health from the start of a trip there to the end of the trip back, while in the area; build 22:
		   * not while a captain of the save is out (SOMVR_PRACTICE_CAPTAIN_STAKES: he can kill Talion, as anywhere) */
		pr_mh(&m, (g_pr.here || g_pr.trip.phase != SOMVR_PRT_IDLE) && !kp_stakes_now() && !atomic_load(&g_pr_mortal), t);
	/* SOMVR_PRACTICE=auto: once, auto_s after the player is first in the world (alive, no scripted moment, not paused) */
	if (g_pr.mode == SOMVR_PR_AUTO && !g_pr.auto_done) {
		int ready = have && (m.life < 0 || m.life == 2) && !(gate & ~(uint32_t)SOMVR_PRG_UNKNOWN);
		if (!ready)
			g_pr.world_t = 0;
		else if (!g_pr.world_t)
			g_pr.world_t = t;
		else if (t - g_pr.world_t >= g_pr.auto_s) {
			g_pr.auto_done = 1, g_pr.autos++;
			pr_go(&m, have, t, "auto");
		}
	}
	pr_orc_tick(&m, t);
	kp_frame(&m, have, t); /* build 22: SOMVR_CAPTAIN - our captain's fate, the test words */
	pr_group_tick(t); /* build 19 */
	pr_far_tick(&m, t); /* build 18 */
	pr_place_tick(&m, have, t); /* build 31: Place orc */
	static uint32_t put_seen; /* b35 (re-xr-pipeline.md 160): `practice put X Y Z [TYPE]` */
	uint32_t ps = atomic_load_explicit(&g_pr_put_seq, memory_order_acquire);
	if (ps != put_seen) {
		put_seen = ps;
		for (int i = 0; i < 3; i++) {
			uint32_t b = atomic_load_explicit(&g_pr_put_at[i], memory_order_relaxed);
			memcpy(&g_pr_place_at[i], &b, 4);
		}
		int ty = (int)atomic_load_explicit(&g_pr_put_type, memory_order_relaxed);
		pr_spawn_at(&m, ty >= 0 && ty < SOMVR_PRO_N ? ty : g_pr.cur_orc, t, "practice put", 3);
	}
	if (g_pr.here && have && t >= g_pr.next_clear) {
		g_pr.next_clear = t + 1.0;
		if (!(gate & ~(uint32_t)SOMVR_PRG_TRIP)) /* the clear's gate: never in a scripted moment */
			pr_quiet_tick(t, g_pr.quiet_on ? "back in the area" : "in the area");
		pr_clear(&m, gate, t);
	}
	pr_light_tick(t);
	if (g_vm.on)
		atomic_store_explicit(&g_vmo.pstate,
				      (g_pr.here ? SOMVR_VMA_PR_HERE : 0u) | (g_pr.have_back ? SOMVR_VMA_PR_BACK : 0u) |
					      (g_pr.orc_h || pr_group_out() ? SOMVR_VMA_PR_ORC : 0u) |
					      (g_pr.trip.phase != SOMVR_PRT_IDLE || g_pr.pend_act ? SOMVR_VMA_PR_BUSY : 0u),
				      memory_order_release);
}

/* H4's stereo path (eye 0's record open), with SOMVR_DASH's blink: the trip's black (the larger level wins) */
static void pr_fade(void)
{
	if (!g_pr.on || !g_fr.rec || g_pr.trip.phase == SOMVR_PRT_IDLE)
		return;
	float b = somvr_pr_trip_fade(&g_pr.trip, pr_tc(), pr_now());
	uint32_t lv = (uint32_t)(b * 255 + 0.5f), was = somvr_rec_fade(g_fr.rec->flags);
	if (b > 0 && lv > was)
		g_fr.rec->flags = (g_fr.rec->flags & ~SOMVR_REC_FADE_MASK) | lv << SOMVR_REC_FADE_SHIFT;
}

/* the trigger word "practice ..." (writer thread): the text after "practice "; the TRIGGER line's word */
static const char *pr_word(const char *w)
{
	somvr_pr_req r;
	if (!g_pr.on)
		return "practice (SOMVR_PRACTICE is off: ignored)";
	float av;
	char ak[8], tail;
	if (!strncmp(w, "weather ", 8)) { /* build 36 (a test aid): GameServerShell.ChangeWeather DEF now (pr_light_tick) */
		const char *d = w + 8;
		size_t n = strcspn(d, " \r\n");
		int ok = n > 0 && n < sizeof g_pr_wx;
		for (size_t i = 0; ok && i < n; i++)
			ok = (d[i] >= 'A' && d[i] <= 'Z') || (d[i] >= 'a' && d[i] <= 'z') || (d[i] >= '0' && d[i] <= '9') || d[i] == '_';
		if (!ok || atomic_load(&g_pr_wx_pend))
			return "practice weather DEF (a Weather/Definition name; one at a time: ignored)";
		memcpy(g_pr_wx, d, n), g_pr_wx[n] = 0;
		atomic_store_explicit(&g_pr_wx_pend, 1, memory_order_release);
		return "practice weather (a test aid: ChangeWeather on the game thread)";
	}
	if (!strncmp(w, "mortal ", 7)) { /* build 27 (a test aid): the game's own min health back while on (Talion can die) */
		atomic_store(&g_pr_mortal, !strcmp(w + 7, "on"));
		return atomic_load(&g_pr_mortal) ? "practice mortal on (the game's own min health: Talion can die)" : "practice mortal off";
	}
	if (sscanf(w, " %7s %f %c", ak, &av, &tail) == 2 && (!strcmp(ak, "hurt") || !strcmp(ak, "focus")) && isfinite(av)) {
		uint32_t b; /* build 17's test aids: health / Focus set (pr_aid_tick) */
		memcpy(&b, &av, sizeof b);
		atomic_store(&g_pr_aid_kind, ak[0] == 'h' ? 1u : 2u), atomic_store(&g_pr_aid_val, b);
		atomic_fetch_add_explicit(&g_pr_aid_seq, 1, memory_order_release);
		return ak[0] == 'h' ? "practice hurt" : "practice focus";
	}
	float pa[3];
	char pt[16] = "";
	int pn = sscanf(w, " put %f %f %f %15s", &pa[0], &pa[1], &pa[2], pt);
	if (pn >= 3 && isfinite(pa[0]) && isfinite(pa[1]) && isfinite(pa[2])) { /* b35 (160, a test aid): an orc at a spot */
		int ty = pn == 4 ? somvr_pr_orc_parse(pt) : -1;
		if (pn == 4 && ty < 0)
			return "practice put (not a type: ignored)";
		for (int i = 0; i < 3; i++) {
			uint32_t b;
			memcpy(&b, &pa[i], 4);
			atomic_store_explicit(&g_pr_put_at[i], b, memory_order_relaxed);
		}
		atomic_store_explicit(&g_pr_put_type, (uint32_t)ty, memory_order_relaxed);
		atomic_fetch_add_explicit(&g_pr_put_seq, 1, memory_order_release);
		return "practice put (an orc at the spot, facing Talion)";
	}
	if (!somvr_pr_word(w, &r))
		return "practice (not go|back|spawn [infantry|shield|berserker|caragor|ghul|graug]|far [TYPE]|remove|status|"
		       "arrows|health|hurt HP|focus F: ignored)";
	if (r.have_at) /* the spot first: the trig's release publishes it */
		for (int i = 0; i < 3; i++) {
			uint32_t b;
			memcpy(&b, &r.at[i], 4);
			atomic_store_explicit(&g_pr_at_req[i], b, memory_order_relaxed);
		}
	atomic_store_explicit(&g_pr_at_has, r.have_at, memory_order_relaxed);
	uint32_t o = atomic_load(&g_pr_trig);
	atomic_store_explicit(&g_pr_trig, somvr_vm_pr_pack(somvr_vm_pr_seq(o) + 1, r.action, r.orc + 1), memory_order_release);
	static const char *const n[SOMVR_PRA_N] = { "practice",        "practice go",     "practice back",
						    "practice spawn",  "practice remove", "practice status",
						    "practice arrows", "practice health", "practice far" };
	return n[r.action];
}

/* vm_start: the practice page's tab */
static uint32_t pr_avail(void) { return g_pr.on ? SOMVR_VMA_PRACTICE : 0u; }

/* somvr_start, before the menu: the switch, the sites, the parts, the CONFIG line */
static void pr_start(const struct site_spec *sites, int *bad)
{
	static const char *const clear_w[] = { "0", "armed", "all" }, *const day_w[] = { "0", "weather", "day" },
			  *const sky_w[] = { "dry", "clear", "always" };
	memset(&g_pr, 0, sizeof g_pr);
	const char *v = getenv("SOMVR_PRACTICE");
	if (v && v[0] && !somvr_pr_mode_parse(v, &g_pr.mode)) {
		logf_("WARN SOMVR_PRACTICE=%s is not 0|1|auto: off", v);
		*bad = 1;
	}
	if (g_pr.mode == SOMVR_PR_OFF)
		return;
	g_pr.want = 1;
	g_pr_sites = sites;
	g_pr.spot[0] = 40400, g_pr.spot[1] = 3900, g_pr.spot[2] = -21600; /* re-combat-spawn.md 3's recipe (RT: quiet) */
	env_vec("SOMVR_PRACTICE_AT", g_pr.spot, 3, -1e6f, 1e6f, bad);
	g_pr.radius_cm = 100 * env_float("SOMVR_PRACTICE_RADIUS_M", 100, 10, 1000, bad);
	g_pr.herb_m = env_float("SOMVR_PRACTICE_HERB_M", 150, 10, 1000, bad); /* build 37: New herb's reach */
	g_pr.edge_cm = 100 * env_float("SOMVR_PRACTICE_EDGE_M", 10, 0, 100, bad); /* build 36 (0 = build 35's one edge) */
	g_pr.clear = env_word("SOMVR_PRACTICE_CLEAR", clear_w, 3, SOMVR_PRC_ARMED);
	g_pr.clear_cm = 100 * env_float("SOMVR_PRACTICE_CLEAR_M", 60, 5, 200, bad);
	g_pr.day = env_word("SOMVR_PRACTICE_DAY", day_w, 3, PR_DAY_FULL);
	g_pr.sky = env_word("SOMVR_PRACTICE_SKY", sky_w, 3, SOMVR_PRSKY_DRY); /* build 36 (som-b36-winlooks) */
	copy_env("SOMVR_PRACTICE_WEATHER", g_pr.weather, sizeof g_pr.weather);
	int wok = g_pr.weather[0] != 0;
	for (const char *c = g_pr.weather; *c && wok; c++)
		wok = (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_';
	if (!wok) {
		if (g_pr.weather[0])
			logf_("WARN SOMVR_PRACTICE_WEATHER=%s is not a record name: DayNoRain", g_pr.weather), *bad = 1;
		snprintf(g_pr.weather, sizeof g_pr.weather, "DayNoRain");
	}
	g_pr.minhealth = env_float("SOMVR_PRACTICE_MINHEALTH", 20, 0, 100000, bad);
	g_pr.ahead_cm = env_float("SOMVR_PRACTICE_AHEAD_CM", 350, 150, 2000, bad);
	g_pr.charge_orc = env_word("SOMVR_PRACTICE_CHARGE_ORC", (const char *const[]){ "off", "on" }, 2, 0); /* build 37 */
	logf_("CONFIG practice charge_orc=%s (SOMVR_PRACTICE_CHARGE_ORC, build 37: Practice > Charge execution with no orc within "
	      "%.0f cm spawns one facing Talion %.0f cm ahead of the eyes)", g_pr.charge_orc ? "on" : "off", (double)PR_CHARGE_NEAR_CM,
	      (double)PR_CHARGE_AT_CM);
	g_pr.group_n = (int)env_float("SOMVR_PRACTICE_GROUP_N", 4, 3, SOMVR_PR_GROUP_MAX, bad); /* build 19: "Spawn group"; 27: to 8 */
	g_pr.far_cm = env_float("SOMVR_PRACTICE_FAR_CM", 1800, 800, 4000, bad); /* build 18: Spawn far orc (unaware) */
	g_pr.auto_s = env_float("SOMVR_PRACTICE_AUTO_S", 5, 0, 600, bad);
	const char *ot = getenv("SOMVR_PRACTICE_ORC");
	g_pr.def_orc = ot && ot[0] ? somvr_pr_orc_parse(ot) : SOMVR_PRO_INFANTRY;
	if (g_pr.def_orc < 0) {
		logf_("WARN SOMVR_PRACTICE_ORC=%s is not infantry|shield|berserker: infantry", ot);
		g_pr.def_orc = SOMVR_PRO_INFANTRY, *bad = 1;
	}
	g_pr.cur_orc = g_pr.def_orc;
	static const char *const onoff_w[] = { "0", "1" };
	g_pr.quiet_mode = env_word("SOMVR_PRACTICE_QUIET", onoff_w, 2, 1);
	g_pr.wait_mode = env_word("SOMVR_PRACTICE_WAIT", onoff_w, 2, 0);
	g_pr.load_mode = env_word("SOMVR_PRACTICE_LOADHOLD", onoff_w, 2, 1);
	somvr_pr_tcfg_default(&g_pr.tcfg);
	g_pr.tcfg_down = g_pr.tcfg, g_pr.tcfg_down.hold_s = 0.2f;
	g_pr.tcfg_quick = g_pr.tcfg_down, g_pr.tcfg_quick.fade_out_s = 0.12f, g_pr.tcfg_quick.settle_s = 0.1f;
	g_pr.tcfg_quick.hold_s = 0.0f, g_pr.tcfg_quick.fade_in_s = 0.2f;
	/* build 37 (som-b37-practrip, re-xr-pipeline.md 27.10): a trip lands only on ground; the climb's blinks to a ledge
	 * need ground under him, not the stand on it (a narrow top: the ray may pass it) */
	g_pr.ground_on = env_word("SOMVR_PRACTICE_GROUND", onoff_w, 2, 1);
	static const char *const hold_w[] = { "pin", "grav" };
	g_pr.ground_hold = env_word("SOMVR_PRACTICE_GROUND_HOLD", hold_w, 2, 0);
	g_pr.tcfg_down.need_on = 0, g_pr.tcfg_quick.need_on = 0;
	g_pr.tcfg.ground_wait_s = env_float("SOMVR_PRACTICE_GROUND_WAIT_S", 5, 2, 600, bad); /* (a trip there or back) */
	g_pr.ground_last = SOMVR_PRGD_UNKNOWN;
	/* the sites: ours, the console's, the fallback's reads of the gate */
	char why[512];
	uint32_t ok = 0;
	for (int i = 0; i < PRS_N; i++) {
		int k = check_site(&sites[i], why, sizeof why) == 0;
		ok |= (uint32_t)k << i;
		logf_("PRACTICE site %s %s %#llx -> %#llx: %s", sites[i].name, k ? "ok" : "MISMATCH", (unsigned long long)sites[i].addr,
		      (unsigned long long)sites[i].target, why);
	}
	uint32_t okq = 0;
	for (int i = 0; i < PRQ_N && g_pr.quiet_mode; i++) {
		int k = check_site(&g_prq_sites[i], why, sizeof why) == 0;
		okq |= (uint32_t)k << i;
		logf_("PRACTICE site %s %s %#llx -> %#llx: %s", g_prq_sites[i].name, k ? "ok" : "MISMATCH",
		      (unsigned long long)g_prq_sites[i].addr, (unsigned long long)g_prq_sites[i].target, why);
	}
	g_pr.ok_quiet = okq == (1u << PRQ_N) - 1;
	uint32_t okf = 0; /* build 17: the refills' calls */
	for (int i = 0; i < PRF_N; i++) {
		int k = check_site(&g_prf_sites[i], why, sizeof why) == 0;
		okf |= (uint32_t)k << i;
		logf_("PRACTICE site %s %s %#llx -> %#llx: %s", g_prf_sites[i].name, k ? "ok" : "MISMATCH",
		      (unsigned long long)g_prf_sites[i].addr, (unsigned long long)g_prf_sites[i].target, why);
	}
	g_pr.ok_tokens = okf >> PRF_TOKENS_ADD & 1, g_pr.ok_energy = okf >> PRF_ENERGY_FULL & 1;
	g_pr.ok_heal = okf >> PRF_HEAL & 1;
	if (okf != (1u << PRF_N) - 1)
		logf_("WARN SOMVR_PRACTICE: a refill site did not match (tokens %d, energy %d, heal %d): that refill does nothing",
		      g_pr.ok_tokens, g_pr.ok_energy, g_pr.ok_heal), *bad = 1;
	int con = check_site(&g_es_sites[ES_RUNSTRING], why, sizeof why) == 0;
	logf_("PRACTICE site %s %s (the console: Teleport, AdvanceTimeOfDay, ChangeWeather): %s", g_es_sites[ES_RUNSTRING].name,
	      con ? "ok" : "MISMATCH", why);
	static const int gate_fs[] = { FS_PRES_CAM, FS_CINE, FS_COMBAT, FS_SERVER, FS_SYNC, FS_SYNC_REC };
	g_pr.ok_gate = 1;
	for (size_t i = 0; i < sizeof gate_fs / sizeof gate_fs[0]; i++)
		g_pr.ok_gate &= check_site(&g_fb_sites[gate_fs[i]], why, sizeof why) == 0;
	g_pr.ok_mount = check_site(&g_fb_sites[FS_MOUNT], why, sizeof why) == 0;
	g_pr.ok_life = check_site(&g_fb_sites[FS_LIFE], why, sizeof why) == 0;
#define PRB(s) (1u << PRS_##s)
	g_pr.ok_mh = (ok & PRB(MINHEALTH)) != 0;
	g_pr.ok_ray = (ok & PRB(RAY)) != 0;
	g_pr.ok_remove = (ok & PRB(REMOVE)) != 0;
	g_pr.ok_spawn = (ok & (PRB(BYNAME) | PRB(SPAWN) | PRB(ZONE) | PRB(LEVEL) | PRB(RAY) | PRB(SETZONE) | PRB(REMOVE))) ==
			(PRB(BYNAME) | PRB(SPAWN) | PRB(ZONE) | PRB(LEVEL) | PRB(RAY) | PRB(SETZONE) | PRB(REMOVE));
	g_pr.ok_light = (ok & (PRB(WEATHER) | PRB(TODNAME))) == (PRB(WEATHER) | PRB(TODNAME));
	g_pr.ok_clear = g_pr.ok_remove && g_pr.ok_gate && g_pr.ok_mount;
#undef PRB
	if (!con) {
		logf_("WARN SOMVR_PRACTICE: the console site ES_RUNSTRING did not match: no practice area (no teleport)");
		*bad = 1;
		return;
	}
	if (!g_pr.ok_mh)
		logf_("WARN SOMVR_PRACTICE: PR_SETMINHEALTH did not match: Talion CAN die in the practice area");
	if (!g_pr.ok_spawn)
		logf_("WARN SOMVR_PRACTICE: the spawn's sites did not all match: no orcs on demand");
	if (!g_pr.ok_clear && g_pr.clear != SOMVR_PRC_OFF)
		logf_("WARN SOMVR_PRACTICE: %s: the area is not kept clear", !g_pr.ok_remove ? "PR_OBJSYS_REMOVE did not match" :
		      "the scripted-moment gate's or the mount's fallback sites (FB_PRES_CAM, FB_CINE, FB_COMBAT, FB_SERVER, FB_SYNC, "
		      "FB_SYNC_REC, FB_MOUNT) did not match");
	if (!g_pr.ok_light && g_pr.day != PR_DAY_OFF)
		logf_("WARN SOMVR_PRACTICE: PR_WEATHER/PR_TOD_NAME did not match: the light is left as it is");
	if (g_pr.quiet_mode && !g_pr.ok_quiet)
		logf_("WARN SOMVR_PRACTICE: the spawn manager's sites (PRQ_*) did not all match: patrols keep spawning in the area "
		      "(the keep-clear pass removes them)");
	g_pr.on = 1;
	logf_("CONFIG practice: %s; the spot (%.0f %.0f %.0f) cm (RT: re-combat-spawn.md 3's slave-group ground, P9's teleport), "
	      "the area %.0f m (out past %.0f m: SOMVR_PRACTICE_EDGE_M); a blink there and back (fade %.2f/%.2f s, settle %.2f s, hold %.2f s, %d teleports at most); "
	      "min health %g%s; clear %s within %.0f m every 1 s%s (never in a presentation, a cinematic or scripted camera, a "
	      "sync, the pause); quiet %s; wait %s; light %s (%s; sky %s: %s; the weather with the teleport)%s; load hold %s; "
	      "orcs %.0f cm ahead (the far orc %.0f cm, its back to Talion), the type %s (%s), spawn %s; auto after %.1f s; "
	      "the menu's practice page and the trigger words \"practice go|back|spawn [TYPE]|far [TYPE]|remove|status\"",
	      somvr_pr_mode_name(g_pr.mode), g_pr.spot[0], g_pr.spot[1], g_pr.spot[2], (double)(g_pr.radius_cm / 100),
	      (double)((g_pr.radius_cm + g_pr.edge_cm) / 100), (double)g_pr.tcfg.fade_out_s, (double)g_pr.tcfg.fade_in_s, (double)g_pr.tcfg.settle_s, (double)g_pr.tcfg.hold_s,
	      g_pr.tcfg.tries, (double)g_pr.minhealth, g_pr.ok_mh ? "" : " (OFF: its site)", clear_w[g_pr.clear],
	      (double)(g_pr.clear_cm / 100), g_pr.ok_clear ? "" : " (OFF: its sites)",
	      !g_pr.quiet_mode ? "off (SOMVR_PRACTICE_QUIET=0)" : g_pr.ok_quiet ? "on (the ambient AI spawn manager off and its "
	      "AI removed while Talion is in the area; given back when he leaves)" : "OFF (its sites)",
	      g_pr.wait_mode ? "on (SOMVR_PRACTICE_WAIT: go and back asked for in the pause, a death or a scripted moment wait "
	      "until the game is back 1 s)" : "off (a trip starts at once, its black even over the pause menu)",
	      day_w[g_pr.day], g_pr.weather, sky_w[g_pr.sky], g_pr.sky == SOMVR_PRSKY_ALWAYS ? "every other day weather "
	      "changed (a test)" : g_pr.sky == SOMVR_PRSKY_CLEAR ? "a wet or overcast day changed, a clear one kept" :
	      "a wet day changed, a dry one kept", g_pr.ok_light ? "" : " (OFF: its sites)",
	      g_pr.load_mode ? "on (the black holds for the game's pause after our ChangeWeather: up to 3.6 s for it to come, "
	      "until it ends, at most 5.5 s after the arrival)" : "off (SOMVR_PRACTICE_LOADHOLD=0: logged only)",
	      (double)g_pr.ahead_cm, (double)g_pr.far_cm, somvr_pr_orc_label(g_pr.def_orc),
	      somvr_pr_orc_template(g_pr.def_orc), g_pr.ok_spawn ? "on" : "OFF (its sites)", (double)g_pr.auto_s);
	logf_("CONFIG practice ground=%s hold=%s wait=%.1f s (SOMVR_PRACTICE_GROUND, _HOLD=pin|grav, _WAIT_S; build 37): %s",
	      g_pr.ground_on ? "on" : "off",
	      g_pr.ground_hold ? "grav (his gravity off while nothing is under him; the pins the backstop)" : "pin",
	      (double)g_pr.tcfg.ground_wait_s,
	      !g_pr.ground_on ? "the trips' rules before build 37 (3 teleports, then the fade-in where Talion is)"
	      : !g_pr.ok_ray ? "OFF (the ray's site): the rules before build 37"
	      : "a trip lands only on ground - a ray down 40 m from his root each frame in the black; nothing there (the world "
		"not streamed in yet: a far Teleport never streams it in) = the black holds and he is kept at the target (no "
		"try counted), at most SOMVR_PRACTICE_GROUND_WAIT_S, then a trip there goes back where he was; the arrival "
		"standing on it (the climb's blinks: ground under him); the fall rules (SOMVR_WALK_GROUND) wait during a trip "
		"and 2 s after it");
}

/* somvr_fini */
static void pr_end_log(void)
{
	if (!g_pr.want)
		return;
	logf_("PRACTICE END %s: frames %llu (other thread %llu); trips there %llu back %llu down %llu, arrived %llu, failed %llu, teleports "
	      "%llu, auto %llu; requests trigger %llu menu %llu refused %llu waited %llu (started %llu, dropped %llu%s); orcs spawned %llu (failed %llu) removed %llu down "
	      "%llu; clear passes %llu held %llu removed %llu; quiet on %llu again %llu off %llu (ambient AI removed %llu%s); "
	      "walked out %llu; min health set %llu again %llu back %llu; light day %llu weather %llu restored %llu dry kept %llu; "
	      "its pause awaited %llu came %llu (in view %llu, at most %.2f s after, %.2f s long) none %llu; %llu lines "
	      "suppressed", g_pr.on ? "on" : "off (see the WARN)",
	      (unsigned long long)g_pr.frames, (unsigned long long)g_pr.other, (unsigned long long)g_pr.trips[PR_TRIP_GO],
	      (unsigned long long)g_pr.trips[PR_TRIP_BACK], (unsigned long long)g_pr.trips[PR_TRIP_DOWN], (unsigned long long)g_pr.arrived, (unsigned long long)g_pr.failed,
	      (unsigned long long)g_pr.teleports, (unsigned long long)g_pr.autos, (unsigned long long)g_pr.req_trig,
	      (unsigned long long)g_pr.req_menu, (unsigned long long)g_pr.refused, (unsigned long long)g_pr.waits,
	      (unsigned long long)g_pr.wait_starts, (unsigned long long)g_pr.wait_dropped,
	      g_pr.pend_act ? ", one still waiting" : "", (unsigned long long)g_pr.spawns,
	      (unsigned long long)g_pr.spawn_fail, (unsigned long long)g_pr.removes, (unsigned long long)g_pr.killed,
	      (unsigned long long)g_pr.passes, (unsigned long long)g_pr.held, (unsigned long long)g_pr.cleared,
	      (unsigned long long)g_pr.quiet_on, (unsigned long long)g_pr.quiet_again, (unsigned long long)g_pr.quiet_off,
	      (unsigned long long)g_pr.quiet_removed, g_pr.quiet ? "; still off at the end" : "",
	      (unsigned long long)g_pr.walked_out, (unsigned long long)g_pr.mh_sets, (unsigned long long)g_pr.mh_reasserts,
	      (unsigned long long)g_pr.mh_restores, (unsigned long long)g_pr.advances, (unsigned long long)g_pr.weathers,
	      (unsigned long long)g_pr.restores, (unsigned long long)g_pr.pw_dry, (unsigned long long)g_pr.pw_waits,
	      (unsigned long long)g_pr.pw_pauses, (unsigned long long)g_pr.pw_open, g_pr.pw_after_max, g_pr.pw_len_max,
	      (unsigned long long)g_pr.pw_none, (unsigned long long)g_pr.suppressed);
	logf_("PRACTICE END ground (build 37): trips that waited for the ground %llu (the longest %.2f s), pins %llu, never "
	      "came %llu (back where he was %llu); the hold %s: gravity held off %llu times (%llu frames)%s",
	      (unsigned long long)g_pr.ground_waits, g_pr.ground_wait_max, (unsigned long long)g_pr.ground_pins,
	      (unsigned long long)g_pr.ground_none, (unsigned long long)g_pr.ground_backs, g_pr.ground_hold ? "grav" : "pin",
	      (unsigned long long)g_pr.grav_holds, (unsigned long long)g_pr.grav_frames, g_pr.grav_off ? ", STILL OFF" : "");
	logf_("PRACTICE END refills: arrows + focus %llu, health %llu, refused %llu (not in the area, no player, a trip); test "
	      "aids hurt %llu focus %llu; far orcs %llu (noticed Talion %llu); groups %llu (of %d)",
	      (unsigned long long)g_pr.fill_arrows, (unsigned long long)g_pr.fill_health, (unsigned long long)g_pr.fill_refused,
	      (unsigned long long)g_pr.aid_hurt, (unsigned long long)g_pr.aid_focus, (unsigned long long)g_pr.far_spawns,
	      (unsigned long long)g_pr.far_noticed, (unsigned long long)g_pr.groups, g_pr.group_n);
}
