#include "ScriptEngine.h"

#include "Adapters/ScriptableObjectAdapter.h"
#include "Modules/ColorModule.h"
#include "Modules/MathModule.h"
#include "Modules/ScriptModule.h"
#include "Modules/VectorModule.h"
#include "ScriptPropertiesObject.h"
#include "ScriptableObject.h"
#include "WallpaperEngine/Audio/AudioContext.h"
#include "WallpaperEngine/Audio/Drivers/Recorders/PlaybackRecorder.h"
#include "WallpaperEngine/Data/Model/Property.h"
#include "WallpaperEngine/Data/Utils/ScopeGuard.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Render/CObject.h"
#include "WallpaperEngine/Render/Objects/CSound.h"
#include "WallpaperEngine/Render/Wallpapers/CScene.h"
#include "WallpaperEngine/Scripting/Builtins.generated.h"
#include "quickjs.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <future>
#include <optional>
#include <poll.h>
#include <ranges>
#include <signal.h>
#include <spawn.h>
#include <sstream>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace WallpaperEngine::Render::Objects {
class CSound;
}
using namespace WallpaperEngine::Scripting;
using namespace WallpaperEngine::Data::Model;

extern char** environ;
extern float g_Time;
extern float g_TimeLast;

namespace {
struct TextureAnimationApi {
    WallpaperEngine::Scripting::ScriptableObject* object;
};

const JSClassDef textureAnimationClassDefinition = {
    .class_name = "ITextureAnimation",
    .finalizer =
	[] (JSRuntime*, JSValueConst value) {
	    // class IDs are per-runtime, so read the id from the object being finalized
	    delete static_cast<TextureAnimationApi*> (JS_GetOpaque (value, JS_GetClassID (value)));
	},
};

TextureAnimationApi* get_texture_animation_api (JSValueConst value) {
    return static_cast<TextureAnimationApi*> (JS_GetOpaque (value, JS_GetClassID (value)));
}

JSValue texture_animation_set_frame (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* api = get_texture_animation_api (this_val);

    if (api == nullptr || api->object == nullptr) {
	return JS_UNDEFINED;
    }

    if (argc < 1) {
	return JS_EXCEPTION;
    }

    int32_t frame = 0;

    if (JS_ToInt32 (ctx, &frame, argv[0]) < 0) {
	return JS_EXCEPTION;
    }

    api->object->setTextureAnimationFrame (frame);
    return JS_UNDEFINED;
}

JSValue texture_animation_play (JSContext*, JSValueConst this_val, int, JSValueConst*) {
    auto* api = get_texture_animation_api (this_val);

    if (api == nullptr || api->object == nullptr) {
	return JS_UNDEFINED;
    }

    api->object->playTextureAnimation ();
    return JS_UNDEFINED;
}

JSValue texture_animation_pause (JSContext*, JSValueConst this_val, int, JSValueConst*) {
    auto* api = get_texture_animation_api (this_val);

    if (api == nullptr || api->object == nullptr) {
	return JS_UNDEFINED;
    }

    api->object->pauseTextureAnimation ();
    return JS_UNDEFINED;
}

JSValue texture_animation_stop (JSContext*, JSValueConst this_val, int, JSValueConst*) {
    auto* api = get_texture_animation_api (this_val);

    if (api == nullptr || api->object == nullptr) {
	return JS_UNDEFINED;
    }

    api->object->stopTextureAnimation ();
    return JS_UNDEFINED;
}

JSValue texture_animation_set_framerate (JSContext*, JSValueConst, int, JSValueConst*) {
    // playback speed overrides are not implemented yet; keep the call safe
    return JS_UNDEFINED;
}

constexpr const char* kCursorHandlers[] = {
    "cursorEnter", "cursorLeave", "cursorMove", "cursorDown", "cursorUp", "cursorClick",
};
constexpr uint32_t kCursorHandlerCount = sizeof (kCursorHandlers) / sizeof (kCursorHandlers[0]);

uint32_t cursorHandlerBit (const char* name) {
    for (uint32_t i = 0; i < kCursorHandlerCount; ++i) {
	if (strcmp (name, kCursorHandlers[i]) == 0) {
	    return 1u << i;
	}
    }

    return 0;
}

uint32_t readCursorHandlerMask (JSContext* ctx, JSValue module) {
    uint32_t mask = 0;

    for (uint32_t i = 0; i < kCursorHandlerCount; ++i) {
	JSValue fn = JS_GetPropertyStr (ctx, module, kCursorHandlers[i]);

	if (JS_IsFunction (ctx, fn)) {
	    mask |= 1u << i;
	}

	JS_FreeValue (ctx, fn);
    }

    return mask;
}
} // namespace

void scriptengine_dump (JSContext* ctx, JSValueConst obj) {
    JSPropertyEnum* props;
    uint32_t len;

    if (JS_GetOwnPropertyNames (ctx, &props, &len, obj, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK) < 0) {
	return;
    }

    for (uint32_t i = 0; i < len; ++i) {
	const char* name = JS_AtomToCString (ctx, props[i].atom);

	JSValue val = JS_GetProperty (ctx, obj, props[i].atom);

	const char* value_str = JS_ToCString (ctx, val);

	printf ("%s = %s\n", name, value_str ? value_str : "<non-string>");

	JS_FreeCString (ctx, value_str);
	JS_FreeValue (ctx, val);
	JS_FreeCString (ctx, name);
    }

    js_free (ctx, props);
}

JSModuleDef* scriptengine_module_loader (JSContext* ctx, const char* module, void* opaque) {
    const auto* scriptEngine = static_cast<ScriptEngine*> (opaque);

    const auto& modules = scriptEngine->getModules ();
    const auto it = modules.find (module);

    if (it == modules.end ()) {
	return nullptr;
    }

    return it->second->getDefinition ();
}

JSValue ScriptEngine::userPropertiesToJs () const {
    JSValue result = JS_NewObject (this->m_context);

    for (const auto& [name, property] : this->m_scene.getUserProperties ()) {
	JSValue value;

	if (property->getType () == DynamicValue::Vec4) {
	    // Wallpaper Engine exposes color properties as Vec3.
	    const glm::vec3 color = property->getVec3 ();

	    value = this->m_adapters.vec3->instantiate ();
	    JS_SetPropertyStr (this->m_context, value, "x", JS_NewFloat64 (this->m_context, color.x));
	    JS_SetPropertyStr (this->m_context, value, "y", JS_NewFloat64 (this->m_context, color.y));
	    JS_SetPropertyStr (this->m_context, value, "z", JS_NewFloat64 (this->m_context, color.z));
	} else {
	    value = this->dynamicToJs (*property);
	}

	JS_SetPropertyStr (this->m_context, result, name.c_str (), value);
    }

    return result;
}

