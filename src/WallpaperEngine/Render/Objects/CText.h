#pragma once

#include <string>
#include <vector>

#include <GL/glew.h>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "WallpaperEngine/Render/CObject.h"
#include "WallpaperEngine/Scripting/ScriptEngine.h"
#include "WallpaperEngine/Scripting/ScriptableObject.h"

// Forward-declare FreeType types to avoid leaking the header into users.
struct FT_LibraryRec_;
struct FT_FaceRec_;
typedef struct FT_LibraryRec_* FT_Library;
typedef struct FT_FaceRec_* FT_Face;

namespace WallpaperEngine::Render::Wallpapers {
class CScene;
}

namespace WallpaperEngine::Render::Objects {
using namespace WallpaperEngine::Data::Model;

/**
 * Text renderer.
 *
 * Renders text objects as a single FreeType-rasterized single-channel (GL_RED)
 * texture drawn on a textured quad with its own minimal GLSL shader. Does NOT
 * go through CRenderable / materials / passes. Effects declared on the text
 * itself are not rendered yet; only inherited tint from composition-layer
 * ancestors is approximated in render() and renderLocal().
 *
 * Supports UTF-8 (malformed sequences become U+FFFD), multiline text,
 * embedded/system fonts, scripted/dynamic text and basic line alignment.
 * Wrapping, authored-box placement/padding and per-glyph font fallback are not
 * implemented yet.
 */
class CText final : virtual public CObject, public Scripting::ScriptableObject {
public:
    CText (Wallpapers::CScene& scene, const Text& text);
    ~CText () override;

    void setup () override;
    void render () override;
    [[nodiscard]] bool isVisible () const override;

    /**
     * Render into a composition layer's local framebuffer. Uses the text's own
     * transform relative to its parent (the composition layer), ignoring the
     * parent's scale, which is applied when the captured texture is composited.
     */
    void renderLocal (float width, float height);

private:
    // Rebuilds the glyph texture (and matching quad VBO) from the given string.
    // Reuses existing GL handles if already allocated, so this is safe to call
    // every time the rendered text changes.
    void rebuildTextureFrom (const std::string& text);
    void buildShader ();
    void uploadQuadVertices ();
    void draw (const glm::mat4& mvp, const glm::vec4& color);

    /** Updates scripted text, font size and the glyph texture for this frame. */
    bool prepareFrame ();
    /** Final vertex color, including composition-layer tint and text alpha. */
    [[nodiscard]] glm::vec4 resolveDrawColor () const;

    // setup() helpers (kept small to keep the setup flow linear).
    bool initFreeType ();
    bool loadEmbeddedFont ();
    bool loadSystemFont ();
    unsigned int computeEffectivePixelSize () const;
    void initScriptLayer ();

    const Text& m_text;
    std::string m_lastRenderedText;
    unsigned int m_lastPixelSize = 0;
    Scripting::ScriptLayerHandle m_layerHandle = Scripting::kInvalidLayerHandle;

    FT_Library m_ftLibrary = nullptr;
    FT_Face m_ftFace = nullptr;
    std::vector<uint8_t> m_fontData;

    GLuint m_texture = 0;
    GLuint m_program = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    GLint m_uMVP = -1;
    GLint m_uColor = -1;
    GLint m_uTexture = -1;

    glm::vec2 m_quadSize = { 0.0f, 0.0f };

    bool m_valid = false;
};
} // namespace WallpaperEngine::Render::Objects
