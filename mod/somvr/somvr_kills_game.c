/* somvr_kills_game.c - SOMVR_KILLS=on|off (build 31, docs/re-xr-pipeline.md 133, handoff/HANDOFF-combat.md "build 31"):
 * kills the game's way in VR. Included by somvr.c after the combat, execute, verbs, stealth, beasts, captain and abil
 * code (it uses their helpers); the pure part is somvr_kills.c.
 *
 * A kill by the player's own hand - the ordinary killing blow (cb_hit: the hit dealt with the orc's min health at 1, so
 * the blow that would kill is known exactly), the execution (xc_contact), the stealth kill (stl_deal, and the kill the
 * game offers where build 30 pressed X: vb_contact), the downed orc's stab (SOMVR_ABIL) - is handed to kl_take before
 * the game's Combat_DealHit. kl_take picks the victim's half (the record's own sync's TargetAttack when it has one: the
 * game's own pairing; else the table by the blow: side, kind, part, state) and starts it on the orc alone:
 * FUN_026ed9b0(orc Combat, node 0, the AI_Bip_PC_* record, Talion's HOBJECT, 0) - the plain start wrapper (NPC::vf_92's,
 * the sync's FUN_02807d20's) into Combat_StartAttack. No sync is made: nothing aligns or moves Talion, no camera, no
 * presentation; Talion's half (the SourceAttack) is never started. The orc's navigation is held as the sync holds it
 * (FUN_02972f90(its AI, 0, 2): the sync start's bit 2 of AI+0xe98). The orc is placed to the player, never the reverse:
 * turned (and slid only if inside min_cm) at most 45 deg / 30 cm over 150 ms, through the NPC movement's slot
 * (somvr_w_aimove -> kl_aim, after the game's step; the fight space and the body hold skip it while its kill plays).
 *
 * The death is the game's: the animation's ragdoll keys (DELAYRAGDOLL/RESUMERAGDOLL/ALLOWRAGDOLL) are a sync action's
 * flag (FUN_02804050 from Combat_OnModelString+0x766: sync+0x238), so without a sync nothing holds a ragdoll - the
 * kill is ours to time, from the pose (the victim's record stays on +0x4d0 past its end): when the half is cut (the
 * orc's attack leaves the record: its own AI's avoid or melee, 0.3-1.7 s on a fighting orc), down (its Head node 25 cm
 * lower and no longer falling), not playing (the head moved under 6 cm in 0.35 s), a fall, or 2 s: Combat_DealHit(the torso, Talion's Combat, the
 * orc, the record, forceKill) with SOMVR_COMBAT_DEATH's type (Block_Light: no death reaction, the ragdoll from the pose
 * it is in) - XP, the Nemesis line and the loot as any player kill. A _Sever row cuts the head off with the game's own
 * sever (XC_SEVER, FUN_026d4670, the blade's velocity): at the death (SOMVR_KILLS_SEVER=death) or at the blow (=blow).
 * While its kill plays the orc takes no other hit of ours (combat_frame's contact loop, the stealth judge).
 *
 * Not taken (the old path): an UNAWARE orc (its half does not play in its ambient state: docs 133.2 - the stealth kill
 * stays the silent kill; SOMVR_KILLS_UNAWARE=on takes it anyway), captains (their finishers and Last Words stay the
 * game's rules), beasts, an orc in a sync, a dead one, a record that does not start. While its kill plays the fight
 * circle grants the orc no attack (ff_judge) and it rolls no block (rx_roll). Switches: SOMVR_KILLS=on|off (code default off), SOMVR_KILLS_EXEC=blow|
 * trigger (the execution: the next heavy blow while charged | build 30's trigger held), SOMVR_KILLS_SEVER=death|blow,
 * SOMVR_KILLS_ALIGN_MS (150), _ALIGN_DEG (45), _ALIGN_CM (30), _MIN_CM (55), _MAX_S (2), _STALL_MS (350), _STALL_CM (6),
 * _DOWN_CM (25), _STAB_DEG (60), _UNAWARE (off), _PREP (off), _FREE (off), SOMVR_KILLS_TIMEOUT_S (6). Test words
 * (SOMVR_HANDS_TEST): `kills status|on|off|play NAME [S]|cat [FROM]|kill ROW|sever|prep|free|unaware on|off`. Lines: `CONFIG kills ...`, `KILLS ...`, `KILLS END`. */

/* KILLS_SITES_BEGIN (read only, never patched). KL_START FUN_026ed9b0(Combat*, HRECORD node, HRECORD attack, HOBJECT
 * target, bool): the start wrapper (c+0x150/+0x158/+0x170 cleared, c+0x178 = -1, then Combat_StartAttack); KL_NAV
 * FUN_02972f90(AI, bool on, bit): the AI's movement-disable bit (AI+0xe98), the sync start's (bit 2, on = 0); what the
 * sync start (FUN_028081a0) does to its target besides (SOMVR_KILLS_PREP): KL_PREP_A FUN_02697700(CCharacter) (its
 * Combat's +0x98 list, FUN_02710f60(e, 0) each), KL_PREP_B FUN_02693a80(CCharacter) (two look targets cleared),
 * KL_PREP_C FUN_00d61d00(Combat+0x90, 0) (flag 2 of +0x118 -> +0x2d8 + 0x1d0 off) then +0x12f = 1; KL_SYNC_END
 * FUN_028060a0(sync action): the game's end of a sync (both Combats' FUN_026c6f30, its objects released) - a held orc's
 * grab ended before its kill (kl_held_kill). The AI hold (SOMVR_KILLS_AI_HOLD): the four calls of the start wrapper by
 * which an NPC starts its own attacks and reactions - KL_AI_NPC (NPC::vf_92+0x63e: its scheduled attack), KL_AI_ACT1/2
 * (FUN_02a88710+0xfdd/+0x101d: the AI actions' attacks, the avoid among them), KL_AI_MISC (FUN_02741140+0x175) - go
 * through somvr_w_klai, which refuses a start on an orc whose kill plays (the role a sync's +0x190 plays for its target) */
static const struct site_spec g_kl_sites_game[] = {
SITE("KL_START", K_ENTRY, 0x26ed9b0, 0x26ed9b0, "48 c7 87 50 01 00 00 00 00 00 00 45 0f b6 c0 c7 87 58 01 00 00", 0x26ed9b0, "48 c7 87 50 01 00 00 00 00 00 00 45 0f b6 c0"),
SITE("KL_NAV", K_ENTRY, 0x2972f90, 0x2972f90, "44 0f b6 87 98 0e 00 00 89 d1 b8 01 00 00 00 d3", 0x2972f90, "44 0f b6 87 98 0e 00 00 89 d1 b8 01 00 00 00"),
SITE("KL_PREP_A", K_ENTRY, 0x2697700, 0x2697700, "48 8b bf d0 06 00 00 48 85 ff 74 0c e9 2f 20 03", 0x2697700, "48 8b bf d0 06 00 00 48 85 ff 74 0c e9 2f 20"),
SITE("KL_PREP_B", K_ENTRY, 0x2693a80, 0x2693a80, "e8 8b fe ff ff 48 8b 4f 08 48 85 c9 0f 84 8b 00", 0x2693a80, "e8 8b fe ff ff 48 8b 4f 08 48 85 c9 0f 84 8b"),
SITE("KL_PREP_C", K_ENTRY, 0xd61d00, 0xd61d00, "48 8b 87 18 01 00 00 48 85 c0 74 54 55 53 89 f3", 0xd61d00, "48 8b 87 18 01 00 00 48 85 c0 74 54 55 53 89"),
SITE("KL_SYNC_END", K_ENTRY, 0x28060a0, 0x28060a0, "41 55 41 54 55 53 48 83 ec 08 4c 8b 6f 10 4d 85", 0x28060a0, "41 55 41 54 55 53 48 83 ec 08 4c 8b 6f 10 4d"),
SITE("KL_AI_NPC", K_CALL, 0x297749e, 0x2977494, "0f 10 05 d5 d2 9c 00 48 89 c7 e8 0d 65 d7 ff 48", 0x26ed9b0, "48 c7 87 50 01 00 00 00 00 00 00 45 0f b6 c0"),
SITE("KL_AI_ACT1", K_CALL, 0x2a896ed, 0x2a896e3, "58 45 31 c0 4c 89 e9 4c 89 ff e8 be 42 c6 ff e9", 0x26ed9b0, "48 c7 87 50 01 00 00 00 00 00 00 45 0f b6 c0"),
SITE("KL_AI_ACT2", K_CALL, 0x2a8972d, 0x2a89723, "58 45 31 c0 4c 89 e9 48 89 ef e8 7e 42 c6 ff e9", 0x26ed9b0, "48 c7 87 50 01 00 00 00 00 00 00 45 0f b6 c0"),
SITE("KL_AI_MISC", K_CALL, 0x27412b5, 0x27412ab, "89 f6 f3 0f 10 05 bb 34 c0 00 e8 f6 c6 fa ff f3", 0x26ed9b0, "48 c7 87 50 01 00 00 00 00 00 00 45 0f b6 c0"),
};
/* KILLS_SITES_END */
enum { KLS_START, KLS_NAV, KLS_PREP_A, KLS_PREP_B, KLS_PREP_C, KLS_SYNC_END, KLS_AI_NPC, KLS_AI_ACT1, KLS_AI_ACT2, KLS_AI_MISC,
       KLS_N };
_Static_assert(sizeof g_kl_sites_game / sizeof g_kl_sites_game[0] == KLS_N, "kills sites");
static const struct site_spec *g_kl_sites = g_kl_sites_game;
typedef void (*kl_start_fn)(uintptr_t c, uintptr_t node, uintptr_t attack, uintptr_t target, int flag);
typedef void (*kl_nav_fn)(uintptr_t ai, int on, int bit);
typedef void (*kl_ptr_fn)(uintptr_t p);
typedef void (*kl_flag_fn)(uintptr_t p, int on);
#define KL_FN(i, T) ((T)g_kl_sites[i].target)
#define KL_SYNC_TARGET 1 /* Combat/Sync (0x4baa4a0) field 1: TargetAttack */
#define KL_N 4           /* kills at once */
#define KL_NAV_BIT 2

/* The table: the victim's half by what it shows (docs 133.2: each played on a practice orc headless, both eyes; the
 * length and the death's moment from those runs). side, kinds, parts, states, sever, death_s, len_s */
