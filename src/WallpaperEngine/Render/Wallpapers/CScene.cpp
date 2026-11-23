#include "WallpaperEngine/Render/Objects/CImage.h"
#include "WallpaperEngine/Render/Objects/CModel.h"
#include "WallpaperEngine/Render/Objects/CParticle.h"
#include "WallpaperEngine/Render/Objects/CSound.h"
#include "WallpaperEngine/Render/Objects/CText.h"

#include "WallpaperEngine/Render/WallpaperState.h"
#include "WallpaperEngine/Scripting/ScriptableObject.h"

#include "CScene.h"
#include "WallpaperEngine/Logging/Log.h"

#include "WallpaperEngine/Data/Model/Wallpaper.h"
#include "WallpaperEngine/Data/Parsers/ObjectParser.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <ranges>

extern float g_Time;
extern float g_TimeLast;

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render;
using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Data::Parsers;
using namespace WallpaperEngine::Render::Wallpapers;

CScene::CScene (
    const Wallpaper& wallpaper, RenderContext& context, AudioContext& audioContext,
    const WallpaperState::TextureUVsScaling& scalingMode, const uint32_t& clampMode, const glm::ivec2& renderSize
) : CWallpaper (wallpaper, context, audioContext, scalingMode, clampMode, renderSize) {
    // caller should check this, if not a std::bad_cast is good to throw
    auto scene = wallpaper.as<Scene> ();

    // setup scripting engine
    this->m_scriptEngine = std::make_unique<Scripting::ScriptEngine> (*this, context.getMediaSource ());
    // setup the scene camera
    this->m_camera = std::make_unique<Camera> (*this, scene->camera);

    float width = static_cast<float> (scene->camera.projection.width);
    float height = static_cast<float> (scene->camera.projection.height);
    const bool isPerspective = scene->camera.projection.isPerspective;

    // 3D scenes have no authored projection size; render at the output resolution
    if (isPerspective) {
	width = static_cast<float> (this->getContext ().getOutput ().getFullWidth ());
	height = static_cast<float> (this->getContext ().getOutput ().getFullHeight ());
    } else if (scene->camera.projection.isAuto) {
	// detect size if the orthogonal project is auto
	glm::vec2 maxExtent = { 0.0f, 0.0f };

	for (const auto& object : scene->objects) {
	    if (!object->is<Image> ()) {
		continue;
	    }

	    const auto* image = object->as<Image> ();
	    if (!image->origin || !image->origin->value) {
		continue;
	    }

	    const glm::vec3 origin = image->origin->value->getVec3 ();
	    const glm::vec2 halfSize = image->size / 2.0f;

	    maxExtent.x = glm::max (maxExtent.x, glm::abs (origin.x) + halfSize.x);
	    maxExtent.y = glm::max (maxExtent.y, glm::abs (origin.y) + halfSize.y);
	}

	if (maxExtent.x > 0.0f && maxExtent.y > 0.0f) {
	    width = maxExtent.x * 2.0f;
	    height = maxExtent.y * 2.0f;
	} else {
	    width = static_cast<float> (this->getContext ().getOutput ().getFullWidth ());
	    height = static_cast<float> (this->getContext ().getOutput ().getFullHeight ());
	    sLog.debug ("Auto projection: falling back to screen resolution ", width, "x", height);
	}
    }

    this->m_parallaxDisplacement = { 0, 0 };

    this->m_projectionWidth = width;
    this->m_projectionHeight = height;

    // pixel size the scene renders at: the output viewport scaled by --render-scale,
    // falling back to the projection size when the caller did not provide one
    this->m_renderWidth = this->getRenderSize ().x > 0 ? static_cast<float> (this->getRenderSize ().x) : width;
    this->m_renderHeight = this->getRenderSize ().y > 0 ? static_cast<float> (this->getRenderSize ().y) : height;

    // TODO: CONVERSION
    // instead of rendering the projection 1:1 into a small FBO and cropping UVs at composite
    // time (which bilinearly upscales pixel art into a blur), adapt the camera's orthogonal
    // window to the render target's aspect ratio so the scene renders directly at screen
    // resolution — the same world region ends up visible, matching Wallpaper Engine semantics
    if (isPerspective) {
	// 3D scenes render through a perspective camera. The composite pass samples
	// the scene FBO with the same UV convention as video textures, so the
	// perspective projection must be mirrored vertically even on X11 (whose
	// XImage/root-pixmap output has no present-time flip). Wayland and GLFW
	// already use a flipped projection, so this is true for every output.
	this->m_camera->setPerspectiveProjection (width, height, true);
    } else {
	float windowWidth = width;
	float windowHeight = height;

	if (this->getState ().getTextureUVsScaling () != WallpaperState::TextureUVsScaling::StretchUVs) {
	    const float m1 = this->m_renderWidth / width;
	    const float m2 = this->m_renderHeight / height;
	    // cover (default/fill) crops the overflowing axis, fit letterboxes with clear color
	    const float m = this->getState ().getTextureUVsScaling () == WallpaperState::TextureUVsScaling::ZoomFitUVs
		? glm::min (m1, m2)
		: glm::max (m1, m2);

	    if (m > 0.0f) {
		windowWidth = this->m_renderWidth / m;
		windowHeight = this->m_renderHeight / m;
	    }
	}

	this->m_camera->setOrthogonalProjection (windowWidth, windowHeight);
    }

    // setup framebuffers here as they're required for the scene setup;
    // 3D scenes depth-test their models, so only their scene framebuffer gets
    // a depth attachment
    this->setupFramebuffers (isPerspective);

    // intermediate render targets follow the render resolution, not the projection:
    // effect shaders address them through g_TexelSize, which tracks the scene FBO
    const uint32_t sceneWidth = static_cast<uint32_t> (glm::max (1.0f, this->m_renderWidth));
    const uint32_t sceneHeight = static_cast<uint32_t> (glm::max (1.0f, this->m_renderHeight));

    this->_rt_shadowAtlas = this->create (
	"_rt_shadowAtlas", TextureFormat_ARGB8888, TextureFlags_ClampUVs, 1.0, { sceneWidth, sceneHeight },
	{ sceneWidth, sceneHeight }
    );
    this->alias ("_alias_lightCookie", "_rt_shadowAtlas");

    // model materials sample the reflection render target (e.g. the grid in
    // Fantastic Car); the pass below fills it with the mirrored reflected
    // models. It needs its own depth buffer because models depth-test inside it.
    if (isPerspective) {
	this->_rt_Reflection = this->create (
	    "_rt_Reflection", TextureFormat_ARGB8888, TextureFlags_ClampUVs, 1.0, { sceneWidth, sceneHeight },
	    { sceneWidth, sceneHeight }, true
	);
    }

    // set clear color
    const glm::vec3 clearColor = scene->colors.clear->value->getVec3 ();

    this->m_clearColor = clearColor;
    glClearColor (clearColor.r, clearColor.g, clearColor.b, 1.0f);

    // create all objects based off their dependencies
    for (const auto& object : scene->objects) {
	this->createObject (*object);
    }

    // copy over objects by render order
    for (const auto& object : scene->objects) {
	this->addObjectToRenderOrder (*object);
    }

    // reflection state is static, resolve it once the objects exist
    this->setupReflection ();

    // create extra framebuffers for the bloom effect
    this->_rt_4FrameBuffer = this->create (
	"_rt_4FrameBuffer", TextureFormat_ARGB8888, TextureFlags_ClampUVs, 1.0, { sceneWidth / 4, sceneHeight / 4 },
	{ sceneWidth / 4, sceneHeight / 4 }
    );
    this->_rt_8FrameBuffer = this->create (
	"_rt_8FrameBuffer", TextureFormat_ARGB8888, TextureFlags_ClampUVs, 1.0, { sceneWidth / 8, sceneHeight / 8 },
	{ sceneWidth / 8, sceneHeight / 8 }
    );
    this->_rt_Bloom = this->create (
	"_rt_Bloom", TextureFormat_ARGB8888, TextureFlags_ClampUVs, 1.0, { sceneWidth / 8, sceneHeight / 8 },
	{ sceneWidth / 8, sceneHeight / 8 }
    );

    //
    // Had to get a little creative with the effects to achieve the same bloom effect without any custom code
    // this custom image loads some effect files from the virtual container to achieve the same bloom effect
    // this approach requires of two extra draw calls due to the way the effect works in official WPE
    // (it renders directly to the screen, whereas here we never do that from a scene)
    //

    const auto bloomOrigin = glm::vec3 { sceneWidth / 2, sceneHeight / 2, 0.0f };
    const auto bloomSize = glm::vec2 { sceneWidth, sceneHeight };

    const JSON bloom
	= { { "image", "models/wpenginelinux.json" },
	    { "name", "bloomimagewpenginelinux" },
	    { "visible", true },
	    { "scale", "1.0 1.0 1.0" },
	    { "angles", "0.0 0.0 0.0" },
	    { "origin",
	      std::to_string (bloomOrigin.x) + " " + std::to_string (bloomOrigin.y) + " "
		  + std::to_string (bloomOrigin.z) },
	    { "size", std::to_string (bloomSize.x) + " " + std::to_string (bloomSize.y) },
	    { "id", -1 },
	    { "effects",
	      JSON::array (
		  { { { "file", "effects/wpenginelinux/bloomeffect.json" },
		      { "id", 15242000 },
		      { "name", "" },
		      { "passes",
			JSON::array (
			    { { { "constantshadervalues",
				  { { "bloomstrength", this->getScene ().camera.bloom.strength->value->getFloat () },
				    { "bloomthreshold",
				      this->getScene ().camera.bloom.threshold->value->getFloat () } } } },
			      { { "constantshadervalues",
				  { { "bloomstrength", this->getScene ().camera.bloom.strength->value->getFloat () },
				    { "bloomthreshold",
				      this->getScene ().camera.bloom.threshold->value->getFloat () } } } },
			      { { "constantshadervalues",
				  { { "bloomstrength", this->getScene ().camera.bloom.strength->value->getFloat () },
				    { "bloomthreshold",
				      this->getScene ().camera.bloom.threshold->value->getFloat () } } } } }
			) } } }
	      ) } };

    // create image for bloom passes. HDR bloom is an additional Ultra-quality
    // effect in Wallpaper Engine; the normal bloom configured in the scene is
    // still used by everyone without Ultra post-processing, so it must stay
    // enabled for HDR scenes as well.
    if (scene->camera.bloom.enabled->value->getBool ()) {
	this->m_bloomObjectData = ObjectParser::parse (bloom, scene->project);
	this->m_bloomObject = this->createObject (*this->m_bloomObjectData);

	// createObject() can return null when the object fails to setup (e.g. shader errors),
	if (this->m_bloomObject != nullptr) {
	    this->m_objectsByRenderOrder.push_back (this->m_bloomObject);
	} else {
	    sLog.error ("Bloom object failed to setup, bloom disabled for this wallpaper");
	}
    }

    this->m_scriptEngine->initialize ();
}

