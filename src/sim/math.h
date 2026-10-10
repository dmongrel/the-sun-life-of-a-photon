// The small vector maths the simulation and the renderer share.
#pragma once

#include <cmath>

namespace sim {

constexpr float kPi    = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

inline Vec3  operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3  operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3  operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3  operator*(float s, Vec3 a) { return a * s; }
inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3  Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Length(Vec3 v) { return std::sqrt(Dot(v, v)); }
inline Vec3  Normalize(Vec3 v) {
    const float n = Length(v);
    return n > 0.0f ? v * (1.0f / n) : Vec3{0, 0, 1};
}
inline Vec3 Lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }

// Rotates `v` about +z by `angle` radians, counter-clockwise seen from +z.
inline Vec3 RotateZ(Vec3 v, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return {c * v.x - s * v.y, s * v.x + c * v.y, v.z};
}

// Spherical interpolation between two unit vectors; well behaved short of exactly opposite.
Vec3 Slerp(Vec3 a, Vec3 b, float t);

inline float Clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
inline float SmoothStep(float a, float b, float x) {
    const float t = Clamp01((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

// Column-major, as GLSL expects.
struct Mat4 {
    float m[16] = {};
};

struct CameraPose {
    Vec3 eye;
    Vec3 forward;  // unit
    Vec3 up;       // roughly up; LookAt makes it orthogonal
};

Mat4 LookAt(const CameraPose& pose);
Mat4 Perspective(float verticalFovRadians, float aspect, float zNear, float zFar);  // Vulkan clip space
Mat4 Multiply(const Mat4& a, const Mat4& b);
Mat4 Inverse(const Mat4& m);

// Clip-space position of a point (w = 1) or a direction (w = 0).
void Project(const Mat4& m, Vec3 p, float w, float out[4]);

}  // namespace sim
