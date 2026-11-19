#include "CRenderable.h"

#include "WallpaperEngine/Data/Model/Material.h"
#include "WallpaperEngine/Data/Model/Object.h"
#include "WallpaperEngine/Data/Parsers/MaterialParser.h"

#include <algorithm>
#include <cmath>

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render::Objects;
using namespace WallpaperEngine::Render::Objects::Effects;
using namespace WallpaperEngine::Data::Parsers;
using namespace WallpaperEngine::Data::Builders;

CRenderable::CRenderable (Wallpapers::CScene& scene, const Object& object, const Material& material) :
    CObject (scene, object), Render::FBOProvider (&scene), m_material (material) { }

void CRenderable::detectTexture () {
    if (TextureMap* textures = &(*this->m_material.passes.begin ())->textures; !textures->empty ()) {
	std::string textureName = textures->begin ()->second;

	if (textureName.find ("_rt_") == 0 || textureName.find ("_alias_") == 0) {
	    this->m_texture = this->getScene ().findFBO (textureName);
	} else {
	    this->m_texture = this->getContext ().resolveTexture (textureName);
	}
    }
}

void CRenderable::setup () {
    CObject::setup ();

    // calculate full animation time (if any)
    this->m_animationTime = 0.0;

    for (const auto& cur : this->getTexture ()->getFrames ()) {
	this->m_animationTime += cur->frametime;
    }

    // init()/applyUserProperties() run before setup, so setFrame() has to be
    // remembered until the texture and its duration are known
    if (this->m_pendingAnimationFrame.has_value ()) {
	const int frame = *this->m_pendingAnimationFrame;
	this->m_pendingAnimationFrame.reset ();
	this->setTextureAnimationFrame (frame);
    }
}

std::shared_ptr<const TextureProvider> CRenderable::getTexture () const { return this->m_texture; }

double CRenderable::getAnimationTime () const { return this->m_animationTime; }

double CRenderable::getFrameStartTime (size_t frame) const {
    const auto& frames = this->m_texture->getFrames ();
    double time = 0.0;

    for (size_t i = 0; i < frame && i < frames.size (); ++i) {
	time += frames[i]->frametime;
    }

    return time;
}

void CRenderable::setTextureAnimationFrame (int frame) {
    if (this->m_texture == nullptr || this->m_animationTime <= 0.0) {
	this->m_pendingAnimationFrame = frame;
	return;
    }

    const auto& frames = this->m_texture->getFrames ();

    if (frames.empty ()) {
	this->m_pendingAnimationFrame = frame;
	return;
    }

    const int clamped = std::clamp (frame, 0, static_cast<int> (frames.size ()) - 1);

    this->m_animationPlayhead = this->getFrameStartTime (static_cast<size_t> (clamped));
    this->m_animationResumeTime = this->getContext ().getDriver ().getRenderTime ();
}

void CRenderable::pauseTextureAnimation () {
    if (!this->m_animationPlaying) {
	return;
    }

    this->m_animationPlayhead = this->m_animationTime > 0.0 ? this->getTextureAnimationTime () : 0.0;
    this->m_animationPlaying = false;
}

void CRenderable::playTextureAnimation () {
    if (this->m_animationPlaying) {
	return;
    }

    this->m_animationResumeTime = this->getContext ().getDriver ().getRenderTime ();
    this->m_animationPlaying = true;
}

void CRenderable::stopTextureAnimation () {
    this->m_animationPlayhead = 0.0;
    this->m_animationPlaying = false;
    this->m_pendingAnimationFrame.reset ();
}

double CRenderable::getTextureAnimationTime () const {
    double time = this->m_animationPlayhead;

    if (this->m_animationPlaying) {
	time += this->getContext ().getDriver ().getRenderTime () - this->m_animationResumeTime;
    }

    if (this->m_animationTime > 0.0) {
	time = std::fmod (time, this->m_animationTime);

	if (time < 0.0) {
	    time += this->m_animationTime;
	}
    }

    return time;
}
