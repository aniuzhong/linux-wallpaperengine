#include "SceneObject.h"

#include "Adapters/ScriptableObjectAdapter.h"
#include "ScriptEngine.h"
#include "ScriptableObject.h"
#include "WallpaperEngine/Data/Utils/ScopeGuard.h"
#include "WallpaperEngine/Render/Wallpapers/CScene.h"

#include <cmath>
#include <string>

#include <glm/geometric.hpp>

using namespace WallpaperEngine::Scripting;

SceneObject* get_opaque (JSValueConst this_val) {
    JSClassID classId;
    return static_cast<SceneObject*> (JS_GetAnyOpaque (this_val, &classId));
}

JSValue get_bloom (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewBool (ctx, container->getScene ().getScene ().camera.bloom.enabled->value->getBool ());
}

JSValue get_bloomstrength (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewInt32 (ctx, container->getScene ().getScene ().camera.bloom.strength->value->getInt ());
}

JSValue get_bloomthreshold (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewInt32 (ctx, container->getScene ().getScene ().camera.bloom.threshold->value->getInt ());
}

JSValue get_clearenabled (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewBool (ctx, container->getScene ().getScene ().camera.bloom.enabled->value->getBool ());
}

JSValue get_clearcolor (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return container->getEngine ().getAdapters ().vec3->instantiate (
	*container->getScene ().getScene ().colors.clear->value
    );
}

JSValue get_ambientcolor (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return container->getEngine ().getAdapters ().vec3->instantiate (
	*container->getScene ().getScene ().colors.ambient->value
    );
}

JSValue get_skylightcolor (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return container->getEngine ().getAdapters ().vec3->instantiate (
	*container->getScene ().getScene ().colors.ambient->value
    );
}

JSValue get_fov (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewFloat64 (ctx, container->getScene ().getScene ().camera.projection.fov->value->getFloat ());
}

JSValue set_fov (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc != 1 || !JS_IsNumber (argv[0])) {
	return JS_ThrowTypeError (ctx, "fov expects a number");
    }

    double value = 0.0;

    if (JS_ToFloat64 (ctx, &value, argv[0]) < 0) {
	return JS_EXCEPTION;
    }

    if (!std::isfinite (value)) {
	return JS_ThrowRangeError (ctx, "fov expects a finite number");
    }

    auto* container = get_opaque (this_val);

    container->getScene ().getCamera ().setFov (static_cast<float> (value));

    return JS_UNDEFINED;
}

JSValue get_nearz (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewFloat64 (ctx, container->getScene ().getScene ().camera.projection.nearz->value->getFloat ());
}

JSValue get_farz (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewFloat64 (ctx, container->getScene ().getScene ().camera.projection.farz->value->getFloat ());
}

JSValue get_camerafade (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewBool (ctx, container->getScene ().getScene ().camera.fade->value->getBool ());
}

JSValue get_camerashake (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewBool (ctx, container->getScene ().getScene ().camera.shake.enabled->value->getBool ());
}

JSValue get_camerashakespeed (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewFloat64 (ctx, container->getScene ().getScene ().camera.shake.speed->value->getFloat ());
}

JSValue get_camerashakeamplitude (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewFloat64 (ctx, container->getScene ().getScene ().camera.shake.amplitude->value->getFloat ());
}

JSValue get_camerashakeroughness (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewFloat64 (ctx, container->getScene ().getScene ().camera.shake.roughness->value->getFloat ());
}

JSValue get_cameraparallax (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewBool (ctx, container->getScene ().getScene ().camera.parallax.enabled->value->getBool ());
}

JSValue get_cameraparallaxamount (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewFloat64 (ctx, container->getScene ().getScene ().camera.parallax.amount->value->getFloat ());
}

JSValue get_cameraparallaxdelay (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewFloat64 (ctx, container->getScene ().getScene ().camera.parallax.delay->value->getFloat ());
}

JSValue get_cameraparallaxmouseinfluence (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewFloat64 (ctx, container->getScene ().getScene ().camera.parallax.mouseInfluence->value->getFloat ());
}

