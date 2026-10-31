/* somvr_map.h - build 27 (som-b27-world; docs/re-xr-pipeline.md 111): the world map by the laser and the fingertip
 * (SOMVR_MAP_POINT). Pure: no globals, no game reads. Tested by tests/test_map.c; somvr_map_game.c reads the game and
 * applies the results.
 *
 * The game's map (CUIPDAZoneMapLayer, the right stick's click) is the pad's only: a fixed cross in the middle of the
 * screen, the left stick pans the map under it (MapMoveX/Y 59/60), whatever is under the cross is selected (its name top
 * right; at a forge tower the popup "(X) Fast Travel / (Y) Advance Time" up-left of the cross), A sets or clears the
 * waypoint there (MenuEnter 54 + MapSelect 65), X fast travels (MenuAux6 87), Y advances the time (MenuAux7 88). A KBM
 * mouse pans by a left drag and sets the waypoint by the right button (FUN_00bf34d0's bindings), but in the pad's profile
 * no mouse event reaches it: boot 20261030-073117 - the laser's hover, its click (GFx) and Mouse1 held moved nothing.
 *
 * So the pointer drives the pad's own commands:
 * - a press on the map = "bring this place to the cross": at once the map pans by the stick's command (full deflection
 *   toward it) for the time the game's pan takes to cover that distance (pan_pxs, lag_s); the place's name and its
 *   prompts come up as the game's selection (it also snaps the cross onto an icon near it);
 * - a click on the cross itself (within centre_px) = A: the waypoint set (or cleared) there;
 * - a click on the forge tower popup's rows (only while the selection under the cross is a travel point) = X (Fast
 *   Travel) or Y (Advance Time);
 * - a press still held after its pan and click_s steers: the map keeps moving toward where the pointer is, at full
 *   speed steer_px away from the cross, slower nearer (the player's eye closes the loop) until it is let go;
 * - a new press cancels a pan under way. Nothing moves the view: the map is on the menu board. */
#ifndef SOMVR_MAP_H
#define SOMVR_MAP_H

#include <stdint.h>

/* the game's commands */
#define SOMVR_MAP_CMD_ENTER 54   /* MenuEnter: the waypoint prompt's binding (FUN_00bf34d0: 0x36) */
#define SOMVR_MAP_CMD_MOVEX 59   /* MapMoveX / MapMoveY: the pan (the left stick's in the map) */
#define SOMVR_MAP_CMD_MOVEY 60
#define SOMVR_MAP_CMD_SELECT 65  /* MapSelect: the pad's A sends it with MenuEnter */
#define SOMVR_MAP_CMD_TRAVEL 87  /* MenuAux6: Fast Travel (CUIPDAZoneMapLayer::vf_10 0x57) */
#define SOMVR_MAP_CMD_TIME 88    /* MenuAux7: Advance Time (vf_10 0x58) */

/* the tower popup's rows, UI pixels from the cross (+x right, +y down), measured on the 1920 x 1080 UI (boot
 * 20261030-073117's map: "(X) Fast Travel" 425..465, "(Y) Advance Time" 466..505, the box 850..1105, the cross 960,537) */
#define SOMVR_MAP_POP_X0 (-112.0f)
#define SOMVR_MAP_POP_X1 (148.0f)
#define SOMVR_MAP_POP_Y0 (-118.0f)
#define SOMVR_MAP_POP_YM (-76.0f)
#define SOMVR_MAP_POP_Y1 (-34.0f)

typedef struct {
    int on;          /* SOMVR_MAP_POINT=on */
    float pan_pxs;   /* the game's pan at MapMoveX/Y 1.0: UI pixels a second */
    float lag_s;     /* the pan's start: added to every planned pan */
    float steer_px;  /* a held press: full speed this far from the cross */
    float click_s;   /* a press held this long (and its pan done) steers */
    float centre_px; /* a click this near the cross = A */
    float min_px;    /* a click nearer than this to the cross plans no pan (but A wins within centre_px) */
    float ysign;     /* MapMoveY for a place above the cross (UI y < 0): +1 = up is positive */
} somvr_map_cfg;
void somvr_map_cfg_default(somvr_map_cfg *c);
/* "on" / "off" [",PXS,LAG_MS,STEER_PX"]; unset = off. 0 = understood, -1 = not (off) */
int somvr_map_parse(const char *v, somvr_map_cfg *c);

enum { SOMVR_MAPR_PAN, SOMVR_MAPR_A, SOMVR_MAPR_X, SOMVR_MAPR_Y };
extern const char *const somvr_map_region_names[4];
/* what a click at px (UI pixels from the cross) means; travel = the popup is up (a travel point under the cross) */
int somvr_map_region(const somvr_map_cfg *c, const float px[2], int travel);

enum { SOMVR_MAPP_IDLE, SOMVR_MAPP_DOWN, SOMVR_MAPP_STEER, SOMVR_MAPP_PAN, SOMVR_MAPP_PRESS };
extern const char *const somvr_map_phase_names[5];

typedef struct {
    int phase;
    double t0;          /* the phase's start */
    double t_down;      /* the last press's start */
    float down[2];      /* the press's point (UI px from the cross) */
    float dir[2];       /* a planned pan: the unit direction (UI px axes) */
    double end;         /* a planned pan's or a button's end */
    int cmd[2], ncmd;   /* the button pressed (PRESS) */
    int region;         /* the last click's region */
    float plan_px, plan_s; /* the last pan's distance and time */
    uint64_t clicks, pans, cancels, steers, a, x, y, ticks_axis;
} somvr_map_st;
void somvr_map_init(somvr_map_st *s);

typedef struct {
    int map;            /* the zone map is the top menu layer */
    int ptr;            /* the pointer (laser or fingertip) is on the board: px is valid */
    int pressing;       /* its press is held now */
    int down;           /* a press began this tick */
    float px[2];        /* the pointer's point, UI pixels from the cross */
    int travel;         /* the selection under the cross is a travel point (the popup's rows answer) */
    double t;
} somvr_map_in;

enum { SOMVR_MAPE_NONE, SOMVR_MAPE_CLICK, SOMVR_MAPE_STEER_ON, SOMVR_MAPE_STEER_OFF, SOMVR_MAPE_PAN_END,
       SOMVR_MAPE_CANCEL, SOMVR_MAPE_CLOSED };
typedef struct {
    int naxis;          /* 2: MapMoveX/Y carry axis[] this tick */
    float axis[2];
    int ncmd, cmd[2];   /* commands held this tick */
    int event;          /* SOMVR_MAPE_* (logs) */
} somvr_map_out;
void somvr_map_step(somvr_map_st *s, const somvr_map_cfg *c, const somvr_map_in *in, somvr_map_out *o);

#endif
