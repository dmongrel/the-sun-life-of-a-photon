#version 450
#extension GL_GOOGLE_include_directive : require

// The scene, ray traced per pixel: a starfield, the planets, and the Sun with its cut-away.
//
// The Sun is drawn the way the famous solar videos show it rather than at its real brightness:
// granulation like DKIST's, the mottled orange chromosphere of SDO's 304 channel, a glowing limb
// and a faint corona. The planets are procedural, lit by the Sun at the origin.

#include "common.glsl"

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

// NASA and USGS planet maps, equirectangular, north up: Pluto (New Horizons), Mars (Viking),
// Earth (Blue Marble) and its clouds, Jupiter (Cassini), Mercury (MESSENGER), Earth's city lights
// (Black Marble). The order matches IDR_MAP_* in src/resource.h.
layout(set = 0, binding = 4) uniform sampler2D maps[7];

// Samples a map at (lat, lon). Longitude wraps where atan jumps from pi to -pi, and the derivatives
// there would pick the smallest mip in a one-pixel seam; taking them from whichever of two
// longitude origins is continuous at this pixel avoids it.
vec4 sampleMap(int k, float lat, float lon) {
    float u  = fract(lon / TWO_PI);
    float u2 = fract(u + 0.5) - 0.5;
    vec2  uv = vec2(u, 0.5 - lat / PI);
    vec2  dx = vec2(dFdx(u), dFdx(uv.y)), dy = vec2(dFdy(u), dFdy(uv.y));
    vec2  dx2 = vec2(dFdx(u2), dx.y), dy2 = vec2(dFdy(u2), dy.y);
    if (abs(dx2.x) + abs(dy2.x) < abs(dx.x) + abs(dy.x)) {
        dx = dx2;
        dy = dy2;
    }
    if (k == 0) return textureGrad(maps[0], uv, dx, dy);
    if (k == 1) return textureGrad(maps[1], uv, dx, dy);
    if (k == 2) return textureGrad(maps[2], uv, dx, dy);
    if (k == 3) return textureGrad(maps[3], uv, dx, dy);
    if (k == 4) return textureGrad(maps[4], uv, dx, dy);
    if (k == 5) return textureGrad(maps[5], uv, dx, dy);
    return textureGrad(maps[6], uv, dx, dy);
}

// ---- the sky -------------------------------------------------------------------------------

vec3 starLayer(vec3 rd, float scale, float density, float gain) {
    vec3  q    = rd * scale;
    vec3  cell = floor(q);
    vec3  h    = hash33(cell);
    if (h.x > density) return vec3(0.0);
    vec3  star = normalize(cell + 0.25 + 0.5 * hash33(cell + 7.1)) * scale;
    float px   = length(q - star) / scale * frame.render.y;  // pixels from the star
    float size = 0.75 * frame.viewport.y / 1080.0;
    float mag  = pow(h.y, 7.0) * 3.0 + 0.05;
    // A colour by temperature: mostly white, some blue, some orange.
    vec3  tint = mix(vec3(1.0, 0.82, 0.62), vec3(0.72, 0.84, 1.0), h.z);
    return tint * gain * mag * exp(-px * px / (size * size));
}

vec3 sky(vec3 rd) {
    vec3 c = vec3(0.0);
    c += starLayer(rd, 140.0, 0.008, 2.2);
    c += starLayer(rd, 330.0, 0.003, 0.9);
    c += starLayer(rd, 620.0, 0.00125, 0.45);
    // The Milky Way as a faint band, tilted 60 degrees to the planets' plane.
    vec3  pole = normalize(vec3(0.0, -0.87, 0.5));
    float lat  = dot(rd, pole);
    float band = exp(-lat * lat / 0.03) * (0.4 + fbm(rd * 6.0, 4));
    c += vec3(0.55, 0.55, 0.62) * band * 0.012;
    return c;
}

// ---- the Sun's surface ---------------------------------------------------------------------

