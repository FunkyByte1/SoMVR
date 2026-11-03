/* somvr_regs.h - the register frame somvr.c's asm WRAPPER builds for a hook's C side, and the game's integer
 * arguments in it on either ABI (docs/re-winport.md 7.1, 7.8; win/README.md 6).
 *
 * The wrapper saves every call-clobbered register (and xmm0-15) into struct regs and calls the C side with a pointer
 * to it; the C side may change them (rax = the result). Linux (SysV): arguments 1-6 in rdi rsi rdx rcx r8 r9. Windows
 * (Win64, COFF wrapper): 1-4 in rcx rdx r8 r9, then the caller's stack - the wrapper stores its entry rsp ([rsp] = the
 * return address) at +0x150, argument 5 is at entry rsp + 0x28 (past the 32-byte home area), 6 at +0x30, 7 at +0x38.
 * Floats are positional on Win64 (argument k in xmm(k-1), its integer register unused); a member returning a struct
 * takes (this, hidden out, ...) on MSVC, (hidden out, this, ...) on Itanium. Both families are always defined (the
 * tests check them on Linux); RA1..RA7 are the build's own. */
#ifndef SOMVR_REGS_H
#define SOMVR_REGS_H
#include <stddef.h>
#include <stdint.h>

struct regs {
	uint64_t rax, rcx, rdx, rsi, rdi, r8, r9, r10, r11, ra; /* ra = the game's return address */
	float xmm[16][4];
};
_Static_assert(offsetof(struct regs, ra) == 0x48, "regs.ra");
_Static_assert(offsetof(struct regs, xmm) == 0x50, "regs.xmm");
_Static_assert(sizeof(struct regs) == 0x150, "regs size");

#define RA_SYSV1(r) ((r)->rdi)
#define RA_SYSV2(r) ((r)->rsi)
#define RA_SYSV3(r) ((r)->rdx)
#define RA_SYSV4(r) ((r)->rcx)
#define RA_SYSV5(r) ((r)->r8)
#define RA_SYSV6(r) ((r)->r9)

#define somvr_win_regs_rsp(r) (*(const uint64_t *)((const char *)(r) + 0x150)) /* the COFF wrapper's entry rsp */
#define RA_WIN1(r) ((r)->rcx)
#define RA_WIN2(r) ((r)->rdx)
#define RA_WIN3(r) ((r)->r8)
#define RA_WIN4(r) ((r)->r9)
#define RA_WIN5(r) (*(const uint64_t *)(somvr_win_regs_rsp(r) + 0x28))
#define RA_WIN6(r) (*(const uint64_t *)(somvr_win_regs_rsp(r) + 0x30))
#define RA_WIN7(r) (*(const uint64_t *)(somvr_win_regs_rsp(r) + 0x38))
/* argument k >= 5 by number (winport-stereo, docs/re-winport.md 7.7): [entry rsp + 8k]; a float there is the slot's low
 * 4 bytes (RA_WIN5 = RA_STK(r, 5)) */
#define RA_STK(r, k) (((const uint64_t *)somvr_win_regs_rsp(r))[k])
#define RF_STK(r, k) (*(const float *)&((const uint64_t *)somvr_win_regs_rsp(r))[k])

#ifdef _WIN32
#define RA1 RA_WIN1
#define RA2 RA_WIN2
#define RA3 RA_WIN3
#define RA4 RA_WIN4
#define RA5 RA_WIN5
#define RA6 RA_WIN6
#define RA7 RA_WIN7
#else
#define RA1 RA_SYSV1
#define RA2 RA_SYSV2
#define RA3 RA_SYSV3
#define RA4 RA_SYSV4
#define RA5 RA_SYSV5
#define RA6 RA_SYSV6
#endif
#endif
