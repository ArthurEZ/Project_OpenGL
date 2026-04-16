#include "math.h"
#include <algorithm>

Mat4 identity_mat4() {
    return {{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    }};
}

Mat4 mul_mat4(const Mat4& a, const Mat4& b) {
    Mat4 out = {};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            out.m[col * 4 + row] =
                a.m[0 * 4 + row] * b.m[col * 4 + 0] +
                a.m[1 * 4 + row] * b.m[col * 4 + 1] +
                a.m[2 * 4 + row] * b.m[col * 4 + 2] +
                a.m[3 * 4 + row] * b.m[col * 4 + 3];
        }
    }
    return out;
}

Vec3 transform_point(const Mat4& m, const Vec3& v) {
    return {
        m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12],
        m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13],
        m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14]
    };
}

Mat4 compose_trs(
    const std::array<float, 3>& t,
    const std::array<float, 4>& q,
    const std::array<float, 3>& s
) {
    const float x = q[0];
    const float y = q[1];
    const float z = q[2];
    const float w = q[3];

    const float xx = x * x;
    const float yy = y * y;
    const float zz = z * z;
    const float xy = x * y;
    const float xz = x * z;
    const float yz = y * z;
    const float wx = w * x;
    const float wy = w * y;
    const float wz = w * z;

    Mat4 m = identity_mat4();

    m.m[0] = (1.0f - 2.0f * (yy + zz)) * s[0];
    m.m[1] = (2.0f * (xy + wz)) * s[0];
    m.m[2] = (2.0f * (xz - wy)) * s[0];

    m.m[4] = (2.0f * (xy - wz)) * s[1];
    m.m[5] = (1.0f - 2.0f * (xx + zz)) * s[1];
    m.m[6] = (2.0f * (yz + wx)) * s[1];

    m.m[8] = (2.0f * (xz + wy)) * s[2];
    m.m[9] = (2.0f * (yz - wx)) * s[2];
    m.m[10] = (1.0f - 2.0f * (xx + yy)) * s[2];

    m.m[12] = t[0];
    m.m[13] = t[1];
    m.m[14] = t[2];
    return m;
}

Vec3 operator+(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 operator-(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 operator*(const Vec3& v, float s) {
    return {v.x * s, v.y * s, v.z * s};
}

float length_sq(const Vec3& v) {
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

float length(const Vec3& v) {
    return std::sqrt(length_sq(v));
}

Vec3 normalized(const Vec3& v) {
    const float len = length(v);
    if (len < 1e-6f) {
        return {};
    }
    return v * (1.0f / len);
}

float clampf(float value, float min_value, float max_value) {
    return std::max(min_value, std::min(max_value, value));
}

float distance_xz_sq(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return dx * dx + dz * dz;
}
