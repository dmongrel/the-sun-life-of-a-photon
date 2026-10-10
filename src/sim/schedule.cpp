#include "sim/schedule.h"

namespace sim {
namespace {

// The whole schedule. Edit here to change the pacing; everything else follows.
constexpr Schedule kSchedule = {
    // Phase 1, the flight in (200 s): far point to Pluto, then Neptune, Uranus, Saturn, Jupiter,
    // Mars, Earth, Venus, Mercury, and into orbit at the Sun.
    .legs = {24, 20, 20, 20, 20, 22, 16, 16, 16, 26},

    // Phase 2: the title, a film credit, soon after arrival.
    .titleStart   = 4.0f,
    .titleFadeIn  = 2.0f,
    .titleHold    = 10.0f,
    .titleFadeOut = 2.5f,

    // Phase 3: the wedge opens once the title has gone, the photon makes its journey, and the
    // wedge closes again.
    .wedgeOpen = {22.0f, 32.0f},
    .stages =
        {
            {34.0f, 82.0f},    // core
            {82.0f, 136.0f},   // radiative zone
            {136.0f, 150.0f},  // tachocline
            {150.0f, 188.0f},  // convective zone
            {188.0f, 200.0f},  // photosphere
            {200.0f, 216.0f},  // out into space
        },
    .captions =
        {
            {22.0f, 33.0f},    // the cut-away
            {35.0f, 50.0f},    // born in the core
            {52.0f, 80.0f},    // the random walk
            {83.0f, 107.0f},   // the radiative zone
            {109.0f, 134.0f},  // no single photon
            {137.0f, 149.0f},  // the tachocline
            {151.0f, 186.0f},  // convection
            {188.0f, 199.0f},  // the photosphere
            {201.0f, 218.0f},  // to Earth
        },
    .wedgeClose = {222.0f, 234.0f},
    .atSun      = 240.0f,

    // Phase 4, the flight out, slower than the flight in.
    .outbound = 240.0f,

    .eruptions      = {-6.0f, 40.0f, 95.0f, 150.0f, 205.0f, 245.0f},
    .eruptionLength = 50.0f,
};

}  // namespace

const Schedule& Timing() { return kSchedule; }

float InboundLength() {
    float total = 0.0f;
    for (float leg : kSchedule.legs) total += leg;
    return total;
}

float ArrivalTime() { return InboundLength(); }
float DepartureTime() { return ArrivalTime() + kSchedule.atSun; }
float LoopLength() { return DepartureTime() + kSchedule.outbound; }

}  // namespace sim
