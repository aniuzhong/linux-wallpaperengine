#pragma once

#include <cctype>
#include <cstring>
#include <string_view>
#include <vector>

#include <glm/detail/qualifier.hpp>
#include <glm/detail/type_vec1.hpp>
#include <string>

#include "WallpaperEngine/Data/Utils/SFINAE.h"
#include "WallpaperEngine/Logging/Log.h"

namespace WallpaperEngine::Data::Builders {
using namespace WallpaperEngine::Data::Utils::SFINAE;

class VectorBuilder {
    /**
     * Convert template that calls the proper std::strto* function
     * based on the incoming type
     *
     * @tparam type
     * @param str
     * @return
     */
    template <typename type> static type convert (const char* str);

    /**
     * Vector literals arrive in two dialects: project.json values use spaces
     * ("0.1 0.2 0.3") while shader annotation defaults written by Wallpaper
     * Engine's editor use commas, with or without a trailing space
     * ("0.10,0.11,0.12" / "0.0, 1.0"). Treat commas and whitespace alike as
     * separators and split the literal into tokens; callers validate the
     * token count against the expected vector length.
     *
     * @param str
     * @return
     */
    static auto tokenize (std::string_view sv) {
	static constexpr std::string_view ws = ", \t\n\v\f\r";
	std::vector<std::string> v;
	for (auto b = sv.find_first_not_of (ws); b != std::string_view::npos;) {
	    auto e = sv.find_first_of (ws, b);
	    v.emplace_back (sv.substr (b, e - b));
	    b = sv.find_first_not_of (ws, e);
	}
	return v;
    }

public:
    /**
     * Takes the string and returns the vector size (2, 3 or 4)
     *
     * TODO: THIS SHOULD BE MOVED, RENAMED OR PLACED SOMEWHERE WHERE IT MAKES MORE SENSE
     *
     * @param str
     * @return
     */
    static int preparseSize (const std::string& str) {
	const std::size_t count = tokenize (str).size ();
	return static_cast<int> (count > 4 ? 4 : count);
    }

    /**
     * Takes a string value and parses it into a glm::vec.
     * Values can be separated by spaces or commas (any whitespace runs around
     * them are ignored); exactly one value per vector component is required.
     *
     * @tparam length Vector length
     * @tparam type Vector storage type
     * @tparam qualifier Precision qualifier
     *
     * @param str The string to parse the vector from
     *
     * @return
     */
    template <int length, typename type, glm::qualifier qualifier>
    [[nodiscard]] static glm::vec<length, type, qualifier> parse (const std::string& str) {
	// ensure a valid type is used, only 1 to 4 vectors are supported
	static_assert (length >= 1 && length <= 4, "Invalid vector length");

	const std::vector<std::string> tokens = tokenize (str);

	if (tokens.size () < static_cast<std::size_t> (length)) {
	    sLog.exception ("Invalid vector format: " + str + " (too few values, expected: ", length, ")");
	}

	if (tokens.size () > static_cast<std::size_t> (length)) {
	    sLog.exception ("Invalid vector format: " + str + " (too many values, expected: ", length, ")");
	}

	// token count validated, values can be used directly without issues
	glm::vec<length, type, qualifier> result;
	for (int i = 0; i < length; i++) {
	    result[i] = convert<type> (tokens[i].c_str ());
	}

	return result;
    }
    template <typename T, typename std::enable_if_t<is_glm_vec<T>::value, int> = 0>
    [[nodiscard]] static T parse (const std::string& str) {
	constexpr int length = GlmVecTraits<T>::length;
	constexpr glm::qualifier qualifier = GlmVecTraits<T>::qualifier;

	// call the specialized version of the function
	return parse<length, typename GlmVecTraits<T>::type, qualifier> (str);
    }
};

template <> inline float VectorBuilder::convert<float> (const char* str) { return std::strtof (str, nullptr); }

template <> inline int VectorBuilder::convert<int> (const char* str) { return std::stoi (str); }

template <> inline unsigned int VectorBuilder::convert<unsigned int> (const char* str) {
    return std::strtoul (str, nullptr, 10);
}

template <> inline double VectorBuilder::convert<double> (const char* str) { return std::strtod (str, nullptr); }

template <> inline uint8_t VectorBuilder::convert<uint8_t> (const char* str) { return std::strtoul (str, nullptr, 10); }

template <> inline uint16_t VectorBuilder::convert<uint16_t> (const char* str) {
    return std::strtoul (str, nullptr, 10);
}

template <> inline uint64_t VectorBuilder::convert<uint64_t> (const char* str) {
    return std::strtoull (str, nullptr, 10);
}

template <> inline int8_t VectorBuilder::convert<int8_t> (const char* str) { return std::strtol (str, nullptr, 10); }

template <> inline int16_t VectorBuilder::convert<int16_t> (const char* str) { return std::strtol (str, nullptr, 10); }

template <> inline int64_t VectorBuilder::convert<int64_t> (const char* str) { return std::strtoll (str, nullptr, 10); }

template <> inline bool VectorBuilder::convert<bool> (const char* str) { return std::strtoul (str, nullptr, 10) > 0; }

} // namespace WallpaperEngine::Data::Parsers
