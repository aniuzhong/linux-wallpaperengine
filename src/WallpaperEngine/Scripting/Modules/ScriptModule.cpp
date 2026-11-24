#include "ScriptModule.h"

#include "WallpaperEngine/Scripting/ScriptEngine.h"

using namespace WallpaperEngine::Scripting::Modules;

uint32_t WallpaperEngine::Scripting::Modules::getModuleInstanceId (JSContext* ctx, JSModuleDef* m) {
    JSValue value = JS_GetModulePrivateValue (ctx, m);
    int32_t result = 0;

    if (JS_IsNumber (value)) {
	JS_ToInt32 (ctx, &result, value);
    }

    JS_FreeValue (ctx, value);

    return static_cast<uint32_t> (result);
}

ScriptModule::ScriptModule (ScriptEngine& engine, const std::string& name, JSModuleInitFunc* init) :
    m_name (name), m_engine (engine) {
    this->m_definition = JS_NewCModule (this->m_engine.getContext (), this->m_name.c_str (), init);
}

ScriptModule::~ScriptModule () { }