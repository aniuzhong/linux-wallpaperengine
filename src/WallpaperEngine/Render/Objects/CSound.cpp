#include <SDL.h>

#include "CSound.h"

#include "WallpaperEngine/FileSystem/Container.h"
#include "WallpaperEngine/Logging/Log.h"

#include <ranges>

using namespace WallpaperEngine::Render::Objects;

CSound::CSound (Wallpapers::CScene& scene, const Sound& sound) :
    CObject (scene, sound), Scripting::ScriptableObject (scene, sound), m_sound (sound) {
    this->m_audioEnabled = this->getContext ().getApp ().getContext ().settings.audio.enabled;
    this->m_repeat = this->m_sound.playbackmode.has_value () && this->m_sound.playbackmode == "loop";

    // Looping ambient sounds start with the wallpaper. One-shot sounds are
    // created on demand so they do not play accidentally on scene load.
    if (this->m_audioEnabled && this->m_repeat) {
	this->createStreams ();
    }
}

CSound::~CSound () { this->clearStreams (); }

void CSound::createStreams () {
    for (const auto& cur : this->m_sound.sounds) {
	Audio::AudioStream* stream = nullptr;

	// a single broken audio stream must not kill the whole wallpaper
	try {
	    stream = new Audio::AudioStream (this->getScene ().getAudioContext (), this->getAssetLocator ().read (cur));
	} catch (const std::exception& e) {
	    sLog.error ("Cannot initialize audio stream ", cur, ": ", e.what ());
	    continue;
	}

	stream->setRepeat (this->m_repeat);

	// add the stream to the context so it can be played
	this->m_audioStreams.insert_or_assign (this->getScene ().getAudioContext ().addStream (stream), stream);
    }
}

void CSound::clearStreams () {
    for (const auto& [id, stream] : this->m_audioStreams) {
	this->getScene ().getAudioContext ().removeStream (id);
	delete stream;
    }

    this->m_audioStreams.clear ();
}

void CSound::render () { }

void CSound::playSound () {
    if (!this->m_audioEnabled) {
	return;
    }

    if (!this->m_repeat) {
	// play() starts playback if it is not already running; don't restart a
	// one-shot stream on every script tick.
	for (const auto* stream : this->m_audioStreams | std::views::values) {
	    if (!stream->isFinished ()) {
		return;
	    }
	}

	this->clearStreams ();
    }

    if (this->m_audioStreams.empty ()) {
	this->createStreams ();
    }
}

void CSound::stopSound () { this->clearStreams (); }

bool CSound::isSoundPlaying () const {
    if (this->m_audioStreams.empty ()) {
	return false;
    }

    if (this->m_repeat) {
	return true;
    }

    for (const auto* stream : this->m_audioStreams | std::views::values) {
	if (!stream->isFinished ()) {
	    return true;
	}
    }

    return false;
}
