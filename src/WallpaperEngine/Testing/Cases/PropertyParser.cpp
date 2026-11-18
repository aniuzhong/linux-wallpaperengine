#include <catch2/catch_test_macros.hpp>

#include <string>

#include "WallpaperEngine/Data/Model/Property.h"
#include "WallpaperEngine/Data/Parsers/PropertyParser.h"

using WallpaperEngine::Data::JSON::JSON;
using WallpaperEngine::Data::Model::DynamicValue;
using WallpaperEngine::Data::Parsers::PropertyParser;

TEST_CASE ("Bool properties without a value default to false") {
    const JSON propertyData = {
	{ "type", "bool" },
	{ "text", "Enabled" },
    };

    const auto property = PropertyParser::parse (propertyData, "enabled");

    REQUIRE (property != nullptr);
    CHECK (property->getType () == DynamicValue::Boolean);
    CHECK_FALSE (property->getBool ());
}

TEST_CASE ("Corsair-O-Tron stringified bool property parses") {
    // corsair_o_tron/project.json: "logorings" is authored as the string
    // "true". Reading it as a JSON bool used to throw type_error.
    const JSON propertyData = {
	{ "order", 3 },
	{ "text", "Show logo rings" },
	{ "type", "bool" },
	{ "value", "true" },
    };

    const auto property = PropertyParser::parse (propertyData, "logorings");

    REQUIRE (property != nullptr);
    CHECK (property->getType () == DynamicValue::Boolean);
    CHECK (property->getBool ());
}

TEST_CASE ("Corsair-O-Tron stringified slider value parses") {
    // corsair_o_tron/project.json: "showbottom" stores its value as "150".
    const JSON propertyData = {
	{ "order", 9 },     { "text", "Bottom bar height" }, { "type", "slider" }, { "min", 0 }, { "max", 650 },
	{ "value", "150" },
    };

    const auto property = PropertyParser::parse (propertyData, "showbottom");

    REQUIRE (property != nullptr);
    CHECK (property->getType () == DynamicValue::Float);
    CHECK (property->getFloat () == 150.0f);

    const std::string dump = property->dump ();
    CHECK (dump.find ("Min: 0") != std::string::npos);
    CHECK (dump.find ("Max: 650") != std::string::npos);
    CHECK (dump.find ("Value: 150") != std::string::npos);
}

TEST_CASE ("Directory properties are parsed as file-like properties") {
    const JSON propertyData = {
	{ "type", "directory" },
	{ "text", "Folder" },
    };

    const auto property = PropertyParser::parse (propertyData, "folder");

    REQUIRE (property != nullptr);
    CHECK (property->dump ().find ("folder - file") != std::string::npos);
}