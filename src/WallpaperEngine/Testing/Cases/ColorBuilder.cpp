#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Data/Builders/ColorBuilder.h"

using WallpaperEngine::Data::Builders::ColorBuilder;
using WallpaperEngine::Data::Model::Color;

TEST_CASE ("2348914357 (Space Journey) colors: dotless values are parsed as normalized floats, not bytes") {
    // Space Journey hand-authors its color properties without
    // decimal points: hudcolor/shieldcolor "0 1 1" is cyan and
    // spaceshipdetailscolor "1 0 0" is red on the official client.
    // Parsing them as 0-255 bytes renders the whole scene almost black.
    CHECK (ColorBuilder::parse ("0 1 1") == Color (0, 1, 1, 1));
    CHECK (ColorBuilder::parse ("1 0 0") == Color (1, 0, 0, 1));
    CHECK (ColorBuilder::parse ("1 1 1") == Color (1, 1, 1, 1));
    CHECK (ColorBuilder::parse ("0 1 1", 0.5f) == Color (0, 1, 1, 0.5f));
}

TEST_CASE ("3774904326 (Lost Valley 2 | northway.) particle colors: byte-encoded dotless values are scaled down") {
    // particle presets store colors in 0-255 bytes without decimal points:
    // colorrandom "197 214 255" is pale blue on the official client, parsing
    // it as normalized floats saturates the rain sprites to pure white.
    const auto paleBlue = ColorBuilder::parse ("197 214 255");

    CHECK (paleBlue.r == 197.0f / 255.0f);
    CHECK (paleBlue.g == 214.0f / 255.0f);
    CHECK (paleBlue.b == 1.0f);
    CHECK (ColorBuilder::parse ("255 255 255") == Color (1, 1, 1, 1));
    // vec4 byte-encoded alpha scales with rgb; vec3 keeps the caller's normalized alpha
    CHECK (ColorBuilder::parse ("197 214 255 128") == Color (197.0f / 255.0f, 214.0f / 255.0f, 1.0f, 128.0f / 255.0f));
    CHECK (ColorBuilder::parse ("197 214 255", 0.5f) == Color (197.0f / 255.0f, 214.0f / 255.0f, 1.0f, 0.5f));
}
