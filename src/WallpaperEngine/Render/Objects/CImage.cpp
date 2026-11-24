#include "CImage.h"

#include "CModel.h"
#include "CRenderable.h"
#include "CText.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "WallpaperEngine/Data/Model/DynamicValue.h"
#include "WallpaperEngine/Data/Model/Material.h"
#include "WallpaperEngine/Data/Model/Object.h"
#include "WallpaperEngine/Data/Model/UserSetting.h"
#include "WallpaperEngine/Data/Parsers/MaterialParser.h"
#include "WallpaperEngine/Data/Utils/BinaryReader.h"
#include "WallpaperEngine/Data/Utils/MemoryStream.h"
#include "WallpaperEngine/Data/Utils/ScopeGuard.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Render/Utils/CompositionLayer.h"

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render::Objects;
using namespace WallpaperEngine::Render::Objects::Effects;
using namespace WallpaperEngine::Data::Parsers;
using namespace WallpaperEngine::Data::Builders;
using namespace WallpaperEngine::Data::Utils;

namespace {
bool isMagentaNeonTint (const glm::vec3& color) { return color.r > 0.55f && color.g < 0.25f && color.b > 0.45f; }

std::optional<glm::vec3> findMagentaCompositeTint (const Image& image, const std::vector<int>& skippedEffectIds) {
    for (const auto& effect : image.effects) {
	if (std::find (skippedEffectIds.begin (), skippedEffectIds.end (), static_cast<int> (effect->id))
	    != skippedEffectIds.end ()) {
	    continue;
	}
	if (!effect->visible->value->getBool ()) {
	    continue;
	}

	for (const auto& passOverride : effect->passOverrides) {
	    const auto compositeCombo = passOverride->combos.find ("COMPOSITE");
	    if (compositeCombo == passOverride->combos.end () || compositeCombo->second != 2) {
		continue;
	    }

	    const auto compositeColor = passOverride->constants.find ("compositecolor");
	    if (compositeColor == passOverride->constants.end () || compositeColor->second == nullptr
		|| compositeColor->second->value == nullptr) {
		continue;
	    }

	    const auto tint = compositeColor->second->value->getVec3 ();
	    if (isMagentaNeonTint (tint)) {
		return tint;
	    }
	}
    }

    return std::nullopt;
}

struct PuppetMeshBlock {
    size_t headerOffset = 0;
    uint32_t vertexBytes = 0;
    uint32_t indexBytes = 0;
};

std::optional<PuppetMeshBlock> findPuppetMeshBlock (
    const BinaryReader& reader, size_t markerSize, size_t mdlsOffset, size_t meshHeaderSize, size_t vertexStride
) {
    for (size_t offset = markerSize; offset + meshHeaderSize + sizeof (uint32_t) < mdlsOffset; offset++) {
	reader.base ().seekg (static_cast<std::streamoff> (offset + sizeof (uint32_t)), std::ios::beg);
	const uint32_t candidateVertexBytes = reader.nextUInt32 ();
	const size_t verticesOffset = offset + meshHeaderSize;
	const size_t indexLengthOffset = verticesOffset + candidateVertexBytes;

	if (candidateVertexBytes == 0 || candidateVertexBytes % vertexStride != 0
	    || indexLengthOffset + sizeof (uint32_t) > mdlsOffset) {
	    continue;
	}

	reader.base ().seekg (static_cast<std::streamoff> (indexLengthOffset), std::ios::beg);
	const uint32_t candidateIndexBytes = reader.nextUInt32 ();
	const size_t indicesOffset = indexLengthOffset + sizeof (uint32_t);
	if (candidateIndexBytes == 0 || candidateIndexBytes % (sizeof (uint16_t) * 3) != 0
	    || indicesOffset + candidateIndexBytes > mdlsOffset) {
	    continue;
	}

	return PuppetMeshBlock { .headerOffset = offset,
				 .vertexBytes = candidateVertexBytes,
				 .indexBytes = candidateIndexBytes };
    }

    return std::nullopt;
}
}

