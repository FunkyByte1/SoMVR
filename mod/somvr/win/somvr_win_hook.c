/* somvr_win_hook.c - the host's patch engine (somvr_win_hook.h, somvr_win_host.h; docs/re-xr-pipeline.md 156):
 * the logic's game-code writes, entry trampolines and mid-function detours (MinHook 1.3.4), each recorded by its
 * address with a host-owned pointer slot, so a swap of the logic module rewrites slots and never game code. */
#define SOMVR_HOST 1
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include "MinHook.h"
#include "somvr_win.h"
#include "somvr_win_hook.h"
#include "somvr_win_host.h"
#include "somvr_win_hostint.h"

/* MinHook's trampoline.c uses MSVC's __movsb intrinsic; with the xwin headers first clang leaves it a call */
void __movsb(unsigned char *d, const unsigned char *src, size_t n);
void __movsb(unsigned char *d, const unsigned char *src, size_t n)
{
	__asm__ volatile("rep movsb" : "+D"(d), "+S"(src), "+c"(n) : : "memory");
}

static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static MH_STATUS g_init = MH_UNKNOWN;
static SRWLOCK g_mx = SRWLOCK_INIT; /* the records (the logic's install paths, its runtime toggles, the swap) */

static BOOL CALLBACK mh_init(PINIT_ONCE o, PVOID p, PVOID *c)
{
	(void)o, (void)p, (void)c;
	g_init = MH_Initialize();
	return TRUE;
}

static int mh_ready(char *why, size_t cap)
{
	InitOnceExecuteOnce(&g_once, mh_init, NULL, NULL);
	if (g_init != MH_OK && g_init != MH_ERROR_ALREADY_INITIALIZED) {
		snprintf(why, cap, "MH_Initialize: %s", MH_StatusToString(g_init));
		return -1;
	}
	return 0;
}

/* ---- the host stubs: 8-byte `jmp [rip+4090]` stubs in an r-x page, each one's slot in the rw page after it ------ */
#define SP_STUBS 512 /* 4096 / 8 */
static struct { uintptr_t code; int used; } g_sp[256];
static int g_nsp;

static int reach(uintptr_t a, uintptr_t b)
{
	int64_t d = (int64_t)a - (int64_t)b;
	return d > -0x7ff00000ll && d < 0x7ff00000ll;
}

