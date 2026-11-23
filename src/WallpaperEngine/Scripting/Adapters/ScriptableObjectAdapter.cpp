#include "ScriptableObjectAdapter.h"

#include <utility>

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

    try {
	// find the property inside, otherwise return undefined
	auto& property = container->object.getProperty (name);

	return container->adapter.getEngine ().dynamicToJs (property);
    } catch (const std::exception&) {
	return JS_UNDEFINED;
    }
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

ScriptableObjectAdapter::ScriptableObjectAdapter (ScriptEngine& engine, std::string name) :
    ObjectAdapter (engine), m_exoticMethods (
				{
				    .get_property = scriptableobject_property_get,
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
    const bool isParticle = object.getObject ().is<Particle> ();
    const bool isSound = object.getObject ().is<Sound> ();

    if (isParticle) {
	JS_DefinePropertyValueStr (
	    ctx, result, "emitParticles", JS_NewCFunction (ctx, scriptableobject_emit_particles, "emitParticles", 1),
	    JS_PROP_ENUMERABLE
	);
    }

    if (isSound) {
	JS_DefinePropertyValueStr (
	    ctx, result, "play", JS_NewCFunction (ctx, scriptableobject_play, "play", 0), JS_PROP_ENUMERABLE
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