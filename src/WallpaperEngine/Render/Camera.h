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
     * Advances the camera path animation (if the scene has camera paths) and
     * rebuilds the lookat from the interpolated eye/center/up.
     */
    void tick ();

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

private:
    void updatePerspectiveProjection (float fov, float zoom);

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

    /** current eye/center/up, updated by the path animation each tick */
    glm::vec3 m_currentEye;
    glm::vec3 m_currentCenter;
    glm::vec3 m_currentUp;
};
} // namespace WallpaperEngine::Render
