#include "SubprocessApp.h"
#include "WPSchemeHandlerFactory.h"

#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/WebBrowser/CEF/WebApi.generated.h"
#include "include/cef_v8.h"

using namespace WallpaperEngine::WebBrowser::CEF;

SubprocessApp::SubprocessApp (const std::vector<std::string>& workshopIds) : m_workshopIds (workshopIds) { }

void SubprocessApp::OnRegisterCustomSchemes (CefRawPtr<CefSchemeRegistrar> registrar) {
    // register all the needed schemes, "wp" + the background id is going to be our scheme
    for (const auto& workshopId : this->m_workshopIds) {
	registrar->AddCustomScheme (
	    WPSchemeHandlerFactory::generateSchemeName (workshopId),
	    CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE | CEF_SCHEME_OPTION_FETCH_ENABLED
		| CEF_SCHEME_OPTION_CORS_ENABLED
	);
    }
}

CefRefPtr<CefRenderProcessHandler> SubprocessApp::GetRenderProcessHandler () { return this; }

void SubprocessApp::OnContextCreated (
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context
) {
    // The Wallpaper Engine API belongs to the wallpaper page itself. Injecting
    // it into cross-origin iframes would only pollute their globals and patch
    // their history implementation for no benefit.
    if (!frame->IsMain ()) {
	return;
    }

    CefRefPtr<CefV8Value> result;
    CefRefPtr<CefV8Exception> exception;

    if (!context->Eval (WEB_API_SCRIPT, frame->GetURL (), 0, result, exception) && exception) {
	sLog.error ("Cannot install the web wallpaper API: ", exception->GetMessage ().ToString ());
    }
}

const std::map<std::string, WPSchemeHandlerFactory*>& SubprocessApp::getHandlerFactories () const {
    return this->m_handlerFactories;
}
