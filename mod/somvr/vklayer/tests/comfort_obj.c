/* comfort_obj.c - the preload's `somvr_comfort` object for test_layer.c, in a library of its own (like hud_obj.c): a
 * comfort scenario dlopens this with RTLD_GLOBAL; every other scenario has no such symbol at all (a preload without
 * the vignette). */
#include "../../somvr_comfort.h"

__attribute__((visibility("default"))) somvr_comfort_shared somvr_comfort;
