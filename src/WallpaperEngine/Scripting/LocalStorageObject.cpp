#include "LocalStorageObject.h"

#include "ScriptEngine.h"
#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Render/Wallpapers/CScene.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string_view>

using namespace WallpaperEngine::Scripting;

namespace {
LocalStorageObject* getStorage (JSValueConst value) {
    JSClassID classId = 0;
    return static_cast<LocalStorageObject*> (JS_GetAnyOpaque (value, &classId));
}

bool toString (JSContext* ctx, JSValueConst value, std::string& output) {
    const char* text = JS_ToCString (ctx, value);

    if (text == nullptr) {
	JS_FreeValue (ctx, JS_GetException (ctx));
	return false;
    }

    output = text;
    JS_FreeCString (ctx, text);
    return true;
}

LocalStorageObject::Location readLocation (JSContext* ctx, int argc, JSValueConst* argv, int index) {
    if (argc <= index) {
	return LocalStorageObject::Location::Screen;
    }

    std::string value;

    if (!toString (ctx, argv[index], value)) {
	return LocalStorageObject::Location::Screen;
    }

    return value == "global" ? LocalStorageObject::Location::Global : LocalStorageObject::Location::Screen;
}
} // namespace

JSValue localStorage_get (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* storage = getStorage (this_val);

    if (storage == nullptr || argc < 1) {
	return JS_NULL;
    }

    std::string key;

    if (!toString (ctx, argv[0], key)) {
	return JS_NULL;
    }

    return storage->get (ctx, key, readLocation (ctx, argc, argv, 1));
}

JSValue localStorage_set (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* storage = getStorage (this_val);

    if (storage == nullptr || argc < 2) {
	return JS_UNDEFINED;
    }

    std::string key;

    if (!toString (ctx, argv[0], key)) {
	return JS_UNDEFINED;
    }

    storage->set (ctx, key, argv[1], readLocation (ctx, argc, argv, 2));

    return JS_UNDEFINED;
}

JSValue localStorage_delete (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* storage = getStorage (this_val);

    if (storage == nullptr || argc < 1) {
	return JS_FALSE;
    }

    std::string key;

    if (!toString (ctx, argv[0], key)) {
	return JS_FALSE;
    }

    return JS_NewBool (ctx, storage->remove (key, readLocation (ctx, argc, argv, 1)));
}

JSValue localStorage_clear (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* storage = getStorage (this_val);

    if (storage != nullptr) {
	storage->clear (readLocation (ctx, argc, argv, 0));
    }

    return JS_UNDEFINED;
}

LocalStorageObject::LocalStorageObject (ScriptEngine& engine, Render::Wallpapers::CScene& scene) :
    m_scene (scene), m_engine (engine), m_classId (0) {
    this->m_definition = { .class_name = "ILocalStorage" };
    JS_NewClassID (this->m_engine.getRuntime (), &this->m_classId);
    JS_NewClass (this->m_engine.getRuntime (), this->m_classId, &this->m_definition);
    this->m_instance = JS_NewObjectClass (this->m_engine.getContext (), this->m_classId);

    JS_DupValue (this->m_engine.getContext (), this->m_instance);

    JS_SetOpaque (this->m_instance, this);
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "LOCATION_GLOBAL",
	JS_NewString (this->m_engine.getContext (), "global"), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "LOCATION_SCREEN",
	JS_NewString (this->m_engine.getContext (), "screen"), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "get",
	JS_NewCFunction (this->m_engine.getContext (), localStorage_get, "get", 2), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "set",
	JS_NewCFunction (this->m_engine.getContext (), localStorage_set, "set", 3), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "delete",
	JS_NewCFunction (this->m_engine.getContext (), localStorage_delete, "delete", 2), JS_PROP_ENUMERABLE
    );
    // Keep the helper name the old JS shim exposed.
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "remove",
	JS_NewCFunction (this->m_engine.getContext (), localStorage_delete, "remove", 2), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "clear",
	JS_NewCFunction (this->m_engine.getContext (), localStorage_clear, "clear", 1), JS_PROP_ENUMERABLE
    );
}

LocalStorageObject::~LocalStorageObject () { JS_FreeValue (this->m_engine.getContext (), this->m_instance); }

JSValue LocalStorageObject::get (JSContext* ctx, const std::string& key, Location location) {
    Bucket& values = this->bucket (location);
    const auto it = values.values.find (key);

    if (it == values.values.end ()) {
	return JS_NULL;
    }

    JSValue result = JS_ParseJSON (ctx, it->second.data (), it->second.size (), "<localStorage>");

    if (JS_IsException (result)) {
	JS_FreeValue (ctx, JS_GetException (ctx));
	sLog.error ("localStorage: stored value for '", key, "' is not valid JSON");
	return JS_NULL;
    }

    return result;
}

