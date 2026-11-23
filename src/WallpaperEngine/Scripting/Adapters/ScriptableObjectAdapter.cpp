#include "ScriptableObjectAdapter.h"

#include <cstring>
#include <utility>

#include "WallpaperEngine/Data/Model/Object.h"
#include "WallpaperEngine/Data/Utils/ScopeGuard.h"
#include "WallpaperEngine/Render/Objects/CParticle.h"
#include "WallpaperEngine/Render/Objects/CSound.h"
#include "WallpaperEngine/Scripting/ScriptEngine.h"
#include "WallpaperEngine/Scripting/ScriptableObject.h"

using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Data::Utils;
using namespace WallpaperEngine::Scripting::Adapters;

#define SCRIPTABLE_OPAQUE_MAGIC 0xdeadbeef

struct OpaqueScriptableObjectAdapter {
    unsigned int magic;
    ScriptableObjectAdapter& adapter;
    WallpaperEngine::Scripting::ScriptableObject& object;
};

void scriptableobject_finalizer (JSRuntime* rt, JSValueConst val);

JSValue scriptableobject_property_get (JSContext* ctx, JSValueConst obj_val, JSAtom atom, JSValueConst receiver) {
    JSClassID classId = 0;

    auto* container = static_cast<OpaqueScriptableObjectAdapter*> (JS_GetAnyOpaque (obj_val, &classId));

    if (!container || container->magic != SCRIPTABLE_OPAQUE_MAGIC) {
	return JS_EXCEPTION;
    }

    const char* name = JS_AtomToCString (ctx, atom);

    if (name == nullptr) {
	return JS_EXCEPTION;
    }

    ScopeGuard guard ([=] { JS_FreeCString (ctx, name); });

    const Object& model = container->object.getObject ();

    // WE exposes the generated layer implementation as `.instance`; old scripts
    // use it to reach properties available on the concrete layer class.
    if (std::strcmp (name, "instance") == 0) {
	return JS_DupValue (ctx, obj_val);
    }

    if (std::strcmp (name, "name") == 0) {
	return JS_NewString (ctx, model.name.c_str ());
    }

    // Image and text sizes are model metadata rather than scriptable values.
    if (std::strcmp (name, "size") == 0) {
	glm::vec2 size;
	bool found = false;

	if (model.is<Image> ()) {
	    size = model.as<Image> ()->size;
	    found = true;
	} else if (model.is<Text> ()) {
	    size = model.as<Text> ()->size;
	    found = true;
	}

	if (!found) {
	    return JS_UNDEFINED;
	}

	JSValue value = container->adapter.getEngine ().getAdapters ().vec2->instantiate ();
	JS_SetPropertyStr (ctx, value, "x", JS_NewFloat64 (ctx, size.x));
	JS_SetPropertyStr (ctx, value, "y", JS_NewFloat64 (ctx, size.y));

	return value;
    }

    try {
	// find the property inside, otherwise return undefined
	auto& property = container->object.getProperty (name);

	return container->adapter.getEngine ().dynamicToJs (property);
    } catch (const std::exception&) {
	return JS_UNDEFINED;
    }
}

int scriptableobject_property_set (
    JSContext* ctx, JSValueConst obj_val, JSAtom atom, JSValueConst val, JSValueConst receiver, int flags
) {
    JSClassID classId = 0;
    auto* container = static_cast<OpaqueScriptableObjectAdapter*> (JS_GetAnyOpaque (obj_val, &classId));

    if (!container || container->magic != SCRIPTABLE_OPAQUE_MAGIC) {
	return 0;
    }

    const char* name = JS_AtomToCString (ctx, atom);

    if (name == nullptr) {
	return 0;
    }

    ScopeGuard guard ([=] { JS_FreeCString (ctx, name); });

    // Image/text size is read-only metadata; ignore writes instead of logging a
    // missing-property error from the generic property path.
    if (std::strcmp (name, "size") == 0) {
	return 0;
    }

    try {
	auto& property = container->object.getProperty (name);
	container->adapter.getEngine ().writeJsValue (ctx, val, property);
    } catch (const std::exception&) { }

    return 0;
}

WallpaperEngine::Scripting::ScriptableObject* ScriptableObjectAdapter::getObject (JSValueConst value) {
    JSClassID classId = 0;
    auto* container = static_cast<OpaqueScriptableObjectAdapter*> (JS_GetAnyOpaque (value, &classId));

    if (!container || container->magic != SCRIPTABLE_OPAQUE_MAGIC) {
	return nullptr;
    }

    return &container->object;
}

static JSValue scriptableobject_emit_particles (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* object = ScriptableObjectAdapter::getObject (this_val);

    if (object == nullptr) {
	return JS_EXCEPTION;
    }

    int count = 1;
    if (argc >= 1) {
	JS_ToInt32 (ctx, &count, argv[0]);
    }

    object->emitParticles (count);
    return JS_UNDEFINED;
}

static JSValue scriptableobject_play (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* object = ScriptableObjectAdapter::getObject (this_val);

    if (object == nullptr) {
	return JS_EXCEPTION;
    }

    object->playSound ();
    return JS_UNDEFINED;
}

static JSValue scriptableobject_stop (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* object = ScriptableObjectAdapter::getObject (this_val);

    if (object == nullptr) {
	return JS_EXCEPTION;
    }

    object->stopSound ();
    return JS_UNDEFINED;
}

static JSValue scriptableobject_pause (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* object = ScriptableObjectAdapter::getObject (this_val);

    if (object == nullptr) {
	return JS_EXCEPTION;
    }

    object->pauseSound ();
    return JS_UNDEFINED;
}

