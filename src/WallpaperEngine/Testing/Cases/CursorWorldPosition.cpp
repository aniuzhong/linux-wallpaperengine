// Include the project header first: it pulls in CEF, which defines a CHECK macro
// that would otherwise replace Catch2's assertion macro.
#include "WallpaperEngine/Render/Wallpapers/CScene.h"
#undef CHECK

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using WallpaperEngine::Render::Wallpapers::CScene;

namespace {
constexpr float kSceneWidth = 1920.0f;
constexpr float kSceneHeight = 1080.0f;

glm::vec3 cursorWorld (float mouseX, float mouseY, float shownWidth, float shownHeight) {
    return CScene::mapMousePositionToWorld (
	{ mouseX, mouseY }, 0.0f, 1.0f, 0.0f, 1.0f, kSceneWidth, kSceneHeight, shownWidth, shownHeight
    );
}
} // namespace

TEST_CASE ("cursor world position matches layer origins on a 16:9 output") {
    // Space Journey projects at 1920x1080; on a 16:9 output the camera window is
    // uncropped, so the old projection-sized mapping is already correct.
    for (const auto& [mouse, expected] :
	 { std::pair { glm::vec2 { 0.0f, 0.0f }, glm::vec3 { 0.0f, 1080.0f, 0.0f } },
	   std::pair { glm::vec2 { 0.5f, 0.5f }, glm::vec3 { 960.0f, 540.0f, 0.0f } },
	   std::pair { glm::vec2 { 1.0f, 1.0f }, glm::vec3 { 1920.0f, 0.0f, 0.0f } } }) {
	const glm::vec3 result = cursorWorld (mouse.x, mouse.y, kSceneWidth, kSceneHeight);

	CHECK (result.x == Catch::Approx (expected.x));
	CHECK (result.y == Catch::Approx (expected.y));
    }
}

TEST_CASE ("cursor world position accounts for horizontal camera cropping") {
    // A 16:10 output covers a 16:9 scene by cropping 96 scene units on each side:
    // the camera shows 1728x1080 world units, centred on the authored projection.
    constexpr float shownWidth = 1728.0f;
    constexpr float shownHeight = 1080.0f;

    const glm::vec3 left = cursorWorld (0.0f, 0.5f, shownWidth, shownHeight);
    const glm::vec3 centre = cursorWorld (0.5f, 0.5f, shownWidth, shownHeight);
    const glm::vec3 right = cursorWorld (1.0f, 0.5f, shownWidth, shownHeight);

    CHECK (left.x == Catch::Approx (96.0f));
    CHECK (centre.x == Catch::Approx (960.0f));
    CHECK (right.x == Catch::Approx (1824.0f));

    // Vertical is uncropped and stays identical to the 16:9 mapping.
    CHECK (left.y == Catch::Approx (540.0f));
    CHECK (right.y == Catch::Approx (540.0f));
}

TEST_CASE ("cursor world position accounts for vertical camera cropping") {
    // An ultrawide output covers the same scene by cropping the camera window to
    // 1920x600: the visible y range is centred on the authored projection and
    // keeps the same orientation as layer origins.
    constexpr float shownWidth = 1920.0f;
    constexpr float shownHeight = 600.0f;

    const glm::vec3 top = cursorWorld (0.5f, 0.0f, shownWidth, shownHeight);
    const glm::vec3 centre = cursorWorld (0.5f, 0.5f, shownWidth, shownHeight);
    const glm::vec3 bottom = cursorWorld (0.5f, 1.0f, shownWidth, shownHeight);

    CHECK (top.x == Catch::Approx (960.0f));
    CHECK (top.y == Catch::Approx (840.0f));
    CHECK (centre.y == Catch::Approx (540.0f));
    CHECK (bottom.y == Catch::Approx (240.0f));
}

TEST_CASE ("cursor world position undoes a cropped UV range") {
    // updateMouse stores the mouse in scene UV space. When the render target only
    // shows a sub-rectangle, mapMousePositionToWorld has to recover the viewport
    // fraction before scaling, otherwise the cursor drifts horizontally.
    const float ustart = 0.1f;
    const float uend = 0.9f;
    const float mouseX = 0.25f;

    const glm::vec3 result = CScene::mapMousePositionToWorld (
	{ ustart + mouseX * (uend - ustart), 0.5f }, ustart, uend, 0.0f, 1.0f, kSceneWidth, kSceneHeight, kSceneWidth,
	kSceneHeight
    );

    CHECK (result.x == Catch::Approx (480.0f));
}
