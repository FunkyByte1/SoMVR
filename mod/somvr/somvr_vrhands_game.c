/* somvr_vrhands_game.c - our own VR hands, the game side (somvr_vrhands.h; docs/re-xr-pipeline.md 33). NOT a
 * translation unit of its own: somvr.c includes it after somvr_fingers_game.c (it uses g_arm, g_bd, bf_input, the
 * frame g_fr, the log).
 *   SOMVR_VRHANDS=1   the layer draws hands of its own (the MIT WebXR generic hand) on the controllers and the game's
 *                     forearms and hands are not drawn in the eyes meanwhile (SOMVR_HEAD_PARTS=main's body node hidden
 *                     although SOMVR_BODY=hands edited it); the sword, the dagger and the bow stay the game's. Needs
 *                     SOMVR_ARMS=1 (the grips the arms are solved to: our hand sits exactly on the game's hilt) and
 *                     SOMVR_HEAD_PARTS=main. Keep SOMVR_BODY=hands: if the layer cannot draw (an old layer, a failure),
 *                     it says so in somvr_vrhands.layer_state and the game's hands are drawn again.
 *   Per stereo frame: arm_eval (phase 2) keeps the two grips it solved to (after SOMVR_TWOHAND moved the left onto the
 *   hilt), back in XR space, and each hand's five finger curls (somvr_fingers.c's targets and easing on the
 *   controller's grip, trigger and touches; a held weapon or the bow = a fist); H4 publishes them for that frame with
 *   SOMVR_VHF_HIDE when SOMVR_HEAD_PARTS hid the body node in its eye passes (first person: not third person, not held
 *   away by the body moments).
 *   SOMVR_XR_HANDS_DEPTH=world (build 11, somvr_vrhands v2; docs/re-xr-pipeline.md 33.8; the layer reads the same
 *                     switch): the world hides our hands. H1 keeps each eye's near plane: an entry's depth_k = near cm /
 *                     (100 x the player size) = the XR metres of depth 1 (reversed Z, an infinite far plane). H8, right
 *                     after eye e's image: the eye target's depth-stencil (rt+0x200, an engine wrapper whose texture is
 *                     found by its vtable and checked by GetDesc: D32S8 at the 3D size, DEPTH_STENCIL) copied with the
 *                     game's CopyResource into one of two textures of ours (made with its desc), only while the layer
 *                     draws our hands and the frame's entry hides the game's; their VkImages and the frame each holds
 *                     published in somvr_vrhands (dinfo, dframe). Any mismatch: `VRHANDS depth off ...` once, the
 *                     hands stay (unhidden by the world).
 * Lines: `CONFIG vrhands ...`, `VRHANDS ...` (the layer's state changes, a summary every 10 s), `VRHANDS END ...`. */

__attribute__((visibility("default"), used)) somvr_vrhands_shared g_vho __asm__(SOMVR_VRHANDS_SYMBOL);
static struct {
	int want, on;
	/* phase 2 -> H4 */
	int have;              /* arm_eval kept the grips of this frame */
	somvr_vrhands_frame e;
	fg_cfg fc;
	fg_state st[2];
	uint32_t state_seen;   /* the layer's state last logged */
	uint64_t frames, hidden, pub, nogrips;
	double next_log;
} g_vh;

/* SOMVR_XR_HANDS_DEPTH=world (build 11): the eyes' depth */
#define RT_DEPTH 0x200 /* the eye target's depth-stencil wrapper (FUN_0074df10: FUN_0074ecb0's pooled texture, flag 1) */
#define BIND_SR 0x8u   /* D3D11_BIND_SHADER_RESOURCE */
#define BIND_DS 0x40u  /* D3D11_BIND_DEPTH_STENCIL */
static struct {
	int want, state;          /* SOMVR_XR_HANDS_DEPTH: 1 world; state 0 not made, 1 made, -1 off for this run */
	void *tex[2];             /* ours (IdxD3D11Texture2D) */
	d3d_tex_desc desc;        /* the depth surface's, as made */
	uintptr_t wrap, surf, off; /* the last depth surface found: rt+0x200's wrapper, its texture, at wrapper+off */
	somvr_eyes_info pub;
	float k[2];               /* H1: each eye's depth_k this frame */
	uint64_t k_frame[2];
	float last_k;             /* the last frame's (logs) */
	uint64_t copies, skipped, makes, nok, mismatch;
	uint32_t mis_w, mis_h;    /* the last depth size that did not match the eye image (logged once) */
} g_vd;

