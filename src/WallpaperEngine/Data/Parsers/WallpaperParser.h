#pragma once

#include "WallpaperEngine/Data/JSON.h"
#include "WallpaperEngine/Data/Model/Types.h"
#include "WallpaperEngine/Data/Model/Wallpaper.h"

namespace WallpaperEngine::Data::Parsers {
using JSON = WallpaperEngine::Data::JSON::JSON;
using namespace WallpaperEngine::Data::Model;

class WallpaperParser {
public:
    static WallpaperUniquePtr parse (const JSON& file, Project& project);
    /**
     * Wallpaper Engine 2D scenes carry an orthogonalprojection object; 3D scenes
     * set it to null or omit it (and keep fov/nearz/farz at the general level).
     */
    [[nodiscard]] static bool isPerspectiveScene (const JSON& general);

private:
    static SceneUniquePtr parseScene (const JSON& file, Project& project);
    static VideoUniquePtr parseVideo (const JSON& file, Project& project);
    static WebUniquePtr parseWeb (const JSON& file, Project& project);
    static std::vector<CameraPathSource> parseCameraPaths (const JSON& camera, const Project& project);
    static ObjectList parseObjects (const JSON& objects, const Project& project);
};
} // namespace WallpaperEngine::Data::Parsers