JSValue ScriptEngine::dynamicToJs (DynamicValue& value) const {
    switch (value.getType ()) {
	case DynamicValue::Null:
	    return JS_NULL;
	case DynamicValue::String:
	    return JS_NewString (this->m_context, value.getString ().c_str ());
	case DynamicValue::Float:
	    return JS_NewFloat64 (this->m_context, value.getFloat ());
	case DynamicValue::Int:
	    return JS_NewInt32 (this->m_context, value.getInt ());
	case DynamicValue::Boolean:
	    return JS_NewBool (this->m_context, value.getBool ());
	case DynamicValue::Vec2:
	    return this->m_adapters.vec2->instantiate (value, true);
	case DynamicValue::Vec3:
	    return this->m_adapters.vec3->instantiate (value, true);
	case DynamicValue::Vec4:
	    return this->m_adapters.vec4->instantiate (value, true);
	default:
	    return JS_UNDEFINED;
    }
}

/**
 * Convert the value returned by a script into the DynamicValue it drives.
 *
 * @return false when the value cannot be converted and the script should be disabled.
 */
static bool jsToDynamicValue (JSContext* ctx, JSValue val, DynamicValue& source) {
    if (JS_IsException (val)) {
	return false;
    }

    // scalar types returned directly
    int tag = JS_VALUE_GET_TAG (val);

    if (tag == JS_TAG_UNDEFINED || tag == JS_TAG_UNINITIALIZED || tag == JS_TAG_NULL) {
	source.update (DynamicValue::UpdateSource::Script);
	return true;
    }

    if (tag == JS_TAG_INT) {
	source.update (JS_VALUE_GET_INT (val), DynamicValue::UpdateSource::Script);
	return true;
    }

    if (tag == JS_TAG_BOOL) {
	source.update (static_cast<bool> (JS_VALUE_GET_BOOL (val)), DynamicValue::UpdateSource::Script);
	return true;
    }

    if (JS_TAG_IS_FLOAT64 (tag)) {
	source.update (static_cast<float> (JS_VALUE_GET_FLOAT64 (val)), DynamicValue::UpdateSource::Script);
	return true;
    }

    if (tag == JS_TAG_STRING) {
	const char* str = JS_ToCString (ctx, val);

	if (str == nullptr) {
	    // stringification can throw (for example when toString() throws); consume
	    // the secondary exception so it cannot leak into later script calls
	    JS_FreeValue (ctx, JS_GetException (ctx));
	    sLog.error ("ScriptEngine: script returned a string that could not be read");
	    return false;
	}

	source.update (std::string (str), DynamicValue::UpdateSource::Script);
	JS_FreeCString (ctx, str);
	return true;
    }

    // the engine has no job loop, so a promise can never settle
    if (JS_IsPromise (val)) {
	sLog.error ("ScriptEngine: async script results are not supported");
	return false;
    }

    // look into the object and extract x/y/z/w properties
    if (tag == JS_TAG_OBJECT) {
	JSValue x = JS_GetPropertyStr (ctx, val, "x");
	JSValue y = JS_GetPropertyStr (ctx, val, "y");
	JSValue z = JS_GetPropertyStr (ctx, val, "z");
	JSValue w = JS_GetPropertyStr (ctx, val, "w");
	ScopeGuard guard ([=] {
	    JS_FreeValue (ctx, x);
	    JS_FreeValue (ctx, y);
	    JS_FreeValue (ctx, z);
	    JS_FreeValue (ctx, w);
	});

	if (!JS_IsNumber (x) || !JS_IsNumber (y)) {
	    sLog.error ("ScriptEngine: Vector's x and y components must be numbers");
	    return false;
	}

	double xVal = 0.0f, yVal = 0.0f, zVal = 0.0f, wVal = 0.0f;

	JS_ToFloat64 (ctx, &xVal, x);
	JS_ToFloat64 (ctx, &yVal, y);

	if (!JS_IsNumber (z)) {
	    source.update (glm::vec2 (xVal, yVal), DynamicValue::UpdateSource::Script);
	    return true;
	}

	JS_ToFloat64 (ctx, &zVal, z);

	if (!JS_IsNumber (w)) {
	    source.update (glm::vec3 (xVal, yVal, zVal), DynamicValue::UpdateSource::Script);
	    return true;
	}

	JS_ToFloat64 (ctx, &wVal, w);

	source.update (glm::vec4 (xVal, yVal, zVal, wVal), DynamicValue::UpdateSource::Script);
	return true;
    }

    return false;
}

bool ScriptEngine::writeJsValue (JSContext* ctx, JSValue value, DynamicValue& source) {
    return jsToDynamicValue (ctx, value, source);
}