JSValue get_layer (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc != 1) {
	return JS_EXCEPTION;
    }

    auto* container = get_opaque (this_val);

    JSValue layer = argv[0];

    if (JS_IsNumber (layer)) {
	int id = 0;

	JS_ToInt32 (ctx, &id, layer);

	auto* object = container->getScene ().getObject (id);

	if (object == nullptr) {
	    return JS_UNDEFINED;
	}

	if (!object->is<ScriptableObject> ()) {
	    return JS_UNDEFINED;
	}

	// TODO: REMOVE THIS CONST_CAST?
	return container->getEngine ().getAdapters ().object->instantiate (
	    const_cast<ScriptableObject&> (*object->as<ScriptableObject> ())
	);
    } else if (JS_IsString (layer)) {
	// find by name, this is harder
	const char* result = JS_ToCString (ctx, layer);

	if (result == nullptr) {
	    return JS_UNDEFINED;
	}

	ScopeGuard guard ([=] { JS_FreeCString (ctx, result); });

	if (auto* object = container->getScene ().getObjectByName (result); object != nullptr) {
	    if (object->is<ScriptableObject> ()) {
		return container->getEngine ().getAdapters ().object->instantiate (*object->as<ScriptableObject> ());
	    }
	}

	// A missing optional layer is not a script error. WE scripts commonly do
	// `let layer = thisScene.getLayer("name"); if (layer) ...`.
	return JS_UNDEFINED;
    }

    return JS_EXCEPTION;
}

JSValue get_layer_index (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc != 1) {
	return JS_EXCEPTION;
    }

    auto* container = get_opaque (this_val);
    const auto& objects = container->getScene ().getObjectsByRenderOrder ();
    int id = -1;
    std::string name;
    bool hasName = false;

    if (JS_IsNumber (argv[0])) {
	JS_ToInt32 (ctx, &id, argv[0]);
    } else if (JS_IsString (argv[0])) {
	const char* value = JS_ToCString (ctx, argv[0]);

	if (value == nullptr) {
	    return JS_EXCEPTION;
	}

	name = value;
	hasName = true;
	JS_FreeCString (ctx, value);
    } else if (JS_IsObject (argv[0])) {
	auto* object = WallpaperEngine::Scripting::Adapters::ScriptableObjectAdapter::getObject (argv[0]);

	if (object != nullptr) {
	    id = object->getId ();
	}
    } else {
	return JS_EXCEPTION;
    }

    for (size_t i = 0; i < objects.size (); ++i) {
	if (objects[i] == nullptr) {
	    continue;
	}

	if ((id >= 0 && objects[i]->getId () == id) || (hasName && objects[i]->getObject ().name == name)) {
	    return JS_NewInt32 (ctx, static_cast<int32_t> (i));
	}
    }

    return JS_NewInt32 (ctx, -1);
}

WallpaperEngine::Render::CObject*
resolve_layer (WallpaperEngine::Render::Wallpapers::CScene& scene, JSContext* ctx, JSValueConst value) {
    if (JS_IsNumber (value)) {
	int id = 0;

	JS_ToInt32 (ctx, &id, value);

	return scene.getObject (id);
    }

    if (JS_IsString (value)) {
	const char* name = JS_ToCString (ctx, value);

	if (name == nullptr) {
	    return nullptr;
	}

	auto* object = scene.getObjectByName (name);

	JS_FreeCString (ctx, name);

	return object;
    }

    if (JS_IsObject (value)) {
	return WallpaperEngine::Scripting::Adapters::ScriptableObjectAdapter::getObject (value);
    }

    return nullptr;
}

JSValue create_layer (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc != 1 || !JS_IsString (argv[0])) {
	return JS_EXCEPTION;
    }

    auto* container = get_opaque (this_val);
    const char* asset = JS_ToCString (ctx, argv[0]);

    if (asset == nullptr) {
	return JS_EXCEPTION;
    }

    auto* object = container->getScene ().createLayerFromAsset (asset);

    JS_FreeCString (ctx, asset);

    if (object == nullptr || !object->is<ScriptableObject> ()) {
	return JS_UNDEFINED;
    }

    return container->getEngine ().getAdapters ().object->instantiate (*object->as<ScriptableObject> ());
}

JSValue destroy_layer (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc != 1) {
	return JS_EXCEPTION;
    }

    auto* container = get_opaque (this_val);
    auto* object = resolve_layer (container->getScene (), ctx, argv[0]);

    return JS_NewBool (ctx, container->getScene ().destroyLayer (object));
}

JSValue sort_layer (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc != 2 || !JS_IsNumber (argv[1])) {
	return JS_EXCEPTION;
    }

    auto* container = get_opaque (this_val);
    auto* object = resolve_layer (container->getScene (), ctx, argv[0]);
    int index = 0;

    JS_ToInt32 (ctx, &index, argv[1]);

    if (object != nullptr) {
	container->getScene ().sortLayer (object, index);
    }

    return JS_UNDEFINED;
}

JSValue scene_set_value (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    // Scene-wide camera/shader settings are not writable yet; accept writes so
    // scripts that toggle them can keep running instead of throwing.
    return JS_UNDEFINED;
}

