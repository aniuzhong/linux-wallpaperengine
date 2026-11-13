#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

#include "WallpaperEngine/Assets/AssetLocator.h"
#include "WallpaperEngine/Data/JSON.h"
#include "WallpaperEngine/Data/Model/Object.h"
#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Model/Wallpaper.h"
#include "WallpaperEngine/Data/Parsers/ObjectParser.h"
#include "WallpaperEngine/Data/Parsers/ProjectParser.h"
#include "WallpaperEngine/FileSystem/Container.h"

using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Data::Parsers;
using WallpaperEngine::Assets::AssetLocator;
using WallpaperEngine::Data::JSON::JSON;
using WallpaperEngine::FileSystem::Container;

namespace {
void writeU16 (std::stringstream& out, const uint16_t value) {
    out.write (reinterpret_cast<const char*> (&value), sizeof (value));
}

void writeU32 (std::stringstream& out, const uint32_t value) {
    out.write (reinterpret_cast<const char*> (&value), sizeof (value));
}

void writeFloat (std::stringstream& out, const float value) {
    out.write (reinterpret_cast<const char*> (&value), sizeof (value));
}

void writeString (std::stringstream& out, const std::string& value) {
    out.write (value.c_str (), static_cast<std::streamsize> (value.size () + 1));
}

std::string makeModel () {
    std::stringstream out (std::ios::in | std::ios::out | std::ios::binary);

    // 3047596375 (Starscape) is MDLV0017 with two sections; one section is
    // enough to check that ObjectParser creates a ModelObject.
    writeString (out, "MDLV0017");
    writeU32 (out, 15); // header mask
    writeU32 (out, 1); // skins
    writeU32 (out, 1); // sections
    writeString (out, "materials/models/space boi/diffuse_0.json");
    writeU32 (out, 0); // 16-bit indices
    writeFloat (out, -1120.0134f);
    writeFloat (out, -201.6729f);
    writeFloat (out, -1120.0134f);
    writeFloat (out, 1120.0134f);
    writeFloat (out, 606.3911f);
    writeFloat (out, 1120.0134f);
    writeU32 (out, 15); // section mask
    writeU32 (out, 3 * 12 * sizeof (float));
    for (int i = 0; i < 3 * 12; i++) {
	writeFloat (out, 0.0f);
    }
    writeU32 (out, 3 * sizeof (uint16_t));
    writeU16 (out, 0);
    writeU16 (out, 1);
    writeU16 (out, 2);

    return out.str ();
}

Project makeProject () {
    auto container = std::make_unique<Container> ();
    const JSON material = JSON::parse (R"({"passes":[{"shader":"genericimage2"}]})");

    container->getVFS ().add ("materials/models/space boi/diffuse_0.json", material);
    container->getVFS ().add ("models/space boi/space boi.mdl", makeModel ());

    return Project {
	.title = "object parser test",
	.type = Project::Type_Scene,
	.workshopId = "",
	.supportsAudioProcessing = false,
	.properties = {},
	.wallpaper = nullptr,
	.assetLocator = std::make_unique<AssetLocator> (std::move (container)),
    };
}
} // namespace

TEST_CASE ("3047596375 (Starscape) object parser builds a ModelObject") {
    auto project = makeProject ();
    const auto object = JSON::parse (R"({
        "id": 22,
        "name": "Base",
        "model": "models/space boi/space boi.mdl",
        "origin": "0.00000 -0.84027 0.00000",
        "scale": "0.00300 0.00300 0.00300",
        "angles": {"value": "0.00000 0.00000 0.00000"},
        "skin": null,
        "reflected": null
    })");

    const auto parsed = ObjectParser::parse (object, project);

    REQUIRE (parsed->is<ModelObject> ());
    const auto* model = parsed->as<ModelObject> ();
    CHECK (model->id == 22);
    CHECK (model->name == "Base");
    CHECK (model->skin == 0);
    CHECK_FALSE (model->reflected);

    REQUIRE (model->file != nullptr);
    CHECK (model->file->version == "MDLV0017");
    REQUIRE (model->file->sections.size () == 1);
    CHECK (model->file->sections.front ().materialPaths.front () == "materials/models/space boi/diffuse_0.json");

    const glm::vec3 origin = model->origin->value->getVec3 ();
    CHECK (origin.y == Catch::Approx (-0.84027f));
    const glm::vec3 scale = model->groupScale->value->getVec3 ();
    CHECK (scale.x == Catch::Approx (0.003f));
}

