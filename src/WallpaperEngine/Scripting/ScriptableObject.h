#pragma once
#include "WallpaperEngine/Data/Model/Types.h"
#include "WallpaperEngine/Render/CObject.h"

#include <optional>

namespace WallpaperEngine::Render::Wallpapers {
class CScene;
}

namespace WallpaperEngine::Render::Objects {
class CRenderable;
}

namespace WallpaperEngine::Scripting {
class ScriptableObject : virtual public CObject {
public:
    struct PropertyEntry {
	std::string key;
	DynamicValue& value;
    };

    ScriptableObject (Wallpapers::CScene& scene, const Object& object);
    virtual ~ScriptableObject ();

    DynamicValue& getProperty (const std::string& name);

    const std::map<std::string, PropertyEntry>& getProperties () const;

    /** Attach the renderable controlled by this scriptable object (derived constructors) */
    void attachRenderable (WallpaperEngine::Render::Objects::CRenderable* renderable);

    /** Queue or apply texture animation commands (SceneScript ITextureAnimation) */
    void setTextureAnimationFrame (int frame);
    void pauseTextureAnimation ();
    void playTextureAnimation ();
    void stopTextureAnimation ();

    /** SceneScript `thisLayer.emitParticles(count)` (particle layers only). */
    virtual void emitParticles (int count) { }
    /** SceneScript `thisLayer.play()` (sound layers only). */
    virtual void playSound () { }
    /** SceneScript `thisLayer.stop()` (sound layers only). */
    virtual void stopSound () { }
    /** SceneScript `thisLayer.pause()` (sound layers only). */
    virtual void pauseSound () { }
    /** SceneScript `thisLayer.isPlaying()` (sound layers only). */
    [[nodiscard]] virtual bool isSoundPlaying () const { return false; }

protected:
    void registerProperty (const std::string& name, DynamicValue& value);

private:
    std::map<std::string, PropertyEntry> m_properties;
    WallpaperEngine::Render::Objects::CRenderable* m_renderable = nullptr;
    std::optional<int> m_pendingAnimationFrame = std::nullopt;
    std::optional<bool> m_pendingAnimationPlaying = std::nullopt;
    bool m_pendingAnimationRewind = false;
};
}