CScene::~CScene () {
    // bloom object is in the objects list, so no need to explicitly delete it
    this->m_bloomObject = nullptr;

    for (const auto& val : this->m_objects | std::views::values) {
	delete val;
    }

    this->m_objectsByRenderOrder.clear ();
    this->m_objects.clear ();
}

Render::CObject* CScene::createObject (const Object& object) {
    Render::CObject* renderObject = nullptr;

    // ensure the item is not loaded already
    if (const auto current = this->m_objects.find (object.id); current != this->m_objects.end ()) {
	return current->second;
    }

    // check dependencies too!
    for (const auto& cur : object.dependencies) {
	// self-dependency is a possibility...
	if (cur == object.id) {
	    continue;
	}

	const auto dep
	    = std::ranges::find_if (this->getScene ().objects, [&cur] (const auto& o) { return o->id == cur; });

	if (dep != this->getScene ().objects.end ()) {
	    this->createObject (**dep);
	}
    }

    // check if the item has any parent and also create it first
    if (object.parent.has_value ()) {
	int parentId = object.parent.value ();

	const auto dep = std::ranges::find_if (this->getScene ().objects, [&parentId] (const auto& o) {
	    return o->id == parentId;
	});

	if (dep == this->getScene ().objects.end ()) {
	    sLog.exception ("Cannot find parent ", parentId, " for object ", object.id);
	}

	this->createObject (**dep);
    }

    renderObject = this->dispatchObjectType (object);

    if (renderObject != nullptr) {
	this->m_objects.emplace (renderObject->getId (), renderObject);
    }

    return renderObject;
}

