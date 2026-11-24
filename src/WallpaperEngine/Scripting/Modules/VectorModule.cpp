#include "VectorModule.h"

#include <map>

#include "VectorMath.h"
#include "WallpaperEngine/Scripting/ScriptEngine.h"

using namespace WallpaperEngine::Scripting::Modules;

namespace {
uint32_t VectorModuleInstanceId = 0;
std::map<uint32_t, VectorModule&> vectorModules;

VectorModule* moduleForMagic (const int magic) {
    const auto it = vectorModules.find (static_cast<uint32_t> (magic));

    if (it == vectorModules.end ()) {
	return nullptr;
    }

    return &it->second;
}

JSValue wevector_angle_vector2 (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    if (argc != 1 || !JS_IsNumber (argv[0])) {
	return JS_ThrowTypeError (ctx, "angleVector2 expects a number of degrees");
    }

    VectorModule* module = moduleForMagic (magic);

    if (module == nullptr) {
	return JS_ThrowInternalError (ctx, "WEVector module is not initialized");
    }

    double degrees = 0.0;

    if (JS_ToFloat64 (ctx, &degrees, argv[0]) < 0) {
	return JS_EXCEPTION;
    }

    const glm::vec2 direction = VectorMath::angleVector2 (degrees);
    auto* adapter = module->getEngine ().getAdapters ().vec2.get ();
    JSValue result = adapter->instantiate ();

    JS_SetPropertyStr (ctx, result, "x", JS_NewFloat64 (ctx, direction.x));
    JS_SetPropertyStr (ctx, result, "y", JS_NewFloat64 (ctx, direction.y));

    return result;
}

JSValue wevector_vector_angle2 (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    if (argc != 1 || !JS_IsObject (argv[0])) {
	return JS_ThrowTypeError (ctx, "vectorAngle2 expects a Vec2");
    }

    JSValue x = JS_GetPropertyStr (ctx, argv[0], "x");
    JSValue y = JS_GetPropertyStr (ctx, argv[0], "y");

    if (!JS_IsNumber (x) || !JS_IsNumber (y)) {
	JS_FreeValue (ctx, x);
	JS_FreeValue (ctx, y);
	return JS_ThrowTypeError (ctx, "vectorAngle2 expects a Vec2");
    }

    double xValue = 0.0;
    double yValue = 0.0;

    JS_ToFloat64 (ctx, &xValue, x);
    JS_ToFloat64 (ctx, &yValue, y);

    JS_FreeValue (ctx, x);
    JS_FreeValue (ctx, y);

    return JS_NewFloat64 (ctx, VectorMath::vectorAngle2 ({ static_cast<float> (xValue), static_cast<float> (yValue) }));
}

int wevector_init (JSContext* ctx, JSModuleDef* m) {
    const uint32_t instanceId = getModuleInstanceId (ctx, m);

    JS_SetModuleExport (
	ctx, m, "angleVector2",
	JS_NewCFunctionMagic (ctx, wevector_angle_vector2, "angleVector2", 1, JS_CFUNC_generic_magic, instanceId)
    );
    JS_SetModuleExport (
	ctx, m, "vectorAngle2",
	JS_NewCFunctionMagic (ctx, wevector_vector_angle2, "vectorAngle2", 1, JS_CFUNC_generic_magic, instanceId)
    );

    return 0;
}
} // namespace

VectorModule::VectorModule (ScriptEngine& engine) : ScriptModule (engine, "WEVector", wevector_init) {
    this->m_instanceId = ++VectorModuleInstanceId;

    JS_AddModuleExport (this->getEngine ().getContext (), this->getDefinition (), "angleVector2");
    JS_AddModuleExport (this->getEngine ().getContext (), this->getDefinition (), "vectorAngle2");
    JS_SetModulePrivateValue (
	this->getEngine ().getContext (), this->getDefinition (),
	JS_NewInt32 (this->getEngine ().getContext (), this->m_instanceId)
    );

    vectorModules.emplace (this->m_instanceId, *this);
}

VectorModule::~VectorModule () { vectorModules.erase (this->m_instanceId); }