ScriptEngine::ScriptEngine (Wallpapers::CScene& scene, Media::MediaSource& mediaSource) :
    m_scene (scene), m_mediaSource (mediaSource) {
    this->m_unregisterMediaUpdateCallback
	= mediaSource.addMetadataListener ([this] (const Media::MediaSource::MediaInfo& info) {
	      this->notifyMediaUpdate (info);
	  });

    this->m_unregisterAlbumArtUpdateCallback
	= mediaSource.addAlbumArtListener ([this] (const Media::MediaSource::MediaInfo& info) {
	      // TODO: SEPARATE THESE INTO THEIR OWN UPDATES SO JS ONLY RECEIVES THE MEANINGFUL UPDATES
	      this->notifyMediaUpdate (info);
	  });

    this->m_runtime = JS_NewRuntime ();

    if (!this->m_runtime) {
	sLog.exception ("ScriptEngine: Failed to create JS runtime");
    }

    // debug leaks on termination
    JS_SetDumpFlags (this->m_runtime, JS_DUMP_LEAKS);

    this->m_context = JS_NewContext (this->m_runtime);

    if (!this->m_context) {
	JS_FreeRuntime (this->m_runtime);
	sLog.exception ("ScriptEngine: Failed to create JS context");
    }

    this->m_globalThis = JS_GetGlobalObject (this->m_context);

    JS_NewClassID (this->m_runtime, &this->m_textureAnimationClassId);

    if (JS_NewClass (this->m_runtime, this->m_textureAnimationClassId, &textureAnimationClassDefinition) < 0) {
	sLog.error ("ScriptEngine: failed to register ITextureAnimation class");
    }

    this->m_adapters = {
	.vec4 = std::unique_ptr<Adapters::VectorAdapter<4>> (new Adapters::VectorAdapter<4> (*this)),
	.vec3 = std::unique_ptr<Adapters::VectorAdapter<3>> (new Adapters::VectorAdapter<3> (*this)),
	.vec2 = std::unique_ptr<Adapters::VectorAdapter<2>> (new Adapters::VectorAdapter<2> (*this)),
	.object
	= std::unique_ptr<Adapters::ScriptableObjectAdapter> (new Adapters::ScriptableObjectAdapter (*this, "ILayer")),
    };

    this->m_engineObject = std::make_unique<EngineObject> (*this, scene);
    this->m_inputObject = std::make_unique<InputObject> (*this, scene);
    this->m_sceneObject = std::make_unique<SceneObject> (*this, scene);
    this->m_consoleObject = std::make_unique<ConsoleObject> (*this, scene);
    this->m_scriptPropertiesObject = std::make_unique<ScriptPropertiesObject> (*this, scene);
    this->m_localStorageObject = std::make_unique<LocalStorageObject> (*this, scene);

    auto wemath = std::make_unique<Modules::MathModule> (*this);
    auto wecolor = std::make_unique<Modules::ColorModule> (*this);
    auto wevector = std::make_unique<Modules::VectorModule> (*this);

    this->m_modules.emplace (wemath->getName (), std::move (wemath));
    this->m_modules.emplace (wecolor->getName (), std::move (wecolor));
    this->m_modules.emplace (wevector->getName (), std::move (wevector));

    JS_SetModuleLoaderFunc (this->m_runtime, nullptr, scriptengine_module_loader, this);
    // persistent storage must exist before the builtins' fallback shim runs
    JS_DefinePropertyValueStr (
	this->m_context, this->m_globalThis, "localStorage", this->m_localStorageObject->getInstance (),
	JS_PROP_ENUMERABLE
    );
    // setup scene objects and other things
    this->installBuiltins ();
    // add engine to the global
    JS_DefinePropertyValueStr (
	this->m_context, this->m_globalThis, "engine", this->m_engineObject->getInstance (), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_context, this->m_globalThis, "input", this->m_inputObject->getInstance (), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_context, this->m_globalThis, "thisScene", this->m_sceneObject->getInstance (), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_context, this->m_globalThis, "console", this->m_consoleObject->getInstance (), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_context, this->m_globalThis, "shared", JS_NewObject (this->m_context), JS_PROP_ENUMERABLE
    );
}

ScriptEngine::~ScriptEngine () {
    this->m_unregisterMediaUpdateCallback ();
    this->m_unregisterAlbumArtUpdateCallback ();

    for (const auto& module : this->m_scriptModules | std::views::values) {
	JS_FreeValue (this->m_context, module.module);
	JS_FreeValue (this->m_context, module.thisLayer);
    }

    JS_FreeValue (this->m_context, this->m_globalThis);

    // These wrappers own JSValues and must release them while the context is
    // still alive.
    this->m_consoleObject.reset ();
    this->m_engineObject.reset ();
    this->m_inputObject.reset ();
    this->m_sceneObject.reset ();
    this->m_scriptPropertiesObject.reset ();
    this->m_localStorageObject.reset ();
    this->m_modules.clear ();
    this->m_scriptModules.clear ();

    if (this->m_context) {
	JS_FreeContext (this->m_context);
    }
    if (this->m_runtime) {
	JS_FreeRuntime (this->m_runtime);
    }

    // JS_FreeRuntime runs the vector finalizers, which call back into
    // VectorAdapter::free(). Keep the adapters alive until the runtime is
    // gone, then mark the context as invalid so their destructors do not
    // touch freed JSValues.
    this->m_context = nullptr;
    this->m_runtime = nullptr;

    this->m_adapters.vec4.reset ();
    this->m_adapters.vec3.reset ();
    this->m_adapters.vec2.reset ();
    this->m_adapters.object.reset ();
}

/// Helper to check for and log JS exceptions
static void logJSValue (JSContext* ctx, const char* context, JSValue value) {
    // JS_GetException() returns this sentinel when a binding returned
    // JS_EXCEPTION without actually throwing. Report that instead of
    // stringifying the internal value as "[uninitialized]".
    if (JS_IsUninitialized (value)) {
	sLog.error ("ScriptEngine [", context, "]: exception thrown without a value");
	return;
    }

    if (JS_IsNull (value) || JS_IsUndefined (value)) {
	return;
    }

    const char* str = JS_ToCString (ctx, value);

    if (str == nullptr) {
	// stringification can throw (for example when toString() throws); consume
	// the secondary exception so it cannot leak into later script calls
	JS_FreeValue (ctx, JS_GetException (ctx));
	sLog.error ("ScriptEngine [", context, "]: exception could not be converted to a string");
	return;
    }

    sLog.error ("ScriptEngine [", context, "]: ", str);
    JS_FreeCString (ctx, str);

    if (JS_IsObject (value)) {
	JSValue stack = JS_GetPropertyStr (ctx, value, "stack");

	if (JS_IsException (stack)) {
	    JS_FreeValue (ctx, JS_GetException (ctx));
	} else if (!JS_IsUndefined (stack) && !JS_IsNull (stack)) {
	    const char* stackStr = JS_ToCString (ctx, stack);

	    if (stackStr != nullptr) {
		sLog.error ("ScriptEngine [", context, "] stack: ", stackStr);
		JS_FreeCString (ctx, stackStr);
	    } else {
		JS_FreeValue (ctx, JS_GetException (ctx));
	    }
	}

	JS_FreeValue (ctx, stack);
    }
}

static void logJSException (JSContext* ctx, const char* context) {
    JSValue exc = JS_GetException (ctx);
    logJSValue (ctx, context, exc);
    JS_FreeValue (ctx, exc);
}

void ScriptEngine::installBuiltins () {
    if (this->m_builtinsInstalled || !this->m_context) {
	return;
    }

    JSValue result = JS_Eval (
	this->m_context, SCENE_SCRIPT_BUILTINS, strlen (SCENE_SCRIPT_BUILTINS), "<scene-script-builtins>",
	JS_EVAL_TYPE_GLOBAL
    );
    if (JS_IsException (result)) {
	logJSException (this->m_context, "installBuiltins");
    }
    JS_FreeValue (this->m_context, result);
    this->m_builtinsInstalled = true;
}

