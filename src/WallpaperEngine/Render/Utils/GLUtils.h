#pragma once

#include "WallpaperEngine/Logging/Log.h"

#include <GL/glew.h>

namespace WallpaperEngine::Render::Utils {
/**
 * Drains every pending OpenGL error.
 *
 * GL error state is sticky, so errors left behind by the scene render would
 * otherwise be reported by the next glGetError() and misattributed to the
 * readback that is about to happen.
 *
 * @param reason Description of the upcoming readback, used for debug logging.
 */
inline void drainGLErrors (const char* reason) {
    GLenum error = glGetError ();

    while (error != GL_NO_ERROR) {
	sLog.debug ("Discarding stale OpenGL error ", reason, ": ", error);
	error = glGetError ();
    }
}
} // namespace WallpaperEngine::Render::Utils
