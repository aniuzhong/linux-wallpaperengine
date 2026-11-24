#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include <glm/vec4.hpp>

#include "WallpaperEngine/Render/CameraProjection.h"

using WallpaperEngine::Render::CameraProjection::buildOrthogonalProjection;
using WallpaperEngine::Render::CameraProjection::buildPerspectiveProjection;

TEST_CASE ("3047596375 (Starscape) perspective camera uses the authored projection") {
    // scene.json general: fov = 50, nearz = 0.01, farz = 11;
    // orthogonalprojection is null, so Starscape takes the perspective path.
    constexpr float fov = 50.0f;
    constexpr float nearz = 0.01f;
    constexpr float farz = 11.0f;
    // The scene has no authored aspect; this one comes from the output.
    constexpr float outputAspect = 16.0f / 9.0f;

    const glm::mat4 projection = buildPerspectiveProjection (fov, outputAspect, nearz, farz, false);
    const float tanHalfFov = std::tan (glm::radians (fov) / 2.0f);

    CHECK (projection[0][0] == Catch::Approx (1.0f / (tanHalfFov * outputAspect)));
    CHECK (projection[1][1] == Catch::Approx (1.0f / tanHalfFov));
    CHECK (projection[2][2] == Catch::Approx (-(farz + nearz) / (farz - nearz)));
    CHECK (projection[3][2] == Catch::Approx (-(2.0f * farz * nearz) / (farz - nearz)));
}

TEST_CASE ("3047596375 (Starscape) perspective Y flip mirrors only the Y axis") {
    // The projection flip stores the scene FBO in the uniform presentation
    // convention shared with video textures, so every output renders scenes
    // Y-flipped; per-output present-time compensation lives in the composite
    // pass (see Output::renderVFlip).
    constexpr float fov = 50.0f;
    constexpr float nearz = 0.01f;
    constexpr float farz = 11.0f;
    constexpr float outputAspect = 16.0f / 9.0f;

    const glm::mat4 normal = buildPerspectiveProjection (fov, outputAspect, nearz, farz, false);
    const glm::mat4 flipped = buildPerspectiveProjection (fov, outputAspect, nearz, farz, true);

    CHECK (flipped[1][1] == Catch::Approx (-normal[1][1]));
    CHECK (flipped[0][0] == Catch::Approx (normal[0][0]));
    CHECK (flipped[2][2] == Catch::Approx (normal[2][2]));
    CHECK (flipped[3][2] == Catch::Approx (normal[3][2]));
}

TEST_CASE ("3042492564 (Hoshi-Tele) camera object zoom scales the projection axes") {
    // CameraObject 319 authors zoom 0.73. The projection helper must scale both
    // axes without touching the near/far terms.
    constexpr float fov = 50.0f;
    constexpr float nearz = 0.01f;
    constexpr float farz = 10000.0f;
    constexpr float aspect = 16.0f / 9.0f;
    constexpr float zoom = 0.73f;

    const glm::mat4 normal = buildPerspectiveProjection (fov, aspect, nearz, farz, false, 1.0f);
    const glm::mat4 zoomed = buildPerspectiveProjection (fov, aspect, nearz, farz, false, zoom);

    CHECK (zoomed[0][0] == Catch::Approx (zoom * normal[0][0]));
    CHECK (zoomed[1][1] == Catch::Approx (zoom * normal[1][1]));
    CHECK (zoomed[2][2] == Catch::Approx (normal[2][2]));
    CHECK (zoomed[3][2] == Catch::Approx (normal[3][2]));
}

TEST_CASE ("2D camera zoom shrinks the visible orthographic window") {
    // CameraTransforms.zoom is a 2D-only relative zoom. A 2x zoom must show
    // half the authored world width without moving the centre.
    constexpr float width = 1920.0f;
    constexpr float height = 1080.0f;

    const glm::mat4 normal = buildOrthogonalProjection (width, height, -1.0f, 1.0f, glm::vec3 (0.0f), 1.0f);
    const glm::mat4 zoomed = buildOrthogonalProjection (width, height, -1.0f, 1.0f, glm::vec3 (0.0f), 2.0f);

    const auto projectX
	= [] (const glm::mat4& matrix, const float x) { return (matrix * glm::vec4 (x, 0.0f, 0.0f, 1.0f)).x; };

    CHECK (projectX (normal, 480.0f) == Catch::Approx (0.5f));
    CHECK (projectX (zoomed, 480.0f) == Catch::Approx (1.0f));
    CHECK (projectX (zoomed, 240.0f) == Catch::Approx (0.5f));
}

TEST_CASE ("2D camera zoom keeps the authored eye as the projection origin") {
    // The orthographic projection's translation is the fixed authored eye;
    // animated/scripted eye values are applied through the lookAt matrix.
    constexpr float width = 1920.0f;
    constexpr float height = 1080.0f;
    const glm::vec3 eye (100.0f, 50.0f, 0.0f);

    const glm::mat4 normal = buildOrthogonalProjection (width, height, -1.0f, 1.0f, eye, 1.0f);
    const glm::mat4 zoomed = buildOrthogonalProjection (width, height, -1.0f, 1.0f, eye, 2.0f);

    const auto projectX
	= [] (const glm::mat4& matrix, const float x) { return (matrix * glm::vec4 (x, 0.0f, 0.0f, 1.0f)).x; };

    // The point that cancels the authored eye stays at the projection centre.
    CHECK (projectX (normal, -eye.x) == Catch::Approx (0.0f));
    CHECK (projectX (zoomed, -eye.x) == Catch::Approx (0.0f));

    // A fixed world offset is scaled by the zoom.
    CHECK (projectX (normal, -eye.x + 240.0f) == Catch::Approx (0.25f));
    CHECK (projectX (zoomed, -eye.x + 240.0f) == Catch::Approx (0.5f));
}