CImage::CImage (Wallpapers::CScene& scene, const Image& image) :
    CObject (scene, image), CRenderable (scene, image, *image.model->material), ScriptableObject (scene, image),
    m_sceneSpacePosition (GL_NONE), m_copySpacePosition (GL_NONE), m_passSpacePosition (GL_NONE),
    m_texcoordCopy (GL_NONE), m_texcoordPass (GL_NONE), m_modelViewProjectionScreen (),
    m_modelViewProjectionPass (glm::mat4 (1.0)), m_modelViewProjectionCopy (), m_modelViewProjectionScreenInverse (),
    m_modelViewProjectionPassInverse (glm::inverse (m_modelViewProjectionPass)), m_modelViewProjectionCopyInverse (),
    m_modelMatrix (), m_viewProjectionMatrix (), m_image (image), m_pos (), m_initialized (false) {
    // replay texture-animation requests made by scripts during base construction
    this->attachRenderable (this);

    // register any properties in use on this object
    this->registerProperty ("origin", *image.origin->value);
    this->registerProperty ("scale", *image.scale->value);
    this->registerProperty ("angles", *image.angles->value);
    this->registerProperty ("visible", *image.visible->value);
    this->registerProperty ("alpha", *image.alpha->value);
    this->registerProperty ("color", *image.color->value);
    this->registerProperty ("parallaxDepth", *image.parallaxDepth->value);

    // Origin is registered by CObject. Images keep their own scale/angles
    // settings; the group copies on ObjectData are not used by localTransform.
    this->registerAnimations (
	{
	    image.scale.get (),
	    image.angles.get (),
	    image.color.get (),
	    image.alpha.get (),
	    image.parallaxDepth.get (),
	    image.brightness.get (),
	}
    );

    if (image.model != nullptr && image.model->material != nullptr) {
	this->registerMaterial (*image.model->material);
    }

    for (const auto& effect : image.effects) {
	for (const auto& pass : effect->passOverrides) {
	    this->registerShaderConstants (pass->constants);
	}

	if (effect->effect == nullptr) {
	    continue;
	}

	for (const auto& pass : effect->effect->passes) {
	    if (pass->material.has_value () && *pass->material != nullptr) {
		this->registerMaterial (**pass->material);
	    }
	}
    }

    this->registerEffectScripts ();

    // passes read g_Color4 through a pointer captured during setupUniforms, so it must hold
    // the composed value before setup() runs
    this->updateColor4 ();

    // get scene width and height to calculate positions
    auto scene_width = static_cast<float> (scene.getWidth ());
    auto scene_height = static_cast<float> (scene.getHeight ());

    const auto transform = this->resolveTransform (this->getImage ());
    glm::vec3 origin = transform.origin;
    glm::vec2 size = this->getSize ();
    glm::vec3 scale = transform.scale;

    this->detectTexture ();

    // A zero-sized solid layer is an event/transform parent (e.g. the "Camera
    // Flash + Audio" container in 3420380809), not a full-screen quad. WE does
    // not draw it; promoting it to the scene size paints an opaque white layer
    // over everything that was rendered before it.
    if (this->m_image.model->solidlayer && size.x == 0.0f && size.y == 0.0f && !this->m_image.model->fullscreen) {
	this->m_invisibleSolid = true;
	this->m_size = { 0.0f, 0.0f };
	this->m_texture = std::make_shared<CFBO> ("", TextureFormat_ARGB8888, TextureFlags_NoFlags, 1, 1, 1, 1, 1);
	return;
    }

    // detect texture (if any)
    if (this->m_texture == nullptr) {
	// if (this->m_image->isSolid ()) // layer receives cursor events:
	// https://docs.wallpaperengine.io/en/scene/scenescript/reference/event/cursor.html same applies to effects
	// TODO: create a dummy texture of correct size, fbo constructors should be enough, but this should be properly
	// handled
	this->m_texture = std::make_shared<CFBO> (
	    "", TextureFormat_ARGB8888, TextureFlags_NoFlags, 1, size.x, size.y, size.x, size.y
	);
    }

    // If the wallpaper doesn't specify a size, fall back to the model's declared dimensions
    // before the detected texture: the first material pass texture can be much smaller than
    // the image itself (e.g. deep_space's Background pass lists a 480x270 flow mask first)
    // while the model json declares the quad extent the wallpaper was authored with
    if ((size.x == 0.0f || size.y == 0.0f) && this->getImage ().model->width.has_value ()
	&& this->getImage ().model->height.has_value ()) {
	size.x = static_cast<float> (this->getImage ().model->width.value ());
	size.y = static_cast<float> (this->getImage ().model->height.value ());
    } else if ((size.x == 0.0f || size.y == 0.0f) && this->m_texture != nullptr) {
	size.x = static_cast<float> (this->m_texture->getRealWidth ());
	size.y = static_cast<float> (this->m_texture->getRealHeight ());
    }

    // fullscreen layers should use the whole projection's size
    // TODO: WHAT SHOULD AUTOSIZE DO?
    if (this->getImage ().model->fullscreen) {
	size = { scene_width, scene_height };
	origin = { scene_width / 2, scene_height / 2, 0 };

	// TODO: CHANGE ALIGNMENT TOO?
    }
    this->m_size = size;

    // Composition layers capture their direct children in local space so the
    // layer transform and effects apply to the composite, not to each child.
    this->resolveCompositionMode ();

    glm::vec2 scaledSize = size * glm::vec2 (scale);

    // calculate the center and shift from there
    this->m_pos.x = origin.x - (scaledSize.x / 2);
    this->m_pos.w = origin.y + (scaledSize.y / 2);
    this->m_pos.z = origin.x + (scaledSize.x / 2);
    this->m_pos.y = origin.y - (scaledSize.y / 2);

    if (this->getImage ().alignment.find ("top") != std::string::npos) {
	this->m_pos.y -= scaledSize.y / 2;
	this->m_pos.w -= scaledSize.y / 2;
    } else if (this->getImage ().alignment.find ("bottom") != std::string::npos) {
	this->m_pos.y += scaledSize.y / 2;
	this->m_pos.w += scaledSize.y / 2;
    }

    if (this->getImage ().alignment.find ("left") != std::string::npos) {
	this->m_pos.x += scaledSize.x / 2;
	this->m_pos.z += scaledSize.x / 2;
    } else if (this->getImage ().alignment.find ("right") != std::string::npos) {
	this->m_pos.x -= scaledSize.x / 2;
	this->m_pos.z -= scaledSize.x / 2;
    }

    // wallpaper engine
    this->m_pos.x -= scene_width / 2;
    this->m_pos.y = scene_height / 2 - this->m_pos.y;
    this->m_pos.z -= scene_width / 2;
    this->m_pos.w = scene_height / 2 - this->m_pos.w;

    // register both FBOs into the scene
    std::ostringstream nameA, nameB;

    // TODO: determine when _rt_imageLayerComposite and _rt_imageLayerAlbedo is used
    nameA << "_rt_imageLayerComposite_" << this->getImage ().id << "_a";
    nameB << "_rt_imageLayerComposite_" << this->getImage ().id << "_b";

    this->m_currentMainFBO = this->m_mainFBO = scene.create (
	nameA.str (), TextureFormat_ARGB8888, this->m_texture->getFlags (), 1, { size.x, size.y }, { size.x, size.y }
    );
    this->m_currentSubFBO = this->m_subFBO = scene.create (
	nameB.str (), TextureFormat_ARGB8888, this->m_texture->getFlags (), 1, { size.x, size.y }, { size.x, size.y }
    );

    if (this->m_compositionMode == CompositionMode::Capture && (size.x <= 0.0f || size.y <= 0.0f)) {
	this->m_compositionMode = CompositionMode::Normal;
    }

    if (this->m_compositionMode == CompositionMode::Capture) {
	// TODO(composition-capture): size the capture from the union of the
	// children's local bounds plus effect padding instead of the authored layer
	// rectangle, so children that overflow the layer keep rendering into it
	// (the tint-only skip path avoids that clipping today)
	std::ostringstream captureName;
	captureName << "_rt_compositionLayer_" << this->m_image.id;
	this->m_captureFBO = scene.create (
	    captureName.str (), TextureFormat_ARGB8888, TextureFlags_ClampUVs, 1, { size.x, size.y }, { size.x, size.y }
	);
	// The effect chain samples the captured children as its input texture.
	this->m_texture = this->m_captureFBO;
    }

    // build a list of vertices, these might need some change later (or maybe invert the camera)
    GLfloat sceneSpacePosition[] = { this->m_pos.x, this->m_pos.y, 0.0f, this->m_pos.x, this->m_pos.w, 0.0f,
				     this->m_pos.z, this->m_pos.y, 0.0f, this->m_pos.z, this->m_pos.y, 0.0f,
				     this->m_pos.x, this->m_pos.w, 0.0f, this->m_pos.z, this->m_pos.w, 0.0f };

    float width = 1.0f;
    float height = 1.0f;

    if (this->getTexture ()->isAnimated ()) {
	// animated images use different coordinates as they're essentially a texture atlas
	width = static_cast<float> (this->getTexture ()->getRealWidth ())
	    / static_cast<float> (this->getTexture ()->getTextureWidth (0));
	height = static_cast<float> (this->getTexture ()->getRealHeight ())
	    / static_cast<float> (this->getTexture ()->getTextureHeight (0));
    }
    // calculate the correct texCoord limits for the texture based on the texture screen size and real size
    else if (
	this->getTexture () != nullptr
	&& (this->getTexture ()->getTextureWidth (0) != this->getTexture ()->getRealWidth ()
	    || this->getTexture ()->getTextureHeight (0) != this->getTexture ()->getRealHeight ())
    ) {
	// Account for padding in non-power-of-two textures: clamp UVs to the real content
	width = static_cast<float> (this->getTexture ()->getRealWidth ())
	    / static_cast<float> (this->getTexture ()->getTextureWidth (0));
	height = static_cast<float> (this->getTexture ()->getRealHeight ())
	    / static_cast<float> (this->getTexture ()->getTextureHeight (0));
    }

    // TODO: RECALCULATE THESE POSITIONS FOR PASSTHROUGH SO THEY TAKE THE RIGHT PART OF THE TEXTURE
    float x = 0.0f;
    float y = 0.0f;

    if (this->getTexture ()->isAnimated ()) {
	// animations should be copied completely
	x = 0.0f;
	y = 0.0f;
	width = 1.0f;
	height = 1.0f;
    }

    GLfloat realWidth = size.x;
    GLfloat realHeight = size.y;
    GLfloat realX = 0.0;
    GLfloat realY = 0.0;

    if (this->getImage ().model->passthrough) {
	// Passthrough shaders fill the destination FBO from texcoords and sample the scene using positions.
	// Keep the destination quad full-screen in local FBO space, but pass scene-space positions through.
	x = 0.0f;
	y = 0.0f;
	width = 1.0f;
	height = 1.0f;
	realX = this->m_pos.x;
	realY = this->m_pos.w;
	realWidth = this->m_pos.z;
	realHeight = this->m_pos.y;

	if (this->getImage ().model->fullscreen) {
	    realX = -1.0;
	    realY = -1.0;
	    realWidth = 1.0;
	    realHeight = 1.0;
	}
    }

    GLfloat texcoordCopy[] = { x, height, x, y, width, height, width, height, x, y, width, y };

    GLfloat copySpacePosition[] = { realX,     realHeight, 0.0f, realX, realY, 0.0f, realWidth, realHeight, 0.0f,
				    realWidth, realHeight, 0.0f, realX, realY, 0.0f, realWidth, realY,      0.0f };

    GLfloat texcoordPass[] = { 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f };

    GLfloat passSpacePosition[]
	= { -1.0, 1.0, 0.0f, -1.0, -1.0, 0.0f, 1.0, 1.0, 0.0f, 1.0, 1.0, 0.0f, -1.0, -1.0, 0.0f, 1.0, -1.0, 0.0f };

    // bind vertex list to the openGL buffers
    glGenBuffers (1, &this->m_sceneSpacePosition);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_sceneSpacePosition);
    glBufferData (GL_ARRAY_BUFFER, sizeof (sceneSpacePosition), sceneSpacePosition, GL_STATIC_DRAW);

    glGenBuffers (1, &this->m_copySpacePosition);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_copySpacePosition);
    glBufferData (GL_ARRAY_BUFFER, sizeof (copySpacePosition), copySpacePosition, GL_STATIC_DRAW);

    // bind pass' vertex list to the openGL buffers
    glGenBuffers (1, &this->m_passSpacePosition);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_passSpacePosition);
    glBufferData (GL_ARRAY_BUFFER, sizeof (passSpacePosition), passSpacePosition, GL_STATIC_DRAW);

    glGenBuffers (1, &this->m_texcoordCopy);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_texcoordCopy);
    glBufferData (GL_ARRAY_BUFFER, sizeof (texcoordCopy), texcoordCopy, GL_STATIC_DRAW);

    glGenBuffers (1, &this->m_texcoordPass);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_texcoordPass);
    glBufferData (GL_ARRAY_BUFFER, sizeof (texcoordPass), texcoordPass, GL_STATIC_DRAW);

    this->m_hasPuppetMesh = this->loadPuppetMesh (size);

    // compute the center of the image in scene space for rotation
    this->m_sceneCenter
	= glm::vec3 ((this->m_pos.x + this->m_pos.z) / 2.0f, (this->m_pos.y + this->m_pos.w) / 2.0f, 0.0f);

    this->m_modelViewProjectionScreen
	= this->getScene ().getCamera ().getProjection () * this->getScene ().getCamera ().getLookAt ();

    if (this->getImage ().model->passthrough) {
	this->m_modelViewProjectionCopy = this->m_modelViewProjectionScreen;
    } else {
	this->m_modelViewProjectionCopy = glm::ortho<float> (0.0, size.x, 0.0, size.y);
    }
    this->m_modelViewProjectionCopyInverse = glm::inverse (this->m_modelViewProjectionCopy);
    this->m_modelMatrix = glm::ortho<float> (0.0, size.x, 0.0, size.y);
    this->m_viewProjectionMatrix = glm::mat4 (1.0);

    // ensure the input texture is marked as used
    // this makes video playback start if it's not already
    this->m_texture->incrementUsageCount ();
}

