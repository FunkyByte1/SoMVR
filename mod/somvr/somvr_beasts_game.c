/* somvr_beasts_game.c - SOMVR_BEASTS (build 22, som-b22-beasts; docs/re-xr-pipeline.md 92, handoff/HANDOFF-beasts.md).
 * NOT a translation unit of its own: somvr.c includes it last (it uses the combat module's orcs, THREAT's node calls,
 * fb_rec_name, the log). Nothing of the beasts was made for VR before build 22: combat's blade met every enemy as an
 * orc's upright capsule about its root, THREAT and DEFENCE took the blow from the attacker's R_Hand.
 *   SOMVR_BEASTS=0 (the code's default) | 1 (build 22's env):
 *     - a beast (its character record at Combat+0x4c8: '*Warg*' a caragor, '*Goblin*' a ghul, '*Troll*' a graug) is
 *       met by the blade as its bone chain's capsules (somvr_bz_capsules: the head and snout, neck, chest, pelvis,
 *       tail - the nodes its model has, looked up once by name, placed every frame by GetNodeTransform), not the orc's
 *       upright one: a caragor's low, long body and its head are where the blade finds them;
 *     - THREAT's glow and DEFENCE's blow come from a caragor's Head (its jaws: the bite and the pounce), from a ghul's
 *       or graug's R_Hand as an orc's (their Head when the model has no R_Hand);
 *     - BEASTS lines: each beast seen (its nodes, offsets from its root), its attacks (the record, at whom, how far),
 *       Talion's sync slots and attack while a beast is near.
 *   SOMVR_BEASTS_TRACE=N (0): headless proof - N lines at most of the view's per-frame path (the frame's base, which
 *     the body moments hold, Talion's root, the beast) from a beast's attack at Talion until 3 s after it.
 *   test word `beasts skills [SUBSTR]`: the save's Inventory/Skill/Skills records with the owned bit (riding, 92.4).
 *   test word `beasts grant NAME` (SOMVR_HANDS_TEST only; build 23, som-b23-brand): that record's owned bit set in
 *     Talion's inventory in memory (e.g. PSS_Instant_Brand, the combat brand's skill: re-xr-pipeline.md 97) - a test
 *     aid for the run's own session (what the game saves goes to the run's save copy only).
 *
 * BEASTS_SITES_BEGIN (read only, never patched; checked at start with SOMVR_BEASTS; counted on its own by
 * check_sites.py): BZ_SKILL_CAT FUN_0216d900 (the category's registration: its rip load proves the global 0x4b56640);
 * BZ_HAS_SKILL FUN_02313570 (HasSkill: inv+0x268's bit set, then FUN_023134f0); BZ_SKILL_BIT FUN_023134f0 (the bit
 * of a record: its index in its category); BZ_PURCHASE FUN_0268eee0 (Character.PurchaseSkill: the inventory at
 * CCharacter+0x910). Order = BZS_* */
static const struct site_spec g_bz_sites_game[] = {
SITE("BZ_SKILL_CAT", K_ENTRY, 0x216d900, 0x216d900, "40 0f b6 ce 48 8b 35 35 8d 9e 02 48 89 fa bf 44", 0x216d900, "40 0f b6 ce 48 8b 35 35 8d 9e 02 48 89 fa bf"),
SITE("BZ_HAS_SKILL", K_ENTRY, 0x2313570, 0x2313570, "48 85 f6 74 13 48 81 c7 68 02 00 00 e9 6f ff ff", 0x2313570, "48 85 f6 74 13 48 81 c7 68 02 00 00 e9 6f ff"),
SITE("BZ_SKILL_BIT", K_ENTRY, 0x23134f0, 0x23134f0, "48 85 f6 74 6b 48 8b 46 18 48 8b 50 38 f6 46 01", 0x23134f0, "48 85 f6 74 6b 48 8b 46 18 48 8b 50 38 f6 46"),
SITE("BZ_PURCHASE", K_ENTRY, 0x268eee0, 0x268eee0, "41 54 48 89 f2 89 fe 55 53 48 81 ec 10 04 00 00", 0x268eee0, "41 54 48 89 f2 89 fe 55 53 48 81 ec 10 04 00"),
};
/* BEASTS_SITES_END */
enum { BZS_SKILL_CAT, BZS_HAS_SKILL, BZS_SKILL_BIT, BZS_PURCHASE, BZS_N };
_Static_assert(sizeof g_bz_sites_game / sizeof g_bz_sites_game[0] == BZS_N, "beasts sites");
static const struct site_spec *g_bz_sites = g_bz_sites_game; /* selftest: stand-ins */

