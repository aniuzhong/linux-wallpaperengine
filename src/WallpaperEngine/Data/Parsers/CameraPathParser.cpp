#include "CameraPathParser.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "WallpaperEngine/Logging/Log.h"

using namespace WallpaperEngine::Data::JSON;
using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Data::Parsers;

namespace {
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

float keyframeTime (const JSON& transform) {
    const auto timestamp = transform.find ("timestamp");
    return timestamp != transform.end () && timestamp->is_number () ? timestamp->get<float> () : 0.0f;
}

CameraPath parseLegacyPath (const JSON& data) {
    CameraPath result {
	.name = "",
	.duration = 0.0f,
    };

    if (const auto name = data.find ("name"); name != data.end () && name->is_string ()) {
	result.name = name->get<std::string> ();
    }

    if (const auto duration = data.find ("duration"); duration != data.end () && duration->is_number ()) {
	result.duration = duration->get<float> ();
    }

    const auto transforms = data.find ("transforms");

    if (transforms == data.end () || !transforms->is_array ()) {
	return result;
    }

    std::vector<std::size_t> order (transforms->size ());
    std::iota (order.begin (), order.end (), 0);

    // Legacy exports are normally chronological, but normalize the access
    // order so evaluate() can rely on ascending timestamps.
    std::ranges::sort (order, {}, [&transforms] (std::size_t index) { return keyframeTime ((*transforms)[index]); });

    for (const std::size_t index : order) {
	const auto& transform = (*transforms)[index];

	if (!transform.is_object ()) {
	    continue;
	}

	const float time = keyframeTime (transform);
	const auto center = transform.require<glm::vec3> ("center", "Camera path keyframe must have a center");
	const auto eye = transform.require<glm::vec3> ("eye", "Camera path keyframe must have an eye");
	const auto up = transform.require<glm::vec3> ("up", "Camera path keyframe must have an up");

	for (int channel = 0; channel < 3; channel++) {
	    result.center[channel].keyframes.push_back ({ .time = time, .value = center[channel] });
	    result.eye[channel].keyframes.push_back ({ .time = time, .value = eye[channel] });
	    result.up[channel].keyframes.push_back ({ .time = time, .value = up[channel] });
	}

	if (const auto fov = transform.find ("fov"); fov != transform.end () && fov->is_number ()) {
	    result.fov.keyframes.push_back ({ .time = time, .value = fov->get<float> () });
	}

	if (const auto zoom = transform.find ("zoom"); zoom != transform.end () && zoom->is_number ()) {
	    result.zoom.keyframes.push_back ({ .time = time, .value = zoom->get<float> () });
	}
    }

    result.duration = findDuration (result);
    return result;
}

bool looksLikeCurvePath (const JSON& path) {
    return path.find ("options") != path.end () || path.find ("center") != path.end ()
	|| path.find ("eye") != path.end () || path.find ("up") != path.end () || path.find ("fov") != path.end ()
	|| path.find ("zoom") != path.end ();
}
} // namespace

std::vector<CameraPath> CameraPathParser::parse (const WallpaperEngine::Data::JSON::JSON& data) {
    std::vector<CameraPath> result = {};
    bool warnedModernCurve = false;

    const auto paths = data.find ("paths");

    if (paths == data.end () || !paths->is_array ()) {
	return result;
    }

    for (const auto& path : *paths) {
	if (!path.is_object ()) {
	    continue;
	}

	if (const auto visible = path.find ("visible");
	    visible != path.end () && visible->is_boolean () && !visible->get<bool> ()) {
	    continue;
	}

	if (path.find ("transforms") != path.end ()) {
	    CameraPath parsed = parseLegacyPath (path);

	    if (parsed.duration > 0.0f) {
		result.push_back (std::move (parsed));
	    }

	    continue;
	}

	// The newer editor stores per-channel curves (options.fps/length,
	// center/eye/up c0..c2 channels, fov/zoom and front/back Bezier handles).
	// PR #632's CameraPathParser/CameraPath design is the reference for that;
	// keep the warning until a real wallpaper needs it and a fixture exists.
	if (looksLikeCurvePath (path)) {
	    if (!warnedModernCurve) {
		sLog.error ("Modern camera curve paths are not supported yet; using the resting camera pose");
		warnedModernCurve = true;
	    }
	}
    }

    return result;
}