CImage::~CImage () {
    this->m_texture->decrementUsageCount ();

    // delete passes first as they depend on the image's data
    for (auto* pass : this->m_passes) {
	delete pass;
    }

    this->m_passes.clear ();

    // free any gl resources
    glDeleteBuffers (1, &this->m_sceneSpacePosition);
    glDeleteBuffers (1, &this->m_copySpacePosition);
    glDeleteBuffers (1, &this->m_passSpacePosition);
    glDeleteBuffers (1, &this->m_texcoordCopy);
    glDeleteBuffers (1, &this->m_texcoordPass);
    if (this->m_puppetSpacePosition != GL_NONE) {
	glDeleteBuffers (1, &this->m_puppetSpacePosition);
    }
    if (this->m_puppetTexCoord != GL_NONE) {
	glDeleteBuffers (1, &this->m_puppetTexCoord);
    }
    if (this->m_puppetIndices != GL_NONE) {
	glDeleteBuffers (1, &this->m_puppetIndices);
    }
}

void CImage::resolveCompositionMode () {
    if (!WallpaperEngine::Render::Utils::isCompositionLayer (this->m_image)) {
	return;
    }

    // Only homogeneous, capturable child sets are supported; sounds, lights
    // and cameras do not render and therefore do not disqualify a layer.
    bool hasTextChildren = false;
    bool hasModelChildren = false;
    bool hasImageChildren = false;
    bool hasUnsupportedRenderableChildren = false;

    for (const auto& child : this->getScene ().getScene ().objects) {
	if (!child->parent.has_value () || child->parent.value () != this->m_image.id) {
	    continue;
	}

	// renderable objects may not exist yet while constructors run, so cache
	// the ids here and resolve them in captureChildren ()
	this->m_compositionChildren.push_back (child->id);

	if (child->is<Text> ()) {
	    hasTextChildren = true;
	} else if (child->is<ModelObject> ()) {
	    hasModelChildren = true;
	} else if (child->is<Image> ()) {
	    hasImageChildren = true;
	} else if (child->is<Sound> () || child->is<LightObject> () || child->is<CameraObject> ()) {
	    // non-rendering objects do not affect the child composite
	} else {
	    // particles and unknown objects cannot be rendered into the capture
	    hasUnsupportedRenderableChildren = true;
	}
    }

    const bool homogeneousChildren
	= !hasUnsupportedRenderableChildren && (hasTextChildren + hasModelChildren + hasImageChildren) == 1;

    if (!homogeneousChildren) {
	return;
    }

    const bool hasVisibleEffects = WallpaperEngine::Render::Utils::hasVisibleEffects (this->m_image);
    const bool tintOnly = WallpaperEngine::Render::Utils::isTintOnlyCompositionLayer (this->m_image);

    if (hasTextChildren) {
	// A text-only tint layer can keep rendering its children normally: CText
	// applies the ancestor tint itself, which avoids clipping glyphs that
	// overflow the authored layer rectangle. Layers that copy the background
	// still need the normal screen-copy path so the background is tinted too.
	// A color-blending pass still needs a capture because it has to run on the
	// children, not on the scene behind them.
	if (tintOnly) {
	    if (!this->m_image.copybackground) {
		this->m_compositionMode = this->m_image.colorBlendMode->value->getInt () > 0
		    ? CompositionMode::Capture
		    : CompositionMode::SkipLayer;
	    }
	} else if (hasVisibleEffects) {
	    this->m_compositionMode = CompositionMode::Capture;
	}
    } else if (hasModelChildren) {
	if (hasVisibleEffects) {
	    this->m_compositionMode = CompositionMode::Capture;
	}
    } else if (hasImageChildren) {
	// Image/puppet capture is not implemented yet. Keep the children visible
	// and skip the composition layer instead of copying the whole background
	// through its shake/blur chain.
	this->m_compositionMode = CompositionMode::SkipLayer;
    }
}

