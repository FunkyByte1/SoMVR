#version 450
// somvr_handdraw.c: the VR hands (docs/re-xr-pipeline.md 33). XR-space vertices (skinned on the CPU), one eye's
// clip-from-XR matrix; an occluder (a quad of the wrist panel or the watch) has alpha 0: depth only.
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_nrm;
layout(location = 2) in float in_a;
layout(push_constant) uniform PC {
    mat4 vp;      // clip <- XR space (Vulkan clip: y down, z 0..1)
    vec4 eye;     // xyz: the eye in XR space
    vec4 light;   // xyz: toward the key light (unit, XR space); w: the hand's opacity
    vec4 base;    // rgb: the albedo (linear); a: 1 = shaded, 0 = an occluder (writes depth only)
    vec4 params;  // x: key, y: ambient, z: rim, w: specular
} pc;
layout(location = 0) out vec3 v_nrm;
layout(location = 1) out vec3 v_view;
layout(location = 2) out float v_a;
void main() {
    gl_Position = pc.vp * vec4(in_pos, 1.0);
    v_nrm = in_nrm;
    v_view = pc.eye.xyz - in_pos;
    v_a = in_a;
}
