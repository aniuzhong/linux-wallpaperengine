(function () {
    if (window.__wpengine) {
        return;
    }

    var engine = {
        audioListener: null,
    };

    try {
        // keep the bridge out of the page's global namespace enumeration and
        // prevent it from being accidentally overwritten or deleted
        Object.defineProperty(window, '__wpengine', {
            value: engine,
            configurable: false,
            enumerable: false,
            writable: false,
        });
    } catch (e) {
        window.__wpengine = engine;
    }

    // Chromium refuses history.pushState/replaceState calls that would change
    // a file:// document to another file path. Single-page routers (Angular's
    // PathLocationStrategy for example) use those calls during their initial
    // navigation and abort the whole page when the SecurityError is thrown.
    // Swallow only that specific error so the router can keep its internal
    // state; relative asset URLs still resolve against the project directory.
    function ignoreFileHistoryError(original) {
        return function () {
            try {
                return original.apply(this, arguments);
            } catch (e) {
                if (e && e.name === 'SecurityError' && location.protocol === 'file:') {
                    return undefined;
                }

                throw e;
            }
        };
    }

    try {
        history.pushState = ignoreFileHistoryError(history.pushState);
        history.replaceState = ignoreFileHistoryError(history.replaceState);
    } catch (e) {
    }

    function silentAudio() {
        return Array(128).fill(0);
    }

    window.wallpaperRegisterAudioListener = function (listener) {
        engine.audioListener = listener;

        if (typeof listener === 'function') {
            // defer the initial frame so the page can finish initializing the
            // variables its audio listener uses
            setTimeout(function () {
                try {
                    listener(silentAudio());
                } catch (e) {
                }
            }, 0);
        }
    };

    window.wallpaperRegisterMediaPropertiesListener = function (listener) {
        engine.mediaPropertiesListener = listener;
    };
    window.wallpaperRegisterMediaThumbnailListener = function (listener) {
        engine.mediaThumbnailListener = listener;
    };
    window.wallpaperRegisterMediaStatusListener = function (listener) {
        engine.mediaStatusListener = listener;
    };
    window.wallpaperRegisterMediaPlaybackListener = function (listener) {
        engine.mediaPlaybackListener = listener;
    };
    window.wallpaperRegisterMediaTimelineListener = function (listener) {
        engine.mediaTimelineListener = listener;
    };

    window.wallpaperRequestRandomFileForProperty = function (name, callback) {
        if (typeof callback === 'function') {
            try {
                callback(name, '');
            } catch (e) {
            }
        }
    };

    window.wallpaperMediaIntegration = {
        PLAYBACK_PLAYING: 0,
        PLAYBACK_PAUSED: 1,
        PLAYBACK_STOPPED: 2,
        playback: {
            PLAYING: 0,
            PAUSED: 1,
            STOPPED: 2,
        },
    };

    function reportApplyError(error) {
        if (window.console && window.console.error) {
            window.console.error('wpengine: failed to apply properties', error);
        }
    }

    engine.applyProperties = function (general, user) {
        var attempts = 0;
        var generalApplied = false;

        function apply() {
            var listener = window.wallpaperPropertyListener;
            var hasGeneral = listener && typeof listener.applyGeneralProperties === 'function';
            var hasUser = listener && typeof listener.applyUserProperties === 'function';

            if (!hasGeneral && !hasUser) {
                // some frameworks define the listener asynchronously during
                // bootstrap, keep retrying for a few seconds before giving up
                if (++attempts < 200) {
                    setTimeout(apply, 50);
                }
                return;
            }

            if (hasGeneral && !generalApplied) {
                try {
                    listener.applyGeneralProperties(general || {});
                    generalApplied = true;
                } catch (e) {
                    reportApplyError(e);
                }
            }

            if (hasUser) {
                try {
                    listener.applyUserProperties(user || {});
                } catch (e) {
                    reportApplyError(e);
                }
                return;
            }

            // a page may assign the listener's callbacks incrementally; keep
            // polling for applyUserProperties instead of giving up early
            if (++attempts < 200) {
                setTimeout(apply, 50);
            }
        }

        apply();
    };
})();
