/* somvr_inrec_game.c - b35: the session's input recorded (SOMVR_INPUT_REC) and a recording replayed into a headless game
 * (SOMVR_INPUT_REPLAY), the game side; included by somvr.c last (it uses somvr.c's statics: the frame g_fr, the rig,
 * the input g_inp, the combat's orcs, the slots, the health, the practice area, the log). The format and the pure
 * parts: somvr_inrec.h / somvr_inrec.c; docs/re-xr-pipeline.md 160; handoff/HANDOFF-hla.md "replay".
 *
 *   SOMVR_INPUT_REC=off|on  (default off; the user's builds: on) every stereo frame's head, both controllers (grip, aim,
 *                      velocities, flags, every button / trigger / squeeze / stick and the press counters) and the rig,
 *                      the hits' blade and each landed hit, Talion's and the nearest orcs' state at 4 Hz, the bug marks:
 *                      <SOMVR_LOG>.inrec. The game thread only copies a record into a ring (ir_push); the writer thread
 *                      (the log's, every 250 ms) writes it out. A bug mark gets the last 30 s as marks/<id>/input.inrec.
 *   SOMVR_INPUT_REC_MB=N    (default 200) the file's cap; past it the session's file stops (the marks' copies too)
 *   SOMVR_INPUT_REPLAY=FILE headless tests only (needs SOMVR_HANDS_TEST=1): the word `replay start` loads FILE (a
 *                      recording or a cut of one: scripts/inrec.py cut) and from the next stereo frame on gives the game
 *                      its head (the eyes' poses), both grips and the controls, frame by frame in real time (the source's
 *                      XrTime = its first + the live time since the start, interpolated), each pose moved by the remap
 *                      (somvr_inrec.h) so it keeps its place to the rig; `replay stop`, `replay status`. The end of the
 *                      source ends it (0.5 s held).
 * Lines: `CONFIG input rec ...`, `INREC t=... last 60 s ...`, `INREC mark #N ...`, `INREC END ...`, `REPLAY ...`. */

#define IR_RING (1u << 20)        /* the game -> writer ring (bytes): ~30 s of frames at 90 Hz */
#define IR_IDX 256                /* the writer's (time, offset) index of frame records: one each 0.25 s, 64 s */
#define IR_MARK_S 30.0            /* a bug mark's copy: the last 30 s */
#define IR_STATE_S 0.25           /* STATE records: 4 Hz */
#define IR_REPLAY_TAIL_NS 500000000LL /* the source's last frame held this long, then the replay ends */

static struct {
	int on;
	char path[512];
	uint64_t cap;
	unsigned char *ring;
	_Atomic uint64_t head, tail;
	/* the game thread (the producer) */
	uint64_t frames, states, blades, hits, marks, notes, drops, pushed;
	double next_state, next_log, t_start;
	double cost_us, cost_max_us, w_cost_us, w_cost_max_us;
	uint64_t cost_n, w_cost_n, w_frames, w_drops;
	float last_psi;              /* the last stereo frame's base heading (the replay's kappa) */
	int have_psi;
	struct cb_orc orcs[CB_ORCS]; /* the last combat frame's orcs */
	int norcs;
	double orcs_t;
	unsigned char cfg_rec[1024]; /* the CFG record (combat's sc_cfg), again at the head of each mark's copy */
	size_t cfg_len;
	int blade_on;                /* the last BLADE record had a blade */
	sc_cfg cfg_last;             /* ... as last recorded (a change at run time - a word, the VR menu - is recorded again) */
	uint64_t cfgs;
	/* the writer thread (the consumer; cmu: it or the end) */
	pthread_mutex_t cmu;
	FILE *f;
	uint64_t written, w_written;
	int full, warned;
	struct { double t; uint64_t off; } idx[IR_IDX];
	int idx_n, idx_head;
	double idx_next;
	/* a bug mark: the game thread -> the writer thread */
	_Atomic uint32_t mark_req;
	uint32_t mark_done;
	char mark_dir[SOMVR_SS_DIR_MAX + 48];
	double mark_t;
	uint32_t mark_n;
	uint64_t mark_copies, mark_fails;
} g_ir = { .cmu = PTHREAD_MUTEX_INITIALIZER };

static struct {
	int want;                    /* SOMVR_INPUT_REPLAY (with SOMVR_HANDS_TEST) */
	char path[512];
	pthread_mutex_t mu;          /* the loaded array: the writer thread -> the game thread */
	somvr_ir_frame *pend;
	int pend_n;
	_Atomic uint32_t req;        /* 1 start (pend ready), 2 stop */
	/* the game thread */
	somvr_ir_frame *f;
	int n, on;
	int64_t T0, S0, last_T, end_T;
	float kappa;
	uint8_t live0[2][SOMVR_NBTN], src0[2][SOMVR_NBTN], ofs[2][SOMVR_NBTN], last_out[2][SOMVR_NBTN];
	int have_ofs;
	uint64_t frames, interp, held, hands, snaps, starts, ends;
	double start_t, next_log;
	float max_rot_deg;
	_Atomic uint32_t status_req;
} g_irp = { .mu = PTHREAD_MUTEX_INITIALIZER };