// ---------------------------------------------------------------------------
// Layer-script API (Phase 2)
// ---------------------------------------------------------------------------

void ScriptEngine::ensureLayerRegistry () {
    if (this->m_layerRegistryReady || !this->m_context) {
	return;
    }
    JSContext* ctx = this->m_context;
    JSValue globalObj = JS_GetGlobalObject (ctx);
    JS_SetPropertyStr (ctx, globalObj, "__textLayers", JS_NewObject (ctx));
    JS_FreeValue (ctx, globalObj);
    this->m_layerRegistryReady = true;
}

ScriptLayerHandle ScriptEngine::createLayerScript (
    const std::string& scriptSource, std::map<std::string, UserSettingUniquePtr>& initialScriptProps,
    const std::string& initialText
) {
    if (!this->m_context) {
	sLog.error ("ScriptEngine: No JS context available");
	return kInvalidLayerHandle;
    }

    this->ensureLayerRegistry ();

    JSContext* ctx = this->m_context;
    JSValue globalObj = JS_GetGlobalObject (ctx);

    // Seed initial scriptProperties and text as temporary globals the IIFE reads.
    JSValue seedProps = JS_NewObject (ctx);

    for (auto& [name, dynVal] : initialScriptProps) {
	JS_SetPropertyStr (ctx, seedProps, name.c_str (), this->dynamicToJs (*dynVal->value));
    }

    JS_SetPropertyStr (ctx, globalObj, "__layerSeedProps", seedProps);
    JS_SetPropertyStr (ctx, globalObj, "__layerSeedText", JS_NewString (ctx, initialText.c_str ()));

    const ScriptLayerHandle id = this->m_nextLayerId++;

    // Same stripping logic as evaluate(): WE scripts come as ES6 modules but
    // QuickJS is easier to drive as plain script evaluation.
    std::string body = scriptSource;
    size_t pos;
    while ((pos = body.find ("'use strict';")) != std::string::npos) {
	body.erase (pos, 13);
    }
    while ((pos = body.find ("\"use strict\";")) != std::string::npos) {
	body.erase (pos, 13);
    }
    while ((pos = body.find ("export ")) != std::string::npos) {
	body.erase (pos, 7);
    }

    // The IIFE gives every layer its own closure for top-level vars and
    // functions, so two layers that both define `function update()` or a
    // top-level `var scriptProperties` don't clobber each other. Lifecycle
    // hooks are captured into globalThis.__textLayers[id] so tick/destroy can
    // reach them later. `typeof init === 'function'` is safe even when
    // `init` was never declared — bare-identifier `typeof` never throws.
    std::ostringstream wrapper;
    wrapper
	<< "(function() {\n"
	<< "  var __id = " << id << ";\n"
	<< "  var __props = Object.assign({}, globalThis.__layerSeedProps || {});\n"
	<< "  var thisLayer = { text: String(globalThis.__layerSeedText || '') };\n"
	<< "  var thisScene = {\n"
	<< "    get time()        { var c = globalThis.__sceneCtx; return c ? c.time : 0; },\n"
	<< "    get currentTime() { var c = globalThis.__sceneCtx; return c ? c.time : 0; },\n"
	<< "    get dt()          { var c = globalThis.__sceneCtx; return c ? c.dt   : 0; },\n"
	<< "    get fps()         { var c = globalThis.__sceneCtx; return c ? c.fps  : 60; },\n"
	<< "  };\n"
	// Minimal WE `engine` shim. Real Wallpaper Engine exposes a broad API
	// (media events, audio buffer, user input); we provide just enough for
	// the common built-in text scripts to run without ReferenceError.
	// `frametime` is the per-frame delta in seconds (what InsertFPS reads).
	// Scene-wide values and registerAsset come from the real IEngine object.
	<< "  var engine = {\n"
	<< "    get frametime() { var c = globalThis.__sceneCtx; return c ? c.dt : 0; },\n"
	<< "    get time()      { var c = globalThis.__sceneCtx; return c ? c.time : 0; },\n"
	<< "    get userProperties()   { return globalThis.engine ? globalThis.engine.userProperties : {}; },\n"
	<< "    get canvasSize()       { return globalThis.engine ? globalThis.engine.canvasSize : { x: 0, y: 0 }; },\n"
	<< "    get screenResolution() { return globalThis.engine ? globalThis.engine.screenResolution : { x: 0, y: 0 "
	   "}; },\n"
	<< "    registerAsset: function(file) {\n"
	<< "      return globalThis.engine ? globalThis.engine.registerAsset(file) : String(file);\n"
	<< "    },\n"
	<< "  };\n"
	<< "  function createScriptProperties() {\n"
	<< "    var builder = {\n"
	<< "      addSlider:   function(o){ if (!(o.name in __props)) __props[o.name] = o.value; return builder; },\n"
	<< "      addCheckbox: function(o){ if (!(o.name in __props)) __props[o.name] = o.value; return builder; },\n"
	<< "      addCombo:    function(o){ if (!(o.name in __props)) __props[o.name] = o.value; return builder; },\n"
	<< "      addColor:    function(o){ if (!(o.name in __props)) __props[o.name] = o.value; return builder; },\n"
	<< "      addText:     function(o){ if (!(o.name in __props)) __props[o.name] = o.value; return builder; },\n"
	<< "      finish:      function(){ return __props; }\n"
	<< "    };\n"
	<< "    return builder;\n"
	<< "  }\n"
	<< body
	<< "\n"
	// `_tick` wraps the user's `update()` so both WE text conventions work:
	//   A) `export function update() { thisLayer.text = …; }` (mutates in place)
	//   B) `export function update(value) { …; return value; }` (returns new text)
	// We pass the current text in, and if the return value is a string we
	// adopt it as the new `thisLayer.text`. Non-string / undefined return
	// leaves `thisLayer.text` as whatever the function assigned itself.
	<< "  globalThis.__textLayers[__id] = {\n"
	<< "    thisLayer: thisLayer,\n"
	<< "    thisScene: thisScene,\n"
	<< "    _init:    (typeof init    === 'function') ? init    : null,\n"
	<< "    _destroy: (typeof destroy === 'function') ? destroy : null,\n"
	<< "    _tick:    (typeof update  === 'function')\n"
	<< "              ? function() {\n"
	<< "                  var r = update(thisLayer.text);\n"
	<< "                  if (typeof r === 'string') thisLayer.text = r;\n"
	<< "                }\n"
	<< "              : null,\n"
	<< "    _scriptProperties: (typeof scriptProperties !== 'undefined') ? scriptProperties : __props\n"
	<< "  };\n"
	<< "})();\n";

    const std::string evalScript = wrapper.str ();
    JSValue result = JS_Eval (ctx, evalScript.c_str (), evalScript.size (), "<layer-script>", JS_EVAL_TYPE_GLOBAL);

    // Unset seeds so they don't leak into the next createLayerScript call.
    JS_SetPropertyStr (ctx, globalObj, "__layerSeedProps", JS_UNDEFINED);
    JS_SetPropertyStr (ctx, globalObj, "__layerSeedText", JS_UNDEFINED);
    JS_FreeValue (ctx, globalObj);

    if (JS_IsException (result)) {
	logJSException (ctx, "createLayerScript");
	JS_FreeValue (ctx, result);
	return kInvalidLayerHandle;
    }
    JS_FreeValue (ctx, result);

    this->m_layerInitialized[id] = false;
    return id;
}