#define F SOMVR_KL_FRONT
#define B SOMVR_KL_BACK
#define ST SOMVR_KL_STAB
#define CU (SOMVR_KL_CUT | SOMVR_KL_OVER | SOMVR_KL_RISE)
#define AK SOMVR_KL_ANYKIND
#define BO SOMVR_KL_BODY
#define NK SOMVR_KL_NECK
#define AP SOMVR_KL_ANYPART
#define FI (SOMVR_KL_FIGHT | SOMVR_KL_COUNTER)
static const somvr_kl_row g_kl_rows[] = {
	/* standing, fighting: the executions' victims (the ordinary killing blow and the execution) */
	{ "AI_Bip_PC_Standing_InstaKill_F", "PC_Bip_Standing_InstaKill_F", F, ST, BO, FI, 0, 0, 0 },
	{ "AI_Bip_PC_Standing_InstaKill_F3", "PC_Bip_Standing_InstaKill_F3", F, CU, BO, FI, 0, 0, 0 },
	{ "AI_Bip_PC_Standing_InstaKill_F4", "PC_Bip_Standing_InstaKill_F4", F, ST, BO, FI, 0, 0, 0 },
	{ "AI_Bip_PC_Standing_InstaKill_F5", "PC_Bip_Standing_InstaKill_F5", F, CU, BO, FI, 0, 0, 0 },
	{ "AI_Bip_PC_Standing_InstaKill_F6", "PC_Bip_Standing_InstaKill_F6", F, CU, BO, FI, 0, 0, 0 },
	{ "AI_Bip_PC_Standing_InstaKill_F8", "PC_Bip_Standing_InstaKill_F8", F, ST, BO, FI, 0, 0, 0 },
	{ "AI_Bip_PC_Standing_InstaKill_F9", "PC_Bip_Standing_InstaKill_F9", F, CU, BO, FI, 0, 0, 0 },
	{ "AI_Bip_PC_Standing_InstaKill_F2_Sever", "PC_Bip_Standing_InstaKill_F2_Sever", F, CU, NK, FI, 1, 0, 0 },
	{ "AI_Bip_PC_Standing_InstaKill_F7_Sever", "PC_Bip_Standing_InstaKill_F7_Sever", F, CU, NK, FI, 1, 0, 0 },
	{ "AI_Bip_PC_Standing_InstaKill_F10_Sever", "PC_Bip_Standing_InstaKill_F10_Sever", F, CU, NK, FI, 1, 0, 0 },
	{ "AI_Bip_PC_Standing_InstaKill_B", "PC_Bip_Standing_InstaKill_B", B, ST, BO, FI, 0, 0, 0 },
	{ "AI_Bip_PC_Standing_InstaKill_B2", "PC_Bip_Standing_InstaKill_B2", B, CU, BO, FI, 0, 0, 0 },
	{ "AI_Bip_PC_Standing_InstaKill_B4", "PC_Bip_Standing_InstaKill_B4", B, AK, BO, FI, 0, 0, 0 },
	{ "AI_Bip_PC_Standing_InstaKill_B3_Sever", "PC_Bip_Standing_InstaKill_B3", B, CU, NK, FI, 1, 0, 0 },
	{ "AI_Bip_PC_Standing_InstaKill_B4_Sever", "PC_Bip_Standing_InstaKill_B4_Sever", B, CU, NK, FI, 1, 0, 0 },
	/* the counter that kills (the riposte into an attack) */
	{ "AI_Bip_PC_Sync_CntrKill_F", "PC_Bip_Sync_CntrKill_F", F, AK, AP, SOMVR_KL_COUNTER, 0, 0, 0 },
	{ "AI_Bip_PC_Sync_CntrKill_B", "PC_Bip_Sync_CntrKill_B", B, AK, AP, SOMVR_KL_COUNTER, 0, 0, 0 },
	/* stunned */
	{ "AI_Bip_PC_Stunned_InstaKill_F", "PC_Bip_Stunned_InstaKill_F", F, AK, AP, SOMVR_KL_STUNNED, 0, 0, 0 },
	{ "AI_Bip_PC_Stunned_InstaKill_B", "PC_Bip_Stunned_InstaKill_B", B, AK, AP, SOMVR_KL_STUNNED, 0, 0, 0 },
	{ "AI_Bip_PC_Stunned_InstaKill_B2", "PC_Bip_Stunned_InstaKill_B2", B, AK, AP, SOMVR_KL_STUNNED, 0, 0, 0 },
	{ "AI_Bip_PC_Stunned_InstaKill_B3", "PC_Bip_Stunned_InstaKill_B3", B, AK, AP, SOMVR_KL_STUNNED, 0, 0, 0 },
	/* unaware: the stealth kill (the silent family; from the front the F rows the game offers) */
	{ "AI_Bip_PC_Sneak_IK_B", "PC_Bip_SneakInstaKill_Silent_B", B, AK, AP, SOMVR_KL_UNAWARE, 0, 0, 0 },
	{ "AI_Bip_PC_Sneak_IK_B_2", "PC_Bip_SneakInstaKill_Silent_B_2", B, AK, AP, SOMVR_KL_UNAWARE, 0, 0, 0 },
	{ "AI_Bip_PC_Sneak_IK_B_3", "PC_Bip_SneakInstaKill_Silent_B_3", B, AK, AP, SOMVR_KL_UNAWARE, 0, 0, 0 },
	{ "AI_Bip_PC_Sneak_IK_B_4", "PC_Bip_SneakInstaKill_Silent_B_4", B, AK, AP, SOMVR_KL_UNAWARE, 0, 0, 0 },
	{ "AI_Bip_PC_Sneak_IK_F_1", "PC_Bip_SneakInstaKill_Silent_F_1", F, AK, AP, SOMVR_KL_UNAWARE, 0, 0, 0 },
	{ "AI_Bip_PC_Sneak_IK_F_2", "PC_Bip_SneakInstaKill_Silent_F_2", F, AK, AP, SOMVR_KL_UNAWARE, 0, 0, 0 },
	/* held by the grab (the interrogation's kills: helper 135's h31_held_kill calls kl_held_kill) */
	{ "AI_Bip_PC_Sync_Intrgtn_Stnd_F_Kill_Silent", "PC_Bip_Sync_Intrgtn_Stnd_F_Kill_Silent", F, AK, AP, SOMVR_KL_HELD, 0, 0, 0 },
	{ "AI_Bip_PC_Sync_Intrgtn_Stnd_F_Kill_Loud", "PC_Bip_Sync_Intrgtn_Stnd_F_Kill_Loud", F, AK, AP, SOMVR_KL_HELD, 0, 0, 0 },
	{ "AI_Bip_PC_Sync_Intrgtn_Stnd_B_Kill_Silent", "PC_Bip_Sync_Intrgtn_Stnd_B_Kill_Silent", B, AK, AP, SOMVR_KL_HELD, 0, 0, 0 },
	{ "AI_Bip_PC_Sync_Intrgtn_Stnd_B_Kill_Loud", "PC_Bip_Sync_Intrgtn_Stnd_B_Kill_Loud", B, AK, AP, SOMVR_KL_HELD, 0, 0, 0 },
	/* downed: the ground finish (the stab down; the neck rows sever) */
	{ "AI_Bip_PC_KD_InstaKill_F", "PC_Bip_KD_InstaKill_F_Sever", F | B, AK, BO, SOMVR_KL_DOWNED, 0, 0, 0 },
	{ "AI_Bip_PC_KD_InstaKill_B", "PC_Bip_KD_InstaKill_B", F | B, AK, BO, SOMVR_KL_DOWNED, 0, 0, 0 },
	{ "AI_Bip_PC_KD_InstaKill_L", "PC_Bip_KD_InstaKill_L", F | B, AK, BO, SOMVR_KL_DOWNED, 0, 0, 0 },
	{ "AI_Bip_PC_KD_InstaKill_R", "PC_Bip_KD_InstaKill_R", F | B, AK, BO, SOMVR_KL_DOWNED, 0, 0, 0 },
	{ "AI_Bip_PC_KD_InstaKill_R2", "PC_Bip_KD_InstaKill_R2", F | B, AK, BO, SOMVR_KL_DOWNED, 0, 0, 0 },
	{ "AI_Bip_PC_KD_InstaKill_R1_Sever", "PC_Bip_KD_InstaKill_R1_Sever", F | B, AK, NK, SOMVR_KL_DOWNED, 1, 0, 0 },
	{ "AI_Bip_PC_KD_InstaKill_R2_Sever", "PC_Bip_KD_InstaKill_R2_Sever", F | B, AK, NK, SOMVR_KL_DOWNED, 1, 0, 0 },
	{ "AI_Bip_PC_KD_InstaKill_L1_Sever", "PC_Bip_KD_InstaKill_L1_Sever", F | B, AK, NK, SOMVR_KL_DOWNED, 1, 0, 0 },
};
#undef F
#undef B
#undef ST
#undef CU
#undef AK
#undef BO
#undef NK
#undef AP
#undef FI
#define KL_ROWS ((int)(sizeof g_kl_rows / sizeof g_kl_rows[0]))

struct kl_one {
	int on;
	uintptr_t c, hobj, vrec, drec, ai;
	int row, fk, sever, severed, tries, blow_kind, blow_side, blow_part, blow_state, nav;
	char vname[96], dname[96], who[48];
	double t0, gt0, last_line;
	float pos0[3], dir[3], vel[3], hp0;
	float head_min, head0, hv0[3], disp_max, head_min_t, head_still;
	double still_t;
	/* the placement (kl_aim reads these on the movement's thread) */
	_Atomic int al_on;
	float al_dyaw, al_dpos[2];
	_Atomic uint32_t al_applied_k; /* the share applied so far x 1e6 */
	_Atomic uint32_t al_calls, al_rot_ok, al_rot_bad;
	double al_t0;
	uintptr_t free_at;
	uint32_t free_was;
};
static struct {
	int on, exec_blow, sever_blow, ok, ok_nav, start_mode, free, prep, ok_prep, unaware, ai_hold, ok_ai, ok_sync_end, abort_ok, brain;
	_Atomic int dealing;              /* kl_deal's own DealHit runs (its death reaction is never held) */
	uintptr_t pcomb;                  /* PlayerCombat (kl_frame's) */
	/* build 31b, SOMVR_KILLS_UNAWARE=sync: the game's own stealth kill on an unaware orc, Talion held in place */
	struct { int on; uintptr_t c, hobj, atk, vic; char an[96], vn[96]; double t0, gt0; float root0[3], blade_v[3];
		 float root_max; int seen_sync, frames; } sy;
	uint64_t sync_starts, sync_kills, sync_forced, sync_fail;
	double sy_end_t;
	_Atomic uint64_t ai_held, ai_passed;
	float align_s, align_deg, align_cm, min_cm, timeout_s, lead_s, stab_cos, neck_up_cm, default_s, stall_s, stall_cm, down_cm,
	      stall_drop_cm;
	struct kl_one k[KL_N];
	uintptr_t vrec[KL_ROWS], srec[KL_ROWS], sever_head;
	signed char vstate[KL_ROWS], sstate[KL_ROWS];
	unsigned char off[KL_ROWS];
	unsigned turn;
	int sever_looked;
	/* the catalogue's play (test only) */
	struct { int on; uintptr_t c, hobj, rec; char name[96]; double t0, gt0, until, next; float pos0[3], yaw0, head0, head_min,
		 head_min_t, moved; int ended; uintptr_t free_at, ai; uint32_t free_was; } pl;
	int cat_on, cat_i;
	/* Talion's root for kl_aim's hold (the game thread writes it each frame; a seqlock) */
	_Atomic uint32_t rseq;
	float root[3];
	_Atomic uint64_t held, vetoes;
	double cat_next;
	/* requests from the words' thread */
	_Atomic int req_play, req_cat, req_kill, req_sever, req_status, req_mode;
	char req_name[96];
	float req_s;
	/* counts */
	uint64_t by_stall, by_down, refused_unaware, swp_gone;
	uint64_t takes, kills, by_moment, by_end, by_cut, by_fall, by_timeout, by_dead, fails, refused, start_fail, severs,
		no_row, floors, skipped_hits, aligned, align_far, plays;
	uint64_t by_who[6];
} g_kl;

static int swp_blade_in(uintptr_t c);        /* somvr_swordphys_game.c (helper 134) */
static void swp_release(const char *why);
static uintptr_t swp_held_orc(uintptr_t *hobj);
static double kl_gnow(void) /* the game's clock (s; slowed with the world: SlowMO), real time if unread */
{
	uintptr_t tm = peek_ptr(g_ffg.clock_pp), p = tm ? peek_ptr(tm + 8) : 0;
	int32_t ms = 0;
	if (p && peek(p + 0x14, &ms, sizeof ms) && ms > 0)
		return ms * 1e-3;
	return mono();
}

