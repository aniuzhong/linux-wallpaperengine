#pragma once

#include "X11Output.h"

namespace WallpaperEngine::Render::Drivers::Output {
/**
 * Root-pixmap presentation backend.
 *
 * The rendered frame is read back from the GL framebuffer and copied into a
 * pixmap that is installed as the X11 root background (_XROOTPMAP_ID).
 */
class X11RootPixmapOutput final : public X11Output {
public:
    X11RootPixmapOutput (ApplicationContext& context, VideoDriver& driver);
    ~X11RootPixmapOutput () override;

    bool renderVFlip () const override;
    bool haveImageBuffer () const override;
    void* getImageBuffer () const override;
    uint32_t getImageBufferSize () const override;
    void updateRender () const override;

protected:
    void initPresentation () override;
    void freePresentation () override;

private:
    Pixmap m_pixmap = None;
    GC m_gc = nullptr;
    char* m_imageData = nullptr;
    uint32_t m_imageSize = 0;
    XImage* m_image = nullptr;
};
} // namespace WallpaperEngine::Render::Drivers::Output