#define BZ_SKILL_CAT_PP 0x4b56640 /* Inventory/Skill/Skills (BZ_SKILL_CAT) */
#define BZ_CH_INV 0x910           /* CCharacter: the inventory (BZ_PURCHASE) */
#define BZ_INV_SKILLS 0x268       /* inventory: the owned skills, a bit vector (begin, end) (BZ_HAS_SKILL) */
#define BZ_C_CHARREC 0x4c8        /* Combat: the character record ('Warg_Brown': P7) */
#define BZ_C_ATTACK 0x4d0         /* Combat: the current attack record (THREAT's TH_CURATK) */
#define BZ_C_TARGET 0x4c0         /* Combat: the attack's target HOBJECT (THREAT's TH_ATK_START) */
#define BZ_P_MOUNT 0x1d8          /* Player: the mount HOBJECT (the fallback's FB_P_MOUNT, FB_MOUNT's proof) */
#define BZ_PC_STRUGGLE 0x8d8      /* PlayerCombat: the state struggle, +0x8e0 the sync struggle (re-m1m2.md 6.2) */
#define BZ_MAX 16
#define BZ_NEAR_CM 1500.0f        /* the BEASTS lines: a beast within 15 m */
#define BZ_TRACE_CM 800.0f        /* the trace: an attack of a beast within 8 m at Talion */
#define BZ_TRACE_TAIL_S 3.0

static const char *const g_bz_part_names[SOMVR_BZP_N][5] = {
	{ "Head", "Jaw", "Bip_Head", NULL },
	{ "Neck", "Neck1", "Neck2", "Neck_1", NULL },
	{ "Torso", "Spine2", "Spine1", "Chest", "Spine" },
	{ "Pelvis", "Hips", "Spine0", NULL },
	{ "Tail2", "Tail1", "Tail", "Tail_1", "Tail01" },
};
/* logged once per beast with their places (the RT: which names its model has) */
static const char *const g_bz_extra_names[] = { "R_Hand", "L_Hand", "R_Foot", "L_Foot", "R_Arml", "L_Arml", "R_Legl",
						"L_Legl", "R_Legu", "Mouth", "Tongue", "Nose", "R_Weapon", "World_Ground",
						"Saddle", "SocketSaddle", "Rider", "SocketRider", "Mount", "SocketMount" };

/* the limbs' nodes (a graug's legs and arms; the fists for DEFENCE): BZN_* */
enum { BZN_R_LEGU, BZN_L_LEGU, BZN_R_LEGL, BZN_L_LEGL, BZN_R_FOOT, BZN_L_FOOT, BZN_R_ARML, BZN_L_ARML, BZN_R_HAND,
       BZN_L_HAND, BZN_N };
static const char *const g_bz_limb_names[BZN_N] = { "R_Legu", "L_Legu", "R_Legl", "L_Legl", "R_Foot", "L_Foot", "R_Arml",
						     "L_Arml", "R_Hand", "L_Hand" };
struct bz_one {
	uintptr_t c, hobj;
	int kind;
	char rec[64];
	int nodes_st;                  /* 0 not looked up, 1 done */
	uint32_t node[SOMVR_BZP_N], lnode[BZN_N];
	int have[SOMVR_BZP_N], lhave[BZN_N];
	uintptr_t atk;                 /* the attack record last frame */
	double seen_t, last_t;
};

static struct {
	int on, ok_skill, trace_max, hand_head;
	somvr_bz_shape shape[SOMVR_BZ_N];
	struct bz_one b[BZ_MAX];
	_Atomic int req_skills;
	char skill_sub[48];
	char grant[48]; /* build 23: `beasts grant NAME` (a test aid) */
	uintptr_t t_sync[2], t_atk, t_strug[2]; /* Talion's Combat +0x188/+0x190, his attack, PlayerCombat's struggles */
	uintptr_t mount;                        /* the Player's mount HOBJECT (P+0x1d8) last frame */
	double trace_until;
	int trace_lines;
	uint64_t seen, attacks, at_talion, sweeps, contacts, chain_frames, fb_frames, blow_frames, lines, suppressed;
	int kills[SOMVR_BZ_N];
} g_bz;

static struct log_late g_bz_late; /* build 27 */
static int bz_log_ok(void)
{
	if (g_bz.lines < 600)
		return ++g_bz.lines;
	if (log_late_ok(&g_bz_late, 100, mono())) /* build 27: then 100 a 10 minutes */
		return 1;
	g_bz.suppressed++;
	return 0;
}

