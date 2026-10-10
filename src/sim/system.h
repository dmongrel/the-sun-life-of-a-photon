// The solar system as the saver shows it, and everything that moves in it, as pure functions of
// loop time: where the camera is and what it looks at, the Sun's spin, the cut-away wedge, the
// photon's journey and the eruptions.
//
// Units: one solar radius. The Sun sits at the origin with its rotation axis along +z, turning
// counter-clockwise seen from +z, the same way the planets orbit. The planets lie near the xy plane.
#pragma once

#include "sim/math.h"
#include "sim/schedule.h"

#include <vector>

namespace sim {

constexpr int kPlanetCount = 9;

enum class Look { Mercury, Venus, Earth, Mars, Jupiter, Saturn, Uranus, Neptune, Pluto };

struct Planet {
    const wchar_t* name;
    Look           look;
    float          au;          // true mean distance, for the record
    float          distance;    // shown distance from the Sun, solar radii
    float          angleDeg;    // where on its orbit the route meets it
    float          radius;      // shown radius, solar radii
    float          tiltDeg;     // axial tilt
    float          dayLoopSeconds;  // one rotation on screen
    float          ringInner, ringOuter;  // in planet radii; 0 when there are no rings
    Vec3           position;    // derived
};

// Pluto first: the order the route meets them on the way in.
const Planet* Planets();

// The moons: for each planet, the closest ten big enough to be round, with Mars's two, and Pan,
// in the Encke gap of Saturn's rings. Sized like the planets, (true radius)^0.6; orbits out to
// four planet radii are true to scale, and squeezed beyond, so the outer moons stay near their
// planet.
constexpr int kMoonCount = 26;

enum class MoonLook { Plain, Luna, Charon, Phobos, Deimos };

struct MoonState {
    Vec3     position;
    float    radius = 0.0f;   // shown mean radius, solar radii
    Vec3     normal;          // of its orbit; the moon is locked, one face to its planet
    int      parent = 0;      // index into Planets()
    Vec3     axes{1, 1, 1};   // semi-axes over the mean radius: toward the planet, along track, normal
    MoonLook look   = MoonLook::Plain;
    float    brightness = 1.0f;
    const char* name = "";
};

// Every moon at loop time `t`.
void Moons(float t, MoonState out[kMoonCount]);

// The camera's field of view, vertical, radians.
constexpr float kFovY = 0.80f;

struct Wedge {
    float open      = 0.0f;  // 0 closed .. 1 fully open
    float halfAngle = 0.0f;  // radians, either side of the centre line
    float centre    = 0.0f;  // azimuth of the centre line in the Sun's frame
};

// A point on the photon's path, in the plane of the right-hand cut face: r from the Sun's centre
// (solar radii) and a, the angle above the Sun's equator in that plane.
struct PathPoint {
    float r = 0, a = 0;
};

struct PhotonPath {
    std::vector<PathPoint> points;
    int                    stageBegin[kStageCount + 1] = {};  // points of stage k: [begin k, begin k+1)
};

struct Eruption {
    float age     = -1.0f;  // seconds since it began; < 0 or > length: inactive
    Vec3  dir;              // launch direction, Sun frame, unit
    float seed    = 0.0f;
    float scale   = 1.0f;   // size
};

struct Moment {
    float      t = 0.0f;  // loop time, wrapped
    CameraPose camera;
    float      sunAngle = 0.0f;  // the Sun frame's rotation about +z, radians
    float      sunRate  = 0.0f;  // radians per loop second
    Wedge      wedge;

    float sinceArrival = -1e9f;  // loop seconds since arrival (negative before)
    float titleAlpha   = 0.0f;
    int   caption      = -1;     // which caption, or -1
    float captionAlpha = 0.0f;
    int   stage        = -1;     // photon stage, or -1
    float stageAlpha   = 0.0f;
    float photonHead   = 0.0f;   // fractional index into the path points
    float photonAlpha  = 0.0f;   // the whole path fades with the wedge
    float route        = -1.0f;  // on the flights, where along the route: planet i is at i + 1
    bool  outbound     = false;  // flying out, so the route parameter is falling
    Eruption eruptions[kEruptionCount];
};

Moment At(float loopSeconds);

const PhotonPath& Photon();

// The photon path point (r, a) as a world-space position at this moment.
Vec3 PathToWorld(const Moment& m, PathPoint p);

// Sun frame <-> world.
inline Vec3 SunToWorld(const Moment& m, Vec3 p) { return RotateZ(p, m.sunAngle); }

}  // namespace sim
