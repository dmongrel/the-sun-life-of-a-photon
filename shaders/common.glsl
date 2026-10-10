// Shared by every shader: the frame's uniforms, noise, and the cut-away Sun as a solid.
//
// Units are solar radii. The Sun sits at the origin, its rotation axis along +z. "Sun frame" is
// the frame that turns with it; world = RotateZ(sun frame, sunAngle). The layout of Frame must
// match FrameUniforms in src/render/renderer.cpp.

#define PLANET_COUNT   9
#define ERUPTION_COUNT 6

layout(set = 0, binding = 0, std140) uniform Frame {
    mat4 viewProj;
    vec4 camPos;     // xyz: camera; w: loop time, seconds
    vec4 camFwd;     // xyz: forward; w: tan(half the vertical field of view)
    vec4 camRight;   // xyz: right; w: aspect
    vec4 camUp;      // xyz: up
    vec4 viewport;   // width, height, 1/width, 1/height (pixels)
    vec4 render;     // x: exposure, y: focal length in pixels, z: frame number, w: largest point size
    vec4 sun;        // x: the Sun frame's angle, y: wedge half-angle, z: wedge centre azimuth, w: wedge open 0..1
    vec4 sun2;       // x: particle strength, y: photon stage (-1 none), z: Sun radius on screen, pixels,
                     // w: the Sun frame's rate, radians per second
    vec4 planets[PLANET_COUNT];     // xyz: centre, w: radius (Pluto first: see sim/system.cpp)
    vec4 planetSpin[PLANET_COUNT];  // x: axial tilt, y: spin angle, z, w: ring inner, outer (planet radii)
    vec4 eruptions[ERUPTION_COUNT]; // xyz: launch direction (Sun frame), w: age, seconds (< 0: none)
    vec4 eruptInfo[ERUPTION_COUNT]; // x: seed, y: size, z: lifetime, seconds
} frame;

const float PI     = 3.14159265358979;
const float TWO_PI = 6.28318530717959;

float loopTime() { return frame.camPos.w; }

float wrapPi(float a) { return a - TWO_PI * floor((a + PI) / TWO_PI); }

// ---- the layers, as fractions of the solar radius (The Sun References, life-of-a-photon.md) ---

const float CORE_EDGE       = 0.25;
const float TACHOCLINE_BASE = 0.66;
const float CONVECTION_BASE = 0.71;

// The convection rolls drawn on the cut faces: CONV_CELLS rolls from pole to pole, each turning the
// other way from its neighbours, so they rise together in pairs and sink together in pairs.
const float CONV_CELLS = 14.0;
const float ROLL_RATE  = 0.35;  // radians per second

// ---- frames --------------------------------------------------------------------------------

vec3 worldToSun(vec3 p) {
    float c = cos(frame.sun.x), s = sin(frame.sun.x);
    return vec3(c * p.x + s * p.y, -s * p.x + c * p.y, p.z);
}

vec3 sunToWorld(vec3 p) {
    float c = cos(frame.sun.x), s = sin(frame.sun.x);
    return vec3(c * p.x - s * p.y, s * p.x + c * p.y, p.z);
}

// ---- noise ---------------------------------------------------------------------------------

float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

vec3 hash33(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.xxy + p.yxx) * p.zyx);
}

float valueNoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash13(i);
    float n100 = hash13(i + vec3(1, 0, 0));
    float n010 = hash13(i + vec3(0, 1, 0));
    float n110 = hash13(i + vec3(1, 1, 0));
    float n001 = hash13(i + vec3(0, 0, 1));
    float n101 = hash13(i + vec3(1, 0, 1));
    float n011 = hash13(i + vec3(0, 1, 1));
    float n111 = hash13(i + vec3(1, 1, 1));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

float fbm(vec3 p, int octaves) {
    float sum = 0.0, amp = 0.5;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * valueNoise(p);
        p = p * 2.03 + vec3(17.1, 3.7, 9.2);
        amp *= 0.5;
    }
    return sum;
}

