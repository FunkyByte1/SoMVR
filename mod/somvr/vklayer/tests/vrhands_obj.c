/* vrhands_obj.c - the preload's `somvr_vrhands` object for test_layer.c, in a library of its own (like comfort_obj.c):
 * a VR hands scenario dlopens this with RTLD_GLOBAL; every other scenario has no such symbol at all (a preload without
 * our hands). */
#include "../../somvr_vrhands.h"

__attribute__((visibility("default"))) somvr_vrhands_shared somvr_vrhands;
