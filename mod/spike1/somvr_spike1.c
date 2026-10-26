/*
 * somvr_spike1.c - Shadow of Mordor VR, spike 1: the flat, off-screen camera experiment
 * (docs/re-view-render.md sections 2, 3 and 7). An LD_PRELOAD library for the native Linux (Feral) build:
 * bin/ShadowOfMordor, non-PIE ELF at 0x400000, GNU build-id 87ec5d6fd94afd47a24d5da7c9b0c2b74b30c6e9.
 *
 * Usage (the game process only; scripts/boot_check.sh --preload SO sets the first three):
 *   LD_PRELOAD=/abs/path/build/somvr_spike1.so
 *   SOMVR_LOG=FILE          required. Unset or empty = the library logs nothing and changes nothing.
 *   SOMVR_TRIGGER=FILE      the offset is on while this file exists (checked every 250 ms); its first word
 *                           "view" or "vp" overrides SOMVR_AT at run time (empty file = SOMVR_AT).
 *   SOMVR_MODE=log          (default) change nothing, only log
 *             =off30        after the trigger: +30 cm along the camera's right axis (quat applied to (1,0,0))
 *             =alt3         after the trigger: +3 cm on odd frames only
 *             =sbs          after the trigger: H4 runs the player view twice per frame (docs/re-stereo.md section 8):
 *                           eye 0 in the left half, eye 1 in the right half, -/+ SOMVR_SBS_CM (default 3.2) along the
 *                           right axis, each with half the horizontal FOV; trigger word "sbs0" = both at the centre.
 *                           The trigger words "sbs" / "sbs0" select sbs at run time in any SOMVR_MODE.
 *                           "sbsB" / "sbsB0" = layout B (docs/re-stereo.md 9.1): the same two calls, -/+ SOMVR_SBS_CM
 *                           or 0, but both eyes get the full rect {0,0,1,1} and the full FOV (eye R ends on screen).
 *   Trigger words (the file's first word, exact): view, vp, sbs, sbs0, sbsB, sbsB0, blur. "blur" = the offset of
 *                           SOMVR_MODE at SOMVR_AT, plus the game's velocity-blur bytes back while it stands (COMFORT).
 *   SOMVR_AT=view           (default) apply the offset to the SomView after FUN_00a5a820 (H1)
 *           =vp             apply it to a copy of the transform handed to BeginPerspectiveViewport (H2)
 * Example: boot_check.sh ... --preload $PWD/build/somvr_spike1.so --game-env SOMVR_MODE=off30, then `trigger`.
 *
 * Hooks (call-site rel32 / vtable slot patches, never prologues; each site is byte-checked first and skipped
 * on any mismatch; the SITE table below is also read by check_sites.py):
 *   H1  call at 0x9ee7d4 in FUN_009ee170 -> FUN_00a5a820(cam, SomView*, sceneParams): post-hook, log/offset
 *   H2  CRendererHandle vtable slot 37 (0x335cb60) -> 0x705500 BeginPerspectiveViewport(near xmm0, far xmm1,
 *       this rdi, xf rsi, fov[2] rdx, rect[4] rcx): log every call; vp mode passes a modified copy of xf
 *   H3  call at 0x72c749 in FUN_0072c400 (render thread) -> FUN_0072bb20(ds, xf, tanHalf[2], centerOffset[2],
 *       pixelRect[4], type; near xmm0, far xmm1): log only
 *   H4  call at 0x9ef51a in FUN_009ef390 -> FUN_009ee170(gm, &rt3D, origin, player, endFlags, first): sbs only
 *   EXPO   call at 0x7d740b in FUN_007d6e20 (render thread) -> FUN_007ea320(state 0x4625840, ctx), the eye-adaptation
 *          step (docs/re-stereo.md 9.1): log only, every call while the trigger stands and 2 s after, else <= 2/s
 *   AUDIO  CSoundMgr vtable slot 23 (0x346dad0) -> 0x8ed8a0 bool(this, pos[3], front[3], top[3], idx), the Wwise
 *          listener (9.3): log only, 1 line/s per idx, every call while the trigger stands
 * Save guard (SOMVR_SAVEDIR) and comfort pokes (SOMVR_COMFORT=1): see their sections below.
 * The game was built with GCC 8.3 (IPA-RA), so a caller may keep values in call-clobbered registers that the
 * original callee does not touch: every hook enters through an asm wrapper that saves and restores all
 * call-clobbered registers except rax.
 *
 * Inert unless /proc/self/exe is named ShadowOfMordor, the cmdline has no --type= (CEF helpers) and the main
 * executable's in-memory build-id note matches. In the game it then removes itself from LD_PRELOAD so the game's
 * children (CEF, FeralLinuxMessage, crash reporter, the steam stub) never load it. Log: the first 300 calls of each hook, then every 300th, plus
 * 300-call bursts for all hooks when H1 first runs and when the trigger is seen; a RATE line every second.
 * Build: ./build.sh   Offline check: ./build.sh --check
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <locale.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#define SOM_BUILDID  "87ec5d6fd94afd47a24d5da7c9b0c2b74b30c6e9"
#define SOM_EXE_NAME "ShadowOfMordor"
#define RENDERER_FRAME_OFF 0x38 /* CRendererHandle::vf_29 BeginFrame (0x703300) starts with add [rdi+0x38],1 */

/* ------------------------------------------------------------------------------------------------------------ */
/* Patch sites                                                                                                    */

enum { K_CALL = 1, K_VSLOT = 2 };
struct site_spec {
	const char *name;
	int kind;
	uintptr_t addr;         /* K_CALL: the E8 byte; K_VSLOT: the 8-byte slot */
	uintptr_t ctx;          /* start of the bytes checked (contains addr) */
	const char *ctx_hex;    /* expected bytes from ctx on */
	uintptr_t target;       /* expected call target / slot value */
	const char *prologue_hex; /* expected first bytes of the target */
};
#define SITE(n, k, a, c, h, t, p) { n, k, a, c, h, t, p }

/* SITES-BEGIN (check_sites.py parses these lines; one SITE per line; order = H1, H2, H3, H4, EXPO, AUDIO) */
static const struct site_spec g_game_sites[6] = {
SITE("H1", K_CALL, 0x9ee7d4, 0x9ee7c1, "48 8d 94 24 b0 00 00 00 48 8d b4 24 80 00 00 00 48 89 ef e8 47 c0 06 00", 0xa5a820, "41 55 49 89 d5 41 54 55 48 89 f5 53 48 89 fb"),
SITE("H2", K_VSLOT, 0x335cb60, 0x335cb20, "00 33 70 00 00 00 00 00 20 4a 70 00 00 00 00 00 c0 33 70 00 00 00 00 00 40 53 70 00 00 00 00 00 d0 34 70 00 00 00 00 00 b0 4e 70 00 00 00 00 00 a0 2a 70 00 00 00 00 00 80 48 70 00 00 00 00 00 00 55 70 00 00 00 00 00", 0x705500, "41 55 66 0f ef db 49 89 cd 41 54 49 89 d4"),
SITE("H3", K_CALL, 0x72c749, 0x72c734, "4c 8d 44 24 40 48 8d 4c 24 38 48 89 ee 48 89 df 48 8d 54 24 30 e8 d2 f3 ff ff", 0x72bb20, "41 57 49 89 d7 41 56 49 89 ce 41 55 45 89 cd"),
SITE("H4", K_CALL, 0x9ef51a, 0x9ef502, "41 b8 02 00 00 00 48 8d 74 24 10 48 c7 44 24 20 00 00 00 00 48 8b 48 f8 e8 51 ec ff ff", 0x9ee170, "41 57 49 89 ff 41 56 49 89 f6 41 55 45 89 cd"),
SITE("EXPO_HDR", K_CALL, 0x7d740b, 0x7d7405, "48 89 de 4c 89 ef e8 10 2f 01 00", 0x7ea320, "83 3d 19 45 e3 03 01 0f 8e 5b 04 00 00 55 48"),
SITE("AUDIO_LISTENER", K_VSLOT, 0x346dad0, 0x346dac0, "70 07 8f 00 00 00 00 00 60 d7 8e 00 00 00 00 00 a0 d8 8e 00 00 00 00 00", 0x8ed8a0, "80 7f 08 00 75 0a 31 c0 c3 0f 1f 80 00 00 00"),
};
/* SITES-END */

/* SITES-BEGIN (save guard, SOMVR_SAVEDIR): WBOnlineService vtable 0x34823a0, slots 219-222 = the game's only callers
 * of the remote-storage accessor FUN_01bc5510 that touch files (FileWrite, GetFileSize, FileRead, FileDelete); no
 * direct calls to these four functions exist and each pointer appears once in the binary (checked 2026-10-18) */
static const struct site_spec g_guard_sites[4] = {
SITE("G219", K_VSLOT, 0x3482a78, 0x3482a78, "70 99 91 00 00 00 00 00", 0x919970, "41 54 41 89 cc 55 48 89 d5 53 48 8b 07 48 89 f3"),
SITE("G220", K_VSLOT, 0x3482a80, 0x3482a80, "10 99 91 00 00 00 00 00", 0x919910, "53 48 8b 07 48 89 f3 48 8b 80 d0 06 00 00 48 3d"),
SITE("G221", K_VSLOT, 0x3482a88, 0x3482a88, "60 9a 91 00 00 00 00 00", 0x919a60, "41 54 41 89 cc 55 48 89 d5 53 48 8b 07 48 89 f3"),
SITE("G222", K_VSLOT, 0x3482a90, 0x3482a90, "b0 98 91 00 00 00 00 00", 0x9198b0, "53 48 8b 07 48 89 f3 48 8b 80 d0 06 00 00 48 3d"),
};
/* SITES-END */

