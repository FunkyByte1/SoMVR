/* somvr_win_hostint.h - the host's own files among themselves (not the logic's): the patch engine's swap
 * (somvr_win_hook.c) for the reload (somvr_win_host.c), D3D's discovery (somvr_win_d3d.c) for both */
#ifndef SOMVR_WIN_HOSTINT_H
#define SOMVR_WIN_HOSTINT_H
#include <stddef.h>
#include <stdint.h>
#include "somvr_win_host.h"

struct hk_commit_stats {
	int threads, claimed, released, kept, added, failed, pages;
	double freeze_ms;
};
void hk_stage_begin(int gen, uintptr_t lo, uintptr_t hi); /* the staged module's image: its writes are kept */
int hk_commit(struct hk_commit_stats *st);                 /* the swap's stores, the threads held */
void hk_stage_abort(void);
int hk_gen(void);
void hk_stats(char *out, size_t cap);

/* the live logic module's entry points (somvr_win_host.c) */
struct host_logic {
	void *mod;
	uintptr_t lo, hi;
	int gen;
	somvr_logic_init_fn init;
	somvr_logic_com_fn com;
	somvr_logic_present_fn present;
	somvr_logic_idle_fn idle;
	somvr_logic_stop_fn stop;
	somvr_logic_abitest_fn abitest;
};
void somvr_win_log_warn(const char *fmt, ...) __attribute__((format(printf, 1, 2))); /* somvr_win_main.c: a bare "WARN ..." line (the runs' gate counts it) */
extern struct host_logic *host_live; /* NULL until the boot's module is loaded */
int host_module_of(uintptr_t a, uintptr_t *base); /* the logic module's generation an address lies in, else -1 */

/* D3D's discovery (somvr_win_d3d.c): what a new module is told at a swap */
void d3d_replay(const struct host_logic *L); /* the four vtables met so far, then the first Present's work if it ran */
void d3d_present_stats(double *max_ms, uint64_t *n); /* the frame interval's max since the last call, the presents */
#endif