static void bz_start(int *bad)
{
	memset(&g_bz, 0, sizeof g_bz);
	static const char *const w[] = { "0", "1" };
	g_bz.on = env_word("SOMVR_BEASTS", w, 2, 0);
	g_bz.trace_max = (int)env_float("SOMVR_BEASTS_TRACE", 0, 0, 20000, bad);
	for (int k = 0; k < SOMVR_BZ_N; k++)
		somvr_bz_shape_default(k, &g_bz.shape[k]);
	if (!g_bz.on)
		return;
	char why[160];
	g_bz.ok_skill = 1;
	for (int i = 0; i < BZS_N; i++) {
		int ok = check_site(&g_bz_sites[i], why, sizeof why) == 0;
		g_bz.ok_skill &= ok;
		if (!ok)
			logf_("WARN SOMVR_BEASTS: site %s MISMATCH (%s): no skills list", g_bz_sites[i].name, why), *bad = 1;
	}
	const somvr_bz_shape *c = &g_bz.shape[SOMVR_BZ_CARAGOR];
	logf_("CONFIG beasts=1 (SOMVR_BEASTS): caragors ('*Warg*'), ghuls ('*Goblin*'), graugs ('*Troll*') met by the blade as "
	      "their bone chain's capsules (a caragor: head %.0f cm + the snout %.0f cm, body %.0f, tail %.0f; no chain: %.0f cm "
	      "along its facing), THREAT's and DEFENCE's blow from a caragor's Head (ghuls, graugs: R_Hand); the trace %d lines; "
	      "the skills' sites %s", c->r_head, c->snout, c->r_body, c->r_tail, c->fb_r, g_bz.trace_max,
	      g_bz.ok_skill ? "ok" : "MISMATCH");
}

/* the beast entry of Combat c (made on first sight; NULL = not a beast, or off) */
static struct bz_one *bz_get(uintptr_t c, uintptr_t hobj, double t)
{
	if (!g_bz.on || !c)
		return NULL;
	int free_i = -1, old_i = 0;
	for (int i = 0; i < BZ_MAX; i++) {
		if (g_bz.b[i].c == c && g_bz.b[i].hobj == hobj)
			return g_bz.b[i].last_t = t, g_bz.b[i].kind ? &g_bz.b[i] : NULL;
		if (!g_bz.b[i].c && free_i < 0)
			free_i = i;
		if (g_bz.b[i].last_t < g_bz.b[old_i].last_t)
			old_i = i;
	}
	struct bz_one *e = &g_bz.b[free_i >= 0 ? free_i : old_i];
	memset(e, 0, sizeof *e);
	e->c = c, e->hobj = hobj, e->seen_t = e->last_t = t;
	fb_rec_name(peek_ptr(c + BZ_C_CHARREC), e->rec, sizeof e->rec);
	e->kind = somvr_bz_kind(e->rec);
	return e->kind ? e : NULL;
}

static int bz_node_pos(uintptr_t m, uintptr_t obj, uint32_t node, float out[3])
{
	float xf[16] = { 0 };
	if ((((nodexf_fn)g_node_sites[NS_NODEXF].target)((void *)m, obj, node, xf, 1) & 0xff) || !vec_ok(xf, 1e7f))
		return 0;
	memcpy(out, xf, 12);
	return 1;
}

static int bz_node_id(uintptr_t m, uintptr_t obj, const char *name, uint32_t *node)
{
	*node = 0;
	return !(TH_FN(THS_GETNODE, th_getnode_fn)(m, obj, name, node) & 0xff);
}