static int kl_db_ready(void)
{
	uintptr_t cat = peek_ptr(g_cbg.atkcat_pp);
	uint32_t cnt = 0;
	return cat && peek(cat + CAT_COUNT, &cnt, sizeof cnt) && cnt && g_cb.ok_hit;
}
static uintptr_t kl_byname(const char *nm)
{
	return nm && kl_db_ready() ? cb_rec_valid(CB_FN(CS_ATK_BYNAME, cb_byname_fn)(nm)) : 0;
}
static uintptr_t kl_vrec(int i) /* the row's victim record (looked up once) */
{
	if (i < 0 || i >= KL_ROWS)
		return 0;
	if (!g_kl.vstate[i] && kl_db_ready()) {
		g_kl.vrec[i] = kl_byname(g_kl_rows[i].name);
		g_kl.vstate[i] = g_kl.vrec[i] ? 1 : -1;
		if (!g_kl.vrec[i])
			g_kl.off[i] = 1, logf_("KILLS row '%s' not in Combat/Attack: off", g_kl_rows[i].name);
	}
	return g_kl.vstate[i] > 0 ? g_kl.vrec[i] : 0;
}
static uintptr_t kl_srec(int i)
{
	if (i < 0 || i >= KL_ROWS || !g_kl_rows[i].src)
		return 0;
	if (!g_kl.sstate[i] && kl_db_ready()) {
		g_kl.srec[i] = kl_byname(g_kl_rows[i].src);
		g_kl.sstate[i] = g_kl.srec[i] ? 1 : -1;
	}
	return g_kl.sstate[i] > 0 ? g_kl.srec[i] : 0;
}
/* a record's own sync's TargetAttack (the game's own pairing), 0 = none */
static uintptr_t kl_target_of(uintptr_t rec)
{
	uintptr_t anim = rec ? rx_db_rec(rec, g_fig.sch_attack, FI_ATK_ANIM) : 0;
	uintptr_t sync = anim ? rx_db_rec(anim, g_fig.sch_anim, FI_ANIM_SYNC) : 0;
	return sync ? cb_rec_valid(rx_db_rec(sync, XC_SYNC_SCHEMA, KL_SYNC_TARGET)) : 0;
}
/* the 'Head' sever type: the first SeverTarget of a _Sever row's player record (the executions' data) */
static uintptr_t kl_sever_type(void)
{
	if (g_kl.sever_looked || !kl_db_ready() || !g_cb.ok_ad)
		return g_kl.sever_head;
	g_kl.sever_looked = 1;
	static const char *const recs[] = { "PC_Bip_Standing_InstaKill_F2_Sever", "PC_Bip_KD_InstaKill_R1_Sever" };
	for (size_t k = 0; k < 2 && !g_kl.sever_head; k++) {
		uintptr_t r = kl_byname(recs[k]), ad = r ? CB_FN(CS_ATTACKDATA, cb_attackdata_fn)(r) : 0;
		uint64_t v[1] = { 0 };
		int ty = 0, ns = 0;
		if (ad && rx_db_list(ad, g_rxg.sch_ad, XC_AD_SEVER, v, 1, &ty, &ns) >= 0 && ns && ty == 0xa && v[0])
			g_kl.sever_head = (uintptr_t)v[0];
	}
	char sn[96] = "-";
	if (g_kl.sever_head)
		rx_rec_name(g_kl.sever_head, sn, sizeof sn);
	logf_("KILLS the sever type: '%s' %#lx (the _Sever executions' SeverTarget)", sn, (unsigned long)g_kl.sever_head);
	return g_kl.sever_head;
}

/* the victim record's AnimationData (schema 0x4ba8640): Name, BehaviorNode, SyncToAnim, Loop, PlaybackRate,
 * TargetCoupling, AllowedBehaviorNodes - what decides whether its animation plays on the orc alone (docs 133.2) */
#define KL_AD_SYNCTOANIM 3
static void kl_anim_desc(uintptr_t rec, char *out, size_t cap)
{
	static const int f[] = { 0, 2, 3, 5, 7, 19, 32 };
	static const char *const fn[] = { "Name", "BehaviorNode", "SyncToAnim", "Loop", "PlaybackRate", "TargetCoupling",
					  "AllowedBehaviorNodes" };
	uintptr_t ad = rec ? rx_db_rec(rec, g_fig.sch_attack, FI_ATK_ANIM) : 0;
	size_t o = 0;
	out[0] = 0;
	if (!ad) {
		snprintf(out, cap, "no AnimationData");
		return;
	}
	for (size_t k = 0; k < sizeof f / sizeof f[0] && o + 8 < cap; k++) {
		uint64_t v[2] = { 0, 0 };
		int ty = 0, n = 0, r = rx_db_list(ad, g_fig.sch_anim, f[k], v, 2, &ty, &n);
		char val[96] = "-";
		if (r < 0)
			snprintf(val, sizeof val, "absent");
		else if (ty == 1)
			snprintf(val, sizeof val, "bool %d", rx_db_bool(ad, g_fig.sch_anim, f[k]));
		else if (ty == 0xa && n > 0)
			rx_rec_name((uintptr_t)v[0], val, sizeof val), val[0] ? (void)0 : (void)snprintf(val, sizeof val, "rec %#lx",
													(unsigned long)v[0]);
		else if (ty == 2 && n > 0) {
			float x;
			uint32_t u = (uint32_t)v[0];
			memcpy(&x, &u, 4);
			snprintf(val, sizeof val, "%.3f", (double)x);
		} else if (n > 0) {
			char sbuf[48] = "";
			if (v[0] > 0x10000 && peek((uintptr_t)v[0], sbuf, sizeof sbuf - 1))
				for (size_t j = 0; j < sizeof sbuf - 1; j++)
					if (sbuf[j] && (sbuf[j] < 32 || sbuf[j] > 126)) {
						sbuf[0] = 0;
						break;
					}
			snprintf(val, sizeof val, "type %d n %d %#llx%s%s%s", ty, n, (unsigned long long)v[0], sbuf[0] ? " '" : "", sbuf,
				 sbuf[0] ? "'" : "");
		} else
			snprintf(val, sizeof val, "type %d n 0", ty);
		o += (size_t)snprintf(out + o, cap - o, "%s%s %s", k ? ", " : "", fn[k], val);
	}
}
/* a bool field's word and bit (the probe's db_get layout), 0 = not a bool or absent */
static uintptr_t kl_bool_at(uintptr_t rec, uintptr_t holder, int i, int *bit)
{
	unsigned char e[16];
	uint16_t cnt, vi;
	if (!rec || rx_db_entry(rec, rx_sch_hash(holder, i), e))
		return 0;
	memcpy(&cnt, e + 0xc, 2), memcpy(&vi, e + 0xe, 2);
	uintptr_t vals = peek_ptr(rec + 0x10);
	if (!cnt || (e[0xa] & 0x3f) != 1 || !vals)
		return 0;
	*bit = vi & 31;
	return vals + (uintptr_t)(vi >> 5) * 8;
}
/* SOMVR_KILLS_FREE: the victim's SyncToAnim off while its kill plays (its time no longer follows Talion's half, which
 * never runs); 1 = cleared now (restore with kl_free_back) */
static int kl_free(uintptr_t vrec, uintptr_t *at, uint32_t *was)
{
	uintptr_t ad = vrec ? rx_db_rec(vrec, g_fig.sch_attack, FI_ATK_ANIM) : 0;
	int bit = 0;
	uintptr_t w = ad ? kl_bool_at(ad, g_fig.sch_anim, KL_AD_SYNCTOANIM, &bit) : 0;
	uint32_t v = 0;
	*at = 0;
	if (!w || !peek(w, &v, 4) || !(v & (1u << bit)))
		return 0;
	uint32_t nv = v & ~(1u << bit);
	if (!arm_poke(w, &nv, 4))
		return 0;
	*at = w, *was = v;
	return 1;
}
static void kl_free_back(uintptr_t at, uint32_t was)
{
	if (at)
		arm_poke(at, &was, 4);
}

/* SOMVR_KILLS_PREP: the orc readied as the sync start readies its target, before its half starts (an unaware orc's own
 * ambient state kept most victims' animations from playing: boot 20261102-113313) */
static int kl_prep(uintptr_t c)
{
	if (!g_kl.prep || !g_kl.ok_prep)
		return 0;
	uintptr_t ch = peek_ptr(c + C_OWNER), m = peek_ptr(c + 0x90);
	if (ch) {
		KL_FN(KLS_PREP_A, kl_ptr_fn)(ch);
		KL_FN(KLS_PREP_B, kl_ptr_fn)(ch);
	}
	if (m) {
		KL_FN(KLS_PREP_C, kl_flag_fn)(m, 0);
		unsigned char one = 1;
		arm_poke(m + 0x12f, &one, 1);
	}
	return 1;
}

/* SOMVR_KILLS_BRAIN: what Combat_StartAttack's sync start does to an NPC target before the target's half starts
 * (0x26ed0c0..: when the NPC's current attack has no Sync, FUN_0296b0d0(npc) = its brain's (npc+0xc78) byte +0x402 = 1;
 * the re-plan of FUN_029ac890 is +0x401) - the brain lets go of its ambient action, so an unaware orc takes the half */
#define KL_NPC_BRAIN 0xc78
#define KL_BRAIN_BREAK 0x402
static int kl_brain(uintptr_t c)
{
	if (!g_kl.brain)
		return 0;
	uintptr_t npc = peek_ptr(c + C_OWNER), br = npc ? peek_ptr(npc + KL_NPC_BRAIN) : 0;
	unsigned char one = 1;
	return br && arm_poke(br + KL_BRAIN_BREAK, &one, 1);
}

/* ---- build 31b: SOMVR_KILLS_UNAWARE=sync - the unaware orc's half by the game's own sync (docs 133.9) ---------------
 * An unaware orc's half plays only through the paired action's blend nodes (133.8), which need the sync. So the game's
 * own stealth kill is started the way SOMVR_FINISH / SOMVR_ABOVE start the game's kills: the game's selection for that
 * orc on the stealth node (vb_pick: its predicates - behind, unaware, in reach), FUN_00a8c700(pc, its KBM node, the
 * attack, the orc) - the client side and Combat_StartAttack, the sync and the orc's half; no camera step (FUN_00a8e120's
 * presentation is not asked). Talion stays the player's: SOMVR_COMBAT_HOLD_TALION's hold takes every LOCO tick while it
 * runs (kl_sync_hold), pulls the sync's moves of his root back at once and keeps the view on the pin; his half plays
 * underneath (only our hands are drawn). The death is the game's own (his half's APPLYDAMAGE, the sync's ragdoll keys);
 * safety: 8 s without the orc dead -> the sync ended (KL_SYNC_END) and our DealHit (forceKill). */
static int kl_sync_hold(double t)
{
	(void)t;
	return g_kl.sy.on;
}
/* the view's side (the body moments, the presentation's window): the kill, then HOLD talion's return of the body, then
 * 1.5 s more while his sync's record lingers (boot 184927: the body moments held the view on the lingering record and
 * eased it 14.5 cm at their release) */
