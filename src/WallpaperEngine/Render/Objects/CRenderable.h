#pragma once
#include "WallpaperEngine/Render/CObject.h"

#include "WallpaperEngine/Render/FBOProvider.h"
#include "WallpaperEngine/Render/Objects/Effects/CPass.h"
#include "WallpaperEngine/Render/Wallpapers/CScene.h"
#include <optional>

#include "WallpaperEngine/Render/Shaders/Shader.h"

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render;

namespace WallpaperEngine::Render::Objects {
class CRenderable : virtual public CObject, public FBOProvider {
    friend CObject;

public:
    CRenderable (Wallpapers::CScene& scene, const Object& object, const Material& material);

    [[nodiscard]] std::shared_ptr<const TextureProvider> getTexture () const;

    [[nodiscard]] double getAnimationTime () const;

    /** Move the animation playhead to the start of the given frame (SceneScript setFrame) */
    void setTextureAnimationFrame (int frame);
    /** Pause the animation at its current position (SceneScript pause) */
    void pauseTextureAnimation ();
    /** Resume the animation from its current position (SceneScript play) */
    void playTextureAnimation ();
    /** Stop the animation and rewind to the first frame (SceneScript stop) */
    void stopTextureAnimation ();
    /** Current position in the animation loop, in seconds */
    [[nodiscard]] double getTextureAnimationTime () const;

    void setup () override;

    [[nodiscard]] virtual const float& getBrightness () const = 0;
    [[nodiscard]] virtual const float& getUserAlpha () const = 0;
    [[nodiscard]] virtual const float& getAlpha () const = 0;
    [[nodiscard]] virtual const glm::vec3& getColor () const = 0;
    [[nodiscard]] virtual const glm::vec4& getColor4 () const = 0;
    [[nodiscard]] virtual const glm::vec3& getCompositeColor () const = 0;

protected:
    void detectTexture ();

    /** Start time of the frame at index `frame`, in seconds */
    [[nodiscard]] double getFrameStartTime (size_t frame) const;

    double m_animationTime = 0.0;
    /** Position the animation resumes from, in seconds */
    double m_animationPlayhead = 0.0;
    /** Render time at which m_animationPlayhead was set */
    double m_animationResumeTime = 0.0;
    bool m_animationPlaying = true;
    /** Frame requested before the texture/duration was available (SceneScript init) */
    std::optional<int> m_pendingAnimationFrame = std::nullopt;

    std::shared_ptr<const TextureProvider> m_texture = nullptr;
    const Material& m_material;
};
}
