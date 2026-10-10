#include "sim/math.h"

namespace sim {

Vec3 Slerp(Vec3 a, Vec3 b, float t) {
    const float d = Dot(a, b);
    if (d > 0.9995f) return Normalize(Lerp(a, b, t));
    const float angle = std::acos(d < -1.0f ? -1.0f : d);
    // The direction in the plane of a and b, perpendicular to a.
    Vec3 perp = b - a * d;
    if (Length(perp) < 1e-5f) perp = Cross(a, Vec3{0, 0, 1});
    perp = Normalize(perp);
    return Normalize(a * std::cos(angle * t) + perp * std::sin(angle * t));
}

Mat4 LookAt(const CameraPose& pose) {
    const Vec3 f = Normalize(pose.forward);
    const Vec3 s = Normalize(Cross(f, pose.up));
    const Vec3 u = Cross(s, f);

    Mat4 r;
    r.m[0]  = s.x;
    r.m[4]  = s.y;
    r.m[8]  = s.z;
    r.m[1]  = u.x;
    r.m[5]  = u.y;
    r.m[9]  = u.z;
    r.m[2]  = -f.x;
    r.m[6]  = -f.y;
    r.m[10] = -f.z;
    r.m[12] = -Dot(s, pose.eye);
    r.m[13] = -Dot(u, pose.eye);
    r.m[14] = Dot(f, pose.eye);
    r.m[15] = 1.0f;
    return r;
}

Mat4 Perspective(float fovY, float aspect, float zNear, float zFar) {
    // Right-handed view space looking down -z, Vulkan clip space: y down, depth 0..1.
    const float f = 1.0f / std::tan(fovY * 0.5f);
    Mat4        p;
    p.m[0]  = f / aspect;
    p.m[5]  = -f;
    p.m[10] = zFar / (zNear - zFar);
    p.m[11] = -1.0f;
    p.m[14] = zNear * zFar / (zNear - zFar);
    return p;
}

Mat4 Multiply(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            r.m[col * 4 + row] = sum;
        }
    }
    return r;
}

Mat4 Inverse(const Mat4& in) {
    // Cofactor expansion; the matrices here are well conditioned.
    const float* m = in.m;
    float        inv[16];
    inv[0]  = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4]  = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8]  = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1]  = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5]  = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9]  = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2]  = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6]  = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3]  = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7]  = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    Mat4        r;
    if (det == 0.0f) return r;
    for (int i = 0; i < 16; ++i) r.m[i] = inv[i] / det;
    return r;
}

void Project(const Mat4& m, Vec3 p, float w, float out[4]) {
    for (int r = 0; r < 4; ++r) out[r] = m.m[r] * p.x + m.m[4 + r] * p.y + m.m[8 + r] * p.z + m.m[12 + r] * w;
}

}  // namespace sim
