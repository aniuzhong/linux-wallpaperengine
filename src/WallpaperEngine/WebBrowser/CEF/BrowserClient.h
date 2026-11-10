#pragma once

#include <atomic>

#include "include/cef_client.h"

namespace WallpaperEngine::WebBrowser::CEF {
// *************************************************************************
//! \brief Provide access to browser-instance-specific callbacks. A single
//! CefClient instance can be shared among any number of browsers.
// *************************************************************************
class BrowserClient : public CefClient, public CefLifeSpanHandler {
public:
    explicit BrowserClient (CefRefPtr<CefRenderHandler> ptr);

    [[nodiscard]] CefRefPtr<CefRenderHandler> GetRenderHandler () override;
    [[nodiscard]] CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler () override;

    //! Called when the browser is irrevocably closed: the render widget is
    //! gone and no more browser callbacks will arrive. Shutdown pumps the
    //! message loop until this fires after CloseBrowser(true), calling
    //! CefShutdown with a browser still alive deadlocks it
    void OnBeforeClose (CefRefPtr<CefBrowser> browser) override;

    [[nodiscard]] bool isBrowserClosed () const { return this->m_browserClosed.load (std::memory_order_acquire); }

    CefRefPtr<CefRenderHandler> m_renderHandler = nullptr;

private:
    std::atomic<bool> m_browserClosed { false };

    IMPLEMENT_REFCOUNTING (BrowserClient);
};
} // namespace WallpaperEngine::WebBrowser::CEF
