#include <SDL.h>

#include "CSound.h"

#include "WallpaperEngine/FileSystem/Container.h"
#include "WallpaperEngine/Logging/Log.h"

using namespace WallpaperEngine::Render::Objects;

CSound::CSound (Wallpapers::CScene& scene, const Sound& sound) : CObject (scene, sound), m_sound (sound) {
    if (this->getContext ().getApp ().getContext ().settings.audio.enabled) {
	this->load ();
    }
}

CSound::~CSound () {
    // free all the sound buffers and streams
    for (const auto& stream : this->m_audioStreams) {
	this->getScene ().getAudioContext ().removeStream (stream.first);
	delete stream.second;
    }

    this->m_audioStreams.clear ();
}

void CSound::load () {
    for (const auto& cur : this->m_sound.sounds) {
	Audio::AudioStream* stream = nullptr;

	// a single broken audio stream must not kill the whole wallpaper
	try {
	    stream = new Audio::AudioStream (this->getScene ().getAudioContext (), this->getAssetLocator ().read (cur));
	} catch (const std::exception& e) {
	    sLog.error ("Cannot initialize audio stream ", cur, ": ", e.what ());
	    continue;
	}

	stream->setRepeat (this->m_sound.playbackmode.has_value () && this->m_sound.playbackmode == "loop");

	// add the stream to the context so it can be played
	this->m_audioStreams.insert_or_assign (this->getScene ().getAudioContext ().addStream (stream), stream);
    }
}

void CSound::render () { }
