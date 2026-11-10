#pragma once

#include "WPSchemeHandlerFactory.h"
#include "include/cef_app.h"

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
 */
class SubprocessApp : public CefApp {
public:
    explicit SubprocessApp (const std::vector<std::string>& workshopIds);

    void OnRegisterCustomSchemes (CefRawPtr<CefSchemeRegistrar> registrar) override;

protected:
    const std::map<std::string, WPSchemeHandlerFactory*>& getHandlerFactories () const;
    std::map<std::string, WPSchemeHandlerFactory*> m_handlerFactories = {};
    std::vector<std::string> m_workshopIds = {};

private:
    IMPLEMENT_REFCOUNTING (SubprocessApp);
    DISALLOW_COPY_AND_ASSIGN (SubprocessApp);
};
} // namespace WallpaperEngine::WebBrowser::CEF
