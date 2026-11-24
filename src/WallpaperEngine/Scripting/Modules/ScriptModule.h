#pragma once

#include "quickjs.h"

#include <string>

namespace WallpaperEngine::Scripting {
class ScriptEngine;
}
namespace WallpaperEngine::Scripting::Modules {
/**
 * Reads the instance id stored in a C module's private value.
 *
 * Module init callbacks only receive the module definition, so constructors
 * store their id with JS_SetModulePrivateValue and the callback retrieves it
 * here. Returns 0 when no value was set.
 */
uint32_t getModuleInstanceId (JSContext* ctx, JSModuleDef* m);

class ScriptModule {
public:
    virtual ~ScriptModule ();

    ScriptEngine& getEngine () const { return m_engine; }
    const std::string& getName () const { return m_name; }
    JSModuleDef* getDefinition () const { return m_definition; }

protected:
    ScriptModule (ScriptEngine& engine, const std::string& name, JSModuleInitFunc* init);

    const std::string m_name;
    ScriptEngine& m_engine;
    JSModuleDef* m_definition;
};
}