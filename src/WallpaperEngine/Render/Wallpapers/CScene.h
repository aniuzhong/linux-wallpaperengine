#pragma once

#include <array>

#include "WallpaperEngine/Render/Camera.h"

#include "WallpaperEngine/Render/CWallpaper.h"
#include "WallpaperEngine/Scripting/ScriptEngine.h"

namespace WallpaperEngine::Render {
class Camera;
class CObject;
}

namespace WallpaperEngine::Render::Wallpapers {
using namespace WallpaperEngine::Data::Model;

class CScene final : public CWallpaper {
public:
    CScene (
	const Wallpaper& wallpaper, RenderContext& context, AudioContext& audioContext,
	const WallpaperState::TextureUVsScaling& scalingMode, const uint32_t& clampMode,
	const glm::ivec2& renderSize = {}
    );

    ~CScene () override;

    [[nodiscard]] Scripting::ScriptEngine& getScriptEngine () const;
    [[nodiscard]] Camera& getCamera () const;

    [[nodiscard]] const Scene& getScene () const;

    /** World-space dimensions of the scene (the camera projection size) */
    [[nodiscard]] int getWidth () const override;
    [[nodiscard]] int getHeight () const override;

    /** Pixel resolution the scene renders at (render size), for resolution-dependent uniforms */
    [[nodiscard]] const glm::vec2* getTexelSize () const;
    [[nodiscard]] const glm::vec2* getTexelSizeHalf () const;

    // Time accessors used by dynamic text layers (CText + ScriptEngine).
    // Read from the application-wide g_Time/g_TimeLast globals that other
    // renderers already consume via extern (e.g. CParticle).
    [[nodiscard]] float getTime () const;
    [[nodiscard]] float getDeltaTime () const;
    [[nodiscard]] float getFps () const;

    const glm::vec2* getMousePosition () const;
    const glm::vec2* getMousePositionLast () const;
    const glm::vec2* getMousePositionNormalized () const;
    const glm::vec2* getParallaxDisplacement () const;

    [[nodiscard]] const std::vector<CObject*>& getObjectsByRenderOrder () const;
    [[nodiscard]] const CObject* getObject (int id) const;

    /** Fixed number of light slots WE materials receive (g_LightsPosition/g_LightsColorRadius) */
    static constexpr int kMaxShaderLights = 4;

    [[nodiscard]] const glm::vec3* getLightPositions () const { return this->m_lightPositions.data (); }
    [[nodiscard]] const glm::vec4* getLightColorsRadius () const { return this->m_lightColorsRadius.data (); }

    /** Clamps a parallax offset so a layer covering the shown region cannot be
     * displaced past its own bounds (which would expose the clear color) */
    [[nodiscard]] glm::vec2 clampParallaxOffset (const glm::vec2& offset, const glm::vec2& scaledSize) const;

protected:
    void renderFrame (const glm::ivec4& viewport) override;
    void setupFramebuffers (bool sceneDepthBuffer) override;
    void updateUVs (const glm::ivec4& viewport, const bool vflip) override;
    void updateMouse (const glm::ivec4& viewport);

    friend class CWallpaper;

private:
    Render::CObject* createObject (const Object& object);
    Render::CObject* dispatchObjectType (const Object& object);
    void addObjectToRenderOrder (const Object& object);
    /** Refreshes the shader light arrays from the scene's light objects */
    void updateLights ();

    std::unique_ptr<Scripting::ScriptEngine> m_scriptEngine;
    std::unique_ptr<Camera> m_camera;
    /** World-space size of the camera projection (what objects position themselves against) */
    float m_projectionWidth = 0.0f;
    float m_projectionHeight = 0.0f;
    /** Pixel size the scene renders at (output viewport * render scale) */
    float m_renderWidth = 0.0f;
    float m_renderHeight = 0.0f;
    /** Texel sizes of the scene FBO, consumed by effect shaders (g_TexelSize) */
    glm::vec2 m_texelSize = {};
    glm::vec2 m_texelSizeHalf = {};
    ObjectUniquePtr m_bloomObjectData;
    CObject* m_bloomObject = nullptr;
    /** Shader light state, refreshed each frame from the scene's light objects */
    std::array<glm::vec3, kMaxShaderLights> m_lightPositions = {};
    std::array<glm::vec4, kMaxShaderLights> m_lightColorsRadius = {};
    std::map<int, CObject*> m_objects = {};
    std::vector<CObject*> m_objectsByRenderOrder = {};
    std::vector<DynamicValue*> m_scriptedValues = {};
    glm::vec2 m_mousePosition = {};
    glm::vec2 m_mousePositionLast = {};
    glm::vec2 m_mousePositionNormalized = {};
    glm::vec2 m_parallaxDisplacement = {};
    std::shared_ptr<const CFBO> _rt_4FrameBuffer = nullptr;
    std::shared_ptr<const CFBO> _rt_8FrameBuffer = nullptr;
    std::shared_ptr<const CFBO> _rt_Bloom = nullptr;
    std::shared_ptr<const CFBO> _rt_shadowAtlas = nullptr;
};
} // namespace WallpaperEngine::Render::Wallpaper
