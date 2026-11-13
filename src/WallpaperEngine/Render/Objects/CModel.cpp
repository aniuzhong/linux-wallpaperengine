#include "CModel.h"

#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Render/Helpers/ContextAware.h"
#include "WallpaperEngine/Render/Wallpapers/CScene.h"

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render;
using namespace WallpaperEngine::Render::Objects;

namespace {
/**
 * Minimal 1x1 white texture, models have no base texture of their own but
 * CPass expects one for g_Texture0's resolution chain.
 */
class WhiteTexture final : public TextureProvider {
public:
    WhiteTexture () {
	glGenTextures (1, &this->m_texture);
	glBindTexture (GL_TEXTURE_2D, this->m_texture);
	const uint8_t white[4] = { 255, 255, 255, 255 };
	glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	this->m_resolution = glm::vec4 (1.0f, 1.0f, 1.0f / 1.0f, 1.0f / 1.0f);
    }

    ~WhiteTexture () override { glDeleteTextures (1, &this->m_texture); }

    [[nodiscard]] GLuint getTextureID (uint32_t) const override { return this->m_texture; }
    [[nodiscard]] uint32_t getTextureWidth (uint32_t) const override { return 1; }
    [[nodiscard]] uint32_t getTextureHeight (uint32_t) const override { return 1; }
    [[nodiscard]] uint32_t getRealWidth () const override { return 1; }
    [[nodiscard]] uint32_t getRealHeight () const override { return 1; }
    [[nodiscard]] TextureFormat getFormat () const override { return TextureFormat_ARGB8888; }
    [[nodiscard]] uint32_t getFlags () const override { return 0; }
    [[nodiscard]] const std::vector<FrameSharedPtr>& getFrames () const override { return this->m_frames; }
    [[nodiscard]] const glm::vec4* getResolution () const override { return &this->m_resolution; }
    [[nodiscard]] bool isAnimated () const override { return false; }
    [[nodiscard]] bool isReady () const override { return true; }
    [[nodiscard]] uint32_t getSpritesheetCols () const override { return 0; }
    [[nodiscard]] uint32_t getSpritesheetRows () const override { return 0; }
    [[nodiscard]] uint32_t getSpritesheetFrames () const override { return 0; }
    [[nodiscard]] float getSpritesheetDuration () const override { return 0.0f; }
    void update () const override { }
    void incrementUsageCount () const override { }
    void decrementUsageCount () const override { }

private:
    GLuint m_texture = GL_NONE;
    std::vector<FrameSharedPtr> m_frames = {};
    glm::vec4 m_resolution = glm::vec4 (1.0f);
};
} // namespace

CModel::CModel (Wallpapers::CScene& scene, const ModelObject& model) :
    CObject (scene, model), CRenderable (scene, model, sectionMaterial (model.file->sections.front (), model.skin)),
    m_model (model) { }

CModel::~CModel () {
    for (const auto& section : this->m_sections) {
	if (section.vertexBuffer) {
	    glDeleteBuffers (1, &section.vertexBuffer);
	}
	if (section.indexBuffer) {
	    glDeleteBuffers (1, &section.indexBuffer);
	}
	if (section.vao) {
	    glDeleteVertexArrays (1, &section.vao);
	}
    }

    for (const auto& sectionPasses : this->m_passes) {
	for (const auto* pass : sectionPasses) {
	    delete pass;
	}
    }
}

const ModelObject& CModel::getModel () const { return this->m_model; }

bool CModel::samplesReflection () const {
    for (const auto& section : this->m_model.file->sections) {
	const Material& material = sectionMaterial (section, this->m_model.skin);

	for (const auto& pass : material.passes) {
	    for (const auto& [index, texture] : pass->textures) {
		if (texture == "_rt_Reflection") {
		    return true;
		}
	    }
	}
    }

    return false;
}

float CModel::getWorldOriginY () const { return this->resolveTransform (this->m_model).origin.y; }