__attribute__((format(printf, 1, 2))) static void vd_off(const char *fmt, ...)
{
	char b[400];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(b, sizeof b, fmt, ap);
	va_end(ap);
	g_vd.state = -1;
	logf_("VRHANDS depth off for this run: %s (the world does not hide our hands)", b);
}

/* H1 (after H4 put this frame's entry): eye e's near plane (cm) as the renderer gets it, with this frame's player size;
 * once both eyes have theirs, the entry's depth_k (the eyes share one near plane: else none) */
static void vh_eye_near(int e, float near_cm)
{
	if (e < 0 || e > 1 || !g_vh.on || g_vd.want <= 0 || g_vd.state < 0)
		return;
	float s = somvr_rig_scale(&g_rig);
	g_vd.k[e] = near_cm > 0 && s > 0 ? near_cm / (100.0f * s) : 0;
	g_vd.k_frame[e] = g_fr.frame;
	if (g_vd.k_frame[0] != g_fr.frame || g_vd.k_frame[1] != g_fr.frame)
		return;
	float k = g_vd.k[0] > 0 && fabsf(g_vd.k[0] - g_vd.k[1]) < 1e-6f ? g_vd.k[0] : 0;
	somvr_vrhands_set_depth_k(&g_vho, g_fr.frame, k);
	g_vd.nok += k > 0;
	g_vd.last_k = k;
}

/* the depth texture behind the eye target's wrapper (vtable IdxD3D11Texture2D, GetDesc: a 2D DEPTH_STENCIL texture of
 * the 3D surface's size); NULL = none (vd_off said why, or the eye image's desc is not known yet) */
static void *vd_surface(uintptr_t rt, const struct d3d_src *s, d3d_tex_desc *out)
{
	uintptr_t wrap = peek_ptr(rt + RT_DEPTH);
	if (!wrap)
		return vd_off("the eye target %#lx has no depth-stencil at +0x%x", (unsigned long)rt, RT_DEPTH), NULL;
	if (wrap == g_vd.wrap && g_vd.surf && peek_ptr(wrap + g_vd.off) == g_vd.surf && g_vd.desc.w == g_et.desc.w &&
	    g_vd.desc.h == g_et.desc.h) {
		*out = g_vd.desc;
		return (void *)g_vd.surf;
	}
	static const uintptr_t offs[] = { 0x28, 0x30, 0x38, 0x18, 0x20, 0x40, 0x48, 0x50, 0x58 };
	for (size_t i = 0; i < sizeof offs / sizeof offs[0]; i++) {
		uintptr_t t = peek_ptr(wrap + offs[i]);
		if (!t || vptr_of((void *)t) != g_d3d.vt_tex)
			continue;
		d3d_tex_desc d;
		memset(&d, 0, sizeof d);
		s->getdesc((void *)t, &d);
		if (!(d.bind & BIND_DS))
			continue;
		if (d.samples != 1 || d.mips != 1 || d.array != 1)
			return vd_off("the depth-stencil texture %#lx (wrapper %#lx +0x%lx) is %ux%u samples %u mips %u array %u",
				      (unsigned long)t, (unsigned long)wrap, (unsigned long)offs[i], d.w, d.h, d.samples, d.mips,
				      d.array), NULL;
		if (d.w != g_et.desc.w || d.h != g_et.desc.h) { /* a resolution change under way (SOMVR_EYE_SIZE's switch):
								 * no copy this frame, the next frames look again */
			g_vd.mismatch++;
			if (g_vd.mis_w != d.w || g_vd.mis_h != d.h)
				logf_("VRHANDS depth: the depth-stencil is %ux%u, the eye image %ux%u: no copy until they agree", d.w,
				      d.h, g_et.desc.w, g_et.desc.h);
			g_vd.mis_w = d.w, g_vd.mis_h = d.h;
			return NULL;
		}
		g_vd.mis_w = g_vd.mis_h = 0;
		logf_("VRHANDS depth: the eye target %#lx's depth-stencil: wrapper %#lx +0x%lx = texture %#lx %ux%u format %u "
		      "bind %#x", (unsigned long)rt, (unsigned long)wrap, (unsigned long)offs[i], (unsigned long)t, d.w, d.h,
		      d.format, d.bind);
		g_vd.wrap = wrap, g_vd.surf = t, g_vd.off = offs[i];
		*out = d;
		return (void *)t;
	}
	uint64_t q[12];
	memset(q, 0, sizeof q);
	peek(wrap, q, sizeof q);
	return vd_off("no DEPTH_STENCIL texture in the wrapper %#lx (+0..0x58: %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx "
		      "%llx %llx)", (unsigned long)wrap, (unsigned long long)q[0], (unsigned long long)q[1],
		      (unsigned long long)q[2], (unsigned long long)q[3], (unsigned long long)q[4], (unsigned long long)q[5],
		      (unsigned long long)q[6], (unsigned long long)q[7], (unsigned long long)q[8], (unsigned long long)q[9],
		      (unsigned long long)q[10], (unsigned long long)q[11]), NULL;
}