Render::CObject* CScene::dispatchObjectType (const Object& object) {
    Render::CObject* renderObject = nullptr;

    if (object.is<Image> ()) {
	renderObject = new Objects::CImage (*this, *object.as<Image> ());
    } else if (object.is<Sound> ()) {
	renderObject = new Objects::CSound (*this, *object.as<Sound> ());
    } else if (object.is<Text> ()) {
	renderObject = new Objects::CText (*this, *object.as<Text> ());
    } else if (object.is<CameraObject> ()) {
	// camera objects carry no geometry; their pose/path was consumed by the
	// parser before the scene started
	return nullptr;
    } else if (object.is<ModelObject> ()) {
	renderObject = new Objects::CModel (*this, *object.as<ModelObject> ());
    } else if (object.is<LightObject> ()) {
	// lights carry no visual; their state feeds shader uniforms in a later slice
	return nullptr;
    } else if (object.is<Particle> ()) {
	const auto& particleData = *object.as<Particle> ();

	if (this->getContext ().getApp ().getContext ().settings.general.disableParticles == true) {
	    sLog.debug ("Ignoring particle system (disabled in settings): ", particleData.name);
	    return nullptr;
	}

	if (particleData.material == nullptr || particleData.material->material == nullptr) {
	    sLog.error ("Ignoring particle system without a material: ", particleData.name);
	    return nullptr;
	}

	renderObject = new Objects::CParticle (*this, particleData);
    } else {
	sLog.error ("Unknown object type, creating placeholder, empty object: ", object.id);
	renderObject = new CObject (*this, object);
    }

    try {
	renderObject->setup ();
    } catch (const std::exception& e) {
	sLog.error ("Failed to setup object ", object.id, ": ", e.what ());
	delete renderObject;
	renderObject = nullptr;
    }

    return renderObject;
}

void CScene::addObjectToRenderOrder (const Object& object) {
    const auto obj = this->m_objects.find (object.id);

    // ignores not created objects like particle systems
    if (obj == this->m_objects.end ()) {
	return;
    }

    // take into account any dependency first
    for (const auto& dep : object.dependencies) {
	// self-dependency is possible
	if (dep == object.id) {
	    continue;
	}

	// add the dependency to the list if it's created
	auto depIt = std::ranges::find_if (this->getScene ().objects, [&dep] (const auto& o) { return o->id == dep; });

	if (depIt != this->getScene ().objects.end ()) {
	    this->addObjectToRenderOrder (**depIt);
	} else {
	    sLog.error ("Cannot find dependency ", dep, " for object ", object.id);
	}
    }

    // ensure we're added only once to the render list
    const auto renderIt = std::ranges::find_if (this->m_objectsByRenderOrder, [&object] (const auto& o) {
	return o->getId () == object.id;
    });

    if (renderIt == this->m_objectsByRenderOrder.end ()) {
	this->m_objectsByRenderOrder.emplace_back (obj->second);
    }
}

