#pragma once

#include <glm/vec4.hpp>
#include <map>
#include <string>

#include "WallpaperEngine/Application/ApplicationContext.h"
#include "WallpaperEngine/Render/Drivers/Detectors/FullScreenDetector.h"

using namespace WallpaperEngine::Application;

namespace WallpaperEngine::Application {
class ApplicationContext;
}

namespace WallpaperEngine::Render::Drivers {
class VideoDriver;

namespace Detectors {
    class FullScreenDetector;
}

namespace Output {
    class OutputViewport;

    class Output {
    public:
	Output (ApplicationContext& context, VideoDriver& driver);
	virtual ~Output () = default;

	virtual void reset () = 0;

	int getFullWidth () const;
	int getFullHeight () const;

	virtual bool renderVFlip () const = 0;
	virtual bool renderMultiple () const = 0;
	virtual bool haveImageBuffer () const = 0;
	const std::map<std::string, OutputViewport*>& getViewports () const;
	virtual void* getImageBuffer () const = 0;
	virtual uint32_t getImageBufferSize () const = 0;
	virtual void updateRender () const = 0;
	/**
	 * Called by the driver after a frame has been presented (after the buffer
	 * swap). Outputs that display the driver window directly can use this to
	 * signal that the first frame is actually visible.
	 */
	virtual void onFramePresented () const { }

    protected:
	/**
	 * Creates the first-frame marker exactly once. The wrapper uses it to keep
	 * the native desktop opaque until the engine has something on screen.
	 */
	void notifyFirstFrame () const;

	mutable int m_fullWidth = 0;
	mutable int m_fullHeight = 0;
	mutable std::map<std::string, OutputViewport*> m_viewports = {};
	ApplicationContext& m_context;
	VideoDriver& m_driver;

    private:
	mutable bool m_firstFrameNotified = false;
    };
} // namespace Output
} // namespace WallpaperEngine::Render::Drivers
