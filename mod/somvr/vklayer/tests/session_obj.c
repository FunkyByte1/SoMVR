/* session_obj.c - the preload's `somvr_session` object for test_layer.c, in a library of its own (like vrmenu_obj.c): the
 * theatre scenario dlopens this with RTLD_GLOBAL; every other scenario has no such symbol (a preload without it). */
#include "../../somvr_session.h"

__attribute__((visibility("default"))) somvr_session_shared somvr_session;