void CScene::sortModelsByDepth () {
    // 2d scenes use the array order as their draw order; only perspective
    // scenes have real 3d depth to sort against
    if (this->m_camera == nullptr || this->m_camera->isOrthogonal ()) {
	return;
    }

    const glm::vec3 eye = this->m_camera->getEye ();
    const glm::vec3 view = this->m_camera->getCenter () - eye;

    if (glm::dot (view, view) <= 0.0f) {
	return;
    }

    const glm::vec3 forward = glm::normalize (view);

    struct ModelDepth {
	Objects::CModel* model;
	float depth;
    };

    std::vector<ModelDepth> models = {};
    models.reserve (this->m_objectsByRenderOrder.size ());

    for (CObject* object : this->m_objectsByRenderOrder) {
	if (!object->is<Objects::CModel> ()) {
	    continue;
	}

	auto* model = object->as<Objects::CModel> ();

	// Only alpha-blended models need back-to-front ordering. Opaque models
	// rely on depth testing, and additive passes are order-independent.
	// Background models often disable depth writes while using normal
	// blending; sorting those by origin can put them after foreground
	// geometry and paint over it (dna_fragment does exactly that).
	if (!model->needsDepthSort ()) {
	    continue;
	}

	// camera-space depth: distance along the view direction, not the
	// euclidean distance, so objects outside the frustum sort consistently
	models.push_back ({ model, glm::dot (model->getWorldOrigin () - eye, forward) });
    }

    if (models.size () < 2) {
	return;
    }

    // back-to-front: the farthest alpha-blended model first so overlapping
    // surfaces blend over the geometry behind them. Opaque, additive and
    // screen-space layers keep their authored slots.
    std::ranges::stable_sort (models, std::greater {}, &ModelDepth::depth);

    auto nextModel = models.begin ();
    for (CObject*& object : this->m_objectsByRenderOrder) {
	if (!object->is<Objects::CModel> () || !object->as<Objects::CModel> ()->needsDepthSort ()) {
	    continue;
	}

	object = (nextModel++)->model;
    }
}

void CScene::updateLights () {
    this->m_lightPositions.fill (glm::vec3 (0.0f));
    this->m_lightColorsRadius.fill (glm::vec4 (0.0f));

    int slot = 0;

    for (const auto& object : this->getScene ().objects) {
	if (!object->is<LightObject> () || slot >= kMaxShaderLights) {
	    continue;
	}

	const auto& light = *object->as<LightObject> ();

	if (!light.groupVisible->value->getBool ()) {
	    continue;
	}

	// WE packs the shader light as color * intensity with the falloff
	// radius in the alpha slot (ComputeLightSpecular's linear attenuation)
	const glm::vec3 color = light.color->value->getVec3 () * light.intensity->value->getFloat ();

	this->m_lightPositions[slot] = light.origin->value->getVec3 ();
	this->m_lightColorsRadius[slot] = glm::vec4 (color, light.radius->value->getFloat ());
	slot++;
    }
}

ScriptEngine& CScene::getScriptEngine () const { return *this->m_scriptEngine; }
Camera& CScene::getCamera () const { return *this->m_camera; }

