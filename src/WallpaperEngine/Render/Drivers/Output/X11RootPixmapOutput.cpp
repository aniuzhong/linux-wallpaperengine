#include "X11RootPixmapOutput.h"

#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Render/Drivers/VideoDriver.h"

#include <X11/Xatom.h>

#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <unistd.h>

using namespace WallpaperEngine::Render::Drivers::Output;

namespace {
/**
 * Signals the wrapper that the first frame has actually been submitted to X11.
 * The path is fixed (per XDG runtime dir) so the wrapper and engine do not need
 * to exchange another environment variable. The wrapper removes the file before
 * starting the engine and after the engine exits.
 */
void signalFirstFrame () {
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
} // namespace

X11RootPixmapOutput::X11RootPixmapOutput (ApplicationContext& context, VideoDriver& driver) :
    X11Output (context, driver) {
    this->initialize ();
}

X11RootPixmapOutput::~X11RootPixmapOutput () {
    this->beginDestruction ();
    this->freePresentation ();
}

void X11RootPixmapOutput::initPresentation () {
    if (this->m_display == nullptr || this->m_root == None || this->m_fullWidth <= 0 || this->m_fullHeight <= 0) {
	sLog.error ("Cannot initialize the X11 root pixmap output without a valid XRandR layout");
	return;
    }

    // create pixmap so we can draw things in there
    this->m_pixmap = XCreatePixmap (this->m_display, this->m_root, this->m_fullWidth, this->m_fullHeight, 24);

    if (this->m_pixmap == None) {
	sLog.error ("Cannot create the X11 root pixmap");
	return;
    }

    this->m_gc = XCreateGC (this->m_display, this->m_pixmap, 0, nullptr);

    if (this->m_gc == nullptr) {
	sLog.error ("Cannot create the X11 graphics context for the root pixmap");
	this->freePresentation ();
	return;
    }

    // pre-fill it with black
    XFillRectangle (this->m_display, this->m_pixmap, this->m_gc, 0, 0, this->m_fullWidth, this->m_fullHeight);
    // set the window background as our pixmap
    XSetWindowBackgroundPixmap (this->m_display, this->m_root, this->m_pixmap);
    // allocate space for the image's data. The XImage below uses a 32-bit
    // scanline pad, so bytes_per_line is width * 4 for this 24-bit ZPixmap,
    // matching the tightly packed buffer glReadPixels fills.
    this->m_imageSize = static_cast<uint32_t> (this->m_fullWidth) * static_cast<uint32_t> (this->m_fullHeight) * 4;
    this->m_imageData = new char[this->m_imageSize];
    // create an image so we can copy it over
    this->m_image = XCreateImage (
	this->m_display, CopyFromParent, 24, ZPixmap, 0, this->m_imageData, this->m_fullWidth, this->m_fullHeight, 32, 0
    );

    if (this->m_image == nullptr) {
	sLog.error ("Cannot create the X11 image for the root pixmap");
	this->freePresentation ();
	return;
    }

    // setup driver's render changing the window's size
    this->m_driver.resizeWindow ({ this->m_fullWidth, this->m_fullHeight });
}

void X11RootPixmapOutput::freePresentation () {
    // XDestroyImage would free image->data; the buffer is owned by new[] here.
    if (this->m_image != nullptr) {
	this->m_image->data = nullptr;
	XDestroyImage (this->m_image);
	this->m_image = nullptr;
    }
    delete[] this->m_imageData;
    this->m_imageData = nullptr;
    this->m_imageSize = 0;

    if (this->m_display != nullptr) {
	if (this->m_gc != nullptr) {
	    XFreeGC (this->m_display, this->m_gc);
	}
	if (this->m_pixmap != None) {
	    XFreePixmap (this->m_display, this->m_pixmap);
	}
    }
    this->m_gc = nullptr;
    this->m_pixmap = None;
}

bool X11RootPixmapOutput::renderVFlip () const { return false; }

bool X11RootPixmapOutput::haveImageBuffer () const { return this->m_imageData != nullptr; }

void* X11RootPixmapOutput::getImageBuffer () const { return this->m_imageData; }

uint32_t X11RootPixmapOutput::getImageBufferSize () const { return this->m_imageSize; }

void X11RootPixmapOutput::updateRender () const {
    if (this->m_display == nullptr || this->m_image == nullptr || this->m_gc == nullptr || this->m_pixmap == None) {
	return;
    }

    // put the image back into the screen
    XPutImage (
	this->m_display, this->m_pixmap, this->m_gc, this->m_image, 0, 0, 0, 0, this->m_fullWidth, this->m_fullHeight
    );

    // _XROOTPMAP_ID & ESETROOT_PMAP_ID allow other programs (compositors) to
    // edit the background. Without these, other programs will clear the screen.
    // it also forces the compositor to refresh the background (tested with picom)
    const Atom prop_root = XInternAtom (this->m_display, "_XROOTPMAP_ID", False);
    const Atom prop_esetroot = XInternAtom (this->m_display, "ESETROOT_PMAP_ID", False);
    XChangeProperty (
	this->m_display, this->m_root, prop_root, XA_PIXMAP, 32, PropModeReplace, (unsigned char*)&this->m_pixmap, 1
    );
    XChangeProperty (
	this->m_display, this->m_root, prop_esetroot, XA_PIXMAP, 32, PropModeReplace, (unsigned char*)&this->m_pixmap, 1
    );

    XClearWindow (this->m_display, this->m_root);
    XFlush (this->m_display);

    // The wrapper keeps the native desktop opaque until this file appears, so
    // the transparent desktop never exposes the previous engine's last frame.
    static bool firstFrameSignaled = false;
    if (!firstFrameSignaled) {
	signalFirstFrame ();
	firstFrameSignaled = true;
    }
}