// Cellular noise: x = distance to the nearest feature point, y = to the second nearest. The
// granulation is its cells: bright where hot gas wells up, dark lanes (y - x small) where it sinks.
vec2 worley(vec3 p) {
    vec3  i = floor(p);
    vec3  f = fract(p);
    float d1 = 8.0, d2 = 8.0;
    for (int z = -1; z <= 1; ++z)
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x) {
                vec3  g = vec3(x, y, z);
                vec3  o = hash33(i + g);
                vec3  d = g + o - f;
                float dd = dot(d, d);
                if (dd < d1) {
                    d2 = d1;
                    d1 = dd;
                } else if (dd < d2) {
                    d2 = dd;
                }
            }
    return sqrt(vec2(d1, d2));
}

// ---- the cut-away Sun ------------------------------------------------------------------------
//
// The unit sphere less a wedge: every point whose azimuth is within the half-angle of the centre
// line, at any height. All in the Sun frame.

bool inWedge(vec3 q) {
    if (frame.sun.y <= 0.0) return false;
    return abs(wrapPi(atan(q.y, q.x) - frame.sun.z)) < frame.sun.y;
}

bool inSolid(vec3 q) { return dot(q, q) < 1.0 && !inWedge(q); }

// The first point along ro + t rd (Sun frame, rd unit) where the ray enters the cut Sun.
// Returns t, or -1 for a miss. part: 0 the surface, 1 the right-hand face (centre + half),
// 2 the left-hand face.
float sunHit(vec3 ro, vec3 rd, out int part) {
    part = 0;
    float b = dot(ro, rd);
    float c = dot(ro, ro) - 1.0;
    float h = b * b - c;
    if (h < 0.0) return -1.0;
    h = sqrt(h);
    float t0 = -b - h, t1 = -b + h;
    if (t1 < 0.0) return -1.0;
    if (t0 >= 0.0 && !inWedge(ro + rd * t0)) return t0;
    t0 = max(t0, 0.0);
    if (frame.sun.y <= 0.0) return t0;

    // Through the opening: the first face crossing that leads into the solid.
    float best = -1.0;
    for (int k = 0; k < 2; ++k) {
        float phi   = frame.sun.z + (k == 0 ? frame.sun.y : -frame.sun.y);
        vec3  n     = vec3(-sin(phi), cos(phi), 0.0);
        float denom = dot(rd, n);
        if (abs(denom) < 1e-6) continue;
        float t = -dot(ro, n) / denom;
        if (t <= t0 || t >= t1) continue;
        vec3 p = ro + rd * t;
        if (dot(p.xy, vec2(cos(phi), sin(phi))) < 0.0) continue;  // the plane's other half
        if (!inSolid(ro + rd * (t + 1e-4))) continue;
        if (best < 0.0 || t < best) {
            best = t;
            part = k + 1;
        }
    }
    return best;
}

// Whether a point (Sun frame) can be seen from the camera, or the Sun is in the way.
bool sunVisible(vec3 q) {
    vec3  cam = worldToSun(frame.camPos.xyz);
    vec3  d   = q - cam;
    float len = length(d);
    int   part;
    float t = sunHit(cam, d / len, part);
    return t < 0.0 || t > len - 2e-3;
}

// The convection roll a point of a cut face sits in, given its radius r and the angle a above
// the equator in the face's plane. Returns (u, v) across the roll, each about -1..1, and the way
// it turns (+1 or -1) in w.z. u is outward, v along the face.
vec3 rollCoords(float r, float a) {
    float width = PI / CONV_CELLS;
    float k     = floor((a + 0.5 * PI) / width);
    float ac    = -0.5 * PI + (k + 0.5) * width;
    float half_ = 0.5 * (1.0 - CONVECTION_BASE);
    float u     = (r - (CONVECTION_BASE + half_)) / half_;
    float v     = (a - ac) / (0.5 * width);
    float turn  = mod(k, 2.0) < 0.5 ? 1.0 : -1.0;
    return vec3(u, v, turn);
}
