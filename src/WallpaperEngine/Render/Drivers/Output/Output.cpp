#include "Output.h"

#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <unistd.h>

using namespace WallpaperEngine::Render::Drivers::Output;

Output::Output (ApplicationContext& context, VideoDriver& driver) : m_context (context), m_driver (driver) { }

const std::map<std::string, OutputViewport*>& Output::getViewports () const { return this->m_viewports; }

int Output::getFullWidth () const { return this->m_fullWidth; }

int Output::getFullHeight () const { return this->m_fullHeight; }

void Output::notifyFirstFrame () const {
    if (this->m_firstFrameNotified) {
	return;
    }

    this->m_firstFrameNotified = true;

    const char* runtimeDir = getenv ("XDG_RUNTIME_DIR");
    if (runtimeDir == nullptr || *runtimeDir == '\0') {
	runtimeDir = "/tmp";
    }

    const std::string path = std::string (runtimeDir) + "/wallpaperengine.ready";
    const int fd = open (path.c_str (), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd >= 0) {
	close (fd);
    }
}