static int kl_sync_view(double t)
{
	return g_kl.sy.on || (g_tk.active && g_tk.by_kls) || (g_kl.sy_end_t > 0 && t - g_kl.sy_end_t < 1.5);
}
static int kl_sync_take(uintptr_t pc, const struct cb_orc *o, const char *who, double t)
{
	uintptr_t pcomb = g_kl.pcomb, node = 0, n2 = 0, sy = 0, atk = 0;
	const char *why = "no PlayerCombat";
	uint8_t pad = 0;
	if (g_kl.sy.on)
		why = "another sync kill runs";
	else if (pcomb && g_tk.on && (node = vb_node_named(pcomb, g_vb.stealth_node)) &&
		 (atk = vb_pick(pcomb, pc, o, node, &sy, &why))) {
		peek(pcomb + FI_PC_PAD, &pad, 1);
		n2 = FI_FN(FIS_KBMNODE, fi_kbm_fn)(node, pad != 0);
	} else if (!g_tk.on)
		why = "SOMVR_COMBAT_HOLD_TALION is off (nothing would hold Talion)";
	char an[96] = "-";
	if (!atk || !n2) {
		g_kl.sync_fail++;
		logf_("KILLS sync t=%.3f %s on unaware orc %#lx: the game offers no stealth kill on it (%s) - the silent kill", t, who,
		      (unsigned long)o->c, why);
		return 0;
	}
	memset(&g_kl.sy, 0, sizeof g_kl.sy);
	uintptr_t th = peek_ptr(pc + C_HOBJ);
	if (th)
		peek(th + OBJ_POS, g_kl.sy.root0, sizeof g_kl.sy.root0);
	g_kl.sy.on = 1, g_kl.sy.c = o->c, g_kl.sy.hobj = o->hobj, g_kl.sy.atk = atk, g_kl.sy.t0 = t, g_kl.sy.gt0 = kl_gnow();
	vb_name(atk, g_kl.sy.an, sizeof g_kl.sy.an);
	FI_FN(FIS_START, fi_start_fn)(pcomb, n2, atk, o->hobj);
	uintptr_t ta = peek_ptr(pc + VB_C_ATTACK), s0 = peek_ptr(pc + C_SYNC), vic = peek_ptr(o->c + VB_C_ATTACK);
	char tn[96], sn[96], vn[96];
	vb_name(vic, vn, sizeof vn);
	snprintf(g_kl.sy.vn, sizeof g_kl.sy.vn, "%s", vn);
	g_kl.sy.vic = vic;
	g_kl.sync_starts++;
	g_xc.trace_until = t + 8.0; /* SOMVR_EXECUTE's eye trace (`EXECUTE f=`): the view and the root tick by tick */
	logf_("KILLS sync start t=%.3f %s on unaware orc %#lx: the game's own '%s' (sync '%s') by FUN_00a8c700 (node '%s', no "
	      "camera step) -> Talion's attack '%s', his +0x188 %#lx, the orc's half '%s'; Talion held (HOLD talion), his root "
	      "at %.1f,%.1f,%.1f", t, who, (unsigned long)o->c, g_kl.sy.an, vb_name(sy, an, sizeof an), g_vb.stealth_node,
	      vb_name(ta, tn, sizeof tn), (unsigned long)s0, vn, g_kl.sy.root0[0], g_kl.sy.root0[1], g_kl.sy.root0[2]);
	(void)sn;
	return 1;
}
/* kl_frame: the sync kill's watch - Talion's root against where it was, the end, the safety */
static void kl_sync_frame(uintptr_t pc, double t)
{
	if (!g_kl.sy.on || !pc)
		return;
	uintptr_t th = peek_ptr(pc + C_HOBJ), s0 = peek_ptr(pc + C_SYNC), rec0 = s0 ? peek_ptr(s0 + FB_SYNC_REC) : 0;
	float r[3] = { 0, 0, 0 };
	if (th && peek(th + OBJ_POS, r, sizeof r))
		g_kl.sy.root_max = fmaxf(g_kl.sy.root_max, hypotf(r[0] - g_kl.sy.root0[0], r[2] - g_kl.sy.root0[2]));
	g_kl.sy.seen_sync |= rec0 != 0, g_kl.sy.frames++;
	struct cb_orc o;
	int alive = cb_orc_get(g_kl.sy.c, &o) && !cb_dead(g_kl.sy.c);
	double el = t - g_kl.sy.t0;
	const char *why = !alive ? (cb_orc_get(g_kl.sy.c, &o) ? "the orc dead (the game's kill)" : "the orc gone")
			  : g_kl.sy.seen_sync && !rec0 && el > 0.3 ? "the sync over, the orc alive" : el > 8.0 ? "8 s" : NULL;
	if (!why)
		return;
	int forced = alive;
	if (forced) { /* the safety: the sync ended, our kill */
		if (s0 && rec0 && g_kl.ok_sync_end)
			KL_FN(KLS_SYNC_END, kl_ptr_fn)(s0);
		atomic_store(&g_kl.dealing, 1);
		CB_FN(CS_DEALHIT, cb_dealhit_fn)((cb_ltvec){ o.pos[0], o.pos[1] + 100.0f, o.pos[2] }, pc, o.hobj, stl_rec(), 1);
		atomic_store(&g_kl.dealing, 0);
		g_kl.sync_forced++;
	} else if (strstr(why, "dead"))
		g_kl.sync_kills++, CB_COUNT(kills);
	logf_("KILLS sync end t=%.3f orc %#lx '%s' / '%s': %s after %.2f s%s; Talion's root moved up to %.1f cm from where he "
	      "stood (%d frames; his sync seen %d)", t, (unsigned long)g_kl.sy.c, g_kl.sy.an, g_kl.sy.vn, why, el,
	      forced ? (cb_dead(g_kl.sy.c) ? " - ended, our kill: dead" : " - ended, our kill: NOT dead") : "", g_kl.sy.root_max,
	      g_kl.sy.frames, g_kl.sy.seen_sync);
	if (!forced)
		kb_note(g_kl.sy.c, g_kl.sy.hobj, g_kl.sy.atk, "the game's stealth kill (SOMVR_KILLS sync)", t);
	g_kl.sy.on = 0, g_kl.sy_end_t = t;
}

static struct kl_one *kl_of(uintptr_t c)
{
	for (int i = 0; c && i < KL_N; i++)
		if (g_kl.k[i].on && g_kl.k[i].c == c)
			return &g_kl.k[i];
	return NULL;
}
/* the AI hold: an NPC's own attack or reaction start on an orc whose kill plays is refused (any thread) */
__attribute__((used)) void somvr_w_klai(uintptr_t c, uintptr_t node, uintptr_t attack, uintptr_t target, int flag)
{
	if (g_kl.on && g_kl.ai_hold && c && !atomic_load(&g_kl.dealing)) {
		int held = g_kl.pl.on && g_kl.pl.c == c;
		for (int i = 0; i < KL_N && !held; i++)
			held = g_kl.k[i].on && g_kl.k[i].c == c;
		if (held) {
			atomic_fetch_add(&g_kl.ai_held, 1);
			return;
		}
	}
	atomic_fetch_add(&g_kl.ai_passed, 1);
	KL_FN(KLS_START, kl_start_fn)(c, node, attack, target, flag);
}

/* the fight circle's score hook (SOMVR_FRONT's ff_judge, its thread): this orc's kill plays - no attack granted */
static int kl_hobj_busy(uintptr_t hobj)
{
	if (hobj && g_kl.pl.on && g_kl.pl.hobj == hobj)
		return 1; /* (the catalogue's play) */
	for (int i = 0; hobj && i < KL_N; i++)
		if (g_kl.k[i].on && g_kl.k[i].hobj == hobj) {
			atomic_fetch_add(&g_kl.vetoes, 1);
			return 1;
		}
	return 0;
}
/* combat_frame's contact loop and the stealth judge: this orc's kill plays - no other hit */
static int kl_busy(uintptr_t c)
{
	if (!kl_of(c) && !(c && g_kl.pl.on && g_kl.pl.c == c) && !(c && g_kl.sy.on && g_kl.sy.c == c))
		return 0;
	g_kl.skipped_hits++;
	return 1;
}
static int kl_is_beast(uintptr_t c, uintptr_t hobj)
{
	struct bz_one *e = g_bz.on && c ? bz_get(c, hobj, cb_now()) : NULL;
	return e && e->kind != SOMVR_BZ_NONE;
}
static float kl_yaw(uintptr_t hobj)
{
	float q[4] = { 0, 0, 0, 1 };
	return peek(hobj + OBJ_ROT, q, sizeof q) && vec_ok(q, 2.0f) ? sv_heading(sv_qnormalize(sv_quat_of(q))) : 0;
}
/* the orc's state for the pick: its combat state record's name or its current attack's (the game's words) */
static int kl_state_of(uintptr_t c, int hint, char *why, size_t cap)
{
	char sn[96] = "", an[96] = "";
	uintptr_t s = peek_ptr(c + VB_C_STATE), a = peek_ptr(c + VB_C_ATTACK);
	if (s)
		rx_rec_name(s, sn, sizeof sn);
	if (a)
		rx_rec_name(a, an, sizeof an);
	snprintf(why, cap, "state '%s', attack '%s'", sn[0] ? sn : "-", an[0] ? an : "-");
	if (strstr(sn, "KnockDown") || strstr(sn, "Knockdown") || strstr(an, "_KD") || strstr(an, "Knockdown"))
		return SOMVR_KL_DOWNED;
	if (strstr(sn, "Stun") || strstr(an, "Stunned"))
		return SOMVR_KL_STUNNED;
	if (hint)
		return hint;
	return SOMVR_KL_FIGHT;
}

/* the death's DealHit: the torso (or the root + 1 m), Talion's Combat, the record, forceKill; SOMVR_COMBAT_DEATH's type */
static int kl_deal(uintptr_t pc, struct kl_one *k, const char *why, double t)
{
	struct cb_orc o;
	if (!cb_orc_get(k->c, &o))
		return 0;
	float nd[4][3], at[3] = { o.pos[0], o.pos[1] + 100.0f, o.pos[2] };
	if (stl_body(&o, nd))
		memcpy(at, nd[1], sizeof at);
	float hd0[3];
	unsigned char once = 0;
	peek(pc + C_HITDIR, hd0, sizeof hd0), peek(pc + C_HITONCE, &once, 1);
	memcpy((void *)(pc + C_HITDIR), k->dir, sizeof k->dir);
	*(volatile unsigned char *)(pc + C_HITONCE) = 0;
	uintptr_t dslot = 0;
	uint64_t dwas = 0;
	char death[120] = "the record's own RecoilTypeHit";
	if (g_kd.mode == KD_TYPE) { /* SOMVR_COMBAT_DEATH's type: no death reaction, the ragdoll from the pose it is in */
		uintptr_t ad = g_df.ok_ad ? DF_FN(DFS_ATTACKDATA, cb_attackdata_fn)(k->drec) : 0;
		uintptr_t sl = ad ? kx_slot(ad, g_dfg.sch_ad, DF_AD_RECOIL_HIT) : 0, r = kx_find(g_kd.type);
		uint64_t want = (uint64_t)r;
		if (sl && r && peek(sl, &dwas, 8) && (dwas == want || arm_poke(sl, &want, 8))) {
			dslot = dwas == want ? 0 : sl;
			snprintf(death, sizeof death, "RecoilTypeHit '%s' (SOMVR_COMBAT_DEATH)", g_kd.type);
		}
	}
	snprintf(g_kd.note, sizeof g_kd.note, "a kill's end (SOMVR_KILLS): %.60s", death);
	g_kd.pre_c = k->c, g_kd.pre_dead = cb_dead(k->c);
	int dead0 = cb_dead(k->c);
	float hp0 = cb_health(k->c);
	atomic_store(&g_kl.dealing, 1);
	CB_FN(CS_DEALHIT, cb_dealhit_fn)((cb_ltvec){ at[0], at[1], at[2] }, pc, k->hobj, k->drec, k->fk);
	atomic_store(&g_kl.dealing, 0);
	kx_hit_back(dslot, dwas);
	memcpy((void *)(pc + C_HITDIR), hd0, sizeof hd0);
	*(volatile unsigned char *)(pc + C_HITONCE) = once;
	int dead1 = cb_dead(k->c);
	float hp1 = cb_health(k->c);
	char sev[160] = "";
	if (dead1 && k->sever && !k->severed && g_xc.ok_sever && kl_sever_type() && peek_ptr(k->c + 0x90)) {
		float vel[3] = { k->vel[0], k->vel[1] + 150.0f, k->vel[2] }, rot[3] = { 0, 0, 0 };
		XC_FN(XCS_SEVER, xc_sever_fn)(k->c, g_kl.sever_head, vel, rot, peek_ptr(pc + C_HOBJ));
		k->severed = 1, g_kl.severs++;
		snprintf(sev, sizeof sev, "; the head severed at the death (XC_SEVER, velocity %.0f,%.0f,%.0f)", vel[0], vel[1], vel[2]);
	}
	double el = kl_gnow() - k->gt0;
	logf_("KILLS kill t=%.3f orc %#lx (%s): '%s' %.2f s in (%s) -> Combat_DealHit(the %s at %.0f,%.0f,%.0f, pc=%#lx, rec='%s', "
	      "forceKill=%d): dead %d->%d health %.1f->%.1f (at the blow %.1f); the death %s%s; the head %+.0f cm over the root "
	      "at the start, lowest %+.0f", t, (unsigned long)k->c, k->who, k->vname, el, why, at[1] > o.pos[1] + 99.5f &&
	      at[1] < o.pos[1] + 100.5f ? "root + 1 m" : "torso", at[0], at[1], at[2], (unsigned long)pc, k->dname, k->fk, dead0,
	      dead1, hp0, hp1, k->hp0, death, sev, k->head0, k->head_min);
	if (dead1 && swp_blade_in(k->c)) /* helper 134: the blade held in it while its half played - out at the death */
		swp_release("the kill's death (SOMVR_KILLS)"), strncat(sev, "; the blade in it released", sizeof sev - strlen(sev) - 1);
	if (dead1 && !dead0) {
		g_kl.kills++, CB_COUNT(kills);
		kb_note(k->c, k->hobj, k->drec, "a kill's end (SOMVR_KILLS)", t); /* COMBAT death: where the body goes */
		in_haptic_event(SOMVR_HAND_R, 0.6f, 0.12f, SOMVR_HAP_COMBAT);
	}
	return dead1;
}

static void kl_end(struct kl_one *k)
{
	atomic_store(&k->al_on, 0);
	kl_free_back(k->free_at, k->free_was), k->free_at = 0;
	k->on = 0;
}

