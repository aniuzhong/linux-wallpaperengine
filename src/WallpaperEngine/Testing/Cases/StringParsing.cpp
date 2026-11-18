#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Data/Utils/StringParsing.h"

using namespace WallpaperEngine::Data::Utils;

TEST_CASE ("parseLegacyBoolean accepts the legacy spellings", "[data]") {
    CHECK (parseLegacyBoolean ("true"));
    CHECK (parseLegacyBoolean ("1"));
    CHECK_FALSE (parseLegacyBoolean ("false"));
    CHECK_FALSE (parseLegacyBoolean ("0"));
    CHECK_FALSE (parseLegacyBoolean (""));
}

TEST_CASE ("parseFloat requires the complete string to be numeric", "[data]") {
    CHECK (parseFloat ("150") == 150.0f);
    CHECK (parseFloat ("-1.5") == -1.5f);
    CHECK (parseFloat ("1e3") == 1000.0f);
    CHECK_FALSE (parseFloat ("150px").has_value ());
    CHECK_FALSE (parseFloat ("1e999").has_value ());
    CHECK_FALSE (parseFloat ("").has_value ());
}

TEST_CASE ("parseInt32 requires a complete, in-range integer", "[data]") {
    CHECK (parseInt32 ("42") == 42);
    CHECK (parseInt32 ("-7") == -7);
    CHECK (parseInt32 ("ff", 16) == 255);
    CHECK_FALSE (parseInt32 ("42px").has_value ());
    CHECK_FALSE (parseInt32 ("2147483648").has_value ());
    CHECK_FALSE (parseInt32 ("").has_value ());
}

TEST_CASE ("parseUint32 rejects signs and values outside the unsigned range", "[data]") {
    CHECK (parseUint32 ("42") == 42u);
    CHECK (parseUint32 ("ffffffff", 16) == 4294967295u);
    CHECK_FALSE (parseUint32 ("-1").has_value ());
    CHECK_FALSE (parseUint32 ("4294967296").has_value ());
    CHECK_FALSE (parseUint32 ("42px").has_value ());
}
