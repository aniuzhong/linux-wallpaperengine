#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

#include "WallpaperEngine/Assets/AssetLocator.h"
#include "WallpaperEngine/Data/Model/Types.h"
#include "WallpaperEngine/FileSystem/Container.h"
#include "WallpaperEngine/Render/Shaders/ShaderUnit.h"

using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Render::Shaders;
using WallpaperEngine::Assets::AssetLocator;
using WallpaperEngine::FileSystem::Container;

TEST_CASE ("Arsenal's LIGHTMAP generic.vert does not wrap a_TexCoordVec4 in vec4 padding") {
    // Arsenal's pistols use generic.vert with LIGHTMAP enabled: the live branch
    // assigns the vec4 attribute a_TexCoordVec4 to v_TexCoord. The preprocessor
    // scans both #if branches, so the dead vec2 branch is still visible when
    // applyLinkedVaryingCompatibility widens v_TexCoord for the fragment stage.
    // Without the bare-identifier check this becomes vec4(a_TexCoordVec4, 0.0, 1.0).
    const std::string fragmentSource = R"(
varying vec4 v_TexCoord;

void main() {
}
)";

    const std::string vertexSource = R"(
attribute vec4 a_TexCoordVec4;
attribute vec2 a_TexCoord;
varying vec2 v_TexCoord;

void main() {
#if LIGHTMAP
    v_TexCoord = a_TexCoordVec4;
#else
    v_TexCoord = a_TexCoord;
#endif
}
)";

    auto container = std::make_unique<Container> ();
    AssetLocator assetLocator (std::move (container));
    const ShaderConstantMap constants = {};
    const TextureMap textures = {};
    // Arsenal's materials set "lightmap": 1, same as generic.vert's LIGHTMAP branch
    const ComboMap combos = { { "lightmap", 1 } };

    ShaderUnit fragment (
	GLSLContext::UnitType_Fragment, "generic.frag", fragmentSource, assetLocator, constants, textures, textures,
	combos, combos
    );
    ShaderUnit vertex (
	GLSLContext::UnitType_Vertex, "generic.vert", vertexSource, assetLocator, constants, textures, textures, combos,
	combos
    );

    vertex.linkToUnit (&fragment);

    const std::string& compiled = vertex.compile ();

    CHECK (compiled.find ("v_TexCoord = a_TexCoordVec4;") != std::string::npos);
    CHECK (compiled.find ("vec4(a_TexCoordVec4") == std::string::npos);
}
