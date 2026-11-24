#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/constants.hpp>

#include "WallpaperEngine/Data/JSON.h"
#include "WallpaperEngine/Data/Model/Animation.h"
#include "WallpaperEngine/Data/Model/UserSetting.h"
#include "WallpaperEngine/Data/Parsers/AnimationParser.h"
#include "WallpaperEngine/Data/Parsers/UserSettingParser.h"
#include "WallpaperEngine/Render/AnimationRuntime.h"

using namespace WallpaperEngine::Data::JSON;
using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Data::Parsers;
using namespace WallpaperEngine::Render;

namespace {
constexpr const char* kStarscapeAnimation = R"({
    "c0": [
        {"frame": 0, "value": 0, "front": {"enabled": true, "magic": true, "x": 1, "y": 0},
         "back": {"enabled": true, "magic": true, "x": -1, "y": 0}, "lockangle": true, "locklength": true},
        {"frame": 1800, "value": 0, "front": {"enabled": true, "magic": true, "x": 1, "y": 0},
         "back": {"enabled": true, "magic": true, "x": -1, "y": 0}, "lockangle": true, "locklength": true}
    ],
    "c1": [
        {"frame": 0, "value": 0, "front": {"enabled": true, "magic": true, "x": 1, "y": 0},
         "back": {"enabled": true, "magic": true, "x": -1, "y": 0}, "lockangle": true, "locklength": true},
        {"frame": 1800, "value": 6.2831855, "front": {"enabled": true, "magic": true, "x": 1, "y": 0},
         "back": {"enabled": true, "magic": true, "x": -1, "y": 0}, "lockangle": true, "locklength": true}
    ],
    "c2": [
        {"frame": 0, "value": 0, "front": {"enabled": true, "magic": true, "x": 1, "y": 0},
         "back": {"enabled": true, "magic": true, "x": -1, "y": 0}, "lockangle": true, "locklength": true},
        {"frame": 1800, "value": 0, "front": {"enabled": true, "magic": true, "x": 1, "y": 0},
         "back": {"enabled": true, "magic": true, "x": -1, "y": 0}, "lockangle": true, "locklength": true}
    ],
    "options": {"fps": 30, "length": 1800, "mode": "mirror", "wraploop": null},
    "relative": true
})";
} // namespace

TEST_CASE ("timeline animation parses the Starscape angle curve") {
    const Animation animation = AnimationParser::parse (JSON::parse (kStarscapeAnimation));

    CHECK (animation.duration () == Catch::Approx (60.0f));
    CHECK (animation.mode == AnimationMode::Mirror);
    CHECK (animation.relative);
    CHECK_FALSE (animation.startPaused);
    CHECK_FALSE (animation.wrapLoop);
    CHECK (animation.name.empty ());
    CHECK (animation.events.empty ());
    CHECK_FALSE (animation.empty ());

    const glm::vec3 fallback { 0.0f };

    CHECK (animation.evaluate (0.0f, fallback).y == Catch::Approx (0.0f));
    // x = +/-1, y = 0 places both control points at the middle of the
    // segment, so the curve is an ease and not a straight line.
    CHECK (animation.evaluate (15.0f, fallback).y == Catch::Approx (0.6653425f).margin (0.0001f));
    CHECK (animation.evaluate (30.0f, fallback).y == Catch::Approx (glm::pi<float> ()));
    CHECK (
	animation.evaluate (45.0f, fallback).y == Catch::Approx (2.0f * glm::pi<float> () - 0.6653425f).margin (0.0001f)
    );
    CHECK (animation.evaluate (60.0f, fallback).y == Catch::Approx (2.0f * glm::pi<float> ()));
    // Mirror mode plays the authored curve backwards for another 60 seconds.
    CHECK (animation.evaluate (90.0f, fallback).y == Catch::Approx (glm::pi<float> ()));
    CHECK (animation.evaluate (120.0f, fallback).y == Catch::Approx (0.0f));
}

TEST_CASE ("timeline animation evaluates Bezier handles") {
    const Animation animation = AnimationParser::parse (JSON::parse (R"({
        "c0": [
            {"frame": 0, "value": 0, "front": {"enabled": true, "x": 1, "y": 10}},
            {"frame": 100, "value": 0}
        ],
        "options": {"fps": 30, "length": 100, "mode": "single"}
    })"));

    CHECK (animation.evaluate (50.0f / 30.0f, {}).x == Catch::Approx (4.438677f).margin (0.0001f));
}

