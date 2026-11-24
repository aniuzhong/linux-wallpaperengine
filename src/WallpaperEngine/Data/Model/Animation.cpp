#include "Animation.h"

#include <algorithm>
#include <cmath>
#include <ranges>

#include <glm/common.hpp>

#include "WallpaperEngine/Data/Utils/Bezier.h"

using namespace WallpaperEngine::Data::Model;
using WallpaperEngine::Data::Utils::cubicBezierAtX;

namespace {
float evaluateSegment (const AnimationKeyframe& previous, const AnimationKeyframe& next, const float frame) {
    const float span = next.frame - previous.frame;

    if (span <= 0.0f || frame >= next.frame) {
	return next.value;
    }

    if (next.step) {
	return previous.value;
    }

    // Wallpaper Engine normalises the handle time coordinate to half the
    // segment duration and stores the value offset directly.
    const float p0x = previous.frame;
    const float p1x = p0x + (previous.front.enabled ? previous.front.x * span * 0.5f : 0.0f);
    const float p2x = next.frame + (next.back.enabled ? next.back.x * span * 0.5f : 0.0f);
    const float p3x = next.frame;
    const float p0y = previous.value;
    const float p1y = p0y + (previous.front.enabled ? previous.front.y : 0.0f);
    const float p2y = next.value + (next.back.enabled ? next.back.y : 0.0f);
    const float p3y = next.value;

    if (const auto value = cubicBezierAtX (p0x, p1x, p2x, p3x, p0y, p1y, p2y, p3y, frame)) {
	return value.value ();
    }

    return glm::mix (previous.value, next.value, (frame - previous.frame) / span);
}
} // namespace

float AnimationChannel::evaluate (const float frame, const float fallback, const float wrapLength) const {
    if (this->keyframes.empty ()) {
	return fallback;
    }

    const AnimationKeyframe& first = this->keyframes.front ();
    const AnimationKeyframe& last = this->keyframes.back ();

    // wrapLoop interpolates across the loop boundary when the authored
    // keyframes do not span the full timeline.
    if (wrapLength > 0.0f) {
	if (frame < first.frame) {
	    AnimationKeyframe previous = last;
	    previous.frame -= wrapLength;

	    if (previous.frame < first.frame) {
		return evaluateSegment (previous, first, frame);
	    }
	} else if (frame > last.frame) {
	    AnimationKeyframe next = first;
	    next.frame += wrapLength;

	    if (next.frame > last.frame) {
		return evaluateSegment (last, next, frame);
	    }
	}
    }

    if (frame <= first.frame) {
	return first.value;
    }

    if (frame >= last.frame) {
	return last.value;
    }

    for (size_t index = 1; index < this->keyframes.size (); index++) {
	const AnimationKeyframe& previous = this->keyframes[index - 1];
	const AnimationKeyframe& next = this->keyframes[index];

	if (frame > next.frame) {
	    continue;
	}

	return evaluateSegment (previous, next, frame);
    }

    return last.value;
}

float Animation::duration () const {
    if (this->length <= 0.0f || this->fps <= 0.0f) {
	return 0.0f;
    }

    return this->length / this->fps;
}

bool Animation::empty () const {
    return std::ranges::all_of (this->channels, [] (const AnimationChannel& channel) {
	return channel.keyframes.empty ();
    });
}

glm::vec3 Animation::evaluate (const float seconds, const glm::vec3& fallback) const {
    const float animationDuration = this->duration ();

    if (animationDuration <= 0.0f) {
	return fallback;
    }

    const auto wrap = [animationDuration] (float value) {
	value = std::fmod (value, animationDuration);
	return value < 0.0f ? value + animationDuration : value;
    };

    float time = seconds;

    switch (this->mode) {
	case AnimationMode::Single:
	    if (this->wrapLoop) {
		time = wrap (time);
	    } else {
		time = glm::clamp (time, 0.0f, animationDuration);
	    }
	    break;
	case AnimationMode::Loop:
	    time = wrap (time);
	    break;
	case AnimationMode::Mirror:
	    {
		const float cycle = animationDuration * 2.0f;
		time = std::fmod (time, cycle);

		if (time < 0.0f) {
		    time += cycle;
		}

		// Wallpaper Engine plays the authored curve forwards for one full
		// duration, then backwards for another full duration.
		if (time > animationDuration) {
		    time = cycle - time;
		}
		break;
	    }
    }

    const float frame = time * this->fps;
    const float wrapLength = this->wrapLoop ? this->length : 0.0f;

    return glm::vec3 {
	this->channels[0].evaluate (frame, fallback.x, wrapLength),
	this->channels[1].evaluate (frame, fallback.y, wrapLength),
	this->channels[2].evaluate (frame, fallback.z, wrapLength),
    };
}
