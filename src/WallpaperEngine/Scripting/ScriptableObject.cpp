#include "ScriptableObject.h"

#include "ScriptEngine.h"
#include "WallpaperEngine/Data/Model/Object.h"
#include "WallpaperEngine/Data/Utils/ScopeGuard.h"
#include "WallpaperEngine/Render/Objects/CRenderable.h"

#include <ranges>

using namespace WallpaperEngine::Render;
using namespace WallpaperEngine::Scripting;

ScriptableObject::ScriptableObject (Wallpapers::CScene& scene, const Object& object) : CObject (scene, object) {
    const bool isImage = object.is<Image> ();
    const bool isParticle = object.is<Particle> ();
    const bool isText = object.is<Text> ();

    // typed objects register the values their renderers read
    this->registerProperty ("origin", *object.origin->value);

    if (!isImage && !isParticle && !isText) {
	this->registerProperty ("scale", *object.groupScale->value);
    }

    if (!isImage && !isParticle) {
	this->registerProperty ("angles", *object.groupAngles->value);
    }

    if (!isImage && !isParticle && !isText) {
	this->registerProperty ("visible", *object.groupVisible->value);
    }
}

DynamicValue& ScriptableObject::getProperty (const std::string& name) {
    const auto it = this->m_properties.find (name);

    if (it == this->m_properties.end ()) {
	sLog.exception ("Property '" + name + "' not found on object '" + this->getObject ().name + "'");
    }

    return it->second.value;
}

ScriptableObject::~ScriptableObject () { this->getScene ().getScriptEngine ().removeScriptsForObject (*this); }

const std::map<std::string, ScriptableObject::PropertyEntry>& ScriptableObject::getProperties () const {
    return this->m_properties;
}

void ScriptableObject::attachRenderable (Render::Objects::CRenderable* renderable) {
    this->m_renderable = renderable;

    if (this->m_renderable == nullptr) {
	return;
    }

    // scripts run from the base constructor, before the object is fully built; replay
    // anything they requested through thisLayer.getTextureAnimation()
    if (this->m_pendingAnimationRewind) {
	this->m_renderable->stopTextureAnimation ();
    }

    if (this->m_pendingAnimationFrame.has_value ()) {
	this->m_renderable->setTextureAnimationFrame (*this->m_pendingAnimationFrame);
    }

    if (this->m_pendingAnimationPlaying.has_value ()) {
	if (*this->m_pendingAnimationPlaying) {
	    this->m_renderable->playTextureAnimation ();
	} else {
	    this->m_renderable->pauseTextureAnimation ();
	}
    }

    this->m_pendingAnimationFrame.reset ();
    this->m_pendingAnimationPlaying.reset ();
    this->m_pendingAnimationRewind = false;
}

void ScriptableObject::setTextureAnimationFrame (int frame) {
    if (this->m_renderable != nullptr) {
	this->m_renderable->setTextureAnimationFrame (frame);
	return;
    }

    this->m_pendingAnimationFrame = frame;
    this->m_pendingAnimationRewind = false;
}

void ScriptableObject::pauseTextureAnimation () {
    if (this->m_renderable != nullptr) {
	this->m_renderable->pauseTextureAnimation ();
	return;
    }

    this->m_pendingAnimationPlaying = false;
}

void ScriptableObject::playTextureAnimation () {
    if (this->m_renderable != nullptr) {
	this->m_renderable->playTextureAnimation ();
	return;
    }

    this->m_pendingAnimationPlaying = true;
}

void ScriptableObject::stopTextureAnimation () {
    if (this->m_renderable != nullptr) {
	this->m_renderable->stopTextureAnimation ();
	return;
    }

    this->m_pendingAnimationFrame.reset ();
    this->m_pendingAnimationPlaying = false;
    this->m_pendingAnimationRewind = true;
}

void ScriptableObject::registerProperty (const std::string& name, DynamicValue& value) {
    auto inserted = this->m_properties.emplace (
	name, PropertyEntry { .key = name + "_" + std::to_string (this->getId ()), .value = value }
    );

    if (!inserted.second) {
	return;
    }

    this->getScene ().getScriptEngine ().queueScript (inserted.first->second.key, inserted.first->second.value, *this);
}

void ScriptableObject::registerValueScript (const std::string& key, DynamicValue& value) {
    this->getScene ().getScriptEngine ().queueScript (key, value, *this);
}