/* our two depth textures with the surface's desc (sampled: SHADER_RESOURCE kept or added); 0 = made */
static int vd_make(void *dev, const d3d_tex_desc *sd)
{
	d3d_create_fn create = (d3d_create_fn)vslot(dev, g_d3d.vt_dev, SLOT_CREATETEX2D, DS_CREATETEX2D);
	if (!create)
		return vd_off("the device's CreateTexture2D slot is not the checked one"), -1;
	for (int e = 0; e < 2; e++) {
		tex_drop(&g_vd.tex[e]);
		atomic_store_explicit(&g_vho.dframe[e], 0, memory_order_release);
	}
	d3d_tex_desc d = *sd;
	d.usage = 0, d.cpu = 0, d.misc = 0;
	if (d.format == 19) /* R32G8X24_TYPELESS: a sampled view is allowed (the engine's own has one) */
		d.bind |= BIND_SR;
	for (int e = 0; e < 2; e++) {
		void *t = NULL;
		int32_t hr = create(dev, &d, NULL, &t);
		if (hr < 0 || !t || vptr_of(t) != g_d3d.vt_tex)
			return vd_off("CreateTexture2D(%ux%u format %u bind %#x) eye %d: hr %#x", d.w, d.h, d.format, d.bind, e,
				      (unsigned)hr), -1;
		g_vd.tex[e] = t;
	}
	g_vd.desc = *sd;
	g_vd.state = 1;
	g_vd.makes++;
	g_vd.pub.w = d.w, g_vd.pub.h = d.h, g_vd.pub.dxgi_format = d.format;
	g_vd.pub.image[0] = g_vd.pub.image[1] = 0;
	g_vd.pub.gen++;
	somvr_vrhands_depth_publish(&g_vho, &g_vd.pub);
	logf_("VRHANDS depth made %llu: 2 textures %ux%u format %u bind %#x tex=%p,%p gen=%u", (unsigned long long)g_vd.makes,
	      d.w, d.h, d.format, d.bind, g_vd.tex[0], g_vd.tex[1], g_vd.pub.gen);
	return 0;
}

/* their VkImages -> somvr_vrhands.dinfo (gen bumps on any change; Feral makes them at the first copy) */
static void vd_publish(void)
{
	somvr_eyes_info in = g_vd.pub;
	for (int e = 0; e < 2; e++)
		in.image[e] = tex_vkimage(g_vd.tex[e]);
	if (!memcmp(&in, &g_vd.pub, sizeof in))
		return;
	in.gen = g_vd.pub.gen + 1;
	g_vd.pub = in;
	somvr_vrhands_depth_publish(&g_vho, &in);
	logf_("VRHANDS depth gen=%u %ux%u dxgi=%u vkimage=%#llx,%#llx", in.gen, in.w, in.h, in.dxgi_format,
	      (unsigned long long)in.image[0], (unsigned long long)in.image[1]);
}

/* H8 (render thread), eye e's image just copied: its depth too, while the layer draws our hands in this frame */
static void vh_depth_eye(int e, uint64_t frame, uintptr_t rt)
{
	if (!g_vh.on || g_vd.want <= 0 || g_vd.state < 0 || e < 0 || e > 1)
		return;
	const somvr_vrhands_frame *fe = &g_vho.f[frame % SOMVR_NREC];
	uint32_t fl = atomic_load_explicit(&fe->seq, memory_order_acquire) == frame ? fe->flags : 0;
	if (atomic_load_explicit(&g_vho.layer_state, memory_order_acquire) != SOMVR_VHL_DRAWING ||
	    !(fl & SOMVR_VHF_HIDE) || atomic_load_explicit(&fe->seq, memory_order_acquire) != frame) {
		g_vd.skipped++; /* no hands of ours in this frame: no copy */
		return;
	}
	struct d3d_src s;
	if (!d3d_source(HOLDER_TEX3D, &s))
		return vd_off("the device, context or 3D surface are not the expected objects");
	d3d_tex_desc sd;
	void *surf = vd_surface(rt, &s, &sd);
	if (!surf)
		return;
	if ((g_vd.state == 0 || memcmp(&sd, &g_vd.desc, sizeof sd)) && vd_make(s.dev, &sd))
		return;
	s.copy(s.ctx, g_vd.tex[e], surf);
	g_vd.copies++;
	atomic_fetch_add_explicit(&g_vho.n_dcopies, 1, memory_order_relaxed);
	vd_publish();
	atomic_store_explicit(&g_vho.dframe[e], frame, memory_order_release);
}