/* ------------------------------------------------------------------------------------------------------------ */
/* Register frame built by the asm wrappers                                                                      */

struct regs {
	uint64_t rax, rcx, rdx, rsi, rdi, r8, r9, r10, r11, ra; /* ra = the game's return address */
	float xmm[16][4];
};
_Static_assert(offsetof(struct regs, ra) == 0x48, "regs.ra");
_Static_assert(offsetof(struct regs, xmm) == 0x50, "regs.xmm");
_Static_assert(sizeof(struct regs) == 0x150, "regs size");

#define WSAVEX(i) "  movdqu %xmm" #i ", 0x50+" #i "*16(%rsp)\n"
#define WLOADX(i) "  movdqu 0x50+" #i "*16(%rsp), %xmm" #i "\n"
#define WRAPPER(name, cfn)                                                                                     \
	__asm__(".text\n.p2align 4\n.globl " name "\n.hidden " name "\n.type " name ",@function\n" name ":\n"   \
		"  push %rbp\n  mov %rsp, %rbp\n  sub $0x160, %rsp\n  and $-16, %rsp\n"                           \
		"  mov %rax, 0x00(%rsp)\n  mov %rcx, 0x08(%rsp)\n  mov %rdx, 0x10(%rsp)\n  mov %rsi, 0x18(%rsp)\n" \
		"  mov %rdi, 0x20(%rsp)\n  mov %r8, 0x28(%rsp)\n  mov %r9, 0x30(%rsp)\n  mov %r10, 0x38(%rsp)\n"  \
		"  mov %r11, 0x40(%rsp)\n  mov 8(%rbp), %rax\n  mov %rax, 0x48(%rsp)\n"                           \
		WSAVEX(0) WSAVEX(1) WSAVEX(2) WSAVEX(3) WSAVEX(4) WSAVEX(5) WSAVEX(6) WSAVEX(7)                \
		WSAVEX(8) WSAVEX(9) WSAVEX(10) WSAVEX(11) WSAVEX(12) WSAVEX(13) WSAVEX(14) WSAVEX(15)          \
		"  mov %rsp, %rdi\n  cld\n  call " cfn "\n"                                                       \
		WLOADX(0) WLOADX(1) WLOADX(2) WLOADX(3) WLOADX(4) WLOADX(5) WLOADX(6) WLOADX(7)                \
		WLOADX(8) WLOADX(9) WLOADX(10) WLOADX(11) WLOADX(12) WLOADX(13) WLOADX(14) WLOADX(15)          \
		"  mov 0x08(%rsp), %rcx\n  mov 0x10(%rsp), %rdx\n  mov 0x18(%rsp), %rsi\n  mov 0x20(%rsp), %rdi\n" \
		"  mov 0x28(%rsp), %r8\n  mov 0x30(%rsp), %r9\n  mov 0x38(%rsp), %r10\n  mov 0x40(%rsp), %r11\n"  \
		"  mov 0x00(%rsp), %rax\n  leave\n  ret\n.size " name ", .-" name "\n")

void somvr_h1_c(struct regs *r);
void somvr_h2_c(struct regs *r);
void somvr_h3_c(struct regs *r);
void somvr_h4_c(struct regs *r);
void somvr_w_h1(void);
void somvr_w_h2(void);
void somvr_w_h3(void);
void somvr_w_h4(void);
void somvr_expo_c(struct regs *r);
void somvr_aud_c(struct regs *r);
void somvr_w_expo(void);
void somvr_w_aud(void);
WRAPPER("somvr_w_h1", "somvr_h1_c");
WRAPPER("somvr_w_h2", "somvr_h2_c");
WRAPPER("somvr_w_h3", "somvr_h3_c");
WRAPPER("somvr_w_h4", "somvr_h4_c");
WRAPPER("somvr_w_expo", "somvr_expo_c");
WRAPPER("somvr_w_aud", "somvr_aud_c");

/* ------------------------------------------------------------------------------------------------------------ */
/* State                                                                                                          */

enum { M_LOG, M_OFF30, M_ALT3, M_SBS };
static const char *const g_mode_names[] = { "log", "off30", "alt3", "sbs" };
enum { AT_VIEW, AT_VP };
enum { HK_H1, HK_H2, HK_H3, HK_H4, HK_EXPO, HK_AUD, HK_N };

struct hook {
	const char *name;
	int installed;
	uintptr_t orig;
	_Atomic uint64_t calls, mods, logged, burst_end;
};

static struct hook g_h[HK_N] = { { .name = "H1" }, { .name = "H2" }, { .name = "H3" }, { .name = "H4" },
				 { .name = "EXPO" }, { .name = "AUDIO" } };
_Static_assert(sizeof g_game_sites / sizeof g_game_sites[0] == HK_N, "one game site per hook");
/* sbs (docs/re-stereo.md section 8): game thread only (H4 sets it around its two calls; H1/H2 read it) */
static int g_eye = -1;
static int g_eye_full, g_eye_zero; /* g_sbs_full / g_sbs_zero latched by H4 for both eyes of one frame */
static float g_sbs_cm = 3.2f;
static _Atomic int g_sbs_zero; /* trigger word "sbs0": both eyes at the centre (the hidden-state check) */
static _Atomic int g_sbs_word; /* trigger word "sbs"/"sbs0"/"sbsB"/"sbsB0": sbs for this trigger, whatever SOMVR_MODE says */
static _Atomic int g_sbs_full; /* trigger word "sbsB"/"sbsB0": layout B, both eyes the full rect and the full FOV */
static _Atomic int g_blur_word; /* trigger word "blur": the game's velocity-blur bytes while it stands (COMFORT=1) */
static _Atomic uint64_t g_trig_off_us; /* mono_us() of the last trigger on -> off (EXPO logs 2 s after it) */
static uintptr_t g_foliage = 0x46a9de0; /* FUN_009ee170 per-call statics 0x46a9de0..0x46a9e0f (selftest: a buffer) */
#define FOLIAGE_LEN 0x30
static uintptr_t g_ra_vp = 0x9ef2bc; /* return address of the player view's vf_37 call (0x9ef2b5); selftest: 0 = any */
static int g_mode = M_LOG, g_at_env = AT_VIEW, g_active, g_comfort;
static _Atomic int g_at = AT_VIEW; /* the trigger file can switch it at run time */
static char g_log_path[1024], g_trig_path[1024];
static _Atomic int g_trig, g_stop;
static _Atomic uintptr_t g_renderer;
static locale_t g_cloc;
static int g_fd = -1;

#define LOGBUF (1u << 20)
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;       /* hooks: held for one memcpy */
static pthread_mutex_t g_flush_mu = PTHREAD_MUTEX_INITIALIZER; /* flushers only (writer thread, exit) */
static char g_buf[2][LOGBUF];
static size_t g_len;
static int g_cur;
static _Atomic uint64_t g_drops;

/* ------------------------------------------------------------------------------------------------------------ */
/* Logging                                                                                                        */

static double mono(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static uint64_t mono_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

/* read n bytes at addr without faulting: a stale or bad game pointer reads as -1 (process_vm_readv on ourselves:
 * EFAULT instead of SIGSEGV; one syscall, so only on lines that are logged) */
static int safe_read(uintptr_t addr, void *out, size_t n)
{
	if (addr < 0x10000 || addr + n < addr)
		return -1;
	struct iovec lo = { out, n }, re = { (void *)addr, n };
	return process_vm_readv(getpid(), &lo, 1, &re, 1, 0) == (ssize_t)n ? 0 : -1;
}

/* at most one caller per period: 1 = this call may log (next = now + period) */
static int rate_ok(_Atomic uint64_t *next, uint64_t now, uint64_t period)
{
	uint64_t nx = atomic_load_explicit(next, memory_order_relaxed);
	return now >= nx && atomic_compare_exchange_strong(next, &nx, now + period);
}

static int my_tid(void)
{
	static __thread int tid;
	if (!tid)
		tid = (int)syscall(SYS_gettid);
	return tid;
}

static void log_raw(const char *s, size_t n)
{
	pthread_mutex_lock(&g_mu);
	if (g_len + n <= LOGBUF) {
		memcpy(g_buf[g_cur] + g_len, s, n);
		g_len += n;
	} else {
		atomic_fetch_add_explicit(&g_drops, 1, memory_order_relaxed);
	}
	pthread_mutex_unlock(&g_mu);
}

__attribute__((format(printf, 1, 2))) static void logf_(const char *fmt, ...)
{
	char line[2048];
	va_list ap;
	locale_t old = g_cloc ? uselocale(g_cloc) : (locale_t)0;
	va_start(ap, fmt);
	int n = vsnprintf(line, sizeof line - 1, fmt, ap);
	va_end(ap);
	if (old)
		uselocale(old);
	if (n <= 0)
		return;
	if ((size_t)n > sizeof line - 2)
		n = (int)sizeof line - 2;
	if (line[n - 1] != '\n')
		line[n++] = '\n';
	log_raw(line, (size_t)n);
}

static void flush_log(void)
{
	if (g_fd < 0)
		return;
	pthread_mutex_lock(&g_flush_mu);
	pthread_mutex_lock(&g_mu);
	int c = g_cur;
	size_t n = g_len;
	g_cur ^= 1;
	g_len = 0;
	pthread_mutex_unlock(&g_mu);
	const char *p = g_buf[c];
	while (n > 0) {
		ssize_t w = write(g_fd, p, n);
		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0)
			break;
		p += w;
		n -= (size_t)w;
	}
	pthread_mutex_unlock(&g_flush_mu);
}