static int ir_seek(FILE *f, uint64_t off)
{
#ifdef _WIN32
	return _fseeki64(f, (long long)off, SEEK_SET);
#else
	return fseeko(f, (off_t)off, SEEK_SET);
#endif
}

/* ---- the ring (one producer: the game thread; one consumer: the writer thread) ------------------------------------- */

static void ir_push(const void *rec, size_t n)
{
	if (!g_ir.on || !g_ir.ring)
		return;
	uint64_t h = atomic_load_explicit(&g_ir.head, memory_order_relaxed),
		 t = atomic_load_explicit(&g_ir.tail, memory_order_acquire);
	if (h - t + n > IR_RING) {
		g_ir.drops++, g_ir.w_drops++;
		return;
	}
	size_t at = (size_t)(h % IR_RING), first = n < IR_RING - at ? n : IR_RING - at;
	memcpy(g_ir.ring + at, rec, first);
	if (first < n)
		memcpy(g_ir.ring, (const char *)rec + first, n - first);
	atomic_store_explicit(&g_ir.head, h + n, memory_order_release);
	g_ir.pushed += n;
}

static void ir_note(double t, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void ir_note(double t, const char *fmt, ...)
{
	if (!g_ir.on)
		return;
	char s[200], rec[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(s, sizeof s, fmt, ap);
	va_end(ap);
	size_t n = somvr_ir_note_make(rec, sizeof rec, t, s);
	if (n)
		ir_push(rec, n), g_ir.notes++;
}

/* ---- the writer thread: out to the file, the index, the marks' copies ----------------------------------------------- */

static void ir_index(const unsigned char *buf, size_t len, uint64_t off0)
{
	somvr_ir_rh rh;
	for (size_t o = 0; somvr_ir_next(buf, len, o, &rh); o += rh.size) {
		if (rh.type != SOMVR_IR_FRAME || rh.size < sizeof(somvr_ir_frame))
			continue;
		double t;
		memcpy(&t, buf + o + offsetof(somvr_ir_frame, t), sizeof t);
		if (t < g_ir.idx_next)
			continue;
		g_ir.idx_next = t + 0.25;
		g_ir.idx[g_ir.idx_head] = (typeof(g_ir.idx[0])){ t, off0 + o };
		g_ir.idx_head = (g_ir.idx_head + 1) % IR_IDX;
		if (g_ir.idx_n < IR_IDX)
			g_ir.idx_n++;
	}
}

/* the mark's copy: the frames from 30 s before it to now, after a header and the CFG record (cmu held, f flushed) */
static void ir_mark_copy(void)
{
	char out[sizeof g_ir.mark_dir + 24];
	snprintf(out, sizeof out, "%s/input.inrec", g_ir.mark_dir);
	uint64_t from = 0;
	double from_t = 0;
	int found = 0;
	for (int k = 0; k < g_ir.idx_n; k++) { /* oldest first */
		int i = (g_ir.idx_head - g_ir.idx_n + k + IR_IDX) % IR_IDX;
		if (g_ir.idx[i].t >= g_ir.mark_t - IR_MARK_S) {
			from = g_ir.idx[i].off, from_t = g_ir.idx[i].t, found = 1;
			break;
		}
	}
	FILE *in = found ? fopen(g_ir.path, "rb") : NULL, *o = in ? fopen(out, "wb") : NULL;
	uint64_t n = 0;
	int ok = in && o && !ir_seek(in, from);
	if (ok) {
		somvr_ir_hdr hd;
		somvr_ir_hdr_init(&hd, g_ir.mark_t, (int64_t)time(NULL), (int32_t)getpid(), 1u);
		ok = fwrite(&hd, sizeof hd, 1, o) == 1 && (!g_ir.cfg_len || fwrite(g_ir.cfg_rec, g_ir.cfg_len, 1, o) == 1);
		static unsigned char b[1 << 16];
		size_t k;
		while (ok && (k = fread(b, 1, sizeof b, in)) > 0)
			ok = fwrite(b, 1, k, o) == k, n += k;
	}
	if (o && fclose(o))
		ok = 0;
	if (in)
		fclose(in);
	ok ? g_ir.mark_copies++ : g_ir.mark_fails++;
	logf_("INREC mark #%u t=%.3f: %s %s (%.1f MB from %.1f s before it%s)", g_ir.mark_n, g_ir.mark_t,
	      ok ? "the last 30 s ->" : "NOT copied to", out, (double)n / 1e6,
	      found ? g_ir.mark_t - from_t : 0.0,
	      !found ? "; no frame recorded in the last 30 s" : g_ir.full ? "; the session's file is full" : "");
}

/* the writer thread every 250 ms (and the end): the ring to the file, a mark's copy */
static void ir_drain(void)
{
	if (!g_ir.on)
		return;
	pthread_mutex_lock(&g_ir.cmu);
	uint64_t t = atomic_load_explicit(&g_ir.tail, memory_order_relaxed),
		 h = atomic_load_explicit(&g_ir.head, memory_order_acquire);
	static unsigned char b[IR_RING];
	size_t n = (size_t)(h - t), at = (size_t)(t % IR_RING), first = n < IR_RING - at ? n : IR_RING - at;
	memcpy(b, g_ir.ring + at, first);
	if (first < n)
		memcpy(b + first, g_ir.ring, n - first);
	atomic_store_explicit(&g_ir.tail, h, memory_order_release);
	if (g_ir.f && n) {
		if (g_ir.written + n > g_ir.cap) {
			if (!g_ir.full)
				logf_("INREC full: %.0f MB written (SOMVR_INPUT_REC_MB): the rest of the session is not recorded",
				      (double)g_ir.written / 1e6);
			g_ir.full = 1;
		} else if (fwrite(b, 1, n, g_ir.f) != n) {
			if (!g_ir.warned++)
				logf_("WARN INREC: %s: the write failed (%s): no more recorded", g_ir.path, strerror(errno));
			fclose(g_ir.f), g_ir.f = NULL;
		} else {
			ir_index(b, n, g_ir.written);
			g_ir.written += n, g_ir.w_written += n;
			fflush(g_ir.f); /* on disk every 250 ms: a crash loses at most that */
		}
	}
	uint32_t mr = atomic_load_explicit(&g_ir.mark_req, memory_order_acquire);
	if (mr != g_ir.mark_done) {
		g_ir.mark_done = mr;
		if (g_ir.f)
			fflush(g_ir.f);
		ir_mark_copy();
	}
	pthread_mutex_unlock(&g_ir.cmu);
}

/* ---- the game thread: the records --------------------------------------------------------------------------------- */

/* combat_frame: this frame's orcs (for the STATE records) */
static void ir_combat_orcs(const struct cb_orc *o, int n)
{
	if (!g_ir.on)
		return;
	n = n < 0 ? 0 : n > CB_ORCS ? CB_ORCS : n;
	memcpy(g_ir.orcs, o, (size_t)n * sizeof *o);
	g_ir.norcs = n, g_ir.orcs_t = cb_now();
}

static int ir_orc_cmp(const void *a, const void *b)
{
	float x = ((const struct cb_orc *)a)->dist, y = ((const struct cb_orc *)b)->dist;
	return x < y ? -1 : x > y;
}

static void ir_state(double t)
{
	somvr_ir_state s;
	memset(&s, 0, sizeof s);
	s.h = (somvr_ir_rh){ SOMVR_IR_STATE, sizeof s };
	s.t = t, s.xr_time = g_fr.views.time, s.ctx = (uint32_t)g_inp.ctx;
	uintptr_t pm = 0, obj = 0;
	float rq[4] = { 0, 0, 0, 1 };
	s.health = s.health_max = NAN;
	if (g_inp.pl && player_objs(g_inp.pl, &pm, &obj) && peek(obj + OBJ_POS, s.root, sizeof s.root) && vec_ok(s.root, 1e7f) &&
	    peek(obj + OBJ_ROT, rq, sizeof rq))
		s.flags |= SOMVR_IRS_BODY, s.yaw = sv_heading(sv_qnormalize(sv_quat_of(rq)));
	if (g_hu.have)
		s.flags |= SOMVR_IRS_HEALTH, s.health = g_hu.hp, s.health_max = g_hu.hp_max;
	s.flags |= (stl_crouched() ? SOMVR_IRS_CROUCH : 0u) |
		   (g_sl.on && g_sl.have_g && g_sl.g.item[0] && !g_sl.g.sheathed ? SOMVR_IRS_SWORD : 0u) |
		   (bo_out() ? SOMVR_IRS_BOW : 0u) | (bo_dagger_out() ? SOMVR_IRS_DAGGER : 0u) |
		   (cl_holds(SOMVR_HAND_L) ? SOMVR_IRS_CLIMB_L : 0u) | (cl_holds(SOMVR_HAND_R) ? SOMVR_IRS_CLIMB_R : 0u) |
		   (vb_grab_held() ? SOMVR_IRS_GRAB : 0u) | (fi_holds() ? SOMVR_IRS_FINISH : 0u) |
		   (g_pr.here ? SOMVR_IRS_PRACTICE : 0u) | (game_paused() ? SOMVR_IRS_PAUSED : 0u) |
		   (vm_open() ? SOMVR_IRS_VRMENU : 0u);
	struct cb_orc o[CB_ORCS];
	int n = t - g_ir.orcs_t < 1.0 ? g_ir.norcs : 0;
	memcpy(o, g_ir.orcs, (size_t)n * sizeof *o);
	qsort(o, (size_t)n, sizeof *o, ir_orc_cmp);
	for (int i = 0; i < n && (int)s.n_orcs < SOMVR_IR_ORCS; i++) {
		somvr_ir_orc *r = &s.orc[s.n_orcs++];
		memcpy(r->pos, o[i].pos, sizeof r->pos);
		r->dist = o[i].dist, r->id = (uint32_t)o[i].hobj;
		float q[4] = { 0, 0, 0, 1 };
		if (o[i].hobj && peek(o[i].hobj + OBJ_ROT, q, sizeof q))
			r->yaw = sv_heading(sv_qnormalize(sv_quat_of(q)));
		r->health = cb_health(o[i].c);
		r->flags = (isfinite(r->health) ? SOMVR_IRO_HEALTH : 0u) | (cb_dead(o[i].c) ? SOMVR_IRO_DEAD : 0u) |
			   (cb_sync(o[i].c) ? SOMVR_IRO_SYNC : 0u);
		int ours = o[i].hobj && o[i].hobj == g_pr.orc_h;
		for (int k = 0; k < g_pr_keep.n && !ours; k++)
			ours = o[i].hobj && o[i].hobj == g_pr_keep.h[k];
		if (ours)
			r->flags |= SOMVR_IRO_PRACTICE;
	}
	ir_push(&s, sizeof s), g_ir.states++;
}

static void ir_log(double t)
{
	if (t < g_ir.next_log)
		return;
	if (g_ir.next_log > 0)
		logf_("INREC t=%.3f last 60 s: %llu frames, %.2f MB written (%.0f MB so far%s), %llu dropped (ring full); the "
		      "game thread's cost %.2f us a frame (max %.1f)", t, (unsigned long long)g_ir.w_frames,
		      (double)g_ir.w_written / 1e6, (double)g_ir.written / 1e6, g_ir.full ? ", FULL" : "",
		      (unsigned long long)g_ir.w_drops, g_ir.w_cost_n ? g_ir.w_cost_us / (double)g_ir.w_cost_n : 0.0,
		      g_ir.w_cost_max_us);
	g_ir.next_log = t + 60, g_ir.w_frames = g_ir.w_drops = g_ir.w_cost_n = 0, g_ir.w_written = 0;
	g_ir.w_cost_us = g_ir.w_cost_max_us = 0;
}

/* in_frame, H4 (a stereo frame), after the grips are located and the test words put (physical controllers, before
 * the left-handed mirror): one FRAME record; a STATE record each 0.25 s */
static void ir_frame(const somvr_hand phys[2], int complete)
{
	g_ir.last_psi = g_fr.base.yaw, g_ir.have_psi = 1;
	if (!g_ir.on)
		return;
	double t0 = mono(), t = cb_now();
	somvr_ir_frame r;
	memset(&r, 0, sizeof r);
	r.h = (somvr_ir_rh){ SOMVR_IR_FRAME, sizeof r };
	r.frame = (uint32_t)g_fr.frame, r.t = t, r.xr_time = g_fr.views.time;
	for (int e = 0; e < 2; e++)
		memcpy(r.eye_p[e], g_fr.views.pose[e].p, sizeof r.eye_p[e]), memcpy(r.eye_q[e], g_fr.views.pose[e].q, sizeof r.eye_q[e]);
	r.rig_p0[0] = g_rig.p0.x, r.rig_p0[1] = g_rig.p0.y, r.rig_p0[2] = g_rig.p0.z, r.rig_yaw0 = g_rig.yaw0;
	r.base[0] = g_fr.base.pos.x, r.base[1] = g_fr.base.pos.y, r.base[2] = g_fr.base.pos.z, r.base_yaw = g_fr.base.yaw;
	r.scale = somvr_rig_scale(&g_rig);
	r.flags = (g_lh ? SOMVR_IRF_LEFTY : 0u) | (g_fr.views.fresh ? SOMVR_IRF_FRESH : 0u) |
		  (g_inp.have_st ? SOMVR_IRF_SNAP : 0u) | (g_irp.on ? SOMVR_IRF_REPLAYED : 0u) |
		  (complete ? SOMVR_IRF_STEREO : 0u);
	somvr_input_state st = g_inp.st; /* the controls as the logic has them (roles) -> physical */
	if (g_lh)
		sc_mirror_state(&st);
	r.snap_time = g_inp.have_st ? st.time : 0;
	r.profile[0] = st.profile[0], r.profile[1] = st.profile[1], r.in_flags = st.flags;
	for (int p = 0; p < 2; p++) {
		float sq = -1;
		if (g_bd.test && (g_bd.t_sq[p] >= 0 || ((p ^ g_lh) == SOMVR_HAND_R && g_bd.t_draw > 0)))
			sq = bd_squeeze(p ^ g_lh, st.hand[p].squeeze), r.flags |= SOMVR_IRF_TESTHANDS;
		if (g_bd.test && g_bd.t_on[p])
			r.flags |= SOMVR_IRF_TESTHANDS;
		somvr_ir_hand_put(&r.hand[p], &phys[p], g_inp.have_st ? &st.hand[p] : NULL, sq);
		if (g_bd.test && g_bf.t_trig[p] >= 0) /* `hands trigger`: the fingers' test trigger (the fist) */
			r.hand[p].trigger = g_bf.t_trig[p], r.hand[p].active |= SOMVR_HACT_INPUT, r.flags |= SOMVR_IRF_TESTHANDS;
	}
	ir_push(&r, sizeof r), g_ir.frames++, g_ir.w_frames++;
	if (t >= g_ir.next_state)
		g_ir.next_state = t + IR_STATE_S, ir_state(t);
	double us = (mono() - t0) * 1e6;
	g_ir.cost_us += us, g_ir.w_cost_us += us, g_ir.cost_n++, g_ir.w_cost_n++;
	g_ir.cost_max_us = fmax(g_ir.cost_max_us, us), g_ir.w_cost_max_us = fmax(g_ir.w_cost_max_us, us);
	ir_log(t);
}

/* combat_frame: the hits' blade as the swing tracker got it (pushed) or not, or none (b NULL: the tracker reset) */
static void ir_blade(const somvr_blade *b, int pushed)
{
	if (!g_ir.on)
		return;
	if (memcmp(&g_cb.cfg, &g_ir.cfg_last, sizeof g_cb.cfg)) { /* combat's rules changed: the new ones before this blade */
		g_ir.cfg_last = g_cb.cfg;
		size_t n = somvr_ir_cfg_make(g_ir.cfg_rec, sizeof g_ir.cfg_rec, SOMVR_IRC_SCCFG, &g_cb.cfg, (uint32_t)sizeof g_cb.cfg);
		if (n)
			g_ir.cfg_len = n, ir_push(g_ir.cfg_rec, n), g_ir.cfgs++;
	}
	int valid = b && b->valid;
	if (!valid && !g_ir.blade_on)
		return; /* no blade: recorded once, at its first frame (the tracker's reset) */
	g_ir.blade_on = valid;
	somvr_ir_blade r;
	memset(&r, 0, sizeof r);
	r.h = (somvr_ir_rh){ SOMVR_IR_BLADE, sizeof r };
	r.frame = (uint32_t)g_fr.frame;
	if (valid) {
		r.t = b->t, r.flags = SOMVR_IRB_VALID | (pushed ? SOMVR_IRB_PUSHED : 0u);
		memcpy(r.base, b->base, sizeof r.base), memcpy(r.tip, b->tip, sizeof r.tip);
		memcpy(r.vel_tip, b->vel_tip, sizeof r.vel_tip), memcpy(r.vel_base, b->vel_base, sizeof r.vel_base);
		memcpy(r.flat_n, b->flat_n, sizeof r.flat_n);
	} else {
		r.t = cb_now();
	}
	ir_push(&r, sizeof r), g_ir.blades++;
}

/* combat_frame: a hit dealt (before cb_hit / the execute's) */
static void ir_hit(const struct cb_orc *o, int tier, float along, float edge_deg, float blow_j, float mps, float k, double t,
		   uint32_t kind)
{
	if (!g_ir.on)
		return;
	somvr_ir_hit r = { { SOMVR_IR_HIT, sizeof r }, (uint32_t)o->hobj, t, tier, along, edge_deg, blow_j, mps, k,
			   (uint32_t)g_fr.frame, kind };
	ir_push(&r, sizeof r), g_ir.hits++;
}

/* ss_mark (game thread): the mark's record and its copy (the writer thread, at its next turn) */
static void ir_mark(const char *dir, const char *name, uint32_t n, double t)
{
	if (!g_ir.on)
		return;
	somvr_ir_mark r;
	memset(&r, 0, sizeof r);
	r.h = (somvr_ir_rh){ SOMVR_IR_MARK, sizeof r };
	r.n = n, r.t = t;
	snprintf(r.name, sizeof r.name, "%s", name);
	ir_push(&r, sizeof r), g_ir.marks++;
	if (atomic_load_explicit(&g_ir.mark_req, memory_order_acquire) != g_ir.mark_done)
		return; /* the last one's copy is still due (2 s apart: cannot happen at 250 ms) */
	snprintf(g_ir.mark_dir, sizeof g_ir.mark_dir, "%s", dir);
	g_ir.mark_t = t, g_ir.mark_n = n;
	atomic_fetch_add_explicit(&g_ir.mark_req, 1, memory_order_release);
}

/* ---- SOMVR_INPUT_REPLAY (headless tests): the source frames into the game's head, grips and controls ---------------- */

/* the writer thread (`replay start`): the file's frames into an array */
static const char *ir_replay_load(void)
{
	static char why[160];
	FILE *in = fopen(g_irp.path, "rb");
	if (!in) {
		snprintf(why, sizeof why, "replay start: %.80s not opened (%s)", g_irp.path, strerror(errno));
		return why;
	}
	somvr_ir_hdr hd;
	somvr_ir_frame *f = NULL;
	int n = 0, cap = 0, bad = 0, nonmono = 0;
	if (fread(&hd, sizeof hd, 1, in) != 1 || !somvr_ir_hdr_ok(&hd) || ir_seek(in, hd.hdr_size))
		bad = 1;
	static unsigned char b[65536];
	somvr_ir_rh rh;
	while (!bad && fread(&rh, sizeof rh, 1, in) == 1) {
		if (rh.size < sizeof rh || rh.size % 8) {
			bad = 2;
			break;
		}
		memcpy(b, &rh, sizeof rh);
		if (fread(b + sizeof rh, rh.size - sizeof rh, 1, in) != 1)
			break; /* a cut-off last record */
		if (rh.type != SOMVR_IR_FRAME || rh.size < sizeof(somvr_ir_frame))
			continue;
		somvr_ir_frame fr;
		memcpy(&fr, b, sizeof fr);
		if (n && fr.xr_time <= f[n - 1].xr_time) {
			nonmono++;
			continue;
		}
		if (n == cap) {
			cap = cap ? 2 * cap : 4096;
			somvr_ir_frame *g = realloc(f, (size_t)cap * sizeof *f);
			if (!g) {
				bad = 3;
				break;
			}
			f = g;
		}
		f[n++] = fr;
	}
	fclose(in);
	if (bad || n < 2) {
		free(f);
		snprintf(why, sizeof why, "replay start: %.80s %s", g_irp.path,
			 bad == 1 ? "is not an input recording (SOMVRIN1, this build's frame size)" : bad == 2 ? "has a bad record" :
			 bad == 3 ? "does not fit in memory" : "has fewer than 2 frames");
		return why;
	}
	pthread_mutex_lock(&g_irp.mu);
	free(g_irp.pend);
	g_irp.pend = f, g_irp.pend_n = n;
	pthread_mutex_unlock(&g_irp.mu);
	atomic_store_explicit(&g_irp.req, 1, memory_order_release);
	snprintf(why, sizeof why, "replay start: %d frames, %.1f s%s (from the next stereo frame)", n,
		 (double)(f[n - 1].xr_time - f[0].xr_time) * 1e-9, nonmono ? " (some out of order skipped)" : "");
	return why;
}

/* the writer thread: "replay start|stop|status" */
static const char *ir_word(const char *w)
{
	if (!g_irp.want)
		return "replay (SOMVR_INPUT_REPLAY with SOMVR_HANDS_TEST=1 only: ignored)";
	if (!strncmp(w, "start", 5))
		return ir_replay_load();
	if (!strncmp(w, "stop", 4)) {
		atomic_store_explicit(&g_irp.req, 2, memory_order_release);
		return "replay stop";
	}
	if (!strncmp(w, "status", 6)) {
		atomic_store(&g_irp.status_req, 1);
		return "replay status";
	}
	return "replay (not start|stop|status: ignored)";
}

static void ir_replay_end(const char *why, double t)
{
	if (!g_irp.on)
		return;
	g_irp.on = 0, g_irp.ends++;
	for (int p = 0; p < 2; p++) /* the press counters go on from where the replay left them */
		for (int k = 0; k < SOMVR_NBTN; k++)
			g_irp.ofs[p][k] = (uint8_t)(g_irp.last_out[p][k] - g_inp.st_raw.hand[p].presses[k]);
	g_irp.have_ofs = 1;
	logf_("REPLAY t=%.3f end (%s) after %.1f s: %llu frames (%llu between two source frames, %llu held), grips %llu, "
	      "controls %llu; the remap's turn at most %.1f deg", t, why, t - g_irp.start_t, (unsigned long long)g_irp.frames,
	      (unsigned long long)g_irp.interp, (unsigned long long)g_irp.held, (unsigned long long)g_irp.hands,
	      (unsigned long long)g_irp.snaps, (double)g_irp.max_rot_deg);
	ir_note(t, "replay end (%s)", why);
}

/* the source at the live XrTime T, through the remap onto the live rig (0 = no replay now) */
static int ir_replay_at(int64_t T, somvr_ir_frame *out, somvr_ir_map *m, uint32_t *fl)
{
	if (!g_irp.on || T <= 0)
		return 0;
	int64_t S = g_irp.S0 + (T - g_irp.T0);
	if (S > g_irp.end_T + IR_REPLAY_TAIL_NS) {
		ir_replay_end("the source ended", cb_now());
		return 0;
	}
	somvr_ir_sample(g_irp.f, g_irp.n, S, out, fl);
	float p0[3] = { g_rig.p0.x, g_rig.p0.y, g_rig.p0.z };
	somvr_ir_map_make(m, out->rig_p0, out->rig_yaw0, p0, g_rig.yaw0, g_irp.kappa);
	somvr_ir_map_frame(m, out);
	float d = fabsf(m->rot) * 57.29578f;
	g_irp.max_rot_deg = fmaxf(g_irp.max_rot_deg, d);
	return 1;
}

/* H4, right after the views are located (a stereo frame): the start / stop asked; the head's eye poses */
static void ir_views(void)
{
	if (!g_irp.want)
		return;
	double t = cb_now();
	uint32_t rq = atomic_exchange_explicit(&g_irp.req, 0, memory_order_acq_rel);
	if (rq == 2)
		ir_replay_end("the word", t);
	if (rq == 1) {
		ir_replay_end("a new start", t);
		pthread_mutex_lock(&g_irp.mu);
		free(g_irp.f);
		g_irp.f = g_irp.pend, g_irp.n = g_irp.pend_n, g_irp.pend = NULL, g_irp.pend_n = 0;
		pthread_mutex_unlock(&g_irp.mu);
		if (g_irp.f && g_irp.n >= 2) {
			g_irp.on = 1, g_irp.starts++;
			g_irp.T0 = g_fr.views.time, g_irp.S0 = g_irp.f[0].xr_time, g_irp.end_T = g_irp.f[g_irp.n - 1].xr_time;
			g_irp.kappa = g_ir.have_psi ? sv_wrap_pi(g_irp.f[0].base_yaw - g_ir.last_psi) : 0;
			g_irp.frames = g_irp.interp = g_irp.held = g_irp.hands = g_irp.snaps = 0, g_irp.max_rot_deg = 0;
			g_irp.start_t = t, g_irp.next_log = t + 1;
			for (int p = 0; p < 2; p++)
				for (int k = 0; k < SOMVR_NBTN; k++)
					g_irp.live0[p][k] = (uint8_t)(g_inp.st_raw.hand[p].presses[k] + (g_irp.have_ofs ? g_irp.ofs[p][k] : 0)),
					g_irp.src0[p][k] = g_irp.f[0].hand[p].presses[k], g_irp.last_out[p][k] = g_irp.live0[p][k];
			const somvr_ir_frame *a = &g_irp.f[0];
			logf_("REPLAY t=%.3f start: %s, %d frames, %.1f s (the source's t=%.3f..%.3f, %s), kappa %.1f deg (the "
			      "source's base heading %.1f, ours %.1f), the source's P0 %.1f %.1f %.1f yaw0 %.1f, ours %.1f %.1f %.1f "
			      "yaw0 %.1f", t, g_irp.path, g_irp.n, (double)(g_irp.end_T - g_irp.S0) * 1e-9, a->t,
			      g_irp.f[g_irp.n - 1].t, a->flags & SOMVR_IRF_LEFTY ? "left-handed" : "right-handed",
			      (double)(g_irp.kappa * 57.29578f), (double)(a->base_yaw * 57.29578f), (double)(g_ir.last_psi * 57.29578f),
			      (double)a->rig_p0[0], (double)a->rig_p0[1], (double)a->rig_p0[2], (double)(a->rig_yaw0 * 57.29578f),
			      (double)g_rig.p0.x, (double)g_rig.p0.y, (double)g_rig.p0.z, (double)(g_rig.yaw0 * 57.29578f));
			ir_note(t, "replay start %d frames kappa %.4f", g_irp.n, (double)g_irp.kappa);
		}
	}
	if (atomic_exchange(&g_irp.status_req, 0))
		logf_("REPLAY t=%.3f status: %s, %llu frames so far, the source at %.3f s of %.3f", t, g_irp.on ? "on" : "off",
		      (unsigned long long)g_irp.frames, g_irp.on ? (double)(g_fr.views.time - g_irp.T0) * 1e-9 : 0.0,
		      g_irp.n ? (double)(g_irp.end_T - g_irp.S0) * 1e-9 : 0.0);
	somvr_ir_frame s;
	somvr_ir_map m;
	uint32_t fl = 0;
	if (!ir_replay_at(g_fr.views.time, &s, &m, &fl))
		return;
	for (int e = 0; e < 2; e++)
		memcpy(g_fr.views.pose[e].p, s.eye_p[e], sizeof s.eye_p[e]), memcpy(g_fr.views.pose[e].q, s.eye_q[e], sizeof s.eye_q[e]);
	g_irp.frames++, g_irp.interp += fl & 1, g_irp.held += (fl >> 1) & 1;
	somvr_ir_replay r;
	memset(&r, 0, sizeof r);
	r.h = (somvr_ir_rh){ SOMVR_IR_REPLAY, sizeof r };
	r.frame = (uint32_t)g_fr.frame, r.src_time = g_irp.S0 + (g_fr.views.time - g_irp.T0), r.src_t0 = g_irp.S0;
	r.rot = m.rot, r.kappa = m.kappa, r.flags = fl;
	memcpy(r.p0_src, m.p0_src, sizeof r.p0_src), memcpy(r.p0_live, m.p0_live, sizeof r.p0_live);
	ir_push(&r, sizeof r);
	if (t >= g_irp.next_log && g_irp.frames > 1) {
		g_irp.next_log = t + 5;
		logf_("REPLAY t=%.3f %.1f s in: %llu frames, the source's t=%.3f; head %.3f %.3f %.3f (xr, m); turn %.1f deg", t,
		      t - g_irp.start_t, (unsigned long long)g_irp.frames, s.t, (double)s.eye_p[0][0], (double)s.eye_p[0][1],
		      (double)s.eye_p[0][2], (double)(m.rot * 57.29578f));
	}
}

/* in_frame's and the arms' locates, after the test words (physical controllers): the source's grips at T */
static void ir_hands(somvr_hand h[2], int64_t T)
{
	somvr_ir_frame s;
	somvr_ir_map m;
	uint32_t fl;
	if (!ir_replay_at(T, &s, &m, &fl))
		return;
	for (int p = 0; p < 2; p++)
		somvr_ir_hand_get(&s.hand[p], T, &h[p], NULL);
	g_irp.hands++;
}

/* in_tick, after the snapshot's read (physical, before the mirror): the source's controls (1 = st holds them now).
 * Not replaying: the press counters after a replay go on from where it left them. */
static int ir_snap(somvr_input_state *st, int got)
{
	if (!g_irp.want)
		return 0;
	if (!g_irp.on) {
		if (got && g_irp.have_ofs)
			for (int p = 0; p < 2; p++)
				for (int k = 0; k < SOMVR_NBTN; k++)
					st->hand[p].presses[k] = (uint8_t)(st->hand[p].presses[k] + g_irp.ofs[p][k]);
		return 0;
	}
	if (!got)
		*st = g_inp.st_raw;
	int64_t T = got && st->time > 0 ? st->time : g_fr.views.time;
	somvr_ir_frame s;
	somvr_ir_map m;
	uint32_t fl;
	if (!ir_replay_at(T, &s, &m, &fl))
		return 0;
	st->profile[0] = s.profile[0], st->profile[1] = s.profile[1];
	for (int p = 0; p < 2; p++) {
		somvr_ir_hand_get(&s.hand[p], T, NULL, &st->hand[p]);
		for (int k = 0; k < SOMVR_NBTN; k++)
			st->hand[p].presses[k] = g_irp.last_out[p][k] =
				somvr_ir_press(g_irp.live0[p][k], g_irp.src0[p][k], s.hand[p].presses[k]);
	}
	g_irp.snaps++;
	return 1;
}

/* ---- config and end ----------------------------------------------------------------------------------------------- */

static void ir_config(int *bad)
{
	static const char *const onoff_w[] = { "off", "on" };
	g_ir.on = env_word("SOMVR_INPUT_REC", onoff_w, 2, 0);
	g_ir.cap = (uint64_t)env_float("SOMVR_INPUT_REC_MB", 200, 1, 4000, bad) * 1000000u;
	const char *lp = getenv("SOMVR_LOG");
	if (g_ir.on && !(lp && lp[0])) {
		logf_("CONFIG input rec: on, but no SOMVR_LOG to put it beside: not recorded"), g_ir.on = 0;
	} else if (g_ir.on) {
		snprintf(g_ir.path, sizeof g_ir.path, "%s.inrec", lp);
		g_ir.ring = malloc(IR_RING);
		g_ir.f = g_ir.ring ? fopen(g_ir.path, "wb") : NULL;
		if (!g_ir.f) {
			*bad = 1, logf_("WARN SOMVR_INPUT_REC: %s not opened (%s): not recorded", g_ir.path, strerror(errno));
			free(g_ir.ring), g_ir.ring = NULL, g_ir.on = 0;
		} else {
			setvbuf(g_ir.f, NULL, _IOFBF, 1 << 18);
			somvr_ir_hdr hd;
			g_ir.t_start = cb_now();
			somvr_ir_hdr_init(&hd, g_ir.t_start, (int64_t)time(NULL), (int32_t)getpid(), 0);
			if (fwrite(&hd, sizeof hd, 1, g_ir.f) == 1)
				g_ir.written = sizeof hd;
			g_ir.cfg_last = g_cb.cfg;
			g_ir.cfg_len = somvr_ir_cfg_make(g_ir.cfg_rec, sizeof g_ir.cfg_rec, SOMVR_IRC_SCCFG, &g_cb.cfg,
							 (uint32_t)sizeof g_cb.cfg);
			if (g_ir.cfg_len)
				ir_push(g_ir.cfg_rec, g_ir.cfg_len), g_ir.cfgs++;
			ir_note(g_ir.t_start, "rec start pid %d", (int)getpid());
			g_ir.next_log = g_ir.t_start + 60;
		}
	}
	if (g_ir.on)
		logf_("CONFIG input rec: on -> %s (%u-byte frames, 4 Hz state, the blade and the hits; at most %.0f MB; a bug "
		      "mark keeps the last 30 s; re-xr-pipeline.md 160)", g_ir.path, (unsigned)sizeof(somvr_ir_frame),
		      (double)g_ir.cap / 1e6);
	const char *rp = getenv("SOMVR_INPUT_REPLAY");
	if (rp && rp[0]) {
		if (!g_bd.test) {
			logf_("CONFIG replay: SOMVR_INPUT_REPLAY needs SOMVR_HANDS_TEST=1 (headless tests only): off");
		} else {
			g_irp.want = 1;
			snprintf(g_irp.path, sizeof g_irp.path, "%s", rp);
			logf_("CONFIG replay: SOMVR_INPUT_REPLAY=%s (the word `replay start` loads it; the head, the grips and the "
			      "controls from the next stereo frame)", g_irp.path);
		}
	}
}

static void ir_end_log(void)
{
	if (g_irp.want)
		ir_replay_end("the game's end", cb_now());
	if (!g_ir.on)
		return;
	ir_drain();
	pthread_mutex_lock(&g_ir.cmu);
	double s = cb_now() - g_ir.t_start;
	if (g_ir.f)
		fclose(g_ir.f), g_ir.f = NULL;
	logf_("INREC END %s: %llu frames, %llu states, %llu blades, %llu hits, %llu marks (%llu copied, %llu failed), %llu "
	      "notes; %.1f MB in %.0f s (%.0f MB an hour%s), %llu records dropped; the game thread's cost %.2f us a frame "
	      "(max %.1f)", g_ir.path, (unsigned long long)g_ir.frames, (unsigned long long)g_ir.states,
	      (unsigned long long)g_ir.blades, (unsigned long long)g_ir.hits, (unsigned long long)g_ir.marks,
	      (unsigned long long)g_ir.mark_copies, (unsigned long long)g_ir.mark_fails, (unsigned long long)g_ir.notes,
	      (double)g_ir.written / 1e6, s, s > 1 ? (double)g_ir.written / 1e6 * 3600 / s : 0.0, g_ir.full ? ", FULL" : "",
	      (unsigned long long)g_ir.drops, g_ir.cost_n ? g_ir.cost_us / (double)g_ir.cost_n : 0.0, g_ir.cost_max_us);
	pthread_mutex_unlock(&g_ir.cmu);
}
