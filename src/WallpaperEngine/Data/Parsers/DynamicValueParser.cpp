#include "DynamicValueParser.h"

#include "UserSettingParser.h"
#include "WallpaperEngine/Data/Model/DynamicValue.h"
#include "WallpaperEngine/Data/Utils/StringParsing.h"

#include <regex>
#include <set>

using namespace WallpaperEngine::Data::Parsers;
using WallpaperEngine::Data::Utils::parseFloat;

namespace {
/**
 * `addText` script properties are declared only inside the module source. Their
 * defaults are arbitrary strings, including comma-separated numeric text, which
 * the generic value parser would otherwise turn into vec2/vec3 values. Collect
 * the declared names so the script property can stay a JS string.
 *
 * Scene parsing only: this is not part of the per-frame path.
 */
std::set<std::string> collectAddTextNames (const std::string& source) {
    static const std::regex pattern (R"(\.addText\s*\(\s*\{[^}]*?\bname\s*:\s*(['"])([^'"]*)\1)", std::regex::optimize);

    std::set<std::string> result;

    for (auto it = std::sregex_iterator (source.begin (), source.end (), pattern); it != std::sregex_iterator ();
	 ++it) {
	result.insert ((*it)[2].str ());
    }

    return result;
}
} // namespace

DynamicValueUniquePtr
DynamicValueParser::parse (const json& data, const Properties& properties, bool expectColor, bool forceString) {
    auto value = std::make_unique<DynamicValue> ();
    auto valueIt = data;
    std::optional<std::string> scriptSource = std::nullopt;
    std::optional<json> scriptPropsJson = std::nullopt;

    if (data.is_object ()) {
	const auto user = data.optional ("user");
	const auto script = data.optional ("script");
	valueIt = data.require ("value", "User setting must have a value");

	if (script.has_value () && !script->is_null ()) {
	    scriptSource = script->get<std::string> ();
	    scriptPropsJson = data.optional ("scriptproperties");
	}
    }

    // actual value parsing
    if (valueIt.is_string ()) {
	if (forceString) {
	    value->update (valueIt.get<std::string> (), DynamicValue::UpdateSource::Initialization);
	} else if (expectColor) {
	    value->update (Builders::ColorBuilder::parse (valueIt), DynamicValue::UpdateSource::Initialization);
	} else {
	    std::string str = valueIt;
	    int size = Builders::VectorBuilder::preparseSize (str);

	    if (size == 1) {
		// scalar? text value?
		if (const auto parsed = parseFloat (str); parsed.has_value ()) {
		    value->update (*parsed, DynamicValue::UpdateSource::Initialization);
		} else {
		    value->update (str, DynamicValue::UpdateSource::Initialization);
		}
	    } else if (size == 2) {
		value->update (static_cast<glm::vec2> (valueIt), DynamicValue::UpdateSource::Initialization);
	    } else if (size == 3) {
		value->update (static_cast<glm::vec3> (valueIt), DynamicValue::UpdateSource::Initialization);
	    } else if (size == 0) {
		// no numeric tokens: text values and empty/whitespace-only strings
		// are kept as-is instead of being treated as a vector
		value->update (str, DynamicValue::UpdateSource::Initialization);
	    } else {
		value->update (static_cast<glm::vec4> (valueIt), DynamicValue::UpdateSource::Initialization);
	    }
	}
    } else if (valueIt.is_number_integer ()) {
	value->update (valueIt.get<int> (), DynamicValue::UpdateSource::Initialization);
    } else if (valueIt.is_number_float ()) {
	value->update (valueIt.get<float> (), DynamicValue::UpdateSource::Initialization);
    } else if (valueIt.is_boolean ()) {
	value->update (valueIt.get<bool> (), DynamicValue::UpdateSource::Initialization);
    } else if (valueIt.is_null ()) {
	// null value with no connection to property
	value->update (DynamicValue::UpdateSource::Initialization);
    }

    if (scriptSource.has_value ()) {
	std::map<std::string, UserSettingUniquePtr> scriptProps;

	if (scriptPropsJson.has_value () && scriptPropsJson->is_object ()) {
	    const auto textNames = collectAddTextNames (*scriptSource);

	    for (const auto& [key, propData] : scriptPropsJson->items ()) {
		scriptProps[key] = UserSettingParser::parse (propData, properties, false, textNames.contains (key));
	    }
	}

	value->setProperties (std::move (scriptProps));
	value->setScriptSource (scriptSource.value ());
    }

    return value;
}