TEST_CASE ("2886819832 (Sonic CD - Special Stage) object parser builds a CameraObject") {
    auto project = makeProject ();
    const auto object = JSON::parse (R"({
        "id": 319,
        "name": "Dynamic Cam",
        "camera": "default",
        "path": "scripts/camera_paths_319.json",
        "queuemode": "random",
        "fov": 50.0,
        "zoom": 1.0,
        "origin": "0.00000 0.71910 0.00000",
        "angles": "0.00000 3.14159 0.00000",
        "visible": {
            "user": {"condition": "0", "name": "cameratype"},
            "value": true
        }
    })");

    const auto parsed = ObjectParser::parse (object, project);

    REQUIRE (parsed->is<CameraObject> ());
    const auto* camera = parsed->as<CameraObject> ();
    CHECK (camera->id == 319);
    CHECK (camera->name == "Dynamic Cam");
    CHECK (camera->camera == "default");
    CHECK (camera->path == "scripts/camera_paths_319.json");
    CHECK (camera->queueMode == "random");
    CHECK (camera->hasFov);
    CHECK (camera->fov->value->getFloat () == Catch::Approx (50.0f));
    CHECK (camera->zoom->value->getFloat () == Catch::Approx (1.0f));
    CHECK (camera->groupVisible->value->getBool ());

    const glm::vec3 origin = camera->origin->value->getVec3 ();
    CHECK (origin.y == Catch::Approx (0.7191f));
    const glm::vec3 angles = camera->groupAngles->value->getVec3 ();
    CHECK (angles.y == Catch::Approx (3.14159f));
}

TEST_CASE ("Audiophile legacy project loads as a scene with unique object ids") {
    // Official default project. project.json omits "type" and every object in
    // audiophile.json omits "id"/"name"; the model files are intentionally not
    // mounted, so parseModelObject must keep the base objects instead of
    // aborting the scene.
    auto container = std::make_unique<Container> ();

    container->getVFS ().add ("audiophile.json", JSON::parse (R"({
            "objects": [{
                "model": "models/audiophile/bars.mdl",
                "reflected": true
            }, {
                "model": "models/audiophile/flow.mdl"
            }, {
                "model": "models/audiophile/glow.mdl"
            }, {
                "model": "models/grid/grid.mdl",
                "skin": 1
            }],
            "camera": {
                "eye": "0 0.1 3.3",
                "center": "0 0.2 2",
                "up": "0 1 0"
            },
            "general": {
                "supportsaudioprocessing": true,
                "norecompile" : true,
                "clearcolor": "0.05 0.05 0.05"
            }
        })"));

    const auto project = ProjectParser::parse (
	JSON::parse (R"({
            "title": "Audiophile",
            "preview": "preview.jpg",
            "file": "audiophile.json",
            "timestamp": 0,
            "authorsteamid": "76561197975619619",
            "official" : true,
            "general": {
                "supportsaudioprocessing": true,
                "properties": {
                    "schemecolor": {
                        "type": "color",
                        "text": "ui_browse_properties_accent_color",
                        "value": "0.45 1 0.1"
                    }
                }
            }
        })"),
	std::make_unique<AssetLocator> (std::move (container))
    );

    CHECK (project->type == Project::Type_Scene);
    REQUIRE (project->wallpaper->is<Scene> ());

    const auto& scene = *project->wallpaper->as<Scene> ();
    REQUIRE (scene.objects.size () == 4);

    std::set<int> ids;

    for (const auto& object : scene.objects) {
	// -1 is reserved for CScene's internal bloom object.
	CHECK (object->id < -1);
	CHECK (object->name.empty ());
	ids.insert (object->id);
    }

    CHECK (ids.size () == scene.objects.size ());
}

TEST_CASE ("Sheep legacy application is not defaulted to scene") {
    // Sheep also omits "type", but its main file is a Unity application.
    CHECK_THROWS_AS (
	ProjectParser::parse (
	    JSON::parse (R"({
                "title": "Sheep",
                "preview": "preview.jpg",
                "file": "sheep.exe",
                "authorsteamid": "76561197975619619",
                "official" : true,
                "timestamp": 0,
                "general": {
                    "properties" : {
                        "schemecolor" : {
                            "text" : "Scheme color",
                            "type" : "color",
                            "value" : "0.19607843137254902 0.4470588235294118 0.25098039215686274"
                        }
                    }
                }
            })"),
	    std::make_unique<AssetLocator> (std::make_unique<Container> ())
	),
	std::runtime_error
    );
}
