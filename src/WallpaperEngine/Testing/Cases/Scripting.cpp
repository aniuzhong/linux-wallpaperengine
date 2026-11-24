#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Scripting/EngineObject.h"
#include "WallpaperEngine/Scripting/Modules/VectorMath.h"

using WallpaperEngine::Scripting::resolveFrameTime;
using WallpaperEngine::Scripting::Modules::VectorMath::angleVector2;
using WallpaperEngine::Scripting::Modules::VectorMath::vectorAngle2;

TEST_CASE ("frame time fallback only covers the pre-first-frame zero") {
    // Scripts are initialized before the render loop advances g_Time; a zero
    // delta would make common 1 / frametime logic produce NaN.
    CHECK (resolveFrameTime (0.0f) == Catch::Approx (1.0f / 60.0f));
    CHECK (resolveFrameTime (-1.0f) == Catch::Approx (1.0f / 60.0f));

    // Real high-refresh deltas must pass through unchanged. Clamping these to
    // 16.7 ms made scripts run at 60 Hz on 144/240 Hz outputs.
    CHECK (resolveFrameTime (1.0f / 144.0f) == Catch::Approx (1.0f / 144.0f));
    CHECK (resolveFrameTime (1.0f / 240.0f) == Catch::Approx (1.0f / 240.0f));
    CHECK (resolveFrameTime (1.0f / 30.0f) == Catch::Approx (1.0f / 30.0f));
}

TEST_CASE ("WEVector angleVector2 returns unit directions in degrees") {
    const auto check = [] (const double degrees, const float x, const float y) {
	const glm::vec2 direction = angleVector2 (degrees);

	CHECK (direction.x == Catch::Approx (x).margin (1e-5f));
	CHECK (direction.y == Catch::Approx (y).margin (1e-5f));
    };

    check (0.0, 1.0f, 0.0f);
    check (90.0, 0.0f, 1.0f);
    check (180.0, -1.0f, 0.0f);
    check (270.0, 0.0f, -1.0f);
}

TEST_CASE ("WEVector vectorAngle2 matches the direction convention") {
    CHECK (vectorAngle2 ({ 1.0f, 0.0f }) == Catch::Approx (0.0).margin (1e-5));
    CHECK (vectorAngle2 ({ 0.0f, 1.0f }) == Catch::Approx (90.0).margin (1e-5));
    CHECK (vectorAngle2 ({ -1.0f, 0.0f }) == Catch::Approx (180.0).margin (1e-5));
    CHECK (vectorAngle2 ({ 0.0f, -1.0f }) == Catch::Approx (-90.0).margin (1e-5));

    for (const double degrees : { 30.0, 135.0, -90.0 }) {
	CHECK (vectorAngle2 (angleVector2 (degrees)) == Catch::Approx (degrees).margin (1e-4));
    }
}
