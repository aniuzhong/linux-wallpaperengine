#pragma once

#include <array>
#include <optional>

#include "WallpaperEngine/Render/AnimationRuntime.h"
#include "WallpaperEngine/Render/Camera.h"

#include "WallpaperEngine/Render/CWallpaper.h"
#include "WallpaperEngine/Scripting/ScriptEngine.h"

namespace WallpaperEngine::Render {
class Camera;
class CObject;

namespace Objects {
    class CModel;
}
}

namespace WallpaperEngine::Render::Wallpapers {
using namespace WallpaperEngine::Data::Model;

class CScene final : public CWallpaper {
public:
    CScene (
	const Wallpaper& wallpaper, RenderContext& context, AudioContext& audioContext,
	const WallpaperState::TextureUVsScaling& scalingMode, const uint32_t& clampMode,
	const glm::ivec2& renderSize = {}, const glm::ivec2& viewportSize = {}
    );

    ~CScene () override;

    [[nodiscard]] Scripting::ScriptEngine& getScriptEngine () const;
    [[nodiscard]] Camera& getCamera () const;
    [[nodiscard]] AnimationRuntime& getAnimationRuntime ();

    [[nodiscard]] const Scene& getScene () const;

    /** World-space dimensions of the scene (the camera projection size) */
    [[nodiscard]] int getWidth () const override;
    [[nodiscard]] int getHeight () const override;

    /** User properties and the values currently configured for them. */
    [[nodiscard]] const Properties& getUserProperties () const;
    /** Project metadata (title, workshop id, ...). */
    [[nodiscard]] const Project& getProject () const;
    /** Physical resolution of the output the wallpaper is rendered on. */
    [[nodiscard]] glm::ivec2 getScreenResolution () const;

    /** Pixel resolution the scene renders at (render size), for resolution-dependent uniforms */
    [[nodiscard]] const glm::vec2* getTexelSize () const;
    [[nodiscard]] const glm::vec2* getTexelSizeHalf () const;

    // Time accessors used by dynamic text layers (CText + ScriptEngine).
    // Read from the application-wide g_Time/g_TimeLast globals that other
    // renderers already consume via extern (e.g. CParticle).
    [[nodiscard]] float getTime () const;
    [[nodiscard]] float getDeltaTime () const;
    [[nodiscard]] float getFps () const;

    [[nodiscard]] const glm::vec2* getMousePosition () const;
    [[nodiscard]] const glm::vec2* getMousePositionLast () const;
    [[nodiscard]] const glm::vec2* getMousePositionNormalized () const;
    /** Pointer position in viewport space (0,0 top-left; 1,1 bottom-right). */
    [[nodiscard]] const glm::vec2* getMousePositionViewport () const;
    [[nodiscard]] const glm::vec2* getParallaxDisplacement () const;
    [[nodiscard]] const glm::vec2* getParallaxPosition () const;

    [[nodiscard]] const std::vector<CObject*>& getObjectsByRenderOrder () const;
    [[nodiscard]] const CObject* getObject (int id) const;
    [[nodiscard]] CObject* getObject (int id);
    [[nodiscard]] CObject* getObjectByName (const std::string& name) const;

    CObject* createLayerFromAsset (const std::string& asset);
    bool destroyLayer (CObject* object);
    void sortLayer (CObject* object, int index);

    /** Fixed number of light slots WE materials receive (g_LightsPosition/g_LightsColorRadius) */
    static constexpr int kMaxShaderLights = 4;

    [[nodiscard]] const glm::vec3* getLightPositions () const { return this->m_lightPositions.data (); }
    [[nodiscard]] const glm::vec4* getLightColorsRadius () const { return this->m_lightColorsRadius.data (); }

    /** Clamps a parallax offset so a layer covering the shown region cannot be
     * displaced past its own bounds (which would expose the clear color) */
    [[nodiscard]] glm::vec2 clampParallaxOffset (const glm::vec2& offset, const glm::vec2& scaledSize) const;
    [[nodiscard]] glm::vec3 getMousePositionWorld () const;

    /**
     * Maps the virtual mouse position (UV space, including any UV crop) to the scene
     * coordinate space layer origins use. The camera window is what is actually shown,
     * so cover/fill cropping has to be accounted for or the cursor drifts on that axis.
     */
    [[nodiscard]] static glm::vec3 mapMousePositionToWorld (
	const glm::vec2& mousePosition, float ustart, float uend, float vstart, float vend, float sceneWidth,
	float sceneHeight, float shownWidth, float shownHeight
    );

protected:
    void renderFrame (const glm::ivec4& viewport) override;
    void setupFramebuffers (bool sceneDepthBuffer) override;
    void updateUVs (const glm::ivec4& viewport, bool vflip) override;
    void updateMouse (const glm::ivec4& viewport);
    void dispatchCursorEvents ();