void CImage::captureChildren () {
    if (this->m_captureFBO == nullptr) {
	return;
    }

    GLint previousFramebuffer = 0;
    GLint previousViewport[4] = {};
    GLboolean previousColorMask[4] = {};
    GLfloat previousClearColor[4] = {};
    glGetIntegerv (GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv (GL_VIEWPORT, previousViewport);
    glGetBooleanv (GL_COLOR_WRITEMASK, previousColorMask);
    glGetFloatv (GL_COLOR_CLEAR_VALUE, previousClearColor);

    ScopeGuard restoreState ([&] {
	glBindFramebuffer (GL_FRAMEBUFFER, previousFramebuffer);
	glViewport (previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
	glColorMask (previousColorMask[0], previousColorMask[1], previousColorMask[2], previousColorMask[3]);
	glClearColor (previousClearColor[0], previousClearColor[1], previousClearColor[2], previousClearColor[3]);
    });

    glBindFramebuffer (GL_FRAMEBUFFER, this->m_captureFBO->getFramebuffer ());
    glViewport (
	0, 0, static_cast<GLsizei> (this->m_captureFBO->getRealWidth ()),
	static_cast<GLsizei> (this->m_captureFBO->getRealHeight ())
    );
    // A previous image pass may have disabled alpha writes; the captured content
    // needs its alpha for the composition/effect chain.
    glColorMask (true, true, true, true);

    glClearColor (0.0f, 0.0f, 0.0f, 0.0f);
    glClear (GL_COLOR_BUFFER_BIT);

    // every renderable exists once the scene is rendering; children without
    // one (sounds, disabled particles, ...) are skipped by the null check
    for (const int childId : this->m_compositionChildren) {
	auto* renderChild = this->getScene ().getObject (childId);

	if (renderChild == nullptr) {
	    continue;
	}

	if (renderChild->is<CText> ()) {
	    renderChild->as<CText> ()->renderLocal (this->m_size.x, this->m_size.y);
	} else if (renderChild->is<CModel> ()) {
	    renderChild->as<CModel> ()->renderLocal (this->m_captureFBO, this->m_size.x, this->m_size.y);
	}
    }
}

bool CImage::loadPuppetMesh (const glm::vec2& size) {
    if (!this->getImage ().model->puppet.has_value ()) {
	return false;
    }

    try {
	const auto stream = this->getScene ().getScene ().project.assetLocator->read (*this->getImage ().model->puppet);
	std::vector<char> data { std::istreambuf_iterator<char> (*stream), std::istreambuf_iterator<char> () };

	constexpr size_t markerSize = 9;
	constexpr size_t meshHeaderSize = sizeof (uint32_t) * 2;
	constexpr size_t vertexStride = 80;
	constexpr size_t positionOffset = 0;
	constexpr size_t uvOffset = 72;

	const std::string puppetVersion
	    = data.size () >= markerSize ? std::string (data.data (), strlen ("MDLV0021")) : "";
	if (puppetVersion != "MDLV0021" && puppetVersion != "MDLV0023") {
	    sLog.error ("Unsupported puppet model header ", puppetVersion, " in ", *this->getImage ().model->puppet);
	    return false;
	}

	const size_t mdlsOffset = [&data] () -> size_t {
	    for (size_t offset = markerSize; offset + strlen ("MDLS") < data.size (); offset++) {
		if (std::memcmp (data.data () + offset, "MDLS", strlen ("MDLS")) == 0) {
		    return offset;
		}
	    }
	    return data.size ();
	}();

	auto meshBuffer = std::make_unique<char[]> (data.size ());
	std::copy (data.begin (), data.end (), meshBuffer.get ());
	const BinaryReader reader (std::make_shared<MemoryStream> (std::move (meshBuffer), data.size ()));
	const auto meshBlock = findPuppetMeshBlock (reader, markerSize, mdlsOffset, meshHeaderSize, vertexStride);
	if (!meshBlock.has_value ()) {
	    sLog.error ("Could not find a usable MDLV mesh block in ", *this->getImage ().model->puppet);
	    return false;
	}

	const size_t vertexCount = meshBlock->vertexBytes / vertexStride;
	const size_t verticesOffset = meshBlock->headerOffset + meshHeaderSize;
	const size_t indicesOffset = verticesOffset + meshBlock->vertexBytes + sizeof (uint32_t);
	const size_t indexCount = meshBlock->indexBytes / sizeof (uint16_t);
	std::vector<GLfloat> texcoords;
	std::vector<GLushort> indices;

	this->m_puppetRawPositions.clear ();
	this->m_puppetRawPositions.reserve (vertexCount * 3);
	texcoords.reserve (vertexCount * 2);
	indices.reserve (indexCount);

	for (size_t index = 0; index < vertexCount; index++) {
	    const size_t vertexOffset = verticesOffset + index * vertexStride;
	    reader.base ().seekg (static_cast<std::streamoff> (vertexOffset + positionOffset), std::ios::beg);
	    const float x = reader.nextFloat ();
	    const float y = reader.nextFloat ();
	    const float z = reader.nextFloat ();
	    reader.base ().seekg (static_cast<std::streamoff> (vertexOffset + uvOffset), std::ios::beg);
	    const float u = reader.nextFloat ();
	    const float v = reader.nextFloat ();

	    this->m_puppetRawPositions.push_back (x);
	    this->m_puppetRawPositions.push_back (y);
	    this->m_puppetRawPositions.push_back (z);
	    texcoords.push_back (u);
	    texcoords.push_back (v);
	}

	reader.base ().seekg (static_cast<std::streamoff> (indicesOffset), std::ios::beg);
	for (size_t index = 0; index < indexCount; index++) {
	    uint16_t value = 0;
	    reader.next (reinterpret_cast<char*> (&value), sizeof (value));
	    if (value >= vertexCount) {
		sLog.error ("Invalid puppet mesh index ", value, " in ", *this->getImage ().model->puppet);
		return false;
	    }
	    indices.push_back (value);
	}

	this->updatePuppetPositionBuffer (size);

	glGenBuffers (1, &this->m_puppetTexCoord);
	glBindBuffer (GL_ARRAY_BUFFER, this->m_puppetTexCoord);
	glBufferData (GL_ARRAY_BUFFER, texcoords.size () * sizeof (GLfloat), texcoords.data (), GL_STATIC_DRAW);

	glGenBuffers (1, &this->m_puppetIndices);
	glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, this->m_puppetIndices);
	glBufferData (GL_ELEMENT_ARRAY_BUFFER, indices.size () * sizeof (GLushort), indices.data (), GL_STATIC_DRAW);

	this->m_puppetIndexCount = static_cast<GLsizei> (indices.size ());
	sLog.out (
	    "Loaded puppet mesh ", *this->getImage ().model->puppet, " version=", puppetVersion,
	    " vertices=", vertexCount, " indices=", this->m_puppetIndexCount
	);

	return true;
    } catch (const std::exception& ex) {
	sLog.error ("Could not load puppet mesh ", *this->getImage ().model->puppet, ": ", ex.what ());
	return false;
    }
}

void CImage::updatePuppetPositionBuffer (const glm::vec2& size) {
    if (this->m_puppetRawPositions.empty ()) {
	return;
    }

    std::vector<GLfloat> positions;
    positions.reserve (this->m_puppetRawPositions.size ());
    for (size_t index = 0; index + 2 < this->m_puppetRawPositions.size (); index += 3) {
	positions.push_back (size.x / 2.0f + this->m_puppetRawPositions[index]);
	positions.push_back (size.y / 2.0f - this->m_puppetRawPositions[index + 1]);
	positions.push_back (this->m_puppetRawPositions[index + 2]);
    }

    if (this->m_puppetSpacePosition == GL_NONE) {
	glGenBuffers (1, &this->m_puppetSpacePosition);
    }
    glBindBuffer (GL_ARRAY_BUFFER, this->m_puppetSpacePosition);
    glBufferData (GL_ARRAY_BUFFER, positions.size () * sizeof (GLfloat), positions.data (), GL_DYNAMIC_DRAW);
}

void CImage::setupPuppetGeometryCallback (Effects::CPass* pass) const {
    pass->setGeometryCallback (
	[this, pass] () {
	    const GLint position = glGetAttribLocation (pass->getProgramID (), "a_Position");
	    const GLint texCoord = glGetAttribLocation (pass->getProgramID (), "a_TexCoord");

	    if (position >= 0) {
		glEnableVertexAttribArray (position);
		glBindBuffer (GL_ARRAY_BUFFER, this->m_puppetSpacePosition);
		glVertexAttribPointer (position, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
	    }

	    if (texCoord >= 0) {
		glEnableVertexAttribArray (texCoord);
		glBindBuffer (GL_ARRAY_BUFFER, this->m_puppetTexCoord);
		glVertexAttribPointer (texCoord, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
	    }
	},
	[this] () {
	    GLint currentFramebuffer = 0;
	    glGetIntegerv (GL_DRAW_FRAMEBUFFER_BINDING, &currentFramebuffer);
	    if (currentFramebuffer != static_cast<GLint> (this->getScene ().getFBO ()->getFramebuffer ())) {
		GLfloat previousClearColor[4] = {};
		glGetFloatv (GL_COLOR_CLEAR_VALUE, previousClearColor);
		glClearColor (0.0f, 0.0f, 0.0f, 0.0f);
		glClear (GL_COLOR_BUFFER_BIT);
		glClearColor (
		    previousClearColor[0], previousClearColor[1], previousClearColor[2], previousClearColor[3]
		);
	    }
	    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, this->m_puppetIndices);
	    glDrawElements (GL_TRIANGLES, this->m_puppetIndexCount, GL_UNSIGNED_SHORT, nullptr);
	},
	[pass] () {
	    const GLint position = glGetAttribLocation (pass->getProgramID (), "a_Position");
	    const GLint texCoord = glGetAttribLocation (pass->getProgramID (), "a_TexCoord");

	    if (position >= 0) {
		glDisableVertexAttribArray (position);
	    }

	    if (texCoord >= 0) {
		glDisableVertexAttribArray (texCoord);
	    }
	}
    );
}

void CImage::setup () {
    // do not double-init stuff, that's bad!
    if (this->m_initialized) {
	return;
    }

    if (this->m_invisibleSolid) {
	return;
    }

    // TODO: CHECK ORDER OF THINGS, 2419444134'S ID 27 DEPENDS ON 104'S COMPOSITE_A WHEN OUR LAST RENDER IS ON
    // COMPOSITE_B
    // TODO: SUPPORT PASSTHROUGH (IT'S A SHADER)

    // Layers that render nothing still need a transform so cursor hit-testing
    // and layer scripts work: skip-mode composition layers and passthrough
    // images without any visible effect.
    if (this->m_compositionMode == CompositionMode::SkipLayer
	|| (this->m_image.model->passthrough && !WallpaperEngine::Render::Utils::hasVisibleEffects (this->m_image))) {
	this->updateScreenSpacePosition ();
	this->m_initialized = true;
	return;
    }

    const auto& debug = this->getScene ().getContext ().getApp ().getContext ().settings.render.debug;
    const bool captureChildren = this->m_compositionMode == CompositionMode::Capture;

    // Base passes sample the scene framebuffer. A capture layer starts its
    // effect chain from the captured children instead, so those passes are not
    // built at all. Keep their blending mode for the final composite.
    std::optional<BlendingMode> compositionBlendMode = std::nullopt;

    if (captureChildren && !this->getImage ().model->material->passes.empty ()) {
	compositionBlendMode = this->getImage ().model->material->passes.front ()->blending;
    }

    if (!captureChildren) {
	for (const auto& cur : this->getImage ().model->material->passes) {
	    this->m_passes.push_back (
		new CPass (*this, std::make_shared<FBOProvider> (this), *cur, std::nullopt, std::nullopt, std::nullopt)
	    );
	}
    }

    // prepare the passes list
    if (!debug.baseOnly && !this->getImage ().effects.empty ()) {
	// generate the effects used by this material
	for (const auto& cur : this->m_image.effects) {
	    if (std::find (debug.skipEffects.begin (), debug.skipEffects.end (), static_cast<int> (cur->id))
		!= debug.skipEffects.end ()) {
		continue;
	    }

	    // do not add non-visible effects, this might need some adjustements tho as some effects might not be
	    // visible but affect the output of the image...
	    if (!cur->visible->value->getBool ()) {
		continue;
	    }

	    const auto fboProvider = std::make_shared<FBOProvider> (this);

	    // create all the fbos for this effect
	    for (const auto& fbo : cur->effect->fbos) {
		fboProvider->create (*fbo, this->m_texture->getFlags (), this->getSize ());
	    }

	    // TODO: MAKE USE OF ZIP OPERATOR IN BOOST? WAY OVERKILL JUST FOR THIS...

	    auto curEffect = cur->effect->passes.begin ();
	    auto endEffect = cur->effect->passes.end ();
	    auto curOverride = cur->passOverrides.begin ();
	    auto endOverride = cur->passOverrides.end ();

	    for (; curEffect != endEffect; ++curEffect) {
		if (!(*curEffect)->material.has_value ()) {
		    if (!(*curEffect)->command.has_value ()) {
			sLog.error ("Pass without material and command not supported");
			continue;
		    }

		    if (!(*curEffect)->source.has_value ()) {
			sLog.error ("Pass without material and source not supported");
			continue;
		    }

		    if (!(*curEffect)->target.has_value ()) {
			sLog.error ("Pass without material and target not supported");
			continue;
		    }

		    if ((*curEffect)->command != Command_Copy) {
			sLog.error ("Only copy command is supported for pass without material");
			continue;
		    }

		    auto virtualPass
			= std::make_unique<MaterialPass> (MaterialPass { .blending = BlendingMode_Normal,
									 .cullmode = CullingMode_Disable,
									 .depthtest = DepthtestMode_Disabled,
									 .depthwrite = DepthwriteMode_Disabled,
									 .shader = "commands/copy",
									 .textures = { { 0, *(*curEffect)->source } },
									 .combos = {},
									 .constants = {} });

		    const auto& config = *this->m_virtualPassess.emplace_back (std::move (virtualPass));

		    // build a pass for a copy shader
		    this->m_passes.push_back (new CPass (
			*this, fboProvider, config, std::nullopt, std::nullopt, (*curEffect)->target.value ()
		    ));
		} else {
		    for (auto& pass : (*curEffect)->material.value ()->passes) {
			const auto override = curOverride != endOverride
			    ? **curOverride
			    : std::optional<std::reference_wrapper<const ImageEffectPassOverride>> (std::nullopt);
			const auto target = (*curEffect)->target.has_value ()
			    ? *(*curEffect)->target
			    : std::optional<std::reference_wrapper<std::string>> (std::nullopt);

			this->m_passes.push_back (
			    new CPass (*this, fboProvider, *pass, override, (*curEffect)->binds, target)
			);
		    }

		    if (curOverride != endOverride) {
			++curOverride;
		    }
		}
	    }
	}
    }

    if (!debug.baseOnly) {
	const auto magentaCompositeTint = findMagentaCompositeTint (this->m_image, debug.skipEffects);
	if (magentaCompositeTint.has_value ()) {
	    auto tintOverride = std::make_unique<ImageEffectPassOverride> (ImageEffectPassOverride {
		.id = -1,
		.combos = {
		    { "BLENDMODE", 30 },
		},
		.constants = {},
		.textures = {},
	    });
	    tintOverride->constants.emplace ("color", UserSettingBuilder::fromValue (magentaCompositeTint.value ()));
	    tintOverride->constants.emplace ("alpha", UserSettingBuilder::fromValue (1.0f));

	    this->m_materials.compatibilityMaterials.emplace_back (
		MaterialParser::load (this->getScene ().getScene ().project, "materials/effects/tint.json")
	    );
	    this->m_materials.compatibilityOverrides.emplace_back (std::move (tintOverride));

	    this->m_passes.push_back (new CPass (
		*this, std::make_shared<FBOProvider> (this),
		**this->m_materials.compatibilityMaterials.back ()->passes.begin (),
		*this->m_materials.compatibilityOverrides.back (), std::nullopt, std::nullopt
	    ));
	}
    }

    // extra render pass if there's any blending to be done
    if (!debug.baseOnly && this->m_image.colorBlendMode->value->getInt () > 0) {
	this->m_materials.colorBlending.material
	    = MaterialParser::load (this->getScene ().getScene ().project, "materials/util/effectpassthrough.json");
	this->m_materials.colorBlending.override = std::make_unique<ImageEffectPassOverride> (ImageEffectPassOverride {
            .id = -1,
            .combos = {
                {"BLENDMODE", this->m_image.colorBlendMode->value->getInt()},
            },
            .constants = {},
            .textures = {},
        });

	// composition pass: its input FBO already has the object's
	// color/brightness/alpha applied by the base pass, g_Color4 stays identity
	this->m_passes.push_back (new CPass (
	    *this, std::make_shared<FBOProvider> (this), **this->m_materials.colorBlending.material->passes.begin (),
	    *this->m_materials.colorBlending.override, std::nullopt, std::nullopt, true
	));
    }

    if (captureChildren) {
	// The captured children are already premultiplied by the child passes;
	// only the final composite needs the composition layer's blend mode.
	if (compositionBlendMode.has_value () && !this->m_passes.empty ()) {
	    this->m_passes.back ()->setBlendingMode (compositionBlendMode.value ());
	}
    } else if (this->m_passes.size () > 1) {
	// if there's more than one pass the blendmode has to be moved from the beginning to the end
	const auto first = this->m_passes.begin ();
	const auto last = this->m_passes.rbegin ();

	(*last)->setBlendingMode ((*first)->getBlendingMode ());
	(*first)->setBlendingMode (BlendingMode_Normal);
    }

    CRenderable::setup ();

    if (captureChildren) {
	this->m_captureReady = this->m_captureFBO != nullptr && !this->m_passes.empty ();

	if (this->m_captureReady) {
	    // The first pass of the effect chain has no scene-copy input of its
	    // own; feed it the captured children instead.
	    this->m_passes.front ()->setTextureOverride (0, this->m_captureFBO);
	}
    }

    this->setupPasses ();
    // initialise the local/clip transform so the first hit test is accurate
    this->updateScreenSpacePosition ();
    this->m_initialized = true;
}

void CImage::setupPasses () {
    // do a pass on everything and setup proper inputs and values
    std::shared_ptr<const CFBO> drawTo = this->m_currentMainFBO;
    std::shared_ptr<const TextureProvider> asInput = this->getTexture ();
    GLuint texcoord = this->getTexCoordCopy ();

    auto cur = this->m_passes.begin ();
    auto end = this->m_passes.end ();
    bool first = true;
    bool inTargetEffectSequence = false;
    std::shared_ptr<const TextureProvider> effectInput = nullptr;

    for (; cur != end; ++cur) {
	// TODO: PROPERLY CHECK EFFECT'S VISIBILITY AND TAKE IT INTO ACCOUNT
	// TODO: THIS REQUIRES ON-THE-FLY EVALUATION OF EFFECTS VISIBILITY TO FIGURE OUT
	// TODO: WHICH ONE IS THE LAST + A FEW OTHER THINGS
	Effects::CPass* pass = *cur;
	std::shared_ptr<const CFBO> prevDrawTo = drawTo;
	bool writesToTarget = false;
	const bool isFirstPass = first;
	GLuint spacePosition = (isFirstPass)
	    ? (this->m_hasPuppetMesh ? this->m_puppetSpacePosition : this->getCopySpacePosition ())
	    : this->getPassSpacePosition ();
	const glm::mat4* projection
	    = (isFirstPass) ? &this->m_modelViewProjectionCopy : &this->m_modelViewProjectionPass;
	const glm::mat4* inverseProjection
	    = (isFirstPass) ? &this->m_modelViewProjectionCopyInverse : &this->m_modelViewProjectionPassInverse;
	first = false;

	if (isFirstPass && this->m_hasPuppetMesh) {
	    pass->setBlendingMode (BlendingMode_Translucent);
	    this->setupPuppetGeometryCallback (pass);
	}

	if (isFirstPass && this->capturesChildren ()) {
	    // Composition captures start from a local texture, so the first pass
	    // must fill the local framebuffer with the clip-space quad. The
	    // passthrough copy geometry is scene-space and would sample/draw the
	    // wrong region when the effect chain has more than one pass. The
	    // final scene pass below still switches back to the scene quad.
	    spacePosition = this->getPassSpacePosition ();
	    projection = &this->m_modelViewProjectionPass;
	    inverseProjection = &this->m_modelViewProjectionPassInverse;
	}

	pass->setModelMatrix (&this->m_modelMatrix);
	pass->setViewProjectionMatrix (&this->m_viewProjectionMatrix);

	writesToTarget = this->configurePassTarget (pass, drawTo, asInput, effectInput, inTargetEffectSequence);
	// determine if it's the last element in the list as this is a screen-copy-like process
	// TODO: PROPERLY CHECK IF THIS IS ALL THAT'S NEEDED
	if (!writesToTarget && this->shouldRenderFinalPass (std::next (cur) == end)) {
	    // TODO: PROPERLY CHECK EFFECT'S VISIBILITY AND TAKE IT INTO ACCOUNT
	    drawTo = this->getScene ().getFBO ();

	    // fullscreen layers are post-processing: their composite must cover the
	    // whole framebuffer, so draw the clip-space quad with an identity
	    // projection. A perspective camera (model scenes, camera paths) would
	    // otherwise project the z=0 scene quad mostly off-screen and black the
	    // wallpaper out
	    if (this->getImage ().model->fullscreen) {
		spacePosition = this->getPassSpacePosition ();
		projection = &this->m_modelViewProjectionPass;
		inverseProjection = &this->m_modelViewProjectionPassInverse;
	    } else {
		spacePosition = this->getSceneSpacePosition ();
		// Puppet layers without an effect chain of their own hit this path
		// with their first (base) pass. Their geometry callback still feeds
		// local-layer mesh vertices, which the scene-space screen MVP would
		// draw 1:1 at raw mesh coordinates; localToScreen carries the layer
		// placement (resolveTransform) and camera, mapping the mesh onto the
		// scene-space quad, matching the transform used by the multi-pass
		// composite path.
		if (this->m_hasPuppetMesh && isFirstPass) {
		    projection = &this->m_localToScreen;
		    inverseProjection = &this->m_screenToLocal;
		} else {
		    projection = &this->m_modelViewProjectionScreen;
		    inverseProjection = &this->m_modelViewProjectionScreenInverse;
		}
	    }
	}

	pass->setDestination (drawTo);
	pass->setInput (asInput);
	pass->setPreviousInput (inTargetEffectSequence ? effectInput : nullptr);
	pass->setPosition (spacePosition);
	pass->setTexCoord (texcoord);
	pass->setModelViewProjectionMatrix (projection);
	pass->setModelViewProjectionMatrixInverse (inverseProjection);

	texcoord = this->getTexCoordPass ();

	if (writesToTarget) {
	    asInput = drawTo;
	    drawTo = prevDrawTo;
	} else {
	    drawTo = prevDrawTo;
	    this->pinpongFramebuffer (&drawTo, &asInput);
	    inTargetEffectSequence = false;
	    effectInput = nullptr;
	}
    }
}

bool CImage::shouldRenderFinalPass (bool isLastPass) const {
    // Visibility can change at runtime; render() already skips hidden layers.
    if (!isLastPass) {
	return false;
    }

    const auto& debug = this->getScene ().getContext ().getApp ().getContext ().settings.render.debug;
    return !(debug.noSolidFinal && this->getImage ().model->solidlayer);
}

bool CImage::configurePassTarget (
    Effects::CPass* pass, std::shared_ptr<const CFBO>& drawTo, const std::shared_ptr<const TextureProvider>& asInput,
    std::shared_ptr<const TextureProvider>& effectInput, bool& inTargetEffectSequence
) {
    if (!pass->getTarget ().has_value ()) {
	return false;
    }

    const std::string target = pass->getTarget ().value ();
    std::shared_ptr<const CFBO> resolved = pass->getFBOProvider ()->find (target);
    if (resolved == nullptr) {
	resolved = this->getScene ().findFBO (target);
    }
    if (resolved == nullptr) {
	sLog.error (
	    "Pass target FBO '", target, "' could not be resolved for object ", pass->getRenderable ().getId (),
	    " shader=", pass->getPass ().shader
	);
	return false;
    }

    if (!inTargetEffectSequence) {
	effectInput = asInput;
	inTargetEffectSequence = true;
    }
    drawTo = resolved;
    return true;
}

void CImage::pinpongFramebuffer (std::shared_ptr<const CFBO>* drawTo, std::shared_ptr<const TextureProvider>* asInput) {
    // temporarily store FBOs used
    std::shared_ptr<const CFBO> currentMainFBO = this->m_currentMainFBO;
    std::shared_ptr<const CFBO> currentSubFBO = this->m_currentSubFBO;

    if (drawTo != nullptr) {
	*drawTo = currentSubFBO;
    }
    if (asInput != nullptr) {
	*asInput = currentMainFBO;
    }

    // swap the FBOs
    this->m_currentMainFBO = currentSubFBO;
    this->m_currentSubFBO = currentMainFBO;
}

void CImage::render () {
    // do not try to render something that did not initialize successfully
    if (!this->m_initialized || this->m_compositionMode == CompositionMode::SkipLayer) {
	return;
    }

    if (!this->getImage ().visible->value->getBool ()) {
	return;
    }

    this->updateColor4 ();

    if (this->m_captureReady) {
	this->captureChildren ();
    }

    glColorMask (true, true, true, true);

    // Always update screen transform (handles rotation + parallax dynamically)
    this->updateScreenSpacePosition ();

#if !NDEBUG
    std::string str = "Image ";

    if (this->getScene ().getScene ().camera.bloom.enabled->value->getBool () && this->getId () == -1) {
	str += "bloom";
    } else {
	str += this->getImage ().name + " (" + std::to_string (this->getId ()) + ", "
	    + this->getImage ().model->material->filename + ")";
    }

    glPushDebugGroup (GL_DEBUG_SOURCE_APPLICATION, 0, -1, str.c_str ());
#endif /* DEBUG */

    auto cur = this->m_passes.begin ();

    for (const auto end = this->m_passes.end (); cur != end; ++cur) {
	if (std::next (cur) == end) {
	    glColorMask (true, true, true, false);
	}

	(*cur)->render ();
    }

#if !NDEBUG
    glPopDebugGroup ();
#endif /* DEBUG */
}

const float& CImage::getBrightness () const { return this->m_image.brightness->value->getFloat (); }

const float& CImage::getUserAlpha () const { return this->m_image.alpha->value->getFloat (); }

const float& CImage::getAlpha () const { return this->m_image.alpha->value->getFloat (); }

const glm::vec3& CImage::getColor () const { return this->m_image.color->value->getVec3 (); }

const glm::vec4& CImage::getColor4 () const { return this->m_color4; }

void CImage::registerEffectScripts () {
    std::set<const DynamicValue*> registered = {};

    for (size_t effectIndex = 0; effectIndex < this->m_image.effects.size (); effectIndex++) {
	const auto& effect = this->m_image.effects[effectIndex];

	if (effect == nullptr) {
	    continue;
	}

	for (size_t passIndex = 0; passIndex < effect->passOverrides.size (); passIndex++) {
	    const auto& pass = effect->passOverrides[passIndex];

	    if (pass == nullptr) {
		continue;
	    }

	    for (const auto& [name, setting] : pass->constants) {
		if (setting == nullptr || setting->value == nullptr) {
		    continue;
		}

		if (!setting->value->getScriptSource ().has_value ()) {
		    continue;
		}

		// A single pass override can be shared by multiple effects in the
		// same layer; never queue the same DynamicValue more than once.
		if (!registered.insert (setting->value.get ()).second) {
		    continue;
		}

		const std::string key = "effect_" + std::to_string (this->getId ()) + "_" + std::to_string (effectIndex)
		    + "_" + std::to_string (passIndex) + "_" + name;

		this->registerValueScript (key, *setting->value);
	    }
	}
    }
}

void CImage::updateColor4 () {
    // when the material sets the VERSION combo, genericimage2 swaps g_Brightness/g_UserAlpha for a single
    // g_Color4, and genericimage3/4 only ever had g_Color4: fold both settings in or they silently stop
    // applying to those passes
    this->m_color4 = glm::vec4 { this->m_image.color->value->getVec3 () * this->m_image.brightness->value->getFloat (),
				 this->m_image.color->value->getVec4 ().a * this->m_image.alpha->value->getFloat () };
}

const glm::vec3& CImage::getCompositeColor () const { return this->m_image.color->value->getVec3 (); }

glm::vec2 CImage::resolveGeometrySize (float sceneWidth, float sceneHeight, glm::vec3& origin) const {
    // Passthrough materials (composelayer family) bind _rt_FullFrameBuffer as texture 0, so the
    // texture-based getSize () below resolves to the scene resolution instead of the object's own
    // extent and the per-frame quad ends up scene-size * scale. The declared size is authoritative
    // here, mirroring the constructor, which runs before detectTexture () and therefore always
    // seeded m_pos from image.size.
    glm::vec2 size = this->getImage ().model->passthrough ? this->getImage ().size : this->getSize ();

    if ((size.x == 0.0f || size.y == 0.0f) && this->m_texture != nullptr) {
	size.x = static_cast<float> (this->m_texture->getRealWidth ());
	size.y = static_cast<float> (this->m_texture->getRealHeight ());
    } else if (
	(size.x == 0.0f || size.y == 0.0f) && this->getImage ().model->width.has_value ()
	&& this->getImage ().model->height.has_value ()
    ) {
	size.x = static_cast<float> (this->getImage ().model->width.value ());
	size.y = static_cast<float> (this->getImage ().model->height.value ());
    }

    if (this->getImage ().model->fullscreen) {
	size = { sceneWidth, sceneHeight };
	origin = { sceneWidth / 2.0f, sceneHeight / 2.0f, 0.0f };
    }

    return size;
}

void CImage::updateScenePosition (
    const glm::vec3& origin, const glm::vec2& size, const glm::vec3& scale, float sceneWidth, float sceneHeight
) {
    const glm::vec2 scaledSize = size * glm::vec2 (scale);
    this->m_pos.x = origin.x - (scaledSize.x / 2.0f);
    this->m_pos.w = origin.y + (scaledSize.y / 2.0f);
    this->m_pos.z = origin.x + (scaledSize.x / 2.0f);
    this->m_pos.y = origin.y - (scaledSize.y / 2.0f);

    if (this->getImage ().alignment.find ("top") != std::string::npos) {
	this->m_pos.y -= scaledSize.y / 2.0f;
	this->m_pos.w -= scaledSize.y / 2.0f;
    } else if (this->getImage ().alignment.find ("bottom") != std::string::npos) {
	this->m_pos.y += scaledSize.y / 2.0f;
	this->m_pos.w += scaledSize.y / 2.0f;
    }

    if (this->getImage ().alignment.find ("left") != std::string::npos) {
	this->m_pos.x += scaledSize.x / 2.0f;
	this->m_pos.z += scaledSize.x / 2.0f;
    } else if (this->getImage ().alignment.find ("right") != std::string::npos) {
	this->m_pos.x -= scaledSize.x / 2.0f;
	this->m_pos.z -= scaledSize.x / 2.0f;
    }

    this->m_pos.x -= sceneWidth / 2.0f;
    this->m_pos.y = sceneHeight / 2.0f - this->m_pos.y;
    this->m_pos.z -= sceneWidth / 2.0f;
    this->m_pos.w = sceneHeight / 2.0f - this->m_pos.w;
}

void CImage::uploadGeometryBuffers (const glm::vec2& size) {
    GLfloat sceneSpacePosition[] = { this->m_pos.x, this->m_pos.y, 0.0f, this->m_pos.x, this->m_pos.w, 0.0f,
				     this->m_pos.z, this->m_pos.y, 0.0f, this->m_pos.z, this->m_pos.y, 0.0f,
				     this->m_pos.x, this->m_pos.w, 0.0f, this->m_pos.z, this->m_pos.w, 0.0f };

    float width = 1.0f;
    float height = 1.0f;
    if (this->getTexture () != nullptr && !this->getTexture ()->isAnimated ()
	&& (this->getTexture ()->getTextureWidth (0) != this->getTexture ()->getRealWidth ()
	    || this->getTexture ()->getTextureHeight (0) != this->getTexture ()->getRealHeight ())) {
	width = static_cast<float> (this->getTexture ()->getRealWidth ())
	    / static_cast<float> (this->getTexture ()->getTextureWidth (0));
	height = static_cast<float> (this->getTexture ()->getRealHeight ())
	    / static_cast<float> (this->getTexture ()->getTextureHeight (0));
    }

    float x = 0.0f;
    float y = 0.0f;
    GLfloat realWidth = size.x;
    GLfloat realHeight = size.y;
    GLfloat realX = 0.0f;
    GLfloat realY = 0.0f;

    if (this->getImage ().model->passthrough) {
	width = 1.0f;
	height = 1.0f;
	realX = this->m_pos.x;
	realY = this->m_pos.w;
	realWidth = this->m_pos.z;
	realHeight = this->m_pos.y;

	if (this->getImage ().model->fullscreen) {
	    realX = -1.0f;
	    realY = -1.0f;
	    realWidth = 1.0f;
	    realHeight = 1.0f;
	}
    }

    GLfloat texcoordCopy[] = { x, height, x, y, width, height, width, height, x, y, width, y };
    GLfloat copySpacePosition[] = { realX,     realHeight, 0.0f, realX, realY, 0.0f, realWidth, realHeight, 0.0f,
				    realWidth, realHeight, 0.0f, realX, realY, 0.0f, realWidth, realY,      0.0f };

    glBindBuffer (GL_ARRAY_BUFFER, this->m_sceneSpacePosition);
    glBufferData (GL_ARRAY_BUFFER, sizeof (sceneSpacePosition), sceneSpacePosition, GL_DYNAMIC_DRAW);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_copySpacePosition);
    glBufferData (GL_ARRAY_BUFFER, sizeof (copySpacePosition), copySpacePosition, GL_DYNAMIC_DRAW);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_texcoordCopy);
    glBufferData (GL_ARRAY_BUFFER, sizeof (texcoordCopy), texcoordCopy, GL_DYNAMIC_DRAW);

    this->m_sceneCenter
	= glm::vec3 ((this->m_pos.x + this->m_pos.z) / 2.0f, (this->m_pos.y + this->m_pos.w) / 2.0f, 0.0f);
    this->m_modelViewProjectionCopy = this->getImage ().model->passthrough
	? this->m_modelViewProjectionScreen
	: glm::ortho<float> (0.0, size.x, 0.0, size.y);
    this->m_modelViewProjectionCopyInverse = glm::inverse (this->m_modelViewProjectionCopy);
    this->m_modelMatrix = glm::ortho<float> (0.0, size.x, 0.0, size.y);
}