void ScriptEngine::tickLayer (ScriptLayerHandle handle, double time, double deltaTime, double fps) {
    if (!this->m_context || handle == kInvalidLayerHandle) {
	return;
    }
    JSContext* ctx = this->m_context;
    JSValue globalObj = JS_GetGlobalObject (ctx);

    JSValue sceneCtx = JS_NewObject (ctx);
    JS_SetPropertyStr (ctx, sceneCtx, "time", JS_NewFloat64 (ctx, time));
    JS_SetPropertyStr (ctx, sceneCtx, "dt", JS_NewFloat64 (ctx, deltaTime));
    JS_SetPropertyStr (ctx, sceneCtx, "fps", JS_NewFloat64 (ctx, fps));
    JS_SetPropertyStr (ctx, globalObj, "__sceneCtx", sceneCtx);

    JSValue layers = JS_GetPropertyStr (ctx, globalObj, "__textLayers");
    JSValue layerObj = JS_GetPropertyUint32 (ctx, layers, static_cast<uint32_t> (handle));
    JS_FreeValue (ctx, layers);

    if (JS_IsUndefined (layerObj) || JS_IsNull (layerObj)) {
	JS_FreeValue (ctx, layerObj);
	JS_FreeValue (ctx, globalObj);
	return;
    }

    auto callHook = [&] (const char* prop, const char* tag) {
	JSValue fn = JS_GetPropertyStr (ctx, layerObj, prop);
	if (JS_IsFunction (ctx, fn)) {
	    JSValue ret = JS_Call (ctx, fn, layerObj, 0, nullptr);
	    if (JS_IsException (ret)) {
		logJSException (ctx, tag);
	    }
	    JS_FreeValue (ctx, ret);
	}
	JS_FreeValue (ctx, fn);
    };

    auto it = this->m_layerInitialized.find (handle);
    if (it != this->m_layerInitialized.end () && !it->second) {
	callHook ("_init", "layer.init");
	it->second = true;
    }
    callHook ("_tick", "layer.update");

    JS_FreeValue (ctx, layerObj);
    JS_FreeValue (ctx, globalObj);
}

std::string ScriptEngine::layerText (ScriptLayerHandle handle) {
    if (!this->m_context || handle == kInvalidLayerHandle) {
	return {};
    }
    JSContext* ctx = this->m_context;
    JSValue globalObj = JS_GetGlobalObject (ctx);
    JSValue layers = JS_GetPropertyStr (ctx, globalObj, "__textLayers");
    JSValue layerObj = JS_GetPropertyUint32 (ctx, layers, static_cast<uint32_t> (handle));

    std::string result;
    if (!JS_IsUndefined (layerObj) && !JS_IsNull (layerObj)) {
	JSValue thisLayer = JS_GetPropertyStr (ctx, layerObj, "thisLayer");
	JSValue textVal = JS_GetPropertyStr (ctx, thisLayer, "text");
	if (!JS_IsUndefined (textVal) && !JS_IsNull (textVal)) {
	    const char* cstr = JS_ToCString (ctx, textVal);
	    if (cstr) {
		result.assign (cstr);
		JS_FreeCString (ctx, cstr);
	    }
	}
	JS_FreeValue (ctx, textVal);
	JS_FreeValue (ctx, thisLayer);
    }

    JS_FreeValue (ctx, layerObj);
    JS_FreeValue (ctx, layers);
    JS_FreeValue (ctx, globalObj);
    return result;
}

void ScriptEngine::destroyLayer (ScriptLayerHandle handle) {
    if (!this->m_context || handle == kInvalidLayerHandle) {
	return;
    }
    JSContext* ctx = this->m_context;
    JSValue globalObj = JS_GetGlobalObject (ctx);
    JSValue layers = JS_GetPropertyStr (ctx, globalObj, "__textLayers");
    JSValue layerObj = JS_GetPropertyUint32 (ctx, layers, static_cast<uint32_t> (handle));

    if (!JS_IsUndefined (layerObj) && !JS_IsNull (layerObj)) {
	JSValue fn = JS_GetPropertyStr (ctx, layerObj, "_destroy");
	if (JS_IsFunction (ctx, fn)) {
	    JSValue ret = JS_Call (ctx, fn, layerObj, 0, nullptr);
	    if (JS_IsException (ret)) {
		logJSException (ctx, "layer.destroy");
	    }
	    JS_FreeValue (ctx, ret);
	}
	JS_FreeValue (ctx, fn);
    }
    JS_FreeValue (ctx, layerObj);
    JS_FreeValue (ctx, layers);
    JS_FreeValue (ctx, globalObj);

    // Remove the entry from globalThis.__textLayers so GC can reclaim its closures.
    const std::string delScript = "delete globalThis.__textLayers[" + std::to_string (handle) + "];";
    JSValue delResult = JS_Eval (ctx, delScript.c_str (), delScript.size (), "<layer-destroy>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException (delResult)) {
	logJSException (ctx, "layer.destroy.delete");
    }
    JS_FreeValue (ctx, delResult);

    this->m_layerInitialized.erase (handle);
}

