#include "EngineObject.h"
#include "ScriptEngine.h"
#include "WallpaperEngine/Audio/AudioContext.h"
#include "WallpaperEngine/Audio/Drivers/Recorders/PlaybackRecorder.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Render/Wallpapers/CScene.h"

#include <algorithm>
#include <ranges>

using namespace WallpaperEngine::Scripting;

extern float g_Time;
extern float g_TimeLast;
extern float g_Daytime;

static uint32_t EngineInstanceId = 0;
std::map<uint32_t, EngineObject&> engineInstances;

JSValue engine_set_value (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { return JS_EXCEPTION; }

JSValue engine_open_user_shortcut (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return JS_UNDEFINED;
}

JSValue engine_register_asset (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc != 1 || !JS_IsString (argv[0])) {
	return JS_UNDEFINED;
    }

    return JS_DupValue (ctx, argv[0]);
}

JSValue engine_get_frametime (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return JS_NewFloat64 (ctx, g_Time - g_TimeLast);
}

JSValue engine_get_runtime (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return JS_NewFloat64 (ctx, g_Time);
}

JSValue engine_get_daytime (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return JS_NewFloat64 (ctx, g_Daytime);
}

namespace {
/**
 * Creates a Float32Array whose backing store can be updated in-place every frame.
 * `data` points into the ArrayBuffer and stays valid while the cached JSValue lives.
 */
JSValue createFloat32Array (JSContext* ctx, int length, float** data) {
    JSValue lengthValue = JS_NewInt32 (ctx, length);
    JSValue array = JS_NewTypedArray (ctx, 1, &lengthValue, JS_TYPED_ARRAY_FLOAT32);
    JS_FreeValue (ctx, lengthValue);

    *data = nullptr;

    if (JS_IsException (array)) {
	return array;
    }

    size_t byteOffset = 0;
    size_t byteLength = 0;
    size_t bytesPerElement = 0;
    JSValue buffer = JS_GetTypedArrayBuffer (ctx, array, &byteOffset, &byteLength, &bytesPerElement);

    if (JS_IsException (buffer)) {
	JS_FreeValue (ctx, array);
	return buffer;
    }

    size_t bufferSize = 0;
    auto* base = JS_GetArrayBuffer (ctx, &bufferSize, buffer);
    JS_FreeValue (ctx, buffer);

    if (base == nullptr || bytesPerElement != sizeof (float) || byteOffset + byteLength > bufferSize) {
	JS_FreeValue (ctx, array);
	return JS_EXCEPTION;
    }

    *data = reinterpret_cast<float*> (base + byteOffset);
    return array;
}
} // namespace

JSValue engine_register_audio_buffers (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JSClassID classId = 0;
    auto* engine = static_cast<EngineObject*> (JS_GetAnyOpaque (this_val, &classId));

    if (engine == nullptr) {
	return JS_ThrowInternalError (ctx, "registerAudioBuffers: no engine");
    }

    int32_t resolution = 32;

    if (argc >= 1 && JS_IsNumber (argv[0])) {
	JS_ToInt32 (ctx, &resolution, argv[0]);
    }

    if (resolution != 16 && resolution != 32 && resolution != 64) {
	resolution = 32;
    }

    return engine->getAudioBuffer (resolution);
}

JSValue engine_stop_interval (
    JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValueConst* func_data
) {
    if (argc != 1) {
	return JS_EXCEPTION;
    }

    const auto it = engineInstances.find (magic);

    if (it == engineInstances.end ()) {
	return JS_EXCEPTION;
    }

    int id = 0;

    JS_ToInt32 (ctx, &id, argv[0]);

    it->second.clearInterval (id);

    return JS_UNDEFINED;
}

JSValue engine_stop_timeout (
    JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValueConst* func_data
) {
    if (argc != 1) {
	return JS_EXCEPTION;
    }

    const auto it = engineInstances.find (magic);

    if (it == engineInstances.end ()) {
	return JS_EXCEPTION;
    }

    int id = 0;

    JS_ToInt32 (ctx, &id, argv[0]);

    it->second.clearTimeout (id);

    return JS_UNDEFINED;
}

JSValue engine_set_interval (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    if (argc < 1) {
	return JS_EXCEPTION;
    }

    int delay = 0;

    if (argc > 1) {
	JS_ToInt32 (ctx, &delay, argv[1]);
    }

    JSValue function = argv[0];

    if (!JS_IsFunction (ctx, function)) {
	return JS_EXCEPTION;
    }

    const auto it = engineInstances.find (magic);

    if (it == engineInstances.end ()) {
	return JS_EXCEPTION;
    }

    int id = it->second.reserveNextIntervalId (function, delay);

    JSValue args[] = { JS_NewInt32 (ctx, id) };

    return JS_NewCFunctionData (ctx, engine_stop_interval, 2, magic, 1, args);
}