// Hot to cool, through the 304-angstrom palette: white-yellow, orange, deep red.
vec3 sunPalette(float x) {
    x = clamp(x, 0.0, 1.2);
    vec3 a = vec3(0.30, 0.04, 0.00);
    vec3 b = vec3(0.95, 0.30, 0.03);
    vec3 c = vec3(1.20, 0.66, 0.18);
    vec3 d = vec3(1.50, 1.15, 0.70);
    if (x < 0.45) return mix(a, b, x / 0.45);
    if (x < 0.85) return mix(b, c, (x - 0.45) / 0.40);
    return mix(c, d, (x - 0.85) / 0.35);
}

// Differential rotation relative to the frame, which turns with the equator. Snodgrass & Ulrich
// (1990): 14.71 - 2.39 sin^2 - 1.78 sin^4 degrees a day, so the poles lag the equator by a third.
float relativeRate(float sinLat) {
    float s2 = sinLat * sinLat;
    return frame.sun2.w * (-2.39 * s2 - 1.78 * s2 * s2) / 14.71;
}

// Two layers of pattern, each reset every FLOW seconds and cross-faded, so the shear of the
// differential rotation shows without winding the surface up forever.
const float FLOW = 50.0;

float surfaceLayer(vec3 n, float age, float seed) {
    float lon = atan(n.y, n.x) - relativeRate(n.z) * age;
    float cl  = sqrt(max(1.0 - n.z * n.z, 0.0));
    vec3  p   = vec3(cl * cos(lon), cl * sin(lon), n.z);
    float t   = loopTime();
    // Granules: cells about 1/80 of the radius, far larger than life so they still read on screen.
    vec2  w    = worley(p * 80.0 + vec3(seed, 0.0, t * 0.06));
    float gran = smoothstep(0.0, 0.35, w.y - w.x);
    // The chromospheric network: the mottling that fills SDO's 304 images.
    float net  = fbm(p * 9.0 + vec3(seed * 0.3, t * 0.01, 0.0), 5);
    // Active regions, bright; filaments, dark threads across the disk.
    float act  = smoothstep(0.58, 0.80, fbm(p * 2.2 + vec3(5.0), 3));
    float fil  = 1.0 - smoothstep(0.0, 0.025, abs(fbm(p * 3.0 + vec3(9.0, 2.0, seed * 0.02), 4) - 0.5));
    fil *= smoothstep(0.35, 0.6, fbm(p * 1.5 + 3.0, 2));
    return 0.14 * gran + 0.60 * net + 0.65 * act - 0.30 * fil;
}

vec3 sunSurface(vec3 n, vec3 view) {
    float f   = fract(loopTime() / FLOW);
    float a   = surfaceLayer(n, f * FLOW, 0.0);
    float b   = surfaceLayer(n, fract(f + 0.5) * FLOW, 43.0);
    float pat = mix(a, b, abs(2.0 * f - 1.0));
    float mu  = clamp(dot(n, -view), 0.0, 1.0);
    // Limb darkening in visible light, eased so the edge still glows the way 304 shows it.
    float limb = 0.55 + 0.45 * pow(mu, 0.5);
    vec3  c    = sunPalette(pat * 1.2 - 0.10) * limb * 0.62;
    c += vec3(1.0, 0.35, 0.06) * pow(1.0 - mu, 4.0) * 0.6;
    return c;
}

// ---- the cut faces -------------------------------------------------------------------------

