#pragma once

#include "ObjectAdapter.h"

namespace WallpaperEngine::Scripting::Adapters {
class ScriptableObjectAdapter : public ObjectAdapter {
public:
    explicit ScriptableObjectAdapter (ScriptEngine& engine, std::string name);

    JSValue instantiate (ScriptableObject& object) override;
    JSValue instantiate (Data::Model::DynamicValue& value) override;

    /** Resolve the ScriptableObject behind an adapter instance (for layer methods). */
    static WallpaperEngine::Scripting::ScriptableObject* getObject (JSValueConst value);

private:
    JSClassExoticMethods m_exoticMethods;
    std::string m_name;
};
}