/* SOMVR_ABIL, SOMVR_EXECUTE, SOMVR_STEALTH_KILL, SOMVR_VERBS and cb_hit: a kill by the player's hand, before the game's
 * Combat_DealHit. rec = the caller's record (its own sync's TargetAttack is the victim when it has one; else the table by
 * the blow; force_row >= 0: that row), fk = the forceKill the caller would pass, hint = a state the caller knows
 * (SOMVR_KL_UNAWARE, _COUNTER, _DOWNED; 0 = read). 1 = taken: the orc plays its half, the death at its moment (the caller
 * deals no hit); 0 = not (the caller's own hit) */
static int kl_take_row(uintptr_t pc, const struct cb_orc *o, uintptr_t pair, uintptr_t deal, int src_first,
		       const float point[3], const float vel[3], const float axis[3], int hint, int force_row, const char *who,
		       double t)
{
	if (!g_kl.on || !g_kl.ok || !pc || !o || !o->c || !kl_db_ready())
		return 0;
	char ended[120] = "";
	if (g_kl.abort_ok && g_kl.ok_sync_end && cb_sync(o->c) && !cb_dead(o->c) && !kp_of_combat(o->c)) {
		/* the held orc (kl_held_kill): the grab's sync ended as a new sync ends it (FUN_028081a0's abort) */
		uintptr_t s1 = peek_ptr(o->c + C_SYNC + 8), s0 = peek_ptr(o->c + C_SYNC);
		char sn[96] = "";
		if (s1)
			rx_rec_name(peek_ptr(s1 + 0x10), sn, sizeof sn), KL_FN(KLS_SYNC_END, kl_ptr_fn)(s1);
		if (s0 && peek_ptr(s0 + 0x10))
			KL_FN(KLS_SYNC_END, kl_ptr_fn)(s0);
		snprintf(ended, sizeof ended, "; its sync '%s' ended first (KL_SYNC_END)", sn[0] ? sn : "?");
	}
	const char *no = cb_dead(o->c) ? "dead" : kl_of(o->c) ? "its kill already plays" : kp_of_combat(o->c) ? "a captain (his "
			 "own rules, Last Words)" : kl_is_beast(o->c, o->hobj) ? "a beast" : cb_sync(o->c) ? "in a sync" : NULL;
	struct stl_aw aw;
	memset(&aw, 0, sizeof aw);
	int woke = 0;
	if (!no && g_kl.unaware == 3 && stl_aware(o, peek_ptr(pc + C_HOBJ), &aw) >= 0 && aw.game == 0 && !aw.engaged)
		return kl_sync_take(pc, o, who, t); /* build 31b: the game's own paired kill, Talion held (0 = the silent kill) */
	if (!no && g_kl.unaware != 1 && stl_aware(o, peek_ptr(pc + C_HOBJ), &aw) >= 0 && aw.game == 0 && !aw.engaged) {
		float wake = 3.0f; /* =wake: its awareness meter (NPC+0xeb8, the AIIsAware predicate's) set as a noticing orc's,
				    * so its animation leaves the ambient state; its own reactions are held by the AI hold */
		if (g_kl.unaware == 2 && aw.npc && arm_poke(aw.npc + STL_NPC_AWARE, &wake, sizeof wake))
			woke = 1;
		else {
			no = "unaware: its half does not play on an orc that has not noticed Talion (docs 133.3) - the silent kill";
			g_kl.refused_unaware++;
		}
	}
	struct kl_one *k = NULL;
	for (int i = 0; !no && i < KL_N && !k; i++)
		if (!g_kl.k[i].on)
			k = &g_kl.k[i];
	if (!no && !k)
		no = "4 kills already play";
	if (no) {
		g_kl.refused++;
		logf_("KILLS t=%.3f %s on orc %#lx: not taken (%s): the old path", t, who, (unsigned long)o->c, no);
		return 0;
	}
	float yaw = kl_yaw(o->hobj), off = 0, nd[4][3];
	int skel = stl_body(o, nd);
	char sw[200];
	somvr_kl_blow bl;
	bl.side = somvr_kl_side(point, o->pos, yaw, 110.0f, &off);
	bl.kind = somvr_kl_kind(vel, axis, g_kl.stab_cos);
	/* the neck: the contact within 12 cm of its Neck or Head node (the skeleton's); without it, the height (boot 132856's
	 * chest cuts read as "the neck" by the height rule: the capsule's contact sits high) */
	bl.part = somvr_kl_part(point[1] - o->pos[1], skel ? 1e9f : g_kl.neck_up_cm,
				skel ? fminf(dist3(point, nd[2]), dist3(point, nd[3])) : NAN, 12.0f);
	bl.state = kl_state_of(o->c, hint, sw, sizeof sw);
	/* the victim: the caller's record's own pairing, else the table by the blow */
	uintptr_t vr = force_row >= 0 ? kl_vrec(force_row) : kl_target_of(pair);
	int row = force_row;
	char vn[96] = "";
	const char *how = force_row >= 0 ? "the test's row" : "the record's own sync";
	if (vr) {
		rx_rec_name(vr, vn, sizeof vn);
		if (row < 0)
			row = somvr_kl_find(g_kl_rows, KL_ROWS, vn);
	} else {
		for (int i = 0; i < KL_ROWS; i++)
			kl_vrec(i); /* the rows the database lacks are off */
		row = somvr_kl_pick(g_kl_rows, KL_ROWS, &bl, g_kl.turn++, g_kl.off);
		vr = row >= 0 ? kl_vrec(row) : 0;
		how = "the table by the blow";
		if (vr)
			snprintf(vn, sizeof vn, "%s", g_kl_rows[row].name);
	}
	if (!vr) {
		g_kl.no_row++;
		logf_("KILLS t=%.3f %s on orc %#lx: no victim row for %s from its %s (%+.0f deg), %s, %s (%s): the old path", t, who,
		      (unsigned long)o->c, somvr_kl_kind_name(bl.kind), bl.side == SOMVR_KL_BACK ? "back" : "front", off,
		      bl.part == SOMVR_KL_NECK ? "the neck" : "the body", somvr_kl_state_name(bl.state), sw);
		return 0;
	}
	uintptr_t hp = peek_ptr(pc + C_HOBJ), fat = 0;
	uint32_t fwas = 0;
	float hp0 = cb_health(o->c);
	int freed = g_kl.free && kl_free(vr, &fat, &fwas);
	int prepped = kl_prep(o->c), braked = kl_brain(o->c);
	KL_FN(KLS_START, kl_start_fn)(o->c, 0, vr, hp, 0);
	uintptr_t now = peek_ptr(o->c + VB_C_ATTACK);
	if (now != vr) {
		kl_free_back(fat, fwas);
		char an[96];
		g_kl.start_fail++;
		if (row >= 0)
			g_kl.off[row] = 1;
		logf_("KILLS t=%.3f %s on orc %#lx: '%s' (%s) did not start (its attack now '%s'): the old path%s", t, who,
		      (unsigned long)o->c, vn, how, vb_name(now, an, sizeof an), row >= 0 ? "; the row off" : "");
		return 0;
	}
	memset(k, 0, sizeof *k);
	k->on = 1, k->c = o->c, k->hobj = o->hobj, k->vrec = vr, k->row = row;
	k->free_at = fat, k->free_was = fwas;
	/* the death's record: the row's own player record first where the table picked (the execution), else the caller's */
	uintptr_t sr = kl_srec(row);
	k->drec = src_first && sr ? sr : deal ? deal : pair ? pair : sr;
	if (!k->drec)
		k->drec = stl_rec(); /* (a row whose player record the database lacks, no caller's: the silent kill's) */
	k->fk = 1; /* the blow was the kill (a captain is never taken: his rules stay the old path's) */
	k->sever = row >= 0 ? g_kl_rows[row].sever : strstr(vn, "_Sever") != NULL;
	k->blow_kind = bl.kind, k->blow_side = bl.side, k->blow_part = bl.part, k->blow_state = bl.state;
	snprintf(k->vname, sizeof k->vname, "%s", vn);
	vb_name(k->drec, k->dname, sizeof k->dname);
	snprintf(k->who, sizeof k->who, "%s", who);
	k->t0 = t, k->gt0 = kl_gnow(), k->hp0 = hp0;
	memcpy(k->pos0, o->pos, sizeof k->pos0);
	float vl = sqrtf(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
	for (int j = 0; j < 3; j++)
		k->dir[j] = vl > 1.0f ? vel[j] / vl : 0, k->vel[j] = vl > 1.0f ? vel[j] / vl * fminf(vl, 600.0f) : 0;
	if (!(vl > 1.0f))
		k->dir[2] = 1;
	/* the head over the ground it stood on (world height less the start's root height: the root itself moves with a
	 * half's root motion - boot 145246's 102 cm "moved" was the root's, not the head's) */
	k->head0 = k->head_min = k->head_still = skel ? nd[3][1] - o->pos[1] : NAN;
	for (int j = 0; j < 3; j++)
		k->hv0[j] = skel ? nd[3][j] - o->pos[j] : NAN;
	k->ai = peek_ptr(o->c + C_AI);
	if (g_kl.ok_nav && k->ai) {
		KL_FN(KLS_NAV, kl_nav_fn)(k->ai, 0, KL_NAV_BIT); /* its navigation held, as a sync's start holds it */
		k->nav = 1;
	}
	/* the placement: the orc to the player */
	float root[3] = { 0, 0, 0 }, dyaw = 0, dp[2] = { 0, 0 };
	int have_root = hp && peek(hp + OBJ_POS, root, sizeof root) && vec_ok(root, 1e7f);
	int side = row >= 0 ? (g_kl_rows[row].side == SOMVR_KL_BACK ? SOMVR_KL_BACK : g_kl_rows[row].side == SOMVR_KL_FRONT
			       ? SOMVR_KL_FRONT : bl.side) : strstr(vn, "_B") ? SOMVR_KL_BACK : SOMVR_KL_FRONT;
	int al = have_root && bl.state != SOMVR_KL_DOWNED && g_kl.align_s > 0 &&
		 somvr_kl_align(o->pos, yaw, root, side, g_kl.min_cm, g_kl.align_cm, g_kl.align_deg, &dyaw, dp);
	if (al) {
		k->al_dyaw = dyaw, k->al_dpos[0] = dp[0], k->al_dpos[1] = dp[1], k->al_t0 = mono();
		atomic_store(&k->al_applied_k, 0);
		atomic_store(&k->al_on, 1);
		g_kl.aligned++;
	} else if (have_root && bl.state != SOMVR_KL_DOWNED)
		g_kl.align_far++;
	if (k->sever && g_kl.sever_blow && g_xc.ok_sever && kl_sever_type() && peek_ptr(o->c + 0x90)) {
		float rot[3] = { 0, 0, 0 };
		XC_FN(XCS_SEVER, xc_sever_fn)(o->c, g_kl.sever_head, k->vel, rot, hp);
		k->severed = 1, g_kl.severs++;
	}
	g_kl.takes++;
	int wi = !strcmp(who, "the killing blow") ? 0 : !strcmp(who, "the execution") ? 1 : !strcmp(who, "the stealth kill") ? 2
		 : !strcmp(who, "the offered stealth kill") ? 3 : !strcmp(who, "the downed orc's stab") ? 4 : 5;
	g_kl.by_who[wi]++;
	float da = row >= 0 ? somvr_kl_death_at(&g_kl_rows[row], g_kl.lead_s) : NAN;
	char ds[48];
	snprintf(ds, sizeof ds, isfinite(da) ? "at +%.2f s" : "at its end", (double)da);
	logf_("KILLS start t=%.3f %s on orc %#lx (health %.1f): %s from its %s (%+.0f deg off its facing), %s, %s (%s) -> '%s' "
	      "(%s; row %d) on the orc alone (KL_START, Talion's half not started; its navigation %s); the death %s with '%s'; "
	      "%s; placement %s (turn %+.1f deg, slide %.0f,%.0f cm over %.0f ms)%s", t, who, (unsigned long)o->c, hp0,
	      somvr_kl_kind_name(bl.kind), bl.side == SOMVR_KL_BACK ? "back" : "front", off, bl.part == SOMVR_KL_NECK ? "the neck"
	      : "the body", somvr_kl_state_name(bl.state), sw, vn, how, row, k->nav ? "held" : "free", ds, k->dname,
	      k->sever ? (k->severed ? "the head severed at the blow" : "the head off at the death") : "no sever",
	      al ? "on" : have_root ? "too far off: none" : "no root", dyaw * 57.29578f, dp[0], dp[1], (double)g_kl.align_s * 1e3,
	      freed ? "; its SyncToAnim off while it plays (SOMVR_KILLS_FREE)" : prepped ? "; readied as a sync target (SOMVR_KILLS_PREP)" : "");
	if (ended[0])
		logf_("KILLS t=%.3f orc %#lx%s", t, (unsigned long)o->c, ended);
	if (braked && bl.state == SOMVR_KL_UNAWARE)
		logf_("KILLS t=%.3f orc %#lx unaware: its brain's ambient action let go first (SOMVR_KILLS_BRAIN, FUN_0296b0d0's write)", t,
		      (unsigned long)o->c);
	if (woke)
		logf_("KILLS t=%.3f orc %#lx was unaware: its awareness set as a noticing orc's before its half (SOMVR_KILLS_UNAWARE="
		      "wake)", t, (unsigned long)o->c);
	return 1;
}
static int kl_take(uintptr_t pc, const struct cb_orc *o, uintptr_t pair, uintptr_t deal, int src_first, const float point[3],
		   const float vel[3], const float axis[3], int hint, const char *who, double t)
{
	return kl_take_row(pc, o, pair, deal, src_first, point, vel, axis, hint, -1, who, t);
}

/* helper 135's hook (som-b31-hands, h31_held_kill in somvr_hands31_game.c): a sword hit on the orc the left hand holds
 * kills it - the held rows (the interrogation's standing kills, by the side), the death at its moment; 0 = not taken
 * (the caller's own forceKill hit). rec = the caller's record (its DealHit at the death when the row's own is absent) */
__attribute__((unused)) static int kl_held_kill(uintptr_t pc, const struct cb_orc *o, uintptr_t rec, const float point[3],
						 const float vel[3],
			const float axis[3], double t)
{
	g_kl.abort_ok = 1;
	int r = kl_take(pc, o, 0, rec, 1, point, vel, axis, SOMVR_KL_HELD, "the held orc's kill", t);
	g_kl.abort_ok = 0;
	return r;
}

/* somvr_w_aimove (somvr_fight_game.c), after the game's step, on the movement's thread: an orc whose kill plays is placed
 * (its share of the turn and slide by now) and is not held by the fight space or the body hold. 1 = ours */
static int kl_aim(uintptr_t mv, const float was[3], float *xf)
{
	(void)was;
	if (!g_kl.on)
		return 0;
	uintptr_t ch = *(uintptr_t *)(mv + FS_MV_CHAR), h = ch ? *(uintptr_t *)(ch + 8) : 0;
	if (!h)
		return 0;
	for (int i = 0; i < KL_N; i++) {
		struct kl_one *k = &g_kl.k[i];
		if (!k->on || k->hobj != h)
			continue;
		/* never into the player's body: its root kept min_cm from Talion's (the fight space's 90 cm is off for it) */
		uint32_t s0 = atomic_load(&g_kl.rseq);
		float r[3] = { g_kl.root[0], g_kl.root[1], g_kl.root[2] };
		if (!(s0 & 1) && s0 && atomic_load(&g_kl.rseq) == s0) {
			float dx = xf[0] - r[0], dz = xf[2] - r[2], dd = sqrtf(dx * dx + dz * dz);
			if (dd > 1e-3f && dd < g_kl.min_cm)
				xf[0] = r[0] + dx / dd * g_kl.min_cm, xf[2] = r[2] + dz / dd * g_kl.min_cm, atomic_fetch_add(&g_kl.held, 1);
		}
		if (!atomic_load(&k->al_on))
			return 1;
		atomic_fetch_add(&k->al_calls, 1);
		float e = somvr_kl_ease(mono() - k->al_t0, g_kl.align_s);
		uint32_t a0 = atomic_load(&k->al_applied_k), a1 = (uint32_t)(e * 1e6f);
		if (a1 <= a0)
			return 1;
		atomic_store(&k->al_applied_k, a1);
		float s = (float)(a1 - a0) * 1e-6f;
		xf[0] += k->al_dpos[0] * s, xf[2] += k->al_dpos[1] * s;
		/* the turn: xf's rotation (x y z w after the position, LTRigidTransform) - checked against the object's own */
		float oq[4];
		if (peek(h + OBJ_ROT, oq, sizeof oq) && vec_ok(xf + 3, 2.0f)) {
			float d = fabsf(oq[0] * xf[3] + oq[1] * xf[4] + oq[2] * xf[5] + oq[3] * xf[6]);
			if (d > 0.95f) {
				atomic_fetch_add(&k->al_rot_ok, 1);
				float a = k->al_dyaw * s * 0.5f, sy = sinf(a), cy = cosf(a);
				float x = xf[3], y = xf[4], z = xf[5], w = xf[6]; /* q' = (0, sy, 0, cy) * q */
				xf[3] = cy * x + sy * z, xf[4] = cy * y + sy * w, xf[5] = cy * z - sy * x, xf[6] = cy * w - sy * y;
			} else
				atomic_fetch_add(&k->al_rot_bad, 1);
		}
		return 1;
	}
	return 0;
}

/* every combat frame (combat_frame, after xc_frame): each kill's moment; the catalogue's play */
static void kl_play_frame(uintptr_t pcomb, uintptr_t pc, const float *root, double t);
static void kl_frame(uintptr_t pcomb, uintptr_t pc, const float *root, double t)
{
	if (!g_kl.ok)
		return;
	int m = atomic_exchange(&g_kl.req_mode, 0);
	if (m)
		g_kl.on = m == 2, logf_("KILLS t=%.3f %s (the word)", t, g_kl.on ? "on" : "off");
	if (atomic_exchange(&g_kl.req_status, 0))
		logf_("KILLS status t=%.3f detail: down %llu, not playing %llu, unaware refused %llu; AI starts held %llu", t,
		      (unsigned long long)g_kl.by_down, (unsigned long long)g_kl.by_stall, (unsigned long long)g_kl.refused_unaware,
		      (unsigned long long)atomic_load(&g_kl.ai_held)),
		logf_("KILLS status t=%.3f %s: takes %llu kills %llu (by the moment %llu, the end %llu, a cut %llu, a fall %llu, the "
		      "timeout %llu, dead by another %llu), refused %llu, no row %llu, start failed %llu, severs %llu, hits skipped "
		      "%llu, placed %llu (too far %llu); by who: blow %llu execution %llu stealth %llu offered %llu downed %llu "
		      "test %llu", t, g_kl.on ? "on" : "off", (unsigned long long)g_kl.takes, (unsigned long long)g_kl.kills,
		      (unsigned long long)g_kl.by_moment, (unsigned long long)g_kl.by_end, (unsigned long long)g_kl.by_cut,
		      (unsigned long long)g_kl.by_fall, (unsigned long long)g_kl.by_timeout, (unsigned long long)g_kl.by_dead,
		      (unsigned long long)g_kl.refused, (unsigned long long)g_kl.no_row, (unsigned long long)g_kl.start_fail,
		      (unsigned long long)g_kl.severs, (unsigned long long)g_kl.skipped_hits, (unsigned long long)g_kl.aligned,
		      (unsigned long long)g_kl.align_far, (unsigned long long)g_kl.by_who[0], (unsigned long long)g_kl.by_who[1],
		      (unsigned long long)g_kl.by_who[2], (unsigned long long)g_kl.by_who[3], (unsigned long long)g_kl.by_who[4],
		      (unsigned long long)g_kl.by_who[5]);
	g_kl.pcomb = pcomb;
	kl_play_frame(pcomb, pc, root, t);
	kl_sync_frame(pc, t);
	{ /* helper 134's blade state on an orc that is gone (or another orc now at its address: the practice's respawns reuse
	   * them) holds every later contact on that Combat - out (boot 20261102-140609: 150 contacts, no hit) */
		uintptr_t sh = 0, sc = swp_held_orc(&sh);
		if (sc && peek_ptr(sc + C_HOBJ) != sh)
			swp_release("its orc gone (SOMVR_KILLS' check)"), g_kl.swp_gone++;
	}
	if (root) { /* for kl_aim's hold */
		atomic_fetch_add(&g_kl.rseq, 1);
		memcpy(g_kl.root, root, sizeof g_kl.root);
		atomic_fetch_add(&g_kl.rseq, 1);
	}
	for (int i = 0; i < KL_N; i++) {
		struct kl_one *k = &g_kl.k[i];
		if (!k->on)
			continue;
		struct cb_orc o;
		if (!cb_orc_get(k->c, &o) || cb_dead(k->c)) {
			g_kl.by_dead++;
			logf_("KILLS t=%.3f orc %#lx: %s %.2f s into '%s' - its kill over", t, (unsigned long)k->c,
			      cb_orc_get(k->c, &o) ? "dead by another" : "gone", kl_gnow() - k->gt0, k->vname);
			kl_end(k);
			continue;
		}
		double el = kl_gnow() - k->gt0;
		float nd[4][3], head = NAN;
		double el0 = kl_gnow() - k->gt0;
		if (stl_body(&o, nd) && isfinite(k->head_min)) {
			head = nd[3][1] - k->pos0[1];
			if (!(fabsf(head - k->head_still) <= 3.0f))
				k->head_still = head, k->still_t = el0; /* the last time the head moved 3 cm */
			if (head < k->head_min)
				k->head_min = head, k->head_min_t = (float)el0;
			float dv[3] = { nd[3][0] - o.pos[0] - k->hv0[0], nd[3][1] - o.pos[1] - k->hv0[1], nd[3][2] - o.pos[2] - k->hv0[2] };
			k->disp_max = fmaxf(k->disp_max, sqrtf(dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]));
		}
		if (atomic_load(&k->al_on) && mono() - k->al_t0 > g_kl.align_s + 0.05) {
			atomic_store(&k->al_on, 0);
			logf_("KILLS place t=%.3f orc %#lx: the turn %+.1f deg and slide %.0f,%.0f cm done (%u movement calls, the "
			      "rotation %s: %u checked, %u not the object's); now %.0f deg off Talion, %.0f cm from him", t,
			      (unsigned long)k->c, k->al_dyaw * 57.29578f, k->al_dpos[0], k->al_dpos[1], atomic_load(&k->al_calls),
			      atomic_load(&k->al_rot_bad) ? "NOT always" : "the object's", atomic_load(&k->al_rot_ok),
			      atomic_load(&k->al_rot_bad), root ? fabsf(sv_wrap_pi(atan2f(root[0] - o.pos[0], root[2] - o.pos[2]) -
			      kl_yaw(o.hobj))) * 57.29578f : -1.0f, root ? hypotf(root[0] - o.pos[0], root[2] - o.pos[2]) : -1.0f);
		}
		uintptr_t cur = peek_ptr(k->c + VB_C_ATTACK);
		float da = k->row >= 0 ? somvr_kl_death_at(&g_kl_rows[k->row], g_kl.lead_s) : NAN;
		if (!isfinite(da))
			da = k->blow_state == SOMVR_KL_DOWNED ? fminf(g_kl.default_s, 2.5f) : g_kl.default_s; /* the cap (the victim's
				* record stays on it past its end); a downed orc's half is short (boot 183248: 5 s on the ground) */
		const char *why = NULL;
		static char cutw[160];
		if (cur != k->vrec && el > 0.05) {
			int cut = el < (isfinite(da) ? da - 0.3 : 0.6);
			char an[96];
			snprintf(cutw, sizeof cutw, "%s (its attack now '%s')", cut ? "its animation cut" : "its animation's end",
				 vb_name(cur, an, sizeof an));
			why = cutw;
			if (cut)
				g_kl.by_cut++;
			else
				g_kl.by_end++;
		} else if (isfinite(head) && k->blow_state == SOMVR_KL_DOWNED && el >= 0.6 && el - k->still_t >= 0.8) {
			why = "its half over (a downed orc: the head still for 0.8 s)"; /* (its head is on the ground already: no drop -
										   * boot 182739 read that as "not playing") */
			g_kl.by_down++;
		} else if (isfinite(head) && k->blow_state != SOMVR_KL_DOWNED && el >= g_kl.stall_s &&
			   k->head0 - k->head_min < g_kl.stall_drop_cm) {
			snprintf(cutw, sizeof cutw, "its half not playing (the head %.1f cm down in %.2f s)", k->head0 - k->head_min, el);
			why = cutw; /* (an orc whose animation state takes no attack: no wait) */
			g_kl.by_stall++;
		} else if (isfinite(head) && el >= 0.4 && k->head_min < k->head0 - g_kl.down_cm &&
			   (head > k->head_min + 8.0f || el - k->head_min_t >= 0.2)) {
			snprintf(cutw, sizeof cutw, "down (the head %.0f cm lower at +%.2f s, its fall over)", k->head0 - k->head_min,
				 (double)k->head_min_t);
			why = cutw;
			g_kl.by_down++;
		} else if (isfinite(head) && el >= 0.4 && k->head0 - k->head_min >= g_kl.stall_drop_cm && el - k->still_t >= 0.6) {
			snprintf(cutw, sizeof cutw, "settled (the head %.0f cm down, still for 0.6 s: the half's end pose)",
				 k->head0 - k->head_min);
			why = cutw; /* (a kneeling end: under the down rule's 25 cm - boot 145852's stunned kill ran to the cap) */
			g_kl.by_down++;
		} else if (isfinite(da) && el >= da) {
			why = "the row's moment";
			g_kl.by_moment++;
		} else if (k->pos0[1] - o.pos[1] > 150.0f) {
			why = "a fall";
			g_kl.by_fall++;
		} else if (el >= g_kl.timeout_s) {
			why = "the timeout";
			g_kl.by_timeout++;
		}
		if (!why)
			continue;
		if (kl_deal(pc, k, why, t) || ++k->tries >= 3) {
			if (k->tries >= 3 && !cb_dead(k->c))
				logf_("WARN KILLS t=%.3f orc %#lx: not dead after 3 DealHits ('%s')", t, (unsigned long)k->c, k->dname);
			kl_end(k);
		}
	}
}

