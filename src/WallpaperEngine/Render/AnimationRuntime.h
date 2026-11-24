#pragma once

#include <vector>

#include "WallpaperEngine/Data/Model/UserSetting.h"

namespace WallpaperEngine::Render {
/**
 * Drives timeline animations attached to UserSettings.
 *
 * Scripted properties and properties bound to user settings are skipped so
 * the animation cannot fight another owner of the same value. Paused
 * animations are not registered yet because the SceneScript playback control
 * API is still missing.
 */
class AnimationRuntime {
public:
    void registerSetting (Data::Model::UserSetting& setting);
    void registerSetting (const Data::Model::UserSettingUniquePtr& setting);
    void registerSettings (const Data::Model::ShaderConstantMap& settings);
    void tick (float time);

private:
    struct Entry {
	Data::Model::UserSetting* setting = nullptr;
	Data::Model::DynamicValue base;
	float startTime = 0.0f;
	bool pendingStart = true;
    };

    static bool supportsType (const Data::Model::DynamicValue& value);
    static void apply (Entry& entry, float localTime);

    std::vector<Entry> m_entries = {};
    float m_sceneTime = 0.0f;
    float m_lastTime = 0.0f;
    bool m_started = false;
};
} // namespace WallpaperEngine::Render
