#include "X11DesktopWindowOutput.h"

#include "OutputViewport.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Render/Drivers/VideoDriver.h"

#include <algorithm>
#include <climits>

using namespace WallpaperEngine::Render::Drivers::Output;

X11DesktopWindowOutput::X11DesktopWindowOutput (ApplicationContext& context, VideoDriver& driver) :
    X11Output (context, driver) {
    this->initialize ();
}

X11DesktopWindowOutput::~X11DesktopWindowOutput () {
    this->beginDestruction ();
    this->freePresentation ();
}

void X11DesktopWindowOutput::initPresentation () {
    if (this->m_display == nullptr || this->m_root == None || this->m_fullWidth <= 0 || this->m_fullHeight <= 0) {
	sLog.error ("Cannot initialize the X11 desktop window output without a valid XRandR layout");
	return;
    }

    if (this->m_screens.empty ()) {
	sLog.error ("Cannot initialize the X11 desktop window output without any requested screen");
	return;
    }

    // One window covers exactly the requested outputs. Viewport coordinates
    // are global in the XRandR layout; translate them to window-local
    // coordinates and keep globalPosition/logicalSize untouched for span
    // calculations.
    int minX = INT_MAX;
    int minY = INT_MAX;
    int maxX = INT_MIN;
    int maxY = INT_MIN;

    for (const auto* screen : this->m_screens) {
	minX = std::min (minX, screen->viewport.x);
	minY = std::min (minY, screen->viewport.y);
	maxX = std::max (maxX, screen->viewport.x + screen->viewport.z);
	maxY = std::max (maxY, screen->viewport.y + screen->viewport.w);
    }

    this->m_fullWidth = maxX - minX;
    this->m_fullHeight = maxY - minY;

    if (this->m_fullWidth <= 0 || this->m_fullHeight <= 0) {
	sLog.error ("Cannot determine the X11 desktop window geometry from the requested outputs");
	return;
    }

    for (auto* screen : this->m_screens) {
	screen->viewport.x -= minX;
	screen->viewport.y -= minY;
    }

    this->m_driver.resizeWindow ({ minX, minY, this->m_fullWidth, this->m_fullHeight });

    if (!this->m_driver.configureAsDesktopWindow ()) {
	sLog.error ("Cannot configure the driver window as an X11 desktop window");
	return;
    }

    this->m_driver.showWindow ();
    // The window manager may have applied its own geometry and stacking while
    // mapping the window, so apply both again once it is visible.
    this->m_driver.lowerWindow ();
    this->m_driver.resizeWindow ({ minX, minY, this->m_fullWidth, this->m_fullHeight });
}

void X11DesktopWindowOutput::freePresentation () { this->m_driver.hideWindow (); }

bool X11DesktopWindowOutput::renderVFlip () const { return true; }

bool X11DesktopWindowOutput::haveImageBuffer () const { return false; }

void* X11DesktopWindowOutput::getImageBuffer () const { return nullptr; }

uint32_t X11DesktopWindowOutput::getImageBufferSize () const { return 0; }

void X11DesktopWindowOutput::updateRender () const {
    // Presentation happens in the driver's buffer swap; nothing to copy here.
}

void X11DesktopWindowOutput::onFramePresented () const { this->notifyFirstFrame (); }