/* once per beast: its parts' nodes by name, and the log of every name the model has (offsets from the root, cm) */
static void bz_nodes(struct bz_one *e, const float root[3], double t)
{
	e->nodes_st = 1;
	uintptr_t m = peek_ptr(g_som.model_pp), mi = peek_ptr(e->hobj + OBJ_MODEL), mv = vptr_of((void *)mi);
	uintptr_t f = mv ? peek_ptr(mv + 0x20) : 0;
	int pr = f ? page_prot(f) : -1;
	if (!g_th.ok_node || !m || pr < 0 || !(pr & PROT_EXEC)) {
		if (bz_log_ok())
			logf_("BEASTS t=%.3f %s '%s' Combat=%#lx: no node calls (THREAT's GetNode %s, the model %#lx): the facing's "
			      "capsule", t, somvr_bz_kind_name(e->kind), e->rec, (unsigned long)e->c, g_th.ok_node ? "ok" : "off",
			      (unsigned long)mi);
		return;
	}
	char line[1500];
	size_t o = (size_t)snprintf(line, sizeof line, "BEASTS t=%.3f nodes of the %s '%s' Combat=%#lx hobj=%#lx (root %.1f,%.1f,%.1f):",
				    t, somvr_bz_kind_name(e->kind), e->rec, (unsigned long)e->c, (unsigned long)e->hobj, root[0],
				    root[1], root[2]);
	for (int p = 0; p < SOMVR_BZP_N; p++)
		for (int j = 0; j < 5 && g_bz_part_names[p][j] && !e->have[p]; j++) {
			uint32_t nd;
			float at[3];
			if (bz_node_id(m, e->hobj, g_bz_part_names[p][j], &nd) && bz_node_pos(m, e->hobj, nd, at) &&
			    dist3(at, root) < 600.0f) {
				e->node[p] = nd, e->have[p] = 1;
				o += (size_t)snprintf(line + o, sizeof line - o, " %s=%s(%+.0f,%+.0f,%+.0f)",
						      p == 0 ? "head" : p == 1 ? "neck" : p == 2 ? "chest" : p == 3 ? "pelvis" : "tail",
						      g_bz_part_names[p][j], at[0] - root[0], at[1] - root[1], at[2] - root[2]);
			}
		}
	for (int j = 0; j < BZN_N; j++) {
		float at[3];
		e->lhave[j] = bz_node_id(m, e->hobj, g_bz_limb_names[j], &e->lnode[j]) && bz_node_pos(m, e->hobj, e->lnode[j], at) &&
			      dist3(at, root) < 800.0f;
	}
	for (size_t j = 0; j < sizeof g_bz_extra_names / sizeof g_bz_extra_names[0] && o < sizeof line - 60; j++) {
		uint32_t nd;
		float at[3];
		if (bz_node_id(m, e->hobj, g_bz_extra_names[j], &nd) && bz_node_pos(m, e->hobj, nd, at))
			o += (size_t)snprintf(line + o, sizeof line - o, " %s(%+.0f,%+.0f,%+.0f)", g_bz_extra_names[j],
					      at[0] - root[0], at[1] - root[1], at[2] - root[2]);
	}
	float q[4] = { 0, 0, 0, 1 };
	peek(e->hobj + OBJ_ROT, q, sizeof q);
	sv_vec3 fw = sv_qrot(sv_qnormalize(sv_quat_of(q)), (sv_vec3){ 0, 0, 1 });
	if (bz_log_ok())
		logf_("%s; facing %+.2f,%+.2f,%+.2f", line, fw.x, fw.y, fw.z);
}

/* a limb node's world place this frame */
static int bz_limb_pos(const struct bz_one *e, uintptr_t m, int j, float out[3])
{
	return e->lhave[j] && m && bz_node_pos(m, e->hobj, e->lnode[j], out);
}

/* SOMVR_BEASTS: the blade's capsules on Combat c's beast this frame (0 = not a beast, or a ghul without its chain: the
 * orc's upright capsule, which fits a man-high ghul) */
static int bz_capsules(const struct cb_orc *o, somvr_bz_cap out[SOMVR_BZ_CAPS_ALL], double t)
{
	struct bz_one *e = bz_get(o->c, o->hobj, t);
	if (!e)
		return 0;
	if (!e->nodes_st)
		bz_nodes(e, o->pos, t);
	float pos[SOMVR_BZP_N][3] = { { 0 } }, q[4] = { 0, 0, 0, 1 };
	int have[SOMVR_BZP_N] = { 0 };
	uintptr_t m = peek_ptr(g_som.model_pp);
	for (int p = 0; p < SOMVR_BZP_N; p++)
		have[p] = e->have[p] && m && bz_node_pos(m, o->hobj, e->node[p], pos[p]) && dist3(pos[p], o->pos) < 600.0f;
	peek(o->hobj + OBJ_ROT, q, sizeof q);
	sv_vec3 fw = sv_qrot(sv_qnormalize(sv_quat_of(q)), (sv_vec3){ 0, 0, 1 });
	float fwd[3] = { fw.x, fw.y, fw.z };
	int n = somvr_bz_capsules(&g_bz.shape[e->kind], (const float (*)[3])pos, have, o->pos, fwd, out);
	if (n == 1 && out[0].part == -2 && e->kind == SOMVR_BZ_GHUL)
		return 0;
	static const int limb[SOMVR_BZL_N][2] = { { BZN_R_LEGU, BZN_R_LEGL }, { BZN_L_LEGU, BZN_L_LEGL }, { BZN_R_LEGL, BZN_R_FOOT },
						 { BZN_L_LEGL, BZN_L_FOOT }, { BZN_R_ARML, BZN_R_HAND }, { BZN_L_ARML, BZN_L_HAND } };
	for (int l = 0; l < SOMVR_BZL_N; l++) {
		float a[3], b[3], r = somvr_bz_limb_r(e->kind, l);
		if (r > 0 && bz_limb_pos(e, m, limb[l][0], a) && bz_limb_pos(e, m, limb[l][1], b))
			n = somvr_bz_limb(n, out, a, b, r, l);
	}
	if (n == 1 && out[0].part == -2)
		g_bz.fb_frames++;
	else if (n)
		g_bz.chain_frames++;
	return n;
}

