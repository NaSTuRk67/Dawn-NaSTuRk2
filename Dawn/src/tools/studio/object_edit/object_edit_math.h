#pragma once

/**
 * Small vector and quaternion types for the object edit module. They match the studio's
 * System.Numerics behaviour where the edit rules depend on it: exact equality, Normalize,
 * and the Euler convention the inspector shows (XYZ degrees).
 */

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawn::tools::studio::object_edit {

/** Three floats in the studio's world space. Equality is exact, as Vector3 == is. */
struct Vec3 final {
    float x{};
    float y{};
    float z{};

    [[nodiscard]] bool operator==(const Vec3&) const noexcept = default;
};

/** A quaternion stored x, y, z, w. The identity is (0, 0, 0, 1). */
struct Quat final {
    float x{};
    float y{};
    float z{};
    float w{1.0F};

    [[nodiscard]] bool operator==(const Quat&) const noexcept = default;
};

[[nodiscard]] inline bool is_finite(Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] inline bool is_finite(Quat value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
}

[[nodiscard]] inline float length_squared(Quat value) noexcept {
    return value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
}

/** Divides by the length, as Quaternion.Normalize does. A zero quaternion yields NaN, as it does there. */
[[nodiscard]] inline Quat normalize(Quat value) noexcept {
    const float length = std::sqrt(length_squared(value));
    return Quat{value.x / length, value.y / length, value.z / length, value.w / length};
}

/** True when a point lies inside the box, edges included. A non-finite point is never inside. */
[[nodiscard]] inline bool contains_inclusive(Vec3 minimum, Vec3 maximum, Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z)
        && value.x >= minimum.x && value.x <= maximum.x
        && value.y >= minimum.y && value.y <= maximum.y
        && value.z >= minimum.z && value.z <= maximum.z;
}

/** Euler XYZ degrees of a rotation, the same formulas the inspector uses. The input is normalized first. */
[[nodiscard]] inline Vec3 degrees_from_quaternion(Quat value) noexcept {
    const Quat q = normalize(value);
    const float pitchSine = std::clamp(2.0F * (q.w * q.y - q.z * q.x), -1.0F, 1.0F);
    const Vec3 radians{
        std::atan2(2.0F * (q.w * q.x + q.y * q.z), 1.0F - 2.0F * (q.x * q.x + q.y * q.y)),
        std::asin(pitchSine),
        std::atan2(2.0F * (q.w * q.z + q.x * q.y), 1.0F - 2.0F * (q.y * q.y + q.z * q.z)),
    };
    constexpr float kToDegrees = 180.0F / std::numbers::pi_v<float>;
    return Vec3{radians.x * kToDegrees, radians.y * kToDegrees, radians.z * kToDegrees};
}

/** The inverse of degrees_from_quaternion. The result is a unit quaternion. */
[[nodiscard]] inline Quat quaternion_from_degrees(Vec3 value) noexcept {
    constexpr float kHalfToRadians = std::numbers::pi_v<float> / 360.0F;
    const float sx = std::sin(value.x * kHalfToRadians);
    const float cx = std::cos(value.x * kHalfToRadians);
    const float sy = std::sin(value.y * kHalfToRadians);
    const float cy = std::cos(value.y * kHalfToRadians);
    const float sz = std::sin(value.z * kHalfToRadians);
    const float cz = std::cos(value.z * kHalfToRadians);
    return normalize(Quat{
        sx * cy * cz - cx * sy * sz,
        cx * sy * cz + sx * cy * sz,
        cx * cy * sz - sx * sy * cz,
        cx * cy * cz + sx * sy * sz,
    });
}

} // namespace dawn::tools::studio::object_edit
