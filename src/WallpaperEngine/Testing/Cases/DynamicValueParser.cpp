#include <catch2/catch_approx.hpp>
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

TEST_CASE ("Scripted layer angles convert between degrees and radians") {
    DynamicValue angles (glm::vec3 (1.0f, 0.0f, 0.0f));
    angles.setAngular (true);

    // Static and user/animation values are already radians and stay untouched.
    angles.update (glm::vec3 (0.5f, 0.25f, 0.0f), DynamicValue::UpdateSource::Initialization);
    CHECK (angles.getVec3 ().x == Catch::Approx (0.5f));

    // SceneScript works in degrees; the stored value (consumed by renderers) is radians.
    angles.update (glm::vec3 (180.0f, 0.0f, -90.0f), DynamicValue::UpdateSource::Script);
    CHECK (angles.getVec3 ().x == Catch::Approx (3.14159265f));
    CHECK (angles.getVec3 ().y == Catch::Approx (0.0f));
    CHECK (angles.getVec3 ().z == Catch::Approx (-1.57079633f));
}

TEST_CASE ("Non-angular values keep scripted numbers as-is") {
    DynamicValue value (glm::vec3 (0.0f));
    value.update (glm::vec3 (180.0f, 90.0f, 45.0f), DynamicValue::UpdateSource::Script);

    CHECK (value.getVec3 ().x == Catch::Approx (180.0f));
    CHECK (value.getVec3 ().y == Catch::Approx (90.0f));
    CHECK (value.getVec3 ().z == Catch::Approx (45.0f));
}

TEST_CASE ("Copying a DynamicValue preserves its angular flag") {
    DynamicValue angles (glm::vec3 (0.0f));
    angles.setAngular (true);

    DynamicValue copy (angles);
    CHECK (copy.isAngular ());

    copy.update (glm::vec3 (180.0f, 0.0f, 0.0f), DynamicValue::UpdateSource::Script);
    CHECK (copy.getVec3 ().x == Catch::Approx (3.14159265f));
}
