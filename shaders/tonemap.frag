#version 450
#extension GL_GOOGLE_include_directive : require

// HDR scene -> screen: bloom from the scene's mip chain, a filmic curve, sRGB encoding, dither.

#include "common.glsl"

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform sampler2D scene;

// ACES fitted curve (Narkowicz 2015): gentle shoulder that keeps the core from clipping to white.
vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

vec3 toSrgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

void main() {
    vec3 hdr = textureLod(scene, vUV, 0.0).rgb;

    // Wide, soft glow: each coarser mip spreads light further.
    float levels = float(textureQueryLevels(scene));
    vec3  bloom = vec3(0.0);
    float weight = 0.0;
    for (float lod = 1.0; lod < 7.0; lod += 1.0) {
        if (lod >= levels) break;
        float w = 1.0 / lod;
        bloom += textureLod(scene, vUV, lod).rgb * w;
        weight += w;
    }
    if (weight > 0.0) hdr += bloom / weight * 0.35;

    vec3 c = aces(hdr * frame.render.x);

    // A light vignette keeps the eye on the middle.
    vec2 v = vUV - 0.5;
    c *= 1.0 - 0.25 * dot(v, v) * 2.0;

    c = toSrgb(c);
    // Dither before 8-bit quantisation, or the dark gradients band.
    float n = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    c += (n - 0.5) / 255.0;
    outColor = vec4(c, 1.0);
}