/* ---- the catalogue (test only): a victim record played on the nearest orc, its trace; no death ------------------ */
static void kl_play_start(uintptr_t pcomb, uintptr_t pc, const float *root, const char *name, float s, double t)
{
	struct cb_orc o[1];
	uintptr_t rec = kl_byname(name);
	if (!root || !rec || cb_orcs_r(pcomb, pc, root, 500, o, 1) != 1) {
		logf_("KILLS play t=%.3f '%s': %s", t, name, !rec ? "not in Combat/Attack" : "no orc within 5 m");
		return;
	}
	if (g_kl.pl.free_at)
		kl_free_back(g_kl.pl.free_at, g_kl.pl.free_was), g_kl.pl.free_at = 0;
	uintptr_t fat = 0;
	uint32_t fwas = 0;
	int freed = g_kl.free && kl_free(rec, &fat, &fwas);
	char ad[600];
	kl_anim_desc(rec, ad, sizeof ad);
	if (g_kl.pl.ai && g_kl.ok_nav)
		KL_FN(KLS_NAV, kl_nav_fn)(g_kl.pl.ai, 1, KL_NAV_BIT);
	int prepped = kl_prep(o[0].c);
	kl_brain(o[0].c);
	KL_FN(KLS_START, kl_start_fn)(o[0].c, 0, rec, peek_ptr(pc + C_HOBJ), 0);
	uintptr_t now = peek_ptr(o[0].c + VB_C_ATTACK);
	float nd[4][3];
	int sk = stl_body(&o[0], nd);
	memset(&g_kl.pl, 0, sizeof g_kl.pl);
	g_kl.pl.on = now == rec, g_kl.pl.c = o[0].c, g_kl.pl.hobj = o[0].hobj, g_kl.pl.rec = rec, g_kl.pl.t0 = t;
	g_kl.pl.gt0 = kl_gnow(), g_kl.pl.until = t + (s > 0 ? s : 6.0f), g_kl.pl.next = t + 0.1;
	snprintf(g_kl.pl.name, sizeof g_kl.pl.name, "%s", name);
	g_kl.pl.free_at = fat, g_kl.pl.free_was = fwas;
	g_kl.pl.ai = g_kl.ok_nav ? peek_ptr(o[0].c + C_AI) : 0;
	if (g_kl.pl.ai)
		KL_FN(KLS_NAV, kl_nav_fn)(g_kl.pl.ai, 0, KL_NAV_BIT); /* held as a kill holds it (and vetoed from attacks) */
	memcpy(g_kl.pl.pos0, o[0].pos, sizeof g_kl.pl.pos0);
	g_kl.pl.yaw0 = kl_yaw(o[0].hobj), g_kl.pl.head0 = g_kl.pl.head_min = sk ? nd[3][1] - o[0].pos[1] : NAN;
	g_kl.plays++;
	char an[96];
	char sw[200];
	struct stl_aw aw;
	memset(&aw, 0, sizeof aw);
	stl_aware(&o[0], peek_ptr(pc + C_HOBJ), &aw);
	kl_state_of(o[0].c, 0, sw, sizeof sw);
	logf_("KILLS play t=%.3f '%s' on orc %#lx at %.0f cm (%s, aware %d): %s (its attack '%s'); the head %+.0f cm over its root; "
	      "its AnimationData: %s%s%s", t, name, (unsigned long)o[0].c, o[0].dist, sw, aw.game,
	      now == rec ? "started" : "NOT started", vb_name(now, an, sizeof an), g_kl.pl.head0, ad,
	      freed ? "; SyncToAnim off for it (SOMVR_KILLS_FREE)" : "", prepped ? "; readied as a sync target" : "");
}
static void kl_play_frame(uintptr_t pcomb, uintptr_t pc, const float *root, double t)
{
	if (atomic_exchange(&g_kl.req_play, 0)) {
		g_kl.cat_on = 0;
		kl_play_start(pcomb, pc, root, g_kl.req_name, g_kl.req_s, t);
	}
	int cf = atomic_exchange(&g_kl.req_cat, 0);
	if (cf)
		g_kl.cat_on = 1, g_kl.cat_i = cf - 1, g_kl.cat_next = t;
	if (atomic_exchange(&g_kl.req_sever, 0)) {
		struct cb_orc o[1];
		if (root && cb_orcs_r(pcomb, pc, root, 500, o, 1) == 1 && kl_sever_type() && g_xc.ok_sever && peek_ptr(o[0].c + 0x90)) {
			float vel[3] = { 0, 150, 300 }, rot[3] = { 0, 0, 0 };
			XC_FN(XCS_SEVER, xc_sever_fn)(o[0].c, g_kl.sever_head, vel, rot, peek_ptr(pc + C_HOBJ));
			logf_("KILLS sever t=%.3f orc %#lx (alive, health %.1f, dead %d): severed (XC_SEVER)", t, (unsigned long)o[0].c,
			      cb_health(o[0].c), cb_dead(o[0].c));
		} else
			logf_("KILLS sever t=%.3f: no orc within 5 m / no sever type", t);
	}
	if (atomic_exchange(&g_kl.req_kill, 0)) {
		struct cb_orc o[1];
		int row = somvr_kl_find(g_kl_rows, KL_ROWS, g_kl.req_name);
		if (row >= 0 && root && cb_orcs_r(pcomb, pc, root, 500, o, 1) == 1) {
			float p[3] = { o[0].pos[0], o[0].pos[1] + 110.0f, o[0].pos[2] }, v[3] = { 0, 0, 0 }, ax[3] = { 0, 0, 1 };
			for (int j = 0; j < 3; j += 2)
				v[j] = (o[0].pos[j] - root[j]) * 5.0f;
			if (!kl_take_row(pc, &o[0], 0, 0, 1, p, v, ax, 0, row, "a test kill", t))
				logf_("KILLS kill t=%.3f '%s': not taken", t, g_kl.req_name);
		} else
			logf_("KILLS kill t=%.3f '%s': %s", t, g_kl.req_name, row < 0 ? "not a row" : "no orc within 5 m");
	}
	if (g_kl.pl.on) {
		struct cb_orc o;
		uintptr_t cur = cb_orc_get(g_kl.pl.c, &o) ? peek_ptr(g_kl.pl.c + VB_C_ATTACK) : 0;
		double el = kl_gnow() - g_kl.pl.gt0;
		float nd[4][3];
		int sk = cur && stl_body(&o, nd);
		if (sk && nd[3][1] - o.pos[1] < g_kl.pl.head_min)
			g_kl.pl.head_min = nd[3][1] - o.pos[1], g_kl.pl.head_min_t = (float)el;
		if (cur)
			g_kl.pl.moved = fmaxf(g_kl.pl.moved, hypotf(o.pos[0] - g_kl.pl.pos0[0], o.pos[2] - g_kl.pl.pos0[2]));
		if (cur && t >= g_kl.pl.next) {
			g_kl.pl.next = t + 0.1;
			char an[96];
			float fx = sinf(g_kl.pl.yaw0), fz = cosf(g_kl.pl.yaw0), dx = o.pos[0] - g_kl.pl.pos0[0], dz = o.pos[2] - g_kl.pl.pos0[2];
			logf_("KILLS play +%.2f '%s': attack '%s' root %+.0f ahead %+.0f right %+.0f up cm, yaw %+.0f deg; pelvis/neck/"
			      "head %+.0f/%+.0f/%+.0f cm; dead %d health %.1f", el, g_kl.pl.name, vb_name(cur, an, sizeof an),
			      dx * fx + dz * fz, dx * fz - dz * fx, o.pos[1] - g_kl.pl.pos0[1],
			      sv_wrap_pi(kl_yaw(o.hobj) - g_kl.pl.yaw0) * 57.29578f, sk ? nd[0][1] - o.pos[1] : NAN,
			      sk ? nd[2][1] - o.pos[1] : NAN, sk ? nd[3][1] - o.pos[1] : NAN, cb_dead(g_kl.pl.c), cb_health(g_kl.pl.c));
		}
		if (!cur || cur != g_kl.pl.rec || t >= g_kl.pl.until) {
			char an[96];
			logf_("KILLS play END '%s': %.2f s (%s; its attack now '%s'), the root moved up to %.0f cm, the head %+.0f -> "
			      "lowest %+.0f cm at +%.2f s", g_kl.pl.name, el, !cur ? "the orc gone" : cur != g_kl.pl.rec ? "the attack "
			      "left the record" : "the trace's limit", vb_name(cur, an, sizeof an), g_kl.pl.moved, g_kl.pl.head0,
			      g_kl.pl.head_min, g_kl.pl.head_min_t);
			g_kl.pl.on = 0;
			kl_free_back(g_kl.pl.free_at, g_kl.pl.free_was), g_kl.pl.free_at = 0;
			if (g_kl.pl.ai && cur)
				KL_FN(KLS_NAV, kl_nav_fn)(g_kl.pl.ai, 1, KL_NAV_BIT);
			g_kl.pl.ai = 0;
			g_kl.cat_next = t + 1.5;
		}
	}
	if (g_kl.cat_on && !g_kl.pl.on && t >= g_kl.cat_next) {
		if (g_kl.cat_i >= KL_ROWS) {
			g_kl.cat_on = 0;
			logf_("KILLS cat t=%.3f done (%d rows)", t, KL_ROWS);
		} else
			kl_play_start(pcomb, pc, root, g_kl_rows[g_kl.cat_i++].name, 6.0f, t);
	}
}

