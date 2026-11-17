#pragma once

#include <functional>
#include <string>
#include <vector>

namespace WallpaperEngine::Render::Utils {
enum class TextAlignment {
    Left,
    Center,
    Right,
};

enum class TextVerticalAlignment {
    Top,
    Center,
    Bottom,
};

struct FontMetrics {
    int ascent = 0;
    int descent = 0;
    int lineHeight = 0;
};

struct GlyphMetrics {
    int advance = 0;
    int width = 0;
    int height = 0;
    int bearingX = 0;
    int bearingY = 0;
};

struct TextLayoutOptions {
    int boxWidth = 0;
    int boxHeight = 0;
    int paddingX = 0;
    int paddingY = 0;
    TextAlignment alignment = TextAlignment::Center;
    TextVerticalAlignment verticalAlignment = TextVerticalAlignment::Center;
};

struct TextLineLayout {
    std::u32string text;
    int width = 0;
    int x = 0;
    int baseline = 0;
};

struct TextLayout {
    int width = 1;
    int height = 1;
    FontMetrics fontMetrics;
    std::vector<TextLineLayout> lines;
};

using GlyphMetricsProvider = std::function<GlyphMetrics (char32_t)>;

/**
 * Lays out already-decoded lines into a bitmap-sized text box.
 *
 * Line heights and baselines come from the font metrics, not from the glyphs
 * that happen to be present, so dynamic text keeps a stable box. Horizontal
 * and vertical alignment are applied inside the authored box (when one is
 * given); padding is applied inside the box as well. If the box is smaller
 * than the measured content, the box is expanded so glyphs are not clipped.
 */
[[nodiscard]] TextLayout layoutLines (
    const std::vector<std::u32string>& lines, const FontMetrics& fontMetrics, const TextLayoutOptions& options,
    const GlyphMetricsProvider& measureGlyph
);
} // namespace WallpaperEngine::Render::Utils
