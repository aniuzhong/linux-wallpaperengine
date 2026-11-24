#pragma once

#include <cmath>

#include <glm/vec2.hpp>

namespace WallpaperEngine::Scripting::Modules::VectorMath {
inline constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
inline constexpr double kRadiansToDegrees = 180.0 / 3.14159265358979323846;

/** WEVector.angleVector2: direction vector for an angle in degrees. */
[[nodiscard]] inline glm::vec2 angleVector2 (const double degrees) {
    const double radians = degrees * kDegreesToRadians;

    return {
	static_cast<float> (std::cos (radians)),
	static_cast<float> (std::sin (radians)),
    };
}

/** WEVector.vectorAngle2: angle in degrees for a 2D direction vector. */
[[nodiscard]] inline double vectorAngle2 (const glm::vec2& direction) {
    return std::atan2 (static_cast<double> (direction.y), static_cast<double> (direction.x)) * kRadiansToDegrees;
}
} // namespace WallpaperEngine::Scripting::Modules::VectorMath