vec3 sunInterior(vec3 q, int part) {
    float r = length(q);
    float s = length(q.xy);
    float a = atan(q.z, s);
    float t = loopTime();
    float stage = frame.sun2.y;
    vec3  c;
    if (r < CORE_EDGE) {
        // The core: white hot, churning.
        float x    = r / CORE_EDGE;
        float turb = fbm(vec3(q * 9.0) + vec3(0.0, 0.0, t * 0.15), 4);
        c = mix(vec3(1.9, 1.5, 0.9), vec3(1.6, 0.9, 0.3), x * x) * (0.8 + 0.4 * turb);
        if (stage == 0.0) c *= 1.15;
    } else if (r < TACHOCLINE_BASE) {
        // The radiative zone: the energy diffusing out as slow rings of light.
        float x      = (r - CORE_EDGE) / (TACHOCLINE_BASE - CORE_EDGE);
        float ripple = 0.5 + 0.5 * sin(60.0 * r - t * 0.8 + 3.0 * fbm(q * 4.0, 3));
        float turb   = fbm(vec3(q * 6.0) + vec3(t * 0.03), 4);
        c = mix(vec3(1.7, 0.95, 0.30), vec3(0.95, 0.36, 0.08), x) * (0.75 + 0.2 * ripple + 0.25 * turb);
        if (stage == 1.0) c *= 1.15;
    } else if (r < CONVECTION_BASE) {
        // The tachocline: a thin layer of shear, its streaks drawn out along the face.
        float streak = fbm(vec3(r * 140.0, a * 3.0 - t * 0.2, 0.0), 3);
        c = vec3(1.5, 0.62, 0.14) * (0.7 + 0.6 * streak);
        if (stage == 2.0) c *= 1.2;
    } else {
        // The convective zone: rolls of plasma, rising hot and bright, sinking cooler and dark.
        vec3  roll  = rollCoords(r, a);
        float up    = -roll.z * roll.y;  // where this roll's flow points outward
        float rho   = length(roll.xy);
        float theta = atan(roll.y, roll.x) - roll.z * ROLL_RATE * t;
        float swirl = fbm(vec3(rho * 3.0, cos(theta) * 1.6, sin(theta) * 1.6) + roll.z * 7.0 + floor(a * 4.5), 4);
        float heat  = clamp(0.5 + 0.5 * up + 0.35 * (swirl - 0.5) - 0.2 * max(rho - 1.0, 0.0), 0.0, 1.0);
        c = mix(vec3(0.45, 0.10, 0.02), vec3(1.45, 0.66, 0.16), heat);
        if (stage == 3.0) c *= 1.15;
    }
    // Faint lines at the layer boundaries, as on a diagram.
    float px   = 1.5 / max(frame.sun2.z, 1.0);  // a pixel and a half, in solar radii
    float line = 0.0;
    for (int i = 0; i < 3; ++i) {
        float edge = i == 0 ? CORE_EDGE : (i == 1 ? TACHOCLINE_BASE : CONVECTION_BASE);
        line = max(line, 1.0 - smoothstep(0.0, 0.004 + px, abs(r - edge)));
    }
    c += vec3(1.0, 0.8, 0.55) * line * 0.35;
    // The rim of the face, where it meets the surface: the photosphere's thin bright skin.
    c = mix(c, vec3(1.6, 0.9, 0.35), smoothstep(0.985, 1.0, r) * 0.7);
    return c * 0.42 * frame.sun.w;
}

// ---- the corona ----------------------------------------------------------------------------

vec3 corona(vec3 ro, vec3 rd, float tMax) {
    float tc = clamp(-dot(ro, rd), 0.0, tMax);
    vec3  cp = ro + rd * tc;  // closest approach to the centre, Sun frame
    float b  = length(cp);
    if (b < 0.98) return vec3(0.0);
    float near = smoothstep(0.98, 1.0, b);
    vec3  dir  = cp / b;
    float t    = loopTime();
    // Spicules and the chromosphere: a ragged glowing skin just above the limb.
    float rag  = fbm(dir * 40.0 + vec3(t * 0.05), 3);
    float skin = exp(-max(b - 1.0, 0.0) / (0.010 + 0.022 * rag));
    // Streamers: the corona's rays, brighter at low latitudes.
    float ray  = fbm(vec3(dir.xy * 3.0, dir.z * 1.5) + vec3(t * 0.004), 4);
    float k    = pow(b, -4.5) * (0.35 + 1.1 * ray * ray) * (1.0 - 0.5 * abs(dir.z));
    return near * (vec3(1.0, 0.42, 0.08) * skin * 1.1 + vec3(0.95, 0.62, 0.32) * k * 0.30);
}

// ---- the planets ---------------------------------------------------------------------------
// Index order is the route's: Pluto, Neptune, Uranus, Saturn, Jupiter, Mars, Earth, Venus, Mercury.

