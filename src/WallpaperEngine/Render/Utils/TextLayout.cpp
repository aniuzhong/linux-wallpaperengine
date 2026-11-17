#include "TextLayout.h"

#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <utility>

namespace WallpaperEngine::Render::Utils {
namespace {
    int horizontalOffset (int contentWidth, int lineWidth, TextAlignment alignment) {
	switch (alignment) {
	    case TextAlignment::Left:
		return 0;
	    case TextAlignment::Right:
		return contentWidth - lineWidth;
	    case TextAlignment::Center:
		return (contentWidth - lineWidth) / 2;
	}

	return 0;
    }

    int verticalOffset (int contentHeight, int textHeight, TextVerticalAlignment alignment) {
	switch (alignment) {
	    case TextVerticalAlignment::Top:
		return 0;
	    case TextVerticalAlignment::Bottom:
		return contentHeight - textHeight;
	    case TextVerticalAlignment::Center:
		return (contentHeight - textHeight) / 2;
	}

	return 0;
    }
} // namespace

TextLayout layoutLines (
    const std::vector<std::u32string>& lines, const FontMetrics& fontMetrics, const TextLayoutOptions& options,
    const GlyphMetricsProvider& measureGlyph
) {
    TextLayout layout;

    FontMetrics metrics = fontMetrics;
    metrics.ascent = std::max (0, metrics.ascent);
    metrics.descent = std::max (0, metrics.descent);
    metrics.lineHeight = std::max ({ metrics.lineHeight, metrics.ascent + metrics.descent, 1 });
    layout.fontMetrics = metrics;

    std::unordered_map<char32_t, GlyphMetrics> glyphCache;
    const auto measure = [&glyphCache, &measureGlyph] (char32_t codepoint) -> GlyphMetrics {
	const auto found = glyphCache.find (codepoint);

	if (found != glyphCache.end ()) {
	    return found->second;
	}

	GlyphMetrics glyph = measureGlyph ? measureGlyph (codepoint) : GlyphMetrics {};
	glyph.advance = std::max (0, glyph.advance);
	glyphCache.emplace (codepoint, glyph);
	return glyph;
    };

    static const std::vector<std::u32string> kEmptyLines { std::u32string {} };
    const auto& sourceLines = lines.empty () ? kEmptyLines : lines;

    std::vector<TextLineLayout> lineLayouts;
    lineLayouts.reserve (sourceLines.size ());
    int maxLineWidth = 0;

    for (const auto& line : sourceLines) {
	TextLineLayout lineLayout;
	lineLayout.text = line;

	for (const char32_t codepoint : line) {
	    lineLayout.width += measure (codepoint).advance;
	}

	maxLineWidth = std::max (maxLineWidth, lineLayout.width);
	lineLayouts.push_back (std::move (lineLayout));
    }

    const int contentWidth = maxLineWidth;
    const int lineCount = static_cast<int> (lineLayouts.size ());

    // Font metrics, not the glyphs present, define the line box so dynamic text
    // does not jitter as characters change. The box is slightly taller than the
    // tight glyph bounds used by the old renderer, so a single line can sit
    // about one pixel off its previous baseline; that shift is intentional.
    const int contentHeight = metrics.ascent + metrics.descent + metrics.lineHeight * (lineCount - 1);
    const int paddingX = std::max (0, options.paddingX);
    const int paddingY = std::max (0, options.paddingY);

    const int boxWidth = std::max ({ 0, options.boxWidth, contentWidth + 2 * paddingX });
    const int boxHeight = std::max ({ 0, options.boxHeight, contentHeight + 2 * paddingY });

    layout.width = std::max (1, boxWidth);
    layout.height = std::max (1, boxHeight);

    const int contentAreaWidth = std::max (0, layout.width - 2 * paddingX);
    const int contentAreaHeight = std::max (0, layout.height - 2 * paddingY);
    const int blockTop = paddingY + verticalOffset (contentAreaHeight, contentHeight, options.verticalAlignment);

    for (std::size_t index = 0; index < lineLayouts.size (); ++index) {
	TextLineLayout& line = lineLayouts[index];
	line.x = paddingX + horizontalOffset (contentAreaWidth, line.width, options.alignment);
	line.baseline = blockTop + metrics.ascent + static_cast<int> (index) * metrics.lineHeight;
    }

    layout.lines = std::move (lineLayouts);
    return layout;
}
} // namespace WallpaperEngine::Render::Utils
