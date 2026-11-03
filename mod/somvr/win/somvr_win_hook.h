/* somvr_win_hook.h - the Windows patch engine's hooks beyond somvr.c's call/slot patches (win/README.md 6):
 * entry trampolines and mid-function detours, both through MinHook 1.3.4 (third_party/minhook-1.3.4, BSD-2-Clause:
 * its instruction decoder relocates the overwritten instructions into the trampoline - no hand-written relocator). */
#ifndef SOMVR_WIN_HOOK_H
#define SOMVR_WIN_HOOK_H
#include <stddef.h>
#include <stdint.h>

/* the registers a mid-function detour's C function sees and may change; the first 0x150 bytes are somvr.c's
 * struct regs (WRAPPER's), then the callee-saved ones, the flags and the stack pointer at the detour point */
struct somvr_win_regs {
	uint64_t rax, rcx, rdx, rsi, rdi, r8, r9, r10, r11, ra; /* ra: the qword at [rsp] (no return address here) */
	float xmm[16][4];
	uint64_t rbx, rbp, r12, r13, r14, r15, rflags, rsp;    /* rsp: read only */
};
_Static_assert(offsetof(struct somvr_win_regs, xmm) == 0x50 && offsetof(struct somvr_win_regs, rbx) == 0x150,
	       "somvr_win_regs = struct regs + the rest");

/* calls of the game function at fn (absolute) go to hook (same prototype, Win64); *orig = the original (a trampoline
 * MinHook built within +-2 GB). Enabled at once (MinHook suspends the other threads while it writes). 0 = ok, else
 * -1 and why. */
int somvr_win_hook_entry(uintptr_t fn, void *hook, void **orig, char *why, size_t cap);
/* a detour at the instruction boundary at (absolute; the flags and every register as they are there): cfn runs
 * with the registers (it may change them, rsp aside), then the overwritten instructions (relocated) and the code
 * after them. The porter picks a point no branch jumps into within its first 5 bytes. 0 = ok. */
int somvr_win_hook_mid(uintptr_t at, void (*cfn)(struct somvr_win_regs *), char *why, size_t cap);
/* undo one (entry or mid) hook; 0 = ok */
int somvr_win_hook_remove(uintptr_t at);
#endif
