#version 450

// A particle is a small Gaussian, added into the HDR scene.

layout(location = 0) in vec3 vColor;
layout(location = 0) out vec4 outColor;

void main() {
    vec2  d  = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(d, d);
    if (r2 > 1.0) discard;
    outColor = vec4(vColor * exp(-r2 * 4.0) * 1.4, 0.0);
}
