#pragma once

#include "X11Output.h"

namespace WallpaperEngine::Render::Drivers::Output {
/**
 * Desktop-window presentation backend.
 *
 * The driver's GLFW window is turned into an X11 desktop window covering the
 * bounding box of the requested outputs. Frames are presented directly by the
 * driver's buffer swap, so no framebuffer readback or root pixmap is needed.
 *
 * This backend assumes the desktop shell (for example peony) keeps its own
 * desktop window above this one and leaves it transparent where the wallpaper
 * should show through.
 */
class X11DesktopWindowOutput final : public X11Output {
public:
    X11DesktopWindowOutput (ApplicationContext& context, VideoDriver& driver);
    ~X11DesktopWindowOutput () override;

    bool renderVFlip () const override;
    bool haveImageBuffer () const override;
    void* getImageBuffer () const override;
    uint32_t getImageBufferSize () const override;
    void updateRender () const override;
    void onFramePresented () const override;

protected:
    void initPresentation () override;
    void freePresentation () override;
};
} // namespace WallpaperEngine::Render::Drivers::Output
