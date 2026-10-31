/* somvr_hla_game.c - build 27 (docs/re-xr-pipeline.md 112, som-b27-hla; included by somvr.c after the modules it reads):
 * the VR menu's avail bits for the Walk toward row (SOMVR_MOVE live) and for the Controls page's lines of every
 * gesture that runs in this session - each bit only where the gesture's switch is on and its module started (the same
 * conditions the module itself acts on), so the page never names a thing that does nothing. */

static uint64_t hla_vm_avail(void)
{
	uint64_t a = 0;
	if (g_inp.on) /* the stick's walk pre-rotation (in_dpsi) runs with the input */
		a |= SOMVR_VMA_WALKDIR;
	if (g_jp.cfg.vault) /* SOMVR_VAULT: A at the gold ring past a 45-120 cm obstacle */
		a |= SOMVR_VMA_VAULT;
	if (g_fa.mode == FA_ON && g_fa.drop) /* SOMVR_ABOVE: the plunge from a ledge (not =log, not SOMVR_ABOVE_DROP=off) */
		a |= SOMVR_VMA_ABOVE;
	if (g_vb.mode == VB_ON) { /* the left hand's verbs: the brand, the rally, a Domination's B and Y */
		if (g_br.mode == BR_ON)
			a |= SOMVR_VMA_BRAND;
		if (g_vb.rally)
			a |= SOMVR_VMA_RALLY;
		if (g_vb.dismiss)
			a |= SOMVR_VMA_DISMISS;
		if (g_vb.command)
			a |= SOMVR_VMA_COMMAND;
	}
	if (g_us.want && g_us.death) /* SOMVR_USE_DEATH: Mission Failed's A Retry / B Abandon */
		a |= SOMVR_VMA_FAILED;
	if (g_us.want && g_us.skip) /* SOMVR_USE_SKIP: B closes a Nemesis result page */
		a |= SOMVR_VMA_NEMESIS;
	if (g_cl.on && g_cl.cfg.down_over) /* SOMVR_CLIMB_DOWN_OVER: on a top, a grip reached down over the edge */
		a |= SOMVR_VMA_DOWN;
	if (g_cl.on && g_cl.cfg.mantle && g_cl.cfg.am_overhang) /* SOMVR_CLIMB_MANTLE_OVERHANG with the mantle */
		a |= SOMVR_VMA_OVERHANG;
	if (g_mt.counter) /* SOMVR_RIDE_COUNTER (with SOMVR_RIDE): a caragor's leap blocked = the game's counter */
		a |= SOMVR_VMA_COUNTER;
	if (g_mt.on && g_mt.dom_hold_s > 0) /* SOMVR_RIDE_DOM_HOLD_MS: "Hold tight!" held by the closed hand */
		a |= SOMVR_VMA_DOMHOLD;
	if (g_ws.want && g_ws.cfg.vault_cm > 0) /* SOMVR_WRAITH_STEP_VAULT_CM: the step aimed at an orc = over it */
		a |= SOMVR_VMA_OVERORC;
	if (g_stg.on) /* SOMVR_STRUGGLE=thrust: the death struggle answered by the hand and a stab */
		a |= SOMVR_VMA_STRUGGLE;
	if (g_cl.on && g_cl.cfg.mantle) /* SOMVR_CLIMB_MANTLE: A at a top's gold ring = onto it */
		a |= SOMVR_VMA_MANTLE;
	if (g_co.gl_mode == 2) /* SOMVR_GLYPH=head: the glyph by the look in the vision */
		a |= SOMVR_VMA_GLYPH;
	if (g_py.shove) /* SOMVR_SHOVE: a shield shoved aside by the left hand */
		a |= SOMVR_VMA_SHOVE;
	if (g_abl.mode == AB_ON && g_abl.on[ABK_FLASH]) /* build 27, SOMVR_ABIL: the wraith flash by the push */
		a |= SOMVR_VMA_FLASH;
	if (g_abl.mode == AB_ON && g_abl.on[ABK_GROUND]) /* build 27, SOMVR_ABIL: the ground finisher by the stab */
		a |= SOMVR_VMA_GROUNDK;
	if (g_abl.mode == AB_ON && g_ab2.on && g_ab2.ignite && g_ab2.whistle) /* build 27c: the Ignite and Whistle lines */
		a |= SOMVR_VMA_ABIL2;
	return a;
}
