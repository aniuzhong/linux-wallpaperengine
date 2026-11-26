#pragma once

#include <atomic>
#include <string>

#include "include/cef_client.h"

namespace WallpaperEngine::WebBrowser::CEF {
// *************************************************************************
//! \brief Provide access to browser-instance-specific callbacks. A single
//! CefClient instance can be shared among any number of browsers.
// *************************************************************************
class BrowserClient : public CefClient, public CefLifeSpanHandler, public CefLoadHandler, public CefDisplayHandler {
public:
    BrowserClient (CefRefPtr<CefRenderHandler> ptr, const std::string& userProperties, int fps);

    [[nodiscard]] CefRefPtr<CefRenderHandler> GetRenderHandler () override;
    [[nodiscard]] CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler () override;
    [[nodiscard]] CefRefPtr<CefLoadHandler> GetLoadHandler () override;
    [[nodiscard]] CefRefPtr<CefDisplayHandler> GetDisplayHandler () override;

    void OnLoadEnd (CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode) override;
    bool OnConsoleMessage (
	CefRefPtr<CefBrowser> browser, cef_log_severity_t level, const CefString& message, const CefString& source,
	int line
    ) override;

    //! Called when the browser is irrevocably closed: the render widget is
    //! gone and no more browser callbacks will arrive. Shutdown pumps the
    //! message loop until this fires after CloseBrowser(true), calling
    //! CefShutdown with a browser still alive deadlocks it
    void OnBeforeClose (CefRefPtr<CefBrowser> browser) override;

    [[nodiscard]] bool isBrowserClosed () const { return this->m_browserClosed.load (std::memory_order_acquire); }

    CefRefPtr<CefRenderHandler> m_renderHandler = nullptr;

private:
    /** JS snippet that hands the project's properties to the web wallpaper */
    std::string m_applyPropertiesScript;
    std::atomic<bool> m_browserClosed { false };

    IMPLEMENT_REFCOUNTING (BrowserClient);
};
} // namespace WallpaperEngine::WebBrowser::CEF