JSValue engine_set_timeout (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    if (argc < 1) {
	return JS_EXCEPTION;
    }

    int delay = 0;

    if (argc > 1) {
	JS_ToInt32 (ctx, &delay, argv[1]);
    }

    JSValue function = argv[0];

    if (!JS_IsFunction (ctx, function)) {
	return JS_EXCEPTION;
    }

    const auto it = engineInstances.find (magic);

    if (it == engineInstances.end ()) {
	return JS_EXCEPTION;
    }

    int id = it->second.reserveNextTimeoutId (function, delay);

    JSValue args[] = { JS_NewInt32 (ctx, id) };

    return JS_NewCFunctionData (ctx, engine_stop_timeout, 2, magic, 1, args);
}

EngineObject::EngineObject (ScriptEngine& engine, Render::Wallpapers::CScene& scene) :
    m_scene (scene), m_engine (engine), m_instanceId (++EngineInstanceId), m_classId (0) {
    this->m_definition = { .class_name = "IEngine" };
    JS_NewClassID (this->m_engine.getRuntime (), &this->m_classId);
    JS_NewClass (this->m_engine.getRuntime (), this->m_classId, &this->m_definition);
    this->m_instance = JS_NewObjectClass (this->m_engine.getContext (), this->m_classId);

    JS_DupValue (this->m_engine.getContext (), this->m_instance);

    // set properties
    JS_SetOpaque (this->m_instance, this);
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "frametime"),
	JS_NewCFunction (this->m_engine.getContext (), engine_get_frametime, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), engine_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "runtime"),
	JS_NewCFunction (this->m_engine.getContext (), engine_get_runtime, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), engine_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "timeOfDay"),
	JS_NewCFunction (this->m_engine.getContext (), engine_get_daytime, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), engine_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "AUDIO_RESOLUTION_16",
	JS_NewInt32 (this->m_engine.getContext (), 16), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "AUDIO_RESOLUTION_32",
	JS_NewInt32 (this->m_engine.getContext (), 32), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "AUDIO_RESOLUTION_64",
	JS_NewInt32 (this->m_engine.getContext (), 64), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "setInterval",
	JS_NewCFunctionMagic (
	    this->m_engine.getContext (), engine_set_interval, "setInterval", 2, JS_CFUNC_generic, this->m_instanceId
	),
	JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "setTimeout",
	JS_NewCFunctionMagic (
	    this->m_engine.getContext (), engine_set_timeout, "setTimeout", 2, JS_CFUNC_generic, this->m_instanceId
	),
	JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "openUserShortcut",
	JS_NewCFunction (this->m_engine.getContext (), engine_open_user_shortcut, "openUserShortcut", 0),
	JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "registerAsset",
	JS_NewCFunction (this->m_engine.getContext (), engine_register_asset, "registerAsset", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "registerAudioBuffers",
	JS_NewCFunction (this->m_engine.getContext (), engine_register_audio_buffers, "registerAudioBuffers", 1),
	JS_PROP_ENUMERABLE
    );
    // TODO: ADD THE REST OF THE DEFINITION!
}

JSValue EngineObject::getAudioBuffer (int resolution) {
    const auto it = this->m_audioBuffers.find (resolution);

    if (it != this->m_audioBuffers.end ()) {
	return JS_DupValue (this->m_engine.getContext (), it->second.object);
    }

    JSContext* ctx = this->m_engine.getContext ();
    AudioBufferState state { .resolution = resolution };

    const auto cleanup = [ctx, &state] () {
	JS_FreeValue (ctx, state.left);
	JS_FreeValue (ctx, state.right);
	JS_FreeValue (ctx, state.average);
    };

    state.left = createFloat32Array (ctx, resolution, &state.leftData);

    if (JS_IsException (state.left)) {
	cleanup ();
	return JS_EXCEPTION;
    }

    state.right = createFloat32Array (ctx, resolution, &state.rightData);

    if (JS_IsException (state.right)) {
	cleanup ();
	return JS_EXCEPTION;
    }

    state.average = createFloat32Array (ctx, resolution, &state.averageData);

    if (JS_IsException (state.average)) {
	cleanup ();
	return JS_EXCEPTION;
    }

    state.object = JS_NewObject (ctx);

    if (JS_IsException (state.object)) {
	cleanup ();
	return JS_EXCEPTION;
    }

    // The three arrays are live Float32Arrays updated from the recorder every tick.
    // PulseAudio capture is currently mono, so left/right mirror the same spectrum
    // until the recorder exposes separate channels.
    JS_SetPropertyStr (ctx, state.object, "left", JS_DupValue (ctx, state.left));
    JS_SetPropertyStr (ctx, state.object, "right", JS_DupValue (ctx, state.right));
    JS_SetPropertyStr (ctx, state.object, "average", JS_DupValue (ctx, state.average));

    const auto inserted = this->m_audioBuffers.emplace (resolution, std::move (state));

    return JS_DupValue (ctx, inserted.first->second.object);
}

