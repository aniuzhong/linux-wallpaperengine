#include "X11Output.h"

#include "GLFWOutputViewport.h"
#include "WallpaperEngine/Logging/Log.h"

#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#include <ranges>

using namespace WallpaperEngine::Render::Drivers::Output;

namespace {
void CustomXIOErrorExitHandler (Display* dsp, void* userdata) {
    const auto context = static_cast<X11Output*> (userdata);

    sLog.debugerror ("Critical XServer error detected. Attempting to recover...");

    // refetch all the resources
    context->reset ();
}

int CustomXErrorHandler (Display* dpy, XErrorEvent* event) {
    sLog.debugerror ("Detected X error");

    return 0;
}

int CustomXIOErrorHandler (Display* dsp) {
    sLog.debugerror ("Detected X error");

    return 0;
}
} // namespace

X11Output::X11Output (ApplicationContext& context, VideoDriver& driver) : Output (context, driver) {
    // do not use previous handler, it might stop the app under weird circumstances
    XSetErrorHandler (CustomXErrorHandler);
    XSetIOErrorHandler (CustomXIOErrorHandler);
}

X11Output::~X11Output () {
    this->beginDestruction ();
    this->teardown ();
}

void X11Output::beginDestruction () { this->m_destroying = true; }

void X11Output::initialize () {
    this->openDisplay ();
    this->loadLayout ();
    this->initPresentation ();
}

void X11Output::teardown () {
    // free presentation before the display used by it is closed; during
    // destruction the derived destructor has already done so
    if (!this->m_destroying) {
	this->freePresentation ();
    }

    this->freeViewports ();
    this->closeDisplay ();
}

void X11Output::reset () {
    // an X I/O error can be reported while the connection is being closed
    if (this->m_destroying) {
	return;
    }

    this->teardown ();
    this->initialize ();

    // TODO: BRING BACK THIS FUNCTIONALITY
    // this->m_driver.getFullscreenDetector ().reset ();
}

void X11Output::openDisplay () {
    this->m_display = XOpenDisplay (nullptr);
#ifdef HAVE_XSETIOERROREXITHANDLER
    if (this->m_display != nullptr) {
	XSetIOErrorExitHandler (this->m_display, CustomXIOErrorExitHandler, this);
    }
#endif /* HAVE_XSETIOERROREXITHANDLER */
}

void X11Output::freeViewports () {
    // delete owned viewport objects (m_viewports holds non-owning aliases)
    for (const auto& screen : this->m_screens) {
	delete screen;
    }

    this->m_screens.clear ();
    this->m_viewports.clear ();
}

void X11Output::closeDisplay () {
    if (this->m_display != nullptr) {
	XCloseDisplay (this->m_display);
	this->m_display = nullptr;
    }

    this->m_root = None;
    this->m_fullWidth = 0;
    this->m_fullHeight = 0;
}

void X11Output::loadLayout () {
    if (this->m_display == nullptr) {
	return;
    }

    int xrandr_result, xrandr_error;

    if (!XRRQueryExtension (this->m_display, &xrandr_result, &xrandr_error)) {
	sLog.error ("XRandr is not present, cannot detect specified screens, running in window mode");
	return;
    }

    this->m_root = DefaultRootWindow (this->m_display);
    this->m_fullWidth = DisplayWidth (this->m_display, DefaultScreen (this->m_display));
    this->m_fullHeight = DisplayHeight (this->m_display, DefaultScreen (this->m_display));
    XRRScreenResources* screenResources = XRRGetScreenResources (this->m_display, DefaultRootWindow (this->m_display));

    if (screenResources == nullptr) {
	sLog.error ("Cannot detect screen sizes using xrandr, running in window mode");
	return;
    }

    discoverOutputs (screenResources);
    XRRFreeScreenResources (screenResources);
    validateOutputs ();
}

void X11Output::discoverOutputs (XRRScreenResources* screenResources) {
    for (int i = 0; i < screenResources->noutput; i++) {
	const XRROutputInfo* info = XRRGetOutputInfo (this->m_display, screenResources, screenResources->outputs[i]);

	// screen not in use, ignore it
	if (info == nullptr || info->connection != RR_Connected) {
	    continue;
	}

	XRRCrtcInfo* crtc = XRRGetCrtcInfo (this->m_display, screenResources, info->crtc);

	// screen not active, ignore it
	if (crtc == nullptr) {
	    continue;
	}

	// check if this screen is part of a span group
	bool inSpanGroup = false;
	for (const auto& spanGroup : this->m_context.settings.general.spanGroups) {
	    for (const auto& screen : spanGroup.screens) {
		if (screen == info->name) {
		    inSpanGroup = true;
		    break;
		}
	    }
	    if (inSpanGroup) {
		break;
	    }
	}

	// only keep info of registered screens
	if (inSpanGroup
	    || this->m_context.settings.general.screenBackgrounds.find (info->name)
		!= this->m_context.settings.general.screenBackgrounds.end ()) {
	    sLog.out (
		"Found requested screen: ", info->name, " -> ", crtc->x, "x", crtc->y, ":", crtc->width, "x",
		crtc->height
	    );

	    auto* vp = new GLFWOutputViewport { { crtc->x, crtc->y, crtc->width, crtc->height }, info->name };
	    vp->globalPosition = { crtc->x, crtc->y };
	    vp->logicalSize = { crtc->width, crtc->height };
	    this->m_screens.push_back (vp);
	    this->m_viewports[info->name] = vp;
	}

	XRRFreeCrtcInfo (crtc);
    }
}

void X11Output::validateOutputs () const {
    bool any = false;

    for (const auto& o : this->m_screens) {
	const auto cur = this->m_context.settings.general.screenBackgrounds.find (o->name);

	if (cur != this->m_context.settings.general.screenBackgrounds.end ()) {
	    any = true;
	    break;
	}

	// also check span groups
	for (const auto& spanGroup : this->m_context.settings.general.spanGroups) {
	    for (const auto& screen : spanGroup.screens) {
		if (screen == o->name) {
		    any = true;
		    break;
		}
	    }
	    if (any) {
		break;
	    }
	}
	if (any) {
	    break;
	}
    }

    if (!any) {
	sLog.error ("No outputs could be initialized, please check the parameters and try again");
	sLog.error ("Detected outputs:");

	for (const auto& o : this->m_screens) {
	    sLog.error ("  ", o->name);
	}

	sLog.error ("Requested: ");

	for (const auto& o : this->m_context.settings.general.screenBackgrounds | std::views::keys) {
	    sLog.error ("  ", o);
	}

	sLog.exception ("Cannot continue...");
    }
}

bool X11Output::renderMultiple () const { return this->m_viewports.size () > 1; }
