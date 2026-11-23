#pragma once

#include "WallpaperEngine/Audio/AudioStream.h"
#include "WallpaperEngine/Render/CObject.h"
#include "WallpaperEngine/Scripting/ScriptableObject.h"

using namespace WallpaperEngine;

namespace WallpaperEngine::Render::Wallpapers {
class CScene;
}

namespace WallpaperEngine::Render::Objects {
using namespace WallpaperEngine::Data::Model;

class CSound final : public Scripting::ScriptableObject {
public:
    CSound (Wallpapers::CScene& scene, const Sound& sound);
    ~CSound () override;

    void render () override;

    /** SceneScript `thisLayer.play()` (sound layers). */
    void playSound () override;
    /** SceneScript `thisLayer.stop()` (sound layers). */
    void stopSound () override;
    /** SceneScript `thisLayer.isPlaying()` (sound layers). */
    [[nodiscard]] bool isSoundPlaying () const override;

protected:
    /** Creates and registers one stream per configured sound file. */
    void createStreams ();
    /** Unregisters and destroys every active stream. */
    void clearStreams ();

private:
    std::map<int, Audio::AudioStream*> m_audioStreams = {};

    const Sound& m_sound;
    /** Looping layers start automatically; single-shot layers wait for play(). */
    bool m_repeat = false;
    bool m_audioEnabled = false;
};
} // namespace WallpaperEngine::Render::Objects
