#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "WallpaperEngine/Application/ApplicationContext.h"
#include "WallpaperEngine/Application/WallpaperApplication.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/WebBrowser/CEF/SubprocessApp.h"
#include "include/cef_app.h"

WallpaperEngine::Application::WallpaperApplication* app;

// async-signal-safe stop request: the OS signal handler may interrupt any
// thread, including CEF threads in the middle of malloc or logging, so it can
// only write this flag; the render loop polls it and main() reports the
// signal once the loop has stopped
static volatile sig_atomic_t g_stopRequested = 0;

void signalhandler (const int sig) { g_stopRequested = sig; }

void initLogging () {
    sLog.addOutput (new std::ostream (std::cout.rdbuf ()));
    sLog.addError (new std::ostream (std::cerr.rdbuf ()));
}

int main (int argc, char* argv[]) {
    try {
	// if type parameter is specified, this is a subprocess, so no logging should be enabled from our side
	bool enableLogging = true;
	const std::string typeZygote = "--type=zygote";
	const std::string typeUtility = "--type=utility";

	bool isSubprocess = false;

	for (int i = 1; i < argc; i++) {
	    if (strncmp (typeZygote.c_str (), argv[i], typeZygote.size ()) == 0) {
		enableLogging = false;
	    }

	    if (strncmp (typeUtility.c_str (), argv[i], typeUtility.size ()) == 0) {
		enableLogging = false;
	    }

	    if (strncmp (argv[i], "--type=", 7) == 0) {
		isSubprocess = true;
	    }
	}

	if (isSubprocess) {
	    // CEF subprocess processes (zygote, renderer, gpu, utility...) must
	    // enter CefExecuteProcess immediately: the full application cannot
	    // run here, its argument parser would reject CEF's own switches and
	    // the zygote would die before answering the browser's ping
	    CefMainArgs mainArgs (argc, argv);
	    // the browser process exports the workshop ids through the
	    // environment (see BrowserApp's constructor) so subprocesses can
	    // register the custom scheme names with CEF; they cannot travel on
	    // the command line, appended arguments end up merged into a single
	    // token there and never arrive as separate argv entries
	    std::vector<std::string> workshopIds;

	    if (const char* ids = getenv ("WPENGINE_WORKSHOP_IDS")) {
		std::string token;

		for (size_t i = 0;; i++) {
		    if (ids[i] == '\0' || ids[i] == ',') {
			if (!token.empty ()) {
			    workshopIds.push_back (token);
			}

			token.clear ();

			if (ids[i] == '\0') {
			    break;
			}
		    } else {
			token += ids[i];
		    }
		}
	    }

	    CefRefPtr<WallpaperEngine::WebBrowser::CEF::SubprocessApp> subprocessApp
		= new WallpaperEngine::WebBrowser::CEF::SubprocessApp (workshopIds);
	    return CefExecuteProcess (mainArgs, subprocessApp, nullptr);
	}

	if (enableLogging) {
	    initLogging ();
	}

	WallpaperEngine::Application::ApplicationContext appContext (argc, argv);

	appContext.loadSettingsFromArgv ();

	app = new WallpaperEngine::Application::WallpaperApplication (appContext);

	// halt if the list-properties option was specified
	if (appContext.settings.general.onlyListProperties) {
	    delete app;
	    return 0;
	}

	// attach signals to gracefully stop: the handler may interrupt any
	// thread, including CEF threads, so it can only write the
	// async-signal-safe flag above; the render loop polls it and the stop is
	// reported below, on the main thread
	std::signal (SIGINT, signalhandler);
	std::signal (SIGTERM, signalhandler);

	// let the render loop watch the flag written by the signal handler
	app->setStopFlag (&g_stopRequested);

	// show the wallpaper application
	app->show ();

	if (g_stopRequested != 0) {
	    sLog.out ("Stop requested by signal ", static_cast<int> (g_stopRequested));
	}

	delete app;

	return 0;
    } catch (const std::exception& e) {
	std::cerr << e.what () << std::endl;
	return 1;
    }
}