TEST_CASE ("timeline animation single and loop modes clamp or wrap time") {
    const auto makeAnimation = [] (const std::string& mode) {
	return AnimationParser::parse (
	    JSON::parse (
		std::string (R"({
            "c0": [
                {"frame": 0, "value": 0},
                {"frame": 60, "value": 10}
            ],
            "options": {"fps": 30, "length": 60, "mode": ")")
		+ mode + R"("}
        })"
	    )
	);
    };

    const glm::vec3 fallback { 0.0f };

    const Animation single = makeAnimation ("single");
    CHECK (single.evaluate (1.0f, fallback).x == Catch::Approx (5.0f));
    CHECK (single.evaluate (2.0f, fallback).x == Catch::Approx (10.0f));
    CHECK (single.evaluate (3.0f, fallback).x == Catch::Approx (10.0f));

    const Animation loop = makeAnimation ("loop");
    CHECK (loop.evaluate (1.0f, fallback).x == Catch::Approx (5.0f));
    CHECK (loop.evaluate (2.0f, fallback).x == Catch::Approx (0.0f));
    CHECK (loop.evaluate (3.0f, fallback).x == Catch::Approx (5.0f));

    const Animation repeat = makeAnimation ("repeat");
    CHECK (repeat.mode == AnimationMode::Loop);
    CHECK (repeat.evaluate (2.0f, fallback).x == Catch::Approx (0.0f));
}

TEST_CASE ("timeline animation supports step keyframes") {
    const Animation animation = AnimationParser::parse (JSON::parse (R"({
        "c0": [
            {"frame": 0, "value": 2},
            {"frame": 10, "value": 8, "step": true}
        ],
        "options": {"fps": 30, "length": 10, "mode": "single"}
    })"));

    CHECK (animation.evaluate (9.0f / 30.0f, {}).x == Catch::Approx (2.0f));
    CHECK (animation.evaluate (10.0f / 30.0f, {}).x == Catch::Approx (8.0f));
}

TEST_CASE ("timeline animation wraps across the loop boundary") {
    const Animation animation = AnimationParser::parse (JSON::parse (R"({
        "c0": [
            {"frame": 0, "value": 10},
            {"frame": 2, "value": 20}
        ],
        "options": {"fps": 1, "length": 4, "mode": "loop", "wraploop": true}
    })"));

    CHECK (animation.evaluate (0.0f, {}).x == Catch::Approx (10.0f));
    CHECK (animation.evaluate (2.0f, {}).x == Catch::Approx (20.0f));
    // Between the last keyframe and the loop end the curve interpolates
    // towards the first keyframe instead of holding the last value.
    CHECK (animation.evaluate (3.0f, {}).x == Catch::Approx (15.0f));
    CHECK (animation.evaluate (4.0f, {}).x == Catch::Approx (10.0f));
}

TEST_CASE ("timeline animation duration covers the last authored frame") {
    const Animation animation = AnimationParser::parse (JSON::parse (R"({
        "c0": [
            {"frame": 0, "value": 0},
            {"frame": 20, "value": 10}
        ],
        "options": {"fps": 10, "length": 10, "mode": "single"}
    })"));

    CHECK (animation.duration () == Catch::Approx (2.0f));
}

TEST_CASE ("timeline animation can be attached to a UserSetting") {
    const auto setting = UserSettingParser::parse (
	JSON::parse (std::string ("{\"value\":\"0 0 0\",\"animation\":") + kStarscapeAnimation + "}"), {}
    );

    REQUIRE (setting->animation.has_value ());
    CHECK (setting->animation->duration () == Catch::Approx (60.0f));
    CHECK (setting->value->getVec3 ().y == Catch::Approx (0.0f));
}

TEST_CASE ("animation runtime writes relative values into the setting") {
    auto setting = UserSettingParser::parse (
	JSON::parse (std::string ("{\"value\":\"1 2 3\",\"animation\":") + kStarscapeAnimation + "}"), {}
    );

    AnimationRuntime runtime;
    runtime.registerSetting (*setting);

    runtime.tick (0.0f);
    CHECK (setting->value->getVec3 ().y == Catch::Approx (2.0f));

    // AnimationRuntime caps a single delta; advance in normal frame-sized steps.
    for (int frame = 0; frame < 120; frame++) {
	runtime.tick (static_cast<float> (frame + 1) * 0.25f);
    }

    CHECK (setting->value->getVec3 ().x == Catch::Approx (1.0f));
    CHECK (setting->value->getVec3 ().y == Catch::Approx (2.0f + glm::pi<float> ()));
    CHECK (setting->value->getVec3 ().z == Catch::Approx (3.0f));
}

TEST_CASE ("animation runtime plays named, non-paused animations") {
    auto setting = UserSettingParser::parse (
	JSON::parse (R"({
        "value": "0 0 0",
        "animation": {
            "c0": [
                {"frame": 0, "value": 0},
                {"frame": 30, "value": 0}
            ],
            "c1": [
                {"frame": 0, "value": 0, "front": {"enabled": true, "x": 0.5, "y": 0}},
                {"frame": 30, "value": 2, "back": {"enabled": true, "x": -0.5, "y": 0}}
            ],
            "c2": [
                {"frame": 0, "value": 0},
                {"frame": 30, "value": 0}
            ],
            "options": {"fps": 30, "length": 30, "mode": "single", "name": "named"},
            "relative": true
        }
    })"),
	{}
    );

    AnimationRuntime runtime;
    runtime.registerSetting (*setting);

    runtime.tick (0.0f);

    for (int step = 0; step < 5; step++) {
	runtime.tick (static_cast<float> (step + 1) * 0.1f);
    }

    CHECK (setting->value->getVec3 ().y == Catch::Approx (1.0f));
}
