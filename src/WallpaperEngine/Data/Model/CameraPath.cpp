#include "CameraPath.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include "WallpaperEngine/Data/Utils/Bezier.h"

using namespace WallpaperEngine::Data::Model;
using WallpaperEngine::Data::Utils::cubicBezierAtX;

float CameraPathChannel::evaluate (const float time, const float fallback) const {
    if (this->keyframes.empty ()) {
	return fallback;
    }

    if (time <= this->keyframes.front ().time) {
	return this->keyframes.front ().value;
    }

    if (time >= this->keyframes.back ().time) {
	return this->keyframes.back ().value;
    }

    for (size_t index = 1; index < this->keyframes.size (); index++) {
	const CameraPathKeyframe& previous = this->keyframes[index - 1];
	const CameraPathKeyframe& next = this->keyframes[index];

	if (time > next.time) {
	    continue;
	}

	const float span = next.time - previous.time;

	if (span <= 0.0f) {
	    return next.value;
	}

	if (!previous.outgoing.enabled || !next.incoming.enabled) {
	    const float amount = (time - previous.time) / span;
	    return glm::mix (previous.value, next.value, amount);
	}

	// The editor stores a cubic Bezier in the time/value plane. Find the
	// curve parameter for the requested time, then evaluate its value.
	const auto value = cubicBezierAtX (
	    previous.time, previous.time + previous.outgoing.offset.x, next.time + next.incoming.offset.x, next.time,
	    previous.value, previous.value + previous.outgoing.offset.y, next.value + next.incoming.offset.y,
	    next.value, time
	);

	if (value.has_value ()) {
	    return value.value ();
	}

	return glm::mix (previous.value, next.value, (time - previous.time) / span);
    }

    return this->keyframes.back ().value;
}

CameraTransform CameraPath::evaluate (const float time, const CameraTransform& fallback) const {
    CameraTransform result = fallback;

    for (int channel = 0; channel < 3; channel++) {
	result.center[channel] = this->center[channel].evaluate (time, result.center[channel]);
	result.eye[channel] = this->eye[channel].evaluate (time, result.eye[channel]);
	result.up[channel] = this->up[channel].evaluate (time, result.up[channel]);
    }

    result.fov = this->fov.evaluate (time, result.fov);
    result.zoom = this->zoom.evaluate (time, result.zoom);

    if (glm::dot (result.up, result.up) > 0.000001f) {
	result.up = glm::normalize (result.up);
    } else {
	result.up = fallback.up;
    }

    return result;
}
