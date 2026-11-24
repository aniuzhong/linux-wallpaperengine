#include "AnimationParser.h"

#include <algorithm>
#include <cctype>
#include <ranges>

#include "WallpaperEngine/Data/Utils/StringParsing.h"

using JSON = WallpaperEngine::Data::JSON::JSON;
using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Data::Parsers;
using namespace WallpaperEngine::Data::Utils;

namespace {
float numberOr (const JSON& object, const std::string& key, const float fallback = 0.0f) {
    const auto value = object.find (key);

    if (value == object.end ()) {
	return fallback;
    }

    if (value->is_number ()) {
	return value->get<float> ();
    }

    if (value->is_string ()) {
	if (const auto parsed = parseFloat (value->get<std::string> ()); parsed.has_value ()) {
	    return parsed.value ();
	}
    }

    return fallback;
}

bool boolOr (const JSON& object, const std::string& key, const bool fallback = false) {
    const auto value = object.find (key);

    if (value == object.end ()) {
	return fallback;
    }

    if (value->is_boolean ()) {
	return value->get<bool> ();
    }

    if (value->is_number ()) {
	return value->get<double> () != 0.0;
    }

    if (value->is_string ()) {
	return parseLegacyBoolean (value->get<std::string> ());
    }

    return fallback;
}

std::string stringOr (const JSON& object, const std::string& key, std::string fallback = {}) {
    const auto value = object.find (key);

    if (value != object.end () && value->is_string ()) {
	return value->get<std::string> ();
    }

    return fallback;
}

AnimationHandle parseHandle (const JSON& keyframe, const std::string& name) {
    const auto handle = keyframe.find (name);

    if (handle == keyframe.end () || !handle->is_object ()) {
	return {};
    }

    return AnimationHandle {
	.enabled = boolOr (*handle, "enabled"),
	.magic = boolOr (*handle, "magic"),
	.x = numberOr (*handle, "x"),
	.y = numberOr (*handle, "y"),
    };
}

AnimationChannel parseChannel (const JSON& data) {
    AnimationChannel result = {};

    if (!data.is_array ()) {
	return result;
    }

    for (const auto& keyframe : data) {
	if (!keyframe.is_object ()) {
	    continue;
	}

	result.keyframes.push_back (
	    AnimationKeyframe {
		.frame = numberOr (keyframe, "frame"),
		.value = numberOr (keyframe, "value"),
		.front = parseHandle (keyframe, "front"),
		.back = parseHandle (keyframe, "back"),
		.step = boolOr (keyframe, "step"),
		.lockAngle = boolOr (keyframe, "lockangle"),
		.lockLength = boolOr (keyframe, "locklength"),
	    }
	);
    }

    std::ranges::sort (result.keyframes, {}, &AnimationKeyframe::frame);
    return result;
}

AnimationMode parseMode (const std::string& value) {
    std::string mode = value;
    std::ranges::transform (mode, mode.begin (), [] (const unsigned char character) {
	return static_cast<char> (std::tolower (character));
    });

    if (mode == "loop" || mode == "repeat") {
	return AnimationMode::Loop;
    }

    if (mode == "mirror") {
	return AnimationMode::Mirror;
    }

    return AnimationMode::Single;
}

void parseEvents (const JSON& options, Animation& result) {
    const auto events = options.find ("events");

    if (events == options.end () || !events->is_array ()) {
	return;
    }

    for (const auto& event : *events) {
	if (!event.is_object ()) {
	    continue;
	}

	result.events.push_back (
	    AnimationEvent {
		.frame = numberOr (event, "frame"),
		.name = stringOr (event, "name"),
	    }
	);
    }
}
} // namespace

namespace WallpaperEngine::Data::Parsers {
Animation AnimationParser::parse (const WallpaperEngine::Data::JSON::JSON& data) {
    Animation result = {};

    if (!data.is_object ()) {
	return result;
    }

    result.relative = boolOr (data, "relative");

    if (const auto options = data.find ("options"); options != data.end () && options->is_object ()) {
	result.fps = numberOr (*options, "fps", 30.0f);
	result.length = numberOr (*options, "length", 0.0f);
	result.mode = parseMode (stringOr (*options, "mode", "single"));
	result.startPaused = boolOr (*options, "startpaused");
	result.wrapLoop = boolOr (*options, "wraploop");
	result.name = stringOr (*options, "name");
	parseEvents (*options, result);
    }

    if (const auto channel = data.find ("c0"); channel != data.end ()) {
	result.channels[0] = parseChannel (*channel);
    }

    if (const auto channel = data.find ("c1"); channel != data.end ()) {
	result.channels[1] = parseChannel (*channel);
    }

    if (const auto channel = data.find ("c2"); channel != data.end ()) {
	result.channels[2] = parseChannel (*channel);
    }

    // Some exports omit options.length or stop it before the last authored
    // frame. Cover both cases so the final keyframe is always reachable.
    for (const auto& channel : result.channels) {
	if (!channel.keyframes.empty ()) {
	    result.length = std::max (result.length, channel.keyframes.back ().frame);
	}
    }

    if (result.fps <= 0.0f) {
	result.fps = 30.0f;
    }

    return result;
}
} // namespace WallpaperEngine::Data::Parsers