void CScene::renderFrame (const glm::ivec4& viewport) {
    // ensure the virtual mouse position is up to date
    this->updateMouse (viewport);
    this->dispatchCursorEvents ();

    // update the parallax position if required
    if (this->getScene ().camera.parallax.enabled->value->getBool ()
	&& !this->getContext ().getApp ().getContext ().settings.mouse.disableparallax) {
	const float influence = this->getScene ().camera.parallax.mouseInfluence->value->getFloat ();
	const float amount = this->getScene ().camera.parallax.amount->value->getFloat ();
	const float delay = this->getScene ().camera.parallax.delay->value->getFloat ();
	// Camera parallax parameter conventions and tuning references:
	//
	//   cameraparallaxdelay           smoothing time in seconds (WE editor range 0..2, default 0.1)
	//   cameraparallaxamount          overall strength (WE editor range 0..2)
	//   cameraparallaxmouseinfluence  mouse influence (WE editor range -1..1; 0 disables it,
	//                                 negative values invert the movement direction)
	//   parallaxDepth (per layer)     X/Y strength; 0 disables that axis, negative inverts it
	//
	// Delay is a time constant, not a rate. Use first-order exponential smoothing:
	//   alpha = 1 - exp(-dt / delay), with delay <= 0 snapping to the target immediately.
	// The old `delay * dt` lerp is equivalent to tau = 1/delay: the common 0.1 value produced
	// a 10 s lag while 2.0 produced 0.5 s, so larger authored delays moved faster.
	//
	// Sources:
	//   - WE editor Delay slider range 0..2 s (registration of
	//     ui_editor_scene_options_camera_parallax_delay in bin/wallpaperui.exe: min 0.0, max 2.0)
	//   - https://docs.wallpaperengine.io/en/scene/parallax/introduction.html
	//   - upstream PR Almamu/linux-wallpaperengine#741 (delay in seconds, exp smoothing)
	//   - upstream PR Almamu/linux-wallpaperengine#624 uses delay * 0.1; not adopted because it compresses the
	//     editor's 0..2 s range to 0..0.2 s (0.1 would be only 10 ms)
	const float timeConstant = delay;
	const float dt = g_Time - g_TimeLast;
	const float response = timeConstant > 0.0f ? 1.0f - std::exp (-dt / timeConstant) : 1.0f;

	// m_mousePosition is scene UV with y top-down; center it so the pointer spans -0.5..0.5.
	// With mouse input disabled the position reads as {0,0}, which would sit the virtual
	// cursor at a screen corner and apply a constant saturated displacement; center it instead.
	const glm::vec2 centeredMouse = this->getContext ().getApp ().getContext ().settings.mouse.enabled
	    ? this->m_mousePosition - glm::vec2 (0.5f, 0.5f)
	    : glm::vec2 (0.0f, 0.0f);

	// The smoothed pointer is shared by layer displacement and g_ParallaxPosition.
	this->m_parallaxPointer = glm::mix (this->m_parallaxPointer, centeredMouse, response);
	// Layer displacement is a screen fraction and already contains amount and influence;
	// CImage/CParticle multiply it by the layer's authored parallaxDepth.
	this->m_parallaxDisplacement = (this->m_parallaxPointer * amount) * influence;

	// Shader-driven depth effects (built-in effects/depthparallax) sample g_ParallaxPosition.
	// WE feeds a -1..1 centered pointer scaled by mouse influence and camera parallax amount,
	// with y flipped for the shader-space viewport direction. m_parallaxPointer is -0.5..0.5,
	// hence the *2. The 0.5 center makes an unscaled/no-parallax value neutral.
	this->m_parallaxPosition = glm::vec2 (
	    0.5f + (this->m_parallaxPointer.x * 2.0f) * influence * amount,
	    0.5f - (this->m_parallaxPointer.y * 2.0f) * influence * amount
	);
    } else {
	// neutral pointer so shaders that sample g_ParallaxPosition do not keep a stale offset
	this->m_parallaxPointer = glm::vec2 (0.0f);
	this->m_parallaxDisplacement = glm::vec2 (0.0f);
	this->m_parallaxPosition = glm::vec2 (0.5f);
    }

    // run a tick in the javascript logic
    this->getScriptEngine ().tick ();

    // advance the camera path animation before drawing anything
    this->m_camera->tick ();

    // depth order the 3d objects for the current camera pose; translucent
    // surfaces only composite correctly when they are drawn back-to-front
    this->sortModelsByDepth ();

    // refresh the shader light arrays so materials pick up current light state
    this->updateLights ();

    // update main textures for images
    for (const auto& cur : this->m_objectsByRenderOrder) {
	if (!cur->is<Objects::CImage> ()) {
	    continue;
	}

	const Objects::CImage* image = cur->as<Objects::CImage> ();

#if !NDEBUG
	const std::string message = "Updating texture " + image->getImage ().model->filename;

	glPushDebugGroup (GL_DEBUG_SOURCE_APPLICATION, 0, -1, message.c_str ());
#endif

	image->getTexture ()->update ();

#if !NDEBUG
	glPopDebugGroup ();
#endif
    }

    // render the mirrored models into _rt_Reflection before the scene itself;
    // mirror-surface materials sample it while they draw
    this->renderReflection ();

    // bind the vertex array
    glBindVertexArray (this->m_vaoBuffer);
    // use the scene's framebuffer by default
    glBindFramebuffer (GL_FRAMEBUFFER, this->getWallpaperFramebuffer ());
    // ensure we render over the whole framebuffer
    glViewport (0, 0, this->m_sceneFBO->getRealWidth (), this->m_sceneFBO->getRealHeight ());

    // depth clear is ignored while the depth mask is off (the last pass of a
    // frame may leave it disabled, e.g. translucent model materials)
    glDepthMask (GL_TRUE);
    glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    for (const auto& cur : this->m_objectsByRenderOrder) {
	if (!cur->isVisibleInTree ()) {
	    continue;
	}

	const auto& debug = this->getContext ().getApp ().getContext ().settings.render.debug;
	if (debug.objectFilter.has_value () && cur->getId () != debug.objectFilter.value ()) {
	    continue;
	}
	if (std::ranges::find (debug.skipObjects, cur->getId ()) != debug.skipObjects.end ()) {
	    continue;
	}

	// text/model children of a capturing composition layer are rendered into
	// the layer's capture target, not directly into the scene; image children
	// never have a capturing parent (the layer skips itself instead)
	if ((cur->is<Objects::CText> () || cur->is<Objects::CModel> ()) && this->isCapturedByCompositionParent (*cur)) {
	    continue;
	}

	cur->render ();
    }
}

bool CScene::isCapturedByCompositionParent (const CObject& object) const {
    if (!object.getObject ().parent.has_value ()) {
	return false;
    }

    const auto* parent = this->getObject (object.getObject ().parent.value ());

    return parent != nullptr && parent->is<Objects::CImage> () && parent->as<Objects::CImage> ()->capturesChildren ();
}

void CScene::setupReflection () {
    // only perspective scenes have a reflection target to fill
    if (this->_rt_Reflection == nullptr) {
	return;
    }

    for (auto* object : this->m_objectsByRenderOrder) {
	if (!object->is<Objects::CModel> ()) {
	    continue;
	}

	auto* model = object->as<Objects::CModel> ();

	if (model->getModel ().reflected) {
	    this->m_reflectedModels.push_back (model);
	}

	// the first surface sampling the target defines the mirror plane
	// (WE scenes have a single floor)
	if (!this->m_reflectionPlaneY.has_value () && model->samplesReflection ()) {
	    this->m_reflectionPlaneY = model->getWorldOriginY ();
	}
    }

    if (!this->m_reflectionPlaneY.has_value ()) {
	this->m_reflectedModels.clear ();
    }
}