bool readCameraVec3 (JSContext* ctx, JSValueConst value, glm::vec3& output) {
    if (!JS_IsObject (value)) {
	return false;
    }

    JSValue x = JS_GetPropertyStr (ctx, value, "x");
    JSValue y = JS_GetPropertyStr (ctx, value, "y");
    JSValue z = JS_GetPropertyStr (ctx, value, "z");

    const bool valid = JS_IsNumber (x) && JS_IsNumber (y) && JS_IsNumber (z);

    if (valid) {
	double xValue = 0.0;
	double yValue = 0.0;
	double zValue = 0.0;

	JS_ToFloat64 (ctx, &xValue, x);
	JS_ToFloat64 (ctx, &yValue, y);
	JS_ToFloat64 (ctx, &zValue, z);
	output = { static_cast<float> (xValue), static_cast<float> (yValue), static_cast<float> (zValue) };
    }

    JS_FreeValue (ctx, x);
    JS_FreeValue (ctx, y);
    JS_FreeValue (ctx, z);

    return valid;
}

JSValue makeCameraVec3 (JSContext* ctx, SceneObject& container, const glm::vec3& value) {
    auto* adapter = container.getEngine ().getAdapters ().vec3.get ();
    JSValue result = adapter->instantiate ();

    JS_SetPropertyStr (ctx, result, "x", JS_NewFloat64 (ctx, value.x));
    JS_SetPropertyStr (ctx, result, "y", JS_NewFloat64 (ctx, value.y));
    JS_SetPropertyStr (ctx, result, "z", JS_NewFloat64 (ctx, value.z));

    return result;
}

JSValue get_camera_transforms (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);
    const auto& camera = container->getScene ().getCamera ();

    JSValue result = JS_NewObject (ctx);

    JS_SetPropertyStr (ctx, result, "eye", makeCameraVec3 (ctx, *container, camera.getEye ()));
    JS_SetPropertyStr (ctx, result, "center", makeCameraVec3 (ctx, *container, camera.getCenter ()));
    JS_SetPropertyStr (ctx, result, "up", makeCameraVec3 (ctx, *container, camera.getUp ()));
    JS_SetPropertyStr (ctx, result, "zoom", JS_NewFloat64 (ctx, camera.getZoom ()));

    return result;
}

JSValue set_camera_transforms (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc != 1 || !JS_IsObject (argv[0])) {
	return JS_ThrowTypeError (ctx, "setCameraTransforms expects a CameraTransforms object");
    }

    auto* container = get_opaque (this_val);
    auto& camera = container->getScene ().getCamera ();

    glm::vec3 eye = camera.getEye ();
    glm::vec3 center = camera.getCenter ();
    glm::vec3 up = camera.getUp ();

    JSValue eyeValue = JS_GetPropertyStr (ctx, argv[0], "eye");

    if (!readCameraVec3 (ctx, eyeValue, eye)) {
	JS_FreeValue (ctx, eyeValue);
	return JS_ThrowTypeError (ctx, "setCameraTransforms expects eye as a Vec3");
    }

    JS_FreeValue (ctx, eyeValue);

    JSValue centerValue = JS_GetPropertyStr (ctx, argv[0], "center");

    if (!readCameraVec3 (ctx, centerValue, center)) {
	JS_FreeValue (ctx, centerValue);
	return JS_ThrowTypeError (ctx, "setCameraTransforms expects center as a Vec3");
    }

    JS_FreeValue (ctx, centerValue);

    if (glm::dot (center - eye, center - eye) <= 1e-6f) {
	return JS_ThrowTypeError (ctx, "setCameraTransforms expects eye and center to differ");
    }

    // "up" is optional and keeps the current camera roll when omitted.
    JSValue upValue = JS_GetPropertyStr (ctx, argv[0], "up");

    if (JS_IsObject (upValue)) {
	if (!readCameraVec3 (ctx, upValue, up)) {
	    JS_FreeValue (ctx, upValue);
	    return JS_ThrowTypeError (ctx, "setCameraTransforms expects up as a Vec3");
	}
    } else if (!JS_IsUndefined (upValue) && !JS_IsNull (upValue)) {
	JS_FreeValue (ctx, upValue);
	return JS_ThrowTypeError (ctx, "setCameraTransforms expects up as a Vec3");
    }

    JS_FreeValue (ctx, upValue);

    float zoom = camera.getZoom ();
    JSValue zoomValue = JS_GetPropertyStr (ctx, argv[0], "zoom");

    if (JS_IsNumber (zoomValue)) {
	double zoomNumber = 0.0;

	if (JS_ToFloat64 (ctx, &zoomNumber, zoomValue) < 0) {
	    JS_FreeValue (ctx, zoomValue);
	    return JS_EXCEPTION;
	}

	if (!std::isfinite (zoomNumber) || zoomNumber <= 0.0) {
	    JS_FreeValue (ctx, zoomValue);
	    return JS_ThrowTypeError (ctx, "setCameraTransforms expects zoom to be a positive number");
	}

	zoom = static_cast<float> (zoomNumber);
    } else if (!JS_IsUndefined (zoomValue) && !JS_IsNull (zoomValue)) {
	JS_FreeValue (ctx, zoomValue);
	return JS_ThrowTypeError (ctx, "setCameraTransforms expects zoom to be a number");
    }

    JS_FreeValue (ctx, zoomValue);

    camera.setStaticTransforms (eye, center, up, zoom);

    return JS_UNDEFINED;
}

