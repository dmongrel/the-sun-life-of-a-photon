// The loop's timing: every phase, flyby, photon stage, caption and eruption, in one table.
//
// Times are loop seconds at speed 1. The host multiplies wall-clock time by the speed factor
// (SUN_PHOTON_SPEED) before anything here sees it, so the whole loop scales together.
#pragma once

namespace sim {

constexpr int kLegCount      = 10;  // far point -> Pluto -> ... -> Mercury -> the orbit
constexpr int kStageCount    = 6;   // core, radiative zone, tachocline, convective zone, photosphere, space
constexpr int kCaptionCount  = 9;
constexpr int kEruptionCount = 6;

struct Span {
    float start = 0, end = 0;
};

struct Schedule {
    // Phase 1: seconds spent on each leg of the flight in. Phase 4 flies the same route backwards.
    float legs[kLegCount];

    // Phases 2 and 3, in seconds after arrival at the Sun.
    float titleStart, titleFadeIn, titleHold, titleFadeOut;
    Span  wedgeOpen;  // opening
    Span  stages[kStageCount];
    Span  captions[kCaptionCount];
    Span  wedgeClose;
    float atSun;  // arrival to departure; the camera co-rotates exactly once in this time

    // Phase 4.
    float outbound;

    // Coronal mass ejections, start times in seconds after arrival (may be negative), and how
    // long each one lasts.
    float eruptions[kEruptionCount];
    float eruptionLength;
};

const Schedule& Timing();

// Derived from the table.
float InboundLength();   // phase 1
float ArrivalTime();     // loop time the camera reaches the Sun
float DepartureTime();   // loop time phase 4 begins
float LoopLength();      // the whole loop

}  // namespace sim