void CScene::renderReflection () {
    if (this->_rt_Reflection == nullptr || !this->m_reflectionPlaneY.has_value () || this->m_reflectedModels.empty ()) {
	return;
    }

    // the reflection is the original camera viewing the world mirrored over
    // the mirror plane; the model matrices carry the mirror (see CModel) so
    // the camera and its uniforms stay untouched
    glm::mat4 mirror = glm::mat4 (1.0f);
    mirror[1][1] = -1.0f;
    mirror[3][1] = 2.0f * *this->m_reflectionPlaneY;

    glBindFramebuffer (GL_FRAMEBUFFER, this->_rt_Reflection->getFramebuffer ());
    glViewport (
	0, 0, static_cast<GLsizei> (this->_rt_Reflection->getRealWidth ()),
	static_cast<GLsizei> (this->_rt_Reflection->getRealHeight ())
    );

    glDepthMask (GL_TRUE);
    glClearColor (this->m_clearColor.r, this->m_clearColor.g, this->m_clearColor.b, 1.0f);
    glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // the world mirror reverses triangle orientation, hand the winding back
    // so backface culling keeps culling the same faces as the main pass
    glFrontFace (GL_CW);

    for (auto* model : this->m_reflectedModels) {
	model->render (this->_rt_Reflection, mirror);
    }

    glFrontFace (GL_CCW);
}

void CScene::updateMouse (const glm::ivec4& viewport) {
    // With mouse input disabled there is no reliable pointer position; use the
    // center of the visible UV region so scripts that follow the cursor start
    // from a neutral pose instead of saturating at a screen corner. Set the
    // previous position to the same value to avoid a synthetic first-frame
    // pointer delta (g_PointerPositionLast and cursor-velocity scripts).
    if (!this->getContext ().getApp ().getContext ().settings.mouse.enabled) {
	const auto uvs = this->getState ().getTextureUVs ();
	const glm::vec2 center = {
	    (uvs.ustart + uvs.uend) * 0.5f,
	    (uvs.vstart + uvs.vend) * 0.5f,
	};

	this->m_mousePosition = center;
	this->m_mousePositionLast = center;
	this->m_mousePositionNormalized = center;
	this->m_mousePositionViewport = { 0.5f, 0.5f };
	return;
    }

    // update virtual mouse position first
    const glm::dvec2 position = this->getContext ().getInputContext ().getMouseInput ().position ();

    // rollover the position to the last
    this->m_mousePositionLast = this->m_mousePosition;

    // calculate the current position of the mouse in viewport space [0, 1]
    double mouseX = glm::clamp ((position.x - viewport.x) / viewport.z, 0.0, 1.0);
    // Normalize Y coordinate (OpenGL convention: 0=bottom, 1=top)
    // Particle code expects this convention: 0=bottom results in negative Y (down), 1=top results in positive Y (up)
    double normalizedMouseY = glm::clamp ((position.y - viewport.y) / viewport.w, 0.0, 1.0);

    // SceneScript cursor events use a top-left origin
    this->m_mousePositionViewport = { mouseX, 1.0 - normalizedMouseY };

    // Account for UV cropping when using fill/fit scaling modes
    // The scene may be rendered larger than viewport and cropped via UVs
    const auto uvs = this->getState ().getTextureUVs ();

    // Map mouse position from viewport space to scene UV space
    // UVs define what portion of the scene texture is visible
    this->m_mousePositionNormalized.x = uvs.ustart + mouseX * (uvs.uend - uvs.ustart);
    this->m_mousePositionNormalized.y = uvs.vstart + normalizedMouseY * (uvs.vend - uvs.vstart);

    // Invert previous normalization of Y to match what the shader expects
    double mouseY = 1.0 - normalizedMouseY;

    this->m_mousePosition.x = this->m_mousePositionNormalized.x;
    this->m_mousePosition.y = uvs.vstart + mouseY * (uvs.vend - uvs.vstart);
}