const float& CModel::getBrightness () const {
    static const float brightness = 1.0f;
    return brightness;
}

const float& CModel::getUserAlpha () const {
    static const float alpha = 1.0f;
    return alpha;
}

const float& CModel::getAlpha () const {
    static const float alpha = 1.0f;
    return alpha;
}

const glm::vec3& CModel::getColor () const { return this->m_color; }

const glm::vec4& CModel::getColor4 () const { return this->m_color4; }

const glm::vec3& CModel::getCompositeColor () const { return this->m_color; }

void CModel::setup () {
    // CPass requires a base texture for its g_Texture0 chain, models sample
    // everything through their material passes so a 1x1 white placeholder is
    // enough here
    this->m_texture = std::make_shared<WhiteTexture> ();

    // skinned MDLV0023 meshes need bone palette support that is not implemented
    // yet; skip the whole model instead of uploading a mismatched vertex layout
    for (const auto& section : this->m_model.file->sections) {
	if (section.attrMask == MeshFormat_Skinned) {
	    sLog.error ("Skinned model ", this->m_model.file->version, " is not supported yet, skipping object");
	    return;
	}
    }

    CRenderable::setup ();
    this->setupSections ();

    // build one pass list per section, mirroring CImage's base material passes;
    // each section renders with the material of the object's selected skin
    this->m_passes.reserve (this->m_model.file->sections.size ());

    for (const auto& section : this->m_model.file->sections) {
	const Material& material = sectionMaterial (section, this->m_model.skin);
	std::vector<Effects::CPass*> sectionPasses = {};
	sectionPasses.reserve (material.passes.size ());

	for (const auto& cur : material.passes) {
	    auto* pass = new Effects::CPass (
		*this, std::make_shared<FBOProvider> (this), *cur, std::nullopt, std::nullopt, std::nullopt
	    );

	    // models draw straight into the scene framebuffer, CPass always
	    // needs a destination and an input though
	    pass->setDestination (this->getScene ().getFBO ());
	    pass->setInput (this->m_texture);

	    sectionPasses.push_back (pass);
	}

	this->m_passes.push_back (std::move (sectionPasses));
    }

    // opaque sections first and translucent ones after (stable), mirroring
    // WE's draw order so translucent geometry doesn't get occluded by opaque
    // sections that would otherwise render after it
    this->m_drawOrder.resize (this->m_model.file->sections.size ());
    std::iota (this->m_drawOrder.begin (), this->m_drawOrder.end (), 0);

    const auto isTranslucent = [this] (size_t index) {
	const Material& material = sectionMaterial (this->m_model.file->sections[index], this->m_model.skin);

	if (material.passes.empty ()) {
	    return false;
	}

	const auto blending = material.passes.front ()->blending;

	return blending == BlendingMode_Translucent || blending == BlendingMode_Additive;
    };

    std::stable_partition (this->m_drawOrder.begin (), this->m_drawOrder.end (), [&isTranslucent] (size_t index) {
	return !isTranslucent (index);
    });
}

