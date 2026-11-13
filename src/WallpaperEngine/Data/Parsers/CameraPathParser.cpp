#include "CameraPathParser.h"

#include <algorithm>

using namespace WallpaperEngine::Data::JSON;
using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Data::Parsers;

namespace {
template <typename T> T numberOr (const JSON& object, const std::string& key, const T fallback = T {}) {
    const auto value = object.find (key);
    return value != object.end () && value->is_number () ? value->get<T> () : fallback;
}

bool boolOr (const JSON& object, const std::string& key, const bool fallback = false) {
    const auto value = object.find (key);
    return value != object.end () && value->is_boolean () ? value->get<bool> () : fallback;
}

std::string stringOr (const JSON& object, const std::string& key, std::string fallback = {}) {
    const auto value = object.find (key);
    return value != object.end () && value->is_string () ? value->get<std::string> () : fallback;
}

float lastTime (const CameraPathChannel& channel) {
    return channel.keyframes.empty () ? 0.0f : channel.keyframes.back ().time;
}

float findDuration (const CameraPath& path) {
    float duration = path.duration;

    for (int channel = 0; channel < 3; channel++) {
	duration = std::max (duration, lastTime (path.center[channel]));
	duration = std::max (duration, lastTime (path.eye[channel]));
	duration = std::max (duration, lastTime (path.up[channel]));
    }

    duration = std::max (duration, lastTime (path.fov));
    duration = std::max (duration, lastTime (path.zoom));
    return duration;
}

CameraPath parseLegacyPath (const JSON& data) {
    CameraPath result {
	.name = stringOr (data, "name"),
	.duration = numberOr (data, "duration", 0.0f),
    };

    const auto transforms = data.find ("transforms");

    if (transforms == data.end () || !transforms->is_array ()) {
	return result;
    }

    // Legacy exports are normally chronological, but normalize the order so
    // every generated channel is ascending without sorting them individually.
    std::vector<const JSON*> ordered;
    ordered.reserve (transforms->size ());

    for (const auto& transform : *transforms) {
	if (transform.is_object ()) {
	    ordered.push_back (&transform);
	}
    }

    std::ranges::sort (ordered, {}, [] (const JSON* transform) { return numberOr (*transform, "timestamp", 0.0f); });

    for (const JSON* transform : ordered) {
	const float time = numberOr (*transform, "timestamp", 0.0f);
	const auto center = transform->require<glm::vec3> ("center", "Camera path keyframe must have a center");
	const auto eye = transform->require<glm::vec3> ("eye", "Camera path keyframe must have an eye");
	const auto up = transform->require<glm::vec3> ("up", "Camera path keyframe must have an up");

	for (int channel = 0; channel < 3; channel++) {
	    result.center[channel].keyframes.push_back ({ .time = time, .value = center[channel] });
	    result.eye[channel].keyframes.push_back ({ .time = time, .value = eye[channel] });
	    result.up[channel].keyframes.push_back ({ .time = time, .value = up[channel] });
	}

	if (const auto fov = transform->find ("fov"); fov != transform->end () && fov->is_number ()) {
	    result.fov.keyframes.push_back ({ .time = time, .value = fov->get<float> () });
	}

	if (const auto zoom = transform->find ("zoom"); zoom != transform->end () && zoom->is_number ()) {
	    result.zoom.keyframes.push_back ({ .time = time, .value = zoom->get<float> () });
	}
    }

    result.duration = findDuration (result);
    return result;
}

CameraPathHandle parseHandle (const JSON& keyframe, const std::string& name) {
    const auto handle = keyframe.find (name);

    if (handle == keyframe.end () || !handle->is_object ()) {
	return {};
    }

    return CameraPathHandle {
	.enabled = boolOr (*handle, "enabled"),
	.offset = glm::vec2 (numberOr (*handle, "x", 0.0f), numberOr (*handle, "y", 0.0f)),
    };
}

CameraPathChannel parseChannel (const JSON& data, const float fps) {
    CameraPathChannel result = {};
    const float safeFps = fps > 0.0f ? fps : 1.0f;

    if (!data.is_array ()) {
	return result;
    }

    for (const auto& keyframe : data) {
	if (!keyframe.is_object ()) {
	    continue;
	}

	result.keyframes.push_back (
	    CameraPathKeyframe {
		.time = numberOr (keyframe, "frame", 0.0f) / safeFps,
		.value = numberOr (keyframe, "value", 0.0f),
		.incoming = parseHandle (keyframe, "back"),
		.outgoing = parseHandle (keyframe, "front"),
	    }
	);
    }

    std::ranges::sort (result.keyframes, {}, &CameraPathKeyframe::time);
    return result;
}

void parseVectorChannels (const JSON& data, const float fps, std::array<CameraPathChannel, 3>& output) {
    if (!data.is_object ()) {
	return;
    }

    for (std::size_t channel = 0; channel < output.size (); channel++) {
	const auto value = data.find ("c" + std::to_string (channel));

	if (value != data.end ()) {
	    output[channel] = parseChannel (*value, fps);
	}
    }
}

CameraPath parseCurvePath (const JSON& data) {
    float fps = 30.0f;
    float length = 0.0f;

    if (const auto options = data.find ("options"); options != data.end () && options->is_object ()) {
	fps = numberOr (*options, "fps", fps);
	length = numberOr (*options, "length", length);
    }

    CameraPath result {
	.name = stringOr (data, "name"),
	.duration = fps > 0.0f ? length / fps : 0.0f,
    };

    if (const auto center = data.find ("center"); center != data.end ()) {
	parseVectorChannels (*center, fps, result.center);
    }

    if (const auto eye = data.find ("eye"); eye != data.end ()) {
	parseVectorChannels (*eye, fps, result.eye);
    }

    if (const auto up = data.find ("up"); up != data.end ()) {
	parseVectorChannels (*up, fps, result.up);
    }

    if (const auto fov = data.find ("fov"); fov != data.end ()) {
	result.fov = parseChannel (*fov, fps);
    }

    if (const auto zoom = data.find ("zoom"); zoom != data.end ()) {
	result.zoom = parseChannel (*zoom, fps);
    }

    result.duration = findDuration (result);
    return result;
}
} // namespace

std::vector<CameraPath> CameraPathParser::parse (const WallpaperEngine::Data::JSON::JSON& data) {
    std::vector<CameraPath> result = {};

    const auto paths = data.find ("paths");

    if (paths == data.end () || !paths->is_array ()) {
	return result;
    }

    for (const auto& path : *paths) {
	if (!path.is_object () || !boolOr (path, "visible", true)) {
	    continue;
	}

	CameraPath parsed = path.find ("transforms") != path.end () ? parseLegacyPath (path) : parseCurvePath (path);

	if (parsed.duration > 0.0f) {
	    result.push_back (std::move (parsed));
	}
    }

    return result;
}
