/* objective_obj.c - the preload's `somvr_objective` object for test_layer.c, in a library of its own (like
 * threat_obj.c): an objective scenario dlopens this with RTLD_GLOBAL; every other scenario has no such symbol at all
 * (a preload without the objective marker). */
#include "../../somvr_objective.h"

__attribute__((visibility("default"))) somvr_objective_shared somvr_objective;
