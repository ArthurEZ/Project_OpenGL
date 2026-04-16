#ifndef MATH_H
#define MATH_H

#include <array>
#include <cmath>

constexpr float kPi = 3.14159265358979323846f;

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Mat4 {
    std::array<float, 16> m{};
};

Mat4 identity_mat4();
Mat4 mul_mat4(const Mat4& a, const Mat4& b);
Vec3 transform_point(const Mat4& m, const Vec3& v);
Mat4 compose_trs(
    const std::array<float, 3>& t,
    const std::array<float, 4>& q,
    const std::array<float, 3>& s
);

Vec3 operator+(const Vec3& a, const Vec3& b);
Vec3 operator-(const Vec3& a, const Vec3& b);
Vec3 operator*(const Vec3& v, float s);

float length_sq(const Vec3& v);
float length(const Vec3& v);
Vec3 normalized(const Vec3& v);
float clampf(float value, float min_value, float max_value);
float distance_xz_sq(const Vec3& a, const Vec3& b);

#endif // MATH_H
