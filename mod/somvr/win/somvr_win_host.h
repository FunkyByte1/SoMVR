/* somvr_win_host.h - the Windows DLL in two (docs/re-xr-pipeline.md 156, win/README.md 8).
 *
 * xinput9_1_0.dll is the HOST and is never reloaded: the XInput proxy, the exe gate, MinHook, the patch engine's stubs
 * and slots, the bridge to the layer and every object the layer reads (somvr_shared & co.), D3D's discovery (the device,
 * the context, the swap chain, the first Present), a small persistent area. somvr_logic.dll is everything else (somvr.c,
 * the CORE files, the site tables): the host loads it at start the same way in every build, and with
 * SOMVR_DEV_RELOAD=1 again in the world - the swap - without a new game.
 *
 * Every place where the game reaches the logic goes through a host-owned pointer slot: a call/jmp patch's E8/E9 points
 * at a host stub (jmp [rip+slot]), a MinHook entry hook's detour is a host stub, a mid-function detour calls a host stub,
 * a vtable / IAT entry is itself the slot. The logic's write_code and MinHook calls land here; the host installs each
 * patch once (keyed by its address; the SITE name is the logic's log) and afterwards only rewrites its slot. A swap:
 * the new module's init runs staged (its writes kept, nothing changes in the game), then all slots are rewritten at
 * once with the game's threads held; a slot the new module did not claim points back to the original (the call's
 * target, the slot's old pointer, MinHook's trampoline, a no-op for a mid detour). The old module is never unloaded. */
#ifndef SOMVR_WIN_HOST_H
#define SOMVR_WIN_HOST_H
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#ifdef SOMVR_HOST
#define SOMVR_HOST_API __declspec(dllexport)
#else
#define SOMVR_HOST_API __declspec(dllimport)
#endif

/* ---- the patch engine (the logic's write_code; MinHook: somvr_win_hook.h) -------------------------------------- */
/* n bytes at dst on the logic's behalf. Inside a loaded image (the exe, DXVK's DLLs): a rel32 of an E8/E9 (n 4 at
 * dst = the opcode + 1), a pointer slot (n 8, aligned) or other bytes (n <= 16), each recorded; anywhere else (the
 * game's heap) a plain write. 0 = ok, else -1 and err. */
SOMVR_HOST_API int somvr_host_write(uintptr_t dst, const void *src, size_t n, char *err, size_t cap);
/* the bytes at a as the calling module sees them: the original bytes where it has not patched yet (a reload's checks
 * see the game as a fresh boot does), its own patch where it has. Unreadable bytes stay as dst had them. */
SOMVR_HOST_API void somvr_host_view(uintptr_t a, void *dst, size_t n);
/* the patch at `at` is kept when a later module does not claim it (the save guard's slots: never back to the game's) */
SOMVR_HOST_API void somvr_host_keep(uintptr_t at);

/* ---- the persistent area: what must outlive a swap (the D3D objects a module made, ...) ------------------------- */
/* a block by name, zeroed at the first ask (*fresh = 1); the same name and size again: the same block (*fresh = 0); a
 * new size: a new zeroed block (the layout changed - *fresh = 1, logged). NULL only when out of memory. */
SOMVR_HOST_API void *somvr_host_persist(const char *name, size_t size, int *fresh);

/* ---- the swap ----------------------------------------------------------------------------------------------------- */
/* Who may swap (b37, docs/re-xr-pipeline.md 156.10):
 *   - a developer's game (SOMVR_DEV_RELOAD=1, not a user's session): the trigger word, the watched file's new stamp,
 *     and the player's swap below;
 *   - a session the launcher started for the user (SOMVR_USER_SESSION=1, scripts/som-win-session.sh): ONLY the
 *     player's own press - somvr_host_player_swap, with SOMVR_LIVE=on; the word and the file watch are refused there
 *     even with SOMVR_DEV_RELOAD=1. */
/* the trigger word `reload [force] [KEY=VALUE ...]` (the logic's writer thread hands it over): the host's reload
 * thread does it; refused with a log line without SOMVR_DEV_RELOAD=1 or in a user's session */