void LocalStorageObject::set (JSContext* ctx, const std::string& key, JSValueConst value, Location location) {
    JSValue serialized = JS_JSONStringify (ctx, value, JS_UNDEFINED, JS_UNDEFINED);

    if (JS_IsException (serialized)) {
	JS_FreeValue (ctx, JS_GetException (ctx));
	sLog.error ("localStorage: value for '", key, "' cannot be serialized");
	return;
    }

    std::string text = "null";

    if (!JS_IsUndefined (serialized)) {
	const char* raw = JS_ToCString (ctx, serialized);

	if (raw == nullptr) {
	    JS_FreeValue (ctx, JS_GetException (ctx));
	    JS_FreeValue (ctx, serialized);
	    sLog.error ("localStorage: value for '", key, "' cannot be read");
	    return;
	}

	text = raw;
	JS_FreeCString (ctx, raw);
    }

    JS_FreeValue (ctx, serialized);

    Bucket& values = this->bucket (location);
    values.values.insert_or_assign (key, std::move (text));
    this->save (values, location);
}

bool LocalStorageObject::remove (const std::string& key, Location location) {
    Bucket& values = this->bucket (location);
    const bool removed = values.values.erase (key) > 0;

    if (removed) {
	this->save (values, location);
    }

    return removed;
}

void LocalStorageObject::clear (Location location) {
    Bucket& values = this->bucket (location);
    values.values.clear ();
    this->save (values, location);
}

LocalStorageObject::Bucket& LocalStorageObject::bucket (Location location) {
    Bucket& result = location == Location::Global ? this->m_global : this->m_screen;
    this->load (result, location);

    return result;
}

void LocalStorageObject::load (Bucket& target, Location location) {
    if (target.loaded) {
	return;
    }

    target.loaded = true;

    const std::filesystem::path path = this->storagePath (location);
    std::ifstream file (path);

    if (!file.is_open ()) {
	return;
    }

    try {
	const nlohmann::json data = nlohmann::json::parse (file);

	if (!data.is_object ()) {
	    sLog.error ("localStorage: ", path, " does not contain an object");
	    return;
	}

	for (const auto& [key, value] : data.items ()) {
	    target.values.insert_or_assign (key, value.dump ());
	}
    } catch (const std::exception& e) {
	sLog.error ("localStorage: cannot read ", path, ": ", e.what ());
    }
}

void LocalStorageObject::save (Bucket& source, Location location) {
    const std::filesystem::path path = this->storagePath (location);
    nlohmann::json data = nlohmann::json::object ();

    for (const auto& [key, text] : source.values) {
	try {
	    data[key] = nlohmann::json::parse (text);
	} catch (const std::exception&) {
	    data[key] = text;
	}
    }

    std::error_code error;

    std::filesystem::create_directories (path.parent_path (), error);

    if (error) {
	sLog.error ("localStorage: cannot create ", path.parent_path (), ": ", error.message ());
	return;
    }

    const std::filesystem::path temporary = path.string () + ".tmp";
    std::ofstream file (temporary);

    if (!file.is_open ()) {
	sLog.error ("localStorage: cannot write ", temporary);
	return;
    }

    file << data.dump (2);
    file.close ();

    std::filesystem::rename (temporary, path, error);

    if (error) {
	sLog.error ("localStorage: cannot replace ", path, ": ", error.message ());
    }
}

std::string LocalStorageObject::storageKey () const {
    const Data::Model::Project& project = this->m_scene.getProject ();
    std::string key = project.workshopId;

    if (key.empty () || key.front () == '-') {
	key = "local-";

	for (const unsigned char character : project.title) {
	    if (std::isalnum (character) || character == '-' || character == '_' || character == '.') {
		key += static_cast<char> (character);
	    } else {
		key += '_';
	    }
	}
    }

    return key;
}

std::string LocalStorageObject::storagePath (Location location) const {
    std::filesystem::path base;
    const char* dataHome = std::getenv ("XDG_DATA_HOME");

    if (dataHome != nullptr && *dataHome != '\0') {
	base = dataHome;
    } else {
	const char* home = std::getenv ("HOME");

	if (home != nullptr && *home != '\0') {
	    base = std::filesystem::path (home) / ".local" / "share";
	} else {
	    base = std::filesystem::temp_directory_path ();
	}
    }

    const char* file = location == Location::Global ? "global.json" : "screen.json";

    return (base / "linux-wallpaperengine" / "storage" / this->storageKey () / file).string ();
}
