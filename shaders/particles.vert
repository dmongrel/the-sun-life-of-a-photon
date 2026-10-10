#version 450
#extension GL_GOOGLE_include_directive : require

// The Sun's swirling particles: 790,000 of them, the Milky Way saver's count. None is stored. Each
// is placed from its index and the loop time alone, so every frame (and every capture) of a given
// moment is the same.
//
//   [0, SURFACE)              the surface: granules turning, the whole skin sheared by the
//                             differential rotation
//   [.., + FACE)              the cut faces: the core's flicker, the radiative zone's slow
//                             outward seep, the convection rolls
//   [.., + LOOPS)             coronal loops arching over active regions, gas sliding along them
//   [.., + CME)               coronal mass ejections: a bright expanding front round a dark
//                             cavity, with the erupted filament's dense core inside
//
// A particle the cut Sun hides from the camera is dropped.

#include "common.glsl"

const uint SURFACE = 420000u;
const uint FACE    = 250000u;
const uint LOOPS   = 30000u;
const uint CME     = 90000u;  // ERUPTION_COUNT x 15,000
const uint PER_CME = CME / uint(ERUPTION_COUNT);
const uint LOOP_COUNT = 30u;

layout(location = 0) out vec3 vColor;

void cull() {
    gl_Position  = vec4(2.0, 2.0, 2.0, 1.0);
    gl_PointSize = 1.0;
    vColor       = vec3(0.0);
}