static JSValue scriptableobject_is_playing (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* object = ScriptableObjectAdapter::getObject (this_val);

    if (object == nullptr) {
	return JS_EXCEPTION;
    }

    return JS_NewBool (ctx, object->isSoundPlaying ());
}

static JSValue scriptableobject_get_material (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    // Material property access is exposed for script compatibility; properties
    // written here are not bound to shader uniforms yet.
    return JS_NewObject (ctx);
}

static JSValue scriptableobject_get_effect (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* object = ScriptableObjectAdapter::getObject (this_val);

    if (object == nullptr || argc < 1) {
	return JS_UNDEFINED;
    }

    const Object& model = object->getObject ();

    if (!model.is<Image> ()) {
	return JS_UNDEFINED;
    }

    const auto& effects = model.as<Image> ()->effects;
    bool found = false;

    if (JS_IsNumber (argv[0])) {
	int index = 0;
	JS_ToInt32 (ctx, &index, argv[0]);
	found = index >= 0 && static_cast<size_t> (index) < effects.size ();
    } else {
	const char* query = JS_ToCString (ctx, argv[0]);

	if (query == nullptr) {
	    return JS_UNDEFINED;
	}

	for (const auto& effect : effects) {
	    if (effect->name == query) {
		found = true;
		break;
	    }
	}

	JS_FreeCString (ctx, query);
    }

    if (!found) {
	return JS_UNDEFINED;
    }

    JSValue result = JS_NewObject (ctx);
    JS_DefinePropertyValueStr (
	ctx, result, "getMaterial", JS_NewCFunction (ctx, scriptableobject_get_material, "getMaterial", 1),
	JS_PROP_ENUMERABLE
    );

    return result;
}

static JSValue scriptableobject_get_effect_count (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* object = ScriptableObjectAdapter::getObject (this_val);

    if (object == nullptr) {
	return JS_UNDEFINED;
    }

    const Object& model = object->getObject ();

    if (!model.is<Image> ()) {
	return JS_NewInt32 (ctx, 0);
    }

    return JS_NewInt32 (ctx, static_cast<int32_t> (model.as<Image> ()->effects.size ()));
}

ScriptableObjectAdapter::ScriptableObjectAdapter (ScriptEngine& engine, std::string name) :
    ObjectAdapter (engine), m_exoticMethods (
				{
				    .get_property = scriptableobject_property_get,
				    .set_property = scriptableobject_property_set,
				}
			    ),
    m_name (std::move (name)) {
    this->registerType (
	{
	    .class_name = m_name.c_str (),
	    .finalizer = scriptableobject_finalizer,
	    .exotic = &m_exoticMethods,
	}
    );
}

JSValue ScriptableObjectAdapter::instantiate (ScriptableObject& object) {
    JSValue result = this->ObjectAdapter::instantiate (object);
    JS_SetOpaque (
	result,
	new OpaqueScriptableObjectAdapter { .magic = SCRIPTABLE_OPAQUE_MAGIC, .adapter = *this, .object = object }
    );

    // Match WE's layer-specific API surface: emitParticles belongs to
    // IParticleSystem and play() is implemented for sound layers. Keep the
    // methods off unrelated layers so feature checks stay meaningful.
    JSContext* ctx = this->getEngine ().getContext ();

    // Use the data-model type: scripts on particle/sound layers are queued from
    // the ScriptableObject base constructor, before the C++ derived type is
    // complete. A dynamic_cast<CParticle*> at that point fails and would leave
    // thisLayer without emitParticles().
    const bool isImage = object.getObject ().is<Image> ();
    const bool isParticle = object.getObject ().is<Particle> ();
    const bool isSound = object.getObject ().is<Sound> ();

    if (isParticle) {
	JS_DefinePropertyValueStr (
	    ctx, result, "emitParticles", JS_NewCFunction (ctx, scriptableobject_emit_particles, "emitParticles", 1),
	    JS_PROP_ENUMERABLE
	);
    }

    if (isImage) {
	JS_DefinePropertyValueStr (
	    ctx, result, "getEffect", JS_NewCFunction (ctx, scriptableobject_get_effect, "getEffect", 1),
	    JS_PROP_ENUMERABLE
	);
	JS_DefinePropertyValueStr (
	    ctx, result, "getEffectCount",
	    JS_NewCFunction (ctx, scriptableobject_get_effect_count, "getEffectCount", 0), JS_PROP_ENUMERABLE
	);
    }

    if (isSound) {
	JS_DefinePropertyValueStr (
	    ctx, result, "play", JS_NewCFunction (ctx, scriptableobject_play, "play", 0), JS_PROP_ENUMERABLE
	);
	JS_DefinePropertyValueStr (
	    ctx, result, "stop", JS_NewCFunction (ctx, scriptableobject_stop, "stop", 0), JS_PROP_ENUMERABLE
	);
	JS_DefinePropertyValueStr (
	    ctx, result, "pause", JS_NewCFunction (ctx, scriptableobject_pause, "pause", 0), JS_PROP_ENUMERABLE
	);
	JS_DefinePropertyValueStr (
	    ctx, result, "isPlaying", JS_NewCFunction (ctx, scriptableobject_is_playing, "isPlaying", 0),
	    JS_PROP_ENUMERABLE
	);
    }

    return result;
}

void scriptableobject_finalizer (JSRuntime* rt, JSValueConst val) {
    JSClassID classId = 0;

    const auto* container = static_cast<OpaqueScriptableObjectAdapter*> (JS_GetAnyOpaque (val, &classId));

    if (container == nullptr || container->magic != SCRIPTABLE_OPAQUE_MAGIC) {
	return;
    }

    delete container;
}

JSValue ScriptableObjectAdapter::instantiate (DynamicValue& value) {
    throw std::runtime_error ("Cannot create a ScriptableObject instance from a DynamicValue");
}