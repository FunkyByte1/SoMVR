#version 450
// somvr_handdraw.c: a soft, wrapped key light, a sky/ground ambient, a rim and a little sheen; premultiplied alpha
// (the layer is blended BLEND_TEXTURE_SOURCE_ALPHA without UNPREMULTIPLIED: color = rgb * a).
// WORLD_DEPTH (build 11, SOMVR_XR_HANDS_DEPTH=world; build_spv.sh compiles this file twice): the game's depth of this
// eye hides the hand where it goes into the world. The game's depth is reversed Z with an infinite far plane (depth =
// near / z), so its view distance in XR metres is eye.w / depth (eye.w = depth_k; 0 = no world depth this frame); ours
// is 1 / gl_FragCoord.w (the clip w of an OpenXR projection = the distance along the eye's forward). The edge is soft:
// SOFT metres wide, centred BIAS metres behind the world's surface (drawn up to the surface, gone SOFT behind it).
layout(location = 0) in vec3 v_nrm;
layout(location = 1) in vec3 v_view;
layout(location = 2) in float v_a;
layout(push_constant) uniform PC {
    mat4 vp;
    vec4 eye;
    vec4 light;
    vec4 base;
    vec4 params;
} pc;
layout(location = 0) out vec4 o_col;
#ifdef WORLD_DEPTH
layout(set = 0, binding = 0) uniform sampler2D world; // this eye's game depth (or a 1x1 zero: no world)
layout(constant_id = 0) const float SOFT = 0.015;     // the edge's width (m)
layout(constant_id = 1) const float BIAS = 0.0075;    // its centre behind the surface (m)
layout(constant_id = 2) const float TEXEL = 1.0;      // the game depth's pixels per pixel of ours
layout(constant_id = 3) const int DEBUG = 0;          // 1: the hand shows (its distance - the world's): R 0.5 +- 10 cm
#endif
void main() {
    if (pc.base.a < 0.5) { // an occluder: nothing drawn, its depth kept (the hands behind it are hidden)
        o_col = vec4(0.0);
        return;
    }
    vec3 n = normalize(v_nrm);
    vec3 v = normalize(v_view);
    if (dot(n, v) < 0.0) n = -n; // the inside of the wrist's opening
    vec3 l = pc.light.xyz;
    float wrap = clamp((dot(n, l) + 0.35) / 1.35, 0.0, 1.0);
    float hemi = 0.5 + 0.5 * n.y;
    vec3 amb = mix(vec3(0.55, 0.50, 0.45), vec3(0.85, 0.88, 0.95), hemi) * pc.params.y;
    vec3 col = pc.base.rgb * (amb + vec3(1.0, 0.96, 0.9) * wrap * wrap * pc.params.x);
    float rim = pow(1.0 - clamp(dot(n, v), 0.0, 1.0), 3.0) * pc.params.z;
    vec3 h = normalize(l + v);
    float spec = pow(clamp(dot(n, h), 0.0, 1.0), 20.0) * pc.params.w;
    col += pc.base.rgb * rim + vec3(spec);
    float a = clamp(v_a, 0.0, 1.0) * pc.light.w;
#ifdef WORLD_DEPTH
    if (pc.eye.w > 0.0) {
        ivec2 last = textureSize(world, 0) - ivec2(1);
        float d = texelFetch(world, clamp(ivec2(gl_FragCoord.xy * TEXEL), ivec2(0), last), 0).r;
        // a hand holding a weapon (base.a > 1): the world's depth read as Talion's held items are drawn (their depth
        // as if the near plane were base.a times the eyes'), so the hilt it holds sits where the eyes see it
        float zw = d > 0.0 ? pc.eye.w * max(pc.base.a, 1.0) / d : 0.0;
        float t = 1.0;
        if (d > 0.0) { // 0: the sky (infinitely far)
            t = clamp((zw + BIAS + 0.5 * SOFT - 1.0 / gl_FragCoord.w) / SOFT, 0.0, 1.0);
            a *= t * t * (3.0 - 2.0 * t);
        }
        if (DEBUG != 0) { // the measure, not the look: opaque, the depth difference in red
            float dz = d > 0.0 ? 1.0 / gl_FragCoord.w - zw : 1.0;
            o_col = vec4(clamp(0.5 + dz / 0.2, 0.0, 1.0), d > 0.0 ? 0.0 : 1.0, t, 1.0) * clamp(v_a, 0.0, 1.0);
            return;
        }
    }
#endif
    o_col = vec4(col * a, a);
}
