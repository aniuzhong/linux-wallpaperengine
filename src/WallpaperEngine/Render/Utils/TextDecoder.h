#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace WallpaperEngine::Render::Utils {
/**
 * Decodes UTF-8 into Unicode code points.
 *
 * Ill-formed input never throws. Each ill-formed subsequence is replaced with
 * U+FFFD. Structurally complete sequences that are not Unicode scalar values
 * (overlong forms, UTF-16 surrogates, code points above U+10FFFF) are consumed
 * as a unit. Structural errors consume only the valid prefix, so the byte that
 * broke the sequence is decoded again instead of being swallowed.
 */
[[nodiscard]] std::u32string decodeUtf8 (std::string_view text);

/**
 * Splits decoded text into lines.
 *
 * Handles LF, CRLF and lone CR as line breaks, expands tabs to the next
 * four-column stop (columns count code points, not terminal display width),
 * ignores other C0/C1 control characters, and preserves empty lines. Empty
 * input yields one empty line.
 */
[[nodiscard]] std::vector<std::u32string> splitLines (std::u32string_view text);

/**
 * Convenience wrapper for decodeUtf8() + splitLines(). A leading UTF-8 BOM is
 * removed.
 */
[[nodiscard]] std::vector<std::u32string> decodeLines (std::string_view text);
} // namespace WallpaperEngine::Render::Utils