uint pcg(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Uniform [0, 1) values for particle `id`, channel `k`.
float rnd(uint id, uint k) { return float(pcg(id * 8u + k) >> 8) * (1.0 / 16777216.0); }

vec3 sphere(float lat, float lon) { return vec3(cos(lat) * cos(lon), cos(lat) * sin(lon), sin(lat)); }

// A point on the face at azimuth phi, from (r, a), lifted a hair into the open wedge.
vec3 facePoint(float phi, float r, float a, float side) {
    vec3 out_   = vec3(cos(phi), sin(phi), 0.0);
    vec3 normal = vec3(sin(phi), -cos(phi), 0.0) * side;
    return out_ * (r * cos(a)) + vec3(0.0, 0.0, r * sin(a)) + normal * 0.006;
}

// Whether a planet or a moon stands between the camera and this point: Mercury crossing the
// Sun hides the particles behind it, as the ray-traced disk does.
bool blocks(vec4 s, vec3 o, vec3 d, float len) {
    vec3  oc = o - s.xyz;
    float b  = dot(oc, d);
    float h  = b * b - (dot(oc, oc) - s.w * s.w);
    if (h < 0.0) return false;
    float t = -b - sqrt(h);
    return t > 0.0 && t < len;
}

bool behindBody(vec3 world) {
    vec3  o   = frame.camPos.xyz;
    vec3  v   = world - o;
    float len = length(v);
    vec3  d   = v / len;
    for (int i = 0; i < PLANET_COUNT; ++i)
        if (blocks(frame.planets[i], o, d, len)) return true;
    for (int k = 0; k < MOON_COUNT; ++k)
        if (blocks(frame.moons[k], o, d, len)) return true;
    return false;
}

float relativeRate(float sinLat) {
    float s2 = sinLat * sinLat;
    return frame.sun2.w * (-2.39 * s2 - 1.78 * s2 * s2) / 14.71;
}

void main() {
    uint  id = uint(gl_VertexIndex);
    float t  = loopTime();
    vec3  q;            // Sun frame
    vec3  color;
    float size = 1.0;   // relative sprite size

    if (id < SURFACE) {
        // Each particle lives FLOW seconds, drifting with its latitude's rotation and circling
        // in its granule, then is born again where it began: the shear shows, and never winds up.
        const float FLOW = 36.0;
        float life = fract(t / FLOW + rnd(id, 0u));
        float age  = life * FLOW;
        float z    = rnd(id, 1u) * 2.0 - 1.0;
        float lat  = asin(z);
        float lon  = rnd(id, 2u) * TWO_PI + relativeRate(z) * age;
        vec3  n    = sphere(lat, lon);
        vec3  east = normalize(vec3(-sin(lon), cos(lon), 0.0));
        vec3  north = cross(n, east);
        float rho  = 0.003 + 0.012 * rnd(id, 3u);
        float spin = (rnd(id, 4u) < 0.5 ? -1.0 : 1.0) * (0.4 + 0.8 * rnd(id, 5u));
        float th   = rnd(id, 6u) * TWO_PI + spin * age;
        q = normalize(n + (east * cos(th) + north * sin(th)) * rho) * 1.004;
        if (inWedge(q)) {
            cull();
            return;
        }
        float fade = sin(PI * life);
        float heat = rnd(id, 7u);
        color = mix(vec3(1.0, 0.32, 0.04), vec3(1.0, 0.62, 0.22), heat * heat) * fade * 0.11;
    } else if (id < SURFACE + FACE) {
        if (frame.sun.w <= 0.0) {
            cull();
            return;
        }
        uint  k    = id - SURFACE;
        float side = rnd(k, 0u) < 0.5 ? 1.0 : -1.0;
        float phi  = frame.sun.z + side * frame.sun.y;
        float zone = rnd(k, 1u);
        float r, a;
        if (zone < 0.14) {
            // The core: sparks of fusion, flickering on and off.
            r = CORE_EDGE * sqrt(rnd(k, 2u));
            a = (rnd(k, 3u) - 0.5) * PI;
            float jit = t * (0.5 + rnd(k, 4u));
            r += 0.01 * sin(jit * 3.1 + 7.0 * rnd(k, 5u));
            a += 0.03 * cos(jit * 2.3 + 5.0 * rnd(k, 6u));
            float flash = pow(0.5 + 0.5 * sin(t * (1.5 + 3.0 * rnd(k, 7u)) + 40.0 * rnd(k, 5u)), 12.0);
            color = vec3(1.0, 0.95, 0.8) * (0.25 + 1.6 * flash) * 0.35;
            size  = 1.2;
        } else if (zone < 0.55) {
            // The radiative zone: a slow outward seep, each particle jittering as it goes.
            const float LIFE = 30.0;
            float life = fract(t / LIFE + rnd(k, 2u));
            float r0   = mix(CORE_EDGE, TACHOCLINE_BASE, rnd(k, 3u));
            r = r0 + 0.035 * life;
            a = (rnd(k, 4u) - 0.5) * PI;
            r += 0.006 * sin(t * 2.7 + 30.0 * rnd(k, 5u));
            a += 0.010 * cos(t * 2.1 + 30.0 * rnd(k, 6u));
            if (r > TACHOCLINE_BASE) {
                cull();
                return;
            }
            float x = (r - CORE_EDGE) / (TACHOCLINE_BASE - CORE_EDGE);
            color = mix(vec3(1.0, 0.8, 0.45), vec3(1.0, 0.45, 0.12), x) * sin(PI * life) * 0.22;
        } else {
            // The convective zone: round and round the rolls.
            float width = PI / CONV_CELLS;
            float cell  = floor(rnd(k, 2u) * CONV_CELLS);
            float ac    = -0.5 * PI + (cell + 0.5) * width;
            float turn  = mod(cell, 2.0) < 0.5 ? 1.0 : -1.0;
            float half_ = 0.5 * (1.0 - CONVECTION_BASE);
            float rho   = 0.15 + 0.82 * sqrt(rnd(k, 3u));
            float th    = rnd(k, 4u) * TWO_PI + turn * ROLL_RATE * t * (1.25 - 0.35 * rho);
            float u     = rho * cos(th);
            float v     = rho * sin(th);
            r = CONVECTION_BASE + half_ + u * half_ * 0.96;
            a = ac + v * 0.5 * width * 0.96;
            // Rising (outward) is hot; sinking is cooler.
            float rising = -turn * v;
            color = mix(vec3(0.9, 0.25, 0.04), vec3(1.0, 0.78, 0.35), 0.5 + 0.5 * rising) * 0.26;
        }
        q = facePoint(phi, r, a, side);
        if (q.z < WEDGE_FLOOR) {  // below the wedge's floor, inside the Sun
            cull();
            return;
        }
        color *= frame.sun.w * 0.45;
    } else if (id < SURFACE + FACE + LOOPS) {
        // Loops stand on the limb as the camera sees it, the gas flowing up one leg and down the
        // other. The camera stays at azimuth 0 of the Sun frame while it is there.
        uint  k    = id - SURFACE - FACE;
        uint  loop = k / (LOOPS / LOOP_COUNT);
        float lr   = float(loop);
        vec3  cam  = normalize(worldToSun(frame.camPos.xyz));
        vec3  side = normalize(cross(cam, vec3(0.0, 0.0, 1.0)));
        vec3  up   = cross(side, cam);
        // Five active regions, a handful of loops each.
        float region = mod(lr, 5.0);
        float pa   = hash13(vec3(region, 1.0, 2.0)) * TWO_PI + (hash13(vec3(lr, 1.0, 2.0)) - 0.5) * 0.35;
        vec3  foot = normalize((side * cos(pa) + up * sin(pa)) + cam * (hash13(vec3(lr, 3.0, 4.0)) - 0.55) * 0.5);
        vec3  span = normalize(cross(foot, cam) + up * (hash13(vec3(lr, 5.0, 6.0)) - 0.5));
        float w    = 0.04 + 0.10 * hash13(vec3(lr, 7.0, 8.0));
        float h    = w * (0.4 + 0.9 * hash13(vec3(lr, 9.0, 1.0)));
        float s    = fract(rnd(k, 0u) + t * (0.02 + 0.03 * hash13(vec3(lr, 2.0, 3.0))));
        float x    = s * 2.0 - 1.0;
        vec3  p    = foot + span * (w * x) + foot * (h * sqrt(max(1.0 - x * x, 0.0)));
        p += (vec3(rnd(k, 1u), rnd(k, 2u), rnd(k, 3u)) - 0.5) * 0.008;
        q = p;
        float glow = 0.6 + 0.4 * sin(t * 0.3 + lr);
        color = vec3(1.0, 0.66, 0.24) * glow * 0.07;
    } else if (id < SURFACE + FACE + LOOPS + CME) {
        uint  k  = id - SURFACE - FACE - LOOPS;
        int   e  = int(k / PER_CME);
        vec4  ev = frame.eruptions[e];
        vec4  info = frame.eruptInfo[e];
        float age  = ev.w;
        if (age < 0.0 || age > info.z) {
            cull();
            return;
        }
        float x    = age / info.z;
        float sz   = info.y;
        vec3  dir  = normalize(ev.xyz);
        vec3  tang = normalize(cross(dir, normalize(vec3(sin(info.x), cos(info.x), 0.6))));
        vec3  bin  = cross(dir, tang);
        // Slow rise as a prominence, then the eruption: the height runs away.
        float H    = sz * (0.04 + 0.10 * smoothstep(0.0, 0.12, x) + 3.2 * pow(max(x - 0.08, 0.0), 1.6));
        float cls  = rnd(k, 0u);
        float lag  = 0.82 + 0.3 * rnd(k, 1u);
        float phi  = rnd(k, 2u) * PI;
        float W    = sz * 0.10 + 0.55 * H;
        float fade = smoothstep(0.0, 0.04, x) * (1.0 - smoothstep(0.55, 1.0, x));
        vec3  p;
        if (cls < 0.62) {
            // The bright front: a shell round the loop, out of the plane as well as in it.
            float hh = H * lag;
            float out_ = (rnd(k, 3u) * 2.0 - 1.0) * W * 0.6 * sin(phi);
            p = dir * (1.0 + hh * sin(phi)) + tang * (W * cos(phi)) + bin * out_;
            p += (vec3(rnd(k, 4u), rnd(k, 5u), rnd(k, 6u)) - 0.5) * (0.02 + 0.08 * H);
            color = vec3(1.0, 0.62, 0.32) * 0.11;
        } else if (cls < 0.9) {
            // The filament: denser and redder, lower down, twisted.
            float hh  = H * 0.5 * lag;
            float tw  = phi * 6.0 + age * 0.8;
            float rr  = 0.01 + 0.04 * H;
            p = dir * (1.0 + hh * sin(phi)) + tang * (W * 0.5 * cos(phi)) + (bin * cos(tw) + dir * sin(tw)) * rr;
            p += (vec3(rnd(k, 4u), rnd(k, 5u), rnd(k, 6u)) - 0.5) * 0.012;
            color = vec3(1.0, 0.34, 0.07) * 0.16;
        } else {
            // Material draining back down the legs.
            float f   = rnd(k, 3u);
            float leg = rnd(k, 4u) < 0.5 ? -1.0 : 1.0;
            float fall = fract(f + age * 0.08);
            p = dir * (1.0 + H * 0.3 * (1.0 - fall)) + tang * (leg * W * 0.45);
            p += (vec3(rnd(k, 5u), rnd(k, 6u), rnd(k, 7u)) - 0.5) * 0.02;
            color = vec3(1.0, 0.42, 0.10) * 0.09;
        }
        if (length(p) < 1.003) p = normalize(p) * 1.003;
        q = p;
        // Spread thin while the eruption is small, or 15,000 sprites in one spot burn white.
        color *= fade * min(1.0, (0.08 + H) / 0.5);
        size = 1.15;
    } else {
        cull();
        return;
    }

    if (!sunVisible(q)) {
        cull();
        return;
    }
    vec3 world = sunToWorld(q);
    if (behindBody(world)) {
        cull();
        return;
    }
    vec4 clip  = frame.viewProj * vec4(world, 1.0);
    if (clip.w <= 0.01) {
        cull();
        return;
    }
    // Brightness per sprite holds steady as the Sun grows on screen: more pixels, same total.
    float dist  = length(world - frame.camPos.xyz);
    float scale = frame.viewport.y / 1080.0;
    float px    = clamp(1.9 * scale * size, 1.0, frame.render.w);
    gl_PointSize = px;
    gl_Position  = clip;
    float near   = (3.4 * 3.4) / (dist * dist);
    vColor = color * frame.sun2.x * min(near, 1.6) * 4.0 / (px * px);
}