JSValue ScriptEngine::call (JSValue module, int argc, JSValue argv[], const char* name) {
    // check if there's an update method and run it
    JSValue function = JS_GetPropertyStr (this->m_context, module, name);
    ScopeGuard guard ([&] () { JS_FreeValue (this->m_context, function); });

    if (!JS_IsFunction (this->m_context, function)) {
	return JS_UNDEFINED;
    }

    // Last-resort guard: adapters may throw C++ exceptions from inside a JS
    // callback. Convert them to JS exceptions so one broken script cannot
    // terminate the process. Prefer throwing JS exceptions at the callback
    // boundary; unwinding through the QuickJS C interpreter is not fully safe.
    try {
	return JS_Call (this->m_context, function, module, argc, argv);
    } catch (const std::exception& e) {
	JS_ThrowInternalError (this->m_context, "%s failed: %s", name, e.what ());
	return JS_EXCEPTION;
    } catch (...) {
	JS_ThrowInternalError (this->m_context, "%s failed with an unknown exception", name);
	return JS_EXCEPTION;
    }
}

void ScriptEngine::activateModule (LoadedModule& module) {
    this->m_runningModule = &module;
    JS_SetPropertyStr (
	this->m_context, this->m_globalThis, "thisLayer", JS_DupValue (this->m_context, module.thisLayer)
    );
}

void ScriptEngine::disableModule (LoadedModule& module, const std::string& context) {
    if (module.disabled) {
	return;
    }

    module.disabled = true;
    sLog.error ("ScriptEngine: disabling script after ", context);
}

bool ScriptEngine::runInit (LoadedModule& module) {
    JSValue args[] = { this->dynamicToJs (module.value) };
    JSValue result = this->call (module.module, 1, args, "init");

    ScopeGuard guard ([this, args, result] () {
	JS_FreeValue (this->m_context, result);
	JS_FreeValue (this->m_context, args[0]);
    });

    if (JS_IsException (result) || JS_IsUninitialized (result)) {
	logJSException (this->m_context, ("queueScript.init " + module.key).c_str ());
	this->disableModule (module, "init failure: " + module.key);
	return false;
    }

    if (!JS_IsUndefined (result)) {
	if (!jsToDynamicValue (this->m_context, result, module.value)) {
	    sLog.error ("ScriptEngine: init returned an unsupported value for ", module.key);
	    this->disableModule (module, "init conversion failure: " + module.key);
	    return false;
	}
    }

    return true;
}

bool ScriptEngine::runUpdate (LoadedModule& module, const std::string& context, const std::string& failure) {
    JSValue args[] = { this->dynamicToJs (module.value) };
    JSValue result = this->call (module.module, 1, args, "update");

    ScopeGuard guard ([this, args, result] () {
	JS_FreeValue (this->m_context, result);
	JS_FreeValue (this->m_context, args[0]);
    });

    if (JS_IsException (result) || JS_IsUninitialized (result)) {
	logJSException (this->m_context, context.c_str ());
	this->disableModule (module, failure);
	return false;
    }

    // modules without an update export evaluate to undefined, keep the parsed value as is
    if (!JS_IsUndefined (result)) {
	if (!jsToDynamicValue (this->m_context, result, module.value)) {
	    sLog.error ("ScriptEngine: update returned an unsupported value for ", module.key);
	    this->disableModule (module, failure);
	    return false;
	}
    }

    return true;
}

bool ScriptEngine::initializeModule (LoadedModule& module) {
    if (module.disabled || module.initialized) {
	return !module.disabled;
    }

    this->activateModule (module);

    if (module.hasInit && !this->runInit (module)) {
	this->m_runningModule = nullptr;
	return false;
    }

    if (!this->runUpdate (module, "queueScript.update " + module.key, "initial update failure: " + module.key)) {
	this->m_runningModule = nullptr;
	return false;
    }

    module.initialized = true;
    this->m_runningModule = nullptr;
    return true;
}

void ScriptEngine::initialize () {
    this->m_sceneReady = true;

    std::vector<LoadedModule*> pending;

    for (auto& module : this->m_scriptModules | std::views::values) {
	if (!module.disabled && !module.initialized) {
	    pending.push_back (&module);
	}
    }

    std::ranges::sort (pending, {}, &LoadedModule::order);

    for (auto* module : pending) {
	this->initializeModule (*module);
    }

    this->dispatchApplyUserProperties ();
    this->m_runningModule = nullptr;
}

void ScriptEngine::dispatchApplyUserProperties () {
    if (!this->m_context) {
	return;
    }

    JSContext* ctx = this->m_context;
    JSValue properties = this->userPropertiesToJs ();

    // A property handler may destroy layers, so collect the targets before
    // calling into scripts instead of iterating the module map directly.
    std::vector<LoadedModule*> pending;

    for (auto& module : this->m_scriptModules | std::views::values) {
	if (!module.disabled && module.initialized) {
	    pending.push_back (&module);
	}
    }

    for (auto* module : pending) {
	JSValue function = JS_GetPropertyStr (ctx, module->module, "applyUserProperties");

	if (JS_IsFunction (ctx, function)) {
	    this->activateModule (*module);

	    JSValue result = this->call (module->module, 1, &properties, "applyUserProperties");

	    if (JS_IsException (result)) {
		logJSException (ctx, ("queueScript.applyUserProperties " + module->key).c_str ());
	    }

	    JS_FreeValue (ctx, result);
	    this->m_runningModule = nullptr;
	}

	JS_FreeValue (ctx, function);
    }

    JS_FreeValue (ctx, properties);
}

bool ScriptEngine::hasCursorHandler (ScriptableObject& object) const {
    for (const auto& module : this->m_scriptModules | std::views::values) {
	if (!module.disabled && module.object == &object && module.cursorHandlerMask != 0) {
	    return true;
	}
    }

    return false;
}

void ScriptEngine::dispatchCursorEvent (
    ScriptableObject& object, const char* name, const glm::vec3& worldPosition, const glm::vec2& localPosition
) {
    const uint32_t handlerBit = cursorHandlerBit (name);

    for (auto& module : this->m_scriptModules | std::views::values) {
	if (module.disabled || module.object != &object || (module.cursorHandlerMask & handlerBit) == 0) {
	    continue;
	}

	JSValue event = JS_NewObject (this->m_context);
	DynamicValue worldValue { glm::vec3 (worldPosition) };
	JS_SetPropertyStr (
	    this->m_context, event, "worldPosition", this->m_adapters.vec3->instantiate (worldValue, true)
	);

	DynamicValue localValue { glm::vec3 (localPosition.x, localPosition.y, 0.0f) };
	JS_SetPropertyStr (
	    this->m_context, event, "localPosition", this->m_adapters.vec3->instantiate (localValue, true)
	);

	this->activateModule (module);
	JSValue result = this->call (module.module, 1, &event, name);

	if (JS_IsException (result)) {
	    const std::string context = std::string (name) + " " + module.key;
	    logJSException (this->m_context, context.c_str ());
	    this->disableModule (module, "cursor handler failure: " + context);
	}

	JS_FreeValue (this->m_context, result);
	JS_FreeValue (this->m_context, event);
    }
}

