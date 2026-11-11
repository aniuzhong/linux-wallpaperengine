#pragma once

#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace WallpaperEngine::Render::CameraProjection {
/**
 * Resolves the near plane used by the perspective camera.
 *
 * Wallpaper Engine's 2D scenes rely on a zero near plane: every object sits at
 * z = 0, so a positive near plane would clip the entire scene. Only the
 * perspective path needs a strictly positive value, and it must not change the
 * shared nearz default.
 *
 * Explicit positive values are kept as authored, including values smaller than
 * the fallback, so scene-provided camera settings are not overridden.
 */
[[nodiscard]] constexpr float resolvePerspectiveNearZ (const float configured) {
    return configured > 0.0f ? configured : 0.01f;
}

/**
 * Builds the perspective projection used by 3D scenes.
 *
 * Keeping this pure makes the near-plane and Y-flip policy testable without
 * constructing a Camera. `flipY` mirrors the projection vertically to
 * compensate the output's present-time flip (see Output::renderVFlip).
 */
[[nodiscard]] inline glm::mat4 buildPerspectiveProjection (
    const float fovDegrees, const float aspect, const float nearz, const float farz, const bool flipY
) {
    const glm::mat4 projection = glm::perspective<float> (
	glm::radians (glm::clamp (fovDegrees, 1.0f, 179.0f)), aspect > 0.0f ? aspect : 1.0f,
	resolvePerspectiveNearZ (nearz), farz
    );

    if (!flipY) {
	return projection;
    }

    glm::mat4 flipped = projection;
    flipped[1][1] *= -1.0f;
    return flipped;
}
} // namespace WallpaperEngine::Render::CameraProjection