/* combat_frame's sweep against Combat o's target: the beast's capsules (else the orc's); 1 = contact (*ct) */
static int bz_sweep(const struct cb_orc *o, const somvr_blade *prev, const somvr_blade *b, sc_contact *ct, double t)
{
	somvr_bz_cap cs[SOMVR_BZ_CAPS_ALL];
	int n = bz_capsules(o, cs, t);
	if (!n) {
		float c0[3], c1[3];
		cb_capsule(o, c0, c1);
		return sc_sweep_capsule(prev, b, g_cb.cfg.blade_r, c0, c1, g_cb.orc_r, g_cb.cfg.steps, ct);
	}
	g_bz.sweeps++;
	int hit = 0;
	for (int k = 0; k < n; k++) {
		sc_contact c;
		if (sc_sweep_capsule(prev, b, g_cb.cfg.blade_r, cs[k].c0, cs[k].c1, cs[k].r, g_cb.cfg.steps, &c) &&
		    (!hit || c.u < ct->u))
			*ct = c, hit = 1;
	}
	g_bz.contacts += hit;
	return hit;
}

/* THREAT's weapon point (th_point): the node name for this attacker (NULL = R_Hand, as an orc's); retry = 1 after
 * R_Hand was not found */
static const char *bz_hand_name(uintptr_t c, uintptr_t hobj, int retry)
{
	struct bz_one *e = bz_get(c, hobj, cb_now());
	if (!e)
		return NULL;
	return e->kind == SOMVR_BZ_CARAGOR || retry ? "Head" : NULL;
}

/* SOMVR_DEFENCE_JUDGE=contact (df_weapons): a beast's blow as "weapons" when it holds none - a caragor's jaws, a graug's
 * fists (somvr_bz_blows); the count (0..2) */
static int bz_weapons(uintptr_t c, float ob[2][3], float ot[2][3])
{
	uintptr_t hobj = c ? peek_ptr(c + C_HOBJ) : 0;
	struct bz_one *e = bz_get(c, hobj, cb_now());
	uintptr_t m = peek_ptr(g_som.model_pp);
	if (!e || !m)
		return 0;
	float root[3];
	if (!e->nodes_st && peek(hobj + OBJ_POS, root, sizeof root))
		bz_nodes(e, root, cb_now());
	float hd[3] = { 0 }, nk[3] = { 0 }, arm[2][3] = { { 0 } }, hand[2][3] = { { 0 } };
	int jaws = e->have[SOMVR_BZP_HEAD] && e->have[SOMVR_BZP_NECK] && bz_node_pos(m, hobj, e->node[SOMVR_BZP_HEAD], hd) &&
		   bz_node_pos(m, hobj, e->node[SOMVR_BZP_NECK], nk);
	int fist[2] = { bz_limb_pos(e, m, BZN_R_ARML, arm[0]) && bz_limb_pos(e, m, BZN_R_HAND, hand[0]),
			bz_limb_pos(e, m, BZN_L_ARML, arm[1]) && bz_limb_pos(e, m, BZN_L_HAND, hand[1]) };
	int n = somvr_bz_blows(e->kind, &g_bz.shape[e->kind], hd, nk, jaws, (const float (*)[3])arm, (const float (*)[3])hand,
			       fist, ob, ot);
	g_bz.blow_frames += n > 0;
	return n;
}

/* DEFENCE (build 22): a caragor's bite lands at range - the game's StrikeRange 100..200 cm, half-angle 180; boot
 * 20261029-025723's test guard found the jaws 1.6 m to the side at the impact - so a caragor's (and a weaponless ghul's)
 * blow is judged on its line (from its head to Talion's body: the blade across it, in the way, blocks), not by the
 * blades meeting; a graug's fists keep the contact judge */
static int bz_line_judge(uintptr_t c)
{
	struct bz_one *e = bz_get(c, c ? peek_ptr(c + C_HOBJ) : 0, cb_now());
	return e && (e->kind == SOMVR_BZ_CARAGOR || e->kind == SOMVR_BZ_GHUL);
}

/* SOMVR_COMBAT_HOLD_TALION (tk_pm_own): an enemy's sync on Talion is a beast's (its record 'AI_Warg_...', 'AI_Goblin_...',
 * 'AI_Troll_...'): held through the struggle too, as long as it runs (SOMVR_BZ_HOLD_MAX_S) */
static int bz_hold_beast(uintptr_t rec)
{
	char nm[96];
	return g_bz.on && rec ? somvr_bz_kind(fb_rec_name(rec, nm, sizeof nm)) : 0;
}

static void bz_skills(uintptr_t pc, double t);