void EngineObject::updateAudioBuffers () {
    if (this->m_audioBuffers.empty ()) {
	return;
    }

    const auto& recorder = this->m_scene.getAudioContext ().getRecorder ();

    for (auto& [resolution, state] : this->m_audioBuffers) {
	const float* data = resolution == 16 ? recorder.audio16
	    : resolution == 32               ? recorder.audio32
					     : recorder.audio64;

	for (int32_t i = 0; i < resolution; ++i) {
	    // SceneScript explicitly allows values above 1.0; only clamp the
	    // negative excursions so audio-reactive properties stay sane.
	    const float value = std::max (data[i], 0.0f);

	    state.leftData[i] = value;
	    state.rightData[i] = value;
	    state.averageData[i] = value;
	}
    }
}

EngineObject::~EngineObject () {
    // clear all the timeouts and intervals
    for (const auto& [id, timeout] : this->m_timeouts) {
	JS_FreeValue (this->m_engine.getContext (), timeout.callback);
    }
    for (const auto& [id, interval] : this->m_intervals) {
	JS_FreeValue (this->m_engine.getContext (), interval.callback);
    }

    engineInstances.erase (this->m_instanceId);
    this->m_intervals.clear ();
    this->m_timeouts.clear ();

    for (auto& [resolution, state] : this->m_audioBuffers) {
	JS_FreeValue (this->m_engine.getContext (), state.object);
	JS_FreeValue (this->m_engine.getContext (), state.left);
	JS_FreeValue (this->m_engine.getContext (), state.right);
	JS_FreeValue (this->m_engine.getContext (), state.average);
    }
    this->m_audioBuffers.clear ();

    JS_FreeValue (this->m_engine.getContext (), this->m_instance);
}

uint32_t EngineObject::reserveNextTimeoutId (JSValue function, uint64_t duration) {
    const auto id = ++this->m_nextTimeoutId;

    this->m_timeouts[id] = Timeout { .callback = function,
				     .duration = std::chrono::milliseconds (duration),
				     .next = std::chrono::steady_clock::now () + std::chrono::milliseconds (duration) };

    return id;
}

uint32_t EngineObject::reserveNextIntervalId (JSValue function, uint64_t duration) {
    const auto id = ++this->m_nextIntervalId;

    this->m_intervals[id]
	= Timeout { .callback = function,
		    .duration = std::chrono::milliseconds (duration),
		    .next = std::chrono::steady_clock::now () + std::chrono::milliseconds (duration) };

    return id;
}

void EngineObject::clearInterval (uint32_t id) {
    const auto it = this->m_intervals.find (id);

    if (it == this->m_intervals.end ()) {
	return;
    }

    JS_FreeValue (this->getEngine ().getContext (), it->second.callback);

    this->m_intervals.erase (id);
}

void EngineObject::clearTimeout (uint32_t id) {
    const auto it = this->m_timeouts.find (id);

    if (it == this->m_timeouts.end ()) {
	return;
    }

    JS_FreeValue (this->getEngine ().getContext (), it->second.callback);

    this->m_timeouts.erase (id);
}

void EngineObject::tick () {
    this->updateAudioBuffers ();

    const auto now = std::chrono::steady_clock::now ();

    // check any interval and run them if needed
    for (auto& timeout : this->m_intervals | std::views::values) {
	if (timeout.next > now) {
	    continue;
	}

	timeout.next = now + timeout.duration;

	JS_Call (this->m_engine.getContext (), timeout.callback, JS_NULL, 0, nullptr);
    }

    std::vector<uint32_t> removeTimeouts;

    // check any timeout and run them if needed
    for (auto& [id, timeout] : this->m_timeouts) {
	if (timeout.next > now) {
	    continue;
	}

	JS_Call (this->m_engine.getContext (), timeout.callback, JS_NULL, 0, nullptr);

	JS_FreeValue (this->m_engine.getContext (), timeout.callback);

	removeTimeouts.push_back (id);
    }

    for (auto id : removeTimeouts) {
	this->m_timeouts.erase (id);
    }
}