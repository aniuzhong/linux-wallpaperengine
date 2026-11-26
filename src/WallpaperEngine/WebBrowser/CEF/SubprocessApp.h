#pragma once

#include "WPSchemeHandlerFactory.h"
#include "include/cef_app.h"
#include "include/cef_render_process_handler.h"

#include <map>
#include <string>
#include <vector>

namespace WallpaperEngine::WebBrowser::CEF {
/**
 * CEF app for subprocess processes (zygote, renderer, gpu, utility...). These
 * processes must enter CefExecuteProcess immediately when launched, long before
 * the full application exists, so this class cannot depend on it. Custom scheme
 * registration only needs the background ids, which arrive as positional
 * arguments in the subprocess command line.
 *
 * It also implements CefRenderProcessHandler so every renderer context gets the
 * Wallpaper Engine web API before any page script runs.
 */
class SubprocessApp : public CefApp, public CefRenderProcessHandler {
public:
    explicit SubprocessApp (const std::vector<std::string>& workshopIds);

    void OnRegisterCustomSchemes (CefRawPtr<CefSchemeRegistrar> registrar) override;
    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler () override;
    void OnContextCreated (
	CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context
    ) override;

protected:
    const std::map<std::string, WPSchemeHandlerFactory*>& getHandlerFactories () const;
    std::map<std::string, WPSchemeHandlerFactory*> m_handlerFactories = {};
    std::vector<std::string> m_workshopIds = {};

private:
    IMPLEMENT_REFCOUNTING (SubprocessApp);
    DISALLOW_COPY_AND_ASSIGN (SubprocessApp);
};
} // namespace WallpaperEngine::WebBrowser::CEF
