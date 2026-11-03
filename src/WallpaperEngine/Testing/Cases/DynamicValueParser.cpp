#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Data/Model/DynamicValue.h"
#include "WallpaperEngine/Data/Parsers/DynamicValueParser.h"

using WallpaperEngine::Data::JSON::JSON;
using WallpaperEngine::Data::Model::DynamicValue;
using WallpaperEngine::Data::Model::Properties;
using WallpaperEngine::Data::Parsers::DynamicValueParser;

TEST_CASE ("Text values are guessed as text, not vectors") {
    const Properties noProperties = {};
    auto guess = [&noProperties] (const char* raw) {
	return DynamicValueParser::parse (JSON (raw), noProperties, false)->getType ();
    };

    CHECK (guess ("W E D N E S D A Y") == DynamicValue::String); // 2944773634
    CHECK (guess ("30. January 2022") == DynamicValue::String); // 2944773634
    CHECK (guess ("12:00:00 AM") == DynamicValue::String); // 3218303536
}