CImage::ResolvedTransform CImage::updateGeometryBuffers () {
    auto sceneWidth = static_cast<float> (this->getScene ().getWidth ());
    auto sceneHeight = static_cast<float> (this->getScene ().getHeight ());
    const auto transform = this->resolveTransform (this->getImage ());
    glm::vec3 origin = transform.origin;
    const glm::vec3 scale = transform.scale;
    const glm::vec2 size = this->resolveGeometrySize (sceneWidth, sceneHeight, origin);
    const glm::vec2 previousSize = this->m_size;
    this->m_size = size;
    if (this->m_hasPuppetMesh && size != previousSize) {
	this->updatePuppetPositionBuffer (size);
    }

    this->updateScenePosition (origin, size, scale, sceneWidth, sceneHeight);
    this->uploadGeometryBuffers (size);
    return transform;
}

void CImage::updateScreenSpacePosition () {
    const ResolvedTransform transform = this->updateGeometryBuffers ();

    // Build rotation from angles (already in radians from scene.json — see CParticle.cpp:2119)
    // Negate X and Z rotations to account for Y-flipped coordinate system (CParticle.cpp:2120)
    const float angle = transform.angle;
    auto rotModel = glm::mat4 (1.0f);
    if (angle != 0.0f) {
	rotModel = glm::translate (rotModel, this->m_sceneCenter);
	rotModel = glm::rotate (rotModel, -angle, glm::vec3 (0.0f, 0.0f, 1.0f));
	rotModel = glm::translate (rotModel, -this->m_sceneCenter);
    }

    glm::mat4 mvp
	= this->getScene ().getCamera ().getProjection () * this->getScene ().getCamera ().getLookAt () * rotModel;

    // Apply parallax displacement if enabled
    if (this->getScene ().getScene ().camera.parallax.enabled
	&& !this->getScene ().getContext ().getApp ().getContext ().settings.mouse.disableparallax) {
	const glm::vec2 depth = this->getImage ().parallaxDepth->value->getVec2 ();
	const glm::vec2* displacement = this->getScene ().getParallaxDisplacement ();
	// displacement is a screen fraction of the camera's orthogonal window, the
	// world region actually visible (not the full projection)
	const auto referenceSize = this->getScene ().getCamera ().getWidth ();
	// parallaxDepth 0 (e.g. fullscreen solid bg): glued to viewport; parallax already baked into
	// displacement (CScene::renderFrame), re-adding it pushes bg off-screen and exposes clear color at edges.
	// Other layers: camera follows cursor, layers move opposite (negated, Wallpaper Engine style);
	// displacement = screen fraction, per-axis scene dimension.
	glm::vec2 offset = glm::vec2 (
	    -depth.x * displacement->x * referenceSize,
	    -depth.y * displacement->y * this->getScene ().getCamera ().getHeight ()
	);
	// a layer covering the shown region must not slide past its own bounds either
	offset = this->getScene ().clampParallaxOffset (offset, this->m_size * glm::vec2 (transform.scale));
	mvp = glm::translate (mvp, { offset.x, offset.y, 0.0f });
    }

    this->m_modelViewProjectionScreen = mvp;
    this->m_modelViewProjectionScreenInverse = glm::inverse (mvp);

    // Local layer pixels (origin top-left, 0..m_size) to clip space. Inverting this
    // gives both the precise hit test and CursorEvent.localPosition, including
    // rotation, scale and parallax.
    glm::mat4 localToWorld = glm::translate (glm::mat4 (1.0f), glm::vec3 (this->m_pos.x, this->m_pos.y, 0.0f));
    localToWorld = glm::scale (localToWorld, glm::vec3 (transform.scale.x, -transform.scale.y, 1.0f));
    this->m_localToScreen = mvp * localToWorld;
    this->m_screenToLocal = glm::inverse (this->m_localToScreen);

    if (this->getImage ().model->passthrough) {
	this->m_modelViewProjectionCopy = this->m_modelViewProjectionScreen;
	this->m_modelViewProjectionCopyInverse = this->m_modelViewProjectionScreenInverse;
    }
}

