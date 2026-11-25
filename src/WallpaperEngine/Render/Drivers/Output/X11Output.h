#pragma once

#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#include <vector>

#include "Output.h"

namespace WallpaperEngine::Render::Drivers::Output {
/**
 * Shared X11 output base: display, XRandR screen discovery and viewports.
 *
 * Presentation is implemented by subclasses, which must call initialize() once
 * their own construction is complete:
 *  - X11RootPixmapOutput writes the rendered frame into the root pixmap.
 */
class X11Output : public Output {
public:
    X11Output (ApplicationContext& context, VideoDriver& driver);
    ~X11Output () override;

    void reset () override final;
    bool renderMultiple () const override;

protected:
    /**
     * Connects to the X server, loads the layout and initializes presentation.
     *
     * This calls into the presentation hooks, so it is only valid while the
     * most-derived object is fully constructed (i.e. from the derived
     * constructor body or from reset()).
     */
    void initialize ();
    void teardown ();
    // Subclasses must call this before releasing their presentation resources
    // in their destructor so an X I/O error callback cannot re-enter reset().
    void beginDestruction ();

    void openDisplay ();
    void closeDisplay ();
    void loadLayout ();
    void freeViewports ();
    void discoverOutputs (XRRScreenResources* screenResources);
    void validateOutputs () const;

    virtual void initPresentation () = 0;
    virtual void freePresentation () = 0;

    Display* m_display = nullptr;
    Window m_root = None;
    std::vector<OutputViewport*> m_screens = {};

private:
    // Set while the object is being destroyed so X error callbacks cannot call
    // into presentation hooks of an already-destroyed subclass.
    bool m_destroying = false;
};
} // namespace WallpaperEngine::Render::Drivers::Output