/* H4, every combat frame (o, n: combat's orcs this frame; NULL: none read - the sword away) */
static void bz_frame(uintptr_t pl, const float root[3], const struct cb_orc *o, int n, double t)
{
	if (!g_bz.on)
		return;
	float own_root[3];
	uintptr_t pm0 = 0, obj0 = 0;
	if (!pl) /* SOMVR_FALLBACK's moment (a struggle): combat had no player this frame; the lines and the trace go on */
		pl = g_fr.pl, root = NULL;
	if (!pl)
		return;
	if (!root && player_objs(pl, &pm0, &obj0) && peek(obj0 + OBJ_POS, own_root, sizeof own_root) && vec_ok(own_root, 1e7f))
		root = own_root;
	uintptr_t pcomb = peek_ptr(pl + P_COMBAT), pc = pcomb ? peek_ptr(pcomb + PC_SERVER) : 0;
	if (pc && (vptr_of((void *)pc) != g_cbg.vt_combat || !peek_ptr(pc + C_PLAYER)))
		pc = 0;
	if (atomic_exchange(&g_bz.req_skills, 0))
		bz_skills(pc, t);
	uintptr_t mnt = 0;
	if (peek(pl + BZ_P_MOUNT, &mnt, sizeof mnt) && mnt != g_bz.mount) {
		float mp[3] = { 0, 0, 0 };
		if (mnt)
			peek(mnt + OBJ_POS, mp, sizeof mp);
		if (bz_log_ok())
			logf_("BEASTS t=%.3f Talion's mount %#lx -> %#lx%s (at %.0f,%.0f,%.0f)", t, (unsigned long)g_bz.mount,
			      (unsigned long)mnt, mnt ? ": MOUNTED" : ": off", mp[0], mp[1], mp[2]);
		g_bz.mount = mnt;
	}
	struct cb_orc own[CB_ORCS];
	if (!o && pcomb && root)
		n = cb_orcs_r(pcomb, pc, root, BZ_NEAR_CM, own, CB_ORCS), o = own;
	uintptr_t th = pc ? peek_ptr(pc + C_HOBJ) : 0;
	int near_atk = 0, nb_kind = 0;
	const struct cb_orc *nb = NULL;
	for (int i = 0; o && i < n; i++) {
		if (o[i].dist > BZ_NEAR_CM)
			continue;
		struct bz_one *e = bz_get(o[i].c, o[i].hobj, t);
		if (!e)
			continue;
		if (!nb)
			nb = &o[i], nb_kind = e->kind;
		if (!e->nodes_st) {
			g_bz.seen++;
			bz_nodes(e, o[i].pos, t);
		}
		uintptr_t atk = peek_ptr(o[i].c + BZ_C_ATTACK), tgt = peek_ptr(o[i].c + BZ_C_TARGET);
		if (atk && tgt == th && o[i].dist < BZ_TRACE_CM)
			near_atk = 1;
		if (atk != e->atk) {
			char an[96] = "-", sn[96] = "";
			uintptr_t s[2] = { 0, 0 };
			if (atk)
				fb_rec_name(atk, an, sizeof an), g_bz.attacks++, g_bz.at_talion += tgt == th;
			peek(o[i].c + C_SYNC, s, sizeof s);
			if (s[0])
				fb_rec_name(peek_ptr(s[0] + 0x10), sn, sizeof sn);
			float hp = -1;
			uintptr_t ow = peek_ptr(o[i].c + C_OWNER);
			if (ow)
				peek(ow + CH_HEALTH, &hp, sizeof hp);
			/* its bearing off the head's view (THREAT's convention: + right) - headless guards are placed by it */
			sv_vec3 hpw;
			sv_quat hq;
			somvr_rig_head(&g_rig, g_fr.base, g_fr.views.pose, &hpw, &hq);
			float hd[3] = { hpw.x, hpw.y, hpw.z }, hqv[4] = { hq.x, hq.y, hq.z, hq.w }, l[3];
			somvr_th_local(hd, hqv, o[i].pos, l);
			somvr_th_dir dir = somvr_th_direction(l);
			if (bz_log_ok())
				logf_("BEASTS t=%.3f %s '%s' Combat=%#lx attack '%s'%s at %.2f m bearing %+.0f deg (health %.0f%s%s%s)",
				      t, somvr_bz_kind_name(e->kind), e->rec, (unsigned long)o[i].c, an,
				      !atk ? "" : tgt == th ? " at Talion" : tgt ? " at another" : " (no target)", o[i].dist / 100.0f,
				      (double)dir.yaw_deg, (double)hp, s[0] ? ", its sync '" : "", sn, s[0] ? "'" : "");
			e->atk = atk;
		}
	}
	/* Talion's side while a beast is near: his sync slots, his attack, the struggles */
	if (nb && pc) {
		uintptr_t s[2] = { 0, 0 }, a = peek_ptr(pc + BZ_C_ATTACK), st[2] = { 0, 0 };
		peek(pc + C_SYNC, s, sizeof s);
		peek(pcomb + BZ_PC_STRUGGLE, st, sizeof st);
		if (s[0] != g_bz.t_sync[0] || s[1] != g_bz.t_sync[1] || a != g_bz.t_atk || st[0] != g_bz.t_strug[0] ||
		    st[1] != g_bz.t_strug[1]) {
			char an[96] = "-", s0[96] = "", s1[96] = "";
			if (a)
				fb_rec_name(a, an, sizeof an);
			if (s[0])
				fb_rec_name(peek_ptr(s[0] + 0x10), s0, sizeof s0);
			if (s[1])
				fb_rec_name(peek_ptr(s[1] + 0x10), s1, sizeof s1);
			if (bz_log_ok())
				logf_("BEASTS t=%.3f Talion: attack '%s', sync %#lx '%s' / target sync %#lx '%s', struggle %#lx/%#lx "
				      "(the %s at %.2f m)", t, an, (unsigned long)s[0], s0, (unsigned long)s[1], s1,
				      (unsigned long)st[0], (unsigned long)st[1], somvr_bz_kind_name(nb_kind), nb->dist / 100.0f);
			g_bz.t_sync[0] = s[0], g_bz.t_sync[1] = s[1], g_bz.t_atk = a, g_bz.t_strug[0] = st[0], g_bz.t_strug[1] = st[1];
			if (s[0] || s[1] || st[0] || st[1])
				near_atk = 1;
		}
		if (s[0] || s[1] || st[0] || st[1])
			near_atk = 1;
	}
	/* SOMVR_BEASTS_TRACE: the view's per-frame path */
	if (near_atk)
		g_bz.trace_until = t + BZ_TRACE_TAIL_S;
	if (g_bz.trace_max && t < g_bz.trace_until && g_bz.trace_lines < g_bz.trace_max && root) {
		g_bz.trace_lines++;
		logf_("BEASTS view f=%llu t=%.4f base %.1f,%.1f,%.1f yaw %.1f root %.1f,%.1f,%.1f base-root %.1f cm beast %s %.1f m",
		      (unsigned long long)g_fr.frame, t, g_fr.base.pos.x, g_fr.base.pos.y, g_fr.base.pos.z,
		      (double)(g_fr.base.yaw * 57.2958f), root[0], root[1], root[2],
		      (double)hypotf(g_fr.base.pos.x - root[0], g_fr.base.pos.z - root[2]),
		      nb ? somvr_bz_kind_name(nb_kind) : "-",
		      nb ? nb->dist / 100.0f : -1.0f);
	}
}

