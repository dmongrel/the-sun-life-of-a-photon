#include "sim/system.h"

#include <algorithm>
#include <cstdint>

namespace sim {
namespace {

constexpr float kDeg = kPi / 180.0f;

// ---- the planets ---------------------------------------------------------------------------
//
// Distances are squeezed so the trip takes minutes, not years: Pluto at 330 solar radii instead
// of 8,500. Sizes grow as (true radius)^0.6, Earth at 0.2 solar radii, so every planet is a sphere
// you can see at flyby distance while Jupiter still dwarfs Mercury. Real proportions would leave
// every planet an invisible speck for nearly the whole trip.

Planet g_planets[kPlanetCount] = {
    // name        look           au      dist   angle  radius tilt   day    rings
    {L"Pluto",   Look::Pluto,   39.48f, 330.0f,  8.0f, 0.073f, 122.5f, -60.0f, 0, 0, {}},
    {L"Neptune", Look::Neptune, 30.07f, 262.0f, -7.0f, 0.450f, 28.3f, 40.0f, 0, 0, {}},
    {L"Uranus",  Look::Uranus,  19.19f, 196.0f, 10.0f, 0.460f, 97.8f, -40.0f, 0, 0, {}},
    {L"Saturn",  Look::Saturn,   9.54f, 132.0f, -8.0f, 0.750f, 26.7f, 30.0f, 1.24f, 2.27f, {}},
    {L"Jupiter", Look::Jupiter,  5.20f,  82.0f,  6.0f, 0.840f,  3.1f, 25.0f, 0, 0, {}},
    {L"Mars",    Look::Mars,     1.52f,  34.0f, -9.0f, 0.137f, 25.2f, 40.0f, 0, 0, {}},
    {L"Earth",   Look::Earth,    1.00f,  24.0f,  7.0f, 0.200f, 23.4f, 40.0f, 0, 0, {}},
    {L"Venus",   Look::Venus,    0.72f,  17.0f, -6.0f, 0.194f, 177.4f, -120.0f, 0, 0, {}},
    {L"Mercury", Look::Mercury,  0.39f,  11.0f,  4.0f, 0.112f,  0.0f, 120.0f, 0, 0, {}},
};

// Where the loop starts and ends: far enough out that Pluto is below a pixel.
constexpr float kFarDistance = 520.0f, kFarAngle = 2.0f * kDeg, kFarHeight = 35.0f;

// The orbit at the Sun: distance from its centre, and elevation above its equator, so the cut
// faces show and the north pole tips toward the viewer.
constexpr float kOrbitDistance = 3.4f, kOrbitElevation = 18.0f * kDeg;

// The wedge, fully open, spans 135 degrees.
constexpr float kWedgeHalf = 67.5f * kDeg;

Vec3 OnOrbit(float distance, float angle, float height) {
    return {distance * std::cos(angle), distance * std::sin(angle), height};
}

Vec3 OrbitPoint() {
    return {kOrbitDistance * std::cos(kOrbitElevation), 0.0f, kOrbitDistance * std::sin(kOrbitElevation)};
}

// ---- the route -----------------------------------------------------------------------------

struct Route {
    std::vector<Vec3> points;  // far point, one pass point per planet, the orbit point
    std::vector<float> knots;  // centripetal parameterisation
};

Route BuildRoute() {
    for (Planet& p : g_planets) p.position = OnOrbit(p.distance, p.angleDeg * kDeg, 0.0f);

    Route r;
    r.points.push_back(OnOrbit(kFarDistance, kFarAngle, kFarHeight));
    for (int i = 0; i < kPlanetCount; ++i) {
        const Planet& p    = g_planets[i];
        const Vec3    prev = r.points.back();
        const Vec3    next = i + 1 < kPlanetCount ? g_planets[i + 1].position : OrbitPoint();
        // Pass beside each planet, alternating sides and a little above or below, so the route
        // weaves.
        const Vec3  along = Normalize(next - prev);
        const Vec3  side  = Normalize(Cross(along, Vec3{0, 0, 1}));
        const float sign  = (i % 2 == 0) ? 1.0f : -1.0f;
        const float pass  = 3.4f * p.radius + 0.25f;
        r.points.push_back(p.position + side * (sign * pass) + Vec3{0, 0, -sign * 0.8f * p.radius});
    }
    r.points.push_back(OrbitPoint());

    // Centripetal Catmull-Rom knots, so legs of 70 and 7 radii side by side don't overshoot.
    r.knots.push_back(0.0f);
    for (std::size_t i = 1; i < r.points.size(); ++i) {
        r.knots.push_back(r.knots.back() + std::sqrt(Length(r.points[i] - r.points[i - 1])));
    }
    return r;
}

const Route& TheRoute() {
    static const Route route = BuildRoute();
    return route;
}

// The point at fraction f of leg i (from point i to point i + 1).
Vec3 RoutePoint(int i, float f) {
    const Route& r = TheRoute();
    const int    n = static_cast<int>(r.points.size());
    auto P = [&](int k) -> Vec3 {
        if (k < 0) return r.points[0] * 2.0f - r.points[1];
        if (k >= n) return r.points[n - 1] * 2.0f - r.points[n - 2];
        return r.points[k];
    };
    auto K = [&](int k) -> float {
        if (k < 0) return r.knots[0] - (r.knots[1] - r.knots[0]);
        if (k >= n) return r.knots[n - 1] + (r.knots[n - 1] - r.knots[n - 2]);
        return r.knots[k];
    };
    const Vec3  p0 = P(i - 1), p1 = P(i), p2 = P(i + 1), p3 = P(i + 2);
    const float t0 = K(i - 1), t1 = K(i), t2 = K(i + 1), t3 = K(i + 2);
    const float t  = t1 + (t2 - t1) * f;
    // Barry-Goldman pyramid.
    auto mix = [](Vec3 a, Vec3 b, float ta, float tb, float tt) {
        const float d = tb - ta;
        return d > 1e-6f ? a * ((tb - tt) / d) + b * ((tt - ta) / d) : a;
    };
    const Vec3 a1 = mix(p0, p1, t0, t1, t);
    const Vec3 a2 = mix(p1, p2, t1, t2, t);
    const Vec3 a3 = mix(p2, p3, t2, t3, t);
    const Vec3 b1 = mix(a1, a2, t0, t2, t);
    const Vec3 b2 = mix(a2, a3, t1, t3, t);
    return mix(b1, b2, t1, t2, t);
}

// Within a leg the camera slows near each planet and hurries between them.
float LegEase(float s) {
    constexpr float a = 0.75f;
    return s - a * std::sin(kTwoPi * s) / kTwoPi;
}

// The flight in, `seconds` into phase 1. Phase 4 calls it with time running backwards, which turns
// every look toward a planet into its mirror image: lit side first, dark side last.
CameraPose Inbound(float seconds) {
    const Schedule& s = Timing();
    int   leg = 0;
    float t   = std::max(seconds, 0.0f);
    while (leg < kLegCount - 1 && t > s.legs[leg]) {
        t -= s.legs[leg];
        ++leg;
    }
    const float f = LegEase(Clamp01(t / s.legs[leg]));
    const float u = static_cast<float>(leg) + f;  // route parameter: planet i is at u = i + 1

    CameraPose pose;
    pose.eye = RoutePoint(leg, f);
    pose.up  = {0, 0, 1};

    // Ahead is the Sun. Around each planet the gaze swings to it, holds it through closest
    // approach and on as it falls behind, so its lit side comes round, then swings back.
    const Vec3 toSun = Normalize(Vec3{} - pose.eye);
    float      best  = 0.0f;
    Vec3       toPlanet{};
    for (int i = 0; i < kPlanetCount; ++i) {
        const float du = u - static_cast<float>(i + 1);
        const float w  = SmoothStep(-0.32f, -0.10f, du) * (1.0f - SmoothStep(0.16f, 0.36f, du));
        if (w > best) {
            best     = w;
            toPlanet = Normalize(g_planets[i].position - pose.eye);
        }
    }
    pose.forward = best > 0.0f ? Slerp(toSun, toPlanet, best) : toSun;
    return pose;
}

// ---- the photon's path ---------------------------------------------------------------------

// The layers' outer radii, from the shared references (life-of-a-photon.md).
constexpr float kCoreEdge = 0.25f, kTachoclineBase = 0.66f, kConvectionBase = 0.71f;

struct Rng {
    std::uint32_t state;
    float Next() {  // [0, 1)
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(state >> 8) * (1.0f / 16777216.0f);
    }
};

// A random walk in the face plane, (s, h) = (r cos a, r sin a), from `from` until it reaches
// radius `until`. Each step goes `step` in a random direction, nudged outward by `bias` of a step:
// on screen it wanders, doubles back, and only slowly gets anywhere.
void Walk(std::vector<PathPoint>& out, Rng& rng, float until, float step, float bias, int maxSteps) {
    PathPoint p = out.back();
    float     s = p.r * std::cos(p.a), h = p.r * std::sin(p.a);
    for (int i = 0; i < maxSteps && p.r < until; ++i) {
        const float angle = kTwoPi * rng.Next();
        const float len   = step * (0.55f + 0.9f * rng.Next());
        const float r     = std::max(std::sqrt(s * s + h * h), 1e-4f);
        s += len * std::cos(angle) + bias * step * s / r;
        h += len * std::sin(angle) + bias * step * h / r;
        // Stay on the face: off the axis, and between about 50 degrees south and 60 north.
        s = std::max(s, 0.025f);
        const float a = std::atan2(h, s);
        if (a > 1.05f || a < -0.85f) h *= 0.9f;
        p = {std::sqrt(s * s + h * h), std::atan2(h, s)};
        out.push_back(p);
    }
}

PhotonPath BuildPhoton() {
    PhotonPath path;
    Rng        rng{0x5EED5u};
    auto&      pts = path.points;

    // 0: the core, from near the centre to its edge.
    path.stageBegin[0] = 0;
    pts.push_back({0.035f, 0.20f});
    Walk(pts, rng, kCoreEdge, 0.030f, 0.18f, 400);

    // 1: the radiative zone, longer steps as the gas thins.
    path.stageBegin[1] = static_cast<int>(pts.size()) - 1;
    Walk(pts, rng, kTachoclineBase, 0.045f, 0.18f, 400);

    // 2: the tachocline, a thin shear layer.
    path.stageBegin[2] = static_cast<int>(pts.size()) - 1;
    Walk(pts, rng, kConvectionBase, 0.014f, 0.30f, 100);

    // 3: the convective zone. The energy rides the plasma: up in a hot plume, over, down a little
    // as it cools, and up again. A looping curve (a prolate cycloid) up through the zone.
    path.stageBegin[3] = static_cast<int>(pts.size()) - 1;
    {
        const PathPoint start = pts.back();
        const float     rise  = 0.995f - start.r;
        constexpr float loops = 2.5f;
        constexpr int   n     = 120;
        const float     c     = rise / (kTwoPi * loops);
        constexpr float d     = 0.034f;
        for (int i = 1; i <= n; ++i) {
            const float phi = kTwoPi * loops * static_cast<float>(i) / n;
            const float r   = start.r + c * phi + d * std::sin(phi);
            const float y   = d * (1.0f - std::cos(phi));
            pts.push_back({r, start.a + y / r});
        }
    }

    // 4: the photosphere, where the gas turns transparent.
    path.stageBegin[4] = static_cast<int>(pts.size()) - 1;
    {
        const PathPoint last = pts.back();
        for (int i = 1; i <= 4; ++i) pts.push_back({last.r + (1.004f - last.r) * i / 4.0f, last.a});
    }

    // 5: out into space, straight.
    path.stageBegin[5] = static_cast<int>(pts.size()) - 1;
    {
        const PathPoint last = pts.back();
        for (int i = 1; i <= 24; ++i) pts.push_back({last.r + 1.7f * i / 24.0f, last.a});
    }
    path.stageBegin[6] = static_cast<int>(pts.size()) - 1;
    return path;
}

// ---- eruptions -----------------------------------------------------------------------------

// Position angles on screen (0 = right, 90 = up) where each eruption leaves the limb: all on the
// left half, away from the photon's face on the right.
constexpr float kEruptionAngles[kEruptionCount] = {160.0f, 205.0f, 128.0f, 186.0f, 228.0f, 148.0f};

float Fade(float t, float in0, float in1, float out0, float out1) {
    return SmoothStep(in0, in1, t) * (1.0f - SmoothStep(out0, out1, t));
}

}  // namespace

const Planet* Planets() {
    TheRoute();  // places the planets
    return g_planets;
}

const PhotonPath& Photon() {
    static const PhotonPath path = BuildPhoton();
    return path;
}

Moment At(float loopSeconds) {
    TheRoute();
    const Schedule& s    = Timing();
    const float     loop = LoopLength();
    float           t    = std::fmod(loopSeconds, loop);
    if (t < 0.0f) t += loop;

    Moment m;
    m.t = t;
    const float arrival   = ArrivalTime();
    const float departure = DepartureTime();
    m.sinceArrival        = t - arrival;

    // The Sun's frame turns once while the camera is at the Sun, and the camera turns with it, so
    // the route out leaves from exactly where the route in arrived.
    m.sunRate  = kTwoPi / s.atSun;
    m.sunAngle = m.sunRate * (t - arrival);

    if (t < arrival) {
        m.camera = Inbound(t);
    } else if (t < departure) {
        m.camera.eye     = RotateZ(OrbitPoint(), m.sunAngle);
        m.camera.forward = Normalize(Vec3{} - m.camera.eye);
        m.camera.up      = {0, 0, 1};
    } else {
        const float back = (t - departure) / s.outbound;  // 0 .. 1
        m.camera         = Inbound(InboundLength() * (1.0f - back));
    }

    const float a = m.sinceArrival;
    // The wedge, centred on the camera's side of the Sun. In the Sun's frame the camera sits at
    // azimuth 0 throughout the visit.
    m.wedge.open      = SmoothStep(s.wedgeOpen.start, s.wedgeOpen.end, a) *
                   (1.0f - SmoothStep(s.wedgeClose.start, s.wedgeClose.end, a));
    m.wedge.halfAngle = kWedgeHalf * m.wedge.open;
    m.wedge.centre    = 0.0f;

    m.titleAlpha = Fade(a, s.titleStart, s.titleStart + s.titleFadeIn, s.titleStart + s.titleFadeIn + s.titleHold,
                        s.titleStart + s.titleFadeIn + s.titleHold + s.titleFadeOut);

    for (int i = 0; i < kCaptionCount; ++i) {
        const Span& c = s.captions[i];
        if (a >= c.start && a < c.end) {
            m.caption      = i;
            m.captionAlpha = Fade(a, c.start, c.start + 1.2f, c.end - 1.2f, c.end);
        }
    }

    // The photon: born when the core stage begins, revealed point by point at an even pace within
    // each stage, and faded out with the wedge.
    m.photonAlpha = SmoothStep(s.stages[0].start - 1.0f, s.stages[0].start + 0.5f, a) *
                    (1.0f - SmoothStep(s.wedgeClose.start - 4.0f, s.wedgeClose.start + 2.0f, a));
    const PhotonPath& path = Photon();
    m.photonHead           = 0.0f;
    for (int k = 0; k < kStageCount; ++k) {
        const Span& st = s.stages[k];
        if (a >= st.start) {
            const float f = Clamp01((a - st.start) / (st.end - st.start));
            m.photonHead  = path.stageBegin[k] + f * (path.stageBegin[k + 1] - path.stageBegin[k]);
        }
        if (a >= st.start && a < st.end) {
            m.stage      = k;
            m.stageAlpha = Fade(a, st.start, st.start + 1.2f, st.end - 0.9f, st.end);
        }
    }
    // The last label stays until the path fades.
    if (a >= s.stages[kStageCount - 1].end && m.photonAlpha > 0.0f) {
        m.stage      = kStageCount - 1;
        m.stageAlpha = m.photonAlpha;
    }

    // Eruptions leave from the limb as the camera sees it from its place in the Sun's frame.
    const Vec3 cam   = OrbitPoint();
    const Vec3 fwd   = Normalize(Vec3{} - cam);
    const Vec3 right = Normalize(Cross(fwd, Vec3{0, 0, 1}));
    const Vec3 up    = Cross(right, fwd);
    for (int i = 0; i < kEruptionCount; ++i) {
        Eruption&   e   = m.eruptions[i];
        const float psi = kEruptionAngles[i] * kDeg;
        e.age   = a - s.eruptions[i];
        e.dir   = Normalize(right * std::cos(psi) + up * std::sin(psi) + fwd * -0.18f);
        e.seed  = 17.0f * static_cast<float>(i) + 3.0f;
        e.scale = 0.8f + 0.12f * static_cast<float>((i * 7) % 5);
        if (e.age > s.eruptionLength) e.age = -1.0f;
    }
    return m;
}

Vec3 PathToWorld(const Moment& m, PathPoint p) {
    // The right-hand face, as the camera sees it, at azimuth centre + half angle. Lift the path a
    // hair off the face, into the open wedge, so it is never hidden by the face it lies on.
    const float az     = m.wedge.centre + m.wedge.halfAngle;
    const Vec3  out    = {std::cos(az), std::sin(az), 0.0f};
    const Vec3  normal = {std::sin(az), -std::cos(az), 0.0f};
    const Vec3  local  = out * (p.r * std::cos(p.a)) + Vec3{0, 0, p.r * std::sin(p.a)} + normal * 0.004f;
    return SunToWorld(m, local);
}

}  // namespace sim
