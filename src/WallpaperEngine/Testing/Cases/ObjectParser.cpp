#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>

#include "WallpaperEngine/Assets/AssetLocator.h"
#include "WallpaperEngine/Data/JSON.h"
#include "WallpaperEngine/Data/Model/Object.h"
#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Model/Wallpaper.h"
#include "WallpaperEngine/Data/Parsers/ObjectParser.h"
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
