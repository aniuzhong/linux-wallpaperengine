#include "BrowserClient.h"
#include "WallpaperEngine/Logging/Log.h"

#include <algorithm>

using namespace WallpaperEngine::WebBrowser::CEF;

BrowserClient::BrowserClient (CefRefPtr<CefRenderHandler> ptr, const std::string& userProperties, int fps) :
    m_renderHandler (std::move (ptr)) {
    // applyGeneralProperties must arrive first: some wallpapers start their
    // render loop with the FPS value and break if it is still undefined.
    const std::string generalProperties = "{\"fps\":" + std::to_string (std::max (1, fps)) + "}";

    this->m_applyPropertiesScript = "if (window.__wpengine && window.__wpengine.applyProperties) {"
				    "window.__wpengine.applyProperties("
	+ generalProperties + ", " + userProperties + ");}";
}

CefRefPtr<CefRenderHandler> BrowserClient::GetRenderHandler () { return this->m_renderHandler; }

CefRefPtr<CefLifeSpanHandler> BrowserClient::GetLifeSpanHandler () { return this; }

CefRefPtr<CefLoadHandler> BrowserClient::GetLoadHandler () { return this; }

CefRefPtr<CefDisplayHandler> BrowserClient::GetDisplayHandler () { return this; }

void BrowserClient::OnLoadEnd (CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode) {
    if (!frame->IsMain ()) {
	return;
    }

    frame->ExecuteJavaScript (this->m_applyPropertiesScript, frame->GetURL (), 0);
}

bool BrowserClient::OnConsoleMessage (
    CefRefPtr<CefBrowser> browser, cef_log_severity_t level, const CefString& message, const CefString& source, int line
) {
    if (level >= LOGSEVERITY_ERROR) {
	sLog.error ("CEF console: ", source.ToString (), ":", line, ": ", message.ToString ());
    } else {
	sLog.debug ("CEF console: ", source.ToString (), ":", line, ": ", message.ToString ());
    }

    return false;
}

void BrowserClient::OnBeforeClose (CefRefPtr<CefBrowser> browser) {
    this->m_browserClosed.store (true, std::memory_order_release);
}
