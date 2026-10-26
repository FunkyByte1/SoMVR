/* hud_obj.c - the preload's `somvr_hud` object for test_layer.c, in a library of its own (like input_obj.c): a HUD
 * scenario dlopens this with RTLD_GLOBAL; every other scenario has no such symbol at all (a preload without H11). */
#include "../../somvr_hud.h"

__attribute__((visibility("default"))) somvr_hud_shared somvr_hud;
