#version 450

// Text takes its coverage from the Michroma atlas; lines are anti-aliased from their distance to
// the centre line, interpolated across the quad. Dashed lines (the photon's crawling ants) also
// cut the line into dashes by their phase along it, which the CPU moves every frame. Soft lines
// (the dark band under the ants) fade out towards their edges.

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec4 vLine;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 3) uniform sampler2D textAtlas;

const float DASH = 0.58;  // of each period, the part drawn

void main() {
    float coverage;
    if (vLine.x > 0.5) {
        coverage = clamp(vLine.y + 0.5 - abs(vLine.z), 0.0, 1.0);
        if (vLine.x > 2.5) {  // soft: round ends, the outer w pixels all round fading out
            float beyond = max(max(-vUV.x, vUV.x - vUV.y), 0.0);
            float d      = length(vec2(beyond, vLine.z));
            float t      = clamp((vLine.y - d) / max(vLine.w, 1e-3), 0.0, 1.0);
            coverage = t * t * (3.0 - 2.0 * t);
        } else if (vLine.x > 1.5) {
            float phase = fract(vLine.w);
            float aa    = max(fwidth(vLine.w), 1e-4);
            float on    = smoothstep(0.0, aa, phase) * (1.0 - smoothstep(DASH - aa, DASH, phase));
            coverage *= on;
        }
    } else {
        coverage = texture(textAtlas, vUV).r;
    }
    outColor = vec4(vColor.rgb, vColor.a * coverage);
}