vec3 planetAlbedo(int i, vec3 n, float lat, float lon) {
    float t = loopTime();
    vec3  p = vec3(cos(lat) * cos(lon), cos(lat) * sin(lon), sin(lat));
    if (i == 0) {  // Pluto: tan and pale, dark red-brown belts, and the bright heart
        float f     = fbm(p * 5.0, 5);
        vec3  c     = mix(vec3(0.60, 0.47, 0.36), vec3(0.82, 0.72, 0.60), f);
        float belt  = smoothstep(0.35, 0.0, abs(lat + 0.05)) * smoothstep(0.45, 0.6, fbm(p * 3.0 + 2.0, 4));
        c           = mix(c, vec3(0.32, 0.17, 0.10), belt);
        vec3  heart = vec3(cos(0.3) * cos(PI), cos(0.3) * sin(PI), sin(0.3));
        float h     = smoothstep(0.42, 0.30, length(p - heart) + 0.08 * f);
        vec3  proc  = mix(c, vec3(0.97, 0.93, 0.88), h) * 0.75;
        // New Horizons saw one hemisphere well; the rest of the map is black. Its colours are
        // Ralph's blue, red and near-infrared, so they are eased toward Pluto's tans.
        vec3  m     = sampleMap(0, lat, lon).rgb;
        float seen  = smoothstep(0.015, 0.06, max(m.r, max(m.g, m.b)));
        float l     = dot(m, vec3(0.3, 0.55, 0.15));
        vec3  tan_  = mix(vec3(0.30, 0.16, 0.08), vec3(0.95, 0.88, 0.78), smoothstep(0.05, 0.75, l));
        return mix(proc, pow(mix(m, tan_, 0.35), vec3(1.35)) * 0.95, seen);
    }
    if (i == 1) {  // Neptune: deep blue, faint bands, a dark spot
        float band = 0.5 + 0.5 * sin(lat * 9.0 + 2.0 * fbm(p * 4.0 + t * 0.01, 3));
        vec3  c    = mix(vec3(0.18, 0.32, 0.78), vec3(0.32, 0.48, 0.92), band);
        vec3  spot = vec3(cos(-0.35) * cos(1.0), cos(-0.35) * sin(1.0), sin(-0.35));
        c *= 1.0 - 0.55 * smoothstep(0.22, 0.12, length(p - spot));
        return c;
    }
    if (i == 2) {  // Uranus: pale cyan, almost featureless
        return mix(vec3(0.58, 0.80, 0.84), vec3(0.66, 0.86, 0.88), 0.5 + 0.5 * sin(lat * 6.0));
    }
    if (i == 4) {  // Jupiter: Cassini's map
        return sampleMap(4, lat, lon).rgb;
    }
    if (i == 3) {  // Saturn: belts and zones
        float warp = fbm(vec3(p.xy * 3.0, p.z * 9.0) + vec3(t * 0.02, 0.0, 0.0), 5);
        float y    = lat * 5.0 + (warp - 0.5) * 0.7;
        float band = 0.5 + 0.5 * sin(y * 2.0) * cos(y * 0.7);
        return mix(vec3(0.80, 0.68, 0.46), vec3(0.94, 0.86, 0.66), band);
    }
    if (i == 5) {  // Mars: Viking's colourised mosaic
        return sampleMap(1, lat, lon).rgb;
    }
    if (i == 6) {  // Earth: the Blue Marble, under its clouds, drifting slowly east
        vec3  c     = sampleMap(2, lat, lon).rgb;
        float cloud = sampleMap(3, lat, lon - t * 0.01).r;
        return mix(c, vec3(0.95), smoothstep(0.08, 0.9, cloud));
    }
    if (i == 7) {  // Venus: pale yellow cloud, faint chevrons
        float f = fbm(vec3(p.xy * 2.0, p.z * 5.0) + vec3(t * 0.01), 5);
        return vec3(0.92, 0.82, 0.58) * (0.85 + 0.2 * f);
    }
    // Mercury: MESSENGER's mosaic, grey with a faint warm cast
    return sampleMap(5, lat, lon).r * vec3(1.0, 0.95, 0.88) * 1.1;
}

float sphereHit(vec3 ro, vec3 rd, vec3 c, float r) {
    vec3  o = ro - c;
    float b = dot(o, rd);
    float h = b * b - (dot(o, o) - r * r);
    if (h < 0.0) return -1.0;
    float t = -b - sqrt(h);
    return t > 0.0 ? t : -1.0;
}