const Image& CImage::getImage () const { return this->m_image; }

bool CImage::isVisible () const { return this->m_image.visible->value->getBool (); }

bool CImage::hitTest (const glm::vec2& normalizedMouse, glm::vec2* localPosition) const {
    if (!this->m_initialized || !this->m_image.visible->value->getBool () || this->getAlpha () <= 0.0f) {
	return false;
    }

    // Cursor coordinates are top-left based; the scene FBO is presented vertically
    // flipped, so this is also the direction used by the clip-space transform.
    const glm::vec2 ndc = { normalizedMouse.x * 2.0f - 1.0f, normalizedMouse.y * 2.0f - 1.0f };

    // The image is a plane at local z=0. Use the projected centre to recover the
    // matching NDC depth, which keeps perspective scenes usable.
    const glm::vec4 centerClip
	= this->m_localToScreen * glm::vec4 (this->m_size.x * 0.5f, this->m_size.y * 0.5f, 0.0f, 1.0f);

    if (std::abs (centerClip.w) < 1e-6f) {
	return false;
    }

    const float ndcZ = centerClip.z / centerClip.w;
    const glm::vec4 local = this->m_screenToLocal * glm::vec4 (ndc.x, ndc.y, ndcZ, 1.0f);

    if (std::abs (local.w) < 1e-6f) {
	return false;
    }

    const glm::vec2 position = glm::vec2 (local.x, local.y) / local.w;

    if (localPosition != nullptr) {
	*localPosition = position;
    }

    return position.x >= 0.0f && position.y >= 0.0f && position.x <= this->m_size.x && position.y <= this->m_size.y;
}

glm::vec2 CImage::getSize () const {
    if (this->m_size.x > 0.0f && this->m_size.y > 0.0f) {
	return this->m_size;
    }
    if (this->m_texture == nullptr) {
	return this->getImage ().size;
    }

    return { this->m_texture->getRealWidth (), this->m_texture->getRealHeight () };
}

GLuint CImage::getSceneSpacePosition () const { return this->m_sceneSpacePosition; }

GLuint CImage::getCopySpacePosition () const { return this->m_copySpacePosition; }

GLuint CImage::getPassSpacePosition () const { return this->m_passSpacePosition; }

GLuint CImage::getTexCoordCopy () const { return this->m_texcoordCopy; }

GLuint CImage::getTexCoordPass () const { return this->m_texcoordPass; }