/* `beasts skills [SUBSTR]` (game thread): every Inventory/Skill/Skills record and the owned bit in Talion's inventory -
 * the reads of FUN_02313570 / FUN_023134f0 by peek (no game call) */
static void bz_skills(uintptr_t pc, double t)
{
	uintptr_t cat = g_bz.ok_skill ? peek_ptr(BZ_SKILL_CAT_PP) : 0, base = cat ? peek_ptr(cat + CAT_RECS) : 0;
	uint32_t n = 0;
	uintptr_t ch = pc ? peek_ptr(pc + C_OWNER) : 0, inv = ch ? peek_ptr(ch + BZ_CH_INV) : 0;
	uintptr_t vb = inv ? peek_ptr(inv + BZ_INV_SKILLS) : 0, ve = inv ? peek_ptr(inv + BZ_INV_SKILLS + 8) : 0;
	if (!cat || !base || !peek(cat + CAT_COUNT, &n, sizeof n) || n > 4000 || !inv || !vb || ve < vb) {
		logf_("BEASTS t=%.3f skills: not read (sites %s, category %#lx count %u, Talion's Combat %#lx inventory %#lx bits "
		      "%#lx..%#lx)", t, g_bz.ok_skill ? "ok" : "MISMATCH", (unsigned long)cat, n, (unsigned long)pc,
		      (unsigned long)inv, (unsigned long)vb, (unsigned long)ve);
		return;
	}
	uint32_t words = (uint32_t)((ve - vb) / 4), owned = 0, shown = 0;
	char line[1200];
	size_t o = 0;
	for (uint32_t i = 0; i < n; i++) {
		uintptr_t rec = base + (uintptr_t)i * DBREC_SIZE, rc = 0, rb = 0;
		uint8_t fl = 0;
		uint32_t word, bit, v = 0;
		if (!peek(rec + 1, &fl, 1) || !(rc = peek_ptr(rec + 0x18)))
			continue;
		if (!(fl & 2)) {
			rb = peek_ptr(rc + 0x48);
			uintptr_t ix = (rec - rb) >> 5;
			word = (uint32_t)(ix >> 5 & 0x7ffffff), bit = (uint32_t)(ix & 31);
		} else {
			rb = peek_ptr(rc + 0x38);
			uint32_t u = (uint32_t)((int32_t)((rec - rb) >> 4) * -0x55555555);
			word = u >> 5, bit = u & 31;
		}
		int have = word < words && peek(vb + (uintptr_t)word * 4, &v, 4) && (v >> bit & 1);
		char nm[80];
		fb_rec_name(rec, nm, sizeof nm);
		if (g_bz.grant[0] && !strcmp(nm, g_bz.grant) && g_bd.test && word < words) { /* build 23: a test aid */
			uint32_t v1 = v | 1u << bit;
			int ok = have || arm_poke(vb + (uintptr_t)word * 4, &v1, sizeof v1);
			logf_("BEASTS t=%.3f grant: '%s' (word %u bit %u) %s (a test aid: `beasts grant`)", t, nm, word, bit,
			      have ? "owned already" : ok ? "now owned" : "NOT written");
			have = ok;
			g_bz.grant[0] = 0;
		}
		owned += have;
		if (g_bz.skill_sub[0] && !strcasestr(nm, g_bz.skill_sub))
			continue;
		shown++;
		o += (size_t)snprintf(line + o, sizeof line - o, " %s=%d", nm, have);
		if (o > sizeof line - 120) {
			logf_("BEASTS skills:%s", line);
			o = 0;
		}
	}
	if (o)
		logf_("BEASTS skills:%s", line);
	logf_("BEASTS t=%.3f skills: %u records, %u owned, %u shown ('%s'); Talion's inventory %#lx, %u bit words", t, n, owned,
	      shown, g_bz.skill_sub, (unsigned long)inv, words);
}