void ScriptEngine::removeScriptsForObject (ScriptableObject& object) {
    for (auto it = this->m_scriptModules.begin (); it != this->m_scriptModules.end ();) {
	if (it->second.object != &object) {
	    ++it;
	    continue;
	}

	if (this->m_runningModule == &it->second) {
	    this->m_runningModule = nullptr;
	}

	JS_FreeValue (this->m_context, it->second.module);
	JS_FreeValue (this->m_context, it->second.thisLayer);
	it = this->m_scriptModules.erase (it);
    }
}

void ScriptEngine::queueScript (const std::string& key, DynamicValue& currentValue, ScriptableObject& object) {
    const auto source = currentValue.getScriptSource ();

    if (!source.has_value ()) {
	return;
    }

    auto it = this->m_scriptModules.find (key);

    if (it != this->m_scriptModules.end ()) {
	return;
    }

    // quickjs-ng's JS_Eval returns a promise for modules instead of the namespace object, so
    // compile the module first to keep hold of its definition, evaluate it and then store the
    // namespace (which exposes the exported functions) as the module value
    JSValue compiled = JS_Eval (
	this->m_context, source->c_str (), source->size (), key.c_str (),
	JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY
    );

    if (JS_IsException (compiled)) {
	logJSException (this->m_context, ("queueScript.eval " + key).c_str ());
	return;
    }

    JSModuleDef* moduleDef = nullptr;

    if (JS_VALUE_GET_TAG (compiled) == JS_TAG_MODULE) {
	moduleDef = static_cast<JSModuleDef*> (JS_VALUE_GET_PTR (compiled));
    }

    // bind the running module before evaluation: createScriptProperties().finish() runs during
    // the module's top-level code and needs access to the value being scripted
    LoadedModule temporary { currentValue, key, JS_UNDEFINED, JS_UNDEFINED, &object };
    this->m_runningModule = &temporary;

    // running the compiled module links and executes it, the returned promise fulfills with
    // undefined for modules without top-level await (imports are resolved synchronously here)
    JSValue evaluation = JS_UNDEFINED;

    try {
	evaluation = JS_EvalFunction (this->m_context, compiled);
    } catch (const std::exception& e) {
	// last-resort guard for C++ exceptions escaping the module's top-level code
	JS_ThrowInternalError (this->m_context, "module %s evaluation failed: %s", key.c_str (), e.what ());
	evaluation = JS_EXCEPTION;
    } catch (...) {
	JS_ThrowInternalError (this->m_context, "module %s evaluation failed with an unknown exception", key.c_str ());
	evaluation = JS_EXCEPTION;
    }

    if (JS_IsException (evaluation)) {
	logJSException (this->m_context, ("queueScript.evaluate " + key).c_str ());
	this->m_runningModule = nullptr;
	return;
    }

    if (JS_IsPromise (evaluation)) {
	const JSPromiseStateEnum promiseState = JS_PromiseState (this->m_context, evaluation);

	if (promiseState == JS_PROMISE_REJECTED) {
	    JSValue reason = JS_PromiseResult (this->m_context, evaluation);

	    if (JS_IsUndefined (reason)) {
		sLog.error (
		    "ScriptEngine [queueScript.topLevel ", key, "]: module top-level code rejected with undefined"
		);
	    } else if (JS_IsNull (reason)) {
		sLog.error ("ScriptEngine [queueScript.topLevel ", key, "]: module top-level code rejected with null");
	    } else {
		logJSValue (this->m_context, ("queueScript.topLevel " + key).c_str (), reason);
	    }

	    JS_FreeValue (this->m_context, reason);
	    JS_FreeValue (this->m_context, evaluation);
	    this->m_runningModule = nullptr;
	    return;
	}

	if (promiseState == JS_PROMISE_PENDING) {
	    sLog.error ("ScriptEngine: module ", key, " uses unsupported top-level await; disabled");
	    JS_FreeValue (this->m_context, evaluation);
	    this->m_runningModule = nullptr;
	    return;
	}
    }

    JS_FreeValue (this->m_context, evaluation);

    JSValue module = JS_UNDEFINED;

    if (moduleDef != nullptr) {
	module = JS_GetModuleNamespace (this->m_context, moduleDef);

	if (JS_IsException (module)) {
	    logJSException (this->m_context, ("queueScript.namespace " + key).c_str ());
	    this->m_runningModule = nullptr;
	    return;
	}
    } else {
	sLog.error ("ScriptEngine: module ", key, " did not compile to a module definition");
    }

    auto inserted = this->m_scriptModules.emplace (
	key,
	LoadedModule {
	    .value = currentValue,
	    .key = key,
	    .module = module,
	    .object = &object,
	}
    );

    if (!inserted.second) {
	JS_FreeValue (this->m_context, module);
	this->m_runningModule = nullptr;
	return;
    }

    inserted.first->second.cursorHandlerMask = readCursorHandlerMask (this->m_context, module);
    inserted.first->second.order = this->m_nextModuleOrder++;

    JSValue initFunction = JS_GetPropertyStr (this->m_context, module, "init");
    inserted.first->second.hasInit = JS_IsFunction (this->m_context, initFunction);
    JS_FreeValue (this->m_context, initFunction);

    JSValue thisLayer = this->m_adapters.object->instantiate (object);

    // expose texture animation control through thisLayer.getTextureAnimation()
    {
	JSValue anim = JS_NewObjectClass (this->m_context, this->m_textureAnimationClassId);
	JS_SetOpaque (anim, new TextureAnimationApi { .object = &object });

	JS_DefinePropertyValueStr (
	    this->m_context, anim, "setFrame",
	    JS_NewCFunction (this->m_context, texture_animation_set_frame, "setFrame", 1), JS_PROP_ENUMERABLE
	);
	JS_DefinePropertyValueStr (
	    this->m_context, anim, "setFramerate",
	    JS_NewCFunction (this->m_context, texture_animation_set_framerate, "setFramerate", 1), JS_PROP_ENUMERABLE
	);
	JS_DefinePropertyValueStr (
	    this->m_context, anim, "play", JS_NewCFunction (this->m_context, texture_animation_play, "play", 0),
	    JS_PROP_ENUMERABLE
	);
	JS_DefinePropertyValueStr (
	    this->m_context, anim, "pause", JS_NewCFunction (this->m_context, texture_animation_pause, "pause", 0),
	    JS_PROP_ENUMERABLE
	);
	JS_DefinePropertyValueStr (
	    this->m_context, anim, "stop", JS_NewCFunction (this->m_context, texture_animation_stop, "stop", 0),
	    JS_PROP_ENUMERABLE
	);

	JS_DefinePropertyValueStr (this->m_context, thisLayer, "__textureAnimation", anim, JS_PROP_ENUMERABLE);

	JSValue getter = JS_NewCFunction (
	    this->m_context,
	    [] (JSContext* ctx, JSValueConst this_val, int, JSValueConst*) -> JSValue {
		return JS_GetPropertyStr (ctx, this_val, "__textureAnimation");
	    },
	    "getTextureAnimation", 0
	);
	JS_DefinePropertyValueStr (this->m_context, thisLayer, "getTextureAnimation", getter, JS_PROP_ENUMERABLE);
    }

    inserted.first->second.thisLayer = JS_DupValue (this->m_context, thisLayer);
    JS_SetPropertyStr (this->m_context, this->m_globalThis, "thisLayer", thisLayer);

    if (this->m_sceneReady) {
	this->initializeModule (inserted.first->second);
    }

    this->m_runningModule = nullptr;
}

