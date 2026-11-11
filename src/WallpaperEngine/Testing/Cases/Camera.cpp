#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "WallpaperEngine/Render/CameraProjection.h"

using WallpaperEngine::Render::CameraProjection::buildPerspectiveProjection;
using WallpaperEngine::Render::CameraProjection::resolvePerspectiveNearZ;

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

TEST_CASE ("Perspective near plane falls back when the parser default nearz is 0") {
    // The parser defaults nearz to 0.0; the perspective path needs a positive
    // near plane, so 0.0 resolves to 0.01.
    CHECK (resolvePerspectiveNearZ (0.0f) == Catch::Approx (0.01f));
}

TEST_CASE ("3047596375 (Starscape) Y flip follows the output's present-time flip") {
    // Wayland and GLFW mirror the scene again when presenting it, so their
    // perspective projection is Y-flipped; X11 is not (see Output::renderVFlip).
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
