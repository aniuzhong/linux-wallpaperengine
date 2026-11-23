#pragma once

#include "quickjs.h"

#include <map>
#include <string>

namespace WallpaperEngine::Render::Wallpapers {
class CScene;
}
namespace WallpaperEngine::Scripting {
class ScriptEngine;

/**
 * File-backed implementation of the SceneScript `localStorage` global.
 *
 * Values are kept per wallpaper (workshop id, or the project title for local
 * projects) in the user's data directory, split into a global and a per-screen
 * bucket. Each value is stored as JSON so types survive a reload.
 */
class LocalStorageObject {
public:
    LocalStorageObject (ScriptEngine& engine, Render::Wallpapers::CScene& scene);
    ~LocalStorageObject ();

    const Render::Wallpapers::CScene& getScene () const { return m_scene; }
    JSValue getInstance () const { return m_instance; }

    enum class Location { Screen, Global };

    /** Reads a value (parsed from JSON); missing values become JS null. */
    JSValue get (JSContext* ctx, const std::string& key, Location location);
    /** Serializes and writes a value, then persists the bucket. */
    void set (JSContext* ctx, const std::string& key, JSValueConst value, Location location);
    /** @return true when the key existed and was removed. */
    bool remove (const std::string& key, Location location);
    /** Removes every key in the bucket and persists it. */
    void clear (Location location);

private:
    struct Bucket {
	bool loaded = false;
	std::map<std::string, std::string> values;
    };

    Bucket& bucket (Location location);
    /** Ensures the matching JSON file has been read into the bucket. */
    void load (Bucket& target, Location location);
    /** Writes the bucket to disk, creating the per-wallpaper directory. */
    void save (Bucket& source, Location location);
    [[nodiscard]] std::string storageKey () const;
    [[nodiscard]] std::string storagePath (Location location) const;

protected:
    Render::Wallpapers::CScene& m_scene;
    ScriptEngine& m_engine;

    JSClassID m_classId;
    JSClassDef m_definition;
    JSValue m_instance;

    Bucket m_screen;
    Bucket m_global;
};
} // namespace WallpaperEngine::Scripting