/* ---- the words (SOMVR_HANDS_TEST; the words' thread: requests the game thread takes) ------------------------------ */
static const char *kl_word(const char *w)
{
	if (!strcmp(w, "status"))
		return atomic_store(&g_kl.req_status, 1), "kills status";
	if (!strcmp(w, "on") || !strcmp(w, "off"))
		return atomic_store(&g_kl.req_mode, w[1] == 'n' ? 2 : 1), w[1] == 'n' ? "kills on" : "kills off";
	if (!strcmp(w, "free on") || !strcmp(w, "free off"))
		return g_kl.free = w[6] == 'n', g_kl.free ? "kills free on (the victim's SyncToAnim off while it plays)" : "kills free off";
	if (!strcmp(w, "brain on") || !strcmp(w, "brain off"))
		return g_kl.brain = w[7] == 'n', g_kl.brain ? "kills brain on" : "kills brain off";
	if (!strcmp(w, "aihold on") || !strcmp(w, "aihold off"))
		return g_kl.ai_hold = w[8] == 'n', g_kl.ai_hold ? "kills aihold on" : "kills aihold off";
	if (!strcmp(w, "unaware on") || !strcmp(w, "unaware off") || !strcmp(w, "unaware wake") || !strcmp(w, "unaware sync"))
		return g_kl.unaware = w[8] == 'w' ? 2 : w[8] == 's' ? 3 : w[9] == 'n', g_kl.unaware == 3 ? "kills unaware sync" :
		       g_kl.unaware == 2 ? "kills unaware wake" : g_kl.unaware ? "kills unaware on" : "kills unaware off";
	if (!strcmp(w, "prep on") || !strcmp(w, "prep off"))
		return g_kl.prep = w[6] == 'n', g_kl.prep ? "kills prep on (readied as a sync target)" : "kills prep off";
	if (!strcmp(w, "sever"))
		return atomic_store(&g_kl.req_sever, 1), "kills sever (a test: the nearest orc's head, alive)";
	if (!strncmp(w, "cat", 3)) {
		int from = atoi(w + 3);
		atomic_store(&g_kl.req_cat, (from > 0 && from < KL_ROWS ? from : 0) + 1);
		return "kills cat (every row played on the nearest orc in turn)";
	}
	if (!strncmp(w, "play ", 5) || !strncmp(w, "kill ", 5)) {
		char nm[96] = "";
		float s = 0;
		if (sscanf(w + 5, "%95s %f", nm, &s) < 1)
			return "kills play|kill NAME";
		snprintf(g_kl.req_name, sizeof g_kl.req_name, "%s", nm);
		g_kl.req_s = s;
		atomic_store(w[0] == 'p' ? &g_kl.req_play : &g_kl.req_kill, 1);
		return w[0] == 'p' ? "kills play (a test: the victim record on the nearest orc, no death)" : "kills kill (a test kill)";
	}
	return NULL;
}

