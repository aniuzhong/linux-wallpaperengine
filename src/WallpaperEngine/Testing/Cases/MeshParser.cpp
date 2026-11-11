#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <sstream>
#include <string>

#include "WallpaperEngine/Assets/AssetLocator.h"
#include "WallpaperEngine/Data/JSON.h"
#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Model/Wallpaper.h"
#include "WallpaperEngine/Data/Parsers/MeshParser.h"
#include "WallpaperEngine/FileSystem/Container.h"

using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Data::Parsers;
using WallpaperEngine::Assets::AssetLocator;
using WallpaperEngine::Data::JSON::JSON;
using WallpaperEngine::FileSystem::Container;

namespace {
Project makeProject () {
    auto container = std::make_unique<Container> ();
    const JSON material = JSON::parse (R"({"passes":[{"shader":"genericimage2"}]})");

    container->getVFS ().add ("materials/models/space boi/diffuse_0.json", material);
    container->getVFS ().add ("materials/models/space boi/diffuse_1.json", material);
    container->getVFS ().add ("materials/小窝.json", material);

    return Project {
	.title = "mesh parser test",
	.type = Project::Type_Scene,
	.workshopId = "",
	.supportsAudioProcessing = false,
	.properties = {},
	.wallpaper = nullptr,
	.assetLocator = std::make_unique<AssetLocator> (std::move (container)),
    };
}

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

std::shared_ptr<std::stringstream> makeStream () {
    return std::make_shared<std::stringstream> (std::ios::in | std::ios::out | std::ios::binary);
}

void writeTriangle (std::stringstream& out, const uint32_t attrMask, const uint32_t vertexCount) {
    const uint32_t stride = meshVertexStride (attrMask);

    writeU32 (out, vertexCount * stride * sizeof (float));

    for (uint32_t i = 0; i < vertexCount * stride; i++) {
	writeFloat (out, 0.0f);
    }

    writeU32 (out, 3 * sizeof (uint16_t));
    writeU16 (out, 0);
    writeU16 (out, 1 < vertexCount ? 1 : 0);
    writeU16 (out, 2 < vertexCount ? 2 : 0);
}
} // namespace

TEST_CASE ("3047596375 (Starscape) MDLV0017 mesh sections use the authored layout") {
    auto project = makeProject ();
    auto stream = makeStream ();

    writeString (*stream, "MDLV0017");
    writeU32 (*stream, 15); // header mask: position + normal + tangent + uv
    writeU32 (*stream, 1); // skins
    writeU32 (*stream, 2); // sections

    // section 0: materials/models/space boi/diffuse_0.json
    writeString (*stream, "materials/models/space boi/diffuse_0.json");
    writeU32 (*stream, 0); // 16-bit indices
    writeFloat (*stream, -1120.0134f);
    writeFloat (*stream, -201.6729f);
    writeFloat (*stream, -1120.0134f);
    writeFloat (*stream, 1120.0134f);
    writeFloat (*stream, 606.3911f);
    writeFloat (*stream, 1120.0134f);
    writeU32 (*stream, 15); // section mask
    writeTriangle (*stream, 15, 3);

    // section 1: materials/models/space boi/diffuse_1.json
    writeString (*stream, "materials/models/space boi/diffuse_1.json");
    writeU32 (*stream, 0); // 16-bit indices
    writeFloat (*stream, -892.1984f);
    writeFloat (*stream, -0.0001f);
    writeFloat (*stream, -883.4126f);
    writeFloat (*stream, 932.3599f);
    writeFloat (*stream, 833.6581f);
    writeFloat (*stream, 962.8938f);
    writeU32 (*stream, 15); // section mask
    writeTriangle (*stream, 15, 3);

    stream->seekg (0);

    const auto file = MeshParser::parse (stream, project, "space boi.mdl");

    REQUIRE (file != nullptr);
    CHECK (file->version == "MDLV0017");
    CHECK (file->headerAttrMask == 15);
    REQUIRE (file->sections.size () == 2);

    const auto& section = file->sections.front ();
    CHECK (section.attrMask == 15);
    REQUIRE (section.materialPaths.size () == 1);
    CHECK (section.materialPaths.front () == "materials/models/space boi/diffuse_0.json");
    REQUIRE (section.materials.size () == 1);
    CHECK (section.materials.front ()->passes.size () == 1);
    CHECK (section.vertices.size () == 3 * meshVertexStride (15));
    CHECK (section.indices.size () == 3);
    CHECK (section.aabbMin.x == Catch::Approx (-1120.0134f));
    CHECK (section.aabbMax.y == Catch::Approx (606.3911f));

    const auto layout = meshVertexLayout (section.attrMask);
    CHECK (layout.stride == 12);
    CHECK (layout.normalOffset == 3);
    CHECK (layout.tangentOffset == 6);
    CHECK (layout.skinOffset == -1);
    CHECK (layout.uvOffset == 10);
    CHECK (layout.lightmapUVOffset == -1);

    CHECK (file->sections[1].materialPaths.front () == "materials/models/space boi/diffuse_1.json");
}

TEST_CASE ("3803547930 (puppet) MDLV0023 skinned submesh uses its layout tag") {
    auto project = makeProject ();
    auto stream = makeStream ();

    writeString (*stream, "MDLV0023");
    writeU32 (*stream, 25165833); // header word
    writeU32 (*stream, 1); // skins
    writeU32 (*stream, 1); // sections

    writeString (*stream, "materials/小窝.json");
    writeU32 (*stream, 0); // 16-bit indices

    for (int i = 0; i < 6; i++) {
	writeFloat (*stream, 0.0f); // empty bounding box
    }

    writeU32 (*stream, 25165839); // real skinned layout tag
    writeTriangle (*stream, 25165839, 3);

    for (int i = 0; i < 6; i++) {
	writeU16 (*stream, 0); // trailer
    }

    stream->seekg (0);

    const auto file = MeshParser::parse (stream, project, "puppet.mdl");

    REQUIRE (file != nullptr);
    CHECK (file->version == "MDLV0023");
    CHECK (file->headerAttrMask == 25165833);
    REQUIRE (file->sections.size () == 1);

    const auto& section = file->sections.front ();
    CHECK (section.attrMask == 25165839);
    REQUIRE (section.materialPaths.size () == 1);
    CHECK (section.materialPaths.front () == "materials/小窝.json");
    REQUIRE (section.materials.size () == 1);
    CHECK (section.materials.front ()->passes.size () == 1);
    CHECK (section.vertices.size () == 3 * meshVertexStride (25165839));
    CHECK (section.indices.size () == 3);

    const auto layout = meshVertexLayout (section.attrMask);
    CHECK (layout.stride == 20);
    CHECK (layout.normalOffset == 3);
    CHECK (layout.tangentOffset == 6);
    CHECK (layout.skinOffset == 10);
    CHECK (layout.uvOffset == 18);
    CHECK (layout.lightmapUVOffset == -1);
}