void ScriptEngine::tick () {
    // run intervals
    this->m_engineObject->tick ();

    // run any pending notifications

    // run all update methods
    for (auto& module : this->m_scriptModules | std::views::values) {
	if (module.disabled || !module.initialized) {
	    continue;
	}

	this->activateModule (module);
	this->runUpdate (module, "tick.update " + module.key, "update failure: " + module.key);
    }
}

void ScriptEngine::notifyMediaUpdate (const Media::MediaSource::MediaInfo& media) {
    JSContext* ctx = this->m_context;

    DynamicValue primaryColorValue (glm::vec3 (0.12f, 0.12f, 0.12f));
    DynamicValue secondaryColorValue (glm::vec3 (0.0f, 0.0f, 0.0f));
    DynamicValue tertiaryColorValue (glm::vec3 (0.25f, 0.25f, 0.25f));
    DynamicValue highContrastColorValue (glm::vec3 (1.0f, 1.0f, 1.0f));

    // TODO: PROCESS THESE COLORS INSTEAD OF HARDCODING THEM
    JSValue primaryColor = this->m_adapters.vec3->instantiate (primaryColorValue, true);
    JSValue secondaryColor = this->m_adapters.vec3->instantiate (secondaryColorValue, true);
    JSValue tertiaryColor = this->m_adapters.vec3->instantiate (tertiaryColorValue, true);
    JSValue highContrastColor = this->m_adapters.vec3->instantiate (highContrastColorValue, true);

    JSValue propertiesEvent = JS_NewObject (ctx);

    // set properties
    JS_SetPropertyStr (ctx, propertiesEvent, "title", JS_NewString (ctx, media.title.c_str ()));
    JS_SetPropertyStr (ctx, propertiesEvent, "artist", JS_NewString (ctx, media.artist.c_str ()));
    JS_SetPropertyStr (ctx, propertiesEvent, "albumTitle", JS_NewString (ctx, media.album.c_str ()));

    JSValue playbackEvent = JS_NewObject (ctx);

    JS_SetPropertyStr (ctx, playbackEvent, "state", JS_NewInt32 (ctx, media.playbackState));

    JSValue mediaTimelineEvent = JS_NewObject (ctx);

    JS_SetPropertyStr (ctx, mediaTimelineEvent, "position", JS_NewFloat64 (ctx, media.position));
    JS_SetPropertyStr (ctx, mediaTimelineEvent, "duration", JS_NewFloat64 (ctx, media.duration));

    JSValue mediaThumbnailEvent = JS_NewObject (ctx);

    JS_SetPropertyStr (ctx, mediaThumbnailEvent, "hasThumbnail", JS_NewBool (ctx, media.url.has_value ()));
    JS_SetPropertyStr (ctx, mediaThumbnailEvent, "primaryColor", primaryColor);
    JS_SetPropertyStr (ctx, mediaThumbnailEvent, "secondaryColor", secondaryColor);
    JS_SetPropertyStr (ctx, mediaThumbnailEvent, "tertiaryColor", tertiaryColor);
    JS_SetPropertyStr (ctx, mediaThumbnailEvent, "highContrastColor", highContrastColor);

    JSValue propertiesArgs[] = { propertiesEvent };
    JSValue playbackArgs[] = { playbackEvent };
    JSValue mediaTimelineArgs[] = { mediaTimelineEvent };
    JSValue mediaThumbnailArgs[] = { mediaThumbnailEvent };

    for (auto& module : this->m_scriptModules | std::views::values) {
	if (module.disabled) {
	    continue;
	}

	bool failed = false;

	const auto dispatch = [&] (const char* handler, JSValue* args) {
	    JSValue result = this->call (module.module, 1, args, handler);

	    if (JS_IsException (result)) {
		const std::string context = std::string (handler) + " " + module.key;
		logJSException (ctx, context.c_str ());
		failed = true;
	    }

	    JS_FreeValue (ctx, result);
	};

	// call all methods
	dispatch ("mediaPropertiesChanged", propertiesArgs);
	dispatch ("mediaPlaybackChanged", playbackArgs);
	dispatch ("mediaTimelineChanged", mediaTimelineArgs);
	dispatch ("mediaThumbnailChanged", mediaThumbnailArgs);

	if (failed) {
	    this->disableModule (module, "media handler failure: " + module.key);
	}
    }

    // free all created objects as we don't keep a ref to them anymore
    JS_FreeValue (ctx, propertiesEvent);
    JS_FreeValue (ctx, playbackEvent);
    JS_FreeValue (ctx, mediaTimelineEvent);
    JS_FreeValue (ctx, mediaThumbnailEvent);
}