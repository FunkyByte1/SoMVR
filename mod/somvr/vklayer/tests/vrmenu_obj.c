/* vrmenu_obj.c - the preload's `somvr_vrmenu` object for test_layer.c, in a library of its own (like comfort_obj.c): a
 * VR menu scenario dlopens this with RTLD_GLOBAL; every other scenario has no such symbol at all (a preload without
 * the menu). */
#include "../../somvr_vrmenu.h"

__attribute__((visibility("default"))) somvr_vrmenu_shared somvr_vrmenu;
