#include "BrowserApp.h"
#include "WallpaperEngine/Logging/Log.h"

#include <filesystem>
#include <ranges>

using namespace WallpaperEngine::WebBrowser::CEF;

static std::vector<std::string> collectSchemeIds (WallpaperEngine::Application::WallpaperApplication& application) {
    std::vector<std::string> ids;

    for (const auto& info : application.getBackgrounds () | std::views::values) {
	ids.push_back (info->workshopId);
    }

    return ids;
}

BrowserApp::BrowserApp (WallpaperEngine::Application::WallpaperApplication& application) :
    // custom schemes MUST be registered with the same names in EVERY process
    // (browser, zygote, renderer, utility): the mojo wire format used to
    // serialize URLs and origins across processes depends on whether the
    // scheme is registered as standard there, and mismatched registrations
    // make messages fail deserialization, killing renderers with
    // "Terminating render process for bad Mojo message". OnRegisterCustomSchemes
    // runs before this constructor body, so the ids reach the base class
    // through its constructor
    SubprocessApp (collectSchemeIds (application)), m_application (application) {
    std::vector<std::string> ids;

    for (const auto& info : application.getBackgrounds () | std::views::values) {
	this->m_handlerFactories[info->workshopId] = new WPSchemeHandlerFactory (*info);
	ids.push_back (info->workshopId);
    }

    // subprocesses register their own schemes through the same mechanism,
    // the ids travel through the environment instead of the command line:
    // re-appending arguments to the child command line is unreliable here,
    // the appended arguments end up merged into a single token together with
    // CEF's own switches and never reach main() as separate argv entries.
    // Environment variables are inherited by both exec'd and zygote-forked
    // children untouched
    std::string joined;

    for (const auto& id : ids) {
	joined += (joined.empty () ? "" : ",") + id;
    }

    setenv ("WPENGINE_WORKSHOP_IDS", joined.c_str (), 1);
}

CefRefPtr<CefBrowserProcessHandler> BrowserApp::GetBrowserProcessHandler () { return this; }

void BrowserApp::OnContextInitialized () {
    // register all the needed schemes, "wp" + the background id is going to be our scheme
    for (const auto& [workshopId, factory] : this->getHandlerFactories ()) {
	CefRegisterSchemeHandlerFactory (
	    WPSchemeHandlerFactory::generateSchemeName (workshopId), static_cast<const char*> (nullptr), factory
	);
    }
}

void BrowserApp::OnBeforeCommandLineProcessing (const CefString& process_type, CefRefPtr<CefCommandLine> command_line) {
    // off-screen rendering never presents through a window system, but CEF
    // still chooses an ozone platform from the session environment. Under a
    // Wayland session the browser process connects to the compositor during
    // CefInitialize and can deadlock inside the linux-dmabuf negotiation
    // (observed with mutter + vmwgfx): the main thread gets stuck on a futex
    // before the first frame, the wallpaper window stays frozen at the bottom
    // layer and the child processes terminate themselves after 15 seconds
    // without a mojo connection. The headless ozone platform keeps every CEF
    // process away from the display server and makes the behavior identical
    // no matter which session type the app was started from
    if (!command_line->HasSwitch ("ozone-platform")) {
	command_line->AppendSwitchWithValue ("ozone-platform", "headless");
    }

    command_line->AppendSwitchWithValue (
	"--disable-features",
	"IsolateOrigins,HardwareMediaKeyHandling,WebContentsOcclusion,RendererCodeIntegrityEnabled,site-per-process"
    );
    command_line->AppendSwitch ("--disable-gpu-shader-disk-cache");
    command_line->AppendSwitch ("--disable-site-isolation-trials");
    command_line->AppendSwitch ("--disable-web-security");
    // pages opened from disk need to read the file properties the user picked
    // (images, presets), which arrive as absolute paths
    command_line->AppendSwitch ("--allow-file-access-from-files");
    command_line->AppendSwitchWithValue ("--remote-allow-origins", "*");
    // the profile is a throwaway directory recreated every run: skip the
    // desktop keyring entirely, it can stall startup ~25s on KDE sessions
    // waiting for KWallet to answer
    command_line->AppendSwitchWithValue ("--password-store", "basic");
    command_line->AppendSwitchWithValue ("--autoplay-policy", "no-user-gesture-required");
    command_line->AppendSwitch ("--disable-background-timer-throttling");
    command_line->AppendSwitch ("--disable-backgrounding-occluded-windows");
    command_line->AppendSwitch ("--disable-background-media-suspend");
    command_line->AppendSwitch ("--disable-renderer-backgrounding");
    command_line->AppendSwitch ("--disable-test-root-certs");
    command_line->AppendSwitch ("--disable-bundled-ppapi-flash");
    command_line->AppendSwitch ("--disable-breakpad");
    command_line->AppendSwitch ("--disable-field-trial-config");
    command_line->AppendSwitch ("--no-experiments");
    // TODO: ACTIVATE THIS IF WE EVER SUPPORT MACOS OFFICIALLY
    /*
if (process_type.empty()) {
#if defined(OS_MACOSX)
  // Disable the macOS keychain prompt. Cookies will not be encrypted.
  command_line->AppendSwitch("use-mock-keychain");
#endif
}*/
}

const WallpaperEngine::Application::WallpaperApplication& BrowserApp::getApplication () const {
    return this->m_application;
}