void CScene::dispatchCursorEvents () {
    if (!this->getContext ().getApp ().getContext ().settings.mouse.enabled) {
	return;
    }

    // the last object in render order is the top-most one
    Scripting::ScriptableObject* target = nullptr;
    glm::vec2 targetLocal = {};

    for (auto it = this->m_objectsByRenderOrder.rbegin (); it != this->m_objectsByRenderOrder.rend (); ++it) {
	auto* scriptable = dynamic_cast<Scripting::ScriptableObject*> (*it);

	if (!(*it)->isVisibleInTree ()) {
	    continue;
	}

	// non-scripted layers cannot receive cursor events, so avoid hit-testing them
	if (scriptable == nullptr || !this->m_scriptEngine->hasCursorHandler (*scriptable)) {
	    continue;
	}

	if ((*it)->hitTest (this->m_mousePositionViewport, &targetLocal)) {
	    target = scriptable;
	    break;
	}
    }

    const glm::vec3 worldPosition = this->getMousePositionWorld ();

    if (target != this->m_cursorTarget) {
	if (this->m_cursorTarget != nullptr) {
	    this->m_scriptEngine->dispatchCursorEvent (
		*this->m_cursorTarget, "cursorLeave", worldPosition, this->m_cursorTargetLocal
	    );
	}

	if (target != nullptr) {
	    this->m_scriptEngine->dispatchCursorEvent (*target, "cursorEnter", worldPosition, targetLocal);
	}

	this->m_cursorTarget = target;
	this->m_cursorTargetLocal = targetLocal;
    }

    const bool moved = this->m_mousePositionViewport != this->m_lastCursorEventPosition;

    if (target != nullptr && moved) {
	this->m_scriptEngine->dispatchCursorEvent (*target, "cursorMove", worldPosition, targetLocal);
    }

    if (moved) {
	this->m_lastCursorEventPosition = this->m_mousePositionViewport;
    }

    if (target != nullptr) {
	this->m_cursorTargetLocal = targetLocal;
    }

    const bool leftDown = this->getContext ().getInputContext ().getMouseInput ().leftClick ()
	== WallpaperEngine::Input::MouseClickStatus::Clicked;

    if (leftDown && !this->m_mouseLeftDown) {
	this->m_cursorPressTarget = target;

	if (target != nullptr) {
	    this->m_scriptEngine->dispatchCursorEvent (*target, "cursorDown", worldPosition, targetLocal);
	}
    } else if (!leftDown && this->m_mouseLeftDown) {
	if (this->m_cursorPressTarget != nullptr) {
	    glm::vec2 pressLocal = {};

	    // even when the cursor left the object this still yields its local
	    // coordinates, which is what cursorUp/cursorClick should report. The
	    // boolean result is intentionally discarded.
	    (void)this->m_cursorPressTarget->hitTest (this->m_mousePositionViewport, &pressLocal);

	    this->m_scriptEngine->dispatchCursorEvent (
		*this->m_cursorPressTarget, "cursorUp", worldPosition, pressLocal
	    );

	    // cursorClick only exists when press and release happened on the same object
	    if (target == this->m_cursorPressTarget) {
		this->m_scriptEngine->dispatchCursorEvent (
		    *this->m_cursorPressTarget, "cursorClick", worldPosition, pressLocal
		);
	    }
	}

	this->m_cursorPressTarget = nullptr;
    }

    this->m_mouseLeftDown = leftDown;
}

const Scene& CScene::getScene () const { return *this->getWallpaperData ().as<Scene> (); }

int CScene::getWidth () const { return static_cast<int> (this->m_projectionWidth); }

int CScene::getHeight () const { return static_cast<int> (this->m_projectionHeight); }

const Properties& CScene::getUserProperties () const { return this->m_wallpaperData.project.properties; }

const Project& CScene::getProject () const { return this->m_wallpaperData.project; }

glm::ivec2 CScene::getScreenResolution () const {
    const auto& output = this->getContext ().getOutput ();

    return { output.getFullWidth (), output.getFullHeight () };
}

const glm::vec2* CScene::getTexelSize () const { return &this->m_texelSize; }

const glm::vec2* CScene::getTexelSizeHalf () const { return &this->m_texelSizeHalf; }

void CScene::setupFramebuffers (bool sceneDepthBuffer) {
    const auto width = static_cast<uint32_t> (glm::max (1.0f, this->m_renderWidth));
    const auto height = static_cast<uint32_t> (glm::max (1.0f, this->m_renderHeight));
    const uint32_t clamp = this->getState ().getClampingMode ();

    // create framebuffer for the scene at the render resolution, scenes render
    // directly at screen resolution instead of the projection size
    this->m_sceneFBO = this->create (
	"_rt_FullFrameBuffer", TextureFormat_ARGB8888, clamp, 1.0, { width, height }, { width, height },
	sceneDepthBuffer
    );

    this->alias ("_rt_MipMappedFrameBuffer", "_rt_FullFrameBuffer");

    this->m_texelSize = glm::vec2 (1.0f / static_cast<float> (width), 1.0f / static_cast<float> (height));
    this->m_texelSizeHalf = this->m_texelSize * 0.5f;
}

void CScene::updateUVs (const glm::ivec4& viewport, const bool vflip) {
    // the scene FBO matches the output viewport, so the composite maps it 1:1 and
    // any aspect adaptation already happened in the camera's orthogonal window
    this->updateStateUVs (
	viewport, vflip, static_cast<int> (this->m_renderWidth), static_cast<int> (this->m_renderHeight)
    );
}

float CScene::getTime () const { return g_Time; }

float CScene::getDeltaTime () const { return g_Time - g_TimeLast; }

float CScene::getFps () const {
    const float dt = g_Time - g_TimeLast;
    // Guard against the first frame (where g_TimeLast is 0 so dt == g_Time)
    // and division by zero on the very first call.
    if (dt <= 1e-6f) {
	return 60.0f;
    }
    return 1.0f / dt;
}

const glm::vec2* CScene::getMousePosition () const { return &this->m_mousePosition; }

const glm::vec2* CScene::getMousePositionLast () const { return &this->m_mousePositionLast; }

const glm::vec2* CScene::getMousePositionNormalized () const { return &this->m_mousePositionNormalized; }

glm::vec3 CScene::mapMousePositionToWorld (
    const glm::vec2& mousePosition, float ustart, float uend, float vstart, float vend, float sceneWidth,
    float sceneHeight, float shownWidth, float shownHeight
) {
    // Undo the UV crop to recover the viewport fractions, then map them onto the world region the
    // camera actually shows. Scaling by the authored projection size alone breaks whenever the
    // camera window is cropped (cover/fill on a non-matching aspect ratio): the cursor would move
    // faster than the mouse on the cropped axis.
    const float uRange = uend - ustart;
    const float vRange = vend - vstart;
    const float u = uRange != 0.0f ? (mousePosition.x - ustart) / uRange : 0.5f;
    const float v = vRange != 0.0f ? (mousePosition.y - vstart) / vRange : 0.5f;

    // y keeps the orientation layer origins use for SceneScript (measured from the bottom); only
    // the visible range is scaled by the camera window.
    return {
	sceneWidth / 2.0f + (u - 0.5f) * shownWidth,
	sceneHeight / 2.0f - (v - 0.5f) * shownHeight,
	0.0f,
    };
}

