#include <catch2/catch_test_macros.hpp>

#include <glm/vec2.hpp>
#include <optional>
#include <string>

#include "WallpaperEngine/Data/JSON.h"

using WallpaperEngine::Data::JSON::JSON;

namespace {
glm::vec2 paddingOf (const JSON& value) {
    const JSON object = { { "padding", value } };
    return object.optional ("padding", glm::vec2 (0.0f));
}
} // namespace

TEST_CASE ("JSON optional parses valid values") {
    CHECK (paddingOf ("8 9") == glm::vec2 (8.0f, 9.0f));
    CHECK (paddingOf (32) == glm::vec2 (32.0f));

    const JSON object = { { "name", "clock" }, { "id", 7 } };
    CHECK (object.optional ("name", std::string ("x")) == "clock");
    CHECK (object.optional ("id", 0) == 7);
    CHECK (object.optional ("missing", 3) == 3);
    CHECK (JSON { { "id", nullptr } }.optional ("id", 3) == 3);
}

TEST_CASE ("JSON optional falls back for values that cannot be converted") {
    // These used to escape the noexcept optional() overloads and call
    // std::terminate instead of keeping the caller's default.
    CHECK (paddingOf ("32") == glm::vec2 (0.0f));
    CHECK (paddingOf ("") == glm::vec2 (0.0f));
    CHECK (paddingOf (true) == glm::vec2 (0.0f));

    const JSON object = { { "name", true } };
    CHECK (object.optional ("name", std::string ("fallback")) == "fallback");
}
