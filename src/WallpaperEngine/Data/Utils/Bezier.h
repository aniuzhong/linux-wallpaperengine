#pragma once

#include <cmath>
#include <optional>

namespace WallpaperEngine::Data::Utils {
/** Evaluates one coordinate of a cubic Bezier at parameter `t`. */
inline float cubicBezier (const float p0, const float p1, const float p2, const float p3, const float t) {
    const float inverse = 1.0f - t;
    return inverse * inverse * inverse * p0 + 3.0f * inverse * inverse * t * p1 + 3.0f * inverse * t * t * p2
	+ t * t * t * p3;
}

/**
 * Solves the cubic Bezier parameter for `x` and evaluates `y` there.
 *
 * Returns nullopt when the control points are not monotonic in x; callers
 * fall back to linear interpolation in that case.
 */
inline std::optional<float> cubicBezierAtX (
    const float x0, const float x1, const float x2, const float x3, const float y0, const float y1, const float y2,
    const float y3, const float x
) {
    if (!(x0 <= x1 && x1 <= x2 && x2 <= x3)) {
	return std::nullopt;
    }

    float lower = 0.0f;
    float upper = 1.0f;

    for (int iteration = 0; iteration < 24; iteration++) {
	const float parameter = (lower + upper) * 0.5f;
	const float value = cubicBezier (x0, x1, x2, x3, parameter);

	if (std::abs (value - x) <= 0.00001f) {
	    lower = upper = parameter;
	    break;
	}

	if (value < x) {
	    lower = parameter;
	} else {
	    upper = parameter;
	}
    }

    return cubicBezier (y0, y1, y2, y3, (lower + upper) * 0.5f);
}
} // namespace WallpaperEngine::Data::Utils