/* a free stub within rel32 reach of nr (0 = anywhere); its slot set to v first. 0 = none */
static uintptr_t stub_new(uintptr_t nr, uintptr_t v)
{
	int k = -1;
	for (int i = 0; i < g_nsp && k < 0; i++)
		if (g_sp[i].used < SP_STUBS && (!nr || (reach(g_sp[i].code, nr) && reach(g_sp[i].code + 4096, nr))))
			k = i;
	if (k < 0) {
		if (g_nsp >= (int)(sizeof g_sp / sizeof g_sp[0]))
			return 0;
		unsigned char *p = nr ? somvr_alloc_near(nr, nr, 8192) : VirtualAlloc(NULL, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		if (!p)
			return 0;
		for (int i = 0; i < SP_STUBS; i++) { /* every stub at once: the code page is never written again */
			static const unsigned char st[8] = { 0xff, 0x25, 0xfa, 0x0f, 0x00, 0x00, 0xcc, 0xcc };
			memcpy(p + 8 * i, st, 8);
		}
		DWORD old;
		if (!VirtualProtect(p, 4096, PAGE_EXECUTE_READ, &old))
			return 0;
		FlushInstructionCache(GetCurrentProcess(), p, 4096);
		g_sp[k = g_nsp++].code = (uintptr_t)p;
		g_sp[k].used = 0;
	}
	uintptr_t s = g_sp[k].code + 8 * (uintptr_t)g_sp[k].used++;
	__atomic_store_n((uintptr_t *)(s + 4096), v, __ATOMIC_RELEASE);
	return s;
}
#define STUB_SLOT(s) ((uintptr_t *)((s) + 4096))

/* ---- the records --------------------------------------------------------------------------------------------- */
enum { HP_REL = 1, HP_SLOT, HP_DATA, HP_ENTRY, HP_MID };
static const char *const g_kind_w[] = { "?", "call/jmp", "slot", "bytes", "entry", "mid" };
struct hrec {
	uintptr_t at;       /* REL: the E8/E9; SLOT: the slot; DATA: the first byte; ENTRY: the function; MID: the instruction */
	uint8_t kind, n;    /* n: the bytes the patch covers (REL 5, SLOT 8, DATA n, ENTRY/MID 5) */
	uint8_t keep;       /* never released (somvr_host_keep) */
	uint8_t claimed;    /* the live module's claim is in effect (0: released - the original behaviour) */
	uint8_t installed;  /* the game-side patch exists (the rel32, the slot, the bytes, MinHook enabled) */
	uint8_t orig[16];   /* the bytes before the first patch */
	uint8_t dval[16], pdval[16]; /* DATA: the live claim's bytes, the staged claim's */
	uintptr_t base;     /* REL: the original target; SLOT: the original pointer; ENTRY: the trampoline; MID: the no-op */
	uintptr_t stub;     /* REL/ENTRY/MID: the host stub (its slot STUB_SLOT) */
	uintptr_t val, pval; /* the live claim's target / pointer / function, the staged claim's */
	int pgen;           /* the generation of the staged claim (== g_stage while staging) */
	int gen;            /* the generation of the live claim */
};
#define NREC 4096
static struct hrec g_rec[NREC];
static int g_nrec;
static int g_gen;              /* the live module's generation */
static int g_stage = -1;       /* the generation being staged (a reload's init), -1 = none */
static uintptr_t g_stage_lo, g_stage_hi; /* the staged module's image: its calls are staged */
static struct { int rel, slot, data, entry, mid, plain; } g_n;

static void mid_nop(struct somvr_win_regs *r) { (void)r; }

static int in_image(uintptr_t a)
{
	MEMORY_BASIC_INFORMATION m;
	return VirtualQuery((const void *)a, &m, sizeof m) && m.State == MEM_COMMIT && m.Type == MEM_IMAGE;
}

static struct hrec *rec_at(uintptr_t at, int kind)
{
	for (int i = 0; i < g_nrec; i++)
		if (g_rec[i].at == at && (!kind || g_rec[i].kind == kind))
			return &g_rec[i];
	return NULL;
}

static int staged(uintptr_t caller) { return g_stage >= 0 && caller >= g_stage_lo && caller < g_stage_hi; }

/* the bytes a record shows: as patched by the claim v (REL: the opcode + rel32 to the host stub; SLOT: v; DATA: d;
 * ENTRY/MID: the live bytes - MinHook's jump) or, released, the original ones */
static void rec_bytes(const struct hrec *r, int patched, uintptr_t v, const uint8_t *d, uint8_t *out)
{
	if (!patched) {
		memcpy(out, r->orig, r->n);
		return;
	}
	switch (r->kind) {
	case HP_REL: {
		int32_t rel = (int32_t)((int64_t)r->stub - (int64_t)(r->at + 5));
		out[0] = r->orig[0];
		memcpy(out + 1, &rel, 4);
		break;
	}
	case HP_SLOT:
		memcpy(out, &v, 8);
		break;
	case HP_DATA:
		memcpy(out, d, r->n);
		break;
	default:
		if (r->installed)
			memcpy(out, (const void *)r->at, r->n);
		else
			memcpy(out, r->orig, r->n);
	}
}

static int readable(uintptr_t a, size_t n)
{
	for (uintptr_t p = a & ~(uintptr_t)4095; p < a + n; p += 4096) {
		MEMORY_BASIC_INFORMATION m;
		if (!VirtualQuery((const void *)p, &m, sizeof m) || m.State != MEM_COMMIT ||
		    (m.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || !m.Protect)
			return 0;
	}
	return 1;
}

/* mode 0: the live module's view (its claims patched, the rest original); 1: the staged module's (its staged claims
 * patched); 2: the game's original bytes everywhere we patched */
static void view_as(uintptr_t a, void *dst, size_t n, int mode)
{
	if (readable(a, n))
		memcpy(dst, (const void *)a, n);
	for (int i = 0; i < g_nrec; i++) {
		const struct hrec *r = &g_rec[i];
		if (r->at >= a + n || r->at + r->n <= a)
			continue;
		uint8_t b[16];
		if (mode == 1)
			rec_bytes(r, r->pgen == g_stage, r->pval, r->pdval, b);
		else
			rec_bytes(r, mode == 0 && r->claimed, r->val, r->dval, b);
		for (unsigned k = 0; k < r->n; k++)
			if (r->at + k >= a && r->at + k < a + n)
				((uint8_t *)dst)[r->at + k - a] = b[k];
	}
}

__attribute__((noinline)) void somvr_host_view(uintptr_t a, void *dst, size_t n)
{
	uintptr_t caller = (uintptr_t)__builtin_return_address(0);
	AcquireSRWLockShared(&g_mx);
	view_as(a, dst, n, staged(caller));
	ReleaseSRWLockShared(&g_mx);
}

/* write n bytes (code or a read-only slot): the page(s) writable for the copy, the protection back */
static int raw_write(uintptr_t dst, const void *src, size_t n, char *err, size_t cap)
{
	DWORD old, o2;
	if (!VirtualProtect((void *)dst, n, PAGE_EXECUTE_READWRITE, &old))
		return snprintf(err, cap, "VirtualProtect %#llx: %lu", (unsigned long long)dst, GetLastError()), -1;
	memcpy((void *)dst, src, n);
	VirtualProtect((void *)dst, n, old, &o2);
	FlushInstructionCache(GetCurrentProcess(), (void *)dst, n);
	return 0;
}

/* a patch the host makes for itself (the stand-in's hook test): no logic module claims it, a swap leaves it */
static int host_own(uintptr_t caller)
{
	uintptr_t b;
	return host_module_of(caller, &b) < 0;
}

static struct hrec *rec_new(uintptr_t at, int kind, int n, char *err, size_t cap)
{
	if (g_nrec >= NREC)
		return snprintf(err, cap, "the host's %d patch records are used up", NREC), NULL;
	struct hrec *r = &g_rec[g_nrec];
	memset(r, 0, sizeof *r);
	r->at = at, r->kind = (uint8_t)kind, r->n = (uint8_t)n, r->pgen = -1, r->gen = -1;
	view_as(at, r->orig, (size_t)n, 2); /* what the game had (the earlier records' originals over it) */
	g_nrec++;
	return r;
}

__attribute__((noinline)) int somvr_host_write(uintptr_t dst, const void *src, size_t n, char *err, size_t cap)
{
	uintptr_t caller = (uintptr_t)__builtin_return_address(0);
	if (!n || n > 16 || !in_image(dst)) { /* the game's heap (a record's field): written as before, not recorded */
		g_n.plain++;
		return raw_write(dst, src, n, err, cap);
	}
	AcquireSRWLockExclusive(&g_mx);
	int st = staged(caller), rc = 0;
	uint8_t op = 0;
	view_as(dst - 1, &op, 1, st);
	int kind = n == 4 && (op == 0xe8 || op == 0xe9) ? HP_REL : n == 8 && !(dst & 7) ? HP_SLOT : HP_DATA;
	uintptr_t at = kind == HP_REL ? dst - 1 : dst;
	struct hrec *r = rec_at(at, kind);
	if (!r && (r = rec_new(at, kind, kind == HP_REL ? 5 : (int)n, err, cap)))
		r->keep = (uint8_t)host_own(caller);
	if (!r) {
		ReleaseSRWLockExclusive(&g_mx);
		return -1;
	}
	if (kind == HP_DATA && r->n != n) {
		ReleaseSRWLockExclusive(&g_mx);
		return snprintf(err, cap, "%zu bytes at %#llx: an earlier patch there has %u", n, (unsigned long long)dst, r->n), -1;
	}
	uintptr_t v = 0;
	if (kind == HP_REL) {
		int32_t rel;
		memcpy(&rel, src, 4);
		v = at + 5 + (intptr_t)rel;
		if (!r->stub) {
			int32_t o;
			memcpy(&o, r->orig + 1, 4);
			r->base = at + 5 + (intptr_t)o;
			if (!(r->stub = stub_new(at, r->base))) {
				ReleaseSRWLockExclusive(&g_mx);
				return snprintf(err, cap, "no host stub within +-2 GB of %#llx", (unsigned long long)at), -1;
			}
			g_n.rel++;
		}
	} else if (kind == HP_SLOT) {
		memcpy(&v, src, 8);
		if (r->gen < 0 && r->pgen < 0)
			memcpy(&r->base, r->orig, 8), g_n.slot++;
	} else if (r->gen < 0 && r->pgen < 0) {
		g_n.data++;
	}
	if (st) { /* a reload's init: kept for the swap */
		r->pgen = g_stage, r->pval = v;
		if (kind == HP_DATA)
			memcpy(r->pdval, src, n);
	} else {
		r->claimed = 1, r->gen = g_gen, r->val = v;
		if (kind == HP_DATA)
			memcpy(r->dval, src, n);
		if (kind == HP_REL) {
			__atomic_store_n(STUB_SLOT(r->stub), v, __ATOMIC_RELEASE);
			if (!r->installed) {
				int32_t rel = (int32_t)((int64_t)r->stub - (int64_t)(at + 5));
				rc = raw_write(at + 1, &rel, 4, err, cap);
			}
		} else {
			rc = raw_write(dst, src, n, err, cap);
		}
		if (rc == 0)
			r->installed = 1;
	}
	ReleaseSRWLockExclusive(&g_mx);
	return rc;
}

void somvr_host_keep(uintptr_t at)
{
	AcquireSRWLockExclusive(&g_mx);
	for (int i = 0; i < g_nrec; i++)
		if (g_rec[i].at == at || (g_rec[i].kind == HP_REL && g_rec[i].at + 1 == at))
			g_rec[i].keep = 1;
	ReleaseSRWLockExclusive(&g_mx);
}

/* ---- MinHook: an entry hook's detour and a mid detour's C call are host stubs ---------------------------------- */
__attribute__((noinline)) int somvr_win_hook_entry(uintptr_t fn, void *hook, void **orig, char *why, size_t cap)
{
	uintptr_t caller = (uintptr_t)__builtin_return_address(0);
	if (mh_ready(why, cap))
		return -1;
	AcquireSRWLockExclusive(&g_mx);
	int st = staged(caller);
	struct hrec *r = rec_at(fn, HP_ENTRY);
	if (r) { /* installed before: the slot only */
		if (st)
			r->pgen = g_stage, r->pval = (uintptr_t)hook;
		else
			r->claimed = 1, r->gen = g_gen, r->val = (uintptr_t)hook, __atomic_store_n(STUB_SLOT(r->stub), (uintptr_t)hook, __ATOMIC_RELEASE);
		if (orig)
			*orig = (void *)r->base;
		ReleaseSRWLockExclusive(&g_mx);
		snprintf(why, cap, "entry %#llx -> host stub %#llx -> %p (installed before; the original via %#llx)",
			 (unsigned long long)fn, (unsigned long long)r->stub, hook, (unsigned long long)r->base);
		return 0;
	}
	if (!(r = rec_new(fn, HP_ENTRY, 5, why, cap))) {
		ReleaseSRWLockExclusive(&g_mx);
		return -1;
	}
	uintptr_t s = stub_new(0, (uintptr_t)hook);
	void *tramp = NULL;
	MH_STATUS e = s ? MH_CreateHook((LPVOID)fn, (LPVOID)s, &tramp) : MH_ERROR_MEMORY_ALLOC;
	if (e != MH_OK) {
		g_nrec--; /* the newest record: nothing of it is live */
		ReleaseSRWLockExclusive(&g_mx);
		return snprintf(why, cap, "MH_CreateHook %#llx: %s", (unsigned long long)fn, MH_StatusToString(e)), -1;
	}
	r->stub = s, r->base = (uintptr_t)tramp;
	r->keep = (uint8_t)host_own(caller);
	g_n.entry++;
	if (st) {
		r->pgen = g_stage, r->pval = (uintptr_t)hook; /* enabled at the swap */
	} else {
		r->claimed = 1, r->gen = g_gen, r->val = (uintptr_t)hook;
		if ((e = MH_EnableHook((LPVOID)fn)) != MH_OK) {
			__atomic_store_n(STUB_SLOT(s), (uintptr_t)tramp, __ATOMIC_RELEASE);
			r->claimed = 0;
			ReleaseSRWLockExclusive(&g_mx);
			return snprintf(why, cap, "MH_EnableHook %#llx: %s", (unsigned long long)fn, MH_StatusToString(e)), -1;
		}
		r->installed = 1;
	}
	if (orig)
		*orig = tramp;
	ReleaseSRWLockExclusive(&g_mx);
	snprintf(why, cap, "entry %#llx -> host stub %#llx -> %p (original via %p)", (unsigned long long)fn,
		 (unsigned long long)s, hook, tramp);
	return 0;
}

/* the mid-function stub, assembled once here and copied per detour: it saves every register into a
 * somvr_win_regs on an aligned frame, calls the C function (slot CFN: the host stub whose slot holds the logic's
 * function) with it in rcx, restores (the C side's changes included) and jumps to MinHook's trampoline (slot TRAMP):
 * the relocated instructions, then back. */
extern const unsigned char somvr_mid_tpl[], somvr_mid_tpl_cfn[], somvr_mid_tpl_tramp[], somvr_mid_tpl_end[];
__asm__(".section .rdata,\"dr\"\n.p2align 4\n.globl somvr_mid_tpl\nsomvr_mid_tpl:\n"
	"  pushfq\n  push %rbp\n  mov %rsp, %rbp\n  sub $0x1c0, %rsp\n  and $-16, %rsp\n"
	"  mov %rax, 0x00(%rsp)\n  mov %rcx, 0x08(%rsp)\n  mov %rdx, 0x10(%rsp)\n  mov %rsi, 0x18(%rsp)\n"
	"  mov %rdi, 0x20(%rsp)\n  mov %r8, 0x28(%rsp)\n  mov %r9, 0x30(%rsp)\n  mov %r10, 0x38(%rsp)\n"
	"  mov %r11, 0x40(%rsp)\n  mov 16(%rbp), %rax\n  mov %rax, 0x48(%rsp)\n"
	"  movdqu %xmm0, 0x50(%rsp)\n  movdqu %xmm1, 0x60(%rsp)\n  movdqu %xmm2, 0x70(%rsp)\n  movdqu %xmm3, 0x80(%rsp)\n"
	"  movdqu %xmm4, 0x90(%rsp)\n  movdqu %xmm5, 0xa0(%rsp)\n  movdqu %xmm6, 0xb0(%rsp)\n  movdqu %xmm7, 0xc0(%rsp)\n"
	"  movdqu %xmm8, 0xd0(%rsp)\n  movdqu %xmm9, 0xe0(%rsp)\n  movdqu %xmm10, 0xf0(%rsp)\n  movdqu %xmm11, 0x100(%rsp)\n"
	"  movdqu %xmm12, 0x110(%rsp)\n  movdqu %xmm13, 0x120(%rsp)\n  movdqu %xmm14, 0x130(%rsp)\n"
	"  movdqu %xmm15, 0x140(%rsp)\n"
	"  mov %rbx, 0x150(%rsp)\n  mov 0(%rbp), %rax\n  mov %rax, 0x158(%rsp)\n  mov %r12, 0x160(%rsp)\n"
	"  mov %r13, 0x168(%rsp)\n  mov %r14, 0x170(%rsp)\n  mov %r15, 0x178(%rsp)\n  mov 8(%rbp), %rax\n"
	"  mov %rax, 0x180(%rsp)\n  lea 16(%rbp), %rax\n  mov %rax, 0x188(%rsp)\n"
	"  mov %rsp, %rcx\n  sub $0x20, %rsp\n  cld\n  call *somvr_mid_tpl_cfn(%rip)\n  add $0x20, %rsp\n"
	"  mov 0x150(%rsp), %rbx\n  mov 0x160(%rsp), %r12\n  mov 0x168(%rsp), %r13\n  mov 0x170(%rsp), %r14\n"
	"  mov 0x178(%rsp), %r15\n  mov 0x158(%rsp), %rax\n  mov %rax, 0(%rbp)\n  mov 0x180(%rsp), %rax\n"
	"  mov %rax, 8(%rbp)\n"
	"  movdqu 0x50(%rsp), %xmm0\n  movdqu 0x60(%rsp), %xmm1\n  movdqu 0x70(%rsp), %xmm2\n  movdqu 0x80(%rsp), %xmm3\n"
	"  movdqu 0x90(%rsp), %xmm4\n  movdqu 0xa0(%rsp), %xmm5\n  movdqu 0xb0(%rsp), %xmm6\n  movdqu 0xc0(%rsp), %xmm7\n"
	"  movdqu 0xd0(%rsp), %xmm8\n  movdqu 0xe0(%rsp), %xmm9\n  movdqu 0xf0(%rsp), %xmm10\n  movdqu 0x100(%rsp), %xmm11\n"
	"  movdqu 0x110(%rsp), %xmm12\n  movdqu 0x120(%rsp), %xmm13\n  movdqu 0x130(%rsp), %xmm14\n"
	"  movdqu 0x140(%rsp), %xmm15\n"
	"  mov 0x08(%rsp), %rcx\n  mov 0x10(%rsp), %rdx\n  mov 0x18(%rsp), %rsi\n  mov 0x20(%rsp), %rdi\n"
	"  mov 0x28(%rsp), %r8\n  mov 0x30(%rsp), %r9\n  mov 0x38(%rsp), %r10\n  mov 0x40(%rsp), %r11\n"
	"  mov 0x00(%rsp), %rax\n  mov %rbp, %rsp\n  pop %rbp\n  popfq\n"
	"  jmp *somvr_mid_tpl_tramp(%rip)\n"
	".p2align 3\n.globl somvr_mid_tpl_cfn\nsomvr_mid_tpl_cfn:\n  .quad 0\n"
	".globl somvr_mid_tpl_tramp\nsomvr_mid_tpl_tramp:\n  .quad 0\n"
	".globl somvr_mid_tpl_end\nsomvr_mid_tpl_end:\n.text\n");

static unsigned char *g_mid_page;
static size_t g_mid_used;

__attribute__((noinline)) int somvr_win_hook_mid(uintptr_t at, void (*cfn)(struct somvr_win_regs *), char *why, size_t cap)
{
	uintptr_t caller = (uintptr_t)__builtin_return_address(0);
	if (mh_ready(why, cap))
		return -1;
	AcquireSRWLockExclusive(&g_mx);
	int st = staged(caller);
	struct hrec *r = rec_at(at, HP_MID);
	if (r) {
		if (st)
			r->pgen = g_stage, r->pval = (uintptr_t)cfn;
		else
			r->claimed = 1, r->gen = g_gen, r->val = (uintptr_t)cfn, __atomic_store_n(STUB_SLOT(r->stub), (uintptr_t)cfn, __ATOMIC_RELEASE);
		ReleaseSRWLockExclusive(&g_mx);
		snprintf(why, cap, "mid %#llx -> host stub %#llx -> %p (installed before)", (unsigned long long)at,
			 (unsigned long long)r->stub, (void *)cfn);
		return 0;
	}
	size_t n = (size_t)(somvr_mid_tpl_end - somvr_mid_tpl);
	size_t ocfn = (size_t)(somvr_mid_tpl_cfn - somvr_mid_tpl), otr = (size_t)(somvr_mid_tpl_tramp - somvr_mid_tpl);
	size_t step = (n + 15) & ~(size_t)15;
	const char *bad = NULL;
	if (!g_mid_page && !(g_mid_page = VirtualAlloc(NULL, 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)))
		bad = "no page for the stubs";
	else if (g_mid_used + step > 65536)
		bad = "stub page full";
	uintptr_t hs = bad ? 0 : stub_new(0, (uintptr_t)cfn);
	if (!bad && !hs)
		bad = "no host stub";
	if (!bad && !(r = rec_new(at, HP_MID, 5, why, cap)))
		bad = why;
	if (bad) {
		ReleaseSRWLockExclusive(&g_mx);
		if (bad != why)
			snprintf(why, cap, "%s", bad);
		return -1;
	}
	unsigned char *stub = g_mid_page + g_mid_used;
	DWORD old;
	VirtualProtect(g_mid_page, 65536, PAGE_READWRITE, &old);
	memcpy(stub, somvr_mid_tpl, n);
	memcpy(stub + ocfn, &hs, 8);
	VirtualProtect(g_mid_page, 65536, PAGE_EXECUTE_READ, &old);
	void *tramp = NULL;
	MH_STATUS s = MH_CreateHook((LPVOID)at, stub, &tramp);
	if (s != MH_OK) {
		g_nrec--;
		ReleaseSRWLockExclusive(&g_mx);
		return snprintf(why, cap, "MH_CreateHook %#llx: %s", (unsigned long long)at, MH_StatusToString(s)), -1;
	}
	VirtualProtect(g_mid_page, 65536, PAGE_READWRITE, &old);
	memcpy(stub + otr, &tramp, 8);
	VirtualProtect(g_mid_page, 65536, PAGE_EXECUTE_READ, &old);
	FlushInstructionCache(GetCurrentProcess(), stub, n);
	g_mid_used += step;
	r->stub = hs, r->base = (uintptr_t)mid_nop;
	r->keep = (uint8_t)host_own(caller);
	g_n.mid++;
	if (st) {
		r->pgen = g_stage, r->pval = (uintptr_t)cfn;
	} else {
		r->claimed = 1, r->gen = g_gen, r->val = (uintptr_t)cfn;
		if ((s = MH_EnableHook((LPVOID)at)) != MH_OK) {
			__atomic_store_n(STUB_SLOT(hs), (uintptr_t)mid_nop, __ATOMIC_RELEASE);
			r->claimed = 0;
			ReleaseSRWLockExclusive(&g_mx);
			return snprintf(why, cap, "MH_EnableHook %#llx: %s", (unsigned long long)at, MH_StatusToString(s)), -1;
		}
		r->installed = 1;
	}
	ReleaseSRWLockExclusive(&g_mx);
	snprintf(why, cap, "mid %#llx -> stub %p -> host stub %#llx -> %p, then the relocated bytes at %p", (unsigned long long)at,
		 (void *)stub, (unsigned long long)hs, (void *)cfn, tramp);
	return 0;
}

/* an entry or mid hook back to the original (its slot; the jump stays): 0 = ok */
int somvr_win_hook_remove(uintptr_t at)
{
	AcquireSRWLockExclusive(&g_mx);
	struct hrec *r = rec_at(at, HP_ENTRY);
	if (!r)
		r = rec_at(at, HP_MID);
	if (r)
		r->claimed = 0, __atomic_store_n(STUB_SLOT(r->stub), r->base, __ATOMIC_RELEASE);
	ReleaseSRWLockExclusive(&g_mx);
	return r ? 0 : -1;
}

/* ---- the swap (somvr_win_host.c drives it) -------------------------------------------------------------------- */
void hk_stage_begin(int gen, uintptr_t lo, uintptr_t hi)
{
	AcquireSRWLockExclusive(&g_mx);
	g_stage_lo = lo, g_stage_hi = hi, g_stage = gen;
	ReleaseSRWLockExclusive(&g_mx);
}

/* the game's threads held (all but this one): Toolhelp's list, each suspended and its context read (the suspension
 * done); no heap, no log, no lock of anyone else's while they are held */
static HANDLE g_frz[1024];
static int g_nfrz;
static int freeze(void)
{
	g_nfrz = 0;
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snap == INVALID_HANDLE_VALUE)
		return -1;
	THREADENTRY32 te;
	te.dwSize = sizeof te;
	DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId();
	for (BOOL ok = Thread32First(snap, &te); ok && g_nfrz < (int)(sizeof g_frz / sizeof g_frz[0]); ok = Thread32Next(snap, &te)) {
		if (te.th32OwnerProcessID != pid || te.th32ThreadID == me)
			continue;
		HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
		if (!h)
			continue;
		if (SuspendThread(h) == (DWORD)-1) {
			CloseHandle(h);
			continue;
		}
		CONTEXT c;
		memset(&c, 0, sizeof c);
		c.ContextFlags = CONTEXT_CONTROL;
		GetThreadContext(h, &c);
		g_frz[g_nfrz++] = h;
	}
	CloseHandle(snap);
	return g_nfrz;
}
static void thaw(void)
{
	for (int i = 0; i < g_nfrz; i++)
		ResumeThread(g_frz[i]), CloseHandle(g_frz[i]);
	g_nfrz = 0;
}

/* a staged claim's patch the game does not have yet (a new SITE): the bytes the freeze writes */
static int rec_pending_patch(const struct hrec *r) { return r->kind == HP_REL || r->kind == HP_SLOT || r->kind == HP_DATA; }

/* the swap: every record's slot (or slot-like store) set at once, the game's threads held. A staged claim -> its
 * value; no claim (the new module has no such SITE) -> the original, unless kept. Returns the frozen threads' count;
 * the counts and the freeze's time into st. */
int hk_commit(struct hk_commit_stats *st)
{
	memset(st, 0, sizeof *st);
	AcquireSRWLockExclusive(&g_mx);
	int gen = g_stage;
	/* 1. MinHook's new hooks (their slots already hold the new module's functions): MinHook holds the threads itself */
	for (int i = 0; i < g_nrec; i++) {
		struct hrec *r = &g_rec[i];
		if ((r->kind == HP_ENTRY || r->kind == HP_MID) && r->pgen == gen && !r->installed) {
			__atomic_store_n(STUB_SLOT(r->stub), r->pval, __ATOMIC_RELEASE);
			if (MH_EnableHook((LPVOID)r->at) == MH_OK)
				r->installed = 1, st->added++;
			else
				st->failed++;
		}
	}
	/* 2. the pages the freeze stores into: writable first (VirtualProtect is a call the held threads might block) */
	static struct { uintptr_t pg; DWORD old; } prot[512];
	int np = 0;
	for (int i = 0; i < g_nrec; i++) {
		struct hrec *r = &g_rec[i];
		if (!rec_pending_patch(r) || (r->kind == HP_REL && r->installed))
			continue;
		if (r->pgen != gen && (r->keep || !r->installed))
			continue;
		for (uintptr_t p = r->at & ~(uintptr_t)4095; p < r->at + r->n; p += 4096) {
			int k = 0;
			while (k < np && prot[k].pg != p)
				k++;
			if (k == np && np < (int)(sizeof prot / sizeof prot[0])) {
				if (VirtualProtect((void *)p, 4096, PAGE_EXECUTE_READWRITE, &prot[np].old))
					prot[np++].pg = p;
			}
		}
	}
	LARGE_INTEGER f, a, b;
	QueryPerformanceFrequency(&f), QueryPerformanceCounter(&a);
	st->threads = freeze();
	/* 3. the stores */
	for (int i = 0; i < g_nrec; i++) {
		struct hrec *r = &g_rec[i];
		int claim = r->pgen == gen;
		if (!claim && r->keep) {
			st->kept++;
			continue;
		}
		if (!claim && !r->installed)
			continue;
		uintptr_t v = claim ? r->pval : r->base;
		switch (r->kind) {
		case HP_REL:
			__atomic_store_n(STUB_SLOT(r->stub), v, __ATOMIC_RELEASE);
			if (!r->installed) {
				int32_t rel = (int32_t)((int64_t)r->stub - (int64_t)(r->at + 5));
				memcpy((void *)(r->at + 1), &rel, 4);
				r->installed = 1, st->added++;
			}
			break;
		case HP_SLOT:
			__atomic_store_n((uintptr_t *)r->at, v, __ATOMIC_RELEASE);
			if (!r->installed)
				r->installed = 1, st->added++;
			break;
		case HP_DATA:
			memcpy((void *)r->at, claim ? r->pdval : r->orig, r->n);
			if (!r->installed)
				r->installed = 1, st->added++;
			break;
		default:
			if (r->installed)
				__atomic_store_n(STUB_SLOT(r->stub), v, __ATOMIC_RELEASE);
		}
		if (claim) {
			r->claimed = 1, r->gen = gen, r->val = r->pval;
			memcpy(r->dval, r->pdval, sizeof r->dval);
			st->claimed++;
		} else {
			r->claimed = 0;
			st->released++;
		}
	}
	thaw();
	QueryPerformanceCounter(&b);
	st->freeze_ms = 1e3 * (double)(b.QuadPart - a.QuadPart) / (double)f.QuadPart;
	/* 4. the protections back, the code pages flushed */
	for (int k = 0; k < np; k++) {
		DWORD o2;
		VirtualProtect((void *)prot[k].pg, 4096, prot[k].old, &o2);
		FlushInstructionCache(GetCurrentProcess(), (void *)prot[k].pg, 4096);
	}
	st->pages = np;
	g_gen = gen, g_stage = -1, g_stage_lo = g_stage_hi = 0;
	ReleaseSRWLockExclusive(&g_mx);
	return st->threads;
}

/* a staged module that never goes live (its init refused, the swap called off): its claims dropped */
void hk_stage_abort(void)
{
	AcquireSRWLockExclusive(&g_mx);
	for (int i = 0; i < g_nrec; i++)
		if (g_rec[i].pgen == g_stage)
			g_rec[i].pgen = -1;
	g_stage = -1, g_stage_lo = g_stage_hi = 0;
	ReleaseSRWLockExclusive(&g_mx);
}

int hk_gen(void) { return g_gen; }

void hk_stats(char *out, size_t cap)
{
	AcquireSRWLockShared(&g_mx);
	int live = 0, rel = 0, kept = 0;
	for (int i = 0; i < g_nrec; i++)
		live += g_rec[i].claimed, rel += !g_rec[i].claimed && g_rec[i].installed, kept += g_rec[i].keep;
	snprintf(out, cap, "%d patches (%d call/jmp, %d slots, %d bytes, %d entry, %d mid; %d claimed, %d released, %d kept), "
		 "%d stub pages, %d plain writes", g_nrec, g_n.rel, g_n.slot, g_n.data, g_n.entry, g_n.mid, live, rel, kept, g_nsp,
		 g_n.plain);
	ReleaseSRWLockShared(&g_mx);
	(void)g_kind_w;
}
