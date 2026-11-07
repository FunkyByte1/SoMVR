/* zprobe_obj.c - build 41: the preload's `somvr_zprobe` object for test_layer.c (as vrhands_obj.c): the depth probe's
 * scenario dlopens this with RTLD_GLOBAL; every other scenario has no such symbol (a preload without the probe). */
#include "../../somvr_zprobe.h"

__attribute__((visibility("default"))) somvr_zprobe_shared somvr_zprobe;
