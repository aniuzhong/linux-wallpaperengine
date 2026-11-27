#pragma once

#include "WallpaperEngine/Render/Wallpapers/CScene.h"
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "WallpaperEngine/Data/Model/Wallpaper.h"

#include <chrono>

namespace WallpaperEngine::Render::Wallpapers {
class CScene;
}

namespace WallpaperEngine::Render {
using namespace WallpaperEngine::Data::Model;

class Camera {
public:
    Camera (Wallpapers::CScene& scene, const SceneData::Camera& camera);
    ~Camera ();

    void setOrthogonalProjection (const float width, const float height);
    void setPerspectiveProjection (float width, float height, bool flipY);
    /**
     * Applies a static camera transform from SceneScript or a camera layer.
     * Camera paths own the camera when present, so this is ignored while a
     * path is active.
     *
     * `zoom` is a relative zoom applied to both projections; 3D scenes also
     * use setFov() for the field of view itself.
     */
    void setStaticTransforms (const glm::vec3& eye, const glm::vec3& center, const glm::vec3& up, float zoom);
    /**
     * Updates the perspective field of view used by 3D scenes. 2D scenes use
     * CameraTransforms.zoom instead.
     */
    void setFov (float fov);
    /**
     * Advances the camera path animation (if the scene has camera paths) and
     * rebuilds the lookat from the interpolated eye/center/up.
     */
    void tick ();

    [[nodiscard]] bool usesCameraPath () const;

    [[nodiscard]] const glm::vec3& getCenter () const;
    [[nodiscard]] const glm::vec3& getEye () const;
    [[nodiscard]] const glm::vec3& getUp () const;
    [[nodiscard]] const glm::mat4& getProjection () const;
    [[nodiscard]] const glm::mat4& getLookAt () const;
    [[nodiscard]] Wallpapers::CScene& getScene () const;
    [[nodiscard]] bool isOrthogonal () const;
    [[nodiscard]] bool isYFlipped () const;
    [[nodiscard]] float getWidth () const;
    [[nodiscard]] float getHeight () const;
    [[nodiscard]] float getFov () const;
    [[nodiscard]] float getNearZ () const;
    [[nodiscard]] float getFarZ () const;
    [[nodiscard]] float getZoom () const;

private:
    void updatePerspectiveProjection (float fov, float zoom);
    void updateOrthogonalProjection ();
    [[nodiscard]] float getOrthogonalZoom () const;

    float m_width;
    float m_height;
    bool m_isOrthogonal = false;
    bool m_isYFlipped = false;
    glm::mat4 m_projection = {};
    glm::mat4 m_lookat = {};
    const SceneData::Camera& m_camera;
    Wallpapers::CScene& m_scene;

    /** camera path playback state, paths concatenate and loop */
    std::chrono::steady_clock::time_point m_pathStart;
    float m_totalPathDuration = 0.0f;

    /** current eye/center/up/zoom, updated by paths or static script transforms */
    glm::vec3 m_currentEye;
    glm::vec3 m_currentCenter;
    glm::vec3 m_currentUp;
    float m_currentZoom = 1.0f;
    bool m_pathConflictWarned = false;
};
} // namespace WallpaperEngine::Render