void CModel::setupSections () {
    const size_t sectionCount = this->m_model.file->sections.size ();

    this->m_sections.reserve (sectionCount);

    for (const auto& section : this->m_model.file->sections) {
	const MeshVertexLayout layout = meshVertexLayout (section.attrMask);
	// When the output presents with a vertical flip, the perspective camera
	// also mirrors Y, which reverses triangle winding on screen. Reverse the
	// index order in that case so front faces stay front with backface
	// culling; X11 (no present-time flip) keeps the authored winding.
	std::vector<uint32_t> indices = section.indices;

	if (this->getScene ().getCamera ().isYFlipped ()) {
	    for (size_t t = 0; t + 2 < indices.size (); t += 3) {
		std::swap (indices[t + 1], indices[t + 2]);
	    }
	}

	SectionBuffers buffers = {
	    .vao = GL_NONE,
	    .vertexBuffer = GL_NONE,
	    .indexBuffer = GL_NONE,
	    .stride = layout.stride,
	    .indexCount = section.indices.size (),
	    .attrMask = section.attrMask,
	};

	glGenVertexArrays (1, &buffers.vao);
	glGenBuffers (1, &buffers.vertexBuffer);
	glGenBuffers (1, &buffers.indexBuffer);

	glBindVertexArray (buffers.vao);

	glBindBuffer (GL_ARRAY_BUFFER, buffers.vertexBuffer);
	glBufferData (
	    GL_ARRAY_BUFFER, section.vertices.size () * sizeof (float), section.vertices.data (), GL_STATIC_DRAW
	);

	glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, buffers.indexBuffer);
	glBufferData (GL_ELEMENT_ARRAY_BUFFER, indices.size () * sizeof (uint32_t), indices.data (), GL_STATIC_DRAW);

	glBindVertexArray (GL_NONE);

	this->m_sections.push_back (buffers);
    }
}

void CModel::updateModelMatrix () {
    // models need the full three axis rotation, resolveTransform folds the
    // parent chain but keeps a single z angle, so the local part is recomputed
    // here mirroring CParticle::updateMatrices
    const CObject::ResolvedTransform transform = this->resolveTransform (this->m_model);

    const glm::vec3 origin = transform.origin;
    const glm::vec3 scale = transform.scale;
    const glm::vec3 angles = this->m_model.groupAngles->value->getVec3 ();

    this->m_modelMatrix = glm::mat4 (1.0f);
    this->m_modelMatrix = glm::translate (this->m_modelMatrix, origin);
    // Negate X and Z rotations to account for Y-flipped coordinate system
    this->m_modelMatrix = glm::rotate (this->m_modelMatrix, -angles.z, glm::vec3 (0, 0, 1));
    this->m_modelMatrix = glm::rotate (this->m_modelMatrix, angles.y, glm::vec3 (0, 1, 0));
    this->m_modelMatrix = glm::rotate (this->m_modelMatrix, -angles.x, glm::vec3 (1, 0, 0));
    this->m_modelMatrix = glm::scale (this->m_modelMatrix, scale);
    // the reflection pass renders the world mirrored over the mirror plane
    // with the original camera, which is what a planar mirror shows
    this->m_modelMatrix = this->m_reflectionTransform * this->m_modelMatrix;

    this->m_viewProjectionMatrix
	= this->getScene ().getCamera ().getProjection () * this->getScene ().getCamera ().getLookAt ();
    this->m_mvpMatrix = this->m_viewProjectionMatrix * this->m_modelMatrix;
    this->m_mvpMatrixInverse = glm::inverse (this->m_mvpMatrix);
}

void CModel::render () {
    this->m_reflectionTransform = glm::mat4 (1.0f);
    this->render (this->getScene ().getFBO (), glm::mat4 (1.0f));
}