static void kl_start(int *bad)
{
	memset(&g_kl, 0, sizeof g_kl);
	static const char *const mode_w[] = { "off", "on" };
	g_kl.on = env_word("SOMVR_KILLS", mode_w, 2, 0);
	static const char *const exec_w[] = { "trigger", "blow" };
	g_kl.exec_blow = env_word("SOMVR_KILLS_EXEC", exec_w, 2, 1);
	static const char *const sev_w[] = { "death", "blow" };
	g_kl.sever_blow = env_word("SOMVR_KILLS_SEVER", sev_w, 2, 0);
	g_kl.free = env_word("SOMVR_KILLS_FREE", mode_w, 2, 0);
	g_kl.prep = env_word("SOMVR_KILLS_PREP", mode_w, 2, 0); /* (no difference seen: boot 131221) */
	g_kl.align_s = env_float("SOMVR_KILLS_ALIGN_MS", 150, 0, 500, bad) * 1e-3f;
	g_kl.align_deg = env_float("SOMVR_KILLS_ALIGN_DEG", 45, 0, 90, bad);
	g_kl.align_cm = env_float("SOMVR_KILLS_ALIGN_CM", 30, 0, 60, bad);
	g_kl.min_cm = env_float("SOMVR_KILLS_MIN_CM", 55, 0, 120, bad);
	g_kl.timeout_s = env_float("SOMVR_KILLS_TIMEOUT_S", 6, 1, 20, bad);
	g_kl.lead_s = env_float("SOMVR_KILLS_LEAD_S", 0.10f, 0, 1, bad);
	g_kl.default_s = env_float("SOMVR_KILLS_MAX_S", 5.0f, 0.3f, 10, bad);
	g_kl.ai_hold = env_word("SOMVR_KILLS_AI_HOLD", mode_w, 2, 1);
	g_kl.brain = env_word("SOMVR_KILLS_BRAIN", mode_w, 2, 0); /* (no difference seen: boot 144518) */
	g_kl.stall_s = env_float("SOMVR_KILLS_STALL_MS", 800, 100, 2000, bad) * 1e-3f; /* (idle breathing moves the head 5-8 cm) */
	g_kl.stall_cm = env_float("SOMVR_KILLS_STALL_CM", 12, 1, 40, bad);
	/* ... or its head not 8 cm lower by then (boot 144518: a standing unaware orc's head swayed 12+ cm, 0 down, 5 s) */
	g_kl.stall_drop_cm = env_float("SOMVR_KILLS_STALL_DROP_CM", 8, 0, 60, bad);
	g_kl.down_cm = env_float("SOMVR_KILLS_DOWN_CM", 25, 5, 120, bad);
	static const char *const un_w[] = { "off", "on", "wake", "sync" };
	g_kl.unaware = env_word("SOMVR_KILLS_UNAWARE", un_w, 4, 0);
	g_kl.stab_cos = cosf(env_float("SOMVR_KILLS_STAB_DEG", 60, 10, 85, bad) * 0.01745329f);
	g_kl.neck_up_cm = 140;
	g_kl.start_mode = g_kl.on;
	char why[256];
	int ok_start = check_site(&g_kl_sites[KLS_START], why, sizeof why) == 0;
	if (!ok_start)
		logf_("WARN SOMVR_KILLS: the site KL_START did not match (%s): off", why);
	g_kl.ok_nav = check_site(&g_kl_sites[KLS_NAV], why, sizeof why) == 0;
	if (!g_kl.ok_nav)
		logf_("WARN SOMVR_KILLS: the site KL_NAV did not match (%s): the orc's navigation not held", why);
	g_kl.ok_prep = check_site(&g_kl_sites[KLS_PREP_A], why, sizeof why) == 0 &&
		       check_site(&g_kl_sites[KLS_PREP_B], why, sizeof why) == 0 && check_site(&g_kl_sites[KLS_PREP_C], why, sizeof why) == 0;
	if (!g_kl.ok_prep)
		logf_("WARN SOMVR_KILLS: a KL_PREP site did not match (%s): no readying", why);
	g_kl.ok_sync_end = check_site(&g_kl_sites[KLS_SYNC_END], why, sizeof why) == 0;
	if (!g_kl.ok_sync_end)
		logf_("WARN SOMVR_KILLS: KL_SYNC_END did not match (%s): a held orc's kill stays the old one", why);
	int nai = 0;
	if (ok_start && g_kl.ai_hold)
		for (int i = KLS_AI_NPC; i <= KLS_AI_MISC; i++)
			nai += bo_patch_call(&g_kl_sites[i], (void *)somvr_w_klai);
	g_kl.ok_ai = nai;
	if (g_kl.ai_hold && nai != 4)
		logf_("WARN SOMVR_KILLS: the AI hold's call sites: %d of 4 patched", nai);
	*bad += !ok_start + !g_kl.ok_nav + !g_kl.ok_prep + !g_kl.ok_sync_end + (g_kl.ai_hold && nai != 4);
	g_kl.ok = ok_start;
	logf_("CONFIG kills=%s (SOMVR_KILLS): a kill by the player's hand plays the victim's half of the game's paired kill on "
	      "the orc alone (%d rows; Talion's half never started), the death at the row's moment with the game's DealHit; "
	      "the execution %s (SOMVR_KILLS_EXEC); the sever at the %s; placement %.0f ms, %.0f deg, %.0f cm (inside %.0f cm); "
	      "the timeout %.1f s, the lead %.2f s; a stab within %.0f deg of the blade%s", g_kl.on ? "on" : "off", KL_ROWS,
	      g_kl.exec_blow ? "= the next heavy blow while charged (no button)" : "= a heavy hit with the trigger held (build 30)",
	      g_kl.sever_blow ? "blow" : "death", (double)g_kl.align_s * 1e3, (double)g_kl.align_deg, (double)g_kl.align_cm,
	      (double)g_kl.min_cm, (double)g_kl.timeout_s, (double)g_kl.lead_s, acosf(g_kl.stab_cos) * 57.29578f,
	      g_kl.ok ? "" : " - OFF (a site)");
}
static void kl_end_log(void)
{
	if (!g_kl.ok)
		return;
	logf_("KILLS END sync: starts %llu, the game's kills %llu, forced %llu, not offered %llu", (unsigned long long)g_kl.sync_starts,
	      (unsigned long long)g_kl.sync_kills, (unsigned long long)g_kl.sync_forced, (unsigned long long)g_kl.sync_fail);
	logf_("KILLS END detail: down %llu, not playing %llu, unaware refused %llu; the AI hold (%d sites): %llu starts held, "
	      "%llu passed", (unsigned long long)g_kl.by_down, (unsigned long long)g_kl.by_stall,
	      (unsigned long long)g_kl.refused_unaware, g_kl.ok_ai, (unsigned long long)atomic_load(&g_kl.ai_held),
	      (unsigned long long)atomic_load(&g_kl.ai_passed));
	logf_("KILLS END %s: takes %llu kills %llu (the moment %llu, the end %llu, cut %llu, fall %llu, timeout %llu, dead by "
	      "another %llu), refused %llu, no row %llu, start failed %llu, severs %llu, hits skipped %llu, placed %llu (too far "
	      "%llu), plays %llu, held off the body %llu, attacks vetoed %llu; by who: blow %llu execution %llu stealth %llu offered %llu downed %llu "
	      "test %llu", g_kl.on ? "on" : "off", (unsigned long long)g_kl.takes, (unsigned long long)g_kl.kills,
	      (unsigned long long)g_kl.by_moment, (unsigned long long)g_kl.by_end, (unsigned long long)g_kl.by_cut,
	      (unsigned long long)g_kl.by_fall, (unsigned long long)g_kl.by_timeout, (unsigned long long)g_kl.by_dead,
	      (unsigned long long)g_kl.refused, (unsigned long long)g_kl.no_row, (unsigned long long)g_kl.start_fail,
	      (unsigned long long)g_kl.severs, (unsigned long long)g_kl.skipped_hits, (unsigned long long)g_kl.aligned,
	      (unsigned long long)g_kl.align_far, (unsigned long long)g_kl.plays, (unsigned long long)atomic_load(&g_kl.held),
	      (unsigned long long)atomic_load(&g_kl.vetoes),
	      (unsigned long long)g_kl.by_who[0],
	      (unsigned long long)g_kl.by_who[1], (unsigned long long)g_kl.by_who[2], (unsigned long long)g_kl.by_who[3],
	      (unsigned long long)g_kl.by_who[4], (unsigned long long)g_kl.by_who[5]);
}

/* SOMVR_EXECUTE (xc_contact): the execution is the next heavy blow while charged (no button: the player always holds
 * the trigger with the grip, the user's mark 20) */
static int kl_exec_blow(void) { return g_kl.on && g_kl.ok && g_kl.exec_blow; }
static int kl_on(void) { return g_kl.on && g_kl.ok; }

/* cb_hit's ordinary killing blow: the hit dealt with the orc's min health at 1 (the game's own clamp, PR_SETMINHEALTH:
 * CCharacter+0x9b4), so the blow that would kill is known exactly; back after the call. Only where kl_take could take it */
static void kl_floor_on(const struct cb_orc *o, struct kl_floor *f)
{
	memset(f, 0, sizeof *f);
	if (!g_kl.on || !g_kl.ok || !g_pr.ok_mh || !o || cb_dead(o->c) || kl_of(o->c) || kp_of_combat(o->c) ||
	    kl_is_beast(o->c, o->hobj) || cb_sync(o->c))
		return;
	uintptr_t own = peek_ptr(o->c + C_OWNER);
	float hp = cb_health(o->c), was = 0;
	if (!own || !isfinite(hp) || !(hp > 2.05f) || !peek(own + PR_CH_MINHEALTH, &was, sizeof was) || was >= 2.0f)
		return;
	PR_FN(PRS_MINHEALTH, pr_minhealth_fn)(own, 2.0f); /* 2: an orc left at 1.0 is dead (boot 20261102-113313) */
	f->on = 1, f->own = own, f->was = was, f->fl = 2.0f;
	g_kl.floors++;
}
static void kl_floor_off(const struct kl_floor *f)
{
	if (f->on)
		PR_FN(PRS_MINHEALTH, pr_minhealth_fn)(f->own, f->was);
}
/* 1 = the floored hit was the killing blow (its health at the floor, from above it) */
static int kl_floor_hit(const struct kl_floor *f, float hp0, float hp1)
{
	return f->on && isfinite(hp1) && hp1 <= f->fl + 0.01f && hp0 > f->fl + 0.01f;
}
