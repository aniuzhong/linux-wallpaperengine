#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Data/Parsers/WallpaperParser.h"

using WallpaperEngine::Data::JSON::JSON;
using WallpaperEngine::Data::Parsers::WallpaperParser;

TEST_CASE ("Scene projection type comes from the orthogonal projection block") {
    // 3047596375 (Starscape) scene.json general.orthogonalprojection: null
    CHECK (WallpaperParser::isPerspectiveScene (JSON::parse (R"({"orthogonalprojection":null})")));

    // 2348914357 (Space Journey) scene.json general.orthogonalprojection:
    // {"height":1080,"width":1920}
    CHECK_FALSE (
	WallpaperParser::isPerspectiveScene (JSON::parse (R"({"orthogonalprojection":{"height":1080,"width":1920}})"))
    );
}