/* "beasts skills [SUBSTR]|status" (the writer thread: the game thread acts) */
static const char *bz_word(const char *w)
{
	if (!g_bz.on)
		return "beasts (SOMVR_BEASTS is off: ignored)";
	if (!strncmp(w, "skills", 6)) {
		const char *s = w + 6;
		while (*s == ' ')
			s++;
		size_t l = strlen(s);
		l = l < sizeof g_bz.skill_sub ? l : sizeof g_bz.skill_sub - 1;
		memcpy(g_bz.skill_sub, s, l), g_bz.skill_sub[l] = 0;
		atomic_store(&g_bz.req_skills, 1);
		return "beasts skills";
	}
	if (!strncmp(w, "grant ", 6)) { /* build 23 (som-b23-brand): a skill's owned bit set (SOMVR_HANDS_TEST) */
		if (!g_bd.test)
			return "beasts grant (SOMVR_HANDS_TEST is off: ignored)";
		const char *s = w + 6;
		while (*s == ' ')
			s++;
		size_t l = strcspn(s, " \t\r\n");
		if (!l || l >= sizeof g_bz.grant)
			return "beasts grant (NAME: ignored)";
		memcpy(g_bz.grant, s, l), g_bz.grant[l] = 0;
		snprintf(g_bz.skill_sub, sizeof g_bz.skill_sub, "%s", g_bz.grant);
		atomic_store(&g_bz.req_skills, 1);
		return "beasts grant (a test aid: that skill's owned bit set in memory)";
	}
	if (!strcmp(w, "status")) {
		logf_("BEASTS status: seen %llu, attacks %llu (at Talion %llu), sweeps %llu contacts %llu, chain frames %llu, "
		      "facing frames %llu, trace %d/%d", (unsigned long long)g_bz.seen, (unsigned long long)g_bz.attacks,
		      (unsigned long long)g_bz.at_talion, (unsigned long long)g_bz.sweeps, (unsigned long long)g_bz.contacts,
		      (unsigned long long)g_bz.chain_frames, (unsigned long long)g_bz.fb_frames, g_bz.trace_lines, g_bz.trace_max);
		return "beasts status";
	}
	return "beasts (not skills|grant|status: ignored)";
}

static void bz_end_log(void)
{
	if (!g_bz.on)
		return;
	logf_("BEASTS END seen %llu, attacks %llu (at Talion %llu), sweeps %llu contacts %llu, chain frames %llu, facing "
	      "frames %llu, DEFENCE blow frames %llu, trace %d lines; %llu lines suppressed", (unsigned long long)g_bz.seen,
	      (unsigned long long)g_bz.attacks, (unsigned long long)g_bz.at_talion, (unsigned long long)g_bz.sweeps,
	      (unsigned long long)g_bz.contacts, (unsigned long long)g_bz.chain_frames, (unsigned long long)g_bz.fb_frames,
	      (unsigned long long)g_bz.blow_frames, g_bz.trace_lines, (unsigned long long)g_bz.suppressed);
}