SceneObject::SceneObject (ScriptEngine& engine, Render::Wallpapers::CScene& scene) :
    m_scene (scene), m_engine (engine), m_classId (0) {
    this->m_definition = { .class_name = "IScene" };
    JS_NewClassID (this->m_engine.getRuntime (), &this->m_classId);
    JS_NewClass (this->m_engine.getRuntime (), this->m_classId, &this->m_definition);
    this->m_instance = JS_NewObjectClass (this->m_engine.getContext (), this->m_classId);

    JS_DupValue (this->m_engine.getContext (), this->m_instance);

    // set properties
    JS_SetOpaque (this->m_instance, this);
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "bloom"),
	JS_NewCFunction (this->m_engine.getContext (), get_bloom, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "bloomstrength"),
	JS_NewCFunction (this->m_engine.getContext (), get_bloomstrength, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "bloomthreshold"),
	JS_NewCFunction (this->m_engine.getContext (), get_bloomthreshold, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "clearenabled"),
	JS_NewCFunction (this->m_engine.getContext (), get_clearenabled, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "clearcolor"),
	JS_NewCFunction (this->m_engine.getContext (), get_clearcolor, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "ambientcolor"),
	JS_NewCFunction (this->m_engine.getContext (), get_ambientcolor, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "skylightcolor"),
	JS_NewCFunction (this->m_engine.getContext (), get_skylightcolor, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "fov"),
	JS_NewCFunction (this->m_engine.getContext (), get_fov, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), set_fov, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "nearz"),
	JS_NewCFunction (this->m_engine.getContext (), get_nearz, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "farz"),
	JS_NewCFunction (this->m_engine.getContext (), get_farz, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "camerafade"),
	JS_NewCFunction (this->m_engine.getContext (), get_camerafade, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "camerashake"),
	JS_NewCFunction (this->m_engine.getContext (), get_camerashake, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "camerashakespeed"),
	JS_NewCFunction (this->m_engine.getContext (), get_camerashakespeed, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance,
	JS_NewAtom (this->m_engine.getContext (), "camerashakeamplitude"),
	JS_NewCFunction (this->m_engine.getContext (), get_camerashakeamplitude, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance,
	JS_NewAtom (this->m_engine.getContext (), "camerashakeroughness"),
	JS_NewCFunction (this->m_engine.getContext (), get_camerashakeroughness, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "cameraparallax"),
	JS_NewCFunction (this->m_engine.getContext (), get_cameraparallax, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance,
	JS_NewAtom (this->m_engine.getContext (), "cameraparallaxamount"),
	JS_NewCFunction (this->m_engine.getContext (), get_cameraparallaxamount, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance,
	JS_NewAtom (this->m_engine.getContext (), "cameraparallaxdelay"),
	JS_NewCFunction (this->m_engine.getContext (), get_cameraparallaxdelay, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance,
	JS_NewAtom (this->m_engine.getContext (), "cameraparallaxmouseinfluence"),
	JS_NewCFunction (this->m_engine.getContext (), get_cameraparallaxmouseinfluence, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "getLayer",
	JS_NewCFunction (this->m_engine.getContext (), get_layer, "getLayer", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "getLayerIndex",
	JS_NewCFunction (this->m_engine.getContext (), get_layer_index, "getLayerIndex", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "createLayer",
	JS_NewCFunction (this->m_engine.getContext (), create_layer, "createLayer", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "destroyLayer",
	JS_NewCFunction (this->m_engine.getContext (), destroy_layer, "destroyLayer", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "sortLayer",
	JS_NewCFunction (this->m_engine.getContext (), sort_layer, "sortLayer", 2), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "getCameraTransforms",
	JS_NewCFunction (this->m_engine.getContext (), get_camera_transforms, "getCameraTransforms", 0),
	JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "setCameraTransforms",
	JS_NewCFunction (this->m_engine.getContext (), set_camera_transforms, "setCameraTransforms", 1),
	JS_PROP_ENUMERABLE
    );
    // TODO: ADD REST OF THE METHODS
}

SceneObject::~SceneObject () { JS_FreeValue (this->m_engine.getContext (), this->m_instance); }