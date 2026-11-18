#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace WallpaperEngine::Data::Utils {
/**
 * Wallpaper Engine serializes booleans as JSON booleans and, in legacy
 * projects, as the strings "true" / "1". Everything else is false.
 */
[[nodiscard]] inline bool parseLegacyBoolean (std::string_view value) { return value == "true" || value == "1"; }

/**
 * Parses a complete, unprefixed integer string in the given base.
 * Returns nullopt for empty, partial or out-of-range values.
 */
template <typename T> [[nodiscard]] inline std::optional<T> parseInteger (std::string_view value, int base = 10) {
    if (value.empty ()) {
	return std::nullopt;
    }

    T result = 0;
    const auto [end, error] = std::from_chars (value.data (), value.data () + value.size (), result, base);

    if (error != std::errc {} || end != value.data () + value.size ()) {
	return std::nullopt;
    }

    return result;
}

[[nodiscard]] inline std::optional<std::int32_t> parseInt32 (std::string_view value, int base = 10) {
    return parseInteger<std::int32_t> (value, base);
}

[[nodiscard]] inline std::optional<std::uint32_t> parseUint32 (std::string_view value, int base = 10) {
    return parseInteger<std::uint32_t> (value, base);
}

/**
 * Parses a complete numeric string. Returns nullopt for empty, partially
 * numeric ("150px") or out-of-range values.
 */
[[nodiscard]] inline std::optional<float> parseFloat (std::string_view value) {
    std::string copy (value);
    std::size_t parsed = 0;

    try {
	const float result = std::stof (copy, &parsed);

	if (parsed != copy.size ()) {
	    return std::nullopt;
	}

	return result;
    } catch (const std::logic_error&) {
	return std::nullopt;
    }
}
} // namespace WallpaperEngine::Data::Utils
