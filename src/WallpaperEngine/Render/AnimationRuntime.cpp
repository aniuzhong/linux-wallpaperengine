#include "AnimationRuntime.h"

#include <algorithm>
#include <cmath>
#include <ranges>

#include <glm/common.hpp>

#include "WallpaperEngine/Data/Model/DynamicValue.h"
#include "WallpaperEngine/Logging/Log.h"

using namespace WallpaperEngine::Render;
using namespace WallpaperEngine::Data::Model;

namespace {
// Cap a single frame delta so a long pause (fullscreen detection, debugger)
// does not fast-forward or visibly jump every timeline animation on resume.
constexpr float kMaximumDelta = 0.25f;

glm::vec3 baseValue (const DynamicValue& value) {
    switch (value.getType ()) {
	case DynamicValue::Vec2:
	    {
		const glm::vec2 vector = value.getVec2 ();
		return { vector.x, vector.y, 0.0f };
	    }
	case DynamicValue::Vec3:
	    return value.getVec3 ();
	case DynamicValue::Vec4:
	    {
		const glm::vec4 vector = value.getVec4 ();
		return { vector.x, vector.y, vector.z };
	    }
	case DynamicValue::Float:
	    return { value.getFloat (), 0.0f, 0.0f };
	case DynamicValue::Int:
	    return { static_cast<float> (value.getInt ()), 0.0f, 0.0f };
	default:
	    return {};
    }
}
} // namespace

bool AnimationRuntime::supportsType (const DynamicValue& value) {
    switch (value.getType ()) {
	case DynamicValue::Vec2:
	case DynamicValue::Vec3:
	case DynamicValue::Vec4:
	case DynamicValue::Float:
	case DynamicValue::Int:
	    return true;
	default:
	    return false;
    }
}

void AnimationRuntime::registerSetting (UserSetting& setting) {
    if (!setting.animation.has_value () || setting.value == nullptr) {
	return;
    }

    const Animation& animation = setting.animation.value ();

    // Paused animations need the SceneScript playback API to start them, which
    // is not implemented yet. Auto-playing animations are driven here even
    // when they carry a name or timeline events (event dispatch is still TODO).
    if (animation.startPaused) {
	sLog.debug ("Skipping timeline animation: SceneScript playback control is not implemented yet");
	return;
    }

    if (animation.empty () || animation.duration () <= 0.0f) {
	return;
    }

    // Do not compete with scripts or user properties for the same value.
    if (setting.value->getScriptSource ().has_value ()) {
	return;
    }

    if (setting.property != nullptr) {
	sLog.debug ("Skipping timeline animation on a user-property-bound value");
	return;
    }

    if (!supportsType (*setting.value)) {
	return;
    }

    if (std::ranges::find (this->m_entries, &setting, &Entry::setting) != this->m_entries.end ()) {
	return;
    }

    this->m_entries.push_back (
	Entry {
	    .setting = &setting,
	    .base = *setting.value,
	    .startTime = this->m_sceneTime,
	    .pendingStart = true,
	}
    );
}

void AnimationRuntime::registerSetting (const UserSettingUniquePtr& setting) {
    if (setting != nullptr) {
	this->registerSetting (*setting);
    }
}

void AnimationRuntime::registerSettings (const ShaderConstantMap& settings) {
    for (const auto& entry : settings) {
	this->registerSetting (entry.second);
    }
}

void AnimationRuntime::tick (const float time) {
    float delta = this->m_started ? time - this->m_lastTime : 0.0f;

    this->m_started = true;
    this->m_lastTime = time;

    if (delta < 0.0f) {
	delta = 0.0f;
    }

    this->m_sceneTime += std::min (delta, kMaximumDelta);

    for (Entry& entry : this->m_entries) {
	if (entry.pendingStart) {
	    entry.startTime = this->m_sceneTime;
	    entry.pendingStart = false;
	}

	this->apply (entry, this->m_sceneTime - entry.startTime);
    }
}

void AnimationRuntime::apply (Entry& entry, const float localTime) {
    const Animation& animation = entry.setting->animation.value ();
    const glm::vec3 base = baseValue (entry.base);
    const glm::vec3 fallback = animation.relative ? glm::vec3 (0.0f) : base;
    const glm::vec3 animated = animation.evaluate (localTime, fallback);
    const glm::vec3 result = animation.relative ? base + animated : animated;

    switch (entry.setting->value->getType ()) {
	case DynamicValue::Vec2:
	    entry.setting->value->update (glm::vec2 (result.x, result.y), DynamicValue::UpdateSource::Animation);
	    break;
	case DynamicValue::Vec3:
	    entry.setting->value->update (result, DynamicValue::UpdateSource::Animation);
	    break;
	case DynamicValue::Vec4:
	    entry.setting->value->update (
		glm::vec4 (result, entry.base.getVec4 ().w), DynamicValue::UpdateSource::Animation
	    );
	    break;
	case DynamicValue::Float:
	    entry.setting->value->update (result.x, DynamicValue::UpdateSource::Animation);
	    break;
	case DynamicValue::Int:
	    entry.setting->value->update (
		static_cast<int> (std::lround (result.x)), DynamicValue::UpdateSource::Animation
	    );
	    break;
	default:
	    break;
    }
}
