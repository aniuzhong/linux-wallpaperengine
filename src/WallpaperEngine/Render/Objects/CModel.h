#pragma once

#include "CRenderable.h"
#include "WallpaperEngine/Render/CObject.h"
#include "WallpaperEngine/Render/Objects/Effects/CPass.h"
#include "WallpaperEngine/Render/Wallpapers/CScene.h"

#include "WallpaperEngine/Data/Model/ModelFile.h"
#include "WallpaperEngine/Data/Model/Object.h"

#include <glm/vec3.hpp>
#include <numeric>
#include <vector>

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render;

namespace WallpaperEngine::Render::Objects {
class CModel final : public CRenderable {
    friend CObject;

public:
    CModel (Wallpapers::CScene& scene, const ModelObject& model);
    ~CModel () override;

    void setup () override;
    void render () override;
    /**
     * Draws the model into an explicit framebuffer, used for the reflection
     * pass: the mirror transform reflects the model over the mirror plane
     * while the camera stays the original one
     */
    void render (const std::shared_ptr<const CFBO>& destination, const glm::mat4& mirror);

    [[nodiscard]] const ModelObject& getModel () const;

    [[nodiscard]] const float& getBrightness () const override;
    [[nodiscard]] const float& getUserAlpha () const override;
    [[nodiscard]] const float& getAlpha () const override;
    [[nodiscard]] const glm::vec3& getColor () const override;
    [[nodiscard]] const glm::vec4& getColor4 () const override;
    [[nodiscard]] const glm::vec3& getCompositeColor () const override;

private:
    /**
     * GL buffers for a single mesh section
     */
    struct SectionBuffers {
	GLuint vao = GL_NONE;
	GLuint vertexBuffer = GL_NONE;
	GLuint indexBuffer = GL_NONE;
	/** Number of floats per vertex, decided by the section's attribute mask */
	uint32_t stride = 0;
	/** Index count to draw */
	size_t indexCount = 0;
	/** Attribute mask used at bind time */
	uint32_t attrMask = 0;
    };

    void setupSections ();
    void updateModelMatrix ();

    /** Extra transform for the reflection pass (the mirror over the plane) */
    glm::mat4 m_reflectionTransform = glm::mat4 (1.0f);

    const ModelObject& m_model;
    std::vector<SectionBuffers> m_sections;

    /** One pass list per section, aligned with m_sections */
    std::vector<std::vector<Effects::CPass*>> m_passes;

    /** Section indices to draw, opaque sections first and translucent ones after */
    std::vector<size_t> m_drawOrder;

    glm::mat4 m_modelMatrix = glm::mat4 (1.0f);
    glm::mat4 m_viewProjectionMatrix = glm::mat4 (1.0f);
    glm::mat4 m_mvpMatrix = glm::mat4 (1.0f);
    glm::mat4 m_mvpMatrixInverse = glm::mat4 (1.0f);

    glm::vec3 m_color = glm::vec3 (1.0f);
    glm::vec4 m_color4 = glm::vec4 (1.0f);
};
} // namespace WallpaperEngine::Render::Objects