void CModel::render (const std::shared_ptr<const CFBO>& destination, const glm::mat4& mirror) {
    this->m_reflectionTransform = mirror;

    // point the passes at the requested framebuffer, the main pass keeps them
    // on the scene framebuffer so restore it after drawing
    for (const auto& sectionPasses : this->m_passes) {
	for (auto* pass : sectionPasses) {
	    pass->setDestination (destination);
	}
    }

    this->updateModelMatrix ();

    for (const size_t i : this->m_drawOrder) {
	const SectionBuffers& buffers = this->m_sections[i];

	glBindVertexArray (buffers.vao);

	for (auto* pass : this->m_passes[i]) {
	    pass->setModelMatrix (&this->m_modelMatrix);
	    pass->setViewProjectionMatrix (&this->m_viewProjectionMatrix);
	    pass->setModelViewProjectionMatrix (&this->m_mvpMatrix);
	    pass->setModelViewProjectionMatrixInverse (&this->m_mvpMatrixInverse);

	    pass->setGeometryCallback (
		[&buffers, pass] () {
		    const MeshVertexLayout layout = meshVertexLayout (buffers.attrMask);
		    const GLuint program = pass->getProgramID ();

		    // attribute pointers capture whatever buffer is bound at
		    // call time, make sure it's this section's vertex buffer
		    glBindBuffer (GL_ARRAY_BUFFER, buffers.vertexBuffer);

		    auto bindAttribute = [program, &layout] (const char* name, GLint size, uint32_t offsetFloats) {
			const GLint location = glGetAttribLocation (program, name);

			if (location < 0) {
			    return;
			}

			glEnableVertexAttribArray (location);
			glVertexAttribPointer (
			    location, size, GL_FLOAT, GL_FALSE, static_cast<GLsizei> (layout.stride * sizeof (float)),
			    reinterpret_cast<const void*> (static_cast<uintptr_t> (offsetFloats * sizeof (float)))
			);
		    };

		    bindAttribute ("a_Position", 3, layout.positionOffset);

		    if (layout.normalOffset >= 0) {
			bindAttribute ("a_Normal", 3, layout.normalOffset);
		    } else {
			const GLint location = glGetAttribLocation (program, "a_Normal");
			if (location >= 0) {
			    glVertexAttrib3f (location, 0.0f, 1.0f, 0.0f);
			}
		    }

		    if (layout.tangentOffset >= 0) {
			bindAttribute ("a_Tangent4", 4, layout.tangentOffset);
		    } else {
			const GLint location = glGetAttribLocation (program, "a_Tangent4");
			if (location >= 0) {
			    glVertexAttrib4f (location, 1.0f, 0.0f, 0.0f, 1.0f);
			}
		    }

		    // lightmap materials declare a vec4 texcoord that spans
		    // both uv channels packed together (generic.vert's
		    // LIGHTMAP path), plain materials use a vec2
		    const GLint texcoordVec4 = glGetAttribLocation (program, "a_TexCoordVec4");

		    if (texcoordVec4 >= 0) {
			// LIGHTMAP materials pack both uv channels into a vec4; without
			// the second channel only feed xy and let GL supply defaults.
			const GLint components = layout.lightmapUVOffset >= 0 ? 4 : 2;
			glEnableVertexAttribArray (texcoordVec4);
			glVertexAttribPointer (
			    texcoordVec4, components, GL_FLOAT, GL_FALSE,
			    static_cast<GLsizei> (layout.stride * sizeof (float)),
			    reinterpret_cast<const void*> (static_cast<uintptr_t> (layout.uvOffset * sizeof (float)))
			);
		    } else {
			bindAttribute ("a_TexCoord", 2, layout.uvOffset);
		    }

		    if (layout.lightmapUVOffset >= 0) {
			bindAttribute ("a_LightmapTexCoord", 2, layout.lightmapUVOffset);
		    }
		},
		[&buffers] () {
		    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, buffers.indexBuffer);
		    glDrawElements (GL_TRIANGLES, static_cast<GLsizei> (buffers.indexCount), GL_UNSIGNED_INT, nullptr);
		},
		[pass] () {
		    const GLuint program = pass->getProgramID ();
		    const char* names[] = { "a_Position", "a_Normal",       "a_Tangent4",
					    "a_TexCoord", "a_TexCoordVec4", "a_LightmapTexCoord" };

		    for (const char* name : names) {
			const GLint location = glGetAttribLocation (program, name);

			if (location >= 0) {
			    glDisableVertexAttribArray (location);
			}
		    }
		}
	    );

	    pass->render ();
	}
    }

    if (destination != this->getScene ().getFBO ()) {
	const auto sceneFBO = this->getScene ().getFBO ();

	for (const auto& sectionPasses : this->m_passes) {
	    for (auto* pass : sectionPasses) {
		pass->setDestination (sceneFBO);
	    }
	}
    }
}