/* the game convention's grip (somvr_hand_game: 100 (x, y, -z) cm, (-x, -y, z, w)) back in XR space */
static void vh_to_xr(const sa_xf *g, somvr_posef *o)
{
	o->p[0] = g->p.x * 0.01f, o->p[1] = g->p.y * 0.01f, o->p[2] = -g->p.z * 0.01f;
	o->q[0] = -g->q.x, o->q[1] = -g->q.y, o->q[2] = g->q.z, o->q[3] = g->q.w;
}

/* bd_body_drawn: the layer draws our hands, so the body node (the game's hands) is hidden in the eyes */
static int vh_hides(void)
{
	return g_vh.on && atomic_load_explicit(&g_vho.layer_state, memory_order_acquire) == SOMVR_VHL_DRAWING;
}

/* arm_eval, after SOMVR_TWOHAND: the grips the arms are solved to (valid[h]) at time T, the fingers' curls */
static void vh_arm(const sa_xf g[2], const int valid[2], int64_t T, float dt)
{
	if (!g_vh.on)
		return;
	somvr_vrhands_frame *e = &g_vh.e;
	memset(e, 0, sizeof *e);
	e->time = T;
	for (int h = 0; h < 2; h++) {
		fg_in in;
		bf_input(h, &in);
		if (in.hold == FG_HOLD_GAME) /* the bow: our hand closes round its grip */
			in.hold = FG_HOLD_FIST;
		fg_step(&g_vh.st[h], &g_vh.fc, &in, dt);
		for (int k = 0; k < 5; k++)
			e->curl[h][k] = g_vh.st[h].c[k];
		if (in.hold == FG_HOLD_FIST)
			e->flags |= h ? SOMVR_VHF_HOLD_R : SOMVR_VHF_HOLD_L;
		if (!valid[h])
			continue;
		vh_to_xr(&g[h], &e->grip[h]);
		e->flags |= h ? SOMVR_VHF_R : SOMVR_VHF_L;
	}
	if (g_bd.two.w > 0)
		e->flags |= SOMVR_VHF_TWO;
	sf_ghost(e); /* SOMVR_HANDS_SURFACE: the real hands where a surface stopped these (v3) */
	bo_vh_arrow(g, valid, e); /* SOMVR_BOW: the arrow in the string hand or on the string (v4) */
	g_vh.have = 1;
}

/* H4, after hp_frame: this frame's entry (live: a first-person stereo frame) */
static void vh_h4(int live)
{
	if (!g_vh.on)
		return;
	g_vh.frames++;
	somvr_vrhands_frame e;
	memset(&e, 0, sizeof e);
	if (g_vh.have)
		e = g_vh.e;
	else
		g_vh.nogrips++;
	g_vh.have = 0;
	if (live && vh_hides() && g_hp_main_hid == g_fr.frame) /* SOMVR_HEAD_PARTS hid the body node: ours are drawn */
		e.flags |= SOMVR_VHF_HIDE, g_vh.hidden++, atomic_fetch_add_explicit(&g_vho.n_hidden, 1, memory_order_relaxed);
	e.depth_k = 0; /* SOMVR_XR_HANDS_DEPTH=world: set by the eyes' H1 (vh_eye_near), which run after this */
	somvr_vrhands_put(&g_vho, g_fr.frame, &e);
	g_vh.pub++;
	uint32_t st = atomic_load_explicit(&g_vho.layer_state, memory_order_acquire);
	double t = mono();
	if (st != g_vh.state_seen) {
		static const char *const sw[] = { "not drawing yet", "drawing ours (the game's hands hidden in the eyes)",
						  "off (the game's hands drawn)" };
		logf_("VRHANDS t=%.3f frame=%llu the layer: %s", t, (unsigned long long)g_fr.frame, st < 3 ? sw[st] : "?");
		g_vh.state_seen = st;
	}
	if (t >= g_vh.next_log) {
		g_vh.next_log = t + 10.0;
		logf_("VRHANDS t=%.3f entries %llu, ours drawn for %llu (the layer: %llu), no grips %llu; flags 0x%x curls L %.2f "
		      "%.2f %.2f %.2f %.2f R %.2f %.2f %.2f %.2f %.2f; depth: k %.4f m, copies %llu, skipped %llu, the layer "
		      "tested %llu", t, (unsigned long long)g_vh.pub,
		      (unsigned long long)g_vh.hidden, (unsigned long long)atomic_load(&g_vho.n_drawn),
		      (unsigned long long)g_vh.nogrips, e.flags, e.curl[0][0], e.curl[0][1], e.curl[0][2], e.curl[0][3],
		      e.curl[0][4], e.curl[1][0], e.curl[1][1], e.curl[1][2], e.curl[1][3], e.curl[1][4], (double)g_vd.last_k,
		      (unsigned long long)g_vd.copies, (unsigned long long)g_vd.skipped,
		      (unsigned long long)atomic_load(&g_vho.n_dused));
	}
}

