/* somvr_win_hook.c - see somvr_win_hook.h */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "MinHook.h"
#include "somvr_win_hook.h"

/* MinHook's trampoline.c uses MSVC's __movsb intrinsic; with the xwin headers first clang leaves it a call */
void __movsb(unsigned char *d, const unsigned char *src, size_t n);
void __movsb(unsigned char *d, const unsigned char *src, size_t n)
{
	__asm__ volatile("rep movsb" : "+D"(d), "+S"(src), "+c"(n) : : "memory");
}

static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static MH_STATUS g_init = MH_UNKNOWN;

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

int somvr_win_hook_entry(uintptr_t fn, void *hook, void **orig, char *why, size_t cap)
{
	if (mh_ready(why, cap))
		return -1;
	MH_STATUS s = MH_CreateHook((LPVOID)fn, hook, orig);
	if (s != MH_OK)
		return snprintf(why, cap, "MH_CreateHook %#llx: %s", (unsigned long long)fn, MH_StatusToString(s)), -1;
	if ((s = MH_EnableHook((LPVOID)fn)) != MH_OK) {
		MH_RemoveHook((LPVOID)fn);
		return snprintf(why, cap, "MH_EnableHook %#llx: %s", (unsigned long long)fn, MH_StatusToString(s)), -1;
	}
	snprintf(why, cap, "entry %#llx -> %p (original via %p)", (unsigned long long)fn, hook, orig ? *orig : NULL);
	return 0;
}

/* the mid-function stub, assembled once here and copied per detour: it saves every register into a
 * somvr_win_regs on an aligned frame, calls the C function (slot CFN) with it in rcx, restores (the C side's changes
 * included) and jumps to MinHook's trampoline (slot TRAMP): the relocated instructions, then back. */
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

int somvr_win_hook_mid(uintptr_t at, void (*cfn)(struct somvr_win_regs *), char *why, size_t cap)
{
	if (mh_ready(why, cap))
		return -1;
	size_t n = (size_t)(somvr_mid_tpl_end - somvr_mid_tpl);
	size_t ocfn = (size_t)(somvr_mid_tpl_cfn - somvr_mid_tpl), otr = (size_t)(somvr_mid_tpl_tramp - somvr_mid_tpl);
	size_t step = (n + 15) & ~(size_t)15;
	if (!g_mid_page && !(g_mid_page = VirtualAlloc(NULL, 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)))
		return snprintf(why, cap, "no page for the stubs"), -1;
	if (g_mid_used + step > 65536)
		return snprintf(why, cap, "stub page full"), -1;
	unsigned char *stub = g_mid_page + g_mid_used;
	DWORD old;
	VirtualProtect(g_mid_page, 65536, PAGE_READWRITE, &old);
	memcpy(stub, somvr_mid_tpl, n);
	memcpy(stub + ocfn, &cfn, 8);
	VirtualProtect(g_mid_page, 65536, PAGE_EXECUTE_READ, &old);
	void *tramp = NULL;
	MH_STATUS s = MH_CreateHook((LPVOID)at, stub, &tramp);
	if (s != MH_OK)
		return snprintf(why, cap, "MH_CreateHook %#llx: %s", (unsigned long long)at, MH_StatusToString(s)), -1;
	VirtualProtect(g_mid_page, 65536, PAGE_READWRITE, &old);
	memcpy(stub + otr, &tramp, 8);
	VirtualProtect(g_mid_page, 65536, PAGE_EXECUTE_READ, &old);
	FlushInstructionCache(GetCurrentProcess(), stub, n);
	g_mid_used += step;
	if ((s = MH_EnableHook((LPVOID)at)) != MH_OK) {
		MH_RemoveHook((LPVOID)at);
		return snprintf(why, cap, "MH_EnableHook %#llx: %s", (unsigned long long)at, MH_StatusToString(s)), -1;
	}
	snprintf(why, cap, "mid %#llx -> stub %p -> %p, then the relocated bytes at %p", (unsigned long long)at,
		 (void *)stub, (void *)cfn, tramp);
	return 0;
}

int somvr_win_hook_remove(uintptr_t at)
{
	MH_STATUS s = MH_DisableHook((LPVOID)at);
	if (s == MH_OK)
		s = MH_RemoveHook((LPVOID)at);
	return s == MH_OK ? 0 : -1;
}