/* "H1 n=.. t=.. tid=.. thr=.." */
static int line_head(char *b, size_t cap, const char *hk, uint64_t n)
{
	char thr[32] = { 0 };
	prctl(PR_GET_NAME, thr, 0, 0, 0);
	for (char *q = thr; *q; q++)
		if (*q == ' ' || *q == '\t')
			*q = '_';
	return snprintf(b, cap, "%s n=%llu t=%.6f tid=%d thr=%s", hk, (unsigned long long)n, mono(), my_tid(), thr);
}

static int should_log(struct hook *h, uint64_t n)
{
	return n <= 300 || n % 300 == 0 || n <= atomic_load_explicit(&h->burst_end, memory_order_relaxed);
}

static void arm_bursts(const char *why)
{
	for (int i = 0; i < HK_N; i++)
		atomic_store(&g_h[i].burst_end, atomic_load(&g_h[i].calls) + 300);
	logf_("BURST t=%.6f why=%s next 300 calls of each hook are logged (H1 at n=%llu, H2 %llu, H3 %llu, H4 %llu)",
	      mono(), why, (unsigned long long)atomic_load(&g_h[0].calls), (unsigned long long)atomic_load(&g_h[1].calls),
	      (unsigned long long)atomic_load(&g_h[2].calls), (unsigned long long)atomic_load(&g_h[3].calls));
}