SOMVR_HOST_API void somvr_host_reload_request(const char *word);
/* the player's own Apply / Undo press (the VR menu's Test page; somvr_live_game.c): swap to the logic module at dll
 * (a full Windows path) at the next quiet moment - never forced, no switch changes. why: the log's reason ("apply
 * patch 3"). 0 = queued (the outcome: the host's RELOAD lines and somvr_host_gen moving by one), -1 = refused at once
 * (logged): neither SOMVR_LIVE=on nor SOMVR_DEV_RELOAD=1, an empty path, or a swap already queued or running */
SOMVR_HOST_API int somvr_host_player_swap(const char *dll, const char *why);
/* the generation of the module now live (0 = the boot's) */
SOMVR_HOST_API int somvr_host_gen(void);

/* ---- the swap's check (b37, docs 156.9) ------------------------------------------------------------------------ */
/* the live module's last stereo frame, written by the game thread at each one's end into the persistent block
 * SOMVR_WHERE_BLOCK (seqlock: seq odd while written). The mapping from the tracked space into the world: world =
 * O + s R(yaw) m(p) (somvr_eyes.h: O = B - s R P0, yaw = psi - yaw0). The host reads it just before a swap and after it:
 * the new module's first frame against the old one's last (their motion taken out) - `RELOAD view moved` past 1 deg /
 * 2 cm. */
#define SOMVR_WHERE_BLOCK "view.where"
struct somvr_where {
	_Atomic uint64_t seq;
	uint64_t frame;      /* the renderer's frame */
	int gen;             /* the module that wrote it */
	int have_talion;
	float o[3], yaw;     /* the mapping: its origin (cm) and its heading (rad) */
	float scale;         /* s (1 = 1:1) */
	float base[3], psi;  /* the frame's base B and heading psi, after every stage (bodymoments, the size, ...) */
	float talion[3], talion_yaw; /* Talion's root (cm) and his heading (rad) */
	double t;            /* the module's clock at the frame (QPC seconds) */
};

/* ---- the logic's entry points (somvr_logic.dll exports them; the host finds them by name) ----------------------- */
struct somvr_logic_boot {
	uint32_t size;
	int reason;      /* 0 = the boot (the main thread before the game's entry), 1 = a reload (in the world) */
	int gen;         /* 0 at the boot, then 1, 2, ... */
	int test;        /* the stand-in (SOMVR_WIN_TEST=1): every SITE blank */
	int sites_off;   /* SOMVR_WIN_SITES=0 or the stand-in: every SITE blank */
	uintptr_t img;   /* the exe's base */
	const char *sha; /* the exe's sha256 (hex) */
	const char *tag; /* its tag (the tables' exe), "test" for the stand-in */
	const char *who; /* the gate's line for START */
};
#define SOMVR_LOGIC_ABI 1
/* the first export: the logic's ABI (SOMVR_LOGIC_ABI) - another number is refused */
typedef int (*somvr_logic_abi_fn)(void);
/* the switches, the tables, the checks, the hooks, the writer thread (somvr_start); 0 = ok */
typedef int (*somvr_logic_init_fn)(const struct somvr_logic_boot *b);
/* a D3D11/DXGI interface's vtable for the COM rows ("ID3D11Device", "ID3D11DeviceContext", "IDXGISwapChain",
 * "ID3D11Texture2D"); returns the rows filled */
typedef int (*somvr_logic_com_fn)(const char *iface, void **vtbl);
/* the first Present's work (H8's vtables, the D3D sites, H9): once the four interfaces are filled */
typedef void (*somvr_logic_present_fn)(void);
/* 1 = quiet (nothing of the game held in a state only this module knows how to end); else 0 and why */
typedef int (*somvr_logic_idle_fn)(char *why, size_t cap);
/* phase 0: before the new module's init - its threads ended and joined, its log flushed; phase 1: after the swap -
 * retired (no more log lines, no END lines at the exit; its code stays mapped for a thread still inside it) */
typedef void (*somvr_logic_stop_fn)(int phase);
/* the stand-in's hook test (the WRAPPER hooks' Win64 reads) */
typedef int (*somvr_logic_abitest_fn)(char *out, size_t cap);
#endif