vec3 planetAxis(int i) {
    float tilt = frame.planetSpin[i].x;
    return vec3(sin(tilt), 0.0, cos(tilt));
}

vec3 shadePlanet(int i, vec3 ro, vec3 rd, float t) {
    vec3  c  = frame.planets[i].xyz;
    vec3  p  = ro + rd * t;
    vec3  n  = normalize(p - c);
    vec3  ax = planetAxis(i);
    vec3  e1 = normalize(cross(vec3(0.0, 1.0, 0.0), ax));
    vec3  e2 = cross(ax, e1);
    float lat = asin(clamp(dot(n, ax), -1.0, 1.0));
    float lon = atan(dot(n, e2), dot(n, e1)) + frame.planetSpin[i].y;
    if (i == 0) {
        // Pluto turns once in 6.4 days: hold it still, the hemisphere New Horizons mapped (the
        // heart, at the map's centre) toward the Sun, so the lit side the camera turns to is that one.
        vec3 s = normalize(-c);
        lon = atan(dot(n, e2), dot(n, e1)) - atan(dot(s, e2), dot(s, e1)) + PI;
        // Its tilt of 122.5 degrees puts the map's north (the IAU pole) on the other end of the
        // axis: turn the globe over, which flips latitude and longitude together.
        lat = -lat;
        lon = TWO_PI - lon;
    }
    vec3  L   = normalize(-p);
    float ndl = dot(n, L);
    vec3  albedo = planetAlbedo(i, n, lat, lon);
    // A soft terminator; gas giants darken toward the limb.
    float lit = smoothstep(-0.06, 0.25, ndl) * max(ndl, 0.0) * 0.6 + smoothstep(-0.05, 0.15, ndl) * 0.4 * max(ndl, 0.0);
    float mu  = max(dot(n, -rd), 0.0);
    if (i >= 1 && i <= 4) lit *= 0.65 + 0.35 * mu;
    vec3 col = albedo * lit * 1.05 + albedo * 0.004;
    // Atmospheres glow at the rim on the day side.
    vec3 air = i == 6 ? vec3(0.35, 0.55, 1.0) : (i == 7 ? vec3(0.9, 0.8, 0.5) : ((i == 1 || i == 2) ? vec3(0.4, 0.6, 1.0) : vec3(0.0)));
    col += air * pow(1.0 - mu, 3.0) * smoothstep(-0.2, 0.4, ndl) * 0.6;
    // Earth's night side: its cities, sodium-lamp orange, as in The Black Hole's Earth. They fade in
    // across the terminator and show dimmed through cloud. Read through sRGB decoding, which with a
    // small floor keeps the JPEG's faint 8x8 block haze round each city from glowing as squares.
    if (i == 6) {
        float city  = max(sampleMap(6, lat, lon).r - 0.004, 0.0);
        float cloud = smoothstep(0.08, 0.9, sampleMap(3, lat, lon - loopTime() * 0.01).r);
        col += vec3(1.0, 0.72, 0.42) * (6.0 * city * (1.0 - 0.75 * cloud) * (1.0 - smoothstep(-0.12, 0.08, ndl)));
    }
    return col;
}