/* bd_config */
static void vh_config(int *bad)
{
	static const char *const onoff_w[] = { "0", "1" };
	(void)bad;
	g_vh.want = env_word("SOMVR_VRHANDS", onoff_w, 2, 0);
	g_vd.want = somvr_vh_depth_word(getenv("SOMVR_XR_HANDS_DEPTH"));
	if (g_vd.want < 0) {
		logf_("WARN SOMVR_XR_HANDS_DEPTH=%s is not world or off: off", getenv("SOMVR_XR_HANDS_DEPTH"));
		g_vd.want = 0;
	}
	fg_cfg_default(&g_vh.fc);
	g_vh.fc.tau_s = g_bf.cfg.tau_s > 0 ? g_bf.cfg.tau_s : g_vh.fc.tau_s; /* SOMVR_FINGERS_MS */
}

/* bd_start (after arm_start): the object published */
static void vh_start(void)
{
	if (g_vh.want && !g_arm.on)
		logf_("WARN SOMVR_VRHANDS=1 needs SOMVR_ARMS=1 (the grips the arms are solved to): the game's hands stay");
	if (g_vh.want && !(g_hp.slots >> SOMVR_HP_MAIN & 1))
		logf_("WARN SOMVR_VRHANDS=1 without SOMVR_HEAD_PARTS=main: the game's body is not hidden in the eyes, ours are "
		      "drawn over it");
	g_vh.on = g_vh.want && g_arm.on;
	memset(g_vh.st, 0, sizeof g_vh.st);
	g_vh.next_log = mono() + 10.0;
	if (g_vh.on) {
		g_vho.magic = SOMVR_VRHANDS_MAGIC;
		g_vho.version = SOMVR_VRHANDS_VERSION;
		g_vho.size = (uint32_t)sizeof g_vho;
		g_vho.pid = (int32_t)getpid();
		atomic_store_explicit(&g_vho.active, 1, memory_order_release);
	}
	memset(g_vd.tex, 0, sizeof g_vd.tex);
	g_vd.state = 0;
	logf_("CONFIG vrhands=%d (the layer's own hands on the controllers, the game's hidden in the eyes while it draws them; "
	      "fingers eased %.0f ms) depth=%s", g_vh.on, g_vh.fc.tau_s * 1e3,
	      g_vd.want > 0 ? "world (each eye's depth copied at its end for the layer)" : "off");
}

static void vh_end_log(void)
{
	if (!g_vh.want)
		return;
	logf_("VRHANDS END on=%d: entries %llu, ours %llu (the layer drew %llu, its state %u), no grips %llu; depth %s: k in "
	      "%llu entries, copies %llu, skipped %llu, makes %llu, size mismatches %llu, the layer tested %llu frames", g_vh.on,
	      (unsigned long long)g_vh.pub, (unsigned long long)g_vh.hidden, (unsigned long long)atomic_load(&g_vho.n_drawn),
	      atomic_load(&g_vho.layer_state), (unsigned long long)g_vh.nogrips,
	      g_vd.want <= 0 ? "off" : g_vd.state < 0 ? "failed" : "world", (unsigned long long)g_vd.nok,
	      (unsigned long long)g_vd.copies, (unsigned long long)g_vd.skipped, (unsigned long long)g_vd.makes,
	      (unsigned long long)g_vd.mismatch, (unsigned long long)atomic_load(&g_vho.n_dused));
}
