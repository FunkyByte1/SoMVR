/* threat_obj.c - the preload's `somvr_threat` object for test_layer.c, in a library of its own (like comfort_obj.c): a
 * threat scenario dlopens this with RTLD_GLOBAL; every other scenario has no such symbol at all (a preload without
 * the threat cues). */
#include "../../somvr_threat.h"

__attribute__((visibility("default"))) somvr_threat_shared somvr_threat;
