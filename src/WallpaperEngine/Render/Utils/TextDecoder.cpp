#include "TextDecoder.h"

#include <cstddef>
#include <cstdint>

namespace WallpaperEngine::Render::Utils {
namespace {
    constexpr char32_t kReplacementCharacter = U'\uFFFD';
    constexpr std::size_t kTabWidth = 4;
} // namespace

std::u32string decodeUtf8 (std::string_view text) {
    std::u32string result;
    result.reserve (text.size ());

    std::size_t position = 0;

    while (position < text.size ()) {
	const auto lead = static_cast<std::uint8_t> (text[position]);

	if (lead < 0x80) {
	    result.push_back (static_cast<char32_t> (lead));
	    ++position;
	    continue;
	}

	int extraBytes = 0;
	std::uint32_t codepoint = 0;
	std::uint32_t minimumCodepoint = 0;

	if (lead >= 0xC0 && lead <= 0xDF) {
	    extraBytes = 1;
	    codepoint = lead & 0x1F;
	    minimumCodepoint = 0x80;
	} else if (lead >= 0xE0 && lead <= 0xEF) {
	    extraBytes = 2;
	    codepoint = lead & 0x0F;
	    minimumCodepoint = 0x800;
	} else if (lead >= 0xF0 && lead <= 0xF7) {
	    extraBytes = 3;
	    codepoint = lead & 0x07;
	    minimumCodepoint = 0x10000;
	} else {
	    // stray continuation byte or an invalid lead
	    result.push_back (kReplacementCharacter);
	    ++position;
	    continue;
	}

	std::size_t validBytes = 1;
	bool complete = true;

	for (int index = 1; index <= extraBytes; ++index) {
	    const std::size_t offset = position + static_cast<std::size_t> (index);

	    if (offset >= text.size ()) {
		complete = false;
		break;
	    }

	    const auto continuation = static_cast<std::uint8_t> (text[offset]);

	    if ((continuation & 0xC0) != 0x80) {
		complete = false;
		break;
	    }

	    codepoint = (codepoint << 6) | (continuation & 0x3F);
	    ++validBytes;
	}

	if (!complete) {
	    // consume only the prefix that was structurally valid; the byte that
	    // broke the sequence is decoded again on the next iteration
	    result.push_back (kReplacementCharacter);
	    position += validBytes;
	    continue;
	}

	if (codepoint < minimumCodepoint || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
	    // structurally complete, but not a Unicode scalar value
	    result.push_back (kReplacementCharacter);
	    position += static_cast<std::size_t> (extraBytes) + 1;
	    continue;
	}

	result.push_back (static_cast<char32_t> (codepoint));
	position += static_cast<std::size_t> (extraBytes) + 1;
    }

    return result;
}

std::vector<std::u32string> splitLines (std::u32string_view text) {
    std::vector<std::u32string> lines (1);
    std::size_t column = 0;

    for (std::size_t index = 0; index < text.size (); ++index) {
	const char32_t codepoint = text[index];

	if (codepoint == U'\r') {
	    if (index + 1 < text.size () && text[index + 1] == U'\n') {
		++index;
	    }

	    lines.emplace_back ();
	    column = 0;
	    continue;
	}

	if (codepoint == U'\n') {
	    lines.emplace_back ();
	    column = 0;
	    continue;
	}

	if (codepoint == U'\t') {
	    const std::size_t spaces = kTabWidth - (column % kTabWidth);
	    lines.back ().append (spaces, U' ');
	    column += spaces;
	    continue;
	}

	if (codepoint < U'\u0020' || (codepoint >= U'\u007F' && codepoint <= U'\u009F')) {
	    // control characters have no visible glyph; drop them instead of
	    // rendering .notdef boxes
	    continue;
	}

	lines.back ().push_back (codepoint);
	++column;
    }

    return lines;
}

std::vector<std::u32string> decodeLines (std::string_view text) {
    std::u32string decoded = decodeUtf8 (text);

    if (decoded.starts_with (U'\uFEFF')) {
	decoded.erase (0, 1);
    }

    return splitLines (decoded);
}
} // namespace WallpaperEngine::Render::Utils
