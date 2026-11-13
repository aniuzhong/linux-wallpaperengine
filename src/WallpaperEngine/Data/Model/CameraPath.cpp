#include "CameraPath.h"

#include <algorithm>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

using namespace WallpaperEngine::Data::Model;

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

	// Legacy transforms and modern linear curves use straight interpolation.
	// The incoming/outgoing Bezier handles are already stored on the keyframes;
	// the curve evaluation will be added together with the modern editor parser.
	const float amount = (time - previous.time) / span;
	return glm::mix (previous.value, next.value, amount);
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

    result.fov = glm::clamp (result.fov, 1.0f, 179.0f);
    result.zoom = glm::max (result.zoom, 0.0001f);
    return result;
}