static uint32_t xf_hash(const float *xf)
{
	const unsigned char *p = (const unsigned char *)xf;
	uint32_t h = 2166136261u;
	for (int i = 0; i < 28; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Offset logic                                                                                                   */

/* the mode in force: a trigger word "sbs"/"sbs0" switches to sbs for as long as it stands */
static int mode_now(void)
{
	return atomic_load_explicit(&g_sbs_word, memory_order_relaxed) ? M_SBS : g_mode;
}

/* camera right axis = quat (x, y, z, w; w last, as FUN_0072b050 reads it) applied to (1, 0, 0) */
static void right_axis(const float q[4], float out[3])
{
	float x = q[0], y = q[1], z = q[2], w = q[3];
	float nn = x * x + y * y + z * z + w * w;
	float s = nn > 1e-12f ? 2.0f / nn : 0.0f;
	out[0] = 1.0f - s * (y * y + z * z);
	out[1] = s * (x * y + w * z);
	out[2] = s * (x * z - w * y);
}

/* frame number for alt3: the renderer's BeginFrame counter once H2 has seen the renderer, else the hook's count */
static uint64_t frame_no(uint64_t own_n, char *src)
{
	uintptr_t r = atomic_load_explicit(&g_renderer, memory_order_relaxed);
	if (r) {
		*src = 'r';
		return *(volatile uint32_t *)(r + RENDERER_FRAME_OFF);
	}
	*src = 'n';
	return own_n;
}

/* distance to add (cm) at this hook point, 0 = none */
static float offset_cm(int at_here, uint64_t frame)
{
	int m = mode_now();
	if (m == M_LOG || atomic_load_explicit(&g_at, memory_order_relaxed) != at_here ||
	    !atomic_load_explicit(&g_trig, memory_order_acquire))
		return 0.0f;
	if (m == M_OFF30)
		return 30.0f;
	if (m == M_SBS)
		return 0.0f; /* sbs offsets the eyes in H1 while H4 runs them */
	return (frame & 1) ? 3.0f : 0.0f;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Comfort pokes (SOMVR_COMFORT=1), every frame from H2: the game's static initialisers set these after our
 * constructor, so a one-time poke would be overwritten */

struct poke {
	const char *name;
	uintptr_t addr;
	unsigned char want, blur; /* blur = the trigger word "blur" puts the game's value back while it stands */
	unsigned char game, have_game; /* the last value seen that was not ours (game thread only) */
};
enum { PK_SHAKE, PK_MB, PK_DOF, PK_VB55, PK_VB58, PK_N };
static struct poke g_pokes[PK_N] = {
	{ "shake_off", 0x46b72d2, 1, 0, 0, 0 },  /* 1 = FUN_00d8b030/FUN_00d8afc0 return no camera FX offset / FOV scale */
	{ "motion_blur", 0x46b6651, 0, 0, 0, 0 }, /* getter FUN_00d292d0, read in FUN_00a5a5a0: 0 = scene params +0x58 cleared */
	{ "dof", 0x46b6652, 0, 0, 0, 0 },         /* getter FUN_00d292c0: 0 = depth of field (+0x114) off */
	/* docs/re-stereo.md 9.2: .bss bytes, 1 from the static initialiser FUN_0056ff70 (0x570af9 / 0x570af2); FUN_009ee170
	 * ANDs sp+0x55 (0x9ee82d, object velocity blur, med) and sp+0x58 (0x9ee7fe) with them right after H1 + SKY_MODS */
	{ "velblur55", 0x46a9300, 0, 1, 0, 0 },
	{ "velblur58", 0x46a9301, 0, 1, 0, 0 },
};

static void comfort_poke(void)
{
	int blur = atomic_load_explicit(&g_blur_word, memory_order_relaxed);
	for (int i = 0; i < PK_N; i++) {
		struct poke *p = &g_pokes[i];
		volatile unsigned char *b = (volatile unsigned char *)p->addr;
		unsigned char was = *b;
		int restore = blur && p->blur;
		if (was != p->want && !restore) /* not ours: the game's (initialiser, menu) */
			p->game = was, p->have_game = 1;
		unsigned char now = restore && p->have_game ? p->game : p->want;
		if (was != now) {
			*b = now;
			logf_("COMFORT %s %#lx was=%u now=%u%s t=%.6f", p->name, (unsigned long)p->addr, was, now,
			      restore ? " (blur: the game's value)" : "", mono());
		}
	}
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Hooks                                                                                                          */

typedef uint64_t (*h1_fn)(void *cam, void *view, void *sp);
typedef uint64_t (*h2_fn)(float nz, float fz, void *self, const float *xf, const float *fov, const float *rect);
typedef uint64_t (*h3_fn)(float nz, float fz, void *ds, const float *xf, float *tan_half, float *center,
			  int *prect, int type);

/* the last H1 camera (the game's view position before any offset) for the AUDIO lines; game thread writes, any reads */
static _Atomic float g_cam_pos[3];
static _Atomic uint64_t g_cam_n;
static _Atomic uintptr_t g_cam_obj;

__attribute__((used)) void somvr_h1_c(struct regs *r)
{
	struct hook *h = &g_h[HK_H1];
	void *cam = (void *)r->rdi, *sp = (void *)r->rdx;
	float *view = (float *)r->rsi; /* SomView: pos[3] quat[4] fovX fovY near far, 0x2c bytes */
	r->rax = ((h1_fn)h->orig)(cam, view, sp);

	uint64_t n = atomic_fetch_add_explicit(&h->calls, 1, memory_order_relaxed) + 1;
	if (n == 1)
		arm_bursts("h1-first");
	char fsrc;
	uint64_t fr = frame_no(n, &fsrc);
	float d = offset_cm(AT_VIEW, fr), in[7], rt[3] = { 0, 0, 0 };
	memcpy(in, view, sizeof in);
	for (int i = 0; i < 3; i++)
		atomic_store_explicit(&g_cam_pos[i], in[i], memory_order_relaxed);
	atomic_store_explicit(&g_cam_obj, (uintptr_t) * (void *const *)((const char *)cam + 0x338), memory_order_relaxed);
	atomic_store_explicit(&g_cam_n, n, memory_order_release);
	if (mode_now() == M_SBS && g_eye >= 0) { /* each eye +-d along the right axis; layout A: half the width */
		d = g_eye_zero ? 0.0f : (g_eye ? g_sbs_cm : -g_sbs_cm);
		if (!g_eye_full)
			view[7] = 2.0f * atanf(0.5f * tanf(0.5f * view[7]));
		atomic_fetch_add_explicit(&h->mods, 1, memory_order_relaxed);
	}
	if (d != 0.0f) {
		right_axis(view + 3, rt);
		view[0] += d * rt[0];
		view[1] += d * rt[1];
		view[2] += d * rt[2];
		if (mode_now() != M_SBS)
			atomic_fetch_add_explicit(&h->mods, 1, memory_order_relaxed);
	}
	if (!should_log(h, n))
		return;
	atomic_fetch_add_explicit(&h->logged, 1, memory_order_relaxed);
	char hd[128];
	line_head(hd, sizeof hd, "H1", n);
	const char *c = (const char *)cam;
	/* the velocity-blur flags as FUN_00a5a5a0 left them; FUN_009ee170 ANDs them with the gate bytes right after */
	int f55 = sp ? *((const unsigned char *)sp + 0x55) : -1, f58 = sp ? *((const unsigned char *)sp + 0x58) : -1;
	logf_("%s eye=%d ra=%#lx cam=%p obj=%p view=%p sp=%p cf=%d cl=%d fr=%llu%c pos=%.9g,%.9g,%.9g quat=%.9g,%.9g,%.9g,%.9g "
	      "fov=%.9g,%.9g nf=%.9g,%.9g xh=%08x mod=%g d=%.6g,%.6g,%.6g out=%.9g,%.9g,%.9g oxh=%08x sp55=%d&%u sp58=%d&%u",
	      hd, g_eye, (unsigned long)r->ra, cam, *(void *const *)(c + 0x338), (void *)view, sp, *(const int *)(c + 0x3e4),
	      *(const int *)(c + 0x3e8), (unsigned long long)fr, fsrc, in[0], in[1], in[2], in[3], in[4], in[5], in[6],
	      view[7], view[8], view[9], view[10], xf_hash(in), d, d * rt[0], d * rt[1], d * rt[2], view[0], view[1],
	      view[2], xf_hash(view), f55, *(volatile unsigned char *)g_pokes[PK_VB55].addr, f58,
	      *(volatile unsigned char *)g_pokes[PK_VB58].addr);
}

__attribute__((used)) void somvr_h2_c(struct regs *r)
{
	struct hook *h = &g_h[HK_H2];
	float nz = r->xmm[0][0], fz = r->xmm[1][0];
	void *self = (void *)r->rdi;
	const float *xf = (const float *)r->rsi, *fov = (const float *)r->rdx, *rect = (const float *)r->rcx;
	uintptr_t expect = 0;
	atomic_compare_exchange_strong(&g_renderer, &expect, (uintptr_t)self);
	if (g_comfort)
		comfort_poke();

	uint64_t n = atomic_fetch_add_explicit(&h->calls, 1, memory_order_relaxed) + 1;
	uint64_t fr = *(volatile uint32_t *)((const char *)self + RENDERER_FRAME_OFF);
	float d = offset_cm(AT_VP, fr), xc[7], rt[3] = { 0, 0, 0 };
	const float *pass = xf;
	if (d != 0.0f) { /* never write through the caller's pointer: pass a modified copy */
		memcpy(xc, xf, sizeof xc);
		right_axis(xc + 3, rt);
		xc[0] += d * rt[0];
		xc[1] += d * rt[1];
		xc[2] += d * rt[2];
		pass = xc;
		atomic_fetch_add_explicit(&h->mods, 1, memory_order_relaxed);
	}
	float rc[4];
	const float *prc = rect;
	if (mode_now() == M_SBS && g_eye >= 0 && (!g_ra_vp || r->ra == g_ra_vp)) { /* the eye's rect; never the caller's memory */
		if (g_eye_full) /* layout B: the whole target for each eye */
			rc[0] = 0.0f, rc[1] = 0.0f, rc[2] = 1.0f, rc[3] = 1.0f;
		else /* layout A: the eye's half */
			rc[0] = g_eye ? 0.5f : 0.0f, rc[1] = 0.0f, rc[2] = g_eye ? 1.0f : 0.5f, rc[3] = 1.0f;
		prc = rc;
		atomic_fetch_add_explicit(&h->mods, 1, memory_order_relaxed);
	}
	r->rax = ((h2_fn)h->orig)(nz, fz, self, pass, fov, prc);
	if (!should_log(h, n))
		return;
	atomic_fetch_add_explicit(&h->logged, 1, memory_order_relaxed);
	char hd[128];
	line_head(hd, sizeof hd, "H2", n);
	logf_("%s eye=%d ra=%#lx this=%p rf=%llu xf=%p fovp=%p rectp=%p near=%.9g far=%.9g pos=%.9g,%.9g,%.9g "
	      "quat=%.9g,%.9g,%.9g,%.9g fov=%.9g,%.9g rect=%.9g,%.9g,%.9g,%.9g xh=%08x ret=%d mod=%g d=%.6g,%.6g,%.6g "
	      "out=%.9g,%.9g,%.9g oxh=%08x",
	      hd, g_eye, (unsigned long)r->ra, self, (unsigned long long)fr, (const void *)xf, (const void *)fov,
	      (const void *)rect, nz, fz, xf[0], xf[1], xf[2], xf[3], xf[4], xf[5], xf[6], fov[0], fov[1], prc[0],
	      prc[1], prc[2], prc[3], xf_hash(xf), (int)(r->rax & 0xff), d, d * rt[0], d * rt[1], d * rt[2],
	      pass[0], pass[1], pass[2], xf_hash(pass));
}

__attribute__((used)) void somvr_h3_c(struct regs *r)
{
	struct hook *h = &g_h[HK_H3];
	float nz = r->xmm[0][0], fz = r->xmm[1][0];
	void *ds = (void *)r->rdi;
	const float *xf = (const float *)r->rsi;
	float *tan_half = (float *)r->rdx, *center = (float *)r->rcx;
	int *prect = (int *)r->r8, type = (int)r->r9;

	uint64_t n = atomic_fetch_add_explicit(&h->calls, 1, memory_order_relaxed) + 1;
	int lg = should_log(h, n);
	float x[7], t[2], co[2];
	int pr[4];
	if (lg) { /* snapshot the inputs before the call */
		memcpy(x, xf, sizeof x);
		memcpy(t, tan_half, sizeof t);
		memcpy(co, center, sizeof co);
		memcpy(pr, prect, sizeof pr);
	}
	r->rax = ((h3_fn)h->orig)(nz, fz, ds, xf, tan_half, center, prect, type);
	if (!lg)
		return;
	atomic_fetch_add_explicit(&h->logged, 1, memory_order_relaxed);
	uintptr_t rend = atomic_load_explicit(&g_renderer, memory_order_relaxed);
	long long rf = rend ? (long long)*(volatile uint32_t *)(rend + RENDERER_FRAME_OFF) : -1;
	char hd[128];
	line_head(hd, sizeof hd, "H3", n);
	logf_("%s ra=%#lx ds=%p xf=%p rf=%lld near=%.9g far=%.9g pos=%.9g,%.9g,%.9g quat=%.9g,%.9g,%.9g,%.9g xh=%08x "
	      "tan=%.9g,%.9g co=%.9g,%.9g prect=%d,%d,%d,%d type=%d ret=%d",
	      hd, (unsigned long)r->ra, ds, (const void *)xf, rf, nz, fz, x[0], x[1], x[2], x[3], x[4], x[5], x[6],
	      xf_hash(x), t[0], t[1], co[0], co[1], pr[0], pr[1], pr[2], pr[3], type, (int)(r->rax & 0xff));
}

typedef uint64_t (*h4_fn)(void *gm, void *rt, const int *origin, void *player, int end_flags, int first);

/* H4: the player loop's call of the per-view body FUN_009ee170 (call 0x9ef51a). sbs + trigger: two calls per frame,
 * eye 0 with the caller's `first`, eye 1 with first = 0; the per-call statics and the cut byte gm+0x20 are restored
 * between them so both eyes start from the same state (docs/re-stereo.md sections 3, 8) */
__attribute__((used)) void somvr_h4_c(struct regs *r)
{
	struct hook *h = &g_h[HK_H4];
	h4_fn f = (h4_fn)h->orig;
	void *gm = (void *)r->rdi, *rt = (void *)r->rsi, *pl = (void *)r->rcx;
	const int *org = (const int *)r->rdx;
	int ef = (int)r->r8, first = (int)(r->r9 & 0xff);
	uint64_t n = atomic_fetch_add_explicit(&h->calls, 1, memory_order_relaxed) + 1;
	if (mode_now() != M_SBS || !atomic_load_explicit(&g_trig, memory_order_acquire)) {
		r->rax = f(gm, rt, org, pl, ef, first);
		return;
	}
	unsigned char snap[FOLIAGE_LEN], cut = *((volatile unsigned char *)gm + 0x20);
	memcpy(snap, (const void *)g_foliage, sizeof snap);
	g_eye_full = atomic_load_explicit(&g_sbs_full, memory_order_relaxed);
	g_eye_zero = atomic_load_explicit(&g_sbs_zero, memory_order_relaxed);
	double t0 = mono();
	g_eye = 0;
	f(gm, rt, org, pl, ef, first);
	double t1 = mono();
	int same = !memcmp(snap, (const void *)g_foliage, sizeof snap);
	unsigned char cut1 = *((volatile unsigned char *)gm + 0x20);
	memcpy((void *)g_foliage, snap, sizeof snap);
	*((volatile unsigned char *)gm + 0x20) = cut;
	g_eye = 1;
	r->rax = f(gm, rt, org, pl, ef, 0);
	double t2 = mono();
	g_eye = -1;
	atomic_fetch_add_explicit(&h->mods, 1, memory_order_relaxed);
	if (!should_log(h, n))
		return;
	atomic_fetch_add_explicit(&h->logged, 1, memory_order_relaxed);
	char hd[128];
	line_head(hd, sizeof hd, "H4", n);
	logf_("%s ra=%#lx gm=%p rt=%p player=%p ef=%d first=%d eyeL_ms=%.3f eyeR_ms=%.3f statics_changed=%d cut=%u->%u "
	      "zero=%d layout=%c",
	      hd, (unsigned long)r->ra, gm, rt, pl, ef, first, (t1 - t0) * 1e3, (t2 - t1) * 1e3, !same, cut, cut1,
	      g_eye_zero, g_eye_full ? 'B' : 'A');
}

/* the trigger stands, or stood until less than 2 s ago */
static int trig_window(uint64_t now)
{
	if (atomic_load_explicit(&g_trig, memory_order_acquire))
		return 1;
	uint64_t off = atomic_load_explicit(&g_trig_off_us, memory_order_relaxed);
	return off && now - off < 2000000u;
}

/* EXPO (docs/re-stereo.md 9.1, 9.5 test 1): the call 0x7d740b -> FUN_007ea320(state = 0x4625840, ctx), the eye
 * adaptation step, once per RenderScene on the render thread. Pass-through. Logged: k_fRealTime (0x4625460), the
 * last step time [0x4625844] before/after (after = realTime: the step stamp), the exposure [0x4625840] before/after,
 * the bound target ds+8 (0x461e848) with its HDR flag byte rt+8 (bit 0x80) and HDR set *(rt+0x50) (guarded reads).
 * Expectation with two views per frame: two calls per rf, the second with realTime == last (step 0), the same set.
 * No locks beyond the log memcpy; the addresses are variables only so that the selftest can point them at buffers. */
static uintptr_t g_a_realtime = 0x4625460, g_a_expo = 0x4625840, g_a_bound = 0x461e848;
static _Atomic uint64_t g_expo_next;
typedef uint64_t (*expo_fn)(void *state, void *ctx);

__attribute__((used)) void somvr_expo_c(struct regs *r)
{
	struct hook *h = &g_h[HK_EXPO];
	void *state = (void *)r->rdi, *ctx = (void *)r->rsi;
	uint64_t n = atomic_fetch_add_explicit(&h->calls, 1, memory_order_relaxed) + 1, now = mono_us();
	int win = trig_window(now);
	if (!win && !rate_ok(&g_expo_next, now, 500000u)) { /* <= 2 lines/s outside the trigger window */
		r->rax = ((expo_fn)h->orig)(state, ctx);
		return;
	}
	const volatile float *ex = (const volatile float *)g_a_expo;
	float real = *(const volatile float *)g_a_realtime, last0 = ex[1], e0 = ex[0];
	uintptr_t rt = *(const volatile uintptr_t *)g_a_bound, set = 0;
	unsigned char fl = 0;
	int rd_fl = rt ? safe_read(rt + 8, &fl, 1) : -1, rd_set = rt ? safe_read(rt + 0x50, &set, sizeof set) : -1;
	r->rax = ((expo_fn)h->orig)(state, ctx);
	float last1 = ex[1], e1 = ex[0];
	uintptr_t rt1 = *(const volatile uintptr_t *)g_a_bound;
	atomic_fetch_add_explicit(&h->logged, 1, memory_order_relaxed);
	uintptr_t rend = atomic_load_explicit(&g_renderer, memory_order_relaxed);
	long long rf = rend ? (long long)*(volatile uint32_t *)(rend + RENDERER_FRAME_OFF) : -1;
	char hd[128], sfl[8], sset[24];
	line_head(hd, sizeof hd, "EXPO", n);
	if (rd_fl == 0)
		snprintf(sfl, sizeof sfl, "%d", (fl & 0x80) != 0);
	else
		snprintf(sfl, sizeof sfl, "%s", rt ? "?" : "-");
	if (rd_set == 0)
		snprintf(sset, sizeof sset, "%#lx", (unsigned long)set);
	else
		snprintf(sset, sizeof sset, "%s", rt ? "?" : "-");
	logf_("%s ra=%#lx state=%p ctx=%p rf=%lld real=%.9g last=%.9g->%.9g step=%.9g expo=%.9g->%.9g rt=%#lx hdr=%s "
	      "set=%s rt_after=%#lx win=%d",
	      hd, (unsigned long)r->ra, state, ctx, rf, real, last0, last1, real - last0, e0, e1, (unsigned long)rt, sfl,
	      sset, (unsigned long)rt1, win);
}

/* AUDIO (docs/re-stereo.md 9.3, 9.5 test 5): CSoundMgr::vf_23, the Wwise listener setter
 * bool(this, const float pos[3], const float front[3], const float top[3], uint idx). Pass-through. Logged with the
 * caller's return address (who sets the listener: camera or Talion?) next to the last H1 camera position and the
 * position at *(cam+0x338)+8. The vectors are read with safe_read: vf_23 itself returns 0 without touching them when
 * the manager is not up (this+8 == 0). 1 line/s per idx (idx >= 8 share one slot), every call while the trigger stands */
#define AUD_SLOTS 9
static _Atomic uint64_t g_aud_next[AUD_SLOTS];
typedef uint64_t (*aud_fn)(void *self, const float *pos, const float *front, const float *top, uint32_t idx);

static int fmt_vec(char *out, size_t cap, uintptr_t p, float v[3])
{
	if (safe_read(p, v, 3 * sizeof(float)) == 0) {
		snprintf(out, cap, "%.9g,%.9g,%.9g", v[0], v[1], v[2]);
		return 1;
	}
	snprintf(out, cap, "?(%#lx)", (unsigned long)p);
	return 0;
}

__attribute__((used)) void somvr_aud_c(struct regs *r)
{
	struct hook *h = &g_h[HK_AUD];
	void *self = (void *)r->rdi;
	const float *pos = (const float *)r->rsi, *front = (const float *)r->rdx, *top = (const float *)r->rcx;
	uint32_t idx = (uint32_t)r->r8;
	uint64_t n = atomic_fetch_add_explicit(&h->calls, 1, memory_order_relaxed) + 1, now = mono_us();
	int lg = atomic_load_explicit(&g_trig, memory_order_acquire) ||
		 rate_ok(&g_aud_next[idx < AUD_SLOTS - 1 ? idx : AUD_SLOTS - 1], now, 1000000u);
	char sp[64] = "", sf[64] = "", st[64] = "", so[64] = "-";
	float p[3] = { 0, 0, 0 }, v[3];
	int pos_ok = 0;
	if (lg) { /* the inputs as the game passed them, before the call */
		pos_ok = fmt_vec(sp, sizeof sp, (uintptr_t)pos, p);
		fmt_vec(sf, sizeof sf, (uintptr_t)front, v);
		fmt_vec(st, sizeof st, (uintptr_t)top, v);
	}
	r->rax = ((aud_fn)h->orig)(self, pos, front, top, idx);
	if (!lg)
		return;
	atomic_fetch_add_explicit(&h->logged, 1, memory_order_relaxed);
	uint64_t cn = atomic_load_explicit(&g_cam_n, memory_order_acquire);
	float c[3], o[3];
	for (int i = 0; i < 3; i++)
		c[i] = atomic_load_explicit(&g_cam_pos[i], memory_order_relaxed);
	uintptr_t obj = atomic_load_explicit(&g_cam_obj, memory_order_relaxed);
	if (obj)
		fmt_vec(so, sizeof so, obj + 8, o);
	float dx = p[0] - c[0], dy = p[1] - c[1], dz = p[2] - c[2];
	char hd[128];
	line_head(hd, sizeof hd, "AUDIO", n);
	logf_("%s ra=%#lx this=%p idx=%u ret=%d pos=%s front=%s top=%s cam=%.9g,%.9g,%.9g cam_n=%llu d_cam=%.6g obj=%#lx "
	      "objpos=%s",
	      hd, (unsigned long)r->ra, self, idx, (int)(r->rax & 0xff), sp, sf, st, c[0], c[1], c[2],
	      (unsigned long long)cn, cn && pos_ok ? sqrtf(dx * dx + dy * dy + dz * dz) : -1.0f, (unsigned long)obj, so);
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Patching                                                                                                       */

static int hexval(int c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* "48 8d ..." -> bytes (no sscanf: keeps the library on old glibc symbol versions) */
static int parse_hex(const char *s, unsigned char *out, int cap)
{
	int n = 0;
	while (*s) {
		if (*s == ' ') {
			s++;
			continue;
		}
		int hi = hexval(s[0]), lo = hi < 0 ? -1 : hexval(s[1]);
		if (n >= cap || lo < 0)
			return -1;
		out[n++] = (unsigned char)(hi * 16 + lo);
		s += 2;
	}
	return n;
}

static const char *parse_ulx(const char *s, unsigned long *v)
{
	unsigned long x = 0;
	int d, any = 0;
	while ((d = hexval(*s)) >= 0) {
		x = x * 16 + (unsigned long)d;
		s++;
		any = 1;
	}
	*v = x;
	return any ? s : NULL;
}

static void hex_of(const unsigned char *p, int n, char *out, size_t cap)
{
	size_t o = 0;
	for (int i = 0; i < n && o + 4 < cap; i++)
		o += (size_t)snprintf(out + o, cap - o, i ? " %02x" : "%02x", p[i]);
	out[o] = 0;
}

/* protection of the page holding addr from /proc/self/maps, -1 = not mapped */
static int page_prot(uintptr_t addr)
{
	FILE *f = fopen("/proc/self/maps", "re");
	if (!f)
		return -1;
	char line[512];
	int prot = -1;
	while (fgets(line, sizeof line, f)) {
		unsigned long lo, hi;
		const char *q = parse_ulx(line, &lo), *perm;
		if (!q || *q != '-' || !(q = parse_ulx(q + 1, &hi)) || *q != ' ' || strlen(q) < 5)
			continue;
		perm = q + 1;
		if (addr >= lo && addr < hi) {
			prot = (perm[0] == 'r' ? PROT_READ : 0) | (perm[1] == 'w' ? PROT_WRITE : 0) |
			       (perm[2] == 'x' ? PROT_EXEC : 0);
			break;
		}
	}
	fclose(f);
	return prot;
}

static int range_readable(uintptr_t a, size_t n)
{
	for (uintptr_t p = a & ~(uintptr_t)4095; p < a + n; p += 4096) {
		int pr = page_prot(p);
		if (pr < 0 || !(pr & PROT_READ))
			return 0;
	}
	return 1;
}

/* write n bytes at dst: each page made writable (keeping its other bits), then its old protection restored */
static int write_code(uintptr_t dst, const void *src, size_t n, char *err, size_t errcap)
{
	uintptr_t p0 = dst & ~(uintptr_t)4095, p1 = (dst + n - 1) & ~(uintptr_t)4095;
	int prot[2];
	prot[0] = page_prot(p0);
	prot[1] = p1 != p0 ? page_prot(p1) : prot[0];
	if (prot[0] < 0 || prot[1] < 0) {
		snprintf(err, errcap, "page not mapped");
		return -1;
	}
	for (int i = 0; i < (p1 != p0 ? 2 : 1); i++)
		if (mprotect((void *)(i ? p1 : p0), 4096, prot[i] | PROT_READ | PROT_WRITE) != 0) {
			snprintf(err, errcap, "mprotect rw: %s", strerror(errno));
			if (i)
				mprotect((void *)p0, 4096, prot[0]);
			return -1;
		}
	memcpy((void *)dst, src, n);
	__builtin___clear_cache((char *)dst, (char *)dst + n);
	int rc = 0;
	for (int i = 0; i < (p1 != p0 ? 2 : 1); i++)
		if (mprotect((void *)(i ? p1 : p0), 4096, prot[i]) != 0) {
			snprintf(err, errcap, "mprotect restore: %s", strerror(errno));
			rc = -1;
		}
	return rc;
}

/* one RX page within +-2 GB of the low code; slots of 16 bytes: jmp [rip+0]; dq target */
static unsigned char *g_stub;
static int g_stub_used;

static int stub_reach(uintptr_t stub, uintptr_t site)
{
	int64_t rel = (int64_t)stub - (int64_t)(site + 5);
	return rel >= INT32_MIN && rel <= INT32_MAX;
}

static unsigned char *stub_page(uintptr_t near_lo, uintptr_t near_hi)
{
	if (g_stub)
		return g_stub;
	static const uintptr_t far_tries[] = { 0x40000000, 0x50000000, 0x60000000, 0x70000000 };
	for (int i = 0; i < 64 + 4; i++) {
		uintptr_t want = i < 64 ? (uintptr_t)0x3f0000 - (uintptr_t)i * 0x10000 : far_tries[i - 64];
		if (want < 0x10000)
			continue;
		void *p = mmap((void *)want, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
			       -1, 0);
		if (p == MAP_FAILED)
			continue;
		if (stub_reach((uintptr_t)p, near_lo) && stub_reach((uintptr_t)p, near_hi)) {
			memset(p, 0xcc, 4096);
			return g_stub = p;
		}
		munmap(p, 4096);
	}
	return NULL;
}

/* verify one site; 0 = ok. why gets the reason or the original bytes */
static int check_site(const struct site_spec *s, char *why, size_t cap)
{
	unsigned char exp[128], pro[32];
	int n = parse_hex(s->ctx_hex, exp, sizeof exp), np = parse_hex(s->prologue_hex, pro, sizeof pro);
	size_t off = s->addr - s->ctx;
	char live[400];
	if (n <= 0 || np <= 0 || s->addr < s->ctx || off + (s->kind == K_CALL ? 5 : 8) > (size_t)n) {
		snprintf(why, cap, "bad site spec");
		return -1;
	}
	if (!range_readable(s->ctx, (size_t)n) || !range_readable(s->target, (size_t)np)) {
		snprintf(why, cap, "site or target not mapped readable");
		return -1;
	}
	if (memcmp((const void *)s->ctx, exp, (size_t)n) != 0) {
		hex_of((const unsigned char *)s->ctx, n, live, sizeof live);
		snprintf(why, cap, "bytes at %#lx differ: live %s", (unsigned long)s->ctx, live);
		return -1;
	}
	if (s->kind == K_CALL) {
		const unsigned char *p = (const unsigned char *)s->addr;
		int32_t rel;
		memcpy(&rel, p + 1, 4);
		uintptr_t tgt = s->addr + 5 + (intptr_t)rel;
		if (p[0] != 0xe8 || tgt != s->target) {
			snprintf(why, cap, "not a call to %#lx (op %02x target %#lx)", (unsigned long)s->target, p[0],
				 (unsigned long)tgt);
			return -1;
		}
	} else {
		uintptr_t v;
		memcpy(&v, (const void *)s->addr, 8);
		if (v != s->target || (s->addr & 7)) {
			snprintf(why, cap, "slot holds %#lx, want %#lx", (unsigned long)v, (unsigned long)s->target);
			return -1;
		}
	}
	if (memcmp((const void *)s->target, pro, (size_t)np) != 0) {
		hex_of((const unsigned char *)s->target, np, live, sizeof live);
		snprintf(why, cap, "target %#lx prologue differs: live %s", (unsigned long)s->target, live);
		return -1;
	}
	hex_of((const unsigned char *)s->addr, s->kind == K_CALL ? 5 : 8, live, sizeof live);
	snprintf(why, cap, "%s", live);
	return 0;
}

static void install_all(const struct site_spec *sites)
{
	void (*const wr[HK_N])(void) = { somvr_w_h1, somvr_w_h2, somvr_w_h3, somvr_w_h4, somvr_w_expo, somvr_w_aud };
	int ok[HK_N];
	char why[HK_N][512];
	uintptr_t lo = UINTPTR_MAX, hi = 0;
	for (int i = 0; i < HK_N; i++) { /* phase 1: verify */
		ok[i] = check_site(&sites[i], why[i], sizeof why[i]) == 0;
		if (ok[i] && sites[i].kind == K_CALL) {
			lo = sites[i].addr < lo ? sites[i].addr : lo;
			hi = sites[i].addr > hi ? sites[i].addr : hi;
		}
	}
	int stub_slot[HK_N];
	for (int i = 0; i < HK_N; i++)
		stub_slot[i] = -1;
	if (hi) { /* phase 2: stubs, then the page goes RX */
		unsigned char *pg = stub_page(lo, hi);
		for (int i = 0; i < HK_N; i++) {
			if (!ok[i] || sites[i].kind != K_CALL)
				continue;
			if (!pg) {
				ok[i] = 0;
				snprintf(why[i], sizeof why[i], "no stub page within +-2 GB");
				continue;
			}
			unsigned char *st = pg + 16 * g_stub_used;
			uintptr_t tgt = (uintptr_t)wr[i];
			static const unsigned char jmp[6] = { 0xff, 0x25, 0, 0, 0, 0 };
			memcpy(st, jmp, 6);
			memcpy(st + 6, &tgt, 8);
			stub_slot[i] = g_stub_used++;
		}
		if (pg && mprotect(pg, 4096, PROT_READ | PROT_EXEC) != 0)
			for (int i = 0; i < HK_N; i++)
				if (stub_slot[i] >= 0) {
					ok[i] = 0;
					snprintf(why[i], sizeof why[i], "stub mprotect rx: %s", strerror(errno));
				}
	}
	for (int i = 0; i < HK_N; i++) { /* phase 3: patch */
		const struct site_spec *s = &sites[i];
		char err[200] = "";
		unsigned char now[8];
		int rc = -1;
		if (ok[i] && s->kind == K_CALL) {
			uintptr_t st = (uintptr_t)(g_stub + 16 * stub_slot[i]);
			int32_t rel = (int32_t)((int64_t)st - (int64_t)(s->addr + 5));
			rc = write_code(s->addr + 1, &rel, 4, err, sizeof err);
		} else if (ok[i]) {
			uintptr_t w = (uintptr_t)wr[i];
			rc = write_code(s->addr, &w, 8, err, sizeof err);
		}
		if (ok[i] && rc == 0) {
			g_h[i].orig = s->target;
			g_h[i].installed = 1;
			char nb[64];
			memcpy(now, (const void *)s->addr, s->kind == K_CALL ? 5 : 8);
			hex_of(now, s->kind == K_CALL ? 5 : 8, nb, sizeof nb);
			logf_("HOOK %s ok %s %#lx orig=[%s] target=%#lx now=[%s] via=%#lx", s->name,
			      s->kind == K_CALL ? "call" : "vslot", (unsigned long)s->addr, why[i], (unsigned long)s->target,
			      nb, s->kind == K_CALL ? (unsigned long)(g_stub + 16 * stub_slot[i]) : (unsigned long)wr[i]);
		} else {
			logf_("HOOK %s SKIPPED %#lx: %s%s", s->name, (unsigned long)s->addr, ok[i] ? "write failed: " : "",
			      ok[i] ? err : why[i]);
		}
	}
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Save guard (SOMVR_SAVEDIR=DIR): the four WBOnlineService file slots are replaced by local file I/O in DIR. None
 * of them calls the original code, so the game never reaches Steam remote storage (and never the real GameData.sav).
 * Signatures from the disassembly of the originals (each forwards its arguments to the storage interface):
 *   219 bool    Write(this, const char *name, const void *data, int32 size)   -> storage slot 0 FileWrite
 *   220 int32   Size(this, const char *name)                                  -> slot 15 GetFileSize (0 = absent)
 *   221 int32   Read(this, const char *name, void *buf, int32 size)           -> slot 1 FileRead (bytes read)
 *   222 bool    Delete(this, const char *name)                                -> slot 6 FileDelete
 * The calls are virtual (indirect), so plain C functions with the standard ABI are safe here (no IPA-RA). */

static char g_save_dir[1024];
static pthread_mutex_t g_save_mu = PTHREAD_MUTEX_INITIALIZER;

/* DIR/name for a plain file name; 0 = ok. The game passes "\GameData.sav" (run 2): leading '\' are dropped, then
 * the name must be non-empty with no '/', no other '\' and no leading '.'. */
static int save_path(const char *name, char *out, size_t cap)
{
	while (name && *name == '\\')
		name++;
	if (!name || !name[0] || name[0] == '.' || strchr(name, '/') || strchr(name, '\\') || strlen(name) > 200)
		return -1;
	return snprintf(out, cap, "%s/%s", g_save_dir, name) < (int)cap ? 0 : -1;
}

static uint64_t sg_write(void *self, const char *name, const void *data, int size)
{
	(void)self;
	char path[1300], tmp[1310];
	int ok = 0;
	pthread_mutex_lock(&g_save_mu);
	if (size >= 0 && (data || size == 0) && save_path(name, path, sizeof path) == 0) {
		snprintf(tmp, sizeof tmp, "%s.tmp", path);
		int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
		if (fd >= 0) {
			const char *p = data;
			size_t left = (size_t)size;
			while (left > 0) {
				ssize_t w = write(fd, p, left);
				if (w < 0 && errno == EINTR)
					continue;
				if (w <= 0)
					break;
				p += w;
				left -= (size_t)w;
			}
			ok = left == 0 && fsync(fd) == 0;
			ok = close(fd) == 0 && ok;
			ok = ok && rename(tmp, path) == 0;
			if (!ok)
				unlink(tmp);
		}
	}
	pthread_mutex_unlock(&g_save_mu);
	logf_("SAVE write name=%s size=%d ok=%d t=%.6f", name ? name : "(null)", size, ok, mono());
	flush_log();
	return (uint64_t)ok;
}

static uint64_t sg_size(void *self, const char *name)
{
	(void)self;
	char path[1300];
	struct stat st;
	int32_t n = 0;
	if (save_path(name, path, sizeof path) == 0 && stat(path, &st) == 0 && S_ISREG(st.st_mode))
		n = st.st_size > INT32_MAX ? 0 : (int32_t)st.st_size;
	logf_("SAVE size name=%s -> %d t=%.6f", name ? name : "(null)", n, mono());
	return (uint64_t)(uint32_t)n;
}

static uint64_t sg_read(void *self, const char *name, void *buf, int size)
{
	(void)self;
	char path[1300];
	int32_t got = 0;
	pthread_mutex_lock(&g_save_mu);
	if (size > 0 && buf && save_path(name, path, sizeof path) == 0) {
		int fd = open(path, O_RDONLY | O_CLOEXEC);
		if (fd >= 0) {
			char *p = buf;
			while (got < size) {
				ssize_t r = read(fd, p + got, (size_t)(size - got));
				if (r < 0 && errno == EINTR)
					continue;
				if (r <= 0)
					break;
				got += (int32_t)r;
			}
			close(fd);
		}
	}
	pthread_mutex_unlock(&g_save_mu);
	logf_("SAVE read name=%s size=%d -> %d t=%.6f", name ? name : "(null)", size, got, mono());
	return (uint64_t)(uint32_t)got;
}

static uint64_t sg_delete(void *self, const char *name)
{
	(void)self;
	char path[1300];
	int ok = 0;
	pthread_mutex_lock(&g_save_mu);
	if (save_path(name, path, sizeof path) == 0)
		ok = unlink(path) == 0;
	pthread_mutex_unlock(&g_save_mu);
	logf_("SAVE delete name=%s ok=%d t=%.6f", name ? name : "(null)", ok, mono());
	return (uint64_t)ok;
}

/* verify all four slots, then patch all four; 0 = the guard is complete. Any failure: -1 (the caller exits). */
static int guard_install(const struct site_spec *sites, const char *dir)
{
	void *const fn[4] = { (void *)sg_write, (void *)sg_size, (void *)sg_read, (void *)sg_delete };
	char why[512];
	struct stat st;
	if (!dir || !dir[0] || snprintf(g_save_dir, sizeof g_save_dir, "%s", dir) >= (int)sizeof g_save_dir ||
	    stat(dir, &st) != 0 || !S_ISDIR(st.st_mode) || access(dir, W_OK) != 0) {
		logf_("GUARD FAIL: SOMVR_SAVEDIR=%s is not a writable directory", dir ? dir : "(null)");
		return -1;
	}
	for (int i = 0; i < 4; i++)
		if (sites[i].kind != K_VSLOT || check_site(&sites[i], why, sizeof why) != 0) {
			logf_("GUARD FAIL: %s %#lx: %s", sites[i].name, (unsigned long)sites[i].addr, why);
			return -1;
		}
	for (int i = 0; i < 4; i++) {
		char err[200] = "";
		uintptr_t w = (uintptr_t)fn[i], now = 0;
		if (write_code(sites[i].addr, &w, 8, err, sizeof err) != 0 || (memcpy(&now, (void *)sites[i].addr, 8), now != w)) {
			logf_("GUARD FAIL: %s write: %s", sites[i].name, err[0] ? err : "slot does not hold the new value");
			return -1;
		}
		logf_("GUARD %s ok vslot %#lx orig=%#lx now=%#lx", sites[i].name, (unsigned long)sites[i].addr,
		      (unsigned long)sites[i].target, (unsigned long)w);
	}
	logf_("GUARD on: save files in %s; the game's remote-storage file calls never reach Steam", g_save_dir);
	return 0;
}

/* SOMVR_SAVEDIR set: the guard must be complete or the process ends here, before the game runs */
static void guard_or_die(const struct site_spec *sites, int log_ok)
{
	const char *dir = getenv("SOMVR_SAVEDIR");
	if (!dir)
		return;
	if (!log_ok || guard_install(sites, dir) != 0) {
		logf_("GUARD: exiting with status 3 before the game runs (SOMVR_SAVEDIR is set, the guard is not complete)");
		flush_log();
		_exit(3);
	}
	flush_log();
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Writer thread: trigger poll (250 ms), RATE line and flush (1 s)                                                */

static void *writer_main(void *arg)
{
	(void)arg;
	prctl(PR_SET_NAME, "somvr-log", 0, 0, 0);
	double last = mono();
	uint64_t prev[HK_N] = { 0 }, prevm[HK_N] = { 0 };
	while (!atomic_load(&g_stop)) {
		struct timespec ts = { 0, 250 * 1000 * 1000 };
		nanosleep(&ts, NULL);
		if (g_trig_path[0]) { /* file present = offset on; absent = off; its first word (exact) picks the variant */
			int on = 0, at = g_at_env, zero = 0, sbsw = 0, full = 0, blur = 0, bad = 0;
			char w[16] = { 0 };
			int fd = open(g_trig_path, O_RDONLY | O_CLOEXEC);
			if (fd >= 0) {
				ssize_t n = read(fd, w, sizeof w - 1);
				close(fd);
				on = 1;
				size_t k = 0;
				while (n > 0 && k < (size_t)n && (w[k] == ' ' || w[k] == '\t' || w[k] == '\n' || w[k] == '\r'))
					k++;
				memmove(w, w + k, sizeof w - k);
				w[strcspn(w, " \t\r\n")] = 0;
				if (!strcmp(w, "vp"))
					at = AT_VP;
				else if (!strcmp(w, "view"))
					at = AT_VIEW;
				else if (!strcmp(w, "blur"))
					blur = 1;
				else if (!strcmp(w, "sbs") || !strcmp(w, "sbs0") || !strcmp(w, "sbsB") || !strcmp(w, "sbsB0")) {
					sbsw = 1;
					full = w[3] == 'B';
					zero = w[strlen(w) - 1] == '0';
				} else if (w[0])
					bad = 1; /* unknown word: the trigger without a variant (SOMVR_MODE at SOMVR_AT) */
			}
			int was_on = atomic_load(&g_trig);
			if (on != was_on || at != atomic_load(&g_at) || zero != atomic_load(&g_sbs_zero) ||
			    sbsw != atomic_load(&g_sbs_word) || full != atomic_load(&g_sbs_full) ||
			    blur != atomic_load(&g_blur_word)) {
				atomic_store(&g_at, at);
				atomic_store(&g_sbs_zero, zero);
				atomic_store(&g_sbs_full, full);
				atomic_store(&g_blur_word, blur);
				atomic_store(&g_sbs_word, sbsw);
				if (was_on && !on)
					atomic_store(&g_trig_off_us, mono_us());
				atomic_store_explicit(&g_trig, on, memory_order_release);
				struct timespec wall;
				clock_gettime(CLOCK_REALTIME, &wall);
				logf_("TRIGGER %s t=%.6f wall=%lld.%03ld path=%s mode=%s at=%s%s%s%s word=%s%s", on ? "on" : "off",
				      mono(), (long long)wall.tv_sec, wall.tv_nsec / 1000000, g_trig_path, g_mode_names[mode_now()],
				      at == AT_VIEW ? "view" : "vp", zero ? " sbs0" : "", full ? " layoutB" : "",
				      blur ? (g_comfort ? " blur" : " blur(no COMFORT: nothing to restore)") : "",
				      w[0] ? w : "-", bad ? " WARN unknown word, ignored" : "");
				if (on)
					arm_bursts("trigger");
			}
		}
		double t = mono();
		if (t - last >= 1.0) {
			uint64_t c[HK_N], m[HK_N];
			for (int i = 0; i < HK_N; i++) {
				c[i] = atomic_load(&g_h[i].calls);
				m[i] = atomic_load(&g_h[i].mods);
			}
			logf_("RATE t=%.6f dt=%.3f H1=%llu H2=%llu H3=%llu H4=%llu EXPO=%llu AUDIO=%llu mods=%llu,%llu,%llu,%llu "
			      "total=%llu,%llu,%llu,%llu trig=%d drops=%llu",
			      t, t - last, (unsigned long long)(c[0] - prev[0]), (unsigned long long)(c[1] - prev[1]),
			      (unsigned long long)(c[2] - prev[2]), (unsigned long long)(c[3] - prev[3]),
			      (unsigned long long)(c[HK_EXPO] - prev[HK_EXPO]), (unsigned long long)(c[HK_AUD] - prev[HK_AUD]),
			      (unsigned long long)(m[0] - prevm[0]), (unsigned long long)(m[1] - prevm[1]),
			      (unsigned long long)(m[2] - prevm[2]), (unsigned long long)(m[3] - prevm[3]),
			      (unsigned long long)c[0], (unsigned long long)c[1], (unsigned long long)c[2],
			      (unsigned long long)c[3], atomic_load(&g_trig), (unsigned long long)atomic_load(&g_drops));
			memcpy(prev, c, sizeof prev);
			memcpy(prevm, m, sizeof prevm);
			last = t;
			flush_log();
		}
	}
	return NULL;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Start                                                                                                          */

static void copy_env(const char *k, char *dst, size_t cap)
{
	const char *v = getenv(k);
	snprintf(dst, cap, "%s", v ? v : "");
}

/* everything after the gate: open the log, patch, start the writer. Returns 0 if the log is open. */
static int somvr_start(const struct site_spec *sites, const char *who)
{
	char mode[32], at[32];
	copy_env("SOMVR_LOG", g_log_path, sizeof g_log_path);
	if (!g_log_path[0])
		return -1;
	copy_env("SOMVR_TRIGGER", g_trig_path, sizeof g_trig_path);
	copy_env("SOMVR_MODE", mode, sizeof mode);
	copy_env("SOMVR_AT", at, sizeof at);
	{
		const char *c = getenv("SOMVR_COMFORT"), *cm = getenv("SOMVR_SBS_CM");
		g_comfort = c && !strcmp(c, "1");
		if (cm && cm[0]) {
			char *e;
			double v = strtod_l(cm, &e, g_cloc ? g_cloc : newlocale(LC_ALL_MASK, "C", (locale_t)0));
			if (*e == 0 && v >= 0 && v <= 50)
				g_sbs_cm = (float)v;
		}
	}
	g_fd = open(g_log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
	if (g_fd < 0)
		return -1;
	g_cloc = newlocale(LC_ALL_MASK, "C", (locale_t)0);
	int bad_mode = 0, bad_at = 0;
	if (!mode[0] || !strcmp(mode, "log"))
		g_mode = M_LOG;
	else if (!strcmp(mode, "off30"))
		g_mode = M_OFF30;
	else if (!strcmp(mode, "alt3"))
		g_mode = M_ALT3;
	else if (!strcmp(mode, "sbs"))
		g_mode = M_SBS;
	else
		bad_mode = 1;
	if (!at[0] || !strcmp(at, "view"))
		g_at_env = AT_VIEW;
	else if (!strcmp(at, "vp"))
		g_at_env = AT_VP;
	else
		bad_at = 1;
	atomic_store(&g_at, g_at_env);

	char exe[512] = "";
	ssize_t el = readlink("/proc/self/exe", exe, sizeof exe - 1);
	exe[el > 0 ? el : 0] = 0;
	struct timespec wall;
	clock_gettime(CLOCK_REALTIME, &wall);
	logf_("START somvr_spike1 built %s %s pid=%d ppid=%d exe=%s gate=%s t=%.6f wall=%lld.%03ld", __DATE__, __TIME__,
	      (int)getpid(), (int)getppid(), exe, who, mono(), (long long)wall.tv_sec, wall.tv_nsec / 1000000);
	logf_("CONFIG mode=%s at=%s comfort=%d savedir=%s trigger=%s%s%s",
	      g_mode_names[g_mode], g_at_env == AT_VIEW ? "view" : "vp",
	      g_comfort, getenv("SOMVR_SAVEDIR") ? getenv("SOMVR_SAVEDIR") : "(unset: no guard)",
	      g_trig_path[0] ? g_trig_path : "(unset: never fires)",
	      bad_mode ? " WARN unknown SOMVR_MODE, using log" : "", bad_at ? " WARN unknown SOMVR_AT, using view" : "");

	install_all(sites);
	g_active = 1;
	int need = g_mode == M_SBS ? HK_H4 : g_at_env == AT_VIEW ? HK_H1 : HK_H2;
	if (g_mode == M_SBS && (!g_h[HK_H1].installed || !g_h[HK_H2].installed))
		logf_("WARN mode sbs needs H1 and H2 as well: the eyes will not be offset or placed");
	if (g_mode != M_LOG && !g_h[need].installed)
		logf_("WARN mode %s at %s needs %s, which is not installed: nothing will be offset", mode,
		      g_at_env == AT_VIEW ? "view" : "vp", g_h[need].name);
	flush_log();

	sigset_t all, old;
	sigfillset(&all);
	pthread_sigmask(SIG_SETMASK, &all, &old); /* the writer thread gets no signals */
	pthread_t th;
	int rc = pthread_create(&th, NULL, writer_main, NULL);
	pthread_sigmask(SIG_SETMASK, &old, NULL);
	if (rc == 0)
		pthread_detach(th);
	else
		logf_("WARN writer thread: %s (no trigger, no periodic flush)", strerror(rc));
	return 0;
}

__attribute__((destructor)) static void somvr_fini(void)
{
	if (!g_active)
		return;
	logf_("END t=%.6f calls=%llu,%llu,%llu mods=%llu,%llu,%llu drops=%llu", mono(),
	      (unsigned long long)atomic_load(&g_h[0].calls), (unsigned long long)atomic_load(&g_h[1].calls),
	      (unsigned long long)atomic_load(&g_h[2].calls), (unsigned long long)atomic_load(&g_h[0].mods),
	      (unsigned long long)atomic_load(&g_h[1].mods), (unsigned long long)atomic_load(&g_h[2].mods),
	      (unsigned long long)atomic_load(&g_drops));
	flush_log();
}

#ifndef SOMVR_SELFTEST
/* ------------------------------------------------------------------------------------------------------------ */
/* Gate: only the game's own process                                                                              */

static int exe_is_game(void)
{
	char exe[4096];
	ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
	if (n <= 0)
		return 0;
	exe[n] = 0;
	const char *b = strrchr(exe, '/');
	return b && !strcmp(b + 1, SOM_EXE_NAME);
}

static int cmdline_is_helper(void)
{
	char buf[16384];
	int fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return 1;
	ssize_t n = read(fd, buf, sizeof buf - 1);
	close(fd);
	if (n < 0)
		return 1;
	buf[n] = 0;
	for (ssize_t i = 0; i < n; i += (ssize_t)strlen(buf + i) + 1)
		if (!strncmp(buf + i, "--type=", 7))
			return 1;
	return 0;
}

static int bid_cb(struct dl_phdr_info *info, size_t size, void *data)
{
	(void)size;
	int *res = data;
	*res = 0; /* the first object is the main program */
	if (info->dlpi_name && info->dlpi_name[0])
		return 1;
	if (info->dlpi_addr != 0)
		return 1; /* the game is non-PIE */
	unsigned char want[20];
	if (parse_hex(SOM_BUILDID, want, sizeof want) != 20)
		return 1;
	for (int i = 0; i < info->dlpi_phnum; i++) {
		const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
		if (ph->p_type != PT_NOTE)
			continue;
		const unsigned char *p = (const unsigned char *)(info->dlpi_addr + ph->p_vaddr), *end = p + ph->p_memsz;
		while (p + sizeof(ElfW(Nhdr)) <= end) {
			const ElfW(Nhdr) *nh = (const ElfW(Nhdr) *)p;
			const unsigned char *name = p + sizeof *nh, *desc = name + ((nh->n_namesz + 3) & ~3u);
			if (desc + nh->n_descsz > end)
				break;
			if (nh->n_type == NT_GNU_BUILD_ID && nh->n_namesz == 4 && !memcmp(name, "GNU", 4) &&
			    nh->n_descsz == 20 && !memcmp(desc, want, 20)) {
				*res = 1;
				return 1;
			}
			p = desc + ((nh->n_descsz + 3) & ~3u);
		}
	}
	return 1;
}

static int buildid_matches(void)
{
	int res = 0;
	dl_iterate_phdr(bid_cb, &res);
	return res;
}

/* The game's children (FeralCefSubprocess, FeralLinuxMessage, the crash reporter, the `steam` stub run through
 * system()) inherit its environment: drop this library from LD_PRELOAD (other entries stay; empty = unset). */
static void drop_self_from_preload(char *was, size_t wcap, char *now, size_t ncap)
{
	const char *lp = getenv("LD_PRELOAD");
	snprintf(was, wcap, "%s", lp ? lp : "");
	snprintf(now, ncap, "(unset)");
	if (!lp)
		return;
	Dl_info di;
	const char *me = dladdr((void *)drop_self_from_preload, &di) && di.dli_fname ? di.dli_fname : "";
	const char *mb = strrchr(me, '/') ? strrchr(me, '/') + 1 : me;
	char keep[4096] = "", tok[4096];
	size_t k = 0;
	for (const char *p = lp; *p;) {
		size_t n = strcspn(p, ": ");
		if (n && n < sizeof tok) {
			memcpy(tok, p, n);
			tok[n] = 0;
			const char *tb = strrchr(tok, '/') ? strrchr(tok, '/') + 1 : tok;
			int mine = !strcmp(tok, me) || (mb[0] && !strcmp(tb, mb));
			if (!mine && k + n + 2 < sizeof keep)
				k += (size_t)snprintf(keep + k, sizeof keep - k, "%s%s", k ? ":" : "", tok);
		}
		p += n;
		if (*p)
			p++;
	}
	if (k) {
		setenv("LD_PRELOAD", keep, 1);
		snprintf(now, ncap, "%s", keep);
	} else {
		unsetenv("LD_PRELOAD");
	}
}

__attribute__((constructor)) static void somvr_ctor(void)
{
	if (!exe_is_game() || cmdline_is_helper())
		return; /* not the game process */
	int want_guard = getenv("SOMVR_SAVEDIR") != NULL;
	const char *lp = getenv("SOMVR_LOG");
	if (!lp || !lp[0] || !buildid_matches()) {
		if (want_guard) /* fail closed: a guard was asked for and cannot be set up */
			_exit(3);
		return; /* no log path or another build: change nothing */
	}
	char was[1024], now[1024];
	drop_self_from_preload(was, sizeof was, now, sizeof now);
	int rc = somvr_start(g_game_sites, "exe+cmdline+buildid");
	if (rc == 0)
		logf_("ENV LD_PRELOAD was=%s now=%s (children do not load the library)", was, now);
	guard_or_die(g_guard_sites, rc == 0);
}
#endif
