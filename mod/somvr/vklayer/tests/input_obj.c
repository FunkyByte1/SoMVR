/* input_obj.c - the preload's `somvr_input` object for test_layer.c, in a library of its own: a scenario that wants it
 * dlopens this with RTLD_GLOBAL (like the LD_PRELOAD library, it is then in the global scope the layer's
 * dlsym(RTLD_DEFAULT) searches); every other scenario has no such symbol at all (a v2 preload). */
#include "../../somvr_input.h"

__attribute__((visibility("default"))) somvr_input_shared somvr_input;