    friend class CWallpaper;

private:
    Render::CObject* createObject (const Object& object);
    Render::CObject* dispatchObjectType (const Object& object);
    void addObjectToRenderOrder (const Object& object);
    /**
     * Whether the object's parent is a composition layer that captures its
     * children, meaning the object must not render into the scene itself.
     */
    [[nodiscard]] bool isCapturedByCompositionParent (const CObject& object) const;
    /**
     * Orders translucent 3D model objects back-to-front along the camera's
     * view direction. Opaque models and screen-space layers keep the authored
     * scene.json order; opaque depth testing handles their visibility.
     */
    void sortModelsByDepth ();
    /** Refreshes the shader light arrays from the scene's light objects */
    void updateLights ();
    /** Collects the reflected models and the mirror plane once the scene is set up */
    void setupReflection ();
    /**
     * Renders models marked `reflected` into _rt_Reflection, mirrored over the
     * mirror surface's plane, so materials sampling the reflection target pick
     * it up through screen-space UVs. The camera itself stays untouched.
     */
    void renderReflection ();

    std::unique_ptr<Scripting::ScriptEngine> m_scriptEngine;
    std::unique_ptr<Camera> m_camera;
    AnimationRuntime m_animationRuntime;
    /** World-space size of the camera projection (what objects position themselves against) */
    float m_projectionWidth = 0.0f;
    float m_projectionHeight = 0.0f;
    /** Pixel size the scene renders at (output viewport * render scale) */
    float m_renderWidth = 0.0f;
    float m_renderHeight = 0.0f;
    /** Logical viewport size (unscaled) this scene renders for */
    glm::ivec2 m_viewportSize = {};
    /** Texel sizes of the scene FBO, consumed by effect shaders (g_TexelSize) */
    glm::vec2 m_texelSize = {};
    glm::vec2 m_texelSizeHalf = {};
    ObjectUniquePtr m_bloomObjectData;
    CObject* m_bloomObject = nullptr;
    std::map<int, ObjectUniquePtr> m_dynamicObjects = {};
    int m_nextDynamicObjectId = -100000;
    /** Shader light state, refreshed each frame from the scene's light objects */
    std::array<glm::vec3, kMaxShaderLights> m_lightPositions = {};
    std::array<glm::vec4, kMaxShaderLights> m_lightColorsRadius = {};
    /** Scene clear color, also used to clear the reflection target */
    glm::vec3 m_clearColor = glm::vec3 (0.0f);
    std::map<int, CObject*> m_objects = {};
    std::vector<CObject*> m_objectsByRenderOrder = {};
    /** Models rendered into the reflection target, and the plane they mirror over */
    std::vector<Objects::CModel*> m_reflectedModels = {};
    std::optional<float> m_reflectionPlaneY = std::nullopt;
    std::vector<DynamicValue*> m_scriptedValues = {};
    glm::vec2 m_mousePosition = {};
    glm::vec2 m_mousePositionLast = {};
    glm::vec2 m_mousePositionNormalized = {};
    /** Cursor fraction in the output viewport, top-left origin (for hit testing) */
    glm::vec2 m_mousePositionViewport = {};
    glm::vec2 m_lastCursorEventPosition = { -1.0f, -1.0f };
    Scripting::ScriptableObject* m_cursorTarget = nullptr;
    glm::vec2 m_cursorTargetLocal = {};
    Scripting::ScriptableObject* m_cursorPressTarget = nullptr;
    bool m_mouseLeftDown = false;
    /** Smoothed layer displacement in screen fractions; includes amount and influence. */
    glm::vec2 m_parallaxDisplacement = {};
    /** Smoothed scene-UV mouse offset in [-0.5, 0.5] (y top-down). */
    glm::vec2 m_parallaxPointer = {};
    /** Shader-space pointer uploaded as g_ParallaxPosition; center is (0.5, 0.5). */
    glm::vec2 m_parallaxPosition = { 0.5f, 0.5f };
    std::shared_ptr<const CFBO> _rt_4FrameBuffer = nullptr;
    std::shared_ptr<const CFBO> _rt_8FrameBuffer = nullptr;
    std::shared_ptr<const CFBO> _rt_Bloom = nullptr;
    std::shared_ptr<const CFBO> _rt_shadowAtlas = nullptr;
    std::shared_ptr<const CFBO> _rt_Reflection = nullptr;
};
} // namespace WallpaperEngine::Render::Wallpaper