glm::vec3 CScene::getMousePositionWorld () const {
    // https://docs.wallpaperengine.io/en/scene/scenescript/reference/class/IInput.html
    const auto uvs = this->getState ().getTextureUVs ();

    return mapMousePositionToWorld (
	this->m_mousePosition, uvs.ustart, uvs.uend, uvs.vstart, uvs.vend, static_cast<float> (this->getWidth ()),
	static_cast<float> (this->getHeight ()), this->m_camera->getWidth (), this->m_camera->getHeight ()
    );
}

const glm::vec2* CScene::getParallaxDisplacement () const { return &this->m_parallaxDisplacement; }

const glm::vec2* CScene::getParallaxPosition () const { return &this->m_parallaxPosition; }

glm::vec2 CScene::clampParallaxOffset (const glm::vec2& offset, const glm::vec2& scaledSize) const {
    // the camera's orthogonal window is the world region actually shown on screen,
    // so a layer that covers it must not be displaced past its own bounds
    const float shownWidth = this->m_camera->getWidth ();
    const float shownHeight = this->m_camera->getHeight ();

    glm::vec2 result = offset;

    // a layer that covers the shown region must not be displaced past its own
    // bounds, otherwise the clear color becomes visible around the edges. zero
    // margin (stretch mode, aspect-equal output) pins the layer in place.
    // smaller (interior) layers have negative margins and keep their full parallax
    const float marginX = (scaledSize.x - shownWidth) / 2.0f;
    const float marginY = (scaledSize.y - shownHeight) / 2.0f;

    if (marginX >= 0.0f) {
	result.x = glm::clamp (result.x, -marginX, marginX);
    }
    if (marginY >= 0.0f) {
	result.y = glm::clamp (result.y, -marginY, marginY);
    }

    return result;
}

const std::vector<CObject*>& CScene::getObjectsByRenderOrder () const { return this->m_objectsByRenderOrder; }

const CObject* CScene::getObject (int id) const {
    const auto object = this->m_objects.find (id);
    return object == this->m_objects.end () ? nullptr : object->second;
}

CObject* CScene::getObject (int id) {
    const auto object = this->m_objects.find (id);
    return object == this->m_objects.end () ? nullptr : object->second;
}

CObject* CScene::getObjectByName (const std::string& name) const {
    // Layer scripts resolve other layers by name. Render order is not populated
    // yet while object constructors run their scripts, so search every object
    // created so far instead of only the draw list.
    for (const auto& [id, object] : this->m_objects) {
	if (object != nullptr && object->getObject ().name == name) {
	    return object;
	}
    }

    return nullptr;
}

CObject* CScene::createLayerFromAsset (const std::string& asset) {
    if (asset.empty ()) {
	return nullptr;
    }

    const int id = this->m_nextDynamicObjectId--;
    const bool isParticle = asset.rfind ("particles/", 0) == 0;
    JSON layer = {
	{ "id", id },          { "name", "dynamic_" + std::to_string (id) },
	{ "origin", "0 0 0" }, { "scale", "1 1 1" },
	{ "angles", "0 0 0" }, { "visible", true },
    };

    layer[isParticle ? "particle" : "image"] = asset;

    try {
	auto data = ObjectParser::parse (layer, this->getScene ().project);
	auto* object = this->createObject (*data);

	if (object == nullptr) {
	    return nullptr;
	}

	this->addObjectToRenderOrder (*data);
	this->m_dynamicObjects.emplace (id, std::move (data));

	return object;
    } catch (const std::exception& e) {
	sLog.error ("Cannot create layer from asset ", asset, ": ", e.what ());
	return nullptr;
    }
}

bool CScene::destroyLayer (CObject* object) {
    if (object == nullptr) {
	return false;
    }

    const int id = object->getId ();

    if (this->m_dynamicObjects.find (id) == this->m_dynamicObjects.end ()) {
	return false;
    }

    if (this->m_cursorTarget == object) {
	this->m_cursorTarget = nullptr;
    }
    if (this->m_cursorPressTarget == object) {
	this->m_cursorPressTarget = nullptr;
    }

    this->m_objects.erase (id);

    const auto renderIt = std::ranges::find (this->m_objectsByRenderOrder, object);
    if (renderIt != this->m_objectsByRenderOrder.end ()) {
	this->m_objectsByRenderOrder.erase (renderIt);
    }

    delete object;
    this->m_dynamicObjects.erase (id);

    return true;
}

void CScene::sortLayer (CObject* object, int index) {
    if (object == nullptr) {
	return;
    }

    const auto current = std::ranges::find (this->m_objectsByRenderOrder, object);

    if (current == this->m_objectsByRenderOrder.end ()) {
	return;
    }

    this->m_objectsByRenderOrder.erase (current);

    const auto size = static_cast<int> (this->m_objectsByRenderOrder.size ());
    index = std::clamp (index, 0, size);
    this->m_objectsByRenderOrder.insert (this->m_objectsByRenderOrder.begin () + index, object);
}
