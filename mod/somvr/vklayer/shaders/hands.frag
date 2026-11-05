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
// build 40 (som-b40-herb, docs/re-xr-pipeline.md 176): the pick outline (params.w < 0). The plant is a cylinder: its
// foot light.xyz (XR m, Y up), params.y round, params.z high; a disc over it marks the pixels to look at. A pixel whose
// ray passes the cylinder looks at the game's depth round it (16 rays, 4 steps out to params.x of our pixels): each
// neighbour's depth back into the world along this pixel's ray - a neighbour inside the cylinder (the plant, not the
// grass beside it) with this pixel more than gap = -params.w farther behind it is the plant's silhouette edge: drawn
// base.rgb, soft outward, x the disc's alpha x light.w. Never hidden by the world's depth (it is the world's own edge).
float wz(ivec2 p, ivec2 last) {
    float d = texelFetch(world, clamp(p, ivec2(0), last), 0).r;
    return d > 0.0 ? pc.eye.w / d : 1.0e6; // (0: the sky, infinitely far)
}
void outline() {
    float a = clamp(v_a, 0.0, 1.0) * pc.light.w;
    vec3 foot = pc.light.xyz, dr = normalize(-v_view); // this pixel's ray from the eye
    float R = pc.params.y, H = pc.params.z, gap = -pc.params.w;
    // the ray's nearest pass by the plant's axis (in the ground plane) while it is within the plant's height
    float tlo = 0.0, thi = 1.0e3;
    if (abs(dr.y) > 1.0e-4) {
        float t1 = (foot.y - 0.05 - pc.eye.y) / dr.y, t2 = (foot.y + H + 0.05 - pc.eye.y) / dr.y;
        tlo = max(min(t1, t2), 0.0), thi = max(t1, t2);
    } else if (pc.eye.y < foot.y - 0.05 || pc.eye.y > foot.y + H + 0.05) {
        o_col = vec4(0.0);
        return;
    }
    vec2 v = foot.xz - pc.eye.xz, dd = dr.xz;
    float ll = dot(dd, dd), t = ll > 1.0e-8 ? clamp(dot(v, dd) / ll, tlo, thi) : tlo;
    if (thi < tlo || length(dd * t - v) > R + 0.06 || a <= 0.0) { // nowhere near the plant: nothing (no depth read)
        o_col = vec4(0.0);
        return;
    }
    if (pc.eye.w <= 0.0) { // no world depth this frame: a faint glow (where, not what)
        o_col = vec4(pc.base.rgb, 1.0) * (0.18 * a);
        return;
    }
    vec3 f = vec3(pc.vp[0][3], pc.vp[1][3], pc.vp[2][3]); // clip w = the distance along the eye's forward
    float w0 = dot(f, pc.eye.xyz) + pc.vp[3][3], fd = max(dot(f, dr), 1.0e-3);
    ivec2 last = textureSize(world, 0) - ivec2(1);
    vec2 c = gl_FragCoord.xy * TEXEL;
    float z0 = wz(ivec2(c), last), w = max(pc.params.x, 1.0) * TEXEL, best = 0.0;
    for (int i = 0; i < 16; i++) {
        vec2 dir = vec2(cos(0.39269908 * float(i)), sin(0.39269908 * float(i)));
        for (int r = 1; r <= 4; r++) {
            float zn = wz(ivec2(c + dir * (w * float(r) * 0.25)), last);
            if (!(z0 - zn > gap)) continue;
            vec3 q = pc.eye.xyz + dr * ((zn - w0) / fd) - foot; // (a few pixels off this ray: < 2 cm at 2 m)
            if (length(q.xz) < R && q.y > 0.04 && q.y < H) {
                best = max(best, 1.0 - 0.22 * float(r - 1));
                break;
            }
        }
    }
    a *= best;
    o_col = vec4(pc.base.rgb * a, a);
}
#endif
void main() {
#ifdef WORLD_DEPTH
    if (pc.params.w < 0.0) {
        outline();
        return;
    }
#endif
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