// Saturn's rings: (colour, alpha) where the ray crosses the ring plane, if it is in front of tMax.
vec4 rings(int i, vec3 ro, vec3 rd, float tMax) {
    float inner = frame.planetSpin[i].z, outer = frame.planetSpin[i].w;
    if (outer <= 0.0) return vec4(0.0);
    vec3  c  = frame.planets[i].xyz;
    float R  = frame.planets[i].w;
    vec3  ax = planetAxis(i);
    float d  = dot(rd, ax);
    if (abs(d) < 1e-5) return vec4(0.0);
    float t = dot(c - ro, ax) / d;
    if (t <= 0.0 || t >= tMax) return vec4(0.0);
    vec3  q  = ro + rd * t;
    float rr = length(q - c) / R;
    if (rr < inner || rr > outer) return vec4(0.0);
    if (i == 2) {
        // Uranus: thin dark rings, the bright epsilon ring outermost. They lie in its equator, so
        // with the planet on its side they stand nearly on end and show the tilt.
        const float radii[5] = float[5](1.64, 1.70, 1.79, 1.86, 2.00);
        float ring = 0.0;
        for (int k = 0; k < 5; ++k) ring = max(ring, (k == 4 ? 0.9 : 0.45) * (1.0 - smoothstep(0.0, k == 4 ? 0.022 : 0.010, abs(rr - radii[k]))));
        vec3 Lu = normalize(-q);
        return vec4(vec3(0.42, 0.45, 0.48) * (0.35 + 0.65 * abs(dot(ax, Lu))), ring * 0.45);
    }
    float x       = (rr - inner) / (outer - inner);
    float density = 0.45 + 0.4 * fbm(vec3(rr * 40.0, 0.0, 0.0), 4) + 0.25 * sin(rr * 160.0);
    density *= smoothstep(0.0, 0.05, x) * smoothstep(1.0, 0.95, x);
    density *= 1.0 - smoothstep(0.0, 0.015, 0.035 - abs(rr - 1.99)) * 0.95;  // the Cassini division
    density  = clamp(density, 0.0, 1.0);
    if (rr < 1.52) density *= 0.35;  // the faint C ring
    vec3  L     = normalize(-q);
    float light = 0.35 + 0.65 * abs(dot(ax, L));
    // The planet's shadow across the rings.
    vec3  o  = q - c;
    float b  = dot(o, L);
    float h  = b * b - (dot(o, o) - R * R);
    if (h > 0.0 && -b - sqrt(h) > 0.0) light *= 0.08;
    return vec4(vec3(0.86, 0.76, 0.58) * light * 1.2, density * 0.9);
}

void main() {
    vec2 ndc = vUV * 2.0 - 1.0;
    float tanH = frame.camFwd.w;
    vec3 rd = normalize(frame.camFwd.xyz + ndc.x * frame.camRight.w * tanH * frame.camRight.xyz -
                        ndc.y * tanH * frame.camUp.xyz);
    vec3 ro = frame.camPos.xyz;

    // The nearest planet.
    float tPlanet = 1e9;
    int   which   = -1;
    for (int i = 0; i < PLANET_COUNT; ++i) {
        float t = sphereHit(ro, rd, frame.planets[i].xyz, frame.planets[i].w);
        if (t > 0.0 && t < tPlanet) {
            tPlanet = t;
            which   = i;
        }
    }

    // The Sun, in its own frame.
    vec3  sro = worldToSun(ro), srd = worldToSun(rd);
    int   part;
    float tSun = sunHit(sro, srd, part);

    vec3  col;
    float tHit = 1e9;
    if (tSun > 0.0 && tSun < tPlanet) {
        vec3 q = sro + srd * tSun;
        col  = part == 0 ? sunSurface(normalize(q), srd) : sunInterior(q, part);
        tHit = tSun;
    } else if (which >= 0) {
        col  = shadePlanet(which, ro, rd, tPlanet);
        tHit = tPlanet;
    } else {
        col = sky(rd);
        col += corona(sro, srd, 1e6);
    }

    // Glare round the Sun while it is small, so from Pluto it is a bright star, not a speck.
    float sunPx = frame.sun2.z;
    vec3  toSun = normalize(-ro);
    float ang   = acos(clamp(dot(rd, toSun), -1.0, 1.0)) * frame.render.y;  // pixels off centre
    float small = 1.0 - smoothstep(12.0, 80.0, sunPx);
    if (small > 0.0 && tHit > length(ro) - 1.0) {
        float core = max(ang - sunPx, 0.0);
        col += vec3(1.0, 0.72, 0.42) * small * (1.4 * exp(-core / 2.5) + 0.25 * exp(-core / 18.0) + 0.05 * exp(-core / 90.0));
    }

    // Saturn's rings lie over whatever is behind them.
    for (int i = 0; i < PLANET_COUNT; ++i) {
        vec4 ring = rings(i, ro, rd, tHit);
        col = mix(col, ring.rgb, ring.a);
    }

    outColor = vec4(col, 1.0);
}
