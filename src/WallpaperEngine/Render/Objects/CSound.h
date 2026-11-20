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

protected:
    void load ();

private:
    std::map<int, Audio::AudioStream*> m_audioStreams = {};

    const Sound& m_sound;
};
} // namespace WallpaperEngine::Render::Objects
